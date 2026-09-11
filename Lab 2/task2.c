/*
 * task2.c - Hybrid MPI + OpenMP Prime Search
 *
 * This program finds all prime numbers strictly less than n
 * using a hybrid MPI + OpenMP approach.
 *
 * MPI is used to distribute the overall candidate range
 * between processes, while OpenMP is used to further divide
 * each process's range between its threads.
 *
 * The root MPI process:
 *   1. Reads n from the command line.
 *   2. Broadcasts n to all MPI processes.
 *   3. Receives the results from all processes.
 *   4. Writes the final sorted list of primes to a file.
 *
 * Each MPI process:
 *   1. Receives its candidate range from the MPI workload
 *      distribution.
 *   2. Uses OpenMP threads to search its range in parallel.
 *   3. Stores results in thread-private arrays.
 *
 * Compile:
 *   mpicc -fopenmp Task2.c -o Task2 -lm
 *
 * Run:
 *   OMP_NUM_THREADS=2 mpirun -np 4 ./Task2 10000000
 */

#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <mpi.h>
#include <omp.h> 

/*
 * Check whether k is prime.
 *
 * Returns:
 *   1 -> k is prime
 *   0 -> k is not prime
 *
 * Optimisations:
 *   - Numbers less than 2 are not prime.
 *   - 2 is the only even prime.
 *   - Other even numbers can be rejected immediately.
 *   - Only odd divisors up to sqrt(k) need to be tested.
 */
int is_prime(int k)
{
    int i;

    if (k < 2)
        return 0;

    if (k == 2)
        return 1;

    if (k % 2 == 0)
        return 0;

    for (
        i = 3;
        i <= (int)sqrt((double)k);
        i += 2
    )
    {
        if (k % i == 0)
            return 0;
    }

    return 1;
}


/*
 * Structure containing the information required by each
 * OpenMP thread.
 *
 * Each thread gets its own structure and its own local
 * prime array, so threads do not need locks or mutexes
 * when storing their results.
 */
typedef struct
{
    int thread_id;

    int start;
    int end;

    int *local_primes;
    int local_count;
    int local_capacity;

} thread_arg_t;


