/* connect to an INDI server and show all desired device.property.element
 *   with possible wild card * in any category.
 * All types but BLOBs are handled from their defXXX messages. Receipt of a
 *   defBLOB sends enableBLOB then uses setBLOBVector for the value. BLOBs
 *   are stored in a file dev.nam.elem.format. only .z compression is handled.
 * exit status: 0 at least some found, 1 some not found, 2 real trouble.
 */

#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>
#include <errno.h>
#include <signal.h>
#include <time.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>

#include "indiapi.h"
#include "connect_to.h"
#include "lilxml.h"
#include "base64.h"
#include "zlib.h"

#define	LABELCOL	(52)	/* startig column for -l labels */

/* table of INDI definition elements, plus setBLOB.
 * we also look for set* if -m
 */
typedef struct {
    const char *vec;		/* vector name */
    const char *one;		/* one element name */
} INDIDef;
static INDIDef defs[] = {
    {"defTextVector",   "defText"},
    {"defNumberVector", "defNumber"},
    {"defSwitchVector", "defSwitch"},
    {"defLightVector",  "defLight"},
    {"defBLOBVector",   "defBLOB"},
    {"setBLOBVector",   "oneBLOB"},
    {"setTextVector",   "oneText"},
    {"setNumberVector", "oneNumber"},
    {"setSwitchVector", "oneSwitch"},
    {"setLightVector",  "oneLight"},
};
static int ndefs = 6;			/* or 10 if -m */

/* table of keyword to use in query vs name of INDI defXXX attribute */
typedef struct {
    const char *keyword;
    const char *indiattr;
} INDIkwattr;
static INDIkwattr kwattr[] = {
    {"_LABEL", "label"},
    {"_GROUP", "group"},
    {"_STATE", "state"},
    {"_PERM", "perm"},
    {"_TO", "timeout"},
    {"_TS", "timestamp"},
};
#define NKWA   ((int)((sizeof(kwattr)/sizeof(kwattr[0]))))

typedef struct {
    char *d;				/* device to seek */
    char *p;				/* property to seek */
    char *e;				/* element to seek */
    int wc : 1;				/* whether pattern uses wild cards */
    int ok : 1;				/* something matched this query */
} SearchDef;
static SearchDef *srchs;		/* properties to look for */
static int nsrchs;




/* malloced linked lists of devices, properties and elements for kflag. All lists in alpha order by name.
 *
 *   kdevs
 *    |
 *    V
 *   Dev1  ->   Prop1  ->  Elem1  ->  Elem2  ->  NULL
 *               |
 *               V
 *              Prop2  ->  Elem1  ->  Elem2  ->  Elem3  ->  NULL
 *    |          |
 *    V          V
 *              NULL
 *
 *   Dev2  ->   Prop1  ->  Elem1  ->  NULL
 *               |
 *    |          V
 *    V         NULL
 *
 *   NULL
 */
typedef struct _KElement {
    char *elemname;
    char *elemlabel;
    struct _KElement *next_elemp;
} KElement;
typedef struct _KProperty {
    char *propname;
    char *proplabel;
    char *proptype;
    char *perm;
    struct _KElement *elemsp;
    struct _KProperty *next_propp;
} KProperty;
typedef struct _KDevice {
    char *devname;
    struct _KProperty *propsp;
    struct _KDevice *next_devp;
} KDevice;

static KDevice *kdevs;

static char *me;			/* our name for usage() message */
static char host_def[] = "localhost";	/* default host name */
static char *host = host_def;		/* working host name */
#define INDIPORT	7624		/* default port */
static int port = INDIPORT;		/* working port number */
#define TIMEOUT		2		/* default timeout, secs */
static int timeout = TIMEOUT;		/* working timeout, secs */
static int verbose;			/* report extra info */
static LilXML *lillp;			/* XML parser context */
#define WILDCARD	'*'		/* show all in this category */
static int onematch;			/* only one possible match */
static int justvalue;			/* if just one match show only value */
static int monitor;			/* keep watching even after seen def */
static int directfd = -1;		/* direct filedes to server, if >= 0 */
static FILE *svrwfp;			/* FILE * to talk to server */
static FILE *svrrfp;			/* FILE * to read from server */
static int aflag;			/* timestamp in BLOB file name */
static int wflag;			/* show wo properties too */
static int qflag;			/* don't show some errors */
static int Bflag;			/* include BLOBs */
static int bflag;			/* exclude BLOBs */
static int lflag;			/* display labels */
static int oflag;			/* send BLOBs to stdout */
static int fflag;			/* don't print the def values */
static int kflag;                       /* pretty-print in plain text format */
static int Kflag;                       /* pretty-print in DoKuWiki format */
static time_t k_time;                   /* time of most recent new entry */

