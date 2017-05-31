/*  - FOX - A tool for testing Open-Channel SSDs
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

#include <pthread.h>
#include <liblightnvm.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/queue.h>
#include "fox.h"

LIST_HEAD(node_list, fox_node) node_head = LIST_HEAD_INITIALIZER(node_head);
LIST_HEAD(eng_list, fox_engine) eng_head = LIST_HEAD_INITIALIZER(eng_head);

static struct fox_argp *argp;

static int fox_check_workload (struct fox_workload *wl)
{
    int pg_ppas = wl->geo->nsectors * wl->geo->nplanes;

    if (wl->channels > wl->geo->nchannels ||
            wl->luns > wl->geo->nluns ||
            wl->blks > wl->geo->nblocks ||
            wl->pgs > wl->geo->npages) {
        printf (" Invalid device geometry.\n");
        return -1;
    }

    wl->channels = (!wl->channels) ? 1 : wl->channels;
    wl->luns = (!wl->luns) ? 1 : wl->luns;
    wl->blks = (!wl->blks) ? 1 : wl->blks;
    wl->pgs = (!wl->pgs) ? 1 : wl->pgs;

    if (wl->nthreads > wl->channels * wl->luns) {
        printf (" Number of jobs cannot exceed total number of LUNs.\n");
        return -1;
    }

    wl->nthreads = (!wl->nthreads) ? 1 : wl->nthreads;

    if (wl->r_factor + wl->w_factor == 0)
        wl->r_factor = 100;

    if (wl->r_factor == 0 && wl->w_factor > 0)
        wl->r_factor = 100 - wl->w_factor;
    else if (wl->w_factor == 0 && wl->r_factor > 0)
        wl->w_factor = 100 - wl->r_factor;

    if (wl->r_factor + wl->w_factor != 100) {
        printf (" Read + Write percentage must be equal to 100.\n");
        return -1;
    }

    if (wl->nppas > 64 || wl->nppas % pg_ppas != 0) {
        printf (" Vector must be multiple of %d and <= 64.\n", pg_ppas);
        return -1;
    }

    wl->nppas = (!wl->nppas) ? pg_ppas : wl->nppas;

    return 0;
}

static void fox_setup_io_factor (struct fox_workload *wl)
{
    uint16_t mm;

    if (wl->w_factor == 0 || wl->r_factor == 0) {
	wl->w_factor = wl->w_factor / 100;
	wl->r_factor = wl->r_factor / 100;
	return;
    }

    mm = (wl->w_factor > wl->r_factor) ? wl->r_factor : wl->w_factor;

    while (mm > 1)
    {
        if ((wl->w_factor % mm == 0) && (wl->r_factor % mm == 0))
            break;
        mm--;
    }

    wl->r_factor = wl->r_factor / mm;
    wl->w_factor = wl->w_factor / mm;
}

/*
 * Define here how to schedule delays between IOs in each thread
 * For now, we divide max_delay / n_threads and set an increasing delay each
 * For example: max_delay: 800, n_threads: 4
 * Th 1: 800, Th 2: 600, Th 3: 400, Th 4: 200
 */
static void fox_setup_delay (struct fox_node *nodes)
{
    int th_i, it, mod;
    struct fox_workload *wl = nodes[0].wl;

    it = wl->max_delay / wl->nthreads;
    mod = wl->max_delay % wl->nthreads;

    for (th_i = 0; th_i < wl->nthreads; th_i++)
        nodes[th_i].delay += (th_i + 1) * it;

    for (th_i = 0; th_i < mod; th_i++)
        nodes[th_i].delay += th_i + 1;
}

int fox_engine_register (struct fox_engine *eng)
{
    if (!eng)
        return -1;

    LIST_INSERT_HEAD(&eng_head, eng, entry);
    return 0;
}

struct fox_engine *fox_get_engine(uint16_t id)
{
    struct fox_engine *eng;

    LIST_FOREACH(eng, &eng_head, entry){
        /* returns engine 1 if parameter is not provided */
        if (id == 0 && eng->id == 1)
            return eng;

        if(eng->id == id)
            return eng;
    }

    return NULL;
}

static int fox_init_engs (struct fox_workload *wl)
{
    if (foxeng_seq_init(wl) || foxeng_rr_init(wl) || foxeng_iso_init(wl))
        return -1;

    return 0;
}

static void fox_exit_engs (void)
{
    struct fox_engine *eng;
    LIST_FOREACH(eng, &eng_head, entry){
        eng->exit();
    }
}

