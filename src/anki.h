#ifndef FF_ANKI_H
#define FF_ANKI_H

#include "scheduler.h"
#include "textutil.h"

#include <stdint.h>

typedef struct FfAnki FfAnki;

typedef struct FfAnkiView {
    int64_t anki_deck_id;
    int64_t source_id;
    char deck_name[256];
    char preview[160];
    int has_image;
    int has_audio;
    int suspended;
    int ord;
    FfCardSched sched;
    int rendered;
    FfRendered front;
    FfRendered back;
} FfAnkiView;

typedef int (*FfAnkiFn)(const FfAnkiView *view, void *user);

int ff_anki_open(FfAnki **out, const char *utf8_path, char *err, int err_len);
void ff_anki_close(FfAnki *anki);
int ff_anki_each(
    FfAnki *anki,
    int render,
    FfAnkiFn fn,
    void *user,
    int *rows,
    int *stubs,
    char *err,
    int err_len);

#endif