static void addKElement (const char *devname, const char *propname, const char *proplabel,
    const char *proptype, const char *perm, const char *elemname, const char *elemlabel);
static void printKplain(void);
static void printKDoKu(void);
static void usage (void);
static void crackDPE (char *spec);
static void addSearchDef (char *dev, char *prop, char *ele);
static void openINDIServer(void);
static void getprops(void);
static void listenINDI(void);
static int finished (void);
static void onAlarm (int dummy);
static int readServerChar(void);
static void findDPE (XMLEle *root);
static void findEle (XMLEle *root, const char *dev, const char *nam,
    const char *defone, SearchDef *sp);
static void enableBLOBs(char *dev, char *nam);
static void oneBLOB (XMLEle *parent, XMLEle *root, const char *dev, const char *nam,
    const char *enam, const char *p, int plen);
static void bye(int n);

int
main (int ac, char *av[])
{
	/* save our name */
	me = av[0];

	/* crack args */
	while (--ac && **++av == '-') {
	    char *s = *av;
	    while (*++s) {
		switch (*s) {
		case '1':	/* just value */
		    justvalue++;
		    break;
		case 'a':	/* add BLOB timestamp */
		    aflag++;
		    break;
		case 'B':
		    if (bflag) {
			fprintf (stderr, "May not use both -b and -B\n");
			usage();
		    }
		    Bflag++;
		    break;
		case 'b':
		    if (Bflag) {
			fprintf (stderr, "May not use both -b and -B\n");
			usage();
		    }
		    bflag++;
		    break;
		case 'd':
		    if (ac < 2) {
			fprintf (stderr, "-d requires open fileno\n");
			usage();
		    }
		    directfd = atoi(*++av);
		    ac--;
		    break;
		case 'f':
		    fflag++;
		    break;
		case 'h':
		    if (directfd >= 0) {
			fprintf (stderr, "May not combine -d and -h\n");
			usage();
		    }
		    if (ac < 2) {
			fprintf (stderr, "-h requires host name\n");
			usage();
		    }
		    host = *++av;
		    ac--;
		    break;
		case 'k':
		    kflag++;
		    break;
		case 'K':
		    Kflag++;
		    break;
		case 'l':
		    lflag++;
		    break;
		case 'm':
		    monitor++;
		    ndefs = 10;			/* include set*Vectors too */
		    break;
		case 'o':
		    oflag++;
		    break;
		case 'p':
		    if (directfd >= 0) {
			fprintf (stderr, "May not combine -d and -p\n");
			usage();
		    }
		    if (ac < 2) {
			fprintf (stderr, "-p requires tcp port number\n");
			usage();
		    }
		    port = atoi(*++av);
		    ac--;
		    break;
		case 'q':
		    qflag++;
		    break;
		case 't':
		    if (ac < 2) {
			fprintf (stderr, "-t requires timeout\n");
			usage();
		    }
		    timeout = atoi(*++av);
		    ac--;
		    break;
		case 'v':	/* verbose */
		    verbose++;
		    break;
		case 'w':
		    wflag++;
		    break;
		default:
		    fprintf (stderr, "Unknown flag: %c\n", *s);
		    usage();
		}
	    }
	}

	/* now ac args starting with av[0] */
	if (ac == 0)
	    av[ac++] = (char *)"*.*.*";		/* default is get everything */

	/* crack each d.p.e */
	while (ac--)
	    crackDPE (*av++);
	onematch = nsrchs == 1 && !srchs[0].wc;

	/* open connection */
	if (directfd >= 0) {
	    svrwfp = fdopen (directfd, "w");
	    svrrfp = fdopen (directfd, "r");
	    if (!svrwfp || !svrrfp) {
		fprintf (stderr, "Direct fd %d not valid\n", directfd);
		exit(1);
	    }
	    setbuf (svrrfp, NULL);	/* don't absorb next guy's stuff */
	    if (verbose)
		fprintf (stderr, "Using direct fd %d\n", directfd);
	} else {
	    openINDIServer();
	    if (verbose)
		fprintf (stderr, "Connected to %s:%d\n", host, port);
	}

	/* build a parser context for cracking XML responses */
	lillp = newLilXML();

	/* issue getProperties */
	getprops();

	/* unbuffered stdout so we can redirect with immediate results */
	setbuf (stdout, NULL);

	/* listen for responses, looking for d.p.e or timeout */
	listenINDI();

	return (0);
}

