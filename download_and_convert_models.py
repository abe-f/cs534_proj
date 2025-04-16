import os
import shutil

if "AI_Benchmark_V6.0.2.apk" not in os.listdir("."):
    os.system("wget http://data.vision.ee.ethz.ch/ihnatova/ai_benchmark/public/600/AI_Benchmark_V6.0.2.apk")

os.system("mkdir ai_benchmark")
os.system("cp AI_Benchmark_V6.0.2.apk ai_benchmark/AI_Benchmark_V6.0.2.apk")
os.system("mv ai_benchmark/AI_Benchmark_V6.0.2.apk ai_benchmark/AI_Benchmark_V6.0.2.zip")
os.system("cd ai_benchmark && unzip -o AI*")
os.system("mkdir models")

src_dir = "ai_benchmark/assets/models"
dst_root = "models"

for filename in os.listdir(src_dir):
    if filename.endswith("_float.tflite"):
        folder_name = filename.replace("_float.tflite", "")
        target_dir = os.path.join(dst_root, folder_name)
        if not os.path.exists(target_dir):
            os.makedirs(target_dir)
        shutil.copy(
            os.path.join(src_dir, filename),
            os.path.join(target_dir, filename)
        )

        #print(f"python3 quantize.py {src_dir}/{filename} data/chairs.png {target_dir}")
        os.system(f"python3 quantize.py {src_dir}/{filename} data/chairs.jpg {target_dir}")
