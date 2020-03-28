import argparse
import logging
import logging.config
import toml
import sys

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

sys.path[:0] = config.get("python", {}).get("add_to_path", [])

logging.basicConfig(
    level=logging.INFO
)

file_config = config.get("logging", {}).get("file_config")
if file_config is not None:
    logging.config.fileConfig(file_config)


import asyncio
import signal

import hintd.daemon

loop = asyncio.get_event_loop()
d = hintd.daemon.HintDaemon(args, config, loop)

task = asyncio.ensure_future(d.run())
loop.add_signal_handler(signal.SIGINT, task.cancel)
loop.add_signal_handler(signal.SIGTERM, task.cancel)

try:
    loop.run_until_complete(task)
finally:
    loop.close()
