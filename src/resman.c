#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <pthread.h>
#include "resman.h"
#include "dynarr.h"
#include "threadpool.h"

struct resource {
	int id;
	char *name;
	void *data;
	int result;	/* last callback-reported success/fail code */

	int done_pending;
	int delete_pending;
	pthread_mutex_t done_lock;
};

struct resman {
	struct resource **res;
	struct thread_pool *tpool;

	pthread_mutex_t lock;	/* global resman lock (for res array changes) */

	resman_load_func load_func;
	resman_done_func done_func;
	resman_destroy_func destroy_func;

	void *load_func_cls;
	void *done_func_cls;
	void *destroy_func_cls;
};


static int find_resource(struct resman *rman, const char *fname);
static int add_resource(struct resman *rman, const char *fname, void *data);
static void remove_resource(struct resman *rman, int idx);
static void work_func(void *data, void *cls);

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
	int num_threads = TPOOL_AUTO;

	memset(rman, 0, sizeof *rman);

	if((env = getenv("RESMAN_THREADS"))) {
		num_threads = atoi(env);
	}

	if(!(rman->tpool = tpool_create(num_threads))) {
		return -1;
	}
	tpool_set_work_func(rman->tpool, work_func, rman);

	if(!(rman->res = dynarr_alloc(0, sizeof *rman->res))) {
		tpool_free(rman->tpool);
		return -1;
	}

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
		free(rman->res[i]);
	}
	dynarr_free(rman->res);

	tpool_free(rman->tpool);

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

int resman_lookup(struct resman *rman, const char *fname, void *data)
{
	int ridx;

	if((ridx = find_resource(rman, fname)) != -1) {
		return ridx;
	}

	/* resource not found, create a new one and start a loading job */
	return add_resource(rman, fname, data);
}

void resman_wait(struct resman *rman, int id)
{
	/* TODO */
}

int resman_poll(struct resman *rman)
{
	int i, num_res;

	/* first check all the resources to see if any is pending deletion */
	num_res = dynarr_size(rman->res);
	for(i=0; i<num_res; i++) {
		struct resource *res = rman->res[i];
		if(!res) {
			continue;
		}

		if(res->delete_pending) {
			if(rman->destroy_func) {
				rman->destroy_func(i, rman->destroy_func_cls);
			}
			remove_resource(rman, i);
		}
	}


	if(!rman->done_func) {
		return 0;	/* no done callback; there's no point in checking anything */
	}

	for(i=0; i<num_res; i++) {
		struct resource *res = rman->res[i];
		if(!res) {
			continue;
		}

		pthread_mutex_lock(&res->done_lock);
		if(!res->done_pending) {
			pthread_mutex_unlock(&res->done_lock);
			continue;
		}

		/* so a done callback *is* pending... */
		res->done_pending = 0;
		if(rman->done_func(i, rman->done_func_cls) == -1) {
			/* done-func returned -1, so let's remove the resource */
			pthread_mutex_unlock(&res->done_lock);
			remove_resource(rman, i);
			continue;
		}
		pthread_mutex_unlock(&res->done_lock);
	}
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
	pthread_mutex_init(&res->done_lock, 0);


	/* check to see if there's an emtpy (previously erased) slot */
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
			free(res);
			return -1;
		}
		rman->res = tmparr;
	} else {
		/* free slot found, just use it */
		res = rman->res[idx];
	}

	res->id = idx;	/* set the resource id */

	/* start a loading job ... */
	tpool_add_work(rman->tpool, rman->res[idx]);
	return idx;
}

/* remove a resource and leave the pointer null to reuse the slot */
static void remove_resource(struct resman *rman, int idx)
{
	if(rman->destroy_func) {
		rman->destroy_func(idx, rman->destroy_func_cls);
	}

	pthread_mutex_destroy(&rman->res[idx]->done_lock);

	free(rman->res[idx]);
	rman->res[idx] = 0;
}

/* this is the background work function which handles all the
 * first-stage resource loading...
 */
static void work_func(void *data, void *cls)
{
	struct resource *res = data;
	struct resman *rman = cls;

	res->result = rman->load_func(res->name, res->id, rman->load_func_cls);
	if(res->result == -1 && !rman->done_func) {
		/* if there's no done function and we got an error, mark this
		 * resource for deletion in the caller context
		 */
		res->delete_pending = 1;
		return;
	}

	pthread_mutex_lock(&res->done_lock);
	res->done_pending = 1;
	pthread_mutex_unlock(&res->done_lock);
}
