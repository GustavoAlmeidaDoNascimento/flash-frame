# Flash Frame

A small always-on-top flashcard window for Windows. It stays above other programs, lives in the notification area, and reviews cards with Anki's SM-2 scheduler.

Text decks are kept inside a 20 MB working-set budget. Decks with images or audio are allowed only after you confirm, because they make the process heavier. The window is about 440 by 320 pixels, dark, and pinned to a corner of the desktop.

## Review

Flash Frame uses the same SM-2 defaults as Anki's classic scheduler, not FSRS.

- New cards step through 1 minute, then 10 minutes. **Good** on the last learning step graduates the card at 1 day. **Easy** graduates it at 4 days.
- Review cards use Again, Hard, Good, and Easy. Ease starts at 2.50 and never drops below 1.30.
- **Again** on a review card sends it to a 10 minute relearning step and shortens the interval.
- The queue is due learning cards, then reviews (200 a day), then new cards (20 a day).

Buttons show the next delay (`1m`, `10m`, `4d`). Space reveals the answer, then grades **Good**. After the answer is visible, `1`–`4` are Again, Hard, Good, and Easy.

## Memory

The 20 MB figure is a working-set budget for text, not a hard cap that kills the process. A Windows window has to map fonts and system libraries, so a hard 20 MB limit would crash a normal GUI. On an idle text collection the process sits around 18 MB.

What the limiter actually does:

- SQLite's soft heap is 2 MB for text decks and 32 MB after you accept media.
- Only the current card is decoded. Images are scaled to at most 640×480.
- If the working set passes the budget, the app releases SQLite's cache and asks Windows to trim the process, at most once every 10 seconds.
- Text mode refuses to decode images. Media mode raises the budget to 256 MB.

The header shows the current working set. It turns amber when the budget is exceeded.

## Anki decks

**Import deck** reads a `.apkg` file, including shared decks from [AnkiWeb](https://ankiweb.net/shared/decks/). The importer prefers `collection.anki21b` (zstd), then `collection.anki21`, then a real `collection.anki2`. The compatibility stub that only says "please update Anki" is ignored.

Before a deck with `<img>` tags or `[sound:...]` is written, Flash Frame lists the affected cards and explains that the process will get heavier. **Load deck** continues. **Don't show this again** skips the warning next time and still marks those decks as media decks. The same warning appears when you add an image or audio file to a card you create yourself.

Creating a card or importing a deck never uploads anything. Media files are copied under `%APPDATA%\FlashFrame\media\<deck id>\`.

## Window, tray, startup

The window is a small borderless tool window, always on top, with no taskbar button. Drag the header to move it. **−** and **×** both hide it. The tray menu is the only **Quit**.

Windows chooses whether a new tray icon starts in the overflow (the hidden icons). Flash Frame cannot force that placement. Left-click the icon to show or hide the window.

The first launch registers Flash Frame in `HKCU\Software\Microsoft\Windows\CurrentVersion\Run`. Turn that off from the tray menu. A second launch focuses the window that is already running.

Click the deck name to switch decks. `D` does the same, `I` imports, `N` adds a card, and Esc hides the window.

## Build

Windows, Visual Studio 2022 Build Tools (the C compiler is `cl`). From the repository root:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\tools\fetch_deps.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File .\build.ps1 tests
powershell -NoProfile -ExecutionPolicy Bypass -File .\build.ps1 import
powershell -NoProfile -ExecutionPolicy Bypass -File .\build.ps1 app
```

`build.ps1 tests` builds the scheduler and text tests. `import` builds the `.apkg` tests. `app` writes `build\flash-frame.exe`.

Dependencies are downloaded into `third_party/` and are not committed:

| Piece | Role | License |
| --- | --- | --- |
| SQLite amalgamation | collection database | public domain |
| zstd | `anki21b` decompression | BSD |
| miniz 2.2 | zip reader | unlicense |
| cJSON | legacy Anki JSON | MIT |

## Layout

```
src/scheduler.c    SM-2
src/textutil.c     HTML, cloze, templates
src/storage.c      SQLite collection
src/apkg.c         .apkg import
src/win/           always-on-top window, tray, audio, images
tests/             scheduler tests and tiny .apkg fixtures
```

The collection is `%APPDATA%\FlashFrame\collection.db`. Each deck row has a `kind` column, today always `local`, so a future account can mark a deck as cloud without a migration.

## Roadmap

**Linux.** The core (scheduler, text, storage, import) is C and keeps the Windows path code behind `_WIN32`. A Linux build still needs a window: always-on-top is reliable on X11 and limited on Wayland, and startup would be a `.desktop` autostart entry plus a tray icon. That window is not built yet.

**Flash Frame account.** Text decks would sync to an account, automatically, on every change. Each deck would be either cloud or local. Images and audio would stay on the machine. There is no server in this repository.

## Português

Flash Frame é uma janela pequena de flashcards, sempre por cima das outras, na área de notificação do Windows. A revisão segue o SM-2 do Anki: Again, Hard, Good e Easy, com cartas novas, em aprendizado, revisão e reaprendizado.

Baralhos só de texto ficam num orçamento de cerca de 20 MB de working set. O programa não se mata ao passar disso: ele limita o cache do SQLite, decodifica uma carta por vez e pede ao Windows para devolver memória. Imagem e áudio, num `.apkg` ou numa carta nova, só entram depois de um aviso que lista as cartas afetadas. Dá para marcar **não mostrar novamente**.

A primeira execução entra na inicialização do Windows. Fechar a janela esconde o programa; sair é pelo ícone da bandeja. O Windows decide se esse ícone nasce entre os ícones ocultos.

O Linux e a conta online ficam para depois. No Linux, ficar sempre visível é confiável no X11 e limitado no Wayland. A conta sincronizaria só baralhos de texto, no automático, e cada baralho seria da nuvem ou só local. A mídia continua na máquina.

## License

MIT. Copyright (c) 2026 Gustavo Almeida do Nascimento. See [LICENSE](LICENSE).
