#!/usr/bin/python3
import argparse
import datetime
from pathlib import Path
import pandas as pd
import sqlalchemy as sa
from typing import List, Tuple


def parse_date(val: str) -> int:
    datetime.datetime.strptime(val, "%Y%m%d")
    return int(val)


class Calendar:

    def __init__(self, path: Path):
        self.dates = pd.read_csv(path, header=None, dtype=int)[0].values
        self.dates = sorted(self.dates)

    def next_business_day(self, date, shift=1) -> int:
        if shift >= 1:
            return [x for x in self.dates if x > int(date)][shift - 1]
        if not shift:
            raise
        return [x for x in self.dates if x < int(date)][shift]

    def date_range(self, start: int, end: int) -> List[int]:
        return [x for x in self.dates if x >= start and x <= end]


def markettype_to_session(
        mkttype: int,
        has_night: bool) -> List[Tuple[datetime.time, datetime.time]]:
    sessions = list()
    if mkttype == 1:
        sessions.append(
            (datetime.time(hour=9, minute=15), datetime.time(hour=11,
                                                             minute=30)))
        sessions.append(
            (datetime.time(hour=13, minute=0), datetime.time(hour=15,
                                                             minute=15)))
    elif mkttype == 2:
        sessions.append(
            (datetime.time(hour=9, minute=0), datetime.time(hour=10,
                                                            minute=15)))
        sessions.append(
            (datetime.time(hour=10,
                           minute=30), datetime.time(hour=11, minute=30)))
        sessions.append(
            (datetime.time(hour=13, minute=30), datetime.time(hour=15,
                                                              minute=0)))
    elif mkttype == 3:
        if has_night:
            sessions.append(
                (datetime.time(hour=21,
                               minute=0), datetime.time(hour=2, minute=30)))

        sessions.append(
            (datetime.time(hour=9, minute=0), datetime.time(hour=10,
                                                            minute=15)))
        sessions.append(
            (datetime.time(hour=10,
                           minute=30), datetime.time(hour=11, minute=30)))
        sessions.append(
            (datetime.time(hour=13, minute=30), datetime.time(hour=15,
                                                              minute=0)))
    elif mkttype == 4:
        if has_night:
            sessions.append(
                (datetime.time(hour=21,
                               minute=0), datetime.time(hour=1, minute=0)))
        sessions.append(
            (datetime.time(hour=9, minute=0), datetime.time(hour=10,
                                                            minute=15)))
        sessions.append(
            (datetime.time(hour=10,
                           minute=30), datetime.time(hour=11, minute=30)))
        sessions.append(
            (datetime.time(hour=13, minute=30), datetime.time(hour=15,
                                                              minute=0)))
    elif mkttype == 5:
        if has_night:
            sessions.append(
                (datetime.time(hour=21,
                               minute=0), datetime.time(hour=23, minute=30)))

        sessions.append(
            (datetime.time(hour=9, minute=0), datetime.time(hour=10,
                                                            minute=15)))
        sessions.append(
            (datetime.time(hour=10,
                           minute=30), datetime.time(hour=11, minute=30)))
        sessions.append(
            (datetime.time(hour=13, minute=30), datetime.time(hour=15,
                                                              minute=0)))
    elif mkttype == 6:
        sessions = []
        if has_night:
            sessions.append(
                (datetime.time(hour=21,
                               minute=0), datetime.time(hour=23, minute=0)))

        sessions.append(
            (datetime.time(hour=9, minute=0), datetime.time(hour=10,
                                                            minute=15)))
        sessions.append(
            (datetime.time(hour=10,
                           minute=30), datetime.time(hour=11, minute=30)))
        sessions.append(
            (datetime.time(hour=13, minute=30), datetime.time(hour=15,
                                                              minute=0)))
    elif mkttype == 11:
        sessions.append(
            (datetime.time(hour=9, minute=30), datetime.time(hour=11,
                                                             minute=30)))
        sessions.append(
            (datetime.time(hour=13, minute=0), datetime.time(hour=15,
                                                             minute=0)))
    elif mkttype == 12:
        sessions.append(
            (datetime.time(hour=9, minute=30), datetime.time(hour=11,
                                                             minute=30)))
        sessions.append(
            (datetime.time(hour=13, minute=0), datetime.time(hour=15,
                                                             minute=15)))
    else:
        raise RuntimeError(f"Unsupported markettype {mkttype}")

    return sessions


def get_session(cal: Calendar, date: int, mkttype: int) -> str:
    date_str: str = str(date)
    date_pre_str: str = str(cal.next_business_day(date, shift=-1))

    dt: datetime.datetime = datetime.datetime.strptime(date_str, "%Y%m%d")
    pre_dt: datetime.datetime = datetime.datetime.strptime(
        date_pre_str, "%Y%m%d")

    has_night: bool = (dt.weekday() == 0 and pre_dt == dt - datetime.timedelta(days=3)) or \
            (pre_dt == dt - datetime.timedelta(days=1))

    sessions = markettype_to_session(mkttype, has_night)

    session_strs = [
        "-".join([x.strftime("%H%M%S") for x in y]) for y in sessions
    ]
    return ";".join(session_strs)


