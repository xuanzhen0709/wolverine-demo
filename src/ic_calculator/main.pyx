import numpy as np
from enum import IntEnum
import yaml
from pathlib import Path
import pandas as pd
from typing import Dict, Set
from abc import ABC, abstractmethod
from cfi.wolverine.signal import *

import cython
# libc.stdint provide c-native types c_uint32 etc
from libc.stdint cimport *

# speed up numpy arrays
cimport numpy as np
np.import_array()
np.set_printoptions(suppress=True)
import time

####################################################################################################
import datetime
import numpy as np
from pathlib import Path
from typing import List, Set, Union

class Calendar:
    """
    This class provides trading calendar lookups.

    A typical use case would be:

    Calendar.get_instance().next_business_day(20211202, 1)
    """
    __INSTANCE = None

    def __init__(self):
        path: Path = Path("/mnt/nas-3/CTA/Data/ChinaTradingDates.txt")
        with open(path) as fin:
            lines = [int(x) for x in fin.read().splitlines()]
            self.dates: np.ndarray = np.array(lines, dtype=np.uint32)
            self.date_set: Set[int] = set(lines)

    @staticmethod
    def get_instance() -> "Calendar":
        """
        Return:
            the global Calendar instance
        """
        if Calendar.__INSTANCE is None:
            Calendar.__INSTANCE = Calendar()
        return Calendar.__INSTANCE

    def next_business_day(self, date: Union[str, int], shift: int = 1) -> int:
        """
        find the business day with a an optional shift

        Args:
            date: reference date
            shift: offset to the reference date

        Return:
            date
        """
        date_int = int(date)
        idx: int = int(np.searchsorted(self.dates, date_int, side="left"))
        if self.dates[idx] == date_int:
            return int(self.dates[idx + shift])
        if shift > 0:
            idx -= 1
        return int(self.dates[idx + shift])

    def is_business_day(self, date: Union[str, int]) -> bool:
        """
        tells if the given date is a business day

        Args:
            date: given date

        Return:
            whether the given date is a business day or not
        """
        date_int = int(date)
        return date_int in self.date_set

    def get_business_days(self, start_date: Union[str, int],
                          end_date: Union[str, int]) -> List[int]:
        """
        retrieve a list of business days given a date range

        Args:
            start_date: start of the range (inclusive)
            end_date: end of the range (inclusive)
        
        Return:
            list of dates (integer)
        """
        start: int = int(start_date)
        end: int = int(end_date)
        st_idx: int = np.searchsorted(self.dates, start, side="left")
        rt_idx: int = np.searchsorted(self.dates, end, side="right")
        return [int(x) for x in self.dates[st_idx:rt_idx]]
#############################################################################################

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

