import urllib.error
import runpy
import logging
import copy
from sleekxmpp.exceptions import IqError, IqTimeout
from sleekxmpp.xmlstream import ET, JID
from sleekxmpp import Iq, Message, ClientXMPP
from sleekxmpp.xmlstream.handler import Callback
from sleekxmpp.xmlstream.matcher import StanzaPath

import hintmodules.weather.stanza as weather_stanza
import hintmodules.departure.stanza as departure_stanza
import hintmodules.errors

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

    def __init__(
            self,
            controller,
            modulepath):
        self._controller = controller
        self._modulepath = modulepath
        self.credentials = None
        self.weather_sources = None
        self.departure = None
        self.reload()

    def _validate_credentials(self, credentials):
        if not "jid" in credentials:
            raise ConfigError("jid not in credentials")
        if not "password" in credentials:
            raise ConfigError("password not in credentials")

    def _update(
            self,
            credentials,
            weather_sources,
            departure):
        self._validate_credentials(credentials)
        if (self.credentials is not None and
            credentials != self.credentials):
            # FIXME: run reconnect
            pass

        self.credentials = credentials
        self.weather_sources = weather_sources
        self.departure = departure

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
        data = runpy.run_path(
            self._modulepath,
            init_globals={
                "controller": self._controller
            },
            run_name="hint_config")

        credentials = self.require(data, "credentials")
        try:
            weather_sources = dict(
                self.require(data,
                             "weather_sources"))
        except (TypeError, ValueError) as err:
            raise ConfigError(
                "weather_sources must be dict-able: {}".format(err))
        departure = self.require(data, "departure")

        self._update(
            credentials,
            weather_sources,
            departure)

class HintBot:
    def __init__(self, config):
        self._logger = logging.getLogger("main")
        self._config = Config(self, config)
        self._config.reload()

        self._xmpp = ClientXMPP(
            self._config.jid, self._config.password)
        del self._config.password

        self._xmpp.register_handler(
            Callback(
                "",
                StanzaPath("iq@type=get/weather_sources"),
                lambda iq: self._xmpp.event("weather_sources.get", iq)))
        self._xmpp.register_handler(
            Callback(
                "",
                StanzaPath("iq@type=get/weather_data"),
                lambda iq: self._xmpp.event("weather_data.get", iq)))
        self._xmpp.register_handler(
            Callback(
                "",
                StanzaPath("iq@type=get/departure"),
                lambda iq: self._xmpp.event("departure.get", iq)))

        self._xmpp.add_event_handler(
            "session_start",
            self.session_start)
        self._xmpp.add_event_handler(
            "session_end",
            self.session_end)
        self._xmpp.add_event_handler(
            "weather_sources.get",
            self.get_weather_sources)
        self._xmpp.add_event_handler(
            "weather_data.get",
            self.get_weather_data)
        self._xmpp.add_event_handler(
            "departure.get",
            self.get_departure_data)

        self._weather_geocache = {}

    def get_departure_data(self, stanza):
        departures = self._config.departure()

        response = self._xmpp.Iq()
        for lane, dest, remaining_time in departures:
            dt = departure_stanza.DepartureTime()
            dt["eta"] = int(remaining_time)
            dt["destination"] = dest
            dt["lane"] = lane
            response["departure"]["data"].append(dt)

        response["to"] = stanza["from"]
        response["type"] = "result"
        response["id"] = stanza["id"]
        response.send()

    @staticmethod
    def _weather_service_key(uri, lat, lon):
        return (uri,
                "{:.4f}".format(lat),
                "{:.4f}".format(lon))

    def _get_weather_service_for(self, uri, lat, lon):
        if uri not in self._config.weather_sources:
            raise ValueError("No such weather service: {}".format(uri))

        key = self._weather_service_key(uri, lat, lon)
        try:
            return self._weather_geocache[key]
        except KeyError:
            service = self._config.weather_sources[uri](lat, lon)
            self._weather_geocache[key] = service
            return service

    def get_weather_data(self, orig_iq):
        lat = orig_iq["weather_data"]["location"]["lat"]
        lon = orig_iq["weather_data"]["location"]["lon"]
        uri = orig_iq["weather_data"]["from"]

        response = self._xmpp.Iq()
        response["to"] = orig_iq["from"]
        response["id"] = orig_iq["id"]

        try:
            service = self._get_weather_service_for(
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
                    service.query_interval(request_stanza)
                    weather_data.append(request_stanza)
                elif request_stanza.name == weather_stanza.PointData.name:
                    service.query_data_point(request_stanza)
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

        iq = self._xmpp.Iq()
        iq['to'] = orig_iq['from']
        iq['type'] = 'result'
        iq['id'] = orig_iq['id']
        iq.set_payload(response)
        iq.send()

    def session_start(self, event):
        self._departure_push_destinations = set()
        self._xmpp.send_presence()

    def session_end(self, event):
        self._departure_push_destinations = set()

    def run(self):
        self._xmpp.connect()
        self._xmpp.process(block=True)
