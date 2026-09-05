"""WSGI application gating the ShaderPlayer installer behind a paid Stripe
Checkout session.

Two routes, default deny on everything else:

    GET /api/download?session_id=cs_...
        Verifies the session against Stripe and returns the streaming URL as
        JSON. Never touches the download counter.

    GET /api/download/file?session_id=cs_...
        Re-verifies the session, then increments the download counter and
        streams the installer.

The split exists because a browser navigating to the streaming URL is what
raises the save dialog; a `fetch()` against it would pull the whole 200 MB
body into memory first.

All configuration is read from the environment once, at import time. A
missing required variable raises immediately, naming itself, so a
misconfigured deployment fails at process start rather than on the first
request.
"""

from __future__ import annotations

import dataclasses
import json
import os
import re
import sqlite3
import sys
import urllib.error
import urllib.parse
import urllib.request
from datetime import datetime, timezone
from pathlib import Path
from typing import Any, Callable, Iterable

SESSION_ID_RE = re.compile(r"^cs_[A-Za-z0-9_]{10,200}$")
STRIPE_TIMEOUT_SECONDS = 10
DB_TIMEOUT_SECONDS = 5
STREAM_CHUNK_BYTES = 65536


def _require_env(name: str) -> str:
    """Read a required environment variable, raising with its name if absent."""
    value = os.environ.get(name)
    if not value:
        raise RuntimeError(f"missing required environment variable: {name}")
    return value


def _require_absolute_path_env(name: str) -> Path:
    path = Path(_require_env(name))
    if not path.is_absolute():
        raise RuntimeError(f"{name} must be an absolute path, got {path!s}")
    return path


@dataclasses.dataclass(frozen=True)
class Config:
    """Deployment configuration, resolved once from the environment."""

    stripe_secret_key: str
    price_id: str
    release_file: Path
    download_db: Path
    max_downloads: int


def _load_config() -> Config:
    max_downloads_raw = os.environ.get("SHADERPLAYER_MAX_DOWNLOADS", "5")
    try:
        max_downloads = int(max_downloads_raw)
    except ValueError as exc:
        raise RuntimeError(
            "SHADERPLAYER_MAX_DOWNLOADS must be an integer, "
            f"got {max_downloads_raw!r}"
        ) from exc

    return Config(
        stripe_secret_key=_require_env("STRIPE_SECRET_KEY"),
        price_id=_require_env("SHADERPLAYER_PRICE_ID"),
        release_file=_require_absolute_path_env("SHADERPLAYER_RELEASE_FILE"),
        download_db=_require_absolute_path_env("SHADERPLAYER_DOWNLOAD_DB"),
        max_downloads=max_downloads,
    )


CONFIG = _load_config()


class VerificationError(Exception):
    """A checkout session failed verification.

    The message is for the server log only; every caller turns this into a
    generic 403 body rather than passing the reason to the client.
    """


def _fetch_checkout_session(session_id: str) -> dict[str, Any]:
    """Fetch a Checkout Session from Stripe's REST API.

    Kept as a single narrow function (rather than inlined) so tests can
    monkeypatch it and exercise the rest of the module with no network
    access.
    """
    url = (
        "https://api.stripe.com/v1/checkout/sessions/"
        f"{urllib.parse.quote(session_id, safe='')}?expand[]=line_items"
    )
    request = urllib.request.Request(
        url,
        headers={"Authorization": f"Bearer {CONFIG.stripe_secret_key}"},
        method="GET",
    )
    with urllib.request.urlopen(request, timeout=STRIPE_TIMEOUT_SECONDS) as response:
        return json.loads(response.read().decode("utf-8"))


def _verify_session(session_id: str) -> None:
    """Raise VerificationError unless the session is a completed, paid
    purchase of SHADERPLAYER_PRICE_ID."""
    if not SESSION_ID_RE.match(session_id):
        raise VerificationError("malformed session id")

    try:
        session = _fetch_checkout_session(session_id)
    except urllib.error.HTTPError as exc:
        # The body is Stripe's own error detail; it is deliberately not read
        # or forwarded, only the status code is logged.
        raise VerificationError(f"stripe returned HTTP {exc.code}") from exc
    except urllib.error.URLError as exc:
        raise VerificationError(f"stripe request failed: {exc.reason}") from exc

    if session.get("status") != "complete":
        raise VerificationError("session status is not complete")
    if session.get("payment_status") != "paid":
        raise VerificationError("session payment_status is not paid")

    line_items = session.get("line_items", {}).get("data", [])
    matched = any(
        item.get("price", {}).get("id") == CONFIG.price_id for item in line_items
    )
    if not matched:
        raise VerificationError("no line item matches the configured price id")


def _connect_db() -> sqlite3.Connection:
    CONFIG.download_db.parent.mkdir(parents=True, exist_ok=True)
    conn = sqlite3.connect(str(CONFIG.download_db), timeout=DB_TIMEOUT_SECONDS)
    conn.execute(
        """
        CREATE TABLE IF NOT EXISTS downloads (
            session_id TEXT PRIMARY KEY,
            first_seen TEXT NOT NULL,
            count INTEGER NOT NULL
        )
        """
    )
    return conn


def _download_count(conn: sqlite3.Connection, session_id: str) -> int:
    row = conn.execute(
        "SELECT count FROM downloads WHERE session_id = ?", (session_id,)
    ).fetchone()
    return row[0] if row is not None else 0


