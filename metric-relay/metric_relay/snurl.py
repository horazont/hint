#!/usr/bin/env python3
"""
Sensor Node UDP Reliability Layer
#################################

The Sensor Node UDP Reliability Layer (SNURL) Protocol provides the following
capabilities on top of UDP:

* Preservation of datagram order
* Finite retransmission attempts to cover short bursts of or a baseline loss of
  packets.

In contrast to TCP, it does **not** provide:

* Guarantee of reception of all data (up to the termination of the connection)

.. autoclass:: Protocol
"""

import asyncio
import functools
import ipaddress
import logging
import numbers
import random
import socket
import struct
import signal
import time

from datetime import timedelta
from enum import Enum

import aioxmpp.callbacks

from hintlib.utils import (
    unpack_and_splice,
)


_rng = random.SystemRandom()


class PacketType(Enum):
    ECHO_REQ = 0x01
    ECHO_RESP = 0x02
    APP_REQ = 0x03
    APP_RESP = 0x04
    DACK = 0x05
    DATA = 0x06


common_header_fmt = struct.Struct(
    "<"
    "B"  # version = 0x00
    "B"  # packet type
    "L"  # connection id
    "H"  # minimum available serial number
    "H"  # maximum sequential serial number received
    "H"  # last received serial number
)

echo_req_fmt = struct.Struct(
    "<"
    "L"  # echo request ID
    "L"  # sender timestamp
)

echo_resp_fmt = struct.Struct(
    "<"
    "L"  # echo request ID
    "L"  # sender timestamp
    "L"  # receiver timestamp
)

app_req_header_fmt = struct.Struct(
    "<"
    "L"  # app request ID
    "B"  # request type
)

app_resp_header_fmt = struct.Struct(
    "<"
    "L"  # app request ID
)

dack_entry_fmt = struct.Struct(
    "<"
    "HH"  # start and end of range
)

data_entry_header_fmt = struct.Struct(
    "<"
    "H"  # serial number
    "B"  # length
)


class SerialNumber:
    """
    An :rfc:`1982` compliant Serial Number implementation.

    :param bits: The number of bits of the serial number.
    :param value: The value of the serial number.

    It supports the full arithmetic defined in the specification.

    Like other numeric types, :class:`SerialNumber` objects are immutable.
    """

    __slots__ = (
        "_bits",
        "_mod",
        "_value",
    )

    def __init__(self, bits, value=0):
        if not isinstance(value, numbers.Integral):
            raise TypeError(
                "SerialNumber objects can only hold integers (got {!r})".format(
                    value,
                )
            )

        super().__init__()
        self._mod = 2**bits
        self._bits = bits
        if not (0 <= value < self._mod):
            raise ValueError(
                "{} out of bounds for SerialNumber with SERIAL_BITS={}".format(
                    value,
                    bits,
                )
            )
        self._value = value

    def __eq__(self, other):
        if isinstance(other, SerialNumber):
            if self._mod != other._mod:
                return False
            return self._value == other._value

        if self._value == other:
            return True

        return NotImplemented

    def __lt__(self, other):
        if not isinstance(other, SerialNumber):
            return NotImplemented

        thresh = self._mod >> 1
        i1, i2 = self._value, other._value

        return (
            (i1 < i2 and i2 - i1 < thresh) or
            (i1 > i2 and i1 - i2 > thresh)
        )

    def __le__(self, other):
        return self < other or self == other

    def __gt__(self, other):
        if not isinstance(other, SerialNumber):
            return NotImplemented

        thresh = self._mod >> 1
        i1, i2 = self._value, other._value

        return (
            (i1 < i2 and i2 - i1 > thresh) or
            (i1 > i2 and i1 - i2 < thresh)
        )

    def __ge__(self, other):
        return self > other or self == other

    def __add__(self, other):
        if not isinstance(other, numbers.Integral):
            raise TypeError(
                "only integers can be added to SerialNumber objects "
                "(got {!r})".format(other)
            )

        thresh = self._mod >> 1

        if not (-thresh < other < thresh):
            raise ValueError(
                "{!r} out of bounds for addition to SerialNumber with "
                "SERIAL_BITS={!r}".format(other, self._bits)
            )

        new_value = (self._value + other) % self._mod

        return SerialNumber(self._bits, new_value)

    def __sub__(self, other):
        if isinstance(other, SerialNumber):
            if self >= other:
                # return positive result
                if self._value >= other._value:
                    return self._value - other._value
                else:
                    return ((other._value ^ 0xffff) + 1) + self._value
            if not (self < other):
                raise ValueError(
                    "difference between {} and {} is undefined".format(
                        self, other,
                    )
                )
            return -(other - self)

        if not isinstance(other, numbers.Integral):
            raise TypeError(
                "only integers can be subtracted from SerialNumber objects "
                "(got {!r})".format(other)
            )

        thresh = self._mod >> 1

        if not (-thresh < other < thresh):
            raise ValueError(
                "{!r} out of bounds for subtraction from SerialNumber with "
                "SERIAL_BITS={!r}".format(other, self._bits)
            )

        new_value = (self._value - other) % self._mod

        return SerialNumber(self._bits, new_value)

    def __radd__(self, other):
        return self + other

    def __repr__(self):
        return "<{}.{}(bits={}, value={})>".format(
            type(self).__module__,
            type(self).__qualname__,
            self._bits,
            self._value,
        )

    def __str__(self):
        return "{}_{{{}}}".format(
            self._value,
            self._bits
        )

    def __hash__(self):
        return hash(self._value)

    def to_int(self):
        return self._value


