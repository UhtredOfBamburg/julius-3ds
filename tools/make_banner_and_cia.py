#!/usr/bin/env python3
import math
import os
import struct
import sys
import wave
import zlib

def read_png(filename):
    with open(filename, 'rb') as f:
        data = f.read()
    assert data[:8] == b'\x89PNG\r\n\x1a\n'
    idx = 8
    idat = bytearray()
    palette = []
    width, height = 0, 0
    color_type = 0
    while idx < len(data):
        length, chunk_type = struct.unpack('>I4s', data[idx:idx+8])
        chunk_data = data[idx+8:idx+8+length]
        idx += 12 + length
        if chunk_type == b'IHDR':
            width, height, bit_depth, color_type, comp, filt, inter = struct.unpack('>IIBBBBB', chunk_data)
        elif chunk_type == b'PLTE':
            for i in range(0, len(chunk_data), 3):
                palette.append((chunk_data[i], chunk_data[i+1], chunk_data[i+2], 255))
        elif chunk_type == b'tRNS':
            for i, a in enumerate(chunk_data):
                if i < len(palette):
                    r, g, b, _ = palette[i]
                    palette[i] = (r, g, b, a)
        elif chunk_type == b'IDAT':
            idat.extend(chunk_data)
        elif chunk_type == b'IEND':
            break
            
    decomp = zlib.decompress(bytes(idat))
    pixels = []
    bpp = 1 if color_type == 3 else 4
    stride = 1 + width * bpp
    for y in range(height):
        line = decomp[y*stride + 1 : (y+1)*stride]
        row = []
        for x in range(width):
            if color_type == 3:
                idx_pal = line[x]
                row.append(palette[idx_pal] if idx_pal < len(palette) else (0,0,0,0))
            else:
                row.append((line[x*4], line[x*4+1], line[x*4+2], line[x*4+3]))
        pixels.append(row)
    return width, height, pixels

def save_png(filename, width, height, pixels):
    raw_data = bytearray()
    for y in range(height):
        raw_data.append(0)
        for x in range(width):
            r, g, b, a = pixels[y][x]
            raw_data.extend([r, g, b, a])
    compressed = zlib.compress(bytes(raw_data), 9)
    
    def make_chunk(chunk_type, data):
        c = chunk_type + data
        crc = zlib.crc32(c) & 0xffffffff
        return struct.pack('>I', len(data)) + c + struct.pack('>I', crc)
    
    png = b'\x89PNG\r\n\x1a\n'
    ihdr_data = struct.pack('>IIBBBBB', width, height, 8, 6, 0, 0, 0)
    png += make_chunk(b'IHDR', ihdr_data)
    png += make_chunk(b'IDAT', compressed)
    png += make_chunk(b'IEND', b'')
    with open(filename, 'wb') as f:
        f.write(png)

