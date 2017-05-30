import contextlib
import pathlib
import re
import unittest
import unittest.mock

import hintd.cconstants as cconstants


CONST_DEFINE_RE = re.compile(
    "^#define\s+(?P<name>\w+)\s*\((?P<base>0x|0b|0)?(?P<value>[0-9a-f]+)U?L?\)\s*$",
    re.I
)

CONSTANTS = {}


BASEMAP = {
    None: 10,
    "0x": 16,
    "0b": 2,
    "0": 8,
}


def read_constants(header_name):
    with (pathlib.Path(__file__).parent.parent.parent /
          "common" / header_name).open("r") as f:
        for line in f:
            m = CONST_DEFINE_RE.match(line)
            if not m:
                continue
            groups = m.groupdict()
            CONSTANTS[groups["name"]] = int(
                groups["value"],
                BASEMAP[groups.get("base")]
            )

read_constants("comm.h")
read_constants("comm_arduino.h")
read_constants("comm_lpc1114.h")


class TestAddress(unittest.TestCase):
    def __init__(self, *args, **kwargs):
        super().__init__(*args, **kwargs)

        self.REFERENCE = {
            key: value
            for key, value in CONSTANTS.items()
            if key.startswith("MSG_ADDRESS_")
        }

    def test_values(self):
        for constant_name, value in self.REFERENCE.items():
            attr_name = constant_name.split("_", 2)[2]
            self.assertEqual(
                value,
                getattr(cconstants.Address, attr_name).value
            )


class TestFlag(unittest.TestCase):
    def __init__(self, *args, **kwargs):
        super().__init__(*args, **kwargs)

        self.REFERENCE = {
            key: value
            for key, value in CONSTANTS.items()
            if key.startswith("MSG_FLAG_")
        }

    def test_values(self):
        self.assertTrue(self.REFERENCE)
        for constant_name, value in self.REFERENCE.items():
            attr_name = constant_name.split("_", 2)[2]
            self.assertEqual(
                value,
                getattr(cconstants.Flag, attr_name).value
            )


class TestArduinoSubjects(unittest.TestCase):
    def __init__(self, *args, **kwargs):
        super().__init__(*args, **kwargs)

        self.REFERENCE = {
            key: value
            for key, value in CONSTANTS.items()
            if key.startswith("ARD_SUBJECT_")
        }

    def test_values(self):
        self.assertTrue(self.REFERENCE)
        for constant_name, value in self.REFERENCE.items():
            attr_name = constant_name.split("_", 2)[2]
            self.assertEqual(
                value,
                getattr(cconstants.ArduinoSubject, attr_name).value
            )


class TestLPCCommands(unittest.TestCase):
    def __init__(self, *args, **kwargs):
        super().__init__(*args, **kwargs)

        self.REFERENCE = {
            key: value
            for key, value in CONSTANTS.items()
            if key.startswith("LPC_CMD_")
        }

    def test_values(self):
        self.assertTrue(self.REFERENCE)
        for constant_name, value in self.REFERENCE.items():
            attr_name = constant_name.split("_", 2)[2]
            self.assertEqual(
                value,
                getattr(cconstants.LPCCommand, attr_name).value
            )


class TestLPCSubjects(unittest.TestCase):
    def __init__(self, *args, **kwargs):
        super().__init__(*args, **kwargs)

        self.REFERENCE = {
            key: value
            for key, value in CONSTANTS.items()
            if key.startswith("LPC_SUBJECT_")
        }

    def test_values(self):
        self.assertTrue(self.REFERENCE)
        for constant_name, value in self.REFERENCE.items():
            attr_name = constant_name.split("_", 2)[2]
            self.assertEqual(
                value,
                getattr(cconstants.LPCSubject, attr_name).value
            )


class TestLPCFonts(unittest.TestCase):
    def __init__(self, *args, **kwargs):
        super().__init__(*args, **kwargs)

        self.REFERENCE = {
            key: value
            for key, value in CONSTANTS.items()
            if key.startswith("LPC_FONT_")
        }

    def test_values(self):
        self.assertTrue(self.REFERENCE)
        for constant_name, value in self.REFERENCE.items():
            attr_name = constant_name.split("_", 2)[2]
            self.assertEqual(
                value,
                getattr(cconstants.LPCFont, attr_name).value
            )


