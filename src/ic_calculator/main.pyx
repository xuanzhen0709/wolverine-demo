import time
import numpy as np
from enum import IntEnum
import yaml
from pathlib import Path
import pandas as pd
from typing import Dict, Set, List
from abc import ABC, abstractmethod
from cfi.wolverine.signal import *
from .business_calendar import *

import cython
# libc.stdint provide c-native types c_uint32 etc
from libc.stdint cimport *

# speed up numpy arrays
cimport numpy as np
np.import_array()
np.set_printoptions(suppress=True)


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


cdef void match_sig_with_md_ts(
        const int sig_nr,
        const np.uint64_t[:] sig_localtime,
        np.float64_t[:] sig_mid_px,
        const np.uint64_t[:] sig_fut_localtime,
        np.float64_t[:] sig_fut_mid_px,
        const int md_nr,
        const np.uint64_t[:] md_localtime,
        const np.float64_t[:] md_mid_px):
    cdef int sig_idx = 0
    cdef int ret_idx = 0
    cdef int fut_ret_idx = 0
    while sig_idx < sig_nr:
        while ret_idx < md_nr and md_localtime[ret_idx] <= sig_localtime[sig_idx]:
            ret_idx += 1
        if ret_idx:
            sig_mid_px[sig_idx] = md_mid_px[ret_idx - 1]

        while fut_ret_idx < md_nr and md_localtime[fut_ret_idx] <= sig_fut_localtime[sig_idx]:
            fut_ret_idx += 1
        if fut_ret_idx and fut_ret_idx < md_nr:
            sig_fut_mid_px[sig_idx] = md_mid_px[fut_ret_idx - 1]

        sig_idx += 1


