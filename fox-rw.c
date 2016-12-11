#include <liblightnvm.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/time.h>
#include <stdio.h>
#include "fox.h"

static double fox_check_progress_pgs (struct fox_node *node)
{
    uint32_t t_pgs = node->npgs * node->nblks * node->nluns * node->nchs;

    return (100 / (double) t_pgs) * (double) node->stats.pgs_done;
}

static double fox_check_progress_runtime (struct fox_node *node)
{
    fox_timestamp_end(FOX_STATS_RUNTIME, &node->stats);

    return (100 / (double) (node->wl->runtime)) * (node->stats.runtime / SEC64);
}

static int fox_update_runtime (struct fox_node *node)
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

static int fox_write_blk (struct fox_tgt_blk *tgt, struct fox_node *node,
                        struct fox_blkbuf *buf, uint16_t npgs, uint16_t blkoff)
{
    int i;
    uint8_t *buf_off, failed = 0;
    struct fox_output_row *row;
    uint64_t tstart, tend;

    if (blkoff + npgs > node->npgs)
        printf ("Wrong write offset. pg (%d) > pgs_per_blk (%d).\n",
                                             blkoff + npgs, (int) node->npgs);

    for (i = blkoff; i < blkoff + npgs; i++) {

        buf_off = buf->buf_w + node->wl->geo.vpg_nbytes * i;
        tstart = fox_timestamp_tmp_start(&node->stats);

        if (nvm_vblk_pwrite(tgt->vblk, buf_off, node->wl->geo.vpg_nbytes,
                                                 node->wl->geo.vpg_nbytes * i)){
            fox_set_stats (FOX_STATS_FAIL_W, &node->stats, 1);
            tend = fox_timestamp_end(FOX_STATS_RUNTIME, &node->stats);
            failed++;
            goto FAILED;
        }

        tend = fox_timestamp_end(FOX_STATS_WRITE_T, &node->stats);
        fox_timestamp_end(FOX_STATS_RW_SECT, &node->stats);
        fox_set_stats(FOX_STATS_BWRITTEN,&node->stats,node->wl->geo.vpg_nbytes);
        fox_set_stats(FOX_STATS_BRW_SEC, &node->stats,node->wl->geo.vpg_nbytes);
        fox_set_stats(FOX_STATS_IOPS, &node->stats, 1);

FAILED:
        fox_set_stats (FOX_STATS_PGS_W, &node->stats, 1);
        node->stats.pgs_done++;

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
            row->size = node->wl->geo.vpg_nbytes;
            fox_output_append(row, node->nid);
        }

        if (fox_update_runtime(node)||(node->wl->stats->flags & FOX_FLAG_DONE))
            return 1;
    }
    if (node->delay)
        usleep(node->delay);

    return 0;
}

static int fox_read_blk (struct fox_tgt_blk *tgt, struct fox_node *node,
                        struct fox_blkbuf *buf, uint16_t npgs, uint16_t blkoff)
{
    int i;
    uint8_t *buf_off, failed = 0, cmp;
    struct fox_output_row *row;
    uint64_t tstart, tend;

    if (blkoff + npgs > node->npgs)
        printf ("Wrong read offset. pg (%d) > pgs_per_blk (%d).\n",
                                             blkoff + npgs, (int) node->npgs);

    for (i = blkoff; i < blkoff + npgs; i++) {
        buf_off = buf->buf_r + node->wl->geo.vpg_nbytes * i;
        tstart = fox_timestamp_tmp_start(&node->stats);

        if (nvm_vblk_pread(tgt->vblk, buf_off, node->wl->geo.vpg_nbytes,
                                                 node->wl->geo.vpg_nbytes * i)){
            fox_set_stats (FOX_STATS_FAIL_R, &node->stats, 1);
            tend = fox_timestamp_end(FOX_STATS_RUNTIME, &node->stats);
            failed++;
            goto FAILED;
        }

        tend = fox_timestamp_end(FOX_STATS_READ_T, &node->stats);
        fox_timestamp_end(FOX_STATS_RW_SECT, &node->stats);

        cmp = (node->wl->memcmp) ? fox_blkbuf_cmp(node, buf, i, 1) : 2;

        fox_set_stats (FOX_STATS_BREAD, &node->stats, node->wl->geo.vpg_nbytes);
        fox_set_stats (FOX_STATS_BRW_SEC,&node->stats,node->wl->geo.vpg_nbytes);
        fox_set_stats(FOX_STATS_IOPS, &node->stats, 1);

FAILED:
        fox_set_stats (FOX_STATS_PGS_R, &node->stats, 1);

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
            row->size = node->wl->geo.vpg_nbytes;
            fox_output_append(row, node->nid);
        }

