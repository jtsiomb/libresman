#ifndef RESOURCE_MANAGER_H_
#define RESOURCE_MANAGER_H_

/* TODO API */

/* usage example:
int texload(const char *fname, void *cls)
{
	// open image, parse data ...
}

struct resman rman;

resman_init(&rman);
resman_set_load_func(&rman, texload, 0);
resman_set_done_func(&rman, texload_done, 0);
...

struct texture *tex;
struct resman_job *rjob;

rjob = resman_get(&rman, "tex.png", 0);
...
resman_wait_job(&rman, rjob);
tex = resman_get_job_data(rjob);
resman_free_job(&rman, rjob);

...
resman_destroy(&rman);
*/

struct resman;

typedef int (*resman_load_func_t)(const char *fname, void *cls);
typedef void (*resman_done_func_t)(int status, void *cls);


int resman_init(struct resman *rman);
void resman_destroy(struct resman *rman);

/* The load callback will be called to load a data file. It may be called in the
 * context of a different loading thread.
 */
void resman_set_load_func(struct resman *rman, resman_load_func_t func, void *cls);

/* The "done" callback will be called in the context of the main thread, whenever a
 * file was sucessfully loaded, or an error occured.
 * It's first argument (status) is set to whatever the load function returned, and its
 * closure pointer is the closure 
 */
void resman_set_done_func(struct resman *rman, resman_done_func_t func);

#endif	/* RESOURCE_MANAGER_H_ */
