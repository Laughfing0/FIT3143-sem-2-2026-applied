/*
 * common.h - definitions shared by node.c (Task 1) and base.c (Task 2).
 *
 * Process layout (MPMD launch, world size = m*n + 1):
 *   ranks 0 .. m*n-1 : ev_node   (EV charging nodes, m x n Cartesian grid)
 *   rank  m*n        : ev_base   (base station)
 *
 * Every message is an array of doubles whose element [0] is the send time
 * (MPI_Wtime), set automatically by pool_send(). The receiver uses it to
 * compute the one-way communication delay (Task 3).
 */
#ifndef COMMON_H
#define COMMON_H

#define _DEFAULT_SOURCE
#include <mpi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

enum { TAG_REQ = 1, TAG_RESP, TAG_REPORT, TAG_REPLY, TAG_TERM, TAG_DONE, TAG_FINAL };
enum { T_REQ, T_RESP, T_REPORT, T_REPLY, T_TERM, T_DONE, NTYPES };   /* T_x == TAG_x - 1 */
static const char *TNAME[NTYPES] = {
    "neighbour_request", "neighbour_response", "report_to_base",
    "base_reply", "terminate", "done"};

#define MAXC     64                 /* max candidate nodes in a base reply  */
#define MAXBUF   (MAXC + 2)         /* largest message, in doubles          */
#define REQ_LEN  2                  /* [t, rank]                            */
#define RESP_LEN 3                  /* [t, free, k]                         */
#define RPT_LEN  23                 /* see node_resolve() in node.c         */
#define DONE_LEN (2 + 3 * NTYPES + 5)
#define POLL_US  100

typedef struct {
    int m, n, k, thr, cycle_ms;
    double sim_s, period, busy_prob;
    unsigned seed;
} cfg_t;

/* args: m n [k=6] [sim_s=30] [cycle_ms=500] [thr=2] [period_s=5] [busy_prob=0.8]
 * returns 0 if ok, 1 on error (message printed by rank 0). */
static inline int parse_cfg(int argc, char **argv, int rank, int size, cfg_t *c)
{
    if (argc < 3) {
        if (rank == 0)
            fprintf(stderr, "Usage: %s m n [k] [sim_s] [cycle_ms] [thr] [period_s] [busy_prob]\n", argv[0]);
        return 1;
    }
    c->m = atoi(argv[1]); c->n = atoi(argv[2]);
    c->k = argc > 3 ? atoi(argv[3]) : 6;
    c->sim_s = argc > 4 ? atof(argv[4]) : 30.0;
    c->cycle_ms = argc > 5 ? atoi(argv[5]) : 500;
    c->thr = argc > 6 ? atoi(argv[6]) : 2;
    c->period = argc > 7 ? atof(argv[7]) : 5.0;
    c->busy_prob = argc > 8 ? atof(argv[8]) : 0.8;
    c->seed = (unsigned)time(NULL);
    if (c->m < 1 || c->n < 1 || c->k < 1 || c->cycle_ms < 5 || size != c->m * c->n + 1) {
        if (rank == 0)
            fprintf(stderr, "Invalid arguments: need m,n,k >= 1, cycle_ms >= 5 and "
                            "total processes = m*n+1 = %d (got %d)\n", c->m * c->n + 1, size);
        return 1;
    }
    return 0;
}

/* ---------- delay statistics ---------- */
typedef struct { double cnt, sum, max; } dstat;
static inline void dstat_add(dstat *s, double d)
{
    s->cnt += 1; s->sum += d; if (d > s->max) s->max = d;
}

/* ---------- non-blocking send pool (no blocking send calls) ---------- */
typedef struct { MPI_Request req; double buf[MAXBUF]; } slot_t;
typedef struct { slot_t *s; int n; long msgs, bytes; } pool_t;

static inline void pool_init(pool_t *p, int n)
{
    p->s = malloc(n * sizeof(slot_t));
    p->n = n; p->msgs = p->bytes = 0;
    for (int i = 0; i < n; i++) p->s[i].req = MPI_REQUEST_NULL;
}

static inline void pool_send(pool_t *p, int dest, int tag, const double *data, int count)
{
    int idx = -1, flag;
    while (idx < 0) {
        for (int i = 0; i < p->n; i++) {
            if (p->s[i].req != MPI_REQUEST_NULL)
                MPI_Test(&p->s[i].req, &flag, MPI_STATUS_IGNORE);
            if (p->s[i].req == MPI_REQUEST_NULL) { idx = i; break; }
        }
        if (idx < 0) usleep(10);
    }
    memcpy(p->s[idx].buf, data, count * sizeof(double));
    p->s[idx].buf[0] = MPI_Wtime();                    /* send timestamp */
    MPI_Isend(p->s[idx].buf, count, MPI_DOUBLE, dest, tag, MPI_COMM_WORLD, &p->s[idx].req);
    p->msgs++; p->bytes += count * sizeof(double);
}

static inline void pool_free(pool_t *p)
{
    for (int i = 0; i < p->n; i++)
        if (p->s[i].req != MPI_REQUEST_NULL) MPI_Wait(&p->s[i].req, MPI_STATUS_IGNORE);
    free(p->s);
}

static inline void now_ts(double ts[6])
{
    time_t t = time(NULL); struct tm tmv; localtime_r(&t, &tmv);
    ts[0] = tmv.tm_year + 1900; ts[1] = tmv.tm_mon + 1; ts[2] = tmv.tm_mday;
    ts[3] = tmv.tm_hour; ts[4] = tmv.tm_min; ts[5] = tmv.tm_sec;
}

/* neighbours in the grid: up, down, left, right (-1 if none); returns count */
static inline int grid_neighbors(int r, int m, int n, int out[4])
{
    int x = r / n, y = r % n, c = 0;
    out[0] = x > 0     ? (x - 1) * n + y : -1;
    out[1] = x < m - 1 ? (x + 1) * n + y : -1;
    out[2] = y > 0     ? x * n + y - 1   : -1;
    out[3] = y < n - 1 ? x * n + y + 1   : -1;
    for (int i = 0; i < 4; i++) if (out[i] >= 0) c++;
    return c;
}

#endif
