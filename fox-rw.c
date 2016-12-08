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

static int fox_write_blk (NVM_VBLK vblk, struct fox_node *node,
                        struct fox_blkbuf *buf, uint16_t npgs, uint16_t blkoff)
{
    int i;
    uint8_t *buf_off;

    if (blkoff + npgs > node->npgs)
        printf ("Wrong write offset. pg (%d) > pgs_per_blk (%d).\n",
                                             blkoff + npgs, (int) node->npgs);

    for (i = blkoff; i < blkoff + npgs; i++) {

        buf_off = buf->buf_w + node->wl->geo.vpg_nbytes * i;
        fox_timestamp_tmp_start(&node->stats);
        
        if (nvm_vblk_pwrite(vblk, buf_off, node->wl->geo.vpg_nbytes,
                                                 node->wl->geo.vpg_nbytes * i)){
            fox_set_stats (FOX_STATS_FAIL_W, &node->stats, 1);
            goto FAILED;
        }
        
        fox_timestamp_end(FOX_STATS_WRITE_T, &node->stats);
        fox_timestamp_end(FOX_STATS_RW_SECT, &node->stats);
        fox_set_stats(FOX_STATS_BWRITTEN,&node->stats,node->wl->geo.vpg_nbytes);
        fox_set_stats(FOX_STATS_BRW_SEC, &node->stats,node->wl->geo.vpg_nbytes);   
        
FAILED:
        fox_set_stats (FOX_STATS_PGS_W, &node->stats, 1);
        node->stats.pgs_done++;
        
        if (fox_update_runtime(node)||(node->wl->stats->flags & FOX_FLAG_DONE))
            return 1;
    }
    if (node->delay)
        usleep(node->delay);

    return 0;
}

static int fox_read_blk (NVM_VBLK vblk, struct fox_node *node,
                        struct fox_blkbuf *buf, uint16_t npgs, uint16_t blkoff)
{
    int i;
    uint8_t *buf_off;

    if (blkoff + npgs > node->npgs)
        printf ("Wrong read offset. pg (%d) > pgs_per_blk (%d).\n",
                                             blkoff + npgs, (int) node->npgs);

    for (i = blkoff; i < blkoff + npgs; i++) {
        buf_off = buf->buf_r + node->wl->geo.vpg_nbytes * i;
        fox_timestamp_tmp_start(&node->stats);

        if (nvm_vblk_pread(vblk, buf_off, node->wl->geo.vpg_nbytes,
                                                 node->wl->geo.vpg_nbytes * i)){
            fox_set_stats (FOX_STATS_FAIL_R, &node->stats, 1);
            goto FAILED;
        }

        fox_timestamp_end(FOX_STATS_READ_T, &node->stats);
        fox_timestamp_end(FOX_STATS_RW_SECT, &node->stats);

        if (node->wl->memcmp)
            fox_blkbuf_cmp(node, buf, i, 1);

        fox_set_stats (FOX_STATS_BREAD, &node->stats, node->wl->geo.vpg_nbytes);
        fox_set_stats (FOX_STATS_BRW_SEC,&node->stats,node->wl->geo.vpg_nbytes);

FAILED:
        fox_set_stats (FOX_STATS_PGS_R, &node->stats, 1);

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

static int fox_erase_blk (NVM_VBLK vblk, struct fox_node *node)
{
    fox_timestamp_tmp_start(&node->stats);

    if (nvm_vblk_erase (vblk))
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

        if (fox_erase_blk (node->vblk_tgt, node))
            return 1;
    }

    return 0;
}

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

                if (fox_write_blk(node->vblk_tgt, node, &bufblk, npgs, pgoff_w))
                    goto BREAK;
                pgoff_w += npgs;

                aux_r = 0;
                while (aux_r < node->wl->r_factor) {
                    npgs = (pgoff_r + node->wl->r_factor > pgoff_w) ?
                                    pgoff_w - pgoff_r : node->wl->r_factor;

                    if (fox_read_blk(node->vblk_tgt,node,&bufblk,npgs,pgoff_r))
                        goto BREAK;

                    aux_r += npgs;
                    pgoff_r = (pgoff_r + node->wl->r_factor > pgoff_w) ?
                                                            0 : pgoff_r + npgs;
                }
            }

READ:
            /* 100 % reads */
            if (node->wl->w_factor == 0) {
                if (fox_read_blk (node->vblk_tgt, node, &bufblk, node->npgs, 0))
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
    fox_free_blkbuf (&bufblk);

OUT:
    return NULL;
}