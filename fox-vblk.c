/*  - FOX - A tool for testing Open-Channel SSDs
 *      - LiblightNVM vblk abstraction
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

#include <stdlib.h>
#include <stdio.h>
#include "fox.h"

int fox_vblk_tgt (struct fox_node *node, uint16_t chid, uint16_t lunid,
                                                                 uint32_t blkid)
{
    int t_blks, t_luns, blk_ch, blk_lun, boff;
    struct fox_workload *wl = node->wl;

    t_luns = wl->luns * wl->channels;
    t_blks = wl->blks * t_luns;
    blk_lun = t_blks / t_luns;
    blk_ch = blk_lun * wl->luns;

    if (chid > wl->channels - 1 || lunid > wl->luns - 1 || blkid > wl->blks - 1)
        return -1;

    boff = (chid * blk_ch) + (lunid * blk_lun) + blkid;

    /* TODO: bitmap of busy blocks
             set node->vblk_tgt as idle and boff as busy */

    node->vblk_tgt.vblk = wl->vblks[boff];
    node->vblk_tgt.ch = chid;
    node->vblk_tgt.lun = lunid;
    node->vblk_tgt.blk = blkid;

    return 0;
}

static int fox_write_vblk (NVM_VBLK vblk, struct fox_workload *wl)
{
    uint8_t *buf, *buf_off;
    int i;

    buf = malloc (wl->geo.vblk_nbytes);

    for (i = 0; i < wl->pgs; i++) {
        buf_off = buf + wl->geo.vpg_nbytes * i;

        if (nvm_vblk_pwrite(vblk, buf_off, wl->geo.vpg_nbytes,
                                                       wl->geo.vpg_nbytes * i)){
            printf ("WARNING: error when writing to vblk page.\n");
            return -1;
        }
    }

    return 0;
}

int fox_alloc_vblks (struct fox_workload *wl)
{
    int ch_i, lun_i, blk_i, t_blks, t_luns, blk_ch, blk_lun;

    t_luns = wl->luns * wl->channels;
    t_blks = wl->blks * t_luns;
    blk_lun = t_blks / t_luns;
    blk_ch = blk_lun * wl->luns;

    wl->vblks = malloc (sizeof(NVM_VBLK) * t_blks);

    if (!wl->vblks)
        return -1;

    printf ("\n");
    for (blk_i = 0; blk_i < t_blks; blk_i++) {
        printf ("\r - Allocating blocks... [%d/%d]", blk_i, t_blks);
        fflush(stdout);

        wl->vblks[blk_i] = nvm_vblk_new();

        ch_i = blk_i / blk_ch;
        lun_i = (blk_i % blk_ch) / blk_lun;

        fox_timestamp_tmp_start(wl->stats);

        /* TODO: treat error */
        nvm_vblk_gets(wl->vblks[blk_i], wl->dev, ch_i, lun_i);
        fox_timestamp_end(FOX_STATS_ERASE_T, wl->stats);
        fox_set_stats (FOX_STATS_ERASED_BLK, wl->stats, 1);

        /* Write wl->pgs to vblk for 100% read workload */
        if (wl->w_factor == 0)
            fox_write_vblk (wl->vblks[blk_i], wl);
    }
    printf ("\r - Preparing blocks... [%d/%d]\n", blk_i, t_blks);

    return 0;
}

void fox_free_vblks (struct fox_workload *wl)
{
    int blk_i, t_blks;

    t_blks = wl->blks * wl->luns * wl->channels;

    for (blk_i = 0; blk_i < t_blks; blk_i++)
        nvm_vblk_put(wl->vblks[blk_i]);

    free (wl->vblks);
}