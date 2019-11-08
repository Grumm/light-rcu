/*
    Lazy RCU: extra light read section and hard write section
    locks taken only on lrcu_ptr  
*/

#include <lrcu/lrcu.h>
#include "lrcu_internal.h"
#include "range.h"

static struct lrcu_handler *__lrcu_handler = NULL;
LRCU_TLS_DEFINE(struct lrcu_thread_info *, __lrcu_thread_info);

/***********************************************************/

void lrcu_write_barrier_ns(u8 ns_id){
    struct lrcu_handler *h = LRCU_GET_HANDLER();
    struct lrcu_namespace *ns;

    LRCU_ASSERT(h);

    ns = h->ns[ns_id];
    LRCU_ASSERT(ns);

    ns->version++; /* new ptr's should be seen only with new version */
}
LRCU_EXPORT_SYMBOL(lrcu_write_barrier_ns);

/***********************************************************/

void lrcu_write_lock_ns(u8 ns_id){
    struct lrcu_handler *h = LRCU_GET_HANDLER();
    struct lrcu_namespace *ns;

    LRCU_ASSERT(h);

    ns = h->ns[ns_id];
    LRCU_ASSERT(ns);

    lrcu_spin_lock(&ns->write_lock);
    ns->version++;
    wmb();
}
LRCU_EXPORT_SYMBOL(lrcu_write_lock_ns);

/***********************************************************/

void lrcu_write_unlock_ns(u8 ns_id){
    struct lrcu_handler *h = LRCU_GET_HANDLER();
    struct lrcu_namespace *ns;

    LRCU_ASSERT(h);

    ns = h->ns[ns_id];
    LRCU_ASSERT(ns);

    //wmb();
    //ns->version++;
    lrcu_spin_unlock(&ns->write_lock);
}
LRCU_EXPORT_SYMBOL(lrcu_write_unlock_ns);


/***********************************************************/

/* 
    corener case 3 possible here??????:
    multiple assign_pointers on to the same pointers,
    and read section with dereference_pointers

    ....
    *pp = oldptr, v = 1
    ...some other lock()
                            read_lock(v = 1)
    ns->version++(v = 2)
                            dereference_ptr(pp = oldptr, v = 2)
    *pp = newptr
    free(oldptr)
    ns->version++(v = 3)
                            do_work(oldptr)

*/
void __lrcu_assign_pointer_ns(u8 ns_id, void **pp, void *newptr){
    lrcu_write_barrier_ns(ns_id);
    wmb();
    *pp = newptr;
}
LRCU_EXPORT_SYMBOL(__lrcu_assign_pointer_ns);

void lrcu_assign_ptr(struct lrcu_ptr *ptr, void *newptr){
    lrcu_write_barrier_ns(ptr->ns_id);
    wmb();
    ptr->ptr = newptr;
}
LRCU_EXPORT_SYMBOL(lrcu_assign_ptr);

void __lrcu_assign_ptr(struct lrcu_ptr *ptr, void *newptr){
    ptr->ptr = newptr;
}
LRCU_EXPORT_SYMBOL(__lrcu_assign_ptr);

/***********************************************************/

void lrcu_read_lock_ns(u8 ns_id){
    struct lrcu_thread_info *ti = LRCU_GET_TI();
    struct lrcu_handler *h = LRCU_GET_HANDLER();
    lrcu_local_namespace_t * lns;
    struct lrcu_namespace *ns;

    LRCU_ASSERT(h);

    ns = h->ns[ns_id];
    LRCU_ASSERT(ns);

    LRCU_ASSERT(ti);
    lns = LRCU_GET_LNS_ID(ti, ns_id);

    lns->counter++; /* can be nested! */
    barrier(); /* make sure counter changed first, and only after 
                            that version. see worker thread read order */
    /* only first entrance in read section matters */
    if(likely(lns->counter == 1)){
        lns->version = ns->version;
        /* say we entered read section with this ns version */
        /* barrier for worker thread to see new version */
        wmb();
    }
}
LRCU_EXPORT_SYMBOL(lrcu_read_lock_ns);

/***********************************************************/

