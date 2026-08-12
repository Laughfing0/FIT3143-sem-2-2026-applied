/*
 * task3.c
 *
 * Parallel (OpenMP) version of task1.c.
 * Finds and prints all prime numbers strictly less than n.
 *
 * Partitioning scheme: BLOCK (contiguous-range) partitioning.
 *
 *     - The range [2, n) is split into NUM_THREADS contiguous,
 *       roughly equal-sized chunks.
 *
 *     - Each OpenMP thread tests its own chunk independently
 *       and stores results in its OWN dynamic array.
 *
 *     - Because each thread writes only to its own local array,
 *       no critical section or mutex is required during the
 *       prime-number search.
 *
 *     - Because chunks are contiguous and increasing,
 *       concatenating the thread results in thread order
 *       (0, 1, 2, ...) already produces a sorted list.
 *
 * NOTE ON TIMING:
 *     omp_get_wtime() measures real (wall-clock) elapsed time.
 *     This is appropriate for measuring parallel execution time
 *     and comparing it with the serial version.
 *
 * Compile:
 *     docker start -ai fit3143
 *     cd /workspace    
 *     gcc task3.c -o task3 -lm -fopenmp
 *
 * Run:
 *     ./task3
 *
 *     (enter n and number of threads when prompted)
 */


#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <omp.h>


/*
 * Structure used to store the results produced by
 * each OpenMP thread.
 *
 * Each thread has its own thread_arg_t structure.
 */
typedef struct
{
    int thread_id;

    /*
     * start = first number this thread checks.
     * end   = first number NOT checked.
     *
     * Therefore, the thread checks:
     *
     *     start <= i < end
     */
    int start;
    int end;

    /*
     * Dynamically allocated array containing
     * the prime numbers found by this thread.
     */
    int *local_primes;

    /*
     * Number of primes currently stored.
     */
    int local_count;

    /*
     * Current capacity of local_primes.
     */
    int local_capacity;

} thread_arg_t;


/*
 * ============================================================
 * Function: is_prime
 * ============================================================
 *
 * Checks whether an integer k is prime.
 *
 * Returns:
 *
 *     1 -> k is prime
 *     0 -> k is not prime
 *
 * The function only checks possible divisors up to sqrt(k).
 * Even numbers greater than 2 are rejected immediately.
 */
