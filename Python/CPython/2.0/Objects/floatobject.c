
/* Float object implementation */

/* XXX There should be overflow checks here, but it's hard to check
   for any kind of float exception without losing portability. */

#include "Python.h"

#include <ctype.h>

#ifdef i860
/* Cray APP has bogus definition of HUGE_VAL in <math.h> */
#undef HUGE_VAL
#endif

#if defined(HUGE_VAL) && !defined(CHECK)
#define CHECK(x) if (errno != 0) ; \
	else if (-HUGE_VAL <= (x) && (x) <= HUGE_VAL) ; \
	else errno = ERANGE
#endif

#ifndef CHECK
#define CHECK(x) /* Don't know how to check */
#endif

#if !defined(__STDC__) && !defined(macintosh)
extern double fmod(double, double);
extern double pow(double, double);
#endif

#ifdef sun
/* On SunOS4.1 only libm.a exists. Make sure that references to all
   needed math functions exist in the executable, so that dynamic
   loading of mathmodule does not fail. */
double (*_Py_math_funcs_hack[])() = {
	acos, asin, atan, atan2, ceil, cos, cosh, exp, fabs, floor,
	fmod, log, log10, pow, sin, sinh, sqrt, tan, tanh
};
#endif

/* Special free list -- see comments for same code in intobject.c. */
#define BLOCK_SIZE	1000	/* 1K less typical malloc overhead */
#define BHEAD_SIZE	8	/* Enough for a 64-bit pointer */
#define N_FLOATOBJECTS	((BLOCK_SIZE - BHEAD_SIZE) / sizeof(PyFloatObject))

struct _floatblock {
	struct _floatblock *next;
	PyFloatObject objects[N_FLOATOBJECTS];
};

typedef struct _floatblock PyFloatBlock;

static PyFloatBlock *block_list = NULL;
static PyFloatObject *free_list = NULL;

static PyFloatObject *
fill_free_list(void)
{
	PyFloatObject *p, *q;
	/* XXX Float blocks escape the object heap. Use PyObject_MALLOC ??? */
	p = (PyFloatObject *) PyMem_MALLOC(sizeof(PyFloatBlock));
	if (p == NULL)
		return (PyFloatObject *) PyErr_NoMemory();
	((PyFloatBlock *)p)->next = block_list;
	block_list = (PyFloatBlock *)p;
	p = &((PyFloatBlock *)p)->objects[0];
	q = p + N_FLOATOBJECTS;
	while (--q > p)
		q->ob_type = (struct _typeobject *)(q-1);
	q->ob_type = NULL;
	return p + N_FLOATOBJECTS - 1;
}

PyObject *
PyFloat_FromDouble(double fval)
{
	register PyFloatObject *op;
	if (free_list == NULL) {
		if ((free_list = fill_free_list()) == NULL)
			return NULL;
	}
	/* PyObject_New is inlined */
	op = free_list;
	free_list = (PyFloatObject *)op->ob_type;
	PyObject_INIT(op, &PyFloat_Type);
	op->ob_fval = fval;
	return (PyObject *) op;
}

