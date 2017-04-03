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

#include "types.h"

enum{
	LRCU_NS_DEFAULT = 0,
	LRCU_NS_MAX,
};
#define LRCU_WORKER_SLEEP_US	1000

/***********************************************************/


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
	size_t id;
	u64 version;
	u8 counter; /* max nesting depth 255 */
};

/* XXX make number of namespaces dynamic??? */
struct lrcu_thread_info{
	struct lrcu_handler *h;
	struct lrcu_local_namespace lns[LRCU_NS_MAX];
};

/***********************************************************/

#define lrcu_write_lock() ({ \
			struct lrcu_thread_info *__ti = LRCU_GET_TI(); \
			lrcu_write_lock_ns(lrcu_ti_get_ns(__ti, LRCU_NS_DEFAULT)); \
		})

void lrcu_write_lock_ns(struct lrcu_namespace *ns);

/***********************************************************/

void lrcu_assign_pointer(struct lrcu_ptr *ptr, void *newptr);

/***********************************************************/

#define lrcu_write_unlock() ({ \
			struct lrcu_thread_info *__ti = LRCU_GET_TI(); \
			lrcu_write_unlock_ns(lrcu_ti_get_ns(__ti, LRCU_NS_DEFAULT)); \
		})

void lrcu_write_unlock_ns(struct lrcu_namespace *ns);

/***********************************************************/

#define lrcu_read_lock() lrcu_read_lock_ns(LRCU_NS_DEFAULT)

#define lrcu_read_lock_ns(x) ({ \
			struct lrcu_thread_info *__ti = LRCU_GET_TI(); \
			__lrcu_read_lock(__ti, lrcu_ti_get_ns(__ti, (x))); \
		})

void __lrcu_read_lock(struct lrcu_thread_info *ti, struct lrcu_namespace *ns);

/***********************************************************/

void *lrcu_read_dereference_pointer(struct lrcu_ptr *ptr);

/***********************************************************/

#define lrcu_read_unlock() lrcu_read_unlock_ns(LRCU_NS_DEFAULT)

#define lrcu_read_unlock_ns(x) ({ \
			struct lrcu_thread_info *__ti = LRCU_GET_TI(); \
			__lrcu_read_unlock(__ti, lrcu_ti_get_ns(__ti, (x))); \
		})

void __lrcu_read_unlock(struct lrcu_thread_info *ti, struct lrcu_namespace *ns);

/***********************************************************/

#define lrcu_call(x, y) ({ \
			struct lrcu_handler *__handler = LRCU_GET_HANDLER(); \
			if(__handler != NULL) \
				__lrcu_call(lrcu_get_ns(__handler, (x)->ns_id), (x), (y)); \
		})

void __lrcu_call(struct lrcu_namespace *ns, 
							struct lrcu_ptr *ptr, lrcu_destructor_t *destr);

/***********************************************************/

#define lrcu_init() ({ \
			__lrcu_init(); \
			lrcu_ns_init(LRCU_NS_DEFAULT); \
		})

struct lrcu_handler *__lrcu_init(void);

void lrcu_deinit(void);

/***********************************************************/

struct lrcu_namespace *lrcu_ns_init(u8 id);

void lrcu_ns_deinit(u8 id);

/***********************************************************/

#define lrcu_thread_init() ({ \
			__lrcu_thread_init(); \
			lrcu_thread_set_ns(LRCU_NS_DEFAULT); \
		})

struct lrcu_thread_info *__lrcu_thread_init(void);

/***********************************************************/

#define lrcu_thread_set_ns(x) ({ \
			struct lrcu_thread_info *__ti = LRCU_GET_TI(); \
			__lrcu_thread_set_ns(__ti, (x)); \
		})

bool __lrcu_thread_set_ns(struct lrcu_thread_info *ti, u8 ns_id);

/***********************************************************/

#define lrcu_thread_deinit() ({ \
			struct lrcu_thread_info *__ti = LRCU_GET_TI(); \
			__lrcu_thread_deinit(__ti); \
		})

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

#define LRCU_GET_HANDLER() (__lrcu_handler)
#define LRCU_SET_HANDLER(x) __lrcu_handler = (x)
#define LRCU_DEL_HANDLER(x) __lrcu_handler =NULL

#define LRCU_GET_TI() (__lrcu_thread_info)
#define LRCU_SET_TI(x) __lrcu_thread_info = (x)
#define LRCU_DEL_TI(x) __lrcu_thread_info = NULL

#endif