static void
usage()
{
	int i;

	fprintf(stderr, "Purpose: retrieve readable properties from an INDI server\n");
	fprintf(stderr, "%s\n", "$Revision: 1.7 $");
	fprintf(stderr, "Usage: %s [options] [device[.property[.element]]] ...\n",me);
	fprintf(stderr, "  Any component may be \"*\" to match all (beware shell metacharacters).\n");
	fprintf(stderr, "  Absent components work as \"*\"\n");
	fprintf(stderr, "  Reports all properties if none specified.\n");
	fprintf(stderr, "  If -B then BLOBs are saved in file named device.property.element.format\n");
	fprintf(stderr, "  In perl try: %s\n", "%props = split (/[=\\n]/, `getINDI`);");
	fprintf(stderr, "  Set element to one of following to return property attribute:\n");
	for (i = 0; i < NKWA; i++)
	    fprintf (stderr, "    %10s to report %s\n", kwattr[i].keyword,
	    						kwattr[i].indiattr);
	fprintf(stderr, "Output format: output is fully qualified name=value one per line\n");
	fprintf(stderr, "  or just value if -1 and exactly one query without wildcards.\n");
	fprintf(stderr, "Options:\n");
	fprintf(stderr, "  -1    : print just value if expecting exactly one response\n");
	fprintf(stderr, "  -a    : add timestamp in BLOB file name\n");
	fprintf(stderr, "  -B    : include fetching BLOBs\n");
	fprintf(stderr, "  -b    : exclude fetching BLOBs (deprecated, now the default)\n");
	fprintf(stderr, "  -d f  : use file descriptor f already open to server\n");
	fprintf(stderr, "  -f    : don't print the def* values\n");
	fprintf(stderr, "  -h h  : alternate host, default is %s\n", host_def);
	fprintf(stderr, "  -K    : pretty-print for a dokuwiki table\n");
	fprintf(stderr, "  -k    : pretty-print for a plain text table\n");
	fprintf(stderr, "  -l    : append label to each reported property (unless -1)\n");
	fprintf(stderr, "  -m    : keep monitoring for more updates\n");
	fprintf(stderr, "  -o    : send BLOBs to stdout\n");
	fprintf(stderr, "  -p p  : alternate port, default is %d\n", INDIPORT);
	fprintf(stderr, "  -q    : suppress some error messages\n");
	fprintf(stderr, "  -t t  : max time to wait, default is %d secs; 0 is forever\n",TIMEOUT);
	fprintf(stderr, "  -v    : verbose (cumulative)\n");
	fprintf(stderr, "  -w    : show write-only properties too\n");
	fprintf(stderr, "Exit status:\n");
	fprintf(stderr, "  0: found at least one match for each query\n");
	fprintf(stderr, "  1: at least one query returned nothing\n");
	fprintf(stderr, "  2: real trouble, try repeating with -v\n");

	exit (2);
}

/* crack spec and add to srchs[], else exit */
static void
crackDPE (char *spec)
{
	char d[1024], p[1024], e[2048];

	if (verbose > 1)
	    fprintf (stderr, "looking for %s\n", spec);
	int ns = sscanf (spec, "%[^.].%[^.].%s", d, p, e);
        if (ns < 3)
            strcpy (e, "*");
        if (ns < 2)
            strcpy (p, "*");
        if (ns < 1) {
	    fprintf (stderr, "Unknown format for property spec: %s\n", spec);
	    usage();
	}

	addSearchDef (d, p, e);
}

