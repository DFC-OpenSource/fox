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

static void fox_set_engine (struct fox_workload *wl)
{
    switch (wl->engine) {
        case FOX_ENGINE_1:
            wl->fengine_fn = fox_engine1;
            break;
        case FOX_ENGINE_2:
            wl->fengine_fn = fox_engine2;
            break;
        default:
            printf("thread: Engine not found.\n");
    }
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

    fox_set_engine (wl);
    if (!wl->fengine_fn)
        goto ERR;

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
        if(pthread_create (&node[i].tid, NULL, wl->fengine_fn, &node[i]))
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
