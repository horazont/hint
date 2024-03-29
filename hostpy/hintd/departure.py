import asyncio
import functools
import json
import logging
import re
import time
import typing

from datetime import timedelta, datetime

import aiohttp

import hintlib.cache
import hintlib.xso

from hintlib.services import PeerLockService, RestartingTask

from .ui import Screen, metrics

from hintd.cconstants import LPCFont, rgb24_to_rgb16, LPCTableAlignment


AGE_MAP = [
    "█",
    "▉",
    "▊",
    "▋",
    "▌",
    "▍",
    "▎",
    "▏",
]


DVB_DATE_RE = re.compile(
    r"/Date\((?P<unixms>[0-9]+)-(?P<offset>[0-9]+)\)/",
    re.I,
)


def parse_dvb_date(s: str) -> datetime:
    match = DVB_DATE_RE.match(s)
    if not match:
        raise ValueError("not a valid DVB date: {!r}".format(s))
    data = match.groupdict()
    if data["offset"] != "0000":
        raise ValueError("unexpected offset!")
    return datetime.utcfromtimestamp(int(data["unixms"]) / 1000)


class DepartureModelRow(typing.NamedTuple):
    lane: str
    destination: str
    timestamp: float
    eta: float
    labels: typing.Optional[frozenset] = None
    colour: typing.Optional[typing.Tuple[int, int, int]] = None


class StopConfig(typing.NamedTuple):
    id_: str
    offset: float
    filter_func: typing.Callable


class DepartureScreen(Screen):
    ROW_HEIGHT = 14

    def __init__(self):
        super().__init__(
            "DVB",
            "DVB Abfahrtsmonitor"
        )
        self.data = []

    def quality_char(self, age):
        qmin = round(age.total_seconds() / 15)

        if qmin <= 4:
            index = qmin
        else:
            index = 4 + ((qmin - 4) / 2)

        if index >= len(AGE_MAP):
            index = len(AGE_MAP)-1

        return AGE_MAP[qmin]

    def paint(self):
        ntotalrows = metrics.SCREEN_CLIENT_AREA_HEIGHT // self.ROW_HEIGHT
        ndatarows = ntotalrows - 1
        table_height = ntotalrows * self.ROW_HEIGHT
        table_width = 40+168+28+18

        x0 = (metrics.SCREEN_CLIENT_AREA_WIDTH - table_width) // 2
        y0 = (metrics.SCREEN_CLIENT_AREA_HEIGHT - table_height) // 2

        self._ui.table_start(
            x0,
            y0+self.ROW_HEIGHT,
            self.ROW_HEIGHT,
            [
                (40, LPCTableAlignment.LEFT),
                (168, LPCTableAlignment.LEFT),
                (28, LPCTableAlignment.RIGHT),
                (18, LPCTableAlignment.CENTER),
            ]
        )

        self._ui.table_row(
            LPCFont.DEJAVU_SANS_12PX_BF,
            metrics.THEME_CLIENT_AREA_COLOUR,
            metrics.THEME_CLIENT_AREA_BACKGROUND_COLOUR,
            [
                "L#",
                "Fahrtziel",
                "min",
                AGE_MAP[0],
            ]
        )

        now = datetime.utcnow()
        view_data = [
            (row, dt)
            for row, dt in (
                (row, row.eta - now) for row in self.data
            )
            if dt > timedelta(minutes=-1.5)
        ]

        for row, dt in view_data[:ndatarows]:
            age = row.timestamp - now
            colour = row.colour or metrics.THEME_CLIENT_AREA_BACKGROUND_COLOUR
            self._ui.table_row(
                LPCFont.DEJAVU_SANS_12PX,
                metrics.THEME_CLIENT_AREA_COLOUR,
                colour,
                [
                    row.lane,
                    row.destination,
                    str(round(dt.total_seconds() / 60.0)),
                    self.quality_char(age),
                ],
            )

        for i in range(len(view_data), ndatarows):
            self._ui.table_row(
                LPCFont.DEJAVU_SANS_12PX,
                metrics.THEME_CLIENT_AREA_COLOUR,
                metrics.THEME_CLIENT_AREA_BACKGROUND_COLOUR,
                ["", "", "", ""],
            )


