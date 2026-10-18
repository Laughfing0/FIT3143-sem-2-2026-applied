/*
 * node.c - Task 1: EV charging node (Open MPI + OpenMP)
 *
 * One MPI process = one EV charging node, placed on an m x n Cartesian grid
 * (MPI virtual topology). Node r has coordinate (r / n, r % n).
 *
 * Each node runs one OpenMP parallel region of k+1 threads:
 *   thread 0     : controller, the only thread that calls MPI (FUNNELED)
 *   threads 1..k : charging ports; each periodically flips its status
 *                  (1 = in use, 0 = free) in a shared array (arrival/departure)
 *
 * Congestion (free ports < thr): request free-port counts from the 4 grid
 * neighbours (unicast). If any neighbour has vacancies, redirect EVs there;
 * otherwise send ONE report to the base station and wait (non-blocking) for
 * its reply, still answering neighbour requests meanwhile.
 *
 * Shutdown: on TERM the node stops opening incidents, finishes pending ones,
 * sends DONE (with its delay statistics), keeps serving neighbours until FINAL,
 * then closes its log, frees memory and finalises MPI.
 *
 * Build: mpicc -fopenmp -O2 -Wall -o ev_node node.c
 */
#include "common.h"
#include <omp.h>
#include <stdarg.h>
#include <sys/stat.h>

static int aread(int *p) { int v;
#pragma omp atomic read
    v = *p;
    return v; }
static void awrite(int *p, int v)
{
#pragma omp atomic write
    *p = v;
}

typedef struct {
    const cfg_t *c;
    int rank, k, thr, nn, nb[4];
    int *status;                       /* shared: 1 in use, 0 free */
    pool_t pool;
    dstat st[NTYPES];
    FILE *lg;
    int incident, got, awaiting, terminating, done_sent, finished;
    int resp_free[4], inc_free;
    double inc_ts[6];
    long incidents, redirects, reports;
} node_t;

static int count_free(node_t *nd)
{
    int f = 0;
    for (int i = 0; i < nd->k; i++) if (!aread(&nd->status[i])) f++;
    return f;
}

static void nlog(node_t *nd, const char *fmt, ...)
{
    double ts[6]; now_ts(ts);
    fprintf(nd->lg, "%04d-%02d-%02d %02d:%02d:%02d  ",
            (int)ts[0], (int)ts[1], (int)ts[2], (int)ts[3], (int)ts[4], (int)ts[5]);
    va_list ap; va_start(ap, fmt); vfprintf(nd->lg, fmt, ap); va_end(ap);
    fputc('\n', nd->lg);
}

/* all neighbour replies are in: redirect to a neighbour or alert the base */
static void node_resolve(node_t *nd)
{
    int best = -1, bf = -1;
    for (int d = 0; d < 4; d++)
        if (nd->nb[d] >= 0 && nd->resp_free[d] >= nd->thr && nd->resp_free[d] > bf) {
            best = d; bf = nd->resp_free[d];
        }
    nd->incident = 0;
    if (best >= 0) {
        nd->redirects++;
        nlog(nd, "INCIDENT resolved locally: redirect EVs to neighbour node %d (%d free)",
             nd->nb[best], bf);
        return;
    }
    /* report layout: [0] send time, [1] node, [2..7] discovery timestamp
     * (Y M D h m s), [8] free ports, [9] k, [10] #neighbours,
     * [11] msgs sent to neighbours, [12] msgs received, [13] request size (B),
     * [14] response size (B), [15+2d] neighbour rank, [16+2d] its free ports */
    double rep[RPT_LEN] = {0};
    rep[1] = nd->rank;
    for (int i = 0; i < 6; i++) rep[2 + i] = nd->inc_ts[i];
    rep[8]  = nd->inc_free;
    rep[9]  = nd->k;
    rep[10] = nd->nn;
    rep[11] = nd->nn;
    rep[12] = nd->nn;
    rep[13] = REQ_LEN * sizeof(double);
    rep[14] = RESP_LEN * sizeof(double);
    for (int d = 0; d < 4; d++) {
        rep[15 + 2 * d] = nd->nb[d];
        rep[16 + 2 * d] = nd->nb[d] >= 0 ? nd->resp_free[d] : -1;
    }
    pool_send(&nd->pool, nd->c->m * nd->c->n, TAG_REPORT, rep, RPT_LEN);
    nd->reports++; nd->awaiting = 1;
    nlog(nd, "INCIDENT: node and neighbours congested, report sent to base station");
}