class SerialNumberProvider:
    """
    Serial number generator.

    This object can be used as a context manager. When entering the context,
    it returns the next serial number. If the context is exited cleanly, the
    change is committed, otherwise the serial number is rolled back so that
    the same number will be returned in the next call again.
    """

    def __init__(self, bits, start=0):
        super().__init__()
        self._current = SerialNumber(bits, start)
        self._requesting = False

    def __enter__(self):
        if self._requesting:
            raise ValueError("only one serial can be requested at any time")
        return self._current

    def __exit__(self, exc_type, exc_value, tb):
        self._requesting = False
        if exc_type is None:
            self._current += 1

    @property
    def current(self):
        return self._current


class SerialNumberRangeSet:
    def __init__(self):
        self._ranges = []

    def add(self, sn):
        for i, (start, end) in enumerate(self._ranges):
            if start <= sn <= end:
                return

            if end + 1 == sn:
                if i+1 < len(self._ranges):
                    next_start, next_end = self._ranges[i+1]
                    if next_start == sn + 1:
                        self._ranges[i] = start, next_end
                        del self._ranges[i+1]
                        return

                self._ranges[i] = start, sn
                return

            if start == sn + 1:
                self._ranges[i] = sn, end
                return

        self._ranges.append((sn, sn))

    def discard_if_first(self, sn):
        if not self._ranges:
            return
        start, end = self._ranges[0]
        if start == sn:
            if start == end:
                del self._ranges[0]
            else:
                self._ranges[0] = start + 1, end

    def discard_up_to(self, sn):
        while self._ranges and self._ranges[0][0] <= sn:
            start, end = self._ranges[0]
            if end <= sn:
                del self._ranges[0]
                continue
            self._ranges[0] = sn + 1, end

    @property
    def nranges(self):
        return len(self._ranges)

    def iter_ranges(self):
        return iter(self._ranges)

    def clear(self):
        self._ranges.clear()

    def __repr__(self):
        return "<{}.{} {!r}>".format(
            type(self).__module__,
            type(self).__name__,
            list(self._ranges),
        )

    def __contains__(self, item):
        for start, end in self._ranges:
            if item >= start and item <= end:
                return True
        return False

    @property
    def first_start(self):
        if not self._ranges:
            return None
        return self._ranges[0][0]

    @property
    def first_end(self):
        if not self._ranges:
            return None
        return self._ranges[0][1]