class DVBRequester(hintlib.cache.AdvancedHTTPRequester):
    API_URL = "https://webapi.vvo-online.de/dm?format=json"
    CACHE_TTL = timedelta(seconds=15)

    def _create_session(self):
        return aiohttp.ClientSession(
            timeout=aiohttp.ClientTimeout(total=15)
        )

    def _get_backing_off_result(self, expired_cache_entry=None, **kwargs):
        return expired_cache_entry

    async def _perform_http_request(self,
                                    session,
                                    expired_cache_entry=None,
                                    *,
                                    stop_id=None):
        now = datetime.utcnow()

        request = {
            "isarrival": True,
            "limit": 60,
            "mentzonly": False,
            "mot": [
                "Tram",
                "CityBus",
                "IntercityBus",
                "SuburbanRailway",
                "Train",
                "Cableway",
                "Ferry",
                "HailedSharedTaxi",
            ],
            "shorttermchanges": True,
            "stopid": stop_id,
            "time": now.strftime("%Y-%m-%dT%H:%M:%S.000Z"),
        }

        try:
            async with session.get(self.API_URL, json=request) as resp:
                if resp.status != 200:
                    raise hintlib.cache.RequestError(
                        "Unexpected HTTP response: {} {}".format(resp.status,
                                                                 resp.reason),
                        back_off=True,
                        cache_entry=expired_cache_entry,
                        use_context=False,
                    )
                data = json.loads(await resp.read())
        except asyncio.TimeoutError as exc:
            raise hintlib.cache.RequestError(
                "Timeout during request",
                back_off=True,
                cache_entry=expired_cache_entry,
            ) from exc

        if data.get("Status", {}).get("Code") == "ServiceError":
            raise hintlib.cache.RequestError(
                "service error returned",
                back_off=True,
                cache_entry=expired_cache_entry,
            )

        cache_entry = expired_cache_entry or hintlib.cache.CacheEntry()
        try:
            cache_entry.data = [
                DepartureModelRow(
                    lane=row["LineName"],
                    destination=row["Direction"],
                    timestamp=now,
                    eta=parse_dvb_date(row.get("RealTime",
                                               row["ScheduledTime"])),
                ) for row in data["Departures"]
            ]
        except (ValueError, KeyError, TypeError) as exc:
            self.logger.error("failed to parse response: %r", data)
            raise hintlib.cache.RequestError(
                "Failed to parse response",
                back_off=True,
                cache_entry=expired_cache_entry,
            ) from exc
        cache_entry.expires = datetime.utcnow() + self.CACHE_TTL
        cache_entry.last_modified = None
        return cache_entry


def _pattern_filter(cfg):
    match_return_value = cfg.get("action", "keep") != "drop"
    pattern_rx = re.compile(cfg["pattern"], re.I)
    field = cfg["match"]

    def filter_func(item):
        if pattern_rx.match(getattr(item, field)):
            return match_return_value
        return not match_return_value

    return filter_func


FILTER_TYPES = {
    "pattern": _pattern_filter,
}


def compile_filter(filter_cfg):
    return FILTER_TYPES[filter_cfg["type"]](filter_cfg)


def compile_stop_cfg(stop_cfg):
    stop_id = stop_cfg["id"]
    offset = stop_cfg.get("offset", 0)
    filters = stop_cfg.get("filters", [])

    if not isinstance(stop_id, str):
        raise TypeError("stop id must be str")
    if not isinstance(offset, (int, float)):
        raise TypeError("stop offset must be numeric")
    if not isinstance(filters, list):
        raise TypeError("stop offset must be an array")

    offset = timedelta(seconds=offset)

    filter_parts = []
    for filter_ in filters:
        filter_parts.append(compile_filter(filter_))

    if len(filter_parts) > 1:
        def filter_func(item):
            for part in filter_parts:
                if not part(item):
                    return False
            return True
    elif len(filter_parts) == 1:
        filter_func = filter_parts[0]
    else:
        def filter_func(item):
            return True

    return StopConfig(
        id_=stop_id,
        offset=offset,
        filter_func=filter_func,
    )


