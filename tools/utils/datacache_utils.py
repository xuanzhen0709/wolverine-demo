import numpy as np
from cfi.wolverine.event import *

class DataCache:
    def __init__(self):
        self.exchtime = []
        self.localtime = []
        self.ap = []
        self.bp = []

    def push(self, ev: CsSnapshotEvent):
        self.exchtime.append(ev.exchtime)
        self.localtime.append(ev.localtime)
        self.ap.append(np.ndarray.copy(
            ev.data[CsSnapshotEvent.FldType.AP.value][0]))
        self.bp.append(np.ndarray.copy(
            ev.data[CsSnapshotEvent.FldType.BP.value][0]))

    def clear(self):
        self.exchtime.clear()
        self.localtime.clear()
        self.ap.clear()
        self.bp.clear()
