import numpy as np
from enum import IntEnum
import yaml
from pathlib import Path
import pandas as pd
from typing import Dict, Set

from cfi.wolverine.signal import *

import cython
# libc.stdint provide c-native types c_uint32 etc
from libc.stdint cimport *

# speed up numpy arrays
cimport numpy as np
np.import_array()


class SigFileType(IntEnum):
    csv = 0
    npy = 1


class InsDataCache:
    def __init__(self, name: str):
        self.name: str = name
        self.exchtime = []
        self.localtime = []
        self.ap = []
        self.bp = []
    
    def push(self, ss: MdSnapshot):
        self.exchtime.append(ss.exchtime)
        self.localtime.append(ss.localtime)
        level1: MdLevel = ss.levels[0]
        self.ap.append(level1.ap)
        self.bp.append(level1.bp)


def hhmmssf_to_exchtime(val: str) -> int:
    hh: int = int(val[:2])
    mm: int = int(val[3:5])
    ss: int = int(val[6:8])
    ff: int = int(val[9:])
    time: int = hh * 3600 * int(1e9) + mm * 60 * int(1e9) + ss * int(1e9) + ff
    if hh >= 18:
        time -= 24 * 3600 * int(1e9)


cdef void match_sig_with_md(
        const int sig_nr,
        const np.uint64_t[:] sig_localtime,
        np.float64_t[:] sig_mid_px,
        const np.uint64_t[:] sig_fut_localtime,
        np.float64_t[:] sig_fut_mid_px,
        const int ret_nr,
        const np.uint64_t[:] ret_localtime,
        const np.float64_t[:] ret_mid_px):
    cdef int sig_idx = 0
    cdef int ret_idx = 0
    cdef int fut_ret_idx = 0
    while sig_idx < sig_nr:
        while ret_idx < ret_nr and ret_localtime[ret_idx] <= sig_localtime[sig_idx]:
            ret_idx += 1
        if ret_idx:
            sig_mid_px[sig_idx] = ret_mid_px[ret_idx - 1]

        while fut_ret_idx < ret_nr and ret_localtime[fut_ret_idx] <= sig_fut_localtime[sig_idx]:
            fut_ret_idx += 1
        if fut_ret_idx and fut_ret_idx < ret_nr:
            sig_fut_mid_px[sig_idx] = ret_mid_px[fut_ret_idx - 1]

        sig_idx += 1


