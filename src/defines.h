#ifndef __LRCU_USER_DEFINES_H__
#define __LRCU_USER_DEFINES_H__

enum{
    LRCU_NS_DEFAULT = 0,
    /* add your own namespaces here */
    LRCU_NS_MAX, /* but no more than 255 */
};

#define LRCU_THREADS_MAX 64

/* time between worker cycles */
#define LRCU_WORKER_SLEEP_US    50000
/* time between synchronize waiting loop */
#define LRCU_NS_SYNC_SLEEP_US   10000
/* hang detection mechanism to prevent complete malfunction */
#define LRCU_HANG_TIMEOUT_S     60

/***********************************************************/
/* OS api abstraction layer */

#define LRCU_KERNEL __KERNEL__

#if LRCU_KERNEL
#include "linux.h"
#else
#include "user.h"
#endif

#endif /* __LRCU_USER_DEFINES_H__ */