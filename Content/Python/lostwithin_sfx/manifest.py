"""SFX manifest loading and validation (no `unreal` import).

The manifest lives at <project>/Tools/SFX/sfx-manifest.json. Each sound entry becomes `variations` assets named
<folder>/<name>_01, _02, ...
"""

from __future__ import annotations

import json
import math
import re
from typing import Any, Dict, List, Mapping, Optional, Sequence, Tuple

MIN_DURATION = 0.5
MAX_DURATION = 30.0
MAX_PROMPT_LENGTH = 450  # API limit, verified 2026-11-18 (400 invalid_text_length above it)
MAX_VARIATIONS = 10
CREDITS_PER_SECOND = 40
MODELS = ("eleven_text_to_sound_v2", "eleven_text_to_sound_v3")

_ID_RE = re.compile(r"^[a-z0-9][a-z0-9_]*$")
_NAME_RE = re.compile(r"^[A-Za-z][A-Za-z0-9_]*$")
_NAME_TAKE_SUFFIX_RE = re.compile(r"_\d+$")
_FOLDER_RE = re.compile(r"^/Game(/[A-Za-z0-9_]+)+$")
_FORMAT_RE = re.compile(r"^(wav|pcm)_(8000|16000|22050|24000|32000|44100|48000)$")

_ENTRY_KEYS = {"id", "prompt", "duration", "loop", "variations", "folder", "name",
               "model_id", "output_format", "prompt_influence", "trim_silence", "normalize_peak_db", "notes"}
MIN_NORMALIZE_DB = -30.0
_DEFAULT_KEYS = {"model_id", "output_format", "prompt_influence", "variations"}
_BUILTIN_DEFAULTS: Dict[str, Any] = {"model_id": "eleven_text_to_sound_v3", "output_format": "wav_48000",
                                     "prompt_influence": 0.3, "variations": 1}


class ManifestError(ValueError):
    pass


class SoundEntry:
    __slots__ = ("id", "prompt", "duration", "loop", "variations", "folder", "name",
                 "model_id", "output_format", "prompt_influence", "trim_silence", "normalize_peak_db")

    def __init__(self, id: str, prompt: str, duration: float, loop: bool, variations: int, folder: str,
                 name: str, model_id: str, output_format: str, prompt_influence: float,
                 trim_silence: bool = False, normalize_peak_db: Optional[float] = None) -> None:
        self.trim_silence = trim_silence
        self.normalize_peak_db = normalize_peak_db
        self.id = id
        self.prompt = prompt
        self.duration = duration
        self.loop = loop
        self.variations = variations
        self.folder = folder
        self.name = name
        self.model_id = model_id
        self.output_format = output_format
        self.prompt_influence = prompt_influence

    def asset_name(self, take: int) -> str:
        return "{}_{:02d}".format(self.name, take)

    def asset_path(self, take: int) -> str:
        return "{}/{}".format(self.folder, self.asset_name(take))

    def object_path(self, take: int) -> str:
        return "{}.{}".format(self.asset_path(take), self.asset_name(take))

    def takes(self) -> List[int]:
        return list(range(1, self.variations + 1))

    def request_params(self) -> Dict[str, Any]:
        return {
            "text": self.prompt,
            "duration_seconds": self.duration,
            "prompt_influence": self.prompt_influence,
            "loop": self.loop,
            "model_id": self.model_id,
            "output_format": self.output_format,
        }

    def __repr__(self) -> str:
        return "SoundEntry(id={!r})".format(self.id)


class Manifest:
    def __init__(self, version: int, max_credits_per_run: int, sounds: List[SoundEntry], source: str) -> None:
        self.version = version
        self.max_credits_per_run = max_credits_per_run
        self.sounds = sounds
        self.source = source

    def get(self, sound_id: str) -> SoundEntry:
        for entry in self.sounds:
            if entry.id == sound_id:
                return entry
        raise ManifestError("{}: no sound with id '{}'".format(self.source, sound_id))

    def ids(self) -> List[str]:
        return [entry.id for entry in self.sounds]


