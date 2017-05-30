import asyncio
import binascii
import ctypes
import logging
import os
import pathlib
import pty
import random
import signal

from cffi import FFI

import serial
import serial_asyncio

import PyQt5.Qt as Qt

import quamash

from hintd.cconstants import (
    Address,
    ArduinoSubject,
    LPCCommand,
    LPCSubject,
    compose_message,
    rgb16_to_rgb24,
)
from hintd.ccomm import ffi
from hintd.protocol import Protocol
import hintd.utils

from faked.ui.faked import Ui_MainWindow

SENSOR_IDS = [
    binascii.a2b_hex(x)
    for x in [
            "00112233445566",
            "11223344556677",
    ]
]

_ptsname_ffi = FFI()
_ptsname_ffi.cdef(
    """
    int ptsname_r(int fd, char *buf, size_t buflen);
    int grantpt(int fd);
    int unlockpt(int fd);
    """
)
_libc = _ptsname_ffi.dlopen(None)


def _oserror_from_errno():
    errno = ctypes.get_errno()
    return OSError("[Errno {}] {}".format(
        errno,
        os.strerror(errno)
    ))


def ptsname(fd):
    buf = bytearray(1024)
    result = _libc.ptsname_r(
        fd,
        _ptsname_ffi.from_buffer(buf),
        len(buf)
    )
    if result != 0:
        raise _oserror_from_errno()
    return buf.rstrip(b"\0").decode()


def grantpt(fd):
    result = _libc.grantpt(fd)
    if result != 0:
        raise _oserror_from_errno()


def unlockpt(fd):
    result = _libc.unlockpt(fd)
    if result != 0:
        raise _oserror_from_errno()


async def handle_client(protocol):
    sensors = list(SENSOR_IDS)
    await asyncio.sleep(1)
    try:
        print("client is there")
        while True:
            # try:
            #     next_sensor = sensors.pop()
            # except IndexError:
            #     sensors = list(SENSOR_IDS)
            #     next_sensor = sensors.pop()

            # # we fake temperature data here
            # msg = ffi.new("struct ard_msg_t*")
            # msg.subject = ArduinoSubject.SENSOR_READOUT.value
            # msg.data.sensor_readout.sensor_id[0:7] = next_sensor
            # msg.data.sensor_readout.raw_readout = round(random.gauss(
            #     20*16, 5*16
            # ))

            # payload = bytes(ffi.buffer(
            #     msg,
            #     ffi.sizeof(msg.data.sensor_readout)+1
            # ))

            # protocol.send_message(
            #     Address.HOST,
            #     payload,
            #     sender=Address.ARDUINO,
            # )

            await asyncio.sleep(5)
    finally:
        protocol.close()


