# Wallmane

A custom launcher for Warmane's WotLK 3.3.5a private server. Built with WinUI 3 and C++/WinRT.

---

## What it does

Wallmane replaces the default process of manually navigating to your WoW folder every time you want to play. It sits as a persistent launcher with a dark, atmospheric interface — real-time server status, news from Warmane, one-click addon installs, and Discord Rich Presence when you're in game.

---

## Features

**Home**
- Live realm status pulled directly from Warmane (Onyxia, Lordaeron, Icecrown, Blackrock)
- Latest news and patch announcements from warmane.com
- One-click Play button that launches Wow.exe from your saved path

**Addons**
- Curated list of essential WotLK 3.3.5a addons installed with a single click
- Automatically detects what you already have installed
- Downloads from verified sources and extracts directly into your Interface/AddOns folder
  - pfQuest, Deadly Boss Mods (Warmane fork), Recount, ElvUI, OmniCC

**Settings**
- Browse and save your Wow.exe path — persists between sessions
- One-click cache cleaner
- HD character model patch installer (WoD models backported to 3.3.5a)
- Option to minimize the launcher when the game starts
- WotLK 3.3.5a torrent magnet link

**Discord Rich Presence**
- Automatically connects to Discord when the launcher starts
- Updates your status to show you're playing WotLK on Warmane when you hit Play

**Visuals**
- Atmospheric dark theme with gold accents
- Native GPU-accelerated rain and lightning animation (no WebView, no video files)
- Custom dark titlebar — no white Windows chrome
- Custom app icon

---

## Requirements

- Windows 10 (build 17763) or later
- Windows App Runtime 2.0
- Your own WotLK 3.3.5a client (Wow.exe)

---

## Building

Requires Visual Studio 2022 with the following workloads:
- Desktop development with C++
- Windows application development (WinUI 3 / Windows App SDK)

```
git clone https://github.com/yourusername/wallmane.git
```

Open `wallmane.slnx` in Visual Studio, set the configuration to `Debug x64`, and hit F5.

On first build, Visual Studio will restore NuGet packages automatically including the Windows App Runtime.

---

## Notes

- The addon installer uses Windows' built-in `tar.exe` (available since Windows 10 build 17063) to extract downloads. No third-party dependencies.
- Passwords or account credentials are never stored — the launcher does not interact with Warmane's authentication system.
- The HD patch installer requires a direct `.mpq` download link. The placeholder URL in the source needs to be replaced with a link from the Warmane forums before that feature works.
- Discord Rich Presence connects over a local named pipe. It silently does nothing if Discord is not running.

---

## License

MIT
