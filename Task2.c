/* task2.c
 * 
 * Purpose: Find and print all prime numbers strictly less than an integer n
 *          entered by the user using POSIX Threads for parallel processing
 *
 * Partitioning scheme: BLOCK partitioning
 *   - The range [2, n) is divided into approximately equal-sized
 *     contiguous blocks, with each block assigned to one thread.
 *   - Each thread checks its own block independently and stores the
 *     prime numbers it finds in its own dynamically allocated array.
 *   - Since each thread has its own array, the threads do not need
 *     to access or modify the same array during the prime search,
 *     meaning no mutex or lock is required for thread safety.
 *   - The blocks are assigned in increasing order, so concatenating
 *     the results from thread 0, thread 1, thread 2, etc. produces
 *     the final list of primes in ascending order.
 *
 * Timing:
 *   - clock_gettime(CLOCK_MONOTONIC, ...) is used to measure wall-clock
 *     elapsed time
 *   - This is more appropriate for measuring parallel execution time
 *     than clock(), because clock() measures CPU time which can be
 *     accumulated across multiple threads.
 *   - Therefore, it is not an accurate measure of parallel speedup.
 *
 * Compile:
 *     gcc task2.c -o task2 -lm -lpthread; Compiled using gcc compiler with the math library linked for mathematical functions and the pthread library linked for POSIX threads
 *
 * Run:
 *     ./task2
 */

/* Imports needed for the task */
#include <stdio.h> 
#include <stdlib.h>
#include <math.h>
#include <time.h>
#include <pthread.h> /* POSIX Threads library for parallel processing */

/* Structure used to store the results produced by each POSIX thread, named thread_arg_t */
typedef struct {
    int thread_id; /* Unique ID for the thread */
    int start; /* Stores the inclusive starting point of the thread */
    int end; /* Stores the exclusive end point of the thread */
    int *local_primes; /* Pointer to a dynamically allocated array to store the prime numbers found by the thread */
    int local_count; /* Count of prime numbers found by the thread */
    int local_capacity; /* Current amount of space in the local_primes array */
} thread_arg_t;

/* Function to check if a number is prime */
int is_prime(int k) /* The function accepts an integer k and returns an integer: 1 if k is prime, 0 otherwise */
{
    int i; /* Variable used to iterate through possible divisors of k */

    /* Numbers less than 2 are not prime. */
    if (k < 2)
        return 0;

    /* 2 is the only even prime number. */
    if (k == 2)
        return 1;

    /* Any even number greater than 2 cannot be prime. */
    if (k % 2 == 0)
        return 0;

    /*
     * Only check odd divisors up to sqrt(k).
     *
     * k is converted to a double so that sqrt() can calculate
     * its square root, and the result is converted back to int
     * for comparison with the integer loop variable i.
     *
     * i starts at 3 and increases by 2, meaning that only
     * odd possible divisors are checked.
     */
    for (i = 3; i <= (int)sqrt((double)k); i += 2) {

        /* If k is exactly divisible by i, then k is not prime. */
        if (k % i == 0)
            return 0;
    }

    /* If no divisors were found, k is prime. */
    return 1;
}

/* Worker function executed by each POSIX thread.
 *
 * The function receives a void pointer because pthread_create()
 * requires the thread function to have this general-purpose
 * pointer type.
 */
