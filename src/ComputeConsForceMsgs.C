/**
***  Copyright (c) 1995, 1996, 1997, 1998, 1999, 2000 by
***  The Board of Trustees of the University of Illinois.
***  All rights reserved.
**/

#include "ComputeConsForceMsgs.h"
#include "packmsg.h"

#include "ComputeMgr.decl.h"

PACK_MSG(ComputeConsForceMsg,
  PACK_RESIZE(aid);
  PACK_RESIZE(f);
)
