#include <math.h>

#include "astro.h"
#include "localrs.h"

#define	MAXROCK		2	/* max time to rock from now, days */

/* find rise, transit and set times of the given object at np for dis within
 *   a day or so of now.
 * if the object is now up, find previous rise and next set;
 * if the object is now down, find previous set and next rise;
 * regardless of whether up find the next transit following the reported rise.
 * error returns for r/s value are -1 = never up or -2 = never rise/set
 *  (circumpolar) and for transit are -1 = never up or -2 for never transits
 *  (geostationary).
 * normal return values are JD of event.
 */
void
localrs (Now *np, Obj *op, double dis, double *risep, double *transp,
double *setp)
{
        RiseSet rs, *rp = &rs;
        int rok, tok, sok;
        double mjd0;
	double rock, delmjd;
        int nbounce;
	Now mynow;
        int up;

	/* init */
	mynow = *np, np = &mynow;	/* guard caller's mjd */
	obj_cir (np, op);
        up = op->s_alt > -dis;
        nbounce = 0;
	delmjd = is_type(op,EARTHSATM) ? 0.5/op->es_n : 0.5;
        mjd0 = mjd;
        rok = sok = 0;
        tok = !transp;

	/* loop until find each or moved far enough */
        while (1) {
            riset_cir (np, op, dis, rp);

            if (!rok) {
                if (rp->rs_flags & RS_NEVERUP) {
                    *risep = -1;
                    rok = 1;
                } else if (rp->rs_flags & RS_CIRCUMPOLAR) {
                    *risep = -2;
                    rok = 1;
                } else if ((up && rp->rs_risetm <= mjd0) 
                                            || (!up && rp->rs_risetm >= mjd0)) {
                    *risep = rp->rs_risetm + MJD0;
                    rok = 1;
                }
	    }

            if (!sok) {
                if (rp->rs_flags & RS_NEVERUP) {
                    *setp = -1;
                    sok = 1;
                } else if (rp->rs_flags & RS_CIRCUMPOLAR) {
                    *setp = -2;
                    sok = 1;
                } else if ((up && rp->rs_settm >= mjd0)
                                            || (!up && rp->rs_settm <= mjd0)) {
                    *setp = rp->rs_settm + MJD0;
                    sok = 1;
                }
            }

            if (!tok) {
                if (rp->rs_flags & RS_NEVERUP) {
                    *transp = -1;
                    tok = 1;
		} else if (rp->rs_flags & RS_NOTRANS) {
                    *transp = -2;
                    tok = 1;
		} else if (rok) {
		    if (rp->rs_trantm + MJD0 > *risep) {
			*transp = rp->rs_trantm + MJD0;
			tok = 1;
		    }
                }
            }

            /* rock back and forth further and further away from mjd0 */
	    nbounce = nbounce >= 0 ? -nbounce-1 : -nbounce;
	    rock = delmjd*nbounce;
            mjd = mjd0 + rock;

	    /* check done */
	    if (rok && tok && sok)
		return;
	    if (fabs(rock) > MAXROCK) {
		if (!rok)
		    *risep = -1;
		if (!tok)
		    *transp = -2;
		if (!sok)
		    *setp = -1;
		return;
	    }
	}
}
