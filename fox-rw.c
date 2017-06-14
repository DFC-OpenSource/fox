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

    //printf("check: %f\n",fox_check_progress_pgs(node));

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

int fox_write_vec_pg (struct fox_node *node, struct fox_blkbuf *buf, uint16_t blkoff, uint16_t pgoff)
{
    struct nvm_ret ret = {0,0};
    struct nvm_addr *ppa_vec;
    uint64_t tstart, tend;
    uint8_t failed = 0;
    struct fox_output_row *row;
    uint8_t *vecbuf;

    int ppa_i, cmd_i, vec_i, cmdppas;
    int ppas_pg = node->wl->geo->nsectors * node->wl->geo->nplanes;
    int nppas = ppas_pg * node->nchs * node->nluns;
    int cmdpgs;
    int ncmds = (nppas % node->wl->nppas == 0) ? nppas / node->wl->nppas : (nppas / node->wl->nppas) + 1;
    size_t vpg_sz = node->wl->geo->page_nbytes * node->wl->geo->nplanes;
    size_t tot_bytes = node->wl->geo->sector_nbytes * nppas;

    const int PMODE = nvm_dev_get_pmode(node->wl->dev);

    ppa_vec = calloc (1, sizeof (struct nvm_addr) * nppas);
    if (!ppa_vec)
        return -1;

    vecbuf = calloc (1, node->nchs * node->nluns * tot_bytes);
    if (!vecbuf)
        return -1;

    ppa_i = 0;
    for (cmd_i = 0; cmd_i < ncmds; cmd_i++) {

        cmdppas = (ppa_i + node->wl->nppas > nppas) ? nppas - ppa_i : node->wl->nppas;
        cmdpgs = (cmdppas % ppas_pg == 0) ? cmdppas / ppas_pg : (cmdppas / ppas_pg) + 1;

        for (vec_i = 0; vec_i < cmdppas; vec_i++) {
            ppa_vec[vec_i].g.ch = node->ch[((vec_i + cmd_i * node->wl->nppas) / ppas_pg) / node->nluns];
            ppa_vec[vec_i].g.lun = node->lun[((vec_i + cmd_i * node->wl->nppas) / ppas_pg) % node->nluns];
            ppa_vec[vec_i].g.pl = ((vec_i + cmd_i * node->wl->nppas) % ppas_pg) / node->wl->geo->nsectors;

            fox_vblk_tgt(node, ppa_vec[vec_i].g.ch, ppa_vec[vec_i].g.lun, blkoff);

            ppa_vec[vec_i].g.blk = node->vblk_tgt.vblk->blks[0].g.blk;
            ppa_vec[vec_i].g.pg = pgoff;
            ppa_vec[vec_i].g.sec = vec_i % node->wl->geo->nsectors;

            ppa_i++;

            memcpy(vecbuf + node->wl->geo->sector_nbytes * (vec_i + cmd_i * node->wl->nppas),

                    (buf[(vec_i + cmd_i * node->wl->nppas) / ppas_pg].buf_w + vpg_sz * pgoff) +
                    (ppa_vec[vec_i].g.sec +
                    (ppa_vec[vec_i].g.pl * node->wl->geo->nsectors)
                    * node->wl->geo->sector_nbytes),

                    node->wl->geo->sector_nbytes);
        }

        tstart = fox_timestamp_tmp_start(&node->stats);

        if (nvm_addr_write(node->wl->dev, ppa_vec, cmdppas,
                vecbuf, NULL, PMODE, &ret)) {
            fox_set_stats(FOX_STATS_FAIL_W, &node->stats, cmdpgs);
            tend = fox_timestamp_end(FOX_STATS_RUNTIME, &node->stats);
            failed++;
            goto FAILED;
        }

        tend = fox_timestamp_end(FOX_STATS_WRITE_T, &node->stats);

        fox_timestamp_end(FOX_STATS_RW_SECT, &node->stats);
        fox_set_stats(FOX_STATS_BWRITTEN, &node->stats, node->wl->geo->sector_nbytes * cmdppas);
        fox_set_stats(FOX_STATS_BRW_SEC, &node->stats, node->wl->geo->sector_nbytes * cmdppas);
        fox_set_stats(FOX_STATS_IOPS, &node->stats, 1);

FAILED:
        fox_set_stats(FOX_STATS_PGS_W, &node->stats, cmdpgs);
        node->stats.pgs_done += cmdpgs;

        if (node->wl->output) {
            row = fox_output_new();
            row->ch = node->nchs;
            row->lun = node->nluns;
            row->blk = blkoff;
            row->pg = pgoff;
            row->tstart = tstart;
            row->tend = tend;
            row->ulat = tend - tstart;
            row->type = 'w';
            row->failed = failed;
            row->datacmp = 2;
            row->size = node->wl->geo->sector_nbytes * cmdppas;
            fox_output_append(row, node->nid);
        }

        if (fox_update_runtime(node) || (node->wl->stats->flags & FOX_FLAG_DONE))
            return 1;
        else if (node->delay)
            usleep(node->delay);

    }

    return 0;
}

