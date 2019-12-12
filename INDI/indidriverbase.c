#if 0
    INDI
    Copyright (C) 2013 Elwood C. Downey

    This library is free software; you can redistribute it and/or
    modify it under the terms of the GNU Lesser General Public
    License as published by the Free Software Foundation; either
    version 2.1 of the License, or (at your option) any later version.

    This library is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
    Lesser General Public License for more details.

    You should have received a copy of the GNU Lesser General Public
    License along with this library; if not, write to the Free Software
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA

#endif

/* The functions in this file are grouped into several broad categories:
 * Drivers must define IS*() functions that we call to deliver incoming INDI messages.
 * Drivers may call ID*() functions to send outgoing INDI XML commands.
 * Drivers may call IE*() functions to build an event-driver program.
 * Drivers may call IU*() functions to perform various common utility tasks.
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <time.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>

#include "indidevapi.h"
#include "base64.h"
#include "eventloop.h"


/* The first time a new FILE is encountered by any of the ID*() functions,
 *   it is associated with its own mutex behind which all subsequent
 *   output is marshalled.
 * N.B. use an array of pointers to mutexes, not an array of mutexes,
 *   because it is not portable to assume mutexes can be moved in memory.
 */
typedef struct {
    FILE *fp;				/* a FILE pointer used to send messages */
    pthread_mutex_t m;			/* a mutex to linearize access to this FILE */
} FILEMutex;
static FILEMutex **filemutex;		/* malloced array or pointers to FILEMutex */
static int nfilemutex;			/* n pointers in filemutex[] */
static pthread_rwlock_t filem_rw;	/* protect filemutex[] access itself */


/* local functions */
static void clientMsgCB (int fd, void *context);
static int dispatch (XMLEle *root, char msg[]);
static int crackDN (XMLEle *root, char **dev, char **name, char msg[]);
static const char *pstateStr(IPState s);
static int crackIPState (char *str, IPState *ip);
static int crackISState (char *str, ISState *ip);
static const char *sstateStr(ISState s);
static const char *ruleStr(ISRule r);
static const char *permStr(IPerm p);
static void timestamp (char buf[], size_t buf_size);
static void vsmessage (FILE *fp, const char *fmt, va_list ap);
static int sexagesimal (const char *str0, double *dp);
static void xmlv1(FILE *fp);
static void fmutexInit (void);
static void fmutexLock (FILE *fp);
static void fmutexUnlock (FILE *fp);



/*****************************************************************************
 * functions we define that drivers may call
 * IDF and ISF versions allow setting explicit output stream, others default to stdout.
 */

/* tell client to create a text vector property */

static void
_IDFDefText (FILE *fp, const ITextVectorProperty *tvp, const char *fmt, va_list ap)
{
	char ts[64];
	int i;

	fmutexLock (fp);

	timestamp (ts, sizeof(ts));

	xmlv1(fp);
	fprintf (fp, "<defTextVector\n");
	fprintf (fp, "  device='%s'\n", tvp->device);
	fprintf (fp, "  name='%s'\n", tvp->name);
	fprintf (fp, "  label='%s'\n", tvp->label);
	fprintf (fp, "  group='%s'\n", tvp->group);
	fprintf (fp, "  state='%s'\n", pstateStr(tvp->s));
	fprintf (fp, "  perm='%s'\n", permStr(tvp->p));
	fprintf (fp, "  timeout='%g'\n", tvp->timeout);
	fprintf (fp, "  timestamp='%s'\n", ts);
	if (fmt)
	    vsmessage (fp, fmt, ap);
	fprintf (fp, ">\n");

	for (i = 0; i < tvp->ntp; i++) {
	    IText *tp = &tvp->tp[i];
	    fprintf (fp, "  <defText\n");
	    fprintf (fp, "    name='%s'\n", tp->name);
	    fprintf (fp, "    label='%s'>\n", entityXML(tp->label));
	    fprintf (fp, "      %s\n", tp->text ? entityXML(tp->text) : "");
	    fprintf (fp, "  </defText>\n");
	}

	fprintf (fp, "</defTextVector>\n");
	fflush (fp);

	fmutexUnlock (fp);
}

void
IDFDefText (FILE *fp, const ITextVectorProperty *tvp, const char *fmt, ...)
{
	va_list ap;
	va_start (ap, fmt);
	_IDFDefText (fp, tvp, fmt, ap);
	va_end (ap);
}

void
IDDefText (const ITextVectorProperty *tvp, const char *fmt, ...)
{
	va_list ap;
	va_start (ap, fmt);
	_IDFDefText (stdout, tvp, fmt, ap);
	va_end (ap);
}


/* tell client to create a new numeric vector property */

static void
_IDFDefNumber (FILE *fp, const INumberVectorProperty *nvp, const char *fmt, va_list ap)
{
	char ts[64];
	int i;

	fmutexLock (fp);

	timestamp (ts, sizeof(ts));

	xmlv1(fp);
	fprintf (fp, "<defNumberVector\n");
	fprintf (fp, "  device='%s'\n", nvp->device);
	fprintf (fp, "  name='%s'\n", nvp->name);
	fprintf (fp, "  label='%s'\n", nvp->label);
	fprintf (fp, "  group='%s'\n", nvp->group);
	fprintf (fp, "  state='%s'\n", pstateStr(nvp->s));
	fprintf (fp, "  perm='%s'\n", permStr(nvp->p));
	fprintf (fp, "  timeout='%g'\n", nvp->timeout);
	fprintf (fp, "  timestamp='%s'\n", ts);
	if (fmt)
	    vsmessage (fp, fmt, ap);
	fprintf (fp, ">\n");

	for (i = 0; i < nvp->nnp; i++) {
	    INumber *np = &nvp->np[i];
	    fprintf (fp, "  <defNumber\n");
	    fprintf (fp, "    name='%s'\n", np->name);
	    fprintf (fp, "    label='%s'\n", np->label);
	    fprintf (fp, "    format='%s'\n", np->format);
	    fprintf (fp, "    min='%.20g'\n", np->min);
	    fprintf (fp, "    max='%.20g'\n", np->max);
	    fprintf (fp, "    step='%.20g'>\n", np->step);
	    fprintf (fp, "      %.20g\n", np->value);
	    fprintf (fp, "  </defNumber>\n");
	}

	fprintf (fp, "</defNumberVector>\n");
	fflush (fp);

	fmutexUnlock (fp);
}

void
IDFDefNumber (FILE *fp, const INumberVectorProperty *nvp, const char *fmt, ...)
{
	va_list ap;
	va_start (ap, fmt);
	_IDFDefNumber (fp, nvp, fmt, ap);
	va_end (ap);
}

void
IDDefNumber (const INumberVectorProperty *nvp, const char *fmt, ...)
{
	va_list ap;
	va_start (ap, fmt);
	_IDFDefNumber (stdout, nvp, fmt, ap);
	va_end (ap);
}