class MainWindow(Qt.QMainWindow):
    def __init__(self, loop, args, parent=None):
        super().__init__(parent)
        self._ui = Ui_MainWindow()
        self._ui.setupUi(self)
        self._loop = loop
        self._args = args
        self._logger = logging.getLogger("main")

        self._fontmap = {
            0x10: Qt.QFont("DejaVu Sans", 9),
            0x20: Qt.QFont("DejaVu Sans", 12),
            0x21: Qt.QFont("DejaVu Sans", 12, Qt.QFont.Bold),
            0x31: Qt.QFont("Cantarell", 20, Qt.QFont.Bold),
        }

        self._text_flagmap = {
            0: Qt.Qt.AlignLeft,
            1: Qt.Qt.AlignRight,
            2: Qt.Qt.AlignHCenter,
        }

        self._clients = []
        self._tty_transport = None

    def _client_disconnected(self, client):
        self._clients.remove(client)
        if self._args.use_tty:
            asyncio.ensure_future(self._open_new_tty())

    async def _open_new_tty(self):
        if self._tty_transport is not None:
            if self._tty_transport.serial is not None:
                self._tty_transport.abort()
        if os.path.islink(self._args.use_tty):
            os.unlink(self._args.use_tty)

        self._tty_transport, _ = await serial_asyncio.create_serial_connection(
            self._loop,
            self.create_protocol,
            "/dev/ptmx",
        )
        filename = ptsname(self._tty_transport.serial.fd)
        self._logger.info("opened new TTY device: %s", filename)
        grantpt(self._tty_transport.serial.fd)
        unlockpt(self._tty_transport.serial.fd)
        os.symlink(filename, self._args.use_tty)

    def create_protocol(self):
        protocol = Protocol(
            local_address=Address.LPC1114,
            ping_peer=None,
            expect_acks=False,
            send_acks={Address.HOST},
        )
        # hintd.utils.logged_future(
        #     logging.getLogger("client_task"),
        #     handle_client(protocol),
        # )
        protocol.on_message.connect(self.on_message_received)

        def send_mouse_event(x, y, z):
            msg = ffi.new("struct lpc_msg_t*")
            msg.subject = LPCSubject.TOUCH_EVENT.value
            msg.payload.touch_ev.x = x
            msg.payload.touch_ev.y = y
            msg.payload.touch_ev.z = z
            msg = bytes(ffi.buffer(
                msg,
                ffi.sizeof("struct lpc_msg_t")
            ))
            protocol.send_message(Address.HOST, msg)

        self._ui.display.on_mouse_event.connect(send_mouse_event)
        self._clients.append(protocol)

        def disconnect():
            self._client_disconnected(protocol)

        protocol.on_disconnect.connect(
            disconnect
        )
        return protocol

    def on_command_received(self, cmd):
        cmd_enum = LPCCommand(cmd.cmd)

        if cmd_enum == LPCCommand.FILL_RECT:
            args = cmd.args.fill_rect
            colour = Qt.QColor(*rgb16_to_rgb24(args.colour), 255)
            self._ui.display.fill_rect(
                colour,
                args.x0,
                args.y0,
                args.x1,
                args.y1,
            )

        elif cmd_enum == LPCCommand.DRAW_RECT:
            args = cmd.args.draw_rect
            colour = Qt.QColor(*rgb16_to_rgb24(args.colour), 255)
            self._ui.display.draw_rect(
                colour,
                args.x0,
                args.y0,
                args.x1,
                args.y1,
            )

        elif cmd_enum == LPCCommand.DRAW_LINE:
            args = cmd.args.draw_line
            colour = Qt.QColor(*rgb16_to_rgb24(args.colour), 255)
            self._ui.display.draw_line(
                colour,
                args.x0,
                args.y0,
                args.x1,
                args.y1,
            )

        elif cmd_enum == LPCCommand.DRAW_TEXT:
            args = cmd.args.draw_text
            colour = Qt.QColor(*rgb16_to_rgb24(args.fgcolour), 255)
            font = self._fontmap[args.font]
            text_len = (len(cmd) -
                        ffi.sizeof("lpc_cmd_id_t") -
                        ffi.sizeof("struct lpc_cmd_draw_text") - 1)
            text = bytes(args.text[0:text_len]).decode()
            self._ui.display.draw_text(
                colour,
                font,
                args.x0,
                args.y0,
                text,
            )

        elif cmd_enum == LPCCommand.TABLE_START:
            args = cmd.args.table_start
            self._table_columns = [
                (args.columns[i].width,
                 self._text_flagmap[args.columns[i].alignment])
                for i in range(args.column_count)
            ]
            self._table_x0 = args.x0
            self._table_y0 = args.y0
            self._table_row_height = args.row_height
            self._table_width = sum(col[0] for col in self._table_columns)

        elif cmd_enum == LPCCommand.TABLE_ROW:
            args = cmd.args.table_row
            fgcolour = Qt.QColor(*rgb16_to_rgb24(args.fgcolour), 255)
            bgcolour = Qt.QColor(*rgb16_to_rgb24(args.bgcolour), 255)
            font = self._fontmap[args.font]
            metrics = Qt.QFontMetrics(font)
            font_height = metrics.ascent()
            data_len = (len(cmd) -
                        ffi.sizeof("lpc_cmd_id_t") -
                        ffi.sizeof("struct lpc_cmd_table_row_t") - 1)
            cols = [
                col.decode()
                for col in bytes(args.contents[0:data_len]).split(b"\0")
            ]

            self._ui.display.fill_rect(
                bgcolour,
                self._table_x0,
                self._table_y0-font_height,
                self._table_x0+self._table_width-1,
                self._table_y0+self._table_row_height-font_height-1,
            )

            x = self._table_x0
            for text, info in zip(cols, self._table_columns):
                self._ui.display.draw_text_rect(
                    fgcolour,
                    font,
                    x,
                    self._table_y0-font_height,
                    x+info[0],
                    self._table_y0+self._table_row_height-font_height,
                    text,
                    info[1] | Qt.Qt.TextSingleLine,
                )
                x += info[0]

            self._table_y0 += self._table_row_height

        elif cmd_enum == LPCCommand.TABLE_ROW_EX:
            args = cmd.args.table_row_ex
            font = self._fontmap[args.font]
            metrics = Qt.QFontMetrics(font)
            font_height = metrics.ascent()
            max_len = (len(cmd) -
                       ffi.sizeof("lpc_cmd_id_t") -
                       ffi.sizeof("struct lpc_cmd_table_row_ex_t"))
            buf = bytes(ffi.cast("uint8_t*", args.contents)[0:max_len])

            x = self._table_x0
            for info in self._table_columns:
                col = ffi.cast(
                    "const struct table_column_ex_t*",
                    ffi.from_buffer(buf)
                )
                fgcolour = Qt.QColor(*rgb16_to_rgb24(col.fgcolour), 255)
                bgcolour = Qt.QColor(*rgb16_to_rgb24(col.bgcolour), 255)
                flags = self._text_flagmap[col.alignment]
                text = ffi.string(col.text)

                x0 = x
                y0 = self._table_y0-font_height
                x1 = x+info[0]-1
                y1 = self._table_y0-font_height+self._table_row_height-1

                self._ui.display.fill_rect(
                    bgcolour,
                    x0, y0, x1, y1
                )

                self._ui.display.draw_text_rect(
                    fgcolour,
                    font,
                    x0, y0, x1+1, y1+1,
                    text.decode("utf-8"),
                    flags | Qt.Qt.TextSingleLine,
                )

                buf = buf[ffi.sizeof("struct table_column_ex_t")+len(text)+1:]

                x += info[0]

            self._table_y0 += self._table_row_height

        else:
            self._logger.warning(
                "ignoring command id %s",
                cmd_enum,
            )

    def on_message_received(self, sender, recipient, payload):
        if sender == Address.HOST and recipient == Address.LPC1114:
            self.on_command_received(payload)
        else:
            self._logger.warning(
                "ignoring message from %r to %r",
                sender, recipient,
            )

    async def run(self):
        self._interrupt_event = asyncio.Event()

        self._loop.add_signal_handler(
            signal.SIGINT,
            self._interrupt_event.set
        )

        self._loop.add_signal_handler(
            signal.SIGTERM,
            self._interrupt_event.set
        )

        if self._args.use_tty:
            unlink = self._args.use_tty
            await self._open_new_tty()
            server = None
        else:
            unlink = self._args.sock_path
            server = await self._loop.create_unix_server(
                self.create_protocol,
                self._args.sock_path,
            )

        self.show()
        try:
            await self._interrupt_event.wait()
        finally:
            os.unlink(unlink)
            if server is not None:
                server.close()
                await server.wait_closed()
            for client in self._clients:
                try:
                    client.close()
                except:
                    pass

    def closeEvent(self, ev):
        self._interrupt_event.set()
        super().closeEvent(ev)


