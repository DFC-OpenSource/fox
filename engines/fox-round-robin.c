/* ENGINE 2: Channels, LUNs and blocks round-robin:
 * (ch,lun,blk,pg)
 * (0,0,0,0)
 * (1,0,0,0)
 * (2,0,0,0)
 * (0,1,0,0)
 * (1,1,0,0)
 * (2,1,0,0)
 * (0,0,0,1)
 * (1,0,0,1) ...
 *  */

#include <stdio.h>
#include <stdlib.h>
#include "../fox.h"

struct rr_var {
    int ncol;
    int pgs_sblk;
    int ch_i;
    int lun_i;
    int blk_i;
    int pg_i;
    int roff;
    int woff;
    int r_i;
    int w_i;
    uint8_t end;
    struct fox_rw_iterator *it;
    struct fox_blkbuf *bufblk;
};

static int rr_write_factor (struct fox_node *node, struct rr_var *var)
{
    while (var->woff < node->wl->w_factor) {
        var->pg_i = var->it->row_w % node->npgs;
        var->blk_i = var->it->row_w / node->npgs;
        var->ch_i = var->it->col_w % node->nchs;
        var->lun_i = var->it->col_w / node->nchs;

        fox_vblk_tgt(node, node->ch[var->ch_i], node->lun[var->lun_i],
                                                                    var->blk_i);

        if (fox_write_blk(&node->vblk_tgt, node, &var->bufblk[var->it->col_w],
                                                                 1, var->pg_i))
            return -1;

        if (!fox_iterator_next(var->it, FOX_WRITE)) {
            var->end++;
            break;
        }
        var->woff++;
    }
    return 0;
}

static int rr_read_factor (struct fox_node *node, struct rr_var *var)
{
    while (var->roff < node->wl->r_factor) {
        var->w_i = (var->it->row_w * var->ncol) + var->it->col_w;
        var->r_i = (var->it->row_r * var->ncol) + var->it->col_r;
        if (var->r_i >= var->w_i) {
            do {
                fox_iterator_prior(var->it, FOX_READ);
                var->w_i = (var->it->row_w * var->ncol) + var->it->col_w;
                var->r_i = (var->it->row_r * var->ncol) + var->it->col_r;
            } while (var->r_i > 0 && var->r_i > var->w_i - var->pgs_sblk);
        } else if (var->r_i < var->w_i - var->pgs_sblk) {
            do {
                fox_iterator_next(var->it, FOX_READ);
                var->w_i = (var->it->row_w * var->ncol) + var->it->col_w;
                var->r_i = (var->it->row_r * var->ncol) + var->it->col_r;
            } while (var->r_i < var->w_i - var->pgs_sblk);
        }

        var->pg_i = var->it->row_r % node->npgs;
        var->blk_i = var->it->row_r / node->npgs;
        var->ch_i = var->it->col_r % node->nchs;
        var->lun_i = var->it->col_r / node->nchs;

        fox_vblk_tgt(node, node->ch[var->ch_i], node->lun[var->lun_i],
                                                                   var->blk_i);
        if (fox_read_blk(&node->vblk_tgt, node, &var->bufblk[var->it->col_r], 1,
                                                                    var->pg_i))
            return -1;

        fox_iterator_next(var->it, FOX_READ);
        var->roff++;
    }

    return 0;
}

static int rr_read_100 (struct fox_node *node, struct rr_var *var)
{
    do {
        var->pg_i = var->it->row_r % node->npgs;
        var->blk_i = var->it->row_r / node->npgs;
        var->ch_i = var->it->col_r % node->nchs;
        var->lun_i = var->it->col_r / node->nchs;

        fox_vblk_tgt(node, node->ch[var->ch_i], node->lun[var->lun_i],
                                                                   var->blk_i);
        if (fox_read_blk(&node->vblk_tgt, node, &var->bufblk[var->it->col_w],
                                                                 1, var->pg_i))
            return -1;

        fox_iterator_next(var->it, FOX_READ);

        var->r_i = (var->it->row_r * var->ncol) + var->it->col_r;
        if (var->r_i >= var->pgs_sblk * node->nblks - 1)
            var->end++;

    } while (!var->end);

    return 0;
}

static int rr_init_var (struct fox_node *node, struct rr_var *var)
{
    node->stats.pgs_done = 0;
    var->ncol = node->nluns * node->nchs;
    var->pgs_sblk = var->ncol * node->npgs;

    var->it = fox_iterator_new(node);
    if (!var->it)
        goto OUT;

    var->bufblk = malloc(sizeof (struct fox_blkbuf) * node->nchs * node->nluns);
    if (!var->bufblk)
        goto ITERATOR;

    for (var->blk_i = 0; var->blk_i < node->nchs * node->nluns; var->blk_i++) {
        if (fox_alloc_blk_buf (node, &var->bufblk[var->blk_i])) {
            fox_free_blkbuf(var->bufblk, var->blk_i);
            goto BUFBLK;
        }
    }

    return 0;

BUFBLK:
    free (var->bufblk);
ITERATOR:
    fox_iterator_free(var->it);
OUT:
    return -1;
}

static int rr_start (struct fox_node *node)
{
    struct rr_var var;

    if (rr_init_var (node, &var))
        return -1;

    fox_start_node (node);

    do {
        var.end = 0;
        fox_iterator_reset(var.it);
        do {
            if (node->wl->w_factor == 0)
                goto READ;

            var.roff = 0;
            var.woff = 0;

            if(rr_write_factor (node, &var))
                goto BREAK;

            if (var.end)
                fox_iterator_prior(var.it, FOX_WRITE);

            if (rr_read_factor (node, &var))
                goto BREAK;

READ:
            /* 100 % reads */
            if (node->wl->w_factor == 0)
                if (rr_read_100 (node, &var))
                    goto BREAK;

        } while (!var.end);

BREAK:
        if ((node->wl->stats->flags & FOX_FLAG_DONE) || !node->wl->runtime ||
                                                   node->stats.progress >= 100)
            break;

        if (node->wl->w_factor != 0)
            if (fox_erase_all_vblks (node))
                break;

    } while (1);

    fox_end_node (node);
    fox_free_blkbuf(var.bufblk, node->nchs * node->nluns);

    return 0;
}

static void rr_exit (struct fox_node *node)
{
    return;
}

static struct fox_engine rr_engine = {
    .id             = 2,
    .name           = "round-robin",
    .start          = rr_start,
    .exit           = rr_exit,
};

int foxeng_rr_init (struct fox_workload *wl)
{
    return fox_engine_register(&rr_engine);
}