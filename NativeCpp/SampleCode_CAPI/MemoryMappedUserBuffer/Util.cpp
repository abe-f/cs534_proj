//==============================================================================
//
//  Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
//  All rights reserved.
//  Confidential and Proprietary - Qualcomm Technologies, Inc.
//
//==============================================================================

#include <cerrno>
#include <cmath>
#include <cstring>
#include <limits>
#include <string>
#include <sys/stat.h>
#include <vector>
#include <fstream>
#include <iostream>
#include <sstream>
#include <cstdlib>
#include <set>
#include <string>

#ifdef _WIN32
#define NOMINMAX // std::min
#define NOCRYPT
#define NOGDI

#include <Windows.h>
#include <windows.h>
#include <psapi.h>
#include <winevt.h>
#else
#include <sys/mman.h>
#include <dlfcn.h>
#endif

#include "Util.hpp"
#include "DlSystem/TensorShape.h"
#include "SNPE/SNPEBuilder.h"
#define RPCMEM_HEAP_ID_SYSTEM 25
#define RPCMEM_DEFAULT_FLAGS  1
#define DMABUF_HEAP_NAME_SYSTEM "system"
#define DMABUF_DEFAULT_FLAGS  1

size_t resizable_dim;


size_t calcSizeFromDims(const size_t* dims, size_t rank, size_t elementSize )
{
    if (rank == 0) return 0;
    size_t size = elementSize;
    while (rank--) {
        (*dims == 0) ? size *= resizable_dim : size *= *dims;
        dims++;
    }
    return size;
}

Snpe_TensorShape_Handle_t calcStrides(Snpe_TensorShape_Handle_t dimsHandle, size_t elementSize){
    std::vector<size_t> strides(Snpe_TensorShape_Rank(dimsHandle));
    strides[strides.size() - 1] = elementSize;
    size_t stride = strides[strides.size() - 1];
    for (size_t i = Snpe_TensorShape_Rank(dimsHandle) - 1; i > 0; i--)
    {
        if(Snpe_TensorShape_At(dimsHandle, i) != 0)
            stride *= Snpe_TensorShape_At(dimsHandle, i);
        else
            stride *= resizable_dim;
        strides[i-1] = stride;
    }
    Snpe_TensorShape_Handle_t tensorShapeHandle = Snpe_TensorShape_CreateDimsSize(strides.data(), Snpe_TensorShape_Rank(dimsHandle));
    return tensorShapeHandle;
}

Snpe_PerformanceProfile_t extractPerfProfile(std::string perfProfileStr){
    Snpe_PerformanceProfile_t perfProfile = SNPE_PERFORMANCE_PROFILE_BALANCED;

    if (perfProfileStr == "default" || perfProfileStr == "balanced")
    {
        perfProfile = SNPE_PERFORMANCE_PROFILE_BALANCED;
    }
    else if (perfProfileStr == "high_performance")
    {
        perfProfile = SNPE_PERFORMANCE_PROFILE_HIGH_PERFORMANCE;
    }
    else if (perfProfileStr == "power_saver")
    {
        perfProfile = SNPE_PERFORMANCE_PROFILE_POWER_SAVER;
    }
    else if (perfProfileStr == "system_settings")
    {
        perfProfile = SNPE_PERFORMANCE_PROFILE_SYSTEM_SETTINGS;
    }
    else if (perfProfileStr == "sustained_high_performance")
    {
        perfProfile = SNPE_PERFORMANCE_PROFILE_SUSTAINED_HIGH_PERFORMANCE;
    }
    else if (perfProfileStr == "burst")
    {
        perfProfile = SNPE_PERFORMANCE_PROFILE_BURST;
    }
    else if (perfProfileStr == "low_power_saver")
    {
        perfProfile = SNPE_PERFORMANCE_PROFILE_LOW_POWER_SAVER;
    }
    else if (perfProfileStr == "high_power_saver")
    {
        perfProfile = SNPE_PERFORMANCE_PROFILE_HIGH_POWER_SAVER;
    }
    else if (perfProfileStr == "low_balanced")
    {
        perfProfile = SNPE_PERFORMANCE_PROFILE_LOW_BALANCED;
    }
    else if (perfProfileStr == "extreme_power_saver")
    {
        perfProfile = SNPE_PERFORMANCE_PROFILE_EXTREME_POWER_SAVER;
    }
    else
    {
        std::cerr
            << "ERROR: Invalid setting pased to the argument -p.\n "
               "Please check the Arguments section in the description.\n";
        std::exit(EXIT_FAILURE);
    }
    return perfProfile;
}

