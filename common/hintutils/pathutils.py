import contextlib
import os
import pathlib
import tempfile
import urllib.parse


def escape_path(s):
    return urllib.parse.quote(s, safe=" ")


def unescape_path(s):
    return urllib.parse.unquote(s)


@contextlib.contextmanager
def safe_writer(destpath, mode="wb"):
    destpath = pathlib.Path(destpath)
    with tempfile.NamedTemporaryFile(
            mode=mode,
            dir=str(destpath.parent),
            prefix=".",
            delete=False) as tmpfile:
        try:
            yield tmpfile
        except:
            os.unlink(tmpfile.name)
            raise
        else:
            os.replace(tmpfile.name, str(destpath))


@contextlib.contextmanager
def extremely_safe_writer(destpath, mode="wb"):
    dirfd = os.open(str(destpath.parent), os.O_DIRECTORY)
    try:
        with safe_writer(destpath, mode) as f:
            yield f
            os.fsync(f.fileno())
        os.fsync(dirfd)
    finally:
        os.close(dirfd)


def write_file_safe(path, parts):
    dirfd = os.open(str(path.parent), os.O_DIRECTORY)
    try:
        with safe_writer(path, "xb") as f:
            f.writelines(parts)
            f.flush()
            os.fsync(f.fileno())
        os.fsync(dirfd)
    finally:
        os.close(dirfd)
