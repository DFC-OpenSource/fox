#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/time.h>
#include <pthread.h>
#include <string.h>
#include "fox.h"

int fox_init_stats (struct fox_stats *st)
{
    memset (st, 0, sizeof (struct fox_stats));

    if (pthread_mutex_init (&st->s_mutex, NULL) != 0)
        return -1;

    return 0;
}

void fox_exit_stats (struct fox_stats *st)
{
    pthread_mutex_destroy (&st->s_mutex);
}

static uint16_t fox_get_progress (struct fox_stats *st)
{
    uint8_t prog;

    pthread_mutex_lock(&st->s_mutex);
    prog = st->progress;
    pthread_mutex_unlock(&st->s_mutex);

    return prog;
}

static uint64_t fox_get_tot_runtime (struct fox_node *nodes)
{
    int i;
    uint64_t tot = 0;

    for (i = 0; i < nodes[0].wl->nthreads; i++) {
        tot += nodes[i].stats.runtime;
    }

    return tot / (uint64_t) nodes[0].wl->nthreads;
}

void fox_set_progress (struct fox_stats *st, uint16_t val)
{
    pthread_mutex_lock(&st->s_mutex);
    st->progress = val;
    pthread_mutex_unlock(&st->s_mutex);
}

void fox_set_stats (uint8_t type, struct fox_stats *st, int64_t val)
{
    switch (type) {
        case FOX_STATS_RUNTIME:
            st->runtime = (uint64_t) val;
            break;
        case FOX_STATS_ERASE_T:
            st->erase_t += (uint64_t) val;
            break;
        case FOX_STATS_READ_T:
            st->read_t += (uint64_t) val;
            break;
        case FOX_STATS_WRITE_T:
            st->write_t += (uint64_t) val;
            break;
        case FOX_STATS_ERASED_BLK:
            st->erased_blks += (uint32_t) val;
            break;
        case FOX_STATS_PGS_R:
            st->pgs_r += (uint32_t) val;
            break;
        case FOX_STATS_PGS_W:
            st->pgs_w += (uint32_t) val;
            break;
        case FOX_STATS_BREAD:
            st->bread += (uint64_t) val;
            break;
        case FOX_STATS_BWRITTEN:
            st->bwritten += (uint64_t) val;
            break;
        case FOX_STATS_FAIL_CMP:
            st->fail_cmp += (uint32_t) val;
            break;
        case FOX_STATS_FAIL_E:
            st->fail_e += (uint32_t) val;
            break;
        case FOX_STATS_FAIL_R:
            st->fail_r += (uint32_t) val;
            break;
        case FOX_STATS_FAIL_W:
            st->fail_w += (uint32_t) val;
            break;
    }
}

void fox_timestamp_start (struct fox_stats *st)
{
    gettimeofday(&st->tval, NULL);
}

void fox_timestamp_tmp_start (struct fox_stats *st)
{
    gettimeofday(&st->tval_tmp, NULL);
}

void fox_timestamp_end (uint8_t type, struct fox_stats *st)
{
    struct timeval te;
    struct timeval *ts;
    int64_t usec_s, usec_e, tot;

    gettimeofday(&te, NULL);

    ts = (type == FOX_STATS_RUNTIME) ? &st->tval : &st->tval_tmp;

    usec_e = te.tv_sec * SEC64;
    usec_e += te.tv_usec;
    usec_s = ts->tv_sec * SEC64;
    usec_s += ts->tv_usec;

    tot = usec_e - usec_s;

    fox_set_stats (type, st, tot);
}

void fox_start_node (struct fox_node *node)
{
    node->stats.flags |= FOX_FLAG_READY;
    fox_wait_for_ready (node->wl);
    fox_timestamp_start(&node->stats);
}

void fox_end_node (struct fox_node *node)
{
    fox_timestamp_end(FOX_STATS_RUNTIME, &node->stats);
    node->stats.flags |= FOX_FLAG_DONE;
    node->stats.progress = 100;
}

void fox_merge_stats (struct fox_node *nodes, struct fox_stats *st)
{
    int i;

    for (i = 0; i < nodes[0].wl->nthreads; i++) {
        st->bread += nodes[i].stats.bread;
        st->bwritten += nodes[i].stats.bwritten;
        st->erase_t += nodes[i].stats.erase_t;
        st->read_t += nodes[i].stats.read_t;
        st->pgs_r += nodes[i].stats.pgs_r;
        st->pgs_w += nodes[i].stats.pgs_w;
        st->write_t += nodes[i].stats.write_t;
        st->erased_blks += nodes[i].stats.erased_blks;
        st->fail_e += nodes[i].stats.fail_e;
        st->fail_w += nodes[i].stats.fail_w;
        st->fail_r += nodes[i].stats.fail_r;
        st->fail_cmp += nodes[i].stats.fail_cmp;
    }

    fox_timestamp_end (FOX_STATS_RUNTIME, st);

    st->runtime = fox_get_tot_runtime(nodes);
}