/* grow srchs[] with the new search */
static void
addSearchDef (char *dev, char *prop, char *ele)
{
	if (!srchs)
	    srchs = (SearchDef *) malloc (1);		/* realloc seed */
	srchs = (SearchDef *) realloc (srchs, (nsrchs+1)*sizeof(SearchDef));
	srchs[nsrchs].d = strcpy ((char *)malloc(strlen(dev)+1), dev);
	srchs[nsrchs].p = strcpy ((char *)malloc(strlen(prop)+1), prop);
	srchs[nsrchs].e = strcpy ((char *)malloc(strlen(ele)+1), ele);
	srchs[nsrchs].wc = *dev==WILDCARD || *prop==WILDCARD || *ele==WILDCARD;
	srchs[nsrchs].ok = 0;
	nsrchs++;
}

/* open a connection to the given host and port.
 * set svrwfp and svrrfp or die.
 */
static void
openINDIServer (void)
{
	struct sockaddr_in serv_addr;
	struct hostent *hp;
	int sockfd;
        int i;

	/* lookup host address */
	hp = gethostbyname (host);
	if (!hp) {
	    herror ("gethostbyname");
	    exit (2);
	}

	/* create a socket to the INDI server */
	(void) memset ((char *)&serv_addr, 0, sizeof(serv_addr));
	serv_addr.sin_family = AF_INET;
	serv_addr.sin_addr.s_addr =
			    ((struct in_addr *)(hp->h_addr_list[0]))->s_addr;
	serv_addr.sin_port = htons(port);
	if ((sockfd = socket (AF_INET, SOCK_STREAM, 0)) < 0) {
	    perror ("socket");
	    exit(2);
	}

	/* connect */
        for (i = 0; i < 2; i++)
            if (connect_to (sockfd,(struct sockaddr *)&serv_addr,sizeof(serv_addr), 1000) == 0)
                break;
        if (i == 2) {
	    perror ("connect");
	    exit(2);
	}

	/* prepare for line-oriented i/o with client */
	svrwfp = fdopen (sockfd, "w");
	svrrfp = fdopen (sockfd, "r");
}

/* issue getProperties to svrwfp, possibly constrained to one device */
static void
getprops()
{
	char **udevs;
	int nudevs, wildcard;
	char *oneprop;
	int i, j;

	/* make room for a list of each unqiue device, worst case is all different */
	udevs = (char **) malloc (nsrchs * sizeof(char *));

	/* make a list of each unique device, or whether see a WILDCARD */
	nudevs = 0;
	wildcard = 0;
	for (i = 0; i < nsrchs; i++) {
	    char *d = srchs[i].d;
	    if (*d == WILDCARD) {
		wildcard = 1;
		break;
	    }
	    for (j = 0; j < nudevs; j++)
		if (strcmp (d, udevs[j]) == 0)
		    break;
	    if (j == nudevs)
		udevs[nudevs++] = d;
	}

	/* if exactly one specified device check further if only one property */
	oneprop = NULL;
	if (!wildcard && nudevs == 1) {
	    for (i = 0; i < nsrchs; i++) {
		char *p = srchs[i].p;
		if (*p == WILDCARD || (oneprop && strcmp(p,oneprop))) {
		    oneprop = NULL;
		    break;
		} else
		    oneprop = p;
	    }
	}

	/* send maximally qualified query */
	if (oneprop) {
	    /* exactly one dev.property */
	    fprintf(svrwfp, "<getProperties version='%g' device='%s' name='%s'/>\n", INDIV, udevs[0], oneprop);
	    if (verbose > 1)
		fprintf(stderr, "<getProperties version='%g' device='%s' name='%s'/>\n", INDIV, udevs[0], oneprop);
	} else if (wildcard) {
	    /* at least one dev with wildcard so no specificity possible */
	    fprintf(svrwfp, "<getProperties version='%g'/>\n", INDIV);
	    if (verbose > 1)
		fprintf(stderr, "<getProperties version='%g'/>\n", INDIV);
	} else {
	    /* all specs are dev.prop, send separate query for each */
	    for (i = 0; i < nudevs; i++) {
		fprintf(svrwfp, "<getProperties version='%g' device='%s'/>\n", INDIV, udevs[i]);
		if (verbose > 1)
		    fprintf(stderr, "<getProperties version='%g' device='%s'/>\n", INDIV, udevs[i]);
	    }
	}

	/* insure transmission */
	fflush (svrwfp);

	/* clean up */
	free (udevs);
}

