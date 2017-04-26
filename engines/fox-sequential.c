/*  - FOX - A tool for testing Open-Channel SSDs
 *      - Engine 1. Sequential I/O
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

/* ENGINE 1: All sequential:
 * (ch,lun,blk,pg)
 * (0,0,0,0)
 * (0,0,0,1)
 * (0,0,0,2)
 * (0,0,1,0)
 * (0,0,1,1)
 * (0,0,1,2)
 * (0,1,0,0)
 * (0,1,0,1) ...
 */

#include <stdio.h>
#include <stdlib.h>
#include "../fox.h"

static int seq_start (struct fox_node *node)
{
    uint32_t t_blks;
    uint16_t t_luns, blk_lun, blk_ch, pgoff_r, pgoff_w, npgs, aux_r;
    int ch_i, lun_i, blk_i;
    node->stats.pgs_done = 0;
    struct fox_blkbuf nbuf;

    t_luns = node->nluns * node->nchs;
    t_blks = node->nblks * t_luns;
    blk_lun = t_blks / t_luns;
    blk_ch = blk_lun * node->nluns;

    if (fox_alloc_blk_buf (node, &nbuf))
        goto OUT;

    fox_start_node (node);

    do {
        for (blk_i = 0; blk_i < t_blks; blk_i++) {
            ch_i = blk_i / blk_ch;
            lun_i = (blk_i % blk_ch) / blk_lun;

            fox_vblk_tgt(node, node->ch[ch_i],node->lun[lun_i],blk_i % blk_lun);

            if (node->wl->w_factor == 0)
                goto READ;

            pgoff_r = 0;
            pgoff_w = 0;
            while (pgoff_w < node->npgs) {
                if (node->wl->r_factor == 0)
                    npgs = node->npgs;
                else
                    npgs = (pgoff_w + node->wl->w_factor > node->npgs) ?
                                    node->npgs - pgoff_w : node->wl->w_factor;

                if (fox_write_blk(&node->vblk_tgt,node,&nbuf,npgs,pgoff_w))
                    goto BREAK;
                pgoff_w += npgs;

                aux_r = 0;
                while (aux_r < node->wl->r_factor) {
                    if (node->wl->w_factor == 0)
                        npgs = node->npgs;
                    npgs = (pgoff_r + node->wl->r_factor > pgoff_w) ?
                                    pgoff_w - pgoff_r : node->wl->r_factor;

                    if (fox_read_blk(&node->vblk_tgt,node,&nbuf,npgs,pgoff_r))
                        goto BREAK;

                    aux_r += npgs;
                    pgoff_r = (pgoff_r + node->wl->r_factor > pgoff_w) ?
                                                            0 : pgoff_r + npgs;
                }
            }

READ:
            /* 100 % reads */
            if (node->wl->w_factor == 0) {
                if (fox_read_blk (&node->vblk_tgt,node,&nbuf,node->npgs,0))
                    goto BREAK;
            }
            if (node->wl->w_factor < 100)
                fox_blkbuf_reset(node, &nbuf);
        }

BREAK:
        if ((node->wl->stats->flags & FOX_FLAG_DONE) || !node->wl->runtime ||
                                                   node->stats.progress >= 100)
            break;

        if (node->wl->w_factor != 0)
            if (fox_erase_all_vblks (node))
                break;

    } while (1);

    fox_end_node (node);
    fox_free_blkbuf (&nbuf, 1);
    return 0;

OUT:
    return -1;
}

static void seq_exit (void)
{
    return;
}

static struct fox_engine seq_engine = {
    .id             = FOX_ENGINE_1,
    .name           = "sequential",
    .start          = seq_start,
    .exit           = seq_exit,
};

int foxeng_seq_init (struct fox_workload *wl)
{
    return fox_engine_register(&seq_engine);
}