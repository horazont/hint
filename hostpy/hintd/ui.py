import asyncio
import enum
import logging
import math
import typing

from datetime import datetime, timedelta

import aioxmpp.callbacks

import hintlib.services

from hintd.ccomm import ffi
from hintd.cconstants import (
    rgb24_to_rgb16,
    LPCCommand,
    Address,
    LPCFont,
    LPCTableAlignment,
    TableColumnEx,
)
from hintd.utils import logged_future


class metrics:
    LCD_WIDTH = 320
    LCD_HEIGHT = 240

    SCREEN_MARGIN_TOP = 0
    SCREEN_MARGIN_LEFT = 0
    SCREEN_MARGIN_RIGHT = 62
    SCREEN_MARGIN_BOTTOM = 0

    SCREEN_CLIENT_AREA_TOP = 0
    SCREEN_CLIENT_AREA_LEFT = 0
    SCREEN_CLIENT_AREA_RIGHT = ((LCD_WIDTH-1)-64)
    SCREEN_CLIENT_AREA_BOTTOM = (LCD_HEIGHT-1)

    SCREEN_CLIENT_AREA_WIDTH = (SCREEN_CLIENT_AREA_RIGHT-SCREEN_CLIENT_AREA_LEFT-1)
    SCREEN_CLIENT_AREA_HEIGHT = (SCREEN_CLIENT_AREA_BOTTOM-SCREEN_CLIENT_AREA_TOP-1)

    CLOCK_POSITION_X = ((LCD_WIDTH-1)-(SCREEN_MARGIN_RIGHT-2))
    CLOCK_POSITION_Y = 18
    CLOCK_HEIGHT = 26

    TAB_WIDTH = 60
    TAB_HEIGHT = 28
    TAB_PADDING = 4

    TABBAR_LEFT = ((LCD_WIDTH-1)-SCREEN_MARGIN_RIGHT)
    TABBAR_TOP = (SCREEN_CLIENT_AREA_TOP+CLOCK_HEIGHT)

    THEME_DEFAULT_COLOUR = 0, 0, 0
    THEME_DEFAULT_BACKGROUND_COLOUR = 255, 255, 255

    THEME_CLIENT_AREA_BACKGROUND_COLOUR = THEME_DEFAULT_BACKGROUND_COLOUR
    THEME_CLIENT_AREA_COLOUR = THEME_DEFAULT_COLOUR
    THEME_CLIENT_AREA_BORDER_COLOUR = THEME_DEFAULT_COLOUR

    THEME_TH_BACKGROUND_COLOUR = 255, 0, 255
    THEME_TH_COLOUR = THEME_DEFAULT_COLOUR

    THEME_BACKDROP_BACKGROUND_COLOUR = 156, 2, 2
    THEME_BACKDROP_COLOUR = 255, 255, 255

    THEME_H1_BACKGROUND_COLOUR = 255, 255, 255
    THEME_H1_COLOUR = 0, 0, 0
    THEME_H1_BORDER_COLOUR = 0, 0, 0

    THEME_TAB_BACKGROUND_COLOUR = 0, 0, 0
    THEME_TAB_BORDER_COLOUR = 191, 191, 191
    THEME_TAB_COLOUR = 255, 255, 255
    THEME_TAB_ACTIVE_BACKGROUND_COLOUR = 255, 255, 255
    THEME_TAB_ACTIVE_BORDER_COLOUR = 0, 0, 0
    THEME_TAB_ACTIVE_COLOUR = 0, 0, 0


class Screen:
    on_invalidate = aioxmpp.callbacks.Signal()
    on_activate = aioxmpp.callbacks.Signal()
    on_deactivate = aioxmpp.callbacks.Signal()

    def __init__(self, tab_caption, title):
        super().__init__()
        self.tab_caption = tab_caption
        self.title = title
        self.logger = logging.getLogger(
            ".".join([__name__, type(self).__qualname__])
        )
        self._ui = None

    def invalidate(self):
        self._invalidated = True
        self.on_invalidate(self)

    def touch_down(self, x, y, z):
        pass

    def touch_move(self, x, y, z):
        pass

    def touch_up(self, x, y, z):
        pass

    def activate(self, ui):
        self._ui = ui
        self.on_activate()
        self.invalidate()

    def deactivate(self):
        self.on_deactivate()
        del self._ui

    def sleep(self):
        pass

    def wakeup(self):
        pass

    def paint(self):
        pass


class DirtKind(enum.Enum):
    DISPLAY = 0
    TAB = 1
    CLOCK = 2
    CLIENT_AREA = 3
    SCREEN = 4


