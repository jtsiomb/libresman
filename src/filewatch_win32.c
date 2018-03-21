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
/* file modification monitoring for windows */
#if !defined(NOWATCH) && defined(WIN32)
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <malloc.h>
#include "filewatch.h"
#include "resman.h"
#include "resman_impl.h"
#include "dynarr.h"

#define RES_BUF_SIZE	8192

struct watch_item {
	struct resource *res;
	struct watch_item *next;
};

struct watch_dir {
	HANDLE handle;
	OVERLAPPED over;	/* overlapped I/O structure */
	char *buf_unaligned, *buf;
	int nref;
	char *watch_path;

	struct watch_item *items;
};

static char *abs_path(const char *fname);
static void clean_path(char *path);

int resman_init_file_monitor(struct resman *rman)
{
	rman->nresmap = rb_create(RB_KEY_ADDR);
	rman->watchdirs = rb_create(RB_KEY_STRING);
	rman->wdirbyev = rb_create(RB_KEY_ADDR);

	return 0;
}

void resman_destroy_file_monitor(struct resman *rman)
{
	rb_free(rman->nresmap);
	rb_free(rman->watchdirs);
	rb_free(rman->wdirbyev);
}

int resman_start_watch(struct resman *rman, struct resource *res)
{
	char *path = 0, *last_slash;
	struct watch_dir *wdir = 0;
	struct watch_item *witem = 0;

	/* construct an absolute path for the directory containing this file (must free it afterwards) */
	if(!(path = abs_path(res->name))) {
		return -1;
	}
	clean_path(path);

	/* we need the directory path, so let's find the last slash and cut it there */
	if(!(last_slash = strrchr(path, '\\'))) {
		goto err;
	}
	*last_slash = 0;

	/* check to see if we already have a watch handle for this directory */
	if((wdir = rb_find(rman->watchdirs, path))) {
		wdir->nref++;	/* ... if so, increase the refcount */
	} else {
		/* otherwise start a new watch */
		if(!(wdir = malloc(sizeof *wdir))) {
			perror("failed to allocate watchdir");
			goto err;
		}
		memset(wdir, 0, sizeof *wdir);
		wdir->nref = 1;

		/* open the directory we need to watch */
		wdir->handle = CreateFile(path, FILE_LIST_DIRECTORY, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
			0, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED, 0);
		if(wdir->handle == INVALID_HANDLE_VALUE) {
			fprintf(stderr, "failed to watch %s: failed to open directory: %s\n", res->name, path);
			goto err;
		}

		if(!(wdir->buf_unaligned = malloc(RES_BUF_SIZE + 3))) {
			fprintf(stderr, "failed to allocate watch result buffer (%d bytes)\n", RES_BUF_SIZE);
			goto err;
		}
		wdir->buf = (char*)(((intptr_t)wdir->buf_unaligned + 3) & ~(intptr_t)0x3);

		memset(&wdir->over, 0, sizeof wdir->over);
		wdir->over.hEvent = CreateEvent(0, TRUE, FALSE, 0);

		if(!ReadDirectoryChangesW(wdir->handle, wdir->buf, RES_BUF_SIZE, 0, FILE_NOTIFY_CHANGE_LAST_WRITE, 0, &wdir->over, 0)) {
			fprintf(stderr, "failed to start async dirchange monitoring\n");
			goto err;
		}

		wdir->watch_path = path;

		rb_insert(rman->watchdirs, path, wdir);
		rb_insert(rman->wdirbyev, wdir->over.hEvent, wdir);
		rman->wait_handles = dynarr_push(rman->wait_handles, &wdir->over.hEvent);
	}

	/* add a new watch item to this watch dir */
	if(!(witem = malloc(sizeof *witem))) {
		perror("failed to allocate watch item");
		goto err;
	}
	witem->next = wdir->items;
	wdir->items = witem;
	witem->res = res;

	res->watch_path = path;
	return 0;
err:
	free(path);
	if(wdir) {
		if(wdir->handle && wdir->handle != INVALID_HANDLE_VALUE) {
			CloseHandle(wdir->handle);
		}
		free(wdir->buf_unaligned);
		if(wdir->over.hEvent && wdir->over.hEvent != INVALID_HANDLE_VALUE) {
			CloseHandle(wdir->over.hEvent);
		}
	}
	return -1;
}

void resman_stop_watch(struct resman *rman, struct resource *res)
{
	int i, sz;
	struct watch_dir *wdir;

	if(!res->watch_path) {
		return;
	}

	if(!(wdir = rb_find(rman->watchdirs, res->watch_path))) {
		return;
	}

	/* if there is no other reference to this watch dir, destroy it */
	if(--wdir->nref <= 0) {
		/* find the handle in the wait_handles array and remove it */
		sz = dynarr_size(rman->wait_handles);
		for(i=0; i<sz; i++) {
			if(rman->wait_handles[i] == wdir->handle) {
				/* swap the end for it and pop */
				rman->wait_handles[i] = rman->wait_handles[sz - 1];
				rman->wait_handles[sz - 1] = 0;
				dynarr_pop(rman->wait_handles);
				break;
			}
		}

		rb_delete(rman->wdirbyev, wdir->over.hEvent);
		rb_delete(rman->watchdirs, wdir->watch_path);

		CancelIo(wdir->handle);
		CloseHandle(wdir->handle);
		CloseHandle(wdir->over.hEvent);
		free(wdir->watch_path);
		free(wdir);

		res->watch_path = 0;
	} else {
		/* just remove this watch item */
		if(wdir->items && wdir->items->res == res) {
			struct watch_item *tmp = wdir->items;
			wdir->items = wdir->items->next;
			free(tmp);
		} else {
			struct watch_item *wprev = wdir->items;
			struct watch_item *witem = wprev->next;

			while(witem) {
				if(witem->res == res) {
					struct watch_item *tmp = witem;
					wprev->next = witem->next;
					break;
				}
				witem = witem->next;
			}
		}
	}
}

