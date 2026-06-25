import os

files = ['Error.wav', 'Intro.wav', 'Wifi finished.wav', 'buffer.wav', 'button.wav', 'leaderboard.wav']
output_file = 'sounds.h'

with open(output_file, 'w') as f:
    f.write('#pragma once\n\n')
    
    for filename in files:
        if not os.path.exists(filename):
            continue
            
        var_name = filename.replace('.wav', '').replace(' ', '_').lower() + '_wav'
        
        with open(filename, 'rb') as wav_file:
            data = wav_file.read()
            
        f.write(f'const unsigned char {var_name}[] PROGMEM = {{\n')
        
        # Write bytes in hex format
        for i, byte in enumerate(data):
            f.write(f'0x{byte:02x}, ')
            if (i + 1) % 16 == 0:
                f.write('\n')
                
        f.write('\n};\n')
        f.write(f'const unsigned int {var_name}_len = {len(data)};\n\n')

print("Successfully generated sounds.h")
