//#define SPHLATCH_CARTESIAN_XYZ
//#define SPHLATCH_CARTESIAN_YZX
//#define SPHLATCH_CARTESIAN_ZXY
#define SPHLATCH_HILBERT3D

// uncomment for single-precision calculation
//#define SPHLATCH_SINGLEPREC

// enable parallel version
#define SPHLATCH_PARALLEL

//#define SPHLATCH_RANKSPACESERIALIZE

// enable checking of bounds for the neighbour lists
//#define SPHLATCH_CHECKNONEIGHBOURS

#include <cstdlib>
#include <iostream>
#include <iomanip>
#include <fstream>
#include <string>

#include <boost/program_options/option.hpp>
#include <boost/program_options/cmdline.hpp>
#include <boost/program_options/options_description.hpp>
#include <boost/program_options/variables_map.hpp>
#include <boost/program_options/parsers.hpp>
#include <boost/program_options/positional_options.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/assign/std/vector.hpp>

namespace po = boost::program_options;

#include "typedefs.h"
typedef sphlatch::valueType valueType;
typedef sphlatch::identType identType;
typedef sphlatch::valvectType valvectType;

typedef sphlatch::valvectRefType valvectRefType;
typedef sphlatch::idvectRefType idvectRefType;
typedef sphlatch::matrixRefType matrixRefType;

#include "particle_manager.h"
typedef sphlatch::ParticleManager part_type;

#include "io_manager.h"
typedef sphlatch::IOManager io_type;

#include "communication_manager.h"
typedef sphlatch::CommunicationManager comm_type;

#include "costzone.h"
typedef sphlatch::CostZone costzone_type;

#include "kernel_cubicspline3d.h"
typedef sphlatch::CubicSpline3D kernel_type;

#include "rankspace.h"
typedef sphlatch::Rankspace neighsearch_type;

#include "eos_tillotson.h"
typedef sphlatch::Tillotson eos_type;

#include "lagrange_sphere1D_solver.h"
typedef sphlatch::LagrangeSphere1DSolver lg1D_solver_type;

#include "lookup_table.h"
typedef sphlatch::LookupTable lut_type;

using namespace boost::assign;
using namespace sphlatch::vectindices;

void densPress()
{
  part_type& PartManager(part_type::instance());
  comm_type& CommManager(comm_type::instance());
  costzone_type& CostZone(costzone_type::instance());

  matrixRefType pos(PartManager.pos);

  valvectRefType m(PartManager.m);
  valvectRefType h(PartManager.h);
  valvectRefType u(PartManager.u);
  valvectRefType p(PartManager.p);
  valvectRefType cs(PartManager.cs);
  valvectRefType rho(PartManager.rho);

  idvectRefType id(PartManager.id);
  idvectRefType noneigh(PartManager.noneigh);

  const size_t noParts = PartManager.getNoLocalParts();
  const size_t noTotParts = noParts + PartManager.getNoGhostParts();

  /// send ghosts to other domains
  CommManager.sendGhosts(pos);
  CommManager.sendGhosts(id);
  CommManager.sendGhosts(m);
  CommManager.sendGhosts(h);

  ///
  /// define kernel and neighbour search algorithm
  ///
  kernel_type Kernel;
  neighsearch_type Nsearch;
  Nsearch.prepare();
  Nsearch.neighbourList.resize(1024);
  Nsearch.neighDistList.resize(1024);

  ///
  /// 1st SPH sum: density
  /// I need    : pos, h, m
  /// I provide : rho
  ///
  for (size_t k = 0; k < noParts; k++)
    {
      ///
      /// find neighbours
      ///
      const size_t i = k;
      const valueType hi = h(i);
      const valueType srchRad = 2. * hi;
      Nsearch.findNeighbours(i, srchRad);

      const size_t noNeighs = Nsearch.neighbourList[0];

      ///
      /// store the number of neighbours
      ///
      noneigh(i) = noNeighs;

      static valueType rhoi;
      rhoi = 0.;

      ///
      /// sum over neighbours
      ///
      for (size_t curNeigh = 1; curNeigh <= noNeighs; curNeigh++)
        {
          const valueType r = Nsearch.neighDistList[curNeigh];
          const size_t j = Nsearch.neighbourList[curNeigh];

          const valueType hij = 0.5 * (hi + h(j));

          rhoi += m(j) * Kernel.value(r, hij);
        }
      rho(i) = rhoi;
    }


  ///
  /// calculate pressure
  ///
  eos_type& EOS(eos_type::instance());
  for (size_t k = 0; k < noParts; k++)
    {
      EOS(k, p(k), cs(k));
    }
}


