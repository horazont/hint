import base64
import calendar
import dataclasses
import functools
import enum
import itertools
import operator
import re
import typing

from datetime import datetime

import schema

import aiohttp

import hintlib.sample

from . import interface


class Precision(enum.Enum):
    AUTO = ""
    NANOSECONDS = "ns"
    MICROSECONDS = "u"
    MILLISECONDS = "ms"
    SECONDS = "s"


class AuthMode(enum.Enum):
    PARAMETER = "parameter"
    HTTP = "http"


def enum_type(t):
    @functools.wraps(t)
    def wrapper(v):
        return t(v)
    return wrapper


_AUTH_SCHEMA = schema.Schema({
    "username": str,
    "password": str,
    "mode": enum_type(AuthMode),
})


_TRANSPORT_SCHEMA = schema.Schema({
    "api_url": str,
    "api_version": "v1",
    schema.Optional("auth", default=None): _AUTH_SCHEMA,
})


_SINK_SCHEMA = schema.Schema({
    "database": str,
    schema.Optional("retention_policy", default=None): str,
    schema.Optional("auth", default=None): _AUTH_SCHEMA,
    schema.Optional("precision", default=Precision.AUTO): enum_type(Precision),
})


@dataclasses.dataclass(frozen=True)
class AuthConfig:
    username: str
    password: str
    mode: AuthMode

    def update_args(self, headers, params):
        if self.mode == AuthMode.HTTP:
            headers["Authorization"] = "Basic {}".format(
                base64.b64encode(
                    "{}:{}".format(
                        self.username,
                        self.password,
                    ).encode("utf-8")
                ).decode("ascii"),
            )
        else:
            params["u"] = self.username
            params["p"] = self.password


@dataclasses.dataclass(frozen=True)
class TransportConfig:
    api_url: str
    auth: typing.Optional[AuthConfig]


@dataclasses.dataclass(frozen=True)
class SinkConfig:
    database: str
    retention_policy: typing.Optional[str]
    precision: Precision
    auth: typing.Optional[AuthConfig]


InfluxDBValue = typing.Union[str, int, float, bool]


_ESCAPE_MEASUREMENT = re.compile(r"([\\,\s])")
_ESCAPE_NAME = re.compile(r"([\\,\s=])")
_ESCAPE_STR = re.compile(r'([\\"])')


def _escape_re(rx: re.Pattern, s: str) -> str:
    return rx.subn(r"\\\1", s)[0]


def escape_measurement(s: str) -> str:
    return _escape_re(_ESCAPE_MEASUREMENT, s)


def escape_name(s: str) -> str:
    return _escape_re(_ESCAPE_NAME, s)


def escape_str(s: str):
    return '"{}"'.format(_escape_re(_ESCAPE_STR, s))


def encode_field_value(v: InfluxDBValue) -> bytes:
    if isinstance(v, bool):
        return str(v).encode("ascii")
    if isinstance(v, str):
        return escape_str(v).encode("utf-8")
    if isinstance(v, int):
        return f"{v!r}i".format(v).encode("ascii")
    if isinstance(v, float):
        return repr(v).encode("ascii")
    raise TypeError(f"not a valid InfluxDBValue (type {type(v)}): {v!r}")


def encode_measurement_name(v: str) -> bytes:
    return escape_measurement(v).encode("utf-8")


def encode_tag_part(v: str) -> bytes:
    return escape_name(v).encode("utf-8")


def encode_field_key(v: str) -> bytes:
    return escape_name(v).encode("utf-8")


def _divround(v: int, divisor: int) -> int:
    assert divisor % 2 == 0
    v, remainder = divmod(v, divisor)
    if remainder >= divisor // 2:
        v += 1
    return v


def encode_timestamp(dt: datetime, ns_part: int,
                     precision: Precision) -> bytes:
    if precision == Precision.AUTO:
        raise ValueError("auto precision not supported for encoding")
    if not (0 <= ns_part < 1000):
        raise ValueError(
            f"nanosecond part must be in 0..999, got {ns_part}"
        )
    utc_seconds = calendar.timegm(dt.utctimetuple())
    full_timestamp = (
        (utc_seconds * 1000000 + dt.microsecond) * 1000 + ns_part
    )

    if precision == Precision.MICROSECONDS:
        full_timestamp = _divround(full_timestamp, 1000)
    elif precision == Precision.MILLISECONDS:
        full_timestamp = _divround(full_timestamp, 1000000)
    elif precision == Precision.SECONDS:
        full_timestamp = _divround(full_timestamp, 1000000000)

    return str(full_timestamp).encode("utf-8")


def encode_tag_pair(t: typing.Tuple[str, str]) -> bytes:
    return b"=".join(map(encode_tag_part, t))


def encode_field_pair(f: typing.Tuple[str, InfluxDBValue]) -> bytes:
    return b"=".join((
        encode_field_key(f[0]),
        encode_field_value(f[1]),
    ))


class InfluxDBSample(typing.NamedTuple):
    measurement: str
    tags: typing.Tuple[typing.Tuple[str, str]]
    fields: typing.Tuple[typing.Tuple[str, InfluxDBValue]]
    timestamp: datetime
    ns_part: int

    def encode(self, precision) -> bytes:
        parts = []
        comma_parts = [
            encode_measurement_name(self.measurement),
        ]
        comma_parts.extend(map(encode_tag_pair, self.tags))
        parts.append(b",".join(comma_parts))

        comma_parts = map(encode_field_pair, self.fields)
        parts.append(b",".join(comma_parts))

        parts.append(encode_timestamp(self.timestamp, self.ns_part,
                                      precision))
        return b" ".join(parts) + b"\n"


