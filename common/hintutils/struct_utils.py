def unpack_and_splice(buf, struct_obj):
    result = buf[struct_obj.size:]
    return result, struct_obj.unpack(buf[:struct_obj.size])


def unpack_all(buf, struct_obj, *, discard=False):
    size = struct_obj.size
    if len(buf) % size != 0 and not discard:
        raise ValueError(
            "buffer does not contain an integer number of structs"
        )
    return (
        struct_obj.unpack(buf[i*size:(i+1)*size])
        for i in range(len(buf)//size)
    )


def read_single(f, struct_obj):
    buf = bytearray()
    size = struct_obj.size
    while len(buf) < size:
        missing = size - len(buf)
        data = f.read(missing)
        if not data:
            raise EOFError
        buf.extend(data)
    result, = unpack_all(buf, struct_obj)
    return result


def read_all(f, struct_obj):
    while True:
        try:
            yield read_single(f, struct_obj)
        except EOFError:
            return


def write_single(f, struct_obj, *args):
    f.write(struct_obj.pack(*args))
