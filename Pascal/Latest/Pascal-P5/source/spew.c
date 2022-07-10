/*******************************************************************************
*                                                                              *
*                                   SPEW TEST                                  *
*                                                                              *
* LICENSING:                                                                   *
*                                                                              *
* Copyright (c) 1996, 2018, Scott A. Franco                                    *
* All rights reserved.                                                         *
*                                                                              *
* Redistribution and use in source and binary forms, with or without           *
* modification, are permitted provided that the following conditions are met:  *
*                                                                              *
* 1. Redistributions of source code must retain the above copyright notice,    *
*    this list of conditions and the following disclaimer.                     *
* 2. Redistributions in binary form must reproduce the above copyright         *
*    notice, this list of conditions and the following disclaimer in the       *
*    documentation and/or other materials provided with the distribution.      *
*                                                                              *
* THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"  *
* AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE    *
* IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE   *
* ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE     *
* LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR          *
* CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF         *
* SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS     *
* INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN      *
* CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)      *
* ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE   *
* POSSIBILITY OF SUCH DAMAGE.                                                  *
*                                                                              *
* The views and conclusions contained in the software and documentation are    *
* those of the authors and should not be interpreted as representing official  *
* policies, either expressed or implied, of the Pascal-P6 project.             *
*                                                                              *
* FUNCTION:                                                                    *
*                                                                              *
* Takes a file parameter. We walk though the given file, one character at a    *
* time, one line at a time, and knock out each character in the file with an   *
* alternate character, then parse that, placing the errors into a temp file.   *
* the error file is then inspected for error numbers and types, and the        *
* results tabulated, then output at the end of the program.                    *
*                                                                              *
* If parse returns "operator attention", then we abort, and print the line and *
* character which caused the error. The file "spewtest.pas" will have the      *
* file that caused the error. An operator attention error is either a problem  *
* with files, access or other run problem, or a system fault. All of these     *
* should be corrected immediately, so we stop.                                 *
*                                                                              *
* If the run completes, the top 10 errors are presented, with the highest      *
* count first to lowest last. These are the number of errors caused by just    *
* a one character source change, so its directly indicative of the quality of  *
* the parser's error recovery.                                                 *
*                                                                              *
* The top error entry is replaced back into the spewtest.pas file, this is     *
* for the convenience of being able to immediately test for the highest        *
* error count case.                                                            *
*                                                                              *
* Translated from Pascal 6/7/2018                                              *
*                                                                              *
* To do:                                                                       *
*                                                                              *
* 1. Catch recycle errors.                                                     *                                                                             *
*                                                                              *
*******************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <limits.h>
#include <string.h>
#include <unistd.h>

#define ALTCHR '~' /* alternate test character */
#define ERRNO  10  /* number of top count errors to log (must be even) */
#define MAXLEN 250 /* maximum line length to process */
#define MAXLIN 20000 /* maximum number of source lines */

int lincnt;  /* line we are testing on */
int chrcnt;  /* character of testing line */
int lincnts; /* save for line */
int chrcnts; /* save for chr */
int totlin;  /* total lines in file */
int totchr;  /* total characters in file */
int done;    /* done with testing flag */
int single;  /* single fault mode */
int randomm;  /* random/linear fault mode */
int limit;   /* limit of iterations to perform */
int again;   /* start again mode (repeat) */

/* top error log, stores the top 10 errors by total spew or number of output
   errors */
typedef struct {

    int errcnt; /* number of errors */
    int errlin; /* line it occurred on */
    int errchr; /* character it occurred on */

} errrec;
errrec errlog[10];
char srcnam[250]; /* name of source file */
int chrlin[MAXLIN]; /* number of characters in each line */

/*******************************************************************************

Find random number

Find random number between 0 and N.

*******************************************************************************/

int randn(int limit)

{

    return limit*rand()/RAND_MAX;

}