void lrcu_read_unlock_ns(u8 ns_id){
    struct lrcu_handler *h = LRCU_GET_HANDLER();
    struct lrcu_thread_info *ti = LRCU_GET_TI();
    struct lrcu_namespace *ns;
    lrcu_local_namespace_t * lns;

    LRCU_ASSERT(h);

    ns = h->ns[ns_id];
    LRCU_ASSERT(ns);

    LRCU_ASSERT(ti);
    lns = LRCU_GET_LNS_ID(ti, ns_id);

    if(lns->counter != 1){
        lns->counter--;
    }else{
        /* between protected data access and actual destruction of the object */
        barrier();
        lns->counter--;
        if(lns->version != ns->version){
            /* 
            XXX notify thread that called synchronize() that we are done.
            In current implementation I see this can be omitted since 
            we can use lazy checks in synchronize(), but if not, might wanna use 
            per-ns/per-thread pthread condition variable
            */
        }
        barrier();
    }
    LRCU_ASSERT(lns->counter >= 0);
}
LRCU_EXPORT_SYMBOL(lrcu_read_unlock_ns);

/***********************************************************/

void *__lrcu_dereference(void **ptr){
    void *p = ACCESS_ONCE(*ptr);

    read_barrier_depends();
    return p;
}
LRCU_EXPORT_SYMBOL(__lrcu_dereference);

void *lrcu_dereference_ptr(struct lrcu_ptr *ptr){
    void *p = ACCESS_ONCE(ptr->ptr);

    read_barrier_depends();
    return p;
}
LRCU_EXPORT_SYMBOL(lrcu_dereference_ptr);

/***********************************************************/

void lrcu_call_ns(u8 ns_id, void *p, lrcu_destructor_t *destr){
    struct lrcu_handler *h = LRCU_GET_HANDLER();
    struct lrcu_namespace *ns;
    struct lrcu_ptr local_ptr = {
        .deinit = destr,
        .ptr = p,
    };

    LRCU_ASSERT(h);

    ns = h->ns[ns_id];
    LRCU_ASSERT(ns);

    local_ptr.version = ns->version, /* synchronize() will be called on this version */

    /* since we don't have local_irq_save(), this_cpu_ptr()
                        functions, only option is spinlock */
#ifdef LRCU_LIST_ATOMIC
    lrcu_list_add_atomic(&ns->free_list, local_ptr);
#else
    lrcu_spin_lock(&ns->list_lock);

                            /* NOT A POINTER!!! */
    lrcu_list_add(&ns->free_list, local_ptr);
    lrcu_spin_unlock(&ns->list_lock);
#endif
    /* XXX wakeup thread. see lrcu_read_unlock */
}
LRCU_EXPORT_SYMBOL(lrcu_call_ns);

/***********************************************************/

void lrcu_call_head_ns(u8 ns_id, struct lrcu_ptr_head *head,
                                    lrcu_destructor_t *destr){
    struct lrcu_handler *h = LRCU_GET_HANDLER();
    struct lrcu_namespace *ns;

    LRCU_ASSERT(h);

    ns = h->ns[ns_id];
    LRCU_ASSERT(ns);

    head->func = destr;
    head->ns_id = ns_id;
    head->version = ns->version;

#ifdef LRCU_LIST_ATOMIC
    lrcu_list_insert_atomic(&ns->free_hlist, &head->list);
#else
    lrcu_spin_lock(&ns->list_hlock);
    lrcu_list_insert(&ns->free_hlist, &head->list);
    lrcu_spin_unlock(&ns->list_hlock);
#endif
}
LRCU_EXPORT_SYMBOL(lrcu_call_head_ns);

/***********************************************************/

