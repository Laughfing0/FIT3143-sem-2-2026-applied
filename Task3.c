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
     *
     * For example:
     *
     *     n = 2
     *
     * There are no prime numbers less than 2.
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
     *
     * If the user enters 0 or a negative number,
     * display an error and terminate the program.
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
     *
     *     2, 3, 4, ..., n - 1
     *
     * There is no benefit in creating more threads
     * than there are numbers to test.
     */
    if (num_threads > n - 2)
        num_threads = n - 2;

    /* ========================================================
       Allocate thread/block information
       ======================================================== */

    /*
     * Allocate memory for an array of thread_arg_t structures.
     *
     * There is one structure for each block of work.
     */
    targs = malloc(
        num_threads * sizeof(thread_arg_t)
    );

    /*
     * Check whether the memory allocation was successful.
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
     *
     * The search begins at 2.
     */
    cur = 2;

    /*
     * Create the ranges for each block.
     *
     * For example, with n = 100 and 4 threads:
     *
     *     Block 0: [2, 27)
     *     Block 1: [27, 52)
     *     Block 2: [52, 77)
     *     Block 3: [77, 100)
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

        /*
         * Store the first number in this block.
         */
        targs[i].start = cur;

        /*
         * Calculate the end of this block.
         *
         * The end value is exclusive.
         */
        targs[i].end = cur + chunk_size;

        /*
         * Make sure the block does not extend beyond n.
         */
        if (targs[i].end > n)
            targs[i].end = n;

        /*
         * Move cur to the end of the current block.
         *
         * This means the next block starts where
         * the current block finishes.
         */
            cur = targs[i].end;

        /*
         * Initialise the local prime array pointer.
         *
         * The actual array will be allocated inside
         * the parallel section.
         */
        targs[i].local_primes = NULL;

        /*
         * No primes have been found yet.
         */
        targs[i].local_count = 0;

        /*
         * No storage has been allocated yet.
         */
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
     * This is the main difference between Task 2 and Task 3.
     *
     * #pragma omp parallel for tells OpenMP to divide
     * the iterations of the following for loop between
     * multiple threads.
     *
     * num_threads(num_threads)
     *     Requests the number of threads specified by the user.
     *
     * schedule(static)
     *     Gives threads fixed, contiguous chunks of loop
     *     iterations.
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


        /*
         * Give this block's local prime array an
         * initial capacity of 1024 integers.
         */
        targs[i].local_capacity = 1024;


        /*
         * No primes have been found in this block yet.
         */
        targs[i].local_count = 0;


        /*
         * Allocate memory for this block's local
         * prime-number array.
         *
         * Each block has its OWN array.
         */
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
             * Print an error identifying which block
             * failed to allocate memory.
             */
            fprintf(
                stderr,
                "Thread %d: memory allocation failed.\n",
                targs[i].thread_id
            );


            /*
             * Skip this block if memory allocation failed.
             *
             * The continue statement moves to the next
             * iteration of the OpenMP loop.
             */
            continue;
        }


        /*
         * Search through this block's assigned range.
         *
         * The range is:
         *
         *     start <= j < end
         *
         * Therefore, j starts at the block's first number
         * and stops before the block's end value.
         */
        for (
            j = targs[i].start;
            j < targs[i].end;
            j++
        )
        {
            /*
             * Check whether the current number j is prime.
             */
            if (is_prime(j))
            {
                /*
                 * Check whether the local array is full.
                 */
                if (
                    targs[i].local_count ==
                    targs[i].local_capacity
                )
                {
                    /*
                     * Double the amount of memory available
                     * for this block's prime array.
                     */
                    targs[i].local_capacity *= 2;


                    /*
                     * realloc() attempts to resize the
                     * existing array to the new capacity.
                     *
                     * A temporary pointer is used so that
                     * the original pointer is not lost if
                     * realloc() fails.
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
                         * Free the original local array
                         * because this block cannot continue.
                         */
                        free(targs[i].local_primes);


                        /*
                         * Set the pointer to NULL so that
                         * it does not point to freed memory.
                         */
                        targs[i].local_primes = NULL;


                        /*
                         * Stop processing this block.
                         */
                        break;
                    }


                    /*
                     * realloc() succeeded, so update the
                     * pointer to point to the resized array.
                     */
                    targs[i].local_primes = temp;
                }


                /*
                 * Store the prime number j in this block's
                 * local array.
                 */
                targs[i].local_primes[
                    targs[i].local_count
                ] = j;


                /*
                 * Increase the number of primes found
                 * in this block.
                 */
                targs[i].local_count++;
            }
        }
    }


    /*
     * IMPORTANT:
     *
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
     * Calculate the total elapsed wall-clock time.
     *
     * Unlike Task 2, there is no need to manually
     * calculate seconds and nanoseconds because
     * omp_get_wtime() already returns seconds as a double.
     */
    elapsed_time = end_time - start_time;


    /* ========================================================
       Count total number of primes
       ======================================================== */


    /*
     * Add together the number of primes found in
     * each block.
     */
    for (i = 0; i < num_threads; i++)
    {
        total_count += targs[i].local_count;
    }


    /* ========================================================
       Create final prime array
       ======================================================== */


    /*
     * Allocate enough memory to store every prime found
     * by all OpenMP threads.
     */
    primes = malloc(
        total_count * sizeof(int)
    );


    /*
     * Check whether the allocation was successful.
     */
    if (primes == NULL)
    {
        fprintf(
            stderr,
            "Memory allocation failed.\n"
        );


        /*
         * Free the local arrays belonging to each block.
         */
        for (i = 0; i < num_threads; i++)
        {
            free(targs[i].local_primes);
        }


        /*
         * Free the thread/block information array.
         */
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
         * Process the blocks in increasing order:
         *
         *     Block 0
         *     Block 1
         *     Block 2
         *     ...
         *
         * Because the blocks contain increasing ranges
         * of numbers, their results are already sorted
         * relative to one another.
         */
        for (i = 0; i < num_threads; i++)
        {
            /*
             * Copy each prime from the current block's
             * local array into the final array.
             */
            for (
                j = 0;
                j < targs[i].local_count;
                j++
            )
            {
                primes[idx] =
                    targs[i].local_primes[j];


                /*
                 * Move to the next position in the
                 * final primes array.
                 */
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
        /*
         * Print a heading for the list.
         */
        printf(
            "\nPrime numbers less than %d:\n",
            n
        );


        /*
         * Print every prime number in ascending order.
         */
        for (i = 0; i < total_count; i++)
        {
            /*
             * Print the current prime.
             */
            printf(
                "%d",
                primes[i]
            );


            /*
             * Print a comma between values,
             * but not after the final value.
             */
            if (i < total_count - 1)
                printf(", ");
        }


        /*
         * Move to the next line after printing the
         * complete list.
         */
        printf("\n");
    }


    /*
     * For larger n values, write the prime numbers
     * to a text file.
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
         * Check whether the file was opened successfully.
         */
        if (file == NULL)
        {
            fprintf(
                stderr,
                "Could not open primes_openmp.txt "
                "for writing.\n"
            );


            /*
             * Free allocated memory before exiting.
             */
            free(primes);
            free(targs);


            return 1;
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
         * Write every prime number to the file.
         */
        for (i = 0; i < total_count; i++)
        {
            /*
             * Write the current prime number.
             */
            fprintf(
                file,
                "%d",
                primes[i]
            );


            /*
             * Add a comma between values,
             * but not after the final value.
             */
            if (i < total_count - 1)
                fprintf(file, ", ");
        }


        /*
         * Add a newline at the end of the file.
         */
        fprintf(file, "\n");


        /*
         * Close the file after all results have
         * been written.
         */
        fclose(file);


        /*
         * Inform the user that the results were
         * successfully written to the file.
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
     * Print the total number of prime numbers found.
     */
    printf(
        "Number of primes found: %d\n",
        total_count
    );


    /*
     * Print the number of OpenMP threads used.
     */
    printf(
        "Number of threads used: %d\n",
        num_threads
    );


    /*
     * Print the wall-clock execution time
     * to six decimal places.
     */
    printf(
        "Execution time (wall clock): %.6f seconds\n",
        elapsed_time
    );


    /* ========================================================
       Free allocated memory
       ======================================================== */


    /*
     * Free the final array containing all prime numbers.
     */
    free(primes);


    /*
     * Free the array containing the information
     * for each block/thread.
     */
    free(targs);


    /*
     * Return 0 to indicate successful execution.
     */
    return 0;
}