cdef void match_sig_with_md(
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

def localtime_shift_in_futret_bias(x:np.uint64, futret_bias: int, session: List) -> np.uint64:
    for i in range(len(session)-1):
        if x > session[i][1] - futret_bias and x <= session[i][1] :
            # print("shift localtime:" + str(pd.to_datetime(x, unit='ns'))+"-->"+ str(pd.to_datetime(x + (session[i+1][0] - session[i][1]) , unit='ns')))
            x += (session[i+1][0] - session[i][1]) 
    return x

def session_maker(date_list: List,  session: List):
    # print("session_maker:")
    ans = []
    for date in date_list:
        str_time = str(date) + '00:00:00'
        time_stamp = time.mktime(time.strptime(str_time, '%Y%m%d%H:%M:%S'))
        ts = time_stamp * int(1e9)
        for s in session:
            ans.append([np.uint64(i + ts) for i in s])
    
    # for s in ans:
    #     for i in s:
    #         print(i)
    #         print(pd.to_datetime(i, unit='ns'))
    # print("session_maker end")

    return ans

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

    def cache2ret_df(self):
        return  pd.DataFrame(
                {
                    "exchtime": self.exchtime,
                    "localtime": self.localtime,
                    "ap": self.ap,
                    "bp": self.bp
                })


def calculate_ic_for_target(ret_df: pd.DataFrame, sig_df: pd.DataFrame, futret_bias: int) -> pd.DataFrame:
    ret_df["localtime"] = ret_df["localtime"].astype(np.uint64)
    ret_df["mid_px"] = (ret_df["ap"] + ret_df["bp"]) / 2
    
    sig_df["localtime"] = sig_df["localtime"].astype(np.uint64)
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

class MdCache(ABC):

    def __init__(self, futret_bias: Dict[int, int], fout: _io.TextIOWrapper):
        self.futret_bias: Dict[int, str] = futret_bias
        self.fout = fout
        self.name2sessions: Dict[string, List] = {}

    @abstractmethod
    def on_sod(self, ev: SodEvent, sig_df: pd.DataFrame, today: int, date: int):
        pass

    @abstractmethod
    def on_snapshot(self, ev: SnapshotEvent):
        pass

    @abstractmethod
    def on_eod(self, date: int):
        pass

    @abstractmethod
    def __del__(self):
        pass

class DailyMdCache(MdCache):

    def __init__(self, futret_bias: Dict[int, int], fout:_io.TextIOWrapper):
        super().__init__(futret_bias, fout)
        self.sig_df: pd.DataFrame = None
        self.ins_cache: Dict[int, InsDataCache] = {}
        self.fout.write(f"date," + ",".join(self.futret_bias.values()) + "\n")
     
    def on_sod(self, ev: SodEvent, sig_df: pd.DataFrame, today: int, date: int):
        if today != date:
            self.ins_cache.clear()
            self.sig_df = sig_df

        for i in range(ev.ins_nr):
            ms_ptr: int = ctypes.cast(ev.ms[i], ctypes.c_void_p).value
            ms: MdStatic = ev.ms[i].contents
            print(f"\t{i+1},{ms_ptr},{ms.instrument}")            
            tk_exch = ms.ticker.decode("utf8") + "." + ms.exchange.decode("utf8")            
            self.ins_cache[ms_ptr] = InsDataCache(tk_exch)
            if tk_exch not in self.name2sessions.keys():
                print("collect sessions info")
                sessions = []
                for j in range(ms.session_nr):
                    sessions.append([ms.session[j].begin, ms.session[j].end])
                self.name2sessions[tk_exch] = sessions
        
    def on_snapshot(self, ev: SnapshotEvent):
        ms_ptr: int = ctypes.cast(ev.ms, ctypes.c_void_p).value
        # ms: MdStatic = ev.ms.contents
        ss: MdSnapshot = ev.snapshot.contents
        self.ins_cache[ms_ptr].push(ss)

    def on_eod(self, date: int):
        print(f"{date},calculating ic")
        fut_ret_all = []
        for futret_bias in self.futret_bias:
            sigs = []
            fut_ret = []
            for cache in self.ins_cache.values():
                sessions = session_maker([date], self.name2sessions[cache.tk_exch])
                sig_df = self.sig_df[["exchtime", "localtime", cache.tk_exch]].copy()
                # sig_df.to_csv(f"{date}_{cache.tk_exch}_sig_before.csv", sep=',')
                sig_df['localtime'] = sig_df['localtime'].apply(localtime_shift_in_futret_bias, args = (futret_bias, sessions))
                sig_df["localtime"] = sig_df["localtime"].astype(np.uint64)
                # sig_df.to_csv(f"{date}_{cache.tk_exch}_sig_after.csv", sep=',')
                ret_df = cache.cache2ret_df()
                sig_df: pd.DataFrame = calculate_ic_for_target(ret_df, sig_df, futret_bias)
                # sig_df["time"] = pd.to_datetime(sig_df['localtime'],unit='ns')
                # sig_df.to_csv(f"{date}_{cache.tk_exch}_sig_latest.csv", sep=',')
                sigs.append(sig_df[cache.tk_exch])
                fut_ret.append(sig_df["fut_ret"])
           
            sig_1d = np.ma.masked_invalid(np.ravel(sigs))
            fut_ret_1d = np.ma.masked_invalid(np.ravel(fut_ret))
            corr = np.ma.corrcoef(sig_1d, fut_ret_1d)
            fut_ret_all.append(corr[0][1])
        self.fout.write(f"{date}," + ",".join(f"{x}" for x in fut_ret_all) + "\n")

    def __del__(self):
        pass

class ContinuousMDCache(MdCache):

    def __init__(self, futret_bias: Dict[int, int], fout: _io.TextIOWrapper):
        super().__init__(futret_bias, fout)
        self.ins_cache: Dict[string, InsDataCache] = {}
        self.ins_cache_list: List = []
        self.sig_df_list: List = []
        self.date_list: List = []
        self.cal : Calendar  = Calendar.get_instance()
        self.fout.write(",".join(self.futret_bias.values()) + "\n")

    def on_sod(self, ev: SodEvent, sig_df: pd.DataFrame, today: int, date: int):
        # For a new day, save the previous day's data 
        if today != date:
            # print("today:"+ str(today)+ " date" + str(date))
            if today: 
                self.ins_cache_list.append(self.ins_cache)
                self.ins_cache.clear()
            self.sig_df_list.append(sig_df)
            self.date_list.append(date)

        for i in range(ev.ins_nr):
            ms: MdStatic = ev.ms[i].contents
            print(f"\t{i+1}, {ms.instrument}")
            tk_exch = ms.ticker.decode("utf8") + "." + ms.exchange.decode("utf8")
            self.ins_cache[tk_exch] = InsDataCache(tk_exch)
            if tk_exch not in self.name2sessions.keys():
                print("collect sessions info")
                sessions = []
                for j in range(ms.session_nr):
                    sessions.append([ms.session[j].begin, ms.session[j].end])
                self.name2sessions[tk_exch] = sessions

    def on_snapshot(self, ev: SnapshotEvent):
        ms: MdStatic = ev.ms.contents
        ss: MdSnapshot = ev.snapshot.contents
        self.ins_cache[ms.ticker.decode("utf8") + "." + ms.exchange.decode("utf8")].push(ss)

    def on_eod(self, date: int):
        pass

    def __del__(self):
        self.ins_cache_list.append(self.ins_cache)
        print(f"calculating ic")
        sig_ret_date = []

        Continuous_sig :pd.DataFrame = self.sig_df_list[0]
        Continuous_ret :Dict[string, pd.DataFrame] = {}
        for tk_exch, cache in self.ins_cache_list[0].items():
            Continuous_ret[tk_exch] = cache.cache2ret_df()
        
        date_list = [self.date_list[0]]

        for i in range(len(self.date_list)-1):
            if self.date_list[i+1] == self.cal.next_business_day(self.date_list[i]):
                Continuous_sig = pd.concat([Continuous_sig, self.sig_df_list[i+1]])
                for cache in self.ins_cache_list[i+1].values():
                    Continuous_ret[cache.tk_exch] = pd.concat([Continuous_ret[cache.tk_exch], cache.cache2ret_df()])
                date_list.append(self.date_list[i+1])
            else:
                sig_ret_date.append({
                  "date": date_list,
                  "sig_df": Continuous_sig,
                  "ret_df": Continuous_ret
                })
                Continuous_sig = self.sig_df_list[i+1]
                for cache in self.ins_cache_list[i+1].values():
                    Continuous_ret[cache.tk_exch] = cache.cache2ret_df()
                date_list = [self.date_list[i+1]]

        sig_ret_date.append({
            "date": date_list,
            "sig_df": Continuous_sig,
            "ret_df": Continuous_ret
        })
        # print(sig_ret_date)

        fut_ret_all = []
        for futret_bias in self.futret_bias:
            sigs = []
            fut_ret = []
            for srd in sig_ret_date:
                total_sig_df = srd['sig_df']
                total_ret_df =  srd['ret_df']
                total_date = srd['date']
                for tk_exch in total_ret_df.keys():
                    sessions = session_maker(total_date, self.name2sessions[tk_exch])
                    sig_df = total_sig_df[["exchtime", "localtime", tk_exch]].copy()
                    sig_df['localtime'] = sig_df['localtime'].apply(localtime_shift_in_futret_bias, args = (futret_bias, sessions))
                    sig_df["localtime"] = sig_df["localtime"].astype(np.uint64)
                    sig_df: pd.DataFrame = calculate_ic_for_target(total_ret_df[tk_exch], sig_df, futret_bias)
                    # sig_df.to_csv(f"{cache.tk_exch}_sig.csv", sep=',')
                    sigs.append(sig_df[tk_exch])
                    fut_ret.append(sig_df["fut_ret"])
            sig_1d = np.ma.masked_invalid(np.ravel(sigs))
            fut_ret_1d = np.ma.masked_invalid(np.ravel(fut_ret))
            corr = np.ma.corrcoef(sig_1d, fut_ret_1d)
            fut_ret_all.append(corr[0][1])
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
        mode: str = str(cfg["mode"])
        self.cache = DailyMdCache(self.futret_bias, self.fout) if mode == "daily" else ContinuousMDCache(self.futret_bias, self.fout) 

    def on_sod(self, date: int, ev: SodEvent):
        print(f"on_sod:{date},ins_nr:{ev.ins_nr}")
        today = self.today
        if self.today != date:
            self.today = date
            self.load_signal()

        self.cache.on_sod(ev, self.sig_df, today, date)

    def on_eod(self, date: int):
        self.cache.on_eod(date)

    def on_snapshot(self, ev: SnapshotEvent):
        self.cache.on_snapshot(ev)

def pysig_create():
    return ICCalculator()
