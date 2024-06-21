import time
import numpy as np
from enum import IntEnum
import yaml
from pathlib import Path
import pandas as pd
from typing import Dict, Set, List
from cfi.wolverine.signal import *
from copy import deepcopy
try:
    from ..utils.business_calendar import *
except:
    from cfi.wlpysig.tools.business_calendar import *

import cython
# libc.stdint provide c-native types c_uint32 etc
from libc.stdint cimport *

# speed up numpy arrays
cimport numpy as cnp
cnp.import_array()
np.set_printoptions(suppress=True)


def str2ns(val: str) -> int:
    if val.endswith("ns"):
        val_ns = float(val[:-2])
    elif val.endswith("s"):
        val_ns = float(val[:-1]) * int(1e9)
    elif val.endswith("m"):
        val_ns = float(val[:-1]) * int(1e9) * 60
    elif val.endswith("h"):
        val_ns = float(val[:-1]) * int(1e9) * 3600
    else:
        raise RuntimeError(f"unknown unit {val}")
    return int(val_ns)


class SigFileType(IntEnum):
    csv = 0
    npy = 1


def hhmmssf_to_exchtime(val: str) -> int:
    hh: int = int(val[:2])
    mm: int = int(val[3:5])
    ss: int = int(val[6:8])
    ff: int = int(val[9:])
    time: int = hh * 3600 * int(1e9) + mm * 60 * int(1e9) + ss * int(1e9) + ff
    if hh >= 18:
        time -= 24 * 3600 * int(1e9)
    return time


cdef void match_A_with_B_ts(
        const int A_nr,
        const cnp.uint64_t[:] A_localtime,
        cnp.float64_t[:] ans,
        const int B_nr,
        const cnp.uint64_t[:] B_localtime,
        const cnp.float64_t[:] matched_data,
        const bint can_exceed):
    cdef int A_idx = 0
    cdef int B_idx = 0

    while A_idx < A_nr:
        while B_idx < B_nr and B_localtime[B_idx] <= A_localtime[A_idx]:
            B_idx += 1
        if B_idx and (can_exceed or B_idx < B_nr):
            ans[A_idx] = matched_data[B_idx - 1]

        A_idx += 1


cdef int search_session_end_index(
        const cnp.uint64_t[:] sig_localtime,
        const cnp.uint64_t target,
        int left,
        int right):

    cdef int l = left, mid
    while left <= right:
        mid = ((right - left) >> 1) + left
        if sig_localtime[mid] > target:
            right = mid - 1
        else:
            left = mid + 1

    if right < l:
        return -1
    return right


def find_all_shift_info(sig_localtime: np.ndarray,
                        sessions: List) -> List[Dict[string, int]]:
    left: int = 0
    right: int = len(sig_localtime) - 1
    session_num: int = len(sessions)
    ans: List[Dict[string, int]] = []

    for i in range(session_num - 1):
        idx = search_session_end_index(
            sig_localtime, sessions[i][1], left, right)
        if idx != -1:
            ans.append({
                "session_end": sessions[i][1],
                "start_shift_idx": idx,
                "time_shift": sessions[i + 1][0] - sessions[i][1]
            })
            left = idx + 1

    return ans


def shift_fut_localtime(fut_localtime: np.ndarray, shift_info: List):
    cdef int start_idx
    cdef cnp.uint64_t session_end
    cdef cnp.uint64_t time_shift

    for info in shift_info:
        start_idx = info['start_shift_idx']
        session_end = info['session_end']
        time_shift = info['time_shift']

        while start_idx >= 0 and fut_localtime[start_idx] > session_end:
            fut_localtime[start_idx] += time_shift
            start_idx -= 1


def make_localtime_session(date: int, session_list: List):
    daily_session: List = []
    str_time: str = str(date) + '00:00:00'
    time_stamp: float = time.mktime(time.strptime(str_time, '%Y%m%d%H:%M:%S'))
    today_ts: float = time_stamp * int(1e9)

    pre_business_day: int = Calendar.get_instance().next_business_day(date, -1)
    pre_day: int = next_day(pre_business_day)
    pre_str_time: str = str(pre_day) + '00:00:00'
    pre_time_stamp: float = time.mktime(
        time.strptime(pre_str_time, '%Y%m%d%H:%M:%S'))
    pre_ts: float = pre_time_stamp * int(1e9)

    for s in session_list:
        if s[0] < 0:
            daily_session.append([np.uint64(i + pre_ts) for i in s])
        else:
            daily_session.append([np.uint64(i + today_ts) for i in s])

    return daily_session


class InsDataCache:

    def __init__(self, tk_exch: str):
        self.tk_exch: str = tk_exch
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

    def cache2df(self):
        return pd.DataFrame(
            {
                "exchtime": self.exchtime,
                "localtime": self.localtime,
                "ap": self.ap,
                "bp": self.bp
            })


