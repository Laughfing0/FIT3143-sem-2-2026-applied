/* task2.c
 * 
 * Purpose: Find and print all prime numbers strictly less than an integer n
 *          entered by the user using POSIX Threads for parallel processing
 *
 * Partitioning scheme: BLOCK partitioning
 *   - The range [2, n) is divided into approximately equal-sized
 *     contiguous blocks, with each block assigned to one thread.
 *   - Each thread checks its own block independently and stores
 *     the prime numbers it finds in its own dynamically allocated array.
 *   - Since each thread has its own array, the threads do not need
 *     to access or modify the same array during the prime search,
 *     meaning no mutex or lock is required for thread safety.
 *   - The blocks are assigned in increasing order, so concatenating
 *     the results from thread 0, thread 1, thread 2, etc. produces
 *     the final list of primes in ascending order.
 *
 * Timing:
 *   - clock_gettime(CLOCK_MONOTONIC, ...) is used to measure wall-clock
 *     elapsed time.
 *   - The overall timer measures the complete execution time, including
 *     thread creation, prime searching, result collection, result
 *     processing, and file output.
 *   - A separate timer measures the parallelizable prime-search section.
 *   - The serial and parallel fractions are calculated from these timings
 *     for use with Amdahl's Law.
 *
 * Compile:
 *     gcc task2.c -o task2 -lm -lpthread
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


/* Structure used to store the results produced by each POSIX thread */
typedef struct {
    int thread_id; /* Unique ID for the thread */
    int start; /* Inclusive starting point of the thread */
    int end; /* Exclusive end point of the thread */
    int *local_primes; /* Array storing primes found by the thread */
    int local_count; /* Number of primes found by the thread */
    int local_capacity; /* Current capacity of local_primes */
} thread_arg_t;


/* Function to check if a number is prime */
int is_prime(int k)
{
    int i;

    /* Numbers less than 2 are not prime. */
    if (k < 2)
        return 0;

    /* 2 is the only even prime number. */
    if (k == 2)
        return 1;

    /* Any even number greater than 2 is not prime. */
    if (k % 2 == 0)
        return 0;

    /* Only check odd divisors up to sqrt(k). */
    for (i = 3; i <= (int)sqrt((double)k); i += 2) {

        /* If k is divisible by i, it is not prime. */
        if (k % i == 0)
            return 0;
    }

    /* No divisors were found, so k is prime. */
    return 1;
}


/* Worker function executed by each POSIX thread */
void *worker(void *arg)
{
    /*
     * Convert the generic void pointer back into
     * a pointer to our thread_arg_t structure.
     */
    thread_arg_t *t = (thread_arg_t *)arg;

    int i;

    /*
     * Give this thread's local prime array an
     * initial capacity of 1024 integers.
     */
    t->local_capacity = 1024;

    /* No primes have been found yet. */
    t->local_count = 0;

    /*
     * Allocate memory for this thread's local
     * prime array.
     */
    t->local_primes =
        malloc(t->local_capacity * sizeof(int));

    /* Check whether memory allocation succeeded. */
    if (t->local_primes == NULL) {

        fprintf(
            stderr,
            "Thread %d: memory allocation failed.\n",
            t->thread_id
        );

        pthread_exit(NULL);
    }

    /*
     * Search through this thread's assigned range.
     *
     * start <= i < end
     */
    for (i = t->start; i < t->end; i++) {

        /* Check whether the current number is prime. */
        if (is_prime(i)) {

            /*
             * Increase storage if the local array is full.
             */
            if (t->local_count == t->local_capacity) {

                /* Double the array capacity. */
                t->local_capacity *= 2;

                /*
                 * Resize the local prime array.
                 */
                t->local_primes =
                    realloc(
                        t->local_primes,
                        t->local_capacity * sizeof(int)
                    );

                /* Check whether reallocation succeeded. */
                if (t->local_primes == NULL) {

                    fprintf(
                        stderr,
                        "Thread %d: memory reallocation failed.\n",
                        t->thread_id
                    );

                    pthread_exit(NULL);
                }
            }

            /*
             * Store the prime in this thread's
             * private local array.
             */
            t->local_primes[t->local_count] = i;

            /* Increase the number of primes found. */
            t->local_count++;
        }
    }

    /*
     * The thread has finished its assigned range.
     */
    return NULL;
}


