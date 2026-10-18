#!/usr/bin/env python3
"""Inspect PCM WAV format and signal levels using only the Python standard library.

Reports what the file actually contains and cross-checks the RIFF header fields
against the payload on disk. That cross-check is the useful part after a failed
or interrupted save: a WAV whose header promises more audio than the file holds
looks playable in some tools and silently truncated in others.

Exit codes: 0 readable (warnings may still be printed), 2 unreadable or
structurally invalid.
"""
from __future__ import annotations

import argparse
import math
import statistics
import struct
import sys
import wave
from pathlib import Path

# Value used to normalize every sample width onto one signed 16-bit scale.
FULL_SCALE = 32768


def read_riff_header(path: Path) -> dict:
    """Parse the 44-byte canonical PCM header without relying on `wave`.

    Returns a dict of the header fields, or a dict with ``error`` set when the
    bytes are not a RIFF/WAVE PCM file.
    """
    info: dict = {"error": None}
    try:
        with path.open("rb") as handle:
            raw = handle.read(12)
            if len(raw) < 12:
                info["error"] = "file is shorter than a RIFF header"
                return info
            riff, riff_size, wave_tag = struct.unpack("<4sI4s", raw)
            info["riff_id"] = riff.decode("latin-1")
            info["riff_size"] = riff_size
            info["wave_id"] = wave_tag.decode("latin-1")
            if riff != b"RIFF" or wave_tag != b"WAVE":
                info["error"] = "not a RIFF/WAVE file"
                return info

            fmt_chunk = handle.read(24)
            if len(fmt_chunk) < 24:
                info["error"] = "file is truncated inside the fmt chunk"
                return info
            (
                fmt_id,
                fmt_size,
                audio_format,
                channels,
                sample_rate,
                byte_rate,
                block_align,
                bits,
            ) = struct.unpack("<4sIHHIIHH", fmt_chunk)
            info["fmt_id"] = fmt_id.decode("latin-1")
            info["fmt_size"] = fmt_size
            info["audio_format"] = audio_format
            info["channels"] = channels
            info["sample_rate"] = sample_rate
            info["byte_rate"] = byte_rate
            info["block_align"] = block_align
            info["bits"] = bits

            data_chunk = handle.read(8)
            if len(data_chunk) < 8:
                info["error"] = "file is truncated before the data chunk"
                return info
            data_id, data_size = struct.unpack("<4sI", data_chunk)
            info["data_id"] = data_id.decode("latin-1")
            info["data_size"] = data_size
            info["data_offset"] = 44
    except OSError as exc:
        info["error"] = f"cannot read file: {exc}"
    return info


