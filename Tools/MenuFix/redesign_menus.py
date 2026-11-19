#!/usr/bin/env python3
"""Idempotent Unreal Editor UMG redesign & cleanup tool for LostWithin menus.

Implements Wave 4 (T8 MainMenu & T9 OptionsMenu) from
`.ai/tasks/2026-11-19-173555-fix-main-menu/DESIGN.md` over EngineWire MCP
(`npx -y unreal-engine-mcp-server@0.6.0`).

Safety invariants:
- Refuses to mutate anything while PIE is active or if required assets are missing.
- Automatically backs up `Content/UI/Menu/MainMenu.uasset` and
  `OptionsMenu.uasset` into `Saved/Backups/MenuFix/` before any write (preserving
  the original baseline backup unless `--force-backup` is passed).
- Verifies `/Script/LostWithin.LostWithinMainMenu` exists before reparenting
  `MainMenu` (or reports preflight status cleanly in `--dry-run`).
"""

from __future__ import annotations

import argparse
import base64
import hashlib
import json
import os
import shutil
import subprocess
import sys
import time
from datetime import datetime, timezone
from pathlib import Path
from typing import Any


MENU_PATHS = {
    "main": "/Game/UI/Menu/MainMenu",
    "options": "/Game/UI/Menu/OptionsMenu",
}

REQUIRED_ASSETS = [
    "/Game/UI/Menu/MainMenu",
    "/Game/UI/Menu/OptionsMenu",
    "/Game/UI/Menu/Textures/Haunted_Radio_Maze",
    "/Game/UI/Menu/Textures/bg_options",
    "/Game/UI/Fonts/Cinzel-Bold_Font",
    "/Game/UI/Fonts/Cinzel-Regular_Font",
    "/Game/Audio/SFX/UI/click",
]

LEGACY_OPTIONS_VARIABLES = [
    "windowmode",
    "resolutionindex",
    "resolution",
    "graphicsindex",
    "vsync?",
]

LEGACY_OPTIONS_FUNCTIONS = [
    "GetText",
    "GetText_0",
    "GetText_1",
    "GetText_2",
    "GetText_3",
]


class McpError(RuntimeError):
    """Raised when an EngineWire MCP call fails unexpectedly."""


class McpSession:
    """Persistent stdio JSON-RPC session with unreal-engine-mcp-server."""

    def __init__(self, project_dir: Path, mcp_url: str, min_interval_sec: float = 0.55) -> None:
        self.project_dir = project_dir
        self.mcp_url = mcp_url
        self.min_interval_sec = min_interval_sec
        self._proc: subprocess.Popen[str] | None = None
        self._next_id = 1
        self._last_call_ts = 0.0

    def __enter__(self) -> McpSession:
        env = os.environ.copy()
        env["UE_PROJECT_PATH"] = str(self.project_dir)
        env["UNREAL_MCP_URL"] = self.mcp_url
        self._proc = subprocess.Popen(
            ["npx", "-y", "unreal-engine-mcp-server@0.6.0"],
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            env=env,
        )
        init = self._rpc(
            "initialize",
            {
                "protocolVersion": "2024-11-05",
                "capabilities": {},
                "clientInfo": {"name": "lostwithin-menufix", "version": "1.0.0"},
            },
        )
        if "error" in init:
            raise McpError(f"MCP initialize failed: {init['error']}")
        self._notify("notifications/initialized")
        return self

    def __exit__(self, *_: object) -> None:
        if self._proc is None:
            return
        self._proc.terminate()
        try:
            self._proc.wait(timeout=5)
        except subprocess.TimeoutExpired:
            self._proc.kill()
        self._proc = None

    def _notify(self, method: str, params: dict[str, Any] | None = None) -> None:
        assert self._proc is not None and self._proc.stdin is not None
        payload: dict[str, Any] = {"jsonrpc": "2.0", "method": method}
        if params is not None:
            payload["params"] = params
        self._proc.stdin.write(json.dumps(payload) + "\n")
        self._proc.stdin.flush()

    def _rpc(self, method: str, params: dict[str, Any]) -> dict[str, Any]:
        assert self._proc is not None and self._proc.stdin is not None and self._proc.stdout is not None
        req_id = self._next_id
        self._next_id += 1
        msg = {"jsonrpc": "2.0", "id": req_id, "method": method, "params": params}
        self._proc.stdin.write(json.dumps(msg) + "\n")
        self._proc.stdin.flush()
        while True:
            line = self._proc.stdout.readline()
            if not line:
                err = self._proc.stderr.read() if self._proc.stderr else ""
                raise McpError(f"MCP EOF while waiting for id={req_id}; stderr={err!r}")
            line = line.strip()
            if not line:
                continue
            try:
                obj = json.loads(line)
            except json.JSONDecodeError:
                continue
            if obj.get("id") == req_id:
                return obj

    def _throttle(self) -> None:
        # Respect the 120 calls/min server rate limit comfortably.
        now = time.monotonic()
        wait = self.min_interval_sec - (now - self._last_call_ts)
        if wait > 0:
            time.sleep(wait)
        self._last_call_ts = time.monotonic()

    def unreal(
        self,
        operation: str,
        tool: str,
        action: str,
        capability: str | None = None,
        params: dict[str, Any] | None = None,
        consent: dict[str, Any] | None = None,
    ) -> dict[str, Any]:
        self._throttle()
        args: dict[str, Any] = {
            "operation": operation,
            "tool": tool,
            "action": action,
        }
        if capability is not None:
            args["capability"] = capability
        if params is not None:
            args["params"] = params
        if consent is not None:
            args["consent"] = consent
        resp = self._rpc("tools/call", {"name": "unreal", "arguments": args})
        if "error" in resp:
            raise McpError(f"RPC error for {tool}.{action}: {resp['error']}")
        res = resp.get("result", {})
        sc = res.get("structuredContent", {})
        if res.get("isError") or sc.get("success") is False:
            msg = sc.get("message") or sc.get("error") or json.dumps(res)[:400]
            raise McpError(f"{tool}.{action} failed: {msg}")
        return sc

    def execute(self, tool: str, action: str, capability: str, params: dict[str, Any]) -> dict[str, Any]:
        return self.unreal("execute", tool, action, capability=capability, params=params)

    def execute_python(self, code: str) -> dict[str, Any]:
        desc = self.unreal("describe", "system_control", "execute_python")
        grant = desc["consentGrant"]
        sc = self.unreal(
            "execute",
            "system_control",
            "execute_python",
            capability="system_control.execute_python",
            params={"code": code},
            consent=grant,
        )
        return sc.get("data", {})


