import calendar

from datetime import datetime


def dt_to_ts(dt):
    return calendar.timegm(dt.utctimetuple())


def dt_to_ts_exact(dt):
    return calendar.timegm(dt.utctimetuple()) + dt.microsecond / 1e6


def decompose_dt(dt):
    return dt_to_ts(dt), dt.microsecond


def compose_dt(t_s, t_us):
    return datetime.utcfromtimestamp(t_s).replace(
        microsecond=t_us
    )