/* Windows Modification
  add the definitions to build pass
  add function: void* dlopen(const char *filename, int flags);
  add function: void *dlsym(void *handle, const char *symbol);
  windows equivalents of dlopen, dlsym from dlfcn.h
  add enum class: enum class DirMode
  add function: static enum class DirMode : uint32_t;
  add function: DirMode operator|(DirMode lhs, DirMode rhs);
  add function: static bool CreateDir(const std::string& path, DirMode dirmode);
  modified function: bool EnsureDirectory(const std::string& dir);
  */
#ifdef _WIN32
#define DL_DEFAULT (void *)(0x4)
static std::set<HMODULE> sg_modHandles;
static thread_local char *sg_lastErrMsg = "";
enum : unsigned { DL_NOW = 0x0001U, DL_LOCAL = 0x0002U, DL_GLOBAL = 0x0004U, DL_NOLOAD = 0x0008U };
void* dlopen(const char *filename, int flags) {
  HMODULE mod;
  HANDLE cur_proc;
  DWORD as_is, to_be;
  bool loadedBefore = false;

  if (!filename || ::strlen(filename) == 0) {
    // TODO: we don't support empty filename now
    sg_lastErrMsg = "filename is null or empty";
    return NULL;
  }

  // POSIX asks one of symbol resolving approaches:
  // NOW or LAZY must be specified for loading library
  // NOLOAD is optional to be specified for library checking
  if (!(flags & DL_NOW)) {
    // TODO: since Windows does not provide existing API so lazy
    // symbol resolving needs to do relocation by ourself
    // that would be too costly. SNPE didn't use this feature now
    // , wait until we really need it. keep the flexibility here
    // ask caller MUST pass DL_NOW
    sg_lastErrMsg = "flags must either include DL_NOW or only specify DL_NOLOAD";
    return NULL;
  }

  // Only test if the specified library is loaded or not
  // Return its handle if loaded, otherwise, return NULL
  if (flags & DL_NOLOAD) {
    // If the library is loaded, it would be a handle to this library
    // If the library is not loaded, it would be a NULL
    mod = GetModuleHandleA(filename);
    return static_cast<void *>(mod);
  }

  cur_proc = GetCurrentProcess();

  if (EnumProcessModules(cur_proc, NULL, 0, &as_is) == 0) {
    sg_lastErrMsg = "enumerate modules failed before loading module";
    return NULL;
  }

  // search from system lib path first
  mod = LoadLibraryExA(filename, NULL, LOAD_WITH_ALTERED_SEARCH_PATH);
  if (!mod) {
    sg_lastErrMsg = "load library failed";
    return NULL;
  }

  if (EnumProcessModules(cur_proc, NULL, 0, &to_be) == 0) {
    sg_lastErrMsg = "enumerate modules failed after loading module";
    FreeLibrary(mod);
    return NULL;
  }

  if (as_is == to_be) {
    loadedBefore = true;
  }

  // (not loadedBefore) and DL_LOCAL means this lib was not loaded yet
  // add it into the local set
  //
  // If loadedBefore and DL_LOCAL, means this lib was already loaded
  // 2 cases here for how it was loaded before:
  // a. with DL_LOCAL, just ignore since it was already in local set
  // b. with DL_GLOBAL, POSIX asks it in global, ignore it, too
  if ((!loadedBefore) && (flags & DL_LOCAL)) {
    sg_modHandles.insert(mod);
  }

  // once callers ask for global, needs to be in global thereafter
  // so the lib should be removed from local set
  if (flags & DL_GLOBAL) {
    sg_modHandles.erase(mod);
  }

  return static_cast<void *>(mod);
}

