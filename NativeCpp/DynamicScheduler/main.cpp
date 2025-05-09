//==============================================================================
//
//  Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
//  All rights reserved.
//  Confidential and Proprietary - Qualcomm Technologies, Inc.
//
//==============================================================================
//
// This file contains an example application that loads and executes a neural
// network using the SNPE C++ API and saves the layer output to a file.
// Inputs to and outputs from the network are conveyed in binary form as single
// precision floating point values.
//
#include <cstring>
#include <iostream>
#include <fstream>
#include <cstdlib>
#include <vector>
#include <string>
#include <iterator>
#include <unordered_map>
#include <algorithm>
#include <chrono>
#include <thread>

#include "CheckRuntime.hpp"
#include "LoadContainer.hpp"
#include "LoadUDOPackage.hpp"
#include "SetBuilderOptions.hpp"
#include "LoadInputTensor.hpp"
#include "CreateUserBuffer.hpp"
#include "PreprocessInput.hpp"
#include "SaveOutputTensor.hpp"
#include "Util.hpp"
#include "DlSystem/DlError.hpp"
#include "DlSystem/RuntimeList.hpp"

#include "DlSystem/UserBufferMap.hpp"
#include "DlSystem/IUserBuffer.hpp"
#include "DlSystem/SNPEPerfProfile.h"
#include "DlContainer/IDlContainer.hpp"
#include "SNPE/SNPE.hpp"
#include "SNPE/SNPEFactory.hpp"
#include "DiagLog/IDiagLog.hpp"

#ifndef _WIN32
#include <getopt.h>
#else
#include "GetOpt.hpp"
using namespace WinOpt;
#endif
/* Windows Modification
 * Replace <getopt.h> to <GetOpt.hpp> and refactor the "Process command line arguments" part
 */

const int FAILURE = 1;
const int SUCCESS = 0;

    // Command line arguments
    static std::string dlc = "";
    static std::string OutputDir = "./output/";
    const char *inputFile = "";
    std::string bufferTypeStr = "ITENSOR";
    std::string userBufferSourceStr = "CPUBUFFER";
    std::string staticQuantizationStr = "false";
    static zdl::DlSystem::RuntimeList runtimeList;
    bool runtimeSpecified = false;
    bool execStatus = false;
    bool usingInitCaching = false;
    bool staticQuantization = false;
    bool cpuFixedPointMode = false;
    std::string UdoPackagePath = "";
    bool useNativeInputFiles = false;
    static std::string perfProfileStr = "default";
    static zdl::DlSystem::PerformanceProfile_t PerfProfile = zdl::DlSystem::PerformanceProfile_t::BALANCED;;

