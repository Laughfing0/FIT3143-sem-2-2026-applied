/* Task2.c
 * Parallel (POSIX Threads) version of Task1.c.
 * Finds and prints all prime numbers strictly less than n.
 *
 * Partitioning scheme: BLOCK (contiguous-range) partitioning.
 *   - The range [2, n) is split into NUM_THREADS contiguous, roughly
 *     equal-sized chunks.
 *   - Each thread tests its own chunk independently and stores results
 *     in its OWN dynamic array -> no locking needed during the search.
 *   - Because chunks are contiguous and increasing, concatenating the
 *     thread results in thread order (0,1,2,...) already yields a
 *     sorted (ascending) list -> no merge/sort step required.
 *
 * NOTE ON TIMING:
 *   clock() measures CPU time, which is summed across all threads and
 *   therefore NOT a fair way to measure parallel speedup. We use
 *   clock_gettime(CLOCK_MONOTONIC, ...) instead, which measures real
 *   (wall-clock) elapsed time. Make sure your serial task1.c is timed
 *   the same way before comparing speedups.
 *
 * Compile:
 *     gcc Task2.c -o Task2 -lm -lpthread; Compiled using gcc compiler with the math library linked for mathematical functions and the pthread library linked for POSIX threads
 *
 * Run:
 *     ./Task2
 */
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <time.h>
#include <pthread.h>

typedef struct {
    int thread_id;
    int start;          /* inclusive */
    int end;             /* exclusive */
    int *local_primes;   /* dynamically grown array, local to this thread */
    int local_count;
    int local_capacity;
} thread_arg_t;

int is_prime(int k)
{
    int i;
    if (k < 2)
        return 0;
    if (k == 2)
        return 1;
    if (k % 2 == 0)
        return 0;
    for (i = 3; i <= (int)sqrt((double)k); i += 2) {
        if (k % i == 0)
            return 0;
    }
    return 1;
}

/* Each thread runs this: test its own [start, end) range and store
 * results in its own local buffer. Purely thread-local writes, so no
 * mutex is required here. */
void *worker(void *arg)
{
    thread_arg_t *t = (thread_arg_t *)arg;
    int i;

    t->local_capacity = 1024;
    t->local_count = 0;
    t->local_primes = malloc(t->local_capacity * sizeof(int));
    if (t->local_primes == NULL) {
        fprintf(stderr, "Thread %d: memory allocation failed.\n", t->thread_id);
        pthread_exit(NULL);
    }

    for (i = t->start; i < t->end; i++) {
        if (is_prime(i)) {
            if (t->local_count == t->local_capacity) {
                t->local_capacity *= 2;
                t->local_primes = realloc(t->local_primes,
                                           t->local_capacity * sizeof(int));
                if (t->local_primes == NULL) {
                    fprintf(stderr, "Thread %d: memory reallocation failed.\n",
                            t->thread_id);
                    pthread_exit(NULL);
                }
            }
            t->local_primes[t->local_count] = i;
            t->local_count++;
        }
    }

    return NULL;
}

int main(void)
{
    int n, num_threads;
    int i, j;
    FILE *file;
    struct timespec start_ts, end_ts;
    double elapsed_time;
    pthread_t *threads;
    thread_arg_t *targs;
    int total_count = 0;
    int *primes; /* final concatenated, sorted array */
    int chunk_size, cur;

    printf("Enter an integer n: ");
    scanf("%d", &n);

    printf("Enter number of threads: ");
    scanf("%d", &num_threads);

    if (n <= 2) {
        printf("There are no prime numbers strictly less than %d.\n", n);
        return 0;
    }
    if (num_threads < 1) {
        fprintf(stderr, "Number of threads must be >= 1.\n");
        return 1;
    }
    /* Don't create more threads than there are numbers to test. */
    if (num_threads > n - 2)
        num_threads = n - 2;

    threads = malloc(num_threads * sizeof(pthread_t));
    targs = malloc(num_threads * sizeof(thread_arg_t));
    if (threads == NULL || targs == NULL) {
        fprintf(stderr, "Memory allocation failed.\n");
        return 1;
    }

    /* --- Block partitioning of [2, n) among num_threads threads --- */
    chunk_size = (n - 2 + num_threads - 1) / num_threads; /* ceil division */
    cur = 2;

    clock_gettime(CLOCK_MONOTONIC, &start_ts);

    for (i = 0; i < num_threads; i++) {
        targs[i].thread_id = i;
        targs[i].start = cur;
        targs[i].end = cur + chunk_size;
        if (targs[i].end > n)
            targs[i].end = n;
        cur = targs[i].end;

        pthread_create(&threads[i], NULL, worker, &targs[i]);
    }

    for (i = 0; i < num_threads; i++) {
        pthread_join(threads[i], NULL);
        total_count += targs[i].local_count;
    }

    clock_gettime(CLOCK_MONOTONIC, &end_ts);
    elapsed_time = (end_ts.tv_sec - start_ts.tv_sec) +
                   (end_ts.tv_nsec - start_ts.tv_nsec) / 1e9;

    /* --- Concatenate thread-local results in thread order.
     *     Ranges are contiguous & increasing, so this is already sorted. --- */
    primes = malloc(total_count * sizeof(int));
    if (primes == NULL) {
        fprintf(stderr, "Memory allocation failed.\n");
        return 1;
    }
    {
        int idx = 0;
        for (i = 0; i < num_threads; i++) {
            for (j = 0; j < targs[i].local_count; j++) {
                primes[idx++] = targs[i].local_primes[j];
            }
            free(targs[i].local_primes);
        }
    }

    /* --- Output: same behaviour as task1.c --- */
    if (n < 100) {
        printf("\nPrime numbers less than %d:\n", n);
        for (i = 0; i < total_count; i++) {
            printf("%d", primes[i]);
            if (i < total_count - 1)
                printf(", ");
        }
        printf("\n");
    } else {
        file = fopen("primes_parallel.txt", "w");
        if (file == NULL) {
            fprintf(stderr, "Could not open primes_parallel.txt for writing.\n");
            free(primes);
            free(threads);
            free(targs);
            return 1;
        }
        fprintf(file, "Prime numbers less than %d:\n", n);
        for (i = 0; i < total_count; i++) {
            fprintf(file, "%d", primes[i]);
            if (i < total_count - 1)
                fprintf(file, ", ");
        }
        fprintf(file, "\n");
        fclose(file);
        printf("\nPrime numbers have been written to primes_parallel.txt\n");
    }

    printf("Number of primes found: %d\n", total_count);
    printf("Number of threads used: %d\n", num_threads);
    printf("Execution time (wall clock): %.6f seconds\n", elapsed_time);

    free(primes);
    free(threads);
    free(targs);
    return 0;
}