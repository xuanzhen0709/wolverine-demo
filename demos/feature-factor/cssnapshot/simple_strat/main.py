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
        self.mss = []
        self.ins_nr: int = 0
        self.last_price = []
        self.exchtime = []
        self.localtime = []
        self.start_ts: float = 0

    def initialize(self, cfg_str: str):
        print(f"loading config")
        cfg = yaml.safe_load(cfg_str)

    def on_sod(self, ev: SodEvent):
        self.start_ts = time.time()
        self.cnt = 0
        self.mss.clear()

        print(self._apis)
        my_targets = self._call_api("get_targets")
        print(f"my targets:{len(my_targets)}")
        factor_list = self._call_api("get_factor_list")
        for i, fct in enumerate(factor_list):
            tg = self._call_api("get_factor_targets", i)
            print(f"fct target,{i},{fct},{len(tg)}")
        targets = []
        print(f"on_sod:{ev.date},ins_nr:{ev.ins_nr}")
        for i in range(ev.ins_nr):
            ms: MdStatic = ev.ms[i].contents
            self.mss.append(ms)

            targets.append(
                ms.instrument.decode("utf8") + "." + ms.exchange.decode("utf8")
            )
            # print(f"\t{i+1},{ms.instrument}")
        self.ins_nr = ev.ins_nr
        self.last_price.clear()
        self.exchtime.clear()
        self.localtime.clear()

    def on_eod(self, ev: EodEvent):
        date: int = int(ev.date)
        now: float = time.time()
        sod_eod_ts: float = now - self.start_ts
        print(f"on_eod:{date},total time:{sod_eod_ts:.2f}s,total tick cnt:{self.cnt}")

    def on_cs_snapshot(self, ev: CsSnapshotEvent):
        self.cnt += 1
        factors: np.ndarray = self.get_factors()
        print(
            f"on_cs_snapshot,{ev.exchtime},{ev.localtime},shape:{factors.shape}\n{factors}"
        )
        self.update_signal(ev.exchtime, ev.localtime, factors.sum(axis=0))


def pysig_create():
    return MySig()
