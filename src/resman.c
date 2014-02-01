#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "resman.h"
#include "dynarr.h"
#include "threadpool.h"

struct resource {
	char *name;
	void *data;
};

struct resman {
	struct resource *res;

	resman_load_func load_func;
	resman_create_func create_func;
	resman_update_func update_func;
	resman_destroy_func destroy_func;

	void *load_func_cls;
	void *create_func_cls;
	void *update_func_cls;
	void *destroy_func_cls;
};


static int find_resource(struct resman *rman, const char *fname);
static int add_resource(struct resman *rman, const char *fname, void *data);

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

	if(!(rman->res = dynarr_alloc(0, sizeof *rman->res))) {
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
			rman->destroy_func(rman->res[i].data, rman->destroy_func_cls);
		}
	}
	dynarr_free(rman->res);
}


void resman_set_load_func(struct resman *rman, resman_load_func func, void *cls)
{
	rman->load_func = func;
	rman->load_func_cls = cls;
}

void resman_set_create_func(struct resman *rman, resman_create_func func, void *cls)
{
	rman->create_func = func;
	rman->create_func_cls = cls;
}

void resman_set_update_func(struct resman *rman, resman_update_func func, void *cls)
{
	rman->update_func = func;
	rman->update_func_cls = cls;
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
	/* TODO */
	return 0;
}


void resman_set_res_data(struct resman *rman, int res_id, void *data)
{
	if(res_id >= 0 && res_id < dynarr_size(rman->res)) {
		rman->res[res_id].data = data;
	}
}

void *resman_get_res_data(struct resman *rman, int res_id)
{
	if(res_id >= 0 && res_id < dynarr_size(rman->res)) {
		return rman->res[res_id].data;
	}
	return 0;
}

static int find_resource(struct resman *rman, const char *fname)
{
	int i, sz = dynarr_size(rman->res);

	for(i=0; i<sz; i++) {
		if(strcmp(rman->res[i].name, fname) == 0) {
			return i;
		}
	}
	return -1;
}

static int add_resource(struct resman *rman, const char *fname, void *data)
{
	int idx = dynarr_size(rman->res);

	struct resource *tmp = dynarr_push(rman->res, 0);
	if(!tmp) {
		return -1;
	}
	rman->res = tmp;

	rman->res[idx].name = strdup(fname);
	assert(rman->res[idx].name);

	rman->res[idx].data = data;

	/* TODO start a loading job ... */

	return idx;
}
