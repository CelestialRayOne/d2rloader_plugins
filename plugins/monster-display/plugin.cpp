// Monster Display
//
// Replaces what the stock monster health panel shows with, top to bottom:
//   - the monster name (stock)
//   - a framed health bar, twice as wide for prime evils, with exact life and
//     a smaller percentage after max life
//   - "Level X <type>" (the stock type / description line with the level in
//     front of it)
//   - the unique affixes, with Aura Enchanted on its own row showing the aura
//     name and its final level (both auras for Heralds)
//   - the resistances the hovering player's hits actually meet, including the
//     Summon Resist Cap for allied monsters
//   - the immunity line, built the way the game builds it but from the same
//     values as the resistance row, so the Summon Resist Cap is respected
//
// 1.0.18: the nameplate's Name and NameBold move with the shrunk bar's top
//   edge, so they keep their layout distance above the bar instead of
//   floating half the stock bar height higher.
//
// 1.0.17: the controller nameplate's frame now shrinks with its bar. The frame
//   art is drawn at its own size times the widget's scale, so a smaller
//   rectangle left it at the stock height; the frame's scale now follows the
//   bar's height and its rectangle is divided by the same ratio, so its caps
//   still sit on the bar's edges. The shrunk bar keeps the stock bar's bottom
//   edge instead of its centre (a quarter of the stock height lower at 50%).
//   A NameBold text box in TargetAttached gets the name's text and colour on
//   every update, which draws the name bold. width_percent default 85.
//
// 1.0.16: the controller nameplate (the TargetAttached container, drawn over
//   the hovered monster's head) gets its bar and frames shrunk around their
//   centre to a configured share of their stock size: [nameplate]
//   width_percent of the width the game gives the bar (the width of the
//   name) and height_percent of the bar's layout height, 75 and 50 by
//   default. Its content (name, fill, frames) stays stock.
//
// 1.0.15: a bound demon is shown as an ally. It keeps the class id of the
//   hostile monster it was bound from, whose monstats.txt row has Align 0, so
//   the ally test treated it as an enemy and the effective view subtracted the
//   hovering player's sunder and -enemy resistance from its resistances (17%
//   summon physical resistance showed as 2% for an owner with -15% enemy
//   physical resistance). The ally test now reads Align from the row Bound
//   Demon Scaling gives a bound demon through its "stats-row" service, the
//   same row Summon Resist Cap decides its cap by. Until a bound demon's
//   server data arrives, its client copy's bind_demon state stands in for
//   that answer, so its uncapped stock immunity line is not shown meanwhile.
//
// 1.0.14: a monster's panel data is read on the game thread and kept for
//   RefreshMs (120 ms), so right after Bind Demon the panel went on showing
//   the monster's numbers from before the bind for up to that long. Bound
//   Demon Scaling (celestialrayone.bound-demon-scaling 1.0.3 and later)
//   publishes the "demon-bound" event on the game thread as soon as a demon
//   is bound; a demon the panel holds data for is read again right there, so
//   its new life shows in the same frame.
//
// Everything below was read out of the D2RLoader 1.3.1 process image
// (D2R 3.3.93847). Every entry, call site and layout used here is checked byte
// for byte at load; nothing is installed when a check fails.
//
// ---------------------------------------------------------------------------
// 1. How the stock panel works
// ---------------------------------------------------------------------------
//   0CEA50 (unit, position, attached) builds the panel data for the hovered
//     monster every frame (single caller 0C0C8D): name, colour, health as a
//     percentage, the "type / description" line, the unique-affix line and
//     the immunity line, and posts it as message MonsterHealth:SetInfo.
//     The type is the string key "UndeadDescriptX" or "DemonID" (0CED50
//     window: 3AF190 is-demon, 3AF2C0 is-undead); the description is the
//     monstats.txt DescStr string.
//   1F84E0 (panel, message, container, attached) applies it to one container:
//     message+88h is the message hash, message+110h the 64-byte payload:
//       +00 name   +08 colour (16 bytes)   +18 health percent (int)
//       +20 unique-affix line, or the type line when there are no affixes
//       +28 immunity line   +30 position   +38 unique flag (byte)
//     It sizes "Health" to the width of the name text (1F8784..1F87F9: 8460F0
//     ui scale, 903C20 measure(text, style +B4h, scale in xmm2), 1E6750 widget
//     scale, 857520 set width, 857570 set x), copies that rectangle onto
//     UniqueMonsterBackground / NormalMonsterBackground and shows one of them.
//     The pc layout is updated once (container = panel), the controller one
//     once per ScreenAnchored / TargetAttached container.
//   This plugin runs after the stock update and fills only containers that
//   hold its own widgets. The one other container it touches is the
//   controller nameplate: the TargetAttached container (the panel's child of
//   that name, updated with attached = 1; the pc layout passes the panel
//   itself), when it holds none of the plugin's widgets. Its content stays
//   stock. Only its bar and both frames are resized: the stock update has
//   just made the bar as wide as the name, centred it, set the fill and
//   copied the bar's rectangle onto both frames, and it never sets the bar's
//   y or height. The plugin shrinks the bar to [nameplate] width_percent of
//   that width (around its centre) and height_percent of the bar's layout
//   height (remembered once; the bar keeps the stock bar's bottom edge), and
//   sets the fill again for the new rectangle. It does so only when the
//   container has its Name text box, which is exactly when the stock update
//   sized the bar, so the width is never shrunk twice.
//   The frames follow the bar on screen. GrowableImageWidget::Draw (8DCE80)
//   draws the frame art at its own size times the widget's scale (+80h, read
//   at 8DCED5), and its caps are child images anchored at the frame's left
//   and right middle (8DD6FF: (0, 0.5) and (1, 0.5)), whose offsets the
//   screen position walk (8562F0) multiplies by the frame's scale (8563B8).
//   A rectangle alone therefore moves the caps but never changes the art's
//   height. With r = the bar's new height over its layout height, each frame
//   gets scale r (times its own scale before the plugin changed it, normally
//   1) and the bar's rectangle with width and height divided by r: its caps
//   land on the bar's left and right edges, centred on it, and all of its art
//   is r times the stock size. At 100% the frames get the bar's rectangle and
//   their own scale back, as stock.
//   Name and NameBold keep their layout distance above the bar: the stock
//   update never sets their rectangles, and the plugin moves both by as much
//   as the bar's top edge moved (their layout y remembered once, like the
//   bar's).
//   The name stays the stock Name text box. When TargetAttached also has a
//   text box named NameBold, the plugin gives it the name's text and colour
//   on every update, exactly as the stock update fills Name (1F8731: payload
//   +08, 16 bytes, to the text box at +BCh, then the text from payload +00),
//   and clears it with Name. Placed a pixel or two beside Name in the layout,
//   the two copies draw the name bold; the game's text styles have no bold
//   option and its only bold font is an italic one.
//   Widgets: rect x,y,w,h at +70h (8562A0 reads the 16 bytes), fitToParent
//   byte at +52h, SetVisible is vtable +50h, text set through 86DD50.
//
// ---------------------------------------------------------------------------
// 2. Exact life, level, resistances and aura levels
// ---------------------------------------------------------------------------
//   The client only knows life as a percentage. The exact values live on the
//   server unit, so they are computed on the authoritative game thread
//   (ThreadService::runOnGameThread) and cached for the panel:
//     single player / TCP host  : the server unit is found with 09A5A0
//                                 (id, type) and read directly.
//     joined TCP/IP client      : the client asks the host through the plugin
//                                 network channel; the host computes the same
//                                 data for that player and answers.
//   Life is stat 6 / 7 (in 256ths), level stat 12. A monster with life bars
//   reports its total life through the Monster Life Bars plugin's
//   "life-totals" service (D2RLoader plugin communication: provider
//   celestialrayone.monster-life-bars, version 1), asked on the game thread
//   with the server unit: bars left x bar size + current bar, out of bars
//   total x bar size, 64-bit. Every other monster reads stat 6 / 7 as before.
//
//   Resistances follow SUNITDMG_ApplyResistancesAndAbsorb 4523E0 exactly. For
//   a monster: resist stat -> sunder 44F6D0 (item_pierce_*_immunity) ->
//   -enemy resistance 44F8B0 (passive_*_pierce, ignored against immunity),
//   both called with the hovering player as attacker; then a positive result
//   is uncapped, a non-positive one is floored at the engine's floor
//   (mov ecx,-100 at 4524C4). Physical resistance is 0 against undead while
//   the attacker has state 47. The switch 092A10 that would let -enemy
//   resistance reduce immunities returns 0 in this build.
//   Allied monsters (monstats.txt Align 1: summons, hirelings, town NPCs)
//   always show their own resistances, without the hovering player's sunder or
//   -enemy resistance: those steps describe hits the player cannot land on
//   them, and subtracting them made e.g. a summon's 40% physical show as 25%
//   for an owner with -15% enemy physical resistance.
//   A bound demon keeps the class id of the hostile monster it was bound from,
//   and that row has Align 0. It is decided by the row Bound Demon Scaling
//   gives it instead: that plugin's "stats-row" service (D2RLoader plugin
//   communication: provider celestialrayone.bound-demon-scaling, version 1),
//   asked on the game thread with the server unit, returns the pseudo row a
//   bound demon takes its stats from (-1 for every other unit), and Align is
//   read from that row. Summon Resist Cap decides the cap by the same row.
//   Without that plugin, or while it names no pseudo row, a bound demon is
//   decided by its own row.
//   Which monsters are capped is asked from the Summon Resist Cap plugin
//   itself, through its "cap-decision" service (D2RLoader plugin
//   communication: provider celestialrayone.summon-resist-cap, version 1),
//   on the game thread with the server unit. That one answer covers its
//   Align column, its extra_monster_ids and its uncap pairs: a summon whose
//   owner has a paired stat is reported uncapped and can show 100 or more,
//   exactly as the engine resolves it. Nothing of that plugin's config is
//   copied here. A capped monster gets the player cap while the cap plugin's
//   jump at the 45249C gate is live: 75 + the max-resist stat, ceiling 95
//   (4524DE), physical 50 (4524D6), floor -100 (4524E7). No difficulty
//   penalty, as in the engine.
//
//   Aura Enchanted (MonUMod 30, callback 495CD0) gives one skill through the
//   skill list; Heralds also get one more as stat 151 (item_aura) with the
//   skill id as its layer. The skills to look for come from Monster Affixes'
//   "aura-enchanted" service (its TOML lists) when that plugin publishes it,
//   else from the game's tables at 1D1DE70 (7 records) and 1D1DF20 (4), both
//   24-byte records with the skill id at +14h. A bound demon rolls its aura
//   from Summon Enhancements' own list (its auras key), which that plugin
//   publishes under the same service name and shape; those skills are looked
//   for as well. Levels are read back with 33DCD0 / 33D1E0 and stat 151.
//
// ---------------------------------------------------------------------------
// 3. Text
// ---------------------------------------------------------------------------
//   Affix names come from the game's own builder 1DFAE0 (the one that makes
//   the stock affix line), called with the unit's MonUMod list minus Aura
//   Enchanted, so they read exactly as stock. Colours use the "ColorCode"
//   string plus '0' + colour id (the game's 0CD6E0). Type names come from the
//   localization service ("UndeadDescriptX", "DemonID"). Skill names are the
//   game's own: the skills.txt record (097790) holds its skilldesc.txt row at
//   +23Ch, 088850 returns that row, and its "str name" string id sits at +8
//   (the skilldesc.txt column table, 3025A5); the localization service turns the
//   id into text. A skill without a skilldesc row falls back to
//   "skillname<id>".
//
//   Minions carry in their own MonUMod list every affix their boss passes on
//   (monumod.txt xfer = 1), also the ones that do nothing for a minion. The
//   affixes in minion_hidden_affixes are left out for minions. A minion is
//   what the stock builder treats as one before it writes its minion line:
//   type flag 10h set and none of 0Fh (0CED9A 38E0E0, 0CF05D 38E870 ->
//   1DF830). 38E870 (unit, mask) tests mask against the type flag word at
//   monster data +1Ah. The stock line for a minion is that minion line, never
//   an affix line, so a minion left with nothing to show takes it as its type
//   line, like a minion without affixes.
//
//   Bosses are monstats.txt primeevil: flags at record offset 60, bit 7 (the
//   3.3 column parser 39DB30, descriptor slot rbp+480h).
//
// ---------------------------------------------------------------------------
// 4. Immunity line (1.0.10)
// ---------------------------------------------------------------------------
//   The stock builder 0CEA50 makes the line on the client from the client
//   copy of the monster's resist stats (0CEDCC..0CF7B1). Per element, in the
//   order Physical 36, Magic 37, Fire 39, Lightning 41, Cold 43, Poison 45:
//   value = resist stat; if value >= 100 it subtracts the local player's
//   item_pierce_*_immunity (sunder: 192, 193, 189, 190, 187, 191; the -enemy
//   resistance branch behind 092A10 is off in this build); value >= 100 adds
//   the element. Class ids 704..709 never get a line (0CEDB8: sub 2C0h,
//   cmp 5, jbe). It cannot know about the Summon Resist Cap, which exists only
//   on the server, so a capped summon past 100 showed "Immune to X" although
//   its capped value tops out at 95 (physical 50).
//
//   The plugin builds the row itself, the same way:
//     text      d2r strings strScaryMonDesc5, 6, 1, 3, 2, 4, or the
//               PhysicalImmunityIcon .. PoisonImmunityIcon set while 077E10
//               (a display setting, read by the stock code at 0CEDEA..) is on
//     colours   4, 8, 1, 9, 3, 2 through the game's colour function 0CD6E0
//     separator TargetHealthBarTextSeparator coloured 5 while 846210 is on,
//               otherwise KeyComma followed by space
//   but an element counts as immune only when the value the damage code
//   uses for the hovering player's hit is 100 or more: ResolveResist with the
//   hovering player as attacker (sunder applied as stock does, Summon Resist
//   Cap applied), on the server, in both resistance modes. Until the server
//   data arrives the stock line is shown, except for allied monsters
//   (monstats.txt Align 1), which may be capped and show nothing yet. A
//   bound demon counts as one of those until then: the client cannot ask
//   Bound Demon Scaling, so the client copy's bind_demon state (207) stands
//   in, the state the game's own demon save tests (533CE0, at 533D60:
//   mov edx,0CFh / call 3351B0). That test is checked at load; when it does
//   not match, a bound demon shows the stock line until its data arrives.

#include <D2RLPlugin/api.h>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace {

constexpr char PluginIdText[]  = "celestialrayone.monster-display";
constexpr char PluginVersion[] = "1.0.19";
constexpr char SourceFile[]    = "monster-display";

const D2RL::PluginContext* Context{};
std::uintptr_t             Base{};

// ---------------------------------------------------------------------------
// Default configuration. Written to <scope>\d2rloader\config on first run.
// ---------------------------------------------------------------------------

constexpr char DefaultConfigToml[] = R"toml(# Monster Display
#
# Shows, for the monster under the cursor (keyboard/mouse and controller):
#   name
#   health bar in a frame, exact life and a smaller percentage after it
#   Level X <type> (the game's own type / description line after the level)
#   unique affixes
#   Aura Enchanted with the aura name and level on its own row
#   the resistances your hits actually meet
#   the game's immunity line
#
# Everything is drawn by the game's own widgets in hudmonsterhealthhd.json
# (data/global/ui/layouts, controller and controller/pc). The plugin fills the
# widgets it finds by name inside the panel (pc) or the ScreenAnchored
# container (controller), and leaves every other container alone, apart from
# the size of the controller nameplate's bar ([nameplate] below):
#   Name, Health, UniqueMonsterBackground, NormalMonsterBackground  (stock)
#   HPValue, HPPercent                                               (bar text)
#   LevelType, Affixes, Aura, Resistances, Immunities                (rows)
# The five rows are stacked in that order starting at LevelType's position;
# empty rows are hidden and take no space, each row advances by its own
# height from the layout plus row_gap. A row can carry two decoration images,
# <Row>TopLine and <Row>BottomLine (the layouts give Resistances a pair); they
# move and hide together with their row, and when they are GrowableImageWidgets
# they are also sized to the row's text, so short resistances get short bars.
#
# Exact life, level, aura levels and resistances come from the server unit.
# In single player and on a TCP/IP host they are read directly; a joined
# TCP/IP client asks the host, which needs this plugin too. Without it the bar
# shows the percentage only and the level, aura level and resistance rows are
# left out.
#
# Remove yin's monsterdisplay.dll: both hook the same two game functions.
#
# Console command: monsterdisplay            status and counters
#                  monsterdisplay debug on|off
#
# Restart the game after changing this file.

# File format version. Leave this value unchanged.
config_version = 2
enabled = true

[bar]
# Width of the health bar for ordinary monsters, in layout units (the panel in
# the shipped layouts is 1000 wide). 0 keeps the game's own sizing (the bar as
# wide as the name).
width = 800
# Prime evils (monstats.txt primeevil) get a bar this many times as wide.
boss_width_multiplier = 2.0
# ... and this many times as tall. The boss frame (BossMonsterBackground,
# PANEL/MonsterDisplayFrames/boss_*) has a fixed opening built for 1.25, so
# change this only together with that art. Without that widget in the layout
# bosses keep the normal height.
boss_height_multiplier = 1.25
# Height of the frame art in layout units. Only change it together with the
# frame sprites (the shipped ones are 101 tall).
frame_height = 101

[nameplate]
# The controller nameplate over the hovered monster's head (TargetAttached in
# the controller layout). The game makes its bar exactly as wide as the
# monster's name; the bar is shrunk to these percentages of that width
# (around its centre) and of the bar's height in the layout (keeping the
# stock bar's bottom edge). The frame follows it: its art is scaled with the
# bar's height. 100 keeps the game's own size.
# The name keeps its layout distance above the bar and moves with it. Its
# size is the layout's pointSize. A text box named NameBold in
# TargetAttached gets the name's text and colour on every update; placed a
# pixel or two beside Name it draws the name bold. Without it the name is
# drawn once.
width_percent = 85
height_percent = 50

[text]
# Level in front of the type line: "<level_label> <level>".
show_level = true
level_label = "Level"
# Exact life inside the bar ("12 / 12") and the percentage after it ("(100%)").
show_hp = true
show_hp_percent = true
# Unique affixes, in the game's own wording.
show_affixes = true
# Affixes not shown on minions, as monumod.txt ids. A boss passes every affix
# whose monumod.txt xfer is 1 on to its minions, also the ones that do nothing
# for a minion; the ones listed here are left out on minions. The boss always
# shows all of its own. A minion left with no affix to show gets the game's
# own type line, like a minion without affixes. [] shows everything.
#   5 Extra Strong       6 Extra Fast         7 Cursed
#   8 Magic Resistant    9 Fire Enchanted    17 Lightning Enchanted
#  18 Cold Enchanted    25 Mana Burn         26 Teleportation
#  27 Spectral Hit      28 Stone Skin        29 Multiple Shots
#  30 Aura Enchanted
minion_hidden_affixes = [9, 17, 18, 26, 27, 28]
# D2R colour id of the affix and aura rows (see the list at the bottom).
affix_color = 4
# Aura Enchanted on its own row as "Aura Enchanted (Fanaticism Lv. 13)".
# false keeps it among the other affixes, without the aura or its level.
separate_aura = true
aura_level_label = "Lv."
# The immunity line ("Immune to Fire"), last. Same strings, colours and
# separators as the game's own line, but an element only shows when the
# resistance your hits really meet is 100 or more, with your sunder applied
# as the game does and the Summon Resist Cap respected: a capped summon tops
# out at 95 (physical 50), so it never shows as immune.
show_immunities = true
# Space between two rows under the bar, in layout units (10 = 5 px at 1080p).
# The gap between the bar and the first row comes from the layout.
row_gap = 10

[resistances]
show = true
# effective : what the hovering player's hits meet: sunder and -enemy
#             resistance are applied, exactly as the damage code does.
#             Allied monsters (monstats Align 1: your summons, hirelings,
#             NPCs) always show their own values, since you cannot hit them.
#             A bound demon counts as one when the Bound Demon Scaling plugin
#             gives it a pseudo row whose Align is 1.
# monster   : the monster's own values, before anything the attacker has.
# The Summon Resist Cap is applied in both modes, exactly as the game applies
# it: the display asks the Summon Resist Cap plugin which monsters it caps, so
# its Align rule, its extra rows and its uncap pairs all show correctly.
# There is nothing to set up here for it.
mode = "effective"
# Reorder, omit or repeat: {fire} {lightning} {cold} {poison} {magic} {physical}
# {color:N} sets the colour of the literal text that follows it. Values always
# use their own colour below.
template = "{fire} {lightning} {cold} {poison} {magic} {physical}"
separator_color = 0
fire_color = 1
lightning_color = 9
cold_color = 3
poison_color = 2
magic_color = 8
physical_color = 4

[debug]
# Writes every data request and answer to the plugin log.
log = false

