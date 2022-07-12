/* Hey Emacs, this is -*-C-*- 
 ******************************************************************************
 * linuxaudiodev.c -- Linux audio device for python.
 * 
 * Author          : Peter Bosch
 * Created On      : Thu Mar  2 21:10:33 2000
 * Last Modified By: Peter Bosch
 * Last Modified On: Fri Mar 24 11:27:00 2000
 * Status          : Unknown, Use with caution!
 * 
 * Unless other notices are present in any part of this file
 * explicitly claiming copyrights for other people and/or 
 * organizations, the contents of this file is fully copyright 
 * (C) 2000 Peter Bosch, all rights reserved.
 ******************************************************************************
 */

#include "Python.h"
#include "structmember.h"

#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif

#ifdef HAVE_FCNTL_H
#include <fcntl.h>
#else
#define O_RDONLY 00
#define O_WRONLY 01
#endif


#include <sys/ioctl.h>
#if defined(linux)
#include <linux/soundcard.h>

typedef unsigned long uint32_t;

#elif defined(__FreeBSD__)
#include <machine/soundcard.h>

#ifndef SNDCTL_DSP_CHANNELS
#define SNDCTL_DSP_CHANNELS SOUND_PCM_WRITE_CHANNELS
#endif

#endif

typedef struct {
    PyObject_HEAD;
    int		x_fd;		/* The open file */
    int         x_mode;           /* file mode */
    int		x_icount;	/* Input count */
    int		x_ocount;	/* Output count */
    uint32_t	x_afmts;	/* Audio formats supported by hardware*/
} lad_t;

/* XXX several format defined in soundcard.h are not supported,
   including _NE (native endian) options and S32 options
*/

static struct {
    int		a_bps;
    uint32_t	a_fmt;
    char       *a_name;
} audio_types[] = {
    {  8, 	AFMT_MU_LAW, "Logarithmic mu-law audio" },
    {  8, 	AFMT_A_LAW,  "Logarithmic A-law audio" },
    {  8,	AFMT_U8,     "Standard unsigned 8-bit audio" },
    {  8, 	AFMT_S8,     "Standard signed 8-bit audio" },
    { 16, 	AFMT_U16_BE, "Big-endian 16-bit unsigned audio" },
    { 16, 	AFMT_U16_LE, "Little-endian 16-bit unsigned audio" },
    { 16, 	AFMT_S16_BE, "Big-endian 16-bit signed audio" },
    { 16, 	AFMT_S16_LE, "Little-endian 16-bit signed audio" },
    { 16, 	AFMT_S16_NE, "Native-endian 16-bit signed audio" },
};

static int n_audio_types = sizeof(audio_types) / sizeof(audio_types[0]);

staticforward PyTypeObject Ladtype;

static PyObject *LinuxAudioError;

static lad_t *
newladobject(PyObject *arg)
{
    lad_t *xp;
    int fd, afmts, imode;
    char *mode;
    char *basedev;

    /* Check arg for r/w/rw */
    if (!PyArg_ParseTuple(arg, "s:open", &mode)) return NULL;
    if (strcmp(mode, "r") == 0)
        imode = O_RDONLY;
    else if (strcmp(mode, "w") == 0)
        imode = O_WRONLY;
    else {
        PyErr_SetString(LinuxAudioError, "Mode should be one of 'r', or 'w'");
        return NULL;
    }

    /* Open the correct device.  The base device name comes from the
     * AUDIODEV environment variable first, then /dev/dsp.  The
     * control device tacks "ctl" onto the base device name.
     * 
     * Note that the only difference between /dev/audio and /dev/dsp
     * is that the former uses logarithmic mu-law encoding and the
     * latter uses 8-bit unsigned encoding.
     */

    basedev = getenv("AUDIODEV");
    if (!basedev)
        basedev = "/dev/dsp";

    if ((fd = open(basedev, imode)) == -1) {
        PyErr_SetFromErrnoWithFilename(LinuxAudioError, basedev);
        return NULL;
    }
    if (imode == O_WRONLY && ioctl(fd, SNDCTL_DSP_NONBLOCK, NULL) == -1) {
        PyErr_SetFromErrnoWithFilename(LinuxAudioError, basedev);
        return NULL;
    }
    if (ioctl(fd, SNDCTL_DSP_GETFMTS, &afmts) == -1) {
        PyErr_SetFromErrnoWithFilename(LinuxAudioError, basedev);
        return NULL;
    }
    /* Create and initialize the object */
    if ((xp = PyObject_New(lad_t, &Ladtype)) == NULL) {
        close(fd);
        return NULL;
    }
    xp->x_fd = fd;
    xp->x_mode = imode;
    xp->x_icount = xp->x_ocount = 0;
    xp->x_afmts  = afmts;
    return xp;
}

