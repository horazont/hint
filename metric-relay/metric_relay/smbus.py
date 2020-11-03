import asyncio
import functools
import itertools
import numbers
import time
import typing

from datetime import datetime

import smbus

import schema

import hintlib.bme280
import hintlib.sample

from . import interface


def byte_range(x: int):
    if not (0 <= x < 255):
        raise ValueError("must be in 0..255")
    return x


byte = schema.And(int, byte_range)


class Transport(interface.Transport[smbus.SMBus]):
    def __init__(self, *, config: smbus.SMBus, **kwargs):
        super().__init__(config=config, **kwargs)
        self._bus = config
        self._executor = None

    @classmethod
    def get_config_schema(cls) -> schema.Schema:
        return schema.Schema({
            "bus": int,
        })

    @classmethod
    def compile_config(cls, cfg) -> smbus.SMBus:
        bus_index = cfg["bus"]
        try:
            return smbus.SMBus(bus_index)
        except OSError as exc:
            raise ValueError(
                f"failed to open I2C bus {bus_index:d}: {exc}"
            ) from exc

    async def write_byte(self, device: int, register: int, data: int):
        loop = asyncio.get_event_loop()
        self.logger.debug("write: 0x%02x 0x%02x <- 0x%02x",
                          device, register, data)
        await loop.run_in_executor(
            self._executor,
            functools.partial(
                self._bus.write_byte_data,
                device,
                register,
                data,
            )
        )

    async def read_byte(self, device: int, register: int):
        loop = asyncio.get_event_loop()
        self.logger.debug("reading: 0x%02x 0x%02x", device, register)
        result = await loop.run_in_executor(
            self._executor,
            functools.partial(
                self._bus.read_byte_data,
                device,
                register,
            )
        )
        self.logger.debug("read: 0x%02x 0x%02x -> 0x%02x",
                          device, register, result)
        return result

    async def read_range(self, device: int, register_start: int, nbytes: int):
        loop = asyncio.get_event_loop()
        self.logger.debug("reading: 0x%02x 0x%02x [%d]",
                          device, register_start, nbytes)
        result = bytes(await loop.run_in_executor(
            self._executor,
            functools.partial(
                self._bus.read_i2c_block_data,
                device,
                register_start,
                nbytes,
            )
        ))
        self.logger.debug("read: 0x%02x 0x%02x [%d] -> %r",
                          device, register_start, nbytes, result)
        return result

    async def verified_write(self, device: int, register: int, data: int):
        await self.write_byte(device, register, data)
        readback = await self.read_byte(device, register)
        if data != readback:
            raise ValueError(
                f"readback {readback:02x} does not match data {data:02x}"
            )
        self.logger.debug("verified write to 0x%02x 0x%02x completed",
                          device, register)


class BME280Config(typing.NamedTuple):
    address: int
    module: str
    interval: numbers.Real

    cfg_reg_value: int
    ctrl_hum_reg_value: int
    ctrl_meas_reg_value: int

    reconfigure_interval: int
    instance: typing.Optional[str] = None