# D2R colour ids:
#  0 White         1 Red           2 BrightGreen   3 Blue
#  4 Gold          5 DarkGray      6 Black         7 PaleGold
#  8 Orange        9 Yellow       10 DarkGreen    11 Purple
# 12 Green        13 LightGray    14 DullGold     15 YellowGreen
# 16 Amber        17 ForestGreen  18 Indigo       19 LimeGreen
# 20 Khaki        21 Silver       22 SoftWhite    23 GrayWhite
# 24 BlueGray     25 Teal         26 Goldenrod    27 Charcoal
# 28 BrightGold   29 Violet       30 Beige        31 Cyan
# 32 Pink         33 Lavender     34 LeafGreen    35 PaleYellow
# 36 SoftRed      37 SkyBlue      38 RedOrange
)toml";

// ---------------------------------------------------------------------------
// Addresses (RVAs into the D2RLoader 1.3.1 image)
// ---------------------------------------------------------------------------

// Hooked
constexpr std::uint64_t BuilderRva     = 0x0CEA50;
constexpr std::uint64_t PanelUpdateRva = 0x1F84E0;
// Proven by windows (call targets and field offsets read from the game's own code)
constexpr std::uint64_t PanelWindowRva = 0x1F8771;  // SetText .. SetVisible
constexpr std::uint64_t TypeWindowRva  = 0x0CED50;  // is-demon / is-undead and their keys
constexpr std::uint64_t PrimeNameRva   = 0x39E0E1;  // lea "primeevil" -> slot rbp+480h
constexpr std::uint64_t PrimeFieldRva  = 0x39E263;  // slot type 1Dh, bit 7, offset 3Ch
constexpr std::uint64_t GetRectRva     = 0x8562A0;
constexpr std::uint64_t SetWidthRva    = 0x857520;
constexpr std::uint64_t SetXRva        = 0x857570;
// Called
constexpr std::uint64_t SetTextRva     = 0x86DD50;
constexpr std::uint64_t ColorizeRva    = 0x0CD6E0;  // prepends the game's ColorCode + '0'+colour to a 1024-byte buffer
constexpr std::uint64_t SetHealthValueRva = 0x2AC890;  // HealthBarWidget value, rebuilds the fill rect
constexpr std::uint64_t UiScaleRva     = 0x8460F0;
constexpr std::uint64_t MeasureRva     = 0x903C20;
constexpr std::uint64_t WidgetScaleRva = 0x1E6750;
constexpr std::uint64_t IsDemonRva     = 0x3AF190;
constexpr std::uint64_t IsUndeadRva    = 0x3AF2C0;
constexpr std::uint64_t FindChildRva   = 0x856220;
constexpr std::uint64_t GetUModsRva    = 0x38E310;
constexpr std::uint64_t MonTypeFlagRva = 0x38E870;  // (unit, mask): mask & type flag word at monster data +1Ah
constexpr std::uint64_t UModNamesRva   = 0x1DFAE0;
constexpr std::uint64_t AllocatorRva   = 0x12063C0;
constexpr std::uint64_t UnitTypeRva    = 0x34B9D0;
constexpr std::uint64_t UnitIdRva      = 0x34A330;
constexpr std::uint64_t ClassIdRva     = 0x349860;
constexpr std::uint64_t DataContextRva = 0x34A0E0;
constexpr std::uint64_t MonStatsRva    = 0x0976E0;
constexpr std::uint64_t ReadStatRva    = 0x2F5020;  // loader thunk
constexpr std::uint64_t LocalIndexRva  = 0x08B2D0;
constexpr std::uint64_t LocalPlayerRva = 0x09A480;
constexpr std::uint64_t ServerGameRva  = 0x405790;  // current server game (global handle 2A9C020)
constexpr std::uint64_t ServerUnitRva  = 0x48FE80;  // SUNIT_GetServerUnit(game, type, id)
constexpr std::uint64_t SunderRva      = 0x44F6D0;
constexpr std::uint64_t PierceRva      = 0x44F8B0;
constexpr std::uint64_t CheckStateRva  = 0x3351B0;
constexpr std::uint64_t SkillByIdRva   = 0x33DCD0;
constexpr std::uint64_t SkillsRecordRva = 0x097790;  // (dataContext, skillId) -> skills.txt record
constexpr std::uint64_t SkillDescRva    = 0x088850;  // (dataContext, skillId) -> its skilldesc.txt record
constexpr std::uint64_t SkillDescColumnsRva = 0x3025A5;  // skilldesc.txt column table: "str name" string key at +8
constexpr std::size_t   SkillDescRowOffset     = 0x23C;  // skills.txt record: skilldesc row, int16
constexpr std::size_t   SkillDescStrNameOffset = 0x08;   // skilldesc.txt record: "str name" string id
constexpr std::uint16_t MissingStringId        = 5382;   // what a string key that does not exist compiles to
constexpr std::uint64_t SkillLevelRva  = 0x33D1E0;  // loader thunk
constexpr std::uint64_t IconKeysRva    = 0x077E10;  // stock immunity line: icon string set on
constexpr std::uint64_t SeparatorRva   = 0x846210;  // stock immunity line: separator style
// Read only
constexpr std::uint64_t AuraTableRva        = 0x1D1DE70;
constexpr std::uint64_t HeraldTableRva      = 0x1D1DF20;
constexpr std::uint64_t CapGateRva          = 0x45249C;
constexpr std::uint64_t UncappedFloorRva    = 0x4524C4;  // mov ecx, imm32
constexpr std::uint64_t PhysicalCapOpRva    = 0x4524D5;  // mov eax, imm32
constexpr std::uint64_t ElementalCapOpRva   = 0x4524DD;  // mov eax, imm32
constexpr std::uint64_t CappedFloorRva      = 0x4524E7;  // mov ecx, imm32
constexpr std::uint64_t ImmuneClassRva      = 0x0CEDB8;  // mov eax,2C0h / sub r14w,ax / cmp r14w,5 / jbe
constexpr std::uint64_t BindDemonTestRva    = 0x533D60;  // demon save 533CE0: mov edx,0CFh / mov rcx,rax / call 3351B0
constexpr std::uint64_t NameColorRva        = 0x1F8731;  // stock update: name colour to Name +BCh, text from payload +00
constexpr std::uint64_t ChildScaleRva       = 0x8563B8;  // screen position: parent rect +70h, parent scale +80h
constexpr std::uint64_t FrameArtScaleRva    = 0x8DCED5;  // GrowableImageWidget::Draw: art times the widget's +80h
constexpr std::uint64_t CapAnchorRva        = 0x8DD6FF;  // GrowableImageWidget setup: cap anchors (0, 0.5) / (1, 0.5)

// Message and payload
constexpr std::uint64_t SetInfoHash        = 0x40FFBA2D98A3FF97ULL;
constexpr std::uint64_t ClearHash          = 0x73013BE2814ED07AULL;
constexpr std::size_t   MessageHashOffset  = 0x88;
constexpr std::size_t   MessagePayloadOff  = 0x110;
constexpr std::size_t   PayloadName        = 0x00;
constexpr std::size_t   PayloadColor       = 0x08;  // 16 bytes
constexpr std::size_t   PayloadHealth      = 0x18;
constexpr std::size_t   PayloadLine        = 0x20;
constexpr std::size_t   PayloadImmunities  = 0x28;

// Widgets
constexpr std::size_t WidgetFitToParent   = 0x52;
constexpr std::size_t WidgetRect          = 0x70;
constexpr std::size_t WidgetOwnScale      = 0x80;   // float, the widget's own scale
constexpr std::size_t TextStyleOffset     = 0xB4;
constexpr std::size_t TextColorOffset     = 0xBC;   // 16 bytes, inside the style
constexpr std::size_t SetVisibleSlot      = 0x50 / 8;
constexpr std::size_t TypeDescSlot        = 0x58 / 8;
constexpr std::size_t TypeDescNameOffset  = 0x08;
constexpr std::size_t TypeDescParentOffset = 0x10;

// Units and tables
constexpr std::int32_t UnitTypePlayer  = 0;
constexpr std::int32_t UnitTypeMonster = 1;
constexpr std::size_t  MonStatsFlags   = 60;     // flags holding primeevil
constexpr std::size_t  MonStatsAlign   = 0x87;   // monstats.txt Align, 1 = ally
constexpr std::int32_t ImmuneClassFirst = 0x2C0; // 704: classes 704..709 get no immunity line
constexpr std::int32_t ImmuneClassSpan  = 5;
constexpr std::int32_t ImmuneValue      = 100;
constexpr std::uint8_t PrimeEvilMask   = 0x80;   // bit 7
constexpr std::size_t  GameDataContext = 0x106;  // game+106h, read by 44F6D0/44F8B0
constexpr std::uint8_t AuraEnchanted   = 30;
constexpr std::int32_t MaximumUMod     = 44;     // 1DFAE0 names ids 1 to 2Ch
constexpr std::uint16_t MonTypeMinion  = 0x10;
constexpr std::uint16_t MonTypeBosses  = 0x0F;   // the bits 38E0E0 tests before the minion bit
constexpr std::int32_t StatLife        = 6;
constexpr std::int32_t StatMaxLife     = 7;
constexpr std::int32_t StatLevel       = 12;
constexpr std::int32_t StatItemAura    = 151;
constexpr std::int32_t SanctuaryState  = 47;
constexpr std::int32_t BindDemonState  = 207;    // states.txt bind_demon, what 533D60 tests

constexpr std::uint64_t RefreshMs        = 120;
constexpr std::uint64_t HoverTimeoutMs   = 1000;
constexpr std::uint64_t ConnectRetryMs   = 3000;
constexpr std::uint16_t ChannelId        = 1;
constexpr std::uint64_t CompatibilityToken = 0x4D4F4E4453500003ULL;  // "MONDSP" v3 (1.0.12: 64-bit life)
constexpr std::uint16_t MessageRequest   = 1;
constexpr std::uint16_t MessageReply     = 2;
constexpr std::uint32_t MaximumConfigBytes = 32768;

// Byte witnesses, read from the D2RLoader 1.3.1 image (IDA, process dump md5 8af7a47c).
constexpr std::uint8_t BuilderEntryBytes[]{
    0x48, 0x89, 0x5C, 0x24, 0x18, 0x55, 0x56, 0x57, 0x41, 0x54, 0x41, 0x55, 0x41, 0x56, 0x41, 0x57 };
constexpr std::uint8_t PanelUpdateEntryBytes[]{
    0x48, 0x89, 0x5C, 0x24, 0x18, 0x44, 0x88, 0x4C, 0x24, 0x20, 0x48, 0x89, 0x54, 0x24, 0x10, 0x48,
    0x89, 0x4C, 0x24, 0x08 };
constexpr std::uint8_t PanelWindowBytes[]{
    0xE8, 0xDA, 0x55, 0x67, 0x00, 0x48, 0x85, 0xF6, 0x0F, 0x84, 0xFC, 0x00, 0x00, 0x00, 0x0F, 0x29,
    0x74, 0x24, 0x40, 0xE8, 0x67, 0xD9, 0x64, 0x00, 0x49, 0x8B, 0x0E, 0x49, 0x8D, 0x94, 0x24, 0xB4,
    0x00, 0x00, 0x00, 0x0F, 0x28, 0xD0, 0xE8, 0x84, 0xB4, 0x70, 0x00, 0x4C, 0x8B, 0xA4, 0x24, 0x90,
    0x00, 0x00, 0x00, 0x49, 0x8B, 0xCC, 0x66, 0x0F, 0x6E, 0xF0, 0x0F, 0x5B, 0xF6, 0xE8, 0x9D, 0xDF,
    0xFE, 0xFF, 0x48, 0x8B, 0xCE, 0xF3, 0x0F, 0x5E, 0xF0, 0xF3, 0x0F, 0x2C, 0xDE, 0x8B, 0xD3, 0xE8,
    0x5B, 0xED, 0x65, 0x00, 0x0F, 0x28, 0x74, 0x24, 0x40, 0x8B, 0xC3, 0x99, 0x48, 0x8B, 0xCE, 0x2B,
    0xC2, 0xD1, 0xF8, 0x80, 0xBC, 0x24, 0xA8, 0x00, 0x00, 0x00, 0x00, 0x74, 0x11, 0x41, 0x8B, 0x94,
    0x24, 0x6C, 0x01, 0x00, 0x00, 0x2B, 0xD0, 0xE8, 0x83, 0xED, 0x65, 0x00, 0xEB, 0x16, 0x41, 0x8B,
    0x94, 0x24, 0x68, 0x01, 0x00, 0x00, 0x2B, 0xD0, 0xE8, 0x72, 0xED, 0x65, 0x00, 0xEB, 0x05, 0x48,
    0x85, 0xF6, 0x74, 0x76, 0x48, 0x8B, 0x06, 0xB2, 0x01, 0x48, 0x8B, 0xCE, 0xFF, 0x50, 0x50 };
constexpr std::uint8_t TypeWindowBytes[]{
    0x48, 0x8B, 0xCE, 0xE8, 0x38, 0x04, 0x2E, 0x00, 0x85, 0xC0, 0x74, 0x09, 0x48, 0x8D, 0x3D, 0xF5,
    0x3D, 0xBF, 0x01, 0xEB, 0x15, 0x48, 0x8B, 0xCE, 0xE8, 0x53, 0x05, 0x2E, 0x00 };
constexpr std::uint8_t PrimeNameBytes[]{
    0x48, 0x8D, 0x05, 0x78, 0x83, 0x96, 0x01, 0x48, 0x89, 0x85, 0x80, 0x04, 0x00, 0x00 };
constexpr std::uint8_t PrimeFieldBytes[]{
    0xC7, 0x85, 0x88, 0x04, 0x00, 0x00, 0x1D, 0x00, 0x00, 0x00, 0xC7, 0x85, 0x8C, 0x04, 0x00, 0x00,
    0x07, 0x00, 0x00, 0x00, 0x48, 0xC7, 0x85, 0x90, 0x04, 0x00, 0x00, 0x3C, 0x00, 0x00, 0x00 };
constexpr std::uint8_t GetRectBytes[]{
    0x40, 0x53, 0x48, 0x83, 0xEC, 0x30, 0x80, 0x79, 0x52, 0x00, 0x48, 0x8B, 0xDA, 0x74, 0x2A, 0x48,
    0x8B, 0x49, 0x30, 0x48, 0x8D, 0x54, 0x24, 0x20, 0xE8, 0xE3, 0xFF, 0xFF, 0xFF, 0x33, 0xC0, 0x48,
    0x89, 0x03, 0x8B, 0x44, 0x24, 0x28, 0x89, 0x43, 0x08, 0x8B, 0x44, 0x24, 0x2C, 0x89, 0x43, 0x0C,
    0x48, 0x8B, 0xC3, 0x48, 0x83, 0xC4, 0x30, 0x5B, 0xC3, 0x0F, 0x10, 0x41, 0x70 };
constexpr std::uint8_t SetWidthBytes[]{
    0x48, 0x89, 0x5C, 0x24, 0x08, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x80, 0x79, 0x52, 0x00, 0x8B, 0xFA,
    0x48, 0x8B, 0xD9, 0x74, 0x22, 0x48, 0x8D, 0x4C, 0x24, 0x38, 0xC6, 0x44, 0x24, 0x38, 0x00, 0xE8,
    0xCC, 0xD3, 0xFF, 0xFF, 0x84, 0xC0, 0x74, 0x01, 0xCC, 0x89, 0x7B, 0x78 };
constexpr std::uint8_t SetXBytes[]{
    0x48, 0x89, 0x5C, 0x24, 0x08, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x80, 0x79, 0x52, 0x00, 0x8B, 0xFA,
    0x48, 0x8B, 0xD9, 0x74, 0x22, 0x48, 0x8D, 0x4C, 0x24, 0x38, 0xC6, 0x44, 0x24, 0x38, 0x00, 0xE8,
    0x9C, 0xD5, 0xFF, 0xFF, 0x84, 0xC0, 0x74, 0x01, 0xCC, 0x89, 0x7B, 0x70 };
constexpr std::uint8_t FindChildBytes[]{
    0x48, 0x89, 0x5C, 0x24, 0x10, 0x48, 0x89, 0x74, 0x24, 0x18, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x48,
    0x8B, 0x59, 0x58 };
constexpr std::uint8_t GetUModsBytes[]{
    0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B, 0xD9, 0x48, 0x85, 0xC9, 0x74, 0x0A, 0xE8, 0xAD,
    0xD6, 0xFB, 0xFF, 0x83, 0xF8, 0x01, 0x74, 0x19 };
constexpr std::uint8_t MonTypeFlagBytes[]{
    0x48, 0x89, 0x5C, 0x24, 0x10, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x0F, 0xB7, 0xFA, 0x48, 0x8B, 0xD9,
    0x48, 0x85, 0xC9, 0x74, 0x0A, 0xE8, 0x46, 0xD1, 0xFB, 0xFF, 0x83, 0xF8, 0x01, 0x74, 0x19, 0x48,
    0x8D, 0x4C, 0x24, 0x30, 0xC6, 0x44, 0x24, 0x30, 0x00, 0xE8, 0x52, 0x3C, 0xE1, 0xFF, 0x84, 0xC0,
    0x74, 0x01, 0xCC, 0x48, 0x85, 0xDB, 0x74, 0x29, 0x48, 0x8B, 0xCB, 0xE8, 0x20, 0xD1, 0xFB, 0xFF,
    0x83, 0xF8, 0x01, 0x75, 0x1C, 0x48, 0x8B, 0x43, 0x10, 0x48, 0x85, 0xC0, 0x74, 0x13, 0x66, 0x85,
    0x78, 0x1A, 0x74, 0x0D, 0xB0, 0x01 };
constexpr std::uint8_t UModNamesBytes[]{
    0x48, 0x89, 0x5C, 0x24, 0x10, 0x55, 0x56, 0x57, 0x41, 0x54, 0x41, 0x55, 0x41, 0x56, 0x41, 0x57,
    0x48, 0x8D, 0xAC, 0x24, 0x10, 0xFB, 0xFF, 0xFF };
constexpr std::uint8_t AllocatorBytes[]{
    0x48, 0x83, 0xEC, 0x28, 0x65, 0x48, 0x8B, 0x04, 0x25, 0x58, 0x00, 0x00, 0x00 };
constexpr std::uint8_t UnitTypeBytes[]{
    0x48, 0x83, 0xEC, 0x28, 0x48, 0x85, 0xC9, 0x75, 0x1D, 0x88, 0x4C, 0x24, 0x30, 0x48, 0x8D, 0x4C };
constexpr std::uint8_t UnitIdBytes[]{
    0x48, 0x83, 0xEC, 0x28, 0x48, 0x85, 0xC9, 0x75, 0x1D, 0x88, 0x4C, 0x24, 0x30, 0x48, 0x8D, 0x4C,
    0x24, 0x30 };
constexpr std::uint8_t ClassIdBytes[]{
    0x48, 0x83, 0xEC, 0x28, 0x48, 0x85, 0xC9, 0x75, 0x1D, 0x88, 0x4C, 0x24, 0x30, 0x48, 0x8D, 0x4C,
    0x24, 0x30, 0xE8, 0x49, 0xCB, 0xFF, 0xFF, 0x84, 0xC0, 0x74, 0x01, 0xCC };
constexpr std::uint8_t DataContextBytes[]{
    0x48, 0x83, 0xEC, 0x28, 0x48, 0x85, 0xC9, 0x75, 0x1A, 0x88, 0x4C, 0x24, 0x30, 0x48, 0x8D, 0x4C,
    0x24, 0x30 };
constexpr std::uint8_t MonStatsBytes[]{
    0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x74, 0x24, 0x20, 0x57, 0x48, 0x83, 0xEC, 0x30, 0x48,
    0x63, 0xF2, 0xE8, 0x99, 0x93, 0x26, 0x00, 0x48, 0x8B, 0xF8, 0x48, 0x8B, 0xDE, 0x85, 0xF6, 0x78 };
// 097790 up to its row count check, 088850 up to the read of the skilldesc row
// (+23Ch), and the "str name" column entry: type 18h (string key), offset 8.
constexpr std::uint8_t SkillsRecordBytes[] {
    0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x74, 0x24, 0x20, 0x57, 0x48, 0x83, 0xEC, 0x30, 0x48,
    0x63, 0xF2, 0xE8, 0xE9, 0x92, 0x26, 0x00, 0x48, 0x8B, 0xF8, 0x48, 0x8B, 0xDE, 0x85, 0xF6, 0x78,
    0x09, 0x48, 0x3B, 0x98, 0xB8, 0x11, 0x00, 0x00, 0x72, 0x18 };