int main (int argc, char **argv) {
    struct fox_workload *wl;
    struct fox_node *nodes;
    struct fox_stats *gl_stats;
    int ret = -1;
    int mode;

    argp = calloc (sizeof (struct fox_argp), 1);
    if (!argp)
        goto RETURN;

    mode = fox_argp_init (argc, argv, argp);
    if (mode < 0)
        goto ARGP;

    if (mode == FOX_IO_MODE) {
        ret = fox_mio_init (argp);
        goto ARGP;
    }

    gl_stats = malloc (sizeof (struct fox_stats));
    if (!gl_stats)
        goto ARGP;

    wl = calloc (sizeof (struct fox_workload), 1);
    if (!wl)
        goto GL_STATS;

    pthread_mutex_init (&wl->start_mut, NULL);
    pthread_cond_init (&wl->start_con, NULL);
    pthread_mutex_init (&wl->monitor_mut, NULL);
    pthread_cond_init (&wl->monitor_con, NULL);

    wl->runtime = argp->runtime;
    wl->devname = argp->devname;
    wl->channels = argp->channels;
    wl->luns = argp->luns;
    wl->blks = argp->blks;
    wl->pgs = argp->pgs;
    wl->nthreads = argp->nthreads;
    wl->r_factor = argp->r_factor;
    wl->w_factor = argp->w_factor;
    wl->nppas = argp->vector;
    wl->max_delay = argp->max_delay;
    wl->memcmp = argp->memcmp;
    wl->output = argp->output;

    if (wl->devname[0] == 0) {
        wl->devname = malloc (13);
        if (!wl->devname)
            goto MUTEX;

        memcpy (wl->devname, "/dev/nvme0n1", 13);
    }

    wl->dev = prov_dev_open(wl->devname);
    if (!wl->dev) {
        printf(" Device not found.\n");
        goto DEVNAME;
    }

    wl->geo = prov_get_geo(wl->dev);

    if (prov_init(wl->dev, wl->geo))
        goto DEV_CLOSE;
    LIST_INIT(&eng_head);

    if (fox_init_engs(wl))
        goto EXIT_PROV;

    wl->engine = fox_get_engine(argp->engine);
    if (!wl->engine) {
        printf(" Engine not found.\n");
        goto EXIT_ENG;
    }

    if (fox_check_workload(wl))
        goto EXIT_ENG;

    /* Engine 3 and 100% read workload requires geometry memory comparison */
    if (wl->engine->id == FOX_ENGINE_3 || wl->r_factor == 100)
        if (wl->memcmp && wl->memcmp != WB_GEOMETRY) {
            printf ("\n NOTE: This mode requires geometry write buffer (3).\n");
            wl->memcmp = WB_GEOMETRY;
        }

    if (fox_init_stats (gl_stats))
        goto EXIT_ENG;

    wl->stats = gl_stats;

    if (wl->output && fox_output_init (wl))
        goto EXIT_STATS;

    fox_show_workload (wl);
    fox_setup_io_factor (wl);

    nodes = fox_create_threads (wl);
    if (!nodes)
        goto EXIT_OUTPUT;

    fox_setup_delay (nodes);

    if (fox_alloc_vblks (wl))
        goto EXIT_THREADS;

    fox_monitor (nodes);

    fox_merge_stats (nodes, gl_stats);
    fox_show_stats (wl, nodes);

    if (wl->stats->fail_cmp)
        printf (" - CORRUPTION detected, read data under ./corruption\n\n");

    if (wl->output) {
        printf (" - Generating files under ./output ...\n\n");
        fox_output_flush ();
        fox_output_flush_rt ();
    }

    ret = 0;
    fox_free_vblks (wl);

EXIT_THREADS:
    fox_exit_threads (nodes);
EXIT_OUTPUT:
    if (wl->output)
        fox_output_exit ();
EXIT_STATS:
    fox_exit_stats (gl_stats);
    wl->stats = NULL;
EXIT_ENG:
    fox_exit_engs ();
EXIT_PROV:
    prov_exit ();
DEV_CLOSE:
    prov_dev_close(wl->dev);
DEVNAME:
    if (!(argp->arg_flag & CMDARG_FLAG_D))
        free (wl->devname);
MUTEX:
    pthread_mutex_destroy (&wl->start_mut);
    pthread_cond_destroy (&wl->start_con);
    pthread_mutex_destroy (&wl->monitor_mut);
    pthread_cond_destroy (&wl->monitor_con);

    free (wl);
GL_STATS:
    free (gl_stats);
ARGP:
    free (argp);
RETURN:
    return ret;
}