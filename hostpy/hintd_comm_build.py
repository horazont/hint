import re

from cffi import FFI

import hintd.protocol

ffibuilder = FFI()
ffibuilder.set_source(
    "_hintd_comm",
    """
    #include "../common/comm_lpc1114.h"
    #include "../common/comm_arduino.h"
    """
)

simple_typedef_re = re.compile(
    r"""typedef\s+\w+\s+\w+;""",
    re.VERBOSE
)

struct_re = re.compile(
    r"""struct\s+(__attribute__\(\(packed\)\)\s*)?\w+\s*\{
    .+?
    \};""",
    re.VERBOSE | re.I | re.DOTALL
)


def extract_structs(fname):
    with open(fname) as f:
        source = f.read()

    for item in simple_typedef_re.finditer(source):
        yield item.group(0)

    for item in struct_re.finditer(source):
        yield item.group(0).replace(
            "__attribute__((packed))", ""
        ).replace(
            "uint8_t raw[MSG_MAX_PAYLOAD-sizeof(lpc_cmd_id_t)];",
            "uint8_t raw[{}];".format(
                hintd.cconstants.MAX_PAYLOAD_LENGTH-2
            ),
        ).replace(
            "CFFI_DOTDOTDOT",
            "...;",
        )


defs = []
defs.extend(extract_structs("../common/types.h"))
defs.extend(extract_structs("../common/comm_lpc1114.h"))
defs.extend(extract_structs("../common/comm_arduino.h"))

ffibuilder.cdef(
    "\n".join(defs),
    packed=True
)

if __name__ == "__main__":
    ffibuilder.compile(verbose=True)
