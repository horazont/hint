import ast
import asyncio
import functools
import hashlib
import json
import time

from datetime import datetime

import aiohttp

import aioxmpp.disco.xso
import aioxmpp.pubsub.xso
import aioxmpp.structs

from aioxmpp.utils import namespaces

import hintmodules.warnings.xso as warnings_xso
import hintmodules.utils


def hash_warning(warning):
    hf = hashlib.sha1()
    hf.update(str(warning["start"]).encode("utf-8"))
    hf.update(b"\x00")
    hf.update(warning["event"].encode("utf-8"))
    hf.update(b"\x00")
    hf.update(warning["headline"].encode("utf-8"))
    hf.update(b"\x00")
    hf.update(warning["description"].encode("utf-8"))
    hf.update(b"\x00")
    hf.update(warning["instruction"].encode("utf-8"))
    hf.update(b"\x00")
    if warning["altitudeStart"] is not None:
        hf.update(str(warning["altitudeStart"]).encode("utf-8"))
    hf.update(b"\x00")
    if warning["altitudeEnd"] is not None:
        hf.update(str(warning["altitudeEnd"]).encode("utf-8"))
    hf.update(b"\x00")
    if warning.get("end") is not None:
        hf.update(str(warning["end"]).encode("utf-8"))
    hf.update(b"\x00")
    return hf.hexdigest()


def point_in_poly(x, y, poly):
    # check if point is a vertex
    if (x, y) in poly:
        return True

    # check if point is on a boundary
    for i in range(len(poly)):
        p1 = None
        p2 = None
        if i == 0:
            p1 = poly[0]
            p2 = poly[1]
        else:
            p1 = poly[i-1]
            p2 = poly[i]
        if (p1[1] == p2[1] and
                p1[1] == y and
                x > min(p1[0], p2[0]) and
                x < max(p1[0], p2[0])):
            return True

    n = len(poly)
    inside = False

    p1x, p1y = poly[0]
    for i in range(n+1):
        p2x, p2y = poly[i % n]
        if y > min(p1y, p2y):
            if y <= max(p1y, p2y):
                if x <= max(p1x, p2x):
                    if p1y != p2y:
                        xints = (y-p1y)*(p2x-p1x)/(p2y-p1y)+p1x
                    if p1x == p2x or x <= xints:
                        inside = not inside
        p1x, p1y = p2x, p2y

    return inside


def load_cellfile(f, logger):
    logger.info("loading warning cell information ... "
                "(this may take a moment)")

    with f:
        warnid_to_celldata = ast.literal_eval(f.read())

    boxes = []
    names = []

    for warnid, celldata in warnid_to_celldata.items():
        boxes.append((celldata["bbox"], warnid))
        names.append((celldata["name"], warnid))

    logger.info("warning cell information loaded")

    return (
        warnid_to_celldata,
        boxes,
        names,
    )


async def unpack_future(data_future, n):
    return (await data_future)[n]


