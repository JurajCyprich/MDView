# MD View

Jednoduchý prehliadač a editor súborov Markdown pre Windows 10/11. Jeden samostatný súbor `MDView.exe` napísaný v C (Win32), bez inštalácie a bez závislostí.

- svetlý a tmavý režim
- editor so živým náhľadom, ktorý sa dá skryť
- otvorenie súboru pretiahnutím do okna alebo cez `Ctrl+O`
- hľadanie so zvýraznením výskytov, bez ohľadu na diakritiku
- označovanie a kopírovanie textu
- vstavaný návod (`F1`)
- aktualizácia priamo z aplikácie (tlačidlo **Aktualizovať** alebo `Ctrl+U`)

Podrobný popis ovládania je v [NAVOD.md](NAVOD.md).

## Spustenie

Stiahnite `MDView.exe` a spustite ho. Súbor `.md` možno pretiahnuť do okna alebo priamo na ikonu exe.

## Zostavenie

Potrebné sú Visual Studio Build Tools (MSVC) a Windows SDK. Cestu k `vcvars64.bat` v `build.bat` upravte podľa svojej inštalácie.

```bat
build.bat
```

## Vydanie novej verzie

Aplikácia si novú verziu hľadá v najnovšom vydaní (GitHub Releases) tohto repozitára. Aby ju našla:

1. zvýšte `APP_VERSION` v `mdview.c` a spustite `build.bat`,
2. vytvorte vydanie s tagom v tvare `v1.2`,
3. priložte k nemu súbor s názvom presne `MDView.exe`.

| Súbor | Účel |
| :-- | :-- |
| `mdview.c` | celý zdrojový kód |
| `mdview.rc` | vloženie ikony a `NAVOD.md` do exe |
| `mdview.ico` | ikona programu |
| `app.manifest` | vzhľad ovládacích prvkov a DPI |
| `build.bat` | zostavenie |
