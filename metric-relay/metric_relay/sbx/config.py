import array
import pathlib

from schema import Schema, Optional, Or, And, Const


_batch_size = And(int, Const(lambda x: x > 0))

stream_schema = Schema({
    "part": str,
    Optional("instance", default=0): Or(int, str),
    "subpart": str,
    Optional("range", default=1.0): float,
    Optional("sample_type", default="h"): And(str, Const(array.array)),
    Optional("batch_size"): _batch_size,
})

sbx_source_schema = Schema({
    "module_name": str,
    Optional("default_stream_batch_size", default=512): _batch_size,
    "streams": [stream_schema],
    "samples": {
        "rewrite": [Or(
            {
                "rewrite": "instance",
                "part": str,
                "instance": str,
                "new_instance": str,
            }
        )],
    },
})
