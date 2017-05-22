/*  - FOX - A tool for testing Open-Channel SSDs
 *      - Buffer control
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

#include <string.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>

#include "fox.h"

#define FOX_BUF_WRITE 0x1
#define FOX_BUF_READ  0x0

static void fox_fill_wb (uint8_t *wb, size_t sz)
{
    int i;
    uint8_t byte = 0xff;

    srand(time (NULL));
    for (i = 0; i < sz; i++) {
        if (i % 16 == 0)
            byte = rand() % 255;
        memset (&wb[i], byte, 1);
    }
}

static void *fox_alloc_blk_buf_t (struct fox_node *node, uint8_t type)
{
    void *buf;
    size_t vpg_sz = node->wl->geo->page_nbytes * node->wl->geo->nplanes;
    size_t size = node->npgs * vpg_sz;

    buf = aligned_alloc (node->wl->geo->sector_nbytes, size);

    if (!buf)
        return buf;
    if (type == FOX_BUF_WRITE)
        fox_fill_wb (buf, size);
    else
        memset (buf, 0x0, size);
    return buf;
}

int fox_alloc_blk_buf (struct fox_node *node, struct fox_blkbuf *buf)
{
    buf->buf_r = fox_alloc_blk_buf_t(node, FOX_BUF_READ);
    buf->buf_w = fox_alloc_blk_buf_t(node, FOX_BUF_WRITE);

    if (!buf->buf_w || !buf->buf_r)
        return -1;

    return 0;
}

void fox_blkbuf_reset (struct fox_node *node, struct fox_blkbuf *buf)
{
    size_t vpg_sz = node->wl->geo->page_nbytes * node->wl->geo->nplanes;
    
    //fox_fill_wb (buf->buf_w, node->npgs * node->wl->geo->vpg_nbytes);
    memset (buf->buf_r, 0x0, node->npgs * vpg_sz);
}

void fox_free_blkbuf (struct fox_blkbuf *buf, int count)
{
    int i;

    for (i = 0; i < count; i++) {
        free (buf[i].buf_r);
        free (buf[i].buf_w);
    }
}

int fox_blkbuf_cmp (struct fox_node *node, struct fox_blkbuf *buf,
                                                 uint16_t pgoff, uint16_t npgs)
{
    uint8_t *offw, *offr;
    size_t vpg_sz = node->wl->geo->page_nbytes * node->wl->geo->nplanes;

    if (!node->wl->memcmp)
        return -1;

    if (pgoff + npgs > node->npgs) {
        printf ("Wrong memcmp offset. pg (%d) > pgs_per_blk (%d).\n",
                                               pgoff + npgs, (int) node->npgs);
        return -1;
    }

    offw = buf->buf_w + vpg_sz * pgoff;
    offr = buf->buf_r + vpg_sz * pgoff;

    if (memcmp (offw, offr, vpg_sz * npgs)) {
        fox_set_stats(FOX_STATS_FAIL_CMP, &node->stats, 1);
        return 1;
    }

    return 0;
}
