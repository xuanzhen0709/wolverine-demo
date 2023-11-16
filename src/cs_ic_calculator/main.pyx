import numpy as np
from enum import IntEnum
import yaml
from pathlib import Path
import pandas as pd
import time
from typing import List, Dict
from cfi.wolverine.signal import *
from .business_calendar import *

import cython
# libc.stdint provide c-native types c_uint32 etc
from libc.stdint cimport *

# speed up numpy arrays
cimport numpy as np
np.import_array()


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
        self.ap.append(np.ndarray.copy(ev.data[CsSnapshotEvent.FldType.AP.value][0]))
        self.bp.append(np.ndarray.copy(ev.data[CsSnapshotEvent.FldType.BP.value][0]))

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


def hhmmssf_to_exchtime(val: str) -> int:
    hh: int = int(val[:2])
    mm: int = int(val[3:5])
    ss: int = int(val[6:8])
    ff: int = int(val[9:])
    time: int = hh * 3600 * int(1e9) + mm * 60 * int(1e9) + ss * int(1e9) + ff
    if hh >= 18:
        time -= 24 * 3600 * int(1e9)
    return time


def make_localtime_session(date: int,  session: List) -> List[List]:
    daily_session: List = []
    str_time: str = str(date) + '00:00:00'
    time_stamp: float = time.mktime(time.strptime(str_time, '%Y%m%d%H:%M:%S'))
    today_ts: float = time_stamp * int(1e9)

    pre_business_day: int = Calendar.get_instance().next_business_day(date, -1)
    pre_day: int = next_day(pre_business_day)
    pre_str_time: str = str(pre_day) + '00:00:00'
    pre_time_stamp: float = time.mktime(time.strptime(pre_str_time, '%Y%m%d%H:%M:%S'))
    pre_ts: float = pre_time_stamp * int(1e9)
    
    for s in session:
        if s[0] < 0:
            daily_session.append([ np.uint64(i + pre_ts) for i in s ])
        else:
            daily_session.append([ np.uint64(i + today_ts) for i in s ])

    return daily_session


