
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <mpi.h>

#define ITERATIONS 500
#define SHIFT_ROW 0
#define SHIFT_COL 1
#define DISP 1

/* Check if a number is prime. Returns 1 if prime, 0 otherwise. */
int is_prime(int n) {
    if (n < 2) {
        return 0;
    }

    if (n == 2) {
        return 1;
    }

    if (n % 2 == 0) {
        return 0;
    }

    for (int i = 3; i <= n / i; i += 2) {
        if (n % i == 0) {
            return 0;
        }
    }

    return 1;
}

/* Generate a random prime number between 2 and 1,000,000. */
int generate_prime(void) {
    int n;

    do {
        n = 2 + rand() % 999999;
    } while (!is_prime(n));

    return n;
}

int main(int argc, char *argv[]) {
    int ndims = 2, size, my_rank, my_cart_rank;
    int nrows, ncols, ierr;
    int dims[2], coord[2], wrap_around[2];
    int nbr_i_lo, nbr_i_hi, nbr_j_lo, nbr_j_hi;
    int reorder = 1;
    MPI_Comm comm2D;

    MPI_Init(&argc, &argv);
    MPI_Comm_size(MPI_COMM_WORLD, &size);
    MPI_Comm_rank(MPI_COMM_WORLD, &my_rank);

    /* Read grid dimensions from command-line arguments. */
    if (argc == 3) {
        nrows = atoi(argv[1]);
        ncols = atoi(argv[2]);

        if (nrows <= 0 || ncols <= 0 ||
            nrows * ncols != size) {
            if (my_rank == 0)
                fprintf(stderr, "Invalid grid dimensions.\n");
            MPI_Abort(MPI_COMM_WORLD, 1);
        }

        dims[0] = nrows;
        dims[1] = ncols;
    } else if (argc == 1) {
        dims[0] = dims[1] = 0;
        MPI_Dims_create(size, ndims, dims);
    } else {
        if (my_rank == 0)
            fprintf(stderr, "Usage: mpirun -np N ./task2 [rows cols]\n");
        MPI_Abort(MPI_COMM_WORLD, 1);
    }

    /* Create the non-periodic Cartesian grid. */
    wrap_around[0] = wrap_around[1] = 0;

    ierr = MPI_Cart_create(MPI_COMM_WORLD, ndims, dims,
                           wrap_around, reorder, &comm2D);
    if (ierr != MPI_SUCCESS)
        MPI_Abort(MPI_COMM_WORLD, ierr);

    /* Find this process's Cartesian rank and coordinates. */
    MPI_Comm_rank(comm2D, &my_cart_rank);
    MPI_Cart_coords(comm2D, my_cart_rank, ndims, coord);

    /* Find neighbours along rows and columns. */
    MPI_Cart_shift(comm2D, SHIFT_ROW, DISP,
                   &nbr_i_lo, &nbr_i_hi);
    MPI_Cart_shift(comm2D, SHIFT_COL, DISP,
                   &nbr_j_lo, &nbr_j_hi);

    /* Create one log file per receiving process, even if no matches occur. */
    char filename[64];
    snprintf(filename, sizeof(filename), "rank_%d.txt", my_cart_rank);

    FILE *log = fopen(filename, "w");
    if (log == NULL) {
        fprintf(stderr, "Rank %d could not open %s\n",
                my_cart_rank, filename);
        MPI_Abort(comm2D, 1);
    }

    /* Different seeds give processes different random sequences. */
    srand((unsigned int)time(NULL) +
          (unsigned int)my_cart_rank * 12345U);

    for (int iter = 0; iter < ITERATIONS; iter++) {
        int my_prime = generate_prime();

        /* Initialise receive buffers because MPI_PROC_NULL does not
           provide a received value. */
        int from_top = -1, from_bottom = -1;
        int from_left = -1, from_right = -1;

        /* Exchange along the row dimension: top and bottom. */
        MPI_Sendrecv(&my_prime, 1, MPI_INT, nbr_i_lo, 0,
                     &from_bottom, 1, MPI_INT, nbr_i_hi, 0,
                     comm2D, MPI_STATUS_IGNORE);

        MPI_Sendrecv(&my_prime, 1, MPI_INT, nbr_i_hi, 1,
                     &from_top, 1, MPI_INT, nbr_i_lo, 1,
                     comm2D, MPI_STATUS_IGNORE);

        /* Exchange along the column dimension: left and right. */
        MPI_Sendrecv(&my_prime, 1, MPI_INT, nbr_j_lo, 2,
                     &from_right, 1, MPI_INT, nbr_j_hi, 2,
                     comm2D, MPI_STATUS_IGNORE);

        MPI_Sendrecv(&my_prime, 1, MPI_INT, nbr_j_hi, 3,
                     &from_left, 1, MPI_INT, nbr_j_lo, 3,
                     comm2D, MPI_STATUS_IGNORE);

        /* Log every match received from a real neighbour. */
        if (nbr_i_lo != MPI_PROC_NULL && from_top == my_prime)
            fprintf(log, "iteration=%d prime=%d receiver_rank=%d sender_rank=%d\n",
                    iter, my_prime, my_cart_rank, nbr_i_lo);

        if (nbr_i_hi != MPI_PROC_NULL && from_bottom == my_prime)
            fprintf(log, "iteration=%d prime=%d receiver_rank=%d sender_rank=%d\n",
                    iter, my_prime, my_cart_rank, nbr_i_hi);

        if (nbr_j_lo != MPI_PROC_NULL && from_left == my_prime)
            fprintf(log, "iteration=%d prime=%d receiver_rank=%d sender_rank=%d\n",
                    iter, my_prime, my_cart_rank, nbr_j_lo);

        if (nbr_j_hi != MPI_PROC_NULL && from_right == my_prime)
            fprintf(log, "iteration=%d prime=%d receiver_rank=%d sender_rank=%d\n",
                    iter, my_prime, my_cart_rank, nbr_j_hi);
    }

    fclose(log);

    MPI_Comm_free(&comm2D);
    MPI_Finalize();
    return 0;
}