def sha256_file(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as f:
        for chunk in iter(lambda: f.read(65536), b""):
            h.update(chunk)
    return h.hexdigest()


def backup_menu_assets(project_dir: Path, force_snapshot: bool = False) -> Path:
    """Back up MainMenu.uasset and OptionsMenu.uasset under Saved/Backups/MenuFix/."""
    backup_dir = project_dir / "Saved" / "Backups" / "MenuFix"
    backup_dir.mkdir(parents=True, exist_ok=True)
    manifest_path = backup_dir / "backup_manifest.json"
    manifest: dict[str, Any] = {}
    if manifest_path.is_file():
        try:
            manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
        except json.JSONDecodeError:
            manifest = {}

    sources = [
        project_dir / "Content" / "UI" / "Menu" / "MainMenu.uasset",
        project_dir / "Content" / "UI" / "Menu" / "OptionsMenu.uasset",
    ]
    ts = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ")
    for src in sources:
        if not src.is_file():
            raise FileNotFoundError(f"Expected menu asset missing on disk: {src}")
        baseline = backup_dir / src.name
        if not baseline.exists():
            shutil.copy2(src, baseline)
            manifest[f"baseline/{src.name}"] = {
                "copied_utc": ts,
                "sha256": sha256_file(baseline),
                "bytes": baseline.stat().st_size,
            }
        elif force_snapshot:
            snap = backup_dir / f"{src.stem}_{ts}{src.suffix}"
            shutil.copy2(src, snap)
            manifest[f"snapshot/{snap.name}"] = {
                "copied_utc": ts,
                "sha256": sha256_file(snap),
                "bytes": snap.stat().st_size,
            }

    manifest_path.write_text(json.dumps(manifest, indent=2), encoding="utf-8")
    return backup_dir


def get_widget_map(mcp: McpSession, widget_path: str) -> tuple[dict[str, Any], dict[str, dict[str, Any]]]:
    sc = mcp.execute(
        "manage_blueprint",
        "get_widget_info",
        "blueprint.get_widget_info",
        {"widgetPath": widget_path, "info": "widget"},
    )
    info = sc["data"]["widgetInfo"]
    by_name = {w["name"]: w for w in info.get("widgets", [])}
    return info, by_name


def ensure_panel(
    mcp: McpSession,
    widget_path: str,
    by_name: dict[str, dict[str, Any]],
    slot_name: str,
    widget_kind: str,
    parent_slot: str,
    **extra: Any,
) -> None:
    if slot_name not in by_name:
        params: dict[str, Any] = {
            "widgetPath": widget_path,
            "widgetKind": widget_kind,
            "slotName": slot_name,
            "parentSlot": parent_slot,
        }
        params.update(extra)
        mcp.execute("manage_blueprint", "add_panel_widget", "blueprint.add_panel_widget", params)
        by_name[slot_name] = {"name": slot_name, "parentName": parent_slot, "widgetClass": widget_kind}


def ensure_content(
    mcp: McpSession,
    widget_path: str,
    by_name: dict[str, dict[str, Any]],
    slot_name: str,
    widget_kind: str,
    parent_slot: str,
    **extra: Any,
) -> None:
    if slot_name not in by_name:
        params: dict[str, Any] = {
            "widgetPath": widget_path,
            "widgetKind": widget_kind,
            "slotName": slot_name,
            "parentSlot": parent_slot,
        }
        params.update(extra)
        mcp.execute("manage_blueprint", "add_content_widget", "blueprint.add_content_widget", params)
        by_name[slot_name] = {"name": slot_name, "parentName": parent_slot, "widgetClass": widget_kind}


def reparent_widget(
    mcp: McpSession,
    widget_path: str,
    by_name: dict[str, dict[str, Any]],
    slot_name: str,
    new_parent: str,
    index: int | None = None,
    force_order: bool = False,
) -> None:
    cur = by_name.get(slot_name, {}).get("parentName")
    if cur == new_parent and not force_order:
        return
    params: dict[str, Any] = {
        "edit": "reparent_widget",
        "widgetPath": widget_path,
        "slotName": slot_name,
        "newParent": new_parent,
    }
    if index is not None:
        params["index"] = index
    mcp.execute("manage_blueprint", "edit_widget_blueprint", "blueprint.edit_widget_blueprint", params)
    if slot_name in by_name:
        by_name[slot_name]["parentName"] = new_parent


def remove_widget_if_exists(
    mcp: McpSession,
    widget_path: str,
    by_name: dict[str, dict[str, Any]],
    slot_name: str,
) -> None:
    if slot_name not in by_name:
        return
    mcp.execute(
        "manage_blueprint",
        "remove_widget",
        "blueprint.remove_widget",
        {"widgetPath": widget_path, "slotName": slot_name},
    )
    by_name.pop(slot_name, None)


def clear_event_graph(mcp: McpSession, blueprint_path: str) -> dict[str, int]:
    """Delete all removable EventGraph nodes (retrying single-node on protected stubs)."""
    sc = mcp.execute(
        "manage_blueprint",
        "inspect_graph",
        "blueprint.inspect_graph",
        {"blueprintPath": blueprint_path, "graphName": "EventGraph", "info": "graph", "includePins": False, "limit": 500},
    )
    nodes = sc["data"].get("nodes", [])
    if not nodes:
        return {"initial": 0, "deleted": 0, "remaining": 0}

    non_events = [n["nodeId"] for n in nodes if not str(n.get("nodeName", "")).startswith("K2Node_Event")]
    events = [n["nodeId"] for n in nodes if str(n.get("nodeName", "")).startswith("K2Node_Event")]
    deleted = 0

    for batch_ids in (non_events, events):
        if not batch_ids:
            continue
        try:
            mcp.execute(
                "manage_blueprint",
                "delete_node",
                "blueprint.delete_node",
                {"blueprintPath": blueprint_path, "graphName": "EventGraph", "nodeIds": batch_ids},
            )
            deleted += len(batch_ids)
        except McpError:
            for nid in batch_ids:
                try:
                    mcp.execute(
                        "manage_blueprint",
                        "delete_node",
                        "blueprint.delete_node",
                        {"blueprintPath": blueprint_path, "graphName": "EventGraph", "nodeId": nid},
                    )
                    deleted += 1
                except McpError:
                    # Leave empty/protected Unreal event stub unwired if non-deletable.
                    pass

    after = mcp.execute(
        "manage_blueprint",
        "inspect_graph",
        "blueprint.inspect_graph",
        {"blueprintPath": blueprint_path, "graphName": "EventGraph", "info": "graph", "includePins": False, "limit": 500},
    )
    remaining = len(after["data"].get("nodes", []))
    return {"initial": len(nodes), "deleted": deleted, "remaining": remaining}


# Shared Unreal Python helper library embedded in execute_python styling passes.
UNREAL_STYLE_HELPERS = r'''
import json, unreal

def srgb_hex_to_linear(hex_str):
    s = hex_str.strip().lstrip("#")
    if len(s) == 6:
        s += "FF"
    if len(s) != 8:
        raise ValueError(f"Expected RRGGBB or RRGGBBAA hex, got {hex_str!r}")
    def ch(v):
        c = v / 255.0
        return c / 12.92 if c <= 0.04045 else ((c + 0.055) / 1.055) ** 2.4
    r = ch(int(s[0:2], 16))
    g = ch(int(s[2:4], 16))
    b = ch(int(s[4:6], 16))
    a = int(s[6:8], 16) / 255.0
    return unreal.LinearColor(r, g, b, a)

def make_slate_color(hex_str):
    sc = unreal.SlateColor()
    sc.set_editor_property("specified_color", srgb_hex_to_linear(hex_str))
    sc.set_editor_property("color_use_rule", unreal.SlateColorStylingMode.USE_COLOR_SPECIFIED)
    return sc

def make_margin(l, t, r, b):
    m = unreal.Margin()
    m.set_editor_property("left", float(l))
    m.set_editor_property("top", float(t))
    m.set_editor_property("right", float(r))
    m.set_editor_property("bottom", float(b))
    return m

def make_font(font_asset, size, letter_spacing=0, outline_size=0, outline_hex="#000000FF"):
    fi = unreal.SlateFontInfo()
    fi.set_editor_property("font_object", font_asset)
    fi.set_editor_property("typeface_font_name", "Default")
    fi.set_editor_property("size", int(size))
    fi.set_editor_property("letter_spacing", int(letter_spacing))
    ol = unreal.FontOutlineSettings()
    ol.set_editor_property("outline_size", int(outline_size))
    ol.set_editor_property("outline_color", srgb_hex_to_linear(outline_hex))
    fi.set_editor_property("outline_settings", ol)
    return fi

def make_rounded_brush(fill_hex, outline_hex="#00000000", outline_width=0.0, radius=4.0):
    br = unreal.SlateBrush()
    br.set_editor_property("draw_as", unreal.SlateBrushDrawType.ROUNDED_BOX)
    br.set_editor_property("tint_color", make_slate_color(fill_hex))
    ol = unreal.SlateBrushOutlineSettings()
    ol.set_editor_property("corner_radii", unreal.Vector4(radius, radius, radius, radius))
    ol.set_editor_property("color", make_slate_color(outline_hex))
    ol.set_editor_property("width", float(outline_width))
    ol.set_editor_property("rounding_type", unreal.SlateBrushRoundingType.FIXED_RADIUS)
    ol.set_editor_property("use_brush_transparency", True)
    br.set_editor_property("outline_settings", ol)
    return br

def make_box_brush(fill_hex):
    br = unreal.SlateBrush()
    br.set_editor_property("draw_as", unreal.SlateBrushDrawType.BOX)
    br.set_editor_property("tint_color", make_slate_color(fill_hex))
    return br

def make_no_draw_brush():
    br = unreal.SlateBrush()
    br.set_editor_property("draw_as", unreal.SlateBrushDrawType.NO_DRAW_TYPE)
    br.set_editor_property("tint_color", make_slate_color("#00000000"))
    return br

def make_sound(sound_asset):
    ss = unreal.SlateSound()
    ss.set_editor_property("resource_object", sound_asset)
    return ss

def make_button_style(normal_hex, hovered_hex, pressed_hex,
                      normal_outline="#4A2322CC", hovered_outline="#C93832FF",
                      pressed_outline="#8F1916FF", outline_width=1.0, radius=4.0,
                      click_sound=None, pad=(16, 8, 16, 8)):
    st = unreal.ButtonStyle()
    st.set_editor_property("normal", make_rounded_brush(normal_hex, normal_outline, outline_width, radius))
    st.set_editor_property("hovered", make_rounded_brush(hovered_hex, hovered_outline, outline_width, radius))
    st.set_editor_property("pressed", make_rounded_brush(pressed_hex, pressed_outline, outline_width, radius))
    st.set_editor_property("disabled", make_rounded_brush("#0D0E12AA", "#2A1E1E66", outline_width, radius))
    st.set_editor_property("normal_padding", make_margin(*pad))
    st.set_editor_property("pressed_padding", make_margin(pad[0], pad[1] + 1, pad[2], max(0, pad[3] - 1)))
    if click_sound is not None:
        st.set_editor_property("pressed_slate_sound", make_sound(click_sound))
    return st

def set_canvas_slot(widget, min_x, min_y, max_x, max_y, off_l=0.0, off_t=0.0, off_r=0.0, off_b=0.0, z=0, align=(0.0, 0.0)):
    sl = widget.slot
    anc = unreal.Anchors()
    anc.set_editor_property("minimum", unreal.Vector2D(float(min_x), float(min_y)))
    anc.set_editor_property("maximum", unreal.Vector2D(float(max_x), float(max_y)))
    sl.set_anchors(anc)
    sl.set_alignment(unreal.Vector2D(float(align[0]), float(align[1])))
    sl.set_auto_size(False)
    sl.set_offsets(make_margin(off_l, off_t, off_r, off_b))
    sl.set_z_order(int(z))

def set_vbox_slot(widget, h_align, v_align, fill=None, pad=(0, 0, 0, 0)):
    sl = widget.slot
    sl.set_horizontal_alignment(h_align)
    sl.set_vertical_alignment(v_align)
    sl.set_padding(make_margin(*pad))
    cs = unreal.SlateChildSize()
    if fill is None:
        cs.set_editor_property("size_rule", unreal.SlateSizeRule.AUTOMATIC)
        cs.set_editor_property("value", 1.0)
    else:
        cs.set_editor_property("size_rule", unreal.SlateSizeRule.FILL)
        cs.set_editor_property("value", float(fill))
    sl.set_size(cs)

def set_hbox_slot(widget, h_align, v_align, fill=None, pad=(0, 0, 0, 0)):
    set_vbox_slot(widget, h_align, v_align, fill=fill, pad=pad)

def set_overlay_slot(widget, h_align, v_align, pad=(0, 0, 0, 0)):
    sl = widget.slot
    sl.set_horizontal_alignment(h_align)
    sl.set_vertical_alignment(v_align)
    sl.set_padding(make_margin(*pad))

def set_button_label(button, label_widget, text, font_asset, size, color_hex, style):
    button.set_style(style)
    button.set_background_color(unreal.LinearColor(1.0, 1.0, 1.0, 1.0))
    button.set_color_and_opacity(unreal.LinearColor(1.0, 1.0, 1.0, 1.0))
    button.set_editor_property("is_focusable", True)
    bsl = label_widget.slot
    bsl.set_horizontal_alignment(unreal.HorizontalAlignment.H_ALIGN_CENTER)
    bsl.set_vertical_alignment(unreal.VerticalAlignment.V_ALIGN_CENTER)
    bsl.set_padding(make_margin(4, 2, 4, 2))
    label_widget.set_text(text)
    label_widget.set_font(make_font(font_asset, size))
    label_widget.set_color_and_opacity(make_slate_color(color_hex))
    label_widget.set_shadow_offset(unreal.Vector2D(1.0, 1.0))
    label_widget.set_shadow_color_and_opacity(srgb_hex_to_linear("#000000B0"))
    label_widget.set_editor_property("justification", unreal.TextJustify.CENTER)

def configure_text(tb, text, font_asset, size, color_hex, just=unreal.TextJustify.LEFT, shadow_off=(0.0, 0.0), shadow_hex="#00000000"):
    if text is not None:
        tb.set_text(text)
    tb.set_font(make_font(font_asset, size))
    tb.set_color_and_opacity(make_slate_color(color_hex))
    tb.set_shadow_offset(unreal.Vector2D(float(shadow_off[0]), float(shadow_off[1])))
    tb.set_shadow_color_and_opacity(srgb_hex_to_linear(shadow_hex))
    tb.set_editor_property("justification", just)

def collect_widgets(asset_prefix):
    found = {}
    for w in unreal.ObjectIterator(unreal.Widget):
        p = w.get_path_name()
        if asset_prefix in p:
            found[w.get_name()] = w
    return found
'''


MAIN_MENU_STYLE_CODE = UNREAL_STYLE_HELPERS + r'''
eal = unreal.EditorAssetLibrary
font_bold = eal.load_asset("/Game/UI/Fonts/Cinzel-Bold_Font")
font_reg = eal.load_asset("/Game/UI/Fonts/Cinzel-Regular_Font")
tex_bg = eal.load_asset("/Game/UI/Menu/Textures/Haunted_Radio_Maze")
click_sfx = eal.load_asset("/Game/Audio/SFX/UI/click")

w = collect_widgets("/Game/UI/Menu/MainMenu.MainMenu:WidgetTree.")
required = [
    "CanvasPanel_117", "BgScaleBox", "Image_403", "LeftScrim", "LeftColumn",
    "TitleBox", "TextBlock_0", "SubtitleText", "TitleRuleBox", "TitleRule",
    "LeftSpacer", "LeftBottomSpacer", "VerticalBox_0",
    "PlaySizeBox", "Button_0", "TextBlock_56",
    "OptionsSizeBox", "Button_19", "OptionsButtonText",
    "QuitSizeBox", "Button_43", "QuitButtonText",
]
missing = [k for k in required if k not in w]
if missing:
    raise RuntimeError(f"MainMenu missing expected widgets before styling: {missing}")

w["CanvasPanel_117"].set_visibility(unreal.SlateVisibility.VISIBLE)

# Full-bleed responsive background preserving 1672x941 artwork aspect ratio
bg_sb = w["BgScaleBox"]
bg_sb.set_stretch(unreal.Stretch.SCALE_TO_FILL)
bg_sb.set_stretch_direction(unreal.StretchDirection.BOTH)
bg_sb.set_visibility(unreal.SlateVisibility.SELF_HIT_TEST_INVISIBLE)
set_canvas_slot(bg_sb, 0.0, 0.0, 1.0, 1.0, z=0)

img = w["Image_403"]
img.set_brush_from_texture(tex_bg, match_size=True)
img.set_color_and_opacity(unreal.LinearColor(1.0, 1.0, 1.0, 1.0))
img.set_visibility(unreal.SlateVisibility.VISIBLE)
img.slot.set_horizontal_alignment(unreal.HorizontalAlignment.H_ALIGN_FILL)
img.slot.set_vertical_alignment(unreal.VerticalAlignment.V_ALIGN_FILL)

# Left scrim anchor [0,0..0.42,1.0]
scrim = w["LeftScrim"]
scrim.set_brush(make_box_brush("#07080BCC"))
scrim.set_brush_color(unreal.LinearColor(1.0, 1.0, 1.0, 1.0))
scrim.set_padding(make_margin(64, 72, 48, 72))
scrim.set_horizontal_alignment(unreal.HorizontalAlignment.H_ALIGN_FILL)
scrim.set_vertical_alignment(unreal.VerticalAlignment.V_ALIGN_FILL)
scrim.set_visibility(unreal.SlateVisibility.VISIBLE)
set_canvas_slot(scrim, 0.0, 0.0, 0.42, 1.0, z=1)

# LeftColumn structure: TitleBox (top), LeftSpacer (fill 1.0), VerticalBox_0 (bottom-middle), LeftBottomSpacer (fill 0.22)
w["LeftColumn"].slot.set_horizontal_alignment(unreal.HorizontalAlignment.H_ALIGN_FILL)
w["LeftColumn"].slot.set_vertical_alignment(unreal.VerticalAlignment.V_ALIGN_FILL)
set_vbox_slot(w["TitleBox"], unreal.HorizontalAlignment.H_ALIGN_LEFT, unreal.VerticalAlignment.V_ALIGN_TOP)
configure_text(
    w["TextBlock_0"], "LOST WITHIN", font_bold, 62, "#F0E9D8",
    just=unreal.TextJustify.LEFT, shadow_off=(2.0, 3.0), shadow_hex="#000000D9",
)
set_vbox_slot(w["TextBlock_0"], unreal.HorizontalAlignment.H_ALIGN_LEFT, unreal.VerticalAlignment.V_ALIGN_TOP)

configure_text(
    w["SubtitleText"], "SUBJECT 09  \u00b7  CONTAINMENT BREACH", font_reg, 18, "#B8332E",
    just=unreal.TextJustify.LEFT, shadow_off=(1.0, 2.0), shadow_hex="#000000CC",
)
set_vbox_slot(w["SubtitleText"], unreal.HorizontalAlignment.H_ALIGN_LEFT, unreal.VerticalAlignment.V_ALIGN_TOP, pad=(0, 8, 0, 0))

tr_box = w["TitleRuleBox"]
tr_box.set_width_override(280.0)
tr_box.set_height_override(2.0)
set_vbox_slot(tr_box, unreal.HorizontalAlignment.H_ALIGN_LEFT, unreal.VerticalAlignment.V_ALIGN_TOP, pad=(0, 12, 0, 12))
w["TitleRule"].set_brush(make_box_brush("#8F1916CC"))
w["TitleRule"].set_brush_color(unreal.LinearColor(1.0, 1.0, 1.0, 1.0))
w["TitleRule"].slot.set_horizontal_alignment(unreal.HorizontalAlignment.H_ALIGN_FILL)
w["TitleRule"].slot.set_vertical_alignment(unreal.VerticalAlignment.V_ALIGN_FILL)

w["LeftSpacer"].set_size(unreal.Vector2D(1.0, 1.0))
set_vbox_slot(w["LeftSpacer"], unreal.HorizontalAlignment.H_ALIGN_FILL, unreal.VerticalAlignment.V_ALIGN_FILL, fill=1.0)

set_vbox_slot(w["VerticalBox_0"], unreal.HorizontalAlignment.H_ALIGN_LEFT, unreal.VerticalAlignment.V_ALIGN_BOTTOM)
w["LeftBottomSpacer"].set_size(unreal.Vector2D(1.0, 1.0))
set_vbox_slot(w["LeftBottomSpacer"], unreal.HorizontalAlignment.H_ALIGN_FILL, unreal.VerticalAlignment.V_ALIGN_FILL, fill=0.22)

btn_style = make_button_style(
    normal_hex="#111319E6",
    hovered_hex="#6B1613F5",
    pressed_hex="#450D0CF5",
    normal_outline="#4A2322CC",
    hovered_outline="#C93832FF",
    pressed_outline="#8F1916FF",
    outline_width=1.0,
    radius=4.0,
    click_sound=click_sfx,
)

buttons = [
    ("PlaySizeBox", "Button_0", "TextBlock_56", "PLAY", 16.0),
    ("OptionsSizeBox", "Button_19", "OptionsButtonText", "OPTIONS", 16.0),
    ("QuitSizeBox", "Button_43", "QuitButtonText", "QUIT", 0.0),
]
for box_name, btn_name, txt_name, label, pad_bottom in buttons:
    sb = w[box_name]
    sb.set_width_override(320.0)
    sb.set_height_override(56.0)
    set_vbox_slot(sb, unreal.HorizontalAlignment.H_ALIGN_LEFT, unreal.VerticalAlignment.V_ALIGN_CENTER, pad=(0, 0, 0, pad_bottom))
    sz_slot = w[btn_name].slot
    sz_slot.set_horizontal_alignment(unreal.HorizontalAlignment.H_ALIGN_FILL)
    sz_slot.set_vertical_alignment(unreal.VerticalAlignment.V_ALIGN_FILL)
    set_button_label(w[btn_name], w[txt_name], label, font_bold, 22, "#EFE8D8", btn_style)

wbp = unreal.EditorAssetLibrary.load_asset("/Game/UI/Menu/MainMenu")
wbp.modify()
print(json.dumps({"mainmenu_styled": len(w)}))
'''


OPTIONS_MENU_STYLE_CODE = UNREAL_STYLE_HELPERS + r'''
eal = unreal.EditorAssetLibrary
font_bold = eal.load_asset("/Game/UI/Fonts/Cinzel-Bold_Font")
font_reg = eal.load_asset("/Game/UI/Fonts/Cinzel-Regular_Font")
tex_bg = eal.load_asset("/Game/UI/Menu/Textures/bg_options")
click_sfx = eal.load_asset("/Game/Audio/SFX/UI/click")

w = collect_widgets("/Game/UI/Menu/OptionsMenu.OptionsMenu:WidgetTree.")
required = [
    "CanvasPanel_82", "BgScaleBox", "Image_81", "OptionsVeil", "OptionsPanel", "OptionsMainLayout",
    "TopBarBox", "HorizontalBox_0",
    "Button_1", "TextBlock_12", "Button_3", "TextBlock_319",
    "Button_4", "TextBlock_452", "Button_5", "TextBlock_612",
    "HeaderRuleBox", "HeaderRule", "OptionsBodyOverlay",
    "VB_HowToPlay", "HowToPlayFrame", "GuideScrollBox", "MultiLineEditableTextBox_2",
    "VB_ScreenSettings", "ApplyButtonBox", "Button_0", "TextBlock_0",
    "VB_SoundSettings", "HorizontalBox_3", "SizeBox", "TextBlock_13", "MusicSliderBox", "MusicSlider",
    "MusicPercentBox", "MusicPercentText",
    "HorizontalBox_4", "SizeBox_8", "TextBlock_14", "SfxSliderBox", "SfxSlider",
    "SfxPercentBox", "SfxPercentText",
]
missing = [k for k in required if k not in w]
if missing:
    raise RuntimeError(f"OptionsMenu missing expected widgets before styling: {missing}")

w["CanvasPanel_82"].set_visibility(unreal.SlateVisibility.VISIBLE)

# Full-bleed bg_options preserving 1672x941 aspect ratio
bg_sb = w["BgScaleBox"]
bg_sb.set_stretch(unreal.Stretch.SCALE_TO_FILL)
bg_sb.set_stretch_direction(unreal.StretchDirection.BOTH)
bg_sb.set_visibility(unreal.SlateVisibility.SELF_HIT_TEST_INVISIBLE)
set_canvas_slot(bg_sb, 0.0, 0.0, 1.0, 1.0, z=0)

img = w["Image_81"]
img.set_brush_from_texture(tex_bg, match_size=True)
img.set_color_and_opacity(unreal.LinearColor(1.0, 1.0, 1.0, 1.0))
img.set_visibility(unreal.SlateVisibility.VISIBLE)
img.slot.set_horizontal_alignment(unreal.HorizontalAlignment.H_ALIGN_FILL)
img.slot.set_vertical_alignment(unreal.VerticalAlignment.V_ALIGN_FILL)

# Dark veil
veil = w["OptionsVeil"]
veil.set_brush(make_box_brush("#06070AB8"))
veil.set_brush_color(unreal.LinearColor(1.0, 1.0, 1.0, 1.0))
veil.set_visibility(unreal.SlateVisibility.VISIBLE)
set_canvas_slot(veil, 0.0, 0.0, 1.0, 1.0, z=1)

# Framed panel [0.08, 0.07..0.92, 0.93]
panel = w["OptionsPanel"]
panel.set_brush(make_rounded_brush("#0B0C10EB", outline_hex="#6E1C19B0", outline_width=1.5, radius=6.0))
panel.set_brush_color(unreal.LinearColor(1.0, 1.0, 1.0, 1.0))
panel.set_padding(make_margin(40, 32, 40, 32))
panel.set_horizontal_alignment(unreal.HorizontalAlignment.H_ALIGN_FILL)
panel.set_vertical_alignment(unreal.VerticalAlignment.V_ALIGN_FILL)
panel.set_visibility(unreal.SlateVisibility.VISIBLE)
set_canvas_slot(panel, 0.08, 0.07, 0.92, 0.93, z=2)
w["OptionsMainLayout"].slot.set_horizontal_alignment(unreal.HorizontalAlignment.H_ALIGN_FILL)
w["OptionsMainLayout"].slot.set_vertical_alignment(unreal.VerticalAlignment.V_ALIGN_FILL)

# Top bar (58 px height, 16 px gaps, Cinzel-Bold 19)
top_box = w["TopBarBox"]
top_box.set_height_override(58.0)
top_box.clear_width_override()
set_vbox_slot(top_box, unreal.HorizontalAlignment.H_ALIGN_FILL, unreal.VerticalAlignment.V_ALIGN_TOP, pad=(0, 0, 0, 14))
w["HorizontalBox_0"].slot.set_horizontal_alignment(unreal.HorizontalAlignment.H_ALIGN_FILL)
w["HorizontalBox_0"].slot.set_vertical_alignment(unreal.VerticalAlignment.V_ALIGN_FILL)

tab_inactive = make_button_style(
    normal_hex="#15171EE0", hovered_hex="#571311EB", pressed_hex="#3B0B0AF5",
    normal_outline="#4A2322CC", hovered_outline="#C93832FF", pressed_outline="#8F1916FF",
    outline_width=1.0, radius=4.0, click_sound=click_sfx,
)
tab_active = make_button_style(
    normal_hex="#781815F5", hovered_hex="#8E201CF5", pressed_hex="#5A110FF5",
    normal_outline="#D4423CFF", hovered_outline="#E6524BFF", pressed_outline="#8F1916FF",
    outline_width=1.5, radius=4.0, click_sound=click_sfx,
)

top_buttons = [
    ("Button_1", "TextBlock_12", "< BACK", tab_inactive, 16.0),
    ("Button_3", "TextBlock_319", "HOW TO PLAY", tab_active, 16.0),
    ("Button_4", "TextBlock_452", "SCREEN", tab_inactive, 16.0),
    ("Button_5", "TextBlock_612", "SOUND", tab_inactive, 0.0),
]
for btn_n, txt_n, lbl, st, pad_r in top_buttons:
    set_hbox_slot(w[btn_n], unreal.HorizontalAlignment.H_ALIGN_FILL, unreal.VerticalAlignment.V_ALIGN_FILL, fill=1.0, pad=(0, 0, pad_r, 0))
    set_button_label(w[btn_n], w[txt_n], lbl, font_bold, 19, "#EFE8D8", st)

# Header divider rule
hr_box = w["HeaderRuleBox"]
hr_box.set_height_override(1.0)
hr_box.clear_width_override()
set_vbox_slot(hr_box, unreal.HorizontalAlignment.H_ALIGN_FILL, unreal.VerticalAlignment.V_ALIGN_TOP, pad=(0, 0, 0, 24))
w["HeaderRule"].set_brush(make_box_brush("#3E1C1AA0"))
w["HeaderRule"].set_brush_color(unreal.LinearColor(1.0, 1.0, 1.0, 1.0))
w["HeaderRule"].slot.set_horizontal_alignment(unreal.HorizontalAlignment.H_ALIGN_FILL)
w["HeaderRule"].slot.set_vertical_alignment(unreal.VerticalAlignment.V_ALIGN_FILL)

# Body overlay holding the 3 tab VBs
set_vbox_slot(w["OptionsBodyOverlay"], unreal.HorizontalAlignment.H_ALIGN_FILL, unreal.VerticalAlignment.V_ALIGN_FILL, fill=1.0)

# Tab 0: How To Play
w["VB_HowToPlay"].set_visibility(unreal.SlateVisibility.SELF_HIT_TEST_INVISIBLE)
set_overlay_slot(w["VB_HowToPlay"], unreal.HorizontalAlignment.H_ALIGN_FILL, unreal.VerticalAlignment.V_ALIGN_FILL)

htp_frame = w["HowToPlayFrame"]
htp_frame.set_brush(make_rounded_brush("#0E1016CC", outline_hex="#332020AA", outline_width=1.0, radius=4.0))
htp_frame.set_brush_color(unreal.LinearColor(1.0, 1.0, 1.0, 1.0))
htp_frame.set_padding(make_margin(28, 22, 28, 22))
htp_frame.set_horizontal_alignment(unreal.HorizontalAlignment.H_ALIGN_FILL)
htp_frame.set_vertical_alignment(unreal.VerticalAlignment.V_ALIGN_FILL)
set_vbox_slot(htp_frame, unreal.HorizontalAlignment.H_ALIGN_FILL, unreal.VerticalAlignment.V_ALIGN_FILL, fill=1.0)

sb_style = unreal.ScrollBarStyle()
sb_style.set_editor_property("normal_thumb_image", make_rounded_brush("#353944FF", radius=3.0))
sb_style.set_editor_property("hovered_thumb_image", make_rounded_brush("#8F1916FF", radius=3.0))
sb_style.set_editor_property("dragged_thumb_image", make_rounded_brush("#B8332EFF", radius=3.0))
sb_style.set_editor_property("vertical_background_image", make_box_brush("#0A0C1066"))
sb_style.set_editor_property("horizontal_background_image", make_box_brush("#0A0C1066"))
sb_style.set_editor_property("thickness", 6.0)

gsb = w["GuideScrollBox"]
gsb.set_scroll_bar_visibility(unreal.SlateVisibility.VISIBLE)
gsb.set_always_show_scrollbar(False)
gsb.set_allow_overscroll(False)
gsb.set_animate_wheel_scrolling(True)
gsb.set_wheel_scroll_multiplier(1.0)
gsb.set_scrollbar_thickness(unreal.Vector2D(6.0, 6.0))
gsb.set_scrollbar_padding(make_margin(8, 0, 0, 0))
gsb.set_editor_property("widget_bar_style", sb_style)
gsb.slot.set_horizontal_alignment(unreal.HorizontalAlignment.H_ALIGN_FILL)
gsb.slot.set_vertical_alignment(unreal.VerticalAlignment.V_ALIGN_FILL)

guide = w["MultiLineEditableTextBox_2"]
guide.set_is_read_only(True)
guide.set_editor_property("allow_context_menu", False)
guide.set_editor_property("auto_wrap_text", True)
guide.set_editor_property("wrap_text_at", 0.0)
guide.set_editor_property("wrapping_policy", unreal.TextWrappingPolicy.DEFAULT_WRAPPING)
guide.set_editor_property("justification", unreal.TextJustify.LEFT)
guide.set_foreground_color(srgb_hex_to_linear("#E8E0CEFF"))
guide.slot.set_horizontal_alignment(unreal.HorizontalAlignment.H_ALIGN_FILL)
guide.slot.set_vertical_alignment(unreal.VerticalAlignment.V_ALIGN_FILL)

guide_fi = make_font(font_reg, 18)
guide_ts = unreal.TextBlockStyle()
guide_ts.set_editor_property("font", guide_fi)
guide_ts.set_editor_property("color_and_opacity", make_slate_color("#E8E0CEFF"))
guide_ts.set_editor_property("shadow_offset", unreal.Vector2D(1.0, 1.0))
guide_ts.set_editor_property("shadow_color_and_opacity", srgb_hex_to_linear("#000000A0"))

etb_style = guide.get_editor_property("widget_style")
etb_style.set_editor_property("text_style", guide_ts)
etb_style.set_editor_property("background_image_normal", make_no_draw_brush())
etb_style.set_editor_property("background_image_hovered", make_no_draw_brush())
etb_style.set_editor_property("background_image_focused", make_no_draw_brush())
etb_style.set_editor_property("background_image_read_only", make_no_draw_brush())
etb_style.set_editor_property("background_color", make_slate_color("#00000000"))
etb_style.set_editor_property("foreground_color", make_slate_color("#E8E0CEFF"))
etb_style.set_editor_property("read_only_foreground_color", make_slate_color("#E8E0CEFF"))
etb_style.set_editor_property("focused_foreground_color", make_slate_color("#E8E0CEFF"))
etb_style.set_editor_property("padding", make_margin(0, 0, 12, 0))
etb_style.set_editor_property("scroll_bar_style", sb_style)
guide.set_editor_property("widget_style", etb_style)
guide.set_text_style(guide_ts)

# Tab 1: Screen Settings
w["VB_ScreenSettings"].set_visibility(unreal.SlateVisibility.COLLAPSED)
set_overlay_slot(w["VB_ScreenSettings"], unreal.HorizontalAlignment.H_ALIGN_CENTER, unreal.VerticalAlignment.V_ALIGN_CENTER)

step_btn_style = make_button_style(
    normal_hex="#181B24FF", hovered_hex="#7D1C19FF", pressed_hex="#4D110FFF",
    normal_outline="#4A2322CC", hovered_outline="#D4423CFF", pressed_outline="#8F1916FF",
    outline_width=1.0, radius=4.0, click_sound=click_sfx, pad=(4, 2, 4, 2),
)
apply_btn_style = make_button_style(
    normal_hex="#781815F5", hovered_hex="#99201CFF", pressed_hex="#52100EF5",
    normal_outline="#C93832FF", hovered_outline="#E6524BFF", pressed_outline="#8F1916FF",
    outline_width=1.5, radius=4.0, click_sound=click_sfx, pad=(8, 4, 8, 4),
)

screen_rows = [
    ("ScreenRowSizeBox_0", "ScreenRowBorder_0", "HorizontalBox_36", "SizeBox_0", "TextBlock_48", "WINDOW MODE",
     "ScreenRowSpacer_0", "WinModeMinusBox", "windowmode-", "TextBlock_126",
     "SizeBox_4", "TextBlock_124", "WinModePlusBox", "windowmode+", "TextBlock_127", 12.0),
    ("ScreenRowSizeBox_1", "ScreenRowBorder_1", "HorizontalBox", "SizeBox_1", "TextBlock", "RESOLUTION",
     "ScreenRowSpacer_1", "ResMinusBox", "resolution-", "TextBlock_1",
     "SizeBox_5", "TextBlock_2", "ResPlusBox", "resolution+", "TextBlock_3", 12.0),
    ("ScreenRowSizeBox_2", "ScreenRowBorder_2", "HorizontalBox_1", "SizeBox_2", "TextBlock_4", "GRAPHICS",
     "ScreenRowSpacer_2", "GraphicsMinusBox", "graphics-", "TextBlock_5",
     "SizeBox_6", "TextBlock_6", "GraphicsPlusBox", "graphics+", "TextBlock_7", 12.0),
    ("ScreenRowSizeBox_3", "ScreenRowBorder_3", "HorizontalBox_2", "SizeBox_3", "TextBlock_8", "VSYNC",
     "ScreenRowSpacer_3", "VSyncMinusBox", "vsync-", "TextBlock_9",
     "SizeBox_7", "TextBlock_10", "VSyncPlusBox", "vsync+", "TextBlock_11", 0.0),
]

for (rbox_n, rbrd_n, hbox_n, lbl_box_n, lbl_n, lbl_txt,
     spc_n, mbox_n, mbtn_n, mtxt_n, vbox_n, vtxt_n, pbox_n, pbtn_n, ptxt_n, pad_b) in screen_rows:
    rbox = w[rbox_n]
    rbox.set_width_override(880.0)
    rbox.set_height_override(64.0)
    set_vbox_slot(rbox, unreal.HorizontalAlignment.H_ALIGN_CENTER, unreal.VerticalAlignment.V_ALIGN_CENTER, pad=(0, 0, 0, pad_b))

    rbrd = w[rbrd_n]
    rbrd.set_brush(make_rounded_brush("#11131AD8", outline_hex="#2D1F22B0", outline_width=1.0, radius=4.0))
    rbrd.set_brush_color(unreal.LinearColor(1.0, 1.0, 1.0, 1.0))
    rbrd.set_padding(make_margin(24, 8, 24, 8))
    rbrd.set_horizontal_alignment(unreal.HorizontalAlignment.H_ALIGN_FILL)
    rbrd.set_vertical_alignment(unreal.VerticalAlignment.V_ALIGN_CENTER)
    rbrd.slot.set_horizontal_alignment(unreal.HorizontalAlignment.H_ALIGN_FILL)
    rbrd.slot.set_vertical_alignment(unreal.VerticalAlignment.V_ALIGN_FILL)

    w[hbox_n].slot.set_horizontal_alignment(unreal.HorizontalAlignment.H_ALIGN_FILL)
    w[hbox_n].slot.set_vertical_alignment(unreal.VerticalAlignment.V_ALIGN_CENTER)

    lbox = w[lbl_box_n]
    lbox.set_width_override(260.0)
    lbox.clear_height_override()
    set_hbox_slot(lbox, unreal.HorizontalAlignment.H_ALIGN_LEFT, unreal.VerticalAlignment.V_ALIGN_CENTER)
    w[lbl_n].slot.set_horizontal_alignment(unreal.HorizontalAlignment.H_ALIGN_LEFT)
    w[lbl_n].slot.set_vertical_alignment(unreal.VerticalAlignment.V_ALIGN_CENTER)
    configure_text(w[lbl_n], lbl_txt, font_reg, 20, "#EAE2D0", just=unreal.TextJustify.LEFT)

    w[spc_n].set_size(unreal.Vector2D(1.0, 1.0))
    set_hbox_slot(w[spc_n], unreal.HorizontalAlignment.H_ALIGN_FILL, unreal.VerticalAlignment.V_ALIGN_CENTER, fill=1.0)

    mbox = w[mbox_n]
    mbox.set_width_override(46.0)
    mbox.set_height_override(42.0)
    set_hbox_slot(mbox, unreal.HorizontalAlignment.H_ALIGN_CENTER, unreal.VerticalAlignment.V_ALIGN_CENTER)
    w[mbtn_n].slot.set_horizontal_alignment(unreal.HorizontalAlignment.H_ALIGN_FILL)
    w[mbtn_n].slot.set_vertical_alignment(unreal.VerticalAlignment.V_ALIGN_FILL)
    set_button_label(w[mbtn_n], w[mtxt_n], "<", font_bold, 20, "#EFE8D8", step_btn_style)

    vbox = w[vbox_n]
    vbox.set_width_override(280.0)
    vbox.clear_height_override()
    set_hbox_slot(vbox, unreal.HorizontalAlignment.H_ALIGN_CENTER, unreal.VerticalAlignment.V_ALIGN_CENTER, pad=(8, 0, 8, 0))
    w[vtxt_n].slot.set_horizontal_alignment(unreal.HorizontalAlignment.H_ALIGN_CENTER)
    w[vtxt_n].slot.set_vertical_alignment(unreal.VerticalAlignment.V_ALIGN_CENTER)
    configure_text(w[vtxt_n], None, font_bold, 20, "#F2ECE0", just=unreal.TextJustify.CENTER)

    pbox = w[pbox_n]
    pbox.set_width_override(46.0)
    pbox.set_height_override(42.0)
    set_hbox_slot(pbox, unreal.HorizontalAlignment.H_ALIGN_CENTER, unreal.VerticalAlignment.V_ALIGN_CENTER)
    w[pbtn_n].slot.set_horizontal_alignment(unreal.HorizontalAlignment.H_ALIGN_FILL)
    w[pbtn_n].slot.set_vertical_alignment(unreal.VerticalAlignment.V_ALIGN_FILL)
    set_button_label(w[pbtn_n], w[ptxt_n], ">", font_bold, 20, "#EFE8D8", step_btn_style)

ap_box = w["ApplyButtonBox"]
ap_box.set_width_override(320.0)
ap_box.set_height_override(54.0)
set_vbox_slot(ap_box, unreal.HorizontalAlignment.H_ALIGN_CENTER, unreal.VerticalAlignment.V_ALIGN_CENTER, pad=(0, 24, 0, 0))
w["Button_0"].slot.set_horizontal_alignment(unreal.HorizontalAlignment.H_ALIGN_FILL)
w["Button_0"].slot.set_vertical_alignment(unreal.VerticalAlignment.V_ALIGN_FILL)
set_button_label(w["Button_0"], w["TextBlock_0"], "APPLY", font_bold, 20, "#F4EFE4", apply_btn_style)

# Tab 2: Sound Settings
w["VB_SoundSettings"].set_visibility(unreal.SlateVisibility.COLLAPSED)
set_overlay_slot(w["VB_SoundSettings"], unreal.HorizontalAlignment.H_ALIGN_CENTER, unreal.VerticalAlignment.V_ALIGN_CENTER)

sound_rows = [
    ("SoundRowSizeBox_0", "SoundRowBorder_0", "HorizontalBox_3", "SizeBox", "TextBlock_13", "MUSIC",
     "SoundRowSpacer_0", "MusicSliderBox", "MusicSlider", "MusicPercentBox", "MusicPercentText", 16.0),
    ("SoundRowSizeBox_1", "SoundRowBorder_1", "HorizontalBox_4", "SizeBox_8", "TextBlock_14", "SFX",
     "SoundRowSpacer_1", "SfxSliderBox", "SfxSlider", "SfxPercentBox", "SfxPercentText", 0.0),
]

for (rbox_n, rbrd_n, hbox_n, lbl_box_n, lbl_n, lbl_txt,
     spc_n, sbox_n, sld_n, pbox_n, ptxt_n, pad_b) in sound_rows:
    rbox = w[rbox_n]
    rbox.set_width_override(780.0)
    rbox.set_height_override(74.0)
    set_vbox_slot(rbox, unreal.HorizontalAlignment.H_ALIGN_CENTER, unreal.VerticalAlignment.V_ALIGN_CENTER, pad=(0, 0, 0, pad_b))

    rbrd = w[rbrd_n]
    rbrd.set_brush(make_rounded_brush("#11131AD8", outline_hex="#2D1F22B0", outline_width=1.0, radius=4.0))
    rbrd.set_brush_color(unreal.LinearColor(1.0, 1.0, 1.0, 1.0))
    rbrd.set_padding(make_margin(24, 12, 24, 12))
    rbrd.set_horizontal_alignment(unreal.HorizontalAlignment.H_ALIGN_FILL)
    rbrd.set_vertical_alignment(unreal.VerticalAlignment.V_ALIGN_CENTER)
    rbrd.slot.set_horizontal_alignment(unreal.HorizontalAlignment.H_ALIGN_FILL)
    rbrd.slot.set_vertical_alignment(unreal.VerticalAlignment.V_ALIGN_FILL)

    w[hbox_n].slot.set_horizontal_alignment(unreal.HorizontalAlignment.H_ALIGN_FILL)
    w[hbox_n].slot.set_vertical_alignment(unreal.VerticalAlignment.V_ALIGN_CENTER)

    lbox = w[lbl_box_n]
    lbox.set_width_override(180.0)
    lbox.clear_height_override()
    set_hbox_slot(lbox, unreal.HorizontalAlignment.H_ALIGN_LEFT, unreal.VerticalAlignment.V_ALIGN_CENTER)
    w[lbl_n].slot.set_horizontal_alignment(unreal.HorizontalAlignment.H_ALIGN_LEFT)
    w[lbl_n].slot.set_vertical_alignment(unreal.VerticalAlignment.V_ALIGN_CENTER)
    configure_text(w[lbl_n], lbl_txt, font_reg, 20, "#EAE2D0", just=unreal.TextJustify.LEFT)

    w[spc_n].set_size(unreal.Vector2D(1.0, 1.0))
    set_hbox_slot(w[spc_n], unreal.HorizontalAlignment.H_ALIGN_FILL, unreal.VerticalAlignment.V_ALIGN_CENTER, fill=1.0)

    sbox = w[sbox_n]
    sbox.set_width_override(380.0)
    sbox.set_height_override(36.0)
    set_hbox_slot(sbox, unreal.HorizontalAlignment.H_ALIGN_CENTER, unreal.VerticalAlignment.V_ALIGN_CENTER)

    sld = w[sld_n]
    sld.slot.set_horizontal_alignment(unreal.HorizontalAlignment.H_ALIGN_FILL)
    sld.slot.set_vertical_alignment(unreal.VerticalAlignment.V_ALIGN_CENTER)
    sst = sld.get_editor_property("widget_style")
    ds = unreal.Vector2f()
    ds.import_text("(X=380.0,Y=32.0)")
    sst.set_editor_property("desired_size", ds)
    sst.set_editor_property("slider_background_color", srgb_hex_to_linear("#1A1D24FF"))
    sst.set_editor_property("slider_background_accent_color", srgb_hex_to_linear("#2C303CFF"))
    sst.set_editor_property("slider_value_main_color", srgb_hex_to_linear("#A1221DFF"))
    sst.set_editor_property("slider_handle_main_color", srgb_hex_to_linear("#F0E8D6FF"))
    sst.set_editor_property("slider_handle_outline_color", srgb_hex_to_linear("#6E1C19FF"))
    sld.set_editor_property("widget_style", sst)

    pbox = w[pbox_n]
    pbox.set_width_override(84.0)
    pbox.clear_height_override()
    set_hbox_slot(pbox, unreal.HorizontalAlignment.H_ALIGN_RIGHT, unreal.VerticalAlignment.V_ALIGN_CENTER, pad=(12, 0, 0, 0))
    w[ptxt_n].slot.set_horizontal_alignment(unreal.HorizontalAlignment.H_ALIGN_RIGHT)
    w[ptxt_n].slot.set_vertical_alignment(unreal.VerticalAlignment.V_ALIGN_CENTER)
    configure_text(w[ptxt_n], "100%", font_bold, 19, "#EAE2D0", just=unreal.TextJustify.RIGHT)

wbp = unreal.EditorAssetLibrary.load_asset("/Game/UI/Menu/OptionsMenu")
wbp.modify()
print(json.dumps({"optionsmenu_styled": len(w)}))
'''


def rebuild_main_menu(mcp: McpSession) -> dict[str, Any]:
    path = MENU_PATHS["main"]
    info, by_name = get_widget_map(mcp, path)

    # 1. Verify ULostWithinMainMenu exists and reparent if needed.
    cls_probe = mcp.execute_python(
        'import json, unreal; '
        'c = unreal.load_class(None, "/Script/LostWithin.LostWithinMainMenu"); '
        'print(json.dumps({"exists": c is not None}))'
    )
    cls_res = json.loads(cls_probe["output"])
    if not cls_res.get("exists"):
        raise McpError(
            "Native class /Script/LostWithin.LostWithinMainMenu is not loaded in the editor yet. "
            "Run the Wave 3 full build & reopen editor first."
        )

    reparented = False
    if info.get("parentClass") != "LostWithinMainMenu":
        mcp.execute(
            "manage_blueprint",
            "edit_widget_blueprint",
            "blueprint.edit_widget_blueprint",
            {
                "edit": "set_parent_class",
                "widgetPath": path,
                "parentClass": "/Script/LostWithin.LostWithinMainMenu",
            },
        )
        reparented = True

    root = info.get("rootWidget") or "CanvasPanel_117"

    # 2. Clean redundant button image children BEFORE adding button text children.
    remove_widget_if_exists(mcp, path, by_name, "OptionsButtonArt")
    remove_widget_if_exists(mcp, path, by_name, "QuitButtonArt")

    # 3. Create new structure panels cleanly & reparent preserved widgets out of old ScaleBoxes.
    ensure_panel(mcp, path, by_name, "BgScaleBox", "scale_box", root, stretch="ScaleToFill", stretchDirection="Both")
    reparent_widget(mcp, path, by_name, "Image_403", "BgScaleBox", index=0)

    ensure_panel(mcp, path, by_name, "LeftScrim", "border", root)
    ensure_panel(mcp, path, by_name, "LeftColumn", "vertical_box", "LeftScrim")
    ensure_panel(mcp, path, by_name, "TitleBox", "vertical_box", "LeftColumn")
    reparent_widget(mcp, path, by_name, "TextBlock_0", "TitleBox", index=0)
    ensure_content(mcp, path, by_name, "SubtitleText", "text_block", "TitleBox", text="SUBJECT 09  \u00b7  CONTAINMENT BREACH")
    ensure_panel(mcp, path, by_name, "TitleRuleBox", "size_box", "TitleBox", widthOverride=280.0, heightOverride=2.0)
    ensure_panel(mcp, path, by_name, "TitleRule", "border", "TitleRuleBox")

    ensure_panel(mcp, path, by_name, "LeftSpacer", "spacer", "LeftColumn")
    reparent_widget(mcp, path, by_name, "VerticalBox_0", "LeftColumn", index=2, force_order=True)
    ensure_panel(mcp, path, by_name, "LeftBottomSpacer", "spacer", "LeftColumn")

    ensure_panel(mcp, path, by_name, "PlaySizeBox", "size_box", "VerticalBox_0", widthOverride=320.0, heightOverride=56.0)
    ensure_panel(mcp, path, by_name, "OptionsSizeBox", "size_box", "VerticalBox_0", widthOverride=320.0, heightOverride=56.0)
    ensure_panel(mcp, path, by_name, "QuitSizeBox", "size_box", "VerticalBox_0", widthOverride=320.0, heightOverride=56.0)

    reparent_widget(mcp, path, by_name, "Button_0", "PlaySizeBox", index=0)
    reparent_widget(mcp, path, by_name, "Button_19", "OptionsSizeBox", index=0)
    reparent_widget(mcp, path, by_name, "Button_43", "QuitSizeBox", index=0)

    ensure_content(mcp, path, by_name, "OptionsButtonText", "text_block", "Button_19", text="OPTIONS")
    ensure_content(mcp, path, by_name, "QuitButtonText", "text_block", "Button_43", text="QUIT")

    # Enforce deterministic child ordering inside containers on re-runs.
    reparent_widget(mcp, path, by_name, "BgScaleBox", root, index=0, force_order=True)
    reparent_widget(mcp, path, by_name, "LeftScrim", root, index=1, force_order=True)
    for idx, child in enumerate(["TitleBox", "LeftSpacer", "VerticalBox_0", "LeftBottomSpacer"]):
        reparent_widget(mcp, path, by_name, child, "LeftColumn", index=idx, force_order=True)
    for idx, child in enumerate(["TextBlock_0", "SubtitleText", "TitleRuleBox"]):
        reparent_widget(mcp, path, by_name, child, "TitleBox", index=idx, force_order=True)
    for idx, child in enumerate(["PlaySizeBox", "OptionsSizeBox", "QuitSizeBox"]):
        reparent_widget(mcp, path, by_name, child, "VerticalBox_0", index=idx, force_order=True)

    # 4. Safe to remove emptied ScaleBoxes now.
    remove_widget_if_exists(mcp, path, by_name, "ScaleBox_56")
    remove_widget_if_exists(mcp, path, by_name, "ScaleBox_99")

    # 5. Apply Slate brushes, fonts, colors, slots, and sizes.
    style_res = mcp.execute_python(MAIN_MENU_STYLE_CODE)

    # 6. Clear redundant EventGraph nodes and compile + save.
    graph_stats = clear_event_graph(mcp, path)
    mcp.execute(
        "manage_blueprint",
        "compile",
        "blueprint.compile",
        {"blueprintPath": path, "saveAfterCompile": True},
    )
    final_info, _ = get_widget_map(mcp, path)
    return {
        "reparented": reparented,
        "parentClass": final_info.get("parentClass"),
        "widgetCount": len(final_info.get("widgets", [])),
        "style": json.loads(style_res.get("output", "{}")),
        "graph": graph_stats,
    }


def rebuild_options_menu(mcp: McpSession) -> dict[str, Any]:
    path = MENU_PATHS["options"]
    info, by_name = get_widget_map(mcp, path)

    # 1. Strip dead EventGraph nodes, legacy functions, and unused BP variables first.
    graph_before = clear_event_graph(mcp, path)

    removed_vars: list[str] = []
    try:
        mcp.execute(
            "manage_blueprint",
            "remove_variable",
            "blueprint.remove_variable",
            {"blueprintPath": path, "variableNames": LEGACY_OPTIONS_VARIABLES},
        )
        removed_vars = list(LEGACY_OPTIONS_VARIABLES)
    except McpError:
        for vname in LEGACY_OPTIONS_VARIABLES:
            try:
                mcp.execute(
                    "manage_blueprint",
                    "remove_variable",
                    "blueprint.remove_variable",
                    {"blueprintPath": path, "variableName": vname},
                )
                removed_vars.append(vname)
            except McpError:
                pass

    removed_funcs: list[str] = []
    for fname in LEGACY_OPTIONS_FUNCTIONS:
        try:
            mcp.execute(
                "manage_blueprint",
                "remove_function",
                "blueprint.remove_function",
                {"blueprintPath": path, "kind": "function", "functionName": fname},
            )
            removed_funcs.append(fname)
        except McpError:
            pass

    # Re-sweep EventGraph in case variable deletion left detached nodes.
    graph_after = clear_event_graph(mcp, path)

    # 2. Build framed responsive hierarchy preserving every C++-bound widget name.
    root = info.get("rootWidget") or "CanvasPanel_82"
    ensure_panel(mcp, path, by_name, "BgScaleBox", "scale_box", root, stretch="ScaleToFill", stretchDirection="Both")
    reparent_widget(mcp, path, by_name, "Image_81", "BgScaleBox", index=0)

    ensure_panel(mcp, path, by_name, "OptionsVeil", "border", root)
    ensure_panel(mcp, path, by_name, "OptionsPanel", "border", root)
    ensure_panel(mcp, path, by_name, "OptionsMainLayout", "vertical_box", "OptionsPanel")

    ensure_panel(mcp, path, by_name, "TopBarBox", "size_box", "OptionsMainLayout", heightOverride=58.0)
    reparent_widget(mcp, path, by_name, "HorizontalBox_0", "TopBarBox", index=0)

    ensure_panel(mcp, path, by_name, "HeaderRuleBox", "size_box", "OptionsMainLayout", heightOverride=1.0)
    ensure_panel(mcp, path, by_name, "HeaderRule", "border", "HeaderRuleBox")

    ensure_panel(mcp, path, by_name, "OptionsBodyOverlay", "overlay", "OptionsMainLayout")

    # Tab 0: How To Play
    reparent_widget(mcp, path, by_name, "VB_HowToPlay", "OptionsBodyOverlay", index=0, force_order=True)
    ensure_panel(mcp, path, by_name, "HowToPlayFrame", "border", "VB_HowToPlay")
    ensure_panel(
        mcp, path, by_name, "GuideScrollBox", "scroll_box", "HowToPlayFrame",
        orientation="Vertical", scrollBarVisibility="Visible", alwaysShowScrollbar=False,
    )
    reparent_widget(mcp, path, by_name, "MultiLineEditableTextBox_2", "GuideScrollBox", index=0, force_order=True)

    # Tab 1: Screen Settings
    reparent_widget(mcp, path, by_name, "VB_ScreenSettings", "OptionsBodyOverlay", index=1, force_order=True)
    screen_row_specs = [
        ("ScreenRowSizeBox_0", "ScreenRowBorder_0", "HorizontalBox_36", "SizeBox_0",
         "ScreenRowSpacer_0", "WinModeMinusBox", "windowmode-", "SizeBox_4", "WinModePlusBox", "windowmode+"),
        ("ScreenRowSizeBox_1", "ScreenRowBorder_1", "HorizontalBox", "SizeBox_1",
         "ScreenRowSpacer_1", "ResMinusBox", "resolution-", "SizeBox_5", "ResPlusBox", "resolution+"),
        ("ScreenRowSizeBox_2", "ScreenRowBorder_2", "HorizontalBox_1", "SizeBox_2",
         "ScreenRowSpacer_2", "GraphicsMinusBox", "graphics-", "SizeBox_6", "GraphicsPlusBox", "graphics+"),
        ("ScreenRowSizeBox_3", "ScreenRowBorder_3", "HorizontalBox_2", "SizeBox_3",
         "ScreenRowSpacer_3", "VSyncMinusBox", "vsync-", "SizeBox_7", "VSyncPlusBox", "vsync+"),
    ]
    for r_idx, (rbox, rbrd, hbox, lbox, spc, mbox, mbtn, vbox, pbox, pbtn) in enumerate(screen_row_specs):
        ensure_panel(mcp, path, by_name, rbox, "size_box", "VB_ScreenSettings", widthOverride=880.0, heightOverride=64.0)
        ensure_panel(mcp, path, by_name, rbrd, "border", rbox)
        reparent_widget(mcp, path, by_name, hbox, rbrd, index=0)
        ensure_panel(mcp, path, by_name, spc, "spacer", hbox)
        ensure_panel(mcp, path, by_name, mbox, "size_box", hbox, widthOverride=46.0, heightOverride=42.0)
        reparent_widget(mcp, path, by_name, mbtn, mbox, index=0)
        ensure_panel(mcp, path, by_name, pbox, "size_box", hbox, widthOverride=46.0, heightOverride=42.0)
        reparent_widget(mcp, path, by_name, pbtn, pbox, index=0)
        for c_idx, child in enumerate([lbox, spc, mbox, vbox, pbox]):
            reparent_widget(mcp, path, by_name, child, hbox, index=c_idx, force_order=True)
        reparent_widget(mcp, path, by_name, rbox, "VB_ScreenSettings", index=r_idx, force_order=True)

    ensure_panel(mcp, path, by_name, "ApplyButtonBox", "size_box", "VB_ScreenSettings", widthOverride=320.0, heightOverride=54.0)
    reparent_widget(mcp, path, by_name, "Button_0", "ApplyButtonBox", index=0)
    reparent_widget(mcp, path, by_name, "ApplyButtonBox", "VB_ScreenSettings", index=4, force_order=True)

    # Tab 2: Sound Settings
    reparent_widget(mcp, path, by_name, "VB_SoundSettings", "OptionsBodyOverlay", index=2, force_order=True)
    sound_row_specs = [
        ("SoundRowSizeBox_0", "SoundRowBorder_0", "HorizontalBox_3", "SizeBox",
         "SoundRowSpacer_0", "MusicSliderBox", "MusicSlider", "MusicPercentBox", "MusicPercentText", 0),
        ("SoundRowSizeBox_1", "SoundRowBorder_1", "HorizontalBox_4", "SizeBox_8",
         "SoundRowSpacer_1", "SfxSliderBox", "SfxSlider", "SfxPercentBox", "SfxPercentText", 1),
    ]
    for rbox, rbrd, hbox, lbox, spc, sbox, sld, pbox, ptxt, r_idx in sound_row_specs:
        ensure_panel(mcp, path, by_name, rbox, "size_box", "VB_SoundSettings", widthOverride=780.0, heightOverride=74.0)
        ensure_panel(mcp, path, by_name, rbrd, "border", rbox)
        reparent_widget(mcp, path, by_name, hbox, rbrd, index=0)
        ensure_panel(mcp, path, by_name, spc, "spacer", hbox)
        ensure_panel(mcp, path, by_name, sbox, "size_box", hbox, widthOverride=380.0, heightOverride=36.0)
        reparent_widget(mcp, path, by_name, sld, sbox, index=0)
        ensure_panel(mcp, path, by_name, pbox, "size_box", hbox, widthOverride=84.0)
        ensure_content(mcp, path, by_name, ptxt, "text_block", pbox, text="100%")
        for c_idx, child in enumerate([lbox, spc, sbox, pbox]):
            reparent_widget(mcp, path, by_name, child, hbox, index=c_idx, force_order=True)
        reparent_widget(mcp, path, by_name, rbox, "VB_SoundSettings", index=r_idx, force_order=True)

    # Enforce deterministic top-level ordering inside root and main layout.
    for idx, child in enumerate(["BgScaleBox", "OptionsVeil", "OptionsPanel"]):
        reparent_widget(mcp, path, by_name, child, root, index=idx, force_order=True)
    for idx, child in enumerate(["TopBarBox", "HeaderRuleBox", "OptionsBodyOverlay"]):
        reparent_widget(mcp, path, by_name, child, "OptionsMainLayout", index=idx, force_order=True)
    for idx, child in enumerate(["Button_1", "Button_3", "Button_4", "Button_5"]):
        reparent_widget(mcp, path, by_name, child, "HorizontalBox_0", index=idx, force_order=True)

    # 3. Remove obsolete ScaleBoxes & slider inner HorizontalBoxes after moving children out.
    for dead_widget in [
        "ScaleBox_39",
        "ScaleBox_87",
        "ScaleBox_119",
        "ScaleBox_148",
        "HorizontalBox_332",
        "HorizontalBox_5",
    ]:
        remove_widget_if_exists(mcp, path, by_name, dead_widget)

    # 4. Apply Slate brushes, fonts, colors, slider styles, and slots, then compile + save.
    style_res = mcp.execute_python(OPTIONS_MENU_STYLE_CODE)
    mcp.execute(
        "manage_blueprint",
        "compile",
        "blueprint.compile",
        {"blueprintPath": path, "saveAfterCompile": True},
    )
    final_info, _ = get_widget_map(mcp, path)
    return {
        "parentClass": final_info.get("parentClass"),
        "widgetCount": len(final_info.get("widgets", [])),
        "removedVariables": removed_vars,
        "removedFunctions": removed_funcs,
        "graphBefore": graph_before,
        "graphAfter": graph_after,
        "style": json.loads(style_res.get("output", "{}")),
    }


def render_previews(mcp: McpSession, project_dir: Path, targets: list[str]) -> list[str]:
    out_dir = project_dir / "Saved" / "Temp" / "MenuFix" / "Previews"
    out_dir.mkdir(parents=True, exist_ok=True)
    saved: list[str] = []
    matrix = {
        "main": [("MainMenu", MENU_PATHS["main"], ["1920x1080", "2560x1080", "1280x720"])],
        "options": [("OptionsMenu", MENU_PATHS["options"], ["1920x1080", "1280x720"])],
    }
    for key in targets:
        for label, asset_path, resolutions in matrix[key]:
            for res in resolutions:
                sc = mcp.execute(
                    "manage_blueprint",
                    "preview_widget",
                    "blueprint.preview_widget",
                    {"widgetPath": asset_path, "resolution": res, "waitForShaders": True},
                )
                data = sc.get("data", {})
                img_b64 = data.get("imageBase64")
                if not img_b64:
                    raise McpError(f"preview_widget returned no imageBase64 for {asset_path} @ {res}")
                out_path = out_dir / f"{label}_{res}.png"
                out_path.write_bytes(base64.b64decode(img_b64))
                saved.append(str(out_path))
    return saved


def run_preflight(mcp: McpSession) -> dict[str, Any]:
    state = mcp.execute(
        "inspect",
        "get_editor_state",
        "inspect.get_editor_state",
        {"editorState": "world_settings"},
    )["data"]
    if state.get("isPlayInEditor") or state.get("isPIEActive"):
        raise McpError("PIE is active in Unreal Editor. Stop Play before modifying menu assets.")

    probe_code = (
        "import json, unreal\n"
        f"assets = {json.dumps(REQUIRED_ASSETS)}\n"
        "eal = unreal.EditorAssetLibrary\n"
        "print(json.dumps({\n"
        "  'assets_exist': {p: bool(eal.does_asset_exist(p)) for p in assets},\n"
        "  'has_LostWithinMainMenu': unreal.load_class(None, '/Script/LostWithin.LostWithinMainMenu') is not None,\n"
        "  'has_LostWithinOptionsMenu': unreal.load_class(None, '/Script/LostWithin.LostWithinOptionsMenu') is not None,\n"
        "  'dirty_content': [str(x.get_path_name()) for x in unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages()],\n"
        "  'dirty_maps': [str(x.get_path_name()) for x in unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages()],\n"
        "}))\n"
    )
    py_data = json.loads(mcp.execute_python(probe_code)["output"])
    missing_assets = [k for k, ok in py_data["assets_exist"].items() if not ok]
    if missing_assets:
        raise McpError(f"Missing required Unreal assets: {missing_assets}")

    main_info, _ = get_widget_map(mcp, MENU_PATHS["main"])
    opt_info, _ = get_widget_map(mcp, MENU_PATHS["options"])
    main_graph = mcp.execute(
        "manage_blueprint",
        "inspect_graph",
        "blueprint.inspect_graph",
        {"blueprintPath": MENU_PATHS["main"], "graphName": "EventGraph", "info": "graph", "includePins": False, "limit": 500},
    )["data"]
    opt_graph = mcp.execute(
        "manage_blueprint",
        "inspect_graph",
        "blueprint.inspect_graph",
        {"blueprintPath": MENU_PATHS["options"], "graphName": "EventGraph", "info": "graph", "includePins": False, "limit": 500},
    )["data"]

    return {
        "editorWorld": state.get("worldName"),
        "isPlayInEditor": state.get("isPlayInEditor"),
        "isPIEActive": state.get("isPIEActive"),
        "dirtyContent": py_data["dirty_content"],
        "dirtyMaps": py_data["dirty_maps"],
        "hasLostWithinMainMenu": py_data["has_LostWithinMainMenu"],
        "hasLostWithinOptionsMenu": py_data["has_LostWithinOptionsMenu"],
        "mainMenu": {
            "parentClass": main_info.get("parentClass"),
            "widgetCount": len(main_info.get("widgets", [])),
            "eventGraphNodes": main_graph.get("totalCount", len(main_graph.get("nodes", []))),
        },
        "optionsMenu": {
            "parentClass": opt_info.get("parentClass"),
            "widgetCount": len(opt_info.get("widgets", [])),
            "eventGraphNodes": opt_graph.get("totalCount", len(opt_graph.get("nodes", []))),
        },
    }


def main(argv: list[str] | None = None) -> int:
    default_project = Path(__file__).resolve().parents[2]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--project-dir", type=Path, default=default_project)
    parser.add_argument(
        "--mcp-url",
        default=os.environ.get("UNREAL_MCP_URL", "http://127.0.0.1:7600/mcp"),
    )
    parser.add_argument(
        "--target",
        choices=("all", "main", "options", "preview", "verify"),
        default="all",
    )
    parser.add_argument("--dry-run", action="store_true", help="Inspect read-only without copying backups or modifying assets.")
    parser.add_argument("--skip-preview", action="store_true", help="Skip rendering preview_widget PNGs.")
    parser.add_argument("--force-backup", action="store_true", help="Write an additional timestamped backup alongside the baseline.")
    args = parser.parse_args(argv)

    project_dir = args.project_dir.resolve()
    uproject = project_dir / "LostWithin.uproject"
    if not uproject.is_file():
        print(f"ERROR: LostWithin.uproject not found under {project_dir}", file=sys.stderr)
        return 2

    with McpSession(project_dir, args.mcp_url) as mcp:
        preflight = run_preflight(mcp)
        report: dict[str, Any] = {"preflight": preflight, "dryRun": args.dry_run, "target": args.target}

        if args.dry_run or args.target == "verify":
            print(json.dumps(report, indent=2))
            return 0

        if args.target in ("all", "main", "options"):
            backup_dir = backup_menu_assets(project_dir, force_snapshot=args.force_backup)
            report["backupDir"] = str(backup_dir)

        if args.target in ("all", "main"):
            report["main"] = rebuild_main_menu(mcp)
        if args.target in ("all", "options"):
            report["options"] = rebuild_options_menu(mcp)

        if args.target == "preview":
            preview_targets = ["main", "options"]
        elif args.target in ("all", "main", "options") and not args.skip_preview:
            preview_targets = ["main", "options"] if args.target == "all" else [args.target]
        else:
            preview_targets = []

        if preview_targets:
            report["previews"] = render_previews(mcp, project_dir, preview_targets)

        report["postcheck"] = run_preflight(mcp)
        print(json.dumps(report, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
