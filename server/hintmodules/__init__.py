import urllib.error
import logging

from sleekxmpp import ClientXMPP
from sleekxmpp.xmlstream.handler import Callback
from sleekxmpp.xmlstream.matcher import StanzaPath

import hintmodules.weather.stanza as weather_stanza
import hintmodules.departure.stanza as departure_stanza
import hintmodules.sensor.stanza as sensor_stanza
import hintmodules.errors

__version__ = "0.2"


class ConfigError(Exception):
    pass


class Config:
    @staticmethod
    def require(data, key):
        try:
            return data[key]
        except KeyError as err:
            raise ConfigError(
                "Required config key not found: {}".format(err)) from None

    def __init__(self,
                 controller,
                 modulepath):
        self._controller = controller
        self._modulepath = modulepath
        self.credentials = None
        self.weather_sources = None
        self.departure = None
        self.reload()

    def _validate_credentials(self, credentials):
        if "jid" not in credentials:
            raise ConfigError("jid not in credentials")
        if "password" not in credentials:
            raise ConfigError("password not in credentials")

    def _update(self,
                credentials,
                weather_sources,
                departure,
                sensor_sinks,
                start):
        self._validate_credentials(credentials)
        if (self.credentials is not None and
               credentials != self.credentials):
            # FIXME: run reconnect
            pass

        self.credentials = credentials
        self.weather_sources = weather_sources
        self.departure = departure
        self.sensor_sinks = sensor_sinks
        self.start = start

    @property
    def jid(self):
        return self.credentials["jid"]

    @property
    def password(self):
        return self.credentials["password"]

    @password.deleter
    def password(self):
        self.credentials["password"] = None

    def reload(self):
        data = {}
        with open(self._modulepath, "rb") as f:
            exec(f.read(), data)

        credentials = self.require(data, "credentials")
        try:
            weather_sources = dict(
                self.require(data,
                             "weather_sources"))
        except (TypeError, ValueError) as err:
            raise ConfigError(
                "weather_sources must be dict-able: {}".format(err))
        departure = self.require(data, "departure")
        sensor_sinks = self.require(data, "sensor_sinks")

        start = data.get("start", None)

        self._update(
            credentials,
            weather_sources,
            departure,
            sensor_sinks,
            start)


