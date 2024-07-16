import datetime
from typing import Union


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
        str(date), "%Y%m%d"
    ) + datetime.timedelta(days=shift)
    return int(n_day.strftime("%Y%m%d"))
