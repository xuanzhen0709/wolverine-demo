import numpy as np
from enum import IntEnum
import yaml
from pathlib import Path
import pandas as pd
import time
from typing import List, Dict
from cfi.wolverine.signal import *
from cfi.wolverine.event import *

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

    def clear(self):
        self.exchtime.clear()
        self.localtime.clear()
        self.ap.clear()
        self.bp.clear()


def str2ns(val: str) -> int:
    if val.endswith("ns"):
        val_ns = int(val[:-2])
    elif val.endswith("s"):
        val_ns = int(val[:-1]) * int(1e9)
    elif val.endswith("m"):
        val_ns = int(val[:-1]) * int(1e9) * 60
    elif val.endswith("h"):
        val_ns = int(val[:-1]) * int(1e9) * 3600
    else:
        raise RuntimeError(f"unknown unit {val}")
    return val_ns

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

def make_filled_times(ffill_interval: int,
                      local_session: List,
                      exch_session: List) -> (cnp.uint64_t[:], cnp.int64_t[:], List):

    shift_info: List[Dict[string, int]] = []
    idx: int = 0
    ffilled_local_arrays: List = []
    session_num = len(local_session)
    for i, session in enumerate(local_session):
        tmp = np.arange(
            session[0],
            session[1] + 1,
            ffill_interval,
            dtype=np.uint64)
        ffilled_local_arrays.append(tmp)
        idx += len(tmp)
        if i < session_num - 1:
            shift_info.append({
                "session_end": session[1],
                "start_shift_idx": idx - 1,
                "time_shift": local_session[i + 1][0] - session[1]
            })

    ffilled_local_array = np.hstack(ffilled_local_arrays)

    ffilled_exch_arrays: List = []
    for session in exch_session:
        tmp = np.arange(session[0], session[1], ffill_interval, dtype=np.int64)
        ffilled_exch_arrays.append(tmp)
    ffilled_exch_array = np.hstack(ffilled_exch_arrays)

    return ffilled_local_array, ffilled_exch_array, shift_info


def shift_fut_localtime(fut_localtime: cnp.uint64_t[:], shift_info: List):
    cdef int start_idx
    cdef cnp.uint64_t session_end
    cdef cnp.uint64_t time_shift

    for info in shift_info:
        start_idx = info["start_shift_idx"]
        session_end = info["session_end"]
        time_shift = info["time_shift"]

        while fut_localtime[start_idx] > session_end:
            fut_localtime[start_idx] += time_shift
            start_idx -= 1


