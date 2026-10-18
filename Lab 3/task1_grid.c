
#include <mpi.h>
#include <omp.h>

#include <stdio.h>
#include <stdlib.h>
#include <limits.h>
#include <time.h>

/* Simulation settings */
#define NUM_PORTS 6
#define THRESHOLD 2
#define MAX_ITERATIONS 10

/* MPI message tags */
#define TAG_AVAILABILITY 100
#define TAG_REQUEST      200
#define TAG_RESPONSE     300
#define TAG_STATUS       400
#define TAG_CONTROL      500

/* Report fields sent from each node to the base station */
#define REPORT_FIELDS 18

enum {
    REPORT_ITERATION = 0,
    REPORT_NODE,
    REPORT_AVAILABLE,
    REPORT_LOW,
    REPORT_CONGESTION,
    REPORT_NORTH,
    REPORT_SOUTH,
    REPORT_WEST,
    REPORT_EAST,
    REPORT_NORTH_AVAILABLE,
    REPORT_SOUTH_AVAILABLE,
    REPORT_WEST_AVAILABLE,
    REPORT_EAST_AVAILABLE,
    REPORT_MESSAGES_SENT,
    REPORT_MESSAGES_RECEIVED,
    REPORT_BYTES_SENT,
    REPORT_BYTES_RECEIVED,
    REPORT_TIMESTAMP
};

/* ---------------------------------------------------------
   BASE STATION

   Receives one status report from every node per iteration.
   Logs congestion and recommends the nearest available node(s).
   Sends a continue/terminate command to every charging node.
   --------------------------------------------------------- */

