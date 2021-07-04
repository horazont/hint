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
    ROW_HEIGHT = 24
    DATE_COLUMN_WIDTH = 56
    DATA_COLUMN_WIDTH = 56

    def __init__(self):
        super().__init__(
            "Shia",
            "Time since last Fauch"
        )
        self.data = None

    def paint(self):
        if not self.data:
            return

        x0 = 0
        y0 = 0
        table_width = metrics.SCREEN_CLIENT_AREA_WIDTH

        self._ui.table_start(
            x0,
            y0+self.ROW_HEIGHT,
            self.ROW_HEIGHT,
            [
                (table_width, LPCTableAlignment.CENTER)
            ]
        )

        ndays = math.floor(
            (datetime.utcnow() - self.data).total_seconds() / 86400
        )

        self._ui.table_row(
            LPCFont.DEJAVU_SANS_12PX_BF,
            metrics.THEME_CLIENT_AREA_COLOUR,
            metrics.THEME_CLIENT_AREA_BACKGROUND_COLOUR,
            [
                "Seit dem letzten Fauchen ist"
                if ndays == 1 else
                "Seit dem letzten Fauchen sind"
            ]
        )

        self._ui.table_row(
            LPCFont.CANTARELL_20PX_BF,
            metrics.THEME_CLIENT_AREA_COLOUR,
            metrics.THEME_CLIENT_AREA_BACKGROUND_COLOUR,
            [
                "{}".format(ndays)
            ]
        )

        self._ui.table_row(
            LPCFont.DEJAVU_SANS_12PX_BF,
            metrics.THEME_CLIENT_AREA_COLOUR,
            metrics.THEME_CLIENT_AREA_BACKGROUND_COLOUR,
            [
                "Tage vergangen" if ndays != 1 else "Tag vergangen",
            ]
        )


class ShiaReqeuster(hintlib.cache.AdvancedHTTPRequester):
    API_URL = "https://sotecware.net/files/noindex/shia.date.txt"
    CACHE_TTL = timedelta(hours=1)

    def _create_session(self):
        return aiohttp.ClientSession(
            timeout=aiohttp.ClientTimeout(total=15)
        )

    def _get_backing_off_result(self, expired_cache_entry=None, **kwargs):
        return expired_cache_entry

    async def _perform_http_request(self,
                                    session,
                                    expired_cache_entry=None):
        now = datetime.utcnow()

        try:
            async with session.get(self.API_URL) as resp:
                if resp.status != 200:
                    raise hintlib.cache.RequestError(
                        "Unexpected HTTP response: {} {}".format(resp.status,
                                                                 resp.reason),
                        back_off=True,
                        cache_entry=expired_cache_entry,
                        use_context=False,
                    )
                data = datetime.strptime((await resp.read()).decode("ascii").strip(), "%Y-%m-%dT%H:%M:%S")
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


class ShiaService:
    def __init__(self):
        self.logger = logging.getLogger(
            ".".join([__name__, type(self).__qualname__])
        )
        self.screen = CovidScreen()
        self.requester = ShiaReqeuster()

        self._wakeup_event = asyncio.Event()
        self._worker_task = RestartingTask(self._worker)
        self._worker_task.start()

        self._handle_screen_deactivated()
        self.screen.on_activate.connect(self._handle_screen_activated)
        self.screen.on_deactivate.connect(self._handle_screen_deactivated)

    def _handle_screen_activated(self):
        self.logger.debug("screen is active: refreshing immediately and "
                          "enabling short polling mode")
        self._wakeup_event.set()

    def _handle_screen_deactivated(self):
        self.logger.debug("screen is inactive: enabling long polling mode")

    async def _poll(self):
        self.logger.debug("polling!", stack_info=True)
        self.screen.data = await self.requester.request()

    def configure(self, shia_cfg):
        pass

    async def _worker(self):
        while True:
            try:
                await self._poll()
            finally:
                # always force a repaint, no matter the success
                self.screen.invalidate()

            self.logger.debug("waiting for wakeup signal")
            self._wakeup_event.clear()
            try:
                await self._wakeup_event.wait()
            except asyncio.TimeoutError:
                # this just means that we didn’t get an external wakeup
                pass
