#ifndef _COMMON_TYPES_H
#define _COMMON_TYPES_H

#include <stdint.h>
#include <stdbool.h>

typedef uint16_t lpc_cmd_id_t;
typedef uint16_t colour_t;
typedef int16_t coord_int_t;
typedef uint8_t table_column_alignment_t;

#define TABLE_ALIGN_LEFT (0)
#define TABLE_ALIGN_RIGHT (1)
#define TABLE_ALIGN_CENTER (2)

struct __attribute__((packed)) table_column_t {
    coord_int_t width;
    table_column_alignment_t alignment;
};

#endif
