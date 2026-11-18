/* Independent compositor oracles. SPDX-License-Identifier: GPL-3.0-only */
#include "frame.h"
#include <assert.h>
#include <limits.h>
#include <string.h>
typedef struct sink_state { int calls, fail; wtop_rectangle seen; } sink_state;
static int sink(void *context, const wtop_frame *frame, const wtop_rectangle *r)
{
    sink_state *state = (sink_state *)context;
    assert(frame->back && r->left >= 0 && r->top >= 0);
    assert(r->right < frame->width && r->bottom < frame->height);
    ++state->calls; state->seen = *r;
    return !state->fail;
}
int main(void)
{
    wtop_frame frame;
    sink_state state;
    wtop_cell *original;
    int i;
    memset(&frame, 0, sizeof(frame)); memset(&state, 0, sizeof(state));
    assert(wtop_frame_resize(&frame, 80, 25));
    wtop_frame_begin(&frame, 7);
    wtop_frame_put(&frame, 79, 24, 'Z', 0x30);
    assert(!state.calls && !frame.valid);
    assert(wtop_frame_present(&frame, sink, &state) == 1);
    assert(state.calls == 1 && state.seen.left == 0 && state.seen.right == 79 && state.seen.bottom == 24);
    assert(wtop_frame_present(&frame, sink, &state) == 0 && state.calls == 1);
    wtop_frame_put(&frame, 5, 6, 'A', 7);
    assert(wtop_frame_present(&frame, sink, &state) == 1);
    assert(state.seen.left == 5 && state.seen.right == 5 && state.seen.top == 6 && state.seen.bottom == 6);
    wtop_frame_attribute(&frame, 5, 6, 0x20);
    state.fail = 1;
    assert(wtop_frame_present(&frame, sink, &state) == -1 && !frame.valid);
    assert(frame.front[6 * 80 + 5].attribute == 7);
    state.fail = 0;
    assert(wtop_frame_present(&frame, sink, &state) == 1 && state.seen.right == 79);
    original = frame.back;
    assert(!wtop_frame_resize(&frame, -1, 25));
    assert(!wtop_frame_resize(&frame, 32767, 32767));
    assert(frame.back == original && frame.valid);
    assert(wtop_frame_resize(&frame, 40, 10));
    wtop_frame_begin(&frame, 7);
    wtop_frame_fill(&frame, -3, 0, 5, 'x', 2);
    assert(frame.back[0].glyph == 'x' && frame.back[1].glyph == 'x' && frame.back[2].glyph == ' ');
    wtop_frame_fill(&frame, INT_MIN, 0, INT_MAX, '!', 3);
    assert(frame.back[0].glyph == 'x');
    wtop_frame_fill(&frame, 39, 9, 200, 'Y', 0x30);
    assert(frame.back[399].glyph == 'Y' && frame.back[398].glyph == ' ');
    for (i = 0; i < 1000; ++i) {
        assert(wtop_frame_present(&frame, sink, &state) >= 0);
        wtop_frame_begin(&frame, 7);
        wtop_frame_fill(&frame, 0, 0, 2, 'x', 2);
        wtop_frame_put(&frame, 39, 9, 'Y', 0x30);
    }
    assert(state.calls == 5); /* Four successful changes plus one failed sink. */
    wtop_frame_dispose(&frame); wtop_frame_dispose(&frame);
    assert(!frame.front && !frame.back && !frame.count);
    return 0;
}
