/*
 * base.c - Task 2: base station (single MPI process, single thread)
 *
 * - Polls for reports from the EV charging nodes.
 * - Remembers the last report time of every node. A report from node j means
 *   "j and its neighbours are full" for `period` seconds.
 * - For a report from node r:
 *     * if every neighbour of r reported within the period -> reply "none"
 *     * otherwise reply with ALL nearest (Manhattan distance) nodes that are
 *       not known to be full.
 * - Logs alerts, computed nodes, simulation time, message totals and per-type
 *   delay statistics to base_station_log.txt, and appends one row per message
 *   type to analysis.csv (Task 3).
 * - Shutdown: after sim_s seconds sends TERM to all nodes, keeps servicing
 *   reports until every node has sent DONE, then sends FINAL, drains its
 *   sends, closes files, frees memory and finalises MPI.
 *
 * Build: mpicc -O2 -Wall -o ev_base base.c
 */
#include "common.h"
#include <limits.h>

static void run_base(const cfg_t *c)
{
    int N = c->m * c->n, m = c->m, n = c->n;
    pool_t pool; pool_init(&pool, N + 16);
    FILE *lg = fopen("base_station_log.txt", "w");
    if (!lg) { fprintf(stderr, "cannot open base log\n"); MPI_Abort(MPI_COMM_WORLD, 3); }
    double *last = malloc(N * sizeof(double));
    int *full = malloc(N * sizeof(int));
    for (int i = 0; i < N; i++) last[i] = -1;
    dstat own[NTYPES]; memset(own, 0, sizeof own);
    dstat agg[NTYPES]; memset(agg, 0, sizeof agg);
    long alerts = 0, node_msgs = 0, node_bytes = 0;
    long n_inc = 0, n_red = 0, n_rep = 0;
    int term_sent = 0, done_cnt = 0, cand[MAXC];
    double buf[MAXBUF], start = MPI_Wtime();

    int *wg, wflag;
    MPI_Comm_get_attr(MPI_COMM_WORLD, MPI_WTIME_IS_GLOBAL, &wg, &wflag);
    fprintf(lg, "Base station started: grid %dx%d, k=%d, thr=%d, period=%.2fs, sim=%.1fs, "
                "MPI_WTIME_IS_GLOBAL=%d\n", m, n, c->k, c->thr, c->period, c->sim_s, wflag && *wg);

    while (done_cnt < N) {
        if (!term_sent && MPI_Wtime() - start >= c->sim_s) {
            double tm[1] = {0};
            for (int i = 0; i < N; i++) pool_send(&pool, i, TAG_TERM, tm, 1);
            term_sent = 1;
            fprintf(lg, "[%.3f] TERM sent to all nodes\n", MPI_Wtime() - start);
        }
        int flag; MPI_Status stt;
        MPI_Iprobe(MPI_ANY_SOURCE, MPI_ANY_TAG, MPI_COMM_WORLD, &flag, &stt);
        if (!flag) { usleep(POLL_US); continue; }
        MPI_Recv(buf, MAXBUF, MPI_DOUBLE, stt.MPI_SOURCE, stt.MPI_TAG, MPI_COMM_WORLD, &stt);
        double now = MPI_Wtime();

        if (stt.MPI_TAG == TAG_REPORT) {
            dstat_add(&own[T_REPORT], now - buf[0]);
            int r = (int)buf[1]; alerts++;
            last[r] = now;
            /* nodes known to be full: recent reporters and their neighbours */
            memset(full, 0, N * sizeof(int));
            for (int j = 0; j < N; j++)
                if (last[j] >= 0 && now - last[j] <= c->period) {
                    int nb[4]; grid_neighbors(j, m, n, nb);
                    full[j] = 1;
                    for (int d = 0; d < 4; d++) if (nb[d] >= 0) full[nb[d]] = 1;
                }
            for (int d = 0; d < 4; d++) if ((int)buf[15 + 2 * d] >= 0) full[(int)buf[15 + 2 * d]] = 1;

            int nb[4], nbs = grid_neighbors(r, m, n, nb), recent = 0;
            for (int d = 0; d < 4; d++)
                if (nb[d] >= 0 && last[nb[d]] >= 0 && now - last[nb[d]] <= c->period) recent++;

            int cnt = 0, best = INT_MAX;
            if (!(nbs > 0 && recent == nbs))      /* all neighbours reported -> none */
                for (int i = 0; i < N; i++) {
                    if (full[i]) continue;
                    int dist = abs(i / n - r / n) + abs(i % n - r % n);
                    if (dist < best) { best = dist; cnt = 0; }
                    if (dist == best && cnt < MAXC) cand[cnt++] = i;
                }

            double rp[MAXBUF] = {0};
            rp[1] = cnt;
            for (int i = 0; i < cnt; i++) rp[2 + i] = cand[i];
            pool_send(&pool, r, TAG_REPLY, rp, 2 + cnt);

            fprintf(lg, "[%.3f] ALERT #%ld from node %d at %04d-%02d-%02d %02d:%02d:%02d | free=%d/%d | "
                        "neighbours:", now - start, alerts, r, (int)buf[2], (int)buf[3], (int)buf[4],
                    (int)buf[5], (int)buf[6], (int)buf[7], (int)buf[8], (int)buf[9]);
            for (int d = 0; d < 4; d++)
                if ((int)buf[15 + 2 * d] >= 0)
                    fprintf(lg, " node%d(free=%d)", (int)buf[15 + 2 * d], (int)buf[16 + 2 * d]);
            fprintf(lg, " | msgs sent/recv with neighbours=%d/%d, request=%dB response=%dB | ",
                    (int)buf[11], (int)buf[12], (int)buf[13], (int)buf[14]);
            if (cnt == 0) fprintf(lg, "result: NO node available nearby\n");
            else {
                fprintf(lg, "result: nearest available (dist %d):", best);
                for (int i = 0; i < cnt; i++) fprintf(lg, " %d", cand[i]);
                fputc('\n', lg);
            }
        } else if (stt.MPI_TAG == TAG_DONE) {
            dstat_add(&own[T_DONE], now - buf[0]);
            for (int t = 0; t < NTYPES; t++) {
                agg[t].cnt += buf[2 + 3 * t]; agg[t].sum += buf[3 + 3 * t];
                if (buf[4 + 3 * t] > agg[t].max) agg[t].max = buf[4 + 3 * t];
            }
            int o = 2 + 3 * NTYPES;
            n_inc += (long)buf[o]; n_red += (long)buf[o + 1]; n_rep += (long)buf[o + 2];
            node_msgs += (long)buf[o + 3]; node_bytes += (long)buf[o + 4];
            done_cnt++;
        }
    }
    /* all nodes done: release them */
    double fn[1] = {0};
    for (int i = 0; i < N; i++) pool_send(&pool, i, TAG_FINAL, fn, 1);
    long base_msgs = pool.msgs, base_bytes = pool.bytes;
    pool_free(&pool);
    double simt = MPI_Wtime() - start;

    for (int t = 0; t < NTYPES; t++)
        if (own[t].cnt > 0) agg[t] = own[t];     /* REPORT and DONE are measured at the base */
    fprintf(lg, "\n===== SUMMARY =====\nSimulation time: %.3f s\nAlerts detected (reports received): %ld\n"
                "Incidents detected by nodes: %ld, local redirects/forwards: %ld, reports: %ld\n"
                "Total messages: %ld (nodes sent %ld, base sent %ld), total bytes: %ld\n\n"
                "%-20s %8s %14s %14s\n", simt, alerts, n_inc, n_red, n_rep,
            node_msgs + base_msgs, node_msgs, base_msgs, node_bytes + base_bytes,
            "message type", "count", "avg delay(us)", "max delay(us)");
    for (int t = 0; t < NTYPES; t++)
        fprintf(lg, "%-20s %8.0f %14.2f %14.2f\n", TNAME[t], agg[t].cnt,
                agg[t].cnt ? agg[t].sum / agg[t].cnt * 1e6 : 0.0, agg[t].max * 1e6);
    fclose(lg);

    FILE *csv = fopen("analysis.csv", "a");
    if (csv) {
        fseek(csv, 0, SEEK_END);
        if (ftell(csv) == 0)
            fprintf(csv, "topology,m,n,nodes,msg_type,count,avg_delay_us,max_delay_us,sim_time_s\n");
        for (int t = 0; t < NTYPES; t++)
            if (agg[t].cnt > 0)
                fprintf(csv, "%s,%d,%d,%d,%s,%.0f,%.3f,%.3f,%.3f\n", (m == 1 || n == 1) ? "line" : "grid",
                        m, n, N, TNAME[t], agg[t].cnt, agg[t].sum / agg[t].cnt * 1e6, agg[t].max * 1e6, simt);
        fclose(csv);
    }
    printf("Simulation finished: %.2f s, %ld alerts. See base_station_log.txt, logs/, analysis.csv\n", simt, alerts);
    free(last); free(full);
}

int main(int argc, char *argv[])
{
    int provided, rank, size;
    MPI_Init_thread(&argc, &argv, MPI_THREAD_FUNNELED, &provided);
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &size);

    cfg_t c;
    if (parse_cfg(argc, argv, rank, size, &c)) { MPI_Finalize(); return 1; }
    if (rank != c.m * c.n) {   /* base station must be the last rank */
        fprintf(stderr, "ev_base started on rank %d, expected %d; check the mpirun line\n", rank, c.m * c.n);
        MPI_Abort(MPI_COMM_WORLD, 4);
    }

    /* matches the node programs' MPI_Comm_split (base is excluded from the node communicator) */
    MPI_Comm nodecomm;
    MPI_Comm_split(MPI_COMM_WORLD, MPI_UNDEFINED, rank, &nodecomm);

    run_base(&c);

    MPI_Finalize();
    return 0;
}
