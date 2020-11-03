import abc
import asyncio
import collections
import enum
import typing

import hintlib.utils


class OverflowPolicy(enum.Enum):
    REJECT = "reject"
    DROP_OLD = "drop-old"
    DROP_NEW = "drop-new"


class Queue(metaclass=abc.ABCMeta):
    @abc.abstractmethod
    def push(self, item):
        pass

    @abc.abstractmethod
    async def run(self):
        pass


class EphemeralQueue(Queue):
    def __init__(self, *,
                 sink: typing.Callable,
                 logger,
                 max_depth: int,
                 overflow_policy: OverflowPolicy,
                 max_retries: int = 0):
        super().__init__()
        self._queue = collections.deque(maxlen=max_depth)
        self._nonempty = asyncio.Condition()
        self._overflow_policy = overflow_policy
        self._sink = sink
        self._max_retries = max_retries
        self._retry_backoff = hintlib.utils.ExponentialBackOff()
        self.logger = logger

    def _apply_overflow_policy(self):
        if self._overflow_policy == OverflowPolicy.REJECT:
            self.logger.debug("rejecting item due to overfull queue")
            raise asyncio.QueueFull
        if self._overflow_policy == OverflowPolicy.DROP_NEW:
            self.logger.warning(
                "DATA LOSS: dropping new item due to overfull queue"
            )
            return False
        self.logger.warning(
            "DATA LOSS: dropping old item due to overfull queue"
        )
        return True

    async def push(self, item):
        await self._nonempty.acquire()
        try:
            if len(self._queue) == self._queue.maxlen:
                if not self._apply_drop_policy():
                    # drop new item
                    return

            self._queue.append(item)
            self._nonempty.notify()
        finally:
            self._nonempty.release()

    async def _sink_with_retries(self, item):
        self._retry_backoff.reset()
        first_err = None
        last_err = None
        for i in range(self._max_retries + 1):
            try:
                await self._sink(item)
            except Exception as exc:
                if first_err is None:
                    first_err = exc
                will_retry = i < self._max_retries
                if will_retry:
                    delay = next(self._retry_backoff)
                    self.logger.warning(
                        "failed to submit item %r to sink",
                        item,
                        exc_info=True
                    )
                    await asyncio.sleep(delay)
            else:
                return

        if first_err is not last_err:
            self.logger.error(
                "failed to sink item %r multiple times. first error "
                "was %s (%s)",
                item,
                first_err,
                type(first_err).__name__,
                exc_info=last_err,
            )
        else:
            self.logger.error(
                "failed to sink item %r on first and only attempt",
                item,
                exc_info=last_err,
            )

    async def run(self):
        while True:
            await self._nonempty.acquire()
            try:
                await self._nonempty.wait_for(lambda: len(self._queue) > 0)
                item = self._queue.pop()
            finally:
                self._nonempty.release()
            await self._sink_with_retries(item)
