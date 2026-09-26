"""Install the Qt desktop packages for Windows (64-bit MSVC) straight from download.qt.io.

Qt 6.11 moved each Windows toolchain into its own repository folder
(e.g. windows_x86/desktop/qt6_6111/qt6_6111_msvc2022_64/), which aqtinstall
3.3 does not read yet. This script reads that folder's Updates.xml and
extracts the base package's archives into OUTDIR.

Usage: python install_qt_windows.py VERSION OUTDIR
Prints the Qt root directory (e.g. OUTDIR/6.11.1/msvc2022_64) on the last line.
"""
import re
import subprocess
import sys
import urllib.request
import xml.etree.ElementTree as ET
from pathlib import Path

BASE = "https://download.qt.io/online/qtsdkrepository/windows_x86/desktop"


def fetch(url: str) -> bytes:
    with urllib.request.urlopen(url, timeout=120) as r:
        return r.read()


def main() -> None:
    version, outdir = sys.argv[1], Path(sys.argv[2])
    tag = version.replace(".", "")
    root = f"{BASE}/qt6_{tag}"

    # Find the 64-bit MSVC toolchain folder (qt6_6111_msvc2022_64 or newer).
    listing = fetch(root + "/").decode()
    folders = sorted(set(re.findall(rf'href="(qt6_{tag}_msvc\d+_64)/"', listing)))
    if not folders:
        sys.exit(f"No 64-bit MSVC folder under {root}/")
    folder = folders[-1]
    arch = "win64_" + folder.split(f"qt6_{tag}_", 1)[1]  # win64_msvc2022_64
    repo = f"{root}/{folder}"
    print(f"Using {repo} ({arch})", flush=True)

    updates = ET.fromstring(fetch(repo + "/Updates.xml"))
    wanted = f"qt.qt6.{tag}.{arch}"
    pkg = next((p for p in updates.iter("PackageUpdate") if p.findtext("Name") == wanted), None)
    if pkg is None:
        names = [p.findtext("Name") for p in updates.iter("PackageUpdate")][:40]
        sys.exit(f"{wanted} not found; packages: {names}")

    pkg_version = pkg.findtext("Version")
    archives = [a.strip() for a in (pkg.findtext("DownloadableArchives") or "").split(",") if a.strip()]
    outdir.mkdir(parents=True, exist_ok=True)
    tmp = outdir / "_downloads"
    tmp.mkdir(exist_ok=True)
    for name in archives:
        url = f"{repo}/{wanted}/{pkg_version}{name}"
        dest = tmp / name
        print(f"Downloading {name}", flush=True)
        dest.write_bytes(fetch(url))
        subprocess.run(["7z", "x", "-y", "-bso0", "-bsp0", f"-o{outdir}", str(dest)], check=True)
        dest.unlink()

    qt_root = outdir / version / arch.removeprefix("win64_")
    if not (qt_root / "bin").is_dir():
        found = sorted(str(p.parent) for p in outdir.rglob("qmake*.exe"))
        found += sorted(str(p.parent) for p in outdir.rglob("Qt6CoreConfig.cmake"))
        if not found:
            tree = sorted(str(p.relative_to(outdir)) for p in outdir.glob("*/*/*"))[:60]
            sys.exit(f"Qt bin folder not found under {outdir}; archives={archives[:20]}; tree={tree}")
        qt_root = Path(found[0]).parent
    # Make the install relocatable for qmake/qtpaths users.
    (qt_root / "bin" / "qt.conf").write_text("[Paths]\nPrefix=..\n")
    print(qt_root.as_posix())


if __name__ == "__main__":
    main()