/* tell client to create a new switch vector property */
static void
_IDFDefSwitch (FILE *fp, const ISwitchVectorProperty *svp, const char *fmt, va_list ap)
{
	char ts[64];
	int i;

	fmutexLock (fp);

	timestamp (ts, sizeof(ts));

	xmlv1(fp);
	fprintf (fp, "<defSwitchVector\n");
	fprintf (fp, "  device='%s'\n", svp->device);
	fprintf (fp, "  name='%s'\n", svp->name);
	fprintf (fp, "  label='%s'\n", svp->label);
	fprintf (fp, "  group='%s'\n", svp->group);
	fprintf (fp, "  state='%s'\n", pstateStr(svp->s));
	fprintf (fp, "  perm='%s'\n", permStr(svp->p));
	fprintf (fp, "  rule='%s'\n", ruleStr (svp->r));
	fprintf (fp, "  timeout='%g'\n", svp->timeout);
	fprintf (fp, "  timestamp='%s'\n", ts);
	if (fmt)
	    vsmessage (fp, fmt, ap);
	fprintf (fp, ">\n");

	for (i = 0; i < svp->nsp; i++) {
	    ISwitch *sp = &svp->sp[i];
	    fprintf (fp, "  <defSwitch\n");
	    fprintf (fp, "    name='%s'\n", sp->name);
	    fprintf (fp, "    label='%s'>\n", sp->label);
	    fprintf (fp, "      %s\n", sstateStr(sp->s));
	    fprintf (fp, "  </defSwitch>\n");
	}

	fprintf (fp, "</defSwitchVector>\n");
	fflush (fp);

	fmutexUnlock (fp);
}

void
IDFDefSwitch (FILE *fp, const ISwitchVectorProperty *svp, const char *fmt, ...)
{
	va_list ap;
	va_start (ap, fmt);
	_IDFDefSwitch (fp, svp, fmt, ap);
	va_end (ap);
}

void
IDDefSwitch (const ISwitchVectorProperty *svp, const char *fmt, ...)
{
	va_list ap;
	va_start (ap, fmt);
	_IDFDefSwitch (stdout, svp, fmt, ap);
	va_end (ap);
}

/* tell client to create a new lights vector property */
static void
_IDFDefLight (FILE *fp, const ILightVectorProperty *lvp, const char *fmt, va_list ap)
{
	char ts[64];
	int i;

	fmutexLock (fp);

	timestamp (ts, sizeof(ts));

	xmlv1(fp);
	fprintf (fp, "<defLightVector\n");
	fprintf (fp, "  device='%s'\n", lvp->device);
	fprintf (fp, "  name='%s'\n", lvp->name);
	fprintf (fp, "  label='%s'\n", lvp->label);
	fprintf (fp, "  group='%s'\n", lvp->group);
	fprintf (fp, "  state='%s'\n", pstateStr(lvp->s));
	fprintf (fp, "  timestamp='%s'\n", ts);
	if (fmt)
	    vsmessage (fp, fmt, ap);
	fprintf (fp, ">\n");

	for (i = 0; i < lvp->nlp; i++) {
	    ILight *lp = &lvp->lp[i];
	    fprintf (fp, "  <defLight\n");
	    fprintf (fp, "    name='%s'\n", lp->name);
	    fprintf (fp, "    label='%s'>\n", lp->label);
	    fprintf (fp, "      %s\n", pstateStr(lp->s));
	    fprintf (fp, "  </defLight>\n");
	}

	fprintf (fp, "</defLightVector>\n");
	fflush (fp);

	fmutexUnlock (fp);
}

void
IDFDefLight (FILE *fp, const ILightVectorProperty *lvp, const char *fmt, ...)
{
	va_list ap;
	va_start (ap, fmt);
	_IDFDefLight (fp, lvp, fmt, ap);
	va_end (ap);
}

void
IDDefLight (const ILightVectorProperty *lvp, const char *fmt, ...)
{
	va_list ap;
	va_start (ap, fmt);
	_IDFDefLight (stdout, lvp, fmt, ap);
	va_end (ap);
}

/* tell client to create a new BLOB vector property */
static void
_IDFDefBLOB (FILE *fp, const IBLOBVectorProperty *bvp, const char *fmt, va_list ap)
{
	char ts[64];
	int i;

	fmutexLock (fp);

	timestamp (ts, sizeof(ts));

	xmlv1(fp);
	fprintf (fp, "<defBLOBVector\n");
	fprintf (fp, "  device='%s'\n", bvp->device);
	fprintf (fp, "  name='%s'\n", bvp->name);
	fprintf (fp, "  label='%s'\n", bvp->label);
	fprintf (fp, "  group='%s'\n", bvp->group);
	fprintf (fp, "  state='%s'\n", pstateStr(bvp->s));
	fprintf (fp, "  perm='%s'\n", permStr(bvp->p));
	fprintf (fp, "  timeout='%g'\n", bvp->timeout);
	fprintf (fp, "  timestamp='%s'\n", ts);
	if (fmt)
	    vsmessage (fp, fmt, ap);
	fprintf (fp, ">\n");

	for (i = 0; i < bvp->nbp; i++) {
	    IBLOB *bp = &bvp->bp[i];
	    fprintf (fp, "  <defBLOB\n");
	    fprintf (fp, "    name='%s'\n", bp->name);
	    fprintf (fp, "    label='%s'\n", bp->label);
	    fprintf (fp, "  />\n");
	}

	fprintf (fp, "</defBLOBVector>\n");
	fflush (fp);

	fmutexUnlock (fp);
}

void
IDFDefBLOB (FILE *fp, const IBLOBVectorProperty *nvp, const char *fmt, ...)
{
	va_list ap;
	va_start (ap, fmt);
	_IDFDefBLOB (fp, nvp, fmt, ap);
	va_end (ap);
}

void
IDDefBLOB (const IBLOBVectorProperty *nvp, const char *fmt, ...)
{
	va_list ap;
	va_start (ap, fmt);
	_IDFDefBLOB (stdout, nvp, fmt, ap);
	va_end (ap);
}


/* tell client to update an existing text vector property */
static void
_IDFSetText (FILE *fp, const ITextVectorProperty *tvp, const char *fmt, va_list ap)
{
	char ts[64];
	int i;

	fmutexLock (fp);

	timestamp (ts, sizeof(ts));

	xmlv1(fp);
	fprintf (fp, "<setTextVector\n");
	fprintf (fp, "  device='%s'\n", tvp->device);
	fprintf (fp, "  name='%s'\n", tvp->name);
	fprintf (fp, "  state='%s'\n", pstateStr(tvp->s));
	fprintf (fp, "  timeout='%g'\n", tvp->timeout);
	fprintf (fp, "  timestamp='%s'\n", ts);
	if (fmt)
	    vsmessage (fp, fmt, ap);
	fprintf (fp, ">\n");

	for (i = 0; i < tvp->ntp; i++) {
	    IText *tp = &tvp->tp[i];
	    fprintf (fp, "  <oneText name='%s'>\n", tp->name);
	    fprintf (fp, "      %s\n", tp->text ? entityXML(tp->text) : "");
	    fprintf (fp, "  </oneText>\n");
	}

	fprintf (fp, "</setTextVector>\n");
	fflush (fp);

	fmutexUnlock (fp);
}

void
IDFSetText (FILE *fp, const ITextVectorProperty *tvp, const char *fmt, ...)
{
	va_list ap;
	va_start (ap, fmt);
	_IDFSetText (fp, tvp, fmt, ap);
	va_end (ap);
}

void
IDSetText (const ITextVectorProperty *tvp, const char *fmt, ...)
{
	va_list ap;
	va_start (ap, fmt);
	_IDFSetText (stdout, tvp, fmt, ap);
	va_end (ap);
}

