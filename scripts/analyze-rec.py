#!/usr/bin/env python3
"""Checker for the framework capture tools (raw S16_LE, mono).

Reads what `audiorecord_test` / `audiotrack_test` share:
  devices/maple/kitkat/audio/audiorecord_test.cpp writes raw S16_LE mono
  (or a wav header gets skipped if one is present), the loopback recipe
  lives in docs/devices/maple/quirks.md TEST 50/TEST 51.

Prints the length/peak, then works on the loudest region: its dominant
frequency (a played 440 Hz tone should read 440.00) and the envelope
spectrum (sidebands around the carrier = stutter/underruns).

usage: python3 scripts/analyze-rec.py <capture> [rate]
needs numpy. Tested against the 2026-10-19 maple runs.
"""
import sys
import numpy as np
import wave

path = sys.argv[1]
fs = int(sys.argv[2]) if len(sys.argv) > 2 else 48000

try:
    w = wave.open(path, 'rb')
    x = np.frombuffer(w.readframes(w.getnframes()), dtype=np.int16).astype(np.float32)
    fs = w.getframerate()
except Exception as e:
    print("no wav header (%s), treating as raw S16_LE @ %d Hz" % (e, fs))
    raw = np.fromfile(path, dtype=np.uint8)
    x = np.frombuffer(raw[44:].tobytes(), dtype=np.int16).astype(np.float32)

print("samples %d  dur %.2fs  fs %d  peak %.0f" % (len(x), len(x)/fs, fs, np.abs(x).max()))

# RMS profile in 0.25 s blocks
blk = fs // 4
prof = np.array([np.sqrt((x[i*blk:(i+1)*blk]**2).mean()) for i in range(len(x)//blk)])
peak = prof.max()
# tone region = blocks above 30% of peak, take the longest run
runs, cur = [], None
for i, v in enumerate(prof):
    if v > 0.3*peak:
        cur = i if cur is None else cur
    else:
        if cur is not None:
            runs.append((cur, i-1)); cur = None
if cur is not None:
    runs.append((cur, len(prof)-1))
if not runs:
    print("no tone region found"); sys.exit(1)
a, b = max(runs, key=lambda r: r[1]-r[0])
t0, t1 = a*blk/fs, (b+1)*blk/fs
print("tone region %.2f..%.2fs" % (t0, t1))

seg = x[int(t0*fs):int(t1*fs)]
# dominant frequency
X = np.abs(np.fft.rfft(seg*np.hanning(len(seg))))
f = np.fft.rfftfreq(len(seg), 1/fs)
f0 = f[np.argmax(X)]
print("dominant tone: %.2f Hz" % f0)

# demodulate at f0, envelope spectrum
tt = np.arange(len(seg))/fs
z = seg*np.exp(-2j*np.pi*f0*tt)
k = 64
env = np.abs(np.convolve(z, np.ones(k)/k, mode='same'))
E = np.abs(np.fft.rfft((env - env.mean())*np.hanning(len(env))))
fe = np.fft.rfftfreq(len(env), 1/fs)
m = (fe > 1) & (fe < 500)
if m.any():
    order = np.argsort(E[m])[::-1]
    print("top modulation freqs (Hz, rel):")
    for i in order[:8]:
        print("  %8.2f Hz  %.3f" % (fe[m][i], E[m][i]/E[m].max()))
    # modulation depth: fundamental / (2*mean)
    fund = E[m][order[0]]
    print("mod depth (approx) %.2f" % (fund/(2*np.pi*max(env.mean(),1))))
