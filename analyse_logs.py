import numpy as np


bytes_transferred = 6021120

all_start_times = list()
yolo_runtimes = list()
transfer_times = list()
resnet_runtimes = list()

def process_files(file1_path, file2_path, file3_path, file4_path):
    with open(file1_path, 'r') as f1, open(file2_path, 'r') as f2, open(file3_path, 'r') as f3, open(file4_path, 'r') as f4:
        for line_num, (line1, line2, line3, line4) in enumerate(zip(f1, f2, f3, f4), start=1):
            try:
                val1 = float(line1.strip().split()[1])
                val2 = float(line2.strip().split()[1])
                val3 = float(line3.strip().split()[1])
                val4 = float(line4.strip().split()[1])

                all_start_times.append(val1)
                diff1 = val2 - val1
                yolo_runtimes.append(diff1)
                diff2 = val3 - val2
                transfer_times.append(diff2)
                diff3 = val4 - val3
                resnet_runtimes.append(diff3)

                print(f"Line {line_num}: Diff1 (1-2) = {diff1}, Diff2 (2-3) = {diff2}, Diff3 (3-4) = {diff3}")
            except ValueError:
                print(f"Line {line_num}: Error parsing line as float.")
            except Exception as e:
                print(f"Line {line_num}: Unexpected error - {e}")

# Example usage
port = 50051
process_files(f"yolo_start_{port}.txt", f"yolo_end_{port}.txt", f"resnet_start_{port}.txt", f"resnet_end_{port}.txt")

print("Average total for each request:", np.mean(yolo_runtimes)+np.mean(transfer_times)+np.mean(resnet_runtimes))
print("Average yolo runtime:", np.mean(yolo_runtimes))
print("Average transfer times:", np.mean(transfer_times))
print("Average resnet runtime:", np.mean(resnet_runtimes))
print("Throughput (req/s)", len(yolo_runtimes)*1.0/(all_start_times[-1]-all_start_times[0]))
print("Throughput (MB/s)", bytes_transferred*len(transfer_times)/np.sum(transfer_times)/1e6)
