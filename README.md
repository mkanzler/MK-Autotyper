<p align="center"><img src="AutoTyper/AutoTyper.svg" width="72" alt="MK AutoTyper icon"></p>

<h1 align="center">MK AutoTyper</h1>

<p align="center">A lightweight Windows app for typing text, reusing snippets, and protecting sensitive content.</p>

<p align="center"><strong>Windows 10 / 11</strong> &nbsp;·&nbsp; <strong>x64 &amp; x86</strong> &nbsp;·&nbsp; <strong>Portable</strong></p>

<p align="center">
  <a href="https://github.com/mkanzler/MK-Autotyper/releases/download/release-2.0/MK-AutoTyper-v2.0-windows-x64.exe"><strong>Download x64 EXE</strong></a>
  &nbsp;·&nbsp; <a href="https://github.com/mkanzler/MK-Autotyper/releases/download/release-2.0/MK-AutoTyper-v2.0-windows-x86.exe">x86 EXE</a>
  &nbsp;·&nbsp; <a href="https://github.com/mkanzler/MK-Autotyper/releases/tag/release-2.0">ZIPs and source</a>
  &nbsp;·&nbsp; <a href="docs/user-guide.md">User guide</a>
  &nbsp;·&nbsp; <a href="docs/releases/v2.0.md">Release notes</a>
  &nbsp;·&nbsp; <a href="https://github.com/mkanzler/MK-Autotyper/issues">Report an issue</a>
</p>

<picture>
  <source media="(prefers-color-scheme: dark)" srcset="docs/images/app-dark.jpg">
  <source media="(prefers-color-scheme: light)" srcset="docs/images/app-light.jpg">
  <img src="docs/images/app-dark.jpg" alt="MK AutoTyper: text editor, saved snippets, input methods and typing delays" width="1026">
</picture>

## What it does

| Feature | How it helps |
| --- | --- |
| **Type into other apps** | Send text as keystrokes with adjustable start and character delays. Choose scan-code or Unicode input. |
| **Reuse saved snippets** | Save common replies, commands, or text. Find a snippet by name and start it from a keyboard overlay. |
| **Protect sensitive snippets** | Encrypt content with a master password. Unlock it when needed; the password is not remembered. |
| **Start and stop quickly** | Use one configurable global shortcut for clipboard typing and stopping an active session. |
| **Choose your appearance** | Use light mode, dark mode, or follow Windows automatically. |
| **Keep it in the tray** | Close to the system tray or start with Windows in the background. |

Built with native Windows controls. No installer, browser extension, service, or separate C++ runtime installation is required.

## Get started

1. Download the **x64 EXE** for most PCs, or the **x86 EXE** for 32-bit Windows, from the [v2.0 release](https://github.com/mkanzler/MK-Autotyper/releases/tag/release-2.0).
2. Save the EXE in a permanent folder and run it. Optional ZIP bundles contain the same app, license and quick-start guide; extract a ZIP before running **MK-AutoTyper.exe**.
3. Enter text, choose the delays, click **Start typing**, and focus the destination before the countdown finishes.

To use saved snippets, press **Save** in the editor. From another app, press **Ctrl+Alt+P**, search by name, and press **Enter**. The first match is selected automatically.

| Default shortcut | Action |
| --- | --- |
| **Ctrl+Alt+V** | Type clipboard text, or stop the current session |
| **Ctrl+Alt+P** | Search and type a saved snippet |
| **Ctrl+S** | Save the current snippet |
| **Ctrl+N** | Start a new snippet |
| **Ctrl+Tab** | Insert a tab character in the editor |

Change the two global shortcuts in **More settings**. English US/UK and German keyboard layouts are supported. Tab, Enter and Backspace keep their normal behavior in the destination app.

## Light and dark

<table>
  <tr>
    <td width="50%"><img src="docs/images/app-dark.jpg" alt="Dark theme"><br><strong>Dark</strong></td>
    <td width="50%"><img src="docs/images/app-light.jpg" alt="Light theme"><br><strong>Light</strong></td>
  </tr>
</table>

The first launch follows Windows. Your theme, delays, shortcuts, and window position are saved per user.

## Documentation

- [User guide](docs/user-guide.md) — typing, snippets, shortcuts, startup, and troubleshooting.
- [Security and storage](docs/security.md) — encryption, local data, and limitations.
- [Build from source](docs/building.md) — Visual Studio setup and release packaging.
- [Developer documentation](docs/README.md) — architecture, file format, and testing.

## License

Copyright © 2024–2026 Marcel Kanzler. Licensed under [GNU GPLv3](LICENSE), version 3 only (`GPL-3.0-only`). You may use, modify and redistribute the app under those terms. It is provided without warranty. Each executable embeds the license; the release also provides the license file and corresponding source archive.
