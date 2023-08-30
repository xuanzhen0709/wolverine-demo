import numpy as np
from enum import IntEnum
import yaml
from pathlib import Path
import pandas as pd
import time

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


def hhmmssf_to_exchtime(val: str) -> int:
    hh: int = int(val[:2])
    mm: int = int(val[3:5])
    ss: int = int(val[6:8])
    ff: int = int(val[9:])
    time: int = hh * 3600 * int(1e9) + mm * 60 * int(1e9) + ss * int(1e9) + ff
    if hh >= 18:
        time -= 24 * 3600 * int(1e9)
    return time


def make_sessions(date: int,  session: List):
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


class CsICCalculator(SignalBase):

    def __init__(self):
        super().__init__()
        self.signame: str = ""
        self.sigdir: Path = Path("")
        self.sig_file_type: SigFileType = SigFileType.csv

        self.today: int = 0
        self.targets: List[str] = []
        self.sig_df: pd.DataFrame = None
        self.futret_bias_str: str = ""
        self.futret_bias: int = -1
        self.cache: DataCache = DataCache()

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
        self.sigdir = Path(cfg["sigdir"])
        self.sig_file_type: SigFileType = SigFileType[str(cfg["file_type"])]
        self.futret_bias_str = str(cfg["futret_bias"])
        if self.futret_bias_str.endswith("ns"):
            self.futret_bias = int(self.futret_bias_str[:-2])
        elif self.futret_bias_str.endswith("s"):
            self.futret_bias = int(self.futret_bias_str[:-1]) * int(1e9)
        elif self.futret_bias_str.endswith("m"):
            self.futret_bias = int(self.futret_bias_str[:-1]) * int(1e9) * 60
        elif self.futret_bias_str.endswith("h"):
            self.futret_bias = int(self.futret_bias_str[:-1]) * int(1e9) * 3600
        else:
            raise RuntimeError("unknown unit {futret_bias_str}")
        odir: Path = Path(cfg["output_dir"])
        odir.mkdir(parents=True, exist_ok=True)
        ofile: Path = odir / f"{self.signame}.csv"
        self.fout = open(ofile, "w", buffering=1)
        self.fout.write(f"date,exchtime,localtime,ic\n")

    def on_sod(self, date: int, ev: SodEvent):
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

        sig_targets: List[str] = [x for x in self.sig_df.columns if x not in ["exchtime", "localtime"]]
        if self.targets != sig_targets:
            raise RuntimeError(f"sig targets mismatch")

    def on_eod(self, date: int):
        if len(self.session) != 1:
            raise Exception(f"Inconsistent trading times in cross-section data")
        session_list = list( list(i) for i in list(self.session)[0])
        for s in session_list:
            if self.futret_bias > s[1] - s[0]:
                raise Exception(f"futret_bias {self.futret_bias_str} exceed trading period length")
        
        print(f"{date},calculating ic")
        all_session = make_sessions(date, session_list)
        shift_info = find_all_shift_info(self.sig_df["localtime"], all_session)

        cdef np.uint64_t[:] sig_localtime = self.sig_df["localtime"].values
        cdef np.ndarray[np.float64_t, ndim=2] sigs = self.sig_df[self.targets].astype(np.float64).values
        cdef np.ndarray[np.float64_t, ndim=2] mid_px = np.full_like(self.sig_df[self.targets].values, fill_value=np.nan, dtype=np.float64)
        cdef np.ndarray[np.float64_t, ndim=2] fut_mid_px = np.full_like(self.sig_df[self.targets].values, fill_value=np.nan, dtype=np.float64)
        cdef np.ndarray[np.uint64_t, ndim=1] md_localtime = np.array(self.cache.localtime, dtype=np.uint64)
        cdef np.ndarray[np.float64_t, ndim=2] md_mid_px = (np.array(self.cache.ap, dtype=np.float64) + np.array(self.cache.bp, dtype=np.float64)) / 2

        cdef np.uint64_t[:] fut_localtime = self.sig_df["localtime"].values + self.futret_bias
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

        match_sig_with_md(len(self.sig_df.index),
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
        cdef np.int64_t[:] sig_exchtime = self.sig_df["exchtime"].values
        a = time.time()
        while idx < len(self.sig_df.index):
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
