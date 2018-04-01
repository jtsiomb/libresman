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
#include <string.h>
#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include "resman.h"
#include "resman_impl.h"
#include "dynarr.h"
#include "filewatch.h"
#include "timer.h"

#if defined(WIN32) || defined(__WIN32__)
#include <windows.h>
#else
#include <unistd.h>
#include <fcntl.h>
#include <sys/select.h>
#endif

struct work_item {
	struct resman *rman;
	struct resource *res;

	struct work_item *next;
};


static int find_resource(struct resman *rman, const char *fname);
static int add_resource(struct resman *rman, const char *fname, void *data);
static void remove_resource(struct resman *rman, int idx);
static void work_func(void *cls);
/* these two functions should only be called with the resman mutex locked */
static struct work_item *alloc_work_item(struct resman *rman);
static void free_work_item(struct resman *rman, struct work_item *w);

static void wait_for_any_event(struct resman *rman);

static struct resman_thread_pool *thread_pool;


struct resman *resman_create(void)
{
	struct resman *rman = malloc(sizeof *rman);
	if(resman_init(rman) == -1) {
		free(rman);
		return 0;
	}
	return rman;
}

void resman_free(struct resman *rman)
{
	resman_destroy(rman);
	free(rman);
}

int resman_init(struct resman *rman)
{
	const char *env;

	/* initialize timer */
	resman_get_time_msec();

	if(!thread_pool) {
		/* create the thread pool if there isn't one */
		int num_threads = 0;	/* automatically determine number of threads */
		if((env = getenv("RESMAN_THREADS"))) {
			num_threads = atoi(env);
		}
		if(num_threads < 1) {
			if((num_threads = resman_tpool_num_processors() - 1) < 1) {
				num_threads = 1;
			}
		}
		if(!(thread_pool = resman_tpool_create(num_threads))) {
			return -1;
		}
	}
	resman_tpool_addref(thread_pool);

	memset(rman, 0, sizeof *rman);
	rman->tpool = thread_pool;

#if defined(WIN32) || defined(__WIN32__)
	if(!(rman->wait_handles = dynarr_alloc(0, sizeof *rman->wait_handles))) {
		return -1;
	}
	rman->tpool_wait_handle = resman_tpool_get_wait_handle(rman->tpool);
	if(!(rman->wait_handles = dynarr_push(rman->wait_handles, &rman->tpool_wait_handle))) {
		return -1;
	}
#else
	if(!(rman->wait_fds = dynarr_alloc(0, sizeof *rman->wait_fds))) {
		return -1;
	}
	rman->tpool_wait_fd = resman_tpool_get_wait_fd(rman->tpool);
	if(!(rman->wait_fds = dynarr_push(rman->wait_fds, &rman->tpool_wait_fd))) {
		return -1;
	}
	/* make pipe read end nonblocking */
	fcntl(rman->tpool_wait_fd, F_SETFL, fcntl(rman->tpool_wait_fd, F_GETFL) | O_NONBLOCK);
#endif


	if(resman_init_file_monitor(rman) == -1) {
		return -1;
	}

	if(!(rman->res = dynarr_alloc(0, sizeof *rman->res))) {
		return -1;
	}

	rman->opt[RESMAN_OPT_TIMESLICE] = 16;

	pthread_mutex_init(&rman->lock, 0);
	return 0;
}

void resman_destroy(struct resman *rman)
{
	int i;
	if(!rman) return;

	for(i=0; i<dynarr_size(rman->res); i++) {
		if(rman->destroy_func) {
			rman->destroy_func(i, rman->destroy_func_cls);
		}
		free(rman->res[i]->name);
		free(rman->res[i]);
	}
	dynarr_free(rman->res);

	resman_tpool_release(rman->tpool);

#if defined(WIN32) || defined(__WIN32__)
	dynarr_free(rman->wait_handles);
#else
	dynarr_free(rman->wait_fds);
#endif
	resman_destroy_file_monitor(rman);

	pthread_mutex_destroy(&rman->lock);
}