constexpr std::uint8_t SkillDescBytes[] {
    0x48, 0x89, 0x5C, 0x24, 0x08, 0x55, 0x56, 0x57, 0x48, 0x83, 0xEC, 0x30, 0x48, 0x63, 0xF2, 0x0F,
    0xB6, 0xE9, 0xE8, 0x29, 0x82, 0x27, 0x00, 0x48, 0x8B, 0xF8, 0x48, 0x8B, 0xDE, 0x85, 0xF6, 0x78,
    0x09, 0x48, 0x3B, 0x98, 0xB8, 0x11, 0x00, 0x00, 0x72, 0x1C, 0x48, 0x8D, 0x4C, 0x24, 0x58, 0xC6,
    0x44, 0x24, 0x58, 0x00, 0xE8, 0x97, 0xDB, 0xFF, 0xFF, 0x84, 0xC0, 0x74, 0x01, 0xCC, 0x85, 0xF6,
    0x0F, 0x88, 0xDF, 0x00, 0x00, 0x00, 0x48, 0x3B, 0x9F, 0xB8, 0x11, 0x00, 0x00, 0x0F, 0x83, 0xD2,
    0x00, 0x00, 0x00, 0x48, 0x81, 0xC7, 0xB0, 0x11, 0x00, 0x00, 0x48, 0x89, 0x5C, 0x24, 0x60, 0x48,
    0x3B, 0x5F, 0x08, 0x72, 0x1E, 0x48, 0x8D, 0x44, 0x24, 0x60, 0x48, 0x89, 0x7C, 0x24, 0x28, 0x48,
    0x8D, 0x4C, 0x24, 0x20, 0x48, 0x89, 0x44, 0x24, 0x20, 0xE8, 0x02, 0xD2, 0xFF, 0xFF, 0x84, 0xC0,
    0x74, 0x01, 0xCC, 0x48, 0x69, 0x44, 0x24, 0x60, 0xEC, 0x02, 0x00, 0x00, 0x48, 0x03, 0x07, 0x0F,
    0x84, 0x90, 0x00, 0x00, 0x00, 0x48, 0x0F, 0xBF, 0x98, 0x3C, 0x02, 0x00, 0x00 };
constexpr std::uint8_t SkillDescColumnsBytes[] {
    0x48, 0xC7, 0x85, 0x58, 0x01, 0x00, 0x00, 0x18, 0x00, 0x00, 0x00, 0x48, 0xC7, 0x85, 0x60, 0x01,
    0x00, 0x00, 0x08, 0x00, 0x00, 0x00, 0x4C, 0x89, 0x8D, 0x68, 0x01, 0x00, 0x00 };
constexpr std::uint8_t ReadStatBytes[]{
    0xFF, 0x25, 0xF2, 0x51, 0xB3, 0x03 };
constexpr std::uint8_t LocalIndexBytes[]{
    0x8B, 0x05, 0x2E, 0x84, 0x99, 0x02, 0xC3 };
constexpr std::uint8_t LocalPlayerBytes[]{
    0x48, 0x89, 0x5C, 0x24, 0x08, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x83, 0xF9, 0x08 };
constexpr std::uint8_t ServerGameBytes[]{
    0x48, 0x83, 0xEC, 0x28, 0x48, 0x8B, 0x15, 0x85, 0x68, 0x69, 0x02, 0x33, 0xC9, 0x48, 0x8D, 0x42,
    0x01, 0x48, 0x83, 0xF8, 0x01, 0x48, 0x0F, 0x47, 0xCA, 0xE8, 0x42, 0x0B, 0x00, 0x00 };
constexpr std::uint8_t ServerUnitBytes[]{
    0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x74, 0x24, 0x18, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x41,
    0x8B, 0xD8, 0x8B, 0xF2, 0x48, 0x8B, 0xF9, 0x48, 0x85, 0xC9, 0x75, 0x13, 0x88, 0x4C, 0x24, 0x38,
    0x48, 0x8D, 0x4C, 0x24, 0x38, 0xE8, 0x46, 0xD1, 0xFF, 0xFF, 0x84, 0xC0, 0x74, 0x01, 0xCC, 0x83,
    0xFE, 0x05, 0x75, 0x09 };
constexpr std::uint8_t SunderBytes[]{
    0x40, 0x53, 0x55, 0x57, 0x48, 0x81, 0xEC, 0xD0, 0x00, 0x00, 0x00, 0x48, 0x8B, 0x05, 0xE6, 0xBB,
    0x57, 0x02 };
constexpr std::uint8_t PierceBytes[]{
    0x40, 0x53, 0x55, 0x56, 0x41, 0x57, 0x48, 0x81, 0xEC, 0xC8, 0x00, 0x00, 0x00, 0x48, 0x8B, 0x05,
    0x04, 0xBA, 0x57, 0x02 };
constexpr std::uint8_t CheckStateBytes[]{
    0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x74, 0x24, 0x10, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x8B,
    0xDA, 0x48, 0x8B, 0xF1 };
constexpr std::uint8_t SkillByIdBytes[]{
    0x48, 0x89, 0x5C, 0x24, 0x08, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x41, 0x8B, 0xD8, 0x8B, 0xFA };
constexpr std::uint8_t ColorizeBytes[]{
    0x48, 0x89, 0x5C, 0x24, 0x18, 0x57, 0x48, 0x81, 0xEC, 0x50, 0x04, 0x00, 0x00, 0x48, 0x8B, 0x05,
    0xD4, 0xDB, 0x8F, 0x02, 0x48, 0x33, 0xC4, 0x48, 0x89, 0x84, 0x24, 0x40, 0x04, 0x00, 0x00, 0x80,
    0x39, 0x00, 0x8B, 0xFA, 0x48, 0x8B, 0xD9 };
// 077E10, whole function: setting byte +DC8h of 076010's block, or 2A23274.
constexpr std::uint8_t IconKeysBytes[]{
    0x48, 0x83, 0xEC, 0x28, 0xE8, 0xF7, 0xE1, 0xFF, 0xFF, 0x80, 0xB8, 0xC8, 0x0D, 0x00, 0x00, 0x00,
    0x75, 0x10, 0x80, 0x3D, 0x4B, 0xB4, 0x9A, 0x02, 0x00, 0x75, 0x07, 0x32, 0xC0, 0x48, 0x83, 0xC4,
    0x28, 0xC3, 0xB0, 0x01, 0x48, 0x83, 0xC4, 0x28, 0xC3 };
// 846210, whole function: byte +B9h of the UI settings object, or 650210.
constexpr std::uint8_t SeparatorBytes[]{
    0x48, 0x8B, 0x05, 0x59, 0x9F, 0xBF, 0x02, 0x48, 0x85, 0xC0, 0x74, 0x08, 0x0F, 0xB6, 0x80, 0xB9,
    0x00, 0x00, 0x00, 0xC3, 0xE9, 0xE7, 0x9F, 0xE0, 0xFF };
// 0CEDB8: the class ids the stock builder leaves without an immunity line.
constexpr std::uint8_t ImmuneClassBytes[]{
    0xB8, 0xC0, 0x02, 0x00, 0x00, 0x66, 0x44, 0x2B, 0xF0, 0x66, 0x41, 0x83, 0xFE, 0x05, 0x0F, 0x86,
    0x8D, 0x0A, 0x00, 0x00 };
// 1F8731, the stock update: movups xmm0,[r14+8] / movups [r12+0BCh],xmm0 /
// mov rcx,[r14]: the name's colour (payload +08, 16 bytes) goes to the Name
// text box at +BCh, and its text is payload +00.
constexpr std::uint8_t NameColorBytes[]{
    0x41, 0x0F, 0x10, 0x46, 0x08, 0x41, 0x0F, 0x11, 0x84, 0x24, 0xBC, 0x00, 0x00, 0x00, 0x49, 0x8B, 0x0E };
// 8563B8, the screen position walk 8562F0: movups xmm0,[rbx+70h] /
// movups [rsp+30h],xmm0 / mov ecx,[rsp+38h] / movss xmm1,[rbx+80h]: a child's
// offset is multiplied by its parent's own scale.
constexpr std::uint8_t ChildScaleBytes[]{
    0x0F, 0x10, 0x43, 0x70, 0x0F, 0x11, 0x44, 0x24, 0x30, 0x8B, 0x4C, 0x24, 0x38, 0xF3, 0x0F, 0x10, 0x8B, 0x80,
    0x00, 0x00, 0x00 };
// 8DCED5, GrowableImageWidget::Draw: mulss xmm2,[rdi+80h] / lea r14,[rdi+140h]:
// the frame art is drawn at its own size times the widget's scale; the left
// cap is the image widget at +140h.
constexpr std::uint8_t FrameArtScaleBytes[]{
    0xF3, 0x0F, 0x59, 0x97, 0x80, 0x00, 0x00, 0x00, 0x4C, 0x8D, 0xB7, 0x40, 0x01, 0x00, 0x00 };
// 8DD6FF, GrowableImageWidget setup: the left cap (+140h) is anchored at
// (0, 0.5) of the frame (+188h / +18Ch) and the right cap (+280h) at (1, 0.5)
// (+2C8h / +2CCh).
constexpr std::uint8_t CapAnchorBytes[]{
    0xC7, 0x87, 0x88, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x48, 0x8D, 0xB7, 0x40, 0x01, 0x00, 0x00, 0xC7,
    0x87, 0x8C, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x3F, 0x48, 0x8B, 0xCE, 0xC7, 0x87, 0xC8, 0x02, 0x00, 0x00,
    0x00, 0x00, 0x80, 0x3F, 0xC7, 0x87, 0xCC, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x3F };
// 533D60, inside the demon save 533CE0: mov edx,0CFh / mov rcx,rax / call
// 3351B0 (STATES_CheckState), the game's own test for a bound demon.
constexpr std::uint8_t BindDemonTestBytes[]{
    0xBA, 0xCF, 0x00, 0x00, 0x00, 0x48, 0x8B, 0xC8, 0xE8, 0x43, 0x14, 0xE0, 0xFF };
constexpr std::uint8_t SetHealthValueBytes[]{
    0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B, 0xD9, 0x85, 0xD2, 0x79, 0x04, 0x33, 0xD2, 0xEB,
    0x0A, 0xB8, 0x64, 0x00, 0x00, 0x00, 0x3B, 0xD0, 0x0F, 0x4F, 0xD0, 0x66, 0x0F, 0x6E, 0xCA, 0x0F,
    0x5B, 0xC9, 0x89, 0x91, 0x94, 0x01, 0x00, 0x00 };

// ---------------------------------------------------------------------------
// Native functions
// ---------------------------------------------------------------------------

struct StringView {
    const char* data;
    std::size_t size;
};

using BuilderFn     = std::uint64_t(__fastcall*)(void* unit, std::uint64_t position, std::uint8_t attached);
using PanelUpdateFn = std::uint64_t(__fastcall*)(void* panel, void* message, void* container, std::uint8_t attached);
using SetTextFn     = void*(__fastcall*)(void* textBox, const StringView* text);
using ColorizeFn    = char*(__fastcall*)(char* buffer, char color);
using UiScaleFn     = float(__fastcall*)();
using MeasureFn     = std::int32_t(__fastcall*)(const char* text, const void* style, float scale);
using WidgetScaleFn = float(__fastcall*)(void* widget);
using UnitCheckFn   = std::int32_t(__fastcall*)(void* unit);
using FindChildFn   = void*(__fastcall*)(void* parent, const char* name);
using GetUModsFn    = const std::uint8_t*(__fastcall*)(void* unit);
using MonTypeFlagFn = bool(__fastcall*)(void* unit, std::uint16_t mask);
using UModNamesFn   = void*(__fastcall*)(void* output, std::uint8_t dataContext, const std::uint8_t* umods,
                          std::int32_t maximum, std::int32_t color, const char* prefix);
using AllocatorFn   = void*(__fastcall*)();
using FreeFn        = void(__fastcall*)(void* allocator, const void* block);
using UnitTypeFn    = std::int32_t(__fastcall*)(void* unit);
using UnitIdFn      = std::uint32_t(__fastcall*)(void* unit, const char* file, std::int32_t line);
using ClassIdFn     = std::int32_t(__fastcall*)(void* unit, const char* file, std::int32_t line);
using DataContextFn = std::uint8_t(__fastcall*)(void* unit);
using MonStatsFn    = const std::uint8_t*(__fastcall*)(std::uint8_t dataContext, std::int32_t classId);
using ReadStatFn    = std::int32_t(__fastcall*)(void* unit, std::int32_t stat, std::int32_t layer);
using LocalIndexFn  = std::int32_t(__fastcall*)();
using LocalPlayerFn = void*(__fastcall*)(std::int32_t index);
using ServerGameFn  = void*(__fastcall*)();
using ServerUnitFn  = void*(__fastcall*)(void* game, std::int32_t type, std::uint32_t id);
using ResistStepFn  = std::int32_t(__fastcall*)(void* context, void* record, std::int32_t resist);
using CheckStateFn  = std::int32_t(__fastcall*)(void* unit, std::int32_t state);
using SkillByIdFn   = void*(__fastcall*)(void* unit, std::int32_t skillId, std::uint32_t owner);
using SkillRecordFn = const std::uint8_t*(__fastcall*)(std::uint8_t dataContext, std::int32_t skillId);
using SkillLevelFn  = std::int32_t(__fastcall*)(void* unit, void* skill, std::uint64_t bonus, std::uint64_t unused);
using TypeDescFn    = void*(__fastcall*)(void* widget);
using SetVisibleFn  = void(__fastcall*)(void* widget, std::uint64_t visible);
using SetHealthValueFn = void(__fastcall*)(void* bar, std::int32_t value);
using GetRectFn     = void*(__fastcall*)(void* widget, void* out);
using UiFlagFn      = std::uint8_t(__fastcall*)();

BuilderFn     OriginalBuilder{};
PanelUpdateFn OriginalPanelUpdate{};
SetTextFn     NativeSetText{};
ColorizeFn    NativeColorize{};
UiScaleFn     UiScale{};
MeasureFn     MeasureText{};
WidgetScaleFn WidgetScale{};
UnitCheckFn   IsDemon{};
UnitCheckFn   IsUndead{};
FindChildFn   FindChild{};
GetUModsFn    GetUMods{};
MonTypeFlagFn HasMonTypeFlag{};
UModNamesFn   UModNames{};
AllocatorFn   GetAllocator{};
UnitTypeFn    UnitType{};
UnitIdFn      UnitId{};
ClassIdFn     ClassId{};
DataContextFn DataContext{};
MonStatsFn    MonStats{};
ReadStatFn    ReadStat{};
LocalIndexFn  LocalIndex{};
LocalPlayerFn LocalPlayer{};
ServerGameFn  ServerGame{};
ServerUnitFn  ServerUnit{};
ResistStepFn  SunderStep{};
ResistStepFn  PierceStep{};
CheckStateFn  CheckState{};
SkillByIdFn   SkillById{};
SkillRecordFn SkillsRecord{};
SkillRecordFn SkillDescRecord{};
SkillLevelFn  SkillLevel{};
SetHealthValueFn SetHealthValue{};
GetRectFn     NativeGetRect{};
UiFlagFn      IconKeys{};
UiFlagFn      SeparatorStyle{};

template <typename T>
auto At(std::uint64_t rva) noexcept -> T {
    return reinterpret_cast<T>(Base + rva);
}

auto Bytes(const void* base, std::size_t offset) noexcept -> std::uint8_t* {
    return const_cast<std::uint8_t*>(static_cast<const std::uint8_t*>(base)) + offset;
}

template <typename T>
auto Read(const void* base, std::size_t offset) noexcept -> T {
    T value{};
    std::memcpy(&value, Bytes(base, offset), sizeof(T));
    return value;
}

template <typename T>
void Write(void* base, std::size_t offset, T value) noexcept {
    std::memcpy(Bytes(base, offset), &value, sizeof(T));
}

// ---------------------------------------------------------------------------
// Configuration
// ---------------------------------------------------------------------------

enum class Element : std::int32_t { Fire, Lightning, Cold, Poison, Magic, Physical, Count };

struct ElementInfo {
    const char*  placeholder;
    std::int32_t resistStat;
    std::int32_t maxStat;      // -1: none
    std::int32_t pierceStat;   // passive_*_pierce
    std::int32_t sunderStat;   // item_pierce_*_immunity
    std::int32_t defaultColor;
};

// Stat ids as the game's own tables use them (0CEDCC..0CEF7F builds the same
// {resist, sunder, pierce} triples; 4523E0 reads +8 resist, +C max, +10 pierce,
// +14 sunder from its per-type records).
constexpr std::array<ElementInfo, 6> Elements{ {
    { "fire",      39,  40, 333, 189, 1 },
    { "lightning", 41,  42, 334, 190, 9 },
    { "cold",      43,  44, 335, 187, 3 },
    { "poison",    45,  46, 336, 191, 2 },
    { "magic",     37,  -1, 358, 193, 8 },
    { "physical",  36,  -1, 366, 192, 4 },
} };

struct TemplatePart {
    enum class Kind { Literal, Value, Color } kind{ Kind::Literal };
    std::string  text;
    std::int32_t value{};
};

struct Settings {
    bool         enabled{ true };
    std::int32_t barWidth{ 800 };
    double       bossMultiplier{ 2.0 };
    double       bossHeightMultiplier{ 1.25 };
    std::int32_t frameHeight{ 101 };
    std::int32_t nameplateWidthPercent{ 85 };
    std::int32_t nameplateHeightPercent{ 50 };
    bool         showLevel{ true };
    std::string  levelLabel{ "Level" };
    bool         showHp{ true };
    bool         showHpPercent{ true };
    bool         showAffixes{ true };
    std::vector<std::int32_t> minionHiddenAffixes{ 9, 17, 18, 26, 27, 28 };
    std::int32_t affixColor{ 4 };
    bool         separateAura{ true };
    std::string  auraLevelLabel{ "Lv." };
    bool         showImmunities{ true };
    std::int32_t rowGap{ 10 };
    bool         showResists{ true };
    bool         effective{ true };
    std::string  resistTemplate{ "{fire} {lightning} {cold} {poison} {magic} {physical}" };
    std::int32_t separatorColor{ 0 };
    std::array<std::int32_t, 6> elementColors{ 1, 9, 3, 2, 8, 4 };
    bool         debugLog{ false };
    std::vector<TemplatePart> parts;
};

Settings    Config{};
std::string ConfigProblems;
std::atomic<bool> DebugOn{ false };

auto Trim(std::string_view text) -> std::string_view {
    while (!text.empty() && (text.front() == ' ' || text.front() == '\t' || text.front() == '\r')) text.remove_prefix(1);
    while (!text.empty() && (text.back() == ' ' || text.back() == '\t' || text.back() == '\r')) text.remove_suffix(1);
    return text;
}

auto StripComment(std::string_view line) -> std::string_view {
    bool quoted = false;
    for (std::size_t i = 0; i < line.size(); ++i) {
        if (line[i] == '"') quoted = !quoted;
        if (line[i] == '#' && !quoted) return line.substr(0, i);
    }
    return line;
}

void Problem(std::string_view key, const char* what) {
    if (!ConfigProblems.empty()) ConfigProblems += "; ";
    ConfigProblems.append(key.data(), key.size());
    ConfigProblems += ": ";
    ConfigProblems += what;
}

auto ParseBool(std::string_view value, bool& out) -> bool {
    if (value == "true") { out = true; return true; }
    if (value == "false") { out = false; return true; }
    return false;
}

auto ParseInt(std::string_view value, std::int32_t low, std::int32_t high, std::int32_t& out) -> bool {
    if (value.empty()) return false;
    bool negative = false;
    std::size_t i = 0;
    if (value[0] == '-') { negative = true; i = 1; }
    if (i >= value.size()) return false;
    std::int64_t number = 0;
    for (; i < value.size(); ++i) {
        if (value[i] < '0' || value[i] > '9') return false;
        number = number * 10 + (value[i] - '0');
        if (number > 1000000000LL) return false;
    }
    if (negative) number = -number;
    if (number < low || number > high) return false;
    out = static_cast<std::int32_t>(number);
    return true;
}

auto ParseDouble(std::string_view value, double low, double high, double& out) -> bool {
    std::string copy(value);
    char* end = nullptr;
    const double number = std::strtod(copy.c_str(), &end);
    if (end == copy.c_str() || *end != '\0' || !(number >= low && number <= high)) return false;
    out = number;
    return true;
}

auto ParseString(std::string_view value, std::string& out) -> bool {
    if (value.size() < 2 || value.front() != '"' || value.back() != '"') return false;
    out.assign(value.data() + 1, value.size() - 2);
    return true;
}

auto ParseIdList(std::string_view value, std::vector<std::int32_t>& out) -> bool {
    if (value.size() < 2 || value.front() != '[' || value.back() != ']') return false;
    out.clear();
    std::int64_t current = 0;
    bool have = false;
    for (const char c : value.substr(1, value.size() - 2)) {
        if (c >= '0' && c <= '9') {
            current = current * 10 + (c - '0');
            if (current > 1000000) return false;
            have = true;
        } else if (c == ',' || c == ' ' || c == '\t') {
            if (have) out.push_back(static_cast<std::int32_t>(current));
            current = 0;
            have = false;
        } else {
            return false;
        }
    }
    if (have) out.push_back(static_cast<std::int32_t>(current));
    return true;
}