/**************************************************************************
RED_FLAG 22-Sep-2000 tim
PyFloat_FromString's pend argument is braindead.  Prior to this RED_FLAG,

1.  If v was a regular string, *pend was set to point to its terminating
    null byte.  That's useless (the caller can find that without any
    help from this function!).

2.  If v was a Unicode string, or an object convertible to a character
    buffer, *pend was set to point into stack trash (the auto temp
    vector holding the character buffer).  That was downright dangerous.

Since we can't change the interface of a public API function, pend is
still supported but now *officially* useless:  if pend is not NULL,
*pend is set to NULL.
**************************************************************************/
PyObject *
PyFloat_FromString(PyObject *v, char **pend)
{
	const char *s, *last, *end;
	double x;
	char buffer[256]; /* for errors */
	char s_buffer[256]; /* for objects convertible to a char buffer */
	int len;

	if (pend)
		*pend = NULL;
	if (PyString_Check(v)) {
		s = PyString_AS_STRING(v);
		len = PyString_GET_SIZE(v);
	}
	else if (PyUnicode_Check(v)) {
		if (PyUnicode_GET_SIZE(v) >= sizeof(s_buffer)) {
			PyErr_SetString(PyExc_ValueError,
				"Unicode float() literal too long to convert");
			return NULL;
		}
		if (PyUnicode_EncodeDecimal(PyUnicode_AS_UNICODE(v),
					    PyUnicode_GET_SIZE(v),
					    s_buffer, 
					    NULL))
			return NULL;
		s = s_buffer;
		len = (int)strlen(s);
	}
	else if (PyObject_AsCharBuffer(v, &s, &len)) {
		PyErr_SetString(PyExc_TypeError,
				"float() needs a string argument");
		return NULL;
	}

	last = s + len;
	while (*s && isspace(Py_CHARMASK(*s)))
		s++;
	if (*s == '\0') {
		PyErr_SetString(PyExc_ValueError, "empty string for float()");
		return NULL;
	}
	/* We don't care about overflow or underflow.  If the platform supports
	 * them, infinities and signed zeroes (on underflow) are fine.
	 * However, strtod can return 0 for denormalized numbers, where atof
	 * does not.  So (alas!) we special-case a zero result.  Note that
	 * whether strtod sets errno on underflow is not defined, so we can't
	 * key off errno.
         */
	PyFPE_START_PROTECT("strtod", return NULL)
	x = strtod(s, (char **)&end);
	PyFPE_END_PROTECT(x)
	errno = 0;
	/* Believe it or not, Solaris 2.6 can move end *beyond* the null
	   byte at the end of the string, when the input is inf(inity). */
	if (end > last)
		end = last;
	if (end == s) {
		sprintf(buffer, "invalid literal for float(): %.200s", s);
		PyErr_SetString(PyExc_ValueError, buffer);
		return NULL;
	}
	/* Since end != s, the platform made *some* kind of sense out
	   of the input.  Trust it. */
	while (*end && isspace(Py_CHARMASK(*end)))
		end++;
	if (*end != '\0') {
		sprintf(buffer, "invalid literal for float(): %.200s", s);
		PyErr_SetString(PyExc_ValueError, buffer);
		return NULL;
	}
	else if (end != last) {
		PyErr_SetString(PyExc_ValueError,
				"null byte in argument for float()");
		return NULL;
	}
	if (x == 0.0) {
		/* See above -- may have been strtod being anal
		   about denorms. */
		PyFPE_START_PROTECT("atof", return NULL)
		x = atof(s);
		PyFPE_END_PROTECT(x)
		errno = 0;    /* whether atof ever set errno is undefined */
	}
	return PyFloat_FromDouble(x);
}

static void
float_dealloc(PyFloatObject *op)
{
	op->ob_type = (struct _typeobject *)free_list;
	free_list = op;
}

double
PyFloat_AsDouble(PyObject *op)
{
	PyNumberMethods *nb;
	PyFloatObject *fo;
	double val;
	
	if (op && PyFloat_Check(op))
		return PyFloat_AS_DOUBLE((PyFloatObject*) op);
	
	if (op == NULL || (nb = op->ob_type->tp_as_number) == NULL ||
	    nb->nb_float == NULL) {
		PyErr_BadArgument();
		return -1;
	}
	
	fo = (PyFloatObject*) (*nb->nb_float) (op);
	if (fo == NULL)
		return -1;
	if (!PyFloat_Check(fo)) {
		PyErr_SetString(PyExc_TypeError,
				"nb_float should return float object");
		return -1;
	}
	
	val = PyFloat_AS_DOUBLE(fo);
	Py_DECREF(fo);
	
	return val;
}

/* Methods */

void
PyFloat_AsStringEx(char *buf, PyFloatObject *v, int precision)
{
	register char *cp;
	/* Subroutine for float_repr and float_print.
	   We want float numbers to be recognizable as such,
	   i.e., they should contain a decimal point or an exponent.
	   However, %g may print the number as an integer;
	   in such cases, we append ".0" to the string. */
	sprintf(buf, "%.*g", precision, v->ob_fval);
	cp = buf;
	if (*cp == '-')
		cp++;
	for (; *cp != '\0'; cp++) {
		/* Any non-digit means it's not an integer;
		   this takes care of NAN and INF as well. */
		if (!isdigit(Py_CHARMASK(*cp)))
			break;
	}
	if (*cp == '\0') {
		*cp++ = '.';
		*cp++ = '0';
		*cp++ = '\0';
	}
}

/* Precisions used by repr() and str(), respectively.

   The repr() precision (17 significant decimal digits) is the minimal number
   that is guaranteed to have enough precision so that if the number is read
   back in the exact same binary value is recreated.  This is true for IEEE
   floating point by design, and also happens to work for all other modern
   hardware.

   The str() precision is chosen so that in most cases, the rounding noise
   created by various operations is suppressed, while giving plenty of
   precision for practical use.

*/

