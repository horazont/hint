import argparse
import logging
import logging.config
import toml

parser = argparse.ArgumentParser()
parser.add_argument(
    "-c", "--config",
    help="Configuration file (currently required)",
    required=True,
    type=argparse.FileType("r"),
)

args = parser.parse_args()

with args.config as f:
    config = toml.load(f)

logging.basicConfig(
    level=logging.INFO
)

file_config = config.get("logging", {}).get("file_config")
if file_config is not None:
    logging.config.fileConfig(file_config)


import asyncio
import signal

import hintd

loop = asyncio.get_event_loop()
d = hintd.HintDaemon(args, config, loop)

task = asyncio.async(d.run())
loop.add_signal_handler(signal.SIGINT, task.cancel)
loop.add_signal_handler(signal.SIGTERM, task.cancel)

try:
    loop.run_until_complete(task)
finally:
    loop.close()
