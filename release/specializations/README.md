# Specializations

| | |
|---|---|
| Plugin id | `celestialrayone.specializations` |
| DLL | `d2rl-celestialrayone-specializations.dll` |
| Version | 1.14.2 |
| Needs | D2RLoader 1.3.1, on the host and every client |
| Author | CelestialRayOne |

A D2RLoader plugin that adds a specialization system to a Diablo II: Resurrected mod:

- a **Specialization** entry on the character sheet (a **+** while the character has points to spend, a **-** once it has learned a perk and has no point left) and a **Specialization** alert on the HUD,
- a **specialization window** with one tab per specialization and rows of perks, where the character spends specialization points,
- every learned perk saved on the character as a stat you choose,
- **Magic** and **Physical** resistance rows on the character sheet.

The perks do nothing by themselves. Your mod gives each perk stat its meaning: skill formulas, cube recipes, treasure classes, other plugins. The plugin draws the window, keeps the points, applies the rules and checks every request on the host.

## Contents

- [Requirements](#requirements)
- [Files](#files)
- [Installation](#installation)
- [Setup checklist](#setup-checklist)
- [How it works](#how-it-works)
- [itemstatcost.txt](#itemstatcosttxt)
- [Configuration](#configuration)
- [Strings](#strings)
- [The specialization window](#the-specialization-window)
- [Character sheet](#character-sheet)
- [HUD alert](#hud-alert)
- [Giving points](#giving-points)
- [Giving the perks their effects](#giving-the-perks-their-effects)
- [Diagnostics](#diagnostics)
- [Limits](#limits)
- [Building from source](#building-from-source)
- [Eastern Sun Resurrected's setup](#eastern-sun-resurrecteds-setup)

## Requirements

- **D2RLoader 1.3.1.** At load the plugin checks every game function it uses, byte for byte, against the D2RLoader 1.3.1 process image. On any other build it installs nothing and logs `this D2R build does not match; nothing was installed.`
- **The host and every client run the plugin, the same version.** A client only sends requests. The host checks the rules and writes the stats, so a client whose host does not run the plugin cannot learn anything.
- **Mouse and keyboard.** The plugin fills the keyboard/mouse character sheet. The controller sheet is not supported, and perks are learned with the mouse.
- **Your mod's data:** stat rows, strings, art and a few layout widgets, all described below.

## Files

| File | What it is |
|---|---|
| `d2rl-celestialrayone-specializations.dll` | The plugin. |
| `celestialrayone.specializations.toml` | The config. When it is missing, the plugin writes its default config on first start. |
| `SpecializationPanelhd.json` | The specialization window layout built into the plugin. Start your own layout from it. |
| `plugin.cpp`, `plugin.rc`, `SpecializationPanelLayout.inc` | The source. |
| `README.md` | This guide. |

## Installation

`<scope>` is your mod's folder (`mods\<YourMod>\`) for a mod-scoped install, or the game folder for a global one.

1. Copy `d2rl-celestialrayone-specializations.dll` to `<scope>\d2rloader\plugins\`.
2. Put your config at `<scope>\d2rloader\config\celestialrayone.specializations.toml`. Without one, the plugin writes the default config there on first start. Its stat rows are examples: set them to your own before you play.
3. Add the data below to your mod.
4. Start the game, then read `<scope>\d2rloader\logs\celestialrayone.specializations.log` or type `specializations` in the D2RLoader console.

The window's layout is inside the DLL, so the window needs no layout file.

## Setup checklist

1. **itemstatcost.txt:** a points stat and one stat per perk, plus a respec stat and an unlock stat if you use them.
2. **Config:** those stat rows, the perks per row and the point rules.
3. **Strings:** the window title and tab names, the points line, the unlearn line, a title and a description per perk.
4. **Art:** the window's art and one icon sheet per specialization, or your own layout.
5. **Character sheet:** `MagicText`, `PhysicalText`, `SpecializationButton` and `SpecializationTitle` in `characterstatspanelhd.json`.
6. **HUD:** `SpecializationAlert` in `hudpanelhd.json`.
7. **Points:** an item, quest or plugin that raises the points stat, and the level points in the config.
8. **Perk effects:** formulas, recipes, treasure classes or plugins that read the perk stats.
9. **Check:** the log line `itemstatcost.txt checked, all N stats are saved and sent to the client.` and the console command.

## How it works

### Points

- A character's points are its **own** value of `points_stat`: the base value a pSpell, a quest or a plugin writes. Points granted by items do not count.
- Learning a perk costs 1 point. A character **owns** its unspent points plus one per learned perk.
- A character is **unlocked** once it owns any point. With `unlock_stat` set, it is unlocked once its own value of that stat is 1 or more, even if its points were lost.
- Once unlocked, the host keeps the owned points at `unlock_points` plus one for every level in `point_levels` the character has reached, never above `max_points`. It adds the missing points when the character enters a game, levels up, or its points or unlock stat change. Points from other sources count too and are never taken away, except above `max_points`.
- `max_points` caps the owned points, whatever gives them. The host removes unspent points above it when the character enters a game, levels up, gains points or sends a learn or unlearn click.
- With `point_levels = []` the plugin gives no level points, and the cap is only applied on learn and unlearn clicks.

### Learning

A perk can be learned when:

- the character has at least 1 point,
- no perk of another specialization is learned (the first perk picks the specialization for good),
- every row above it has a learned perk of this specialization,
- no perk of its own row is learned,
- every perk stat of its specialization has a saved itemstatcost row.

Learning writes `perk_value` into the perk's stat (the character's own value, saved with it) and takes 1 point. A perk counts as learned while the character's own value of its stat is above 0.

### Unlearning

Shift + click on a learned perk unlearns it when:

- `respec_stat` is set and has a saved itemstatcost row,
- the character's own value of `respec_stat` is at least `unlearn_cost`,
- no perk in a row below it is learned. Only the deepest learned row can be unlearned, so the rows always stay taken in order.

The perk's stat goes back to 0, the point comes back, and `unlearn_cost` is taken from `respec_stat`. The freed row can be learned again, with any of its perks. With every perk unlearned, the character can pick any specialization again.

### Locks

- Once a perk is learned, every other specialization is locked.
- A specialization whose perk stats are missing or not saved stays locked.
- A character holding perks of two specializations (written by something else) has every specialization locked.

### Host and client

The client sends a request (player, specialization, row, perk) over the plugin's network channel. The host finds the player in its game, checks the same rules against its own copy of the character and writes the stats. The new values reach the client by themselves because the stat rows have `Saved` = 1. The host never answers.

## itemstatcost.txt

Every stat the plugin reads or writes needs a row: `points_stat`, every perk stat, `respec_stat` and `unlock_stat`.

| Column | Value | Why |
|---|---|---|
| `Saved` | 1 | Required. The game tells a character's client about a change of its own value only for rows with `Saved` = 1. Without it the host learns the perk but the window and the sheet never see it. |
| `CSvBits` | above 0 | Required. D2RLoader's extended character save writes a character's own stat only then. 8 holds 0 to 255. |
| `CSvParam` | blank | The plugin uses layer 0. |

- Add the rows at the end of the file. Changing `CSvBits` later stops existing characters from loading.
- The perk stats need no description: the window shows the titles and descriptions from the config.
- The plugin checks the rows every time the tables load and logs the result (see [Diagnostics](#diagnostics)).

Eastern Sun Resurrected's rows, as a working reference (other columns blank):

| Stat | *ID | Send Bits | Saved | CSvSigned | CSvBits | Used as |
|---|---|---|---|---|---|---|
| `specializationpoints` | 926 | 12 | 1 | 0 | 12 | `points_stat` |
| `alchemy_1` to `alchemy_15` | 927 to 941 | 8 | 1 | 0 | 8 | perk stats |
| `specializationtokencap` | 942 | 4 | 1 | 0 | 4 | `unlock_stat` |
| `respecpoints` | 237 | 32 | 1 | 0 | 32 | `respec_stat` (also `Signed` 1 and `fCallback` 1) |

## Configuration

`<scope>\d2rloader\config\celestialrayone.specializations.toml`. The comments in the file describe every setting as well.

### `[specializations]`

The Default column is the value in the default config file.

| Key | Default | Allowed | Meaning |
|---|---|---|---|
| `enabled` | `true` | `true`, `false` | Master switch. |
| `points_stat` | `1000` | 0 to 32767 | Required. The stat row of the specialization points. |
| `perk_value` | `1` | 1 or more | The value written into a perk's stat when it is learned. |
| `perks_per_row` | `3` | 1 to 5 | Perks per row, the same for every specialization. A specialization's stats are split into rows of this size. |
| `respec_stat` | `-1` | -1 to 32767 | The stat that pays for unlearning. -1 turns unlearning off. |
| `unlearn_cost` | `5` | 1 or more | How much of `respec_stat` one unlearn takes. |
| `unlearn_hint` | `"@d2r:SpecializationsUnlearnHint"` | text | The gray line under every perk's description while unlearning is on. `%d` is replaced by `unlearn_cost`. |
| `learn_sound` | `"cursor_stat_point_one_hd"` | sounds.txt name | Played when a learn click goes out. An empty text plays nothing. |
| `unlearn_sound` | `"cursor_stat_point_one_hd"` | sounds.txt name | Played when an unlearn click goes out. |
| `point_levels` | `[45, 60, 75, 90]` | list of levels | Each level reached gives one point once the character is unlocked. `[]` turns level points off. |
| `unlock_points` | `1` | 0 to 1000 | The points an unlocked character owns before any point level. |
| `unlock_stat` | `-1` | -1 to 32767 | A stat that keeps the character unlocked once its own value is 1 or more. -1 counts any owned point as unlocked. |
| `max_points` | `5` | 0 to 1000 | Hard cap on owned points (unspent plus learned perks). |

The default stat rows (`1000`, `1001` to `1060`) are examples. Set every row to your own.

### `[[specialization]]`

One block per specialization, in tab order (the first block is `Tab1`), up to 8.

| Key | Meaning |
|---|---|
| `name` | Shown in the log and the console only. The tab's text is in the layout. |
| `stats` | The perk stat rows, row by row: `perks_per_row` stats per row, 1 to 10 full rows. |
| `titles` | One text per perk, in the same order as `stats`. Optional. |
| `descriptions` | One text per perk, in the same order as `stats`. Optional. |

Texts:

- `"@d2r:Key"` is a string from your mod's string files. Any `"@namespace:Key"` is looked up the same way. A key that is not found shows itself, so a typo is visible in game.
- Any other text is shown as written. `\n` starts a new line; `\"`, `\\` and `\t` work too.
- A `titles` or `descriptions` list of another length than `stats` is ignored, and the log says so.

### File format

The plugin reads `[specializations]` and `[[specialization]]` sections, `key = value` lines, arrays over several lines, `#` comments, numbers (`_` allowed) and quoted texts. A bad value keeps its default and is logged. A missing `points_stat` or no `[[specialization]]` block stops the plugin (`the config cannot be used`).

### Example

Eastern Sun Resurrected's config, first block only:

```toml
[specializations]
enabled = true
points_stat = 926
perk_value = 1
perks_per_row = 3
respec_stat = 237
unlearn_cost = 5
unlearn_hint = "@d2r:SpecializationsUnlearnHint"
learn_sound = "cursor_stat_point_one_hd"
unlearn_sound = "cursor_stat_point_one_hd"
point_levels = [45, 60, 75, 90]
unlock_points = 1
unlock_stat = 942
max_points = 5

[[specialization]]
name = "Alchemist"
stats = [
    927, 928, 929,
    930, 931, 932,
    933, 934, 935,
    936, 937, 938,
    939, 940, 941,
]
titles = [
    "@d2r:SpecializationsAlchemist1_1Title", "@d2r:SpecializationsAlchemist1_2Title", "@d2r:SpecializationsAlchemist1_3Title",
    "@d2r:SpecializationsAlchemist2_1Title", "@d2r:SpecializationsAlchemist2_2Title", "@d2r:SpecializationsAlchemist2_3Title",
    "@d2r:SpecializationsAlchemist3_1Title", "@d2r:SpecializationsAlchemist3_2Title", "@d2r:SpecializationsAlchemist3_3Title",
    "@d2r:SpecializationsAlchemist4_1Title", "@d2r:SpecializationsAlchemist4_2Title", "@d2r:SpecializationsAlchemist4_3Title",
    "@d2r:SpecializationsAlchemist5_1Title", "@d2r:SpecializationsAlchemist5_2Title", "@d2r:SpecializationsAlchemist5_3Title",
]
descriptions = [
    "@d2r:SpecializationsAlchemist1_1Desc", "@d2r:SpecializationsAlchemist1_2Desc", "@d2r:SpecializationsAlchemist1_3Desc",
    "@d2r:SpecializationsAlchemist2_1Desc", "@d2r:SpecializationsAlchemist2_2Desc", "@d2r:SpecializationsAlchemist2_3Desc",
    "@d2r:SpecializationsAlchemist3_1Desc", "@d2r:SpecializationsAlchemist3_2Desc", "@d2r:SpecializationsAlchemist3_3Desc",
    "@d2r:SpecializationsAlchemist4_1Desc", "@d2r:SpecializationsAlchemist4_2Desc", "@d2r:SpecializationsAlchemist4_3Desc",
    "@d2r:SpecializationsAlchemist5_1Desc", "@d2r:SpecializationsAlchemist5_2Desc", "@d2r:SpecializationsAlchemist5_3Desc",
]
```

The Blacksmith (946 to 960), Jeweler (961 to 975) and Archaeologist (976 to 990) blocks follow the same pattern.

## Strings

Strings go in your mod's string files (for example `data/local/lng/strings/item-modifiers.json`), with every language.

| Key | Used by | ESR's English text |
|---|---|---|
| `SpecializationsPointsAvailable` | The window's points line. Always this key; `%d` is the unspent points. | `Available Points: %d` |
| `SpecializationsUnlearnHint` | The default `unlearn_hint`; `%d` is `unlearn_cost`. | `Shift + Click to Unlearn. Costs %d Respec Points.` |
| `SpecializationsTitle` | The built-in window's title. | `Specializations` |
| `SpecializationsAlchemist`, `SpecializationsBlacksmith`, `SpecializationsJeweler`, `SpecializationsArchaeologist` | The built-in window's tab labels. | `Alchemist`, `Blacksmith`, `Jeweler`, `Archaeologist` |
| Your perk titles and descriptions | The config's `titles` and `descriptions`. | `SpecializationsAlchemist1_1Title` = `Efficient Remedies`, `SpecializationsAlchemist1_1Desc` = `Increase the effect of healing and mana potions by 20%.` |
| `strClose` | The close button's tooltip. | The game's own string. |
| `CharacterSheetResistanceStat` | The format of the two resistance rows, as for the stock rows. | The game's own string. |

ESR's sheet and HUD layouts also use these strings. The plugin does not read them:

| Key | ESR's English text |
|---|---|
| `SpecializationsSheetEntry` | `Specialization` |
| `SpecializationsOpenTooltip` | `Open Specializations` |
| `CharacterScreenMagicResistance` | `Magic Resistance` |
| `CharacterScreenPhysicalResistance` | `Physical Resistance` |
| `CharacterStatsTooltipMagicResistance` | `Reduces Magic damage taken.` |
| `CharacterStatsTooltipPhysicalResistance` | `Reduces Physical damage taken.` |

The window's texts and the config's texts must be full references (`@d2r:Key`). The stock sheet and HUD layouts take the usual `@Key`.

## The specialization window

### The built-in window

The plugin registers its own layout at `data/global/ui/layouts/celestialrayone.specializations/SpecializationPanelhd.json`. It is Eastern Sun Resurrected's window: 4 specializations of 5 rows of 3 perks, 1279 x 1440 layout units. `SpecializationPanelhd.json` in the package is this layout.

It works as it is when your mod has 4 specializations of 5 rows of 3 perks and ships its art and strings.

Art, under `data/hd/global/ui/`:

| Layout path | File | Size | Frames |
|---|---|---|---|
| `PANEL\Specializations\Background` | `panel/specializations/background.sprite` | 1279 x 1440 | 1 |
| `PANEL\Specializations\Tab` | `panel/specializations/tab.sprite` | 280 x 84 | 3: normal, hovered, pressed |
| `PANEL\Specializations\TabActive` | `panel/specializations/tabactive.sprite` | 280 x 84 | 1 |
| `PANEL\Specializations\TabLocked` | `panel/specializations/tablocked.sprite` | 280 x 84 | 1 |
| `PANEL\Specializations\TabChosen` | `panel/specializations/tabchosen.sprite` | 44 x 44 | 1 |
| `PANEL\Specializations\Socket` | `panel/specializations/socket.sprite` | 176 x 176 | 1 |
| `PANEL\Specializations\RingLearned` | `panel/specializations/ringlearned.sprite` | 176 x 176 | 1 |
| `PANEL\Specializations\Glow` | `panel/specializations/glow.sprite` | 260 x 260 | 1 |
| `PANEL\Specializations\Shade` | `panel/specializations/shade.sprite` | 132 x 130 | 1 |
| `PANEL\Specializations\TipBack1` to `TipBack14` | `panel/specializations/tipback1.sprite` to `tipback14.sprite` | 720 x (100 + 42 x lines) | 1 |
| `Specializations\alchemist`, `blacksmith`, `jeweler`, `archaeologist` | `specializations/<name>.sprite` | 132 x 130 per frame | 15 |
| `PANEL\closebtn_4x` | The game's own close button. | | |

ESR ships a `.lowend.sprite` next to every `.sprite`, as the game's own art does.

Strings: `SpecializationsTitle`, the four tab names, `SpecializationsPointsAvailable` and `strClose` (see [Strings](#strings)).

For any other shape (other numbers of specializations, rows or perks per row) or other art, replace the layout.

### Your own layout

1. Copy `SpecializationPanelhd.json` to your mod as `data/global/ui/layouts/celestialrayone.specializations/SpecializationPanelhd.json`. A file there replaces the built-in layout.
2. Keep the root: `"type": "Panel"` and `"name": "celestialrayone.specializations/SpecializationPanel"`.
3. Write every text as a full reference, `@d2r:Key`. With any other text D2RLoader refuses the layout and the window does not open; the plugin then logs `the window did not open`.
4. Name the widgets as below. Every widget is optional: a missing one is skipped.

Hidden widgets still take mouse input. That is why the pages hold images only, and why the hover areas exist once, at window level, after the pages.

### Widgets

`s` = specialization, `r` = row, `p` = perk, all counted from 1.

| Widget | Where | Type | What the plugin does with it |
|---|---|---|---|
| `Tab<s>` | window | ButtonWidget | Nothing. Its click sends `SpecializationsTab<s>`. |
| `TabActive<s>` | window | image | Shown while specialization `s` is on screen. |
| `TabChosen<s>` | window | image | Shown while `s` has a learned perk. |
| `TabLocked<s>` | window | image | Shown while `s` is locked. |
| `Page<s>` | window | container | Shown while `s` is on screen. Holds images only. |
| `Glow<r>_<p>` | in `Page<s>` | image | Shown while the perk can be learned now. |
| `Learned<r>_<p>` | in `Page<s>` | image | Shown while the perk is learned. |
| `Locked<r>_<p>` | in `Page<s>` | image | Shown while the perk is neither learned nor learnable. |
| `Icon<r>_<p>`, `Socket<r>_<p>` | in `Page<s>` | image | Nothing. The perk's icon and its frame. |
| `Hover<r>_<p>` | window, after the pages | FocusableWidget | Shown for every perk position the specialization on screen has. Hovering it shows `Tip<r>_<p>`. A click on it learns the perk, Shift + click unlearns it. |
| `Perk<r>_<p>` | window | any | Optional. Shown only where a click does something: learnable, or unlearnable while Shift is held. Hovering and clicking it work like `Hover<r>_<p>`. |
| `Tip<r>_<p>` | window | container | The tooltip of that position, shown while it is hovered. |
| `TipTitle` | in `Tip<r>_<p>` | TextBoxWidget | The perk's title. |
| `TipText` | in `Tip<r>_<p>` | TextBoxWidget | The perk's description, broken into lines by the plugin. |
| `TipHint1` to `TipHint13` | in `Tip<r>_<p>` | TextBoxWidget | The gray unlearn line. The plugin shows the one for the text line it starts on. |
| `TipBack1` to `TipBack14` | in `Tip<r>_<p>` | image | The tooltip's background. The plugin shows the one for the tooltip's number of lines. |
| `PointsAvailable` | window | TextBoxWidget | `@d2r:SpecializationsPointsAvailable`, with `%d` the unspent points. |

`TabLabel<s>`, `Title`, `Background`, `Close` and `ClickCatcher` in the built-in layout are plain layout widgets the plugin does not touch.

### Click messages

Set them as a widget's `onClickMessage`. The plugin consumes them.

| Message | What it does |
|---|---|
| `PanelManager:OpenPanel:SpecializationsOpen` | Opens the window, or closes it when it is open. Does nothing while the character has no point and no learned perk. |
| `PanelManager:OpenPanel:SpecializationsClose` | Closes the window. |
| `PanelManager:OpenPanel:SpecializationsTab<s>` | Shows specialization `s`. |
| `PanelManager:OpenPanel:SpecializationsPerk.<r>.<p>` | Learns perk `p` of row `r` of the specialization on screen, or unlearns it with Shift. Optional: a click on a hover area does the same. |

### Tooltip

- The plugin breaks the description into lines itself, for a `TipText` **672 units wide at `$XMediumFontSize`** (it breaks at 640 measured units). Keep that width and that font size, or the line count and the background will be off.
- Lines are **42 units** apart. In the built-in layout `TipText` sits at y 88 inside the tooltip, `TipHint<k>` at y 88 + 42 x k, and `TipBack<n>` is 720 x (100 + 42 x n).
- While `respec_stat` is set, the unlearn line shows under every perk's description, after one empty line, so players always know the cost. When it does not fit on one line, it is broken after its first sentence.
- A tooltip has 1 to 14 lines: the description, the empty line and the unlearn line.
- A text box prints colour codes as letters, so keep them out of the perk texts. The colours come from each text box's font colour.

### Perk icons

- One sprite sheet per specialization, one frame per perk in row order: row 1 perks 1 to 3 are frames 0 to 2, row 2 is frames 3 to 5, and so on.
- Each frame is the size of a skill icon (132 x 130 in ESR's sheets).
- Each `Icon<r>_<p>` image names its sheet and frame:

```json
{ "type": "ImageWidget", "name": "Icon1_2", "fields": { "rect": { "x": 573, "y": 307 }, "filename": "Specializations\\alchemist", "frame": 1 } }
```

### Input

- **Learn:** click a perk: press and release over the same perk, with the game window in front. Swapped mouse buttons are respected.
- **Unlearn:** Shift + click.
- **Close:** Space (not while typing in chat), Escape, the close button, or the open message again.
- The window opens on the chosen specialization's page. Before a choice, it opens on the last one viewed.
- `learn_sound` and `unlearn_sound` play when the click goes out.

## Character sheet

In `characterstatspanelhd.json` (keyboard/mouse). The four widgets must be **direct children of the panel's root** (`CharacterStatsPanel`). The plugin does not find widgets nested deeper.

| Widget | Type | What the plugin does |
|---|---|---|
| `MagicText` | TextBoxWidget | Every frame: the character's `magicresist` (stat 37), capped at 75 + `maxmagicresist` and at most 95. |
| `PhysicalText` | TextBoxWidget | Every frame: `damageresist` (stat 36), capped at the game's physical cap (50). |
| `SpecializationButton` | ButtonWidget | Six frames: 0 to 2 are the + (normal, hovered, pressed), 3 to 5 the - (normal, hovered, pressed). Shown with the + while the character has points, with the - once it has learned a perk and has no point left, hidden and unclickable otherwise. Give it `"onClickMessage": "PanelManager:OpenPanel:SpecializationsOpen"`. |
| `SpecializationTitle` | any | Shown and hidden with the button. |

The two rows use the floor, format and colours of the four stock resistance rows: gold at the cap, red below 0, otherwise the panel's default colour. Like the damage code, they apply no difficulty penalty. The floor and the caps are read live from the game code, so a patch that moves them moves these rows too.

The row labels are your own widgets. ESR's widgets, placed on its taller sheet (move them to fit yours):

```json
{
    "type": "TextBoxWidget",
    "name": "MagicResistanceTitle",
    "fields": {
        "rect": { "x": 93, "y": 1354, "width": 359, "height": 57 },
        "text": "@CharacterScreenMagicResistance",
        "style": {
            "fontColor": "$FontColorWhite",
            "fontFace": "Formal",
            "pointSize": "$SmallPanelFontSize",
            "spacing": "$ReducedSpacing",
            "alignment": { "h": "center", "v": "center" }
        }
    },
    "children": [
        {
            "type": "FocusableWidget",
            "name": "Tooltip",
            "fields": {
                "fitToParent": true,
                "tooltipString": "@CharacterStatsTooltipMagicResistance",
                "tooltipStyle": { "fontStyle": { "options": { "newlineHandling": "standard" } } }
            }
        }
    ]
},
{
    "type": "TextBoxWidget",
    "name": "MagicText",
    "fields": {
        "rect": { "x": 455, "y": 1354, "width": 113, "height": 57 },
        "style": { "pointSize": "$MediumPanelFontSize", "alignment": { "h": "center", "v": "center" } }
    }
},
{
    "type": "TextBoxWidget",
    "name": "PhysicalResistanceTitle",
    "fields": {
        "rect": { "x": 592, "y": 1354, "width": 359, "height": 57 },
        "text": "@CharacterScreenPhysicalResistance",
        "style": {
            "fontColor": "$FontColorWhite",
            "fontFace": "Formal",
            "pointSize": "$SmallPanelFontSize",
            "spacing": "$ReducedSpacing",
            "alignment": { "h": "center", "v": "center" }
        }
    },
    "children": [
        {
            "type": "FocusableWidget",
            "name": "Tooltip",
            "fields": {
                "fitToParent": true,
                "tooltipString": "@CharacterStatsTooltipPhysicalResistance",
                "tooltipStyle": { "fontStyle": { "options": { "newlineHandling": "standard" } } }
            }
        }
    ]
},
{
    "type": "TextBoxWidget",
    "name": "PhysicalText",
    "fields": {
        "rect": { "x": 955, "y": 1354, "width": 113, "height": 57 },
        "style": { "pointSize": "$MediumPanelFontSize", "alignment": { "h": "center", "v": "center" } }
    }
},
{
    "type": "ButtonWidget",
    "name": "SpecializationButton",
    "fields": {
        "rect": { "x": 382, "y": 1571 },
        "filename": "PANEL\\Character_Sheet\\SpecializationButton",
        "normalFrame": 0,
        "hoveredFrame": 1,
        "pressedFrame": 2,
        "disabledFrame": 0,
        "onClickMessage": "PanelManager:OpenPanel:SpecializationsOpen",
        "tooltipString": "@SpecializationsOpenTooltip"
    }
},
{
    "type": "TextBoxWidget",
    "name": "SpecializationTitle",
    "fields": {
        "rect": { "x": 452, "y": 1567, "width": 360, "height": 64 },
        "text": "@SpecializationsSheetEntry",
        "style": {
            "fontColor": "$FontColorWhite",
            "pointSize": "$MediumPanelFontSize",
            "alignment": { "h": "left", "v": "center" }
        }
    }
}
```

ESR's button sprite is `data/hd/global/ui/panel/character_sheet/specializationbutton.sprite`: six frames of 56 x 56.

## HUD alert

`SpecializationAlert` in `hudpanelhd.json`, any clickable widget. The plugin shows it while:

- the character has points,
- the window and the character sheet are closed,
- the window has not been opened since the last new point.

Like the stock New Stats and New Skill alerts, opening the window hides it until the character gets a new point, and a new game shows it again while points are left. Give it `"onClickMessage": "PanelManager:OpenPanel:SpecializationsOpen"`.

ESR's alert uses the game's quest button art:

```json
{
    "type": "ButtonWidget", "name": "SpecializationAlert",
    "fields": {
        "rect": { "x": 176, "y": -164 },
        "filename": "PANEL/HUD_02/quest_button",
        "hoveredFrame": 3,
        "onClickMessage": "PanelManager:OpenPanel:SpecializationsOpen"
    },
    "children": [
        {
            "type": "TextBoxWidget", "name": "Label",
            "fields": {
                "anchor": { "x": 0.5 },
                "rect": { "y": -3 },
                "fontType": "16pt",
                "text": "@SpecializationsSheetEntry",
                "style": {
                    "pointSize": "$XMediumLargeFontSize",
                    "alignment": { "v": "bottom", "h": "center" },
                    "spacing": "$MinimumSpacing",
                    "dropShadow": "$DefaultDropShadow"
                }
            }
        }
    ]
}
```

Without the widget, the plugin logs `hudpanelhd.json has no SpecializationAlert widget, so there is no HUD alert.`

## Giving points

- Points are the character's own value of `points_stat`. Anything can raise it: a usable item's pSpell, a quest, another plugin.
- With `point_levels`, the plugin adds the level points by itself once the character is unlocked. The item that unlocks a character should give `unlock_points` plus one point per point level already passed, so a character that unlocks late is not short. With ESR's values that is 3 points at level 63.
- With `unlock_stat`, that item also raises the unlock stat to 1, and the character stays unlocked even if its points are taken.

ESR's token, the Mystical Tesseract (misc.txt, `pSpell` 4):

| Column | Value |
|---|---|
| `stat1` | `specializationpoints` |
| `calc1` | `(stat('specializationtokencap'.accr)<1)?(1+(stat('level'.accr)>44)+(stat('level'.accr)>59)+(stat('level'.accr)>74)+(stat('level'.accr)>89)):0` |
| `stat2` | `specializationtokencap` |
| `calc2` | `(stat('specializationtokencap'.accr)<1)` |

The first use gives 1 point plus one per level bracket passed (45, 60, 75, 90) and sets the cap stat to 1. Later uses give nothing.

## Giving the perks their effects

The plugin only writes the perk stats. Everything a perk does is up to your mod, for example:

- skill and missile formulas: `(stat('alchemy_3'.accr)>0)?50:0`,
- cube recipes gated on a perk: `op` 6 checks a player stat equal to `value` (`param` = the perk's stat row),
- treasure classes with a `ConditionCalc` on a perk stat,
- other plugins that read the stat.

A learned perk's stat holds `perk_value` (1 by default). An unlearned perk's stat is 0.

## Diagnostics

### Console

Type `specializations` in the D2RLoader console. For example:

```
Specializations 1.14.2: active. Points stat 926, perk value 1, 4 specializations, 3 perks per row.
  itemstatcost: 61 stats, 0 missing, 0 with CSvBits 0, 0 with Saved 0.
  character sheet: hooked, 5210 updates. Floor -100, magic cap 75 + max magic resistance up to 95, physical cap 50.
  you: 1 points, 5 respec points, specialization Alchemist, learned: Alchemist 1.1, Alchemist 2.3.
  window: closed, per-frame refresh idle.
  client: 2 perk clicks, 0 not learnable, 2 sent, 0 dropped. Host: 2 received, 2 learned, 0 refused, 0 for an unknown player.
```

The itemstatcost line counts `points_stat` and the perk stats.

### Log

`<scope>\d2rloader\logs\celestialrayone.specializations.log`

| Message | Meaning |
|---|---|
| `this D2R build does not match; nothing was installed.` | Not D2RLoader 1.3.1, or another plugin already changed one of the places this plugin checks (for example by hooking the character sheet's resistance rows). The lines before it name each place that did not match. |
| `the config cannot be used; nothing was installed.` | `points_stat` is missing or there is no `[[specialization]]` block. The line before it lists the config problems. |
| `stats have no itemstatcost.txt row` | A configured stat row does not exist. |
| `stats have CSvBits 0` | Those stats are not saved: every character comes back without them. |
| `stats have Saved = 0` | The host changes them, but the window and the sheet never see it. |
| `has perk stats without a saved itemstatcost row` | That specialization stays locked until its rows are fixed. |
| `has no saved itemstatcost row; unlearning is off.` | `respec_stat`'s row is missing or not saved. |
| `itemstatcost.txt checked, all N stats are saved and sent to the client.` | All rows are fine. |
| `hudpanelhd.json has no SpecializationAlert widget` | No HUD alert. |
| `the window did not open` | A replaced layout has a text that is not a full `@d2r:Key` reference. D2RLoader's own log names it. |
| `the host refused the plugin channel` | The host does not run this plugin, or runs another version. |
| `the character sheet hook at 0x14E6E70 failed to install` | No Magic, Physical or Specialization rows on the sheet. |
| `by its level: N added.` | Level points given to a player. |
| `over max_points` | Unspent points above the cap were removed. |
| `is hooked by another plugin; calls go through that hook.` | Information only: another plugin hooks a function this one only calls. |

## Limits

- 8 specializations, 1 to 10 rows each, 1 to 5 perks per row (the same for all), one perk per row.
- Stat rows 0 to 32767.
- A tooltip has at most 14 lines.
- The tooltip's line breaking assumes a `TipText` 672 units wide at `$XMediumFontSize`.
- The points line always uses the key `SpecializationsPointsAvailable`.
- Mouse and keyboard only.

## Building from source

- D2RLoader PluginSDK 0.3.0, the SDK of D2RLoader 1.3.1. Include `<D2RLPlugin/api.h>` and link the CMake target `D2RLPlugin::D2RLPlugin`.
- MSVC, x64, C++20. Compile `plugin.rc` into the DLL: it carries D2RLoader's manifest resource and the version info.
- Name the DLL `d2rl-celestialrayone-specializations.dll`.
- The addresses in `plugin.cpp` belong to the D2RLoader 1.3.1 process image. A new D2RLoader build needs them found again.

With the SDK added to your CMake project:

```cmake
add_library(specializations SHARED plugin.cpp plugin.rc)
target_link_libraries(specializations PRIVATE D2RLPlugin::D2RLPlugin)
set_target_properties(specializations PROPERTIES
    OUTPUT_NAME d2rl-celestialrayone-specializations
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON)
```

### The built-in layout

`SpecializationPanelLayout.inc` is generated from `SpecializationPanelhd.json`. The JSON is written with one-space indentation and split at line ends into raw string pieces of at most 12,000 bytes, below MSVC's string literal limit. After you edit the JSON, regenerate the `.inc` and rebuild.

This script, run next to both files, produces the shipped `.inc` byte for byte. It needs strict JSON: game layouts may use trailing commas, this script does not accept them.

```python
import json

layout = json.load(open("SpecializationPanelhd.json", encoding="utf-8-sig"))
text = json.dumps(layout, indent=1, ensure_ascii=False) + "\n"
chunks, chunk = [], ""
for line in text.splitlines(keepends=True):
    if chunk and len((chunk + line).encode("utf-8")) > 12000:
        chunks.append(chunk)
        chunk = ""
    chunk += line
chunks.append(chunk)
head = (
    "// The specialization window layout (data/global/ui/layouts/celestialrayone.specializations/\n"
    "// SpecializationPanelhd.json), compiled into the plugin. Generated; split into pieces below\n"
    "// MSVC's string literal limit. The plugin joins them and registers the result.\n"
    "constexpr const char* const PanelLayoutChunks[] = {\n"
)
body = "".join('R"SPLJ(' + c + ')SPLJ",\n' for c in chunks)
with open("SpecializationPanelLayout.inc", "w", encoding="utf-8", newline="\n") as f:
    f.write(head + body + "};\n")
```

To change the window without rebuilding, ship the edited JSON in your mod instead (see [Your own layout](#your-own-layout)).

## Eastern Sun Resurrected's setup

ESR uses the built-in window and these stat rows:

| | Stat rows |
|---|---|
| Points | `specializationpoints` (926) |
| Alchemist | `alchemy_1` to `alchemy_15` (927 to 941) |
| Blacksmith | `blacksmithing_1` to `blacksmithing_15` (946 to 960) |
| Jeweler | `jeweler_1` to `jeweler_15` (961 to 975) |
| Archaeologist | `archaeology_1` to `archaeology_15` (976 to 990) |
| Unlearning | `respecpoints` (237), 5 per unlearn |
| Unlock | `specializationtokencap` (942), set by the Mystical Tesseract |

- The Mystical Tesseract, made from 10 Mystical Shards, unlocks the system and gives 1 point plus one per level bracket passed.
- The plugin gives one more point at levels 45, 60, 75 and 90, never more than 5 in total.
- The perks' effects are ESR's softcode and its own perk plugins.
