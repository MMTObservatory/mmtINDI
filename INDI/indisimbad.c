/* Simbad interface
 * connects to http://simbad.u-strasbg.fr/simbad to retrieve named target information.
 * last successful target is updated and published once per minute.
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <signal.h>
#include <stdarg.h>
#include <dirent.h>
#include <float.h>
#include <math.h>
#include <errno.h>
#include <ctype.h>
#include <time.h>
#include <fcntl.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>

#include "astro.h"
#include "indidevapi.h"
#include "configfile.h"
#include "location.h"
#include "localrs.h"
#include "indisimbadprops.h"

/* compile-time config setup */
#define EXITSLEEP       10			/* seconds to wait before exiting */
#define	NOEVENT		999			/* no rise/set/transit magic value */
#define	PUBMS		1000			/* publishing period, ms */
#define	SOCKTO		10			/* socket IO timeout, seconds */
static char simhost_france[] = "simbad.u-strasbg.fr";
static char simhost_harvard[] = "simbad.harvard.edu";

/* local functions */
static void initOnce(void);
static int simbad (const char *id, int lid, char whynot[]);
static int simbad_try (const char cleanid[], const char host[], int lid, char whynot[]);
static int mkconnection (const char *host, int port, char msg[]);
static void uptimeCB (void *dummy);
static void publishCB (void *dummy);
static void computeNow();
static double mjdnow(void);


/* send client definitions of all or specified properties */
void
ISGetProperties (char const *dev, char const *nam)
{

        if (dev && strcmp (SIMBADDEV, dev))
	    return;

	initOnce();

	if (!nam || !strcmp (nam, simbad_version.name))
	    IDDefNumber (&simbad_version, NULL);
	if (!nam || !strcmp (nam, simbad_lookup.name))
	    IDDefText (&simbad_lookup, NULL);
	if (!nam || !strcmp (nam, simbad_results.name))
	    IDDefNumber (&simbad_results, NULL);
}

void
ISNewNumber (const char *dev, const char *name, double *doubles, char *names[], int n)
{
}

void
ISNewText (const char *dev, const char *name, char *texts[], char *names[], int n)
{
	if (!IUCrackText (&simbad_lookup, dev, name, texts, names, n)) {

	    char *id = simbad_lookup.tp[NAME_SL].text;
	    int lid = atoi(simbad_lookup.tp[LID_SL].text);
	    char whynot[1024];

	    simbad_lookup.s = IPS_BUSY;
	    IDSetText (&simbad_lookup, "Looking up '%s' for LID %d", id, lid);

	    if (simbad (id, lid, whynot) < 0) {

		/* result failed: report Alert */
		simbad_lookup.s = IPS_ALERT;
		IDSetText (&simbad_lookup, "Simbad error: %s", whynot);

	    } else {

		/* lookup successful: publish results and report OK */
		simbad_results.s = IPS_OK;
		simbad_results.np[LID_SR].value = lid;
		IDSetNumber (&simbad_results, "Results for '%s'", id);
		simbad_lookup.s = IPS_OK;
		IDSetText (&simbad_lookup, "Results for '%s'", id);
	    }
	}
}

void
ISNewSwitch (const char *dev, const char *name, ISState *states, char *names[],
int n)
{
}

void
ISNewBLOB (const char *dev, const char *name, int sizes[],
    int blobsizes[], char *blobs[], char *formats[], char *names[], int n)
{
}

/* indiserver is sending us a message from a snooped device */
void
ISSnoopDevice (XMLEle *root)
{
}

/* call to perform one-time initializations.
 * harmlessly does nothing if called again.
 */
static void
initOnce()
{
	static int before;

	/* ignore if been here before */
	if (before)
	    return;
	before = 1;

	/* snoop LBTO for weather info */
	IDSnoopDevice("LBTO", "Weather");

	/* handle io errors inline */
	signal (SIGPIPE, SIG_IGN);

	/* start uptime heartbeat */
	IEAddTimer (5000, uptimeCB, NULL);

	/* start publishing */
	IEAddTimer (PUBMS, publishCB, NULL);
}