static void handle_event(struct resman *rman, HANDLE hev, struct watch_dir *wdir)
{
	struct resource *res = 0;
	struct watch_item *witem;
	FILE_NOTIFY_INFORMATION *info;
	DWORD res_size;

	if(!GetOverlappedResult(hev, &wdir->over, &res_size, FALSE)) {
		return;
	}

	info = (FILE_NOTIFY_INFORMATION*)wdir->buf;

	for(;;) {
		if(info->Action == FILE_ACTION_MODIFIED) {
			char *name;
			int len = info->FileNameLength / 2;
			wchar_t *wname = alloca((len + 1) * sizeof *wname);
			memcpy(wname, info->FileName, info->FileNameLength);
			wname[len] = 0;

			len = wcstombs(0, wname, 0);
			name = alloca(len + 1);
			wcstombs(name, wname, len + 1);

			witem = wdir->items;
			while(witem) {
				if(strstr(witem->res->name, name)) {
					res = witem->res;
					break;
				}
				witem = witem->next;
			}
			if(!res) {
				fprintf(stderr, "failed to find the modified watch item (%s)\n", name);
			} else {
				/* found the resource, schedule a reload */
				printf("file \"%s\" modified\n", res->name);
				resman_reload(rman, res);
			}
		}

		if(info->NextEntryOffset) {
			info = (FILE_NOTIFY_INFORMATION*)((char*)info + info->NextEntryOffset);
		} else {
			break;
		}
	}
}

void resman_check_watch(struct resman *rman)
{
	struct watch_dir *wdir;
	unsigned int idx;

	unsigned int num_handles = dynarr_size(rman->wait_handles);
	if(!num_handles) {
		return;
	}

	idx = WaitForMultipleObjectsEx(num_handles, rman->wait_handles, FALSE, 0, TRUE);
	if(idx == WAIT_FAILED) {
		unsigned int err = GetLastError();
		fprintf(stderr, "failed to check for file modification: %u\n", err);
		return;
	}
	if(idx >= WAIT_OBJECT_0 && idx < WAIT_OBJECT_0 + num_handles) {
		if(!(wdir = rb_find(rman->wdirbyev, rman->wait_handles[idx]))) {
			if(rman->wait_handles[idx] != rman->tpool_wait_handle) {
				fprintf(stderr, "got change handle, but failed to find corresponding watch_dir!\n");
			}
			return;
		}

		handle_event(rman, rman->wait_handles[idx], wdir);

		/* restart the watch call */
		ReadDirectoryChangesW(wdir->handle, wdir->buf, RES_BUF_SIZE, FALSE, FILE_NOTIFY_CHANGE_LAST_WRITE, 0, &wdir->over, 0);
	}
}


/* returns a new path buffer which is the equivalent absolute path to "fname"
* the user takes ownership and must free it when done.
*/
static char *abs_path(const char *fname)
{
	if(fname[0] != '/' && fname[1] != ':') {	/* not an absolute path */
		char *path;
		int cwdsz = GetCurrentDirectory(0, 0);
		int pathsz = strlen(fname) + cwdsz + 1;

		if(!(path = malloc(pathsz + 1))) {
			return 0;
		}
		GetCurrentDirectory(pathsz, path);

		/* now copy the rest of the path */
		strcat(path, "\\");
		strcat(path, fname);
		return path;
	}

	return strdup(fname);
}

static void clean_path(char *path)
{
	char *ptr, *dest;

	/* first pass, change '/' -> '\\' */
	ptr = path;
	while(*ptr) {
		if(*ptr == '/') *ptr = '\\';
		ptr++;
	}

	/* now go through the path and remove any \.\ or \..\ parts */
	ptr = dest = path;
	while(*ptr) {
		if(ptr > path && ptr[-1] == '\\') {
			/* we're just after a slash, so any .\ or ..\ here should be purged */
			if(ptr[0] == '.' && ptr[1] == '\\') {
				ptr += 2;
				continue;
			}
			if(ptr[0] == '.' && ptr[1] == '.' && ptr[2] == '\\') {
				/* search backwards for the previous slash */
				dest -= 2;
				while(dest > path && *dest != '\\') {
					dest--;
				}
				if(*dest == '\\') {
					/* found it, continue after this one */
					dest++;
				} /* .. otherwise, reached the beginning, go from there */

				ptr += 3;
				continue;
			}
		}
		if(dest != ptr) {
			*dest++ = *ptr++;
		} else {
			dest++;
			ptr++;
		}
	}
}


#else
int resman_filewatch_win32_silence_empty_file_warning;
#endif	/* WIN32 */
