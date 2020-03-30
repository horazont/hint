import functools
import operator
import typing

from enum import Enum


class Address(Enum):
    HOST = 0x0
    LPC1114 = 0x1
    ARDUINO = 0x2


class Flag(Enum):
    ACK = 0x10
    NAK_CODE_ORDER = 0x20
    NAK_CODE_UNKNOWN_COMMAND = 0x40
    NAK_OUT_OF_MEMORY = 0x60
    ECHO = 0x80
    RESET = 0xFF


class ArduinoSubject(Enum):
    SENSOR_READOUT = 1


class LPCCommand(Enum):
    FILL_RECT = 0x01
    DRAW_RECT = 0x02
    DRAW_IMAGE_START = 0x03
    DRAW_IMAGE_DATA = 0x04
    RESET_STATE = 0x06
    DRAW_TEXT = 0x07
    TABLE_START = 0x08
    TABLE_ROW = 0x09
    DRAW_LINE = 0x0B
    SET_BRIGHTNESS = 0x0C
    LULLABY = 0x0D
    WAKE_UP = 0x0E
    TABLE_ROW_EX = 0x0F


class LPCSubject(Enum):
    TOUCH_EVENT = 0x01


class LPCFont(Enum):
    DEJAVU_SANS_9PX = 0x10
    DEJAVU_SANS_12PX = 0x20
    DEJAVU_SANS_12PX_BF = 0x21
    CANTARELL_20PX_BF = 0x31


class LPCTableAlignment(Enum):
    LEFT = 0
    RIGHT = 1
    CENTER = 2


class TableColumnEx(typing.NamedTuple):
    bgcolour: typing.Tuple[int, int, int]
    fgcolour: typing.Tuple[int, int, int]
    alignment: LPCTableAlignment
    text: str


MAX_PAYLOAD_LENGTH = 0xfa
MAX_ADDRESS = 0x3


HDR_MASK_FLAGS = 0xff000000
HDR_SHIFT_FLAGS = 24
HDR_MASK_PAYLOAD_LENGTH = 0x00ff0000
HDR_SHIFT_PAYLOAD_LENGTH = 16
HDR_MASK_SENDER = 0x00003000
HDR_SHIFT_SENDER = 12
HDR_MASK_RECIPIENT = 0x00000300
HDR_SHIFT_RECIPIENT = 8
HDR_MASK_RESERVED = 0x000000ff
HDR_SHIFT_RESERVED = 0


def compose_message_header(sender, recipient, payload_length, flags,
                           message_id):
    if payload_length > MAX_PAYLOAD_LENGTH:
        raise ValueError("payload too long ({} > {})".format(
            payload_length,
            MAX_PAYLOAD_LENGTH
        ))

    if not (0 <= message_id <= 0xf):
        raise ValueError("message id ({!r}) out of bounds".format(
            message_id
        ))

    flags_int = functools.reduce(
        operator.or_,
        (flag.value for flag in flags),
        0
    ) | (message_id & 0xF)

    result = 0
    result |= (sender.value << HDR_SHIFT_SENDER) & HDR_MASK_SENDER
    result |= (recipient.value << HDR_SHIFT_RECIPIENT) & HDR_MASK_RECIPIENT
    result |= (flags_int << HDR_SHIFT_FLAGS) & HDR_MASK_FLAGS
    result |= ((payload_length << HDR_SHIFT_PAYLOAD_LENGTH) &
               HDR_MASK_PAYLOAD_LENGTH)

    return result.to_bytes(4, "little")


def decompose_message_header(header):
    as_int = int.from_bytes(header, "little")

    flags = set()

    flags_int = (as_int & HDR_MASK_FLAGS) >> HDR_SHIFT_FLAGS
    if flags_int == Flag.RESET.value:
        flags.add(Flag.RESET)
        message_id = 0
    else:
        for flag in Flag:
            if flag.value & flags_int == flag.value:
                flags.add(flag)
        message_id = flags_int & 0xF

    sender = Address((as_int & HDR_MASK_SENDER) >> HDR_SHIFT_SENDER)
    recipient = Address((as_int & HDR_MASK_RECIPIENT) >> HDR_SHIFT_RECIPIENT)
    payload_length = ((as_int & HDR_MASK_PAYLOAD_LENGTH) >>
                      HDR_SHIFT_PAYLOAD_LENGTH)

    return sender, recipient, payload_length, flags, message_id


def adler8ish(data):
    A, B = 1, 0
    for byte in data:
        A = (A + byte) % 13
        B = (A + B) % 13

    return (A << 4) | B


def compose_message(sender, recipient, flags, payload, message_id):
    header = compose_message_header(
        sender, recipient, len(payload), flags,
        message_id
    )
    if len(payload) == 0:
        return header

    checksum = adler8ish(payload)

    return b"".join([
        header,
        payload,
        checksum.to_bytes(1, "little"),
    ])


def rgb16_to_rgb24(value):
    r = (value >> 11) & 0x1F
    g = (value >> 5) & 0x3F
    b = value & 0x1F
    return round(r/31*255), round(g/63*255), round(b/31*255)


def rgb24_to_rgb16(r, g, b):
    r = max(min(round(r/255*31), 31), 0)
    g = max(min(round(g/255*63), 63), 0)
    b = max(min(round(b/255*31), 31), 0)
    return (r << 11) | (g << 5) | b
