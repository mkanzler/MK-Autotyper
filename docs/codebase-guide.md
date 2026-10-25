# Reading and changing the code

MK AutoTyper is one native Win32 executable. Small components live in headers so the application and isolated tests use the same implementation. There is no UI framework, dependency injection container or third-party cryptography library.

## Where to start

| File under `AutoTyper/` | Responsibility |
| --- | --- |
| `AutoTyper.cpp` | Main window, native controls and painting, editor state, tray commands, and connecting user actions to components. Start at `WinMain`, then `windowProc`. |
| `KeyboardInput.h` | English/German character mapping, scan-code/Unicode events, normalization, delays, cancellation and input cleanup. |
| `TypingSession.h` | One background typing job and synchronized progress. Owns the job's text and worker lifetime. |
| `TypingTarget.h` | Capturing and restoring the destination window. |
| `Hotkey.h` | Shortcut validation, labels, registration ownership and hotkey-control caret behavior. |
| `SnippetDocument.h` | Move-only action model, key/modifier identifiers, bounds, validation and text-editor compatibility boundary. |
| `SnippetCodec.h` | Strict, bounded JSON document encoding/decoding in wiping buffers. |
| `SnippetStore.h` | Unicode display names, one UTF-8 JSON collection, bounded parsing, serialized writers and atomic replacement. |
| `EncryptedSnippets.h` | Bounded binary parsing, Windows CNG encryption, authenticated storage, conversions and master-password changes. |
| `SecureMemory.h` | Move-only buffers and text, wiping, and temporary string guards. |
| `AppDialogs.h` | Shared appearance, themed messages/confirmations, native theme dropdown drawing/resources, snippet picker, name dialog and preferences dialog. |
| `PasswordDialog.h` | Masked password entry, confirmation, retry and control cleanup. |
| `SnippetSearch.h` | Searchable shortcut overlay, filtering, navigation and selection. |
| `SessionPanel.h` | Countdown/progress overlay and cancellation UI. |
| `Settings.h`, `WindowPlacement.h` | Validated per-user preferences and DPI-aware window placement. |
| `StartupShortcut.h` | Current-user Startup shortcut creation/removal. |

`AutoTyper.rc`, `Resource.h`, the icon files and the embedded license define executable resources. `AutoTyper.h`, `framework.h` and `targetver.h` are Visual Studio support headers.

## Important flows

- **Editor typing:** the main window reads a `secure::Text`, captures options and starts `typing_session::Session`. The worker calls the keyboard runner; the UI polls progress. Cancellation joins the worker before teardown. The worker never reads native UI controls.
- **Shortcut snippets:** capture the foreground target, show `snippetsearch`, load the selected saved content (prompting for a password when encrypted), restore the target, and start a session with the captured preferences. The editor is independent of this saved-content lookup.
- **Saving:** `saveSnippet` coordinates the operation. `promptNewSnippetName` handles name validation/retries; `saveSnippetContents` reads the editor once into a one-text-action document and handles storage or conversion. Only a successful save updates the editor's saved selection and clears its modified state.
- **Encrypted storage:** each content operation creates a local vault and key, calls `openAuthenticatedVault`, performs its operation, and destroys the key on return. Only creating an encrypted destination may create a missing vault. `list` intentionally reads visible names without a password; content operations authenticate the complete catalog before using it.

## State and ownership rules

Main-window state in `AutoTyper.cpp` belongs to the UI thread and stays in its anonymous namespace. The session class owns cross-thread state and synchronization. Modal guards block global typing/search starts. Preferences and password entry also pause shortcut registrations where keyboard capture requires it, then restore the previous registrations when closing.

`Preferences::theme` stores the user's Windows/Light/Dark choice. `darkTheme` in the main window is the resolved palette, not a second saved preference. New settings default to Windows; `Settings.h` migrates an older explicit `DarkTheme` choice only if `ThemeMode` is absent. Windows appearance notifications update the main window and send the synchronous `dialogs::ThemeChanged` message to open dialogs. Each dialog rebuilds its own brushes without recreating its input controls or clearing the user's entries. App messages go through `showMessage` so the modal guard also blocks global clipboard/snippet starts.

`chooseTheme` tracks a native popup menu anchored to the theme button. `ThemeMenu` owns its menu, background brush and accessibility names until tracking ends. The main window measures and draws its three rows with the current palette. Selection returns a command and applies/saves immediately; dismissal returns no command. A Windows appearance change ends an open menu before refreshing the palette.

`selectedEncrypted` describes the **saved source**. The encryption checkbox describes the **pending destination on Save**. They may differ while editing; do not update the source flag before storage succeeds. This distinction drives conversion, unsaved-change prompts and protected-editor cleanup.

Passwords and decrypted text must stay in `secure::Text`/`secure::Buffer` whenever possible. Do not introduce password caches, log secret values, capture plaintext in background lambdas, or copy it into settings. Clear native password controls and undo buffers before executing the protected action. Clear protected editor text and undo history when the typing attempt ends, stops or fails. Windows/control limitations are described in [security and storage](security.md#memory-lifetime).

Storage updates write and flush a temporary destination before replacement. Plain/encrypted conversion spans two files, so preserve its explicit rollback behavior. The active files are `%LOCALAPPDATA%\MK-AutoTyper\Snippets.json` and `Encrypted.dat`, with document schema version 1 and container marker `MKATVAUL`. No name is used as a filename; display names live in the collection/vault records. Future schema/container changes must be explicit; do not change password derivation as a side effect of a document feature. See [snippet-format.md](snippet-format.md) for the format, current GUI boundary, and phased action editor/runner plan.

## Keeping changes readable

- Use `.clang-format` for application and test C++ files. Include order is preserved because Windows headers have ordering requirements.
- Prefer descriptive operation names, early returns and short functions with one clear task. Extract repeated rules when they must remain consistent; avoid one-use wrappers that merely rename a Win32 call.
- Keep comments focused on ownership, security, compatibility and reasons for non-obvious behavior. Native event dispatch and drawing coordinates can remain explicit.
- Keep the production dependency footprint unchanged unless a dependency solves a concrete problem that Windows APIs cannot reasonably solve.

From a Visual Studio developer shell, format a changed file with `clang-format -i <file>` and check it with `clang-format --dry-run --Werror <file>`. The formatter is a development tool, not an application dependency.

Run both test scripts documented in [building.md](building.md) after behavior refactors. The keyboard/runtime suite avoids desktop injection; the UI/storage suite uses temporary files and isolated settings. Tests compile with `/W4 /WX`. Build both x64 and Win32 Release configurations before distributing changes. Encryption changes require the tampering, password-change, conversion and cleanup regression tests, not only a successful encrypt/decrypt round trip.
