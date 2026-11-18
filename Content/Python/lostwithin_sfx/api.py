"""ElevenLabs Sound Effects client (standard library only, no `unreal` import).

The API key is read on the workstation from the ELEVENLABS_API_KEY environment variable or from
%LOCALAPPDATA%\\LostWithin\\elevenlabs-api-key.txt. It is never logged, returned or written anywhere.
"""

from __future__ import annotations

import json
import os
import time
import urllib.error
import urllib.parse
import urllib.request
from typing import Any, Callable, Mapping, Optional

API_BASE = "https://api.elevenlabs.io"
USER_AGENT = "LostWithin-SFX/1"
KEY_ENV = "ELEVENLABS_API_KEY"
KEY_FILE_PARTS = ("LostWithin", "elevenlabs-api-key.txt")
RETRY_STATUSES = (429, 500, 502, 503, 504)
MAX_RETRY_DELAY = 30.0
REDACTED = "[REDACTED]"

Opener = Callable[..., Any]


class SfxError(Exception):
    """Any failure the tool reports to the user."""


class ApiKeyMissing(SfxError):
    pass


class ApiError(SfxError):
    def __init__(self, status: int, message: str) -> None:
        super().__init__("ElevenLabs API error {}: {}".format(status, message))
        self.status = status
        self.message = message


def default_key_file(environ: Optional[Mapping[str, str]] = None) -> Optional[str]:
    environ = os.environ if environ is None else environ
    base = environ.get("LOCALAPPDATA")
    if not base:
        return None
    return os.path.join(base, *KEY_FILE_PARTS)


def describe_key_locations(environ: Optional[Mapping[str, str]] = None) -> str:
    key_file = default_key_file(environ) or "%LOCALAPPDATA%\\" + "\\".join(KEY_FILE_PARTS)
    return "the {} environment variable or the file {}".format(KEY_ENV, key_file)


def load_api_key(environ: Optional[Mapping[str, str]] = None, key_file: Optional[str] = None) -> str:
    """Returns the key from the environment, else from the key file. Raises ApiKeyMissing."""
    environ = os.environ if environ is None else environ
    key = (environ.get(KEY_ENV) or "").strip()
    if key:
        return key
    path = key_file if key_file is not None else default_key_file(environ)
    if path and os.path.isfile(path):
        with open(path, "r", encoding="utf-8-sig") as handle:
            key = handle.read().strip()
        if key:
            return key
        raise ApiKeyMissing("The ElevenLabs key file {} is empty. Paste the key into it and save.".format(path))
    raise ApiKeyMissing("No ElevenLabs API key found. Put it in " + describe_key_locations(environ) + ".")


def redact(text: Any, key: Optional[str]) -> str:
    text = "" if text is None else str(text)
    if key:
        text = text.replace(key, REDACTED)
    return text


def format_error_detail(raw_body: bytes) -> str:
    """Turns an error body into one readable line (ElevenLabs uses `detail` as a string, object or list)."""
    text = raw_body.decode("utf-8", "replace")
    try:
        payload = json.loads(text)
    except ValueError:
        return text.strip()[:500] or "no details"
    detail = payload.get("detail", payload) if isinstance(payload, dict) else payload
    if isinstance(detail, str):
        return detail
    if isinstance(detail, dict):
        parts = [str(detail[k]) for k in ("status", "message") if detail.get(k)]
        return ": ".join(parts) if parts else json.dumps(detail)[:500]
    if isinstance(detail, list):
        lines = []
        for item in detail:
            if isinstance(item, dict):
                loc = ".".join(str(part) for part in item.get("loc", []))
                lines.append("{} {}".format(loc, item.get("msg", "")).strip())
            else:
                lines.append(str(item))
        return "; ".join(lines)
    return str(detail)[:500]


def _read_error_body(error: urllib.error.HTTPError) -> bytes:
    try:
        return error.read() or b""
    except OSError as read_error:
        # The status code is still reported; only the body text is lost.
        return "(error body unreadable: {})".format(read_error).encode("utf-8")
    finally:
        error.close()


class Client:
    def __init__(self, api_key: str, opener: Optional[Opener] = None, base_url: str = API_BASE,
                 sleep: Callable[[float], None] = time.sleep, max_retries: int = 3, timeout: float = 180.0) -> None:
        if not api_key:
            raise ApiKeyMissing("Empty ElevenLabs API key.")
        self._key = api_key
        self._opener = opener or urllib.request.urlopen
        self._base = base_url.rstrip("/")
        self._sleep = sleep
        self._max_retries = max_retries
        self._timeout = timeout

    def __repr__(self) -> str:
        return "Client(base_url={!r})".format(self._base)

    def sound_generation(self, text: str, duration_seconds: float, prompt_influence: float, loop: bool,
                         model_id: str, output_format: str) -> bytes:
        body = {
            "text": text,
            "duration_seconds": float(duration_seconds),
            "prompt_influence": float(prompt_influence),
            "loop": bool(loop),
            "model_id": model_id,
        }
        return self._request("POST", "/v1/sound-generation", {"output_format": output_format}, body)

    def subscription(self) -> dict:
        raw = self._request("GET", "/v1/user/subscription")
        return json.loads(raw.decode("utf-8"))

    def _request(self, method: str, path: str, query: Optional[Mapping[str, str]] = None,
                 body: Optional[Mapping[str, Any]] = None) -> bytes:
        url = self._base + path
        if query:
            url += "?" + urllib.parse.urlencode(query)
        data = json.dumps(body).encode("utf-8") if body is not None else None
        headers = {"xi-api-key": self._key, "User-Agent": USER_AGENT, "Accept": "*/*"}
        if data is not None:
            headers["Content-Type"] = "application/json"

        attempt = 0
        while True:
            request = urllib.request.Request(url, data=data, headers=headers, method=method)
            try:
                with self._opener(request, timeout=self._timeout) as response:
                    return response.read()
            except urllib.error.HTTPError as error:
                message = redact(format_error_detail(_read_error_body(error)), self._key)
                if error.code in RETRY_STATUSES and attempt < self._max_retries:
                    self._sleep(self.retry_delay(attempt, error.headers))
                    attempt += 1
                    continue
                raise ApiError(error.code, message) from None
            except urllib.error.URLError as error:
                if attempt < self._max_retries:
                    self._sleep(self.retry_delay(attempt, None))
                    attempt += 1
                    continue
                raise SfxError("Network error reaching ElevenLabs: " + redact(error.reason, self._key)) from None

    @staticmethod
    def retry_delay(attempt: int, headers: Optional[Mapping[str, str]]) -> float:
        value = headers.get("Retry-After") if headers is not None else None
        if value is not None:
            try:
                return min(MAX_RETRY_DELAY, max(0.0, float(value)))
            except ValueError:
                pass  # Retry-After may be an HTTP date; fall back to exponential backoff below.
        return float(2 ** (attempt + 1))
