# Snippet format

## Document model

A snippet is an ordered `snippetdocument::Document`, independent of its editor, playback engine and storage protection. `SnippetDocument.h` defines and validates actions. `SnippetCodec.h` converts that model to/from JSON. Both storage backends accept documents; the encrypted backend encrypts the complete serialized document with the existing Windows CNG cryptography.

Storage uses stable filenames under `%LOCALAPPDATA%\MK-AutoTyper`: one readable UTF-8 `Snippets.json` collection for all unencrypted entries, and the authenticated `Encrypted.dat` vault for protected entries (container marker `MKATVAUL`). Names are display text inside these stores; no user-supplied name is a path component. Unicode, emoji, punctuation, reserved Windows device words and trailing dots/spaces are supported. Names remain limited to 80 UTF-16 units; empty names, control characters and invalid Unicode are rejected.

The document schema is version 1. Optional root fields add variable expansion and typing overrides:

```json
{
  "format": "mk-autotyper-snippet",
  "version": 1,
  "expandVariables": true,
  "typing": {"overrideGlobal": true, "unicode": false, "keystrokeDelay": 10, "initialDelay": 1000},
  "actions": [
    {"type": "text", "text": "Hello {name}"},
    {"type": "key", "encoding": "virtualKey", "code": 9, "modifiers": 0, "extended": false},
    {"type": "delay", "milliseconds": 500},
    {"type": "key", "encoding": "virtualKey", "code": 82, "modifiers": 8, "extended": false}
  ]
}
```

This represents text, Tab, a 500 ms delay, and Win+R. It describes actions; loading or saving a document never executes them. Names live in collection/vault entries, separately from action documents. Renaming changes the display-name record, so it cannot leave a stale name inside the action payload.

| Action | Fields and meaning |
| --- | --- |
| `text` | `text`: literal Unicode text, including tabs/line endings. Literal unless `expandVariables` is enabled; key commands are always separate key actions. |
| `key` | `encoding`: `virtualKey` or `scanCode`; `code`: numeric Windows key identifier; `modifiers`: bit mask; `extended`: physical E0 scan-code prefix. This describes one balanced key press/chord. |
| `delay` | `milliseconds`: a nonnegative duration. This is a timed delay, distinct from a future user-controlled pause/resume command. |

Modifier flags are Control=1, Alt=2, Shift=4, Windows=8; combine them with bitwise OR. Virtual-key codes are 1–254, scan codes 1–255. `extended` must be false for virtual keys; Playback sends their Windows virtual-key identity; scan-code actions retain the stored physical code and extended flag. A key action can also represent a standalone modifier key, such as Left Windows. Representability does not promise that Windows or a destination accepts every key combination. The executor preflights collisions with AutoTyper global shortcuts. Windows may ignore reserved system shortcuts; the app does not bypass those restrictions.

The codec preserves order, fields, text, empty documents and empty text actions. JSON property order is irrelevant, while array order is significant. Duplicate/missing fields, invalid Unicode, invalid numbers, trailing data, unknown fields, unknown actions and unknown versions are rejected. Unknown future content must never be silently dropped and then overwritten. New features add typed model members and a new schema version as needed; storage and encryption continue handling the same serialized-document boundary.

Bounds: 4,096 actions, 1,048,576 total text UTF-16 units, 24 hours per delay, 8 Mi UTF-16 units per encoded document, 64 MiB per unencrypted collection, and the existing 4,096-record/64 MiB encrypted collection limits. An unencrypted collection also has aggregate limits of 32,768 actions and 16 Mi UTF-16 units of text. Empty strings do not allocate secret-buffer pages. These keep malformed files from producing unbounded allocation or recursion. The schema parser has a fixed depth rather than an arbitrary JSON object tree. It supports JSON string escapes and UTF-8/UTF-16 validation without a third-party dependency.

Documents are move-only because their text uses `secure::Text`. Temporary JSON, UTF-8 conversion buffers and decrypted action text use owned wiping buffers. Passwords are prompted per protected operation by default, or on first use when the optional session unlock is enabled. Neither passwords nor session keys are stored in the document or settings. Authenticated encryption binds records to the new container version and catalog names. Conversion preserves the complete action sequence and the existing save-first/rollback behavior; password changes validate and re-encrypt every document. A plaintext conversion deliberately writes a readable JSON file.

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

## Editor and playback

The main window starts on an empty text step. A visible top-to-bottom card flow edits the complete document. Text grows as needed; key/pause summaries expand for editing. Drag/drop or keyboard commands change the order. Switching cards commits the active field before changing the selection. Text steps use the normal editable field, not a read-only summary. Save always takes the complete document. Add text/key/pause, edit, move and remove preserve all steps. The worker dispatches text, balanced key chords and cancellable delays through the same keyboard runtime. Focus/layout checks and partial-input releases apply to both entry points. Per-action progress identifies the active step and timed pause.

The optional `expandVariables` boolean defaults to false. When enabled, `{name}` requests a case-sensitive custom field, `{date}` and `{time}` use one local timestamp, and `{{`/`}}` escape braces. Custom names contain up to 64 letters/digits/underscores/hyphens. There are at most 32 custom fields with 65,536 UTF-16 units per value. Expansion is nonrecursive and bounded by the existing total text limit. It changes only a temporary playback document, never saved source text.

The optional `typing` object contains `overrideGlobal`, `unicode`, `keystrokeDelay` (2-60,000 ms) and `initialDelay` (0-60,000 ms). All four fields are required when the object is present; duplicates and unknown fields are rejected. Missing settings use global defaults. Saved custom values can remain present while `overrideGlobal` is false. The main window scope switch shows either global values or saved custom values. Custom edits never overwrite global preferences, and switching back preserves custom values for reuse.

Text-only storage convenience APIs continue rejecting sequences and enabled template/typing metadata instead of flattening them. GUI paths load documents and preserve their metadata. The file/container names and encryption are unchanged. Future action kinds or schema changes must remain explicit, bounded and covered by parser, storage, playback and cleanup tests.