/* tell client to update an existing numeric vector property */
static void
_IDFSetNumber (FILE *fp, const INumberVectorProperty *nvp, const char *fmt, va_list ap)
{
	char ts[64];
	int i;

	fmutexLock (fp);

	timestamp (ts, sizeof(ts));

	xmlv1(fp);
	fprintf (fp, "<setNumberVector\n");
	fprintf (fp, "  device='%s'\n", nvp->device);
	fprintf (fp, "  name='%s'\n", nvp->name);
	fprintf (fp, "  state='%s'\n", pstateStr(nvp->s));
	fprintf (fp, "  timeout='%g'\n", nvp->timeout);
	fprintf (fp, "  timestamp='%s'\n", ts);
	if (fmt)
	    vsmessage (fp, fmt, ap);
	fprintf (fp, ">\n");

	for (i = 0; i < nvp->nnp; i++) {
	    INumber *np = &nvp->np[i];
	    fprintf (fp, "  <oneNumber name='%s'>\n", np->name);
	    fprintf (fp, "      %.20g\n", np->value);
	    fprintf (fp, "  </oneNumber>\n");
	}

	fprintf (fp, "</setNumberVector>\n");
	fflush (fp);

	fmutexUnlock (fp);
}

void
IDFSetNumber (FILE *fp, const INumberVectorProperty *nvp, const char *fmt, ...)
{
	va_list ap;
	va_start (ap, fmt);
	_IDFSetNumber (fp, nvp, fmt, ap);
	va_end (ap);
}

void
IDSetNumber (const INumberVectorProperty *nvp, const char *fmt, ...)
{
	va_list ap;
	va_start (ap, fmt);
	_IDFSetNumber (stdout, nvp, fmt, ap);
	va_end (ap);
}

/* tell client to update an existing switch vector property */
static void
_IDFSetSwitch (FILE *fp, const ISwitchVectorProperty *svp, const char *fmt, va_list ap)
{
	char ts[64];
	int i;

	fmutexLock (fp);

	timestamp (ts, sizeof(ts));

	xmlv1(fp);
	fprintf (fp, "<setSwitchVector\n");
	fprintf (fp, "  device='%s'\n", svp->device);
	fprintf (fp, "  name='%s'\n", svp->name);
	fprintf (fp, "  state='%s'\n", pstateStr(svp->s));
	fprintf (fp, "  timeout='%g'\n", svp->timeout);
	fprintf (fp, "  timestamp='%s'\n", ts);
	if (fmt)
	    vsmessage (fp, fmt, ap);
	fprintf (fp, ">\n");

	for (i = 0; i < svp->nsp; i++) {
	    ISwitch *sp = &svp->sp[i];
	    fprintf (fp, "  <oneSwitch name='%s'>\n", sp->name);
	    fprintf (fp, "      %s\n", sstateStr(sp->s));
	    fprintf (fp, "  </oneSwitch>\n");
	}

	fprintf (fp, "</setSwitchVector>\n");
	fflush (fp);

	fmutexUnlock (fp);
}

void
IDFSetSwitch (FILE *fp, const ISwitchVectorProperty *svp, const char *fmt, ...)
{
	va_list ap;
	va_start (ap, fmt);
	_IDFSetSwitch (fp, svp, fmt, ap);
	va_end (ap);
}

void
IDSetSwitch (const ISwitchVectorProperty *svp, const char *fmt, ...)
{
	va_list ap;
	va_start (ap, fmt);
	_IDFSetSwitch (stdout, svp, fmt, ap);
	va_end (ap);
}


/* tell client to update an existing lights vector property */
static void
_IDFSetLight (FILE *fp, const ILightVectorProperty *lvp, const char *fmt, va_list ap)
{
	char ts[64];
	int i;

	fmutexLock (fp);

	timestamp (ts, sizeof(ts));

	xmlv1(fp);
	fprintf (fp, "<setLightVector\n");
	fprintf (fp, "  device='%s'\n", lvp->device);
	fprintf (fp, "  name='%s'\n", lvp->name);
	fprintf (fp, "  state='%s'\n", pstateStr(lvp->s));
	fprintf (fp, "  timestamp='%s'\n", ts);
	if (fmt)
	    vsmessage (fp, fmt, ap);
	fprintf (fp, ">\n");

	for (i = 0; i < lvp->nlp; i++) {
	    ILight *lp = &lvp->lp[i];
	    fprintf (fp, "  <oneLight name='%s'>\n", lp->name);
	    fprintf (fp, "      %s\n", pstateStr(lp->s));
	    fprintf (fp, "  </oneLight>\n");
	}

	fprintf (fp, "</setLightVector>\n");
	fflush (fp);

	fmutexUnlock (fp);
}

void
IDFSetLight (FILE *fp, const ILightVectorProperty *lvp, const char *fmt, ...)
{
	va_list ap;
	va_start (ap, fmt);
	_IDFSetLight (fp, lvp, fmt, ap);
	va_end (ap);
}

void
IDSetLight (const ILightVectorProperty *lvp, const char *fmt, ...)
{
	va_list ap;
	va_start (ap, fmt);
	_IDFSetLight (stdout, lvp, fmt, ap);
	va_end (ap);
}

/* tell client to update an existing BLOB vector property */
static void
_IDFSetBLOB (FILE *fp, const IBLOBVectorProperty *bvp, const char *fmt, va_list ap)
{
	char ts[64];
	int i;

	fmutexLock (fp);

	timestamp (ts, sizeof(ts));

	xmlv1(fp);
	fprintf (fp, "<setBLOBVector\n");
	fprintf (fp, "  device='%s'\n", bvp->device);
	fprintf (fp, "  name='%s'\n", bvp->name);
	fprintf (fp, "  state='%s'\n", pstateStr(bvp->s));
	fprintf (fp, "  timeout='%g'\n", bvp->timeout);
	fprintf (fp, "  timestamp='%s'\n", ts);
	if (fmt)
	    vsmessage (fp, fmt, ap);
	fprintf (fp, ">\n");

	for (i = 0; i < bvp->nbp; i++) {
	    IBLOB *bp = &bvp->bp[i];
	    unsigned char *encblob;
	    int l;

	    fprintf (fp, "  <oneBLOB\n");
	    fprintf (fp, "    name='%s'\n", bp->name);
	    fprintf (fp, "    size='%d'\n", bp->size);
	    fprintf (fp, "    format='%s'>\n", bp->format);

	    encblob = (unsigned char *) malloc (4*bp->bloblen/3+4);
	    l = to64frombits(encblob, (const unsigned char *)bp->blob, bp->bloblen);

	    /* pretty but about 2x slower
	    for (j = 0; j < l; j += 72)
		fprintf (fp, "%.72s\n", encblob+j);
	    */
	    fwrite (encblob, 1, l, fp);

	    free (encblob);

	    fprintf (fp, "  </oneBLOB>\n");
	}

	fprintf (fp, "</setBLOBVector>\n");
	fflush (fp);

	fmutexUnlock (fp);
}

void
IDFSetBLOB (FILE *fp, const IBLOBVectorProperty *bvp, const char *fmt, ...)
{
	va_list ap;
	va_start (ap, fmt);
	_IDFSetBLOB (fp, bvp, fmt, ap);
	va_end (ap);
}

void
IDSetBLOB (const IBLOBVectorProperty *bvp, const char *fmt, ...)
{
	va_list ap;
	va_start (ap, fmt);
	_IDFSetBLOB (stdout, bvp, fmt, ap);
	va_end (ap);
}