class Protocol(asyncio.DatagramProtocol):
    """
    Sensor Node UDP Reliability Layer protocol implementation.

    .. signal:: on_data_received(payload: bytes)

        Fires when a datagram was received.

        Events are fired in the order the datagrams were send by the sender,
        not necessarily in reception order. This also means that events may
        be delayed and are thus not a good source of timestamps for received
        datagrams.

    .. signal:: on_resync()

        Fires when a connection resyncs.

        This always happens when the other side has lots its entire state.

    This is a :class:`asyncio.DatagramProtocol`.
    """

    SERIAL_BITS = 16
    MAX_PACKET_SIZE = 1200

    on_data_received = aioxmpp.callbacks.Signal()
    on_resync = aioxmpp.callbacks.Signal()

    def __init__(self, dest_port, *,
                 retransmit_threshold=timedelta(seconds=0.05),
                 tx_max_buffer_size=16,
                 rx_loss_emulation=False,
                 autohandshake=True,
                 logger=None):
        super().__init__()
        self.retransmit_threshold = retransmit_threshold
        self.logger = logger or logging.getLogger(__name__)

        self.app_request_handler = None

        self._connection_id = 0

        self._tx_buffer = []
        self._tx_max_buffer_size = tx_max_buffer_size
        self._rx_loss_emulation = rx_loss_emulation
        self._transport = None
        self._tx_sn = SerialNumberProvider(self.SERIAL_BITS)
        self._tx_dest_addr = ("255.255.255.255", dest_port)
        self._tx_broadcast_addr = self._tx_dest_addr
        self._tx_last_acked_sn = None
        self._tx_broadcast_threshold = self._tx_max_buffer_size // 2

        self.tx_retransmit_count = 0
        self.tx_sent = 0
        self.tx_dropped = 0
        self.rx_given_up_count = 0

        self.tx_app_request_retransmit_interval = timedelta(seconds=1)

        self._rx_max_consecutive_sn = SerialNumber(
            self.SERIAL_BITS,
            2**self.SERIAL_BITS - 1
        )
        self._rx_out_of_order = SerialNumberRangeSet()
        self._rx_last_sn = self._rx_max_consecutive_sn
        self._rx_app_requests = {}
        self._autohandshake = autohandshake

        self._rx_buffer = []

        self.synchronized = asyncio.Event()
        self.synchronized.clear()

    @property
    def tx_buffer_size(self):
        return len(self._tx_buffer)

    def connection_made(self, transport):
        self.logger.debug("using transport %r", transport)
        self._transport = transport
        sock = self._transport.get_extra_info("socket")
        sock.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)

    def connection_lost(self, exc):
        self._transport = None
        self.logger.debug("lost transport: %r", exc)

    def _mark_received_locally(self, sn):
        if sn <= self._rx_max_consecutive_sn:
            self.logger.debug("old packet received (%s)", sn)
            return False

        if (self._rx_max_consecutive_sn + 1) == sn:
            self._rx_max_consecutive_sn += 1
            self._rx_out_of_order.discard_up_to(sn)
        else:
            if sn in self._rx_out_of_order:
                return False
            self._rx_out_of_order.add(sn)

        if self._rx_out_of_order.first_start == self._rx_max_consecutive_sn + 1:
            self._rx_max_consecutive_sn = self._rx_out_of_order.first_end
            self._rx_out_of_order.discard_up_to(self._rx_max_consecutive_sn)

        self.logger.debug(
            "marked %s as received. rx map: max=%s, out_of_order=%r",
            sn,
            self._rx_max_consecutive_sn,
            self._rx_out_of_order
        )
        return True

    def _mark_received_remotely_single(self, sn):
        for i, (_, buffered_sn, _) in enumerate(self._tx_buffer):
            if buffered_sn == sn:
                self.logger.debug(
                    "dropping %s from buffer as it was received by peer",
                    buffered_sn,
                )
                del self._tx_buffer[i]
                return

    def _mark_received_remotely_up_to(self, sn):
        self.logger.debug("dropping everything up to %s from buffer",
                          sn)

        last_less_than = None
        for i, (_, buffered_sn, _) in enumerate(self._tx_buffer):
            if buffered_sn <= sn:
                last_less_than = i
                continue
            break

        if last_less_than is None:
            return

        self.logger.debug(
            "dropping %r from buffer as they were received by peer",
            [sn for _, sn, _ in self._tx_buffer[:i+1]]
        )
        del self._tx_buffer[:i+1]

    def _require_connection(self):
        if not self._transport:
            raise ConnectionError("not connected")

    def _flush_rx_buffer(self):
        for _, payload in self._rx_buffer:
            self.on_data_received(payload)
        self._rx_buffer.clear()

    def datagram_received(self, data, addr):
        if (self._rx_loss_emulation and
                random.random() < self._rx_loss_emulation):
            self.logger.debug("dropping datagram for packet loss emulation")
            return

        if len(data) < common_header_fmt.size:
            self.logger.warning(
                "dropping short datagram (len %d < header size %d)",
                len(data),
                common_header_fmt.size,
            )
            return

        data, common_hdr = unpack_and_splice(data, common_header_fmt)
        version, packet_type, connection_id, min_avail_sn, max_recvd_sn, \
            last_recvd_sn = common_hdr

        if version != 0x00:
            self.logger.warning(
                "dropping datagram with unsupported version (%d)",
                version,
            )
            return

        try:
            packet_type = PacketType(packet_type)
        except ValueError:
            self.logger.warning(
                "dropping datagram with unknown packet type (%d)",
                packet_type,
            )
            return

        min_avail_sn = SerialNumber(self.SERIAL_BITS, min_avail_sn)
        max_recvd_sn = SerialNumber(self.SERIAL_BITS, max_recvd_sn)
        last_recvd_sn = SerialNumber(self.SERIAL_BITS, last_recvd_sn)

        self.logger.debug(
            "datagram: packet_type = %s, connection_id = 0x%08x, "
            "min_avail_sn = %s, max_recvd_sn = %s, last_recvd_sn = %s",
            packet_type, connection_id, min_avail_sn, max_recvd_sn,
            last_recvd_sn,
        )

        valid_connection = (connection_id and
                            connection_id == self._connection_id)
        if valid_connection:
            self._mark_received_remotely_up_to(max_recvd_sn)
            self._mark_received_remotely_single(last_recvd_sn)
            self._tx_last_acked_sn = last_recvd_sn
            self.logger.debug("tx buffer is now: %r", self._tx_buffer)
        else:
            self.logger.debug(
                "datagram does not belong to handshaked connection"
            )

        if ((packet_type == PacketType.DATA or
             packet_type == PacketType.DACK) and
                not valid_connection and
                self._autohandshake):
            if not connection_id:
                connection_id = random.getrandbits(32)
                self.logger.info(
                    "uninitialized connection id received, syncing with 0x%08x",
                    connection_id,
                )
            else:
                self.logger.info(
                    "unknown connection id received, syncing"
                )

            self._connection_id = connection_id
            self._flush_rx_buffer()
            self._rx_out_of_order.clear()
            self._rx_max_consecutive_sn = min_avail_sn - 1
            self._tx_last_acked_sn = self._tx_sn.current
            self._tx_dest_addr = addr
            self.synchronized.set()
            self.on_resync()
            valid_connection = True

        handler_name = "_handle_{}".format(packet_type.name.lower())
        try:
            handler = getattr(self, handler_name)
        except AttributeError:
            self.logger.debug(
                "no handler to handle type %s",
                packet_type,
            )
        else:
            try:
                handler(data,
                        valid_connection=valid_connection,
                        addr=addr)
            except:  # NOQA
                self.logger.exception(
                    "failed to process packet: %r",
                    data,
                )

        if valid_connection:
            # discard state for everything up to min_avail_sn
            self._rx_out_of_order.discard_up_to(min_avail_sn)
            if self._rx_max_consecutive_sn < min_avail_sn:
                self.logger.debug("giving up on receiving frames")
                self._rx_max_consecutive_sn = min_avail_sn

    def _handle_data_entry(self, sn, payload):
        self.logger.debug(
            "data frame received: sn = %s, payload = %r",
            sn, payload,
        )
        if not self._mark_received_locally(sn):
            self.logger.debug("duplicate frame, discarding")
            return

        for i, (recvd_sn, _) in enumerate(self._rx_buffer):
            if recvd_sn == sn:
                # already in buffer
                break
            if recvd_sn > sn:
                # insert before
                self._rx_buffer.insert(i, (sn, payload))
                break
        else:
            self._rx_buffer.append((sn, payload))

    def _handle_data(self, remainder, valid_connection, **kwargs):
        if not valid_connection:
            self.logger.debug("ignoring DATA from unknown connection")
            return

        first_sn = None
        while remainder:
            remainder, (sn, length) = unpack_and_splice(
                remainder,
                data_entry_header_fmt
            )
            sn = SerialNumber(self.SERIAL_BITS, sn)
            if first_sn is None:
                first_sn = sn

            payload = remainder[:length]
            remainder = remainder[length:]
            self._handle_data_entry(sn, payload)

        delete_up_to = 0
        for i, (recvd_sn, payload) in enumerate(self._rx_buffer):
            delete_up_to = i
            if recvd_sn > self._rx_max_consecutive_sn:
                break
            self.logger.debug("emitting event for %r", payload)
            self.on_data_received(payload)
        else:
            delete_up_to = len(self._rx_buffer)
        self.logger.debug("rx max = %s, delete_up_to = %d, rx buffer = %r",
                          self._rx_max_consecutive_sn,
                          delete_up_to,
                          self._rx_buffer)
        del self._rx_buffer[:delete_up_to]

        self._rx_last_sn = first_sn
        self._emit_ack()

    def _handle_dack(self, remainder, valid_connection, **kwargs):
        if not valid_connection:
            self.logger.debug("ignoring DACK from unknown connection")
            return

        while remainder:
            remainder, (first, last) = unpack_and_splice(
                remainder,
                dack_entry_fmt,
            )
            while first <= last:
                self._mark_received_remotely_single(first)
                first += 1

    def _handle_app_req(self, remainder, addr, **kwargs):
        remainder, (request_id, type_) = unpack_and_splice(
            remainder,
            app_req_header_fmt,
        )
        self.logger.debug("app request 0x%08x: request received (type=%r)",
                          request_id,
                          type_)

        if self.app_request_handler is not None:
            try:
                response = self.app_request_handler(type_, remainder)
            except:  # NOQA
                self.logger.exception(
                    "app request 0x%08x: app request handler failed on payload "
                    "(type=%r) %r",
                    request_id,
                    type_,
                    remainder,
                )

            packet = b"".join([
                self._compose_common_header(PacketType.APP_RESP),
                app_resp_header_fmt.pack(request_id),
                response,
            ])

            self._tx(packet, dest=addr)
        else:
            self.logger.warning(
                "app request 0x%08x: received app request, but no handler "
                "installed. assign a callable to the app_request_handler "
                "attribute",
                request_id,
            )

    def _handle_app_resp(self, remainder, **kwargs):
        remainder, (request_id, ) = unpack_and_splice(
            remainder,
            app_resp_header_fmt,
        )

        self.logger.debug("app request 0x%08x: response received",
                          request_id)

        try:
            fut = self._rx_app_requests[request_id]
        except KeyError:
            self.logger.debug("app request 0x%08x: no response future. "
                              "late response?",
                              request_id)
        else:
            fut.set_result(remainder)

    def _compose_common_header(self, packet_type):
        if self._tx_buffer:
            min_avail_sn = self._tx_buffer[0][1]
        else:
            min_avail_sn = self._tx_sn.current

        return common_header_fmt.pack(
            0x00,
            packet_type.value,
            self._connection_id,
            min_avail_sn.to_int(),
            self._rx_max_consecutive_sn.to_int(),
            self._rx_last_sn.to_int(),
        )

    def _tx(self, packet, dest):
        self.logger.debug("sending packet to %s: %r",
                          dest, packet)
        self._transport.sendto(packet, dest)

    def _emit_ack(self):
        common_hdr = self._compose_common_header(PacketType.DACK)

        parts = [common_hdr]
        for i, (start, end) in zip(range(256),
                                   self._rx_out_of_order.iter_ranges()):
            parts.append(dack_entry_fmt.pack(start.to_int(), end.to_int()))

        self.logger.debug("sending ack for %d ranges", len(parts)-1)
        self._tx(b"".join(parts), self._tx_dest_addr)

    def _trigger_tx(self, use_broadcast):
        if not self._tx_buffer:
            return

        common_hdr = self._compose_common_header(PacketType.DATA)
        _, main_sn, main_frame = self._tx_buffer[-1]
        parts = [common_hdr, main_frame]
        total_length = sum(map(len, parts))

        i = 0
        now = time.monotonic()
        while total_length < self.MAX_PACKET_SIZE:
            pb_ts, pb_sn, pb_frame = self._tx_buffer[i]
            if pb_sn == main_sn:
                break
            parts.append(pb_frame)
            total_length += len(pb_frame)
            self.tx_retransmit_count += 1
            i += 1

        self.logger.debug(
            "transmitting frame for sn %s with %d piggybacked frame(s)",
            main_sn,
            len(parts) - 2,
        )

        dest = self._tx_broadcast_addr if use_broadcast else self._tx_dest_addr
        self._tx(b"".join(parts), dest)

    def send_frame(self, buf):
        self._require_connection()

        with self._tx_sn as sn:
            data_entry_hdr = data_entry_header_fmt.pack(
                sn.to_int(),
                len(buf),
            )

            frame = b"".join([data_entry_hdr, buf])
            ts = time.monotonic()
            if len(self._tx_buffer) == self._tx_max_buffer_size:
                self.logger.debug(
                    "dropping frame from tx buffer due to space limitations"
                )
                self.tx_dropped += 1
                del self._tx_buffer[0]
            self._tx_buffer.append((ts, sn, frame))

            use_broadcast = (
                self._connection_id == 0 or
                self._tx_last_acked_sn is None or
                sn - self._tx_last_acked_sn > self._tx_broadcast_threshold
            )

            self._trigger_tx(use_broadcast)
            self.tx_sent += 1

    def error_received(self, exc):
        pass

    async def _app_request_tx_task(self, max_retries, packet, dest, fut,
                                   request_id):
        self.logger.debug(
            "app request 0x%08x: started tx task to %r (max_retries=%d)",
            request_id,
            dest,
            max_retries,
        )

        for i in range(max_retries):
            self.logger.debug("app request 0x%08x: transmit attempt %d",
                              request_id, i+1)
            self._tx(packet, dest)

            done, pending = await asyncio.wait(
                [fut],
                timeout=self.tx_app_request_retransmit_interval.total_seconds(),
            )

            if fut in done:
                self.logger.debug("app request 0x%08x: response future done",
                                  request_id)
                return fut.result()

        self.logger.debug("app request 0x%08x: out of attempts, raising error",
                          request_id)

        raise TimeoutError("no response received in time")

    def _app_request_fut_done(self, request_id, fut):
        del self._rx_app_requests[request_id]

    def app_request(self, type_, payload, dest=None, *,
                    max_retries=3):
        """
        Send an application request to the currently locked-to peer, or the
        given destination address.
        """

        request_id = _rng.getrandbits(32)
        packet = b"".join([
            self._compose_common_header(PacketType.APP_REQ),
            app_req_header_fmt.pack(request_id, type_),
            payload,
        ])

        fut = asyncio.Future()
        fut.add_done_callback(functools.partial(
            self._app_request_fut_done,
            request_id,
        ))

        self._rx_app_requests[request_id] = fut

        dest = dest or self._tx_dest_addr

        return asyncio.ensure_future(self._app_request_tx_task(
            max_retries,
            packet,
            dest,
            fut,
            request_id,
        ))


