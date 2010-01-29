#ifndef SPHLATCH_CLUMP_FINDER_CPP
#define SPHLATCH_CLUMP_FINDER_CPP

/*
 *  clump_finder.cpp
 *
 *
 *  Created by Andreas Reufer on 10.12.09
 *  Copyright 2009 University of Berne. All rights reserved.
 *
 */

#include <list>

#include "typedefs.h"
#include "particle_set.cpp"
#include "clump_particle.h"

#include "bhtree.cpp"
#include "bhtree_worker_neighfunc.cpp"

namespace sphlatch {

template<typename _partT>
class Clump
{
public:
   typedef std::list<_partT*>   partPtrLT;
   vect3dT com;
   vect3dT vel;
   vect3dT L;
   vect3dT P;
   fType   m;

   cType id;

   void addParticle(_partT* _partPtr)
   {
      pList.push_back(_partPtr);
   }

   bool operator<(const Clump& _rhs)
   {
      return(m < _rhs.m);
   }

   void calcProperties()
   {
      typename partPtrLT::const_iterator pItr;
      m   = 0.;
      com = 0., 0., 0.;

      for (pItr = pList.begin(); pItr != pList.end(); pItr++)
      {
         const fType mi = (*pItr)->m;
         m   += mi;
         P   += mi * (*pItr)->vel;
         com += mi * (*pItr)->pos;
      }
      com /= m;
      vel  = P / m;

      L = 0., 0., 0.;
      for (pItr = pList.begin(); pItr != pList.end(); pItr++)
      {
         const fType   mi   = (*pItr)->m;
         const vect3dT rvec = (*pItr)->pos - com;
         L += mi * cross(rvec, (*pItr)->vel);
      }
   }

private:
   partPtrLT pList;
};
  
  
template<typename _partT>
class Clumps : public std::list< Clump<_partT> >
{
public:
   typedef _partT                partT;
   typedef ParticleSet<_partT>   partSetT;
   typedef std::list<_partT*>    partPtrLT;

   typedef BHTree                treeT;

   typedef Clump<_partT>         clumpT;
   typedef std::list<clumpT> parentT;

public:
   Clumps() { }
   ~Clumps() { }

   void getClumps(partSetT& _parts, const fType _rhoMin);

private:
   cType mergeClumps(const cType cida, const cType cidb, partSetT& _parts);
};

template<typename _partT>
cType Clumps<_partT>::mergeClumps(const cType cida, const cType cidb,
                                       partSetT& _parts)
{
   if (cida == cidb)
      return(cida);

   cType nid, oid;

   if (cida < cidb)
   {
      nid = cida;
      oid = cidb;
   }
   else
   {
      nid = cidb;
      oid = cida;
   }

   const size_t nop = _parts.getNop();
   for (size_t i = 0; i < nop; i++)
   {
      if (_parts[i].clumpid == oid)
         _parts[i].clumpid = nid;
   }

   return(nid);
}

template<typename _partT>
void Clumps<_partT>::getClumps(ParticleSet<_partT>& _parts,
                                    const fType          _rhoMin)
{
   treeT& Tree(treeT::instance());

   const size_t nop       = _parts.getNop();
   const fType  costppart = 1. / nop;

   Tree.setExtent(_parts.getBox() * 1.1);

   for (size_t i = 0; i < nop; i++)
   {
      _parts[i].cost = costppart;
      Tree.insertPart(_parts[i]);
   }
   Tree.update(0.8, 1.2);

   for (size_t i = 0; i < nop; i++)
      _parts[i].clumpid = CLUMPNOTSET;

   cType nextFreeId = 1;

   NeighFindWorker<_partT> NFW(&Tree);
   for (size_t i = 0; i < nop; i++)
   {
      partT* cPart = &(_parts[i]);

      //
      // if density is too low, particle does not belong to any clump
      // regardless of previous state
      //
      if (_parts[i].rho < _rhoMin)
      {
         _parts[i].clumpid = CLUMPNONE;
         continue;
      }
      else
      {
         if ((_parts[i].clumpid == CLUMPNONE) ||
             (_parts[i].clumpid == CLUMPNOTSET))
         {
            partPtrLT neighs = NFW(cPart, cPart->h);

            std::set<cType> neighClumps;
            typename partPtrLT::iterator nitr;
            for (nitr = neighs.begin(); nitr != neighs.end(); nitr++)
               if (((*nitr)->rho > _rhoMin) && ((*nitr)->clumpid > 0))
                  neighClumps.insert((*nitr)->clumpid);

            const size_t noClumpsFound = neighClumps.size();
            switch (noClumpsFound)
            {
            case 0:
            {
               // so all particles
               cType nclumpid = nextFreeId;
               nextFreeId++;

               partPtrLT clumpees;
               for (nitr = neighs.begin(); nitr != neighs.end(); nitr++)
                  if ((*nitr)->rho > _rhoMin)
                  {
                     (*nitr)->clumpid = CLUMPONLIST;
                     clumpees.push_back(*nitr);
                  }
                  else
                     (*nitr)->clumpid = CLUMPNONE;

               // start a new clump.
               size_t nocp = 0;
               while (not clumpees.empty())
               {
                  partT* curf = clumpees.back();
                  clumpees.pop_back();

                  curf->clumpid = nclumpid;
                  nocp++;

                  neighs.clear();
                  neighs = NFW(curf, curf->h);

                  for (nitr = neighs.begin(); nitr != neighs.end(); nitr++)
                  {
                     if ((*nitr)->rho > _rhoMin)
                     {
                        if ((*nitr)->clumpid == nclumpid)
                           continue;

                        switch ((*nitr)->clumpid)
                        {
                        case CLUMPNONE:
                           (*nitr)->clumpid = CLUMPONLIST;
                           clumpees.push_back(*nitr);
                           break;

                        case CLUMPNOTSET:
                           (*nitr)->clumpid = CLUMPONLIST;
                           clumpees.push_back(*nitr);
                           break;

                        case CLUMPONLIST:
                           break;

                        default:
                           nclumpid = mergeClumps((*nitr)->clumpid,
                                                  nclumpid, _parts);
                           (*nitr)->clumpid = nclumpid;
                           break;
                        }
                     }
                     else
                     {
                        (*nitr)->clumpid = CLUMPNONE;
                     }
                  }
               }
            }
               break;

            case 1:
            {
               cPart->clumpid = *(neighClumps.begin());
            }
               break;

            default:
            {
               cType nclumpid = *(neighClumps.begin());

               std::set<cType>::const_iterator ncItr;
               for (ncItr = neighClumps.begin(); ncItr != neighClumps.end();
                    ncItr++)
                  nclumpid = mergeClumps(*ncItr, nclumpid, _parts);

               cPart->clumpid = nclumpid;
            }
               break;
            }
         }
         else
         { }
      }
   }
   Tree.clear();

   // search for all clump ids
   std::set<cType> clumpIDs;

   for (size_t i = 0; i < nop; i++)
      if (_parts[i].clumpid > 0)
         clumpIDs.insert(_parts[i].clumpid);

   std::set<cType>::const_iterator cidItr;
   for (cidItr = clumpIDs.begin(); cidItr != clumpIDs.end(); cidItr++)
   {
      clumpT curClump;
      const cType cid = *cidItr;
      curClump.id = cid;

      for (size_t i = 0; i < nop; i++)
        if ( _parts[i].clumpid == cid )
          curClump.addParticle( &(_parts[i]) );

      curClump.calcProperties();

      parentT::push_back( curClump );
   }
   parentT::sort();

   return;
}
}
#endif

