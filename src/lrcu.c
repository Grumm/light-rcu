/*
    Lazy RCU: extra light read section and hard write section
    locks taken only on lrcu_ptr  
*/

#include "list.h"
#include "atomics.h"
#include "lrcu.h"
#include "lrcu_internal.h"
#include "range_bintree.h"

LRCU_TLS_DEFINE(struct lrcu_handler *, __lrcu_handler);
LRCU_TLS_DEFINE(struct lrcu_thread_info *, __lrcu_thread_info);

/***********************************************************/

static inline void __lrcu_write_barrier(struct lrcu_namespace *ns){
    barrier();
    ns->version++;
    barrier(); /* new ptr's should be seen only with new version */
}

void lrcu_write_barrier_ns(u8 ns_id){
    struct lrcu_thread_info *ti = LRCU_GET_TI();

    if(unlikely(ti == NULL || ti->h == NULL || ti->h->ns[ns_id] == NULL)){
        LRCU_BUG();
        return;
    }

    __lrcu_write_barrier(ti->h->ns[ns_id]);
}

/***********************************************************/

static inline void __lrcu_write_lock(struct lrcu_namespace *ns){
    spin_lock(&ns->write_lock);
    __lrcu_write_barrier(ns);
}

void lrcu_write_lock_ns(u8 ns_id){
    struct lrcu_thread_info *ti = LRCU_GET_TI();

    if(unlikely(ti == NULL || ti->h == NULL || ti->h->ns[ns_id] == NULL)){
        LRCU_BUG();
        return;
    }

    __lrcu_write_lock(ti->h->ns[ns_id]);
}

/***********************************************************/

void __lrcu_assign_pointer(void **pp, void *newptr){
    if(newptr != NULL)
        wmb();
    *pp = newptr;
}

void lrcu_assign_ptr(struct lrcu_ptr *ptr, void *newptr,
                        u8 ns_id, lrcu_destructor_t *callback){
    wmb();
    ptr->ptr = newptr;
    ptr->ns_id = ns_id;
    ptr->deinit = callback;
}

/***********************************************************/

static inline void __lrcu_write_unlock(struct lrcu_namespace *ns){
    spin_unlock(&ns->write_lock);
    __lrcu_write_barrier(ns);
}

void lrcu_write_unlock_ns(u8 ns_id){
    struct lrcu_thread_info *ti = LRCU_GET_TI();

    if(unlikely(ti == NULL || ti->h == NULL || ti->h->ns[ns_id] == NULL)){
        LRCU_BUG();
        return;
    }

    __lrcu_write_unlock(ti->h->ns[ns_id]);
}

/***********************************************************/

static inline void __lrcu_read_lock(struct lrcu_thread_info *ti, struct lrcu_namespace *ns){
    if(ti == NULL)
        return;
    
    LRCU_GET_LNS(ti, ns)->counter++; /* can be nested! */
    barrier(); /* make sure counter changed first, and only after 
                            that version. see worker thread read order */
    /* only first entrance in read section matters */
    if(likely(LRCU_GET_LNS(ti, ns)->counter == 1)){
        LRCU_GET_LNS(ti, ns)->version = ns->version;
        /* say we entered read section with this ns version */
        /* barrier for worker thread to see new version */
        wmb();
    }
}

void lrcu_read_lock_ns(u8 ns_id){
    struct lrcu_thread_info *ti = LRCU_GET_TI();

    if(unlikely(ti == NULL || ti->h == NULL)){
        LRCU_BUG();
        return;
    }

    __lrcu_read_lock(ti, ti->h->ns[ns_id]);
}

/***********************************************************/

void *__lrcu_dereference_pointer(void **ptr){
    void *p = ACCESS_ONCE(*ptr);

    rmb_depends();
    return p;
}

void *lrcu_dereference_ptr(struct lrcu_ptr *ptr){
    void *p = ACCESS_ONCE(ptr->ptr);

    rmb_depends();
    return p;
}

