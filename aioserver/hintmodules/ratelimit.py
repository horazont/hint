import asyncio
import collections

from datetime import datetime, timedelta

import pytz

import aioxmpp.errors
import aioxmpp.structs

from aioxmpp.utils import namespaces

import hintmodules.utils


class RateLimitBin:
    def __init__(self, logger):
        super().__init__()
        self._limits = {}
        self._usages = {}
        self.logger = logger

        self._cleanup_task = asyncio.ensure_future(
            self.cleanup_task()
        )
        self._cleanup_task.add_done_callback(
            hintmodules.utils.log_failure(self.logger)
        )

    def _iter_partials(self, path, action):
        for partial_action_end in range(len(action)+1):
            if partial_action_end:
                partial_action = action[:-partial_action_end]
            else:
                partial_action = action

            for partial_path_end in range(len(path)+1):
                if partial_path_end:
                    partial_path = path[:-partial_path_end]
                else:
                    partial_path = path

                yield partial_path, partial_action

    def set_limit(self, path, action, limit):
        """
        Set the limit for the given `action` in this bin at `path` to `limit`.
        `limit` must be an integer value (0 to disallow completely) or
        :data:`None` to remove the limit.
        """
        self.logger.debug("limit for action %r at %r set to %r",
                          action, path, limit)
        self._limits.setdefault(path, {})[action] = limit

    def test(self, path, action, n=1):
        """
        Test whether `n` usages of `action` at the given `path` would still be
        within limits.
        """
        for path, action in self._iter_partials(path, action):
            limit = self._limits.get(path, {}).get(action)
            if limit is not None:
                self.logger.debug("relevant node %r %r", path, action)
                usage = self._usages.get(path, {}).get(
                    action, 0
                )
                return usage + n <= limit

        return True

    def register(self, path, action, n=1):
        """
        Register `n` usages (default 1) of `action` at `path`. Return true if
        the usages are still within limits, false otherwase. If the usage is
        not within limits, it is still registered.

        Use :meth:`test` to check beforehands whether a usage is within limits,
        but take care of race conditions.

        :meth:`register` only registers usages for pathes and actions which
        have limits set, to save memory. If a limit is defined after the first
        usage is registered, the usage will not be counted.
        """
        within_limits = True

        for path, action in self._iter_partials(path, action):
            limit = self._limits.get(path, {}).get(action)
            if limit is not None:
                self._usages.setdefault(
                    path,
                    collections.Counter()
                )[action] += n

                current = self._usages[path][action]

                within_limits = within_limits and current <= limit

                self.logger.debug("usage for action %r at %r is now at %d "
                                  "(out of %d; within_limits=%r)",
                                  action, path,
                                  current, limit,
                                  within_limits)

        return within_limits

    def reset_usages(self):
        """
        Clear all usages in the bin.
        """
        self.logger.debug("usages reset")
        self._usages.clear()

    async def close(self):
        self._cleanup_task.cancel()
        await self._cleanup_task


class InnerDayBin(RateLimitBin):
    def __init__(self, logger, length, timezone=pytz.UTC):
        super().__init__(logger)
        self.length = int(length)
        self.timezone = timezone

    @asyncio.coroutine
    def cleanup_task(self):
        while True:
            now = self.timezone.fromutc(
                datetime.utcnow()
            )
            today = now.replace(
                hour=0, minute=0, second=0, microsecond=0
            )

            now = pytz.UTC.normalize(now).replace(tzinfo=None)
            today = pytz.UTC.normalize(today).replace(tzinfo=None)

            now_seconds = (now - today).total_seconds()
            intervalno = now_seconds // self.length
            next_wakeup = today + timedelta(
                seconds=(intervalno+1)*self.length
            )

            self.logger.debug("next wakeup at %s", next_wakeup)

            sleep_time = max(0, (next_wakeup-now).total_seconds())

            self.logger.debug("sleeping for %.0f seconds", sleep_time)

            yield from asyncio.sleep(
                sleep_time
            )

            self.reset_usages()