/*
Corner cases.
sequence 1:
    write_lock(v = 1)
    assign_ptr
    lrcu_call(p, v = 1)
                            read_lock(v = 1) 
                            deref_ptr(p)                         <---- this cannot happen in correct code(barriers etc.)
                                                worker_thread(p) <---- we here
    write_unlock
                            ...
                            read_unlock
**************************************************************
sequence 2:
    write_lock(v = 1)
                            read_lock(v = 1)
                            deref_ptr(p)
    assign_ptr
    lrcu_call(p, v = 1)
                                                worker_thread(p, v = 1) <---- we here. 
                                                                        1 == 1 => we cannot free it
                                                                        we should wait until counter == 0
    write_unlock
                            ...
                            read_unlock
*/
static inline void __lrcu_get_synchronized(struct lrcu_namespace *ns, lrcu_rangetree_t *rbt){
    struct lrcu_thread_info *ti;
    lrcu_list_t *e, *e_prev;
    u64 current_version;

    current_version = ns->version;
    
    lrcu_spin_lock(&ns->threads_lock);

    /* calculate thread's minimum version */
    lrcu_list_for_each_ptr(ti, e, e_prev, &ns->threads){
        LRCU_TIMER_TYPE *ti_timeval = &ti->timeval[ns->id];
        struct lrcu_local_namespace lns;
        struct lrcu_local_namespace hung_lns;

        lns = *LRCU_GET_LNS(ti, ns);
        hung_lns = *LRCU_GET_HUNG_LNS(ti, ns);
        /* read lns. both counter and version */
        barrier();
        /*
            Calculate thread's safe release version.
            init. lns.version = 0, counter = 0
            thread1 enters and leaves write section, ns->version = 1
            thread2 enters and leaves write section, ns->version = 2, lrcu_call ptr, version = 2
            thread3 enters read section, counter = 1, version = 2
            worker wakes up, reads thread's lns, version = 2, counter = 1,
                            init timeval = now, set hung_lns.version = 2, counter = 1,
            worker wakes up, reads thread's lns, version = 2, counter = 1,
                            reads timeval, checks timeout criteria, not timed out
                            check hung_lns.version against lns.version, if less,
                            thread woke up after last read section, and entered it again,
                            with another ns version, if more or equal, thread either woke up and 
                            entered read section with same verison, or never left previous
                            so hung_lns.version == lns.version, therefore same version,
                            add [hung_lns.version, current_version] to rbt,
                            cannot free ptr with version = 2
            worker wakes up, reads thread's lns, version = 2, counter = 1,
                            reads timeval, checks timeout criteria, timed out,
                            so hung_lns.version == lns.version,
                            add [hung_lns.version, current_version] to rbt,
                            moved thread to hung_threads list
                            set hung_lns.version to current_version
                            cannot free ptr with version = 2
... 10 timutes pased
            worker wakes up, reads thread's lns, version = 2, counter = 1,
                            usual threads list all empty, checking hung_threads list
                            counter = 1 and hung_lns.version == lns.version, still hanging,
                            add [lns.version, hung_lns.version] to rbt,
                            cannot free ptr with version = 2
            thread2 enters and leaves write section, ns->version = 3, lrcu_call ptr, version = 3
            worker wakes up, reads thread's lns, version = 2, counter = 1,
                            usual threads list all empty, checking hung_threads list
                            counter = 1 and hung_lns.version == lns.version, still hanging,
                            add [lns.version, hung_lns.version] to rbt,
                            cannot free ptr with version = 2
                            can free ptr with version = 3
            thread3 leaves read section, counter = 0, version = 2
... thread 3 keeps working with more recent data, but does not considered as such,
    thus data gets freed, so incorrect behaviour.
            thread3 enters read section, counter = 1, version = 3
            worker wakes up, reads thread's lns, version = 3, counter = 1,
                            usual threads list all empty, checking hung_threads list
                            counter = 1 and hung_lns.version < lns.version, 
                            something happened, data moved forward, 
                            remove thread from hung_threads list to usual one
                            add [lns.version, current_version] to rbt,
                            can free ptr with version = 2
            worker wakes up, reads thread's lns, version = 2, counter = 0,
                            nothing to add to lrcu_rangetree
        */
        if(lns.counter != 0){
            lrcu_rangetree_add(rbt, lns.version, current_version);
            /* 
                Handling hung thread case.
                We use hung_lns in usual threads to identify hung threads
                                in hung threads to identify unfeasable version range for thread
             */
            /* we will know that hung thread unhang if we past current_version */

            if(likely(LRCU_TIMER_ISSET(ti_timeval))){ /* hanging timer initialized */
                /* we are the only ones who has access to hung lns */
                if(lns.version <= hung_lns.version){
                    const LRCU_TIMER_TYPE timeout =
                                LRCU_TIMER_INIT(LRCU_HANG_TIMEOUT_S, 0);
                    LRCU_TIMER_TYPE timer_expires;
                    LRCU_TIMER_TYPE now;

                    LRCU_TIMER_ADD(ti_timeval, &timeout, &timer_expires);
                    LRCU_TIMER_GET(&now);
                    if(unlikely(!LRCU_TIMER_CMP(&now, &timer_expires, <))){ /* !< is >= */
                        /* timer expired. thread sleeps too long in read-section 
                            put thread in list of hanging threads and check them separately */

                        LRCU_WARN("hung thread");
                        lrcu_list_unlink_next(&ns->threads, e_prev);
                        lrcu_list_insert(&ns->hung_threads, e);
                        //LRCU_TIMER_CLEAR(&LRCU_GET_LNS(ti, ns)->timeval);
                    }
                }
            }else{
                LRCU_TIMER_GET(ti_timeval);
            }
            lns.version = current_version; /* record version in which we hang */
            *LRCU_GET_HUNG_LNS(ti, ns) = lns;
        }else{ /* counter == 0 */
            /* corner case 2: we have current version + 1 */
            LRCU_TIMER_CLEAR(ti_timeval); /* reset hanging thread timer */
        }
    }

    /* the point of hang thread, that it has hang version, and  */
    lrcu_list_for_each_ptr(ti, e, e_prev, &ns->hung_threads){
        struct lrcu_local_namespace lns, hung_lns;

        hung_lns = *LRCU_GET_HUNG_LNS(ti, ns);
        lns = *LRCU_GET_LNS(ti, ns);
        /* read lns and hung_lns */
        barrier();
        if(lns.version > hung_lns.version || lns.counter == 0){
            /* we have been changed after we hung */
            lrcu_list_unlink_next(&ns->hung_threads, e_prev);
            lrcu_list_insert(&ns->threads, e);
        }

        if(lns.counter != 0){
            if(lns.version > hung_lns.version)
                lrcu_rangetree_add(rbt, lns.version, current_version);
            else
                lrcu_rangetree_add(rbt, lns.version, hung_lns.version);
        }

        /* 
            unfeasable version range is [lns->version, hung_lns->version]
            we can use max version only if all other (usual)threads have 
            either zero counter, or have known hang point(version)
        */
    }
    lrcu_spin_unlock(&ns->threads_lock);
    lrcu_rangetree_optimize(rbt, RANGE_BINTREE_OPTLEVEL_MERGE);
}

