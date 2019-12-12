/* interface for configfile.c
 */

#ifndef CONFIGFILE_H
#define CONFIGFILE_H

/* opaque handle types */
typedef struct _CFValues CFValues;

/* load an existing file from the config area of the observatory file system.
 * returns NULL if trouble with a brief explanation in whynot[].
 */
extern CFValues *cfLoad (const char *name, int echo, char whynot[]);

/* given the CFValues returned from cfLoad(), extract a value of the given
 *   name and type. The default value is returned if name is not found.
 * N.B. the memory pointed to by cfValue is reclaimed by cfFree() so the
 *   caller must make a copy if they want it to persist longer. The memory
 *   returned by cfMValue() is a realloced copy that persists until the
 *   caller passes it to free().
 */
extern char *cfValue (CFValues *cvp, const char *name, const char *def);
extern char *cfMValue  (CFValues *cvp, const char *name, const char *def);
extern double cfDblValue (CFValues *cvp, const char *name, double def);
extern int cfIntValue (CFValues *cvp, const char *name, int def);

/* create an empty CFValues for use by the cfAdd family of functions */
extern CFValues *cfNew(void);

/* add a name/value pair to a CFValues
 */
extern void cfAddValue (CFValues *cvp, const char *name, const char *value);
extern void cfAddDblValue (CFValues *cvp, const char *name, double dvalue);
extern void cfAddIntValue (CFValues *cvp, const char *name, int ivalue);

/* change an existing name/value pair in a CFValues
 */
extern void cfChgValue (CFValues *cvp, const char *name, const char *value);
extern void cfChgDblValue (CFValues *cvp, const char *name, double dvalue);
extern void cfChgIntValue (CFValues *cvp, const char *name, int ivalue);

/* write a new config file. the name will be located within the config area of 
 * observatory file system. If the file already exists it will be overwritten.
 * returns 0 if ok else -1 with a brief explanation in whynot[].
 * N.B. this does not free memory used by cvp, caller must still call cfFree()
 */
extern int cfSave (CFValues *cvp, const char *name, char whynot[]);

/* free memory used to hold cache of config file entries */
extern void cfFree (CFValues *cvp);

/* full paths to root of observatory file system and default config directory */
extern const char *obshome(void);
extern const char *cfghome(void);


#endif /* CONFIGFILE_H */
