//==============================================================================
//
//  Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
//  All rights reserved.
//  Confidential and Proprietary - Qualcomm Technologies, Inc.
//
//==============================================================================

#include <vector>
#include <sstream>
#include <cstring>
#include <string>
#include <cstdlib>
#include <sys/stat.h>
#include <cerrno>
#include <limits>

#ifndef _WIN32
#include <cmath>
#else
#define NOMINMAX // std::min
#define NOCRYPT
#define NOGDI
#include <Windows.h>
#endif

#include "Util.hpp"

#include "DlSystem/ITensorFactory.hpp"
#include "DlSystem/TensorShape.hpp"

#ifdef _WIN32
/* Windows Modification
add the definitions to build pass
add enum class: enum class DirMode
add function: static enum class DirMode : uint32_t;
add function: DirMode operator|(DirMode lhs, DirMode rhs);
add function: static bool CreateDir(const std::string& path, DirMode dirmode);
modified function: bool EnsureDirectory(const std::string& dir);
*/

static enum class DirMode : uint32_t {
  S_DEFAULT_ = 0777,
  S_IRWXU_ = 0700,
  S_IRUSR_ = 0400,
  S_IWUSR_ = 0200,
  S_IXUSR_ = 0100,
  S_IRWXG_ = 0070,
  S_IRGRP_ = 0040,
  S_IWGRP_ = 0020,
  S_IXGRP_ = 0010,
  S_IRWXO_ = 0007,
  S_IROTH_ = 0004,
  S_IWOTH_ = 0002,
  S_IXOTH_ = 0001
};
static DirMode operator|(DirMode lhs, DirMode rhs);
static bool CreateDir(const std::string& path, DirMode dirmode);
static DirMode operator|(DirMode lhs, DirMode rhs) {
  return static_cast<DirMode>(static_cast<uint32_t>(lhs) | static_cast<uint32_t>(rhs));
}

static bool CreateDir(const std::string& path, DirMode dirmode) {
  struct stat st;
  // it create a directory successfully or directory exists already, return true.
  if ((stat(path.c_str(), &st) != 0 && (CreateDirectoryA(path.c_str(), NULL) != 0)) ||
    ((st.st_mode & S_IFDIR) != 0)) {
    return true;
  }
  else {
    std::cerr << "Create " << path << " fail! Error code : " << GetLastError() << std::endl;
  }
  return false;
}

bool EnsureDirectory(const std::string& dir)
{
    auto i = dir.find_last_of('/');
    std::string prefix = dir.substr(0, i);
    struct stat st;

    if (dir.empty() || dir == "." || dir == "..") {
        return true;
    }

    if (i != std::string::npos && !EnsureDirectory(prefix)) {
        return false;
    }

    // if existed and is a folder, return true
    // if existed ans is not a folder, no way to do, false
    if (stat(dir.c_str(), &st) == 0) {
        if (st.st_mode & S_IFDIR) {
            return true;
        } else {
            return false;
        }
    }

    // from here, means no file or folder use dir name
    // let's create it as a folder
    if (CreateDir(dir, DirMode::S_IRWXU_ |
                                    DirMode::S_IRGRP_ |
                                    DirMode::S_IXGRP_ |
                                    DirMode::S_IROTH_ |
                                    DirMode::S_IXOTH_ )) {
        return true;
    } else {
        // basically, shouldn't be here, check platform-specific error
        // ex: permission, resource...etc
        return false;
    }
}
#else
bool EnsureDirectory(const std::string& dir)
{
   auto i = dir.find_last_of('/');
   std::string prefix = dir.substr(0, i);

   if (dir.empty() || dir == "." || dir == "..")
   {
      return true;
   }

   if (i != std::string::npos && !EnsureDirectory(prefix))
   {
      return false;
   }

   int rc = mkdir(dir.c_str(),  S_IRWXU | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH);
   if (rc == -1 && errno != EEXIST)
   {
      return false;
   }
   else
   {
      struct stat st;
      if (stat(dir.c_str(), &st) == -1)
      {
         return false;
      }

      return S_ISDIR(st.st_mode);
   }
}
#endif
size_t resizable_dim;

size_t calcSizeFromDims(const zdl::DlSystem::Dimension *dims, size_t rank, size_t elementSize )
{
   if (rank == 0) return 0;
   size_t size = elementSize;
   while (rank--) {
      (*dims == 0) ? size *= resizable_dim : size *= *dims;
      dims++;
   }
   return size;
}

std::vector<float> loadFloatDataFile(const std::string& inputFile)
{
    std::vector<float> vec;
    loadByteDataFile(inputFile, vec);
    return vec;
}

std::vector<unsigned char> loadByteDataFile(const std::string& inputFile)
{
   std::vector<unsigned char> vec;
   loadByteDataFile(inputFile, vec);
   return vec;
}

std::vector<unsigned char> loadByteDataFileBatched(const std::string& inputFile)
{
   std::vector<unsigned char> vec;
   size_t offset=0;
   loadByteDataFileBatched(inputFile, vec, offset);
   return vec;
}

