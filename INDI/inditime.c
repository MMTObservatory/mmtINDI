/* time driver
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <math.h>
#include <time.h>
#include <sys/time.h>
#include <unistd.h>

#include "astro.h"
#include "indidevapi.h"
#include "configfile.h"
#include "localrs.h"
#include "location.h"

#define	POLLMS		500		/* ms */

/* operational info */
#define MYDEV "Time"		/* Device name we call ourselves */


/* Version property */
typedef enum {  /* N.B. order must match array in vector version_elements[] */
    DRIVER_TVER, PROPS_TVER, UPTIME_TVER,
    N_TVER
} TVersionIndex;
static INumber version_elements[N_TVER] = {
    {"Driver", 		"Driver version",		"%6.3f", 0, 0, 0, 1.1},
    {"Properties", 	"Property list version",	"%6.3f", 0, 0, 0, 1.1},
    {"Uptime", 	        "Seconds since driver start",	"%6.0f"},
};
INumberVectorProperty timeVersion = {MYDEV, "Version",
    "Version info",
    "", IP_RO, 0, IPS_IDLE, version_elements, NARRAY(version_elements)};


/* Now parameter */
typedef enum {	/* N.B. order must match array below */
    JD_N, UNIX_N, UTC_N, UTCD_N, LT_N, LST_N, MOONAZ_N, MOONALT_N, MOONELONG_N,
    MOONLIT_N, SUNAZ_N, SUNALT_N, N_N
} NowElementsIndex;
static INumber now_elements[N_N] = {
    {"JD",        "Julian Date",                      "%13.5f"},
    {"UNIX",      "Secs since 1/1/1970 UTC",          "%13.2f"},
    {"UTC",       "Universal time",                   "%8.6m"},
    {"UTCDate",   "Packed Universal date",            "%8.0f"},
    {"LT",        "Local time",                       "%8.6m"},
    {"LST",       "Sidereal time",                    "%8.6m"},
    {"MoonAz",    "Moon az, degs EofN",               "%9.6m"},
    {"MoonAlt",   "Moon alt, degs up",                "%9.6m"},
    {"MoonElong", "Moon elongation, degs E of Sun",   "%9.3f"},
    {"MoonLit",   "Moon percent sunlit",              "%9.3f"},
    {"SunAz",     "Sun az, degs EofN",                "%9.6m"},
    {"SunAlt",    "Sun alt, degs up",                 "%9.6m"}
};
static INumberVectorProperty timeNow = {MYDEV, "Now", "Current circumstances",
    "", IP_RO, 0, IPS_OK, now_elements, NARRAY(now_elements)};


/* Events parameter */
typedef enum {	/* N.B. order must match array below */
    MRISE_E, MSET_E, SRISE_E, SSET_E, DUSK_E, DAWN_E, N_E
} EventsElementsIndex;
static INumber events_elements[N_E] = {
    {"MoonRise",   "Moon rise, UTC",       "%12.6m"},
    {"MoonSet",    "Moon set, UTC",        "%12.6m"},
    {"SunRise",    "Sun rise, UTC",        "%12.6m"},
    {"SunSet",     "Sun set, UTC",         "%12.6m"},
    {"Dusk",       "Dusk, UTC",            "%12.6m"},
    {"Dawn",       "Dawn, UTC",            "%12.6m"},
};
static INumberVectorProperty timeEvents={MYDEV, "Events", "Sun and Moon events",
    "", IP_RO, 0, IPS_OK, events_elements, NARRAY(events_elements)};


/* Location parameter */
typedef enum {	/* N.B. order must match array below */
    LAT_L, LONG_L, ELEV_L, MAGDECL_L, N_L
} LocationElementsIndex;
static INumber location_elements[N_L] = {
    {"Latitude",   "Latitude, degs N",                 "%9.6m"},
    {"Longitude",  "Longitude, degs E",                "%9.6m"},
    {"Elevation",  "Elevation, m",                     "%8.1f"},
    {"MagDecl",    "Mag declination, degs mag-true",   "%9.6m"},
};
static INumberVectorProperty location={MYDEV, "Location","Observatory location",
    "", IP_RO, 0, IPS_OK, location_elements, NARRAY(location_elements)};