static void node_check(node_t *nd)
{
    int fr = count_free(nd);
    nlog(nd, "availability %d/%d", fr, nd->k);
    if (nd->terminating || nd->incident || nd->awaiting || fr >= nd->thr) return;

    nd->incidents++; nd->incident = 1; nd->got = 0; nd->inc_free = fr;
    now_ts(nd->inc_ts);
    nlog(nd, "CONGESTED (%d free < %d): requesting neighbour data", fr, nd->thr);
    for (int d = 0; d < 4; d++)
        if (nd->nb[d] >= 0) {
            double rq[REQ_LEN] = {0, (double)nd->rank};
            pool_send(&nd->pool, nd->nb[d], TAG_REQ, rq, REQ_LEN);
        }
    if (nd->nn == 0) node_resolve(nd);
}

static void node_handle(node_t *nd, int tag, int src, const double *buf)
{
    switch (tag) {
    case TAG_REQ: {
        double r[RESP_LEN] = {0, (double)count_free(nd), (double)nd->k};
        pool_send(&nd->pool, src, TAG_RESP, r, RESP_LEN);
        break; }
    case TAG_RESP:
        for (int d = 0; d < 4; d++)
            if (nd->nb[d] == src) nd->resp_free[d] = (int)buf[1];
        if (nd->incident && ++nd->got == nd->nn) node_resolve(nd);
        break;
    case TAG_REPLY: {
        int cnt = (int)buf[1];
        nd->awaiting = 0;
        if (cnt == 0) nlog(nd, "base reply: no available node nearby");
        else {
            char s[512] = ""; int len = 0;
            for (int i = 0; i < cnt && len < 480; i++) len += sprintf(s + len, "%d ", (int)buf[2 + i]);
            nlog(nd, "base reply: forwarding EVs to nearest available node(s): %s", s);
            nd->redirects++;
        }
        break; }
    case TAG_TERM: nd->terminating = 1; nlog(nd, "TERM received"); break;
    case TAG_FINAL: nd->finished = 1; break;
    }
}

