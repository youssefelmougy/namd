
#ifndef PSF_FILE_H 
#define PSF_FILE_H 

#include <stdio.h>

int psf_start_atoms(FILE *);
int psf_start_bonds(FILE *);
int psf_start_angles(FILE *);
int psf_start_dihedrals(FILE *);
int psf_start_impropers(FILE *);

int psf_get_atom(FILE *f, char *name, char *atype, char *resname,
                 char *segname, char *resid, double *q, double *m);
int psf_get_bonds(FILE *f, int n, int *bonds);
int psf_get_angles(FILE *f, int n, int *angles);
int psf_get_dihedrals(FILE *f, int n, int *dihedrals);
int psf_get_impropers(FILE *f, int n, int *impropers);

#endif