#define PREC_REPR	17
#define PREC_STR	12

void
PyFloat_AsString(char *buf, PyFloatObject *v)
{
	PyFloat_AsStringEx(buf, v, PREC_STR);
}

/* ARGSUSED */
static int
float_print(PyFloatObject *v, FILE *fp, int flags)
     /* flags -- not used but required by interface */
{
	char buf[100];
	PyFloat_AsStringEx(buf, v, flags&Py_PRINT_RAW ? PREC_STR : PREC_REPR);
	fputs(buf, fp);
	return 0;
}

static PyObject *
float_repr(PyFloatObject *v)
{
	char buf[100];
	PyFloat_AsStringEx(buf, v, PREC_REPR);
	return PyString_FromString(buf);
}

static PyObject *
float_str(PyFloatObject *v)
{
	char buf[100];
	PyFloat_AsStringEx(buf, v, PREC_STR);
	return PyString_FromString(buf);
}

static int
float_compare(PyFloatObject *v, PyFloatObject *w)
{
	double i = v->ob_fval;
	double j = w->ob_fval;
	return (i < j) ? -1 : (i > j) ? 1 : 0;
}


static long
float_hash(PyFloatObject *v)
{
	return _Py_HashDouble(v->ob_fval);
}

static PyObject *
float_add(PyFloatObject *v, PyFloatObject *w)
{
	double result;
	PyFPE_START_PROTECT("add", return 0)
	result = v->ob_fval + w->ob_fval;
	PyFPE_END_PROTECT(result)
	return PyFloat_FromDouble(result);
}

static PyObject *
float_sub(PyFloatObject *v, PyFloatObject *w)
{
	double result;
	PyFPE_START_PROTECT("subtract", return 0)
	result = v->ob_fval - w->ob_fval;
	PyFPE_END_PROTECT(result)
	return PyFloat_FromDouble(result);
}

static PyObject *
float_mul(PyFloatObject *v, PyFloatObject *w)
{
	double result;

	PyFPE_START_PROTECT("multiply", return 0)
	result = v->ob_fval * w->ob_fval;
	PyFPE_END_PROTECT(result)
	return PyFloat_FromDouble(result);
}

static PyObject *
float_div(PyFloatObject *v, PyFloatObject *w)
{
	double result;
	if (w->ob_fval == 0) {
		PyErr_SetString(PyExc_ZeroDivisionError, "float division");
		return NULL;
	}
	PyFPE_START_PROTECT("divide", return 0)
	result = v->ob_fval / w->ob_fval;
	PyFPE_END_PROTECT(result)
	return PyFloat_FromDouble(result);
}

static PyObject *
float_rem(PyFloatObject *v, PyFloatObject *w)
{
	double vx, wx;
	double mod;
	wx = w->ob_fval;
	if (wx == 0.0) {
		PyErr_SetString(PyExc_ZeroDivisionError, "float modulo");
		return NULL;
	}
	PyFPE_START_PROTECT("modulo", return 0)
	vx = v->ob_fval;
	mod = fmod(vx, wx);
	/* note: checking mod*wx < 0 is incorrect -- underflows to
	   0 if wx < sqrt(smallest nonzero double) */
	if (mod && ((wx < 0) != (mod < 0))) {
		mod += wx;
	}
	PyFPE_END_PROTECT(mod)
	return PyFloat_FromDouble(mod);
}

static PyObject *
float_divmod(PyFloatObject *v, PyFloatObject *w)
{
	double vx, wx;
	double div, mod, floordiv;
	wx = w->ob_fval;
	if (wx == 0.0) {
		PyErr_SetString(PyExc_ZeroDivisionError, "float divmod()");
		return NULL;
	}
	PyFPE_START_PROTECT("divmod", return 0)
	vx = v->ob_fval;
	mod = fmod(vx, wx);
	/* fmod is typically exact, so vx-mod is *mathematically* an
	   exact multiple of wx.  But this is fp arithmetic, and fp
	   vx - mod is an approximation; the result is that div may
	   not be an exact integral value after the division, although
	   it will always be very close to one.
	*/
	div = (vx - mod) / wx;
	/* note: checking mod*wx < 0 is incorrect -- underflows to
	   0 if wx < sqrt(smallest nonzero double) */
	if (mod && ((wx < 0) != (mod < 0))) {
		mod += wx;
		div -= 1.0;
	}
	/* snap quotient to nearest integral value */
	floordiv = floor(div);
	if (div - floordiv > 0.5)
		floordiv += 1.0;
	PyFPE_END_PROTECT(div)
	return Py_BuildValue("(dd)", floordiv, mod);
}

