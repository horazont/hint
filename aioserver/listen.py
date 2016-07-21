#!/usr/bin/python3
import asyncio
import functools
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
        data = {}

    store = aioxmpp.security_layer.PublicKeyPinStore()
    store.import_from_json(data)
    del data

    @asyncio.coroutine
    def fail_always(*args, **kwargs):
        return False

    def verifier_factory():
        return aioxmpp.security_layer.PinningPKIXCertificateVerifier(
            store.query,
            fail_always
        )

    return verifier_factory


async def main(config):
    recipient = aioxmpp.structs.JID.fromstr(config.get("xmpp", "jid"))
    password = config.get("xmpp", "password")
    jid = recipient.replace(resource=recipient.resource+"-client")

    @asyncio.coroutine
    def get_password(client_jid, nattempt):
        if nattempt > 1:
            return None
        return password

    client = aioxmpp.node.PresenceManagedClient(
        jid,
        aioxmpp.security_layer.security_layer(
            aioxmpp.security_layer.STARTTLSProvider(
                aioxmpp.security_layer.default_ssl_context,
                get_pkfprint_store_verifier_factory(
                    config.get("xmpp", "tls_pk_pinstore", fallback=None),
                ),
            ),
            [
                aioxmpp.security_layer.PasswordSASLProvider(
                    get_password
                )
            ]
        ),
    )

    pubsub_svc = client.summon(aioxmpp.pubsub.Service)

    async with aioxmpp.node.UseConnected(
            client,
            presence=aioxmpp.structs.PresenceState(False),
            timeout=timedelta(seconds=30)) as stream:

        iq = aioxmpp.stanza.IQ(
            to=recipient,
            type_="get",
        )
        iq.payload = warnings_xso.LookupGeoCoord()
        iq.payload.lon = 51.0492
        iq.payload.lat = 13.7381

        async def print_item(jid, node, item, **kwargs):
            warning = item.registered_payload
            if warning is None:
                warning = (await pubsub_svc.get_items_by_id(
                    jid, node, [item.id_]
                )).payload.items[0].registered_payload

            print("ID: {}".format(item.id_))
            print("Subject: {}".format(warning.headline))
            print("Event: {}".format(warning.event))
            print("Is-Preliminary: {}".format(
                warning.is_preliminary
            ))
            print("Type: {}".format(warning.type_))
            print("Level: {}".format(warning.level))
            print("Start: {}".format(warning.start))
            print("End: {}".format(warning.end))
            if warning.altitude_start is not None:
                print("Altitude-Start: {}".format(warning.altitude_start))
            if warning.altitude_end is not None:
                print("Altitude-End: {}".format(warning.altitude_end))
            if warning.description:
                print("Description:")
                print(
                    textwrap.indent(
                        textwrap.fill(warning.description, width=76),
                        "  "
                    )
                )
            if warning.instruction:
                print("Instruction:")
                print(
                    textwrap.indent(
                        textwrap.fill(warning.instruction, width=76),
                        "  "
                    )
                )
            print()

        pubsub_svc.on_item_published.connect(
            print_item,
            pubsub_svc.on_item_published.SPAWN_WITH_LOOP(None)
        )

        def print_retracted(jid, node, id_, **kwargs):
            print("ID: {}".format(id_))
            print("Retracted: True")
            print()

        pubsub_svc.on_item_retracted.connect(
            print_retracted,
        )

        response = await stream.send_iq_and_wait_for_reply(iq)
        for item in response.items:
            await pubsub_svc.subscribe(
                item.jid,
                item.node,
                subscription_jid=client.local_jid,
            )

        while True:
            await asyncio.sleep(1)


if __name__ == "__main__":
    import configparser

    cfg = configparser.ConfigParser(delimiters=("=",))
    cfg.read("config.ini")

    import logging.config
    logging.config.fileConfig("logging.ini")

    asyncio.get_event_loop().run_until_complete(main(cfg))
