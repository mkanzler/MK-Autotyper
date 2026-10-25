# Testing

## Automated suites

Run both scripts in [building](building.md). Tests use production headers, with warnings treated as errors.

| Area | Coverage |
| --- | --- |
| Keyboard mapping | 381 printable US/UK/German combinations, both modes, Caps Lock, Shift/AltGr, punctuation/numpad/dead keys, physical Enter/Tab/Backspace |
| Typing runtime | Line endings, surrogate pairs, delays, progress, cancellation/restart, partial-input cleanup and shortcut collisions |
| Native UI | Editor commands, themes, menus, shortcut capture, search, name validation, empty-editor title/confirmation and protected cleanup |
| Preferences/startup | Defaults, invalid values/types, registry round trips, placement and disposable shortcut readback/removal |
| JSON | Unicode/escapes, action order, key encodings/modifiers, bounds, malformed/future data, duplicate names/fields, concurrent writers and atomic-save failure |
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
