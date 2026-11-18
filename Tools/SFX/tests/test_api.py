from __future__ import annotations

import email.message
import io
import json
import os
import tempfile
import unittest
import urllib.error
from typing import Any, List

import support
from lostwithin_sfx.api import ApiError, ApiKeyMissing, Client, SfxError, load_api_key, redact


def _http_error(code: int, body: Any, retry_after: str = "") -> urllib.error.HTTPError:
    headers = email.message.Message()
    if retry_after:
        headers["Retry-After"] = retry_after
    raw = body if isinstance(body, bytes) else json.dumps(body).encode("utf-8")
    return urllib.error.HTTPError("https://api.elevenlabs.io/x", code, "error", headers, io.BytesIO(raw))


class FakeOpener:
    def __init__(self, outcomes: List[Any]) -> None:
        self.outcomes = list(outcomes)
        self.requests: List[Any] = []

    def __call__(self, request: Any, timeout: float) -> support.FakeResponse:
        self.requests.append(request)
        outcome = self.outcomes.pop(0)
        if isinstance(outcome, Exception):
            raise outcome
        return support.FakeResponse(outcome)


class ClientTests(unittest.TestCase):
    def make(self, outcomes: List[Any]):
        opener = FakeOpener(outcomes)
        sleeps: List[float] = []
        client = Client(support.FAKE_KEY, opener=opener, sleep=sleeps.append)
        return client, opener, sleeps

    def generate(self, client: Client) -> bytes:
        return client.sound_generation("door creak", 1.5, 0.4, True, "eleven_text_to_sound_v3", "wav_48000")

    def test_sound_generation_request_shape(self) -> None:
        client, opener, _ = self.make([b"RIFFaudio"])
        self.assertEqual(self.generate(client), b"RIFFaudio")
        request = opener.requests[0]
        self.assertEqual(request.get_method(), "POST")
        self.assertEqual(request.full_url, "https://api.elevenlabs.io/v1/sound-generation?output_format=wav_48000")
        self.assertEqual(request.get_header("Xi-api-key"), support.FAKE_KEY)
        self.assertEqual(request.get_header("User-agent"), "LostWithin-SFX/1")
        self.assertEqual(json.loads(request.data.decode("utf-8")), {
            "text": "door creak", "duration_seconds": 1.5, "prompt_influence": 0.4,
            "loop": True, "model_id": "eleven_text_to_sound_v3"})

    def test_retries_429_honouring_retry_after(self) -> None:
        client, opener, sleeps = self.make([_http_error(429, {"detail": "busy"}, retry_after="3"), b"ok"])
        self.assertEqual(self.generate(client), b"ok")
        self.assertEqual(sleeps, [3.0])

    def test_gives_up_after_three_retries_on_5xx(self) -> None:
        client, opener, sleeps = self.make([_http_error(503, {"detail": "down"}) for _ in range(4)])
        with self.assertRaises(ApiError) as caught:
            self.generate(client)
        self.assertEqual(caught.exception.status, 503)
        self.assertEqual(sleeps, [2.0, 4.0, 8.0])

    def test_422_reports_validation_detail_without_retry(self) -> None:
        detail = {"detail": [{"loc": ["body", "duration_seconds"], "msg": "must be <= 30", "type": "value_error"}]}
        client, opener, sleeps = self.make([_http_error(422, detail)])
        with self.assertRaises(ApiError) as caught:
            self.generate(client)
        self.assertEqual(caught.exception.status, 422)
        self.assertIn("body.duration_seconds must be <= 30", str(caught.exception))
        self.assertEqual(sleeps, [])

    def test_401_reports_status_and_message(self) -> None:
        detail = {"detail": {"status": "missing_permissions", "message": "needs sound_generation"}}
        client, _, _ = self.make([_http_error(401, detail)])
        with self.assertRaises(ApiError) as caught:
            self.generate(client)
        self.assertIn("missing_permissions: needs sound_generation", str(caught.exception))

    def test_network_errors_retry_then_fail(self) -> None:
        client, _, sleeps = self.make([urllib.error.URLError("no route")] * 4)
        with self.assertRaises(SfxError) as caught:
            self.generate(client)
        self.assertIn("no route", str(caught.exception))
        self.assertEqual(len(sleeps), 3)

    def test_key_never_appears_in_errors_or_repr(self) -> None:
        echoed = {"detail": "invalid key " + support.FAKE_KEY}
        client, _, _ = self.make([_http_error(401, echoed)])
        with self.assertRaises(ApiError) as caught:
            self.generate(client)
        self.assertNotIn(support.FAKE_KEY, str(caught.exception))
        self.assertIn("[REDACTED]", str(caught.exception))
        self.assertNotIn(support.FAKE_KEY, repr(client))
        self.assertEqual(redact("x " + support.FAKE_KEY, support.FAKE_KEY), "x [REDACTED]")

    def test_subscription_parses_json(self) -> None:
        client, opener, _ = self.make([json.dumps({"character_count": 5}).encode("utf-8")])
        self.assertEqual(client.subscription()["character_count"], 5)
        self.assertEqual(opener.requests[0].get_method(), "GET")


class KeyLookupTests(unittest.TestCase):
    def test_environment_variable_wins(self) -> None:
        self.assertEqual(load_api_key({"ELEVENLABS_API_KEY": " abc \n", "LOCALAPPDATA": "/nowhere"}), "abc")

    def test_key_file_under_localappdata(self) -> None:
        with tempfile.TemporaryDirectory() as base:
            os.makedirs(os.path.join(base, "LostWithin"))
            with open(os.path.join(base, "LostWithin", "elevenlabs-api-key.txt"), "w", encoding="utf-8-sig") as handle:
                handle.write("from-file\r\n")
            self.assertEqual(load_api_key({"LOCALAPPDATA": base}), "from-file")

    def test_empty_key_file_is_reported_as_empty(self) -> None:
        with tempfile.TemporaryDirectory() as base:
            os.makedirs(os.path.join(base, "LostWithin"))
            open(os.path.join(base, "LostWithin", "elevenlabs-api-key.txt"), "w", encoding="utf-8").close()
            with self.assertRaises(ApiKeyMissing) as caught:
                load_api_key({"LOCALAPPDATA": base})
        self.assertIn("is empty", str(caught.exception))

    def test_missing_key_names_both_locations(self) -> None:
        with tempfile.TemporaryDirectory() as base:
            with self.assertRaises(ApiKeyMissing) as caught:
                load_api_key({"LOCALAPPDATA": base})
        message = str(caught.exception)
        self.assertIn("ELEVENLABS_API_KEY", message)
        self.assertIn("elevenlabs-api-key.txt", message)


if __name__ == "__main__":
    unittest.main()
