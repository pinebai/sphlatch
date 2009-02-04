#ifndef BHTREE_WORKER_TREEDUMP_H
#define BHTREE_WORKER_TREEDUMP_H

/*
 *  bhtree_worker_treedump.h
 *
 *  Created by Andreas Reufer on 14.12.08.
 *  Copyright 2007 University of Berne. All rights reserved.
 *
 */

#include <fstream>
#include "bhtree_worker.h"

namespace sphlatch {
class BHTreeDump : public BHTreeWorker {
public:
   BHTreeDump(const treePtrT _treePtr) : BHTreeWorker(_treePtr) { }
   BHTreeDump(const BHTreeDump& _dumper) : BHTreeWorker(_dumper) { }
   ~BHTreeDump() { }

public:
   void dotDump(std::string _dotFilename);

private:
   std::fstream dumpFile;

   void dotRecursor();
};

void BHTreeDump::dotDump(std::string _dotFilename)
{
   dumpFile.open(_dotFilename.c_str(), std::ios::out);

   goRoot();
   dumpFile << "digraph graphname { \n";
   dotRecursor();
   dumpFile << "}\n";
   dumpFile.close();
}

void BHTreeDump::dotRecursor()
{
   if (curPtr->isParticle)
      dumpFile << "P";
   else
      dumpFile << "C";

   dumpFile << abs(curPtr->ident) << " [";
   dumpFile << "label=\"\",";

   if (curPtr->isParticle)
      dumpFile << "shape=circle,color=green";
   else if (curPtr->isCZ)
   {
      dumpFile << "shape=diamond,";
      if (static_cast<czllPtrT>(curPtr)->atBottom)
         dumpFile << "color=darkorange";
      else
         dumpFile << "color=red";
   }
   else
      dumpFile << "shape=box,color=red";

   dumpFile << ",style=filled";
   dumpFile << ",width=0.1,height=0.1];\n";

   if (curPtr->isParticle == false)
   {
      for (size_t i = 0; i < 8; i++)
      {
         if (static_cast<cellPtrT>(curPtr)->child[i] != NULL)
         {
            dumpFile << "C" << abs(curPtr->ident)
                     << " -> ";
            if (static_cast<cellPtrT>(curPtr)->child[i]->isParticle)
               dumpFile << "P";
            else
               dumpFile << "C";

            dumpFile << abs(static_cast<cellPtrT>(curPtr)->child[i]->ident)
                     << " \n";
            goChild(i);
            dotRecursor();
            goUp();
         }
      }
   }
}
};

#endif