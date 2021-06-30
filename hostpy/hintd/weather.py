import asyncio
import enum
import logging
import math
import os
import typing

from datetime import timedelta, datetime

import pytz

import aioxmpp

import hintlib.cache
import hintlib.weather.service
import hintlib.services
import hintlib.utils
import hintlib.xso

import hintd.ui
from hintd.cconstants import (
    LPCTableAlignment,
    LPCFont,
    TableColumnEx,
)
from hintd.ui import metrics


class WeatherModelItem(typing.NamedTuple):
    start: datetime
    temperature: typing.Optional[float]
    precipitation: typing.Optional[float]
    precipitation_probability: typing.Optional[float]
    cloud_cover: typing.Optional[float]


def unpack_avg(agg):
    if agg is None:
        return None
    if agg.avg is None:
        return None
    return agg.avg


def unpack_sum(agg):
    if agg is None:
        return None
    if agg.sum_ is None:
        return None
    return agg.sum_


def format_dynamic_number(v):
    w = '0' if abs(v) > 9.5 else '1'
    sign = '–' if v < 0 else ''
    fmt = f"{sign}{{:.{w}f}}"
    return fmt.format(v)


def clamp(v, min_, max_):
    return max(min(v, max_), min_)


def cubehelix(gray, s, r, h) -> typing.Tuple[int, int, int]:
    a = h * gray * (1 - gray) / 2
    phi = 2 * math.pi * (s / 3 + r * gray)
    cos_phi = math.cos(phi)
    sin_phi = math.sin(phi)
    rf = clamp(
        gray + a*(-0.14861*cos_phi + 1.78277*sin_phi),
        0.0, 1.0,
    )
    gf = clamp(
        gray + a*(-0.29227*cos_phi - 0.90649*sin_phi),
        0.0, 1.0,
    )
    bf = clamp(
        gray + a*(1.97294*cos_phi),
        0.0, 1.0,
    )

    return round(rf*255), round(gf*255), round(bf*255)


def tempcolour(minT, maxT, T):
    normT = clamp((T - minT) / (maxT - minT), 0.0, 1.0)
    return cubehelix(normT, math.pi / 12., -1.0, 2.0)


def hsv_to_rgb24(h, s, v):
    if s == 0:
        return (round(v*255),)*3

    h = h % (math.pi*2)

    indexf = h / (math.pi*2 / 6)
    index = math.floor(indexf)
    fractional = indexf - index

    p = v * (1.0 - s)
    q = v * (1.0 - (s * fractional))
    t = v * (1.0 - (s * (1.0 - fractional)))

    return tuple(round(v*255) for v in [
        (v, t, p),
        (q, v, p),
        (p, v, t),
        (p, q, v),
        (t, p, v),
        (v, p, q)
    ][index])


def luminance(r, g, b):
    return (
        (r/255) * 0.2126 +
        (g/255) * 0.7152 +
        (b/255) * 0.0722
    )


def get_text_colour(r, g, b):
    if luminance(r, g, b) < 0.69:
        return (255, 255, 255)
    else:
        return (0, 0, 0)


def cloudcolour(cloudiness, precipitation):
    precipitation /= 5
    cloudiness = clamp(cloudiness/1.5, 0.0, 0.6667)
    precipitation = max(precipitation, 0)

    return hsv_to_rgb24(
        (min(max(precipitation - 1.0, 0.0)/ 3.0, 1.0/3.0) + 2/3) * math.pi + 2,
        min(precipitation, 1.0),
        1.0 - cloudiness,
    )


class DirtKind(enum.Enum):
    SENSOR_GROUPS = 0
    FORECAST = 1


