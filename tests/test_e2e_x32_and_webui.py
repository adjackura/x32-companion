#!/usr/bin/env python3
"""End-to-end validation for embedded Web UI assets, JS syntax, and X32 UDP OSC protocol."""

import hashlib
import json
import os
import re
import shutil
import socket
import struct
import subprocess
import sys
import tempfile
import time
import urllib.request
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))

from x32_mock_server import X32MockServer, osc_decode, osc_encode  # noqa: E402


def find_node_bin() -> str:
    found = shutil.which("node")
    if found:
        return found
    nvm_dir = Path.home() / ".nvm" / "versions" / "node"
    if nvm_dir.is_dir():
        candidates = sorted(nvm_dir.glob("*/bin/node"))
        if candidates:
            return str(candidates[-1])
    return "node"


def test_embed_and_size() -> None:
    res = subprocess.run(
        [sys.executable, str(ROOT / "tools" / "embed_webui.py")],
        cwd=str(ROOT),
        capture_output=True,
        text=True,
        check=True,
    )
    print(res.stdout.strip())
    assets_h = (ROOT / "firmware" / "x32_companion" / "webui_assets.h").read_text(encoding="utf-8")
    m = re.search(r"WEBUI_INDEX_GZ_LEN\s*=\s*(\d+);", assets_h)
    assert m is not None, "WEBUI_INDEX_GZ_LEN not found in webui_assets.h"
    gz_len = int(m.group(1))
    assert 1000 < gz_len < 35000, f"Unexpected gzipped Web UI size: {gz_len} bytes"
    print(f"[PASS] Web UI embedded gzip size: {gz_len} bytes (< 35000 limit)")


def test_webui_js_syntax() -> None:
    html = (ROOT / "webui" / "index.html").read_text(encoding="utf-8")
    scripts = re.findall(r"<script[^>]*>(.*?)</script>", html, flags=re.DOTALL | re.IGNORECASE)
    assert scripts, "No <script> block found in webui/index.html"
    js_code = "\n\n".join(scripts)

    node_bin = find_node_bin()
    fd, tmp_path = tempfile.mkstemp(suffix=".js", prefix="x32_webui_check_")
    try:
        with os.fdopen(fd, "w", encoding="utf-8") as f:
            f.write(js_code)
        res = subprocess.run(
            [node_bin, "--check", tmp_path],
            capture_output=True,
            text=True,
        )
        assert res.returncode == 0, f"Web UI JS syntax error:\n{res.stderr}"
    finally:
        try:
            os.unlink(tmp_path)
        except OSError:
            pass
    print("[PASS] Web UI <script> passed node --check")
    forbidden = ("tab-network", "tbtn-network", "wifiScan", "wifiConnect", "x32Discover", "x32Connect", "setBacklight", "scMuteBtn", "scSoloBtn")
    found = [tok for tok in forbidden if re.search(rf"\b{re.escape(tok)}\b", html)]
    assert not found, f"Removed Web UI network tokens still present: {found}"
    required = ("tab-config", "tbtn-config", "tab-presets", "tbtn-presets", "confirmLoadDialog", "saveSlotDialog", "x32_session")
    missing = [tok for tok in required if tok not in html]
    assert not missing, f"Missing expected Presets/Passkey Option 1 tokens in Web UI: {missing}"
    print("[PASS] Web UI has no Wi-Fi/X32/backlight mutation controls and includes Presets + Option 1 session auth")


def recv_osc(sock: socket.socket, timeout: float = 2.0):
    sock.settimeout(timeout)
    data, peer = sock.recvfrom(4096)
    return osc_decode(data), peer


def recv_matching(sock: socket.socket, want_addr: str, timeout: float = 2.5):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        rem = max(0.05, deadline - time.monotonic())
        sock.settimeout(rem)
        try:
            data, _ = sock.recvfrom(4096)
        except socket.timeout:
            break
        addr, types, args = osc_decode(data)
        if addr == want_addr:
            return addr, types, args
    raise AssertionError(f"Timed out waiting for OSC address {want_addr}")


def drain_socket(sock: socket.socket) -> None:
    sock.settimeout(0.02)
    while True:
        try:
            sock.recvfrom(4096)
        except socket.timeout:
            break


