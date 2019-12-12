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

/* main() just for classic INDI driver.
 * most functions are in indidriverbase.c.
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>

#include "indidevapi.h"

int
main (int ac, char *av[])
{
	/* no args other than self */
	if (ac > 1) {
	    fprintf (stderr, "Usage: %s\n", av[0]);
	    fprintf (stderr, "Purpose: INDI Device driver framework.\n");
	    exit(1);
	}

	/* register stdin for INDI callbacks */
	IUAddConnection (STDIN_FILENO);

	/* go */
	IUEventLoop();

	/* eh?? */
	fprintf (stderr, "inf loop ended\n");
	return (1);
}
