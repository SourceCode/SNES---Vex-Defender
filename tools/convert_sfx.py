"""
VEX DEFENDER - Sound Effect Conversion Pipeline
Converts source WAV files to SNES BRR format.

Pipeline:
  1. Load source WAV (stereo, 44.1kHz, 16-bit)
  2. Convert to mono (average channels)
  3. Downsample to 16000 Hz
  4. Trim leading silence
  5. Truncate to max duration (short SFX)
  6. Normalize amplitude
  7. Ensure sample count is multiple of 16 (BRR block alignment)
  8. Write 16-bit mono WAV at 16kHz
  9. Run snesbrr -e to produce .brr file

Usage:
  python tools/convert_sfx.py

Requires: snesbrr.exe in PVSnesLib tools path
"""

import wave
import struct
import os
import subprocess
import sys

# === Configuration ===
SRC_DIR = "C:/Users/Ryan Rentfro/Downloads/RawSounds"
OUT_DIR = "assets/sfx"
SNESBRR = "J:/code/snes/snes-build-tools/tools/pvsneslib/devkitsnes/tools/snesbrr.exe"
TARGET_RATE = 16000  # Target sample rate for BRR
SILENCE_THRESHOLD = 800  # Amplitude threshold for silence trimming

# === Sound mappings: source WAV -> output name, max duration (seconds) ===
MAPPINGS = [
    ("Zap.wav",          "player_shoot",   0.12),
    ("Zap02.wav",        "enemy_shoot",    0.12),
    ("explode.wav",      "explosion",      0.25),
    ("Hit01.wav",        "hit",            0.12),
    ("Bleep.wav",        "menu_select",    0.10),
    ("Bleep.wav",        "menu_move",      0.06),
    ("Bleep.wav",        "dialog_blip",    0.04),
    ("Test0001.wav",     "level_up",       0.40),
    ("ShipHit.wav",      "heal",           0.25),
]


def read_wav_mono(filepath):
    """Read a WAV file and return mono 16-bit samples at original rate."""
    w = wave.open(filepath, 'rb')
    nch = w.getnchannels()
    rate = w.getframerate()
    nframes = w.getnframes()
    raw = w.readframes(nframes)
    w.close()

    samples = struct.unpack('<' + 'h' * (nframes * nch), raw)

    # Convert to mono
    mono = []
    for i in range(0, len(samples), nch):
        s = 0
        for c in range(nch):
            s += samples[i + c]
        mono.append(s // nch)

    return mono, rate


def downsample(samples, src_rate, dst_rate):
    """Simple downsampling by nearest-neighbor."""
    ratio = src_rate / dst_rate
    out = []
    i = 0.0
    while int(i) < len(samples):
        out.append(samples[int(i)])
        i += ratio
    return out


def trim_silence(samples, threshold):
    """Remove leading silence below threshold."""
    start = 0
    for i, s in enumerate(samples):
        if abs(s) > threshold:
            start = i
            break
    # Small lead-in (16 samples = 1 BRR block)
    start = max(0, start - 16)
    return samples[start:]


def normalize(samples, target_peak=28000):
    """Normalize amplitude to target peak."""
    peak = max(abs(s) for s in samples) if samples else 1
    if peak == 0:
        return samples
    scale = target_peak / peak
    return [max(-32768, min(32767, int(s * scale))) for s in samples]


def align_brr(samples):
    """Pad to multiple of 16 samples (BRR block size)."""
    remainder = len(samples) % 16
    if remainder != 0:
        samples.extend([0] * (16 - remainder))
    return samples


def write_wav(filepath, samples, rate):
    """Write mono 16-bit WAV."""
    w = wave.open(filepath, 'wb')
    w.setnchannels(1)
    w.setsampwidth(2)
    w.setframerate(rate)
    raw = struct.pack('<' + 'h' * len(samples), *samples)
    w.writeframes(raw)
    w.close()


def convert_to_brr(wav_path, brr_path):
    """Run snesbrr to convert WAV to BRR."""
    result = subprocess.run(
        [SNESBRR, "-e", wav_path, brr_path],
        capture_output=True, text=True
    )
    if result.returncode != 0:
        print(f"  ERROR: snesbrr failed: {result.stderr}")
        return False
    return True


def main():
    os.makedirs(OUT_DIR, exist_ok=True)

    total_brr_size = 0
    success = 0

    for src_name, out_name, max_dur in MAPPINGS:
        src_path = os.path.join(SRC_DIR, src_name)
        wav_out = os.path.join(OUT_DIR, out_name + ".wav")
        brr_out = os.path.join(OUT_DIR, out_name + ".brr")

        print(f"Converting: {src_name} -> {out_name}")

        if not os.path.exists(src_path):
            print(f"  WARNING: Source not found: {src_path}")
            continue

        # Read and convert to mono
        mono, src_rate = read_wav_mono(src_path)
        print(f"  Source: {len(mono)} samples @ {src_rate}Hz")

        # Downsample
        resampled = downsample(mono, src_rate, TARGET_RATE)
        print(f"  Downsampled: {len(resampled)} samples @ {TARGET_RATE}Hz")

        # Trim leading silence
        trimmed = trim_silence(resampled, SILENCE_THRESHOLD)
        print(f"  After trim: {len(trimmed)} samples")

        # Truncate to max duration
        max_samples = int(max_dur * TARGET_RATE)
        truncated = trimmed[:max_samples]
        print(f"  Truncated to {max_dur}s: {len(truncated)} samples")

        # Normalize
        normalized = normalize(truncated)

        # Align to BRR blocks
        aligned = align_brr(list(normalized))
        print(f"  BRR-aligned: {len(aligned)} samples")

        # Write intermediate WAV
        write_wav(wav_out, aligned, TARGET_RATE)

        # Convert to BRR
        if convert_to_brr(wav_out, brr_out):
            brr_size = os.path.getsize(brr_out)
            total_brr_size += brr_size
            print(f"  BRR output: {brr_size} bytes")
            success += 1
        else:
            print(f"  FAILED to convert to BRR")

    print(f"\n=== Results ===")
    print(f"Converted: {success}/{len(MAPPINGS)}")
    print(f"Total BRR size: {total_brr_size} bytes ({total_brr_size/1024:.1f} KB)")
    print(f"SPC ARAM budget: ~6KB for SFX")


if __name__ == "__main__":
    main()
