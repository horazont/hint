import asyncio

import importlib

import aioxmpp.service


class HintService(aioxmpp.service.Service):
    def __init__(self, client, **kwargs):
        super().__init__(client, **kwargs)
        self._plugins = {}

        self.ratelimit = None
        self.http_session_factory = None

    def load_plugin(self, section):
        path = section.get("plugin")
        module_name, class_ = path.rsplit(".", 1)
        try:
            module = importlib.import_module(module_name)
        except ImportError:
            self.logger.error("failed to import plugin module %r",
                              module_name,
                              exc_info=True)
            raise ValueError("invalid plugin class: {!r}".format(path))

        try:
            class_ = getattr(module, class_)
        except AttributeError:
            self.logger.error(
                "failed to find class %r in plugin module %r",
                class_,
                module_name,
                exc_info=True,
            )
            raise ValueError("invalid plugin class: {!r}".format(path))

        id_ = section.name.split(":", 1)[1]

        instance = class_(self, section)
        self._plugins[id_] = instance

        return instance

    async def _shutdown(self):
        try:
            await asyncio.gather(
                *(
                    plugin.shutdown()
                    for plugin in self._plugins.values()
                )
            )
        finally:
            self._plugins.clear()
