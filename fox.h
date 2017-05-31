/*  - FOX - A tool for testing Open-Channel SSDs
 *
 * Copyright (C) 2016, IT University of Copenhagen. All rights reserved.
 * Written by Ivan Luiz Picoli <ivpi@itu.dk>
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

#include <sys/queue.h>
#include <stdint.h>
#include <liblightnvm.h>
#include <sys/time.h>

#ifndef FOX_H
#define FOX_H

#define AND64 0xffffffffffffffff
#define SEC64 (1000000 & AND64)

#define FOX_ENGINE_1  0x1 /* All sequential */
#define FOX_ENGINE_2  0x2 /* All round-robin */
#define FOX_ENGINE_3  0x3 /* I/O Isolation */

#define PROV_NBLK_PER_VBLK 0x1

enum {
    FOX_STATS_ERASE_T = 0x1,
    FOX_STATS_READ_T,
    FOX_STATS_WRITE_T,
    FOX_STATS_RUNTIME,
    FOX_STATS_RW_SECT,
    FOX_STATS_ERASED_BLK,
    FOX_STATS_PGS_W,
    FOX_STATS_PGS_R,
    FOX_STATS_BREAD,
    FOX_STATS_BWRITTEN,
    FOX_STATS_BRW_SEC,
    FOX_STATS_IOPS,
    FOX_STATS_FAIL_CMP,
    FOX_STATS_FAIL_E,
    FOX_STATS_FAIL_R,
    FOX_STATS_FAIL_W
};

#define FOX_FLAG_READY      (1 << 0)
#define FOX_FLAG_DONE       (1 << 1)
#define FOX_FLAG_MONITOR    (1 << 2)

#define CMDARG_LEN          32
#define CMDARG_FLAG_D       (1 << 0)
#define CMDARG_FLAG_T       (1 << 1)
#define CMDARG_FLAG_C       (1 << 2)
#define CMDARG_FLAG_L       (1 << 3)
#define CMDARG_FLAG_B       (1 << 4)
#define CMDARG_FLAG_P       (1 << 5)
#define CMDARG_FLAG_J       (1 << 6)
#define CMDARG_FLAG_W       (1 << 7)
#define CMDARG_FLAG_R       (1 << 8)
#define CMDARG_FLAG_V       (1 << 9)
#define CMDARG_FLAG_S       (1 << 10)
#define CMDARG_FLAG_M       (1 << 11)
#define CMDARG_FLAG_O       (1 << 12)
#define CMDARG_FLAG_E       (1 << 13)

#define FOX_RUN_MODE         0x0
#define FOX_IO_MODE          0x1

#define WB_GEO_FILL         0x1
#define WB_GEO_CMP          0x2

enum {
    WB_DISABLE  = 0x0,
    WB_RANDOM   = 0x1,
    WB_READABLE = 0x2,
    WB_GEOMETRY = 0x3
};

enum cmdtypes {
    CMDARG_RUN      = 1,
    CMDARG_ERASE    = 2,
    CMDARG_WRITE    = 3,
    CMDARG_READ     = 4
};

struct fox_argp
{
    /* GLOBAL */
    int         cmdtype;
    int         arg_num;
    uint32_t    arg_flag;
    char        devname[CMDARG_LEN];

    /* run parameters */
    uint64_t    runtime;
    uint8_t     channels;
    uint8_t     luns;
    uint32_t    blks;
    uint32_t    pgs;
    uint8_t     nthreads;
    uint16_t    w_factor;
    uint16_t    r_factor;
    uint16_t    vector;
    uint32_t    max_delay;
    uint8_t     memcmp;
    uint8_t     output;
    uint32_t    engine;

    /* r/w/e parameters */
    uint8_t     io_ch;
    uint8_t     io_lun;
    uint32_t    io_blk;
    uint32_t    io_pg;
    uint32_t    io_seq;
    uint8_t     io_random;
    uint8_t     io_verb;
    uint8_t     io_out;
};

struct fox_node;

