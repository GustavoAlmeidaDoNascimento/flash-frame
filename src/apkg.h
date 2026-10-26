#ifndef FF_APKG_H
#define FF_APKG_H

#include "storage.h"

typedef struct FfMediaScan {
    int image_cards;
    int audio_cards;
    int preview_count;
    char preview[80][180];
} FfMediaScan;

typedef struct FfWarnChoice {
    int load;
    int dont_show_again;
} FfWarnChoice;

typedef int (*FfWarnFn)(const FfMediaScan *scan, void *user, FfWarnChoice *choice);
typedef void (*FfProgressFn)(int cards, void *user);

typedef struct FfImportRequest {
    FfStore *store;
    const char *apkg_path;
    const char *media_root;
    int skip_warn;
    FfWarnFn warn;
    void *warn_user;
    FfProgressFn progress;
    void *progress_user;
} FfImportRequest;

typedef struct FfImportResult {
    int cancelled;
    int cards;
    int decks;
    int image_cards;
    int audio_cards;
    int skipped;
    int dont_show_again;
} FfImportResult;

int ff_import_apkg(const FfImportRequest *request, FfImportResult *result, char *err, int err_len);

#endif