class WeatherScreen(hintd.ui.Screen):
    TIME_COLUMN_WIDTH = 42
    WEATHER_COLUMN_WIDTH = 21
    BAR_HEIGHT = 48

    TEMP_MIN = -10.0
    TEMP_MAX = 40.0

    TEXT_HEIGHT = 11
    TEXT_BASELINE = 9

    SENSOR_TEXT_HEIGHT = 14
    SENSOR_TEXT_BASELINE = 12
    SENSOR_DATA_COLUMN_WIDTH = 25

    PRECIPITATION_PROBABILITY_THRESHOLD = 0.005
    PRECIPITATION_AMOUNT_THRESHOLD = 0.05

    GROUP_MARGIN = 2

    def __init__(self):
        super().__init__(
            "Enviro",
            "Wetterdaten",
        )
        self.tz = pytz.UTC
        self.forecast = []
        self.sensor_groups = []
        self.sensor_data = {}
        self._dirt = {DirtKind.SENSOR_GROUPS, DirtKind.FORECAST}

    def _paint_weather_bar(self, ui, x0, y0, prev_time, items):
        assert len(items) == 6

        time_columns = []

        data_columns_temp = []
        data_columns_precip = []

        for item in items:
            local_prev_time = self.tz.normalize(prev_time)
            local_start_time = self.tz.normalize(item.start)

            if local_prev_time.date() != local_start_time.date():
                time_fmt = "%d %b"
            else:
                time_fmt = "%H:%M"
            time_label = local_start_time.strftime(time_fmt)
            time_columns.append(time_label)

            prev_time = item.start

            temp_celsius = hintlib.utils.kelvin_to_celsius(item.temperature)
            temp_bgcolour = tempcolour(self.TEMP_MIN, self.TEMP_MAX,
                                       temp_celsius)
            temp_fgcolour = get_text_colour(*temp_bgcolour)
            temp_label = format_dynamic_number(temp_celsius)
            temp_unit = "°C"

            data_columns_temp.append(TableColumnEx(
                bgcolour=temp_bgcolour,
                fgcolour=temp_fgcolour,
                text=temp_label,
                alignment=LPCTableAlignment.RIGHT,
            ))

            data_columns_temp.append(TableColumnEx(
                bgcolour=temp_bgcolour,
                fgcolour=temp_fgcolour,
                text=temp_unit,
                alignment=LPCTableAlignment.LEFT,
            ))

            cloud_bgcolour = cloudcolour(
                item.cloud_cover,
                0.0
            )
            cloud_fgcolour = get_text_colour(*cloud_bgcolour)

            if (item.precipitation_probability is not None and
                    item.precipitation_probability >=
                    self.PRECIPITATION_PROBABILITY_THRESHOLD):
                cloud_label = "{:.0f}".format(item.precipitation_probability * 100)
            else:
                cloud_label = ""

            data_columns_precip.append(TableColumnEx(
                bgcolour=cloud_bgcolour,
                fgcolour=cloud_fgcolour,
                text=cloud_label,
                alignment=LPCTableAlignment.CENTER,
            ))

            precip_bgcolour = cloudcolour(
                0.0,
                item.precipitation,
            )
            precip_fgcolour = get_text_colour(*precip_bgcolour)

            if item.precipitation >= self.PRECIPITATION_AMOUNT_THRESHOLD:
                precip_label = format_dynamic_number(item.precipitation)
            else:
                precip_label = ""

            data_columns_precip.append(TableColumnEx(
                bgcolour=precip_bgcolour,
                fgcolour=precip_fgcolour,
                text=precip_label,
                alignment=LPCTableAlignment.CENTER,
            ))

        ui.table_start(
            x0, y0 + self.TEXT_BASELINE,
            self.TEXT_HEIGHT,
            [
                (self.TIME_COLUMN_WIDTH, LPCTableAlignment.LEFT)
            ] * 6,
        )

        ui.table_row(
            LPCFont.DEJAVU_SANS_9PX,
            metrics.THEME_CLIENT_AREA_COLOUR,
            metrics.THEME_CLIENT_AREA_BACKGROUND_COLOUR,
            time_columns,
        )

        ui.table_start(
            x0, y0 + self.TEXT_BASELINE + self.TEXT_HEIGHT,
            self.TEXT_HEIGHT,
            [
                (self.WEATHER_COLUMN_WIDTH, LPCTableAlignment.RIGHT),
                (self.WEATHER_COLUMN_WIDTH, LPCTableAlignment.LEFT),
            ] * 6,
        )

        ui.table_row_ex(
            LPCFont.DEJAVU_SANS_9PX,
            data_columns_temp,
        )

        ui.table_row_ex(
            LPCFont.DEJAVU_SANS_9PX,
            data_columns_precip,
        )

        return prev_time

    def _paint_sensor_group(self, ui, x0, y0, width, group, really):
        if really:
            ui.fill_rect(
                x0, y0,
                x0, y0 + self.SENSOR_TEXT_HEIGHT,
                metrics.THEME_CLIENT_AREA_BACKGROUND_COLOUR,
            )

            ui.draw_text(
                x0, y0 + self.SENSOR_TEXT_BASELINE,
                LPCFont.DEJAVU_SANS_12PX,
                metrics.THEME_CLIENT_AREA_COLOUR,
                group.label,
            )

        y0 += self.SENSOR_TEXT_HEIGHT

        if really:
            ui.table_start(
                x0, y0 + self.SENSOR_TEXT_BASELINE,
                self.SENSOR_TEXT_HEIGHT,
                [
                    (self.SENSOR_DATA_COLUMN_WIDTH, LPCTableAlignment.RIGHT),
                    (self.SENSOR_DATA_COLUMN_WIDTH, LPCTableAlignment.LEFT),
                ],
            )

            for row in group.rows:
                try:
                    value = self.sensor_data[row.sensor][row.value]
                except KeyError:
                    value = None

                if value is None:
                    cols = unset_render_func()
                else:
                    cols = row.render_func(value)

                ui.table_row_ex(
                    LPCFont.DEJAVU_SANS_12PX,
                    cols,
                )

        return y0 + self.SENSOR_TEXT_HEIGHT * len(group.rows)

    def _paint_sensors(self, ui):
        x0 = metrics.SCREEN_CLIENT_AREA_LEFT
        y0 = metrics.SCREEN_CLIENT_AREA_TOP

        if not self.sensor_groups:
            return y0

        group_width = (metrics.SCREEN_CLIENT_AREA_WIDTH
                       - self.GROUP_MARGIN * (len(self.sensor_groups)-1)
                       ) // len(self.sensor_groups)

        really = DirtKind.SENSOR_GROUPS in self._dirt

        y1 = y0
        for group in self.sensor_groups:
            y1 = max(self._paint_sensor_group(
                ui, x0, y0, group_width, group, really,
            ), y1)
            x0 += group_width + self.GROUP_MARGIN

        if y1 != y0:
            y1 += self.GROUP_MARGIN

        return y1

    def paint(self):
        y0 = self._paint_sensors(self._ui)

        if DirtKind.FORECAST not in self._dirt:
            return

        prev_time = datetime.utcnow().replace(tzinfo=pytz.UTC)

        for i in range(4):
            offset = i*6
            end = (i+1)*6

            prev_time = self._paint_weather_bar(
                self._ui,
                metrics.SCREEN_CLIENT_AREA_LEFT,
                y0 + self.BAR_HEIGHT*i,
                prev_time,
                self.forecast[offset:end]
            )

    def activate(self, ui):
        super().activate(ui)
        self.invalidate()

    def invalidate(self, part=None):
        if part is None:
            self._dirt = {DirtKind.FORECAST, DirtKind.SENSOR_GROUPS}
        else:
            self._dirt.add(part)
        return super().invalidate()


