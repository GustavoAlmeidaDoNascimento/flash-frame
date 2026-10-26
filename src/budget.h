#ifndef FF_BUDGET_H
#define FF_BUDGET_H

#include <stdint.h>

enum {
    FF_BUDGET_TEXT = 20 * 1024 * 1024,
    FF_BUDGET_HEAVY = 256 * 1024 * 1024
};

unsigned long long ff_budget_limit(int heavy);
int ff_budget_over(int heavy, unsigned long long working_set);
int ff_budget_allow_image(int heavy, unsigned long long working_set);
long long ff_budget_sqlite_heap(int heavy);

#endif
