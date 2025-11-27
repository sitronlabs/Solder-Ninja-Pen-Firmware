#!/usr/bin/env python3
"""
Icon Generator Script for Solder Ninja Pen Firmware

This script generates icons.h in the gen/ folder from PNG files in res/icons/.
Each PNG file is converted to a 1-bit per pixel bitmap struct.

Usage:
    python scripts/icons.py
"""

import os
import sys
from pathlib import Path
import argparse
import struct
import zlib

# PlatformIO environment import (only available in PlatformIO context)
try:
    Import("env")
    PLATFORMIO_CONTEXT = True
except NameError:
    PLATFORMIO_CONTEXT = False

def read_png_file(filename):
    """Read PNG file and extract image data using only standard library."""
    with open(filename, 'rb') as f:
        # Read PNG signature
        signature = f.read(8)
        if signature != b'\x89PNG\r\n\x1a\n':
            raise ValueError("Not a valid PNG file")
        
        # Read chunks until we find IDAT
        image_data = b''
        width = height = 0
        bit_depth = 8
        color_type = 0
        compression = 0
        filter_method = 0
        interlace = 0
        
        while True:
            # Read chunk length
            length_data = f.read(4)
            if len(length_data) < 4:
                break
            length = struct.unpack('>I', length_data)[0]
            
            # Read chunk type
            chunk_type = f.read(4)
            
            # Read chunk data
            chunk_data = f.read(length)
            
            # Read CRC
            crc = f.read(4)
            
            if chunk_type == b'IHDR':
                # Parse IHDR chunk
                width = struct.unpack('>I', chunk_data[0:4])[0]
                height = struct.unpack('>I', chunk_data[4:8])[0]
                bit_depth = chunk_data[8]
                color_type = chunk_data[9]
                compression = chunk_data[10]
                filter_method = chunk_data[11]
                interlace = chunk_data[12]
                
            elif chunk_type == b'IDAT':
                # Collect image data
                image_data += chunk_data
                
            elif chunk_type == b'IEND':
                # End of image data
                break
        
        # Decompress image data
        try:
            decompressed_data = zlib.decompress(image_data)
        except zlib.error as e:
            raise ValueError(f"Failed to decompress image data: {e}")
        
        return width, height, bit_depth, color_type, decompressed_data

