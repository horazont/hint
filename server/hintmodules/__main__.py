import argparse
import logging
import os.path
import sys

parser = argparse.ArgumentParser()
parser.add_argument(
    "-c", "--config",
    metavar="FILE",
    default="hintbot_config.py",
    help="Configuration module (python) to load")
parser.add_argument(
    "-v",
    action="count",
    default=0,
    help="Increase verbosity",
    dest="verbosity")

args = parser.parse_args()

logging.basicConfig(
    level={
        0: logging.ERROR,
        1: logging.WARNING,
        2: logging.INFO
    }.get(args.verbosity, logging.DEBUG),
    format='{0}:%(levelname)-8s %(message)s'.format("hintbot")
)

import hintmodules

hintbot = hintmodules.HintBot(args.config)
hintbot.run()
