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

static void fox_mio_fill_buf_rdm (char *wb, size_t sz)
{
    int i;
    uint8_t byte = 0xff;

    srand(time (NULL));
    for (i = 0; i < sz; i++) {
        byte = rand() % 255;
        memset (&wb[i], byte, 1);
    }
}

static void fox_mio_fill_buf(char *buf, struct fox_argp *argp, int npgs) {
    unsigned int written;
    int pg_lines, i, pg;
    char aux[9];
    char input_char;
    uint32_t pl_sz;
    uint32_t val[9] = {0,0,argp->io_ch,0,argp->io_lun,0,argp->io_blk,0,0};

    if (argp->io_random) {
        fox_mio_fill_buf_rdm (buf, geo->page_nbytes * geo->npages);
        return;
    }

    written = 0;
    pl_sz = geo->nplanes * geo->page_nbytes;
    pg_lines = pl_sz / 64;

    for (pg = argp->io_pg; pg < argp->io_pg + npgs; pg++) {
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

    vblk = nvm_vblk_alloc(dev, &addr, 1);
    if (!vblk)
        return ret;

    offset = argp->io_pg * vpg_sz;

    if (argp->cmdtype == CMDARG_WRITE)
        fox_mio_fill_buf(buf + offset, argp, argp->io_seq);

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
    uint32_t cblk;

    addr.ppa = 0x0;
    cblk = argp->io_blk;

    for (blk_i = 0; blk_i < argp->io_seq; blk_i++) {
        addr.g.ch = argp->io_ch;
        addr.g.lun = argp->io_lun;
        addr.g.blk = cblk;

        vblk = nvm_vblk_alloc(dev, &addr, 1);
        if (!vblk)
            return -1;

        if (prov_vblock_erase(vblk) < 0) {
            nvm_vblk_free(vblk);
            return -1;
        }

        nvm_vblk_free(vblk);
        cblk++;
    }

    return 0;
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