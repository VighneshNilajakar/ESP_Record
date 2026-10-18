"""Tests for tools/analyze_wav.py.

Standard library only, so CI needs no package installation.
"""
from __future__ import annotations

import io
import struct
import sys
import tempfile
import unittest
import wave
from contextlib import redirect_stdout, redirect_stderr
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))

import analyze_wav  # noqa: E402


def write_wav(path: Path, samples, rate: int = 16000, channels: int = 1, width: int = 2) -> Path:
    """Write a real PCM WAV using the standard library."""
    with wave.open(str(path), "wb") as handle:
        handle.setnchannels(channels)
        handle.setsampwidth(width)
        handle.setframerate(rate)
        handle.writeframes(b"".join(struct.pack("<h", s) for s in samples))
    return path


class AnalyzeWavTests(unittest.TestCase):
    def setUp(self) -> None:
        self._tmp = tempfile.TemporaryDirectory()
        self.dir = Path(self._tmp.name)
        self.addCleanup(self._tmp.cleanup)

    def run_analyze(self, path: Path) -> tuple[int, str]:
        buffer = io.StringIO()
        with redirect_stdout(buffer), redirect_stderr(buffer):
            code = analyze_wav.analyze(path, out=buffer)
        return code, buffer.getvalue()

    # ------------------------------------------------------------ structure

    def test_valid_non_silent_pcm16(self) -> None:
        path = write_wav(self.dir / "ok.wav", [1000, -1000, 2000, -2000] * 100)
        code, output = self.run_analyze(path)
        self.assertEqual(code, 0)
        self.assertIn("Channels: 1", output)
        self.assertIn("16000 Hz", output)

    def test_invalid_file_returns_error(self) -> None:
        path = self.dir / "broken.wav"
        path.write_bytes(b"not a wav")
        code, output = self.run_analyze(path)
        self.assertEqual(code, 2)
        self.assertIn("ERROR", output)

    def test_empty_file_returns_error(self) -> None:
        path = self.dir / "empty.wav"
        path.write_bytes(b"")
        code, _ = self.run_analyze(path)
        self.assertEqual(code, 2)

    def test_truncated_file_returns_error(self) -> None:
        path = write_wav(self.dir / "cut.wav", [100] * 50)
        data = path.read_bytes()
        path.write_bytes(data[:20])
        code, output = self.run_analyze(path)
        self.assertEqual(code, 2)
        self.assertIn("ERROR", output)

    # -------------------------------------------------------- RIFF header

    def test_header_fields_are_reported_and_consistent(self) -> None:
        path = write_wav(self.dir / "hdr.wav", [10, -10] * 200, rate=16000, channels=2)
        info = analyze_wav.read_riff_header(path)
        self.assertIsNone(info["error"])
        self.assertEqual(info["riff_id"], "RIFF")
        self.assertEqual(info["wave_id"], "WAVE")
        self.assertEqual(info["audio_format"], 1)
        self.assertEqual(info["channels"], 2)
        self.assertEqual(info["sample_rate"], 16000)
        self.assertEqual(info["bits"], 16)
        self.assertEqual(info["block_align"], 4)
        self.assertEqual(info["byte_rate"], 16000 * 4)
        self.assertEqual(info["riff_size"] + 8, path.stat().st_size)

        code, output = self.run_analyze(path)
        self.assertEqual(code, 0)
        self.assertNotIn("RIFF header declares", output)

    def test_lying_riff_size_is_reported(self) -> None:
        """A header that overstates the length is the signature of an
        interrupted save, and is exactly what the recorder's size check
        exists to prevent on the device."""
        path = write_wav(self.dir / "liar.wav", [5] * 100)
        data = bytearray(path.read_bytes())
        # Inflate the declared RIFF size by 4096 bytes.
        struct.pack_into("<I", data, 4, struct.unpack_from("<I", data, 4)[0] + 4096)
        path.write_bytes(bytes(data))

        code, output = self.run_analyze(path)
        self.assertEqual(code, 0)
        self.assertIn("RIFF header declares", output)
        self.assertIn("truncated", output)

    def test_trailing_bytes_are_reported(self) -> None:
        path = write_wav(self.dir / "extra.wav", [5] * 100)
        with path.open("ab") as handle:
            handle.write(b"\x00" * 32)
        code, output = self.run_analyze(path)
        self.assertEqual(code, 0)
        self.assertIn("trailing data", output)

    def test_non_pcm_format_is_rejected(self) -> None:
        path = write_wav(self.dir / "pcm.wav", [1] * 20)
        data = bytearray(path.read_bytes())
        struct.pack_into("<H", data, 20, 3)  # IEEE float
        path.write_bytes(bytes(data))
        code, output = self.run_analyze(path)
        self.assertEqual(code, 2)
        self.assertIn("not uncompressed PCM", output)

    def test_inconsistent_block_align_is_reported(self) -> None:
        path = write_wav(self.dir / "align.wav", [1] * 20)
        data = bytearray(path.read_bytes())
        struct.pack_into("<H", data, 32, 7)  # block_align
        path.write_bytes(bytes(data))
        code, output = self.run_analyze(path)
        self.assertEqual(code, 0)
        self.assertIn("block align", output)

    # ------------------------------------------------------------- signals

    def test_silence_warns_but_is_readable(self) -> None:
        path = write_wav(self.dir / "silence.wav", [0] * 1000)
        code, output = self.run_analyze(path)
        self.assertEqual(code, 0)
        self.assertIn("nearly silent", output)

    def test_clipping_is_reported(self) -> None:
        path = write_wav(self.dir / "loud.wav", [32767, -32768] * 200)
        code, output = self.run_analyze(path)
        self.assertEqual(code, 0)
        self.assertIn("near full scale", output)

    def test_dc_offset_is_reported(self) -> None:
        path = write_wav(self.dir / "dc.wav", [10000] * 500)
        code, output = self.run_analyze(path)
        self.assertEqual(code, 0)
        self.assertIn("DC offset", output)

    def test_non_16k_rate_is_flagged(self) -> None:
        path = write_wav(self.dir / "rate.wav", [100] * 200, rate=8000)
        code, output = self.run_analyze(path)
        self.assertEqual(code, 0)
        self.assertIn("sample rate is not 16 kHz", output)

    def test_window_rms_is_reported(self) -> None:
        path = write_wav(self.dir / "win.wav", [2000, -2000] * 400)
        code, output = self.run_analyze(path)
        self.assertEqual(code, 0)
        self.assertIn("window RMS", output)

    # ------------------------------------------------------- sample widths

    def test_24_bit_payload_is_analysed(self) -> None:
        path = self.dir / "w24.wav"
        with wave.open(str(path), "wb") as handle:
            handle.setnchannels(1)
            handle.setsampwidth(3)
            handle.setframerate(16000)
            # A 24-bit sample is the low three bytes of the 32-bit little-endian
            # encoding. To read back as 1000 on the 16-bit scale it holds 1000<<8.
            handle.writeframes(
                b"".join(struct.pack("<i", v << 8)[0:3] for v in [100, -100] * 100)
            )
        code, output = self.run_analyze(path)
        self.assertEqual(code, 0)
        self.assertIn("24 bits", output)
        self.assertIn("Peak: 100", output)

    def test_32_bit_payload_is_analysed(self) -> None:
        path = self.dir / "w32.wav"
        with wave.open(str(path), "wb") as handle:
            handle.setnchannels(1)
            handle.setsampwidth(4)
            handle.setframerate(16000)
            handle.writeframes(b"".join(struct.pack("<i", v << 16) for v in [100, -100] * 100))
        code, output = self.run_analyze(path)
        self.assertEqual(code, 0)
        self.assertIn("32 bits", output)
        self.assertIn("Peak: 100", output)

    def test_8_bit_payload_is_treated_as_unsigned(self) -> None:
        path = self.dir / "w8.wav"
        with wave.open(str(path), "wb") as handle:
            handle.setnchannels(1)
            handle.setsampwidth(1)
            handle.setframerate(16000)
            # 128 is the zero point for unsigned 8-bit PCM.
            handle.writeframes(bytes([128] * 100))
        code, output = self.run_analyze(path)
        self.assertEqual(code, 0)
        self.assertIn("8 bits", output)
        self.assertIn("nearly silent", output)


class SampleDecoderTests(unittest.TestCase):
    def test_decode_8_bit_is_centered(self) -> None:
        self.assertEqual(analyze_wav.decode_samples(bytes([128]), 1), [0])
        self.assertEqual(analyze_wav.decode_samples(bytes([255]), 1), [127 * 256])

    def test_decode_16_bit_signed(self) -> None:
        raw = struct.pack("<hh", -32768, 32767)
        self.assertEqual(analyze_wav.decode_samples(raw, 2), [-32768, 32767])

    def test_decode_24_bit_scaled_to_16(self) -> None:
        raw = struct.pack("<i", 1000 << 8)[0:3]
        self.assertEqual(analyze_wav.decode_samples(raw, 3), [1000])

    def test_decode_32_bit_scaled_to_16(self) -> None:
        raw = struct.pack("<i", -1000 << 16)
        self.assertEqual(analyze_wav.decode_samples(raw, 4), [-1000])


if __name__ == "__main__":
    unittest.main(verbosity=2)