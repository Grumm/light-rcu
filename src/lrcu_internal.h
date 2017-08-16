#ifndef _LRCU_INTERNAL_H
#define _LRCU_INTERNAL_H

#include "atomics.h"
#include "list.h"
/*
 * container_of - cast a member of a structure out to the containing structure
 * @ptr:        the pointer to the member.
 * @type:       the type of the container struct this is embedded in.
 * @member:     the name of the member within the struct.
 *
 */
#define container_of(ptr, type, member) ({                      \
        const typeof( ((type *)0)->member ) *__mptr = (ptr);    \
        (type *)( (char *)__mptr - offsetof(type,member) );})

#define likely(x)      __builtin_expect(!!(x), 1)
#define unlikely(x)    __builtin_expect(!!(x), 0)
#define ACCESS_ONCE(x) (*(volatile typeof(x) *)&(x))

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
    struct lrcu_namespace *worker_ns[LRCU_NS_MAX]; 
    /* duplicated set of pointers. used to properly remove namespaces */

    struct lrcu_thread_info *worker_ti;
    LRCU_THREAD_T worker_tid;
    int worker_state;
    u32 worker_timeout;
};

struct lrcu_namespace {
    spinlock_t  write_lock;
    u64 processed_version;
    u32 sync_timeout;
    spinlock_t  threads_lock;
    list_head_t threads;
    list_head_t hung_threads;
    u8 id;
    spinlock_t  list_lock;
    list_head_t free_list, worker_list;
    u64 version LRCU_ALIGNED;
} LRCU_ALIGNED;

struct lrcu_local_namespace {
    u64 version;
    i32 counter; /* max nesting depth 2^32 */
};

/* XXX make number of namespaces dynamic??? */
struct lrcu_thread_info{
    struct lrcu_handler *h;
    LRCU_TIMER_TYPE timeval[LRCU_NS_MAX];
    struct lrcu_local_namespace lns[LRCU_NS_MAX];
    struct lrcu_local_namespace hung_lns[LRCU_NS_MAX];
};

#define LRCU_GET_LNS_ID(ti, ns_id) (&(ti)->lns[(ns_id)])
#define LRCU_GET_LNS(ti, ns) LRCU_GET_LNS_ID((ti), (ns)->id)
#define LRCU_GET_HUNG_LNS(ti, ns) (&(ti)->hung_lns[(ns)->id])

#define LRCU_GET_HANDLER() (__lrcu_handler)
#define LRCU_SET_HANDLER(x) __lrcu_handler = (x)
#define LRCU_DEL_HANDLER() __lrcu_handler = NULL

#define LRCU_GET_TI() LRCU_TLS_GET(__lrcu_thread_info)
#define LRCU_SET_TI(x) LRCU_TLS_SET(__lrcu_thread_info, (x))
#define LRCU_DEL_TI(x) LRCU_TLS_SET(__lrcu_thread_info, NULL)

#endif /* _LRCU_INTERNAL_H */