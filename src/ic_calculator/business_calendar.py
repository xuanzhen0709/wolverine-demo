# -*- coding: utf-8 -*-
import datetime
import numpy as np
from sqlalchemy import create_engine
import pandas as pd
from typing import List, Set, Union

class Calendar:
    """
    This class provides trading calendar lookups.

    A typical use case would be:

    Calendar.get_instance().next_business_day(20211202, 1)
    """
    __INSTANCE = None

    def __init__(self, coutry_code="CHN"):
        self.coutry_code = coutry_code
        _engine = create_engine(f"mssql+pymssql://public_data:public_data@dbs.cfi/WDDB")
        _select_sql = f'''
            SELECT WORKING_DATE FROM GLOBALWORKINGDAY WHERE COUNTRY_CODE = '{self.coutry_code}' ORDER BY WORKING_DATE
        '''
        _df = pd.read_sql(_select_sql, con=_engine)   
        self.dates: np.ndarray = np.array(_df['WORKING_DATE'], dtype=np.uint32)
        self.date_set: Set[int] = set(self.dates)

    @staticmethod
    def get_instance() -> "Calendar":
        """
        Return:
            the global Calendar instance
        """
        if Calendar.__INSTANCE is None:
            Calendar.__INSTANCE = Calendar()
        return Calendar.__INSTANCE

    def next_business_day(self, date: Union[str, int], shift: int = 1) -> int:
        """
        find the business day with a an optional shift

        Args:
            date: reference date
            shift: offset to the reference date

        Return:
            date
        """
        date_int = int(date)
        idx: int = int(np.searchsorted(self.dates, date_int, side="left"))
        if self.dates[idx] == date_int:
            return int(self.dates[idx + shift])
        if shift > 0:
            idx -= 1
        return int(self.dates[idx + shift])

    def is_business_day(self, date: Union[str, int]) -> bool:
        """
        tells if the given date is a business day

        Args:
            date: given date

        Return:
            whether the given date is a business day or not
        """
        date_int = int(date)
        return date_int in self.date_set

    def get_business_days(self, start_date: Union[str, int],
                          end_date: Union[str, int]) -> List[int]:
        """
        retrieve a list of business days given a date range

        Args:
            start_date: start of the range (inclusive)
            end_date: end of the range (inclusive)
        
        Return:
            list of dates (integer)
        """
        start: int = int(start_date)
        end: int = int(end_date)
        st_idx: int = np.searchsorted(self.dates, start, side="left")
        rt_idx: int = np.searchsorted(self.dates, end, side="right")
        return [int(x) for x in self.dates[st_idx:rt_idx]]


def today() -> int:
    """
    Return:
        today as an integer
    """
    return int(datetime.date.today().strftime("%Y%m%d"))


def next_day(date: Union[str, int], shift: int = 1) -> int:
    """
    find the date with an offset in term of CALENDAR days to the given date

    Args:
        date: reference date
        shift: offset to the date in CALENDAR days
    
    Return:
        date
    """
    n_day: datetime.datetime = datetime.datetime.strptime(
        str(date), "%Y%m%d") + datetime.timedelta(days=shift)
    return int(n_day.strftime("%Y%m%d"))


if __name__ == "__main__":
    cal = Calendar.get_instance()
    print(cal.next_business_day(20150102, 1))
    print(cal.next_business_day(20150102, -1))
    print(cal.next_business_day(20150105, 1))
    print(cal.next_business_day(20150105, -1))
    k = cal.get_business_days(20150105, 20150110)
    print(cal.next_business_day(20150106, -1))
    print(cal.is_business_day(20231121))
