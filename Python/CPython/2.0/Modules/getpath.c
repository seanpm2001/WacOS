
/* Return the initial module search path. */

#include "Python.h"
#include "osdefs.h"

#include <sys/types.h>
#include <sys/stat.h>
#include <string.h>

#if HAVE_UNISTD_H
#include <unistd.h>
#endif /* HAVE_UNISTD_H */

#ifdef WITH_NEXT_FRAMEWORK
#include <mach-o/dyld.h>
#endif

/* Search in some common locations for the associated Python libraries.
 *
 * Two directories must be found, the platform independent directory
 * (prefix), containing the common .py and .pyc files, and the platform
 * dependent directory (exec_prefix), containing the shared library
 * modules.  Note that prefix and exec_prefix can be the same directory,
 * but for some installations, they are different.
 *
 * Py_GetPath() carries out separate searches for prefix and exec_prefix.
 * Each search tries a number of different locations until a ``landmark''
 * file or directory is found.  If no prefix or exec_prefix is found, a
 * warning message is issued and the preprocessor defined PREFIX and
 * EXEC_PREFIX are used (even though they will not work); python carries on
 * as best as is possible, but most imports will fail.
 *
 * Before any searches are done, the location of the executable is
 * determined.  If argv[0] has one or more slashs in it, it is used
 * unchanged.  Otherwise, it must have been invoked from the shell's path,
 * so we search $PATH for the named executable and use that.  If the
 * executable was not found on $PATH (or there was no $PATH environment
 * variable), the original argv[0] string is used.
 *
 * Next, the executable location is examined to see if it is a symbolic
 * link.  If so, the link is chased (correctly interpreting a relative
 * pathname if one is found) and the directory of the link target is used.
 *
 * Finally, argv0_path is set to the directory containing the executable
 * (i.e. the last component is stripped).
 *
 * With argv0_path in hand, we perform a number of steps.  The same steps
 * are performed for prefix and for exec_prefix, but with a different
 * landmark.
 *
 * Step 1. Are we running python out of the build directory?  This is
 * checked by looking for a different kind of landmark relative to
 * argv0_path.  For prefix, the landmark's path is derived from the VPATH
 * preprocessor variable (taking into account that its value is almost, but
 * not quite, what we need).  For exec_prefix, the landmark is
 * Modules/Setup.  If the landmark is found, we're done.
 *
 * For the remaining steps, the prefix landmark will always be
 * lib/python$VERSION/os.py and the exec_prefix will always be
 * lib/python$VERSION/lib-dynload, where $VERSION is Python's version
 * number as supplied by the Makefile.  Note that this means that no more
 * build directory checking is performed; if the first step did not find
 * the landmarks, the assumption is that python is running from an
 * installed setup.
 *
 * Step 2. See if the $PYTHONHOME environment variable points to the
 * installed location of the Python libraries.  If $PYTHONHOME is set, then
 * it points to prefix and exec_prefix.  $PYTHONHOME can be a single
 * directory, which is used for both, or the prefix and exec_prefix
 * directories separated by a colon.
 *
 * Step 3. Try to find prefix and exec_prefix relative to argv0_path,
 * backtracking up the path until it is exhausted.  This is the most common
 * step to succeed.  Note that if prefix and exec_prefix are different,
 * exec_prefix is more likely to be found; however if exec_prefix is a
 * subdirectory of prefix, both will be found.
 *
 * Step 4. Search the directories pointed to by the preprocessor variables
 * PREFIX and EXEC_PREFIX.  These are supplied by the Makefile but can be
 * passed in as options to the configure script.
 *
 * That's it!
 *
 * Well, almost.  Once we have determined prefix and exec_prefix, the
 * preprocessor variable PYTHONPATH is used to construct a path.  Each
 * relative path on PYTHONPATH is prefixed with prefix.  Then the directory
 * containing the shared library modules is appended.  The environment
 * variable $PYTHONPATH is inserted in front of it all.  Finally, the
 * prefix and exec_prefix globals are tweaked so they reflect the values
 * expected by other code, by stripping the "lib/python$VERSION/..." stuff
 * off.  If either points to the build directory, the globals are reset to
 * the corresponding preprocessor variables (so sys.prefix will reflect the
 * installation location, even though sys.path points into the build
 * directory).  This seems to make more sense given that currently the only
 * known use of sys.prefix and sys.exec_prefix is for the ILU installation
 * process to find the installed Python tree.
 */