static bool lrcu_ns_destructor(struct lrcu_namespace *ns, bool forced){
    struct lrcu_thread_info *ti;
    lrcu_list_t *e, *e_prev;

    lrcu_spin_lock(&ns->threads_lock);
    lrcu_list_for_each_ptr(ti, e, e_prev, &ns->threads){
        if(forced || (ti->lns[ns->id].counter == 0 &&
                    ti->lns[ns->id].version >= ns->version)){
            lrcu_list_unlink_next(&ns->threads, e_prev);
            LRCU_FREE(e);
        }
    }
    if(lrcu_list_empty(&ns->threads)){
        lrcu_spin_unlock(&ns->threads_lock);
        LRCU_FREE(ns);
        return true;
    }
    lrcu_spin_unlock(&ns->threads_lock);
    return false;
}

static inline void *lrcu_worker(void *arg){
    struct lrcu_handler *h = (struct lrcu_handler *)arg;
    lrcu_range_t ranges[LRCU_THREADS_MAX];

    /*
        pointer to ti, so that any ns when added threads, 
        added this one too. this allows to use any ns in destructors
    */
    h->worker_ti = __lrcu_thread_init();
    LRCU_ASSERT(h->worker_ti);

    h->worker_state = LRCU_WORKER_RUNNING;
    /* let initializing thread see that we changed state */
    wmb();

    while(h->worker_state != LRCU_WORKER_STOP && !LRCU_THREAD_SHOULD_STOP()){
        size_t i;

        for(i = 0; i < LRCU_NS_MAX; i++){
            struct lrcu_namespace *ns = h->worker_ns[i];
            if(ns == NULL)
                continue;

            if(!lrcu_list_empty(&ns->free_list)){

#ifdef LRCU_LIST_ATOMIC
                lrcu_list_splice_atomic(&ns->worker_list, &ns->free_list);
#else
                lrcu_spin_lock(&ns->list_lock);
                lrcu_list_splice(&ns->worker_list, &ns->free_list);
                lrcu_spin_unlock(&ns->list_lock);
#endif
            }
            if(!lrcu_list_empty(&ns->free_hlist)){
                //LRCU_LOG("free_hlist not empty\n");

#ifdef LRCU_LIST_ATOMIC
                lrcu_list_splice_atomic(&ns->worker_hlist, &ns->free_hlist);
#else
                lrcu_spin_lock(&ns->list_hlock);
                lrcu_list_splice(&ns->worker_hlist, &ns->free_hlist);
                lrcu_spin_unlock(&ns->list_hlock);
#endif
            }
            if(!lrcu_list_empty(&ns->worker_list) ||
                        !lrcu_list_empty(&ns->worker_hlist)){
                struct lrcu_ptr *ptr;
                lrcu_list_t *n, *n_prev;
                lrcu_rangetree_t rbt = RANGE_BINTREE_INIT(ranges, LRCU_THREADS_MAX);
                //LRCU_LOG("worker not empty\n");

                __lrcu_get_synchronized(ns, &rbt);

                lrcu_list_for_each(n, n_prev, &ns->worker_list){
                    ptr = (struct lrcu_ptr *)n->data;
                    /* do actual job */
                    if(!lrcu_rangetree_find(&rbt, ptr->version)){
                        ptr->deinit(ptr->ptr); /* XXX we could reschedule if we are not ready */
                        lrcu_list_unlink_next(&ns->worker_list, n_prev);
                        LRCU_FREE(n);
                    }
                }
////////////////TODO
                lrcu_list_for_each(n, n_prev, &ns->worker_hlist){
                    struct lrcu_ptr_head *h = container_of(n, struct lrcu_ptr_head, list);
                    /* do actual job */
                    //LRCU_LOG("for each worker_hlist %"PRIu64"\n", h->version);
                    if(!lrcu_rangetree_find(&rbt, h->version)){
                        lrcu_list_unlink_next(&ns->worker_hlist, n_prev);
                        h->func(h);
                    }
                }
                /* barrier for processed version write */
                wmb();
                /* every callback up to min_version has been called. release lrcu_barrier */
                ns->processed_version = lrcu_rangetree_getmin(&rbt);
                if(ns->processed_version == 0)
                    ns->processed_version = ns->version + 1;
            }
            /* make sure we see both ns[] and worker_ns[] */
            rmb();
            /*  h->worker_ns[i] and h->ns[i] could be different values,
                that means ns is freed. our action is to wait for all threads
                either to suspend, or enter and leave lrcu_read section, so
                that we definately know that thread does not have released ns
                The main problem is when thread dereferences h->ns[i],
                he could be rescheduled for undetermined amount of time,
                meanwhile ns destructor could be called, but when that thread
                wakes up, it would access freed memory. This means, that any
                thread could not be trusted to not have pointer to freeing ns,
                until it reaches some state, that would indicate 100% it passing
                that section of code, e.g. (thread_info->lns[i].version >= ns->version)
            */
            if(unlikely(lrcu_list_empty(&ns->worker_list)
                                && lrcu_list_empty(&ns->worker_hlist)
                                && h->worker_ns[i] != h->ns[i])){
                lrcu_spin_lock(&h->ns_lock);
                /* check again under spinlock */
                if(likely(h->worker_ns[i] != h->ns[i]
                        && lrcu_list_empty(&ns->free_list)
                        && lrcu_list_empty(&ns->free_hlist)
                        && lrcu_ns_destructor(h->worker_ns[i], false))){
                    h->worker_ns[i] = NULL;
                    lrcu_spin_unlock(&h->ns_lock);
                    continue;
                }
                lrcu_spin_unlock(&h->ns_lock);
            }

            lrcu_write_barrier_ns(i); /* bump version so that threads do not hang */
            if(lrcu_list_empty(&ns->worker_list) &&
                        lrcu_list_empty(&ns->worker_hlist))
                ns->processed_version = ns->version;
        }
        LRCU_USLEEP(h->worker_timeout);
    }
    lrcu_thread_deinit();
    h->worker_state = LRCU_WORKER_DONE;
    return NULL;
}

