import numpy as np
import yaml

from cfi.wolverine.signal import *


class MySig(SignalBase):

    def __init__(self):
        super().__init__()
        self.cnt: int = 0
        self.sigval: np.ndarray = np.full((1, ), np.nan, dtype=np.float64)
        print(self.sigval)

    def initialize(self, path: str):
        if not path:
            return
        print(f"loadding config:{path}")
        with open(path) as fin:
            cfg = yaml.safe_load(fin)

            for _i in cfg["marketdata"]:
                self.subscribe(_i["type"], [], _i["symbols"])

            self.set_targets(cfg["targets"])

    def on_sod(self, date: int, ev: SodEvent):
        self.cnt = 0
        print(f"on_sod:{date},ins_nr:{ev.ins_nr}")
        for i in range(ev.ins_nr):
            ms: MdStatic = ev.ms[i].contents
            print(f"\t{i+1},{ms.instrument}")

    def on_eod(self, date: int):
        print(f"on_eod:{date},total tick cnt:{self.cnt}")

    def on_snapshot(self, ev: SnapshotEvent):
        ms: MdStatic = ev.ms.contents
        ss: MdSnapshot = ev.snapshot.contents

        print(
            f"on_snapshot:{ss.md_type},{ms.instrument},{ss.exchtime},{ss.last_price},{ss.levels[0]}"
        )
        # raise NotImplementedError

    def on_bar(self, ev: BarEvent):
        self.cnt += 1
        ms: MdStatic = ev.ms.contents
        bar: MdBar = ev.bar.contents
        print(
            f"on_bar:{ms.instrument},{ms.exchange},{bar.exchtime},{bar.localtime},{bar.open}/{bar.high}/{bar.low}/{bar.close},{bar.volume},{bar.turnover}"
        )
        self.sigval[0] = self.cnt
        self.update_signal(bar.exchtime, bar.localtime, self.sigval)

    def on_cs_snapshot(self, ev: CsSnapshotEvent):
        print("on_cs_snapshot")
        # raise NotImplementedError


def pysig_create():
    return MySig()
