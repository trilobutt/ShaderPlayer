"""Unit tests for download.py. No network access: the Stripe call is
monkeypatched at the module level. Configuration is pointed at a temporary
directory before the module under test is imported, since it reads the
environment exactly once, at import time.
"""

from __future__ import annotations

import dataclasses
import os
import sys
import tempfile
import unittest
from pathlib import Path
from typing import Any, Callable

# Make `import download` resolve to the sibling module regardless of the
# working directory the test runner was invoked from.
sys.path.insert(0, str(Path(__file__).resolve().parent))

_TMPDIR = tempfile.TemporaryDirectory()
_TMP_PATH = Path(_TMPDIR.name)
_RELEASE_FILE = _TMP_PATH / "ShaderPlayerSetup.exe"
_RELEASE_FILE.write_bytes(b"fake installer bytes")
_DOWNLOAD_DB = _TMP_PATH / "downloads.sqlite3"

os.environ["STRIPE_SECRET_KEY"] = "sk_test_fake_secret"
os.environ["SHADERPLAYER_PRICE_ID"] = "price_test_123"
os.environ["SHADERPLAYER_RELEASE_FILE"] = str(_RELEASE_FILE)
os.environ["SHADERPLAYER_DOWNLOAD_DB"] = str(_DOWNLOAD_DB)
os.environ["SHADERPLAYER_MAX_DOWNLOADS"] = "3"

import download  # noqa: E402  (must follow the environment setup above)


def _valid_session(price_id: str = "price_test_123") -> dict[str, Any]:
    return {
        "status": "complete",
        "payment_status": "paid",
        "line_items": {"data": [{"price": {"id": price_id}}]},
    }


def _call_app(
    environ: dict[str, Any],
) -> tuple[str, list[tuple[str, str]], bytes]:
    captured: dict[str, Any] = {}

    def start_response(status: str, headers: list[tuple[str, str]]) -> None:
        captured["status"] = status
        captured["headers"] = headers

    body = b"".join(download.application(environ, start_response))
    return captured["status"], captured["headers"], body


def _make_environ(
    path: str, session_id: str | None, script_name: str = ""
) -> dict[str, Any]:
    """Build a WSGI environ for `path`, split at `script_name` the way a server
    mounting the app under a prefix would."""
    query_string = f"session_id={session_id}" if session_id is not None else ""
    assert path.startswith(script_name)
    return {
        "REQUEST_METHOD": "GET",
        "SCRIPT_NAME": script_name,
        "PATH_INFO": path[len(script_name) :],
        "QUERY_STRING": query_string,
        # A stand-in for the server-supplied wsgi.file_wrapper: reads the
        # file in chunks and closes it once exhausted, as a real one would.
        "wsgi.file_wrapper": _fake_file_wrapper,
    }


def _fake_file_wrapper(file_obj: Any, *_args: Any) -> Any:
    try:
        while True:
            chunk = file_obj.read(65536)
            if not chunk:
                return
            yield chunk
    finally:
        file_obj.close()


class SessionIdValidatorTests(unittest.TestCase):
    def test_accepts_real_shape(self) -> None:
        self.assertIsNotNone(
            download.SESSION_ID_RE.match("cs_test_a1B2c3D4e5F6g7H8i9J0")
        )

    def test_rejects_wrong_prefix(self) -> None:
        self.assertIsNone(
            download.SESSION_ID_RE.match("sk_test_a1B2c3D4e5F6g7H8i9J0")
        )

    def test_rejects_shell_metacharacter(self) -> None:
        self.assertIsNone(
            download.SESSION_ID_RE.match("cs_test_a1B2c3D4e5F6g7H8i9J0; rm -rf /")
        )

    def test_rejects_on_length(self) -> None:
        # Fewer than 10 characters after the "cs_" prefix.
        self.assertIsNone(download.SESSION_ID_RE.match("cs_short"))


class VerificationFlowTests(unittest.TestCase):
    def setUp(self) -> None:
        # Isolate this test's download counter from every other test's.
        db_path = _TMP_PATH / f"{self._testMethodName}.sqlite3"
        self._original_config = download.CONFIG
        download.CONFIG = dataclasses.replace(download.CONFIG, download_db=db_path)
        self._original_fetch = download._fetch_checkout_session

    def tearDown(self) -> None:
        download.CONFIG = self._original_config
        download._fetch_checkout_session = self._original_fetch

    def _patch_fetch(self, fetch: Callable[[str], dict[str, Any]]) -> None:
        download._fetch_checkout_session = fetch

    def test_paid_session_with_matching_price_is_allowed(self) -> None:
        self._patch_fetch(lambda session_id: _valid_session())
        environ = _make_environ("/api/download", "cs_test_" + "a" * 12)

        status, _headers, body = _call_app(environ)

        self.assertEqual(status, "200 OK")
        self.assertIn(b'"url"', body)
        self.assertIn(b"/api/download/file?session_id=", body)

    def test_price_id_mismatch_is_refused(self) -> None:
        self._patch_fetch(lambda session_id: _valid_session(price_id="price_other"))
        environ = _make_environ("/api/download", "cs_test_" + "a" * 12)

        status, _headers, _body = _call_app(environ)

        self.assertEqual(status, "403 Forbidden")

    def test_session_at_cap_is_refused(self) -> None:
        self._patch_fetch(lambda session_id: _valid_session())
        session_id = "cs_test_" + "b" * 12

        conn = download._connect_db()
        try:
            for _ in range(download.CONFIG.max_downloads):
                download._record_download(conn, session_id)
        finally:
            conn.close()

        environ = _make_environ("/api/download/file", session_id)
        status, _headers, _body = _call_app(environ)

        self.assertEqual(status, "403 Forbidden")

    def test_session_under_cap_streams_the_file(self) -> None:
        self._patch_fetch(lambda session_id: _valid_session())
        session_id = "cs_test_" + "c" * 12

        environ = _make_environ("/api/download/file", session_id)
        status, headers, body = _call_app(environ)

        self.assertEqual(status, "200 OK")
        header_dict = dict(headers)
        self.assertEqual(header_dict["Content-Type"], "application/octet-stream")
        self.assertIn("ShaderPlayerSetup.exe", header_dict["Content-Disposition"])
        self.assertEqual(body, b"fake installer bytes")

    def test_routes_under_an_api_prefix_mount(self) -> None:
        # Mounted at /api/ the server splits the path, so PATH_INFO alone is
        # "/download". The route has to be read from SCRIPT_NAME + PATH_INFO.
        self._patch_fetch(lambda session_id: _valid_session())
        environ = _make_environ(
            "/api/download", "cs_test_" + "f" * 12, script_name="/api"
        )

        status, _headers, body = _call_app(environ)

        self.assertEqual(status, "200 OK")
        self.assertIn(b"/api/download/file?session_id=", body)

    def test_unknown_path_is_404(self) -> None:
        environ = _make_environ("/api/nope", "cs_test_" + "d" * 12)
        status, _headers, _body = _call_app(environ)
        self.assertEqual(status, "404 Not Found")

    def test_non_get_method_is_404(self) -> None:
        environ = _make_environ("/api/download", "cs_test_" + "e" * 12)
        environ["REQUEST_METHOD"] = "POST"
        status, _headers, _body = _call_app(environ)
        self.assertEqual(status, "404 Not Found")


if __name__ == "__main__":
    unittest.main()
