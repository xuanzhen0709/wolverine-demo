import numpy as np
from pathlib import Path
import time
import yaml

from cfi.wolverine.signal import *
from cfi.wolverine.event import *


class MySig(SignalBase):
    def __init__(self):
        super().__init__()
        self.cnt: int = 0
        self.start_ts: float = 0
        self.sigval: np.ndarray = np.full((1,), fill_value=np.nan)

    def initialize(self, cfg_str: str):
        print(f"loading config")
        cfg = yaml.safe_load(cfg_str)

    def on_sod(self, ev: SodEvent):
        self.start_ts = time.time()
        self.cnt = 0
        print(f"on_sod:{ev.date},ins_nr:{ev.ins_nr}")

    def on_eod(self, ev: EodEvent):
        date: int = int(ev.date)
        now: float = time.time()
        sod_eod_ts: float = now - self.start_ts
        print(f"on_eod:{date},total time:{sod_eod_ts:.2f}s,total tick cnt:{self.cnt}")

    def on_snapshot(self, ev: SnapshotEvent):
        self.cnt += 1
        ss: MdSnapshot = ev.snapshot.contents
        factors: np.ndarray = self.get_factors()
        if self.cnt % 10000 == 0:
            # dump every 10000 ticks
            print(factors, flush=True)
        self.sigval[0] = np.nansum(factors)
        self.update_signal(ss.exchtime, ss.localtime, self.sigval)


def pysig_create():
    return MySig()
