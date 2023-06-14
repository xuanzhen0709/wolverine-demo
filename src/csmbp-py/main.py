import numpy as np
import time
import yaml

from cfi.wolverine.signal import *


class MySig(SignalBase):

    def __init__(self):
        super().__init__()
        self.cnt: int = 0
        self.mss = []
        self.last_price = []
        self.exchtime = []
        self.localtime = []
        self.start_ts: float = 0

    def initialize(self, path: str):
        if not path:
            return
        print(f"loadding config:{path}")
        with open(path) as fin:
            cfg = yaml.safe_load(fin)


    def on_sod(self, date: int, ev: SodEvent):
        self.start_ts = time.time()
        self.cnt = 0
        self.mss.clear()

        targets = []
        print(f"on_sod:{date},ins_nr:{ev.ins_nr}")
        for i in range(ev.ins_nr):
            ms: MdStatic = ev.ms[i].contents
            self.mss.append(ms)

            targets.append(
                ms.instrument.decode("utf8") + "." +
                ms.exchange.decode("utf8"))
            # print(f"\t{i+1},{ms.instrument}")
        self.last_price.clear()
        self.exchtime.clear()
        self.localtime.clear()

    def on_eod(self, date: int):
        now: float = time.time()
        sod_eod_ts: float = now - self.start_ts
        print(
            f"on_eod:{date},total time:{sod_eod_ts:.2f}s,total tick cnt:{self.cnt}"
        )
        # concat cached data
        exchtime: np.ndarray = np.array(self.exchtime, dtype=np.int64)
        localtime: np.ndarray = np.array(self.localtime, dtype=np.uint64)
        last_price: np.ndarray = np.array(self.last_price, dtype=np.float64)
        now_2: float = time.time()
        concat_ts: float = now_2 - now
        print(f"concat time:{concat_ts:.2}s")

        # just for demonstration purposes, we try to update signals using dummy values
        ins_nr: int = len(self.mss)
        for idx in range(len(exchtime)):
            self.update_signal(int(exchtime[idx]), int(localtime[idx]),
                               np.full((ins_nr, ), idx, dtype=np.float64))

    def on_cs_snapshot(self, ev: CsSnapshotEvent):
        self.cnt += 1
        print(f"on_cs_snapshot,{ev.exchtime}")
        self.exchtime.append(ev.exchtime)
        self.localtime.append(ev.localtime)

        # ev.data is a dictionary mapping from int -> np.ndarray
        last_price_data = ev.data[MdFld.last_price.value]
        # NOTE: we must explicitly create a copy of the data if we cache it in any way
        self.last_price.append(np.ndarray.copy(last_price_data))

    def on_cs_mbp(self, ev: CsMbpEvent):
        exchtime: ctypes.c_int64 = ev.exchtime
        localtime: ctypes.c_uint64 = ev.localtime
        ins_nr: ctypes.c_int64 = ev.ins_nr
        print(f"on_cs_mbp:{exchtime},{localtime},{ins_nr}")

        # for idx in range(ins_nr):
        #     ins_data: CsMbpInsData = ev.ins_data[idx].contents
        #     ms: MdStatic = ins_data.ms.contents

        #     print(f"\t{ms.instrument},{ins_data.msg_nr}")
        #     for i in range(ins_data.msg_nr):
        #         msg: CsMbpMsg = ins_data.msgs[i].contents
        #         if msg.type == CsMbpMsg.MsgType_NewOrder.value:
        #             order: NewOrder = msg.ptr.order.contents
        #             print(f"\t\t{msg.type},order,{order.qty}@{order.price},{order.side},{order.type}")
        #         elif msg.type == CsMbpMsg.MsgType_CancelOrder.value:
        #             cancel: CancelOrder = msg.ptr.cancel.contents
        #             print(f"\t\t{msg.type},cancel,{cancel.qty}@{cancel.price},{cancel.side},{cancel.type}")
        #         elif msg.type == CsMbpMsg.MsgType_Trade.value:
        #             trade: CancelOrder = msg.ptr.trade.contents
        #             print(f"\t\t{msg.type},trade,{trade.qty}@{trade.price},{trade.bid_oid}/{trade.ask_oid}")
        #         elif msg.type == CsMbpMsg.MsgType_Mbp.value:
        #             mbp: MBP = msg.ptr.mbp.contents
        #             print(f"\t\t{msg.type},mbp,{mbp.bid_qty[0]}@{mbp.bid_px[0]},{mbp.ask_qty[0]}/{mbp.ask_px[0]}")
        #         else:
        #             print(f"\t\t{msg.type},unknown,{type(msg.type)},{CsMbpMsg.MsgType_NewOrder},{type(CsMbpMsg.MsgType_NewOrder)}")
                
        #         if i >= 9:
        #             break


def pysig_create():
    return MySig()