/*******************************************************************************

Count characters and lines in file

Counts the lines and characters in a source file. The character count does not
include end of line characters. Also fills out the characters per line array.

*******************************************************************************/

void countchar(/* source filename */ char* sn)

{

    int c;     /* character buffer */
    int cc;    /* characters in this line */
    FILE* sfp; /* source file */

    totlin = 0; /* clear line and character */
    totchr = 0;
    sfp = fopen(sn, "r");
    if (!sfp) {

        fprintf(stderr, "*** Cannot open source file %s\n", sn);
        exit(1);

    }
    cc = 0;
    while ((c = fgetc(sfp)) != EOF) {

        if (c == '\n') {

            totlin++; /* count lines */
            chrlin[totlin] = cc; /* set character count for this line */
            cc = 0; /* clear character count */

        } else { totchr++; cc++; } /* count characters */

    }
    fclose(sfp); /* close files */

}

/*******************************************************************************

Find fault position

Finds the existing fault position in spewtest.pas. Uses that to set the line and
character position.

*******************************************************************************/

void findfault(void)

{

    int c;
    FILE* sfp; /* source file */
    int found; /* fault was found */
    int linpos;
    int chrpos;

    linpos = 1; /* clear line and character */
    chrpos = 1;
    sfp = fopen("spewtest.pas", "r");
    if (!sfp) {

        fprintf(stderr, "*** Cannot open source file spewtest.pas\n");
        exit(1);

    }
    found = 0;
    while ((c = fgetc(sfp)) != EOF && !found) {

        if (c == ALTCHR) {

printf("Found line: %d char: %d\n", linpos, chrpos);
            /*
             * Found fault character, and again mode is on, set the line and
             * character count.
             */
             lincnt = linpos;
             chrcnt = chrpos;
             found = 1;

        }
        if (c == '\n') {

            linpos++; /* count lines */
            chrpos = 1; /* clear character count */

        } else chrpos++; /* count characters */

    }
    fclose(sfp); /* close files */
    if (!found) {

        fprintf(stderr, "*** No fault was found in spewtest.pas\n");
        exit(1);

    }

}

/*******************************************************************************

Create temp file

Copies the source file to a temp file. When the line and character counts match
the one we are testing, we output the alternate character to the file, then
continue copying until we reach the end of the file. If we reach the end of the
file, and the error point is not encountered, then the done flag is set.

*******************************************************************************/

void createtemp(/* alternate character */ char ac,
                /* source filename */     char* sn)

{

    int c, nc; /* character buffer */
    int lc;    /* line counter */
    int cc;    /* character counter */
    int found; /* found replacement position */
    int fcc;   /* found line length */
    FILE* sfp; /* source file */
    FILE* dfp; /* destination file */

    lincnts = lincnt; /* save line and character */
    chrcnts = chrcnt;
    done = 0; /* set not done */
    found = 0; /* set not done */
    sfp = fopen(sn, "r");
    if (!sfp) {

        fprintf(stderr, "*** Cannot open source file %s\n", sn);
        exit(1);

    }
    dfp = fopen("spewtest.pas", "w");
    if (!dfp) {

        fprintf(stderr, "*** Cannot open output file\n");
        exit(1);

    }
    lc = 1; /* set line and character counters */
    cc = 1;
    fcc = 0; /* clear found line count */
    /* copy source file to temp file */
    do { /* until end of source */

        do { /* read source line */

            nc = c = getc(sfp); /* get next source file character */
            if (c != EOF) {

                /* if we are at the test location, replace the character with the
                   alternate */
                if (lc == lincnt && cc == chrcnt) {

                    /*
                     * Note if the line is empty, we have nothing to replace.
                     * Just flag it done and move on.
                     */
                    if (c != '\n') nc = ac; /* replace character */
                    found = 1; /* flag replacement occurred */

                }
                putc(nc, dfp); /* output to temp file */
                if (c != '\n') cc = cc+1; /* count characters */

            }

        } while (c != '\n' && c != EOF); /* until end of line */
        if (c != EOF) {

            /* if we found a line, then put the length of that line here */
            if (fcc == 0 && found) fcc = cc;
            lc = lc+1; /* count lines */
            cc = 1; /* reset characters */

        }

    } while (c != EOF);
    fclose(sfp); /* close files */
    fclose(dfp);
    /* advance character count */
    chrcnt = chrcnt+1;
    if (chrcnt >= fcc) { /* off end of line, go next line */

        lincnt = lincnt+1; /* count off lines */
        chrcnt = 1; /* reset character count */

    }
    /* check test counter off end of file */
    done = !found;

}