cdef void match_sig_with_md_cs(
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


def find_all_shift_info(sig_localtime_df: pd.DataFrame,
                        sessions: List) -> List[Dict[string, int]]:
    left: int = 0
    right: int = len(sig_localtime_df) - 1
    session_num: int = len(sessions)
    ans: List[Dict[string, int]] = []

    for i in range(session_num - 1):
        idx = search_session_end_index(
            sig_localtime_df.values, sessions[i][1], left, right)
        if idx != -1:
            ans.append({
                "session_end": sessions[i][1],
                "start_shift_idx": idx,
                "time_shift": sessions[i + 1][0] - sessions[i][1]
            })
            left = idx + 1

    return ans


def shift_fut_localtime(fut_localtime_df: pd.DataFrame, shift_info: List):
    cdef np.uint64_t[:] fut_localtime = fut_localtime_df.values
    cdef int start_idx
    cdef np.uint64_t session_end
    cdef np.uint64_t time_shift

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


def calculate_fut_ret(ret_df: pd.DataFrame, sig_df: pd.DataFrame,
                      futret_bias: int, shift_info: List) -> pd.DataFrame:
    ret_df["localtime"] = ret_df["localtime"].astype(np.uint64)
    ret_df["mid_px"] = (ret_df["ap"] + ret_df["bp"]) / 2
    sig_df["fut_localtime"] = sig_df["localtime"] + futret_bias
    #--------------------------- mask ---------------------------#
    # ts = sig_df["localtime"]
    # new_ts = sig_df["fut_localtime"]
    # for i in range(len(name2all_session[cache.tk_exch])-1):
    #     start = name2all_session[cache.tk_exch][i][0]
    #     end = name2all_session[cache.tk_exch][i][1]
    #     diff = name2all_session[cache.tk_exch][i+1][0] - end
    #     mask = (ts >= start) & (ts < end)
    #     new_ts.loc[mask] += diff
    #--------------------------- mask ---------------------------#
    shift_fut_localtime(sig_df["fut_localtime"], shift_info)

    sig_df["mid_px"] = np.nan
    sig_df["fut_mid_px"] = np.nan
    match_sig_with_md_ts(len(sig_df.index),
                         sig_df["localtime"].values,
                         sig_df["mid_px"].values,
                         sig_df["fut_localtime"].values,
                         sig_df["fut_mid_px"].values,
                         len(ret_df.index),
                         ret_df["localtime"].values,
                         ret_df["mid_px"].values)
    sig_df["fut_ret"] = (sig_df["fut_mid_px"] / sig_df["mid_px"]) - 1
    return sig_df


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


class SnapshotCache(ABC):

    def __init__(
            self, futret_bias: Dict[int, str], ofile: Path, map_file: Path):
        pass

    @abstractmethod
    def push(self, ev: Union[SnapshotEvent, CsSnapshotEvent]):
        pass

    @abstractmethod
    def clear(self):
        pass

    @abstractmethod
    def on_sod(self, ev: SodEvent, sig_df: pd.DataFrame, today: int):
        pass

    @abstractmethod
    def on_eod(self, date: int):
        pass


class TsSnapshotCache(SnapshotCache):

    def __init__(self, futret_bias: Dict[int, str], ofile: Path):
        self.cache: Dict[int, InsDataCache] = {}
        self.name2sessions: Dict[string, List] = {}
        self.futret_bias = futret_bias
        self.sig_df: pd.DataFrame = None
        self.fout = open(ofile, "w", buffering=1)
        self.fout.write(f"date," + ",".join(self.futret_bias.values()) + "\n")

    def push(self, ev: SnapshotEvent):
        ms_ptr: int = ctypes.cast(ev.ms, ctypes.c_void_p).value
        # ms: MdStatic = ev.ms.contents
        ss: MdSnapshot = ev.snapshot.contents
        self.cache[ms_ptr].push(ss)

    def clear(self):
        self.cache.clear()
        self.name2sessions.clear()

    def on_sod(self, ev: SodEvent, sig_df: pd.DataFrame, today: int):
        date: int = int(ev.date)
        if today != date:
            self.clear()
            self.sig_df = sig_df

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

    def check_futret_bias(self):
        for futret_bias, str_futret_bias in self.futret_bias.items():
            for tk_exch, session_list in self.name2sessions.items():
                for session in session_list:
                    if futret_bias > session[1] - session[0]:
                        raise Exception(
                            f"futret_bias {str_futret_bias} exceed trading period length of {tk_exch}")

    def on_eod(self, date: int):
        self.check_futret_bias()
        name2shift_info: Dict[string, List] = {}
        # mask: name2all_session : Dict[string, List] = {}
        for tk_exch, sessions in self.name2sessions.items():
            all_session = make_localtime_session(date, sessions)
            name2shift_info[tk_exch] = find_all_shift_info(
                self.sig_df["localtime"], all_session)
            # mask: name2all_session[tk_exch] = all_session

        fut_ret_all = []
        for fb, str_fb in self.futret_bias.items():
            print(f"start ic calculating for futret_bias {str_fb}")
            T_fb_start = time.perf_counter()
            sigs = []
            fut_ret = []
            for cache in self.cache.values():
                sig_df = self.sig_df[["exchtime",
                                      "localtime", cache.tk_exch]].copy()
                ret_df = cache.cache2df()
                print(f"start match sig & md for {cache.tk_exch}")
                T_match_start = time.perf_counter()
                sig_df: pd.DataFrame = calculate_fut_ret(
                    ret_df, sig_df, fb, name2shift_info[cache.tk_exch])
                T_match_end = time.perf_counter()
                T_match_cost = (T_match_end - T_match_start) * 1000
                print(
                    f"end match sig & md for {cache.tk_exch}, total cost {T_match_cost}ms.")

                sigs.append(sig_df[cache.tk_exch])
                fut_ret.append(sig_df["fut_ret"])

            print(f"start calculating ic")
            T_calculate_start = time.perf_counter()
            sig_1d = np.ma.masked_invalid(np.ravel(sigs))
            fut_ret_1d = np.ma.masked_invalid(np.ravel(fut_ret))
            corr = np.ma.corrcoef(sig_1d, fut_ret_1d)
            T_calculate_end = time.perf_counter()
            T_calculate_cost = (T_calculate_end - T_calculate_start) * 1000
            print(f"end calculating ic, total cost {T_calculate_cost}ms.")

            fut_ret_all.append(corr[0][1])
            T_fb_end = time.perf_counter()
            T_fb_cost = (T_fb_end - T_fb_start) * 1000
            print(
                f"end ic calculating for futret_bias {str_fb}, total cost {T_fb_cost}ms.")

        self.fout.write(
            f"{date}," +
            ",".join(
                f"{x}" for x in fut_ret_all) +
            "\n")


class CsSnapshotCache(SnapshotCache):

    def __init__(
            self, futret_bias: Dict[int, str], ofile: Path, map_file: Path):
        self.exchtime = []
        self.localtime = []
        self.ap = []
        self.bp = []
        self.session: set(tuple) = set()
        self.futret_bias = futret_bias
        self.fout = open(ofile, "w", buffering=1)
        self.sig_df: pd.DataFrame = None
        self.map_info: pd.DataFrame = pd.read_excel(
            map_file, header=None, sheet_name=None)
        self.fut_target: str = None
        self.stock_targets: pd.Series = None
        self.md_targets: List[str] = []

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
        self.session.clear()

    def on_sod(self, ev: SodEvent, sig_df: pd.DataFrame, today: int):
        date: int = int(ev.date)
        if today != date:
            self.clear()
            self.sig_df = sig_df

        self.md_targets.clear()
        for i in range(ev.ins_nr):
            ms: MdStatic = ev.ms[i].contents
            self.md_targets.append(
                ms.ticker.decode("utf8") +
                "." +
                ms.exchange.decode("utf8"))
            sessions = []
            for j in range(ms.session_nr):
                sessions.append(
                    tuple([ms.session[j].begin, ms.session[j].end]))
            self.session.add(tuple(sessions))

        sig_targets: List[str] = [
            x for x in self.sig_df.columns if x not in [
                "exchtime", "localtime"]]

        if len(sig_targets) != 1:
            raise RuntimeError("multi targets in signal file!")

        if None is self.fut_target:
            self.fut_target = sig_targets[0]
            self.stock_targets = self.map_info[self.fut_target][0]
            self.fout.write(f"date," + ",".join(self.stock_targets) + "\n")
        else:
            if self.fut_target != sig_targets[0]:
                raise RuntimeError(
                    "Today's target in signal file is different from the previous day's!")

    def on_eod(self, date: int):

        if len(self.session) != 1:
            raise Exception(
                f"Inconsistent trading times in cross-section data")
        exch_session = list(list(i) for i in list(self.session)[0])

        futret_bias = list(self.futret_bias.keys())[0]
        futret_bias_str = list(self.futret_bias.values())[0]
        for s in exch_session:
            if futret_bias > s[1] - s[0]:
                raise Exception(
                    f"futret_bias {futret_bias_str} exceed trading period length")

        print(f"{date},calculating ic")
        local_session = make_localtime_session(date, exch_session)
        shift_info = find_all_shift_info(
            self.sig_df["localtime"], local_session)
        cdef np.uint64_t[:] sig_localtime = self.sig_df["localtime"].values
        cdef np.float64_t[:] sigs = self.sig_df[self.fut_target].astype(np.float64).values
        cdef np.uint64_t[:] fut_localtime = self.sig_df["localtime"].values + futret_bias
        sig_shape = (len(sig_localtime), len(self.md_targets))
        cdef np.ndarray[np.float64_t, ndim = 2] mid_px = np.full(sig_shape, fill_value=np.nan, dtype=np.float64)
        cdef np.ndarray[np.float64_t, ndim = 2] fut_mid_px = np.full(sig_shape, fill_value=np.nan, dtype=np.float64)
        cdef np.ndarray[np.uint64_t, ndim = 1] md_localtime = np.array(self.localtime, dtype=np.uint64)
        cdef np.ndarray[np.float64_t, ndim = 2] md_mid_px = (np.array(self.ap, dtype=np.float64) + np.array(self.bp, dtype=np.float64)) / 2

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

        match_sig_with_md_cs(len(self.sig_df.index),
                             sig_localtime,
                             mid_px,
                             fut_localtime,
                             fut_mid_px,
                             md_localtime.shape[0],
                             md_localtime,
                             md_mid_px)

        cdef np.ndarray[np.float64_t, ndim = 2] fut_ret = (fut_mid_px / mid_px) - 1

        target_idx: List = []
        for tgt in self.stock_targets:
            try:
                idx = self.md_targets.index(tgt)
                target_idx.append(idx)
            except Exception as e:
                print(f"Warning: There is no {tgt} data for the day of {date}")
                target_idx.append(-1)

        cdef np.float64_t ic
        a = time.time()
        fut_ret_all = []
        for idx in target_idx:
            if idx != -1:
                ic = np.ma.corrcoef(
                    np.ma.masked_invalid(sigs),
                    np.ma.masked_invalid(fut_ret[:, idx])
                )[0][1]
                fut_ret_all.append(ic)
            else:
                fut_ret_all.append(None)

        self.fout.write(
            f"{date}," +
            ",".join(
                f"{x}" if x else "" for x in fut_ret_all) +
            "\n")
        b = time.time()
        print(f"time:{b - a}")


class MdCache(ABC):

    def __init__(
            self, futret_bias: Dict[int, str], ofile: Path, map_file: Path):
        pass

    @abstractmethod
    def on_sod(self, ev: SodEvent, sig_df: pd.DataFrame, today: int):
        pass

    @abstractmethod
    def on_snapshot(self, ev: Union[SnapshotEvent, CsSnapshotEvent]):
        pass

    @abstractmethod
    def on_eod(self, ev: EodEvent):
        pass

    @abstractmethod
    def __del__(self):
        pass


class DailyMdCache(MdCache):

    def __init__(
            self, futret_bias: Dict[int, str], ofile: Path, map_file: Path):
        self.cache = TsSnapshotCache(
            futret_bias, ofile) if None is map_file else CsSnapshotCache(
            futret_bias, ofile, map_file)

    def on_sod(self, ev: SodEvent, sig_df: pd.DataFrame, today: int):
        self.cache.on_sod(ev, sig_df, today)

    def on_snapshot(self, ev: Union[SnapshotEvent, CsSnapshotEvent]):
        self.cache.push(ev)

    def on_eod(self, ev: EodEvent):
        date: int = int(ev.date)
        print(f"{date},calculating ic")
        self.cache.on_eod(date)

    def __del__(self):
        pass


class ContinuousMDCache(MdCache):

    def __init__(
            self, futret_bias: Dict[int, str], ofile: Path, map_file: Path):
        self.fout = open(ofile, "w", buffering=1)
        self.name2sessions: Dict[string, List] = {}
        self.futret_bias = futret_bias
        self.ins_cache: Dict[int, InsDataCache] = {}

        self.fout.write(",".join(self.futret_bias.values()) + "\n")
        self.matched_sig_ret: Dict[int, Dict] = {}
        for fb in self.futret_bias:
            self.matched_sig_ret[fb] = {}

    def on_sod(self, ev: SodEvent, sig_df: pd.DataFrame, today: int):
        date: int = int(ev.date)
        if today != date:
            self.ins_cache.clear()
            self.sig_df = sig_df
            self.name2sessions.clear()

        for i in range(ev.ins_nr):
            ms_ptr: int = ctypes.cast(ev.ms[i], ctypes.c_void_p).value
            ms: MdStatic = ev.ms[i].contents
            print(f"\t{i+1},{ms_ptr},{ms.instrument}")
            tk_exch = ms.ticker.decode(
                "utf8") + "." + ms.exchange.decode("utf8")
            self.ins_cache[ms_ptr] = InsDataCache(tk_exch)
            sessions = []
            for j in range(ms.session_nr):
                sessions.append([ms.session[j].begin, ms.session[j].end])
            self.name2sessions[tk_exch] = sessions

    def on_snapshot(self, ev: SnapshotEvent):
        ms_ptr: int = ctypes.cast(ev.ms, ctypes.c_void_p).value
        # ms: MdStatic = ev.ms.contents
        ss: MdSnapshot = ev.snapshot.contents
        self.ins_cache[ms_ptr].push(ss)

    def check_futret_bias(self):
        for futret_bias, str_futret_bias in self.futret_bias.items():
            for tk_exch, session_list in self.name2sessions.items():
                for session in session_list:
                    if futret_bias > session[1] - session[0]:
                        raise Exception(
                            f"futret_bias {str_futret_bias} exceed trading period length of {tk_exch}")

    def on_eod(self, ev: EodEvent):
        date: int = int(ev.date)
        self.check_futret_bias()
        print(f"{date}, shift time and match")

        name2shift_info: Dict[string, List] = {}
        for tk_exch, sessions in self.name2sessions.items():
            all_session = make_localtime_session(date, sessions)
            name2shift_info[tk_exch] = find_all_shift_info(
                self.sig_df["localtime"], all_session)

        fut_ret_all = []
        for futret_bias, str_futret_bias in self.futret_bias.items():
            for cache in self.ins_cache.values():
                sig_df = self.sig_df[["exchtime",
                                      "localtime", cache.tk_exch]].copy()
                ret_df = cache.cache2df()
                print(f"start match sig & md for {cache.tk_exch}")
                T_match_start = time.perf_counter()
                sig_df: pd.DataFrame = calculate_fut_ret(
                    ret_df, sig_df, futret_bias, name2shift_info[cache.tk_exch])
                T_match_end = time.perf_counter()
                T_match_cost = (T_match_end - T_match_start) * 1000
                print(
                    f"end match sig & md for {cache.tk_exch}, total cost {T_match_cost}ms.")

                if cache.tk_exch in self.matched_sig_ret[futret_bias]:
                    self.matched_sig_ret[futret_bias][cache.tk_exch]['sig'].append(
                        sig_df[cache.tk_exch])
                    self.matched_sig_ret[futret_bias][cache.tk_exch]["fut_ret"].append(
                        sig_df["fut_ret"])
                else:
                    self.matched_sig_ret[futret_bias][cache.tk_exch] = {}
                    self.matched_sig_ret[futret_bias][cache.tk_exch]['sig'] = [
                        sig_df[cache.tk_exch]]
                    self.matched_sig_ret[futret_bias][cache.tk_exch]["fut_ret"] = [
                        sig_df["fut_ret"]]

    def __del__(self):
        print(f"calculating ic")
        fut_ret_all = []
        for futret_bias, str_futret_bias in self.futret_bias.items():
            print(f"start ic calculating for futret_bias {str_futret_bias}")
            T_fb_start = time.perf_counter()
            sigs = []
            fut_ret = []
            for tk_exch in self.matched_sig_ret[futret_bias]:
                sigs.append(
                    pd.concat(
                        self.matched_sig_ret[futret_bias][tk_exch]['sig']))
                fut_ret.append(
                    pd.concat(
                        self.matched_sig_ret[futret_bias][tk_exch]["fut_ret"]))

            print(f"start calculating ic")
            T_calculate_start = time.perf_counter()
            sig_1d = np.ma.masked_invalid(np.ravel(sigs))
            fut_ret_1d = np.ma.masked_invalid(np.ravel(fut_ret))
            corr = np.ma.corrcoef(sig_1d, fut_ret_1d)
            T_calculate_end = time.perf_counter()
            T_calculate_cost = (T_calculate_end - T_calculate_start) * 1000
            print(f"end calculating ic, total cost {T_calculate_cost}ms.")

            fut_ret_all.append(corr[0][1])
            T_fb_end = time.perf_counter()
            T_fb_cost = (T_fb_end - T_fb_start) * 1000
            print(
                f"end ic calculating for futret_bias {str_futret_bias}, total cost {T_fb_cost}ms.")
        self.fout.write(",".join(f"{x}" for x in fut_ret_all) + "\n")


class ICCalculator(SignalBase):

    def __init__(self):
        super().__init__()
        self.signame: str = ""
        self.sigdir: Path = Path("")
        self.sig_file_type: SigFileType = SigFileType.csv

        self.today: int = 0
        self.sig_df: pd.DataFrame = None
        self.futret_bias: Dict[int, str] = {}
        self.cache: MdCache = None
        self.map_file: Path = None

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
        odir: Path = Path(cfg["output_dir"])
        odir.mkdir(parents=True, exist_ok=True)
        ofile: Path = odir / f"{self.signame}.csv"
        mode: str = str(cfg["mode"])
        if "stock_map" in cfg:
            self.map_file = Path(cfg["stock_map"])
            if len(self.futret_bias) != 1:
                raise RuntimeError(
                    "fut2stock mode only support one futret_bias")
        self.cache = DailyMdCache(
            self.futret_bias,
            ofile,
            self.map_file) if mode == "daily" else ContinuousMDCache(
            self.futret_bias,
            ofile,
            self.map_file)

    def on_sod(self, ev: SodEvent):
        date: int = int(ev.date)
        print(f"on_sod:{date},ins_nr:{ev.ins_nr}")
        today = self.today
        if self.today != date:
            self.today = date
            self.load_signal()

        self.cache.on_sod(ev, self.sig_df, today)

    def on_eod(self, ev: EodEvent):
        self.cache.on_eod(ev)

    def on_snapshot(self, ev: SnapshotEvent):
        self.cache.on_snapshot(ev)

    def on_cs_snapshot(self, ev: CsSnapshotEvent):
        self.cache.on_snapshot(ev)


def pysig_create():
    return ICCalculator()
