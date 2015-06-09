from sleekxmpp import Iq, Message
from sleekxmpp.xmlstream import register_stanza_plugin, ElementBase, ET, JID

xmlns = "https://xmlns.zombofant.net/xmpp/public-transport"

class Departure(ElementBase):
    namespace = xmlns
    name = "departure"
    plugin_attrib = name
    interfaces = set()

class Data(ElementBase):
    namespace = xmlns
    name = "data"
    plugin_attrib = name
    interfaces = set()

class DepartureTime(ElementBase):
    namespace = xmlns
    name = "dt"
    plugin_attrib = name
    interfaces = set(("eta", "destination", "lane", "ts"))

    def get_destination(self):
        return self._get_attr("d")

    def get_eta(self):
        return float(self._get_attr("e"))

    def get_lane(self):
        return self._get_attr("l")

    def get_timestamp(self):
        return float(self._get_attr("ts"))

    def set_destination(self, value):
        self._set_attr("d", value)

    def set_eta(self, value):
        self._set_attr("e", "{:d}".format(value))

    def set_lane(self, value):
        self._set_attr("l", value)

    def set_timestamp(self, value):
        self._set_attr("ts", "{:d}".format(value))


register_stanza_plugin(Iq, Departure)
register_stanza_plugin(Departure, Data)
register_stanza_plugin(Data, DepartureTime, iterable=True)
