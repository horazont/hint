import asyncio
import functools
import json
import logging
import math
import re
import time
import typing

from datetime import timedelta, datetime

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


class CovidScreen(Screen):
    ROW_HEIGHT = 14
    DATE_COLUMN_WIDTH = 56
    DATA_COLUMN_WIDTH = 56

    def __init__(self):
        super().__init__(
            "Covid",
            "COVID-19 Inzidenzen"
        )
        self.data = {}
        self.default_colour = metrics.THEME_CLIENT_AREA_BACKGROUND_COLOUR
        self.thresholds = []

    def make_axis(self, vmin, vmax, nticks):
        # roughly taken from https://stackoverflow.com/a/16363437/1248008
        range_ = nice_num(vmax - vmin, False)
        tick_offset = nice_num(range_ / (nticks - 1), True)
        base = math.floor(vmin / tick_offset) * tick_offset
        return [
            base + tick_offset * i
            for i in range(nticks + 1)
        ]

    @staticmethod
    def value_to_coord(graph_y0, graph_y1, vmin, vmax, v):
        return round(graph_y1 - (v - vmin) / (vmax - vmin) *
                     (graph_y1 - graph_y0))

    def paint(self):
        if not self.data:
            return

        graph_x0 = metrics.SCREEN_CLIENT_AREA_LEFT + 32
        graph_y0 = metrics.SCREEN_CLIENT_AREA_TOP + 16
        graph_x1 = metrics.SCREEN_CLIENT_AREA_RIGHT - 2
        graph_y1 = metrics.SCREEN_CLIENT_AREA_BOTTOM - 20
        graph_h = graph_y1 - graph_y0
        rows = self.data["rows"]
        nrows = len(rows)

        day_width = (graph_x1 - graph_x0) / (nrows - 1)

        lines = {}
        dt0 = min((dt for (dt, *_) in rows))

        vmin = min(v for _, *vs in rows for v in vs)
        vmin = 0
        vmax = max(v for _, *vs in rows for v in vs)

        for dt, *vals in rows:
            for i, ((name, colour), v) in enumerate(zip(self.data["columns"], vals)):
                lines.setdefault(i, (colour, []))[1].append(((dt - dt0).total_seconds() / 86400, v))

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
            for day, value in line:
                x = round(day * day_width + graph_x0)
                y = self.value_to_coord(graph_y0, graph_y1, vmin, vmax, value)
                if prev_x is not None and prev_y is not None:
                    self._ui.draw_line(
                        prev_x, prev_y,
                        x, y,
                        colour,
                    )
                prev_x, prev_y = x, y

        date_subdivision = 4

        self._ui.table_start(
            graph_x0,
            graph_y1 + 14,
            10,
            [
                (round(day_width * i * date_subdivision - round(day_width * (i-1) * date_subdivision)), LPCTableAlignment.LEFT)
                for i in range(nrows // date_subdivision)
            ]
        )

        self._ui.table_row(
            LPCFont.DEJAVU_SANS_9PX,
            metrics.THEME_CLIENT_AREA_COLOUR,
            metrics.THEME_CLIENT_AREA_BACKGROUND_COLOUR,
            [
                dt.strftime("%d.%m.")
                for dt, *_ in rows[::date_subdivision]
            ]
        )

        # and now for the date ticks, which are slightly worse

        for i in range(nrows // date_subdivision):
            x = round(day_width * i * date_subdivision) + graph_x0
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
            graph_x1 - 28,
            graph_y1 + 14,
            LPCFont.DEJAVU_SANS_9PX,
            metrics.THEME_CLIENT_AREA_COLOUR,
            rows[-1][0].strftime("%d.%m.")
        )



class DoofesCovidRequester(hintlib.cache.AdvancedHTTPRequester):
    API_URL = "https://doofescovid.de/api/datasources/proxy/10/query"
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
                                    query):
        now = datetime.utcnow()

        try:
            async with session.get(
                    self.API_URL,
                    params={
                        "q": query,
                        "epoch": "ms",
                        "db": "covid",
                    }) as resp:
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
            cache_entry.data = [
                (datetime.utcfromtimestamp(ts / 1000), v)
                for ts, v in data["results"][0]["series"][0]["values"]
                if v is not None
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


class CovidService:
    QUERY_TEMPLATE = 'SELECT sum("d7pubcases") / sum("population") * 100000 FROM "rki_data_v1_geo" WHERE {extra_where} time >= now() - 15d GROUP BY time(1d) fill(null)'

    def __init__(self):
        self.logger = logging.getLogger(
            ".".join([__name__, type(self).__qualname__])
        )
        self.screen = CovidScreen()
        self.active_poll_interval = timedelta(hours=1)
        self.inactive_poll_interval = timedelta(hours=2)
        self._stops = []
        self._label_func = lambda x: x
        self._colourize_func = lambda x: x
        self.requester = DoofesCovidRequester()

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

    async def _poll(self):
        all_data = await asyncio.gather(
            *(
                self.requester.request(
                    query=self.QUERY_TEMPLATE.format(
                        extra_where="({}) AND".format(q) if q is not None else "",
                    ),
                )
                for *_, q in self._columns
            )
        )

        rows = []
        for col_pairs in zip(*all_data):
            date = col_pairs[0][0]
            row = [date]
            row.extend(v for _, v in col_pairs)
            rows.append(tuple(row))

        self.screen.data = {
            "columns": [(label, colour) for label, colour, _ in self._columns],
            "rows": sorted(rows, key=lambda x: x[0]),
        }

    def configure(self, covid_cfg):
        self._columns = [
            (col["label"], col["colour"], col.get("q"))
            for col in covid_cfg["columns"]
        ]
        self.screen.default_colour = covid_cfg.get("default_colour", (255, 255, 255))
        self.screen.thresholds = sorted((
            (thresh["value"], thresh["colour"])
            for thresh in covid_cfg.get("thresholds", [])
        ), key=lambda x: x[0], reverse=True)

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
