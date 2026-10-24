#!/usr/bin/env python3
"""Generate config.h for the root sketch from a .env file.

The ESP32 has no filesystem to read a .env from at run time, so configuration is
resolved at build time. Copy .env.example to .env, edit it, run this script, then
compile. Without .env (or with the generated config.h deleted) the sketch falls
back to the reference-board defaults that are compiled into it.

The parser is deliberately strict: an unknown key or an out-of-range value is an
error, not a silent default, because a typo in a GPIO number is exactly the kind
of mistake that wastes a hardware session.
"""

import argparse
import os
import re
import sys

# Repo root is the parent of this script's directory, so the tool works from any
# working directory.
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DEFAULT_ENV = os.path.join(ROOT, ".env")
DEFAULT_OUT = os.path.join(ROOT, "config.h")

# key -> (macro, kind, options)
SPECS = {
    "RECORD_SECONDS": ("ADR_RECORD_SECONDS", "uint", {"min": 1, "max": 120}),
    "SAMPLE_RATE_HZ": ("ADR_SAMPLE_RATE_HZ", "uint", {"min": 8000, "max": 48000}),
    "SERIAL_BAUD": ("ADR_SERIAL_BAUD", "uint", {"min": 9600, "max": 2000000}),
    "I2S_SLOT": ("ADR_I2S_SLOT_RIGHT", "slot", {}),
    "PCM_RIGHT_SHIFT": ("ADR_I2S_PCM_RIGHT_SHIFT", "uint", {"min": 0, "max": 31}),
    "PIN_BCLK": ("ADR_PIN_BCLK", "pin", {}),
    "PIN_WS": ("ADR_PIN_WS", "pin", {}),
    "PIN_DOUT": ("ADR_PIN_DOUT", "pin", {}),
    "PIN_LED": ("ADR_PIN_LED", "pin", {}),
    "LED_ACTIVE_HIGH": ("ADR_LED_ACTIVE_HIGH", "bool", {}),
    "SD_FREQUENCY_KHZ": ("ADR_SD_FREQUENCY_KHZ", "sd", {}),
    "SD_MOUNT_POINT": ("ADR_SD_MOUNT_POINT", "string", {}),
}

INPUT_ONLY_PINS = range(34, 40)
FLASH_PINS = range(6, 12)


class ConfigError(Exception):
    pass


def _strip_inline_comment(value):
    """Drop ` # ...` comments without trimming a legitimate quoted value."""
    in_single = False
    in_double = False
    for i, char in enumerate(value):
        if char == "'" and not in_double:
            in_single = not in_single
        elif char == '"' and not in_single:
            in_double = not in_double
        elif char == "#" and not in_single and not in_double and i > 0 and value[i - 1].isspace():
            return value[:i].rstrip()
    return value


def _unquote(value):
    if len(value) >= 2 and value[0] == value[-1] and value[0] in ("'", '"'):
        return value[1:-1]
    return value


def read_env(path):
    """Return {key: (value, line_number)} or raise ConfigError."""
    if not os.path.exists(path):
        raise ConfigError("%s does not exist" % path)

    values = {}
    with open(path, "r", encoding="utf-8") as handle:
        for lineno, raw in enumerate(handle, 1):
            line = raw.strip()
            if not line or line.startswith("#"):
                continue
            if line.startswith("export "):
                line = line[len("export "):].lstrip()
            if "=" not in line:
                raise ConfigError(
                    "%s:%d: expected KEY=value, got %r" % (path, lineno, raw.rstrip())
                )
            key, value = line.split("=", 1)
            key = key.strip()
            value = _unquote(_strip_inline_comment(value.strip()))
            if not re.match(r"^[A-Z][A-Z0-9_]*$", key):
                raise ConfigError("%s:%d: invalid key %r" % (path, lineno, key))
            if key in values:
                raise ConfigError("%s:%d: %s is set twice" % (path, lineno, key))
            values[key] = (value, lineno)
    return values


def _parse_uint(key, value, lineno, path, minimum, maximum):
    try:
        if value.lower().startswith(("0x", "-0x")):
            number = int(value, 16)
        else:
            number = int(value, 10)
    except ValueError:
        raise ConfigError("%s:%d: %s must be an integer, got %r" % (path, lineno, key, value))
    if not minimum <= number <= maximum:
        raise ConfigError(
            "%s:%d: %s must be between %d and %d, got %d"
            % (path, lineno, key, minimum, maximum, number)
        )
    return number


