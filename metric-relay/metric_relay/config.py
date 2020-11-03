import dataclasses
import enum
import importlib
import logging
import logging.config
import typing

import toml

import schema

import metric_relay.interface
import metric_relay.queue


class RouteFilterPolicy(enum.Enum):
    DROP = 'drop'
    ACCEPT = 'accept'


BASE_SCHEMA = schema.Schema({
    schema.Optional("logging", default={}): {
        schema.Optional("config", default=None): schema.Or(
            str,  # to load a config file
            dict,  # to load a logging dictConfig
        ),
        schema.Optional("verbosity", default=None): schema.Or(
            int,
            schema.Or("ERROR", "WARNING", "INFO", "DEBUG"),
        ),
    },
    "transports": {
        str: {
            "class": str,
            schema.Optional(str): object,
        },
    },
    "sources": {
        str: {
            "class": str,
            "transport": str,
            schema.Optional(str): object,
        },
    },
    "sinks": {
        str: {
            "class": str,
            "transport": str,
            schema.Optional(str): object,
        },
    },
    "batch_routes": [{
        "from": str,
        "to": str,
        schema.Optional("queue"): {
            "max_depth": int,
            schema.Optional(
                "overflow",
                default=metric_relay.queue.OverflowPolicy.REJECT,
            ): lambda x: metric_relay.queue.OverflowPolicy,
            "persistent": bool,
        },
        schema.Optional("filter"): {
            "policy": RouteFilterPolicy,
            schema.Optional("rules"): [{
                "type": str,
                str: object,
            }]
        },
        schema.Optional("persistent", default=False): bool,
    }],
    "stream_routes": [{
        "from": str,
        "to": str,
        schema.Optional("filter"): {
            "policy": schema.Or("drop", "accept"),
            schema.Optional("rules"): [{
                "type": str,
                str: object,
            }]
        },
        schema.Optional("persistent", default=False): bool,
    }]
})


@dataclasses.dataclass
class LoggingConfig:
    config: typing.Optional[dict]
    verbosity: typing.Optional[int]

    @classmethod
    def from_dict(self, d: typing.Mapping):
        if d["config"] is not None:
            if (d["verbosity"] is not None and
                    not d["config"].get("incremental")):
                raise ValueError(
                    "both logging.verbosity and logging.config are set, "
                    "but logging.config.incremental is not true -- "
                    "this is a conflict and not supported"
                )

            if isinstance(d["config"], str):
                with open(d["config"], "r") as f:
                    cfg = toml.load(f)
                return LoggingConfig(config=cfg, verbosity=None)
            else:
                return LoggingConfig(config=d["config"],
                                     verbosity=None)

        return LoggingConfig(config=None, verbosity=d["verbosity"])

    def apply_(self):
        logging.basicConfig(
            format='%(asctime)s %(levelname)-7s [%(name)s]   %(message)s',
            level={
                0: logging.ERROR,
                1: logging.WARNING,
                2: logging.INFO,
            }.get(self.verbosity or 0, logging.DEBUG)
        )
        if self.config is not None:
            logging.config.dictConfig(self.config)


@dataclasses.dataclass
class TransportConfig:
    class_: type
    extra_config: object

    def instantiate(self, *, logger, **kwargs):
        return self.class_(
            config=self.extra_config,
            logger=logger,
            **kwargs
        )


@dataclasses.dataclass
class SourceConfig:
    class_: type
    transport: str
    extra_config: object

    def instantiate(
            self,
            *,
            logger,
            transports,
            **kwargs):
        return self.class_(
            config=self.extra_config,
            transport=transports[self.transport],
            logger=logger,
            **kwargs
        )


@dataclasses.dataclass
class SinkConfig:
    class_: type
    transport: str
    extra_config: object

    def instantiate(
            self,
            *,
            logger,
            transports,
            **kwargs):
        return self.class_(
            config=self.extra_config,
            transport=transports[self.transport],
            logger=logger,
            **kwargs
        )


@dataclasses.dataclass
class RouteFilterRule:
    type_: type
    extra_config: object