class ForecastService:
    def __init__(self, screen: WeatherScreen):
        self.logger = logging.getLogger(
            ".".join([__name__, type(self).__qualname__])
        )
        self.screen = screen
        self.forecast_poll_interval = timedelta(minutes=5)
        self._forecast_worker_task = hintlib.services.RestartingTask(
            self._forecast_worker
        )
        self._forecast_worker_task.start()
        self._backend = None
        self._tz = pytz.UTC

        self.screen.on_activate.connect(self._handle_screen_activated)
        self.screen.on_deactivate.connect(self._handle_screen_deactivated)

    def _handle_screen_activated(self):
        # TODO: reset backoff timer and requery now if failing
        pass

    def _handle_screen_deactivated(self):
        pass

    def _generate_intervals(self):
        base = datetime.utcnow().replace(minute=0, second=0, microsecond=0)
        for h in range(1, 25):
            yield (base + timedelta(hours=h), base + timedelta(hours=h+1))

    async def _poll_forecast(self):
        intervals = list(self._generate_intervals())
        data = await hintlib.weather.service.intervals_from_source(
            self._backend,
            self._lat,
            self._lon,
            intervals,
        )

        items = []
        for interval in data:
            items.append(WeatherModelItem(
                start=interval.start.replace(tzinfo=pytz.UTC),
                precipitation=unpack_sum(interval.precipitation),
                precipitation_probability=interval.precipitation_probability,
                temperature=unpack_avg(interval.temperature),
                cloud_cover=unpack_avg(interval.cloud_cover),
            ))

        self.screen.forecast[:] = items
        self.screen.invalidate(DirtKind.FORECAST)

    async def _forecast_worker(self):
        while True:
            await self._poll_forecast()

            interval = self.forecast_poll_interval
            self.logger.debug("running next poll in %s", interval)
            await asyncio.sleep(interval.total_seconds())

    def configure(self, cfg):
        backend_type = cfg["backend"]["type"]
        class_ = hintlib.utils.get_class_by_path(backend_type, logger=self.logger)

        tz = pytz.timezone(cfg.get("timezone", os.environ.get("TZ", "Etc/UTC")))

        lat, lon = cfg["lat"], cfg["lon"]
        if not isinstance(lat, float):
            raise TypeError("latitute must be a float")
        if not isinstance(lon, float):
            raise TypeError("longitude must be a float")

        self._backend = class_(cfg["backend"])
        self.screen.tz = tz
        self._lat = lat
        self._lon = lon


