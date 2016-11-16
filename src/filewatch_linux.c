/*
libresman - a multithreaded resource data file manager.
Copyright (C) 2014-2016  John Tsiombikas <nuclear@member.fsf.org>

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
/* file modification monitoring with inotify */
#if !defined(NOWATCH) && defined(__linux__)
#include <stdio.h>
#include <assert.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/inotify.h>
#include "filewatch.h"
#include "resman.h"
#include "resman_impl.h"

static void reload_modified(struct rbnode *node, void *cls);

int resman_init_file_monitor(struct resman *rman)
{
	int fd;

	if((fd = inotify_init()) == -1) {
		return -1;
	}
	/* set non-blocking flag, to allow polling by reading */
	fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) | O_NONBLOCK);
	rman->inotify_fd = fd;

	/* create the fd->resource map */
	rman->nresmap = rb_create(RB_KEY_INT);
	/* create the modified set */
	rman->modset = rb_create(RB_KEY_INT);
	return 0;
}

void resman_destroy_file_monitor(struct resman *rman)
{
	rb_free(rman->nresmap);
	rb_free(rman->modset);

	if(rman->inotify_fd >= 0) {
		close(rman->inotify_fd);
		rman->inotify_fd = -1;
	}
}

int resman_start_watch(struct resman *rman, struct resource *res)
{
	int fd;

	if(res->nfd > 0) {
		return 0;	/* already started a watch for this resource */
	}

	if((fd = inotify_add_watch(rman->inotify_fd, res->name, IN_MODIFY)) == -1) {
		return -1;
	}
	printf("started watching file \"%s\" for modification (fd %d)\n", res->name, fd);
	rb_inserti(rman->nresmap, fd, res);

	res->nfd = fd;
	return 0;
}

void resman_stop_watch(struct resman *rman, struct resource *res)
{
	if(res->nfd > 0) {
		rb_deletei(rman->nresmap, res->nfd);
		inotify_rm_watch(rman->inotify_fd, res->nfd);
	}
}

void resman_check_watch(struct resman *rman)
{
	char buf[512];
	struct inotify_event *ev;
	int sz, evsize;

	while((sz = read(rman->inotify_fd, buf, sizeof buf)) > 0) {
		ev = (struct inotify_event*)buf;
		while(sz > 0) {
			if(ev->mask & IN_MODIFY) {
				/* add the file descriptor to the modified set */
				rb_inserti(rman->modset, ev->wd, 0);
			}

			evsize = sizeof *ev + ev->len;
			sz -= evsize;
			ev += evsize;
		}
	}

	/* for each item in the modified set, start a new job to reload it */
	rb_foreach(rman->modset, reload_modified, rman);
	rb_clear(rman->modset);
}

/* this is called for each item in the modified set (see above) */
static void reload_modified(struct rbnode *node, void *cls)
{
	int watch_fd;
	struct resource *res;
	struct resman *rman = cls;

	watch_fd = rb_node_keyi(node);

	if(!(res = rb_findi(rman->nresmap, watch_fd))) {
		fprintf(stderr, "reload_modified: can't find resource for watch descriptor: %d\n", watch_fd);
		return;
	}
	assert(watch_fd == res->nfd);

	printf("file \"%s\" modified (fd %d)\n", res->name, rb_node_keyi(node));

	resman_reload(rman, res);
}

#else
int resman_filewatch_linux_silence_empty_file_warning;
#endif	/* __linux__ */