@dataclasses.dataclass
class RouteFilterConfig:
    policy: RouteFilterPolicy
    rules: typing.List[RouteFilterRule]


@dataclasses.dataclass
class RouteConfig:
    from_: str
    to: str
    persistent: bool
    filter_: RouteFilterConfig


@dataclasses.dataclass
class Config:
    logging: LoggingConfig
    transports: typing.Mapping[str, TransportConfig]
    sources: typing.Mapping[str, SourceConfig]
    sinks: typing.Mapping[str, SinkConfig]
    batch_routes: typing.List[RouteConfig]
    stream_routes: typing.List[RouteConfig]


class ClassLookupError(LookupError):
    pass


class ConfigError(ValueError):
    pass


def find_class(fqcn: str, required_baseclass: type) -> type:
    # TODO: something with entrypoints :>
    if not fqcn.startswith("metric_relay"):
        raise RuntimeError(f"class name outside package: {fqcn!r}")

    module_path, class_name = fqcn.rsplit(".", 1)
    try:
        module = importlib.import_module(module_path)
    except ImportError as exc:
        raise ClassLookupError(
            f"failed to import module {module_path!r} for {fqcn!r}: {exc}"
        ) from exc

    try:
        class_ = getattr(module, class_name)
    except AttributeError as exc:
        raise ClassLookupError(
            f"module {module_path!r} for {fqcn!r} has no attribute "
            f"{class_name!r}"
        ) from exc

    if not issubclass(class_, required_baseclass):
        raise ClassLookupError(
            f"{fqcn!r} is not a valid {required_baseclass}"
        )

    return class_


def _compile_transports(
        transports_cfg: typing.Mapping
        ) -> typing.Mapping[str, TransportConfig]:
    transports = {}

    for name, cfg in transports_cfg.items():
        try:
            class_ = find_class(cfg["class"],
                                metric_relay.interface.Transport)
        except ClassLookupError as exc:
            raise ConfigError(
                f"transport {name!r} specifies invalid class: {exc}"
            ) from exc

        transport_schema = class_.get_config_schema()
        extra_cfg = dict(cfg)
        del extra_cfg["class"]

        try:
            extra_cfg = transport_schema.validate(extra_cfg)
            compiled_extra_cfg = class_.compile_config(extra_cfg)
        except (ConfigError, RuntimeError, schema.SchemaError) as exc:
            raise ConfigError(
                f"transport {name!r} of type {class_} has invalid "
                f"configuration: {exc}"
            )

        transports[name] = TransportConfig(
            class_=class_,
            extra_config=compiled_extra_cfg,
        )

    return transports


def _compile_sinks(
        sinks_cfg: typing.Mapping,
        transports: typing.Mapping[str, TransportConfig],
        ) -> typing.Mapping[str, SinkConfig]:
    sinks = {}

    for name, cfg in sinks_cfg.items():
        try:
            class_ = find_class(
                cfg["class"],
                metric_relay.interface.Sink
            )
        except ClassLookupError as exc:
            raise ConfigError(
                f"sink {name!r} specifies invalid class: {exc}"
            ) from exc

        sink_schema = class_.get_config_schema()
        extra_cfg = dict(cfg)
        del extra_cfg["class"]
        del extra_cfg["transport"]

        try:
            extra_cfg = sink_schema.validate(extra_cfg)
            compiled_extra_cfg = class_.compile_config(extra_cfg)
        except (ConfigError, RuntimeError, schema.SchemaError) as exc:
            raise ConfigError(
                f"sink {name!r} of type {class_} has invalid "
                f"configuration: {exc}"
            )

        transport_name = cfg["transport"]
        try:
            transport = transports[transport_name].class_
        except KeyError as exc:
            raise ConfigError(
                f"sink {name!r} references undeclared transport "
                f"{transport_name}"
            )

        if not class_.supports_transport(transport, compiled_extra_cfg):
            raise ConfigError(
                f"sink {name!r} can not be used with transport "
                f"{transport_name} of type {transport}"
            )

        sinks[name] = SinkConfig(
            class_=class_,
            transport=cfg["transport"],
            extra_config=compiled_extra_cfg,
        )

    return sinks


