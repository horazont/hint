import asyncio
import functools
import logging
import pathlib
import struct
import typing

from datetime import datetime, timedelta

from hintlib.utils import unpack_and_splice, escape_path
from hintlib import sample, timeline

import metric_relay.snurl

from ..base import DataClass, Source, DataChunk

from . import stream, wireformat, config


SENDER_PORT = 7285
RECEIVER_PORT = 7284


app_req_set_sntp_server_fmt = struct.Struct(
    "<"
    "4s"
)


data_frame_header_fmt = struct.Struct(
   "<"
   "L"  # rtc timestamp
   "B"  # type
)


def rtcify_samples(
        samples: typing.Iterable[sample.RawSample],
        rtcifier: timeline.RTCifier
        ) -> typing.Iterable[sample.Sample]:
    for s in samples:
        if isinstance(s.timestamp, datetime):
            yield sample.Sample(
                timestamp=s.timestamp,
                sensor=s.sensor,
                value=s.value,
            )
        else:
            yield sample.Sample(
                timestamp=rtcifier.map_to_rtc(s.timestamp),
                sensor=s.sensor,
                value=s.value,
            )


def deenumify_path(path):
    return path.replace(
        part=path.part.value,
        subpart=path.subpart.value if path.subpart else None,
    )


def deenumify_samples(samples):
    for s in samples:
        yield s.replace(
            sensor=deenumify_path(s.sensor)
        )


def batch_samples(
        samples: typing.Iterable[sample.Sample]
        ) -> typing.Iterable[sample.SampleBatch]:
    curr_batch_ts = None
    curr_batch_bare_path = None
    curr_batch_samples = {}
    for s in samples:
        bare_path = s.sensor.replace(subpart=None)
        if (curr_batch_ts != s.timestamp or
                curr_batch_bare_path != bare_path):
            if curr_batch_samples:
                yield sample.SampleBatch(
                    timestamp=curr_batch_ts,
                    bare_path=curr_batch_bare_path,
                    samples=curr_batch_samples,
                )
                curr_batch_samples = {}

            curr_batch_ts = s.timestamp
            curr_batch_bare_path = bare_path

        if s.sensor.subpart in curr_batch_samples:
            raise RuntimeError

        curr_batch_samples[s.sensor.subpart] = s.value

    if curr_batch_samples:
        yield sample.SampleBatch(
            timestamp=curr_batch_ts,
            bare_path=curr_batch_bare_path,
            samples=curr_batch_samples,
        )


class StreamProcessor:
    __slots__ = ("buffer_", "range_")

    def __init__(self, cfg, default_batch_size, emit_cb, logger):
        super().__init__()
        self.buffer_ = stream.Buffer(
            None,
            emit_cb,
            sample_type=cfg["sample_type"],
            logger=logger,
        )
        self.buffer_.batch_size = cfg.get("batch_size", default_batch_size)
        self.range_ = cfg["range"]