#ifndef VERSION
#define VERSION "2.0"
#endif

#ifndef VPATH
#define VPATH "."
#endif

#ifndef PREFIX
#define PREFIX "/usr/local"
#endif

#ifndef EXEC_PREFIX
#define EXEC_PREFIX PREFIX
#endif

#ifndef PYTHONPATH
/* I know this isn't K&R C, but the Makefile specifies it anyway */
#define PYTHONPATH PREFIX "/lib/python" VERSION ":" \
	      EXEC_PREFIX "/lib/python" VERSION "/lib-dynload"
#endif

#ifndef LANDMARK
#define LANDMARK "os.py"
#endif
 
static char prefix[MAXPATHLEN+1];
static char exec_prefix[MAXPATHLEN+1];
static char progpath[MAXPATHLEN+1];
static char *module_search_path = NULL;
static char lib_python[] = "lib/python" VERSION;

static void
reduce(char *dir)
{
    size_t i = strlen(dir);
    while (i > 0 && dir[i] != SEP)
        --i;
    dir[i] = '\0';
}


#ifndef S_ISREG
#define S_ISREG(x) (((x) & S_IFMT) == S_IFREG)
#endif

#ifndef S_ISDIR
#define S_ISDIR(x) (((x) & S_IFMT) == S_IFDIR)
#endif

static int
isfile(char *filename)		/* Is file, not directory */
{
    struct stat buf;
    if (stat(filename, &buf) != 0)
        return 0;
    if (!S_ISREG(buf.st_mode))
        return 0;
    return 1;
}


static int
ismodule(char *filename)	/* Is module -- check for .pyc/.pyo too */
{
    if (isfile(filename))
        return 1;

    /* Check for the compiled version of prefix. */
    if (strlen(filename) < MAXPATHLEN) {
        strcat(filename, Py_OptimizeFlag ? "o" : "c");
        if (isfile(filename))
            return 1;
    }
    return 0;
}


static int
isxfile(char *filename)		/* Is executable file */
{
    struct stat buf;
    if (stat(filename, &buf) != 0)
        return 0;
    if (!S_ISREG(buf.st_mode))
        return 0;
    if ((buf.st_mode & 0111) == 0)
        return 0;
    return 1;
}


static int
isdir(char *filename)			/* Is directory */
{
    struct stat buf;
    if (stat(filename, &buf) != 0)
        return 0;
    if (!S_ISDIR(buf.st_mode))
        return 0;
    return 1;
}


/* joinpath requires that any buffer argument passed to it has at
   least MAXPATHLEN + 1 bytes allocated.  If this requirement is met,
   it guarantees that it will never overflow the buffer.  If stuff
   is too long, buffer will contain a truncated copy of stuff.
*/
static void
joinpath(char *buffer, char *stuff)
{
    size_t n, k;
    if (stuff[0] == SEP)
        n = 0;
    else {
        n = strlen(buffer);
        if (n > 0 && buffer[n-1] != SEP && n < MAXPATHLEN)
            buffer[n++] = SEP;
    }
    k = strlen(stuff);
    if (n + k > MAXPATHLEN)
        k = MAXPATHLEN - n;
    strncpy(buffer+n, stuff, k);
    buffer[n+k] = '\0';
}