/********************************************************************************

Place error in error log

Finds the minimum count entry in the error log, and replaces that. If the new
error is not above that, it is discarded.

*******************************************************************************/

void logerr(/* Error count */      int err,
            /* Line number */      int lin,
            /* Character number */ int chr)

{

    /* error log index */         int ei;
    /* minimum error count */     int min;
    /* index for minimum entry */ int mi;

    min = INT_MAX; /* set no minimum */
    mi = 0;
    /* find minimum entry */
    for (ei = 0; ei < ERRNO; ei++) {

        if (errlog[ei].errcnt < min) {

            /* found an entry smaller than last, use it */
            min = errlog[ei].errcnt; /* save error count */
            mi = ei; /* save index */

        }

    }
    if (err > min) { /* if new error count > min error count */

        errlog[mi].errcnt = err; /* set error count */
        errlog[mi].errlin = lin; /* set line */
        errlog[mi].errchr = chr; /* set character */

    }

}

/********************************************************************************

Sort the error log

Just bubble sorts the error log. Speed is not a big issue here.

*******************************************************************************/

void srterr(void)

{

    errrec errsav; /* save for error log entry */
    int swap;      /* swap flag */
    int ei;        /* index for that */

    do { /* sort table */

        swap = 0; /* set no swap happened */
        ei = 0; /* set 1st entry */
        while (ei < ERRNO-1) { /* traverse the log */

            if (errlog[ei].errcnt < errlog[ei+1].errcnt) { /* swap */

                /* save this entry */
                memcpy(&errsav, &errlog[ei], sizeof(errrec));
                /* copy next to this */
                memcpy(&errlog[ei],  &errlog[ei+1], sizeof(errrec));
                /* place this to next */
                memcpy(&errlog[ei+1], &errsav, sizeof(errrec));
                swap = 1; /* set swap occurred */

            }
            ei = ei+1; /* skip to next pair */

        }

    } while (swap); /* until no swap occurred */

}

/*******************************************************************************

Analize error file

Reads the error output file, and analizes the errors it contains. The error
total line is found, and the number of errors taken from that. If no error total
line is found, then the compiler is assumed to have crashed.

Error totals are logged. Crashes terminate immediately.

*******************************************************************************/

void anaerr(void)