typedef int  (fengine_start)(struct fox_node *);
typedef void (fengine_exit)(void);

struct nvm_vblk {
    struct nvm_dev  *dev;
    struct nvm_addr blks[128];
    int             nblks;
    size_t          nbytes;
    size_t          pos_write;
    size_t          pos_read;
    int             nthreads;
};

struct fox_engine {
    uint16_t                id;
    char                    *name;
    fengine_start           *start;
    fengine_exit            *exit;
    LIST_ENTRY(fox_engine)  entry;
};

struct fox_stats {
    struct timeval  tval;
    struct timeval  tval_tmp;
    uint64_t        runtime;
    uint64_t        rw_sect; /* r/w time in the last second */
    uint64_t        read_t;
    uint64_t        write_t;
    uint64_t        erase_t;
    uint32_t        erased_blks;
    uint32_t        pgs_r;
    uint32_t        pgs_w;
    uint32_t        io_count;
    uint64_t        bread;
    uint64_t        bwritten;
    uint64_t        brw_sec; /* transferred bytes in the last second */
    uint32_t        iops;
    uint16_t        progress;
    uint32_t        pgs_done;
    uint32_t        fail_cmp;
    uint32_t        fail_e;
    uint32_t        fail_w;
    uint32_t        fail_r;
    uint8_t         flags;
    pthread_mutex_t s_mutex;
};

struct fox_workload {
    char                    *devname;
    uint8_t                 channels;
    uint8_t                 luns;
    uint32_t                blks;
    uint32_t                pgs;
    uint8_t                 nthreads;
    uint16_t                w_factor;
    uint16_t                r_factor;
    uint16_t                nppas;
    uint32_t                max_delay;
    uint8_t                 memcmp;
    uint8_t                 output;
    uint64_t                runtime; /* seconds */
    struct fox_engine       *engine;
    struct nvm_dev          *dev;
    const struct nvm_geo    *geo;
    struct nvm_vblk         **vblks;
    struct fox_stats        *stats;
    pthread_mutex_t         start_mut;
    pthread_cond_t          start_con;
    pthread_mutex_t         monitor_mut;
    pthread_cond_t          monitor_con;
};

struct fox_blkbuf {
    uint8_t     *buf_w;
    uint8_t     *buf_r;
};

struct fox_tgt_blk {
    struct nvm_vblk    *vblk;
    uint16_t           ch;
    uint16_t           lun;
    uint32_t           blk;
};

struct fox_node {
    uint8_t             nid;
    uint8_t             nchs;
    uint8_t             nluns;
    uint32_t            nblks;
    uint32_t            npgs;
    uint8_t             *ch;
    uint8_t             *lun;
    uint8_t             *blk;
    uint32_t            delay;
    pthread_t           tid;
    struct fox_workload *wl;
    struct fox_stats    stats;
    struct fox_tgt_blk  vblk_tgt;
    struct fox_engine   *engine;
    LIST_ENTRY(fox_node) entry;
};

struct fox_output_row_rt {
    uint64_t    timestp;
    uint16_t    nid;
    long double thpt;
    long double iops;
    struct fox_stats                stats;
    TAILQ_ENTRY(fox_output_row_rt)  entry;
};

struct fox_output_row {
    uint64_t    seq;
    uint64_t    node_seq;
    uint16_t    tid;
    uint16_t    ch;
    uint16_t    lun;
    uint32_t    blk;
    uint32_t    pg;
    uint64_t    tstart;
    uint64_t    tend;
    uint32_t    ulat;
    char        type;
    uint8_t     failed;
    uint8_t     datacmp;
    uint32_t    size;
    TAILQ_ENTRY(fox_output_row) entry;
};

/* Provisioning */

struct prov_vblk{
    struct nvm_addr         addr;
    struct nvm_vblk         *blk;
    uint8_t                 *state;
    CIRCLEQ_ENTRY(prov_vblk)   entry;
};

