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
cimport numpy as cnp
cnp.import_array()
np.set_printoptions(suppress=True)


class SigFileType(IntEnum):
    csv = 0
    npy = 1


class ExecPriceType(IntEnum):
    mid = 0
    close = 1
    vwap = 2


def hhmmssf_to_exchtime(val: str) -> int:
    hh: int = int(val[:2])
    mm: int = int(val[3:5])
    ss: int = int(val[6:8])
    ff: int = int(val[9:])
    time: int = hh * 3600 * int(1e9) + mm * 60 * int(1e9) + ss * int(1e9) + ff
    if hh >= 18:
        time -= 24 * 3600 * int(1e9)
    return time


cdef void match_A_with_B_cs(
        const int A_nr,
        const cnp.uint64_t[:] A_localtime,
        cnp.float64_t[:, :] ans,
        const int B_nr,
        const cnp.uint64_t[:] B_localtime,
        const cnp.float64_t[:, :] matched_data):
    cdef int A_idx = 0
    cdef int B_idx = 0

    while A_idx < A_nr:
        while B_idx < B_nr and B_localtime[B_idx] <= A_localtime[A_idx]:
            B_idx += 1
        if B_idx:
            ans[A_idx, :] = matched_data[B_idx - 1, :]

        A_idx += 1

cdef void calculate_vwap_cs(
        const int tgt_nr,
        const int sig_nr,
        const cnp.uint64_t[:] sig_localtime,
        const cnp.uint64_t[:] end_localtime,
        const int md_nr,
        const cnp.uint64_t[:] md_localtime,
        const cnp.float64_t[:, :] md_turnover,
        const cnp.float64_t[:, :] md_volume,
        const cnp.float64_t[:, :] sig_close,
        cnp.float64_t[:, :] vwap):

    cdef int sig_idx = 0
    cdef int md_idx = 0
    cdef int start_idx = 0

    while sig_idx < sig_nr:
        turnover = np.zeros(tgt_nr, dtype=np.float64)
        volume = np.zeros(tgt_nr, dtype=np.float64)

        while md_idx < md_nr and md_localtime[md_idx] < sig_localtime[sig_idx]:
            md_idx += 1
        start_idx = md_idx
        while start_idx < md_nr and md_localtime[start_idx] < end_localtime[sig_idx]:
            turnover += md_turnover[start_idx, :]
            volume += md_volume[start_idx, :]
            start_idx += 1

        for i in range(tgt_nr):
            vwap[sig_idx, i] = sig_close[sig_idx,
                                         i] if 0 == volume[i] else turnover[i] / volume[i]

        sig_idx += 1


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


def find_all_shift_info(sig_nr: int,
                        sig_localtime: cnp.uint64_t[:],
                        sessions: List) -> List[Dict[string, int]]:
    left: int = 0
    right: int = sig_nr - 1
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


def shift_fut_localtime(
        fut_localtime_df: Union[pd.DataFrame, cnp.uint64_t[:]], shift_info: List):
    cdef cnp.uint64_t[:] fut_localtime = fut_localtime_df.values if isinstance(fut_localtime_df, pd.DataFrame) else fut_localtime_df
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


def make_localtime_session(date: int, session_list: List):
    daily_session: List = []
    str_time: str = str(date) + "00:00:00"
    time_stamp: float = time.mktime(time.strptime(str_time, "%Y%m%d%H:%M:%S"))
    today_ts: float = time_stamp * int(1e9)

    pre_business_day: int = Calendar.get_instance().next_business_day(date, -1)
    pre_day: int = next_day(pre_business_day)
    pre_str_time: str = str(pre_day) + "00:00:00"
    pre_time_stamp: float = time.mktime(
        time.strptime(pre_str_time, "%Y%m%d%H:%M:%S"))
    pre_ts: float = pre_time_stamp * int(1e9)

    for s in session_list:
        if s[0] < 0:
            daily_session.append([np.uint64(i + pre_ts) for i in s])
        else:
            daily_session.append([np.uint64(i + today_ts) for i in s])

    return daily_session


