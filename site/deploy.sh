#!/usr/bin/env bash
#
# Deploy site/dist to an Opalstack static app.
#
#   ./site/deploy.sh --app marcsplained_shaderplayer
#   ./site/deploy.sh --app marcsplained_shaderplayer --no-build
#   ./site/deploy.sh --app marcsplained_shaderplayer --live      # drops noindex
#
# The app directory is a required argument with no default, and the script
# refuses to run unless that directory already exists on the server. The sync
# deletes remote files that are absent locally, so a mistyped name would
# otherwise erase a live site.
#
# Note on rsync: Git Bash ships no rsync, but the server has one. Rather than
# fall back to scp, which cannot remove stale files, the tree is shipped to a
# staging directory and the server's own rsync moves it into place with
# --delete. Same semantics, and the deletion is decided on the machine that
# holds both trees.
#
# .well-known/ is excluded from the delete: it is where an ACME client puts its
# challenge files, and wiping it mid-renewal breaks the certificate.

set -euo pipefail

readonly SSH_HOST="opal"
readonly SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
readonly DIST_DIR="${SCRIPT_DIR}/dist"

APP=""
BUILD=1
NOINDEX=1

die() { printf 'deploy: %s\n' "$*" >&2; exit 1; }

usage() {
    sed -n '3,18p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'
    exit "${1:-1}"
}

while [ $# -gt 0 ]; do
    case "$1" in
        --app)      APP="${2:-}"; shift 2 || die "--app needs a value" ;;
        --app=*)    APP="${1#*=}"; shift ;;
        --no-build) BUILD=0; shift ;;
        --live)     NOINDEX=0; shift ;;
        -h|--help)  usage 0 ;;
        *)          die "unknown argument: $1 (try --help)" ;;
    esac
done

# --- validate the target before touching anything ------------------------------

[ -n "$APP" ] || die "--app is required and has no default (e.g. --app marcsplained_shaderplayer)"

# An app name reaches a remote path, so it is validated as a bare name: no
# slashes, no dots, nothing that could climb out of ~/apps.
case "$APP" in
    *[!A-Za-z0-9_-]*) die "app name may contain only letters, digits, underscore and hyphen: '$APP'" ;;
esac

# The ABL Films site is not a target of this project under any circumstances.
case "$APP" in
    *abl2019*) die "refusing to deploy to '$APP': this script never touches abl2019" ;;
esac

readonly REMOTE_DIR="apps/${APP}"

# --- build ---------------------------------------------------------------------

if [ "$BUILD" -eq 1 ]; then
    if [ -x "${SCRIPT_DIR}/.venv/Scripts/python.exe" ]; then
        PY="${SCRIPT_DIR}/.venv/Scripts/python.exe"
    elif [ -x "${SCRIPT_DIR}/.venv/bin/python" ]; then
        PY="${SCRIPT_DIR}/.venv/bin/python"
    else
        die "no venv at ${SCRIPT_DIR}/.venv; create it and pip install -r site/requirements.txt"
    fi

    if [ "$NOINDEX" -eq 1 ]; then
        printf 'deploy: building (noindex)\n'
        "$PY" "${SCRIPT_DIR}/build.py"
    else
        printf 'deploy: building (LIVE, indexable)\n'
        "$PY" "${SCRIPT_DIR}/build.py" --no-noindex
    fi
fi

[ -d "$DIST_DIR" ] || die "no build output at ${DIST_DIR}"
[ -f "${DIST_DIR}/index.html" ] || die "${DIST_DIR} has no index.html; refusing to sync a partial tree"

# --- confirm the remote app exists ---------------------------------------------

printf 'deploy: checking ~/%s on %s\n' "$REMOTE_DIR" "$SSH_HOST"
ssh -o BatchMode=yes "$SSH_HOST" "test -d ~/${REMOTE_DIR}" \
    || die "~/${REMOTE_DIR} does not exist on ${SSH_HOST}. Create the app first; this script does not create one."

# --- ship, then sync into place -------------------------------------------------

STAGE="tmp/deploy-${APP}-$$"
cleanup() { ssh -o BatchMode=yes "$SSH_HOST" "rm -rf ~/${STAGE}" >/dev/null 2>&1 || true; }
trap cleanup EXIT

printf 'deploy: uploading %s files\n' "$(find "$DIST_DIR" -type f | wc -l | tr -d ' ')"
ssh -o BatchMode=yes "$SSH_HOST" "rm -rf ~/${STAGE} && mkdir -p ~/${STAGE}"
tar -czf - -C "$DIST_DIR" . | ssh -o BatchMode=yes "$SSH_HOST" "tar -xzf - -C ~/${STAGE}"

printf 'deploy: syncing into ~/%s\n' "$REMOTE_DIR"
ssh -o BatchMode=yes "$SSH_HOST" \
    "rsync -a --delete --exclude '.well-known/' --itemize-changes ~/${STAGE}/ ~/${REMOTE_DIR}/ | tail -20"

printf 'deploy: done\n'
