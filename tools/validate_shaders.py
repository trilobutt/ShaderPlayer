#!/usr/bin/env python3
"""Compile ShaderPlayer HLSL shaders with the real injected preamble.

The in-app compiler never sees a shader file verbatim: ShaderManager prepends the
shared helper library (src/ShaderCommon.hlsli), an AudioConstants block when the
shader declares audio inputs, and a #define alias per ISF parameter. Running fxc
on the raw file therefore reports undeclared-identifier errors for every
parameter and is worthless as a check. This reproduces the preamble exactly.

Usage:
    python tools/validate_shaders.py                 # all of default_shaders/
    python tools/validate_shaders.py path/to/a.hlsl  # specific files
    python tools/validate_shaders.py --dump a.hlsl   # print the combined source

Exit code is non-zero if any shader fails to compile or overflows the cbuffer.
"""

from __future__ import annotations

import argparse
import json
import os
import subprocess
import sys
import tempfile
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
SHADER_COMMON = REPO_ROOT / "src" / "ShaderCommon.hlsli"
DEFAULT_SHADER_DIR = REPO_ROOT / "default_shaders"

# Must match kCustomFloats in src/Common.h (float4 custom[8]).
CUSTOM_FLOATS = 32

# Must match ShaderManager::BuildDefinesPreamble.
AUDIO_PREAMBLE = (
    "cbuffer AudioConstants : register(b1) {\n"
    "    float audioRms; float audioBass; float audioMid; float audioHigh;\n"
    "    float audioBeat; float audioSpectralCentroid; float audioPad0; float audioPad1;\n"
    "};\n"
    "Texture2D spectrumTexture : register(t3);\n"
)

BAND_MAP = {
    "rms": "audioRms",
    "bass": "audioBass",
    "mid": "audioMid",
    "high": "audioHigh",
    "beat": "audioBeat",
    "centroid": "audioSpectralCentroid",
}

SIZE_OF = {"float": 1, "bool": 1, "long": 1, "event": 1, "point2d": 2, "color": 4}

COMPONENTS = "xyzw"


class ShaderError(Exception):
    """Raised for problems that stop a shader being compiled at all."""


def find_fxc() -> Path:
    """Locate fxc.exe: $FXC, then the newest Windows 10 SDK x64 build tools."""
    env = os.environ.get("FXC")
    if env:
        p = Path(env)
        if not p.is_file():
            raise ShaderError(f"$FXC does not point at a file: {env}")
        return p

    for kit in (
        Path(r"C:\Program Files (x86)\Windows Kits\10\bin"),
        Path(r"C:\Program Files\Windows Kits\10\bin"),
    ):
        if not kit.is_dir():
            continue
        candidates = sorted(
            (d for d in kit.iterdir() if d.is_dir() and (d / "x64" / "fxc.exe").is_file()),
            key=lambda d: d.name,
        )
        if candidates:
            return candidates[-1] / "x64" / "fxc.exe"

    raise ShaderError(
        "fxc.exe not found. Install the Windows 10 SDK or set the FXC environment "
        "variable to its full path."
    )


def read_text(path: Path) -> str:
    """Read a shader as text. Files in the set are UTF-8; cp1252 is the fallback."""
    data = path.read_bytes()
    if data.startswith(b"\xef\xbb\xbf"):
        raise ShaderError("file starts with a UTF-8 BOM, which fxc rejects")
    try:
        return data.decode("utf-8")
    except UnicodeDecodeError:
        return data.decode("cp1252")


def parse_isf(source: str) -> tuple[list[dict], int, int]:
    """Mirror ShaderManager::ParseISFParams.

    Returns (params, packed_float_count, dropped_param_count). Each param is a
    dict with name/type/offset (offset is None for audio bands).
    """
    start = source.find("/*{")
    if start < 0:
        raise ShaderError("no ISF block (/*{ ... }*/) found")
    end = source.find("}*/", start)
    if end < 0:
        raise ShaderError("ISF block opened with /*{ but never closed with }*/")

    body = "{" + source[start + len("/*{"): end] + "}"
    try:
        block = json.loads(body)
    except json.JSONDecodeError as exc:
        # The C++ swallows this and returns an empty param list, which then makes
        # every parameter reference an undeclared identifier and the shader
        # vanishes from the library with no UI trace. Surface it loudly here.
        raise ShaderError(f"ISF block is not valid JSON: {exc}") from exc

    inputs = block.get("INPUTS")
    if inputs is None:
        return [], 0, 0
    if not isinstance(inputs, list):
        raise ShaderError('"INPUTS" is present but is not an array')

    params: list[dict] = []
    offset = 0
    dropped = 0

    for i, entry in enumerate(inputs):
        if not isinstance(entry, dict) or "NAME" not in entry or "TYPE" not in entry:
            continue
        name = entry["NAME"]
        type_ = entry["TYPE"]

        if type_ == "audio":
            band = entry.get("BAND", "rms")
            params.append({"name": name, "type": "audio", "band": band, "offset": None})
            continue

        if type_ not in SIZE_OF:
            continue  # Unknown type; the C++ skips it too.

        if type_ == "point2d" and offset % 2:
            offset += 1
        elif type_ == "color":
            while offset % 4:
                offset += 1

        size = SIZE_OF[type_]
        if offset + size > CUSTOM_FLOATS:
            # The C++ breaks out of the loop here, dropping this and every
            # remaining parameter.
            dropped = sum(
                1
                for e in inputs[i:]
                if isinstance(e, dict) and e.get("TYPE") in SIZE_OF
            )
            break

        params.append({"name": name, "type": type_, "offset": offset})
        offset += size

    return params, offset, dropped


