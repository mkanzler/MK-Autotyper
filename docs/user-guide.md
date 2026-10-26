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

CRLF, CR and LF each produce one line-break action. Choose Enter or Shift+Enter in **More settings**. In scan-code input, German `@` uses left Ctrl + left Alt + Q; Unicode input continues sending the character directly. The destination determines whether these keys insert a line or submit a form. Tabs and backspaces produce real key presses. The literal characters `\t` are not a tab command.

Release shortcut keys before typing begins. Held modifiers postpone input for up to five seconds. Switching the foreground window or keyboard layout stops the job. Windows can block input into elevated apps; run the destination and AutoTyper at the same permission level.

## Editor and snippets

The editor supports Ctrl+A, cut/copy/paste, multilevel undo/redo, word navigation/deletion, selection, multiline text and word wrapping. Ctrl+Z undoes; Ctrl+Y or Ctrl+Shift+Z redoes. Right-click or press Shift+F10 for editing commands. Tab moves between controls; **Ctrl+Tab** inserts a tab.

Use New, Save and the saved-snippet selector above the editor. Manage contains Rename and Delete for the selected saved snippet. Selecting a snippet loads it without starting typing. Changes are saved only by an explicit save action. Renaming does not save pending body changes; deleting an entry leaves its current text in the editor.

Names support Unicode, emoji, punctuation, trailing dots/spaces and device words such as `CON`. Names are display text, never filenames. The limit is 80 UTF-16 units. Empty names, control characters, invalid Unicode and case-insensitive duplicates are rejected. Save/Rename show a live count and validation feedback; Ctrl+A selects the full name.

The title displays an unsaved `*` for nonempty modified content. Loading/New/Exit confirms unsaved nonempty changes. An empty editor has no unsaved marker and does not ask to save.

### Actions and shortcuts

New snippets start with one expanded Text card. Steps run from top to bottom, and every step remains visible in the flow. **+ Add step** at the end adds Text, Key / shortcut or Pause. Text cards grow with their contents up to the space available in the window; longer text scrolls within its field. Folding the current text card preserves its caret and undo history. The flow scrolls as the sequence grows. Wheel input follows the Windows scroll setting, and resizing keeps the visible card anchored.

Key and pause steps are added collapsed, with their shortcut or duration visible in the header. Click a header to expand or collapse it. Only the current key/pause editor expands, while text cards can remain open. Edits are committed before changing cards or saving; Save always stores the complete sequence.

Drag the six-dot handle at the left of a header to change the order. The insertion line shows where the step will land, and dragging near the top or bottom scrolls the flow. Escape, losing capture or releasing outside the flow cancels without changing the order. The release position determines the final destination. On a focused header, Alt+Up/Alt+Down also reorder steps. The **...** menu offers Move up, Move down and Remove step. Removing the last step restores an empty Text card.

For a key step, press **Record** and enter a shortcut, or search for a friendly key name such as Enter, Tab, Escape or F5. The first search match is preselected; Enter chooses it and Down moves into the results. Ctrl, Alt, Shift and Win switches build combinations without having to press them. Numpad Enter is distinct from Enter. Recording pauses AutoTyper's global shortcuts until the recorded key is released; Esc or Cancel leaves the previous shortcut unchanged. Some Windows-reserved combinations cannot be captured directly; select the key and modifiers instead.

Pause steps use milliseconds: 1000 means one second. Values must be 0 to 86,400,000. Text steps always remain directly editable; switching steps preserves their contents and the snippet's unsaved state.

The same start/stop shortcut cancels text, shortcuts, countdowns and timed pauses. Typing stays bound to the original destination window. A shortcut that opens another window can stop the remaining sequence when focus changes. Windows reserves combinations such as Ctrl+Alt+Delete. A step matching either AutoTyper global shortcut is rejected before any steps are sent; change that step or the global shortcut to resolve the conflict.

### Variables

Enable **Use variables** below the text/step editor. Use `{name}` or `{reference}` in text steps. The app asks for each unique field once, reuses its value across steps, and clears those values after the job. Field names are case-sensitive and support up to 64 letters/digits, underscores or hyphens. At most 32 custom fields are supported; prompts show four fields per page. Values for encrypted snippets are masked.

`{date}` inserts the local date as `YYYY-MM-DD`; `{time}` inserts the local time as `HH:MM:SS`. Both are captured once after the prompts. Use `{{` and `}}` for literal braces. Entered values remain literal text and are never interpreted as more variables or key commands. Variable expansion is off by default, so existing text with braces remains literal.

### Per-snippet typing settings

Leave **Custom for this snippet** unchecked to edit and use your global input mode and delays. Check it to edit that snippet's scan-code/Unicode choice, keystroke delay and initial delay directly in the right-hand panel, then Save. These overrides apply from both the main window and Ctrl+Alt+P, and do not change the global preferences. The initial delay starts after any variable prompts.

### Search from another app

Press **Ctrl+Alt+P**, then type part of a snippet name. Matching ignores case, including German umlauts. The first match is selected automatically.

- Use Up/Down or Page Up/Page Down to navigate, or scroll/select with the mouse.
- Press Enter or double-click to type into the previously active app.
- At most four result rows are visible.
- Escape or clicking outside dismisses the selector; no match cannot start a job.

Search uses saved content and its typing overrides, or the current global settings when no override is enabled. It leaves unsaved editor content alone and is ignored during a session/countdown. Protected snippets request the password at the selector's position.

### Protected snippets

Enable **Encrypt snippet**, then Save. The first protected save creates one master password shared by all protected snippets. Use 12–1,024 characters. By default, every protected operation requires the password; it is not cached.

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

Theme, delays, countdown visibility, line-break choice, session unlock preference, global shortcuts and normal window position are saved per user. Input-method selection returns to Scan-code input on launch.

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
