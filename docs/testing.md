# Testing

## Automated suites

Run both scripts in [building](building.md). Tests use production headers, with warnings treated as errors.

| Area | Coverage |
| --- | --- |
| Keyboard mapping | 381 printable US/UK/German combinations, both modes, Caps Lock, Shift/AltGr, punctuation/numpad/dead keys, physical Enter/Tab/Backspace and explicit German Ctrl+Alt+Q for `@` |
| Typing runtime | Line endings, surrogate pairs, delays, progress, cancellation/restart, partial-input cleanup and shortcut collisions |
| Actions and variables | Ordered text/key/pause playback, balanced Windows/Ctrl/Alt/Shift events, long-pause cancellation, focus changes, preflight shortcut conflicts, partial-input cleanup, settings isolation, repeated/literal/bounded fields and JSON round trips |
| Action UI | Aggregate text bounds and correction at the exact limit, invalid-pause card access/removal, both themes, growing text cards, collapsed key/pause summaries, expansion, drop boundaries/release position, Escape/capture-loss cancellation, drag auto-scroll, high-resolution wheel accumulation, passive resize anchoring, adaptive text height and folding/undo, keyboard ordering, 32-bit scrolling and virtualization at 4096 steps, text-first defaults, searchable key selection, Ctrl+Alt+V capture/cancel/release, distinct Numpad Enter, settings isolation, accessible themed Add step menu, contextual controls, repeated growth/shrink repaint regions and text/selection preservation at 100%/150%/200% DPI, Manage menu and header/card spacing, paged masked fields, encrypted saves and cleanup |
| Native UI | Editor commands, themes, menus, shortcut capture, search, name validation, empty-editor title/confirmation and protected cleanup |
| Preferences/startup | Defaults, invalid values/types, registry round trips, placement and disposable shortcut readback/removal |
| JSON | Unicode/escapes, action order, key encodings/modifiers, bounds, malformed/future data, duplicate names/fields, concurrent writers and atomic-save failure |
| Session unlock | First-use/cancel prompts, settings Apply/Cancel and persistence, key reuse without the password, tamper/password-change invalidation, manual lock and editor/undo cleanup |
| Encryption | Wrong/weak passwords, tampering, randomization, password changes, conversions, collisions, rollback and cleanup |

Layout tests check Windows translation and planned events, not physical hardware or browser behavior. Fixtures use isolated files/registry keys. They do not enable real autostart, read the user's clipboard, or inject desktop typing.

## Additional checks

- Build Release x64 and x86.
- Run MSVC `/analyze /W4 /WX`.
- Run parser/ownership tests under AddressSanitizer after relevant changes.
- Check formatting, local documentation links and `git diff --check`.
- Check release version, architecture, imports, ZIP contents and hashes.

## Manual release checks

These checks need interactive destinations and are not implied by unit-test results:

1. Launch packaged x64 and, where available, x86 on supported Windows versions.
2. Inspect light/dark/system appearance, minimum size, keyboard navigation and high DPI.
3. Compare a disposable English/German sample in native fields and Chrome/Edge textarea/contenteditable fields, in both input modes. Include `yYzZ @ € { } [ ] \\ | ~ ^ äöüß`.
4. Check clipboard start/stop, held modifiers, zero initial delay, line breaks/tabs, cancellation, focus/layout changes and permission-level mismatch.
5. Save/load/search snippets and verify protected loading, password retry/change and cleanup.
6. Check tray restore/exit and startup. Sign-out/sign-in and physical multi-monitor behavior are separate checks.

Report destination/version/layout/mode results explicitly. Event-planning tests do not establish browser end-to-end compatibility.

Live-resize checks should include repeated enlargement/shrinking, maximize/restore, and both themes. Inspect the result immediately, before minimizing or reopening the window. Native repaint tests use an off-screen, non-activating fixture because hidden windows do not maintain child paint regions.
