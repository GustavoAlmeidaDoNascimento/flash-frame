#include "apkg.h"

#include "anki.h"
#include "path.h"
#include "proto.h"
#include "util.h"

#include "cJSON.h"
#include "miniz.h"
#include "zstd.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

#define MEDIA_BUCKETS 2048

typedef struct MediaNode {
    char *zip_name;
    char *file_name;
    struct MediaNode *next;
} MediaNode;

typedef struct MediaMap {
    MediaNode *buckets[MEDIA_BUCKETS];
    int count;
} MediaMap;

typedef struct DeckLink {
    int64_t anki;
    int64_t ours;
} DeckLink;

typedef struct ImportCtx {
    const FfImportRequest *request;
    FfImportResult *result;
    mz_zip_archive *zip;
    MediaMap map;
    DeckLink links[512];
    int nlinks;
    int64_t created[512];
    int ncreated;
    int map_ready;
} ImportCtx;

static void set_err(char *err, int err_len, const char *msg) {
    if (err && err_len > 0) {
        ff_copy_cap(err, (size_t)err_len, msg ? msg : "");
    }
}

static const char *base_name(const char *path) {
    const char *base = path ? path : "";
    const char *cursor;
    for (cursor = base; *cursor; cursor++) {
        if (*cursor == '/' || *cursor == '\\') {
            base = cursor + 1;
        }
    }
    return base;
}

static int collection_rank(const char *name) {
    name = base_name(name);
    if (strcmp(name, "collection.anki21b") == 0) {
        return 3;
    }
    if (strcmp(name, "collection.anki21") == 0) {
        return 2;
    }
    if (strcmp(name, "collection.anki2") == 0) {
        return 1;
    }
    return 0;
}

static unsigned hash_text(const char *text) {
    unsigned hash = 5381u;
    while (text && *text) {
        hash = hash * 33u + (unsigned char)*text++;
    }
    return hash % MEDIA_BUCKETS;
}

static void map_free(MediaMap *map) {
    int i;
    for (i = 0; i < MEDIA_BUCKETS; i++) {
        MediaNode *node = map->buckets[i];
        while (node) {
            MediaNode *next = node->next;
            free(node->zip_name);
            free(node->file_name);
            free(node);
            node = next;
        }
        map->buckets[i] = NULL;
    }
    map->count = 0;
}

static int map_add(MediaMap *map, const char *zip_name, const char *file_name) {
    unsigned bucket;
    MediaNode *node;
    if (!zip_name || !file_name || !file_name[0] || map->count > 100000) {
        return 0;
    }
    node = (MediaNode *)calloc(1, sizeof(*node));
    if (!node) {
        return -1;
    }
    node->zip_name = ff_strdup(zip_name);
    node->file_name = ff_strdup(file_name);
    if (!node->zip_name || !node->file_name) {
        free(node->zip_name);
        free(node->file_name);
        free(node);
        return -1;
    }
    bucket = hash_text(file_name);
    node->next = map->buckets[bucket];
    map->buckets[bucket] = node;
    map->count++;
    return 0;
}

static MediaNode *map_find(MediaMap *map, const char *file_name) {
    MediaNode *node = map->buckets[hash_text(file_name)];
    while (node) {
        if (strcmp(node->file_name, file_name) == 0) {
            return node;
        }
        node = node->next;
    }
    return NULL;
}

static int on_proto_name(int index, const char *name, int legacy, void *user) {
    char zip_name[32];
    int zip_index = legacy >= 0 ? legacy : index;
    snprintf(zip_name, sizeof(zip_name), "%d", zip_index);
    return map_add((MediaMap *)user, zip_name, name) == 0 ? 0 : -1;
}

static int parse_media_json(MediaMap *map, char *text) {
    cJSON *root;
    const cJSON *item;
    root = cJSON_Parse(text);
    if (!cJSON_IsObject(root)) {
        cJSON_Delete(root);
        return -1;
    }
    for (item = root->child; item; item = item->next) {
        if (!cJSON_IsString(item) || !item->string || !item->valuestring) {
            continue;
        }
        if (map_add(map, item->string, item->valuestring) != 0) {
            cJSON_Delete(root);
            return -1;
        }
    }
    cJSON_Delete(root);
    return 0;
}

