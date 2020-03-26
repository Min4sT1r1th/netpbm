/*
** Version 1.0  September 28, 1996
**
** Copyright (C) 1996 by Mike Burns <burns@cac.psu.edu>
**
** Adapted to Netpbm 2005.08.10 by Bryan Henderson
**
** Permission to use, copy, modify, and distribute this software and its
** documentation for any purpose and without fee is hereby granted, provided
** that the above copyright notice appear in all copies and that both that
** copyright notice and this permission notice appear in supporting
** documentation.  This software is provided "as is" without express or
** implied warranty.
*/

/* References
** ----------
** The select k'th value implementation is based on Algorithm 489 by
** Robert W. Floyd from the "Collected Algorithms from ACM" Volume II.
**
** The histogram sort is based is described in the paper "A Fast Two-
** Dimensional Median Filtering Algorithm" in "IEEE Transactions on
** Acoustics, Speech, and Signal Processing" Vol. ASSP-27, No. 1, February
** 1979.  The algorithm I more closely followed is found in "Digital
** Image Processing Algorithms" by Ioannis Pitas.
*/


#include "pm_c_util.h"
#include "pgm.h"
#include "shhopt.h"
#include "mallocvar.h"
#include "nstring.h"

enum medianMethod {MEDIAN_UNSPECIFIED, SELECT_MEDIAN, HISTOGRAM_SORT_MEDIAN};
#define MAX_MEDIAN_TYPES      2

struct cmdlineInfo {
    /* All the information the user supplied in the command line,
       in a form easy for the program to use.
    */
    const char * inputFileName;
    unsigned int width;
    unsigned int height;
    unsigned int cutoff;
    enum medianMethod type;
};


/* Global variables common to each median sort routine. */
static int const forceplain = 0;
static int format;
static gray maxval;
static gray **grays;
static gray *grayrow;
static int ccolso2, crowso2;
static int row;



static void
parseCommandLine(int argc, char ** argv,
                 struct cmdlineInfo * const cmdlineP) {
/*----------------------------------------------------------------------------
   Note that the file spec array we return is stored in the storage that
   was passed to us as the argv array.
-----------------------------------------------------------------------------*/
    optEntry * option_def;
        /* Instructions to pm_optParseOptions3 on how to parse our options.
         */
    optStruct3 opt;

    unsigned int option_def_index;
    unsigned int widthSpec, heightSpec, cutoffSpec, typeSpec;
    const char * type;

    MALLOCARRAY_NOFAIL(option_def, 100);

    option_def_index = 0;   /* incremented by OPTENT3 */
    OPTENT3(0, "width",     OPT_UINT, &cmdlineP->width,
            &widthSpec, 0);
    OPTENT3(0, "height",    OPT_UINT, &cmdlineP->height,
            &heightSpec, 0);
    OPTENT3(0, "cutoff",    OPT_UINT, &cmdlineP->cutoff,
            &cutoffSpec, 0);
    OPTENT3(0, "type",    OPT_STRING, &type,
            &typeSpec, 0);


    opt.opt_table = option_def;
    opt.short_allowed = FALSE;  /* We have no short (old-fashioned) options */
    opt.allowNegNum = FALSE;  /* We may have parms that are negative numbers */

    pm_optParseOptions3(&argc, argv, opt, sizeof(opt), 0);
        /* Uses and sets argc, argv, and some of *cmdlineP and others. */

    if (!widthSpec)
        cmdlineP->width = 3;
    if (!heightSpec)
        cmdlineP->height = 3;
    if (!cutoffSpec)
        cmdlineP->cutoff = 250;

    if (typeSpec) {
        if (streq(type, "histogram_sort"))
            cmdlineP->type = HISTOGRAM_SORT_MEDIAN;
        else if (streq(type, "select"))
            cmdlineP->type = SELECT_MEDIAN;
        else
            pm_error("Invalid value '%s' for -type.  Valid values are "
                     "'histogram_sort' and 'select'", type);
    } else
        cmdlineP->type = MEDIAN_UNSPECIFIED;

    if (argc-1 < 1)
        cmdlineP->inputFileName = "-";
    else {
        cmdlineP->inputFileName = argv[1];
        if (argc-1 > 1)
            pm_error ("Too many arguments.  The only argument is "
                      "the optional input file name");
    }
}



static void
select489(gray * const a,
          int *  const parray,
          int    const n,
          int    const k) {

    gray t;
    int i, j, l, r;
    int ptmp;

    l = 0;
    r = n - 1;
    while ( r > l ) {
        t = a[parray[k]];
        i = l;
        j = r;
        ptmp = parray[l];
        parray[l] = parray[k];
        parray[k] = ptmp;
        if ( a[parray[r]] > t ) {
            ptmp = parray[r];
            parray[r] = parray[l];
            parray[l] = ptmp;
        }
        while ( i < j ) {
            ptmp = parray[i];
            parray[i] = parray[j];
            parray[j] = ptmp;
            ++i;
            --j;
            while ( a[parray[i]] < t )
                ++i;
            while ( a[parray[j]] > t )
                --j;
        }
        if ( a[parray[l]] == t ) {
            ptmp = parray[l];
            parray[l] = parray[j];
            parray[j] = ptmp;
        } else {
            ++j;
            ptmp = parray[j];
            parray[j] = parray[r];
            parray[r] = ptmp;
        }
        if ( j <= k )
            l = j + 1;
        if ( k <= j )
            r = j - 1;
    }
}



