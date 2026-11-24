#!/usr/bin/env python3
"""Regenerates the ALAC test fixtures in this directory.

Not run by the build: the encoded .m4a files are committed, so tests need no
encoder. Requires ffmpeg and, for the Apple-encoded files, macOS afconvert.

The PCM is a deterministic integer signal; tests/music_decoder_test.cpp
recomputes it with the same formula (signal_sample) and, ALAC being lossless,
requires every decoded sample to match exactly.
"""
import pathlib
import shutil
import struct
import subprocess
import sys
import tempfile

FRAMES = 9000  # two full 4096-frame packets plus a partial one


def mix32(x):
    x &= 0xFFFFFFFF
    x ^= x >> 13
    x = (x * 0x5BD1E995) & 0xFFFFFFFF
    x ^= x >> 15
    return x


def signal_sample(i, c, bits):
    """Digital silence at the start and in a gap (zero-run coding), triangle
    waves with a little noise, then full-scale noise for the last quarter
    (which the encoders store as uncompressed escape frames)."""
    if i < 600 or 3000 <= i < 3400:
        return 0
    if 3400 <= i < 3800:  # sparse clicks: short zero runs
        return (1 + c) if i % (5 + c) == 0 else 0
    amplitude = (1 << (bits - 1)) - 1
    noise = mix32(i * 2654435761 + c * 40503 + 12345)
    if i >= FRAMES * 3 // 4:
        return noise % (2 * amplitude + 1) - amplitude
    period = 97 + 31 * c
    t = i % period
    ramp = t if t < period // 2 else period - t
    tri = (ramp * 4 * amplitude) // (period * 2) - amplitude // 2
    return tri + noise % (amplitude >> 6) - (amplitude >> 7)


def write_wav(path, rate, channels, bits, container_bits=None):
    container_bits = container_bits or bits
    width = container_bits // 8
    frames = bytearray()
    for i in range(FRAMES):
        for c in range(channels):
            value = signal_sample(i, c, bits) << (container_bits - bits)
            frames += (value & ((1 << container_bits) - 1)).to_bytes(width, "little")
    header = b"RIFF" + struct.pack("<I", 36 + len(frames)) + b"WAVE"
    header += b"fmt " + struct.pack("<IHHIIHH", 16, 1, channels, rate, rate * channels * width,
                                    channels * width, container_bits)
    header += b"data" + struct.pack("<I", len(frames))
    path.write_bytes(header + frames)


def run(*args):
    subprocess.run(args, check=True)


def main():
    out = pathlib.Path(__file__).resolve().parent
    have_afconvert = shutil.which("afconvert") is not None
    with tempfile.TemporaryDirectory() as tmp:
        tmp = pathlib.Path(tmp)
        ff = ["ffmpeg", "-nostdin", "-hide_banner", "-loglevel", "error", "-y"]

        write_wav(tmp / "s16.wav", 44100, 2, 16)
        run(*ff, "-i", tmp / "s16.wav", "-c:a", "alac", out / "ffmpeg-stereo-16.m4a")

        write_wav(tmp / "mono.wav", 22050, 1, 16)
        run(*ff, "-i", tmp / "mono.wav", "-c:a", "alac", out / "ffmpeg-mono-16.m4a")

        write_wav(tmp / "s24.wav", 96000, 2, 24)
        run(*ff, "-i", tmp / "s24.wav", "-c:a", "alac", "-sample_fmt", "s32p", out / "ffmpeg-stereo-24.m4a")

        write_wav(tmp / "six.wav", 48000, 6, 16)
        run(*ff, "-i", tmp / "six.wav", "-c:a", "alac", out / "ffmpeg-5.1-16.m4a")

        if have_afconvert:
            run("afconvert", "-f", "m4af", "-d", "alac", tmp / "s16.wav", out / "apple-stereo-16.m4a")
            run("afconvert", "-f", "m4af", "-d", "alac", tmp / "s24.wav", out / "apple-stereo-24.m4a")
            write_wav(tmp / "s20.wav", 48000, 2, 20, container_bits=24)
            run("afconvert", "-f", "m4af", "-d", "alac/2", tmp / "s20.wav", out / "apple-stereo-20.m4a")
            # AAC in the same container must still be rejected, not misdecoded.
            run("afconvert", "-f", "m4af", "-d", "aac", "-b", "64000", tmp / "mono.wav", out / "apple-aac.m4a")
        else:
            print("afconvert not found: Apple-encoded fixtures not regenerated", file=sys.stderr)


if __name__ == "__main__":
    main()
