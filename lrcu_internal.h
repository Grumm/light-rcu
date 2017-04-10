#ifndef _LRCU_INTERNAL_H
#define _LRCU_INTERNAL_H

#include <pthread.h>

#include "types.h"
#include "atomics.h"

enum{ /* thread worker states */
    LRCU_WORKER_READY = 0,
    LRCU_WORKER_RUN,
    LRCU_WORKER_RUNNING,
    LRCU_WORKER_STOP,
    LRCU_WORKER_DONE,
};

struct lrcu_handler{
    spinlock_t  ns_lock;
    struct lrcu_namespace *ns[LRCU_NS_MAX];

    struct lrcu_thread_info *worker_ti;
    pthread_t worker_tid;
    int worker_state;
    u32 worker_timeout;
};

struct lrcu_namespace {
    spinlock_t  write_lock;
    u64 version;
    u64 processed_version;
    u32 sync_timeout;
    spinlock_t  threads_lock;
    list_head_t threads;
    list_head_t hung_threads;
    u8 id;
    spinlock_t  list_lock;
    list_head_t free_list, worker_list;
};

#define LRCU_GET_LNS_ID(ti, ns_id) (&(ti)->lns[(ns_id)])
#define LRCU_GET_LNS(ti, ns) LRCU_GET_LNS_ID((ti), (ns)->id)
#define LRCU_GET_HUNG_LNS(ti, ns) (&(ti)->hung_lns[(ns)->id])

#define LRCU_GET_HANDLER() (__lrcu_handler)
#define LRCU_SET_HANDLER(x) __lrcu_handler = (x)
#define LRCU_DEL_HANDLER(x) __lrcu_handler = NULL

#define LRCU_GET_TI() (__lrcu_thread_info)
#define LRCU_SET_TI(x) __lrcu_thread_info = (x)
#define LRCU_DEL_TI(x) __lrcu_thread_info = NULL


#endif /* _LRCU_INTERNAL_H */