PORT1 = 7200
PORT2 = 7201


async def _recv_setup(loop, args):
    def receiver_factory():
        protocol = Protocol(
            PORT1,
            logger=logging.getLogger("receiver"),
            rx_loss_emulation=args.loss,
        )
        return protocol

    _, receiver = await loop.create_datagram_endpoint(
        receiver_factory,
        (args.rx_bind, PORT2),
    )

    return {"receiver": receiver}


async def _send_setup(loop, args):
    def sender_factory():
        protocol = Protocol(
            PORT2,
            logger=logging.getLogger("sender"),
            tx_max_buffer_size=args.max_buffer_size,
            rx_loss_emulation=args.loss,
        )
        return protocol

    _, sender = await loop.create_datagram_endpoint(
        sender_factory,
        ("127.0.0.1", PORT1),
    )

    return {
        "sender": sender,
    }


async def _dualsocket_setup(loop, args):
    d = dict()
    d1, d2 = await asyncio.gather(
        _send_setup(loop, args),
        _recv_setup(loop, args),
    )
    d.update(d1)
    d.update(d2)
    return d


async def _sender_impl(loop, args, sender, receiver):
    ctr = 0
    for i in range(args.count):
        if i % 10 == 0:
            print(".", end="", flush=True)
        sender.send_frame(ctr.to_bytes(4, 'little'))
        ctr += 1
        await asyncio.sleep(1/args.rate)


