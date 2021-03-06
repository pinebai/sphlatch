#ifndef SPHLATCH_PARTICLE_SET_CPP
#define SPHLATCH_PARTICLE_SET_CPP

#include <sys/stat.h>
#include <boost/lexical_cast.hpp>
#include "particle_set.h"

namespace sphlatch {
template<typename _partT>
ParticleSet<_partT>::ParticleSet()
{
#ifdef SPHLATCH_HDF5
   if (sizeof(fType) == 8)
   {
      h5mFTYPE = H5T_NATIVE_DOUBLE;
      h5fFTYPE = H5T_IEEE_F64LE;
   }
   else
   {
      h5mFTYPE = H5T_NATIVE_FLOAT;
      h5fFTYPE = H5T_IEEE_F32LE;
   }

   h5mITYPE = H5T_NATIVE_INT;
   h5fITYPE = H5T_STD_I32LE;
#endif
}

template<typename _partT>
ParticleSet<_partT>::~ParticleSet()
{ }

#ifdef SPHLATCH_HDF5
template<typename _partT>
std::list<std::string> ParticleSet<_partT>::dsetsInFile;

template<typename _partT>
std::list<std::string> ParticleSet<_partT>::attrsInFile;
#endif

template<typename _partT>
_partT & ParticleSet<_partT>::operator[](const size_t _i)
{
   return(parts[_i]);
}

template<typename _partT>
void ParticleSet<_partT>::operator=(const ParticleSet& _rhs)
{
   step       = _rhs.step;
   attributes = _rhs.attributes;
   parts      = _rhs.parts;
   return(*this);
}

template<typename _partT>
void ParticleSet<_partT>::resize(const size_t _i)
{
   parts.resize(_i);
}

