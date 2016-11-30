#include <sys/queue.h>
#include <stdint.h>
#include <liblightnvm.h>
#include <sys/time.h>

#ifndef FOX_H
#define FOX_H

#define AND64 0xffffffffffffffff
#define SEC64 (1000000 & AND64)

#define FOX_ENGINE_1  0x1 /* All sequential */

enum {
    FOX_STATS_ERASE_T = 0x1,
    FOX_STATS_READ_T,
    FOX_STATS_WRITE_T,
    FOX_STATS_RUNTIME,
    FOX_STATS_ERASED_BLK,
    FOX_STATS_PGS_W,
    FOX_STATS_PGS_R,
    FOX_STATS_BREAD,
    FOX_STATS_BWRITTEN,
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
    uint64_t        read_t;
    uint64_t        write_t;
    uint64_t        erase_t;
    uint32_t        erased_blks;
    uint32_t        pgs_r;
    uint32_t        pgs_w;
    uint64_t        bread;
    uint64_t        bwritten;
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
void                 fox_show_stats (struct fox_workload *);
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
void                 fox_free_blkbuf (struct fox_blkbuf *);
int                  fox_blkbuf_cmp (struct fox_node *, struct fox_blkbuf *,
                                                           uint16_t, uint16_t);
void                *fox_engine1 (void *);

#endif /* FOX_H */
