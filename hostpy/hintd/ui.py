import asyncio
import logging

from datetime import datetime, timedelta

from hintd.ccomm import ffi
from hintd.cconstants import (
    rgb24_to_rgb16,
    LPCCommand,
    Address,
    LPCFont,
)
from hintd.utils import logged_future

LCD_WIDTH = 320
LCD_HEIGHT = 240

SCREEN_MARGIN_TOP = 22
SCREEN_MARGIN_LEFT = 0
SCREEN_MARGIN_RIGHT = 62
SCREEN_MARGIN_BOTTOM = 0

SCREEN_CLIENT_AREA_TOP = 24
SCREEN_CLIENT_AREA_LEFT = 2
SCREEN_CLIENT_AREA_RIGHT = ((LCD_WIDTH-1)-64)
SCREEN_CLIENT_AREA_BOTTOM = ((LCD_HEIGHT-1)-2)

SCREEN_CLIENT_AREA_WIDTH = (SCREEN_CLIENT_AREA_RIGHT-SCREEN_CLIENT_AREA_LEFT-1)

SCREEN_HEADER_MARGIN_TOP = 0
SCREEN_HEADER_MARGIN_LEFT = 8
SCREEN_HEADER_MARGIN_RIGHT = 72
SCREEN_HEADER_HEIGHT = 22

CLOCK_POSITION_X = ((LCD_WIDTH-1)-64)
CLOCK_POSITION_Y = 18

TAB_WIDTH = 60
TAB_HEIGHT = 28
TAB_PADDING = 4

TABBAR_LEFT = ((LCD_WIDTH-1)-SCREEN_MARGIN_RIGHT)
TABBAR_TOP = (SCREEN_CLIENT_AREA_TOP+4)

THEME_DEFAULT_COLOUR = 0, 0, 0
THEME_DEFAULT_BACKGROUND_COLOUR = 255, 255, 255

THEME_CLIENT_AREA_BACKGROUND_COLOUR = THEME_DEFAULT_BACKGROUND_COLOUR
THEME_CLIENT_AREA_COLOUR = THEME_DEFAULT_COLOUR
THEME_CLIENT_AREA_BORDER_COLOUR = THEME_DEFAULT_COLOUR

THEME_TH_BACKGROUND_COLOUR = 255, 0, 255
THEME_TH_COLOUR = THEME_DEFAULT_COLOUR

THEME_BACKDROP_BACKGROUND_COLOUR = 0, 0, 0
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
    def __init__(self, tab_caption, title):
        super().__init__()
        self.tab_caption = tab_caption
        self.title = title
        self.logger = logging.getLogger(
            ".".join([__name__, type(self).__qualname__])
        )

    def touch_down(self, x, y, z):
        pass

    def touch_move(self, x, y, z):
        pass

    def touch_up(self, x, y, z):
        pass

    def activate(self, main, ui):
        self._main = main
        self._ui = ui

    def deactivate(self):
        del self._main
        del self._ui

    def sleep(self):
        pass

    def wakeup(self):
        pass

    def paint(self):
        pass


class DepartureScreen(Screen):
    def __init__(self):
        super().__init__(
            "DVB",
            "DVB Abfahrtsmonitor"
        )

    def paint(self):
        self._ui.draw_text(
            SCREEN_CLIENT_AREA_LEFT,
            SCREEN_CLIENT_AREA_TOP+14,
            LPCFont.DEJAVU_SANS_12PX_BF,
            THEME_CLIENT_AREA_COLOUR,
            "Hello World!",
        )


