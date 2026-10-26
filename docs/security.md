# Security and storage

## Local data

| Data | Normal location |
| --- | --- |
| Unencrypted snippets | `%LOCALAPPDATA%\MK-AutoTyper\Snippets.json` |
| Protected snippets | `%LOCALAPPDATA%\MK-AutoTyper\Encrypted.dat` |
| Preferences | `HKEY_CURRENT_USER\Software\MK-AutoTyper` |
| Optional startup shortcut | Current user's Startup folder, `MK AutoTyper.lnk` |

Files are created on the first relevant save. Names are data inside the collection/vault, not path components. Names remain visible for searching when the body is encrypted. The master password is not stored in files, preferences or logs.

Windows can redirect storage when a packaged app launches AutoTyper. Exit any running tray instance and launch directly from File Explorer to use the normal user location.

Updates flush a temporary file before atomic replacement. Per-store locks serialize writers. Parsing has file, record, action and text bounds; malformed/unsupported collections are not silently overwritten. See the [file format](snippet-format.md).

## Encryption

Protected content uses Windows CNG:

- AES-256-GCM, with fresh random 96-bit nonces and 128-bit authentication tags.
- PBKDF2-HMAC-SHA-256, with a random 128-bit salt and 600,000 iterations.
- UTF-16LE password bytes; new master passwords require 12–1,024 characters.
- Associated data binding records to names and vault format/salt.
- An encrypted manifest digest authenticating the catalog and ciphertext.

The entire action document is encrypted. Protected save/load/rename/delete and password changes require the master password. By default, each operation asks for the password. The optional **Unlock encrypted snippets for this session** preference asks only when encrypted content is first needed; startup and name-only browsing never unlock the vault. Only the preference is saved. The password is never cached. A password change authenticates the collection, generates new salt/key/nonces, re-encrypts every entry and replaces the vault atomically.

The implementation uses Windows APIs rather than third-party cryptography. References: [authenticated cipher mode](https://learn.microsoft.com/en-us/windows/win32/api/bcrypt/ns-bcrypt-bcrypt_authenticated_cipher_mode_info), [PBKDF2](https://learn.microsoft.com/en-us/windows/win32/api/bcrypt/nf-bcrypt-bcryptderivekeypbkdf2).

## Memory lifetime

Owned passwords, decrypted text, JSON and conversion buffers use move-only allocations, best-effort page locking and `SecureZeroMemory` before release. Key handles are destroyed after each operation. In session mode, only an authenticated 256-bit derived key is retained, protected with Windows `CryptProtectMemory` using `CRYPTPROTECTMEMORY_SAME_PROCESS`. Each operation restores a temporary key and re-authenticates the current vault; a changed salt or failed authentication clears the session. No plaintext snippets are cached by the session. See [Windows memory protection](https://learn.microsoft.com/en-us/windows/win32/api/dpapi/nf-dpapi-cryptprotectmemory). Masked password controls and their undo histories are cleared before the protected operation runs.

Decrypted content remains available while editing. Editor text and undo history are cleared after a typing attempt finishes, stops or fails, or when the protected editor is closed/discarded. The worker clears consumed input data and releases remaining content. Owned key-action data is cleared on destruction.

Cleanup is best effort: Windows controls, input queues, paging, crash dumps and destination apps can retain copies outside these buffers. The app does not erase an external clipboard or protect against malware reading an unlocked process.

## Unlocked sessions

**Lock encrypted snippets** in More settings or the tray menu clears the retained key and protected editor/undo content, and stops an active typing job. Unsaved editor changes are confirmed before manual locking. Disabling the preference clears the retained key; the current editor remains available under the normal per-operation rules. Exit clears the key, and each launch starts locked. Closing to the tray, Windows lock, and sleep keep the session unlocked; use Lock encrypted snippets or Exit to end it. A new master password is still confirmed with the current password, and the session is refreshed only after the vault update succeeds.

Session mode is a convenience tradeoff: it retains the ability to decrypt until locked or exited. Windows memory protection and best-effort page locking reduce exposure but cannot prevent malicious code controlling the process from using an unlocked session. A cache failure leaves the vault locked rather than storing an unprotected key. The on-disk encryption format is unchanged.

## Action sequences and variables

The entire action document, including templates and per-snippet settings, is encrypted for a protected snippet. Expanded field values are not written back to either snippet store or preferences. Field names and values use owned wiping buffers; repeated placeholder scans borrow their source without allocating copies. Encrypted field inputs are masked and their undo buffers are cleared. Temporary input values are released before the worker starts; the worker owns the expanded document and clears it before reporting completion, cancellation or failure. Protected inline editor fields, action documents, captured key data and undo histories are cleared together under the existing editor cleanup rules.

Variable values are literal text; they cannot inject key or delay actions or recursively expand placeholders. Expansion has field/value and total text limits. Shortcut recording installs no keyboard hook. It temporarily releases the application hotkeys and keeps local Save/New commands paused until the recorded key is released. Cancelling capture preserves the previous key. Key events are balanced, partial input triggers best-effort releases, and delays remain cancellable. Playback stops on destination-window or keyboard-layout changes. Names remain visible in the catalog as before.

## Conversion and backups

Conversion commits the destination before removing the source. On source-removal failure it attempts to remove the new copy. A reported rollback failure or process interruption between file updates can leave both copies; this is not one transaction across both files.

Encrypting previously unencrypted content does not erase old disk sectors, file versions or backups. An older encrypted backup remains protected by its original password. Lost master passwords cannot be recovered.

## Distribution

Release executables use `asInvoker` permissions and are not packed or obfuscated. Global shortcuts use `RegisterHotKey`; deliberate typing uses `SendInput`. The app does not install a low-level keyboard hook, driver, extension or service.

Unsigned binaries can receive reputation warnings. Antivirus results apply to specific hashes/signatures, not all future builds or vendors. Do not disable protection or add exclusions to distribute the app; submit suspected false positives through the vendor's official review process.

Do not post passwords, decrypted snippets or vault files in public reports. A minimal [issue](https://github.com/mkanzler/MK-Autotyper/issues) should identify the affected version and reproduction steps without secret data.
