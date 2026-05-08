import asyncio
import functools
import json
import logging
import math
import re
import time
import typing

from datetime import timedelta, datetime, UTC

import aiohttp

import hintlib.cache
import hintlib.xso

from hintlib.services import PeerLockService, RestartingTask

from .ui import Screen, metrics

from hintd.cconstants import (
    LPCFont,
    rgb24_to_rgb16,
    LPCTableAlignment,
    TableColumnEx,
)


def luminance(r, g, b):
    return (
        (r/255) * 0.2126 +
        (g/255) * 0.7152 +
        (b/255) * 0.0722
    )


def get_text_colour(r, g, b):
    if luminance(r, g, b) < 0.69:
        return (255, 255, 255)
    else:
        return (0, 0, 0)


def nice_num(range_: float, rounded: bool) -> float:
    exponent = math.floor(math.log10(range_))
    next_pot = 10**exponent
    fraction = range_ / next_pot

    rounded_fraction = None
    if rounded:
        if fraction < 1.5:
            rounded_fraction = 1
        elif fraction < 3:
            rounded_fraction = 2
        elif fraction < 7:
            rounded_fraction = 5
        else:
            rounded_fraction = 10
    else:
        if fraction <= 1:
            rounded_fraction = 1
        elif fraction <= 2:
            rounded_fraction = 2
        elif fraction <= 5:
            rounded_fraction = 5
        else:
            rounded_fraction = 10

    return rounded_fraction * next_pot


def nice_timedelta(seconds: float) -> str:
    if seconds < 60.0:
        return f"{seconds:.0f}s"
    elif seconds < 3600.0:
        return f"{seconds/60:.0f}m"
    elif seconds < 86400.0:
        return f"{seconds/3600:.0f}h"
    else:
        return f"{seconds/86400:.0f}d"


class InfluxScreen(Screen):
    ROW_HEIGHT = 14
    DATE_COLUMN_WIDTH = 56
    DATA_COLUMN_WIDTH = 56

    def __init__(self):
        super().__init__(
            "Graph",
            "InfluxDB graph",
        )
        self.data = {}
        self.default_colour = metrics.THEME_CLIENT_AREA_BACKGROUND_COLOUR
        self.thresholds = []

    def make_axis(self, vmin, vmax, nticks):
        # roughly taken from https://stackoverflow.com/a/16363437/1248008
        range_ = nice_num(vmax - vmin, False)
        digits = math.log10(range_)
        if digits < 0:
            digits = math.ceil(-digits)
        else:
            digits = 0
        tick_offset = nice_num(range_ / (nticks - 1), True)
        base = math.floor(vmin / tick_offset) * tick_offset
        return [
            round(base + tick_offset * i, digits)
            for i in range(nticks + 1)
        ]

    @staticmethod
    def value_to_coord(graph_y0, graph_y1, vmin, vmax, v):
        return round(graph_y1 - (v - vmin) / (vmax - vmin) *
                     (graph_y1 - graph_y0))

    def paint(self):
        if not self.data or not self.data["rows"]:
            return

        print(self.data)

        graph_x0 = metrics.SCREEN_CLIENT_AREA_LEFT + 32
        graph_y0 = metrics.SCREEN_CLIENT_AREA_TOP + 16
        graph_x1 = metrics.SCREEN_CLIENT_AREA_RIGHT - 2
        graph_y1 = metrics.SCREEN_CLIENT_AREA_BOTTOM - 20
        graph_h = graph_y1 - graph_y0
        rows = self.data["rows"]
        nrows = len(rows)

        d_x = (graph_x1 - graph_x0) / (nrows - 1)

        lines = {}
        tmin = min((dt for (dt, *_) in rows))
        tmax = max((dt for (dt, *_) in rows))
        d_ts = (tmax - tmin).total_seconds() / (nrows - 1)

        vmin = min(v for _, *vs in rows for v in vs if v is not None)
        vmin = 0
        vmax = max(v for _, *vs in rows for v in vs if v is not None)

        for dt, *vals in rows:
            for i, ((name, colour), v) in enumerate(zip(self.data["columns"], vals)):
                lines.setdefault(i, (colour, []))[1].append(((dt - tmin).total_seconds() / d_ts, v))

        # and now some ticks
        ticks = self.make_axis(vmin, vmax, 5)
        # update range for nicer looking data
        vmin, *_, vmax = ticks

        # lets draw the configured thresholds first, so that any off by one is covered up by the axis drawing :-X
        for cutoff, colour in self.thresholds:
            if vmin < cutoff < vmax:
                y = self.value_to_coord(graph_y0, graph_y1, vmin, vmax, cutoff)
                self._ui.draw_line(
                    graph_x0, y,
                    graph_x1, y,
                    colour,
                )

        # now the axes
        self._ui.draw_line(
            graph_x0, graph_y1,
            graph_x0, graph_y0,
            metrics.THEME_CLIENT_AREA_COLOUR,
        )

        self._ui.draw_line(
            graph_x0, graph_y1,
            graph_x1, graph_y1,
            metrics.THEME_CLIENT_AREA_COLOUR,
        )

        for tick in ticks:
            y = self.value_to_coord(graph_y0, graph_y1, vmin, vmax, tick)
            self._ui.draw_line(
                graph_x0 - 2, y,
                graph_x0, y,
                metrics.THEME_CLIENT_AREA_COLOUR,
            )
            self._ui.draw_text(
                metrics.SCREEN_CLIENT_AREA_LEFT + 2,
                y + 5,
                LPCFont.DEJAVU_SANS_9PX,
                metrics.THEME_CLIENT_AREA_COLOUR,
                str(tick),
            )

        for (colour, line) in lines.values():
            prev_x, prev_y = None, None
            for i, value in line:
                if value is None:
                    prev_x = None
                    prev_y = None
                    continue
                x = round(i * d_x + graph_x0)
                y = self.value_to_coord(graph_y0, graph_y1, vmin, vmax, value)
                if prev_x is not None and prev_y is not None:
                    self._ui.draw_line(
                        prev_x, prev_y,
                        x, y,
                        colour,
                    )
                prev_x, prev_y = x, y

        ntimeticks = 6
        d_x_tick = (graph_x1 - graph_x0) / ntimeticks
        d_ts_tick = (tmax - tmin).total_seconds() / ntimeticks

        self._ui.table_start(
            graph_x0,
            graph_y1 + 14,
            10,
            [
                (round(d_x_tick * i - round(d_x_tick * (i-1))), LPCTableAlignment.LEFT)
                for i in range(ntimeticks)
            ]
        )

        self._ui.table_row(
            LPCFont.DEJAVU_SANS_9PX,
            metrics.THEME_CLIENT_AREA_COLOUR,
            metrics.THEME_CLIENT_AREA_BACKGROUND_COLOUR,
            [
                f"-{nice_timedelta(d_ts_tick * (ntimeticks+1 - i))}"
                for i in range(1, ntimeticks+1)
            ]
        )

        # and now for the date ticks, which are slightly worse

        for i in range(6):
            x = round(d_x_tick * i) + graph_x0
            self._ui.draw_line(
                x, graph_y1,
                x, graph_y1 + 2,
                metrics.THEME_CLIENT_AREA_COLOUR,
            )

        self._ui.draw_line(
            graph_x1, graph_y1,
            graph_x1, graph_y1 + 2,
            metrics.THEME_CLIENT_AREA_COLOUR,
        )

        self._ui.draw_text(
            graph_x1 - 8,
            graph_y1 + 14,
            LPCFont.DEJAVU_SANS_9PX,
            metrics.THEME_CLIENT_AREA_COLOUR,
            "0"
        )