class Testconstants(unittest.TestCase):
    def test_MAX_PAYLOAD_LENGTH(self):
        self.assertEqual(
            cconstants.MAX_PAYLOAD_LENGTH,
            CONSTANTS["MSG_MAX_PAYLOAD"]
        )

    def test_MAX_ADDRESS(self):
        self.assertEqual(
            cconstants.MAX_ADDRESS,
            CONSTANTS["MSG_MAX_ADDRESS"]
        )


class Testcompose_message_header(unittest.TestCase):
    def test_some(self):
        references = [
            ((cconstants.Address.HOST, cconstants.Address.LPC1114, 13, set()),
             b'\x00\x01\x0d\x00'),
            ((cconstants.Address.ARDUINO, cconstants.Address.LPC1114, 74,
              {cconstants.Flag.ACK}),
             b'\x00\x21\x4a\x10'),
        ]

        for ((sender, recipient, payload_length, flags),
             reference) in references:
            self.assertEqual(
                cconstants.compose_message_header(
                    sender,
                    recipient,
                    payload_length,
                    flags,
                    0,
                ),
                reference
            )

    def test_message_id(self):
        references = [
            ((cconstants.Address.HOST, cconstants.Address.LPC1114, 13, set(),
              1),
             b'\x00\x01\x0d\x01'),
            ((cconstants.Address.ARDUINO, cconstants.Address.LPC1114, 74,
              {cconstants.Flag.ACK}, 2),
             b'\x00\x21\x4a\x12'),
        ]

        for ((sender, recipient, payload_length, flags, message_id),
             reference) in references:
            self.assertEqual(
                cconstants.compose_message_header(
                    sender,
                    recipient,
                    payload_length,
                    flags,
                    message_id,
                ),
                reference
            )

    def test_reject_out_of_range_message_id(self):
        with self.assertRaisesRegex(ValueError, "message id \(16\) out of bounds"):
            cconstants.compose_message_header(
                cconstants.Address.HOST,
                cconstants.Address.LPC1114,
                cconstants.MAX_PAYLOAD_LENGTH,
                set(),
                0x10
            )

    def test_reject_too_long_payload(self):
        with self.assertRaisesRegex(ValueError, "payload too long"):
            cconstants.compose_message_header(
                cconstants.Address.HOST,
                cconstants.Address.LPC1114,
                cconstants.MAX_PAYLOAD_LENGTH+1,
                set(),
                0
            )

        cconstants.compose_message_header(
            cconstants.Address.HOST,
            cconstants.Address.LPC1114,
            cconstants.MAX_PAYLOAD_LENGTH,
            set(),
            0
        )


class Testdecompose_message_header(unittest.TestCase):
    def test_some(self):
        references = [
            ((cconstants.Address.HOST, cconstants.Address.LPC1114, 13,
              set(), 0),
             b'\x00\x01\x0d\x00'),
            ((cconstants.Address.ARDUINO, cconstants.Address.LPC1114, 74,
              {cconstants.Flag.ACK}, 0),
             b'\x00\x21\x4a\x10'),
            ((cconstants.Address.ARDUINO, cconstants.Address.LPC1114, 74,
              {cconstants.Flag.RESET}, 0),
             b'\x00\x21\x4a\xff'),
            ((cconstants.Address.HOST, cconstants.Address.LPC1114, 13,
              set(), 1),
             b'\x00\x01\x0d\x01'),
            ((cconstants.Address.ARDUINO, cconstants.Address.LPC1114, 74,
              {cconstants.Flag.ACK}, 2),
             b'\x00\x21\x4a\x12'),
        ]

        for reference, header in references:
            self.assertEqual(
                cconstants.decompose_message_header(
                    header
                ),
                reference,
                header,
            )


