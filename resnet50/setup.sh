pip install onnx
pip install onnx-simplifier

wget https://zenodo.org/record/4735647/files/resnet50_v1.onnx
snpe-onnx-to-dlc --input_network=resnet50_v1.onnx --output_path=resnet50_v1.dlc --input_dim input_tensor:0 1,3,224,224

cd data
python3 resize_and_format.py

# To add: json file, snpe_bench_commands file, etc