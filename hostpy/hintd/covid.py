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

    def paint(self):
        if not self.data:
            return

        ntotalrows = metrics.SCREEN_CLIENT_AREA_HEIGHT // self.ROW_HEIGHT
        ndatarows = len(self.data["rows"])
        ncolumns = len(self.data["columns"])
        table_height = ntotalrows * self.ROW_HEIGHT
        table_width = ncolumns * self.DATA_COLUMN_WIDTH + self.DATE_COLUMN_WIDTH
        x0 = (metrics.SCREEN_CLIENT_AREA_WIDTH - table_width) // 2
        y0 = (metrics.SCREEN_CLIENT_AREA_HEIGHT - table_height) // 2

        self._ui.table_start(
            x0,
            y0+self.ROW_HEIGHT,
            self.ROW_HEIGHT,
            [
                (self.DATE_COLUMN_WIDTH, LPCTableAlignment.LEFT)
            ] + [
                (self.DATA_COLUMN_WIDTH, LPCTableAlignment.LEFT)
            ] * ncolumns
        )

        self._ui.table_row(
            LPCFont.DEJAVU_SANS_12PX_BF,
            metrics.THEME_CLIENT_AREA_COLOUR,
            metrics.THEME_CLIENT_AREA_BACKGROUND_COLOUR,
            [
                "Datum",
            ] + self.data["columns"]
        )

        for dt, *vals in sorted(self.data["rows"], key=lambda x: x[0], reverse=True):
            columns = [
                TableColumnEx(
                    bgcolour=metrics.THEME_CLIENT_AREA_BACKGROUND_COLOUR,
                    fgcolour=metrics.THEME_CLIENT_AREA_COLOUR,
                    text=dt.strftime("%d %b"),
                    alignment=LPCTableAlignment.LEFT,
                )
            ]

            for value in vals:
                colour = self.default_colour
                for thresh_cutoff, thresh_colour in self.thresholds:
                    if value >= thresh_cutoff:
                        colour = thresh_colour

                if value < 100:
                    text = "{:.2g}".format(value)
                else:
                    text = "{:.0f}".format(value)

                columns.append(
                    TableColumnEx(
                        bgcolour=colour,
                        fgcolour=get_text_colour(*colour),
                        text=text,
                        alignment=LPCTableAlignment.RIGHT,
                    )
                )

            self._ui.table_row_ex(
                LPCFont.DEJAVU_SANS_12PX,
                columns,
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
                for _, q in self._columns
            )
        )

        rows = []
        for col_pairs in zip(*all_data):
            date = col_pairs[0][0]
            row = [date]
            row.extend(v for _, v in col_pairs)
            rows.append(tuple(row))

        self.screen.data = {
            "columns": [label for label, _ in self._columns],
            "rows": rows,
        }

    def configure(self, covid_cfg):
        self._columns = [
            (col["label"], col.get("q"))
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
