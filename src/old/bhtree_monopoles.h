#ifndef BHTREE_MONOPOLES_H
#define BHTREE_MONOPOLES_H

/*
 *  bhtree_monopoles.h
 *
 *
 *  Created by Andreas Reufer on 07.02.08.
 *  Copyright 2008 University of Berne. All rights reserved.
 *
 */

#include "bhtree_monopole_node.h"

namespace sphlatch {
class Monopoles : public BHtree<Monopoles>
{
typedef genericNode* nodePtrT;
typedef particleNode* partPtrT;
typedef genericCellNode* cellPtrT;
typedef monopoleCellNode* monoPtrT;

public:
///
/// allocates the root monopole cell node
///
void allocRootNode()
{
  rootPtr = new monopoleCellNode;
}

///
/// report number of multipole moments
/// (includes center of mass and mass)
///
size_t noMultipoleMoments()
{
  return MSIZE; ///  3 center of mass + 1 monopoles = 4 multipoles
}

///
/// allocates a new monopole cell node and connects it as child _n
/// no check is performed, whether curNodePtr points to a cell node!
///
void allocNewCellChild(const size_t _n)
{
// allocate new cell node
  monoPtrT newNodePtr =
    new monopoleCellNode;

// connect the new cell node to curNodePtr
  newNodePtr->parent = curNodePtr;
  static_cast<cellPtrT>(curNodePtr)->child[_n] = newNodePtr;

// set cell vars to zero
  static_cast<monoPtrT>(newNodePtr)->mass = 0.;
  static_cast<monoPtrT>(newNodePtr)->xCom = 0.;
  static_cast<monoPtrT>(newNodePtr)->yCom = 0.;
  static_cast<monoPtrT>(newNodePtr)->zCom = 0.;
}

///
/// calculate multipole from children
/// this function does not check, whether the current node is actually a cell!
///
void calcMultipole()
{
//
// add up the contributions from the children with
// the same locality as the current node.
// all this locality business guarantees, that every particle
// contributes only ONCE to the multipole moments of the global tree
// but "ghost" cells still contain the multipoles contributed by
// their ghost children.
//
// a special case are the deepest toptree cells which are per
// definition local. if all children are ghosts, none of if contri-
// butes anything so cm gets 0 and fucks up the center of mass
// of the cell. so if nobody contributes anything, omit the addition
// to the cell.
//
  static valvectType curCell(MSIZE), childCell(MSIZE);

  for (size_t i = 0; i < MSIZE; i++)
    {
      curCell[i] = 0.;
    }

// first calculate center of mass
  for (size_t i = 0; i < 8; i++)
    {
      if (static_cast<cellPtrT>(curNodePtr)->child[i] != NULL)
        {
          if (static_cast<cellPtrT>(curNodePtr)->child[i]->isLocal
              == curNodePtr->isLocal)
            {
              goChild(i);
              if (curNodePtr->isParticle == true)
                {
                  partToVect(childCell);
                }
              else
                {
                  cellToVect(childCell);
                }
              goUp();

              addCOM(curCell, childCell);
            }
        }
    }

// copy data to node itself ...
  if (curCell[MASS] > 0.)
    {
      vectToCell(curCell);
    }
}

///
/// add center of mass
///
void addCOM(valvectRefType _target, const valvectRefType _source)
{
  /// const static does not seem to work here
  const fType oldMass = _target[MASS];

  _target[MASS] += _source[MASS];

  if (_target[MASS] > 0.)
    {
      /// const static does not seem to work here
      const fType newMassInv = (1.0 / _target[MASS]);
      _target[CX] = newMassInv * (oldMass * _target[CX]
                                  + _source[MASS] * _source[CX]);
      _target[CY] = newMassInv * (oldMass * _target[CY]
                                  + _source[MASS] * _source[CY]);
      _target[CZ] = newMassInv * (oldMass * _target[CZ]
                                  + _source[MASS] * _source[CZ]);
    }
}

///
/// add multipole moments
/// either for a cell by its children multipole moments or to add up
/// multipoles globally in the top-tree
///
void addMP(valvectRefType _target, const valvectRefType _source)
{
}

///
/// copy current cell node to a vector
/// no checks are performed, whether current node is a
/// cell nor if vector has the right size
///
void cellToVect(valvectRefType _vect)
{
  _vect[CX] = static_cast<monoPtrT>(curNodePtr)->xCom;
  _vect[CY] = static_cast<monoPtrT>(curNodePtr)->yCom;
  _vect[CZ] = static_cast<monoPtrT>(curNodePtr)->zCom;
  _vect[MASS] = static_cast<monoPtrT>(curNodePtr)->mass;
}

///
/// copy current particle node to a vector
/// no checks are performed, whether current node is a
/// particle nor if vector has the right size
///
void partToVect(valvectRefType _vect)
{
  _vect[CX] = static_cast<partPtrT>(curNodePtr)->xPos;
  _vect[CY] = static_cast<partPtrT>(curNodePtr)->yPos;
  _vect[CZ] = static_cast<partPtrT>(curNodePtr)->zPos;
  _vect[MASS] = static_cast<partPtrT>(curNodePtr)->mass;
}

///
/// copy vector to current cell node
/// no checks are performed, whether current node is a
/// cell nor if vector has the right size
///
void vectToCell(valvectRefType _vect)
{
  static_cast<monoPtrT>(curNodePtr)->xCom = _vect[CX];
  static_cast<monoPtrT>(curNodePtr)->yCom = _vect[CY];
  static_cast<monoPtrT>(curNodePtr)->zCom = _vect[CZ];
  static_cast<monoPtrT>(curNodePtr)->mass = _vect[MASS];
}


///
/// stop recursion if:
/// - current node is empty << ??
/// - MAC is fulfilled
///
bool calcGravMAC(void)
{
  /// any way to speed this up?
  cellPartDist = sqrt(
    (static_cast<monoPtrT>(curNodePtr)->xCom - curGravParticleX) *
    (static_cast<monoPtrT>(curNodePtr)->xCom - curGravParticleX) +
    (static_cast<monoPtrT>(curNodePtr)->yCom - curGravParticleY) *
    (static_cast<monoPtrT>(curNodePtr)->yCom - curGravParticleY) +
    (static_cast<monoPtrT>(curNodePtr)->zCom - curGravParticleZ) *
    (static_cast<monoPtrT>(curNodePtr)->zCom - curGravParticleZ)
    );
  return(((static_cast<monoPtrT>(curNodePtr)->cellSize)
          / cellPartDist) < thetaMAC);
}


///
/// calculate acceleration due to a cell
/// no check whether current node is actually a cell!
///
void calcGravCell()
{
  /// cellPartDist is already set by the MAC function

  /// intermediate results for monopole term
  const fType rInvPow3 = 1. / (cellPartDist * cellPartDist * cellPartDist);

  const fType rx = curGravParticleX
                       - static_cast<monoPtrT>(curNodePtr)->xCom;
  const fType ry = curGravParticleY
                       - static_cast<monoPtrT>(curNodePtr)->yCom;
  const fType rz = curGravParticleZ
                       - static_cast<monoPtrT>(curNodePtr)->zCom;
  const fType mass = static_cast<monoPtrT>(curNodePtr)->mass;

  /// gravity due to monopole term
  curGravParticleAX -= (rInvPow3) * mass * rx;
  curGravParticleAY -= (rInvPow3) * mass * ry;
  curGravParticleAZ -= (rInvPow3) * mass * rz;
}

private:
enum monopoleIndex { CX, CY, CZ, MASS, MSIZE };

private:
};
};

#endif
