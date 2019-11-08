#include <lrcu/lrcu.h>

/* Simple API usage example */

#define INVALID_C_BEFORE   ((u64)-1ULL)
#define INVALID_C_AFTER    ((u64)-2ULL)

struct shared_ptr{
    void *ptr;
    int reader_timer, writer_timer;
    int flag;
    int timer;
};

struct shared_data{
    u64 c;
    struct shared_ptr *shptr;
    lrcu_ptr_head_t lrcu_head;
};

struct shared_data *shared_data_constructor(struct shared_ptr *shptr,
                                                struct shared_data *data){
    struct shared_data *newdata = LRCU_MALLOC(sizeof(struct shared_data));

    if(newdata){
        newdata->shptr = shptr;
        if(data){
            LRCU_ASSERT(data->c != INVALID_C_AFTER);
            LRCU_ASSERT(data->c != INVALID_C_BEFORE);
            newdata->c = data->c + 1;
        }else
            newdata->c = 1;
    }
    return newdata;
}

static u64 freed;

void shared_data_destructor(void *p){
    struct shared_data *data = container_of(p, struct shared_data, lrcu_head);

    LRCU_ASSERT(p);
    freed++;
    data->shptr = NULL;
    data->c = INVALID_C_AFTER;
    LRCU_FREE(data);
}

bool shared_data_process(struct shared_data *data){
    if(data){
        (void)data->c;
        LRCU_ASSERT(data->c);
        LRCU_ASSERT(data->c != INVALID_C_AFTER);
        if(data->c == INVALID_C_BEFORE)
            return false;
    }
    return true;
}

void shared_data_release(struct shared_data *data){
    if(data){
        data->c = INVALID_C_BEFORE;
        lrcu_call_head(&data->lrcu_head, shared_data_destructor);
    }
}

void *reader(void *arg){
    struct shared_ptr *shptr = (struct shared_ptr *)arg;
    u64 processed = 0, accrel = 0;

    lrcu_thread_init();

    while(shptr->flag && !LRCU_THREAD_SHOULD_STOP()){
        struct shared_data *data;
        lrcu_read_lock();

        data = lrcu_dereference(shptr->ptr);
        if(!shared_data_process(data))
            accrel++;
        processed++;
        lrcu_read_unlock();
        if(shptr->reader_timer > 1)
            LRCU_USLEEP(shptr->reader_timer);
        else if(shptr->reader_timer == 1)
            LRCU_YIELD();
        else if(unlikely(processed % 1000 == 0))
            LRCU_YIELD();
    }
    LRCU_LOG("reader: processed %"PRIu64"; accessed released %"PRIu64"\n",
            processed, accrel);

    lrcu_thread_deinit();
    return NULL;
}

void *writer(void *arg){
    struct shared_ptr *shptr = (struct shared_ptr *)arg;
    u64 processed = 0;

    lrcu_thread_init();

    while(shptr->flag && !LRCU_THREAD_SHOULD_STOP()){
        struct shared_data *data;
        void *t;
        lrcu_write_lock();

        data = shptr->ptr;

        t = shared_data_constructor(shptr, data);
        /* still need lrcu_assign_pointer() even if we are under lock */
        lrcu_assign_pointer(shptr->ptr, t);
        processed++;
        lrcu_write_unlock();
        if(shptr->writer_timer > 1)
            LRCU_USLEEP(shptr->writer_timer);
        else if(shptr->writer_timer == 1)
            LRCU_YIELD();
        else if(unlikely(processed % 1000 == 0))
            LRCU_YIELD();

        shared_data_release(data);
    }
    LRCU_LOG("writer: processed %"PRIu64"\n", processed);

    lrcu_thread_deinit();
    return NULL;
}

void *timer_thread(void *arg){
    struct shared_ptr *shptr = arg;

    LRCU_USLEEP(shptr->timer);
    shptr->flag = 0;
    return NULL;
}

int run_test(int total_timer, int readers, int writers){
    struct shared_ptr *shptr = LRCU_CALLOC(1, sizeof(struct shared_ptr));
    LRCU_THREAD_T *r_tids;
    LRCU_THREAD_T *w_tids;
    LRCU_THREAD_T tthread;
    int i;
    int err = EXIT_FAILURE;


    r_tids = LRCU_MALLOC(readers * sizeof(LRCU_THREAD_T));
    w_tids = LRCU_MALLOC(writers * sizeof(LRCU_THREAD_T));
    if(shptr == NULL || r_tids == NULL || w_tids == NULL)
        goto out;

    shptr->timer = total_timer * 1000000;
    shptr->reader_timer = 0;
    shptr->writer_timer = 0;
    shptr->flag = 1;

    lrcu_thread_init();

    if(LRCU_THREAD_CREATE(&tthread, timer_thread, (void *)shptr))
        goto out;
    for(i = 0; i < readers; i++){
        if(LRCU_THREAD_CREATE(&r_tids[i], reader, (void *)shptr))
            goto out;
    }
    for(i = 0; i < writers; i++){
        if(LRCU_THREAD_CREATE(&w_tids[i], writer, (void *)shptr))
            goto out;
    }

    LRCU_THREAD_JOIN(&tthread);

    for(i = 0; i < readers; i++){
        LRCU_THREAD_JOIN(&r_tids[i]);
    }
    for(i = 0; i < writers; i++){
        LRCU_THREAD_JOIN(&w_tids[i]);
    }

    lrcu_thread_deinit();
    LRCU_LOG("freed: %"PRIu64"\n", freed);
    err = EXIT_SUCCESS;

out:
    LRCU_FREE(r_tids);
    LRCU_FREE(w_tids);
    LRCU_FREE(shptr);
    return err;
}

#ifdef LRCU_USER
int main(int argc, char *argv[]){
    int readers = 2;
    int writers = 1;
    int total_timer = 10;
    int ret;

    if(argc > 1)
        total_timer = atoi(argv[1]);
    if(argc > 2)
        readers = atoi(argv[2]);
    if(argc > 3)
        writers = atoi(argv[3]);

    lrcu_init();
    while(1)
    {
        ret = run_test(total_timer, readers, writers);
        lrcu_barrier();
    }
    lrcu_deinit();
    return ret;
}
#else

#include <linux/module.h>

static int total_timer    = 10;
static int readers    = 2;
static int writers    = 1;

module_param(total_timer, int, 0);
MODULE_PARM_DESC(total_timer, "Time in seconds to run the test");

module_param(readers, int, 0);
MODULE_PARM_DESC(readers, "Number of readers");

module_param(writers, int, 0);
MODULE_PARM_DESC(writers, "Number of writers");

static int __init lrcu_test_start(void)
{
    return run_test(total_timer, readers, writers);
}
static void __exit lrcu_test_cleanup_module(void)
{
    lrcu_barrier();
}

module_init(lrcu_test_start);
module_exit(lrcu_test_cleanup_module);

#define DRV_VERSION "1.1"
MODULE_VERSION(DRV_VERSION);
MODULE_LICENSE("GPL");

MODULE_DESCRIPTION("LRCU" ", v" DRV_VERSION);
MODULE_AUTHOR("Andrei Dubasov, andrew.dubasov@gmail.com");

#endif