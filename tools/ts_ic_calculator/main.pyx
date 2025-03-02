import numpy as np
import yaml
from pathlib import Path
import pandas as pd
from typing import List, Dict
from cfi.wolverine.signal import *
from cfi.wolverine.marketdata import *
from cfi.wolverine.event import *
from cfi.wolverine.misc.sigreader import SignalReader
from ..utils.datacache_utils import DataCache
from ..utils.sig_file_type import SigFileType
from ..utils.calendar_utils import *
from cfi.wlpysig.tools.utils import common_utils
# libc.stdint provide c-native types c_uint32 etc
from libc.stdint cimport *
# speed up numpy arrays
cimport numpy as cnp
cnp.import_array()



class TsICCalculator(SignalBase):
    def __init__(self):
        super().__init__()
        self.signame: str = ""
        self.outname: str = ""
        self.sigdir: Path = Path("")
        self.sig_file_type: SigFileType = SigFileType.csv

        self.today: int = 0
        self.targets: List[str] = []
        self.sig_df: pd.DataFrame = None
        self.futret_bias: Dict[int, str] = {}
        self.cache: DataCache = DataCache()
        self.ffill_interval: int = 0
        self.use_system_uv: bool = False

        self.session: set(tuple) = set()
        self.exch_session: List[List] = []

    def _setup_output_file(self, cfg: dict):
        odir: Path = Path(cfg["output_dir"])
        odir.mkdir(parents=True, exist_ok=True)
        ofile: Path = odir / f"{self.outname}.csv"
        self.fout = open(ofile, "w", buffering=1)
        self.fout.write(
            f"date,target," +
            ",".join(
                self.futret_bias.values()) +
            "\n")

    def initialize(self, cfg_str: str):
        print(f"loading config")
        try:
            cfg = yaml.safe_load(cfg_str)
            self.signame = str(cfg["signame"])
            self.outname = str(cfg.get("outname", self.signame))
            self.sigdir = Path(cfg["sigdir"])
            self.sig_file_type: SigFileType = SigFileType[str(cfg["file_type"])]

            for futret_bias_str in cfg["futret_bias"]:
                self.futret_bias[common_utils.str2ns(futret_bias_str)] = futret_bias_str
            if "ffill_interval" in cfg:
                self.ffill_interval = common_utils.str2ns(str(cfg["ffill_interval"]))
            self.use_system_uv = cfg.get("use_system_uv", False)

            self._setup_output_file(cfg)
        except Exception as e:
            print(f"Error initializing configuration: {e}")

    def _load_signal(self):
        try:
            data_path: Path = self.sigdir / self.signame / str(self.today) / f"{self.signame}-{self.today}.data.{SigFileType(self.sig_file_type).name}"
            reader = SignalReader(data_path, instrument="stocks.CHN")
            df = reader.read(convert_localtime=False)
            df["localtime"] = df["localtime"].astype(np.int64).astype(np.uint64)
            df["exchtime"] = df["exchtime"].astype(np.int64)
            self.sig_df = df
        except Exception as e:
            print(f"Error loading signal data: {e}")

    def _get_session_tuples(self, ms: MdStatic):
        sessions = []
        for j in range(ms.session_nr):
            sessions.append(
                tuple([ms.session[j].begin, ms.session[j].end]))
        return tuple(sessions)

    def _check_session(self):
        if len(self.session) != 1:
            raise Exception(
                f"Inconsistent trading times in cross - section data")

        self.exch_session = list(list(i) for i in list(self.session)[0])
        for futret_bias, str_futret_bias in self.futret_bias.items():
            for s in self.exch_session:
                if futret_bias > s[1] - s[0]:
                    raise Exception(
                        f"futret_bias {str_futret_bias} exceed trading period length")

    def _process_signal_columns(self) -> List[str]:
        sig_targets: List[str] = list(self.sig_df.columns)
        for _i, _x in enumerate(sig_targets):
            if _x.startswith("SZ") or _x.startswith("SH"):
                sig_targets[_i] = f"{_x[2:]}.{_x[:2]}"
        self.sig_df.columns = sig_targets
        return [_x for _x in sig_targets if _x not in ["exchtime", "localtime"]]

    
    def on_sod(self, ev: SodEvent):
        date = int(ev.date)
        if self.today != date:
            self.today = date
            self._load_signal()
            self.cache.clear()
            self.session.clear()

        print(f"on_sod:{date},ins_nr:{ev.ins_nr}")
        self.targets.clear()
        for i in range(ev.ins_nr):
            ms: MdStatic = ev.ms[i].contents
            self.targets.append(ms.ticker.decode("utf8") + "." + ms.exchange.decode("utf8"))
            self.session.add(self._get_session_tuples(ms))
        
        self._check_session()

        sig_targets: List[str] = self._process_signal_columns()

        if self.targets != sig_targets:
            if self.use_system_uv:
                new_sigs   = set(self.targets) - set(sig_targets)
                extra_sigs = set(sig_targets) - set(self.targets)
                print(
                    f"use_system_uv is on,new:{len(new_sigs)},extra:{len(extra_sigs)}",
                    flush=True)
                self.sig_df[list(new_sigs)] = np.nan
                self.sig_df = self.sig_df[[
                    "exchtime", "localtime"] + self.targets].copy()
            else:
                raise RuntimeError(f"sig targets mismatch")

    def _write_ic_to_file(self, date: int, all_ic: Dict[int, List[float]]):
        for target_idx, target in enumerate(self.targets):
            ic_str = ",".join(
                f"{all_ic[fb][target_idx]}" if not np.isnan(all_ic[fb][target_idx]) else "" for fb in self.futret_bias)
            self.fout.write(
                f"{date},{target},{ic_str}\n")

    def on_eod(self, ev: EodEvent):
        date = int(ev.date)
        print(f"{date},calculating ic")
        local_session = common_utils.make_localtime_session(date, self.exch_session)
        target_nr = len(self.targets)
        # process signal array
        if self.ffill_interval:
            localtime_array, exchtime_array, shift_info = common_utils.make_filled_times(
                self.ffill_interval, local_session, self.exch_session)
            sig_array = np.full([len(localtime_array), target_nr],
                                fill_value=np.nan, dtype=np.float64)
            common_utils.match_A_with_B_cs(
                localtime_array,
                sig_array,
                self.sig_df["localtime"].values,
                self.sig_df[self.targets].astype(np.float64).values,
                True)
        else:
            localtime_array = self.sig_df["localtime"].values
            # exchtime_array = self.sig_df["exchtime"].values
            shift_info = common_utils.find_all_shift_info(localtime_array, local_session)
            sig_array = self.sig_df[self.targets].astype(np.float64).values

        # process market data array
        md_mid_px = (np.array(self.cache.ap, dtype=np.float64) +
                     np.array(self.cache.bp, dtype=np.float64)) / 2
        md_localtime = np.array(self.cache.localtime, dtype=np.uint64)

        sig_mid_px = np.full([len(localtime_array), target_nr],
                             fill_value=np.nan, dtype=np.float64)
        common_utils.match_A_with_B_cs(
            localtime_array,
            sig_mid_px,
            md_localtime,
            md_mid_px,
            True)

        all_ic: Dict = {}
        for fb, str_fb in self.futret_bias.items():
            print(f"start ic calculating for futret_bias {str_fb}")
            fut_localtime = localtime_array + fb
            common_utils.shift_fut_localtime(fut_localtime, shift_info)
            fut_mid_px = np.full([len(localtime_array), target_nr],
                                 fill_value=np.nan, dtype=np.float64)
            common_utils.match_A_with_B_cs(
                fut_localtime,
                fut_mid_px,
                md_localtime,
                md_mid_px,
                False)
            fut_ret = (fut_mid_px / sig_mid_px) - 1

            ic_list = []
            for target_idx in range(target_nr):
                ic = common_utils.calculate_ic(sig_array[:,target_idx], fut_ret[:, target_idx])
                ic_list.append(ic)
            all_ic[fb] = ic_list

        self._write_ic_to_file(date, all_ic)
        self.fout.close()
    
    def on_cs_snapshot(self, ev: CsSnapshotEvent):
        self.cache.push(ev)

def pysig_create():
    return TsICCalculator()