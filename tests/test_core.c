#include "budget.h"
#include "proto.h"
#include "scheduler.h"
#include "textutil.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_failed = 0;

#define EXPECT(cond)                                                                 \
    do {                                                                             \
        if (!(cond)) {                                                               \
            printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);                   \
            g_failed++;                                                              \
        }                                                                            \
    } while (0)

static FfSchedConfig cfg_plain(void) {
    FfSchedConfig cfg;
    ff_sched_config_default(&cfg);
    cfg.fuzz = 0;
    return cfg;
}

static void test_scheduler(void) {
    FfSchedConfig cfg = cfg_plain();
    FfCardSched card;
    FfDelays delays;
    const int64_t now = 1700000000;
    char label[32];

    ff_sched_new_card(&cfg, &card);
    ff_sched_answer(&cfg, &card, FF_GOOD, now);
    EXPECT(card.state == FF_STATE_LEARNING);
    EXPECT(card.step == 1);
    EXPECT(card.due == now + 600);
    EXPECT(card.ease == 2500);
    EXPECT(card.reps == 1);

    ff_sched_new_card(&cfg, &card);
    ff_sched_answer(&cfg, &card, FF_AGAIN, now);
    EXPECT(card.state == FF_STATE_LEARNING);
    EXPECT(card.step == 0);
    EXPECT(card.due == now + 60);
    EXPECT(card.ease == 2500);

    ff_sched_new_card(&cfg, &card);
    ff_sched_answer(&cfg, &card, FF_HARD, now);
    EXPECT(card.state == FF_STATE_LEARNING);
    EXPECT(card.due == now + 330);

    ff_sched_new_card(&cfg, &card);
    ff_sched_answer(&cfg, &card, FF_EASY, now);
    EXPECT(card.state == FF_STATE_REVIEW);
    EXPECT(card.interval_secs == 4 * 86400);
    EXPECT(card.due == now + 4 * 86400);

    ff_sched_new_card(&cfg, &card);
    ff_sched_answer(&cfg, &card, FF_GOOD, now);
    ff_sched_answer(&cfg, &card, FF_GOOD, now + 600);
    EXPECT(card.state == FF_STATE_REVIEW);
    EXPECT(card.interval_secs == 86400);

    ff_sched_new_card(&cfg, &card);
    ff_sched_answer(&cfg, &card, FF_AGAIN, now);
    ff_sched_answer(&cfg, &card, FF_GOOD, now + 60);
    EXPECT(card.state == FF_STATE_LEARNING);
    EXPECT(card.step == 1);
    EXPECT(card.due == now + 60 + 600);

    card.state = FF_STATE_REVIEW;
    card.step = 0;
    card.ease = 2500;
    card.reps = 3;
    card.lapses = 0;
    card.interval_secs = 10 * 86400;
    card.due = now;
    ff_sched_answer(&cfg, &card, FF_GOOD, now);
    EXPECT(card.state == FF_STATE_REVIEW);
    EXPECT(card.interval_secs == 25 * 86400);
    EXPECT(card.ease == 2500);

    card.state = FF_STATE_REVIEW;
    card.ease = 2500;
    card.interval_secs = 10 * 86400;
    card.due = now;
    ff_sched_answer(&cfg, &card, FF_HARD, now);
    EXPECT(card.interval_secs == 12 * 86400);
    EXPECT(card.ease == 2350);

    card.state = FF_STATE_REVIEW;
    card.ease = 2500;
    card.interval_secs = 10 * 86400;
    card.due = now;
    ff_sched_answer(&cfg, &card, FF_EASY, now);
    EXPECT(card.interval_secs == 32 * 86400);
    EXPECT(card.ease == 2650);

    card.state = FF_STATE_REVIEW;
    card.ease = 2500;
    card.reps = 4;
    card.lapses = 0;
    card.interval_secs = 10 * 86400;
    card.due = now;
    ff_sched_answer(&cfg, &card, FF_AGAIN, now);
    EXPECT(card.state == FF_STATE_RELEARNING);
    EXPECT(card.ease == 2300);
    EXPECT(card.lapses == 1);
    EXPECT(card.interval_secs == 86400);
    EXPECT(card.due == now + 600);

    ff_sched_answer(&cfg, &card, FF_GOOD, now + 600);
    EXPECT(card.state == FF_STATE_REVIEW);
    EXPECT(card.interval_secs == 86400);
    EXPECT(card.ease == 2300);

    card.state = FF_STATE_REVIEW;
    card.ease = 2500;
    card.interval_secs = 10 * 86400;
    card.due = now;
    card.lapses = 0;
    ff_sched_answer(&cfg, &card, FF_AGAIN, now);
    ff_sched_answer(&cfg, &card, FF_EASY, now);
    EXPECT(card.state == FF_STATE_REVIEW);
    EXPECT(card.interval_secs == 4 * 86400);

    card.state = FF_STATE_REVIEW;
    card.ease = 1400;
    card.interval_secs = 5 * 86400;
    card.due = now;
    ff_sched_answer(&cfg, &card, FF_AGAIN, now);
    EXPECT(card.ease == 1300);

    card.ease = 1300;
    card.state = FF_STATE_REVIEW;
    card.interval_secs = 5 * 86400;
    card.due = now;
    ff_sched_answer(&cfg, &card, FF_AGAIN, now);
    EXPECT(card.ease == 1300);

    card.state = FF_STATE_REVIEW;
    card.ease = 2500;
    card.interval_secs = 10 * 86400;
    card.due = now - 3 * 86400;
    ff_sched_answer(&cfg, &card, FF_GOOD, now);
    EXPECT(card.interval_secs == 27 * 86400);

    cfg.max_secs = 10 * 86400;
    card.state = FF_STATE_REVIEW;
    card.ease = 2500;
    card.interval_secs = 9 * 86400;
    card.due = now;
    ff_sched_answer(&cfg, &card, FF_GOOD, now);
    EXPECT(card.interval_secs == 10 * 86400);

    cfg = cfg_plain();
    cfg.learn_count = 1;
    cfg.learn_secs[0] = 60;
    ff_sched_new_card(&cfg, &card);
    ff_sched_answer(&cfg, &card, FF_GOOD, now);
    EXPECT(card.state == FF_STATE_REVIEW);
    EXPECT(card.interval_secs == 86400);

    cfg = cfg_plain();
    ff_sched_new_card(&cfg, &card);
    ff_sched_preview(&cfg, &card, now, &delays);
    EXPECT(delays.again_secs == 60);
    EXPECT(delays.good_secs == 600);
    EXPECT(delays.easy_secs == 4 * 86400);

    ff_format_span(30, label, sizeof(label));
    EXPECT(strcmp(label, "30s") == 0);
    ff_format_span(60, label, sizeof(label));
    EXPECT(strcmp(label, "1m") == 0);
    ff_format_span(600, label, sizeof(label));
    EXPECT(strcmp(label, "10m") == 0);
    ff_format_span(86400, label, sizeof(label));
    EXPECT(strcmp(label, "1d") == 0);
    ff_format_span(4 * 86400, label, sizeof(label));
    EXPECT(strcmp(label, "4d") == 0);
}