/* listen for INDI traffic on svrrfp.
 * print matching srchs[] and return when see all.
 * timeout and exit if any trouble.
 */
static void
listenINDI ()
{
	char msg[1024];

	/* arrange to call onAlarm() if not seeing any more defXXX */
	signal (SIGALRM, onAlarm);
	alarm (timeout);

	/* read from server, exit if find all requested properties */
	while (1) {
	    XMLEle *root = readXMLEle (lillp, readServerChar(), msg);
	    if (root) {
		/* found a complete XML element */
		if (verbose > 2)
		    prXMLEle (stderr, root, 0);
		findDPE (root);
		if (finished() == 0)
		    bye (0);		/* found all we want */
		delXMLEle (root);	/* not yet, delete and continue */
	    } else if (msg[0]) {
		fprintf (stderr, "Bad XML from %s:%d: %s\n", host, port, msg);
		bye(2);
	    }
	}
}

/* return 0 if we are sure we have everything we are looking for, else -1 */
static int
finished ()
{
	int i;

        // wait forever?
	if (monitor)
	    return(-1);

        // waited long enough for new properties to pretty-print?
        if ((kflag || Kflag) && (time(NULL) - k_time > timeout))
            return (0);

        // found all specified properties?
	for (i = 0; i < nsrchs; i++)
	    if (srchs[i].wc || !srchs[i].ok)
		return (-1);

        // finished
	return (0);
}

/* called after timeout seconds either because we are matching wild cards or
 * there is something still not found
 */
static void
onAlarm (int dummy)
{
	int trouble = 0;
	int i;

	for (i = 0; i < nsrchs; i++) {
	    if (!srchs[i].ok) {
		trouble = 1;
		if (!qflag)
		    fprintf (stderr, "No %s.%s.%s from %s:%d\n", srchs[i].d,
					srchs[i].p, srchs[i].e, host, port);
	    }
	}

	bye (trouble ? 1 : 0);
}

/* read one char from svrrfp */
static int
readServerChar ()
{
	int c = fgetc (svrrfp);

	if (c == EOF) {
	    if (ferror(svrrfp))
		perror ("read");
	    else
		fprintf (stderr,"INDI server %s:%d disconnected\n", host, port);
	    bye (2);
	}

	if (verbose > 3)
	    fprintf (stderr, "%c", c);

	return (c);
}

/* print value if root is any srchs[] we are looking for*/
static void
findDPE (XMLEle *root)
{
	int i, j;

	for (i = 0; i < nsrchs; i++) {
	    /* for each property we are looking for */
	    for (j = 0; j < ndefs; j++) {
		/* for each possible type */
		int isdef = !strncmp(defs[j].vec, "def", 3);
		if ((!fflag || !isdef) && strcmp (tagXMLEle (root), defs[j].vec) == 0) {
		    /* legal defXXXVector, check device */
		    char *dev = findXMLAttValu (root, "device");
		    char *idev = srchs[i].d;
		    if (idev[0] == WILDCARD || !strcmp (dev,idev)) {
			/* found device, check name */
			char *nam = findXMLAttValu (root, "name");
			char *iprop = srchs[i].p;
			if (iprop[0] == WILDCARD || !strcmp (nam,iprop)) {
			    /* found device and name, check perm */
			    char *perm = findXMLAttValu (root, "perm");
			    if (!wflag && perm[0] && !strchr (perm, 'r')) {
				if (verbose)
				    fprintf (stderr, "%s.%s is write-only\n",
								    dev, nam);
			    } else {
				/* check elements or attr keywords */
				if (!strcmp (defs[j].vec, "defBLOBVector") && Bflag)
                                    enableBLOBs (dev,nam);      // ask for it
                                findEle(root,dev,nam,defs[j].one,&srchs[i]);
				if (onematch)
				    return;		/* only one can match */
				if (!strncmp (defs[j].vec, "def", 3))
				    alarm (timeout);	/* reset timer if def */
			    }
			}
		    }
		}
	    }
	}
}

