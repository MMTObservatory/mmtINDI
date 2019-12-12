#include "astro.h"
#include "configfile.h"
#include "location.h"

/* get lt (+N rads), lg (+E rads) and elev (earth radii) from config file.
 * N.B. file is assumed to have +E longitude.
 */
void
getLocation(double *ltp, double *lgp, double *elp)
{
	static int ok;
	static double lt, lg, el;

	if (!ok) {
	    /* cache the first time */
	    CFValues *cvp = cfLoad ("latlong.cfg", 1, NULL);
	    lt = degrad(cfDblValue(cvp,"latitude", 33.9850));
	    lg = degrad(cfDblValue(cvp,"longitude", -107.18944));
	    el = cfDblValue(cvp,"elevation", 3235.0)/ERAD;
	    cfFree (cvp);
	    ok = 1;
	}

	*ltp = lt;
	*lgp = lg;
	*elp = el;
}
