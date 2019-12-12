/* shell command driver.
 * Sync: waits for command to exit then reports stdout/err and exit status.
 * ASync: publishes each new line of stdout/stderr, exit blank until command exits.
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

#define	SHDEV	"Shell"

#include "astro.h"
#include "indidevapi.h"
#include "configfile.h"

/* Version property */
typedef enum {  /* N.B. order must match array in vector version_elements[] */
    DRIVER_SHVER, PROPS_SHVER, UPTIME_SHVER,
    N_SHVER
} SHVersionIndex;
static INumber shversion_elements[N_SHVER] = {
    {"Driver", 		"Driver version",		"%6.3f", 0, 0, 0, 1.0},
    {"Properties", 	"Property list version",	"%6.3f", 0, 0, 0, 1.0},
    {"Uptime", 	        "Seconds since driver start",	"%6.0f"},
};
INumberVectorProperty shversion = {SHDEV, "Version",
    "Version info",
    "", IP_RO, 0, IPS_IDLE, shversion_elements, NARRAY(shversion_elements)};


/* Sync property */
typedef enum {  /* N.B. order must match array in vector synccommand_elements[] */
    CMD_SYN, STDOUT_SYN, STDERR_SYN, EXIT_SYN,
    N_SYN
} SyncSCIndex;
static IText synccommand_elements[N_SYN] = {
    {"command",         "Shell command"},
    {"stdout",          "Aggregate stdout response"},
    {"stderr",          "Aggregate stderr response"},
    {"exit",            "0:OK, >0:exit <0:signal"},
};
ITextVectorProperty synccommand = {SHDEV, "Sync",
    "Synchronous shell command",
    "", IP_RW, 0, IPS_IDLE, synccommand_elements, NARRAY(synccommand_elements)};



/* ASync property */
typedef enum {  /* N.B. order must match array in vector asynccommand_elements[] */
    CMD_ASYN, STDOUT_ASYN, STDERR_ASYN, EXIT_ASYN,
    N_ASYN
} ASyncSCIndex;
static IText asynccommand_elements[N_ASYN] = {
    {"command",         "Shell command"},
    {"stdout",          "Running stdout response"},
    {"stderr",          "Running stderr response"},
    {"exit",            "0:OK, >0:exit <0:signal"},
};
ITextVectorProperty asynccommand = {SHDEV, "ASync",
    "Asynchronous shell command",
    "", IP_RW, 0, IPS_IDLE, asynccommand_elements, NARRAY(asynccommand_elements)};



static void addText (IText *tp, int tplen, const char buf[], int buflen);
static int runThread (void *(*thread_f)(void *));
static void *syncThread(void *notused);
static void *asyncThread(void *notused);
static void runCommand (ITextVectorProperty *ivp, int cmd_idx, int out_idx, int err_idx,
	int exit_idx, int sync);
static void uptimeCB (void *not_used);


