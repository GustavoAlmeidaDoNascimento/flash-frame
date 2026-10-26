#ifndef FF_TEXTUTIL_H
#define FF_TEXTUTIL_H

typedef struct FfRendered {
    char *text;
    char *image;
    char *audio;
    int image_count;
    int audio_count;
} FfRendered;

int ff_html_to_text(const char *html, char **out);
void ff_rendered_free(FfRendered *rendered);
void ff_rendered_clear(FfRendered *rendered);

int ff_note_has_image(const char *fields);
int ff_note_has_audio(const char *fields);

int ff_render_side(
    const char *fmt,
    char **fields,
    const char **names,
    int nfields,
    int cloze_ord,
    int reveal,
    const char *front_side,
    FfRendered *out);

void ff_snippet(const char *text, char *dst, int dst_len);

#endif