T = typing.TypeVar("T")


def batcher(
        iterable: typing.Iterable[T],
        batch_size: int,
        ) -> typing.Generator[typing.Iterable[T], None, None]:
    def grouper():
        for i in itertools.count():
            for _ in range(batch_size):
                yield i

    grouped = zip(grouper(), iterable)
    for _, batch_items in itertools.groupby(grouped, key=lambda x: x[0]):
        yield map(operator.itemgetter(1), batch_items)


async def _sample_encoder(
        sample_batches: typing.AsyncIterable[
            typing.Iterable[InfluxDBSample]],
        precision: Precision):
    async for samples in sample_batches:
        yield b"".join(sample.encode(precision) for sample in samples)


async def _async_batcher(
        iterable: typing.Iterable[T],
        batch_size: int,
        ) -> typing.AsyncGenerator[typing.List[T], None]:
    for batch in batcher(iterable, batch_size):
        yield list(batch)


class InfluxDBError(Exception):
    def __init__(self, status, msg):
        super().__init__(f"{msg} ({status})")
        self.status = status
        self.msg = msg


class InfluxDBPermissionError(InfluxDBError):
    pass


class InfluxDBDataError(InfluxDBError):
    pass


class InfluxDBNotFoundError(InfluxDBError):
    pass


class HTTPAPITransport(interface.Transport):
    def __init__(self, *, config: TransportConfig, **kwargs):
        super().__init__(config=config, **kwargs)
        self._cfg = config

    @classmethod
    def get_config_schema(cls) -> schema.Schema:
        return _TRANSPORT_SCHEMA

    @classmethod
    def compile_config(cls, cfg: typing.Mapping) -> TransportConfig:
        if cfg["auth"] is not None:
            auth = AuthConfig(**cfg["auth"])
        else:
            auth = None
        api_url = cfg["api_url"]
        return TransportConfig(
            api_url=api_url,
            auth=auth,
        )

    async def write(
            self,
            session: aiohttp.ClientSession,
            database: str,
            retention_policy: typing.Optional[str],
            precision: Precision,
            samples: typing.AsyncIterable[typing.Iterable[InfluxDBSample]],
            auth: typing.Optional[AuthConfig] = None):
        if auth is None:
            auth = self._cfg.auth

        write_url = "{}/write".format(self._cfg.api_url)
        headers = {}
        params = {}
        if auth is not None:
            auth.update_args(headers, params)
        params["db"] = database
        params["precision"] = precision.value
        if retention_policy is not None:
            params["rp"] = retention_policy

        async with session.post(
                write_url,
                headers=headers,
                params=params,
                data=_sample_encoder(samples, precision)) as resp:
            if resp.status == 401 or resp.status == 403:
                raise InfluxDBPermissionError(resp.status, resp.reason)
            elif resp.status == 400 or resp.status == 413:
                raise InfluxDBDataError(resp.status, resp.reason)
            elif resp.status == 404:
                raise InfluxDBNotFoundError(resp.status, resp.reason)
            elif resp.status != 204:
                raise InfluxDBError(resp.status, resp.reason)


class Sink(interface.Sink[SinkConfig]):
    def __init__(self, *, config: SinkConfig, **kwargs):
        super().__init__(config=config, **kwargs)
        self._cfg = config

    @classmethod
    def get_config_schema(cls) -> schema.Schema:
        return _SINK_SCHEMA

    @classmethod
    def compile_config(cls, cfg: typing.Mapping) -> SinkConfig:
        if cfg["auth"] is not None:
            auth = AuthConfig(**cfg["auth"])
        else:
            auth = None
        return SinkConfig(
            database=cfg["database"],
            retention_policy=cfg["retention_policy"],
            precision=cfg["precision"],
            auth=auth,
        )

    @classmethod
    def accepts(cls, dataclass: interface.DataClass) -> bool:
        return dataclass == interface.DataClass.SAMPLE_BATCH

    @classmethod
    def supports_transport(cls,
                           transport_class: type,
                           config: SinkConfig) -> bool:
        return issubclass(transport_class, HTTPAPITransport)

    def _convert_samples(
            self,
            sample_batches: typing.Sequence[hintlib.sample.SampleBatch],
            ) -> typing.Generator[InfluxDBSample, None, None]:
        for batch in sample_batches:
            tags = [
                ("module", batch.bare_path.module)
            ]
            if batch.bare_path.instance is not None:
                tags.append(
                    ("instance", batch.bare_path.instance),
                )

            samples = dict(batch.samples)
            if None in samples:
                if len(samples) > 1:
                    raise ValueError("malformed sample batch")
                samples["value"] = samples.pop(None)

            yield InfluxDBSample(
                measurement=batch.bare_path.part,
                tags=tuple(tags),
                fields=tuple(samples.items()),
                timestamp=batch.timestamp,
                ns_part=0,
            )

    async def submit(self, data: interface.DataChunk):
        assert data.class_ == interface.DataClass.SAMPLE_BATCH

        precision = self._cfg.precision
        if precision == Precision.AUTO:
            precision = Precision.MILLISECONDS

        async with aiohttp.ClientSession() as session:
            await self.transport.write(
                session,
                self._cfg.database,
                self._cfg.retention_policy,
                precision,
                _async_batcher(
                    self._convert_samples(data.data),
                    1000,
                ),
                self._cfg.auth,
            )
