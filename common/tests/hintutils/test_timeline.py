import contextlib
import random
import unittest
import unittest.mock

from datetime import datetime, timedelta

import hintutils.timeline as timeline


class TestTimeline(unittest.TestCase):
    def setUp(self):
        self.tl = timeline.Timeline(2**16, 1000)

    def tearDown(self):
        del self.tl

    def test_feed_and_transform_monotonically(self):
        for i in range(0, 2**16, 100):
            self.assertEqual(
                i,
                self.tl.feed_and_transform(i),
            )

    def test_feed_and_transform_wraparound_to_zero(self):
        self.tl.feed_and_transform(0)
        self.tl.feed_and_transform(10000)
        self.tl.feed_and_transform(20000)
        self.tl.feed_and_transform(30000)
        self.tl.feed_and_transform(40000)
        self.tl.feed_and_transform(50000)
        self.tl.feed_and_transform(60000)
        self.assertEqual(
            self.tl.feed_and_transform(0),
            2**16
        )

    def test_feed_and_transform_wraparound_above_zero(self):
        self.tl.feed_and_transform(0)
        self.tl.feed_and_transform(10000)
        self.tl.feed_and_transform(20000)
        self.tl.feed_and_transform(30000)
        self.tl.feed_and_transform(40000)
        self.tl.feed_and_transform(50000)
        self.tl.feed_and_transform(60000)
        self.assertEqual(
            self.tl.feed_and_transform(1200),
            2**16 + 1200
        )

    def test_feed_and_transform_slack(self):
        self.tl.feed_and_transform(0)
        self.tl.feed_and_transform(10000)
        self.tl.feed_and_transform(20000)
        self.tl.feed_and_transform(30000)
        self.tl.feed_and_transform(40000)
        self.tl.feed_and_transform(50000)
        self.tl.feed_and_transform(60000)
        self.assertEqual(
            self.tl.feed_and_transform(59001),
            59001,
        )

    def test_feed_and_transform_slack_after_wraparound(self):
        self.tl.feed_and_transform(0)
        self.tl.feed_and_transform(10000)
        self.tl.feed_and_transform(20000)
        self.tl.feed_and_transform(30000)
        self.tl.feed_and_transform(40000)
        self.tl.feed_and_transform(50000)
        self.tl.feed_and_transform(60000)
        self.tl.feed_and_transform(10)
        self.assertEqual(
            self.tl.feed_and_transform(65535),
            65535,
        )

    def test_feed_and_transform_slack_wraparound_slack(self):
        self.tl.feed_and_transform(0)
        self.tl.feed_and_transform(10000)
        self.tl.feed_and_transform(20000)
        self.tl.feed_and_transform(30000)
        self.tl.feed_and_transform(40000)
        self.tl.feed_and_transform(50000)
        self.tl.feed_and_transform(60000)
        self.assertEqual(
            self.tl.feed_and_transform(59001),
            59001,
        )
        self.tl.feed_and_transform(10)
        self.assertEqual(
            self.tl.feed_and_transform(65535),
            65535,
        )
        self.assertEqual(
            self.tl.feed_and_transform(10),
            2**16 + 10,
        )

    def test_reset_and_feed(self):
        self.tl.reset(1000)
        self.assertEqual(
            self.tl.feed_and_transform(1000),
            0,
        )

        for i in range(1000, 2**16, 100):
            self.assertEqual(
                i-1000,
                self.tl.feed_and_transform(i),
            )

        self.assertEqual(
            2**16-1000,
            self.tl.feed_and_transform(0)
        )

    def test_feed_and_transform_slack_after_reset(self):
        self.assertEqual(
            -999,
            self.tl.feed_and_transform(2**16-999)
        )

    def test_forward(self):
        self.tl.forward(2**16 + 5)
        self.assertEqual(
            2**16 + 10,
            self.tl.feed_and_transform(10)
        )


class TestRTCifier(unittest.TestCase):
    def setUp(self):
        self.tl = timeline.Timeline(2**16, 1000)
        self.rtcifier = timeline.RTCifier(
            self.tl
        )

    def test_align_maps_directly(self):
        dt0 = datetime.utcnow().replace(microsecond=0)
        t0 = 5

        self.rtcifier.align(
            dt0,
            t0,
        )

        self.assertEqual(
            self.rtcifier.map_to_rtc(5),
            dt0,
        )

        self.assertEqual(
            self.rtcifier.map_to_rtc(0),
            dt0 - timedelta(milliseconds=5)
        )

    # def test_align_correctly_aliases_to_second_after_wraparound(self):
    #     dt0 = datetime.utcnow().replace(microsecond=0)
    #     t0 = 5

    #     self.rtcifier.align(
    #         dt0,
    #         t0,
    #     )

    #     for i in range(0, 65536, 100):
    #         self.assertEqual(
    #             self.rtcifier.map_to_rtc(i),
    #             dt0 + timedelta(milliseconds=i)
    #         )

    #     self.assertEqual(
    #         self.rtcifier.map_to_rtc(0),
    #         dt0 + timedelta(milliseconds=65536),
    #     )

    #     self.rtcifier.align(
    #         dt0 + timedelta(seconds=65),
    #         0,
    #     )

    #     self.assertEqual(
    #         self.rtcifier.map_to_rtc(0),
    #         (dt0 + timedelta(milliseconds=65536)),
    #     )

    def test_align_learns_over_several_samples(self):
        # we introduce a drift after two samples
        dt0 = datetime(2017, 6, 10, 9, 41, 0)

        self.rtcifier.align(
            dt0,
            0,
        )

        self.assertEqual(
            self.rtcifier.map_to_rtc(0),
            dt0
        )

        self.rtcifier.align(
            dt0 + timedelta(seconds=1),
            1000,
        )

        self.assertEqual(
            self.rtcifier.map_to_rtc(1000),
            dt0 + timedelta(seconds=1)
        )

        self.rtcifier.align(
            dt0 + timedelta(seconds=3),
            2000,
        )

        self.assertEqual(
            self.rtcifier.map_to_rtc(2000),
            dt0 + timedelta(microseconds=2333333)
        )

        self.rtcifier.align(
            dt0 + timedelta(seconds=4),
            3000,
        )

        self.assertEqual(
            self.rtcifier.map_to_rtc(3000),
            dt0 + timedelta(milliseconds=3500)
        )

    def test_align_resets_completely_on_large_difference(self):
        # we introduce a drift after two samples
        dt0 = datetime(2017, 6, 10, 9, 41, 0)

        self.rtcifier.align(
            dt0,
            0,
        )

        self.assertEqual(
            self.rtcifier.map_to_rtc(0),
            dt0
        )

        self.rtcifier.align(
            dt0 + timedelta(seconds=1),
            1000,
        )

        self.assertEqual(
            self.rtcifier.map_to_rtc(1000),
            dt0 + timedelta(seconds=1)
        )

        self.rtcifier.align(
            dt0 + timedelta(seconds=3),
            2000,
        )

        self.assertEqual(
            self.rtcifier.map_to_rtc(2000),
            dt0 + timedelta(microseconds=2333333)
        )

        self.rtcifier.align(
            dt0 + timedelta(seconds=10),
            3000,
        )

        self.assertEqual(
            self.rtcifier.map_to_rtc(3000),
            dt0 + timedelta(seconds=10)
        )
