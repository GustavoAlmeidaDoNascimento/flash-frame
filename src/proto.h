#ifndef FF_PROTO_H
#define FF_PROTO_H

#include <stddef.h>
#include <stdint.h>

typedef int (*FfMediaNameFn)(int index, const char *name, int legacy_index, void *user);

int ff_pb_media_names(const uint8_t *data, size_t len, FfMediaNameFn on_name, void *user);
int ff_pb_two_strings(const uint8_t *data, size_t len, char *first, size_t first_len, char *second, size_t second_len);
int ff_pb_field_varint(const uint8_t *data, size_t len, uint32_t field, uint64_t *out);

#endif
