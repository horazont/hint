#!/usr/bin/python3
import asyncio
import json
import textwrap

from datetime import timedelta, datetime

try:
    import readline  # NOQA
except ImportError:
    pass

import aioxmpp.node
import aioxmpp.pubsub
import aioxmpp.security_layer
import aioxmpp.stanza
import aioxmpp.structs

import hintmodules.warnings.xso as warnings_xso
import hintmodules.weather.xso as weather_xso


def get_pkfprint_store_verifier_factory(store_path):
    if store_path is not None:
        with open(store_path, "r") as f:
            data = json.load(f)
    else:
        data = None

    return data


async def main(config):
    recipient = aioxmpp.structs.JID.fromstr(config["xmpp"]["jid"])
    password = config["xmpp"]["password"]
    jid = recipient.replace(resource=recipient.resource+"-client")

    client = aioxmpp.node.PresenceManagedClient(
        jid,
        aioxmpp.make_security_layer(
            password,
            pin_store=get_pkfprint_store_verifier_factory(
                config["xmpp"].get("tls_pk_pinstore"),
            ),
            pin_type=aioxmpp.security_layer.PinType.PUBLIC_KEY,
        ),
    )

    async with aioxmpp.node.UseConnected(
            client,
            presence=aioxmpp.structs.PresenceState(False),
            timeout=timedelta(seconds=30)) as stream:

        iq = aioxmpp.stanza.IQ(
            to=recipient,
            type_="get",
        )
        #  51.0492, 13.7381 -- Dresden
        #  52.3744779, 9.7385532 -- Hannover
        iq.payload = warnings_xso.SearchAlerts()
        # iq.payload.lon = 52.3744779
        # iq.payload.lat = 9.7385532
        iq.payload.lon = 51.0492
        iq.payload.lat = 13.7381

        response = await stream.send_iq_and_wait_for_reply(iq)
        for alert in response.items:
            info = alert.infos[0]
            print("ID: {}".format(alert.identifier))
            print("Subject: {}".format(info.headline))
            print("Category: {}".format(info.category))
            print("Event: {}".format(info.event))
            if info.response_type is not None:
                print("Response-Type: {}".format(info.response_type))
            print("Urgency: {}".format(info.urgency))
            print("Severity: {}".format(info.severity))
            print("Certainty: {}".format(info.certainty))
            for key, value in info.parameters.items():
                print("Parameter-{}: {}".format(key, value))
            print("Effective: {}".format(info.effective))
            print("Onset: {}".format(info.onset))
            if info.expires is not None:
                print("Expires: {}".format(info.expires))
            for key, value in info.event_codes.items():
                print("Event-{}: {}".format(key, value))
            if info.description:
                print("Description:")
                print(
                    textwrap.indent(
                        textwrap.fill(info.description, width=76),
                        "  "
                    )
                )
            if info.instruction:
                print("Instruction:")
                print(
                    textwrap.indent(
                        textwrap.fill(info.instruction, width=76),
                        "  "
                    )
                )
            print()

        # iq = aioxmpp.stanza.IQ(
        #     to=recipient,
        #     type_="get",
        # )
        # iq.payload = weather_xso.SourcesRequest()

        # iq = aioxmpp.stanza.IQ(
        #     to=recipient,
        #     type_="get",
        # )
        # iq.payload = weather_xso.WeatherRequest()

        # location = weather_xso.Location()
        # location.lon = 51.0492
        # location.lat = 13.7381
        # location.source = weather_xso.Source("api.forecast.io")

        # interval = weather_xso.Interval()
        # interval.start = (
        #     datetime.utcnow().replace(minute=0, second=0, microsecond=0) +
        #     timedelta(hours=1)
        # )
        # interval.end = interval.start + timedelta(hours=3)

        # location.intervals.append(interval)

        # iq.payload.locations.append(location)

        # response = await stream.send_iq_and_wait_for_reply(iq)

        # print()
        # for location in response.locations:
        #     print(location.lat, location.lon)
        #     print(location.source)

        #     for interval in location.intervals:
        #         print(interval)

        #     print()

if __name__ == "__main__":
    import toml

    with open("config.toml") as f:
        cfg = toml.load(f)

    import logging.config
    logging.config.fileConfig("logging.ini")

    asyncio.get_event_loop().run_until_complete(main(cfg))