/* init_path_from_argv0 requirs that path be allocated at least
   MAXPATHLEN + 1 bytes and that argv0_path be no more than MAXPATHLEN
   bytes. 
*/
static void
init_path_from_argv0(char *path, char *argv0_path)
{
    if (argv0_path[0] == '/')
	strcpy(path, argv0_path);
    else if (argv0_path[0] == '.') {
	getcwd(path, MAXPATHLEN);
	if (argv0_path[1] == '/') 
	    joinpath(path, argv0_path + 2);
	else
	    joinpath(path, argv0_path);
    }
    else {
	getcwd(path, MAXPATHLEN);
	joinpath(path, argv0_path);
    }
}

/* search_for_prefix requires that argv0_path be no more than MAXPATHLEN 
   bytes long.
*/
static int
search_for_prefix(char *argv0_path, char *home)
{
    size_t n;
    char *vpath;

    /* If PYTHONHOME is set, we believe it unconditionally */
    if (home) {
        char *delim;
        strncpy(prefix, home, MAXPATHLEN);
        delim = strchr(prefix, DELIM);
        if (delim)
            *delim = '\0';
        joinpath(prefix, lib_python);
        joinpath(prefix, LANDMARK);
        return 1;
    }

    /* Check to see if argv[0] is in the build directory */
    strcpy(prefix, argv0_path);
    joinpath(prefix, "Modules/Setup");
    if (isfile(prefix)) {
        /* Check VPATH to see if argv0_path is in the build directory.
         * Complication: the VPATH passed in is relative to the
         * Modules build directory and points to the Modules source
         * directory; we need it relative to the build tree and
         * pointing to the source tree.  Solution: chop off a leading
         * ".." (but only if it's there -- it could be an absolute
         * path) and chop off the final component (assuming it's
         * "Modules").
         */
        vpath = VPATH;
        if (vpath[0] == '.' && vpath[1] == '.' && vpath[2] == '/')
            vpath += 3;
        strcpy(prefix, argv0_path);
        joinpath(prefix, vpath);
        reduce(prefix);
        joinpath(prefix, "Lib");
        joinpath(prefix, LANDMARK);
        if (ismodule(prefix))
            return -1;
    }

    /* Search from argv0_path, until root is found */
    init_path_from_argv0(prefix, argv0_path);
    do {
        n = strlen(prefix);
        joinpath(prefix, lib_python);
        joinpath(prefix, LANDMARK);
        if (ismodule(prefix))
            return 1;
        prefix[n] = '\0';
        reduce(prefix);
    } while (prefix[0]);

    /* Look at configure's PREFIX */
    strncpy(prefix, PREFIX, MAXPATHLEN);
    joinpath(prefix, lib_python);
    joinpath(prefix, LANDMARK);
    if (ismodule(prefix))
        return 1;

    /* Fail */
    return 0;
}


/* search_for_exec_prefix requires that argv0_path be no more than
   MAXPATHLEN bytes long.  
*/
static int
search_for_exec_prefix(char *argv0_path, char *home)
{
    size_t n;

    /* If PYTHONHOME is set, we believe it unconditionally */
    if (home) {
        char *delim;
        delim = strchr(home, DELIM);
        if (delim)
            strncpy(exec_prefix, delim+1, MAXPATHLEN);
        else
            strncpy(exec_prefix, home, MAXPATHLEN);
        joinpath(exec_prefix, lib_python);
        joinpath(exec_prefix, "lib-dynload");
        return 1;
    }

    /* Check to see if argv[0] is in the build directory */
    strcpy(exec_prefix, argv0_path);
    joinpath(exec_prefix, "Modules/Setup");
    if (isfile(exec_prefix)) {
        reduce(exec_prefix);
        return -1;
    }

    /* Search from argv0_path, until root is found */
    init_path_from_argv0(exec_prefix, argv0_path);
    do {
        n = strlen(exec_prefix);
        joinpath(exec_prefix, lib_python);
        joinpath(exec_prefix, "lib-dynload");
        if (isdir(exec_prefix))
            return 1;
        exec_prefix[n] = '\0';
        reduce(exec_prefix);
    } while (exec_prefix[0]);

    /* Look at configure's EXEC_PREFIX */
    strncpy(exec_prefix, EXEC_PREFIX, MAXPATHLEN);
    joinpath(exec_prefix, lib_python);
    joinpath(exec_prefix, "lib-dynload");
    if (isdir(exec_prefix))
        return 1;

    /* Fail */
    return 0;
}