static int64_t zip_size(FILE *file) {
#ifdef _WIN32
    if (_fseeki64(file, 0, SEEK_END) != 0) {
        return -1;
    }
    return (int64_t)_ftelli64(file);
#else
    if (fseeko(file, 0, SEEK_END) != 0) {
        return -1;
    }
    return (int64_t)ftello(file);
#endif
}

static void zip_rewind(FILE *file) {
#ifdef _WIN32
    _fseeki64(file, 0, SEEK_SET);
#else
    fseeko(file, 0, SEEK_SET);
#endif
}

static int find_named(mz_zip_archive *zip, const char *name) {
    mz_uint count = mz_zip_reader_get_num_files(zip);
    mz_uint i;
    for (i = 0; i < count; i++) {
        mz_zip_archive_file_stat stat;
        if (!mz_zip_reader_file_stat(zip, i, &stat) || stat.m_is_directory) {
            continue;
        }
        if (strcmp(stat.m_filename, name) == 0 || strcmp(base_name(stat.m_filename), name) == 0) {
            return (int)i;
        }
    }
    return -1;
}

static int extract_index(mz_zip_archive *zip, int index, const char *dest) {
    FILE *out = ff_path_fopen(dest, "wb");
    mz_bool ok;
    if (!out) {
        return -1;
    }
    ok = mz_zip_reader_extract_to_cfile(zip, (mz_uint)index, out, 0);
    fclose(out);
    if (!ok) {
        ff_path_remove_file(dest);
        return -1;
    }
    return 0;
}

static int file_kind(const char *path) {
    unsigned char magic[16];
    FILE *file = ff_path_fopen(path, "rb");
    size_t n;
    if (!file) {
        return 0;
    }
    n = fread(magic, 1, sizeof(magic), file);
    fclose(file);
    if (n >= 4 && magic[0] == 0x28 && magic[1] == 0xB5 && magic[2] == 0x2F && magic[3] == 0xFD) {
        return 2;
    }
    if (n >= 15 && memcmp(magic, "SQLite format 3", 15) == 0) {
        return 1;
    }
    return 0;
}

static int decompress_zstd(const char *src, const char *dst, char *err, int err_len) {
    FILE *in = ff_path_fopen(src, "rb");
    FILE *out = ff_path_fopen(dst, "wb");
    ZSTD_DCtx *ctx = NULL;
    size_t in_size = ZSTD_DStreamInSize();
    size_t out_size = ZSTD_DStreamOutSize();
    unsigned char *in_buf = NULL;
    unsigned char *out_buf = NULL;
    size_t last = 0;
    int rc = -1;
    if (!in || !out) {
        set_err(err, err_len, "Could not open a temporary file while reading the deck.");
        goto done;
    }
    ctx = ZSTD_createDCtx();
    in_buf = (unsigned char *)malloc(in_size);
    out_buf = (unsigned char *)malloc(out_size);
    if (!ctx || !in_buf || !out_buf) {
        set_err(err, err_len, "Out of memory while decompressing the deck.");
        goto done;
    }
    while (1) {
        size_t got = fread(in_buf, 1, in_size, in);
        int last_chunk = got < in_size;
        ZSTD_inBuffer input = {in_buf, got, 0};
        while (input.pos < input.size) {
            ZSTD_outBuffer output = {out_buf, out_size, 0};
            last = ZSTD_decompressStream(ctx, &output, &input);
            if (ZSTD_isError(last)) {
                set_err(err, err_len, "This deck's collection could not be decompressed.");
                goto done;
            }
            if (output.pos && fwrite(output.dst, 1, output.pos, out) != output.pos) {
                set_err(err, err_len, "Could not write the temporary collection.");
                goto done;
            }
        }
        if (last_chunk) {
            if (last != 0) {
                set_err(err, err_len, "The deck collection is truncated.");
                goto done;
            }
            break;
        }
    }
    rc = 0;
done:
    free(in_buf);
    free(out_buf);
    ZSTD_freeDCtx(ctx);
    if (in) {
        fclose(in);
    }
    if (out) {
        fclose(out);
    }
    if (rc != 0) {
        ff_path_remove_file(dst);
    }
    return rc;
}

