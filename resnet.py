import torch
import sys
import io
import grpc
from concurrent import futures
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

log_file = open(f"resnet_start_{sys.argv[1]}.txt", "w")
log_file.close()
log_file = open(f"resnet_end_{sys.argv[1]}.txt", "w")
log_file.close()

# Load models
resnet_model = models.resnet50(weights=ResNet50_Weights.IMAGENET1K_V1).eval().to(device)

CHUNK_SIZE = 1024 * 1024  # 1 MB
num_req = 0

class TensorServiceServicer(tensor_stream_pb2_grpc.TensorServiceServicer):
    def StreamTensor(self, request_iterator, context):
        buffer = io.BytesIO()

        for chunk in request_iterator:
            buffer.write(chunk.chunk_data)
            if chunk.is_last:
                break

        buffer.seek(0)
        global num_req
        num_req += 1
        log_file = open(f"resnet_start_{sys.argv[1]}.txt", "a")
        log_file.write(f"{num_req} {time.time()}\n")
        log_file.close()

        tensor = torch.load(buffer)
        print("Server received tensor with shape:", tensor.shape, "at", time.time())
        # Count bytes transferred
        bytes_transferred = tensor.numel() * tensor.element_size()
        print("Bytes transferred:", bytes_transferred)

        processed_tensor = torch.ones(1)

        # Serialize and stream it back
        output_buffer = io.BytesIO()
        torch.save(processed_tensor, output_buffer)
        output_buffer.seek(0)

        while True:
            chunk = output_buffer.read(CHUNK_SIZE)
            if not chunk:
                break
            yield tensor_stream_pb2.TensorChunk(
                chunk_data=chunk,
                is_last=output_buffer.tell() == output_buffer.getbuffer().nbytes
            )

        # Pass to ResNet (not timed)
        with torch.no_grad():
            resnet_out = resnet_model(tensor)
            # print("Resnet output:", resnet_out)

        end_time = time.time()
        print("Finished ResNet at", end_time)

        log_file = open(f"resnet_end_{sys.argv[1]}.txt", "a")
        log_file.write(f"{num_req} {end_time}\n")
        log_file.close()

def serve():
    server = grpc.server(futures.ThreadPoolExecutor(max_workers=10))
    tensor_stream_pb2_grpc.add_TensorServiceServicer_to_server(TensorServiceServicer(), server)
    server.add_insecure_port('[::]:'+sys.argv[1])
    server.start()
    print("Server started on port", sys.argv[1])
    server.wait_for_termination()

if __name__ == '__main__':
    serve()

