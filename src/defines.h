#ifndef __LRCU_USER_DEFINES_H__
#define __LRCU_USER_DEFINES_H__

enum{
    LRCU_NS_DEFAULT = 0,
    /* add your own namespaces here */
    LRCU_NS_MAX, /* but no more than 255 */
};

#define LRCU_THREADS_MAX 64

#define LRCU_WORKER_SLEEP_US    50000
#define LRCU_NS_SYNC_SLEEP_US   10000
#define LRCU_HANG_TIMEOUT_S     1

/***********************************************************/
/* down here os api abstraction layer */

#define LRCU_KERNEL __KERNEL__

/* to indicate which one to use */
#define BINTREE_SEARCH_SELF_IMPLEMENTED

#define LRCU_CACHE_LINE_SIZE 64
#define LRCU_ALIGNED __attribute__((__aligned__(LRCU_CACHE_LINE_SIZE)))

#ifdef LRCU_KERNEL
#include "linux.h"
#else
#include "user.h"
#endif

#endif /* __LRCU_USER_DEFINES_H__ */