void TfNToFloat(float *out,
                uint8_t *in,
                const unsigned char stepEquivalentTo0,
                const float quantizedStepSize,
                size_t numElement,
                int bitWidth)
{
   for (size_t i = 0; i < numElement; ++i) {
       if (8 == bitWidth) {
           double quantizedValue = static_cast <double> (in[i]);
           double stepEqTo0 = static_cast <double> (stepEquivalentTo0);
           out[i] = static_cast <double> ((quantizedValue - stepEqTo0) * quantizedStepSize);
       }
       else if (16 == bitWidth) {
           uint16_t *temp = (uint16_t *)in;
           double quantizedValue = static_cast <double> (temp[i]);
           double stepEqTo0 = static_cast <double> (stepEquivalentTo0);
           out[i] = static_cast <double> ((quantizedValue - stepEqTo0) * quantizedStepSize);
       }
   }
}

bool FloatToTfN(uint8_t* out,
                unsigned char& stepEquivalentTo0,
                float& quantizedStepSize,
                bool staticQuantization,
                float* in,
                size_t numElement,
                int bitWidth)
{
   double encodingMin;
   double encodingMax;
   double encodingRange;
   double trueBitWidthMax = pow(2, bitWidth) -1;

   if (!staticQuantization) {
      float trueMin = std::numeric_limits <float>::max();
      float trueMax = std::numeric_limits <float>::min();

      for (size_t i = 0; i < numElement; ++i) {
         trueMin = fmin(trueMin, in[i]);
         trueMax = fmax(trueMax, in[i]);
      }

      double stepCloseTo0;

      if (trueMin > 0.0f) {
         stepCloseTo0 = 0.0;
         encodingMin = 0.0;
         encodingMax = trueMax;
      } else if (trueMax < 0.0f) {
         stepCloseTo0 = trueBitWidthMax;
         encodingMin = trueMin;
         encodingMax = 0.0;
      } else {
         double trueStepSize = static_cast <double>(trueMax - trueMin) / trueBitWidthMax;
         stepCloseTo0 = -trueMin / trueStepSize;
         if (stepCloseTo0 == round(stepCloseTo0)) {
            // 0.0 is exactly representable
            encodingMin = trueMin;
            encodingMax = trueMax;
         } else {
            stepCloseTo0 = round(stepCloseTo0);
            encodingMin = (0.0 - stepCloseTo0) * trueStepSize;
            encodingMax = (trueBitWidthMax - stepCloseTo0) * trueStepSize;
         }
      }

      const double minEncodingRange = 0.01;
      encodingRange = encodingMax - encodingMin;
      quantizedStepSize = encodingRange / trueBitWidthMax;
      stepEquivalentTo0 = static_cast <unsigned char> (round(stepCloseTo0));

      if (encodingRange < minEncodingRange) {
         std::cerr << "Expect the encoding range to be larger than " << minEncodingRange << "\n"
                   << "Got: " << encodingRange << "\n";
         return false;
      }
   }
   else
   {
      if (bitWidth == 8) {
         encodingMin = (0 - static_cast <uint8_t> (stepEquivalentTo0)) * quantizedStepSize;
      } else if (bitWidth == 16) {
         encodingMin = (0 - static_cast <uint16_t> (stepEquivalentTo0)) * quantizedStepSize;
      } else {
         std::cerr << "Quantization bitWidth is invalid " << std::endl;
         return false;
      }
      encodingMax = (trueBitWidthMax - stepEquivalentTo0) * quantizedStepSize;
      encodingRange = encodingMax - encodingMin;
   }

   for (size_t i = 0; i < numElement; ++i) {
      int quantizedValue = round(trueBitWidthMax * (in[i] - encodingMin) / encodingRange);

      if (quantizedValue < 0)
         quantizedValue = 0;
      else if (quantizedValue > (int)trueBitWidthMax)
         quantizedValue = (int)trueBitWidthMax;

      if(bitWidth == 8){
         out[i] = static_cast <uint8_t> (quantizedValue);
      }
      else if(bitWidth == 16){
         uint16_t *temp = (uint16_t *)out;
         temp[i] = static_cast <uint16_t> (quantizedValue);
      }
   }
   return true;
}

bool loadByteDataFileBatchedTfN(const std::string& inputFile, std::vector<uint8_t>& loadVector, size_t offset,
                                unsigned char& stepEquivalentTo0, float& quantizedStepSize, bool staticQuantization,
                                int bitWidth, bool useNativeInputFiles)
{
   std::ifstream in(inputFile, std::ifstream::binary);
   std::vector<float> inVector;
   if (!in.is_open() || !in.good())
   {
      std::cerr << "Failed to open input file: " << inputFile << "\n";
   }

   in.seekg(0, in.end);
   size_t length = in.tellg();
   in.seekg(0, in.beg);

   if (useNativeInputFiles) { //No need to convert datatypes if native input given
      if(!in.read( reinterpret_cast<char*> (&loadVector[0]), length)) {
         std::cerr << "Failed to read the contents of: " << inputFile << "\n";
      }
      return true;
   }

   if (loadVector.size() == 0) {
      loadVector.resize(length / sizeof(uint8_t));
   } else if (loadVector.size() < length/sizeof(float)) {
      std::cerr << "Vector is not large enough to hold data of input file: " << inputFile << "\n";
   }

   int elementSize = bitWidth / 8;
   inVector.resize(loadVector.size() / elementSize);
   if (!in.read( reinterpret_cast<char*> (&inVector[0]), length) )
   {
      std::cerr << "Failed to read the contents of: " << inputFile << "\n";
   }
   size_t dataStartPos = (offset * length * elementSize) / sizeof(float);
   if(!FloatToTfN(&loadVector[dataStartPos], stepEquivalentTo0, quantizedStepSize, staticQuantization, inVector.data(), inVector.size(), bitWidth))
   {
     return false;
   }
   return true;
}

