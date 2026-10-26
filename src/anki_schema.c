#include "anki_priv.h"

#include "proto.h"

#include "cJSON.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static void set_err(char *err, int err_len, const char *msg) {
    if (err && err_len > 0) {
        ff_copy_cap(err, (size_t)err_len, msg ? msg : "");
    }
}

static int table_exists(sqlite3 *db, const char *name) {
    sqlite3_stmt *stmt = NULL;
    int yes = 0;
    if (sqlite3_prepare_v2(db, "SELECT 1 FROM sqlite_master WHERE type='table' AND name=?", -1, &stmt, NULL) != SQLITE_OK) {
        return 0;
    }
    sqlite3_bind_text(stmt, 1, name, -1, SQLITE_STATIC);
    yes = sqlite3_step(stmt) == SQLITE_ROW;
    sqlite3_finalize(stmt);
    return yes;
}

static void fill_default_templates(FfNoteType *nt) {
    int t;
    const char *front = nt->nfields > 0 && nt->fields[0][0] ? nt->fields[0] : "Front";
    const char *back = nt->nfields > 1 && nt->fields[1][0] ? nt->fields[1] : front;
    if (nt->ntmpls < 1) {
        nt->ntmpls = 1;
    }
    if (nt->ntmpls > FF_MAX_TMPLS) {
        nt->ntmpls = FF_MAX_TMPLS;
    }
    for (t = 0; t < nt->ntmpls; t++) {
        if (nt->qfmt[t][0]) {
            continue;
        }
        if (nt->cloze) {
            snprintf(nt->qfmt[t], FF_QFMT, "{{cloze:%s}}", front);
            snprintf(nt->afmt[t], FF_AFMT, "{{cloze:%s}}", front);
        } else if (t > 0) {
            snprintf(nt->qfmt[t], FF_QFMT, "{{%s}}", back);
            snprintf(nt->afmt[t], FF_AFMT, "{{FrontSide}}<hr>{{%s}}", front);
        } else {
            snprintf(nt->qfmt[t], FF_QFMT, "{{%s}}", front);
            snprintf(nt->afmt[t], FF_AFMT, "{{FrontSide}}<hr>{{%s}}", back);
        }
    }
}

static int load_crt(FfAnki *anki) {
    sqlite3_stmt *stmt = NULL;
    anki->crt = (int64_t)time(NULL);
    if (sqlite3_prepare_v2(anki->db, "SELECT crt FROM col LIMIT 1", -1, &stmt, NULL) != SQLITE_OK) {
        return 0;
    }
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        anki->crt = sqlite3_column_int64(stmt, 0);
    }
    sqlite3_finalize(stmt);
    return 0;
}

