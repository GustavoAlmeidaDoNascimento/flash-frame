#ifndef FF_ANKI_PRIV_H
#define FF_ANKI_PRIV_H

#include "anki.h"
#include "util.h"

#include "sqlite3.h"

#define FF_MAX_TYPES 32
#define FF_MAX_FIELDS 16
#define FF_MAX_TMPLS 6
#define FF_MAX_DECKS 512
#define FF_QFMT 2048
#define FF_AFMT 4096

typedef struct FfNoteType {
    int64_t id;
    char name[64];
    int cloze;
    int nfields;
    char fields[FF_MAX_FIELDS][64];
    int ntmpls;
    char qfmt[FF_MAX_TMPLS][FF_QFMT];
    char afmt[FF_MAX_TMPLS][FF_AFMT];
} FfNoteType;

typedef struct FfAnkiDeckName {
    int64_t id;
    char name[256];
} FfAnkiDeckName;

struct FfAnki {
    sqlite3 *db;
    int64_t crt;
    FfNoteType *types;
    int ntypes;
    FfAnkiDeckName *decks;
    int ndecks;
};

int ff_anki_load_models(FfAnki *anki, char *err, int err_len);

static inline FfNoteType *ff_anki_type(FfAnki *anki, int64_t id) {
    int i;
    for (i = 0; i < anki->ntypes; i++) {
        if (anki->types[i].id == id) {
            return &anki->types[i];
        }
    }
    return NULL;
}

static inline void ff_anki_deck_name(const FfAnki *anki, int64_t id, char *dst, size_t dst_len) {
    int i;
    for (i = 0; i < anki->ndecks; i++) {
        if (anki->decks[i].id == id) {
            ff_copy_cap(dst, dst_len, anki->decks[i].name);
            return;
        }
    }
    ff_copy_cap(dst, dst_len, "Imported");
}

#endif
