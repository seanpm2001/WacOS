
#include <stdlib.h>
#include <lwp/lwp.h>
#include <lwp/stackdep.h>

#define STACKSIZE	1000	/* stacksize for a thread */
#define NSTACKS		2	/* # stacks to be put in cache initially */

struct lock {
	int lock_locked;
	cv_t lock_condvar;
	mon_t lock_monitor;
};


/*
 * Initialization.
 */
static void PyThread__init_thread(void)
{
	lwp_setstkcache(STACKSIZE, NSTACKS);
}

/*
 * Thread support.
 */


int PyThread_start_new_thread(void (*func)(void *), void *arg)
{
	thread_t tid;
	int success;
	dprintf(("PyThread_start_new_thread called\n"));
	if (!initialized)
		PyThread_init_thread();
	success = lwp_create(&tid, func, MINPRIO, 0, lwp_newstk(), 1, arg);
	return success < 0 ? 0 : 1;
}

long PyThread_get_thread_ident(void)
{
	thread_t tid;
	if (!initialized)
		PyThread_init_thread();
	if (lwp_self(&tid) < 0)
		return -1;
	return tid.thread_id;
}

static void do_PyThread_exit_thread(int no_cleanup)
{
	dprintf(("PyThread_exit_thread called\n"));
	if (!initialized)
		if (no_cleanup)
			_exit(0);
		else
			exit(0);
	lwp_destroy(SELF);
}

void PyThread_exit_thread(void)
{
	do_PyThread_exit_thread(0);
}

void PyThread__exit_thread(void)
{
	do_PyThread_exit_thread(1);
}

#ifndef NO_EXIT_PROG
static void do_PyThread_exit_prog(int status, int no_cleanup)
{
	dprintf(("PyThread_exit_prog(%d) called\n", status));
	if (!initialized)
		if (no_cleanup)
			_exit(status);
		else
			exit(status);
	pod_exit(status);
}

void PyThread_exit_prog(int status)
{
	do_PyThread_exit_prog(status, 0);
}

void PyThread__exit_prog(int status)
{
	do_PyThread_exit_prog(status, 1);
}
#endif /* NO_EXIT_PROG */

/*
 * Lock support.
 */
PyThread_type_lock PyThread_allocate_lock(void)
{
	struct lock *lock;
	extern char *malloc(size_t);

	dprintf(("PyThread_allocate_lock called\n"));
	if (!initialized)
		PyThread_init_thread();

	lock = (struct lock *) malloc(sizeof(struct lock));
	lock->lock_locked = 0;
	(void) mon_create(&lock->lock_monitor);
	(void) cv_create(&lock->lock_condvar, lock->lock_monitor);
	dprintf(("PyThread_allocate_lock() -> %p\n", lock));
	return (PyThread_type_lock) lock;
}

void PyThread_free_lock(PyThread_type_lock lock)
{
	dprintf(("PyThread_free_lock(%p) called\n", lock));
	mon_destroy(((struct lock *) lock)->lock_monitor);
	free((char *) lock);
}

int PyThread_acquire_lock(PyThread_type_lock lock, int waitflag)
{
	int success;

	dprintf(("PyThread_acquire_lock(%p, %d) called\n", lock, waitflag));
	success = 0;

	(void) mon_enter(((struct lock *) lock)->lock_monitor);
	if (waitflag)
		while (((struct lock *) lock)->lock_locked)
			cv_wait(((struct lock *) lock)->lock_condvar);
	if (!((struct lock *) lock)->lock_locked) {
		success = 1;
		((struct lock *) lock)->lock_locked = 1;
	}
	cv_broadcast(((struct lock *) lock)->lock_condvar);
	mon_exit(((struct lock *) lock)->lock_monitor);
	dprintf(("PyThread_acquire_lock(%p, %d) -> %d\n", lock, waitflag, success));
	return success;
}

void PyThread_release_lock(PyThread_type_lock lock)
{
	dprintf(("PyThread_release_lock(%p) called\n", lock));
	(void) mon_enter(((struct lock *) lock)->lock_monitor);
	((struct lock *) lock)->lock_locked = 0;
	cv_broadcast(((struct lock *) lock)->lock_condvar);
	mon_exit(((struct lock *) lock)->lock_monitor);
}

/*
 * Semaphore support.
 */
PyThread_type_sema PyThread_allocate_sema(int value)
{
	PyThread_type_sema sema = 0;
	dprintf(("PyThread_allocate_sema called\n"));
	if (!initialized)
		PyThread_init_thread();

	dprintf(("PyThread_allocate_sema() -> %p\n",  sema));
	return (PyThread_type_sema) sema;
}

void PyThread_free_sema(PyThread_type_sema sema)
{
	dprintf(("PyThread_free_sema(%p) called\n",  sema));
}

int PyThread_down_sema(PyThread_type_sema sema, int waitflag)
{
	dprintf(("PyThread_down_sema(%p, %d) called\n",  sema, waitflag));
	dprintf(("PyThread_down_sema(%p) return\n",  sema));
	return -1;
}

void PyThread_up_sema(PyThread_type_sema sema)
{
	dprintf(("PyThread_up_sema(%p)\n",  sema));
}