def get_future_insinfo(cal, engine, meta, date: int) -> pd.DataFrame:
    pc_table: sa.Table = sa.Table("PrimaryContract",
                                  meta,
                                  autoload_with=engine)
    ii_table: sa.Table = sa.Table("InsInfo", meta, autoload_with=engine)
    # print(list(prod_ticker_map.keys()))
    with engine.begin() as conn:
        pc_today = sa.select(pc_table).where(
            pc_table.c.Date == int(date),
            pc_table.c.Product.notlike("%[0-9]")).subquery("pc")
        ii_today = sa.select(ii_table).where(
            ii_table.c.Date == int(date)).subquery("ii")

        all_join = sa.join(pc_today, ii_today,
                           pc_today.c.Instrument == ii_today.c.Instrument)
        stmt = sa.select(all_join)
        # stmt = sa.select(ii_today).join(
        #     pc_today, ii_today.c.Instrument == pc_today.c.Instrument)
        data = []
        for row in conn.execute(stmt).fetchall():
            data.append(dict(row))

        if not data:
            raise RuntimeError("date {date} nothing returned")

        df = pd.DataFrame.from_records(data)
        df = df[[col for col in df.columns if not col.endswith("_1")]]
        df.set_index("Ticker", inplace=True, verify_integrity=True)
        df["Session"] = df.apply(
            lambda x: get_session(cal, x["Date"], x["MarketType"]), axis=1)
        df.drop(columns=["Date", "Product"], inplace=True)
        return df


# /mnt/nas-v/StockData/ConfigDailyStore/20230413/signaldb-config/20230413-insinfo.csv
def get_stock_insinfo(cal, date: int, prefix_path: Path) -> pd.DataFrame:
    origin_csv: Path = prefix_path.joinpath(f"{date}", "signaldb-config",
                                            f"{date}-insinfo.csv")
    origin_df = pd.read_csv(origin_csv,
                            header=0,
                            index_col=None,
                            encoding="gbk")

    new_df = pd.DataFrame(origin_df,
                          columns=[
                              'id', 'tick', 'multiplier', 'preclose',
                              'lowlimit', 'highlimit'
                          ])
    new_df.rename(columns={
        'id': 'Instrument',
        'tick': 'TickSize',
        'multiplier': 'Multiplier',
        'preclose': 'Close',
        'lowlimit': 'LimitDown',
        'highlimit': 'LimitUp'
    },
                  inplace=True)
    new_df = new_df[~new_df["Instrument"].str.startswith("I")]

    new_df['Exchange'] = new_df['Instrument'].str[:2]
    new_df['Ticker'] = new_df['Instrument'].str[2:]
    sessions = get_session(cal, date, 11)
    new_df["Session"] = sessions
    new_df.set_index("Ticker", inplace=True, verify_integrity=True)
    return new_df


def main():
    parser = argparse.ArgumentParser("generate primary contract info")
    parser.add_argument("-c",
                        "--calendar",
                        type=lambda x: Path(x).resolve(),
                        default=Path("ChinaTradingDates.txt"),
                        help="calendar file")
    parser.add_argument("-o",
                        "--output-dir",
                        type=lambda x: Path(x).resolve(),
                        default=Path("data/insinfo"),
                        help="output dir")
    parser.add_argument("-sp",
                        "--stock-path",
                        type=lambda x: Path(x).resolve(),
                        default=Path("/mnt/nas-v/StockData/ConfigDailyStore/"),
                        help="stock insinfo path")
    parser.add_argument("-sd",
                        "--start",
                        type=parse_date,
                        required=True,
                        help="start date")
    parser.add_argument("-ed",
                        "--end",
                        type=parse_date,
                        required=True,
                        help="end date")
    args = parser.parse_args()

    cal = Calendar(args.calendar)

    engine = sa.create_engine(
        "mssql+pymssql://public_data:public_data@dbs.cfi/RefData?charset=utf8")

    meta = sa.MetaData()

    output_dir: Path = args.output_dir
    stock_path: Path = args.stock_path
    output_dir.mkdir(parents=True, exist_ok=True)

    errors = {}
    for date in cal.date_range(args.start, args.end):
        print(f"running date {date}")
        try:
            future_df = get_future_insinfo(cal, engine, meta, date)
        except Exception as future_error:
            errors[date] = [future_error]

        try:
            stock_df = get_stock_insinfo(cal, date, stock_path)
        except Exception as stock_error:
            if date in errors.keys():
                errors[date].append(stock_error)
            else:
                errors[date] = [stock_error]

        if date not in errors.keys():
            df = pd.concat([future_df, stock_df])
            df.to_csv(output_dir.joinpath(f"{date}.csv"))

    if errors:
        str_error = '\n'
        for (date, error) in errors.items():
            str_error += "  " + str(date) + " : " + " ".join(
                str(x) for x in error) + '\n'
        raise RuntimeError(str_error)


if __name__ == "__main__":
    main()