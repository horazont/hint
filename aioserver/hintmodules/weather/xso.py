import aioxmpp.stanza
import aioxmpp.xso as xso

from aioxmpp.utils import namespaces

namespaces.hint_weather = "https://xmlns.zombofant.net/xmpp/hint/weather/1.0"


class Aggregated(xso.XSO):
    avg = xso.Attr(
        "avg",
        type_=xso.Float()
    )

    min_ = xso.Attr(
        "min",
        type_=xso.Float(),
        default=None,
    )

    min_t = xso.Attr(
        "min-t",
        type_=xso.DateTime(),
        default=None,
    )

    max_ = xso.Attr(
        "max",
        type_=xso.Float(),
        default=None,
    )

    max_t = xso.Attr(
        "max-t",
        type_=xso.DateTime(),
        default=None,
    )

    def __repr__(self):
        return "<{}.{} min={!r} avg={!r} max={!r}>".format(
            type(self).__module__,
            type(self).__qualname__,
            self.min_,
            self.avg,
            self.max_,
        )


class Temperature(Aggregated):
    TAG = (namespaces.hint_weather, "temp")


class ApparentTemperature(Aggregated):
    TAG = (namespaces.hint_weather, "apparent-temp")


class DewpointTemperature(Aggregated):
    TAG = (namespaces.hint_weather, "dewpoint-temp")


class CloudCover(Aggregated):
    TAG = (namespaces.hint_weather, "cloud-cover")

    type_ = xso.Attr(
        "type",
        validator=xso.RestrictToSet(
            ["low", "mid", "high"],
        ),
        default=None,
    )


class Pressure(Aggregated):
    TAG = (namespaces.hint_weather, "pressure")


class WindSpeed(Aggregated):
    TAG = (namespaces.hint_weather, "wind-speed")


class WindBearing(Aggregated):
    TAG = (namespaces.hint_weather, "wind-bearing")


class Ozone(Aggregated):
    TAG = (namespaces.hint_weather, "ozone")


class Humidity(Aggregated):
    TAG = (namespaces.hint_weather, "humidity")


class Fog(Aggregated):
    TAG = (namespaces.hint_weather, "fog")


class Visibility(Aggregated):
    TAG = (namespaces.hint_weather, "visibility")


class Precipitation(xso.XSO):
    TAG = (namespaces.hint_weather, "precipitation")

    type_ = xso.Attr(
        "type",
        validator=xso.RestrictToSet(
            [
                "rain",
                "snow",
                "sleet",
            ]
        ),
        default=None,
    )

    sum_ = xso.Attr(
        "sum",
        type_=xso.Float()
    )

    min_sum = xso.Attr(
        "min-sum",
        type_=xso.Float(),
        default=None,
    )

    max_sum = xso.Attr(
        "max-sum",
        type_=xso.Float(),
        default=None,
    )

    def __repr__(self):
        return "<{}.{} sum={!r} min-sum={!r} max-sum={!r}>".format(
            type(self).__module__,
            type(self).__qualname__,
            self.sum_,
            self.min_sum,
            self.max_sum,
        )


class Interval(xso.XSO):
    TAG = (namespaces.hint_weather, "interval")

    start = xso.Attr(
        "start",
        type_=xso.DateTime()
    )

    end = xso.Attr(
        "end",
        type_=xso.DateTime()
    )

    apparent_temperature = xso.Child([ApparentTemperature])
    cloud_cover = xso.ChildMap(
        [
            CloudCover,
        ],
        key=lambda x: x.type_
    )
    dewpoint_temperature = xso.Child([DewpointTemperature])
    humidity = xso.Child([Humidity])
    fog = xso.Child([Fog])
    ozone = xso.Child([Ozone])
    precipitation = xso.Child([Precipitation])
    pressure = xso.Child([Pressure])
    temperature = xso.Child([Temperature])
    visibility = xso.Child([Visibility])
    wind_bearing = xso.Child([WindBearing])
    wind_speed = xso.Child([WindSpeed])

    def __repr__(self):
        data = [
            ("apparent_temperature", self.apparent_temperature),
            ("cloud_cover", self.cloud_cover),
            ("dewpoint_temperature", self.dewpoint_temperature),
            ("fog", self.fog),
            ("humidity", self.humidity),
            ("ozone", self.ozone),
            ("pressure", self.pressure),
            ("precipitation", self.precipitation),
            ("temperature", self.temperature),
            ("visibility", self.visibility),
            ("wind_speed", self.wind_speed),
            ("wind_bearing", self.wind_bearing),
        ]

        return "<{}.{}({!r}, {!r}) {}>".format(
            type(self).__module__,
            type(self).__qualname__,
            self.start,
            self.end,
            " ".join(
                "{}={!r}".format(key, value)
                for key, value in data
                if value is not None
            )
        )



class Source(xso.XSO):
    TAG = (namespaces.hint_weather, "source")

    uri = xso.Attr(
        "uri",
    )

    description = xso.ChildText(
        (namespaces.hint_weather, "description"),
        default=None,
    )

    license = xso.ChildText(
        (namespaces.hint_weather, "license"),
        default=None,
    )

    def __init__(self, uri, description=None, license=None):
        super().__init__()
        self.uri = uri
        self.description = description
        self.license = license

    def __repr__(self):
        return "<{}.{}({!r}, description={!r}, license={!r})>".format(
            type(self).__module__,
            type(self).__qualname__,
            self.uri,
            self.description,
            self.license,
        )


class Location(xso.XSO):
    TAG = (namespaces.hint_weather, "location")

    lat = xso.Attr(
        "lat",
        type_=xso.Float()
    )

    lon = xso.Attr(
        "lon",
        type_=xso.Float()
    )

    intervals = xso.ChildList([Interval])

    source = xso.Child([Source])


@aioxmpp.stanza.IQ.as_payload_class
class WeatherRequest(xso.XSO):
    TAG = (namespaces.hint_weather, "weather")

    locations = xso.ChildList([Location])


@aioxmpp.stanza.IQ.as_payload_class
class SourcesRequest(xso.XSO):
    TAG = (namespaces.hint_weather, "sources")

    sources = xso.ChildList([Source])
