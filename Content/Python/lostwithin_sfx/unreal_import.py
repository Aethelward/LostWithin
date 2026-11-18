"""Unreal side of the SFX tool: asset checks, WAV import, metadata, progress and dialogs.

This is the default backend for lostwithin_sfx._run(); tests replace it with a fake that has the same functions.
"""

from __future__ import annotations

import datetime
import json
import os
from typing import Any, Dict, List

import unreal

from .api import SfxError
from .manifest import SoundEntry

MANIFEST_RELATIVE = ("Tools", "SFX", "sfx-manifest.json")
DOWNLOADS_FOLDER = "ElevenLabs"
TAG_PROMPT = "ElevenLabs.Prompt"
TAG_MODEL = "ElevenLabs.Model"
TAG_DATE = "ElevenLabs.Date"
TAG_ID = "ElevenLabs.ManifestId"


def project_dir() -> str:
    return unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir())


def manifest_path() -> str:
    return os.path.join(project_dir(), *MANIFEST_RELATIVE)


def _downloads_dir(entry: SoundEntry) -> str:
    saved = unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_saved_dir())
    path = os.path.join(saved, DOWNLOADS_FOLDER, entry.id)
    os.makedirs(path, exist_ok=True)
    return path


def asset_exists(asset_path: str) -> bool:
    return bool(unreal.EditorAssetLibrary.does_asset_exist(asset_path))


def missing_takes(entry: SoundEntry) -> List[int]:
    return [take for take in entry.takes() if not asset_exists(entry.asset_path(take))]


def write_download(entry: SoundEntry, take: int, wav_bytes: bytes, info: Dict[str, Any], stamp: str) -> str:
    """Keeps the generated WAV and a parameter sidecar (never the API key) under Saved/ElevenLabs/<id>/."""
    folder = _downloads_dir(entry)
    base = os.path.join(folder, "{}_{}".format(stamp, entry.asset_name(take)))
    wav_file = base + ".wav"
    with open(wav_file, "wb") as handle:
        handle.write(wav_bytes)
    sidecar = {
        "id": entry.id,
        "take": take,
        "asset": entry.asset_path(take),
        "request": entry.request_params(),
        "audio": info,
        "generated_at": stamp,
    }
    with open(base + ".json", "w", encoding="utf-8") as handle:
        json.dump(sidecar, handle, indent=2)
    return wav_file


def write_invalid(entry: SoundEntry, take: int, raw: bytes, stamp: str) -> str:
    """Keeps an unusable response for diagnosis; it is never imported."""
    path = os.path.join(_downloads_dir(entry), "{}_{}.invalid".format(stamp, entry.asset_name(take)))
    with open(path, "wb") as handle:
        handle.write(raw)
    return path


def _sound_duration(asset: Any) -> float:
    try:
        return float(asset.get_editor_property("duration"))
    except Exception as error:  # Unreal raises a plain Exception for unknown properties.
        unreal.log_warning("ElevenLabs SFX: cannot read duration of {}: {}".format(asset.get_path_name(), error))
        return -1.0


def import_take(entry: SoundEntry, take: int, wav_file: str, replace: bool) -> Dict[str, Any]:
    task = unreal.AssetImportTask()
    task.set_editor_property("filename", wav_file)
    task.set_editor_property("destination_path", entry.folder)
    task.set_editor_property("destination_name", entry.asset_name(take))
    task.set_editor_property("automated", True)
    task.set_editor_property("replace_existing", replace)
    task.set_editor_property("save", False)
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])

    asset_path = entry.asset_path(take)
    asset = unreal.EditorAssetLibrary.load_asset(asset_path)
    if not isinstance(asset, unreal.SoundWave):
        raise SfxError("Import of {} did not produce a SoundWave at {}".format(os.path.basename(wav_file), asset_path))

    asset.set_editor_property("looping", bool(entry.loop))
    unreal.EditorAssetLibrary.set_metadata_tag(asset, TAG_PROMPT, entry.prompt)
    unreal.EditorAssetLibrary.set_metadata_tag(asset, TAG_MODEL, entry.model_id)
    unreal.EditorAssetLibrary.set_metadata_tag(asset, TAG_DATE, datetime.date.today().isoformat())
    unreal.EditorAssetLibrary.set_metadata_tag(asset, TAG_ID, entry.id)
    if not unreal.EditorAssetLibrary.save_loaded_asset(asset, False):
        raise SfxError("Could not save {}".format(asset_path))
    return {"asset": asset_path, "duration": round(_sound_duration(asset), 3), "looping": bool(entry.loop)}


class Progress:
    def __init__(self, total: int, title: str) -> None:
        self._task = unreal.ScopedSlowTask(float(total), title)

    def __enter__(self) -> "Progress":
        self._task.__enter__()
        self._task.make_dialog(True)
        return self

    def __exit__(self, *exc_info: Any) -> None:
        self._task.__exit__(*exc_info)

    def step(self, label: str) -> bool:
        """Advances one take; returns False when the user pressed Cancel."""
        if self._task.should_cancel():
            return False
        self._task.enter_progress_frame(1.0, label)
        return True


def progress(total: int, title: str) -> Progress:
    return Progress(total, title)


def confirm(title: str, message: str) -> bool:
    answer = unreal.EditorDialog.show_message(title, message, unreal.AppMsgType.YES_NO)
    return answer == unreal.AppReturnType.YES


def show(title: str, message: str) -> None:
    unreal.EditorDialog.show_message(title, message, unreal.AppMsgType.OK)


def log(message: str) -> None:
    unreal.log("ElevenLabs SFX: " + message)


def warn(message: str) -> None:
    unreal.log_warning("ElevenLabs SFX: " + message)


def error(message: str) -> None:
    unreal.log_error("ElevenLabs SFX: " + message)