/***********************************************************/

static inline void __lrcu_read_unlock(struct lrcu_thread_info *ti, struct lrcu_namespace *ns){
    if(unlikely(ti == NULL || ns == NULL)){
        LRCU_BUG();
        return;
    }
    if(LRCU_GET_LNS(ti, ns)->counter != 1){
        LRCU_GET_LNS(ti, ns)->counter--;
    }else{
        /* between protected data access and actual destruction of the object */
        barrier();
        LRCU_GET_LNS(ti, ns)->counter--;
        if(LRCU_GET_LNS(ti, ns)->version != ns->version){
            /* 
            XXX notify thread that called synchronize() that we are done.
            In current implementation I see this can be omitted since 
            we can use lazy checks in synchronize(), but if not, might wanna use 
            per-ns/per-thread pthread condition variable
            */
        }
        barrier();
    }
    assert(LRCU_GET_LNS(ti, ns)->counter >= 0);
}

void lrcu_read_unlock_ns(u8 ns_id){
    struct lrcu_thread_info *ti = LRCU_GET_TI();

    if(unlikely(ti == NULL || ti->h == NULL)){
        LRCU_BUG();
        return;
    }

    __lrcu_read_unlock(ti, ti->h->ns[ns_id]);
}

/***********************************************************/

static inline void __lrcu_call(struct lrcu_namespace *ns, 
                            void *p, lrcu_destructor_t *destr){
    struct lrcu_ptr local_ptr = {
        .deinit = destr,
        .ptr = p,
    };

    if(unlikely(ns == NULL)){
        LRCU_BUG();
        destr(p);
        return;
    }

    local_ptr.version = ns->version, /* synchronize() will be called on this version */

    spin_lock(&ns->list_lock);

                            /* NOT A POINTER!!! */
    list_add(&ns->free_list, local_ptr);
    spin_unlock(&ns->list_lock);
    /* XXX wakeup thread. see lrcu_read_unlock */
}

void lrcu_call_ns(u8 ns_id, void *p, lrcu_destructor_t *destr){
    struct lrcu_handler *h = LRCU_GET_HANDLER();

    if(unlikely(h == NULL)){
        LRCU_BUG();
        destr(p);
    }else{
        __lrcu_call(h->ns[ns_id], p, destr);
    }
}

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
static inline void __lrcu_get_synchronized(struct lrcu_namespace *ns, struct range_bintree *rbt){
    struct lrcu_thread_info *ti;
    list_t *e, *next;
    u64 current_version;

    current_version = ns->version;
    
    spin_lock(&ns->threads_lock);

    /* calculate thread's minimum version */
    list_for_each_ptr(ti, e, next, &ns->threads){
        struct timeval *ti_timeval = &ti->timeval[ns->id];
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
                            nothing to add to range_bintree
        */
        if(lns.counter != 0){
            range_bintree_add(rbt, lns.version, current_version);
            /* 
                Handling hung thread case.
                We use hung_lns in usual threads to identify hung threads
                                in hung threads to identify unfeasable version range for thread
             */
            /* we will know that hung thread unhang if we past current_version */

            if(likely(timerisset(ti_timeval))){ /* hanging timer initialized */
                /* we are the only ones who has access to hung lns */
                if(lns.version <= hung_lns.version){
                    static const struct timeval timeout = { .tv_sec = LRCU_HANG_TIMEOUT_S,
                                                            .tv_usec = 0, };
                    struct timeval timer_expires;
                    struct timeval now;

                    timeradd(ti_timeval, &timeout, &timer_expires);
                    gettimeofday(&now, NULL);
                    if(unlikely(!timercmp(&now, &timer_expires, <))){ /* !< is >= */
                        /* timer expired. thread sleeps too long in read-section 
                            put thread in list of hanging threads and check them separately */

                        LRCU_WARN("hung thread");
                        list_unlink(&ns->threads, e);
                        list_insert(&ns->hung_threads, e);
                        //timerclear(&LRCU_GET_LNS(ti, ns)->timeval);
                    }
                }
            }else{
                gettimeofday(ti_timeval, NULL);
            }
            lns.version = current_version; /* record version in which we hang */
            *LRCU_GET_HUNG_LNS(ti, ns) = lns;
        }else{ /* counter == 0 */
            /* corner case 2: we have current version + 1 */
            timerclear(ti_timeval); /* reset hanging thread timer */
        }
    }

    /* the point of hang thread, that it has hang version, and  */
    list_for_each_ptr(ti, e, next, &ns->hung_threads){
        struct lrcu_local_namespace lns, hung_lns;

        hung_lns = *LRCU_GET_HUNG_LNS(ti, ns);
        lns = *LRCU_GET_LNS(ti, ns);
        /* read lns and hung_lns */
        barrier();
        if(lns.version > hung_lns.version || lns.counter == 0){
            /* we have been changed after we hung */
            list_unlink(&ns->hung_threads, e);
            list_insert(&ns->threads, e);
        }

        if(lns.counter != 0){
            if(lns.version > hung_lns.version)
                range_bintree_add(rbt, lns.version, current_version);
            else
                range_bintree_add(rbt, lns.version, hung_lns.version);
        }

        /* 
            unfeasable version range is [lns->version, hung_lns->version]
            we can use max version only if all other (usual)threads have 
            either zero counter, or have known hang point(version)
        */
    }
    spin_unlock(&ns->threads_lock);
    range_bintree_optimize(rbt, RANGE_BINTREE_OPTLEVEL_MERGE);
}