class Testadler8ish(unittest.TestCase):
    def test_some(self):
        references = [
            (b'\x61\x2a\xef\xa8\x58\x78\xf6\x3b\xb7\xf3\xca\xef\x73\xb0\x69\xcf\x40\xb5\x92\x05\xca\xf2\x0e\x54\x44\x7e\x01\x5c\x22\x17\xd6\x8d\x54\x28\x29\xb4\x7f\xae\x27\x65\x1d\x41\x79\x5e\xec\x4b\xa0\xae\x5d\x7e\x83\x5c\xa7\x21\xe6\x15\x2b\xb5\xf9\x85\x67\x6c\xb6\x98\x31\x10\xdc\x0e\xee\xa2\x43\xd6\x0d\x4d\x4c\x24\x7e\xf9\x85\x9f\xb5\xa2\xab\xdd\x41\xfd\xbf\xda\x3a\x0a\xd1\x3c\x74\xa9\xd3\x91\xff\x59\x1c\x41\x6f\x02\xc8\xef\x53\x4b\x75\x6f\xc7\xf3\xc1\x3f\x46\x42\x6a\x45\x4c\xb5\xc7\x37\xe9\xa9\x66\x90\x44\xb1\x70\xe7\x35\xda\xc9\x5f\x66\xd8\x5c\x61\xaa\x9e\xd3\x20\xb4\x82\x8f\x7a\x3d\x3d\xe5\x6d\x14\xd6\x23\x1f\xa8\xa8\x2e\xe2\x5d\x66\x3a\xe6\x23\x2a\xcc\xca\xc3\x85\xab\x6f\x40\x83\xf2\x83\xdb\x10\x02\x6b\x00\xb3\x3c\xca\x2f\x0d\x40\x70\x57\x7d\x1d\x4f\xbf\x4b\x52\xe5\x94\x6a\x77\x70\xd5\xad\xa3\xe4\x82\xe6\x1b\xa2\x66\xd2\x5e\x51\x53\x18\x85\x3d\xb6\x8b\xdb\xdf\x78\x89\x06\xa4\x52\xa7\x01\x11\x49\xb9\x24\x8e\x1e\xd0\x03\xa4\xfd\x46\x37\x12\x7c\xe2\xf4\x22\xa3\x2d\xcc\xed\x41\x6f\xe4\x4d\x2e\xfd\xc7\xc0\x9e\x72', 0x25),  # NOQA
            (b'\xc8\x96\x7f\xf0\x04\x1e\xe1\x0b\x5e\x21\x6b\x81\x78\x09\x29\xd9\x89\xf8\x6c\x96\x2d\x70\xe5\x5c\x94\x19\xc1\x2a\x74\x7a\xb3\xff\xf0\x0c\xf0\xfd\x1c\x8c\x82\xa1\xba\x5b\x1c\x69\x97\x4f\x18\x50\xf5\xa7\xdb\x30\x8b\xf5\xa4\xeb\x6c\x3b\x68\x1e\xde\x40\x71\x1e\xda\x73\xbd\xb7\x07\x7e\x29\x3e\x8f\xc1\x05\xc7\xe5\xb7\xaf\x4f\xe2\x0b\x12\x82\x7c\x47\x43\xea\x7c\x41\xb1\x4e\xb4\x87\xf0\xe6\xef\xe3\x27\xb1\x42\xc5\x6b\x15\x89\x25\x67\xb9\x63\xb9\x74\x42\xbb\x99\x1d\x69\xdf\x75\xce\x6c\x83\xce\x51\xcd\xb8\xac\x7e\x43\x1d\x22\x3a\x94\x70\x9a\x7f\x55\x95\x12\xe7\xe1\x32\xdb\x22\x5f\x1d\x6f\xf1\x41\x09\x5d\x2d\x32\x63\x37\x1d\x0b\x1e\x50\x4e\x88\x9a\xcd\xc9\x1b\xce\x7e\x18\x18\x9f\x94\xba\xbb\xb5\x56\xf9\x82\x14\x54\xd3\x9a\xc6\x24\x61\x1f\xc4\x0c\x5f\xa3\x7e\xd7\xc0\xd2\x9f\x95\xbe\xb8\x96\x37\x0d\xb9\x83\xc7\xf8\x70\xfb\x20\x61\x98\x49', 0x63),  # NOQA
            (b'\xde\xf3\xda\xa2\xe8\x7b\x5e\xe7\xae\xb3\x18\xd8\xd1\xa7\xb1\x0f\x38\x15\x36\x9b\xb8\x9b\x86\xc2\xc1\xb7\xcb\x6c\x27\xdb\x1a\x82\xbd\x17\x83\xe9\x0c\x39\x4b\x13\x87\x8d\xa9\x6f\x93\x82\xce\x2d\x3a\x3e\xb9\xe7\x74\x0d\x22\x70\x5d\x51\x1e\xbe\x67\x09\x6f\xf2\x6a\x6a\x19\x95\x83\xbd\x88\xe4\x06\xec\x27\xff\xbd\x2c\x72\x4e\x45\x48\xc2', 0x5b),  # NOQA
            (b'\x42\xe1\xc7\xa9\x0d\x8f\x23\x60\x54\x02\xcb\x45\x10\x7b\xb6\x2f\x4d\xcd\x9d\xc1\xd8\xde\x69\x07\x97\x2e\x3f\x3f\xee\x94\x3f\x22\x0a\xd3\xb5\x0f\xca\xc0\x07\x4b\xbc\xd1\x85\x18\x71\xbc\xef\xfd\xab\xf8\x3a\x9d\x6b\xd7\x16\x11\xff\x5d\x4d\xde\x33\x3d\x59\x06\x52\xf2\xdd\xfe\xfd\x73\xd3\x82\xfa\x3d\xc5\x98\x6a\x56\x02\xa7\x7d\xca\x5f\x9a\x15\x37\xb0\xce\x98\xc7\xb2\x0d', 0xc8),  # NOQA
            (b'\x8d\x43\x9c\xbf\x69\xb8\x5a\xab\x76\x5b\xda\x76\x81\x49\x9f\xb2\x06\xc7\xcd\x1b\x1d\x13\xc6\xb2\x46\xd7\x7e\x1f\x5e\x6e\xa6\x44\x20\xba\xc2\xc0\x8b\xcc\x78\xdf\x2b\x08\x88\x65\x8e\x89\xc2\x9f\xb6\xc5\x1b\x8b\xdf\x28\x55\xa7\xc8\xa4\xfc\x65\x23\x61\x8e\xab\x92\xcb\x3e\x90\x53\x34\xf3\x1a\xd8\xbb\x6c\xfb\x85\x9b\xda\x62\x2d\x0c\x0a\x85\x4c\xa5\x63\x2b\x45\x18\xa0\xe6\xe6\x2d\x0e\xfc\x0d\x96\xef\x47\x1d\x19\x0b', 0x2a),  # NOQA
            (b'\x76\x20\x9e\x94\x8e\xa2\xa8\x64\xb1\x29\xaf\x99\x43\xca\xa5\x78\x1e\x7e\x16\xac\x01\x27\x72\xb0\xc8\xaa\x07\xea\x5e\x02\x9b\x1a\x84\x5d\xb0\xd9\x42\xac\xee\x26\xe3\x22\x98\xf7\x1e\xb1\x48\xd1\x70\xd8\x53\xb6\x51\xfb\x8d\xdc\xcf\xb3\x5d\x47\x50\x7e\x48\x7f\xae\xa0\xc0\xcd\xce\xc4\xa0\xe2\x3c\x67\x00\x0d\x2a\xe1\x12\x23\xfe\x20\x21\xac\x0d\x98\x72\x5f\x0a\xf0', 0xc1),  # NOQA
            (b'\x49\xc2\x18\x9d\xb4\xb9\x6b\x2c\x70\xef\x57\x9a\xd6\xa0\x57\x26\x74\x90\xbb\x9d\x22\xad\x7a\x53\xff\x74\x73\xe0\x7b\xd0\x9f\x22\xaf\xbf\x2b\x5d\x10\x3d\xe7\x74\x18\x8b\xa0\x4a\x9e\x4c\x59\x33\x2f\x7e\xd8\x82\xee\x35\x85\xd0\x17\x5b\x85\x9c\x41\x4a\x44\xdc\x47\x8c\x9a\xf9\x60\x98\xb3\xaf\x68\x93\xf2\x5e\xbb\x98\x04\xf6\xc3\x4a\x7e\x2b\x82\xcb\x5a\xf1\x18\x4c\x3c\xfd\x4d\xed\x1c\x74\x9c\x3f\x73\x5e\x26\x58\xce\x26\x76\x5c\x27\x22\x57\x19\x0a\xa1\xe6\xa8\x7d\x56\x03\xeb\x40\x68\xa5\xa5\xb8\xf1\x67\x2b\x5b\x11\xbd\x78\xb2\xd1', 0x14),  # NOQA
            (b'\x1a\xc2\x47\xcd\xd1\x20\xd1\x7f\x08\xcb\x04\x60\xb4\x40\x88\xca\x8e\xe9\x54\xd8\xdd\xc1\xcf\x99\x7e\xca\x06\x15\xc6\x1b\xff\xa4\x20\xe0\xe4\x96\xa5\xc6\x16\x1a\x94\x65\x69\x83\x73\xb0\x37\x0a\x0d\x8b\x1f\x1e\xe2\xc2\xd4\x74\x0e\x93\x43\x7a\x26\x19\x81\x4d\xdf\xcf\xca\x34\x02\xff\x44\x5e\x45\xc6\x6f\xa3\xe9\x99\xb9\xc3\x05\x58\x93\x2f\xc1\xd4\x83\x0a\xab\x90\x41\xb5\x0e\x42\x1c\x15\x4e\x27\x63\xf4\xae\x22\xd8\x9b\x8c\x55\x40\x2a\xef\x26\x51\x6a\x88\xee\x57\x2f\xd6\x7b\x4f\x81\x87\x20\x55\x56\xd7\xd1\xa8\xb7\x53\x39\x1d\x27\x61\xd7\x4c\x2a\xcd\xc1\xb4\xf1\x97\xdb\x58\x48\x26\xef\x77\xc7\x4a\x40\xe9\xfd\x27\xee\x5b\x0c\xbb\x43\xb5\x87\xd6\x22\x47\xc1\xea\x62\x24\x46\xeb\x5d\xec\x27\x84\x03\x5b\x17\x3d\xa8\x42\x3f\xcc\xa2\x97\xbd\xac\x7b\xa0\xac\xa0\x21\x5d\x84\x2e\x51\x20\xcc\x6f\x3c\x77\x88\x50\x2a\xd5\xa4\x60\x3a\x4d\x6b\x33\xa3\x34\xb9\x91\xd7\x65\x66\xa8\x64\x06\x93\x4f\x2f\xb1\xef\xe8\x31\x3c\x2e\x27\x7e\x21\x3f\xb8\x41\x42\x8f\x6e\x17\x76\xc3\x1a\xd3\x2d\x93\x9e\xcb\x3f\x70\x72\xe0\x0b', 0x81),  # NOQA
            (b'\x43\xf3\x7f\x4c\xf6\xb5\xa4\x81\xca\xeb\xcf\xb4\xb0\x3f\x06\xe4\xa2\x25\x35\x30\x4f\xef\x12\x75\xd6\x98\x0d\x4f\x3f\x59\x01\xe7\xa9\x9e\x26\x90\x11\x17\x6c\x6a', 0x19),  # NOQA
            (b'\x2d\xc0\x91\x1d\x9b\x9c\x53\x66\x21\x48\x3e\x41\xed\xf0\x8a\x0b\x77\x77\x0a\x31\xe4\xb2\x85\xcc\x87\xce\xeb\x93\xf0\x12\x37\x64\x9f\x9e\xad\x6c\xc4\xbc\x84\x37\x51\x8f\x8f\x53\x42\xca\xea\x0d\xfd\xfc\x39\x27\xef\x77\x18\xfc\xba\x4d\xd5\xa5\xe9\xc3\x4c\xeb\x6f\x8b\xf4\xe4\x08\x74\xc7\x2c\x48\xf9\xcb\x1c\xff\x57\xfe\xd6\x1a', 0x18),  # NOQA
        ]

        for data, checksum in references:
            self.assertEqual(
                cconstants.adler8ish(data),
                checksum,
                data
            )


