#include "proto.h"

#include <string.h>

typedef struct Pb {
    const uint8_t *p;
    const uint8_t *end;
} Pb;

static int read_varint(Pb *pb, uint64_t *out) {
    uint64_t value = 0;
    int shift = 0;
    while (pb->p < pb->end && shift <= 63) {
        uint8_t byte = *pb->p++;
        value |= (uint64_t)(byte & 0x7Fu) << shift;
        if ((byte & 0x80u) == 0) {
            *out = value;
            return 0;
        }
        shift += 7;
    }
    return -1;
}

static int skip_value(Pb *pb, uint32_t wire) {
    uint64_t len = 0;
    if (wire == 0) {
        return read_varint(pb, &len);
    }
    if (wire == 1) {
        if ((size_t)(pb->end - pb->p) < 8) {
            return -1;
        }
        pb->p += 8;
        return 0;
    }
    if (wire == 5) {
        if ((size_t)(pb->end - pb->p) < 4) {
            return -1;
        }
        pb->p += 4;
        return 0;
    }
    if (wire == 2) {
        if (read_varint(pb, &len) != 0) {
            return -1;
        }
        if (len > (uint64_t)(pb->end - pb->p)) {
            return -1;
        }
        pb->p += (size_t)len;
        return 0;
    }
    return -1;
}

static int take_bytes(Pb *pb, const uint8_t **data, size_t *len) {
    uint64_t n = 0;
    if (read_varint(pb, &n) != 0) {
        return -1;
    }
    if (n > (uint64_t)(pb->end - pb->p)) {
        return -1;
    }
    *data = pb->p;
    *len = (size_t)n;
    pb->p += (size_t)n;
    return 0;
}

static int next_field(Pb *pb, uint32_t *field, uint32_t *wire, uint64_t *varint, const uint8_t **bytes, size_t *len) {
    uint64_t key = 0;
    if (pb->p >= pb->end) {
        return 1;
    }
    if (read_varint(pb, &key) != 0) {
        return -1;
    }
    *field = (uint32_t)(key >> 3);
    *wire = (uint32_t)(key & 7u);
    *varint = 0;
    *bytes = NULL;
    *len = 0;
    if (*wire == 0) {
        return read_varint(pb, varint);
    }
    if (*wire == 2) {
        return take_bytes(pb, bytes, len);
    }
    return skip_value(pb, *wire);
}

static int looks_like_text(const uint8_t *data, size_t len) {
    size_t i;
    if (len == 0) {
        return 1;
    }
    for (i = 0; i < len; i++) {
        if (data[i] == 0 || data[i] < 9) {
            return 0;
        }
    }
    return 1;
}

static void copy_text(char *dst, size_t dst_len, const uint8_t *src, size_t src_len) {
    size_t n;
    if (!dst || dst_len == 0) {
        return;
    }
    n = src_len;
    if (n >= dst_len) {
        n = dst_len - 1;
    }
    memcpy(dst, src, n);
    dst[n] = '\0';
}

int ff_pb_two_strings(const uint8_t *data, size_t len, char *first, size_t first_len, char *second, size_t second_len) {
    Pb pb;
    if (first && first_len) {
        first[0] = '\0';
    }
    if (second && second_len) {
        second[0] = '\0';
    }
    pb.p = data;
    pb.end = data + len;
    while (pb.p < pb.end) {
        uint32_t field = 0;
        uint32_t wire = 0;
        uint64_t varint = 0;
        const uint8_t *bytes = NULL;
        size_t n = 0;
        int rc = next_field(&pb, &field, &wire, &varint, &bytes, &n);
        if (rc != 0) {
            return rc < 0 ? -1 : 0;
        }
        if (wire != 2 || !looks_like_text(bytes, n)) {
            continue;
        }
        if (field == 1) {
            copy_text(first, first_len, bytes, n);
        } else if (field == 2) {
            copy_text(second, second_len, bytes, n);
        }
    }
    return 0;
}

int ff_pb_field_varint(const uint8_t *data, size_t len, uint32_t want, uint64_t *out) {
    Pb pb;
    pb.p = data;
    pb.end = data + len;
    while (pb.p < pb.end) {
        uint32_t field = 0;
        uint32_t wire = 0;
        uint64_t varint = 0;
        const uint8_t *bytes = NULL;
        size_t n = 0;
        int rc = next_field(&pb, &field, &wire, &varint, &bytes, &n);
        if (rc != 0) {
            return rc < 0 ? -1 : 1;
        }
        if (field == want && wire == 0) {
            *out = varint;
            return 0;
        }
    }
    return 1;
}

static int parse_entry(const uint8_t *data, size_t len, char *name, size_t name_len, int *legacy) {
    Pb pb;
    name[0] = '\0';
    *legacy = -1;
    pb.p = data;
    pb.end = data + len;
    while (pb.p < pb.end) {
        uint32_t field = 0;
        uint32_t wire = 0;
        uint64_t varint = 0;
        const uint8_t *bytes = NULL;
        size_t n = 0;
        int rc = next_field(&pb, &field, &wire, &varint, &bytes, &n);
        if (rc != 0) {
            return rc < 0 ? -1 : 0;
        }
        if (field == 1 && wire == 2 && looks_like_text(bytes, n)) {
            copy_text(name, name_len, bytes, n);
        } else if (field == 255 && wire == 0 && varint <= 1000000) {
            *legacy = (int)varint;
        }
    }
    return 0;
}

int ff_pb_media_names(const uint8_t *data, size_t len, FfMediaNameFn on_name, void *user) {
    Pb pb;
    int index = 0;
    pb.p = data;
    pb.end = data + len;
    while (pb.p < pb.end) {
        uint32_t field = 0;
        uint32_t wire = 0;
        uint64_t varint = 0;
        const uint8_t *bytes = NULL;
        size_t n = 0;
        char name[512];
        int legacy = -1;
        int rc = next_field(&pb, &field, &wire, &varint, &bytes, &n);
        if (rc != 0) {
            return rc < 0 ? -1 : 0;
        }
        if (field != 1 || wire != 2) {
            continue;
        }
        if (parse_entry(bytes, n, name, sizeof(name), &legacy) != 0) {
            return -1;
        }
        if (name[0] && on_name) {
            if (on_name(index, name, legacy, user) != 0) {
                return 0;
            }
        }
        index++;
        if (index > 200000) {
            return -1;
        }
    }
    return 0;
}