/* send client a message for a specific device or at large if !dev */
static void
_IDFMessage (FILE *fp, const char *dev, const char *fmt, va_list ap)
{
	char ts[64];

	fmutexLock (fp);

	timestamp (ts, sizeof(ts));

	xmlv1(fp);
	fprintf (fp, "<message\n");
	if (dev)
	    fprintf (fp, " device='%s'\n", dev);
	fprintf (fp, "  timestamp='%s'\n", ts);
	if (fmt)
	    vsmessage (fp, fmt, ap);
	fprintf (fp, "/>\n");
	fflush (fp);

	fmutexUnlock (fp);
}

void
IDFMessage (FILE *fp, const char *dev, const char *fmt, ...)
{
	va_list ap;
	va_start (ap, fmt);
	_IDFMessage (fp, dev, fmt, ap);
	va_end (ap);
}

void
IDMessage (const char *dev, const char *fmt, ...)
{
	va_list ap;
	va_start (ap, fmt);
	_IDFMessage (stdout, dev, fmt, ap);
	va_end (ap);
}

/* tell Client to delete the property with given name on given device, or
 * entire device if !name
 */
static void
_IDFDelete (FILE *fp, const char *dev, const char *name, const char *fmt, va_list ap)
{
	char ts[64];

	fmutexLock (fp);

	timestamp (ts, sizeof(ts));

	xmlv1(fp);
	fprintf (fp, "<delProperty\n  device='%s'\n", dev);
	if (name)
	    fprintf (fp, " name='%s'\n", name);
	fprintf (fp, "  timestamp='%s'\n", ts);
	if (fmt)
	    vsmessage (fp, fmt, ap);
	fprintf (fp, "/>\n");
	fflush (fp);

	fmutexUnlock (fp);
}

void
IDFDelete (FILE *fp, const char *dev, const char *name, const char *fmt, ...)
{
	va_list ap;
	va_start (ap, fmt);
	_IDFDelete (fp, dev, name, fmt, ap);
	va_end (ap);
}

void
IDDelete (const char *dev, const char *name, const char *fmt, ...)
{
	va_list ap;
	va_start (ap, fmt);
	_IDFDelete (stdout, dev, name, fmt, ap);
	va_end (ap);
}


/* log message to our stderr which the indiserver will capture
 * and store in its log with a time stamp and an indication of who sent it.
 */
void
IDLog (const char *fmt, ...)
{
	fmutexLock (stderr);

  	va_list ap;
	va_start (ap, fmt);
	vfprintf (stderr, fmt, ap);
	va_end (ap);

	fmutexUnlock (stderr);
}

/* tell indiserver we want to snoop on the given device/property.
 * name ignored if NULL or empty.
 */
void
IDFSnoopDevice (FILE *fp, const char *snooped_device_name, const char *snooped_property_name)
{
	fmutexLock (fp);

	xmlv1(fp);
	if (snooped_property_name && snooped_property_name[0])
	    fprintf (fp, "<getProperties version='%g' device='%s' name='%s'/>\n",
				    INDIV, snooped_device_name, snooped_property_name);
	else
	    fprintf (fp, "<getProperties version='%g' device='%s'/>\n", INDIV, snooped_device_name);
	fflush (fp);

	fmutexUnlock (fp);
}

void
IDSnoopDevice (const char *snooped_device_name, const char *snooped_property_name)
{
	IDFSnoopDevice (stdout, snooped_device_name, snooped_property_name);
}

/* tell indiserver whether we want BLOBs from the given snooped device.
 * silently ignored if given device is not already registered for snooping.
 */
void 
IDFSnoopBLOBs (FILE *fp, char *snooped_device_name, BLOBHandling bh)
{
	const char *how;

	fmutexLock (fp);

	switch (bh) {
	case B_NEVER: how = "Never"; break;
	case B_ALSO:  how = "Also";  break;
	case B_ONLY:  how = "Only";  break;
	default: return;
	}

	xmlv1(fp);
	fprintf (fp, "<enableBLOB device='%s'>%s</enableBLOB>\n",
						snooped_device_name, how);
	fflush (fp);

	fmutexUnlock (fp);
}

void 
IDSnoopBLOBs (char *snooped_device_name, BLOBHandling bh)
{
	IDFSnoopBLOBs (stdout, snooped_device_name, bh);
}

/* print message to fp
 * N.B. can not vfprint directly, must go through entityXML()
 */
static void
vsmessage (FILE *fp, const char *fmt, va_list ap)
{
	char msg[1024];
	vsnprintf (msg, sizeof(msg), fmt, ap);
	fprintf (fp, "  message='%s'\n", entityXML(msg));
}

/******************************************************
 * "INDI" wrappers to the more generic eventloop facility.
 */

int
IEAddCallback (int readfiledes, IE_CBF *fp, void *p)
{
	return (addCallback (readfiledes, (CBF*)fp, p));
}

void
IERmCallback (int callbackid)
{
	rmCallback (callbackid);
}

int
IEAddTimer (int millisecs, IE_TCF *fp, void *p)
{
	return (addTimer (millisecs, (TCF*)fp, p));
}

void
IERmTimer (int timerid)
{
	rmTimer (timerid);
}

int
IEAddWorkProc (IE_WPF *fp, void *p)
{
	return (addWorkProc ((WPF*)fp, p));
}

void
IERmWorkProc (int workprocid)
{
	rmWorkProc (workprocid);
}

int
IEDeferLoop (int maxms, int *flagp)
{
	return (deferLoop (maxms, flagp));
}

int
IEDeferLoop0 (int maxms, int *flagp)
{
	return (deferLoop0 (maxms, flagp));
}


/***************************************
 * convenience IU functions
 */


/* find a member of an IText vector, else NULL */
IText *
IUFindText  (const ITextVectorProperty *tvp, const char *name)
{
	int i;

	for (i = 0; i < tvp->ntp; i++)
	    if (strcmp (tvp->tp[i].name, name) == 0)
		return (&tvp->tp[i]);
	fprintf (stderr, "No IText '%s' in %s.%s\n",name,tvp->device,tvp->name);
	return (NULL);
}

/* convenience function for use in your implementation of ISNewText().
 * given a candidate Text and the args from ISNewText, fill in the
 * elements and return 0 else return -1 if it's the wrong candidate or not
 * all the elements are present.
 */
int
IUCrackText (ITextVectorProperty *tvp, const char *dev, const char *name,
char *texts[], char *names[], int n)
{
	int i, j;

	if (strcmp(dev,tvp->device) || strcmp(name,tvp->name))
	    return (-1);	/* not this property */

	for (i = 0; i < tvp->ntp; i++) {
	    for (j = 0; j < n; j++) {
		if (!strcmp(tvp->tp[i].name, names[j])) {
		    IUSaveText (&tvp->tp[i], texts[j]);
		    break;
		}
	    }
	    if (j == n)
		return (-1);	/* missing element */
	}
	return (0);
}

/* find a member of an INumber vector, else NULL */
INumber *
IUFindNumber(const INumberVectorProperty *nvp, const char *name)
{
	int i;

	for (i = 0; i < nvp->nnp; i++)
	    if (strcmp (nvp->np[i].name, name) == 0)
		return (&nvp->np[i]);
	fprintf(stderr,"No INumber '%s' in %s.%s\n",name,nvp->device,nvp->name);
	return (NULL);
}

/* convenience function for use in your implementation of ISNewNumber().
 * given a candidate Number and the args from ISNewNumber, fill in the
 * elements and return 0 else return -1 if it's the wrong candidate or not
 * all the elements are present.
 */
