#include <sys/queue.h>
#include <pthread.h>
#include <string.h>
#include <stdint.h>
#include <time.h>
#include <stdlib.h>
#include <stdio.h>

#include "fox.h"

TAILQ_HEAD(out_list,fox_output_row) out_head = TAILQ_HEAD_INITIALIZER(out_head);
static pthread_mutex_t out_mutex;
static pthread_mutex_t file_mutex;
static uint64_t sequence;
static uint64_t *node_seq;
static uint64_t usec;

int fox_output_init (int node_count)
{
    struct timeval tv;

    node_seq = malloc (sizeof(uint64_t) * node_count);
    if (!node_seq)
        return -1;
    memset (node_seq, 0, sizeof(uint64_t) * node_count);

    TAILQ_INIT (&out_head);
    pthread_mutex_init (&out_mutex, NULL);
    pthread_mutex_init (&file_mutex, NULL);
    sequence = 0;

    gettimeofday(&tv, NULL);
    usec = tv.tv_sec * SEC64;
    usec += tv.tv_usec;

    return 0;
}

void fox_output_exit (void)
{
    pthread_mutex_destroy (&out_mutex);
    pthread_mutex_destroy (&file_mutex);
    free (node_seq);
}

struct fox_output_row *fox_output_new (void)
{
    struct fox_output_row *row;

    row = malloc (sizeof(struct fox_output_row));

    return row;
}

void fox_output_append (struct fox_output_row *row, int node_id)
{
    row->tid = node_id;
    row->node_seq = node_seq[node_id];
    node_seq[node_id]++;

    pthread_mutex_lock (&out_mutex);

    row->seq = sequence;
    sequence++;
    TAILQ_INSERT_TAIL (&out_head, row, entry);

    pthread_mutex_unlock (&out_mutex);
}

void fox_output_flush (void)
{
    FILE *fp;
    char filename[40];
    struct fox_output_row *row;

    pthread_mutex_lock (&file_mutex);

    sprintf (filename, "%lu_fox_io.csv", usec);
    fp = fopen(filename, "a");
    if (!fp)
        goto UNLOCK_FILE;

    while (!TAILQ_EMPTY (&out_head)) {
        pthread_mutex_lock (&out_mutex);

        row = TAILQ_FIRST (&out_head);
        if (!row)
            goto UNLOCK_QUEUE;

        TAILQ_REMOVE (&out_head, row, entry);

        pthread_mutex_unlock (&out_mutex);

        if(fprintf (fp,
                "%lu;"
                "%lu;"
                "%d;"
                "%d;"
                "%d;"
                "%d;"
                "%d;"
                "%lu;"
                "%lu;"
                "%d;"
                "%c;"
                "%d;"
                "%d;"
                "%d\n",
                row->seq,
                row->node_seq,
                row->tid,
                row->ch,
                row->lun,
                row->blk,
                row->pg,
                row->tstart,
                row->tend,
                row->ulat,
                row->type,
                row->failed,
                row->datacmp,
                row->size) < 0) {
            printf (" [fox-output: ERROR. Not possible to flush results.]\n");
            goto CLOSE_FILE;
        }
        free (row);
    }

UNLOCK_QUEUE:
    pthread_mutex_unlock (&out_mutex);
CLOSE_FILE:
    fclose(fp);
UNLOCK_FILE:
    pthread_mutex_unlock (&file_mutex);
}
