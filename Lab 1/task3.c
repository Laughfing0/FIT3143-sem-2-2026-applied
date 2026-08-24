/*
 * Task3.c
 *
 * Purpose: Find and print all prime numbers strictly less than an
 *          integer n entered by the user using OpenMP for parallel
 *          processing.
 *
 * Partitioning scheme: BLOCK (contiguous-range) partitioning.
 *
 *     The range [2, n) is divided into approximately equal-sized
 *     contiguous blocks.
 *
 *     Each OpenMP thread processes one block independently and
 *     stores the prime numbers it finds in its own dynamically
 *     allocated array.
 *
 *     Because the blocks are contiguous and increasing,
 *     concatenating the thread results in thread order
 *     produces a sorted list of prime numbers.
 *
 * Timing:
 *
 *     omp_get_wtime() measures real wall-clock elapsed time.
 *     This is appropriate for measuring parallel execution time
 *     and comparing the result with the serial version.
 *
 * Compile:
 *
 *     docker start -ai fit3143 or docker run -it --name fit3143 monashfit/fit3143:latest, docker rm fit3143
 *     cd /workspace
 *     gcc task3.c -o task3 -lm -fopenmp
 *
 *     - gcc       = GNU C compiler
 *     - Task3.c   = source code file
 *     - -o Task3  = name of the output executable
 *     - -lm       = link the math library for sqrt()
 *     - -fopenmp  = enable OpenMP support
 *
 * Run:
 *
 *     ./task3
 *
 *     The program will ask the user to enter n and
 *     the desired number of threads.
 */

/* Imports needed for the task */
#include <stdio.h> /* Gives standard Input/Output functionality such as printf(), scanf(), fprintf(), fopen() and fclose() */
#include <stdlib.h> /* Gives general purpose functions needed for dynamic memory management such as malloc(), realloc() and free() */
#include <math.h> /* Gives mathematical functions such as sqrt() */
#include <omp.h> /* Gives OpenMP functionality, including omp_get_wtime() and OpenMP parallel directives */

/*
 * Structure used to store information and results
 * belonging to each block of work.
 *
 * Each element of this structure represents one
 * block that will be processed by an OpenMP thread.
 */
