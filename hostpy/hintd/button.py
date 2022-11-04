import asyncio
import dataclasses
import functools
import json
import logging
import math
import re
import time
import typing

from datetime import timedelta, datetime

import aiohttp

import aioxmpp

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


class ButtonScreen(Screen):
    GROUP_MARGIN = 4
    BUTTON_SPACING = 2
    BUTTONS_PER_ROW = 2
    GROUP_HEADER_HEIGHT = 12

    def __init__(self, queue):
        super().__init__(
            "Hey!",
            "Getting some Aufmerksamkeit"
        )
        self.queue = queue
        self.button_groups = []
        self._active_button = None

    @property
    def _group_h(self):
        return (
            metrics.SCREEN_CLIENT_AREA_HEIGHT -
            self.GROUP_MARGIN * (len(self.button_groups) + 1)
        ) // len(self.button_groups)

    @property
    def _inner_width(self):
        return metrics.SCREEN_CLIENT_AREA_WIDTH - self.GROUP_MARGIN * 2

    @property
    def _button_width(self):
        return self._inner_width // self.BUTTONS_PER_ROW

    def _button_height(self, nbuttons):
        nrows = (nbuttons + self.BUTTONS_PER_ROW - 1) // self.BUTTONS_PER_ROW
        return (self._group_h - self.GROUP_HEADER_HEIGHT - self.BUTTON_SPACING * (nrows - 1)) // nrows

    def _paint_group(self, x0, y0, x1, y1, group):
        # 2xN always
        width = x1 - x0
        height = y1 - y0

        self._ui.draw_text(
            x0, y0 + 9,
            LPCFont.DEJAVU_SANS_9PX,
            metrics.THEME_CLIENT_AREA_COLOUR,
            group.label,
        )

        button_width = self._button_width
        nrows = (len(group.buttons) + self.BUTTONS_PER_ROW - 1) // self.BUTTONS_PER_ROW
        button_height = self._button_height(len(group.buttons))
        row_height = button_height // 3

        y0 += self.GROUP_HEADER_HEIGHT

        self._ui.table_start(
            x0, y0 + row_height,
            row_height,
            [
                (button_width, LPCTableAlignment.CENTER)
            ] * self.BUTTONS_PER_ROW
        )

        for row in range(nrows):
            columns = [""] * self.BUTTONS_PER_ROW
            for col in range(self.BUTTONS_PER_ROW):
                i = col + row * self.BUTTONS_PER_ROW
                if i >= len(group.buttons):
                    break
                columns[col] = group.buttons[i].label
            self._ui.table_row(
                LPCFont.DEJAVU_SANS_12PX,
                metrics.THEME_CLIENT_AREA_COLOUR,
                metrics.THEME_CLIENT_AREA_BACKGROUND_COLOUR,
                [""] * self.BUTTONS_PER_ROW,
            )
            self._ui.table_row(
                LPCFont.DEJAVU_SANS_12PX,
                metrics.THEME_CLIENT_AREA_COLOUR,
                metrics.THEME_CLIENT_AREA_BACKGROUND_COLOUR,
                columns,
            )
            self._ui.table_row(
                LPCFont.DEJAVU_SANS_12PX,
                metrics.THEME_CLIENT_AREA_COLOUR,
                metrics.THEME_CLIENT_AREA_BACKGROUND_COLOUR,
                [""] * self.BUTTONS_PER_ROW,
            )

    def paint(self):
        x0 = metrics.SCREEN_CLIENT_AREA_LEFT + self.GROUP_MARGIN
        x1 = metrics.SCREEN_CLIENT_AREA_RIGHT - self.GROUP_MARGIN
        group_h = self._group_h
        for i, group in enumerate(self.button_groups):
            y0 = self.GROUP_MARGIN + (
                metrics.SCREEN_CLIENT_AREA_TOP + (
                    group_h + self.GROUP_MARGIN
                ) * i
            )
            y1 = y0 + group_h
            self._paint_group(x0, y0, x1, y1, group)

    def _hit(self, x, y):
        x -= self.GROUP_MARGIN
        y -= self.GROUP_MARGIN
        if x < 0 or y < 0:
            return None
        ngroups = len(self.button_groups)
        group_h = self._group_h
        group_index, y = divmod(y, group_h + self.GROUP_MARGIN)
        if y >= group_h or group_index >= ngroups:
            # hit the margin, ignore
            return None
        y -= self.GROUP_HEADER_HEIGHT
        group = self.button_groups[group_index]
        button_width = self._button_width
        column = x // button_width
        if column >= self.BUTTONS_PER_ROW:
            # hit the margin, ignore
            return None
        nbuttons = len(group.buttons)
        button_height = self._button_height(nbuttons)
        row = y // button_height
        index = row * self.BUTTONS_PER_ROW + column
        if index >= nbuttons:
            return None

        x0 = column * button_width + self.GROUP_MARGIN
        x1 = x0 + button_width
        y0 = row * button_height + self.GROUP_HEADER_HEIGHT + (group_h + self.GROUP_MARGIN) * group_index + self.GROUP_MARGIN
        y1 = y0 + button_height

        return group, index, (x0, y0, x1, y1), group.buttons[index]

    def touch_down(self, x, y, z):
        button = self._hit(x, y)
        if button is None:
            self._active_button = None
            return
        _, _, (x0, y0, x1, y1), button = button
        print("touch_down hit", x, y)
        self._active_button = button
        self._ui.draw_rect(
            x0, y0,
            x1, y1,
            metrics.THEME_CLIENT_AREA_COLOUR,
        )

    def touch_up(self, x, y, z):
        prev_button = self._active_button
        self._active_button = None
        button = self._hit(x, y)
        if button is None:
            print("touch_up no hit", x, y)
            return
        group, _, (x0, y0, x1, y1), button = button
        if button is prev_button:
            print("touch_up match")
            self._ui.draw_rect(
                x0, y0,
                x1, y1,
                metrics.THEME_CLIENT_AREA_BACKGROUND_COLOUR,
            )
            self.queue.put_nowait((group.address, button.message))
        else:
            print("touch_up mismatch")
            # force full repaint
            self.invalidate()


