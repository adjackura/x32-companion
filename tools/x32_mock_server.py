#!/usr/bin/env python3
"""Virtual Behringer X32 console (UDP OSC port 10023 + optional HTTP control dashboard)."""

import argparse
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
import math
import re
import socket
import struct
import threading
import time
from typing import Any, Dict, List, Optional, Tuple


def align4(n: int) -> int:
    return (n + 3) & ~3


def osc_pad_str(s: str) -> bytes:
    raw = s.encode("utf-8") + b"\x00"
    pad = align4(len(raw)) - len(raw)
    return raw + (b"\x00" * pad)


def osc_encode(addr: str, types: str = "", *args: Any) -> bytes:
    out = bytearray(osc_pad_str(addr))
    if not types:
        return bytes(out)
    out.extend(osc_pad_str("," + types))
    for t, val in zip(types, args):
        if t == "i":
            out.extend(struct.pack(">i", int(val)))
        elif t == "f":
            out.extend(struct.pack(">f", float(val)))
        elif t == "s":
            out.extend(osc_pad_str(str(val)))
        elif t == "b":
            b = bytes(val)
            out.extend(struct.pack(">I", len(b)))
            out.extend(b)
            pad = align4(len(b)) - len(b)
            out.extend(b"\x00" * pad)
    return bytes(out)


def osc_decode(pkt: bytes) -> Tuple[str, str, List[Any]]:
    nul = pkt.find(b"\x00")
    if nul < 0:
        raise ValueError("Missing address NUL")
    addr = pkt[:nul].decode("utf-8", errors="replace")
    off = align4(nul + 1)
    if off >= len(pkt):
        return addr, "", []
    if pkt[off : off + 1] != b",":
        raise ValueError("Missing type tag comma")
    tnul = pkt.find(b"\x00", off)
    if tnul < 0:
        raise ValueError("Missing type tag NUL")
    types = pkt[off + 1 : tnul].decode("utf-8", errors="replace")
    off = align4(tnul + 1)
    args: List[Any] = []
    for t in types:
        if t == "i":
            args.append(struct.unpack(">i", pkt[off : off + 4])[0])
            off += 4
        elif t == "f":
            args.append(struct.unpack(">f", pkt[off : off + 4])[0])
            off += 4
        elif t == "s":
            snul = pkt.find(b"\x00", off)
            s = pkt[off:snul].decode("utf-8", errors="replace")
            off = align4(snul + 1)
            args.append(s)
        elif t == "b":
            blen = struct.unpack(">I", pkt[off : off + 4])[0]
            off += 4
            args.append(pkt[off : off + blen])
            off = align4(off + blen)
    return addr, types, args


def resolve_advertise_ip(configured_ip: str, peer_ip: str) -> str:
    if configured_ip and configured_ip != "auto":
        return configured_ip
    if peer_ip.startswith("127."):
        return "127.0.0.1"
    probe_target = peer_ip if peer_ip and peer_ip != "255.255.255.255" else "8.8.8.8"
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try:
        s.connect((probe_target, 10023))
        return s.getsockname()[0]
    except OSError:
        return "127.0.0.1"
    finally:
        s.close()


_CH_MIX_RE = re.compile(r"^/(ch|bus)/(\d{2})/mix/(fader|on)$")

_BAND_CH = [
    ("Kick In", 1, 2), ("Kick Out", 1, 3), ("Snare Top", 1, 4), ("Snare Bot", 1, 5),
    ("Hi-Hat", 1, 9), ("Rack Tom", 1, 6), ("Floor Tom", 1, 8), ("OH L", 1, 48),
    ("OH R", 1, 49), ("Bass DI", 2, 17), ("Bass Amp", 2, 24), ("EGtr 1 L", 3, 20),
    ("EGtr 1 R", 3, 21), ("AcGtr", 3, 23), ("Keys L", 4, 27), ("Keys R", 4, 30),
    ("Tracks L", 5, 60), ("Tracks R", 5, 60), ("Click", 5, 12), ("Guide", 5, 45),
    ("Lead Vox", 6, 41), ("BVox 1", 6, 42), ("BVox 2", 7, 42), ("BVox 3", 7, 43),
    ("Host Mic", 7, 50), ("Guest 1", 7, 50), ("Guest 2", 7, 52), ("FX Plate", 5, 61),
    ("FX Hall", 5, 61), ("FX Delay", 5, 61), ("Room L", 4, 48), ("Room R", 4, 49),
]
_BAND_DCA = ["DRUMS", "BASS", "GUITARS", "KEYS", "TRACKS", "LEAD VOX", "BG VOX", "FX"]

_WORSHIP_CH = [
    ("Kick", 1, 2), ("Snare", 1, 4), ("Hat", 1, 9), ("Tom 1", 1, 6),
    ("Tom 2", 1, 8), ("OH L", 1, 48), ("OH R", 1, 49), ("SPD L", 1, 11),
    ("SPD R", 1, 11), ("Bass", 2, 17), ("EGtr 1", 3, 20), ("EGtr 2", 3, 21),
    ("AcGtr 1", 3, 23), ("AcGtr 2", 3, 23), ("Keys 1 L", 4, 27), ("Keys 1 R", 4, 30),
    ("Keys 2 L", 4, 31), ("Keys 2 R", 4, 32), ("Tracks L", 5, 60), ("Tracks R", 5, 60),
    ("Click", 5, 12), ("Guide", 5, 45), ("WL Vox", 6, 41), ("Vox 2", 6, 42),
    ("Vox 3", 6, 42), ("Vox 4", 6, 43), ("Choir L", 7, 43), ("Choir R", 7, 43),
    ("Pastor", 7, 53), ("Host", 7, 50), ("Amb L", 4, 48), ("Amb R", 4, 49),
]
_WORSHIP_DCA = ["DRUMS", "BASS", "GTRS", "KEYS", "TRACKS", "VOCALS", "SPEECH", "AMBIENCE"]