        if (node->wl->w_factor == 0) {
            node->stats.pgs_done++;
            if (fox_update_runtime(node))
                return 1;
        }

        if (node->wl->stats->flags & FOX_FLAG_DONE)
            return 1;
    }
    if (node->delay)
        usleep(node->delay);

    return 0;
}

static int fox_erase_blk (struct fox_tgt_blk *tgt, struct fox_node *node)
{
    fox_timestamp_tmp_start(&node->stats);

    if (nvm_vblk_erase (tgt->vblk))
        fox_set_stats (FOX_STATS_FAIL_E, &node->stats, 1);

    fox_timestamp_end(FOX_STATS_ERASE_T, &node->stats);
    fox_set_stats (FOX_STATS_ERASED_BLK, &node->stats, 1);

    if (fox_update_runtime(node) || node->wl->stats->flags & FOX_FLAG_DONE)
        return 1;

    return 0;
}

static int fox_erase_all_vblks (struct fox_node *node)
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

static struct fox_rw_iterator *fox_iterator_new (struct fox_node *node)
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

static void fox_iterator_free (struct fox_rw_iterator *it)
{
    free (it);
}

static int fox_iterator_next (struct fox_rw_iterator *it, uint8_t type)
{
    uint32_t *row, *col;

    row = (type == FOX_READ) ? &it->row_r : &it->row_w;
    col = (type == FOX_READ) ? &it->col_r : &it->col_w;

    *col = (*col >= it->cols - 1) ? 0 : *col + 1;

    if (!(*col))
        *row = (*row >= it->rows - 1) ? 0 : *row + 1;

   return !(type == FOX_WRITE && !(*col) && !(*row));
}

static int fox_iterator_prior (struct fox_rw_iterator *it, uint8_t type)
{
    uint32_t *row, *col;

    row = (type == FOX_READ) ? &it->row_r : &it->row_w;
    col = (type == FOX_READ) ? &it->col_r : &it->col_w;

    *col = (*col == 0) ? it->cols - 1 : *col - 1;

    if (*col == it->cols - 1)
        *row = (*row == 0) ? it->rows - 1 : *row - 1;

   return !(type == FOX_WRITE && (*col == it->cols - 1) &&
                                                        (*row == it->rows - 1));
}

static void fox_iterator_reset (struct fox_rw_iterator *it)
{
    it->row_w = 0;
    it->row_r = 0;
    it->col_r = 0;
    it->col_w = 0;
}

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
void *fox_engine1 (void * arg)
{
    struct fox_node *node = (struct fox_node *) arg;
    uint32_t t_blks;
    uint16_t t_luns, blk_lun, blk_ch, pgoff_r, pgoff_w, npgs, aux_r;
    int ch_i, lun_i, blk_i;
    struct fox_blkbuf bufblk;
    node->stats.pgs_done = 0;

    t_luns = node->nluns * node->nchs;
    t_blks = node->nblks * t_luns;
    blk_lun = t_blks / t_luns;
    blk_ch = blk_lun * node->nluns;

    if (fox_alloc_blk_buf (node, &bufblk))
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
                npgs = (pgoff_w + node->wl->w_factor > node->npgs) ?
                                    node->npgs - pgoff_w : node->wl->w_factor;

                if (fox_write_blk(&node->vblk_tgt,node,&bufblk,npgs,pgoff_w))
                    goto BREAK;
                pgoff_w += npgs;

                aux_r = 0;
                while (aux_r < node->wl->r_factor) {
                    npgs = (pgoff_r + node->wl->r_factor > pgoff_w) ?
                                    pgoff_w - pgoff_r : node->wl->r_factor;

                    if (fox_read_blk(&node->vblk_tgt,node,&bufblk,npgs,pgoff_r))
                        goto BREAK;

                    aux_r += npgs;
                    pgoff_r = (pgoff_r + node->wl->r_factor > pgoff_w) ?
                                                            0 : pgoff_r + npgs;
                }
            }

READ:
            /* 100 % reads */
            if (node->wl->w_factor == 0) {
                if (fox_read_blk (&node->vblk_tgt,node,&bufblk,node->npgs,0))
                    goto BREAK;
            }
            fox_blkbuf_reset(node, &bufblk);
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
    fox_free_blkbuf (&bufblk, 1);

