# Download AI Benchmark and copy models in models folder
#wget http://data.vision.ee.ethz.ch/ihnatova/ai_benchmark/public/600/AI_Benchmark_V6.0.2.apk
#mkdir ai_benchmark
#cp AI_Benchmark_V6.0.2.apk ai_benchmark/AI_Benchmark_V6.0.2.zip
#cd ai_benchmark
#unzip AI*
#cd ..
#mkdir models
#cp ai_benchmark/assets/models/*_float.tflite models

# Download SNPE SDK
wget https://softwarecenter.qualcomm.com/api/download/software/sdks/Qualcomm_AI_Runtime_Community/All/2.33.0.250327/v2.33.0.250327.zip
unzip v2.33*

# Get the dockerfile from the qidk repo
git clone https://github.com/quic/qidk
cd qidk
git apply ../qidk.patch
cd Tools/snpe_qnn_docker
docker build -t snpe_qnn .
cd ../../..

# Run and exec
# The container's /work directory is mapped to the directory this command is run from.
docker run --net host -dit --name snpe_qnn_container --mount type=bind,source="$(pwd)",target="/work/" --workdir="/work" snpe_qnn:latest
docker exec -it snpe_qnn_container /bin/bash
# On first time setup, this will download a bunch of stuff. For the path, say /work/qairt/2.33.0250327/
# Then just say yes and press enter for everything