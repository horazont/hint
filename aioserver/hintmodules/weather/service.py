import math

import aioxmpp.errors

from aioxmpp.utils import namespaces

import hintmodules.service

from . import xso as weather_xso


def aggregate_avg(items, attrname):
    sum_ = 0
    min_ = None
    max_ = None

    count = 0
    for item in items:
        value = getattr(item, attrname)
        if value is None:
            continue

        sum_ += value
        count += 1
        min_ = value if min_ is None else min(min_, value)
        max_ = value if max_ is None else max(max_, value)

    if count == 0:
        return None

    result = {
        "avg": sum_/count,
    }

    if count > 1:
        result.update({
            "min_": min_,
            "max_": max_,
        })

    return result


def aggregate_angle(items, attrname):
    sum_sin = 0
    sum_cos = 0

    count = 0
    for item in items:
        value = getattr(item, attrname)
        if value is None:
            continue

        sum_sin += math.sin(value)
        sum_cos += math.cos(value)
        count += 1

    if count == 0:
        return None

    result = {
        "avg": math.atan2(sum_sin/count, sum_cos/count),
    }

    return result


def aggregate_sum(items, attrname):
    sum_ = 0

    count = 0
    for item in items:
        value = getattr(item, attrname)
        if value is None:
            continue

        sum_ += value
        count += 1

    if count == 0:
        return None

    return sum_


def aggregate_construct(items, srcattr,
                        class_,
                        aggregator=aggregate_avg):
    pack = aggregator(items, srcattr)
    if pack is None:
        return None

    instance = class_()
    for attr, value in pack.items():
        setattr(instance, attr, value)

    return instance


def select_intervals(start, end, intervals):
    if not intervals:
        return [], False

    start_candidates = [
        interval
        for interval in intervals
        if interval.start == intervals[0].start
    ]
    start_candidates.reverse()

    best_candidate_loss = None
    best_candidate = []

    for start_interval in start_candidates:
        chain = [start_interval]
        prev = start_interval.end

        for interval in intervals:
            if interval.start == prev and interval.end <= end:
                chain.append(interval)
                prev = interval.end
                if prev == end:
                    return chain, chain[0].start == start

        loss = (
            abs((chain[0].start - start).total_seconds()) +
            abs((chain[-1].end - end).total_seconds())
        )

        if best_candidate_loss is None or best_candidate_loss > loss:
            best_candidate_loss = loss
            best_candidate = chain

    return best_candidate, False


