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

    node->vblk_tgt = wl->vblks[boff];

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
                                                       wl->geo.vpg_nbytes * i))
            printf ("WARNING: error when writing to vblk page.\n");
    }
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
        printf ("\r - Preparing blocks... [%d/%d]", blk_i, t_blks);
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
