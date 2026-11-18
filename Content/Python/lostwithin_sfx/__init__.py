"""LostWithin SFX generation with ElevenLabs.

Usage in the editor's Python console or through MCP execute_python:

    import lostwithin_sfx as sfx
    sfx.status()                              # what exists and what is missing
    sfx.generate("death_sting", dry_run=True)  # takes and credits, no API call
    sfx.generate("death_sting")               # generate + import the missing takes
    sfx.generate_missing()                    # every missing take, in capped runs
    sfx.credits()                             # credits used/limit (needs User read permission)

The sound list is <project>/Tools/SFX/sfx-manifest.json. See docs/elevenlabs-sfx.md.
Nothing here imports `unreal` at module load, so the pure parts can be tested outside the editor.
"""

from __future__ import annotations

import datetime
import importlib
import sys
from typing import Any, Callable, Dict, List, Optional, Sequence, Tuple

from .api import ApiError, ApiKeyMissing, Client, SfxError, load_api_key, redact
from .manifest import Manifest, ManifestError, SoundEntry, load_manifest, plan_run
from .wav import AudioFormatError, WavInfo, normalize_peak, normalize_response, trim_silence

__all__ = ["status", "generate", "generate_missing", "credits", "format_summary", "reload_modules",
           "SfxError", "ApiError", "ApiKeyMissing", "ManifestError", "AudioFormatError"]

TITLE = "ElevenLabs SFX"
DURATION_WARNING_TOLERANCE = 0.25
MANIFEST_LABEL = "Tools/SFX/sfx-manifest.json"


def _default_backend() -> Any:
    from . import unreal_import
    return unreal_import


def _load(backend: Any) -> Manifest:
    return load_manifest(backend.manifest_path())


def _row(planned: Sequence[Any]) -> Dict[str, Any]:
    entry, takes, cost = planned
    return {"id": entry.id, "assets": [entry.asset_path(t) for t in takes], "credits": cost}


def status(backend: Any = None) -> Dict[str, Any]:
    backend = backend or _default_backend()
    manifest = _load(backend)
    rows = []
    missing_total = 0
    credits_total = 0
    for entry in manifest.sounds:
        missing = backend.missing_takes(entry)
        cost = plan_run([(entry, missing)], sys.maxsize)[3]
        rows.append({
            "id": entry.id,
            "present": [entry.asset_path(t) for t in entry.takes() if t not in missing],
            "missing": [entry.asset_path(t) for t in missing],
            "credits_missing": cost,
        })
        missing_total += len(missing)
        credits_total += cost
    return {"manifest": MANIFEST_LABEL, "sounds": rows, "missing_takes": missing_total,
            "credits_missing": credits_total, "max_credits_per_run": manifest.max_credits_per_run}


def generate(sound_id: str, force: bool = False, dry_run: bool = False, max_credits: Optional[int] = None,
             backend: Any = None, client_factory: Optional[Callable[[str], Client]] = None) -> Dict[str, Any]:
    """Generates the missing takes of one manifest entry (all takes with force=True)."""
    backend = backend or _default_backend()
    manifest = _load(backend)
    entry = manifest.get(sound_id)
    return _run(manifest, [entry], force, dry_run, max_credits, False, backend, client_factory)


def generate_missing(dry_run: bool = False, max_credits: Optional[int] = None, interactive: bool = False,
                     backend: Any = None, client_factory: Optional[Callable[[str], Client]] = None) -> Dict[str, Any]:
    """Generates missing takes for every entry, taking whole entries in order up to the credit cap."""
    backend = backend or _default_backend()
    manifest = _load(backend)
    return _run(manifest, manifest.sounds, False, dry_run, max_credits, interactive, backend, client_factory)


def credits(backend: Any = None, client_factory: Optional[Callable[[str], Client]] = None) -> Optional[Dict[str, Any]]:
    backend = backend or _default_backend()
    client = (client_factory or Client)(load_api_key())
    try:
        subscription = client.subscription()
    except ApiError as error:
        if error.status in (401, 403):
            backend.warn("credit check unavailable ({}); the key may lack the User read permission".format(error.status))
            return None
        raise
    used = subscription.get("character_count")
    limit = subscription.get("character_limit")
    remaining = limit - used if isinstance(used, int) and isinstance(limit, int) else None
    return {"tier": subscription.get("tier"), "used": used, "limit": limit, "remaining": remaining,
            "resets_unix": subscription.get("next_character_count_reset_unix")}


def format_summary(summary: Dict[str, Any]) -> str:
    lines = []
    if summary.get("dry_run"):
        lines.append("Dry run: no API calls made.")
    lines.append("Planned: {} take(s), about {} credits (cap {}).".format(
        sum(len(r["assets"]) for r in summary["planned"]), summary["estimated_credits"], summary["cap"]))
    for done in summary["completed"]:
        lines.append("  imported {} ({} s)".format(done["asset"], done["duration"]))
    if summary["deferred"]:
        lines.append("Deferred to the next run: " + ", ".join(r["id"] for r in summary["deferred"]))
    if summary["refused"]:
        lines.append("Refused, over the cap on their own: " + ", ".join(
            "{} ({} credits)".format(r["id"], r["credits"]) for r in summary["refused"]))
    for warning in summary["warnings"]:
        lines.append("Warning: " + warning)
    if summary.get("cancelled"):
        lines.append("Cancelled.")
    if summary.get("error"):
        lines.append("Error: " + summary["error"])
    return "\n".join(lines)


