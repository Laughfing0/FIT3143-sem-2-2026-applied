/* Task1.c
 *
 * Purpose: Find and print all prime numbers strictly less than an integer n entered by the user.
 *
 * Compile:
 *     gcc task1.c -o task1 -lm
 *
 * Run:
 *     ./task1
 */

/* Imports needed for the task */
#include <stdio.h> /* Gives standard Input/Output functionality */
#include <stdlib.h> /* Gives general purpose functions needed for memory management */
#include <math.h> /* Gives mathematical functions needed to calculate square root */
#include <time.h> /* Gives time functions needed to measure execution time */

/* Function to check if a number is prime */
int is_prime(int k) /* The functions accepts an integer k and returns an integer. It returns 1 if k is prime, 0 otherwise */
{
    int i; /* Variable to iterate through possible divisors of k */

    /* Numbers less than 2 are not prime. */
    if (k < 2)
        return 0; 

    /* 2 is the only even prime number. */
    if (k == 2)
        return 1;

    if (k % 2 == 0)
        return 0;

    /* Only check odd divisors up to sqrt(k). */
    for (i = 3; i <= (int)sqrt((double)k); i += 2) { /* Use double to convert k to a floating-point number for using sqrt then convert back to int */

        /* If k is exactly divisible by i, then k is not prime. */
        if (k % i == 0)
            return 0;
    }

    /* If no divisors were found, k is prime. */
    return 1;
}

/* Main function to find and print prime numbers */
int main(void) /* The main function does not accept any arguments and returns an integer */
{
    int n; /* Variable to store the integer entered by the user */
    int *primes; /* Pointer to dynamically allocated array to store prime numbers */
    int count = 0; /* Variable to count the number of prime numbers found */
    int capacity = 1024; /* Initial capacity for the primes array */
    int i; /* Variable to iterate through numbers */
    FILE *file; /* File pointer to write prime numbers to a file */
    clock_t start, end; /* Variables to measure execution time of type clock_t */
    double elapsed_time; /* Variable to store elapsed time in seconds */

    printf("Enter an integer n: "); /* Prompt the user to enter an integer n */
    scanf("%d", &n); /* Read the integer n entered by the user, storing it in the variable n using the address-of operator & */

    if (n <= 2) {
        printf("There are no prime numbers strictly less than %d.\n", n); /* Here %d acts as a placeholder for the value of n */
        return 0; /* Exit the program if n is less than or equal to 2 */
    }

    /* Allocate initial memory for storing prime numbers using the capacity and size needed to store a single integer. */
    primes = malloc(capacity * sizeof(int));

    /* Check if memory allocation was successful. */
    if (primes == NULL) {
        fprintf(stderr, "Memory allocation failed.\n"); /* Prints a standard error message */
        return 1; /* Exit the program with a 1 to indicate an error */
    }

    /* Start measuring execution time using the clock() function */
    start = clock();

    /* Search for prime numbers strictly less than n. */
    for (i = 2; i < n; i++) {
        if (is_prime(i)) { /* Check if the current number i is prime using the is_prime function, dividing the number by all integers from 2 to sqrt(i) to check for divisibility */

            /* Increase storage if necessary. */
            if (count == capacity) {
                capacity *= 2; /* Double the capacity of the primes array to accommodate more prime numbers */

                primes = realloc(primes, capacity * sizeof(int)); /* Reallocate memory for the primes array with the new capacity, using realloc to resize the previously allocated memory block */

                if (primes == NULL) {
                    fprintf(stderr, "Memory reallocation failed.\n");
                    return 1;
                }
            }

            primes[count] = i; /* Store the prime number i in the primes array at the current count index to maintain sorted order */
            count++; /* Increment the count of prime numbers found */
        }
    }

    /* Stop measuring execution time. */
    end = clock();

    /* Calculate elapsed time in seconds by subtracting the start time from the end time and dividing by CLOCKS_PER_SEC to convert clock ticks to seconds. */
    elapsed_time = (double)(end - start) / CLOCKS_PER_SEC;

    /*
     * For small n, print primes to standard output.
     * For larger n, write them to a text file.
     */
    if (n < 100) {
        printf("\nPrime numbers less than %d:\n", n); /* Print a header for the list of prime numbers to the terminal */

        /* Print all primes in ascending order, separated by commas. */
        for (i = 0; i < count; i++) {
            printf("%d", primes[i]); /* Print the current prime number of type int */

            if (i < count - 1) /* Print a comma between values, but not after the final value. */
                printf(", ");
        }

        printf("\n");

    } else {
        file = fopen("Primes.txt", "w"); /* Open a file named "Primes.txt" for writing */

        /* Check if the file was opened successfully. If not, print an error message and free the allocated memory before exiting. */
        if (file == NULL) {
            fprintf(stderr, "Could not open Primes.txt for writing.\n");
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

        fclose(file); /* Close the file after writing all prime numbers to it */

        printf("\nPrime numbers have been written to Primes.txt\n");
    }

    printf("Number of primes found: %d\n", count);
    printf("Execution time: %.6f seconds\n", elapsed_time); /* Print the execution time in seconds, to six decimal places */

    free(primes); /* Free the dynamically allocated memory for the primes array to prevent memory leaks */

    return 0; /* Exit the program successfully with a return value of 0 */
}