def test_x32_osc_e2e() -> None:
    srv = X32MockServer(host="127.0.0.1", port=0, advertise_ip="127.0.0.1")
    srv.start()
    addr_tuple = ("127.0.0.1", srv.port)

    sock_a = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock_b = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock_a.bind(("127.0.0.1", 0))
    sock_b.bind(("127.0.0.1", 0))

    try:
        # 1. /xinfo discovery
        sock_a.sendto(osc_encode("/xinfo"), addr_tuple)
        (r_addr, r_types, r_args), _ = recv_osc(sock_a)
        assert r_addr == "/xinfo"
        assert r_types == "ssss"
        assert r_args == ["127.0.0.1", "X32-SIM", "X32", "4.09"]
        print(f"[PASS] /xinfo discovery: {r_args}")

        # 2. /xremote subscription & live sync between two clients
        sock_a.sendto(osc_encode("/xremote"), addr_tuple)
        time.sleep(0.05)

        sock_b.sendto(osc_encode("/ch/01/config/name", "s", "Sub Kick"), addr_tuple)
        _, t, a = recv_matching(sock_a, "/ch/01/config/name")
        assert t == "s" and a == ["Sub Kick"]

        # Query confirms persistence
        sock_a.sendto(osc_encode("/ch/01/config/name"), addr_tuple)
        _, t, a = recv_matching(sock_a, "/ch/01/config/name")
        assert a == ["Sub Kick"]

        # 3. DCA 1-digit addressing: /dca/1/config/name, /dca/1/fader, /dca/1/on
        sock_b.sendto(osc_encode("/dca/1/config/name", "s", "KIT"), addr_tuple)
        _, t, a = recv_matching(sock_a, "/dca/1/config/name")
        assert t == "s" and a == ["KIT"]

        sock_b.sendto(osc_encode("/dca/1/fader", "f", 0.625), addr_tuple)
        _, t, a = recv_matching(sock_a, "/dca/1/fader")
        assert t == "f" and abs(a[0] - 0.625) < 1e-4

        sock_b.sendto(osc_encode("/dca/1/on", "i", 0), addr_tuple)
        _, t, a = recv_matching(sock_a, "/dca/1/on")
        assert t == "i" and a == [0]

        # 4. Channel DCA bitmask & Mute Group mutation
        sock_b.sendto(osc_encode("/ch/05/grp/dca", "i", 0b00001010), addr_tuple)
        _, t, a = recv_matching(sock_a, "/ch/05/grp/dca")
        assert t == "i" and a == [10]

        sock_b.sendto(osc_encode("/config/mute/1", "i", 1), addr_tuple)
        _, t, a = recv_matching(sock_a, "/config/mute/1")
        assert t == "i" and a == [1]

        # 5. Follow X32 [SELECT] (/-stat/selidx)
        sock_b.sendto(osc_encode("/-stat/selidx", "i", 20), addr_tuple)
        _, t, a = recv_matching(sock_a, "/-stat/selidx")
        assert t == "i" and a == [20]

        # 6. Clear solo action
        sock_b.sendto(osc_encode("/-stat/solosw/01", "i", 1), addr_tuple)
        _, _, a = recv_matching(sock_a, "/-stat/solosw/01")
        assert a == [1]
        sock_b.sendto(osc_encode("/-stat/clearsolo", "i", 1), addr_tuple)
        _, _, a = recv_matching(sock_a, "/-stat/clearsolo")
        assert a == [1]
        sock_a.sendto(osc_encode("/-stat/solosw/01"), addr_tuple)
        _, _, a = recv_matching(sock_a, "/-stat/solosw/01")
        assert a == [0], f"Expected solo 01 cleared to 0, got {a}"
        print("[PASS] /xremote bidirectional mutations, DCA/MuteGrp/selidx/clearsolo verified")

        # 7. /meters/1 blob subscription & DCA/mute gating
        # Reset DCA 1 & Mute Group 1 from steps 3-4 so Ch 01 passes signal
        sock_b.sendto(osc_encode("/dca/1/on", "i", 1), addr_tuple)
        sock_b.sendto(osc_encode("/config/mute/1", "i", 0), addr_tuple)
        time.sleep(0.05)
        drain_socket(sock_a)

        sock_a.sendto(osc_encode("/meters", "s", "/meters/1"), addr_tuple)
        _, mt, ma = recv_matching(sock_a, "/meters/1", timeout=2.0)
        assert mt == "b" and len(ma) == 1
        blob = ma[0]
        float_count = struct.unpack("<I", blob[:4])[0]
        assert float_count == 96, f"Expected 96 floats in /meters/1 blob, got {float_count}"
        assert len(blob) >= 4 + 96 * 4
        ch1_amp = struct.unpack("<f", blob[4:8])[0]
        assert ch1_amp > 0.05, f"Expected non-zero ch1 meter before mute, got {ch1_amp}"

        # Mute DCA 1 and confirm Ch 01 (member of DCA 1) gates to 0.0
        sock_b.sendto(osc_encode("/dca/1/on", "i", 0), addr_tuple)
        drain_socket(sock_a)
        time.sleep(0.25)
        _, mt, ma = recv_matching(sock_a, "/meters/1", timeout=2.0)
        assert struct.unpack("<f", ma[0][4:8])[0] == 0.0, "Expected 0.0 meter when DCA 1 muted"

        # Restore DCA 1, then mute ch 1 directly and confirm meter goes to 0.0
        sock_b.sendto(osc_encode("/dca/1/on", "i", 1), addr_tuple)
        sock_b.sendto(osc_encode("/ch/01/mix/on", "i", 0), addr_tuple)
        drain_socket(sock_a)
        time.sleep(0.25)
        _, mt, ma = recv_matching(sock_a, "/meters/1", timeout=2.0)
        blob2 = ma[0]
        ch1_muted_amp = struct.unpack("<f", blob2[4:8])[0]
        assert ch1_muted_amp == 0.0, f"Expected 0.0 meter when ch1 muted, got {ch1_muted_amp}"
        print("[PASS] /meters/1 mixed-endian blob decoding, DCA & channel mute gating verified")

        # 8. Simulated console power-off / power-on transitions
        srv.handle_http_action({"action": "power", "online": False})
        st_off = srv.get_http_state()
        assert st_off["online"] is False and st_off["xremoteClients"] == 0
        drain_socket(sock_a)
        sock_a.sendto(osc_encode("/xinfo"), addr_tuple)
        timed_out = False
        try:
            recv_osc(sock_a, timeout=0.25)
        except socket.timeout:
            timed_out = True
        assert timed_out, "Expected /xinfo timeout while simulated X32 is powered off"

        srv.handle_http_action({"action": "power", "online": True})
        assert srv.get_http_state()["online"] is True
        sock_a.sendto(osc_encode("/xinfo"), addr_tuple)
        (r_addr2, r_types2, _), _ = recv_osc(sock_a, timeout=1.0)
        assert r_addr2 == "/xinfo" and r_types2 == "ssss"
        print("[PASS] Simulated X32 offline/online power transitions verified")

    finally:
        sock_a.close()
        sock_b.close()
        srv.stop()