typedef struct
{
    int thread_id; /* Stores the ID/index associated with this block */

    /*
     * start = first number in this block.
     * end   = first number NOT included in this block.
     *
     * Therefore, the numbers checked are:
     *
     *     start <= i < end
     */
    int start;
    int end;

    /*
     * Pointer to a dynamically allocated array
     * containing the prime numbers found in this block.
     */
    int *local_primes;

    /*
     * Stores the number of prime numbers currently
     * stored in local_primes.
     */
    int local_count;

    /*
     * Stores the current amount of space available
     * in the local_primes array.
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
 */
int is_prime(int k) /* The function accepts an integer k and returns an integer: 1 if k is prime, 0 otherwise */
{
    int i; /* Variable used to iterate through possible divisors of k */


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
     * Only check odd divisors up to sqrt(k).
     *
     * k is converted to double so that sqrt() can
     * calculate its square root.
     *
     * The result of sqrt() is then converted back to
     * int for comparison with i.
     *
     * i increases by 2 so that only odd divisors are checked.
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
 *
 * The main function controls the entire program.
 *
 * Unlike Task 2, there is no separate worker() function.
 * OpenMP handles the creation and management of threads
 * using the #pragma omp parallel for directive.
 */
int main(void) /* The main function does not accept any arguments and returns an integer */
{
    int n; /* Variable to store the upper limit entered by the user */

    int num_threads; /* Variable to store the number of OpenMP threads requested by the user */

    int i; /* Loop counter used for iterating through thread/block information */

    int j; /* Loop counter used for iterating through local prime arrays */

    FILE *file; /* File pointer used to write large prime-number results to a text file */

    /*
     * Pointer to an array of thread_arg_t structures.
     *
     * Each structure stores information about one block,
     * including its range and locally found primes.
     */
    thread_arg_t *targs;


    /*
     * Stores the total number of prime numbers found
     * by all OpenMP threads.
     */
    int total_count = 0;

    /*
     * Pointer to the final array containing all prime numbers.
     *
     * This array is created after all OpenMP threads
     * have completed the search.
     */
    int *primes;

    /*
     * Variables used to calculate the size and starting
     * position of each block.
     */
    int chunk_size;
    int cur;

    /*
     * Variables used for measuring wall-clock execution time.
     *
     * omp_get_wtime() returns the current wall-clock time
     * as a double measured in seconds.
     */
    double start_time;
    double end_time;
    double elapsed_time;

    /* ========================================================
       Get input from the user
       ======================================================== */

    /*
     * Ask the user to enter the upper limit n.
     */
    printf("Enter an integer n: ");

    /*
     * Read the integer entered by the user.
     *
     * &n gives scanf() the memory address of n so that
     * scanf() can store the input in that variable.
     */
    scanf("%d", &n);

    /*
     * Ask the user how many OpenMP threads should be used.
     */
    printf("Enter number of threads: ");

    /*
     * Read the requested number of threads.
     */
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
     * At least one thread is required.
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
     * There are n - 2 candidate numbers to test:
     */
    if (num_threads > n - 2)
        num_threads = n - 2;

    /* ========================================================
       Allocate thread/block information
       ======================================================== */

    targs = malloc(
        num_threads * sizeof(thread_arg_t)
    );

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
     * There are (n - 2) candidate numbers in the range [2, n).
     *
     * The expression below performs ceiling division:
     *
     *     ceil((n - 2) / num_threads)
     *
     * This makes the blocks approximately equal in size.
     */
    chunk_size =
        (n - 2 + num_threads - 1) / num_threads;


    /*
     * cur stores the first number that has not yet
     * been assigned to a block.
     */
    cur = 2;

    /*
     * Create the ranges for each block.
     */
    for (i = 0; i < num_threads; i++)
    {
        /*
         * Store an ID associated with this block.
         *
         * This is used to identify the block when
         * displaying error messages.
         */
        targs[i].thread_id = i;

        targs[i].start = cur;

        targs[i].end = cur + chunk_size;

        /*
         * Make sure the block does not extend beyond n.
         */
        if (targs[i].end > n)
            targs[i].end = n;
        
        cur = targs[i].end;

        targs[i].local_primes = NULL;

        targs[i].local_count = 0;

        targs[i].local_capacity = 0;
    }

    /* ========================================================
       Start timing
       ======================================================== */

    /*
     * Start the wall-clock timer immediately before
     * the parallel prime-number calculation.
     *
     * omp_get_wtime() returns the current time in seconds.
     */
    start_time = omp_get_wtime();

    /* ========================================================
       Parallel prime-number search
       ======================================================== */

    /*
     * #pragma omp parallel for tells OpenMP to divide
     * the iterations of the following for loop between
     * multiple threads i.e. parallelise the loop.
     *
     * This matches the BLOCK partitioning strategy.
     */
    #pragma omp parallel for num_threads(num_threads) schedule(static)

    /*
     * This loop iterates through the blocks.
     *
     * Each iteration represents one block of numbers
     * that needs to be searched.
     */
    for (i = 0; i < num_threads; i++)
    {
        /*
         * j is used to iterate through the numbers
         * within this block.
         */
        int j;

        targs[i].local_capacity = 1024;

        targs[i].local_count = 0;

        targs[i].local_primes =
            malloc(
                targs[i].local_capacity * sizeof(int)
            );

        if (targs[i].local_primes == NULL)
        {

            fprintf(
                stderr,
                "Thread %d: memory allocation failed.\n",
                targs[i].thread_id
            );

            /*
             * The continue statement moves to the next
             * iteration of the OpenMP loop.
             */
            continue;
        }

        /*
         * Search through this block's assigned range:
         *     start <= j < end
         */
        for (
            j = targs[i].start;
            j < targs[i].end;
            j++
        )
        {

            if (is_prime(j))
            {

                if (
                    targs[i].local_count ==
                    targs[i].local_capacity
                )
                {
               
                    targs[i].local_capacity *= 2;

                    int *temp = realloc(
                        targs[i].local_primes,
                        targs[i].local_capacity *
                            sizeof(int)
                    );

                    if (temp == NULL)
                    {
                        fprintf(
                            stderr,
                            "Thread %d: memory reallocation failed.\n",
                            targs[i].thread_id
                        );

                        free(targs[i].local_primes);

                        targs[i].local_primes = NULL;

                        break;
                    }

                    targs[i].local_primes = temp;
                }

                targs[i].local_primes[
                    targs[i].local_count
                ] = j;

                targs[i].local_count++;
            }
        }
    }

    /*
     * The program does not continue past an OpenMP
     * parallel-for until all iterations have completed.
     *
     * Therefore, by this point all OpenMP threads have
     * finished their assigned blocks.
     */

    /* ========================================================
       Stop timing
       ======================================================== */

    /*
     * Record the wall-clock time after all parallel
     * work has completed.
     */
    end_time = omp_get_wtime();

    /*
     * Calculate the total elapsed wall-clock time
     * omp_get_wtime() returns seconds as a double.
     */
    elapsed_time = end_time - start_time;

    /* ========================================================
       Count total number of primes
       ======================================================== */
    for (i = 0; i < num_threads; i++)
    {
        total_count += targs[i].local_count;
    }

    /* ========================================================
       Create final prime array
       ======================================================== */

    primes = malloc(
        total_count * sizeof(int)
    );

    if (primes == NULL)
    {
        fprintf(
            stderr,
            "Memory allocation failed.\n"
        );

        for (i = 0; i < num_threads; i++)
        {
            free(targs[i].local_primes);
        }

        free(targs);

        return 1;
    }

    /* ========================================================
       Concatenate thread/block results
       ======================================================== */

    /*
     * idx stores the next available position
     * in the final primes array.
     */
    {
        int idx = 0;

        /*
         * Process the blocks in increasing order, so the 
         * increasing ranges of numbers are already sorted
         * relative to one another.
         */
        for (i = 0; i < num_threads; i++)
        {
            
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
             * The local array is no longer required,
             * so free its dynamically allocated memory.
             */
            free(targs[i].local_primes);
        }
    }

    /* ========================================================
       Output results
       ======================================================== */

    /*
     * For small n values, print the prime numbers
     * directly to standard output.
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
     * For larger n values, write the prime numbers
     * to a text file.
     */
    else
    {

        file = fopen(
            "primes_openmp.txt",
            "w"
        );

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

        fprintf(
            file,
            "Prime numbers less than %d:\n",
            n
        );

        /*
         * Write every prime number to the file.
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

        fclose(file);

         */
        printf(
            "\nPrime numbers have been written "
            "to primes_openmp.txt\n"
        );
    }

    /* ========================================================
       Print statistics
       ======================================================== */
    printf(
        "Number of primes found: %d\n",
        total_count
    );

    printf(
        "Number of threads used: %d\n",
        num_threads
    );

    printf(
        "Execution time (wall clock): %.6f seconds\n",
        elapsed_time
    );

    /* ========================================================
       Free allocated memory
       ======================================================== */
    free(primes);

    free(targs);

    /*
     * Return 0 to indicate successful execution.
     */
    return 0;
}