auto ParseTemplate(std::string_view text, std::vector<TemplatePart>& parts) -> bool {
    parts.clear();
    std::string literal;
    auto flush = [&]() {
        if (!literal.empty()) {
            parts.push_back({ TemplatePart::Kind::Literal, literal, 0 });
            literal.clear();
        }
    };
    for (std::size_t i = 0; i < text.size();) {
        if (text[i] != '{') {
            literal.push_back(text[i++]);
            continue;
        }
        const std::size_t close = text.find('}', i);
        if (close == std::string_view::npos) return false;
        const std::string_view token = text.substr(i + 1, close - i - 1);
        bool known = false;
        for (std::size_t e = 0; e < Elements.size(); ++e) {
            if (token == Elements[e].placeholder) {
                flush();
                parts.push_back({ TemplatePart::Kind::Value, {}, static_cast<std::int32_t>(e) });
                known = true;
                break;
            }
        }
        if (!known && token.substr(0, 6) == "color:") {
            std::int32_t color = 0;
            if (!ParseInt(token.substr(6), 0, 38, color)) return false;
            flush();
            parts.push_back({ TemplatePart::Kind::Color, {}, color });
            known = true;
        }
        if (!known) return false;
        i = close + 1;
    }
    flush();
    return true;
}

void ApplyConfigLine(Settings& s, std::string_view section, std::string_view key, std::string_view value) {
    const auto color = [&](std::int32_t& out) {
        if (!ParseInt(value, 0, 38, out)) Problem(key, "expected a colour id 0 to 38");
    };
    const auto flag = [&](bool& out) {
        if (!ParseBool(value, out)) Problem(key, "expected true or false");
    };
    if (section.empty()) {
        if (key == "enabled") flag(s.enabled);
    } else if (section == "bar") {
        if (key == "width") {
            if (!ParseInt(value, 0, 4000, s.barWidth)) Problem(key, "expected 0 to 4000");
        } else if (key == "boss_width_multiplier") {
            if (!ParseDouble(value, 0.5, 4.0, s.bossMultiplier)) Problem(key, "expected 0.5 to 4.0");
        } else if (key == "boss_height_multiplier") {
            if (!ParseDouble(value, 1.0, 2.0, s.bossHeightMultiplier)) Problem(key, "expected 1.0 to 2.0");
        } else if (key == "frame_height") {
            if (!ParseInt(value, 1, 1000, s.frameHeight)) Problem(key, "expected 1 to 1000");
        }
    } else if (section == "nameplate") {
        if (key == "width_percent") {
            if (!ParseInt(value, 10, 200, s.nameplateWidthPercent)) Problem(key, "expected 10 to 200");
        } else if (key == "height_percent") {
            if (!ParseInt(value, 10, 200, s.nameplateHeightPercent)) Problem(key, "expected 10 to 200");
        }
    } else if (section == "text") {
        if (key == "show_level") flag(s.showLevel);
        else if (key == "level_label") { if (!ParseString(value, s.levelLabel)) Problem(key, "expected a quoted string"); }
        else if (key == "show_hp") flag(s.showHp);
        else if (key == "show_hp_percent") flag(s.showHpPercent);
        else if (key == "show_affixes") flag(s.showAffixes);
        else if (key == "minion_hidden_affixes") {
            std::vector<std::int32_t> ids;
            if (ParseIdList(value, ids)
                    && std::all_of(ids.begin(), ids.end(), [](std::int32_t id) { return id >= 1 && id <= MaximumUMod; })) {
                s.minionHiddenAffixes = ids;
            } else {
                Problem(key, "expected a list of monumod.txt ids from 1 to 44; using the default");
            }
        }
        else if (key == "affix_color") color(s.affixColor);
        else if (key == "separate_aura") flag(s.separateAura);
        else if (key == "aura_level_label") { if (!ParseString(value, s.auraLevelLabel)) Problem(key, "expected a quoted string"); }
        else if (key == "show_immunities") flag(s.showImmunities);
        else if (key == "row_gap") {
            if (!ParseInt(value, 0, 200, s.rowGap)) Problem(key, "expected 0 to 200");
        }
    } else if (section == "resistances") {
        if (key == "show") flag(s.showResists);
        else if (key == "mode") {
            std::string mode;
            if (!ParseString(value, mode) || (mode != "effective" && mode != "monster")) {
                Problem(key, "expected \"effective\" or \"monster\"");
            } else {
                s.effective = mode == "effective";
            }
        } else if (key == "template") {
            std::string text;
            if (!ParseString(value, text)) Problem(key, "expected a quoted string");
            else s.resistTemplate = text;
        } else if (key == "separator_color") color(s.separatorColor);
        else {
            for (std::size_t e = 0; e < Elements.size(); ++e) {
                if (key == std::string(Elements[e].placeholder) + "_color") color(s.elementColors[e]);
            }
        }
    } else if (section == "summon_cap") {
        // 1.0.8 kept a copy of the cap plugin's settings here. The cap plugin
        // is now asked directly, so these keys do nothing.
        Problem(key, "[summon_cap] is no longer read; the Summon Resist Cap plugin is asked directly");
    } else if (section == "debug") {
        if (key == "log") flag(s.debugLog);
    }
}

void ReadConfiguration() {
    Settings parsed{};
    ConfigProblems.clear();
    if (!Context->EnsureConfig(DefaultConfigToml)) {
        Context->LogWarn("MonsterDisplay: the config file could not be created; using defaults.");
    } else {
        std::string buffer(MaximumConfigBytes, '\0');
        if (Context->ReadConfig(buffer.data(), static_cast<std::uint32_t>(buffer.size()), nullptr)) {
            buffer.resize(std::strlen(buffer.c_str()));
            std::string_view text(buffer);
            std::string_view section;
            while (!text.empty()) {
                const std::size_t end = text.find('\n');
                const std::string_view line = Trim(StripComment(text.substr(0, end)));
                text = end == std::string_view::npos ? std::string_view{} : text.substr(end + 1);
                if (line.empty()) continue;
                if (line.front() == '[') {
                    if (line.back() == ']') section = Trim(line.substr(1, line.size() - 2));
                    continue;
                }
                const std::size_t equals = line.find('=');
                if (equals == std::string_view::npos) continue;
                ApplyConfigLine(parsed, section, Trim(line.substr(0, equals)), Trim(line.substr(equals + 1)));
            }
        } else {
            Context->LogWarn("MonsterDisplay: the config file could not be read; using defaults.");
        }
    }
    if (!ParseTemplate(parsed.resistTemplate, parsed.parts)) {
        Problem("template", "unknown placeholder or unclosed brace; using the default");
        parsed.resistTemplate = Settings{}.resistTemplate;
        ParseTemplate(parsed.resistTemplate, parsed.parts);
    }
    Config = parsed;
    DebugOn.store(Config.debugLog);
    if (!ConfigProblems.empty()) D2RL::LogWarnF(Context, "MonsterDisplay: %s", ConfigProblems.c_str());
}

// ---------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------

enum class PluginState { NotLoaded, DisabledByConfig, UnsupportedBuild, InstallFailed, Active };
PluginState       State{ PluginState::NotLoaded };
std::atomic<bool> Active{};
std::string       InactiveReason;

const D2RL::ThreadService*       Threads{};
const D2RL::LocalizationService* Lang{};
const D2RL::NetworkService*      Network{};
D2RL::Network::ChannelHandle     Channel{ D2RL::Network::InvalidChannelHandle };

// The monster under the cursor, as the builder saw it (UI thread only).
struct HoverInfo {
    bool          valid{};
    void*         clientUnit{};
    std::uint32_t unitId{};
    std::int32_t  classId{ -1 };
    std::uint8_t  dataContext{};
    bool          boss{};
    bool          undead{};
    bool          demon{};
    std::array<std::uint8_t, 10> umods{};
    std::size_t   umodCount{};
    bool          auraEnchanted{};
    bool          minion{};
    bool          ally{};
    std::uint64_t tick{};
};
HoverInfo Hover{};

// Exact data from the server unit.
struct MonsterData {
    std::uint32_t unitId{};
    std::int64_t  life{};
    std::int64_t  maxLife{};
    std::int32_t  level{};
    std::array<std::int16_t, 6> resist{};
    std::uint8_t  flags{};  // 1: resistances valid, 2: capped as an ally, 4: effective (attacker applied),
                            // 8: shown as an ally (own values)
    std::uint8_t  immune{}; // bit per Element: the value the hovering player's hits meet is 100+
    std::uint8_t  auraCount{};
    std::array<std::uint16_t, 4> auraSkill{};
    std::array<std::uint16_t, 4> auraLevel{};
};

#pragma pack(push, 1)
struct WireRequest {
    std::uint32_t unitId;
    std::uint32_t viewerId;
};
struct WireReply {
    std::uint32_t unitId;
    std::int64_t  life;
    std::int64_t  maxLife;
    std::int32_t  level;
    std::int16_t  resist[6];
    std::uint8_t  flags;
    std::uint8_t  immune;
    std::uint8_t  auraCount;
    std::uint16_t auraSkill[4];
    std::uint16_t auraLevel[4];
};
#pragma pack(pop)
static_assert(sizeof(WireReply) <= D2RL::Network::MaxPayloadSize);

struct CacheEntry {
    MonsterData   data{};
    std::uint64_t received{};
    bool          used{};
};
std::mutex                 CacheMutex;
std::array<CacheEntry, 16> Cache{};

// Single player / host requests from the UI to the game thread.
struct LocalRequest {
    std::uint32_t unitId{};
    std::uint32_t viewerId{};
    std::uintptr_t clientUnit{};
    bool          pending{};
};
std::mutex        RequestMutex;
LocalRequest      Pending{};

// Host side of the network channel.
struct HostRequest {
    D2RL::Network::PeerHandle peer{};
    std::uint32_t unitId{};
    std::uint32_t viewerId{};
};
std::mutex               HostMutex;
std::vector<HostRequest> HostQueue;

enum class DataMode { Unknown, Local, Network };
std::atomic<DataMode> Mode{ DataMode::Unknown };
std::atomic<bool>     Connected{};
std::uint64_t         LastConnectTick{};
std::uint64_t         LastRequestTick{};
std::uint32_t         LastRequestUnit{};

// Counters for the console command.
std::atomic<std::uint64_t> BuilderCalls{};
std::atomic<std::uint64_t> PanelUpdates{};
std::atomic<std::uint64_t> NameplateUpdates{};
std::atomic<std::uint64_t> NameplateBoldUpdates{};
std::atomic<std::uint64_t> LocalRequests{};
std::atomic<std::uint64_t> NetworkRequests{};
std::atomic<std::uint64_t> Computed{};
std::atomic<std::uint64_t> ComputeMisses{};
std::atomic<std::uint64_t> RepliesSent{};
std::atomic<std::uint64_t> RepliesReceived{};
std::atomic<std::uint64_t> CallbacksRun{};
std::atomic<std::uint64_t> MissNoUnit{};
std::atomic<std::uint64_t> MissClientUnit{};
std::atomic<std::uint32_t> LastQueueResult{};
std::uint64_t              FirstQueueTick{};
bool                       WarnedQueue{};
bool                       WarnedNoCallbacks{};
std::atomic<bool>          WarnedMiss{};

auto Now() noexcept -> std::uint64_t {
    return GetTickCount64();
}

void Debug(const char* format, ...) {
    if (!DebugOn.load(std::memory_order_relaxed)) return;
    char text[512];
    va_list args;
    va_start(args, format);
    std::vsnprintf(text, sizeof(text), format, args);
    va_end(args);
    Context->LogInfo(text);
}

void StoreData(const MonsterData& data) {
    std::lock_guard lock(CacheMutex);
    CacheEntry* slot = nullptr;
    for (auto& entry : Cache) {
        if (entry.used && entry.data.unitId == data.unitId) {
            slot = &entry;
            break;
        }
    }
    if (slot == nullptr) {
        slot = &Cache[0];
        for (auto& entry : Cache) {
            if (!entry.used) {
                slot = &entry;
                break;
            }
            if (entry.received < slot->received) slot = &entry;
        }
    }
    slot->data     = data;
    slot->received = Now();
    slot->used     = true;
}

auto LoadData(std::uint32_t unitId, MonsterData& out, std::uint64_t& age) -> bool {
    std::lock_guard lock(CacheMutex);
    for (const auto& entry : Cache) {
        if (entry.used && entry.data.unitId == unitId) {
            out = entry.data;
            age = Now() - entry.received;
            return true;
        }
    }
    return false;
}

void ResetSession() {
    {
        std::lock_guard lock(CacheMutex);
        Cache = {};
    }
    {
        std::lock_guard lock(RequestMutex);
        Pending = {};
    }
    Hover = {};
    Mode.store(DataMode::Unknown);
    LastRequestTick = 0;
    LastRequestUnit = 0;
    FirstQueueTick  = 0;
}

// ---------------------------------------------------------------------------
// Game thread: exact life, level, resistances and auras
// ---------------------------------------------------------------------------

auto ReadImmediate(std::uint64_t opcodeRva, std::uint8_t opcodeLow, std::uint8_t opcodeHigh, std::int32_t fallback,
                   std::int32_t low, std::int32_t high) -> std::int32_t {
    const std::uint8_t opcode = Read<std::uint8_t>(reinterpret_cast<void*>(Base), opcodeRva);
    if (opcode < opcodeLow || opcode > opcodeHigh) return fallback;
    const std::int32_t value = Read<std::int32_t>(reinterpret_cast<void*>(Base), opcodeRva + 1);
    return value >= low && value <= high ? value : fallback;
}

struct ResistRules {
    std::int32_t uncappedFloor{ -100 };
    std::int32_t cappedFloor{ -100 };
    std::int32_t physicalCap{ 50 };
    std::int32_t ceiling{ 95 };
    std::int32_t base{ 75 };
    bool         capLive{};
};

auto CurrentRules() -> ResistRules {
    ResistRules rules{};
    rules.uncappedFloor = ReadImmediate(UncappedFloorRva, 0xB8, 0xBF, -100, -100000, 0);
    rules.cappedFloor   = ReadImmediate(CappedFloorRva, 0xB8, 0xBF, -100, -100000, 0);
    rules.physicalCap   = ReadImmediate(PhysicalCapOpRva, 0xB8, 0xBF, 50, 0, 1000);
    rules.ceiling       = ReadImmediate(ElementalCapOpRva, 0xB8, 0xBF, 95, 0, 1000);
    // The Summon Resist Cap plugin replaces the gate's "cmp [rsi+24h],ebp /
    // jnz" (39 6E 24 75 57) with a jump into its relay.
    rules.capLive = Read<std::uint8_t>(reinterpret_cast<void*>(Base), CapGateRva) == 0xE9;
    return rules;
}

// Summon Resist Cap's "cap-decision" service, in the layout its provider
// publishes (celestialrayone.summon-resist-cap 3.3.0 and later).
struct CapDecisionService {
    using MonsterCapStateFn = std::int32_t(__cdecl*)(void* monster) noexcept;

    std::uint32_t     serviceSize;
    std::uint32_t     serviceVersion;
    // Game thread, server monster unit: 1 capped, 0 not capped, -1 the cap
    // plugin is not deciding (disabled, not armed, not a monster).
    MonsterCapStateFn monsterCapState;
};
static_assert(sizeof(CapDecisionService) == 16);
static_assert(offsetof(CapDecisionService, monsterCapState) == 8);

constexpr char          CapProviderId[]   = "celestialrayone.summon-resist-cap";
constexpr char          CapServiceName[]  = "cap-decision";
constexpr std::uint32_t CapServiceVersion = 1;
constexpr std::uint64_t CapServiceRetryMs = 5000;

// Game thread only, apart from the two atomics the console reads.
D2RL::PluginCommunication::ServiceLease<CapDecisionService> CapLease;
std::uint64_t              CapLastAttempt{};
std::atomic<bool>          CapAcquired{};
std::atomic<std::uint32_t> CapLastResult{ 0xFFFFFFFFU };
std::atomic<bool>          CapWarned{};

// Acquires the service on first use and retries every few seconds while it is
// missing. Held until unload.
auto CapService() -> const CapDecisionService* {
    if (CapLease) return CapLease.Get();
    const std::uint64_t now = Now();
    if (CapLastAttempt != 0 && now - CapLastAttempt < CapServiceRetryMs) return nullptr;
    CapLastAttempt = now;
    const D2RL::PluginCommunicationService* communication = nullptr;
    if (Context->QueryService(&communication) != D2RL::ServiceQueryResult::Success
            || !D2RL::HasPluginCommunicationServiceField(communication, D2RL::PluginCommunicationServiceRequiredSize)) {
        CapLastResult.store(static_cast<std::uint32_t>(D2RL::PluginCommunication::Result::Unavailable));
        return nullptr;
    }
    const auto result = D2RL::PluginCommunication::Acquire(Context, communication, CapProviderId, CapServiceName,
        CapServiceVersion, static_cast<std::uint32_t>(sizeof(CapDecisionService)), &CapLease);
    CapLastResult.store(static_cast<std::uint32_t>(result));
    if (result != D2RL::PluginCommunication::Result::Success || !CapLease || CapLease->monsterCapState == nullptr) {
        CapLease.Reset();
        return nullptr;
    }
    CapAcquired.store(true);
    Context->LogInfo("MonsterDisplay: using the Summon Resist Cap cap-decision service.");
    return CapLease.Get();
}

// Whether the engine caps this server monster's resistances. Asked only while
// the cap plugin's gate is live.
auto CappedBySummonResistCap(void* monster) -> bool {
    const CapDecisionService* service = CapService();
    if (service == nullptr) {
        if (!CapWarned.exchange(true)) {
            D2RL::LogWarnF(Context,
                "MonsterDisplay: the Summon Resist Cap gate is live but its cap-decision service is not available "
                "(result %u; it needs Summon Resist Cap 3.3.0 or later). Allied monsters show uncapped "
                "resistances until it is.",
                CapLastResult.load());
        }
        return false;
    }
    return service->monsterCapState(monster) == 1;
}

// Monster Life Bars' "life-totals" service, in the layout its provider
// publishes (celestialrayone.monster-life-bars 1.0.0 and later).
struct LifeTotalsService {
    using LifeTotalsFn = bool(__cdecl*)(const void* unit, std::int64_t* current, std::int64_t* maximum) noexcept;

    std::uint32_t serviceSize;
    std::uint32_t serviceVersion;
    // Game thread, server unit: true when it carries life bars; current and
    // maximum are its total life in 256ths.
    LifeTotalsFn  lifeTotals;
};
static_assert(sizeof(LifeTotalsService) == 16);
static_assert(offsetof(LifeTotalsService, lifeTotals) == 8);

constexpr char          LifeBarsProviderId[]     = "celestialrayone.monster-life-bars";
constexpr char          LifeBarsServiceName[]    = "life-totals";
constexpr std::uint32_t LifeBarsServiceVersion   = 1;
constexpr std::uint64_t LifeBarsServiceRetryMs   = 5000;

// Game thread only, apart from the two atomics the console reads.
D2RL::PluginCommunication::ServiceLease<LifeTotalsService> LifeBarsLease;
std::uint64_t              LifeBarsLastAttempt{};
std::atomic<bool>          LifeBarsAcquired{};
std::atomic<std::uint32_t> LifeBarsLastResult{ 0xFFFFFFFFU };

// Acquires the service on first use and retries every few seconds while it is
// missing (Monster Life Bars not installed, or not loaded yet). Held until
// unload.
auto LifeBarsService() -> const LifeTotalsService* {
    if (LifeBarsLease) return LifeBarsLease.Get();
    const std::uint64_t now = Now();
    if (LifeBarsLastAttempt != 0 && now - LifeBarsLastAttempt < LifeBarsServiceRetryMs) return nullptr;
    LifeBarsLastAttempt = now;
    const D2RL::PluginCommunicationService* communication = nullptr;
    if (Context->QueryService(&communication) != D2RL::ServiceQueryResult::Success
            || !D2RL::HasPluginCommunicationServiceField(communication, D2RL::PluginCommunicationServiceRequiredSize)) {
        LifeBarsLastResult.store(static_cast<std::uint32_t>(D2RL::PluginCommunication::Result::Unavailable));
        return nullptr;
    }
    const auto result = D2RL::PluginCommunication::Acquire(Context, communication, LifeBarsProviderId,
        LifeBarsServiceName, LifeBarsServiceVersion, static_cast<std::uint32_t>(sizeof(LifeTotalsService)),
        &LifeBarsLease);
    LifeBarsLastResult.store(static_cast<std::uint32_t>(result));
    if (result != D2RL::PluginCommunication::Result::Success || !LifeBarsLease || LifeBarsLease->lifeTotals == nullptr) {
        LifeBarsLease.Reset();
        return nullptr;
    }
    LifeBarsAcquired.store(true);
    Context->LogInfo("MonsterDisplay: using the Monster Life Bars life-totals service.");
    return LifeBarsLease.Get();
}

