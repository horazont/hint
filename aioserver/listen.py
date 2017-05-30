#!/usr/bin/python3
import asyncio
import functools
import hashlib
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
import hintmodules.warnings.cap as cap_xso
import hintmodules.weather.xso as weather_xso

from hintmodules.warnings.dwd import point_in_poly


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

    pubsub_svc = client.summon(aioxmpp.PubSubClient)

    live = set()

    async with aioxmpp.node.UseConnected(
            client,
            presence=aioxmpp.structs.PresenceState(False),
            timeout=timedelta(seconds=30)) as stream:

        lon = 51.0492
        lat = 13.7381

        async def print_item(jid, node, item, **kwargs):
            alert = item.registered_payload

            # print("Pubsub-ID: {}".format(item.id_))

            if alert is None:
                print("No alert payload!")
                return

            info = alert.infos[0]

            area_with_poly = info.get_area_with_polygon()
            if area_with_poly is None or not point_in_poly(
                    lon, lat, area_with_poly.polygon):
                return
            else:
                try:
                    if any(point_in_poly(lon, lat, excluded_polygon)
                           for excluded_polygon in cap_xso.PolygonType.parse(
                                   area_with_poly.geocodes["EXCLUDED_POLYGON"]
                           )):
                        return
                except KeyError:
                    pass

            live.add(item.id_)

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
            hf = hashlib.sha1()
            hf.update(str(area_with_poly.polygon).encode("utf-8"))
            print("Polygon-Hash: {}".format(
                hf.hexdigest()
            ))
            print()

        pubsub_svc.on_item_published.connect(
            print_item,
            pubsub_svc.on_item_published.SPAWN_WITH_LOOP(None)
        )

        def print_retracted(jid, node, id_, **kwargs):
            try:
                live.remove(id_)
            except KeyError:
                pass
            else:
                print("ID: {}".format(id_))
                print("Retracted: True")
                print()

        pubsub_svc.on_item_retracted.connect(
            print_retracted,
        )

        await pubsub_svc.subscribe(
            aioxmpp.structs.JID.fromstr(
                config["warnings"]["plugins"][0]["pubsub_jid"]
            ),
            config["warnings"]["plugins"][0]["pubsub_node"],
            subscription_jid=client.local_jid,
        )

        while True:
            await asyncio.sleep(1)


if __name__ == "__main__":
    import toml

    with open("config.toml") as f:
        cfg = toml.load(f)

    import logging.config
    logging.config.fileConfig("logging.ini")

    loop = asyncio.get_event_loop()
    try:
        loop.run_until_complete(main(cfg))
    finally:
        loop.close()