static void
lad_dealloc(lad_t *xp)
{
    /* if already closed, don't reclose it */
    if (xp->x_fd != -1)
	close(xp->x_fd);
    PyObject_Del(xp);
}

static PyObject *
lad_read(lad_t *self, PyObject *args)
{
    int size, count;
    char *cp;
    PyObject *rv;
	
    if (!PyArg_ParseTuple(args, "i:read", &size))
        return NULL;
    rv = PyString_FromStringAndSize(NULL, size);
    if (rv == NULL)
        return NULL;
    cp = PyString_AS_STRING(rv);
    if ((count = read(self->x_fd, cp, size)) < 0) {
        PyErr_SetFromErrno(LinuxAudioError);
        Py_DECREF(rv);
        return NULL;
    }
    self->x_icount += count;
    if (_PyString_Resize(&rv, count) == -1)
	return NULL;
    return rv;
}

static PyObject *
lad_write(lad_t *self, PyObject *args)
{
    char *cp;
    int rv, size;
	
    if (!PyArg_ParseTuple(args, "s#:write", &cp, &size)) 
	return NULL;

    while (size > 0) {
        if ((rv = write(self->x_fd, cp, size)) == -1) {
            PyErr_SetFromErrno(LinuxAudioError);
            return NULL;
        }
        self->x_ocount += rv;
        size -= rv;
        cp += rv;
    }
    Py_INCREF(Py_None);
    return Py_None;
}

static PyObject *
lad_close(lad_t *self, PyObject *args)
{
    if (!PyArg_ParseTuple(args, ":close"))
	return NULL;

    if (self->x_fd >= 0) {
        close(self->x_fd);
        self->x_fd = -1;
    }
    Py_INCREF(Py_None);
    return Py_None;
}

static PyObject *
lad_fileno(lad_t *self, PyObject *args)
{
    if (!PyArg_ParseTuple(args, ":fileno")) 
	return NULL;
    return PyInt_FromLong(self->x_fd);
}

static PyObject *
lad_setparameters(lad_t *self, PyObject *args)
{
    int rate, ssize, nchannels, n, fmt, emulate=0;

    if (!PyArg_ParseTuple(args, "iiii|i:setparameters",
                          &rate, &ssize, &nchannels, &fmt, &emulate))
        return NULL;
  
    if (rate < 0) {
	PyErr_Format(PyExc_ValueError, "expected rate >= 0, not %d",
		     rate); 
	return NULL;
    }
    if (ssize < 0) {
	PyErr_Format(PyExc_ValueError, "expected sample size >= 0, not %d",
		     ssize);
	return NULL;
    }
    if (nchannels != 1 && nchannels != 2) {
	PyErr_Format(PyExc_ValueError, "nchannels must be 1 or 2, not %d",
		     nchannels);
	return NULL;
    }

    if (ioctl(self->x_fd, SNDCTL_DSP_SPEED, &rate) == -1) {
        PyErr_SetFromErrno(LinuxAudioError);
        return NULL;
    }
    if (ioctl(self->x_fd, SNDCTL_DSP_CHANNELS, &nchannels) == -1) {
        PyErr_SetFromErrno(LinuxAudioError);
        return NULL;
    }

    for (n = 0; n < n_audio_types; n++)
        if (fmt == audio_types[n].a_fmt)
            break;
    if (n == n_audio_types) {
	PyErr_Format(PyExc_ValueError, "unknown audio encoding: %d", fmt);
	return NULL;
    }
    if (audio_types[n].a_bps != ssize) {
	PyErr_Format(PyExc_ValueError, 
		     "sample size %d expected for %s: %d received",
		     audio_types[n].a_bps, audio_types[n].a_name, ssize);
	return NULL;
    }

    if (emulate == 0) {
	if ((self->x_afmts & audio_types[n].a_fmt) == 0) {
	    PyErr_Format(PyExc_ValueError, 
			 "format not supported by device: %s",
			 audio_types[n].a_name);
	    return NULL;
	}
    }
    if (ioctl(self->x_fd, SNDCTL_DSP_SETFMT, 
	      &audio_types[n].a_fmt) == -1) {
        PyErr_SetFromErrno(LinuxAudioError);
        return NULL;
    }

    Py_INCREF(Py_None);
    return Py_None;
}

