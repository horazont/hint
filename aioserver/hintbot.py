#!/usr/bin/python3
import argparse
import configparser
import logging
import logging.config

parser = argparse.ArgumentParser()
parser.add_argument(
    "-c", "--config",
    help="Configuration file (currently required)",
    required=True
)

args = parser.parse_args()
config = configparser.ConfigParser(delimiters=("=", ))
config.read(args.config)

logging.basicConfig(
    level=logging.INFO
)

file_config = config.get("logging", "file_config", fallback=None)
if file_config is not None:
    logging.config.fileConfig(file_config)


import asyncio
import signal

import hintmodules.main

d = hintmodules.main.HintBot(args, config)
loop = asyncio.get_event_loop()

task = asyncio.async(d.run())
loop.add_signal_handler(signal.SIGINT, task.cancel)
loop.add_signal_handler(signal.SIGTERM, task.cancel)

loop.run_until_complete(task)
