/*
    Lazy RCU: extra light read section and hard write section
    locks taken only on lrcu_ptr  
*/

#include <unistd.h>
#include <assert.h>

#include "types.h"
#include "list.h"
#include "atomics.h"
#include "lrcu.h"
#include "lrcu_internal.h"

struct lrcu_handler *__lrcu_handler = NULL;
__thread struct lrcu_thread_info *__lrcu_thread_info = NULL;

/***********************************************************/

static inline void __lrcu_write_lock(struct lrcu_namespace *ns){
    spin_lock(&ns->write_lock);
    ns->version++;
    barrier(); /* new ptr's should be seen only with new version */
}

void lrcu_write_lock_ns(u8 ns_id){
    struct lrcu_thread_info *ti = LRCU_GET_TI();

    if(unlikely(ti == NULL || ti->h == NULL || ti->h->ns[ns_id] == NULL)){
        LRCU_BUG();
        return;
    }

    __lrcu_write_lock(ti->h->ns[ns_id]);
}

/* TODO fix */
void lrcu_assign_pointer(struct lrcu_ptr *ptr, void *newptr){
    struct lrcu_ptr old_ptr;

    old_ptr = *ptr;
    ptr->ptr = newptr;
    wmb();

    lrcu_call(&old_ptr, ptr->deinit);
    /* add old_ptr to free_queue and wake up lrcu_worker thread */
}