async def _tx_stats_impl(loop, args, sender, **kwargs):
    tx_buffer_size_accum = []

    try:
        while True:
            await asyncio.sleep(0.01)
            tx_buffer_size_accum.append(sender.tx_buffer_size)
    except asyncio.CancelledError:
        await asyncio.sleep(0.5)

        print()

        print("STATISTICS")
        print(" tx buffer size     = {}/{:.2f}/{}".format(
            min(tx_buffer_size_accum),
            sum(tx_buffer_size_accum) / len(tx_buffer_size_accum),
            max(tx_buffer_size_accum),
        ))
        print(" tx sent            = {}".format(sender.tx_sent))
        print(" tx dropped         = {}".format(sender.tx_dropped))
        print(" tx retransmissions = {}".format(sender.tx_retransmit_count))


async def _rx_stats_impl(loop, args, receiver, **kwargs):
    dups = 0
    out_of_order = 0
    lost = 0
    max_ = None
    seen_data = set()

    def data_received(payload):
        nonlocal max_, dups, out_of_order, lost
        ctr = int.from_bytes(payload, 'little')
        if ctr in seen_data:
            dups += 1
        if max_ is None:
            max_ = ctr
        else:
            if ctr <= max_:
                out_of_order += 1
            else:
                lost += (ctr - max_) - 1
            max_ = max(ctr, max_)
        seen_data.add(ctr)

    receiver.on_data_received.connect(
        data_received,
    )

    try:
        while True:
            await asyncio.sleep(60)
    except asyncio.CancelledError:
        await asyncio.sleep(0.5)

        print()

        if "sender" in kwargs:
            rx_percentage = "{:.0f}%".format(
                len(seen_data) / kwargs["sender"].tx_sent * 100,
            )
        else:
            rx_percentage = "?%"

        print("STATISTICS")
        print(" rx dups            = {}".format(dups))
        print(" rx lost            = {}".format(lost))
        print(" rx out of order    = {}".format(out_of_order))
        print(" rx total           = {} ({})".format(
            len(seen_data),
            rx_percentage,
        ))