OUT:
    return NULL;
}

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
void *fox_engine2 (void * arg)
{
    struct fox_node *node = (struct fox_node *) arg;
    uint8_t end;
    int ncol, pgs_sblk, ch_i, lun_i, blk_i, pg_i, roff, woff, r_i,w_i;
    struct fox_blkbuf *bufblk;
    struct fox_rw_iterator *it;

    node->stats.pgs_done = 0;

    ncol = node->nluns * node->nchs;
    pgs_sblk = ncol * node->npgs;

    it = fox_iterator_new(node);
    if (!it)
        goto OUT;

    bufblk = malloc (sizeof (struct fox_blkbuf) * node->nchs * node->nluns);
    if (!bufblk)
        goto ITERATOR;

    for (blk_i = 0; blk_i < node->nchs * node->nluns; blk_i++) {
        if (fox_alloc_blk_buf (node, &bufblk[blk_i])) {
            fox_free_blkbuf(bufblk, blk_i);
            goto BUFBLK;
        }
    }

    fox_start_node (node);

    do {
        end = 0;
        fox_iterator_reset(it);
        do {
            if (node->wl->w_factor == 0)
                goto READ;

            roff = 0;
            woff = 0;
            while (woff < node->wl->w_factor) {
                pg_i = it->row_w % node->npgs;
                blk_i = it->row_w / node->npgs;
                ch_i = it->col_w % node->nchs;
                lun_i = it->col_w / node->nchs;
                fox_vblk_tgt(node, node->ch[ch_i], node->lun[lun_i], blk_i);
                if (fox_write_blk(&node->vblk_tgt, node, &bufblk[it->col_w], 1,
                                                                         pg_i))
                    goto BREAK;
                if (!fox_iterator_next(it, FOX_WRITE)) {
                    end++;
                    break;
                }
                woff++;
            }

            if (end)
                fox_iterator_prior(it, FOX_WRITE);

            while (roff < node->wl->r_factor) {
                w_i = (it->row_w * ncol) + it->col_w;
                r_i = (it->row_r * ncol) + it->col_r;
                if (r_i >= w_i) {
                    do {
                        fox_iterator_prior(it, FOX_READ);
                        w_i = (it->row_w * ncol) + it->col_w;
                        r_i = (it->row_r * ncol) + it->col_r;
                    } while (r_i > 0 && r_i > w_i - pgs_sblk);
                } else if (r_i < w_i - pgs_sblk) {
                    do {
                        fox_iterator_next(it, FOX_READ);
                        w_i = (it->row_w * ncol) + it->col_w;
                        r_i = (it->row_r * ncol) + it->col_r;
                    } while (r_i < w_i - pgs_sblk);
                }

                pg_i = it->row_r % node->npgs;
                blk_i = it->row_r / node->npgs;
                ch_i = it->col_r % node->nchs;
                lun_i = it->col_r / node->nchs;

                fox_vblk_tgt(node, node->ch[ch_i], node->lun[lun_i], blk_i);
                if (fox_read_blk(&node->vblk_tgt, node, &bufblk[it->col_r], 1,
                                                                         pg_i))
                    goto BREAK;

                fox_iterator_next(it, FOX_READ);
                roff++;
            }

READ:
            /* 100 % reads */
            if (node->wl->w_factor == 0) {
                do {
                    pg_i = it->row_r % node->npgs;
                    blk_i = it->row_r / node->npgs;
                    ch_i = it->col_r % node->nchs;
                    lun_i = it->col_r / node->nchs;

                    fox_vblk_tgt(node, node->ch[ch_i], node->lun[lun_i], blk_i);
                    if (fox_read_blk(&node->vblk_tgt, node, &bufblk[it->col_w],
                                                                      1, pg_i))
                        goto BREAK;

                    fox_iterator_next(it, FOX_READ);

                    r_i = (it->row_r * ncol) + it->col_r;
                    if (r_i >= pgs_sblk * node->nblks - 1)
                        end++;

                } while (!end);
            }
        } while (!end);

BREAK:
        if ((node->wl->stats->flags & FOX_FLAG_DONE) || !node->wl->runtime ||
                                                   node->stats.progress >= 100)
            break;

        if (node->wl->w_factor != 0)
            if (fox_erase_all_vblks (node))
                break;

    } while (1);

    fox_end_node (node);
    fox_free_blkbuf(bufblk, node->nchs * node->nluns);

BUFBLK:
    free (bufblk);
ITERATOR:
    fox_iterator_free(it);
OUT:
    return NULL;
}