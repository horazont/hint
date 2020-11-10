import io
import logging
import os
import struct

from datetime import datetime, timedelta

from hintlib import utils, timeline


def decompress(average, packet):
    def to_sint(v):
        return struct.unpack("<h", struct.pack("<H", v))[0]

    values = [to_sint(average)]

    remaining_payload_size = len(packet)

    bitmap = []
    while remaining_payload_size > 0:
        remaining_payload_size -= 1
        next_bitmap_part = packet[0]
        packet = packet[1:]

        for i in range(7, -1, -1):
            bit = (next_bitmap_part & (1 << i)) >> i
            bitmap.append(bit)
            if bit:
                remaining_payload_size -= 1
            else:
                remaining_payload_size -= 2
            if remaining_payload_size <= 0:
                if remaining_payload_size < 0:
                    print("reference:", average)
                    print("bitmap so far:", bitmap)
                    print("remaining packet:", packet)
                    raise ValueError(
                        "codec error: remaining payload is negative!"
                    )
                break

    for compressed in bitmap:
        if compressed:
            raw, = struct.unpack("<B", packet[:1])
            packet = packet[1:]
        else:
            raw, = struct.unpack("<H", packet[:2])
            packet = packet[2:]
        value = (raw + average) % 65536
        values.append(to_sint(value))

    assert not packet

    return values


