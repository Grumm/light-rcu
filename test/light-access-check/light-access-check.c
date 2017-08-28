#include <stdlib.h>
#include <stdio.h>
#include <pthread.h>
#include <inttypes.h>
#include <unistd.h>

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
    struct shared_data *newdata = malloc(sizeof(struct shared_data));

    newdata->shptr = shptr;
    if(data)
        newdata->c = data->c + 1;
    else
        newdata->c = 1;
    return newdata;
}

void shared_data_destructor(void *p){
    struct shared_data *data = p;
    
    assert(p);
    data->shptr = NULL;
    data->c = INVALID_C_AFTER;
    free(data);
}

bool shared_data_process(struct shared_data *data){
    if(data){
        (void)data->c;
        assert(data->c);
        assert(data->c != INVALID_C_AFTER);
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
    lrcu_thread_init();

    while(shptr->flag){
        lrcu_read_lock();
        struct shared_data *data = lrcu_dereference(shptr->ptr);
        if(!shared_data_process(data))
            accrel++;
        processed++;
        lrcu_read_unlock();
        if(shptr->reader_timer)
            usleep(shptr->reader_timer);
    }
    printf("reader: processed %"PRIu64"; accessed released %"PRIu64"\n",
            processed, accrel);

    lrcu_thread_deinit();
    return NULL;
}

void *writer(void *arg){
    struct shared_ptr *shptr = (struct shared_ptr *)arg;
    u64 processed = 0;

    lrcu_thread_init();

    while(shptr->flag){
        lrcu_write_lock();
        struct shared_data *data = shptr->ptr;

        /* no need for lrcu_assign_pointer() since we are under lock */
        shptr->ptr = shared_data_constructor(shptr, data);
        processed++;
        lrcu_write_unlock();
        if(shptr->writer_timer)
            usleep(shptr->writer_timer);
        shared_data_release(data);
    }
    printf("writer: processed %"PRIu64"\n", processed);

    lrcu_thread_deinit();
    return NULL;
}

int main(int argc, char *argv[]){
    struct shared_ptr shptr = {NULL, 0, 0, 1};
    pthread_t *r_tids;
    pthread_t *w_tids;
    int readers = 2;
    int writers = 1;
    int i;
    int err = EXIT_SUCCESS;
    int total_timer = 10;

    if(argc > 1)
        total_timer = atoi(argv[1]);
    total_timer *= 1000000;
    if(argc > 2)
        readers = atoi(argv[2]);
    if(argc > 3)
        writers = atoi(argv[3]);
    r_tids = malloc(readers * sizeof(pthread_t));
    w_tids = malloc(writers * sizeof(pthread_t));
    if(r_tids == NULL || w_tids == NULL){
        err = EXIT_FAILURE;
        goto out;
    }

    lrcu_init();
    lrcu_thread_init();

    for(i = 0; i < readers; i++){
        if(pthread_create(&r_tids[i], NULL, reader, (void *)&shptr))
            exit(EXIT_FAILURE);
    }
    for(i = 0; i < writers; i++){
        if(pthread_create(&w_tids[i], NULL, writer, (void *)&shptr))
            exit(EXIT_FAILURE);
    }
    usleep(total_timer);
    shptr.flag = 0;

    for(i = 0; i < readers; i++){
        pthread_join(r_tids[i], NULL);
    }
    for(i = 0; i < writers; i++){
        pthread_join(w_tids[i], NULL);
    }

    lrcu_thread_deinit();
    lrcu_deinit();

out:
    free(r_tids);
    free(w_tids);
    return err;
}