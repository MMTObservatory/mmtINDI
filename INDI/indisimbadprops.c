/* Simbad driver properties */


#include <stdio.h>

#include "indidevapi.h"
#include "indisimbadprops.h"

/* Version property */
static INumber simbad_version_elements[N_SVER] = {
    {"Driver", 		"Driver version",		"%6.3f", 0, 0, 0, 0.1},
    {"Properties", 	"Property list version",	"%6.3f", 0, 0, 0, 0.1},
    {"Uptime", 	        "Seconds since driver start",	"%6.0f"},
};
INumberVectorProperty simbad_version = {SIMBADDEV, "Version",
    "Version info",
    "", IP_RO, 0, IPS_IDLE, simbad_version_elements, NARRAY(simbad_version_elements)};


/* Lookup property */
static IText simbad_lookup_elements[N_SL] = {
    {"name",		"Simbad ID"},
    {"LID",		"Random Lookup ID"},
};
ITextVectorProperty simbad_lookup = {SIMBADDEV, "Lookup",
    "Simbad query ID",
    "", IP_RW, 0, IPS_IDLE, simbad_lookup_elements, NARRAY(simbad_lookup_elements)
};




/* Results property */
static INumber simbad_results_elements[N_SR] = {
    {"RA2K",		"RA @ J2000, hours",		"%12.9m"},
    {"Dec2K",		"Dec @ J2000, degs",		"%12.9m"},
    {"PMRA",		"PM in RA, mas/yr on sky",	"%12.3f"},
    {"PMDec",		"PM in Dec, mas/yr",		"%12.3f"},
    {"RadVel",		"Radial vel, km/s",		"%12.3f"},
    {"Parallax",	"Parallax, mas",		"%12.3f"},
    {"Magnitude",	"Magnitude, R else V",		"%12.3f"},
    {"RA2KNow",		"RA @ J2000 now, hours",	"%12.9m"},
    {"Dec2KNow",	"Dec @ J2000 now, degs",	"%12.9m"},
    {"Altitude",        "Altitude, degs up",	        "%12.6f"},
    {"Azimuth",	        "Azimuth, degs E of N",	        "%12.6f"},
    {"RiseTime",        "Rise time, UTC, or 999",       "%12.3m"},
    {"SetTime",	        "Set time, UTC, or 999",        "%12.3m"},
    {"TransitTime",     "Transit time, UTC, or 999",    "%12.3m"},
    {"UTC",             "UTC for stats",                "%12.6m"},
    {"LID",             "Matching Lookup ID",           "%12.0"},
};
INumberVectorProperty simbad_results = {SIMBADDEV, "Results",
    "Simbad query results",
    "", IP_RO, 0, IPS_IDLE, simbad_results_elements, NARRAY(simbad_results_elements)
};
