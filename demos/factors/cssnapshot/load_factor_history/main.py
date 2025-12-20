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
        ystd = self._call_api("get_trading_date", ev.date, -1)
        print(f"today:{ev.date},ystd:{ystd}\n")
        factors = self._call_api("get_factor_list")

        for i in range(len(factors)):
            print(f"loading factor history,{factors[i]},date:{ystd}")
            fct_histdata = self._call_api("load_factor_history", i, ystd)
            print(fct_histdata)
        
    def on_eod(self, ev: EodEvent):
        pass

    def on_cs_snapshot(self, ev: CsSnapshotEvent):
        pass


def pysig_create():
    return MySig()
