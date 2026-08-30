#!/usr/bin/env python3
"""Extract the shipped shader inventory into a JSON document the site builds from.

Reads every ``*.hlsl`` file in ``default_shaders/`` (or ``--shader-dir``), parses
its ISF JSON block exactly as ``ShaderManager::ParseISFParams`` does in
``src/ShaderManager.cpp``, and writes one JSON object per shader to
``site/content/shaders.json`` (or ``--out``). This is the single inventory the
site's generated reference pages (B5) are built from, so it must never drift
from what the app itself would parse: the packing rules, the ``MIN``/``MAX``/
``STEP`` ``is_number()`` gate, the ``long`` ``VALUES``/``LABELS`` pairing and the
``audio`` band mapping are all mirrored from the C++ function, not
reinterpreted.

Output shape::

    {
      "generated_from": "default_shaders",
      "shaders": [
        {
          "name": "colour_grading",
          "file": "colour_grading.hlsl",
          "title": "Colour Grading",
          "type": "video",
          "description": "...",
          "param_count": 14,
          "custom_floats": 14,
          "params": [
            {
              "name": "liftRed", "label": "Lift R", "type": "float",
              "default": 0.0, "min": -0.5, "max": 0.5, "step": 0.01
            },
            ...
          ]
        },
        ...
      ]
    }

A ``long`` param additionally carries ``values`` (ints) and ``labels``
(strings). An ``audio`` param additionally carries ``band`` and reports
``default``/``min``/``max``/``step`` as ``null``, since it consumes no
``custom[]`` slot and none of those fields apply to it.

Every problem found (missing/unparseable ISF block, empty ``DESCRIPTION``, a
packed float total above ``kCustomFloats``) is collected across all files and
reported together on stderr; the script exits 1 if any file failed.
"""

from __future__ import annotations

import argparse
import json
import sys
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any

REPO_ROOT = Path(__file__).resolve().parents[2]
DEFAULT_SHADER_DIR = REPO_ROOT / "default_shaders"
DEFAULT_OUT = REPO_ROOT / "site" / "content" / "shaders.json"

# Must match kCustomFloats in src/Common.h (float4 custom[8]).
CUSTOM_FLOATS = 32

# Field defaults on the C++ ShaderParam struct (src/Common.h), used whenever
# MIN/MAX/STEP is absent from the ISF block entirely.
DEFAULT_MIN = 0.0
DEFAULT_MAX = 1.0
DEFAULT_STEP = 0.01

# Small explicit acronym set for title-casing filenames. A clever heuristic
# would guess wrong on the next shader added; this list is checked by hand.
ACRONYMS = {"crt": "CRT", "rgb": "RGB", "dla": "DLA"}

# Words that stay lowercase inside a title, and stems whose display name is not
# recoverable from the underscores alone (a hyphenated surname, mostly). Both are
# hand-checked lists rather than heuristics, which would guess wrong on the next
# shader added.
LOWERCASE_WORDS = {"of", "the", "and", "in", "on", "to", "a", "an"}
TITLE_OVERRIDES = {
    "hele_shaw_fingering": "Hele-Shaw Fingering",
    "diffusion_limited_aggregation": "Diffusion-Limited Aggregation",
    "non_euclidean_lens": "Non-Euclidean Lens",
    "physarum_slime_mould": "Physarum Slime Mould",
}

OPEN_TAG = "/*{"
CLOSE_TAG = "}*/"


@dataclass
class ParamInfo:
    """One ISF INPUTS entry, packed the way ParseISFParams packs it."""

    name: str
    label: str
    type: str
    default: Any
    min: float | None
    max: float | None
    step: float | None
    values: list[int] | None = None
    labels: list[str] | None = None
    band: str | None = None

    def to_dict(self) -> dict[str, Any]:
        d: dict[str, Any] = {
            "name": self.name,
            "label": self.label,
            "type": self.type,
            "default": self.default,
            "min": self.min,
            "max": self.max,
            "step": self.step,
        }
        if self.values is not None:
            d["values"] = self.values
        if self.labels is not None:
            d["labels"] = self.labels
        if self.band is not None:
            d["band"] = self.band
        return d


@dataclass
class ShaderInfo:
    """One shader file's extracted inventory entry."""

    name: str
    file: str
    title: str
    type: str
    description: str
    params: list[ParamInfo] = field(default_factory=list)
    # The packing loop's own running offset, not a re-derived sum: alignment
    # padding (point2d to the next even slot, color to the next multiple of
    # four) consumes floats no param claims, so summing each param's own
    # size back out would silently drop that padding and undercount.
    custom_floats: int = 0

    def to_dict(self) -> dict[str, Any]:
        return {
            "name": self.name,
            "file": self.file,
            "title": self.title,
            "type": self.type,
            "description": self.description,
            "param_count": len(self.params),
            "custom_floats": self.custom_floats,
            "params": [p.to_dict() for p in self.params],
        }


