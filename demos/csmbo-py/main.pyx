import numpy as np
import datetime
import time
import yaml

from cfi.wolverine.signal import *

# use cython to speed up c-native types
import cython
# libc.stdint provide c-native types c_uint32 etc
from libc.stdint cimport *

# speed up numpy arrays
cimport numpy as np
np.import_array()


class MySig(SignalBase):

    def __init__(self):
        super().__init__()
        self.cnt: int = 0
        self.mss = []
        self.last_price = []
        self.exchtime = []
        self.localtime = []
        self.start_ts: float = 0

    def initialize(self, cfg_str: str):
        print(f"loading config")
        cfg = yaml.safe_load(cfg_str)

    def on_sod(self, ev: SodEvent):
        self.start_ts = time.time()
        self.cnt = 0
        self.mss.clear()

        targets = []
        print(f"on_sod:{ev.date},ins_nr:{ev.ins_nr}")
        for i in range(ev.ins_nr):
            ms: MdStatic = ev.get_ms(i)
            self.mss.append(ms)

            targets.append(
                ms.instrument.decode("utf8") + "." +
                ms.exchange.decode("utf8"))
            # print(f"\t{i+1},{ms.instrument}")
        self.last_price.clear()
        self.exchtime.clear()
        self.localtime.clear()

    def on_eod(self, ev: EodEvent):
        date: int = int(ev.date)
        now: float = time.time()
        sod_eod_ts: float = now - self.start_ts
        print(
            f"on_eod:{date},total time:{sod_eod_ts:.2f}s,total tick cnt:{self.cnt}"
        )
        # concat cached data
        exchtime: np.ndarray = np.array(self.exchtime, dtype=np.int64)
        localtime: np.ndarray = np.array(self.localtime, dtype=np.uint64)
        last_price: np.ndarray = np.array(self.last_price, dtype=np.float64)
        now_2: float = time.time()
        concat_ts: float = now_2 - now
        print(f"concat time:{concat_ts:.2}s")

        # just for demonstration purposes, we try to update signals using dummy values
        ins_nr: int = len(self.mss)
        for idx in range(len(exchtime)):
            self.update_signal(int(exchtime[idx]), int(localtime[idx]),
                               np.full((ins_nr, ), idx, dtype=np.float64))

    def on_cs_snapshot(self, ev: CsSnapshotEvent):
        self.cnt += 1
        # print(f"on_cs_snapshot,{ev.exchtime}")
        self.exchtime.append(ev.exchtime)
        self.localtime.append(ev.localtime)

        # ev.data is a dictionary mapping from int -> np.ndarray
        last_price_data = ev.get_arr(CsSnapshotEvent.FldType.LAST_PRICE)
        # NOTE: we must explicitly create a copy of the data if we cache it in any way
        self.last_price.append(np.ndarray.copy(last_price_data))

    def on_cs_mbo(self, ev: CsMboEvent):
        exchtime: int = ev.exchtime
        localtime: int = ev.localtime

        cdef int ins_nr = ev.ins_nr
        dt = datetime.datetime.fromtimestamp(localtime / 1e9)
        print(f"on_cs_mbo:{exchtime},{localtime}/{dt},{ins_nr}")
        trades: CsMboEvent.Trade = ev.get_trades()
        cdef uint32_t* cnts = <uint32_t*><intptr_t>(ctypes.addressof(trades.cnt.contents))
        cdef int ii = 0
        cdef int ti = 0
        cdef int ins_cnt = 0
        cdef double* price_arr
        cdef uint32_t* qty_arr
        cdef long long turnover
        for ii in range(ins_nr):
            ins_cnt = cnts[ii]
            turnover = 0
            if ins_cnt == 0:
                continue
            bidid = <uint64_t*><intptr_t>(ctypes.addressof(trades.bidid[ii].contents))
            askid = <uint64_t*><intptr_t>(ctypes.addressof(trades.askid[ii].contents))
            print(f"ins:{ii},cnt:{ins_cnt}")
            for ti in range(ins_cnt):
                #print(f"trade,{ti}/{ins_cnt},{bidid[ti]},{askid[ti]}")
                pass



def pysig_create():
    return MySig()