static void fox_show_progress (struct fox_node *node)
{
    int node_i;
    uint16_t n_prog, wl_prog = 0;
    long double tot_sec = 0, totalb = 0;

    fox_timestamp_end (FOX_STATS_RUNTIME, node[0].wl->stats);

    printf ("\r");
    for (node_i = 0; node_i < node[0].wl->nthreads; node_i++) {

        n_prog = fox_get_progress(&node[node_i].stats);
        wl_prog += n_prog;
        tot_sec += node[node_i].stats.read_t + node[node_i].stats.write_t;
        totalb += node[node_i].stats.bread + node[node_i].stats.bwritten;
        printf(" [%d:%d%%]", node[node_i].nid, n_prog);

    }
    wl_prog = (uint16_t) ((double) wl_prog / (double) node[0].wl->nthreads);

    totalb = totalb / (long double) (1024 * 1024);
    tot_sec = (long double) node[0].wl->stats->runtime / (long double) SEC64;

    printf(" [%d%%|%.2Lf MB/s]", wl_prog, totalb / tot_sec);
    fflush(stdout);
}

static uint8_t fox_check_runtime (struct fox_workload *wl)
{
    if (wl->runtime) {
        fox_timestamp_end (FOX_STATS_RUNTIME, wl->stats);

        if (wl->stats->runtime / SEC64 > wl->runtime)
            return 1;
    }

    return 0;
}

void fox_monitor (struct fox_node *nodes)
{
    int i, ndone, show;
    int nn;
    struct fox_workload *wl = nodes[0].wl;

    nn = wl->nthreads;

    printf ("\n - Synchronizing threads...\n");

    /* wait to all threads be ready to start */
    do {
        usleep(1);

        ndone = 0;
        for (i = 0; i < nn; i++) {
            if (nodes[i].stats.flags & FOX_FLAG_READY)
                ndone++;
        }
    } while (ndone < nn);

    /* start all threads */
    wl->stats->flags |= FOX_FLAG_READY;
    fox_timestamp_start (wl->stats);

    printf ("\n - Workload started.\n\n");

    /* show progress and wait until all threads are done */
    show = 0;
    fox_show_progress (nodes);
    do {
        usleep(50000);

        show++;
        if (show % 20 == 0) {
            fox_show_progress (nodes);
            show = 0;
        }

        if (fox_check_runtime (wl))
            wl->stats->flags |= FOX_FLAG_DONE;

        ndone = 0;
        for (i = 0; i < nn; i++) {
            if (nodes[i].stats.flags & FOX_FLAG_DONE)
                ndone++;
        }
    } while (ndone < nn);

    fox_show_progress (nodes);
}

void fox_show_stats (struct fox_workload *wl)
{
    long double th, totb;
    long double sec;
    uint64_t elat, rlat, wlat;

    struct fox_stats *st = wl->stats;

    sec = wl->stats->runtime / (long double) SEC64;
    totb = st->bread + st->bwritten;

    th = (sec) ? totb / sec : 0;

    elat = (st->erased_blks) ? st->erase_t / st->erased_blks : 0;
    rlat = (st->pgs_r) ? st->read_t / (st->pgs_r & AND64) : 0;
    wlat = (st->pgs_w) ? st->write_t / (st->pgs_w & AND64) : 0;

    printf ("\n\n --- RESULTS ---\n\n");
    printf (" - Total time    : %lu m-sec\n", st->runtime / (1000 & AND64));
    printf (" - Read data     : %lu KB\n", st->bread / (1024 & AND64));
    printf (" - Read pages    : %d\n", st->pgs_r);
    printf (" - Written data  : %lu KB\n", st->bwritten / (1024 & AND64));
    printf (" - Written pages : %d\n", st->pgs_w);
    printf (" - Throughput    : %.2Lf MB/sec\n", th / ((1024 * 1024) & AND64));
    printf (" - IOPS          : %.1Lf\n", (st->pgs_r + st->pgs_w) / sec);
    printf (" - Erased blocks : %d\n", st->erased_blks);
    printf (" - Erase latency : %lu u-sec\n", elat);
    printf (" - Read latency  : %lu u-sec\n", rlat);
    printf (" - Write latency : %lu u-sec\n", wlat);
    printf (" - Failed memcmp : %d\n", st->fail_cmp);
    printf (" - Failed writes : %d\n", st->fail_w);
    printf (" - Failed reads  : %d\n", st->fail_r);
    printf (" - Failed erases : %d\n\n", st->fail_e);
}

void fox_show_workload (struct fox_workload *wl)
{
    printf ("\n --- WORKLOAD ---\n\n");
    printf (" - Threads      : %d\n",wl->nthreads);
    printf (" - N of Channels: %d\n", wl->channels);
    printf (" - LUNs per Chan: %d\n", wl->luns);
    printf (" - Blks per LUN : %d\n", wl->blks);
    printf (" - Pgs per Blk  : %d\n", wl->pgs);
    printf (" - Write factor : %d %%\n", wl->w_factor);
    printf (" - read factor  : %d %%\n", wl->r_factor);
}