class Buffer:
    """
    A frontend to a persistent (restart-safe) stream sample buffer.

    :param persistent_directory: Location where samples are stored before they
                                 are emitted.
    :type persistent_directory: :class:`pathlib.Path`

    .. method:: on_emit(rtc, period, samples, handle)

       A batch of samples is ready.

       :param rtc: The timestamp of the first sample.
       :type rtc: :class:`datetime.datetime`
       :param period: The interval between consecutive samples.
       :type period: :class:`datetime.timedelta`
       :param samples: The sample values
       :type samples: :class:`collections.abc.Sequence`
       :param handle: A handle object (see below)

       The `handle` object has a :meth:`close` method which must be called
       after the data has been successfully processed. Only then the data will
       be deleted from the persistent storage.

    """

    _header = struct.Struct(
        "<BQLHLc",
    )

    class _Handle:
        def __init__(self, path):
            super().__init__()
            self.__path = path

        def close(self):
            try:
                self.__path.unlink()
            except OSError:
                pass

    def __init__(self, persistent_directory,
                 on_emit,
                 *,
                 sample_type="H",
                 logger=None):
        if len(sample_type) != 1 or not (32 <= ord(sample_type[0]) <= 127):
            raise ValueError("invalid sample type")
        super().__init__()
        self.logger = logger or logging.getLogger(
            ".".join([__name__, type(self).__qualname__])
        )
        self.on_emit = on_emit
        persistent_directory.mkdir(exist_ok=True)
        self.__dirfd = os.open(str(persistent_directory), os.O_DIRECTORY)
        self.__path = persistent_directory / "current"

        self.__sample_type = sample_type
        self.__sample_struct = struct.Struct(
            # we only allow ascii here
            "<"+sample_type
        )
        self.batch_size = 1024

        self.__batch_seq_abs0 = None
        self.__batch_seq_rel0 = None
        self.__batch_data = None

        self.__timeline = timeline.Timeline(
            2**16,
            2**15,
        )
        self.__period = None
        self.__alignment_t0 = None
        self.__alignment_data = []
        self._emit_existing()

    def __del__(self):
        os.close(self.__dirfd)

    def reset(self):
        self._emit()
        self.__alignment_data.clear()
        self.__timeline.reset(0)

    def align(self, seq_rel, rtc, period):
        """
        Configure the mapping between sequence number and real-time clock.

        :param sequence: Sequence number of an arbitrary reference sample.
        :type sequence: :class:`int`
        :param rtc: Timestamp of the reference sample.
        :type rtc: :class:`datetime.datetime`
        :param period: Interval between two consecutive samples
        :type period: :class:`datetime.timedelta`

        The assignment of RTC times to sequence numbers is configured smoothly
        by using the most recent three calls to :meth:`align` to determine the
        mapping.

        A change to `period` causes current buffers to be emitted and the
        mapping to be reset.

        The reference sample addressed by `sequence` must be within 32768
        samples around the last submitted sample; otherwise, incorrect
        alignment will occur.
        """

        if self.__period != period:
            self.reset()
        self.__period = period

        offset = self.__timeline.feed_and_transform(seq_rel)
        self.__timeline.reset(seq_rel)
        if len(self.__alignment_data) > 999:
            del self.__alignment_data[0]

        for i in range(len(self.__alignment_data)):
            old_seq_abs, old_rtc = self.__alignment_data[i]
            self.__alignment_data[i] = (
                old_seq_abs - offset,
                old_rtc,
            )

        self.__alignment_data.append((0, rtc))

        expected = (
            self.__alignment_data[0][1] -
            self.__alignment_data[0][0]*self.__period
        )

        self.__alignment_t0 = rtc + sum(
            (
                (old_rtc-old_seq_abs*self.__period)-rtc
                for old_seq_abs, old_rtc
                in self.__alignment_data
            ),
            timedelta(0)
        ) / len(self.__alignment_data)

        self.logger.debug(
            "difference: %s%s; drift: %s%s",
            "-" if self.__alignment_t0 < rtc else "",
            abs(self.__alignment_t0 - rtc),
            "-" if expected < self.__alignment_t0 else "",
            abs(expected - self.__alignment_t0)
        )

        if self.__batch_seq_abs0 is not None:
            self.__batch_seq_abs0 -= offset

    def _buffer_samples(self, samples):
        try:
            f = self.__path.open("xb")
        except FileExistsError:
            f = self.__path.open("r+b")

        with f:
            f.seek(0)
            # we always re-write the header with current information
            f.write(self._make_header())
            f.seek(0, io.SEEK_END)
            f.writelines(
                self.__sample_struct.pack(sample)
                for sample in samples
            )
            os.fsync(f.fileno())

        os.fsync(self.__dirfd)

        # self.__timeline.forward(len(samples))
        self.__batch_data.extend(samples)

    def _get_batch_t0(self):
        return self.__alignment_t0 + self.__period * self.__batch_seq_abs0

    def _emit(self):
        if not self.__batch_data:
            return

        t0 = self.__alignment_t0 + self.__period * self.__batch_seq_abs0
        data = self.__batch_data
        nitems = len(data)
        self.__batch_data = []

        persistent = self.__path.parent / str(t0.isoformat())
        self.__path.rename(persistent)

        self.on_emit(
            t0,
            self.__batch_seq_rel0,
            self.__period,
            data,
            self._Handle(persistent)
        )

        self.__batch_seq_abs0 += nitems
        self.__batch_seq_rel0 = (self.__batch_seq_rel0 + nitems) % (2**16)
        self.__batch_data = []

    def submit(self, first_seq_rel, samples):
        """
        Submit samples into the buffer.

        :param first_seq: The sequence number of the first sample.
        :type first_seq: :class:`int`
        :param samples: Samples to submit
        :type samples: :class:`collections.abc.Iterable`

        If the first sequence number of the samples to submit is not the
        expected next sequence number, the current buffer contents are emitted,
        since the buffer does not handle discontinuities.
        """
        first_seq_abs = self.__timeline.feed_and_transform(
            first_seq_rel
        )

        if self.__batch_seq_rel0 is None:
            self.__batch_seq_rel0 = first_seq_rel
            self.__batch_seq_abs0 = first_seq_abs
            self.__batch_data = []

        if first_seq_abs != self.__batch_seq_abs0 + len(self.__batch_data):
            self._emit()
            self.__batch_seq_abs0 = first_seq_abs
            self.__batch_seq_rel0 = first_seq_rel

        samples = list(samples)
        while len(samples) + len(self.__batch_data) >= self.batch_size:
            to_submit = self.batch_size - len(self.__batch_data)
            self._buffer_samples(samples[:to_submit])
            self._emit()
            del samples[:to_submit]
        if samples:
            self._buffer_samples(samples)

    def _make_header(self):
        t0 = self._get_batch_t0()
        t0_s, t0_us = utils.decompose_dt(t0)

        return self._header.pack(
            0x00,  # version
            t0_s, t0_us,
            self.__batch_seq_rel0,
            round(self.__period.total_seconds() * 1e6),  # period
            self.__sample_type.encode("ascii"),  # sample type
        )

    def _parse_sample_data(self, f):
        version, t0_s, t0_us, seq0, period, sample_type = \
            utils.read_single(
                f,
                self._header
            )

        self.logger.debug(
            "found file with version %d",
            version,
        )

        if version != 0x00:
            self.logger.warning(
                "discarding data due to unsupported format"
            )
            return

        sample_type = sample_type.decode("ascii", errors="replace")
        t0 = datetime.utcfromtimestamp(t0_s).replace(microsecond=t0_us)
        period = timedelta(microseconds=period)
        data = [
            value
            for value, in utils.read_all(
                    f,
                    self.__sample_struct)
        ]

        self.logger.debug(
            "found %d samples starting at %r with period %s",
            len(data),
            t0,
            period,
        )

        return t0, seq0, period, data

    def _emit_existing(self):
        items = []

        for path in self.__path.parent.iterdir():
            try:
                f = path.open("rb")
            except OSError:
                self.logger.warning(
                    "failed to recover data from %s",
                    path,
                )
                try:
                    path.unlink()
                except OSError:
                    pass
                continue

            with f:
                try:
                    data = self._parse_sample_data(f)
                except EOFError:
                    data = None

            if data is not None:
                items.append((data, path))

        items.sort(key=lambda x: x[0])

        for (t0, seq0, period, data), path in items:
            self.on_emit(
                t0,
                seq0,
                period,
                data,
                self._Handle(path)
            )