class CsICCalculator(SignalBase):

    def __init__(self):
        super().__init__()
        self.signame: str = ""
        self.outname: str = ""
        self.sigdir: Path = Path("")
        self.sig_file_type: SigFileType = SigFileType.csv

        self.today: int = 0
        self.targets: List[str] = []
        self.sig_df: pd.DataFrame = None
        self.futret_bias: Dict[int, str] = {}
        self.cache: DataCache = DataCache()
        self.ffill_interval: int = 0
        self.use_system_uv: bool = False

        self.session: set(tuple) = set()

    def load_signal(self):
        data_path: Path = (
            self.sigdir / self.signame / str(self.today) / f"{self.signame}-{self.today}.data.npy"
        )
        reader = SignalReader(data_path, instrument="stocks.CHN")
        df = reader.read(convert_localtime=False)
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

        for futret_bias_str in cfg["futret_bias"]:
            if futret_bias_str.endswith("ns"):
                self.futret_bias[int(futret_bias_str[:-2])] = futret_bias_str
            elif futret_bias_str.endswith("s"):
                self.futret_bias[int(futret_bias_str[:-1])
                                 * int(1e9)] = futret_bias_str
            elif futret_bias_str.endswith("m"):
                self.futret_bias[int(futret_bias_str[:-1])
                                 * int(1e9) * 60] = futret_bias_str
            elif futret_bias_str.endswith("h"):
                self.futret_bias[int(futret_bias_str[:-1])
                                 * int(1e9) * 3600] = futret_bias_str
            else:
                raise RuntimeError(f"unknown unit {futret_bias_str}")

        if "ffill_interval" in cfg:
            self.ffill_interval = str2ns(str(cfg["ffill_interval"]))

        self.use_system_uv = cfg.get("use_system_uv", False)

        odir: Path = Path(cfg["output_dir"])
        odir.mkdir(parents=True, exist_ok=True)
        ofile: Path = odir / f"{self.outname}.csv"
        self.fout = open(ofile, "w", buffering=1)
        self.fout.write(
            f"date,exchtime,localtime," +
            ",".join(
                self.futret_bias.values()) +
            "\n")

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

        for futret_bias, str_futret_bias in self.futret_bias.items():
            for s in exch_session:
                if futret_bias > s[1] - s[0]:
                    raise Exception(
                        f"futret_bias {str_futret_bias} exceed trading period length")

        print(f"{date},calculating ic")
        local_session = make_localtime_session(date, exch_session)
        target_nr = len(self.targets)
        if self.ffill_interval:
            localtime_array, exchtime_array, shift_info = make_filled_times(
                self.ffill_interval, local_session, exch_session)
            localtime_nr = len(localtime_array)

            sig_array = np.full([localtime_nr, target_nr],
                                fill_value=np.nan, dtype=np.float64)
            match_A_with_B_cs(
                localtime_nr,
                localtime_array,
                sig_array,
                len(self.sig_df),
                self.sig_df["localtime"].values,
                self.sig_df[self.targets].astype(np.float64).values,
                True)

        else:
            localtime_array = self.sig_df["localtime"].values
            exchtime_array = self.sig_df["exchtime"].values
            shift_info = find_all_shift_info(localtime_array, local_session)
            localtime_nr = len(localtime_array)
            sig_array = self.sig_df[self.targets].astype(np.float64).values

        md_mid_px = (np.array(self.cache.ap, dtype=np.float64) +
                     np.array(self.cache.bp, dtype=np.float64)) / 2
        md_localtime = np.array(self.cache.localtime, dtype=np.uint64)

        sig_mid_px = np.full([localtime_nr, target_nr],
                             fill_value=np.nan, dtype=np.float64)
        match_A_with_B_cs(
            localtime_nr,
            localtime_array,
            sig_mid_px,
            md_localtime.shape[0],
            md_localtime,
            md_mid_px,
            True)

        all_ic: Dict = {}
        for fb, str_fb in self.futret_bias.items():
            print(f"start ic calculating for futret_bias {str_fb}")
            fut_localtime = localtime_array + fb
            shift_fut_localtime(fut_localtime, shift_info)
            # fut_localtime_nr = np.count_nonzero(fut_localtime <= local_session[-1][-1])
            fut_mid_px = np.full([localtime_nr, target_nr],
                                 fill_value=np.nan, dtype=np.float64)
            match_A_with_B_cs(
                localtime_nr, # fut_localtime_nr,
                fut_localtime,
                fut_mid_px,
                md_localtime.shape[0],
                md_localtime,
                md_mid_px,
                False)
            fut_ret = (fut_mid_px / sig_mid_px) - 1

            ic_list = []
            idx = 0
            a = time.time()
            while idx < localtime_nr:
                ic = np.ma.corrcoef(
                    np.ma.masked_invalid(sig_array[idx, :]),
                    np.ma.masked_invalid(fut_ret[idx, :])
                )[0][1]
                ic_list.append(ic)
                idx += 1
            b = time.time()
            print(f"time:{b - a}")
            all_ic[fb] = ic_list

        idx = 0
        # convert localtime to Local TimeZone
        localtime_array = pd.to_datetime(localtime_array, utc=True) \
            .tz_convert("Asia/Shanghai") \
            .tz_localize(None) \
            .astype(np.int64)

        while idx < localtime_nr:
            ic_str = ",".join(
                f"{all_ic[fb][idx]}" if all_ic[fb][idx] else "" for fb in self.futret_bias)
            self.fout.write(
                f"{date},{exchtime_array[idx]},{localtime_array[idx]},{ic_str}\n")
            idx += 1

    def on_cs_snapshot(self, ev: CsSnapshotEvent):
        self.cache.push(ev)


def pysig_create():
    return CsICCalculator()
