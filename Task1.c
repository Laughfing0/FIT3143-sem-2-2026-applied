#include <stdio.h>
#include <math.h>
#include <stdlib.h>
#include <time.h>


int prime_check(int k)
{
    int i;

    if (k < 2) {
        return 0;
    }

    if (k == 2) {
        return 1;
    }

    if (k % 2 == 0) {
        return 0;
    }
}


// Task 1 - Serial Code - Finding Prime Numbers

/*
Requirements:

- User inputs integer n
- Outputs a list of sorted prime numbers

- std output for n < 100
- txt file for n > 100
*/



int main(int argc, char *argv[]) {

    // take user input
    long long number;
    printf("Input integer: \n");
    scanf("%lld", &number);



    // check command line requirements
    if (argc != 1 ) {
        printf("Usage: integer\n");
        return 1;
    }


    // Once we have input 


    



}