static bool lrcu_ns_destructor(struct lrcu_namespace *ns, bool forced){
    struct lrcu_thread_info *ti;
    list_t *e, *next;

    spin_lock(&ns->threads_lock);
    list_for_each_ptr(ti, e, next, &ns->threads){
        if(forced || (ti->lns[ns->id].counter == 0 &&
                    ti->lns[ns->id].version >= ns->version)){
            list_unlink(&ns->threads, e);
            LRCU_FREE(e);
        }
    }
    if(list_empty(&ns->threads)){
        spin_unlock(&ns->threads_lock);
        LRCU_FREE(ns);
        return true;
    }
    spin_unlock(&ns->threads_lock);
    return false;
}

static inline void *lrcu_worker(void *arg){
    struct lrcu_handler *h = (struct lrcu_handler *)arg;
    struct range ranges[LRCU_THREADS_MAX];

    /*
        pointer to ti, so that any ns when added threads, 
        added this one too. this allows to use any ns in destructors
    */
    h->worker_ti = __lrcu_thread_init();
    if(h->worker_ti == NULL)
        LRCU_BUG();
    h->worker_state = LRCU_WORKER_RUNNING;
    /* let initializing thread see that we changed state */
    wmb();

    while(h->worker_state != LRCU_WORKER_STOP){
        size_t i;

        for(i = 0; i < LRCU_NS_MAX; i++){
            struct lrcu_namespace *ns = h->worker_ns[i];
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
                struct range_bintree rbt = RANGE_BINTREE_INIT(ranges, LRCU_THREADS_MAX);

                __lrcu_get_synchronized(ns, &rbt);

                list_for_each(n, next, &ns->worker_list){
                    ptr = (struct lrcu_ptr *)n->data;
                    /* do actual job */
                    if(!range_bintree_find(&rbt, ptr->version)){
                        ptr->deinit(ptr->ptr); /* XXX we could reschedule if we are not ready */
                        list_unlink(&ns->worker_list, n);
                        free(n);
                    }
                }
                /* barrier for processed version write */
                wmb();
                /* every callback up to min_version has been called. release lrcu_barrier */
                ns->processed_version = range_bintree_getmin(&rbt);
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
            if(unlikely(list_empty(&ns->worker_list)
                                && h->worker_ns[i] != h->ns[i])){
                spin_lock(&h->ns_lock);
                /* check again under spinlock */
                if(likely(h->worker_ns[i] != h->ns[i]
                        && list_empty(&ns->free_list)
                        && lrcu_ns_destructor(h->worker_ns[i], false))){
                    h->worker_ns[i] = NULL;
                    spin_unlock(&h->ns_lock);
                    continue;
                }
                spin_unlock(&h->ns_lock);
            }
            if(!list_empty(&ns->worker_list)){
                __lrcu_write_barrier(ns); /* bump version so that threads do not hang */
            }
        }
        usleep(h->worker_timeout);
    }
    lrcu_thread_deinit();
    h->worker_state = LRCU_WORKER_DONE;
    return NULL;
}