def _parse_pin(key, value, lineno, path):
    pin = _parse_uint(key, value, lineno, path, 0, 39)
    # DOUT is an input to the ESP32, so an input-only pin is fine there.
    if key != "PIN_DOUT" and pin in INPUT_ONLY_PINS:
        raise ConfigError(
            "%s:%d: GPIO%d is input-only on the original ESP32 and cannot drive %s"
            % (path, lineno, pin, key[4:])
        )
    return pin


def resolve(values, path):
    """Validate every key and return an ordered list of (macro, rendered value)."""
    unknown = sorted(set(values) - set(SPECS))
    if unknown:
        raise ConfigError(
            "%s: unknown key(s): %s\nknown keys: %s"
            % (path, ", ".join(unknown), ", ".join(sorted(SPECS)))
        )

    warnings = []
    rendered = []
    pins = {}
    for key, (macro, kind, options) in SPECS.items():
        if key not in values:
            continue
        value, lineno = values[key]

        if kind == "uint":
            number = _parse_uint(key, value, lineno, path, options["min"], options["max"])
            suffix = "UL" if key != "PCM_RIGHT_SHIFT" else ""
            rendered.append((macro, "%d%s" % (number, suffix)))
        elif kind == "slot":
            lowered = value.lower()
            if lowered in ("right", "1"):
                rendered.append((macro, "1"))
            elif lowered in ("left", "0"):
                rendered.append((macro, "0"))
            else:
                raise ConfigError(
                    "%s:%d: I2S_SLOT must be left or right, got %r" % (path, lineno, value)
                )
        elif kind == "pin":
            pin = _parse_pin(key, value, lineno, path)
            if pin in FLASH_PINS:
                warnings.append(
                    "%s:%d: GPIO%d is usually connected to SPI flash; confirm the board"
                    % (path, lineno, pin)
                )
            pins[key] = (pin, lineno)
            rendered.append((macro, str(pin)))
        elif kind == "bool":
            lowered = value.lower()
            if lowered in ("1", "true", "yes", "on"):
                rendered.append((macro, "1"))
            elif lowered in ("0", "false", "no", "off"):
                rendered.append((macro, "0"))
            else:
                raise ConfigError(
                    "%s:%d: LED_ACTIVE_HIGH must be 0/1, got %r" % (path, lineno, value)
                )
        elif kind == "sd":
            if value.strip() == "0":
                rendered.append((macro, "0"))
            else:
                number = _parse_uint(key, value, lineno, path, 400, 40000)
                rendered.append((macro, str(number)))
        elif kind == "string":
            if not re.match(r"^/[A-Za-z0-9_./-]*$", value):
                raise ConfigError(
                    "%s:%d: SD_MOUNT_POINT must be an absolute path, got %r"
                    % (path, lineno, value)
                )
            rendered.append((macro, '"%s"' % value))

    seen = {}
    for key, (pin, lineno) in pins.items():
        if pin in seen:
            raise ConfigError(
                "%s:%d: %s and %s both use GPIO%d"
                % (path, lineno, seen[pin][0], key, pin)
            )
        seen[pin] = (key, lineno)

    return rendered, warnings


def render(rendered, env_path):
    lines = [
        "/* Generated by tools/gen_config.py from %s -- do not edit." % os.path.basename(env_path),
        " * Delete this file to return to the defaults compiled into the sketch. */",
        "#ifndef ADR_GENERATED_CONFIG_H",
        "#define ADR_GENERATED_CONFIG_H",
        "",
    ]
    for macro, value in rendered:
        lines.append("#define %s %s" % (macro, value))
    lines.append("")
    lines.append("#endif  // ADR_GENERATED_CONFIG_H")
    lines.append("")
    return "\n".join(lines)


def main(argv):
    parser = argparse.ArgumentParser(description="Generate config.h for the root sketch from .env")
    parser.add_argument("--env", default=DEFAULT_ENV, help="path to .env (default: repo root)")
    parser.add_argument("--out", default=DEFAULT_OUT, help="path to config.h (default: repo root)")
    args = parser.parse_args(argv)

    try:
        values = read_env(args.env)
        rendered, warnings = resolve(values, args.env)
    except ConfigError as error:
        print("gen_config: %s" % error, file=sys.stderr)
        return 2

    for warning in warnings:
        print("gen_config: warning: %s" % warning, file=sys.stderr)

    text = render(rendered, args.env)
    with open(args.out, "w", encoding="utf-8", newline="\n") as handle:
        handle.write(text)

    print("Wrote %s from %s (%d setting(s)):" % (args.out, args.env, len(rendered)))
    for macro, value in rendered:
        print("  %-24s %s" % (macro, value))
    print("Compile the root sketch again for the changes to take effect.")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
