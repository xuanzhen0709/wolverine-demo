import numpy as np
from typing import Dict, Tuple
import yaml

from cfi.wolverine.marketdata import MdFld, MdStatic, MdSession, MdSnapshot, MdBar


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

    def on_sod(self, date: int, ms: MdStatic):
        pass

    def on_snapshot(self, ms: MdStatic, data) -> float:
        raise RuntimeError("Not implemented")

    def on_bar(self, ms: MdStatic, bar: MdBar) -> float:
        idx: int = self.cnt % self.window
        self.buffer[idx, :] = (bar.close, bar.volume)
        self.cnt += 1
        if self.cnt < self.window:
            return np.nan
        return np.corrcoef(self.buffer[0, :], self.buffer[1, :])[0, 1]

    def on_snapshot_batch(
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


def pysig_on_sod(hdl, date: int, ms_ptr: int):
    ms: MdStatic = MdStatic.from_address(ms_ptr)
    hdl.on_sod(date, ms)


def pysig_on_snapshot(hdl, ms_ptr: int, md_ptr: int):
    ms: MdStatic = MdStatic.from_address(ms_ptr)
    md: MdSnapshot = MdSnapshot.from_address(md_ptr)
    return hdl.on_snapshot(ms, md)


def pysig_on_bar(hdl, ms_ptr: int, bar_ptr: int):
    ms: MdStatic = MdStatic.from_address(ms_ptr)
    bar: MdBar = MdBar.from_address(bar_ptr)
    return hdl.on_bar(ms, bar)


def pysig_on_snapshot_batch(hdl, ms_ptr: int, md: Dict[MdFld, np.ndarray]):
    ms: MdStatic = MdStatic.from_address(ms_ptr)
    return hdl.on_snapshot_batch(ms, md)


def pysig_on_eod(hdl, date: int):
    hdl.on_eod(date)