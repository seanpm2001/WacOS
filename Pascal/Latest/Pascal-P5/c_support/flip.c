/*******************************************************************************
*
*                 CONVERT LINE ENDINGS BETWEEN UNIX AND DOS
*
* Converts line endings from dos format to Unix, or Unix to dos.
*
* Format:
*
* flip [-udbc] [file] ...
*
* The options are:
*
*    u     Generate Unix mode line endings.
*    d,m   Generate DOS mode line endings.
*    b     Force convertion on binary file (default is do not convert).
*    c     Send output to standard output instead of converting in place.
*
* This is the same utilitiy that comes with several versions of Unix.
*
* Note that we use a universal line ending recognizer, that sees any of the
* following line endings as valid:
*
* crlf
* lfcr
* cf
* lf
*
* Thus, it will convert anything to the desired line ending, and is a no-op
* if the file is already in that format.
*
* This is the WAY LINE ENDINGS SHOULD BE RECOGNIZED IN *** ANY *** PROGRAM.
* Copy this code and USE IT. Type your input files binary and provide your OWN
* line ending recognizer instead of the braindead one standard with libc.
* Think for yourself, use the force. Peace.
*
* Unlike the original flip utility there is no automatic sensing of system type.
* It defaults to Unix line endings. The lesson here is don't use the default.
*
*******************************************************************************/

#include <stdio.h>
#include <stdlib.h>

int main(int argc, char *argv[])

{

    int c;
    int lf = 0;
    int cr = 0;
    FILE *sfp, *dfp;
    /* options */    
    int unixmode;
    int forcebin;
    int pstdout;
    char *cp;

    unixmode = 1; /* set default is unix mode */
    forcebin = 0; /* do not force convertion of bin file */
    pstdout = 0; /* do not send to standard output */

    /* process all present files */
    while (--argc > 0) {

        if (argv[1][0] == '-') { /* there are options */

            cp = &argv[1][1]; /* index 1st option character */
            while (*cp) { /* not end */

                if (*cp == 'd' || *cp == 'm') unixmode = 0; /* set dos mode */
                else if (*cp == 'u') unixmode = 1; /* set unix mode */
                else if (*cp == 'b') forcebin = 1; /* set force convert binary */
                else if (*cp == 'c') pstdout = 1; /* send to stdout */
                else { /* bad option */

                    printf("Bad option character %c\n", *cp);
                    exit(1);

                }
                cp++; /* next character */

            }
            ++argv; /* next argument */
            goto skip; /* skip to next */

        }
        if ((sfp = fopen(*++argv, "rb")) == NULL) {

            printf("flip: Can't open %s\n", *argv);
            exit(1);

        }
        if (pstdout) dfp = stdout; /* send to standard output */
        else if ((dfp = fopen("flip_temp", "wb")) == NULL) {

            printf("flip: Can't open output file %s\n", *argv);
            exit(1);

        }
        /* copy contents and fix line endings to temp file */
        while ((c = getc(sfp)) != EOF) {
        
            if (c == '\n') {
        
                if (cr) {
        
                    /* Last was lf, this is cr, ignore */
                    cr = 0;
                    lf = 0;
        
                } else {
        
                    /* output newline and flag last */
                    if (unixmode) fprintf(dfp, "\n");
                    else fprintf(dfp, "\r\n");
                    lf = 1;
        
                }
        
             } else if (c == '\r') {
        
                if (lf) {
        
                    /* last was cr, this is lf, ignore */
                    cr = 0;
                    lf = 0;
                  
                } else {
        
                    /* output newline and flag last */
                    if (unixmode) fprintf(dfp, "\n");
                    else fprintf(dfp, "\r\n");
                    cr = 1;
        
                }
        
            } else {
        
                /* Check binary character. We also check for common control 
                   characters (characters under space). The idea of this check
                   is that if the command is executed on too wide a swath
                   (like *), that it won't convert any binary files in the 
                   directory. */
                if ((c > 0x7f || (c < ' ' && c != '\t' && c != '\v' && 
                                 c != '\b' && c != '\f' && c != '\a')) && !forcebin) {

                    printf("File %s is binary, skipping\n", *argv);
                    fclose(sfp); // close input file
                    fclose(dfp); // close output file
                    remove("flip_temp"); // remove temp file
                    goto skip; // skip to next file

                }
                /* output normal character */
                putc(c, dfp);
                cr = 0;
                lf = 0;
        
            }
        
        }
        /* close files and rename destination to source */
        fclose(sfp); /* close input file */
        if (!pstdout)  { /* didn't print to stdout */

            fclose(dfp); /* close output file */
            if (remove(*argv)) { /* delete original */

                printf("Cannot remove file %s\n", *argv);
                exit(1);

            }
            if (rename("flip_temp", *argv)) { /* rename temp to final */

                printf("Cannot rename flip_temp to %s\n", *argv);
                exit(1);

            }

        }
        skip: ; // skip to next file
        
    }

}
            
            
            
            
            
            
            
            
            
