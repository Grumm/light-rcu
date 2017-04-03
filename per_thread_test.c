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

#if 0
#define LRCU_LOG(x, ...) printf((x), ##__VA_ARGS__)
#define LRCU_LOG2(x, ...) printf((x), ##__VA_ARGS__)
#define LRCU_LOG3(x, ...) printf((x), ##__VA_ARGS__)
#else
#define LRCU_LOG(x, ...) 
#define LRCU_LOG2(x, ...) 
#define LRCU_LOG3(x, ...) printf((x), ##__VA_ARGS__)
#endif

#if 0
#define READ_TIMEOUT 	10000
#define WRITE_TIMEOUT 	10000
#else
#define READ_TIMEOUT 	0
#define WRITE_TIMEOUT 	0
#endif

void *reader(void *arg){
	volatile list_head_t *list = (list_head_t *)arg;
	struct working_data *w;
	struct lrcu_ptr *ptr;
	list_t *n, *next;

	lrcu_thread_init();

	while(1){
		lrcu_read_lock();
		list_for_each(n, next, list){
			ptr = (struct lrcu_ptr *)&n->data;
			w = lrcu_read_dereference_pointer(ptr);
			LRCU_LOG2("read %"PRIu64"\n", w->c);
			fflush(stdout);
		}
		lrcu_read_unlock();
		if(READ_TIMEOUT)
			usleep(READ_TIMEOUT);
	}
}

void working_data_destructor(void *p){
	struct working_data *w = (struct working_data *)p;

	LRCU_LOG("destructor %"PRIu64"\n", w->c);
	free(w);
}

void list_destructor(void *p){
	LRCU_LOG("destructor2 \n");
	free(p);
}

void *writer(void *arg){
	list_head_t *list = (list_head_t *)arg;
	struct working_data *w;
	struct lrcu_ptr ptr = { .deinit = working_data_destructor, };
	u64 counter = 0;
	list_t *e;
	static u64 c = 0;

	lrcu_thread_init();

	while(1){
		int op = rand() % 2;

		if(c > 10000)
			op = 1;

		switch(op){
			default:
			case 0:
				w = malloc(sizeof(struct working_data));
				w->c = counter++;
				ptr.ptr = w;
				lrcu_write_lock();
				LRCU_LOG2("contructor %"PRIu64"\n", w->c);
				c++;
				list_add(list, ptr);
				lrcu_write_unlock();
				break;
			case 1:
				if(list->head == NULL)
					break;

				lrcu_write_lock();
				e = list->head;
				if(e){
					c--;
					struct lrcu_ptr *eptr = (struct lrcu_ptr *)&e->data;
					struct lrcu_ptr list_entry = {
						.deinit = list_destructor,
						.ptr = e,
					};

					list_unlink(list, e);
					lrcu_call(eptr, eptr->deinit);
					lrcu_call(&list_entry, list_entry.deinit);
					//free(ptr);
				}
				lrcu_write_unlock();
				break;
		}
		if(c%100 == 0)
			LRCU_LOG3("writer %"PRIu64"\n", c);
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