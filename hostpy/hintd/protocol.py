import asyncio
import logging
import time

from enum import Enum

import aioxmpp.callbacks
import aioxmpp.custom_queue

from . import utils

from .cconstants import (
    compose_message,
    decompose_message_header,
    adler8ish,
    Address,
    Flag,
)
from .ccomm import ffi


class RecvState(Enum):
    HEADER = 0
    PAYLOAD = 1
    CHECKSUM = 2


class EchoState(Enum):
    IDLE = 0
    SENT = 1
    RECEIVED = 2


class GuardedFFIObject:
    def __init__(self, cast, data):
        self.__raw = bytearray(data)
        self.__ffi_object = ffi.cast(cast, ffi.from_buffer(self.__raw))

    def __getattr__(self, key):
        if key.startswith("_GuardedFFIObject__"):
            return self.__dict__[key]
        ffi = self.__ffi_object
        return getattr(ffi, key)

    def __repr__(self):
        return "<GuardedFFIObject {!r}>".format(self.__ffi_object)

    def __len__(self):
        return len(self.__raw)


class Protocol(asyncio.Protocol):
    TX_ACK_TIMEOUT = 5
    TX_RETRY = 3

    on_connect = aioxmpp.callbacks.Signal()
    on_disconnect = aioxmpp.callbacks.Signal()
    on_message = aioxmpp.callbacks.Signal()

    def __init__(self, *,
                 local_address=Address.HOST,
                 ping_peer=Address.LPC1114,
                 expect_acks=True,
                 send_acks=False,
                 loop=None):
        super().__init__()
        self._loop = loop or asyncio.get_event_loop()

        self._logger = logging.getLogger(
            type(self).__module__ + "." + type(self).__qualname__
        )

        self._transport = None
        self._exc = ConnectionError("not connected")

        self._expect_acks = expect_acks
        self._send_acks = send_acks

        self._local_address = local_address
        self._ping_peer = ping_peer

        self._rx_buf = b""
        self._rx_header = None
        self._rx_payload = None
        self._rx_state = RecvState.HEADER

        self._tx_queue = aioxmpp.custom_queue.AsyncDeque()
        self._tx_ack_event = asyncio.Condition()
        self._tx_acks = set()
        self._tx_task = None

        self._echo_task = None
        self._echo_event = asyncio.Event()
        self._echo_state = EchoState.IDLE

    def on_message_received(self, sender, recipient, payload):
        self._logger.debug(
            "message received: sender=%r, recipient=%r, payload=%r",
            sender, recipient, payload,
        )
        self.on_message(sender, recipient, payload)

    def _critical_task_done(self, fut):
        if fut.exception() and not fut.cancelled():
            if self._transport is not None:
                self._logger.warning(
                    "critical task %r failed, "
                    "killing transport to kill protocol",
                    fut)
                self._transport.abort()

    async def _echo_task_impl(self):
        self._echo_state = EchoState.IDLE
        self._echo_event.clear()
        while True:
            self._echo_state = EchoState.SENT
            self._send_raw(
                self._local_address,
                self._ping_peer,
                {Flag.ECHO},
                b"",
            )
            self._logger.debug("echo request sent")

            while True:
                try:
                    await asyncio.wait_for(
                        self._echo_event.wait(),
                        timeout=10,
                    )
                except asyncio.TimeoutError:
                    self._logger.warning("echo timeout")
                    self._exc = ConnectionError("ping timeout")
                    self._transport.abort()
                else:
                    if self._echo_state != EchoState.RECEIVED:
                        continue
                self._logger.debug("echo response received")
                break

            await asyncio.sleep(10)

    async def _wait_for_specific_ack(self, message_id):
        self._logger.debug("starting to wait for ack %d (acquiring lock)",
                           message_id)
        await self._tx_ack_event.acquire()
        self._logger.debug("acquired lock (message_id=%d)", message_id)
        try:
            while True:
                try:
                    self._tx_acks.remove(message_id)
                except KeyError:
                    self._logger.debug("message not acked yet, "
                                       "waiting for event (message_id=%d)",
                                       message_id)
                    # message was not acked yet
                    await self._tx_ack_event.wait()
                    continue
                else:
                    self._logger.debug("message acked! (message_id=%d)",
                                       message_id)
                    return
        finally:
            self._tx_ack_event.release()

    def _write(self, buf):
        self._logger.debug(">> %r", buf)
        self._transport.write(buf)

    async def _tx_send_message(self, message, message_id):
        self._logger.debug("flags=%r", message[2])
        message = compose_message(*message, message_id)
        self._logger.debug(
            "decomposed=%r",
            decompose_message_header(message)[:4]
        )
        if not self._expect_acks:
            self._write(message)
            return

        had_timeout = False
        for i in range(1, self.TX_RETRY+1):
            if had_timeout:
                self._logger.warning(
                    "TX ack timer timed out, retrying transmission "
                    "({} out of {})".format(
                        i+1, self.TX_RETRY
                    )
                )
            try:
                self._write(message)
                await asyncio.wait_for(
                    self._wait_for_specific_ack(message_id),
                    timeout=self.TX_ACK_TIMEOUT
                )
            except asyncio.TimeoutError:
                continue
            else:
                break
        else:
            self._logger.error(
                "message was not acked at all! data was lost!"
            )

    async def _tx_impl(self):
        ctr = 0
        while True:
            message = await self._tx_queue.get()
            await self._tx_send_message(message, ctr)
            ctr = (ctr + 1) % 16

    def _require_conn(self):
        if self._transport is None:
            raise self._exc

    def _process_data(self, state):
        self._logger.debug("processing rx buffer. curr state: %r", state)
        if state == RecvState.HEADER:
            if len(self._rx_buf) < 4:
                return RecvState.HEADER

            self._rx_header_raw = self._rx_buf[:4]
            self._rx_header = decompose_message_header(
                self._rx_header_raw
            )
            self._logger.debug(
                "decoded message header: %r",
                self._rx_header
            )
            self._rx_buf = self._rx_buf[4:]
            if self._rx_header[2] == 0:
                self._message_received(
                    self._rx_header[0],
                    self._rx_header[1],
                    self._rx_header[3],
                    self._rx_header[4],
                    b""
                )
                return RecvState.HEADER, True
            return RecvState.PAYLOAD, True

        elif state == RecvState.PAYLOAD:
            payload_len = self._rx_header[2]
            if len(self._rx_buf) < payload_len:
                return RecvState.PAYLOAD, False
            self._rx_payload = self._rx_buf[:payload_len]
            self._rx_buf = self._rx_buf[payload_len:]
            return RecvState.CHECKSUM, True

        elif state == RecvState.CHECKSUM:
            if len(self._rx_buf) < 1:
                return RecvState.CHECKSUM, False

            calculated = adler8ish(self._rx_payload)
            received = self._rx_buf[0]
            self._rx_buf = self._rx_buf[1:]
            if calculated != received:
                self._logger.warn(
                    "checksum mismatch: 0x%02x != 0x%02x",
                    calculated, received
                )
                return RecvState.HEADER, True

            self._message_received(
                self._rx_header[0],  # sender
                self._rx_header[1],  # recipient
                self._rx_header[3],  # flags
                self._rx_header[4],  # message_id
                self._rx_payload
            )
            self._rx_payload = None
            return RecvState.HEADER, True

        raise RuntimeError(
            "unexpected state: {}".format(state)
        )

    async def _notify_acked(self, message_id):
        self._logger.debug("ack received (message_id=%d), acquiring lock",
                           message_id)
        await self._tx_ack_event.acquire()
        self._logger.debug("ack received (message_id=%d), lock acquired",
                           message_id)
        try:
            self._tx_acks.add(message_id)
            self._logger.debug("ack received (message_id=%d), sending notify",
                               message_id)
            self._tx_ack_event.notify_all()
            self._logger.debug("ack received (message_id=%d), notified",
                               message_id)
        finally:
            self._tx_ack_event.release()

    def _message_received(self, sender, recipient, flags, message_id, payload):
        if recipient == self._local_address:
            if Flag.ECHO in flags and Flag.ACK not in flags:
                self._logger.debug(
                    "echo req received: sender=%r, recipient=%r, "
                    "payload length=%r; sending response",
                    sender, recipient,
                    len(payload),
                )
                self._send_raw(
                    recipient,
                    sender,
                    {Flag.ECHO, Flag.ACK},
                    payload,
                )
                return

            if Flag.ECHO in flags and Flag.ACK in flags:
                self._logger.debug(
                    "echo res received: sender=%r, recipient=%r, "
                    "payload length=%r",
                    sender, recipient,
                    len(payload),
                )
                self._echo_state = EchoState.RECEIVED
                self._echo_event.set()
                return

            if Flag.ACK in flags:
                self._logger.debug(
                    "ack received: sender=%r, recipient=%r, message_id=%r",
                    sender, recipient,
                    message_id,
                )
                utils.logged_future(
                    self._logger,
                    self._notify_acked(message_id)
                )
                return

            if Flag.RESET in flags:
                return

        self._logger.debug(
            "processing raw message: sender=%r, recipient=%r, payload=%r, flags=%r",
            sender, recipient, payload, flags,
        )

        if sender == Address.ARDUINO:
            payload = GuardedFFIObject(
                "struct ard_msg_t*",
                payload
            )

        elif sender == Address.HOST and recipient == Address.LPC1114:
            payload = GuardedFFIObject(
                "struct lpc_cmd_t*",
                payload
            )

        elif sender == Address.LPC1114 and recipient == Address.HOST:
            payload = GuardedFFIObject(
                "struct lpc_msg_t*",
                payload
            )

        else:
            self._logger.warning(
                "cannot decode payload from %r to %r",
                sender, recipient,
            )

        if (self._send_acks is True or
            (self._send_acks is not False and sender in self._send_acks)):
            self._logger.debug("sending ack")
            self._send_raw(recipient, sender, {Flag.ACK}, b"",
                           message_id=message_id)

        self._loop.call_soon(
            self.on_message_received,
            sender, recipient, payload
        )

    def _send_raw(self, sender, recipient, flags, payload, message_id=0):
        msg = compose_message(
            sender, recipient, flags, payload,
            message_id
        )
        self._write(msg)

    def send_message(self, recipient, payload, *, sender=None, reset=False):
        if sender is None:
            sender = self._local_address

        flags = set()
        if reset:
            if payload:
                raise ValueError("cannot send payload with RESET message")
            flags.add(Flag.RESET)

        self._tx_queue.put_nowait(
            (sender, recipient, flags, payload),
        )

    def connection_made(self, transport):
        self._logger.debug("connection_made: transport=%r", transport)
        self._transport = transport
        self._transport.set_write_buffer_limits(0)
        if self._ping_peer is not None:
            self._logger.debug("setting up echo task")
            self._echo_task = utils.logged_future(
                self._logger,
                self._echo_task_impl()
            )
            self._echo_task.add_done_callback(
                self._critical_task_done
            )

        self._logger.debug("setting up tx task")
        self._tx_task = utils.logged_future(
            self._logger,
            self._tx_impl()
        )
        self._tx_task.add_done_callback(
            self._critical_task_done
        )

        self.on_connect()

    def data_received(self, data):
        try:
            self._rx_buf += data
            self._logger.debug("received %d bytes of data; "
                               "internal buffer is now at %d bytes "
                               "(data=%r)",
                               len(data), len(self._rx_buf),
                               data)
            new_state, cont = self._process_data(self._rx_state)
            while cont:
                cont = False
                self._rx_state = new_state
                if self._rx_buf:
                    new_state, cont = self._process_data(
                        self._rx_state
                    )
            self._logger.debug("current rx state: %r; "
                               "internal buffer size: %d",
                               self._rx_state,
                               len(self._rx_buf))
        except:
            self._logger.warning(
                "exception while receiving data",
                exc_info=True,
            )
            raise

    def eof_received(self):
        pass

    def connection_lost(self, exc):
        self._logger.debug("connection_lost: exc=%r", exc)
        self._transport = None
        self._exc = exc or self._exc or ConnectionError("disconnected")
        if self._echo_task is not None:
            self._echo_task.cancel()
            self._echo_task = None
        self._tx_task.cancel()
        self._tx_task = None
        self.on_disconnect()

    def close(self):
        self._require_conn()
        self._transport.close()