def _compile_sources(
        sources_cfg: typing.Mapping,
        transports: typing.Mapping[str, TransportConfig],
        ) -> typing.Mapping[str, SinkConfig]:
    sources = {}

    for name, cfg in sources_cfg.items():
        try:
            class_ = find_class(
                cfg["class"],
                metric_relay.interface.Source
            )
        except ClassLookupError as exc:
            raise ConfigError(
                f"source {name!r} specifies invalid class: {exc}"
            ) from exc

        source_schema = class_.get_config_schema()
        extra_cfg = dict(cfg)
        del extra_cfg["class"]
        del extra_cfg["transport"]

        try:
            extra_cfg = source_schema.validate(extra_cfg)
            compiled_extra_cfg = class_.compile_config(extra_cfg)
        except (ConfigError, RuntimeError, schema.SchemaError) as exc:
            raise ConfigError(
                f"source {name!r} of type {class_} has invalid "
                f"configuration: {exc}"
            )

        transport_name = cfg["transport"]
        try:
            transport = transports[transport_name].class_
        except KeyError as exc:
            raise ConfigError(
                f"source {name!r} references undeclared transport "
                f"{transport_name}"
            )

        if not class_.supports_transport(transport, compiled_extra_cfg):
            raise ConfigError(
                f"source {name!r} can not be used with transport "
                f"{transport_name} of type {transport}"
            )

        sources[name] = SourceConfig(
            class_=class_,
            transport=cfg["transport"],
            extra_config=compiled_extra_cfg,
        )

    return sources


def _compile_routes(
        routes_cfg: typing.Mapping,
        sources: typing.Mapping[str, SourceConfig],
        sinks: typing.Mapping[str, SinkConfig],
        data_class: metric_relay.interface.DataClass,
        ) -> typing.List[RouteConfig]:
    routes = []

    for cfg in routes_cfg:
        source_name = cfg["from"]
        sink_name = cfg["to"]

        try:
            source_cfg = sources[source_name]
        except KeyError:
            raise ConfigError(
                f"route from {source_name!r} to {sink_name!r} references "
                f"non-existent source"
            )

        try:
            sink_cfg = sinks[sink_name]
        except KeyError:
            raise ConfigError(
                f"route from {source_name!r} to {sink_name!r} references "
                f"non-existent sink"
            )

        if not sink_cfg.class_.accepts(data_class):
            raise ConfigError(
                f"route to {sink_name!r} transporting {data_class} is "
                f"invalid: the sink {sink_cfg.class_} does not accept "
                f"{data_class}"
            )

        if not source_cfg.class_.emits(data_class):
            raise ConfigError(
                f"route from {source_name!r} transporting {data_class} is "
                f"invalid: the source {sink_cfg.class_} does not emit "
                f"{data_class}"
            )

        routes.append(
            RouteConfig(
                from_=source_name,
                to=sink_name,
                persistent=cfg["persistent"],
                filter_=RouteFilterConfig(
                    policy=RouteFilterPolicy.ACCEPT,
                    rules=[],
                )
            )
        )

    return routes


def compile_config(root_cfg: typing.Mapping):
    try:
        root_cfg = BASE_SCHEMA.validate(root_cfg)
    except schema.SchemaError as exc:
        raise ConfigError(
            f"failed to validate configuration: {exc}"
        ) from exc

    logging_config = LoggingConfig.from_dict(root_cfg.get("logging", {}))
    transports = _compile_transports(root_cfg["transports"])
    sinks = _compile_sinks(root_cfg["sinks"], transports)
    sources = _compile_sources(root_cfg["sources"], transports)
    batch_routes = _compile_routes(
        root_cfg["batch_routes"],
        sources,
        sinks,
        metric_relay.interface.DataClass.SAMPLE_BATCH,
    )
    stream_routes = _compile_routes(
        root_cfg["stream_routes"],
        sources,
        sinks,
        metric_relay.interface.DataClass.STREAM,
    )

    return Config(
        logging=logging_config,
        transports=transports,
        sinks=sinks,
        sources=sources,
        batch_routes=batch_routes,
        stream_routes=stream_routes,
    )