cdef int search_session_end_index(
        const np.uint64_t[:] sig_localtime,
        const np.uint64_t target,
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


def find_all_shift_info(sig_localtime_df: pd.DataFrame, sessions: List) -> List[ Dict[string, int] ]:
    left: int = 0
    right: int = len(sig_localtime_df) - 1
    session_num: int = len(sessions)
    ans: List[ Dict[string, int] ] = []

    for i in range(session_num - 1):
        idx = search_session_end_index(sig_localtime_df.values, sessions[i][1], left, right)
        if idx != -1:
            ans.append({
                "session_end": sessions[i][1],
                "start_shift_idx": idx,
                "time_shift": sessions[i+1][0] - sessions[i][1]
            })
            left = idx + 1

    return ans


cdef void match_sig_with_md(
        const int sig_nr,
        const np.uint64_t[:] sig_localtime,
        np.float64_t[:, :] sig_mid_px,
        const np.uint64_t[:] sig_fut_localtime,
        np.float64_t[:, :] sig_fut_mid_px,
        const int md_nr,
        const np.uint64_t[:] md_localtime,
        const np.float64_t[:, :] md_mid_px):
    cdef int sig_idx = 0
    cdef int ret_idx = 0
    cdef int fut_ret_idx = 0
    while sig_idx < sig_nr:
        while ret_idx < md_nr and md_localtime[ret_idx] <= sig_localtime[sig_idx]:
            ret_idx += 1
        if ret_idx:
            sig_mid_px[sig_idx, :] = md_mid_px[ret_idx - 1, :]

        while fut_ret_idx < md_nr and md_localtime[fut_ret_idx] <= sig_fut_localtime[sig_idx]:
            fut_ret_idx += 1
        if fut_ret_idx and fut_ret_idx < md_nr:
            sig_fut_mid_px[sig_idx, :] = md_mid_px[fut_ret_idx - 1, :]

        sig_idx += 1


def extend_sig_df(
        const int df_size,
        const np.uint64_t[:] sig_localtime,
        const np.int64_t[:] sig_exchtime,
        local_session:List,
        exch_session:List, 
        ffill_interval:int) -> (List, List):
    localtime_list: List = []
    exchtime_list: List = []
    ic_info_list: List = []
    
    session_num = len(local_session)
    last_idx = df_size - 1 

    cur_session = 0
    cur_localtime = local_session[cur_session][0]
    cur_exchtime = exch_session[cur_session][0]
    while cur_localtime < sig_localtime[0]:
        cur_localtime += ffill_interval
        cur_exchtime += ffill_interval

    for i in range(last_idx):
        next_localtime = sig_localtime[i+1]
        while cur_localtime < next_localtime:
            localtime_list.append(cur_localtime)
            ic_info_list.append(i)
            exchtime_list.append(cur_exchtime)
            cur_localtime += ffill_interval
            cur_exchtime += ffill_interval
            if cur_localtime > local_session[cur_session][1]:
                if cur_session + 1 < session_num and  sig_localtime[i] >=  local_session[cur_session+1][0]:
                    cur_session += 1
                    if i+1 < df_size:
                        cur_localtime = local_session[cur_session][0]
                        cur_exchtime = exch_session[cur_session][0]
                        while cur_localtime < sig_localtime[i+1]:
                            localtime_list.append(cur_localtime)
                            ic_info_list.append(i)
                            exchtime_list.append(cur_exchtime)
                            cur_localtime += ffill_interval
                            cur_exchtime += ffill_interval
                break
           
    localtime = sig_localtime[last_idx]
    exchtime = sig_exchtime[last_idx]
    while localtime <= local_session[-1][1]:
        localtime_list.append(localtime)
        ic_info_list.append(last_idx)
        exchtime_list.append(exchtime)
        localtime += ffill_interval
        exchtime += ffill_interval
    
    return localtime_list, exchtime_list, ic_info_list


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
        self.futret_bias_str: str = ""
        self.futret_bias: int = -1
        self.cache: DataCache = DataCache()
        self.ffill_interval: int = 0
        self.use_system_uv: bool = False

        self.session: set(tuple) = set()

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
            targets: List[str] = [x.decode("utf8") for x in uv]

            exchtime = np.memmap(date_dir.joinpath(f"{signame}-{date}.ts.npy"), dtype=np.int64, mode="r")
            localtime = np.memmap(date_dir.joinpath(f"{signame}-{date}.localts.npy"), dtype=np.uint64, mode="r")
            sigs = np.memmap(date_dir.joinpath(f"{signame}-{date}.data.npy"), dtype=np.float64, mode="r", shape=(exchtime.shape[0], len(targets)))
            
            df: pd.DataFrame = pd.DataFrame(sigs, columns=targets)
            df["exchtime"] = exchtime
            df["localtime"] = localtime
            df = df[["exchtime", "localtime"] + targets]
            return df

        if self.sig_file_type == SigFileType.csv:
            self.sig_df = __load_csv(self.sigdir, self.signame, self.today)
        else:
            self.sig_df = __load_npy(self.sigdir, self.signame, self.today)

    def initialize(self, cfg_str: str):
        print(f"loading config")
        cfg = yaml.safe_load(cfg_str)
        self.signame = str(cfg["signame"])
        self.outname = str(cfg.get("outname", self.signame))
        self.sigdir = Path(cfg["sigdir"])
        self.sig_file_type: SigFileType = SigFileType[str(cfg["file_type"])]
        self.futret_bias_str = str(cfg["futret_bias"])
        self.futret_bias = str2ns(self.futret_bias_str)
        if 'ffill_interval' in cfg:
            self.ffill_interval = str2ns(str(cfg["ffill_interval"]))
        
        self.use_system_uv = cfg.get("use_system_uv", False)
        
        odir: Path = Path(cfg["output_dir"])
        odir.mkdir(parents=True, exist_ok=True)
        ofile: Path = odir / f"{self.outname}.csv"
        self.fout = open(ofile, "w", buffering=1)
        self.fout.write(f"date,exchtime,localtime,ic\n")

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
            self.targets.append(ms.ticker.decode("utf8") + "." + ms.exchange.decode("utf8"))
            sessions = []
            for j in range(ms.session_nr):
                sessions.append(tuple([ms.session[j].begin, ms.session[j].end]))
            self.session.add(tuple(sessions))

        sig_targets: List[str] = list(self.sig_df.columns)

        for _i, _x in enumerate(sig_targets):
            if _x in ["exchtime", "localtime"]:
                continue
            if _x.startswith("SZ") or _x.startswith("SH"):
                sig_targets[_i] = f"{_x[2:]}.{_x[:2]}"
        self.sig_df.columns = sig_targets

        sig_targets = [_x for _x in sig_targets if _x not in ["exchtime", "localtime"]]
        if self.targets != sig_targets:
            if self.use_system_uv:
                new_sigs = set(self.targets) - set(sig_targets)
                extra_sigs = set(sig_targets) - set(self.targets)
                print(f"use_system_uv is on,new:{len(new_sigs)},extra:{len(extra_sigs)}", flush=True)
                self.sig_df[list(new_sigs)] = np.nan
                self.sig_df = self.sig_df[["exchtime", "localtime"] + self.targets].copy()
            else:
                raise RuntimeError(f"sig targets mismatch")

    def on_eod(self, ev: EodEvent):
        date = int(ev.date)
        if len(self.session) != 1:
            raise Exception(f"Inconsistent trading times in cross-section data")
        exch_session = list( list(i) for i in list(self.session)[0])
        for s in exch_session:
            if self.futret_bias > s[1] - s[0]:
                raise Exception(f"futret_bias {self.futret_bias_str} exceed trading period length")

        print(f"{date},calculating ic")
        local_session = make_localtime_session(date, exch_session)
        shift_info = find_all_shift_info(self.sig_df["localtime"], local_session)
        
        localtime_array = self.sig_df["localtime"].values
        exchtime_array = self.sig_df["exchtime"].values
        sig_array = self.sig_df[self.targets].astype(np.float64).values
        sig_shape = (len(localtime_array), len(self.targets))

        if self.ffill_interval:
            self.sig_df = self.sig_df[self.sig_df["localtime"]<=local_session[-1][1]]
            localtime_list, exchtime_list, ic_info_list = extend_sig_df(len(self.sig_df), 
                                                                        self.sig_df["localtime"].values, 
                                                                        self.sig_df["exchtime"].values, 
                                                                        local_session,
                                                                        exch_session, 
                                                                        self.ffill_interval)
            localtime_array = np.array(localtime_list, dtype=np.uint64)
            exchtime_array = np.array(exchtime_list, dtype=np.int64)
            filled_size = len(localtime_list)
            sig_shape = (filled_size, len(self.targets))
            sig_array_filled = np.full(sig_shape, fill_value=np.nan, dtype=np.float64)

            for i in range(filled_size):
                sig_array_filled[i] =  sig_array[ic_info_list[i]]
            sig_array = sig_array_filled

        cdef np.uint64_t[:] sig_localtime = localtime_array
        cdef np.int64_t[:] sig_exchtime = exchtime_array
        cdef np.ndarray[np.float64_t, ndim=2] sigs = sig_array
        cdef np.ndarray[np.float64_t, ndim=2] mid_px = np.full(sig_shape, fill_value=np.nan, dtype=np.float64)
        cdef np.ndarray[np.float64_t, ndim=2] fut_mid_px = np.full(sig_shape, fill_value=np.nan, dtype=np.float64)
        cdef np.ndarray[np.uint64_t, ndim=1] md_localtime = np.array(self.cache.localtime, dtype=np.uint64)
        cdef np.ndarray[np.float64_t, ndim=2] md_mid_px = (np.array(self.cache.ap, dtype=np.float64) + np.array(self.cache.bp, dtype=np.float64)) / 2

        cdef np.uint64_t[:] fut_localtime = localtime_array + self.futret_bias
        cdef int start_idx 
        cdef np.uint64_t session_end
        cdef np.uint64_t time_shift 
        
        for info in shift_info:
            start_idx = info['start_shift_idx']
            session_end = info['session_end']
            time_shift = info['time_shift']

            while fut_localtime[start_idx] > session_end:
                fut_localtime[start_idx] += time_shift
                start_idx -= 1

        match_sig_with_md(len(sig_localtime),
                sig_localtime,
                mid_px,
                fut_localtime,
                fut_mid_px,
                md_localtime.shape[0],
                md_localtime,
                md_mid_px)
        cdef np.ndarray[np.float64_t, ndim=2] fut_ret = (fut_mid_px / mid_px) - 1

        cdef np.float64_t ic
        cdef int idx = 0
        a = time.time()
        while idx < len(sigs):
            ic = np.ma.corrcoef(
                            np.ma.masked_invalid(sigs[idx, :]),
                            np.ma.masked_invalid(fut_ret[idx, :])
                        )[0][1]
            self.fout.write(f"{date},{sig_exchtime[idx]},{sig_localtime[idx]},{ic}\n")
            idx += 1
        b = time.time()
        print(f"time:{b - a}")

        #sig_1d = np.ma.masked_invalid(np.ravel(sigs))
        #fut_ret_1d = np.ma.masked_invalid(np.ravel(fut_ret))
        #corr = np.ma.corrcoef(sig_1d, fut_ret_1d)
        #fut_ret_all.append(corr[0][1])


    def on_cs_snapshot(self, ev: CsSnapshotEvent):
        self.cache.push(ev)


def pysig_create():
    return CsICCalculator()
