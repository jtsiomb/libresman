#ifndef RESOURCE_MANAGER_H_
#define RESOURCE_MANAGER_H_

struct resman;

int resman_init(struct resman *rman);
void resman_destroy(struct resman *rman);

/* The load callback will be called to load a data file. It may be called in the
 * context of a different loading thread.
 */
/*void resman_set_load_func(struct resman *rman, resman_load_func_t func, void *cls);*/

/* The "done" callback will be called in the context of the main thread, whenever a
 * file was sucessfully loaded, or an error occured.
 * It's first argument (status) is set to whatever the load function returned, and its
 * closure pointer is the closure  ...
 */
/*void resman_set_done_func(struct resman *rman, resman_done_func_t func);*/


int resman_lookup(struct resman *rman, const char *fname, void *cls);
void resman_wait(struct resman *rman, int id);

int resman_poll(struct resman *rman);

void resman_set_res_data(struct resman *rman, int res_id, void *data);
void *resman_get_res_data(struct resman *rman, int res_id);


#endif	/* RESOURCE_MANAGER_H_ */