/* print elements in root speced in sp (known to be of type defone).
 * print just value if onematch and justvalue else fully qualified name.
 */
static void
findEle (XMLEle *root, const char *dev, const char *nam, const char *defone, SearchDef *sp)
{
	char *iele = sp->e;
	XMLEle *ep;
	int i;

	/* check for attr keyword */
	for (i = 0; i < NKWA; i++) {
	    if (strcmp (iele, kwattr[i].keyword) == 0) {
		/* just print the property state, not the element values */
		char *s = findXMLAttValu (root, kwattr[i].indiattr);
		sp->ok = 1;   			/* progress */
		if (onematch && justvalue)
		    printf ("%s\n", s);
		else
		    printf ("%s.%s.%s=%s\n", dev, nam, kwattr[i].keyword, s);
		return;
	    }
	}

	/* no special attr, look for specific element name */
	for (ep = nextXMLEle(root,1); ep; ep = nextXMLEle(root,0)) {
	    if (!strcmp (tagXMLEle (ep), defone)) {
		/* legal defXXX, check deeper */
		char *enam = findXMLAttValu (ep, "name");
		if (iele[0]==WILDCARD || !strcmp(enam,iele)) {
		    /* found it! */
		    char *p = pcdataXMLEle(ep);
                    int is_blob = !strcmp (defone+3, "BLOB");
                    int is_def = !strncmp (defone, "def", 3);
                    if (is_blob) {
			if (!is_def) {
			    oneBLOB (root, ep, dev, nam, enam, p, pcdatalenXMLEle(ep));
			    sp->ok = 1;   		/* mark progress */
			}
		    } else 
			sp->ok = 1;   		/* mark progress */

                    // handle output formats
		    if (!is_blob && onematch && justvalue)
			printf ("%s\n", p);
		    else {
                        char *elabel = findXMLAttValu (ep, "label");
                        if (kflag || Kflag) {
                            if (!is_blob || is_def) {
                                char *perm = findXMLAttValu (root, "perm");
                                char *plabel = findXMLAttValu (root, "label");
                                addKElement (dev, nam, plabel, defone+3, perm, enam, elabel);
                            }
                        } else {
                            int w = 0;
                            if (is_blob) {
                                if (is_def)
                                    w = printf ("%s.%s.%s=(BLOB)", dev, nam, enam);
                            } else
                                w = printf ("%s.%s.%s=%s", dev, nam, enam, p);
                            if (lflag && (!is_blob || is_def))
                                printf ("%*s%s", LABELCOL-w-1, "", elabel);
                            if (w > 0)
                                putchar ('\n');
                        }
		    }
		    if (onematch)
			return;      	/* only one can match*/
		}
	    }
	}
}

/* send server command to svrwfp that enables blobs for the given dev nam then query again
 */
static void
enableBLOBs(char *dev, char *nam)
{
	if (verbose > 1)
	    fprintf (stderr,"<enableBLOB device='%s' name='%s'>Also</enableBLOB>\n", dev, nam);
	fprintf (svrwfp,"<enableBLOB device='%s' name='%s'>Also</enableBLOB>\n", dev, nam);

        fprintf(svrwfp, "<getProperties version='%g' device='%s' name='%s'/>\n", INDIV, dev, nam);
        if (verbose > 1)
            fprintf(stderr, "<getProperties version='%g' device='%s' name='%s'/>\n", INDIV, dev, nam);

	fflush (svrwfp);
}

/* given a oneBLOB pcdata, save or print
 */
