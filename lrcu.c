/*
	Lazy RCU: extra light read section and hard write section
	locks taken only on lrcu_ptr  
*/

/***********************************************************/

/* can't be nested */
void __lrcu_write_lock(struct lrcu_namespace *ns){
	spin_lock(&ns->write_lock);
	ptr->ns->version++;
	barrier(); /* new ptr's should be seen only with new version */
}

void __lrcu_assign_pointer(struct lrcu_ptr *ptr, void *newptr){
	struct lrcu_ptr old_ptr;

	//ptr->ptr = NULL; 
					/* make sure anyone who dereferences since now, 
						will get either NULL or new version with correct pointer */
	//ptr->version = ptr->ns->version;
	//barrier();
	old_ptr = *ptr;
	ptr->ptr = newptr;
	barrier();
	lrcu_call(ptr->ns, &old_ptr, ptr->deinit);
	/* add old_ptr to free_queue and wake up lrcu_worker thread */
	/* 
		We are under ns->write_lock. so old ns->version is obsolete and 
		all pointers that have that version or less should be freed after
		grace period.
		Need to change ns->version */
}

void __lrcu_write_unlock(struct lrcu_namespace *ns){
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
	/* tell ns that grace period is in progress */
}

void *__lrcu_read_dereference_pointer(struct lrcu_thread_info *ti,
												struct lrcu_ptr *ptr){
	struct lrcu_ptr ptr;
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

void __lrcu_call(struct lrcu_thread_info *ti, 
							struct lrcu_ptr *ptr, lrcu_destructor_t *destr){
	struct lrcu_ptr local_ptr = *ptr;
	struct lrcu_namespace *ns = ti->h.ns[ptr->ns_id];
							

	local_ptr.deinit = destr;
	local_ptr.version = ns->version; /* synchronize() will be called on this version */

	spin_lock(&ns->list_lock);

							/* NOT A POINTER!!! */
	list_add(&ns->free_list, local_ptr);
	spin_unlock(&ns->list_lock);
	/* XXX wakeup thread. see lrcu_read_unlock */
}

static inline bool lrcu_worker_process_call(struct lrcu_namespace *ns, 
												struct lrcu_ptr *ptr){
	struct lrcu_thread_info *ti;
	list_t *e, *prev;

	/* make sure that each thread that uses ns, has 
		local_namespace either bigger version, or zero counter */
	list_for_each(e, next, ns->threads){
		struct lrcu_local_namespace lns;
		bool ready = false;

		ti = e->data;

		lns = *LRCU_GET_LNS(ti, ns);
		barrier();
		if(lns->version > ns->version || /* XXX what about when version wraps -1 ? */
						lns->counter == 0){
			/* it's time!!! */
			ready = true;
		}
		if(!ready)
			break;
	}
	return true;
}

static inline void *lrcu_worker(void *arg){
	struct lrcu_handler *h = (struct lrcu_handler *)arg;

	while(h->worker_run){
		size_t i;

		for(i = 0; i < LRCU_NS_MAX; i++){
			struct lrcu_namespace *ns = h->ns;
			if(ns == NULL)
				continue;

			if(!list_empty(&ns->free_list)){
				spin_lock(&ns->list_lock);
				list_splice(&ns->worker_list, &ns->free_list);
				//list_init(&ns->free_list);
				spin_unlock(&ns->list_lock);
			}
			if(!list_empty(&ns->worker_list)){
				struct lrcu_ptr *ptr;
				list_t *n, *next;
				list_for_each(n, next, &ns->worker_list){
					ptr = n->data;
					/* do actual job */
					if(lrcu_worker_process_call(ns, ptr)){
						list_unlink(&ns->worker_list, n);
						free(n);
					}
				}
			}
		}
		usleep(h->sleep_time);
	}
}

struct lrcu_handler *lrcu_init(void){
	struct lrcu_handler *h;
	pthread_t worker_tid;

	h = calloc(1, sizeof(struct lrcu_handler));
	if(h == NULL)
		return NULL;

	h->worker_run = true;
	h->sleep_time = 1000000; //1 sec
	if(pthread_create(&h->worker_tid, NULL, lrcu_worker, lrcu_worker, (void *)h))
		goto out;

	return h;
out:
	free(h);
	return NULL
}

void lrcu_deinit(struct lrcu_handler *h){
	h->worker_run = false;
	pthread_join(&h->worker_tid);
	/* TODO remove all ns */
}

/***********************************************************/

struct lrcu_namespace *lrcu_ns_init(struct lrcu_handler *h, u8 id){
	struct lrcu_namespace *ns = NULL;

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

void lrcu_ns_deinit(struct lrcu_handler *h, u8 id){
	spin_lock(&h->ns_lock);
	free(h->ns[id]);
	h->ns[id] = NULL;
	spin_unlock(&h->ns_lock);
}

struct lrcu_thread_info *lrcu_thread_init(struct lrcu_handler *h){
	struct lrcu_thread_info *ti = NULL;
	size_t i;

	ti = calloc(1, sizeof(struct lrcu_thread_info));
	if(ti == NULL)
		return NULL;

	for(i = 0; i < LRCU_NS_MAX; i++){
		ti->lns[i]->id = i;
	}
	ti->h = h;
	LRCU_SET_TI(ti);

	return ti;
}

void lrcu_thread_deinit(struct lrcu_thread_info *ti){
	LRCU_DEL_TI(ti);
	free(ti);
}

/* already allocated ptr */
void __lrcu_ptr_init(struct lrcu_ptr *ptr, u8 ns_id, 
								lrcu_destructor_t *deinit){
	ptr->ns_id = ns_id;
	ptr->deinit = deinit;
}