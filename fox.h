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
    FOX_STATS_FAIL_CMP,
    FOX_STATS_FAIL_E,
    FOX_STATS_FAIL_R,
    FOX_STATS_FAIL_W
};

#define FOX_FLAG_READY (1 << 0)
#define FOX_FLAG_DONE  (1 << 1)

typedef void *(fengine_fn)(void *);

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
    char             *devname;
    uint8_t          engine;
    uint8_t          channels;
    uint8_t          luns;
    uint32_t         blks;
    uint32_t         pgs;
    uint8_t          nthreads;
    uint16_t         w_factor;
    uint16_t         r_factor;
    uint32_t         max_delay;
    uint8_t          memcmp;
    uint64_t         runtime; /* u-seconds */
    fengine_fn       *fengine_fn;
    NVM_DEV          dev;
    NVM_GEO          geo;
    NVM_VBLK         *vblks;
    struct fox_stats *stats;
    pthread_mutex_t  start_mut;
    pthread_cond_t   start_con;
};

struct fox_blkbuf {
    uint8_t *buf_w;
    uint8_t *buf_r;
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
    NVM_VBLK            vblk_tgt;
    LIST_ENTRY(node)    entry;
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

struct fox_node     *fox_create_threads (struct fox_workload *);
void                 fox_exit_threads (struct fox_node *);
void                 fox_merge_stats (struct fox_node *, struct fox_stats *);
void                 fox_monitor (struct fox_node *);
void                 fox_set_stats (uint8_t, struct fox_stats *, int64_t);
void                 fox_start_node (struct fox_node *);
void                 fox_end_node (struct fox_node *);
void                 fox_timestamp_start (struct fox_stats *);
void                 fox_timestamp_tmp_start (struct fox_stats *);
void                 fox_timestamp_end (uint8_t, struct fox_stats *);
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
int                  fox_alloc_blk_buf (struct fox_node *, struct fox_blkbuf *);
void                 fox_blkbuf_reset (struct fox_node *, struct fox_blkbuf *);
void                 fox_free_blkbuf (struct fox_blkbuf *, int);
int                  fox_blkbuf_cmp (struct fox_node *, struct fox_blkbuf *,
                                                           uint16_t, uint16_t);
void                *fox_engine1 (void *);
void                *fox_engine2 (void *);

#endif /* FOX_H */