class DirtMarker(typing.NamedTuple):
    kind: DirtKind
    info: typing.Any = None

    @classmethod
    def display(cls):
        return cls(DirtKind.DISPLAY)

    @classmethod
    def clock(cls):
        return cls(DirtKind.CLOCK)

    @classmethod
    def client_area(cls):
        return cls(DirtKind.CLIENT_AREA)

    @classmethod
    def screen(cls, *args):
        return cls(DirtKind.SCREEN, ScreenInfo(*args))

    @classmethod
    def tab(cls, *args):
        return cls(DirtKind.TAB, TabInfo(*args))


class TabInfo(typing.NamedTuple):
    index: int


class ScreenInfo(typing.NamedTuple):
    index: int


class UI:
    SLEEP_TIMEOUT = timedelta(seconds=30)

    def __init__(self, main):
        super().__init__()
        self._loop = asyncio.get_event_loop()
        self.logger = logging.getLogger(
            ".".join([__name__, type(self).__qualname__])
        )
        self._screens = []
        self._dirt = {DirtMarker(DirtKind.DISPLAY)}
        self._main = main
        self._current_screen = None
        self._sleeping = True
        self._clock_task = hintlib.services.RestartingTask(self._clock_impl)
        self._sleep_handle = None

    def _draw_clock(self, t):
        colon = ":" if t.second % 2 == 0 else " "
        text = "{:02d}{}{:02d}".format(
            t.hour,
            colon,
            t.minute,
        )
        self.fill_rect(
            metrics.CLOCK_POSITION_X,
            0,
            metrics.LCD_WIDTH-1, metrics.CLOCK_POSITION_Y+2,
            metrics.THEME_BACKDROP_BACKGROUND_COLOUR,
        )
        self.draw_text(
            metrics.CLOCK_POSITION_X,
            metrics.CLOCK_POSITION_Y,
            LPCFont.CANTARELL_20PX_BF,
            metrics.THEME_BACKDROP_COLOUR,
            text,
        )

    async def _clock_impl(self):
        # for once, we don’t need utc :-O
        now = datetime.now()
        while True:
            next_second = now.replace(microsecond=0)+timedelta(seconds=1)
            tts = (next_second - now).total_seconds()
            if tts > 0:
                await asyncio.sleep(tts)
            now = datetime.now()
            self.invalidate(DirtMarker(DirtKind.CLOCK))

    def add_screen(self, screen):
        index = len(self._screens)
        self._screens.append(screen)
        screen.on_invalidate.connect(self._screen_invalidated)
        if self._current_screen is None:
            self._activate_screen(screen)
            self.invalidate(DirtMarker.screen(index))
        self.invalidate(DirtMarker.tab(index))

    def _screen_invalidated(self, screen):
        self.invalidate(DirtMarker.screen(self._screens.index(screen)))

    def _activate_screen(self, screen):
        if self._current_screen is not None:
            self.invalidate(DirtMarker.tab(
                self._screens.index(self._current_screen)
            ))
            if not not self._sleeping:
                self._current_screen.deactivate()
        self._current_screen = screen
        screen_index = self._screens.index(self._current_screen)
        self.invalidate(DirtMarker.tab(screen_index))
        self.invalidate(DirtMarker.client_area())
        if not self._sleeping:
            self._current_screen.activate(self)

    def _reset_sleep_timer(self):
        if self._sleep_handle is not None:
            self._sleep_handle.cancel()
        self._sleep_handle = self._loop.call_later(
            self.SLEEP_TIMEOUT.total_seconds(),
            self.sleep,
        )
        self.logger.debug("reset sleep timer")

    def _handle_tabbar_touch(self, x, y, z):
        tab_index = math.floor(y / metrics.TAB_HEIGHT)
        if tab_index < 0 or tab_index >= len(self._screens):
            return
        new_screen = self._screens[tab_index]
        if self._current_screen is not new_screen:
            self._activate_screen(new_screen)

    def touch_down(self, x, y, z):
        self.wakeup()
        if x >= metrics.TABBAR_LEFT and y >= metrics.TABBAR_TOP:
            self._handle_tabbar_touch(x - metrics.TABBAR_LEFT,
                                      y - metrics.TABBAR_TOP, z)

    def touch_move(self, x, y, z):
        self.wakeup()

    def touch_up(self, x, y, z):
        self.wakeup()

    def _invalidate_delayed(self):
        if not self._dirt or self._sleeping:
            return
        self.repaint()

    def invalidate(self, marker):
        self._dirt.add(marker)
        self._loop.call_soon(self._invalidate_delayed)

    def repaint(self):
        if self._sleeping:
            self._dirt.clear()
            return

        if not self._dirt:
            return

        all_dirty = DirtMarker.display() in self._dirt
        if all_dirty:
            self.fill_rect(
                0,
                0,
                (metrics.LCD_WIDTH-1),
                (metrics.LCD_HEIGHT-1),
                metrics.THEME_BACKDROP_BACKGROUND_COLOUR,
            )

        try:
            self._draw_screen_background()
            if self._current_screen is not None:
                index = self._screens.index(self._current_screen)
                if (all_dirty or
                        DirtMarker.client_area() in self._dirt or
                        DirtMarker.screen(index) in self._dirt):
                    self._current_screen.paint()
            self._draw_screen_clock()
            self._draw_screen_title()
        except:  # NOQA
            self.logger.exception("repaint failed")
            raise

        self._dirt.clear()

    def sleep(self):
        if self._sleeping:
            return
        self.logger.info("UI entering sleep state")
        self._sleeping = True
        self._clock_task.stop()
        if self._current_screen is not None:
            self._current_screen.deactivate()

        buf = bytearray(
            ffi.sizeof("lpc_cmd_id_t")
        )
        cmd = ffi.cast("struct lpc_cmd_t*", ffi.from_buffer(buf))
        cmd.cmd = LPCCommand.LULLABY.value
        self._main.send_message(Address.LPC1114, bytes(buf))

    def wakeup(self):
        self._reset_sleep_timer()
        if not self._sleeping:
            return
        self.logger.info("UI waking up")
        self._sleeping = False
        self._clock_task.start()
        if self._current_screen is not None:
            self._current_screen.activate(self)

        buf = bytearray(
            ffi.sizeof("lpc_cmd_id_t")
        )
        cmd = ffi.cast("struct lpc_cmd_t*", ffi.from_buffer(buf))
        cmd.cmd = LPCCommand.WAKE_UP.value
        self._main.send_message(Address.LPC1114, bytes(buf))
        self.set_brightness(0x0fff)
        self.invalidate(DirtMarker(DirtKind.DISPLAY))

    def _draw_screen_clock(self):
        all_dirty = DirtMarker.display() in self._dirt
        if not all_dirty and DirtMarker.clock() not in self._dirt:
            return

        if all_dirty:
            self.fill_rect(
                metrics.CLOCK_POSITION_X, 0,
                metrics.LCD_WIDTH-1, metrics.CLOCK_HEIGHT,
                metrics.THEME_BACKDROP_BACKGROUND_COLOUR
            )

        self._draw_clock(datetime.now())

    def _draw_screen_title(self):
        all_dirty = DirtMarker.display() in self._dirt

        y0 = metrics.TABBAR_TOP
        x0 = metrics.TABBAR_LEFT
        if all_dirty:
            self.fill_rect(
                x0, y0,
                metrics.LCD_WIDTH-1, metrics.LCD_HEIGHT-1,
                metrics.THEME_BACKDROP_BACKGROUND_COLOUR,
            )

        for i, screen in enumerate(self._screens):
            if all_dirty or DirtMarker.tab(i) in self._dirt:
                self._draw_tab(
                    screen.tab_caption,
                    x0,
                    y0,
                    screen is self._current_screen
                )
            y0 += metrics.TAB_HEIGHT + metrics.TAB_PADDING

    def _draw_screen_background(self):
        if not (DirtMarker.client_area() in self._dirt or
                DirtMarker.display() in self._dirt):
            return

        self.fill_rect(
            metrics.SCREEN_MARGIN_LEFT,
            metrics.SCREEN_MARGIN_TOP,
            (metrics.LCD_WIDTH-1)-metrics.SCREEN_MARGIN_RIGHT,
            (metrics.LCD_HEIGHT-1)-metrics.SCREEN_MARGIN_BOTTOM,
            metrics.THEME_CLIENT_AREA_BACKGROUND_COLOUR
        )
        self.fill_rect(
            (metrics.LCD_WIDTH-1)-metrics.SCREEN_MARGIN_RIGHT-1,
            metrics.SCREEN_MARGIN_TOP,
            (metrics.LCD_WIDTH-1)-metrics.SCREEN_MARGIN_RIGHT-1,
            (metrics.LCD_HEIGHT-1)-metrics.SCREEN_MARGIN_BOTTOM,
            metrics.THEME_CLIENT_AREA_BORDER_COLOUR
        )

    def _draw_tab(self, name, x0, y0, active):
        bgcolour = (
            metrics.THEME_TAB_ACTIVE_BACKGROUND_COLOUR
            if active
            else metrics.THEME_TAB_BACKGROUND_COLOUR
        )

        textcolour = (
            metrics.THEME_TAB_ACTIVE_COLOUR
            if active
            else metrics.THEME_TAB_COLOUR
        )

        bordercolour = (
            metrics.THEME_TAB_ACTIVE_BORDER_COLOUR
            if active
            else metrics.THEME_TAB_BORDER_COLOUR
        )

        x0 += -1 if active else 1

        self.fill_rect(
            x0, y0,
            x0+metrics.TAB_WIDTH-2, y0+metrics.TAB_HEIGHT-1,
            bgcolour,
        )

        self.draw_line(
            x0+metrics.TAB_WIDTH-1, y0+1,
            x0+metrics.TAB_WIDTH-1, y0+metrics.TAB_HEIGHT-2,
            bgcolour,
        )

        self.draw_line(
            x0, y0+1,
            x0+metrics.TAB_WIDTH-3, y0+1,
            bordercolour)
        self.draw_line(
            x0+metrics.TAB_WIDTH-2, y0+2,
            x0+metrics.TAB_WIDTH-2, y0+metrics.TAB_HEIGHT-3,
            bordercolour)
        self.draw_line(
            x0+metrics.TAB_WIDTH-3, y0+metrics.TAB_HEIGHT-2,
            x0, y0+metrics.TAB_HEIGHT-2,
            bordercolour)

        self.draw_text(
            x0+2, y0+6+metrics.TAB_HEIGHT//2,
            LPCFont.DEJAVU_SANS_12PX,
            textcolour,
            name
        )

    def fill_rect(self, x0, y0, x1, y1, colour):
        colour = rgb24_to_rgb16(*colour)
        buf = bytearray(
            ffi.sizeof("lpc_cmd_id_t") +
            ffi.sizeof("struct lpc_cmd_draw_rect_t")
        )
        cmd = ffi.cast(
            "struct lpc_cmd_t*",
            ffi.from_buffer(buf)
        )
        cmd.cmd = LPCCommand.FILL_RECT.value
        cmd.args.fill_rect.colour = colour
        cmd.args.fill_rect.x0 = x0
        cmd.args.fill_rect.x1 = x1
        cmd.args.fill_rect.y0 = y0
        cmd.args.fill_rect.y1 = y1
        self._main.send_message(Address.LPC1114, buf)

    def draw_rect(self, x0, y0, x1, y1, colour):
        colour = rgb24_to_rgb16(*colour)
        buf = bytearray(
            ffi.sizeof("lpc_cmd_id_t") +
            ffi.sizeof("struct lpc_cmd_draw_rect_t")
        )
        cmd = ffi.cast(
            "struct lpc_cmd_t*",
            ffi.from_buffer(buf)
        )
        cmd.cmd = LPCCommand.DRAW_RECT.value
        cmd.args.draw_rect.colour = colour
        cmd.args.draw_rect.x0 = x0
        cmd.args.draw_rect.x1 = x1
        cmd.args.draw_rect.y0 = y0
        cmd.args.draw_rect.y1 = y1
        self._main.send_message(Address.LPC1114, buf)

    def draw_line(self, x0, y0, x1, y1, colour):
        colour = rgb24_to_rgb16(*colour)
        buf = bytearray(
            ffi.sizeof("lpc_cmd_id_t") +
            ffi.sizeof("struct lpc_cmd_draw_line_t")
        )
        cmd = ffi.cast(
            "struct lpc_cmd_t*",
            ffi.from_buffer(buf)
        )
        cmd.cmd = LPCCommand.DRAW_LINE.value
        cmd.args.draw_line.colour = colour
        cmd.args.draw_line.x0 = x0
        cmd.args.draw_line.x1 = x1
        cmd.args.draw_line.y0 = y0
        cmd.args.draw_line.y1 = y1
        self._main.send_message(Address.LPC1114, buf)

    def draw_text(self, x0, y0, font, colour, text):
        text_bytes = text.encode("utf-8") + b"\x00"
        colour = rgb24_to_rgb16(*colour)
        buf = bytearray(
            ffi.sizeof("lpc_cmd_id_t") +
            ffi.sizeof("struct lpc_cmd_draw_text") +
            len(text_bytes)
        )
        cmd = ffi.cast(
            "struct lpc_cmd_t*",
            ffi.from_buffer(buf)
        )
        cmd.cmd = LPCCommand.DRAW_TEXT.value
        cmd.args.draw_text.fgcolour = colour
        cmd.args.draw_text.font = font.value
        cmd.args.draw_text.x0 = x0
        cmd.args.draw_text.y0 = y0
        cmd.args.draw_text.text[0:len(text_bytes)] = text_bytes
        self._main.send_message(Address.LPC1114, buf)

    def set_brightness(self, brightness):
        buf = bytearray(
            ffi.sizeof("lpc_cmd_id_t") +
            ffi.sizeof("struct lpc_cmd_set_brightness_t")
        )
        cmd = ffi.cast("struct lpc_cmd_t*", ffi.from_buffer(buf))
        cmd.cmd = LPCCommand.SET_BRIGHTNESS.value
        cmd.args.set_brightness.brightness = 0x0fff
        self._main.send_message(Address.LPC1114, bytes(buf))

    def table_start(self, x0, y0, row_height, columns):
        col_offset = (
            ffi.sizeof("lpc_cmd_id_t") +
            ffi.sizeof("struct lpc_cmd_table_start_t")
        )
        col_skip = ffi.sizeof("struct table_column_t")
        buf = bytearray(
            col_offset + col_skip * len(columns)
        )
        cmd = ffi.cast("struct lpc_cmd_t*", ffi.from_buffer(buf))
        cmd.cmd = LPCCommand.TABLE_START.value
        cmd.args.table_start.x0 = x0
        cmd.args.table_start.y0 = y0
        cmd.args.table_start.row_height = row_height
        cmd.args.table_start.column_count = len(columns)

        for i, (width, alignment) in enumerate(columns):
            cmd.args.table_start.columns[i].width = width
            cmd.args.table_start.columns[i].alignment = alignment.value
            # col_base = col_offset + col_skip*i
            # column = ffi.cast("struct table_column_t*", ffi.from_buffer(buf[col_base:col_base+col_skip]))
            # column.width = width
            # column.alignment = alignment

        self._main.send_message(Address.LPC1114, bytes(buf))

    def table_row(self, font, fg_colour, bg_colour, columns):
        columns_encoded = b"\0".join([c.encode("utf-8") for c in columns] + [b""])
        buf = bytearray(
            ffi.sizeof("lpc_cmd_id_t") +
            ffi.sizeof("struct lpc_cmd_table_row_t") +
            len(columns_encoded),
        )
        cmd = ffi.cast("struct lpc_cmd_t*", ffi.from_buffer(buf))
        cmd.cmd = LPCCommand.TABLE_ROW.value
        cmd.args.table_row.font = font.value
        cmd.args.table_row.fgcolour = rgb24_to_rgb16(*fg_colour)
        cmd.args.table_row.bgcolour = rgb24_to_rgb16(*bg_colour)
        cmd.args.table_row.contents[0:len(columns_encoded)] = columns_encoded

        self._main.send_message(Address.LPC1114, bytes(buf))

    def table_row_ex(self, font, columns: typing.Sequence[TableColumnEx]):
        texts_encoded = [
            col.text.encode("utf-8") + b"\0"
            for col in columns
        ]
        texts_len = sum(map(len, texts_encoded))
        col_base = (
            ffi.sizeof("lpc_cmd_id_t") +
            ffi.sizeof("struct lpc_cmd_table_row_ex_t")
        )
        col_header_size = ffi.sizeof("struct table_column_ex_t")
        buf = bytearray(
            col_base +
            col_header_size * len(columns) +
            texts_len
        )
        cmd = ffi.cast("struct lpc_cmd_t*", ffi.from_buffer(buf))
        cmd.cmd = LPCCommand.TABLE_ROW_EX.value
        cmd.args.table_row_ex.font = font.value

        col_offset = col_base
        for text_encoded, column_py in zip(texts_encoded, columns):
            col_size = col_header_size + len(text_encoded)
            col_buf = bytearray(col_size)
            column_c = ffi.cast(
                "struct table_column_ex_t*",
                ffi.from_buffer(col_buf)
            )
            column_c.bgcolour = rgb24_to_rgb16(*column_py.bgcolour)
            column_c.fgcolour = rgb24_to_rgb16(*column_py.fgcolour)
            column_c.alignment = column_py.alignment.value
            column_c.text[0:len(text_encoded)] = text_encoded

            buf[col_offset:col_offset+col_size] = col_buf
            col_offset += col_size

        self._main.send_message(Address.LPC1114, bytes(buf))