static void run_base_station(int rows, int cols, int world_size)
{
    int node_count = world_size - 1;

    /* Reports are indexed by Cartesian grid rank. */
    long long (*reports)[REPORT_FIELDS] =
        calloc((size_t)node_count, sizeof(*reports));

    if (reports == NULL) {
        fprintf(stderr, "Base station: memory allocation failed.\n");
        MPI_Abort(MPI_COMM_WORLD, 1);
    }

    printf("\nBase station started. Grid: %d x %d, nodes: %d\n\n",
           rows, cols, node_count);

    for (int iteration = 0;
         iteration < MAX_ITERATIONS;
         iteration++) {

        /*
         * Receive exactly one report from every charging node.
         * Reports may arrive in any order.
         */
        for (int received = 0; received < node_count; received++) {
            long long report[REPORT_FIELDS];

            MPI_Recv(report,
                     REPORT_FIELDS,
                     MPI_LONG_LONG_INT,
                     MPI_ANY_SOURCE,
                     TAG_STATUS,
                     MPI_COMM_WORLD,
                     MPI_STATUS_IGNORE);

            int id = (int)report[REPORT_NODE];

            if (id < 0 || id >= node_count) {
                fprintf(stderr, "Base station: invalid node ID.\n");
                MPI_Abort(MPI_COMM_WORLD, 2);
            }

            for (int field = 0; field < REPORT_FIELDS; field++) {
                reports[id][field] = report[field];
            }
        }

        printf("========== ITERATION %d ==========\n", iteration);

        /* Log nodes that cannot redirect to a free neighbour. */
        for (int id = 0; id < node_count; id++) {
            long long *r = reports[id];

            if (r[REPORT_CONGESTION] == 1) {
                printf(
                    "[BASE] CONGESTION timestamp=%lld "
                    "node=%d availability=%lld/%d\n",
                    r[REPORT_TIMESTAMP],
                    id,
                    r[REPORT_AVAILABLE],
                    NUM_PORTS
                );

                printf(
                    "       Neighbours "
                    "N=%lld (available=%lld), "
                    "S=%lld (available=%lld), "
                    "W=%lld (available=%lld), "
                    "E=%lld (available=%lld)\n",
                    r[REPORT_NORTH],
                    r[REPORT_NORTH_AVAILABLE],
                    r[REPORT_SOUTH],
                    r[REPORT_SOUTH_AVAILABLE],
                    r[REPORT_WEST],
                    r[REPORT_WEST_AVAILABLE],
                    r[REPORT_EAST],
                    r[REPORT_EAST_AVAILABLE]
                );

                printf(
                    "       Neighbour traffic: "
                    "sent=%lld messages (%lld bytes), "
                    "received=%lld messages (%lld bytes)\n",
                    r[REPORT_MESSAGES_SENT],
                    r[REPORT_BYTES_SENT],
                    r[REPORT_MESSAGES_RECEIVED],
                    r[REPORT_BYTES_RECEIVED]
                );
            }
        }

        /*
         * Send each node a control message:
         * response[0] = 1 if simulation should terminate
         * response[1] = number of recommended nodes
         * response[2...] = recommended Cartesian node IDs
         *
         * For a congestion report, find every available node
         * at the minimum Manhattan distance from the reporting node.
         */
        for (int id = 0; id < node_count; id++) {
            size_t response_length = (size_t)node_count + 2;

            long long *response =
                malloc(response_length * sizeof(*response));

            if (response == NULL) {
                fprintf(stderr,
                        "Base station: response allocation failed.\n");
                MPI_Abort(MPI_COMM_WORLD, 3);
            }

            for (int i = 0; i < (int)response_length; i++) {
                response[i] = -1;
            }

            response[0] =
                (iteration == MAX_ITERATIONS - 1) ? 1 : 0;
            response[1] = 0;

            if (reports[id][REPORT_CONGESTION] == 1) {
                int source_row = id / cols;
                int source_col = id % cols;
                int nearest_distance = INT_MAX;
                int recommendation_count = 0;

                for (int candidate = 0;
                     candidate < node_count;
                     candidate++) {

                    /* Only recommend nodes with free ports. */
                    if (reports[candidate][REPORT_AVAILABLE] <= 0) {
                        continue;
                    }

                    if (candidate == id) {
                        continue;
                    }

                    int candidate_row = candidate / cols;
                    int candidate_col = candidate % cols;

                    int distance =
                        abs(candidate_row - source_row) +
                        abs(candidate_col - source_col);

                    if (distance < nearest_distance) {
                        nearest_distance = distance;
                        recommendation_count = 0;
                    }

                    if (distance == nearest_distance) {
                        response[2 + recommendation_count] =
                            candidate;
                        recommendation_count++;
                    }
                }

                response[1] = recommendation_count;

                if (recommendation_count == 0) {
                    printf(
                        "[BASE] No available charging node "
                        "for node %d in iteration %d.\n",
                        id, iteration
                    );
                } else {
                    printf("[BASE] Recommendation for node %d: ",
                           id);

                    for (int j = 0; j < recommendation_count; j++) {
                        printf("%lld%s",
                               response[2 + j],
                               (j + 1 < recommendation_count)
                                   ? ", " : "");
                    }

                    printf(" (nearest available node(s))\n");
                }
            }

            /*
             * Grid rank id corresponds to world rank id + 1,
             * because world rank 0 is the base station.
             */
            MPI_Send(response,
                     (int)response_length,
                     MPI_LONG_LONG_INT,
                     id + 1,
                     TAG_CONTROL,
                     MPI_COMM_WORLD);

            free(response);
        }

        printf("\n");
    }

    free(reports);

    printf("Base station: all termination messages sent.\n");
}

/* ---------------------------------------------------------
   CHARGING NODE
   --------------------------------------------------------- */

