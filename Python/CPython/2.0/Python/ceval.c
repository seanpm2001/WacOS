
/* Execute compiled code */

/* XXX TO DO:
   XXX how to pass arguments to call_trace?
   XXX speed up searching for keywords by using a dictionary
   XXX document it!
   */

#include "Python.h"

#include "compile.h"
#include "frameobject.h"
#include "eval.h"
#include "opcode.h"

#ifdef macintosh
#include "macglue.h"
#endif

#include <ctype.h>

/* Turn this on if your compiler chokes on the big switch: */
/* #define CASE_TOO_BIG 1 */

#ifdef Py_DEBUG
/* For debugging the interpreter: */
#define LLTRACE  1	/* Low-level trace feature */
#define CHECKEXC 1	/* Double-check exception checking */
#endif


/* Forward declarations */

static PyObject *eval_code2(PyCodeObject *,
			    PyObject *, PyObject *,
			    PyObject **, int,
			    PyObject **, int,
			    PyObject **, int,
			    PyObject *);
#ifdef LLTRACE
static int prtrace(PyObject *, char *);
#endif
static void call_exc_trace(PyObject **, PyObject**, PyFrameObject *);
static int call_trace(PyObject **, PyObject **,
		      PyFrameObject *, char *, PyObject *);
static PyObject *call_builtin(PyObject *, PyObject *, PyObject *);
static PyObject *call_function(PyObject *, PyObject *, PyObject *);
static PyObject *loop_subscript(PyObject *, PyObject *);
static PyObject *apply_slice(PyObject *, PyObject *, PyObject *);
static int assign_slice(PyObject *, PyObject *,
			PyObject *, PyObject *);
static PyObject *cmp_outcome(int, PyObject *, PyObject *);
static PyObject *import_from(PyObject *, PyObject *);
static int import_all_from(PyObject *, PyObject *);
static PyObject *build_class(PyObject *, PyObject *, PyObject *);
static int exec_statement(PyFrameObject *,
			  PyObject *, PyObject *, PyObject *);
static void set_exc_info(PyThreadState *, PyObject *, PyObject *, PyObject *);
static void reset_exc_info(PyThreadState *);
static void format_exc_check_arg(PyObject *, char *, PyObject *);

#define NAME_ERROR_MSG \
	"There is no variable named '%s'"
#define UNBOUNDLOCAL_ERROR_MSG \
	"Local variable '%.200s' referenced before assignment"

/* Dynamic execution profile */
#ifdef DYNAMIC_EXECUTION_PROFILE
#ifdef DXPAIRS
static long dxpairs[257][256];
#define dxp dxpairs[256]
#else
static long dxp[256];
#endif
#endif


#ifdef WITH_THREAD

#ifndef DONT_HAVE_ERRNO_H
#include <errno.h>
#endif
#include "pythread.h"

extern int _PyThread_Started; /* Flag for Py_Exit */

static PyThread_type_lock interpreter_lock = 0;
static long main_thread = 0;

void
PyEval_InitThreads(void)
{
	if (interpreter_lock)
		return;
	_PyThread_Started = 1;
	interpreter_lock = PyThread_allocate_lock();
	PyThread_acquire_lock(interpreter_lock, 1);
	main_thread = PyThread_get_thread_ident();
}

void
PyEval_AcquireLock(void)
{
	PyThread_acquire_lock(interpreter_lock, 1);
}

void
PyEval_ReleaseLock(void)
{
	PyThread_release_lock(interpreter_lock);
}

void
PyEval_AcquireThread(PyThreadState *tstate)
{
	if (tstate == NULL)
		Py_FatalError("PyEval_AcquireThread: NULL new thread state");
	PyThread_acquire_lock(interpreter_lock, 1);
	if (PyThreadState_Swap(tstate) != NULL)
		Py_FatalError(
			"PyEval_AcquireThread: non-NULL old thread state");
}

void
PyEval_ReleaseThread(PyThreadState *tstate)
{
	if (tstate == NULL)
		Py_FatalError("PyEval_ReleaseThread: NULL thread state");
	if (PyThreadState_Swap(NULL) != tstate)
		Py_FatalError("PyEval_ReleaseThread: wrong thread state");
	PyThread_release_lock(interpreter_lock);
}

/* This function is called from PyOS_AfterFork to ensure that newly
   created child processes don't hold locks referring to threads which
   are not running in the child process.  (This could also be done using
   pthread_atfork mechanism, at least for the pthreads implementation.) */

void
PyEval_ReInitThreads(void)
{
	if (!interpreter_lock)
		return;
	/*XXX Can't use PyThread_free_lock here because it does too
	  much error-checking.  Doing this cleanly would require
	  adding a new function to each thread_*.h.  Instead, just
	  create a new lock and waste a little bit of memory */
	interpreter_lock = PyThread_allocate_lock();
	PyThread_acquire_lock(interpreter_lock, 1);
	main_thread = PyThread_get_thread_ident();
}
#endif

/* Functions save_thread and restore_thread are always defined so
   dynamically loaded modules needn't be compiled separately for use
   with and without threads: */

PyThreadState *
PyEval_SaveThread(void)
{
	PyThreadState *tstate = PyThreadState_Swap(NULL);
	if (tstate == NULL)
		Py_FatalError("PyEval_SaveThread: NULL tstate");
#ifdef WITH_THREAD
	if (interpreter_lock)
		PyThread_release_lock(interpreter_lock);
#endif
	return tstate;
}

void
PyEval_RestoreThread(PyThreadState *tstate)
{
	if (tstate == NULL)
		Py_FatalError("PyEval_RestoreThread: NULL tstate");
#ifdef WITH_THREAD
	if (interpreter_lock) {
		int err = errno;
		PyThread_acquire_lock(interpreter_lock, 1);
		errno = err;
	}
#endif
	PyThreadState_Swap(tstate);
}


/* Mechanism whereby asynchronously executing callbacks (e.g. UNIX
   signal handlers or Mac I/O completion routines) can schedule calls
   to a function to be called synchronously.
   The synchronous function is called with one void* argument.
   It should return 0 for success or -1 for failure -- failure should
   be accompanied by an exception.

   If registry succeeds, the registry function returns 0; if it fails
   (e.g. due to too many pending calls) it returns -1 (without setting
   an exception condition).

   Note that because registry may occur from within signal handlers,
   or other asynchronous events, calling malloc() is unsafe!

#ifdef WITH_THREAD
   Any thread can schedule pending calls, but only the main thread
   will execute them.
#endif

   XXX WARNING!  ASYNCHRONOUSLY EXECUTING CODE!
   There are two possible race conditions:
   (1) nested asynchronous registry calls;
   (2) registry calls made while pending calls are being processed.
   While (1) is very unlikely, (2) is a real possibility.
   The current code is safe against (2), but not against (1).
   The safety against (2) is derived from the fact that only one
   thread (the main thread) ever takes things out of the queue.

   XXX Darn!  With the advent of thread state, we should have an array
   of pending calls per thread in the thread state!  Later...
*/

#define NPENDINGCALLS 32
static struct {
	int (*func)(void *);
	void *arg;
} pendingcalls[NPENDINGCALLS];
static volatile int pendingfirst = 0;
static volatile int pendinglast = 0;
static volatile int things_to_do = 0;

int
Py_AddPendingCall(int (*func)(void *), void *arg)
{
	static int busy = 0;
	int i, j;
	/* XXX Begin critical section */
	/* XXX If you want this to be safe against nested
	   XXX asynchronous calls, you'll have to work harder! */
	if (busy)
		return -1;
	busy = 1;
	i = pendinglast;
	j = (i + 1) % NPENDINGCALLS;
	if (j == pendingfirst)
		return -1; /* Queue full */
	pendingcalls[i].func = func;
	pendingcalls[i].arg = arg;
	pendinglast = j;
	things_to_do = 1; /* Signal main loop */
	busy = 0;
	/* XXX End critical section */
	return 0;
}

int
Py_MakePendingCalls(void)
{
	static int busy = 0;
#ifdef WITH_THREAD
	if (main_thread && PyThread_get_thread_ident() != main_thread)
		return 0;
#endif
	if (busy)
		return 0;
	busy = 1;
	things_to_do = 0;
	for (;;) {
		int i;
		int (*func)(void *);
		void *arg;
		i = pendingfirst;
		if (i == pendinglast)
			break; /* Queue empty */
		func = pendingcalls[i].func;
		arg = pendingcalls[i].arg;
		pendingfirst = (i + 1) % NPENDINGCALLS;
		if (func(arg) < 0) {
			busy = 0;
			things_to_do = 1; /* We're not done yet */
			return -1;
		}
	}
	busy = 0;
	return 0;
}


/* The interpreter's recursion limit */

static int recursion_limit = 1000;

int
Py_GetRecursionLimit(void)
{
	return recursion_limit;
}

void
Py_SetRecursionLimit(int new_limit)
{
	recursion_limit = new_limit;
}

/* Status code for main loop (reason for stack unwind) */

enum why_code {
		WHY_NOT,	/* No error */
		WHY_EXCEPTION,	/* Exception occurred */
		WHY_RERAISE,	/* Exception re-raised by 'finally' */
		WHY_RETURN,	/* 'return' statement */
		WHY_BREAK	/* 'break' statement */
};

static enum why_code do_raise(PyObject *, PyObject *, PyObject *);
static int unpack_sequence(PyObject *, int, PyObject **);


PyObject *
PyEval_EvalCode(PyCodeObject *co, PyObject *globals, PyObject *locals)
{
	return eval_code2(co,
			  globals, locals,
			  (PyObject **)NULL, 0,
			  (PyObject **)NULL, 0,
			  (PyObject **)NULL, 0,
			  (PyObject *)NULL);
}


/* Interpreter main loop */

static PyObject *
eval_code2(PyCodeObject *co, PyObject *globals, PyObject *locals,
	   PyObject **args, int argcount, PyObject **kws, int kwcount,
	   PyObject **defs, int defcount, PyObject *owner)
{
#ifdef DXPAIRS
	int lastopcode = 0;
#endif
	register unsigned char *next_instr;
	register int opcode=0;	/* Current opcode */
	register int oparg=0;	/* Current opcode argument, if any */
	register PyObject **stack_pointer;
	register enum why_code why; /* Reason for block stack unwind */
	register int err;	/* Error status -- nonzero if error */
	register PyObject *x;	/* Result object -- NULL if error */
	register PyObject *v;	/* Temporary objects popped off stack */
	register PyObject *w;
	register PyObject *u;
	register PyObject *t;
	register PyObject *stream = NULL;    /* for PRINT opcodes */
	register PyFrameObject *f; /* Current frame */
	register PyObject **fastlocals;
	PyObject *retval = NULL;	/* Return value */
	PyThreadState *tstate = PyThreadState_GET();
	unsigned char *first_instr;
#ifdef LLTRACE
	int lltrace;
#endif
#if defined(Py_DEBUG) || defined(LLTRACE)
	/* Make it easier to find out where we are with a debugger */
	char *filename = PyString_AsString(co->co_filename);
#endif

/* Code access macros */

#define GETCONST(i)	Getconst(f, i)
#define GETNAME(i)	Getname(f, i)
#define GETNAMEV(i)	Getnamev(f, i)
#define INSTR_OFFSET()	(next_instr - first_instr)
#define NEXTOP()	(*next_instr++)
#define NEXTARG()	(next_instr += 2, (next_instr[-1]<<8) + next_instr[-2])
#define JUMPTO(x)	(next_instr = first_instr + (x))
#define JUMPBY(x)	(next_instr += (x))

/* Stack manipulation macros */

#define STACK_LEVEL()	(stack_pointer - f->f_valuestack)
#define EMPTY()		(STACK_LEVEL() == 0)
#define TOP()		(stack_pointer[-1])
#define BASIC_PUSH(v)	(*stack_pointer++ = (v))
#define BASIC_POP()	(*--stack_pointer)

#ifdef LLTRACE
#define PUSH(v)		(BASIC_PUSH(v), lltrace && prtrace(TOP(), "push"))
#define POP()		(lltrace && prtrace(TOP(), "pop"), BASIC_POP())
#else
#define PUSH(v)		BASIC_PUSH(v)
#define POP()		BASIC_POP()
#endif

/* Local variable macros */

#define GETLOCAL(i)	(fastlocals[i])
#define SETLOCAL(i, value)	do { Py_XDECREF(GETLOCAL(i)); \
				     GETLOCAL(i) = value; } while (0)

