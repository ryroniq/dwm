# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

This is a personal fork of [suckless dwm](https://dwm.suckless.org) 6.8, patched with every relevant upstream bugfix and then extended with a single large custom commit (`e363f6e`, "My custom dwm tweaks") that replaces dwm's fixed-tag model with a dynamic tagging + class-grouping system. Treat `dwm.c` as significantly diverged from stock dwm — most patches from suckless.org/patches will not apply cleanly, and behavior described in generic dwm docs/tutorials often does not hold here.

## Build & test

```sh
make              # builds ./dwm (copies config.def.h -> config.h on first build only)
make clean install
make test         # builds and runs tagmath_test, the only automated test
```

- `config.h` is generated from `config.def.h` **only if it doesn't already exist** (`.gitignore`d, not tracked). If you edit `config.def.h` and `config.h` is already present, your changes are silently ignored until you `rm config.h` or `make clean` first.
- `make test` builds `tagmath_test.c` standalone against `tagmath.h` only — no X11/Xft needed. This is the one part of the WM's logic exercised by an automated test, specifically because it has produced real bugs before (an off-by-8x array size, a shift-by-64 UB case) per the comments in `tagmath_test.c`. When touching `tagmath.h` or any of the tag-bitmask arithmetic in `dwm.c` (`_tag_insert`, `_tag_remove`, `tag_stack`, `tag_swap`, `tag_adjacent`), update `tagmath_test.c` and run `make test`.
- There is no test coverage for anything event-driven (the rest of `dwm.c`); this matches upstream dwm, which has none either. Manual verification means running the built `dwm` in a nested X session (e.g. `Xephyr`) or swapping it in for a real session.
- `transient.c` (`cc transient.c -o transient -lX11`) is a standalone scratch helper for manually reproducing transient-window focus behavior; it is not part of the `dwm` build.

## Architecture

Single-binary X11 window manager, event-driven from `main()` -> `run()`, dispatching through the `handler[]` array indexed by X event type (see the file header comment in `dwm.c` for the general dwm design — client list, focus stack, O(1) event dispatch — that part is unchanged from upstream). Source layout:

- `dwm.c` — everything: event handlers, layouts, bar rendering, all custom logic (~3500 lines, single TU).
- `drw.c`/`drw.h` — Xlib/Xft drawing abstraction, kept in sync with dmenu upstream; not customized.
- `util.c`/`util.h` — `die()`/`ecalloc()` plus `MAX`/`MIN`/`BETWEEN`/`LENGTH` macros; stock.
- `tagmath.h` — pure tag-bitmask arithmetic (`tag_bit_insert`/`tag_bit_remove`, `TAG_LSB`/`TAG_MSB`, the `SYNC_VIEWMODE` macro), deliberately free of X11 so it links into both `dwm` and `tagmath_test` without a display connection.
- `config.def.h` — the only config file; there is no tracked `config.h` (see build note above).

### The two custom subsystems

Everything distinctive about this fork comes from two orthogonal changes to how clients are grouped and viewed, both introduced together in `e363f6e`:

**1. Dynamic tags, not a fixed 9-tag array.** `tag_t` is `unsigned long long` (64 tag slots max), defined in `tagmath.h`. Unlike stock dwm's static `tags[]` used directly as bit positions, tag *slots* here can be created, deleted, and reordered at runtime:
- `_tag_insert()` (`dwm.c`) opens a new zero bit at a computed position, shifting every client's `Client.tags` and the monitor's `curtags`/`prevtags` up to make room.
- `_tag_remove()` closes a tag slot, shifting everything above it down.
- `tag_stack()`/`tag_swap()` reorder the currently-selected tag bit(s) relative to occupied tags, one bit position at a time, via `tag_bit_insert`/`tag_bit_remove`.
- Bar labels for tags (`tlabels[]`) are seeded from the static `tags[]` array in `config.def.h` at startup but only as *initial* display strings for slots 0..N; slots beyond `LENGTH(tags)` get numeric labels. The bitmask itself has no fixed correspondence to `tags[]` once insert/remove/stack/swap have run.
- `TAGKEYS(KEY, IDX)` in `config.def.h` still binds `MODKEY+<key>` to a literal bit index (`TAG_UNIT << IDX`) for the first 10 slots; everything beyond that is reached via the stack/swap/insert/remove operations rather than direct keybindings.

**2. A second grouping axis: `Class`.** Clients are also grouped by `WM_CLASS` into a dynamic linked list (`classes` global, `struct Class`), independent of tags. `_class_find_or_create()` matches a window's class against `crules[]` in `config.def.h`, which can *rename/merge* a real WM_CLASS into a shared bucket (e.g. `st-256color` and `st` both land in one `Class` named `"st"`, sharing one bar entry and one `LayoutParams`) — this is a merge mechanism, not just a display label; see the comment on `ClassRule` in `dwm.c`. Each `Class` carries its own persistent `LayoutParams` (`nmaster`, `mfact`, layout index), so switching classes restores that class's own layout state.

A `Monitor` is in exactly one of two `ViewMode`s at a time (`m->viewmode`, re-derived after every `curtags` assignment via `SYNC_VIEWMODE(m)` — never set directly): `ViewClass` (showing every client of `m->curcls`) or `ViewTag` (showing clients matching `m->curtags`). `ISVISIBLE(C)` branches on this. `_layout_params()` picks `tag_lt_params[lsb_of(curtags)]` in `ViewTag` mode or `curcls->params` in `ViewClass` mode — i.e. layout/nmaster/mfact state is tracked **per tag and per class independently**, not globally.

Most user-facing bindings are written against the `group_*` family (`group_select`, `group_adjacent`, `group_stack`, `group_swap`, `group_insert`, `group_append` in `dwm.c`), which are thin dispatchers that forward to the `class_*` or `tag_*` implementation depending on `selmon->viewmode` — the same key does different things depending on current mode. When changing behavior of one of these operations, check both the `class_*` and `tag_*` sibling for the other mode.

### The bar

`drawbar()` is one large hand-rolled renderer, not a set of independent widgets. It draws, left to right: class list, tag list, urgent-client list, layout symbol + nmaster + mfact indicators, status text, then a client "taskbar" (visible clients on the monitor, each optionally prefixed with a single/double-letter label from `clabels[]` for direct selection via `client_select`/`CLIENTKEYS`). Each region truncates itself with a `<`/`>` ellipsis and scrolls to keep the current selection centered when it doesn't fit (`BAR_CLASS_MAX`/`BAR_TAG_MAX`/`BAR_URGENT_MAX`/`BAR_CLIENT_MAX` in `config.def.h` bound how many entries are drawn at once).

While drawing, `drawbar()` records the pixel x-boundary of every segment it draws into parallel static arrays (`classclick[]`, `tagclick[]`, `urgentclick[]`, `clientclick[]`). `buttonpress()` hit-tests a click by walking those same arrays in the same left-to-right order. **These two functions must stay in lockstep by construction order** — if you add/reorder a region in `drawbar()`, update the corresponding branch in `buttonpress()` (and vice versa), since there's no shared enum/table tying a region to its click array.

### Restart/reload

`reload()` doesn't exec-restart dwm in place; it sets `return_code = EXIT_RELOAD` (1) and lets the event loop exit normally, and `main()` returns that code. Restarting dwm on exit code 1 is expected to be handled by an external wrapper (e.g. a loop in `.xinitrc`), which is not part of this repo.

## Config conventions specific to this fork

- `crules[]` (not present in stock dwm) merges/renames WM_CLASS values into shared `Class` buckets with shared layout state — see above.
- `clabels[]` defines the taskbar quick-select letters (paired with `CLIENTKEYS` in the keybinding table); order matters, it's positional against the visible client list, not tied to any particular window.
- `default_lt_params` is the fallback `LayoutParams` (nmaster/mfact/layout index) used for classes/tags that don't have an explicit override.
- The status bar text is fed externally (e.g. from `.xinitrc`) the same way as stock dwm, via `xsetroot -name`; `BAR_STATUS_LENGTH`/`BAR_STATUS_WIDTH` cap how much of it is stored/drawn.
- `dmenucmd[]` in `config.def.h` invokes `mydmenu`, not the stock `dmenu_run` — this fork expects a custom/patched dmenu binary named `mydmenu` on `$PATH`, not present in this repo.
