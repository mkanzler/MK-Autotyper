# Snippet format

## Document model

A snippet is an ordered `snippetdocument::Document`, independent of its editor, playback engine and storage protection. `SnippetDocument.h` defines and validates actions. `SnippetCodec.h` converts that model to/from JSON. Both storage backends accept documents; the encrypted backend encrypts the complete serialized document with the existing Windows CNG cryptography.

Storage uses stable filenames under `%LOCALAPPDATA%\MK-AutoTyper`: one readable UTF-8 `Snippets.json` collection for all unencrypted entries, and the authenticated `Encrypted.dat` vault for protected entries (container marker `MKATVAUL`). Names are display text inside these stores; no user-supplied name is a path component. Unicode, emoji, punctuation, reserved Windows device words and trailing dots/spaces are supported. Names remain limited to 80 UTF-16 units; empty names, control characters and invalid Unicode are rejected.

The first document schema is version 1:

```json
{
  "format": "mk-autotyper-snippet",
  "version": 1,
  "actions": [
    {"type": "text", "text": "Example"},
    {"type": "key", "encoding": "virtualKey", "code": 9, "modifiers": 0, "extended": false},
    {"type": "delay", "milliseconds": 500},
    {"type": "key", "encoding": "virtualKey", "code": 82, "modifiers": 8, "extended": false}
  ]
}
```

This represents text, Tab, a 500 ms delay, and Win+R. It describes actions; loading or saving a document never executes them. Names live in collection/vault entries, separately from action documents. Renaming changes the display-name record, so it cannot leave a stale name inside the action payload.

| Action | Fields and meaning |
| --- | --- |
| `text` | `text`: literal Unicode text, including tabs/line endings. Strings such as `{Win+R}` remain literal text. |
| `key` | `encoding`: `virtualKey` or `scanCode`; `code`: numeric Windows key identifier; `modifiers`: bit mask; `extended`: physical E0 scan-code prefix. This describes one balanced key press/chord. |
| `delay` | `milliseconds`: a nonnegative duration. This is a timed delay, distinct from a future user-controlled pause/resume command. |

Modifier flags are Control=1, Alt=2, Shift=4, Windows=8; combine them with bitwise OR. Virtual-key codes are 1–254, scan codes 1–255. `extended` must be false for virtual keys; Windows resolves their physical mapping at playback time. A key action can also represent a standalone modifier key, such as Left Windows. Representability does not promise that Windows or a destination accepts every key combination. The future executor must reject unsupported mappings and reserved sequences explicitly.

The codec preserves order, fields, text, empty documents and empty text actions. JSON property order is irrelevant, while array order is significant. Duplicate/missing fields, invalid Unicode, invalid numbers, trailing data, unknown fields, unknown actions and unknown versions are rejected. Unknown future content must never be silently dropped and then overwritten. New features add typed model members and a new schema version as needed; storage and encryption continue handling the same serialized-document boundary.

Bounds: 4,096 actions, 1,048,576 total text UTF-16 units, 24 hours per delay, 8 Mi UTF-16 units per encoded document, 64 MiB per unencrypted collection, and the existing 4,096-record/64 MiB encrypted collection limits. An unencrypted collection also has aggregate limits of 32,768 actions and 16 Mi UTF-16 units of text. Empty strings do not allocate secret-buffer pages. These keep malformed files from producing unbounded allocation or recursion. The schema parser has a fixed depth rather than an arbitrary JSON object tree. It supports JSON string escapes and UTF-8/UTF-16 validation without a third-party dependency.

Documents are move-only because their text uses `secure::Text`. Temporary JSON, UTF-8 conversion buffers and decrypted action text use owned wiping buffers. Passwords are still prompted per protected operation and never stored in the document or settings. Authenticated encryption binds records to the new container version and catalog names. Conversion preserves the complete action sequence and the existing save-first/rollback behavior; password changes validate and re-encrypt every document. A plaintext conversion deliberately writes a readable JSON file.

The unencrypted collection has this wrapper:

```json
{
  "format": "mk-autotyper-collection",
  "version": 1,
  "snippets": [
    {
      "name": "Login: example / test?",
      "document": {
        "format": "mk-autotyper-snippet",
        "version": 1,
        "actions": [{"type": "text", "text": "Example"}]
      }
    }
  ]
}
```

One collection is appropriate for a small local app: it makes backups and renames straightforward and removes filename restrictions. The protected vault stays separate so protected bodies never enter the readable collection. Each store commits by flushed atomic replacement. Per-store named mutexes serialize read-modify-write operations across app instances; conversions acquire both in a fixed order. A broken/unsupported JSON collection is reported and never overwritten automatically. The internal format/version fields detect unsupported files; they do not change the folder or filename.

## Current GUI boundary

The current editor saves its text as one `text` action. It and Ctrl+Alt+P can load/play an empty document or one text action. More complex sequences are reported as unsupported by this editor/playback version before any text is returned or input is sent. They cannot be flattened and accidentally saved over. Storage round trips, conversion, rename and password rotation already support sequences.

Version 2.0 provides storage for action documents. Its editor and runtime support text only; unsupported action sequences are rejected.

## Future editor and runtime work

1. **Action editor.** Add a native list of steps with Add text / Key or shortcut / Delay, inline editing, move up/down, duplicate and delete. Keep the existing plain text workflow simple. A focused shortcut-capture field records a chord without registering it as a global shortcut. Display friendly key names, not numeric codes or JSON. Hold the complete document in editor state so Save never reconstructs it from a text preview. Modified-state tracking, New/Load, undo history, and encrypted cleanup must cover every action.
2. **Single action runner.** Extend the existing cancellable session with an ordered dispatcher. Text reuses current English/German mapping, input mode, delays and line-ending behavior. Key actions generate balanced press/release events; delay actions use an interruptible wait. Reject unmappable keys before starting. Stop/failure/focus changes must release injected modifiers, clear consumed action text and wipe the remaining document. Test the event plan with substituted input calls rather than real desktop shortcuts.
3. **Pause/resume and progress.** Add an explicit paused session state, a pause/resume shortcut and current-step progress. Pause only between balanced input events, with no injected keys held. Distinguish a timed delay step from a user pause. Cancel must work during both. Define destination handling before supporting shortcuts that deliberately change the foreground window; never blindly continue typing into an unexpected window.
4. **Preview and step-through.** Show a readable sequence and estimated duration before running. A dry run emits no input, and step-through executes one action at a time. This makes form/tab sequences easier to check.

## Extension points

Add these only when needed, as explicit action types or document metadata:

- **Prompted variables:** ask once for values such as customer name, then substitute explicit variable actions. Keep secret variables in wiping buffers, out of saved settings and logs.
- **Date/time insertion:** an explicit action with a format and an execution-time value; literal text is never interpreted as a template language.
- **Bounded repeat groups:** repeat selected steps a chosen count, with nesting and total-expanded-step limits. Avoid arbitrary scripts or an embedded scripting engine.
- **Wait for a destination:** an explicit target condition and timeout for multi-window workflows; preserve cancellation and verify the target before sending more text.
- **Per-step typing options:** optional overrides for character delay or input mode, while ordinary snippets continue using current settings.
- **Organization/export:** tags, favorites, stable snippet IDs and JSON import/export. Add IDs before introducing references between snippets; references require cycle/depth checks. Export of protected content must require an explicit choice and password.

Each extension should add model validation, codec round-trip/rejection tests, action-runner tests and then the matching GUI control. Keep file I/O and encryption unaware of playback mechanics. The format is extensible without installing a service, driver, scripting runtime, UI framework or JSON library.