bool loadByteDataFileBatchedTf8(const std::string& inputFile, std::vector<uint8_t>& loadVector, size_t offset)
{
   std::ifstream in(inputFile, std::ifstream::binary);
   std::vector<float> inVector;
   if (!in.is_open() || !in.good())
   {
      std::cerr << "Failed to open input file: " << inputFile << "\n";
      return false;
   }

   in.seekg(0, in.end);
   size_t length = in.tellg();
   in.seekg(0, in.beg);

   const size_t vectorNumber = length / sizeof(float);

   if (loadVector.size() == 0) {
      loadVector.resize(vectorNumber);
   } else if (loadVector.size() < vectorNumber) {
      std::cerr << "Vector is not large enough to hold data of input file: " << inputFile << "\n";
      return false;
   }

   inVector.resize((offset+1) * vectorNumber);
   if (!in.read( reinterpret_cast<char*> (&inVector[offset * vectorNumber]), length) )
   {
      std::cerr << "Failed to read the contents of: " << inputFile << "\n";
      return false;
   }

   unsigned char stepEquivalentTo0;
   float quantizedStepSize;
   if(!FloatToTfN(loadVector.data(), stepEquivalentTo0, quantizedStepSize, false, inVector.data(), loadVector.size(), 8))
   {
       return false;
   }
   if (in.is_open()) {
      in.close();
   }
   return true;
}

bool SaveITensorBatched(const std::string& path, const zdl::DlSystem::ITensor* tensor, size_t batchIndex, size_t batchChunk)
{
   if(batchChunk == 0)
      batchChunk = tensor->getSize();
   // Create the directory path if it does not exist
   auto idx = path.find_last_of('/');
   if (idx != std::string::npos)
   {
      std::string dir = path.substr(0, idx);
      if (!EnsureDirectory(dir))
      {
#ifdef _WIN32
         char buf[64] = { 0 };
         const size_t bufSize = sizeof(buf);
         if (strerror_s(buf, bufSize, errno) != 0) {
             buf[bufSize - 1] = '\0';
             std::cerr << "Failed to create output directory: " << dir << ": "
                 << buf << "\n";
         }
#else
         std::cerr << "Failed to create output directory: " << dir << ": "
                   << std::strerror(errno) << "\n";
#endif
         return false;
      }
   }

   std::ofstream os(path, std::ofstream::binary);
   if (!os)
   {
      std::cerr << "Failed to open output file for writing: " << path << "\n";
      return false;
   }

   for ( auto it = tensor->cbegin() + batchIndex * batchChunk; it != tensor->cbegin() + (batchIndex+1) * batchChunk; ++it )
   {
      float f = *it;
      if (!os.write(reinterpret_cast<char*>(&f), sizeof(float)))
      {
         std::cerr << "Failed to write data to: " << path << "\n";
         return false;
      }
   }
   return true;
}

bool SaveUserBufferBatched(const std::string& path, const std::vector<uint8_t>& buffer, size_t batchIndex, size_t batchChunk)
{
   if(batchChunk == 0)
      batchChunk = buffer.size();
   // Create the directory path if it does not exist
   auto idx = path.find_last_of('/');
   if (idx != std::string::npos)
   {
      std::string dir = path.substr(0, idx);
      if (!EnsureDirectory(dir))
      {
#ifdef _WIN32
         char buf[64] = { 0 };
         const size_t bufSize = sizeof(buf);
         if (strerror_s(buf, bufSize, errno) != 0) {
             buf[bufSize - 1] = '\0';
             std::cerr << "Failed to create output directory: " << dir << ": "
                 << buf << "\n";
         }
#else
         std::cerr << "Failed to create output directory: " << dir << ": "
                   << std::strerror(errno) << "\n";
#endif
         return false;
      }
   }

   std::ofstream os(path, std::ofstream::binary);
   if (!os)
   {
      std::cerr << "Failed to open output file for writing: " << path << "\n";
      return false;
   }

   for ( auto it = buffer.begin() + batchIndex * batchChunk; it != buffer.begin() + (batchIndex+1) * batchChunk; ++it )
   {
      uint8_t u = *it;
      if(!os.write((char*)(&u), sizeof(uint8_t)))
      {
        std::cerr << "Failed to write data to: " << path << "\n";
        return false;
      }
   }
   return true;
}

void setResizableDim(size_t resizableDim)
{
    resizable_dim = resizableDim;
}

size_t getResizableDim()
{
    return resizable_dim;
}
