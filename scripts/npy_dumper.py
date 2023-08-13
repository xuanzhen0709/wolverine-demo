import argparse
import numpy as np
from pathlib import Path
import pandas as pd
from typing import List

def read_npy(indir: Path) -> pd.DataFrame:
    date: int = int(indir.resolve().name)
    name: str = indir.resolve().parent.name

    uv = np.memmap(indir.joinpath(f"{name}-{date}.uv.npy"), dtype="S16", mode="r")
    targets: List[str] = [x.decode("utf8") for x in uv]

    exchtime = np.memmap(indir.joinpath(f"{name}-{date}.ts.npy"), dtype=np.int64, mode="r")
    localtime = np.memmap(indir.joinpath(f"{name}-{date}.localts.npy"), dtype=np.uint64, mode="r")
    sigs = np.memmap(indir.joinpath(f"{name}-{date}.data.npy"), dtype=np.float64, mode="r", shape=(exchtime.shape[0], len(targets)))
    
    df: pd.DataFrame = pd.DataFrame(sigs, columns=targets)
    df["exchtime"] = exchtime
    df["localtime"] = localtime
    df = df[["exchtime", "localtime"] + targets]
    return df


def main():
    parser = argparse.ArgumentParser("reader npy")
    parser.add_argument("indir", type=Path, help="input dir")
    parser.add_argument("-o", "--output", type=Path, required=True, help="output dir")
    args = parser.parse_args()

    df: pd.DataFrame = read_npy(args.indir)
    df.to_csv(args.output, index=False)


if __name__ == "__main__":
    main()