/* Start of code */

#ifdef USE_STACKCHECK
	if (tstate->recursion_depth%10 == 0 && PyOS_CheckStack()) {
		PyErr_SetString(PyExc_MemoryError, "Stack overflow");
		return NULL;
	}
#endif

	if (globals == NULL) {
		PyErr_SetString(PyExc_SystemError, "eval_code2: NULL globals");
		return NULL;
	}

#ifdef LLTRACE
	lltrace = PyDict_GetItemString(globals, "__lltrace__") != NULL;
#endif

	f = PyFrame_New(
			tstate,			/*back*/
			co,			/*code*/
			globals,		/*globals*/
			locals);		/*locals*/
	if (f == NULL)
		return NULL;

	tstate->frame = f;
	fastlocals = f->f_localsplus;

	if (co->co_argcount > 0 ||
	    co->co_flags & (CO_VARARGS | CO_VARKEYWORDS)) {
		int i;
		int n = argcount;
		PyObject *kwdict = NULL;
		if (co->co_flags & CO_VARKEYWORDS) {
			kwdict = PyDict_New();
			if (kwdict == NULL)
				goto fail;
			i = co->co_argcount;
			if (co->co_flags & CO_VARARGS)
				i++;
			SETLOCAL(i, kwdict);
		}
		if (argcount > co->co_argcount) {
			if (!(co->co_flags & CO_VARARGS)) {
				PyErr_Format(PyExc_TypeError,
				"too many arguments; expected %d, got %d",
					     co->co_argcount, argcount);
				goto fail;
			}
			n = co->co_argcount;
		}
		for (i = 0; i < n; i++) {
			x = args[i];
			Py_INCREF(x);
			SETLOCAL(i, x);
		}
		if (co->co_flags & CO_VARARGS) {
			u = PyTuple_New(argcount - n);
			if (u == NULL)
				goto fail;
			SETLOCAL(co->co_argcount, u);
			for (i = n; i < argcount; i++) {
				x = args[i];
				Py_INCREF(x);
				PyTuple_SET_ITEM(u, i-n, x);
			}
		}
		for (i = 0; i < kwcount; i++) {
			PyObject *keyword = kws[2*i];
			PyObject *value = kws[2*i + 1];
			int j;
			if (keyword == NULL || !PyString_Check(keyword)) {
				PyErr_SetString(PyExc_TypeError,
						"keywords must be strings");
				goto fail;
			}
			/* XXX slow -- speed up using dictionary? */
			for (j = 0; j < co->co_argcount; j++) {
				PyObject *nm = PyTuple_GET_ITEM(
					co->co_varnames, j);
				if (PyObject_Compare(keyword, nm) == 0)
					break;
			}
			/* Check errors from Compare */
			if (PyErr_Occurred())
				goto fail;
			if (j >= co->co_argcount) {
				if (kwdict == NULL) {
					PyErr_Format(PyExc_TypeError,
					 "unexpected keyword argument: %.400s",
					    PyString_AsString(keyword));
					goto fail;
				}
				PyDict_SetItem(kwdict, keyword, value);
			}
			else {
				if (GETLOCAL(j) != NULL) {
					PyErr_Format(PyExc_TypeError, 
				     "keyword parameter redefined: %.400s",
					     PyString_AsString(keyword));
					goto fail;
				}
				Py_INCREF(value);
				SETLOCAL(j, value);
			}
		}
		if (argcount < co->co_argcount) {
			int m = co->co_argcount - defcount;
			for (i = argcount; i < m; i++) {
				if (GETLOCAL(i) == NULL) {
					PyErr_Format(PyExc_TypeError,
				"not enough arguments; expected %d, got %d",
						     m, i);
					goto fail;
				}
			}
			if (n > m)
				i = n - m;
			else
				i = 0;
			for (; i < defcount; i++) {
				if (GETLOCAL(m+i) == NULL) {
					PyObject *def = defs[i];
					Py_INCREF(def);
					SETLOCAL(m+i, def);
				}
			}
		}
	}
	else {
		if (argcount > 0 || kwcount > 0) {
			PyErr_SetString(PyExc_TypeError,
					"no arguments expected");
			goto fail;
		}
	}

	if (tstate->sys_tracefunc != NULL) {
		/* tstate->sys_tracefunc, if defined, is a function that
		   will be called  on *every* entry to a code block.
		   Its return value, if not None, is a function that
		   will be called at the start of each executed line
		   of code.  (Actually, the function must return
		   itself in order to continue tracing.)
		   The trace functions are called with three arguments:
		   a pointer to the current frame, a string indicating
		   why the function is called, and an argument which
		   depends on the situation.  The global trace function
		   (sys.trace) is also called whenever an exception
		   is detected. */
		if (call_trace(&tstate->sys_tracefunc,
			       &f->f_trace, f, "call",
			       Py_None/*XXX how to compute arguments now?*/)) {
			/* Trace function raised an error */
			goto fail;
		}
	}

	if (tstate->sys_profilefunc != NULL) {
		/* Similar for sys_profilefunc, except it needn't return
		   itself and isn't called for "line" events */
		if (call_trace(&tstate->sys_profilefunc,
			       (PyObject**)0, f, "call",
			       Py_None/*XXX*/)) {
			goto fail;
		}
	}

	if (++tstate->recursion_depth > recursion_limit) {
		--tstate->recursion_depth;
		PyErr_SetString(PyExc_RuntimeError,
				"Maximum recursion depth exceeded");
		tstate->frame = f->f_back;
		Py_DECREF(f);
		return NULL;
	}

	_PyCode_GETCODEPTR(co, &first_instr);
	next_instr = first_instr;
	stack_pointer = f->f_valuestack;
	
	why = WHY_NOT;
	err = 0;
	x = Py_None;	/* Not a reference, just anything non-NULL */
	w = NULL;
	
	for (;;) {
		/* Do periodic things.  Doing this every time through
		   the loop would add too much overhead, so we do it
		   only every Nth instruction.  We also do it if
		   ``things_to_do'' is set, i.e. when an asynchronous
		   event needs attention (e.g. a signal handler or
		   async I/O handler); see Py_AddPendingCall() and
		   Py_MakePendingCalls() above. */
		
		if (things_to_do || --tstate->ticker < 0) {
			tstate->ticker = tstate->interp->checkinterval;
			if (things_to_do) {
				if (Py_MakePendingCalls() < 0) {
					why = WHY_EXCEPTION;
					goto on_error;
				}
			}
#if !defined(HAVE_SIGNAL_H) || defined(macintosh)
			/* If we have true signals, the signal handler
			   will call Py_AddPendingCall() so we don't
			   have to call sigcheck().  On the Mac and
			   DOS, alas, we have to call it. */
			if (PyErr_CheckSignals()) {
				why = WHY_EXCEPTION;
				goto on_error;
			}
#endif

#ifdef WITH_THREAD
			if (interpreter_lock) {
				/* Give another thread a chance */

				if (PyThreadState_Swap(NULL) != tstate)
					Py_FatalError("ceval: tstate mix-up");
				PyThread_release_lock(interpreter_lock);

				/* Other threads may run now */

				PyThread_acquire_lock(interpreter_lock, 1);
				if (PyThreadState_Swap(tstate) != NULL)
					Py_FatalError("ceval: orphan tstate");
			}
#endif
		}

		/* Extract opcode and argument */

#if defined(Py_DEBUG) || defined(LLTRACE)
		f->f_lasti = INSTR_OFFSET();
#endif
		
		opcode = NEXTOP();
		if (HAS_ARG(opcode))
			oparg = NEXTARG();
	  dispatch_opcode:
#ifdef DYNAMIC_EXECUTION_PROFILE
#ifdef DXPAIRS
		dxpairs[lastopcode][opcode]++;
		lastopcode = opcode;
#endif
		dxp[opcode]++;
#endif

#ifdef LLTRACE
		/* Instruction tracing */
		
		if (lltrace) {
			if (HAS_ARG(opcode)) {
				printf("%d: %d, %d\n",
					(int) (INSTR_OFFSET() - 3),
					opcode, oparg);
			}
			else {
				printf("%d: %d\n",
					(int) (INSTR_OFFSET() - 1), opcode);
			}
		}
#endif
		/* Main switch on opcode */
		
		switch (opcode) {
		
		/* BEWARE!
		   It is essential that any operation that fails sets either
		   x to NULL, err to nonzero, or why to anything but WHY_NOT,
		   and that no operation that succeeds does this! */
		
		/* case STOP_CODE: this is an error! */
		
		case POP_TOP:
			v = POP();
			Py_DECREF(v);
			continue;
		
		case ROT_TWO:
			v = POP();
			w = POP();
			PUSH(v);
			PUSH(w);
			continue;
		
		case ROT_THREE:
			v = POP();
			w = POP();
			x = POP();
			PUSH(v);
			PUSH(x);
			PUSH(w);
			continue;
		
		case ROT_FOUR:
			u = POP();
			v = POP();
			w = POP();
			x = POP();
			PUSH(u);
			PUSH(x);
			PUSH(w);
			PUSH(v);
			continue;
		
		case DUP_TOP:
			v = TOP();
			Py_INCREF(v);
			PUSH(v);
			continue;
		
		case DUP_TOPX:
			switch (oparg) {
			case 1:
				x = TOP();
				Py_INCREF(x);
				PUSH(x);
				continue;
			case 2:
				x = POP();
				Py_INCREF(x);
				w = TOP();
				Py_INCREF(w);
				PUSH(x);
				PUSH(w);
				PUSH(x);
				continue;
			case 3:
				x = POP();
				Py_INCREF(x);
				w = POP();
				Py_INCREF(w);
				v = TOP();
				Py_INCREF(v);
				PUSH(w);
				PUSH(x);
				PUSH(v);
				PUSH(w);
				PUSH(x);
				continue;
			case 4:
				x = POP();
				Py_INCREF(x);
				w = POP();
				Py_INCREF(w);
				v = POP();
				Py_INCREF(v);
				u = TOP();
				Py_INCREF(u);
				PUSH(v);
				PUSH(w);
				PUSH(x);
				PUSH(u);
				PUSH(v);
				PUSH(w);
				PUSH(x);
				continue;
			case 5:
				x = POP();
				Py_INCREF(x);
				w = POP();
				Py_INCREF(w);
				v = POP();
				Py_INCREF(v);
				u = POP();
				Py_INCREF(u);
				t = TOP();
				Py_INCREF(t);
				PUSH(u);
				PUSH(v);
				PUSH(w);
				PUSH(x);
				PUSH(t);
				PUSH(u);
				PUSH(v);
				PUSH(w);
				PUSH(x);
				continue;
			default:
				Py_FatalError("invalid argument to DUP_TOPX"
					      " (bytecode corruption?)");
			}
			break;

		case UNARY_POSITIVE:
			v = POP();
			x = PyNumber_Positive(v);
			Py_DECREF(v);
			PUSH(x);
			if (x != NULL) continue;
			break;
		
		case UNARY_NEGATIVE:
			v = POP();
			x = PyNumber_Negative(v);
			Py_DECREF(v);
			PUSH(x);
			if (x != NULL) continue;
			break;
		
		case UNARY_NOT:
			v = POP();
			err = PyObject_IsTrue(v);
			Py_DECREF(v);
			if (err == 0) {
				Py_INCREF(Py_True);
				PUSH(Py_True);
				continue;
			}
			else if (err > 0) {
				Py_INCREF(Py_False);
				PUSH(Py_False);
				err = 0;
				continue;
			}
			break;
		
		case UNARY_CONVERT:
			v = POP();
			x = PyObject_Repr(v);
			Py_DECREF(v);
			PUSH(x);
			if (x != NULL) continue;
			break;
			
		case UNARY_INVERT:
			v = POP();
			x = PyNumber_Invert(v);
			Py_DECREF(v);
			PUSH(x);
			if (x != NULL) continue;
			break;
		
		case BINARY_POWER:
			w = POP();
			v = POP();
			x = PyNumber_Power(v, w, Py_None);
			Py_DECREF(v);
			Py_DECREF(w);
			PUSH(x);
			if (x != NULL) continue;
			break;
		
		case BINARY_MULTIPLY:
			w = POP();
			v = POP();
			x = PyNumber_Multiply(v, w);
			Py_DECREF(v);
			Py_DECREF(w);
			PUSH(x);
			if (x != NULL) continue;
			break;
		
		case BINARY_DIVIDE:
			w = POP();
			v = POP();
			x = PyNumber_Divide(v, w);
			Py_DECREF(v);
			Py_DECREF(w);
			PUSH(x);
			if (x != NULL) continue;
			break;
		
		case BINARY_MODULO:
			w = POP();
			v = POP();
			x = PyNumber_Remainder(v, w);
			Py_DECREF(v);
			Py_DECREF(w);
			PUSH(x);
			if (x != NULL) continue;
			break;
		
		case BINARY_ADD:
			w = POP();
			v = POP();
			if (PyInt_Check(v) && PyInt_Check(w)) {
				/* INLINE: int + int */
				register long a, b, i;
				a = PyInt_AS_LONG(v);
				b = PyInt_AS_LONG(w);
				i = a + b;
				if ((i^a) < 0 && (i^b) < 0) {
					PyErr_SetString(PyExc_OverflowError,
							"integer addition");
					x = NULL;
				}
				else
					x = PyInt_FromLong(i);
			}
			else
				x = PyNumber_Add(v, w);
			Py_DECREF(v);
			Py_DECREF(w);
			PUSH(x);
			if (x != NULL) continue;
			break;
		
		case BINARY_SUBTRACT:
			w = POP();
			v = POP();
			if (PyInt_Check(v) && PyInt_Check(w)) {
				/* INLINE: int - int */
				register long a, b, i;
				a = PyInt_AS_LONG(v);
				b = PyInt_AS_LONG(w);
				i = a - b;
				if ((i^a) < 0 && (i^~b) < 0) {
					PyErr_SetString(PyExc_OverflowError,
							"integer subtraction");
					x = NULL;
				}
				else
					x = PyInt_FromLong(i);
			}
			else
				x = PyNumber_Subtract(v, w);
			Py_DECREF(v);
			Py_DECREF(w);
			PUSH(x);
			if (x != NULL) continue;
			break;
		
		case BINARY_SUBSCR:
			w = POP();
			v = POP();
			if (PyList_Check(v) && PyInt_Check(w)) {
				/* INLINE: list[int] */
				long i = PyInt_AsLong(w);
				if (i < 0)
					i += PyList_GET_SIZE(v);
				if (i < 0 ||
				    i >= PyList_GET_SIZE(v)) {
					PyErr_SetString(PyExc_IndexError,
						"list index out of range");
					x = NULL;
				}
				else {
					x = PyList_GET_ITEM(v, i);
					Py_INCREF(x);
				}
			}
			else
				x = PyObject_GetItem(v, w);
			Py_DECREF(v);
			Py_DECREF(w);
			PUSH(x);
			if (x != NULL) continue;
			break;
		
		case BINARY_LSHIFT:
			w = POP();
			v = POP();
			x = PyNumber_Lshift(v, w);
			Py_DECREF(v);
			Py_DECREF(w);
			PUSH(x);
			if (x != NULL) continue;
			break;
		
		case BINARY_RSHIFT:
			w = POP();
			v = POP();
			x = PyNumber_Rshift(v, w);
			Py_DECREF(v);
			Py_DECREF(w);
			PUSH(x);
			if (x != NULL) continue;
			break;
		
		case BINARY_AND:
			w = POP();
			v = POP();
			x = PyNumber_And(v, w);
			Py_DECREF(v);
			Py_DECREF(w);
			PUSH(x);
			if (x != NULL) continue;
			break;
		
		case BINARY_XOR:
			w = POP();
			v = POP();
			x = PyNumber_Xor(v, w);
			Py_DECREF(v);
			Py_DECREF(w);
			PUSH(x);
			if (x != NULL) continue;
			break;
		
		case BINARY_OR:
			w = POP();
			v = POP();
			x = PyNumber_Or(v, w);
			Py_DECREF(v);
			Py_DECREF(w);
			PUSH(x);
			if (x != NULL) continue;
			break;

		case INPLACE_POWER:
			w = POP();
			v = POP();
			x = PyNumber_InPlacePower(v, w, Py_None);
			Py_DECREF(v);
			Py_DECREF(w);
			PUSH(x);
			if (x != NULL) continue;
			break;
		
		case INPLACE_MULTIPLY:
			w = POP();
			v = POP();
			x = PyNumber_InPlaceMultiply(v, w);
			Py_DECREF(v);
			Py_DECREF(w);
			PUSH(x);
			if (x != NULL) continue;
			break;
		
		case INPLACE_DIVIDE:
			w = POP();
			v = POP();
			x = PyNumber_InPlaceDivide(v, w);
			Py_DECREF(v);
			Py_DECREF(w);
			PUSH(x);
			if (x != NULL) continue;
			break;
		
		case INPLACE_MODULO:
			w = POP();
			v = POP();
			x = PyNumber_InPlaceRemainder(v, w);
			Py_DECREF(v);
			Py_DECREF(w);
			PUSH(x);
			if (x != NULL) continue;
			break;
		
		case INPLACE_ADD:
			w = POP();
			v = POP();
			if (PyInt_Check(v) && PyInt_Check(w)) {
				/* INLINE: int + int */
				register long a, b, i;
				a = PyInt_AS_LONG(v);
				b = PyInt_AS_LONG(w);
				i = a + b;
				if ((i^a) < 0 && (i^b) < 0) {
					PyErr_SetString(PyExc_OverflowError,
							"integer addition");
					x = NULL;
				}
				else
					x = PyInt_FromLong(i);
			}
			else
				x = PyNumber_InPlaceAdd(v, w);
			Py_DECREF(v);
			Py_DECREF(w);
			PUSH(x);
			if (x != NULL) continue;
			break;
		
		case INPLACE_SUBTRACT:
			w = POP();
			v = POP();
			if (PyInt_Check(v) && PyInt_Check(w)) {
				/* INLINE: int - int */
				register long a, b, i;
				a = PyInt_AS_LONG(v);
				b = PyInt_AS_LONG(w);
				i = a - b;
				if ((i^a) < 0 && (i^~b) < 0) {
					PyErr_SetString(PyExc_OverflowError,
							"integer subtraction");
					x = NULL;
				}
				else
					x = PyInt_FromLong(i);
			}
			else
				x = PyNumber_InPlaceSubtract(v, w);
			Py_DECREF(v);
			Py_DECREF(w);
			PUSH(x);
			if (x != NULL) continue;
			break;
		
		case INPLACE_LSHIFT:
			w = POP();
			v = POP();
			x = PyNumber_InPlaceLshift(v, w);
			Py_DECREF(v);
			Py_DECREF(w);
			PUSH(x);
			if (x != NULL) continue;
			break;
		
		case INPLACE_RSHIFT:
			w = POP();
			v = POP();
			x = PyNumber_InPlaceRshift(v, w);
			Py_DECREF(v);
			Py_DECREF(w);
			PUSH(x);
			if (x != NULL) continue;
			break;
		
		case INPLACE_AND:
			w = POP();
			v = POP();
			x = PyNumber_InPlaceAnd(v, w);
			Py_DECREF(v);
			Py_DECREF(w);
			PUSH(x);
			if (x != NULL) continue;
			break;
		
		case INPLACE_XOR:
			w = POP();
			v = POP();
			x = PyNumber_InPlaceXor(v, w);
			Py_DECREF(v);
			Py_DECREF(w);
			PUSH(x);
			if (x != NULL) continue;
			break;
		
		case INPLACE_OR:
			w = POP();
			v = POP();
			x = PyNumber_InPlaceOr(v, w);
			Py_DECREF(v);
			Py_DECREF(w);
			PUSH(x);
			if (x != NULL) continue;
			break;

		case SLICE+0:
		case SLICE+1:
		case SLICE+2:
		case SLICE+3:
			if ((opcode-SLICE) & 2)
				w = POP();
			else
				w = NULL;
			if ((opcode-SLICE) & 1)
				v = POP();
			else
				v = NULL;
			u = POP();
			x = apply_slice(u, v, w);
			Py_DECREF(u);
			Py_XDECREF(v);
			Py_XDECREF(w);
			PUSH(x);
			if (x != NULL) continue;
			break;
		
		case STORE_SLICE+0:
		case STORE_SLICE+1:
		case STORE_SLICE+2:
		case STORE_SLICE+3:
			if ((opcode-STORE_SLICE) & 2)
				w = POP();
			else
				w = NULL;
			if ((opcode-STORE_SLICE) & 1)
				v = POP();
			else
				v = NULL;
			u = POP();
			t = POP();
			err = assign_slice(u, v, w, t); /* u[v:w] = t */
			Py_DECREF(t);
			Py_DECREF(u);
			Py_XDECREF(v);
			Py_XDECREF(w);
			if (err == 0) continue;
			break;
		
		case DELETE_SLICE+0:
		case DELETE_SLICE+1:
		case DELETE_SLICE+2:
		case DELETE_SLICE+3:
			if ((opcode-DELETE_SLICE) & 2)
				w = POP();
			else
				w = NULL;
			if ((opcode-DELETE_SLICE) & 1)
				v = POP();
			else
				v = NULL;
			u = POP();
			err = assign_slice(u, v, w, (PyObject *)NULL);
							/* del u[v:w] */
			Py_DECREF(u);
			Py_XDECREF(v);
			Py_XDECREF(w);
			if (err == 0) continue;
			break;
		
		case STORE_SUBSCR:
			w = POP();
			v = POP();
			u = POP();
			/* v[w] = u */
			err = PyObject_SetItem(v, w, u);
			Py_DECREF(u);
			Py_DECREF(v);
			Py_DECREF(w);
			if (err == 0) continue;
			break;
		
		case DELETE_SUBSCR:
			w = POP();
			v = POP();
			/* del v[w] */
			err = PyObject_DelItem(v, w);
			Py_DECREF(v);
			Py_DECREF(w);
			if (err == 0) continue;
			break;

		case PRINT_EXPR:
			v = POP();
			/* Print value except if None */
			/* After printing, also assign to '_' */
			/* Before, set '_' to None to avoid recursion */
			if (v != Py_None &&
			    (err = PyDict_SetItemString(
				    f->f_builtins, "_", Py_None)) == 0) {
				err = Py_FlushLine();
				if (err == 0) {
					x = PySys_GetObject("stdout");
					if (x == NULL) {
						PyErr_SetString(
							PyExc_RuntimeError,
							"lost sys.stdout");
						err = -1;
					}
				}
				if (err == 0)
					err = PyFile_WriteObject(v, x, 0);
				if (err == 0) {
					PyFile_SoftSpace(x, 1);
					err = Py_FlushLine();
				}
				if (err == 0) {
					err = PyDict_SetItemString(
						f->f_builtins, "_", v);
				}
			}
			Py_DECREF(v);
			break;
		
		case PRINT_ITEM_TO:
			w = stream = POP();
			/* fall through to PRINT_ITEM */

		case PRINT_ITEM:
			v = POP();
			if (stream == NULL || stream == Py_None) {
				w = PySys_GetObject("stdout");
				if (w == NULL) {
					PyErr_SetString(PyExc_RuntimeError,
							"lost sys.stdout");
					err = -1;
				}
			}
			if (w != NULL && PyFile_SoftSpace(w, 1))
				err = PyFile_WriteString(" ", w);
			if (err == 0)
				err = PyFile_WriteObject(v, w, Py_PRINT_RAW);
			if (err == 0 && PyString_Check(v)) {
				/* XXX move into writeobject() ? */
				char *s = PyString_AsString(v);
				int len = PyString_Size(v);
				if (len > 0 &&
				    isspace(Py_CHARMASK(s[len-1])) &&
				    s[len-1] != ' ')
					PyFile_SoftSpace(w, 0);
			}
			Py_DECREF(v);
			Py_XDECREF(stream);
			stream = NULL;
			if (err == 0)
				continue;
			break;
		
		case PRINT_NEWLINE_TO:
			w = stream = POP();
			/* fall through to PRINT_NEWLINE */

		case PRINT_NEWLINE:
			if (stream == NULL || stream == Py_None) {
				w = PySys_GetObject("stdout");
				if (w == NULL)
					PyErr_SetString(PyExc_RuntimeError,
							"lost sys.stdout");
			}
			if (w != NULL) {
				err = PyFile_WriteString("\n", w);
				if (err == 0)
					PyFile_SoftSpace(w, 0);
			}
			Py_XDECREF(stream);
			stream = NULL;
			break;
		

#ifdef CASE_TOO_BIG
		default: switch (opcode) {
#endif
		case BREAK_LOOP:
			why = WHY_BREAK;
			break;

		case RAISE_VARARGS:
			u = v = w = NULL;
			switch (oparg) {
			case 3:
				u = POP(); /* traceback */
				/* Fallthrough */
			case 2:
				v = POP(); /* value */
				/* Fallthrough */
			case 1:
				w = POP(); /* exc */
			case 0: /* Fallthrough */
				why = do_raise(w, v, u);
				break;
			default:
				PyErr_SetString(PyExc_SystemError,
					   "bad RAISE_VARARGS oparg");
				why = WHY_EXCEPTION;
				break;
			}
			break;
		
		case LOAD_LOCALS:
			if ((x = f->f_locals) == NULL) {
				PyErr_SetString(PyExc_SystemError,
						"no locals");
				break;
			}
			Py_INCREF(x);
			PUSH(x);
			break;
		
		case RETURN_VALUE:
			retval = POP();
			why = WHY_RETURN;
			break;

		case EXEC_STMT:
			w = POP();
			v = POP();
			u = POP();
			err = exec_statement(f, u, v, w);
			Py_DECREF(u);
			Py_DECREF(v);
			Py_DECREF(w);
			break;
		
		case POP_BLOCK:
			{
				PyTryBlock *b = PyFrame_BlockPop(f);
				while (STACK_LEVEL() > b->b_level) {
					v = POP();
					Py_DECREF(v);
				}
			}
			break;
		
		case END_FINALLY:
			v = POP();
			if (PyInt_Check(v)) {
				why = (enum why_code) PyInt_AsLong(v);
				if (why == WHY_RETURN)
					retval = POP();
			}
			else if (PyString_Check(v) || PyClass_Check(v)) {
				w = POP();
				u = POP();
				PyErr_Restore(v, w, u);
				why = WHY_RERAISE;
				break;
			}
			else if (v != Py_None) {
				PyErr_SetString(PyExc_SystemError,
					"'finally' pops bad exception");
				why = WHY_EXCEPTION;
			}
			Py_DECREF(v);
			break;
		
		case BUILD_CLASS:
			u = POP();
			v = POP();
			w = POP();
			x = build_class(u, v, w);
			PUSH(x);
			Py_DECREF(u);
			Py_DECREF(v);
			Py_DECREF(w);
			break;
		
		case STORE_NAME:
			w = GETNAMEV(oparg);
			v = POP();
			if ((x = f->f_locals) == NULL) {
				PyErr_SetString(PyExc_SystemError,
						"no locals");
				break;
			}
			err = PyDict_SetItem(x, w, v);
			Py_DECREF(v);
			break;
		
		case DELETE_NAME:
			w = GETNAMEV(oparg);
			if ((x = f->f_locals) == NULL) {
				PyErr_SetString(PyExc_SystemError,
						"no locals");
				break;
			}
			if ((err = PyDict_DelItem(x, w)) != 0)
				format_exc_check_arg(PyExc_NameError, 
							NAME_ERROR_MSG ,w);
			break;

		case UNPACK_SEQUENCE:
			v = POP();
			if (PyTuple_Check(v)) {
				if (PyTuple_Size(v) != oparg) {
					PyErr_SetString(PyExc_ValueError,
						 "unpack tuple of wrong size");
					why = WHY_EXCEPTION;
				}
				else {
					for (; --oparg >= 0; ) {
						w = PyTuple_GET_ITEM(v, oparg);
						Py_INCREF(w);
						PUSH(w);
					}
				}
			}
			else if (PyList_Check(v)) {
				if (PyList_Size(v) != oparg) {
					PyErr_SetString(PyExc_ValueError,
						  "unpack list of wrong size");
					why = WHY_EXCEPTION;
				}
				else {
					for (; --oparg >= 0; ) {
						w = PyList_GET_ITEM(v, oparg);
						Py_INCREF(w);
						PUSH(w);
					}
				}
			}
			else if (PySequence_Check(v)) {
				if (unpack_sequence(v, oparg,
						    stack_pointer + oparg))
					stack_pointer += oparg;
				else
					why = WHY_EXCEPTION;
			}
			else {
				PyErr_SetString(PyExc_TypeError,
						"unpack non-sequence");
				why = WHY_EXCEPTION;
			}
			Py_DECREF(v);
			break;
		
		case STORE_ATTR:
			w = GETNAMEV(oparg);
			v = POP();
			u = POP();
			err = PyObject_SetAttr(v, w, u); /* v.w = u */
			Py_DECREF(v);
			Py_DECREF(u);
			break;
		
		case DELETE_ATTR:
			w = GETNAMEV(oparg);
			v = POP();
			err = PyObject_SetAttr(v, w, (PyObject *)NULL);
							/* del v.w */
			Py_DECREF(v);
			break;
		
		case STORE_GLOBAL:
			w = GETNAMEV(oparg);
			v = POP();
			err = PyDict_SetItem(f->f_globals, w, v);
			Py_DECREF(v);
			break;
		
		case DELETE_GLOBAL:
			w = GETNAMEV(oparg);
			if ((err = PyDict_DelItem(f->f_globals, w)) != 0)
				format_exc_check_arg(
				    PyExc_NameError, NAME_ERROR_MSG ,w);
			break;
		
		case LOAD_CONST:
			x = GETCONST(oparg);
			Py_INCREF(x);
			PUSH(x);
			break;
		
		case LOAD_NAME:
			w = GETNAMEV(oparg);
			if ((x = f->f_locals) == NULL) {
				PyErr_SetString(PyExc_SystemError,
						"no locals");
				break;
			}
			x = PyDict_GetItem(x, w);
			if (x == NULL) {
				x = PyDict_GetItem(f->f_globals, w);
				if (x == NULL) {
					x = PyDict_GetItem(f->f_builtins, w);
					if (x == NULL) {
						format_exc_check_arg(
							    PyExc_NameError, 
							    NAME_ERROR_MSG ,w);
						break;
					}
				}
			}
			Py_INCREF(x);
			PUSH(x);
			break;
		
		case LOAD_GLOBAL:
			w = GETNAMEV(oparg);
			x = PyDict_GetItem(f->f_globals, w);
			if (x == NULL) {
				x = PyDict_GetItem(f->f_builtins, w);
				if (x == NULL) {
					format_exc_check_arg(
						    PyExc_NameError, 
						    NAME_ERROR_MSG ,w);
					break;
				}
			}
			Py_INCREF(x);
			PUSH(x);
			break;

		case LOAD_FAST:
			x = GETLOCAL(oparg);
			if (x == NULL) {
				format_exc_check_arg(
					PyExc_UnboundLocalError,
					UNBOUNDLOCAL_ERROR_MSG,
					PyTuple_GetItem(co->co_varnames, oparg)
					);
				break;
			}
			Py_INCREF(x);
			PUSH(x);
			if (x != NULL) continue;
			break;

		case STORE_FAST:
			v = POP();
			SETLOCAL(oparg, v);
			continue;

		case DELETE_FAST:
			x = GETLOCAL(oparg);
			if (x == NULL) {
				format_exc_check_arg(
					PyExc_UnboundLocalError,
					UNBOUNDLOCAL_ERROR_MSG,
					PyTuple_GetItem(co->co_varnames, oparg)
					);
				break;
			}
			SETLOCAL(oparg, NULL);
			continue;
		
		case BUILD_TUPLE:
			x = PyTuple_New(oparg);
			if (x != NULL) {
				for (; --oparg >= 0;) {
					w = POP();
					PyTuple_SET_ITEM(x, oparg, w);
				}
				PUSH(x);
				continue;
			}
			break;
		
		case BUILD_LIST:
			x =  PyList_New(oparg);
			if (x != NULL) {
				for (; --oparg >= 0;) {
					w = POP();
					PyList_SET_ITEM(x, oparg, w);
				}
				PUSH(x);
				continue;
			}
			break;
		
		case BUILD_MAP:
			x = PyDict_New();
			PUSH(x);
			if (x != NULL) continue;
			break;
		
		case LOAD_ATTR:
			w = GETNAMEV(oparg);
			v = POP();
			x = PyObject_GetAttr(v, w);
			Py_DECREF(v);
			PUSH(x);
			if (x != NULL) continue;
			break;
		
		case COMPARE_OP:
			w = POP();
			v = POP();
			if (PyInt_Check(v) && PyInt_Check(w)) {
				/* INLINE: cmp(int, int) */
				register long a, b;
				register int res;
				a = PyInt_AS_LONG(v);
				b = PyInt_AS_LONG(w);
				switch (oparg) {
				case LT: res = a <  b; break;
				case LE: res = a <= b; break;
				case EQ: res = a == b; break;
				case NE: res = a != b; break;
				case GT: res = a >  b; break;
				case GE: res = a >= b; break;
				case IS: res = v == w; break;
				case IS_NOT: res = v != w; break;
				default: goto slow_compare;
				}
				x = res ? Py_True : Py_False;
				Py_INCREF(x);
			}
			else {
			  slow_compare:
				x = cmp_outcome(oparg, v, w);
			}
			Py_DECREF(v);
			Py_DECREF(w);
			PUSH(x);
			if (x != NULL) continue;
			break;
		
		case IMPORT_NAME:
			w = GETNAMEV(oparg);
			x = PyDict_GetItemString(f->f_builtins, "__import__");
			if (x == NULL) {
				PyErr_SetString(PyExc_ImportError,
						"__import__ not found");
				break;
			}
			u = POP();
			w = Py_BuildValue("(OOOO)",
				    w,
				    f->f_globals,
				    f->f_locals == NULL ?
					  Py_None : f->f_locals,
				    u);
			Py_DECREF(u);
			if (w == NULL) {
				x = NULL;
				break;
			}
			x = PyEval_CallObject(x, w);
			Py_DECREF(w);
			PUSH(x);
			if (x != NULL) continue;
			break;
		
		case IMPORT_STAR:
			v = POP();
			PyFrame_FastToLocals(f);
			if ((x = f->f_locals) == NULL) {
				PyErr_SetString(PyExc_SystemError,
						"no locals");
				break;
			}
			err = import_all_from(x, v);
			PyFrame_LocalsToFast(f, 0);
			Py_DECREF(v);
			if (err == 0) continue;
			break;

		case IMPORT_FROM:
			w = GETNAMEV(oparg);
			v = TOP();
			x = import_from(v, w);
			PUSH(x);
			if (x != NULL) continue;
			break;

		case JUMP_FORWARD:
			JUMPBY(oparg);
			continue;
		
		case JUMP_IF_FALSE:
			err = PyObject_IsTrue(TOP());
			if (err > 0)
				err = 0;
			else if (err == 0)
				JUMPBY(oparg);
			else
				break;
			continue;
		
		case JUMP_IF_TRUE:
			err = PyObject_IsTrue(TOP());
			if (err > 0) {
				err = 0;
				JUMPBY(oparg);
			}
			else if (err == 0)
				;
			else
				break;
			continue;
		
		case JUMP_ABSOLUTE:
			JUMPTO(oparg);
			continue;
		
		case FOR_LOOP:
			/* for v in s: ...
			   On entry: stack contains s, i.
			   On exit: stack contains s, i+1, s[i];
			   but if loop exhausted:
			   	s, i are popped, and we jump */
			w = POP(); /* Loop index */
			v = POP(); /* Sequence object */
			u = loop_subscript(v, w);
			if (u != NULL) {
				PUSH(v);
				x = PyInt_FromLong(PyInt_AsLong(w)+1);
				PUSH(x);
				Py_DECREF(w);
				PUSH(u);
				if (x != NULL) continue;
			}
			else {
				Py_DECREF(v);
				Py_DECREF(w);
				/* A NULL can mean "s exhausted"
				   but also an error: */
				if (PyErr_Occurred())
					why = WHY_EXCEPTION;
				else {
					JUMPBY(oparg);
					continue;
				}
			}
			break;
		
		case SETUP_LOOP:
		case SETUP_EXCEPT:
		case SETUP_FINALLY:
			PyFrame_BlockSetup(f, opcode, INSTR_OFFSET() + oparg,
						STACK_LEVEL());
			continue;
		
		case SET_LINENO:
#ifdef LLTRACE
			if (lltrace)
				printf("--- %s:%d \n", filename, oparg);
#endif
			f->f_lineno = oparg;
			if (f->f_trace == NULL)
				continue;
			/* Trace each line of code reached */
			f->f_lasti = INSTR_OFFSET();
			err = call_trace(&f->f_trace, &f->f_trace,
					 f, "line", Py_None);
			break;

		case CALL_FUNCTION:
		case CALL_FUNCTION_VAR:
		case CALL_FUNCTION_KW:
		case CALL_FUNCTION_VAR_KW:
		{
		    int na = oparg & 0xff;
		    int nk = (oparg>>8) & 0xff;
		    int flags = (opcode - CALL_FUNCTION) & 3;
		    int n = na + 2*nk + (flags & 1) + ((flags >> 1) & 1);
		    PyObject **pfunc = stack_pointer - n - 1;
		    PyObject *func = *pfunc;
		    PyObject *self = NULL;
		    PyObject *class = NULL;
		    f->f_lasti = INSTR_OFFSET() - 3; /* For tracing */
		    if (PyMethod_Check(func)) {
			self = PyMethod_Self(func);
			class = PyMethod_Class(func);
			func = PyMethod_Function(func);
			Py_INCREF(func);
			if (self != NULL) {
			    Py_INCREF(self);
			    Py_DECREF(*pfunc);
			    *pfunc = self;
			    na++;
			    n++;
			}
			else {
			    /* Unbound methods must be called with an
			       instance of the class (or a derived
			       class) as first argument */ 
			    if (na > 0 && (self = stack_pointer[-n]) != NULL 
				&& PyInstance_Check(self) 
				&& PyClass_IsSubclass((PyObject *)
				    (((PyInstanceObject *)self)->in_class),
						      class))
                                  /* Handy-dandy */ ;
			    else {
				PyErr_SetString(PyExc_TypeError,
	    "unbound method must be called with class instance 1st argument");
				x = NULL;
				break;
			    }
			}
		    }
		    else
			Py_INCREF(func);
		    if (PyFunction_Check(func) && flags == 0) {
			PyObject *co = PyFunction_GetCode(func);
			PyObject *globals = PyFunction_GetGlobals(func);
			PyObject *argdefs = PyFunction_GetDefaults(func);
			PyObject **d;
			int nd;
			if (argdefs != NULL) {
			    d = &PyTuple_GET_ITEM(argdefs, 0);
			    nd = ((PyTupleObject *)argdefs)->ob_size;
			}
			else {
			    d = NULL;
			    nd = 0;
			}
			x = eval_code2((PyCodeObject *)co, globals, 
				       (PyObject *)NULL, stack_pointer-n, na,
				       stack_pointer-2*nk, nk, d, nd,
				       class);
		    }
		    else {
			int nstar = 0;
			PyObject *callargs;
			PyObject *stararg = 0;
			PyObject *kwdict = NULL;
			if (flags & 2) {
			    kwdict = POP();
			    if (!PyDict_Check(kwdict)) {
				PyErr_SetString(PyExc_TypeError,
					"** argument must be a dictionary");
				goto extcall_fail;
			    }
			}
			if (flags & 1) {
			    stararg = POP();
			    if (!PySequence_Check(stararg)) {
				PyErr_SetString(PyExc_TypeError,
					"* argument must be a sequence");
				goto extcall_fail;
			    }
			    /* Convert abstract sequence to concrete tuple */
			    if (!PyTuple_Check(stararg)) {
				PyObject *t = NULL;
				t = PySequence_Tuple(stararg);
				if (t == NULL) {
				    goto extcall_fail;
				}
				Py_DECREF(stararg);
				stararg = t;
			    }
			    nstar = PyTuple_GET_SIZE(stararg);
			    if (nstar < 0) {
				goto extcall_fail;
			    }
			}
			if (nk > 0) {
			    if (kwdict == NULL) {
				kwdict = PyDict_New();
				if (kwdict == NULL) {
    				    goto extcall_fail;
				}
			    }
			    else {
				    PyObject *d = PyDict_Copy(kwdict);
				    if (d == NULL) {
					    goto extcall_fail;
				    }
				    Py_DECREF(kwdict);
				    kwdict = d;
			    }
			    err = 0;
			    while (--nk >= 0) {
				PyObject *value = POP();
				PyObject *key = POP();
				if (PyDict_GetItem(kwdict, key) != NULL) {
				    err = 1;
				    PyErr_Format(PyExc_TypeError,
					"keyword parameter redefined: %.400s",
						 PyString_AsString(key));
				    Py_DECREF(key);
				    Py_DECREF(value);
				    goto extcall_fail;
				}
				err = PyDict_SetItem(kwdict, key, value);
				Py_DECREF(key);
				Py_DECREF(value);
				if (err)
				    break;
			    }
			    if (err) {
			      extcall_fail:
				Py_XDECREF(kwdict);
				Py_XDECREF(stararg);
				Py_DECREF(func);
				x=NULL;
				break;
			    }
			}
			callargs = PyTuple_New(na + nstar);
			if (callargs == NULL) {
			    x = NULL;
			    break;
			}
			if (stararg) {
			    int i;
			    for (i = 0; i < nstar; i++) {
				PyObject *a = PyTuple_GET_ITEM(stararg, i);
				Py_INCREF(a);
				PyTuple_SET_ITEM(callargs, na + i, a);
			    }
			    Py_DECREF(stararg);
			}
			while (--na >= 0) {
			    w = POP();
			    PyTuple_SET_ITEM(callargs, na, w);
			}
			x = PyEval_CallObjectWithKeywords(func,
							  callargs,
							  kwdict);  
			Py_DECREF(callargs);
			Py_XDECREF(kwdict);
		    }
		    Py_DECREF(func);
		    while (stack_pointer > pfunc) {
			w = POP();
			Py_DECREF(w);
		    }
		    PUSH(x);
		    if (x != NULL) continue;
		    break;
		}
		
		case MAKE_FUNCTION:
			v = POP(); /* code object */
			x = PyFunction_New(v, f->f_globals);
			Py_DECREF(v);
			/* XXX Maybe this should be a separate opcode? */
			if (x != NULL && oparg > 0) {
				v = PyTuple_New(oparg);
				if (v == NULL) {
					Py_DECREF(x);
					x = NULL;
					break;
				}
				while (--oparg >= 0) {
					w = POP();
					PyTuple_SET_ITEM(v, oparg, w);
				}
				err = PyFunction_SetDefaults(x, v);
				Py_DECREF(v);
			}
			PUSH(x);
			break;

		case BUILD_SLICE:
			if (oparg == 3)
				w = POP();
			else
				w = NULL;
			v = POP();
			u = POP();
			x = PySlice_New(u, v, w);
			Py_DECREF(u);
			Py_DECREF(v);
			Py_XDECREF(w);
			PUSH(x);
			if (x != NULL) continue;
			break;

		case EXTENDED_ARG:
			opcode = NEXTOP();
			oparg = oparg<<16 | NEXTARG();
			goto dispatch_opcode;
			break;

		default:
			fprintf(stderr,
				"XXX lineno: %d, opcode: %d\n",
				f->f_lineno, opcode);
			PyErr_SetString(PyExc_SystemError, "unknown opcode");
			why = WHY_EXCEPTION;
			break;

#ifdef CASE_TOO_BIG
		}
#endif

		} /* switch */

	    on_error:
		
		/* Quickly continue if no error occurred */
		
		if (why == WHY_NOT) {
			if (err == 0 && x != NULL) {
#ifdef CHECKEXC
				/* This check is expensive! */
				if (PyErr_Occurred())
					fprintf(stderr,
						"XXX undetected error\n");
				else
#endif
					continue; /* Normal, fast path */
			}
			why = WHY_EXCEPTION;
			x = Py_None;
			err = 0;
		}

		/* Double-check exception status */
		
		if (why == WHY_EXCEPTION || why == WHY_RERAISE) {
			if (!PyErr_Occurred()) {
				PyErr_SetString(PyExc_SystemError,
					"error return without exception set");
				why = WHY_EXCEPTION;
			}
		}
#ifdef CHECKEXC
		else {
			/* This check is expensive! */
			if (PyErr_Occurred()) {
				fprintf(stderr,
					"XXX undetected error (why=%d)\n",
					why);
				why = WHY_EXCEPTION;
			}
		}
#endif

		/* Log traceback info if this is a real exception */
		
		if (why == WHY_EXCEPTION) {
			f->f_lasti = INSTR_OFFSET() - 1;
			if (HAS_ARG(opcode))
				f->f_lasti -= 2;
			PyTraceBack_Here(f);

			if (f->f_trace)
				call_exc_trace(&f->f_trace, &f->f_trace, f);
			if (tstate->sys_profilefunc)
				call_exc_trace(&tstate->sys_profilefunc,
					       (PyObject**)0, f);
		}
		
		/* For the rest, treat WHY_RERAISE as WHY_EXCEPTION */
		
		if (why == WHY_RERAISE)
			why = WHY_EXCEPTION;

		/* Unwind stacks if a (pseudo) exception occurred */
		
		while (why != WHY_NOT && f->f_iblock > 0) {
			PyTryBlock *b = PyFrame_BlockPop(f);
			while (STACK_LEVEL() > b->b_level) {
				v = POP();
				Py_XDECREF(v);
			}
			if (b->b_type == SETUP_LOOP && why == WHY_BREAK) {
				why = WHY_NOT;
				JUMPTO(b->b_handler);
				break;
			}
			if (b->b_type == SETUP_FINALLY ||
			    (b->b_type == SETUP_EXCEPT &&
			     why == WHY_EXCEPTION)) {
				if (why == WHY_EXCEPTION) {
					PyObject *exc, *val, *tb;
					PyErr_Fetch(&exc, &val, &tb);
					if (val == NULL) {
						val = Py_None;
						Py_INCREF(val);
					}
					/* Make the raw exception data
					   available to the handler,
					   so a program can emulate the
					   Python main loop.  Don't do
					   this for 'finally'. */
					if (b->b_type == SETUP_EXCEPT) {
						PyErr_NormalizeException(
							&exc, &val, &tb);
						set_exc_info(tstate,
							     exc, val, tb);
					}
					PUSH(tb);
					PUSH(val);
					PUSH(exc);
				}
				else {
					if (why == WHY_RETURN)
						PUSH(retval);
					v = PyInt_FromLong((long)why);
					PUSH(v);
				}
				why = WHY_NOT;
				JUMPTO(b->b_handler);
				break;
			}
		} /* unwind stack */

		/* End the loop if we still have an error (or return) */
		
		if (why != WHY_NOT)
			break;
		
	} /* main loop */
	
	/* Pop remaining stack entries */
	
	while (!EMPTY()) {
		v = POP();
		Py_XDECREF(v);
	}
	
	if (why != WHY_RETURN)
		retval = NULL;
	
	if (f->f_trace) {
		if (why == WHY_RETURN) {
			if (call_trace(&f->f_trace, &f->f_trace, f,
				       "return", retval)) {
				Py_XDECREF(retval);
				retval = NULL;
				why = WHY_EXCEPTION;
			}
		}
	}
	
	if (tstate->sys_profilefunc && why == WHY_RETURN) {
		if (call_trace(&tstate->sys_profilefunc, (PyObject**)0,
			       f, "return", retval)) {
			Py_XDECREF(retval);
			retval = NULL;
			why = WHY_EXCEPTION;
		}
	}

	reset_exc_info(tstate);

	--tstate->recursion_depth;

  fail: /* Jump here from prelude on failure */
	
	/* Restore previous frame and release the current one */

	tstate->frame = f->f_back;
	Py_DECREF(f);
	
	return retval;
}