void resman_set_load_func(struct resman *rman, resman_load_func func, void *cls)
{
	rman->load_func = func;
	rman->load_func_cls = cls;
}

void resman_set_done_func(struct resman *rman, resman_done_func func, void *cls)
{
	rman->done_func = func;
	rman->done_func_cls = cls;
}

void resman_set_destroy_func(struct resman *rman, resman_destroy_func func, void *cls)
{
	rman->destroy_func = func;
	rman->destroy_func_cls = cls;
}

void resman_setopt(struct resman *rman, int opt, int val)
{
	if(opt < 0 || opt >= RESMAN_NUM_OPTIONS) {
		return;
	}
	rman->opt[opt] = val;
}

int resman_getopt(struct resman *rman, int opt)
{
	if(opt < 0 || opt >= RESMAN_NUM_OPTIONS) {
		return 0;
	}
	return rman->opt[opt];
}

/* to avoid breaking backwards compatibility, resman_lookup is an alias for resman_add */
int resman_lookup(struct resman *rman, const char *fname, void *data)
{
	return resman_add(rman, fname, data);
}

int resman_add(struct resman *rman, const char *fname, void *data)
{
	int ridx;

	if((ridx = find_resource(rman, fname)) != -1) {
		return ridx;
	}

	/* resource not found, create a new one and start a loading job */
	return add_resource(rman, fname, data);
}

int resman_find(struct resman *rman, const char *fname)
{
	return find_resource(rman, fname);
}

int resman_remove(struct resman *rman, int id)
{
	rman->res[id]->delete_pending = 1;
	return 0;
}

int resman_pending(struct resman *rman)
{
	return resman_tpool_pending_jobs(rman->tpool);
}

void resman_wait_job(struct resman *rman, int id)
{
	int cur_jobs;
	struct resource *res = rman->res[id];

	pthread_mutex_lock(&res->lock);
	while(res->pending) {
		pthread_mutex_unlock(&res->lock);
		cur_jobs = resman_tpool_pending_jobs(rman->tpool);
		resman_tpool_wait_pending(rman->tpool, cur_jobs - 1);
		pthread_mutex_lock(&res->lock);
	}
	pthread_mutex_unlock(&res->lock);
}

void resman_wait_any(struct resman *rman)
{
	int cur_jobs = resman_tpool_pending_jobs(rman->tpool);
	resman_tpool_wait_pending(rman->tpool, cur_jobs - 1);
}

void resman_wait_all(struct resman *rman)
{
	resman_tpool_wait(rman->tpool);
}

int resman_poll(struct resman *rman)
{
	int i, num_res;
	unsigned long start_time, timeslice;

	/* first check all the resources to see if anyone is pending deletion */
	num_res = dynarr_size(rman->res);
	for(i=0; i<num_res; i++) {
		struct resource *res = rman->res[i];
		if(!res) {
			continue;
		}

		/* also make sure we're it's off the queues/workers before deleting */
		if(res->delete_pending && !res->pending) {
			if(rman->destroy_func) {
				rman->destroy_func(i, rman->destroy_func_cls);
			}
			remove_resource(rman, i);
		}
	}


	/* then check for modified files */
	resman_check_watch(rman);

#if !defined(WIN32) && !defined(__WIN32__)
	/* empty the thread pool event pipe (fd is nonblocking) */
	while(read(rman->tpool_wait_fd, &i, sizeof i) > 0);
#endif

	if(!rman->done_func) {
		return 0;	/* no done callback; there's no point in checking anything */
	}

	start_time = resman_get_time_msec();

	for(i=0; i<num_res; i++) {
		struct resource *res = rman->res[i];
		if(!res) {
			continue;
		}

		pthread_mutex_lock(&res->lock);
		if(!res->done_pending) {
			int reload = res->reload_timeout && res->reload_timeout <= start_time;
			pthread_mutex_unlock(&res->lock);
			if(reload) {
				printf("file \"%s\" modified, delayed reload\n", res->name);
				res->reload_timeout = 0;
				resman_reload(rman, res);
			}
			continue;
		}

		/* so a done callback *is* pending... */
		res->done_pending = 0;
		if(rman->done_func(i, rman->done_func_cls) == -1) {
			/* done-func returned -1, so let's remove the resource
			 * but only if this was the first load. Otherwise keep it
			 * around in case it gets valid again...
			 */
			if(res->num_loads == 0) {
				pthread_mutex_unlock(&res->lock);
				remove_resource(rman, i);
				continue;
			}
		}
		res->num_loads++;

		resman_start_watch(rman, res);	/* start watching the file for modifications */
		pthread_mutex_unlock(&res->lock);

		/* poll will be called with a high frequency anyway, so let's not spend
		 * too much time on done callbacks each time through it
		 */
		timeslice = rman->opt[RESMAN_OPT_TIMESLICE];
		if(timeslice > 0 && resman_get_time_msec() - start_time > timeslice) {
			break;
		}
	}
	return 0;
}

