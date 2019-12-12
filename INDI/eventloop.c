#if 0
    INDI
    Copyright (C) 2003 Elwood C. Downey

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

/* suite of functions to implement an event driven program.
 *
 * callbacks may be registered that are triggered when a file descriptor
 *   will not block when read;
 *
 * timers may be registered that will run no sooner than a specified delay from
 *   the moment they were registered;
 *
 * work procedures may be registered that are called when there is nothing
 *   else to do;
 *
 #define MAIN_TEST for a stand-alone test program.
 */

#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>
#include <time.h>
#include <sys/types.h>
#include <sys/time.h>
#include <unistd.h>

#include "eventloop.h"

/* error flag to cause eventLoop() to return, can be set by callbacks if interested.
 */
volatile int eloop_error;

/* info about one registered callback.
 * the malloced array cback is never shrunk, entries are reused. new id's are
 * the index of first unused slot in array (and thus reused like unix' open(2)).
 */
typedef struct {
    int in_use;				/* flag to mark this record is active */
    int fd;				/* fd descriptor to watch for read */
    void *ud;				/* user's data handle */
    CBF *fp;				/* callback function */
} CB;
static CB *cback;			/* malloced list of callbacks */
static int ncback;			/* n entries in cback[] */
static int ncbinuse;			/* n entries in cback[] marked in_use */
static int lastcb;			/* cback index of last cb called */

/* info about one registered timer function.
 * the entries are kept sorted by decreasing time from epoch, ie,
 *   the next entry to fire is at the end of the array.
 */
typedef struct {
    double tgo;				/* trigger time, ms from epoch */
    void *ud;				/* user's data handle */
    TCF *fp;				/* timer function */
    int tid;				/* unique id for this timer */
} TF;
static TF *timef;			/* malloced list of timer functions */
static int ntimef;			/* n entries in ntimef[] */
static int tid;				/* source of unique timer ids */
#define	EPOCHDT(tp) 			/* ms from epoch to timeval *tp */  \
	(((tp)->tv_usec)/1000.0 + ((tp)->tv_sec)*1000.0)

/* info about one registered work procedure.
 * the malloced array wproc is never shrunk, entries are reused. new id's are
 * the index of first unused slot in array (and thus reused like unix' open(2)).
 */
typedef struct {
    int in_use;				/* flag to mark this record is active */
    void *ud;				/* user's data handle */
    WPF *fp;				/* work proc function function */
} WP;
static WP *wproc;			/* malloced list of work procedures */
static int nwproc;			/* n entries in wproc[] */
static int nwpinuse;			/* n entries in wproc[] marked in-use */
static int lastwp;			/* wproc index of last workproc called*/

static void runWorkProc (void);
static void callCallback(fd_set *rfdp);
static int checkTimer();
static void oneLoop(void);
static void deferTO (void *p);

/* loop to dispatch callbacks, work procs and timers as necessary.
 * only returns if any callback happens to set eloop_error.
 */
void
eventLoop()
{
	eloop_error = 0;

	while (!eloop_error)
	    oneLoop();
}

/* allow other timers/callbacks/workprocs to run for at least maxms or
 *   *flagp becomes set; wait forever if maxms is 0.
 * return 0 if flag got set, else -1 if never set and we timed out.
 * the expected usage for this is for the caller to arrange for a T/C/W to set
 *   a flag, then give caller an in-line way to wait for the flag to be set
 *   via that T/C/W.
 */
int
deferLoop (int maxms, int *flagp)
{
	int toflag = 0;
	int totid = maxms ? addTimer (maxms, deferTO, &toflag) : 0;

	/* let other things run until flag is set or timeout */
	while (!*flagp && !toflag)
	    oneLoop();

	/* remove pending timer if it has not already fired */
	if (!toflag && totid)
	    rmTimer (totid);

	/* return whether flag is set */
	return (*flagp ? 0 : -1);
}

/* allow other timers/callbacks/workprocs to run for at least maxms or
 *   *flagp becomes reset. wait forever if maxms is 0.
 * return 0 if flag got reset, else -1 if never reset and we timed out.
 * the expected usage for this is for the caller to arrange for a T/C/W to reset
 *   a flag, then give caller an in-line way to wait for the flag to be reset
 *   via that T/C/W.
 */
int
deferLoop0 (int maxms, int *flagp)
{
	int toflag = 0;
	int totid = maxms ? addTimer (maxms, deferTO, &toflag) : 0;

	/* let other things run until flag is reset or timeout */
	while (*flagp && !toflag)
	    oneLoop();

	/* remove pending timer if it has not already fired */
	if (!toflag && totid)
	    rmTimer (totid);

	/* return whether flag is reset */
	return (*flagp ? -1 : 0);
}