static void
selectMedian(FILE *       const ifP,
             unsigned int const ccols,
             unsigned int const crows,
             unsigned int const cols,
             unsigned int const rows,
             unsigned int const median) {

    unsigned int const numValues = crows * ccols;

    unsigned int col;
    gray * garray;  /* Array of the currently gray values */
    int * parray;
    int * subcol;
    gray ** rowptr;

    garray = pgm_allocrow(numValues);

    MALLOCARRAY(rowptr, crows);
    MALLOCARRAY(parray, numValues);
    MALLOCARRAY(subcol, cols);

    if (rowptr == NULL || parray == NULL || subcol == NULL)
        pm_error("Unable to allocate memory");

    for (col = 0; col < cols; ++col)
        subcol[col] = (col - (ccolso2 + 1)) % ccols;

    /* Apply median to main part of image. */
    for ( ; row < rows; ++row) {
        int crow;
        int rownum, irow, temprow;
        unsigned int col;

        pgm_readpgmrow(ifP, grays[row % crows], cols, maxval, format);

        /* Rotate pointers to rows, so rows can be accessed in order. */
        temprow = (row + 1) % crows;
        rownum = 0;
        for (irow = temprow; irow < crows; ++rownum, ++irow)
            rowptr[rownum] = grays[irow];
        for (irow = 0; irow < temprow; ++rownum, ++irow)
            rowptr[rownum] = grays[irow];

        for (col = 0; col < cols; ++col) {
            if (col < ccolso2 || col >= cols - ccolso2) {
                grayrow[col] = rowptr[crowso2][col];
            } else if (col == ccolso2) {
                unsigned int const leftcol = col - ccolso2;
                unsigned int i;
                i = 0;
                for (crow = 0; crow < crows; ++crow) {
                    gray * const temprptr = rowptr[crow] + leftcol;
                    unsigned int ccol;
                    for (ccol = 0; ccol < ccols; ++ccol) {
                        garray[i] = *(temprptr + ccol);
                        parray[i] = i;
                        ++i;
                    }
                }
                select489(garray, parray, numValues, median);
                grayrow[col] = garray[parray[median]];
            } else {
                unsigned int const addcol = col + ccolso2;
                unsigned int crow;
                unsigned int tsum;
                for (crow = 0, tsum = 0; crow < crows; ++crow, tsum += ccols)
                    garray[tsum + subcol[col]] = *(rowptr[crow] + addcol );
                select489( garray, parray, numValues, median );
                grayrow[col] = garray[parray[median]];
            }
        }
        pgm_writepgmrow( stdout, grayrow, cols, maxval, forceplain );
    }

    {
        unsigned int irow;
        /* Write out remaining unchanged rows. */
        for (irow = crowso2 + 1; irow < crows; ++irow)
            pgm_writepgmrow(stdout, rowptr[irow], cols, maxval, forceplain);
    }
    free(subcol);
    free(parray);
    free(rowptr);
    pgm_freerow(garray);
}