async def _recv_impl(loop, args, receiver):
    while True:
        await asyncio.sleep(1)


async def _app_request_impl(loop, args, sender, receiver, **kwargs):
    fut = receiver.on_data_received.future()
    # we send a data packet first to make the receiver lock to the sender
    while True:
        sender.send_frame(b"\0\0\0\0")
        done, pending = await asyncio.wait([fut], timeout=0.5)
        if done:
            break

    ctr = 0

    def request_handler(type_, request):
        return (int.from_bytes(request, 'little') ^ 0x8000000).to_bytes(
            4, 'little',
        )

    sender.app_request_handler = request_handler

    response = await receiver.app_request(
        0x01,
        ctr.to_bytes(4, 'little')
    )

    assert response == (ctr ^ 0x8000000).to_bytes(
                            4, 'little',
                        )


async def _set_ntp_server_impl(loop, args, receiver, **kwargs):
    await receiver.synchronized.wait()

    command = app_req_set_sntp_server_fmt.pack(
        args.address.packed,
    )

    response = await receiver.app_request(
        AppReqType.SET_NTP_SERVER.value,
        command,
    )

    assert not response


SCENARIOS = {
    "reliability-test": (
        _dualsocket_setup,
        [
            _sender_impl,
            _tx_stats_impl,
            _rx_stats_impl,
        ]
    ),
    "receiver": (
        _recv_setup,
        [
            _recv_impl,
            _rx_stats_impl,
        ]
    ),
    "app-request-test": (
        _dualsocket_setup,
        [
            _app_request_impl,
        ]
    ),
    "set-ntp-server": (
        _recv_setup,
        [
            _set_ntp_server_impl,
        ]
    )
}