/***********************************************************/

void lrcu_synchronize_ns(u8 ns_id){
    struct lrcu_thread_info *ti = LRCU_GET_TI();
    struct lrcu_handler *h = LRCU_GET_HANDLER();
    struct lrcu_namespace *ns;
    lrcu_range_t ranges[LRCU_THREADS_MAX];
    u64 current_version;

    if(ti && LRCU_GET_LNS_ID(ti, ns_id))
        LRCU_ASSERT(LRCU_GET_LNS_ID(ti, ns_id)->counter == 0);

    LRCU_ASSERT(h);

    ns = h->ns[ns_id];
    LRCU_ASSERT(ns);

    current_version = ns->version;
    rmb();
    /* XXX not infinite loop */
    while(1){
        lrcu_rangetree_t rbt = RANGE_BINTREE_INIT(ranges, LRCU_THREADS_MAX);

        __lrcu_get_synchronized(ns, &rbt);

        if(!lrcu_rangetree_find(&rbt, current_version))
            break;
        LRCU_USLEEP(ns->sync_timeout);
    }
}
LRCU_EXPORT_SYMBOL(lrcu_synchronize_ns);

/***********************************************************/

void lrcu_barrier_ns(u8 ns_id){
    struct lrcu_thread_info *ti = LRCU_GET_TI();
    struct lrcu_handler *h = LRCU_GET_HANDLER();
    struct lrcu_namespace *ns;
    u64 current_version;

    lrcu_synchronize_ns(ns_id);

    if(ti && LRCU_GET_LNS_ID(ti, ns_id))
        LRCU_ASSERT(LRCU_GET_LNS_ID(ti, ns_id)->counter == 0);

    LRCU_ASSERT(h);

    ns = h->ns[ns_id];
    LRCU_ASSERT(ns);

    current_version = ns->version;
    /* XXX not infinite loop */
    while(1){
        /* barrier for processed version read */
        rmb();
        if(current_version < ns->processed_version)
            break;
        LRCU_USLEEP(ns->sync_timeout);
    }
}
LRCU_EXPORT_SYMBOL(lrcu_barrier_ns);

