#ifndef FF_SCHEDULER_H
#define FF_SCHEDULER_H

#include <stddef.h>
#include <stdint.h>

enum {
    FF_STATE_NEW = 0,
    FF_STATE_LEARNING = 1,
    FF_STATE_REVIEW = 2,
    FF_STATE_RELEARNING = 3
};

enum {
    FF_AGAIN = 1,
    FF_HARD = 2,
    FF_GOOD = 3,
    FF_EASY = 4
};

/* Anki's default SM-2 deck options. Ease is stored the way Anki stores it:
   2500 means a factor of 2.50. */
typedef struct FfSchedConfig {
    int learn_secs[8];
    int learn_count;
    int relearn_secs[8];
    int relearn_count;
    int64_t graduating_secs;
    int64_t easy_secs;
    int starting_ease;
    int min_ease;
    int hard_permille;
    int easy_bonus_permille;
    int interval_modifier_permille;
    int lapse_new_permille;
    int64_t lapse_min_secs;
    int64_t max_secs;
    int fuzz;
} FfSchedConfig;

typedef struct FfCardSched {
    int state;
    int step;
    int ease;
    int reps;
    int lapses;
    int64_t interval_secs;
    int64_t due;
} FfCardSched;

typedef struct FfDelays {
    int64_t again_secs;
    int64_t hard_secs;
    int64_t good_secs;
    int64_t easy_secs;
} FfDelays;

void ff_sched_config_default(FfSchedConfig *cfg);
void ff_sched_new_card(const FfSchedConfig *cfg, FfCardSched *card);
void ff_sched_answer(const FfSchedConfig *cfg, FfCardSched *card, int rating, int64_t now);
void ff_sched_preview(const FfSchedConfig *cfg, const FfCardSched *card, int64_t now, FfDelays *out);
void ff_format_span(int64_t secs, char *dst, size_t dst_len);

#endif