# (entry, takes) pairs to generate, and (entry, takes, credits) rows after planning.
Work = Sequence[Tuple[SoundEntry, List[int]]]
PlannedRow = Tuple[SoundEntry, List[int], int]


def estimate_credits(duration_seconds: float, takes: int = 1) -> int:
    return int(math.ceil(CREDITS_PER_SECOND * float(duration_seconds) - 1e-9)) * int(takes)


def load_manifest(path: str) -> Manifest:
    try:
        with open(path, "r", encoding="utf-8") as handle:
            data = json.load(handle)
    except OSError as error:
        raise ManifestError("Cannot read manifest {}: {}".format(path, error)) from None
    except ValueError as error:
        raise ManifestError("Manifest {} is not valid JSON: {}".format(path, error)) from None
    return parse_manifest(data, source=path)


def _number(value: Any, where: str, field: str) -> float:
    if isinstance(value, bool) or not isinstance(value, (int, float)):
        raise ManifestError("{}: '{}' must be a number".format(where, field))
    return float(value)


def _check_common(fields: Mapping[str, Any], where: str) -> None:
    if fields["model_id"] not in MODELS:
        raise ManifestError("{}: model_id must be one of {}".format(where, ", ".join(MODELS)))
    output_format = fields["output_format"]
    if not isinstance(output_format, str) or not _FORMAT_RE.match(output_format):
        raise ManifestError("{}: output_format must be wav_<rate> or pcm_<rate>".format(where))
    influence = _number(fields["prompt_influence"], where, "prompt_influence")
    if not 0.0 <= influence <= 1.0:
        raise ManifestError("{}: prompt_influence must be between 0 and 1".format(where))
    variations = fields["variations"]
    if isinstance(variations, bool) or not isinstance(variations, int) or not 1 <= variations <= MAX_VARIATIONS:
        raise ManifestError("{}: variations must be a whole number from 1 to {}".format(where, MAX_VARIATIONS))


def _parse_defaults(data: Mapping[str, Any], source: str) -> Dict[str, Any]:
    defaults = data.get("defaults", {})
    if not isinstance(defaults, dict):
        raise ManifestError("{}: defaults must be an object".format(source))
    unknown = set(defaults) - _DEFAULT_KEYS
    if unknown:
        raise ManifestError("{}: unknown defaults field(s): {}".format(source, ", ".join(sorted(unknown))))
    merged = dict(_BUILTIN_DEFAULTS)
    merged.update(defaults)
    _check_common(merged, source + " defaults")
    return merged