/***********************************************************/

/* TODO make it constructor/destructor */
struct lrcu_handler *lrcu_init(void){
    struct lrcu_handler *h = __lrcu_init();

    LRCU_ASSERT(h);
    if(lrcu_ns_init(LRCU_NS_DEFAULT))
        return h;

    /* failed */
    lrcu_deinit();
    return NULL;
}
LRCU_EXPORT_SYMBOL(lrcu_init);

struct lrcu_handler *__lrcu_init(void){
    struct lrcu_handler *h;

    LRCU_TLS_INIT(__lrcu_thread_info);

    h = LRCU_CALLOC(1, sizeof(struct lrcu_handler));
    if(h == NULL)
        return NULL;

    h->worker_state = LRCU_WORKER_RUN;
    /* protect worker_state reads and writes here and in worker */
    mb();

    h->worker_timeout = LRCU_WORKER_SLEEP_US;
    LRCU_SET_HANDLER(h);

    if(LRCU_THREAD_CREATE(&h->worker_tid, lrcu_worker, (void *)h))
        goto out;

    barrier();
    /* wait for worker thread to start */
    while(h->worker_state == LRCU_WORKER_RUN)
        LRCU_USLEEP(1);

    /* worker_state */
    mb();

    if(h->worker_state != LRCU_WORKER_RUNNING)
        goto out;

    return h;
out:
    LRCU_DEL_HANDLER();
    LRCU_FREE(h);
    LRCU_TLS_DEINIT(__lrcu_thread_info);
    return NULL;
}
LRCU_EXPORT_SYMBOL(__lrcu_init);

void lrcu_deinit(void){
    struct lrcu_handler *h = LRCU_GET_HANDLER();

    LRCU_ASSERT(h);

    /* unsafe */
    //lrcu_ns_deinit(LRCU_NS_DEFAULT); TODO

    h->worker_state = LRCU_WORKER_STOP;

    LRCU_THREAD_JOIN(&h->worker_tid);
    LRCU_DEL_HANDLER();

    LRCU_TLS_DEINIT(__lrcu_thread_info);
    /* TODO remove all ns and do something with all ptrs? */
}
LRCU_EXPORT_SYMBOL(lrcu_deinit);

/***********************************************************/

struct lrcu_namespace *lrcu_ns_init(u8 id){
    struct lrcu_namespace *ns = NULL;
    struct lrcu_handler *h = LRCU_GET_HANDLER();

    LRCU_ASSERT(h);

    /* schedule lrcu_thread_set_ns to worker */

