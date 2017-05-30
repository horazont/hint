import ast
import asyncio
import functools
import hashlib
import io
import logging
import pathlib
import xml.sax
import zipfile

import aioxmpp.disco.xso
import aioxmpp.pubsub.xso
import aioxmpp.structs
import aioxmpp.xso

from aioxmpp.utils import namespaces

import hintmodules.warnings.cap as cap_xso
import hintmodules.utils

logger = logging.getLogger(__name__)

try:
    import aioftp
except:
    logger.error("aioftp not found; cannot use FTPPlugin")
    aioftp = None


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


def _read_cap_file(f):
    result = None

    def cb(v):
        nonlocal result
        result = v

    xso_parser = aioxmpp.xso.XSOParser()
    xso_parser.add_class(cap_xso.Alert, cb)

    driver = aioxmpp.xso.SAXDriver(xso_parser)

    parser = xml.sax.make_parser()
    parser.setFeature(
        xml.sax.handler.feature_namespaces,
        True)
    parser.setFeature(
        xml.sax.handler.feature_external_ges,
        False)
    parser.setContentHandler(driver)

    while True:
        buf = f.read(4096)
        if not buf:
            break
        parser.feed(buf)

    parser.close()

    return result


class FTPPlugin:
    DEFAULT_FTP_PATH = (
        pathlib.PosixPath(
            "/gds/specials/alerts/cap/GER/community_status_geometry/"
        )
    )
    DEFAULT_FTP_SERVER = "ftp-outgoing2.dwd.de"
    DEFAULT_INTERVAL = 60
    DEFAULT_TIMEOUT = 15

    def __init__(self, service, defn):
        super().__init__()
        self.service = service
        self.logger = service.logger.getChild("dwdftp")

        self._data = []

        self._background_task = None

        self._ftp_server = defn.get("ftp_server", self.DEFAULT_FTP_SERVER)
        self._ftp_path = pathlib.PosixPath(defn.get("ftp_path",
                                                    self.DEFAULT_FTP_PATH))
        self._ftp_user = defn["ftp_user"]
        self._ftp_password = defn["ftp_password"]
        self._interval = defn.get("interval", self.DEFAULT_INTERVAL)
        self._ftp_timeout = defn.get("timeout", self.DEFAULT_TIMEOUT)
        self._ftp_client = None
        self._ftp_last_modified = None
        self._mock_data = defn.get("mock_data")

        self._data = []
        self._rect_data = []

        self._backoff_interval = self._interval
        self._max_backoff = self._interval * 16

        self._pubsub_jid = aioxmpp.structs.JID.fromstr(
            defn["pubsub_jid"]
        )
        self._pubsub_node = defn["pubsub_node"]

        self.service.client.on_stream_established.connect(
            self._start_refresher
        )

        self.service.client.on_stream_destroyed.connect(
            self._stop_refresher
        )

        celldata = asyncio.ensure_future(
            asyncio.get_event_loop().run_in_executor(
                None,
                functools.partial(
                    load_cellfile,
                    open(defn["cell_data"], "r"),
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

    @staticmethod
    def _hash_alert(alert):
        hf = hashlib.sha1()
        info = alert.infos[0]
        hf.update(str(info.onset).encode("utf-8"))
        hf.update(b"\x00")
        hf.update(str(info.effective).encode("utf-8"))
        hf.update(b"\x00")
        hf.update(str(info.areas[0].polygon).encode("utf-8"))
        hf.update(b"\x00")
        hf.update(str(info.areas[0].altitude).encode("utf-8"))
        hf.update(b"\x00")
        hf.update(info.category.encode("utf-8"))
        hf.update(b"\x00")
        hf.update(info.response_type.encode("utf-8"))
        hf.update(b"\x00")
        hf.update(str(sorted(info.parameters.items())).encode("utf-8"))
        hf.update(b"\x00")
        hf.update(info.headline.encode("utf-8"))
        hf.update(b"\x00")
        hf.update(info.description.encode("utf-8"))
        hf.update(b"\x00")
        hf.update(info.instruction.encode("utf-8"))
        hf.update(b"\x00")
        hf.update(str(sorted(info.event_codes.items())).encode("utf-8"))
        hf.update(b"\x00")
        hf.update(info.urgency.encode("utf-8"))
        hf.update(b"\x00")
        hf.update(info.severity.encode("utf-8"))
        hf.update(b"\x00")
        hf.update(info.certainty.encode("utf-8"))
        hf.update(b"\x00")
        return hf.hexdigest()

    async def _new_ftp_client(self):
        client = aioftp.Client()
        await client.connect(self._ftp_server)
        await client.login(
            self._ftp_user,
            self._ftp_password
        )

        for part in self._ftp_path.parts[1:]:
            await client.change_directory(part)

        return client

    def _start_refresher(self):
        if self._background_task is not None:
            return

        self._background_task = asyncio.ensure_future(
            self._refresher()
        )
        self._background_task.add_done_callback(
            hintmodules.utils.log_failure(self.logger)
        )

    def _stop_refresher(self):
        if self._background_task is None:
            return

        self._background_task.cancel()
        self._background_task = None

    async def _get_most_recent_zip(self):
        if self._mock_data:
            self.logger.warning("using mocked data!")
            return zipfile.ZipFile(self._mock_data)

        try:
            files = await self._ftp_client.list(recursive=False)
        except (AttributeError, RuntimeError):
            self._ftp_client = await asyncio.wait_for(
                self._new_ftp_client(),
                timeout=self._ftp_timeout,
            )
            files = await self._ftp_client.list(recursive=False)

        # use most recent
        files.sort(key=lambda x: int(x[1]["modify"]), reverse=True)
        filename = files[0][0]
        key = files[0][1]["modify"], files[0][1]["size"], files[0][0]
        self.logger.debug(
            "best candidate: %r (%s bytes, modified %s)",
            str(key[2]),
            key[1],
            key[0]
        )
        if key == self._ftp_last_modified:
            self.logger.info("data on FTP unchanged")
            return None

        buf = io.BytesIO()
        stream = await self._ftp_client.download_stream(filename)
        try:
            while True:
                part = await stream.read()
                if not part:
                    break

                buf.write(part)
        finally:
            await stream.finish()

        buf.seek(0)
        self._ftp_last_modified = key
        return zipfile.ZipFile(buf)

    async def _refresh(self):
        data = []

        zf = await asyncio.wait_for(
            self._get_most_recent_zip(),
            timeout=self._ftp_timeout
        )
        if zf is None:
            self.logger.info("backend suggested that data is fresh")
            return

        with zf:
            for info in zf.infolist():
                if not info.filename.endswith(".xml"):
                    continue
                with zf.open(info.filename) as f:
                    data.append(_read_cap_file(f))

        # "yield"
        await asyncio.sleep(0)

        rect_data = []
        for alert in data:
            for info in alert.infos:
                if not info.areas:
                    continue

                area_with_poly = info.get_area_with_polygon()
                if area_with_poly is None:
                    first_area = info.areas[0]
                    try:
                        celldata = (await self._warnid_to_celldata)[
                            int(first_area.geocodes["WARNCELLID"])
                        ]
                    except KeyError as exc:
                        pass
                    else:
                        print(celldata)

                if area_with_poly is None:
                    continue

                polygon = area_with_poly.polygon
                min_lon = min(lon for lon, _ in polygon)
                min_lat = min(lat for _, lat in polygon)
                max_lon = max(lon for lon, _ in polygon)
                max_lat = max(lat for _, lat in polygon)
                rect_data.append(
                    (
                        ((min_lon, min_lat), (max_lon, max_lat)),
                        polygon,
                        alert,
                    )
                )

        self._data = data
        self._rect_data = rect_data

        await self._push()

    async def _publish_alert(self, alert, hash_):
        pubsub = self.service.pubsub

        await pubsub.publish(
            self._pubsub_jid,
            self._pubsub_node,
            alert,
            id_=hash_
        )

    async def _push(self):
        pubsub = self.service.pubsub

        try:
            await pubsub.create(
                self._pubsub_jid,
                node=self._pubsub_node,
            )
        except aioxmpp.errors.XMPPCancelError as exc:
            if exc.condition == (namespaces.stanzas, "conflict"):
                # node exists, query items
                existing = (await pubsub.get_items(
                    self._pubsub_jid,
                    node=self._pubsub_node,
                )).payload.items
            else:
                raise
        else:
            existing = []

        ids = set(item.id_ for item in existing)
        del existing

        alerts_to_publish = []
        task_futures = []

        for alert in self._data:
            if not alert.infos:
                self.logger.info("alert %r has no infos", alert.identifier)
                return
            hash_ = self._hash_alert(alert)
            try:
                ids.remove(hash_)
            except KeyError:
                continue

            alerts_to_publish.append(
                (alert, hash_)
            )

        self.logger.debug("adding/updating %d items",
                          len(alerts_to_publish))

        if ids:
            self.logger.debug("removing %d obsolete items", len(ids))
            task_futures.extend(
                pubsub.retract(
                    self._pubsub_jid,
                    self._pubsub_node,
                    id_,
                    notify=True,
                )
                for id_ in ids
            )

        await asyncio.gather(*task_futures,
                             return_exceptions=True)

        task_futures = [
            self._publish_alert(alert, hash_)
            for alert, hash_ in alerts_to_publish
        ]

        await asyncio.gather(*task_futures,
                             return_exceptions=True)

    async def _refresher(self):
        while True:
            self._backoff_interval = min(
                self._backoff_interval * 2,
                self._max_backoff
            )

            try:
                await self._refresh()
            except Exception:
                self.logger.exception(
                    "refresh failed, will retry later",
                )
                if self._ftp_client is not None:
                    self._ftp_client.close()
                self._ftp_client = None
            else:
                self._backoff_interval = self._interval

            self.logger.debug("next refresh in %d seconds",
                              self._backoff_interval)
            await asyncio.sleep(self._backoff_interval)

    async def shutdown(self):
        self._stop_refresher()
        try:
            await self._background_task
        except asyncio.CancelledError:
            pass

    async def search_alerts_by_geocoord(self, lat, lon):
        result = []
        for bounds, polygon, alert in self._rect_data:
            (min_lon, min_lat), (max_lon, max_lat) = bounds
            if not (min_lon <= lon <= max_lon and min_lat <= lat <= max_lat):
                continue
            if not point_in_poly(lon, lat, polygon):
                continue
            try:
                if any(point_in_poly(lon, lat, excluded_polygon)
                       for excluded_polygon
                       in cap_xso.PolygonType.parse(
                           alert.infos[0].get_area_with_polygon(
                           ).geocodes["EXCLUDED_POLYGON"]
                       )):
                    continue
            except KeyError:
                pass
            result.append(alert)
        return result


if aioftp is None:
    del FTPPlugin