/* send client definitions of all properties */
void
ISGetProperties (char const *dev, char const *name)
{
	static int before;

	if (!before) {
	    IEAddTimer (5000, uptimeCB, NULL);
	    before = 1;
	}

	if (!name || !strcmp (name, shversion.name))
	    IDDefNumber (&shversion, NULL);
	if (!name || !strcmp (name, asynccommand.name))
	    IDDefText (&asynccommand, NULL);
	if (!name || !strcmp (name, synccommand.name))
	    IDDefText (&synccommand, NULL);
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
	if (!IUCrackText (&synccommand, dev, name, texts, names, n)) {
	    /* only one at a time */
	    if (synccommand.s == IPS_BUSY) {
		IDSetText (&synccommand, "Command is in use");
		return;
	    }

	    /* ack */
	    synccommand.s = IPS_BUSY;
	    IDSetText (&synccommand, NULL);

	    /* run */
	    if (runThread (syncThread) < 0) {
		synccommand.s = IPS_ALERT;
		IDSetText (&synccommand, "Failed to create thread");
	    }

        } else
	
	if (!IUCrackText (&asynccommand, dev, name, texts, names, n)) {
	    /* only one at a time */
	    if (asynccommand.s == IPS_BUSY) {
		IDSetText (&asynccommand, "Command is in use");
		return;
	    }

	    /* ack */
	    asynccommand.s = IPS_BUSY;
	    IDSetText (&asynccommand, NULL);

	    /* run */
	    if (runThread (asyncThread) < 0) {
		asynccommand.s = IPS_ALERT;
		IDSetText (&asynccommand, "Failed to create thread");
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
	fprintf (stderr, "Snooping %s.%s\n", findXMLAttValu (root, "device"),
						findXMLAttValu (root, "name"));
}

/* start the given thread detached.
 * return 0 if ok, else -1
 */
static int
runThread (void *(*thread_f)(void *))
{
	pthread_attr_t attr;
	pthread_t thr;

	/* new thread will be detached so we need no join */
	pthread_attr_init (&attr);
	pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);

	return (pthread_create (&thr, &attr, thread_f, NULL));
}


/* thread to run the synchronous command in synccommand
 */
static void *
syncThread(void *notused)
{
	runCommand (&synccommand, CMD_SYN, STDOUT_SYN, STDERR_SYN, EXIT_SYN, 1);

	return (NULL);
}

/* thread to run the asynchronous command in asynccommand
 */
static void *
asyncThread(void *notused)
{
	runCommand (&asynccommand, CMD_ASYN, STDOUT_ASYN, STDERR_ASYN, EXIT_ASYN, 0);

	return (NULL);
}

/* fork/exec the command given in the given text property at cmd_idx.
 * collect stdout and stderr in out_idx and err_idx and exit status in exit_idx.
 * if sync, publish each time a new stdout or stderr line is collected.
 * publish final status when command finally exits.
 */
static void
runCommand (ITextVectorProperty *ivp, int cmd_idx, int out_idx, int err_idx, int exit_idx, int sync)
{
	int po[2], pe[2];

	/* create out/err pipes */
	pipe(po);
	pipe(pe);

	/* record */
	IDLog ("Running '%s'\n", ivp->tp[cmd_idx].text);

	/* init */
	ivp->s = IPS_BUSY;
	IUSaveText (&ivp->tp[out_idx], "");
	IUSaveText (&ivp->tp[err_idx], "");
	IUSaveText (&ivp->tp[exit_idx], "");

	if (fork()) {
	    /* parent collects stdout/err until both EOF then waits for exit */
	    char buf[4096+1];
	    int so = po[0];
	    int sol = 0;
	    int se = pe[0];
	    int sel = 0;
	    int nf = (so > se ? so : se) + 1;
	    int no = 0, ne = 0;
	    int wstat;

	    /* close unuesed ends */
	    close (po[1]);
	    close (pe[1]);

	    /* collect stdout/err until both report EOF */
	    do {
		fd_set f;
		FD_ZERO(&f);
		FD_SET(so,&f);
		FD_SET(se,&f);
		select (nf, &f, NULL, NULL, NULL);
		if (FD_ISSET (so, &f)) {
		    no = read (so, buf, sizeof(buf)-1);
		    if (no > 0) {
			buf[no] = '\0';
			addText (&ivp->tp[out_idx], sol, buf, no);
			sol += no;
			if (!sync)
			    IDSetText (ivp, NULL);
		    } else {
			IDLog ("Stdout: %s\n", no < 0 ? strerror(errno) : "EOF");
		    }
		}
		if (FD_ISSET (se, &f)) {
		    ne = read (se, buf, sizeof(buf)-1);
		    if (ne > 0) {
			buf[ne] = '\0';
			addText (&ivp->tp[err_idx], sel, buf, ne);
			sel += ne;
			if (!sync)
			    IDSetText (ivp, NULL);
		    } else {
			IDLog ("Stderr: %s\n", no < 0 ? strerror(errno) : "EOF");
		    }
		}
	    } while (no > 0 || ne > 0);

	    /* wait */
	    wait (&wstat);
	    if (WIFEXITED(wstat)) {
		int e = WEXITSTATUS(wstat);
		sprintf (buf, "%d", e);
		IUSaveText (&ivp->tp[exit_idx], buf);
		ivp->s = e ? IPS_ALERT : IPS_OK;
		IDSetText (ivp, "Exit %d", e);
	    } else if (WIFSIGNALED(wstat)) {
		int s = WTERMSIG(wstat);
		sprintf (buf, "%d", -s);
		IUSaveText (&ivp->tp[exit_idx], buf);
		ivp->s = IPS_ALERT;
		IDSetText (ivp, "Signal %d%s", s, WCOREDUMP(wstat) ? " (core dumped)" : "");
	    } else {
		sprintf (buf, "999");
		IUSaveText (&ivp->tp[exit_idx], buf);
		ivp->s = IPS_ALERT;
		IDSetText (ivp, "Unknown exit");
	    }

	} else {
	    /* child runs shell after arranging to collect outputs */
	    dup2(open ("/dev/null", O_RDONLY), 0);
	    dup2 (po[1], 1); close (po[0]);
	    dup2 (pe[1], 2); close (pe[0]);
	    execl ("/bin/sh", "/bin/sh", "-c", ivp->tp[cmd_idx].text, (char*)NULL);
	}
}

/* add buf[buflen] to the given IText, known to already contain tplen.
 */
static void
addText (IText *tp, int tplen, const char buf[], int buflen)
{
	char *bothbuf = (char *) malloc (tplen + buflen + 1);
	sprintf (bothbuf, "%s%s", tp->text, buf);
	IUSaveText (tp, bothbuf);
	free (bothbuf);
}

static void
uptimeCB (void *not_used)
{
	static time_t t0;

	if (t0 == 0)
	    t0 = time(NULL);

	shversion.np[UPTIME_SHVER].value = time(NULL) - t0;
	IDSetNumber (&shversion, NULL);

	IEAddTimer (5000, uptimeCB, NULL);
}