class CsSnapshotCache():

    def __init__(
            self, exec_price_info: List[Dict], exec_price_type: set(), odir: Path, signame: str, use_system_uv: bool):
        self.exchtime = []
        self.localtime = []
        self.last_price = []
        self.exec_price_type = exec_price_type
        self.exec_price_info = exec_price_info
        if ExecPriceType.mid in self.exec_price_type:
            self.ap = []
            self.bp = []
        if ExecPriceType.vwap in self.exec_price_type:
            self.turnover = []
            self.volume = []

        self.sig_df: pd.DataFrame = None
        self.session: set(tuple) = set()
        self.odir = odir / f"{signame}"
        self.targets: List[str] = []
        self.use_system_uv = use_system_uv

        self.sig_close: cnp.ndarray = None

    def push(self, ev: CsSnapshotEvent):
        self.exchtime.append(ev.exchtime)
        self.localtime.append(ev.localtime)
        self.last_price.append(np.ndarray.copy(
            ev.data[CsSnapshotEvent.FldType.LAST_PRICE.value]))

        if ExecPriceType.mid in self.exec_price_type:
            self.ap.append(np.ndarray.copy(
                ev.data[CsSnapshotEvent.FldType.AP.value][0]))
            self.bp.append(np.ndarray.copy(
                ev.data[CsSnapshotEvent.FldType.BP.value][0]))

        if ExecPriceType.vwap in self.exec_price_type:
            self.turnover.append(np.ndarray.copy(
                ev.data[CsSnapshotEvent.FldType.TURNOVER.value]))
            self.volume.append(np.ndarray.copy(
                ev.data[CsSnapshotEvent.FldType.VOLUME.value]))

    def clear(self):
        self.exchtime.clear()
        self.localtime.clear()
        self.last_price.clear()
        if ExecPriceType.mid in self.exec_price_type:
            self.ap.clear()
            self.bp.clear()
        if ExecPriceType.vwap in self.exec_price_type:
            self.turnover.clear()
            self.volume.clear()
        self.session.clear()

    def on_sod(self, ev: SodEvent, sig_df: pd.DataFrame, today: int):
        date = int(ev.date)
        if today != date:
            self.clear()
            self.sig_df = sig_df

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

    def get_close_and_exec_price(self, date: int) -> List[Dict]:

        if len(self.session) != 1:
            raise Exception(
                f"Inconsistent trading times in cross-section data")
        exch_session = list(list(i) for i in list(self.session)[0])

        for s in exch_session:
            for exec_price in self.exec_price_info:
                if exec_price["bias"] > s[1] - s[0]:
                    raise Exception(
                        f"""bias { exec_price["str"] } exceed trading period length""")

        tgt_nr = len(self.targets)

        cdef cnp.uint64_t[:] sig_localtime = self.sig_df["localtime"].values
        sig_nr = sig_localtime.shape[0]
        sig_shape = (sig_nr, tgt_nr)
        local_session = make_localtime_session(date, exch_session)
        shift_info = find_all_shift_info(
            sig_nr, sig_localtime, local_session)

        cdef cnp.ndarray[cnp.uint64_t, ndim = 1] md_localtime = np.array(self.localtime, dtype=np.uint64)
        cdef cnp.ndarray[cnp.float64_t, ndim = 2] md_last_price = np.array(self.last_price, dtype=np.float64)
        cdef cnp.ndarray[cnp.float64_t, ndim = 2] md_turnover, md_volume, md_mid

        if ExecPriceType.vwap in self.exec_price_type:
            md_turnover = np.array(
                self.turnover, dtype=np.float64)
            md_volume = np.array(self.volume, dtype=np.float64)
        if ExecPriceType.mid in self.exec_price_type:
            md_mid = (np.array(self.ap, dtype=np.float64) +
                      np.array(self.bp, dtype=np.float64)) / 2

        # close_i
        cdef cnp.ndarray[cnp.float64_t, ndim = 2] sig_close = np.full(sig_shape, fill_value=np.nan, dtype=np.float64)
        match_A_with_B_cs(
            sig_nr,
            sig_localtime,
            sig_close,
            md_localtime.shape[0],
            md_localtime,
            md_last_price)

        self.sig_close = sig_close.copy()
        # exec_price
        cdef cnp.uint64_t[:] fut_localtime
        cdef cnp.ndarray[cnp.float64_t, ndim= 2] sig_mid, fut_close, vwap
        for i, exec_price in enumerate(self.exec_price_info):
            if exec_price["type"] == ExecPriceType.mid:
                sig_mid = np.full(
                    sig_shape,
                    fill_value=np.nan,
                    dtype=np.float64)
                match_A_with_B_cs(
                    sig_nr,
                    sig_localtime,
                    sig_mid,
                    md_localtime.shape[0],
                    md_localtime,
                    md_mid)
                self.exec_price_info[i]["price"] = sig_mid.copy()
            elif exec_price["type"] == ExecPriceType.close:
                if exec_price["str"] == "close":
                    self.exec_price_info[i]["price"] = sig_close
                else:
                    fut_localtime = self.sig_df["localtime"].values + \
                        exec_price["bias"]
                    shift_fut_localtime(fut_localtime, shift_info)

                    fut_close = np.full(
                        sig_shape, fill_value=np.nan, dtype=np.float64)
                    match_A_with_B_cs(
                        sig_nr,
                        fut_localtime,
                        fut_close,
                        md_localtime.shape[0],
                        md_localtime,
                        md_last_price)
                    self.exec_price_info[i]["price"] = fut_close.copy()
            elif exec_price["type"] == ExecPriceType.vwap:
                fut_localtime = self.sig_df["localtime"].values + \
                    exec_price["bias"]
                shift_fut_localtime(fut_localtime, shift_info)
                vwap = np.full(sig_shape, fill_value=np.nan, dtype=np.float64)
                calculate_vwap_cs(
                    tgt_nr,
                    sig_nr,
                    sig_localtime,
                    fut_localtime,
                    md_localtime.shape[0],
                    md_localtime,
                    md_turnover,
                    md_volume,
                    sig_close,
                    vwap)
                self.exec_price_info[i]["price"] = vwap.copy()
            else:
                raise RuntimeError(f"""unknown unit {exec_price["str"]}""")

    @abstractmethod
    def calculate_PnL(self, target_idx_list: List):
        pass

    def on_eod(self, date: int):
        T_pnl_start = time.perf_counter()
        self.get_close_and_exec_price(date)
        self.calculate_PnL()
        T_pnl_end = time.perf_counter()
        T_pnl_cost = (T_pnl_end - T_pnl_start) * 1000
        print(f"end pnl calculating, total cost {T_pnl_cost}ms.")
        _odir = self.odir / f"{date}"
        _odir.mkdir(parents=True, exist_ok=True)
        sig_exchtime = self.sig_df["exchtime"].values
        sig_localtime = self.sig_df["localtime"].values

        for exec_price in self.exec_price_info:
            _ofile: Path = _odir / f"""{exec_price["str"]}.csv"""
            _fout = open(_ofile, "w", buffering=1)
            _fout.write(f"localtime,exchtime," +
                        ",".join(self.targets) + "\n")
            for i, ltime in enumerate(sig_localtime):
                _fout.write(f"{ltime}, {sig_exchtime[i]}," +
                            ",".join(exec_price["PnL"][i].astype(str)) + "\n")