{

    FILE* fp; /* error file */
    char linbuf[MAXLEN]; /* line buffer */
    int ec; /* error count */
    int ef; /* error line found */
    char* r;

    fp = fopen("spewtest.err", "r");
    if (fp) {

        ec = 0; /* clear error count */
        ef = 0; /* set no error line found */
        do { /* get source lines */

            r = fgets(linbuf, MAXLEN, fp); /* get next line */
            if (r) { /* not EOF */

                /* see if this is the error count line */
                if (!strncmp(linbuf, "Errors in program: ", 19)) {

                    /* found, get error count and flag */
                    sscanf(linbuf, "Errors in program: %d", &ec);
                    ef = 1;

                }

                /* check various fatal conditions */
                if (!strncmp(linbuf, "*** Error: Compiler internal error", 34) ||
                    !strncmp(linbuf, "399  Feature not implemented", 28) ||
                    !strncmp(linbuf, "500  Compiler internal error", 28) ||
                    !strncmp(linbuf, "501  Compiler internal error", 28) ||
                    !strncmp(linbuf, "502  Compiler internal error", 28) ||
                    !strncmp(linbuf, "503  Compiler internal error", 28) ||
                    !strncmp(linbuf, "504  Compiler internal error", 28) ||
                    !strncmp(linbuf, "505  Compiler internal error", 28) ||
                    !strncmp(linbuf, "506  Compiler internal error", 28) ||
                    !strncmp(linbuf, "507  Compiler internal error", 28) ||
                    !strncmp(linbuf, "508  Compiler internal error", 28)) {

                    printf("\n");
                    printf("Error requires attention: see spewtest.pas and "
                           "spewtest.err for details.\n");
                    printf("Line: %d char: %d\n\n", lincnt, chrcnt);
                    printf("\n");
                    exit(1);

                }

            }

        } while (r); /* until EOF */
        fclose(fp);
        if (!ef) {

            printf("\n");
            printf("Compiler crashed: see spewtest.pas and spewtest.err for "
                   "details.\n");
            printf("Line: %d char: %d\n\n", lincnt, chrcnt);
            printf("\n");
            exit(1);

        }

    }
    logerr(ec, lincnts, chrcnts); /* log the error stats */

}

/*******************************************************************************

Run test parse

We copy the source file to a temp file with the inserted error, then run a parse
on the temp file, and collect and tabulate the errors.

*******************************************************************************/

void testparse(/* Alternate character */ char ac,
               /* source filename */     char sn[])

{

    int err;


    createtemp(ac, sn); /* create temp file */
    if (!done) { /* not end of file */

        system("compile spewtest");
        anaerr(); /* do error analisys */

    }

}

/*******************************************************************************

Run program

Validate input, then run a series of tests by copying the source file into a
temp with an error inserted. The resulting error count is then logged, with the
top error producers by error count kept. We then print this list, and reproduce
the top error case in a compilable file.

*******************************************************************************/

void main(/* Input argument count */ int argc,
          /* Input argument array */ char *argv[])

