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
	pthread_mutex_t done_lock;
};

struct resman {
	struct resource **res;
	struct thread_pool *tpool;

	resman_load_func load_func;
	resman_done_func done_func;
	resman_destroy_func destroy_func;

	void *load_func_cls;
	void *done_func_cls;
	void *destroy_func_cls;
};


static int find_resource(struct resman *rman, const char *fname);
static int add_resource(struct resman *rman, const char *fname, void *data);
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
	memset(rman, 0, sizeof *rman);

	if(!(rman->tpool = tpool_create(TPOOL_AUTO))) {
		return -1;
	}
	tpool_set_work_func(rman->tpool, work_func, rman);

	if(!(rman->res = dynarr_alloc(0, sizeof *rman->res))) {
		tpool_free(rman->tpool);
		return -1;
	}

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

	if(!rman->done_func) {
		return 0;	/* no done callback; there's no point in checking anything */
	}

	num_res = dynarr_size(rman->res);
	for(i=0; i<num_res; i++) {
		struct resource *res = rman->res[i];

		printf("locking mutex %d\n", res->id);
		pthread_mutex_lock(&res->done_lock);
		if(!res->done_pending) {
			printf("  unlocking mutex %d\n", res->id);
			pthread_mutex_unlock(&res->done_lock);
			continue;
		}

		/* so a done callback *is* pending... */
		res->done_pending = 0;
		rman->done_func(i, rman->done_func_cls);
		printf("  unlocking mutex %d\n", res->id);
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
	int idx = dynarr_size(rman->res);
	struct resource *res;
	struct resource **tmparr;

	if(!(res = malloc(sizeof *res))) {
		return -1;
	}
	res->id = idx;
	res->name = strdup(fname);
	assert(res->name);
	res->data = data;

	pthread_mutex_init(&res->done_lock, 0);

	if(!(tmparr = dynarr_push(rman->res, &res))) {
		free(res);
		return -1;
	}
	rman->res = tmparr;

	/* start a loading job ... */
	tpool_add_work(rman->tpool, rman->res[idx]);
	return idx;
}

/* this is the background work function which handles all the
 * first-stage resource loading...
 */
static void work_func(void *data, void *cls)
{
	struct resource *res = data;
	struct resman *rman = cls;

	res->result = rman->load_func(res->name, res->id, rman->load_func_cls);

	printf("locking mutex %d\n", res->id);
	pthread_mutex_lock(&res->done_lock);
	res->done_pending = 1;
	printf("  unlocking mutex %d\n", res->id);
	pthread_mutex_unlock(&res->done_lock);
}