int
IUCrackNumber (INumberVectorProperty *nvp, const char *dev, const char *name,
double *doubles, char *names[], int n)
{
	int i, j;

	if (strcmp(dev,nvp->device) || strcmp(name,nvp->name))
	    return (-1);	/* not this property */

	for (i = 0; i < nvp->nnp; i++) {
	    for (j = 0; j < n; j++) {
		if (!strcmp(nvp->np[i].name, names[j])) {
		    nvp->np[i].value = doubles[j];
		    break;
		}
	    }
	    if (j == n)
		return (-1);	/* missing element */
	}
	return (0);
}

/* convenience function for use in your implementation of ISNewSwitch().
 * given a candidate Switch and the args from ISNewSwitch, fill in the
 * elements and return 0 else return -1 if it's the wrong candidate or not
 * all the elements are present or elements don't honor the given ISRule.
 */
int
IUCrackSwitch (ISwitchVectorProperty *svp, const char *dev, const char *name,
ISState *states, char *names[], int n)
{
	int i, j, n_on;

	if (strcmp(dev,svp->device) || strcmp(name,svp->name))
	    return (-1);	/* not this property */

	for (n_on = i = 0; i < svp->nsp; i++) {
	    for (j = 0; j < n; j++) {
		if (!strcmp(svp->sp[i].name, names[j])) {
                    svp->sp[i].s = states[j];           // save state
                    if (svp->sp[i].s == ISS_ON)         // count n on
                        n_on++;
		    break;
		}
	    }
	    if (j == n)
		return (-1);	/* missing element */
	}

        switch (svp->r) {
        case ISR_1OFMANY:
            if (n_on != 1)
                return (-1);
            break;

        case ISR_ATMOST1:
            if (n_on > 1)
                return (-1);
            break;

        case ISR_NOFMANY:
            /* don't care */
            break;
        }

	return (0);
}

/* find a member of an ISwitch vector, else NULL */
ISwitch *
IUFindSwitch(const ISwitchVectorProperty *svp, const char *name)
{
	int i;

	for (i = 0; i < svp->nsp; i++)
	    if (strcmp (svp->sp[i].name, name) == 0)
		return (&svp->sp[i]);
	fprintf(stderr,"No ISwitch '%s' in %s.%s\n",name,svp->device,svp->name);
	return (NULL);
}

/* find an ON member of an ISwitch vector, else NULL.
 * N.B. user must make sense of result with ISRule in mind.
 */
ISwitch *
IUFindOnSwitch(const ISwitchVectorProperty *svp)
{
	int i;

	for (i = 0; i < svp->nsp; i++)
	    if (svp->sp[i].s == ISS_ON)
		return (&svp->sp[i]);
	fprintf(stderr, "No ISwitch On in %s.%s\n", svp->device, svp->name);
	return (NULL);
}

/* Set all switches to off */
void 
IUResetSwitches(const ISwitchVectorProperty *svp)
{
	int i;
    
	for (i = 0; i < svp->nsp; i++)
	    svp->sp[i].s = ISS_OFF;
}

/* convenience function for use in your implementation of ISNewBLOB().
 * given a candidate BLOB and the args from ISNewBLOB, fill in the
 * elements and return 0 else return -1 if it's the wrong candidate or all elements not present.
 * N.B. we set each bvp->bp[i].size but we neither enforce nor interpret format.
 */
int
IUCrackBLOB (IBLOBVectorProperty *bvp, const char *dev, const char *name, int sizes[],
    int blobsizes[], char *blobs[], char *formats[], char *names[], int n)
{
	int i, j;

	if (strcmp(dev,bvp->device) || strcmp(name,bvp->name))
	    return (-1);	/* not this property */

	for (i = 0; i < bvp->nbp; i++) {
	    for (j = 0; j < n; j++) {
		if (!strcmp(bvp->bp[i].name, names[j])) {
		    bvp->bp[i].blob = realloc (bvp->bp[i].blob, blobsizes[j]);
                    memcpy (bvp->bp[i].blob, blobs[j], blobsizes[j]);
                    bvp->bp[i].bloblen = blobsizes[j];
                    bvp->bp[i].size = sizes[j];
                    bvp->bp[i].bvp = bvp;
		    break;
		}
	    }
	    if (j == n)
		return (-1);	/* missing element */
	}
	return (0);
}

/* use this to set the text value of an IText.
 * save malloced copy of resulting printf-style format in tp->text, reusing if not first time.
 * N.B. don't mix using this with setting tp->text directly!
 */
void
IUSaveText (IText *tp, const char *fmt, ...)
{
        /* format the message, if any */
        char msg[2048];
        int msgl;
        if (fmt == NULL) {
            msg[0] = '\0';
            msgl = 0;
        } else {
            va_list ap;
            va_start (ap, fmt);
            msgl = vsnprintf (msg, sizeof(msg), fmt, ap);
            va_end (ap);
        }

	/* resuse for new message */
	tp->text = strcpy ((char *) realloc (tp->text, msgl+1), msg);
}


/*****************************************************************************
 * convenience functions for use in your implementation of ISSnoopDevice().
 */

/* crack the snooped driver setNumberVector or defNumberVector message into
 * the given INumberVectorProperty.
 * return 0 if type, device and name match and all members are present, else
 * return -1
 */
int
IUSnoopNumber (XMLEle *root, INumberVectorProperty *nvp)
{
	char *dev, *name;
	XMLEle *ep;
	int i;

	/* check and crack type, device, name and state */
	if (strcmp (tagXMLEle(root)+3, "NumberVector") ||
					crackDN (root, &dev, &name, NULL) < 0)
	    return (-1);
	if (strcmp (dev, nvp->device) || strcmp (name, nvp->name))
	    return (-1);	/* not this property */
	(void) crackIPState (findXMLAttValu (root,"state"), &nvp->s);

	/* match each INumber with a oneNumber */
	for (i = 0; i < nvp->nnp; i++) {
	    for (ep = nextXMLEle(root,1); ep; ep = nextXMLEle(root,0)) {
		if (!strcmp (tagXMLEle(ep)+3, "Number") &&
			!strcmp (nvp->np[i].name, findXMLAttValu(ep, "name"))) {
		    if (sexagesimal (pcdataXMLEle(ep), &nvp->np[i].value) < 0)
			return (-1);	/* bad number format */
		    break;
		}
	    }
	    if (!ep)
		return (-1);	/* element not found */
	}

	/* ok */
	return (0);
}

/* crack the snooped driver setTextVector or defTextVector message into
 * the given ITextVectorProperty.
 * return 0 if type, device and name match and all members are present, else
 * return -1
 */
int
IUSnoopText (XMLEle *root, ITextVectorProperty *tvp)
{
	char *dev, *name;
	XMLEle *ep;
	int i;

	/* check and crack type, device, name and state */
	if (strcmp (tagXMLEle(root)+3, "TextVector") ||
					crackDN (root, &dev, &name, NULL) < 0)
	    return (-1);
	if (strcmp (dev, tvp->device) || strcmp (name, tvp->name))
	    return (-1);	/* not this property */
	(void) crackIPState (findXMLAttValu (root,"state"), &tvp->s);

	/* match each IText with a oneText */
	for (i = 0; i < tvp->ntp; i++) {
	    for (ep = nextXMLEle(root,1); ep; ep = nextXMLEle(root,0)) {
		if (!strcmp (tagXMLEle(ep)+3, "Text") &&
			!strcmp (tvp->tp[i].name, findXMLAttValu(ep, "name"))) {
		    IUSaveText (&tvp->tp[i], pcdataXMLEle(ep));
		    break;
		}
	    }
	    if (!ep)
		return (-1);	/* element not found */
	}

	/* ok */
	return (0);
}

