#!/usr/bin/python3

if __name__ == "__main__":
    import argparse
    import sys
    import os.path
    import hintmodules
    import logging

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
        level=logging.ERROR,
        format='{0}:%(levelname)-8s %(message)s'.format(
            os.path.basename(sys.argv[0])))

    if args.verbosity >= 3:
        logging.getLogger().setLevel(logging.DEBUG)
    elif args.verbosity >= 2:
        logging.getLogger().setLevel(logging.INFO)
    elif args.verbosity >= 1:
        logging.getLogger().setLevel(logging.WARNING)

    hintbot = hintmodules.HintBot(args.config)
    hintbot.run()
