import numpy as np
from typing import List, Dict
import time
from cfi.wolverine.misc.calendar_utils import CalendarMgr
from .calendar_utils import *

cimport numpy as cnp
cnp.import_array()

def str2ns(val: str) -> int:
    """
    将带单位的时间字符串转换为纳秒数
    """
    if val.endswith("ns"):
        val_ns = int(val[:-2])
    elif val.endswith("s"):
        val_ns = int(val[:-1]) * int(1e9)
    elif val.endswith("m"):
        val_ns = int(val[:-1]) * int(1e9) * 60
    elif val.endswith("h"):
        val_ns = int(val[:-1]) * int(1e9) * 3600
    else:
        raise RuntimeError(f"unknown unit {val}")
    return val_ns


def make_localtime_session(date: int, session: List) -> List[List]:
    """
    根据日期和相对交易时间生成本地时间会话列表
    """
    daily_session: List = []
    str_time: str = str(date) + "00:00:00"
    time_stamp: float = time.mktime(time.strptime(str_time, "%Y%m%d%H:%M:%S"))
    today_ts: float = time_stamp * int(1e9)

    pre_business_day: int = CalendarMgr.get().shift(date, -1)
    pre_day: int = next_day(pre_business_day)
    pre_str_time: str = str(pre_day) + "00:00:00"
    pre_time_stamp: float = time.mktime(
        time.strptime(pre_str_time, "%Y%m%d%H:%M:%S"))
    pre_ts: float = pre_time_stamp * int(1e9)

    for s in session:
        if s[0] < 0:
            daily_session.append([np.uint64(i + pre_ts) for i in s])
        else:
            daily_session.append([np.uint64(i + today_ts) for i in s])

    return daily_session


def search_session_end_index(
        const cnp.uint64_t[:] sig_localtime,
        const cnp.uint64_t target,
        int left,
        int right) -> int:
    """
    二分查找第一个小于等于target的索引
    """
    cdef int l = left, mid
    while left <= right:
        mid = ((right - left) >> 1) + left
        if sig_localtime[mid] > target:
            right = mid - 1
        else:
            left = mid + 1

    if right < l:
        return -1
    return right


def find_all_shift_info(
        sig_localtime: cnp.uint64_t[:], sessions: List) -> List[Dict[str, int]]:
    left: int = 0
    right: int = len(sig_localtime) - 1
    session_num: int = len(sessions)
    ans: List[Dict[str, int]] = []

    for i in range(session_num - 1):
        idx = search_session_end_index(
            sig_localtime, sessions[i][1], left, right)
        if idx != -1:
            ans.append({
                "session_end": sessions[i][1],
                "start_shift_idx": idx,
                "time_shift": sessions[i + 1][0] - sessions[i][1]
            })
            left = idx + 1

    return ans


def match_A_with_B_cs(
        const cnp.uint64_t[:] A_localtime,
        cnp.float64_t[:, :] ans,
        const cnp.uint64_t[:] B_localtime,
        const cnp.float64_t[:, :] matched_data,
        const bint can_exceed):
    """
    根据A_localtime的时间戳，用matched_data填充ans
    """    
    cdef int A_idx = 0, B_idx = 0
    cdef int A_nr = len(A_localtime), B_nr = len(B_localtime)
    
    while A_idx < A_nr:
        while B_idx < B_nr and B_localtime[B_idx] <= A_localtime[A_idx]:
            B_idx += 1
        if B_idx and (can_exceed or B_idx < B_nr):
            ans[A_idx, :] = matched_data[B_idx - 1, :]

        A_idx += 1

def make_filled_times(ffill_interval: int,
                      local_session: List,
                      exch_session: List) -> tuple[cnp.uint64_t[:], cnp.int64_t[:], List]:
    """
    生成填充后的时间数组和偏移信息
    """
    shift_info: List[Dict[str, int]] = []
    idx: int = 0
    ffilled_local_arrays: List = []
    session_num = len(local_session)
    for i, session in enumerate(local_session):
        tmp = np.arange(
            session[0],
            session[1] + 1,
            ffill_interval,
            dtype=np.uint64)
        ffilled_local_arrays.append(tmp)
        idx += len(tmp)
        if i < session_num - 1:
            shift_info.append({
                "session_end": session[1],
                "start_shift_idx": idx - 1,
                "time_shift": local_session[i + 1][0] - session[1]
            })

    ffilled_local_array = np.hstack(ffilled_local_arrays)

    ffilled_exch_arrays: List = []
    for session in exch_session:
        tmp = np.arange(session[0], session[1], ffill_interval, dtype=np.int64)
        ffilled_exch_arrays.append(tmp)
    ffilled_exch_array = np.hstack(ffilled_exch_arrays)

    return ffilled_local_array, ffilled_exch_array, shift_info


def shift_fut_localtime(fut_localtime: cnp.uint64_t[:], shift_info: List):
    """
    调整加入bias偏移后的行情数组
    """
    cdef int start_idx
    cdef cnp.uint64_t session_end
    cdef cnp.uint64_t time_shift

    for info in shift_info:
        start_idx = info["start_shift_idx"]
        session_end = info["session_end"]
        time_shift = info["time_shift"]

        while fut_localtime[start_idx] > session_end:
            fut_localtime[start_idx] += time_shift
            start_idx -= 1

def calculate_ic(sig_array, fut_ret):
    valid_mask = ~np.isnan(sig_array) & ~np.isnan(fut_ret)
    valid_sig = sig_array[valid_mask]
    valid_fut_ret = fut_ret[valid_mask]
    if len(valid_sig) > 0:
        ic = np.ma.corrcoef(
            np.ma.masked_invalid(valid_sig),
            np.ma.masked_invalid(valid_fut_ret)
        )[0][1]
        return ic
    return np.nan