/* Site property */
typedef enum {	/* N.B. order must match array below */
    NAME_S, N_S
} SiteIndex;
static IText site_elements[N_S] = {
    {"Name",      "Name"},
};
static ITextVectorProperty site = {MYDEV, "Site", "Site name",
    "", IP_RO, 0, IPS_OK, site_elements, NARRAY(site_elements)};


/* config info */
static char cfg_fn[] = "time.cfg";
static double hzn_depression;		/* additional rise/set depression, degrees */

/* mjd for next soonest rise/set event */
static double nextmjd = 0;

static int setNow();
static void updateTime(void *p);
static void readConfig (void);


/* send client definitions of all properties */
void
ISGetProperties (char const *dev, char const *name)
{
	if (dev && strcmp (MYDEV, dev))
	    return;

	if (timeNow.np[JD_N].value == 0) {
	    /* one-time */
	    char buf[1024];
	    readConfig();
	    /* set up for ephemerides */
	    sprintf (buf, "%s/auxil", obshome());
	    setMoonDir (strcpy((char*)malloc(strlen(buf)+1),buf)); /* persistent */
	    setNow();
	    IEAddTimer (POLLMS, updateTime, NULL);
	}

	if (!name || !strcmp (name, timeVersion.name))
	    IDDefNumber (&timeVersion, NULL);
	if (!name || !strcmp (name, timeNow.name))
	    IDDefNumber (&timeNow, NULL);
	if (!name || !strcmp (name, timeEvents.name))
	    IDDefNumber (&timeEvents, NULL);
	if (!name || !strcmp (name, location.name))
	    IDDefNumber (&location, NULL);
	if (!name || !strcmp (name, site.name))
	    IDDefText (&site, NULL);
}

void
ISNewNumber (const char *dev, const char *name, double *doubles, char *names[],
int n)
{
}

void
ISNewText (const char *dev, const char *name, char *texts[], char *names[],
int n)
{
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
	fprintf (stderr, "Snooping %s.%s\n", findXMLAttValu (root, "device"),
						findXMLAttValu (root, "name"));
}

/* always set fields of timeNow.np[].
 * also check if need to update moon or sun based on time > nextmjd.
 * return whether did so.
 */
