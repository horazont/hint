import abc
import functools
import sys

from datetime import datetime, timedelta

class CacheEntry:
    expires = None
    data = None

    def __init__(self, data=None):
        super().__init__()
        self.data = data

class RequestError(Exception):
    def __init__(self, *args,
                 back_off=False,
                 cache_entry=None,
                 use_context=True):
        super().__init__(*args)
        self.back_off = back_off
        self.cache_entry = cache_entry
        self.use_context = use_context

class BackingOff(RuntimeError):
    """
    The :class:`BackingOff` exception is thrown when the requester is in back
    off mode, that is, previous requests have failed and no requests will be
    performed until a certain time point.

    .. attribute:: until

       Timestamp until the requester will not perform any requests.
    """

    def __init__(self, until, **kwargs):
        self.until = until
        super().__init__(
            "Backing off from API until {}".format(until),
            **kwargs)

class AdvancedRequester(metaclass=abc.ABCMeta):
    """
    This is a requester which takes care of several functions required to
    suitably request different APIs.

    * Caching: Automatically cache data based on parameters. This requires that
      all parameters are hashable; if that is not the case, the cache must be
      explicitly disabled at the time of the request.
    * Exponential back off: If a request failed, requests are forced to cached
      data or failure for some time before retrying. This is useful to
      circumvent rate limiting and to take load off APIs being overloaded.

    Some parameters control the behaviour of the requester.

    If *back_off* is :data:`True`, exponential back off mode is enabled. If a
    request fails with :attr:`RequestError.back_off` set to :data:`True`, the
    API will be barred from queries for at least *initial_back_off_time*. If
    subsequent calls still fail, the interval is doubled each time a call
    failed. If a call succeeds, the interval is reset to *initial_back_off_time*
    and calls are allowed normally again.

    *back_off_cap* can either be a :class:`datetime.timedelta` or
    :data:`None`. If it is :data:`None`, it defaults to ten times the
    *initial_back_off_time*. *back_off_cap* specifies the maximum time to bar
    the API from requests.
    """

    def __init__(self, *,
                 back_off=True,
                 initial_back_off_time=timedelta(seconds=10),
                 back_off_cap=None,
                 **kwargs):
        super().__init__(**kwargs)
        self._cache = {}
        self.back_off = back_off
        self.backing_off = False
        self.backing_off_until = None
        self.initial_back_off_time = initial_back_off_time
        self.backing_off_interval = initial_back_off_time
        self.back_off_cap = back_off_cap or initial_back_off_time * 10

    def _derive_cache_key(self, **kwargs):
        return frozenset(kwargs.items())

    @abc.abstractmethod
    def _perform_request(self, expired_cache_entry=None, **kwargs):
        """
        Actually perform a request issued through :meth:`request`. This is
        called by :meth:`request` if the cache was unable to supply the
        information.

        The *kwargs* are those passed to :meth:`request` (minus the explicitly
        specified keyword arguments).

        *expired_cache_entry* is either :data:`None` or the :class:`CacheEntry`
        instance which was previously associated with the given arguments, but
        expired. Some APIs might take advantage of cached information.

        If the request fails, the method raises a :class:`RequestError`.

        Return a cache entry to use. The cache entry will be stored in the
        cache, preserving all attributes, and may be passed to this function
        during a subsequent function call.

        The returned cache entry must have a properly set
        :attr:`CacheEntry.expires` attribute. When returning the
        *expired_cache_entry*, it might or might not be useful to update the
        expires, which is why it is left up to the implementation to take care
        of a proper timestamp value.
        """

    def _get_backing_off_result(self, expired_cache_entry=None, **kwargs):
        """
        This method is called by the implementation whenever it is in backing
        off mode and a request was about to be issued.

        :meth:`_perform_request` must not be called and no request must be made
        otherwise. Other resources than the resources queried by this object may
        be used though.

        The *expired_cache_entry* may be a :class:`CacheEntry` object pointing
        to existing data for the request given.

        Return a :class:`CacheEntry` object or :data:`None` if no data can be
        returned. In that case, the caller will throw a :class:`BackingOff`
        exception.

        The default implementation does return :data:`None`, to avoid silently
        returning stale data.
        """

        return None

    def _execute_request(self, now, expired_cache_entry, kwargs):
        """
        Try to execute a request using *kwargs* and return the result.

        Two cases are covered here:

        * Back-off mode is engaged and not yet expired: In that case,
          :meth:`_get_backing_off_result` is called for a result. If the result
          is :data:`None`, a :class:`BackingOff` exception is thrown.

        * Back-off mode is engaged and expired, or back-off mode is not engaged:
          The normal :meth:`_perform_request` call is made; If the call fails
          with an :class:`RequestError`, back-off mode is engaged or continued,
          except if it has been turned off explicitly in the error.

          If no data has been returned inside the :class:`RequestError`, the
          causing error is rethrown (if the :class:`RequestError` is the cause
          itself, it will be rethrown instead of the cause).

        Returns the data returned by the respectively called function.
        """

        if self.backing_off and self.backing_off_until > now:
            cache_entry = self._get_backing_off_result(
                expired_cache_entry,
                **kwargs)
            if cache_entry is None:
                raise BackingOff(self.backing_off_until)
            return cache_entry

        try:
            cache_entry = self._perform_request(
                expired_cache_entry,
                **kwargs)
        except RequestError as err:
            cache_entry = err.cache_entry

            if err.back_off:
                if self.backing_off:
                    self.backing_off_interval *= 2
                    if self.backing_off_interval > self.back_off_cap:
                        self.backing_off_interval = self.back_off_cap
                else:
                    self.backing_off = True
                    self.backing_off_interval = self.initial_back_off_time

                self.backing_off_until = now + self.backing_off_interval

            if cache_entry is None:
                if (    not err.use_context or
                        not hasattr(err, "__context__") or
                        err.__context__ is None):
                    raise
                else:
                    context = err.__context__
                    subcontext = context.__context__ \
                        if hasattr(context.__context__) else None
                    raise context from subcontext
        else:
            self.backing_off = False

        return cache_entry

    def request(self, *, dont_cache=False, **kwargs):
        now = datetime.utcnow()

        if dont_cache:
            return self._execute_request(now, **kwargs).data

        cache_key = self._derive_cache_key(**kwargs)

        try:
            cache_entry = self._cache[cache_key]
        except KeyError:
            cache_entry = None
        else:
            if cache_entry.expires >= now:
                return cache_entry.data

        cache_entry = self._execute_request(
            now,
            cache_entry,
            kwargs)
        # the timestamp must have been set in _execute_request
        self._cache[cache_key] = cache_entry

        return cache_entry.data
