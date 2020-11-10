import abc
import asyncio
import enum
import typing

import schema

import hintlib.sample


class DataClass(enum.Enum):
    STREAM = "stream"
    SAMPLE_BATCH = "sample-batch"


class DataChunk(typing.NamedTuple):
    class_: DataClass
    data: typing.Union[typing.Sequence[hintlib.sample.SampleBatch],
                       hintlib.sample.StreamBlock]

    @classmethod
    def from_stream_block(cls, data: hintlib.sample.StreamBlock):
        return cls(DataClass.STREAM, data)

    @classmethod
    def from_sample_batches(
            cls,
            batches: typing.Sequence[hintlib.sample.SampleBatch]):
        return cls(DataClass.SAMPLE_BATCH, batches)

    @classmethod
    def from_sample_batch(
            cls,
            batch: hintlib.sample.SampleBatch):
        return cls(DataClass.SAMPLE_BATCH, (batch,))


T = typing.TypeVar("T")


class Configurable(typing.Generic[T], metaclass=abc.ABCMeta):
    def __init__(self, *, config: T, **kwargs):
        super().__init__(**kwargs)

    @classmethod
    def get_config_schema(cls) -> schema.Schema:
        return schema.Schema({})

    @classmethod
    def compile_config(cls, cfg: typing.Mapping) -> T:
        return cfg



class Transport(Configurable[T], metaclass=abc.ABCMeta):
    def __init__(self, *, config: T, logger, **kwargs):
        super().__init__(config=config)
        self.logger = logger

    async def run(self):
        while True:
            await asyncio.sleep(3600)


class _SinkSourceBase(Configurable[T], metaclass=abc.ABCMeta):
    def __init__(self, *, logger, transport, **kwargs):
        super().__init__(**kwargs)
        self.logger = logger
        self.transport = transport

    @classmethod
    def supports_transport(
            cls,
            transport_class: type,
            config: T) -> bool:
        return False


class Sink(_SinkSourceBase[T], metaclass=abc.ABCMeta):
    @classmethod
    def accepts(self, dataclass: DataClass) -> bool:
        return False

    @abc.abstractmethod
    async def submit(self, data: DataChunk):
        """
        Submit a piece of `data` into the sink.

        If this function returns without an exception, the data has been
        acknowleged by the next hop of the sink (if such a thing exists) and
        thus, persisted safely.

        If this function raises an exception, the data may have been passed on,
        but it is safe for the data to be passed to this function again in
        order to ensure delivery.
        """

    async def run(self):
        while True:
            await asyncio.sleep(3600)


class Source(_SinkSourceBase[T], metaclass=abc.ABCMeta):
    def __init__(self, **kwargs):
        super().__init__(**kwargs)
        self._on_data = None

    @property
    def on_data(self) -> typing.Callable[..., typing.Awaitable]:
        return self._on_data

    @on_data.setter
    def on_data(self, cb: typing.Callable[..., typing.Awaitable]):
        self._on_data = cb

    async def _emit(self, data: DataChunk):
        """
        Emit `data` to the listener.
        """
        if self._on_data is None:
            self.logger.warning("DATA LOSS: no on_data handler registered")
            return

        await self._on_data(data)

    def _emit_cb(self,
                 data: DataChunk,
                 done_cb: typing.Callable,
                 timeout: typing.Optional[float] = None,
                 loop: typing.Optional[asyncio.BaseEventLoop] = None):
        """
        Emit `data` to the listener and call `done_cb` when the listener has
        finished processing the data.

        Note that `done_cb` is always called, even if the data was not
        processed successfully.
        """
        loop = loop or asyncio.get_event_loop()
        coro = self._emit(data)
        if timeout is not None:
            coro = asyncio.wait_for(coro, timeout, loop=loop)
        task = loop.create_task(coro)

        def on_done(task):
            # just make all warnings go away
            # TODO: maybe something smarter than this?
            if task.exception() is None:
                task.result()
            done_cb()

        task.add_done_callback(on_done)

    @classmethod
    def emits(self, dataclass: DataClass) -> bool:
        return False

    @abc.abstractmethod
    async def run(self):
        while True:
            await asyncio.sleep(3600)
