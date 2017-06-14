#include <stdio.h>
#include <stdlib.h>
#include "../fox.h"

static int vec_start (struct fox_node *node)
{
    uint32_t t_blks;
    uint16_t t_luns, blk_lun, blk_ch, pgoff_r, pgoff_w, npgs, aux_r;
    int ch_i, lun_i, blk_i, blk_x, pg_i;
    node->stats.pgs_done = 0;
    struct fox_blkbuf *nbuf;

    t_luns = node->nluns * node->nchs;
    t_blks = node->nblks * t_luns;
    blk_lun = t_blks / t_luns;
    blk_ch = blk_lun * node->nluns;

    nbuf = malloc (t_luns * sizeof(struct fox_blkbuf));

    for (blk_i = 0; blk_i < t_luns; blk_i++) {
        if (fox_alloc_blk_buf (node, &nbuf[blk_i]))
            goto OUT;
    }

    fox_start_node (node);

    do {
        for (blk_i = 0; blk_i < node->nblks; blk_i++) {
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

                for (pg_i = 0; pg_i < npgs; pg_i++){
                    if(fox_write_vec_pg (node, nbuf, node->vblk_tgt.vblk->blks[0].g.blk,
                                                                    pgoff_w))
                        goto BREAK;
                    pgoff_w += 1;
                }

                aux_r = 0;
                while (aux_r < node->wl->r_factor) {
                    if (node->wl->w_factor == 0)
                        npgs = node->npgs;
                    npgs = (pgoff_r + node->wl->r_factor > pgoff_w) ?
                                    pgoff_w - pgoff_r : node->wl->r_factor;

                    for (pg_i = 0; pg_i < npgs; pg_i++){
                        if(fox_read_vec_pg (node, nbuf, node->vblk_tgt.vblk->blks[0].g.blk,
                                                                    pgoff_r))
                            goto BREAK;
                        pgoff_r = (pgoff_r + node->wl->r_factor > pgoff_w) ?
                                                            0 : pgoff_r + 1;
                        aux_r += 1;
                    }
                }
            }

READ:
            /* 100 % reads */
            if (node->wl->w_factor == 0) {
                for (pg_i = 0; pg_i < node->npgs; pg_i++){
                    if(fox_read_vec_pg (node, nbuf, node->vblk_tgt.vblk->blks[0].g.blk,
                                                                    pg_i))
                        goto BREAK;
                }
            }
            if (node->wl->r_factor > 0) {
                for (blk_x = 0; blk_x < t_luns; blk_x++) {
                    fox_blkbuf_reset(node, &nbuf[blk_x]);
                }
            }
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

    for (blk_i = 0; blk_i < t_luns; blk_i++) {
        fox_free_blkbuf (&nbuf[blk_i], 1);
    }

    return 0;

OUT:
    return -1;
}

static void vec_exit (void)
{
    return;
}

static struct fox_engine vec_engine = {
    .id             = FOX_ENGINE_4,
    .name           = "vectored",
    .start          = vec_start,
    .exit           = vec_exit,
};

int foxeng_vec_init (struct fox_workload *wl)
{
    return fox_engine_register(&vec_engine);
}