// Monster Affixes' "aura-enchanted" service (celestialrayone.monster-affixes
// 1.5.0 and later): the skills its Aura Enchanted roll can give. kind 0 = the
// ones put in the skill list, kind 1 = the herald extras (stat 151 layers).
// It writes up to capacity ids and returns how many it wrote.
struct AuraEnchantedService {
    using CandidatesFn = std::uint32_t(__cdecl*)(std::uint32_t kind, std::uint16_t* skills, std::uint32_t capacity) noexcept;

    std::uint32_t serviceSize;
    std::uint32_t serviceVersion;
    CandidatesFn  candidates;
};
static_assert(sizeof(AuraEnchantedService) == 16);
static_assert(offsetof(AuraEnchantedService, candidates) == 8);

constexpr char          AffixesProviderId[]   = "celestialrayone.monster-affixes";
constexpr char          AffixesServiceName[]  = "aura-enchanted";
constexpr std::uint32_t AffixesServiceVersion = 1;
constexpr std::uint64_t AffixesServiceRetryMs = 5000;
constexpr std::uint32_t AuraKindSkillList     = 0;
constexpr std::uint32_t AuraKindHerald        = 1;
constexpr std::size_t   MaxAuraCandidates     = 128;

// Summon Enhancements (celestialrayone.summon-enhancements 2.2.0 and later)
// publishes the same service for the list bound demons roll from: kind 0 =
// every skill of its auras key, kind 1 = none.
constexpr char SummonsProviderId[] = "celestialrayone.summon-enhancements";

// Game thread only.
D2RL::PluginCommunication::ServiceLease<AuraEnchantedService> AffixesLease;
std::uint64_t AffixesLastAttempt{};
D2RL::PluginCommunication::ServiceLease<AuraEnchantedService> SummonsLease;
std::uint64_t SummonsLastAttempt{};

// Acquired on first use and retried every few seconds while it is missing
// (the provider not installed, or its aura list off). Held until unload.
auto AcquireAuraService(D2RL::PluginCommunication::ServiceLease<AuraEnchantedService>& lease, std::uint64_t& lastAttempt,
        const char* provider, const char* found) -> const AuraEnchantedService* {
    if (lease) return lease.Get();
    const std::uint64_t now = Now();
    if (lastAttempt != 0 && now - lastAttempt < AffixesServiceRetryMs) return nullptr;
    lastAttempt = now;
    const D2RL::PluginCommunicationService* communication = nullptr;
    if (Context->QueryService(&communication) != D2RL::ServiceQueryResult::Success
            || !D2RL::HasPluginCommunicationServiceField(communication, D2RL::PluginCommunicationServiceRequiredSize)) {
        return nullptr;
    }
    const auto result = D2RL::PluginCommunication::Acquire(Context, communication, provider, AffixesServiceName,
        AffixesServiceVersion, static_cast<std::uint32_t>(sizeof(AuraEnchantedService)), &lease);
    if (result != D2RL::PluginCommunication::Result::Success || !lease || lease->candidates == nullptr) {
        lease.Reset();
        return nullptr;
    }
    Context->LogInfo(found);
    return lease.Get();
}

// Without it the game's own tables are read, as before.
auto AffixesService() -> const AuraEnchantedService* {
    return AcquireAuraService(AffixesLease, AffixesLastAttempt, AffixesProviderId,
        "MonsterDisplay: using the Monster Affixes aura-enchanted service.");
}

// Bound demon auras. Without it those are not found, as before.
auto SummonsService() -> const AuraEnchantedService* {
    return AcquireAuraService(SummonsLease, SummonsLastAttempt, SummonsProviderId,
        "MonsterDisplay: using the Summon Enhancements aura-enchanted service (bound demon auras).");
}

// Bound Demon Scaling's "stats-row" service, in the layout its provider
// publishes (celestialrayone.bound-demon-scaling 1.0.0 and later).
struct StatsRowService {
    using StatsRowFn = std::int32_t(__cdecl*)(void* unit) noexcept;

    std::uint32_t serviceSize;
    std::uint32_t serviceVersion;
    // Game thread, server unit: the monstats.txt row a bound demon takes its
    // stats from, -1 for every other unit (and while that plugin is off or
    // names no pseudo row).
    StatsRowFn    statsRow;
};
static_assert(sizeof(StatsRowService) == 16);
static_assert(offsetof(StatsRowService, statsRow) == 8);

constexpr char          StatsRowProviderId[]   = "celestialrayone.bound-demon-scaling";
constexpr char          StatsRowServiceName[]  = "stats-row";
constexpr std::uint32_t StatsRowServiceVersion = 1;
constexpr std::uint64_t StatsRowServiceRetryMs = 5000;

// Game thread only, apart from the atomics the console reads.
D2RL::PluginCommunication::ServiceLease<StatsRowService> StatsRowLease;
std::uint64_t              StatsRowLastAttempt{};
std::atomic<bool>          StatsRowAcquired{};
std::atomic<std::uint32_t> StatsRowLastResult{ 0xFFFFFFFFU };
std::atomic<std::uint64_t> PseudoRowDecisions{};

// Set at load when 533D60 still holds the game's bound demon test; read by the
// UI thread for the pre-data immunity line.
std::atomic<bool> BoundDemonHint{};

// Acquires the service on first use and retries every few seconds while it is
// missing (Bound Demon Scaling not installed, or not loaded yet). Held until
// unload.
auto StatsRowProvider() -> const StatsRowService* {
    if (StatsRowLease) return StatsRowLease.Get();
    const std::uint64_t now = Now();
    if (StatsRowLastAttempt != 0 && now - StatsRowLastAttempt < StatsRowServiceRetryMs) return nullptr;
    StatsRowLastAttempt = now;
    const D2RL::PluginCommunicationService* communication = nullptr;
    if (Context->QueryService(&communication) != D2RL::ServiceQueryResult::Success
            || !D2RL::HasPluginCommunicationServiceField(communication, D2RL::PluginCommunicationServiceRequiredSize)) {
        StatsRowLastResult.store(static_cast<std::uint32_t>(D2RL::PluginCommunication::Result::Unavailable));
        return nullptr;
    }
    const auto result = D2RL::PluginCommunication::Acquire(Context, communication, StatsRowProviderId,
        StatsRowServiceName, StatsRowServiceVersion, static_cast<std::uint32_t>(sizeof(StatsRowService)),
        &StatsRowLease);
    StatsRowLastResult.store(static_cast<std::uint32_t>(result));
    if (result != D2RL::PluginCommunication::Result::Success || !StatsRowLease || StatsRowLease->statsRow == nullptr) {
        StatsRowLease.Reset();
        return nullptr;
    }
    StatsRowAcquired.store(true);
    Context->LogInfo("MonsterDisplay: using Bound Demon Scaling's stats-row service; a bound demon is decided by its "
                     "pseudo row.");
    return StatsRowLease.Get();
}

// The monstats.txt row whose Align decides whether a server monster is shown
// as an ally: a bound demon's pseudo row when Bound Demon Scaling names one,
// the monster's own row otherwise. Game thread only.
auto AllyRowFor(void* monster, std::int32_t classId) -> std::int32_t {
    if (const StatsRowService* service = StatsRowProvider(); service != nullptr) {
        const std::int32_t row = service->statsRow(monster);
        if (row >= 0) {
            PseudoRowDecisions.fetch_add(1, std::memory_order_relaxed);
            return row;
        }
    }
    return classId;
}

auto ResolveResist(void* game, void* monster, void* attacker, const ElementInfo& element, bool physical,
                   const ResistRules& rules, bool capped) -> std::int32_t {
    std::int32_t resist = ReadStat(monster, element.resistStat, 0);
    if (attacker != nullptr) {
        // The two steps read only these fields: context +0 game (its byte at
        // +106h is the data context), +10h attacker, +18h defender, +24h
        // "uncapped" (non-zero for monsters); record +8 resist stat, +C max
        // stat, +10h pierce stat, +14h sunder stat.
        alignas(16) std::uint8_t context[0x80]{};
        Write<void*>(context, 0x00, game);
        Write<void*>(context, 0x10, attacker);
        Write<void*>(context, 0x18, monster);
        Write<std::uint32_t>(context, 0x24, 1);
        std::int32_t damage = 1;
        alignas(16) std::uint8_t record[0x40]{};
        Write<void*>(record, 0x00, &damage);
        Write<std::int32_t>(record, 0x08, element.resistStat);
        Write<std::int32_t>(record, 0x0C, element.maxStat);
        Write<std::int32_t>(record, 0x10, element.pierceStat);
        Write<std::int32_t>(record, 0x14, element.sunderStat);
        resist = SunderStep(context, record, resist);
        resist = PierceStep(context, record, resist);
    }
    if (resist > 0) {
        if (capped) {
            std::int32_t cap = element.maxStat < 0 ? (physical ? rules.physicalCap : rules.base)
                                                   : ReadStat(monster, element.maxStat, 0) + rules.base;
            cap    = std::min(cap, rules.ceiling);
            resist = std::min(std::max(resist, rules.cappedFloor), cap);
        }
        if (physical && attacker != nullptr && CheckState(attacker, SanctuaryState) != 0 && IsUndead(monster) != 0) {
            resist = 0;
        }
        return resist;
    }
    return std::max(resist, rules.uncappedFloor);
}

void AddAura(MonsterData& data, std::int32_t skillId, std::int32_t level) {
    if (skillId <= 0 || skillId > 0xFFFF || level <= 0) return;
    for (std::uint8_t i = 0; i < data.auraCount; ++i) {
        if (data.auraSkill[i] == skillId) return;
    }
    if (data.auraCount >= data.auraSkill.size()) return;
    data.auraSkill[data.auraCount] = static_cast<std::uint16_t>(skillId);
    data.auraLevel[data.auraCount] = static_cast<std::uint16_t>(std::min(level, 0xFFFF));
    ++data.auraCount;
}

void ReadAuras(void* monster, MonsterData& data) {
    const std::uint8_t* umods = GetUMods(monster);
    if (umods == nullptr) return;
    bool enchanted = false;
    for (std::size_t i = 0; i < 9 && umods[i] != 0; ++i) {
        if (umods[i] == AuraEnchanted) enchanted = true;
    }
    if (!enchanted) return;

    std::array<std::uint16_t, MaxAuraCandidates> skills{};
    const auto                  capacity = static_cast<std::uint32_t>(skills.size());
    const AuraEnchantedService* affixes  = AffixesService();

    // Skills Aura Enchanted puts in the skill list.
    std::uint32_t count = 0;
    if (affixes != nullptr) {
        count = std::min(affixes->candidates(AuraKindSkillList, skills.data(), capacity), capacity);
    } else {
        const auto* table = reinterpret_cast<const std::uint8_t*>(Base + AuraTableRva);
        for (std::size_t i = 0; i < 7; ++i) {
            const std::int32_t skillId = Read<std::int32_t>(table, i * 24 + 20);
            if (skillId > 0 && skillId <= 0xFFFF) skills[count++] = static_cast<std::uint16_t>(skillId);
        }
    }
    // A bound demon's aura comes from Summon Enhancements' list instead.
    if (const AuraEnchantedService* summons = SummonsService(); summons != nullptr && count < capacity) {
        count += std::min(summons->candidates(AuraKindSkillList, skills.data() + count, capacity - count), capacity - count);
    }
    for (std::uint32_t i = 0; i < count; ++i) {
        void* skill = SkillById(monster, skills[i], 0xFFFFFFFFU);
        if (skill != nullptr) AddAura(data, skills[i], SkillLevel(monster, skill, 1, 0));
    }

    // Herald extras: stat 151 with the skill id as its layer.
    count = 0;
    if (affixes != nullptr) {
        count = std::min(affixes->candidates(AuraKindHerald, skills.data(), capacity), capacity);
    } else {
        const auto* herald = reinterpret_cast<const std::uint8_t*>(Base + HeraldTableRva);
        for (std::size_t i = 0; i < 4; ++i) {
            const std::int32_t skillId = Read<std::int32_t>(herald, i * 24 + 20) & 0xFFFF;
            if (skillId > 0) skills[count++] = static_cast<std::uint16_t>(skillId);
        }
    }
    for (std::uint32_t i = 0; i < count; ++i) {
        const std::int32_t level = ReadStat(monster, StatItemAura, skills[i]);
        if (level > 0) AddAura(data, skills[i], level);
    }
}

// Runs on the authoritative game thread only. The server copy of a unit is
// found the way the game's own hover path finds it (0CF859 -> 429110):
// current server game 405790, then SUNIT_GetServerUnit 48FE80 (game, type, id).
auto ComputeData(std::uint32_t unitId, std::uint32_t viewerId, std::uintptr_t clientUnit, MonsterData& data) -> bool {
    data = {};
    data.unitId = unitId;
    void* game  = ServerGame();
    if (game == nullptr) {
        MissNoUnit.fetch_add(1, std::memory_order_relaxed);
        if (!WarnedMiss.exchange(true)) Context->LogWarn("MonsterDisplay: there is no server game to read from.");
        return false;
    }
    void* monster = ServerUnit(game, UnitTypeMonster, unitId);
    if (monster == nullptr || UnitType(monster) != UnitTypeMonster) {
        MissNoUnit.fetch_add(1, std::memory_order_relaxed);
        if (!WarnedMiss.exchange(true)) {
            D2RL::LogWarnF(Context, "MonsterDisplay: monster %u has no server unit (first miss, later ones are counted).",
                unitId);
        }
        return false;
    }
    if (reinterpret_cast<std::uintptr_t>(monster) == clientUnit) {
        MissClientUnit.fetch_add(1, std::memory_order_relaxed);
        if (!WarnedMiss.exchange(true)) {
            D2RL::LogWarnF(Context, "MonsterDisplay: the server lookup returned the client copy of monster %u.", unitId);
        }
        return false;
    }
    void* viewer = viewerId != 0xFFFFFFFFU ? ServerUnit(game, UnitTypePlayer, viewerId) : nullptr;
    if (viewer != nullptr && UnitType(viewer) != UnitTypePlayer) viewer = nullptr;

    std::int64_t             totalLife = 0;
    std::int64_t             totalMax  = 0;
    const LifeTotalsService* lifeBars  = LifeBarsService();
    if (lifeBars != nullptr && lifeBars->lifeTotals(monster, &totalLife, &totalMax)) {
        data.life    = totalLife >> 8;
        data.maxLife = totalMax >> 8;
    } else {
        data.life    = static_cast<std::int32_t>(static_cast<std::uint32_t>(ReadStat(monster, StatLife, 0)) >> 8);
        data.maxLife = static_cast<std::int32_t>(static_cast<std::uint32_t>(ReadStat(monster, StatMaxLife, 0)) >> 8);
    }
    data.level   = ReadStat(monster, StatLevel, 0);

    const ResistRules  rules       = CurrentRules();
    const bool         capped      = rules.capLive && CappedBySummonResistCap(monster);
    // An ally (monstats Align 1) cannot be hit by the player, so neither the
    // player's sunder nor his -enemy resistance applies to it: it shows its
    // own values in both modes, and so does its immunity line. A bound demon
    // is decided by the row Bound Demon Scaling gives it, not by the hostile
    // row it was bound from.
    const std::int32_t      classId = ClassId(monster, SourceFile, __LINE__);
    const std::int32_t      allyRow = classId >= 0 ? AllyRowFor(monster, classId) : -1;
    const std::uint8_t*     record  = allyRow >= 0 ? MonStats(DataContext(monster), allyRow) : nullptr;
    const bool              ally    = record != nullptr && record[MonStatsAlign] == 1;
    void*                   hitter  = ally ? nullptr : viewer;
    void*                   attacker = Config.effective ? hitter : nullptr;
    for (std::size_t e = 0; e < Elements.size(); ++e) {
        const bool physical = e == static_cast<std::size_t>(Element::Physical);
        const std::int32_t value = ResolveResist(game, monster, attacker, Elements[e], physical, rules, capped);
        data.resist[e] = static_cast<std::int16_t>(std::clamp(value, -32000, 32000));
    }
    data.flags = static_cast<std::uint8_t>(1U | (capped ? 2U : 0U) | (attacker != nullptr ? 4U : 0U) | (ally ? 8U : 0U));
    // Immunities follow what the hovering player's hits meet, as the stock
    // line does (it subtracts the local player's sunder), in both modes.
    for (std::size_t e = 0; e < Elements.size(); ++e) {
        const bool physical = e == static_cast<std::size_t>(Element::Physical);
        const std::int32_t value = attacker == hitter
            ? data.resist[e]
            : ResolveResist(game, monster, hitter, Elements[e], physical, rules, capped);
        if (value >= ImmuneValue) data.immune = static_cast<std::uint8_t>(data.immune | (1U << e));
    }
    ReadAuras(monster, data);
    return true;
}

void __cdecl LocalCompute(const D2RL::PluginContext*, void*) noexcept {
    CallbacksRun.fetch_add(1, std::memory_order_relaxed);
    LocalRequest request{};
    {
        std::lock_guard lock(RequestMutex);
        request         = Pending;
        Pending.pending = false;
    }
    if (!request.pending || !Active.load()) return;
    MonsterData data{};
    if (ComputeData(request.unitId, request.viewerId, request.clientUnit, data)) {
        StoreData(data);
        Computed.fetch_add(1, std::memory_order_relaxed);
        Debug("MonsterDisplay: unit %u life %lld/%lld level %d res %d %d %d %d %d %d auras %u", data.unitId,
            static_cast<long long>(data.life), static_cast<long long>(data.maxLife), data.level, data.resist[0],
            data.resist[1], data.resist[2], data.resist[3], data.resist[4],
            data.resist[5], data.auraCount);
    } else {
        ComputeMisses.fetch_add(1, std::memory_order_relaxed);
        Debug("MonsterDisplay: no server unit for monster %u", request.unitId);
    }
}

// ---------------------------------------------------------------------------
// Network: joined TCP/IP clients ask the host
// ---------------------------------------------------------------------------

void __cdecl HostDrain(const D2RL::PluginContext*, void*) noexcept {
    std::vector<HostRequest> requests;
    {
        std::lock_guard lock(HostMutex);
        requests.swap(HostQueue);
    }
    if (Network == nullptr || !Active.load()) return;
    for (const HostRequest& request : requests) {
        MonsterData data{};
        if (!ComputeData(request.unitId, request.viewerId, 0, data)) continue;
        WireReply reply{};
        reply.unitId    = data.unitId;
        reply.life      = data.life;
        reply.maxLife   = data.maxLife;
        reply.level     = data.level;
        reply.flags     = data.flags;
        reply.immune    = data.immune;
        reply.auraCount = data.auraCount;
        for (std::size_t i = 0; i < 6; ++i) reply.resist[i] = data.resist[i];
        for (std::size_t i = 0; i < 4; ++i) {
            reply.auraSkill[i] = data.auraSkill[i];
            reply.auraLevel[i] = data.auraLevel[i];
        }
        if (Network->sendToClient(Context, Channel, request.peer, MessageReply, &reply, sizeof(reply))
                == D2RL::Network::Result::Success) {
            RepliesSent.fetch_add(1, std::memory_order_relaxed);
        }
    }
}

void __cdecl OnHostMessage(const D2RL::PluginContext*, D2RL::Network::ChannelHandle, D2RL::Network::PeerHandle peer,
                           std::uint16_t messageId, const void* payload, std::uint32_t size, void*) noexcept {
    if (messageId != MessageRequest || payload == nullptr || size != sizeof(WireRequest) || Threads == nullptr) return;
    WireRequest request{};
    std::memcpy(&request, payload, sizeof(request));
    {
        std::lock_guard lock(HostMutex);
        if (HostQueue.size() >= 64) return;
        HostQueue.push_back({ peer, request.unitId, request.viewerId });
    }
    // One drain per message: a drain the loader drops never blocks the next.
    Threads->runOnGameThread(Context, &HostDrain, nullptr);
}

void __cdecl OnClientMessage(const D2RL::PluginContext*, D2RL::Network::ChannelHandle, std::uint16_t messageId,
                             const void* payload, std::uint32_t size, void*) noexcept {
    if (messageId != MessageReply || payload == nullptr || size != sizeof(WireReply)) return;
    WireReply reply{};
    std::memcpy(&reply, payload, sizeof(reply));
    MonsterData data{};
    data.unitId    = reply.unitId;
    data.life      = reply.life;
    data.maxLife   = reply.maxLife;
    data.level     = reply.level;
    data.flags     = reply.flags;
    data.immune    = reply.immune;
    data.auraCount = std::min<std::uint8_t>(reply.auraCount, 4);
    for (std::size_t i = 0; i < 6; ++i) data.resist[i] = reply.resist[i];
    for (std::size_t i = 0; i < 4; ++i) {
        data.auraSkill[i] = reply.auraSkill[i];
        data.auraLevel[i] = reply.auraLevel[i];
    }
    StoreData(data);
    RepliesReceived.fetch_add(1, std::memory_order_relaxed);
}

