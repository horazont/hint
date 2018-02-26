import aioxmpp


class Buddies(aioxmpp.service.Service):
    ORDER_AFTER = [aioxmpp.RosterClient]

    def __init__(self, client, **kwargs):
        super().__init__(client, **kwargs)
        self.__buddies = []

    def load_buddies(self, buddies_cfg):
        self.__buddies = []
        for buddy in buddies_cfg:
            self.__buddies.append(
                (
                    aioxmpp.JID.fromstr(buddy["jid"]),
                    set(buddy.get("permissions", []))
                )
            )

    def derive_logger(self, logger):
        return logger.getChild("botlib.buddies")

    def get_by_permissions(self, keys):
        for jid, perms, *_ in self.__buddies:
            if "*" in perms or (perms & keys) == keys:
                yield jid

    @aioxmpp.service.depsignal(aioxmpp.Client, "on_stream_established")
    def on_stream_established(self):
        roster = self.dependencies[aioxmpp.RosterClient]
        for jid, *_ in self.__buddies:
            roster.approve(jid)
            roster.subscribe(jid)


class BotCore:
    def __init__(self, xmpp_config):
        super().__init__()
        self.__config = xmpp_config
        self.client = aioxmpp.Client(
            aioxmpp.JID.fromstr(xmpp_config["jid"]),
            aioxmpp.make_security_layer(
                xmpp_config["password"]
            )
        )

        self.__nested_cm = None

        self.buddies = self.client.summon(Buddies)
        self.buddies.load_buddies(xmpp_config.get("buddies", []))

    async def __aenter__(self):
        self.__nested_cm = self.client.connected(
            presence=aioxmpp.PresenceState(True),
        )
        return (await self.__nested_cm.__aenter__())

    async def __aexit__(self, exc_type, exc_value, exc_traceback):
        return (await self.__nested_cm.__aexit__(
            exc_type,
            exc_value,
            exc_traceback,
        ))