class SNURLSBXSource(Source):
    def __init__(self, logger,
                 snurl: metric_relay.snurl.Protocol,
                 cfg: typing.Mapping,
                 **kwargs):
        super().__init__(logger, **kwargs)
        self._snurl = snurl
        self._snurl.on_data_received.connect(self._data_received)
        self._snurl.on_resync.connect(self._resync)

        cfg = config.sbx_source_schema.validate(cfg)

        self._streams = {
            path: StreamProcessor(
                stream_cfg,
                cfg["default_stream_batch_size"],
                functools.partial(self._on_stream_emit, path),
                self.logger.getChild("buffers.{}".format(path)),
            )
            for path, stream_cfg in (
                (sample.SensorPath(
                    part=stream_cfg["part"],
                    instance=stream_cfg["instance"],
                    subpart=stream_cfg["subpart"],
                ), stream_cfg) for stream_cfg in cfg["streams"]
            )
        }

        self._reset_rtc_state()

    def _reset_rtc_state(self):
        self._had_status = False
        self._pre_status_buffer = []
        self._timeline = timeline.Timeline(
            2**16,  # wraparound
            30000,  # 30s slack
        )
        self._rtcifier = timeline.RTCifier(
            self._timeline,
            self.logger.getChild("rtcifier")
        )

        for buf in self._stream_buffers.values():
            buf.reset()

    @property
    def emitted_data_classes(self) -> typing.Iterable[DataClass]:
        return {DataClass.SAMPLE_BATCH, DataClass.STREAM}

    def _data_received(self, remainder: bytes):
        remainder, (rtc_timestamp, type_raw) = unpack_and_splice(
            remainder,
            wireformat.data_frame_header_fmt,
        )

        rtc_timestamp = datetime.utcfromtimestamp(rtc_timestamp)

        try:
            type_ = wireformat.DataFrameType(type_raw)
        except ValueError:
            self.logger.error("invalid data frame type: %r", type_raw)
            return

        if type_ == wireformat.DataFrameType.SBX:
            try:
                obj = wireformat.decode_sbx_message(remainder)
            except Exception:  # NOQA
                self.logger.warning("failed to decode SBX message",
                                    exc_info=True)
            else:
                self._process_message(
                    rtc_timestamp,
                    obj,
                )
        elif type_ == wireformat.DataFrameType.ESP_STATUS:
            try:
                obj = wireformat.ESPStatusMessage.from_buf(
                    rtc_timestamp,
                    remainder,
                )
            except Exception:
                self.logger.warning("failed to decode ESP status message %r",
                                    remainder,
                                    exc_info=True)
            else:
                self._process_message(
                    rtc_timestamp,
                    obj,
                )
        else:
            self.logger.debug("no handler for %s data frame",
                              type_)

    def _process_generic_message(self, obj):
        if hasattr(obj, "get_samples"):
            batches = list(batch_samples(
                rtcify_samples(
                    map(
                        self._individual_rewriter.rewrite,
                        deenumify_samples(obj.get_samples())
                    )
                )
            ))
            self._emit_cb(
                DataChunk.from_sample_batches(batches),
                lambda: None,
            )

        elif isinstance(obj, wireformat.SensorStreamMessage):
            spath = deenumify_path(obj.path)
            try:
                proc = self._streams[spath]
            except KeyError:
                self.logger.warning(
                    "DATA LOSS: no stream processor configured for sensor %s",
                    spath,
                )
            else:
                proc.buffer_.submit(obj.seq, obj.data)

    def _process_status_message(self, rtc_timestamp: datetime, obj):
        now = datetime.utcnow()
        retardation = now - rtc_timestamp
        if retardation > timedelta(seconds=60):
            # suspicious, discard
            self.logger.debug(
                "discarding status package which is late by %s",
                retardation
            )
            return

        self._had_status = True
        self._rtcifier.align(
            rtc_timestamp,
            obj.uptime
        )

        for subpart, state in zip(["accel", "compass"],
                                  [obj.v1_accel_stream_state,
                                   obj.v1_compass_stream_state]):
            seq = state.sequence_number
            rtc = self._rtcifier.map_to_rtc(state.timestamp)
            period = state.period
            for axis in "xyz":
                stream_buffer = self._stream_buffers[
                    sample.SensorPath(
                        sample.Part.LSM303D,
                        0,
                        sample.LSM303DSubpart("{}-{}".format(subpart, axis))
                    )
                ]
                stream_buffer.align(seq, rtc, period)

    def _process_message(self, rtc_timestamp: datetime, obj):
        if obj.type_ == wireformat.MsgType.STATUS:
            self._process_status_message(rtc_timestamp, obj)

        if not self._had_status:
            self._pre_status_buffer.append(obj)
            return

        if self._pre_status_buffer:
            msgs = self._pre_status_buffer
            self._pre_status_buffer = []
            for msg in msgs:
                self._process_generic_message(msg)

        self._process_generic_message(obj)

    def _on_stream_emit(self, path, t0, seq0, period, data, handle):
        range_ = self._streams[path].range_
        block = sample.StreamBlock(
            timestamp=t0,
            path=path,
            seq0=seq0,
            period=period,
            data=data,
        )
        self._emit_cb(
            DataChunk.from_stream_block(block),
            handle.close,
        )

    def _resync(self):
        self._reset_rtc_state()
