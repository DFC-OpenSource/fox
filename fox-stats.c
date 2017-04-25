/*  - FOX - A tool for testing Open-Channel SSDs
 *      - Statistics
 *
 * Copyright (C) 2016, IT University of Copenhagen. All rights reserved.
 * Written by Ivan Luiz Picoli <ivpi@itu.dk>
 *
 * Funding support provided by CAPES Foundation, Ministry of Education
 * of Brazil, Brasilia - DF 70040-020, Brazil.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 *  - Redistributions of source code must retain the above copyright notice,
 *  this list of conditions and the following disclaimer.
 *  - Redistributions in binary form must reproduce the above copyright notice,
 *  this list of conditions and the following disclaimer in the documentation
 *  and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

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
    pthread_mutex_lock(&st->s_mutex);
    switch (type) {
        case FOX_STATS_RUNTIME:
            st->runtime = (uint64_t) val;
            break;
        case FOX_STATS_RW_SECT:
            st->rw_sect += (uint64_t) val;
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
        case FOX_STATS_BRW_SEC:
            st->brw_sec += (uint64_t) val;
            break;
        case FOX_STATS_IOPS:
            st->iops += (uint32_t) val;
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
    pthread_mutex_unlock(&st->s_mutex);
}

void fox_timestamp_start (struct fox_stats *st)
{
    gettimeofday(&st->tval, NULL);
}

uint64_t fox_timestamp_tmp_start (struct fox_stats *st)
{
    uint64_t usec;

    gettimeofday(&st->tval_tmp, NULL);

    usec = st->tval_tmp.tv_sec * SEC64;
    usec += st->tval_tmp.tv_usec;

    return usec;
}

uint64_t fox_timestamp_end (uint8_t type, struct fox_stats *st)
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

    return usec_e;
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
    int node_i, i;
    uint16_t n_prog, wl_prog = 0;
    long double th_sec, tot_sec = 0, totalb = 0, th = 0, iops;
    uint64_t usec, io_count = 0;
    struct fox_output_row_rt **rt;

    usec = fox_timestamp_end (FOX_STATS_RUNTIME, node[0].wl->stats);

    if (node->wl->output) {
        rt = malloc (sizeof(void *) * node->wl->nthreads);
        if (!rt)
            return;

        for (i = 0; i < node->wl->nthreads + 1; i++)
            rt[i] = fox_output_new_rt();
    }

    printf ("\r");
    for (node_i = 0; node_i < node[0].wl->nthreads; node_i++) {

        n_prog = fox_get_progress(&node[node_i].stats);
        wl_prog += n_prog;

        pthread_mutex_lock(&node[node_i].stats.s_mutex);

        if (node->wl->output) {
            rt[node_i + 1]->thpt = (node[node_i].stats.brw_sec == 0 ||
                node[node_i].stats.rw_sect == 0) ? 0 :
                (node[node_i].stats.brw_sec / (long double) (1024 * 1024))
                / (node[node_i].stats.rw_sect / (long double) SEC64);

            rt[node_i + 1]->iops = (node[node_i].stats.iops == 0 ||
                node[node_i].stats.rw_sect == 0) ? 0 :
                ((long double) node[node_i].stats.iops) /
                (node[node_i].stats.rw_sect / (long double) SEC64);

            rt[node_i + 1]->timestp = usec;

            fox_output_append_rt (rt[node_i + 1], node[node_i].nid + 1);
        }

        totalb = node[node_i].stats.brw_sec;
        th_sec = node[node_i].stats.rw_sect;
        node[node_i].stats.rw_sect -= th_sec;
        node[node_i].stats.brw_sec -= totalb;
        th_sec /= (long double) SEC64;
        tot_sec += th_sec;

        th += (totalb == 0 || th_sec == 0) ? 0 : totalb /  th_sec;

        io_count += node[node_i].stats.iops;
        node[node_i].stats.iops = 0;

        pthread_mutex_unlock(&node[node_i].stats.s_mutex);

        printf(" [%d:%d%%]", node[node_i].nid, n_prog);

    }
    wl_prog = (uint16_t) ((double) wl_prog / (double) node[0].wl->nthreads);

    iops = (io_count == 0 || tot_sec == 0) ?
                                          0 : (long double) io_count / tot_sec;

    th = th / (long double) (1024 * 1024);

    if (node->wl->output) {
        rt[0]->thpt = th;
        rt[0]->iops = iops;
        rt[0]->timestp = usec;
        fox_output_append_rt (rt[0], 0);
    }

    printf(" [%d%%|%.2Lf MB/s|%.1Lf]", wl_prog, th, iops);
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

    printf ("\n - Synchronizing threads... (%s engine)\n", wl->engine->name);
    if (wl->engine->id == 3)
        printf ("\n");

    /* Monitor is ready */
    wl->stats->flags |= FOX_FLAG_MONITOR;
    pthread_mutex_lock (&wl->monitor_mut);
    pthread_cond_broadcast(&wl->monitor_con);
    pthread_mutex_unlock (&wl->monitor_mut);

    /* wait to all threads be ready to start */
    do {
        usleep(10000);

        ndone = 0;
        for (i = 0; i < nn; i++) {
            if (nodes[i].stats.flags & FOX_FLAG_READY)
                ndone++;
        }
    } while (ndone < nn);

    /* start all threads */
    wl->stats->flags |= FOX_FLAG_READY;
    pthread_mutex_lock (&wl->start_mut);
    pthread_cond_broadcast(&wl->start_con);
    pthread_mutex_unlock (&wl->start_mut);

    fox_timestamp_start (wl->stats);

    printf ("\n - Workload started.\n\n");

    /* show progress and wait until all threads are done */
    show = 0;
    fox_show_progress (nodes);
    do {
        usleep(50000);

        show++;
        if (show % 10 == 0) {
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

void fox_show_stats (struct fox_workload *wl, struct fox_node *node)
{
    long double th = 0, totb = 0, tsec, io_usec = 0;
    uint64_t elat, rlat, wlat;
    int i;
    char line[80];

    struct fox_stats *st = wl->stats;

    for (i = 0; i < wl->nthreads; i++) {
        io_usec += node[i].stats.runtime;
        totb += node[i].stats.bread + node[i].stats.bwritten;
    }

    tsec = st->runtime / (long double) SEC64;
    th = totb / tsec;

    elat = (st->erased_blks) ? st->erase_t / st->erased_blks : 0;
    rlat = (st->pgs_r) ? st->read_t / (st->pgs_r & AND64) : 0;
    wlat = (st->pgs_w) ? st->write_t / (st->pgs_w & AND64) : 0;

    sprintf (line, "\n\n --- RESULTS ---\n\n");
    fox_print (line);
    sprintf (line, " - Elapsed time  : %lu m-sec\n",st->runtime/(1000 & AND64));
    fox_print (line);
    sprintf (line, " - I/O time (sum): %.0Lf m-sec\n", io_usec/(1000 & AND64));
    fox_print (line);
    sprintf (line, " - Read data     : %lu KB\n", st->bread / (1024 & AND64));
    fox_print (line);
    sprintf (line, " - Read pages    : %d\n", st->pgs_r);
    fox_print (line);
    sprintf (line, " - Written data  : %lu KB\n",st->bwritten / (1024 & AND64));
    fox_print (line);
    sprintf (line, " - Written pages : %d\n", st->pgs_w);
    fox_print (line);
    sprintf(line, " - Throughput    : %.2Lf MB/sec\n",th/((1024*1024) & AND64));
    fox_print (line);
    sprintf (line, " - IOPS          : %.1Lf\n",(st->pgs_r + st->pgs_w) / tsec);
    fox_print (line);
    sprintf (line, " - Erased blocks : %d\n", st->erased_blks);
    fox_print (line);
    sprintf (line, " - Erase latency : %lu u-sec\n", elat);
    fox_print (line);
    sprintf (line, " - Read latency  : %lu u-sec\n", rlat);
    fox_print (line);
    sprintf (line, " - Write latency : %lu u-sec\n", wlat);
    fox_print (line);
    sprintf (line, " - Failed memcmp : %d\n", st->fail_cmp);
    fox_print (line);
    sprintf (line, " - Failed writes : %d\n", st->fail_w);
    fox_print (line);
    sprintf (line, " - Failed reads  : %d\n", st->fail_r);
    fox_print (line);
    sprintf (line, " - Failed erases : %d\n\n", st->fail_e);
    fox_print (line);

    printf (" - Generating files under ./output ...\n\n");
}

void fox_show_workload (struct fox_workload *wl)
{
    char cmp;
    char line[80];

    cmp = (wl->memcmp) ? 'y' : 'n';

    sprintf (line, "\n --- WORKLOAD ---\n\n");
    fox_print (line);
    sprintf (line, " - Device       : %s\n", wl->devname);
    fox_print (line);
    if (wl->runtime)
        sprintf (line, " - Runtime      : %lu sec\n", wl->runtime);
    else
        sprintf (line, " - Runtime      : 1 iteration\n");
    fox_print (line);
    sprintf (line, " - Threads      : %d\n",wl->nthreads);
    fox_print (line);
    sprintf (line, " - N of Channels: %d\n", wl->channels);
    fox_print (line);
    sprintf (line, " - LUNs per Chan: %d\n", wl->luns);
    fox_print (line);
    sprintf (line, " - Blks per LUN : %d\n", wl->blks);
    fox_print (line);
    sprintf (line, " - Pgs per Blk  : %d\n", wl->pgs);
    fox_print (line);
    sprintf (line, " - Write factor : %d %%\n", wl->w_factor);
    fox_print (line);
    sprintf (line, " - read factor  : %d %%\n", wl->r_factor);
    fox_print (line);
    sprintf (line, " - Max I/O delay: %d u-sec\n", wl->max_delay);
    fox_print (line);
    sprintf (line, " - Engine       : %d (%s)\n", wl->engine->id,
                                                            wl->engine->name);
    fox_print (line);
    sprintf (line, " - Read compare : %c\n", cmp);
    fox_print (line);
}