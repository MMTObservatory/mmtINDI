/* this is a valid INDI driver that does nothing except publish its version and uptime.
 * it can serve as starting point for any INDI driver.
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <errno.h>
#include <math.h>
#include <fcntl.h>
#include <time.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/dir.h>

#define	DRIVERNAME	"Stub"

#include "indidevapi.h"
#include "configfile.h"

/* Version property
 */
typedef enum {  /* N.B. order must match array in vector version_elements[] */
    DRIVER_STVER, PROPS_STVER, UPTIME_STVER,
    N_STVER
} STVersionIndex;
static INumber stubversion_elements[N_STVER] = {
    {"Driver", 		"Driver version",		"%6.3f", 0, 0, 0, 1.0},
    {"Properties", 	"Property list version",	"%6.3f", 0, 0, 0, 1.0},
    {"Uptime", 	        "Seconds since driver start",	"%6.0f"},
};
INumberVectorProperty stubversion = {DRIVERNAME, "Version",
    "Version info",
    "", IP_RO, 0, IPS_IDLE, stubversion_elements, NARRAY(stubversion_elements)};


/* config values
 */
static char cfgfn[] = "stub.cfg";                       // our config file name
static int uptime_dt;                                   // uptime period, ms

/* local functions
 */
static void initOnce (void);
static void readConfig (void);
static void uptimeCB (void *not_used);
static void bye (const char *fmt, ...);


/* send client definitions of all our properties
 */
void ISGetProperties (char const *dev, char const *name)
{
        initOnce();

	if (!name || !strcmp (name, stubversion.name))
	    IDDefNumber (&stubversion, NULL);
}

/* called on receipt of a Number property
 */
void ISNewNumber (const char *dev, const char *name, double *doubles, char *names[], int n)
{
        // if (0 == IUCrackNumber (INumberVectorProperty *nvp, dev, name, doubles, names, n)) {

}

/* called on receipt of a IText property
 */
void ISNewText (const char *dev, const char *name, char *texts[], char *names[], int n)
{
        // if (0 == IUCrackText (ITextVectorProperty *tvp, dev, name, texts, names, n))

}


/* called on receipt of a ISwitch property
 */
void ISNewSwitch (const char *dev, const char *name, ISState *states, char *names[], int n)
{
        // if (0 == IUCrackSwitch (ISwitchVectorProperty *svp, dev, name, states, names, n)) {

}

/* called on receipt of a IBLOB property
 */
void ISNewBLOB (const char *dev, const char *name, int sizes[],
    int blobsizes[], char *blobs[], char *formats[], char *names[], int n)
{
}

/* called when indiserver is sending us a message from a snooped device
 */
void ISSnoopDevice (XMLEle *root)
{
	fprintf (stderr, "Snooping %s.%s\n", findXMLAttValu (root, "device"),
						findXMLAttValu (root, "name"));
}

/* called to perform any one-time initialization work.
 * harmless if called again.
 * exit if trouble.
 */
static void initOnce()
{
        // inforce one-time behavior
        static bool before;
        if (before)
            return;
        before = true;

        // read config file
        readConfig();

        // start uptime
        IEAddTimer (uptime_dt, uptimeCB, NULL);
}

/* read out configuration file.
 * exit if trouble.
 */
static void readConfig()
{
        CFValues *cvp = cfLoad (cfgfn, 1, NULL);
        if (!cvp)
            bye ("Can not find config file %s", cfgfn);

        // my_string = cfMValue (cvp, "strname", "default");
        // my_double = cfDblValue (cvp, "dblname", 0.0);
        uptime_dt = cfIntValue (cvp, "uptime_dt", 5000);

        cfFree (cvp);
}

/* called periodically to publish our uptime
 */
static void uptimeCB (void *not_used)
{
	static time_t t0;

	if (t0 == 0)
	    t0 = time(NULL);

	stubversion.np[UPTIME_STVER].value = time(NULL) - t0;
	IDSetNumber (&stubversion, NULL);

	IEAddTimer (uptime_dt, uptimeCB, NULL);
}

/* log and publish a fatal error message to our version property and exit.
 * arguments works like printf()
 */
static void bye (const char *fmt, ...)
{
        char msg[2048];
        va_list va;

        // format message
        va_start (va, fmt);
        vsnprintf (msg, sizeof(msg), fmt, va);
        va_end (va);

        // rm any nl
        char *nl = strchr (msg, '\n');
        if (nl)
            *nl = '\0';

        // publish to inform clients, which also gets logged
        stubversion.s = IPS_ALERT;
        IDDefNumber (&stubversion, msg);         // use Def in case there is very early

        /* outta here */
        exit(1);
}