static void test_text(void) {
    char *text = NULL;
    FfRendered side;
    char *fields[2];
    const char *names[2];
    char cafe[6];
    char snippet[32];

    EXPECT(ff_html_to_text("<b>Hello</b> world", &text) == 0);
    EXPECT(strcmp(text, "Hello world") == 0);
    free(text);

    EXPECT(ff_html_to_text("A<br>B", &text) == 0);
    EXPECT(strcmp(text, "A\nB") == 0);
    free(text);

    EXPECT(ff_html_to_text("<style>.card{color:red}</style>Hi", &text) == 0);
    EXPECT(strcmp(text, "Hi") == 0);
    free(text);

    EXPECT(ff_html_to_text("Tom &amp; Jerry", &text) == 0);
    EXPECT(strcmp(text, "Tom & Jerry") == 0);
    free(text);

    cafe[0] = 'c';
    cafe[1] = 'a';
    cafe[2] = 'f';
    cafe[3] = (char)0xC3;
    cafe[4] = (char)0xA9;
    cafe[5] = '\0';
    EXPECT(ff_html_to_text("caf&#233;", &text) == 0);
    EXPECT(strcmp(text, cafe) == 0);
    free(text);

    EXPECT(ff_html_to_text("[sound:a.mp3]Hello", &text) == 0);
    EXPECT(strcmp(text, "Hello") == 0);
    free(text);

    EXPECT(ff_html_to_text("<img src=\"pic.png\">Hi", &text) == 0);
    EXPECT(strcmp(text, "Hi") == 0);
    free(text);

    EXPECT(ff_note_has_image("x<img src='a.png'>") == 1);
    EXPECT(ff_note_has_image("plain") == 0);
    EXPECT(ff_note_has_audio("say [sound:a.mp3]") == 1);

    fields[0] = "Hi";
    fields[1] = "There";
    names[0] = "Front";
    names[1] = "Back";
    EXPECT(ff_render_side("{{Front}}", fields, names, 2, 1, 0, NULL, &side) == 0);
    EXPECT(strcmp(side.text, "Hi") == 0);
    ff_rendered_free(&side);

    EXPECT(ff_render_side("{{FrontSide}}<hr id=answer>{{Back}}", fields, names, 2, 1, 1, "Hi", &side) == 0);
    EXPECT(strcmp(side.text, "Hi\nThere") == 0);
    ff_rendered_free(&side);

    fields[0] = "The {{c1::capital}} of {{c2::France}}";
    names[0] = "Text";
    EXPECT(ff_render_side("{{cloze:Text}}", fields, names, 1, 1, 0, NULL, &side) == 0);
    EXPECT(strcmp(side.text, "The [...] of France") == 0);
    ff_rendered_free(&side);
    EXPECT(ff_render_side("{{cloze:Text}}", fields, names, 1, 1, 1, NULL, &side) == 0);
    EXPECT(strcmp(side.text, "The capital of France") == 0);
    ff_rendered_free(&side);
    EXPECT(ff_render_side("{{cloze:Text}}", fields, names, 1, 2, 0, NULL, &side) == 0);
    EXPECT(strcmp(side.text, "The capital of [...]") == 0);
    ff_rendered_free(&side);

    fields[0] = "{{c1::Paris::city}}";
    EXPECT(ff_render_side("{{cloze:Text}}", fields, names, 1, 1, 0, NULL, &side) == 0);
    EXPECT(strcmp(side.text, "[city]") == 0);
    ff_rendered_free(&side);

    fields[0] = "Q";
    fields[1] = "";
    names[0] = "Front";
    names[1] = "Back";
    EXPECT(ff_render_side("{{#Back}}X{{/Back}}{{^Back}}Y{{/Back}}", fields, names, 2, 1, 0, NULL, &side) == 0);
    EXPECT(strcmp(side.text, "Y") == 0);
    ff_rendered_free(&side);
    fields[1] = "A";
    EXPECT(ff_render_side("{{#Back}}X{{/Back}}", fields, names, 2, 1, 0, NULL, &side) == 0);
    EXPECT(strcmp(side.text, "X") == 0);
    ff_rendered_free(&side);

    fields[0] = "Hear [sound:a.mp3][sound:b.mp3]<img src=\"pic.png\">";
    EXPECT(ff_render_side("{{Front}}", fields, names, 1, 1, 0, NULL, &side) == 0);
    EXPECT(strcmp(side.text, "Hear") == 0);
    EXPECT(side.image_count == 1);
    EXPECT(strcmp(side.image, "pic.png") == 0);
    EXPECT(side.audio_count == 2);
    EXPECT(strcmp(side.audio, "a.mp3\x1f" "b.mp3") == 0);
    ff_rendered_free(&side);

    ff_snippet("  Hello\nthere  ", snippet, sizeof(snippet));
    EXPECT(strcmp(snippet, "Hello there") == 0);
}

