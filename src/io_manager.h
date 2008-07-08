#ifndef SPHLATCH_IOMANAGER_H
#define SPHLATCH_IOMANAGER_H

///
/// we use HDF5 >= 1.8 functions
///
#define H5_NO_DEPRECATED_SYMBOLS

#include <boost/lexical_cast.hpp>

#include "typedefs.h"
#include "particle_manager.h"

#include <sys/stat.h>
#include <hdf5.h>

#ifdef SPHLATCH_PARALLEL
#include "communication_manager.h"
#endif

namespace sphlatch
{
///
/// \brief I/O Manager for reading and writing particle dumps with HDF5
///
/// \author Andreas Reufer andreas.reufer@space.unibe.ch
///

class IOManager
{
public:

typedef IOManager self_type;
typedef IOManager& self_reference;
typedef IOManager* self_pointer;

typedef std::set< valvectPtrType > valVectSet;
typedef std::set< matrixPtrType >   matrixSet;
typedef std::set< idvectPtrType >   idVectSet;

typedef std::list< std::string > stringListType;

typedef sphlatch::ParticleManager PartManagerType;
#ifdef SPHLATCH_PARALLEL
typedef sphlatch::CommunicationManager CommManagerType;
#endif


static self_reference instance();

void loadDump(std::string _inputFile);
void saveDump(std::string _outputFile,
              std::set<matrixPtrType>  _vectors,
              std::set<valvectPtrType> _scalars,
              std::set<idvectPtrType>  _integers);

void saveDump(std::string _outputFile, quantsRefType _quantities);


void setSinglePrecOut(void);
void setDoublePrecOut(void);

PartManagerType& PartManager;
#ifdef SPHLATCH_PARALLEL
CommManagerType& CommManager;
#endif

protected:
IOManager(void);
~IOManager(void);

private:
static self_pointer _instance;

hid_t H5floatFileType, H5floatMemType, H5idMemType, H5idFileType;

bool objectExist(hid_t &_fileHandle, std::string &_objPath);

static herr_t getObsCallback(hid_t locId, const char *name,
                             const H5L_info_t *info, void *opData);
static herr_t getAttrsCallback(hid_t _loc, const char *_name,
                               const H5A_info_t *info, void *opData);

static stringListType datasetsInFile, attributesInFile;
};

IOManager::self_pointer IOManager::_instance = NULL;

IOManager::self_reference IOManager::instance(void)
{
  if (_instance != NULL)
    return *_instance;
  else
    {
      _instance = new IOManager;
      return *_instance;
    }
}

IOManager::stringListType IOManager::datasetsInFile;
IOManager::stringListType IOManager::attributesInFile;

IOManager::IOManager(void) :
  PartManager(PartManagerType::instance())
#ifdef SPHLATCH_PARALLEL
  , CommManager(CommManagerType::instance())
#endif
{
  setSinglePrecOut();

#ifdef SPHLATCH_SINGLEPREC
  H5floatMemType = H5T_NATIVE_FLOAT;
#else
  H5floatMemType = H5T_NATIVE_DOUBLE;
#endif

  /// use the HDF5 type corresponding to the definition
  /// if identType in typedefs.h:
  /// int       <-> H5T_NATIVE_INT
  /// long      <-> H5T_NATIVE_LONG
  /// long long <-> H5T_NATIVE_LLONG
  H5idMemType = H5T_NATIVE_INT;

  /// 32bit should be wide enough
  H5idFileType = H5T_STD_I32LE;
}

IOManager::~IOManager(void)
{
}

void IOManager::loadDump(std::string _inputFile)
{
#ifdef SPHLATCH_PARALLEL
  const size_t myDomain = CommManager.getMyDomain();
  const size_t noDomains = CommManager.getNoDomains();
#else
  const size_t myDomain = 0;
  const size_t noDomains = 1;
#endif

  hid_t fileHandle, filePropList;

  ///
  /// get a new HDF5 file handle
  ///
  filePropList = H5Pcreate(H5P_FILE_ACCESS);
#ifdef SPHLATCH_PARALLEL
  H5Pset_fapl_mpio(filePropList, MPI::COMM_WORLD, MPI::INFO_NULL);
#endif
  fileHandle = H5Fopen(_inputFile.c_str(), H5F_ACC_RDONLY, filePropList);
  H5Pclose(filePropList);

  hid_t curGroup = H5Gopen(fileHandle, "/current", H5P_DEFAULT);

  datasetsInFile.clear();
  H5Lvisit_by_name(fileHandle, "/current", H5_INDEX_CRT_ORDER,
                   H5_ITER_NATIVE, getObsCallback, NULL, H5P_DEFAULT);

  attributesInFile.clear();

  hsize_t n = 0;
  H5Aiterate_by_name(curGroup, ".", H5_INDEX_CRT_ORDER,
                     H5_ITER_NATIVE, &n, getAttrsCallback, NULL, H5P_DEFAULT);

  size_t noTotParts;
  bool noPartsDet = false;
  hsize_t dimsMem[2], dimsFile[2], offset[2];

  /// make whiny compiler happy
  offset[0] = 0;
  offset[1] = 0;

  hid_t datasetPropList = H5Pcreate(H5P_DATASET_XFER);
#ifdef SPHLATCH_PARALLEL
  H5Pset_dxpl_mpio(datasetPropList, H5FD_MPIO_COLLECTIVE);
#endif

  stringListType::const_iterator listItr = datasetsInFile.begin();
  while (listItr != datasetsInFile.end())
    {
      hid_t curDataset;
      hid_t dataType, memSpace, fileSpace;

      H5T_class_t dataTypeClass;

      curDataset = H5Dopen(curGroup, (*listItr).c_str(), H5P_DEFAULT);

      fileSpace = H5Dget_space(curDataset);
      H5Sget_simple_extent_dims(fileSpace, dimsFile, NULL);

      ///
      /// the first dataset determines the total number of particles
      ///
      if (not noPartsDet)
        {
          noTotParts = dimsFile[0];
          const size_t noPartsChunk
          = lrint(ceil(static_cast<double>(noTotParts) /
                       static_cast<double>(noDomains)));
          const size_t locOffset = noPartsChunk * myDomain;
          const size_t noLocParts =
            std::min(noPartsChunk, noTotParts - locOffset);

          PartManager.setNoParts(noLocParts);
          PartManager.resizeAll();

          dimsMem[0] = noLocParts;
          offset[0] = locOffset;

          noPartsDet = true;
        }
      else
        {
          if (noTotParts != dimsFile[0])
            {
              /// something's fishy
              /// throw(something);
            }
        }

      dataType = H5Dget_type(curDataset);
      dataTypeClass = H5Tget_class(dataType);

      if (dataTypeClass == H5T_INTEGER)
        {
          if (H5Sget_simple_extent_ndims(fileSpace) == 1)
            {
              /// load integer quantity
              idvectPtrType idPtr =
                PartManager.getIdRef((*listItr).c_str());
              if (idPtr != NULL)
                {
                  idvectRefType id(*idPtr);

                  assert(id.size() == dimsMem[0]);
                  dimsMem[1] = 1;
                  memSpace = H5Screate_simple(1, dimsMem, NULL);

                  H5Sselect_hyperslab(fileSpace, H5S_SELECT_SET,
                                      offset, NULL, dimsMem, NULL);

                  H5Dread(curDataset, H5idMemType, memSpace,
                          fileSpace, datasetPropList, &id(0));

                  H5Sclose(memSpace);
                }
              else
                {
                  //std::cout << (*listItr).c_str() << " not known!\n";
                }
            }
        }
      else if (dataTypeClass == H5T_FLOAT)
        {
          if (H5Sget_simple_extent_ndims(fileSpace) > 1)
            {
              /// load vectorial quantity
              matrixPtrType matrPtr =
                PartManager.getVectRef((*listItr).c_str());
              if (matrPtr != NULL)
                {
                  matrixRefType matr(*matrPtr);

                  assert(matr.size1() == dimsMem[0]);
                  dimsMem[1] = matr.size2();
                  memSpace = H5Screate_simple(2, dimsMem, NULL);

                  H5Sselect_hyperslab(fileSpace, H5S_SELECT_SET,
                                      offset, NULL, dimsMem, NULL);

                  H5Dread(curDataset, H5floatMemType, memSpace,
                          fileSpace, datasetPropList, &matr(0, 0));

                  H5Sclose(memSpace);
                }
              else
                {
                  //std::cout << (*listItr).c_str() << " not known!\n";
                }
            }
          else
            {
              /// load scalar quantity
              valvectPtrType vectPtr =
                PartManager.getScalarRef((*listItr).c_str());
              if (vectPtr != NULL)
                {
                  valvectRefType vect(*vectPtr);

                  assert(vect.size() == dimsMem[0]);
                  dimsMem[1] = 1;
                  memSpace = H5Screate_simple(1, dimsMem, NULL);

                  H5Sselect_hyperslab(fileSpace, H5S_SELECT_SET,
                                      offset, NULL, dimsMem, NULL);

                  H5Dread(curDataset, H5floatMemType, memSpace,
                          fileSpace, datasetPropList, &vect(0));

                  H5Sclose(memSpace);
                }
              else
                {
                  //std::cout << (*listItr).c_str() << " not known!\n";
                }
            }
        }

      H5Tclose(dataType);
      H5Sclose(fileSpace);
      H5Dclose(curDataset);

      listItr++;
    }

  ///
  /// read attributes
  ///
  attrMapRefType attributes(PartManager.attributes);

  stringListType::const_iterator attrItr = attributesInFile.begin();
  while (attrItr != attributesInFile.end())
    {
      hid_t curAttr;
      dimsMem[0] = 1;
      valueType attrBuff;

      curAttr = H5Aopen(curGroup, (*attrItr).c_str(), H5P_DEFAULT);
      H5Aread(curAttr, H5floatMemType, &attrBuff);
      H5Aclose(curAttr);

      attributes[ *attrItr ] = attrBuff;

      attrItr++;
    }


  H5Pclose(datasetPropList);
  H5Gclose(curGroup);

  ///
  /// so which step /current points to?
  ///
  char curPointeeBuff[1024];
  H5Lget_val(fileHandle, "/current", curPointeeBuff, 1024, H5P_DEFAULT);

  ///
  /// pass the step number to PartManager
  ///
  std::string curPointee(curPointeeBuff);
  std::istringstream stepString(curPointee.substr(6, 8)); /// chars #6-14
  stepString >> PartManager.step;

  H5Fclose(fileHandle);
}

herr_t IOManager::getObsCallback(hid_t _fileHandle, const char *_name,
                                 const H5L_info_t *info, void *opData)
{
  H5O_info_t curInfo;

  H5Oget_info_by_name(_fileHandle, _name, &curInfo, H5P_DEFAULT);

  if (curInfo.type == H5O_TYPE_DATASET)
    {
      datasetsInFile.push_back(_name);
    }

  herr_t err = 0;
  return err;
}

herr_t IOManager::getAttrsCallback(hid_t _loc, const char *_name,
                                   const H5A_info_t *info, void *opData)
{
  attributesInFile.push_back(_name);
  herr_t err = 0;
  return err;
}

void IOManager::saveDump(std::string _outputFile,
                         matrixSet _vectors,
                         valVectSet _scalars,
                         idVectSet _integers)
{
#ifdef SPHLATCH_PARALLEL
  const size_t myDomain = CommManager.getMyDomain();
  const size_t noDomains = CommManager.getNoDomains();
#else
  const size_t myDomain = 0;
  const size_t noDomains = 1;
#endif

  countsVectType noPartsGlob(noDomains);

  size_t locOffset = 0;
  size_t noTotParts = 0, noLocParts = PartManager.getNoLocalParts();

  noPartsGlob[myDomain] = noLocParts;
#ifdef SPHLATCH_PARALLEL
  CommManager.sumUpCounts(noPartsGlob);
#endif

  for (size_t curDomain = 0; curDomain < noDomains; curDomain++)
    {
      if (myDomain == curDomain)
        {
          locOffset = noTotParts;
        }
      noTotParts += noPartsGlob[curDomain];
    }

  hid_t fileHandle, filePropList;

  ///
  /// get a new HDF5 file handle
  ///
  filePropList = H5Pcreate(H5P_FILE_ACCESS);
#ifdef SPHLATCH_PARALLEL
  H5Pset_fapl_mpio(filePropList, MPI::COMM_WORLD, MPI::INFO_NULL);
#endif
  ///
  /// if the file already exists, open it. otherwise, create a new one
  /// use the stat() syscall, because the parallel HDF5 produces some
  /// nasty output, when the outputfile doesn't exist
  ///
  if (myDomain == 0)
    {
      struct stat statBuff;
      if (stat(_outputFile.c_str(), &statBuff) == -1)
        {
          hid_t filePropLocList = H5Pcreate(H5P_FILE_ACCESS);
          fileHandle = H5Fcreate(_outputFile.c_str(),
                                 H5F_ACC_TRUNC, H5P_DEFAULT, filePropLocList);
          H5Fclose(fileHandle);
          H5Pclose(filePropLocList);
          sleep(1);
        }
    }
#ifdef SPHLATCH_PARALLEL
  CommManager.barrier();
#endif

  fileHandle = H5Fopen(_outputFile.c_str(), H5F_ACC_RDWR, filePropList);
  H5Pclose(filePropList);

  ///
  /// determine group name and check whether the group exists
  ///
  const size_t step = PartManager.step;
  std::string stepString = boost::lexical_cast<std::string>(step);
  std::string stepName = "/Step#";
  stepName.append(stepString);


  ///
  /// start the group business
  ///
  hid_t rootGroup = H5Gopen(fileHandle, "/", H5P_DEFAULT);

  hid_t stepGroup;
  if (objectExist(fileHandle, stepName))
    {
      stepGroup = H5Gopen(fileHandle, stepName.c_str(), H5P_DEFAULT);
    }
  else
    {
      stepGroup = H5Gcreate(fileHandle, stepName.c_str(),
                            H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT);
    }

  hid_t curDataset, filespace, memspace, datasetPropList;
  datasetPropList = H5Pcreate(H5P_DATASET_XFER);

#ifdef SPHLATCH_PARALLEL
  ///
  /// if a domain has nothing to save, the COLLECTIVE IO will fail
  ///
  valueType minLocParts = noLocParts;
  CommManager.min(minLocParts);

  if (minLocParts < 1)
    {
      H5Pset_dxpl_mpio(datasetPropList, H5FD_MPIO_INDEPENDENT);
    }
  else
    {
      H5Pset_dxpl_mpio(datasetPropList, H5FD_MPIO_COLLECTIVE);
    }
#endif

  ///
  /// write data
  ///
  hsize_t dimsMem[2], dimsFile[2], offset[2];

  dimsMem[0] = noLocParts;
  dimsFile[0] = noTotParts;

  offset[0] = locOffset;
  offset[1] = 0;

  idVectSet::const_iterator idItr = _integers.begin();
  while (idItr != _integers.end())
    {
      std::string attrName = PartManager.getName(**idItr);
      idvectRefType idvect(**idItr);

      /// dataset dimensions in memory
      dimsMem[1] = 1;
      memspace = H5Screate_simple(1, dimsMem, NULL);

      /// file dataset dimensions
      dimsFile[1] = 1;
      filespace = H5Screate_simple(1, dimsFile, NULL);

      /// create or open dataset
      if (objectExist(stepGroup, attrName))
        {
          curDataset = H5Dopen(stepGroup, attrName.c_str(), H5P_DEFAULT);
        }
      else
        {
          curDataset = H5Dcreate(stepGroup, attrName.c_str(), H5idFileType,
                                 filespace, H5P_DEFAULT,
                                 H5P_DEFAULT, H5P_DEFAULT);
        }

      /// select hyperslab
      H5Sselect_hyperslab(filespace, H5S_SELECT_SET,
                          offset, NULL, dimsMem, NULL);

      /// write the data
      H5Dwrite(curDataset, H5idMemType, memspace,
               filespace, datasetPropList, &idvect(0));

      H5Dclose(curDataset);
      H5Sclose(memspace);
      H5Sclose(filespace);

      idItr++;
    }


  matrixSet::const_iterator vectItr = _vectors.begin();
  while (vectItr != _vectors.end())
    {
      std::string attrName = PartManager.getName(**vectItr);
      matrixRefType matr(**vectItr);

      /// dataset dimensions in memory
      dimsMem[1] = matr.size2();
      memspace = H5Screate_simple(2, dimsMem, NULL);

      /// file dataset dimensions
      dimsFile[1] = matr.size2();
      filespace = H5Screate_simple(2, dimsFile, NULL);

      /// check if dataset already exists in the right size
      if (objectExist(stepGroup, attrName))
        {
          curDataset = H5Dopen(stepGroup, attrName.c_str(), H5P_DEFAULT);
        }
      else
        {
          curDataset = H5Dcreate(stepGroup, attrName.c_str(), H5floatFileType,
                                 filespace, H5P_DEFAULT,
                                 H5P_DEFAULT, H5P_DEFAULT);
        }

      /// select hyperslab
      H5Sselect_hyperslab(filespace, H5S_SELECT_SET,
                          offset, NULL, dimsMem, NULL);

      /// write the data
      H5Dwrite(curDataset, H5floatMemType, memspace,
               filespace, datasetPropList, &matr(0, 0));

      H5Dclose(curDataset);
      H5Sclose(memspace);
      H5Sclose(filespace);

      vectItr++;
    }

  valVectSet::const_iterator scalarItr = _scalars.begin();
  while (scalarItr != _scalars.end())
    {
      std::string attrName = PartManager.getName(**scalarItr);
      valvectRefType vect(**scalarItr);

      /// dataset dimensions in memory
      dimsMem[1] = 1;
      memspace = H5Screate_simple(1, dimsMem, NULL);

      /// file dataset dimensions
      dimsFile[1] = 1;
      filespace = H5Screate_simple(1, dimsFile, NULL);

      /// check if dataset already exists in the right size
      if (objectExist(stepGroup, attrName))
        {
          curDataset = H5Dopen(stepGroup, attrName.c_str(), H5P_DEFAULT);
        }
      else
        {
          curDataset = H5Dcreate(stepGroup, attrName.c_str(), H5floatFileType,
                                 filespace, H5P_DEFAULT,
                                 H5P_DEFAULT, H5P_DEFAULT);
        }

      /// select hyperslab
      H5Sselect_hyperslab(filespace, H5S_SELECT_SET,
                          offset, NULL, dimsMem, NULL);

      /// write the data
      H5Dwrite(curDataset, H5floatMemType, memspace,
               filespace, datasetPropList, &vect(0));

      H5Dclose(curDataset);
      H5Sclose(memspace);
      H5Sclose(filespace);
      scalarItr++;
    }

  H5Pclose(datasetPropList);

  ///
  /// write attributes
  ///
  attrMapRefType attributes(PartManager.attributes);
  attrMapType::const_iterator attrItr = attributes.begin();
  while (attrItr != attributes.end())
    {
      hid_t curAttr;
      dimsMem[0] = 1;

      memspace = H5Screate_simple(1, dimsMem, NULL);

      curAttr = H5Acreate_by_name(stepGroup, ".", (attrItr->first).c_str(),
                                  H5floatFileType, memspace,
                                  H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT);

      H5Awrite(curAttr, H5floatMemType, &(attrItr->second));
      H5Aclose(curAttr);

      H5Sclose(memspace);

      attrItr++;
    }

  H5Gclose(stepGroup);

  ///
  /// create a link to indicate the current dump
  ///
  if (H5Lexists(rootGroup, "/current", H5P_DEFAULT))
    {
      H5Ldelete(rootGroup, "/current", H5P_DEFAULT);
    }
  H5Lcreate_soft(stepName.c_str(), rootGroup, "/current",
                 H5P_DEFAULT, H5P_DEFAULT);

  H5Gclose(rootGroup);

  H5Fclose(fileHandle);
}

void IOManager::saveDump(std::string _outputFile,
                         quantsRefType _quantities)
{
  saveDump(_outputFile,
           _quantities.vects, _quantities.scalars, _quantities.ints);
}

void IOManager::setSinglePrecOut(void)
{
  H5floatFileType = H5T_IEEE_F32LE;
}

void IOManager::setDoublePrecOut(void)
{
  H5floatFileType = H5T_IEEE_F64LE;
}


bool IOManager::objectExist(hid_t &_fileHandle, std::string &_objPath)
{
  H5Eset_auto(H5E_DEFAULT, NULL, NULL);
  H5O_info_t objInfo;
  herr_t status;

  status = H5Oget_info_by_name(_fileHandle, _objPath.c_str(),
                               &objInfo, H5P_DEFAULT);

  if (status != 0)
    {
      return false;
    }
  else
    {
      return true;
    }
}
};
#endif
