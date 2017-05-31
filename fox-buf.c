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
#include <liblightnvm.h>

#include "fox.h"

#define FOX_BUF_WRITE 0x1
#define FOX_BUF_READ  0x0

void fox_wb_random (uint8_t *wb, size_t sz)
{
    int i;
    uint8_t byte = 0xff;

    srand(time (NULL));
    for (i = 0; i < sz; i += 16) {
        byte = rand() % 255;
        memset (&wb[i], byte, 16);
    }
}

void fox_wb_readable(char *buf, int npgs, const struct nvm_geo *geo,
                                                         struct nvm_addr ppa)
{
    unsigned int written;
    int pg_lines, i, pg;
    char aux[9];
    char input_char;
    uint32_t pl_sz;
    uint32_t val[9] = {0, 0, ppa.g.ch, 0, ppa.g.lun, 0, ppa.g.blk, 0, 0};

    written = 0;
    pl_sz = geo->nplanes * geo->page_nbytes;
    pg_lines = pl_sz / 64;

    for (pg = ppa.g.pg; pg < ppa.g.pg + npgs; pg++) {
        srand(time(NULL) + pg);
        val[8] = pg;
        for (i = 0; i < pg_lines - 1; i++) {
            input_char = (rand() % 93) + 33;
            switch (i) {
                case 0:
                    break;
                case 1:
                case 3:
                case 5:
                case 7:
                case 9:
                    memset(buf, '|', 1);
                    memset(buf + 1, '-', 61);
                    break;
                case 2:
                    sprintf(buf + 24, "CHANN ");
                    goto PRINT;
                case 4:
                    sprintf(buf + 24, "LUN   ");
                    goto PRINT;
                case 6:
                    sprintf(buf + 24, "BLOCK ");
                    goto PRINT;
                case 8:
                    sprintf(buf + 24, "PAGE  ");
PRINT:
                    memset(buf, '|', 1);
                    memset(buf + 1, ' ', 23);
                    sprintf(aux, "%d", val[i]);
                    memcpy(buf + 30, aux, strlen(aux));
                    memset(buf + 30 + strlen(aux), ' ', 23 + 9 - strlen(aux));
                    break;
                default:
                    memset(buf, '|', 1);
                    memset(buf + 1, (char) input_char, 61);
                    break;
            }
            if (i == 0)
                memset(buf, '#', 63);
            else
                memset(buf + 62, '|', 1);

            memset(buf + 63, '\n', 1);
            buf += 64;
            written += 64;
        }
        memset(buf, '#', 63);
        memset(buf + 63, '\n', 1);
        buf += 64;
        written += 64;
    }
}

int fox_wb_geo (uint8_t *wb, size_t sz, const struct nvm_geo *geo,
                                               struct nvm_addr ppa, uint8_t op)
{
    uint16_t nsec, sec_i;
    uint32_t byte_i;
    uint64_t sum, cmpl, cmph;
    struct nvm_addr cppa;
    uint8_t *wboff;

    if (sz % geo->sector_nbytes != 0 ||
                          sz > geo->npages * geo->page_nbytes * geo->nplanes) {

        printf ("\n buf: Buffer is not multiple of sector size or too large.");
        if (op == WB_GEO_FILL) {
            fox_wb_random (wb, sz);
            printf (" Filled with random data.\n");
        } else
            printf (" Comparison not performed.\n");

        return 0;
    }

    nsec = sz / geo->sector_nbytes;

    cppa.g.blk = ppa.g.blk + 1;
    cppa.g.lun = ppa.g.lun + 1;
    cppa.g.ch = ppa.g.ch + 1;
    cppa.g.pg = ppa.g.pg;
    for (sec_i = 0; sec_i < nsec; sec_i++) {
        cppa.g.sec = (sec_i % geo->nsectors) + 1;
        cppa.g.pl = ((sec_i % (geo->nsectors * geo->nplanes))
                                                          / geo->nsectors) + 1;
        if (sec_i % (geo->nsectors * geo->nplanes) == 0)
            cppa.g.pg += 1;

        wboff = wb + (sec_i * geo->sector_nbytes);

        sum = (uint64_t) (cppa.g.sec + cppa.g.pl + cppa.g.pg +
                                           cppa.g.blk + cppa.g.lun + cppa.g.ch);
        cmpl = cppa.ppa;
        cmph = sum;
        for (byte_i = 0; byte_i < geo->sector_nbytes; byte_i += 16) {
            cmpl += (uint64_t) byte_i;
            cmph += (uint64_t) byte_i;

            if (op == WB_GEO_FILL) {
                memcpy (&wboff[byte_i], &cmpl, 8);
                memcpy (&wboff[byte_i + 8], &cmph, 8);
            } else
                if (    memcmp (&wboff[byte_i], &cmpl, 8) ||
                        memcmp (&wboff[byte_i + 8], &cmph, 8))
                    return -1;
        }
    }

    return 0;
}

static void *fox_alloc_blk_buf_t (struct fox_node *node, uint8_t type)
{
    void *buf;
    size_t vpg_sz = node->wl->geo->page_nbytes * node->wl->geo->nplanes;
    size_t size = node->npgs * vpg_sz;
    struct nvm_addr ppa;

    buf = aligned_alloc (node->wl->geo->sector_nbytes, size);
    if (!buf)
        return buf;

    ppa.ppa = 0;
    if (type == FOX_BUF_WRITE) {
        switch (node->wl->memcmp) {
            case WB_READABLE:
                fox_wb_readable (buf, node->npgs, node->wl->geo, ppa);
                break;
            case WB_GEOMETRY:
                fox_wb_geo (buf, size, node->wl->geo, ppa, WB_GEO_FILL);
                break;
            case WB_RANDOM:
            case WB_DISABLE:
            default:
                fox_wb_random (buf, size);
        }
    } else
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
                           uint16_t pgoff, uint16_t npgs, struct nvm_vblk *vblk)
{
    int ret;
    uint8_t *offw, *offr;
    size_t vpg_sz = node->wl->geo->page_nbytes * node->wl->geo->nplanes;
    struct nvm_addr ppa;

    if (!node->wl->memcmp)
        return -1;

    if (pgoff + npgs > node->npgs) {
        printf ("Wrong memcmp offset. pg (%d) > pgs_per_blk (%d).\n",
                                               pgoff + npgs, (int) node->npgs);
        return -1;
    }

    offw = buf->buf_w + vpg_sz * pgoff;
    offr = buf->buf_r + vpg_sz * pgoff;

    switch (node->wl->memcmp) {
        case WB_RANDOM:
        case WB_READABLE:
            ret = memcmp (offw, offr, vpg_sz * npgs);
            break;
        case WB_GEOMETRY:
            ppa.ppa = 0;
            if (node->wl->engine->id == FOX_ENGINE_3 ||
                                                   node->wl->w_factor == 0)
                ppa.ppa = vblk->blks[0].ppa;
            else
                ppa.g.pg = vblk->blks[0].g.pg;

            ret = fox_wb_geo (offr, vpg_sz * npgs, node->wl->geo,
                                                              ppa, WB_GEO_CMP);
            break;
        case WB_DISABLE:
        default:
            ret = 0;
    }

    if (ret) {
        fox_set_stats(FOX_STATS_FAIL_CMP, &node->stats, 1);
        return 1;
    }

    return 0;
}