void *worker(void *arg)
{
    /*
     * Convert the generic void pointer back into a pointer
     * to our thread_arg_t structure.
     *
     * This allows the thread to access its own: attributes.
     */
    thread_arg_t *t = (thread_arg_t *)arg;

    int i; /* Variable used to iterate through this thread's assigned range */

    /*
     * Give this thread's local prime array an initial
     * capacity of 1024 integers.
     */
    t->local_capacity = 1024;

    /*
     * No prime numbers have been found by this thread yet.
     */
    t->local_count = 0;

    /*
     * Allocate memory for this thread's local prime array.
     *
     * Each thread gets its OWN array.
     *
     * This is important because it means multiple threads
     * are not writing to the same array at the same time.
     */
    t->local_primes =
        malloc(t->local_capacity * sizeof(int));

    /*
     * Check whether memory allocation was successful.
     */
    if (t->local_primes == NULL) {

        /* Print an error message identifying which thread failed. */
        fprintf(
            stderr,
            "Thread %d: memory allocation failed.\n",
            t->thread_id
        );

        /*
         * Exit this thread if memory could not be allocated.
         */
        pthread_exit(NULL);
    }

    /*
     * Search through this thread's assigned range.
     *
     * The range is:
     *
     *     start <= i < end
     *
     * 'start' is inclusive and 'end' is exclusive.
     */
    for (i = t->start; i < t->end; i++) {

        /*
         * Check whether the current number i is prime.
         */
        if (is_prime(i)) {

            /*
             * Check whether this thread's local array
             * is full.
             */
            if (t->local_count == t->local_capacity) {

                /*
                 * Double the capacity so that more prime
                 * numbers can be stored.
                 *
                 * For example:
                 *
                 *     1024 -> 2048 -> 4096 -> 8192 -> ...
                 */
                t->local_capacity *= 2;


                /*
                 * Resize the thread's local array.
                 *
                 * realloc() changes the amount of memory
                 * allocated for the existing array.
                 */
                t->local_primes =
                    realloc(
                        t->local_primes,
                        t->local_capacity * sizeof(int)
                    );


                /*
                 * Check whether the reallocation was successful.
                 */
                if (t->local_primes == NULL) {

                    fprintf(
                        stderr,
                        "Thread %d: memory reallocation failed.\n",
                        t->thread_id
                    );

                    /*
                     * Exit the current thread if memory
                     * could not be reallocated.
                     */
                    pthread_exit(NULL);
                }
            }


            /*
             * Store the prime number in this thread's
             * local array.
             *
             * local_count starts at 0, so the first prime
             * is stored at index 0.
             */
            t->local_primes[t->local_count] = i;


            /*
             * Increase the number of primes found by
             * this thread.
             */
            t->local_count++;
        }
    }


    /*
     * The thread has finished its assigned range.
     *
     * Returning NULL indicates that the thread has
     * completed successfully.
     */
    return NULL;
}