static int load_media_map(ImportCtx *ctx, char *err, int err_len) {
    int index;
    mz_zip_archive_file_stat stat;
    unsigned char *bytes = NULL;
    unsigned char *plain = NULL;
    size_t plain_len = 0;
    int rc = -1;
    index = find_named(ctx->zip, "media");
    if (index < 0) {
        return 0;
    }
    if (!mz_zip_reader_file_stat(ctx->zip, (mz_uint)index, &stat)) {
        set_err(err, err_len, "Could not read the deck media index.");
        return -1;
    }
    if (stat.m_uncomp_size > 16u * 1024u * 1024u) {
        set_err(err, err_len, "The media index in this deck is too large.");
        return -1;
    }
    if (stat.m_uncomp_size == 0) {
        return 0;
    }
    bytes = (unsigned char *)malloc((size_t)stat.m_uncomp_size + 1);
    if (!bytes) {
        set_err(err, err_len, "Out of memory.");
        return -1;
    }
    if (!mz_zip_reader_extract_to_mem(ctx->zip, (mz_uint)index, bytes, (size_t)stat.m_uncomp_size, 0)) {
        set_err(err, err_len, "Could not read the deck media index.");
        free(bytes);
        return -1;
    }
    bytes[stat.m_uncomp_size] = 0;
    plain = bytes;
    plain_len = (size_t)stat.m_uncomp_size;
    if (plain_len >= 4 && plain[0] == 0x28 && plain[1] == 0xB5 && plain[2] == 0x2F && plain[3] == 0xFD) {
        unsigned long long bound = ZSTD_getFrameContentSize(plain, plain_len);
        if (bound == ZSTD_CONTENTSIZE_ERROR || bound == ZSTD_CONTENTSIZE_UNKNOWN || bound > 16ull * 1024ull * 1024ull) {
            set_err(err, err_len, "The media index in this deck is too large.");
            free(bytes);
            return -1;
        }
        plain = (unsigned char *)malloc((size_t)bound + 1);
        if (!plain) {
            free(bytes);
            set_err(err, err_len, "Out of memory.");
            return -1;
        }
        plain_len = ZSTD_decompress(plain, (size_t)bound, bytes, (size_t)stat.m_uncomp_size);
        free(bytes);
        bytes = NULL;
        if (ZSTD_isError(plain_len)) {
            free(plain);
            set_err(err, err_len, "Could not decompress the media index.");
            return -1;
        }
        plain[plain_len] = 0;
    }
    if (plain[0] == '{' || plain[0] == ' ' || plain[0] == '\n' || plain[0] == '\t') {
        rc = parse_media_json(&ctx->map, (char *)plain);
    } else {
        rc = ff_pb_media_names(plain, plain_len, on_proto_name, &ctx->map);
    }
    free(plain);
    if (rc != 0) {
        set_err(err, err_len, "Could not understand the deck media index.");
    }
    return rc;
}

static void sanitize_name(const char *in, char *out, size_t out_len) {
    const char *base = base_name(in);
    size_t used = 0;
    if (out_len == 0) {
        return;
    }
    while (*base && used + 1 < out_len) {
        char ch = *base++;
        if ((unsigned char)ch < 32 || strchr("<>:\"|?*", ch)) {
            ch = '_';
        }
        out[used++] = ch;
    }
    out[used] = '\0';
    if (used == 0 || strcmp(out, ".") == 0 || strcmp(out, "..") == 0) {
        ff_copy_cap(out, out_len, "media.bin");
    }
}

