#!/usr/bin/env python3
"""Exercise the installed bundle on an isolated CI display and session bus.

GNOME uses its headless backend; KWin nests in Xvfb. Only the test compositor
uses software-rendering overrides. The app retains its shipped permissions.
Screenshots are review evidence, not a pixel-perfect rendering assertion.
"""

import configparser
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import time

from gi.repository import GLib
from PIL import Image, ImageStat


APP = "org.amule.aMule"
SESSION, output_dir = sys.argv[1:]
OUT = Path(output_dir).resolve()
OUT.mkdir(parents=True, exist_ok=True)
PROCESSES = []
LOGS = []


def run(*args, **kwargs):
    return subprocess.check_output(args, text=True, timeout=15, **kwargs).strip()


def start(name, args, env):
    log = (OUT / f"{name}.log").open("w")
    LOGS.append(log)
    process = subprocess.Popen(args, stdout=log, stderr=subprocess.STDOUT, env=env)
    PROCESSES.append(process)
    return process


def wait_for(description, predicate, seconds=45):
    deadline = time.monotonic() + seconds
    while time.monotonic() < deadline:
        if any(p.poll() is not None for p in PROCESSES):
            raise RuntimeError(f"A process exited while waiting for {description}")
        try:
            value = predicate()
            if value:
                return value
        except (subprocess.SubprocessError, GLib.Error):
            pass
        time.sleep(0.5)
    raise RuntimeError(f"Timed out waiting for {description}")


def shell_eval(expression):
    value = run("gdbus", "call", "--session", "--dest", "org.gnome.Shell",
                "--object-path", "/org/gnome/Shell", "--method",
                "org.gnome.Shell.Eval", expression)
    ok, result = GLib.Variant.parse(None, value, None, None).unpack()
    if not ok:
        raise RuntimeError(f"GNOME evaluation failed: {result}")
    return result


def gnome_windows():
    return json.loads(shell_eval("JSON.stringify(global.get_window_actors().map(a => "
                                 "({title:a.meta_window.get_title(), "
                                 "wayland:a.meta_window.get_client_type() === "
                                 "imports.gi.Meta.WindowClientType.WAYLAND})))"))


def title_seen(title):
    if SESSION == "gnome-wayland":
        return any(title in w["title"] and w["wayland"] for w in gnome_windows())
    if SESSION == "kde-wayland":
        # Native xdg_toplevel traffic proves the app did not fall back to Xwayland.
        return re.search(r'set_title\("[^"\n]*' + re.escape(title),
                         (OUT / "amule.log").read_text(errors="replace"))
    return run("xdotool", "search", "--onlyvisible", "--name", title)


def key(chord):
    if SESSION != "gnome-wayland":
        run("xdotool", "key", "--clearmodifiers", chord)
        time.sleep(0.3)
        return
    # Mutter's virtual keyboard feeds native Wayland windows. XTest cannot.
    keysyms = {"alt": 0xffe9, "ctrl": 0xffe3, "Escape": 0xff1b,
               "Page_Down": 0xff56, "Tab": 0xff09}
    values = [keysyms.get(k, ord(k) if len(k) == 1 else 0) for k in chord.split("+")]
    for state, sequence in [(1, values), (0, list(reversed(values)))]:
        for value in sequence:
            shell_eval(f"global.smokeKeyboard.notify_keyval(0, {value}, {state}); true")
    time.sleep(0.3)


def capture(name):
    path = OUT / f"{name}.png"
    if SESSION == "gnome-wayland":
        value = run("gdbus", "call", "--session", "--dest", "org.gnome.Shell.Screenshot",
                    "--object-path", "/org/gnome/Shell/Screenshot", "--method",
                    "org.gnome.Shell.Screenshot.Screenshot", "false", "false", str(path))
        assert GLib.Variant.parse(None, value, None, None).unpack()[0], value
    else:
        run("import", "-window", "root", str(path))
    with Image.open(path) as image:
        assert min(image.size) >= 600, f"Unexpected screenshot dimensions: {image.size}"
        assert max(ImageStat.Stat(image.convert("RGB")).stddev) > 5, "Blank display"
    print(f"Captured {path.name}", flush=True)