/* Main function to find and print prime numbers using POSIX Threads */
int main(void) /* The main function does not accept any arguments and returns an integer */
{
    int n; /* Variable to store the integer entered by the user */

    int num_threads; /* Variable to store the number of POSIX Threads requested by the user */

    int i; /* Variable used to iterate through the threads */

    int j; /* Variable used to iterate through each thread's local prime array */

    FILE *file; /* File pointer used to write prime numbers to a text file */

    /*
     * Array of pthread_t objects.
     *
     * Each element of the array represents one POSIX thread created
     * by the program.
     */
    pthread_t *threads;

    /*
     * Array of thread_arg_t structures.
     *
     * Each element contains the information belonging
     * to one thread.
     */
    thread_arg_t *targs;

    /*
     * Variables used to measure wall-clock execution time.
     *
     * struct timespec stores time using:
     *     tv_sec  = seconds
     *     tv_nsec = nanoseconds
     */
    struct timespec start_ts, end_ts;

    double elapsed_time; /* Variable to store the elapsed execution time in seconds */

    /*
     * Stores the total number of prime numbers found
     * by all threads.
     */
    int total_count = 0;

    /*
     * Pointer to the final array containing all prime numbers.
     *
     * The individual thread arrays will eventually be
     * combined into this array.
     */
    int *primes;

    /*
     * chunk_size stores the size of each block assigned
     * to a thread.
     */
    int chunk_size;

    /*
     * cur stores the first number that has not yet been
     * assigned to a thread.
     */
    int cur;

    /* Ask the user to enter the upper limit n */
    printf("Enter an integer n: ");

    /*
     * Read the integer entered by the user.
     *
     * &n gives scanf() the memory address of n so that
     * it can store the user's input in n.
     */
    scanf("%d", &n);

    /* Ask the user how many threads should be created */
    printf("Enter number of threads: ");

    /*
     * Read the number of threads entered by the user.
     */
    scanf("%d", &num_threads);

    /*
     * If n <= 2, there are no prime numbers strictly
     * less than n.
     *
     * For example:
     *
     *     n = 2
     *     Numbers less than 2: 0 and 1
     *     Neither is prime.
     */
    if (n <= 2) {
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
     * display an error.
     */
    if (num_threads < 1) {
        fprintf(
            stderr,
            "Number of threads must be >= 1.\n"
        );

        return 1;
    }

    /*
     * There are n - 2 numbers that need to be tested:
     *
     *     2, 3, 4, ..., n - 1
     *
     * There is no point creating more threads than
     * there are numbers to test.
     *
     * For example, if there are only 8 numbers to test,
     * creating 20 threads would be unnecessary.
     */
    if (num_threads > n - 2)
        num_threads = n - 2;

    /*
     * Allocate memory for the array of POSIX Threads.
     *
     * There will be one pthread_t object for each thread.
     */
    threads =
        malloc(num_threads * sizeof(pthread_t));

    /*
     * Allocate memory for the array of thread arguments.
     *
     * There will be one thread_arg_t structure for
     * each thread.
     */
    targs =
        malloc(num_threads * sizeof(thread_arg_t));

    /*
     * Check whether either memory allocation failed.
     *
     * If either pointer is NULL, the allocation failed.
     */
    if (threads == NULL || targs == NULL) {
        fprintf(
            stderr,
            "Memory allocation failed.\n"
        );

        return 1;
    }

    /*
     * =========================================================
     * BLOCK PARTITIONING
     * =========================================================
     *
     * The numbers that need to be tested are in the range:
     *
     *     [2, n)
     *
     * This means:
     *
     *     2 <= number < n
     *
     * The range is divided into approximately equal-sized
     * contiguous blocks.
     */

    /*
     * Calculate the size of each block.
     *
     * The expression:
     *
     *     (n - 2 + num_threads - 1) / num_threads
     *
     * performs ceiling division.
     *
     * This ensures that the entire range is covered.
     */
    chunk_size =
        (n - 2 + num_threads - 1) / num_threads;

    /*
     * Start assigning numbers from 2.
     */
    cur = 2;

    /*
     * Create the range assigned to each thread.
     */
    for (i = 0; i < num_threads; i++) {

        /*
         * Store the thread's ID.
         *
         * Thread IDs in our program are:
         *
         *     0, 1, 2, ..., num_threads - 1
         */
        targs[i].thread_id = i;

        /*
         * The current number becomes the start of
         * this thread's range.
         *
         * start is inclusive.
         */
        targs[i].start = cur;

        /*
         * Calculate the end of this thread's range.
         *
         * end is exclusive.
         */
        targs[i].end = cur + chunk_size;

        /*
         * Make sure the thread does not receive numbers
         * greater than or equal to n.
         */
        if (targs[i].end > n)
            targs[i].end = n;

        /*
         * Move cur to the end of this block.
         *
         * This means the next thread starts where the
         * current thread finishes.
         */
        cur = targs[i].end;

        /*
         * Initialise the local result variables.
         *
         * The worker() function will allocate the actual
         * local_primes array.
         */
        targs[i].local_primes = NULL;
        targs[i].local_count = 0;
        targs[i].local_capacity = 0;
    }

    /*
     * =========================================================
     * START TIMING
     * =========================================================
     *
     * clock_gettime() records the current wall-clock time.
     *
     * CLOCK_MONOTONIC is used because it provides a clock
     * that moves forward consistently and is not affected
     * by changes to the system time.
     */
    clock_gettime(
        CLOCK_MONOTONIC,
        &start_ts
    );

    /*
     * =========================================================
     * CREATE THE THREADS
     * =========================================================
     *
     * Each thread receives:
     *
     *     - worker() as the function it should execute
     *     - &targs[i] as its individual argument
     */
    for (i = 0; i < num_threads; i++) {

        /*
         * pthread_create() creates and starts a new thread.
         *
         * Arguments:
         *
         *     &threads[i]
         *         Stores the ID of the newly created thread.
         *
         *     NULL
         *         Uses the default thread attributes.
         *
         *     worker
         *         Function that the new thread executes.
         *
         *     &targs[i]
         *         Gives the thread its own information,
         *         including its assigned range.
         */
        pthread_create(
            &threads[i],
            NULL,
            worker,
            &targs[i]
        );
    }


    /*
     * =========================================================
     * WAIT FOR ALL THREADS
     * =========================================================
     *
     * pthread_join() makes the main thread wait until
     * the specified thread has finished.
     */
    for (i = 0; i < num_threads; i++) {

        /*
         * Wait for thread i to finish.
         *
         * NULL means that we do not need the thread's
         * return value.
         */
        pthread_join(
            threads[i],
            NULL
        );


        /*
         * Add the number of primes found by this thread
         * to the total number of primes.
         */
        total_count += targs[i].local_count;
    }

    /*
     * =========================================================
     * STOP TIMING
     * =========================================================
     *
     * At this point, every thread has finished its search.
     *
     * Therefore, the time measured includes the complete
     * parallel prime-search process.
     */
    clock_gettime(
        CLOCK_MONOTONIC,
        &end_ts
    );

    /*
     * Calculate elapsed wall-clock time.
     *
     * First calculate the difference in seconds:
     *
     *     end_ts.tv_sec - start_ts.tv_sec
     *
     * Then calculate the difference in nanoseconds:
     *
     *     end_ts.tv_nsec - start_ts.tv_nsec
     *
     * Dividing the nanoseconds by 1e9 converts them
     * into seconds.
     */
    elapsed_time =
        (end_ts.tv_sec - start_ts.tv_sec) +
        (end_ts.tv_nsec - start_ts.tv_nsec) / 1e9;

    /*
     * =========================================================
     * CREATE FINAL PRIME ARRAY
     * =========================================================
     *
     * Each thread currently has its own local array.
     *
     * We now create one final array that will contain
     * all prime numbers found by all threads.
     */
    primes =
        malloc(total_count * sizeof(int));

    /*
     * Check whether memory allocation succeeded.
     */
    if (primes == NULL) {
        fprintf(
            stderr,
            "Memory allocation failed.\n"
        );

        /*
         * Free each thread's local array before exiting if not enough memory was available for the final array.
         */
        for (i = 0; i < num_threads; i++)
            free(targs[i].local_primes);

        /*
         * Free the arrays used to store thread information.
         */
        free(threads);
        free(targs);

        return 1;
    }

    /*
     * =========================================================
     * CONCATENATE THREAD RESULTS
     * =========================================================
     *
     * Each thread has found primes from its own range.
     *
     * For example:
     *
     *     Thread 0 -> [2, 25)
     *     Thread 1 -> [25, 48)
     *     Thread 2 -> [48, 71)
     *
     * Since the ranges are increasing and non-overlapping,
     * the results can be copied in thread order:
     *
     *     Thread 0 results
     *     Thread 1 results
     *     Thread 2 results
     *
     * This automatically produces a sorted final array.
     *
     * Therefore, no sorting algorithm is required.
     */

    /*
     * idx stores the position where the next prime number
     * should be placed in the final primes array.
     */
    {
        int idx = 0;

        /*
         * Process the thread results in order:
         *
         *     0, 1, 2, ..., num_threads - 1
         */
        for (i = 0; i < num_threads; i++) {

            /*
             * Copy every prime found by thread i
             * into the final primes array.
             */
            for (j = 0; j < targs[i].local_count; j++) {

                /*
                 * Copy the current prime from the thread's
                 * local array into the final array.
                 */
                primes[idx] =
                    targs[i].local_primes[j];

                /*
                 * Move to the next position in the
                 * final array.
                 */
                idx++;
            }

            /*
             * The thread's local array is no longer
             * required, so free its dynamically allocated
             * memory.
             */
            free(targs[i].local_primes);
        }
    }

    /*
     * =========================================================
     * OUTPUT RESULTS
     * =========================================================
     *
     * For small n, print the results to the terminal.
     *
     * For larger n, write them to a text file.
     */
    if (n < 100) {

        /*
         * Print a heading for the list of primes.
         */
        printf(
            "\nPrime numbers less than %d:\n",
            n
        );


        /*
         * Print every prime number in the final array.
         *
         * The array is already sorted because the thread
         * results were concatenated in increasing range order.
         */
        for (i = 0; i < total_count; i++) {

            /*
             * Print the current prime number.
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
         * Move to the next line after printing all primes.
         */
        printf("\n");
    }

    /*
     * For larger n, write the prime numbers to a text file.
     */
    else {

        /*
         * Open a file named "primes_parallel.txt"
         * in write mode.
         */
        file = fopen(
            "primes_parallel.txt",
            "w"
        );

        /*
         * Check whether the file was opened successfully.
         */
        if (file == NULL) {

            fprintf(
                stderr,
                "Could not open primes_parallel.txt "
                "for writing.\n"
            );

            /*
             * Free dynamically allocated memory before
             * exiting because of the error.
             */
            free(primes);
            free(threads);
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
        for (i = 0; i < total_count; i++) {

            /*
             * Write the current prime number.
             */
            fprintf(
                file,
                "%d",
                primes[i]
            );

            /*
             * Write a comma between values,
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
         * Close the file after all prime numbers
         * have been written.
         */
        fclose(file);

        /*
         * Tell the user that the results have been
         * successfully written to the file.
         */
        printf(
            "\nPrime numbers have been written "
            "to primes_parallel.txt\n"
        );
    }

    /*
     * =========================================================
     * PRINT STATISTICS
     * =========================================================
     */

    /*
     * Print the total number of prime numbers found
     * by all threads.
     */
    printf(
        "Number of primes found: %d\n",
        total_count
    );

    /*
     * Print the number of threads used for the calculation.
     */
    printf(
        "Number of threads used: %d\n",
        num_threads
    );

    /*
     * Print the wall-clock execution time.
     *
     * %.6f means the time will be displayed as a
     * floating-point number with 6 digits after the
     * decimal point.
     */
    printf(
        "Execution time (wall clock): %.6f seconds\n",
        elapsed_time
    );

    /*
     * =========================================================
     * FREE MEMORY
     * =========================================================
     */

    /*
     * Free the final array containing all prime numbers.
     */
    free(primes);

    /*
     * Free the array containing the pthread_t objects.
     */
    free(threads);

    /*
     * Free the array containing information for each thread.
     */
    free(targs);

    /*
     * Return 0 to indicate that the program completed
     * successfully.
     */
    return 0;
}