static void
oneBLOB (XMLEle *parent, XMLEle *root, const char *dev, const char *nam,
    const char *enam, const char *pc, int pclen)
{
	char *format;
	FILE *fp;
	int bloblen;
	unsigned char *blob;
	int ucs;
	int isz;
	char fn[1024];
	int i;

	/* get uncompressed size */
	ucs = atoi(findXMLAttValu (root, "size"));
	if (verbose > 1)
	    fprintf (stderr, "%s.%s.%s reports uncompressed size as %d\n",
							dev, nam, enam, ucs);

	/* get format and length */
	format = findXMLAttValu (root, "format");
	isz = !strcmp (&format[strlen(format)-2], ".z");

	/* decode blob from base64 in pc */
	blob = (unsigned char *) malloc (3*pclen/4);
	bloblen = from64tobits ((char *)blob, pc);
	if (bloblen < 0) {
	    fprintf (stderr, "%s.%s.%s bad base64\n", dev, nam, enam);
	    bye(2);
	}

	/* uncompress effectively in place if z */
	if (isz) {
	    uLong nuncomp = ucs;
	    unsigned char *uncomp = (unsigned char *) malloc (ucs);
	    int ok = uncompress (uncomp, &nuncomp, blob, bloblen);
	    if (ok != Z_OK) {
		fprintf (stderr, "%s.%s.%s uncompress error %d\n", dev, nam,
								    enam, ok);
		bye(2);
	    }
	    free (blob);
	    blob = uncomp;
	    bloblen = nuncomp;
	}

	/* rig up a file name from property name, include timestamp if desired */
	if (aflag) {
	    char *timestamp = findXMLAttValu (parent, "timestamp");
	    i = sprintf (fn, "%s.%s.%s@%s%s", dev, nam, enam, timestamp, format);
	} else
	    i = sprintf (fn, "%s.%s.%s%s", dev, nam, enam, format);
	if (isz)
	    fn[i-2] = '\0'; 	/* chop off .z */

	/* dispense */
	if (oflag) {
	    /* print */
	    fwrite (blob, bloblen, 1, stdout);
	} else {
	    /* save */
	    fp = fopen (fn, "w");
	    if (fp) {
		if (verbose)
		    fprintf (stderr, "Wrote %s\n", fn);
		fwrite (blob, bloblen, 1, fp);
		fclose (fp);
	    } else {
		fprintf (stderr, "%s: %s\n", fn, strerror(errno));
	    }
	}

	/* clean up */
	free (blob);
}

/* add an element to the tree unless already present.
 * set k_time to time(2) if new.
 */
static void addKElement (const char *devname, const char *propname, const char *proplabel,
    const char *proptype, const char *perm, const char *elemname, const char *elemlabel)
{
        // point prev_devpp at the KDevice whose devname is at or after the new devname
        KDevice **prev_devpp;
        int dev_exists = 0;
        for (prev_devpp = &kdevs; *prev_devpp; prev_devpp = &(*prev_devpp)->next_devp) {
            int cmp = strcmp ((*prev_devpp)->devname, devname);
            if (cmp == 0) {
                dev_exists = 1;
                break;
            }
            if (cmp > 0)
                break;
        }

        // point *prev_devpp to a new KDevice if not already present
        if (!dev_exists) {
            // create a new KDevice
            KDevice *new_devp = (KDevice*) malloc (sizeof(KDevice));

            // link after prev_devpp
            new_devp->devname = strdup (devname);
            new_devp->propsp = NULL;
            new_devp->next_devp = *prev_devpp;
            *prev_devpp = new_devp;
        }

        // KDevice in whose propsp list we now search for propname
        KDevice *dp = *prev_devpp;

        // point prev_proppp at the KProperty whose propname is at or after the new propname
        KProperty **prev_proppp;
        int prop_exists = false;
        for (prev_proppp = &dp->propsp; *prev_proppp; prev_proppp = &(*prev_proppp)->next_propp) {
            int cmp = strcmp ((*prev_proppp)->propname, propname);
            if (cmp == 0) {
                prop_exists = 1;
                break;
            }
            if (cmp > 0)
                break;
        }

        // point *prev_proppp to a new KProperty if not already present
        if (!prop_exists) {
            // create a new KProperty
            KProperty *new_propp = (KProperty*) malloc (sizeof(KProperty));

            // link after prev_proppp
            new_propp->propname = strdup (propname);
            new_propp->proplabel = strdup (proplabel);
            new_propp->proptype = strdup (proptype);
            new_propp->perm = strdup(perm);
            new_propp->elemsp = NULL;
            new_propp->next_propp = *prev_proppp;
            *prev_proppp = new_propp;
        }

        // KProperty in whose elemsp list we now search for elemname
        KProperty *pp = *prev_proppp;

        // point prev_elempp at the KElement whose propname is at or after the new propname
        KElement **prev_elempp;
        int elem_exists = 0;
        for (prev_elempp = &pp->elemsp; *prev_elempp; prev_elempp = &(*prev_elempp)->next_elemp) {
            int cmp = strcmp ((*prev_elempp)->elemname, elemname);
            if (cmp == 0) {
                elem_exists = 1;
                break;
            }
            if (cmp > 0)
                break;
        }

        // point *prev_elempp to a new KProperty if not already present
        if (!elem_exists) {
            // create a new KProperty
            KElement *new_elemp = (KElement*) malloc (sizeof(KElement));

            // link after prev_elempp
            new_elemp->elemname = strdup (elemname);
            new_elemp->elemlabel = strdup (elemlabel);
            new_elemp->next_elemp = *prev_elempp;
            *prev_elempp = new_elemp;

            // record time
            k_time = time(NULL);
        }
}