static int copy_one_media(ImportCtx *ctx, int64_t deck_id, const char *anki_name, char *rel, size_t rel_len) {
    MediaNode *node;
    char safe[240];
    char dest[FF_PATH_MAX];
    char folder[FF_PATH_MAX];
    int index;
    FILE *existing;
    node = map_find(&ctx->map, anki_name);
    if (!node || !ctx->request->media_root) {
        return 1;
    }
    sanitize_name(anki_name, safe, sizeof(safe));
    snprintf(rel, rel_len, "%lld/%s", (long long)deck_id, safe);
    snprintf(folder, sizeof(folder), "%s\\%lld", ctx->request->media_root, (long long)deck_id);
    snprintf(dest, sizeof(dest), "%s\\%s", folder, safe);
    ff_path_mkdir(ctx->request->media_root);
    ff_path_mkdir(folder);
    existing = ff_path_fopen(dest, "rb");
    if (existing) {
        fclose(existing);
        return 0;
    }
    index = find_named(ctx->zip, node->zip_name);
    if (index < 0 || extract_index(ctx->zip, index, dest) != 0) {
        return 1;
    }
    return 0;
}

static char *localize_list(ImportCtx *ctx, int64_t deck_id, const char *list) {
    FfBuf buf;
    const char *cursor = list;
    if (!list || !list[0]) {
        return NULL;
    }
    if (ff_buf_init(&buf, 4096) != 0) {
        return NULL;
    }
    while (*cursor) {
        char name[512];
        char rel[512];
        const char *end = strchr(cursor, '\x1f');
        size_t len = end ? (size_t)(end - cursor) : strlen(cursor);
        if (len >= sizeof(name)) {
            len = sizeof(name) - 1;
        }
        memcpy(name, cursor, len);
        name[len] = '\0';
        if (copy_one_media(ctx, deck_id, name, rel, sizeof(rel)) == 0) {
            if (buf.len) {
                ff_buf_putc(&buf, '\x1f');
            }
            ff_buf_puts(&buf, rel);
        }
        if (!end) {
            break;
        }
        cursor = end + 1;
    }
    if (buf.len == 0) {
        ff_buf_free(&buf);
        return NULL;
    }
    return ff_buf_detach(&buf);
}

static int64_t ensure_deck(ImportCtx *ctx, const FfAnkiView *view) {
    int i;
    int64_t id = 0;
    for (i = 0; i < ctx->nlinks; i++) {
        if (ctx->links[i].anki == view->anki_deck_id) {
            return ctx->links[i].ours;
        }
    }
    if (ctx->nlinks >= 512) {
        return ctx->nlinks ? ctx->links[0].ours : 0;
    }
    if (ff_store_create_deck(ctx->request->store, view->deck_name, 0, &id) != 0) {
        return 0;
    }
    ctx->links[ctx->nlinks].anki = view->anki_deck_id;
    ctx->links[ctx->nlinks].ours = id;
    ctx->nlinks++;
    if (ctx->ncreated < 512) {
        ctx->created[ctx->ncreated++] = id;
    }
    ctx->result->decks++;
    return id;
}

static int on_scan(const FfAnkiView *view, void *user) {
    FfMediaScan *scan = (FfMediaScan *)user;
    const char *kind;
    if (!view->has_image && !view->has_audio) {
        return 0;
    }
    if (view->has_image) {
        scan->image_cards++;
    }
    if (view->has_audio) {
        scan->audio_cards++;
    }
    if (scan->preview_count >= 80) {
        return 0;
    }
    kind = (view->has_image && view->has_audio) ? "image, audio" : (view->has_image ? "image" : "audio");
    snprintf(scan->preview[scan->preview_count], 180, "%s - %s", view->preview[0] ? view->preview : "(no text)", kind);
    scan->preview_count++;
    return 0;
}

