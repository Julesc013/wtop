# Focus, selection and cancellation: source comparison

Inspected 18 November 2026. These are exact-source findings, not assumptions about
every packaged version or claims of universal UX superiority.

| Project / source revision | Startup and navigation | Mouse | Escape | Action-target implication |
| --- | --- | --- | --- | --- |
| htop `21bbc80c7811149ee33f449748addf23740c7b9f` | Maintains a current panel row; startup does not hide its selection | Mouse integrates with the panel's current selection and tags | Current source sets `hideSelection=true` and stops following | Hiding the highlight does not clear the row used by fallback row actions |
| NTop `326ccd80b41842868f4247d1e8978e854658a710` | Cursor index starts at zero; a cursor row and independent tags | Inspected input loop handles keyboard records, with no hover-focus model | Escape aborts Vi input; no equivalent main-view empty-target transition | Cursor and tag semantics need explicit repair to implement the requested state model |
| btop `5d03929a45e0720a4c8e2d189360ff1eb9ce1731` | `proc_selected` and selected PID start at zero; navigation changes one selection | Click selects; repeated click invokes details; outside click clears; no independent hover-focus state found | Cancels filter in filter mode; otherwise opens menu | Different from Wtop's grey focus plus separately confirmed target |
| Wtop Beta 3 candidate | Starts with no focus or confirmed identity; navigation/search only focus | Hover focuses; click confirms | Context back/cancel, then confirmed → focused → none | Mutation requires a confirmed lifetime or explicit tags, followed by native identity checks |

## htop: distinguish a hidden highlight from an empty target

`MainPanel.c` resets `hideSelection` on normal input and handles Escape by setting
that flag while stopping follow mode. `Panel.c` still retains its selected index.
`MainPanel_foreachRow` acts on tagged rows, or otherwise on `Panel_getSelected`.
Therefore describing current htop as having no Escape behaviour is incorrect;
describing its Escape as removing all possible process-action targets is also
incorrect. Wtop intentionally chooses the latter, stronger empty-target state
requested for this project.

Sources: [MainPanel.c](https://github.com/htop-dev/htop/blob/21bbc80c7811149ee33f449748addf23740c7b9f/MainPanel.c),
[Panel.c](https://github.com/htop-dev/htop/blob/21bbc80c7811149ee33f449748addf23740c7b9f/Panel.c),
[CommandLine.c](https://github.com/htop-dev/htop/blob/21bbc80c7811149ee33f449748addf23740c7b9f/CommandLine.c).

## NTop and btop

NTop's inherited table draws cursor/tag emphasis separately. Space toggles a tag
and advances the cursor; its input loop and Vi editor do not implement independent
mouse focus, confirmed selection and contextual main-view Escape. Its useful
dense table/Vi foundation is retained, but the new state model needs real identity
and action-policy changes, not only new colours.

Sources: [NTop ntop.c](https://github.com/gsass1/NTop/blob/326ccd80b41842868f4247d1e8978e854658a710/ntop.c),
[NTop vi.c](https://github.com/gsass1/NTop/blob/326ccd80b41842868f4247d1e8978e854658a710/vi.c).

btop has a genuine initial zero-selection state in its process configuration.
Its process navigation and mouse click handling operate on the process selection,
while Enter toggles a detail view. Its Escape/menu behaviour serves that dashboard
model. Wtop keeps a compact process table and uses a separate focus identity so a
mouse movement cannot silently move an already confirmed mutation target.

Sources: [btop configuration](https://github.com/aristocratos/btop/blob/5d03929a45e0720a4c8e2d189360ff1eb9ce1731/src/btop_config.cpp),
[btop input](https://github.com/aristocratos/btop/blob/5d03929a45e0720a4c8e2d189360ff1eb9ce1731/src/btop_input.cpp),
[btop drawing/navigation](https://github.com/aristocratos/btop/blob/5d03929a45e0720a4c8e2d189360ff1eb9ce1731/src/btop_draw.cpp).

## Usability tradeoff and review checklist

An extra confirmation-selection step makes accidental actions harder, but differs
from htop muscle memory. The footer must explain disabled F7/F8/F9 when only focus
exists, and Enter/click must remain discoverable. Two highlighted rows must make
their roles unambiguous: grey is navigation; dark cyan is the confirmed target.
Tags are another deliberate batch operation and must remain visible/countable.

Review keyboard-only, hover/click, rapid sampling/sorting, process exit/PID reuse,
filter/tab transitions, collapsed ancestors, all keymaps and small layouts.
Check custom black and yellow selection backgrounds as well as default cyan.
No amount of colour testing replaces identity-bound action tests. Stable desktop
acceptance includes a person checking legibility and the host's appearance.
