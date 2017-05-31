#include <liblightnvm.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>
#include <sys/stat.h>
#include "fox.h"

static struct nvm_dev *dev;
static const struct nvm_geo *geo;
static char *buf;

static int fox_mio_rw(struct fox_argp *argp) {
    int pg_i, ret = -1;
    struct nvm_vblk *vblk;
    struct nvm_addr addr;
    uint32_t offset;
    size_t bret, vpg_sz = geo->page_nbytes * geo->nplanes;

    addr.ppa = 0x0;
    addr.g.ch = argp->io_ch;
    addr.g.lun = argp->io_lun;
    addr.g.blk = argp->io_blk;
    addr.g.pg = argp->io_pg;

    vblk = nvm_vblk_alloc(dev, &addr, 1);
    if (!vblk)
        return ret;

    offset = argp->io_pg * vpg_sz;

    if (argp->cmdtype == CMDARG_WRITE) {
        if (argp->io_random)
            fox_wb_random ((uint8_t *)buf, geo->page_nbytes * geo->npages);
        else
            fox_wb_readable (buf + offset, argp->io_seq, geo, addr);
    }

    for (pg_i = 0; pg_i < argp->io_seq; pg_i++) {

        bret = (argp->cmdtype == CMDARG_WRITE) ?
                nvm_vblk_pwrite(vblk, buf + offset, vpg_sz, offset) :
                nvm_vblk_pread(vblk, buf + offset, vpg_sz, offset);

        if (bret != vpg_sz)
            goto FREE_VBLK;

        offset += vpg_sz;
    }

    ret = 0;

FREE_VBLK:
    nvm_vblk_free(vblk);
    return ret;
}

static int fox_mio_erase(struct fox_argp *argp) {
    int blk_i;
    struct nvm_vblk *vblk;
    struct nvm_addr addr;
    uint32_t cblk, pmode;

    addr.ppa = 0x0;
    cblk = argp->io_blk;

    for (blk_i = 0; blk_i < argp->io_seq; blk_i++) {
        addr.g.ch = argp->io_ch;
        addr.g.lun = argp->io_lun;
        addr.g.blk = cblk;

        vblk = nvm_vblk_alloc(dev, &addr, 1);
        if (!vblk)
            goto FAIL;

        pmode = nvm_dev_get_pmode(dev);
        if (nvm_dev_set_pmode(dev, 0x0) < 0)
            goto FREE_VBLK;

        if (nvm_vblk_erase(vblk) < 0)
            goto SET_PMODE;

        if (nvm_dev_set_pmode(dev, pmode) < 0)
            goto FREE_VBLK;

        nvm_vblk_free(vblk);
        cblk++;
    }

    return 0;

SET_PMODE:
    nvm_dev_set_pmode(dev, pmode);
FREE_VBLK:
    nvm_vblk_free(vblk);
FAIL:
    return -1;
}

static int fox_mio_check(struct fox_argp *argp) {
    if (argp->io_ch >= geo->nchannels ||
            argp->io_lun >= geo->nluns ||
            argp->io_blk >= geo->nblocks) {
        goto GEO;
    }

    if (argp->io_seq < 1)
        argp->io_seq = 1;

    if (argp->cmdtype == CMDARG_ERASE) {

        if (argp->io_blk + argp->io_seq > geo->nblocks)
            goto GEO;

    } else if (argp->cmdtype == CMDARG_WRITE || argp->cmdtype == CMDARG_READ) {

        if (argp->io_pg + argp->io_seq > geo->npages)
            goto GEO;

    } else {
        return -1;
    }

    return 0;

GEO:
    printf(" Invalid geometry.\n");
    return -1;
}


static void fox_mio_print_rw(struct fox_argp *argp) {
    FILE *fp = NULL;
    uint64_t usec;
    struct stat st = {0};
    struct timeval tv;
    char filename[80];
    int i, offset;
    uint32_t vpg_sz = geo->page_nbytes * geo->nplanes;
    uint32_t max_offset = (vpg_sz * argp->io_seq) + (vpg_sz * argp->io_pg);

    if (argp->io_out) {
        if (stat("output", &st) == -1)
            mkdir("output", S_IRWXO);

        gettimeofday(&tv, NULL);
        usec = tv.tv_sec * SEC64;
        usec += tv.tv_usec;

        if (argp->cmdtype == CMDARG_READ)
            sprintf(filename, "output/%lu_read-c%dl%db%dp%ds%d", usec,
            argp->io_ch, argp->io_lun, argp->io_blk, argp->io_pg, argp->io_seq);
        else
            sprintf(filename, "output/%lu_write-c%dl%db%dp%ds%d", usec,
            argp->io_ch, argp->io_lun, argp->io_blk, argp->io_pg, argp->io_seq);

        fp = fopen(filename, "a");
    }

    for (offset = vpg_sz * argp->io_pg; offset < max_offset; offset += vpg_sz) {

        if (argp->io_verb && argp->cmdtype == CMDARG_READ) {
            printf("\n =>Page %d\n", offset / vpg_sz);
            for (i = 0; i < 64 * 14; i++)
                printf("%c", buf[offset + i]);
            printf("...");
        }

        if (argp->io_out)
            fwrite(&buf[offset], vpg_sz, 1, fp);

    }

    if (argp->io_out)
        fclose (fp);

    if (argp->io_verb && argp->cmdtype == CMDARG_READ)
        printf("\n %d page(s) have been read.\n", argp->io_seq);
}

static void fox_mio_print(struct fox_argp *argp) {
    switch (argp->cmdtype) {
        case CMDARG_ERASE:
            printf(" %d block(s) have been erased.\n", argp->io_seq);
            break;
        case CMDARG_WRITE:
            if (argp->io_verb)
                printf(" %d page(s) have been programmed.\n", argp->io_seq);
        case CMDARG_READ:
            if (argp->io_out || argp->io_verb)
                fox_mio_print_rw(argp);
    }
}

int fox_mio_init(struct fox_argp *argp) {
    int ret = -1;

    if (argp->devname[0] == 0)
        memcpy(argp->devname, "/dev/nvme0n1", 13);

    dev = prov_dev_open(argp->devname);
    if (!dev) {
        printf(" Device not found.\n");
        return -1;
    }
    geo = prov_get_geo(dev);

    if (fox_mio_check(argp))
        goto CLOSE;

    buf = malloc(geo->page_nbytes * geo->nplanes * geo->npages);
    if (!buf)
        goto CLOSE;

    nvm_dev_set_meta_mode(dev, NVM_META_MODE_ALPHA);

    switch (argp->cmdtype) {
        case CMDARG_ERASE:
            ret = fox_mio_erase(argp);
            break;
        case CMDARG_WRITE:
        case CMDARG_READ:
            ret = fox_mio_rw(argp);
            break;
    }

    if (ret < 0)
        printf("An error has ocurred. Aborted.\n");
    else
        fox_mio_print(argp);

    free(buf);
CLOSE:
    prov_dev_close(dev);
    return ret;
}