void __cdecl OnConnection(const D2RL::PluginContext*, const D2RL::Network::ConnectionEvent* event, void*) noexcept {
    if (event == nullptr) return;
    Connected.store(event->state == D2RL::Network::ConnectionState::Connected);
}

void RegisterChannel() {
    if (Context->QueryService(&Network) != D2RL::ServiceQueryResult::Success
            || !D2RL::HasNetworkServiceField(Network, D2RL::NetworkServiceRequiredSize)) {
        Network = nullptr;
        Context->LogWarn("MonsterDisplay: the network service is unavailable; joined TCP/IP clients show the "
                         "percentage only.");
        return;
    }
    D2RL::Network::ChannelRegistration registration{};
    registration.structSize         = D2RL::Network::ChannelRegistrationSize;
    registration.localChannelId     = ChannelId;
    registration.compatibilityToken = CompatibilityToken;
    registration.hostMessage        = &OnHostMessage;
    registration.clientMessage      = &OnClientMessage;
    registration.connectionState    = &OnConnection;
    if (Network->registerChannel(Context, &registration, &Channel) != D2RL::Network::Result::Success) {
        Channel = D2RL::Network::InvalidChannelHandle;
        Network = nullptr;
        Context->LogWarn("MonsterDisplay: the network channel was refused; joined TCP/IP clients show the percentage "
                         "only.");
    }
}

// ---------------------------------------------------------------------------
// UI thread: requests
// ---------------------------------------------------------------------------

auto LocalPlayerId() -> std::uint32_t {
    void* player = LocalPlayer(LocalIndex());
    return player != nullptr ? UnitId(player, SourceFile, __LINE__) : 0xFFFFFFFFU;
}

// ---------------------------------------------------------------------------
// Bound Demon Scaling's "demon-bound" event
// ---------------------------------------------------------------------------
// The layout its publisher (celestialrayone.bound-demon-scaling 1.0.3 and
// later) sends.
struct DemonBoundEvent {
    std::uint32_t eventSize;
    std::uint32_t unitId;   // the demon's server unit id
    std::uint32_t newBind;  // 1 a new bind, 0 a restore
    std::uint32_t reserved;
};
static_assert(sizeof(DemonBoundEvent) == 16);

constexpr char BoundDemonPublisherId[] = "celestialrayone.bound-demon-scaling";
constexpr char DemonBoundEventName[]   = "demon-bound";

const D2RL::PluginCommunicationService*       BoundEvents{};
D2RL::PluginCommunication::SubscriptionHandle BoundEventHandle{ D2RL::PluginCommunication::InvalidSubscriptionHandle };
std::atomic<std::uint64_t>                    BoundRefreshes{};

// Game thread, right after the demon was bound (the publisher calls it from
// its bind relay): when the panel holds data for it, read it again now
// instead of RefreshMs later.
void __cdecl OnDemonBound(const D2RL::PluginContext*, const D2RL::PluginCommunication::EventView* event, void*) noexcept {
    if (event == nullptr || event->data == nullptr || event->dataSize < sizeof(DemonBoundEvent) || !Active.load()) return;
    DemonBoundEvent bound{};
    std::memcpy(&bound, event->data, sizeof(bound));
    MonsterData   cached{};
    std::uint64_t age = 0;
    if (!LoadData(bound.unitId, cached, age)) return;
    MonsterData data{};
    if (!ComputeData(bound.unitId, LocalPlayerId(), 0, data)) return;
    StoreData(data);
    BoundRefreshes.fetch_add(1, std::memory_order_relaxed);
    Debug("MonsterDisplay: bound demon %u read again right after the bind, life %lld/%lld", data.unitId,
        static_cast<long long>(data.life), static_cast<long long>(data.maxLife));
}

void RegisterBoundDemonEvent() {
    constexpr auto SubscribeEnd = static_cast<std::uint32_t>(
        offsetof(D2RL::PluginCommunicationService, subscribeEvent) + sizeof(D2RL::PluginCommunication::SubscribeEventFn));
    const D2RL::PluginCommunicationService* communication = nullptr;
    if (Context->QueryService(&communication) != D2RL::ServiceQueryResult::Success
            || !D2RL::HasPluginCommunicationServiceField(communication, SubscribeEnd)
            || communication->subscribeEvent == nullptr) {
        Context->LogWarn("MonsterDisplay: plugin events are unavailable; a newly bound demon's panel catches up on "
                         "its normal 120 ms refresh.");
        return;
    }
    const D2RL::PluginCommunication::EventSubscription subscription{
        .structSize        = D2RL::PluginCommunication::EventSubscriptionSize,
        .flags             = 0U,
        .publisherPluginId = BoundDemonPublisherId,
        .name              = DemonBoundEventName,
        .minimumVersion    = 1U,
        .minimumDataSize   = static_cast<std::uint32_t>(sizeof(DemonBoundEvent)),
        .callback          = &OnDemonBound,
        .userData          = nullptr,
    };
    const auto result = communication->subscribeEvent(Context, &subscription, &BoundEventHandle);
    if (result == D2RL::PluginCommunication::Result::Success) {
        BoundEvents = communication;
        return;
    }
    BoundEventHandle = D2RL::PluginCommunication::InvalidSubscriptionHandle;
    D2RL::LogWarnF(Context, "MonsterDisplay: subscribing to Bound Demon Scaling's demon-bound event failed (result %u); "
        "a newly bound demon's panel catches up on its normal 120 ms refresh.", static_cast<unsigned>(result));
}

void RequestNetwork(std::uint32_t unitId, std::uint32_t viewerId, std::uint64_t now) {
    if (Network == nullptr || Channel == D2RL::Network::InvalidChannelHandle) return;
    if (!Connected.load()) {
        if (now - LastConnectTick >= ConnectRetryMs) {
            LastConnectTick = now;
            Network->connectToHost(Context, Channel);
        }
        return;
    }
    const WireRequest request{ unitId, viewerId };
    if (Network->sendToHost(Context, Channel, MessageRequest, &request, sizeof(request))
            == D2RL::Network::Result::Success) {
        NetworkRequests.fetch_add(1, std::memory_order_relaxed);
    }
}

void RequestData(std::uint32_t unitId, void* clientUnit) {
    const std::uint64_t now = Now();
    MonsterData   cached{};
    std::uint64_t age = 0;
    if (LoadData(unitId, cached, age) && age < RefreshMs) return;
    if (LastRequestUnit == unitId && now - LastRequestTick < RefreshMs) return;
    LastRequestUnit = unitId;
    LastRequestTick = now;
    const std::uint32_t viewerId = LocalPlayerId();

    if (Mode.load() != DataMode::Network && Threads != nullptr) {
        {
            std::lock_guard lock(RequestMutex);
            Pending = { unitId, viewerId, reinterpret_cast<std::uintptr_t>(clientUnit), true };
        }
        // One callback per request. The loader drops queued game callbacks
        // on a session change; a dropped one must never block the next, so
        // nothing here waits for a callback to come back.
        const auto result = Threads->runOnGameThread(Context, &LocalCompute, nullptr);
        LastQueueResult.store(static_cast<std::uint32_t>(result));
        if (result == D2RL::Threads::Result::Success) {
            Mode.store(DataMode::Local);
            LocalRequests.fetch_add(1, std::memory_order_relaxed);
            if (FirstQueueTick == 0) FirstQueueTick = now;
            if (!WarnedNoCallbacks && CallbacksRun.load() == 0 && now - FirstQueueTick > 3000) {
                WarnedNoCallbacks = true;
                Context->LogWarn("MonsterDisplay: game-thread reads are queued but none has run in 3 seconds.");
            }
            return;
        }
        if (result != D2RL::Threads::Result::Unavailable) {
            if (!WarnedQueue) {
                WarnedQueue = true;
                D2RL::LogWarnF(Context, "MonsterDisplay: queueing the game-thread read failed with code %u.",
                    static_cast<unsigned>(result));
            }
            return;
        }
        Mode.store(DataMode::Network);
    }
    RequestNetwork(unitId, viewerId, now);
}

// ---------------------------------------------------------------------------
// UI thread: text helpers
// ---------------------------------------------------------------------------

std::unordered_map<std::string, std::string> LangCache;

auto Localized(const std::string& key) -> std::string {
    const auto found = LangCache.find(key);
    if (found != LangCache.end()) return found->second;
    std::string value;
    if (Lang != nullptr) {
        const std::string full = "d2r:" + key;
        char          buffer[512]{};
        std::uint32_t required = 0;
        auto result = Lang->getStringByKey(Context, full.c_str(), buffer, sizeof(buffer), &required);
        if (result == D2RL::Localization::Result::Success) {
            value = buffer;
        } else if (result == D2RL::Localization::Result::BufferTooSmall && required > 0 && required < 65536) {
            std::string big(required, '\0');
            if (Lang->getStringByKey(Context, full.c_str(), big.data(), required, &required)
                    == D2RL::Localization::Result::Success) {
                value = big.c_str();
            }
        }
    }
    LangCache.emplace(key, value);
    return value;
}

// Colours through the game's own function (0CD6E0), the one that colours the
// stock monster and immunity texts: it prepends the game's "ColorCode" string
// and '0' + colour to a 1024-byte buffer. A prefix built from the localization
// service's copy of that string was drawn literally in game ("\xC3\xBF" "c1-1").
auto Colorize(const std::string& text, std::int32_t color) -> std::string {
    if (text.empty()) return {};
    char buffer[1024];
    const std::size_t length = std::min(text.size(), sizeof(buffer) - 16);
    std::memcpy(buffer, text.data(), length);
    buffer[length] = '\0';
    NativeColorize(buffer, static_cast<char>(std::clamp(color, 0, 38)));
    return std::string(buffer);
}

std::unordered_map<std::uint32_t, std::string> LangIdCache;

auto LocalizedById(std::uint32_t stringId) -> std::string {
    const auto found = LangIdCache.find(stringId);
    if (found != LangIdCache.end()) return found->second;
    std::string value;
    if (Lang != nullptr && Lang->getStringById != nullptr) {
        char          buffer[512];
        std::uint32_t required = 0;
        const auto    result   = Lang->getStringById(Context, stringId, buffer, sizeof(buffer), &required);
        if (result == D2RL::Localization::Result::Success) {
            value = buffer;
        } else if (result == D2RL::Localization::Result::BufferTooSmall && required > 0 && required < 65536) {
            std::string big(required, '\0');
            if (Lang->getStringById(Context, stringId, big.data(), required, &required)
                    == D2RL::Localization::Result::Success) {
                value = big.c_str();
            }
        }
    }
    LangIdCache.emplace(stringId, value);
    return value;
}

// The name the game gives the skill: its skilldesc.txt "str name". Skills
// without a skilldesc row (several of ESR's "Old" rows) fall back to the
// "skillname<id>" key.
auto SkillName(std::uint8_t dataContext, std::uint16_t skillId) -> std::string {
    std::string         name;
    const std::uint8_t* skill = SkillsRecord(dataContext, skillId);
    if (skill != nullptr && Read<std::int16_t>(skill, SkillDescRowOffset) >= 0) {
        const std::uint8_t* desc = SkillDescRecord(dataContext, skillId);
        if (desc != nullptr) {
            const std::uint16_t stringId = Read<std::uint16_t>(desc, SkillDescStrNameOffset);
            if (stringId != MissingStringId) name = LocalizedById(stringId);
        }
    }
    if (name.empty()) name = Localized("skillname" + std::to_string(skillId));
    if (name.empty()) name = Localized("Skillname" + std::to_string(skillId));
    if (name.empty()) name = "#" + std::to_string(skillId);
    return name;
}

// The game's own affix-name builder (the stock affix line without its type
// prefix). Its output string is {pointer, length, capacity}; bit 63 of the
// capacity marks inline storage, otherwise the block belongs to the game's
// allocator (0CF6B1 / 1DF7DE free it the same way).
auto AffixNames(std::uint8_t dataContext, const std::uint8_t* umods, std::int32_t color) -> std::string {
    if (umods == nullptr || umods[0] == 0) return {};
    alignas(16) std::uint64_t output[64]{};
    static const char empty[] = "";
    auto* result = static_cast<std::uint64_t*>(UModNames(output, dataContext, umods, 9, color, empty));
    if (result == nullptr) result = output;
    const char*       text   = reinterpret_cast<const char*>(result[0]);
    const std::size_t length = static_cast<std::size_t>(result[1]);
    std::string       value  = text != nullptr && length < 4096 ? std::string(text, length) : std::string();
    if (text != nullptr && static_cast<std::int64_t>(result[2]) >= 0) {
        void* allocator = GetAllocator();
        if (allocator != nullptr) {
            auto** table = *reinterpret_cast<void***>(allocator);
            reinterpret_cast<FreeFn>(table[4])(allocator, text);
        }
    }
    return value;
}

// The stock type line can end in a space ("Undead " when the monster has no
// description), which a centred row counts as text and shifts left.
auto TrimSpaces(std::string text) -> std::string {
    const std::size_t first = text.find_first_not_of(' ');
    if (first == std::string::npos) return {};
    const std::size_t last = text.find_last_not_of(' ');
    return text.substr(first, last - first + 1);
}

auto FormatNumber(std::int64_t value) -> std::string {
    char text[32];
    std::snprintf(text, sizeof(text), "%lld", static_cast<long long>(value));
    return text;
}

// ---------------------------------------------------------------------------
// UI thread: widgets
// ---------------------------------------------------------------------------

struct Rect {
    std::int32_t x, y, w, h;
};

auto GetRect(void* widget) -> Rect {
    Rect rect{};
    std::memcpy(&rect, Bytes(widget, WidgetRect), sizeof(rect));
    return rect;
}

void SetRect(void* widget, const Rect& rect) {
    std::memcpy(Bytes(widget, WidgetRect), &rect, sizeof(rect));
}

auto FitsParent(void* widget) -> bool {
    return Read<std::uint8_t>(widget, WidgetFitToParent) != 0;
}

void SetVisible(void* widget, bool visible) {
    auto** table = Read<void**>(widget, 0);
    reinterpret_cast<SetVisibleFn>(table[SetVisibleSlot])(widget, visible ? 1U : 0U);
}

// Reads the type descriptor names only; never runs a type getter.
auto IsType(void* widget, const char* typeName) -> bool {
    auto** table = Read<void**>(widget, 0);
    void*  desc  = reinterpret_cast<TypeDescFn>(table[TypeDescSlot])(widget);
    const std::size_t length = std::strlen(typeName) + 1;
    for (int depth = 0; desc != nullptr && depth < 32; ++depth) {
        const auto* name = Read<const char*>(desc, TypeDescNameOffset);
        if (name != nullptr && std::strncmp(name, typeName, length) == 0) return true;
        desc = Read<void*>(desc, TypeDescParentOffset);
    }
    return false;
}

auto FindText(void* container, const char* name) -> void* {
    void* widget = FindChild(container, name);
    return widget != nullptr && IsType(widget, "TextBoxWidget") ? widget : nullptr;
}

void SetText(void* box, const std::string& text) {
    const StringView view{ text.c_str(), text.size() };
    NativeSetText(box, &view);
}

auto MeasureWidth(void* box, const std::string& text) -> std::int32_t {
    if (text.empty()) return 0;
    const std::int32_t pixels = MeasureText(text.c_str(), Bytes(box, TextStyleOffset), UiScale());
    float scale = WidgetScale(box);
    if (!(scale > 0.0001F)) scale = 1.0F;
    return static_cast<std::int32_t>(static_cast<float>(pixels) / scale);
}

// Layout rectangles of the rows, remembered before the plugin first moves them.
struct Remembered {
    void* widget{};
    Rect  rect{};
};
std::array<Remembered, 64> Layout{};
std::size_t                LayoutNext{};

// Rows only ever move vertically, so an entry whose x, width or height no
// longer match belongs to a widget rebuilt at the same address (layout reload,
// keyboard <-> controller) and is remembered again.
auto OriginalRect(void* widget) -> Rect {
    const Rect current = GetRect(widget);
    for (auto& entry : Layout) {
        if (entry.widget != widget) continue;
        if (entry.rect.x != current.x || entry.rect.w != current.w || entry.rect.h != current.h) entry.rect = current;
        return entry.rect;
    }
    Remembered& slot = Layout[LayoutNext++ % Layout.size()];
    slot             = { widget, current };
    return slot.rect;
}

// The bar's layout y and height, remembered before the plugin first changes
// them (the stock update only ever sets x and width). A widget rebuilt at the
// same address shows values that are neither the layout's nor the plugin's
// last ones, and is remembered again.
struct BarLayout {
    void*        widget{};
    std::int32_t y{};
    std::int32_t h{};
    std::int32_t lastY{};
    std::int32_t lastH{};
};
std::array<BarLayout, 8> Bars{};
std::size_t              BarNext{};

auto RememberBar(void* widget, const Rect& current) -> BarLayout& {
    for (auto& entry : Bars) {
        if (entry.widget != widget) continue;
        const bool layout = current.y == entry.y && current.h == entry.h;
        const bool ours   = current.y == entry.lastY && current.h == entry.lastH;
        if (!layout && !ours) entry = { widget, current.y, current.h, current.y, current.h };
        return entry;
    }
    BarLayout& slot = Bars[BarNext++ % Bars.size()];
    slot            = { widget, current.y, current.h, current.y, current.h };
    return slot;
}

// A nameplate frame's own scale before the plugin first changed it (normally
// 1), remembered like a bar's layout: a rebuilt widget shows neither value
// and is remembered again.
struct FrameScale {
    void* widget{};
    float original{ 1.0F };
    float last{ 1.0F };
};
std::array<FrameScale, 8> FrameScales{};
std::size_t               FrameScaleNext{};

auto RememberScale(void* frame) -> FrameScale& {
    const float current = Read<float>(frame, WidgetOwnScale);
    for (auto& entry : FrameScales) {
        if (entry.widget != frame) continue;
        if (current != entry.original && current != entry.last) entry = { frame, current, current };
        return entry;
    }
    FrameScale& slot = FrameScales[FrameScaleNext++ % FrameScales.size()];
    slot             = { frame, current, current };
    return slot;
}

// A row's line images: y as in the layout relative to the row (the plugin
// moves y every frame, and x/width for growable lines), remembered once.
struct LineLayout {
    void*        widget{};
    std::int32_t y{};
    std::int32_t h{};
    std::int32_t lastY{};
};
std::array<LineLayout, 16> Lines{};
std::size_t                LineNext{};

auto RememberLine(void* widget, const Rect& current) -> LineLayout& {
    for (auto& entry : Lines) {
        if (entry.widget != widget) continue;
        if (current.h != entry.h || (current.y != entry.y && current.y != entry.lastY)) {
            entry = { widget, current.y, current.h, current.y };
        }
        return entry;
    }
    LineLayout& slot = Lines[LineNext++ % Lines.size()];
    slot             = { widget, current.y, current.h, current.y };
    return slot;
}

// The nameplate's Name and NameBold: their layout y, remembered like a line's
// (the plugin moves them every update).
std::array<LineLayout, 4> NameTexts{};
std::size_t               NameTextNext{};

// Growable line geometry, matching the shipped resist_bar_* art: each end cap
// is 48 wide (a faded stub, the end diamond 20 in from the end, then the ramp
// to full strength) and drawn outside the widget's rect (placeCapsOutsideRect).
// The whole bar reaches LinePadding past the text on each side, which puts the
// end diamonds 26 past it, and is never shorter than LineMinimumWidth so the
// centre diamond keeps room.
constexpr std::int32_t LineCapWidth     = 48;
constexpr std::int32_t LinePadding      = 46;
constexpr std::int32_t LineMinimumWidth = 180;

constexpr std::array<const char*, 5> RowNames{ "LevelType", "Affixes", "Aura", "Resistances", "Immunities" };

struct PanelWidgets {
    void* health{};
    void* uniqueFrame{};
    void* normalFrame{};
    void* bossFrame{};
    void* hpValue{};
    void* hpPercent{};
    std::array<void*, 5> rows{};
    std::array<std::array<void*, 2>, 5> lines{};

    [[nodiscard]] auto ours() const -> bool {
        return hpValue != nullptr || hpPercent != nullptr
            || std::any_of(rows.begin(), rows.end(), [](void* row) { return row != nullptr; });
    }
};

