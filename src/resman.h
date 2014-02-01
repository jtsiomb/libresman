#ifndef RESOURCE_MANAGER_H_
#define RESOURCE_MANAGER_H_

/* load callback: everything or just file read/parse stage
 * create callback: second-stage callback for first creation
 * update callback: second-stage callback for reload
 */
typedef int (*resman_load_func)(const char *fname, void *data, void *closure);
typedef int (*resman_create_func)(void *data, void *closure);
typedef int (*resman_update_func)(void *data, void *closure);
typedef void (*resman_destroy_func)(void *data, void *closure);

struct resman;

struct resman *resman_create(void);
void resman_free(struct resman *rman);

int resman_init(struct resman *rman);
void resman_destroy(struct resman *rman);

void resman_set_load_func(struct resman *rman, resman_load_func func, void *cls);
void resman_set_create_func(struct resman *rman, resman_create_func func, void *cls);
void resman_set_update_func(struct resman *rman, resman_update_func func, void *cls);
void resman_set_destroy_func(struct resman *rman, resman_destroy_func func, void *cls);

int resman_lookup(struct resman *rman, const char *fname, void *data);
void resman_wait(struct resman *rman, int id);

int resman_poll(struct resman *rman);

void resman_set_res_data(struct resman *rman, int res_id, void *data);
void *resman_get_res_data(struct resman *rman, int res_id);


#endif	/* RESOURCE_MANAGER_H_ */
