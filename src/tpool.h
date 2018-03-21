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
#ifndef THREADPOOL_H_
#define THREADPOOL_H_

struct resman_thread_pool;

/* type of the function accepted as work or completion callback */
typedef void (*resman_tpool_callback)(void*);

#ifdef __cplusplus
extern "C" {
#endif

/* if num_threads == 0, auto-detect how many threads to spawn */
struct resman_thread_pool *resman_tpool_create(int num_threads);
void resman_tpool_destroy(struct resman_thread_pool *tpool);

/* optional reference counting interface for thread pool sharing */
int resman_tpool_addref(struct resman_thread_pool *tpool);
int resman_tpool_release(struct resman_thread_pool *tpool);	/* will resman_tpool_destroy on nref 0 */

/* if begin_batch is called before an enqueue, the worker threads will not be
 * signalled to start working until end_batch is called.
 */
void resman_tpool_begin_batch(struct resman_thread_pool *tpool);
void resman_tpool_end_batch(struct resman_thread_pool *tpool);

/* if enqueue is called without calling begin_batch first, it will immediately
 * wake up the worker threads to start working on the enqueued item
 */
int resman_tpool_enqueue(struct resman_thread_pool *tpool, void *data,
		resman_tpool_callback work_func, resman_tpool_callback done_func);
/* clear the work queue. does not cancel any currently running jobs */
void resman_tpool_clear(struct resman_thread_pool *tpool);

/* returns the number of queued work items */
int resman_tpool_queued_jobs(struct resman_thread_pool *tpool);
/* returns the number of active (working) threads */
int resman_tpool_active_jobs(struct resman_thread_pool *tpool);
/* returns the number of pending jobs, both in queue and active */
int resman_tpool_pending_jobs(struct resman_thread_pool *tpool);

/* wait for all pending jobs to be completed */
void resman_tpool_wait(struct resman_thread_pool *tpool);
/* wait until the pending jobs are down to the target specified
 * for example, to wait until a single job has been completed:
 *   resman_tpool_wait_pending(tpool, resman_tpool_pending_jobs(tpool) - 1);
 * this interface is slightly awkward to avoid race conditions. */
void resman_tpool_wait_pending(struct resman_thread_pool *tpool, int pending_target);
/* wait for all pending jobs to be completed for up to "timeout" milliseconds */
long resman_tpool_timedwait(struct resman_thread_pool *tpool, long timeout);

/* return a file descriptor which can be used to wait for pending job
 * completion events. A single char is written every time a job completes.
 * You should empty the pipe every time you receive such an event.
 *
 * This is a UNIX-specific call. On windows it does nothing.
 */
int resman_tpool_get_wait_fd(struct resman_thread_pool *tpool);

/* return an auto-resetting Event HANDLE which can be used to wait for
 * pending job completion events.
 *
 * This is a Win32-specific call. On UNIX it does nothing.
 */
void *resman_tpool_get_wait_handle(struct resman_thread_pool *tpool);

/* returns the number of processors on the system.
 * individual cores in multi-core processors are counted as processors.
 */
int resman_tpool_num_processors(void);

#ifdef __cplusplus
}
#endif

#endif	/* THREADPOOL_H_ */
