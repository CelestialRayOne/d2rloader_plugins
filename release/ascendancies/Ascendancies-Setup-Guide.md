# Ascendancies: setup guide

`celestialrayone.ascendancies` is a D2RLoader plugin that turns ascendancies into a character feature with its own window. It was written for Eastern Sun Resurrected (ESR), and every file ESR uses ships with this package as the working reference.

## What it does

- **Choosing:** a consumable (ESR's Orb of Creation) sets a saved stat. That opens the Ascendancy window and shows a + entry on the character sheet. The window has one page per ascendancy: a portrait, the five tiers, and how to reach the next tier. **Ascend** picks the ascendancy on screen and grants tier 1.
- **Tiers 2 to 5:** they unlock on their own, in any order, from triggers in the TOML:
  - **Boss kills:** checked for every player in the boss's level when the boss dies.
  - **Soulstones:** held soulstones are absorbed into a count, and reaching the cap grants a tier.
- **Bonuses:** each tier is a list of properties, written like cube recipe mods. On the host, the plugin applies them into the stat list of one hidden state, using the game's own property code. Auras and procs work as they do from an item.
  - The list is rebuilt in every game, whenever a tier changes and after a resurrect.
  - Only four small stats are saved on the character.
- **New tier notice:** a "New ascension tier unlocked" notice shows on the HUD for a few seconds. Its button opens the window.
- **Renounce Ascendancy:** asks for confirmation, then removes the ascendancy, its tiers and the orb stat. By default it also clears the soulstone count and the Lich King bonus.
- **Optional extras:**
  - A bonus earned from a specific kill (ESR's Lich King bonus).
  - Kill credit for cube recipes that read a "just killed" stat.
  - Migration of old ascendancy items.
- **Console command:** `ascendancies` prints the status, a check of the data rows, the local character's ascendancy, and counters.

## Requirements

- **D2RLoader 1.3.1**, with the same plugin version on the host and every client.
- **PluginSDK 0.3.0** to build the DLL (the SDK that ships with D2RLoader 1.3.1).
- **Mouse and keyboard.** There is no controller layout yet.
- **A mod-scoped install** (`mods/<YourMod>/d2rloader/...`) is recommended.

## What is in this package

```
celestialrayone-ascendancies/
├── Ascendancies-Setup-Guide.md            this guide
├── mods/Eastern_Sun_Resurrected/          ESR's install, path for path
│   ├── d2rloader/
│   │   ├── config/celestialrayone.ascendancies.toml
│   │   └── plugins/                       d2rl-celestialrayone-ascendancies.dll goes here (build it, step 1)
│   └── Eastern_Sun_Resurrected.mpq/data/
│       ├── global/excel/                  ESR's tables for this feature (see step 2)
│       ├── global/ui/layouts/             ESR's character sheet and HUD layouts (see step 3)
│       └── hd/global/ui/
│           ├── panel/ascendancies/        window, button, dialog and notice art
│           └── ascendancies/              one portrait per ascendancy
├── plugin-source/plugins/ascendancies/    plugin.cpp, plugin.rc, AscendancyPanelLayout.inc
└── extras/
    ├── AscendancyPanelhd.json             the window layout (already built into the DLL)
    ├── strings/ascendancy-strings.json    every string the plugin and the layouts use
    └── excel-rows/                        ESR's rows for itemstatcost, states and misc
```

For your own mod, rename `Eastern_Sun_Resurrected` to your mod's folder. Treat ESR's full tables and layouts as a reference: copy the rows and blocks below into your own files rather than replacing them.

## Step 1: build and install the plugin

1. Put the three files from `plugin-source/plugins/ascendancies/` into a `plugins/ascendancies/` folder of a D2RLoader plugin project. ESR's project is public at github.com/CelestialRayOne/d2rloader_plugins, and its root CMakeLists already has the helper.
2. Add `d2rl_add_plugin(ascendancies)` to the root `CMakeLists.txt`.
3. Build from the x64 Native Tools Command Prompt for Visual Studio:
   ```
   cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
   cmake --build build --target d2rl-celestialrayone-ascendancies
   ```
4. Copy `d2rl-celestialrayone-ascendancies.dll` to `mods/<YourMod>/d2rloader/plugins/`. Plugin DLLs sit flat in that folder.
5. Copy `celestialrayone.ascendancies.toml` to `mods/<YourMod>/d2rloader/config/`.
   - If the TOML is missing, the plugin writes its built-in copy there on first start. That copy is identical to the one in this package.
   - The plugin writes its log to `mods/<YourMod>/d2rloader/logs/celestialrayone.ascendancies.log`.

The plugin is a Shared plugin: it runs on the host and on clients, and D2RLoader records it with saved characters. Close the game before you replace the DLL.

## Step 2: data tables

ESR's exact rows are in `extras/excel-rows/`, with their headers.

### itemstatcost.txt: four saved stats

These stats live on the character, not on items. Add new rows at the end of the file and put their row ids in the TOML.

| Stat (ESR name) | TOML setting | Meaning |
| --- | --- | --- |
| `ascendancy` | `ascendancy_stat` | the chosen ascendancy: its 1-based position among the `[[ascendancy]]` blocks, 0 for none |
| `ascendancy_progress` | `progress_stat` | bits 0 to 4: tiers 1 to 5; bit 5: the Lich King bonus; bit 6: has ascended at least once |
| `ascendancy_soulstones` | `soulstone_stat` | soulstones absorbed, 0 up to `soulstone_cap` |
| `ascendancy_orb` | `orb_stat` | 1 once the Orb of Creation is consumed; Renounce sets it back to 0 |

- **Columns:** each row needs `Saved` = 1, `CSvBits` above 0 (ESR uses 8; the progress stat needs at least 7) and `CSvSigned` = 0. Leave `CSvParam` blank. ESR also sets `Send Bits` = 8 so clients see the values.
- **Checked on every load:** the plugin checks these rows each time the tables load and reports the result in its log and in the console command.

### states.txt: the bonus state

- **What it is:** one state holds the bonus list. It needs no flags; ESR's row is a plain `ascendancy` state at row 698.
- **TOML:** put its row id in `bonus_state`. Nothing in it is saved.

### misc.txt: the item that unlocks the choice

The plugin only watches the orb stat. Anything that sets it to 1 or more works, and the window opens by itself the moment it rises from 0. ESR's Orb of Creation (`u16`) is a usable item, set up like its Mystical Tesseract:

| Column | Value |
| --- | --- |
| `useable` | 1 |
| `type` | `ques` |
| `pSpell` | 4 |
| `stat1` | `ascendancy_orb` |
| `calc1` | 1 |
| `UsageConditionCalc` | `(stat('ascendancy_orb'.accr)<1)`, so a second orb cannot be used while the first counts |

ESR crafts the orb in cubemain.txt from five elemental orbs: "Orb of Creation: 5 Orb of Elements".

### misc.txt: the soulstone item

- **The item:** any item code works; set it in `soulstone_item`. ESR uses the Primordial Soulstone, `u19`.
- **Absorbing:** the host absorbs every soulstone the character holds, in the inventory, the Horadric Cube, the personal stash or on the cursor. It removes the item and raises the count, up to `soulstone_cap`.
- **At the cap:** the character gets `soulstone_tier` (ESR: tier 4 at 10 soulstones).
- **Turning it off:** set `soulstone_tier` to 0 if soulstones should grant nothing.

### skills.txt: tier effects that scale

Fixed bonuses are plain properties in the TOML, such as `str=60`. Bonuses that scale with the character are aura skills:

- **Granting:** a tier grants the aura with the property `aura-hidden[<skills.txt row>]=1`. ESR uses one skill per ascendancy for tier 3 (rows 787 to 801) and one for tier 4 (rows 1508 to 1522).
- **Aura setup:** `srvdofunc` 65 with an `aurastate`, and the effects in `aurastat`/`aurastatcalc` or `passivestat`/`passivecalc`.
- **Lich King bonus:** ESR's formulas read the `lichkingbonus` stat for a 10% total multiplier, written as `*(100+stat('lichkingbonus'.accr)*10)/100`. It is applied before the final division, so it moves breakpoints rather than rounding away.
- **Life and mana:** formulas that read life or mana must remember that `maxhp`, `hitpoints` and `maxmana` are in 256ths.

### properties.txt and propertygroups.txt

- **Tier properties:** the TOML uses properties.txt codes, and propertygroups.txt codes work too. Any code your mod has can be used.
- **ESR's markers:** ESR's tiers also set `ascendancy-tier-2` to `ascendancy-tier-5` (stats 676 to 680). Its softcode and the old-item migration read those.
- **Lich King bonus:** ESR's version is the property `lich-king-bonus` (stat `lichkingbonus`, 826).

### cubemain.txt and monprop.txt

- **No recipes:** the plugin needs no cube recipes. If your mod upgraded ascendancies with recipes before, remove them.
- **Kill credit:** this replaces "just killed" curses cast through `monprop.txt` death skills. Remove those curses too, once the `[[kill_credit]]` rows in the TOML cover every recipe that still reads the stat (see step 6).

## Step 3: layouts

### Character sheet (characterstatspanelhd.json)

Add two widgets. The plugin shows and hides them itself:

- **Hidden** until the orb is consumed.
- **A +** while the character can choose an ascendancy.
- **A -** for good once it has ascended.

```json
{
    "type": "ButtonWidget",
    "name": "AscendancyButton",
    "fields": {
        "rect": { "x": 640, "y": 1571 },
        "filename": "PANEL\\Character_Sheet\\SpecializationButton",
        "normalFrame": 0, "hoveredFrame": 1, "pressedFrame": 2, "disabledFrame": 0,
        "onClickMessage": "PanelManager:OpenPanel:AscendanciesOpen",
        "tooltipString": "@AscendancyOpenTooltip"
    }
},
{
    "type": "TextBoxWidget",
    "name": "AscendancyTitle",
    "fields": {
        "rect": { "x": 710, "y": 1567, "width": 360, "height": 64 },
        "text": "@AscendancySheetEntry",
        "style": {
            "fontColor": "$FontColorWhite",
            "pointSize": "$MediumPanelFontSize",
            "alignment": { "h": "left", "v": "center" }
        }
    }
}
```

- **Button art:** the button needs six frames: + (normal, hovered, pressed), then - (normal, hovered, pressed). ESR reuses its Specialization button art.
- **Positions:** fit them to your sheet.
- **Overlaps:** a hidden widget of another panel still catches clicks. If only part of the + reacts, something hidden is lying over it. ESR's Specialization HUD alert did, until that plugin disabled it while hidden.

### HUD (hudpanelhd.json)

Add the notice container with its button. ESR centres it on the HUD:

```json
{
    "type": "Widget", "name": "AscendancyNotice",
    "fields": { "rect": { "x": 1056, "y": -420, "width": 840, "height": 226 } },
    "children": [
        { "type": "ImageWidget", "name": "Background",
          "fields": { "rect": { "x": 0, "y": 0 }, "filename": "PANEL\\Ascendancies\\Notice" } },
        { "type": "TextBoxWidget", "name": "Text",
          "fields": { "rect": { "x": 40, "y": 22, "width": 760, "height": 70 },
                      "text": "@AscendancyNoticeText",
                      "style": { "fontColor": { "r": 255, "g": 165, "b": 0, "a": 255 },
                                 "pointSize": "$XMediumLargeFontSize",
                                 "alignment": { "h": "center", "v": "center" },
                                 "dropShadow": "$DefaultDropShadow" } } },
        { "type": "ButtonWidget", "name": "AscendancyNoticeButton",
          "fields": { "rect": { "x": 280, "y": 112 }, "filename": "PANEL\\Ascendancies\\Button",
                      "hoveredFrame": 1, "pressedFrame": 2,
                      "onClickMessage": "PanelManager:OpenPanel:AscendanciesNotice" },
          "children": [
            { "type": "TextBoxWidget", "name": "Label",
              "fields": { "rect": { "x": 0, "y": 0, "width": 280, "height": 84 },
                          "text": "@AscendancyNoticeView",
                          "style": { "fontColor": { "r": 214, "g": 180, "b": 128, "a": 255 },
                                     "pointSize": "$SmallMediumFontSize",
                                     "alignment": { "h": "center", "v": "center" },
                                     "dropShadow": "$DefaultDropShadow" } } } ] }
    ]
}
```

The plugin hides the notice and disables its button while it is not showing.

### The Ascendancy window

- **Built in:** the window layout is part of the DLL, so you only need to touch it to change the window.
- **Replacing it:** put an edited copy of `extras/AscendancyPanelhd.json` in your mod at `data/global/ui/layouts/celestialrayone.ascendancies/AscendancyPanelhd.json`. Your mod's copy wins over the built-in one.
- **Text references:** every text in it must be a full reference such as `@d2r:Key`. A plugin layout with an unqualified reference is not loaded.
- **Widgets the plugin uses:**
  - `Name` and `Footer`.
  - `Portraits`, holding `Portrait1` to `PortraitN`, one per `[[ascendancy]]` block, in order.
  - The text columns `L` and `R`. Each holds the slots `L1`, `L2`, ... and `R1`, `R2`, ..., and each slot has the text boxes `HeadOn`, `HeadOff`, `LineOn` and `LineOff`. A page uses the column on the side opposite its portrait.
  - `Action` and `ActionLabel`.
  - `Confirm`, holding `ConfirmLine1` to `ConfirmLine5`, `ConfirmYes` and `ConfirmNo`.
- **Click messages:** `PanelManager:OpenPanel:` followed by `AscendanciesOpen`, `AscendanciesClose`, `AscendanciesPrevious`, `AscendanciesNext`, `AscendanciesAction`, `AscendanciesYes`, `AscendanciesNo` or `AscendanciesNotice`.
- **Closing:** Space and Escape close the window.

## Step 4: art

`.lowend.sprite` files are the same pictures at half size.

| File (under `data/hd/global/ui/`) | Size | Frames |
| --- | --- | --- |
| `panel/ascendancies/background.sprite` | 1630 x 1724 | 1 |
| `panel/ascendancies/button.sprite` | 280 x 84 | 3: normal, hovered, pressed |
| `panel/ascendancies/buttonwide.sprite` | 500 x 84 | 3: normal, hovered, pressed |
| `panel/ascendancies/dialog.sprite` | 760 x 380 | 1 |
| `panel/ascendancies/notice.sprite` | 840 x 226 | 1 |
| `ascendancies/<name>.sprite` | 500 x 750 | 1, one portrait per ascendancy |

- **Portrait names:** the ascendancy's name in lower case without spaces, for example `stancedancer.sprite`.
- **New ascendancies:** a new ascendancy needs its own `PortraitN` widget in the window layout.
- **Missing art:** if a sprite the window uses is missing, the window does not open. The log then says `NotFound` and names the folders to check.

## Step 5: strings

- **Where they go:** the plugin's own texts are game strings (`@d2r:Key` in the TOML and the window, `@Key` in the sheet and HUD layouts). Put them in one of your mod's usual string files; ESR uses `data/local/lng/strings/item-modifiers.json`.
- **This package's file:** `extras/strings/ascendancy-strings.json` holds every one, in all 13 languages, with ESR's ids (mostly 30992 to 31109; the reused tier descriptions keep their older ids):
  - the window texts;
  - the sheet and HUD texts;
  - the names and tier lines of all 15 ascendancies;
  - the in-game tier descriptions the default TOML reuses (`battlemagetierone`, ...).
- **Ids:** native string files need unique numeric ids. If ESR's clash with yours, give them free ids of your own.
- **Plain text:** a TOML text can also be plain text instead of a reference.
- **Line breaks:** the window wraps every line to its column. Long descriptions read best as one line without breaks.
- **Bottom-up strings:** item tooltip strings store their lines bottom-up. A `^` in front of a reference (`"^@d2r:Key"`) makes the window show such a string's lines top-down.

## Step 6: the TOML

The TOML documents every setting in its comments. The main parts:

### [ascendancies]

| Setting | Use |
| --- | --- |
| `ascendancy_stat`, `progress_stat`, `soulstone_stat`, `orb_stat` | the four itemstatcost rows from step 2 |
| `bonus_state` | the states.txt row from step 2 |
| `soulstone_item`, `soulstone_cap`, `soulstone_tier` | the soulstone rule |
| `renounce_clears_soulstones`, `renounce_clears_lich_king` | what Renounce takes besides the ascendancy |
| `class_skill_properties` | the class-skill property of each class, in charstats.txt order. The tier property `class-skills` uses the character's own one |
| `lich_king` | properties the Lich King trigger adds |
| `kill_check_stat`, `kill_credit_frames` | kill credit (below) |
| `legacy_*` | migration of old ascendancy items (below) |
| `tier_header`, `next_tier`, `lich_king_tip`, `ascend_label`, `renounce_label`, `confirm_text` | window texts |
| `notice_seconds` | how long the HUD notice stays (0: off) |
| `ascend_sound`, `renounce_sound` | sounds.txt rows |

### [[ascendancy]]

One block per ascendancy, in window order:

- **`title`:** the page title.
- **`image_side`:** `left` or `right`; the text takes the other side.
- **`tier1` to `tier5`:** property lists, such as `"str=60"`, `"aura-hidden[787]=1"`, `"class-skills=1"` or `"code[param]=min..max"`.
- **`text1` to `text5`:** the window lines for each tier.
- **`legacy_unique`:** the uniqueitems.txt row of an old ascendancy item, for migration.

The order is saved with every character, as `ascendancy_stat`. Never reorder or remove a block; add new ones at the end.

### [[trigger]]

What unlocks tiers 2 to 5. Every living player in the level of the monster that dies is checked.

- **`tier` and `monsters`:** the tier it grants, and a list of monstats.txt ids.
- **`difficulty`:** 0, 1 or 2. Optional; leave it out for any difficulty.
- **`all_within_frames`:** every listed monster must die within this many frames, with the player in the level. ESR uses it for the uber trio.
- **`lich_king = true`:** grants the `lich_king` properties instead of a tier.
- **`min_maxhp`:** the monster's maximum life, in 256ths, must be above it. ESR uses 2,000,000,000 for the players 8 Lich King.

### [[kill_credit]]

Optional. For cube recipes that read a "just killed" stat (ESR's Kill Ledger): when a listed monster dies, every living player in its level gets `state` with `kill_check_stat` set to `value`, for `kill_credit_frames`.

- **`value_above` with `maxhp_above`:** gives a different value for a stronger version of the monster.
- **`skill` and `level`:** the curse skill the old system used.
- **Turning it off:** remove the blocks if your mod has no such recipes.

### Old ascendancy items (optional)

For mods that used to give ascendancies as items:

- **When:** a character with no ascendancy enters a game holding a `legacy_stone_item`.
- **What happens:** the item turns into the saved stats, then is removed. Its uniqueitems row picks the ascendancy through `legacy_unique`. `legacy_tier_stats`, `legacy_soulstone_stat` and `legacy_lich_king_stat` carry the progress over.
- **Keep the item rows:** don't delete the old item's misc.txt and uniqueitems.txt rows while characters may still hold one.

## Checking that it works

- **Console:** type `ascendancies`. It prints:
  - whether the plugin is active, and any config problems;
  - the row check: every saved stat and the bonus state;
  - your ascendancy, tiers, orb, soulstones and Lich King bonus;
  - whether the bonus list is on your character;
  - host and request counters, and the last window-open result.
- **Log:** `d2rloader/logs/celestialrayone.ascendancies.log` explains anything that did not work, such as a missing sprite, a config line it did not understand, or a click it ignored.
- **The window does not open:** the log names the reason. `NotFound` means a missing sprite or an invalid layout.
- **Stats are lost after a restart:** check `Saved` and `CSvBits` on the four rows.

## Multiplayer

- **Host:** applies the bonuses, checks the triggers and absorbs soulstones.
- **Clients:** send Ascend and Renounce as requests over the plugin's network channel.
- **Versions:** every player needs the same plugin version.
