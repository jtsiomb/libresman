#ifndef THREAD_POOL_H_
#define THREAD_POOL_H_

struct thread_pool;

typedef void (*tpool_work_func)(void*);

#define TPOOL_AUTO	0
int tpool_init(struct thread_pool *tpool, int num_threads);
void tpool_destroy(struct thread_pool *tpool);

void tpool_set_work_func(struct thread_pool *tpool, tpool_work_func func, void *cls);

/* TODO cont. */

#endif	/* THREAD_POOL_H_ */
