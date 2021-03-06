#ifndef RANKSPACE_H
#define RANKSPACE_H

/*
 *  rankspace.h
 *
 *
 *  Created by Andreas Reufer on 10.04.08.
 *  Copyright 2008 University of Berne. All rights reserved.
 *
 */

#include "typedefs.h"
#include "particle_manager.h"
#include "neighsearch_errhandler.h"

namespace sphlatch {
class Rankspace {

typedef sphlatch::ParticleManager partManagerType;
partManagerType& PartManager;
matrixRefType pos;

///
/// constructor
///
public:
Rankspace() :
  PartManager(partManagerType::instance()),
  pos(PartManager.pos)
{
  /// ugly!
  neighbourList.resize(100);
  neighDistList.resize(100);
};

///
/// destructor
///
~Rankspace(void)
{
}

private:
partsIndexVectType indicesRankedX, indicesRankedY, indicesRankedZ,
                   rsIndicesX, rsIndicesY, rsIndicesZ;

valvectType cellMinX, cellMinY, cellMinZ,
            cellMaxX, cellMaxY, cellMaxZ;

size_t noParts, noCells1D, noCells2D, noCells3D, partsPerCell1D;
domainPartsIndexType rankspaceCells;
public:

///
/// small helper class to sort the rankspace index vectors
///
private:
template <size_t _idx> class smallerThan {
typedef sphlatch::ParticleManager partManagerType;
partManagerType& PartManager;

public:
smallerThan(void) :
  PartManager(partManagerType::instance())
{
}

bool operator()(const size_t& _i, const size_t& _j)
{
  return(PartManager.pos(_i, _idx) < PartManager.pos(_j, _idx));
}
};

public:
void prepare()
{
  noParts = PartManager.getNoLocalParts() +
            PartManager.getNoGhostParts();

  ///
  /// for some distributions changing this factors increases the speed of
  /// Rankspace considerably!
  ///
  noCells1D = lrint(1.00 * ceil(pow(static_cast<double>(noParts), 1. / 3.)));
  noCells2D = noCells1D * noCells1D;
  noCells3D = noCells2D * noCells1D;

  partsPerCell1D = lrint(ceil(static_cast<double>(noParts) /
                              static_cast<double>(noCells1D)));

  rankspaceCells.resize(noCells3D);

  indicesRankedX.resize(noParts);
  indicesRankedY.resize(noParts);
  indicesRankedZ.resize(noParts);

  rsIndicesX.resize(noParts);
  rsIndicesY.resize(noParts);
  rsIndicesZ.resize(noParts);

  for (size_t i = 0; i < noParts; i++)
    {
      indicesRankedX[i] = i;
      indicesRankedY[i] = i;
      indicesRankedZ[i] = i;
    }

  std::sort(indicesRankedX.begin(), indicesRankedX.end(), smallerThan<X>());
  std::sort(indicesRankedY.begin(), indicesRankedY.end(), smallerThan<Y>());
  std::sort(indicesRankedZ.begin(), indicesRankedZ.end(), smallerThan<Z>());

  ///
  /// calculate the inverse of the ranked indices
  ///
  for (size_t i = 0; i < noParts; i++)
    {
      rsIndicesX[ indicesRankedX[i] ] = i;
      rsIndicesY[ indicesRankedY[i] ] = i;
      rsIndicesZ[ indicesRankedZ[i] ] = i;
    }

  ///
  /// prepare the cell min/max containers
  ///

  /// determine differently

  cellMinX.resize(noCells1D);
  cellMinY.resize(noCells1D);
  cellMinZ.resize(noCells1D);
  cellMaxX.resize(noCells1D);
  cellMaxY.resize(noCells1D);
  cellMaxZ.resize(noCells1D);

  ///
  /// determine the cell min/max
  ///
  size_t minRank, maxRank;
  for (size_t i = 0; i < noCells1D; i++)
    {
      minRank = i * partsPerCell1D;
      maxRank = std::min(minRank + partsPerCell1D - 1, noParts - 1);

      cellMinX[i] = pos(indicesRankedX[minRank], X);
      cellMaxX[i] = pos(indicesRankedX[maxRank], X);
      cellMinY[i] = pos(indicesRankedY[minRank], Y);
      cellMaxY[i] = pos(indicesRankedY[maxRank], Y);
      cellMinZ[i] = pos(indicesRankedZ[minRank], Z);
      cellMaxZ[i] = pos(indicesRankedZ[maxRank], Z);
    }

  indicesRankedX.resize(0);
  indicesRankedY.resize(0);
  indicesRankedZ.resize(0);

  ///
  /// fill into a temporary rankspace cells, will be copied later on
  /// to the actual data structure. also copy the cell indices for each
  /// particle into rsIndices
  ///

  std::vector<std::list<size_t> > tmpRSCells;
  tmpRSCells.resize(noCells3D);
  for (size_t i = 0; i < noCells3D; i++)
    {
      tmpRSCells[i].clear();
    }

  size_t curCellX, curCellY, curCellZ, cartIndex;
  for (size_t i = 0; i < noParts; i++)
    {
      curCellX = (rsIndicesX[i] - (rsIndicesX[i] % partsPerCell1D))
                 / partsPerCell1D;
      curCellY = (rsIndicesY[i] - (rsIndicesY[i] % partsPerCell1D))
                 / partsPerCell1D;
      curCellZ = (rsIndicesZ[i] - (rsIndicesZ[i] % partsPerCell1D))
                 / partsPerCell1D;

      cartIndex = curCellX + curCellY * noCells1D + curCellZ * noCells2D;

#ifdef SPHLATCH_RANKSPACESERIALIZE
      tmpRSCells[cartIndex].push_back(i);
#else
      rankspaceCells[cartIndex].push_back(i);
#endif

      ///
      /// replace indices rsIndices by cell indices
      ///
      rsIndicesX[i] = curCellX;
      rsIndicesY[i] = curCellY;
      rsIndicesZ[i] = curCellZ;
    }

#ifdef SPHLATCH_RANKSPACESERIALIZE
  ///
  /// now reset rankspaceCells and instantate each cells vector
  /// directly with the needed size, this should guarantee better
  /// memory locality for the cell elements
  ///
  rankspaceCells.resize(0);
  rankspaceCells.resize(noCells3D);
  for (size_t i = 0; i < noCells3D; i++)
    {
      rankspaceCells[i].resize(tmpRSCells[i].size());

      std::list<size_t>::const_iterator listItr = tmpRSCells[i].begin();
      std::list<size_t>::const_iterator listEnd = tmpRSCells[i].end();

      partsIndexVectType::iterator vectItr = rankspaceCells[i].begin();

      while (listItr != listEnd)
        {
          *vectItr = *listItr;
          listItr++;
          vectItr++;
        }
    }
#endif
}

public:
partsIndexVectType neighbourList;
valvectType neighDistList;

private:
fType curPartX, curPartY, curPartZ;
size_t minCellX, maxCellX, minCellY, maxCellY, minCellZ, maxCellZ;
public:
void findNeighbours(const size_t _curPartIndex,
                    const fType _search_radius)
{
  curPartX = pos(_curPartIndex, X);
  curPartY = pos(_curPartIndex, Y);
  curPartZ = pos(_curPartIndex, Z);

  minCellX = rsIndicesX[_curPartIndex];
  maxCellX = rsIndicesX[_curPartIndex];
  minCellY = rsIndicesY[_curPartIndex];
  maxCellY = rsIndicesY[_curPartIndex];
  minCellZ = rsIndicesZ[_curPartIndex];
  maxCellZ = rsIndicesZ[_curPartIndex];

#ifdef SPHLATCH_CHECKNONEIGHBOURS
  /// the first index is used for neighbour counting
  /// and cannot be used for a neighbour
  const int noMaxNeighs = neighbourList.size() - 1;
#endif

  ///
  /// determine min. and max. rank in each dimension
  ///
  while ((curPartX - _search_radius) < cellMinX[minCellX ])
    {
      if (minCellX == 0)
        break;
      minCellX--;
    }

  while ((curPartX + _search_radius) > cellMaxX[maxCellX])
    {
      if (maxCellX == noCells1D - 1)
        break;
      maxCellX++;
    }

  while ((curPartY - _search_radius) < cellMinY[minCellY])
    {
      if (minCellY == 0)
        break;
      minCellY--;
    }

  while ((curPartY + _search_radius) > cellMaxY[maxCellY])
    {
      if (maxCellY == noCells1D - 1)
        break;
      maxCellY++;
    }

  while ((curPartZ - _search_radius) < cellMinZ[minCellZ])
    {
      if (minCellZ == 0)
        break;
      minCellZ--;
    }

  while ((curPartZ + _search_radius) > cellMaxZ[maxCellZ])
    {
      if (maxCellZ == noCells1D - 1)
        break;
      maxCellZ++;
    }

  const fType searchRadPow2 = _search_radius * _search_radius;

  static size_t noNeighbours;
  noNeighbours = 0;

  for (size_t curCellX = minCellX; curCellX <= maxCellX; curCellX++)
    {
      for (size_t curCellY = minCellY; curCellY <= maxCellY; curCellY++)
        {
          for (size_t curCellZ = minCellZ; curCellZ <= maxCellZ; curCellZ++)
            {
              const size_t cartIndex = curCellX + curCellY * noCells1D
                                       + curCellZ * noCells2D;
              const size_t noPartsInCell = rankspaceCells[cartIndex].size();

              for (size_t i = 0; i < noPartsInCell; i++)
                {
                  const size_t neighIndex = (rankspaceCells[cartIndex])[i];

                  const fType neighDistPow2 =
                    (pos(neighIndex, X) - curPartX) *
                    (pos(neighIndex, X) - curPartX) +
                    (pos(neighIndex, Y) - curPartY) *
                    (pos(neighIndex, Y) - curPartY) +
                    (pos(neighIndex, Z) - curPartZ) *
                    (pos(neighIndex, Z) - curPartZ);

                  if (neighDistPow2 < searchRadPow2)
                    {
                      noNeighbours++;
#ifdef SPHLATCH_CHECKNONEIGHBOURS
                      if ( static_cast<int>(noNeighbours) > noMaxNeighs )
                        throw TooManyNeighs( _curPartIndex,
                                             noNeighbours,
                                             neighbourList,
                                             neighDistList );
#endif
                      
                      neighbourList[noNeighbours] = neighIndex;
                      neighDistList(noNeighbours) = sqrt(neighDistPow2);

                    }
                }
            }
        }
    }
  neighbourList[0] = noNeighbours;
}
};
};

#endif