async def amain(loop, args):
    logger = logging.getLogger("main")

    sigint_event = asyncio.Event(loop=loop)
    loop.add_signal_handler(signal.SIGINT, sigint_event.set)
    loop.add_signal_handler(signal.SIGTERM, sigint_event.set)

    setup_func, task_funcs = SCENARIOS[args.scenario]

    argv = await setup_func(loop, args)

    sigint_fut = asyncio.ensure_future(sigint_event.wait())

    futures = [sigint_fut]

    for func in task_funcs:
        futures.append(asyncio.ensure_future(func(
            loop, args, **argv
        )))

    logger.debug("started tasks")
    done, pending = await asyncio.wait(
        futures,
        return_when=asyncio.FIRST_COMPLETED
    )

    logger.debug("one task returned (%r), stopping all other", done)

    for fut in pending:
        fut.cancel()

    done, _ = await asyncio.wait(
        futures,
        return_when=asyncio.ALL_COMPLETED
    )

    logger.debug("all tasks stopped")

    for fut in futures:
        if (fut.cancelled() or
                isinstance(fut.exception(), asyncio.CancelledError)):
            continue
        fut.result()


if __name__ == "__main__":
    import argparse
    parser = argparse.ArgumentParser()

    parser.add_argument(
        "--loss",
        type=float,
        default=0,
    )
    parser.add_argument(
        "--max-buffer-size",
        type=int,
        default=16,
    )
    parser.add_argument(
        "--rx-bind",
        default="0.0.0.0",
        help="IP address to bind the receiver endpoint to"
    )
    parser.add_argument(
        "-v",
        dest="verbosity",
        action="count",
        default=0,
    )

    subparsers = parser.add_subparsers()

    subparser = subparsers.add_parser(
        "reliability-test"
    )
    subparser.set_defaults(scenario="reliability-test")
    subparser.add_argument(
        "--rate",
        type=float,
        default=1,
    )
    subparser.add_argument(
        "--count",
        type=int,
        default=100,
    )

    subparser = subparsers.add_parser(
        "receiver"
    )
    subparser.set_defaults(scenario="receiver")

    subparser = subparsers.add_parser(
        "app-request-test"
    )
    subparser.set_defaults(scenario="app-request-test")

    subparser = subparsers.add_parser(
        "set-ntp-server"
    )
    subparser.set_defaults(scenario="set-ntp-server")
    subparser.add_argument(
        "address",
        type=ipaddress.IPv4Address,
    )

    args = parser.parse_args()

    logging.basicConfig(level={
        0: logging.ERROR,
        1: logging.WARNING,
        2: logging.INFO,
    }.get(args.verbosity, logging.DEBUG))

    loop = asyncio.get_event_loop()
    try:
        loop.run_until_complete(amain(loop, args))
    finally:
        loop.close()
