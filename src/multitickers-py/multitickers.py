import numpy as np
from typing import Dict, Tuple
import yaml

from cfi.wolverine.marketdata import *
from cfi.wolverine.signal import SignalBase


class MySig(SignalBase):

    def __init__(self):
        super().__init__()
        self.cnt: int = 0
        self.sigval: np.ndarray = np.full((1, ), np.nan, dtype=np.float64)

    def initialize(self, path: str):
        if not path:
            return
        print(f"loadding config:{path}")
        with open(path) as fin:
            cfg = yaml.safe_load(fin)

            for _i in cfg["marketdata"]:
                self.subscribe(_i["type"], _i["symbols"])

            self.set_targets(cfg["targets"])

    def on_sod(self, date: int, ev: SodEvent):
        self.cnt = 0
        print(f"on_sod:{date},ins_nr:{ev.ins_nr}")
        for i in range(ev.ins_nr):
            ms: MdStatic = ev.ms[i].contents
            print(f"\t{i+1},{ms.instrument}")

    def on_snapshot(self, ev: SnapshotEvent):
        self.cnt += 1
        ms: MdStatic = ev.ms.contents
        ss: MdSnapshot = ev.snapshot.contents
        print(
            f"on_snapshot:{ms.instrument},{ss.exchtime},{ss.last_price},{ss.levels[0]}"
        )
        self.sigval[0] = self.cnt
        self.update_signal(ss.exchtime, self.sigval)

    def on_bar(self, ev: BarEvent):
        ms: MdStatic = ev.ms.contents
        bar: MdBar = ev.bar.contents
        print(
            f"on_bar:{ms.instrument},{ms.exchange},{bar.exchtime},{bar.localtime},{bar.open}/{bar.high}/{bar.low}/{bar.close},{bar.volume},{bar.turnover}"
        )
        raise NotImplementedError

    def on_cs_snapshot(self, ev: CsSnapshotEvent):
        print("on_cs_snapshot")
        raise NotImplementedError

    def on_eod(self, date: int):
        print(f"on_eod:{date},total tick cnt:{self.cnt}")


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