class EdgeCalculator(SignalBase):

    def __init__(self):
        self.signame: str = ""
        self.sigdir: Path = Path("")
        self.sig_file_type: SigFileType = SigFileType.csv

        self.today: int = 0
        self.sig_df: pd.DataFrame = None
        self.futret_bias_list: List = {}
        self.cache: Dict = {}
        self.history: Dict = {}
        self.name2sessions: Dict = {}
        self.quantile: float = 0.1
    
    def load_signal(self):

        def __load_csv(sigdir: Path, signame: str, date: int) -> pd.DataFrame:
            sig_file: Path = sigdir.joinpath(
                signame, str(date), f"{signame}-{date}.csv")
            df: pd.DataFrame = pd.read_csv(sig_file, header=0, index_col=None,
                                           dtype={"exchtime": str, "localtime": np.uint64})
            df["exchtime"] = df["exchtime"].apply(hhmmssf_to_exchtime)
            return df

        def __load_npy(sigdir: Path, signame: str, date: int) -> pd.DataFrame:
            date_dir: Path = sigdir.joinpath(signame, str(date))

            uv = np.memmap(
                date_dir.joinpath(f"{signame}-{date}.uv.npy"),
                dtype="S16",
                mode="r")
            targets: List[str] = [x.decode("utf8") for x in uv]

            exchtime = np.memmap(
                date_dir.joinpath(f"{signame}-{date}.ts.npy"),
                dtype=np.int64,
                mode="r")
            localtime = np.memmap(
                date_dir.joinpath(f"{signame}-{date}.localts.npy"),
                dtype=np.uint64,
                mode="r")
            sigs = np.memmap(
                date_dir.joinpath(f"{signame}-{date}.data.npy"),
                dtype=np.float64,
                mode="r",
                shape=(
                    exchtime.shape[0],
                    len(targets)))

            df: pd.DataFrame = pd.DataFrame(sigs, columns=targets)
            df["exchtime"] = exchtime
            df["localtime"] = localtime
            df = df[["exchtime", "localtime"] + targets]
            return df

        print(f"loading signal {self.today}")
        if self.sig_file_type == SigFileType.csv:
            self.sig_df = __load_csv(self.sigdir, self.signame, self.today)
        else:
            self.sig_df = __load_npy(self.sigdir, self.signame, self.today)

    def initialize(self, cfg_str: str):
        print(f"loading config")
        cfg = yaml.safe_load(cfg_str)
        self.signame = str(cfg["signame"])
        self.sigdir = Path(cfg["sigdir"])
        self.sig_file_type: SigFileType = SigFileType[str(cfg["file_type"])]
        odir: Path = Path(cfg["output_dir"])
        odir.mkdir(parents=True, exist_ok=True)
        ofile: Path = odir / f"{self.signame}.csv"
        if "stock_map" in cfg:
            raise RuntimeError("not support ts2cs currently")
        fb_start = str2ns(cfg["futret_bias_start"])
        fb_end = str2ns(cfg["futret_bias_end"])
        fb_step = str2ns(cfg["futret_bias_step"])
        self.futret_bias_list = np.arange(fb_start, fb_end + 1, fb_step)
        self.quantile = float(cfg["quantile"])
        self.fout = open(ofile, "w", buffering=1)
        self.fout.write(f"futret_bias(ns),min_edge,max_edge,all_avg\n")
	
    def check_futret_bias(self):
        for tk_exch, session_list in self.name2sessions.items():
            for session in session_list:
                if self.futret_bias_list[-1] > session[1] - session[0]:
                    raise Exception(
                        f"futret_bias {self.futret_bias_list[-1]} exceed trading period length of {tk_exch}")

 
    def on_sod(self, ev: SodEvent):
        date: int = int(ev.date)
        print(f"on_sod:{date},ins_nr:{ev.ins_nr}")
        today = self.today
        if self.today != date:
            self.today = date
            self.load_signal()
            self.name2sessions.clear()
            self.cache.clear()

        for i in range(ev.ins_nr):
            ms_ptr: int = ctypes.cast(ev.ms[i], ctypes.c_void_p).value
            ms: MdStatic = ev.ms[i].contents
            print(f"\t{i+1},{ms_ptr},{ms.instrument}")
            tk_exch = ms.ticker.decode(
                "utf8") + "." + ms.exchange.decode("utf8")
            self.cache[ms_ptr] = InsDataCache(tk_exch)
            sessions = []
            for j in range(ms.session_nr):
                sessions.append([ms.session[j].begin, ms.session[j].end])
            self.name2sessions[tk_exch] = sessions

    def on_eod(self, ev: EodEvent):
        date: int = int(ev.date)
        self.check_futret_bias()
        self.history[date] = {
            "sig_df": deepcopy(self.sig_df),
            "exch_session": deepcopy(self.name2sessions),
            "cache": deepcopy(self.cache),
        }

    def on_snapshot(self, ev: SnapshotEvent):
        ms_ptr: int = ctypes.cast(ev.ms, ctypes.c_void_p).value
        # ms: MdStatic = ev.ms.contents
        ss: MdSnapshot = ev.snapshot.contents
        self.cache[ms_ptr].push(ss)


    def __del__(self):
        print("calculating edge mean")
        a = time.perf_counter()
        total_sig = pd.concat([info["sig_df"]
                              for info in self.history.values()])
        all_tgt = [_x for _x in total_sig.columns if _x not in [
            "exchtime", "localtime"]]
        sigs = total_sig[all_tgt].values
        sorted_sigs = sigs[~np.isnan(sigs)].ravel()
        sorted_sigs.sort()
        cnt = int(sorted_sigs.size * self.quantile)
        min_edge = sorted_sigs[cnt - 1]
        max_edge = sorted_sigs[-cnt]
        min_mask = (sigs <= min_edge)
        max_mask = (sigs >= max_edge)
        shift_info: Dict = {}
        md_mid_px: Dict = {}
        sig_mid_px_list: List = []
        for date, info in self.history.items():
            shift_info[date] = {}
            md_mid_px[date] = {}
            localtime_array = info["sig_df"]["localtime"].values
            localtime_nr = len(localtime_array)
            ins_cache = info['cache']
            sig_mid_px_today = pd.DataFrame()
            for cache in ins_cache.values():
                tk_exch = cache.tk_exch
                local_session = make_localtime_session(date, info['exch_session'][tk_exch])
                shift_info[date][tk_exch] = find_all_shift_info(
                    localtime_array, local_session)

                md_mid_px[date][tk_exch] = (np.array(cache.ap, dtype=np.float64) +
                                np.array(cache.bp, dtype=np.float64)) / 2
                md_localtime = np.array(cache.localtime, dtype=np.uint64)

                sig_mid_px = np.full(localtime_nr,
                                    fill_value=np.nan, dtype=np.float64)
                match_A_with_B_ts(
                    localtime_nr,
                    localtime_array,
                    sig_mid_px,
                    md_localtime.shape[0],
                    md_localtime,
                    md_mid_px[date][tk_exch],
                    True)
                sig_mid_px_today[tk_exch] = sig_mid_px
            
            sig_mid_px_list.append(sig_mid_px_today)


        total_sig_mid_px = pd.concat(sig_mid_px_list)
        new_sigs = set(all_tgt) - set(total_sig_mid_px.columns)
        total_sig_mid_px[list(new_sigs)] = np.nan
        sig_mid_pxs = total_sig_mid_px[all_tgt].values

        for _i, fb in enumerate(self.futret_bias_list):
            sig_fut_mid_px_list: List = []
            for date, info in self.history.items():
                localtime_array = info["sig_df"]["localtime"].values
                ins_cache = info['cache']
                fut_mid_px_today = pd.DataFrame()
                localtime_nr = len(localtime_array)

                for cache in ins_cache.values():
                    fut_localtime = localtime_array + fb
                    shift_fut_localtime(fut_localtime, shift_info[date][tk_exch])
                    fut_mid_px = np.full(localtime_nr,
                                        fill_value=np.nan, dtype=np.float64)
                    md_localtime = np.array(
                        cache.localtime, dtype=np.uint64)
                    match_A_with_B_ts(
                        localtime_nr,  # fut_localtime_nr,
                        fut_localtime,
                        fut_mid_px,
                        md_localtime.shape[0],
                        md_localtime,
                        md_mid_px[date][tk_exch],
                        False)
                    fut_mid_px_today[tk_exch] = fut_mid_px

                sig_fut_mid_px_list.append(fut_mid_px_today)

            total_fut_mid_px = pd.concat(sig_fut_mid_px_list)
            new_sigs = set(all_tgt) - set(total_fut_mid_px.columns)
            total_fut_mid_px[list(new_sigs)] = np.nan
            fut_mid_pxs = total_fut_mid_px[all_tgt].values

            all_ret = fut_mid_pxs / sig_mid_pxs
            all_avg = np.nanmean(all_ret)
            min_avg = np.nanmean(all_ret[min_mask])
            max_avg = np.nanmean(all_ret[max_mask])

            self.fout.write(f"{fb},{min_avg},{max_avg},{all_avg}\n")
            if 0 == _i + 1 % 100:
                print(f"finish {_i + 1}/{len_fb}")

        b = time.perf_counter()
        print(f"time: {(b - a) * 1000}ms.")


def pysig_create():
    return EdgeCalculator()