static void
set_exc_info(PyThreadState *tstate, PyObject *type, PyObject *value, PyObject *tb)
{
	PyFrameObject *frame;
	PyObject *tmp_type, *tmp_value, *tmp_tb;

	frame = tstate->frame;
	if (frame->f_exc_type == NULL) {
		/* This frame didn't catch an exception before */
		/* Save previous exception of this thread in this frame */
		if (tstate->exc_type == NULL) {
			Py_INCREF(Py_None);
			tstate->exc_type = Py_None;
		}
		tmp_type = frame->f_exc_type;
		tmp_value = frame->f_exc_value;
		tmp_tb = frame->f_exc_traceback;
		Py_XINCREF(tstate->exc_type);
		Py_XINCREF(tstate->exc_value);
		Py_XINCREF(tstate->exc_traceback);
		frame->f_exc_type = tstate->exc_type;
		frame->f_exc_value = tstate->exc_value;
		frame->f_exc_traceback = tstate->exc_traceback;
		Py_XDECREF(tmp_type);
		Py_XDECREF(tmp_value);
		Py_XDECREF(tmp_tb);
	}
	/* Set new exception for this thread */
	tmp_type = tstate->exc_type;
	tmp_value = tstate->exc_value;
	tmp_tb = tstate->exc_traceback;
	Py_XINCREF(type);
	Py_XINCREF(value);
	Py_XINCREF(tb);
	tstate->exc_type = type;
	tstate->exc_value = value;
	tstate->exc_traceback = tb;
	Py_XDECREF(tmp_type);
	Py_XDECREF(tmp_value);
	Py_XDECREF(tmp_tb);
	/* For b/w compatibility */
	PySys_SetObject("exc_type", type);
	PySys_SetObject("exc_value", value);
	PySys_SetObject("exc_traceback", tb);
}

