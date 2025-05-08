import torch
import sys
import io
import grpc
import tensor_stream_pb2
import tensor_stream_pb2_grpc
from torchvision.models import ResNet50_Weights
from torchvision import models, ops
from PIL import Image
import time
import numpy as np
from ultralytics import YOLO

# Initialize GPU
device = torch.device("cuda:0")
torch.backends.cudnn.benchmark = True

# Load models
yolo_model = YOLO('yolov8n.pt').to(device)

log_file = open(f"yolo_start_{sys.argv[1]}.txt", "w")
log_file.close()
log_file = open(f"yolo_end_{sys.argv[1]}.txt", "w")
log_file.close()

num_req = 0

# Preprocessing
def gpu_preprocess(image):
    arr = np.array(image).transpose(2, 0, 1)
    tensor = torch.from_numpy(arr).float().to(device) / 255.0
    return tensor.unsqueeze(0)

# Image
image = Image.open("leclerc.jpeg").convert("RGB")
gpu_image = gpu_preprocess(image)

# Constants
mean = torch.tensor([0.485, 0.456, 0.406], device=device).view(1, 3, 1, 1)
std = torch.tensor([0.229, 0.224, 0.225], device=device).view(1, 3, 1, 1)

# Simulate fixed number of synthetic ROIs
def generate_synthetic_boxes(num_boxes, height, width):
    boxes = []
    box_width, box_height = 100, 100
    for i in range(num_boxes):
        x1 = np.random.randint(0, width - box_width)
        y1 = np.random.randint(0, height - box_height)
        x2 = x1 + box_width
        y2 = y1 + box_height
        boxes.append([x1, y1, x2, y2])
    return torch.tensor(boxes, dtype=torch.float32, device=device)

CHUNK_SIZE = 1024 * 1024  # 1 MB

def generate_chunks(tensor):
    buffer = io.BytesIO()
    torch.save(tensor, buffer)
    buffer.seek(0)

    while True:
        chunk = buffer.read(CHUNK_SIZE)
        if not chunk:
            break
        yield tensor_stream_pb2.TensorChunk(
            chunk_data=chunk,
            is_last=buffer.tell() == buffer.getbuffer().nbytes
        )

def run():

    start_time = time.time()
    global num_req
    num_req += 1
    log_file = open(f"yolo_start_{sys.argv[1]}.txt", "a")
    log_file.write(f"{num_req} {start_time}\n")
    log_file.close()


    with torch.no_grad():
        _ = yolo_model(gpu_image)  # Just to simulate the pipeline step

    # Instead of using detected boxes, simulate them
    _, _, H, W = gpu_image.shape
    boxes = generate_synthetic_boxes(10, H, W)
    batch_indices = torch.zeros((boxes.shape[0], 1), device=device)
    rois = torch.cat([batch_indices, boxes], dim=1)

    torch.cuda.synchronize()
    transfer_start = time.perf_counter()

    # ROIAlign
    cropped = ops.roi_align(
        gpu_image,
        rois,
        output_size=(224, 224),
        spatial_scale=1.0,
        aligned=True
    )

    # Normalization
    normalized = (cropped - mean) / std

    with grpc.insecure_channel('localhost:50051') as channel:
        stub = tensor_stream_pb2_grpc.TensorServiceStub(channel)
        response_iterator = stub.StreamTensor(generate_chunks(normalized))

        log_file = open(f"yolo_end_{sys.argv[1]}.txt", "a")
        log_file.write(f"{num_req} {time.time()}\n")
        log_file.close()

        # Reassemble chunks
        output_buffer = io.BytesIO()
        for chunk in response_iterator:
            output_buffer.write(chunk.chunk_data)


        output_buffer.seek(0)
        received_tensor = torch.load(output_buffer)
        print("Client received tensor with shape:", received_tensor.shape, "at", time.time())

if __name__ == '__main__':
    for i in range(100):
        print(i)
        run()

