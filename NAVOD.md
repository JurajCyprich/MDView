# MD View — návod

MD View je jednoduchý prehliadač a editor súborov **Markdown** (`.md`). Je to jeden súbor `MDView.exe` — nič sa neinštaluje a dá sa skopírovať na iný počítač s Windows 10 alebo 11.

Tento návod zatvoríte klávesom `F1`, `Esc` alebo tlačidlom **Návod**.

- [Otvorenie súboru](#otvorenie-súboru)
- [Čítanie a kopírovanie textu](#čítanie-a-kopírovanie-textu)
- [Hľadanie](#hľadanie)
- [Úprava dokumentu](#úprava-dokumentu)
- [Svetlý a tmavý režim, veľkosť písma](#svetlý-a-tmavý-režim-veľkosť-písma)
- [Aktualizácie](#aktualizácie)
- [Prehľad klávesových skratiek](#prehľad-klávesových-skratiek)
- [Čo aplikácia zobrazí](#čo-aplikácia-zobrazí)
- [Ako to funguje vo vnútri](#ako-to-funguje-vo-vnútri)

## Otvorenie súboru

Súbor otvoríte tromi spôsobmi:

1. **Pretiahnutím** súboru myšou do okna aplikácie.
2. Tlačidlom **Otvoriť** alebo skratkou `Ctrl+O`.
3. Pretiahnutím súboru na ikonu `MDView.exe`, prípadne cez *Otvoriť v programe* v Prieskumníkovi.

Ak sa súbor medzičasom zmenil na disku, `F5` ho načíta znova.

## Čítanie a kopírovanie textu

Text v náhľade sa označuje myšou rovnako ako v bežnom dokumente. Označený text skopírujete do schránky cez `Ctrl+C` alebo pravým tlačidlom myši → **Kopírovať**. `Ctrl+A` označí celý dokument.

Kliknutie na odkaz:

| Typ odkazu | Čo sa stane |
| :-- | :-- |
| Webová adresa (`https://…`) | Otvorí sa v predvolenom prehliadači |
| Kotva (`#nadpis`) | Dokument sa posunie na daný nadpis |
| Iný súbor `.md` | Otvorí sa priamo v MD View |

## Hľadanie

1. Stlačte `Ctrl+F` a napíšte hľadaný text.
2. Všetky výskyty sa zvýraznia žltou a vedľa poľa sa ukáže počítadlo, napríklad `2 / 7`.
3. `Enter` alebo `F3` skočí na ďalší výskyt, `Shift+F3` na predošlý.
4. `Esc` alebo krížik **✕** v poli hľadanie zruší a zvýraznenie zmizne.

Hľadanie nerozlišuje veľké a malé písmená ani diakritiku — `klavesovych` nájde aj „Klávesových".

Hľadá sa v náhľade. Ak máte zapnutý editor a kurzor je v ňom, hľadá sa v editore.

## Úprava dokumentu

Tlačidlo **Upraviť** alebo `Ctrl+E` rozdelí okno na dve časti: vľavo je zdrojový text, vpravo náhľad, ktorý sa pri písaní priebežne obnovuje. Deliacu čiaru medzi nimi možno ťahať myšou.

Editor a náhľad sa **posúvajú spolu**: keď posuniete jeden, druhý ukáže to isté miesto dokumentu. Po zapnutí editora sa otvorí presne tam, kde ste práve čítali.

Ak v náhľade nájdete miesto, ktoré chcete zmeniť, **dvakrát naň kliknite** — v editore sa označí zodpovedajúci riadok a kurzor sa presunie doň.

To isté tlačidlo editor zase **skryje** a ostane čisté zobrazenie.

- `Ctrl+S` uloží zmeny do pôvodného súboru.
- `Ctrl+Shift+S` uloží dokument pod novým názvom.
- `Ctrl+N` začne nový prázdny dokument.
- Neuložené zmeny označuje hviezdička v názve okna; pri zatváraní sa aplikácia opýta, či ich uložiť.

## Svetlý a tmavý režim, veľkosť písma

Tlačidlo **Tmavý** / **Svetlý** alebo `Ctrl+D` prepína farebný režim. Tlačidlá **A−** a **A+** menia veľkosť písma, `Ctrl+0` ju vráti na pôvodnú.

Obe nastavenia si aplikácia pamätá do ďalšieho spustenia.

## Aktualizácie

Pri každom spustení si aplikácia na pozadí overí, či na GitHube nevyšla novšia verzia. Ak áno, v hornom paneli sa objaví modré tlačidlo **Aktualizovať**.

1. Kliknite na **Aktualizovať** a potvrďte otázku.
2. Nová verzia sa stiahne a nahradí súbor `MDView.exe`.
3. Aplikácia sa sama reštartuje a znova otvorí rozčítaný dokument.

Kontrolu spustíte aj ručne skratkou `Ctrl+U` — vtedy aplikácia oznámi aj to, že máte najnovšiu verziu.

Aktualizácia potrebuje pripojenie na internet a `MDView.exe` musí byť v priečinku, do ktorého možno zapisovať. Bez internetu aplikácia funguje normálne ďalej.

## Prehľad klávesových skratiek

| Skratka | Akcia |
| :-- | :-- |
| `Ctrl+O` | Otvoriť súbor |
| `Ctrl+S` | Uložiť |
| `Ctrl+Shift+S` | Uložiť ako |
| `Ctrl+N` | Nový dokument |
| `F5` | Znovu načítať súbor z disku |
| `Ctrl+E` | Zobraziť / skryť editor |
| `Ctrl+D` | Svetlý / tmavý režim |
| `Ctrl+F` | Hľadať |
| `F3` / `Shift+F3` | Ďalší / predošlý výskyt |
| `Ctrl+C` | Kopírovať označený text |
| `Ctrl+A` | Označiť všetko |
| `Ctrl +` / `Ctrl −` / `Ctrl+0` | Väčšie / menšie / pôvodné písmo |
| `Ctrl+U` | Skontrolovať aktualizácie |
| `F1` | Tento návod |
| `Esc` | Zrušiť hľadanie, zavrieť návod |

## Čo aplikácia zobrazí

- Nadpisy `#` až `######`
- **Tučné**, *kurzívu*, ~~preškrtnuté~~ a `kód v riadku`
- Odrážkové, číslované a zaškrtávacie zoznamy vrátane vnorených
- Tabuľky so zarovnaním stĺpcov a zalomením riadku cez `<br>`
- Bloky kódu medzi trojicami spätných apostrofov
- Citácie, vodorovné čiary a odkazy

Obmedzenia:

- Obrázky sa nezobrazujú, namiesto nich je text *[obrázok: popis]*.
- Emoji sú čiernobiele.
- Široké tabuľky sa prispôsobia šírke okna, nemajú vodorovný posuvník.

## Ako to funguje vo vnútri

Windows obsahuje komponent **RichEdit** — ten istý, ktorý používa WordPad. Vie zobraziť formátovaný text s tabuľkami, označovať ho, kopírovať a hľadať v ňom. MD View si preto text nekreslí sám:

1. Súbor sa načíta (UTF-8, UTF-16 alebo stredoeurópske kódovanie).
2. Text sa prejde po riadkoch a rozpozná sa, čo je nadpis, zoznam, tabuľka, citácia, kód alebo odstavec.
3. Vnútri každého bloku sa nájde tučné písmo, kurzíva, kód a odkazy.
4. Z toho sa vygeneruje dokument vo formáte RTF s farbami aktuálneho režimu.
5. RTF sa odovzdá RichEditu, ktorý ho zobrazí.

Pri každej úprave textu, zmene režimu alebo veľkosti písma sa kroky 2 až 5 zopakujú. Vďaka tomu je celá aplikácia jeden malý súbor bez ďalších závislostí.

Zdrojový kód je v súbore `mdview.c`, zostavuje sa skriptom `build.bat`. Text tohto návodu je v súbore `NAVOD.md` a pri zostavení sa vloží priamo do `MDView.exe`.