/***********************************************************/

static inline void __lrcu_synchronize(struct lrcu_namespace *ns){
    struct range ranges[LRCU_THREADS_MAX];
    u64 current_version;

    if(unlikely(ns == NULL)){
        LRCU_BUG();
        return;
    }

    current_version = ns->version;
    rmb();
    while(1){
        struct range_bintree rbt = RANGE_BINTREE_INIT(ranges, LRCU_THREADS_MAX);

        __lrcu_get_synchronized(ns, &rbt);

        if(!range_bintree_find(&rbt, current_version))
            break;
        usleep(ns->sync_timeout);
    }
}

void lrcu_synchronize_ns(u8 ns_id){
    struct lrcu_thread_info *ti = LRCU_GET_TI();

    if(unlikely(ti == NULL || ti->h == NULL)){
        LRCU_BUG();
        return;
    }
    if(unlikely(LRCU_GET_LNS_ID(ti, ns_id)->counter != 0)){
        LRCU_BUG();
    }

    __lrcu_synchronize(ti->h->ns[ns_id]);
}

/***********************************************************/

static inline void __lrcu_barrier(struct lrcu_namespace *ns){
    u64 current_version;

    if(unlikely(ns == NULL)){
        LRCU_BUG();
        return;
    }

    current_version = ns->version;
    while(1){
        /* barrier for processed version read */
        rmb();
        if(current_version < ns->processed_version)
            break;
        usleep(ns->sync_timeout);
    }
}

void lrcu_barrier_ns(u8 ns_id){
    struct lrcu_thread_info *ti = LRCU_GET_TI();

    if(unlikely(ti == NULL || ti->h == NULL)){
        LRCU_BUG();
        return;
    }
    if(unlikely(LRCU_GET_LNS_ID(ti, ns_id)->counter != 0)){
        LRCU_BUG();
    }

    __lrcu_barrier(ti->h->ns[ns_id]);
}

/***********************************************************/

struct lrcu_handler *lrcu_init(void){
    struct lrcu_handler *h = __lrcu_init();

    if(h == NULL)
        return NULL;

    if(lrcu_ns_init(LRCU_NS_DEFAULT))
        return h;

    /* failed */
    lrcu_deinit();
    return NULL;
}

struct lrcu_handler *__lrcu_init(void){
    struct lrcu_handler *h;

    LRCU_TLS_INIT(__lrcu_handler);
    LRCU_TLS_INIT(__lrcu_thread_info);

    h = LRCU_CALLOC(1, sizeof(struct lrcu_handler));
    if(h == NULL)
        return NULL;

    h->worker_state = LRCU_WORKER_RUN;
    /* protect worker_state reads and writes here and in worker */
    mb();

    h->worker_timeout = LRCU_WORKER_SLEEP_US;
    LRCU_SET_HANDLER(h);

    if(LRCU_THREAD_CREATE(&h->worker_tid, NULL, lrcu_worker, (void *)h))
        goto out;

    barrier();
    /* wait for worker thread to start */
    while(h->worker_state == LRCU_WORKER_RUN)
        cpu_relax();

    /* worker_state */
    mb();

    if(h->worker_state != LRCU_WORKER_RUNNING)
        goto out;