int fox_read_vec_pg (struct fox_node *node, struct fox_blkbuf *buf, uint16_t blkoff, uint16_t pgoff)
{
    struct nvm_ret ret = {0,0};
    struct nvm_addr *ppa_vec;
    uint64_t tstart, tend;
    uint8_t failed = 0;
    struct fox_output_row *row;
    uint8_t *vecbuf;

    int ppa_i, cmd_i, vec_i, cmdppas;
    int ppas_pg = node->wl->geo->nsectors * node->wl->geo->nplanes;
    int nppas = ppas_pg * node->nchs * node->nluns;
    int cmdpgs;
    int ncmds = (nppas % node->wl->nppas == 0) ? nppas / node->wl->nppas : (nppas / node->wl->nppas) + 1;
    size_t vpg_sz = node->wl->geo->page_nbytes * node->wl->geo->nplanes;
    size_t tot_bytes = node->wl->geo->sector_nbytes * nppas;

    const int PMODE = nvm_dev_get_pmode(node->wl->dev);

    ppa_vec = calloc (1, sizeof (struct nvm_addr) * nppas);
    if (!ppa_vec)
        return -1;

    vecbuf = calloc (1, node->nchs * node->nluns * tot_bytes);
    if (!vecbuf)
        return -1;

    ppa_i = 0;
    for (cmd_i = 0; cmd_i < ncmds; cmd_i++) {

        cmdppas = (ppa_i + node->wl->nppas > nppas) ? nppas - ppa_i : node->wl->nppas;
        cmdpgs = (cmdppas % ppas_pg == 0) ? cmdppas / ppas_pg : (cmdppas / ppas_pg) + 1;

        for (vec_i = 0; vec_i < cmdppas; vec_i++) {
            ppa_vec[vec_i].g.ch = node->ch[((vec_i + cmd_i * node->wl->nppas) / ppas_pg) / node->nluns];
            ppa_vec[vec_i].g.lun = node->lun[((vec_i + cmd_i * node->wl->nppas) / ppas_pg) % node->nluns];
            ppa_vec[vec_i].g.pl = ((vec_i + cmd_i * node->wl->nppas) % ppas_pg) / node->wl->geo->nsectors;

            fox_vblk_tgt(node, ppa_vec[vec_i].g.ch, ppa_vec[vec_i].g.lun, blkoff);

            ppa_vec[vec_i].g.blk = node->vblk_tgt.vblk->blks[0].g.blk;
            ppa_vec[vec_i].g.pg = pgoff;
            ppa_vec[vec_i].g.sec = vec_i % node->wl->geo->nsectors;

            ppa_i++;
        }

        tstart = fox_timestamp_tmp_start(&node->stats);

        if (nvm_addr_read(node->wl->dev, ppa_vec, cmdppas,
                vecbuf, NULL, PMODE, &ret)) {
            fox_set_stats(FOX_STATS_FAIL_R, &node->stats, cmdpgs);
            tend = fox_timestamp_end(FOX_STATS_RUNTIME, &node->stats);
            failed++;
            goto FAILED;
        }

        for (vec_i = 0; vec_i < cmdppas; vec_i++) {
            memcpy((buf[(vec_i + cmd_i * node->wl->nppas) / ppas_pg].buf_r + vpg_sz * pgoff) +
                    (ppa_vec[vec_i].g.sec +
                    (ppa_vec[vec_i].g.pl * node->wl->geo->nsectors)
                    * node->wl->geo->sector_nbytes),

                    vecbuf + node->wl->geo->sector_nbytes * (vec_i + cmd_i * node->wl->nppas),

                    node->wl->geo->sector_nbytes);
        }

        tend = fox_timestamp_end(FOX_STATS_READ_T, &node->stats);

        fox_timestamp_end(FOX_STATS_RW_SECT, &node->stats);
        fox_set_stats(FOX_STATS_BREAD, &node->stats, node->wl->geo->sector_nbytes * cmdppas);
        fox_set_stats(FOX_STATS_BRW_SEC, &node->stats, node->wl->geo->sector_nbytes * cmdppas);
        fox_set_stats(FOX_STATS_IOPS, &node->stats, 1);

FAILED:
        fox_set_stats(FOX_STATS_PGS_R, &node->stats, cmdpgs);
        node->stats.pgs_done += cmdpgs;

        if (node->wl->output) {
            row = fox_output_new();
            row->ch = node->nchs;
            row->lun = node->nluns;
            row->blk = blkoff;
            row->pg = pgoff;
            row->tstart = tstart;
            row->tend = tend;
            row->ulat = tend - tstart;
            row->type = 'r';
            row->failed = failed;
            row->datacmp = 2;
            row->size = node->wl->geo->sector_nbytes * cmdppas;
            fox_output_append(row, node->nid);
        }

        if (fox_update_runtime(node) || (node->wl->stats->flags & FOX_FLAG_DONE))
            return 1;
        else if (node->delay)
            usleep(node->delay);

    }

    return 0;
}

