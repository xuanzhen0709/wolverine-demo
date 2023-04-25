import numpy as np
import yaml

from cfi.wolverine.marketdata import *
from cfi.wolverine.signal import SignalBase


class MySig(SignalBase):

    def __init__(self):
        super().__init__()
        self.cnt: int = 0
        self.ins_nr: int = 0
        self.mss = []
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
        self.mss.clear()

        print(f"on_sod:{date}")
        print(f"\tins_nr:{ev.ins_nr}")
        mss = ctypes.POINTER(ctypes.POINTER(MdStatic)).from_address(ev.ms)
        for i in range(ev.ins_nr):
            print(mss.contents[i])
            print(mss.contents[i].contents)
            # ms: MdStatic = mss[i].contents
            # self.mss.append(ms)
            # print(f"\t{i+1},{ms.instrument}")

    def on_snapshot(self, ev: SnapshotEvent):
        print("on_snapshot")
        raise NotImplementedError

    def on_bar(self, ev: BarEvent):
        print("on_bar")
        raise NotImplementedError

    def on_cs_snapshot(self, ev: CsSnapshotEvent):
        self.cnt += 1
        print("on_cs_snapshot")
        exchtime = ev.exchtime
        ins_nr = ev.ins_nr
        fld_nr = ev.fld_nr

        for (fld, data) in ev.data:
            print(f"{fld},{data}")

    def on_eod(self, date: int):
        print(f"total tick cnt:{self.cnt}")
        pass


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
