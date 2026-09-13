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
 * Timing:
 *   - MPI_Wtime() is used to measure wall-clock elapsed time.
 *   - The overall timer measures the complete hybrid execution,
 *     including communication, computation, result collection,
 *     synchronization, and file output.
 *   - A separate timer measures the parallelizable prime-search
 *     portion performed by OpenMP.
 *   - The remaining portion is treated as serial/overhead time
 *     for the Amdahl's Law calculation.
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

    int *process_counts = NULL;
    int *process_displacements = NULL;

    int *all_primes = NULL;
    int total_count = 0;

    int i;

    /*
     * =========================================================
     * TIMING VARIABLES
     * =========================================================
     */

    /*
     * Overall execution timer.
     *
     * Measures the complete hybrid execution, including:
     * - MPI communication
     * - workload distribution
     * - OpenMP computation
     * - result collection
     * - synchronization
     * - file output
     */
    double start_time;
    double end_time;

    /*
     * Timer for the parallelizable prime-search portion.
     */
    double parallel_start_time;
    double parallel_end_time;

    /*
     * Time spent in the parallelizable section.
     */
    double parallel_time;

    /*
     * Estimated serial/overhead portion of execution.
     */
    double serial_time;

    /*
     * Fractions used for Amdahl's Law.
     */
    double serial_fraction;
    double parallel_fraction;

    /*
     * Overall elapsed execution time.
     */
    double overall_time;


    /*
     * ---------------------------------------------------------
     * INITIALISE MPI
     * ---------------------------------------------------------
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
     * Example:
     *
     *   mpirun -np 4 ./Task2 10000000
     *
     * argc = 2
     * argv[1] = "10000000"
     */
    if (rank == 0)
    {
        if (argc != 2)
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
     * ---------------------------------------------------------
     * START OVERALL TIMER
     * ---------------------------------------------------------
     *
     * The timer begins before MPI_Bcast so that the overall
     * runtime includes MPI communication.
     */
    start_time = MPI_Wtime();


    /*
     * Broadcast n from the root process to every MPI process.
     *
     * After this operation, every process knows n.
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
     * The range is divided into approximately equal
     * contiguous blocks between MPI processes.
     */
    local_range = n - 2;

    /*
     * Number of candidates every MPI process receives.
     */
    base_chunk =
        local_range / size;

    /*
     * Number of candidates remaining after equal division.
     */
    remainder =
        local_range % size;


    /*
     * Calculate the start and end of this process's MPI block.
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
     */
    num_threads =
        omp_get_max_threads();


    /*
     * Allocate an array of OpenMP thread structures.
     */
    thread_arg_t *threads =
        malloc(
            num_threads * sizeof(thread_arg_t)
        );


    /*
     * Allocate a local prime array for each OpenMP thread.
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
     * ---------------------------------------------------------
     * START PARALLEL TIMER
     * ---------------------------------------------------------
     *
     * This measures the parallelizable prime-search portion.
     *
     * The timer starts immediately before the OpenMP parallel
     * loop and stops immediately after it finishes.
     */
    parallel_start_time =
        MPI_Wtime();


    /*
     * ---------------------------------------------------------
     * OPENMP PRIME SEARCH
     * ---------------------------------------------------------
     *
     * schedule(static) divides the iterations into fixed
     * contiguous chunks before assigning them to threads.
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
     * STOP PARALLEL TIMER
     * ---------------------------------------------------------
     */
    parallel_end_time =
        MPI_Wtime();


    /*
     * Calculate the time spent in the parallelizable
     * prime-search section.
     */
    parallel_time =
        parallel_end_time -
        parallel_start_time;


    /*
     * ---------------------------------------------------------
     * COMBINE OPENMP THREAD RESULTS
     * ---------------------------------------------------------
     *
     * Each thread has its own sorted block of primes.
     *
     * Because the thread blocks are ordered by candidate
     * range, concatenating them in thread order produces
     * a sorted result for this MPI process.
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
     * Concatenate the thread-local arrays in thread order.
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

        /*
         * The thread-local array is no longer required.
         */
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
     */
    if (rank == 0)
    {
        process_counts =
            malloc(
                size * sizeof(int)
            );
    }


    /*
     * Gather local prime counts to rank 0.
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
     * Rank 0 calculates the total number of primes and
     * the displacement of each process's results.
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
     * Gatherv is required because different MPI processes
     * may find different numbers of primes.
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
     * ---------------------------------------------------------
     * STOP OVERALL TIMER
     * ---------------------------------------------------------
     *
     * Synchronize all MPI processes before stopping the timer.
     *
     * This ensures that the overall timing represents the
     * completion of the complete hybrid program.
     */
    MPI_Barrier(
        MPI_COMM_WORLD
    );


    end_time =
        MPI_Wtime();


    /*
     * Calculate the overall execution time.
     */
    overall_time =
        end_time -
        start_time;


    /*
     * ---------------------------------------------------------
     * AMDAHL'S LAW TIMINGS
     * ---------------------------------------------------------
     *
     * The measured parallel portion is the OpenMP prime-search
     * section.
     *
     * The remaining part of the overall execution is treated
     * as serial/overhead time.
     */
    serial_time =
        overall_time -
        parallel_time;


    /*
     * Calculate the serial fraction.
     */
    serial_fraction =
        serial_time /
        overall_time;


    /*
     * Calculate the parallel fraction.
     */
    parallel_fraction =
        parallel_time /
        overall_time;


    /*
     * ---------------------------------------------------------
     * PRINT RESULTS
     * ---------------------------------------------------------
     *
     * Only rank 0 prints the final statistics.
     */
    if (rank == 0)
    {
        printf(
            "Number of primes: %d\n",
            total_count
        );

        printf(
            "Overall execution time: %.6f seconds\n",
            overall_time
        );

        printf(
            "Parallel portion time: %.6f seconds\n",
            parallel_time
        );

        printf(
            "Serial/overhead portion time: %.6f seconds\n",
            serial_time
        );

        printf(
            "Serial fraction: %.6f\n",
            serial_fraction
        );

        printf(
            "Parallel fraction: %.6f\n",
            parallel_fraction
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