class DailyCsSnapshotCache(CsSnapshotCache):

    def calculate_PnL(self):
        cdef cnp.ndarray[cnp.float64_t, ndim= 2] sigs = self.sig_df[self.targets].values
        # PnL_i = trade_PnL + hold_PnL - fee_PnL
        # trade_PnL = (sig_i - sig_(i-1) ) * (close_i / exec_price - 1)
        # hold_PnL = sig_(i-1) * (close_i / close_(i-1) -1)
        # close_i-1
        cdef cnp.ndarray[cnp.float64_t, ndim = 2] pre_close = np.roll(self.sig_close, 1, axis=0)
        pre_close[0] = np.nan
        # sig_i-1
        cdef cnp.ndarray[cnp.float64_t, ndim = 2] pre_sigs =  np.roll(sigs, 1, axis=0)
        pre_sigs[0] = 0
        cdef cnp.ndarray[cnp.float64_t, ndim = 2] hld_PnL = np.multiply(pre_sigs, (self.sig_close / pre_close - 1))
        # sig_i - sig_i-1
        cdef cnp.ndarray[cnp.float64_t, ndim = 2] sigs_diff = sigs - pre_sigs
        sigs_diff[0] = 0

        for i, exec_price in enumerate(self.exec_price_info):
            trade_PnL = np.multiply(
                sigs_diff, (self.sig_close / exec_price["price"] - 1))
            PnL = np.nan_to_num(hld_PnL + trade_PnL)
            self.exec_price_info[i]["PnL"] = PnL.copy()