class InfluxRequester(hintlib.cache.AdvancedHTTPRequester):
    CACHE_TTL = timedelta(minutes=15)

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
                                    api_url,
                                    db,
                                    query,
                                    auth):
        now = datetime.utcnow()

        try:
            async with session.get(
                    api_url,
                    params={
                        "q": query,
                        "db": db,
                    },
                    auth=auth,
                    ) as resp:
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

        cache_entry = expired_cache_entry or hintlib.cache.CacheEntry()
        try:
            cache_entry.data = data
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


class InfluxService:
    PALETTE = [
        (115, 191, 105),
        (87, 148, 242),
        (255, 152, 48),
    ]

    def __init__(self):
        self.logger = logging.getLogger(
            ".".join([__name__, type(self).__qualname__])
        )
        self.screen = InfluxScreen()
        self.active_poll_interval = timedelta(minutes=5)
        self.inactive_poll_interval = timedelta(minutes=30)
        self._stops = []
        self._label_func = lambda x: x
        self._colourize_func = lambda x: x
        self.requester = InfluxRequester()

        self._wakeup_event = asyncio.Event()
        self._worker_task = RestartingTask(self._worker)
        self._worker_task.start()

        self._query = None
        self._api_url = None
        self._db = None
        self._auth = None
        self._trim_front = None
        self._trim_back = None

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

    async def _poll(self):
        if self._db is None or self._api_url is None or self._query is None:
            self.logger.warn("not requesting influxdb data, because configuration is incomplete")
            return

        series = (await self.requester.request(
            query=self._query,
            db=self._db,
            api_url=self._api_url,
            auth=self._auth,
        ))["results"][0]["series"]

        rows = {}
        columns = []
        ncolumns = len(series)
        for i, column in enumerate(series):
            label = column["tags"]["instance"]  # FIXME: allow other keys
            colour = self.PALETTE[i % len(self.PALETTE)]
            columns.append((label, colour))
            for (time, value) in column["values"]:
                time = datetime.strptime(
                    time,
                    "%Y-%m-%dT%H:%M:%SZ",
                ).replace(tzinfo=UTC)
                rows.setdefault(time, [None]*ncolumns)[i] = value

        rows = sorted(
            (
                (time,)+tuple(values)
                for time, values in rows.items()
            ),
            key=lambda x: x[0],
        )
        if self._trim_back is not None:
            for i in range(self._trim_back):
                rows[i] = (rows[i][0],) + (None,)*ncolumns
        if self._trim_front is not None:
            for i in range(len(rows) - self._trim_front, len(rows)):
                rows[i] = (rows[i][0],) + (None,)*ncolumns

        self.screen.data = {
            "columns": columns,
            "rows": rows,
        }

    def configure(self, influx_cfg):
        self.screen.tab_caption = influx_cfg["caption"]
        self.screen.title = influx_cfg["title"]
        self._trim_front = influx_cfg.get("trim_front")
        self._trim_back = influx_cfg.get("trim_back")
        self._api_url = influx_cfg["api_url"]
        self._db = influx_cfg["db"]
        self._query = influx_cfg["query"]
        try:
            username = influx_cfg["username"]
            password = influx_cfg["password"]
        except KeyError:
            self.logger.info("not using HTTP auth with influxdb source")
            self._auth = None
        else:
            self._auth = aiohttp.BasicAuth(username, password)

        self.screen.default_colour = (255, 255, 255)
        self.screen.thresholds = []

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
