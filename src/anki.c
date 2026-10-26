#include "anki_priv.h"

#include "path.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

static void set_err(char *err, int err_len, const char *msg) {
    if (err && err_len > 0) {
        ff_copy_cap(err, (size_t)err_len, msg ? msg : "");
    }
}

int ff_anki_open(FfAnki **out, const char *utf8_path, char *err, int err_len) {
    FfAnki *anki;
    int rc;
    if (!out || !utf8_path) {
        set_err(err, err_len, "Missing Anki collection.");
        return -1;
    }
    *out = NULL;
    anki = (FfAnki *)calloc(1, sizeof(*anki));
    if (!anki) {
        set_err(err, err_len, "Out of memory.");
        return -1;
    }
    anki->types = (FfNoteType *)calloc(FF_MAX_TYPES, sizeof(FfNoteType));
    anki->decks = (FfAnkiDeckName *)calloc(FF_MAX_DECKS, sizeof(FfAnkiDeckName));
    if (!anki->types || !anki->decks) {
        ff_anki_close(anki);
        set_err(err, err_len, "Out of memory.");
        return -1;
    }
#ifdef _WIN32
    {
        wchar_t wide[FF_PATH_MAX];
        if (ff_path_widen(utf8_path, wide, FF_PATH_MAX) != 0) {
            ff_anki_close(anki);
            set_err(err, err_len, "Could not read the collection path.");
            return -1;
        }
        rc = sqlite3_open16(wide, &anki->db);
    }
#else
    rc = sqlite3_open(utf8_path, &anki->db);
#endif
    if (rc != SQLITE_OK) {
        set_err(err, err_len, anki->db ? sqlite3_errmsg(anki->db) : "Could not open the Anki collection.");
        ff_anki_close(anki);
        return -1;
    }
    sqlite3_exec(anki->db, "PRAGMA query_only=ON; PRAGMA cache_size=-256;", NULL, NULL, NULL);
    if (ff_anki_load_models(anki, err, err_len) != 0) {
        ff_anki_close(anki);
        return -1;
    }
    *out = anki;
    return 0;
}

void ff_anki_close(FfAnki *anki) {
    if (!anki) {
        return;
    }
    sqlite3_close(anki->db);
    free(anki->types);
    free(anki->decks);
    free(anki);
}

static void map_sched(int type, int queue, int due, int ivl, int factor, int reps, int lapses, int64_t crt, FfCardSched *sched, int *suspended) {
    memset(sched, 0, sizeof(*sched));
    *suspended = queue < 0;
    sched->reps = reps > 0 ? reps : 0;
    sched->lapses = lapses > 0 ? lapses : 0;
    sched->ease = factor >= 1300 ? factor : 2500;
    if (type == 2) {
        if (ivl < 1) {
            ivl = 1;
        }
        sched->state = FF_STATE_REVIEW;
        sched->interval_secs = (int64_t)ivl * 86400;
        sched->due = crt + (int64_t)due * 86400;
        return;
    }
    if (type == 0) {
        sched->state = FF_STATE_NEW;
        sched->due = due;
        return;
    }
    sched->state = type == 3 ? FF_STATE_RELEARNING : FF_STATE_LEARNING;
    sched->interval_secs = ivl > 0 ? (int64_t)ivl * 86400 : 0;
    if (due > 100000000) {
        sched->due = due;
    } else {
        sched->due = crt + (int64_t)due * 86400;
    }
}

static int split_fields(char *flds, char **out, int max_fields) {
    int count = 0;
    char *cursor = flds ? flds : "";
    if (max_fields <= 0) {
        return 0;
    }
    out[count++] = cursor;
    while (*cursor && count < max_fields) {
        if (*cursor == '\x1f') {
            *cursor = '\0';
            out[count++] = cursor + 1;
        }
        cursor++;
    }
    return count;
}

static void preview_from_fields(const char *flds, char *dst, int dst_len) {
    char *copy = ff_strdup(flds ? flds : "");
    char *sep;
    char *plain = NULL;
    if (!copy) {
        ff_copy_cap(dst, (size_t)dst_len, "");
        return;
    }
    sep = strchr(copy, '\x1f');
    if (sep) {
        *sep = '\0';
    }
    if (ff_html_to_text(copy, &plain) == 0) {
        ff_snippet(plain, dst, dst_len);
    } else {
        ff_snippet(copy, dst, dst_len);
    }
    free(plain);
    free(copy);
}

