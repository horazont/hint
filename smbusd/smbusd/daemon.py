import asyncio
import functools
import logging

import smbus

import hintlib.bme280


BME280_ADDRESS = 0x77
BME280_REG_ID = 0xd0
BME280_REG_CONFIG = 0xf5
BEM280_REG_CTRL_HUM = 0xf2
BME280_REG_CTRL_MEAS = 0xf4
BME280_REG_DATA_START = 0xf7
BME280_ID = 0x60

BME280_DIG88_SIZE = 26
BME280_DIGE1_SIZE = 7
BME280_READOUT_SIZE = 8

BME280_CFG = (
    0b0 |  # no SPI 3w mode
    (0b001 << 2) |  # filter coefficient 2
    (0b101 << 5)  # 1000 ms standby time
    )

BME280_CTRL_HUM = (
    0b010  # oversample humidity x2
    )

BME280_CTRL_MEAS = (
    0b11 |  # normal mode
    (0b101 << 2) |  # oversample pressure x16
    (0b010 << 5)  # oversample temperature x2
    )


class BME280Client:
    def __init__(self, bus: int, device_addr: int):
        super().__init__()
        self._bus = smbus.SMBus(bus)
        self._executor = None
        self._device_addr = device_addr

    async def _write_byte(self, register: int, data: int):
        loop = asyncio.get_event_loop()
        await loop.run_in_executor(
            self._executor,
            functools.partial(
                self._bus.write_byte_data,
                self._device_addr,
                register,
                data,
            )
        )

    async def _read_byte(self, register: int):
        loop = asyncio.get_event_loop()
        return await loop.run_in_executor(
            self._executor,
            functools.partial(
                self._bus.read_byte_data,
                self._device_addr,
                register,
            )
        )

    async def _read_range(self, register_start: int, nbytes: int):
        loop = asyncio.get_event_loop()
        return bytes(await loop.run_in_executor(
            self._executor,
            functools.partial(
                self._bus.read_i2c_block_data,
                self._device_addr,
                register_start,
                nbytes,
            )
        ))

    async def _verified_write(self, register: int, data: int):
        await self._write_byte(register, data)
        readback = await self._read_byte(register)
        if data != readback:
            raise ValueError(
                f"readback {readback:02x} does not match data {data:02x}"
            )

    async def detect(self):
        id_ = await self._read_byte(BME280_REG_ID)
        if id_ != BME280_ID:
            raise ValueError(f"device responded with incorrect id {id_:02x}")

    async def configure(self):
        await self.detect()
        await self._verified_write(BME280_REG_CONFIG, BME280_CFG)
        await self._verified_write(BME280_REG_CTRL_MEAS, BME280_CTRL_MEAS)
        await self._verified_write(BEM280_REG_CTRL_HUM, BME280_CTRL_HUM)

    async def read_calibration(self):
        dig88 = await self._read_range(0x88, BME280_DIG88_SIZE)
        dige1 = await self._read_range(0xe1, BME280_DIGE1_SIZE)
        return dig88, dige1

    async def read_raw_values(self):
        await self._read_range(BME280_REG_DATA_START, BME280_READOUT_SIZE)


class SMBusd:
    def __init__(self):
        super().__init__()
        self.logger = logging.getLogger(__name__)
        self.bme280 = BME280Client(
            1, 0x77,
        )

    async def run(self):
        await self.bme280.configure()
        dig88, dige1 = await self.bme280.read_calibration()
        calibration_values = hintlib.bme280.get_calibration(dig88, dige1)
        while True:
            raw_values = await self.bme280.read_raw_values()
            temp_raw, pressure_raw, humidity_raw = hintlib.bme280.get_readout(
                raw_values,
            )
            T = hintlib.bme280.compensate_temperature(
                calibration_values,
                temp_raw,
            )
            P = hintlib.bme280.compensate_pressure(
                calibration_values,
                pressure_raw,
                T,
            )
            hum = hintlib.bme280.compensate_humidity(
                calibration_values,
                humidity_raw,
                T,
            )
            print(f"T = {T:5.2f}°C  P = {P:.4f}  hum = {hum:.2f}rH")
            await asyncio.sleep(2)