void *dlsym(void *handle, const char *symbol) {
  FARPROC sym_addr = NULL;
  HANDLE cur_proc;
  DWORD size, size_needed;
  HMODULE *mod_list;
  HMODULE mod = 0;

  if ((!handle) || (!symbol)) {
    return NULL;
  }

  cur_proc = GetCurrentProcess();

  if (EnumProcessModules(cur_proc, NULL, 0, &size) == 0) {
    sg_lastErrMsg = "enumerate modules failed before memory allocation";
    return NULL;
  }

  mod_list = static_cast<HMODULE *>(malloc(size));
  if (!mod_list) {
    sg_lastErrMsg = "malloc failed";
    return NULL;
  }

  if (EnumProcessModules(cur_proc, mod_list, size, &size_needed) == 0) {
    sg_lastErrMsg = "enumerate modules failed after memory allocation";
    free(mod_list);
    return NULL;
  }

  // DL_DEFAULT needs to bypass those modules with DL_LOCAL flag
  if (handle == DL_DEFAULT) {
    for (size_t i = 0; i < (size / sizeof(HMODULE)); i++) {
      auto iter = sg_modHandles.find(mod_list[i]);
      if (iter != sg_modHandles.end()) {
        continue;
      }
      // once find the first non-local module with symbol
      // return its address here to avoid unnecessary looping
      sym_addr = GetProcAddress(mod_list[i], symbol);
      if (sym_addr) {
        free(mod_list);
        return *(void **)(&sym_addr);
      }
    }
  } else {
    mod = static_cast<HMODULE>(handle);
  }

  free(mod_list);
  sym_addr = GetProcAddress(mod, symbol);
  if (!sym_addr) {
    sg_lastErrMsg = "can't resolve symbol";
    return NULL;
  }

  return *(void **)(&sym_addr);
}

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


bool GetMemFnHandles(bool useRpc, MemFnHandlesType_t& memFnHandles) {
    // use libcdsprpc
    if (useRpc) {
#ifdef _WIN32
        void* libRpcHandle = dlopen("libcdsprpc.dll", DL_NOW|DL_GLOBAL);
#else
        void* libRpcHandle = dlopen("libcdsprpc.so", RTLD_NOW|RTLD_GLOBAL);
#endif
        if(libRpcHandle == nullptr) {
            std::cerr << "Error: could not open libcdsprpc " << std::endl;
            return false;
        }
        memFnHandles.rpcMemAllocFnHandle = resolveSymbol<rpcMemAllocFnHandleType_t>(libRpcHandle, "rpcmem_alloc");
        if(memFnHandles.rpcMemAllocFnHandle == nullptr) {
            std::cerr << "Error: could not access rpcmem_alloc" << std::endl;
            return false;
        }
        memFnHandles.rpcMemFreeFnHandle = resolveSymbol<rpcMemFreeFnHandleType_t>(libRpcHandle, "rpcmem_free");
        if(memFnHandles.rpcMemFreeFnHandle == nullptr) {
            std::cerr << "Error: could not access rpcmem_free" << std::endl;
            return false;
        }
    // use libdmabufheap
    } else {
#ifdef _WIN32
        void* libDmaHandle = dlopen("libdmabufheap.dll", DL_NOW|DL_GLOBAL);
#else
        void* libDmaHandle = dlopen("libdmabufheap.so", RTLD_NOW|RTLD_GLOBAL);
#endif
        if(libDmaHandle == nullptr) {
            std::cerr << "Error: could not open libdmabufheap " << std::endl;
            return false;
        }

        memFnHandles.dmaCreateBufAllocFnHandle = resolveSymbol<dmaCreateBufferAllocatorFnHandleType_t>(libDmaHandle, "CreateDmabufHeapBufferAllocator");
        if(memFnHandles.dmaCreateBufAllocFnHandle == nullptr) {
            std::cerr << "Error: could not access CreateDmabufHeapBufferAllocator" << std::endl;
            return false;
        }

        memFnHandles.dmaFreeBufAllocFnHandle = resolveSymbol<dmaFreeBufferAllocatorFnHandleType_t>(libDmaHandle, "FreeDmabufHeapBufferAllocator");
        if(memFnHandles.dmaFreeBufAllocFnHandle == nullptr) {
            std::cerr << "Error: could not access FreeDmabufHeapBufferAllocator" << std::endl;
            return false;
        }

        memFnHandles.dmaMemAllocFnHandle = resolveSymbol<dmaMemAllocFnHandleType_t>(libDmaHandle, "DmabufHeapAlloc");
        if(memFnHandles.dmaMemAllocFnHandle == nullptr) {
            std::cerr << "Error: could not access DmabufHeapAlloc" << std::endl;
            return false;
        }

        memFnHandles.dmaMapHeapToIonFnHandle = resolveSymbol<dmaMapHeapToIonFnHandleType_t>(libDmaHandle, "MapDmabufHeapNameToIonHeap");
        if(memFnHandles.dmaMapHeapToIonFnHandle == nullptr) {
            std::cerr << "Error: could not access MapDmabufHeapNameToIonHeap" << std::endl;
            return false;
        }
    }

    return true;
}

