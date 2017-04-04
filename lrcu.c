/*
	Lazy RCU: extra light read section and hard write section
	locks taken only on lrcu_ptr  
*/

#include <unistd.h>

#include "types.h"
#include "list.h"
#include "atomics.h"
#include "lrcu.h"
#include "lrcu_internal.h"

struct lrcu_handler *__lrcu_handler = NULL;
__thread struct lrcu_thread_info *__lrcu_thread_info;

/***********************************************************/

/* can't be nested */
void lrcu_write_lock_ns(struct lrcu_namespace *ns){
	spin_lock(&ns->write_lock);
	ns->version++;
	barrier(); /* new ptr's should be seen only with new version */
}

void lrcu_assign_pointer(struct lrcu_ptr *ptr, void *newptr){
	struct lrcu_ptr old_ptr;

	//ptr->ptr = NULL; 
					/* make sure anyone who dereferences since now, 
						will get either NULL or new version with correct pointer */
	//ptr->version = ptr->ns->version;
	//barrier();
	old_ptr = *ptr;
	ptr->ptr = newptr;
	barrier();
	/* TODO how? */
	lrcu_call(&old_ptr, ptr->deinit);
	/* add old_ptr to free_queue and wake up lrcu_worker thread */
	/* 
		We are under ns->write_lock. so old ns->version is obsolete and 
		all pointers that have that version or less should be freed after
		grace period.
		Need to change ns->version */
}

void lrcu_write_unlock_ns(struct lrcu_namespace *ns){
	spin_unlock(&ns->write_lock);
}

/***********************************************************/

void __lrcu_read_lock(struct lrcu_thread_info *ti, struct lrcu_namespace *ns){
	
	LRCU_GET_LNS(ti, ns)->counter++; /* can be nested! */
	barrier(); /* make sure counter changed first, and only after 
							that version. see worker thread order */
	/* only first entrance in read section matters */
	if(likely(LRCU_GET_LNS(ti, ns)->counter == 1))
		LRCU_GET_LNS(ti, ns)->version = ns->version;
		/* say we entered read section with this ns version */
	wmb();
	/* tell ns that grace period is in progress */
}

void *lrcu_read_dereference_pointer(struct lrcu_ptr *ptr){
	struct lrcu_ptr local_ptr;
	barrier();
	local_ptr = *ptr;
	/* we access thread-local ns-local variable, so no need for atomic access */
	barrier();

	return local_ptr.ptr;
}

void __lrcu_read_unlock(struct lrcu_thread_info *ti, struct lrcu_namespace *ns){
	if(--LRCU_GET_LNS(ti, ns)->counter == 0){
		if(LRCU_GET_LNS(ti, ns)->version != ns->version){
			/* XXX notify thread that called synchronize() that we are done.
			In current implementation I see this can be omitted since 
			we can use lazy checks in synchronize(), but if not, might wanna use 
			per-ns/per-thread pthread condition variable */
			barrier();
		}
	}
}

/***********************************************************/

void __lrcu_call(struct lrcu_namespace *ns, 
							struct lrcu_ptr *ptr, lrcu_destructor_t *destr){
	struct lrcu_ptr local_ptr = *ptr;					

	local_ptr.deinit = destr;
	local_ptr.version = ns->version; /* synchronize() will be called on this version */

	spin_lock(&ns->list_lock);

							/* NOT A POINTER!!! */
	list_add(&ns->free_list, local_ptr);
	spin_unlock(&ns->list_lock);
	/* XXX wakeup thread. see lrcu_read_unlock */
}
#include <inttypes.h>
#include <stdio.h>

/* ptr->version contains last viable ns version */
static inline bool __lrcu_synchronized(struct lrcu_namespace *ns,
													struct lrcu_ptr *ptr){
	struct lrcu_thread_info *ti;
	list_t *e, *next;
	bool ready;

	/* make sure that each thread that uses ns, has 
		local_namespace either bigger version, or zero counter */
	list_for_each(e, next, &ns->threads){
		struct lrcu_local_namespace lns;

		ready = false;

		ti = *((struct lrcu_thread_info **)(void **)&e->data[0]);

		lns = *LRCU_GET_LNS(ti, ns);
		rmb();
		/*  We need to wait until we exit (ns->namespace or less) read locks versions */
		if(lns.version > ptr->version || /* TODO what about when version wraps -1 ? */
						lns.counter == 0){
			/* it's time!!! */
			ready = true;
			timerclear(&LRCU_GET_LNS(ti, ns)->timeval);
		}else {
			struct timeval now, *lns_tv;
			static const struct timeval timer = { .tv_sec = LRCU_HANG_TIMEOUT_S, .tv_usec = 0, };
			struct timeval timer_expires;

			lns_tv = &LRCU_GET_LNS(ti, ns)->timeval;
			if(likely(timerisset(lns_tv))){
				gettimeofday(&now, NULL);
				timeradd(lns_tv, &timer, &timer_expires);
				if(unlikely(!timercmp(&now, &timer_expires, <))){ /* !< is >= */
					/* timer expired. thread sleeps too long in read-section */
					//LRCU_GET_LNS(ti, ns)->version = ptr->version;
					printf("error %"PRIu64" %"PRIu64" %"PRIu64" %d\n", 
						ptr->version, ns->version, lns.version, lns.counter);
					fflush(stdout);
					//exit(1);
					/* put thread in list of hanging threads and check them separately */
					*LRCU_GET_HUNG_LNS(ti, ns) = lns;
					spin_lock(&ns->threads_lock);//XXX do we need to take a lock?
					list_unlink(&ns->threads, e);
					list_insert(&ns->hung_threads, e);
					spin_unlock(&ns->threads_lock);
					timerclear(&LRCU_GET_LNS(ti, ns)->timeval);
					continue;
				}
			} else{
				gettimeofday(&LRCU_GET_LNS(ti, ns)->timeval, NULL);
			}
			return false;
		}
	}

	ready = true;
	list_for_each(e, next, &ns->hung_threads){
		struct lrcu_local_namespace lns, hung_lns;

		ti = *((struct lrcu_thread_info **)(void **)&e->data[0]);

		hung_lns = *LRCU_GET_HUNG_LNS(ti, ns);
		lns = *LRCU_GET_LNS(ti, ns);
		rmb();
		if(lns.version > hung_lns.version || lns.counter == 0){
			printf("AAAAAAAAAAAAAAAND we are back\n");
					fflush(stdout);
			/* we have been changed after we hang */
			spin_lock(&ns->threads_lock);//XXX do we need to take a lock?
			list_unlink(&ns->hung_threads, e);
			list_insert(&ns->threads, e);
			spin_unlock(&ns->threads_lock);
		}
		/* everything that is between [hung_lns.version and ptr->version] cannot be freed */
		if(lns.version <= ptr->version) /* check ability to free the pointer */
			ready = false;
	}

	return ready;
}

