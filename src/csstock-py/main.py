from .mytest import *

import numpy as np
import pandas as pd
import time
import yaml

from cfi.wolverine.marketdata import *
from cfi.wolverine.signal import SignalBase


class MySig(SignalBase):

    def __init__(self):
        super().__init__()
        self.cnt: int = 0
        self.mss = []
        self.last_price = []
        self.exchtime = []
        self.start_ts: float = 0
        haha()

    def initialize(self, path: str):
        if not path:
            return
        print(f"loadding config:{path}")
        with open(path) as fin:
            cfg = yaml.safe_load(fin)

            for _i in cfg["marketdata"]:
                self.subscribe(_i["type"], _i["fields"], _i["symbols"])

    def on_sod(self, date: int, ev: SodEvent):
        self.start_ts = time.time()
        self.cnt = 0
        self.mss.clear()

        targets = []
        print(f"on_sod:{date},ins_nr:{ev.ins_nr}")
        for i in range(ev.ins_nr):
            ms: MdStatic = ev.ms[i].contents
            self.mss.append(ms)

            targets.append(
                ms.instrument.decode("utf8") + "." +
                ms.exchange.decode("utf8"))
            # print(f"\t{i+1},{ms.instrument}")
        self.set_targets(targets)
        self.last_price.clear()
        self.exchtime.clear()

    def on_snapshot(self, ev: SnapshotEvent):
        ms: MdStatic = ev.ms.contents
        ss: MdSnapshot = ev.snapshot.contents
        print(
            f"on_snapshot:{ms.instrument},{ss.exchtime},{ss.last_price},{ss.levels[0]}"
        )
        raise NotImplementedError

    def on_bar(self, ev: BarEvent):
        ms: MdStatic = ev.ms.contents
        bar: MdBar = ev.bar.contents
        print(
            f"on_bar:{ms.instrument},{ms.exchange},{bar.exchtime},{bar.localtime},{bar.open}/{bar.high}/{bar.low}/{bar.close},{bar.volume},{bar.turnover}"
        )
        raise NotImplementedError

    def on_cs_snapshot(self, ev: CsSnapshotEvent):
        self.cnt += 1
        # print("on_cs_snapshot")
        self.exchtime.append(ev.exchtime)

        # ev.data is a dictionary mapping from int -> np.ndarray
        last_price_data = ev.data[MdFld.last_price.value]
        # NOTE: we must explicitly create a copy of the data if we cache it in any way
        self.last_price.append(np.ndarray.copy(last_price_data))

    def on_eod(self, date: int):
        now: float = time.time()
        sod_eod_ts: float = now - self.start_ts
        print(
            f"on_eod:{date},total time:{sod_eod_ts:.2f}s,total tick cnt:{self.cnt}"
        )
        # concat cached data
        exchtime: np.ndarray = np.array(self.exchtime, dtype=np.int64)
        last_price: np.ndarray = np.array(self.last_price, dtype=np.float64)
        now_2: float = time.time()
        concat_ts: float = now_2 - now
        print(f"concat time:{concat_ts:.2}s")

        # just for demonstration purposes, we try to update signals using dummy values
        ins_nr: int = len(self.mss)
        for idx, _time in enumerate(exchtime):
            self.update_signal(_time, np.full((ins_nr, ),
                                              idx,
                                              dtype=np.float64))


def pysig_create():
    return MySig()


def pysig_set_apis(hdl, apis: dict):
    hdl.set_apis(apis)


def pysig_initialize(hdl, path: str):
    hdl.initialize(path)


def pysig_on_sod(hdl, date: int, ev_ptr: int):
    ev: SodEvent = SodEvent.from_address(ev_ptr)
    hdl.on_sod(date, ev)


def pysig_on_snapshot(hdl, ev_ptr: int):
    ev: SnapshotEvent = SnapshotEvent.from_address(ev_ptr)
    return hdl.on_snapshot(ev)


def pysig_on_bar(hdl, ev_ptr: int):
    ev: BarEvent = BarEvent.from_address(ev_ptr)
    return hdl.on_bar(ev)


def pysig_on_cs_snapshot(hdl, ev_ptr: int):
    ev: CsSnapshotEvent = CsSnapshotEvent.from_address(ev_ptr)
    return hdl.on_cs_snapshot(ev)


def pysig_on_eod(hdl, date: int):
    hdl.on_eod(date)
