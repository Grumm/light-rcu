#ifndef _LRCU_INTERNAL_H
#define _LRCU_INTERNAL_H

#include <pthread.h>
#include "types.h"
#include "atomics.h"

struct lrcu_handler{
	spinlock_t  ns_lock;
	struct lrcu_namespace *ns[LRCU_NS_MAX];
	//list_head_t threads;
	pthread_t worker_tid;
	bool worker_run;
	u32 sleep_time;
};

struct lrcu_namespace {
	spinlock_t  write_lock;
	u64 version;
	spinlock_t  threads_lock;
	list_head_t threads;
	u8 id;
	spinlock_t  list_lock;
	list_head_t free_list, worker_list;
};

#define LRCU_GET_LNS(ti, ns) (&(ti)->lns[(ns)->id])

#endif /* _LRCU_INTERNAL_H */