/* lookup the given ID on simbad.
 * if ok, fill in simbad_results and return 0, else return -1 with excuse in whynot[]
 */
static int
simbad (const char *id, int lid, char whynot[])
{
	char cleanid[512], *idp;
	int idl, i;

	/* copy and scrub id into cleanid */
	idl = strlen(id);
	while (isspace(*id))
	    id++, idl--;
	while (idl > 0 && isspace(id[idl-1]))
	    idl -= 1;
	if (idl == 0) {
	    sprintf (whynot, "Blank id");
	    return (-1);
	}
	idp = cleanid;
	for (i = 0; i < idl && i < (int)sizeof(cleanid)-1; i++) {
	    if (isspace(id[i])) {
		*idp++ = '%';
		*idp++ = '2';
		*idp++ = '0';
	    } else if (id[i] == '+') {
		*idp++ = '%';
		*idp++ = '2';
		*idp++ = 'B';
	    } else
		*idp++ = id[i];
	}
	*idp++ = '\0';
	// IDLog ("ID scrub '%s' -> '%s'\n", id, cleanid);

	/* try each host, USA first then France */
	if (simbad_try (cleanid, simhost_harvard, lid, whynot) < 0) {
	    IDLog ("Error from %s: %s\n", simhost_harvard, whynot);
	    if (simbad_try (cleanid, simhost_france, lid, whynot) < 0) {
		IDLog ( "Error from %s: %s\n", simhost_france, whynot);
		return (-1);
	    }
	}

	/* ok */
	return (0);
}

static void
on_alarm(int sig)
{
	// IDLog ("Internet connection timed out\n");
}

static int
simbad_try (const char cleanid[], const char host[], int lid, char whynot[])
{
	char query[256];
	char buf[1024];
	int s, l, n, w;
	double ra, dec, pmra, pmdec, radvel, para, mag;
	bool rdok, pmok, rvok, paok, magok;
	FILE *fp;

	/* set up timeout timer -- clear when not needed */
	struct sigaction sa;
	memset (&sa, 0, sizeof(sa));
	sa.sa_handler = on_alarm;
	sigemptyset (&sa.sa_mask);
	sigaction(SIGALRM, &sa, NULL);
	alarm (SOCKTO);

	/* open a socket to host */
	IDLog ("Connecting to %s\n", host);
	s = mkconnection (host, 80, whynot);
	if (s < 0) {
	    alarm (0);
	    return (-1);
	}

	/* create the query url
	 * see http://simbad.u-strasbg.fr/simbad/sim-help?Page=sim-url
	 */
	sprintf (buf, "http://%s/simbad/sim-id?output.format=ASCII&coodisp2=d2&obj.coo2=on&frame2=FK5&epoch2=J2000&equi2=2000&list.idsel=off&obj.bibsel=off&obj.messel=off&obj.notesel=off&Ident=%s",
		    host, cleanid);

	IDLog ("URL: %s\n", buf);

	/* create the GET command */
	l = sprintf (query, "GET %s HTTP/1.1\r\nHost: %s\r\nConnection: close\r\nUser-Agent: LBTO\r\n\r\n",
	    buf, host);

	/* send the GET command */
	for (n = 0; n < l; n += w) {
	    w = write (s, query+n, l-n);
	    if (w <= 0) {
		if (w < 0)
		    sprintf (whynot, "Write during '%s': %s", cleanid, strerror(errno));
		else
		    sprintf (whynot, "Write during '%s': EOF", cleanid);
		shutdown (s, SHUT_RDWR);
		close (s);
		alarm (0);
		return (-1);
	    }
	}

	/* read result, looking for good stuff */
	fp = fdopen (s, "r");
	rdok = pmok = rvok = paok = magok = false;
	alarm (SOCKTO);
	while (fgets (buf, sizeof(buf), fp)) {

	    // IDLog ( "%s", buf);

	    if (sscanf (buf, "Coordinates(FK5,ep=J2000,eq=2000): %lf %lf", &ra, &dec) == 2)
		rdok = true;
	    else

	    if (sscanf (buf, "Proper motions: %lf %lf", &pmra, &pmdec) == 2)
		pmok = true;
	    else

	    if (sscanf (buf, "Parallax: %lf", &para) == 1)
		paok = true;
	    else

	    if (sscanf (buf, "Radial Velocity: %lf", &radvel) == 1)
		rvok = true;
	    else

	    /* use V if available, else R */

	    if (!magok && sscanf (buf, "Flux V : %lf", &mag) == 1)
		magok = true;
	    else

	    if (!magok && sscanf (buf, "Flux R : %lf", &mag) == 1)
		magok = true;

	    /* new alarm time for next fgets */

	    alarm (SOCKTO);
	}

	if (ferror (fp)) {
	    if (errno == EINTR)
		sprintf (whynot, "While reading '%s': Connection timed out", cleanid);
	    else
		sprintf (whynot, "While reading '%s': %s", cleanid, strerror(errno));
	    fclose (fp);
	    alarm(0);
	    return (-1);
	}

	/* finished with socket */
	shutdown (s, SHUT_RDWR);
	fclose (fp);	/* also closes s */

	/* find enough? */
	if (!rdok) {
	    sprintf (whynot, "'%s' not found on %s", cleanid, host);
	    alarm(0);
	    return (-1);
	}

	/* fill in simbad_results all but NOW values */
	simbad_results.np[RA2K_SR].value = deghr(ra);
	simbad_results.np[DEC2K_SR].value = dec;
	simbad_results.np[PMRA_SR].value = pmok ? pmra : 0.0;
	simbad_results.np[PMDEC_SR].value = pmok ? pmdec : 0.0;
	simbad_results.np[RADVEL_SR].value = rvok ? radvel : 0.0;
	simbad_results.np[PARALLAX_SR].value = paok? para : 0.0;
	simbad_results.np[MAG_SR].value = magok? mag : 999;


	/* fill in derived values */
	computeNow();

	/* done! */
	alarm(0);
	return (0);
}