class ExtractionError(Exception):
    """A single file's extraction failure; collected rather than raised."""


def titlecase_stem(stem: str) -> str:
    """Turn a filename stem into a display title, honouring ACRONYMS.

    ``rgb_parade`` -> ``RGB Parade``, ``game_of_life`` -> ``Game of Life``.
    Ordinary word-by-word capitalisation, not str.title(), so an apostrophe or
    an already-uppercase acronym token is not mangled.
    """
    if stem in TITLE_OVERRIDES:
        return TITLE_OVERRIDES[stem]
    words = []
    for index, word in enumerate(stem.split("_")):
        lower = word.lower()
        if lower in ACRONYMS:
            words.append(ACRONYMS[lower])
        elif index > 0 and lower in LOWERCASE_WORDS:
            words.append(lower)
        else:
            words.append(word[:1].upper() + word[1:].lower() if word else word)
    return " ".join(words)


def _extract_isf_block(source: str) -> str:
    """Return the raw JSON text between /*{ and }*/, or raise ExtractionError."""
    start = source.find(OPEN_TAG)
    if start == -1:
        raise ExtractionError("no /*{ ISF block found")
    end = source.find(CLOSE_TAG, start)
    if end == -1:
        raise ExtractionError("/*{ opened but no matching }*/ found")
    # Mirrors ShaderManager.cpp: wrap the interior in braces to recover a full object.
    return "{" + source[start + len(OPEN_TAG) : end] + "}"


def _bounded_number(
    input_obj: dict[str, Any], key: str, fallback: float | None
) -> float | None:
    """Mirror the is_number() gate in ParseISFParams for MIN/MAX/STEP.

    Present and a JSON number -> that value. Present but not a number (e.g. an
    array-form bound meant for a point2d/color param) -> null, since the app
    silently ignores it rather than coercing it, and the docs must not invent
    a bound the app never applies. Absent entirely -> the caller's fallback: the
    C++ struct default for the types drawn as a slider, since that genuinely is
    the bound the app applies, and None for the types that show no range at all.
    """
    if key not in input_obj:
        return fallback
    value = input_obj[key]
    if isinstance(value, bool) or not isinstance(value, (int, float)):
        return None
    return float(value)


def _parse_default(input_obj: dict[str, Any], type_: str) -> Any:
    """Mirror the DEFAULT parsing branch in ParseISFParams."""
    if type_ == "point2d":
        out = [0.0, 0.0]
    elif type_ == "color":
        out = [0.0, 0.0, 0.0, 0.0]
    elif type_ == "bool":
        out = False
    else:
        out = 0.0

    if "DEFAULT" not in input_obj:
        return out

    default = input_obj["DEFAULT"]
    if isinstance(default, list):
        n = min(len(default), 4)
        vals = [float(v) for v in default[:n]]
        if type_ == "point2d":
            vals += [0.0] * (2 - len(vals))
            return vals[:2]
        if type_ == "color":
            vals += [0.0] * (4 - len(vals))
            return vals[:4]
        return vals[0] if vals else out
    if isinstance(default, bool):
        return default if type_ == "bool" else (1.0 if default else 0.0)
    if isinstance(default, (int, float)):
        return bool(default) if type_ == "bool" else float(default)
    return out


