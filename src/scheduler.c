#include "scheduler.h"

#include <stdio.h>
#include <string.h>

void ff_sched_config_default(FfSchedConfig *cfg) {
    memset(cfg, 0, sizeof(*cfg));
    cfg->learn_secs[0] = 60;
    cfg->learn_secs[1] = 600;
    cfg->learn_count = 2;
    cfg->relearn_secs[0] = 600;
    cfg->relearn_count = 1;
    cfg->graduating_secs = 86400;
    cfg->easy_secs = 4 * 86400;
    cfg->starting_ease = 2500;
    cfg->min_ease = 1300;
    cfg->hard_permille = 1200;
    cfg->easy_bonus_permille = 1300;
    cfg->interval_modifier_permille = 1000;
    cfg->lapse_new_permille = 0;
    cfg->lapse_min_secs = 86400;
    cfg->max_secs = 36500ll * 86400ll;
    cfg->learn_ahead_secs = 20 * 60;
    cfg->fuzz = 1;
}

void ff_sched_new_card(const FfSchedConfig *cfg, FfCardSched *card) {
    memset(card, 0, sizeof(*card));
    card->state = FF_STATE_NEW;
    card->ease = cfg->starting_ease;
    card->step = 0;
}

static int max_days(const FfSchedConfig *cfg) {
    int64_t days = cfg->max_secs / 86400;
    if (days < 1) {
        return 1;
    }
    if (days > 200000) {
        return 200000;
    }
    return (int)days;
}

static int fuzz_days(int ivl, uint32_t salt) {
    int lo;
    int hi;
    int span;
    int fuzz;
    if (ivl < 2) {
        return ivl < 1 ? 1 : ivl;
    }
    if (ivl == 2) {
        lo = 2;
        hi = 3;
    } else {
        if (ivl < 7) {
            fuzz = ivl / 4;
        } else if (ivl < 30) {
            fuzz = (ivl * 15) / 100;
            if (fuzz < 2) {
                fuzz = 2;
            }
        } else {
            fuzz = ivl / 20;
            if (fuzz < 3) {
                fuzz = 3;
            }
        }
        lo = ivl - fuzz;
        if (lo < 2) {
            lo = 2;
        }
        hi = ivl + fuzz;
    }
    span = hi - lo + 1;
    if (span < 1) {
        return ivl;
    }
    return lo + (int)(salt % (uint32_t)span);
}

static int constrain_days(long long base, int prev_days, int cap_days, int modifier_permille, int fuzz, uint32_t salt) {
    long long ivl = base;
    if (modifier_permille <= 0) {
        modifier_permille = 1000;
    }
    ivl = ivl * modifier_permille / 1000;
    if (ivl < 1) {
        ivl = 1;
    }
    if (ivl > 200000) {
        ivl = 200000;
    }
    if (fuzz) {
        ivl = fuzz_days((int)ivl, salt);
    }
    if (ivl < (long long)prev_days + 1) {
        ivl = (long long)prev_days + 1;
    }
    if (cap_days < 1) {
        cap_days = 1;
    }
    if (ivl > cap_days) {
        ivl = cap_days;
    }
    return (int)ivl;
}

static uint32_t review_salt(const FfCardSched *card, int rating) {
    return (uint32_t)card->reps * 977u ^ (uint32_t)card->interval_secs ^ (uint32_t)rating * 13u;
}

static int step_delay(const int *steps, int count, int step, int hard) {
    if (count <= 0) {
        return 60;
    }
    if (step < 0) {
        step = 0;
    }
    if (step >= count) {
        step = count - 1;
    }
    if (hard && step == 0 && count >= 2) {
        return (steps[0] + steps[1]) / 2;
    }
    return steps[step];
}

static void graduate_new(FfCardSched *card, int64_t now, int64_t interval) {
    if (interval < 86400) {
        interval = 86400;
    }
    card->state = FF_STATE_REVIEW;
    card->step = 0;
    card->interval_secs = interval;
    card->due = now + interval;
}

static void answer_learning(const FfSchedConfig *cfg, FfCardSched *card, int rating, int64_t now, int relearn) {
    const int *steps = relearn ? cfg->relearn_secs : cfg->learn_secs;
    int count = relearn ? cfg->relearn_count : cfg->learn_count;
    int step = card->step;
    if (count > 8) {
        count = 8;
    }
    if (count < 1) {
        count = 1;
    }
    if (step < 0) {
        step = 0;
    }
    if (step >= count) {
        step = count - 1;
    }

    if (rating == FF_AGAIN) {
        card->state = relearn ? FF_STATE_RELEARNING : FF_STATE_LEARNING;
        card->step = 0;
        card->due = now + step_delay(steps, count, 0, 0);
        return;
    }
    if (rating == FF_HARD) {
        card->state = relearn ? FF_STATE_RELEARNING : FF_STATE_LEARNING;
        card->step = step;
        card->due = now + step_delay(steps, count, step, 1);
        return;
    }
    if (rating == FF_EASY) {
        graduate_new(card, now, cfg->easy_secs);
        return;
    }

    if (step + 1 < count) {
        card->state = relearn ? FF_STATE_RELEARNING : FF_STATE_LEARNING;
        card->step = step + 1;
        card->due = now + steps[card->step];
        return;
    }

    if (relearn) {
        int64_t ivl = card->interval_secs;
        if (ivl < cfg->lapse_min_secs) {
            ivl = cfg->lapse_min_secs;
        }
        card->state = FF_STATE_REVIEW;
        card->step = 0;
        card->interval_secs = ivl;
        card->due = now + ivl;
        return;
    }
    graduate_new(card, now, cfg->graduating_secs);
}

