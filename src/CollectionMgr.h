/**
***  Copyright (c) 1995, 1996, 1997, 1998, 1999, 2000 by
***  The Board of Trustees of the University of Illinois.
***  All rights reserved.
**/

#ifndef COLLECTIONMGR_H
#define COLLECTIONMGR_H

#include "charm++.h"

#include "main.h"
#include "NamdTypes.h"
#include "BOCgroup.h"
#include "PatchMap.h"
#include "ProcessorPrivate.h"
#include "CollectionMgr.decl.h"


class SlaveInitMsg : public CMessage_SlaveInitMsg
{
public:
  CkChareID master;
};

class CollectionMgr : public BOCclass
{
public:

  static CollectionMgr *Object() { 
    return CpvAccess(CollectionMgr_instance); 
  }
  CollectionMgr(SlaveInitMsg *msg);
  ~CollectionMgr(void);

  void submitPositions(int seq, FullAtomList &a, Lattice l, int prec);
  void submitVelocities(int seq, FullAtomList &a);

  class CollectVectorInstance
  {
  public:

    CollectVectorInstance(void) : seq(-1) { ; }

    CollectVectorInstance(int s) : seq(s) { ; }

    CollectVectorInstance(int s, int p) : seq(s), precisions(p),
      remaining(PatchMap::Object()->numHomePatches()) { ; }

    // true -> send it and delete it!
    int append(AtomIDList &a, ResizeArray<Vector> &d)
    {
      int size = a.size();
      for( int i = 0; i < size; ++i )
      {
	aid.add(a[i]);
	if ( precisions & 2 ) data.add(d[i]);
	if ( precisions & 1 ) fdata.add(d[i]);
      }
      return ( ! --remaining );
    }

    int seq;
    AtomIDList aid;
    int precisions;
    ResizeArray<Vector> data;
    ResizeArray<FloatVector> fdata;

  private:
    int remaining;

  };

  class CollectVectorSequence
  {
  public:

    CollectVectorInstance* submitData(
	int seq, AtomIDList &i, ResizeArray<Vector> &d, int prec=2)
    {
      CollectVectorInstance **c = data.begin();
      CollectVectorInstance **c_e = data.end();
      for( ; c != c_e && (*c)->seq != seq; ++c );
      if ( c == c_e )
      {
	data.add(new CollectVectorInstance(seq,prec));
	c = data.end() - 1;
      }
      if ( (*c)->append(i,d) )
      {
	CollectVectorInstance *i = *c;
	data.del(c - data.begin());
        return i;
      }
      else
      {
        return 0;
      }
    }

    ResizeArray<CollectVectorInstance*> data;

  };
private:

  CkChareID master;


  CollectVectorSequence positions;
  CollectVectorSequence velocities;

};

#endif