static int load_schema18(FfAnki *anki, char *err, int err_len) {
    sqlite3_stmt *stmt = NULL;
    if (sqlite3_prepare_v2(anki->db, "SELECT id, name, config FROM notetypes ORDER BY id", -1, &stmt, NULL) != SQLITE_OK) {
        set_err(err, err_len, sqlite3_errmsg(anki->db));
        return -1;
    }
    while (sqlite3_step(stmt) == SQLITE_ROW && anki->ntypes < FF_MAX_TYPES) {
        FfNoteType *nt = &anki->types[anki->ntypes++];
        const unsigned char *name = sqlite3_column_text(stmt, 1);
        const void *blob = sqlite3_column_blob(stmt, 2);
        int bytes = sqlite3_column_bytes(stmt, 2);
        uint64_t kind = 0;
        memset(nt, 0, sizeof(*nt));
        nt->id = sqlite3_column_int64(stmt, 0);
        ff_copy_cap(nt->name, sizeof(nt->name), name ? (const char *)name : "Note");
        if (blob && bytes > 0 && bytes < 200000 &&
            ff_pb_field_varint((const uint8_t *)blob, (size_t)bytes, 1, &kind) == 0 && kind == 1) {
            nt->cloze = 1;
        }
    }
    sqlite3_finalize(stmt);

    if (sqlite3_prepare_v2(anki->db, "SELECT ntid, ord, name FROM fields ORDER BY ntid, ord", -1, &stmt, NULL) != SQLITE_OK) {
        set_err(err, err_len, sqlite3_errmsg(anki->db));
        return -1;
    }
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        FfNoteType *nt = ff_anki_type(anki, sqlite3_column_int64(stmt, 0));
        int ord = sqlite3_column_int(stmt, 1);
        const unsigned char *name = sqlite3_column_text(stmt, 2);
        if (!nt || ord < 0 || ord >= FF_MAX_FIELDS) {
            continue;
        }
        ff_copy_cap(nt->fields[ord], sizeof(nt->fields[ord]), name ? (const char *)name : "");
        if (ord + 1 > nt->nfields) {
            nt->nfields = ord + 1;
        }
    }
    sqlite3_finalize(stmt);

    if (sqlite3_prepare_v2(anki->db, "SELECT ntid, ord, config FROM templates ORDER BY ntid, ord", -1, &stmt, NULL) != SQLITE_OK) {
        set_err(err, err_len, sqlite3_errmsg(anki->db));
        return -1;
    }
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        FfNoteType *nt = ff_anki_type(anki, sqlite3_column_int64(stmt, 0));
        int ord = sqlite3_column_int(stmt, 1);
        const void *blob = sqlite3_column_blob(stmt, 2);
        int bytes = sqlite3_column_bytes(stmt, 2);
        if (!nt || ord < 0 || ord >= FF_MAX_TMPLS) {
            continue;
        }
        if (blob && bytes > 0 && bytes < 200000) {
            ff_pb_two_strings(
                (const uint8_t *)blob,
                (size_t)bytes,
                nt->qfmt[ord],
                sizeof(nt->qfmt[ord]),
                nt->afmt[ord],
                sizeof(nt->afmt[ord]));
        }
        if (ord + 1 > nt->ntmpls) {
            nt->ntmpls = ord + 1;
        }
    }
    sqlite3_finalize(stmt);

    if (table_exists(anki->db, "decks") &&
        sqlite3_prepare_v2(anki->db, "SELECT id, name FROM decks ORDER BY id", -1, &stmt, NULL) == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW && anki->ndecks < FF_MAX_DECKS) {
            const unsigned char *name = sqlite3_column_text(stmt, 1);
            anki->decks[anki->ndecks].id = sqlite3_column_int64(stmt, 0);
            ff_copy_cap(anki->decks[anki->ndecks].name, sizeof(anki->decks[anki->ndecks].name), name ? (const char *)name : "Imported");
            anki->ndecks++;
        }
        sqlite3_finalize(stmt);
    }
    return 0;
}

static int64_t json_id(const cJSON *item) {
    const cJSON *id = cJSON_GetObjectItemCaseSensitive(item, "id");
    if (item->string && item->string[0]) {
        return strtoll(item->string, NULL, 10);
    }
    if (cJSON_IsNumber(id)) {
        return (int64_t)id->valuedouble;
    }
    return 0;
}

static void copy_item_string(char *dst, size_t dst_len, const cJSON *obj, const char *key) {
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(obj, key);
    if (cJSON_IsString(item) && item->valuestring) {
        ff_copy_cap(dst, dst_len, item->valuestring);
    } else if (dst_len) {
        dst[0] = '\0';
    }
}

