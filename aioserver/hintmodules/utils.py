import asyncio
import importlib
import logging
import re

from datetime import timedelta
from calendar import timegm


TIMEDELTA_RE = re.compile(
    r"^\s*(?P<value>[0-9]+(\.[0-9]*)?)(?P<unit>[dhms])\s*$",
    re.I,
)


def log_failure(logger):
    def impl(fut):
        try:
            result = fut.result()
        except asyncio.CancelledError:
            pass
        except:
            logger.exception("%r raised an exception", fut)
        else:
            if result is not None:
                logger.info("%r returned an unexpected result: %r",
                            fut, result)
    return impl


def to_timestamp(dt):
    """
    Convert `dt` to a UTC unix timestamp.
    """
    return timegm(dt.utctimetuple())


def parse_timedelta_ex(s):
    match = TIMEDELTA_RE.match(s)
    if match is None:
        raise ValueError("not a valid time interval: {!r}".format(s))

    info = match.groupdict()

    time_info = {
        "h": ("hours", {"minute": 0, "second": 0, "microsecond": 0}),
        "m": ("minutes", {"second": 0, "microsecond": 0}),
        "s": ("seconds", {"microsecond": 0}),
        "d": ("days", {"hour": 0, "minute": 0, "second": 0, "microsecond": 0}),
    }

    kwarg, clear = time_info[info["unit"]]

    kwargs = {
        kwarg: float(info["value"])
    }

    return timedelta(**kwargs), clear


def parse_timedelta(s):
    return parse_timedelta_ex(s)[0]


def get_class_by_path(path, *, logger=None):
    logger = logger or logging.getLogger("__name__")

    module_name, class_ = path.rsplit(".", 1)
    try:
        module = importlib.import_module(module_name)
    except ImportError:
        logger.error("failed to import plugin module %r",
                     module_name,
                     exc_info=True)
        raise ValueError("invalid class: {!r}".format(path))

    try:
        class_ = getattr(module, class_)
    except AttributeError:
        logger.error(
            "failed to find class %r in plugin module %r",
            class_,
            module_name,
            exc_info=True,
        )
        raise ValueError("invalid class: {!r}".format(path))

    return class_
