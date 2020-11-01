import asyncio
import sys

import smbusd.daemon


async def amain():
    daemon = smbusd.daemon.SMBusd()
    await daemon.run()


def main():
    asyncio.run(amain())


def main_wrap():
    sys.exit(main() or 0)