static inline void __lrcu_write_unlock(struct lrcu_namespace *ns){
    spin_unlock(&ns->write_lock);
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

/* quite pointless function...for now TODO */
void *lrcu_read_dereference_pointer(struct lrcu_ptr *ptr){
    struct lrcu_ptr local_ptr;
    barrier();
    local_ptr = *ptr;
    /* we access thread-local ns-local variable, so no need for atomic access */
    barrier();

    return local_ptr.ptr;
}

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

void __lrcu_call_ns(u8 ns_id, void *p, lrcu_destructor_t *destr){
    struct lrcu_handler *h = LRCU_GET_HANDLER();

    if(unlikely(h == NULL)){
        LRCU_BUG();
        destr(p);
    }else{
        __lrcu_call(h->ns[ns_id], p, destr);
    }
}

/* what about when version wraps -1? at least after 143 years on 4Ghz CPU in ticks :) */

/*
Corner cases.
Here's the sequence 1:
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
Here's the sequence 2:
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

/* return range in which pointers could not be released: [vmin, vmax] */
static inline void __lrcu_get_synchronized(struct lrcu_namespace *ns, u64 *vmin, u64 *vmax){
    struct lrcu_thread_info *ti;
    list_t *e, *next;
    u64 min_version, max_version, current_version;
    int threads_with_non_zero_counter = 0;

    current_version = ns->version;
    min_version = current_version + 1; /* +1 used to handle special case 2 */
    max_version = 0;

    /* read ns version */
    rmb();
    
    spin_lock(&ns->threads_lock);

    /* calculate thread's minimum version */
    list_for_each(e, next, &ns->threads){
        struct lrcu_local_namespace lns;
        char *cp = e->data; /* hack with strict aliasing */

        ti = *((struct lrcu_thread_info **)cp);

        lns = *LRCU_GET_LNS(ti, ns);
        /* read lns. both counter and version */
        rmb();
        if(lns.counter != 0){
            threads_with_non_zero_counter = 1;

            /* looking for minimum thread's version */
            if(lns.version < min_version){ /* what if ==? can we free it? */
                min_version = lns.version;
            }

            /* 
                Handling hung thread case.
                We use hung_lns in usual threads to identify hung threads
                                in hung threads to identify unfeasable version range for thread

             */
            if(likely(timerisset(&lns.timeval))){ /* hanging timer uninitialized */
                static const struct timeval timer = { .tv_sec = LRCU_HANG_TIMEOUT_S, .tv_usec = 0, };
                struct timeval timer_expires;
                struct timeval now;
                timeradd(&lns.timeval, &timer, &timer_expires);
                gettimeofday(&now, NULL);

                /* we are the only ones who has access to hung lns */
                if(lns.version != LRCU_GET_HUNG_LNS(ti, ns)->version){ /* we actually not hang! */
                    /* reset timer and version */
                    *LRCU_GET_HUNG_LNS(ti, ns) = lns;
                    gettimeofday(&LRCU_GET_LNS(ti, ns)->timeval, NULL);
                }else if(unlikely(!timercmp(&now, &timer_expires, <))){ /* !< is >= */
                    /* timer expired. thread sleeps too long in read-section 
                        put thread in list of hanging threads and check them separately */
                    *LRCU_GET_HUNG_LNS(ti, ns) = lns;
                    LRCU_GET_HUNG_LNS(ti, ns)->version = current_version; /* record version in which we hang */
                    /* we will know that hung thread unhang if we past current_version */

                    list_unlink(&ns->threads, e);
                    list_insert(&ns->hung_threads, e);
                    //timerclear(&LRCU_GET_LNS(ti, ns)->timeval);
                    continue;
                }
            }else{ /* we may be hang right here */
                gettimeofday(&LRCU_GET_LNS(ti, ns)->timeval, NULL);
                *LRCU_GET_HUNG_LNS(ti, ns) = lns;
            }
        }else{ /* counter == 0 */
            /* corner case 2: we have current version + 1 */
            timerclear(&LRCU_GET_LNS(ti, ns)->timeval); /* reset hanging thread timer */
        }
    }

    /* the point of hang thread, that it has hang version, and  */
    list_for_each(e, next, &ns->hung_threads){
        struct lrcu_local_namespace lns, hung_lns;
        char *cp = e->data;

        ti = *((struct lrcu_thread_info **)cp);

        hung_lns = *LRCU_GET_HUNG_LNS(ti, ns);
        lns = *LRCU_GET_LNS(ti, ns);
        /* read lns and hung_lns */
        rmb();
        if(lns.version > hung_lns.version || lns.counter == 0){
            /* we have been changed after we hung */
            list_unlink(&ns->hung_threads, e);
            list_insert(&ns->threads, e);
        }

        /* 
            unfeasable version range is [lns->version, hung_lns->version]
            we can use max version only if all other (usual)threads have 
            either zero counter, or have known hang point(version)
        */
        if(!threads_with_non_zero_counter && hung_lns.version > max_version)
            max_version =  hung_lns.version;
        /* same as usial thread. only if counter != 0 */
        if(lns.counter != 0 && lns.version < min_version)
            min_version =  lns.version;
    }
    spin_unlock(&ns->threads_lock);

    if(max_version == 0){ /* unmodified */
        max_version = current_version + 1;
    }
    /* some kind of bug */
    if(min_version > max_version){
        LRCU_BUG();
    }

    *vmin = min_version;
    *vmax = max_version;
}

static inline void *lrcu_worker(void *arg){
    struct lrcu_handler *h = (struct lrcu_handler *)arg;

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
            struct lrcu_namespace *ns = h->ns[i];
            if(ns == NULL)
                continue;

            if(!list_empty(&ns->free_list)){
                spin_lock(&ns->list_lock); /* locking in spinlock. careful of deadlock */
                list_splice(&ns->worker_list, &ns->free_list);
                spin_unlock(&ns->list_lock);
            }
            if(!list_empty(&ns->worker_list)){
                struct lrcu_ptr *ptr;
                list_t *n, *next;
                u64 min_version, max_version;

                __lrcu_get_synchronized(ns, &min_version, &max_version);

                list_for_each(n, next, &ns->worker_list){
                    ptr = (struct lrcu_ptr *)n->data;
                    /* do actual job */
                    if(ptr->version < min_version || ptr->version > max_version){
                        ptr->deinit(ptr->ptr); /* XXX we could reschedule if we are not ready */
                        list_unlink(&ns->worker_list, n);
                        free(n);
                    }
                }
                /* barrier for processed version write */
                wmb();
                /* every callback up to min_version has been called. release lrcu_barrier */
                ns->processed_version = min_version;
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
    u64 current_version;

    if(unlikely(ns == NULL)){
        LRCU_BUG();
        return;
    }

    current_version = ns->version;
    rmb();
    while(1){
        u64 min_version, max_version;

        __lrcu_get_synchronized(ns, &min_version, &max_version);

        if(current_version < min_version || current_version > max_version)
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

    lrcu_deinit();
    return NULL;
}

struct lrcu_handler *__lrcu_init(void){
    struct lrcu_handler *h;

    h = calloc(1, sizeof(struct lrcu_handler));
    if(h == NULL)
        return NULL;

    h->worker_state = LRCU_WORKER_RUN;
    /* protect worker_state reads and writes here and in worker */
    mb();

    h->worker_timeout = LRCU_WORKER_SLEEP_US;
    LRCU_SET_HANDLER(h);

    if(pthread_create(&h->worker_tid, NULL, lrcu_worker, (void *)h))
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
    free(h);
    return NULL;
}

void lrcu_deinit(void){
    struct lrcu_handler *h = LRCU_GET_HANDLER();

    if(h == NULL)
        return;

    h->worker_state = LRCU_WORKER_STOP;
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

    /* schedule lrcu_thread_set_ns to worker */

    spin_lock(&h->ns_lock);
    if(h->ns[id]){
        LRCU_BUG();
        goto out;
    }
    if(h->worker_ti == NULL)
        LRCU_BUG();

    ns = calloc(1, sizeof(struct lrcu_namespace));
    if(ns == NULL)
        goto out;

    ns->id = id;
    ns->sync_timeout = LRCU_NS_SYNC_SLEEP_US;
    /* no need to take a lock */
    if (list_add(&ns->threads, h->worker_ti) == NULL){
        free(ns);
        ns = NULL;
        goto out;
    }
    /* first allocate and init, then assign */
    mb();
    h->ns[id] = ns;

out:
    spin_unlock(&h->ns_lock);
    return ns;
}

void lrcu_ns_deinit(u8 id){
    struct lrcu_handler *h = LRCU_GET_HANDLER();
    struct lrcu_namespace *ns;

    if(h == NULL)
        return;

    ns = h->ns[id];
    if(ns == NULL)
        return;

    /* raise callback in this NS? */
    /*
        In callback we zero out ns[id] pointer.
        make sure every access to zero ns pointer leads to ??? not BUG?
        schedule next callback???
    */
    /*
        do we rally need ns_lock???
        set ns ptr to zero, then synchronize? but how worker would be able
        to process it??
        could add flag to ns, or 
    */

    spin_lock(&h->ns_lock);
    h->ns[id] = NULL;
    mb();
    free(h->ns[id]);
    spin_unlock(&h->ns_lock);
    /* TODO remove all threads */
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
    struct lrcu_namespace *ns = ti->h->ns[ns_id];
    list_t *e, *next;
    int found = false;

    if(unlikely(ns == NULL)){
        LRCU_BUG();
        return false;
    }

    spin_lock(&ns->threads_lock); /* locking in spinlock. careful of deadlock */
    list_for_each(e, next, &ns->threads){
        char *cp = e->data; /* hack with strict aliasing */
        struct lrcu_thread_info *nti = *((struct lrcu_thread_info **)cp);

        if(nti == ti){
            list_unlink(&ns->threads, e);
            found = true;
            break;
        }
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
    free(ti);
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