class UI:
    SLEEP_TIMEOUT = timedelta(seconds=30)

    def __init__(self, main):
        super().__init__()
        self._loop = asyncio.get_event_loop()
        self.logger = logging.getLogger(
            ".".join([__name__, type(self).__qualname__])
        )
        self._screens = []
        self._main = main
        self._current_screen = None
        self._invalidated = False
        self._sleeping = True
        self._clock_task = None
        self._sleep_handle = None

    def _draw_clock(self, t):
        colon = ":" if t.second % 2 == 0 else " "
        text = "{:02d}{}{:02d}".format(
            t.hour,
            colon,
            t.minute,
        )
        self.fill_rect(
            CLOCK_POSITION_X,
            0,
            LCD_WIDTH-1, CLOCK_POSITION_Y+2,
            THEME_BACKDROP_BACKGROUND_COLOUR,
        )
        self.draw_text(
            CLOCK_POSITION_X,
            CLOCK_POSITION_Y,
            LPCFont.CANTARELL_20PX_BF,
            THEME_BACKDROP_COLOUR,
            text,
        )

    async def _clock_impl(self):
        # for once, we don’t need utc :-O
        now = datetime.now()
        self._draw_clock(now)
        while True:
            next_second = now.replace(microsecond=0)+timedelta(seconds=1)
            tts = (next_second - now).total_seconds()
            if tts > 0:
                await asyncio.sleep(tts)
            now = datetime.now()
            self._draw_clock(now)

    def add_screen(self, screen):
        self._screens.append(screen)
        if self._current_screen is None:
            self._activate_screen(screen)
        self.invalidate()

    def _activate_screen(self, screen):
        if self._current_screen is not None:
            self._current_screen.deactivate()
        self._current_screen = screen
        self._current_screen.activate(self._main, self)

    def _reset_sleep_timer(self):
        if self._sleep_handle is not None:
            self._sleep_handle.cancel()
        self._sleep_handle = self._loop.call_later(
            self.SLEEP_TIMEOUT.total_seconds(),
            self.sleep,
        )
        self.logger.debug("reset sleep timer")

    def touch_down(self, x, y, z):
        self.wakeup()

    def touch_move(self, x, y, z):
        self.wakeup()

    def touch_up(self, x, y, z):
        self.wakeup()

    def _invalidate_delayed(self):
        if not self._invalidated or self._sleeping:
            return
        self._invalidated = False
        self.full_repaint()

    def invalidate(self):
        self._invalidated = True
        self._loop.call_soon(self._invalidate_delayed)

    def full_repaint(self):
        try:
            self.fill_rect(
                0, 0,
                LCD_WIDTH, LCD_HEIGHT,
                THEME_BACKDROP_BACKGROUND_COLOUR
            )
            self._draw_screen_title()
            self._draw_screen_background()
            if self._current_screen is not None:
                self._current_screen.paint()
        except:
            self.logger.exception("repaint failed")
            raise

    def sleep(self):
        if self._sleeping:
            return
        self.logger.info("UI entering sleep state")
        self._sleeping = True
        self._clock_task.cancel()

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
        self._clock_task = logged_future(
            self.logger.getChild("clock"),
            self._clock_impl()
        )

        buf = bytearray(
            ffi.sizeof("lpc_cmd_id_t")
        )
        cmd = ffi.cast("struct lpc_cmd_t*", ffi.from_buffer(buf))
        cmd.cmd = LPCCommand.WAKE_UP.value
        self._main.send_message(Address.LPC1114, bytes(buf))

    def _draw_screen_title(self):
        self.fill_rect(
            SCREEN_HEADER_MARGIN_LEFT,
            SCREEN_HEADER_MARGIN_TOP,
            (LCD_WIDTH-1)-SCREEN_HEADER_MARGIN_RIGHT,
            SCREEN_HEADER_HEIGHT,
            THEME_H1_BACKGROUND_COLOUR,
        )
        self.draw_rect(
            SCREEN_HEADER_MARGIN_LEFT+1,
            SCREEN_HEADER_MARGIN_TOP+1,
            (LCD_WIDTH-1)-SCREEN_HEADER_MARGIN_RIGHT-1,
            SCREEN_HEADER_HEIGHT,
            THEME_H1_BORDER_COLOUR,
        )

        y0 = TABBAR_TOP
        x0 = TABBAR_LEFT
        for screen in self._screens:
            self._draw_tab(
                screen.tab_caption,
                x0,
                y0,
                screen is self._current_screen
            )
            y0 += TAB_HEIGHT + TAB_PADDING

        if self._current_screen is not None:
            self.draw_text(
                SCREEN_HEADER_MARGIN_LEFT+4,
                SCREEN_HEADER_MARGIN_TOP+16,
                LPCFont.DEJAVU_SANS_12PX_BF,
                THEME_H1_COLOUR,
                self._current_screen.title,
            )

    def _draw_screen_background(self):
        self.fill_rect(
            SCREEN_MARGIN_LEFT,
            SCREEN_MARGIN_TOP,
            (LCD_WIDTH-1)-SCREEN_MARGIN_RIGHT,
            (LCD_HEIGHT-1)-SCREEN_MARGIN_BOTTOM,
            THEME_CLIENT_AREA_BACKGROUND_COLOUR
        )
        self.draw_rect(
            SCREEN_MARGIN_LEFT+1,
            SCREEN_MARGIN_TOP+1,
            (LCD_WIDTH-1)-SCREEN_MARGIN_RIGHT-1,
            (LCD_HEIGHT-1)-SCREEN_MARGIN_BOTTOM-1,
            THEME_CLIENT_AREA_BORDER_COLOUR
        )

    def _draw_tab(self, name, x0, y0, active):
        bgcolour = (
            THEME_TAB_ACTIVE_BACKGROUND_COLOUR
            if active
            else THEME_TAB_BACKGROUND_COLOUR
        )

        textcolour = (
            THEME_TAB_ACTIVE_COLOUR
            if active
            else THEME_TAB_COLOUR
        )

        bordercolour = (
            THEME_TAB_ACTIVE_BORDER_COLOUR
            if active
            else THEME_TAB_BORDER_COLOUR
        )

        x0 += -1 if active else 1

        self.fill_rect(
            x0, y0,
            x0+TAB_WIDTH-2, y0+TAB_HEIGHT-1,
            bgcolour,
        )

        self.draw_line(
            x0+TAB_WIDTH-1, y0+1,
            x0+TAB_WIDTH-1, y0+TAB_HEIGHT-2,
            bgcolour,
        )

        self.draw_line(
            x0, y0+1,
            x0+TAB_WIDTH-3, y0+1,
            bordercolour)
        self.draw_line(
            x0+TAB_WIDTH-2, y0+2,
            x0+TAB_WIDTH-2, y0+TAB_HEIGHT-3,
            bordercolour)
        self.draw_line(
            x0+TAB_WIDTH-3, y0+TAB_HEIGHT-2,
            x0, y0+TAB_HEIGHT-2,
            bordercolour)

        self.draw_text(
            x0+2, y0+6+TAB_HEIGHT//2,
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
            ffi.sizeof("lpc_cmd_id_t")+
            ffi.sizeof("struct lpc_cmd_set_brightness_t")
        )
        cmd = ffi.cast("struct lpc_cmd_t*", ffi.from_buffer(buf))
        cmd.cmd = LPCCommand.SET_BRIGHTNESS.value
        cmd.args.set_brightness.brightness = 0x0fff
        self._main.send_message(Address.LPC1114, bytes(buf))
