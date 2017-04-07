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
#include <stdint.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <liblightnvm.h>
#include <string.h>
#include "fox.h"

static uint8_t  *th_ch;
static uint8_t  *nodes_ch; /* set in config lun, used to pick a
                            *                     node id within the channel */

void fox_wait_for_ready (struct fox_workload *wl)
{
    pthread_mutex_lock(&wl->start_mut);

    if (wl->stats->flags ^ FOX_FLAG_READY)
        pthread_cond_wait(&wl->start_con, &wl->start_mut);

    pthread_mutex_unlock(&wl->start_mut);
}

static int fox_config_ch (struct fox_node *node)
{
    int ch_th, mod_ch, i, add;

    if (node->wl->channels > node->wl->geo.nchannels ||
                                                     node->wl->channels == 0) {
        printf("thread: Invalid number of channels.\n");
        return -1;
    }

    add = 0;
    if (node->wl->channels >= node->wl->nthreads) {

        ch_th = node->wl->channels / node->wl->nthreads;
        mod_ch = node->wl->channels % node->wl->nthreads;

        if (mod_ch && (node->nid >= (node->wl->nthreads - mod_ch)))
            add++;

    } else {
        ch_th = 1;
    }

    node->ch = malloc(sizeof(uint8_t) * ch_th + add);
    if (!node->ch)
        return -1;

    for (i = 0; i < ch_th; i++) {
        if (node->nid > node->wl->channels - 1)
            node->ch[i] = node->nid % node->wl->channels;
        else
            node->ch[i] = node->nid * ch_th + i;
        th_ch[node->ch[i]]++;
    }

    if (add) {
        node->ch[ch_th] = node->wl->channels - (node->wl->nthreads - node->nid);
        th_ch[node->ch[ch_th]]++;
    }

    node->nchs = ch_th + add;

    return 0;
}

static int fox_config_lun (struct fox_node *node)
{
    int lun_th, mod_lun, i, add, n_th, nid;

    if (node->wl->luns > node->wl->geo.nluns || node->wl->luns == 0) {
        printf("thread: Invalid number of LUNs.\n");
        return -1;
    }

    n_th = th_ch[node->ch[0]];
    nid = nodes_ch[node->ch[0]];
    add = 0;
    if (node->wl->luns >= th_ch[node->ch[0]]) {

        lun_th = node->wl->luns / n_th;
        mod_lun = node->wl->luns % n_th;

        if (mod_lun && (nid >= (n_th - mod_lun)))
            add++;

    } else {
        lun_th = 1;
    }

    node->lun = malloc(sizeof(uint8_t) * lun_th + add);
    if (!node->lun)
        return -1;

    for (i = 0; i < lun_th; i++) {
        if (nid > node->wl->luns - 1)
            node->lun[i] = nid % node->wl->luns;
        else
            node->lun[i] = nid * lun_th + i;
    }

    if (add)
        node->lun[lun_th] = node->wl->luns - (n_th - nid);

    node->nluns = lun_th + add;

    for (i = 0; i < node->nchs; i++) {
        nodes_ch[node->ch[i]]++;
    }

    return 0;
}

static void fox_show_geo_dist (struct fox_node *node)
{
    int node_i, ch_i, lun_i;

    printf("\n --- GEOMETRY DISTRIBUTION [TID: (CH LUN)] --- \n");

    for (node_i = 0; node_i < node[0].wl->nthreads; node_i++) {
        if (node_i % 4 == 0)
            printf ("\n");

        printf(" [%d:", node[node_i].nid);
        for (ch_i = 0; ch_i < node[node_i].nchs; ch_i++) {
            for (lun_i = 0; lun_i < node[node_i].nluns; lun_i++)
                printf(" (%d %d)", node[node_i].ch[ch_i],
                                                      node[node_i].lun[lun_i]);
        }
        printf("]  ");
    }
    printf ("\n");
}

static void *fox_thread_node (void * arg)
{
    int ret;
    struct fox_node *node = (struct fox_node *) arg;

    ret = node->engine->start(node);
    if (!ret)
        node->engine->exit(node);
    else
        printf ("thread: Thread %d has failed.\n", node->nid);

    return NULL;
}

struct fox_node *fox_create_threads (struct fox_workload *wl)
{
    int i;
    struct fox_node *node;
    if (!wl)
        goto ERR;

    th_ch = calloc (sizeof(uint8_t) * wl->channels, 0);
    nodes_ch = calloc (sizeof(uint8_t) * wl->channels, 0);
    if (!th_ch || !nodes_ch)
        goto ERR;

    node = malloc (sizeof(struct fox_node) * wl->nthreads);
    if (!node) {
        printf ("thread: Memory allocation failed.\n");
        goto ERR;
    }

    for (i = 0; i < wl->nthreads; i++) {
        node[i].wl = wl;
        node[i].nid = i;
        node[i].nblks = wl->blks;
        node[i].npgs = wl->pgs;
        node[i].delay = 0;

        if (fox_init_stats (&node[i].stats))
            goto ERR;

        if (fox_config_ch(&node[i])) {
            printf("thread: Failed to start. id: %d\n", i);
            goto ERR;
        }
    }

    for (i = 0; i < wl->nthreads; i++) {
        if (fox_config_lun(&node[i])) {
            printf("thread: Failed to start. id: %d\n", i);
            goto ERR;
        }
    }

    fox_show_geo_dist (node);

    for (i = 0; i < wl->nthreads; i++) {
        node[i].engine = wl->engine;

        if(pthread_create (&node[i].tid, NULL, fox_thread_node, &node[i]))
            printf("thread: Failed to start. id: %d\n", i);
    }

    return node;
ERR:
    return NULL;
}

void fox_exit_threads (struct fox_node *nodes)
{
    int i;

    for (i = 0; i < nodes[0].wl->nthreads; i++) {
        free (nodes[i].ch);
        free (nodes[i].lun);
        fox_exit_stats (&nodes[i].stats);
    }
    free (nodes);
}