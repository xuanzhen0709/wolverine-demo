import numpy as np
import yaml

from cfi.wolverine.signal import *


class MySig(SignalBase):

    def __init__(self):
        super().__init__()
        self.cnt: int = 0
        self.sigval: np.ndarray = np.full((1, ), np.nan, dtype=np.float64)

    def initialize(self, cfg_str: str):
        print(f"loading config")
        cfg = yaml.safe_load(cfg_str)

    def on_sod(self, ev: SodEvent):
        self.cnt = 0
        print(f"on_sod:{ev.date},ins_nr:{ev.ins_nr}")
        for i in range(ev.ins_nr):
            ms: MdStatic = ev.ms[i].contents
            print(f"\t{i+1},{ms.instrument}")

    def on_eod(self, ev: EodEvent):
        print(f"on_eod:{ev.date},total tick cnt:{self.cnt}")

    def on_snapshot(self, ev: SnapshotEvent):
        self.cnt += 1
        ms: MdStatic = ev.ms.contents
        ss: MdSnapshot = ev.snapshot.contents
        # print(
        #     f"on_snapshot:{ss.md_type},{ms.instrument},{ss.exchtime},{ss.last_price},{ss.levels[0]}"
        # )
        self.sigval[0] = self.cnt
        self.update_signal(ss.exchtime, ss.localtime, self.sigval)

def pysig_create():
    return MySig()
