/*  - FOX - A tool for testing Open-Channel SSDs
 *      - Output data and statistics
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

#include <sys/queue.h>
#include <pthread.h>
#include <string.h>
#include <stdint.h>
#include <time.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#include "fox.h"

TAILQ_HEAD(out_list,fox_output_row) out_head = TAILQ_HEAD_INITIALIZER(out_head);
TAILQ_HEAD(rt_list,fox_output_row_rt) rt_head = TAILQ_HEAD_INITIALIZER(rt_head);
static pthread_mutex_t out_mutex;
static pthread_mutex_t file_mutex;
static uint64_t sequence;
static uint64_t *node_seq;
static uint64_t usec;

int fox_output_init (struct fox_workload *wl)
{
    struct timeval tv;
    FILE *fp;
    char filename[40];
    struct stat st = {0};

    if (stat("output", &st) == -1)
        mkdir("output", S_IRWXO);

    gettimeofday(&tv, NULL);
    usec = tv.tv_sec * SEC64;
    usec += tv.tv_usec;

    if (wl->output) {
        sprintf (filename, "output/%lu_fox_io.csv", usec);
        fp = fopen(filename, "a");
        if (!fp)
            return -1;

        fprintf (fp, "sequence;node_sequence;node_id;channel;lun;block;page;"
                       "start;end;latency;type;is_failed;read_memcmp;bytes\n");

        fclose(fp);

        sprintf (filename, "output/%lu_fox_rt.csv", usec);
        fp = fopen(filename, "a");
        if (!fp)
            return -1;

        fprintf (fp, "timestamp;node_id;throughput(mb/s);iops\n");

        fclose(fp);
    }

    node_seq = malloc (sizeof(uint64_t) * wl->nthreads);
    if (!node_seq)
        return -1;
    memset (node_seq, 0, sizeof(uint64_t) * wl->nthreads);

    TAILQ_INIT (&out_head);
    TAILQ_INIT (&rt_head);
    pthread_mutex_init (&out_mutex, NULL);
    pthread_mutex_init (&file_mutex, NULL);
    sequence = 0;

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

struct fox_output_row_rt *fox_output_new_rt (void)
{
    struct fox_output_row_rt *row;

    row = malloc (sizeof(struct fox_output_row_rt));

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

void fox_output_append_rt (struct fox_output_row_rt *row, uint16_t nid)
{
    row->nid = nid;

    TAILQ_INSERT_TAIL (&rt_head, row, entry);
}

void fox_print (char *line, uint8_t to_file)
{
    FILE *fp;
    char filename[42];

    if (to_file) {
        sprintf (filename, "output/%lu_fox_meta.csv", usec);
        fp = fopen(filename, "a");
        if (!fp)
            return;

        fputs (line, fp);
        fclose(fp);
    }
    fputs (line, stdout);
}

void fox_output_flush (void)
{
    FILE *fp;
    char filename[40];
    struct fox_output_row *row;
    char tstart[21], tend[21];

    pthread_mutex_lock (&file_mutex);

    sprintf (filename, "output/%lu_fox_io.csv", usec);
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

        sprintf (tstart, "%lu", row->tstart);
        sprintf (tend, "%lu", row->tend);
        memmove (tstart, tstart+4, 17);
        memmove (tend, tend+4, 17);

        if(fprintf (fp,
                "%lu;"
                "%lu;"
                "%d;"
                "%d;"
                "%d;"
                "%d;"
                "%d;"
                "%s;"
                "%s;"
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
                tstart,
                tend,
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

void fox_output_flush_rt (void)
{
    FILE *fp;
    char filename[40];
    struct fox_output_row_rt *row;
    char ts[21];

    sprintf (filename, "output/%lu_fox_rt.csv", usec);
    fp = fopen(filename, "a");
    if (!fp)
        return;

    while (!TAILQ_EMPTY (&rt_head)) {

        row = TAILQ_FIRST (&rt_head);
        if (!row)
            goto CLOSE_FILE;

        TAILQ_REMOVE (&rt_head, row, entry);

        sprintf (ts, "%lu", row->timestp);
        memmove (ts, ts+4, 17);

        if(fprintf (fp,
                "%s;"
                "%d;"
                "%.4Lf;"
                "%.2Lf\n",
                ts,
                row->nid,
                row->thpt,
                row->iops) < 0) {
            printf (" [fox-output: ERROR. Not possible to flush results.]\n");
            goto CLOSE_FILE;
        }
        free (row);
    }

CLOSE_FILE:
    fclose(fp);
}