static int collect_name(int index, const char *name, int legacy, void *user) {
    char *slot = (char *)user;
    (void)index;
    if (legacy >= 0) {
        snprintf(slot + strlen(slot), 64, "%s#%d;", name, legacy);
    } else {
        snprintf(slot + strlen(slot), 64, "%s;", name);
    }
    return 0;
}

static void test_proto_and_budget(void) {
    unsigned char media[] = {
        0x0A, 7, 0x0A, 5, 'a', '.', 'j', 'p', 'g',
        0x0A, 10, 0x0A, 5, 'b', '.', 'm', 'p', '3', 0xF8, 0x0F, 0x03};
    unsigned char formats[] = {
        0x0A, 9, '{', '{', 'F', 'r', 'o', 'n', 't', '}', '}',
        0x12, 8, '{', '{', 'B', 'a', 'c', 'k', '}', '}'};
    char names[64];
    char front[32];
    char back[32];
    uint64_t kind = 99;
    unsigned char cloze[] = {0x08, 0x01};

    names[0] = '\0';
    EXPECT(ff_pb_media_names(media, sizeof(media), collect_name, names) == 0);
    EXPECT(strcmp(names, "a.jpg;b.mp3#3;") == 0);
    EXPECT(ff_pb_two_strings(formats, sizeof(formats), front, sizeof(front), back, sizeof(back)) == 0);
    EXPECT(strcmp(front, "{{Front}}") == 0);
    EXPECT(strcmp(back, "{{Back}}") == 0);
    EXPECT(ff_pb_field_varint(cloze, sizeof(cloze), 1, &kind) == 0);
    EXPECT(kind == 1);

    EXPECT(ff_budget_limit(0) == 20ull * 1024ull * 1024ull);
    EXPECT(ff_budget_over(0, 20ull * 1024ull * 1024ull + 1) == 1);
    EXPECT(ff_budget_allow_image(0, 1024) == 0);
    EXPECT(ff_budget_allow_image(1, 1024) == 1);
    EXPECT(ff_budget_sqlite_heap(0) < ff_budget_sqlite_heap(1));
}

int main(void) {
    test_scheduler();
    test_text();
    test_proto_and_budget();
    if (g_failed) {
        printf("%d test(s) failed\n", g_failed);
        return 1;
    }
    printf("all core tests passed\n");
    return 0;
}
