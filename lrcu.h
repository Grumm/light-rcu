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

#include "atomics.h"

struct lrcu_handler{
	spinlock_t  ns_lock;
	struct lrcu_namespace *ns[LRCU_NS_MAX];
	//list_head_t threads;
	bool worker_run;
	u32 sleep_time;
};
#define LRCU_WORKER_SLEEP_US	1000000

struct lrcu_namespace {
	spinlock_t  write_lock;
	u64 version;
	list_head_t threads;
	u8 id;
};

struct lrcu_local_namespace {
	size_t id;
	u64 version;
	u8 counter; /* max nesting depth 255 */
};

typedef void (lrcu_destructor_t)(void *);

struct lrcu_ptr {
	void *ptr; /* actual data behind pointer */
	lrcu_destructor_t *deinit;
	u64 version;
	u8 ns_id;
};

enum{
	LRCU_NS_DEFAULT = 0,
	LRCU_NS_MAX,
};

/* XXX make number of namespaces dynamic??? */
struct lrcu_thread_info{
	struct lrcu_handler *h;
	struct lrcu_local_namespace lns[LRCU_NS_MAX];
}

#define LRCU_GET_LNS(ti, ns) (&(ti)->lns[(ns)->id])
#define ACCESS_LRCU(p) ((p)->ptr)
#define ACCESS_ONCE(x) (*(volatile typeof(x) *)&(x))

/***********************************************************/

#define lrcu_write_lock() ({ \
			struct lrcu_thread_info *__ti = LRCU_GET_TI(); \
			lrcu_write_lock_ns(__ti->h->ns[LRCU_NS_DEFAULT]); \
		})

void lrcu_write_lock_ns(struct lrcu_namespace *ns);

/***********************************************************/

void lrcu_assign_pointer(struct lrcu_ptr *ptr, void *newptr);

/***********************************************************/

#define lrcu_write_unlock() ({ \
			struct lrcu_thread_info *__ti = LRCU_GET_TI(); \
			lrcu_write_unlock_ns(__ti->h->ns[LRCU_NS_DEFAULT]); \
		})

void lrcu_write_unlock_ns(struct lrcu_namespace *ns);

/***********************************************************/

#define lrcu_read_lock() lrcu_read_lock_ns(LRCU_NS_DEFAULT)

#define lrcu_read_lock_ns(x) ({ \
			struct lrcu_thread_info *__ti = LRCU_GET_TI(); \
			__lrcu_read_lock(__ti, __ti->h->ns[(x)]); \
		})

void __lrcu_read_lock(struct lrcu_thread_info *ti, struct lrcu_namespace *ns);

/***********************************************************/

#define lrcu_read_dereference_pointer(x) ({ \
			struct lrcu_thread_info *__ti = LRCU_GET_TI(); \
			__lrcu_read_dereference_pointer(__ti, (x)); \
		})

void *__lrcu_read_dereference_pointer(struct lrcu_thread_info *ti,
												struct lrcu_ptr *ptr);

/***********************************************************/

#define lrcu_read_unlock() lrcu_read_unlock_ns(LRCU_NS_DEFAULT)

#define lrcu_read_unlock_ns(x) ({ \
			struct lrcu_thread_info *__ti = LRCU_GET_TI(); \
			__lrcu_read_unlock(__ti, __ti->h->ns[(x)]); \
		})

void __lrcu_read_unlock(struct lrcu_thread_info *ti, struct lrcu_namespace *ns);

/***********************************************************/

#define lrcu_call(x, y) ({ \
			struct lrcu_thread_info *__ti = LRCU_GET_TI(); \
			__lrcu_call(__ti, (x), (y)); \
		})

void __lrcu_call(struct lrcu_thread_info *ti, 
							struct lrcu_ptr *ptr, lrcu_destructor_t *destr);

/***********************************************************/

#define lrcu_init() ({ \
			lrcu_ns_init(LRCU_NS_DEFAULT);
			__lrcu_init();
		})
struct lrcu_handler *__lrcu_init(void);

void lrcu_deinit(struct lrcu_handler *h);

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

extern struct lrcu_handler *__lrcu_handler;
extern __thread struct lrcu_thread_info *__lrcu_thread_info;

#define LRCU_GET_HANDLER() (__lrcu_handler)
#define LRCU_SET_HANDLER(x) __lrcu_handler = (x)
#define LRCU_DEL_HANDLER(x) __lrcu_handler =NULL

#define LRCU_GET_TI() (__lrcu_thread_info)
#define LRCU_SET_TI(x) __lrcu_thread_info = (x)
#define LRCU_DEL_TI(x) __lrcu_thread_info = NULL
