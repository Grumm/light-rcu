#include <stdlib.h>
#include <stdio.h>
#include <pthread.h>
#include <inttypes.h>
#include <unistd.h>

#include "lrcu.h"
#include "list.h"

struct working_data{
    u64 c;
};

#define LOGGING_PERIOD 1000000
#if 1
#define LRCU_LOG(x, ...) printf((x), ##__VA_ARGS__)
#define LRCU_LOG2(x, ...) printf((x), ##__VA_ARGS__)
#define LRCU_LOG3(x, ...) printf((x), ##__VA_ARGS__)
#else
#define LRCU_LOG(x, ...) 
#define LRCU_LOG2(x, ...) 
#define LRCU_LOG3(x, ...)
#endif

#if 0
#define READ_TIMEOUT    100000
#define WRITE_TIMEOUT   1000000
#else
#define READ_TIMEOUT    0
#define WRITE_TIMEOUT   0
#endif

void *reader(void *arg){
    volatile list_head_t *list = (list_head_t *)arg;
    struct working_data *w;
    list_t *n, *next;
    u64 reads = 0;

    lrcu_thread_init();

    while(1){
        lrcu_read_lock();
        list_for_each(n, next, list){
            int r;
            r = rand() % 200000;
            if(0 && r == 0){
                printf("usleep--------------------\n");
                fflush(stdout);
                usleep(1200000);
            }
            w = (struct working_data *)&n->data;
            (void)w;
            if(++reads % LOGGING_PERIOD == 0)
                LRCU_LOG2("read %"PRIu64"\n", reads / LOGGING_PERIOD);
            fflush(stdout);
        }
        lrcu_read_unlock();
        if(READ_TIMEOUT)
            usleep(READ_TIMEOUT);
    }
}

void working_data_destructor(void *p){
    struct working_data *w = (struct working_data *)p;

    if(w->c % LOGGING_PERIOD == 0)
        LRCU_LOG("destructor %"PRIu64"\n", w->c/LOGGING_PERIOD);
    free(w);
}

void list_destructor(void *p){
    list_t *e = p;
    struct working_data *w = (struct working_data *)&e->data[0];
    if(w->c % LOGGING_PERIOD == 0)
        LRCU_LOG("destructor2 %"PRIu64"\n", w->c/LOGGING_PERIOD);
    //LRCU_LOG("destructor2 \n");
    free(p);
}

void *writer(void *arg){
    list_head_t *list = (list_head_t *)arg;
    struct working_data w;
    u64 counter = 0;
    list_t *e;
    static u64 c = 0;

    lrcu_thread_init();

    while(1){
        int op = rand() % 2;
        int r;

        r = rand() % 200000;
        if(0 && r == 0){
            printf("usleep--------------------\n");
            fflush(stdout);
            usleep(1200000);
        }

        if(c > 10000)
            op = 1;
        if(counter > 40000000){
            if(c == 0){
                sched_yield();
                usleep(1000000);
                continue;
            }
            op = 1;
        }

        switch(op){
            default:
            case 0:
                //w = malloc(sizeof(struct working_data));
                w.c = counter++;
                lrcu_write_lock();
                if(counter % LOGGING_PERIOD == 0)
                    LRCU_LOG2("contructor %"PRIu64" %"PRIu64" \n", c, counter / LOGGING_PERIOD);
                c++;
                list_add(list, w);
                lrcu_write_unlock();
                break;
            case 1:
                if(list->head == NULL)
                    break;

                lrcu_write_lock();
                e = list->head;
                if(e){
                    c--;
                    struct working_data *w = (struct working_data *)&e->data[0];

                    (void)w;
                    list_unlink(list, e);
                    //lrcu_call(eptr, working_data_destructor);
                    lrcu_call(e, list_destructor);
                    //free(e);
                    //free(ptr);
                }
                lrcu_write_unlock();
                break;
        }
        //if(c%1000 == 0)
        //  LRCU_LOG3("writer %"PRIu64"\n", c);
        if(WRITE_TIMEOUT)
            usleep(WRITE_TIMEOUT);
    }
}

int main(int argc, char *argv[]){
    list_head_t list = {NULL, NULL};
    pthread_t tid;
    int readers = 2;
    int writers = 1;
    int i;

    if(argc > 2)
        writers = atoi(argv[2]);
    if(argc > 1)
        readers = atoi(argv[1]);

    lrcu_init();
    lrcu_thread_init();

    for(i = 0; i < readers; i++){
        if(pthread_create(&tid, NULL, reader, (void *)&list))
            exit(1);
    }
    for(i = 0; i < writers; i++){
        if(pthread_create(&tid, NULL, writer, (void *)&list))
            exit(1);
    }
    pthread_join(tid, NULL);

    return 0;
}