def test_virtual_x32_features() -> None:
    srv = X32MockServer(
        host="127.0.0.1",
        port=0,
        advertise_ip="auto",
        name="X32-VIRTUAL",
        http_port=0,
        preset="band",
    )
    srv.start()
    addr_tuple = ("127.0.0.1", srv.port)
    assert srv.http_port > 0, "Expected ephemeral HTTP port for Virtual X32 dashboard"

    sock_a = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock_b = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock_a.bind(("127.0.0.1", 0))
    sock_b.bind(("127.0.0.1", 0))

    try:
        # 1. advertise_ip="auto" resolves loopback for local clients + custom name
        sock_a.sendto(osc_encode("/xinfo"), addr_tuple)
        (r_addr, r_types, r_args), _ = recv_osc(sock_a)
        assert r_addr == "/xinfo" and r_types == "ssss"
        assert r_args == ["127.0.0.1", "X32-VIRTUAL", "X32", "4.09"], f"Unexpected /xinfo: {r_args}"

        # 2. Stereo link ganging (chlink 1-2 -> ch 1 fader moves ch 2 for both clients)
        sock_a.sendto(osc_encode("/xremote"), addr_tuple)
        sock_b.sendto(osc_encode("/xremote"), addr_tuple)
        time.sleep(0.05)

        sock_b.sendto(osc_encode("/config/chlink/1-2", "i", 1), addr_tuple)
        recv_matching(sock_a, "/config/chlink/1-2")
        sock_b.sendto(osc_encode("/ch/01/mix/fader", "f", 0.42), addr_tuple)

        _, t_a, a_a = recv_matching(sock_a, "/ch/02/mix/fader")
        _, t_b, a_b = recv_matching(sock_b, "/ch/02/mix/fader")
        assert t_a == "f" and abs(a_a[0] - 0.42) < 1e-4
        assert t_b == "f" and abs(a_b[0] - 0.42) < 1e-4

        # 3. HTTP dashboard state & action -> OSC /xremote push
        with urllib.request.urlopen(f"http://127.0.0.1:{srv.http_port}/api/state", timeout=2.0) as resp:
            assert resp.status == 200
            st = json.loads(resp.read().decode("utf-8"))
            assert st["name"] == "X32-VIRTUAL"
            assert len(st["channels"]) == 32

        req = urllib.request.Request(
            f"http://127.0.0.1:{srv.http_port}/api/action",
            data=json.dumps({"action": "select", "selidx": 14}).encode("utf-8"),
            headers={"Content-Type": "application/json"},
            method="POST",
        )
        with urllib.request.urlopen(req, timeout=2.0) as resp:
            assert resp.status == 200
            res = json.loads(resp.read().decode("utf-8"))
            assert res.get("ok") is True

        _, ts, sa = recv_matching(sock_a, "/-stat/selidx")
        assert ts == "i" and sa == [14]

        # 4. Scene /save and /load round-trip
        sock_b.sendto(osc_encode("/ch/02/mix/fader", "f", 0.88), addr_tuple)
        recv_matching(sock_a, "/ch/02/mix/fader")
        sock_b.sendto(osc_encode("/save", "siss", "scene", 6, "Encore Set", "Big finish"), addr_tuple)
        _, _, sv_sc = recv_matching(sock_b, "/save")
        assert sv_sc == ["scene", 1], f"Unexpected scene /save reply: {sv_sc}"
        sock_a.sendto(osc_encode("/-show/showfile/scene/006/name"), addr_tuple)
        assert recv_matching(sock_a, "/-show/showfile/scene/006/name")[2] == ["Encore Set"]
        sock_a.sendto(osc_encode("/-show/showfile/scene/006/notes"), addr_tuple)
        assert recv_matching(sock_a, "/-show/showfile/scene/006/notes")[2] == ["Big finish"]
        sock_a.sendto(osc_encode("/-show/showfile/scene/006/hasdata"), addr_tuple)
        assert recv_matching(sock_a, "/-show/showfile/scene/006/hasdata")[2] == [1]

        sock_b.sendto(osc_encode("/ch/02/mix/fader", "f", 0.10), addr_tuple)
        recv_matching(sock_a, "/ch/02/mix/fader")
        drain_socket(sock_a)
        sock_b.sendto(osc_encode("/load", "si", "scene", 6), addr_tuple)
        _, _, ld_sc = recv_matching(sock_b, "/load")
        assert ld_sc == ["scene", 1], f"Unexpected scene /load reply: {ld_sc}"
        _, _, rf = recv_matching(sock_a, "/ch/02/mix/fader")
        assert abs(rf[0] - 0.88) < 1e-4, f"Expected scene recall fader 0.88, got {rf}"

        # 5. Channel Preset (libchan) /save and scoped /load round-trip
        sock_b.sendto(osc_encode("/ch/03/config/name", "s", "TomHi"), addr_tuple)
        sock_b.sendto(osc_encode("/ch/03/config/color", "i", 3), addr_tuple)
        sock_b.sendto(osc_encode("/ch/03/config/icon", "i", 14), addr_tuple)
        sock_b.sendto(osc_encode("/ch/03/mix/fader", "f", 0.68), addr_tuple)
        sock_b.sendto(osc_encode("/ch/03/mix/on", "i", 0), addr_tuple)
        time.sleep(0.05)
        sock_b.sendto(osc_encode("/save", "sisi", "libchan", 8, "DrumKit", 2), addr_tuple)
        _, _, sv_ch = recv_matching(sock_b, "/save")
        assert sv_ch == ["libchan", 1], f"Unexpected libchan /save reply: {sv_ch}"
        sock_a.sendto(osc_encode("/-libs/ch/009/name"), addr_tuple)
        assert recv_matching(sock_a, "/-libs/ch/009/name")[2] == ["DrumKit"]
        sock_a.sendto(osc_encode("/-libs/ch/009/hasdata"), addr_tuple)
        assert recv_matching(sock_a, "/-libs/ch/009/hasdata")[2] == [1]

        # Prepare Ch 05 with distinct scribble + fader, load scope=0x20 (Sends only, Cfg off)
        sock_b.sendto(osc_encode("/ch/05/config/name", "s", "KeepMe"), addr_tuple)
        sock_b.sendto(osc_encode("/ch/05/config/color", "i", 7), addr_tuple)
        sock_b.sendto(osc_encode("/ch/05/mix/fader", "f", 0.20), addr_tuple)
        sock_b.sendto(osc_encode("/ch/05/mix/on", "i", 1), addr_tuple)
        time.sleep(0.05)
        drain_socket(sock_a)
        sock_b.sendto(osc_encode("/load", "siii", "libchan", 8, 4, 0x20), addr_tuple)
        _, _, ld_ch = recv_matching(sock_b, "/load")
        assert ld_ch == ["libchan", 1]
        assert abs(recv_matching(sock_a, "/ch/05/mix/fader")[2][0] - 0.68) < 1e-4
        assert recv_matching(sock_a, "/ch/05/mix/on")[2] == [0]
        sock_a.sendto(osc_encode("/ch/05/config/name"), addr_tuple)
        assert recv_matching(sock_a, "/ch/05/config/name")[2] == ["KeepMe"]
        sock_a.sendto(osc_encode("/ch/05/config/color"), addr_tuple)
        assert recv_matching(sock_a, "/ch/05/config/color")[2] == [7]

        # Load scope=0x02 (Cfg only) -> updates color/icon
        sock_b.sendto(osc_encode("/load", "siii", "libchan", 8, 4, 0x02), addr_tuple)
        assert recv_matching(sock_b, "/load")[2] == ["libchan", 1]
        sock_a.sendto(osc_encode("/ch/05/config/color"), addr_tuple)
        assert recv_matching(sock_a, "/ch/05/config/color")[2] == [3]
        sock_a.sendto(osc_encode("/ch/05/config/icon"), addr_tuple)
        assert recv_matching(sock_a, "/ch/05/config/icon")[2] == [14]

        # 6. Channel Config OSC round-trip
        sock_a.sendto(osc_encode("/ch/01/config/source"), addr_tuple)
        assert recv_matching(sock_a, "/ch/01/config/source")[2] == [1]
        sock_a.sendto(osc_encode("/config/routing/IN/1-8"), addr_tuple)
        assert recv_matching(sock_a, "/config/routing/IN/1-8")[2] == [0]
        sock_a.sendto(osc_encode("/headamp/000/phantom"), addr_tuple)
        assert recv_matching(sock_a, "/headamp/000/phantom")[2] == [0]

        drain_socket(sock_a)
        sock_b.sendto(osc_encode("/ch/01/config/source", "i", 17), addr_tuple)
        assert recv_matching(sock_a, "/ch/01/config/source")[2] == [17]
        sock_b.sendto(osc_encode("/config/routing/IN/1-8", "i", 4), addr_tuple)
        assert recv_matching(sock_a, "/config/routing/IN/1-8")[2] == [4]
        sock_b.sendto(osc_encode("/headamp/000/gain", "f", 0.5), addr_tuple)
        assert abs(recv_matching(sock_a, "/headamp/000/gain")[2][0] - 0.5) < 1e-4
        sock_b.sendto(osc_encode("/headamp/000/phantom", "i", 1), addr_tuple)
        assert recv_matching(sock_a, "/headamp/000/phantom")[2] == [1]
        sock_b.sendto(osc_encode("/ch/01/preamp/hpon", "i", 1), addr_tuple)
        assert recv_matching(sock_a, "/ch/01/preamp/hpon")[2] == [1]
        sock_b.sendto(osc_encode("/ch/01/preamp/hpf", "f", 0.60), addr_tuple)
        assert abs(recv_matching(sock_a, "/ch/01/preamp/hpf")[2][0] - 0.60) < 1e-4
        sock_b.sendto(osc_encode("/ch/01/mix/pan", "f", 0.25), addr_tuple)
        assert abs(recv_matching(sock_a, "/ch/01/mix/pan")[2][0] - 0.25) < 1e-4

        for q_addr, q_exp in (
            ("/ch/01/config/source", [17]),
            ("/config/routing/IN/1-8", [4]),
            ("/headamp/000/phantom", [1]),
            ("/ch/01/preamp/hpon", [1]),
        ):
            sock_a.sendto(osc_encode(q_addr), addr_tuple)
            assert recv_matching(sock_a, q_addr)[2] == q_exp
        for q_addr, q_exp in (
            ("/headamp/000/gain", 0.5),
            ("/ch/01/preamp/hpf", 0.60),
            ("/ch/01/mix/pan", 0.25),
        ):
            sock_a.sendto(osc_encode(q_addr), addr_tuple)
            assert abs(recv_matching(sock_a, q_addr)[2][0] - q_exp) < 1e-4

        print("[PASS] Virtual X32 auto-IP, stereo link ganging, HTTP->OSC, Scene/libchan, and Channel Config verified")
    finally:
        sock_a.close()
        sock_b.close()
        srv.stop()