class X32MockState:
    def __init__(
        self,
        advertise_ip: str = "127.0.0.1",
        name: str = "X32-SIM",
        preset: str = "band",
    ) -> None:
        self.lock = threading.Lock()
        self.advertise_ip = advertise_ip
        self.name = name
        self.preset = preset
        self.params: Dict[str, Tuple[str, Any]] = {}
        self.xremote_subs: Dict[Tuple[str, int], float] = {}
        self.meter_subs: Dict[Tuple[str, int], float] = {}
        self.scenes: Dict[int, Dict[str, Any]] = {}
        self.ch_presets: Dict[int, Dict[str, Any]] = {}
        self.load_preset(preset)

    @staticmethod
    def _libchan_prefix(ch_idx: int) -> Optional[str]:
        if 0 <= ch_idx < 32:
            return f"/ch/{ch_idx + 1:02d}"
        if 32 <= ch_idx < 40:
            return f"/auxin/{ch_idx - 31:02d}"
        if 48 <= ch_idx < 64:
            return f"/bus/{ch_idx - 47:02d}"
        if ch_idx == 70:
            return "/main/st"
        return None

    def load_preset(self, preset: str) -> List[Tuple[str, str, Any]]:
        preset = preset if preset in ("band", "worship", "blank") else "band"
        self.preset = preset
        self.params.clear()
        self.scenes.clear()
        self.ch_presets.clear()

        is_blank = preset == "blank"
        ch_spec = _WORSHIP_CH if preset == "worship" else _BAND_CH
        dca_spec = _WORSHIP_DCA if preset == "worship" else _BAND_DCA

        for i in range(32):
            p = f"/ch/{i+1:02d}"
            nm, col, icon = ch_spec[i]
            self.params[f"{p}/config/name"] = ("s", "" if is_blank else nm)
            self.params[f"{p}/config/color"] = ("i", 1 if is_blank else col)
            self.params[f"{p}/config/icon"] = ("i", 1 if is_blank else icon)
            self.params[f"{p}/mix/fader"] = ("f", 0.0 if is_blank else 0.75)
            self.params[f"{p}/mix/on"] = ("i", 1)
            self.params[f"{p}/grp/dca"] = ("i", 0 if is_blank else (1 << (i // 4)))
            self.params[f"{p}/grp/mute"] = ("i", 0 if is_blank else (1 if i < 20 else 0))
            self.params[f"/-stat/solosw/{i+1:02d}"] = ("i", 0)
            self.params[f"{p}/config/source"] = ("i", i + 1)
            self.params[f"{p}/preamp/trim"] = ("f", 0.5)
            self.params[f"{p}/preamp/invert"] = ("i", 0)
            self.params[f"{p}/preamp/hpon"] = ("i", 0)
            self.params[f"{p}/preamp/hpf"] = ("f", math.log(80.0 / 20.0) / math.log(20.0))
            self.params[f"{p}/mix/st"] = ("i", 1)
            self.params[f"{p}/mix/pan"] = ("f", 0.5)
            for b in range(16):
                self.params[f"{p}/mix/{b+1:02d}/level"] = ("f", 0.0 if is_blank else 0.5)
                self.params[f"{p}/mix/{b+1:02d}/on"] = ("i", 1)

        for i in range(8):
            p = f"/auxin/{i+1:02d}"
            self.params[f"{p}/config/name"] = ("s", "" if is_blank else f"Aux {i+1}")
            self.params[f"{p}/config/color"] = ("i", 6)
            self.params[f"{p}/config/icon"] = ("i", 1 if is_blank else 55)
            self.params[f"{p}/mix/fader"] = ("f", 0.0 if is_blank else 0.65)
            self.params[f"{p}/mix/on"] = ("i", 1)
            self.params[f"{p}/grp/dca"] = ("i", 0)
            self.params[f"{p}/grp/mute"] = ("i", 0)
            self.params[f"/-stat/solosw/{33+i:02d}"] = ("i", 0)
            self.params[f"{p}/config/source"] = ("i", 33 + i)
            self.params[f"{p}/preamp/trim"] = ("f", 0.5)
            self.params[f"{p}/preamp/invert"] = ("i", 0)
            self.params[f"{p}/mix/st"] = ("i", 1)
            self.params[f"{p}/mix/pan"] = ("f", 0.5)

        for i in range(16):
            p = f"/bus/{i+1:02d}"
            self.params[f"{p}/config/name"] = ("s", "" if is_blank else f"Bus {i+1:02d}")
            self.params[f"{p}/config/color"] = ("i", 4)
            self.params[f"{p}/config/icon"] = ("i", 1 if is_blank else (63 if i < 8 else 61))
            self.params[f"{p}/mix/fader"] = ("f", 0.75)
            self.params[f"{p}/mix/on"] = ("i", 1)
            self.params[f"/-stat/solosw/{49+i:02d}"] = ("i", 0)

        for i in range(8):
            p = f"/dca/{i+1}"
            self.params[f"{p}/config/name"] = ("s", "" if is_blank else dca_spec[i])
            self.params[f"{p}/config/color"] = ("i", i + 1 if not is_blank else 7)
            self.params[f"{p}/config/icon"] = ("i", 1 if is_blank else 70)
            self.params[f"{p}/fader"] = ("f", 0.75)
            self.params[f"{p}/on"] = ("i", 1)
            self.params[f"/-stat/solosw/{73+i:02d}"] = ("i", 0)

        self.params["/main/st/config/name"] = ("s", "MAIN LR")
        self.params["/main/st/config/color"] = ("i", 7)
        self.params["/main/st/config/icon"] = ("i", 1 if is_blank else 66)
        self.params["/main/st/mix/fader"] = ("f", 0.75)
        self.params["/main/st/mix/on"] = ("i", 1)
        self.params["/-stat/solosw/71"] = ("i", 0)
        self.params["/-stat/selidx"] = ("i", 0)
        self.params["/-show/prepos/current"] = ("i", 0)

        for m in range(1, 7):
            self.params[f"/config/mute/{m}"] = ("i", 0)
        for p in range(1, 32, 2):
            self.params[f"/config/chlink/{p}-{p+1}"] = ("i", 0)
        for p in range(1, 16, 2):
            self.params[f"/config/buslink/{p}-{p+1}"] = ("i", 0)
        self.params["/config/routing/IN/1-8"] = ("i", 0)
        self.params["/config/routing/IN/9-16"] = ("i", 1)
        self.params["/config/routing/IN/17-24"] = ("i", 2)
        self.params["/config/routing/IN/25-32"] = ("i", 3)
        self.params["/config/routing/IN/AUX"] = ("i", 0)
        self.params["/-prefs/haflags"] = ("i", 0)
        for ha in range(128):
            self.params[f"/headamp/{ha:03d}/gain"] = ("f", 12.0 / 72.0)
            self.params[f"/headamp/{ha:03d}/phantom"] = ("i", 0)

        scene_specs = [
            ("01 Full Band", "All inputs active"),
            ("02 Acoustic / Speech", "Vocal & acoustic set"),
            ("03 Intermission", "House walk-in music"),
            ("04 Unity All", "Zeroed 0dB fader check"),
        ]
        for s in range(32):
            if not is_blank and s < len(scene_specs):
                sname, snotes = scene_specs[s]
                hasdata = 1
            else:
                sname, snotes, hasdata = "", "", 0
            self.params[f"/-show/showfile/scene/{s:03d}/name"] = ("s", sname)
            self.params[f"/-show/showfile/scene/{s:03d}/notes"] = ("s", snotes)
            self.params[f"/-show/showfile/scene/{s:03d}/hasdata"] = ("i", hasdata)

        starter_presets = [
            ("Kick Punch", 1, 3, 0.80),
            ("Snare Crack", 1, 4, 0.76),
            ("Warm Bass", 4, 17, 0.75),
            ("Acoustic Str", 2, 19, 0.72),
            ("Lead Vocal", 5, 50, 0.82),
            ("Speech Mic", 6, 51, 0.78),
        ]
        for p_idx in range(32):
            if not is_blank and p_idx < len(starter_presets):
                pname, pcol, picon, pfad = starter_presets[p_idx]
                self.ch_presets[p_idx] = {
                    "name": pname,
                    "cfg": {"name": pname[:12], "color": pcol, "icon": picon},
                    "sends": {"fader": pfad, "on": 1, "dca": 0, "mute": 0},
                }
                self.params[f"/-libs/ch/{p_idx + 1:03d}/name"] = ("s", pname)
                self.params[f"/-libs/ch/{p_idx + 1:03d}/hasdata"] = ("i", 1)
            else:
                self.params[f"/-libs/ch/{p_idx + 1:03d}/name"] = ("s", "")
                self.params[f"/-libs/ch/{p_idx + 1:03d}/hasdata"] = ("i", 0)

        return self.snapshot_broadcast_items()

    def save_scene(self, scene_idx: int, name: str, notes: str) -> Tuple[bool, List[Tuple[str, str, Any]]]:
        idx = int(scene_idx)
        if idx < 0 or idx >= 32:
            return False, []
        clean_name = (name or f"Scene {idx:02d}")[:23]
        clean_notes = (notes or "")[:35]
        snap = {
            k: v
            for k, v in self.params.items()
            if not k.startswith("/-show/") and not k.startswith("/-libs/")
        }
        self.scenes[idx] = {"name": clean_name, "notes": clean_notes, "params": snap}
        changes: List[Tuple[str, str, Any]] = []

        def put(addr: str, t: str, val: Any) -> None:
            self.params[addr] = (t, val)
            changes.append((addr, t, val))

        put(f"/-show/showfile/scene/{idx:03d}/name", "s", clean_name)
        put(f"/-show/showfile/scene/{idx:03d}/notes", "s", clean_notes)
        put(f"/-show/showfile/scene/{idx:03d}/hasdata", "i", 1)
        put("/-show/prepos/current", "i", idx)
        return True, changes

    def save_ch_preset(self, slot_idx: int, name: str, ch_idx: int) -> Tuple[bool, List[Tuple[str, str, Any]]]:
        slot = int(slot_idx)
        prefix = self._libchan_prefix(int(ch_idx))
        if slot < 0 or slot >= 32 or prefix is None:
            return False, []
        cur_name = str(self.params.get(f"{prefix}/config/name", ("s", ""))[1])
        clean_name = (name or cur_name or f"Preset {slot + 1:02d}")[:23]
        fader_key = f"{prefix}/mix/fader"
        on_key = f"{prefix}/mix/on"
        self.ch_presets[slot] = {
            "name": clean_name,
            "cfg": {
                "name": cur_name[:12],
                "color": int(self.params.get(f"{prefix}/config/color", ("i", 0))[1]),
                "icon": int(self.params.get(f"{prefix}/config/icon", ("i", 1))[1]),
            },
            "sends": {
                "fader": float(self.params.get(fader_key, ("f", 0.75))[1]),
                "on": int(self.params.get(on_key, ("i", 1))[1]),
                "dca": int(self.params.get(f"{prefix}/grp/dca", ("i", 0))[1]),
                "mute": int(self.params.get(f"{prefix}/grp/mute", ("i", 0))[1]),
            },
        }
        changes: List[Tuple[str, str, Any]] = []
        for addr, t, val in (
            (f"/-libs/ch/{slot + 1:03d}/name", "s", clean_name),
            (f"/-libs/ch/{slot + 1:03d}/hasdata", "i", 1),
        ):
            self.params[addr] = (t, val)
            changes.append((addr, t, val))
        return True, changes

    def load_ch_preset(self, slot_idx: int, ch_idx: int, scope_mask: int) -> Tuple[bool, List[Tuple[str, str, Any]]]:
        slot = int(slot_idx)
        prefix = self._libchan_prefix(int(ch_idx))
        preset = self.ch_presets.get(slot)
        if prefix is None or preset is None:
            return False, []
        scope = int(scope_mask) & 0x3F
        changes: List[Tuple[str, str, Any]] = []

        def put(addr: str, t: str, val: Any) -> None:
            self.params[addr] = (t, val)
            changes.append((addr, t, val))

        if scope & 0x02:
            cfg = preset["cfg"]
            if cfg.get("name"):
                put(f"{prefix}/config/name", "s", str(cfg["name"])[:12])
            put(f"{prefix}/config/color", "i", int(cfg["color"]))
            put(f"{prefix}/config/icon", "i", int(cfg["icon"]))
        if scope & 0x20:
            sends = preset["sends"]
            put(f"{prefix}/mix/fader", "f", float(sends["fader"]))
            put(f"{prefix}/mix/on", "i", int(sends["on"]))
            if 0 <= int(ch_idx) < 40:
                put(f"{prefix}/grp/dca", "i", int(sends["dca"]))
                put(f"{prefix}/grp/mute", "i", int(sends["mute"]))
        return True, changes

    def snapshot_broadcast_items(self) -> List[Tuple[str, str, Any]]:
        items: List[Tuple[str, str, Any]] = []
        for k, (t, v) in self.params.items():
            if "/mix/0" in k or "/mix/1" in k:
                # Skip 512 per-bus sends during preset HTTP blast; keep main params
                if len(k) > 15 and k.endswith(("/level", "/on")):
                    continue
            items.append((k, t, v))
        return items

    def apply_scene(self, scene_idx: int) -> List[Tuple[str, str, Any]]:
        idx = max(0, min(31, int(scene_idx)))
        changes: List[Tuple[str, str, Any]] = []

        def put(addr: str, t: str, val: Any) -> None:
            self.params[addr] = (t, val)
            changes.append((addr, t, val))

        put("/-show/prepos/current", "i", idx)
        saved = self.scenes.get(idx)
        if saved is not None:
            for k, (t, val) in saved["params"].items():
                if self.params.get(k) != (t, val):
                    put(k, t, val)
            return changes
        if idx == 0:
            put("/config/mute/1", "i", 0)
            for i in range(32):
                put(f"/ch/{i+1:02d}/mix/on", "i", 1)
                put(f"/ch/{i+1:02d}/mix/fader", "f", 0.75)
            for d in range(8):
                put(f"/dca/{d+1}/on", "i", 1)
        elif idx == 1:
            put("/config/mute/1", "i", 0)
            for i in range(32):
                acoustic_on = 1 if i >= 13 else 0
                put(f"/ch/{i+1:02d}/mix/on", "i", acoustic_on)
                put(f"/ch/{i+1:02d}/mix/fader", "f", 0.75 if acoustic_on else 0.35)
        elif idx == 2:
            put("/config/mute/1", "i", 1)
            put("/ch/25/mix/on", "i", 1)
            put("/ch/25/mix/fader", "f", 0.80)
            put("/-stat/selidx", "i", 24)
        else:
            put("/config/mute/1", "i", 0)
            for i in range(32):
                put(f"/ch/{i+1:02d}/mix/on", "i", 1)
                put(f"/ch/{i+1:02d}/mix/fader", "f", 0.75)

        return changes


_DASHBOARD_HTML = """<!doctype html>
<html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Virtual Behringer X32</title>
<style>
:root{--bg:#0d1117;--p:#161b22;--p2:#21262d;--b:#30363d;--t:#e6edf3;--m:#8b949e;--c:#39c5cf;--g:#2ea043;--r:#da3633}
*{box-sizing:border-box}body{margin:0;padding:14px;background:var(--bg);color:var(--t);font-family:system-ui,sans-serif;font-size:13px}
h1{margin:0 0 8px;font-size:18px;display:flex;align-items:center;gap:10px;flex-wrap:wrap}
.pill{background:var(--p2);border:1px solid var(--b);border-radius:999px;padding:3px 10px;font-size:12px;color:var(--c)}
.card{background:var(--p);border:1px solid var(--b);border-radius:8px;padding:10px;margin-bottom:10px}
.row{display:flex;gap:6px;flex-wrap:wrap;align-items:center}
.btn{background:var(--p2);border:1px solid var(--b);color:var(--t);border-radius:6px;padding:5px 9px;cursor:pointer;font-weight:700;font-size:12px}
.btn.on{border-color:var(--c);background:#102938}.btn.red{background:#3a1416;border-color:var(--r)}
.grid{display:grid;grid-template-columns:repeat(8,minmax(0,1fr));gap:6px}
@media(max-width:760px){.grid{grid-template-columns:repeat(4,minmax(0,1fr))}}
.ch{background:var(--p2);border:1px solid var(--b);border-radius:6px;padding:6px;display:flex;flex-direction:column;gap:4px}
.ch.sel{outline:2px solid var(--c)}
.ch-top{display:flex;justify-content:space-between;font-size:10px;color:var(--m)}
.ch-nm{font-weight:700;white-space:nowrap;overflow:hidden;text-overflow:ellipsis}
</style></head><body>
<h1><span>Virtual Behringer X32</span><span id="meta" class="pill">--</span></h1>
<div class="card"><div class="row">
  <strong>Preset:</strong>
  <button class="btn" onclick="act({action:'preset',preset:'band'})">Band</button>
  <button class="btn" onclick="act({action:'preset',preset:'worship'})">Worship</button>
  <button class="btn" onclick="act({action:'preset',preset:'blank'})">Blank</button>
  <strong style="margin-left:10px">Scene:</strong>
  <button class="btn" onclick="act({action:'scene',scene:0})">01 Full Band</button>
  <button class="btn" onclick="act({action:'scene',scene:1})">02 Acoustic</button>
  <button class="btn" onclick="act({action:'scene',scene:2})">03 Intermission</button>
  <button class="btn" onclick="act({action:'scene',scene:3})">04 Unity</button>
</div></div>
<div class="card"><div class="row" id="dcaRow"></div><div class="row" style="margin-top:6px" id="mgRow"></div></div>
<div class="card"><div class="grid" id="chGrid"></div></div>
<script>
let S=null;
async function act(body){
  await fetch('/api/action',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(body)});
  poll();
}
function render(){
  if(!S) return;
  document.getElementById('meta').textContent=`${S.name} • UDP :${S.udpPort} • xremote:${S.xremoteClients} meters:${S.meterClients} • SEL:${S.selidx}`;
  document.getElementById('dcaRow').innerHTML='<strong>DCAs:</strong>'+S.dcas.map(d=>
    `<button class="btn ${d.on?'':'red'}" onclick="act({action:'mute_dca',dca:${d.idx+1},on:${d.on?0:1}})">DCA${d.idx+1} ${d.name||''}: ${d.on?'ON':'MUTE'}</button>`
    +`<button class="btn ${S.selidx===72+d.idx?'on':''}" onclick="act({action:'select',selidx:${72+d.idx}})">SEL</button>`
  ).join('')+`<button class="btn ${S.selidx===70?'on':''}" onclick="act({action:'select',selidx:70})">SEL MAIN LR</button>`;
  document.getElementById('mgRow').innerHTML='<strong>Mute Groups:</strong>'+S.muteGroups.map((v,i)=>
    `<button class="btn ${v?'red':''}" onclick="act({action:'mute_grp',grp:${i+1},val:${v?0:1}})">MG ${i+1}: ${v?'MUTE':'OFF'}</button>`
  ).join('');
  document.getElementById('chGrid').innerHTML=S.channels.map(c=>`
    <div class="ch ${S.selidx===c.idx?'sel':''}">
      <div class="ch-top"><span>CH ${String(c.idx+1).padStart(2,'0')}</span><span>#${c.icon}</span></div>
      <div class="ch-nm">${(c.name||'(empty)').replace(/[<>&]/g,'')}</div>
      <div class="row">
        <button class="btn ${S.selidx===c.idx?'on':''}" style="flex:1" onclick="act({action:'select',selidx:${c.idx}})">SEL</button>
        <button class="btn ${c.on?'':'red'}" onclick="act({action:'mute_ch',ch:${c.idx+1},on:${c.on?0:1}})">${c.on?'ON':'M'}</button>
      </div>
    </div>`).join('');
}
async function poll(){
  try{ const r=await fetch('/api/state'); if(r.ok){ S=await r.json(); render(); } }catch(e){}
}
poll(); setInterval(poll,500);
</script></body></html>"""


class _ReusableHTTPServer(ThreadingHTTPServer):
    allow_reuse_address = True


class X32MockServer:
    def __init__(
        self,
        host: str = "0.0.0.0",
        port: int = 10023,
        advertise_ip: str = "127.0.0.1",
        name: str = "X32-SIM",
        http_port: Optional[int] = None,
        preset: str = "band",
    ) -> None:
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self.sock.bind((host, port))
        self.port = self.sock.getsockname()[1]
        self.state = X32MockState(advertise_ip=advertise_ip, name=name, preset=preset)
        self.online = True
        self.running = False
        self._rx_thread: Optional[threading.Thread] = None
        self._meter_thread: Optional[threading.Thread] = None
        self._http_server: Optional[ThreadingHTTPServer] = None
        self._http_thread: Optional[threading.Thread] = None
        self.http_port = 0

        if http_port is not None:
            mock_ref = self

            class _Handler(BaseHTTPRequestHandler):
                def log_message(self, format: str, *args: Any) -> None:
                    return

                def _send(self, code: int, ctype: str, body: bytes) -> None:
                    self.send_response(code)
                    self.send_header("Content-Type", ctype)
                    self.send_header("Content-Length", str(len(body)))
                    self.send_header("Cache-Control", "no-store")
                    self.end_headers()
                    self.wfile.write(body)

                def do_GET(self) -> None:
                    if self.path == "/" or self.path.startswith("/index"):
                        self._send(200, "text/html; charset=utf-8", _DASHBOARD_HTML.encode("utf-8"))
                        return
                    if self.path == "/api/state":
                        payload = mock_ref.get_http_state()
                        self._send(200, "application/json", json.dumps(payload).encode("utf-8"))
                        return
                    self._send(404, "text/plain", b"Not Found")

                def do_POST(self) -> None:
                    if self.path != "/api/action":
                        self._send(404, "text/plain", b"Not Found")
                        return
                    length = int(self.headers.get("Content-Length", "0") or "0")
                    raw = self.rfile.read(min(length, 65536))
                    try:
                        req = json.loads(raw.decode("utf-8"))
                        res = mock_ref.handle_http_action(req)
                        self._send(200, "application/json", json.dumps(res).encode("utf-8"))
                    except Exception as exc:
                        self._send(400, "application/json", json.dumps({"ok": False, "err": str(exc)}).encode("utf-8"))

            self._http_server = _ReusableHTTPServer((host, http_port), _Handler)
            self.http_port = self._http_server.server_address[1]

    def start(self) -> None:
        self.running = True
        self.sock.settimeout(0.2)
        self._rx_thread = threading.Thread(target=self._rx_loop, daemon=True)
        self._meter_thread = threading.Thread(target=self._meter_loop, daemon=True)
        self._rx_thread.start()
        self._meter_thread.start()
        if self._http_server is not None:
            self._http_thread = threading.Thread(target=self._http_server.serve_forever, daemon=True)
            self._http_thread.start()

    def stop(self) -> None:
        self.running = False
        try:
            self.sock.close()
        except OSError:
            pass
        if self._http_server is not None:
            try:
                self._http_server.shutdown()
                self._http_server.server_close()
            except OSError:
                pass

    def _broadcast_xremote(self, pkt: bytes, exclude: Optional[Tuple[str, int]] = None) -> None:
        if not self.online:
            return
        now = time.monotonic()
        with self.state.lock:
            dead = [k for k, exp in self.state.xremote_subs.items() if exp < now]
            for k in dead:
                del self.state.xremote_subs[k]
            targets = [k for k in self.state.xremote_subs.keys() if k != exclude]
        for target in targets:
            try:
                self.sock.sendto(pkt, target)
            except OSError:
                pass

    def _apply_param_mutation(
        self, addr: str, t0: str, val: Any, exclude: Optional[Tuple[str, int]] = None
    ) -> None:
        partner_pkt: Optional[bytes] = None
        with self.state.lock:
            self.state.params[addr] = (t0, val)
            m = _CH_MIX_RE.match(addr)
            if m:
                kind, num_s, leaf = m.group(1), m.group(2), m.group(3)
                idx = int(num_s)
                first = ((idx - 1) // 2) * 2 + 1
                partner = first + 1 if idx == first else first
                link_key = f"/config/{'chlink' if kind == 'ch' else 'buslink'}/{first}-{first+1}"
                if self.state.params.get(link_key, ("i", 0))[1] != 0:
                    p_addr = f"/{kind}/{partner:02d}/mix/{leaf}"
                    self.state.params[p_addr] = (t0, val)
                    partner_pkt = osc_encode(p_addr, t0, val)

        self._broadcast_xremote(osc_encode(addr, t0, val), exclude=exclude)
        if partner_pkt is not None:
            # Broadcast stereo-linked partner to all subscribers including the sender
            self._broadcast_xremote(partner_pkt, exclude=None)

    def get_http_state(self) -> Dict[str, Any]:
        now = time.monotonic()
        with self.state.lock:
            p = self.state.params
            xr = sum(1 for exp in self.state.xremote_subs.values() if exp >= now)
            mt = sum(1 for exp in self.state.meter_subs.values() if exp >= now)
            channels = []
            for i in range(32):
                cp = f"/ch/{i+1:02d}"
                channels.append({
                    "idx": i,
                    "name": p.get(f"{cp}/config/name", ("s", ""))[1],
                    "color": int(p.get(f"{cp}/config/color", ("i", 0))[1]),
                    "icon": int(p.get(f"{cp}/config/icon", ("i", 1))[1]),
                    "fader": round(float(p.get(f"{cp}/mix/fader", ("f", 0.0))[1]), 3),
                    "on": int(p.get(f"{cp}/mix/on", ("i", 1))[1]),
                })
            dcas = []
            for i in range(8):
                dp = f"/dca/{i+1}"
                dcas.append({
                    "idx": i,
                    "name": p.get(f"{dp}/config/name", ("s", ""))[1],
                    "color": int(p.get(f"{dp}/config/color", ("i", 0))[1]),
                    "fader": round(float(p.get(f"{dp}/fader", ("f", 0.75))[1]), 3),
                    "on": int(p.get(f"{dp}/on", ("i", 1))[1]),
                })
            return {
                "name": self.state.name,
                "preset": self.state.preset,
                "online": self.online,
                "udpPort": self.port,
                "selidx": int(p.get("/-stat/selidx", ("i", 0))[1]),
                "scene": int(p.get("/-show/prepos/current", ("i", 0))[1]),
                "xremoteClients": xr,
                "meterClients": mt,
                "muteGroups": [bool(p.get(f"/config/mute/{m}", ("i", 0))[1]) for m in range(1, 7)],
                "channels": channels,
                "dcas": dcas,
            }

    def handle_http_action(self, req: Dict[str, Any]) -> Dict[str, Any]:
        act = str(req.get("action", ""))
        if act == "select":
            sel = max(0, min(79, int(req.get("selidx", 0))))
            self._apply_param_mutation("/-stat/selidx", "i", sel, exclude=None)
        elif act == "preset":
            preset = str(req.get("preset", "band"))
            with self.state.lock:
                items = self.state.load_preset(preset)
            for addr, t, val in items:
                self._broadcast_xremote(osc_encode(addr, t, val), exclude=None)
        elif act == "scene":
            sc = int(req.get("scene", 0))
            with self.state.lock:
                changes = self.state.apply_scene(sc)
            for addr, t, val in changes:
                self._broadcast_xremote(osc_encode(addr, t, val), exclude=None)
        elif act == "mute_ch":
            ch = max(1, min(32, int(req.get("ch", 1))))
            val = 1 if int(req.get("on", 1)) else 0
            self._apply_param_mutation(f"/ch/{ch:02d}/mix/on", "i", val, exclude=None)
        elif act == "fader_ch":
            ch = max(1, min(32, int(req.get("ch", 1))))
            fval = max(0.0, min(1.0, float(req.get("fader", 0.75))))
            self._apply_param_mutation(f"/ch/{ch:02d}/mix/fader", "f", fval, exclude=None)
        elif act == "mute_dca":
            dca = max(1, min(8, int(req.get("dca", 1))))
            val = 1 if int(req.get("on", 1)) else 0
            self._apply_param_mutation(f"/dca/{dca}/on", "i", val, exclude=None)
        elif act == "mute_grp":
            grp = max(1, min(6, int(req.get("grp", 1))))
            val = 1 if int(req.get("val", 0)) else 0
            self._apply_param_mutation(f"/config/mute/{grp}", "i", val, exclude=None)
        elif act == "name_ch":
            ch = max(1, min(32, int(req.get("ch", 1))))
            nm = str(req.get("name", ""))[:12]
            self._apply_param_mutation(f"/ch/{ch:02d}/config/name", "s", nm, exclude=None)
        elif act == "power":
            self.online = bool(req.get("online", True))
            if not self.online:
                with self.state.lock:
                    self.state.xremote_subs.clear()
                    self.state.meter_subs.clear()
        else:
            raise ValueError(f"Unknown action: {act}")
        return {"ok": True}

    def _rx_loop(self) -> None:
        while self.running:
            try:
                data, peer = self.sock.recvfrom(4096)
            except socket.timeout:
                continue
            except OSError:
                break
            if not self.online:
                continue
            try:
                addr, types, args = osc_decode(data)
            except Exception:
                continue

            now = time.monotonic()
            if addr == "/xinfo":
                adv_ip = resolve_advertise_ip(self.state.advertise_ip, peer[0])
                reply = osc_encode("/xinfo", "ssss", adv_ip, self.state.name, "X32", "4.09")
                try:
                    self.sock.sendto(reply, peer)
                except OSError:
                    pass
                continue

            if addr == "/xremote":
                with self.state.lock:
                    self.state.xremote_subs[peer] = now + 12.0
                continue

            if addr == "/meters":
                with self.state.lock:
                    self.state.meter_subs[peer] = now + 12.0
                continue

            if addr == "/-stat/clearsolo" and args:
                if int(args[0]) != 0:
                    cleared: List[str] = []
                    with self.state.lock:
                        for k in list(self.state.params.keys()):
                            if k.startswith("/-stat/solosw/") and self.state.params[k][1] != 0:
                                self.state.params[k] = ("i", 0)
                                cleared.append(k)
                    self._broadcast_xremote(osc_encode("/-stat/clearsolo", "i", 1), exclude=peer)
                    for ck in cleared:
                        self._broadcast_xremote(osc_encode(ck, "i", 0), exclude=peer)
                continue

            if addr == "/-action/goscene" and args:
                with self.state.lock:
                    changes = self.state.apply_scene(int(args[0]))
                for c_addr, ct, cval in changes:
                    self._broadcast_xremote(osc_encode(c_addr, ct, cval), exclude=None)
                continue

            if addr == "/save" and len(args) >= 2:
                kind = str(args[0])
                ok = False
                changes = []
                if kind == "scene" and len(args) >= 4:
                    with self.state.lock:
                        ok, changes = self.state.save_scene(int(args[1]), str(args[2]), str(args[3]))
                elif kind == "libchan" and len(args) >= 4:
                    with self.state.lock:
                        ok, changes = self.state.save_ch_preset(int(args[1]), str(args[2]), int(args[3]))
                for c_addr, ct, cval in changes:
                    self._broadcast_xremote(osc_encode(c_addr, ct, cval), exclude=None)
                try:
                    self.sock.sendto(osc_encode("/save", "si", kind, 1 if ok else 0), peer)
                except OSError:
                    pass
                continue

            if addr == "/load" and len(args) >= 2:
                kind = str(args[0])
                ok = False
                changes = []
                if kind == "scene":
                    sc_idx = int(args[1])
                    if 0 <= sc_idx < 32:
                        with self.state.lock:
                            changes = self.state.apply_scene(sc_idx)
                        ok = True
                elif kind == "libchan" and len(args) >= 4:
                    with self.state.lock:
                        ok, changes = self.state.load_ch_preset(int(args[1]), int(args[2]), int(args[3]))
                for c_addr, ct, cval in changes:
                    self._broadcast_xremote(osc_encode(c_addr, ct, cval), exclude=None)
                try:
                    self.sock.sendto(osc_encode("/load", "si", kind, 1 if ok else 0), peer)
                except OSError:
                    pass
                continue

            if not types:
                with self.state.lock:
                    entry = self.state.params.get(addr)
                if entry is not None:
                    t, val = entry
                    try:
                        self.sock.sendto(osc_encode(addr, t, val), peer)
                    except OSError:
                        pass
            else:
                t0 = types[0]
                val = args[0]
                self._apply_param_mutation(addr, t0, val, exclude=peer)

    def _meter_loop(self) -> None:
        t0 = time.monotonic()
        while self.running:
            time.sleep(0.1)
            if not self.online:
                continue
            now = time.monotonic()
            with self.state.lock:
                dead = [k for k, exp in self.state.meter_subs.items() if exp < now]
                for k in dead:
                    del self.state.meter_subs[k]
                targets = list(self.state.meter_subs.keys())
                if not targets:
                    continue

                p = self.state.params
                active_mute_mask = 0
                for m in range(6):
                    if int(p.get(f"/config/mute/{m+1}", ("i", 0))[1]) != 0:
                        active_mute_mask |= (1 << m)

                muted_dca_mask = 0
                for d in range(8):
                    if int(p.get(f"/dca/{d+1}/on", ("i", 1))[1]) == 0:
                        muted_dca_mask |= (1 << d)

                vals: List[float] = []
                elapsed = now - t0
                for i in range(32):
                    cp = f"/ch/{i+1:02d}"
                    mix_on = int(p.get(f"{cp}/mix/on", ("i", 1))[1])
                    fader = float(p.get(f"{cp}/mix/fader", ("f", 0.75))[1])
                    grp_mute = int(p.get(f"{cp}/grp/mute", ("i", 0))[1])
                    grp_dca = int(p.get(f"{cp}/grp/dca", ("i", 0))[1])
                    muted = (
                        mix_on == 0
                        or fader <= 0.001
                        or (grp_mute & active_mute_mask) != 0
                        or (grp_dca & muted_dca_mask) != 0
                    )
                    if muted:
                        vals.append(0.0)
                    else:
                        amp = (0.20 + 0.45 * (0.5 + 0.5 * math.sin(elapsed * 3.1 + i * 0.43))) * (0.35 + 0.65 * fader)
                        vals.append(amp)
                vals.extend([0.0] * 64)

            blob = bytearray(struct.pack("<I", len(vals)))
            for v in vals:
                blob.extend(struct.pack("<f", v))
            pkt = osc_encode("/meters/1", "b", bytes(blob))
            for target in targets:
                try:
                    self.sock.sendto(pkt, target)
                except OSError:
                    pass


def main() -> None:
    parser = argparse.ArgumentParser(description="Virtual Behringer X32 OSC Server")
    parser.add_argument("--host", default="0.0.0.0")
    parser.add_argument("--port", type=int, default=10023)
    parser.add_argument("--ip", default="auto", help="IP advertised in /xinfo ('auto' detects outbound LAN IP)")
    parser.add_argument("--name", default="X32-VIRTUAL", help="Console name reported in /xinfo")
    parser.add_argument("--preset", choices=["band", "worship", "blank"], default="band")
    parser.add_argument("--http-port", type=int, default=0, help="Optional HTTP control dashboard port (0 = disabled)")
    args = parser.parse_args()

    http_port = args.http_port if args.http_port > 0 else None
    srv = X32MockServer(
        host=args.host,
        port=args.port,
        advertise_ip=args.ip,
        name=args.name,
        http_port=http_port,
        preset=args.preset,
    )
    srv.start()
    print(f"Virtual X32 '{args.name}' listening on UDP {args.host}:{srv.port} (preset={args.preset}, advertise={args.ip})")
    if srv.http_port:
        print(f"Virtual X32 control dashboard listening on http://{args.host}:{srv.http_port}/")
    try:
        while True:
            time.sleep(1.0)
    except KeyboardInterrupt:
        srv.stop()


if __name__ == "__main__":
    main()
