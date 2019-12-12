/* open a config file and give access to values, or create a new config file.
 * #define MAINTEST to make a standalone test program.
 *
 * File format is basically one name and value pair per line:
 *   name consists of alphanumeric characters or underscore;
 *   subsequent blanks, tabs and = are skipped while looking for value;
 *   value is everything subsequent up to EOL or # unless preceeded by \.
 * lines not matching this pattern are ignored.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <ctype.h>

#include "configfile.h"

/* environ variable naming base directory and a default */
const static char ohenv[] = "MMTAOHOME";
const static char ohdef[] = "/opt/local/MMTAO";
const static char defcfg[] = "config";

/* info from config file */
struct _CFValues {
    char *cfgname;		/* malloced copy of file name */
    char **name;		/* malloced list of malloced names */
    char **value;		/* malloced list of malloced values */
    int n;			/* n pairs */
    int echo;			/* whether to print each searched item */
};

static CFValues *crackCF (FILE *fp, char ynot[]);
static void appendString (char **s, int c);

/* character classes */
#define	ISSPACE(c)	((c) == ' '  || (c) == '\t')
#define	ISNVSEP(c)	(ISSPACE(c)  || (c) == '=')
#define	ISNAMEC(c)	(isalnum(c)  || (c) == '_')
#define	ISCOMMC(c)	((c) == '#')
#define	ISEOLC(c)	((c) == '\r' || (c) == '\n')
#define	ISESC(c)	((c) == '\\')

/* open the named config file and return contents ready for cfGet* functions.
 * if echo print retrieved values to stderr.
 * if trouble return NULL with reason in ynot[].
 * N.B. caller must free returned value with cfFree()
 */
CFValues *
cfLoad (const char name[], int echo, char ynot[])
{
    char fn[1024];
    CFValues *cvp;
    FILE *fp;

    /* open file: try ~/. then cwd then system location */
    sprintf (fn, "%s/.%s", getenv("HOME"), name);
    fp = fopen (fn, "r");
    if (!fp) {
        fp = fopen (name, "r");
        if (!fp) {
            sprintf (fn, "%s/%s", cfghome(), name);
            fp = fopen (fn, "r");
            if (!fp) {
                if (ynot)
                    sprintf (ynot, "%s: %s", name, strerror(errno));
                return (NULL);
            }
        }
    }

    /* crack */
    cvp = crackCF (fp, ynot);

    /* record echo setting */
    cvp->echo = echo;

    /* record file name */
    cvp->cfgname = strdup (name);

    /* clean up */
    fclose (fp);
    return (cvp);
}

/* write a new config file. the name will be located within the config area of
 * observatory file system. If the file already exists it will be overwritten.
 * returns 0 if ok else -1 with a brief explanation in ynot[].
 * N.B. this does not free memory used by cvp, caller must still call cfFree()
 */
int
cfSave (CFValues *cvp, const char *name, char ynot[])
{
    char fullname[1024];
    FILE *fp;
    int i;

    /* cvp required */
    if (!cvp) {
        if (ynot)
            sprintf (ynot, "cfSave() called with NULL cvp");
        return (-1);
    }

    /* create file in system location else cwd */
    sprintf (fullname, "%s/%s", cfghome(), name);
    fp = fopen (fullname, "w");
    if (!fp) {
        fp = fopen (name, "w");
        if (!fp) {
            if (ynot)
                sprintf (ynot, "%s: %s", name, strerror(errno));
            return (-1);
        }
    }

    /* write each pair */
    for (i = 0; i < cvp->n; i++)
        fprintf (fp, "%s %s\n", cvp->name[i], cvp->value[i]);

    /* check for trouble */
    if (ferror(fp)) {
        if (ynot)
            sprintf (ynot, "%s: %s", name, strerror(errno));
        fclose (fp);
        return (-1);
    }

    /* clean up and done */
    fclose (fp);
    return (0);
}

/* free memory used by a CFValues */
void
cfFree (CFValues *cvp)
{
    int i;

    if (!cvp)
        return;

    for (i = 0; i < cvp->n; i++) {
        free (cvp->name[i]);
        free (cvp->value[i]);
    }
    free (cvp->cfgname);
    free (cvp->name);
    free (cvp->value);
    free (cvp);
}

/* return the value for the given config file entry name, else def.
 * tolerate no cvp at all.
 * N.B. returned memory disappears when call cfFree().
 * N.B. if not found we can only echo def if given, else caller must echo
 */