class SensorRow(typing.NamedTuple):
    render_func: typing.Callable
    label: str
    sensor: str
    value: str


class SensorGroup(typing.NamedTuple):
    label: str
    rows: typing.Sequence[SensorRow]


class SensorSource(typing.NamedTuple):
    service: aioxmpp.JID
    node: str
    name: str


def unset_render_func() -> typing.Tuple[TableColumnEx, TableColumnEx]:
    return (
        TableColumnEx(
            bgcolour=[0, 0, 0],
            fgcolour=[255, 255, 255],
            text="?",
            alignment=LPCTableAlignment.RIGHT,
        ),
        TableColumnEx(
            bgcolour=[0, 0, 0],
            fgcolour=[255, 255, 255],
            text="?",
            alignment=LPCTableAlignment.LEFT,
        )
    )


def temperature_render_func(
        value: float
        ) -> typing.Tuple[TableColumnEx, TableColumnEx]:
    temp_celsius = value
    temp_bgcolour = tempcolour(
        WeatherScreen.TEMP_MIN,
        WeatherScreen.TEMP_MAX,
        temp_celsius
    )
    temp_fgcolour = get_text_colour(*temp_bgcolour)
    temp_label = format_dynamic_number(temp_celsius)
    temp_unit = "°C"

    return (
        TableColumnEx(
            bgcolour=temp_bgcolour,
            fgcolour=temp_fgcolour,
            text=temp_label,
            alignment=LPCTableAlignment.RIGHT,
        ),
        TableColumnEx(
            bgcolour=temp_bgcolour,
            fgcolour=temp_fgcolour,
            text=temp_unit,
            alignment=LPCTableAlignment.LEFT,
        )
    )


def humidity_render_func(
        value: float
        ) -> typing.Tuple[TableColumnEx, TableColumnEx]:
    label = format_dynamic_number(value)
    unit = "%"

    if value >= 50:
        hue = 240 / 180 * math.pi
        saturation = (value - 50) / 50
    else:
        hue = 36 / 180 * math.pi
        saturation = 1 - value / 50

    bgcolour = hsv_to_rgb24(hue, saturation, 1.0)
    fgcolour = get_text_colour(*bgcolour)

    return (
        TableColumnEx(
            bgcolour=bgcolour,
            fgcolour=fgcolour,
            text=label,
            alignment=LPCTableAlignment.RIGHT,
        ),
        TableColumnEx(
            bgcolour=bgcolour,
            fgcolour=fgcolour,
            text=unit,
            alignment=LPCTableAlignment.LEFT,
        )
    )