int fox_write_blk (struct fox_tgt_blk *tgt, struct fox_node *node,
                        struct fox_blkbuf *buf, uint16_t npgs, uint16_t blkoff)
{
    int i, cmd_pgs;
    uint8_t failed = 0;
    struct fox_output_row *row;
    struct nvm_addr ppa;
    uint64_t tstart, tend;
    size_t tot_bytes;
    size_t vpg_sz = node->wl->geo->page_nbytes * node->wl->geo->nplanes;

    cmd_pgs = node->wl->nppas /
                             (node->wl->geo->nsectors * node->wl->geo->nplanes);

    if (blkoff + npgs > node->npgs)
        printf ("Wrong write offset. pg (%d) > pgs_per_blk (%d).\n",
                                             blkoff + npgs, (int) node->npgs);

    for (i = blkoff; i < blkoff + npgs; i = i + cmd_pgs) {

        tstart = fox_timestamp_tmp_start(&node->stats);

        cmd_pgs = (i + cmd_pgs > blkoff + npgs) ? blkoff + npgs - i : cmd_pgs;
        tot_bytes = vpg_sz * cmd_pgs;

        /* If the data is human readable, updates the buffer */
        if (node->wl->memcmp == WB_READABLE &&
                                        node->wl->engine->id == FOX_ENGINE_1) {
            ppa.ppa = tgt->vblk->blks[0].ppa;
            ppa.g.pg = i;
            fox_wb_readable((char *)(buf->buf_w + vpg_sz * i), cmd_pgs,
                                                            node->wl->geo, ppa);
        }

        if (prov_vblk_pwrite(tgt->vblk,
                            buf->buf_w + vpg_sz * i,
                            tot_bytes,
                            vpg_sz * i) != tot_bytes){
            fox_set_stats (FOX_STATS_FAIL_W, &node->stats, cmd_pgs);
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
    uint64_t tstart, tend, vwpg;
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

        if (prov_vblk_pread(tgt->vblk,
                            buf->buf_r + vpg_sz * i,
                            tot_bytes,
                            vpg_sz * i) != tot_bytes){
            fox_set_stats (FOX_STATS_FAIL_R, &node->stats, cmd_pgs);
            tend = fox_timestamp_end(FOX_STATS_RUNTIME, &node->stats);
            failed++;
            vwpg = tgt->vblk->blks[0].g.pg;
            goto FAILED;
        }

        tend = fox_timestamp_end(FOX_STATS_READ_T, &node->stats);
        fox_timestamp_end(FOX_STATS_RW_SECT, &node->stats);

        /* Set page in vblk for possible memory comparison */
        vwpg = tgt->vblk->blks[0].g.pg;
        tgt->vblk->blks[0].g.pg = i;

        cmp = (node->wl->memcmp) ?
                          fox_blkbuf_cmp(node, buf, i, cmd_pgs, tgt->vblk) : 2;

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

        /* Create a file under /corruption containing the read binary */
        if (node->wl->memcmp && cmp) {
            char filename[40];
            uint32_t pblk = fox_vblk_get_pblk (node->wl, tgt->ch, tgt->lun,
                                                                      tgt->blk);

            sprintf(filename, "c%dl%db%dp%d-seq%d", tgt->ch, tgt->lun, pblk, i,
                                                                      cmd_pgs);

            if (node->wl->memcmp == WB_GEOMETRY)
                fox_wb_geo (buf->buf_w, tot_bytes, node->wl->geo,
                                              tgt->vblk->blks[0], WB_GEO_FILL);

            fox_flush_corruption (filename, buf->buf_w + vpg_sz * i,
                                            buf->buf_r + vpg_sz * i, tot_bytes);
        }

        /* Set page in vblk back to previous position */
        tgt->vblk->blks[0].g.pg = vwpg;

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

    if (prov_vblk_erase (tgt->vblk)<0)
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