char *
cfValue (CFValues *cvp, const char *name, const char *def)
{
    int i;

    if (!cvp) {
        fprintf (stderr, "%s %s (default)\n", name, def);	/* presume echo */
        return ((char *)def);
    }

    for (i = 0; i < cvp->n; i++) {
        if (strcmp (cvp->name[i], name) == 0) {
            if (cvp->echo)
                fprintf (stderr, "%s: %s %s\n", cvp->cfgname, cvp->name[i], cvp->value[i]);
            return (cvp->value[i]);
        }
    }

    if (cvp->echo && def != NULL)
        fprintf (stderr, "%s: %s %s (default)\n", cvp->cfgname, name, def);
    return ((char *)def);
}

/* like cfValue() but returns persistent malloced string
 */
char *
cfMValue  (CFValues *cvp, const char *name, const char *def)
{
    char *v = cfValue (cvp, name, def);
    return (v ? strdup(v) : (char *)def);
}

/* return the value for the given config file entry name as a double, else def
 * tolerate no cvp at all
 */
double
cfDblValue (CFValues *cvp, const char *name, double def)
{
    char defstr[32];
    sprintf (defstr, "%g", def ? def : 0.0);
    char *v = cfValue (cvp, name, defstr);
    if (cvp && cvp->echo && !v)
        fprintf (stderr, "%s: %s %g (default)\n", cvp->cfgname, name, def);
    return (v ? atof(v) : def);
}

/* return the value for the given config file entry name as an int, else def
 * tolerate no cvp at all
 */
int
cfIntValue (CFValues *cvp, const char *name, int def)
{
    char defstr[32];
    sprintf (defstr, "%d", def ? def : 0);
    char *v = cfValue (cvp, name, defstr);
    if (cvp && cvp->echo && !v)
        fprintf (stderr, "%s: %s %d (default)\n", cvp->cfgname, name, def);
    return (v ? strtol(v,NULL,0) : def);
}

/* create an empty CFValues for use by the cfAdd family of functions */
CFValues *
cfNew(void)
{
    CFValues *cvp = (CFValues *) malloc (sizeof(CFValues));;
    cvp->cfgname = strdup ("_Internal_New_");
    cvp->name = (char **) malloc (sizeof(char**));	/* realloc seed */
    cvp->value = (char **) malloc (sizeof(char**));	/* realloc seed */
    cvp->echo = 0;
    cvp->n = 0;

    return (cvp);
}

/* add a string name/value pair to a CFValues */
void
cfAddValue (CFValues *cvp, const char *name, const char *value)
{
    char **p;

    p = cvp->name = (char **) realloc (cvp->name, (cvp->n+1)*sizeof(char**));
    p[cvp->n] = strdup (name);
    p = cvp->value = (char **) realloc (cvp->value, (cvp->n+1)*sizeof(char**));
    p[cvp->n] = strdup (value);
    cvp->n++;
}

/* add a double name/value pair to a CFValues */
void
cfAddDblValue (CFValues *cvp, const char *name, double dvalue)
{
    char fmt[64];
    char **p;
    int l;

    l = sprintf (fmt, "%.20g", dvalue);

    p = cvp->name = (char **) realloc (cvp->name, (cvp->n+1)*sizeof(char**));
    p[cvp->n] = strdup (name);
    p = cvp->value = (char **) realloc (cvp->value, (cvp->n+1)*sizeof(char**));
    p[cvp->n] = strcpy ((char *) malloc(l+1), fmt);
    cvp->n++;
}

/* add an int name/value pair to a CFValues */
void
cfAddIntValue (CFValues *cvp, const char *name, int ivalue)
{
    char fmt[64];
    char **p;
    int l;

    l = sprintf (fmt, "%d", ivalue);

    p = cvp->name = (char **) realloc (cvp->name, (cvp->n+1)*sizeof(char**));
    p[cvp->n] = strdup (name);
    p = cvp->value = (char **) realloc (cvp->value, (cvp->n+1)*sizeof(char**));
    p[cvp->n] = strcpy ((char *) malloc(l+1), fmt);
    cvp->n++;
}

/* change the value for a given named entry in a CFValues */
void
cfChgValue (CFValues *cvp, const char *name, const char *newval)
{
    int i;

    for (i = 0; i < cvp->n; i++) {
        if (strcmp (cvp->name[i], name) == 0) {
            strcpy (cvp->value[i] = (char *) realloc (cvp->value[i], strlen(newval)+1), newval);
        return;
        }
    }
}

/* change the value for a given named integer entry in a CFValues */
void
cfChgIntValue (CFValues *cvp, const char *name, int ivalue)
{
    char fmt[64];
    sprintf (fmt, "%d", ivalue);
    cfChgValue (cvp, name, fmt);
}

