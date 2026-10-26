#ifndef FF_STORAGE_H
#define FF_STORAGE_H

#include "scheduler.h"

#include <stdint.h>

typedef struct FfStore FfStore;

typedef struct FfDeckInfo {
    int64_t id;
    char name[256];
    int new_per_day;
    int reviews_per_day;
    int heavy;
    int learn_due;
    int review_due;
    int new_waiting;
} FfDeckInfo;

typedef struct FfStudyCard {
    int64_t id;
    int64_t deck_id;
    int suspended;
    FfCardSched sched;
    char *front;
    char *back;
    char *image_front;
    char *image_back;
    char *audio_front;
    char *audio_back;
} FfStudyCard;

typedef struct FfInsertCard {
    int64_t deck_id;
    int ord;
    int suspended;
    int64_t source_id;
    FfCardSched sched;
    const char *front;
    const char *back;
    const char *image_front;
    const char *image_back;
    const char *audio_front;
    const char *audio_back;
    int has_media;
} FfInsertCard;

int ff_store_open(FfStore **out, const char *utf8_path, char *err, int err_len);
void ff_store_close(FfStore *store);
void ff_store_set_heap_limit(int heavy);
int ff_store_release_memory(FfStore *store);

int ff_store_meta_get(FfStore *store, const char *key, char *buf, int buf_len);
int ff_store_meta_set(FfStore *store, const char *key, const char *value);

int ff_store_deck_list(FfStore *store, FfDeckInfo **out, int *count, int64_t now);
void ff_store_deck_list_free(FfDeckInfo *decks);

int ff_store_create_deck(FfStore *store, const char *name, int heavy, int64_t *id);
int ff_store_set_deck_heavy(FfStore *store, int64_t id, int heavy);

int ff_store_begin(FfStore *store);
int ff_store_commit(FfStore *store);
int ff_store_rollback(FfStore *store);
int ff_store_insert_card(FfStore *store, const FfInsertCard *card);

int ff_store_next_card(FfStore *store, int64_t deck_id, int64_t now, const char *day, FfStudyCard **out);
void ff_study_card_free(FfStudyCard *card);
int ff_store_save_answer(
    FfStore *store,
    const FfStudyCard *card,
    int rating,
    int state_before,
    int64_t interval_before,
    int64_t now,
    const char *day);

#endif
