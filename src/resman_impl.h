#ifndef RESMAN_IMPL_H_
#define RESMAN_IMPL_H_

#include <pthread.h>
#include "rbtree.h"
#include "threadpool.h"

#ifdef __linux__
#include <unistd.h>
#include <fcntl.h>
#include <sys/inotify.h>
#endif

struct resource {
	int id;
	char *name;
	void *data;
	int result;	/* last callback-reported success/fail code */

	int done_pending;
	int delete_pending;
	pthread_mutex_t lock;

	int num_loads;		/* number of loads up to now */

	/* file change monitoring */
#ifdef __WIN32__
	HANDLE nhandle;
#endif
#ifdef __linux__
	int nfd;
#endif
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

	/* file change monitoring */
	struct rbtree *nresmap;
	struct rbtree *modset;
#ifdef __linux__
	int inotify_fd;
#endif
};


#endif	/* RESMAN_IMPL_H_ */