class ContinuousCsSnapshotCache(CsSnapshotCache):

    def __init__(
            self, exec_price_info: List[Dict], exec_price_type: set(), odir: Path, signame: str, map_file: Path):
        super().__init__()
        self.adj_factor: Dict = {}
        self.pre_day_info: Dict = {}

    def on_sod(self, ev: SodEvent, sig_df: pd.DataFrame, today: int):
        super().on_sod()
        for i in range(ev.ins_nr):
            tk_exch = ms.ticker.decode(
                "utf8") + "." + ms.exchange.decode("utf8")
            self.adj_factor[tk_exch] = ms.adj_factor

    def calculate_PnL(self):
        # PnL_i = trade_PnL + hold_PnL - fee_PnL
        # trade_PnL = (sig_i - sig_(i-1) ) * (close_i / exec_price - 1)
        # hold_PnL = sig_(i-1) * (close_i / close_(i-1) -1)
        cdef cnp.ndarray[cnp.float64_t, ndim= 2] sigs = self.sig_df[self.targets].values
        cdef cnp.ndarray[cnp.float64_t, ndim = 2] pre_close = np.roll(self.sig_close, 1, axis=0)
        cdef cnp.ndarray[cnp.float64_t, ndim = 2] pre_sigs =  np.roll(sigs, 1, axis=0)
        pre_sigs[0] = 0
        for _i, tk_exch in enumerate(self.targets):
            if tk_exch in self.pre_day_info:
                first_valid_idx = self.sig_df[tk_exch].first_valid_index()
                pre_close[_i][first_valid_idx] = self.pre_day_info[tk_exch]['close'] * \
                    self.adj_factor[tk_exch] / \
                    self.pre_day_info[tk_exch]['adj_factor']
                pre_sigs[_i][first_valid_idx] = self.pre_day_info[tk_exch]['sig']

        cdef cnp.ndarray[cnp.float64_t, ndim = 2] hld_PnL = np.multiply(pre_sigs, (self.sig_close / pre_close - 1))
        cdef cnp.ndarray[cnp.float64_t, ndim = 2] sigs_diff = sigs - pre_sigs
        for i, exec_price in enumerate(self.exec_price_info):
            trade_PnL = np.multiply(
                sigs_diff, (self.sig_close / exec_price["price"] - 1))
            PnL = np.nan_to_num(hld_PnL + trade_PnL)
            self.exec_price_info[i]["PnL"] = PnL.copy()

    def on_eod(self, date: int):
        super().on_eod()
        self.pre_day_info.clear()
        for _i, tk_exch in enumerate(self.targets):
            last_valid_idx = self.sig_close[tk_exch].last_valid_index()
            self.pre_day_info[tk_exch] = {
                'close': self.sig_close[tk_exch][last_valid_idx],
                'adj_factor': self.adj_factor[tk_exch],
                'sig': self.sig_df[tk_exch][last_valid_idx],
            }


class PnLCalculator(SignalBase):

    def __init__(self):
        super().__init__()
        self.signame: str = ""
        self.sigdir: Path = Path("")
        self.sig_file_type: SigFileType = SigFileType.csv
        self.today: int = 0
        self.sig_df: pd.DataFrame = None
        self.cache: CsSnapshotCache = None

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

        exec_price_info: List[Dict] = []
        exec_price_type = set()

        for price_str in cfg["exec_price"]:
            price_list = price_str.split("_")
            type_str = price_list[0]
            bias_str = None
            if len(price_list) > 2:
                raise RuntimeError(f"unknown exercise price {price_str}")
            if len(price_list) == 2:
                bias_str = price_list[1]
            try:
                price_type = ExecPriceType[type_str]
            except BaseException:
                raise RuntimeError(
                    f"unknown exercise price {type_str}, only support:{[i.name for i in ExecPriceType]}")
            if None is bias_str:
                price_bias = 0
            elif bias_str.endswith("ns"):
                price_bias = int(bias_str[:-2])
            elif bias_str.endswith("s"):
                price_bias = int(bias_str[:-1]) * int(1e9)
            elif bias_str.endswith("m"):
                price_bias = int(bias_str[:-1]) * int(1e9) * 60
            elif bias_str.endswith("h"):
                price_bias = int(bias_str[:-1]) * int(1e9) * 3600
            else:
                raise RuntimeError(f"unknown unit {bias_str}")
            exec_price_info.append({
                "str": price_str,
                "type": price_type,
                "bias": price_bias
            })
            exec_price_type.add(price_type)

        odir: Path = Path(cfg["output_dir"])
        odir.mkdir(parents=True, exist_ok=True)
        mode: str = str(cfg["mode"])
        use_system_uv = cfg.get("use_system_uv", False)
        if "daily" == mode:
            self.cache = DailyCsSnapshotCache(
                exec_price_info, exec_price_type, odir, self.signame, use_system_uv)
        elif "continuous" == mode:
            self.cache = ContinuousCsSnapshotCache(
                exec_price_info, exec_price_type, odir, self.signame, use_system_uv)
        else:
            raise RuntimeError(f"unknown mode:{mode}")

    def on_sod(self, ev: SodEvent):
        date: int = int(ev.date)
        today = self.today
        if self.today != date:
            self.today = date
            self.load_signal()

        self.cache.on_sod(ev, self.sig_df, today)

    def on_eod(self, ev: EodEvent):
        date: int = int(ev.date)
        print(f"{date},calculating PnL")
        self.cache.on_eod(date)

    def on_cs_snapshot(self, ev: SnapshotEvent):
        self.cache.push(ev)


def pysig_create():
    return PnLCalculator()
