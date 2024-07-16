import numpy as np
from enum import IntEnum
import yaml
from pathlib import Path
import pandas as pd
import time
from typing import List, Dict
from cfi.wolverine.signal import *
from copy import deepcopy

from cfi.wolverine.misc.calendar_utils import CalendarMgr
from cfi.wolverine.misc.sigreader import SignalReader
try:
    from ..utils.calendar_utils import *
except:
    from cfi.wlpysig.tools.calendar_utils import *

import cython
# libc.stdint provide c-native types c_uint32 etc
from libc.stdint cimport *

# speed up numpy arrays
cimport numpy as cnp
cnp.import_array()
np.seterr(all="ignore")


class SigFileType(IntEnum):
    csv = 0
    npy = 1


class DataCache:
    def __init__(self):
        self.exchtime = []
        self.localtime = []
        self.ap = []
        self.bp = []

    def push(self, ev: CsSnapshotEvent):
        self.exchtime.append(ev.exchtime)
        self.localtime.append(ev.localtime)
        self.ap.append(np.ndarray.copy(
            ev.data[CsSnapshotEvent.FldType.AP.value][0]))
        self.bp.append(np.ndarray.copy(
            ev.data[CsSnapshotEvent.FldType.BP.value][0]))

    def to_numpy(self):
        def __fill_df(df, data):
            df.loc[:, "exchtime"] = self.exchtime
            df.loc[:, "localtime"] = self.localtime
            df.iloc[:, 2:] = data

        return np.array(self.exchtime, dtype=np.int64), \
                np.array(self.localtime, dtype=np.uint64), \
                np.array(self.ap, dtype=np.float64), \
                np.array(self.bp, dtype=np.float64)

    def clear(self):
        self.exchtime.clear()
        self.localtime.clear()
        self.ap.clear()
        self.bp.clear()


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


def hhmmssf_to_exchtime(val: str) -> int:
    hh: int = int(val[:2])
    mm: int = int(val[3:5])
    ss: int = int(val[6:8])
    ff: int = int(val[9:])
    time: int = hh * 3600 * int(1e9) + mm * 60 * int(1e9) + ss * int(1e9) + ff
    if hh >= 18:
        time -= 24 * 3600 * int(1e9)
    return time


def make_localtime_session(date: int, session: List) -> List[List]:
    daily_session: List = []
    str_time: str = str(date) + "00:00:00"
    time_stamp: float = time.mktime(time.strptime(str_time, "%Y%m%d%H:%M:%S"))
    today_ts: float = time_stamp * int(1e9)

    pre_business_day: int = CalendarMgr.get().shift(date, -1)
    pre_day: int = next_day(pre_business_day)
    pre_str_time: str = str(pre_day) + "00:00:00"
    pre_time_stamp: float = time.mktime(
        time.strptime(pre_str_time, "%Y%m%d%H:%M:%S"))
    pre_ts: float = pre_time_stamp * int(1e9)

    for s in session:
        if s[0] < 0:
            daily_session.append([np.uint64(i + pre_ts) for i in s])
        else:
            daily_session.append([np.uint64(i + today_ts) for i in s])

    return daily_session


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


def find_all_shift_info(
        sig_localtime: cnp.uint64_t[:], sessions: List) -> List[Dict[string, int]]:
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


cdef void match_A_with_B_cs(
        const int A_nr,
        const cnp.uint64_t[:] A_localtime,
        cnp.float64_t[:, :] ans,
        const int B_nr,
        const cnp.uint64_t[:] B_localtime,
        const cnp.float64_t[:, :] matched_data,
        const bint can_exceed):
    cdef int A_idx = 0
    cdef int B_idx = 0

    while A_idx < A_nr:
        while B_idx < B_nr and B_localtime[B_idx] <= A_localtime[A_idx]:
            B_idx += 1
        if B_idx and (can_exceed or B_idx < B_nr):
            ans[A_idx, :] = matched_data[B_idx - 1, :]

        A_idx += 1