static void
reset_exc_info(PyThreadState *tstate)
{
	PyFrameObject *frame;
	PyObject *tmp_type, *tmp_value, *tmp_tb;
	frame = tstate->frame;
	if (frame->f_exc_type != NULL) {
		/* This frame caught an exception */
		tmp_type = tstate->exc_type;
		tmp_value = tstate->exc_value;
		tmp_tb = tstate->exc_traceback;
		Py_XINCREF(frame->f_exc_type);
		Py_XINCREF(frame->f_exc_value);
		Py_XINCREF(frame->f_exc_traceback);
		tstate->exc_type = frame->f_exc_type;
		tstate->exc_value = frame->f_exc_value;
		tstate->exc_traceback = frame->f_exc_traceback;
		Py_XDECREF(tmp_type);
		Py_XDECREF(tmp_value);
		Py_XDECREF(tmp_tb);
		/* For b/w compatibility */
		PySys_SetObject("exc_type", frame->f_exc_type);
		PySys_SetObject("exc_value", frame->f_exc_value);
		PySys_SetObject("exc_traceback", frame->f_exc_traceback);
	}
	tmp_type = frame->f_exc_type;
	tmp_value = frame->f_exc_value;
	tmp_tb = frame->f_exc_traceback;
	frame->f_exc_type = NULL;
	frame->f_exc_value = NULL;
	frame->f_exc_traceback = NULL;
	Py_XDECREF(tmp_type);
	Py_XDECREF(tmp_value);
	Py_XDECREF(tmp_tb);
}

