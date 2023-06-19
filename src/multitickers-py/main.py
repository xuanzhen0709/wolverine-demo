import numpy as np
import yaml

from cfi.wolverine.signal import *


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

    def on_sod(self, date: int, ev: SodEvent):
        self.cnt = 0
        print(f"on_sod:{date},ins_nr:{ev.ins_nr}")
        for i in range(ev.ins_nr):
            ms: MdStatic = ev.ms[i].contents
            print(f"\t{i+1},{ms.instrument}")

    def on_eod(self, date: int):
        print(f"on_eod:{date},total tick cnt:{self.cnt}")

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
