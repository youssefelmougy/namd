#include <stdlib.h>
#include <string.h>

#include "psf_file.h"

#define PSF_RECORD_LENGTH 	80

/* return # of atoms, or negative if error */

int psf_start_atoms(FILE *file) {
 
  char inbuf[PSF_RECORD_LENGTH+2];
  int natom = 0;
  
  /* skip comments; get number of atoms */
  /* Taken from VMD's ReadPSF */
  do {
    if (inbuf != fgets(inbuf, PSF_RECORD_LENGTH+1, file)) {
      /* EOF with no NATOM */
      return -1;  
    }
    if (strlen(inbuf) > 0) {
      if (!strstr(inbuf, "REMARKS")) {
        if (strstr(inbuf, "NATOM")) {
          natom = atoi(inbuf);
        }
      }
    }
  } while (!natom);
  return natom;
}

int psf_get_atom(FILE *f, char *name, char *atype, char *resname,
   char *segname, char *resid, double *q, double *m) {

  char inbuf[PSF_RECORD_LENGTH+2];
  int i,num, read_count;

  if(inbuf != fgets(inbuf, PSF_RECORD_LENGTH+1, f)) {
    return(-1);
  }
  read_count = sscanf(inbuf, "%d %s %s %s %s %s %lf %lf",
    &num, segname, resid, resname, name, atype, q, m);

  if (read_count != 8) {
    fprintf(stderr,"BAD ATOM LINE IN PSF FILE:\n: %s\n", inbuf);
    return -1;
  }
  if (sscanf(atype, "%d", &i) > 0) {
    fprintf(stderr, "PSF file is in CHARMM format; XPLOR format required.\n");
    return -1;
  }
  return num;
}
 
int psf_start_bonds(FILE *file) {

  char inbuf[PSF_RECORD_LENGTH+2];
  int nbond = -1;

  /* keep reading the next line until a line with NBOND appears */
  do {
    if(inbuf != fgets(inbuf, PSF_RECORD_LENGTH+1, file)) {
      /* EOF encountered with no NBOND line found ==> error, return (-1) */
      return (-1);
    }
    if(strlen(inbuf) > 0 && strstr(inbuf,"NBOND"))
      nbond = atoi(inbuf);
  } while (nbond == -1);

  return nbond;
}

int psf_get_bonds(FILE *f, int n, int *bonds) {

  char inbuf[PSF_RECORD_LENGTH+2];
  char *bondptr = NULL;
  int i=0;
  while (i<n) {
    if((i % 4) == 0) {
      /* must read next line */
      if(!fgets(inbuf,PSF_RECORD_LENGTH+2,f)) {
        /* early EOF encountered */
        break;
      }
      bondptr = inbuf;
    }
    if((bonds[2*i] = atoi(bondptr)) < 1)
      break;
    bondptr += 8;
    if((bonds[2*i+1] = atoi(bondptr)) < 1)
      break;
    bondptr += 8;
    i++;
  }

  return (i != n);
}

int psf_start_angles(FILE *file) {

  char inbuf[PSF_RECORD_LENGTH+2];
  int nbond = -1;

  /* keep reading the next line until a line with NBOND appears */
  do {
    if(inbuf != fgets(inbuf, PSF_RECORD_LENGTH+1, file)) {
      /* EOF encountered with no NBOND line found ==> error, return (-1) */
      return (-1);
    }
    if(strlen(inbuf) > 0 && strstr(inbuf,"NTHETA"))
      nbond = atoi(inbuf);
  } while (nbond == -1);

  return nbond;
}

int psf_get_angles(FILE *f, int n, int *angles) {

  char inbuf[PSF_RECORD_LENGTH+2];
  char *bondptr = NULL;
  int i=0;
  while (i<n) {
    if((i % 3) == 0) {
      /* must read next line */
      if(!fgets(inbuf,PSF_RECORD_LENGTH+2,f)) {
        /* early EOF encountered */
        break;
      }
      bondptr = inbuf;
    }
    if((angles[3*i] = atoi(bondptr)) < 1)
      break;
    bondptr += 8;
    if((angles[3*i+1] = atoi(bondptr)) < 1)
      break;
    bondptr += 8;
    if((angles[3*i+2] = atoi(bondptr)) < 1)
      break;
    bondptr += 8;
    i++;
  }

  return (i != n);
}

int psf_start_dihedrals(FILE *file) {

  char inbuf[PSF_RECORD_LENGTH+2];
  int nbond = -1;

  /* keep reading the next line until a line with NBOND appears */
  do {
    if(inbuf != fgets(inbuf, PSF_RECORD_LENGTH+1, file)) {
      /* EOF encountered with no NBOND line found ==> error, return (-1) */
      return (-1);
    }
    if(strlen(inbuf) > 0 && strstr(inbuf,"NPHI"))
      nbond = atoi(inbuf);
  } while (nbond == -1);

  return nbond;
}

    
int psf_get_dihedrals(FILE *f, int n, int *dihedrals) {

  char inbuf[PSF_RECORD_LENGTH+2];
  char *bondptr = NULL;
  int i=0;
  while (i<n) {
    if((i % 2) == 0) {
      /* must read next line */
      if(!fgets(inbuf,PSF_RECORD_LENGTH+2,f)) {
        /* early EOF encountered */
        break;
      }
      bondptr = inbuf;
    }
    if((dihedrals[4*i] = atoi(bondptr)) < 1)
      break;
    bondptr += 8;
    if((dihedrals[4*i+1] = atoi(bondptr)) < 1)
      break;
    bondptr += 8;
    if((dihedrals[4*i+2] = atoi(bondptr)) < 1)
      break;
    bondptr += 8;
    if((dihedrals[4*i+3] = atoi(bondptr)) < 1)
      break;
    bondptr += 8;
    i++;
  }

  return (i != n);
}

int psf_start_impropers(FILE *file) {

  char inbuf[PSF_RECORD_LENGTH+2];
  int nbond = -1;

  /* keep reading the next line until a line with NBOND appears */
  do {
    if(inbuf != fgets(inbuf, PSF_RECORD_LENGTH+1, file)) {
      /* EOF encountered with no NBOND line found ==> error, return (-1) */
      return (-1);
    }
    if(strlen(inbuf) > 0 && strstr(inbuf,"NIMPHI"))
      nbond = atoi(inbuf);
  } while (nbond == -1);

  return nbond;
}
    
int psf_get_impropers(FILE *f, int n, int *impropers) {
  
  /* Same format */
  return psf_get_dihedrals(f, n, impropers);
}

  