/* crack the snooped driver setLightVector or defLightVector message into
 * the given ILightVectorProperty. it is not necessary that all ILight names
 * be found.
 * return 0 if type, device and name match, else return -1.
 */
int
IUSnoopLight (XMLEle *root, ILightVectorProperty *lvp)
{
	char *dev, *name;
	XMLEle *ep;
	int i;

	/* check and crack type, device, name and state */
	if (strcmp (tagXMLEle(root)+3, "LightVector") ||
					crackDN (root, &dev, &name, NULL) < 0)
	    return (-1);
	if (strcmp (dev, lvp->device) || strcmp (name, lvp->name))
	    return (-1);	/* not this property */
	(void) crackIPState (findXMLAttValu (root,"state"), &lvp->s);

	/* match each oneLight with one ILight */
	for (ep = nextXMLEle(root,1); ep; ep = nextXMLEle(root,0)) {
	    if (!strcmp (tagXMLEle(ep)+3, "Light")) {
		char *name = findXMLAttValu (ep, "name");
		for (i = 0; i < lvp->nlp; i++) {
		    if (!strcmp (lvp->lp[i].name, name)) {
			if (crackIPState(pcdataXMLEle(ep), &lvp->lp[i].s) < 0) {
			    return (-1);	/* unrecognized state */
			}
			break;
		    }
		}
	    }
	}

	/* ok */
	return (0);
}

/* crack the snooped driver setSwitchVector or defSwitchVector message into the
 * given ISwitchVectorProperty. it is not necessary that all ISwitch names be
 * found.
 * return 0 if type, device and name match, else return -1.
 */
int
IUSnoopSwitch (XMLEle *root, ISwitchVectorProperty *svp)
{
	char *dev, *name;
	XMLEle *ep;
	int i;

	/* check and crack type, device, name and state */
	if (strcmp (tagXMLEle(root)+3, "SwitchVector") ||
					crackDN (root, &dev, &name, NULL) < 0)
	    return (-1);
	if (strcmp (dev, svp->device) || strcmp (name, svp->name))
	    return (-1);	/* not this property */
	(void) crackIPState (findXMLAttValu (root,"state"), &svp->s);

	/* match each oneSwitch with one ISwitch */
	for (ep = nextXMLEle(root,1); ep; ep = nextXMLEle(root,0)) {
	    if (!strcmp (tagXMLEle(ep)+3, "Switch")) {
		char *name = findXMLAttValu (ep, "name");
		for (i = 0; i < svp->nsp; i++) {
		    if (!strcmp (svp->sp[i].name, name)) {
			if (crackISState(pcdataXMLEle(ep), &svp->sp[i].s) < 0) {
			    return (-1);	/* unrecognized state */
			}
			break;
		    }
		}
	    }
	}

	/* ok */
	return (0);
}

/* crack the snooped driver setBLOBVector message into the given
 * IBLOBVectorProperty. it is not necessary that all IBLOB names be found.
 * return 0 if type, device and name match, else return -1.
 * N.B. we assume any existing blob in bvp has been malloced, which we free
 *   and replace with a newly malloced blob if found.
 */
int
IUSnoopBLOB (XMLEle *root, IBLOBVectorProperty *bvp)
{
	char *dev, *name;
	XMLEle *ep;
	int i;

	/* check and crack type, device, name and state */
	if (strcmp (tagXMLEle(root), "setBLOBVector") ||
					crackDN (root, &dev, &name, NULL) < 0)
	    return (-1);
	if (strcmp (dev, bvp->device) || strcmp (name, bvp->name))
	    return (-1);	/* not this property */
	(void) crackIPState (findXMLAttValu (root,"state"), &bvp->s);

	/* match each oneBLOB with one IBLOB */
	for (ep = nextXMLEle(root,1); ep; ep = nextXMLEle(root,0)) {
	    if (!strcmp (tagXMLEle(ep)+3, "BLOB")) {
		char *name = findXMLAttValu (ep, "name");
		for (i = 0; i < bvp->nbp; i++) {
		    IBLOB *bp = &bvp->bp[i];
		    if (!strcmp (bp->name, name)) {
			strcpy (bp->format, findXMLAttValu (ep,"format"));
			bp->size = atoi (findXMLAttValu (ep,"size"));
			bp->bloblen = pcdatalenXMLEle(ep)+1;
			if (bp->blob)
			    free (bp->blob);
			bp->blob = strcpy((char *)malloc(bp->bloblen), pcdataXMLEle(ep));
			break;
		    }
		}
	    }
	}

	/* ok */
	return (0);
}

/* prepare to use fd for incoming INDI messages.
 * return id for use with IERmCallback().
 */
int
IUAddConnection (int fd)
{
	return (IEAddCallback (fd, clientMsgCB, newLilXML()));
}

/* relinquish control to framework
 * only returns if any callback sets eloop_error.
 */
void
IUEventLoop(void)
{
	eventLoop();
}


/* callback when INDI client message arrives on fd.
 * collect and dispatch when see outter element closure.
 * set eloop_error if OS trouble or see incompatable INDI version.
 * arg is the LilXML context for this fd.
 * N.B. not for general use, just used by indidrivermain.c:main().
 */
static void
clientMsgCB (int fd, void *context)
{
	LilXML *clixml = (LilXML *) context;
	char buf[4096], msg[1024], *bp;
	int nr, nr0;

	/* one read */
	nr = read (fd, buf, sizeof(buf));
	if (nr <= 0) {
	    eloop_error = 1;            // set before fprintf in case stderr also in trouble
	    if (nr < 0)
		fprintf (stderr, "INDI clientMsgCB() fd %d: %s\n", fd, strerror(errno));
	    else
		fprintf (stderr, "INDI clientMsgCB() fd %d: EOF\n", fd);
	    return;
	}
	nr0 = nr;

	/* crack and dispatch when complete -- abort if trouble to resync */
	for (bp = buf; nr-- > 0; bp++) {
	    XMLEle *root = readXMLEle (clixml, *bp, msg);
	    if (root) {
		if (dispatch (root, msg) < 0)
		    fprintf (stderr, "dispatch error: %s\n", msg);
		delXMLEle (root);
	    } else if (msg[0]) {
		fprintf (stderr, "XML error: %s\n", msg);
		fprintf (stderr, "XML read: %.*s\n", nr0, buf);
		eloop_error = 1;
		return;
	    }
	}
}

/* crack the given INDI XML element and call driver's IS* entry points as they
 *   are recognized.
 * return 0 if ok else -1 with reason in msg[].
 * N.B. exit if getProperties does not proclaim a compatible version.
 */