class ICCalculator(SignalBase):

    def __init__(self):
        super().__init__()
        self.signame: str = ""
        self.sigdir: Path = Path("")
        self.sig_file_type: SigFileType = SigFileType.csv

        self.today: int = 0
        self.sig_df: pd.DataFrame = None
        self.futret_bias: Dict[int, str] = {}
        self.ins_cache: Dict[int, InsDataCache] = {}

    def load_signal(self):

        def __load_csv(sigdir: Path, signame: str, date: int) -> pd.DataFrame:
            sig_file: Path = sigdir.joinpath(signame, str(date), f"{signame}-{date}.csv")
            df: pd.DataFrame = pd.read_csv(sig_file, header=0, index_col=None,
                    dtype={"exchtime": str, "localtime": np.uint64})
            df["exchtime"] = df["exchtime"].apply(hhmmssf_to_exchtime)
            return df

        def __load_npy(sigdir: Path, signame: str, date: int) -> pd.DataFrame:
            date_dir: Path = sigdir.joinpath(signame, str(date))

            uv = np.memmap(date_dir.joinpath(f"{signame}-{date}.uv.npy"), dtype="S16", mode="r")
            targets: List[str ] = [str(x) for x in uv]

            exchtime = np.memmap(date_dir.joinpath(f"{signame}-{date}.ts.npy"), dtype=np.int64, mode="r")
            localtime = np.memmap(date_dir.joinpath(f"{signame}-{date}.localts.npy"), dtype=np.uint64, mode="r")
            sigs = np.memmap(date_dir.joinpath(f"{signame}-{date}.data.npy"), dtype=np.float64, mode="r")
            
            df: pd.DataFrame = pd.DataFrame(sigs, columns=targets)
            df["exchtime"] = exchtime
            df["localtime"] = localtime
            df = df[["exchtime", "localtime"] + targets]
            return df

        if self.sig_file_type == SigFileType.csv:
            self.sig_df = __load_csv(self.sigdir, self.signame, self.today)
        else:
            self.sig_df = __load_npy(self.sigdir, self.signame, self.today)

    def initialize(self, path: str):
        if not path:
            return
        print(f"loadding config:{path}")
        with open(path) as fin:
            cfg = yaml.safe_load(fin)
            self.signame = str(cfg["signame"])
            self.sigdir = Path(cfg["sigdir"])
            self.sig_file_type: SigFileType = SigFileType[str(cfg["file_type"])]
            for futret_bias_str in cfg["futret_bias"]:
                if futret_bias_str.endswith("ns"):
                    self.futret_bias[int(futret_bias_str[:-2])] = futret_bias_str
                elif futret_bias_str.endswith("s"):
                    self.futret_bias[int(futret_bias_str[:-1]) * int(1e9)] = futret_bias_str
                elif futret_bias_str.endswith("m"):
                    self.futret_bias[int(futret_bias_str[:-1]) * int(1e9) * 60] = futret_bias_str
                elif futret_bias_str.endswith("h"):
                    self.futret_bias[int(futret_bias_str[:-1]) * int(1e9) * 3600] = futret_bias_str
                else:
                    raise RuntimeError("unknown unit {futret_bias_str}")
            odir: Path = Path(cfg["output_dir"])
            odir.mkdir(parents=True, exist_ok=True)
            ofile: Path = odir / f"{self.signame}.csv"
            self.fout = open(ofile, "w", buffering=1)
            self.fout.write(f"date," + ",".join(self.futret_bias.values()) + "\n")

    def on_sod(self, date: int, ev: SodEvent):
        print(f"on_sod:{date},ins_nr:{ev.ins_nr}")
        if self.today != date:
            self.today = date
            self.load_signal()
            self.ins_cache.clear()

        for i in range(ev.ins_nr):
            ms_ptr: int = ctypes.cast(ev.ms[i], ctypes.c_void_p).value
            ms: MdStatic = ev.ms[i].contents
            print(f"\t{i+1},{ms_ptr},{ms.instrument}")
            self.ins_cache[ms_ptr] = InsDataCache(ms.instrument.decode("utf8"))

    def on_eod(self, date: int):
        def __calculate_ic_for_target(target: str, futret_bias: int) -> pd.DataFrame:
            sig_df = self.sig_df[["exchtime", "localtime", target]]
            for ms_ptr, cache in self.ins_cache.items():
                ms: MdStatic = MdStatic.from_address(ms_ptr)
                ms_tgt: str = ms.ticker.decode("utf8") + "." + ms.exchange.decode("utf8")
                if ms_tgt == target:
                    break
            else:
                raise RuntimeError(f"no data found for {target}")
            ret_df = pd.DataFrame(
                {
                    "exchtime": cache.exchtime,
                    "localtime": cache.localtime,
                    "ap": cache.ap,
                    "bp": cache.bp
                })
            ret_df["exchtime"] = ret_df["exchtime"].astype(np.int64)
            ret_df["localtime"] = ret_df["localtime"].astype(np.uint64)
            ret_df["mid_px"] = (ret_df["ap"] + ret_df["bp"]) / 2

            sig_df["mid_px"] = np.nan
            sig_df["fut_localtime"] = sig_df["localtime"] + futret_bias
            sig_df["fut_mid_px"] = np.nan
            match_sig_with_md(len(sig_df.index),
                    sig_df["localtime"].values,
                    sig_df["mid_px"].values,
                    sig_df["fut_localtime"].values,
                    sig_df["fut_mid_px"].values,
                    len(ret_df.index),
                    ret_df["localtime"].values,
                    ret_df["mid_px"].values)
            sig_df["fut_ret"] = (sig_df["fut_mid_px"] / sig_df["mid_px"]) - 1
            return sig_df

        print(f"{date},calculating ic")
        targets = [x for x in self.sig_df.columns if x not in ["exchtime", "localtime"]]

        fut_ret_all = []
        for futret_bias in self.futret_bias:
            sigs = []
            fut_ret = []
            for tgt in targets:
                sig_df: pd.DataFrame = __calculate_ic_for_target(tgt, futret_bias)
                sigs.append(sig_df[tgt])
                fut_ret.append(sig_df["fut_ret"])
            sig_1d = np.ma.masked_invalid(np.ravel(sigs))
            fut_ret_1d = np.ma.masked_invalid(np.ravel(fut_ret))
            corr = np.ma.corrcoef(sig_1d, fut_ret_1d)
            fut_ret_all.append(corr[0][1])
        self.fout.write(f"{date}," + ",".join(f"{x}" for x in fut_ret_all) + "\n")


    def on_snapshot(self, ev: SnapshotEvent):
        ms_ptr: int = ctypes.cast(ev.ms, ctypes.c_void_p).value
        # ms: MdStatic = ev.ms.contents
        ss: MdSnapshot = ev.snapshot.contents
        self.ins_cache[ms_ptr].push(ss)

    def on_cs_snapshot(self, ev: CsSnapshotEvent):
        pass


def pysig_create():
    return ICCalculator()