def test_ota_manifest_dry_run() -> None:
    bin_path = ROOT / "build" / "x32_companion.ino.bin"
    if not bin_path.is_file():
        print("[SKIP] test_ota_manifest_dry_run (build/x32_companion.ino.bin not built)")
        return

    header_text = (ROOT / "firmware" / "x32_companion" / "ota_updater.h").read_text(encoding="utf-8")
    ver_m = re.search(r'X32_COMPANION_FW_VERSION\s*=\s*"([^"]+)"', header_text)
    repo_m = re.search(r'X32_COMPANION_GITHUB_REPO\s*=\s*"([^"]+)"', header_text)
    assert ver_m and repo_m, "ota_updater.h missing FW_VERSION or GITHUB_REPO"
    fw_ver = ver_m.group(1)
    github_repo = repo_m.group(1)

    env = dict(os.environ)
    env["SKIP_BUILD"] = "1"
    res = subprocess.run(
        ["bash", str(ROOT / "tools" / "publish_ota.sh"), "--dry-run", f"v{fw_ver}"],
        cwd=str(ROOT),
        env=env,
        capture_output=True,
        text=True,
        check=True,
    )
    assert "[DRY-RUN]" in res.stdout, res.stdout

    manifest_path = ROOT / "build" / "manifest.json"
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    bin_data = bin_path.read_bytes()
    assert manifest["version"] == fw_ver
    assert manifest["tag"] == f"v{fw_ver}"
    assert manifest["url"] == (
        f"https://github.com/{github_repo}/releases/download/v{fw_ver}/x32_companion.ino.bin"
    )
    assert manifest["size"] == len(bin_data)
    assert manifest["sha256"] == hashlib.sha256(bin_data).hexdigest().lower()

    bad = subprocess.run(
        ["bash", str(ROOT / "tools" / "publish_ota.sh"), "--dry-run", "v99.99.99"],
        cwd=str(ROOT),
        env=env,
        capture_output=True,
        text=True,
    )
    assert bad.returncode != 0, "mismatched OTA tag should fail"
    print(f"[PASS] OTA dry-run manifest generation verified ({manifest['size']} bytes, v{fw_ver})")


def main() -> None:
    test_embed_and_size()
    test_webui_js_syntax()
    test_x32_osc_e2e()
    test_virtual_x32_features()
    test_ota_manifest_dry_run()
    print("ALL E2E tests PASSED!")


if __name__ == "__main__":
    main()