static void run_charging_node(MPI_Comm node_comm,
                              int rows,
                              int cols,
                              int world_rank)
{
    int dims[2] = {rows, cols};
    int periods[2] = {0, 0};

    MPI_Comm grid_comm = MPI_COMM_NULL;

    /*
     * Build a non-periodic 2D Cartesian grid using only
     * charging-node processes.
     */
    MPI_Cart_create(node_comm,
                    2,
                    dims,
                    periods,
                    0,
                    &grid_comm);

    if (grid_comm == MPI_COMM_NULL) {
        fprintf(stderr, "World rank %d: grid creation failed.\n",
                world_rank);
        MPI_Abort(MPI_COMM_WORLD, 4);
    }

    int grid_rank;
    int coords[2];

    MPI_Comm_rank(grid_comm, &grid_rank);
    MPI_Cart_coords(grid_comm, grid_rank, 2, coords);

    int north, south, west, east;

    MPI_Cart_shift(grid_comm, 0, 1, &north, &south);
    MPI_Cart_shift(grid_comm, 1, 1, &west, &east);

    int neighbours[4] = {north, south, west, east};

    printf(
        "Node %d at (%d,%d): N=%d S=%d W=%d E=%d\n",
        grid_rank, coords[0], coords[1],
        north, south, west, east
    );

    int port_status[NUM_PORTS] = {0};
    int iteration = 0;

    while (1) {
        int available = 0;

        /*
         * STEP 1: Simulate the port status update.
         *
         * Most cycles: two ports are free.
         * Every fifth cycle: the top-left 2x2 area is full.
         *
         * This creates repeatable test cases for both redirection
         * and congestion. Replace this with real/random port updates
         * when the communication protocol is working.
         */
        #pragma omp parallel for shared(port_status)
        for (int i = 0; i < NUM_PORTS; i++) {
            if ((iteration % 5 == 4) &&
                (coords[0] < 2) &&
                (coords[1] < 2)) {

                port_status[i] = 1;  /* Occupied */
            } else {
                port_status[i] =
                    ((i + grid_rank + iteration) % 3 != 0)
                        ? 1 : 0;
            }
        }

        /* Count free ports using an OpenMP reduction. */
        #pragma omp parallel for reduction(+:available) \
            shared(port_status)
        for (int i = 0; i < NUM_PORTS; i++) {
            if (port_status[i] == 0) {
                available++;
            }
        }

        int low = (available <= THRESHOLD);
        int neighbour_available[4] = {-1, -1, -1, -1};
        int response_available[4] = {-1, -1, -1, -1};
        int incoming_request[4] = {-1, -1, -1, -1};

        long long messages_sent = 0;
        long long messages_received = 0;

        int degree = 0;

        for (int d = 0; d < 4; d++) {
            if (neighbours[d] != MPI_PROC_NULL) {
                degree++;
            }
        }

        /*
         * STEP 2: Exchange availability with every immediate
         * neighbour. All receives are posted before waiting, so
         * adjacent nodes cannot deadlock by waiting on each other.
         */
        MPI_Request requests[8];
        int request_count = 0;

        for (int d = 0; d < 4; d++) {
            if (neighbours[d] != MPI_PROC_NULL) {
                MPI_Irecv(&neighbour_available[d],
                          1,
                          MPI_INT,
                          neighbours[d],
                          TAG_AVAILABILITY,
                          grid_comm,
                          &requests[request_count++]);
            }
        }

        for (int d = 0; d < 4; d++) {
            if (neighbours[d] != MPI_PROC_NULL) {
                MPI_Isend(&available,
                          1,
                          MPI_INT,
                          neighbours[d],
                          TAG_AVAILABILITY,
                          grid_comm,
                          &requests[request_count++]);

                messages_sent++;
            }
        }

        MPI_Waitall(request_count, requests, MPI_STATUSES_IGNORE);
        messages_received += degree;

        /*
         * STEP 3: Nodes with low availability request current
         * availability information from each neighbour.
         *
         * A node expects requests from neighbours whose availability
         * is also at or below the threshold.
         */
        request_count = 0;
        int request_value = grid_rank;

        if (low) {
            for (int d = 0; d < 4; d++) {
                if (neighbours[d] != MPI_PROC_NULL) {
                    MPI_Isend(&request_value,
                              1,
                              MPI_INT,
                              neighbours[d],
                              TAG_REQUEST,
                              grid_comm,
                              &requests[request_count++]);

                    messages_sent++;
                }
            }
        }

        for (int d = 0; d < 4; d++) {
            if (neighbours[d] != MPI_PROC_NULL &&
                neighbour_available[d] <= THRESHOLD) {

                MPI_Irecv(&incoming_request[d],
                          1,
                          MPI_INT,
                          neighbours[d],
                          TAG_REQUEST,
                          grid_comm,
                          &requests[request_count++]);
            }
        }

        MPI_Waitall(request_count, requests, MPI_STATUSES_IGNORE);

        for (int d = 0; d < 4; d++) {
            if (neighbours[d] != MPI_PROC_NULL &&
                neighbour_available[d] <= THRESHOLD) {
                messages_received++;
            }
        }

        /*
         * STEP 4: Reply to received requests with our availability.
         * Low-availability nodes also receive replies to their
         * outgoing requests.
         */
        request_count = 0;

        if (low) {
            for (int d = 0; d < 4; d++) {
                if (neighbours[d] != MPI_PROC_NULL) {
                    MPI_Irecv(&response_available[d],
                              1,
                              MPI_INT,
                              neighbours[d],
                              TAG_RESPONSE,
                              grid_comm,
                              &requests[request_count++]);
                }
            }
        }

        for (int d = 0; d < 4; d++) {
            if (neighbours[d] != MPI_PROC_NULL &&
                neighbour_available[d] <= THRESHOLD) {

                MPI_Isend(&available,
                          1,
                          MPI_INT,
                          neighbours[d],
                          TAG_RESPONSE,
                          grid_comm,
                          &requests[request_count++]);

                messages_sent++;
            }
        }

        MPI_Waitall(request_count, requests, MPI_STATUSES_IGNORE);

        if (low) {
            messages_received += degree;
        }

        /*
         * Use the reply for requested neighbours, otherwise use
         * the availability from the periodic neighbour exchange.
         */
        int final_neighbour_available[4];

        for (int d = 0; d < 4; d++) {
            if (neighbours[d] == MPI_PROC_NULL) {
                final_neighbour_available[d] = -1;
            } else if (low) {
                final_neighbour_available[d] =
                    response_available[d];
            } else {
                final_neighbour_available[d] =
                    neighbour_available[d];
            }
        }

        /*
         * STEP 5: Decide whether redirection is possible.
         * A neighbour with at least one free port can accept EVs.
         */
        int has_neighbour_space = 0;

        for (int d = 0; d < 4; d++) {
            if (neighbours[d] != MPI_PROC_NULL &&
                final_neighbour_available[d] > 0) {
                has_neighbour_space = 1;
            }
        }

        int congestion = low && !has_neighbour_space;

        printf(
            "Node %d iteration %d: available=%d/%d\n",
            grid_rank, iteration, available, NUM_PORTS
        );

        if (low && has_neighbour_space) {
            printf("Node %d: LOW AVAILABILITY; redirect to "
                   "neighbour(s):", grid_rank);

            for (int d = 0; d < 4; d++) {
                if (neighbours[d] != MPI_PROC_NULL &&
                    final_neighbour_available[d] > 0) {
                    printf(" %d (%d free)",
                           neighbours[d],
                           final_neighbour_available[d]);
                }
            }

            printf("\n");
        }

        if (congestion) {
            printf(
                "Node %d: CONGESTION — no neighbouring node "
                "has a free port.\n",
                grid_rank
            );
        }

        /*
         * STEP 6: Send the status report to the base station.
         *
         * Message count/byte metrics here refer to neighbour
         * communication during this iteration. Each neighbour
         * message contains one int.
         */
        long long report[REPORT_FIELDS] = {0};

        report[REPORT_ITERATION] = iteration;
        report[REPORT_NODE] = grid_rank;
        report[REPORT_AVAILABLE] = available;
        report[REPORT_LOW] = low;
        report[REPORT_CONGESTION] = congestion;

        report[REPORT_NORTH] = north;
        report[REPORT_SOUTH] = south;
        report[REPORT_WEST] = west;
        report[REPORT_EAST] = east;

        report[REPORT_NORTH_AVAILABLE] =
            final_neighbour_available[0];
        report[REPORT_SOUTH_AVAILABLE] =
            final_neighbour_available[1];
        report[REPORT_WEST_AVAILABLE] =
            final_neighbour_available[2];
        report[REPORT_EAST_AVAILABLE] =
            final_neighbour_available[3];

        report[REPORT_MESSAGES_SENT] = messages_sent;
        report[REPORT_MESSAGES_RECEIVED] = messages_received;
        report[REPORT_BYTES_SENT] =
            messages_sent * (long long)sizeof(int);
        report[REPORT_BYTES_RECEIVED] =
            messages_received * (long long)sizeof(int);
        report[REPORT_TIMESTAMP] = (long long)time(NULL);

        MPI_Send(report,
                 REPORT_FIELDS,
                 MPI_LONG_LONG_INT,
                 0,
                 TAG_STATUS,
                 MPI_COMM_WORLD);

        /*
         * STEP 7: Wait for the base station's recommendation
         * and continue/terminate command.
         */
        size_t response_length = (size_t)rows * (size_t)cols + 2;

        long long *response =
            malloc(response_length * sizeof(*response));

        if (response == NULL) {
            fprintf(stderr, "Node %d: memory allocation failed.\n",
                    grid_rank);
            MPI_Abort(MPI_COMM_WORLD, 5);
        }

        MPI_Recv(response,
                 (int)response_length,
                 MPI_LONG_LONG_INT,
                 0,
                 TAG_CONTROL,
                 MPI_COMM_WORLD,
                 MPI_STATUS_IGNORE);

        int terminate = (int)response[0];
        int recommendation_count = (int)response[1];

        if (congestion) {
            if (recommendation_count == 0) {
                printf("Node %d: base station reports no "
                       "available charging node.\n", grid_rank);
            } else {
                printf("Node %d: forwarding base-station "
                       "recommendation to EV(s):", grid_rank);

                for (int i = 0; i < recommendation_count; i++) {
                    printf(" %lld", response[2 + i]);
                }

                printf("\n");
            }
        }

        free(response);

        if (terminate) {
            printf("Node %d: received termination command.\n",
                   grid_rank);
            break;
        }

        iteration++;
    }

    MPI_Comm_free(&grid_comm);
}

