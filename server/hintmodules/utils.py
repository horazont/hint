import traceback
import itertools
import urllib.error
import urllib.request
from wsgiref.handlers import format_date_time
from datetime import datetime, timedelta
import email.utils as eutils
from calendar import timegm

def to_timestamp(datetime):
    """
    Convert *datetime* to a UTC unix timestamp.
    """
    return timegm(datetime.utctimetuple())

def parse_http_date(httpdate):
    return datetime(*eutils.parsedate(httpdate)[:6])

def format_http_date(datetime):
    return wsgiref.handlers.format_date_time(to_timestamp(datetime))

def http_request(url,
                 user_agent=None,
                 accept=None,
                 last_modified=None,
                 headers=dict(),
                 timeout=3):
    use_headers = {}
    if user_agent is not None:
        use_headers["User-Agent"] = user_agent
    if accept is not None:
        use_headers["Accept"] = accept
    use_headers.update(headers)
    if last_modified is not None:
        use_headers["If-Modified-Since"] = format_date_time(
            to_timestamp(last_modified))

    request = urllib.request.Request(url, headers=use_headers)
    response = urllib.request.urlopen(request, timeout=timeout)
    last_modified = response.info().get("Last-Modified", None)
    if last_modified is not None:
        timestamp = parse_http_date(last_modified)
    else:
        timestamp = None
    return response, timestamp

def date_to_key(date):
    return (date.year, date.month, date.day, date.hour)

def strip_date(date):
    return datetime(date.year, date.month, date.day, date.hour)

def iter_all_plugins(stanza):
    iterables = iter(stanza.iterables)
    non_iterables = stanza.plugins.values()
    return itertools.chain(iterables, non_iterables)