/* Logic for the raise statement (too complicated for inlining).
   This *consumes* a reference count to each of its arguments. */
static enum why_code
do_raise(PyObject *type, PyObject *value, PyObject *tb)
{
	if (type == NULL) {
		/* Reraise */
		PyThreadState *tstate = PyThreadState_Get();
		type = tstate->exc_type == NULL ? Py_None : tstate->exc_type;
		value = tstate->exc_value;
		tb = tstate->exc_traceback;
		Py_XINCREF(type);
		Py_XINCREF(value);
		Py_XINCREF(tb);
	}
		
	/* We support the following forms of raise:
	   raise <class>, <classinstance>
	   raise <class>, <argument tuple>
	   raise <class>, None
	   raise <class>, <argument>
	   raise <classinstance>, None
	   raise <string>, <object>
	   raise <string>, None

	   An omitted second argument is the same as None.

	   In addition, raise <tuple>, <anything> is the same as
	   raising the tuple's first item (and it better have one!);
	   this rule is applied recursively.

	   Finally, an optional third argument can be supplied, which
	   gives the traceback to be substituted (useful when
	   re-raising an exception after examining it).  */

	/* First, check the traceback argument, replacing None with
	   NULL. */
	if (tb == Py_None) {
		Py_DECREF(tb);
		tb = NULL;
	}
	else if (tb != NULL && !PyTraceBack_Check(tb)) {
		PyErr_SetString(PyExc_TypeError,
			   "raise 3rd arg must be traceback or None");
		goto raise_error;
	}

	/* Next, replace a missing value with None */
	if (value == NULL) {
		value = Py_None;
		Py_INCREF(value);
	}

	/* Next, repeatedly, replace a tuple exception with its first item */
	while (PyTuple_Check(type) && PyTuple_Size(type) > 0) {
		PyObject *tmp = type;
		type = PyTuple_GET_ITEM(type, 0);
		Py_INCREF(type);
		Py_DECREF(tmp);
	}

	if (PyString_Check(type))
		;

	else if (PyClass_Check(type))
		PyErr_NormalizeException(&type, &value, &tb);

	else if (PyInstance_Check(type)) {
		/* Raising an instance.  The value should be a dummy. */
		if (value != Py_None) {
			PyErr_SetString(PyExc_TypeError,
			  "instance exception may not have a separate value");
			goto raise_error;
		}
		else {
			/* Normalize to raise <class>, <instance> */
			Py_DECREF(value);
			value = type;
			type = (PyObject*) ((PyInstanceObject*)type)->in_class;
			Py_INCREF(type);
		}
	}
	else {
		/* Not something you can raise.  You get an exception
		   anyway, just not what you specified :-) */
		PyErr_SetString(PyExc_TypeError,
		    "exceptions must be strings, classes, or instances");
		goto raise_error;
	}
	PyErr_Restore(type, value, tb);
	if (tb == NULL)
		return WHY_EXCEPTION;
	else
		return WHY_RERAISE;
 raise_error:
	Py_XDECREF(value);
	Py_XDECREF(type);
	Py_XDECREF(tb);
	return WHY_EXCEPTION;
}