static void answer_review(const FfSchedConfig *cfg, FfCardSched *card, int rating, int64_t now) {
    int days = (int)(card->interval_secs / 86400);
    int cap = max_days(cfg);
    int delay_days = 0;
    int hard;
    int good;
    long long easy_num;
    int easy;
    if (days < 1) {
        days = 1;
    }
    if (now > card->due) {
        delay_days = (int)((now - card->due) / 86400);
    }

    hard = constrain_days(
        (long long)days * cfg->hard_permille / 1000,
        days,
        cap,
        cfg->interval_modifier_permille,
        cfg->fuzz,
        review_salt(card, FF_HARD));
    good = constrain_days(
        (long long)(days + delay_days / 2) * card->ease / 1000,
        hard,
        cap,
        cfg->interval_modifier_permille,
        cfg->fuzz,
        review_salt(card, FF_GOOD));
    easy_num = (long long)(days + delay_days) * card->ease * cfg->easy_bonus_permille;
    easy = constrain_days(
        easy_num / 1000000ll,
        good,
        cap,
        cfg->interval_modifier_permille,
        cfg->fuzz,
        review_salt(card, FF_EASY));

    if (rating == FF_AGAIN) {
        long long lapse = (long long)days * cfg->lapse_new_permille / 1000;
        int64_t min_days = cfg->lapse_min_secs / 86400;
        if (min_days < 1) {
            min_days = 1;
        }
        if (lapse < min_days) {
            lapse = min_days;
        }
        card->ease -= 200;
        if (card->ease < cfg->min_ease) {
            card->ease = cfg->min_ease;
        }
        card->lapses += 1;
        card->interval_secs = lapse * 86400;
        card->state = FF_STATE_RELEARNING;
        card->step = 0;
        card->due = now + step_delay(cfg->relearn_secs, cfg->relearn_count, 0, 0);
        return;
    }

    if (rating == FF_HARD) {
        card->ease -= 150;
        if (card->ease < cfg->min_ease) {
            card->ease = cfg->min_ease;
        }
        card->interval_secs = (int64_t)hard * 86400;
    } else if (rating == FF_EASY) {
        card->ease += 150;
        card->interval_secs = (int64_t)easy * 86400;
    } else {
        card->interval_secs = (int64_t)good * 86400;
    }
    card->state = FF_STATE_REVIEW;
    card->step = 0;
    card->due = now + card->interval_secs;
}

void ff_sched_answer(const FfSchedConfig *cfg, FfCardSched *card, int rating, int64_t now) {
    int state;
    if (rating < FF_AGAIN || rating > FF_EASY) {
        rating = FF_GOOD;
    }
    state = card->state;
    card->reps += 1;
    if (state == FF_STATE_REVIEW) {
        answer_review(cfg, card, rating, now);
        return;
    }
    answer_learning(cfg, card, rating, now, state == FF_STATE_RELEARNING);
}

void ff_sched_preview(const FfSchedConfig *cfg, const FfCardSched *card, int64_t now, FfDelays *out) {
    FfCardSched copy;
    int64_t *slots[4];
    int rating;
    slots[0] = &out->again_secs;
    slots[1] = &out->hard_secs;
    slots[2] = &out->good_secs;
    slots[3] = &out->easy_secs;
    for (rating = FF_AGAIN; rating <= FF_EASY; rating++) {
        copy = *card;
        ff_sched_answer(cfg, &copy, rating, now);
        *slots[rating - 1] = copy.due - now;
        if (*slots[rating - 1] < 0) {
            *slots[rating - 1] = 0;
        }
    }
}

void ff_format_span(int64_t secs, char *dst, size_t dst_len) {
    if (!dst || dst_len == 0) {
        return;
    }
    if (secs < 0) {
        secs = 0;
    }
    if (secs < 60) {
        snprintf(dst, dst_len, "%ds", (int)secs);
        return;
    }
    if (secs < 3600) {
        snprintf(dst, dst_len, "%dm", (int)((secs + 30) / 60));
        return;
    }
    if (secs < 86400) {
        snprintf(dst, dst_len, "%dh", (int)((secs + 1800) / 3600));
        return;
    }
    if (secs < 30ll * 86400ll) {
        snprintf(dst, dst_len, "%dd", (int)((secs + 43200) / 86400));
        return;
    }
    if (secs < 365ll * 86400ll) {
        snprintf(dst, dst_len, "%.1fmo", (double)secs / (30.0 * 86400.0));
        return;
    }
    snprintf(dst, dst_len, "%.1fy", (double)secs / (365.0 * 86400.0));
}
