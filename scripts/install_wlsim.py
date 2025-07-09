#!/usr/bin/env python3
import argparse
import os
from pathlib import Path
import subprocess
from packaging.version import Version


wlsim_ver: str = "latest"


def get_os_key():

    key = os.environ.get("WLSIM_ENV_KEY", "")
    if not key:
        raise RuntimeError(f"wlsim env not enabled")
    return key


def get_version(path: Path, ver: str):
    versions = []
    for _x in path.iterdir():
        if not _x.is_dir():
            continue
        try:
            versions.append(Version(_x.name))
        except:
            pass

    if ver == "latest":
        return "v" + str(max(versions))
    tgt = Version(ver)

    if tgt.micro or ver.count(".") == 2:
        assert tgt in versions
        return "v" + str(tgt)

    min_ver = tgt
    max_ver = Version(ver + ".9999")
    found = [_x for _x in versions if min_ver <= _x <= max_ver]
    if not found:
        raise RuntimeError(f"path {path} cannot find version {ver}")
    return "v" + str(max(found))


def install_package(name: str, version: str):
    root_dirs = [
        Path("/mnt/nas-3.new/homes/nickchenyj/packages"),
        Path("/mnt/nas-intern/homes/nickchenyj/packages"),
        Path("/mnt/nas-3/homes/nickchenyj/packages"),
    ]

    root_dir = [_x for _x in root_dirs if _x.exists()]
    if not root_dir:
        raise RuntimeError(f"no root dir")
    root_dir = root_dir[0]

    pkgdir: Path = root_dir / name

    global wlsim_ver

    if name == "wlmd":
        if version == "latest":
            main_ver = Version(wlsim_ver)
            version = f"{main_ver.major}.{main_ver.minor}"
        version = get_version(root_dir / name, version)
        distdir = pkgdir / version / f"wlsim.{wlsim_ver}" / get_os_key()
    else:
        version = get_version(root_dir / name, version)
        if name == "wlsim":
            wlsim_ver = version
        distdir = pkgdir / version / get_os_key()

    cmd = ["python3", distdir / "install_runtime.py"]
    print(f"running {cmd}")
    subprocess.run(cmd, check=True)


def main():
    parser = argparse.ArgumentParser("install package")
    parser.add_argument(
        "-v",
        "--version",
        type=str,
        default="latest",
        help="verion to use, latest/v1.8.x/v1.8/etc",
    )
    args = parser.parse_args()
    install_package("wlsim", args.version)
    install_package("wlmd", "latest")
    install_package("cfi-operators", "latest")


if __name__ == "__main__":
    main()
