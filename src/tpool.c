/*
libresman - a multithreaded resource data file manager.
Copyright (C) 2014-2018  John Tsiombikas <nuclear@member.fsf.org>

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU Lesser General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU Lesser General Public License for more details.

You should have received a copy of the GNU Lesser General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <pthread.h>
#include "tpool.h"

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
#include <sys/time.h>

# ifdef __bsd__
#  include <sys/sysctl.h>
# endif
#endif

#if defined(WIN32) || defined(__WIN32__)
#include <windows.h>
#endif


struct work_item {
	void *data;
	resman_tpool_callback work, done;
	struct work_item *next;
};

struct resman_thread_pool {
	pthread_t *threads;
	int num_threads;

	int qsize;
	struct work_item *workq, *workq_tail;
	pthread_mutex_t workq_mutex;
	pthread_cond_t workq_condvar;

	int nactive;	/* number of active workers (not sleeping) */

	pthread_cond_t done_condvar;

	int should_quit;
	int in_batch;

	int nref;	/* reference count */

#if defined(WIN32) || defined(__WIN32__)
	HANDLE wait_event;
#else
	int wait_pipe[2];
#endif
};

static void *thread_func(void *args);
static void send_done_event(struct resman_thread_pool *tpool);

static struct work_item *alloc_work_item(void);
static void free_work_item(struct work_item *w);


struct resman_thread_pool *resman_tpool_create(int num_threads)
{
	int i;
	struct resman_thread_pool *tpool;

	if(!(tpool = calloc(1, sizeof *tpool))) {
		return 0;
	}
	pthread_mutex_init(&tpool->workq_mutex, 0);
	pthread_cond_init(&tpool->workq_condvar, 0);
	pthread_cond_init(&tpool->done_condvar, 0);

#if !defined(WIN32) && !defined(__WIN32__)
	tpool->wait_pipe[0] = tpool->wait_pipe[1] = -1;
#endif

	if(num_threads <= 0) {
		num_threads = resman_tpool_num_processors();
	}
	tpool->num_threads = num_threads;

	if(!(tpool->threads = calloc(num_threads, sizeof *tpool->threads))) {
		free(tpool);
		return 0;
	}
	for(i=0; i<num_threads; i++) {
		if(pthread_create(tpool->threads + i, 0, thread_func, tpool) == -1) {
			/*tpool->threads[i] = 0;*/
			resman_tpool_destroy(tpool);
			return 0;
		}
	}
	return tpool;
}

void resman_tpool_destroy(struct resman_thread_pool *tpool)
{
	int i;
	if(!tpool) return;

	resman_tpool_clear(tpool);
	tpool->should_quit = 1;

	pthread_cond_broadcast(&tpool->workq_condvar);

	if(tpool->threads) {
		printf("resman_thread_pool: waiting for %d worker threads to stop ", tpool->num_threads);
		fflush(stdout);

		for(i=0; i<tpool->num_threads; i++) {
			pthread_join(tpool->threads[i], 0);
			putchar('.');
			fflush(stdout);
		}
		putchar('\n');
		free(tpool->threads);
	}

	/* also wake up anyone waiting on the resman_wait* calls */
	tpool->nactive = 0;
	pthread_cond_broadcast(&tpool->done_condvar);
	send_done_event(tpool);

	pthread_mutex_destroy(&tpool->workq_mutex);
	pthread_cond_destroy(&tpool->workq_condvar);
	pthread_cond_destroy(&tpool->done_condvar);

#if defined(WIN32) || defined(__WIN32__)
	if(tpool->wait_event) {
		CloseHandle(tpool->wait_event);
	}
#else
	if(tpool->wait_pipe[0] >= 0) {
		close(tpool->wait_pipe[0]);
		close(tpool->wait_pipe[1]);
	}
#endif
}

int resman_tpool_addref(struct resman_thread_pool *tpool)
{
	return ++tpool->nref;
}

int resman_tpool_release(struct resman_thread_pool *tpool)
{
	if(--tpool->nref <= 0) {
		resman_tpool_destroy(tpool);
		return 0;
	}
	return tpool->nref;
}