static inline void *lrcu_worker(void *arg){
	struct lrcu_handler *h = (struct lrcu_handler *)arg;

	while(h->worker_run){
		size_t i;

		for(i = 0; i < LRCU_NS_MAX; i++){
			struct lrcu_namespace *ns = h->ns[i];
			if(ns == NULL)
				continue;

			if(!list_empty(&ns->free_list)){
				spin_lock(&ns->list_lock);
				list_splice(&ns->worker_list, &ns->free_list);
				spin_unlock(&ns->list_lock);
			}
			if(!list_empty(&ns->worker_list)){
				struct lrcu_ptr *ptr;
				list_t *n, *next;
				list_for_each(n, next, &ns->worker_list){
					ptr = (struct lrcu_ptr *)n->data;
					/* do actual job */
					if(__lrcu_synchronized(ns, ptr)){
						ptr->deinit(ptr->ptr);
						list_unlink(&ns->worker_list, n);
						free(n);
					}
				}
			}
		}
		usleep(h->sleep_time);
	}
	return NULL;
}

struct lrcu_handler *__lrcu_init(void){
	struct lrcu_handler *h;

	h = calloc(1, sizeof(struct lrcu_handler));
	if(h == NULL)
		return NULL;

	h->worker_run = true;
	h->sleep_time = LRCU_WORKER_SLEEP_US; //1 sec
	if(pthread_create(&h->worker_tid, NULL, lrcu_worker, (void *)h))
		goto out;

	LRCU_SET_HANDLER(h);

	return h;
out:
	free(h);
	return NULL;
}

void lrcu_deinit(void){
	struct lrcu_handler *h = LRCU_GET_HANDLER();

	if(h == NULL)
		return;

	h->worker_run = false;
	pthread_join(h->worker_tid, NULL);
	LRCU_DEL_HANDLER(h);
	/* TODO remove all ns */
}

/***********************************************************/

struct lrcu_namespace *lrcu_ns_init(u8 id){
	struct lrcu_namespace *ns = NULL;
	struct lrcu_handler *h = LRCU_GET_HANDLER();

	if(h == NULL)
		return NULL;

	spin_lock(&h->ns_lock);
	if(h->ns[id])
		goto out;

	ns = calloc(1, sizeof(struct lrcu_namespace));
	if(ns == NULL)
		goto out;

	ns->id = id;
	//list_add(&h->ns, ns);
	h->ns[id] = ns;
out:
	spin_unlock(&h->ns_lock);
	return ns;
}

void __lrcu_ns_deinit(u8 id){
	struct lrcu_handler *h = LRCU_GET_HANDLER();

	if(h == NULL)
		return;

	spin_lock(&h->ns_lock);
	free(h->ns[id]);
	h->ns[id] = NULL;
	spin_unlock(&h->ns_lock);
	/* TODO remove all threads */
}

struct lrcu_thread_info *__lrcu_thread_init(void){
	struct lrcu_thread_info *ti = NULL;
	size_t i;
	struct lrcu_handler *h = LRCU_GET_HANDLER();

	if(h == NULL)
		return NULL;

	ti = calloc(1, sizeof(struct lrcu_thread_info));
	if(ti == NULL)
		return NULL;

	for(i = 0; i < LRCU_NS_MAX; i++){
		ti->lns[i].id = i;
	}
	ti->h = h;
	LRCU_SET_TI(ti);

	return ti;
}

bool __lrcu_thread_set_ns(struct lrcu_thread_info *ti, u8 ns_id){
	struct lrcu_namespace *ns;
	void *ret;

	ns = ti->h->ns[ns_id];
	if(ns == NULL)
		return false;

	spin_lock(&ns->threads_lock);
	ret = list_add(&ns->threads, ti);
	spin_unlock(&ns->threads_lock);

	return ret != NULL;
}

void __lrcu_thread_deinit(struct lrcu_thread_info *ti){
	/* TODO remove from all namespaces */
	LRCU_DEL_TI(ti);
	free(ti);
}

/* already allocated ptr */
void __lrcu_ptr_init(struct lrcu_ptr *ptr, u8 ns_id, 
								lrcu_destructor_t *deinit){
	ptr->ns_id = ns_id;
	ptr->deinit = deinit;
}

struct lrcu_namespace *lrcu_ti_get_ns(struct lrcu_thread_info *ti, u8 id){
	return ti->h->ns[id];
}

struct lrcu_namespace *lrcu_get_ns(struct lrcu_handler *h, u8 id){
	return h->ns[id];
}