# Crisp 5x7 and 4x5 font glyphs
FONT_5X7 = {
    'A': [0x0E, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11],
    'B': [0x1E, 0x11, 0x11, 0x1E, 0x11, 0x11, 0x1E],
    'C': [0x0E, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0E],
    'D': [0x1C, 0x12, 0x11, 0x11, 0x11, 0x12, 0x1C],
    'E': [0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x1F],
    'F': [0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x10],
    'G': [0x0E, 0x11, 0x10, 0x17, 0x11, 0x11, 0x0F],
    'H': [0x11, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11],
    'I': [0x0E, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0E],
    'J': [0x07, 0x02, 0x02, 0x02, 0x02, 0x12, 0x0C],
    'K': [0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11],
    'L': [0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1F],
    'M': [0x11, 0x1B, 0x15, 0x15, 0x11, 0x11, 0x11],
    'N': [0x11, 0x19, 0x15, 0x13, 0x11, 0x11, 0x11],
    'O': [0x0E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E],
    'P': [0x1E, 0x11, 0x11, 0x1E, 0x10, 0x10, 0x10],
    'Q': [0x0E, 0x11, 0x11, 0x11, 0x15, 0x12, 0x0D],
    'R': [0x1E, 0x11, 0x11, 0x1E, 0x14, 0x12, 0x11],
    'S': [0x0E, 0x11, 0x10, 0x0E, 0x01, 0x11, 0x0E],
    'T': [0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04],
    'U': [0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E],
    'V': [0x11, 0x11, 0x11, 0x11, 0x11, 0x0A, 0x04],
    'W': [0x11, 0x11, 0x11, 0x15, 0x15, 0x1B, 0x11],
    'X': [0x11, 0x11, 0x0A, 0x04, 0x0A, 0x11, 0x11],
    'Y': [0x11, 0x11, 0x0A, 0x04, 0x04, 0x04, 0x04],
    'Z': [0x1F, 0x01, 0x02, 0x04, 0x08, 0x10, 0x1F],
    ' ': [0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00],
    '-': [0x00, 0x00, 0x00, 0x1F, 0x00, 0x00, 0x00],
    '.': [0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x04],
    ':': [0x00, 0x04, 0x00, 0x00, 0x00, 0x04, 0x00],
    '3': [0x1E, 0x01, 0x01, 0x0E, 0x01, 0x01, 0x1E],
    'D': [0x1C, 0x12, 0x11, 0x11, 0x11, 0x12, 0x1C],
    'S': [0x0E, 0x11, 0x10, 0x0E, 0x01, 0x11, 0x0E]
}

def draw_text(pixels, text, x_start, y_start, scale=1, fg=(255, 230, 110, 255), shadow=(40, 10, 10, 220)):
    cur_x = x_start
    for ch in text.upper():
        glyph = FONT_5X7.get(ch, FONT_5X7[' '])
        for row_idx, row_val in enumerate(glyph):
            for col_idx in range(5):
                if (row_val >> (4 - col_idx)) & 1:
                    # Draw shadow
                    if shadow:
                        for sy in range(scale):
                            for sx in range(scale):
                                px = cur_x + (col_idx * scale) + sx + 1
                                py = y_start + (row_idx * scale) + sy + 1
                                if 0 <= px < 256 and 0 <= py < 128:
                                    pixels[py][px] = shadow
                    # Draw fg
                    for sy in range(scale):
                        for sx in range(scale):
                            px = cur_x + (col_idx * scale) + sx
                            py = y_start + (row_idx * scale) + sy
                            if 0 <= px < 256 and 0 <= py < 128:
                                pixels[py][px] = fg
        cur_x += (5 + 1) * scale

