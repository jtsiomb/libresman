/* file modification monitoring for windows */
#ifdef WIN32
#include <stdio.h>
#include <assert.h>
#include <malloc.h>
#include "filewatch.h"
#include "resman.h"
#include "resman_impl.h"
#include "dynarr.h"

struct watch_dir {
	HANDLE handle;
	int nref;
};

static void reload_modified(struct rbnode *node, void *cls);

int resman_init_file_monitor(struct resman *rman)
{
	if(!(rman->watch_handles = dynarr_alloc(0, sizeof *rman->watch_handles))) {
		return -1;
	}

	/* create the handle->resource map */
	rman->nresmap = rb_create(RB_KEY_ADDR);
	/* create the watched dirs set */
	rman->watchdirs = rb_create(RB_KEY_STRING);
	return 0;
}

void resman_destroy_file_monitor(struct resman *rman)
{
	dynarr_free(rman->watch_handles);

	rb_free(rman->nresmap);
	rb_free(rman->watchdirs);
}

int resman_start_watch(struct resman *rman, struct resource *res)
{
	char *path;
	HANDLE handle;
	struct watch_dir *wdir;

	/* construct an absolute path for the directory containing this file */
	path = res->name;
	if(path[0] != '/' && path[1] != ':') {	/* not an absolute path */
		char *src, *dest, *lastslash;
		int cwdsz = GetCurrentDirectory(0, 0);
		int pathsz = strlen(path) + cwdsz + 1;

		path = malloc(pathsz + 1);
		GetCurrentDirectory(pathsz, path);

		/* now copy the rest of the path, until the last slash, while converting path separators */
		src = res->name;
		dest = path + strlen(path);

		lastslash = dest;
		*dest++ = '\\';
		while(*src) {
			if(src[-1] == '\\') {
				/* skip any /./ parts of the path */
				if(src[0] == '.' && (src[1] == '/' || src[1] == '\\')) {
					src += 2;
					continue;
				}
				/* normalize any /../ parts of the path */
				if(src[0] == '.' && src[1] == '.' && (src[2] == '/' || src[2] == '\\')) {
					src += 3;
					dest = strrchr(src - 2, '\\');
					assert(dest);
					dest++;
					continue;
				}
			}

			if(*src == '/' || *src == '\\') {
				lastslash = dest;
				*dest++ = '\\';
				src++;
			} else {
				*dest++ = *src++;
			}
		}

		*lastslash = 0;
	}

	/* check to see if we already have a watch handle for this directory */
	if((wdir = rb_find(rman->watchdirs, path))) {
		handle = wdir->handle;
		wdir->nref++;
	} else {
		if(!(wdir = malloc(sizeof *wdir))) {
			perror("failed to allocate watchdir");
			free(path);
			return -1;
		}

		/* otherwise start a new notification */
		if((handle = FindFirstChangeNotification(path, FALSE, FILE_NOTIFY_CHANGE_LAST_WRITE)) == INVALID_HANDLE_VALUE) {
			unsigned int err = GetLastError();
			fprintf(stderr, "failed to watch %s for modification (error: %u)\n", path, err);
			free(wdir);
			free(path);
			return -1;
		}
		wdir->handle = handle;
		wdir->nref = 1;

		rb_insert(rman->watchdirs, path, wdir);
		dynarr_push(rman->watch_handles, &handle);
	}

	rb_insert(rman->nresmap, handle, res);
	res->nhandle = handle;
	res->watch_path = path;
	return 0;
}

void resman_stop_watch(struct resman *rman, struct resource *res)
{
	int i, sz;

	if(res->nhandle) {
		struct watch_dir *wdir = rb_find(rman->watchdirs, res->watch_path);
		if(wdir) {
			if(--wdir->nref <= 0) {
				FindCloseChangeNotification(res->nhandle);

				/* find the handle in the watch_handles array and remove it */
				sz = dynarr_size(rman->watch_handles);
				for(i=0; i<sz; i++) {
					if(rman->watch_handles[i] == res->nhandle) {
						/* swap the end for it and pop */
						rman->watch_handles[i] = rman->watch_handles[sz - 1];
						rman->watch_handles[sz - 1] = 0;
						dynarr_pop(rman->watch_handles);
						break;
					}
				}
			}
			free(wdir);
		}

		rb_delete(rman->nresmap, res->nhandle);
		res->nhandle = 0;
	}
}

void resman_check_watch(struct resman *rman)
{
	unsigned int num_handles = dynarr_size(rman->watch_handles);
	for(;;) {
		struct resource *res;
		unsigned int idx = WaitForMultipleObjects(num_handles, rman->watch_handles, FALSE, 0);
		if(idx < WAIT_OBJECT_0 || idx >= WAIT_OBJECT_0 + num_handles) {
			break;
		}

		if(!(res = rb_find(rman->nresmap, rman->watch_handles[idx]))) {
			fprintf(stderr, "got modification event from unknown resource!\n");
			continue;
		}

		printf("file \"%s\" modified\n", res->name);
		tpool_add_work(rman->tpool, res);
	}
}

#else
int resman_filewatch_win32_silence_empty_file_warning;
#endif	/* WIN32 */