  template<typename _partT>
void ParticleSet<_partT>::reserve(const size_t _i)
{
   parts.reserve(_i);
}

template<typename _partT>
size_t ParticleSet<_partT>::getNop()
{
   return(parts.size());
}

template<typename _partT>
vect3dT ParticleSet<_partT>::getCom()
{
   vect3dT com;

   com = 0., 0., 0.;
   fType m = 0.;

   const size_t nop = parts.size();
   for (size_t i = 0; i < nop; i++)
   {
      com += parts[i].pos * parts[i].m;
      m   += parts[i].m;
   }

   //FIXME: globally sum up
   com /= m;
   return(com);
}

template<typename _partT>
box3dT ParticleSet<_partT>::getBox()
{
   fType xmin = fTypeInf, ymin = fTypeInf, zmin = fTypeInf;
   fType xmax = -fTypeInf, ymax = -fTypeInf, zmax = -fTypeInf;

   const size_t nop = parts.size();
   for (size_t i = 1; i < nop; i++)
   {
      xmin = xmin < parts[i].pos[0] ? xmin : parts[i].pos[0];
      ymin = ymin < parts[i].pos[1] ? ymin : parts[i].pos[1];
      zmin = zmin < parts[i].pos[2] ? zmin : parts[i].pos[2];

      xmax = xmax > parts[i].pos[0] ? xmax : parts[i].pos[0];
      ymax = ymax > parts[i].pos[1] ? ymax : parts[i].pos[1];
      zmax = zmax > parts[i].pos[2] ? zmax : parts[i].pos[2];
   }

   box3dT box;
   box.cen  = 0.5 * (xmax + xmin), 0.5 * (ymax + ymin), 0.5 * (zmax + zmin);
   box.size = std::max(xmax - xmin, std::max(ymax - ymin, zmax - zmin));
   return(box);
}

template<typename _partT>
_partT ParticleSet<_partT>::pop(const size_t _i)
{
  _partT pp;
  const size_t li= parts.size()-1;
  pp = parts[_i];
  parts[_i] = parts[li];
  parts.resize(li);

  return pp;
}

template<typename _partT>
_partT& ParticleSet<_partT>::insert(_partT _p)
{
  const size_t li= parts.size();
  parts.resize(li+1);
  parts[li] = _p;
  return parts[li];
}

template<typename _partT>
std::string ParticleSet<_partT>::getStepName()
{
  std::string stepstr = boost::lexical_cast<std::string>(step);
  std::string stepnam = "/Step#";
  stepnam.append(stepstr);
  return stepnam;
}

#ifdef SPHLATCH_HDF5
template<typename _partT>
void ParticleSet<_partT>::loadHDF5(std::string _filename)
{
   const size_t myDo = 0;
   const size_t noDo = 1;

   struct stat statBuff;

   if (stat(_filename.c_str(), &statBuff) == -1)
   {
      std::cerr << "file " << _filename << " not found!\n";
      return;
   }

   hid_t fh, fpl;

   fpl = H5Pcreate(H5P_FILE_ACCESS);

 #ifdef SPHLATCH_MPI
   H5Pset_fapl_mpio(fpl, MPI::COMM_WORLD, MPI::INFO_NULL);
 #endif
   fh = H5Fopen(_filename.c_str(), H5F_ACC_RDONLY, fpl);
   H5Pclose(fpl);

   hid_t cgp = H5Gopen(fh, "/current", H5P_DEFAULT);

   // look for datasets
   dsetsInFile.clear();
   H5Lvisit_by_name(fh, "/current", H5_INDEX_CRT_ORDER,
                    H5_ITER_NATIVE, getObsCB, NULL, H5P_DEFAULT);

   // load attributes
   attrsInFile.clear();
   hsize_t n = 0;
   H5Aiterate_by_name(cgp, ".", H5_INDEX_CRT_ORDER,
                      H5_ITER_NATIVE, &n, getAttrsCB, NULL, H5P_DEFAULT);


   hid_t accpl = H5Pcreate(H5P_DATASET_XFER);
 #ifdef SPHLATCH_MPI
   H5Pset_dxpl_mpio(accpl, H5FD_MPIO_COLLECTIVE);
 #endif

   bool   nopdet = false;
   size_t notp, nolp = 0, nopck;

   // get the variables we have to load
   ioVarLT loadVars;
   _partT proto;
   loadVars = proto.getLoadVars();

   ioVarT iovDm("_null", 0, 0, IOPart::ITYPE);
   for (strLT::const_iterator dsetItr = dsetsInFile.begin();
        dsetItr != dsetsInFile.end();
        dsetItr++)
   {
      // is the dataset name known?
      const std::string       dsetName = dsetItr->c_str();
      ioVarT                  iovMatch = iovDm;
      ioVarLT::const_iterator varsItr  = loadVars.begin();
      while (varsItr != loadVars.end())
      {
         if (varsItr->name == dsetName)
         {
            iovMatch = *varsItr;
            break;
         }
         varsItr++;
      }
      if (iovMatch == iovDm)
         continue;

      // known, now continue
      hid_t       dset, dtype, mspace, fspace;
      H5T_class_t dtypeClass;

      dset = H5Dopen(cgp, dsetName.c_str(), H5P_DEFAULT);

      fspace = H5Dget_space(dset);
      hsize_t fcounts[2], mcounts[2];

      H5Sget_simple_extent_dims(fspace, fcounts, NULL);

      // is the number of particles already determined?
      if (not nopdet)
      {
         notp  = fcounts[0];
         nopck = lrint(ceil(static_cast<double>(notp) /
                            static_cast<double>(noDo)));
         nolp = std::min(nopck, notp - (nopck * myDo));
         parts.resize(nolp);
         nopdet = true;
      }

      // check type ...
      dtype      = H5Dget_type(dset);
      dtypeClass = H5Tget_class(dtype);
      IOPartT::storetypT mtype;
      hid_t h5mtype;
      switch (dtypeClass)
      {
      case H5T_INTEGER:
         mtype   = IOPart::ITYPE;
         h5mtype = h5mITYPE;
         break;

      case H5T_FLOAT:
         mtype   = IOPart::FTYPE;
         h5mtype = h5mFTYPE;
         break;

      default:
         continue;
      }

      if (mtype != iovMatch.type)
         continue;

      // and 2nd dimension if there is any
      if (H5Sget_simple_extent_ndims(fspace) > 1)
      {
         if (fcounts[1] != iovMatch.width)
            continue;
      }
      else
         fcounts[1] = 1;

      const size_t msize  = H5Tget_size(h5mtype);
      const size_t psize  = sizeof(_partT);
      void         * dptr =
         reinterpret_cast<void*>(reinterpret_cast<char*>(&parts[0]) +
                                 iovMatch.offset);

      hsize_t foffset[2];
      hsize_t moffset[2], mstride[2], mwcounts[2];

      moffset[0] = 0;
      moffset[1] = 0;

      foffset[0] = nopck * myDo;
      foffset[1] = 0;

      mstride[0] = psize / msize;
      mstride[1] = 1;

      mwcounts[0] = nolp * (psize / msize);
      mwcounts[1] = 1;

      mcounts[0] = nolp;
      mcounts[1] = 1;

      const size_t fseccounts = fcounts[1];
      fcounts[1] = 1;

      // let the particle look like one huge 1D array
      mspace = H5Screate_simple(2, mwcounts, NULL);

      for (size_t i = 0; i < fseccounts; i++)
      {
         H5Sselect_hyperslab(fspace, H5S_SELECT_SET,
                             foffset, NULL, fcounts, NULL);
         H5Sselect_hyperslab(mspace, H5S_SELECT_SET,
                             moffset, mstride, mcounts, NULL);
         H5Dread(dset, h5mtype, mspace, fspace, accpl, dptr);
         moffset[0] += 1;
         foffset[1] += 1;
      }
      H5Sclose(mspace);
      H5Dclose(dset);
   }

   H5Pclose(accpl);

   /// so which step /current points to?
   char curptbuff[1024];
   H5Lget_val(fh, "/current", curptbuff, 1024, H5P_DEFAULT);
   std::string        curpt(curptbuff);
   std::istringstream stepstr(curpt.substr(6, 8)); // chars #6-14
   stepstr >> step;

   strLT::const_iterator aItr = attrsInFile.begin();
   while (aItr != attrsInFile.end())
   {
      hid_t attr;
      fType buff;
      attr = H5Aopen(cgp, (*aItr).c_str(), H5P_DEFAULT);
      H5Aread(attr, h5mFTYPE, &buff);
      H5Aclose(attr);
      attributes[*aItr] = buff;
      aItr++;
   }

   H5Gclose(cgp);
   H5Fclose(fh);
}

template<typename _partT>
void ParticleSet<_partT>::saveHDF5(std::string _filename)
{
   const size_t myDo  = 0;
   const size_t noDo  = 1;
   const size_t psize = sizeof(_partT);

   cVType nopG(noDo);
   size_t loff = 0;
   size_t notp = 0, nolp = parts.size();

   nopG[myDo] = nolp;
   // FIXME: sum up nopG

   for (size_t cDo = 0; cDo < noDo; cDo++)
   {
      if (cDo == myDo)
         loff = notp;
      notp += nopG[cDo];
   }

   hid_t fh, fpl;

   fpl = H5Pcreate(H5P_FILE_ACCESS);
 #ifdef SPHLATCH_MPI
   H5Pset_fapl_mpio(fpl, MPI::COMM_WORLD, MPI::INFO_NULL);
 #endif

   ///
   /// if the file already exists, open it. otherwise, create a new one
   /// use the stat() syscall, because the parallel HDF5 produces some
   /// nasty output, when the outputfile doesn't exist
   ///
   if (myDo == 0)
   {
      struct stat statbuff;
      if (stat(_filename.c_str(), &statbuff) == -1)
      {
         hid_t fpll = H5Pcreate(H5P_FILE_ACCESS);
         fh = H5Fcreate(_filename.c_str(), H5F_ACC_TRUNC, H5P_DEFAULT, fpll);
         H5Fclose(fh);
         H5Pclose(fpll);
         sleep(1);
      }
   }
   // FIXME: put a global barrier here

   fh = H5Fopen(_filename.c_str(), H5F_ACC_RDWR, fpl);
   H5Pclose(fpl);
   
   std::string stepnam = getStepName();

   hid_t rg = H5Gopen(fh, "/", H5P_DEFAULT);

   hid_t sg;
   if (objExist(fh, stepnam))
      sg = H5Gopen(fh, stepnam.c_str(), H5P_DEFAULT);
   else
      sg = H5Gcreate(fh, stepnam.c_str(), H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT);

   hid_t accpl = H5Pcreate(H5P_DATASET_XFER);

 #ifdef SPHLATCH_MPI
   // FIXME: make sure ind. is used, when some processes don't have parts
   H5Pset_dxpl_mpio(accpl, H5FD_MPIO_COLLECTIVE);
   //H5Pset_dxpl_mpio(accpl, H5FD_MPIO_INDEPENDENT);
 #endif
   
   ioVarLT saveVars;
   _partT proto;
   saveVars = proto.getSaveVars();

   // write data
   for (ioVarLT::const_iterator vItr = saveVars.begin();
        vItr != saveVars.end();
        vItr++)
   {
      const std::string       dsetnam = vItr->name;
      const size_t            offset  = vItr->offset;
      const size_t            width   = vItr->width;
      const IOPart::storetypT stype   = vItr->type;

      hid_t mtype, ftype;

      switch (stype)
      {
      case IOPart::ITYPE:
         mtype = h5mITYPE;
         ftype = h5fITYPE;
         break;

      case IOPart::FTYPE:
         mtype = h5mFTYPE;
         ftype = h5fFTYPE;
         break;

      default:
         continue;
      }

      const size_t msize  = H5Tget_size(mtype);
      void         * dptr =
         reinterpret_cast<void*>(reinterpret_cast<char*>(&parts[0]) + offset);

      hsize_t fcounts[2];
      fcounts[0] = notp;
      fcounts[1] = width;

      hsize_t foffset[2];
      foffset[0] = loff;
      foffset[1] = 0;

      hsize_t moffset[2];
      moffset[0] = 0;
      moffset[1] = 0;

      hsize_t mstride[2];
      mstride[0] = psize / msize;
      mstride[1] = 1;

      hsize_t mwcounts[2];
      mwcounts[0] = nolp * (psize / msize);
      mwcounts[1] = 1;

      hsize_t mcounts[2];
      mcounts[0] = nolp;
      mcounts[1] = 1;

      // create dataspaces
      hid_t mspace = H5Screate_simple(2, mwcounts, NULL);
      hid_t fspace = H5Screate_simple(2, fcounts, NULL);

      //
      fcounts[1] = 1;

      // create or open dataset
      // FIXME: handle case where dataset exists but with wrong dims
      hid_t dset;
      if (objExist(sg, dsetnam))
         dset = H5Dopen(sg, dsetnam.c_str(), H5P_DEFAULT);
      else
         dset = H5Dcreate(sg, dsetnam.c_str(), ftype,
                          fspace, H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT);


      for (size_t i = 0; i < width; i++)
      {
         H5Sselect_hyperslab(fspace, H5S_SELECT_SET,
                             foffset, NULL, fcounts, NULL);
         H5Sselect_hyperslab(mspace, H5S_SELECT_SET,
                             moffset, mstride, mcounts, NULL);
         H5Dwrite(dset, mtype, mspace, fspace, accpl, dptr);
         moffset[0] += 1;
         foffset[1] += 1;
      }

      H5Dclose(dset);
      H5Sclose(mspace);
      H5Sclose(fspace);
   }
   H5Pclose(accpl);

   attrMT::const_iterator aItr = attributes.begin();
   while (aItr != attributes.end())
   {
      const std::string aname = aItr->first;
      const fType       aval  = aItr->second;

      hid_t   attr;
      hsize_t mcount[2];

      mcount[0] = 1;
      mcount[1] = 1;

      hid_t mspace = H5Screate_simple(1, mcount, NULL);

      if (H5Aexists(sg, aname.c_str()))
         H5Adelete(sg, aname.c_str());

      attr = H5Acreate_by_name(sg, ".", aname.c_str(), h5mFTYPE, mspace,
                               H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT);
      H5Awrite(attr, h5mFTYPE, &aval);
      H5Aclose(attr);
      H5Sclose(mspace);

      aItr++;
   }

   H5Gclose(sg);

   if (H5Lexists(rg, "/current", H5P_DEFAULT))
      H5Ldelete(rg, "/current", H5P_DEFAULT);

   H5Lcreate_soft(stepnam.c_str(), rg, "/current",
                  H5P_DEFAULT, H5P_DEFAULT);

   H5Gclose(rg);
   H5Fclose(fh);
}

template<typename _partT>
void ParticleSet<_partT>::singlePrecOut()
{
  h5fFTYPE = H5T_IEEE_F32LE;
}

template<typename _partT>
void ParticleSet<_partT>::doublePrecOut()
{
  h5fFTYPE = H5T_IEEE_F64LE;
}

template<typename _partT>
herr_t ParticleSet<_partT>::getObsCB(hid_t _fh, const char* _name,
                                     const H5L_info_t* info, void* opData)
{
   H5O_info_t curInfo;

   H5Oget_info_by_name(_fh, _name, &curInfo, H5P_DEFAULT);

   if (curInfo.type == H5O_TYPE_DATASET)
      dsetsInFile.push_back(_name);

   herr_t err = 0;
   return(err);
}

template<typename _partT>
herr_t ParticleSet<_partT>::getAttrsCB(hid_t _loc, const char* _name,
                                       const H5A_info_t* info, void* opData)
{
   attrsInFile.push_back(_name);
   herr_t err = 0;
   return(err);
}

template<typename _partT>
bool ParticleSet<_partT>::objExist(hid_t _fh, std::string _op)
{
   H5Eset_auto(H5E_DEFAULT, NULL, NULL);

   H5O_info_t oi;
   herr_t     stat;

   stat = H5Oget_info_by_name(_fh, _op.c_str(), &oi, H5P_DEFAULT);
   if (stat != 0)
      return(false);
   else
      return(true);
}
#endif

};
#endif