def main():
    assert SESSION in {"x11", "gnome-wayland", "kde-wayland"}
    permissions = run("flatpak", "info", "--user", "--show-permissions", APP)
    (OUT / "permissions.ini").write_text(permissions + "\n")
    metadata = configparser.ConfigParser()
    metadata.read_string(permissions)
    devices = metadata.get("Context", "devices", fallback="").split(";")
    assert "dri" not in devices and "all" not in devices, permissions
    # No --nodevice override: check the actual installed manifest and sandbox.
    run("flatpak", "run", "--command=sh", APP, "-c", "test ! -e /dev/dri")

    host = os.environ.copy()
    runtime = OUT / "runtime"
    runtime.mkdir(mode=0o700)
    host["XDG_RUNTIME_DIR"] = str(runtime)
    host.pop("WAYLAND_DISPLAY", None)
    host.pop("DISPLAY", None)
    host.update(LIBGL_ALWAYS_SOFTWARE="1", GALLIUM_DRIVER="llvmpipe",
                GSETTINGS_BACKEND="memory", XDG_CONFIG_HOME=str(OUT / "host-config"))
    os.environ["XDG_RUNTIME_DIR"] = str(runtime)

    if SESSION == "gnome-wayland":
        host.update(XDG_SESSION_TYPE="wayland", XDG_CURRENT_DESKTOP="GNOME")
        start("compositor", ["gnome-shell", "--wayland", "--headless", "--no-x11",
                             "--wayland-display=smoke-0", "--virtual-monitor=1280x960",
                             "--unsafe-mode"], host)
        # Unsafe mode is confined to this disposable session bus; it enables the
        # compositor's test input and screenshot interfaces, not app permissions.
        wait_for("GNOME test API", lambda: shell_eval("true") == "true")
        shell_eval("global.smokeKeyboard = global.stage.context.get_backend().get_default_seat()"
                   ".create_virtual_device(imports.gi.Clutter.InputDeviceType.KEYBOARD_DEVICE); "
                   "Main.overview.hide(); true")
    else:
        host["DISPLAY"] = ":99"
        os.environ["DISPLAY"] = host["DISPLAY"]
        start("xvfb", ["Xvfb", ":99", "-screen", "0", "1280x960x24", "-nolisten", "tcp"], host)
        wait_for("Xvfb", lambda: run("xdpyinfo", env=host))
        if SESSION == "kde-wayland":
            host.update(XDG_SESSION_TYPE="wayland", XDG_CURRENT_DESKTOP="KDE",
                        QT_QPA_PLATFORM="xcb")
            start("compositor", ["kwin_wayland", "--x11-display=:99", "--width=1280",
                                 "--height=960", "--socket=smoke-0", "--no-lockscreen"], host)
        else:
            start("compositor", ["openbox"], host)
            wait_for("X11 window manager", lambda: "_NET_SUPPORTING_WM_CHECK(WINDOW)" in
                     run("xprop", "-root", "_NET_SUPPORTING_WM_CHECK"))

    app_env = os.environ.copy()
    for variable in ["LIBGL_ALWAYS_SOFTWARE", "GALLIUM_DRIVER", "WAYLAND_DISPLAY", "DISPLAY"]:
        app_env.pop(variable, None)
    options = []
    if SESSION != "x11":
        wait_for("Wayland socket", lambda: (runtime / "smoke-0").is_socket())
        app_env["WAYLAND_DISPLAY"] = "smoke-0"
        options = ["--env=GDK_BACKEND=wayland", "--env=WAYLAND_DEBUG=1"]
    else:
        app_env["DISPLAY"] = ":99"
        options = ["--env=GDK_BACKEND=x11"]
    app_env.update(LANG="C.UTF-8", LC_ALL="C.UTF-8")

    config = OUT / "config"
    config.mkdir()
    (config / "amule.conf").write_text(
        "[eMule]\nFirstRunWizardDone=1\nAutoconnect=0\nNewVersionCheck=0\n"
        "ConfirmExit=0\nStartupMinimized=0\nEnableTrayIcon=0\nUPnPEnabled=0\n"
        "ConnectToED2K=0\nConnectToKad=0\n")
    app = start("amule", ["flatpak", "run", *options, APP, "--config-dir=" + str(config)], app_env)
    wait_for("native aMule window", lambda: title_seen("aMule"))
    time.sleep(3)
    if SESSION == "gnome-wayland":
        shell_eval("global.get_window_actors().find(a => a.meta_window.get_title().includes('aMule'))"
                   ".meta_window.activate(0); true")
    elif SESSION == "kde-wayland":
        window = run("xdotool", "search", "--onlyvisible", "--pid", str(PROCESSES[1].pid)).splitlines()[-1]
        run("xdotool", "windowfocus", window)
    else:
        window = run("xdotool", "search", "--onlyvisible", "--class", "amule").splitlines()[-1]
        run("xdotool", "windowactivate", "--sync", window)
    capture("01-startup")
    key("alt+s")
    capture("02-search")
    key("alt+n")
    key("Tab")
    key("Page_Down")
    capture("03-networks-scroll")
    key("alt+p")
    wait_for("Preferences dialog", lambda: title_seen("Preferences"))
    capture("04-preferences")
    key("Escape")
    if SESSION == "gnome-wayland":
        shell_eval("global.get_window_actors().find(a => a.meta_window.get_title().includes('aMule'))"
                   ".meta_window.move_resize_frame(false, 20, 20, 1024, 740); true")
    else:
        run("xdotool", "windowsize", window, "1024", "740")
    time.sleep(1)
    capture("05-resized")
    key("ctrl+q")
    assert app.wait(timeout=30) == 0, "aMule did not shut down cleanly"
    assert (config / "amule.conf").is_file()
    (OUT / "result.txt").write_text(f"PASS: {SESSION}; no DRI; native window; keyboard navigation; "
                                   "Preferences; resize; screenshots; clean shutdown.\n"
                                   "Visual correctness and hardware performance require review.\n")
    print((OUT / "result.txt").read_text(), flush=True)


try:
    main()
finally:
    for process in reversed(PROCESSES):
        if process.poll() is None:
            process.terminate()
            try:
                process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait(timeout=5)
    for log in LOGS:
        log.close()
