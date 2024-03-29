#!/usr/bin/python3
import asyncio
import logging
import json
import pathlib

import aioxmpp.node
import aioxmpp.security_layer
import aioxmpp.structs

import serial.serialutil
import serial_asyncio

import hintlib.core

from hintd.cconstants import (
    Address,
    ArduinoSubject,
    LPCSubject,
    LPCCommand,
)
import hintd.protocol
import hintd.departure
import hintd.weather
import hintd.covid
import hintd.shia
import hintd.button

from .ui import UI


class HintDaemon:
    def __init__(self, args, config, loop):
        super().__init__()
        self.logger = logging.getLogger("hintd")
        self._config = config
        self._loop = loop

        self._xmpp = hintlib.core.BotCore(
            config["xmpp"],
            client_logger=self.logger.getChild("client"),
        )
        pubsub_service = self._xmpp.client.summon(aioxmpp.PubSubClient)

        self._protocol = None
        self._comm = None

        self._ui = UI(self)
        self._ui.configure_clock(config["clock"])

        self._departure_service = hintd.departure.DepartureService()
        self._departure_service.configure(config["departure"])
        self._ui.add_screen(self._departure_service.screen)

        weather_screen = hintd.weather.WeatherScreen()

        self._forecast_service = hintd.weather.ForecastService(weather_screen)
        self._forecast_service.configure(config["weather"]["forecast"])

        self._weather_sensors_service = hintd.weather.SensorsService(
            weather_screen,
            pubsub_service,
        )
        self._weather_sensors_service.configure(config["weather"]["sensors"])

        self._ui.add_screen(weather_screen)

        self._covid_service = hintd.covid.CovidService()
        self._covid_service.configure(config.get("covid", {}))
        self._ui.add_screen(self._covid_service.screen)

        self._shia_service = hintd.shia.ShiaService()
        self._ui.add_screen(self._shia_service.screen)

        self._button_service = hintd.button.ButtonService(self._xmpp)
        self._button_service.configure(config.get("button", {}))
        self._ui.add_screen(self._button_service.screen)

        self._touch_down = False

    def on_arduino_message(self, payload):
        if payload.subject == ArduinoSubject.SENSOR_READOUT.value:
            self.logger.debug(
                "sensor message: sensor_id=%r, raw=%r",
                payload.data.sensor_readout.sensor_id,
                payload.data.sensor_readout.raw_readout,
            )
        else:
            self.logger.warning(
                "unknown arduino subject: %r",
                payload.subject,
            )

    def on_lpc_message(self, msg):
        subject = LPCSubject(msg.subject)
        if subject == LPCSubject.TOUCH_EVENT:
            x = msg.payload.touch_ev.x
            y = msg.payload.touch_ev.y
            z = msg.payload.touch_ev.z
            if self._touch_down and z == 0:
                self.logger.debug(
                    "touch_up: touch_is_down=%s, z=%d",
                    self._touch_down, z
                )
                self._touch_down = False
                self._ui.touch_up(x, y, z)
            elif self._touch_down and z > 0:
                self.logger.debug(
                    "touch_move: touch_is_down=%s, z=%d",
                    self._touch_down, z
                )
                self._ui.touch_move(x, y, z)
            elif not self._touch_down and z > 0:
                self.logger.debug(
                    "touch_down: touch_is_down=%s, z=%d",
                    self._touch_down, z
                )
                self._touch_down = True
                self._ui.touch_down(x, y, z)
        else:
            self.logger.warning(
                "unknown LPC message received: subject=%d",
                msg.subject
            )

    def on_message_received(self, sender, recipient, payload, ack_fn):
        self.logger.debug(
            "message received: sender=%r, recipient=%r, payload=%r",
            sender,
            recipient,
            payload,
        )

        try:
            if sender == Address.ARDUINO:
                self.on_arduino_message(payload)
            elif sender == Address.LPC1114:
                self.on_lpc_message(payload)
            else:
                self.logger.warning(
                    "no handler for sender %r",
                    sender,
                )
        except:  # NOQA
            self.logger.exception("failed to handle message")
        finally:
            ack_fn()

    def make_protocol(self):
        self._protocol = hintd.protocol.Protocol()
        self._protocol.on_message_received = self.on_message_received
        return self._protocol

    def send_message(self, recipient, payload):
        if self._protocol is not None:
            self._protocol.send_message(recipient, payload)

    def _reset_lpc(self):
        # self._protocol.send_message(
        #     Address.LPC1114,
        #     b"",
        #     reset=True,
        # )
        pass

    async def stay_connected(self):
        try:
            while True:
                comm_path = pathlib.Path(self._config["comm"]["path"])
                try:
                    if comm_path.is_socket():
                        self._comm, _ = await self._loop.create_unix_connection(
                            self.make_protocol,
                            str(comm_path),
                        )
                    else:
                        self._comm, _ = await serial_asyncio.create_serial_connection(
                            self._loop,
                            self.make_protocol,
                            str(comm_path),
                            115200,
                        )
                except (serial.serialutil.SerialException) as exc:
                    self.logger.error(
                        "failed to open serial: %s; retrying in 5s", exc
                    )
                    await asyncio.sleep(5)
                    continue

                self.logger.debug("resetting and waking display")
                self._reset_lpc()
                self.logger.debug("waking up UI")
                self._ui.wakeup()

                self.logger.debug("ready, watching connection")
                disconnected = asyncio.Future()
                self._protocol.on_disconnect.connect(
                    disconnected,
                    self._protocol.on_disconnect.AUTO_FUTURE,
                )

                await disconnected
                self.logger.debug(
                    "serial disconnected, putting UI to sleep"
                )
                self._ui.sleep()
                self.logger.warning(
                    "serial disconnected, trying to reconnect"
                )
        except asyncio.CancelledError:
            if self._comm is not None:
                if (not hasattr(self._comm, "serial") or
                        self._comm.serial is not None):
                    self._comm.abort()

    async def run(self):
        async with self._xmpp:
            try:
                await self.stay_connected()
            except:  # NOQA
                self.logger.exception("wat")
                raise
