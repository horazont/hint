import logging

from datetime import timedelta


class Timeline:
    def __init__(self, wraparound_at, slack):
        super().__init__()
        self.__remote_tip = 0
        self.__local_tip = 0
        self.__wraparound_at = wraparound_at
        self.__slack = slack

    def wraparound_aware_minus(self, v1, v2):
        naive_diff_forward = (v1 - v2) % self.__wraparound_at
        naive_diff_backward = (v2 - v1) % self.__wraparound_at

        if naive_diff_backward < self.__slack:
            return -naive_diff_backward

        return naive_diff_forward

    def reset(self, timestamp):
        """
        Reset internal data structures and start a new epoch at `timestamp`.
        """
        self.__remote_tip = timestamp
        self.__local_tip = 0

    def feed_and_transform(self, timestamp):
        """
        Feed a timestamp into the timeline and return the timestamp relative to
        the epoch.
        """

        change = self.wraparound_aware_minus(timestamp, self.__remote_tip)
        if -self.__slack < change <= 0:
            # in slack region, assume late packet
            return self.__local_tip + change

        self.__remote_tip = timestamp
        self.__local_tip += change
        return self.__local_tip

    def forward(self, offset):
        """
        Advance the timeline by `offset` steps.

        Forwarding happens as if :meth:`feed_and_transform` had been called
        `offset` times, starting with the next timestamp in the timeline,
        incrementing the `timestamp` argument by one each time with proper
        wraparound.
        """
        self.__local_tip += offset
        self.__remote_tip = (self.__remote_tip + offset) % self.__wraparound_at


class RTCifier:
    MAX_HISTORY = 10000
    INIT_HISTORY = 2
    MAX_DIFFERENCE = timedelta(seconds=60)

    def __init__(self, timeline, logger=None):
        super().__init__()
        self.logger = logger or logging.getLogger(
            ".".join([
                __name__,
                type(self).__qualname__,
            ])
        )
        self.__timeline = timeline
        self.__history = []

    def align(self, rtc, timestamp):
        if len(self.__history) >= self.MAX_HISTORY:
            to_cull = len(self.__history) - self.MAX_HISTORY
            del self.__history[:to_cull]
            del to_cull

        # absolute = self.__timeline.feed_and_transform(timestamp)
        # new_ref = (timestamp - absolute % 1000) % 2**16
        new_ref = timestamp
        offset = self.__timeline.feed_and_transform(new_ref)

        for i, (hist_rtc, hist_offset) in enumerate(self.__history):
            self.__history[i] = (hist_rtc, hist_offset - offset)

        self.__history.append((rtc, 0))

        if len(self.__history) < self.INIT_HISTORY:
            to_add = self.INIT_HISTORY - len(self.__history)
            self.__history.extend([self.__history[-1]]*to_add)
            del to_add

        rtcbase = rtc + sum(
            (
                (hist_rtc - timedelta(milliseconds=hist_offset)) - rtc
                for hist_rtc, hist_offset in self.__history
            ),
            timedelta(0)
        ) / len(self.__history)

        expected = self.__history[0][0] - timedelta(
            milliseconds=self.__history[0][1]
        )

        if abs(rtcbase - rtc) >= self.MAX_DIFFERENCE:
            self.logger.warning(
                "large jump in clock (%s -> %s)",
                rtcbase,
                rtc,
            )
            del self.__history[:-1]
            rtcbase = rtc

        self.logger.debug(
            "drifted: %s%s; drift rate: %.2f%%; %d samples",
            "-" if expected < rtcbase else "",
            abs(expected-rtcbase),
            (expected-rtcbase) / (self.__history[-1][0] - self.__history[0][0]) * 100
            if self.__history[-1][0] > self.__history[0][0] else float("inf"),
            len(self.__history)
        )

        self.__timeline.reset(new_ref)
        self.__rtcbase = rtcbase

    def map_to_rtc(self, timestamp):
        return self.__rtcbase + timedelta(
            milliseconds=self.__timeline.feed_and_transform(timestamp)
        )