    lrcu_spin_lock(&h->ns_lock);
    LRCU_ASSERT(!h->ns[id]);
    LRCU_ASSERT(h->worker_ti);

    if(h->worker_ns[id] != NULL){
        lrcu_list_t *e;
        /* alraedy allocated, but pending removal. nothing to do */
        ns = h->worker_ns[id];
        e = lrcu_list_find_ptr(&ns->threads, h->worker_ti);
        LRCU_ASSERT(e);
        /* how can this be? when removing, we should take lock */
        if(e == NULL){
            /* no need to take threads_lock since we are not working ns */
            if (lrcu_list_add(&ns->threads, h->worker_ti) == NULL){
                lrcu_spin_unlock(&h->ns_lock);
                return NULL;
            }
        }
        /* make sure we add first, only then recreate ns. XXX maybe wmb()? */
        barrier();
        h->ns[id] = ns;
        lrcu_spin_unlock(&h->ns_lock);
        return ns;
    }

    ns = LRCU_CALLOC(1, sizeof(struct lrcu_namespace));
    if(ns == NULL)
        goto out;

    ns->id = id;
    ns->version = 1;
    ns->sync_timeout = LRCU_NS_SYNC_SLEEP_US;
    /* no need to take a lock */
    if (lrcu_list_add(&ns->threads, h->worker_ti) == NULL){
        LRCU_FREE(ns);
        ns = NULL;
        goto out;
    }
    /* first allocate and init, then assign */
    wmb();
    h->ns[id] = ns;
    h->worker_ns[id] = ns;
    wmb(); /* make sure they are seen both. have rmb() on read side */

out:
    lrcu_spin_unlock(&h->ns_lock);
    return ns;
}
LRCU_EXPORT_SYMBOL(lrcu_ns_init);

void lrcu_ns_deinit_safe(u8 id){
    struct lrcu_handler *h = LRCU_GET_HANDLER();
    struct lrcu_namespace *ns;
    
    LRCU_ASSERT(h);

    lrcu_spin_lock(&h->ns_lock);
    ns = h->ns[id];
    LRCU_ASSERT(ns);

    h->ns[id] = NULL;
    /* all threads shall see this pointer now */
    wmb();
    lrcu_write_barrier_ns(id); /* bump version */
    lrcu_spin_unlock(&h->ns_lock);
}
LRCU_EXPORT_SYMBOL(lrcu_ns_deinit_safe);

void lrcu_ns_deinit(u8 id){
    struct lrcu_handler *h = LRCU_GET_HANDLER();
    struct lrcu_namespace *ns;

    LRCU_ASSERT(h);

    lrcu_spin_lock(&h->ns_lock);
    ns = h->ns[id];
    LRCU_ASSERT(ns);

    h->ns[id] = NULL;
    /* all threads shall see this pointer now */
    wmb();

    lrcu_write_barrier_ns(id); /* bump version */
    lrcu_barrier_ns(id); /* wait for all callbacks to execute */
    lrcu_ns_destructor(h->worker_ns[id], true);
    barrier();
    h->worker_ns[id] = NULL;

    lrcu_spin_unlock(&h->ns_lock);
}
LRCU_EXPORT_SYMBOL(lrcu_ns_deinit);

struct lrcu_thread_info *lrcu_thread_init(void){
    struct lrcu_thread_info *ti = __lrcu_thread_init();

    LRCU_ASSERT(ti);

    if(!lrcu_thread_set_ns(LRCU_NS_DEFAULT)){
        lrcu_thread_deinit();
        return NULL;
    }

    return ti;
}
LRCU_EXPORT_SYMBOL(lrcu_thread_init);

struct lrcu_thread_info *__lrcu_thread_init(void){
    struct lrcu_thread_info *ti = NULL;
    struct lrcu_handler *h = LRCU_GET_HANDLER();

    LRCU_ASSERT(h);

    ti = LRCU_CALLOC(1, sizeof(struct lrcu_thread_info));
    if(ti == NULL)
        return NULL;

    ti->h = h;
    LRCU_SET_TI(ti);

    return ti;
}
LRCU_EXPORT_SYMBOL(__lrcu_thread_init);

