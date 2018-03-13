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
#ifndef RESOURCE_MANAGER_H_
#define RESOURCE_MANAGER_H_

/* load callback: everything or just file read/parse stage
 * done callback: second-stage callback, called in the context of the
 *                user thread, after the load callback returns
 */
typedef int (*resman_load_func)(const char *fname, int id, void *closure);
typedef int (*resman_done_func)(int id, void *closure);
typedef void (*resman_destroy_func)(int id, void *closure);

struct resman;

#ifdef __cplusplus
extern "C" {
#endif

struct resman *resman_create(void);
void resman_free(struct resman *rman);

int resman_init(struct resman *rman);
void resman_destroy(struct resman *rman);

/* set the function to be called when a resource file needs to be loaded.
 * this function should perform I/O and any parts of loading which can be done
 * in a background thread.  */
void resman_set_load_func(struct resman *rman, resman_load_func func, void *cls);
/* set the function to be called when loading of a resource file is completed.
 * this function is called in the context of the main thread (the thread which
 * calls resman_poll), and should be as fast as possible to avoid blocking the
 * main thread for long.  */
void resman_set_done_func(struct resman *rman, resman_done_func func, void *cls);
/* set the function to be called when a resource needs to be destroyed.
 * this function is also called in the context of the main thread. */
void resman_set_destroy_func(struct resman *rman, resman_destroy_func func, void *cls);

/* call resman_add to add a new resource file and trigger the loading process.
 * If the file is already managed, this function is a no-op.
 * Returns the resource id. */
int resman_add(struct resman *rman, const char *fname, void *data);
/* resman_find returns the resource id associated with a filename.
 * If no match is found, resman_find returns -1. */
int resman_find(struct resman *rman, const char *fname);
/* resman_remove removes and destroys a resource. Further queries with this
 * resource identifier will lead to undefined behavior. */
int resman_remove(struct resman *rman, int id);

/* returns number of pending jobs */
int resman_pending(struct resman *rman);
void resman_wait(struct resman *rman, int id);
void resman_waitall(struct resman *rman);

/* call resman_poll in your main thread to schedule done/destroy callbacks */
int resman_poll(struct resman *rman);

const char *resman_get_res_name(struct resman *rman, int res_id);

void resman_set_res_data(struct resman *rman, int res_id, void *data);
void *resman_get_res_data(struct resman *rman, int res_id);

int resman_get_res_result(struct resman *rman, int res_id);

int resman_get_res_load_count(struct resman *rman, int res_id);

#ifdef __cplusplus
}
#endif


#endif	/* RESOURCE_MANAGER_H_ */
