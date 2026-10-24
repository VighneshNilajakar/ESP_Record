"""Tests for tools/gen_config.py.

Standard library only, so CI needs no package installation.
"""
from __future__ import annotations

import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))

import gen_config  # noqa: E402


class GenConfigTests(unittest.TestCase):
    def setUp(self) -> None:
        self._tmp = tempfile.TemporaryDirectory()
        self.dir = Path(self._tmp.name)
        self.addCleanup(self._tmp.cleanup)

    def write_env(self, text: str, name: str = ".env") -> Path:
        path = self.dir / name
        path.write_text(text, encoding="utf-8")
        return path

    def resolve(self, text: str):
        path = self.write_env(text)
        values = gen_config.read_env(str(path))
        return gen_config.resolve(values, str(path))

    def resolve_ok(self, text: str) -> dict:
        rendered, _ = self.resolve(text)
        return dict(rendered)

    def test_example_env_roundtrip(self) -> None:
        path = ROOT / ".env.example"
        values = gen_config.read_env(str(path))
        rendered, warnings = gen_config.resolve(values, str(path))
        settings = dict(rendered)
        self.assertEqual(settings["ADR_RECORD_SECONDS"], "20UL")
        self.assertEqual(settings["ADR_SAMPLE_RATE_HZ"], "16000UL")
        self.assertEqual(settings["ADR_I2S_SLOT_RIGHT"], "1")
        self.assertEqual(settings["ADR_I2S_PCM_RIGHT_SHIFT"], "16")
        self.assertEqual(settings["ADR_PIN_BCLK"], "14")
        self.assertEqual(settings["ADR_PIN_WS"], "15")
        self.assertEqual(settings["ADR_PIN_DOUT"], "12")
        self.assertEqual(settings["ADR_PIN_LED"], "4")
        self.assertEqual(settings["ADR_SD_FREQUENCY_KHZ"], "4000")
        self.assertEqual(settings["ADR_SD_MOUNT_POINT"], '"/sdcard"')
        self.assertEqual(warnings, [])

    def test_inline_comments_and_quotes(self) -> None:
        settings = self.resolve_ok(
            'RECORD_SECONDS=5  # short clip\nSD_MOUNT_POINT="/sd"  # quoted\n'
        )
        self.assertEqual(settings["ADR_RECORD_SECONDS"], "5UL")
        self.assertEqual(settings["ADR_SD_MOUNT_POINT"], '"/sd"')

    def test_left_slot_maps_to_zero(self) -> None:
        self.assertEqual(self.resolve_ok("I2S_SLOT=left\n")["ADR_I2S_SLOT_RIGHT"], "0")
        self.assertEqual(self.resolve_ok("I2S_SLOT=RIGHT\n")["ADR_I2S_SLOT_RIGHT"], "1")

    def test_sd_clock_zero_means_core_default(self) -> None:
        self.assertEqual(self.resolve_ok("SD_FREQUENCY_KHZ=0\n")["ADR_SD_FREQUENCY_KHZ"], "0")

    def test_unknown_key_is_rejected(self) -> None:
        with self.assertRaises(gen_config.ConfigError) as caught:
            self.resolve("NOPE=1\n")
        self.assertIn("unknown key", str(caught.exception))

    def test_out_of_range_is_rejected(self) -> None:
        with self.assertRaises(gen_config.ConfigError):
            self.resolve("RECORD_SECONDS=0\n")
        with self.assertRaises(gen_config.ConfigError):
            self.resolve("RECORD_SECONDS=121\n")

    def test_duplicate_pin_is_rejected(self) -> None:
        with self.assertRaises(gen_config.ConfigError) as caught:
            self.resolve("PIN_BCLK=14\nPIN_WS=14\n")
        self.assertIn("both use GPIO14", str(caught.exception))

    def test_output_pin_camera_ranges(self) -> None:
        with self.assertRaises(gen_config.ConfigError):
            self.resolve("PIN_LED=34\n")

    def test_data_pin_may_be_input_only(self) -> None:
        self.assertEqual(self.resolve_ok("PIN_DOUT=35\n")["ADR_PIN_DOUT"], "35")

    def test_render_has_include_guard(self) -> None:
        rendered, _ = self.resolve("RECORD_SECONDS=5\n")
        text = gen_config.render(rendered, "x.env")
        self.assertIn("#ifndef ADR_GENERATED_CONFIG_H", text)
        self.assertIn("#define ADR_RECORD_SECONDS 5UL", text)


if __name__ == "__main__":
    unittest.main()
