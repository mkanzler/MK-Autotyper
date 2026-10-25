# User guide

## First launch

Use Windows 10 version 1607 or later, or Windows 11. Choose x64 for 64-bit Windows or x86 for 32-bit Windows. Download and run the EXE directly, or extract a ZIP and run `MK-AutoTyper.exe`. Both formats provide the same portable app, with no installer or separate C++ runtime installation.

Keep the executable in its permanent folder before enabling **Start with Windows**. The first launch follows the Windows app theme.

## Type text

1. Write or paste text into the editor.
2. Set **Keystroke delay** and **Initial delay**.
3. Click **Start typing**, then focus the destination before the initial delay ends.

The main window hides during typing. The countdown/progress panel, global shortcut and tray menu can stop a job. Keystroke delay is 2–60,000 ms; initial delay is 0–60,000 ms. Options are captured when a session starts.

**Scan-code input** is selected on every launch. It uses the destination's English or German layout with Unicode fallback for unmapped characters and dead keys. **Unicode input** sends Windows Unicode events; the destination must support them. US English, UK English and German layouts are supported. Other language layouts are rejected.

CRLF, CR and LF each produce one line-break action. Choose Enter or Shift+Enter in **More settings**. The destination determines whether these keys insert a line or submit a form. Tabs and backspaces produce real key presses. The literal characters `\t` are not a tab command.

Release shortcut keys before typing begins. Held modifiers postpone input for up to five seconds. Switching the foreground window or keyboard layout stops the job. Windows can block input into elevated apps; run the destination and AutoTyper at the same permission level.

## Editor and snippets

The editor supports Ctrl+A, cut/copy/paste, multilevel undo/redo, word navigation/deletion, selection, multiline text and word wrapping. Ctrl+Z undoes; Ctrl+Y or Ctrl+Shift+Z redoes. Right-click or press Shift+F10 for editing commands. Tab moves between controls; **Ctrl+Tab** inserts a tab.

Use New, Save, Rename, Delete and the saved-snippet selector above the editor. Selecting a snippet loads it without starting typing. Changes are saved only by an explicit save action. Renaming does not save pending body changes; deleting an entry leaves its current text in the editor.

Names support Unicode, emoji, punctuation, trailing dots/spaces and device words such as `CON`. Names are display text, never filenames. The limit is 80 UTF-16 units. Empty names, control characters, invalid Unicode and case-insensitive duplicates are rejected. Save/Rename show a live count and validation feedback; Ctrl+A selects the full name.

The title displays an unsaved `*` for nonempty modified content. Loading/New/Exit confirms unsaved nonempty changes. An empty editor has no unsaved marker and does not ask to save.

### Search from another app

Press **Ctrl+Alt+P**, then type part of a snippet name. Matching ignores case, including German umlauts. The first match is selected automatically.

- Use Up/Down or Page Up/Page Down to navigate, or scroll/select with the mouse.
- Press Enter or double-click to type into the previously active app.
- At most four result rows are visible.
- Escape or clicking outside dismisses the selector; no match cannot start a job.

Search uses saved content and current typing settings. It leaves unsaved editor content alone and is ignored during a session/countdown. Protected snippets request the password at the selector's position.

### Protected snippets

Enable **Encrypt snippet**, then Save. The first protected save creates one master password shared by all protected snippets. Use 12–1,024 characters. Every protected load requires the password; it is not cached.

To save a protected snippet without encryption, uncheck the option and Save. To encrypt an unencrypted snippet, check it and Save. The stored format changes only after a successful save.

**More settings → Change master password** re-encrypts the protected collection. A lost master password cannot be recovered. See [security and storage](security.md).

## Shortcuts and preferences

| Default | Action |
| --- | --- |
| Ctrl+Alt+V | Type clipboard text while idle; stop an active job/countdown |
| Ctrl+Alt+P | Search and type saved snippets |
| Ctrl+S | Save the editor snippet |
| Ctrl+N | Start a new snippet |

The two global shortcuts are configurable in More settings and must be different and available. Capture temporarily pauses their registrations. Conflicts leave the dialog open for another choice.

Theme, delays, countdown visibility, line-break choice, global shortcuts and normal window position are saved per user. Input-method selection returns to Scan-code input on launch.

Use **Theme → Windows setting**, **Light**, or **Dark**. Windows setting follows OS changes, including open app dialogs. Light/Dark override the OS setting.

## Tray and startup

Closing the window keeps the app in the system tray. Click its icon to restore; right-click for Stop or Exit. Protected editor content is cleared when its window closes.

**Start with Windows** creates a per-user shortcut and starts hidden in the tray. Uncheck it to remove the shortcut. Enable it again after moving the executable.

| Command-line option | Behavior |
| --- | --- |
| `/tray` | Start hidden in the system tray |
| `/minimized` | Start minimized |

## Troubleshooting

**No typing:** focus the destination field, release Ctrl/Alt/Shift, try the other input method, and check that both apps have the same permission level. Some apps reject simulated input.

**Shortcut unavailable:** another app may have registered it. Choose another combination in More settings.

**Data not in the expected folder:** the normal location is `%LOCALAPPDATA%\MK-AutoTyper`. A packaged parent app can redirect a launched process's files/registry. Exit AutoTyper from the tray, then launch it directly from File Explorer for the normal user location. See [storage](security.md#local-data).
