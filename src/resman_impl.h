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
#ifndef RESMAN_IMPL_H_
#define RESMAN_IMPL_H_

#include <pthread.h>
#include "rbtree.h"
#include "tpool.h"

#ifdef __linux__
#include <unistd.h>
#include <fcntl.h>
#include <sys/inotify.h>
#endif
#ifdef WIN32
#include <windows.h>
#endif
#include "resman.h"

struct work_item;

struct resource {
	int id;
	char *name;
	void *data;
	int result;	/* last callback-reported success/fail code */

	int pending;		/* is being enqueued or actively worked on */
	int done_pending;	/* loading completed but done callback not called yet */
	int delete_pending;	/* marked for deletion during the next poll */
	pthread_mutex_t lock;

	int num_loads;		/* number of loads up to now */

	/* file change monitoring */
#ifdef WIN32
	char *watch_path;
#endif
#ifdef __linux__
	int nfd;
#endif
};


struct resman {
	struct resource **res;
	struct resman_thread_pool *tpool;

	pthread_mutex_t lock;	/* global resman lock (for res array changes) */

	resman_load_func load_func;
	resman_done_func done_func;
	resman_destroy_func destroy_func;

	void *load_func_cls;
	void *done_func_cls;
	void *destroy_func_cls;

	/* file change monitoring */
	struct rbtree *nresmap;
#ifdef __linux__
	int inotify_fd;
	struct rbtree *modset;
#endif
#ifdef WIN32
	struct rbtree *watchdirs, *wdirbyev;
	HANDLE *watch_handles;	/* dynamic array of all the watched directory handles */
#endif

	/* list of free work item structures for the work item allocatro */
	struct work_item *work_items;
};

void resman_reload(struct resman *rman, struct resource *res);


#endif	/* RESMAN_IMPL_H_ */