class Plugin:
    JSONP_URL = "http://www.dwd.de/DWD/warnungen/warnapp/json/warnings.json"

    def __init__(self, service, section):
        super().__init__()
        self.service = service
        self.logger = service.logger.getChild("dwd")

        self._cache_timeout = section.getint("cache_timeout", fallback=300)
        self._cache_expires = time.monotonic() + 1
        self._cache = None
        self._cache_changed = asyncio.Event()

        self._jsonp_url = section.get("jsonp_url", fallback=self.JSONP_URL)
        self._mock_json = None
        mock_json_file = section.get("mock_json", fallback=None)
        if mock_json_file is not None:
            self._mock_json = json.load(open(mock_json_file, "r"))

        self._pubsub_jid = aioxmpp.structs.JID.fromstr(
            section.get("pubsub_jid")
        )

        self._pubsub_node_prefix = section.get("pubsub_node_prefix")

        self._background_task = None

        self.service.client.on_stream_established.connect(
            self._start_refresher,
        )

        self.service.client.on_stream_destroyed.connect(
            self._stop_refresher,
        )

        celldata = asyncio.ensure_future(
            asyncio.get_event_loop().run_in_executor(
                None,
                functools.partial(
                    load_cellfile,
                    open(section.get("cell_data"), "r"),
                    self.logger,
                )
            )
        )

        self._warnid_to_celldata = asyncio.ensure_future(
            unpack_future(celldata, 0)
        )

        self._box_to_warnid = asyncio.ensure_future(
            unpack_future(celldata, 1)
        )

        self._name_to_warnid = asyncio.ensure_future(
            unpack_future(celldata, 2)
        )

    def _start_refresher(self):
        if self._background_task is not None:
            return

        self._background_task = asyncio.ensure_future(
            self._refresher()
        )
        self._background_task.add_done_callback(
            hintmodules.utils.log_failure(self.logger),
        )

    def _stop_refresher(self):
        if self._background_task is None:
            return

        self._background_task.cancel()
        self._background_task = None

    @asyncio.coroutine
    def _wait_for_refresh_time(self):
        while True:
            dt = self._cache_expires - time.monotonic()
            dt = max(dt, 0.1)
            self.logger.debug("refreshing in %.0f seconds",
                              dt)
            try:
                yield from asyncio.wait_for(
                    self._cache_changed.wait(),
                    dt
                )
            except asyncio.TimeoutError:
                self.logger.debug("refreshing now!")
                # need to acquire to be able to leave with block
                return
            else:
                self._cache_changed.clear()
                self.logger.debug("woke up, recalculating timeout")
                continue

    async def _refresh(self):
        if self._mock_json is not None:
            self.logger.warning("using mocked data")
            data = self._mock_json
        else:
            with aiohttp.ClientSession() as session:
                async with session.get(self._jsonp_url) as resp:
                    data = await resp.text()

            data = json.loads(data[data.find("{"):data.rfind("}")+1])

        self._cache_expires = time.monotonic() + self._cache_timeout
        self._cache = data

    def _make_warning_id(self, warning):
        return hash_warning(warning)

    @asyncio.coroutine
    def _publish_warning(self, node, id_, warning, is_preliminary):
        warning_xso = warnings_xso.HintWarning()
        warning_xso.is_preliminary = is_preliminary
        warning_xso.description = warning["description"]
        warning_xso.instruction = warning["instruction"]
        warning_xso.level = warning.get("level")
        warning_xso.type_ = warning.get("type")
        warning_xso.start = datetime.utcfromtimestamp(warning["start"]/1000)
        warning_xso.altitude_start = warning["altitudeStart"]
        warning_xso.altitude_end = warning["altitudeEnd"]

        if warning.get("end"):
            warning_xso.end = datetime.utcfromtimestamp(
                warning["end"]/1000
            )

        warning_xso.event = warning["event"]
        warning_xso.headline = warning["headline"]

        yield from self.service.pubsub.publish(
            self._pubsub_jid,
            node,
            warning_xso,
            id_=id_,
        )

    def _push_warnings(self, node, warnings, is_preliminary, existing_ids):
        for warning in warnings:
            id_ = self._make_warning_id(warning)
            try:
                existing_ids.remove(id_)
            except KeyError:
                # does not exist
                pass
            else:
                continue

            yield asyncio.ensure_future(self._publish_warning(
                node,
                id_,
                warning,
                is_preliminary,
            ))

    @asyncio.coroutine
    def _update_region(self, node, preliminaries, warnings):
        pubsub = self.service.pubsub
        try:
            yield from pubsub.create(
                self._pubsub_jid,
                node=node,
            )
        except aioxmpp.errors.XMPPCancelError as exc:
            if exc.condition == (namespaces.stanzas, "conflict"):
                # node exists, query items
                existing = (yield from pubsub.get_items(
                    self._pubsub_jid,
                    node=node,
                )).payload.items
            else:
                raise
        else:
            existing = []

        ids = set(item.id_ for item in existing)
        del existing

        task_futures = []
        task_futures.extend(
            self._push_warnings(node, preliminaries, True, ids),
        )
        task_futures.extend(
            self._push_warnings(node, warnings, False, ids),
        )

        if task_futures:
            self.logger.debug("adding %d new items to %r",
                              len(task_futures),
                              node)

        if ids:
            self.logger.debug("removing %d obsolete items from %r",
                              len(ids),
                              node)
            task_futures.extend(
                asyncio.ensure_future(
                    pubsub.retract(
                        self._pubsub_jid,
                        node,
                        id_,
                        notify=True,
                    )
                )
                for id_ in ids
            )

        yield from asyncio.gather(*task_futures)

    @asyncio.coroutine
    def _update(self):
        pubsub = self.service.pubsub

        existing_nodes = set(
            node
            for node, _ in (yield from pubsub.get_nodes(
                    self._pubsub_jid,
            ))
            if node.startswith(self._pubsub_node_prefix)
        )

        tasks = []

        regions = (set(self._cache["warnings"]) |
                   set(self._cache["vorabInformation"]))
        for region in regions:
            node = self._pubsub_node_prefix + region
            existing_nodes.discard(node)
            tasks.append(
                asyncio.ensure_future(
                    self._update_region(
                        node,
                        self._cache["vorabInformation"].get(region, []),
                        self._cache["warnings"].get(region, []),
                    )
                )
            )

        yield from asyncio.gather(*tasks)

        existing_nodes = list(existing_nodes)
        self.logger.debug("%d nodes which may need clearing",
                          len(existing_nodes))
        if not existing_nodes:
            return

        tasks = [
            asyncio.ensure_future(
                pubsub.get_items(self._pubsub_jid, node)
            )
            for node in existing_nodes
        ]

        results = yield from asyncio.gather(*tasks)
        tasks.clear()

        for node, result in zip(existing_nodes, results):
            if not result.payload.items:
                continue

            self.logger.debug("removing %d obsolete entries from %r",
                              len(result.payload.items),
                              node)
            tasks.extend(
                asyncio.ensure_future(
                    pubsub.retract(
                        self._pubsub_jid,
                        node,
                        item.id_,
                        notify=True,
                    )
                )
                for item in result.payload.items
            )

        yield from asyncio.gather(*tasks)

    @asyncio.coroutine
    def _refresher(self):
        while True:
            yield from self._wait_for_refresh_time()
            t0 = time.monotonic()
            try:
                yield from self._refresh()
            except aiohttp.ClientError:
                self.logger.warning("failed to download warnings, "
                                    "will retry later", exc_info=True)
                yield from asyncio.sleep(300)
                continue

            t1 = time.monotonic()
            self.logger.debug("download took %.1f seconds", t1-t0)
            try:
                yield from self._update()
            except aioxmpp.errors.XMPPError:
                self.logger.warning("failed to update warnings, "
                                    "will retry later", exc_info=True)
                yield from asyncio.sleep(300)
                continue

            t2 = time.monotonic()
            self.logger.info("push took %.1f seconds", t2-t1)

    @asyncio.coroutine
    def shutdown(self):
        if self._background_task is None:
            return

        self._background_task.cancel()
        try:
            yield from self._background_task
        except asyncio.CancelledError:
            pass

    async def search_nodes_by_location_name(self, name):
        name = " ".join(name.casefold().split())

        name_to_warnid = await self._name_to_warnid

        def find():
            for region_name, region_id in name_to_warnid:
                if name in region_name.casefold():
                    yield aioxmpp.disco.xso.Item(
                        jid=self._pubsub_jid,
                        node=self._pubsub_node_prefix + str(region_id)
                    )

        return find()

    async def search_nodes_by_geocoord(self, lat, lon):
        box_to_warnid = await self._box_to_warnid
        warnid_to_celldata = await self._warnid_to_celldata

        def find():
            for bbox, warnid in box_to_warnid:
                (min_lat, min_lon), (max_lat, max_lon) = bbox
                if not (min_lat <= lat <= max_lat and
                        min_lon <= lon <= max_lon):
                    continue

                points = warnid_to_celldata[warnid]["shape"]
                if point_in_poly(lat, lon, points):
                    yield aioxmpp.disco.xso.Item(
                        jid=self._pubsub_jid,
                        node=self._pubsub_node_prefix + str(warnid),
                    )

        return find()
