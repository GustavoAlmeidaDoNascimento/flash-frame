#include "budget.h"

unsigned long long ff_budget_limit(int heavy) {
    return heavy ? (unsigned long long)FF_BUDGET_HEAVY : (unsigned long long)FF_BUDGET_TEXT;
}

int ff_budget_over(int heavy, unsigned long long working_set) {
    return working_set > ff_budget_limit(heavy);
}

int ff_budget_allow_image(int heavy, unsigned long long working_set) {
    unsigned long long headroom = 2ull * 1024ull * 1024ull;
    if (!heavy) {
        return 0;
    }
    if (working_set > ff_budget_limit(1)) {
        return 0;
    }
    return working_set + headroom < ff_budget_limit(1);
}

long long ff_budget_sqlite_heap(int heavy) {
    return heavy ? (32ll * 1024ll * 1024ll) : (2ll * 1024ll * 1024ll);
}
