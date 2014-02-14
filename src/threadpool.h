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
#ifndef THREAD_POOL_H_
#define THREAD_POOL_H_

struct thread_pool;

typedef void (*tpool_work_func)(void *data, void *cls);

#define TPOOL_AUTO	0
int tpool_init(struct thread_pool *tpool, int num_threads);
void tpool_destroy(struct thread_pool *tpool);

struct thread_pool *tpool_create(int num_threads);
void tpool_free(struct thread_pool *tpool);

void tpool_set_work_func(struct thread_pool *tpool, tpool_work_func func, void *cls);

int tpool_add_work(struct thread_pool *tpool, void *data);

#endif	/* THREAD_POOL_H_ */