struct prov_lun {
    struct nvm_addr         addr;
    uint32_t                nfree_blks;
    uint32_t                nused_blks;
    pthread_mutex_t         l_mutex;
    CIRCLEQ_HEAD(free_blk_list, prov_vblk) free_blk_head;
    CIRCLEQ_HEAD(used_blk_list, prov_vblk) used_blk_head;
};

struct prov_v_dev {
    struct nvm_dev          *dev;
    const struct nvm_geo    *geo;
    struct prov_lun         *luns;
    struct prov_vblk        **prov_vblks;
};

/* End Provisioning */

#define FOX_READ    0x1
#define FOX_WRITE   0x2

/* A workload is a set of parameters that defines the experiment behavior.
 * Check 'struct fox_workload'
 *
 * A node is a thread that carries a distribution and performs iterations.
 *
 * A distribution is a set of blocks distributed among the units of parallelism
 * (channels and LUNs) assigned to a node.
 *
 * An iteration is a group of r/w operations in a given distribution.
 * The iteration is finished when all the pages in the distribution are
 * programmed. In a 100% read workload, the iteration finishes when all the
 * pages have been read.
 *
 * A row is a group of pages where each page is located in a different unit
 * of parallelism. Each row is composed by n pages, where n is the maximum
 * available units given to a specific node.
 *
 * A column is the page offset within a row. For instance:
 *
 * (CH=2,LUN=2,BLK=nb,PG=np)
 *
 *             col      col      col      col
 * row ->   (0,0,0,0)(0,1,0,0)(1,0,0,0)(1,1,0,0)
 * row ->   (0,0,0,1)(0,1,0,1)(1,0,0,1)(1,1,0,1)
 * row ->   (0,0,0,2)(0,1,0,2)(1,0,0,2)(1,1,0,2)
 *                          ...
 * row ->   (0,0,nb,np)(0,1,nb,np)(1,0,nb,np)(1,1,nb,np)
 *
 * This struct is used to keep the current read/write pointers in the iteration.
 */
struct fox_rw_iterator {
    uint32_t    rows;       /* Total rows in the iteration */
    uint32_t    cols;       /* Total columns in a row */
    uint32_t    row_r;      /* Current row for read operation */
    uint32_t    row_w;      /* Current row for write operation */
    uint32_t    col_r;      /* Current page offset within the row for read */
    uint32_t    col_w;      /* Current page offset within the row for write */
    uint32_t    it_count;   /* Number of completed iterations */
};

/* fox-core and threads */
int              fox_argp_init (int, char **, struct fox_argp *);
struct fox_node *fox_create_threads (struct fox_workload *);
void             fox_exit_threads (struct fox_node *);
void             fox_merge_stats (struct fox_node *, struct fox_stats *);
void             fox_monitor (struct fox_node *);
void             fox_set_stats (uint8_t, struct fox_stats *, int64_t);
void             fox_start_node (struct fox_node *);
void             fox_end_node (struct fox_node *);
void             fox_timestamp_start (struct fox_stats *);
uint64_t         fox_timestamp_tmp_start (struct fox_stats *);
uint64_t         fox_timestamp_end (uint8_t, struct fox_stats *);
void             fox_show_stats (struct fox_workload *, struct fox_node *);
void             fox_show_workload (struct fox_workload *);
void             fox_set_progress (struct fox_stats *, uint16_t);
int              fox_init_stats (struct fox_stats *);
void             fox_exit_stats (struct fox_stats *);
void             fox_wait_for_ready (struct fox_workload *);
void             fox_wait_for_monitor (struct fox_workload *);
int              fox_mio_init (struct fox_argp *);

/* fox-vblk */
int              fox_alloc_vblks (struct fox_workload *);
void             fox_free_vblks (struct fox_workload *);
int              fox_vblk_tgt (struct fox_node *, uint16_t, uint16_t, uint32_t);
uint32_t         fox_vblk_get_pblk (struct fox_workload *, uint16_t,
                                                            uint16_t, uint32_t);

