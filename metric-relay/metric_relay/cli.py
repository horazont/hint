import argparse
import asyncio
import logging
import sys

import toml

import metric_relay.config
import metric_relay.daemon


async def amain(config_dict, logger_base):
    config = metric_relay.config.compile_config(config_dict)
    config.logging.apply_()

    transports = {}
    for name, transport_cfg in config.transports.items():
        transport_logger = logger_base.getChild("transport").getChild(name)
        transports[name] = transport_cfg.instantiate(
            logger=transport_logger,
        )

    sources = {}
    for name, source_cfg in config.sources.items():
        source_logger = logger_base.getChild("source").getChild(name)
        sources[name] = source_cfg.instantiate(
            logger=source_logger,
            transports=transports,
        )

    sinks = {}
    for name, sink_cfg in config.sinks.items():
        sink_logger = logger_base.getChild("sink").getChild(name)
        sinks[name] = sink_cfg.instantiate(
            logger=sink_logger,
            transports=transports,
        )

    routes = []
    for route in config.batch_routes:
        routes.append(
            metric_relay.daemon.Route(
                from_=sources[route.from_],
                to=sinks[route.to],
                persistent=route.persistent,
            )
        )

    daemon = metric_relay.daemon.MetricRelay(
        logger=logging.getLogger("metric_relay").getChild("daemon"),
        transports=list(transports.values()),
        sources=list(sources.values()),
        sinks=list(sinks.values()),
        routes=routes,
    )
    await daemon.run()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "-c", "--config",
        type=argparse.FileType("r"),
        required=True,
    )

    args = parser.parse_args()

    with args.config as f:
        config_dict = toml.load(f)

    logger_base = logging.getLogger("metric_relay")

    return asyncio.run(amain(
        config_dict,
        logger_base,
    ))


def main_wrap():
    sys.exit(main() or 0)
