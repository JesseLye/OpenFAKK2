"""Record what the default output device plays (WASAPI loopback) for a number of seconds.

    python sound_capture.py <out.npy> <seconds>

Saves float32 stereo at the device's rate (48 kHz here) as a numpy array. Needs the
pyaudiowpatch and numpy packages (pip install pyaudiowpatch numpy).
"""
import sys

import numpy as np
import pyaudiowpatch as pyaudio

out, secs = sys.argv[1], float(sys.argv[2])
p = pyaudio.PyAudio()
dev = p.get_default_wasapi_loopback()
rate, channels = int(dev["defaultSampleRate"]), dev["maxInputChannels"]
stream = p.open(format=pyaudio.paFloat32, channels=channels, rate=rate, input=True,
                input_device_index=dev["index"], frames_per_buffer=rate // 10)
frames = [np.frombuffer(stream.read(rate // 10, exception_on_overflow=False), dtype=np.float32)
          for _ in range(int(secs * 10))]
stream.close()
p.terminate()
data = np.concatenate(frames).reshape(-1, channels)[:, :2]
np.save(out, data)
print(f"{out}: {len(data) / rate:.1f} s at {rate} Hz from {dev['name']}")