auto Collect(void* container) -> PanelWidgets {
    PanelWidgets widgets{};
    widgets.hpValue   = FindText(container, "HPValue");
    widgets.hpPercent = FindText(container, "HPPercent");
    for (std::size_t i = 0; i < RowNames.size(); ++i) {
        widgets.rows[i]     = FindText(container, RowNames[i]);
        widgets.lines[i][0] = FindChild(container, (std::string(RowNames[i]) + "TopLine").c_str());
        widgets.lines[i][1] = FindChild(container, (std::string(RowNames[i]) + "BottomLine").c_str());
    }
    if (!widgets.ours()) return widgets;
    widgets.health      = FindChild(container, "Health");
    widgets.uniqueFrame = FindChild(container, "UniqueMonsterBackground");
    widgets.normalFrame = FindChild(container, "NormalMonsterBackground");
    widgets.bossFrame   = FindChild(container, "BossMonsterBackground");
    return widgets;
}

void HideOurs(const PanelWidgets& widgets) {
    static const std::string empty;
    if (widgets.bossFrame != nullptr) SetVisible(widgets.bossFrame, false);
    for (void* box : { widgets.hpValue, widgets.hpPercent }) {
        if (box != nullptr) {
            SetText(box, empty);
            SetVisible(box, false);
        }
    }
    for (void* row : widgets.rows) {
        if (row != nullptr) {
            SetText(row, empty);
            SetVisible(row, false);
        }
    }
    for (const auto& pair : widgets.lines) {
        for (void* line : pair) {
            if (line != nullptr) SetVisible(line, false);
        }
    }
}

auto HiddenOnMinions(std::uint8_t umod) -> bool {
    const auto& hidden = Config.minionHiddenAffixes;
    return std::find(hidden.begin(), hidden.end(), static_cast<std::int32_t>(umod)) != hidden.end();
}

auto ResistRow(const MonsterData& data) -> std::string {
    std::string  text;
    std::int32_t literalColor = Config.separatorColor;
    for (const TemplatePart& part : Config.parts) {
        switch (part.kind) {
        case TemplatePart::Kind::Literal:
            text += Colorize(part.text, literalColor);
            break;
        case TemplatePart::Kind::Color:
            literalColor = part.value;
            break;
        case TemplatePart::Kind::Value:
            text += Colorize(FormatNumber(data.resist[static_cast<std::size_t>(part.value)]),
                Config.elementColors[static_cast<std::size_t>(part.value)]);
            break;
        }
    }
    return text;
}

// The immunity row from the server mask, written the way 0CEA50 writes the
// stock line: Physical, Magic, Fire, Lightning, Cold, Poison, each in its own
// colour, joined by the stock separator. Empty text when a string is missing,
// so the caller can fall back.
struct ImmunityText {
    const char*  scary;
    const char*  icon;
    std::int32_t color;
    Element      element;
};
constexpr std::array<ImmunityText, 6> ImmunityOrder{ {
    { "strScaryMonDesc5", "PhysicalImmunityIcon",  4, Element::Physical },
    { "strScaryMonDesc6", "MagicImmunityIcon",     8, Element::Magic },
    { "strScaryMonDesc1", "FireImmunityIcon",      1, Element::Fire },
    { "strScaryMonDesc3", "LightningImmunityIcon", 9, Element::Lightning },
    { "strScaryMonDesc2", "ColdImmunityIcon",      3, Element::Cold },
    { "strScaryMonDesc4", "PoisonImmunityIcon",    2, Element::Poison },
} };

auto ImmunityRow(std::uint8_t mask, bool& complete) -> std::string {
    complete = true;
    std::string row;
    if (mask == 0) return row;
    if (Hover.classId - ImmuneClassFirst >= 0 && Hover.classId - ImmuneClassFirst <= ImmuneClassSpan) return row;
    const bool icons = IconKeys() != 0;
    std::string separator;
    if (SeparatorStyle() != 0) {
        separator = Colorize(Localized("TargetHealthBarTextSeparator"), 5);
    } else {
        separator = Localized("KeyComma") + Localized("space");
    }
    for (const ImmunityText& text : ImmunityOrder) {
        if ((mask & (1U << static_cast<std::uint32_t>(text.element))) == 0) continue;
        const std::string name = Localized(icons ? text.icon : text.scary);
        if (name.empty()) {
            complete = false;
            return {};
        }
        if (!row.empty()) row += separator;
        row += Colorize(name, text.color);
    }
    return row;
}

void Apply(void* container, const std::uint8_t* payload) {
    const PanelWidgets widgets = Collect(container);
    if (!widgets.ours()) return;

    const std::uint64_t now      = Now();
    const bool          hovering = Hover.valid && now - Hover.tick < HoverTimeoutMs;
    MonsterData         data{};
    std::uint64_t       age  = 0;
    const bool          have = hovering && LoadData(Hover.unitId, data, age);

    // The bar. The stock update sized it to the width of the name and placed
    // it around an x it stored at layout load (panel +168h / +16Ch), which is
    // not the centre of these layouts; the plugin centres it in its container
    // and gives it the configured width. HealthBarWidget builds its fill
    // rectangle when its value is set (2AC890 -> 85A190 writes +ACh from the
    // rect at that moment), so the value is set again after the resize,
    // exactly as the stock update does after its own sizing.
    // Prime evils also get a taller bar, grown evenly about the layout's
    // centre line, but only when the layout has the boss frame whose opening
    // is built for that height.
    Rect         bar{};
    bool         barKnown    = false;
    std::int32_t extraHeight = 0;
    const bool   boss        = hovering && Hover.boss;
    if (widgets.health != nullptr) {
        bar      = GetRect(widgets.health);
        barKnown = true;
        BarLayout&         layout = RememberBar(widgets.health, bar);
        const std::int32_t middle = layout.y + layout.h / 2;
        if (!FitsParent(widgets.health)) {
            if (Config.barWidth > 0) {
                double width = Config.barWidth;
                if (boss) width *= Config.bossMultiplier;
                const std::int32_t newWidth = static_cast<std::int32_t>(std::lround(width));
                Rect box{};
                NativeGetRect(container, &box);
                const std::int32_t centre = box.w > 0 ? box.w / 2 : bar.x + bar.w / 2;
                bar.x                     = centre - newWidth / 2;
                bar.w                     = newWidth;
            }
            std::int32_t height = layout.h;
            if (boss && widgets.bossFrame != nullptr) {
                height = static_cast<std::int32_t>(std::lround(layout.h * Config.bossHeightMultiplier));
            }
            extraHeight  = height - layout.h;
            bar.y        = middle - height / 2;
            bar.h        = height;
            layout.lastY = bar.y;
            layout.lastH = bar.h;
            SetRect(widgets.health, bar);
            SetHealthValue(widgets.health, std::clamp(Read<std::int32_t>(payload, PayloadHealth), 0, 100));
        }
        for (void* frame : { widgets.uniqueFrame, widgets.normalFrame }) {
            if (frame != nullptr && !FitsParent(frame)) {
                SetRect(frame, Rect{ bar.x, middle - Config.frameHeight / 2, bar.w, Config.frameHeight });
            }
        }
        if (widgets.bossFrame != nullptr) {
            const std::int32_t bossHeight = Config.frameHeight + extraHeight;
            if (!FitsParent(widgets.bossFrame)) {
                SetRect(widgets.bossFrame, Rect{ bar.x, middle - bossHeight / 2, bar.w, bossHeight });
            }
            SetVisible(widgets.bossFrame, boss);
            if (boss) {
                if (widgets.uniqueFrame != nullptr) SetVisible(widgets.uniqueFrame, false);
                if (widgets.normalFrame != nullptr) SetVisible(widgets.normalFrame, false);
            }
        }
    }

    // Life and percentage.
    std::int32_t percent = std::clamp(Read<std::int32_t>(payload, PayloadHealth), 0, 100);
    std::string  lifeText;
    if (have && data.maxLife > 0) {
        const std::int64_t life = std::clamp<std::int64_t>(data.life, 0, data.maxLife);
        lifeText = FormatNumber(life) + " / " + FormatNumber(data.maxLife);
        percent  = life <= 0 ? 0 : static_cast<std::int32_t>(std::max<std::int64_t>(1, (life * 100 + data.maxLife - 1) / data.maxLife));
    }
    std::string percentText = "(" + FormatNumber(percent) + "%)";
    if (!Config.showHp) lifeText.clear();
    if (!Config.showHpPercent) percentText.clear();
    if (widgets.hpValue != nullptr && !lifeText.empty() && !percentText.empty()) percentText.insert(0, " ");

    const std::int32_t lifeWidth    = widgets.hpValue != nullptr ? MeasureWidth(widgets.hpValue, lifeText) : 0;
    const std::int32_t percentWidth = widgets.hpPercent != nullptr ? MeasureWidth(widgets.hpPercent, percentText) : 0;
    const std::int32_t centre       = barKnown ? bar.x + bar.w / 2 : 0;
    const std::int32_t start        = centre - (lifeWidth + percentWidth) / 2;
    if (widgets.hpValue != nullptr) {
        SetText(widgets.hpValue, lifeText);
        if (barKnown && !FitsParent(widgets.hpValue)) {
            Rect rect = GetRect(widgets.hpValue);
            rect.x    = start;
            SetRect(widgets.hpValue, rect);
        }
        SetVisible(widgets.hpValue, !lifeText.empty());
    }
    if (widgets.hpPercent != nullptr) {
        SetText(widgets.hpPercent, percentText);
        if (barKnown && !FitsParent(widgets.hpPercent)) {
            Rect rect = GetRect(widgets.hpPercent);
            rect.x    = start + lifeWidth;
            SetRect(widgets.hpPercent, rect);
        }
        SetVisible(widgets.hpPercent, !percentText.empty());
    }

    // Rows.
    std::array<std::string, 5> rows{};
    const char* stockLine  = Read<const char*>(payload, PayloadLine);
    const char* immunities = Read<const char*>(payload, PayloadImmunities);

    // The affixes shown: the monster's own list, minus minion_hidden_affixes
    // on a minion.
    std::array<std::uint8_t, 10> affixes{};
    std::size_t affixCount = 0;
    bool        auraShown  = false;
    if (hovering) {
        for (std::size_t i = 0; i < Hover.umodCount; ++i) {
            const std::uint8_t umod = Hover.umods[i];
            if (Hover.minion && HiddenOnMinions(umod)) continue;
            affixes[affixCount++] = umod;
            if (umod == AuraEnchanted) auraShown = true;
        }
    }
    const bool hasAffixes = affixCount > 0;

    std::string& levelType = rows[0];
    if (Config.showLevel && have && data.level > 0) levelType = Config.levelLabel + " " + FormatNumber(data.level);
    std::string typeText;
    if (hasAffixes) {
        if (Hover.undead) typeText = Localized("UndeadDescriptX");
        else if (Hover.demon) typeText = Localized("DemonID");
    } else if (stockLine != nullptr) {
        typeText = TrimSpaces(stockLine);
    }
    if (!typeText.empty()) {
        if (!levelType.empty()) levelType += " ";
        levelType += typeText;
    }

    if (hasAffixes) {
        std::array<std::uint8_t, 10> list{};
        std::size_t count = 0;
        for (std::size_t i = 0; i < affixCount; ++i) {
            if (Config.separateAura && affixes[i] == AuraEnchanted) continue;
            list[count++] = affixes[i];
        }
        if (Config.showAffixes && count > 0) rows[1] = AffixNames(Hover.dataContext, list.data(), Config.affixColor);
        if (Config.separateAura && auraShown) {
            const std::array<std::uint8_t, 2> aura{ AuraEnchanted, 0 };
            std::string line = AffixNames(Hover.dataContext, aura.data(), Config.affixColor);
            if (have && data.auraCount > 0) {
                std::string detail;
                for (std::uint8_t i = 0; i < data.auraCount && i < 4; ++i) {
                    if (!detail.empty()) detail += " / ";
                    detail += SkillName(Hover.dataContext, data.auraSkill[i]) + " " + Config.auraLevelLabel + " " + FormatNumber(data.auraLevel[i]);
                }
                line += " " + Colorize("(" + detail + ")", Config.affixColor);
            }
            rows[2] = line;
        }
    }
    for (std::string& row : rows) row = TrimSpaces(row);
    if (Config.showResists && have && (data.flags & 1U) != 0) rows[3] = ResistRow(data);
    if (Config.showImmunities) {
        // From the server mask once it is here. Before that the stock line,
        // except for allied monsters: they may be capped, and the stock line
        // knows nothing about the cap.
        const bool fromServer = have && (data.flags & 1U) != 0;
        if (fromServer) {
            bool complete = true;
            rows[4] = ImmunityRow(data.immune, complete);
            // A string the localization service does not have: a capped
            // monster still shows nothing, any other the stock line.
            if (!complete) rows[4] = (data.flags & 2U) != 0 || immunities == nullptr ? std::string() : std::string(immunities);
        } else if (!(hovering && Hover.ally) && immunities != nullptr) {
            rows[4] = immunities;
        }
    }

    // Stack the rows from LevelType's layout position, skipping empty ones,
    // with row_gap between two shown rows. A row's line images keep their
    // layout offset from it and share its visibility.
    std::int32_t y      = 0;
    bool         haveY  = false;
    bool         placed = false;
    for (std::size_t i = 0; i < widgets.rows.size(); ++i) {
        void* row = widgets.rows[i];
        if (row == nullptr) continue;
        const Rect original = OriginalRect(row);
        if (!haveY) {
            y     = original.y + (extraHeight + 1) / 2;
            haveY = true;
        }
        SetText(row, rows[i]);
        const bool shown = !rows[i].empty();
        if (shown) {
            if (placed) y += Config.rowGap;
            if (!FitsParent(row)) {
                Rect rect = GetRect(row);
                rect.y    = y;
                SetRect(row, rect);
            }
        }
        std::int32_t textWidth = -1;
        for (void* line : widgets.lines[i]) {
            if (line == nullptr) continue;
            if (shown && !FitsParent(line)) {
                Rect        rect   = GetRect(line);
                LineLayout& layout = RememberLine(line, rect);
                rect.y             = layout.y + (y - original.y);
                if (IsType(line, "GrowableImageWidget")) {
                    if (textWidth < 0) textWidth = MeasureWidth(row, rows[i]);
                    const Rect         rowRect = GetRect(row);
                    const std::int32_t total   = std::max(LineMinimumWidth, textWidth + 2 * LinePadding);
                    rect.w                     = total - 2 * LineCapWidth;
                    rect.x                     = rowRect.x + rowRect.w / 2 - rect.w / 2;
                }
                layout.lastY = rect.y;
                SetRect(line, rect);
            }
            SetVisible(line, shown);
        }
        SetVisible(row, shown);
        if (shown) {
            y += original.h;
            placed = true;
        }
    }
}

// ---------------------------------------------------------------------------
// Hooks
// ---------------------------------------------------------------------------

void CaptureHover(void* unit) {
    Hover = {};
    if (unit == nullptr || UnitType(unit) != UnitTypeMonster) return;
    Hover.valid       = true;
    Hover.clientUnit  = unit;
    Hover.unitId      = UnitId(unit, SourceFile, __LINE__);
    Hover.classId     = ClassId(unit, SourceFile, __LINE__);
    Hover.dataContext = DataContext(unit);
    Hover.tick        = Now();
    const std::uint8_t* record = Hover.classId >= 0 ? MonStats(Hover.dataContext, Hover.classId) : nullptr;
    Hover.boss   = record != nullptr && (record[MonStatsFlags] & PrimeEvilMask) != 0;
    // Until the server data arrives, a bound demon counts as an ally too: the
    // client copy carries the bind_demon state, and Bound Demon Scaling's
    // pseudo row may cap it, which the stock immunity line knows nothing about.
    Hover.ally   = (record != nullptr && record[MonStatsAlign] == 1)
                || (BoundDemonHint.load(std::memory_order_relaxed) && CheckState(unit, BindDemonState) != 0);
    Hover.undead = IsUndead(unit) != 0;
    Hover.demon  = IsDemon(unit) != 0;
    Hover.minion = HasMonTypeFlag(unit, MonTypeMinion) && !HasMonTypeFlag(unit, MonTypeBosses);
    const std::uint8_t* umods = GetUMods(unit);
    if (umods != nullptr) {
        for (std::size_t i = 0; i < 9 && umods[i] != 0; ++i) {
            Hover.umods[Hover.umodCount++] = umods[i];
            if (umods[i] == AuraEnchanted) Hover.auraEnchanted = true;
        }
    }
    RequestData(Hover.unitId, unit);
}

std::uint64_t __fastcall HookedBuilder(void* unit, std::uint64_t position, std::uint8_t attached) {
    if (Active.load(std::memory_order_relaxed)) {
        BuilderCalls.fetch_add(1, std::memory_order_relaxed);
        CaptureHover(unit);
    }
    return OriginalBuilder(unit, position, attached);
}

auto RememberNameText(void* widget, const Rect& current) -> LineLayout& {
    for (auto& entry : NameTexts) {
        if (entry.widget != widget) continue;
        if (current.h != entry.h || (current.y != entry.y && current.y != entry.lastY)) {
            entry = { widget, current.y, current.h, current.y };
        }
        return entry;
    }
    LineLayout& slot = NameTexts[NameTextNext++ % NameTexts.size()];
    slot             = { widget, current.y, current.h, current.y };
    return slot;
}

// The controller nameplate (TargetAttached), right after the stock update:
// see section 1. The bar's height comes from the layout height RememberBar
// keeps, never from the plugin's own last value.
void ApplyNameplate(void* container, const std::uint8_t* payload) {
    if (FindText(container, "Name") == nullptr) return;
    if (void* bold = FindText(container, "NameBold"); bold != nullptr) {
        std::memcpy(Bytes(bold, TextColorOffset), payload + PayloadColor, 16);
        const char* name = Read<const char*>(payload, PayloadName);
        SetText(bold, name != nullptr ? std::string(name) : std::string());
        NameplateBoldUpdates.fetch_add(1, std::memory_order_relaxed);
    }
    if (Config.nameplateWidthPercent == 100 && Config.nameplateHeightPercent == 100) return;
    void* health = FindChild(container, "Health");
    if (health == nullptr || !IsType(health, "HealthBarWidget") || FitsParent(health)) return;
    Rect       bar    = GetRect(health);
    BarLayout& layout = RememberBar(health, bar);
    if (bar.w > 0) {
        const std::int32_t centre = bar.x + bar.w / 2;
        const std::int32_t width  = std::max<std::int32_t>(1, static_cast<std::int32_t>(
            std::lround(static_cast<double>(bar.w) * Config.nameplateWidthPercent / 100.0)));
        bar.x = centre - width / 2;
        bar.w = width;
    }
    if (layout.h > 0) {
        const std::int32_t height = std::max<std::int32_t>(1, static_cast<std::int32_t>(
            std::lround(static_cast<double>(layout.h) * Config.nameplateHeightPercent / 100.0)));
        bar.y = layout.y + layout.h - height;
        bar.h = height;
    }
    layout.lastY = bar.y;
    layout.lastH = bar.h;
    SetRect(health, bar);
    SetHealthValue(health, std::clamp(Read<std::int32_t>(payload, PayloadHealth), 0, 100));
    const std::int32_t lift = bar.y - layout.y;
    for (const char* name : { "Name", "NameBold" }) {
        void* box = FindText(container, name);
        if (box == nullptr || FitsParent(box)) continue;
        Rect        rect = GetRect(box);
        LineLayout& text = RememberNameText(box, rect);
        rect.y           = text.y + lift;
        text.lastY       = rect.y;
        SetRect(box, rect);
    }
    const float ratio = layout.h > 0 ? static_cast<float>(bar.h) / static_cast<float>(layout.h) : 1.0F;
    Rect frameRect = bar;
    if (ratio != 1.0F) {
        frameRect.w = static_cast<std::int32_t>(std::lround(static_cast<double>(bar.w) / ratio));
        frameRect.h = static_cast<std::int32_t>(std::lround(static_cast<double>(bar.h) / ratio));
    }
    for (const char* name : { "UniqueMonsterBackground", "NormalMonsterBackground" }) {
        void* frame = FindChild(container, name);
        if (frame == nullptr || FitsParent(frame)) continue;
        FrameScale& scale = RememberScale(frame);
        const float own   = scale.original * ratio;
        Write<float>(frame, WidgetOwnScale, own);
        scale.last = own;
        SetRect(frame, frameRect);
    }
    NameplateUpdates.fetch_add(1, std::memory_order_relaxed);
}

// The nameplate's Clear: the stock update clears Name; NameBold goes with it.
void ClearNameplate(void* container) {
    if (void* bold = FindText(container, "NameBold"); bold != nullptr) SetText(bold, std::string());
}

// TargetAttached, updated with attached = 1 (the pc layout passes the panel
// itself), and holding none of the plugin's own widgets.
auto IsNameplate(void* panel, void* container, std::uint8_t attached) -> bool {
    return attached != 0 && container != panel && FindChild(panel, "TargetAttached") == container;
}