static void
calculate_path(void)
{
    extern char *Py_GetProgramName(void);

    static char delimiter[2] = {DELIM, '\0'};
    static char separator[2] = {SEP, '\0'};
    char *pythonpath = PYTHONPATH;
    char *rtpypath = getenv("PYTHONPATH");
    char *home = Py_GetPythonHome();
    char *path = getenv("PATH");
    char *prog = Py_GetProgramName();
    char argv0_path[MAXPATHLEN+1];
    int pfound, efound; /* 1 if found; -1 if found build directory */
    char *buf;
    size_t bufsz;
    size_t prefixsz;
    char *defpath = pythonpath;
#ifdef WITH_NEXT_FRAMEWORK
    NSModule pythonModule;
#endif
	
#ifdef WITH_NEXT_FRAMEWORK
    /* XXX Need to check this code for buffer overflows */
    pythonModule = NSModuleForSymbol(NSLookupAndBindSymbol("_Py_Initialize"));
    /* Use dylib functions to find out where the framework was loaded from */
    buf = NSLibraryNameForModule(pythonModule);
    if (buf != NULL) {
        /* We're in a framework. */
        strcpy(progpath, buf);

        /* Frameworks have support for versioning */
        strcpy(lib_python, "lib");
    }
    else {
        /* If we're not in a framework, fall back to the old way
           (even though NSNameOfModule() probably does the same thing.) */
#endif
	
	/* If there is no slash in the argv0 path, then we have to
	 * assume python is on the user's $PATH, since there's no
	 * other way to find a directory to start the search from.  If
	 * $PATH isn't exported, you lose.
	 */
	if (strchr(prog, SEP))
            strncpy(progpath, prog, MAXPATHLEN);
	else if (path) {
	    int bufspace = MAXPATHLEN;
            while (1) {
                char *delim = strchr(path, DELIM);

                if (delim) {
                    size_t len = delim - path;
		    if (len > bufspace)
			len = bufspace;
                    strncpy(progpath, path, len);
                    *(progpath + len) = '\0';
		    bufspace -= len;
                }
                else
                    strncpy(progpath, path, bufspace);

                joinpath(progpath, prog);
                if (isxfile(progpath))
                    break;

                if (!delim) {
                    progpath[0] = '\0';
                    break;
                }
                path = delim + 1;
            }
	}
	else
            progpath[0] = '\0';
#ifdef WITH_NEXT_FRAMEWORK
    }
#endif

    strncpy(argv0_path, progpath, MAXPATHLEN);
	
#if HAVE_READLINK
    {
        char tmpbuffer[MAXPATHLEN+1];
        int linklen = readlink(progpath, tmpbuffer, MAXPATHLEN);
        while (linklen != -1) {
            /* It's not null terminated! */
            tmpbuffer[linklen] = '\0';
            if (tmpbuffer[0] == SEP)
		/* tmpbuffer should never be longer than MAXPATHLEN,
		   but extra check does not hurt */
                strncpy(argv0_path, tmpbuffer, MAXPATHLEN);
            else {
                /* Interpret relative to progpath */
                reduce(argv0_path);
                joinpath(argv0_path, tmpbuffer);
            }
            linklen = readlink(argv0_path, tmpbuffer, MAXPATHLEN);
        }
    }
#endif /* HAVE_READLINK */

    reduce(argv0_path);
    /* At this point, argv0_path is guaranteed to be less than
       MAXPATHLEN bytes long.
    */

    if (!(pfound = search_for_prefix(argv0_path, home))) {
        if (!Py_FrozenFlag)
            fprintf(stderr,
                    "Could not find platform independent libraries <prefix>\n");
        strncpy(prefix, PREFIX, MAXPATHLEN);
        joinpath(prefix, lib_python);
    }
    else
        reduce(prefix);
	
    if (!(efound = search_for_exec_prefix(argv0_path, home))) {
        if (!Py_FrozenFlag)
            fprintf(stderr,
                    "Could not find platform dependent libraries <exec_prefix>\n");
        strncpy(exec_prefix, EXEC_PREFIX, MAXPATHLEN);
        joinpath(exec_prefix, "lib/lib-dynload");
    }
    /* If we found EXEC_PREFIX do *not* reduce it!  (Yet.) */

    if ((!pfound || !efound) && !Py_FrozenFlag)
        fprintf(stderr,
                "Consider setting $PYTHONHOME to <prefix>[:<exec_prefix>]\n");

    /* Calculate size of return buffer.
     */
    bufsz = 0;

    if (rtpypath)
        bufsz += strlen(rtpypath) + 1;

    prefixsz = strlen(prefix) + 1;

    while (1) {
        char *delim = strchr(defpath, DELIM);

        if (defpath[0] != SEP)
            /* Paths are relative to prefix */
            bufsz += prefixsz;

        if (delim)
            bufsz += delim - defpath + 1;
        else {
            bufsz += strlen(defpath) + 1;
            break;
        }
        defpath = delim + 1;
    }

    bufsz += strlen(exec_prefix) + 1;

    /* This is the only malloc call in this file */
    buf = PyMem_Malloc(bufsz);

    if (buf == NULL) {
        /* We can't exit, so print a warning and limp along */
        fprintf(stderr, "Not enough memory for dynamic PYTHONPATH.\n");
        fprintf(stderr, "Using default static PYTHONPATH.\n");
        module_search_path = PYTHONPATH;
    }
    else {
        /* Run-time value of $PYTHONPATH goes first */
        if (rtpypath) {
            strcpy(buf, rtpypath);
            strcat(buf, delimiter);
        }
        else
            buf[0] = '\0';

        /* Next goes merge of compile-time $PYTHONPATH with
         * dynamically located prefix.
         */
        defpath = pythonpath;
        while (1) {
            char *delim = strchr(defpath, DELIM);

            if (defpath[0] != SEP) {
                strcat(buf, prefix);
                strcat(buf, separator);
            }

            if (delim) {
                size_t len = delim - defpath + 1;
                size_t end = strlen(buf) + len;
                strncat(buf, defpath, len);
                *(buf + end) = '\0';
            }
            else {
                strcat(buf, defpath);
                break;
            }
            defpath = delim + 1;
        }
        strcat(buf, delimiter);

        /* Finally, on goes the directory for dynamic-load modules */
        strcat(buf, exec_prefix);

        /* And publish the results */
        module_search_path = buf;
    }

    /* Reduce prefix and exec_prefix to their essence,
     * e.g. /usr/local/lib/python1.5 is reduced to /usr/local.
     * If we're loading relative to the build directory,
     * return the compiled-in defaults instead.
     */
    if (pfound > 0) {
        reduce(prefix);
        reduce(prefix);
    }
    else
        strncpy(prefix, PREFIX, MAXPATHLEN);

    if (efound > 0) {
        reduce(exec_prefix);
        reduce(exec_prefix);
        reduce(exec_prefix);
    }
    else
        strncpy(exec_prefix, EXEC_PREFIX, MAXPATHLEN);
}


/* External interface */

char *
Py_GetPath(void)
{
    if (!module_search_path)
        calculate_path();
    return module_search_path;
}

char *
Py_GetPrefix(void)
{
    if (!module_search_path)
        calculate_path();
    return prefix;
}

char *
Py_GetExecPrefix(void)
{
    if (!module_search_path)
        calculate_path();
    return exec_prefix;
}

char *
Py_GetProgramFullPath(void)
{
    if (!module_search_path)
        calculate_path();
    return progpath;
}
