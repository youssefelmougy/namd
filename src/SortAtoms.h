/**
***  Copyright (c) 1995, 1996, 1997, 1998, 1999, 2000 by
***  The Board of Trustees of the University of Illinois.
***  All rights reserved.
**/

#ifndef SORTATOMS_H
#define SORTATOMS_H

class FullAtom;

void sortAtomsForCUDA(int *order, const FullAtom *atoms, int nfree, int n);

#endif // SORTATOMS_H

