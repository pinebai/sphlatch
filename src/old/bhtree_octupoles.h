#ifndef BHTREE_OCTUPOLES_H
#define BHTREE_OCTUPOLES_H

/*
 *  bhtree_octupoles.h
 *
 *
 *  Created by Andreas Reufer on 07.02.08.
 *  Copyright 2008 University of Berne. All rights reserved.
 *
 */

#include "bhtree_octupole_node.h"

namespace sphlatch {
class Octupoles : public BHtree<Octupoles>
{
typedef genericNode* nodePtrT;
typedef particleNode* partPtrT;
typedef genericCellNode* cellPtrT;
typedef octupoleCellNode* octuPtrT;

public:
///
/// allocates the root monopole cell node
///
void allocRootNode()
{
  rootPtr = new octupoleCellNode;
}

///
/// report number of multipole moments
/// (includes center of mass and mass)
///
size_t noMultipoleMoments()
{
  return OSIZE; ///  3 center of mass + 1 monopole
                /// + 6 quadrupoles + 10 octupoles = 20 multipoles
}

///
/// allocates a new monopole cell node and connects it as child _n
/// no check is performed, whether curNodePtr points to a cell node!
///
void allocNewCellChild(const size_t _n)
{
// allocate new cell node
  octuPtrT newNodePtr =
    new octupoleCellNode;

// connect the new cell node to curNodePtr
  newNodePtr->parent = curNodePtr;
  static_cast<cellPtrT>(curNodePtr)->child[_n] = newNodePtr;

// set cell vars to zero
  static_cast<octuPtrT>(newNodePtr)->mass = 0.;
  static_cast<octuPtrT>(newNodePtr)->xCom = 0.;
  static_cast<octuPtrT>(newNodePtr)->yCom = 0.;
  static_cast<octuPtrT>(newNodePtr)->zCom = 0.;

  static_cast<octuPtrT>(newNodePtr)->q11 = 0.;
  static_cast<octuPtrT>(newNodePtr)->q22 = 0.;
  static_cast<octuPtrT>(newNodePtr)->q33 = 0.;
  static_cast<octuPtrT>(newNodePtr)->q12 = 0.;
  static_cast<octuPtrT>(newNodePtr)->q13 = 0.;
  static_cast<octuPtrT>(newNodePtr)->q23 = 0.;

  static_cast<octuPtrT>(newNodePtr)->s11 = 0.;
  static_cast<octuPtrT>(newNodePtr)->s22 = 0.;
  static_cast<octuPtrT>(newNodePtr)->s33 = 0.;
  static_cast<octuPtrT>(newNodePtr)->s12 = 0.;
  static_cast<octuPtrT>(newNodePtr)->s21 = 0.;
  static_cast<octuPtrT>(newNodePtr)->s13 = 0.;
  static_cast<octuPtrT>(newNodePtr)->s31 = 0.;
  static_cast<octuPtrT>(newNodePtr)->s23 = 0.;
  static_cast<octuPtrT>(newNodePtr)->s32 = 0.;
  static_cast<octuPtrT>(newNodePtr)->s123 = 0.;
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
  static valvectType curCell(OSIZE), childCell(OSIZE);

  for (size_t i = 0; i < OSIZE; i++)
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

  if (curCell[MASS] > 0.)
    {
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

                  addMP(curCell, childCell);
                }
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
  const fType rx = _source[CX] - _target[CX];
  const fType ry = _source[CY] - _target[CY];
  const fType rz = _source[CZ] - _target[CZ];
  const fType rxrx = rx * rx;
  const fType ryry = ry * ry;
  const fType rzrz = rz * rz;
  const fType rr = rxrx + ryry + rzrz;

  _target[Q11] += (3. * rxrx - rr) * _source[MASS] + _source[Q11];
  _target[Q22] += (3. * ryry - rr) * _source[MASS] + _source[Q22];
  _target[Q33] += (3. * rzrz - rr) * _source[MASS] + _source[Q33];

  _target[Q12] += (3. * rx * ry * _source[MASS]) + _source[Q12];
  _target[Q13] += (3. * rx * rz * _source[MASS]) + _source[Q13];
  _target[Q23] += (3. * ry * rz * _source[MASS]) + _source[Q23];

  _target[S11] += (5. * rxrx - 3. * rr) * rx * _source[MASS]
                  + (3. / 2.) * _source[Q11] * rx
                  - _source[Q12] * ry - _source[Q13] * rz
                  + _source[S11];

  _target[S22] += (5. * ryry - 3. * rr) * ry * _source[MASS]
                  + (3. / 2.) * _source[Q22] * ry
                  - _source[Q12] * rx - _source[Q23] * rz
                  + _source[S22];

  _target[S33] += (5. * rzrz - 3. * rr) * rz * _source[MASS]
                  + (3. / 2.) * _source[Q33] * rz
                  - _source[Q13] * rx - _source[Q23] * ry
                  + _source[S33];

  _target[S12] += (15. * rxrx - 3. * rr) * ry * _source[MASS]
                  + (5. / 2.) * _source[Q11] * ry
                  + 4. * _source[Q12] * rx
                  - _source[Q22] * ry - _source[Q23] * rz
                  + _source[S12];
  _target[S21] += (15. * ryry - 3. * rr) * rx * _source[MASS]
                  + (5. / 2.) * _source[Q22] * rx
                  - _source[Q11] * rx + 4. * _source[Q12] * ry
                  - _source[Q13] * rz
                  + _source[S21];

  _target[S13] += (15. * rxrx - 3. * rr) * rz * _source[MASS]
                  + (5. / 2.) * _source[Q11] * rz
                  + 4. * _source[Q13] * rx - _source[Q23] * ry
                  - _source[Q33] * rz
                  + _source[S13];
  _target[S31] += (15. * rzrz - 3. * rr) * rx * _source[MASS]
                  + (5. / 2.) * _source[Q33] * rx
                  - _source[Q11] * rx - _source[Q12] * ry
                  + 4. * _source[Q13] * rz
                  + _source[S31];

  _target[S23] += (15. * ryry - 3. * rr) * rz * _source[MASS]
                  + (5. / 2.) * _source[Q22] * rz
                  - _source[Q13] * rx + 4. * _source[Q23] * ry
                  - _source[Q33] * rz
                  + _source[S23];
  _target[S32] += (15. * rzrz - 3. * rr) * ry * _source[MASS]
                  + (5. / 2.) * _source[Q33] * ry
                  - _source[Q12] * rx - _source[Q22] * ry
                  + 4. * _source[Q23] * rz
                  + _source[S32];

  _target[S123] += 15. * rx * ry * rz * _source[MASS]
                   + 25. * (_source[Q12] * rz + _source[Q13] * ry
                            + _source[Q23] * rx)
                   + _source[S123];
}

///
/// copy current cell node to a vector
/// no checks are performed, whether current node is a
/// cell nor if vector has the right size
///
void cellToVect(valvectRefType _vect)
{
  _vect[CX] = static_cast<octuPtrT>(curNodePtr)->xCom;
  _vect[CY] = static_cast<octuPtrT>(curNodePtr)->yCom;
  _vect[CZ] = static_cast<octuPtrT>(curNodePtr)->zCom;
  _vect[MASS] = static_cast<octuPtrT>(curNodePtr)->mass;

  _vect[Q11] = static_cast<octuPtrT>(curNodePtr)->q11;
  _vect[Q22] = static_cast<octuPtrT>(curNodePtr)->q22;
  _vect[Q33] = static_cast<octuPtrT>(curNodePtr)->q33;
  _vect[Q12] = static_cast<octuPtrT>(curNodePtr)->q12;
  _vect[Q13] = static_cast<octuPtrT>(curNodePtr)->q13;
  _vect[Q23] = static_cast<octuPtrT>(curNodePtr)->q23;

  _vect[S11] = static_cast<octuPtrT>(curNodePtr)->s11;
  _vect[S22] = static_cast<octuPtrT>(curNodePtr)->s22;
  _vect[S33] = static_cast<octuPtrT>(curNodePtr)->s33;
  _vect[S12] = static_cast<octuPtrT>(curNodePtr)->s12;
  _vect[S21] = static_cast<octuPtrT>(curNodePtr)->s21;
  _vect[S13] = static_cast<octuPtrT>(curNodePtr)->s13;
  _vect[S31] = static_cast<octuPtrT>(curNodePtr)->s31;
  _vect[S23] = static_cast<octuPtrT>(curNodePtr)->s23;
  _vect[S32] = static_cast<octuPtrT>(curNodePtr)->s32;
  _vect[S123] = static_cast<octuPtrT>(curNodePtr)->s123;
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

  _vect[Q11] = 0.;
  _vect[Q22] = 0.;
  _vect[Q33] = 0.;
  _vect[Q12] = 0.;
  _vect[Q13] = 0.;
  _vect[Q23] = 0.;

  _vect[S11] = 0.;
  _vect[S22] = 0.;
  _vect[S33] = 0.;
  _vect[S12] = 0.;
  _vect[S21] = 0.;
  _vect[S13] = 0.;
  _vect[S31] = 0.;
  _vect[S23] = 0.;
  _vect[S32] = 0.;
  _vect[S123] = 0.;
}

///
/// copy vector to current cell node
/// no checks are performed, whether current node is a
/// cell nor if vector has the right size
///
void vectToCell(valvectRefType _vect)
{
  static_cast<octuPtrT>(curNodePtr)->xCom = _vect[CX];
  static_cast<octuPtrT>(curNodePtr)->yCom = _vect[CY];
  static_cast<octuPtrT>(curNodePtr)->zCom = _vect[CZ];
  static_cast<octuPtrT>(curNodePtr)->mass = _vect[MASS];

  static_cast<octuPtrT>(curNodePtr)->q11 = _vect[Q11];
  static_cast<octuPtrT>(curNodePtr)->q22 = _vect[Q22];
  static_cast<octuPtrT>(curNodePtr)->q33 = _vect[Q33];
  static_cast<octuPtrT>(curNodePtr)->q12 = _vect[Q12];
  static_cast<octuPtrT>(curNodePtr)->q13 = _vect[Q13];
  static_cast<octuPtrT>(curNodePtr)->q23 = _vect[Q23];

  static_cast<octuPtrT>(curNodePtr)->s11 = _vect[S11];
  static_cast<octuPtrT>(curNodePtr)->s22 = _vect[S22];
  static_cast<octuPtrT>(curNodePtr)->s33 = _vect[S33];
  static_cast<octuPtrT>(curNodePtr)->s12 = _vect[S12];
  static_cast<octuPtrT>(curNodePtr)->s21 = _vect[S21];
  static_cast<octuPtrT>(curNodePtr)->s13 = _vect[S13];
  static_cast<octuPtrT>(curNodePtr)->s31 = _vect[S31];
  static_cast<octuPtrT>(curNodePtr)->s23 = _vect[S23];
  static_cast<octuPtrT>(curNodePtr)->s32 = _vect[S32];
  static_cast<octuPtrT>(curNodePtr)->s123 = _vect[S123];
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
    (static_cast<octuPtrT>(curNodePtr)->xCom - curGravParticleX) *
    (static_cast<octuPtrT>(curNodePtr)->xCom - curGravParticleX) +
    (static_cast<octuPtrT>(curNodePtr)->yCom - curGravParticleY) *
    (static_cast<octuPtrT>(curNodePtr)->yCom - curGravParticleY) +
    (static_cast<octuPtrT>(curNodePtr)->zCom - curGravParticleZ) *
    (static_cast<octuPtrT>(curNodePtr)->zCom - curGravParticleZ)
    );
  return(((static_cast<octuPtrT>(curNodePtr)->cellSize)
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
                       - static_cast<octuPtrT>(curNodePtr)->xCom;
  const fType ry = curGravParticleY
                       - static_cast<octuPtrT>(curNodePtr)->yCom;
  const fType rz = curGravParticleZ
                       - static_cast<octuPtrT>(curNodePtr)->zCom;
  const fType mass = static_cast<octuPtrT>(curNodePtr)->mass;

  /// gravity due to monopole term
  curGravParticleAX -= (rInvPow3) * mass * rx;
  curGravParticleAY -= (rInvPow3) * mass * ry;
  curGravParticleAZ -= (rInvPow3) * mass * rz;

  /// intermediate results for quadrupole term
  const fType rInvPow5 = rInvPow3 / (cellPartDist * cellPartDist);
  const fType rInvPow7 = rInvPow5 / (cellPartDist * cellPartDist);

  const fType q11 = static_cast<octuPtrT>(curNodePtr)->q11;
  const fType q22 = static_cast<octuPtrT>(curNodePtr)->q22;
  const fType q33 = static_cast<octuPtrT>(curNodePtr)->q33;
  const fType q12 = static_cast<octuPtrT>(curNodePtr)->q12;
  const fType q13 = static_cast<octuPtrT>(curNodePtr)->q13;
  const fType q23 = static_cast<octuPtrT>(curNodePtr)->q23;

  const fType q1jrj = q11 * rx + q12 * ry + q13 * rz;
  const fType q2jrj = q12 * rx + q22 * ry + q23 * rz;
  const fType q3jrj = q13 * rx + q23 * ry + q33 * rz;
  const fType qijrirj = q11 * rx * rx +
                            q22 * ry * ry +
                            q33 * rz * rz +
                            2. * q12 * rx * ry +
                            2. * q13 * rx * rz +
                            2. * q23 * ry * rz;

  /// gravity due to quadrupole term
  curGravParticleAX += (rInvPow5) * (q1jrj) - (rInvPow7) * (2.5 * qijrirj * rx);
  curGravParticleAY += (rInvPow5) * (q2jrj) - (rInvPow7) * (2.5 * qijrirj * ry);
  curGravParticleAZ += (rInvPow5) * (q3jrj) - (rInvPow7) * (2.5 * qijrirj * rz);

  /// intermediate results for octupole term
  const fType rInvPow9 = rInvPow7 / (cellPartDist * cellPartDist);

  const fType s11 = static_cast<octuPtrT>(curNodePtr)->s11;
  const fType s22 = static_cast<octuPtrT>(curNodePtr)->s22;
  const fType s33 = static_cast<octuPtrT>(curNodePtr)->s33;
  const fType s12 = static_cast<octuPtrT>(curNodePtr)->s12;
  const fType s21 = static_cast<octuPtrT>(curNodePtr)->s21;
  const fType s13 = static_cast<octuPtrT>(curNodePtr)->s13;
  const fType s31 = static_cast<octuPtrT>(curNodePtr)->s31;
  const fType s23 = static_cast<octuPtrT>(curNodePtr)->s23;
  const fType s32 = static_cast<octuPtrT>(curNodePtr)->s32;
  const fType s123 = static_cast<octuPtrT>(curNodePtr)->s123;

  const fType s1jrj = s11 * rx + s12 * ry + s13 * rz;
  const fType s2jrj = s21 * rx + s22 * ry + s23 * rz;
  const fType s3jrj = s31 * rx + s32 * ry + s33 * rz;

  const fType si1riri = s11 * rx * rx + s21 * ry * ry + s31 * rz * rz;
  const fType si2riri = s12 * rx * rx + s22 * ry * ry + s32 * rz * rz;
  const fType si3riri = s13 * rx * rx + s23 * ry * ry + s33 * rz * rz;

  const fType sijririrj = si1riri * rx + si2riri * ry + si3riri * rz;

  /// gravity due to octupole terms
  curGravParticleAX += (rInvPow7) * (s1jrj * rx + 0.5 * si1riri
                                     + 0.5 * s123 * ry * rz)
                       - (3.5 * rInvPow9) * (sijririrj * rx
                                             + s123 * rx * ry * rz * rx);
  curGravParticleAY += (rInvPow7) * (s2jrj * ry + 0.5 * si2riri
                                     + 0.5 * s123 * rz * rx)
                       - (3.5 * rInvPow9) * (sijririrj * ry
                                             + s123 * rx * ry * rz * ry);
  curGravParticleAZ += (rInvPow7) * (s3jrj * rz + 0.5 * si3riri
                                     + 0.5 * s123 * rx * ry)
                       - (3.5 * rInvPow9) * (sijririrj * rz
                                             + s123 * rx * ry * rz * rz);
}

private:
enum octupoleIndex { CX, CY, CZ, MASS, Q11, Q22, Q33, Q12, Q13, Q23,
                     S11, S22, S33, S12, S21, S13, S31, S23, S32, S123, OSIZE };

private:
};
};

#endif
