from PIL import Image
import numpy as np

def save_image_as_raw(input_image_path, output_raw_path):
    # Load and convert the image to RGB
    img = Image.open(input_image_path).convert('RGB')
    
    # Resize to 224x224
    img_resized = img.resize((224, 224))
    
    # Convert to numpy array
    img_array = np.array(img_resized, dtype=np.uint8)  # Shape: (224, 224, 3)
    
    # Save raw bytes
    img_array.tofile(output_raw_path)

    print(f"Saved .raw file: {output_raw_path}")

# Example usage
save_image_as_raw("chairs.jpg", "chairs.raw")