/* change the value for a given named double entry in a CFValues */
void
cfChgDblValue (CFValues *cvp, const char *name, double dvalue)
{
    char fmt[64];
    sprintf (fmt, "%.20g", dvalue);
    cfChgValue (cvp, name, fmt);
}

/* return the base directory */
const char *
obshome()
{
    static const char *base;

    /* get base first time */
    if (!base) {
        base = getenv (ohenv);
        if (!base)
            base = ohdef;
    }

    return (base);
}

/* return the default system configuration directory */
const char *
cfghome()
{
    static char *dir;

    /* build dir first time */
    if (!dir) {
        char path[1024];
        sprintf (path, "%s/%s", obshome(), defcfg);
        dir = strdup (path);
    }

    return (dir);
}

/* read the given file and build a CFValues.
 * if trouble return NULL with reason in ynot.
 */
static CFValues *
crackCF (FILE *fp, char ynot[])
{
    typedef enum {
        SEEKNAME,			/* seeking name */
        INCOMMENT,			/* within comment */
        INNAME,			/* within name */
        SEEKVALUE,			/* seeking value */
        INVALUE,			/* within value */
        ESCINVAL			/* saw \ in value */
    } CFState;
    CFValues *cvp;
    CFState s = SEEKNAME;
    int c;

    /* init cvp */
    cvp = cfNew();

    /* process file */
    while ((c = fgetc(fp)) != EOF) {
        switch (s) {
        case SEEKNAME:
            if (ISCOMMC(c)) {
                s = INCOMMENT;
            } else if (ISNAMEC(c)) {
                cvp->name[cvp->n] = (char *) calloc (1, 1);
                appendString (&cvp->name[cvp->n], c);
                s = INNAME;
            }
            break;

        case INCOMMENT:
            if (ISEOLC(c))
                s = SEEKNAME;
            break;

        case INNAME:
            if (ISNAMEC(c))
                appendString (&cvp->name[cvp->n], c);
            else if (ISNVSEP(c))
                s = SEEKVALUE;
            else if (ISEOLC(c))
                s = SEEKNAME;
            else
                s = INCOMMENT;
            break;

        case SEEKVALUE:
            if (ISEOLC(c))
                s = SEEKNAME;
            else if (ISCOMMC(c))
                s = INCOMMENT;
            else if (ISESC(c)) {
                cvp->value[cvp->n] = (char *) calloc (1, 1);
                s = ESCINVAL;
            } else if (!ISNVSEP(c)) {
                cvp->value[cvp->n] = (char *) calloc (1, 1);
                appendString (&cvp->value[cvp->n], c);
                s = INVALUE;
            }
            break;

        case INVALUE:
            if (ISESC(c))
                s = ESCINVAL;
            else if (ISEOLC(c) || ISCOMMC(c)) {
                /* done, strip trailing blanks, add to list */
                char *b = &cvp->value[cvp->n][strlen(cvp->value[cvp->n])-1];
                while (b >= &cvp->value[cvp->n][0] && ISSPACE(*b))
                    *b-- = 0;
                cvp->n++;
                cvp->name = (char **) realloc (cvp->name,
                (cvp->n+1)*sizeof(char**));
                cvp->value = (char **) realloc (cvp->value,
                (cvp->n+1)*sizeof(char**));
                if (ISEOLC(c))
                    s = SEEKNAME;
                else
                    s = INCOMMENT;
            } else
                appendString (&cvp->value[cvp->n], c);
            break;

        case ESCINVAL:
            if (!ISEOLC(c))
                appendString (&cvp->value[cvp->n], c);
            s = INVALUE;
            break;
        }
    }

    if (s != SEEKNAME) {
        if (ynot)
            sprintf (ynot, "Unexpected end of file");
        cfFree (cvp);
        return (NULL);
    }

    return (cvp);
}

/* append c to malloced string s and update storage */
static void
appendString (char **s, int c)
{
    int l = strlen (*s);			/* sans \0 */
    (*s) = (char *) realloc ((*s), l+2);	/* c + \0 */
    (*s)[l] = c;
    (*s)[l+1] = '\0';
}

#ifdef MAINTEST
int
main (int ac, char *av[])
{
    char ynot[1024];
    CFValues *cvp;
    int i;

    /* read a config file on stdin */
    cvp = crackCF (stdin, ynot);
    if (!cvp) {
        fprintf (stderr, "%s\n", ynot);
        exit(1);
    }

    for (i = 0; i < cvp->n; i++)
        printf ("%d: '%s' = '%s'\n", i, cvp->name[i], cvp->value[i]);

    /* save */
    if (cfSave (cvp, "x.cfg", ynot) < 0) {
        fprintf (stderr, "%s\n", ynot);
        exit(1);
    }

    return (0);
}
#endif
