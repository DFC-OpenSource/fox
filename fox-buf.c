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
    uint8_t byte;

    srand(time (NULL));
    for (i = 0; i < sz; i++) {
        byte = rand() % 255;
        memset (&wb[i], byte, 1);
    }
}

static void *fox_alloc_blk_buf_t (struct fox_node *node, uint8_t type)
{
    void *buf;
    size_t size = node->npgs * node->wl->geo.vpg_nbytes;

    buf = malloc (size);
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
    //fox_fill_wb (buf->buf_w, node->npgs * node->wl->geo.vpg_nbytes);
    memset (buf->buf_r, 0x0, node->npgs * node->wl->geo.vpg_nbytes);
}

void fox_free_blkbuf (struct fox_blkbuf *buf)
{
    free (buf->buf_r);
    free (buf->buf_w);
}

int fox_blkbuf_cmp (struct fox_node *node, struct fox_blkbuf *buf,
                                                 uint16_t pgoff, uint16_t npgs)
{
    uint8_t *offw, *offr;

    if (!node->wl->memcmp)
        return -1;

    if (pgoff + npgs > node->npgs) {
        printf ("Wrong memcmp offset. pg (%d) > pgs_per_blk (%d).\n",
                                               pgoff + npgs, (int) node->npgs);
        return -1;
    }

    offw = buf->buf_w + node->wl->geo.vpg_nbytes * pgoff;
    offr = buf->buf_r + node->wl->geo.vpg_nbytes * pgoff;

    if (memcmp (offw, offr, node->wl->geo.vpg_nbytes * npgs)) {
        fox_set_stats(FOX_STATS_FAIL_CMP, &node->stats, 1);
        return -1;
    }

    return 0;
}