static int
dispatch (XMLEle *root, char msg[])
{
	char *rtag = tagXMLEle(root);
	XMLEle *ep;
	int n;

	/* check tag in surmised decreasing order of likelyhood */

	if (!strcmp (rtag, "newNumberVector")) {
	    static double *doubles;
	    static char **names;
	    static int maxn;
	    char *dev, *name;

	    /* pull out device and name */
	    if (crackDN (root, &dev, &name, msg) < 0)
		return (-1);

	    /* seed for reallocs */
	    if (!doubles) {
		doubles = (double *) malloc (1);
		names = (char **) malloc (1);
	    }

	    /* pull out each name/value pair */
	    for (n = 0, ep = nextXMLEle(root,1); ep; ep = nextXMLEle(root,0)) {
		if (strcmp (tagXMLEle(ep), "oneNumber") == 0) {
		    char *e = findXMLAttValu (ep, "name");
		    if (*e) {
			if (n >= maxn) {
			    /* grow for this and another */
			    int newsz = (maxn=n+1)*sizeof(double);
			    doubles = (double *) realloc(doubles,newsz);
			    newsz = maxn*sizeof(char *);
			    names = (char **) realloc (names, newsz);
			}
			if (sexagesimal (pcdataXMLEle(ep), &doubles[n]) < 0)
			    IDLog ("%s.%s.%s: Bad Number format %s\n", dev, name,
							    e, pcdataXMLEle(ep));
			else
			    names[n++] = e;
		    }
		}
	    }

	    /* invoke driver if something to do, but not an error if not */
	    if (n > 0)
		ISNewNumber (dev, name, doubles, names, n);
	    else
		IDLog("%s.%s: newNumberVector with no valid members\n",dev,name);
	    return (0);
	}

	if (!strcmp (rtag, "newSwitchVector")) {
	    static ISState *states;
	    static char **names;
	    static int maxn;
	    char *dev, *name;
	    XMLEle *ep;

	    /* pull out device and name */
	    if (crackDN (root, &dev, &name, msg) < 0)
		return (-1);

	    /* seed for reallocs */
	    if (!states) {
		states = (ISState *) malloc (1);
		names = (char **) malloc (1);
	    }

	    /* pull out each name/state pair */
	    for (n = 0, ep = nextXMLEle(root,1); ep; ep = nextXMLEle(root,0)) {
		if (strcmp (tagXMLEle(ep), "oneSwitch") == 0) {
		    char *e = findXMLAttValu (ep, "name");
		    if (*e) {
			if (n >= maxn) {
			    int newsz = (maxn=n+1)*sizeof(ISState);
			    states = (ISState *) realloc(states, newsz);
			    newsz = maxn*sizeof(char *);
			    names = (char **) realloc (names, newsz);
			}
			if (strcmp (pcdataXMLEle(ep),"On") == 0) {
			    states[n] = ISS_ON;
			    names[n] = e;
			    n++;
			} else if (strcmp (pcdataXMLEle(ep),"Off") == 0) {
			    states[n] = ISS_OFF;
			    names[n] = e;
			    n++;
			} else 
			    IDLog ("%s.%s.%s: must be On or Off: %s\n", dev, name, e,
							    pcdataXMLEle(ep));
		    }
		}
	    }

	    /* invoke driver if something to do, but not an error if not */
	    if (n > 0)
		ISNewSwitch (dev, name, states, names, n);
	    else
		IDLog("%s.%s: newSwitchVector with no valid members\n", dev, name);
	    return (0);
	}

	if (!strcmp (rtag, "newTextVector")) {
	    static char **texts;
	    static char **names;
	    static int maxn;
	    char *dev, *name;

	    /* pull out device and name */
	    if (crackDN (root, &dev, &name, msg) < 0)
		return (-1);

	    /* seed for reallocs */
	    if (!texts) {
		texts = (char **) malloc (1);
		names = (char **) malloc (1);
	    }

	    /* pull out each name/text pair */
	    for (n = 0, ep = nextXMLEle(root,1); ep; ep = nextXMLEle(root,0)) {
		if (strcmp (tagXMLEle(ep), "oneText") == 0) {
		    char *e = findXMLAttValu (ep, "name");
		    if (*e) {
			if (n >= maxn) {
			    int newsz = (maxn=n+1)*sizeof(char *);
			    texts = (char **) realloc (texts, newsz);
			    names = (char **) realloc (names, newsz);
			}
			texts[n] = pcdataXMLEle(ep);
			names[n] = e;
			n++;
		    }
		}
	    }

	    /* invoke driver if something to do, but not an error if not */
	    if (n > 0)
		ISNewText (dev, name, texts, names, n);
	    else
		IDLog ("%s.%s: newTextVector with no valid members\n",dev,name);
	    return (0);
	}

	if (!strcmp (rtag, "newBLOBVector")) {
	    static char **blobs;
	    static char **names;
	    static char **formats;
	    static int *blobsizes;
	    static int *sizes;
	    static int maxn;
	    char *dev, *name;
	    int i;

	    /* pull out device and name */
	    if (crackDN (root, &dev, &name, msg) < 0)
		return (-1);

	    /* seed for reallocs */
	    if (!blobs) {
		blobs = (char **) malloc (1);
		names = (char **) malloc (1);
		formats = (char **) malloc (1);
		blobsizes = (int *) malloc (1);
		sizes = (int *) malloc (1);
	    }

	    /* pull out each name/BLOB pair, decode */
	    for (n = 0, ep = nextXMLEle(root,1); ep; ep = nextXMLEle(root,0)) {
		if (strcmp (tagXMLEle(ep), "oneBLOB") == 0) {
		    char *na = findXMLAttValu (ep, "name");
		    char *fa = findXMLAttValu (ep, "format");
		    char *sa = findXMLAttValu (ep, "size");
		    if (*na && *fa && *sa) {
			if (n >= maxn) {
			    int newsz = (maxn=n+1)*sizeof(char *);
			    blobs = (char **) realloc (blobs, newsz);
			    names = (char **) realloc (names, newsz);
			    formats = (char **) realloc(formats,newsz);
			    newsz = maxn*sizeof(int);
			    sizes = (int *) realloc(sizes,newsz);
			    blobsizes = (int *) realloc(blobsizes,newsz);
			}
			blobs[n] = (char *)malloc (3*pcdatalenXMLEle(ep)/4);
			blobsizes[n] = from64tobits(blobs[n], pcdataXMLEle(ep));
			names[n] = na;
			formats[n] = fa;
			sizes[n] = atoi(sa);
			n++;
		    }
		}
	    }

	    /* invoke driver if something to do, but not an error if not */
	    if (n > 0) {
		ISNewBLOB (dev, name, sizes, blobsizes, blobs, formats,names,n);
		for (i = 0; i < n; i++)
		    free (blobs[i]);
	    } else
		IDLog ("%s.%s: newBLOBVector with no valid members\n",dev,name);
	    return (0);
	}

	if (!strcmp (rtag, "getProperties")) {
	    XMLAtt *vp, *dp, *np;
	    double v;

	    /* check version */
	    vp = findXMLAtt (root, "version");
	    if (!vp) {
		fprintf (stderr, "getProperties missing version\n");
		exit(1);
	    }
	    v = atof (valuXMLAtt(vp));
	    if (v > INDIV) {
		fprintf (stderr, "getProperties wants version %g > %g\n", v, INDIV);
		exit(1);
	    }

	    /* ok */
	    dp = findXMLAtt (root, "device");
	    np = findXMLAtt (root, "name");
	    ISGetProperties (dp ? valuXMLAtt(dp) : NULL, np ? valuXMLAtt(np) : NULL);
	    return (0);
	}

	/* other commands might be from a snooped device.
	 * we don't know here which devices are being snooped so we send
	 * all remaining valid messages
	 */
	if (        !strcmp (rtag, "setNumberVector") ||
		    !strcmp (rtag, "setTextVector") ||
		    !strcmp (rtag, "setLightVector") ||
		    !strcmp (rtag, "setSwitchVector") ||
		    !strcmp (rtag, "setBLOBVector") ||
		    !strcmp (rtag, "defNumberVector") ||
		    !strcmp (rtag, "defTextVector") ||
		    !strcmp (rtag, "defLightVector") ||
		    !strcmp (rtag, "defSwitchVector") ||
		    !strcmp (rtag, "defBLOBVector") ||
		    !strcmp (rtag, "message") ||
		    !strcmp (rtag, "delProperty")) {
	    ISSnoopDevice (root);
	    return (0);
	}

	sprintf (msg, "Unknown command: %s", rtag);
	return(1);
}

