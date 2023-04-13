import numpy as np
from typing import Dict, Tuple
import yaml

from cfi.wolverine.marketdata import *


class MySig:

    def __init__(self):
        self.cnt: int = 0
        self.window: int = 0
        self.buffer: np.ndarray = np.full((0, 0), np.nan, dtype=np.float64)

    def load_config(self, path: str):
        if not path:
            return
        print(f"loadding config:{path}")
        with open(path) as fin:
            cfg = yaml.safe_load(fin)
            self.window = int(cfg["window"])
            self.buffer.resize((self.window, 2))

    def on_sod(self, date: int, ev: SodEvent):
        pass

    def on_snapshot(self, ev: SnapshotEvent) -> float:
        raise RuntimeError("Not implemented")

    def on_bar(self, ev: BarEvent) -> float:
        bar: MdBar = MdBar.from_address(ev.bar)

        idx: int = self.cnt % self.window
        self.buffer[idx, :] = (bar.close, bar.volume)
        self.cnt += 1
        if self.cnt < self.window:
            return np.nan
        return np.corrcoef(self.buffer[0, :], self.buffer[1, :])[0, 1]

    def on_whole_day_snapshots(
            self, ms: MdStatic,
            md: Dict[MdFld, np.ndarray]) -> Tuple[np.ndarray, np.ndarray]:
        raise RuntimeError("Not implemented")

    def on_eod(self, date: int):
        print(f"total tick cnt:{self.cnt}")
        pass


def pysig_create():
    return MySig()


def pysig_load_config(hdl, path: str):
    hdl.load_config(path)


def pysig_on_sod(hdl, date: int, ev_ptr: int):
    ev: SodEvent = SodEvent.from_address(ev_ptr)
    hdl.on_sod(date, ev)


def pysig_on_snapshot(hdl, ev_ptr: int):
    ev: SnapshotEvent = SnapshotEvent.from_address(ev_ptr)
    return hdl.on_snapshot(ev)


def pysig_on_whole_day_snapshots(hdl, ms_ptr: int, md: Dict[MdFld, np.ndarray]):
    ms: MdStatic = MdStatic.from_address(ms_ptr)
    return hdl.on_whole_day_snapshots(ms, md)


def pysig_on_bar(hdl, ev_ptr: int):
    ev: BarEvent = BarEvent.from_address(ev_ptr)
    return hdl.on_bar(ev)


def pysig_on_eod(hdl, date: int):
    hdl.on_eod(date)