int is_prime(int k)
{
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
     * Any even number greater than 2 is not prime.
     */
    if (k % 2 == 0)
        return 0;


    /*
     * Only check possible divisors up to sqrt(k).
     *
     * We increase i by 2 because k is already known
     * to be odd, so there is no need to check even
     * divisors.
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


/*
 * ============================================================
 * Main function
 * ============================================================
 */

int main(void)
{
    /*
     * n:
     * Upper limit entered by the user.
     */
    int n;

    /*
     * Number of OpenMP threads requested by the user.
     */
    int num_threads;

    /*
     * Loop counters.
     */
    int i;
    int j;

    /*
     * File pointer used when writing large results
     * to a text file.
     */
    FILE *file;

    /*
     * Arrays used to store information about each thread.
     *
     * targs[i] stores the range and prime results
     * belonging to thread i.
     */
    thread_arg_t *targs;

    /*
     * Total number of prime numbers found by all threads.
     */
    int total_count = 0;

    /*
     * Final array containing all prime numbers.
     *
     * The array is created after all threads have
     * finished their searches.
     */
    int *primes;

    /*
     * Variables used for block partitioning.
     */
    int chunk_size;
    int cur;

    /*
     * Wall-clock timing variables.
     *
     * omp_get_wtime() returns the elapsed wall-clock
     * time in seconds.
     */
    double start_time;
    double end_time;
    double elapsed_time;


    /* ========================================================
       Get input from the user
       ======================================================== */

    printf("Enter an integer n: ");
    scanf("%d", &n);


    printf("Enter number of threads: ");
    scanf("%d", &num_threads);


    /* ========================================================
       Validate input
       ======================================================== */

    /*
     * If n <= 2, there are no prime numbers strictly
     * less than n.
     */
    if (n <= 2)
    {
        printf(
            "There are no prime numbers strictly less than %d.\n",
            n
        );

        return 0;
    }


    /*
     * The user must request at least one thread.
     */
    if (num_threads < 1)
    {
        fprintf(
            stderr,
            "Number of threads must be >= 1.\n"
        );

        return 1;
    }


    /*
     * There are n - 2 numbers to test:
     *
     *     2, 3, 4, ..., n - 1
     *
     * There is no point creating more threads than
     * there are numbers to test.
     */
    if (num_threads > n - 2)
        num_threads = n - 2;


    /* ========================================================
       Allocate thread information
       ======================================================== */

    /*
     * Allocate an array of thread_arg_t structures.
     *
     * Each element stores the information and results
     * belonging to one OpenMP thread.
     */
    targs = malloc(
        num_threads * sizeof(thread_arg_t)
    );


    /*
     * Check whether memory allocation succeeded.
     */
    if (targs == NULL)
    {
        fprintf(
            stderr,
            "Memory allocation failed.\n"
        );

        return 1;
    }


    /* ========================================================
       Block partitioning
       ======================================================== */

    /*
     * Calculate the size of each block.
     *
     * There are (n - 2) numbers in the range [2, n).
     *
     * The expression below performs ceiling division:
     *
     *     ceil((n - 2) / num_threads)
     *
     * This ensures that every number is included.
     */
    chunk_size =
        (n - 2 + num_threads - 1) / num_threads;


    /*
     * cur represents the first number that has not
     * yet been assigned to a thread.
     *
     * The search starts at 2.
     */
    cur = 2;


    /*
     * Create the ranges for each thread.
     *
     * For example, if n = 100 and there are 4 threads,
     * the ranges will be approximately:
     *
     *     Thread 0: [2, 27)
     *     Thread 1: [27, 52)
     *     Thread 2: [52, 77)
     *     Thread 3: [77, 100)
     */
    for (i = 0; i < num_threads; i++)
    {
        /*
         * Store the thread's ID.
         */
        targs[i].thread_id = i;


        /*
         * Store the beginning of this thread's range.
         */
        targs[i].start = cur;


        /*
         * Calculate the end of this thread's range.
         */
        targs[i].end = cur + chunk_size;


        /*
         * Do not allow the end of the final block
         * to go beyond n.
         */
        if (targs[i].end > n)
            targs[i].end = n;


        /*
         * Move cur forward so that the next thread
         * starts where this thread finishes.
         */
        cur = targs[i].end;


        /*
         * Initialise the local result information.
         */
        targs[i].local_primes = NULL;
        targs[i].local_count = 0;
        targs[i].local_capacity = 0;
    }


    /* ========================================================
       Start timing
       ======================================================== */

    /*
     * omp_get_wtime() measures wall-clock time.
     *
     * This timer is started immediately before the
     * parallel prime-number calculation.
     */
    start_time = omp_get_wtime();


    /* ========================================================
       Parallel prime-number search
       ======================================================== */

    /*
     * OpenMP creates num_threads threads.
     *
     * Each thread is assigned one value of i.
     *
     * The schedule(static) clause gives each thread
     * a contiguous block of iterations.
     *
     * This matches our BLOCK partitioning strategy.
     */
    #pragma omp parallel for \
        num_threads(num_threads) \
        schedule(static)

    for (i = 0; i < num_threads; i++)
    {
        int j;

        /*
         * Each iteration of this loop represents one
         * thread's assigned block.
         *
         * The thread creates its own local array.
         */
        targs[i].local_capacity = 1024;

        targs[i].local_count = 0;

        targs[i].local_primes =
            malloc(
                targs[i].local_capacity * sizeof(int)
            );


        /*
         * Check whether memory allocation succeeded.
         */
        if (targs[i].local_primes == NULL)
        {
            /*
             * This error is written to stderr.
             *
             * Since this happens inside the parallel region,
             * the program cannot safely continue searching.
             */
            fprintf(
                stderr,
                "Thread %d: memory allocation failed.\n",
                targs[i].thread_id
            );

            continue;
        }


        /*
         * Search this thread's assigned range.
         */
        for (
            j = targs[i].start;
            j < targs[i].end;
            j++
        )
        {
            /*
             * Check whether j is prime.
             */
            if (is_prime(j))
            {
                /*
                 * If the local array is full,
                 * increase its capacity.
                 */
                if (
                    targs[i].local_count ==
                    targs[i].local_capacity
                )
                {
                    /*
                     * Double the capacity.
                     */
                    targs[i].local_capacity *= 2;


                    /*
                     * Increase the size of the local array.
                     */
                    int *temp = realloc(
                        targs[i].local_primes,
                        targs[i].local_capacity *
                            sizeof(int)
                    );


                    /*
                     * Check whether realloc() succeeded.
                     */
                    if (temp == NULL)
                    {
                        fprintf(
                            stderr,
                            "Thread %d: memory reallocation failed.\n",
                            targs[i].thread_id
                        );

                        /*
                         * Free the existing memory before
                         * stopping this thread's search.
                         */
                        free(targs[i].local_primes);

                        targs[i].local_primes = NULL;

                        break;
                    }


                    /*
                     * Update the pointer after successful
                     * reallocation.
                     */
                    targs[i].local_primes = temp;
                }


                /*
                 * Store the prime number in this thread's
                 * local array.
                 */
                targs[i].local_primes[
                    targs[i].local_count
                ] = j;


                /*
                 * Increase the number of primes found
                 * by this thread.
                 */
                targs[i].local_count++;
            }
        }
    }


    /* ========================================================
       Stop timing
       ======================================================== */

    /*
     * All threads have completed the parallel loop
     * before the program continues.
     */
    end_time = omp_get_wtime();


    /*
     * Calculate elapsed wall-clock time.
     */
    elapsed_time = end_time - start_time;


    /* ========================================================
       Count total number of primes
       ======================================================== */

    /*
     * Add together the number of primes found by
     * each thread.
     */
    for (i = 0; i < num_threads; i++)
    {
        total_count += targs[i].local_count;
    }


    /* ========================================================
       Create final prime array
       ======================================================== */

    /*
     * Allocate enough memory to store all primes found
     * by all threads.
     */
    primes = malloc(
        total_count * sizeof(int)
    );


    /*
     * Check whether allocation succeeded.
     */
    if (primes == NULL)
    {
        fprintf(
            stderr,
            "Memory allocation failed.\n"
        );


        /*
         * Free each thread's local memory.
         */
        for (i = 0; i < num_threads; i++)
        {
            free(targs[i].local_primes);
        }


        free(targs);

        return 1;
    }


    /* ========================================================
       Concatenate thread results
       ======================================================== */

    /*
     * idx keeps track of the next position in the
     * final primes array.
     */
    {
        int idx = 0;


        /*
         * Process thread results in thread order:
         *
         *     0, 1, 2, ..., num_threads - 1
         *
         * Since each thread was given a contiguous
         * increasing range, the results are already
         * sorted.
         */
        for (i = 0; i < num_threads; i++)
        {
            /*
             * Copy this thread's prime numbers into
             * the final array.
             */
            for (
                j = 0;
                j < targs[i].local_count;
                j++
            )
            {
                primes[idx] =
                    targs[i].local_primes[j];

                idx++;
            }


            /*
             * The thread's local array is no longer
             * needed, so release its memory.
             */
            free(targs[i].local_primes);
        }
    }


    /* ========================================================
       Output results
       ======================================================== */

    /*
     * For small n, print the prime numbers to
     * standard output.
     */
    if (n < 100)
    {
        printf(
            "\nPrime numbers less than %d:\n",
            n
        );


        /*
         * Print all primes in ascending order.
         */
        for (i = 0; i < total_count; i++)
        {
            printf("%d", primes[i]);


            /*
             * Print a comma between values,
             * but not after the final value.
             */
            if (i < total_count - 1)
                printf(", ");
        }


        printf("\n");
    }


    /*
     * For larger n, write the results to a text file.
     */
    else
    {
        /*
         * Open the output file in write mode.
         */
        file = fopen(
            "primes_openmp.txt",
            "w"
        );


        /*
         * Check whether the file opened successfully.
         */
        if (file == NULL)
        {
            fprintf(
                stderr,
                "Could not open primes_openmp.txt "
                "for writing.\n"
            );


            free(primes);
            free(targs);

            return 1;
        }


        /*
         * Write a heading to the file.
         */
        fprintf(
            file,
            "Prime numbers less than %d:\n",
            n
        );


        /*
         * Write all prime numbers to the file.
         */
        for (i = 0; i < total_count; i++)
        {
            fprintf(
                file,
                "%d",
                primes[i]
            );


            /*
             * Add a comma between prime numbers.
             */
            if (i < total_count - 1)
                fprintf(file, ", ");
        }


        fprintf(file, "\n");


        /*
         * Close the file.
         */
        fclose(file);


        /*
         * Tell the user where the results were saved.
         */
        printf(
            "\nPrime numbers have been written "
            "to primes_openmp.txt\n"
        );
    }


    /* ========================================================
       Print statistics
       ======================================================== */

    /*
     * Display the total number of primes found.
     */
    printf(
        "Number of primes found: %d\n",
        total_count
    );


    /*
     * Display the number of OpenMP threads used.
     */
    printf(
        "Number of threads used: %d\n",
        num_threads
    );


    /*
     * Display the wall-clock execution time.
     */
    printf(
        "Execution time (wall clock): %.6f seconds\n",
        elapsed_time
    );


    /* ========================================================
       Free allocated memory
       ======================================================== */

    /*
     * Free the final prime array.
     */
    free(primes);


    /*
     * Free the array containing thread information.
     */
    free(targs);


    /*
     * Return 0 to indicate successful execution.
     */
    return 0;
}