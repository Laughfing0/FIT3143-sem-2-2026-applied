/*
 * Task1.c
 *
 * Purpose: Find and print all prime numbers strictly less than an
 *          integer n using MPI for distributed parallel processing.
 *
 * Partitioning scheme: BLOCK (contiguous-range) partitioning.
 *     The range [2, n) is divided into approximately equal-sized
 *     contiguous blocks.
 *
 *     Each MPI process independently searches its assigned block
 *     and stores the prime numbers it finds in a local array, so
 *     when the results are combined, the final array is in sorted order.
 * 
 * MPI communication:
 *     MPI_Bcast()
 *         Root process broadcasts n to all MPI processes.
 *
 *     MPI_Gather()
 *         Each process sends its local count of primes to the root.
 *
 *     MPI_Gatherv()
 *         Each process sends its local prime numbers to root.
 *         Gatherv is used to allow variable counts of primes from each process.
 *
 * Timing:
 *     MPI_Wtime() measures real wall-clock elapsed time.
 *
 *     The timer includes:
 *         - workload computation
 *         - MPI communication
 *         - gathering results
 *         - final result construction
 *         - file output
 *
 * Compile:
 *     mpicc Task1.c -o Task1 -lm
 *
 * Run:
 *     mpirun -np 4 ./Task1 10000000
 *
 *     -np 4       = use 4 MPI processes
 *     10000000    = search for primes strictly less than 10,000,000
 *
 * Notes:
 *     Process 0 is the root process and is responsible for:
 *         - receiving the input
 *         - broadcasting n
 *         - collecting the results
 *         - constructing the final sorted array
 *         - writing the output file
 */

/* ============================================================
   Imports needed for the task
   ============================================================ */

#include <stdio.h>      /* printf(), fprintf(), fopen(), fclose() */
#include <stdlib.h>     /* malloc(), realloc(), free(), atoi() */
#include <math.h>       /* sqrt() */
#include <mpi.h>        /* MPI functionality */

/* ============================================================
   Function: is_prime
   ============================================================ */

/*
 * Checks whether an integer k is prime.
 *
 * Returns:
 *
 *     1 -> k is prime
 *     0 -> k is not prime
 *
 * The function only checks possible divisors up to sqrt(k).
 *
 * Even numbers greater than 2 are rejected immediately.
 */
int is_prime(int k)
{
    /*
     * Loop variable.
     */
    int i;

    /*
     * Numbers less than 2 are not prime.
     */
    if (k < 2)
        return 0;

    /*
     * 2 is the only even prime number.
     */
    if (k == 2)
        return 1;

    /*
     * Any even number greater than 2 cannot be prime.
     */
    if (k % 2 == 0)
        return 0;

    /*
     * Only test odd divisors up to sqrt(k).
     *
     * Increasing by 2 means that even divisors are skipped.
     */
    for (i = 3; i <= (int)sqrt((double)k); i += 2)
    {
        /*
         * If k is exactly divisible by i,
         * then k is not prime.
         */
        if (k % i == 0)
            return 0;
    }

    /*
     * No divisor was found, so k is prime.
     */
    return 1;
}

/* ============================================================
   Main function
   ============================================================ */