void resman_tpool_begin_batch(struct resman_thread_pool *tpool)
{
	tpool->in_batch = 1;
}

void resman_tpool_end_batch(struct resman_thread_pool *tpool)
{
	tpool->in_batch = 0;
	pthread_cond_broadcast(&tpool->workq_condvar);
}

int resman_tpool_enqueue(struct resman_thread_pool *tpool, void *data,
		resman_tpool_callback work_func, resman_tpool_callback done_func)
{
	struct work_item *job;

	if(!(job = alloc_work_item())) {
		return -1;
	}
	job->work = work_func;
	job->done = done_func;
	job->data = data;
	job->next = 0;

	pthread_mutex_lock(&tpool->workq_mutex);
	if(tpool->workq) {
		tpool->workq_tail->next = job;
		tpool->workq_tail = job;
	} else {
		tpool->workq = tpool->workq_tail = job;
	}
	++tpool->qsize;
	pthread_mutex_unlock(&tpool->workq_mutex);

	if(!tpool->in_batch) {
		pthread_cond_broadcast(&tpool->workq_condvar);
	}
	return 0;
}

void resman_tpool_clear(struct resman_thread_pool *tpool)
{
	pthread_mutex_lock(&tpool->workq_mutex);
	while(tpool->workq) {
		void *tmp = tpool->workq;
		tpool->workq = tpool->workq->next;
		free(tmp);
	}
	tpool->workq = tpool->workq_tail = 0;
	tpool->qsize = 0;
	pthread_mutex_unlock(&tpool->workq_mutex);
}

int resman_tpool_queued_jobs(struct resman_thread_pool *tpool)
{
	int res;
	pthread_mutex_lock(&tpool->workq_mutex);
	res = tpool->qsize;
	pthread_mutex_unlock(&tpool->workq_mutex);
	return res;
}

int resman_tpool_active_jobs(struct resman_thread_pool *tpool)
{
	int res;
	pthread_mutex_lock(&tpool->workq_mutex);
	res = tpool->nactive;
	pthread_mutex_unlock(&tpool->workq_mutex);
	return res;
}

int resman_tpool_pending_jobs(struct resman_thread_pool *tpool)
{
	int res;
	pthread_mutex_lock(&tpool->workq_mutex);
	res = tpool->qsize + tpool->nactive;
	pthread_mutex_unlock(&tpool->workq_mutex);
	return res;
}

void resman_tpool_wait(struct resman_thread_pool *tpool)
{
	pthread_mutex_lock(&tpool->workq_mutex);
	while(tpool->nactive || tpool->qsize) {
		pthread_cond_wait(&tpool->done_condvar, &tpool->workq_mutex);
	}
	pthread_mutex_unlock(&tpool->workq_mutex);
}

void resman_tpool_wait_pending(struct resman_thread_pool *tpool, int pending_target)
{
	pthread_mutex_lock(&tpool->workq_mutex);
	while(tpool->qsize + tpool->nactive > pending_target) {
		pthread_cond_wait(&tpool->done_condvar, &tpool->workq_mutex);
	}
	pthread_mutex_unlock(&tpool->workq_mutex);
}

#if defined(WIN32) || defined(__WIN32__)
long resman_tpool_timedwait(struct resman_thread_pool *tpool, long timeout)
{
	fprintf(stderr, "tpool_timedwait currently unimplemented on windows\n");
	abort();
	return 0;
}

/* TODO: actually does this work with MinGW ? */
int resman_tpool_get_wait_fd(struct resman_thread_pool *tpool)
{
	static int once;
	if(!once) {
		once = 1;
		fprintf(stderr, "warning: tpool_get_wait_fd call on Windows does nothing\n");
	}
	return 0;
}

void *resman_tpool_get_wait_handle(struct resman_thread_pool *tpool)
{
	if(!tpool->wait_event) {
		if(!(tpool->wait_event = CreateEvent(0, 0, 0, 0))) {
			return 0;
		}
	}
	return tpool->wait_event;
}

static void send_done_event(struct resman_thread_pool *tpool)
{
	if(tpool->wait_event) {
		SetEvent(tpool->wait_event);
	}
}

#else		/* UNIX */