static int
setNow()
{
	struct tm gtm, ltm, *gp = &gtm, *lp = &ltm;
	Now now, *np = &now;
	Obj so, *sop = &so;
	Obj mo, *mop = &mo;
	double lst, tfrac;
	struct timeval tv;
	double el;
	time_t t;

	/* get time now */
	gettimeofday (&tv, NULL);
	tfrac = tv.tv_usec/1e6;
	t = (time_t)tv.tv_sec;
	gtm = *gmtime (&t);
	t = (time_t)tv.tv_sec;
	setenv ("TZ", "MST7", 1);
	ltm = *localtime (&t);

	/* set up a Now struct */
	memset (np, 0, sizeof(*np));
	mjd = 25567.5 + (t+tfrac)/3600.0/24.0;
	epoch = EOD;
	temp = 0;
	pressure = 1000;
	getLocation(&lat, &lng, &elev);

	/* set the simple time info */
	timeNow.np[JD_N].value = mjd + MJD0;
	timeNow.np[UNIX_N].value = tv.tv_sec + tv.tv_usec*1e-6;
	timeNow.np[UTC_N].value = gp->tm_hour + gp->tm_min/60.0 +
						    (gp->tm_sec + tfrac)/3600.0;
	timeNow.np[UTCD_N].value = (gp->tm_year+1900)*10000 +
					    (gp->tm_mon+1)*100 + gp->tm_mday;
	timeNow.np[LT_N].value = lp->tm_hour + lp->tm_min/60.0 +
						    (lp->tm_sec + tfrac)/3600.0;
	now_lst (np, &lst);
	timeNow.np[LST_N].value = lst;

	/* set moon info */
	memset (mop, 0, sizeof(*mop));
	mop->o_type = PLANET;
	mop->pl_code = MOON;
	obj_cir (np, mop);
	timeNow.np[MOONAZ_N].value = raddeg(mop->s_az);
	timeNow.np[MOONALT_N].value = raddeg(mop->s_alt);
	timeNow.np[MOONELONG_N].value = mop->s_elong;
	el = degrad(mop->s_elong);
	timeNow.np[MOONLIT_N].value = ((1+cos(M_PI-el-degrad(0.1468*sin(el))))*50);

	/* set sun info */
	memset (sop, 0, sizeof(*sop));
	sop->o_type = PLANET;
	sop->pl_code = SUN;
	obj_cir (np, sop);
	timeNow.np[SUNAZ_N].value = raddeg(sop->s_az);
	timeNow.np[SUNALT_N].value = raddeg(sop->s_alt);

	/* update other stuff if past due */
	if (mjd > nextmjd) {
	    double rjd, sjd;
	    int i;

	    /* riset stops within half a min or so,
	     * hedge to make sure alt is well past critcal height.
	     */
	    mjd += 2./60./24.;

	    localrs (np, sop, degrad(sop->s_size/3600./2.0 + hzn_depression), &rjd, NULL, &sjd);
	    timeEvents.np[SRISE_E].value = mjd_hr(rjd);
	    timeEvents.np[SSET_E].value  = mjd_hr(sjd);

	    localrs (np, sop, degrad(18), &rjd, NULL, &sjd);
	    timeEvents.np[DAWN_E].value = mjd_hr(rjd);
	    timeEvents.np[DUSK_E].value = mjd_hr(sjd);

	    localrs (np, mop, degrad(mop->s_size/3600./2.0 + hzn_depression), &rjd, NULL, &sjd);
	    timeEvents.np[MRISE_E].value = mjd_hr(rjd);
	    timeEvents.np[MSET_E].value  = mjd_hr(sjd);

	    /* find next soonest event so we reevaluate then */
	    nextmjd = 1e99;
	    for (i = 0; i < N_E; i++) {
		double v = timeEvents.np[i].value - MJD0;
		if (v > mjd && v < nextmjd)
		    nextmjd = v;
	    }
	    if (nextmjd == 1e99)
		nextmjd = mjd + 0.5;

	    /* yes, we updated next rise/set times */
	    return (1);
	} else {

	    /* no, we did not update next rise/set times */
	    return (0);
	}
}

static void
updateTime (void *p)
{
	static time_t t0;
	int rs = setNow();

	if (t0 == 0)
	    t0 = time(NULL);
	timeVersion.np[UPTIME_TVER].value = time(NULL) - t0;
	IDSetNumber (&timeVersion, NULL);

	IDSetNumber (&timeNow, NULL);
	if (rs)
	    IDSetNumber (&timeEvents, NULL);

	IEAddTimer (POLLMS, updateTime, NULL);
}

/* init location, site and magdecl */
static void
readConfig()
{
	char configdir[1024];
	char errmsg[1024];
	CFValues *cvp;
	double lt, lg, e, m;
	struct timeval tv;
	double md, year;
	int r;
	char *s;

	getLocation (&lt, &lg, &e);
	location.np[LAT_L].value = raddeg(lt);		/* want degs N */
	location.np[LONG_L].value = raddeg(lg);		/* want degs E */
	location.np[ELEV_L].value = e*ERAD;		/* want m */

	cvp = cfLoad ("latlong.cfg", 0, NULL);
	s = cfValue (cvp, "site", "LBT");
	IUSaveText (&site.tp[NAME_S], s);
	cfFree (cvp);

	cvp = cfLoad (cfg_fn, 0, NULL);
	hzn_depression = cfDblValue (cvp, "Horizon", 0.5);
	cfFree (cvp);

	sprintf (configdir, "%s/auxil", obshome());
	gettimeofday (&tv, NULL);
	m = 25567.5 + (tv.tv_sec+tv.tv_usec/1e6)/3600.0/24.0;
	mjd_year (m, &year);
	r = magdecl (lt, lg, e*ERAD, year, configdir, &md, errmsg);
	if (r < 0)
	    fprintf (stderr, "Magdecl err: %s\n", errmsg);
	else
	    location.np[MAGDECL_L].value = raddeg(md);	/* wants degs */
}