static int
unpack_sequence(PyObject *v, int argcnt, PyObject **sp)
{
	int i;
	PyObject *w;
	
	for (i = 0; i < argcnt; i++) {
		if (! (w = PySequence_GetItem(v, i))) {
			if (PyErr_ExceptionMatches(PyExc_IndexError))
				PyErr_SetString(PyExc_ValueError,
					      "unpack sequence of wrong size");
			goto finally;
		}
		*--sp = w;
	}
	/* we better get an IndexError now */
	if (PySequence_GetItem(v, i) == NULL) {
		if (PyErr_ExceptionMatches(PyExc_IndexError)) {
			PyErr_Clear();
			return 1;
		}
		/* some other exception occurred. fall through to finally */
	}
	else
		PyErr_SetString(PyExc_ValueError,
				"unpack sequence of wrong size");
	/* fall through */
finally:
	for (; i > 0; i--, sp++)
		Py_DECREF(*sp);

	return 0;
}


#ifdef LLTRACE
static int
prtrace(PyObject *v, char *str)
{
	printf("%s ", str);
	if (PyObject_Print(v, stdout, 0) != 0)
		PyErr_Clear(); /* Don't know what else to do */
	printf("\n");
	return 1;
}
#endif

static void
call_exc_trace(PyObject **p_trace, PyObject **p_newtrace, PyFrameObject *f)
{
	PyObject *type, *value, *traceback, *arg;
	int err;
	PyErr_Fetch(&type, &value, &traceback);
	if (value == NULL) {
		value = Py_None;
		Py_INCREF(value);
	}
	arg = Py_BuildValue("(OOO)", type, value, traceback);
	if (arg == NULL) {
		PyErr_Restore(type, value, traceback);
		return;
	}
	err = call_trace(p_trace, p_newtrace, f, "exception", arg);
	Py_DECREF(arg);
	if (err == 0)
		PyErr_Restore(type, value, traceback);
	else {
		Py_XDECREF(type);
		Py_XDECREF(value);
		Py_XDECREF(traceback);
	}
}

