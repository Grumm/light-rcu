#include <lrcu/lrcu.h>

/* Simple API usage example */

#define INVALID_C_BEFORE   ((u64)-1ULL)
#define INVALID_C_AFTER    ((u64)-2ULL)

struct shared_ptr{
    void *ptr;
    int reader_timer, writer_timer;
    int flag;
};

struct shared_data{
    u64 c;
    struct shared_ptr *shptr;
};

struct shared_data *shared_data_constructor(struct shared_ptr *shptr,
                                                struct shared_data *data){
    struct shared_data *newdata = LRCU_MALLOC(sizeof(struct shared_data));

    newdata->shptr = shptr;
    if(data)
        newdata->c = data->c + 1;
    else
        newdata->c = 1;
    return newdata;
}

void shared_data_destructor(void *p){
    struct shared_data *data = p;
    
    LRCU_ASSERT(p);
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
        lrcu_call(data, shared_data_destructor);
    }
}

void *reader(void *arg){
    struct shared_ptr *shptr = (struct shared_ptr *)arg;
    u64 processed = 0, accrel = 0;
    return NULL;

    lrcu_thread_init();

    while(shptr->flag){
        struct shared_data *data;
        lrcu_read_lock();
        data = lrcu_dereference(shptr->ptr);
        if(!shared_data_process(data))
            accrel++;
        processed++;
        lrcu_read_unlock();
        if(shptr->reader_timer)
            LRCU_USLEEP(shptr->reader_timer);
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

    while(shptr->flag){
        struct shared_data *data;
        lrcu_write_lock();
        data = shptr->ptr;

        /* no need for lrcu_assign_pointer() since we are under lock */
        shptr->ptr = shared_data_constructor(shptr, data);
        processed++;
        lrcu_write_unlock();
        if(shptr->writer_timer)
            LRCU_USLEEP(shptr->writer_timer);
        shared_data_release(data);
    }
    LRCU_LOG("writer: processed %"PRIu64"\n", processed);

    lrcu_thread_deinit();
    return NULL;
}

int run_test(int total_timer, int readers, int writers){
    struct shared_ptr shptr = {NULL, 0, 0, 1};
    LRCU_THREAD_T *r_tids;
    LRCU_THREAD_T *w_tids;
    int i;
    int err = EXIT_SUCCESS;

    total_timer *= 1000000;
    r_tids = LRCU_MALLOC(readers * sizeof(LRCU_THREAD_T));
    w_tids = LRCU_MALLOC(writers * sizeof(LRCU_THREAD_T));
    if(r_tids == NULL || w_tids == NULL){
        err = EXIT_FAILURE;
        goto out;
    }

    lrcu_init();
    lrcu_thread_init();
    for(i = 0; i < readers; i++){
        if(LRCU_THREAD_CREATE(&r_tids[i], reader, (void *)&shptr))
            LRCU_EXIT(EXIT_FAILURE);
    }
    for(i = 0; i < writers; i++){
        if(LRCU_THREAD_CREATE(&w_tids[i], writer, (void *)&shptr))
            LRCU_EXIT(EXIT_FAILURE);
    }
    LRCU_USLEEP(total_timer);
    shptr.flag = 0;

    for(i = 0; i < readers; i++){
        LRCU_THREAD_JOIN(&r_tids[i]);
    }
    for(i = 0; i < writers; i++){
        LRCU_THREAD_JOIN(&w_tids[i]);
    }

    lrcu_thread_deinit();
    lrcu_deinit();

out:
    LRCU_FREE(r_tids);
    LRCU_FREE(w_tids);
    return err;
}

#ifdef LRCU_USER
int main(int argc, char *argv[]){
    int readers = 2;
    int writers = 1;
    int total_timer = 10;
    if(argc > 1)
        total_timer = atoi(argv[1]);
    if(argc > 2)
        readers = atoi(argv[2]);
    if(argc > 3)
        writers = atoi(argv[3]);

    return run_test(total_timer, readers, writers);
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
}

module_init(lrcu_test_start);
module_exit(lrcu_test_cleanup_module);

#define DRV_VERSION "1.0"
MODULE_VERSION(DRV_VERSION);
MODULE_LICENSE("GPL");

MODULE_DESCRIPTION("LRCU" ", v" DRV_VERSION);
MODULE_AUTHOR("Andrei Dubasov, andrew.dubasov@gmail.com");

#endif