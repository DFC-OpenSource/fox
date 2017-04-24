/*  - FOX - A tool for testing Open-Channel SSDs
 *      - Engine 3 - Channel and LUN I/O Isolation
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

/* Engine 3: IO Isolation
 *
 * This engine applies isolated reads/writes in parallel units.
 * Each thread will perform a single type of IO in chunks of 2MB.
 * All IOs
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include "../fox.h"

/* Sets the thread type (read / write)
 *
 * @return 0 for writing thread, positive for reading thread
 */
static int iso_get_th_type (struct fox_node *node)
{
    uint16_t th_type; // 0 - write, positive - read
    int w_th, r_th;

    if (node->wl->w_factor > 0 && node->wl->r_factor > 0) {
        w_th = (node->wl->w_factor > node->wl->r_factor) ?
                                node->wl->w_factor / node->wl->r_factor : 1;
        r_th = (node->wl->r_factor > node->wl->w_factor) ?
                                node->wl->r_factor / node->wl->w_factor : 1;
    } else {
        w_th = node->wl->w_factor;
        r_th = node->wl->r_factor;
    }

    if (node->nid == 0)
        th_type = (w_th == 1 || r_th == 0) ? 0 : 1;
    else
        th_type = (r_th == 0) ? 0 :
                  (w_th == 0) ? 1 :
                  (r_th > w_th || r_th == w_th) ? node->nid % (r_th + 1) :
                  (node->nid % (w_th + 1)) ? 0 : 1;

    return th_type;
}

static int iso_read_prepare (struct fox_node *node, struct fox_blkbuf *bufblk)
{
    int ch_i, lun_i, blk_i, blkoff, pg_i, cmd_pgs;
    size_t tot_bytes;

    /* Write all blocks for reading threads */
    for (ch_i = 0; ch_i < node->nchs; ch_i++) {
        for (lun_i = 0; lun_i < node->nluns; lun_i++) {
            for (blk_i = 0; blk_i < node->nblks; blk_i++) {

                fox_vblk_tgt(node, node->ch[ch_i], node->lun[lun_i], blk_i);
                cmd_pgs = node->wl->nppas /(node->wl->geo.nsectors *
                                                        node->wl->geo.nplanes);

                for (pg_i = 0; pg_i < node->npgs; pg_i = pg_i + cmd_pgs) {
                    cmd_pgs = (pg_i + cmd_pgs > node->npgs) ? node->npgs - pg_i
                                                                     : cmd_pgs;
                    tot_bytes = node->wl->geo.vpg_nbytes * cmd_pgs;
                    blkoff = (ch_i * node->nluns) + lun_i;
                    if (nvm_vblk_pwrite(node->vblk_tgt.vblk,
                                        bufblk[blkoff].buf_w +
                                                node->wl->geo.vpg_nbytes * pg_i,
                                        tot_bytes,
                                        node->wl->geo.vpg_nbytes * pg_i)){
                        printf ("Engine 3: error when writing to vblk page.n");
                        return -1;
                    }
                }
            }
        }
    }

    return 0;
}

static int iso_rw(struct fox_node *node, struct fox_blkbuf *bufblk, uint8_t dir)
{
    int blk_i, pg_i, lun_i, ch_i, end, ret;
    struct fox_rw_iterator *it;

    it = fox_iterator_new(node);
    if (!it)
        return -1;

    do {
        end = 0;
        do {
            pg_i = it->row_r % node->npgs;
            blk_i = it->row_r / node->npgs;
            ch_i = it->col_r % node->nchs;
            lun_i = it->col_r / node->nchs;

            fox_vblk_tgt(node, node->ch[ch_i], node->lun[lun_i], blk_i);

            ret = (dir == FOX_READ) ?
                fox_read_blk(&node->vblk_tgt, node, &bufblk[it->col_w],1,pg_i) :
                fox_write_blk(&node->vblk_tgt, node, &bufblk[it->col_w],1,pg_i);
            if (ret)
                goto RETURN;

            if (fox_iterator_next(it, 1))
                end++;

        } while (!end);

        if ((node->wl->stats->flags & FOX_FLAG_DONE) || !node->wl->runtime ||
                                                   node->stats.progress >= 100)
            break;

        if (dir == FOX_WRITE)
            if (fox_erase_all_vblks (node))
                break;

    } while (1);

RETURN:
    return 0;
}

static int iso_read (struct fox_node *node)
{
    int ret, blk_i;
    struct fox_blkbuf *bufblk;
    int totblk = node->nchs * node->nluns * node->nblks;

    bufblk = malloc(sizeof (struct fox_blkbuf) * node->nchs * node->nluns);
    if (!bufblk)
        return -1;

    for (blk_i = 0; blk_i < node->nchs * node->nluns; blk_i++) {
        if (fox_alloc_blk_buf (node, &bufblk[blk_i])) {
            fox_free_blkbuf(bufblk, blk_i);
            goto BUFBLK;
        }
    }

    printf(" - TID %d: READ", node->nid);

    /* If 100 % reads, FOX already prepared the blocks */
    if (node->wl->w_factor > 0) {
        printf(" - Filling up %d blocks...\n", totblk);
        ret = iso_read_prepare (node, bufblk);
        if (ret)
            goto FREE_BUF;
    } else
        printf("\n");

    fox_start_node (node);

    if (iso_rw (node, bufblk, FOX_READ)) {
        fox_end_node (node);
        goto FREE_BUF;
    }

    fox_end_node (node);
    fox_free_blkbuf (bufblk, node->nchs * node->nluns);
    free (bufblk);

    return 0;

FREE_BUF:
    fox_free_blkbuf(bufblk, node->nchs * node->nluns);
BUFBLK:
    free (bufblk);
    return -1;
}

static int iso_write (struct fox_node *node)
{
    int blk_i;
    struct fox_blkbuf *bufblk;

    bufblk = malloc(sizeof (struct fox_blkbuf) * node->nchs * node->nluns);
    if (!bufblk)
        return -1;

    for (blk_i = 0; blk_i < node->nchs * node->nluns; blk_i++) {
        if (fox_alloc_blk_buf (node, &bufblk[blk_i])) {
            fox_free_blkbuf(bufblk, blk_i);
            goto BUFBLK;
        }
    }

    printf(" - TID %d: WRITE\n", node->nid);

    fox_start_node (node);

    if (iso_rw (node, bufblk, FOX_WRITE)) {
        fox_end_node (node);
        goto FREE_BUF;
    }

    fox_end_node (node);
    fox_free_blkbuf (bufblk, node->nchs * node->nluns);
    free (bufblk);

    return 0;

FREE_BUF:
    fox_free_blkbuf(bufblk, node->nchs * node->nluns);
BUFBLK:
    free (bufblk);
    return -1;
}

static int iso_start (struct fox_node *node)
{
    int ret, th_type;

    fox_wait_for_monitor (node->wl);

    node->stats.pgs_done = 0;

    th_type = iso_get_th_type (node);
    ret = (th_type) ? iso_read (node) : iso_write (node);

    return ret;
}

static void iso_exit (struct fox_node *node)
{
    return;
}

static struct fox_engine iso_engine = {
    .id             = FOX_ENGINE_3,
    .name           = "isolation",
    .start          = iso_start,
    .exit           = iso_exit,
};

int foxeng_iso_init (struct fox_workload *wl)
{
    return fox_engine_register(&iso_engine);
}