/* PyObject **p_trace: in/out; may not be NULL;
 				may not point to NULL variable initially
  PyObject **p_newtrace: in/out; may be NULL;
  				may point to NULL variable;
 				may be same variable as p_newtrace */

static int
call_trace(PyObject **p_trace, PyObject **p_newtrace, PyFrameObject *f,
	   char *msg, PyObject *arg)
{
	PyThreadState *tstate = f->f_tstate;
	PyObject *args, *what;
	PyObject *res = NULL;
	
	if (tstate->tracing) {
		/* Don't do recursive traces */
		if (p_newtrace) {
			Py_XDECREF(*p_newtrace);
			*p_newtrace = NULL;
		}
		return 0;
	}
	
	args = PyTuple_New(3);
	if (args == NULL)
		goto cleanup;
	what = PyString_FromString(msg);
	if (what == NULL)
		goto cleanup;
	Py_INCREF(f);
	PyTuple_SET_ITEM(args, 0, (PyObject *)f);
	PyTuple_SET_ITEM(args, 1, what);
	if (arg == NULL)
		arg = Py_None;
	Py_INCREF(arg);
	PyTuple_SET_ITEM(args, 2, arg);
	tstate->tracing++;
	PyFrame_FastToLocals(f);
	res = PyEval_CallObject(*p_trace, args); /* May clear *p_trace! */
	PyFrame_LocalsToFast(f, 1);
	tstate->tracing--;
 cleanup:
	Py_XDECREF(args);
	if (res == NULL) {
		/* The trace proc raised an exception */
		PyTraceBack_Here(f);
		Py_XDECREF(*p_trace);
		*p_trace = NULL;
		if (p_newtrace) {
			Py_XDECREF(*p_newtrace);
			*p_newtrace = NULL;
		}
		/* to be extra double plus sure we don't get recursive
		 * calls inf either tracefunc or profilefunc gets an
		 * exception, zap the global variables.
		 */
		Py_XDECREF(tstate->sys_tracefunc);
		tstate->sys_tracefunc = NULL;
		Py_XDECREF(tstate->sys_profilefunc);
		tstate->sys_profilefunc = NULL;
		return -1;
	}
	else {
		if (p_newtrace) {
			Py_XDECREF(*p_newtrace);
			if (res == Py_None)
				*p_newtrace = NULL;
			else {
				Py_INCREF(res);
				*p_newtrace = res;
			}
		}
		Py_DECREF(res);
		return 0;
	}
}

PyObject *
PyEval_GetBuiltins(void)
{
	PyThreadState *tstate = PyThreadState_Get();
	PyFrameObject *current_frame = tstate->frame;
	if (current_frame == NULL)
		return tstate->interp->builtins;
	else
		return current_frame->f_builtins;
}

PyObject *
PyEval_GetLocals(void)
{
	PyFrameObject *current_frame = PyThreadState_Get()->frame;
	if (current_frame == NULL)
		return NULL;
	PyFrame_FastToLocals(current_frame);
	return current_frame->f_locals;
}

PyObject *
PyEval_GetGlobals(void)
{
	PyFrameObject *current_frame = PyThreadState_Get()->frame;
	if (current_frame == NULL)
		return NULL;
	else
		return current_frame->f_globals;
}

PyObject *
PyEval_GetFrame(void)
{
	PyFrameObject *current_frame = PyThreadState_Get()->frame;
	return (PyObject *)current_frame;
}

int
PyEval_GetRestricted(void)
{
	PyFrameObject *current_frame = PyThreadState_Get()->frame;
	return current_frame == NULL ? 0 : current_frame->f_restricted;
}

int
Py_FlushLine(void)
{
	PyObject *f = PySys_GetObject("stdout");
	if (f == NULL)
		return 0;
	if (!PyFile_SoftSpace(f, 0))
		return 0;
	return PyFile_WriteString("\n", f);
}


/* External interface to call any callable object.
   The arg must be a tuple or NULL. */

#undef PyEval_CallObject
/* for backward compatibility: export this interface */

PyObject *
PyEval_CallObject(PyObject *func, PyObject *arg)
{
	return PyEval_CallObjectWithKeywords(func, arg, (PyObject *)NULL);
}
#define PyEval_CallObject(func,arg) \
        PyEval_CallObjectWithKeywords(func, arg, (PyObject *)NULL)

PyObject *
PyEval_CallObjectWithKeywords(PyObject *func, PyObject *arg, PyObject *kw)
{
        ternaryfunc call;
        PyObject *result;

	if (arg == NULL)
		arg = PyTuple_New(0);
	else if (!PyTuple_Check(arg)) {
		PyErr_SetString(PyExc_TypeError,
				"argument list must be a tuple");
		return NULL;
	}
	else
		Py_INCREF(arg);

	if (kw != NULL && !PyDict_Check(kw)) {
		PyErr_SetString(PyExc_TypeError,
				"keyword list must be a dictionary");
		Py_DECREF(arg);
		return NULL;
	}

        if ((call = func->ob_type->tp_call) != NULL)
                result = (*call)(func, arg, kw);
        else if (PyMethod_Check(func) || PyFunction_Check(func))
		result = call_function(func, arg, kw);
	else
		result = call_builtin(func, arg, kw);

	Py_DECREF(arg);
	
        if (result == NULL && !PyErr_Occurred())
		PyErr_SetString(PyExc_SystemError,
			   "NULL result without error in call_object");
        
        return result;
}

static PyObject *
call_builtin(PyObject *func, PyObject *arg, PyObject *kw)
{
	if (PyCFunction_Check(func)) {
		PyCFunction meth = PyCFunction_GetFunction(func);
		PyObject *self = PyCFunction_GetSelf(func);
		int flags = PyCFunction_GetFlags(func);
		if (!(flags & METH_VARARGS)) {
			int size = PyTuple_Size(arg);
			if (size == 1)
				arg = PyTuple_GET_ITEM(arg, 0);
			else if (size == 0)
				arg = NULL;
		}
		if (flags & METH_KEYWORDS)
			return (*(PyCFunctionWithKeywords)meth)(self, arg, kw);
		if (kw != NULL && PyDict_Size(kw) != 0) {
			PyErr_SetString(PyExc_TypeError,
				   "this function takes no keyword arguments");
			return NULL;
		}
		return (*meth)(self, arg);
	}
	if (PyClass_Check(func)) {
		return PyInstance_New(func, arg, kw);
	}
	if (PyInstance_Check(func)) {
	        PyObject *res, *call = PyObject_GetAttrString(func,"__call__");
		if (call == NULL) {
			PyErr_Clear();
			PyErr_SetString(PyExc_AttributeError,
				   "no __call__ method defined");
			return NULL;
		}
		res = PyEval_CallObjectWithKeywords(call, arg, kw);
		Py_DECREF(call);
		return res;
	}
	PyErr_Format(PyExc_TypeError, "call of non-function (type %.400s)",
		     func->ob_type->tp_name);
	return NULL;
}

static PyObject *
call_function(PyObject *func, PyObject *arg, PyObject *kw)
{
	PyObject *class = NULL; /* == owner */
	PyObject *argdefs;
	PyObject **d, **k;
	int nk, nd;
	PyObject *result;
	
	if (kw != NULL && !PyDict_Check(kw)) {
		PyErr_BadInternalCall();
		return NULL;
	}
	
	if (PyMethod_Check(func)) {
		PyObject *self = PyMethod_Self(func);
		class = PyMethod_Class(func);
		func = PyMethod_Function(func);
		if (self == NULL) {
			/* Unbound methods must be called with an instance of
			   the class (or a derived class) as first argument */
			if (PyTuple_Size(arg) >= 1) {
				self = PyTuple_GET_ITEM(arg, 0);
				if (self != NULL &&
				    PyInstance_Check(self) &&
				    PyClass_IsSubclass((PyObject *)
				      (((PyInstanceObject *)self)->in_class),
					       class))
					/* Handy-dandy */ ;
				else
					self = NULL;
			}
			if (self == NULL) {
				PyErr_SetString(PyExc_TypeError,
	   "unbound method must be called with class instance 1st argument");
				return NULL;
			}
			Py_INCREF(arg);
		}
		else {
			int argcount = PyTuple_Size(arg);
			PyObject *newarg = PyTuple_New(argcount + 1);
			int i;
			if (newarg == NULL)
				return NULL;
			Py_INCREF(self);
			PyTuple_SET_ITEM(newarg, 0, self);
			for (i = 0; i < argcount; i++) {
				PyObject *v = PyTuple_GET_ITEM(arg, i);
				Py_XINCREF(v);
				PyTuple_SET_ITEM(newarg, i+1, v);
			}
			arg = newarg;
		}
		if (!PyFunction_Check(func)) {
			result = PyEval_CallObjectWithKeywords(func, arg, kw);
			Py_DECREF(arg);
			return result;
		}
	}
	else {
		if (!PyFunction_Check(func)) {
			PyErr_Format(PyExc_TypeError,
				     "call of non-function (type %.200s)",
				     func->ob_type->tp_name);
			return NULL;
		}
		Py_INCREF(arg);
	}
	
	argdefs = PyFunction_GetDefaults(func);
	if (argdefs != NULL && PyTuple_Check(argdefs)) {
		d = &PyTuple_GET_ITEM((PyTupleObject *)argdefs, 0);
		nd = PyTuple_Size(argdefs);
	}
	else {
		d = NULL;
		nd = 0;
	}
	
	if (kw != NULL) {
		int pos, i;
		nk = PyDict_Size(kw);
		k = PyMem_NEW(PyObject *, 2*nk);
		if (k == NULL) {
			PyErr_NoMemory();
			Py_DECREF(arg);
			return NULL;
		}
		pos = i = 0;
		while (PyDict_Next(kw, &pos, &k[i], &k[i+1]))
			i += 2;
		nk = i/2;
		/* XXX This is broken if the caller deletes dict items! */
	}
	else {
		k = NULL;
		nk = 0;
	}
	
	result = eval_code2(
		(PyCodeObject *)PyFunction_GetCode(func),
		PyFunction_GetGlobals(func), (PyObject *)NULL,
		&PyTuple_GET_ITEM(arg, 0), PyTuple_Size(arg),
		k, nk,
		d, nd,
		class);
	
	Py_DECREF(arg);
	if (k != NULL)
		PyMem_DEL(k);
	
	return result;
}

