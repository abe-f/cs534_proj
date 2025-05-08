# Pushing SNPE libraries and binaries to the device

export SNPE_TARGET_ARCH=aarch64-android
export SNPE_TARGET_DSPARCH=hexagon-v73

adb shell "mkdir -p /data/local/tmp/snpeexample/$SNPE_TARGET_ARCH/bin"
adb shell "mkdir -p /data/local/tmp/snpeexample/$SNPE_TARGET_ARCH/lib"
adb shell "mkdir -p /data/local/tmp/snpeexample/dsp/lib"

adb push $SNPE_ROOT/lib/$SNPE_TARGET_ARCH/*.so \
      /data/local/tmp/snpeexample/$SNPE_TARGET_ARCH/lib
adb push $SNPE_ROOT/lib/$SNPE_TARGET_DSPARCH/unsigned/*.so \
      /data/local/tmp/snpeexample/dsp/lib
adb push $SNPE_ROOT/bin/$SNPE_TARGET_ARCH/snpe-net-run \
      /data/local/tmp/snpeexample/$SNPE_TARGET_ARCH/bin

# Setup environment variables on the device
adb shell
export SNPE_TARGET_ARCH=aarch64-android
export LD_LIBRARY_PATH=$LD_LIBRARY_PATH:/data/local/tmp/snpeexample/$SNPE_TARGET_ARCH/lib
export PATH=$PATH:/data/local/tmp/snpeexample/$SNPE_TARGET_ARCH/bin
snpe-net-run -h
exit

# Push model data to Android target
adb shell "rm -r /data/local/tmp/model_to_use"
adb shell "mkdir -p /data/local/tmp/model_to_use"
adb push inception_v3_quantized.dlc /data/local/tmp/model_to_use/
adb push inception_v3_346x346x3_input_list.txt /data/local/tmp/model_to_use/
adb push inception_v3_346x346x3.raw /data/local/tmp/model_to_use/

# Running on Android using CPU Runtime
adb shell
rm -r /data/local/tmp/model_to_use/output
export SNPE_TARGET_ARCH=aarch64-android
export LD_LIBRARY_PATH=$LD_LIBRARY_PATH:/data/local/tmp/snpeexample/$SNPE_TARGET_ARCH/lib
export PATH=$PATH:/data/local/tmp/snpeexample/$SNPE_TARGET_ARCH/bin
cd /data/local/tmp/model_to_use
snpe-net-run --container inception_v3_quantized.dlc --input_list inception_v3_346x346x3_input_list.txt
exit

# Pull output from Device
adb pull /data/local/tmp/model_to_use/output/SNPEDiag_0.log log_cpu

# Running on Android using GPU Runtime
adb shell
rm -r /data/local/tmp/model_to_use/output
export SNPE_TARGET_ARCH=aarch64-android
export LD_LIBRARY_PATH=$LD_LIBRARY_PATH:/data/local/tmp/snpeexample/$SNPE_TARGET_ARCH/lib
export PATH=$PATH:/data/local/tmp/snpeexample/$SNPE_TARGET_ARCH/bin
export ADSP_LIBRARY_PATH="/data/local/tmp/snpeexample/dsp/lib;/system/lib/rfsa/adsp;/system/vendor/lib/rfsa/adsp;/dsp"
cd /data/local/tmp/model_to_use
snpe-net-run --container inception_v3_quantized.dlc --input_list inception_v3_346x346x3_input_list.txt --use_gpu
exit

# Pull output from Device
adb pull /data/local/tmp/model_to_use/output/SNPEDiag_0.log log_gpu

# Running on Android using DSP Runtime
adb shell
rm -r /data/local/tmp/model_to_use/output
export SNPE_TARGET_ARCH=aarch64-android
export LD_LIBRARY_PATH=$LD_LIBRARY_PATH:/data/local/tmp/snpeexample/$SNPE_TARGET_ARCH/lib
export PATH=$PATH:/data/local/tmp/snpeexample/$SNPE_TARGET_ARCH/bin
export ADSP_LIBRARY_PATH="/data/local/tmp/snpeexample/dsp/lib;/system/lib/rfsa/adsp;/system/vendor/lib/rfsa/adsp;/dsp"
cd /data/local/tmp/model_to_use
snpe-net-run --container inception_v3_quantized.dlc --input_list inception_v3_346x346x3_input_list.txt --use_dsp
exit

# Pull output from Device
adb pull /data/local/tmp/model_to_use/output/SNPEDiag_0.log log_dsp

# Diagview
snpe-diagview --input_log=log_cpu | grep "Total Inference Time"
snpe-diagview --input_log=log_gpu | grep "Total Inference Time"
snpe-diagview --input_log=log_dsp | grep "Total Inference Time"


# Checking NativeCPP
rm -r output_sample; snpe-sample -b ITENSOR -d inception_v3.dlc -i target_raw_list.txt -o output_sample -r {cpu, gpu, dsp}
rm -r output_sample/; adb pull /data/local/tmp/inception_v3/output_sample output_sample; python3 $SNPE_ROOT/examples/Models/InceptionV3/scripts/show_inceptionv3_classifications.py -i data/target_raw_list.txt -o output_sample/ -l data/imagenet_slim_labels.txt

# Building and pushing a DynamicScheduler
cd $SNPE_ROOT/examples/SNPE/NativeCpp/DynamicScheduler

ndk-build NDK_TOOLCHAIN_VERSION=clang APP_STL=c++_static NDK_PROJECT_PATH=. NDK_APPLICATION_MK=Application.mk APP_BUILD_SCRIPT=Android.mk

export SNPE_TARGET_ARCH=aarch64-android
export SNPE_TARGET_DSPARCH=hexagon-v73
adb shell "mkdir -p /data/local/tmp/snpeexample/$SNPE_TARGET_ARCH/bin"
adb shell "mkdir -p /data/local/tmp/snpeexample/$SNPE_TARGET_ARCH/lib"
adb shell "mkdir -p /data/local/tmp/snpeexample/dsp/lib"
adb push $SNPE_ROOT/lib/$SNPE_TARGET_ARCH/*.so /data/local/tmp/snpeexample/$SNPE_TARGET_ARCH/lib
adb push $SNPE_ROOT/lib/$SNPE_TARGET_DSPARCH/unsigned/*.so /data/local/tmp/snpeexample/dsp/lib
adb push $SNPE_ROOT/examples/SNPE/NativeCpp/DynamicScheduler/obj/local/arm64-v8a/snpe-sample /data/local/tmp/snpeexample/$SNPE_TARGET_ARCH/bin

# Running a DynamicScheduler on the device
adb shell
export SNPE_TARGET_ARCH=aarch64-android
export LD_LIBRARY_PATH=$LD_LIBRARY_PATH:/data/local/tmp/snpeexample/$SNPE_TARGET_ARCH/lib
export PATH=$PATH:/data/local/tmp/snpeexample/$SNPE_TARGET_ARCH/bin
export ADSP_LIBRARY_PATH="/data/local/tmp/snpeexample/dsp/lib;/system/lib/rfsa/adsp;/system/vendor/lib/rfsa/adsp;/dsp"
cd /data/local/tmp/inception_v3
snpe-sample -b ITENSOR -d inception_v3_quantized.dlc  -i target_raw_list.txt -o output_sample -r placeholder
exit