int resman_wait(struct resman *rman)
{
	wait_for_any_event(rman);
	return 0;
}

const char *resman_get_res_name(struct resman *rman, int res_id)
{
	if(res_id >= 0 && res_id < dynarr_size(rman->res)) {
		return rman->res[res_id]->name;
	}
	return 0;
}

void resman_set_res_data(struct resman *rman, int res_id, void *data)
{
	if(res_id >= 0 && res_id < dynarr_size(rman->res)) {
		rman->res[res_id]->data = data;
	}
}

void *resman_get_res_data(struct resman *rman, int res_id)
{
	if(res_id >= 0 && res_id < dynarr_size(rman->res)) {
		return rman->res[res_id]->data;
	}
	return 0;
}

int resman_get_res_result(struct resman *rman, int res_id)
{
	if(res_id >= 0 && res_id < dynarr_size(rman->res)) {
		return rman->res[res_id]->result;
	}
	return -1;
}

int resman_get_res_load_count(struct resman *rman, int res_id)
{
	if(res_id >= 0 && res_id < dynarr_size(rman->res)) {
		return rman->res[res_id]->num_loads;
	}
	return -1;
}

#if defined(WIN32) || defined(__WIN32__)
int *resman_get_wait_fds(struct resman *rman, int *num_fds)
{
	static int once;
	if(!once) {
		once = 1;
		fprintf(stderr, "warning: resman_get_wait_fds does nothing on windows\n");
	}
	return 0;
}

void **resman_get_wait_handles(struct resman *rman, int *num_handles)
{
	*num_handles = dynarr_size(rman->wait_handles);
	return rman->wait_handles;
}

static void wait_for_any_event(struct resman *rman)
{
	unsigned int num_handles;

	if(!(num_handles = dynarr_size(rman->wait_handles))) {
		return;
	}

	WaitForMultipleObjectsEx(num_handles, rman->wait_handles, FALSE, INFINITE, TRUE);
}

#else /* UNIX */
int *resman_get_wait_fds(struct resman *rman, int *num_fds)
{
	*num_fds = dynarr_size(rman->wait_fds);
	return rman->wait_fds;
}

void **resman_get_wait_handles(struct resman *rman, int *num_handles)
{
	static int once;
	if(!once) {
		once = 1;
		fprintf(stderr, "warning: resman_get_wait_handles does nothing on UNIX\n");
	}
	return 0;
}

static void wait_for_any_event(struct resman *rman)
{
	int i, res, numfds, maxfd = 0;
	fd_set rdset;

	if(!(numfds = dynarr_size(rman->wait_fds))) {
		return;
	}

	FD_ZERO(&rdset);
	for(i=0; i<numfds; i++) {
		int fd = rman->wait_fds[i];
		FD_SET(fd, &rdset);
		if(fd > maxfd) maxfd = fd;
	}

	while((res = select(maxfd + 1, &rdset, 0, 0, 0)) == -1 && errno == EINTR);

	if(res == -1) {
		fprintf(stderr, "failed to wait for any events: %s\n", strerror(errno));
	}
}
#endif