int dispatch(int argc, char **argv, std::string device_to_run)
{

	auto start = std::chrono::high_resolution_clock::now();
    // static zdl::DlSystem::Runtime_t runtime = zdl::DlSystem::Runtime_t::CPU;
    zdl::DlSystem::Runtime_t runtime = zdl::DlSystem::Runtime_t::CPU;
    if (device_to_run.compare("gpu") == 0)
    {
	runtime = zdl::DlSystem::Runtime_t::GPU;
	std::cout << "Runtime set to GPU in 'r'" << std::endl;
	    std::cout << "Using the runtime: " << (int) runtime << " at start of dispatch (" << device_to_run <<")" << std::endl;
    }
    else if (device_to_run.compare("aip") == 0)
    {
	runtime = zdl::DlSystem::Runtime_t::AIP_FIXED8_TF;
	std::cout << "Runtime set to AIP in 'r'" << std::endl;
	    std::cout << "Using the runtime: " << (int) runtime << " at start of dispatch (" << device_to_run <<")" << std::endl;
    }
    else if (device_to_run.compare("dsp") == 0)
    {
	runtime = zdl::DlSystem::Runtime_t::DSP;
	std::cout << "Runtime set to DSP in 'r'" << std::endl;
	    std::cout << "Using the runtime: " << (int) runtime << " at start of dispatch (" << device_to_run <<")" << std::endl;
    }
    else if (device_to_run.compare("cpu") == 0)
    {
	runtime = zdl::DlSystem::Runtime_t::CPU;
	std::cout << "Runtime set to CPU in 'r'" << std::endl;
	    std::cout << "Using the runtime: " << (int) runtime << " at start of dispatch (" << device_to_run <<")" << std::endl;
    }
    else
    {
	std::cerr << "The runtime option provide is not valid. Defaulting to the CPU runtime." << std::endl;
	std::cout << "Runtime set to CPU as fallback in 'r'" << std::endl;
    }

    enum
    {
        UNKNOWN,
        USERBUFFER_FLOAT,
        USERBUFFER_TF8,
        ITENSOR,
        USERBUFFER_TF16
    };
    enum
    {
        CPUBUFFER,
        GLBUFFER
    };
    // Check if given arguments represent valid files
    std::ifstream dlcFile(dlc);
    std::ifstream inputList(inputFile);
    if (!dlcFile || !inputList)
    {
        std::cout << "Input list or dlc file not valid. Please ensure that you have provided a valid input list and dlc for processing. Run snpe-sample with the -h flag for more details" << std::endl;
        return EXIT_FAILURE;
    }

    // Check if given buffer type is valid
    int bufferType;
    int bitWidth = 0;
    if (bufferTypeStr == "USERBUFFER_FLOAT")
    {
        bufferType = USERBUFFER_FLOAT;
    }
    else if (bufferTypeStr == "USERBUFFER_TF8")
    {
        bufferType = USERBUFFER_TF8;
        bitWidth = 8;
    }
    else if (bufferTypeStr == "USERBUFFER_TF16")
    {
        bufferType = USERBUFFER_TF16;
        bitWidth = 16;
    }
    else if (bufferTypeStr == "ITENSOR")
    {
        bufferType = ITENSOR;
    }
    else
    {
        std::cout << "Buffer type is not valid. Please run snpe-sample with the -h flag for more details" << std::endl;
        return EXIT_FAILURE;
    }

    // Check if given user buffer source type is valid
    int userBufferSourceType = CPUBUFFER;
    // CPUBUFFER / GLBUFFER supported only for USERBUFFER_FLOAT
    if (bufferType == USERBUFFER_FLOAT)
    {
        if (userBufferSourceStr == "CPUBUFFER")
        {
            userBufferSourceType = CPUBUFFER;
        }
        else if (userBufferSourceStr == "GLBUFFER")
        {
#ifndef ENABLE_GL_BUFFER
            std::cout << "GLBUFFER mode is only supported on Android OS" << std::endl;
            return EXIT_FAILURE;
#endif
            userBufferSourceType = GLBUFFER;
        }
        else
        {
            std::cout
                << "Source of user buffer type is not valid. Please run snpe-sample with the -h flag for more details"
                << std::endl;
            return EXIT_FAILURE;
        }
    }

    if (staticQuantizationStr == "true")
    {
        staticQuantization = true;
    }
    else if (staticQuantizationStr == "false")
    {
        staticQuantization = false;
    }
    else
    {
        std::cout << "Static quantization value is not valid. Please run snpe-sample with the -h flag for more details"
                  << std::endl;
        return EXIT_FAILURE;
    }

    // Check if both runtimelist and runtime are passed in
    if (runtimeSpecified && runtimeList.empty() == false)
    {
        std::cout << "Invalid option cannot mix runtime order -l with runtime -r " << std::endl;
        std::exit(FAILURE);
    }


    // Open the DL container that contains the network to execute.
    // Create an instance of the SNPE network from the now opened container.
    // The factory functions provided by SNPE allow for the specification
    // of which layers of the network should be returned as output and also
    // if the network should be run on the CPU or GPU.
    // The runtime availability API allows for runtime support to be queried.
    // If a selected runtime is not available, we will issue a warning and continue,
    // expecting the invalid configuration to be caught at SNPE network creation.

    if (runtimeSpecified)
    {
	    std::cout << "Using the runtime: " << (int) runtime << " before checkRuntime (" << device_to_run <<")" << std::endl;
        runtime = checkRuntime(runtime, staticQuantization);
	    std::cout << "Using the runtime: " << (int) runtime << " after checkRuntime (" << device_to_run <<")" << std::endl;
    }

    std::unique_ptr<zdl::DlContainer::IDlContainer> container = loadContainerFromFile(dlc);
    if (container == nullptr)
    {
        std::cerr << "Error while opening the container file." << std::endl;
        return EXIT_FAILURE;
    }

    bool useUserSuppliedBuffers = (bufferType == USERBUFFER_FLOAT || bufferType == USERBUFFER_TF8 || bufferType == USERBUFFER_TF16);

    zdl::DlSystem::PlatformConfig platformConfig;
#ifdef ENABLE_GL_BUFFER
    if (userBufferSourceType == GLBUFFER)
    {
        platformConfig.SetIsUserGLBuffer(true);
    }
#endif

    // load UDO package
    if (false == loadUDOPackage(UdoPackagePath))
    {
        std::cerr << "Failed to load UDO Package(s)." << std::endl;
        return EXIT_FAILURE;
    }

    if (perfProfileStr == "default" || perfProfileStr == "balanced")
    {
        PerfProfile = zdl::DlSystem::PerformanceProfile_t::BALANCED;
    }
    else if (perfProfileStr == "high_performance")
    {
        PerfProfile = zdl::DlSystem::PerformanceProfile_t::HIGH_PERFORMANCE;
    }
    else if (perfProfileStr == "power_saver")
    {
        PerfProfile = zdl::DlSystem::PerformanceProfile_t::POWER_SAVER;
    }
    else if (perfProfileStr == "system_settings")
    {
        PerfProfile = zdl::DlSystem::PerformanceProfile_t::SYSTEM_SETTINGS;
    }
    else if (perfProfileStr == "sustained_high_performance")
    {
        PerfProfile = zdl::DlSystem::PerformanceProfile_t::SUSTAINED_HIGH_PERFORMANCE;
    }
    else if (perfProfileStr == "burst")
    {
        PerfProfile = zdl::DlSystem::PerformanceProfile_t::BURST;
    }
    else if (perfProfileStr == "low_power_saver")
    {
        PerfProfile = zdl::DlSystem::PerformanceProfile_t::LOW_POWER_SAVER;
    }
    else if (perfProfileStr == "high_power_saver")
    {
        PerfProfile = zdl::DlSystem::PerformanceProfile_t::HIGH_POWER_SAVER;
    }
    else if (perfProfileStr == "low_balanced")
    {
        PerfProfile = zdl::DlSystem::PerformanceProfile_t::LOW_BALANCED;
    }
    else if (perfProfileStr == "extreme_power_saver")
    {
        PerfProfile = zdl::DlSystem::PerformanceProfile_t::EXTREME_POWER_SAVER;
    }
    else
    {
        std::cerr
            << "ERROR: Invalid setting pased to the argument --perf_profile.\n "
               "Please check the Arguments section in the description below\n";
        return EXIT_FAILURE;
    }

    std::unique_ptr<zdl::SNPE::SNPE> snpe;
	auto builderstart = std::chrono::high_resolution_clock::now();
    snpe = setBuilderOptions(container, runtime, runtimeList,
	     useUserSuppliedBuffers, platformConfig,
	     usingInitCaching, cpuFixedPointMode, PerfProfile);
	auto builderend = std::chrono::high_resolution_clock::now();
  	auto builderduration = std::chrono::duration_cast<std::chrono::microseconds>(builderend - builderstart);
	std::cout << "Time taken to setBuilderOptions: " << builderduration.count() << " microseconds (" << device_to_run << ")" << std::endl;
    std::cout << "Using the runtime: " << (int) runtime << "(" << device_to_run <<")" << std::endl;

    if (snpe == nullptr)
    {
        std::cerr << "Error while building SNPE object." << std::endl;
        return EXIT_FAILURE;
    }
    if (usingInitCaching)
    {
        if (container->save(dlc))
        {
            std::cout << "Saved container into archive successfully" << std::endl;
        }
        else
        {
            std::cout << "Failed to save container into archive" << std::endl;
        }
    }

    // Configure logging output and start logging. The snpe-diagview
    // executable can be used to read the content of this diagnostics file
    auto logger_opt = snpe->getDiagLogInterface();
    if (!logger_opt)
        throw std::runtime_error("SNPE failed to obtain logging interface");
    auto logger = *logger_opt;
    auto opts = logger->getOptions();

    opts.LogFileDirectory = OutputDir;
    if (!logger->setOptions(opts))
    {
        std::cerr << "Failed to set options" << std::endl;
        return EXIT_FAILURE;
    }
    if (!logger->start())
    {
        std::cerr << "Failed to start logger" << std::endl;
        return EXIT_FAILURE;
    }

    // Check the batch size for the container
    // SNPE 1.16.0 (and newer) assumes the first dimension of the tensor shape
    // is the batch size.
    zdl::DlSystem::TensorShape tensorShape;
    tensorShape = snpe->getInputDimensions();
    size_t batchSize = tensorShape.getDimensions()[0];
#ifdef ENABLE_GL_BUFFER
    size_t bufSize = 0;
    if (userBufferSourceType == GLBUFFER)
    {
        if (batchSize > 1)
        {
            std::cerr << "GL buffer source mode does not support batchsize larger than 1" << std::endl;
            return EXIT_FAILURE;
        }
        bufSize = calcSizeFromDims(tensorShape.getDimensions(), tensorShape.rank(), sizeof(float));
    }
#endif
    std::cout << "Batch size for the container is " << batchSize << std::endl;

    // Open the input file listing and group input files into batches
    std::vector<std::vector<std::string>> inputs = preprocessInput(inputFile, batchSize);

    // Load contents of input file batches ino a SNPE tensor or user buffer,
    // user buffer include cpu buffer and OpenGL buffer,
    // execute the network with the input and save each of the returned output to a file.
    if (useUserSuppliedBuffers)
    {
        // SNPE allows its input and output buffers that are fed to the network
        // to come from user-backed buffers. First, SNPE buffers are created from
        // user-backed storage. These SNPE buffers are then supplied to the network
        // and the results are stored in user-backed output buffers. This allows for
        // reusing the same buffers for multiple inputs and outputs.
        zdl::DlSystem::UserBufferMap inputMap, outputMap;
        std::vector<std::unique_ptr<zdl::DlSystem::IUserBuffer>> snpeUserBackedInputBuffers, snpeUserBackedOutputBuffers;
        std::unordered_map<std::string, std::vector<uint8_t>> applicationOutputBuffers;

        if (bufferType == USERBUFFER_TF8 || bufferType == USERBUFFER_TF16)
        {
            createOutputBufferMap(outputMap, applicationOutputBuffers, snpeUserBackedOutputBuffers, snpe, true, bitWidth);

            std::unordered_map<std::string, std::vector<uint8_t>> applicationInputBuffers;
            createInputBufferMap(inputMap, applicationInputBuffers, snpeUserBackedInputBuffers, snpe, true, staticQuantization, bitWidth);

            for (size_t i = 0; i < inputs.size(); i++)
            {
                // Load input user buffer(s) with values from file(s)
                if (batchSize > 1)
                    std::cout << "Batch " << i << ":" << std::endl;
                if (!loadInputUserBufferTfN(applicationInputBuffers, snpe, inputs[i], inputMap, staticQuantization, bitWidth, useNativeInputFiles))
                {
                    return EXIT_FAILURE;
                }
                // Execute the input buffer map on the model with SNPE
                execStatus = snpe->execute(inputMap, outputMap);
                // Save the execution results only if successful
                if (execStatus == true)
                {
                    if (!saveOutput(outputMap, applicationOutputBuffers, OutputDir, i * batchSize, batchSize, true, bitWidth))
                    {
                        return EXIT_FAILURE;
                    }
                }
                else
                {
                    std::cerr << "Error while executing the network." << std::endl;
                }
            }
        }
        else if (bufferType == USERBUFFER_FLOAT)
        {
            createOutputBufferMap(outputMap, applicationOutputBuffers, snpeUserBackedOutputBuffers, snpe, false, bitWidth);

            if (userBufferSourceType == CPUBUFFER || userBufferSourceType == GLBUFFER)
            {
                std::unordered_map<std::string, std::vector<uint8_t>> applicationInputBuffers;
                createInputBufferMap(inputMap, applicationInputBuffers, snpeUserBackedInputBuffers, snpe, false, false, bitWidth);

                for (size_t i = 0; i < inputs.size(); i++)
                {
                    // Load input user buffer(s) with values from file(s)
                    if (batchSize > 1)
                        std::cout << "Batch " << i << ":" << std::endl;
                    if (!loadInputUserBufferFloat(applicationInputBuffers, snpe, inputs[i]))
                    {
                        return EXIT_FAILURE;
                    }
                    // Execute the input buffer map on the model with SNPE
                    execStatus = snpe->execute(inputMap, outputMap);
                    // Save the execution results only if successful
                    if (execStatus == true)
                    {
                        if (!saveOutput(outputMap, applicationOutputBuffers, OutputDir, i * batchSize, batchSize, false, bitWidth))
                        {
                            return EXIT_FAILURE;
                        }
                    }
                    else
                    {
                        std::cerr << "Error while executing the network." << std::endl;
                    }
                }
            }
        }
    }
    else if (bufferType == ITENSOR)
    {
        // A tensor map for SNPE execution outputs
        zdl::DlSystem::TensorMap outputTensorMap;
        // Get input names and number
        const auto &inputTensorNamesRef = snpe->getInputTensorNames();
        if (!inputTensorNamesRef)
            throw std::runtime_error("Error obtaining Input tensor names");
        const auto &inputTensorNames = *inputTensorNamesRef;

        for (size_t i = 0; i < inputs.size(); i++)
        {
            // Load input/output buffers with ITensor
            if (batchSize > 1)
                std::cout << "Batch " << i << ":" << std::endl;
            if (inputTensorNames.size() == 1)
            {
                // Load input/output buffers with ITensor
                std::unique_ptr<zdl::DlSystem::ITensor> inputTensor = loadInputTensor(snpe, inputs[i], inputTensorNames);
                if (!inputTensor)
                {
                    return EXIT_FAILURE;
                }
                // Execute the input tensor on the model with SNPE
	auto executestart = std::chrono::high_resolution_clock::now();
                execStatus = snpe->execute(inputTensor.get(), outputTensorMap);
	auto executeend = std::chrono::high_resolution_clock::now();
  	auto executeduration = std::chrono::duration_cast<std::chrono::microseconds>(executeend - executestart);
	std::cout << "Time taken to execute: " << executeduration.count() << " microseconds" << std::endl;
            }
            else
            {
                std::vector<std::unique_ptr<zdl::DlSystem::ITensor>> inputTensors(inputTensorNames.size());
                zdl::DlSystem::TensorMap inputTensorMap;
                bool inputLoadStatus = false;
                // Load input/output buffers with TensorMap
                std::tie(inputTensorMap, inputLoadStatus) = loadMultipleInput(snpe, inputs[i], inputTensorNames, inputTensors);
                if (!inputLoadStatus)
                {
                    return EXIT_FAILURE;
                }
                // Execute the multiple input tensorMap on the model with SNPE
                execStatus = snpe->execute(inputTensorMap, outputTensorMap);
            }
            // Save the execution results if execution successful
            if (execStatus == true)
            {
                if (!saveOutput(outputTensorMap, OutputDir, i * batchSize, batchSize))
                {
                    return EXIT_FAILURE;
                }
            }
            else
            {
                std::cerr << "Error while executing the network." << std::endl;
            }
        }
    }
    // Freeing of snpe object

    // Terminate Logging
    zdl::SNPE::SNPEFactory::terminateLogging();

	auto end = std::chrono::high_resolution_clock::now();
  	auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
	std::cout << "Total time taken: " << duration.count() << " microseconds for " << device_to_run << std::endl;

    snpe.reset();
    return SUCCESS;
}