def shift_fut_localtime(fut_localtime: cnp.uint64_t[:], shift_info: List):
    cdef int start_idx
    cdef cnp.uint64_t session_end
    cdef cnp.uint64_t time_shift

    for info in shift_info:
        start_idx = info["start_shift_idx"]
        session_end = info["session_end"]
        time_shift = info["time_shift"]
        while start_idx >= 0 and fut_localtime[start_idx] > session_end:
            fut_localtime[start_idx] += time_shift
            start_idx -= 1


class CsEdgeCalculator(SignalBase):

    def __init__(self):
        super().__init__()
        self.signame: str = ""
        self.outname: str = ""
        self.sigdir: Path = Path("")
        self.sig_file_type: SigFileType = SigFileType.csv

        self.today: int = 0
        self.targets: List[str] = []
        self.sig_df: pd.DataFrame = None
        self.cache: DataCache = DataCache()
        self.history: Dict = {}
        self.use_system_uv: bool = False
        self.futret_bias_list: List[np.uint64] = []
        self.quantile: float = 0.1
        self.session: set(tuple) = set()

    def load_signal(self):

        data_path: Path = (
            self.sigdir / self.signame / str(self.today) / f"{self.signame}-{self.today}.data.npy"
        )
        reader = SignalReader(data_path, instrument="stocks.CHN")
        df = reader.read()
        df["localtime"] = df["localtime"].astype(np.int64).astype(np.uint64)
        df["exchtime"] = df["exchtime"].astype(np.int64)
        self.sig_df = df

    def initialize(self, cfg_str: str):
        print(f"loading config")
        cfg = yaml.safe_load(cfg_str)
        self.signame = str(cfg["signame"])
        self.outname = str(cfg.get("outname", self.signame))
        self.sigdir = Path(cfg["sigdir"])
        self.sig_file_type: SigFileType = SigFileType[str(cfg["file_type"])]
        self.use_system_uv = cfg.get("use_system_uv", False)

        outdir: Path = Path(cfg["output_dir"])
        outdir.mkdir(parents=True, exist_ok=True)
        fb_start = str2ns(cfg["futret_bias_start"])
        fb_end = str2ns(cfg["futret_bias_end"])
        fb_step = str2ns(cfg["futret_bias_step"])
        self.futret_bias_list = np.arange(fb_start, fb_end + 1, fb_step)
        self.quantile = float(cfg["quantile"])
        ofile: Path = outdir / f"{self.outname}.csv"
        self.fout = open(ofile, "w", buffering=1)
        self.fout.write(f"futret_bias(ns),min_edge,max_edge,all_avg\n")

    def on_sod(self, ev: SodEvent):
        date = int(ev.date)
        if self.today != date:
            self.today = date
            self.load_signal()
            self.cache.clear()
            self.session.clear()

        print(f"on_sod:{date},ins_nr:{ev.ins_nr}")
        self.targets.clear()
        for i in range(ev.ins_nr):
            ms: MdStatic = ev.ms[i].contents
            self.targets.append(
                ms.ticker.decode("utf8") +
                "." +
                ms.exchange.decode("utf8"))
            sessions = []
            for j in range(ms.session_nr):
                sessions.append(
                    tuple([ms.session[j].begin, ms.session[j].end]))
            self.session.add(tuple(sessions))

        sig_targets: List[str] = list(self.sig_df.columns)

        for _i, _x in enumerate(sig_targets):
            if _x in ["exchtime", "localtime"]:
                continue
            if _x.startswith("SZ") or _x.startswith("SH"):
                sig_targets[_i] = f"{_x[2:]}.{_x[:2]}"
        self.sig_df.columns = sig_targets

        sig_targets = [
            _x for _x in sig_targets if _x not in [
                "exchtime", "localtime"]]
        if self.targets != sig_targets:
            if self.use_system_uv:
                new_sigs = set(self.targets) - set(sig_targets)
                extra_sigs = set(sig_targets) - set(self.targets)
                print(
                    f"use_system_uv is on,new:{len(new_sigs)},extra:{len(extra_sigs)}",
                    flush=True)
                self.sig_df[list(new_sigs)] = np.nan
                self.sig_df = self.sig_df[[
                    "exchtime", "localtime"] + self.targets].copy()
            else:
                raise RuntimeError(f"sig targets mismatch")

    def on_eod(self, ev: EodEvent):
        date = int(ev.date)
        if len(self.session) != 1:
            raise Exception(
                f"Inconsistent trading times in cross-section data")
        exch_session = list(list(i) for i in list(self.session)[0])

        for s in exch_session:
            if self.futret_bias_list[-1] > s[1] - s[0]:
                raise Exception(
                    f"futret_bias {self.futret_bias_list[-1]} exceed trading period length")

        md_exchtime, md_localtime, md_ap, md_bp = self.cache.to_numpy()
        today_data = {
            "sig_df": deepcopy(self.sig_df),
            "exch_session": deepcopy(exch_session),
            "target": deepcopy(self.targets),
            "marketdata": {
                "exchtime": md_exchtime,
                "localtime": md_localtime,
                "ap": md_ap,
                "bp": md_bp,
            }
        }

        self.history[date] = today_data

    def on_cs_snapshot(self, ev: CsSnapshotEvent):
        self.cache.push(ev)

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
        for _idx, (date, info) in enumerate(self.history.items()):
            print(f"pre-processing {_idx+1}/{len(self.history)} {date}")
            localtime_array = info["sig_df"]["localtime"].values
            local_session = make_localtime_session(date, info['exch_session'])
            shift_info[date] = find_all_shift_info(
                localtime_array, local_session)

            target_nr = len(info["target"])
            localtime_nr = len(localtime_array)

            mdinfo = info["marketdata"]
            md_mid_px[date] = (mdinfo["ap"] + mdinfo["bp"]) / 2
            md_localtime = mdinfo["localtime"]

            sig_mid_px = np.full([localtime_nr, target_nr],
                                 fill_value=np.nan, dtype=np.float64)
            match_A_with_B_cs(
                localtime_nr,
                localtime_array,
                sig_mid_px,
                md_localtime.shape[0],
                md_localtime,
                md_mid_px[date],
                True)
            sig_mid_px_list.append(
                pd.DataFrame(
                    sig_mid_px,
                    columns=info['target']))

        total_sig_mid_px = pd.concat(sig_mid_px_list)
        new_sigs = set(all_tgt) - set(total_sig_mid_px.columns)
        total_sig_mid_px[list(new_sigs)] = np.nan
        sig_mid_pxs = total_sig_mid_px[all_tgt].values

        len_fb = len(self.futret_bias_list)
        for _i, fb in enumerate(self.futret_bias_list):
            print(f"calculating futret bias {_i+1}/{len(self.futret_bias_list)}")
            sig_fut_mid_px_list: List = []
            for date, info in self.history.items():
                localtime_array = info["sig_df"]["localtime"].values
                fut_localtime = localtime_array + fb
                shift_fut_localtime(fut_localtime, shift_info[date])
                target_nr = len(info["target"])
                localtime_nr = len(localtime_array)
                fut_mid_px = np.full([localtime_nr, target_nr],
                                     fill_value=np.nan, dtype=np.float64)
                md_localtime = info["marketdata"]["localtime"]
                match_A_with_B_cs(
                    localtime_nr,  # fut_localtime_nr,
                    fut_localtime,
                    fut_mid_px,
                    md_localtime.shape[0],
                    md_localtime,
                    md_mid_px[date],
                    False)
                sig_fut_mid_px_list.append(pd.DataFrame(
                    fut_mid_px, columns=info['target']))

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
    return CsEdgeCalculator()