int main(int argc, char* argv[])
{
  MPI::Init(argc, argv);

  po::options_description Options("Global Options");
  Options.add_options()
  ("help,h", "Produces this Help")
  ("output-file,o", po::value<std::string>(), "output file")
  ("input-file,i", po::value<std::string>(), "input  file");

  po::variables_map VMap;
  po::store(po::command_line_parser(argc, argv).options(Options).run(), VMap);
  po::notify(VMap);

  if (VMap.count("help"))
    {
      std::cerr << Options << std::endl;
      return EXIT_FAILURE;
    }

  if (!VMap.count("output-file"))
    {
      std::cerr << Options << std::endl;
      return EXIT_FAILURE;
    }

  io_type&        IOManager(io_type::instance());
  part_type&      PartManager(part_type::instance());
  comm_type&      CommManager(comm_type::instance());
  costzone_type&  CostZone(costzone_type::instance());

  const valueType mTot = 5.3014e27;
  const valueType mCore = 0.30 * mTot;
  const valueType mMantle = 0.70 * mTot;

  const identType matCore = 5;
  const identType matMantle = 4;

  ///
  /// guess for the density
  ///
  const valueType rhoCore = 6.;
  const valueType rhoMantle = 3.;

  const valueType rCoreGuess = pow(0.75 * mCore / (M_PI * rhoCore), 1. / 3.);
  const valueType rMantleGuess = pow(0.75 * mMantle / (M_PI * rhoMantle)
                                + pow(rCoreGuess, 3.), 1. / 3.);
  const size_t noCells = 200;
  const size_t noEdges = noCells + 1;

  const size_t noCoreCells = lrint(noCells * (rCoreGuess / rMantleGuess));
  const size_t noMantleCells = noCells - noCoreCells;

  lg1D_solver_type Solver(noCells);

  const valueType drCore = rCoreGuess / noCoreCells;
  const valueType drMantle = (rMantleGuess - rCoreGuess) / noMantleCells;

  ///
  /// set the shell edges
  ///
  Solver.r(0) = 0.;
  Solver.v(0) = 0.;
  for (size_t i = 1; i < noEdges; i++)
    {
      if (i <= noCoreCells)
        Solver.r(i) = Solver.r(i - 1) + drCore;
      else
        Solver.r(i) = Solver.r(i - 1) + drMantle;

      Solver.v(i) = 0.;
    }

  ///
  /// set the shell centered values
  ///
  for (size_t i = 0; i < noCells; i++)
    {
      const valueType rI = Solver.r(i);
      const valueType rO = Solver.r(i + 1);
      const valueType rC = 0.5 * (rI + rO);

      const valueType vol = (4. * M_PI / 3.) * (rO * rO * rO - rI * rI * rI);

      if (rC < rCoreGuess)
        {
          Solver.m(i) = vol * rhoCore;
          Solver.mat(i) = 5;
        }
      else
        {
          Solver.m(i) = vol * rhoMantle;
          Solver.mat(i) = 4;
        }

      Solver.u(i) = 1.e10;
    }

  ///
  /// set some constants
  ///
  Solver.uMin = 1.e4;
  Solver.friction = 1.e-2;

  ///
  /// integrate for a certain physical time
  ///
  std::cerr << " start 1D Lagrange solver\n";
  Solver.integrateTo(5.e3); /// replace by option

  valvectType rCen;
  rCen.resize(noCells);

  for (size_t i = 0; i < noCells; i++)
    {
      rCen(i) = 0.5 * (Solver.r(i) + Solver.r(i + 1));
    }

  std::string dumpfilename = "dump.hdf5";
  IOManager.savePrimitive(Solver.r, "r", dumpfilename);
  IOManager.savePrimitive(rCen, "rCen", dumpfilename);
  IOManager.savePrimitive(Solver.v, "v", dumpfilename);
  IOManager.savePrimitive(Solver.m, "m", dumpfilename);
  IOManager.savePrimitive(Solver.u, "u", dumpfilename);
  IOManager.savePrimitive(Solver.p, "p", dumpfilename);
  IOManager.savePrimitive(Solver.mat, "mat", dumpfilename);
  IOManager.savePrimitive(Solver.rho, "rho", dumpfilename);

  matrixRefType pos(PartManager.pos);
  matrixRefType vel(PartManager.vel);
  valvectRefType m(PartManager.m);
  valvectRefType h(PartManager.h);
  valvectRefType p(PartManager.p);
  valvectRefType u(PartManager.u);
  valvectRefType rho(PartManager.rho);

  idvectRefType id(PartManager.id);
  idvectRefType noneigh(PartManager.noneigh);
  idvectRefType mat(PartManager.mat);

  PartManager.useBasicSPH();
  PartManager.useEnergy();
  PartManager.useTimedepEnergy();
  PartManager.useMaterials();

  ///
  /// load particles
  ///
  std::string inputFilename = VMap["input-file"].as<std::string>();
  IOManager.loadDump(inputFilename);
  std::cerr << " read " << inputFilename << "\n";

  ///
  /// exchange particle data
  ///
  CostZone.createDomainPartsIndex();
  CommManager.exchange(CostZone.domainPartsIndex,
                       CostZone.getNoGhosts());
  CommManager.sendGhostsPrepare(CostZone.createDomainGhostIndex());

  ///
  /// scale position, specific volume and smoothing length
  ///
  const valueType rScale = Solver.r(noCells);
  valueType rCore = 0.;
  for (size_t i = 1; i < noCells; i++)
  {
    if ( Solver.mat(i-1) != Solver.mat(i) )
      rCore = Solver.r(i);
  }
  const valueType vScale = pow(rScale, 3.);

  const valueType rhoSet = 3.;

  const size_t noParts = PartManager.getNoLocalParts();
  for (size_t k = 0; k < noParts; k++)
    {
      pos(k, X) *= rScale;
      pos(k, Y) *= rScale;
      pos(k, Z) *= rScale;

      h(k) *= rScale;
      m(k) *= vScale;
    }


  ///
  /// set mass by looking up the density and multiplying
  /// it with the particle volume stored in the mass variable
  ///
  lut_type rhoLUT(rCen, Solver.rho);
  lut_type uLUT(rCen, Solver.u);

  for (size_t k = 0; k < noParts; k++)
    {
      const valueType curRad = sqrt(pos(k, X) * pos(k, X) +
                                    pos(k, Y) * pos(k, Y) +
                                    pos(k, Z) * pos(k, Z));

      m(k) *= rhoLUT(curRad);
      u(k) = uLUT(curRad);

      if (curRad < rCore)
        mat(k) = matCore;
      else
        mat(k) = matMantle;
    }

  std::cerr << " initial guess for particle masses, now calc. SPH density ...\n";

  densPress();

  ///
  /// correct, check, do again
  ///

  sphlatch::quantsType saveQuants;
  saveQuants.vects += &pos;
  saveQuants.scalars += &m, &rho, &h, &u, &p;
  saveQuants.ints += &id, &noneigh, &mat;

  PartManager.step = 0;

  std::string outputFilename = VMap["output-file"].as<std::string>();
  std::cerr << " -> " << outputFilename << "\n";
  IOManager.saveDump(outputFilename, saveQuants);
  std::cerr << "particles saved ... \n";

  MPI::Finalize();
  return EXIT_SUCCESS;
}

