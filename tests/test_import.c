#include "apkg.h"
#include "path.h"
#include "scheduler.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_failed = 0;

#define EXPECT(cond)                                                       \
    do {                                                                   \
        if (!(cond)) {                                                     \
            printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);         \
            g_failed++;                                                    \
        }                                                                  \
    } while (0)

static int warn_reject(const FfMediaScan *scan, void *user, FfWarnChoice *choice) {
    (void)scan;
    (void)user;
    choice->load = 0;
    g_failed++;
    printf("FAIL unexpected media warning\n");
    return 0;
}

static int warn_accept(const FfMediaScan *scan, void *user, FfWarnChoice *choice) {
    int *images = (int *)user;
    *images = scan->image_cards;
    choice->load = 1;
    choice->dont_show_again = 0;
    EXPECT(scan->image_cards >= 1);
    EXPECT(scan->audio_cards >= 1);
    EXPECT(scan->preview_count >= 1);
    return 0;
}

static void remove_db(const char *path) {
    char wal[512];
    char shm[512];
    ff_path_remove_file(path);
    snprintf(wal, sizeof(wal), "%s-wal", path);
    snprintf(shm, sizeof(shm), "%s-shm", path);
    ff_path_remove_file(wal);
    ff_path_remove_file(shm);
}

static int import_one(const char *apkg, const char *db, const char *media, FfWarnFn warn, void *user, FfImportResult *result) {
    FfStore *store = NULL;
    FfImportRequest request;
    char err[256];
    err[0] = '\0';
    remove_db(db);
    if (ff_store_open(&store, db, err, (int)sizeof(err)) != 0) {
        printf("open failed: %s\n", err);
        return -1;
    }
    memset(&request, 0, sizeof(request));
    request.store = store;
    request.apkg_path = apkg;
    request.media_root = media;
    request.warn = warn;
    request.warn_user = user;
    if (ff_import_apkg(&request, result, err, (int)sizeof(err)) != 0) {
        printf("import failed: %s\n", err);
        ff_store_close(store);
        return -1;
    }
    ff_store_close(store);
    return 0;
}

static void test_text(void) {
    FfImportResult result;
    FfStore *store = NULL;
    FfStudyCard *card = NULL;
    FfSchedConfig cfg;
    char err[128];
    int64_t now = 1700000000;
    char day[] = "2026-10-26";
    EXPECT(import_one("build/fixtures/text.apkg", "build/fixtures/text.db", "build/fixtures/text-media", warn_reject, NULL, &result) == 0);
    EXPECT(result.cancelled == 0);
    EXPECT(result.cards == 1);
    EXPECT(result.image_cards == 0);
    EXPECT(ff_store_open(&store, "build/fixtures/text.db", err, (int)sizeof(err)) == 0);
    EXPECT(ff_store_next_card(store, 1, now, day, &card) == 0);
    EXPECT(card != NULL);
    if (card) {
        EXPECT(strcmp(card->front, "Hello") == 0);
        EXPECT(strcmp(card->back, "Hello\nWorld") == 0);
        ff_sched_config_default(&cfg);
        cfg.fuzz = 0;
        ff_sched_answer(&cfg, &card->sched, FF_GOOD, now);
        EXPECT(ff_store_save_answer(store, card, FF_GOOD, FF_STATE_NEW, 0, now, day) == 0);
        ff_study_card_free(card);
        card = NULL;
        EXPECT(ff_store_next_card(store, 1, now + 600, day, &card) == 0);
        EXPECT(card && card->sched.state == FF_STATE_LEARNING);
        ff_study_card_free(card);
    }
    ff_store_close(store);
}

static void test_media(void) {
    FfImportResult result;
    FfStore *store = NULL;
    FfStudyCard *card = NULL;
    int images = 0;
    char err[128];
    FILE *png;
    EXPECT(import_one("build/fixtures/media.apkg", "build/fixtures/media.db", "build/fixtures/media-root", warn_accept, &images, &result) == 0);
    EXPECT(images >= 1);
    EXPECT(result.cards == 1);
    EXPECT(ff_store_open(&store, "build/fixtures/media.db", err, (int)sizeof(err)) == 0);
    EXPECT(ff_store_next_card(store, 1, 1700000000, "2026-10-26", &card) == 0);
    EXPECT(card && strstr(card->front, "See"));
    EXPECT(card && card->image_front && strstr(card->image_front, "pic.png"));
    EXPECT(card && card->audio_back && strstr(card->audio_back, "a.mp3"));
    png = ff_path_fopen("build/fixtures/media-root\\1\\pic.png", "rb");
    EXPECT(png != NULL);
    if (png) {
        fclose(png);
    }
    ff_study_card_free(card);
    ff_store_close(store);
}

static void test_modern(void) {
    FfImportResult result;
    FfStore *store = NULL;
    FfStudyCard *card = NULL;
    char err[256];
    EXPECT(import_one("build/fixtures/modern.apkg", "build/fixtures/modern.db", "build/fixtures/modern-media", warn_reject, NULL, &result) == 0);
    EXPECT(result.cards == 1);
    EXPECT(ff_store_open(&store, "build/fixtures/modern.db", err, (int)sizeof(err)) == 0);
    EXPECT(ff_store_next_card(store, 1, 1700000000, "2026-10-26", &card) == 0);
    EXPECT(card && strcmp(card->front, "The [...] of France") == 0);
    EXPECT(card && strcmp(card->back, "The capital of France") == 0);
    ff_study_card_free(card);
    ff_store_close(store);
}

int main(void) {
    int rc = system("py tests\\make_fixture.py build\\fixtures");
    if (rc != 0) {
        printf("fixture script failed\n");
        return 1;
    }
    test_text();
    test_media();
    test_modern();
    if (g_failed) {
        printf("%d import test(s) failed\n", g_failed);
        return 1;
    }
    printf("all import tests passed\n");
    return 0;
}