def parse_shader(path: Path) -> ShaderInfo:
    """Parse one .hlsl file's ISF block, mirroring ParseISFParams exactly."""
    source = path.read_text(encoding="utf-8")
    json_text = _extract_isf_block(source)

    try:
        block = json.loads(json_text)
    except json.JSONDecodeError as exc:
        raise ExtractionError(f"ISF block is not valid JSON: {exc}") from exc

    if not isinstance(block, dict):
        raise ExtractionError("ISF block did not parse to a JSON object")

    description = str(block.get("DESCRIPTION", "")).strip()
    if not description:
        raise ExtractionError("DESCRIPTION is missing or empty")

    shader_type_raw = block.get("SHADER_TYPE", "")
    if shader_type_raw == "audio":
        shader_type = "audio"
    elif shader_type_raw == "generative":
        shader_type = "generative"
    else:
        shader_type = "video"

    raw_inputs = block.get("INPUTS", [])
    if not isinstance(raw_inputs, list):
        raw_inputs = []

    params: list[ParamInfo] = []
    offset = 0

    for entry in raw_inputs:
        if not isinstance(entry, dict) or "NAME" not in entry or "TYPE" not in entry:
            continue

        name = str(entry["NAME"])
        label = str(entry.get("LABEL", name))
        type_ = str(entry["TYPE"]).lower()

        if type_ not in ("float", "bool", "long", "color", "point2d", "event", "audio"):
            continue

        if type_ == "audio":
            band = str(entry.get("BAND", "rms"))
            params.append(
                ParamInfo(
                    name=name, label=label, type=type_,
                    default=None, min=None, max=None, step=None,
                    band=band,
                )
            )
            continue

        # long renders as a dropdown over VALUES, bool as a checkbox and event as
        # a button, so none of the three exposes a numeric range to the user. The
        # C++ struct defaults still sit on those params in memory, but publishing
        # them would document a "range 0 to 1" beside a dropdown of [240, 480,
        # 576, 720]. No shipped shader declares MIN/MAX on any of these types.
        ranged = type_ not in ("long", "bool", "event")
        p_min = _bounded_number(entry, "MIN", DEFAULT_MIN if ranged else None)
        p_max = _bounded_number(entry, "MAX", DEFAULT_MAX if ranged else None)
        p_step = _bounded_number(entry, "STEP", DEFAULT_STEP if ranged else None)
        p_default = _parse_default(entry, type_)

        if type_ == "long" and isinstance(p_default, float):
            p_default = int(p_default)

        values: list[int] | None = None
        labels: list[str] | None = None
        if type_ == "long" and "VALUES" in entry and isinstance(entry["VALUES"], list):
            values = [int(v) for v in entry["VALUES"]]
            if "LABELS" in entry and isinstance(entry["LABELS"], list):
                labels = [str(v) for v in entry["LABELS"]]
            else:
                labels = [str(v) for v in values]

        size = 2 if type_ == "point2d" else 4 if type_ == "color" else 1
        if type_ == "point2d" and offset % 2 != 0:
            offset += 1
        elif type_ == "color":
            while offset % 4 != 0:
                offset += 1

        if offset + size > CUSTOM_FLOATS:
            # Budget exhausted; ParseISFParams drops this and every remaining
            # INPUTS entry rather than emitting a UI for a param nothing reads.
            break

        offset += size
        params.append(
            ParamInfo(
                name=name, label=label, type=type_,
                default=p_default, min=p_min, max=p_max, step=p_step,
                values=values, labels=labels,
            )
        )

    if offset > CUSTOM_FLOATS:
        raise ExtractionError(f"packed custom[] total {offset} exceeds {CUSTOM_FLOATS}")

    return ShaderInfo(
        name=path.stem,
        file=path.name,
        title=titlecase_stem(path.stem),
        type=shader_type,
        description=description,
        params=params,
        custom_floats=offset,
    )


def extract_all(shader_dir: Path) -> tuple[list[ShaderInfo], list[str]]:
    """Parse every .hlsl file in shader_dir. Returns (shaders, error_lines)."""
    shaders: list[ShaderInfo] = []
    errors: list[str] = []

    for path in sorted(shader_dir.glob("*.hlsl"), key=lambda p: p.name):
        try:
            shaders.append(parse_shader(path))
        except ExtractionError as exc:
            errors.append(f"{path.name}: {exc}")

    return shaders, errors


def write_output(shaders: list[ShaderInfo], shader_dir: Path, out_path: Path) -> None:
    out_path.parent.mkdir(parents=True, exist_ok=True)
    document = {
        "generated_from": shader_dir.name,
        "shaders": [s.to_dict() for s in shaders],
    }
    text = json.dumps(document, indent=2) + "\n"
    out_path.write_text(text, encoding="utf-8")


def parse_args(argv: list[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--shader-dir",
        type=Path,
        default=DEFAULT_SHADER_DIR,
        help="directory of .hlsl files to extract (default: default_shaders/)",
    )
    parser.add_argument(
        "--out",
        type=Path,
        default=DEFAULT_OUT,
        help="output JSON path (default: site/content/shaders.json)",
    )
    return parser.parse_args(argv)


def main(argv: list[str] | None = None) -> int:
    args = parse_args(argv)

    if not args.shader_dir.is_dir():
        print(f"error: no such directory: {args.shader_dir}", file=sys.stderr)
        return 1

    shaders, errors = extract_all(args.shader_dir)

    if errors:
        print(f"{len(errors)} shader(s) failed extraction:", file=sys.stderr)
        for line in errors:
            print(f"  {line}", file=sys.stderr)
        return 1

    write_output(shaders, args.shader_dir, args.out)

    counts: dict[str, int] = {}
    for s in shaders:
        counts[s.type] = counts.get(s.type, 0) + 1
    breakdown = ", ".join(f"{k}={v}" for k, v in sorted(counts.items()))
    print(f"wrote {len(shaders)} shaders to {args.out} ({breakdown})")
    return 0


if __name__ == "__main__":
    sys.exit(main())