def create_banner():
    bw, bh = 256, 128
    pixels = [[(45, 12, 14, 255) for _ in range(bw)] for _ in range(bh)]
    
    # 1. Imperial Roman Crimson Gradient Background with Vignette
    for y in range(bh):
        dy = (y - bh / 2.0) / (bh / 2.0)
        for x in range(bw):
            dx = (x - bw / 2.0) / (bw / 2.0)
            dist = math.sqrt(dx*dx + dy*dy)
            # Center is slightly brighter rich crimson, edges are deep imperial wine
            r = int(max(25, 75 - dist * 35))
            g = int(max(8, 18 - dist * 10))
            b = int(max(10, 22 - dist * 12))
            pixels[y][x] = (r, g, b, 255)

    # 2. Ornate Roman Gold Border
    gold_hi = (255, 230, 120, 255)
    gold_mid = (212, 168, 48, 255)
    gold_dk = (130, 95, 20, 255)
    
    for x in range(bw):
        pixels[2][x] = gold_hi
        pixels[3][x] = gold_mid
        pixels[4][x] = gold_dk
        pixels[bh - 5][x] = gold_hi
        pixels[bh - 4][x] = gold_mid
        pixels[bh - 3][x] = gold_dk
        
    for y in range(bh):
        pixels[y][2] = gold_hi
        pixels[y][3] = gold_mid
        pixels[y][4] = gold_dk
        pixels[y][bw - 5] = gold_hi
        pixels[y][bw - 4] = gold_mid
        pixels[y][bw - 3] = gold_dk

    # 3. Read and blend Julius Icon onto left side
    icon_w, icon_h, icon_pix = read_png('res/julius_256.png')
    target_size = 104
    offset_x = 12
    offset_y = 12
    for dy in range(target_size):
        sy = int((dy / float(target_size)) * icon_h)
        for dx in range(target_size):
            sx = int((dx / float(target_size)) * icon_w)
            src_r, src_g, src_b, src_a = icon_pix[sy][sx]
            if src_a > 10:
                alpha = src_a / 255.0
                dst_r, dst_g, dst_b, dst_a = pixels[offset_y + dy][offset_x + dx]
                out_r = int(src_r * alpha + dst_r * (1.0 - alpha))
                out_g = int(src_g * alpha + dst_g * (1.0 - alpha))
                out_b = int(src_b * alpha + dst_b * (1.0 - alpha))
                pixels[offset_y + dy][offset_x + dx] = (out_r, out_g, out_b, 255)

    # 4. Circular gold border around the coin
    cx = offset_x + target_size // 2
    cy = offset_y + target_size // 2
    radius = target_size // 2 + 1
    for angle_deg in range(360):
        rad = math.radians(angle_deg)
        px = int(cx + radius * math.cos(rad))
        py = int(cy + radius * math.sin(rad))
        if 0 <= px < bw and 0 <= py < bh:
            pixels[py][px] = gold_mid
        px2 = int(cx + (radius+1) * math.cos(rad))
        py2 = int(cy + (radius+1) * math.sin(rad))
        if 0 <= px2 < bw and 0 <= py2 < bh:
            pixels[py2][px2] = gold_dk

    # 5. Golden Lettering on the right side
    # "JULIUS" (scale 2, big bold)
    draw_text(pixels, "JULIUS", 130, 20, scale=2, fg=gold_hi, shadow=(30, 5, 5, 255))
    
    # Gold divider line
    for x in range(128, 246):
        pixels[42][x] = gold_mid
        pixels[43][x] = gold_dk

    # "CAESAR III" (scale 1)
    draw_text(pixels, "CAESAR III", 130, 52, scale=1, fg=(240, 235, 220, 255), shadow=(20, 5, 5, 255))
    
    # "DUAL-SCREEN"
    draw_text(pixels, "DUAL-SCREEN", 130, 70, scale=1, fg=gold_hi, shadow=(20, 5, 5, 255))

    # "NEW 3DS EDITION"
    draw_text(pixels, "NEW 3DS EDITION", 130, 88, scale=1, fg=(200, 185, 140, 255), shadow=(20, 5, 5, 255))

    # Save PNG
    os.makedirs('build-3ds', exist_ok=True)
    save_png('build-3ds/banner.png', bw, bh, pixels)
    print("Created banner image: build-3ds/banner.png")

def create_fanfare_audio():
    # 1.2-second majestic Roman brass fanfare (chords: C major -> G major triumph)
    rate = 22050
    duration = 1.2
    num_samples = int(rate * duration)
    with wave.open('build-3ds/banner.wav', 'w') as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(rate)
        samples = []
        for i in range(num_samples):
            t = i / float(rate)
            # Note 1: 0.0s to 0.4s (C5 + G4)
            # Note 2: 0.4s to 1.2s (E5 + C5 + G4 triumph with slow decay)
            if t < 0.35:
                freq1, freq2 = 523.25, 392.00 # C5, G4
                env = min(1.0, t / 0.05) * math.exp(-3.0 * t)
            else:
                t2 = t - 0.35
                freq1, freq2 = 659.25, 523.25 # E5, C5
                env = min(1.0, t2 / 0.05) * math.exp(-2.2 * t2)
            
            sig = (math.sin(2 * math.pi * freq1 * t) * 0.6 +
                   math.sin(2 * math.pi * freq2 * t) * 0.4 +
                   math.sin(2 * math.pi * freq1 * 2 * t) * 0.15) # rich brass harmonic
            val = int(12000 * env * sig)
            val = max(-32767, min(32767, val))
            samples.append(struct.pack('<h', val))
        w.writeframes(b''.join(samples))
    print("Created fanfare audio: build-3ds/banner.wav")

if __name__ == '__main__':
    create_banner()
    create_fanfare_audio()
