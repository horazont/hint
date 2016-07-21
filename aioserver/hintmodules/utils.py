import asyncio


def log_failure(logger):
    def impl(fut):
        try:
            result = fut.result()
        except asyncio.CancelledError:
            pass
        except:
            logger.exception("%r raised an exception", fut)
        else:
            if result is not None:
                logger.info("%r returned an unexpected result: %r",
                            fut, result)
    return impl