/*

   // some defs

   //#define SPHLATCH_CARTESIAN_XYZ
   //#define SPHLATCH_CARTESIAN_YZX
   //#define SPHLATCH_CARTESIAN_ZXY
 #define SPHLATCH_HILBERT3D

   // uncomment for single-precision calculation
   //#define SPHLATCH_SINGLEPREC

   // enable parallel version
 #define SPHLATCH_PARALLEL

   // enable intensive logging for toptree global summation
   //#define SPHLATCH_TREE_LOGSUMUPMP

   //#define SPHLATCH_RANKSPACESERIALIZE

   // enable checking of bounds for the neighbour lists
   //#define SPHLATCH_CHECKNONEIGHBOURS

   // enable selfgravity?
   //#define SPHLATCH_GRAVITY

   // time dependent energy?
   //#define SPHLATCH_TIMEDEP_ENERGY

   // time dependent smoothing length?
   //#define SPHLATCH_TIMEDEP_SMOOTHING

   // shocktube boundary conditions?
   //#define SPHLATCH_SHOCKTUBE

   // do we need the veolicty divergence?
 #ifdef SPHLATCH_TIMEDEP_ENERGY
 #ifndef SPHLATCH_VELDIV
 #define SPHLATCH_VELDIV
 #endif
 #endif

 #ifdef SPHLATCH_TIMEDEP_SMOOTHING
 #ifndef SPHLATCH_VELDIV
 #define SPHLATCH_VELDIV
 #endif
 #endif

 #include <cstdlib>
 #include <iostream>
 #include <fstream>
 #include <iomanip>
 #include <string>
 #include <cmath>

 #include <boost/program_options/option.hpp>
 #include <boost/program_options/cmdline.hpp>
 #include <boost/program_options/options_description.hpp>
 #include <boost/program_options/variables_map.hpp>
 #include <boost/program_options/parsers.hpp>
 #include <boost/program_options/positional_options.hpp>

 #include <boost/assign/std/vector.hpp>

 #include <boost/mpl/vector_c.hpp>

   namespace po = boost::program_options;

 #include "typedefs.h"
   typedef sphlatch::valueType valueType;
   typedef sphlatch::valueRefType valueRefType;
   typedef sphlatch::valvectType valvectType;
   typedef sphlatch::valvectRefType valvectRefType;

   typedef sphlatch::idvectRefType idvectRefType;
   typedef sphlatch::matrixRefType matrixRefType;
   typedef sphlatch::partsIndexVectType partsIndexVectType;
   typedef sphlatch::quantsType quantsType;

 #include "io_manager.h"
   typedef sphlatch::IOManager io_type;

 #include "particle_manager.h"
   typedef sphlatch::ParticleManager part_type;

 #include "communication_manager.h"
   typedef sphlatch::CommunicationManager comm_type;

 #include "costzone.h"
   typedef sphlatch::CostZone costzone_type;

 #include "log_manager.h"
   typedef sphlatch::LogManager log_type;

   //#include "integrator_verlet.h"
   //typedef sphlatch::VerletMetaIntegrator integrator_type;

 #include "integrator_predcorr.h"
   typedef sphlatch::PredCorrMetaIntegrator integrator_type;

 #ifdef SPHLATCH_TILLOTSON
 #include "eos_tillotson.h"
   typedef sphlatch::Tillotson eos_type;
 #else
 #include "eos_idealgas.h"
   typedef sphlatch::IdealGas eos_type;
 #endif

 #include <boost/progress.hpp>
 #include <vector>

 #include "kernel_cubicspline3d.h"
   typedef sphlatch::CubicSpline3D kernel_type;

 #include "bhtree.h"
   typedef sphlatch::BHtree<sphlatch::Quadrupoles> tree_type;

 #include "rankspace.h"
   typedef sphlatch::Rankspace neighsearch_type;

   using namespace sphlatch::vectindices;
   using namespace boost::assign;

   valueType timeStep()
   {
   part_type& PartManager(part_type::instance());
   comm_type& CommManager(comm_type::instance());
   log_type& Logger(log_type::instance());
   io_type& IOManager(io_type::instance());

   matrixRefType pos(PartManager.pos);
   matrixRefType vel(PartManager.vel);
   matrixRefType acc(PartManager.acc);

   valvectRefType m(PartManager.m);
   valvectRefType h(PartManager.h);
   valvectRefType p(PartManager.p);
   valvectRefType u(PartManager.u);
   valvectRefType cs(PartManager.cs);
   valvectRefType rho(PartManager.rho);
   valvectRefType dudt(PartManager.dudt);
   valvectRefType dhdt(PartManager.dhdt);
   valvectRefType divv(PartManager.divv);

 #ifdef SPHLATCH_GRAVITY
   valvectRefType eps(PartManager.eps);
 #endif

   idvectRefType id(PartManager.id);
   idvectRefType noneigh(PartManager.noneigh);
 #ifdef SPHLATCH_TILLOTSON
   idvectRefType mat(PartManager.mat);
 #endif

   valueRefType time(PartManager.attributes["time"]);
   int& step(PartManager.step);
   //int& substep(PartManager.substep);
   const size_t noParts = PartManager.getNoLocalParts();
   //const size_t myDomain = CommManager.getMyDomain();

   ///
   /// timestep criterion for acceleration
   /// ( see Wetzstein et. al 2008 )
   ///
   valueType dtA = std::numeric_limits<valueType>::max();
   for (size_t i = 0; i < noParts; i++)
    {
      const valueType ai = sqrt(acc(i, X) * acc(i, X) +
                                acc(i, Y) * acc(i, Y) +
                                acc(i, Z) * acc(i, Z));

      if (ai > 0.)
        {
          const valueType dtAi = sqrt(h(i) / ai);
          dtA = dtAi < dtA ? dtAi : dtA;
        }
    }
   dtA *= 0.5;
   CommManager.min(dtA);

 #ifdef SPHLATCH_TIMEDEP_ENERGY
   ///
   /// limit oooling speed in integration time
   /// energy integration
   ///
   valueType dtU = std::numeric_limits<valueType>::max();
   for (size_t i = 0; i < noParts; i++)
    {
      if (dudt(i) < 0.)
        {
          const valueType dtUi = -u(i) / dudt(i);
          dtU = dtUi < dtU ? dtUi : dtU;
        }
    }
   CommManager.min(dtU);
 #endif

 #ifdef SPHLATCH_TIMEDEP_SMOOTHING
   ///
   /// timestep criterion for smoothing length integration
   /// ( see Wetzstein et. al 2008 )
   ///
   valueType dtH = std::numeric_limits<valueType>::max();
   for (size_t i = 0; i < noParts; i++)
    {
      const valueType absdtHi = fabs(h(i) / dhdt(i));

      if (absdtHi > 0.)
        {
          dtH = absdtHi < dtH ? absdtHi : dtH;
        }
    }
   dtH *= 0.15;
   CommManager.min(dtH);
 #endif

   ///
   /// CFL condition
   ///
   valueType dtCFL = std::numeric_limits<valueType>::max();
   const valueType courantNumber = PartManager.attributes["courant"];
   for (size_t i = 0; i < noParts; i++)
    {
      const valueType dtCFLi = h(i) / cs(i);
      dtCFL = dtCFLi < dtCFL ? dtCFLi : dtCFL;
      if (cs(i) > 0.)
        {
          dtCFL = dtCFLi < dtCFL ? dtCFLi : dtCFL;
        }
    }
   dtCFL *= courantNumber;
   CommManager.min(dtCFL);

   ///
   /// distance to next saveItrvl
   ///
   //const valueType saveItrvl = 0.1;
   const valueType saveItrvl = 5.0;
   std::string fileName = "saveDump000000.h5part";
   const valueType nextSaveTime = (floor((time / saveItrvl) + 1.e-6)
 + 1.) * saveItrvl;
   valueType dtSave = nextSaveTime - time;

   ///
   /// determine global minimum.
   /// by parallelly minimizing the timesteps, we
   /// can estimate which ones are dominant
   ///
   valueType dtGlob = std::numeric_limits<valueType>::max();

   dtGlob = dtA < dtGlob ? dtA : dtGlob;
   dtGlob = dtCFL < dtGlob ? dtCFL : dtGlob;
 #ifdef SPHLATCH_TIMEDEP_ENERGY
   dtGlob = dtU < dtGlob ? dtU : dtGlob;
 #endif
 #ifdef SPHLATCH_TIMEDEP_SMOOTHING
   dtGlob = dtH < dtGlob ? dtH : dtGlob;
 #endif
   dtGlob = dtSave < dtGlob ? dtSave : dtGlob;

   Logger.stream << "dtA: " << dtA
 #ifdef SPHLATCH_TIMEDEP_ENERGY
                << " dtU: " << dtU
 #endif
 #ifdef SPHLATCH_TIMEDEP_SMOOTHING
                << " dtH: " << dtH
 #endif
                << " dtCFL: " << dtCFL
                << " dtSave: " << dtSave
                << "   dtGlob: " << dtGlob;
   Logger.flushStream();

   ///
   /// define the quantities to save in a dump
   ///
   quantsType saveQuants;
   saveQuants.vects += &pos, &vel, &acc;
   saveQuants.scalars += &m, &rho, &u, &p, &h, &cs;
   saveQuants.ints += &id, &noneigh;

 #ifdef SPHLATCH_TIMEDEP_ENERGY
   saveQuants.scalars += &dudt;
 #endif
 #ifdef SPHLATCH_TIMEDEP_SMOOTHING
   saveQuants.scalars += &dhdt;
   saveQuants.scalars += &divv;
 #endif
 #ifdef SPHLATCH_GRAVITY
   saveQuants.scalars += &eps;
 #endif
 #ifdef SPHLATCH_TILLOTSON
   saveQuants.ints += &mat;
 #endif

   const valueType curSaveTime = (floor((time / saveItrvl) + 1.e-9))
 * saveItrvl;
   if (fabs(curSaveTime - time) < 1.e-9)
    {
      Logger << "save dump";

      std::string stepString = boost::lexical_cast<std::string>(step);

      fileName.replace(fileName.size() - 7 - stepString.size(),
                       stepString.size(), stepString);

      IOManager.saveDump(fileName, saveQuants);
    }

   return dtGlob;
   }

   void derivate()
   {
   part_type& PartManager(part_type::instance());
   comm_type& CommManager(comm_type::instance());
   //io_type& IOManager(io_type::instance());
   costzone_type& CostZone(costzone_type::instance());
   log_type& Logger(log_type::instance());
   eos_type& EOS(eos_type::instance());

   matrixRefType pos(PartManager.pos);
   matrixRefType vel(PartManager.vel);
   matrixRefType acc(PartManager.acc);

   valvectRefType m(PartManager.m);
   valvectRefType h(PartManager.h);
   valvectRefType p(PartManager.p);
   valvectRefType u(PartManager.u);
   valvectRefType cs(PartManager.cs);
   valvectRefType rho(PartManager.rho);
   valvectRefType dudt(PartManager.dudt);
   valvectRefType dhdt(PartManager.dhdt);
   valvectRefType divv(PartManager.divv);

 #ifdef SPHLATCH_GRAVITY
   valvectRefType eps(PartManager.eps);
 #endif

   idvectRefType id(PartManager.id);
   idvectRefType noneigh(PartManager.noneigh);

   ///
   /// exchange particle data
   ///
   CostZone.createDomainPartsIndex();
   Logger << "new domain decomposition";
   CommManager.exchange(CostZone.domainPartsIndex,
                       CostZone.getNoGhosts());

   ///
   /// prepare ghost sends
   ///
   CommManager.sendGhostsPrepare(CostZone.createDomainGhostIndex());
   Logger.stream << "distributed particles: "
                << PartManager.getNoLocalParts() << " parts. & "
                << PartManager.getNoGhostParts() << " ghosts";
   Logger.flushStream();

   const size_t noParts = PartManager.getNoLocalParts();
   const size_t noTotParts = noParts + PartManager.getNoGhostParts();
   int& step(PartManager.step);
   int& substep(PartManager.substep);
   //const size_t myDomain = CommManager.getMyDomain();

   /// send ghosts to other domains
   CommManager.sendGhosts(pos);
   CommManager.sendGhosts(vel);
   CommManager.sendGhosts(id);
   CommManager.sendGhosts(m);
   CommManager.sendGhosts(h);
 #ifdef SPHLATCH_GRAVITY
   CommManager.sendGhosts(eps); // << eps is not yet used for interacting partners!
 #endif
   Logger << " sent to ghosts: pos, vel, id, m, h, eps";

   ///
   /// zero the derivatives
   ///
   for (size_t i = 0; i < noParts; i++)
    {
      acc(i, X) = 0.;
      acc(i, Y) = 0.;
      acc(i, Z) = 0.;
 #ifdef SPHLATCH_TIMEDEP_ENERGY
      dudt(i) = 0.;
 #endif
 #ifdef SPHLATCH_SHOCKTUBE
      ///
      /// shocktube boundary condition
      ///
      vel(i, X) = 0.;
      vel(i, Y) = 0.;

      if (pos(i, Z) < 5. || pos(i, Z) > 295.)
        vel(i, Z) = 0;
 #endif
    }

 #ifdef SPHLATCH_GRAVITY
   const valueType gravTheta = PartManager.attributes["gravtheta"];
   const valueType gravConst = PartManager.attributes["gravconst"];

   tree_type Tree(gravTheta, gravConst,
                 CostZone.getDepth(), CostZone.getCenter(),
                 CostZone.getSidelength());

   ///
   /// fill up tree, determine ordering, calculate multipoles
   ///
   for (size_t k = 0; k < noTotParts; k++)
    {
      Tree.insertParticle(k);
    }

   Tree.detParticleOrder();
   Tree.calcMultipoles();
   Logger << "Tree ready";

   for (size_t k = 0; k < noParts; k++)
    {
      const size_t i = Tree.particleOrder[k];
      Tree.calcGravity(i);
    }
   Logger << "gravity calculated";
 #endif

   ///
   /// define kernel and neighbour search algorithm
   ///
   kernel_type Kernel;
   neighsearch_type Nsearch;

   ///
   /// the size of the neighbour list doesn't really
   /// affect the performance of the neighbour search,
   /// so it can be choosen quite large
   ///
   Nsearch.prepare();
   Nsearch.neighbourList.resize(1024);
   Nsearch.neighDistList.resize(1024);
   Logger << "Rankspace prepared";

   ///
   /// 1st SPH sum: density
   /// I need    : pos, h, m
   /// I provide : rho
   ///
   for (size_t k = 0; k < noParts; k++)
    {
      ///
      /// find neighbours
      ///
 #ifdef SPHLATCH_GRAVITY
      const size_t i = Tree.particleOrder[k];
 #else
      const size_t i = k;
 #endif
      const valueType hi = h(i);
      const valueType srchRad = 2. * hi;
      Nsearch.findNeighbours(i, srchRad);

      const size_t noNeighs = Nsearch.neighbourList[0];

      ///
      /// store the number of neighbours
      ///
      noneigh(i) = noNeighs;

      static valueType rhoi;
      rhoi = 0.;

      ///
      /// sum over neighbours
      ///
      for (size_t curNeigh = 1; curNeigh <= noNeighs; curNeigh++)
        {
          const valueType r = Nsearch.neighDistList[curNeigh];
          const size_t j = Nsearch.neighbourList[curNeigh];

          const valueType hij = 0.5 * (hi + h(j));

          rhoi += m(j) * Kernel.value(r, hij);
        }
      rho(i) = rhoi;
    }
   Logger << "SPH sum: rho";
   CommManager.sendGhosts(rho);
   Logger << " sent to ghosts: rho";

 #ifdef SPHLATCH_TIMEDEP_ENERGY
   ///
   /// lower temperature bound
   ///

   const valueType uMin = PartManager.attributes["umin"];
   for (size_t i = 0; i < noParts; i++)
    {
      if (u(i) < uMin)
        {
          u(i) = uMin;
        }
    }
   Logger.stream << "assure minimal temperature (umin = " << uMin << ")";
   Logger.flushStream();
 #endif

   ///
   /// pressure
   /// I need    : u, rho
   /// I provide : p, cs
   ///
   for (size_t k = 0; k < noParts; k++)
    {
      EOS(k, p(k), cs(k));
    }
   Logger << "calculated pressure";
   CommManager.sendGhosts(p);
   CommManager.sendGhosts(cs);
   Logger << " sent to ghosts: p, cs";

   ///
   /// 2st SPH sum: acceleration, specific power & velocity divergence
   /// I need    : pos, vel, h, m, rho, u
   /// I provide : acc, dudt, divv
   ///
   const valueType alpha = 1;
   const valueType beta = 2;

   valueType curAccX = 0., curAccY = 0., curAccZ = 0.;
 #ifdef SPHLATCH_TIMEDEP_ENERGY
   valueType curPow = 0.;
 #endif
 #ifdef SPHLATCH_VELDIV
   valueType curVelDiv = 0.;
   valueType divvMax = std::numeric_limits<valueType>::min();
 #endif
   for (size_t k = 0; k < noParts; k++)
    {
 #ifdef SPHLATCH_GRAVITY
      const size_t i = Tree.particleOrder[k];
 #else
      const size_t i = k;
 #endif
      const valueType hi = h(i);
      const valueType rhoi = rho(i);
      const valueType pi = p(i);

      const valueType piOrhoirhoi = pi / (rhoi * rhoi);

      /// find the neighbours
      const valueType srchRad = 2. * hi;
      Nsearch.findNeighbours(i, srchRad);

      const size_t noNeighs = Nsearch.neighbourList[0];

      curAccX = 0.;
      curAccY = 0.;
      curAccZ = 0.;
 #ifdef SPHLATCH_TIMEDEP_ENERGY
      curPow = 0.;
 #endif
 #ifdef SPHLATCH_VELDIV
      curVelDiv = 0.;
 #endif
      const valueType viX = vel(i, X);
      const valueType viY = vel(i, Y);
      const valueType viZ = vel(i, Z);

      const valueType riX = pos(i, X);
      const valueType riY = pos(i, Y);
      const valueType riZ = pos(i, Z);

      const valueType ci = cs(i);

      ///
      /// sum over the neighbours
      ///
      for (size_t curNeigh = 1; curNeigh <= noNeighs; curNeigh++)
        {
          const valueType rij = Nsearch.neighDistList[curNeigh];
          const size_t j = Nsearch.neighbourList[curNeigh];

          const valueType rhoj = rho(j);
          const valueType pj = p(j);

          const valueType hij = 0.5 * (hi + h(j));

          const valueType rijX = riX - pos(j, X);
          const valueType rijY = riY - pos(j, Y);
          const valueType rijZ = riZ - pos(j, Z);

          const valueType vijX = viX - vel(j, X);
          const valueType vijY = viY - vel(j, Y);
          const valueType vijZ = viZ - vel(j, Z);

          const valueType vijrij = rijX * vijX + rijY * vijY + rijZ * vijZ;

          /// make that a static or define outside of loop?
          valueType av = 0;

          /// AV
          if (vijrij < 0.)
            {
              const valueType rijrij = rijX * rijX + rijY * rijY + rijZ * rijZ;
              const valueType rhoij = 0.5 * (rhoi + rhoj);
              const valueType cij = 0.5 * (ci + cs(j));
              const valueType muij = hij * vijrij / (rijrij + 0.01 * hij * hij);

              av = (-alpha * cij * muij + beta * muij * muij) / rhoij;
            }

          const valueType accTerm = piOrhoirhoi + (pj / (rhoj * rhoj)) + av;
          const valueType mj = m(j);

          /// acceleration
          Kernel.derive(rij, hij, rijX, rijY, rijZ);

          curAccX -= mj * accTerm * Kernel.derivX;
          curAccY -= mj * accTerm * Kernel.derivY;
          curAccZ -= mj * accTerm * Kernel.derivZ;

 #ifdef SPHLATCH_VELDIV
          ///
          /// m_j * v_ij * divW_ij
          ///
          const valueType mjvijdivWij = mj * (vijX * Kernel.derivX +
                                              vijY * Kernel.derivY +
                                              vijZ * Kernel.derivZ);
 #endif

 #ifdef SPHLATCH_TIMEDEP_ENERGY
          ///
          /// pdV + AV heating
          ///
          curPow += (0.5 * accTerm * mjvijdivWij);
 #endif
 #ifdef SPHLATCH_VELDIV
          ///
          /// velocity divergence
          ///
          curVelDiv += mjvijdivWij;
 #endif
        }
      acc(i, X) += curAccX;
      acc(i, Y) += curAccY;
      acc(i, Z) += curAccZ;
 #ifdef SPHLATCH_TIMEDEP_ENERGY
      dudt(i) = curPow;
 #endif
 #ifdef SPHLATCH_VELDIV
      divv(i) = curVelDiv / rho(i);
      divvMax = divv(i) > divvMax ? divv(i) : divvMax;
 #endif
 #ifdef SPHLATCH_SHOCKTUBE
      ///
      /// shocktube boundary condition
      ///
      if (pos(i, Z) < 5. || pos(i, Z) > 295.)
        acc(i, Z) = 0.;
 #endif
    }
 #ifdef SPHLATCH_VELDIV
   CommManager.max(divvMax);
 #endif

   Logger << "SPH sum: acc, pow, divv";

 #ifdef SPHLATCH_TIMEDEP_SMOOTHING
   ///
   /// time derivative of smoothing length, limit smoothing
   /// length if necessary
   /// I need    : divv, noneigh
   /// I provide : h, dhdt
   ///
   const valueType noNeighOpt = PartManager.attributes["noneigh"];
   const valueType noNeighMin = (2. / 3.) * noNeighOpt;
   const valueType noNeighMax = (5. / 3.) * noNeighOpt;
   const valueType cDivvMax = divvMax;
 #endif

   const valueType czAtomicLength = CostZone.getAtomicLength();
   for (size_t i = 0; i < noParts; i++)
    {
 #ifdef SPHLATCH_TIMEDEP_SMOOTHING
      const valueType noNeighCur = static_cast<valueType>(noneigh(i));

      const valueType A = exp((noNeighCur - noNeighMin) / 5.);
      const valueType B = exp((noNeighCur - noNeighMax) / 5.);

      const valueType k1 = 1. / (A * (A + (1. / A)));
      const valueType k2 = (A / (A + (1. / A)))
 + 1. / (B * (B + (1. / B))) - 1.;
      const valueType k3 = B / (B + (1. / B));

      dhdt(i) = (k1 * cDivvMax - k3 * cDivvMax
                 - k2 * static_cast<valueType>(1. / 3.) * divv(i)) * h(i);
 #endif
      ///
      /// hard upper limit
      ///
      if (2.5 * h(i) > czAtomicLength)
        {
 #ifdef SPHLATCH_TIMEDEP_SMOOTHING
          if (dhdt(i) > 0.)
            dhdt(i) = 0.;
 #endif
          h(i) = czAtomicLength / 2.5;
        }
    }
   Logger.stream << "adapted smoothing length (2.5h < " << czAtomicLength
 #ifdef SPHLATCH_TIMEDEP_SMOOTHING
                << ", divvmax " << cDivvMax
 #endif
                << ")";
   Logger.flushStream();
   };


   int main(int argc, char* argv[])
   {
   MPI::Init(argc, argv);

   ///
   /// instantate managers
   ///
   io_type& IOManager(io_type::instance());
   part_type& PartManager(part_type::instance());
   comm_type& CommManager(comm_type::instance());
   costzone_type& CostZone(costzone_type::instance());
   log_type& Logger(log_type::instance());

   ///
   /// some simulation parameters
   /// attributes will be overwritten, when defined in file
   ///
   PartManager.attributes["gamma"] = 5. / 3.;
   PartManager.attributes["gravconst"] = 1.0;
   PartManager.attributes["gravtheta"] = 0.7;
   PartManager.attributes["courant"] = 0.3;

   PartManager.attributes["noneigh"] = 50.;
   PartManager.attributes["umin"] = 1000.;

   std::string loadDumpFile = "initial.h5part";
   //const valueType maxTime = 5.;
   const valueType maxTime = 100.;

   ///
   /// define what we're doing
   ///
   PartManager.useGravity();
   PartManager.useBasicSPH();
   PartManager.useAVMonaghan();
   PartManager.useEnergy();

 #ifdef SPHLATCH_TIMEDEP_ENERGY
   PartManager.useTimedepEnergy();
 #endif
 #ifdef SPHLATCH_TIMEDEP_SMOOTHING
   PartManager.useTimedepH();
 #endif
 #ifdef SPHLATCH_TILLOTSON
   PartManager.useMaterials();
 #endif

   ///
   /// some useful references
   ///
   matrixRefType pos(PartManager.pos);
   matrixRefType vel(PartManager.vel);
   matrixRefType acc(PartManager.acc);

   valvectRefType m(PartManager.m);
   valvectRefType u(PartManager.u);
 #ifdef SPHLATCH_TIMEDEP_ENERGY
   valvectRefType dudt(PartManager.dudt);
 #endif
   valvectRefType h(PartManager.h);
 #ifdef SPHLATCH_TIMEDEP_SMOOTHING
   valvectRefType dhdt(PartManager.dhdt);
 #endif
 #ifdef SPHLATCH_GRAVITY
   valvectRefType eps(PartManager.eps);
 #endif
 #ifdef SPHLATCH_TILLOTSON
   idvectRefType mat(PartManager.mat);
 #endif

   idvectRefType id(PartManager.id);
   idvectRefType noneigh(PartManager.noneigh);

   int& step(PartManager.step);
   valueRefType time(PartManager.attributes["time"]);

   const size_t myDomain = CommManager.getMyDomain();

   ///
   /// register the quantites to be exchanged
   ///
   CommManager.exchangeQuants.vects += &pos, &vel;
   CommManager.exchangeQuants.scalars += &m, &u, &h;
   CommManager.exchangeQuants.ints += &id;

 #ifdef SPHLATCH_GRAVITY
   CommManager.exchangeQuants.scalars += &eps;
 #endif
 #ifdef SPHLATCH_TILLOTSON
   CommManager.exchangeQuants.ints += &mat;
 #endif

   ///
   /// instantate the MetaIntegrator
   ///
   //sphlatch::VerletMetaIntegrator Integrator(derivate, timestep);
   sphlatch::PredCorrMetaIntegrator Integrator(derivate, timeStep);

   ///
   /// register spatial, energy and smoothing length integration
   ///
   Integrator.regIntegration(pos, vel, acc);
 #ifdef SPHLATCH_TIMEDEP_ENERGY
   Integrator.regIntegration(u, dudt);
 #endif
 #ifdef SPHLATCH_TIMEDEP_SMOOTHING
   Integrator.regIntegration(h, dhdt);
 #endif

   IOManager.loadDump(loadDumpFile);
   Logger.stream << "loaded " << loadDumpFile;
   Logger.flushStream();

   ///
   /// bootstrap the integrator
   ///
   Integrator.bootstrap();
   Logger << "integrator bootstrapped";

   ///
   /// the integration loop
   ///
   //valueType nextSaveTime = time;
   while (time <= maxTime)
    {
      ///
      /// integrate
      ///
      Integrator.integrate();

      Logger.stream << "finished step " << step << ", now at t = " << time;
      Logger.flushStream();
      Logger.zeroRelTime();

      if (myDomain == 0)
        {
          std::cout << "t = " << std::fixed << std::right
                    << std::setw(12) << std::setprecision(6)
                    << time << " (" << step << ")\n";
        }
    }

   MPI::Finalize();
   return EXIT_SUCCESS;
   }

 */