def _parse_entry(raw: Any, where: str, defaults: Mapping[str, Any], seen_ids: set) -> SoundEntry:
    if not isinstance(raw, dict):
        raise ManifestError("{}: must be an object".format(where))
    sound_id = raw.get("id")
    if not isinstance(sound_id, str) or not _ID_RE.match(sound_id):
        raise ManifestError("{}: id must be lowercase letters, digits and underscores".format(where))
    where = "{} ({})".format(where, sound_id)
    if sound_id in seen_ids:
        raise ManifestError("{}: duplicate id".format(where))
    seen_ids.add(sound_id)

    unknown = set(raw) - _ENTRY_KEYS
    if unknown:
        raise ManifestError("{}: unknown field(s): {}".format(where, ", ".join(sorted(unknown))))

    prompt = raw.get("prompt")
    if not isinstance(prompt, str) or not prompt.strip():
        raise ManifestError("{}: prompt must be non-empty text".format(where))
    if len(prompt.strip()) > MAX_PROMPT_LENGTH:
        raise ManifestError("{}: prompt is {} characters; ElevenLabs accepts at most {}".format(
            where, len(prompt.strip()), MAX_PROMPT_LENGTH))

    duration = _number(raw.get("duration"), where, "duration")
    if not MIN_DURATION <= duration <= MAX_DURATION:
        raise ManifestError("{}: duration must be between {} and {} seconds".format(where, MIN_DURATION, MAX_DURATION))

    loop = raw.get("loop", False)
    if not isinstance(loop, bool):
        raise ManifestError("{}: loop must be true or false".format(where))

    trim = raw.get("trim_silence", False)
    if not isinstance(trim, bool):
        raise ManifestError("{}: trim_silence must be true or false".format(where))
    if trim and loop:
        raise ManifestError("{}: trim_silence would break a seamless loop; use one or the other".format(where))

    normalize = raw.get("normalize_peak_db")
    if normalize is not None:
        normalize = _number(normalize, where, "normalize_peak_db")
        if not MIN_NORMALIZE_DB <= normalize <= 0.0:
            raise ManifestError("{}: normalize_peak_db must be between {} and 0 dBFS".format(where, MIN_NORMALIZE_DB))

    folder = raw.get("folder")
    if not isinstance(folder, str) or not _FOLDER_RE.match(folder):
        raise ManifestError("{}: folder must look like /Game/Some/Folder".format(where))

    name = raw.get("name")
    if not isinstance(name, str) or not _NAME_RE.match(name) or _NAME_TAKE_SUFFIX_RE.search(name):
        raise ManifestError("{}: name must be letters, digits and underscores, without a _NN take suffix".format(where))

    fields = dict(defaults)
    for key in _DEFAULT_KEYS:
        if key in raw:
            fields[key] = raw[key]
    _check_common(fields, where)

    return SoundEntry(id=sound_id, prompt=prompt.strip(), duration=duration, loop=loop,
                      variations=fields["variations"], folder=folder, name=name,
                      model_id=fields["model_id"], output_format=fields["output_format"],
                      prompt_influence=float(fields["prompt_influence"]), trim_silence=trim,
                      normalize_peak_db=normalize)


def parse_manifest(data: Any, source: str = "manifest") -> Manifest:
    if not isinstance(data, dict):
        raise ManifestError("{}: top level must be an object".format(source))
    version = data.get("version")
    if version != 1:
        raise ManifestError("{}: unsupported version {!r} (expected 1)".format(source, version))

    cap = data.get("max_credits_per_run")
    if isinstance(cap, bool) or not isinstance(cap, int) or cap <= 0:
        raise ManifestError("{}: max_credits_per_run must be a positive whole number".format(source))

    defaults = _parse_defaults(data, source)
    raw_sounds = data.get("sounds")
    if not isinstance(raw_sounds, list) or not raw_sounds:
        raise ManifestError("{}: sounds must be a non-empty list".format(source))

    entries: List[SoundEntry] = []
    seen_ids: set = set()
    seen_assets: Dict[str, str] = {}
    for index, raw in enumerate(raw_sounds):
        entry = _parse_entry(raw, "{} sounds[{}]".format(source, index), defaults, seen_ids)
        for take in entry.takes():
            path = entry.asset_path(take).lower()
            if path in seen_assets:
                raise ManifestError("{} ({}): asset {} is also produced by '{}'".format(
                    source, entry.id, entry.asset_path(take), seen_assets[path]))
            seen_assets[path] = entry.id
        entries.append(entry)

    return Manifest(version, cap, entries, source)


def plan_run(work: Work, cap: int) -> Tuple[List[PlannedRow], List[PlannedRow], List[PlannedRow], int]:
    """Splits [(entry, [takes])] into (selected, deferred, refused, selected_credits) under a credit cap.

    Whole entries are taken in order while they fit. An entry that alone exceeds the cap is refused.
    """
    selected: List[PlannedRow] = []
    deferred: List[PlannedRow] = []
    refused: List[PlannedRow] = []
    total = 0
    for entry, takes in work:
        if not takes:
            continue
        cost = estimate_credits(entry.duration, len(takes))
        if cost > cap:
            refused.append((entry, list(takes), cost))
        elif total + cost <= cap:
            selected.append((entry, list(takes), cost))
            total += cost
        else:
            deferred.append((entry, list(takes), cost))
    return selected, deferred, refused, total
