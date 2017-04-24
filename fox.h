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

struct fox_node;

typedef int  (fengine_start)(struct fox_node *);
typedef void (fengine_exit)(struct fox_node *);

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
    char                *devname;
    uint8_t             channels;
    uint8_t             luns;
    uint32_t            blks;
    uint32_t            pgs;
    uint8_t             nthreads;
    uint16_t            w_factor;
    uint16_t            r_factor;
    uint16_t            nppas;
    uint32_t            max_delay;
    uint8_t             memcmp;
    uint8_t             output;
    uint64_t            runtime; /* u-seconds */
    struct fox_engine   *engine;
    NVM_DEV             dev;
    NVM_GEO             geo;
    NVM_VBLK            *vblks;
    struct fox_stats    *stats;
    pthread_mutex_t     start_mut;
    pthread_cond_t      start_con;
    pthread_mutex_t     monitor_mut;
    pthread_cond_t      monitor_con;
};

struct fox_blkbuf {
    uint8_t     *buf_w;
    uint8_t     *buf_r;
};

struct fox_tgt_blk {
    NVM_VBLK    vblk;
    uint16_t    ch;
    uint16_t    lun;
    uint32_t    blk;
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

int                  fox_engine_register (struct fox_engine *);
struct fox_engine   *fox_get_engine(uint16_t);
struct fox_node     *fox_create_threads (struct fox_workload *);
void                 fox_exit_threads (struct fox_node *);
void                 fox_merge_stats (struct fox_node *, struct fox_stats *);
void                 fox_monitor (struct fox_node *);
void                 fox_set_stats (uint8_t, struct fox_stats *, int64_t);
void                 fox_start_node (struct fox_node *);
void                 fox_end_node (struct fox_node *);
void                 fox_timestamp_start (struct fox_stats *);
uint64_t             fox_timestamp_tmp_start (struct fox_stats *);
uint64_t             fox_timestamp_end (uint8_t, struct fox_stats *);
void                 fox_show_stats (struct fox_workload *, struct fox_node *);
void                 fox_show_workload (struct fox_workload *);
int                  fox_alloc_vblks (struct fox_workload *);
void                 fox_free_vblks (struct fox_workload *);
int                  fox_vblk_tgt (struct fox_node *, uint16_t, uint16_t,
                                                                      uint32_t);
void                 fox_set_progress (struct fox_stats *, uint16_t);
int                  fox_init_stats (struct fox_stats *);
void                 fox_exit_stats (struct fox_stats *);
void                 fox_wait_for_ready (struct fox_workload *);
void                 fox_wait_for_monitor (struct fox_workload *);
int                  fox_alloc_blk_buf (struct fox_node *, struct fox_blkbuf *);
void                 fox_blkbuf_reset (struct fox_node *, struct fox_blkbuf *);
void                 fox_free_blkbuf (struct fox_blkbuf *, int);
int                  fox_blkbuf_cmp (struct fox_node *, struct fox_blkbuf *,
                                                           uint16_t, uint16_t);

/* fox-output */
int                  fox_output_init (struct fox_workload *);
void                 fox_output_exit (void);
void                 fox_output_append (struct fox_output_row *, int);
void                 fox_output_append_rt(struct fox_output_row_rt *, uint16_t);
void                 fox_output_flush (void);
void                 fox_output_flush_rt (void);
void                 fox_print (char *);
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
int    foxeng_seq_init (struct fox_workload *);
int    foxeng_rr_init (struct fox_workload *);
int    foxeng_iso_init (struct fox_workload *);
#endif /* FOX_H */