/*
   void IOManager::savePrimitive(matrixRefType _matr,
                              std::string   _name,
                              std::string   _outputFile)
   { }

   void IOManager::savePrimitive(valvectRefType _valvect,
                              std::string    _name,
                              std::string    _outputFile)
   { }

   void IOManager::savePrimitive(idvectRefType _idvect,
                              std::string   _name,
                              std::string   _outputFile)
   { }

   void IOManager::loadPrimitive(valvectRefType _vect,
                              std::string    _name,
                              std::string    _inputFile)
   { }

   fType IOManager::loadAttribute(std::string _name, std::string _inputFile)
   { }

   void IOManager::saveAttribute(fType _attr, std::string _name,
                              std::string _inputFile)
   { }

   IOManager::stringListType
   IOManager::discoverVars(std::string _inputFile,
                        std::string _stepName)
   { }

   IOManager::stringListType
   IOManager::discoverVars(std::string _inputFile)
   { }

   quantsType IOManager::getQuants(std::string _inputFile, std::string _stepName)
   { }

   quantsType IOManager::getQuants(std::string _inputFile)
   { }

   void IOManager::setSinglePrecOut(void)
   { }

   void IOManager::setDoublePrecOut(void)
   { }

   hid_t IOManager::getLocFilehandleRW(std::string _outputFile)
   { }

   hid_t IOManager::getLocFilehandleRO(std::string _inputFile)
   { }*/
