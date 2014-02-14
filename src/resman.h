/*
libresman - a multithreaded resource data file manager.
Copyright (C) 2014  John Tsiombikas <nuclear@member.fsf.org>

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

void resman_set_load_func(struct resman *rman, resman_load_func func, void *cls);
void resman_set_done_func(struct resman *rman, resman_done_func func, void *cls);
void resman_set_destroy_func(struct resman *rman, resman_destroy_func func, void *cls);

int resman_lookup(struct resman *rman, const char *fname, void *data);
void resman_wait(struct resman *rman, int id);

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