static double powu(double x, long n)
{
	double r = 1.;
	double p = x;
	long mask = 1;
	while (mask > 0 && n >= mask) {
		if (n & mask)
			r *= p;
		mask <<= 1;
		p *= p;
	}
	return r;
}

static PyObject *
float_pow(PyFloatObject *v, PyObject *w, PyFloatObject *z)
{
	double iv, iw, ix;
	long intw;
 /* XXX Doesn't handle overflows if z!=None yet; it may never do so :(
  * The z parameter is really only going to be useful for integers and
  * long integers.  Maybe something clever with logarithms could be done.
  * [AMK]
  */
	iv = v->ob_fval;
	iw = ((PyFloatObject *)w)->ob_fval;
	intw = (long)iw;

	/* Sort out special cases here instead of relying on pow() */
	if (iw == 0) { 		/* x**0 is 1, even 0**0 */
		PyFPE_START_PROTECT("pow", return NULL)
		if ((PyObject *)z != Py_None) {
			ix = fmod(1.0, z->ob_fval);
			if (ix != 0 && z->ob_fval < 0)
				ix += z->ob_fval;
		}
		else
			ix = 1.0;
		PyFPE_END_PROTECT(ix)
		return PyFloat_FromDouble(ix); 
	}
	if (iv == 0.0) {
		if (iw < 0.0) {
			PyErr_SetString(PyExc_ZeroDivisionError,
				   "0.0 to a negative power");
			return NULL;
		}
		return PyFloat_FromDouble(0.0);
	}

	if (iw == intw && intw > LONG_MIN) {
		/* ruled out LONG_MIN because -LONG_MIN isn't representable */
		errno = 0;
		PyFPE_START_PROTECT("pow", return NULL)
		if (intw > 0)
			ix = powu(iv, intw);
		else
			ix = 1./powu(iv, -intw);
		PyFPE_END_PROTECT(ix)
	}
	else {
		/* Sort out special cases here instead of relying on pow() */
		if (iv < 0.0) {
			PyErr_SetString(PyExc_ValueError,
				   "negative number to a float power");
			return NULL;
		}
		errno = 0;
		PyFPE_START_PROTECT("pow", return NULL)
		ix = pow(iv, iw);
		PyFPE_END_PROTECT(ix)
	}
	CHECK(ix);
	if (errno != 0) {
		/* XXX could it be another type of error? */
		PyErr_SetFromErrno(PyExc_OverflowError);
		return NULL;
	}
	if ((PyObject *)z != Py_None) {
		PyFPE_START_PROTECT("pow", return NULL)
		ix = fmod(ix, z->ob_fval);	/* XXX To Be Rewritten */
		if (ix != 0 &&
		    ((iv < 0 && z->ob_fval > 0) ||
		     (iv > 0 && z->ob_fval < 0)
		    )) {
		     ix += z->ob_fval;
		}
		PyFPE_END_PROTECT(ix)
	}
	return PyFloat_FromDouble(ix);
}

static PyObject *
float_neg(PyFloatObject *v)
{
	return PyFloat_FromDouble(-v->ob_fval);
}

static PyObject *
float_pos(PyFloatObject *v)
{
	Py_INCREF(v);
	return (PyObject *)v;
}

static PyObject *
float_abs(PyFloatObject *v)
{
	if (v->ob_fval < 0)
		return float_neg(v);
	else
		return float_pos(v);
}

static int
float_nonzero(PyFloatObject *v)
{
	return v->ob_fval != 0.0;
}

static int
float_coerce(PyObject **pv, PyObject **pw)
{
	if (PyInt_Check(*pw)) {
		long x = PyInt_AsLong(*pw);
		*pw = PyFloat_FromDouble((double)x);
		Py_INCREF(*pv);
		return 0;
	}
	else if (PyLong_Check(*pw)) {
		*pw = PyFloat_FromDouble(PyLong_AsDouble(*pw));
		Py_INCREF(*pv);
		return 0;
	}
	return 1; /* Can't do it */
}

static PyObject *
float_int(PyObject *v)
{
	double x = PyFloat_AsDouble(v);
	if (x < 0 ? (x = ceil(x)) < (double)LONG_MIN
	          : (x = floor(x)) > (double)LONG_MAX) {
		PyErr_SetString(PyExc_OverflowError,
				"float too large to convert");
		return NULL;
	}
	return PyInt_FromLong((long)x);
}

