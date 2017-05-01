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

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <sys/queue.h>
#include <errno.h>
#include <time.h>
#include <pthread.h>
#include "fox.h"

struct prov_v_dev virt_dev;

struct nvm_vblk *prov_alloc_vblk(struct nvm_dev *dev, struct nvm_addr *addr)
{
    struct nvm_vblk *vblk;
    const struct nvm_geo *geo;
    int errno;

    geo = virt_dev.geo;
    vblk = malloc(sizeof(struct nvm_vblk));
    if (!vblk) {
        errno = ENOMEM;
	return NULL;
    }
    vblk->nblks = PROV_NBLK_PER_VBLK;
    vblk->blks[0]= *addr;
    vblk->dev = dev;
    vblk->pos_write = 0;
    vblk->pos_read = 0;
    vblk->nbytes = geo->page_nbytes * geo->npages * geo->nplanes;
    return vblk;
}

struct prov_free_blk *prov_init_fblk(int ch, int lun, int blk)
{
    struct prov_free_blk *fblk = malloc(sizeof(struct prov_free_blk));
    fblk->addr = malloc(sizeof(struct nvm_addr));
    fblk->addr->g.ch = ch;
    fblk->addr->g.lun = lun;
    fblk->addr->g.blk = blk;
    fblk->blk = prov_alloc_vblk(virt_dev.dev, fblk->addr);
    return fblk;
}

int prov_get_free_from_bbt(const struct nvm_bbt *bbt, struct prov_nvm_lun *lun)
{
    int blk, pl;
    int idx;
    int ch, l;
    int bad_blk;
    int nplanes = virt_dev.geo->nplanes;
    int64_t nblks = bbt->nblks;

    lun->nfree_blks = 0;
    idx = 0;
    ch = lun->addr->g.ch;
    l = lun->addr->g.lun;
    for (blk=0; blk < nblks; blk+=nplanes){
        bad_blk = 0;
        for (pl = 0; pl<nplanes; pl++)
            bad_blk += bbt->blks[blk + pl];
        
        if (!bad_blk){
            lun->index[idx] = prov_init_fblk(ch, l, blk/nplanes);
            lun->nfree_blks++;
            LIST_INSERT_HEAD(&lun->free_blk_head, lun->index[idx], entry);
            idx++;
        } else {
            lun->index[idx] = NULL;
        }
    }
    return 0;
}

struct prov_nvm_lun prov_init_lun(int lun_idx)
{
    struct prov_nvm_lun lun;
    lun.addr = malloc(sizeof(struct nvm_addr));
    lun.addr->ppa = 0;
    lun.addr->g.ch = lun_idx / virt_dev.geo->nluns;
    lun.addr->g.lun = lun_idx % virt_dev.geo->nluns;
    lun.index = malloc(virt_dev.geo->nblocks * sizeof (struct prov_free_blk *));
    pthread_mutex_init (&(lun.l_mutex), NULL);
    return lun;
}

int prov_init_v_dev(struct nvm_dev *dev, const struct nvm_geo *geo)
{
    int l;
    virt_dev.dev = dev;
    virt_dev.geo = geo;
    int total_luns = virt_dev.geo->nchannels * virt_dev.geo->nluns;
    virt_dev.free_blks = malloc(total_luns * sizeof(struct prov_nvm_lun));
    for (l=0; l<total_luns; l++)
        virt_dev.free_blks[l] = prov_init_lun(l);
        
    return 0;
}

void prov_exit_fblk_lun(struct prov_nvm_lun *lun)
{
    uint32_t i;
    pthread_mutex_lock(&lun->l_mutex);
    for (i=0; i<lun->nfree_blks; i++){
        LIST_REMOVE(lun->index[i], entry);
        free(lun->index[i]);
    }
}

int prov_exit_fblk_list()
{
    int total_luns, l;
    total_luns = virt_dev.geo->nchannels * virt_dev.geo->nluns;
    for (l=0; l<total_luns; l++){
        prov_exit_fblk_lun(&virt_dev.free_blks[l]);
        free(virt_dev.free_blks[l].index);
        pthread_mutex_destroy (&(virt_dev.free_blks[l].l_mutex));
    }
    free(virt_dev.free_blks);
    return 0;
}

int prov_init_fblk_list(struct nvm_dev *dev, const struct nvm_geo *geo)
{
    size_t ch, l;
    size_t nchannels, nluns;

    srand(time (NULL));
    prov_init_v_dev(dev, geo);
    nchannels = virt_dev.geo->nchannels;
    nluns = virt_dev.geo->nluns;

    for (ch=0; ch<nchannels; ch++)
        for (l=0; l<nluns; l++)
	    prov_gen_list_per_lun(ch, l);
    
    return 0;
}

