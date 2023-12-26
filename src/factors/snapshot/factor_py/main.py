import numpy as np
import yaml

from cfi.wolverine.signal import *


class MySig(SignalBase):
    def __init__(self):
        super().__init__()
        self.cnt: np.float64 = 0
        self.seed: np.float64 = 0
        self.sigval: np.ndarray = np.full((1,), fill_value=np.nan, dtype=np.float64)

    def initialize(self, cfg_str: str):
        print(f"loading config")
        cfg = yaml.safe_load(cfg_str)
        self.seed = np.float64(cfg.get("seed", 0))

    def on_sod(self, ev: SodEvent):
        self.cnt = self.seed
        print(f"on_sod:{ev.date},ins_nr:{ev.ins_nr}")

    def on_eod(self, ev: EodEvent):
        print(f"total tick cnt:{self.cnt}")
        pass

    def on_snapshot(self, ev: SnapshotEvent):
        self.cnt += 1
        ss: MdSnapshot = ev.snapshot.contents
        self.sigval[0] = self.cnt
        self.update_signal(ss.exchtime, ss.localtime, self.sigval)


def pysig_create():
    return MySig()