def _record_download(conn: sqlite3.Connection, session_id: str) -> None:
    now = datetime.now(timezone.utc).isoformat()
    conn.execute(
        """
        INSERT INTO downloads (session_id, first_seen, count)
        VALUES (?, ?, 1)
        ON CONFLICT(session_id) DO UPDATE SET count = count + 1
        """,
        (session_id, now),
    )
    conn.commit()


def _log_failure(session_id: str, reason: str) -> None:
    """Log a denial to stderr. Never pass the secret key or a Stripe error
    body here — only the session id and a short internal reason."""
    print(f"download denied: session={session_id} reason={reason}", file=sys.stderr)


def _get_session_id(environ: dict[str, Any]) -> str | None:
    query = urllib.parse.parse_qs(environ.get("QUERY_STRING", ""))
    values = query.get("session_id")
    if not values:
        return None
    return values[0]


def _json_response(
    start_response: Callable[..., Any], status: str, payload: dict[str, Any]
) -> Iterable[bytes]:
    body = json.dumps(payload).encode("utf-8")
    start_response(
        status,
        [
            ("Content-Type", "application/json"),
            ("Content-Length", str(len(body))),
        ],
    )
    return [body]


def _plain_response(
    start_response: Callable[..., Any], status: str, message: str
) -> Iterable[bytes]:
    body = message.encode("utf-8")
    start_response(
        status,
        [
            ("Content-Type", "text/plain; charset=utf-8"),
            ("Content-Length", str(len(body))),
        ],
    )
    return [body]


def _forbidden(start_response: Callable[..., Any]) -> Iterable[bytes]:
    return _plain_response(start_response, "403 Forbidden", "forbidden")


def _not_found(start_response: Callable[..., Any]) -> Iterable[bytes]:
    return _plain_response(start_response, "404 Not Found", "not found")


def _iter_file(file_obj: Any) -> Iterable[bytes]:
    """Fallback for a server that offers no wsgi.file_wrapper."""
    try:
        while True:
            chunk = file_obj.read(STREAM_CHUNK_BYTES)
            if not chunk:
                return
            yield chunk
    finally:
        file_obj.close()


def _stream_file(environ: dict[str, Any], file_obj: Any) -> Iterable[bytes]:
    file_wrapper = environ.get("wsgi.file_wrapper")
    if file_wrapper is not None:
        return file_wrapper(file_obj, STREAM_CHUNK_BYTES)
    return _iter_file(file_obj)


def _handle_verify(
    environ: dict[str, Any], start_response: Callable[..., Any]
) -> Iterable[bytes]:
    session_id = _get_session_id(environ)
    if session_id is None or not SESSION_ID_RE.match(session_id):
        _log_failure(session_id or "<missing>", "malformed or missing session id")
        return _forbidden(start_response)

    try:
        _verify_session(session_id)
    except VerificationError as exc:
        _log_failure(session_id, str(exc))
        return _forbidden(start_response)

    url = f"/api/download/file?session_id={urllib.parse.quote(session_id, safe='')}"
    return _json_response(start_response, "200 OK", {"url": url})


def _handle_file(
    environ: dict[str, Any], start_response: Callable[..., Any]
) -> Iterable[bytes]:
    session_id = _get_session_id(environ)
    if session_id is None or not SESSION_ID_RE.match(session_id):
        _log_failure(session_id or "<missing>", "malformed or missing session id")
        return _forbidden(start_response)

    try:
        _verify_session(session_id)
    except VerificationError as exc:
        _log_failure(session_id, str(exc))
        return _forbidden(start_response)

    conn = _connect_db()
    try:
        if _download_count(conn, session_id) >= CONFIG.max_downloads:
            _log_failure(session_id, "download cap reached")
            return _forbidden(start_response)

        if not CONFIG.release_file.is_file():
            _log_failure(session_id, "release file missing on disk")
            return _plain_response(
                start_response, "500 Internal Server Error", "internal error"
            )

        _record_download(conn, session_id)
    finally:
        conn.close()

    size = os.stat(CONFIG.release_file).st_size
    file_obj = open(CONFIG.release_file, "rb")
    start_response(
        "200 OK",
        [
            ("Content-Type", "application/octet-stream"),
            (
                "Content-Disposition",
                f'attachment; filename="{CONFIG.release_file.name}"',
            ),
            ("Content-Length", str(size)),
        ],
    )
    return _stream_file(environ, file_obj)


def application(
    environ: dict[str, Any], start_response: Callable[..., Any]
) -> Iterable[bytes]:
    """The WSGI entry point. Default deny: any method other than GET, or
    any path other than the two routes below, is a 404."""
    if environ.get("REQUEST_METHOD", "") != "GET":
        return _not_found(start_response)

    # SCRIPT_NAME + PATH_INFO is the request's path however the app is mounted
    # (PEP 3333). Mounted at /api/ on the site, the server hands us
    # SCRIPT_NAME="/api" and PATH_INFO="/download"; given a host of its own it
    # hands us SCRIPT_NAME="" and PATH_INFO="/api/download". Routing on PATH_INFO
    # alone matches only the second and 404s the deployment we actually use.
    path = environ.get("SCRIPT_NAME", "") + environ.get("PATH_INFO", "")
    if path == "/api/download":
        return _handle_verify(environ, start_response)
    if path == "/api/download/file":
        return _handle_file(environ, start_response)

    return _not_found(start_response)