def _new_summary(cap: int, dry_run: bool, selected: List[Any], deferred: List[Any], refused: List[Any],
                 credits_estimate: int) -> Dict[str, Any]:
    return {"cap": cap, "dry_run": dry_run, "estimated_credits": credits_estimate,
            "planned": [_row(r) for r in selected], "deferred": [_row(r) for r in deferred],
            "refused": [_row(r) for r in refused], "completed": [], "warnings": [],
            "cancelled": False, "error": None}


def _run(manifest: Manifest, entries: Sequence[SoundEntry], force: bool, dry_run: bool,
         max_credits: Optional[int], interactive: bool, backend: Any,
         client_factory: Optional[Callable[[str], Client]]) -> Dict[str, Any]:
    cap = manifest.max_credits_per_run if max_credits is None else int(max_credits)
    work = [(entry, entry.takes() if force else backend.missing_takes(entry)) for entry in entries]
    selected, deferred, refused, estimate = plan_run(work, cap)
    summary = _new_summary(cap, dry_run, selected, deferred, refused, estimate)

    if dry_run or not selected:
        if not selected and not refused and not deferred:
            summary["warnings"].append("nothing to generate; every take already exists")
        backend.log(format_summary(summary))
        return summary

    if interactive and not backend.confirm(TITLE, format_summary(summary) + "\n\nGenerate now?"):
        summary["cancelled"] = True
        return summary

    api_key = load_api_key()
    client = (client_factory or Client)(api_key)
    try:
        _generate_selected(client, selected, force, backend, summary)
    except (SfxError, AudioFormatError, OSError) as failure:
        summary["error"] = redact(failure, api_key)
        backend.error(summary["error"])
    backend.log(redact(format_summary(summary), api_key))
    return summary


ATTEMPTS_PER_TAKE = 2


def _process_audio(entry: SoundEntry, raw: bytes) -> Tuple[bytes, WavInfo, Dict[str, Any]]:
    """WAV check, then optional peak normalization, then optional silence trim. Raises AudioFormatError."""
    wav_bytes, info = normalize_response(raw, entry.output_format)
    audio: Dict[str, Any] = info.as_dict()
    if entry.normalize_peak_db is not None:
        wav_bytes, _, gain_db = normalize_peak(wav_bytes, entry.normalize_peak_db)
        audio["gain_db"] = round(gain_db, 2)
    if entry.trim_silence:
        wav_bytes, trimmed = trim_silence(wav_bytes)
        audio["trimmed_duration"] = round(trimmed.duration, 4)
    return wav_bytes, info, audio


def _generate_usable_take(client: Client, entry: SoundEntry, take: int, backend: Any, stamp: str,
                          summary: Dict[str, Any]) -> Tuple[bytes, WavInfo, Dict[str, Any]]:
    """Generates one take, regenerating once when the response is unusable (both responses are kept)."""
    for attempt in range(1, ATTEMPTS_PER_TAKE + 1):
        raw = client.sound_generation(**entry.request_params())
        try:
            return _process_audio(entry, raw)
        except AudioFormatError as bad_audio:
            kept = backend.write_invalid(entry, take, raw, "{}-try{}".format(stamp, attempt))
            if attempt == ATTEMPTS_PER_TAKE:
                raise AudioFormatError("{}: {} (response kept at {})".format(entry.id, bad_audio, kept)) from None
            summary["warnings"].append("{} take {:02d}: {}; generated it again".format(entry.id, take, bad_audio))
    raise AssertionError("unreachable")


def _generate_selected(client: Client, selected: Sequence[Any], force: bool, backend: Any,
                       summary: Dict[str, Any]) -> None:
    stamp = datetime.datetime.now().strftime("%Y%m%d-%H%M%S")
    total = sum(len(takes) for _, takes, _ in selected)
    with backend.progress(total, "Generating {} sound(s) with ElevenLabs".format(total)) as progress:
        for entry, takes, _ in selected:
            for take in takes:
                if not progress.step("{} ({})".format(entry.asset_name(take), entry.id)):
                    summary["cancelled"] = True
                    return
                wav_bytes, info, audio = _generate_usable_take(client, entry, take, backend, stamp, summary)
                wav_file = backend.write_download(entry, take, wav_bytes, audio, stamp)
                result = backend.import_take(entry, take, wav_file, force)
                result["generated_duration"] = round(info.duration, 3)
                if entry.trim_silence:
                    result["trimmed_duration"] = audio["trimmed_duration"]
                summary["completed"].append(result)
                if abs(info.duration - entry.duration) > DURATION_WARNING_TOLERANCE:
                    summary["warnings"].append("{} is {:.2f} s; requested {:.2f} s".format(
                        entry.asset_path(take), info.duration, entry.duration))


def reload_modules() -> None:
    """Reloads the tool after its .py files change, without restarting the editor."""
    for name in ("lostwithin_sfx.api", "lostwithin_sfx.manifest", "lostwithin_sfx.wav",
                 "lostwithin_sfx.unreal_import", "lostwithin_sfx.menu"):
        if name in sys.modules:
            importlib.reload(sys.modules[name])
    importlib.reload(sys.modules[__name__])