class DailyBin(RateLimitBin):
    def __init__(self, logger, timezone=pytz.UTC):
        super().__init__(logger)
        self.timezone = timezone

    @asyncio.coroutine
    def cleanup_task(self):
        while True:
            now = self.timezone.fromutc(
                datetime.utcnow()
            )
            today = now.replace(
                hour=0, minute=0, second=0, microsecond=0
            )

            now = pytz.UTC.normalize(now).replace(tzinfo=None)
            today = pytz.UTC.normalize(today).replace(tzinfo=None)

            next_wakeup = today + timedelta(days=1)

            self.logger.debug("next wakeup at %s", next_wakeup)

            sleep_time = max(0, (next_wakeup-now).total_seconds())

            self.logger.debug("sleeping for %.0f seconds", sleep_time)

            yield from asyncio.sleep(
                sleep_time
            )

            self.reset_usages()


def match_jid(jid_to_match):
    jid_to_match = aioxmpp.structs.JID.fromstr(
        jid_to_match
    )

    def match_jid(stanza):
        jid = stanza.from_
        return jid == jid_to_match or jid == jid_to_match.bare()

    return match_jid


class Service:
    FILTERS = {
        "match_jid": match_jid,
    }

    def __init__(self, config, logger, **kwargs):
        super().__init__()
        self.logger = logger

        self._bins = {
            "daily": DailyBin(logger.getChild("bin:daily")),
            "hourly": InnerDayBin(logger.getChild("bin:hourly"),
                                  3600),
            "minutely": InnerDayBin(logger.getChild("bin:minutely"),
                                    60),
        }

        self._matches = []

        self._load_recursive((), config)

        self._matches.sort(key=lambda x: (len(x[1]), len(x[0])),
                           reverse=True)

    def _load_recursive(self, path, defn):
        defn = dict(defn)
        children = defn.pop("child", [])
        bins = defn.pop("bin", [])

        filters = []
        for filter_type, filter_arg in defn.items():
            try:
                filter_factory = self.FILTERS[filter_type]
            except KeyError:
                self.logger.error(
                    "no such filter (%r); "
                    "section and its children ignored"
                )
                return
            else:
                filters.append(filter_factory(filter_arg))

        self._matches.append((filters, path))

        for i, bin_def in enumerate(bins, 1):
            try:
                action = bin_def["action"]
                bin_ = bin_def["interval"]
                limit = bin_def["size"]
            except KeyError as exc:
                self.logger.error(
                    "bin definition number %d (1-based) at %s is "
                    "missing required attribute %s",
                    i,
                    path,
                    exc,
                )

            try:
                bin_ = self._bins[bin_]
            except KeyError:
                self.logger.warning(
                    "no such bin (%r): directive ignored",
                    bin_,
                )
                continue

            bin_.set_limit(path, action, limit)

        for i, child in enumerate(children, 1):
            key = child.get("id", str(i))
            self._load_recursive(path + (key,), child)

    def check_limit(self, stanza, actions, register=True):
        actions = [
            tuple(part for part in action.split(":") if part)
            if isinstance(action, str) else action
            for action in actions
        ]

        path = ()
        for filters, candidate_path in self._matches:
            if all(filter_(stanza) for filter_ in filters):
                path = candidate_path
                break

        self.logger.debug(
            "using ratelimit path %r for %r (actions=%r)",
            path, stanza, actions
        )

        for bin_ in self._bins.values():
            for action in actions:
                if not bin_.test(path, action):
                    return False

        success = True
        for bin_ in self._bins.values():
            for action in actions:
                if not bin_.register(path, action):
                    success = False

        return success

    def enforce_limit(self, stanza, actions):
        if not self.check_limit(stanza, actions):
            raise aioxmpp.errors.XMPPWaitError(
                (namespaces.stanzas, "resource-constraint"),
                text="Quota exceeded",
            )

    async def close(self):
        for bin_ in self._bins.values():
            await bin_.close()
