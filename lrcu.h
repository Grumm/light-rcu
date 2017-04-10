#ifndef _LRCU_API_H
#define _LRCU_API_H
/*
    Lazy RCU: extra light read section and hard write section
    locks taken only on lrcu_ptr  
*/

/*
    lrcu_thread_info->lrcu_ptr_ns->lrcu_ptr
                                 ->lrcu_ptr
                    ->lrcu_ptr_ns->lrcu_ptr
                                 ->lrcu_ptr
    each thread has to have local version - which section thread in
    (global)lrcu_ptr's have their version. thread accesses
*/

#include <sys/time.h>
#include "types.h"

enum{
    LRCU_NS_DEFAULT = 0,
    LRCU_NS_MAX,
};
#define LRCU_WORKER_SLEEP_US    1000000
#define LRCU_NS_SYNC_SLEEP_US   10000
#define LRCU_HANG_TIMEOUT_S     1

/***********************************************************/

/* user can redefine this */
#ifndef LRCU_BUG
#include <stdlib.h>
#include <stdio.h>
#include <libgen.h>
#define LRCU_BUG()  do{ \
        printf("LRCU BUG %s(%d):%s\n", basename(__FILE__), __LINE__, __FUNCTION__); \
        exit(1); \
    }while(0)
#endif

typedef void (lrcu_destructor_t)(void *);
struct lrcu_namespace;
struct lrcu_handler;

struct lrcu_ptr {
    void *ptr; /* actual data behind pointer */
    lrcu_destructor_t *deinit;
    u64 version;
    u8 ns_id;
};

struct lrcu_local_namespace {
    struct timeval timeval;
    size_t id;
    u64 version;
    i32 counter; /* max nesting depth 2^32 */
};

/* XXX make number of namespaces dynamic??? */
struct lrcu_thread_info{
    struct lrcu_handler *h;
    struct lrcu_local_namespace lns[LRCU_NS_MAX];
    struct lrcu_local_namespace hung_lns[LRCU_NS_MAX];
};

/***********************************************************/

#define lrcu_write_lock() lrcu_write_lock_ns(LRCU_NS_DEFAULT)

void lrcu_write_lock_ns(u8 ns_id);

/***********************************************************/

void lrcu_assign_pointer(struct lrcu_ptr *ptr, void *newptr);

/***********************************************************/

#define lrcu_write_unlock() lrcu_write_unlock_ns(LRCU_NS_DEFAULT)

void lrcu_write_unlock_ns(u8 ns_id);

/***********************************************************/

#define lrcu_read_lock() lrcu_read_lock_ns(LRCU_NS_DEFAULT)

void lrcu_read_lock_ns(u8 ns_id);

/***********************************************************/

void *lrcu_read_dereference_pointer(struct lrcu_ptr *ptr);

/***********************************************************/

#define lrcu_read_unlock() lrcu_read_unlock_ns(LRCU_NS_DEFAULT)

void lrcu_read_unlock_ns(u8 ns_id);

/***********************************************************/

#define lrcu_call(x, y) __lrcu_call_ptr(LRCU_NS_DEFAULT, (x), (y));

/* x - lrcu_ptr */
#define lrcu_call_ptr(x) __lrcu_call_ptr((x)->ns_id, (x)->ptr, (x)->deinit);

void __lrcu_call_ptr(u8 ns_id, void *p, lrcu_destructor_t *destr);

void __lrcu_call(struct lrcu_namespace *ns, 
                            void *p, lrcu_destructor_t *destr);

/***********************************************************/

#define lrcu_synchronize() lrcu_synchronize_ns(LRCU_NS_DEFAULT)

void lrcu_synchronize_ns(u8 ns_id);

/***********************************************************/

#define lrcu_barrier() lrcu_synchronize_ns(LRCU_NS_DEFAULT)

void lrcu_barrier_ns(u8 ns_id);

/***********************************************************/

struct lrcu_handler *lrcu_init(void);

struct lrcu_handler *__lrcu_init(void);

void lrcu_deinit(void);

/***********************************************************/

struct lrcu_namespace *lrcu_ns_init(u8 id);

void lrcu_ns_deinit(u8 id);

/***********************************************************/

struct lrcu_thread_info *lrcu_thread_init(void);

struct lrcu_thread_info *__lrcu_thread_init(void);

/***********************************************************/

bool lrcu_thread_set_ns(u8 ns_id);

bool __lrcu_thread_set_ns(struct lrcu_thread_info *ti, u8 ns_id);

/***********************************************************/

void lrcu_thread_deinit(void);

void __lrcu_thread_deinit(struct lrcu_thread_info *ti);

/***********************************************************/

/* already allocated ptr */
#define lrcu_ptr_init(x) __lrcu_ptr_init((x), LRCU_NS_DEFAULT, free)

void __lrcu_ptr_init(struct lrcu_ptr *ptr, u8 ns_id, 
                            lrcu_destructor_t *deinit);

/***********************************************************/

struct lrcu_namespace *lrcu_ti_get_ns(struct lrcu_thread_info *ti, u8 id);

struct lrcu_namespace *lrcu_get_ns(struct lrcu_handler *h, u8 id);

/***********************************************************/

extern struct lrcu_handler *__lrcu_handler;
extern __thread struct lrcu_thread_info *__lrcu_thread_info;

#endif