int prov_gen_list_per_lun(int ch, int l)
{
    int curr_lun;
    const struct nvm_bbt *bbt;
    struct nvm_ret ret;
    struct nvm_addr *addr = malloc(sizeof(struct nvm_addr));
    curr_lun = ch * virt_dev.geo->nluns + l;
    addr->ppa = 0;
    addr->g.ch = ch;
    addr->g.lun = l;
    virt_dev.free_blks[curr_lun].addr = addr;
    bbt = prov_get_bbt(virt_dev.dev, *addr, &ret);
    if (!bbt)
        return -1;
    LIST_INIT(&(virt_dev.free_blks[curr_lun].free_blk_head));
    prov_get_free_from_bbt(bbt, &virt_dev.free_blks[curr_lun]);
    free(addr);
    return 0;
}

int prov_update_fblk_list(struct prov_nvm_lun *lun, uint32_t blk_idx)
{
    pthread_mutex_lock(&(lun->l_mutex));
    LIST_REMOVE(lun->index[blk_idx], entry);
    free(lun->index[blk_idx]);
    lun->index[blk_idx] = lun->index[lun->nfree_blks-1];
    lun->index[lun->nfree_blks-1] = NULL;
    lun->nfree_blks--;
    pthread_mutex_unlock(&(lun->l_mutex));
    return 0;
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
                                   struct nvm_addr addr, struct nvm_ret *ret)
{
    return nvm_bbt_get(dev, addr, ret);
}

int prov_get_vblock(size_t ch, size_t lun, struct nvm_vblk *vblk)
{
    size_t curr_lun = ch * virt_dev.geo->nluns + lun;
    ssize_t ret;
    int blk_idx;

    if (virt_dev.free_blks[curr_lun].nfree_blks > 0){
        blk_idx = rand() % virt_dev.free_blks[curr_lun].nfree_blks;
        if (virt_dev.free_blks[curr_lun].index[blk_idx] == NULL)      
            return -1;
            
        *vblk = *virt_dev.free_blks[curr_lun].index[blk_idx]->blk;
        ret = prov_vblock_erase(vblk);
        if (ret<0)
            return -1;
            
        prov_update_fblk_list(&virt_dev.free_blks[curr_lun], blk_idx);
        return 0;
    }
    return -1;
}
    
int prov_put_vblock(struct nvm_vblk *vblk)
{
    size_t max_blocks;
    uint64_t ch = vblk->blks[0].g.ch;
    uint64_t l = vblk->blks[0].g.lun;
    size_t curr_lun = ch * virt_dev.geo->nluns + l;
    struct prov_nvm_lun lun = virt_dev.free_blks[curr_lun];
    struct prov_free_blk *fblk = malloc(sizeof(struct prov_free_blk));
    
    max_blocks = virt_dev.geo->nblocks;
    fblk->blk = vblk;
    fblk->addr = &vblk->blks[0];
    if(lun.nfree_blks <max_blocks && lun.index[lun.nfree_blks]==NULL){
        lun.index[lun.nfree_blks] = fblk;
        lun.nfree_blks++;
        LIST_INSERT_HEAD(&lun.free_blk_head, fblk, entry);
        virt_dev.free_blks[curr_lun] = lun;
        return 0;
    }
    free(fblk);
    return -1;  
}

ssize_t prov_vblock_pread(struct nvm_vblk *vblk, void *buf, size_t count,
                                                                size_t offset)
{
    ssize_t nbytes = nvm_vblk_pread(vblk, buf, count, offset);
    return nbytes;
}
    
ssize_t prov_vblock_pwrite(struct nvm_vblk *vblk, const void *buf, 
                                                  size_t count, size_t offset)
{
    ssize_t nbytes = nvm_vblk_pwrite(vblk, buf, count, offset);
    
    return nbytes;
}

ssize_t prov_vblock_erase(struct nvm_vblk *vblk)
{
    int err;
    struct nvm_ret ret;
    err = nvm_vblk_erase(vblk);
    if (err<0)
        nvm_bbt_mark(vblk->dev, vblk->blks, 1, 1, &ret);
        
    return err;
}

void prov_fblk_pr()
{
    int l;
    int total_luns = virt_dev.geo->nchannels * virt_dev.geo->nluns;
    printf("n_luns: %d {\n",total_luns);        
    for (l=0; l< total_luns; l++){
        printf("LUN: %d ", l);
	prov_lun_pr(virt_dev.free_blks[l]);
    }
    printf("}\n");
}

void prov_lun_pr(struct prov_nvm_lun lun)
{
    size_t blk;
    printf("ppa: CH:%d, LUN:%d: {", lun.addr->g.ch, lun.addr->g.lun);
    printf("free blocks: %u ", lun.nfree_blks);
    for (blk=0; blk<lun.nfree_blks; blk++){
        printf("idx %lu: ", blk);
        nvm_addr_pr(*(lun.index[blk]->addr));
    }
    printf("}\n");
}

void prov_dev_pr()
{
    nvm_dev_pr(virt_dev.dev);
}

int prov_init(struct nvm_dev *dev, const struct nvm_geo *geo)
{
    return prov_init_fblk_list(dev, geo);
}

int prov_exit(void)
{
    return prov_exit_fblk_list();
}