/* register a new callback, fp, to be called with ud as arg when fd is ready.
 * return a unique callback id for use with rmCallback().
 */
int
addCallback (int fd, CBF *fp, void *ud)
{
	CB *cp;

	/* reuse first unused slot or grow */
	for (cp = cback; cp < &cback[ncback]; cp++)
	    if (!cp->in_use)
		break;
	if (cp == &cback[ncback]) {
	    cback = cback ? (CB *) realloc (cback, (ncback+1)*sizeof(CB))
	    		  : (CB *) malloc (sizeof(CB));
	    cp = &cback[ncback++];
	}

	/* init new entry */
	cp->in_use = 1;
	cp->fp = fp;
	cp->ud = ud;
	cp->fd = fd;
	ncbinuse++;

	/* id is index into array */
	return (cp - cback);
}

/* remove the callback with the given id, as returned from addCallback().
 * silently ignore if id not valid.
 */
void
rmCallback (int cid)
{
	CB *cp;

	/* validate id */
	if (cid < 0 || cid >= ncback)
	    return;
	cp = &cback[cid];
	if (!cp->in_use)
	    return;

	/* mark for reuse */
	cp->in_use = 0;
	ncbinuse--;
}

/* register a new timer function, fp, to be called with ud as arg after ms
 * milliseconds. add to list in order of decreasing time from now, ie,
 * last entry runs next. return id for use with rmTimer().
 */
int
addTimer (int ms, TCF *fp, void *ud)
{
	struct timeval t;
	TF *tp;

	/* get time now */
	gettimeofday (&t, NULL);

	/* add one entry */
	timef = timef ? (TF *) realloc (timef, (ntimef+1)*sizeof(TF))
		      : (TF *) malloc (sizeof(TF));
	tp = &timef[ntimef++];

	/* init new entry */
	tp->ud = ud;
	tp->fp = fp;
	tp->tgo = EPOCHDT(&t) + ms;

	/* insert maintaining sort */
	for ( ; tp > timef && tp[0].tgo > tp[-1].tgo; tp--) {
	    TF tmptf = tp[-1];
	    tp[-1] = tp[0];
	    tp[0] = tmptf;
	}

	/* store and return new unique id */
	return (tp->tid = ++tid);
}

/* remove the timer with the given tid, as returned from addTimer().
 * silently ignore if id not found.
 */
void
rmTimer (int rmtid)
{
	TF *tp;

	/* find it */
	for (tp = timef; tp < &timef[ntimef]; tp++)
	    if (tp->tid == rmtid)
		break;
	if (tp == &timef[ntimef])
	    return;

	/* bubble it out */
	for (++tp; tp < &timef[ntimef]; tp++)
	    tp[-1] = tp[0];

	/* shrink list */
	timef = (TF *) realloc (timef, (--ntimef)*sizeof(TF));
}

/* add a new work procedure, fp, to be called with ud when nothing else to do.
 * return unique id for use with rmWorkProc().
 */
int
addWorkProc (WPF *fp, void *ud)
{
	WP *wp;

	/* reuse first unused slot or grow */
	for (wp = wproc; wp < &wproc[nwproc]; wp++)
	    if (!wp->in_use)
		break;
	if (wp == &wproc[nwproc]) {
	    wproc = wproc ? (WP *) realloc (wproc, (nwproc+1)*sizeof(WP))
	    		  : (WP *) malloc (sizeof(WP));
	    wp = &wproc[nwproc++];
	}

	/* init new entry */
	wp->in_use = 1;
	wp->fp = fp;
	wp->ud = ud;
	nwpinuse++;

	/* id is index into array */
	return (wp - wproc);
}


/* remove the work proc with the given id, as returned from addWorkProc().
 * silently ignore if id not found.
 */
void
rmWorkProc (int wid)
{
	WP *wp;

	/* validate id */
	if (wid < 0 || wid >= nwproc)
	    return;
	wp = &wproc[wid];
	if (!wp->in_use)
	    return;

	/* mark for reuse */
	wp->in_use = 0;
	nwpinuse--;
}

/* run next work procedure */
static void
runWorkProc ()
{
	WP *wp;

	/* skip if list is empty */
	if (!nwpinuse)
	    return;

	/* find next */
	do {
	    lastwp = (lastwp+1) % nwproc;
	    wp = &wproc[lastwp];
	} while (!wp->in_use);

	/* run */
	(*wp->fp) (wp->ud);
}

/* run next callback whose fd is listed as ready to go in rfdp */
static void
callCallback(fd_set *rfdp)
{
	CB *cp;

	/* skip if list is empty */
	if (!ncbinuse)
	    return;

	/* find next */
	do {
	    lastcb = (lastcb+1) % ncback;
	    cp = &cback[lastcb];
	} while (!cp->in_use || !FD_ISSET (cp->fd, rfdp));

	/* run */
	(*cp->fp) (cp->fd, cp->ud);
}