static int on_write(const FfAnkiView *view, void *user) {
    ImportCtx *ctx = (ImportCtx *)user;
    FfInsertCard card;
    int64_t deck_id;
    char *image_front;
    char *image_back;
    char *audio_front;
    char *audio_back;
    int rc;
    if (!view->front.text && !view->back.text && !view->has_image && !view->has_audio) {
        ctx->result->skipped++;
        return 0;
    }
    if (view->front.text && !view->front.text[0] && view->back.text && !view->back.text[0] && !view->has_image && !view->has_audio) {
        ctx->result->skipped++;
        return 0;
    }
    deck_id = ensure_deck(ctx, view);
    if (!deck_id) {
        return -1;
    }
    if (!ctx->map_ready && (view->has_image || view->has_audio)) {
        if (load_media_map(ctx, NULL, 0) != 0) {
            return -1;
        }
        ctx->map_ready = 1;
    }
    image_front = localize_list(ctx, deck_id, view->front.image);
    image_back = localize_list(ctx, deck_id, view->back.image);
    audio_front = localize_list(ctx, deck_id, view->front.audio);
    audio_back = localize_list(ctx, deck_id, view->back.audio);
    if (view->has_image || view->has_audio) {
        ff_store_set_deck_heavy(ctx->request->store, deck_id, 1);
    }
    memset(&card, 0, sizeof(card));
    card.deck_id = deck_id;
    card.ord = view->ord;
    card.suspended = view->suspended;
    card.source_id = view->source_id;
    card.sched = view->sched;
    card.front = view->front.text ? view->front.text : "";
    card.back = view->back.text ? view->back.text : "";
    card.image_front = image_front;
    card.image_back = image_back;
    card.audio_front = audio_front;
    card.audio_back = audio_back;
    card.has_media = (image_front || image_back || audio_front || audio_back) ? 1 : 0;
    rc = ff_store_insert_card(ctx->request->store, &card);
    free(image_front);
    free(image_back);
    free(audio_front);
    free(audio_back);
    if (rc != 0) {
        return -1;
    }
    ctx->result->cards++;
    if (ctx->request->progress && (ctx->result->cards % 50) == 0) {
        ctx->request->progress(ctx->result->cards, ctx->request->progress_user);
    }
    return 0;
}

static void remove_created_media(ImportCtx *ctx) {
    int i;
    if (!ctx->request->media_root) {
        return;
    }
    for (i = 0; i < ctx->ncreated; i++) {
        char folder[FF_PATH_MAX];
        snprintf(folder, sizeof(folder), "%s\\%lld", ctx->request->media_root, (long long)ctx->created[i]);
        ff_path_remove_children(folder);
    }
}

