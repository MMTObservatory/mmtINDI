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

#ifndef INDIDEVAPIH
#define INDIDEVAPIH

/*******************************************************************************
 * This is the interface to the reference INDI C API device implementation on
 * the Device Driver side. This file are divided into two main sections:
 *
 *   Functions the INDI device driver framework defines which the Driver may
 *   call:
 *
 *     IDxxx functions to send messages to an INDI client or indiserver.
 *     IExxx functions to implement the event driven model.
 *     IUxxx functions to perform handy utility functions.
 *
 *   Functions the INDI device driver framework calls which the Driver must
 *   define:
 *
 *     ISxxx to respond to messages from a Client.
 *     ISSnoopDevice to respond to messages from a snooped Device.
 */

/*******************************************************************************
 * get the data structures
 */

#include "indiapi.h"
#include "lilxml.h"

/*******************************************************************************
 *******************************************************************************
 *
 *  Functions the INDI device driver framework defines which the Driver may call
 *
 *******************************************************************************
 *******************************************************************************
 */

#ifdef __cplusplus
extern "C" {
#endif

/*******************************************************************************
 * Functions Drivers call to define their Properties to Clients.
 */

extern void IDDefText (const ITextVectorProperty *t, const char *msg, ...);
extern void IDDefNumber (const INumberVectorProperty *n, const char *msg, ...);
extern void IDDefSwitch (const ISwitchVectorProperty *s, const char *msg, ...);
extern void IDDefLight (const ILightVectorProperty *l, const char *msg, ...);
extern void IDDefBLOB (const IBLOBVectorProperty *b, const char *msg, ...);

extern void IDFDefText (FILE *fp, const ITextVectorProperty *t, const char *msg, ...);
extern void IDFDefNumber (FILE *fp, const INumberVectorProperty *n, const char *msg, ...);
extern void IDFDefSwitch (FILE *fp, const ISwitchVectorProperty *s, const char *msg, ...);
extern void IDFDefLight (FILE *fp, const ILightVectorProperty *l, const char *msg, ...);
extern void IDFDefBLOB (FILE *fp, const IBLOBVectorProperty *b, const char *msg, ...);


/*******************************************************************************
 * Functions Drivers call to tell Clients of new values for existing Properties.
 * msg argument functions like printf in ANSI C; may be NULL for no message.
 */

extern void IDSetText (const ITextVectorProperty *t, const char *msg, ...);
extern void IDSetNumber (const INumberVectorProperty *n, const char *msg, ...);
extern void IDSetSwitch (const ISwitchVectorProperty *s, const char *msg, ...);
extern void IDSetLight (const ILightVectorProperty *l, const char *msg, ...);
extern void IDSetBLOB (const IBLOBVectorProperty *b, const char *msg, ...);

extern void IDFSetText (FILE *fp, const ITextVectorProperty *t, const char *msg, ...);
extern void IDFSetNumber (FILE *fp, const INumberVectorProperty *n, const char *msg, ...);
extern void IDFSetSwitch (FILE *fp, const ISwitchVectorProperty *s, const char *msg, ...);
extern void IDFSetLight (FILE *fp, const ILightVectorProperty *l, const char *msg, ...);
extern void IDFSetBLOB (FILE *fp, const IBLOBVectorProperty *b, const char *msg, ...);


/*******************************************************************************
 * Function Drivers call to send log messages to Clients. If dev is specified
 * the Client shall associate the message with that device; if dev is NULL the
 * Client shall treat the message as generic from no specific Device.
 */

extern void IDMessage (const char *dev, const char *msg, ...);

extern void IDFMessage (FILE *fp, const char *dev, const char *msg, ...);


/*******************************************************************************
 * Function Drivers call to inform Clients a Property is no longer
 * available, or the entire device is gone if name is NULL.
 * msg argument functions like printf in ANSI C; may be NULL for no message.
 */

extern void IDDelete (const char *dev, const char *name, const char *msg, ...);

extern void IDFDelete (FILE *fp, const char *dev, const char *name, const char *msg, ...);


/*******************************************************************************
 * Function a Driver calls to snoop on another Device. Snooped messages will
 * then arrive via ISSnoopDevice.
 */

extern void IDSnoopDevice (const char *snooped_device, const char *snooped_property);

extern void IDFSnoopDevice (FILE *fp, const char *snooped_device, const char *snooped_property);


/*******************************************************************************
 * Function a Driver calls to control whether they will receive BLOBs from
 * snooped devices.
 */

typedef enum {B_NEVER=0, B_ALSO, B_ONLY} BLOBHandling;

extern void IDSnoopBLOBs (char *snooped_device_name, BLOBHandling bh);

extern void IDFSnoopBLOBs (FILE *fp, char *snooped_device_name, BLOBHandling bh);


/*******************************************************************************
 * Function Drivers call to inform the indiserver to log an arbitray message
 * not associated with any Client or Property.
 */

extern void IDLog (const char *msg, ...);



/*******************************************************************************
 * Functions Drivers call to register with the INDI event utilities.
 *
 *   Callbacks are called when a read on a file descriptor will not block.
 *   Timers are called once after a specified interval.
 *   Workprocs are called when there is nothing else to do.
 *
 * The "Add" functions return a unique id for use with their corresponding "Rm"
 * removal function. An arbitrary pointer may be specified when a function is
 * registered which will be stored and forwarded unchanged when the function
 * is later involked.
 */

/* signature of a callback, timout caller and work procedure function */

typedef void (IE_CBF) (int readfiledes, void *userpointer);
typedef void (IE_TCF) (void *userpointer);
typedef void (IE_WPF) (void *userpointer);

/* functions to add and remove callbacks, timers and work procedures */

extern int  IEAddCallback (int readfiledes, IE_CBF *fp, void *userpointer);
extern void IERmCallback (int callbackid);

extern int  IEAddTimer (int millisecs, IE_TCF *fp, void *userpointer);
extern void IERmTimer (int timerid);

extern int  IEAddWorkProc (IE_WPF *fp, void *userpointer);
extern void IERmWorkProc (int workprocid);

/* wait in-line for a flag to set, presumably by another event function */

extern int IEDeferLoop (int maxms, int *flagp);
extern int IEDeferLoop0 (int maxms, int *flagp);



/*******************************************************************************
 * Functions Drivers call to perform handy utility functions.
 * these do not communicate with the Client in any way.
 */

/* functions to find a specific member of a vector property */

extern IText   *IUFindText  (const ITextVectorProperty *tp, const char *name);
extern INumber *IUFindNumber(const INumberVectorProperty *np, const char *name);
extern ISwitch *IUFindSwitch(const ISwitchVectorProperty *sp, const char *name);
extern ISwitch *IUFindOnSwitch (const ISwitchVectorProperty *sp);

/* functions to help process a new property message from ISNewXXX */

extern int IUCrackNumber(INumberVectorProperty *nvp, const char *dev,
    const char *name, double *doubles, char *names[], int n);
extern int IUCrackText(ITextVectorProperty *tvp, const char *dev,
    const char *name, char *texts[], char *names[], int n);
extern int IUCrackSwitch(ISwitchVectorProperty *tvp, const char *dev,
    const char *name, ISState *states, char *names[], int n);
extern int IUCrackBLOB(IBLOBVectorProperty *bvp, const char *dev, const char *name, int sizes[],
    int blobsizes[], char *blobs[], char *formats[], char *names[], int n);

/* functions to help process a snooped message from ISSnoopDevice */

extern int IUSnoopNumber (XMLEle *root, INumberVectorProperty *nvp);
extern int IUSnoopText (XMLEle *root, ITextVectorProperty *tvp);
extern int IUSnoopLight (XMLEle *root, ILightVectorProperty *lvp);
extern int IUSnoopSwitch (XMLEle *root, ISwitchVectorProperty *svp);
extern int IUSnoopBLOB (XMLEle *root, IBLOBVectorProperty *bvp);

/* function to set all property switches off */
extern void IUResetSwitches(const ISwitchVectorProperty *svp);

/* function to reliably save new text in a IText */
extern void IUSaveText (IText *tp, const char *fmt, ...);


/*******************************************************************************
 * Function a Driver may call to add a file descriptor from which to listen for
 * INDI messages, presummably a socket connected to an indiserver. After all
 * connections are set up, the Driver calls IUEventLoop() which never returns
 * such connected sockets. Returns callback ID for use with IERmCallback if
 * ever needed.
 */

extern int IUAddConnection (int fd);

/* Function drivers may call to relinquish control to the driver framework. Thus
 * function never returns, but calls ISNew*() functions as INDI messages
 * arrive on sockets previously connected via IUAddConnection();
 */

extern void IUEventLoop (void);



/*******************************************************************************
 *******************************************************************************
 *
 *   Functions the INDI Device Driver framework calls which the Driver must
 *   define.
 *
 *******************************************************************************
 *******************************************************************************
 */


/*******************************************************************************
 * Function defined by Drivers that is called when a Client asks for the
 * definitions of Properties. No dev means any device, no propname means all
 * properties.
 */

extern void ISGetProperties (const char *dev, const char *propname);


/*******************************************************************************
 * Functions defined by Drivers that are called when a Client has sent a
 * newXXX message to set different values for named members of the given
 * vector Property. The values and their names are parallel arrays of n
 * elements each.
 */

extern void ISNewText (const char *dev, const char *name, char *texts[],
    char *names[], int n); 

extern void ISNewNumber (const char *dev, const char *name, double *doubles,
    char *names[], int n); 

extern void ISNewSwitch (const char *dev, const char *name, ISState *states,
    char *names[], int n); 

extern void ISNewBLOB (const char *dev, const char *name, int sizes[],
    int blobsizes[], char *blobs[], char *formats[], char *names[], int n); 


/*******************************************************************************
 * Function defined by Drivers that is called when another Driver it is
 * snooping (by having previously called IDSnoopDevice()) sent any INDI message.
 * The argument contains the full message exactly as it was sent by the driver.
 * Hint: use the IUSnoopXXX utility functions to help crack the message if it
 * was one of setXXX or defXXX.
 */

extern void ISSnoopDevice (XMLEle *root);


#ifdef __cplusplus
}
#endif

/* For RCS Only -- Do Not Edit
 * @(#) $RCSfile: indidevapi.h,v $ $Date: 2007/07/01 19:23:58 $ $Revision: 1.2 $ $Name:  $
 */

#endif /* INDIDEVAPIH */
