"""One-shot full build after an explicitly closed editor, then reopen the project.

Writes status JSON and `full-build.log` into `--status`'s parent folder
(typically `LostWithin/Saved/Temp/MenuFix/`). Only run on Windows after explicit
user approval to close Unreal Editor.
"""

from __future__ import annotations

import argparse
import ctypes
from ctypes import wintypes
from datetime import datetime, timezone
import json
import os
from pathlib import Path
import subprocess
import traceback


def write_status(path: Path, state: str, **details: object) -> None:
    temporary = path.with_suffix(".tmp")
    temporary.write_text(
        json.dumps(
            {
                "state": state,
                "updated_utc": datetime.now(timezone.utc).isoformat(),
                **details,
            },
            indent=2,
        ),
        encoding="utf-8",
    )
    temporary.replace(path)
    print(state, details, flush=True)


def wait_for_editor(editor_pid: int, status: Path) -> None:
    kernel = ctypes.WinDLL("kernel32", use_last_error=True)
    kernel.OpenProcess.argtypes = [wintypes.DWORD, wintypes.BOOL, wintypes.DWORD]
    kernel.OpenProcess.restype = wintypes.HANDLE
    kernel.WaitForSingleObject.argtypes = [wintypes.HANDLE, wintypes.DWORD]
    kernel.WaitForSingleObject.restype = wintypes.DWORD
    kernel.CloseHandle.argtypes = [wintypes.HANDLE]
    kernel.CloseHandle.restype = wintypes.BOOL
    handle = kernel.OpenProcess(0x00100000, False, editor_pid)
    if not handle:
        error = ctypes.get_last_error()
        if error == 87:  # Original process already exited.
            return
        raise ctypes.WinError(error)
    try:
        write_status(status, "waiting_for_editor_exit", editor_pid=editor_pid)
        result = kernel.WaitForSingleObject(handle, 120000)
        if result == 0x00000102:
            raise TimeoutError("Editor did not close within 120 seconds; no build was started.")
        if result != 0:
            raise ctypes.WinError(ctypes.get_last_error())
    finally:
        kernel.CloseHandle(handle)


def main() -> int:
    default_project = Path(__file__).resolve().parents[2] / "LostWithin.uproject"
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--project", type=Path, default=default_project)
    parser.add_argument("--engine-root", type=Path, required=True)
    parser.add_argument("--editor-pid", type=int, required=True)
    parser.add_argument("--status", type=Path, default=None)
    parser.add_argument("--map", default="/Game/FirstPerson/Lvl_FirstPerson")
    args = parser.parse_args()

    project = args.project.resolve()
    engine_root = args.engine_root.resolve()
    status = (
        args.status.resolve()
        if args.status is not None
        else (project.parent / "Saved" / "Temp" / "MenuFix" / "build-status.json").resolve()
    )
    status.parent.mkdir(parents=True, exist_ok=True)

    try:
        if os.name != "nt":
            raise RuntimeError("This one-shot helper must run on Windows.")
        build_script = project.parent / "BuildCheck.ps1"
        editor_exe = engine_root / "Engine" / "Binaries" / "Win64" / "UnrealEditor.exe"
        for required in (project, build_script, editor_exe):
            if not required.is_file():
                raise FileNotFoundError(str(required))

        wait_for_editor(args.editor_pid, status)
        build_log = status.parent / "full-build.log"
        write_status(status, "building", build_log=str(build_log))
        with build_log.open("w", encoding="utf-8", errors="replace") as output:
            result = subprocess.run(
                [
                    "powershell.exe",
                    "-NoLogo",
                    "-NoProfile",
                    "-ExecutionPolicy",
                    "Bypass",
                    "-File",
                    str(build_script),
                    "-ProjectPath",
                    str(project),
                    "-EngineRoot",
                    str(engine_root),
                ],
                cwd=project.parent,
                stdin=subprocess.DEVNULL,
                stdout=output,
                stderr=subprocess.STDOUT,
                check=False,
            )
        if result.returncode != 0:
            write_status(
                status,
                "build_failed",
                build_exit_code=result.returncode,
                build_log=str(build_log),
            )
            return result.returncode

        write_status(status, "reopening_editor", build_exit_code=0, build_log=str(build_log))
        editor = subprocess.Popen(
            [str(editor_exe), str(project), args.map],
            cwd=project.parent,
            stdin=subprocess.DEVNULL,
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
            close_fds=True,
            creationflags=subprocess.DETACHED_PROCESS | subprocess.CREATE_NEW_PROCESS_GROUP,
        )
        write_status(
            status,
            "editor_launched",
            build_exit_code=0,
            editor_pid=editor.pid,
            build_log=str(build_log),
        )
        return 0
    except Exception as error:
        traceback.print_exc()
        write_status(status, "failed", error=str(error))
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
