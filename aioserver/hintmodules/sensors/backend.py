import abc
import asyncio
import logging
import pathlib
import subprocess

import hintmodules.utils


class BackendError(Exception):
    pass


class UnknownSensorError(LookupError):
    pass


class UnknownRRDToolResponse(BackendError):
    pass


class Backend(metaclass=abc.ABCMeta):
    def __init__(self, *, logger_base=None):
        super().__init__()
        if logger_base is not None:
            self.logger = logger_base.getChild("rrdtoolbackend")
        else:
            self.logger = logging.getLogger(
                ".".join([__name__, type(self).__qualname__])
            )

    @abc.abstractmethod
    async def submit_sensor_values(self, sensor_type, sensor_id, values):
        pass


class RRDToolBackend(Backend):
    def __init__(self, config, *, root_dir=None, **kwargs):
        super().__init__(**kwargs)
        if root_dir is None:
            root_dir = pathlib.Path.cwd()

        self._sensor_mapping = {
            (map_entry["sensor_type"], map_entry["sensor_id"]): (
                root_dir / map_entry["filename"],
                map_entry["ds_name"]
            )
            for map_entry in config["sensor_mapping"]
        }

        self.logger.debug("sensor_mapping=%r", self._sensor_mapping)

        self._rrd = None

    async def _require_rrd(self):
        if self._rrd is not None:
            if self._rrd.returncode is not None:
                self.logger.warning(
                    "rrdtool slave disappeared: %d",
                    self._rrd.returncode
                )
                self._rrd = None

        if self._rrd is None:
            self._rrd = await asyncio.create_subprocess_exec(
                "rrdtool",
                "-",
                stdout=subprocess.PIPE,
                stdin=subprocess.PIPE,
            )

        return self._rrd

    async def _send_command(self, cmdbytes):
        rrd = await self._require_rrd()

        self.logger.debug(
            "rrdtool << %r",
            cmdbytes.decode()
        )

        rrd.stdin.writelines([cmdbytes, b"\n"])
        await rrd.stdin.drain()

        response = (await rrd.stdout.readline()).decode("ascii")

        if response.startswith("OK"):
            return
        elif response.startswith("ERROR"):
            if "illegal attempt to update using time" in response:
                self.logger.debug("ignoring timestamp issues with rrd: %s",
                                  response)
                # we ignore that, because the user can’t do anything about it
                # anyways
                return
            raise BackendError(response[6:].strip())
        else:
            raise UnknownRRDToolResponse(response)

    async def _submit_to_ds(self, rrdfile, ds_name, data):
        args = ["update", str(rrdfile), "--template"]
        args.append(":".join([ds_name]*len(data)))
        args.append("--")

        for timestamp, value in data:
            arg = "{}:{}".format(
                ("N"
                 if timestamp is None
                 else hintmodules.utils.to_timestamp(timestamp)),
                value,
            )
            args.append(arg)

        await self._send_command(" ".join(args).encode("ascii"))

    async def submit_sensor_values(self, sensor_type, sensor_id, values):
        try:
            rrdfile, series_name = self._sensor_mapping[
                sensor_type, sensor_id
            ]
        except KeyError:
            raise UnknownSensorError((sensor_type, sensor_id)) from None

        await self._submit_to_ds(
            rrdfile,
            series_name,
            values,
        )