std::pair<void*, int> GetBufferAddrFd(size_t buffSize, bool useRpc, void* bufferAllocator, MemFnHandlesType_t& memFnHandles) {
    void* addr = nullptr;
    int fd = -1;

    if (useRpc) {
        addr = memFnHandles.rpcMemAllocFnHandle(RPCMEM_HEAP_ID_SYSTEM, RPCMEM_DEFAULT_FLAGS, buffSize);
    } else {
        fd = memFnHandles.dmaMemAllocFnHandle(bufferAllocator, DMABUF_HEAP_NAME_SYSTEM, buffSize, DMABUF_DEFAULT_FLAGS, 0);
        if (fd <= 0) {
            return {nullptr, -1};
        }
#ifndef _WIN32
        addr = mmap(nullptr, buffSize, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
#endif
    }

    return {addr, fd};
}

int MapDmaHeapToIon(void* bufferAllocator, MemFnHandlesType_t& memFnHandles) {
    return memFnHandles.dmaMapHeapToIonFnHandle(bufferAllocator, DMABUF_HEAP_NAME_SYSTEM, "", 0, DMABUF_DEFAULT_FLAGS, 0);
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
                const uint64_t stepEquivalentTo0,
                const float quantizedStepSize,
                size_t numElement,
                int bitWidth)
{
    for (size_t i = 0; i < numElement; ++i) {
        if (bitWidth == 8) {
            double quantizedValue = static_cast <double> (in[i]);
            double stepEqTo0 = static_cast <double> (stepEquivalentTo0);
            out[i] = static_cast <double> ((quantizedValue - stepEqTo0) * quantizedStepSize);
        }
        else if (bitWidth == 16) {
            uint16_t *temp = (uint16_t *)in;
            double quantizedValue = static_cast <double> (temp[i]);
            double stepEqTo0 = static_cast <double> (stepEquivalentTo0);
            out[i] = static_cast <double> ((quantizedValue - stepEqTo0) * quantizedStepSize);
        }
    }
}

bool FloatToTfN(uint8_t* out,
                uint64_t& stepEquivalentTo0,
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
        stepEquivalentTo0 = static_cast <uint64_t> (round(stepCloseTo0));

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

size_t loadFileToBuffer(const std::string& inputFile, std::vector<float>& loadVector)
{
    std::ifstream in(inputFile, std::ifstream::binary);
    if (!in.is_open() || !in.good())
    {
        std::cerr << "Failed to open input file: " << inputFile << "\n";
        return 0U;
    }

    in.seekg(0, in.end);
    size_t length = in.tellg();
    in.seekg(0, in.beg);

    loadVector.resize(length / sizeof(float));
    if (!in.read( reinterpret_cast<char*> (&loadVector[0]), length) )
    {
        std::cerr << "Failed to read the contents of: " << inputFile << "\n";
        return 0U;
    }

    return length;
}

bool loadByteDataFileBatchedTfN(const std::string& inputFile, void* loadPtr, size_t offset, uint64_t& stepEquivalentTo0,
                                float& quantizedStepSize, bool staticQuantization, int bitWidth) {
    std::vector<float> inVector;
    size_t inputFileLength = loadFileToBuffer(inputFile, inVector);

    if(inputFileLength == 0U || !FloatToTfN((uint8_t*)loadPtr + offset, stepEquivalentTo0, quantizedStepSize, staticQuantization, inVector.data(), inputFileLength/sizeof(float), bitWidth))
    {
        return false;
    }
    return true;
}

bool loadByteDataFileBatchedTfN(const std::string& inputFile, std::vector<uint8_t>& loadVector, size_t offset,
                                uint64_t& stepEquivalentTo0, float& quantizedStepSize, bool staticQuantization, int bitWidth)
{
    std::vector<float> inVector;

    size_t inputFileLength = loadFileToBuffer(inputFile, inVector);

    if (inputFileLength == 0U) {
        return false;
    } else if (loadVector.size() == 0) {
        loadVector.resize(inputFileLength / sizeof(uint8_t));
    } else if (loadVector.size() < inputFileLength/sizeof(float)) {
        std::cerr << "Vector is not large enough to hold data of input file: " << inputFile << "\n";
        return false;
    }

    int elementSize = bitWidth / 8;
    size_t dataStartPos = (offset * inputFileLength * elementSize) / sizeof(float);
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
    }

    in.seekg(0, in.end);
    size_t length = in.tellg();
    in.seekg(0, in.beg);

    if (loadVector.size() == 0) {
        loadVector.resize(length / sizeof(uint8_t));
    } else if (loadVector.size() < length/sizeof(float)) {
        std::cerr << "Vector is not large enough to hold data of input file: " << inputFile << "\n";
    }

    inVector.resize((offset+1) * length / sizeof(uint8_t));
    if (!in.read( reinterpret_cast<char*> (&inVector[offset * length/ sizeof(uint8_t) ]), length) )
    {
        std::cerr << "Failed to read the contents of: " << inputFile << "\n";
    }

    uint64_t stepEquivalentTo0;
    float quantizedStepSize;
    if(!FloatToTfN(loadVector.data(), stepEquivalentTo0, quantizedStepSize, false, inVector.data(), loadVector.size(), 8))
    {
        return false;
    }
    return true;
}

bool loadByteDataFileBatchedFloat(const std::string& inputFile, void* loadPtr, uint64_t offset)
{
    std::ifstream in(inputFile, std::ifstream::binary);
    if (!in.is_open() || !in.good())
    {
        std::cerr << "Failed to open input file: " << inputFile << "\n";
    }

    in.seekg(0, in.end);
    size_t length = in.tellg();
    in.seekg(0, in.beg);

    if (length % sizeof(float) != 0) {
        std::cerr << "Size of input file should be divisible by sizeof(dtype).\n";
        return false;
    }

    if (!in.read( (char*)loadPtr + offset, length) )
    {
        std::cerr << "Failed to read the contents of: " << inputFile << "\n";
    }
    return true;
}

bool SaveITensor(const std::string& path, float* data, size_t tensorStart, size_t tensorEnd)
{
    // Create the directory path if it does not exist
    auto idx = path.find_last_of('/');
    if (idx != std::string::npos)
    {
        std::string dir = path.substr(0, idx);
        if (!EnsureDirectory(dir))
        {
            std::cerr << "Failed to create output directory: " << dir << ": "
                      << std::strerror(errno) << "\n";
            return false;
        }
    }

    std::ofstream os(path, std::ofstream::binary);
    if (!os)
    {
        std::cerr << "Failed to open output file for writing: " << path << "\n";
        return false;
    }

    for ( auto it = data + tensorStart; it != data + tensorEnd; ++it )
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
            std::cerr << "Failed to create output directory: " << dir << ": "
                      << std::strerror(errno) << "\n";
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