class HintBot:
    def __init__(self, config):
        self._logger = logging.getLogger("main")
        self._config = Config(self, config)
        self._config.reload()

        self.xmpp = ClientXMPP(self._config.jid, self._config.password)
        del self._config.password

        self.xmpp.register_handler(
            Callback(
                "",
                StanzaPath("iq@type=get/weather_sources"),
                lambda iq: self.xmpp.event("weather_sources.get", iq)))
        self.xmpp.register_handler(
            Callback(
                "",
                StanzaPath("iq@type=get/weather_data"),
                lambda iq: self.xmpp.event("weather_data.get", iq)))
        self.xmpp.register_handler(
            Callback(
                "",
                StanzaPath("iq@type=get/departure"),
                lambda iq: self.xmpp.event("departure.get", iq)))
        self.xmpp.register_handler(
            Callback(
                "",
                StanzaPath("iq@type=set/sensor_data"),
                lambda iq: self.xmpp.event("sensor_data.set", iq)))
        self.xmpp.register_handler(
            Callback(
                "",
                StanzaPath("iq@type=get/sensor_data"),
                lambda iq: self.xmpp.event("sensor_data.get", iq)))

        self.xmpp.add_event_handler(
            "session_start",
            self.session_start)
        self.xmpp.add_event_handler(
            "session_end",
            self.session_end)
        self.xmpp.add_event_handler(
            "weather_sources.get",
            self.get_weather_sources)
        self.xmpp.add_event_handler(
            "weather_data.get",
            self.get_weather_data)
        self.xmpp.add_event_handler(
            "departure.get",
            self.get_departure_data)
        self.xmpp.add_event_handler(
            "sensor_data.set",
            self.set_sensor_data)
        self.xmpp.add_event_handler(
            "sensor_data.get",
            self.get_sensor_data)

        self._weather_geocache = {}

        self._last_measurements = {}

        if self._config.start is not None:
            self._config.start(self)

    def get_departure_data(self, stanza):
        response = self.xmpp.Iq()
        response["to"] = stanza["from"]
        response["id"] = stanza["id"]

        try:
            departures = self._config.departure()
        except hintmodules.errors.ServiceNotAvailable as err:
            response["type"] = "error"
            response["error"]["condition"] = "service-unavailable"
            response["error"]["type"] = "wait"
            if err.__cause__:
                cause = err.__cause__
                if cause.__context__ and \
                   isinstance(cause, urllib.error.URLError):
                    cause = cause.__context__
                response["error"]["text"] = "{}: {!s}".format(
                    err, cause)
            else:
                response["error"]["text"] = "{}".format(err)
            self._logger.warn("returning error (due to %s)", err)
            response.send()
            return

        for lane, dest, remaining_time, timestamp in departures:
            dt = departure_stanza.DepartureTime()
            dt["eta"] = int(remaining_time)
            dt["destination"] = dest
            dt["lane"] = lane
            dt["timestamp"] = timestamp
            response["departure"]["data"].append(dt)

        response["type"] = "result"
        response.send()

    @staticmethod
    def _weather_service_key(uri, lat, lon):
        return (uri,
                "{:.4f}".format(lat),
                "{:.4f}".format(lon))

    def get_weather_service_for(self, uri, lat, lon):
        try:
            service_entry = self._config.weather_sources[uri]
        except KeyError:
            raise ValueError("No such weather service: {}".format(uri)) from None

        try:
            servicecls, per_coord = service_entry
        except (ValueError, TypeError):
            servicecls = service_entry
            per_coord = True

        if per_coord:
            key = self._weather_service_key(uri, lat, lon)
        else:
            key = uri

        try:
            return self._weather_geocache[key]
        except KeyError:
            if per_coord:
                service = servicecls(lat, lon)
            else:
                service = servicecls

            self._weather_geocache[key] = service
            return service

    def get_weather_data(self, orig_iq):
        lat = orig_iq["weather_data"]["location"]["lat"]
        lon = orig_iq["weather_data"]["location"]["lon"]
        uri = orig_iq["weather_data"]["from"]

        response = self.xmpp.Iq()
        response["to"] = orig_iq["from"]
        response["id"] = orig_iq["id"]

        try:
            service = self.get_weather_service_for(
                uri, lat, lon)
        except ValueError as err:
            print("ENOSERVICE")
            response["type"] = "error"
            response["error"]["condition"] = "undefined-condition"
            response["error"]["type"] = "modify"
            response["error"]["text"] = str(err)
            response.send()
            return

        try:
            weather_data = weather_stanza.Data()
            for request_stanza in orig_iq["weather_data"]:
                if request_stanza.name == weather_stanza.Interval.name:
                    service.query_interval(lat, lon, request_stanza)
                    weather_data.append(request_stanza)
                elif request_stanza.name == weather_stanza.PointData.name:
                    service.query_data_point(lat, lon, request_stanza)
                    weather_data.append(request_stanza)
        except ValueError as err:
            print("EINTERVAL")
            response["type"] = "error"
            response["error"]["condition"] = "undefined-condition"
            response["error"]["type"] = "modify"
            response["error"]["text"] = str(err)
            response.send()
            return
        except hintmodules.errors.ServiceNotAvailable as err:
            print("service not available")
            response["type"] = "error"
            response["error"]["type"] = "cancel"
            response["error"]["condition"] = "service-unavailable"
            if err.__cause__:
                cause = err.__cause__
                if cause.__context__ and \
                   isinstance(cause, urllib.error.URLError):
                    cause = cause.__context__
                response["error"]["text"] = "{}: {!s}".format(
                    err, cause)
            else:
                response["error"]["text"] = "{}".format(err)
            response.send()
            return

        response.append(weather_data)
        response["type"] = "result"

        response.send()

    def get_weather_sources(self, orig_iq):
        response = weather_stanza.Sources()
        for key, sourcecls in self._config.weather_sources.items():
            source = weather_stanza.Source()
            source["uri"] = key
            source["license"] = sourcecls.LICENSE
            source["name"] = sourcecls.NAME
            response.append(source)

        iq = self.xmpp.Iq()
        iq['to'] = orig_iq['from']
        iq['type'] = 'result'
        iq['id'] = orig_iq['id']
        iq.set_payload(response)
        iq.send()

    def set_sensor_data(self, orig_iq):
        unpacked_data = sorted(
            (
                (point["sensor_type"], point["sensor_id"], point["time"], point["raw_value"])
                for point in orig_iq["sensor_data"]
            ),
            key=lambda x: x[2])

        for sensor_type, sensor_id, time, raw_value in unpacked_data:
            if sensor_type != "T":
                self._logger.warn("Unknown sensor type: %s", sensor_type)
                continue

            try:
                dest = self._config.sensor_sinks[sensor_id]
            except KeyError:
                self._logger.warn("No sensor sink configured for id %r",
                                  sensor_id)
                continue

            if sensor_type == "T":
                value = raw_value / 16.
            else:
                value = raw_value

            sensor_key = sensor_type, sensor_id
            try:
                prev_time, *_ = self._last_measurements[sensor_key]
                if prev_time < time:
                    prev_time = None
            except KeyError:
                prev_time = None

            if prev_time is None:
                self._last_measurements[sensor_key] = time, value

            dest.put_sensor_value(time, value)

            self._logger.debug("logged data for sensor %s", sensor_id)

        iq = self.xmpp.Iq()
        iq['to'] = orig_iq['from']
        iq['type'] = 'result'
        iq['id'] = orig_iq['id']
        iq.send()

    def get_sensor_data(self, orig_iq):
        iq = self.xmpp.Iq()
        iq['to'] = orig_iq['from']
        iq['type'] = 'result'
        iq['id'] = orig_iq['id']

        response_data = iq["sensor_data"]

        for request in orig_iq["sensor_data"]["substanzas"]:
            sensor_key = request["sensor_type"], request["sensor_id"]

            try:
                time, value = self._last_measurements[sensor_key]
            except KeyError:
                self._logger.warn("received sensor request for sensor %s, "
                                  "but no data available.", sensor_key)
                continue

            point = sensor_stanza.Point(
                parent=response_data)

            point["sensor_type"], point["sensor_id"] = sensor_key
            point["time"] = time
            point["value"] = value

        iq.send()

    def session_start(self, event):
        self._departure_push_destinations = set()
        self.xmpp.send_presence()

    def session_end(self, event):
        self._departure_push_destinations = set()

    def run(self):
        self.xmpp.connect()
        self.xmpp.process(block=True)

def get_default_user_agent():
    return "hintbot/{}".format(__version__)