static int find_resource(struct resman *rman, const char *fname)
{
	int i, sz = dynarr_size(rman->res);

	for(i=0; i<sz; i++) {
		if(strcmp(rman->res[i]->name, fname) == 0) {
			return i;
		}
	}
	return -1;
}

static int add_resource(struct resman *rman, const char *fname, void *data)
{
	int i, idx = -1, size = dynarr_size(rman->res);
	struct resource *res;
	struct resource **tmparr;

	/* allocate a new resource */
	if(!(res = malloc(sizeof *res))) {
		return -1;
	}
	memset(res, 0, sizeof *res);

	res->name = strdup(fname);
	assert(res->name);
	res->data = data;
	pthread_mutex_init(&res->lock, 0);

	/* check to see if there's an empty (previously erased) slot */
	for(i=0; i<size; i++) {
		if(!rman->res[i]) {
			idx = i;
			break;
		}
	}

	if(idx == -1) {
		/* free slot not found, append a new one */
		idx = size;

		if(!(tmparr = dynarr_push(rman->res, &res))) {
			free(res->name);
			free(res);
			return -1;
		}
		rman->res = tmparr;
	} else {
		/* free slot found, just use it */
		res = rman->res[idx];
	}

	res->id = idx;	/* set the resource id */

	resman_reload(rman, rman->res[idx]);
	return idx;
}

void resman_reload(struct resman *rman, struct resource *res)
{
	struct work_item *work;

	/* start a loading job ... */
	pthread_mutex_lock(&rman->lock);
	work = alloc_work_item(rman);
	pthread_mutex_unlock(&rman->lock);
	work->res = res;

	res->pending = 1;
	resman_tpool_enqueue(rman->tpool, work, work_func, 0);
}

/* remove a resource and leave the pointer null to reuse the slot */
static void remove_resource(struct resman *rman, int idx)
{
	struct resource *res = rman->res[idx];

	resman_stop_watch(rman, res);

	if(rman->destroy_func) {
		rman->destroy_func(idx, rman->destroy_func_cls);
	}

	pthread_mutex_destroy(&res->lock);

	free(res->name);
	free(res);
	rman->res[idx] = 0;
}

/* this is the background work function which handles all the
 * first-stage resource loading...
 */
static void work_func(void *cls)
{
	struct work_item *work = cls;
	struct resource *res = work->res;
	struct resman *rman = work->rman;

	pthread_mutex_lock(&res->lock);
	free_work_item(rman, work);
	pthread_mutex_unlock(&res->lock);

	res->result = rman->load_func(res->name, res->id, rman->load_func_cls);

	pthread_mutex_lock(&res->lock);
	res->pending = 0;	/* no longer being worked on */

	if(!rman->done_func) {
		if(res->result == -1) {
			/* if there's no done function and we got an error, mark this
			 * resource for deletion in the caller context. But only if this
			 * is the first load of this resource.
			 */
			if(res->num_loads == 0) {
				res->delete_pending = 1;
			}
		} else {
			/* succeded, start a watch */
			resman_start_watch(rman, res);
		}
	} else {
		/* if we have a done_func, mark this resource as done */
		res->done_pending = 1;
	}
	pthread_mutex_unlock(&res->lock);
}

static struct work_item *alloc_work_item(struct resman *rman)
{
	struct work_item *res;

	if(rman->work_items) {
		res = rman->work_items;
		rman->work_items = res->next;
	} else {
		if(!(res = malloc(sizeof *res))) {
			perror("failed to allocate resman work item");
			abort();
		}
		res->rman = rman;
	}
	return res;
}

static void free_work_item(struct resman *rman, struct work_item *w)
{
	w->next = rman->work_items;
	rman->work_items = w;
}