def build_preamble(params: list[dict], helpers: str, source_name: str) -> str:
    """Mirror ShaderManager::BuildDefinesPreamble."""
    out = [helpers, "\n"]

    if any(p["type"] == "audio" for p in params):
        out.append(AUDIO_PREAMBLE)

    for p in params:
        if p["type"] == "audio":
            field = BAND_MAP.get(p["band"])
            if field:
                out.append(f"#define {p['name']} {field}\n")
            continue

        idx, comp = p["offset"] // 4, COMPONENTS[p["offset"] % 4]
        slot = f"custom[{idx}]."
        t = p["type"]
        if t in ("float", "event"):
            out.append(f"#define {p['name']} {slot}{comp}\n")
        elif t == "bool":
            out.append(f"#define {p['name']} ({slot}{comp} > 0.5)\n")
        elif t == "long":
            out.append(f"#define {p['name']} int({slot}{comp})\n")
        elif t == "point2d":
            nxt = COMPONENTS[p["offset"] % 4 + 1]
            out.append(f"#define {p['name']} float2({slot}{comp}, {slot}{nxt})\n")
        elif t == "color":
            out.append(f"#define {p['name']} custom[{idx}]\n")

    safe = source_name.replace('"', "").replace("\\", "/")
    out.append(f'#line 1 "{safe}"\n')
    return "".join(out)


def compile_source(fxc: Path, combined: str) -> tuple[bool, str, int]:
    """Run fxc over the combined source. Returns (ok, output, bytecode_size)."""
    tmpdir = Path(tempfile.mkdtemp(prefix="shaderplayer_val_"))
    try:
        src = tmpdir / "shader.hlsl"
        # UTF-8 without BOM: matches the bytes the runtime feeds to D3DCompile.
        src.write_text(combined, encoding="utf-8", newline="\n")

        # /O3 matches D3DCOMPILE_OPTIMIZATION_LEVEL3 in D3D11Renderer::CompilePixelShader.
        cmd = [str(fxc), "/T", "ps_5_0", "/E", "main", "/O3", "/nologo"]
        # /Fo is not optional: without an output file fxc dumps the whole assembly
        # listing to stdout and buries any diagnostics.
        out_path = tmpdir / "shader.cso"
        cmd += ["/Fo", str(out_path), str(src)]

        proc = subprocess.run(cmd, capture_output=True, text=True, errors="replace")
        text = (proc.stdout or "") + (proc.stderr or "")
        # fxc chatters about the object file on success; only diagnostics matter here.
        text = "\n".join(ln for ln in text.splitlines()
                         if "compilation object save succeeded" not in ln)
        size = out_path.stat().st_size if out_path.is_file() else 0
        return proc.returncode == 0, text.strip(), size
    finally:
        for f in tmpdir.iterdir():
            f.unlink(missing_ok=True)
        tmpdir.rmdir()


def validate(path: Path, fxc: Path, helpers: str, *, dump: bool, obj: bool) -> tuple[bool, int]:
    """Validate one shader. Returns (ok, bytecode_size)."""
    label = path.relative_to(REPO_ROOT) if path.is_relative_to(REPO_ROOT) else path

    try:
        source = read_text(path)
        params, packed, dropped = parse_isf(source)
    except ShaderError as exc:
        print(f"FAIL  {label}\n      {exc}")
        return False, 0

    name = str(label).replace("\\", "/")
    combined = build_preamble(params, helpers, name) + source

    if dump:
        print(combined)
        return True, 0

    ok, output, size = compile_source(fxc, combined)

    budget_ok = dropped == 0
    if not budget_ok:
        ok = False

    status = "ok  " if ok else "FAIL"
    detail = f"{packed:2d}/{CUSTOM_FLOATS} floats, {len(params)} params"
    if obj and size:
        detail += f", {size} bytes"
    print(f"{status}  {label}  ({detail})")

    if not budget_ok:
        print(
            f"      cbuffer overflow: {dropped} parameter(s) past the "
            f"{CUSTOM_FLOATS}-float budget are silently dropped at runtime"
        )
    if output:
        indent = "\n".join("      " + ln for ln in output.splitlines())
        print(indent)

    return ok, size


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("files", nargs="*", type=Path,
                    help="shader files to check (default: all of default_shaders/)")
    ap.add_argument("--dump", action="store_true",
                    help="print the combined preamble+source instead of compiling")
    ap.add_argument("--size", action="store_true",
                    help="report compiled bytecode size (for dead-strip checks)")
    args = ap.parse_args()

    try:
        fxc = find_fxc()
        helpers = SHADER_COMMON.read_text(encoding="utf-8")
    except (ShaderError, OSError) as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 2

    if args.files:
        targets = [p.resolve() for p in args.files]
    else:
        targets = sorted(DEFAULT_SHADER_DIR.glob("*.hlsl"))

    missing = [p for p in targets if not p.is_file()]
    if missing:
        for p in missing:
            print(f"error: no such file: {p}", file=sys.stderr)
        return 2
    if not targets:
        print("error: no shaders to validate", file=sys.stderr)
        return 2

    failures = 0
    for path in targets:
        ok, _ = validate(path, fxc, helpers, dump=args.dump, obj=args.size)
        if not ok:
            failures += 1

    if not args.dump:
        print(f"\n{len(targets) - failures}/{len(targets)} shaders compiled clean")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
