# This file converts a tflite model
# Assumes snpe-dlc-quantize and snpe-tflite-to-dlc are in path

import os
import subprocess
import sys
import numpy as np
from PIL import Image
import csv

def run_command(cmd):
    print(f"Running: {cmd}")
    subprocess.run(cmd, shell=True, check=True)

def convert_tflite_to_dlc(tflite_path, dlc_path):
    if os.path.exists(dlc_path):
        print(f"[Skip] DLC already exists: {dlc_path}")
        return
    run_command(f"snpe-tflite-to-dlc --input_network {tflite_path} --output_path {dlc_path}")

def extract_input_shape(dlc_path, csv_path):
    run_command(f"snpe-dlc-info -i {dlc_path} --save {csv_path}")

    with open(csv_path, newline='', encoding='utf-8-sig') as f:
        reader = csv.reader(f)
        for row in reader:
            if len(row) < 2:
                continue
            dims_str = row[1].strip('"')
            dims_split = dims_str.split(',')

            # Skip non-numeric rows (like the header)
            if all(part.strip().isdigit() for part in dims_split):
                return list(map(int, dims_split))

    raise RuntimeError("Could not extract input shape from input_info.csv")

def convert_image_to_raw(png_path, raw_path, dims):
    if os.path.exists(raw_path):
        print(f"[Skip] Raw input already exists: {raw_path}")
        return
    img = Image.open(png_path).convert('RGB')
    _, h, w, c = dims  # Assuming NHWC
    img = img.resize((w, h))
    img_array = np.array(img).astype(np.uint8)
    if img_array.shape != (h, w, c):
        raise ValueError(f"Unexpected shape: {img_array.shape}, expected {(h, w, c)}")
    img_array.tofile(raw_path)

def create_input_list(raw_path, list_path):
    if os.path.exists(list_path):
        print(f"[Skip] Input list already exists: {list_path}")
        return
    with open(list_path, 'w') as f:
        f.write(os.path.abspath(raw_path) + '\n')

def quantize_model(dlc_path, input_list_path, output_dlc_path):
    if os.path.exists(output_dlc_path):
        print(f"[Skip] Quantized DLC already exists: {output_dlc_path}")
        return
    run_command(f"snpe-dlc-quantize --input_dlc {dlc_path} --input_list {input_list_path} --output_dlc {output_dlc_path}")

def main():
    if len(sys.argv) != 4:
        print("Usage: python3 quantize.py <tflite_model_path> <image_path> <output_folder>")
        sys.exit(1)

    tflite_path = sys.argv[1]
    image_path = sys.argv[2]
    output_folder = sys.argv[3]

    os.makedirs(output_folder, exist_ok=True)

    prefix = os.path.join(output_folder, os.path.basename(tflite_path).replace(".tflite", "").replace("_float", ""))

    float_dlc = f"{prefix}_float.dlc"
    quant_dlc = f"{prefix}_quantized.dlc"
    csv_path = os.path.join(output_folder, "input_info.csv")

    convert_tflite_to_dlc(tflite_path, float_dlc)
    input_shape = extract_input_shape(float_dlc, csv_path)

    _, h, w, c = input_shape
    dim_str = f"{h}x{w}x{c}"
    raw_file = f"{prefix}_{dim_str}.raw"
    input_list = f"{prefix}_{dim_str}_input_list.txt"

    convert_image_to_raw(image_path, raw_file, input_shape)
    create_input_list(raw_file, input_list)
    quantize_model(float_dlc, input_list, quant_dlc)

    print(f"\nDone. Quantized DLC saved to: {quant_dlc}")

if __name__ == "__main__":
    main()
