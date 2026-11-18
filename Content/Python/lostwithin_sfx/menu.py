"""Tools > LostWithin menu entries for the SFX tool (registered from Content/Python/init_unreal.py)."""

from __future__ import annotations

import unreal

import lostwithin_sfx
from lostwithin_sfx import unreal_import
from lostwithin_sfx.api import SfxError
from lostwithin_sfx.manifest import ManifestError

OWNER = "LostWithinSFX"
PARENT_MENU = "LevelEditor.MainMenu.Tools"
SUBMENU_NAME = "LostWithin"
SECTION = "ElevenLabsSFX"


def _entry(name: str, label: str, tooltip: str, command: str) -> unreal.ToolMenuEntry:
    entry = unreal.ToolMenuEntry(name=name, type=unreal.MultiBlockType.MENU_ENTRY)
    entry.set_label(label)
    entry.set_tool_tip(tooltip)
    entry.set_string_command(unreal.ToolMenuStringCommandType.PYTHON, "", command)
    return entry


def register() -> bool:
    menus = unreal.ToolMenus.get()
    tools = menus.find_menu(PARENT_MENU) if menus else None
    if tools is None:
        unreal_import.warn("Tools menu not found; menu entries were not added (commandlet or no Level Editor)")
        return False
    submenu = menus.find_menu(PARENT_MENU + "." + SUBMENU_NAME)
    if submenu is None:
        submenu = tools.add_sub_menu(OWNER, "LostWithin", SUBMENU_NAME, "LostWithin", "LostWithin project tools")
    submenu.add_section(SECTION, "ElevenLabs SFX")
    submenu.add_menu_entry(SECTION, _entry(
        "LostWithinGenerateMissingSFX", "Generate missing SFX (ElevenLabs)",
        "Generates the missing sounds listed in Tools/SFX/sfx-manifest.json, after showing the credit cost",
        "import lostwithin_sfx.menu as m; m.generate_missing_from_menu()"))
    submenu.add_menu_entry(SECTION, _entry(
        "LostWithinSFXStatus", "SFX status / credits",
        "Shows which manifest sounds exist, which are missing, and the remaining ElevenLabs credits",
        "import lostwithin_sfx.menu as m; m.status_from_menu()"))
    menus.refresh_all_widgets()
    return True


def generate_missing_from_menu() -> None:
    try:
        summary = lostwithin_sfx.generate_missing(interactive=True)
    except (SfxError, ManifestError) as failure:
        unreal_import.error(str(failure))
        unreal_import.show(lostwithin_sfx.TITLE, str(failure))
        return
    if summary.get("completed") or summary.get("error") or not summary.get("planned"):
        unreal_import.show(lostwithin_sfx.TITLE, lostwithin_sfx.format_summary(summary))


def status_from_menu() -> None:
    try:
        report = lostwithin_sfx.status()
    except ManifestError as failure:
        unreal_import.show(lostwithin_sfx.TITLE, str(failure))
        return
    lines = ["{} sound entries, {} missing take(s), about {} credits to generate (cap {} per run).".format(
        len(report["sounds"]), report["missing_takes"], report["credits_missing"], report["max_credits_per_run"])]
    for row in report["sounds"]:
        if row["missing"]:
            lines.append("  {}: {} missing".format(row["id"], len(row["missing"])))
    try:
        account = lostwithin_sfx.credits()
    except SfxError as failure:
        account = None
        lines.append("Credits: unavailable ({})".format(failure))
    if account:
        lines.append("Credits: {} used of {} ({} left, tier {}).".format(
            account["used"], account["limit"], account["remaining"], account["tier"]))
    unreal_import.show(lostwithin_sfx.TITLE, "\n".join(lines))
