#!/usr/bin/env python3
"""Validate the signal_reader demo: sigreader must replay a saved signal
identically to computing it live.

The demo ships two configs that differ only in how ``fct1`` is obtained:

* ``pystrat.yml``  — ``fct1`` is computed live by ``nickchenyj.cssnap_simple_factor``
  (seed 1.234) **and** saved to disk; ``fct2``–``fct9`` are live factors.
* ``sigreader.yml`` — ``fct1`` is read back from disk via ``system.sigreader``;
  ``fct2``–``fct9`` are the same live factors.

Because the strat (``nickchenyj.cssnap_simple_strat``) sums all factors, its
output is identical between the two runs **iff** ``system.sigreader`` replays
``fct1`` byte-for-byte the same as the live computation. This script runs both
configs in sequence and diffs the ``strat`` output.

Usage:
    python3 scripts/validate_signal_reader.py [-w demos/signal_reader]
"""
import argparse
import shutil
import subprocess
import sys
from pathlib import Path


def run_wlsim(cfg: Path, log: Path) -> int:
    print(f"  wl-sim {cfg}")
    with open(log, "w") as fout:
        rc = subprocess.run(
            f"stdbuf -o0 -e0 wl-sim {cfg} 2>&1",
            shell=True,
            check=False,
            stdout=fout,
            stderr=subprocess.STDOUT,
        ).returncode
    if rc != 0:
        print(f"  !! wl-sim {cfg} failed (exit {rc}); see {log}")
    return rc


def find_strat(output_dir: Path) -> Path:
    matches = list(output_dir.glob("strat/*/strat-*.csv"))
    if not matches:
        raise FileNotFoundError(f"no strat output found under {output_dir}")
    if len(matches) > 1:
        raise FileNotFoundError(f"multiple strat outputs: {matches}")
    return matches[0]


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument(
        "-w", "--workdir",
        type=Path,
        default=Path(__file__).resolve().parent.parent / "demos" / "signal_reader",
        help="signal_reader demo directory (default: demos/signal_reader)",
    )
    parser.add_argument("--pystrat", default="pystrat.yml", help="live+save config")
    parser.add_argument("--sigreader", default="sigreader.yml", help="replay config")
    args = parser.parse_args()

    workdir: Path = args.workdir.resolve()
    pystrat: Path = workdir / args.pystrat
    sigreader: Path = workdir / args.sigreader
    output_dir: Path = workdir / "output"
    log_dir: Path = workdir / "log"
    log_dir.mkdir(parents=True, exist_ok=True)

    for f in (pystrat, sigreader):
        if not f.is_file():
            print(f"config not found: {f}", file=sys.stderr)
            return 2

    # 1. run pystrat.yml — computes fct1 live and saves it; writes strat output.
    print("[1/2] pystrat (live fct1, save fct1 + strat)")
    shutil.rmtree(output_dir, ignore_errors=True)
    if run_wlsim(pystrat, log_dir / "pystrat.log") != 0:
        return 1
    try:
        strat_live = find_strat(output_dir)
    except FileNotFoundError as e:
        print(f"  {e}", file=sys.stderr)
        return 1
    # park the live strat aside; the saved fct1 stays in place for sigreader.
    strat_parked = workdir / "strat_pystrat.csv"
    shutil.copy2(strat_live, strat_parked)
    print(f"  live strat: {strat_live.relative_to(workdir)} "
          f"({sum(1 for _ in open(strat_parked))} lines)")

    # 2. run sigreader.yml — reads fct1 back via system.sigreader; overwrites strat.
    print("[2/2] sigreader (replay fct1 via system.sigreader)")
    if run_wlsim(sigreader, log_dir / "sigreader.log") != 0:
        return 1
    try:
        strat_replay = find_strat(output_dir)
    except FileNotFoundError as e:
        print(f"  {e}", file=sys.stderr)
        return 1
    print(f"  replay strat: {strat_replay.relative_to(workdir)} "
          f"({sum(1 for _ in open(strat_replay))} lines)")

    # 3. compare — must be byte-identical iff sigreader correctly replays fct1.
    print("comparing strat output (live vs replayed)...")
    if subprocess.run(["diff", "-q", str(strat_parked), str(strat_replay)],
                      check=False).returncode == 0:
        print("✅ PASS — sigreader replays fct1 identically to the live factor")
        rc = 0
    else:
        print("❌ FAIL — strat output differs between pystrat and sigreader:")
        subprocess.run(["diff", str(strat_parked), str(strat_replay)], check=False)
        rc = 1

    strat_parked.unlink(missing_ok=True)
    return rc


if __name__ == "__main__":
    sys.exit(main())