    return h;
out:
    LRCU_DEL_HANDLER(h);
    LRCU_FREE(h);
    LRCU_TLS_DEINIT(__lrcu_thread_info);
    LRCU_TLS_DEINIT(__lrcu_handler);
    return NULL;
}

void lrcu_deinit(void){
    struct lrcu_handler *h = LRCU_GET_HANDLER();

    if(h == NULL)
        return;

    h->worker_state = LRCU_WORKER_STOP;
    LCRU_THREAD_JOIN(h->worker_tid, NULL);
    LRCU_DEL_HANDLER(h);

    LRCU_TLS_DEINIT(__lrcu_thread_info);
    LRCU_TLS_DEINIT(__lrcu_handler);
    /* TODO remove all ns and do something with all ptrs? */
}

/***********************************************************/

struct lrcu_namespace *lrcu_ns_init(u8 id){
    struct lrcu_namespace *ns = NULL;
    struct lrcu_handler *h = LRCU_GET_HANDLER();

    if(h == NULL)
        return NULL;

    /* schedule lrcu_thread_set_ns to worker */

    spin_lock(&h->ns_lock);
    if(h->ns[id]){
        LRCU_BUG();
        goto out;
    }

    if(h->worker_ti == NULL)
        LRCU_BUG();

    if(h->worker_ns[id] != NULL){
        /* alraedy allocated, but pending removal. nothing to do */
        ns = h->worker_ns[id];
        list_t *e = list_find_ptr(&ns->threads, h->worker_ti);
        if(e == NULL){
            LRCU_BUG(); /* how can this be? when removing, we should take lock */
            /* no need to take threads_lock since we are not working ns */
            if (list_add(&ns->threads, h->worker_ti) == NULL){
                spin_unlock(&h->ns_lock);
                return NULL;
            }
        }
        /* make sure we add first, only then recreate ns. XXX maybe wmb()? */
        barrier();
        h->ns[id] = ns;
        spin_unlock(&h->ns_lock);
        return ns;
    }

    ns = LRCU_CALLOC(1, sizeof(struct lrcu_namespace));
    if(ns == NULL)
        goto out;

    ns->id = id;
    ns->sync_timeout = LRCU_NS_SYNC_SLEEP_US;
    /* no need to take a lock */
    if (list_add(&ns->threads, h->worker_ti) == NULL){
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
    spin_unlock(&h->ns_lock);
    return ns;
}

void lrcu_ns_deinit_safe(u8 id){
    struct lrcu_handler *h = LRCU_GET_HANDLER();
    struct lrcu_namespace *ns;

    if(h == NULL){
        LRCU_BUG();
        return;
    }

    spin_lock(&h->ns_lock);
    ns = h->ns[id];
    if(ns == NULL){
        LRCU_BUG();
        spin_unlock(&h->ns_lock);
        return;
    }

    h->ns[id] = NULL;
    /* all threads shall see this pointer now */
    wmb();
    lrcu_write_barrier_ns(id); /* bump version */
    spin_unlock(&h->ns_lock);
}

void lrcu_ns_deinit(u8 id){
    struct lrcu_handler *h = LRCU_GET_HANDLER();
    struct lrcu_namespace *ns;

    if(h == NULL){
        LRCU_BUG();
        return;
    }

    spin_lock(&h->ns_lock);
    ns = h->ns[id];
    if(ns == NULL){
        LRCU_BUG();
        spin_unlock(&h->ns_lock);
        return;
    }

    h->ns[id] = NULL;
    /* all threads shall see this pointer now */
    wmb();

    lrcu_write_barrier_ns(id); /* bump version */
    lrcu_barrier_ns(id); /* wait for all callbacks to execute */
    lrcu_ns_destructor(h->worker_ns[id], true);
    barrier();
    h->worker_ns[id] = NULL;

    spin_unlock(&h->ns_lock);
}

struct lrcu_thread_info *lrcu_thread_init(void){
    struct lrcu_thread_info *ti = __lrcu_thread_init();