static void
histogramSortMedian(FILE *       const ifP,
                    unsigned int const ccols,
                    unsigned int const crows,
                    unsigned int const cols,
                    unsigned int const rows,
                    unsigned int const median) {

    unsigned int const histmax = maxval + 1;

    unsigned int * hist;
    unsigned int mdn, ltmdn;
    gray * leftCol;
    gray * rghtCol;
    gray ** rowptr;

    MALLOCARRAY(rowptr, crows);
    MALLOCARRAY(hist, histmax);

    if (rowptr == NULL || hist == NULL)
        pm_error("Unable to allocate memory");

    leftCol = pgm_allocrow(crows);
    rghtCol = pgm_allocrow(crows);

    /* Apply median to main part of image. */
    for ( ; row < rows; ++row) {
        unsigned int col;
        unsigned int temprow;
        unsigned int rownum;
        unsigned int irow;
        unsigned int i;
        /* initialize hist[] */
        for (i = 0; i < histmax; ++i)
            hist[i] = 0;

        pgm_readpgmrow(ifP, grays[row % crows], cols, maxval, format);

        /* Rotate pointers to rows, so rows can be accessed in order. */
        temprow = (row + 1) % crows;
        rownum = 0;
        for (irow = temprow; irow < crows; ++rownum, ++irow)
            rowptr[rownum] = grays[irow];
        for (irow = 0; irow < temprow; ++rownum, ++irow)
            rowptr[rownum] = grays[irow];

        for (col = 0; col < cols; ++col) {
            if (col < ccolso2 || col >= cols - ccolso2)
                grayrow[col] = rowptr[crowso2][col];
            else if (col == ccolso2) {
                unsigned int crow;
                unsigned int const leftcol = col - ccolso2;
                i = 0;
                for (crow = 0; crow < crows; ++crow) {
                    unsigned int ccol;
                    gray * const temprptr = rowptr[crow] + leftcol;
                    for (ccol = 0; ccol < ccols; ++ccol) {
                        gray const g = *(temprptr + ccol);
                        ++hist[g];
                        ++i;
                    }
                }
                ltmdn = 0;
                for (mdn = 0; ltmdn <= median; ++mdn)
                    ltmdn += hist[mdn];
                --mdn;
                if (ltmdn > median)
                    ltmdn -= hist[mdn];

                grayrow[col] = mdn;
            } else {
                unsigned int crow;
                unsigned int const subcol = col - (ccolso2 + 1);
                unsigned int const addcol = col + ccolso2;
                for (crow = 0; crow < crows; ++crow) {
                    leftCol[crow] = *(rowptr[crow] + subcol);
                    rghtCol[crow] = *(rowptr[crow] + addcol);
                }
                for (crow = 0; crow < crows; ++crow) {
                    {
                        gray const g = leftCol[crow];
                        --hist[(unsigned int) g];
                        if ((unsigned int) g < mdn)
                            --ltmdn;
                    }
                    {
                        gray const g = rghtCol[crow];
                        ++hist[(unsigned int) g];
                        if ((unsigned int) g < mdn)
                            ++ltmdn;
                    }
                }
                if (ltmdn > median)
                    do {
                        --mdn;
                        ltmdn -= hist[mdn];
                    } while (ltmdn > median);
                else {
                    /* This one change from Pitas algorithm can reduce run
                    ** time by up to 10%.
                    */
                    while (ltmdn <= median) {
                        ltmdn += hist[mdn];
                        ++mdn;
                    }
                    --mdn;
                    if (ltmdn > median)
                        ltmdn -= hist[mdn];
                }
                grayrow[col] = mdn;
            }
        }
        pgm_writepgmrow(stdout, grayrow, cols, maxval, forceplain);
    }

    {
        /* Write out remaining unchanged rows. */
        unsigned int irow;
        for (irow = crowso2 + 1; irow < crows; ++irow)
            pgm_writepgmrow(stdout, rowptr[irow], cols, maxval, forceplain);
    }
    pgm_freerow(leftCol);
    pgm_freerow(rghtCol);
    free(hist);
    free(rowptr);
}



int
main(int    argc,
     char * argv[]) {

    struct cmdlineInfo cmdline;
    FILE * ifP;
    int cols, rows;
    int median;
    enum medianMethod medianMethod;

    pgm_init(&argc, argv);

    parseCommandLine(argc, argv, &cmdline);

    ifP = pm_openr(cmdline.inputFileName);

    ccolso2 = cmdline.width / 2;
    crowso2 = cmdline.height / 2;

    pgm_readpgminit(ifP, &cols, &rows, &maxval, &format);
    pgm_writepgminit(stdout, cols, rows, maxval, forceplain);

    /* Allocate space for number of rows in mask size. */
    grays = pgm_allocarray(cols, cmdline.height);
    grayrow = pgm_allocrow(cols);

    /* Read in and write out initial rows that won't get changed. */
    for (row = 0; row < cmdline.height - 1; ++row) {
        pgm_readpgmrow(ifP, grays[row], cols, maxval, format);
        /* Write out the unchanged row. */
        if (row < crowso2)
            pgm_writepgmrow(stdout, grays[row], cols, maxval, forceplain);
    }

    median = (cmdline.height * cmdline.width) / 2;

    /* Choose which sort to run. */
    if (cmdline.type == MEDIAN_UNSPECIFIED) {
        if ((maxval / ((cmdline.width * cmdline.height) - 1)) < cmdline.cutoff)
            medianMethod = HISTOGRAM_SORT_MEDIAN;
        else
            medianMethod = SELECT_MEDIAN;
    } else
        medianMethod = cmdline.type;

    switch (medianMethod) {
    case SELECT_MEDIAN:
        selectMedian(ifP, cmdline.width, cmdline.height, cols, rows, median);
        break;

    case HISTOGRAM_SORT_MEDIAN:
        histogramSortMedian(ifP, cmdline.width, cmdline.height,
                            cols, rows, median);
        break;
    case MEDIAN_UNSPECIFIED:
        pm_error("INTERNAL ERROR: median unspecified");
    }

    pm_close(ifP);
    pm_close(stdout);

    pgm_freearray(grays, cmdline.height);
    pgm_freerow(grayrow);

    return 0;
}



