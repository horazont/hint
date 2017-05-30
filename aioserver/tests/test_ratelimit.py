import logging
import unittest

import hintmodules.ratelimit as ratelimit


class TestRateLimitBin(unittest.TestCase):
    def setUp(self):
        self.logger = logging.getLogger("test")
        self.rl = ratelimit.RateLimitBin(self.logger)

    def test_set_limit(self):
        self.rl.set_limit(("a", "b"), ("x", "y"), 10)

    def test_test_without_limits(self):
        self.assertTrue(
            self.rl.test((), (), 100)
        )

        self.assertTrue(
            self.rl.test(("x", ), ("y", ), 100)
        )

    def test_test_after_set_limit(self):
        self.rl.set_limit(("a", "b"), ("x", "y"), 0)

        self.assertTrue(
            self.rl.test(("a", ), ("x", ))
        )

        self.assertTrue(
            self.rl.test(("a",), ("x", "y"))
        )

        self.assertTrue(
            self.rl.test(("a", "b"), ("x", ))
        )

        self.assertFalse(
            self.rl.test(("a", "b"), ("x", "y"))
        )

        self.assertFalse(
            self.rl.test(("a", "b"), ("x", "y", "z"))
        )

        self.assertFalse(
            self.rl.test(("a", "b", "c"), ("x", "y"))
        )

        self.assertFalse(
            self.rl.test(("a", "b", "c"), ("x", "y", "z"))
        )

    def test_register_without_limits(self):
        self.assertTrue(self.rl.register(("a", "b"), ("x", "y")))
        self.assertTrue(self.rl.register(("a", "b"), ("x", )))
        self.assertTrue(self.rl.register(("a", ), ("x", "y")))
        self.assertTrue(self.rl.register(("a", ), ("x", )))

    def test_register_with_nested_actions(self):
        self.rl.set_limit(("a", "b"), ("x", "y", 1), 1)
        self.rl.set_limit(("a", "b"), ("x", "y", 1, 1), 1)
        self.rl.set_limit(("a", "b"), ("x", "y", 1, 1, 1), 1)

        self.assertTrue(
            self.rl.register(("a", "b"), ("x", "y", 1, 1, 1))
        )

        self.assertFalse(
            self.rl.register(("a", "b"), ("x", "y", 1, 1, 1))
        )

        self.rl.set_limit(("a", "b"), ("x", "y", 1, 1, 1), 2)

        self.assertFalse(
            self.rl.register(("a", "b"), ("x", "y", 1, 1, 1))
        )

        self.assertFalse(
            self.rl.register(("a", "b"), ("x", "y", 1, 1))
        )

        self.assertFalse(
            self.rl.register(("a", "b"), ("x", "y", 1))
        )

        self.rl.set_limit(("a", "b"), ("x", "y", 1, 1, 1), 10)

        self.assertFalse(
            self.rl.register(("a", "b"), ("x", "y", 1, 1, 1))
        )

        self.rl.set_limit(("a", "b"), ("x", "y", 1, 1), 10)

        self.rl.set_limit(("a", "b"), ("x", "y", 1), 10)

        self.assertTrue(
            self.rl.register(("a", "b"), ("x", "y", 1, 1, 1), n=4)
        )

        self.rl.set_limit(("a", "b"), ("x", "y", 1), None)
        self.rl.set_limit(("a", "b"), ("x", "y", 1, 1), None)
        self.rl.set_limit(("a", "b"), ("x", "y", 1, 1, 1), None)

        self.assertTrue(
            self.rl.register(("a", "b"), ("x", "y", 1, 1, 1))
        )

    def test_reset_usages(self):
        self.rl.set_limit(("a", ), ("x", ), 1)

        self.assertTrue(
            self.rl.register(("a", "b"), ("x", "y"))
        )

        self.assertFalse(
            self.rl.register(("a", "b"), ("x", "y"))
        )

        self.rl.reset_usages()

        self.assertTrue(
            self.rl.register(("a", "b"), ("x", "y"))
        )

    def test_register_with_nested_paths(self):
        self.rl.set_limit(("a", ), ("x", ), 0)

        self.assertFalse(
            self.rl.register(("a", "b"), ("x", "y"))
        )

        self.rl.reset_usages()

        self.assertFalse(
            self.rl.register(("a", ), ("x", "y"))
        )
        self.rl.reset_usages()

        self.assertFalse(
            self.rl.register(("a", "b"), ("x", ))
        )
        self.rl.reset_usages()

        self.assertFalse(
            self.rl.register(("a", ), ("x", ))
        )

    def tearDown(self):
        del self.rl
        del self.logger