@dataclasses.dataclass(frozen=True)
class Button:
    label: str
    message: str


@dataclasses.dataclass(frozen=True)
class ButtonGroup:
    label: str
    address: aioxmpp.JID
    buttons: typing.Sequence[Button]


class ButtonService:
    def __init__(self, parent_xmpp):
        self.logger = logging.getLogger(
            ".".join([__name__, type(self).__qualname__])
        )
        self.queue = asyncio.Queue()
        self.screen = ButtonScreen(self.queue)

        self._parent_xmpp = parent_xmpp

        self._wakeup_event = asyncio.Event()
        self._worker_task = RestartingTask(self._worker)
        self._worker_task.start()

    async def _poll(self):
        self.logger.debug("polling!", stack_info=True)
        self.screen.data = await self.requester.request()

    def configure(self, button_cfg):
        button_groups = []
        for recipient, reci_cfg in button_cfg["recipient"].items():
            recipient = aioxmpp.JID.fromstr(recipient)
            name = reci_cfg.get("name", recipient.localpart)
            button_groups.append(
                ButtonGroup(
                    label=name,
                    address=recipient,
                    buttons=[
                        Button(
                            message=item["message"],
                            label=item.get("label", item["message"]),
                        )
                        for item in reci_cfg["buttons"]
                    ]
                )
            )

        self.screen.button_groups = button_groups

        try:
            xmpp_cfg = button_cfg["xmpp"]
        except KeyError:
            self._custom_client = None
            return
        self._custom_client = hintlib.core.BotCore(
            xmpp_cfg,
            client_logger=self.logger.getChild("client"),
        )

    async def _send_messages(self, client):
        while True:
            recipient, text = await self.queue.get()
            print("SENDING MESSAGE", recipient, text)
            msg = aioxmpp.Message(to=recipient, type_=aioxmpp.MessageType.CHAT)
            msg.body[None] = text
            await client.send(msg)

    async def _worker(self):
        if self._custom_client is not None:
            async with self._custom_client:
                await self._send_messages(self._custom_client.client)
        else:
            await self._send_messages(self._parent_xmpp.client)