def check_riff(path: Path, out) -> bool:
    """Validate the header against the file on disk. Returns False if unusable."""
    info = read_riff_header(path)
    if info.get("error"):
        print(f"ERROR: {info['error']}", file=out)
        return False
    if info.get("audio_format") != 1:
        print(f"ERROR: audio format {info['audio_format']} is not uncompressed PCM.", file=out)
        return False

    try:
        file_size = path.stat().st_size
    except OSError as exc:
        print(f"ERROR: cannot stat file: {exc}", file=out)
        return False

    # RIFF size counts everything after the first 8 bytes.
    declared_total = info["riff_size"] + 8
    if declared_total != file_size:
        kind = "truncated" if declared_total > file_size else "has trailing data"
        print(
            f"WARNING: RIFF header declares {declared_total} bytes but the file is "
            f"{file_size} bytes ({kind}).",
            file=out,
        )

    payload_available = max(0, file_size - info["data_offset"])
    if info["data_size"] != payload_available:
        print(
            f"WARNING: data chunk declares {info['data_size']} bytes but "
            f"{payload_available} are present.",
            file=out,
        )

    expected_align = info["channels"] * (info["bits"] // 8)
    if info["block_align"] != expected_align:
        print(
            f"WARNING: block align is {info['block_align']} but channels x bytes "
            f"per sample is {expected_align}.",
            file=out,
        )

    if info["byte_rate"] != info["sample_rate"] * expected_align:
        print(
            f"WARNING: byte rate {info['byte_rate']} does not match sample rate x "
            f"block align ({info['sample_rate'] * expected_align}).",
            file=out,
        )

    return True


def decode_samples(raw: bytes, sample_width: int) -> list[int]:
    """Convert raw PCM bytes onto one signed 16-bit analysis scale."""
    if sample_width == 1:
        # 8-bit PCM in WAV is unsigned.
        return [(b - 128) << 8 for b in raw]

    values: list[int] = []
    count = len(raw) // sample_width
    for i in range(count):
        chunk = raw[i * sample_width:(i + 1) * sample_width]
        value = int.from_bytes(chunk, "little", signed=True)
        if sample_width == 3:
            value >>= 8
        elif sample_width == 4:
            value >>= 16
        values.append(value)
    return values


def analyze(path: Path, window_ms: int = 100, out=None) -> int:
    out = out if out is not None else sys.stdout
    path = Path(path)

    check_riff(path, out)

    try:
        with wave.open(str(path), "rb") as wav:
            channels = wav.getnchannels()
            sample_rate = wav.getframerate()
            sample_width = wav.getsampwidth()
            frames = wav.getnframes()
            compression = wav.getcomptype()
            raw = wav.readframes(frames)
    except (wave.Error, OSError, EOFError) as exc:
        print(f"ERROR: cannot read WAV: {exc}", file=out)
        return 2

    duration = frames / sample_rate if sample_rate else 0.0
    print(f"File: {path}", file=out)
    print(
        f"Channels: {channels}; sample rate: {sample_rate} Hz; "
        f"sample width: {sample_width * 8} bits",
        file=out,
    )
    print(
        f"Frames/channel: {frames}; duration: {duration:.3f} s; compression: {compression}",
        file=out,
    )

    if compression != "NONE":
        print("ERROR: only uncompressed PCM WAV is supported by this diagnostic.", file=out)
        return 2
    if sample_width not in (1, 2, 3, 4):
        print(f"ERROR: unsupported sample width: {sample_width}", file=out)
        return 2
    if not raw:
        print("ERROR: WAV payload is empty.", file=out)
        return 2

    bytes_per_frame = channels * sample_width
    expected_bytes = frames * bytes_per_frame
    if len(raw) != expected_bytes:
        print(
            f"WARNING: payload has {len(raw)} bytes; frame count declares {expected_bytes}.",
            file=out,
        )

    usable = len(raw) - (len(raw) % bytes_per_frame)
    values = decode_samples(raw[:usable], sample_width)
    if not values:
        print("ERROR: no complete sample frames in the payload.", file=out)
        return 2

    peak = max(abs(v) for v in values)
    rms = math.sqrt(sum(v * v for v in values) / len(values))
    dc = statistics.fmean(values)
    clipped = sum(1 for v in values if abs(v) >= FULL_SCALE - 1)
    clip_percent = 100.0 * clipped / len(values)

    print(
        f"Peak: {peak}; RMS: {rms:.2f}; RMS/full-scale: {rms / FULL_SCALE:.6f}", file=out
    )
    print(
        f"Approx. DC offset: {dc:.2f}; near-full-scale samples: {clipped}/{len(values)} "
        f"({clip_percent:.4f}%)",
        file=out,
    )

    frames_per_window = max(1, sample_rate * window_ms // 1000)
    window_rms: list[float] = []
    step = frames_per_window * channels
    for start in range(0, len(values), step):
        chunk = values[start:start + step]
        if not chunk:
            continue
        window_rms.append(math.sqrt(sum(v * v for v in chunk) / len(chunk)))
    if window_rms:
        print(
            f"{window_ms} ms window RMS: min={min(window_rms):.2f}, "
            f"median={statistics.median(window_rms):.2f}, max={max(window_rms):.2f}",
            file=out,
        )

    if peak <= 8 or rms <= 2:
        print(
            "WARNING: signal is nearly silent. Check DOUT pin, selected slot/channel, "
            "I2S alignment and gain.",
            file=out,
        )
    if clip_percent > 0.1:
        print(
            "WARNING: many samples are at/near full scale. Reduce digital gain or fix "
            "PCM scaling.",
            file=out,
        )
    if abs(dc) > FULL_SCALE * 0.05:
        print(
            "WARNING: large DC offset; inspect I2S conversion and microphone "
            "bias/data alignment.",
            file=out,
        )
    if sample_rate != 16000:
        print(
            "NOTE: sample rate is not 16 kHz; confirm that this matches the intended "
            "ML pipeline.",
            file=out,
        )
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("wav", type=Path, help="WAV file to inspect")
    parser.add_argument(
        "--window-ms", type=int, default=100, help="RMS window size in milliseconds (default: 100)"
    )
    args = parser.parse_args()
    if args.window_ms < 1:
        parser.error("--window-ms must be >= 1")
    return analyze(args.wav, args.window_ms)


if __name__ == "__main__":
    raise SystemExit(main())