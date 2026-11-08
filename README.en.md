# MyReader

[中文 README](README.md)

A Windows desktop novel reader: a C++ / Win32 + WebView2 host with a local HTML/JS reading UI. Reads online novel sites and local TXT files, with built-in watermark cleaning, Traditional-to-Simplified Chinese conversion, offline download, and TXT export.

## Features

- **Online reading**: paste a book's table-of-contents URL and start reading. Site adapters are externalized as JSON (`data/sites/*.json`), so adding or fixing a site never requires a rebuild
- **Cloudflare checks**: solve the challenge once by hand in a real browser window; afterwards a hidden browser session fetches TOCs and chapters with an identical fingerprint
- **Watermark cleaning**: character whitelist + ad-line detection removes obfuscated characters and promo lines injected into chapter text
- **Traditional → Simplified**: built-in OpenCC mapping, Simplified by default, "keep original" available
- **Local TXT**: drag & drop or import from the bookshelf; automatic chapter splitting (custom regex supported), encoding detection (UTF-8 / GBK / UTF-16), and relocation if the file moves
- **Offline download**: whole book / next N chapters / custom range, resumable, adjustable pacing, auto-pause on verification
- **TXT export**: range / encoding / titles / Simplified conversion options
- **Reading experience**: themes, font, size, line/paragraph/letter spacing, page width; keyboard-driven scrolling; seamless next-chapter append; progress memory; virtualized TOC (thousands of chapters stay smooth); local cover cache

## Keys

| Key | Action |
|---|---|
| ↓ / ↑ | Scroll one step (configurable) |
| Space / Shift+Space, PgDn / PgUp | Scroll one page down / up |
| ← / → | Previous / next chapter |
| Home / End | Chapter start / end |
| Ctrl+, | Settings |
| F11 | Fullscreen |
| Esc | Exit immersive mode / close panels |

## Download

Grab the zipped build from [Releases](https://github.com/daze0515/MyReader/releases), unzip, and run `Reader.exe`. Requires the [WebView2 Runtime](https://developer.microsoft.com/microsoft-edge/webview2/) (preinstalled on Windows 11 and most Windows 10). Keep the `web/` folder next to the exe.

## Build from source

Requires Windows + Visual Studio (Desktop development with C++).

```bat
:: 1. Fetch the WebView2 SDK and extract it into .deps\wv2
curl -sSL -o .deps\wv2.nupkg https://www.nuget.org/api/v2/package/Microsoft.Web.WebView2/1.0.2903.40
:: A .nupkg is a zip; extract it into .deps\wv2 (e.g. tar -xf .deps\wv2.nupkg -C .deps\wv2)

:: 2. Build
build.bat
```

Output lands at `dist\Reader.exe`.

## Tests

```bat
test.bat
```

Includes C++ unit tests (extraction / adapters / TXT / drag-drop) and frontend smoke scripts (`tests/*.mjs`, run with `READER_DEBUG_PORT=9336`). Scraped sample pages (`testdata/`) are not in the repo for copyright reasons; tests that depend on them skip automatically.

## Layout

```
src/      C++ host: window / WebView2 / fetch scheduling / throttling / proxy / storage
web/      Reading UI (index.html / reader.css / reader.js)
res/      App icon
tools/    make_icon.py icon generator (Pillow)
tests/    Unit tests and CDP debug scripts
PLAN.md   Full design & implementation notes (Chinese)
```

Runtime data (bookshelf, settings, chapter cache, covers) lives in a `data/` folder next to the exe.

## Compliance

This project only re-formats content the user can already access. It does not crack paid content, bypass logins, or bundle book sources, and it throttles, caches, and backs off to be gentle on sites. All site content belongs to its respective owners; for personal study only.

## License

[MIT](LICENSE)
