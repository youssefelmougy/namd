/**
***  Copyright (c) 1995, 1996, 1997, 1998, 1999, 2000 by
***  The Board of Trustees of the University of Illinois.
***  All rights reserved.
**/

#ifndef PATCH_H
#define PATCH_H

#include "NamdTypes.h"
#include "OwnerBox.h"
#include "Box.h"
#include "PositionOwnerBox.h"
#include "PositionBox.h"
#include "UniqueSortedArray.h"
#include "Lattice.h"
#include "PatchTypes.h"

typedef SortedArray<ComputeID> ComputeIDList;

class Compute;
class Sequencer;
class PatchMap;


// This the base class of homepatches and proxy patches. It maintains
// common functions of these patches. These include managing dependences
// between compute (force) objects and the patch and updating atom map.

class Patch
{
  public:

     Patch(PatchID pd);
     int hasNewAtoms() { return _hasNewAtoms; }
     virtual ~Patch(void) { };

     // methods for use by Compute objects
     PositionBox<Patch>* registerPositionPickup(ComputeID cid, int trans = 13);
     void unregisterPositionPickup(ComputeID cid,
				   PositionBox<Patch>**const box);
     PositionBox<Patch>* registerAvgPositionPickup(ComputeID cid, int trans = 13);
     void unregisterAvgPositionPickup(ComputeID cid,
				   PositionBox<Patch>**const box);
     Box<Patch,Results>* registerForceDeposit(ComputeID cid);
     void unregisterForceDeposit(ComputeID cid, Box<Patch,Results> **const box);

     // methods for use by Sequencer or ProxyManager
     // void positionsReady(void) { positionsReady(0); }
     void positionsReady(int n=0);

     // methods for Box callbacks
     void positionBoxClosed(void);
     void forceBoxClosed(void);
     void avgPositionBoxClosed(void);

     int getNumAtoms() { return numAtoms; }

     // DMK - Atom Separation (water vs. non-water)
     #if NAMD_SeparateWaters != 0
       int getNumWaterAtoms() { return numWaterAtoms; }
     #endif

     int getNumFixedAtoms() { return numFixedAtoms; }  // not updated
     void setNumFixedAtoms(int numFixed) { numFixedAtoms=numFixed; }  // not updated
     PatchID getPatchID() { return patchID; }
     int getNumComputes() { return positionComputeList.size(); }

#ifdef MEM_OPT_VERSION
     CompAtomExt* getCompAtomExtInfo() { return pExt.begin(); }
#endif

     Lattice &lattice;
     Flags flags;

  protected:

     const PatchID patchID;
     int           numAtoms;
     int           numFixedAtoms;
     CompAtomList  p;
     CompAtomList  p_avg;

     // DMK - Atom Separation (water vs. non-water)
     #if NAMD_SeparateWaters != 0
       int numWaterAtoms;  // Set numWaters to the number of water atoms at
                           //   the lead of the atoms list.  If numWaters is
                           //   set to -1, this should indicate that
                           //   atoms has not been separated yet.
     #endif

#ifdef MEM_OPT_VERSION
     CompAtomExtList pExt;
#endif     

     CompAtom      *positionPtr;
     CompAtom      *avgPositionPtr;
     ForceList     f[Results::maxNumForces];
     Results	   results;

     PositionOwnerBox<Patch> positionBox;
     ComputeIDList              positionComputeList;
     PositionOwnerBox<Patch> avgPositionBox;
     ComputeIDList              avgPositionComputeList;
     OwnerBox<Patch,Results>    forceBox;
     ComputeIDList              forceComputeList;

     virtual void boxClosed(int /* box */) = 0;
     int boxesOpen;

     int _hasNewAtoms;

  private:
  

};


#endif