#define SLICE_ERROR_MSG \
	"standard sequence type does not support step size other than one"

static PyObject *
loop_subscript(PyObject *v, PyObject *w)
{
	PySequenceMethods *sq = v->ob_type->tp_as_sequence;
	int i;
	if (sq == NULL || sq->sq_item == NULL) {
		PyErr_SetString(PyExc_TypeError, "loop over non-sequence");
		return NULL;
	}
	i = PyInt_AsLong(w);
	v = (*sq->sq_item)(v, i);
	if (v)
		return v;
	if (PyErr_ExceptionMatches(PyExc_IndexError))
		PyErr_Clear();
	return NULL;
}

/* Extract a slice index from a PyInt or PyLong, the index is bound to
   the range [-INT_MAX+1, INTMAX]. Returns 0 and an exception if there is
   and error. Returns 1 on success.*/

int
_PyEval_SliceIndex(PyObject *v, int *pi)
{
	if (v != NULL) {
		long x;
		if (PyInt_Check(v)) {
			x = PyInt_AsLong(v);
		} else if (PyLong_Check(v)) {
			x = PyLong_AsLong(v);
			if (x==-1 && PyErr_Occurred()) {
				PyObject *long_zero;

				if (!PyErr_ExceptionMatches( PyExc_OverflowError ) ) {
					/* It's not an overflow error, so just 
					   signal an error */
					return 0;
				}

				/* It's an overflow error, so we need to 
				   check the sign of the long integer, 
				   set the value to INT_MAX or 0, and clear 
				   the error. */

				/* Create a long integer with a value of 0 */
				long_zero = PyLong_FromLong( 0L );
				if (long_zero == NULL) return 0;

				/* Check sign */
				if (PyObject_Compare(long_zero, v) < 0)
					x = INT_MAX;
				else
					x = 0;
				
				/* Free the long integer we created, and clear the
				   OverflowError */
				Py_DECREF(long_zero);
				PyErr_Clear();
			}
		} else {
			PyErr_SetString(PyExc_TypeError,
					"slice index must be int");
			return 0;
		}
		/* Truncate -- very long indices are truncated anyway */
		if (x > INT_MAX)
			x = INT_MAX;
		else if (x < -INT_MAX)
			x = 0;
		*pi = x;
	}
	return 1;
}

static PyObject *
apply_slice(PyObject *u, PyObject *v, PyObject *w) /* return u[v:w] */
{
	int ilow = 0, ihigh = INT_MAX;
	if (!_PyEval_SliceIndex(v, &ilow))
		return NULL;
	if (!_PyEval_SliceIndex(w, &ihigh))
		return NULL;
	return PySequence_GetSlice(u, ilow, ihigh);
}

static int
assign_slice(PyObject *u, PyObject *v, PyObject *w, PyObject *x) /* u[v:w] = x */
{
	int ilow = 0, ihigh = INT_MAX;
	if (!_PyEval_SliceIndex(v, &ilow))
		return -1;
	if (!_PyEval_SliceIndex(w, &ihigh))
		return -1;
	if (x == NULL)
		return PySequence_DelSlice(u, ilow, ihigh);
	else
		return PySequence_SetSlice(u, ilow, ihigh, x);
}

static PyObject *
cmp_outcome(int op, register PyObject *v, register PyObject *w)
{
	register int cmp;
	register int res = 0;
	switch (op) {
	case IS:
	case IS_NOT:
		res = (v == w);
		if (op == (int) IS_NOT)
			res = !res;
		break;
	case IN:
	case NOT_IN:
		res = PySequence_Contains(w, v);
		if (res < 0)
			return NULL;
		if (op == (int) NOT_IN)
			res = !res;
		break;
	case EXC_MATCH:
		res = PyErr_GivenExceptionMatches(v, w);
		break;
	default:
		cmp = PyObject_Compare(v, w);
		if (cmp && PyErr_Occurred())
			return NULL;
		switch (op) {
		case LT: res = cmp <  0; break;
		case LE: res = cmp <= 0; break;
		case EQ: res = cmp == 0; break;
		case NE: res = cmp != 0; break;
		case GT: res = cmp >  0; break;
		case GE: res = cmp >= 0; break;
		/* XXX no default? (res is initialized to 0 though) */
		}
	}
	v = res ? Py_True : Py_False;
	Py_INCREF(v);
	return v;
}

static PyObject *
import_from(PyObject *v, PyObject *name)
{
	PyObject *w, *x;
	if (!PyModule_Check(v)) {
		PyErr_SetString(PyExc_TypeError,
				"import-from requires module object");
		return NULL;
	}
	w = PyModule_GetDict(v); /* TDB: can this not fail ? */
	x = PyDict_GetItem(w, name);
	if (x == NULL) {
		PyErr_Format(PyExc_ImportError,
			     "cannot import name %.230s",
			     PyString_AsString(name));
	} else
		Py_INCREF(x);
	return x;
}

static int
import_all_from(PyObject *locals, PyObject *v)
{
	int pos = 0, err;
	PyObject *name, *value;
	PyObject *w;

	if (!PyModule_Check(v)) {
		PyErr_SetString(PyExc_TypeError,
				"import-from requires module object");
		return -1;
	}
	w = PyModule_GetDict(v); /* TBD: can this not fail ? */

	while (PyDict_Next(w, &pos, &name, &value)) {
		if (!PyString_Check(name) ||
			PyString_AsString(name)[0] == '_')
				continue;
		Py_INCREF(value);
		err = PyDict_SetItem(locals, name, value);
		Py_DECREF(value);
		if (err != 0)
			return -1;
	}
	return 0;
}

static PyObject *
build_class(PyObject *methods, PyObject *bases, PyObject *name)
{
	int i, n;
	if (!PyTuple_Check(bases)) {
		PyErr_SetString(PyExc_SystemError,
				"build_class with non-tuple bases");
		return NULL;
	}
	if (!PyDict_Check(methods)) {
		PyErr_SetString(PyExc_SystemError,
				"build_class with non-dictionary");
		return NULL;
	}
	if (!PyString_Check(name)) {
		PyErr_SetString(PyExc_SystemError,
				"build_class with non-string name");
		return NULL;
	}
	n = PyTuple_Size(bases);
	for (i = 0; i < n; i++) {
		PyObject *base = PyTuple_GET_ITEM(bases, i);
		if (!PyClass_Check(base)) {
			/* Call the base's *type*, if it is callable.
			   This code is a hook for Donald Beaudry's
			   and Jim Fulton's type extensions.  In
			   unextended Python it will never be triggered
			   since its types are not callable.
			   Ditto: call the bases's *class*, if it has
			   one.  This makes the same thing possible
			   without writing C code.  A true meta-object
			   protocol! */
			PyObject *basetype = (PyObject *)base->ob_type;
			PyObject *callable = NULL;
			if (PyCallable_Check(basetype))
				callable = basetype;
			else
				callable = PyObject_GetAttrString(
					base, "__class__");
			if (callable) {
				PyObject *args;
				PyObject *newclass = NULL;
				args = Py_BuildValue(
					"(OOO)", name, bases, methods);
				if (args != NULL) {
					newclass = PyEval_CallObject(
						callable, args);
					Py_DECREF(args);
				}
				if (callable != basetype) {
					Py_DECREF(callable);
				}
				return newclass;
			}
			PyErr_SetString(PyExc_TypeError,
				"base is not a class object");
			return NULL;
		}
	}
	return PyClass_New(bases, methods, name);
}

static int
exec_statement(PyFrameObject *f, PyObject *prog, PyObject *globals,
	       PyObject *locals)
{
	int n;
	PyObject *v;
	int plain = 0;

	if (PyTuple_Check(prog) && globals == Py_None && locals == Py_None &&
	    ((n = PyTuple_Size(prog)) == 2 || n == 3)) {
		/* Backward compatibility hack */
		globals = PyTuple_GetItem(prog, 1);
		if (n == 3)
			locals = PyTuple_GetItem(prog, 2);
		prog = PyTuple_GetItem(prog, 0);
	}
	if (globals == Py_None) {
		globals = PyEval_GetGlobals();
		if (locals == Py_None) {
			locals = PyEval_GetLocals();
			plain = 1;
		}
	}
	else if (locals == Py_None)
		locals = globals;
	if (!PyString_Check(prog) &&
	    !PyUnicode_Check(prog) &&
	    !PyCode_Check(prog) &&
	    !PyFile_Check(prog)) {
		PyErr_SetString(PyExc_TypeError,
			   "exec 1st arg must be string, code or file object");
		return -1;
	}
	if (!PyDict_Check(globals) || !PyDict_Check(locals)) {
		PyErr_SetString(PyExc_TypeError,
		    "exec 2nd/3rd args must be dict or None");
		return -1;
	}
	if (PyDict_GetItemString(globals, "__builtins__") == NULL)
		PyDict_SetItemString(globals, "__builtins__", f->f_builtins);
	if (PyCode_Check(prog)) {
		v = PyEval_EvalCode((PyCodeObject *) prog, globals, locals);
	}
	else if (PyFile_Check(prog)) {
		FILE *fp = PyFile_AsFile(prog);
		char *name = PyString_AsString(PyFile_Name(prog));
		v = PyRun_File(fp, name, Py_file_input, globals, locals);
	}
	else {
		char *str;
		if (PyString_AsStringAndSize(prog, &str, NULL))
			return -1;
		v = PyRun_String(str, Py_file_input, globals, locals);
	}
	if (plain)
		PyFrame_LocalsToFast(f, 0);
	if (v == NULL)
		return -1;
	Py_DECREF(v);
	return 0;
}

static void 
format_exc_check_arg(PyObject *exc, char *format_str, PyObject *obj)
{
	char *obj_str;

	if (!obj)
		return;

	obj_str = PyString_AsString(obj);
	if (!obj_str)
		return;

	PyErr_Format(exc, format_str, obj_str);
}

#ifdef DYNAMIC_EXECUTION_PROFILE

PyObject *
getarray(long a[256])
{
	int i;
	PyObject *l = PyList_New(256);
	if (l == NULL) return NULL;
	for (i = 0; i < 256; i++) {
		PyObject *x = PyInt_FromLong(a[i]);
		if (x == NULL) {
			Py_DECREF(l);
			return NULL;
		}
		PyList_SetItem(l, i, x);
	}
	for (i = 0; i < 256; i++)
		a[i] = 0;
	return l;
}

PyObject *
_Py_GetDXProfile(PyObject *self, PyObject *args)
{
#ifndef DXPAIRS
	return getarray(dxp);
#else
	int i;
	PyObject *l = PyList_New(257);
	if (l == NULL) return NULL;
	for (i = 0; i < 257; i++) {
		PyObject *x = getarray(dxpairs[i]);
		if (x == NULL) {
			Py_DECREF(l);
			return NULL;
		}
		PyList_SetItem(l, i, x);
	}
	return l;
#endif
}

#endif