std::uint64_t __fastcall HookedPanelUpdate(void* panel, void* message, void* container, std::uint8_t attached) {
    const std::uint64_t result = OriginalPanelUpdate(panel, message, container, attached);
    if (!Active.load(std::memory_order_relaxed) || message == nullptr || container == nullptr) return result;
    const std::uint64_t hash = Read<std::uint64_t>(message, MessageHashOffset);
    if (hash == ClearHash) {
        const PanelWidgets widgets = Collect(container);
        if (widgets.ours()) HideOurs(widgets);
        else if (IsNameplate(panel, container, attached)) ClearNameplate(container);
        return result;
    }
    if (hash != SetInfoHash) return result;
    const auto* payload = Read<const std::uint8_t*>(message, MessagePayloadOff);
    if (payload == nullptr) return result;
    PanelUpdates.fetch_add(1, std::memory_order_relaxed);
    if (IsNameplate(panel, container, attached) && !Collect(container).ours()) {
        ApplyNameplate(container, payload);
        return result;
    }
    Apply(container, payload);
    return result;
}

// ---------------------------------------------------------------------------
// Load checks
// ---------------------------------------------------------------------------

struct Witness {
    std::uint64_t       rva;
    const std::uint8_t* bytes;
    std::uint32_t       size;
    const char*         name;
};

template <std::size_t N>
constexpr auto W(std::uint64_t rva, const std::uint8_t (&bytes)[N], const char* name) noexcept -> Witness {
    return { rva, bytes, static_cast<std::uint32_t>(N), name };
}

const std::array<Witness, 40> Witnesses{
    W(BuilderRva, BuilderEntryBytes, "panel data builder"),
    W(PanelUpdateRva, PanelUpdateEntryBytes, "panel update"),
    W(PanelWindowRva, PanelWindowBytes, "panel bar sizing window"),
    W(TypeWindowRva, TypeWindowBytes, "monster type window"),
    W(PrimeNameRva, PrimeNameBytes, "monstats primeevil column name"),
    W(PrimeFieldRva, PrimeFieldBytes, "monstats primeevil column layout"),
    W(GetRectRva, GetRectBytes, "widget rectangle"),
    W(SetWidthRva, SetWidthBytes, "widget width"),
    W(SetXRva, SetXBytes, "widget x"),
    W(FindChildRva, FindChildBytes, "child widget lookup"),
    W(GetUModsRva, GetUModsBytes, "monster affix list"),
    W(MonTypeFlagRva, MonTypeFlagBytes, "monster type flag"),
    W(UModNamesRva, UModNamesBytes, "affix names"),
    W(AllocatorRva, AllocatorBytes, "game allocator"),
    W(UnitTypeRva, UnitTypeBytes, "unit type"),
    W(UnitIdRva, UnitIdBytes, "unit id"),
    W(ClassIdRva, ClassIdBytes, "unit class"),
    W(DataContextRva, DataContextBytes, "data context"),
    W(MonStatsRva, MonStatsBytes, "monstats record"),
    W(ReadStatRva, ReadStatBytes, "stat read"),
    W(LocalIndexRva, LocalIndexBytes, "local player index"),
    W(LocalPlayerRva, LocalPlayerBytes, "local player"),
    W(ServerGameRva, ServerGameBytes, "server game"),
    W(ServerUnitRva, ServerUnitBytes, "server unit lookup"),
    W(SunderRva, SunderBytes, "sunder step"),
    W(PierceRva, PierceBytes, "enemy resistance step"),
    W(CheckStateRva, CheckStateBytes, "state check"),
    W(SkillByIdRva, SkillByIdBytes, "skill lookup"),
    W(SkillsRecordRva, SkillsRecordBytes, "skills.txt record"),
    W(SkillDescRva, SkillDescBytes, "skilldesc.txt record"),
    W(SkillDescColumnsRva, SkillDescColumnsBytes, "skilldesc.txt str name column"),
    W(SetHealthValueRva, SetHealthValueBytes, "health bar value"),
    W(ColorizeRva, ColorizeBytes, "text colouring"),
    W(IconKeysRva, IconKeysBytes, "immunity string set"),
    W(SeparatorRva, SeparatorBytes, "immunity separator style"),
    W(ImmuneClassRva, ImmuneClassBytes, "immunity line class exclusion"),
    W(NameColorRva, NameColorBytes, "nameplate name colour"),
    W(ChildScaleRva, ChildScaleBytes, "child widget scale"),
    W(FrameArtScaleRva, FrameArtScaleBytes, "frame art scale"),
    W(CapAnchorRva, CapAnchorBytes, "frame cap anchors"),
};

constexpr std::uint8_t LoaderThunkBytes[]{ 0xFF, 0x25 };

auto VerifyBuild() -> bool {
    bool ok = true;
    for (const Witness& witness : Witnesses) {
        if (!Context->CheckExpectedBytes(witness.rva, witness.bytes, witness.size)) {
            D2RL::LogErrorF(Context, "MonsterDisplay: %s at RVA 0x%llX does not match D2RLoader 1.3.1.", witness.name,
                static_cast<unsigned long long>(witness.rva));
            ok = false;
        }
    }
    if (!Context->CheckExpectedBytes(SkillLevelRva, LoaderThunkBytes, sizeof(LoaderThunkBytes))) {
        D2RL::LogErrorF(Context, "MonsterDisplay: skill level at RVA 0x%llX does not match D2RLoader 1.3.1.",
            static_cast<unsigned long long>(SkillLevelRva));
        ok = false;
    }
    return ok;
}

void ResolveFunctions() {
    NativeSetText = At<SetTextFn>(SetTextRva);
    NativeColorize = At<ColorizeFn>(ColorizeRva);
    UiScale       = At<UiScaleFn>(UiScaleRva);
    MeasureText   = At<MeasureFn>(MeasureRva);
    WidgetScale   = At<WidgetScaleFn>(WidgetScaleRva);
    IsDemon       = At<UnitCheckFn>(IsDemonRva);
    IsUndead      = At<UnitCheckFn>(IsUndeadRva);
    FindChild     = At<FindChildFn>(FindChildRva);
    GetUMods      = At<GetUModsFn>(GetUModsRva);
    HasMonTypeFlag = At<MonTypeFlagFn>(MonTypeFlagRva);
    UModNames     = At<UModNamesFn>(UModNamesRva);
    GetAllocator  = At<AllocatorFn>(AllocatorRva);
    UnitType      = At<UnitTypeFn>(UnitTypeRva);
    UnitId        = At<UnitIdFn>(UnitIdRva);
    ClassId       = At<ClassIdFn>(ClassIdRva);
    DataContext   = At<DataContextFn>(DataContextRva);
    MonStats      = At<MonStatsFn>(MonStatsRva);
    ReadStat      = At<ReadStatFn>(ReadStatRva);
    LocalIndex    = At<LocalIndexFn>(LocalIndexRva);
    LocalPlayer   = At<LocalPlayerFn>(LocalPlayerRva);
    ServerGame    = At<ServerGameFn>(ServerGameRva);
    ServerUnit    = At<ServerUnitFn>(ServerUnitRva);
    SunderStep    = At<ResistStepFn>(SunderRva);
    PierceStep    = At<ResistStepFn>(PierceRva);
    CheckState    = At<CheckStateFn>(CheckStateRva);
    SkillById     = At<SkillByIdFn>(SkillByIdRva);
    SkillsRecord  = At<SkillRecordFn>(SkillsRecordRva);
    SkillDescRecord = At<SkillRecordFn>(SkillDescRva);
    SkillLevel    = At<SkillLevelFn>(SkillLevelRva);
    SetHealthValue = At<SetHealthValueFn>(SetHealthValueRva);
    NativeGetRect = At<GetRectFn>(GetRectRva);
    IconKeys      = At<UiFlagFn>(IconKeysRva);
    SeparatorStyle = At<UiFlagFn>(SeparatorRva);
}

void __cdecl OnGameChanged(const D2RL::PluginContext*, const D2RL::Lifecycle::GameplayEvent*, void*) noexcept {
    ResetSession();
    Connected.store(false);
    LastConnectTick = 0;
}

void RegisterLifecycle() {
    const D2RL::LifecycleService* lifecycle = nullptr;
    if (Context->QueryService(&lifecycle) != D2RL::ServiceQueryResult::Success
            || !D2RL::HasLifecycleServiceField(lifecycle, D2RL::LifecycleServiceRequiredSize)) {
        Context->LogWarn("MonsterDisplay: the lifecycle service is unavailable; cached data is kept across games.");
        return;
    }
    for (const auto kind : { D2RL::Lifecycle::GameplayEventKind::GameJoined, D2RL::Lifecycle::GameplayEventKind::GameLeft,
                             D2RL::Lifecycle::GameplayEventKind::LocalPlayerReady }) {
        D2RL::Lifecycle::GameplayEventListener listener{};
        listener.structSize = D2RL::Lifecycle::GameplayEventListenerSize;
        listener.kind       = kind;
        listener.callback   = &OnGameChanged;
        D2RL::Lifecycle::ListenerHandle handle = D2RL::Lifecycle::InvalidHandle;
        lifecycle->registerGameplayEventListener(Context, &listener, &handle);
    }
}

// ---------------------------------------------------------------------------
// Console
// ---------------------------------------------------------------------------

auto StateName() -> const char* {
    switch (State) {
    case PluginState::NotLoaded:        return "not loaded";
    case PluginState::DisabledByConfig: return "disabled by config";
    case PluginState::UnsupportedBuild: return "unsupported build, nothing installed";
    case PluginState::InstallFailed:    return "install failed, nothing active";
    case PluginState::Active:           return "active";
    }
    return "?";
}

auto ModeName() -> const char* {
    switch (Mode.load()) {
    case DataMode::Unknown: return "not decided yet";
    case DataMode::Local:   return "local server";
    case DataMode::Network: return Connected.load() ? "network, connected" : "network, not connected";
    }
    return "?";
}

auto __cdecl StatusCommand(D2R::Game::Client*, const D2RL::ConsoleCommandContext* command, void*) noexcept
        -> D2RL::ConsoleCommandResult {
    if (command == nullptr || command->plugin == nullptr) return D2RL::ConsoleCommandResult::Failed;
    const auto say = [&](const char* text) { command->plugin->WriteConsoleMessage(text); };
    std::string_view args = command->args != nullptr ? std::string_view(command->args, command->argsLength) : std::string_view{};
    args = Trim(args);
    if (args.substr(0, 5) == "debug") {
        const std::string_view value = Trim(args.substr(5));
        bool on = !DebugOn.load();
        if (value == "on") on = true;
        else if (value == "off") on = false;
        DebugOn.store(on);
        say(on ? "Monster Display debug log on." : "Monster Display debug log off.");
        return D2RL::ConsoleCommandResult::Handled;
    }
    char line[512];
    std::snprintf(line, sizeof(line), "Monster Display %s: %s%s%s. Data source: %s.", PluginVersion, StateName(),
        InactiveReason.empty() ? "" : " (", InactiveReason.empty() ? "" : (InactiveReason + ")").c_str(), ModeName());
    say(line);
    const ResistRules rules = CurrentRules();
    char decision[96];
    if (CapAcquired.load()) {
        std::snprintf(decision, sizeof(decision), "asked from Summon Resist Cap");
    } else if (CapLastResult.load() == 0xFFFFFFFFU) {
        std::snprintf(decision, sizeof(decision), "not asked yet");
    } else {
        std::snprintf(decision, sizeof(decision), "service unavailable (result %u)", CapLastResult.load());
    }
    std::snprintf(line, sizeof(line),
        "Resistances: %s; floor %d; Summon Resist Cap %s (base %d, ceiling %d, physical %d, floor %d), cap "
        "decision %s.",
        Config.effective ? "effective (your sunder and -enemy resistance applied)" : "monster's own",
        rules.uncappedFloor, rules.capLive ? "live" : "not live", rules.base, rules.ceiling, rules.physicalCap,
        rules.cappedFloor, decision);
    say(line);
    if (LifeBarsAcquired.load()) {
        std::snprintf(line, sizeof(line), "Life: total life of monsters with life bars from Monster Life Bars.");
    } else if (LifeBarsLastResult.load() == 0xFFFFFFFFU) {
        std::snprintf(line, sizeof(line), "Life: Monster Life Bars not asked yet.");
    } else {
        std::snprintf(line, sizeof(line), "Life: Monster Life Bars service unavailable (result %u); monsters with "
            "life bars show their current bar.", LifeBarsLastResult.load());
    }
    say(line);
    const char* hint = BoundDemonHint.load() ? "" : " Before its data arrives a bound demon shows the stock immunity "
                                                    "line (533D60 does not match).";
    if (StatsRowAcquired.load()) {
        std::snprintf(line, sizeof(line), "Bound demons: decided by Bound Demon Scaling's pseudo row (stats-row "
            "service), %llu decisions made as the pseudo row.%s",
            static_cast<unsigned long long>(PseudoRowDecisions.load()), hint);
    } else if (StatsRowLastResult.load() == 0xFFFFFFFFU) {
        std::snprintf(line, sizeof(line), "Bound demons: Bound Demon Scaling not asked yet.%s", hint);
    } else {
        std::snprintf(line, sizeof(line), "Bound demons: Bound Demon Scaling's stats-row service unavailable (result "
            "%u); a bound demon is decided by its own monstats row.%s", StatsRowLastResult.load(), hint);
    }
    say(line);
    if (Config.nameplateWidthPercent == 100 && Config.nameplateHeightPercent == 100) {
        std::snprintf(line, sizeof(line), "Nameplate: stock size.");
    } else {
        std::snprintf(line, sizeof(line), "Nameplate: bar and frames at %d%% of the name's width and %d%% of the layout "
            "height, %llu updates.", Config.nameplateWidthPercent, Config.nameplateHeightPercent,
            static_cast<unsigned long long>(NameplateUpdates.load()));
    }
    say(line);
    std::snprintf(line, sizeof(line), "Nameplate bold name (NameBold): %llu updates.",
        static_cast<unsigned long long>(NameplateBoldUpdates.load()));
    say(line);
    std::snprintf(line, sizeof(line),
        "Counters: builder %llu, panel updates %llu, game reads queued %llu (last result %u), run %llu, computed "
        "%llu, no server unit %llu, client copy %llu, network requests %llu, replies sent %llu, received %llu.",
        static_cast<unsigned long long>(BuilderCalls.load()), static_cast<unsigned long long>(PanelUpdates.load()),
        static_cast<unsigned long long>(LocalRequests.load()), static_cast<unsigned>(LastQueueResult.load()),
        static_cast<unsigned long long>(CallbacksRun.load()), static_cast<unsigned long long>(Computed.load()),
        static_cast<unsigned long long>(MissNoUnit.load()), static_cast<unsigned long long>(MissClientUnit.load()),
        static_cast<unsigned long long>(NetworkRequests.load()), static_cast<unsigned long long>(RepliesSent.load()),
        static_cast<unsigned long long>(RepliesReceived.load()));
    say(line);
    if (Hover.valid) {
        MonsterData   data{};
        std::uint64_t age  = 0;
        const bool    have = LoadData(Hover.unitId, data, age);
        std::snprintf(line, sizeof(line),
            "Last hovered: unit %u, monstats row %d, %s%s%s%s, %zu affixes%s. Data: %s life %lld/%lld level %d, resist "
            "fire %d light %d cold %d poison %d magic %d physical %d%s%s, %u auras.",
            Hover.unitId, Hover.classId, Hover.boss ? "prime evil" : "not a prime evil", Hover.undead ? ", undead" : "",
            Hover.demon ? ", demon" : "", Hover.minion ? ", minion" : "", Hover.umodCount,
            Hover.auraEnchanted ? " incl. Aura Enchanted" : "",
            have ? "cached," : "none,", static_cast<long long>(data.life), static_cast<long long>(data.maxLife),
            data.level, data.resist[0], data.resist[1],
            data.resist[2], data.resist[3], data.resist[4], data.resist[5],
            (data.flags & 8U) != 0 ? " (ally, own values)" : "", (data.flags & 2U) != 0 ? " (capped ally)" : "",
            data.auraCount);
        say(line);
    }
    return D2RL::ConsoleCommandResult::Handled;
}

constexpr D2RL::PluginInfo Info{
    .infoSize    = D2RL::PluginInfoSize,
    .abiVersion  = D2RL_PLUGIN_ABI_VERSION,
    .id          = PluginIdText,
    .name        = "Monster Display",
    .version     = PluginVersion,
    .author      = "CelestialRayOne",
    .description = "Framed monster health bar with exact life, level, type, affixes with aura levels and true resistances.",
    .flags       = D2RL::PluginFlags::Shared | D2RL::PluginFlags::NativeHooks,
};

}  // namespace

D2RL_PLUGIN_EXPORT auto D2RLoaderGetPluginInfo() noexcept -> const D2RL::PluginInfo* {
    return &Info;
}

D2RL_PLUGIN_EXPORT auto D2RLoaderLoadPlugin(const D2RL::PluginContext* context) noexcept -> bool {
    if (!D2RL::HasContext(context) || context->abiVersion != D2RL_PLUGIN_ABI_VERSION) return false;
    Context = context;
    Base    = context->exeBase;
    if (Base == 0) return false;

    ReadConfiguration();
    if (!context->RegisterConsoleCommand("monsterdisplay", &StatusCommand, "Monster Display status; 'debug on|off'.")) {
        context->LogWarn("MonsterDisplay: the console command was not registered.");
    }
    if (!Config.enabled) {
        State = PluginState::DisabledByConfig;
        context->LogInfo("MonsterDisplay: disabled in the config; nothing installed.");
        return true;
    }
    if (!VerifyBuild()) {
        State          = PluginState::UnsupportedBuild;
        InactiveReason = "the game image does not match D2RLoader 1.3.1";
        return true;
    }
    ResolveFunctions();
    // Optional: a mismatch only leaves a bound demon to the Align test until
    // its server data arrives.
    BoundDemonHint.store(context->CheckExpectedBytes(BindDemonTestRva, BindDemonTestBytes, sizeof(BindDemonTestBytes)));
    if (!BoundDemonHint.load()) {
        context->LogWarn("MonsterDisplay: the game's bound demon test at RVA 0x533D60 does not match D2RLoader 1.3.1; a "
                         "bound demon shows the stock immunity line until its data arrives.");
    }

    if (context->QueryService(&Threads) != D2RL::ServiceQueryResult::Success
            || !D2RL::HasThreadServiceField(Threads, D2RL::ThreadServiceRequiredSize)) {
        Threads = nullptr;
        context->LogWarn("MonsterDisplay: the thread service is unavailable; exact life, level and resistances are "
                         "off.");
    }
    if (context->QueryService(&Lang) != D2RL::ServiceQueryResult::Success
            || !D2RL::HasLocalizationServiceField(Lang, D2RL::LocalizationServiceRequiredSize)) {
        Lang = nullptr;
        context->LogWarn("MonsterDisplay: the localization service is unavailable; type and aura names are off.");
    }
    RegisterChannel();
    RegisterLifecycle();
    RegisterBoundDemonEvent();

    // Armed before the hooks land: the builder can run the moment they do.
    Active.store(true);
    if (!context->InstallInlineHook(BuilderRva, BuilderEntryBytes, sizeof(BuilderEntryBytes), &HookedBuilder,
            &OriginalBuilder)
        || !context->InstallInlineHook(PanelUpdateRva, PanelUpdateEntryBytes, sizeof(PanelUpdateEntryBytes),
            &HookedPanelUpdate, &OriginalPanelUpdate)) {
        Active.store(false);
        State          = PluginState::InstallFailed;
        InactiveReason = "a hook was refused (is yin's monsterdisplay.dll still installed?)";
        context->LogError("MonsterDisplay: a hook was refused; remove yin's monsterdisplay.dll, which hooks the same "
                          "two functions.");
        return true;
    }
    State = PluginState::Active;
    D2RL::LogInfoF(context, "MonsterDisplay %s active: bar %d wide (x%.2f for prime evils), resistances %s.",
        PluginVersion, Config.barWidth, Config.bossMultiplier, Config.effective ? "effective" : "monster's own");
    return true;
}

D2RL_PLUGIN_EXPORT void D2RLoaderUnloadPlugin() noexcept {
    Active.store(false);
    constexpr auto UnsubscribeEnd = static_cast<std::uint32_t>(
        offsetof(D2RL::PluginCommunicationService, unsubscribeEvent) + sizeof(D2RL::PluginCommunication::UnsubscribeEventFn));
    if (BoundEvents != nullptr && BoundEventHandle != D2RL::PluginCommunication::InvalidSubscriptionHandle
            && D2RL::HasPluginCommunicationServiceField(BoundEvents, UnsubscribeEnd) && BoundEvents->unsubscribeEvent != nullptr) {
        BoundEvents->unsubscribeEvent(Context, BoundEventHandle);
        BoundEventHandle = D2RL::PluginCommunication::InvalidSubscriptionHandle;
    }
    // Hand the services back while the context is still valid.
    CapLease.Reset();
    LifeBarsLease.Reset();
    AffixesLease.Reset();
    SummonsLease.Reset();
    StatsRowLease.Reset();
}