{

    int ei;
    int ai;
    int fnd;

    printf("\n");
    printf("Spew test vs. 1.0\n");
    printf("\n");

    single = 0; /* set multiple faults */
    randomm = 0; /* set linear fault mode */
    again = 0; /* set no again mode */
    limit = INT_MAX; /* set maximum iterations */
    lincnt = 1; /* set 1st line and character */
    chrcnt = 1;

    /* clear error logging array */
    for (ei = 0; ei < ERRNO; ei++) {

        errlog[ei].errcnt = 0; /* clear error count */
        errlog[ei].errlin = 0; /* clear line number */
        errlog[ei].errchr = 0; /* clear character number */

    }
    ai = 1; /* set 1st argument */
    fnd = 1; /* set option found */
    while (argc > 1 && fnd) { /* process options */

        fnd = 0; /* set no option found */
        if (!strcmp(argv[ai], "--help") || !strcmp(argv[ai], "-h")) {

            ai++; /* next argument */
            argc--;
            printf("\n");
            printf("Spew test: Test random faults in Pascal compile file\n");
            printf("\n");
            printf("spew [<option>]... file\n");
            printf("\n");
            printf("Options:\n");
            printf("\n");
            printf("--help or -h               Help (this message)\n");
            printf("--line or -l <lineno>      Set single fault line number (default 1)\n");
            printf("--char or -c <charno>      Set single fault character number (default 1)\n");
            printf("--random or -r             Set random fault mode (default is linear)\n");
            printf("--iterations or -i <limit> Set max number of iterations to perform\n");
            printf("--proceed or -p            Override single fault mode and proceed\n");
            printf("--again or -a              Run same file again (recover position)\n");
            printf("\n");
            printf("Note either --line or --char places spew into single fault mode.\n");
            printf("Single fault can be overriden with --proceed or -p, but that must appear\n");
            printf("AFTER line and/or character set options.\n");
            printf("The default limit is the file length for linear mode.\n");
            printf("Again mode recovers previous position from spewtest.pas and starts\n");
            printf("from that point.\n");
            printf("\n");
            fnd = 1; /* set option found */

        } else if (!strcmp(argv[ai], "--line") || !strcmp(argv[ai], "-l")) {

            ai++; /* next argument */
            argc--;
            if (argc <= 1) {

                fprintf(stderr, "Parameter expected\n");
                exit(1);

            }
            lincnt = atoi(argv[ai]); /* get line no */
            single = 1; /* set single fault mode */
            ai++; /* next argument */
            argc--;
            fnd = 1; /* set option found */

        } else if (!strcmp(argv[ai], "--char") || !strcmp(argv[ai], "-c")) {

            ai++; /* next argument */
            argc--;
            if (argc <= 1) {

                fprintf(stderr, "Parameter expected\n");
                exit(1);

            }
            chrcnt = atoi(argv[ai]); /* get line no */
            single = 1; /* set single fault mode */
            ai++; /* next argument */
            argc--;
            fnd = 1; /* set option found */

        } else if (!strcmp(argv[ai], "--proceed") || !strcmp(argv[ai], "-p")) {

            ai++; /* next argument */
            argc--;
            single = 0; /* set multiple fault mode */
            fnd = 1; /* set option found */

        } else if (!strcmp(argv[ai], "--random") || !strcmp(argv[ai], "-r")) {

            ai++; /* next argument */
            argc--;
            randomm = 1; /* set random mode */
            fnd = 1; /* set option found */

        } else if (!strcmp(argv[ai], "--again") || !strcmp(argv[ai], "-a")) {

            ai++; /* next argument */
            argc--;
            again = 1; /* set again (repeat) mode */
            fnd = 1; /* set option found */

        } else if (!strcmp(argv[ai], "--interations") || !strcmp(argv[ai], "-i")) {

            ai++; /* next argument */
            argc--;
            if (argc <= 1) {

                fprintf(stderr, "Parameter expected\n");
                exit(1);

            }
            limit = atoi(argv[ai]); /* get iteration limit */
            ai++; /* next argument */
            argc--;
            fnd = 1; /* set option found */

        }

    }
    if (argc < 2) {

        fprintf(stderr, "No source filename specified\n");
        exit(1);

    }
    strcpy(srcnam, argv[ai]);
    printf("Testing with: %s\n", srcnam);
    countchar(srcnam); /* count characters and lines in file, recover previous position */
    if (again) findfault(); /* if again mode find old fault position */
    done = 0; /* set not done */
    do { /* run test */

        if (randomm) do {

            /* set random line and character */
            lincnt = randn(totlin)+1;
            chrcnt = randn(chrlin[lincnt])+1;

        } while (!chrlin[lincnt]);
        printf("Testing: Line: %d Char: %d\n", lincnt, chrcnt);
        testparse(ALTCHR, srcnam); /* with alternate character */

    } while (!done && --limit > 0 && !single); /* until end of source file
                                                  reached, or limit, or single
                                                  mode */
    srterr(); /* sort the error log */
    /* print out error log */
    printf("\n");
    printf("Error log (maximum first to minimum last)\n");
    printf("\n");
    for (ei = 0; ei < ERRNO; ei++) {

        if (errlog[ei].errcnt > 0)
            printf("Count: %d line: %d char: %d\n", errlog[ei].errcnt,
                   errlog[ei].errlin, errlog[ei].errchr);

    }
    printf("\n");
    /* reproduce the top error count for testing convience */
    if (errlog[0].errcnt > 0) {

        lincnt = errlog[0].errlin; /* set line */
        chrcnt = errlog[0].errchr; /* set character */
        createtemp(ALTCHR, srcnam); /* reproduce the file */
        printf("The maximum error case has been reproduced in spewtest.pas\n");
        printf("\n");

    }

    printf("Function complete\n");

}
