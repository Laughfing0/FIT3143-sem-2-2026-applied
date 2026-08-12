/* task1.c
 * Find and print all prime numbers strictly less than n.
 *
 * Compile:
 *     gcc task1.c -o task1 -lm
 *
 * Run:
 *     ./task1
 */

#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <time.h>

int is_prime(int k)
{
    int i;

    if (k < 2)
        return 0;

    if (k == 2)
        return 1;

    if (k % 2 == 0)
        return 0;

    /* Only check divisors up to sqrt(k). */
    for (i = 3; i <= (int)sqrt((double)k); i += 2) {
        if (k % i == 0)
            return 0;
    }

    return 1;
}

int main(void)
{
    int n;
    int *primes;
    int count = 0;
    int capacity = 1024;
    int i;
    FILE *file;
    clock_t start, end;
    double elapsed_time;

    printf("Enter an integer n: ");
    scanf("%d", &n);

    if (n <= 2) {
        printf("There are no prime numbers strictly less than %d.\n", n);
        return 0;
    }

    /* Allocate initial memory for storing prime numbers. */
    primes = malloc(capacity * sizeof(int));

    if (primes == NULL) {
        fprintf(stderr, "Memory allocation failed.\n");
        return 1;
    }

    /* Start measuring execution time. */
    start = clock();

    /* Search for prime numbers strictly less than n. */
    for (i = 2; i < n; i++) {
        if (is_prime(i)) {

            /* Increase storage if necessary. */
            if (count == capacity) {
                capacity *= 2;

                primes = realloc(primes, capacity * sizeof(int));

                if (primes == NULL) {
                    fprintf(stderr, "Memory reallocation failed.\n");
                    return 1;
                }
            }

            primes[count] = i;
            count++;
        }
    }

    /* Stop measuring execution time. */
    end = clock();

    elapsed_time = (double)(end - start) / CLOCKS_PER_SEC;

    /*
     * For small n, print primes to standard output.
     * For larger n, write them to a text file.
     */
    if (n < 100) {
        printf("\nPrime numbers less than %d:\n", n);

        for (i = 0; i < count; i++) {
            printf("%d", primes[i]);

            if (i < count - 1)
                printf(", ");
        }

        printf("\n");
    } else {
        file = fopen("primes.txt", "w");

        if (file == NULL) {
            fprintf(stderr, "Could not open primes.txt for writing.\n");
            free(primes);
            return 1;
        }

        fprintf(file, "Prime numbers less than %d:\n", n);

        for (i = 0; i < count; i++) {
            fprintf(file, "%d", primes[i]);

            if (i < count - 1)
                fprintf(file, ", ");
        }

        fprintf(file, "\n");

        fclose(file);

        printf("\nPrime numbers have been written to primes.txt\n");
    }

    printf("Number of primes found: %d\n", count);
    printf("Execution time: %.6f seconds\n", elapsed_time);

    free(primes);

    return 0;
}