static PyObject *
float_long(PyObject *v)
{
	double x = PyFloat_AsDouble(v);
	return PyLong_FromDouble(x);
}

static PyObject *
float_float(PyObject *v)
{
	Py_INCREF(v);
	return v;
}


static PyNumberMethods float_as_number = {
	(binaryfunc)float_add, /*nb_add*/
	(binaryfunc)float_sub, /*nb_subtract*/
	(binaryfunc)float_mul, /*nb_multiply*/
	(binaryfunc)float_div, /*nb_divide*/
	(binaryfunc)float_rem, /*nb_remainder*/
	(binaryfunc)float_divmod, /*nb_divmod*/
	(ternaryfunc)float_pow, /*nb_power*/
	(unaryfunc)float_neg, /*nb_negative*/
	(unaryfunc)float_pos, /*nb_positive*/
	(unaryfunc)float_abs, /*nb_absolute*/
	(inquiry)float_nonzero, /*nb_nonzero*/
	0,		/*nb_invert*/
	0,		/*nb_lshift*/
	0,		/*nb_rshift*/
	0,		/*nb_and*/
	0,		/*nb_xor*/
	0,		/*nb_or*/
	(coercion)float_coerce, /*nb_coerce*/
	(unaryfunc)float_int, /*nb_int*/
	(unaryfunc)float_long, /*nb_long*/
	(unaryfunc)float_float, /*nb_float*/
	0,		/*nb_oct*/
	0,		/*nb_hex*/
};

PyTypeObject PyFloat_Type = {
	PyObject_HEAD_INIT(&PyType_Type)
	0,
	"float",
	sizeof(PyFloatObject),
	0,
	(destructor)float_dealloc, /*tp_dealloc*/
	(printfunc)float_print, /*tp_print*/
	0,			/*tp_getattr*/
	0,			/*tp_setattr*/
	(cmpfunc)float_compare, /*tp_compare*/
	(reprfunc)float_repr,	/*tp_repr*/
	&float_as_number,	/*tp_as_number*/
	0,			/*tp_as_sequence*/
	0,			/*tp_as_mapping*/
	(hashfunc)float_hash,	/*tp_hash*/
        0,			/*tp_call*/
        (reprfunc)float_str,	/*tp_str*/
};

void
PyFloat_Fini(void)
{
	PyFloatObject *p;
	PyFloatBlock *list, *next;
	int i;
	int bc, bf;	/* block count, number of freed blocks */
	int frem, fsum;	/* remaining unfreed floats per block, total */

	bc = 0;
	bf = 0;
	fsum = 0;
	list = block_list;
	block_list = NULL;
	free_list = NULL;
	while (list != NULL) {
		bc++;
		frem = 0;
		for (i = 0, p = &list->objects[0];
		     i < N_FLOATOBJECTS;
		     i++, p++) {
			if (PyFloat_Check(p) && p->ob_refcnt != 0)
				frem++;
		}
		next = list->next;
		if (frem) {
			list->next = block_list;
			block_list = list;
			for (i = 0, p = &list->objects[0];
			     i < N_FLOATOBJECTS;
			     i++, p++) {
				if (!PyFloat_Check(p) || p->ob_refcnt == 0) {
					p->ob_type = (struct _typeobject *)
						free_list;
					free_list = p;
				}
			}
		}
		else {
			PyMem_FREE(list); /* XXX PyObject_FREE ??? */
			bf++;
		}
		fsum += frem;
		list = next;
	}
	if (!Py_VerboseFlag)
		return;
	fprintf(stderr, "# cleanup floats");
	if (!fsum) {
		fprintf(stderr, "\n");
	}
	else {
		fprintf(stderr,
			": %d unfreed float%s in %d out of %d block%s\n",
			fsum, fsum == 1 ? "" : "s",
			bc - bf, bc, bc == 1 ? "" : "s");
	}
	if (Py_VerboseFlag > 1) {
		list = block_list;
		while (list != NULL) {
			for (i = 0, p = &list->objects[0];
			     i < N_FLOATOBJECTS;
			     i++, p++) {
				if (PyFloat_Check(p) && p->ob_refcnt != 0) {
					char buf[100];
					PyFloat_AsString(buf, p);
					fprintf(stderr,
			     "#   <float at %p, refcnt=%d, val=%s>\n",
						p, p->ob_refcnt, buf);
				}
			}
			list = list->next;
		}
	}
}