int ff_import_apkg(const FfImportRequest *request, FfImportResult *result, char *err, int err_len) {
    mz_zip_archive zip;
    FILE *package = NULL;
    int64_t package_size;
    int best_index = -1;
    int best_rank = 0;
    mz_uint file_count;
    mz_uint i;
    char extracted[FF_PATH_MAX];
    char decoded[FF_PATH_MAX];
    char leaf[64];
    int kind;
    FfAnki *anki = NULL;
    FfMediaScan scan;
    ImportCtx ctx;
    int rows = 0;
    int stubs = 0;
    int rc = -1;
    const char *sqlite_path;
    if (result) {
        memset(result, 0, sizeof(*result));
    }
    if (!request || !request->store || !request->apkg_path || !result) {
        set_err(err, err_len, "Missing deck file.");
        return -1;
    }
    mz_zip_zero_struct(&zip);
    memset(&ctx, 0, sizeof(ctx));
    memset(&scan, 0, sizeof(scan));
    extracted[0] = '\0';
    decoded[0] = '\0';
    ctx.request = request;
    ctx.result = result;
    ctx.zip = &zip;
    package = ff_path_fopen(request->apkg_path, "rb");
    if (!package) {
        set_err(err, err_len, "Could not open that .apkg file.");
        return -1;
    }
    package_size = zip_size(package);
    zip_rewind(package);
    if (package_size < 22 || !mz_zip_reader_init_cfile(&zip, package, (mz_uint64)package_size, 0)) {
        set_err(err, err_len, "That file is not a readable Anki deck package.");
        fclose(package);
        return -1;
    }
    file_count = mz_zip_reader_get_num_files(&zip);
    for (i = 0; i < file_count; i++) {
        mz_zip_archive_file_stat stat;
        int rank;
        if (!mz_zip_reader_file_stat(&zip, i, &stat)) {
            continue;
        }
        rank = collection_rank(stat.m_filename);
        if (rank > best_rank) {
            best_rank = rank;
            best_index = (int)i;
        }
    }
    if (best_index < 0) {
        set_err(err, err_len, "This package has no Anki collection.");
        goto cleanup;
    }
    snprintf(leaf, sizeof(leaf), "ff-%lld-extract.bin", (long long)time(NULL));
    if (ff_path_temp(extracted, sizeof(extracted), leaf) != 0 || extract_index(&zip, best_index, extracted) != 0) {
        set_err(err, err_len, "Could not extract the deck collection.");
        goto cleanup;
    }
    kind = file_kind(extracted);
    sqlite_path = extracted;
    if (kind == 2) {
        snprintf(leaf, sizeof(leaf), "ff-%lld-collection.sqlite", (long long)time(NULL));
        if (ff_path_temp(decoded, sizeof(decoded), leaf) != 0 || decompress_zstd(extracted, decoded, err, err_len) != 0) {
            goto cleanup;
        }
        sqlite_path = decoded;
        kind = file_kind(decoded);
    }
    if (kind != 1) {
        set_err(err, err_len, "Flash Frame could not read this deck's collection.");
        goto cleanup;
    }
    if (ff_anki_open(&anki, sqlite_path, err, err_len) != 0) {
        goto cleanup;
    }
    if (ff_anki_each(anki, 0, on_scan, &scan, &rows, &stubs, err, err_len) != 0) {
        goto cleanup;
    }
    if (rows == 0 && stubs > 0) {
        set_err(err, err_len, "This package only contains Anki's compatibility stub. Export it again from a current Anki.");
        goto cleanup;
    }
    if (rows == 0) {
        set_err(err, err_len, "This deck has no cards.");
        goto cleanup;
    }
    result->image_cards = scan.image_cards;
    result->audio_cards = scan.audio_cards;
    if ((scan.image_cards || scan.audio_cards) && !request->skip_warn) {
        FfWarnChoice choice;
        memset(&choice, 0, sizeof(choice));
        if (!request->warn) {
            set_err(err, err_len, "This deck contains media and needs confirmation.");
            goto cleanup;
        }
        request->warn(&scan, request->warn_user, &choice);
        if (!choice.load) {
            result->cancelled = 1;
            rc = 0;
            goto cleanup;
        }
        result->dont_show_again = choice.dont_show_again;
    }
    if (ff_store_begin(request->store) != 0) {
        set_err(err, err_len, "Could not start saving the deck.");
        goto cleanup;
    }
    if (ff_anki_each(anki, 1, on_write, &ctx, NULL, NULL, err, err_len) != 0) {
        ff_store_rollback(request->store);
        remove_created_media(&ctx);
        if (err && err_len > 0 && !err[0]) {
            set_err(err, err_len, "Import stopped before the deck was saved.");
        }
        goto cleanup;
    }
    if (ff_store_commit(request->store) != 0) {
        ff_store_rollback(request->store);
        remove_created_media(&ctx);
        set_err(err, err_len, "Could not finish saving the deck.");
        goto cleanup;
    }
    if (request->progress) {
        request->progress(result->cards, request->progress_user);
    }
    rc = 0;
cleanup:
    map_free(&ctx.map);
    ff_anki_close(anki);
    mz_zip_reader_end(&zip);
    if (package) {
        fclose(package);
    }
    if (extracted[0]) {
        ff_path_remove_file(extracted);
    }
    if (decoded[0]) {
        ff_path_remove_file(decoded);
    }
    return rc;
}