class Testcompose_message(unittest.TestCase):
    def test_composes_header_payload_and_cs(self):
        payload = b"payload"

        with contextlib.ExitStack() as stack:
            cmh = stack.enter_context(
                unittest.mock.patch(
                    "hintd.cconstants.compose_message_header"
                )
            )
            cmh.return_value = b"header"

            adler8ish = stack.enter_context(
                unittest.mock.patch(
                    "hintd.cconstants.adler8ish"
                )
            )
            adler8ish().to_bytes.return_value = b"checksum"
            adler8ish.mock_calls.clear()

            result = cconstants.compose_message(
                unittest.mock.sentinel.sender,
                unittest.mock.sentinel.recipient,
                unittest.mock.sentinel.flags,
                payload,
                unittest.mock.sentinel.message_id,
            )

        cmh.assert_called_with(
            unittest.mock.sentinel.sender,
            unittest.mock.sentinel.recipient,
            len(payload),
            unittest.mock.sentinel.flags,
            unittest.mock.sentinel.message_id,
        )

        self.assertSequenceEqual(
            adler8ish.mock_calls,
            [
                unittest.mock.call(payload),
                unittest.mock.call().to_bytes(1, "little"),
            ]
        )

        self.assertEqual(
            result,
            b"headerpayloadchecksum",
        )