static int load_legacy(FfAnki *anki, char *err, int err_len) {
    sqlite3_stmt *stmt = NULL;
    const char *models_sql = "SELECT models, decks FROM col LIMIT 1";
    char *models_text = NULL;
    char *decks_text = NULL;
    cJSON *models = NULL;
    cJSON *decks = NULL;
    const cJSON *item;
    if (sqlite3_prepare_v2(anki->db, models_sql, -1, &stmt, NULL) != SQLITE_OK) {
        set_err(err, err_len, "This package has no Anki note types.");
        return -1;
    }
    if (sqlite3_step(stmt) != SQLITE_ROW) {
        sqlite3_finalize(stmt);
        set_err(err, err_len, "The Anki collection is empty.");
        return -1;
    }
    if (sqlite3_column_bytes(stmt, 0) > 2 * 1024 * 1024 || sqlite3_column_bytes(stmt, 1) > 2 * 1024 * 1024) {
        sqlite3_finalize(stmt);
        set_err(err, err_len, "The note-type data in this package is too large to import in light mode.");
        return -1;
    }
    models_text = ff_strdup((const char *)sqlite3_column_text(stmt, 0));
    decks_text = ff_strdup((const char *)sqlite3_column_text(stmt, 1));
    sqlite3_finalize(stmt);
    models = models_text ? cJSON_Parse(models_text) : NULL;
    decks = decks_text ? cJSON_Parse(decks_text) : NULL;
    free(models_text);
    free(decks_text);
    if (!cJSON_IsObject(models)) {
        cJSON_Delete(models);
        cJSON_Delete(decks);
        set_err(err, err_len, "Could not read the Anki note types.");
        return -1;
    }
    for (item = models->child; item && anki->ntypes < FF_MAX_TYPES; item = item->next) {
        FfNoteType *nt = &anki->types[anki->ntypes];
        const cJSON *type = cJSON_GetObjectItemCaseSensitive(item, "type");
        const cJSON *fields = cJSON_GetObjectItemCaseSensitive(item, "flds");
        const cJSON *tmpls = cJSON_GetObjectItemCaseSensitive(item, "tmpls");
        const cJSON *field;
        memset(nt, 0, sizeof(*nt));
        nt->id = json_id(item);
        copy_item_string(nt->name, sizeof(nt->name), item, "name");
        nt->cloze = cJSON_IsNumber(type) && type->valueint == 1;
        if (cJSON_IsArray(fields)) {
            cJSON_ArrayForEach(field, fields) {
                if (nt->nfields >= FF_MAX_FIELDS) {
                    break;
                }
                copy_item_string(nt->fields[nt->nfields], sizeof(nt->fields[nt->nfields]), field, "name");
                nt->nfields++;
            }
        }
        if (cJSON_IsArray(tmpls)) {
            cJSON_ArrayForEach(field, tmpls) {
                if (nt->ntmpls >= FF_MAX_TMPLS) {
                    break;
                }
                copy_item_string(nt->qfmt[nt->ntmpls], sizeof(nt->qfmt[nt->ntmpls]), field, "qfmt");
                copy_item_string(nt->afmt[nt->ntmpls], sizeof(nt->afmt[nt->ntmpls]), field, "afmt");
                nt->ntmpls++;
            }
        }
        anki->ntypes++;
    }
    if (cJSON_IsObject(decks)) {
        for (item = decks->child; item && anki->ndecks < FF_MAX_DECKS; item = item->next) {
            anki->decks[anki->ndecks].id = json_id(item);
            copy_item_string(anki->decks[anki->ndecks].name, sizeof(anki->decks[anki->ndecks].name), item, "name");
            if (!anki->decks[anki->ndecks].name[0]) {
                ff_copy_cap(anki->decks[anki->ndecks].name, sizeof(anki->decks[anki->ndecks].name), "Imported");
            }
            anki->ndecks++;
        }
    }
    cJSON_Delete(models);
    cJSON_Delete(decks);
    return 0;
}

int ff_anki_load_models(FfAnki *anki, char *err, int err_len) {
    int i;
    int rc;
    load_crt(anki);
    if (table_exists(anki->db, "notetypes")) {
        rc = load_schema18(anki, err, err_len);
    } else {
        rc = load_legacy(anki, err, err_len);
    }
    if (rc != 0) {
        return rc;
    }
    for (i = 0; i < anki->ntypes; i++) {
        fill_default_templates(&anki->types[i]);
    }
    if (anki->ndecks == 0) {
        anki->decks[0].id = 1;
        ff_copy_cap(anki->decks[0].name, sizeof(anki->decks[0].name), "Imported");
        anki->ndecks = 1;
    }
    return 0;
}