def main():
    import argparse
    import gc
    import sys
    parser = argparse.ArgumentParser()

    group = parser.add_mutually_exclusive_group()
    group.add_argument(
        "--sock-path",
        default=str(pathlib.Path.cwd() / "fake.comm"),
        help="Socket path (default is ./fake.comm)",
    )

    group.add_argument(
        "--use-tty",
        metavar="PATH",
        help="Allocate a tty and print the name on stdout. A symlink to the "
        "PTS will be placed at PATH"
    )

    parser.add_argument(
        "-v",
        dest="verbosity",
        action="count",
        default=0,
    )

    args = parser.parse_args()

    logging.basicConfig(
        level={
            0: logging.ERROR,
            1: logging.WARNING,
            2: logging.INFO,
        }.get(args.verbosity, logging.DEBUG)
    )
    logging.getLogger("quamash.QEventLoop").setLevel(logging.WARNING)
    logging.getLogger("hintd.protocol").setLevel(logging.INFO)

    app = Qt.QApplication(sys.argv[:1])
    app.setQuitOnLastWindowClosed(False)
    loop = quamash.QEventLoop(app=app)
    asyncio.set_event_loop(loop)
    loop = asyncio.get_event_loop()
    main_window = MainWindow(loop, args)
    try:
        loop.run_until_complete(main_window.run())
    finally:
        loop.close()
        asyncio.set_event_loop(None)

    gc.collect()