def compile_label_rule(rule):
    match_func = compile_filter(rule)
    ok_result = (rule["label"],)
    nok_result = ()

    def label_func(item):
        if match_func(item):
            return ok_result
        return nok_result

    return label_func


def compile_label_cfg(label_cfg):
    label_parts = []
    for rule in label_cfg:
        label_parts.append(compile_label_rule(rule))

    def label_func(item):
        old_labels = item.labels or frozenset()
        labels = set()
        for f in label_parts:
            labels.update(f(item))
        return item._replace(labels=old_labels | labels)

    return label_func


def compile_colourize_rule(rule):
    colour = rule["colour"]
    lane = rule.get("lane", None)
    label = rule.get("label", None)

    def colourize_func(item):
        if lane is not None and item.lane != lane:
            return None
        if label is not None and (not item.labels or label not in item.labels):
            return None
        return colour

    return colourize_func


def compile_colourize_cfg(colourize_cfg):
    rules = []
    for rule in colourize_cfg:
        rules.append(compile_colourize_rule(rule))

    def colourize_func(item):
        for rule in rules:
            colour = rule(item)
            if colour is not None:
                return colour
        return None

    return colourize_func


class DepartureService:
    def __init__(self):
        self.logger = logging.getLogger(
            ".".join([__name__, type(self).__qualname__])
        )
        self.screen = DepartureScreen()
        self.active_poll_interval = timedelta(seconds=30)
        self.inactive_poll_interval = timedelta(seconds=120)
        self._stops = []
        self._label_func = lambda x: x
        self._colourize_func = lambda x: x
        self.requester = DVBRequester()

        self._wakeup_event = asyncio.Event()
        self._worker_task = RestartingTask(self._worker)
        self._worker_task.start()

        self._handle_screen_deactivated()
        self.screen.on_activate.connect(self._handle_screen_activated)
        self.screen.on_deactivate.connect(self._handle_screen_deactivated)

    def _handle_screen_activated(self):
        self.logger.debug("screen is active: refreshing immediately and "
                          "enabling short polling mode")
        self._poll_interval = self.active_poll_interval
        self._wakeup_event.set()

    def _handle_screen_deactivated(self):
        self.logger.debug("screen is inactive: enabling long polling mode")
        self._poll_interval = self.inactive_poll_interval

    def _process_stop_row(self, stop_cfg, row):
        row = self._label_func(row)._replace(
            eta=row.eta + stop_cfg.offset,
        )
        colour = self._colourize_func(row)
        if colour is not None:
            row = row._replace(colour=colour)
        return row

    async def _poll(self):
        all_data = []
        parts = await asyncio.gather()
        for stop_cfg in self._stops:
            try:
                raw_data = await self.requester.request(stop_id=stop_cfg.id_)
            except hintlib.cache.BackingOff:
                self.logger.warning("ignoring stop %r, still backing off and "
                                    "no cached data",
                                    stop_cfg.id_)
                continue
            except hintlib.cache.RequestError as exc:
                self.logger.warning("could not request data for stop %r: %s",
                                    stop_cfg.id_,
                                    exc)
                continue
            all_data.extend(map(
                functools.partial(self._process_stop_row, stop_cfg),
                filter(stop_cfg.filter_func, raw_data)
            ))

        all_data.sort(key=lambda x: x.eta)
        self.screen.data = all_data

    def configure(self, departure_cfg):
        stops = list(map(compile_stop_cfg, departure_cfg["stops"]))
        label_func = compile_label_cfg(departure_cfg["annotate"]["label"])
        colourize_func = compile_colourize_cfg(
            departure_cfg["colourize"]["rule"]
        )

        self._stops = stops
        self._label_func = label_func
        self._colourize_func = colourize_func

    async def _worker(self):
        while True:
            try:
                await self._poll()
            finally:
                # always force a repaint, no matter the success
                self.screen.invalidate()

            interval = self._poll_interval

            self.logger.debug("running next poll in %s", interval)
            self._wakeup_event.clear()
            try:
                await asyncio.wait_for(
                    self._wakeup_event.wait(),
                    interval.total_seconds()
                )
            except asyncio.TimeoutError:
                # this just means that we didn’t get an external wakeup
                pass
