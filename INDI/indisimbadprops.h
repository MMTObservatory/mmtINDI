/* include file to declare the properties used for the Simbad driver */

#ifndef _SIMBAD_H
#define _SIMBAD_H

/* operational info */
#define SIMBADDEV "Simbad"              /* Device name we call ourselves */


/* Version property */
typedef enum {  /* N.B. order must match array in vector simbad_version_elements[] */
    DRIVER_SVER, PROPS_SVER, UPTIME_SVER,
    N_SVER
} SVersionIndex;
extern INumberVectorProperty simbad_version;



/* Lookup property */
typedef enum {	/* N.B. order must match array in vector simbad_lookup_elements[] */
    NAME_SL, LID_SL, N_SL
} LookupIndex;
extern ITextVectorProperty simbad_lookup;



/* Result property */
typedef enum {	/* N.B. order must match array in vector simbad_results_elements[] */
    RA2K_SR, DEC2K_SR, PMRA_SR, PMDEC_SR, RADVEL_SR, PARALLAX_SR, MAG_SR,
    RA2KNOW_SR, DEC2KNOW_SR, ALT_SR, AZ_SR, RISETM_SR, SETTM_SR, TRANSTM_SR, UTC_SR,
    LID_SR,
    N_SR
} ResultsIndex;
extern INumberVectorProperty simbad_results;

#endif // !_SIMBAD_H