class BME280(interface.Source[BME280Config]):
    REG_ID = 0xd0
    REG_CONFIG = 0xf5
    REG_CTRL_HUM = 0xf2
    REG_CTRL_MEAS = 0xf4
    REG_DATA_START = 0xf7

    ID = 0x60

    DIG88_SIZE = 26
    DIGE1_SIZE = 7
    READOUT_SIZE = 8

    def __init__(self, *, config: BME280Config, **kwargs):
        super().__init__(config=config, **kwargs)
        self._address = config.address
        self._bare_sensor_path = hintlib.sample.SensorPath(
            module=config.module,
            part=hintlib.sample.Part.BME280.value,
            instance=config.instance,
        )
        self._interval = config.interval
        self._cfg = config.cfg_reg_value
        self._ctrl_hum = config.ctrl_hum_reg_value
        self._ctrl_meas = config.ctrl_meas_reg_value
        self._reconfigure_interval = config.reconfigure_interval

    @classmethod
    def get_config_schema(cls) -> schema.Schema:
        return schema.Schema({
            "address": byte,
            "module": str,
            schema.Optional("instance", default=None): str,
            "interval": numbers.Real,
            schema.Optional("reconfigure_interval", default=100): int,
        })

    @classmethod
    def compile_config(cls, cfg) -> BME280Config:
        return BME280Config(
            cfg_reg_value=(
                0b0 |  # no SPI 3w mode
                (0b001 << 2) |  # filter coefficient 2
                (0b101 << 5)  # 1000 ms standby time
            ),
            ctrl_hum_reg_value=(
                0b010  # oversample humidity x2
            ),
            ctrl_meas_reg_value=(
                0b11 |  # normal mode
                (0b101 << 2) |  # oversample pressure x16
                (0b010 << 5)  # oversample temperature x2
            ),
            **cfg
        )

    @classmethod
    def emits(cls, dataclass: interface.DataClass) -> bool:
        return dataclass == interface.DataClass.SAMPLE_BATCH

    @classmethod
    def supports_transport(cls, transport, cfg: BME280Config) -> bool:
        return issubclass(transport, Transport)

    async def detect(self):
        id_ = await self.transport.read_byte(self._address, self.REG_ID)
        if id_ != self.ID:
            raise ValueError(f"device responded with incorrect id {id_:02x}")

    async def configure(self):
        self.logger.debug("detecting presence")
        await self.detect()
        self.logger.debug("configuration: %02x %02x %02x",
                          self._cfg, self._ctrl_meas, self._ctrl_hum)
        await self.transport.verified_write(
            self._address,
            self.REG_CONFIG,
            self._cfg,
        )
        await self.transport.verified_write(
            self._address,
            self.REG_CTRL_MEAS,
            self._ctrl_meas,
        )
        await self.transport.verified_write(
            self._address,
            self.REG_CTRL_HUM,
            self._ctrl_hum,
        )
        self.logger.debug("configuration complete")

    async def read_raw_calibration(self):
        dig88 = await self.transport.read_range(
            self._address,
            0x88, self.DIG88_SIZE,
        )
        dige1 = await self.transport.read_range(
            self._address,
            0xe1, self.DIGE1_SIZE,
        )
        return dig88, dige1

    async def read_raw_values(self):
        return await self.transport.read_range(
            self._address,
            self.REG_DATA_START,
            self.READOUT_SIZE,
        )

    def apply_compensations(self, calibration, raw_values):
        temp_raw, pressure_raw, humidity_raw = hintlib.bme280.get_readout(
            raw_values,
        )
        T = hintlib.bme280.compensate_temperature(
            calibration,
            temp_raw,
        )
        P = hintlib.bme280.compensate_pressure(
            calibration,
            pressure_raw,
            T,
        )
        hum = hintlib.bme280.compensate_humidity(
            calibration,
            humidity_raw,
            T,
        )
        return T, P, hum

    def _pack_sample(self, timestamp, T, P, hum):
        return hintlib.sample.SampleBatch(
            timestamp=timestamp,
            bare_path=self._bare_sensor_path,
            samples={
                hintlib.sample.BME280Subpart.HUMIDITY.value: hum,
                hintlib.sample.BME280Subpart.PRESSURE.value: P,
                hintlib.sample.BME280Subpart.TEMPERATURE.value: T,
            },
        )

    async def run(self):
        tnext = time.monotonic()
        while True:
            await self.configure()
            calibration = hintlib.bme280.get_calibration(
                *(await self.read_raw_calibration())
            )
            self.logger.debug("extracted calibration values: %r",
                              calibration)
            self.logger.info("BME280 at address 0x%02x configured and "
                             "calibrated successfully",
                             self._address)
            for i in range(self._reconfigure_interval):
                tnow = time.monotonic()
                if tnow < tnext:
                    self.logger.debug("waiting for %.4fs until next sample",
                                      tnext - tnow)
                    await asyncio.sleep(tnext - tnow)

                tnext += self._interval
                ts = datetime.utcnow()
                raw_values = await self.read_raw_values()
                self.logger.debug("raw values: %r", raw_values)
                T, P, hum = self.apply_compensations(calibration, raw_values)
                self.logger.debug("cooked values: %r %r %r",
                                  T, P, hum)
                sample = self._pack_sample(ts, T, P, hum)
                await self._emit(
                    interface.DataChunk.from_sample_batch(sample)
                )

            self.logger.debug(
                "reconfiguration interval passed, reconfiguring"
            )
