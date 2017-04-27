/*  - FOX - A tool for testing Open-Channel SSDs
 *      - Read / Write helper
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

#include <liblightnvm.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/time.h>
#include <stdio.h>
#include "fox.h"

double fox_check_progress_pgs (struct fox_node *node)
{
    uint32_t t_pgs = node->npgs * node->nblks * node->nluns * node->nchs;

    return (100 / (double) t_pgs) * (double) node->stats.pgs_done;
}

double fox_check_progress_runtime (struct fox_node *node)
{
    fox_timestamp_end(FOX_STATS_RUNTIME, &node->stats);

    return (100 / (double) (node->wl->runtime)) * (node->stats.runtime / SEC64);
}

int fox_update_runtime (struct fox_node *node)
{
    double rt;

    if (node->wl->runtime) {
        rt = fox_check_progress_runtime(node);
        fox_set_progress(&node->stats,
                                  (uint16_t) fox_check_progress_runtime(node));
        if (rt >= 100)
            return 1;
    } else
        fox_set_progress(&node->stats, (uint16_t) fox_check_progress_pgs(node));

    return 0;
}

int fox_write_blk (struct fox_tgt_blk *tgt, struct fox_node *node,
                        struct fox_blkbuf *buf, uint16_t npgs, uint16_t blkoff)
{
    int i, cmd_pgs;
    uint8_t failed = 0;
    struct fox_output_row *row;
    uint64_t tstart, tend;
    size_t tot_bytes;
    size_t vpg_sz = node->wl->geo->page_nbytes * node->wl->geo->nplanes;

    cmd_pgs = node->wl->nppas /(node->wl->geo->nsectors * node->wl->geo->nplanes);

    if (blkoff + npgs > node->npgs)
        printf ("Wrong write offset. pg (%d) > pgs_per_blk (%d).\n",
                                             blkoff + npgs, (int) node->npgs);

    for (i = blkoff; i < blkoff + npgs; i = i + cmd_pgs) {

        tstart = fox_timestamp_tmp_start(&node->stats);

        cmd_pgs = (i + cmd_pgs > blkoff + npgs) ? blkoff + npgs - i : cmd_pgs;
        tot_bytes = vpg_sz * cmd_pgs;

        if (prov_vblock_pwrite(tgt->vblk,
                            buf->buf_w + vpg_sz * i,
                            tot_bytes,
                            vpg_sz * i) != tot_bytes){
            fox_set_stats (FOX_STATS_FAIL_W, &node->stats, 1);
            tend = fox_timestamp_end(FOX_STATS_RUNTIME, &node->stats);
            failed++;
            goto FAILED;
        }

        tend = fox_timestamp_end(FOX_STATS_WRITE_T, &node->stats);
        fox_timestamp_end(FOX_STATS_RW_SECT, &node->stats);
        fox_set_stats(FOX_STATS_BWRITTEN, &node->stats, tot_bytes);
        fox_set_stats(FOX_STATS_BRW_SEC, &node->stats, tot_bytes);
        fox_set_stats(FOX_STATS_IOPS, &node->stats, 1);

FAILED:
        fox_set_stats (FOX_STATS_PGS_W, &node->stats, cmd_pgs);
        node->stats.pgs_done += cmd_pgs;

        if (node->wl->output) {
            row = fox_output_new ();
            row->ch = tgt->ch;
            row->lun = tgt->lun;
            row->blk = tgt->blk;
            row->pg = i;
            row->tstart = tstart;
            row->tend = tend;
            row->ulat = tend - tstart;
            row->type = 'w';
            row->failed = failed;
            row->datacmp = 2;
            row->size = vpg_sz * cmd_pgs;
            fox_output_append(row, node->nid);
        }

        if (fox_update_runtime(node)||(node->wl->stats->flags & FOX_FLAG_DONE))
            return 1;
        else if (node->delay)
            usleep(node->delay);
    }

    return 0;
}

int fox_read_blk (struct fox_tgt_blk *tgt, struct fox_node *node,
                        struct fox_blkbuf *buf, uint16_t npgs, uint16_t blkoff)
{
    int i, cmd_pgs;
    uint8_t failed = 0, cmp = 0;
    struct fox_output_row *row;
    uint64_t tstart, tend;
    size_t tot_bytes;
    size_t vpg_sz = node->wl->geo->page_nbytes * node->wl->geo->nplanes;

    cmd_pgs = node->wl->nppas /(node->wl->geo->nsectors * node->wl->geo->nplanes);

    if (blkoff + npgs > node->npgs)
        printf ("Wrong read offset. pg (%d) > pgs_per_blk (%d).\n",
                                             blkoff + npgs, (int) node->npgs);

    for (i = blkoff; i < blkoff + npgs; i = i + cmd_pgs) {

        tstart = fox_timestamp_tmp_start(&node->stats);

        cmd_pgs = (i + cmd_pgs > blkoff + npgs) ? blkoff + npgs - i : cmd_pgs;
        tot_bytes = vpg_sz * cmd_pgs;

        if (prov_vblock_pread(tgt->vblk,
                            buf->buf_r + vpg_sz * i,
                            tot_bytes,
                            vpg_sz * i) != tot_bytes){
            fox_set_stats (FOX_STATS_FAIL_R, &node->stats, 1);
            tend = fox_timestamp_end(FOX_STATS_RUNTIME, &node->stats);
            failed++;
            goto FAILED;
        }

        tend = fox_timestamp_end(FOX_STATS_READ_T, &node->stats);
        fox_timestamp_end(FOX_STATS_RW_SECT, &node->stats);

        cmp = (node->wl->memcmp) ? fox_blkbuf_cmp(node, buf, i, cmd_pgs) : 2;

        fox_set_stats (FOX_STATS_BREAD, &node->stats, tot_bytes);
        fox_set_stats (FOX_STATS_BRW_SEC,&node->stats, tot_bytes);
        fox_set_stats(FOX_STATS_IOPS, &node->stats, 1);

FAILED:
        fox_set_stats (FOX_STATS_PGS_R, &node->stats, cmd_pgs);

        if (node->wl->output) {
            row = fox_output_new ();
            row->ch = tgt->ch;
            row->lun = tgt->lun;
            row->blk = tgt->blk;
            row->pg = i;
            row->tstart = tstart;
            row->tend = tend;
            row->ulat = tend - tstart;
            row->type = 'r';
            row->failed = failed;
            row->datacmp = cmp;
            row->size = vpg_sz * cmd_pgs;
            fox_output_append(row, node->nid);
        }

        if (node->wl->w_factor == 0  || node->wl->engine->id == FOX_ENGINE_3) {
            node->stats.pgs_done += cmd_pgs;
            if (fox_update_runtime(node))
                return 1;
        }

        if (node->wl->stats->flags & FOX_FLAG_DONE)
            return 1;
        else if (node->delay)
            usleep(node->delay);
    }

    return 0;
}

int fox_erase_blk (struct fox_tgt_blk *tgt, struct fox_node *node)
{
    fox_timestamp_tmp_start(&node->stats);

    if (prov_vblock_erase (tgt->vblk)<0)
        fox_set_stats (FOX_STATS_FAIL_E, &node->stats, 1);

    fox_timestamp_end(FOX_STATS_ERASE_T, &node->stats);
    fox_set_stats (FOX_STATS_ERASED_BLK, &node->stats, 1);

    if (fox_update_runtime(node) || node->wl->stats->flags & FOX_FLAG_DONE)
        return 1;

    return 0;
}

int fox_erase_all_vblks (struct fox_node *node)
{
    uint32_t t_blks, t_luns;
    uint16_t blk_i, lun_i, ch_i, blk_ch, blk_lun;

    t_luns = node->nluns * node->nchs;
    t_blks = node->nblks * t_luns;
    blk_lun = t_blks / t_luns;
    blk_ch = blk_lun * node->nluns;

    for (blk_i = 0; blk_i < t_blks; blk_i++) {
        ch_i = blk_i / blk_ch;
        lun_i = (blk_i % blk_ch) / blk_lun;

        fox_vblk_tgt(node, node->ch[ch_i],node->lun[lun_i],blk_i % blk_lun);

        if (fox_erase_blk (&node->vblk_tgt, node))
            return 1;
    }

    return 0;
}

struct fox_rw_iterator *fox_iterator_new (struct fox_node *node)
{
    struct fox_rw_iterator *it;

    it = malloc (sizeof (struct fox_rw_iterator));
    if (!it)
        return NULL;
    memset (it, 0, sizeof (struct fox_rw_iterator));

    it->cols = node->nchs * node->nluns;
    it->rows = node->nblks * node->npgs;

    return it;
}

void fox_iterator_free (struct fox_rw_iterator *it)
{
    free (it);
}

int fox_iterator_next (struct fox_rw_iterator *it, uint8_t type)
{
    uint32_t *row, *col;

    row = (type == FOX_READ) ? &it->row_r : &it->row_w;
    col = (type == FOX_READ) ? &it->col_r : &it->col_w;

    *col = (*col >= it->cols - 1) ? 0 : *col + 1;

    if (*col == 0)
        *row = (*row >= it->rows - 1) ? 0 : *row + 1;

   return (*col == 0 && *row == 0);
}

int fox_iterator_prior (struct fox_rw_iterator *it, uint8_t type)
{
    uint32_t *row, *col;

    row = (type == FOX_READ) ? &it->row_r : &it->row_w;
    col = (type == FOX_READ) ? &it->col_r : &it->col_w;

    *col = (*col == 0) ? it->cols - 1 : *col - 1;

    if (*col == it->cols - 1)
        *row = (*row == 0) ? it->rows - 1 : *row - 1;

   return ((*col == it->cols - 1) && (*row == it->rows - 1));
}

void fox_iterator_reset (struct fox_rw_iterator *it)
{
    it->row_w = 0;
    it->row_r = 0;
    it->col_r = 0;
    it->col_w = 0;
}
