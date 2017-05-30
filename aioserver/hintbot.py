#!/usr/bin/python3
import argparse
import asyncio
import logging
import logging.config
import signal

import toml

parser = argparse.ArgumentParser()
parser.add_argument(
    "-c", "--config",
    help="Configuration file (currently required)",
    required=True
)

args = parser.parse_args()
with open(args.config, "r") as f:
    config = toml.load(f)

logging.basicConfig(
    level=logging.INFO
)

file_config = config.get("logging", {}).get("file_config")
if file_config is not None:
    logging.config.fileConfig(file_config)

import hintmodules.main

d = hintmodules.main.HintBot(args, config)
loop = asyncio.get_event_loop()

task = asyncio.async(d.run())
loop.add_signal_handler(signal.SIGINT, task.cancel)
loop.add_signal_handler(signal.SIGTERM, task.cancel)

loop.run_until_complete(task)