/* pull out device and name attributes from root.
 * return 0 if ok else -1 with reason in msg[].
 */
static int
crackDN (XMLEle *root, char **dev, char **name, char msg[])
{
	XMLAtt *ap;

	ap = findXMLAtt (root, "device");
	if (!ap) {
	    if (msg)
		sprintf(msg, "%s requires 'device' attribute", tagXMLEle(root));
	    return (-1);
	}
	*dev = valuXMLAtt(ap);

	ap = findXMLAtt (root, "name");
	if (!ap) {
	    if (msg)
		sprintf (msg, "%s requires 'name' attribute", tagXMLEle(root));
	    return (-1);
	}
	*name = valuXMLAtt(ap);

	return (0);
}

/* return static string corresponding to the given property or light state */
const static char *
pstateStr (IPState s)
{
	switch (s) {
	case IPS_IDLE:  return ("Idle");
	case IPS_OK:    return ("Ok");
	case IPS_BUSY:  return ("Busy");
	case IPS_ALERT: return ("Alert");
	default:
	    fprintf (stderr, "Impossible IPState %d\n", s);
	    exit(1);
	}
}

/* crack string into IPState.
 * return 0 if ok, else -1
 */
static int
crackIPState (char *str, IPState *ip)
{
	     if (!strcmp (str, "Idle"))  *ip = IPS_IDLE;
	else if (!strcmp (str, "Ok"))    *ip = IPS_OK;
	else if (!strcmp (str, "Busy"))  *ip = IPS_BUSY;
	else if (!strcmp (str, "Alert")) *ip = IPS_ALERT;
	else return (-1);
	return (0);
}

/* crack string into ISState.
 * return 0 if ok, else -1
 */
static int
crackISState (char *str, ISState *ip)
{
	     if (!strcmp (str, "On"))  *ip = ISS_ON;
	else if (!strcmp (str, "Off")) *ip = ISS_OFF;
	else return (-1);
	return (0);
}

/* return static string corresponding to the given switch state */
const static char *
sstateStr (ISState s)
{
	switch (s) {
	case ISS_ON:  return ("On");
	case ISS_OFF: return ("Off");
	default:
	    fprintf (stderr, "Impossible ISState %d\n", s);
	    exit(1);
	}
}

/* return static string corresponding to the given Rule */
const static char *
ruleStr (ISRule r)
{
	switch (r) {
	case ISR_1OFMANY: return ("OneOfMany");
	case ISR_ATMOST1: return ("AtMostOne");
	case ISR_NOFMANY: return ("AnyOfMany");
	default:
	    fprintf (stderr, "Impossible ISRule %d\n", r);
	    exit(1);
	}
}

/* return static string corresponding to the given IPerm */
const static char *
permStr (IPerm p)
{
	switch (p) {
	case IP_RO: return ("ro");
	case IP_WO: return ("wo");
	case IP_RW: return ("rw");
	default:
	    fprintf (stderr, "Impossible IPerm %d\n", p);
	    exit(1);
	}
}

/* fill ts[] with system time in message format */
static void
timestamp (char ts[], size_t tsl)
{
	struct timeval tv;
	struct tm *tp;
	time_t t;
	int n;

	gettimeofday (&tv, NULL);
	t = (time_t) tv.tv_sec;
	tp = gmtime (&t);
	n = strftime (ts, tsl-5, "%Y-%m-%dT%H:%M:%S", tp);
	sprintf (ts+n, ".%03ld", (long)tv.tv_usec/1000);
}

/* convert sexagesimal string str AxBxC to double.
 *   x can be anything non-numeric. Any missing A, B or C will be assumed 0.
 *   optional - and + can be anywhere.
 * return 0 if ok, -1 if can't find a thing.
 */
static int
sexagesimal (
const char *str0,	/* input string */
double *dp)		/* cracked value, if return 0 */
{
	double a, b, c;
	char str[256];
	char *neg;
	int i, l, isneg;

	/* copy str0 so we can play with it */
	strncpy (str, str0, sizeof(str)-1);
	str[sizeof(str)-1] = '\0';

	/* note first negative (but not fooled by neg exponent) */
	isneg = 0;
	neg = strchr(str, '-');
	if (neg && (neg == str || (neg[-1] != 'E' && neg[-1] != 'e'))) {
	    *neg = ' ';
	    isneg = 1;
	}

	/* crack up to three components -- treat blank as 0 */
	a = b = c = 0.0;
	l = strlen(str);
	for (i = 0; i < l; i++) {
	    if (!isspace(str[i])) {
		if (sscanf (str, "%lf%*[^0-9]%lf%*[^0-9]%lf", &a, &b, &c) < 1)
		    return (-1);
		break;
	    }
	}

	/* back to one value, restoring neg */
	*dp = (c/60.0 + b)/60.0 + a;
	if (isneg)
	    *dp *= -1;
	return (0);
}

/* print the boilerplate comment introducing xml */
static void
xmlv1(FILE *fp)
{
	// fprintf (fp, "<?xml version='1.0'?>\n");
}



/* family of functions to manage the mutexes */


/* initialize the FILEMutex system */
static void
fmutexInit(void)
{
	if (!filemutex) {
	    filemutex = (FILEMutex **) malloc (1);		/* realloc seed and initted flag */
	    pthread_rwlock_init (&filem_rw, NULL);
	}
}

/* lock a given FILE *
 */
static void
fmutexLock (FILE *fp)
{
	FILEMutex *mp = NULL;
	pthread_mutex_t *lp;
	int i;

	/* init first time only */
	fmutexInit();

	/* write-lock access to the list in case we need to add fp */
	pthread_rwlock_wrlock (&filem_rw);

	/* find lock for fp, add to list if new */
	for (i = 0; i < nfilemutex; i++) {
	    if (filemutex[i]->fp == fp) {
		mp = filemutex[i];
		break;
	    }
	}
	if (!mp) {
	    filemutex = (FILEMutex **) realloc (filemutex, (nfilemutex+1)*sizeof(FILEMutex*));
	    filemutex[nfilemutex++] = mp = (FILEMutex *) malloc (sizeof(FILEMutex));
	    mp->fp = fp;
	    pthread_mutex_init (&mp->m, NULL);
	}
	lp = &mp->m;

	/* unlock list, lp will always be stable now */
	pthread_rwlock_unlock (&filem_rw);

	/* lock mutex associated with fp */
	pthread_mutex_lock (lp);
}

/* unlock a given FILE *
 */
static void
fmutexUnlock (FILE *fp)
{
	pthread_mutex_t *lp = NULL;
	int i;

	/* lock access to the list, just need read access */
	pthread_rwlock_rdlock (&filem_rw);

	/* find fp, ignore if not found */
	for (i = 0; i < nfilemutex; i++) {
	    if (filemutex[i]->fp == fp) {
		lp = &filemutex[i]->m;
		break;
	    }
	}

	/* unlock list, lp will always be stable now */
	pthread_rwlock_unlock (&filem_rw);

	if (lp)
	    pthread_mutex_unlock (lp);
}