static int
_ssize(lad_t *self, int *nchannels, int *ssize)
{
    int fmt;

    fmt = 0;
    if (ioctl(self->x_fd, SNDCTL_DSP_SETFMT, &fmt) < 0) 
        return -errno;

    switch (fmt) {
    case AFMT_MU_LAW:
    case AFMT_A_LAW:
    case AFMT_U8:
    case AFMT_S8:
        *ssize = sizeof(char);
        break;
    case AFMT_S16_LE:
    case AFMT_S16_BE:
    case AFMT_U16_LE:
    case AFMT_U16_BE:
        *ssize = sizeof(short);
        break;
    case AFMT_MPEG:
    case AFMT_IMA_ADPCM:
    default:
        return -EOPNOTSUPP;
    }
    *nchannels = 0;
    if (ioctl(self->x_fd, SNDCTL_DSP_CHANNELS, nchannels) < 0)
        return -errno;
    return 0;
}


/* bufsize returns the size of the hardware audio buffer in number 
   of samples */
static PyObject *
lad_bufsize(lad_t *self, PyObject *args)
{
    audio_buf_info ai;
    int nchannels, ssize;

    if (!PyArg_ParseTuple(args, ":bufsize")) return NULL;

    if (_ssize(self, &nchannels, &ssize) < 0) {
        PyErr_SetFromErrno(LinuxAudioError);
        return NULL;
    }
    if (ioctl(self->x_fd, SNDCTL_DSP_GETOSPACE, &ai) < 0) {
        PyErr_SetFromErrno(LinuxAudioError);
        return NULL;
    }
    return PyInt_FromLong((ai.fragstotal * ai.fragsize) / (nchannels * ssize));
}

/* obufcount returns the number of samples that are available in the 
   hardware for playing */
static PyObject *
lad_obufcount(lad_t *self, PyObject *args)
{
    audio_buf_info ai;
    int nchannels, ssize;

    if (!PyArg_ParseTuple(args, ":obufcount"))
        return NULL;

    if (_ssize(self, &nchannels, &ssize) < 0) {
        PyErr_SetFromErrno(LinuxAudioError);
        return NULL;
    }
    if (ioctl(self->x_fd, SNDCTL_DSP_GETOSPACE, &ai) < 0) {
        PyErr_SetFromErrno(LinuxAudioError);
        return NULL;
    }
    return PyInt_FromLong((ai.fragstotal * ai.fragsize - ai.bytes) / 
                          (ssize * nchannels));
}

/* obufcount returns the number of samples that can be played without
   blocking */
static PyObject *
lad_obuffree(lad_t *self, PyObject *args)
{
    audio_buf_info ai;
    int nchannels, ssize;

    if (!PyArg_ParseTuple(args, ":obuffree"))
        return NULL;

    if (_ssize(self, &nchannels, &ssize) < 0) {
        PyErr_SetFromErrno(LinuxAudioError);
        return NULL;
    }
    if (ioctl(self->x_fd, SNDCTL_DSP_GETOSPACE, &ai) < 0) {
        PyErr_SetFromErrno(LinuxAudioError);
        return NULL;
    }
    return PyInt_FromLong(ai.bytes / (ssize * nchannels));
}

/* Flush the device */
static PyObject *
lad_flush(lad_t *self, PyObject *args)
{
    if (!PyArg_ParseTuple(args, ":flush")) return NULL;

    if (ioctl(self->x_fd, SNDCTL_DSP_SYNC, NULL) == -1) {
        PyErr_SetFromErrno(LinuxAudioError);
        return NULL;
    }
    Py_INCREF(Py_None);
    return Py_None;
}

