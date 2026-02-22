#!/usr/bin/env bash
# PostToolUse hook: validate .hlsl files with fxc.exe after Write/Edit
# Reads tool output JSON from stdin; exits 0 always (non-blocking).

input=$(cat)

fp=$(echo "$input" | python3 -c "
import sys, json
try:
    d = json.load(sys.stdin)
    print(d.get('filePath', d.get('file_path', d.get('path', ''))))
except Exception:
    print('')
" 2>/dev/null)

[[ "$fp" == *.hlsl ]] || exit 0

wfp=$(cygpath -w "$fp" 2>/dev/null)
[[ -z "$wfp" ]] && wfp="$fp"

FXC="C:\\Program Files (x86)\\Windows Kits\\10\\bin\\10.0.26100.0\\x64\\fxc.exe"

echo "--- HLSL validation: $fp ---"
powershell.exe -NoProfile -Command "& '$FXC' /T ps_5_0 /E main /nologo '$wfp'" 2>&1
status=$?

if [[ $status -eq 0 ]]; then
    echo "OK — shader compiled successfully."
else
    echo "HLSL compile error(s) in $fp (see above). Fix before committing."
fi

exit 0  # never block the edit; errors are informational
