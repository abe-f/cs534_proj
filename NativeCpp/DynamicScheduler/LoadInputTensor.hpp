//==============================================================================
//
//  Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
//  All rights reserved.
//  Confidential and Proprietary - Qualcomm Technologies, Inc.
//
//==============================================================================

#ifndef LOADINPUTTENSOR_H
#define LOADINPUTTENSOR_H

#include <unordered_map>
#include <string>
#include <vector>

#include "SNPE/SNPE.hpp"
#include "DlSystem/ITensorFactory.hpp"
#include "DlSystem/TensorMap.hpp"

typedef unsigned int GLuint;
std::unique_ptr<zdl::DlSystem::ITensor> loadInputTensor (std::unique_ptr<zdl::SNPE::SNPE>& snpe,
                                                            std::vector<std::string>& fileLines,
                                                            const zdl::DlSystem::StringList& inputTensorNames);

std::tuple<zdl::DlSystem::TensorMap, bool> loadMultipleInput (std::unique_ptr<zdl::SNPE::SNPE>& snpe,
                                                                std::vector<std::string>& fileLines,
                                                                const zdl::DlSystem::StringList& inputTensorNames,
                                                                std::vector<std::unique_ptr<zdl::DlSystem::ITensor>>& inputs);

bool loadInputUserBufferFloat(std::unordered_map<std::string, std::vector<uint8_t>>& applicationBuffers,
                                std::unique_ptr<zdl::SNPE::SNPE>& snpe,
                                std::vector<std::string>& fileLines);

bool loadInputUserBufferTfN(std::unordered_map<std::string, std::vector<uint8_t>>& applicationBuffers,
                         std::unique_ptr<zdl::SNPE::SNPE>& snpe,
                         std::vector<std::string>& fileLines,
                         zdl::DlSystem::UserBufferMap& inputMap,
                         bool staticQuantization,
                         int bitWidth,
                         bool useNativeInputFiles=false);

void loadInputUserBuffer(std::unordered_map<std::string, GLuint>& applicationBuffers,
                                std::unique_ptr<zdl::SNPE::SNPE>& snpe,
                                const GLuint inputglbuffer);

#endif
