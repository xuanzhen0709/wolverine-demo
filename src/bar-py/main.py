import numpy as np
import yaml

from cfi.wolverine.signal import *


class MySig(SignalBase):

    def __init__(self):
        super().__init__()
        self.cnt: int = 0
        self.sigval: np.ndarray = np.full((1, ), np.nan, dtype=np.float64)
        print(self.sigval)

    def initialize(self, cfg_str: str):
        print(f"loading config")
        cfg = yaml.safe_load(cfg_str)

    def on_sod(self, date: int, ev: SodEvent):
        self.cnt = 0
        print(f"on_sod:{date},ins_nr:{ev.ins_nr}")
        for i in range(ev.ins_nr):
            ms: MdStatic = ev.ms[i].contents
            print(f"\t{i+1},{ms.instrument}")

    def on_eod(self, date: int):
        print(f"on_eod:{date},total tick cnt:{self.cnt}")

    def on_bar(self, ev: BarEvent):
        self.cnt += 1
        ms: MdStatic = ev.ms.contents
        bar: MdBar = ev.bar.contents
        print(
            f"on_bar:{ms.instrument},{ms.exchange},{bar.exchtime},{bar.localtime},{bar.open}/{bar.high}/{bar.low}/{bar.close},{bar.volume},{bar.turnover}"
        )
        self.sigval[0] = self.cnt
        self.update_signal(bar.exchtime, bar.localtime, self.sigval)


def pysig_create():
    return MySig()
