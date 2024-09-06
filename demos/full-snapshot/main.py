import ctypes
import numpy as np
import yaml

from cfi.wolverine.signal import *
from cfi.wolverine.marketdata import *


class MySig(SignalBase):

    def __init__(self):
        super().__init__()
        self.cnt: int = 0
        self.sigval: np.ndarray = np.full((1,), np.nan, dtype=np.float64)

    def initialize(self, cfg_str: str):
        print(f"loading config")
        cfg = yaml.safe_load(cfg_str)

    def on_sod(self, ev: SodEvent):
        self.cnt = 0
        print(f"on_sod:{ev.date},ins_nr:{ev.ins_nr}")
        for i in range(ev.ins_nr):
            ms: MdStatic = ev.get_ms(i)
            print(f"\t{i+1},{ms.instrument}")

    def on_eod(self, ev: EodEvent):
        print(f"total tick cnt:{self.cnt}")
        pass

    def on_full_snapshot(self, ev: SnapshotEvent):
        self.cnt += 1
        ms: MdStatic = ev.ms.contents
        ss: MdFullSnapshot = ev.snapshot.contents
        print(
            f"on_full_snapshot:{ms.instrument},{ss.exchtime},{ss.localtime},{ss.last_price},{ss.level_nr}"
        )
        # def get_levels(self) -> MdLevel:
        levels = ss.get_levels()
        for i in range(ss.level_nr):
            lvl = levels[i]
            print(f"\t{i},{lvl.bv}@{lvl.bp},{lvl.av}@{lvl.ap}")
        # self.sigval[0] = np.sum(ap * av + bp * bv)
        # self.update_signal(ss.exchtime, ss.localtime, self.sigval)


def pysig_create():
    return MySig()