bool lrcu_thread_set_ns(u8 ns_id){
    struct lrcu_thread_info *ti = LRCU_GET_TI();
    struct lrcu_handler *h = LRCU_GET_HANDLER();
    struct lrcu_namespace *ns;
    void *ret;

    LRCU_ASSERT(h);
    LRCU_ASSERT(ti);

    lrcu_spin_lock(&h->ns_lock);
    ns = h->ns[ns_id];
    LRCU_ASSERT(ns);
    lrcu_spin_unlock(&h->ns_lock);

    /* locking in spinlock. careful of deadlock */
    lrcu_spin_lock(&ns->threads_lock);
    ret = lrcu_list_add(&ns->threads, ti);
    lrcu_spin_unlock(&ns->threads_lock);

    return ret != NULL;
}
LRCU_EXPORT_SYMBOL(lrcu_thread_set_ns);

/* assume ns lock taken and ti and ti->h non-null */
static bool thread_remove_from_ns(struct lrcu_thread_info *ti, u8 ns_id){
    struct lrcu_namespace *ns = ti->h->worker_ns[ns_id];
    lrcu_list_t *e;
    int found = false;

    LRCU_ASSERT(ns);

    lrcu_spin_lock(&ns->threads_lock); /* locking in spinlock. careful of deadlock */
    e = lrcu_list_find_ptr_unlink(&ns->threads, ti);
    if(e){
        found = true;
        LRCU_FREE(e);
    }
    lrcu_spin_unlock(&ns->threads_lock);
    if(!found){
        /* failed to set thread's ns, then exited thread. */
    }
    return found;
}

bool lrcu_thread_del_ns(u8 ns_id){
    struct lrcu_thread_info *ti = LRCU_GET_TI();
    bool ret;

    LRCU_ASSERT(ti);
    LRCU_ASSERT(ti->h);

    lrcu_spin_lock(&ti->h->ns_lock);
    ret = thread_remove_from_ns(ti, ns_id);
    lrcu_spin_unlock(&ti->h->ns_lock);

    return ret;
}
LRCU_EXPORT_SYMBOL(lrcu_thread_del_ns);

static void thread_destructor_callback(struct lrcu_thread_info *ti){
    struct lrcu_handler *h = ti->h;
    int found = false;
    int i;

    LRCU_ASSERT(ti);
    LRCU_ASSERT(h);

    lrcu_spin_lock(&h->ns_lock);
    for(i = 0; i < LRCU_NS_MAX; i++){
        found = found || thread_remove_from_ns(ti, i);
    }
    lrcu_spin_unlock(&h->ns_lock);
    if(!found){
        /* failed to set thread's ns, then exited thread. */
    }
    LRCU_FREE(ti);
}

#if 0
void lrcu_thread_deinit(void){
    struct lrcu_thread_info *ti = LRCU_GET_TI();
    if(ti == NULL)
        return;

    /* remove from all namespaces */
    __lrcu_call_ns(LRCU_NS_DEFAULT, (void *)ti, (lrcu_destructor_t *)thread_destructor_callback);
    LRCU_DEL_TI(ti);
    /* wait for it to process, since TLS gets deleted with the thread */
    lrcu_barrier_ns(LRCU_NS_DEFAULT);
}
#endif

void lrcu_thread_deinit(void){
    struct lrcu_thread_info *ti = LRCU_GET_TI();

    LRCU_ASSERT(ti);

    if(ti)
        thread_destructor_callback(ti);
    LRCU_DEL_TI(ti);
}
LRCU_EXPORT_SYMBOL(lrcu_thread_deinit);

/* already allocated ptr */
void lrcu_ptr_init(struct lrcu_ptr *ptr, u8 ns_id, 
                                lrcu_destructor_t *deinit){
    ptr->ns_id = ns_id;
    ptr->deinit = deinit;
    ptr->ptr = NULL;
}
LRCU_EXPORT_SYMBOL(lrcu_ptr_init);

/* getters for private fields */
struct lrcu_namespace *lrcu_ti_get_ns(struct lrcu_thread_info *ti, u8 id){
    LRCU_ASSERT(ti);
    LRCU_ASSERT(ti->h);
    return ti->h->ns[id];
}

struct lrcu_namespace *lrcu_get_ns(struct lrcu_handler *h, u8 id){
    LRCU_ASSERT(h);
    return h->ns[id];
}

LRCU_OS_API_DEFINE