static int render_card(FfAnki *anki, int64_t mid, int ord, char *flds, FfAnkiView *view) {
    FfNoteType *nt = ff_anki_type(anki, mid);
    char *fields[FF_MAX_FIELDS];
    const char *names[FF_MAX_FIELDS];
    int nfields;
    int tmpl;
    int i;
    nfields = split_fields(flds, fields, FF_MAX_FIELDS);
    if (!nt) {
        const char *fallback_names[2] = {"Front", "Back"};
        if (ff_render_side("{{Front}}", fields, fallback_names, nfields, ord + 1, 0, NULL, &view->front) != 0) {
            return 0;
        }
        if (ff_render_side("{{FrontSide}}<hr>{{Back}}", fields, fallback_names, nfields, ord + 1, 1, view->front.text, &view->back) != 0) {
            ff_rendered_free(&view->front);
            return 0;
        }
        view->rendered = 1;
        return 1;
    }
    for (i = 0; i < nfields && i < nt->nfields; i++) {
        names[i] = nt->fields[i];
    }
    for (; i < nfields; i++) {
        names[i] = "";
    }
    tmpl = ord;
    if (tmpl < 0 || tmpl >= nt->ntmpls) {
        tmpl = 0;
    }
    if (ff_render_side(nt->qfmt[tmpl], fields, names, nfields, ord + 1, 0, NULL, &view->front) != 0) {
        return 0;
    }
    if (ff_render_side(nt->afmt[tmpl], fields, names, nfields, ord + 1, 1, view->front.text, &view->back) != 0) {
        ff_rendered_free(&view->front);
        return 0;
    }
    view->rendered = 1;
    return 1;
}

int ff_anki_each(FfAnki *anki, int render, FfAnkiFn fn, void *user, int *rows, int *stubs, char *err, int err_len) {
    sqlite3_stmt *stmt = NULL;
    const char *sql =
        "SELECT n.id, n.mid, n.flds, c.id, c.did, c.ord, c.type, c.queue, c.due, c.ivl, c.factor, c.reps, c.lapses "
        "FROM notes n JOIN cards c ON c.nid = n.id ORDER BY c.did, n.id, c.ord";
    if (rows) {
        *rows = 0;
    }
    if (stubs) {
        *stubs = 0;
    }
    if (sqlite3_prepare_v2(anki->db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        set_err(err, err_len, "This Anki collection does not have the expected notes table.");
        return -1;
    }
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        const char *flds = (const char *)sqlite3_column_text(stmt, 2);
        char *owned;
        FfAnkiView view;
        int stop;
        if (flds && strstr(flds, "Please update to the latest Anki version")) {
            if (stubs) {
                (*stubs)++;
            }
            continue;
        }
        if (sqlite3_column_bytes(stmt, 2) > 1024 * 1024) {
            continue;
        }
        owned = ff_strdup(flds ? flds : "");
        if (!owned) {
            sqlite3_finalize(stmt);
            set_err(err, err_len, "Out of memory.");
            return -1;
        }
        memset(&view, 0, sizeof(view));
        view.anki_deck_id = sqlite3_column_int64(stmt, 4);
        view.source_id = sqlite3_column_int64(stmt, 3);
        view.ord = sqlite3_column_int(stmt, 5);
        ff_anki_deck_name(anki, view.anki_deck_id, view.deck_name, sizeof(view.deck_name));
        view.has_image = ff_note_has_image(owned);
        view.has_audio = ff_note_has_audio(owned);
        map_sched(
            sqlite3_column_int(stmt, 6),
            sqlite3_column_int(stmt, 7),
            sqlite3_column_int(stmt, 8),
            sqlite3_column_int(stmt, 9),
            sqlite3_column_int(stmt, 10),
            sqlite3_column_int(stmt, 11),
            sqlite3_column_int(stmt, 12),
            anki->crt,
            &view.sched,
            &view.suspended);
        if (render) {
            if (!render_card(anki, sqlite3_column_int64(stmt, 1), view.ord, owned, &view)) {
                free(owned);
                sqlite3_finalize(stmt);
                set_err(err, err_len, "Could not render a card.");
                return -1;
            }
            ff_snippet(view.front.text, view.preview, (int)sizeof(view.preview));
        } else {
            preview_from_fields(owned, view.preview, (int)sizeof(view.preview));
        }
        free(owned);
        if (rows) {
            (*rows)++;
        }
        stop = fn ? fn(&view, user) : 0;
        if (view.rendered) {
            ff_rendered_free(&view.front);
            ff_rendered_free(&view.back);
        }
        if (stop) {
            sqlite3_finalize(stmt);
            return stop;
        }
    }
    sqlite3_finalize(stmt);
    return 0;
}
