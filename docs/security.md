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

The entire action document is encrypted. Protected save/load/rename/delete and password changes require the master password. There is no password cache or unlocked session. A password change authenticates the collection, generates new salt/key/nonces, re-encrypts every entry and replaces the vault atomically.

The implementation uses Windows APIs rather than third-party cryptography. References: [authenticated cipher mode](https://learn.microsoft.com/en-us/windows/win32/api/bcrypt/ns-bcrypt-bcrypt_authenticated_cipher_mode_info), [PBKDF2](https://learn.microsoft.com/en-us/windows/win32/api/bcrypt/nf-bcrypt-bcryptderivekeypbkdf2).

## Memory lifetime

Owned passwords, decrypted text, JSON and conversion buffers use move-only allocations, best-effort page locking and `SecureZeroMemory` before release. Key handles are destroyed after each operation. Masked password controls and their undo histories are cleared before the protected operation runs.

Decrypted content remains available while editing. Editor text and undo history are cleared after a typing attempt finishes, stops or fails, or when the protected editor is closed/discarded. The worker clears consumed input data and releases remaining content. Owned key-action data is cleared on destruction.

Cleanup is best effort: Windows controls, input queues, paging, crash dumps and destination apps can retain copies outside these buffers. The app does not erase an external clipboard or protect against malware reading an unlocked process.

## Conversion and backups

Conversion commits the destination before removing the source. On source-removal failure it attempts to remove the new copy. A reported rollback failure or process interruption between file updates can leave both copies; this is not one transaction across both files.

Encrypting previously unencrypted content does not erase old disk sectors, file versions or backups. An older encrypted backup remains protected by its original password. Lost master passwords cannot be recovered.

## Distribution

Release executables use `asInvoker` permissions and are not packed or obfuscated. Global shortcuts use `RegisterHotKey`; deliberate typing uses `SendInput`. The app does not install a low-level keyboard hook, driver, extension or service.

Unsigned binaries can receive reputation warnings. Antivirus results apply to specific hashes/signatures, not all future builds or vendors. Do not disable protection or add exclusions to distribute the app; submit suspected false positives through the vendor's official review process.

Do not post passwords, decrypted snippets or vault files in public reports. A minimal [issue](https://github.com/mkanzler/MK-Autotyper/issues) should identify the affected version and reproduction steps without secret data.