int main(int argc, char *argv[])
{
    int rank; /* Identifies the current MPI process */
    int size; /* Total number of MPI processes */

    /*
     * n is the upper limit.
     *
     * Only the root process initially obtains n.
     * MPI_Bcast() then distributes it to all processes.
     */
    int n = 0;

    /*
     * Variables describing the local workload.
     *
     * The process searches:
     *
     *     start <= i < end
     */
    int start; /* First candidate number assigned to this process */
    int end; /* First candidate number NOT assigned to this process */

    /*
     * Number of candidate values assigned to this process.
     */
    int local_range;

    /*
     * Variables used for dynamically storing the primes
     * found by this MPI process.
     */
    int *local_primes; /* Dynamically allocated array of primes found by this process */
    int local_count;
    int local_capacity;

    /*
     * Variables used by the root process when collecting
     * the results from all MPI processes.
     */
    int *counts; /* Number of primes found by each process */
    int *displacements; /* Displacement of each process's primes in the final array */
    int *primes; /* Final array containing all primes found by all processes */
    int total_count; /* Total number of primes found by all processes */

    /*
     * Variables used to calculate the block size.
     */
    int base_chunk; /* Minimum number of candidates assigned to each process */
    int remainder; /* Number of extra candidates that cannot be evenly divided among processes */

    int i; /* Loop variable for candidate numbers */
    int j; /* Loop variable for gathering results */

    /*
     * Timing variables.
     *
     * MPI_Wtime() returns wall-clock time in seconds.
     */
    double start_time;
    double end_time;
    double elapsed_time;

    /*
     * File pointer used by the root process to write
     * the final prime list.
     */
    FILE *file;

    /* ========================================================
       Initialise MPI
       ======================================================== */
    /*
     * Start the MPI environment.
     */
    MPI_Init(&argc, &argv); /* Passes address of number of command-line arguments and the array of command-line arguments */

    /*
     * Determine the rank of this process.
     */
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);

    /*
     * Determine the total number of MPI processes.
     */
    MPI_Comm_size(MPI_COMM_WORLD, &size);


    /* ========================================================
       Get input
       ======================================================== */

    /*
     * Only the root process reads the command-line argument.
     *
     * Example:
     *
     *     mpirun -np 4 ./Task1 10000000
     */
    if (rank == 0)
    {
        /*
         * Check that the user supplied n.
         */
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
            MPI_Abort(MPI_COMM_WORLD, 1);
        }

        /*
         * Convert the command-line argument from a string
         * to an integer.
         */
        n = atoi(argv[1]);

        /*
         * Validate n.
         */
        if (n < 2)
        {
            fprintf(
                stderr,
                "n must be >= 2.\n"
            );

            MPI_Abort(MPI_COMM_WORLD, 1);
        }
    }


    /* ========================================================
       Start overall timing
       ======================================================== */

    /*
     * Start timing before the MPI broadcast.
     *
     * This means communication is included in the measured
     * parallel execution time.
     */
    start_time = MPI_Wtime();


    /* ========================================================
       Broadcast n to all MPI processes
       ======================================================== */

    /*
     * MPI_Bcast() sends n from the root process (rank 0)
     * to every MPI process.
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


    /* ========================================================
       Handle the case where n <= 2
       ======================================================== */

    /*
     * There are no primes strictly less than n
     * when n <= 2.
     */
    if (n <= 2)
    {
        if (rank == 0)
        {
            printf(
                "There are no prime numbers strictly less than %d.\n",
                n
            );
        }

        MPI_Finalize();

        return 0;
    }


    /* ========================================================
       BLOCK workload partitioning
       ======================================================== */

    /*
     * The candidate range is:
     *
     *     [2, n)
     *
     * Therefore there are:
     *
     *     n - 2
     *
     * candidate numbers.
     */
    local_range = n - 2;

    /*
     * Calculate the minimum number of candidates each
     * process receives.
     *
     * Example:
     *
     *     10 candidates
     *     3 processes
     *
     *     base_chunk = 10 / 3 = 3
     */
    base_chunk = local_range / size;

    /*
     * The remainder contains the candidates that cannot
     * be divided equally.
     *
     * In the example above:
     *
     *     remainder = 10 % 3 = 1
     *
     * Therefore one process receives one extra candidate.
     */
    remainder = local_range % size;


    /*
     * Calculate the start and end of this process's block.
     *
     * The first 'remainder' processes receive one extra
     * candidate.
     *
     * This produces balanced contiguous blocks.
     */
    if (rank < remainder)
    {
        /*
         * This process receives base_chunk + 1 candidates.
         */
        start =
            2 +
            rank * (base_chunk + 1);

        end =
            start +
            (base_chunk + 1);
    }
    else
    {
        /*
         * Processes after the remainder receive base_chunk
         * candidates.
         */
        start =
            2 +
            remainder * (base_chunk + 1) +
            (rank - remainder) * base_chunk;

        end =
            start +
            base_chunk;
    }


    /* ========================================================
       Allocate local prime array
       ======================================================== */

    /*
     * Start with space for 1024 primes.
     *
     * The array will be expanded using realloc() if necessary.
     */
    local_capacity = 1024;

    local_count = 0;

    local_primes =
        malloc(
            local_capacity * sizeof(int)
        );

    /*
     * Check whether memory allocation succeeded.
     */
    if (local_primes == NULL)
    {
        fprintf(
            stderr,
            "Process %d: memory allocation failed.\n",
            rank
        );

        MPI_Abort(MPI_COMM_WORLD, 1);
    }


    /* ========================================================
       Search local block for prime numbers
       ======================================================== */

    /*
     * Each MPI process independently searches its own
     * contiguous block.
     *
     * There is no shared memory between MPI processes,
     * so no mutex or lock is required.
     */
    for (i = start; i < end; i++)
    {
        /*
         * Check whether i is prime.
         */
        if (is_prime(i))
        {
            /*
             * If the local array is full, double its size.
             */
            if (local_count == local_capacity)
            {
                /*
                 * Double the amount of allocated memory.
                 */
                local_capacity *= 2;

                /*
                 * Temporarily store the result of realloc()
                 * so that the original pointer is not lost
                 * if the allocation fails.
                 */
                int *temp =
                    realloc(
                        local_primes,
                        local_capacity * sizeof(int)
                    );

                /*
                 * Check whether reallocation succeeded.
                 */
                if (temp == NULL)
                {
                    fprintf(
                        stderr,
                        "Process %d: memory reallocation failed.\n",
                        rank
                    );

                    free(local_primes);

                    MPI_Abort(MPI_COMM_WORLD, 1);
                }

                /*
                 * Update the local array pointer.
                 */
                local_primes = temp;
            }

            /*
             * Store the prime number in the local array.
             *
             * Since i increases from start to end,
             * local_primes is automatically sorted.
             */
            local_primes[local_count] = i;

            local_count++;
        }
    }


    /* ========================================================
       Gather the number of primes found by each process
       ======================================================== */

    /*
     * Only the root process needs an array containing
     * the number of primes found by each process.
     *
     * For example:
     *
     *     rank 0 -> 100 primes
     *     rank 1 -> 98 primes
     *     rank 2 -> 103 primes
     *     rank 3 -> 97 primes
     *
     * These counts are needed by MPI_Gatherv().
     */
    if (rank == 0)
    {
        counts =
            malloc(
                size * sizeof(int)
            );

        if (counts == NULL)
        {
            fprintf(
                stderr,
                "Root: memory allocation failed for counts.\n"
            );

            free(local_primes);

            MPI_Abort(MPI_COMM_WORLD, 1);
        }
    }
    else
    {
        /*
         * Non-root processes do not need the counts array.
         */
        counts = NULL;
    }


    /*
     * Gather local prime counts from all processes.
     *
     * After this operation, root has:
     *
     *     counts[0]
     *     counts[1]
     *     ...
     *     counts[size - 1]
     */
    MPI_Gather(
        &local_count,
        1,
        MPI_INT,
        counts,
        1,
        MPI_INT,
        0,
        MPI_COMM_WORLD
    );


    /* ========================================================
       Prepare Gatherv information on root
       ======================================================== */

    if (rank == 0)
    {
        /*
         * Calculate the total number of primes.
         */
        total_count = 0;

        for (i = 0; i < size; i++)
        {
            total_count += counts[i];
        }

        /*
         * Allocate displacement information.
         *
         * displacements[i] tells MPI where the primes
         * from process i should be placed in the final array.
         */
        displacements =
            malloc(
                size * sizeof(int)
            );

        if (displacements == NULL)
        {
            fprintf(
                stderr,
                "Root: memory allocation failed for displacements.\n"
            );

            free(counts);
            free(local_primes);

            MPI_Abort(MPI_COMM_WORLD, 1);
        }

        /*
         * Calculate the displacement of each process's
         * result in the final prime array.
         */
        displacements[0] = 0;

        for (i = 1; i < size; i++)
        {
            displacements[i] =
                displacements[i - 1] +
                counts[i - 1];
        }

        /*
         * Allocate the final array containing every prime.
         */
        primes =
            malloc(
                total_count * sizeof(int)
            );

        if (primes == NULL)
        {
            fprintf(
                stderr,
                "Root: memory allocation failed for primes.\n"
            );

            free(counts);
            free(displacements);
            free(local_primes);

            MPI_Abort(MPI_COMM_WORLD, 1);
        }
    }
    else
    {
        /*
         * Only root needs these arrays.
         */
        displacements = NULL;
        primes = NULL;
        total_count = 0;
    }


    /* ========================================================
       Gather all prime numbers
       ======================================================== */

    /*
     * MPI_Gatherv() is used because each process can have
     * a different number of prime numbers.
     *
     * For example:
     *
     *     process 0 -> 100 primes
     *     process 1 -> 98 primes
     *     process 2 -> 103 primes
     *
     * Gatherv allows each process to send a different
     * amount of data.
     */
    MPI_Gatherv(
        local_primes,
        local_count,
        MPI_INT,
        primes,
        counts,
        displacements,
        MPI_INT,
        0,
        MPI_COMM_WORLD
    );


    /* ========================================================
       Free local memory
       ======================================================== */

    /*
     * Every process no longer needs its local prime array
     * after MPI_Gatherv() has completed.
     */
    free(local_primes);


    /* ========================================================
       Output results on root
       ======================================================== */

    if (rank == 0)
    {
        /*
         * For small n values, print the primes directly
         * to the terminal.
         */
        if (n < 100)
        {
            printf(
                "\nPrime numbers less than %d:\n",
                n
            );

            for (i = 0; i < total_count; i++)
            {
                printf(
                    "%d",
                    primes[i]
                );

                if (i < total_count - 1)
                    printf(", ");
            }

            printf("\n");
        }

        /*
         * For larger n values, write the results to a file.
         */
        else
        {
            /*
             * Open the output file.
             */
            file =
                fopen(
                    "primes_mpi.txt",
                    "w"
                );

            if (file == NULL)
            {
                fprintf(
                    stderr,
                    "Could not open primes_mpi.txt "
                    "for writing.\n"
                );

                free(primes);
                free(counts);
                free(displacements);

                MPI_Abort(MPI_COMM_WORLD, 1);
            }

            /*
             * Write a heading to the output file.
             */
            fprintf(
                file,
                "Prime numbers less than %d:\n",
                n
            );

            /*
             * Write every prime number.
             *
             * The results are already globally sorted because
             * the MPI processes were assigned increasing
             * contiguous blocks.
             */
            for (i = 0; i < total_count; i++)
            {
                fprintf(
                    file,
                    "%d",
                    primes[i]
                );

                if (i < total_count - 1)
                    fprintf(file, ", ");
            }

            fprintf(file, "\n");

            /*
             * Close the output file.
             */
            fclose(file);

            printf(
                "\nPrime numbers have been written "
                "to primes_mpi.txt\n"
            );
        }


        /* ====================================================
           Print statistics
           ==================================================== */

        printf(
            "Number of primes found: %d\n",
            total_count
        );

        printf(
            "Number of MPI processes used: %d\n",
            size
        );
    }


    /* ========================================================
       Stop timing
       ======================================================== */

    /*
     * Synchronise all MPI processes before recording the
     * final time.
     *
     * This ensures that the reported elapsed time represents
     * the completion time of the entire MPI computation rather
     * than only the root process.
     */
    MPI_Barrier(MPI_COMM_WORLD);

    /*
     * Record the final wall-clock time.
     */
    end_time = MPI_Wtime();

    /*
     * Calculate elapsed time.
     */
    elapsed_time =
        end_time -
        start_time;


    /* ========================================================
       Print execution time
       ======================================================== */

    /*
     * Only the root process prints the final timing result.
     */
    if (rank == 0)
    {
        printf(
            "Execution time (wall clock): %.6f seconds\n",
            elapsed_time
        );
    }


    /* ========================================================
       Free root memory
       ======================================================== */

    if (rank == 0)
    {
        free(primes);
        free(counts);
        free(displacements);
    }


    /* ========================================================
       Finalise MPI
       ======================================================== */

    /*
     * Shut down the MPI environment.
     */
    MPI_Finalize();

    /*
     * Return 0 to indicate successful execution.
     */
    return 0;
}