class Service(hintmodules.service.HintService):
    def __init__(self, client, **kwargs):
        super().__init__(client, **kwargs)

        self.client.stream.register_iq_request_coro(
            "get",
            weather_xso.WeatherRequest,
            self._get_weather_info
        )

        self.client.stream.register_iq_request_coro(
            "get",
            weather_xso.SourcesRequest,
            self._get_sources_info
        )

    async def _get_sources_info(self, request):
        answer = weather_xso.SourcesRequest()

        for uri, plugin in self._plugins.items():
            answer.sources.append(
                weather_xso.Source(
                    uri,
                    description=plugin.DESCRIPTION,
                    license=plugin.DEFAULT_LICENSE,
                )
            )

        return answer

    def get_plugin_by_uri(self, uri):
        return self._plugins[uri]

    async def _get_weather_info(self, request):
        try:
            requests = [
                (self._plugins[location.source.uri],
                 location.source.uri,
                 round(location.lat, 6),
                 round(location.lon, 6),
                 location.intervals)
                for location in request.payload.locations
            ]
        except KeyError as exc:
            raise aioxmpp.errors.XMPPModifyError(
                (namespaces.stanzas, "item-not-found"),
                text="No such service: {!r}".format(str(exc)),
            )

        if self.ratelimit is not None:
            actions = [
                ("weather", uri)
                for _, uri, *_ in requests
            ]

            self.ratelimit.enforce_limit(
                request,
                actions,
            )

        result = weather_xso.WeatherRequest()

        for source, uri, lat, lon, intervals in requests:
            response = weather_xso.Location()
            response.source = weather_xso.Source(
                uri,
                description=source.DESCRIPTION,
                license=source.DEFAULT_LICENSE,
            )
            response.lat = lat
            response.lon = lon

            for req_interval in intervals:
                resp_interval = weather_xso.Interval()
                resp_interval.start = req_interval.start
                resp_interval.end = req_interval.end

                await self._calc_interval_from_source(
                    source,
                    lat,
                    lon,
                    req_interval.start,
                    req_interval.end,
                    resp_interval,
                )

                response.intervals.append(resp_interval)

            result.locations.append(response)

        return result

    async def _calc_interval_from_source(
             self, source, lat, lon, start, end, into):
        self.logger.debug(
            "collecting data from %r for location (%.6f, %.6f) within "
            "[%s, %s]",
            source,
            lat, lon,
            start, end
        )

        datapoints, intervals = await source.get_data(
            lat, lon,
        )

        datapoints = [
            item for item in datapoints
            if start <= item.timestamp <= end
        ]

        if intervals:
            self.logger.debug(
                "first interval [%s, %s]",
                intervals[0].start,
                intervals[0].end
            )

        intervals = [
            item for item in intervals
            if start <= item.start < end and start < item.end <= end
        ]

        self.logger.debug(
            "%d datapoint candidates, %d interval candidates",
            len(datapoints),
            len(intervals)
        )

        intervals, accurate = select_intervals(
            start,
            end,
            intervals
        )

        self.logger.debug(
            "using intervals %r (accurate=%r)",
            intervals,
            accurate
        )

        into.apparent_temperature = aggregate_construct(
            datapoints,
            "apparent_temperature",
            weather_xso.ApparentTemperature,
        )

        into.dewpoint_temperature = aggregate_construct(
            datapoints,
            "dewpoint_temperature",
            weather_xso.DewpointTemperature,
        )

        into.temperature = aggregate_construct(
            datapoints,
            "temperature",
            weather_xso.Temperature,
        )

        into.fog = aggregate_construct(
            datapoints,
            "fog",
            weather_xso.Fog,
        )

        into.humidity = aggregate_construct(
            datapoints,
            "humidity",
            weather_xso.Humidity,
        )

        into.ozone = aggregate_construct(
            datapoints,
            "ozone",
            weather_xso.Ozone,
        )

        into.pressure = aggregate_construct(
            datapoints,
            "pressure",
            weather_xso.Pressure,
        )

        into.visibility = aggregate_construct(
            datapoints,
            "visibility",
            weather_xso.Visibility,
        )

        into.wind_speed = aggregate_construct(
            datapoints,
            "wind_speed",
            weather_xso.WindSpeed,
        )

        into.wind_bearing = aggregate_construct(
            datapoints,
            "wind_bearing",
            weather_xso.WindBearing,
            aggregator=aggregate_angle
        )

        for type_ in [
                "low",
                "mid",
                "high",
                None]:
            attr = "cloud_cover"
            if type_ is not None:
                attr += "_" + type_

            item = aggregate_construct(
                datapoints,
                attr,
                weather_xso.CloudCover,
            )
            if item is None:
                continue

            item.type_ = type_

            into.cloud_cover[type_] = [item]

        precipitation = aggregate_sum(
            intervals,
            "precipitation",
        )

        precipitation_min = aggregate_sum(
            intervals,
            "precipitation_min",
        )

        precipitation_max = aggregate_sum(
            intervals,
            "precipitation_max",
        )

        if (precipitation is not None or
                precipitation_min is not None or
                precipitation_max is not None):
            instance = weather_xso.Precipitation()
            instance.sum_ = precipitation
            instance.min_sum = precipitation_min
            instance.max_sum = precipitation_max
            into.precipitation = instance