/* Main function to find and print prime numbers using POSIX Threads */
int main(void)
{
    int n; /* Upper limit */
    int num_threads; /* Number of threads */
    int i;
    int j;

    FILE *file;

    /* Array containing pthread identifiers. */
    pthread_t *threads;

    /* Array containing information for each thread. */
    thread_arg_t *targs;

    /*
     * =========================================================
     * TIMING VARIABLES
     * =========================================================
     */

    /*
     * Overall execution timer.
     *
     * Measures the complete execution including:
     * - thread setup
     * - prime searching
     * - result collection
     * - result processing
     * - file output
     */
    struct timespec start_ts, end_ts;

    /*
     * Parallel-section timer.
     *
     * Measures the section of the program that performs
     * the prime-search work using POSIX Threads.
     */
    struct timespec parallel_start_ts, parallel_end_ts;

    /* Overall execution time in seconds. */
    double elapsed_time;

    /* Time spent in the parallelizable section. */
    double parallel_time;

    /* Estimated serial portion of the overall execution. */
    double serial_time;

    /* Serial fraction used by Amdahl's Law. */
    double serial_fraction;

    /* Parallel fraction used by Amdahl's Law. */
    double parallel_fraction;

    /*
     * Total number of primes found by all threads.
     */
    int total_count = 0;

    /*
     * Final array containing all prime numbers.
     */
    int *primes;

    /*
     * Size of each block assigned to a thread.
     */
    int chunk_size;

    /*
     * First number not yet assigned to a thread.
     */
    int cur;


    /* Ask the user to enter the upper limit n. */
    printf("Enter an integer n: ");
    scanf("%d", &n);

    /* Ask the user how many threads should be created. */
    printf("Enter number of threads: ");
    scanf("%d", &num_threads);


    /* Handle n <= 2. */
    if (n <= 2) {

        printf(
            "There are no prime numbers strictly less than %d.\n",
            n
        );

        return 0;
    }


    /* At least one thread is required. */
    if (num_threads < 1) {

        fprintf(
            stderr,
            "Number of threads must be >= 1.\n"
        );

        return 1;
    }


    /*
     * Do not create more threads than there are
     * numbers to test.
     */
    if (num_threads > n - 2)
        num_threads = n - 2;


    /*
     * =========================================================
     * START OVERALL TIMER
     * =========================================================
     *
     * This timer measures the overall wall-clock execution time.
     */
    clock_gettime(
        CLOCK_MONOTONIC,
        &start_ts
    );


    /*
     * Allocate memory for the pthread_t array.
     */
    threads =
        malloc(num_threads * sizeof(pthread_t));


    /*
     * Allocate memory for the thread argument array.
     */
    targs =
        malloc(num_threads * sizeof(thread_arg_t));


    /* Check whether either allocation failed. */
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
     * The numbers in [2, n) are divided into approximately
     * equal-sized contiguous blocks.
     */

    /*
     * Calculate the block size using ceiling division.
     */
    chunk_size =
        (n - 2 + num_threads - 1) / num_threads;

    /* Start assigning numbers from 2. */
    cur = 2;


    /*
     * Assign a block to each thread.
     */
    for (i = 0; i < num_threads; i++) {

        /* Store the thread ID. */
        targs[i].thread_id = i;

        /* Store the inclusive start of the block. */
        targs[i].start = cur;

        /* Calculate the exclusive end of the block. */
        targs[i].end = cur + chunk_size;

        /* Do not allow the block to extend beyond n. */
        if (targs[i].end > n)
            targs[i].end = n;

        /* Move to the beginning of the next block. */
        cur = targs[i].end;

        /*
         * Initialise local result variables.
         */
        targs[i].local_primes = NULL;
        targs[i].local_count = 0;
        targs[i].local_capacity = 0;
    }


    /*
     * =========================================================
     * START PARALLEL TIMER
     * =========================================================
     *
     * The parallelizable section begins here.
     *
     * This includes:
     * - creating the POSIX threads
     * - parallel prime searching
     * - waiting for all threads to finish
     *
     * It stops after pthread_join().
     */
    clock_gettime(
        CLOCK_MONOTONIC,
        &parallel_start_ts
    );


    /*
     * =========================================================
     * CREATE THE THREADS
     * =========================================================
     */
    for (i = 0; i < num_threads; i++) {

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
     */
    for (i = 0; i < num_threads; i++) {

        /*
         * Wait for thread i to finish.
         */
        pthread_join(
            threads[i],
            NULL
        );

        /*
         * Add the number of primes found by this
         * thread to the total.
         */
        total_count += targs[i].local_count;
    }


    /*
     * =========================================================
     * STOP PARALLEL TIMER
     * =========================================================
     */
    clock_gettime(
        CLOCK_MONOTONIC,
        &parallel_end_ts
    );


    /*
     * Calculate the time spent in the parallelizable section.
     */
    parallel_time =
        (double)(parallel_end_ts.tv_sec - parallel_start_ts.tv_sec) +
        (double)(parallel_end_ts.tv_nsec - parallel_start_ts.tv_nsec)
            / 1000000000.0;


    /*
     * =========================================================
     * CREATE FINAL PRIME ARRAY
     * =========================================================
     */
    primes =
        malloc(total_count * sizeof(int));


    /* Check whether allocation succeeded. */
    if (primes == NULL) {

        fprintf(
            stderr,
            "Memory allocation failed.\n"
        );

        /* Free each thread's local array. */
        for (i = 0; i < num_threads; i++)
            free(targs[i].local_primes);

        free(threads);
        free(targs);

        return 1;
    }


    /*
     * =========================================================
     * CONCATENATE THREAD RESULTS
     * =========================================================
     *
     * Because the thread ranges are increasing and
     * non-overlapping, copying results in thread order
     * automatically produces a sorted array.
     */
    {
        int idx = 0;

        for (i = 0; i < num_threads; i++) {

            for (j = 0; j < targs[i].local_count; j++) {

                primes[idx] =
                    targs[i].local_primes[j];

                idx++;
            }

            /*
             * The thread's local array is no longer needed.
             */
            free(targs[i].local_primes);
        }
    }


    /*
     * =========================================================
     * OUTPUT RESULTS
     * =========================================================
     */
    if (n < 100) {

        printf(
            "\nPrime numbers less than %d:\n",
            n
        );

        for (i = 0; i < total_count; i++) {

            printf(
                "%d",
                primes[i]
            );

            if (i < total_count - 1)
                printf(", ");
        }

        printf("\n");
    }

    else {

        file = fopen(
            "primes_parallel.txt",
            "w"
        );

        if (file == NULL) {

            fprintf(
                stderr,
                "Could not open primes_parallel.txt "
                "for writing.\n"
            );

            free(primes);
            free(threads);
            free(targs);

            return 1;
        }

        fprintf(
            file,
            "Prime numbers less than %d:\n",
            n
        );

        for (i = 0; i < total_count; i++) {

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

        printf(
            "\nPrime numbers have been written "
            "to primes_parallel.txt\n"
        );
    }


    /*
     * =========================================================
     * STOP OVERALL TIMER
     * =========================================================
     *
     * This occurs after result processing and file output,
     * so elapsed_time represents the overall execution time.
     */
    clock_gettime(
        CLOCK_MONOTONIC,
        &end_ts
    );


    /*
     * Calculate overall elapsed wall-clock time.
     */
    elapsed_time =
        (double)(end_ts.tv_sec - start_ts.tv_sec) +
        (double)(end_ts.tv_nsec - start_ts.tv_nsec)
            / 1000000000.0;


    /*
     * =========================================================
     * CALCULATE AMDAHL FRACTIONS
     * =========================================================
     *
     * The parallelizable portion is measured directly.
     *
     * The remaining portion of the overall execution time
     * is treated as the serial/overhead portion.
     */
    serial_time =
        elapsed_time - parallel_time;

    serial_fraction =
        serial_time / elapsed_time;

    parallel_fraction =
        parallel_time / elapsed_time;


    /*
     * =========================================================
     * PRINT STATISTICS
     * =========================================================
     */

    printf(
        "Number of primes found: %d\n",
        total_count
    );

    printf(
        "Number of threads used: %d\n",
        num_threads
    );

    printf(
        "Overall execution time: %.6f seconds\n",
        elapsed_time
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


    /*
     * =========================================================
     * FREE MEMORY
     * =========================================================
     */

    free(primes);
    free(threads);
    free(targs);


    /*
     * Return 0 to indicate successful execution.
     */
    return 0;
}