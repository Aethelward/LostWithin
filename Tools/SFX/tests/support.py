"""Shared test helpers: puts Content/Python on sys.path and provides fakes for the editor and the API."""

from __future__ import annotations

import io
import json
import os
import sys
import tempfile
from typing import Any, Dict, List, Optional

PROJECT_DIR = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", ".."))
PYTHON_DIR = os.path.join(PROJECT_DIR, "Content", "Python")
MANIFEST_FILE = os.path.join(PROJECT_DIR, "Tools", "SFX", "sfx-manifest.json")
if PYTHON_DIR not in sys.path:
    sys.path.insert(0, PYTHON_DIR)

from lostwithin_sfx.wav import build_wav  # noqa: E402

FAKE_KEY = "sk_test_0123456789abcdef_secret"


def silence_wav(seconds: float, rate: int = 48000) -> bytes:
    return build_wav(b"\x00\x00" * int(rate * seconds), rate)


def write_manifest(data: Dict[str, Any]) -> str:
    handle = tempfile.NamedTemporaryFile("w", suffix=".json", delete=False, encoding="utf-8")
    with handle:
        json.dump(data, handle)
    return handle.name


def small_manifest(cap: int = 3000) -> Dict[str, Any]:
    return {
        "version": 1,
        "max_credits_per_run": cap,
        "defaults": {"model_id": "eleven_text_to_sound_v3", "output_format": "wav_48000",
                     "prompt_influence": 0.4, "variations": 1},
        "sounds": [
            {"id": "a", "prompt": "a sound", "duration": 2.0, "variations": 2, "folder": "/Game/T", "name": "SFX_A"},
            {"id": "b", "prompt": "b sound", "duration": 1.0, "folder": "/Game/T", "name": "SFX_B"},
            {"id": "c", "prompt": "c loop", "duration": 10.0, "loop": True, "folder": "/Game/T", "name": "AMB_C"},
        ],
    }


class FakeProgress:
    def __init__(self, backend: "FakeBackend") -> None:
        self.backend = backend

    def __enter__(self) -> "FakeProgress":
        return self

    def __exit__(self, *exc_info: Any) -> None:
        return None

    def step(self, label: str) -> bool:
        self.backend.steps.append(label)
        return self.backend.cancel_after is None or len(self.backend.steps) <= self.backend.cancel_after


class FakeBackend:
    def __init__(self, manifest_file: str, existing: Optional[List[str]] = None) -> None:
        self.manifest_file = manifest_file
        self.existing = set(existing or [])
        self.imported: List[str] = []
        self.downloads: List[Dict[str, Any]] = []
        self.invalid: List[bytes] = []
        self.messages: List[str] = []
        self.confirmations: List[str] = []
        self.confirm_answer = True
        self.cancel_after: Optional[int] = None
        self.steps: List[str] = []

    def manifest_path(self) -> str:
        return self.manifest_file

    def missing_takes(self, entry: Any) -> List[int]:
        return [t for t in entry.takes() if entry.asset_path(t) not in self.existing]

    def write_download(self, entry: Any, take: int, wav_bytes: bytes, info: Dict[str, Any], stamp: str) -> str:
        self.downloads.append({"asset": entry.asset_path(take), "info": info, "bytes": len(wav_bytes)})
        return "fake/{}.wav".format(entry.asset_name(take))

    def write_invalid(self, entry: Any, take: int, raw: bytes, stamp: str) -> str:
        self.invalid.append(raw)
        return "fake/{}.invalid".format(entry.asset_name(take))

    def import_take(self, entry: Any, take: int, wav_file: str, replace: bool) -> Dict[str, Any]:
        path = entry.asset_path(take)
        self.existing.add(path)
        self.imported.append(path)
        return {"asset": path, "duration": entry.duration, "looping": entry.loop}

    def progress(self, total: int, title: str) -> FakeProgress:
        return FakeProgress(self)

    def confirm(self, title: str, message: str) -> bool:
        self.confirmations.append(message)
        return self.confirm_answer

    def log(self, message: str) -> None:
        self.messages.append(message)

    def warn(self, message: str) -> None:
        self.messages.append(message)

    def error(self, message: str) -> None:
        self.messages.append(message)


class FakeClient:
    """Stands in for api.Client. `responses` is a list of bytes or exceptions, used in order."""

    def __init__(self, responses: Optional[List[Any]] = None, duration_scale: float = 1.0) -> None:
        self.responses = list(responses or [])
        self.calls: List[Dict[str, Any]] = []
        self.duration_scale = duration_scale

    def __call__(self, api_key: str) -> "FakeClient":
        self.api_key = api_key
        return self

    def sound_generation(self, **params: Any) -> bytes:
        self.calls.append(params)
        if self.responses:
            response = self.responses.pop(0)
            if isinstance(response, Exception):
                raise response
            return response
        return silence_wav(params["duration_seconds"] * self.duration_scale)

    def subscription(self) -> Dict[str, Any]:
        return {"tier": "free", "character_count": 1200, "character_limit": 10000,
                "next_character_count_reset_unix": 1}


class FakeResponse(io.BytesIO):
    def __enter__(self) -> "FakeResponse":
        return self

    def __exit__(self, *exc_info: Any) -> None:
        self.close()