int main(int argc, char *argv[])
{
    int rank;
    int size;

    int n;

    int local_range;
    int base_chunk;
    int remainder;

    int start;
    int end;

    int num_threads;

    int *local_primes = NULL;
    int local_count = 0;
    int local_capacity = 1024;

    int *thread_counts = NULL;
    int *thread_displacements = NULL;

    int *process_counts = NULL; /* Number of primes found by each process */
    int *process_displacements = NULL; /* Displacement of each process's results in the final array */

    int *all_primes = NULL; /* Final array containing all primes found by all processes */
    int total_count = 0;

    int i;

    double start_time;
    double end_time;

    /*
     * Initialise the MPI environment.
     */
    MPI_Init(&argc, &argv);


    /*
     * Determine this process's unique rank.
     *
     * Rank 0 will act as the root process.
     */
    MPI_Comm_rank(
        MPI_COMM_WORLD,
        &rank
    );


    /*
     * Determine the total number of MPI processes.
     */
    MPI_Comm_size(
        MPI_COMM_WORLD,
        &size
    );


    /*
     * The root process checks that exactly one command-line
     * argument has been provided.
     *
     * Example: xmpirun -np 4 ./Task2 10000000
     *
     * argc = 2
     * argv[1] = "10000000"
     */
    if (rank == 0)
    {
        if (argc != 2) /* Checks there are only 2 arguments (command name and n)*/
        {
            fprintf(
                stderr,
                "Usage: %s <n>\n",
                argv[0]
            );

            /*
             * Abort all MPI processes because root cannot
             * continue without a valid value of n.
             */
            MPI_Abort(
                MPI_COMM_WORLD,
                1
            );
        }


        /*
         * Convert the command-line argument from a string
         * to an integer.
         */
        n = atoi(argv[1]);


        /*
         * Check that n is at least 2.
         *
         * The performance experiments will use n >= 10000000,
         * but we still handle smaller inputs correctly.
         */
        if (n < 2)
        {
            fprintf(
                stderr,
                "Error: n must be at least 2.\n"
            );

            MPI_Abort(
                MPI_COMM_WORLD,
                1
            );
        }
    }

    /*
     * Start the overall timer.
     *
     * The timer begins before the broadcast so that the
     * measured runtime includes MPI communication as well
     * as computation and result collection.
     */
    start_time = MPI_Wtime();

    /*
     * Broadcast n from the root process to every MPI process.
     *
     * After this operation, every process knows the value of n.
     */
    MPI_Bcast(
        &n,
        1,
        MPI_INT,
        0,
        MPI_COMM_WORLD
    );

    /*
     * ---------------------------------------------------------
     * MPI WORKLOAD DISTRIBUTION
     * ---------------------------------------------------------
     *
     * The candidate numbers are:
     *
     *      2, 3, 4, ..., n-1
     *
     * We divide this range into approximately equal
     * contiguous blocks between MPI processes.
     */
    local_range = n - 2;

    /*
     * base_chunk = number of candidates every MPI process
     *              receives.
     *
     * remainder = number of candidates left over after
     *             dividing the range equally.
     */
    base_chunk =
        local_range / size;

    remainder =
        local_range % size;

    /*
     * Calculate the start and end of this process's MPI block.
     *
     * The first 'remainder' processes receive one additional
     * candidate so that the leftover candidates are distributed
     * as evenly as possible.
     *
     * Therefore:
     *
     *   first remainder processes → base_chunk + 1
     *   remaining processes       → base_chunk
     *
     * The blocks remain contiguous and ordered by rank.
     */
    if (rank < remainder)
    {
        start =
            2 +
            rank * (base_chunk + 1);

        end =
            start +
            (base_chunk + 1);
    }
    else
    {
        start =
            2 +
            remainder * (base_chunk + 1) +
            (rank - remainder) * base_chunk;

        end =
            start +
            base_chunk;
    }

    /*
     * ---------------------------------------------------------
     * OPENMP WORKLOAD DISTRIBUTION
     * ---------------------------------------------------------
     *
     * Each MPI process now has its own candidate range.
     *
     * OpenMP divides this range between threads.
     *
     * Each thread will receive a contiguous block of candidates
     * through the static OpenMP schedule.
     */

    /*
     * Get the number of OpenMP threads requested.
     *
     * omp_get_max_threads() returns the maximum number of
     * threads OpenMP will use for a parallel region.
     *
     * This is controlled when running the program, for example:
     *
     *   OMP_NUM_THREADS=2 mpirun -np 4 ./Task2 10000000
     */
    num_threads =
        omp_get_max_threads();


    /*
     * Allocate an array of the OpenMP threads.
     *
     * Each thread will have its own structure containing:
     *   - its candidate range
     *   - its local prime array
     *   - its number of primes found
     */
    thread_arg_t *threads =
        malloc(
            num_threads * sizeof(thread_arg_t)
        );

    /*
     * Allocate the local prime array for each OpenMP thread.
     *
     * Each thread starts with capacity for 1024 primes.
     *
     * The arrays are independent, so no locks are required
     * when threads add primes to their own arrays.
     */
    for (i = 0; i < num_threads; i++)
    {
        threads[i].thread_id =
            i;

        threads[i].local_primes =
            malloc(
                1024 * sizeof(int)
            );

        threads[i].local_count =
            0;

        threads[i].local_capacity =
            1024;
    }

    /*
     * Parallelise the MPI process's candidate range using OpenMP threads for the following for loop.
     *
     * schedule(static) divides the iterations into fixed contiguous chunks before assigning them to threads.
     *
     * Each iteration checks one candidate number for primality.
     */
    #pragma omp parallel for num_threads(num_threads) schedule(static)
    
    for (i = start; i < end; i++)
    {
        /*
         * Get the ID of the current OpenMP thread.
         */
        int thread_id =
            omp_get_thread_num();

        /*
         * Only this thread accesses its own result array.
         */
        if (is_prime(i))
        {
            /*
             * Expand the thread's local array if necessary.
             */
            if (
                threads[thread_id].local_count >=
                threads[thread_id].local_capacity
            )
            {
                threads[thread_id].local_capacity *= 2;

                threads[thread_id].local_primes =
                    realloc(
                        threads[thread_id].local_primes,
                        threads[thread_id].local_capacity *
                        sizeof(int)
                    );
            }

            /*
             * Store the newly discovered prime.
             */
            threads[thread_id].local_primes[
                threads[thread_id].local_count
            ] = i;

            threads[thread_id].local_count++;
        }
    }

    /*
     * ---------------------------------------------------------
     * COMBINE OPENMP THREAD RESULTS
     * ---------------------------------------------------------
     *
     * Each thread has its own sorted block of primes.
     *
     * Because OpenMP uses static scheduling, the thread blocks are ordered by their candidate ranges, giving a globally sorted result when concatenated.
     */


    /*
     * Calculate the total number of primes found by this
     * MPI process.
     */
    local_count = 0;

    for (i = 0; i < num_threads; i++)
    {
        local_count +=
            threads[i].local_count;
    }

    /*
     * Allocate a single local array containing all primes
     * found by this MPI process.
     */
    local_primes =
        malloc(
            local_count * sizeof(int)
        );

    /*
     * Concatenate the thread-local arrays in thread order, to produce a sorted array of primes for this MPI process.
     */
    int local_position = 0;

    for (i = 0; i < num_threads; i++)
    {
        for (
            int j = 0;
            j < threads[i].local_count;
            j++
        )
        {
            local_primes[local_position++] =
                threads[i].local_primes[j];
        }

        free(
            threads[i].local_primes
        );
    }

    free(threads);

    /*
     * ---------------------------------------------------------
     * MPI RESULT COLLECTION
     * ---------------------------------------------------------
     *
     * First gather the number of primes found by each MPI
     * process.
     *
     * Rank 0 needs these counts to determine how much space
     * to allocate and where each process's results should go.
     */
    if (rank == 0)
    {
        process_counts =
            malloc(
                size * sizeof(int)
            );
    }

    /*
     * Gather the number of primes found by each MPI process.
     *
     * Each process sends its local_count to rank 0.
     */
    MPI_Gather(
        &local_count,
        1,
        MPI_INT,
        process_counts,
        1,
        MPI_INT,
        0,
        MPI_COMM_WORLD
    );

    /*
     * Rank 0 calculates the total number of primes and the
     * displacement of each MPI process's results.
     */
    if (rank == 0)
    {
        process_displacements =
            malloc(
                size * sizeof(int)
            );

        process_displacements[0] =
            0;

        total_count =
            process_counts[0];

        for (i = 1; i < size; i++)
        {
            process_displacements[i] =
                process_displacements[i - 1] +
                process_counts[i - 1];

            total_count +=
                process_counts[i];
        }

        /*
         * Allocate the final array containing all primes.
         */
        all_primes =
            malloc(
                total_count * sizeof(int)
            );
    }

    /*
     * Gatherv is required because different MPI processes may
     * find different numbers of primes.
     *
     * Each process sends local_count primes.
     *
     * Rank 0 places each process's results at the displacement
     * calculated above.
     */
    MPI_Gatherv(
        local_primes,
        local_count,
        MPI_INT,
        all_primes,
        process_counts,
        process_displacements,
        MPI_INT,
        0,
        MPI_COMM_WORLD
    );

    free(local_primes);

    /*
     * ---------------------------------------------------------
     * OUTPUT
     * ---------------------------------------------------------
     *
     * Because MPI processes were assigned contiguous blocks
     * in increasing rank order, and each process concatenated
     * its OpenMP thread results in increasing range order,
     * the final array is already globally sorted.
     */
    if (rank == 0)
    {
        if (n < 100)
        {
            printf(
                "Primes less than %d:\n",
                n
            );

            for (i = 0; i < total_count; i++)
            {
                printf(
                    "%d ",
                    all_primes[i]
                );
            }

            printf("\n");
        }
        else
        {
            FILE *file =
                fopen(
                    "primes_hybrid.txt",
                    "w"
                );

            if (file == NULL)
            {
                fprintf(
                    stderr,
                    "Error: Could not open output file.\n"
                );

                MPI_Abort(
                    MPI_COMM_WORLD,
                    1
                );
            }

            for (i = 0; i < total_count; i++)
            {
                fprintf(
                    file,
                    "%d\n",
                    all_primes[i]
                );
            }

            fclose(file);
        }

        free(all_primes);
        free(process_counts);
        free(process_displacements);
    }

    /*
     * Synchronize every MPI process before stopping the overall timer.
     *
     * This ensures that the timing reflects the completion
     * of the entire hybrid MPI + OpenMP program.
     */
    MPI_Barrier(
        MPI_COMM_WORLD
    );

    end_time =
        MPI_Wtime();

    /*
     * Only the root process prints the final runtime so that
     * the output is not duplicated by every MPI process.
     */
    if (rank == 0)
    {
        printf(
            "Number of primes: %d\n",
            total_count
        );

        printf(
            "Execution time: %f seconds\n",
            end_time - start_time
        );

        printf(
            "MPI processes: %d\n",
            size
        );

        printf(
            "OpenMP threads per process: %d\n",
            num_threads
        );
    }

    /*
     * Shut down the MPI environment.
     */
    MPI_Finalize();

    return 0;
}