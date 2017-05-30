import asyncio

import aioxmpp.service

import hintmodules.utils


class HintService(aioxmpp.service.Service):
    def __init__(self, client, **kwargs):
        super().__init__(client, **kwargs)
        self._plugins = {}

        self.ratelimit = None
        self.http_session_factory = None

    async def configure(self, config):
        for plugin_def in config.get("plugins", []):
            self.load_plugin(plugin_def)

    def load_plugin(self, defn):
        id_ = defn["uri"]
        path = defn["plugin"]
        class_ = hintmodules.utils.get_class_by_path(
            path,
            logger=self.logger
        )
        instance = class_(self, defn)
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
