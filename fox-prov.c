/*  - FOX - A tool for testing Open-Channel SSDs
 *
 * Copyright (C) 2017, IT University of Copenhagen. All rights reserved.
 * Written by Carla Villegas <carv@itu.dk>
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

/*
 * PROVISIONING INTERFACE
 * Wraps liblightnvm vblock IO interface, exposing basic read, write and erase
 * operations.
 * Implements vblock provisioning with get and put operations, keeping record
 * of free and used blocks.
 * Manages bad block table updates.
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <sys/queue.h>
#include <errno.h>
#include <time.h>
#include <pthread.h>
#include "fox.h"

static struct prov_v_dev virt_dev;

int prov_init(struct nvm_dev *dev, const struct nvm_geo *geo)
{
    int lun, err_lun;
    int nluns;
    int nblocks;

    srand(time(NULL));

    virt_dev.dev = dev;
    virt_dev.geo = geo;

    nluns = virt_dev.geo->nchannels * virt_dev.geo->nluns;
    nblocks = virt_dev.geo->nblocks;

    virt_dev.luns = malloc(nluns * sizeof(struct prov_lun));
    if (!virt_dev.luns)
        return -1;

    virt_dev.prov_vblks = malloc(nluns * sizeof(struct prov_vblk *));
    if (!virt_dev.prov_vblks)
        goto FREE_LUNS;

    for (lun = 0; lun < nluns; lun++) {
        virt_dev.prov_vblks[lun] = malloc(nblocks *
                                          sizeof(struct prov_vblk));

        if (!virt_dev.prov_vblks[lun]) {
            for (err_lun = 0; err_lun < lun; err_lun++)
                free(virt_dev.prov_vblks[err_lun]);
            goto FREE_VBLKS;
        }

        if (prov_vblk_list_create(lun) < 0) {
            for (err_lun = 0; err_lun < lun; err_lun++)
                prov_vblk_list_free(err_lun);
            goto FREE_VBLKS_LUN;
        }
    }

    return 0;

  FREE_VBLKS_LUN:
    for (err_lun = 0; err_lun < nluns; err_lun++)
        free(virt_dev.prov_vblks[err_lun]);

  FREE_VBLKS:
    free(virt_dev.prov_vblks);

  FREE_LUNS:
    free(virt_dev.luns);
    return -1;
}

int prov_exit(void)
{
    int lun;
    int nluns;

    nluns = virt_dev.geo->nchannels * virt_dev.geo->nluns;

    for (lun = 0; lun < nluns; lun++) {
        if (prov_vblk_list_free(lun))
            return -1;
        free(virt_dev.prov_vblks[lun]);
    }

    free(virt_dev.prov_vblks);
    free(virt_dev.luns);

    return 0;
}

int prov_vblk_list_create(int lun)
{
    int blk;
    int nblk;
    struct nvm_addr addr;
    const struct nvm_bbt *bbt;
    struct nvm_ret ret;

    addr.ppa = 0x0;
    addr.g.ch = lun / virt_dev.geo->nluns;
    addr.g.lun = lun % virt_dev.geo->nluns;
    virt_dev.luns[lun].addr = addr;
    nblk = virt_dev.geo->nblocks;

    bbt = prov_get_bbt(virt_dev.dev, addr, &ret);
    if (!bbt)
        return -1;

    virt_dev.luns[lun].nfree_blks = 0;
    virt_dev.luns[lun].nused_blks = 0;
    CIRCLEQ_INIT(&(virt_dev.luns[lun].free_blk_head));
    CIRCLEQ_INIT(&(virt_dev.luns[lun].used_blk_head));
    pthread_mutex_init(&(virt_dev.luns[lun].l_mutex), NULL);

    for (blk = 0; blk < nblk; blk++) {
        prov_vblk_alloc(bbt, lun, blk);
    }

    return 0;
}

int prov_vblk_list_free(int lun)
{
    int nblk;
    int blk;
    struct prov_vblk *vblk, *tmp;

    nblk = virt_dev.geo->nblocks;

    if (virt_dev.luns[lun].nfree_blks > 0) {
        vblk = CIRCLEQ_FIRST(&(virt_dev.luns[lun].free_blk_head));

        for (blk = 0; blk < virt_dev.luns[lun].nfree_blks; blk++) {
            tmp = CIRCLEQ_NEXT(vblk, entry);
            CIRCLEQ_REMOVE(&(virt_dev.luns[lun].free_blk_head),
                           vblk, entry);
            vblk = tmp;
        }
    }

    if (virt_dev.luns[lun].nused_blks > 0) {
        vblk = CIRCLEQ_FIRST(&(virt_dev.luns[lun].used_blk_head));

        for (blk = 0; blk < virt_dev.luns[lun].nused_blks; blk++) {
            tmp = CIRCLEQ_NEXT(vblk, entry);
            CIRCLEQ_REMOVE(&(virt_dev.luns[lun].used_blk_head),
                           vblk, entry);
            vblk = tmp;
        }
    }

    for (blk = 0; blk < nblk; blk++) {
        prov_vblk_free(lun, blk);
    }

    pthread_mutex_destroy(&(virt_dev.luns[lun].l_mutex));

    return 0;
}

int prov_vblk_alloc(const struct nvm_bbt *bbt, int lun, int blk)
{
    int pl;
    int bad_blk = 0;
    struct prov_vblk *vblk = &(virt_dev.prov_vblks[lun][blk]);

    vblk->state = malloc(8 * virt_dev.geo->nplanes);
    if (vblk->state == NULL)
        return -1;

    vblk->addr = virt_dev.luns[lun].addr;
    vblk->addr.g.blk = blk;

    for (pl = 0; pl < virt_dev.geo->nplanes; pl++) {
        vblk->state[pl] = bbt->blks[virt_dev.geo->nplanes * blk + pl];
        bad_blk += vblk->state[pl];
    }

    if (!bad_blk) {
        struct prov_vblk *rnd_vblk = prov_vblk_rand(lun);

        if (rnd_vblk == NULL) {
            CIRCLEQ_INSERT_HEAD(&(virt_dev.luns[lun].free_blk_head),
                                vblk, entry);
        } else {

            if (rand() % 2)
                CIRCLEQ_INSERT_BEFORE(&(virt_dev.luns[lun].free_blk_head),
                                                rnd_vblk, vblk, entry);
            else
                CIRCLEQ_INSERT_AFTER(&(virt_dev.luns[lun].free_blk_head),
                                                rnd_vblk, vblk, entry);
        }
        virt_dev.luns[lun].nfree_blks++;
    }
    return 0;
}

int prov_vblk_free(int lun, int blk)
{
    free(virt_dev.prov_vblks[lun][blk].state);
    return 0;
}

struct prov_vblk *prov_vblk_rand(int lun)
{
    int blk, blk_idx;
    struct prov_vblk *vblk, *tmp;

    if (virt_dev.luns[lun].nfree_blks > 0) {
        blk_idx = rand() % virt_dev.luns[lun].nfree_blks;
        vblk = CIRCLEQ_FIRST(&(virt_dev.luns[lun].free_blk_head));

        for (blk = 0; blk < blk_idx; blk++) {
            tmp = CIRCLEQ_NEXT(vblk, entry);
            vblk = tmp;
        }

        return vblk;
    }
    return NULL;
}

struct nvm_dev *prov_dev_open(const char *dev_path)
{
    return nvm_dev_open(dev_path);
}

void prov_dev_close(struct nvm_dev *dev)
{
    return nvm_dev_close(dev);
}

const struct nvm_geo *prov_get_geo(struct nvm_dev *dev)
{
    return nvm_dev_get_geo(dev);
}

const struct nvm_bbt *prov_get_bbt(struct nvm_dev *dev,
                                   struct nvm_addr addr,
                                   struct nvm_ret *ret)
{
    return nvm_bbt_get(dev, addr, ret);
}

ssize_t prov_vblk_pread(struct nvm_vblk * vblk, void *buf, size_t count,
                        size_t offset)
{
    ssize_t nbytes = nvm_vblk_pread(vblk, buf, count, offset);

    return nbytes;
}

ssize_t prov_vblk_pwrite(struct nvm_vblk * vblk, const void *buf,
                         size_t count, size_t offset)
{
    ssize_t nbytes = nvm_vblk_pwrite(vblk, buf, count, offset);

    return nbytes;
}

ssize_t prov_vblk_erase(struct nvm_vblk * vblk)
{
    int pmode;
    int err;

    pmode = nvm_dev_get_pmode(virt_dev.dev);
    if (nvm_dev_set_pmode(virt_dev.dev, 0x0) < 0)
        goto FAIL;

    err = nvm_vblk_erase(vblk);
    if (err < 0) {
        goto FAIL;
    }

    if (nvm_dev_set_pmode(virt_dev.dev, pmode) < 0)
        goto FAIL;

    return err;

  FAIL:
    return -1;
}

int prov_bbt_mark(struct prov_vblk *vblk){

    int lun, blk, pl;
    struct nvm_ret ret;

    nvm_bbt_mark(vblk->blk->dev, vblk->blk->blks, 1, 1, &ret);
    lun = vblk->addr.g.ch * virt_dev.geo->nluns + vblk->addr.g.lun;
    blk = vblk->addr.g.blk;

    for (pl = 0; pl < virt_dev.geo->nplanes; pl++)
        virt_dev.prov_vblks[lun][blk].state[pl] = 1;

    return 0;
}

struct nvm_vblk *prov_vblk_get(int ch, int l)
{
    int lun;

    lun = ch * virt_dev.geo->nluns + l;

    struct prov_lun *p_lun = &virt_dev.luns[lun];

    if (p_lun->nfree_blks > 0) {

        struct prov_vblk *vblk = CIRCLEQ_FIRST(&p_lun->free_blk_head);

        pthread_mutex_lock(&(p_lun->l_mutex));

        CIRCLEQ_REMOVE(&(p_lun->free_blk_head), vblk, entry);
        CIRCLEQ_INSERT_TAIL(&(p_lun->used_blk_head), vblk, entry);

        p_lun->nfree_blks--;
        p_lun->nused_blks++;

        pthread_mutex_unlock(&(p_lun->l_mutex));

        vblk->blk = nvm_vblk_alloc(virt_dev.dev, &vblk->addr, 1);
        if (vblk->blk == NULL)
            goto FAIL;

        if (prov_vblk_erase(vblk->blk) < 0) {
            prov_bbt_mark(vblk);
            nvm_vblk_free(vblk->blk);
            goto FAIL;
        }

        return vblk->blk;
    }
  FAIL:
    return NULL;
}

int prov_vblk_put(struct nvm_vblk *vblk)
{
    int ch, l, blk;
    int lun;

    ch = vblk->blks[0].g.ch;
    l = vblk->blks[0].g.lun;
    blk = vblk->blks[0].g.blk;

    lun = ch * virt_dev.geo->nluns + l;
    struct prov_lun *p_lun = &virt_dev.luns[lun];

    nvm_vblk_free(vblk);

    pthread_mutex_lock(&(p_lun->l_mutex));
    CIRCLEQ_REMOVE(&(p_lun->used_blk_head),
                   &virt_dev.prov_vblks[lun][blk], entry);
    CIRCLEQ_INSERT_TAIL(&(p_lun->free_blk_head),
                        &virt_dev.prov_vblks[lun][blk], entry);
    p_lun->nfree_blks++;
    p_lun->nused_blks--;
    pthread_mutex_unlock(&(p_lun->l_mutex));

    return 0;
}

void prov_fblk_pr(int lun) {
    struct prov_vblk *vblk, *tmp;
    int blk;
    int nblks;

    nblks = virt_dev.luns[lun].nfree_blks;
    if(nblks) {
        vblk = CIRCLEQ_FIRST(&(virt_dev.luns[lun].free_blk_head));
        for (blk = 0; blk < nblks; blk++) {
            tmp = CIRCLEQ_NEXT(vblk, entry);
            nvm_addr_pr(vblk->addr);
            vblk = tmp;
        }
    }
}

void prov_ublk_pr(int lun) {
    struct prov_vblk *vblk, *tmp;
    int blk;
    int nblks;

    nblks = virt_dev.luns[lun].nused_blks;
    if(nblks) {
        vblk = CIRCLEQ_FIRST(&(virt_dev.luns[lun].used_blk_head));
        for (blk = 0; blk < nblks; blk++) {
            tmp = CIRCLEQ_NEXT(vblk, entry);
            nvm_addr_pr(vblk->addr);
            vblk = tmp;
        }
    }
}

void prov_dev_pr()
{
    int lun;
    int nluns;

    nluns = virt_dev.geo->nchannels * virt_dev.geo->nluns;
    printf("Total luns: %d\n---------\n", nluns);

    for (lun = 0; lun < nluns; lun++) {
        printf("LUN %d : %d free, %d used.\n", lun,
               virt_dev.luns[lun].nfree_blks,
               virt_dev.luns[lun].nused_blks);
        printf("-FREE BLOCKS-\n");
        prov_fblk_pr(lun);
        printf("-USED BLOCKS-\n");
        prov_ublk_pr(lun);
    }
    printf("---------\n");
}
