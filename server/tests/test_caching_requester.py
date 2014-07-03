import time
import unittest

from datetime import datetime, timedelta

import hintmodules.caching_requester

class InstrumentedRequester(hintmodules.caching_requester.AdvancedRequester):
    def _perform_request(self, expired_cache_entry=None, fail=False, **kwargs):
        if fail:
            raise hintmodules.caching_requester.RequestError(
                "Doomed to fail",
                back_off=True)
        result = hintmodules.caching_requester.CacheEntry()
        result.data = (kwargs, datetime.utcnow())
        return result

    def _get_backing_off_result(self, expired_cache_entry=None,
                                pass_=False, **kwargs):
        if pass_:
            return expired_cache_entry

        return None

class TestAdvancedRequester(unittest.TestCase):
    def setUp(self):
        self.requester = InstrumentedRequester(
            cache_timeout=timedelta(seconds=0.5),
            initial_back_off_time=timedelta(seconds=0.5))

    def tearDown(self):
        del self.requester

    def test_back_off(self):
        with self.assertRaises(hintmodules.caching_requester.RequestError):
            self.requester.request(fail=True)

        with self.assertRaises(hintmodules.caching_requester.BackingOff):
            self.requester.request()

        time.sleep(0.5)

        with self.assertRaises(hintmodules.caching_requester.RequestError):
            self.requester.request(fail=True)

        with self.assertRaises(hintmodules.caching_requester.BackingOff):
            self.requester.request()

        time.sleep(0.5)

        with self.assertRaises(hintmodules.caching_requester.BackingOff):
            self.requester.request()

        time.sleep(0.5)

        self.assertEqual(
            {"foo": "bar"},
            self.requester.request(foo="bar")[0])

    def test_back_off_value(self):
        value1 = self.requester.request(foo="bar", pass_=True)
        time.sleep(0.5)

        with self.assertRaises(hintmodules.caching_requester.RequestError):
            self.requester.request(fail=True)

        self.assertEqual(
            value1,
            self.requester.request(foo="bar", pass_=True))

    def test_cache(self):
        value1 = self.requester.request(foo="bar")
        time.sleep(0.01)
        value2 = self.requester.request(foo="baz")

        self.assertNotEqual(
            value1,
            value2)

        self.assertEqual(
            value1,
            self.requester.request(**value1[0]))
        self.assertEqual(
            value2,
            self.requester.request(**value2[0]))

        time.sleep(0.5)

        self.assertNotEqual(
            value1,
            self.requester.request(**value1[0]))
        self.assertNotEqual(
            value2,
            self.requester.request(**value2[0]))