static PyObject *
lad_getptr(lad_t *self, PyObject *args)
{
    count_info info;
    int req;

    if (!PyArg_ParseTuple(args, ":getptr"))
	return NULL;
    
    if (self->x_mode == O_RDONLY)
	req = SNDCTL_DSP_GETIPTR;
    else
	req = SNDCTL_DSP_GETOPTR;
    if (ioctl(self->x_fd, req, &info) == -1) {
        PyErr_SetFromErrno(LinuxAudioError);
        return NULL;
    }
    return Py_BuildValue("iii", info.bytes, info.blocks, info.ptr);
}

static PyMethodDef lad_methods[] = {
    { "read",		(PyCFunction)lad_read, METH_VARARGS },
    { "write",		(PyCFunction)lad_write, METH_VARARGS },
    { "setparameters",	(PyCFunction)lad_setparameters, METH_VARARGS },
    { "bufsize",	(PyCFunction)lad_bufsize, METH_VARARGS },
    { "obufcount",	(PyCFunction)lad_obufcount, METH_VARARGS },
    { "obuffree",	(PyCFunction)lad_obuffree, METH_VARARGS },
    { "flush",		(PyCFunction)lad_flush, METH_VARARGS },
    { "close",		(PyCFunction)lad_close, METH_VARARGS },
    { "fileno",     	(PyCFunction)lad_fileno, METH_VARARGS },
    { "getptr",         (PyCFunction)lad_getptr, METH_VARARGS },
    { NULL,		NULL}		/* sentinel */
};

static PyObject *
lad_getattr(lad_t *xp, char *name)
{
    return Py_FindMethod(lad_methods, (PyObject *)xp, name);
}

static PyTypeObject Ladtype = {
    PyObject_HEAD_INIT(&PyType_Type)
    0,				/*ob_size*/
    "linux_audio_device",	/*tp_name*/
    sizeof(lad_t),		/*tp_size*/
    0,				/*tp_itemsize*/
    /* methods */
    (destructor)lad_dealloc,	/*tp_dealloc*/
    0,				/*tp_print*/
    (getattrfunc)lad_getattr,	/*tp_getattr*/
    0,				/*tp_setattr*/
    0,				/*tp_compare*/
    0,				/*tp_repr*/
};

static PyObject *
ladopen(PyObject *self, PyObject *args)
{
    return (PyObject *)newladobject(args);
}

static PyMethodDef linuxaudiodev_methods[] = {
    { "open", ladopen, METH_VARARGS },
    { 0, 0 },
};

void
initlinuxaudiodev(void)
{
    PyObject *m;
  
    m = Py_InitModule("linuxaudiodev", linuxaudiodev_methods);

    LinuxAudioError = PyErr_NewException("linuxaudiodev.error", NULL, NULL);
    if (LinuxAudioError)
	PyModule_AddObject(m, "error", LinuxAudioError);

    if (PyModule_AddIntConstant(m, "AFMT_MU_LAW", (long)AFMT_MU_LAW) == -1)
	return;
    if (PyModule_AddIntConstant(m, "AFMT_A_LAW", (long)AFMT_A_LAW) == -1)
	return;
    if (PyModule_AddIntConstant(m, "AFMT_U8", (long)AFMT_U8) == -1)
	return;
    if (PyModule_AddIntConstant(m, "AFMT_S8", (long)AFMT_S8) == -1)
	return;
    if (PyModule_AddIntConstant(m, "AFMT_U16_BE", (long)AFMT_U16_BE) == -1)
	return;
    if (PyModule_AddIntConstant(m, "AFMT_U16_LE", (long)AFMT_U16_LE) == -1)
	return;
    if (PyModule_AddIntConstant(m, "AFMT_S16_BE", (long)AFMT_S16_BE) == -1)
	return;
    if (PyModule_AddIntConstant(m, "AFMT_S16_LE", (long)AFMT_S16_LE) == -1)
	return;
    if (PyModule_AddIntConstant(m, "AFMT_S16_NE", (long)AFMT_S16_NE) == -1)
	return;

    return;
}
