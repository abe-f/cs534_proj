//==============================================================================
//
//  Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
//  All rights reserved.
//  Confidential and Proprietary - Qualcomm Technologies, Inc.
//
//==============================================================================

#include <iostream>
#include <algorithm>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

#include "SaveOutputTensor.hpp"
#include "Util.hpp"

#include "SNPE/SNPE.hpp"
#include "DlSystem/ITensor.hpp"
#include "DlSystem/StringList.hpp"
#include "DlSystem/TensorMap.hpp"
#include "DlSystem/TensorShape.hpp"

/* Windows Modification
modified function : static std::string ToLegalFilename(const std::string& s);
convert illegal name character in output path.
*/

static std::string ToLegalFilename(const std::string& s);
#ifndef _WIN32
static std::string ToLegalFilename(const std::string& s){ return s; }
#else
static const std::unordered_set<char> illegal_chars = { '<', '>', '\"', '\\',
                                                       '|', '?', ':', '*' };
static std::string ToLegalFilename(const std::string& s)
{
  std::string result = s;

  for (int i = 0; i < s.size(); ++i) {
    char c = s[i];

    if (illegal_chars.find(c) != illegal_chars.end()) {
      result[i] = '_';
    }
  }

  return result;
}
#endif
// Print the results to raw files
// ITensor
bool saveOutput (zdl::DlSystem::TensorMap outputTensorMap,
                 const std::string& outputDir,
                 int num,
                 size_t batchSize)
{
    // Get all output tensor names from the network
    zdl::DlSystem::StringList tensorNames = outputTensorMap.getTensorNames();

    // Iterate through the output Tensor map, and print each output layer name to a raw file
    for( auto& name : tensorNames)
    {
        // Split the batched output tensor and save the results
        for(size_t i=0; i<batchSize; i++) {
            std::ostringstream path;
            path << outputDir << "/"
                 << "Result_" << num + i << "/"
                 << ToLegalFilename(name) << ".raw";
            auto tensorPtr = outputTensorMap.getTensor(name);
            size_t batchChunk = tensorPtr->getSize() / batchSize;

            if(!SaveITensorBatched(path.str(), tensorPtr, i, batchChunk))
            {
                return false;
            }
        }
    }
    return true;
}

// Execute the network on an input user buffer map and print results to raw files
bool saveOutput (zdl::DlSystem::UserBufferMap& outputMap,
                 std::unordered_map<std::string,std::vector<uint8_t>>& applicationOutputBuffers,
                 const std::string& outputDir,
                 int num,
                 size_t batchSize,
                 bool isTfNBuffer,
                 int bitWidth)
{
   // Get all output buffer names from the network
   const zdl::DlSystem::StringList& outputBufferNames = outputMap.getUserBufferNames();

   int elementSize = bitWidth / 8;

   // Iterate through output buffers and print each output to a raw file
   for(auto& name : outputBufferNames)
   {
       for(size_t i=0; i<batchSize; i++) {
           std::ostringstream path;
           path << outputDir << "/"
                << "Result_" << num + i << "/"
                << ToLegalFilename(name) << ".raw";
           auto bufferPtr = outputMap.getUserBuffer(name);
           size_t batchChunk = bufferPtr->getSize() / batchSize;
           size_t dataChunk = bufferPtr->getOutputSize() / batchSize;
           if(batchChunk != dataChunk) {
              std::cout << "\tUserBuffer size is " << bufferPtr->getSize() << " bytes, but "
                                                 << bufferPtr->getOutputSize() << " bytes of data was found." << std::endl;
              if( dataChunk > batchChunk )
                 std::cout << "\tAssign a larger buffer using a bigger -z argument" << std::endl;
              batchChunk = std::min(batchChunk,dataChunk);
           }
           if (isTfNBuffer)
           {
              std::vector<uint8_t> output;
              zdl::DlSystem::UserBufferEncodingTfN ubetfN = dynamic_cast<zdl::DlSystem::UserBufferEncodingTfN &>(outputMap.getUserBuffer(name)->getEncoding());
              output.resize(applicationOutputBuffers.at(name).size() * sizeof(float) / elementSize);
              TfNToFloat(reinterpret_cast<float *>(&output[0]),applicationOutputBuffers.at(name).data(),
                         ubetfN.getStepExactly0(),ubetfN.getQuantizedStepSize(),
                        applicationOutputBuffers.at(name).size() / elementSize, bitWidth);
              if(!SaveUserBufferBatched(path.str(), output, i, batchChunk * sizeof(float) / elementSize))
              {
                  return false;
              }
           }
           else
           {
              if(!SaveUserBufferBatched(path.str(), applicationOutputBuffers.at(name), i, batchChunk))
              {
                  return false;
              }
           }
       }
   }
   return true;
}