int main(int argc, char **argv)
{

	std::string device_to_run = "cpu";
    enum
    {
        UNKNOWN,
        USERBUFFER_FLOAT,
        USERBUFFER_TF8,
        ITENSOR,
        USERBUFFER_TF16
    };
    enum
    {
        CPUBUFFER,
        GLBUFFER
    };


#ifdef __ANDROID__
    // Initialize Logs with level LOG_ERROR.
    zdl::SNPE::SNPEFactory::initializeLogging(zdl::DlSystem::LogLevel_t::LOG_ERROR);
#else
    // Initialize Logs with specified log level as LOG_ERROR and log path as "./Log".
    zdl::SNPE::SNPEFactory::initializeLogging(zdl::DlSystem::LogLevel_t::LOG_ERROR, "./Log");
#endif

    // Update Log Level to LOG_WARN.
    zdl::SNPE::SNPEFactory::setLogLevel(zdl::DlSystem::LogLevel_t::LOG_WARN);

    // Process command line arguments
    int opt = 0;
#ifndef _WIN32
    while ((opt = getopt(argc, argv, "hi:d:o:b:q:s:z:r:l:u:cx:p:n")) != -1)
#else
    enum OPTIONS
    {
        OPT_HELP = 'h',
        OPT_CONTAINER = 'd',
        OPT_INPUT_LIST = 'i',
        OPT_OUTPUT_DIR = 'o',
        OPT_USERBUFFER = 'b',
        OPT_RUNTIME = 'r',
        OPT_RESIZABLE_DIM = 'z',
        OPT_INITBLOBSCACHE = 'c',
        OPT_RUNTIME_ORDER = 'l',
        OPT_STATIC_QUANTIZATION = 'q',
        OPT_UDO_PACKAGE_PATH = 'u',
        OPT_BUFF_SOURCE = 's',
        OPT_CPU_FXP = 'x',
        OPT_NATIVE_INPUT = 'n',
        OPT_PERF_PROFILE = 'p'
    };
    static struct WinOpt::option long_options[] = {
        {"h", WinOpt::no_argument, NULL, OPT_HELP},
        {"d", WinOpt::required_argument, NULL, OPT_CONTAINER},
        {"i", WinOpt::required_argument, NULL, OPT_INPUT_LIST},
        {"o", WinOpt::required_argument, NULL, OPT_OUTPUT_DIR},
        {"b", WinOpt::required_argument, NULL, OPT_USERBUFFER},
        {"r", WinOpt::required_argument, NULL, OPT_RUNTIME},
        {"z", WinOpt::required_argument, NULL, OPT_RESIZABLE_DIM},
        {"c", WinOpt::no_argument, NULL, OPT_INITBLOBSCACHE},
        {"l", WinOpt::required_argument, NULL, OPT_RUNTIME_ORDER},
        {"q", WinOpt::required_argument, NULL, OPT_STATIC_QUANTIZATION},
        {"x", WinOpt::no_argument, NULL, OPT_CPU_FXP},
        {"u", WinOpt::required_argument, NULL, OPT_UDO_PACKAGE_PATH},
        {"s", WinOpt::required_argument, NULL, OPT_BUFF_SOURCE},
        {"n", WinOpt::no_argument, NULL, OPT_NATIVE_INPUT},
        {"p", WinOpt::required_argument, NULL, OPT_PERF_PROFILE},
        {NULL, 0, NULL, 0}};
    int long_index = 0;
    while ((opt = WinOpt::GetOptLongOnly(argc, argv, "", long_options, &long_index)) != -1)
#endif
    {
        switch (opt)
        {
        case 'h':
            std::cout
                << "\nDESCRIPTION:\n"
                << "------------\n"
                << "Example application demonstrating how to load and execute a neural network\n"
                << "using the SNPE C++ API.\n"
                << "\n\n"
                << "REQUIRED ARGUMENTS:\n"
                << "-------------------\n"
                << "  -d  <FILE>   Path to the DL container containing the network.\n"
                << "  -i  <FILE>   Path to a file listing the inputs for the network.\n"
                << "  -o  <PATH>   Path to directory to store output results.\n"
                << "\n"
                << "OPTIONAL ARGUMENTS:\n"
                << "-------------------\n"
                << "  -b  <TYPE>   Type of buffers to use [USERBUFFER_FLOAT, USERBUFFER_TF8, ITENSOR, USERBUFFER_TF16] (" << bufferTypeStr << " is default).\n"
                << "  -q  <BOOL>    Specifies to use static quantization parameters from the model instead of input specific quantization [true, false]. Used in conjunction with USERBUFFER_TF8. \n"
                << "  -r  <RUNTIME> The runtime to be used [gpu, dsp, aip, cpu] (cpu is default). \n"
                << "  -u  <VAL,VAL> Path to UDO package with registration library for UDOs. \n"
                << "                Optionally, user can provide multiple packages as a comma-separated list. \n"
                << "  -z  <NUMBER>  The maximum number that resizable dimensions can grow into. \n"
                << "                Used as a hint to create UserBuffers for models with dynamic sized outputs. Should be a positive integer and is not applicable when using ITensor. \n"
#ifdef ENABLE_GL_BUFFER
                << "  -s  <TYPE>   Source of user buffers to use [GLBUFFER, CPUBUFFER] (" << userBufferSourceStr << " is default).\n"
#endif
                << "  -c           Enable init caching to accelerate the initialization process of SNPE. Defaults to disable.\n"
                << "  -l  <VAL,VAL,VAL> Specifies the order of precedence for runtime e.g  cpu_float32, dsp_fixed8_tf etc. Valid values are:- \n"
                << "                    cpu_float32 (Snapdragon CPU)       = Data & Math: float 32bit \n"
                << "                    gpu_float32_16_hybrid (Adreno GPU) = Data: float 16bit Math: float 32bit \n"
                << "                    dsp_fixed8_tf (Hexagon DSP)        = Data & Math: 8bit fixed point Tensorflow style format \n"
                << "                    gpu_float16 (Adreno GPU)           = Data: float 16bit Math: float 16bit \n"
#if DNN_RUNTIME_HAVE_AIP_RUNTIME
                << "                    aip_fixed8_tf (Snapdragon HTA+HVX) = Data & Math: 8bit fixed point Tensorflow style format \n"

#endif
                << "                    cpu (Snapdragon CPU)               = Same as cpu_float32 \n"
                << "                    gpu (Adreno GPU)                   = Same as gpu_float32_16_hybrid \n"
                << "                    dsp (Hexagon DSP)                  = Same as dsp_fixed8_tf \n"
#if DNN_RUNTIME_HAVE_AIP_RUNTIME
                << "                    aip (Snapdragon HTA+HVX)           = Same as aip_fixed8_tf \n"
#endif
                << "  -x            Specifies to use the fixed point execution on CPU runtime for quantized DLC.\n"
                << "                Used in conjunction with CPU runtime.\n"
                << "  -n            Specifies to consume the input file(s) in their native data types. \n"
                << "  -p <TYPE>     Specifies perf profile to set. Valid settings are \"low_balanced\" , \"balanced\" , \"default\",\n"
                << "\"high_performance\" ,\"sustained_high_performance\", \"burst\", \"low_power_saver\", \"power_saver\",\n"
                << "\"high_power_saver\", \"extreme_power_saver\", and \"system_settings\"."
                << std::endl;

            std::exit(SUCCESS);
        case 'i':
            inputFile = optarg;
            break;
        case 'd':
            dlc = optarg;
            break;
        case 'o':
            OutputDir = optarg;
            break;
        case 'b':
            bufferTypeStr = optarg;
            break;
        case 'q':
            staticQuantizationStr = optarg;
            break;
        case 's':
            userBufferSourceStr = optarg;
            break;
        case 'z':
            setResizableDim(atoi(optarg));
            break;
        case 'r':
            runtimeSpecified = true;
            break;

        case 'l':
        {
            std::string inputString = optarg;
            // std::cout<<"Input String: "<<inputString<<std::endl;
            std::vector<std::string> runtimeStrVector;
            split(runtimeStrVector, inputString, ',');

            // Check for dups
            for (auto it = runtimeStrVector.begin(); it != runtimeStrVector.end() - 1; it++)
            {
                auto found = std::find(it + 1, runtimeStrVector.end(), *it);
                if (found != runtimeStrVector.end())
                {
                    // std::cerr << "Error: Invalid values passed to the argument " << argv[optind - 2] << ". Duplicate entries in runtime order" << std::endl;
                    std::exit(FAILURE);
                }
            }

            runtimeList.clear();
            for (auto &runtimeStr : runtimeStrVector)
            {
                // std::cout<<runtimeStr<<std::endl;
                zdl::DlSystem::Runtime_t eachruntime = zdl::DlSystem::RuntimeList::stringToRuntime(runtimeStr.c_str());
                if (eachruntime != zdl::DlSystem::Runtime_t::UNSET)
                {
                    bool ret = runtimeList.add(eachruntime);
                    if (ret == false)
                    {
                        std::cerr << zdl::DlSystem::getLastErrorString() << std::endl;
                        // std::cerr << "Error: Invalid values passed to the argument " << argv[optind - 2] << ". Please provide comma seperated runtime order of precedence" << std::endl;
                        std::exit(FAILURE);
                    }
                }
                else
                {
                    // std::cerr << "Error: Invalid values passed to the argument " << argv[optind - 2] << ". Please provide comma seperated runtime order of precedence" << std::endl;
                    std::exit(FAILURE);
                }
            }
        }
        break;

        case 'c':
            usingInitCaching = true;
            break;
        case 'u':
            UdoPackagePath = optarg;
            break;
        case 'x':
            cpuFixedPointMode = true;
            break;
        case 'n':
            useNativeInputFiles = true;
            break;
        case 'p':
            perfProfileStr = optarg;
            break;
        default:
            std::cout << "Invalid parameter specified. Please run snpe-sample with the -h flag to see required arguments" << std::endl;
            std::exit(FAILURE);
        }
    }


	std::cout << "Dispatching to main code with cpu" << std::endl;
	std::thread cpu_thread(dispatch, argc, argv, "cpu");
	//cpu_thread.join();

	std::cout << "Dispatching to main code with gpu" << std::endl;
	// dispatch(argc, argv, "gpu");
	std::thread gpu_thread(dispatch, argc, argv, "gpu");
	//gpu_thread.join();

	std::cout << "Dispatching to main code with dsp" << std::endl;
	// dispatch(argc, argv, "dsp");
	std::thread dsp_thread(dispatch, argc, argv, "dsp");
	//dsp_thread.join();

	cpu_thread.join();
	std::cout << "CPU done" << std::endl;
	gpu_thread.join();
	std::cout << "GPU done" << std::endl;
	dsp_thread.join();
	std::cout << "DSP done" << std::endl;
}

