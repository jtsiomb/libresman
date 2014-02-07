#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include "threadpool.h"

struct work_item {
	void *data;
	struct work_item *next;
};

struct thread_pool {
	pthread_t *workers;
	int num_workers;

	pthread_mutex_t work_lock;
	pthread_cond_t work_cond;

	tpool_work_func work_func;
	void *cls;

	struct work_item *work_list, *work_list_tail;
	int work_count;
};


static void *thread_func(void *tp);
static struct work_item *alloc_node(void);
static void free_node(struct work_item *node);
static int get_processor_count(void);



int tpool_init(struct thread_pool *tpool, int num_threads)
{
	int i;

	memset(tpool, 0, sizeof *tpool);


	pthread_mutex_init(&tpool->work_lock, 0);
	pthread_cond_init(&tpool->work_cond, 0);


	if(num_threads <= 0) {
		num_threads = get_processor_count();
	}
	tpool->num_workers = num_threads;

	printf("initializing thread pool with %d worker threads\n", num_threads);

	if(!(tpool->workers = malloc(num_threads * sizeof *tpool->workers))) {
		fprintf(stderr, "failed to create array of %d threads\n", num_threads);
		return -1;
	}

	for(i=0; i<num_threads; i++) {
		if(pthread_create(tpool->workers + i, 0, thread_func, tpool) == -1) {
			fprintf(stderr, "%s: failed to create thread %d\n", __FUNCTION__, i);
			tpool_destroy(tpool);
			return -1;
		}
	}
	return 0;
}

void tpool_destroy(struct thread_pool *tpool)
{
	int i;
	for(i=0; i<tpool->num_workers; i++) {
		void *ret;
		pthread_join(tpool->workers[i], &ret);
	}

	pthread_mutex_destroy(&tpool->work_lock);
	pthread_cond_destroy(&tpool->work_cond);
}

struct thread_pool *tpool_create(int num_threads)
{
	struct thread_pool *tpool = malloc(sizeof *tpool);
	if(!tpool) {
		return 0;
	}
	if(tpool_init(tpool, num_threads) == -1) {
		free(tpool);
		return 0;
	}
	return tpool;
}

void tpool_free(struct thread_pool *tpool)
{
	if(tpool) {
		tpool_destroy(tpool);
		free(tpool);
	}
}

void tpool_set_work_func(struct thread_pool *tpool, tpool_work_func func, void *cls)
{
	tpool->work_func = func;
	tpool->cls = cls;
}

int tpool_add_work(struct thread_pool *tpool, void *data)
{
	struct work_item *node;

	if(!(node = alloc_node())) {
		fprintf(stderr, "%s: failed to allocate new work item node\n", __FUNCTION__);
		return -1;
	}
	node->data = data;
	node->next = 0;

	pthread_mutex_lock(&tpool->work_lock);

	if(!tpool->work_list) {
		tpool->work_list = tpool->work_list_tail = node;
	} else {
		tpool->work_list_tail->next = node;
		tpool->work_list_tail = node;
	}

	pthread_mutex_unlock(&tpool->work_lock);
	return 0;
}


static void *thread_func(void *tp)
{
	struct work_item *job;
	struct thread_pool *tpool = tp;

	pthread_mutex_lock(&tpool->work_lock);
	for(;;) {
		/* while there aren't any work items to do go to sleep on the condvar */
		pthread_cond_wait(&tpool->work_cond, &tpool->work_lock);
		if(!tpool->work_list) {
			continue;	/* spurious wakeup, go back to sleep */
		}

		job = tpool->work_list;
		tpool->work_list = tpool->work_list->next;

		tpool->work_func(job->data, tpool->cls);

		free_node(job);
	}
	pthread_mutex_unlock(&tpool->work_lock);
	return 0;
}

/* TODO: custom allocator */
static struct work_item *alloc_node(void)
{
	return malloc(sizeof(struct work_item));
}

static void free_node(struct work_item *node)
{
	free(node);
}

/* The following highly platform-specific code detects the number
 * of processors available in the system. It's used by the thread pool
 * to autodetect how many threads to spawn.
 * Currently works on: Linux, BSD, Darwin, and Windows.
 */

#if defined(__APPLE__) && defined(__MACH__)
# ifndef __unix__
#  define __unix__	1
# endif	/* unix */
# ifndef __bsd__
#  define __bsd__	1
# endif	/* bsd */
#endif	/* apple */

#if defined(unix) || defined(__unix__)
#include <unistd.h>

# ifdef __bsd__
#  include <sys/sysctl.h>
# endif
#endif

#if defined(WIN32) || defined(__WIN32__)
#include <windows.h>
#endif


static int get_processor_count(void)
{
#if defined(unix) || defined(__unix__)
# if defined(__bsd__)
	/* BSD systems provide the num.processors through sysctl */
	int num, mib[] = {CTL_HW, HW_NCPU};
	size_t len = sizeof num;

	sysctl(mib, 2, &num, &len, 0, 0);
	return num;

# elif defined(__sgi)
	/* SGI IRIX flavour of the _SC_NPROC_ONLN sysconf */
	return sysconf(_SC_NPROC_ONLN);
# else
	/* Linux (and others?) have the _SC_NPROCESSORS_ONLN sysconf */
	return sysconf(_SC_NPROCESSORS_ONLN);
# endif	/* bsd/sgi/other */

#elif defined(WIN32) || defined(__WIN32__)
	/* under windows we need to call GetSystemInfo */
	SYSTEM_INFO info;
	GetSystemInfo(&info);
	return info.dwNumberOfProcessors;
#endif
}
