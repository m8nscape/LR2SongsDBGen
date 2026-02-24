# LR2SongsDBGen

A tool that generates songs.db for Lunatic Rave 2.

Runs about 7x faster than stock on my i7-9700 w/ HDD (WD Blue 3TB, defragmented, win10).

#### Usage

Drag LR2.exe (or LR2_HD.exe) to LR2SongsDBGen.exe

Only supports full rebuild for now. Will automatically create a backup of your previous db.

Regardless of song.db stores informations in UTF-8, LR2 converts them into ANSI, resulting in load fails and silent keysounds if your system locale is not Japanese or Korean (LR2 officially supports these 2 system languages). In that case, use any **x86** Locale simulation tools (e.g.: AppLocale) to run LR2 after generation.

#### Dependencies
```
vcpkg install abseil:x64-windows
vcpkg install re2:x64-windows
vcpkg install tinyxml2:x64-windows
vcpkg install sqlite3:x64-windows
```
