import numpy as np
from pathlib import Path
import time
import yaml

from cfi.wolverine.signal import *
from cfi.wolverine.event import *


class MySig(SignalBase):

    def __init__(self):
        super().__init__()
        self.ftnames = []
        self.features = []

    def initialize(self, cfg_str: str):
        print(f"loading config")
        cfg = yaml.safe_load(cfg_str)
        self.ftnames = cfg["need_features"]

    def on_sod(self, ev: SodEvent):
        print(f"on_sod:{ev.date},ins_nr:{ev.ins_nr}")
        if not self.features:
            for _ft in self.ftnames:
                self.features.append(self._call_api("get_feature", _ft))

    def on_eod(self, ev: EodEvent):
        self.features.clear()

    def on_cs_snapshot(self, ev: CsSnapshotEvent):
        factors: np.ndarray = self.get_factors()
        print(
            f"on_cs_snapshot,{ev.exchtime},{ev.localtime},shape:{factors.shape}\n{factors}"
        )
        for idx, (ftname, ftarr) in enumerate(zip(self.ftnames, self.features)):
            print(f"{idx},{ftname},arr:{ftarr}")
        self.update_signal(ev.exchtime, ev.localtime, factors.sum(axis=0))


def pysig_create():
    return MySig()