long resman_tpool_timedwait(struct resman_thread_pool *tpool, long timeout)
{
	struct timespec tout_ts;
	struct timeval tv0, tv;
	gettimeofday(&tv0, 0);

	long sec = timeout / 1000;
	tout_ts.tv_nsec = tv0.tv_usec * 1000 + (timeout % 1000) * 1000000;
	tout_ts.tv_sec = tv0.tv_sec + sec;

	pthread_mutex_lock(&tpool->workq_mutex);
	while(tpool->nactive || tpool->qsize) {
		if(pthread_cond_timedwait(&tpool->done_condvar,
					&tpool->workq_mutex, &tout_ts) == ETIMEDOUT) {
			break;
		}
	}
	pthread_mutex_unlock(&tpool->workq_mutex);

	gettimeofday(&tv, 0);
	return (tv.tv_sec - tv0.tv_sec) * 1000 + (tv.tv_usec - tv0.tv_usec) / 1000;
}

int resman_tpool_get_wait_fd(struct resman_thread_pool *tpool)
{
	if(tpool->wait_pipe[0] < 0) {
		if(pipe(tpool->wait_pipe) == -1) {
			return -1;
		}
	}
	return tpool->wait_pipe[0];
}

void *resman_tpool_get_wait_handle(struct resman_thread_pool *tpool)
{
	static int once;
	if(!once) {
		once = 1;
		fprintf(stderr, "warning: tpool_get_wait_handle call on UNIX does nothing\n");
	}
	return 0;
}

static void send_done_event(struct resman_thread_pool *tpool)
{
	if(tpool->wait_pipe[1] >= 0) {
		write(tpool->wait_pipe[1], tpool, 1);
	}
}
#endif	/* WIN32/UNIX */

static void *thread_func(void *args)
{
	struct resman_thread_pool *tpool = args;

	pthread_mutex_lock(&tpool->workq_mutex);
	while(!tpool->should_quit) {
		if(!tpool->workq) {
			pthread_cond_wait(&tpool->workq_condvar, &tpool->workq_mutex);
			if(tpool->should_quit) break;
		}

		while(!tpool->should_quit && tpool->workq) {
			/* grab the first job */
			struct work_item *job = tpool->workq;
			tpool->workq = tpool->workq->next;
			if(!tpool->workq)
				tpool->workq_tail = 0;
			++tpool->nactive;
			--tpool->qsize;
			pthread_mutex_unlock(&tpool->workq_mutex);

			/* do the job */
			job->work(job->data);
			if(job->done) {
				job->done(job->data);
			}
			free_work_item(job);

			pthread_mutex_lock(&tpool->workq_mutex);
			/* notify everyone interested that we're done with this job */
			pthread_cond_broadcast(&tpool->done_condvar);
			send_done_event(tpool);
			--tpool->nactive;
		}
	}
	pthread_mutex_unlock(&tpool->workq_mutex);

	return 0;
}


/* The following highly platform-specific code detects the number
 * of processors available in the system. It's used by the thread pool
 * to autodetect how many threads to spawn.
 * Currently works on: Linux, BSD, Darwin, and Windows.
 */
int resman_tpool_num_processors(void)
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

#define MAX_WPOOL_SIZE	64
static pthread_mutex_t wpool_lock = PTHREAD_MUTEX_INITIALIZER;
static struct work_item *wpool;
static int wpool_size;

/* work item allocator */
static struct work_item *alloc_work_item(void)
{
	struct work_item *w;

	pthread_mutex_lock(&wpool_lock);
	if(!wpool) {
		pthread_mutex_unlock(&wpool_lock);
		return malloc(sizeof(struct work_item));
	}

	w = wpool;
	wpool = wpool->next;
	--wpool_size;
	pthread_mutex_unlock(&wpool_lock);
	return w;
}

static void free_work_item(struct work_item *w)
{
	pthread_mutex_lock(&wpool_lock);
	if(wpool_size >= MAX_WPOOL_SIZE) {
		free(w);
	} else {
		w->next = wpool;
		wpool = w;
		++wpool_size;
	}
	pthread_mutex_unlock(&wpool_lock);
}