/* run the next timer callback whose time has come, if any. all we have to do
 * is is check the last entry in timef[] because it is sorted in decreasing
 * order of time from now to run, ie, last entry runs soonest.
 * return whether a callback was triggered.
 */
static int
checkTimer()
{
	struct timeval now;
	double tgonow;
	TF *tp;

	/* skip if list is empty */
	if (!ntimef)
	    return(0);

	gettimeofday (&now, NULL);
	tgonow = EPOCHDT (&now);
	tp = &timef[ntimef-1];
	if (tp->tgo <= tgonow) {
	    ntimef--;			/* pop then call */
	    (*tp->fp) (tp->ud);
	    return (1);
	} else
	    return (0);
}

/* check fd's from each active callback.
 * if any ready, call their callbacks else call each registered work procedure.
 */
static void
oneLoop()
{
	struct timeval tv, *tvp;
	fd_set rfd;
	CB *cp;
	int maxfd, ns;

	/* build list of callback file descriptors to check */
	FD_ZERO (&rfd);
	maxfd = -1;
	for (cp = cback; cp < &cback[ncback]; cp++) {
	    if (cp->in_use) {
		FD_SET (cp->fd, &rfd);
		if (cp->fd > maxfd)
		    maxfd = cp->fd;
	    }
	}

	/* determine timeout:
	 * if there are work procs
	 *   set delay = 0
	 * else if there is at least one timer func
	 *   set delay = time until soonest timer func expires
	 * else
	 *   set delay = forever
	 */
	if (nwpinuse > 0) {
	    tvp = &tv;
	    tvp->tv_sec = tvp->tv_usec = 0;
	} else if (ntimef > 0) {
	    struct timeval now;
	    double late;
	    gettimeofday (&now, NULL);
	    late = timef[ntimef-1].tgo - EPOCHDT (&now);	/* ms late */
	    if (late < 0)
		late = 0;
	    late /= 1000.0;					/* secs late */
	    tvp = &tv;
	    tvp->tv_sec = (long)floor(late);
	    tvp->tv_usec = (long)floor((late - tvp->tv_sec)*1000000.0);
	} else
	    tvp = NULL;

	/* check file descriptors, timeout depending on pending work */
	ns = select (maxfd+1, &rfd, NULL, NULL, tvp);
	if (ns < 0) {
	    perror ("select");
	    exit(1);
	}
	
	/* dispatch at most one user function.
	 * N.B. nesting oneLoops is not a problem but each invokation must only
	 *   run at most one user function in case they interact.
	 */
	if (ns > 0)
	    callCallback(&rfd);
	else if (!checkTimer())
	    runWorkProc();
}

/* timer callback used to implement deferLoop().
 * arg is pointer to int which we set to 1
 */
static void
deferTO (void *p)
{
	*(int*)p = 1;
}

#if defined(MAIN_TEST)
/* make a small stand-alone test program.
 */

#include <unistd.h>
#include <sys/time.h>

int mycid;
int mywid;
int mytid;

int user_a;
int user_b;
int counter;

void
wp (void *ud)
{
	struct timeval tv;

	gettimeofday (&tv, NULL);
	printf ("workproc @ %ld.%03ld %d %d\n",	
		(long)tv.tv_sec, (long)tv.tv_usec/1000, counter, ++(*(int*)ud));
}

void
to (void *ud)
{
	printf ("timeout %d\n", (int)ud);
}

void
stdinCB (int fd, void *ud)
{
	char c;

	if (read (fd, &c, 1) != 1) {
	    perror ("read");
	    exit(1);
	}

	switch (c) {
	case '+': counter++; break;
	case '-': counter--; break;

	case 'W': mywid = addWorkProc (wp, &user_b); break;
	case 'w': rmWorkProc (mywid); break;

	case 'c': rmCallback (mycid); break;

	case 't': rmTimer (mytid); break;
	case '1': mytid = addTimer (1000, to, (void *)1); break;
	case '2': mytid = addTimer (2000, to, (void *)2); break;
	case '3': mytid = addTimer (3000, to, (void *)3); break;
	case '4': mytid = addTimer (4000, to, (void *)4); break;
	case '5': mytid = addTimer (5000, to, (void *)5); break;
	default: return;	/* silently absorb other chars like \n */
	}

	printf ("callback: %d\n", ++(*(int*)ud));
}

int
main (int ac, char *av[])
{
	(void) addCallback (0, stdinCB, &user_a);
	eventLoop();
	exit(0);
}

#endif