/* fox-buf */
int              fox_alloc_blk_buf (struct fox_node *, struct fox_blkbuf *);
void 		 fox_wb_random (uint8_t *, size_t);
int              fox_wb_geo (uint8_t *, size_t, const struct nvm_geo *,
                                                      struct nvm_addr, uint8_t);
void             fox_wb_readable(char *, int, const struct nvm_geo *,
                                                               struct nvm_addr);
void             fox_blkbuf_reset (struct fox_node *, struct fox_blkbuf *);
void             fox_free_blkbuf (struct fox_blkbuf *, int);
int              fox_blkbuf_cmp (struct fox_node *, struct fox_blkbuf *,
                                         uint16_t, uint16_t, struct nvm_vblk *);

/* fox-output */
int              fox_output_init (struct fox_workload *);
void             fox_output_exit (void);
void             fox_output_append (struct fox_output_row *, int);
void             fox_output_append_rt(struct fox_output_row_rt *, uint16_t);
void             fox_output_flush (void);
void             fox_output_flush_rt (void);
void             fox_print (char *, uint8_t);
void             fox_flush_corruption (char *, void *, void *, size_t);
struct fox_output_row       *fox_output_new (void);
struct fox_output_row_rt    *fox_output_new_rt (void);

/* fox-rw */
void   fox_iterator_reset (struct fox_rw_iterator *);
int    fox_iterator_prior (struct fox_rw_iterator *, uint8_t);
int    fox_iterator_next (struct fox_rw_iterator *, uint8_t);
void   fox_iterator_free (struct fox_rw_iterator *);
struct fox_rw_iterator *fox_iterator_new (struct fox_node *);
int    fox_erase_all_vblks (struct fox_node *);
int    fox_erase_blk (struct fox_tgt_blk *, struct fox_node *);
int    fox_read_blk (struct fox_tgt_blk *, struct fox_node *,
                                      struct fox_blkbuf *, uint16_t, uint16_t);
int    fox_write_blk (struct fox_tgt_blk *, struct fox_node *,
                                      struct fox_blkbuf *, uint16_t, uint16_t);
int    fox_update_runtime (struct fox_node *);
double fox_check_progress_runtime (struct fox_node *);
double fox_check_progress_pgs (struct fox_node *);

/* engines */
int                  fox_engine_register (struct fox_engine *);
struct fox_engine   *fox_get_engine(uint16_t);
int                  foxeng_seq_init (struct fox_workload *);
int                  foxeng_rr_init (struct fox_workload *);
int                  foxeng_iso_init (struct fox_workload *);

/* provisioning */
int     prov_init(struct nvm_dev *dev, const struct nvm_geo *geo);
int     prov_exit (void);
int 	prov_vblk_list_create(int lun);
int 	prov_vblk_list_free(int lun);
int 	prov_vblk_alloc(const struct nvm_bbt *bbt, int lun, int blk);
int 	prov_vblk_free(int lun, int blk);

struct prov_vblk *prov_vblk_rand(int lun);
struct nvm_dev   *prov_dev_open(const char *dev_path);
void    	  prov_dev_close(struct nvm_dev *dev);

const struct nvm_geo *prov_get_geo(struct nvm_dev *dev);
const struct nvm_bbt *prov_get_bbt(struct nvm_dev *dev,
                                    struct nvm_addr addr, struct nvm_ret *ret);
ssize_t prov_vblk_pread(struct nvm_vblk *vblk, void *buf, size_t count,
                                                                size_t offset);
ssize_t prov_vblk_pwrite(struct nvm_vblk *vblk, const void *buf,
                                                  size_t count, size_t offset);
ssize_t prov_vblk_erase(struct nvm_vblk *vblk);

struct nvm_vblk	*prov_vblk_get(int ch, int lun);
int    	prov_vblk_put(struct nvm_vblk *vblk);
void 	prov_dev_pr();
void 	prov_ublk_pr(int lun);
void 	prov_fblk_pr(int lun);
#endif /* FOX_H */