def apply_png_filter(filter_type, row_data, prev_row_data, bytes_per_pixel):
    """Apply PNG filter to decode row data."""
    if filter_type == 0:  # None
        return row_data
    elif filter_type == 1:  # Sub
        result = bytearray(row_data)
        for i in range(bytes_per_pixel, len(result)):
            result[i] = (result[i] + result[i - bytes_per_pixel]) % 256
        return bytes(result)
    elif filter_type == 2:  # Up
        if prev_row_data is None:
            return row_data
        result = bytearray(row_data)
        for i in range(len(result)):
            result[i] = (result[i] + prev_row_data[i]) % 256
        return bytes(result)
    elif filter_type == 3:  # Average
        result = bytearray(row_data)
        for i in range(len(result)):
            left = result[i - bytes_per_pixel] if i >= bytes_per_pixel else 0
            up = prev_row_data[i] if prev_row_data else 0
            result[i] = (result[i] + (left + up) // 2) % 256
        return bytes(result)
    elif filter_type == 4:  # Paeth
        # Simplified Paeth predictor
        result = bytearray(row_data)
        for i in range(len(result)):
            left = result[i - bytes_per_pixel] if i >= bytes_per_pixel else 0
            up = prev_row_data[i] if prev_row_data else 0
            up_left = prev_row_data[i - bytes_per_pixel] if prev_row_data and i >= bytes_per_pixel else 0
            
            # Paeth predictor
            p = left + up - up_left
            pa = abs(p - left)
            pb = abs(p - up)
            pc = abs(p - up_left)
            
            if pa <= pb and pa <= pc:
                predictor = left
            elif pb <= pc:
                predictor = up
            else:
                predictor = up_left
                
            result[i] = (result[i] + predictor) % 256
        return bytes(result)
    else:
        return row_data

def convert_to_bitmap(width, height, bit_depth, color_type, image_data, threshold=128):
    """Convert image data to 1-bit bitmap (horizontal bytes)."""
    bytes_per_pixel = 1 if color_type == 0 else 3 if color_type == 2 else 4 if color_type == 6 else 1
    
    # Calculate bytes per row (including filter byte)
    bytes_per_row = (width * bit_depth * bytes_per_pixel + 7) // 8 + 1
    
    bitmap_bytes = []
    prev_row_data = None
    
    for row in range(height):
        row_start = row * bytes_per_row
        filter_byte = image_data[row_start]
        row_data = image_data[row_start + 1:row_start + bytes_per_row]
        
        # Apply PNG filter
        decoded_row = apply_png_filter(filter_byte, row_data, prev_row_data, bytes_per_pixel)
        prev_row_data = decoded_row
        
        # Convert row to pixels based on color type
        if color_type == 0:  # Grayscale
            grayscale_pixels = list(decoded_row[:width])
        elif color_type == 2:  # RGB
            grayscale_pixels = []
            for i in range(0, min(len(decoded_row), width * 3), 3):
                if i + 2 < len(decoded_row):
                    r = decoded_row[i]
                    g = decoded_row[i + 1]
                    b = decoded_row[i + 2]
                    # Convert to grayscale using standard formula
                    gray = int(0.299 * r + 0.587 * g + 0.114 * b)
                    grayscale_pixels.append(gray)
        elif color_type == 6:  # RGBA
            grayscale_pixels = []
            for i in range(0, min(len(decoded_row), width * 4), 4):
                if i + 3 < len(decoded_row):
                    r = decoded_row[i]
                    g = decoded_row[i + 1]
                    b = decoded_row[i + 2]
                    a = decoded_row[i + 3]  # Alpha channel
                    # For transparent pixels (alpha=0), make them black (0)
                    # For opaque pixels, convert to grayscale using standard formula
                    if a == 0:
                        gray = 0
                    else:
                        gray = int(0.299 * r + 0.587 * g + 0.114 * b)
                    grayscale_pixels.append(gray)
        else:
            # For other color types, use raw data
            grayscale_pixels = list(decoded_row[:width])
        
        # Ensure we have the right number of pixels
        while len(grayscale_pixels) < width:
            grayscale_pixels.append(0)
        grayscale_pixels = grayscale_pixels[:width]
        
        # Apply threshold and convert to bits
        bit_pixels = [0 if pixel < threshold else 1 for pixel in grayscale_pixels]
        
        # Convert bits to bytes (8 bits per byte, MSB first)
        # Always generate 2 bytes per row (16 bits) regardless of actual width
        # This ensures all icons have the same format: 32 bytes total (16 rows × 2 bytes)
        
        for row_start in range(0, len(bit_pixels), width):
            row_bits = bit_pixels[row_start:row_start + width]
            
            # Pad row to 16 bits if needed
            while len(row_bits) < 16:
                row_bits.append(0)
            row_bits = row_bits[:16]  # Ensure exactly 16 bits
            
            # Convert to 2 bytes
            byte1 = 0
            byte2 = 0
            for i in range(8):
                if i < len(row_bits):
                    byte1 |= (row_bits[i] << (7 - i))
            for i in range(8, 16):
                if i < len(row_bits):
                    byte2 |= (row_bits[i] << (7 - (i - 8)))
            
            bitmap_bytes.append(byte1)
            bitmap_bytes.append(byte2)
    
    return bitmap_bytes

def process_png_file(image_path):
    """Process a single PNG file and return the bitmap data."""
    try:
        width, height, bit_depth, color_type, image_data = read_png_file(image_path)
        
        bitmap_bytes = convert_to_bitmap(width, height, bit_depth, color_type, image_data)
        
        print(f"Processing {Path(image_path).name}: {width}x{height}, {bit_depth}bpp, type {color_type}, {len(bitmap_bytes)} bytes")
        
        return bitmap_bytes, width, height
        
    except Exception as e:
        print(f"Error processing {Path(image_path).name}: {e}")
        return None, 0, 0

def generate_icons_header(icons_dir, output_path):
    """
    Generate the icons.h header file.
    
    Args:
        icons_dir: Directory containing PNG icon files
        output_path: Output path for the header file
    """
    icons_path = Path(icons_dir)
    png_files = list(icons_path.glob("*.png"))
    
    if not png_files:
        print(f"No PNG files found in {icons_dir}")
        return
    
    # Process each PNG file to create icon data
    icon_data = {}
    for png_file in sorted(png_files):
        icon_name = png_file.stem.lower()
        
        # Read PNG file and get dimensions
        try:
            width, height, bit_depth, color_type, image_data = read_png_file(png_file)
            
            # Convert to bitmap
            bitmap_data = convert_to_bitmap(width, height, bit_depth, color_type, image_data)
            
            # Print all info on one line
            print(f"Processing {png_file.name}: {width}x{height}, {bit_depth}bpp, type {color_type}, {len(bitmap_data)} bytes")
            
            icon_data[icon_name] = {
                'width': width,
                'height': height,
                'data': bitmap_data
            }
            
        except Exception as e:
            print(f"Error processing {png_file.name}: {e}")
            continue
    
    # Generate header content
    header_content = []
    header_content.append("#ifndef ICONS_H")
    header_content.append("#define ICONS_H")
    header_content.append("")
    header_content.append("/* C/C++ headers */")
    header_content.append("#include <stdint.h>")
    header_content.append("")
    header_content.append("/* Project headers */")
    header_content.append("#include \"interface/interface.h\"")
    header_content.append("")
    
    # Process each icon
    for icon_name, data in sorted(icon_data.items()):
        # Generate variable name and comment
        var_name = f"k_icon_{icon_name}"
        comment = f"/* {icon_name.replace('_', ' ').title()} */"
        
        # Format the struct
        struct_lines = []
        struct_lines.append(comment)
        struct_lines.append(f"const struct interface_icon {var_name} = {{")
        struct_lines.append(f"    {data['width']},")
        struct_lines.append(f"    {data['height']},")
        struct_lines.append("    {")
        
        # Format the data array (16 bytes per line)
        data_lines = []
        for i in range(0, len(data['data']), 16):
            line_data = data['data'][i:i+16]
            hex_values = [f"0x{byte:02X}" for byte in line_data]
            data_lines.append("        " + ", ".join(hex_values) + ",")
        
        struct_lines.extend(data_lines)
        struct_lines.append("    }")
        struct_lines.append("};")
        struct_lines.append("")
        
        header_content.extend(struct_lines)
    
    header_content.append("#endif /* ICONS_H */")
    header_content.append("")
    
    # Write the header file
    with open(output_path, 'w') as f:
        f.write('\n'.join(header_content))
    
    print(f"Generated {output_path} with {len(icon_data)} icons")

def main():
    """Main function."""
    # Use PlatformIO environment variables if available, otherwise use defaults
    if PLATFORMIO_CONTEXT:
        # Running in PlatformIO context
        project_dir = env.get("PROJECT_DIR", ".")
        icons_dir = os.path.join(project_dir, "res", "icons")
        output_path = os.path.join(project_dir, "gen", "icons.h")
    else:
        # Running standalone
        parser = argparse.ArgumentParser(description='Generate icons.h from PNG images in res/icons/')
        parser.add_argument('--icons-dir', default='res/icons', 
                           help='Directory containing PNG icon files (default: res/icons)')
        parser.add_argument('--output', default='gen/icons.h',
                           help='Output path for icons.h file (default: gen/icons.h)')
        
        args = parser.parse_args()
        icons_dir = args.icons_dir
        output_path = args.output
    
    # Check if icons directory exists
    if not Path(icons_dir).exists():
        print(f"Error: Icons directory '{icons_dir}' does not exist")
        sys.exit(1)
    
    # Generate the header file
    generate_icons_header(icons_dir, output_path)

# Execute the script when imported by PlatformIO
if PLATFORMIO_CONTEXT:
    main()

# Also allow standalone execution
if __name__ == "__main__":
    main()
