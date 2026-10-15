"""Record what the default output device plays (WASAPI loopback) for a number of seconds.

    python sound_capture.py <out.npy> <seconds>

Saves float32 stereo at the device's rate (48 kHz here) as a numpy array. Needs the
pyaudiowpatch and numpy packages (pip install pyaudiowpatch numpy). WASAPI loopback hands
over nothing while the device plays nothing, so a game that goes silent (the retail one
does when its window loses focus) would block the read for ever; the script plays
silence on the same device for as long as it records, which keeps the loopback running.
"""
import sys

import numpy as np
import pyaudiowpatch as pyaudio

out, secs = sys.argv[1], float(sys.argv[2])
p = pyaudio.PyAudio()
dev = p.get_default_wasapi_loopback()
rate, channels = int(dev["defaultSampleRate"]), dev["maxInputChannels"]
speakers = p.get_device_info_by_index(p.get_host_api_info_by_type(pyaudio.paWASAPI)["defaultOutputDevice"])
silence = p.open(format=pyaudio.paFloat32, channels=int(speakers["maxOutputChannels"]),
                 rate=int(speakers["defaultSampleRate"]), output=True, output_device_index=speakers["index"],
                 stream_callback=lambda data, count, info, status: (bytes(count * 4 * int(speakers["maxOutputChannels"])), pyaudio.paContinue))
stream = p.open(format=pyaudio.paFloat32, channels=channels, rate=rate, input=True,
                input_device_index=dev["index"], frames_per_buffer=rate // 10)
frames = [np.frombuffer(stream.read(rate // 10, exception_on_overflow=False), dtype=np.float32)
          for _ in range(int(secs * 10))]
stream.close()
silence.close()
p.terminate()
data = np.concatenate(frames).reshape(-1, channels)[:, :2]
np.save(out, data)
print(f"{out}: {len(data) / rate:.1f} s at {rate} Hz from {dev['name']}")