static void run_node(const cfg_t *c, int wrank, MPI_Comm nodecomm)
{
    node_t nd; memset(&nd, 0, sizeof nd);
    nd.c = c; nd.rank = wrank; nd.k = c->k; nd.thr = c->thr;

    /* --- MPI virtual topology: 2-D Cartesian grid, non periodic --- */
    MPI_Comm cart; int dims[2] = {c->m, c->n}, per[2] = {0, 0}, coords[2], crank;
    MPI_Cart_create(nodecomm, 2, dims, per, 0, &cart);
    MPI_Comm_rank(cart, &crank);
    if (crank != wrank) {  /* reorder=0 => must match world rank */
        fprintf(stderr, "Cartesian rank differs from world rank\n");
        MPI_Abort(MPI_COMM_WORLD, 2);
    }
    MPI_Cart_coords(cart, crank, 2, coords);
    int up, down, left, right;
    MPI_Cart_shift(cart, 0, 1, &up, &down);
    MPI_Cart_shift(cart, 1, 1, &left, &right);
    int nbs[4] = {up, down, left, right};
    for (int d = 0; d < 4; d++) {
        nd.nb[d] = nbs[d] == MPI_PROC_NULL ? -1 : nbs[d];
        if (nd.nb[d] >= 0) nd.nn++;
    }

    char fname[64]; snprintf(fname, sizeof fname, "logs/node_%d.log", wrank);
    nd.lg = fopen(fname, "w");
    if (!nd.lg) { fprintf(stderr, "cannot open %s\n", fname); MPI_Abort(MPI_COMM_WORLD, 3); }
    nlog(&nd, "node %d at (%d,%d), %d neighbours, %d ports", wrank, coords[0], coords[1], nd.nn, nd.k);

    nd.status = calloc(c->k, sizeof(int));
    pool_init(&nd.pool, 32);
    unsigned seed0 = c->seed + 1000u * wrank;
    for (int i = 0; i < c->k; i++) {
        unsigned s = seed0 + i + 1;
        nd.status[i] = (rand_r(&s) / (double)RAND_MAX) < c->busy_prob;
    }
    int stop = 0;
    omp_set_dynamic(0);

#pragma omp parallel num_threads(c->k + 1) shared(nd, stop)
    {
        int tid = omp_get_thread_num();
        if (tid != 0) {
            /* ---- charging port thread: periodic arrival/departure ---- */
            unsigned s = seed0 + 7919u * tid + 13;
            while (!aread(&stop)) {
                int slept = 0;
                while (slept < c->cycle_ms && !aread(&stop)) { usleep(5000); slept += 5; }
                if (aread(&stop)) break;
                awrite(&nd.status[tid - 1], (rand_r(&s) / (double)RAND_MAX) < c->busy_prob);
            }
        } else {
            /* ---- controller thread: all MPI traffic of this node ---- */
            double buf[MAXBUF], next = MPI_Wtime() + c->cycle_ms / 1000.0;
            while (!nd.finished) {
                if (nd.terminating && !nd.incident && !nd.awaiting && !nd.done_sent) {
                    double dn[MAXBUF] = {0};
                    dn[1] = nd.rank;
                    for (int t = 0; t < NTYPES; t++) {
                        dn[2 + 3 * t] = nd.st[t].cnt; dn[3 + 3 * t] = nd.st[t].sum;
                        dn[4 + 3 * t] = nd.st[t].max;
                    }
                    int o = 2 + 3 * NTYPES;
                    dn[o] = nd.incidents; dn[o + 1] = nd.redirects; dn[o + 2] = nd.reports;
                    dn[o + 3] = nd.pool.msgs + 1; dn[o + 4] = nd.pool.bytes + DONE_LEN * sizeof(double);
                    pool_send(&nd.pool, c->m * c->n, TAG_DONE, dn, DONE_LEN);
                    nd.done_sent = 1;
                }
                int flag; MPI_Status stt;
                MPI_Iprobe(MPI_ANY_SOURCE, MPI_ANY_TAG, MPI_COMM_WORLD, &flag, &stt);
                if (flag) {
                    MPI_Recv(buf, MAXBUF, MPI_DOUBLE, stt.MPI_SOURCE, stt.MPI_TAG, MPI_COMM_WORLD, &stt);
                    double t = MPI_Wtime();
                    int tag = stt.MPI_TAG;
                    if (tag == TAG_REQ || tag == TAG_RESP || tag == TAG_REPLY || tag == TAG_TERM)
                        dstat_add(&nd.st[tag - 1], t - buf[0]);
                    node_handle(&nd, tag, stt.MPI_SOURCE, buf);
                } else if (MPI_Wtime() >= next) {
                    next += c->cycle_ms / 1000.0;
                    node_check(&nd);
                } else usleep(POLL_US);
            }
            awrite(&stop, 1);
        }
    }

    nlog(&nd, "shutdown: incidents=%ld redirects=%ld reports=%ld", nd.incidents, nd.redirects, nd.reports);
    pool_free(&nd.pool);
    fclose(nd.lg);
    free(nd.status);
    MPI_Comm_free(&cart);
}

int main(int argc, char *argv[])
{
    int provided, rank, size;
    MPI_Init_thread(&argc, &argv, MPI_THREAD_FUNNELED, &provided);
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &size);

    cfg_t c;
    if (provided < MPI_THREAD_FUNNELED || parse_cfg(argc, argv, rank, size, &c)) {
        MPI_Finalize();
        return 1;
    }
    int N = c.m * c.n;
    if (rank >= N) {   /* node program must occupy ranks 0..N-1 */
        fprintf(stderr, "ev_node started on rank %d (base station rank); check the mpirun line\n", rank);
        MPI_Abort(MPI_COMM_WORLD, 4);
    }
    mkdir("logs", 0755);                 /* harmless if it already exists */

    /* communicator containing only the EV nodes (the base calls Split with MPI_UNDEFINED) */
    MPI_Comm nodecomm;
    MPI_Comm_split(MPI_COMM_WORLD, 0, rank, &nodecomm);
    run_node(&c, rank, nodecomm);
    MPI_Comm_free(&nodecomm);

    MPI_Finalize();
    return 0;
}