/* establish a TCP connection to the named host on the given port.
 * if ok return file descriptor, else -1 with excuse in msg[].
 * N.B. we assume SIGPIPE is SIG_IGN
 */
static int
mkconnection (
const char *host,	/* name of server */
int port,		/* TCP port */
char msg[])		/* return diagnostic message here, if returning -1 */
{

	struct sockaddr_in serv_addr;
	struct hostent  *hp;
	int sockfd;

	/* lookup host address */
	hp = gethostbyname (host);
	if (!hp) {
	    (void) sprintf (msg, "Can not find IP of %s", host); 
	    return (-1);
	}

	/* create a socket to the host's server */
	(void) memset ((char *)&serv_addr, 0, sizeof(serv_addr));
	serv_addr.sin_family = AF_INET;
	serv_addr.sin_addr.s_addr =
		    ((struct in_addr *)(hp->h_addr_list[0]))->s_addr;
	serv_addr.sin_port = htons((short)port);
	if ((sockfd = socket (AF_INET, SOCK_STREAM, 0)) < 0) {
	    (void) sprintf (msg, "socket(%s/%d): %s", host, port, strerror(errno));
	    return (-1);
	}
	if (connect (sockfd, (struct sockaddr *)&serv_addr,
						sizeof(serv_addr)) < 0) {
	    (void) sprintf (msg, "connect(%s): %s", host, strerror(errno));
	    (void) close(sockfd);
	    return (-1);
	}

	/* ok */
	return (sockfd);
}

/* fill in values derived from basic location.
 * Proper Motion performed from constant motion along great circle arc.
 */