def compile_temperature_sensor_row(row_cfg):
    return SensorRow(
        render_func=temperature_render_func,
        label=row_cfg.get("label"),
        sensor=row_cfg["sensor"],
        value=row_cfg["value"],
    )


def compile_humidity_sensor_row(row_cfg):
    return SensorRow(
        render_func=humidity_render_func,
        label=row_cfg.get("label"),
        sensor=row_cfg["sensor"],
        value=row_cfg["value"],
    )


SENSOR_TYPES = {
    "temperature": compile_temperature_sensor_row,
    "humidity": compile_humidity_sensor_row,
}


def compile_sensor_row(row_cfg):
    return SENSOR_TYPES[row_cfg["type"]](row_cfg)


def compile_sensor_group(group_cfg):
    return SensorGroup(
        label=group_cfg["label"],
        rows=list(map(compile_sensor_row, group_cfg["rows"]))
    )


def compile_sensor_groups(groups_cfg):
    return list(map(compile_sensor_group, groups_cfg))


def compile_sensor_source(source_cfg):
    return SensorSource(
        service=aioxmpp.JID.fromstr(source_cfg["service"]),
        node=source_cfg["node"],
        name=source_cfg["name"],
    )


def compile_sensor_sources(sources_cfg):
    return {(item.service, item.node): item
            for item in map(compile_sensor_source, sources_cfg)}


class SensorsService:
    SUBSCRIPTION_INTERVAL = timedelta(seconds=120)

    def __init__(self,
                 screen: WeatherScreen,
                 client: aioxmpp.PubSubClient):
        super().__init__()
        self.logger = logging.getLogger(
            ".".join([__name__, type(self).__qualname__])
        )
        self.screen = screen
        self._sources = {}
        self._groups = []
        self._data = {}
        self._client = client

        self._client.on_item_published.connect(self._handle_item_published)

        self._subscription_task = hintlib.services.RestartingTask(
            self._subscription_worker
        )
        self._subscription_task.start()

    def _handle_item_published(self, jid, node, item, *, message=None):
        try:
            source = self._sources[jid, node]
        except KeyError:
            return

        if not isinstance(item.registered_payload, hintlib.xso.SampleBatch):
            self.logger.warning("received non-sample-batch pubsub event")
            return

        try:
            self._data[source.name] = {
                sample.subpart: sample.value
                for sample in item.registered_payload.samples
            }
        except BaseException as exc:
            self.logger.error("failed to process sample batch", exc_info=True)

        self.screen.sensor_data = self._data
        self.screen.sensor_groups = self._groups
        self.screen.invalidate(DirtKind.SENSOR_GROUPS)

    async def _ensure_subscription(self, service: aioxmpp.JID, node: str):
        try:
            await self._client.subscribe(
                service,
                node,
            )
        except aioxmpp.errors.XMPPError as exc:
            if exc.condition != aioxmpp.ErrorCondition.CONFLICT:
                raise

    async def _ensure_subscriptions(self):
        await asyncio.gather(*(
            self._ensure_subscription(source.service, source.node)
            for source in self._sources.values()
        ))

    async def _subscription_worker(self):
        while True:
            await self._ensure_subscriptions()
            await asyncio.sleep(self.SUBSCRIPTION_INTERVAL.total_seconds())

    def configure(self, sensors_cfg):
        sources = compile_sensor_sources(sensors_cfg["sources"])
        groups = compile_sensor_groups(sensors_cfg["groups"])
        self._sources = sources
        self._groups = groups
        self._data.clear()
        self.screen.sensor_groups = self._groups
        self._subscription_task.restart()
