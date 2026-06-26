import wave
import struct
import math

sample_rate = 8000
num_samples = sample_rate * 4 # 4 seconds
freq = 440.0

with wave.open('music.wav', 'w') as w:
    w.setnchannels(1) # mono
    w.setsampwidth(1) # 8-bit
    w.setframerate(sample_rate)
    for i in range(num_samples):
        # Generate a square wave beep
        period = int(sample_rate / freq)
        value = 192 if (i % period) < (period // 2) else 64
        data = struct.pack('<B', value)
        w.writeframesraw(data)