    if(ti == NULL)
        return NULL;

    if(!lrcu_thread_set_ns(LRCU_NS_DEFAULT)){
        lrcu_thread_deinit();
        return NULL;
    }

    return ti;
}

struct lrcu_thread_info *__lrcu_thread_init(void){
    struct lrcu_thread_info *ti = NULL;
    struct lrcu_handler *h = LRCU_GET_HANDLER();

    if(h == NULL)
        return NULL;

    ti = LRCU_CALLOC(1, sizeof(struct lrcu_thread_info));
    if(ti == NULL)
        return NULL;

    ti->h = h;
    LRCU_SET_TI(ti);

    return ti;
}

bool lrcu_thread_set_ns(u8 ns_id){
    struct lrcu_thread_info *ti = LRCU_GET_TI();
    struct lrcu_namespace *ns;
    void *ret;

    //LRCU_BUG();
    /* control code does not need BUG() since it returns error code */
    if(ti == NULL || ti->h == NULL)
        return false;

    spin_lock(&ti->h->ns_lock);
    ns = ti->h->ns[ns_id];
    if(ns == NULL){
        spin_unlock(&ti->h->ns_lock);
        return false;
    }

    /* locking in spinlock. careful of deadlock */
    spin_lock(&ns->threads_lock);
    ret = list_add(&ns->threads, ti);
    spin_unlock(&ns->threads_lock);
    spin_unlock(&ti->h->ns_lock);

    return ret != NULL;
}

/* assume ns lock taken and ti and ti->h non-null */
static bool thread_remove_from_ns(struct lrcu_thread_info *ti, u8 ns_id){
    struct lrcu_namespace *ns = ti->h->worker_ns[ns_id];
    list_t *e;
    int found = false;

    if(unlikely(ns == NULL)){
        LRCU_BUG();
        return false;
    }

    spin_lock(&ns->threads_lock); /* locking in spinlock. careful of deadlock */
    e = list_find_ptr(&ns->threads, ti);
    if(e != NULL){
        list_unlink(&ns->threads, e);
        found = true;
    }
    spin_unlock(&ns->threads_lock);
    if(!found){
        /* failed to set thread's ns, then exited thread. */
    }
    return found;
}

bool lrcu_thread_del_ns(u8 ns_id){
    struct lrcu_thread_info *ti = LRCU_GET_TI();
    bool ret;

    //LRCU_BUG();
    /* control code does not need BUG() since it returns error code */
    if(ti == NULL || ti->h == NULL)
        return false;

    spin_lock(&ti->h->ns_lock);
    ret = thread_remove_from_ns(ti, ns_id);
    spin_unlock(&ti->h->ns_lock);

    return ret;
}

static void thread_destructor_callback(struct lrcu_thread_info *ti){
    struct lrcu_handler *h = ti->h;
    int found = false;
    int i;

    if(unlikely(ti == NULL || h == NULL)){
        LRCU_BUG();
        return;
    }

    spin_lock(&h->ns_lock);
    for(i = 0; i < LRCU_NS_MAX; i++){
        found = found || thread_remove_from_ns(ti, i);
    }
    spin_unlock(&h->ns_lock);
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
    if(ti == NULL)
        return;
    thread_destructor_callback(ti);
    LRCU_DEL_TI(ti);
}

/* already allocated ptr */
void __lrcu_ptr_init(struct lrcu_ptr *ptr, u8 ns_id, 
                                lrcu_destructor_t *deinit){
    ptr->ns_id = ns_id;
    ptr->deinit = deinit;
}

/* getters for private fields */
struct lrcu_namespace *lrcu_ti_get_ns(struct lrcu_thread_info *ti, u8 id){
    if(ti == NULL || ti->h == NULL)
        return NULL;
    return ti->h->ns[id];
}

struct lrcu_namespace *lrcu_get_ns(struct lrcu_handler *h, u8 id){
    return h->ns[id];
}