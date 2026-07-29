#!/usr/bin/env bash
# PostToolUse hook: compile the edited shader the way ShaderPlayer actually does.
#
# Running fxc on the raw file reports undeclared-identifier errors for every ISF
# parameter, because the #define aliases and the shared helper library live in the
# injected preamble. tools/validate_shaders.py reproduces that preamble, so its
# output is trustworthy and its line numbers match the file on disk.
#
# Reads tool input JSON from stdin; exits 0 always (non-blocking).

input=$(cat)

fp=$(echo "$input" | python -c "
import sys, json
try:
    d = json.load(sys.stdin)
    ti = d.get('tool_input') or {}
    print(d.get('filePath') or d.get('file_path') or d.get('path')
          or ti.get('file_path') or ti.get('filePath') or ti.get('path') or '')
except Exception:
    print('')
" 2>/dev/null)

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"

case "$fp" in
    *.hlsl) ;;
    *.hlsli)
        echo "--- $fp changed: the shared helper library affects every shader."
        echo "    Run: python tools/validate_shaders.py"
        exit 0
        ;;
    *) exit 0 ;;
esac

echo "--- HLSL validation: $fp ---"
python "$root/tools/validate_shaders.py" "$fp" 2>&1

exit 0  # never block the edit; errors are informational
