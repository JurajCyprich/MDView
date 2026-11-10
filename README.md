# MD View

Jednoduchý prehliadač a editor súborov Markdown pre Windows 10/11. Jeden samostatný súbor `MDView.exe` napísaný v C (Win32), bez inštalácie a bez závislostí.

- svetlý a tmavý režim
- editor so živým náhľadom, ktorý sa dá skryť
- otvorenie súboru pretiahnutím do okna alebo cez `Ctrl+O`
- hľadanie so zvýraznením výskytov
- označovanie a kopírovanie textu
- vstavaný návod (`F1`)

Podrobný popis ovládania je v [NAVOD.md](NAVOD.md).

## Spustenie

Stiahnite `MDView.exe` a spustite ho. Súbor `.md` možno pretiahnuť do okna alebo priamo na ikonu exe.

## Zostavenie

Potrebné sú Visual Studio Build Tools (MSVC) a Windows SDK. Cestu k `vcvars64.bat` v `build.bat` upravte podľa svojej inštalácie.

```bat
build.bat
```

| Súbor | Účel |
| :-- | :-- |
| `mdview.c` | celý zdrojový kód |
| `mdview.rc` | vloženie `NAVOD.md` do exe |
| `app.manifest` | vzhľad ovládacích prvkov a DPI |
| `build.bat` | zostavenie |