/* ---------------------------------------------------------
   MAIN
   --------------------------------------------------------- */

int main(int argc, char **argv)
{
    int world_rank;
    int world_size;
    int rows = 0;
    int cols = 0;
    int provided;

    MPI_Init_thread(&argc,
                    &argv,
                    MPI_THREAD_FUNNELED,
                    &provided);

    if (provided < MPI_THREAD_FUNNELED) {
        fprintf(stderr,
                "MPI implementation does not provide "
                "MPI_THREAD_FUNNELED.\n");
        MPI_Abort(MPI_COMM_WORLD, 6);
    }

    MPI_Comm_rank(MPI_COMM_WORLD, &world_rank);
    MPI_Comm_size(MPI_COMM_WORLD, &world_size);

    /*
     * World rank 0 is the base station.
     * Charging nodes are world ranks 1 through rows*cols.
     */
    if (world_rank == 0) {
        if (argc != 3) {
            fprintf(stderr,
                    "Usage: mpirun -np <rows*cols+1> "
                    "%s <rows> <cols>\n",
                    argv[0]);

            MPI_Abort(MPI_COMM_WORLD, 7);
        }

        rows = atoi(argv[1]);
        cols = atoi(argv[2]);

        if (rows <= 0 ||
            cols <= 0 ||
            rows > INT_MAX / cols ||
            rows * cols != world_size - 1) {

            fprintf(stderr,
                    "Error: rows and cols must be positive, "
                    "and rows*cols must equal world_size-1.\n");

            MPI_Abort(MPI_COMM_WORLD, 8);
        }
    }

    /* Share grid dimensions with every process. */
    MPI_Bcast(&rows, 1, MPI_INT, 0, MPI_COMM_WORLD);
    MPI_Bcast(&cols, 1, MPI_INT, 0, MPI_COMM_WORLD);

    /*
     * Exclude the base station from the charging-node communicator.
     * The key preserves ascending world-rank order.
     */
    MPI_Comm node_comm = MPI_COMM_NULL;

    int color = (world_rank == 0) ? MPI_UNDEFINED : 1;

    MPI_Comm_split(MPI_COMM_WORLD,
                   color,
                   world_rank,
                   &node_comm);

    if (world_rank == 0) {
        run_base_station(rows, cols, world_size);
    } else {
        run_charging_node(node_comm, rows, cols, world_rank);

        MPI_Comm_free(&node_comm);
    }

    MPI_Finalize();

    return 0;
}
