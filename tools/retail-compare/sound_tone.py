"""Write the sound probes' test tone: 1 s of a 1 kHz sine at half scale, 22050 Hz mono
16-bit, a whole number of cycles so it loops without a click.

    python sound_tone.py <out.wav>
"""
import math
import struct
import sys
import wave

RATE = 22050
samples = [int(0.5 * 32767 * math.sin(2 * math.pi * 1000 * i / RATE)) for i in range(RATE)]
with wave.open(sys.argv[1], "wb") as w:
    w.setnchannels(1)
    w.setsampwidth(2)
    w.setframerate(RATE)
    w.writeframes(struct.pack(f"<{len(samples)}h", *samples))
