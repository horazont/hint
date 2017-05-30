import asyncio
import functools


def log_future(logger, fut):
    try:
        result = fut.result()
    except asyncio.CancelledError:
        logger.debug("task %r cancelled", fut)
    except:
        logger.error("task %r failed", fut, exc_info=True)
    else:
        if result is not None:
            logger.info("task %r returned value: %r", fut, result)


def logged_future(logger, coro):
    task = asyncio.ensure_future(coro)
    task.add_done_callback(
        functools.partial(log_future, logger),
    )
    return task