static void
computeNow()
{
	double ra2000 = hrrad(simbad_results.np[RA2K_SR].value);	/* RA rads @ 2000.0 */
	double dec2000 = degrad(simbad_results.np[DEC2K_SR].value);	/* Dec rads @ 2000.0 */
	double mjd0 = mjdnow();						/* MJD now */
	double dt = (mjd0 - J2000)/365.25;				/* years of motion since 2000.0 */
	double rapm = degrad(simbad_results.np[PMRA_SR].value/3.6e6);	/* RA PM rad/yr, on sky */
	double decpm = degrad(simbad_results.np[PMDEC_SR].value/3.6e6);	/* Dec PM rad/yr */
	double parallax = degrad(simbad_results.np[PARALLAX_SR].value/3.6e6); /* parallax, radians */
	double A, b, c, ca, B;						/* spherical triangle */
	double ranow, decnow;						/* ra dec now, rads */
	double elat, elng, delat, delng, sunlng;			/* ecliptic lat/long, rads; sun's */
	Now now, *np = &now;
	Obj obj, *op = &obj;
	double rjd, tjd, sjd;

	/* apply proper motion */
	A = atan2 (rapm, decpm);
	b = dt*sqrt(rapm*rapm + decpm*decpm);
	c = M_PI/2.0 - dec2000;
	solve_sphere (A, b, cos(c), sin(c), &ca, &B);
	ranow = ra2000 + B;
	decnow = M_PI/2.0 - acos(ca);

	/* apply annular parallax
	 * http://star-www.st-and.ac.uk/~fv/webnotes/chapt14.htm
	 */
	eq_ecl (mjd0, ranow, decnow, &elat, &elng);
	sunlng = 2*PI*dt + 4.89;	/* sun elng about 280 degs on Jan 1 */
	delat = parallax * cos (sunlng-elng) * sin (elat);
	delng = parallax * sin (sunlng-elng) / cos (elat);
	ecl_eq (mjd0, elat + delat, elng + delng, &ranow, &decnow);
	range (&ranow, 2.0*M_PI);

	simbad_results.np[RA2KNOW_SR].value = radhr(ranow);
	simbad_results.np[DEC2KNOW_SR].value = raddeg(decnow);


	/* set up now */
	memset (np, 0, sizeof(*np));
	mjd = mjd0;
	getLocation(&lat, &lng, &elev);
	epoch = J2000;
	pressure = 700;		// TODO
	temp = 0;		// TODO

	/* set up obj */
	memset (op, 0, sizeof(*op));
	op->o_type = FIXED;
	strncpy (op->o_name, simbad_lookup.tp[NAME_SL].text, sizeof(op->o_name)-1);
	op->f_RA = ranow;
	op->f_dec = decnow;
	op->f_epoch = J2000;

	/* find position now */
	obj_cir (np, op);
	localrs (np, op, degrad(op->s_size/3600./2.0), &rjd, &tjd, &sjd);	/* limb */
	simbad_results.np[RISETM_SR].value =  rjd < 0 ? NOEVENT : mjd_hr (rjd-MJD0);
	simbad_results.np[TRANSTM_SR].value = tjd < 0 ? NOEVENT : mjd_hr (tjd-MJD0);
	simbad_results.np[SETTM_SR].value =   sjd < 0 ? NOEVENT : mjd_hr (sjd-MJD0);
	simbad_results.np[ALT_SR].value =     raddeg(op->s_alt);
	simbad_results.np[AZ_SR].value =      raddeg(op->s_az);
	simbad_results.np[UTC_SR].value =     mjd_hr(mjd);
}

/* return the mjd now */
static double
mjdnow()
{
        struct timeval tv;

	if (gettimeofday (&tv, NULL) < 0) {
	    IDLog ( "gettimeofday(): %s\n", strerror(errno));
	    exit(1);
	}
	return (25567.5 + (tv.tv_sec+tv.tv_usec/1e6)/3600.0/24.0);
}

static void
uptimeCB (void *not_used)
{
	static time_t t0;

	if (t0 == 0)
	    t0 = time (NULL);

	simbad_version.np[UPTIME_SVER].value = time(NULL) - t0;
	IDSetNumber (&simbad_version, NULL);

	IEAddTimer (5000, uptimeCB, NULL);
}

static void
publishCB  (void *not_used)
{
	if (simbad_results.s == IPS_OK) {
	    computeNow();
	    IDSetNumber (&simbad_results, NULL);
	}

	IEAddTimer (PUBMS, publishCB, NULL);
}