/* print the INDI property tree in plain text starting at kdevs
 */
static void printKplain()
{
        // scan once to get colon column
        int colon_col = 0;
        for (KDevice *dp = kdevs; dp; dp = dp->next_devp) {
            for (KProperty *pp = dp->propsp; pp; pp = pp->next_propp) {

                // find length of property title
                char buf[1024];
                int pl = snprintf (buf, sizeof(buf), "%s: %s, %s", pp->propname,
                                pp->proptype, pp->perm) + 4;            // + indent
                if (pl > colon_col)
                    colon_col = pl;

                // find length of element name
                for (KElement *ep = pp->elemsp; ep; ep = ep->next_elemp) {
                    int el = strlen (ep->elemname) + 8;                 // + indent
                    if (el > colon_col)
                        colon_col = el;
                }
            }
        }

        // scan again to print
        for (KDevice *dp = kdevs; dp; dp = dp->next_devp) {
            printf ("%s\n", dp->devname);
            for (KProperty *pp = dp->propsp; pp; pp = pp->next_propp) {
                int pl = printf ("    %s: %s, %s", pp->propname, pp->proptype, pp->perm);
                printf ("%*s : %s\n", colon_col-pl, "", pp->proplabel);
                for (KElement *ep = pp->elemsp; ep; ep = ep->next_elemp) {
                    printf ("        %-*s : %s\n", colon_col-8, ep->elemname, ep->elemlabel);
                }
            }
        }
}

/* print the INDI property tree suitable for a DoKuWiki page starting at kdevs
 */
static void printKDoKu()
{
        static char khdr[] = "^%-25s^%-25s^%-8s^%-9s^%-32s^\n";
        static char ktle[] = "|%-25s|//%s//||||\n";
        static char kfmt[] = "|%-25s|%-25s|%-8s|%-9s|//%-32s//|\n";
        static char kgap[] = ":::";

        for (KDevice *dp = kdevs; dp; dp = dp->next_devp) {
            printf ("===== %s Properties =====\n", dp->devname);
            printf (khdr, "Property Name", "Element Name", "Type", "Writable", "Description");
            for (KProperty *pp = dp->propsp; pp; pp = pp->next_propp) {
                printf (ktle, pp->propname, pp->proplabel, "", "", "");
                int first = 1;
                for (KElement *ep = pp->elemsp; ep; ep = ep->next_elemp) {
                    if (first)
                        printf (kfmt, kgap, ep->elemname, pp->proptype, pp->perm, ep->elemlabel);
                    else
                        printf (kfmt, kgap, ep->elemname, kgap, kgap, ep->elemlabel);
                    first = 0;
                }
            }
        }
}


/* cleanly close svr/wfp then exit(n)
 */
static void
bye(int n)
{
        if (n == 0) {
            if (kflag)
                printKplain();
            if (Kflag)
                printKDoKu();
        }

	if ((svrwfp || svrrfp) && directfd < 0) {
	    int rfd = svrrfp ? fileno(svrrfp) : -1;
	    int wfd = svrwfp ? fileno(svrwfp) : -1;
	    if (rfd >= 0) {
		shutdown (rfd, SHUT_RDWR);
		fclose (svrrfp);	/* also closes rfd */
	    }
	    if (wfd >= 0 && wfd != rfd) {
		shutdown (wfd, SHUT_RDWR);
		fclose (svrwfp);	/* also closes wfd */
	    }
	}

	exit (n);
}
