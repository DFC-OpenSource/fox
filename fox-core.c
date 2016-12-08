#include <pthread.h>
#include <liblightnvm.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "fox.h"

LIST_HEAD(node_list, fox_node) node_head = LIST_HEAD_INITIALIZER(node_head);

static int fox_check_workload (struct fox_workload *wl)
{
    if (wl->channels > wl->geo.nchannels ||
            wl->luns > wl->geo.nluns ||
            wl->blks > wl->geo.nblocks ||
            wl->pgs > wl->geo.npages ||
            wl->channels < 1 ||
            wl->luns < 1 ||
            wl->blks < 1 ||
            wl->pgs < 1 ||
            wl->nthreads < 1 ||
            wl->nthreads > wl->channels * wl->luns ||
            wl->r_factor + wl->w_factor != 100)
        return -1;

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
/*
    for (th_i = 0; th_i < wl->nthreads; th_i++) {
        printf ("delay %d: %d\n", th_i, nodes[th_i].delay);
    }
*/
}

int main (int argc, char **argv) {
    struct fox_workload *wl;
    struct fox_node *nodes;
    struct fox_stats *gl_stats;

    if (argc != 22) {
        printf (" => Example: fox nvme0n1 runtime 0 ch 8 lun 4 blk 10 pg 128 "
                              "node 8 read 50 write 50 delay 800 compare 1\n");
        return -1;
    }

    gl_stats = malloc (sizeof (struct fox_stats));
    wl = malloc (sizeof (struct fox_workload));

    if (!gl_stats || !wl)
        goto ERR;
    
    pthread_mutex_init (&wl->start_mut, NULL);
    pthread_cond_init (&wl->start_con, NULL);

    wl->engine = FOX_ENGINE_1;
    wl->runtime = atoi(argv[3]);
    wl->devname = malloc(8);
    wl->devname[7] = '\0';
    memcpy(wl->devname, argv[1], 7);
    wl->channels = atoi(argv[5]);
    wl->luns = atoi(argv[7]);
    wl->blks = atoi(argv[9]);
    wl->pgs = atoi(argv[11]);
    wl->nthreads = atoi(argv[13]);
    wl->r_factor = atoi(argv[15]);
    wl->w_factor = atoi(argv[17]);
    wl->max_delay = atoi(argv[19]);
    wl->memcmp = atoi(argv[21]);
    wl->dev = nvm_dev_open(wl->devname);
    wl->geo = nvm_dev_attr_geo(wl->dev);

    if (fox_check_workload(wl))
        goto GEO;

    fox_init_stats (gl_stats);
    wl->stats = gl_stats;

    fox_show_workload (wl);
    fox_setup_io_factor (wl);

    nodes = fox_create_threads(wl);
    if (!nodes)
        goto FREE;

    fox_setup_delay (nodes);

    if (fox_alloc_vblks(wl))
        goto FREE;

    fox_monitor (nodes);

    fox_merge_stats (nodes, gl_stats);
    fox_show_stats (wl, nodes);

    fox_free_vblks(wl);
    fox_exit_threads (nodes);
    fox_exit_stats (gl_stats);
    
    pthread_mutex_destroy (&wl->start_mut);
    pthread_cond_destroy (&wl->start_con);
    free (gl_stats);
    free (wl);

    return 0;

GEO:
    printf ("Workload not accepted.\n");
FREE:
    pthread_mutex_destroy (&wl->start_mut);
    pthread_cond_destroy (&wl->start_con);
    free (gl_stats);
    free (wl);
ERR:
    return -1;
}