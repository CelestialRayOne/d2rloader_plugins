// =============================================================================
//  Jeweler  -  celestialrayone.jeweler
//
//  The Jeweler perks of the specialization system that need code:
//    Arcane Infusion   +10% to the modifiers of the character's amulet
//    Rings of Power    +5% to the modifiers of the character's rings, pooled
//    Amplification     +5% to the modifiers of every jewel and gem socketed in
//                      anything whose stats reach the character, pooled
//    Prismatic Resonance  +6% more to every socketed gem for each different
//                      gem color socketed in anything that counts, pooled
//                      together with Amplification
//    Mirrored Ring     one ring only, mirrored in the other ring slot: its
//                      modifiers are doubled and the other slot shows it as a
//                      ghost, the way a two-handed weapon shows in the shield
//                      slot
//    Six Peas in a Pod +6% more to every socketed gem for each gem of its
//                      color socketed in the same item, pooled with the rest
//    Primordial Jewels a socketed jewel gains one modifier of the perfect gem
//                      its graphic shows, fixed per jewel by its saved seed
//    Steady Pliers     +20% to the modifiers a gemword adds (never the gems'
//                      own), pooled with the rest
//  A perk counts while the character's own value of its stat is above 0.
//  Next to the perks, stats that are not perks:
//    Gem Effect        each [[gem_effect]] stat raises the socketed items of
//                      its types by the character's total value of the stat,
//                      in percent, pooled with the gem perks; with own_sockets
//                      by the value of the item they sit in, there only; a
//                      block can also be limited to uniques it names
//
//  Built for D2R 3.3 under D2RLoader 1.3.1 (D2RCore 1.3.1-beta). Every native
//  function the plugin calls was read out of the dumped D2RLoader 1.3.1 image
//  and decoded before being relied on. Byte witnesses pin each of them (the
//  function's own body, or the game's own call to it), and the stat array
//  layout is pinned against D2RCore!VisitWideUnitStats. On a different build
//  nothing is installed and the console command says why. The plugin patches
//  no game code: it only calls game functions from D2RLoader's thread service.
//
// -----------------------------------------------------------------------------
//  HOW A BONUS REACHES THE CHARACTER (the blacksmith plugin's mechanism)
// -----------------------------------------------------------------------------
//  Each item that earns a bonus carries one extra plain stat list of the
//  plugin's, attached to the item the way 0x43EF10 gives an item its
//  quest-difficulty list (43F407..43F487):
//      2F7300  alloc (10000h, 0, owner type 4, item id)
//      2F2CB0  attach (item, list, 1)       D2RCore AttachWideStatList, mode 1
//      2F7C00  set (context, list, stat, value, layer)
//  and removed with 2F7920 unlink (item, list) + 2F4180 free (context, list).
//  The list is a child of the item, so the item's tooltip shows the raised
//  values and its stats reach the character exactly when the item's do.
//  Never saved or sent: the item serializer only writes lists with flag 40h or
//  2040h. Server and client keep their own lists, built from the same items,
//  so combat (server) and the tooltips and character sheet (client) agree.
//
// -----------------------------------------------------------------------------
//  WHICH ITEMS COUNT
// -----------------------------------------------------------------------------
//  An item's stats reach the character exactly while the item's stat list is
//  merged into the character's. STATLIST_GetOwner 0x2F8120 (item, &mode)
//  returns the unit an item's list is merged into (the unit of the parent list
//  at ex +78h, read at the parent's +A0h), or null. The game's own refresh
//  0x46F220 walks the character's inventory with INVENTORY_GetFirstItem
//  0x388C10 / INVENTORY_GetNextItem 0x38ABA0 and re-merges every item 0x2F8120
//  returns an owner for (D2RCore AttachWideUnitStats). The merge leaves out the
//  inactive weapon set (body locations 11 and 12), 0x475140 leaves out broken
//  items, and a charm is merged only when ITEMS_IsCharmUsable 0x36AE00 allows
//  it (ESR's charm dedup included). So an item counts when 0x2F8120 returns the
//  character: equipped gear in the active set, and every charm the game lets
//  count, wherever its page is. Mode 0 (a dual-wielder's second weapon) still
//  counts: the merge then only holds back its damage stats for its own attacks.
//
// -----------------------------------------------------------------------------
//  AN ITEM'S MODIFIERS
// -----------------------------------------------------------------------------
//  An item's modifiers are read from the stat lists that hold them, never from
//  its full stats array (ex = unit +88h, +A8h, count +B0h, the array
//  D2RCore!VisitWideUnitStats reads, and the one 0x2F5020 reads). That array
//  is the engine's cache of each stat's total, and for a stat with an
//  itemstatcost op the engine stops keeping it current: when such a stat
//  changes, its own total is committed only while every op target on the same
//  item is 0, or while the op does not apply there (D2MOO sub_6FDB64A0, D2RCore
//  +3DBEB0, see the enhanced-damage-fix plugin). Otherwise the total keeps the
//  value it had before, so a bonus written next to the stat never shows in it.
//  passive_summ_mastery_total (op 1) on a ring that also has
//  passive_summ_mastery kept the 10 it had when the ring was loaded while this
//  plugin took its own addition back out of it every pass: 10, 20, 10, 20 at
//  100%, and a swing that grew without end above 100%. The op 13 stats
//  (item_armor_percent, item_maxdamage_percent, item_mindamage_percent,
//  item_maxdurability_percent) on an item with the base value they multiply,
//  and the op 4 and 5 per-level stats on an equipped item, are held back the
//  same way. The array also holds what the engine derives through an op (a
//  weapon's enhanced damage on its base damage, per-level defense), which is
//  not a modifier of the item at all.
//
//  The values come from the item's active child chain instead (ex +90h, next
//  at +68h), summed the way the engine sums them for the item: the stats of
//  every plain list there (+30h, count +38h, 16-byte entries {layer u32, stat
//  u16 at +4, value i32 at +8}), and for every item socketed in it (an extended
//  list in the chain, flag 80000000h) that item's full stats (+A8h, count
//  +B0h), which is what the engine adds into the host for it; both are the
//  layouts the pinned VisitWideUnitStats reads. A socketed item's stats never
//  change after it is socketed, so its totals are current. A list with flag
//  2000h is linked into the other chain (+98h) by the D2RCore attach and counts
//  for nothing, as for the engine. The item's own base values (+30h of the ex
//  itself) are not modifiers and stay out. A pair's modifier value is
//      list total - every plugin bonus list on the item
//  where a plugin bonus list is any plain list with flag 10000h in the item's
//  chain: the jeweler's own list and any other plugin's (the blacksmith carries
//  Unbreakable Gear and Extended Reach on a ring or the amulet when no armor
//  piece is worn). No bonus ever feeds on itself or on another plugin's bonus.
//  The total holds the item's affixes, unique and set item modifiers, the
//  active set bonus lines (state lists on the set item, switched by 0x3D62C0
//  through D2RCore SuppressWideStateStats), a runeword's or gemword's list,
//  and the items socketed in it.
//
// -----------------------------------------------------------------------------
//  SOCKETED JEWELS AND GEMS
// -----------------------------------------------------------------------------
//  Socketing (0x46EF30 -> 0x475140) writes a gem's gems.txt modifiers into the
//  gem's own modifier list (0x3D3670 mode 2, list flag 40h); a jewel keeps its
//  own affixes. The socketed item is then merged into its host, so 0x2F8120 on
//  it returns the host. The host's sockets are its own inventory
//  (UNITS_GetInventory 0x34A360 on the item, walked with 0x388C10/0x38ABA0).
//  Amplification reads each jewel's and gem's own modifiers from the jewel or
//  gem itself, so the host's modifiers and a gemword's bonus never count. The
//  bonus goes on the host, whose tooltip shows it.
//
//  MIRRORED RING (client side): the inventory slot widget's draw 0x2C9940
//  fetches the slot's item, then the item of the paired slot through
//  0x15F220 (4 <-> 5 only), and when the slot is empty and that item is
//  two-handed (GetItemHandednessForUnit == 2, 2C9C06) it draws it in this
//  slot with the background over it: the ghost. Its tooltip is that item's.
//  The plugin redirects that pairing call and that handedness call: for the
//  blocked ring slot, empty, with a ring in the allowed slot and the local
//  character holding the perk, the pair is the allowed slot and the ring
//  counts as two-handed. Every equip, pickup and swap of a body location goes
//  through 0x15C600 (held item, slot, ...), called from five places (2AA937,
//  2AAA79, 2CABDC, 2CAC6C, 2CB0AB); the plugin redirects all five: on the
//  blocked slot a pickup takes the ring from the allowed slot, a held ring
//  swaps with it, and with no ring there the held ring is refused with the
//  game's own "Impossible" (106B70 (player, 14h), as 0x15C600 does for an
//  item that does not fit). The slot's highlight (36BB10 at 2C9BA5) follows.
//  Nothing is sent for the blocked slot, so the server never needs a check.
//  Every redirect is a five-byte call to a near relay page, and every site is
//  byte-checked first; a mismatch leaves the client side off.
//
//  MIRRORED RING (both sides): while the allowed slot holds a ring that
//  counts and the blocked slot is empty, the ring's bonus list carries
//  bonus_percent (100) of every modifier it has, as a second identical ring
//  would; its Rings of Power share is taken twice, and the jewels and gems in
//  it are pooled twice by the gem perks.
//
//  Prismatic Resonance counts the colors of the gems socketed in the items
//  that count (a gem's color is the first color type it matches through
//  0x373890) and gives every socketed gem percent_per_color times that many
//  colors. Its percent is added to Amplification's for each gem, so both are
//  one numerator in the same pool and are rounded once. It uses Socketed
//  Gems' stats list. Six Peas in a Pod counts, per item, the gems of each
//  color in it and gives each gem percent_per_gem times the count of its color
//  there.
//
//  TOOLTIPS: an item's blue lines are drawn from one list of the item: the
//  first in its chain with state 0 and flag 40h (the finder 0x2F5A00), plus
//  the runeword list and that same list of every item in its sockets. The exe
//  draws them in 0x2DA0E0, and D2RCore's own tooltip builder (the one with
//  stat ranges, BuildItemTooltipWithStatRanges) draws them without ever
//  calling 0x2DA0E0, so no hook on the exe covers every tooltip. A plugin list
//  (flag 10000h) is never drawn. So on the CLIENT the bonuses are written into
//  that very list: the plugin remembers what it added to it and the value each
//  stat had right after, and treats its additions as gone the moment the list
//  is a different one or any of those values changed (the client rebuilt the
//  item from a server packet). Its own math always takes its additions back
//  out first, so a bonus is never raised by itself. The server keeps its
//  separate never-saved lists: client units are only ever drawn, never saved.
//
//  PRIMORDIAL JEWELS: a jewel's look is its graphic variant (item data +5Eh,
//  rolled from the item's seed when it is made, saved as 3 bits), and the
//  sprite number is that variant plus 1. The variant picks a perfect gem; its
//  gems.txt row (tables +1658h, 192 bytes) is the one whose +28h holds the
//  gem's items.txt row (the loader turns the "code" column into that row,
//  and D2RCore's LinkGemRows links items to gems by it), and holds nine mods, weapon, helm and
//  shield, each { property id + kind << 16, param, min, max } from +30h, 10h
//  apart. Kind 1 is a propertygroups.txt row (tables +258h, 200 bytes) whose
//  eight members { prop, parmin, parmax, modmin, modmax, chance } start at +8,
//  18h apart. Every property met this way (groups opened, members whose
//  property sets an excluded stat, gemmaster, left out) is one candidate, and
//  the jewel's starting seed (unit +30h, saved with the item) picks one, so the
//  same jewel always gets the same one. Only the SERVER has that seed: the
//  item decoder (0x378860) reads the 32 seed bits only when its save flag is
//  set ([rbp+67h] at 378C3D), and the items the client is sent carry none, so
//  every client copy of a jewel has the same +30h. The server therefore makes
//  the pick and records it by unit id (a client item has its server item's
//  id); the client applies exactly that record and shows nothing for a jewel
//  the server has not picked for (a client joined to a remote host). The game itself applies it: 0x3D5060
//  (context, 7, 0, jewel, prop, 0, 0, 0, prop, state 0, flag 10000h, 0), as
//  the cube applies a recipe mod but with the plugin's flag, so the stats land
//  on a list of the jewel's own that is never saved (0x3D5230 finds or makes
//  it). A min-max value outside the range funcs 15-17 is rolled from the seed
//  first, so the game rolls nothing. Arcane Infusion, Rings of Power and
//  Mirrored Ring see it through the host item's total; Amplification adds it
//  to the jewel's own modifiers. The list goes when the jewel leaves its
//  socket, the perk is lost or the plugin unloads.
//
//  STEADY PLIERS: a runeword's modifiers (runes.txt T1 codes, propertygroups
//  included) live in one list on the item, state 171 with flag 40h, which
//  2F5B80 (item, 171, 40h) finds by its state (2F58C0); the runes' or gems'
//  own modifiers stay on the socketed items. An item is a gemword when it has
//  that list and every item in its sockets is one of socket_types (gems).
//  The list's stats take bonus_percent into the gem pool on that item.
//
//  GEM EFFECT: one stat per [[gem_effect]] block (a stat named in several
//  blocks takes all of their types, so it counts once per item). A stat's
//  value is the character's total, read the way stat('x'.accr) reads it
//  (0x2F5020 on the character): gear, charms, skills and the socketed items
//  themselves, plus this plugin's bonuses on the character's items (a
//  mirrored ring's second copy of the stat), none of which depends on gem
//  effect. Each socketed item adds the values of the stats whose types it
//  matches to its percent in the gem pool, the way Prismatic Resonance's
//  joins it, so they are one numerator, rounded once. No gem effect stat is
//  ever raised by Gem Effect: an item holding one would otherwise raise its
//  own percent, or through another block the other stat's, pass after pass.
//  An item's total percent is floored at -100, so a negative value takes its
//  modifiers down to 0 and never past it.
//
//  A stat whose block has own_sockets takes its value from each host
//  instead: the host's own value, its total (0x2F5020 on the host) less its
//  base and every plugin bonus list on it, the way AN ITEM'S MODIFIERS reads
//  a modifier. That holds the host's affixes, its unique, set and runeword
//  modifiers and the items socketed in it, and never a plugin bonus (a
//  mirrored ring's second copy of the stat, or the Amplification share a
//  host carries for a jewel's copy of it), so the value never feeds on gem
//  effect. It raises only the items socketed in that host, in the same gem
//  pool, so it is rounded once with everything else. One stat has one
//  meaning: a stat named in several blocks takes the first one's own_sockets.
//
//  A block's host_uniques limits it to the items socketed in those uniques:
//  the host's quality is unique (7, ITEMS_GetItemQuality 0x36CF60, item data
//  +0) and its file index (0x36CE70, item data +34h) is one of the rows
//  named, the same two calls the unique pick 0x443910 makes to test an item
//  against a uniqueitems.txt row. The names are the index column, looked up
//  in the table of the character's own data context the way 0x443910 reads
//  it: rows at tables +13C8h, count +13D0h, 15Ch (348) bytes each (443A70,
//  443B82), the index a string of 31 at +2 (its column in the compiler
//  0x31F1D0). The percent is still the character's total of the stat. A
//  name no row has matches nothing, so such a block never counts in any
//  other item.
//
// -----------------------------------------------------------------------------
//  POOLING
// -----------------------------------------------------------------------------
//  Rings of Power and Amplification pool every counting item: per (stat, layer)
//  the numerators own * percent are summed and the bonus is sum / 100, rounded
//  toward zero like the game's own percents. It is split back over the items:
//  each gets floor(n / 100), and the points still missing go to the items with
//  the largest remainders (ties to the earlier item), so the shares add up to
//  the pooled bonus exactly and each tooltip shows about its own part. Stats
//  that only change their own item (itemstatcost op 13: enhanced defense,
//  enhanced damage, durability; 0x2F8AD0 op 13 multiplies the owner item's own
//  base) pool per item instead. An equipped item that does not count is
//  computed on its own (its tooltip shows its bonus) and adds nothing to the
//  pools.
//
// -----------------------------------------------------------------------------
//  ITEM TYPES
// -----------------------------------------------------------------------------
//  ITEMS_CheckItemTypeId 0x373890 (item, type) tests the item's types in the
//  itemtypes equivalence table, so a parent code covers its children.
//  itemtypes.txt codes are resolved against the live table (tables + 1348h,
//  count + 1350h, rows of E8h bytes, code at +0).
//
//  Passes run on D2RLoader's thread service: on the host's game thread every 4
//  game updates for every player in the game (the server client registry), and
//  on the UI thread every 100 ms for the local player.
//
//  Console command "jeweler" shows the settings, what is installed and
//  counters; "jeweler items" lists the bonus on each item of the character.
//  Settings: d2rloader/config/celestialrayone.jeweler.toml
// =============================================================================
#include <D2RLPlugin/api.h>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <climits>
#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

namespace CelestialRayOne::Jeweler {
namespace {

constexpr char PluginVersion[] = "1.5.4";

constexpr const char DefaultConfigToml[] = R"TOML(# celestialrayone.jeweler
#
# Jeweler
#   The Jeweler perks of the specialization system that need code. A perk
#   counts while the character's own value of its stat is above 0 (the value
#   the specializations plugin writes when the perk is learned). Each perk
#   below names its stat, so the stat ids follow your itemstatcost.txt.
#   Gem Effect, the last section, takes stats rather than perks: a stat's
#   value is the percent, from every source.
#
# How a bonus works
#   Every perk raises modifiers the way One's Favorite does in the blacksmith
#   plugin: the bonus is a hidden extra stat list on the item, never saved,
#   so the item's tooltip shows the raised values and the game treats them
#   like the item's own. For a stat with a parameter (a skill, a class, an
#   aura, a proc) only the value rises and the parameter stays.
#   item_charged_skill (204) is never raised: its value packs the charges and
#   the maximum charges. Bonuses other plugins put on an item (the
#   blacksmith's Unbreakable Gear and Extended Reach can ride on a ring or the
#   amulet) are not the item's modifiers and are never raised.
#
# Which items count
#   An item counts while its stats reach the character: equipped gear in the
#   active weapon set, and every charm the game lets count. An equipped item
#   that does not count right now (the inactive weapon set, a broken item)
#   still gets its own bonus, shown in its tooltip, but adds nothing to the
#   pools until it counts again.
#
# Pooling
#   Rings of Power and Amplification add the modifiers of every counting item
#   up per stat and parameter, take the percent once from the sum and round
#   once, so nothing is lost to rounding item by item: six gems with +10 life
#   give +3 life at 5%, where each gem alone would give 0. The pooled bonus
#   is split back over the items so each tooltip shows its own share, and the
#   point the pooling gains goes to the item that lost the most to rounding.
#   Rounding follows the game: toward zero.
#
# Item types
#   item_types takes itemtypes.txt codes. A parent code covers every type
#   below it: "ring" covers the class rings, "amul" the class amulets, "gem"
#   every gem and "jewl" every jewel, Colossal Jewels included.
#
# Built for D2RLoader 1.3.1 (D2R 3.3). Every native function the plugin calls
# is checked byte for byte at load; on any other build the plugin changes
# nothing and the console command "jeweler" says why. "jeweler items" lists
# the bonus on each item of your character.

[jeweler]

# Master switch. false loads the plugin without changing the game.
enabled = true

# true writes every bonus change to the plugin log (the first 200 lines).
# Keep false for play.
diagnostics = false

# true shows this plugin's bonuses in item tooltips, Primordial Jewels'
# modifier included. The game draws an item's blue lines only from the item's
# own modifier list, so on your screen the bonuses are written into that list
# (they are taken back out the moment the perk or the item no longer counts).
# The game itself keeps them apart, in lists that are never saved, so saves
# never contain them either way. false keeps them apart on screen too: they
# still count, they just do not show.
show_in_tooltips = true

# Stats that only change the item they sit in (itemstatcost op 13: enhanced
# defense, enhanced damage, maximum durability). Pooling never moves them
# between items: each item pools its own.
per_item_stats = [
  16,   # item_armor_percent
  17,   # item_maxdamage_percent
  18,   # item_mindamage_percent
  75,   # item_maxdurability_percent
]

# Arcane Infusion (jeweler_1)
#   Every stat listed in stats is raised by bonus_percent on the character's
#   amulet: its affixes, unique or set item modifiers, active green set bonus
#   lines, a gemword's bonus, and the jewels and gems socketed in it. Values
#   under 10 gain nothing at 10%.
#   One itemstatcost.txt id per line; delete a line to stop raising that stat.
[amulet_modifiers]
enabled = true
perk_stat = 961
bonus_percent = 10
item_types = ["amul"]
stats = [
  0,    # strength
  1,    # energy
  2,    # dexterity
  3,    # vitality
  7,    # maxhp
  9,    # maxmana
  11,   # maxstamina
  16,   # item_armor_percent
  17,   # item_maxdamage_percent
  18,   # item_mindamage_percent
  19,   # tohit
  20,   # toblock
  21,   # mindamage
  22,   # maxdamage
  23,   # secondary_mindamage
  24,   # secondary_maxdamage
  27,   # manarecoverybonus
  28,   # staminarecoverybonus
  31,   # armorclass
  32,   # armorclass_vs_missile
  33,   # armorclass_vs_hth
  34,   # normal_damage_reduction
  35,   # magic_damage_reduction
  36,   # damageresist
  37,   # magicresist
  38,   # maxmagicresist
  39,   # fireresist
  40,   # maxfireresist
  41,   # lightresist
  42,   # maxlightresist
  43,   # coldresist
  44,   # maxcoldresist
  45,   # poisonresist
  46,   # maxpoisonresist
  48,   # firemindam
  49,   # firemaxdam
  50,   # lightmindam
  51,   # lightmaxdam
  52,   # magicmindam
  53,   # magicmaxdam
  54,   # coldmindam
  55,   # coldmaxdam
  57,   # poisonmindam
  58,   # poisonmaxdam
  60,   # lifedrainmindam
  62,   # manadrainmindam
  67,   # velocitypercent
  68,   # attackrate
  74,   # hpregen
  76,   # item_maxhp_percent
  77,   # item_maxmana_percent
  78,   # item_attackertakesdamage
  79,   # item_goldbonus
  80,   # item_magicbonus
  83,   # item_addclassskills
  85,   # item_addexperience
  86,   # item_healafterkill
  87,   # item_reducedprices
  89,   # item_lightradius
  93,   # item_fasterattackrate
  96,   # item_fastermovevelocity
  97,   # item_nonclassskill
  99,   # item_fastergethitrate
  102,  # item_fasterblockrate
  105,  # item_fastercastrate
  107,  # item_singleskill
  109,  # curse_resistance
  110,  # item_poisonlengthresist
  111,  # item_normaldamage
  112,  # item_howl
  114,  # item_damagetomana
  116,  # item_fractionaltargetac
  119,  # item_tohit_percent
  120,  # item_damagetargetac
  121,  # item_demondamage_percent
  122,  # item_undeaddamage_percent
  123,  # item_demon_tohit
  124,  # item_undead_tohit
  126,  # item_elemskill
  127,  # item_allskills
  128,  # item_attackertakeslightdamage
  134,  # item_freeze
  135,  # item_openwounds
  136,  # item_crushingblow
  138,  # item_manaafterkill
  141,  # item_deadlystrike
  142,  # item_absorbfire_percent
  143,  # item_absorbfire
  144,  # item_absorblight_percent
  145,  # item_absorblight
  146,  # item_absorbmagic_percent
  147,  # item_absorbmagic
  148,  # item_absorbcold_percent
  149,  # item_absorbcold
  150,  # item_slow
  151,  # item_aura
  155,  # item_reanimate
  156,  # item_pierce
  158,  # item_explosivearrow
  159,  # item_throw_mindamage
  160,  # item_throw_maxdamage
  182,  # armor_override_percent
  187,  # item_pierce_cold_immunity
  188,  # item_addskill_tab
  189,  # item_pierce_fire_immunity
  190,  # item_pierce_light_immunity
  191,  # item_pierce_poison_immunity
  192,  # item_pierce_damage_immunity
  193,  # item_pierce_magic_immunity
  195,  # item_skillonattack
  196,  # item_skillonkill
  197,  # item_skillondeath
  198,  # item_skillonhit
  200,  # item_charge_noconsume
  201,  # item_skillongethit
  214,  # item_armor_perlevel
  216,  # item_hp_perlevel
  217,  # item_mana_perlevel
  218,  # item_maxdamage_perlevel
  219,  # item_maxdamage_percent_perlevel
  220,  # item_strength_perlevel
  221,  # item_dexterity_perlevel
  222,  # item_energy_perlevel
  223,  # item_vitality_perlevel
  224,  # item_tohit_perlevel
  225,  # item_tohitpercent_perlevel
  226,  # item_cold_damagemax_perlevel
  227,  # item_fire_damagemax_perlevel
  228,  # item_ltng_damagemax_perlevel
  229,  # item_pois_damagemax_perlevel
  230,  # item_resist_cold_perlevel
  231,  # item_resist_fire_perlevel
  232,  # item_resist_ltng_perlevel
  233,  # item_resist_pois_perlevel
  238,  # item_thorns_perlevel
  239,  # item_find_gold_perlevel
  240,  # item_find_magic_perlevel
  242,  # item_stamina_perlevel
  244,  # item_damage_undead_perlevel
  247,  # item_crushingblow_perlevel
  250,  # item_deadlystrike_perlevel
  255,  # strengthpercent
  256,  # dexteritypercent
  257,  # vitalitypercent
  258,  # energypercent
  265,  # passive_summ_elemental_resist
  266,  # passive_summ_life
  267,  # passive_summ_phys_resist
  287,  # item_skeleton_trade
  288,  # item_max_skeleton_king
  303,  # passive_summ_mastery_total
  329,  # passive_fire_mastery
  330,  # passive_ltng_mastery
  331,  # passive_cold_mastery
  332,  # passive_pois_mastery
  333,  # passive_fire_pierce
  334,  # passive_ltng_pierce
  335,  # passive_cold_pierce
  336,  # passive_pois_pierce
  338,  # passive_dodge
  339,  # passive_avoid
  340,  # passive_evade
  358,  # passive_mag_pierce
  366,  # passive_dmg_pierce
  371,  # item_skill_cooldown
  372,  # trap_limit
  373,  # twister_limit
  374,  # blade_nova_max_res
  376,  # item_skillontakedmg
  377,  # item_skillontakedmg_hidden
  378,  # item_skillonkill_hidden
  379,  # passive_summ_limit
  391,  # total_fire_resist
  392,  # total_cold_resist
  393,  # total_lightning_resist
  394,  # total_poison_resist
  398,  # generictotalmultiplier
  400,  # total_magic_resist
  404,  # item_skillonattack_hidden
  405,  # item_skillonhit_hidden
  409,  # totalattackspeed
  413,  # potionmultiplier
  415,  # item_skillongethit_hidden
  422,  # item_skilloncast
  423,  # item_skilloncast_hidden
  426,  # extra_items
  434,  # criticaldamage
  435,  # bleedmindam
  436,  # bleedmaxdam
  466,  # item_maxdamage_percent2
  467,  # item_mindamage_percent2
  482,  # passive_mag2_mastery
  483,  # passive_wind_mastery
  486,  # passive_summ_mastery
  489,  # total_elemental_min_damage
  493,  # total_elemental_max_damage
  495,  # total_poison_magic_min_damage
  496,  # total_poison_magic_max_damage
  498,  # item_elemskillcold
  499,  # item_elemskilllight
  500,  # item_elemskillpoison
  501,  # item_elemskillmagic
  502,  # item_elemskillfire
  505,  # lifeonstriking
  506,  # manaonstriking
  516,  # projectilespeed
  519,  # consumeeffect
  520,  # sigillimit
  522,  # miasmachainslimit
]

# Rings of Power (jeweler_2)
#   Every stat listed in stats is raised by bonus_percent on the character's
#   rings (affixes, unique or set item modifiers, active green set bonus
#   lines, gemword bonuses, socketed jewels and gems), pooled over both rings:
#   two rings with +15 life each give +1 life, where each alone gives 0.
[ring_modifiers]
enabled = true
perk_stat = 962
bonus_percent = 5
item_types = ["ring"]
stats = [
  0,    # strength
  1,    # energy
  2,    # dexterity
  3,    # vitality
  7,    # maxhp
  9,    # maxmana
  11,   # maxstamina
  16,   # item_armor_percent
  17,   # item_maxdamage_percent
  18,   # item_mindamage_percent
  19,   # tohit
  20,   # toblock
  21,   # mindamage
  22,   # maxdamage
  23,   # secondary_mindamage
  24,   # secondary_maxdamage
  27,   # manarecoverybonus
  28,   # staminarecoverybonus
  31,   # armorclass
  32,   # armorclass_vs_missile
  33,   # armorclass_vs_hth
  34,   # normal_damage_reduction
  35,   # magic_damage_reduction
  36,   # damageresist
  37,   # magicresist
  38,   # maxmagicresist
  39,   # fireresist
  40,   # maxfireresist
  41,   # lightresist
  42,   # maxlightresist
  43,   # coldresist
  44,   # maxcoldresist
  45,   # poisonresist
  46,   # maxpoisonresist
  48,   # firemindam
  49,   # firemaxdam
  50,   # lightmindam
  51,   # lightmaxdam
  52,   # magicmindam
  53,   # magicmaxdam
  54,   # coldmindam
  55,   # coldmaxdam
  57,   # poisonmindam
  58,   # poisonmaxdam
  60,   # lifedrainmindam
  62,   # manadrainmindam
  67,   # velocitypercent
  68,   # attackrate
  74,   # hpregen
  76,   # item_maxhp_percent
  77,   # item_maxmana_percent
  78,   # item_attackertakesdamage
  79,   # item_goldbonus
  80,   # item_magicbonus
  83,   # item_addclassskills
  85,   # item_addexperience
  86,   # item_healafterkill
  87,   # item_reducedprices
  89,   # item_lightradius
  93,   # item_fasterattackrate
  96,   # item_fastermovevelocity
  97,   # item_nonclassskill
  99,   # item_fastergethitrate
  102,  # item_fasterblockrate
  105,  # item_fastercastrate
  107,  # item_singleskill
  109,  # curse_resistance
  110,  # item_poisonlengthresist
  111,  # item_normaldamage
  112,  # item_howl
  114,  # item_damagetomana
  116,  # item_fractionaltargetac
  119,  # item_tohit_percent
  120,  # item_damagetargetac
  121,  # item_demondamage_percent
  122,  # item_undeaddamage_percent
  123,  # item_demon_tohit
  124,  # item_undead_tohit
  126,  # item_elemskill
  127,  # item_allskills
  128,  # item_attackertakeslightdamage
  134,  # item_freeze
  135,  # item_openwounds
  136,  # item_crushingblow
  138,  # item_manaafterkill
  141,  # item_deadlystrike
  142,  # item_absorbfire_percent
  143,  # item_absorbfire
  144,  # item_absorblight_percent
  145,  # item_absorblight
  146,  # item_absorbmagic_percent
  147,  # item_absorbmagic
  148,  # item_absorbcold_percent
  149,  # item_absorbcold
  150,  # item_slow
  151,  # item_aura
  155,  # item_reanimate
  156,  # item_pierce
  158,  # item_explosivearrow
  159,  # item_throw_mindamage
  160,  # item_throw_maxdamage
  182,  # armor_override_percent
  187,  # item_pierce_cold_immunity
  188,  # item_addskill_tab
  189,  # item_pierce_fire_immunity
  190,  # item_pierce_light_immunity
  191,  # item_pierce_poison_immunity
  192,  # item_pierce_damage_immunity
  193,  # item_pierce_magic_immunity
  195,  # item_skillonattack
  196,  # item_skillonkill
  197,  # item_skillondeath
  198,  # item_skillonhit
  200,  # item_charge_noconsume
  201,  # item_skillongethit
  214,  # item_armor_perlevel
  216,  # item_hp_perlevel
  217,  # item_mana_perlevel
  218,  # item_maxdamage_perlevel
  219,  # item_maxdamage_percent_perlevel
  220,  # item_strength_perlevel
  221,  # item_dexterity_perlevel
  222,  # item_energy_perlevel
  223,  # item_vitality_perlevel
  224,  # item_tohit_perlevel
  225,  # item_tohitpercent_perlevel
  226,  # item_cold_damagemax_perlevel
  227,  # item_fire_damagemax_perlevel
  228,  # item_ltng_damagemax_perlevel
  229,  # item_pois_damagemax_perlevel
  230,  # item_resist_cold_perlevel
  231,  # item_resist_fire_perlevel
  232,  # item_resist_ltng_perlevel
  233,  # item_resist_pois_perlevel
  238,  # item_thorns_perlevel
  239,  # item_find_gold_perlevel
  240,  # item_find_magic_perlevel
  242,  # item_stamina_perlevel
  244,  # item_damage_undead_perlevel
  247,  # item_crushingblow_perlevel
  250,  # item_deadlystrike_perlevel
  255,  # strengthpercent
  256,  # dexteritypercent
  257,  # vitalitypercent
  258,  # energypercent
  265,  # passive_summ_elemental_resist
  266,  # passive_summ_life
  267,  # passive_summ_phys_resist
  287,  # item_skeleton_trade
  288,  # item_max_skeleton_king
  303,  # passive_summ_mastery_total
  329,  # passive_fire_mastery
  330,  # passive_ltng_mastery
  331,  # passive_cold_mastery
  332,  # passive_pois_mastery
  333,  # passive_fire_pierce
  334,  # passive_ltng_pierce
  335,  # passive_cold_pierce
  336,  # passive_pois_pierce
  338,  # passive_dodge
  339,  # passive_avoid
  340,  # passive_evade
  358,  # passive_mag_pierce
  366,  # passive_dmg_pierce
  371,  # item_skill_cooldown
  372,  # trap_limit
  373,  # twister_limit
  374,  # blade_nova_max_res
  376,  # item_skillontakedmg
  377,  # item_skillontakedmg_hidden
  378,  # item_skillonkill_hidden
  379,  # passive_summ_limit
  391,  # total_fire_resist
  392,  # total_cold_resist
  393,  # total_lightning_resist
  394,  # total_poison_resist
  398,  # generictotalmultiplier
  400,  # total_magic_resist
  404,  # item_skillonattack_hidden
  405,  # item_skillonhit_hidden
  409,  # totalattackspeed
  413,  # potionmultiplier
  415,  # item_skillongethit_hidden
  422,  # item_skilloncast
  423,  # item_skilloncast_hidden
  426,  # extra_items
  434,  # criticaldamage
  435,  # bleedmindam
  436,  # bleedmaxdam
  466,  # item_maxdamage_percent2
  467,  # item_mindamage_percent2
  482,  # passive_mag2_mastery
  483,  # passive_wind_mastery
  486,  # passive_summ_mastery
  489,  # total_elemental_min_damage
  493,  # total_elemental_max_damage
  495,  # total_poison_magic_min_damage
  496,  # total_poison_magic_max_damage
  498,  # item_elemskillcold
  499,  # item_elemskilllight
  500,  # item_elemskillpoison
  501,  # item_elemskillmagic
  502,  # item_elemskillfire
  505,  # lifeonstriking
  506,  # manaonstriking
  516,  # projectilespeed
  519,  # consumeeffect
  520,  # sigillimit
  522,  # miasmachainslimit
]

# Amplification (jeweler_3)
#   Every stat listed in stats is raised by bonus_percent on every jewel and
#   gem socketed in anything that counts: weapons, armor, rings, the amulet
#   and charms. Only the jewels' and gems' own modifiers count, never the
#   host item's or a gemword's bonus. All of them are pooled together, and
#   each host's tooltip shows its share. A jewel or gem in the amulet or a
#   ring also gets that item's perk, added: +15% in the amulet with both.
#   item_types are the socketed item types that count.
[socketed_gems]
enabled = true
perk_stat = 963
bonus_percent = 5
item_types = ["gem", "jewl"]
stats = [
  0,    # strength
  1,    # energy
  2,    # dexterity
  3,    # vitality
  7,    # maxhp
  9,    # maxmana
  11,   # maxstamina
  16,   # item_armor_percent
  17,   # item_maxdamage_percent
  18,   # item_mindamage_percent
  19,   # tohit
  20,   # toblock
  21,   # mindamage
  22,   # maxdamage
  23,   # secondary_mindamage
  24,   # secondary_maxdamage
  27,   # manarecoverybonus
  28,   # staminarecoverybonus
  31,   # armorclass
  32,   # armorclass_vs_missile
  33,   # armorclass_vs_hth
  34,   # normal_damage_reduction
  35,   # magic_damage_reduction
  36,   # damageresist
  37,   # magicresist
  38,   # maxmagicresist
  39,   # fireresist
  40,   # maxfireresist
  41,   # lightresist
  42,   # maxlightresist
  43,   # coldresist
  44,   # maxcoldresist
  45,   # poisonresist
  46,   # maxpoisonresist
  48,   # firemindam
  49,   # firemaxdam
  50,   # lightmindam
  51,   # lightmaxdam
  52,   # magicmindam
  53,   # magicmaxdam
  54,   # coldmindam
  55,   # coldmaxdam
  57,   # poisonmindam
  58,   # poisonmaxdam
  60,   # lifedrainmindam
  62,   # manadrainmindam
  67,   # velocitypercent
  68,   # attackrate
  74,   # hpregen
  76,   # item_maxhp_percent
  77,   # item_maxmana_percent
  78,   # item_attackertakesdamage
  79,   # item_goldbonus
  80,   # item_magicbonus
  83,   # item_addclassskills
  85,   # item_addexperience
  86,   # item_healafterkill
  87,   # item_reducedprices
  89,   # item_lightradius
  93,   # item_fasterattackrate
  96,   # item_fastermovevelocity
  97,   # item_nonclassskill
  99,   # item_fastergethitrate
  102,  # item_fasterblockrate
  105,  # item_fastercastrate
  107,  # item_singleskill
  109,  # curse_resistance
  110,  # item_poisonlengthresist
  111,  # item_normaldamage
  112,  # item_howl
  114,  # item_damagetomana
  116,  # item_fractionaltargetac
  119,  # item_tohit_percent
  120,  # item_damagetargetac
  121,  # item_demondamage_percent
  122,  # item_undeaddamage_percent
  123,  # item_demon_tohit
  124,  # item_undead_tohit
  126,  # item_elemskill
  127,  # item_allskills
  128,  # item_attackertakeslightdamage
  134,  # item_freeze
  135,  # item_openwounds
  136,  # item_crushingblow
  138,  # item_manaafterkill
  141,  # item_deadlystrike
  142,  # item_absorbfire_percent
  143,  # item_absorbfire
  144,  # item_absorblight_percent
  145,  # item_absorblight
  146,  # item_absorbmagic_percent
  147,  # item_absorbmagic
  148,  # item_absorbcold_percent
  149,  # item_absorbcold
  150,  # item_slow
  151,  # item_aura
  155,  # item_reanimate
  156,  # item_pierce
  158,  # item_explosivearrow
  159,  # item_throw_mindamage
  160,  # item_throw_maxdamage
  182,  # armor_override_percent
  187,  # item_pierce_cold_immunity
  188,  # item_addskill_tab
  189,  # item_pierce_fire_immunity
  190,  # item_pierce_light_immunity
  191,  # item_pierce_poison_immunity
  192,  # item_pierce_damage_immunity
  193,  # item_pierce_magic_immunity
  195,  # item_skillonattack
  196,  # item_skillonkill
  197,  # item_skillondeath
  198,  # item_skillonhit
  200,  # item_charge_noconsume
  201,  # item_skillongethit
  214,  # item_armor_perlevel
  216,  # item_hp_perlevel
  217,  # item_mana_perlevel
  218,  # item_maxdamage_perlevel
  219,  # item_maxdamage_percent_perlevel
  220,  # item_strength_perlevel
  221,  # item_dexterity_perlevel
  222,  # item_energy_perlevel
  223,  # item_vitality_perlevel
  224,  # item_tohit_perlevel
  225,  # item_tohitpercent_perlevel
  226,  # item_cold_damagemax_perlevel
  227,  # item_fire_damagemax_perlevel
  228,  # item_ltng_damagemax_perlevel
  229,  # item_pois_damagemax_perlevel
  230,  # item_resist_cold_perlevel
  231,  # item_resist_fire_perlevel
  232,  # item_resist_ltng_perlevel
  233,  # item_resist_pois_perlevel
  238,  # item_thorns_perlevel
  239,  # item_find_gold_perlevel
  240,  # item_find_magic_perlevel
  242,  # item_stamina_perlevel
  244,  # item_damage_undead_perlevel
  247,  # item_crushingblow_perlevel
  250,  # item_deadlystrike_perlevel
  255,  # strengthpercent
  256,  # dexteritypercent
  257,  # vitalitypercent
  258,  # energypercent
  265,  # passive_summ_elemental_resist
  266,  # passive_summ_life
  267,  # passive_summ_phys_resist
  287,  # item_skeleton_trade
  288,  # item_max_skeleton_king
  303,  # passive_summ_mastery_total
  329,  # passive_fire_mastery
  330,  # passive_ltng_mastery
  331,  # passive_cold_mastery
  332,  # passive_pois_mastery
  333,  # passive_fire_pierce
  334,  # passive_ltng_pierce
  335,  # passive_cold_pierce
  336,  # passive_pois_pierce
  338,  # passive_dodge
  339,  # passive_avoid
  340,  # passive_evade
  358,  # passive_mag_pierce
  366,  # passive_dmg_pierce
  371,  # item_skill_cooldown
  372,  # trap_limit
  373,  # twister_limit
  374,  # blade_nova_max_res
  376,  # item_skillontakedmg
  377,  # item_skillontakedmg_hidden
  378,  # item_skillonkill_hidden
  379,  # passive_summ_limit
  391,  # total_fire_resist
  392,  # total_cold_resist
  393,  # total_lightning_resist
  394,  # total_poison_resist
  398,  # generictotalmultiplier
  400,  # total_magic_resist
  404,  # item_skillonattack_hidden
  405,  # item_skillonhit_hidden
  409,  # totalattackspeed
  413,  # potionmultiplier
  415,  # item_skillongethit_hidden
  422,  # item_skilloncast
  423,  # item_skilloncast_hidden
  426,  # extra_items
  434,  # criticaldamage
  435,  # bleedmindam
  436,  # bleedmaxdam
  466,  # item_maxdamage_percent2
  467,  # item_mindamage_percent2
  482,  # passive_mag2_mastery
  483,  # passive_wind_mastery
  486,  # passive_summ_mastery
  489,  # total_elemental_min_damage
  493,  # total_elemental_max_damage
  495,  # total_poison_magic_min_damage
  496,  # total_poison_magic_max_damage
  498,  # item_elemskillcold
  499,  # item_elemskilllight
  500,  # item_elemskillpoison
  501,  # item_elemskillmagic
  502,  # item_elemskillfire
  505,  # lifeonstriking
  506,  # manaonstriking
  516,  # projectilespeed
  519,  # consumeeffect
  520,  # sigillimit
  522,  # miasmachainslimit
]

# Prismatic Resonance (jeweler_9)
#   Every socketed gem gets percent_per_color more effect for each different
#   gem color socketed across everything that counts (weapons, armor, rings,
#   the amulet and charms): 4 colors give every gem +24%. It adds to Socketed
#   Gems for gems (+5% and +24% make +29%) and is pooled with it, so the two
#   are rounded together. It raises the stats in Amplification's stats list.
#   Jewels only get Amplification.
#   gem_types: the socketed item types that are gems.
#   color_types: one itemtypes.txt code per color; a gem counts as the first
#   color it matches. Note: in misc.txt the obsidian gems (gck to gbk) have
#   type gema (Amethyst), so they count as amethyst until they get gemk.
[prismatic_resonance]
enabled = true
perk_stat = 969
percent_per_color = 6
gem_types = ["gem"]
color_types = ["gema", "gemt", "gems", "geme", "gemr", "gemd", "gemz", "gemk"]

# Mirrored Ring (jeweler_10)
#   The character wears one ring, mirrored in the other ring slot. While the
#   allowed slot holds a ring and the other slot is empty, the ring's
#   modifiers are raised by bonus_percent (100 doubles them), its Ring
#   Modifiers share counts twice and the jewels and gems in it are pooled
#   twice, exactly as a second identical ring would add.
#   In the inventory the other slot shows the ring as a ghost, the way a
#   two-handed weapon shows in the shield slot: its tooltip is the ring's,
#   clicking it picks the ring up, and dropping a ring on it swaps the rings.
#   With no ring worn, a ring dropped on the other slot answers "Impossible".
#   A ring already in the other slot from before keeps working as a normal
#   ring, and the perk waits until that slot is empty.
#   ring_slot: the slot the ring goes in. 6 is the left ring slot on the
#   inventory screen (the game calls it the right ring), 7 the right one.
#   exclude_stats: itemstatcost ids that are never doubled: the ring's
#   requirements, the bookkeeping stat 356, and 204, whose value packs the
#   charges of a charged skill.
[mirrored_ring]
enabled = true
perk_stat = 970
bonus_percent = 100
ring_slot = 6
item_types = ["ring"]
exclude_stats = [
  91,   # item_req_percent
  92,   # item_levelreq
  204,  # item_charged_skill
  356,  # questitemdifficulty
]

# Six Peas in a Pod (jeweler_12)
#   Every socketed gem gets percent_per_gem more effect for each gem of its
#   color socketed in the same item, itself included (any grade): an item
#   with 5 rubies gives each of them +30%. Colors and gem types come from
#   Prismatic Resonance's color_types and gem_types. It adds to Amplification
#   and Prismatic Resonance and is pooled with them.
[kindred_gems]
enabled = true
perk_stat = 972
percent_per_gem = 6

# Primordial Jewels (jeweler_13)
#   A socketed jewel gains one modifier of the perfect gem it shows. The
#   jewel's sprite number (1 to 8) picks the gem below; that gem's gems.txt
#   weapon, helm and shield mods, property groups opened, are the choices, and
#   the jewel's saved seed picks one, so re-socketing the same jewel always
#   gives the same modifier. The game applies it the way it applies a gem's
#   mod, on a list that is never saved, for jewels socketed in any item the
#   character carries. Amplification, Arcane Infusion, Rings of Power and
#   Mirrored Ring raise it; Six Peas in a Pod and Prismatic Resonance do not.
#   gems: the perfect gem code for each sprite number, 1 to 8.
#   exclude_stats: itemstatcost ids a chosen property may never set.
[primordial_jewels]
enabled = true
perk_stat = 973
item_types = ["jewl"]
gems = ["gvb", "gbb", "gyb", "ggb", "grb", "gwb", "gbk", "skb"]
exclude_stats = [
  692,  # gemmaster
]

# Steady Pliers (jeweler_15)
#   The modifiers a gemword adds (its runes.txt T1 codes, propertygroups
#   included) are raised by bonus_percent. The gems' own modifiers are not:
#   the other gem perks cover those. An item counts as a gemword when it holds
#   a runeword and every item in its sockets is one of socket_types. Uses
#   Amplification's stats list and is pooled with the gem perks.
#   (Unsocketing gemwords without losing the gems is a cube recipe, not
#   this plugin.)
[steady_pliers]
enabled = true
perk_stat = 975
bonus_percent = 20
socket_types = ["gem"]

# Gem Effect (stats, not perks)
#   A gem effect stat raises the modifiers of the socketed items of its
#   types by the character's value of the stat, in percent: at 10, a gem's
#   +100 life gives +110. The value is the character's total from every
#   source (gear, charms, skills, the socketed items themselves), as
#   stat('...'.accr) reads it, so it works without any Jeweler perk. It adds
#   to Amplification, Prismatic Resonance and Six Peas in a Pod and is
#   pooled with them, so they are all rounded once. It raises the stats in
#   Amplification's stats list, never a gem effect stat itself. A negative
#   value lowers the effect; an item lowered by 100% or more gives nothing,
#   never the opposite.
#   Each [[gem_effect]] block is one stat. Copy the block for another stat
#   with other types. A socketed item matching several blocks gets all of
#   their values added up; a stat named in several blocks still counts once
#   per item. For example, one stat for gems and one for jewels:
#     [[gem_effect]]
#     stat_id = <your gem stat>
#     item_types = ["gem"]
#     [[gem_effect]]
#     stat_id = <your jewel stat>
#     item_types = ["jewl"]
#   stat_id: the itemstatcost.txt row of the stat (0 to 32767). Add the row
#   and put its id here; -1 leaves the block off.
#   item_types: the socketed item types it raises ("gem" is every gem,
#   "jewl" every jewel).
#   host_uniques: optional. Unique items, by the index column of
#   uniqueitems.txt exactly as written there (31 characters at most): the
#   block then raises only the items of its types socketed in one of those
#   uniques, still by the character's value of the stat. Leave it out, or
#   empty, for every item. A name uniqueitems.txt does not have matches
#   nothing, and the plugin log names it. For example, a stat for the jewels
#   in one unique:
#     [[gem_effect]]
#     stat_id = <your stat>
#     item_types = ["jewl"]
#     host_uniques = ["Your Unique Index"]
#   own_sockets: false (the default) takes the character's total of the
#   stat, as above. true takes each item's own value of the stat instead,
#   and that value raises only the items of its types socketed in that same
#   item. So the stat can sit on any item, a rare's affix, a unique, a set
#   item, a runeword, and works for that item alone; a jewel carrying it
#   raises the items socketed next to it, itself included. Plugin bonuses
#   never count toward an item's own value. Items still pool with each other.
#   A stat named in several blocks takes the first block's own_sockets.
#   For example, a stat for the jewels in whatever item carries it:
#     [[gem_effect]]
#     stat_id = <your stat>
#     item_types = ["jewl"]
#     own_sockets = true
#   enabled: false switches just this block off.
[[gem_effect]]
enabled = true
stat_id = -1
item_types = ["gem"]
)TOML";

// ---------------------------------------------------------------------------
// Native anchors (D2RLoader 1.3.1 image, D2R 3.3)
// ---------------------------------------------------------------------------
// STATLIST_GetOwner (item, &mode): ex = 34B870(item), parent = ex +78h (extended),
// mode = !(ex flags & 40000000h), returns the parent's unit +A0h. Whole body
// 2F8120..2F8191 (113 bytes)
constexpr std::uint8_t GetOwnerBody[]{
    0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B, 0xDA, 0x48, 0x85, 0xC9, 0x75, 0x15, 0x88, 0x4C,
    0x24, 0x30, 0x48, 0x8D, 0x4C, 0x24, 0x30, 0xE8, 0x44, 0x99, 0xFF, 0xFF, 0x84, 0xC0, 0x74, 0x1F,
    0xCC, 0xEB, 0x1C, 0xE8, 0x28, 0x37, 0x05, 0x00, 0x48, 0x8B, 0xC8, 0x48, 0x85, 0xC0, 0x74, 0x0F,
    0x48, 0x8B, 0x40, 0x78, 0x48, 0x85, 0xC0, 0x74, 0x06, 0x83, 0x78, 0x1C, 0x00, 0x7C, 0x13, 0x48,
    0x85, 0xDB, 0x74, 0x06, 0xC7, 0x03, 0x00, 0x00, 0x00, 0x00, 0x33, 0xC0, 0x48, 0x83, 0xC4, 0x20,
    0x5B, 0xC3, 0x48, 0x85, 0xDB, 0x74, 0x0D, 0x8B, 0x49, 0x1C, 0xC1, 0xE9, 0x1E, 0xF7, 0xD1, 0x83,
    0xE1, 0x01, 0x89, 0x0B, 0x48, 0x8B, 0x80, 0xA0, 0x00, 0x00, 0x00, 0x48, 0x83, 0xC4, 0x20, 0x5B,
    0xC3,
};
constexpr std::uint64_t GetOwnerBodyRva = 0x2F8120;

// INVENTORY_GetFirstItem (inventory): checks the 01020304h tag, returns +10h
// 388C10..388C4C (60 bytes)
constexpr std::uint8_t FirstItemBody[]{
    0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B, 0xD9, 0x48, 0x85, 0xC9, 0x74, 0x2E, 0x81, 0x39,
    0x04, 0x03, 0x02, 0x01, 0x74, 0x1C, 0x48, 0x8D, 0x4C, 0x24, 0x30, 0xC6, 0x44, 0x24, 0x30, 0x00,
    0xE8, 0x1B, 0xB4, 0xFF, 0xFF, 0x84, 0xC0, 0x74, 0x01, 0xCC, 0x81, 0x3B, 0x04, 0x03, 0x02, 0x01,
    0x75, 0x0A, 0x48, 0x8B, 0x43, 0x10, 0x48, 0x83, 0xC4, 0x20, 0x5B, 0xC3,
};
constexpr std::uint64_t FirstItemBodyRva = 0x388C10;

// INVENTORY_GetNextItem (item): item type 4, item data 34A500, returns data +A0h +10h
// 38ABA0..38AC3E (158 bytes)
constexpr std::uint8_t NextItemBody[]{
    0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B, 0xD9, 0x48, 0x85, 0xC9, 0x75, 0x10, 0x88, 0x4C,
    0x24, 0x30, 0x48, 0x8D, 0x4C, 0x24, 0x30, 0xE8, 0x84, 0x98, 0xFF, 0xFF, 0xEB, 0x67, 0xE8, 0x0D,
    0x0E, 0xFC, 0xFF, 0x83, 0xF8, 0x04, 0x74, 0x14, 0x48, 0x8D, 0x4C, 0x24, 0x30, 0xC6, 0x44, 0x24,
    0x30, 0x00, 0xE8, 0x19, 0xA1, 0xFF, 0xFF, 0x84, 0xC0, 0x74, 0x01, 0xCC, 0x48, 0x8B, 0xCB, 0xE8,
    0xEC, 0x0D, 0xFC, 0xFF, 0x83, 0xF8, 0x04, 0x75, 0x41, 0x48, 0x8B, 0xCB, 0xE8, 0xDF, 0x0D, 0xFC,
    0xFF, 0x83, 0xF8, 0x04, 0x75, 0x20, 0x48, 0x8B, 0xCB, 0xE8, 0x02, 0xF9, 0xFB, 0xFF, 0x48, 0x85,
    0xC0, 0x75, 0x2F, 0x48, 0x8D, 0x4C, 0x24, 0x30, 0x88, 0x44, 0x24, 0x30, 0xE8, 0x8F, 0x92, 0xFF,
    0xFF, 0x84, 0xC0, 0x74, 0x01, 0xCC, 0x48, 0x8D, 0x4C, 0x24, 0x30, 0xC6, 0x44, 0x24, 0x30, 0x00,
    0xE8, 0xBB, 0x88, 0xFF, 0xFF, 0x84, 0xC0, 0x74, 0x01, 0xCC, 0x33, 0xC0, 0x48, 0x83, 0xC4, 0x20,
    0x5B, 0xC3, 0x48, 0x05, 0xA0, 0x00, 0x00, 0x00, 0x74, 0xDC, 0x48, 0x8B, 0x40, 0x10,
};
constexpr std::uint64_t NextItemBodyRva = 0x38ABA0;

// DATATBLS_GetItemsTxtRecord (context, class): tables +15A0h, count +15A8h, stride 1C0h
// 314110..31416E (94 bytes)
constexpr std::uint8_t ItemsRecordBody[]{
    0x40, 0x57, 0x48, 0x83, 0xEC, 0x30, 0x8B, 0xFA, 0xE8, 0x73, 0xC9, 0xFE, 0xFF, 0x3B, 0xB8, 0xA8,
    0x15, 0x00, 0x00, 0x72, 0x08, 0x33, 0xC0, 0x48, 0x83, 0xC4, 0x30, 0x5F, 0xC3, 0x48, 0x89, 0x5C,
    0x24, 0x40, 0x48, 0x8D, 0x98, 0xA0, 0x15, 0x00, 0x00, 0x48, 0x89, 0x7C, 0x24, 0x50, 0x48, 0x3B,
    0x7B, 0x08, 0x72, 0x1E, 0x48, 0x8D, 0x44, 0x24, 0x50, 0x48, 0x89, 0x5C, 0x24, 0x28, 0x48, 0x8D,
    0x4C, 0x24, 0x20, 0x48, 0x89, 0x44, 0x24, 0x20, 0xE8, 0xF3, 0xBF, 0xFF, 0xFF, 0x84, 0xC0, 0x74,
    0x01, 0xCC, 0x48, 0x69, 0x44, 0x24, 0x50, 0xC0, 0x01, 0x00, 0x00, 0x48, 0x03, 0x03,
};
constexpr std::uint64_t ItemsRecordBodyRva = 0x314110;

// STATLIST_UnlinkStatList (unit, list): context of the unit, then the detach thunk
// 2F7920..2F793A (26 bytes)
constexpr std::uint8_t UnlinkStatListEntry[]{
    0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B, 0xDA, 0xE8, 0xB2, 0x27, 0x05, 0x00, 0x0F, 0xB6,
    0xC8, 0x48, 0x8B, 0xD3, 0x48, 0x83, 0xC4, 0x20, 0x5B, 0xE9,
};
constexpr std::uint64_t UnlinkStatListEntryRva = 0x2F7920;

// STATLIST_FreeStatList (context, list): frees a plain (non extended) list
// 2F4180..2F4190 (16 bytes)
constexpr std::uint8_t FreeStatListEntry[]{
    0x48, 0x85, 0xD2, 0x74, 0x0A, 0x83, 0x7A, 0x1C, 0x00, 0x0F, 0x8D, 0x21, 0x5D, 0x00, 0x00, 0xC3,
};
constexpr std::uint64_t FreeStatListEntryRva = 0x2F4180;

// INVENTORY_GetItemFromBodyLoc (inventory, loc), checks the 01020304h inventory tag
// 3886D0..38870B (59 bytes)
constexpr std::uint8_t BodyLocItemEntry[]{
    0x48, 0x89, 0x5C, 0x24, 0x10, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x63, 0xFA, 0x48, 0x8B, 0xD9,
    0x48, 0x85, 0xC9, 0x75, 0x20, 0x88, 0x4C, 0x24, 0x30, 0x48, 0x8D, 0x4C, 0x24, 0x30, 0xE8, 0xED,
    0xC9, 0xFF, 0xFF, 0x84, 0xC0, 0x74, 0x73, 0xCC, 0x33, 0xC0, 0x48, 0x8B, 0x5C, 0x24, 0x38, 0x48,
    0x83, 0xC4, 0x20, 0x5F, 0xC3, 0x81, 0x39, 0x04, 0x03, 0x02, 0x01,
};
constexpr std::uint64_t BodyLocItemEntryRva = 0x3886D0;

// UNITS_GetInventory (unit, file, line)
// 34A360..34A38C (44 bytes)
constexpr std::uint8_t InventoryEntry[]{
    0x48, 0x89, 0x5C, 0x24, 0x18, 0x56, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B, 0xF1, 0x48, 0x85, 0xC9,
    0x75, 0x13, 0x88, 0x4C, 0x24, 0x30, 0x48, 0x8D, 0x4C, 0x24, 0x30, 0xE8, 0x70, 0xCC, 0xFF, 0xFF,
    0x84, 0xC0, 0x74, 0x01, 0xCC, 0x48, 0x8B, 0x9E, 0xA0, 0x00, 0x00, 0x00,
};
constexpr std::uint64_t InventoryEntryRva = 0x34A360;

// CLIENT_GetUnitByIdAndType (id, type): client unit table, 128 buckets per type
// 09A5D0..09A5F1 (33 bytes)
constexpr std::uint8_t ClientUnitEntry[]{
    0x4C, 0x63, 0xCA, 0x48, 0x8D, 0x05, 0x36, 0x93, 0x98, 0x02, 0x8B, 0xD1, 0x44, 0x8B, 0xC1, 0x49,
    0x8B, 0xC9, 0x83, 0xE2, 0x7F, 0x48, 0xC1, 0xE1, 0x0A, 0x48, 0x03, 0xC8, 0xE9, 0x7F, 0x4C, 0x00,
    0x00,
};
constexpr std::uint64_t ClientUnitEntryRva = 0x09A5D0;

// SUNIT_GetServerUnit (game, type, id)
// 48FE80..48FE9A (26 bytes)
constexpr std::uint8_t ServerUnitEntry[]{
    0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x74, 0x24, 0x18, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x41,
    0x8B, 0xD8, 0x8B, 0xF2, 0x48, 0x8B, 0xF9, 0x48, 0x85, 0xC9,
};
constexpr std::uint64_t ServerUnitEntryRva = 0x48FE80;

// the local player's index
// 08B2D0..08B2D7 (7 bytes)
constexpr std::uint8_t LocalPlayerIndexEntry[]{
    0x8B, 0x05, 0x2E, 0x84, 0x99, 0x02, 0xC3,
};
constexpr std::uint64_t LocalPlayerIndexEntryRva = 0x08B2D0;

// CLIENT_GetLocalPlayer (index)
// 09A480..09A48D (13 bytes)
constexpr std::uint8_t PlayerFromIndexEntry[]{
    0x48, 0x89, 0x5C, 0x24, 0x08, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x83, 0xF9, 0x08,
};
constexpr std::uint64_t PlayerFromIndexEntryRva = 0x09A480;

// 0x43EF10 giving an item its quest-difficulty list: 2F5B80 find, 34A330 unit id,
// 2F7300 alloc (40h, 0, 4, id), 2F2CB0 attach (item, list, 1), 2F7C00 set (context, list, 356, value, 0)
// 43F407..43F487 (128 bytes)
constexpr std::uint8_t StatListWindow[]{
    0x8B, 0x0E, 0x33, 0xD2, 0x44, 0x8D, 0x42, 0x40, 0xE8, 0x6C, 0x67, 0xEB, 0xFF, 0x48, 0x8B, 0xF8,
    0x48, 0x85, 0xC0, 0x75, 0x37, 0x48, 0x8B, 0x0E, 0x48, 0x8D, 0x15, 0x9A, 0xA8, 0x8D, 0x01, 0x41,
    0xB8, 0x2E, 0x07, 0x00, 0x00, 0xE8, 0xFF, 0xAE, 0xF0, 0xFF, 0x44, 0x8B, 0xC8, 0x8D, 0x4F, 0x40,
    0x33, 0xD2, 0x44, 0x8D, 0x47, 0x04, 0xE8, 0xBE, 0x7E, 0xEB, 0xFF, 0x48, 0x8B, 0x0E, 0x45, 0x8B,
    0xC5, 0x48, 0x8B, 0xD0, 0x48, 0x8B, 0xF8, 0xE8, 0x5D, 0x38, 0xEB, 0xFF, 0x45, 0x0F, 0xB6, 0x8F,
    0x04, 0x01, 0x00, 0x00, 0x41, 0xB8, 0x64, 0x01, 0x00, 0x00, 0x41, 0x0F, 0xB6, 0x8F, 0x06, 0x01,
    0x00, 0x00, 0x48, 0x8B, 0xD7, 0x90, 0x44, 0x89, 0x74, 0x24, 0x20, 0xE8, 0x89, 0x87, 0xEB, 0xFF,
    0x48, 0x8B, 0x0E, 0x45, 0x8B, 0xC5, 0xBA, 0x10, 0x00, 0x00, 0x00, 0xE8, 0x69, 0xE4, 0xF2, 0xFF,
};
constexpr std::uint64_t StatListWindowRva = 0x43F407;

// 3738DC..3739C4 (232 bytes)
constexpr std::uint8_t TypeCheckBody[]{
    0xE8, 0xFF, 0x67, 0xFD, 0xFF, 0x0F, 0xB6, 0xC8, 0xE8, 0xA7, 0xD1, 0xF8, 0xFF, 0x48, 0x8B, 0x98,
    0x50, 0x13, 0x00, 0x00, 0x48, 0x63, 0xC3, 0x48, 0x3B, 0xC3, 0x75, 0x04, 0x85, 0xDB, 0x79, 0x14,
    0x48, 0x8D, 0x4C, 0x24, 0x40, 0xC6, 0x44, 0x24, 0x40, 0x00, 0xE8, 0x55, 0x30, 0xD1, 0xFF, 0x84,
    0xC0, 0x74, 0x01, 0xCC, 0x3B, 0xF3, 0x7D, 0x41, 0x41, 0xB8, 0x75, 0x12, 0x00, 0x00, 0x48, 0x8D,
    0x15, 0x6F, 0x0A, 0x99, 0x01, 0x48, 0x8B, 0xCF, 0xE8, 0x37, 0x5F, 0xFD, 0xFF, 0x48, 0x8B, 0xCF,
    0x8B, 0xD8, 0xE8, 0xAD, 0x67, 0xFD, 0xFF, 0x8B, 0xD3, 0x0F, 0xB6, 0xC8, 0xE8, 0xD3, 0x07, 0xFA,
    0xFF, 0x48, 0x85, 0xC0, 0x75, 0x2E, 0x48, 0x8D, 0x4C, 0x24, 0x40, 0x88, 0x44, 0x24, 0x40, 0xE8,
    0xF0, 0x52, 0xFF, 0xFF, 0x84, 0xC0, 0x74, 0x01, 0xCC, 0x33, 0xC0, 0x48, 0x8B, 0x5C, 0x24, 0x48,
    0x48, 0x8B, 0x6C, 0x24, 0x50, 0x48, 0x8B, 0x74, 0x24, 0x58, 0x48, 0x83, 0xC4, 0x20, 0x41, 0x5F,
    0x41, 0x5E, 0x5F, 0xC3, 0x0F, 0xBF, 0x98, 0x2E, 0x01, 0x00, 0x00, 0x83, 0xFB, 0xFF, 0x74, 0xD9,
    0x48, 0x8B, 0xCF, 0xE8, 0x5C, 0x67, 0xFD, 0xFF, 0x0F, 0xB6, 0xC8, 0xE8, 0x04, 0xD1, 0xF8, 0xFF,
    0x48, 0x8B, 0xE8, 0x0F, 0xAF, 0x98, 0x60, 0x13, 0x00, 0x00, 0x48, 0x8D, 0x88, 0x68, 0x13, 0x00,
    0x00, 0x48, 0x63, 0xD3, 0xE8, 0x7B, 0x38, 0xFF, 0xFF, 0x48, 0x8B, 0xD6, 0x48, 0x8B, 0xCE, 0x48,
    0xC1, 0xE9, 0x05, 0x83, 0xE2, 0x1F, 0x48, 0x8D, 0x34, 0x8D, 0x00, 0x00, 0x00, 0x00, 0x8B, 0x04,
    0x06, 0x48, 0x8D, 0x0D, 0x0C, 0x5D, 0xA2, 0x01,
};
constexpr std::uint64_t TypeCheckBodyRva = 0x3738DC;

// server client registry reset: 256 buckets at 2AAECA0, ready flag 2AAF4A0
// 485290..4852D4 (68 bytes)
constexpr std::uint8_t RegistryReset[]{
    0x33, 0xD2, 0x48, 0x8D, 0x05, 0x07, 0x9A, 0x62, 0x02, 0x8D, 0x4A, 0x20, 0x0F, 0x1F, 0x40, 0x00,
    0x48, 0x89, 0x10, 0x48, 0x89, 0x50, 0x08, 0x48, 0x89, 0x50, 0x10, 0x48, 0x8D, 0x40, 0x40, 0x48,
    0x89, 0x50, 0xD8, 0x48, 0x89, 0x50, 0xE0, 0x48, 0x89, 0x50, 0xE8, 0x48, 0x89, 0x50, 0xF0, 0x48,
    0x89, 0x50, 0xF8, 0x48, 0x83, 0xE9, 0x01, 0x75, 0xD7, 0xC7, 0x05, 0xCD, 0xA1, 0x62, 0x02, 0x01,
    0x00, 0x00, 0x00, 0xC3,
};
constexpr std::uint64_t RegistryResetRva = 0x485290;

// server client lookup: next client at +630h
// 485320..485346 (38 bytes)
constexpr std::uint8_t ClientLookup[]{
    0x0F, 0xB6, 0xC1, 0x48, 0x8D, 0x15, 0x76, 0x99, 0x62, 0x02, 0x48, 0x8B, 0x04, 0xC2, 0x48, 0x85,
    0xC0, 0x74, 0x10, 0x39, 0x08, 0x74, 0x0E, 0x48, 0x8B, 0x80, 0x30, 0x06, 0x00, 0x00, 0x48, 0x85,
    0xC0, 0x75, 0xF0, 0x33, 0xC0, 0xC3,
};
constexpr std::uint64_t ClientLookupRva = 0x485320;

// server client fields: unit id +270h, unit type +26Ch, game +2B0h
// 484CDB..484CF4 (25 bytes)
constexpr std::uint8_t ClientPlayer[]{
    0x44, 0x8B, 0x83, 0x70, 0x02, 0x00, 0x00, 0x8B, 0x93, 0x6C, 0x02, 0x00, 0x00, 0x48, 0x8B, 0x8B,
    0xB0, 0x02, 0x00, 0x00, 0xE8, 0x8C, 0xB1, 0x00, 0x00,
};
constexpr std::uint64_t ClientPlayerRva = 0x484CDB;


constexpr std::uint64_t ReadOwnStatRva      = 0x2F48C0;  // jmp D2RCore ReadWideUnitBaseStat (unit, stat, layer)
constexpr std::uint64_t ReadUnitStatRva     = 0x2F5020;  // jmp D2RCore ReadWideUnitStat (unit, stat, layer)
constexpr std::uint64_t GetDataContextRva   = 0x34A0E0;  // (unit) -> data context, called inside TypeCheckBody
constexpr std::uint64_t GetDataTablesRva    = 0x300A90;  // (context) -> tables, called inside TypeCheckBody
constexpr std::uint64_t CheckItemTypeRva    = 0x373890;  // ITEMS_CheckItemTypeId (item, type)
constexpr std::uint64_t ItemsRecordRva      = 0x314110;  // (context, class) -> items.txt record
constexpr std::uint64_t AllocStatListRva    = 0x2F7300;  // (flags, end frame, owner type, owner id)
constexpr std::uint64_t AttachStatListRva   = 0x2F2CB0;  // loader thunk, (unit, list, update)
constexpr std::uint64_t SetListStatRva      = 0x2F7C00;  // loader thunk, (context, list, stat, value, layer)
constexpr std::uint64_t UnlinkStatListRva   = 0x2F7920;  // (unit, list)
constexpr std::uint64_t FreeStatListRva     = 0x2F4180;  // (context, list)
constexpr std::uint64_t InventoryRva        = 0x34A360;  // UNITS_GetInventory (unit, file, line)
constexpr std::uint64_t BodyLocItemRva      = 0x3886D0;  // INVENTORY_GetItemFromBodyLoc (inventory, body location)
constexpr std::uint64_t FirstItemRva        = 0x388C10;  // INVENTORY_GetFirstItem (inventory)
constexpr std::uint64_t NextItemRva         = 0x38ABA0;  // INVENTORY_GetNextItem (item)
constexpr std::uint64_t GetOwnerRva         = 0x2F8120;  // STATLIST_GetOwner (item, &mode)
constexpr std::uint64_t FindStatListRva     = 0x2F5B80;  // (unit, state, flag), called in the window at 43F40F
constexpr std::int32_t  RunewordState       = 171;       // a runeword's modifier list (states.txt "runeword"), flag 40h
constexpr std::uint64_t ServerUnitRva       = 0x48FE80;  // (game, type, id)
constexpr std::uint64_t ClientUnitRva       = 0x09A5D0;  // (id, type)
constexpr std::uint64_t LocalPlayerIndexRva = 0x08B2D0;
constexpr std::uint64_t PlayerFromIndexRva  = 0x09A480;
constexpr std::uint64_t RegistryRva         = 0x2AAECA0; // proven by RegistryReset and ClientLookup
constexpr std::uint64_t RegistryReadyRva    = 0x2AAF4A0;
constexpr std::size_t   RegistryBuckets     = 256;
constexpr std::size_t   RegistryNext        = 0x630;
constexpr std::size_t   RegistryUnitType    = 0x26C;
constexpr std::size_t   RegistryUnitId      = 0x270;
constexpr std::size_t   RegistryGame        = 0x2B0;

constexpr std::size_t   UnitClassId         = 0x04;
constexpr std::size_t   UnitId              = 0x08;
constexpr std::size_t   UnitStatListEx      = 0x88;
constexpr std::size_t   ListFlags           = 0x1C;
constexpr std::size_t   ListStats           = 0x30;
constexpr std::size_t   ListStatsCount      = 0x38;
constexpr std::size_t   ListNext            = 0x68;
constexpr std::size_t   ExChildren          = 0x90;
constexpr std::size_t   ExFullStats         = 0xA8;
constexpr std::size_t   ExFullStatsCount    = 0xB0;
constexpr std::size_t   StatEntrySize       = 0x10;
constexpr std::size_t   StatEntryLayer      = 0x00;
constexpr std::size_t   StatEntryStat       = 0x04;
constexpr std::size_t   StatEntryValue      = 0x08;
constexpr std::size_t   TablesStatCount     = 0x1260;  // itemstatcost rows
constexpr std::size_t   TablesItemTypes     = 0x1348;  // itemtypes rows
constexpr std::size_t   TablesItemTypeCount = 0x1350;
constexpr std::size_t   ItemTypeStride      = 0xE8;
constexpr std::size_t   ItemCode            = 0x80;    // items.txt code
constexpr std::size_t   TablesItemCount     = 0x15A8;  // items.txt rows (DATATBLS_GetItemsTxtRecord checks it)
constexpr std::uint32_t BonusListFlags      = 0x10000;
constexpr std::uint32_t UnitPlayer          = 0;
constexpr std::uint32_t UnitItem            = 4;
constexpr std::int32_t  FirstBodyLoc        = 1;
constexpr std::int32_t  LastBodyLoc         = 12;
constexpr std::int32_t  ChargedSkill        = 204;  // item_charged_skill: its value packs charges and maximum charges
constexpr std::int32_t  MaxStatId           = 0x8000;  // D2RCore's stat id limit (SetWideListStat cmp esi,8000h)
constexpr std::size_t   MaxChainNodes       = 512;
constexpr std::size_t   MaxInventoryItems   = 4096;
constexpr std::size_t   MaxSocketedItems    = 64;
constexpr std::uint64_t ServerPassUpdates   = 4;
constexpr std::uint64_t ClientPassMs        = 100;

// D2RCore!VisitWideUnitStats (1.3.1-beta), whole body 832560..8325F6: ex =
// unit +88h; extended (+1Ch < 0) -> array +A8h, count +B0h, else +30h / +38h;
// entries of 10h bytes (add r15,10h). The same entries are what the attach
// (3DA4F0) adds up and the stat readers (3D8790, 3D8BD0) search.
constexpr std::uint8_t VisitWideUnitStatsBytes[]{
    0x41, 0x57, 0x41, 0x56, 0x56, 0x57, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x85, 0xD2, 0x74, 0x7A,
    0x48, 0x89, 0xD6, 0x48, 0x85, 0xC9, 0x74, 0x5D, 0x48, 0x8B, 0xB9, 0x88, 0x00, 0x00, 0x00, 0x48,
    0x85, 0xFF, 0x74, 0x51, 0x83, 0x7F, 0x1C, 0x00, 0xB8, 0xA8, 0x00, 0x00, 0x00, 0xBB, 0x30, 0x00,
    0x00, 0x00, 0x48, 0x0F, 0x48, 0xD8, 0x4C, 0x8B, 0x74, 0x1F, 0x08, 0x4D, 0x85, 0xF6, 0x74, 0x35,
    0x45, 0x31, 0xFF, 0x66, 0x66, 0x66, 0x66, 0x2E, 0x0F, 0x1F, 0x84, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x48, 0x8B, 0x0E, 0x89, 0xC8, 0x48, 0x83, 0xE1, 0xFE, 0xA8, 0x01, 0x48, 0x0F, 0x44, 0xCE, 0x48,
    0x8B, 0x01, 0x48, 0x8B, 0x14, 0x1F, 0x4C, 0x01, 0xFA, 0xFF, 0x50, 0x08, 0x49, 0x83, 0xC7, 0x10,
    0x49, 0xFF, 0xCE, 0x75, 0xDB, 0x48, 0x89, 0xF1, 0x48, 0x83, 0xC4, 0x20, 0x5B, 0x5F, 0x5E, 0x41,
    0x5E, 0x41, 0x5F, 0x48, 0xFF, 0x25, 0xC6, 0xDB, 0xEC, 0xFF, 0x48, 0x83, 0xC4, 0x20, 0x5B, 0x5F,
    0x5E, 0x41, 0x5E, 0x41, 0x5F, 0xC3,
};

// Tooltips (see the header)
// 0x2F5A00 (unit, state, flags): the first list of the unit's chain (+90h) with that state (+20h) and a flag (+1Ch) in common
// 2F5A00..2F5A99 (153 bytes)
constexpr std::uint8_t TooltipListBody[]{
    0x48, 0x89, 0x5C, 0x24, 0x10, 0x48, 0x89, 0x74, 0x24, 0x18, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x41,
    0x8B, 0xD8, 0x8B, 0xF2, 0x48, 0x8B, 0xF9, 0x48, 0x85, 0xC9, 0x75, 0x13, 0x88, 0x4C, 0x24, 0x30,
    0x48, 0x8D, 0x4C, 0x24, 0x30, 0xE8, 0x16, 0xB8, 0xFF, 0xFF, 0x84, 0xC0, 0x74, 0x01, 0xCC, 0x48,
    0x8B, 0xCF, 0xE8, 0x39, 0x5E, 0x05, 0x00, 0x48, 0x8B, 0xF8, 0x48, 0x85, 0xC0, 0x74, 0x58, 0x83,
    0x78, 0x1C, 0x00, 0x7C, 0x14, 0x48, 0x8D, 0x4C, 0x24, 0x30, 0xC6, 0x44, 0x24, 0x30, 0x00, 0xE8,
    0x3C, 0xB9, 0xFF, 0xFF, 0x84, 0xC0, 0x74, 0x01, 0xCC, 0x83, 0x7F, 0x1C, 0x00, 0x7D, 0x38, 0x48,
    0x8B, 0x87, 0x90, 0x00, 0x00, 0x00, 0x0F, 0xBA, 0xE3, 0x0D, 0x73, 0x0B, 0x48, 0x8B, 0x87, 0x98,
    0x00, 0x00, 0x00, 0x0F, 0xBA, 0xF3, 0x0D, 0x48, 0x85, 0xC0, 0x74, 0x1B, 0x0F, 0x1F, 0x40, 0x00,
    0x39, 0x70, 0x20, 0x75, 0x09, 0x85, 0xDB, 0x74, 0x10, 0x85, 0x58, 0x1C, 0x75, 0x0B, 0x48, 0x8B,
    0x40, 0x68, 0x48, 0x85, 0xC0, 0x75, 0xE9, 0x33, 0xC0,
};
constexpr std::uint64_t TooltipListBodyRva = 0x2F5A00;

// Primordial Jewels (see the header)
// 0x3D3420, the cube's property apply: 3D5060 (context, 7, 0, item, prop, 0, 0, 0, prop, state 0, flag 40h, 0)
// 3D3420..3D347F (95 bytes)
constexpr std::uint8_t ApplyWrapper[]{
    0x48, 0x85, 0xC9, 0x74, 0x59, 0x48, 0x89, 0x5C, 0x24, 0x08, 0x57, 0x48, 0x83, 0xEC, 0x60, 0x48,
    0x8B, 0xFA, 0x48, 0x8B, 0xD9, 0xE8, 0xA6, 0x6C, 0xF7, 0xFF, 0x0F, 0xB6, 0xC8, 0x4C, 0x8B, 0xCB,
    0x33, 0xC0, 0x45, 0x33, 0xC0, 0x48, 0x89, 0x44, 0x24, 0x58, 0xC7, 0x44, 0x24, 0x50, 0x40, 0x00,
    0x00, 0x00, 0x89, 0x44, 0x24, 0x48, 0x48, 0x89, 0x7C, 0x24, 0x40, 0x8D, 0x50, 0x07, 0x89, 0x44,
    0x24, 0x38, 0x89, 0x44, 0x24, 0x30, 0x89, 0x44, 0x24, 0x28, 0x48, 0x89, 0x7C, 0x24, 0x20, 0xE8,
    0xEC, 0x1B, 0x00, 0x00, 0x48, 0x8B, 0x5C, 0x24, 0x70, 0x48, 0x83, 0xC4, 0x60, 0x5F, 0xC3,
};
constexpr std::uint64_t ApplyWrapperRva = 0x3D3420;

// 0x3D4620's loop over a property's seven funcs: func byte +18h+i, set +2+i, val word +0Ah+2i, stat word +20h+2i
// 3D4710..3D477F (111 bytes)
constexpr std::uint8_t PropertyLoop[]{
    0x0F, 0xB6, 0x4C, 0x3B, 0x18, 0x3B, 0x0D, 0xBD, 0x24, 0xFB, 0x01, 0x7D, 0x7C, 0x4C, 0x8B, 0x14,
    0xCA, 0x4D, 0x85, 0xD2, 0x74, 0x73, 0x48, 0x8B, 0x84, 0x24, 0xD8, 0x00, 0x00, 0x00, 0x4D, 0x8B,
    0xCF, 0x0F, 0xB6, 0x4C, 0x3B, 0x02, 0x4D, 0x8B, 0xC4, 0x48, 0x89, 0x44, 0x24, 0x58, 0x41, 0x8B,
    0xD5, 0x8B, 0x84, 0x24, 0xD0, 0x00, 0x00, 0x00, 0x89, 0x44, 0x24, 0x50, 0x8B, 0x84, 0x24, 0xC8,
    0x00, 0x00, 0x00, 0x89, 0x44, 0x24, 0x48, 0x0F, 0xB7, 0x44, 0x5F, 0x0A, 0x89, 0x6C, 0x24, 0x40,
    0x66, 0x89, 0x44, 0x24, 0x38, 0x0F, 0xB7, 0x44, 0x5F, 0x20, 0x66, 0x89, 0x44, 0x24, 0x30, 0x89,
    0x4C, 0x24, 0x28, 0x41, 0x0F, 0xB6, 0xCE, 0x48, 0x89, 0x74, 0x24, 0x20, 0x41, 0xFF, 0xD2,
};
constexpr std::uint64_t PropertyLoopRva = 0x3D4710;

// properties.txt record getter 0x3D3210: records at tables +240h, count +248h, stride 30h
// 3D3210..3D32AD (157 bytes)
constexpr std::uint8_t PropertiesGetter[]{
    0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x74, 0x24, 0x20, 0x57, 0x48, 0x83, 0xEC, 0x30, 0x48,
    0x63, 0xDA, 0x8B, 0xC3, 0xC1, 0xE8, 0x10, 0x66, 0x85, 0xC0, 0x0F, 0x85, 0x8D, 0x00, 0x00, 0x00,
    0xE8, 0x5B, 0xD8, 0xF2, 0xFF, 0x48, 0x0F, 0xBF, 0xF3, 0x48, 0x8B, 0xF8, 0x48, 0x8B, 0xDE, 0x85,
    0xF6, 0x78, 0x09, 0x48, 0x3B, 0x98, 0x48, 0x02, 0x00, 0x00, 0x72, 0x18, 0x48, 0x8D, 0x4C, 0x24,
    0x48, 0xC6, 0x44, 0x24, 0x48, 0x00, 0xE8, 0x05, 0x98, 0xEE, 0xFF, 0x84, 0xC0, 0x74, 0x01, 0xCC,
    0x85, 0xF6, 0x78, 0x59, 0x48, 0x3B, 0x9F, 0x48, 0x02, 0x00, 0x00, 0x73, 0x50, 0x48, 0x81, 0xC7,
    0x40, 0x02, 0x00, 0x00, 0x48, 0x89, 0x5C, 0x24, 0x50, 0x48, 0x3B, 0x5F, 0x08, 0x72, 0x1E, 0x48,
    0x8D, 0x44, 0x24, 0x50, 0x48, 0x89, 0x7C, 0x24, 0x28, 0x48, 0x8D, 0x4C, 0x24, 0x20, 0x48, 0x89,
    0x44, 0x24, 0x20, 0xE8, 0x38, 0x97, 0xEE, 0xFF, 0x84, 0xC0, 0x74, 0x01, 0xCC, 0x48, 0x8B, 0x44,
    0x24, 0x50, 0x48, 0x8D, 0x04, 0x40, 0x48, 0xC1, 0xE0, 0x04, 0x48, 0x03, 0x07,
};
constexpr std::uint64_t PropertiesGetterRva = 0x3D3210;

// propertygroups.txt record getter 0x2D9670: records at tables +258h, count +260h, stride C8h
// 2D9670..2D970B (155 bytes)
constexpr std::uint8_t GroupsGetter[]{
    0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x74, 0x24, 0x20, 0x89, 0x54, 0x24, 0x10, 0x57, 0x48,
    0x83, 0xEC, 0x30, 0x48, 0x63, 0xDA, 0xE8, 0x05, 0x74, 0x02, 0x00, 0x66, 0x83, 0x7C, 0x24, 0x4A,
    0x01, 0x48, 0x8B, 0xF0, 0x0F, 0x85, 0x81, 0x00, 0x00, 0x00, 0x48, 0x0F, 0xBF, 0xFB, 0x48, 0x8B,
    0xDF, 0x85, 0xFF, 0x78, 0x09, 0x48, 0x3B, 0x98, 0x60, 0x02, 0x00, 0x00, 0x72, 0x18, 0x48, 0x8D,
    0x4C, 0x24, 0x48, 0xC6, 0x44, 0x24, 0x48, 0x00, 0xE8, 0x13, 0xFD, 0xFF, 0xFF, 0x84, 0xC0, 0x74,
    0x01, 0xCC, 0x85, 0xFF, 0x78, 0x55, 0x48, 0x3B, 0x9E, 0x60, 0x02, 0x00, 0x00, 0x73, 0x4C, 0x48,
    0x8D, 0xBE, 0x58, 0x02, 0x00, 0x00, 0x48, 0x89, 0x5C, 0x24, 0x50, 0x48, 0x3B, 0x5F, 0x08, 0x72,
    0x1E, 0x48, 0x8D, 0x44, 0x24, 0x50, 0x48, 0x89, 0x7C, 0x24, 0x28, 0x48, 0x8D, 0x4C, 0x24, 0x20,
    0x48, 0x89, 0x44, 0x24, 0x20, 0xE8, 0x16, 0xFF, 0xFF, 0xFF, 0x84, 0xC0, 0x74, 0x01, 0xCC, 0x48,
    0x69, 0x44, 0x24, 0x50, 0xC8, 0x00, 0x00, 0x00, 0x48, 0x03, 0x07,
};
constexpr std::uint64_t GroupsGetterRva = 0x2D9670;

// 0x3D4DB0 reading a group member: prop +8, parmin +0Ch, parmax +10h, modmin +14h, modmax +18h, 18h apart
// 3D4F3F..3D4F84 (69 bytes)
constexpr std::uint8_t GroupMemberRead[]{
    0x8B, 0x10, 0x83, 0xFA, 0x08, 0x0F, 0x83, 0x8E, 0x00, 0x00, 0x00, 0x48, 0x8D, 0x0C, 0x52, 0x0F,
    0x57, 0xC0, 0x0F, 0x11, 0x44, 0x24, 0x78, 0x8B, 0x44, 0xCE, 0x08, 0x89, 0x44, 0x24, 0x78, 0x8B,
    0x44, 0xCE, 0x14, 0x89, 0x45, 0x80, 0x8B, 0x44, 0xCE, 0x18, 0x89, 0x45, 0x84, 0x8B, 0x44, 0xCE,
    0x0C, 0x44, 0x8B, 0x44, 0xCE, 0x10, 0x41, 0x3B, 0xC0, 0x74, 0x0A, 0x8B, 0xD0, 0x48, 0x8B, 0xCF,
    0xE8, 0xDC, 0x08, 0x00, 0x00,
};
constexpr std::uint64_t GroupMemberReadRva = 0x3D4F3F;

// gems.txt loader 0x3148E0: rows at tables +1658h (begin, count +8), stride C0h
// 3150A0..315110 (112 bytes)
constexpr std::uint8_t GemsTable[]{
    0x8D, 0xB7, 0x58, 0x16, 0x00, 0x00, 0x48, 0x8D, 0x44, 0x24, 0x48, 0x48, 0x3B, 0xF0, 0x74, 0x2B,
    0x48, 0x39, 0x5E, 0x10, 0x7C, 0x18, 0x48, 0x8B, 0x3E, 0xE8, 0x02, 0x13, 0xEF, 0x00, 0x48, 0x8B,
    0xD7, 0x48, 0x8B, 0xC8, 0x4C, 0x8B, 0x00, 0x41, 0xFF, 0x50, 0x20, 0x48, 0x89, 0x1E, 0x48, 0x8D,
    0x54, 0x24, 0x48, 0x48, 0x8B, 0xCE, 0xE8, 0x05, 0xBA, 0x00, 0x00, 0x4C, 0x85, 0x74, 0x24, 0x58,
    0x75, 0x19, 0xE8, 0xD9, 0x12, 0xEF, 0x00, 0x48, 0x8B, 0x54, 0x24, 0x48, 0x48, 0x8B, 0xC8, 0x4C,
    0x8B, 0x00, 0x41, 0xFF, 0x50, 0x20, 0x48, 0x89, 0x5C, 0x24, 0x48, 0x48, 0x8B, 0x3E, 0x48, 0x8B,
    0x46, 0x08, 0x4C, 0x8D, 0x34, 0x40, 0x49, 0xC1, 0xE6, 0x06, 0x4C, 0x03, 0xF7, 0x49, 0x3B, 0xFE,
};
constexpr std::uint64_t GemsTableRva = 0x3150A0;

// gems.txt loader descriptors: weaponmod1 code +30h, param +34h, min +38h, max +3Ch (each mod 10h)
// 314A48..314AAD (101 bytes)
constexpr std::uint8_t GemsMods[]{
    0x48, 0x89, 0x5D, 0x48, 0x48, 0xC7, 0x45, 0x58, 0x1C, 0x00, 0x00, 0x00, 0x48, 0xC7, 0x45, 0x60,
    0x30, 0x00, 0x00, 0x00, 0x48, 0x89, 0x55, 0x68, 0x48, 0xC7, 0x45, 0x78, 0x1C, 0x00, 0x00, 0x00,
    0x48, 0xC7, 0x85, 0x80, 0x00, 0x00, 0x00, 0x34, 0x00, 0x00, 0x00, 0x48, 0x89, 0x8D, 0x88, 0x00,
    0x00, 0x00, 0x48, 0xC7, 0x85, 0x98, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x48, 0xC7, 0x85,
    0xA0, 0x00, 0x00, 0x00, 0x38, 0x00, 0x00, 0x00, 0x48, 0x89, 0x9D, 0xA8, 0x00, 0x00, 0x00, 0x48,
    0xC7, 0x85, 0xB8, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x48, 0xC7, 0x85, 0xC0, 0x00, 0x00,
    0x00, 0x3C, 0x00, 0x00, 0x00,
};
constexpr std::uint64_t GemsModsRva = 0x314A48;

// 0x34B4E0, a unit's starting seed (+30h), the one an item saves
// 34B4E0..34B50D (45 bytes)
constexpr std::uint8_t InitSeedGetter[]{
    0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B, 0xD9, 0x48, 0x85, 0xC9, 0x75, 0x1C, 0x88, 0x4C,
    0x24, 0x30, 0x48, 0x8D, 0x4C, 0x24, 0x30, 0xE8, 0x74, 0xB9, 0xFF, 0xFF, 0x84, 0xC0, 0x74, 0x01,
    0xCC, 0x8B, 0x43, 0x30, 0x48, 0x83, 0xC4, 0x20, 0x5B, 0xC3, 0x8B, 0x41, 0x30,
};
constexpr std::uint64_t InitSeedGetterRva = 0x34B4E0;

// 0x36CC10, an item's graphic variant (item data +5Eh)
// 36CC10..36CC7A (106 bytes)
constexpr std::uint8_t VariantGetter[]{
    0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B, 0xD9, 0x48, 0x85, 0xC9, 0x74, 0x0A, 0xE8, 0xAD,
    0xED, 0xFD, 0xFF, 0x83, 0xF8, 0x04, 0x74, 0x19, 0x48, 0x8D, 0x4C, 0x24, 0x30, 0xC6, 0x44, 0x24,
    0x30, 0x00, 0xE8, 0x49, 0xD8, 0xFF, 0xFF, 0x84, 0xC0, 0x74, 0x01, 0xCC, 0x48, 0x85, 0xDB, 0x74,
    0x2D, 0x48, 0x8B, 0xCB, 0xE8, 0x87, 0xED, 0xFD, 0xFF, 0x83, 0xF8, 0x04, 0x75, 0x20, 0x48, 0x8B,
    0xCB, 0xE8, 0xAA, 0xD8, 0xFD, 0xFF, 0x48, 0x85, 0xC0, 0x75, 0x1B, 0x48, 0x8D, 0x4C, 0x24, 0x30,
    0x88, 0x44, 0x24, 0x30, 0xE8, 0x97, 0xBD, 0xFF, 0xFF, 0x84, 0xC0, 0x74, 0x01, 0xCC, 0x32, 0xC0,
    0x48, 0x83, 0xC4, 0x20, 0x5B, 0xC3, 0x0F, 0xB6, 0x40, 0x5E,
};
constexpr std::uint64_t VariantGetterRva = 0x36CC10;

constexpr std::uint64_t ApplyPropertyRva    = 0x3D5060;  // (context, mode, ., item, prop, ., ., ., prop, state, flag, .)
constexpr std::uint64_t VariantRva          = 0x36CC10;  // (item) -> graphic variant
constexpr std::size_t   TablesProperties    = 0x240;
constexpr std::size_t   TablesPropertyCount = 0x248;
constexpr std::size_t   PropertyRecordSize  = 0x30;
constexpr std::size_t   PropertyFuncs       = 0x18;      // byte per func slot
constexpr std::size_t   PropertyStats       = 0x20;      // word per func slot
constexpr std::size_t   TablesGroups        = 0x258;
constexpr std::size_t   TablesGroupCount    = 0x260;
constexpr std::size_t   GroupRecordSize     = 0xC8;
constexpr std::size_t   GroupMembers        = 0x08;
constexpr std::size_t   GroupMemberSize     = 0x18;
constexpr std::size_t   TablesGems          = 0x1658;
constexpr std::size_t   TablesGemCount      = 0x1660;
constexpr std::size_t   GemRecordSize       = 0xC0;
constexpr std::size_t   GemItem             = 0x28;      // "code" as the gem's items.txt row, as D2RCore's LinkGemRows reads it
constexpr std::size_t   GemMods             = 0x30;
constexpr std::size_t   GemModSize          = 0x10;
constexpr std::size_t   UnitInitSeed        = 0x30;
constexpr std::int32_t  ApplyMode           = 7;         // the cube's mode

// Gem Effect, host_uniques (see the header)
// ITEMS_GetItemQuality (item): item data +0, 2 for a unit that is not an item
// 36CF60..36CFD1 (113 bytes)
constexpr std::uint8_t ItemQualityBody[]{
    0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B, 0xD9, 0x48, 0x85, 0xC9, 0x74, 0x0A, 0xE8, 0x5D,
    0xEA, 0xFD, 0xFF, 0x83, 0xF8, 0x04, 0x74, 0x19, 0x48, 0x8D, 0x4C, 0x24, 0x30, 0xC6, 0x44, 0x24,
    0x30, 0x00, 0xE8, 0x09, 0xA6, 0xFF, 0xFF, 0x84, 0xC0, 0x74, 0x01, 0xCC, 0x48, 0x85, 0xDB, 0x74,
    0x2D, 0x48, 0x8B, 0xCB, 0xE8, 0x37, 0xEA, 0xFD, 0xFF, 0x83, 0xF8, 0x04, 0x75, 0x20, 0x48, 0x8B,
    0xCB, 0xE8, 0x5A, 0xD5, 0xFD, 0xFF, 0x48, 0x85, 0xC0, 0x75, 0x1E, 0x48, 0x8D, 0x4C, 0x24, 0x30,
    0x88, 0x44, 0x24, 0x30, 0xE8, 0x87, 0xC6, 0xFF, 0xFF, 0x84, 0xC0, 0x74, 0x01, 0xCC, 0xB8, 0x02,
    0x00, 0x00, 0x00, 0x48, 0x83, 0xC4, 0x20, 0x5B, 0xC3, 0x8B, 0x00, 0x48, 0x83, 0xC4, 0x20, 0x5B,
    0xC3,
};
constexpr std::uint64_t ItemQualityBodyRva = 0x36CF60;

// an item's file index (item data +34h): its uniqueitems.txt row when it is unique
// 36CE70..36CEE7 (119 bytes)
constexpr std::uint8_t ItemFileIndexBody[]{
    0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B, 0xD9, 0x48, 0x85, 0xC9, 0x75, 0x13, 0x88, 0x4C,
    0x24, 0x30, 0x48, 0x8D, 0x4C, 0x24, 0x30, 0xE8, 0xB4, 0xB7, 0xFF, 0xFF, 0x84, 0xC0, 0x74, 0x01,
    0xCC, 0x48, 0x8B, 0xCB, 0xE8, 0x37, 0xEB, 0xFD, 0xFF, 0x83, 0xF8, 0x04, 0x74, 0x14, 0x48, 0x8D,
    0x4C, 0x24, 0x30, 0xC6, 0x44, 0x24, 0x30, 0x00, 0xE8, 0x63, 0xBA, 0xFF, 0xFF, 0x84, 0xC0, 0x74,
    0x01, 0xCC, 0x48, 0x8B, 0xCB, 0xE8, 0x46, 0xD6, 0xFD, 0xFF, 0x48, 0x8B, 0xD8, 0x48, 0x85, 0xC0,
    0x75, 0x1C, 0x48, 0x8D, 0x4C, 0x24, 0x30, 0x88, 0x44, 0x24, 0x30, 0xE8, 0x20, 0xAD, 0xFF, 0xFF,
    0x84, 0xC0, 0x74, 0x01, 0xCC, 0x8B, 0x43, 0x34, 0x48, 0x83, 0xC4, 0x20, 0x5B, 0xC3, 0x8B, 0x40,
    0x34, 0x48, 0x83, 0xC4, 0x20, 0x5B, 0xC3,
};
constexpr std::uint64_t ItemFileIndexBodyRva = 0x36CE70;

// 0x443910, the unique pick, reading uniqueitems.txt: 300A90 (context), rows +13C8h, count +13D0h
// 443A70..443A88 (24 bytes)
constexpr std::uint8_t UniqueTableRead[]{
    0xE8, 0x1B, 0xD0, 0xEB, 0xFF, 0x48, 0x89, 0x44, 0x24, 0x38, 0x48, 0x8B, 0x98, 0xC8, 0x13, 0x00,
    0x00, 0x48, 0x8B, 0x88, 0xD0, 0x13, 0x00, 0x00,
};
constexpr std::uint64_t UniqueTableReadRva = 0x443A70;

// ... and its step to the next row, 15Ch bytes on, while below the count
// 443B7A..443B96 (28 bytes)
constexpr std::uint8_t UniqueTableStep[]{
    0x48, 0x8B, 0x44, 0x24, 0x38, 0x48, 0xFF, 0xC7, 0x48, 0x81, 0xC3, 0x5C, 0x01, 0x00, 0x00, 0x48,
    0x3B, 0xB8, 0xD0, 0x13, 0x00, 0x00, 0x0F, 0x82, 0x1A, 0xFF, 0xFF, 0xFF,
};
constexpr std::uint64_t UniqueTableStepRva = 0x443B7A;

// the uniqueitems.txt compiler 0x31F1D0: column "index", a string (type 1) of 31 at +2
// 31F200..31F22C (44 bytes)
constexpr std::uint8_t UniqueIndexColumn[]{
    0xE8, 0x8B, 0x18, 0xFE, 0xFF, 0x4C, 0x8B, 0xF8, 0xC7, 0x45, 0xB8, 0x01, 0x00, 0x00, 0x00, 0x45,
    0x33, 0xE4, 0xC7, 0x45, 0xBC, 0x1F, 0x00, 0x00, 0x00, 0x48, 0x8D, 0x05, 0x64, 0xC1, 0x9B, 0x01,
    0x48, 0xC7, 0x45, 0xC0, 0x02, 0x00, 0x00, 0x00, 0x48, 0x89, 0x45, 0xB0,
};
constexpr std::uint64_t UniqueIndexColumnRva = 0x31F200;

constexpr std::size_t   TablesUniqueItems     = 0x13C8;
constexpr std::size_t   TablesUniqueItemCount = 0x13D0;
constexpr std::size_t   UniqueItemSize        = 0x15C;   // 348 bytes a row
constexpr std::size_t   UniqueItemIndex       = 0x02;    // the index column
constexpr std::size_t   UniqueIndexLength     = 31;      // the longest index the game keeps
constexpr std::uint64_t UniqueItemLimit       = 0x8000;  // the compiler stops above it
constexpr std::int32_t  QualityUnique         = 7;

// The fields above, as the witnesses encode them.
constexpr auto Le32(const std::uint8_t* bytes, std::size_t at) noexcept -> std::uint32_t {
    return static_cast<std::uint32_t>(bytes[at]) | (static_cast<std::uint32_t>(bytes[at + 1]) << 8)
        | (static_cast<std::uint32_t>(bytes[at + 2]) << 16) | (static_cast<std::uint32_t>(bytes[at + 3]) << 24);
}
constexpr auto CallTarget(std::uint64_t callRva, const std::uint8_t* call) noexcept -> std::uint64_t {
    return callRva + 5 + static_cast<std::uint64_t>(static_cast<std::int64_t>(static_cast<std::int32_t>(Le32(call, 1))));
}
static_assert(CallTarget(UniqueTableReadRva, UniqueTableRead) == GetDataTablesRva);
static_assert(CallTarget(UniqueIndexColumnRva, UniqueIndexColumn) == GetDataTablesRva);
static_assert(Le32(UniqueTableRead, 13) == TablesUniqueItems && Le32(UniqueTableRead, 20) == TablesUniqueItemCount);
static_assert(Le32(UniqueTableStep, 11) == UniqueItemSize && Le32(UniqueTableStep, 18) == TablesUniqueItemCount);
static_assert(Le32(UniqueIndexColumn, 11) == 1 && Le32(UniqueIndexColumn, 21) == UniqueIndexLength
    && Le32(UniqueIndexColumn, 36) == UniqueItemIndex);
static_assert(ItemFileIndexBody[0x65] == 0x8B && ItemFileIndexBody[0x66] == 0x43 && ItemFileIndexBody[0x67] == 0x34
    && ItemFileIndexBody[0x6E] == 0x8B && ItemFileIndexBody[0x6F] == 0x40 && ItemFileIndexBody[0x70] == 0x34);
static_assert(ItemQualityBody[0x5E] == 0xB8 && ItemQualityBody[0x5F] == 0x02 && ItemQualityBody[0x69] == 0x8B
    && ItemQualityBody[0x6A] == 0x00);

// Mirrored Ring, client side (see the header)
// InventorySlotWidget draw 0x2C9940: the paired slot (call 15F220 at 2C99DD), then its item
// 2C99D5..2C99EC (23 bytes)
constexpr std::uint8_t PairWindow[]{
    0x48, 0x8B, 0xF0, 0x48, 0x89, 0x44, 0x24, 0x60, 0xE8, 0x3E, 0x58, 0xE9, 0xFF, 0x8B, 0xD0, 0x48,
    0x8B, 0xCB, 0xE8, 0xE4, 0xEC, 0x0B, 0x00,
};
constexpr std::uint64_t PairWindowRva = 0x2C99D5;

// draw: does the held item fit this slot (call 36BB10 at 2C9BA5)
// 2C9B9C..2C9BB2 (22 bytes)
constexpr std::uint8_t CanEquipWindow[]{
    0x8B, 0x97, 0xD8, 0x05, 0x00, 0x00, 0x49, 0x8B, 0xCC, 0xE8, 0x66, 0x1F, 0x0A, 0x00, 0x85, 0xC0,
    0x0F, 0x84, 0xB4, 0x03, 0x00, 0x00,
};
constexpr std::uint64_t CanEquipWindowRva = 0x2C9B9C;

// draw: an empty slot draws the paired two-handed item as a ghost
// (call GetItemHandednessForUnit at 2C9C06, == 2, then draw 2A72D0)
// 2C9BFA..2C9C16 (28 bytes)
constexpr std::uint8_t HandWindow[]{
    0x48, 0x8B, 0x4C, 0x24, 0x40, 0x44, 0x0F, 0xB6, 0xC3, 0x49, 0x8B, 0xD6, 0xE8, 0xF9, 0x19, 0xB6,
    0x03, 0x83, 0xF8, 0x02, 0x75, 0x4D, 0x49, 0x8B, 0xD6, 0x48, 0x8B, 0xCF,
};
constexpr std::uint64_t HandWindowRva = 0x2C9BFA;

// 0x2AA7E0: call 15C600 at 2AA937
// 2AA925..2AA93C (23 bytes)
constexpr std::uint8_t RequestWindowA[]{
    0x8B, 0x54, 0x24, 0x34, 0x44, 0x8B, 0xCF, 0x41, 0xB0, 0x01, 0x40, 0x88, 0x74, 0x24, 0x20, 0x48,
    0x8B, 0xCB, 0xE8, 0xC4, 0x1C, 0xEB, 0xFF,
};
constexpr std::uint64_t RequestWindowARva = 0x2AA925;

// UI_ShiftRightClickQuickMoveAction 0x2AA9F0: call 15C600 at 2AAA79
// 2AAA67..2AAA7E (23 bytes)
constexpr std::uint8_t RequestWindowB[]{
    0x8B, 0x54, 0x24, 0x30, 0x44, 0x8B, 0xCB, 0x41, 0xB0, 0x01, 0x40, 0x88, 0x74, 0x24, 0x20, 0x48,
    0x8B, 0xCF, 0xE8, 0x82, 0x1B, 0xEB, 0xFF,
};
constexpr std::uint64_t RequestWindowBRva = 0x2AAA67;

// slot widget vtable +D0h 0x2CABA0 (pick up): call 15C600 (0, slot, 0, -1, FFh) at 2CABDC
// 2CABC1..2CABE6 (37 bytes)
constexpr std::uint8_t RequestWindowC[]{
    0x8B, 0x93, 0xD8, 0x05, 0x00, 0x00, 0x45, 0x33, 0xC0, 0x48, 0x3B, 0xF8, 0x75, 0x1D, 0x41, 0xB9,
    0xFF, 0xFF, 0xFF, 0xFF, 0xC6, 0x44, 0x24, 0x20, 0xFF, 0x33, 0xC9, 0xE8, 0x1F, 0x1A, 0xE9, 0xFF,
    0x48, 0x8B, 0x5C, 0x24, 0x40,
};
constexpr std::uint64_t RequestWindowCRva = 0x2CABC1;

// slot widget vtable +D8h 0x2CAC00 (quick move): call 15C600 at 2CAC6C
// 2CAC5A..2CAC71 (23 bytes)
constexpr std::uint8_t RequestWindowD[]{
    0x8B, 0x9B, 0xD8, 0x05, 0x00, 0x00, 0x41, 0xB0, 0x01, 0x40, 0x88, 0x7C, 0x24, 0x20, 0x8B, 0xD3,
    0x33, 0xC9, 0xE8, 0x8F, 0x19, 0xE9, 0xFF,
};
constexpr std::uint64_t RequestWindowDRva = 0x2CAC5A;

// UI_HandleEquippedItemClick 0x2CACF0, held item: call 15C600 (item, slot, 0, -1, 0) at 2CB0AB
// 2CB09A..2CB0B5 (27 bytes)
constexpr std::uint8_t RequestWindowE[]{
    0x41, 0xB9, 0xFF, 0xFF, 0xFF, 0xFF, 0x45, 0x33, 0xC0, 0xC6, 0x44, 0x24, 0x20, 0x00, 0x48, 0x8B,
    0xCD, 0xE8, 0x50, 0x15, 0xE9, 0xFF, 0xE9, 0x69, 0xFD, 0xFF, 0xFF,
};
constexpr std::uint64_t RequestWindowERva = 0x2CB09A;

// 0x15C600 refusing an item that does not fit: 106B70 (player, 14h), the "Impossible" voice
// 15C793..15C7A0 (13 bytes)
constexpr std::uint8_t ImpossibleWindow[]{
    0xBA, 0x14, 0x00, 0x00, 0x00, 0x48, 0x8B, 0xCB, 0xE8, 0xD0, 0xA3, 0xFA, 0xFF,
};
constexpr std::uint64_t ImpossibleWindowRva = 0x15C793;

constexpr std::uint64_t PairFnRva      = 0x15F220;   // paired slot, 4 <-> 5
constexpr std::uint64_t CanEquipFnRva  = 0x36BB10;   // ITEMS_CanEquipAtBodyLocation (item, slot)
constexpr std::uint64_t HandFnRva      = 0x3E2B604;  // GetItemHandednessForUnit thunk (unit, item, flag)
constexpr std::uint64_t RequestFnRva   = 0x15C600;   // body location request (held item, slot, ., unit id, .)
constexpr std::uint64_t PlaySoundFnRva = 0x106B70;   // character voice (unit, message)
constexpr std::uint16_t ImpossibleMessage = 0x14;
constexpr std::size_t   RelaySize      = 16;         // FF 25 00000000, absolute target

// ---------------------------------------------------------------------------
// Native function types
// ---------------------------------------------------------------------------
using ReadStatFn        = std::int32_t (__fastcall*)(void*, std::int32_t, std::int32_t);
using DataContextFn     = std::uint8_t (__fastcall*)(void*);
using DataTablesFn      = std::uint8_t* (__fastcall*)(std::uint8_t);
using CheckItemTypeFn   = std::int32_t (__fastcall*)(void*, std::int32_t);
using ItemsRecordFn     = std::uint8_t* (__fastcall*)(std::uint8_t, std::uint32_t);
using AllocListFn       = void* (__fastcall*)(std::uint32_t, std::int32_t, std::int32_t, std::uint32_t);
using AttachListFn      = void (__fastcall*)(void*, void*, std::int32_t);
using SetListStatFn     = void (__fastcall*)(std::uint8_t, void*, std::int32_t, std::int32_t, std::int32_t);
using UnlinkListFn      = void (__fastcall*)(void*, void*);
using FreeListFn        = void (__fastcall*)(std::uint8_t, void*);
using InventoryFn       = void* (__fastcall*)(void*, const char*, std::int32_t);
using BodyLocItemFn     = void* (__fastcall*)(void*, std::int32_t);
using FirstItemFn       = void* (__fastcall*)(void*);
using NextItemFn        = void* (__fastcall*)(void*);
using GetOwnerFn        = void* (__fastcall*)(void*, std::uint32_t*);
using FindStatListFn    = void* (__fastcall*)(void*, std::int32_t, std::int32_t);
using ServerUnitFn      = void* (__fastcall*)(void*, std::uint32_t, std::uint32_t);
using ClientUnitFn      = void* (__fastcall*)(std::uint32_t, std::uint32_t);
using LocalIndexFn      = std::uint32_t (__fastcall*)();
using PlayerFromIndexFn = void* (__fastcall*)(std::uint32_t);
using ApplyPropertyFn   = std::int64_t (__fastcall*)(std::uint8_t, std::int32_t, std::int32_t, void*, const void*, std::int32_t,
    std::int32_t, std::int32_t, const void*, std::int32_t, std::int32_t, void*);
using VariantFn         = std::uint8_t (__fastcall*)(void*);
using ItemValueFn       = std::int32_t (__fastcall*)(void*);
using TooltipListFn     = void* (__fastcall*)(void*, std::int32_t, std::int32_t);
using PairFn            = std::int32_t (__fastcall*)(std::int32_t);
using CanEquipFn        = std::int32_t (__fastcall*)(void*, std::int32_t);
using HandFn            = std::int32_t (__fastcall*)(void*, void*, std::int32_t);
using RequestFn         = std::int64_t (__fastcall*)(void*, std::uint32_t, std::uint8_t, std::uint32_t, std::uint8_t);
using PlaySoundFn       = std::int64_t (__fastcall*)(void*, std::uint16_t);

// ---------------------------------------------------------------------------
// Configuration
// ---------------------------------------------------------------------------
constexpr std::int32_t DefaultStats[]{ 0, 1, 2, 3, 7, 9, 11, 16, 17, 18, 19, 20, 21, 22, 23, 24, 27, 28, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 48, 49, 50, 51, 52, 53, 54, 55, 57, 58, 60, 62, 67, 68, 74, 76, 77, 78, 79, 80, 83, 85, 86, 87, 89, 93, 96, 97, 99, 102, 105, 107, 109, 110, 111, 112, 114, 116, 119, 120, 121, 122, 123, 124, 126, 127, 128, 134, 135, 136, 138, 141, 142, 143, 144, 145, 146, 147, 148, 149, 150, 151, 155, 156, 158, 159, 160, 182, 187, 188, 189, 190, 191, 192, 193, 195, 196, 197, 198, 200, 201, 214, 216, 217, 218, 219, 220, 221, 222, 223, 224, 225, 226, 227, 228, 229, 230, 231, 232, 233, 238, 239, 240, 242, 244, 247, 250, 255, 256, 257, 258, 265, 266, 267, 287, 288, 303, 329, 330, 331, 332, 333, 334, 335, 336, 338, 339, 340, 358, 366, 371, 372, 373, 374, 376, 377, 378, 379, 391, 392, 393, 394, 398, 400, 404, 405, 409, 413, 415, 422, 423, 426, 434, 435, 436, 466, 467, 482, 483, 486, 489, 493, 495, 496, 498, 499, 500, 501, 502, 505, 506, 516, 519, 520, 522 };

struct PerkConfig {
    bool                      enabled{ true };
    std::int32_t              stat{ -1 };
    std::int32_t              percent{ 0 };
    std::vector<std::string>  types;
    std::vector<std::int32_t> stats;
};

auto DefaultStatList() -> std::vector<std::int32_t> {
    return { std::begin(DefaultStats), std::end(DefaultStats) };
}

struct PrismConfig {
    bool                     enabled{ true };
    std::int32_t             stat{ 969 };
    std::int32_t             perColor{ 6 };
    std::vector<std::string> gemTypes{ "gem" };
    std::vector<std::string> colorTypes{ "gema", "gemt", "gems", "geme", "gemr", "gemd", "gemz", "gemk" };
};

struct MirrorConfig {
    bool                      enabled{ true };
    std::int32_t              stat{ 970 };
    std::int32_t              percent{ 100 };
    std::int32_t              ringSlot{ 6 };
    std::vector<std::string>  types{ "ring" };
    std::vector<std::int32_t> exclude{ 91, 92, 204, 356 };
};

struct KinConfig {
    bool         enabled{ true };
    std::int32_t stat{ 972 };
    std::int32_t perGem{ 6 };
};

struct PrimordialConfig {
    bool                      enabled{ true };
    std::int32_t              stat{ 973 };
    std::vector<std::string>  types{ "jewl" };
    std::vector<std::string>  gems{ "gvb", "gbb", "gyb", "ggb", "grb", "gwb", "gbk", "skb" };
    std::vector<std::int32_t> exclude{ 692 };
};

struct GemwordConfig {
    bool                     enabled{ true };
    std::int32_t             stat{ 975 };
    std::int32_t             percent{ 20 };
    std::vector<std::string> socketTypes{ "gem" };
};

// One [[gem_effect]] block.
struct GemEffectConfig {
    bool                     enabled{ true };
    std::int32_t             stat{ -1 };   // -1: not set, the block is off
    std::vector<std::string> types{ "gem" };
    std::vector<std::string> hosts;        // host_uniques: uniqueitems.txt index names; none: any item
    bool                     ownSockets{}; // own_sockets: each host's own value, for its own sockets
};

// The Gem Effect stats that count, one per stat (BuildEffects). Each block
// naming the stat is one rule; a socketed item takes the stat's value once
// when any of its rules matches it.
struct EffectRule {
    std::vector<std::string> types;
    std::vector<std::string> hosts;
};

struct EffectStat {
    std::int32_t            stat{ -1 };
    std::vector<EffectRule> rules;
    bool                    ownSockets{};  // the value is each host's own, for the items socketed in it
};

struct Config {
    bool                      enabled{ true };
    bool                      diagnostics{ false };
    bool                      tooltips{ true };
    std::vector<std::int32_t> perItemStats{ 16, 17, 18, 75 };
    PerkConfig                amulet{ true, 961, 10, { "amul" }, DefaultStatList() };
    PerkConfig                ring{ true, 962, 5, { "ring" }, DefaultStatList() };
    PerkConfig                socketed{ true, 963, 5, { "gem", "jewl" }, DefaultStatList() };
    PrismConfig               prism{};
    MirrorConfig              mirror{};
    KinConfig                 kin{};
    PrimordialConfig          primordial{};
    GemwordConfig             gemword{};
    std::vector<GemEffectConfig> effects;  // one per [[gem_effect]] block, as written
};

// ---------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------
const D2RL::PluginContext* Context{};
std::uintptr_t             Base{};
Config                     Settings{};
std::string                ConfigProblems;
std::string                InstallProblem;

ReadStatFn        ReadOwnStat{};
ReadStatFn        ReadUnitStat{};
DataContextFn     GetDataContext{};
DataTablesFn      GetDataTables{};
CheckItemTypeFn   CheckItemType{};
ItemsRecordFn     ItemsRecord{};
AllocListFn       AllocStatList{};
AttachListFn      AttachStatList{};
SetListStatFn     SetListStat{};
UnlinkListFn      UnlinkStatList{};
FreeListFn        FreeStatList{};
InventoryFn       GetInventory{};
BodyLocItemFn     BodyLocItem{};
FirstItemFn       FirstItem{};
NextItemFn        NextItem{};
GetOwnerFn        GetOwner{};
FindStatListFn    FindStatList{};
ServerUnitFn      ServerUnit{};
ClientUnitFn      ClientUnit{};
LocalIndexFn      LocalPlayerIndex{};
PlayerFromIndexFn PlayerFromIndex{};
const D2RL::ThreadService*    ThreadSvc{};
const D2RL::LifecycleService* LifecycleSvc{};
bool                          Ready{};
bool                          FullStatsLayout{};

std::atomic<std::uint64_t> BonusListsAdded{};
std::atomic<std::uint64_t> BonusListsRemoved{};
std::atomic<std::uint64_t> BonusValuesWritten{};
std::atomic<std::uint64_t> HostPasses{};
std::atomic<std::uint64_t> ClientPasses{};
std::atomic<std::uint32_t> PooledGems{};
std::atomic<std::uint32_t> PooledRings{};
std::atomic<std::uint32_t> GemColors{};
std::atomic<bool>          MirrorActive{};
std::atomic<std::uint64_t> PrimordialApplied{};
std::atomic<std::uint64_t> PrimordialRemoved{};
std::atomic<std::uint32_t> PrimordialJewelsNow{};
std::atomic<std::uint32_t> PrimordialCarriers{};
std::atomic<std::int32_t>  PrimordialContext{ -1 };
bool                       PrimordialReady{};
std::string                PrimordialProblem;
ApplyPropertyFn            ApplyProperty{};
TooltipListFn              TooltipList{};
std::string                TooltipProblem;
std::atomic<std::uint32_t> MergedItemsNow{};
VariantFn                  VariantOf{};
std::atomic<std::uint32_t> Gemwords{};
std::vector<EffectStat>    EffectStats;  // the Gem Effect stats that count, one per stat (BuildEffects)
ItemValueFn                ItemQuality{};
ItemValueFn                ItemFileIndex{};
bool                       UniqueHostsReady{};
std::string                UniqueHostsProblem;
std::atomic<std::uint32_t> UnknownUniqueNames{};
std::atomic<std::int32_t>  EffectContext{ -1 };  // the client pass's data context, for the console
std::atomic<std::uint64_t> MirrorPickups{};
std::atomic<std::uint64_t> MirrorSwaps{};
std::atomic<std::uint64_t> MirrorRefusals{};
std::atomic<std::uint32_t> DiagnosticLines{};

template <typename T>
auto Read(const void* base, std::size_t offset) noexcept -> T {
    T value{};
    std::memcpy(&value, static_cast<const std::uint8_t*>(base) + offset, sizeof(T));
    return value;
}

template <typename T>
auto At(std::uint64_t rva) noexcept -> T {
    return reinterpret_cast<T>(Base + rva);
}

void Diagnostic(const char* format, ...) noexcept {
    if (!Settings.diagnostics || Context == nullptr || DiagnosticLines.fetch_add(1) >= 200) return;
    char text[768];
    va_list args;
    va_start(args, format);
    std::vsnprintf(text, sizeof(text), format, args);
    va_end(args);
    Context->LogInfo(text);
}

// ---------------------------------------------------------------------------
// TOML subset: [section], key = integer | true | false | [ints] | ["text", ...]
// ---------------------------------------------------------------------------
void Problem(std::string_view text) {
    if (!ConfigProblems.empty()) ConfigProblems += "; ";
    ConfigProblems.append(text);
}

auto Trim(std::string_view value) noexcept -> std::string_view {
    std::size_t first = 0;
    while (first < value.size() && (value[first] == ' ' || value[first] == '\t' || value[first] == '\r' || value[first] == '\n')) ++first;
    std::size_t last = value.size();
    while (last > first && (value[last - 1] == ' ' || value[last - 1] == '\t' || value[last - 1] == '\r' || value[last - 1] == '\n')) --last;
    return value.substr(first, last - first);
}

auto StripComment(std::string_view line) noexcept -> std::string_view {
    bool quoted = false;
    for (std::size_t i = 0; i < line.size(); ++i) {
        if (line[i] == '"') quoted = !quoted;
        if (line[i] == '#' && !quoted) return line.substr(0, i);
    }
    return line;
}

auto ParseBool(std::string_view value, bool& out) noexcept -> bool {
    if (value == "true") { out = true; return true; }
    if (value == "false") { out = false; return true; }
    return false;
}

auto ParseInt(std::string_view value, std::int32_t& out) noexcept -> bool {
    if (value.empty()) return false;
    bool negative = false;
    std::size_t index = 0;
    if (value[0] == '-' || value[0] == '+') { negative = value[0] == '-'; index = 1; }
    if (index >= value.size()) return false;
    std::int64_t total = 0;
    for (; index < value.size(); ++index) {
        const char c = value[index];
        if (c == '_') continue;
        if (c < '0' || c > '9') return false;
        total = total * 10 + (c - '0');
        if (total > 1'000'000'000) return false;
    }
    out = static_cast<std::int32_t>(negative ? -total : total);
    return true;
}

auto ParseInts(std::string_view value, std::vector<std::int32_t>& out) -> bool {
    if (value.size() < 2 || value.front() != '[' || value.back() != ']') return false;
    std::vector<std::int32_t> parsed;
    std::string_view rest = Trim(value.substr(1, value.size() - 2));
    while (!rest.empty()) {
        const std::size_t comma = rest.find(',');
        std::int32_t number = 0;
        if (!ParseInt(Trim(rest.substr(0, comma)), number)) return false;
        parsed.push_back(number);
        if (comma == std::string_view::npos) break;
        rest = Trim(rest.substr(comma + 1));
    }
    out = std::move(parsed);
    return true;
}

auto ParseStrings(std::string_view value, std::vector<std::string>& out) -> bool {
    if (value.size() < 2 || value.front() != '[' || value.back() != ']') return false;
    std::vector<std::string> parsed;
    std::string_view rest = Trim(value.substr(1, value.size() - 2));
    while (!rest.empty()) {
        if (rest.front() != '"') return false;
        const std::size_t close = rest.find('"', 1);
        if (close == std::string_view::npos) return false;
        parsed.emplace_back(rest.substr(1, close - 1));
        rest = Trim(rest.substr(close + 1));
        if (!rest.empty()) {
            if (rest.front() != ',') return false;
            rest = Trim(rest.substr(1));
        }
    }
    out = std::move(parsed);
    return true;
}

void ApplyPerkKey(PerkConfig& perk, std::string_view section, std::string_view key, std::string_view value) {
    bool ok = true;
    if (key == "enabled") ok = ParseBool(value, perk.enabled);
    else if (key == "perk_stat") ok = ParseInt(value, perk.stat);
    else if (key == "bonus_percent") ok = ParseInt(value, perk.percent);
    else if (key == "item_types") ok = ParseStrings(value, perk.types);
    else if (key == "stats") ok = ParseInts(value, perk.stats);
    if (!ok) Problem(std::string(section) + "." + std::string(key) + " has an invalid value");
}

// The Gem Effect stats that count: the blocks switched on with a stat set,
// one entry per stat. Every block of a stat is one of its rules, so a stat
// named in several blocks counts once per socketed item, when any of its
// blocks matches the item and the item it is socketed in.
void BuildEffects() {
    EffectStats.clear();
    for (std::size_t i = 0; i < Settings.effects.size(); ++i) {
        const GemEffectConfig& effect = Settings.effects[i];
        if (!effect.enabled || effect.stat < 0) continue;
        auto same = std::find_if(EffectStats.begin(), EffectStats.end(),
            [&effect](const EffectStat& known) noexcept { return known.stat == effect.stat; });
        if (same == EffectStats.end()) {
            EffectStats.push_back({ effect.stat, {}, effect.ownSockets });
            same = EffectStats.end() - 1;
        } else if (same->ownSockets != effect.ownSockets) {
            Problem("gem_effect block " + std::to_string(i + 1) + ": stat " + std::to_string(effect.stat)
                + " is already named with own_sockets = " + (same->ownSockets ? "true" : "false") + "; one stat has one meaning, so this block takes it too");
        }
        same->rules.push_back({ effect.types, effect.hosts });
    }
}

void ParseConfig(std::string_view text) {
    std::string section;
    std::size_t cursor = 0;
    while (cursor < text.size()) {
        std::size_t end = text.find('\n', cursor);
        if (end == std::string_view::npos) end = text.size();
        const std::string_view line = Trim(StripComment(text.substr(cursor, end - cursor)));
        cursor = end + 1;
        if (line.empty()) continue;
        if (line.front() == '[') {
            // [name], or [[name]]: one more block of an array of tables.
            const std::size_t open = line.size() > 1 && line[1] == '[' ? 2 : 1;
            const std::size_t close = line.find(']', open);
            section = std::string(Trim(line.substr(open, close == std::string_view::npos ? 0 : close - open)));
            // Every [[gem_effect]] block is one more Gem Effect stat. A single
            // [gem_effect] table (the 1.5.0 form) reads as one block too.
            if (section == "gem_effect") Settings.effects.emplace_back();
            continue;
        }
        const std::size_t equals = line.find('=');
        if (equals == std::string_view::npos) continue;
        const std::string_view key = Trim(line.substr(0, equals));
        std::string joined(Trim(line.substr(equals + 1)));
        // An array may continue over several lines, each with its own comment.
        while (!joined.empty() && joined.front() == '[' && joined.find(']') == std::string::npos && cursor < text.size()) {
            std::size_t next = text.find('\n', cursor);
            if (next == std::string_view::npos) next = text.size();
            joined += ' ';
            joined += std::string(Trim(StripComment(text.substr(cursor, next - cursor))));
            cursor = next + 1;
        }
        // A trailing comma before the closing bracket is allowed.
        if (joined.size() >= 2 && joined.back() == ']') {
            std::size_t last = joined.size() - 1;
            while (last > 0 && (joined[last - 1] == ' ' || joined[last - 1] == '\t')) --last;
            if (last > 0 && joined[last - 1] == ',') joined.erase(last - 1, 1);
        }
        const std::string_view value = joined;
        if (section == "jeweler") {
            bool ok = true;
            if (key == "enabled") ok = ParseBool(value, Settings.enabled);
            else if (key == "diagnostics") ok = ParseBool(value, Settings.diagnostics);
            else if (key == "show_in_tooltips") ok = ParseBool(value, Settings.tooltips);
            else if (key == "per_item_stats") ok = ParseInts(value, Settings.perItemStats);
            if (!ok) Problem("jeweler." + std::string(key) + " has an invalid value");
        } else if (section == "amulet_modifiers") {
            ApplyPerkKey(Settings.amulet, section, key, value);
        } else if (section == "ring_modifiers") {
            ApplyPerkKey(Settings.ring, section, key, value);
        } else if (section == "socketed_gems") {
            ApplyPerkKey(Settings.socketed, section, key, value);
        } else if (section == "prismatic_resonance") {
            bool ok = true;
            if (key == "enabled") ok = ParseBool(value, Settings.prism.enabled);
            else if (key == "perk_stat") ok = ParseInt(value, Settings.prism.stat);
            else if (key == "percent_per_color") ok = ParseInt(value, Settings.prism.perColor);
            else if (key == "gem_types") ok = ParseStrings(value, Settings.prism.gemTypes);
            else if (key == "color_types") ok = ParseStrings(value, Settings.prism.colorTypes);
            if (!ok) Problem("prismatic_resonance." + std::string(key) + " has an invalid value");
        } else if (section == "mirrored_ring") {
            bool ok = true;
            if (key == "enabled") ok = ParseBool(value, Settings.mirror.enabled);
            else if (key == "perk_stat") ok = ParseInt(value, Settings.mirror.stat);
            else if (key == "bonus_percent") ok = ParseInt(value, Settings.mirror.percent);
            else if (key == "ring_slot") ok = ParseInt(value, Settings.mirror.ringSlot);
            else if (key == "item_types") ok = ParseStrings(value, Settings.mirror.types);
            else if (key == "exclude_stats") ok = ParseInts(value, Settings.mirror.exclude);
            if (!ok) Problem("mirrored_ring." + std::string(key) + " has an invalid value");
        } else if (section == "kindred_gems") {
            bool ok = true;
            if (key == "enabled") ok = ParseBool(value, Settings.kin.enabled);
            else if (key == "perk_stat") ok = ParseInt(value, Settings.kin.stat);
            else if (key == "percent_per_gem") ok = ParseInt(value, Settings.kin.perGem);
            if (!ok) Problem("kindred_gems." + std::string(key) + " has an invalid value");
        } else if (section == "primordial_jewels") {
            bool ok = true;
            if (key == "enabled") ok = ParseBool(value, Settings.primordial.enabled);
            else if (key == "perk_stat") ok = ParseInt(value, Settings.primordial.stat);
            else if (key == "item_types") ok = ParseStrings(value, Settings.primordial.types);
            else if (key == "gems") ok = ParseStrings(value, Settings.primordial.gems);
            else if (key == "exclude_stats") ok = ParseInts(value, Settings.primordial.exclude);
            if (!ok) Problem("primordial_jewels." + std::string(key) + " has an invalid value");
        } else if (section == "steady_pliers") {
            bool ok = true;
            if (key == "enabled") ok = ParseBool(value, Settings.gemword.enabled);
            else if (key == "perk_stat") ok = ParseInt(value, Settings.gemword.stat);
            else if (key == "bonus_percent") ok = ParseInt(value, Settings.gemword.percent);
            else if (key == "socket_types") ok = ParseStrings(value, Settings.gemword.socketTypes);
            if (!ok) Problem("steady_pliers." + std::string(key) + " has an invalid value");
        } else if (section == "gem_effect" && !Settings.effects.empty()) {
            GemEffectConfig& effect = Settings.effects.back();
            bool ok = true;
            if (key == "enabled") ok = ParseBool(value, effect.enabled);
            else if (key == "stat_id") ok = ParseInt(value, effect.stat);
            else if (key == "item_types") ok = ParseStrings(value, effect.types);
            else if (key == "host_uniques") ok = ParseStrings(value, effect.hosts);
            else if (key == "own_sockets") ok = ParseBool(value, effect.ownSockets);
            if (!ok) Problem("gem_effect block " + std::to_string(Settings.effects.size()) + ": " + std::string(key) + " has an invalid value");
        }
    }
    for (PerkConfig* perk : { &Settings.amulet, &Settings.ring, &Settings.socketed }) {
        perk->percent = std::clamp(perk->percent, 0, 1000);
    }
    Settings.prism.perColor = std::clamp(Settings.prism.perColor, 0, 1000);
    Settings.mirror.percent = std::clamp(Settings.mirror.percent, 0, 1000);
    Settings.kin.perGem = std::clamp(Settings.kin.perGem, 0, 1000);
    if (Settings.primordial.gems.size() > 8) Settings.primordial.gems.resize(8);
    Settings.gemword.percent = std::clamp(Settings.gemword.percent, 0, 1000);
    if (Settings.mirror.ringSlot != 6 && Settings.mirror.ringSlot != 7) {
        Problem("mirrored_ring.ring_slot must be 6 or 7");
        Settings.mirror.ringSlot = 6;
    }
    if (Settings.prism.colorTypes.size() > 32) Settings.prism.colorTypes.resize(32);
    for (std::size_t i = 0; i < Settings.effects.size(); ++i) {
        GemEffectConfig& effect = Settings.effects[i];
        if (effect.stat < -1 || effect.stat >= MaxStatId) {
            Problem("gem_effect block " + std::to_string(i + 1) + ": stat_id must be -1 or 0 to 32767");
            effect.stat = -1;
        }
        // A name no row can have stays in the list, so the block still matches no other item.
        for (const std::string& name : effect.hosts) {
            if (name.empty() || name.size() > UniqueIndexLength) {
                Problem("gem_effect block " + std::to_string(i + 1) + ": host_uniques \"" + name + "\" "
                    + (name.empty() ? "is empty" : "is longer than the 31 characters uniqueitems.txt keeps for an index")
                    + ", so no unique matches it");
            }
        }
    }
    BuildEffects();
}

void ReadConfiguration() {
    if (!Context->EnsureConfig(DefaultConfigToml)) {
        Context->LogWarn("Jeweler: the config file could not be created; using the defaults.");
        return;
    }
    std::uint32_t required = 0;
    std::string buffer(131072, '\0');
    if (!Context->ReadConfig(buffer.data(), static_cast<std::uint32_t>(buffer.size()), &required)) {
        Context->LogWarn("Jeweler: the config file could not be read; using the defaults.");
        return;
    }
    buffer.resize(std::strlen(buffer.c_str()));
    ParseConfig(buffer);
    if (!ConfigProblems.empty()) D2RL::LogWarnF(Context, "Jeweler: config: %s.", ConfigProblems.c_str());
}

// ---------------------------------------------------------------------------
// Stat sets: one mask byte per stat id
// ---------------------------------------------------------------------------
constexpr std::uint8_t MaskAmulet   = 0x01;
constexpr std::uint8_t MaskRing     = 0x02;
constexpr std::uint8_t MaskSocketed = 0x04;
constexpr std::uint8_t MaskPerItem  = 0x08;
constexpr std::uint8_t MaskEffect   = 0x10;  // a Gem Effect stat

std::array<std::uint8_t, MaxStatId> StatMask{};
std::array<std::size_t, 3>          StatCounts{};

void BuildMasks() noexcept {
    StatMask.fill(0);
    const std::array<std::pair<const PerkConfig*, std::uint8_t>, 3> perks{ { { &Settings.amulet, MaskAmulet },
        { &Settings.ring, MaskRing }, { &Settings.socketed, MaskSocketed } } };
    for (std::size_t p = 0; p < perks.size(); ++p) {
        StatCounts[p] = 0;
        for (const std::int32_t stat : perks[p].first->stats) {
            if (stat == ChargedSkill) {
                if (Context != nullptr) {
                    Context->LogWarn("Jeweler: a stats list names 204 (item_charged_skill); its value packs charges and "
                        "maximum charges, so it is not raised.");
                }
                continue;
            }
            if (stat < 0 || stat >= MaxStatId) continue;
            if ((StatMask[static_cast<std::size_t>(stat)] & perks[p].second) == 0) ++StatCounts[p];
            StatMask[static_cast<std::size_t>(stat)] |= perks[p].second;
        }
    }
    for (const std::int32_t stat : Settings.perItemStats) {
        if (stat >= 0 && stat < MaxStatId) StatMask[static_cast<std::size_t>(stat)] |= MaskPerItem;
    }
    for (const EffectStat& effect : EffectStats) {
        if (effect.stat >= 0 && effect.stat < MaxStatId) StatMask[static_cast<std::size_t>(effect.stat)] |= MaskEffect;
    }
}

auto MaskOf(std::int32_t stat) noexcept -> std::uint8_t {
    return stat >= 0 && stat < MaxStatId ? StatMask[static_cast<std::size_t>(stat)] : 0;
}

// ---------------------------------------------------------------------------
// Units, item types
// ---------------------------------------------------------------------------
auto UnitType(void* unit) noexcept -> std::uint32_t {
    return unit != nullptr ? Read<std::uint32_t>(unit, 0) : 6;
}

auto StatRows(void* unit) noexcept -> std::uint64_t {
    const std::uint8_t context = GetDataContext(unit);
    if (context >= 4) return 0;
    const std::uint8_t* tables = GetDataTables(context);
    return tables != nullptr ? Read<std::uint64_t>(tables, TablesStatCount) : 0;
}

auto HasPerk(void* player, const PerkConfig& perk) noexcept -> bool {
    return perk.enabled && perk.percent != 0 && !perk.stats.empty() && player != nullptr && perk.stat >= 0
        && static_cast<std::uint64_t>(perk.stat) < StatRows(player) && ReadOwnStat(player, perk.stat, 0) > 0;
}

auto HasPrism(void* player) noexcept -> bool {
    const PrismConfig& prism = Settings.prism;
    return prism.enabled && prism.perColor != 0 && !Settings.socketed.stats.empty() && player != nullptr && prism.stat >= 0
        && static_cast<std::uint64_t>(prism.stat) < StatRows(player) && ReadOwnStat(player, prism.stat, 0) > 0;
}

auto HasStatPerk(void* player, bool enabled, std::int32_t stat) noexcept -> bool {
    return enabled && player != nullptr && stat >= 0 && static_cast<std::uint64_t>(stat) < StatRows(player)
        && ReadOwnStat(player, stat, 0) > 0;
}

// A Gem Effect stat's value on the character, as stat('x'.accr) reads it
// (0x2F5020), in percent.
auto EffectValue(void* player, std::int32_t stat) noexcept -> std::int32_t {
    if (stat < 0 || Settings.socketed.stats.empty() || player == nullptr || static_cast<std::uint64_t>(stat) >= StatRows(player)) {
        return 0;
    }
    return ReadUnitStat(player, stat, 0);
}

// A gem's percent in the gem pool: never below -100 (a gem lowered past
// nothing gives nothing), and small enough that modifier * percent * copies,
// summed over every socketed item, stays far inside 64 bits.
auto GemPercent(std::int64_t percent) noexcept -> std::int32_t {
    return static_cast<std::int32_t>(std::clamp<std::int64_t>(percent, -100, 1'000'000));
}

auto AllowedSlot() noexcept -> std::int32_t { return Settings.mirror.ringSlot; }
auto BlockedSlot() noexcept -> std::int32_t { return Settings.mirror.ringSlot == 6 ? 7 : 6; }

// One Gem Effect rule, resolved against one data context's tables.
struct EffectRuleIds {
    std::vector<std::int32_t> types;
    std::vector<std::int32_t> hosts;    // uniqueitems.txt rows of its host_uniques
    bool                      anyHost;  // the block names no unique: any item
};

struct TypeIds {
    const std::uint8_t*       tables{};
    std::vector<std::int32_t> amulet;
    std::vector<std::int32_t> ring;
    std::vector<std::int32_t> socketed;
    std::vector<std::int32_t> gems;
    std::vector<std::int32_t> colors;
    std::vector<std::int32_t> mirror;
    std::vector<std::int32_t> gemwordSockets;
    std::vector<std::int32_t> primordial;
    std::vector<std::vector<EffectRuleIds>> effects;  // per EffectStats entry, one per rule
    std::vector<std::pair<std::string, std::int32_t>> uniques;  // every host_uniques name met, and its row (-1: none)
};

std::mutex                 TypeMutex;
std::array<TypeIds, 4>     TypeCache{};
std::atomic<std::uint32_t> UnknownTypeCodes{};

auto CodeMatches(const std::uint8_t* row, const std::string& code) noexcept -> bool {
    if (code.empty() || code.size() > 4) return false;
    for (std::size_t i = 0; i < 4; ++i) {
        const char want = i < code.size() ? code[i] : ' ';
        const char have = static_cast<char>(row[i]);
        if (i < code.size() ? (have != want) : (have != ' ' && have != '\0')) return false;
    }
    return true;
}

auto ResolveCodes(const std::uint8_t* tables, const std::vector<std::string>& codes) -> std::vector<std::int32_t> {
    std::vector<std::int32_t> ids;
    const auto* rows = Read<const std::uint8_t*>(tables, TablesItemTypes);
    const auto count = Read<std::uint64_t>(tables, TablesItemTypeCount);
    if (rows == nullptr || count == 0 || count > 4096) return ids;
    for (const std::string& code : codes) {
        bool found = false;
        for (std::uint64_t i = 0; i < count; ++i) {
            if (CodeMatches(rows + i * ItemTypeStride, code)) {
                ids.push_back(static_cast<std::int32_t>(i));
                found = true;
                break;
            }
        }
        if (!found && UnknownTypeCodes.fetch_add(1) < 16) {
            D2RL::LogWarnF(Context, "Jeweler: item type code \"%s\" is not in itemtypes.txt; it is ignored.", code.c_str());
        }
    }
    return ids;
}

auto EqualsIgnoringCase(const char* left, const std::string& right) noexcept -> bool {
    for (std::size_t i = 0; i < right.size(); ++i) {
        char a = left[i];
        char b = right[i];
        if (a >= 'A' && a <= 'Z') a = static_cast<char>(a - 'A' + 'a');
        if (b >= 'A' && b <= 'Z') b = static_cast<char>(b - 'A' + 'a');
        if (a != b) return false;
    }
    return true;
}

// A uniqueitems.txt index in these tables: the name exactly as written, else
// one that differs only in letter case (the log says so). -1 when no row has
// it, or when the table's layout could not be verified.
auto FindUniqueRow(const std::uint8_t* tables, const std::string& name) -> std::int32_t {
    if (!UniqueHostsReady || tables == nullptr || name.empty() || name.size() > UniqueIndexLength) return -1;
    const auto* rows  = Read<const std::uint8_t*>(tables, TablesUniqueItems);
    const auto  count = Read<std::uint64_t>(tables, TablesUniqueItemCount);
    if (rows == nullptr || count == 0 || count > UniqueItemLimit) return -1;
    std::int64_t caseOnly = -1;
    for (std::uint64_t row = 0; row < count; ++row) {
        const auto* index = reinterpret_cast<const char*>(rows + row * UniqueItemSize + UniqueItemIndex);
        std::size_t length = 0;
        while (length <= UniqueIndexLength && index[length] != '\0') ++length;
        if (length != name.size()) continue;
        if (std::memcmp(index, name.data(), length) == 0) return static_cast<std::int32_t>(row);
        if (caseOnly < 0 && EqualsIgnoringCase(index, name)) caseOnly = static_cast<std::int64_t>(row);
    }
    if (caseOnly >= 0 && Context != nullptr) {
        D2RL::LogWarnF(Context, "Jeweler: host_uniques \"%s\" matches uniqueitems.txt row %lld only by letter case.", name.c_str(),
            static_cast<long long>(caseOnly));
    }
    return static_cast<std::int32_t>(caseOnly);
}

// A host_uniques name's row, looked up once per table (TypeMutex held).
auto UniqueRowFor(TypeIds& cache, const std::string& name) -> std::int32_t {
    for (const auto& [known, row] : cache.uniques) {
        if (known == name) return row;
    }
    const std::int32_t row = FindUniqueRow(cache.tables, name);
    cache.uniques.emplace_back(name, row);
    if (row < 0 && UniqueHostsReady && !name.empty() && name.size() <= UniqueIndexLength && Context != nullptr
            && UnknownUniqueNames.fetch_add(1) < 16) {
        D2RL::LogWarnF(Context, "Jeweler: host_uniques \"%s\" is not an index in uniqueitems.txt; it matches nothing.", name.c_str());
    }
    return row;
}

auto Types(std::uint8_t context) -> const TypeIds* {
    if (context >= TypeCache.size()) return nullptr;
    const std::uint8_t* tables = GetDataTables(context);
    if (tables == nullptr) return nullptr;
    std::lock_guard lock(TypeMutex);
    TypeIds& cache = TypeCache[context];
    if (cache.tables != tables) {
        cache.tables   = tables;
        cache.amulet   = ResolveCodes(tables, Settings.amulet.types);
        cache.ring     = ResolveCodes(tables, Settings.ring.types);
        cache.socketed = ResolveCodes(tables, Settings.socketed.types);
        cache.gems     = ResolveCodes(tables, Settings.prism.gemTypes);
        cache.colors   = ResolveCodes(tables, Settings.prism.colorTypes);
        cache.mirror   = ResolveCodes(tables, Settings.mirror.types);
        cache.gemwordSockets = ResolveCodes(tables, Settings.gemword.socketTypes);
        cache.primordial = ResolveCodes(tables, Settings.primordial.types);
        cache.effects.clear();
        cache.uniques.clear();
        for (const EffectStat& effect : EffectStats) {
            std::vector<EffectRuleIds> rules;
            for (const EffectRule& rule : effect.rules) {
                EffectRuleIds ids{ ResolveCodes(tables, rule.types), {}, rule.hosts.empty() };
                for (const std::string& name : rule.hosts) {
                    const std::int32_t row = UniqueRowFor(cache, name);
                    if (row >= 0 && std::find(ids.hosts.begin(), ids.hosts.end(), row) == ids.hosts.end()) ids.hosts.push_back(row);
                }
                rules.push_back(std::move(ids));
            }
            cache.effects.push_back(std::move(rules));
        }
    }
    return &cache;
}

auto ItemMatches(void* item, const std::vector<std::int32_t>& types) noexcept -> bool {
    for (const std::int32_t type : types) {
        if (CheckItemType(item, type) != 0) return true;
    }
    return false;
}

// An item's uniqueitems.txt row when it is a unique, else -1.
auto UniqueRowOf(void* item) noexcept -> std::int32_t {
    if (!UniqueHostsReady || item == nullptr || UnitType(item) != UnitItem || ItemQuality(item) != QualityUnique) return -1;
    return ItemFileIndex(item);
}

// A socketed item takes a Gem Effect stat when one of the stat's rules
// matches its types and, for a rule that names uniques, its host is one of
// them (hostUnique: the host's row, -1 when it is no unique).
auto EffectMatches(void* child, std::int32_t hostUnique, const std::vector<EffectRuleIds>& rules) noexcept -> bool {
    for (const EffectRuleIds& rule : rules) {
        if (!rule.anyHost && (hostUnique < 0 || std::find(rule.hosts.begin(), rule.hosts.end(), hostUnique) == rule.hosts.end())) continue;
        if (ItemMatches(child, rule.types)) return true;
    }
    return false;
}

// The unit an item's stat list is merged into (the character for counting
// gear and charms, the host for a socketed item), or null.
auto MergedInto(void* item) noexcept -> void* {
    std::uint32_t mode = 0;
    return GetOwner(item, &mode);
}

// ---------------------------------------------------------------------------
// Stat values
// ---------------------------------------------------------------------------
struct StatKey {
    std::int32_t stat;
    std::int32_t layer;
};

struct StatValue {
    std::int32_t stat;
    std::int32_t layer;
    std::int32_t value;
};

void AddValue(std::vector<StatValue>& out, std::int32_t stat, std::int32_t layer, std::int64_t value) noexcept {
    if (value == 0) return;
    for (StatValue& v : out) {
        if (v.stat == stat && v.layer == layer) {
            v.value += static_cast<std::int32_t>(value);
            return;
        }
    }
    out.push_back({ stat, layer, static_cast<std::int32_t>(value) });
}

auto Lookup(const std::vector<StatValue>& values, std::int32_t stat, std::int32_t layer) noexcept -> std::int32_t {
    for (const StatValue& v : values) {
        if (v.stat == stat && v.layer == layer) return v.value;
    }
    return 0;
}

auto ExOf(void* unit) noexcept -> const std::uint8_t* {
    const auto* ex = Read<const std::uint8_t*>(unit, UnitStatListEx);
    return ex != nullptr && Read<std::int32_t>(ex, ListFlags) < 0 ? ex : nullptr;
}

// A list's stats added into out: a plain list's own (+30h, count +38h), or an
// extended list's full stats (+A8h, count +B0h).
void AddListEntries(const std::uint8_t* list, std::size_t at, std::vector<StatValue>& out) noexcept {
    const auto* entries = Read<const std::uint8_t*>(list, at);
    const auto  count   = Read<std::uint64_t>(list, at + 8);
    if (entries == nullptr || count > 8192) return;
    for (std::uint64_t i = 0; i < count; ++i) {
        const std::uint8_t* entry = entries + i * StatEntrySize;
        AddValue(out, Read<std::uint16_t>(entry, StatEntryStat),
            static_cast<std::int32_t>(Read<std::uint32_t>(entry, StatEntryLayer)), Read<std::int32_t>(entry, StatEntryValue));
    }
}

// What an item's stat lists hold, per (stat, layer) pair (see AN ITEM'S
// MODIFIERS): every list in its active child chain (+90h, next at +68h), and
// for an item socketed in it (an extended list in the chain) the full stats the
// engine itself adds into the host. Never the item's base values, never what
// the engine derives through an op on the item, never a total it stopped
// keeping current.
void ListTotals(void* item, std::vector<StatValue>& out) noexcept {
    out.clear();
    const std::uint8_t* ex = ExOf(item);
    if (ex == nullptr) return;
    const auto* node = Read<const std::uint8_t*>(ex, ExChildren);
    for (std::size_t guard = 0; node != nullptr && guard < MaxChainNodes; ++guard) {
        AddListEntries(node, Read<std::int32_t>(node, ListFlags) < 0 ? ExFullStats : ListStats, out);
        node = Read<const std::uint8_t*>(node, ListNext);
    }
}

// The pairs of an item's list totals whose stat is in one of the mask's sets.
void ItemPairs(const std::vector<StatValue>& totals, std::uint8_t mask, std::vector<StatKey>& out) noexcept {
    out.clear();
    for (const StatValue& v : totals) {
        if ((MaskOf(v.stat) & mask) != 0) out.push_back({ v.stat, v.layer });
    }
}

// Every pair of an item's list totals but the Mirrored Ring exclusions.
void AllPairs(const std::vector<StatValue>& totals, std::vector<StatKey>& out) {
    out.clear();
    for (const StatValue& v : totals) {
        if (std::find(Settings.mirror.exclude.begin(), Settings.mirror.exclude.end(), v.stat) != Settings.mirror.exclude.end()) continue;
        out.push_back({ v.stat, v.layer });
    }
}

// A pair's modifier value on an item: its list total without the plugin bonus
// lists (and on the client without what this plugin merged into the item's
// magic list), both as PluginListValues reads them.
auto OwnValue(const std::vector<StatValue>& totals, const StatKey& key, const std::vector<StatValue>& plugin) noexcept -> std::int64_t {
    return static_cast<std::int64_t>(Lookup(totals, key.stat, key.layer)) - Lookup(plugin, key.stat, key.layer);
}

// An own_sockets Gem Effect stat's value on a host: its own value, read the
// way a modifier is (list total less every plugin bonus list), so the items
// socketed in it count and no plugin bonus does.
auto HostEffectValue(const std::vector<StatValue>& totals, std::int32_t stat, const std::vector<StatValue>& plugin, std::uint64_t rows) noexcept -> std::int32_t {
    if (stat < 0 || Settings.socketed.stats.empty() || static_cast<std::uint64_t>(stat) >= rows) return 0;
    return static_cast<std::int32_t>(std::clamp<std::int64_t>(OwnValue(totals, { stat, 0 }, plugin), INT32_MIN, INT32_MAX));
}

// ---------------------------------------------------------------------------
// Pooling
// ---------------------------------------------------------------------------
constexpr std::uint32_t GlobalPool = 0xFFFFFFFFU;

struct PoolTerm {
    std::uint32_t pool;       // GlobalPool, or the holder index for a per-item pool
    std::int32_t  stat;
    std::int32_t  layer;
    std::uint32_t holder;     // the item that receives the share
    std::int64_t  numerator;  // modifier * percent
};

auto FloorDiv100(std::int64_t n) noexcept -> std::int64_t {
    std::int64_t q = n / 100;
    if (n % 100 < 0) --q;
    return q;
}

// Per pool and pair: bonus = sum / 100 toward zero; each holder gets
// floor(n / 100), then one more point for the k holders with the largest
// remainders, k = bonus - sum of floors (0 <= k <= holders with a remainder).
void Apportion(std::vector<PoolTerm>& terms, std::vector<std::vector<StatValue>>& wanted) noexcept {
    if (terms.empty()) return;
    std::sort(terms.begin(), terms.end(), [](const PoolTerm& a, const PoolTerm& b) noexcept {
        if (a.pool != b.pool) return a.pool < b.pool;
        if (a.stat != b.stat) return a.stat < b.stat;
        if (a.layer != b.layer) return a.layer < b.layer;
        return a.holder < b.holder;
    });
    // One term per holder within each pool and pair.
    std::size_t write = 0;
    for (std::size_t read = 0; read < terms.size(); ++read) {
        if (write > 0) {
            PoolTerm& last = terms[write - 1];
            const PoolTerm& t = terms[read];
            if (last.pool == t.pool && last.stat == t.stat && last.layer == t.layer && last.holder == t.holder) {
                last.numerator += t.numerator;
                continue;
            }
        }
        terms[write++] = terms[read];
    }
    terms.resize(write);
    thread_local std::vector<std::size_t> order;
    for (std::size_t first = 0; first < terms.size();) {
        std::size_t last = first + 1;
        while (last < terms.size() && terms[last].pool == terms[first].pool && terms[last].stat == terms[first].stat
               && terms[last].layer == terms[first].layer) {
            ++last;
        }
        std::int64_t total = 0;
        std::int64_t floors = 0;
        for (std::size_t i = first; i < last; ++i) {
            total += terms[i].numerator;
            floors += FloorDiv100(terms[i].numerator);
        }
        std::int64_t extra = total / 100 - floors;
        order.clear();
        for (std::size_t i = first; i < last; ++i) order.push_back(i);
        std::stable_sort(order.begin(), order.end(), [&](std::size_t a, std::size_t b) noexcept {
            return terms[a].numerator - 100 * FloorDiv100(terms[a].numerator) > terms[b].numerator - 100 * FloorDiv100(terms[b].numerator);
        });
        for (const std::size_t i : order) {
            std::int64_t share = FloorDiv100(terms[i].numerator);
            if (extra > 0 && terms[i].numerator - 100 * share > 0) {
                ++share;
                --extra;
            }
            AddValue(wanted[terms[i].holder], terms[i].stat, terms[i].layer, share);
        }
        first = last;
    }
}

// ---------------------------------------------------------------------------
// Bonus lists
// ---------------------------------------------------------------------------
struct BonusEntry {
    void*                  game{};   // host: the item's game; client: null
    std::uint32_t          id{};
    std::uint32_t          code{};   // items.txt code, for the console listing
    std::int32_t           loc{};    // body location, 0 for an item elsewhere (a charm)
    void*                  list{};    // its own list, or (merged) the item's magic list
    std::vector<StatValue> values;    // what this plugin has in that list
    bool                   seen{};
    bool                   merged{};  // client: values were added into the item's magic list
    std::vector<StatValue> expect;    // merged: each touched stat's value right after
};

struct PrimordialEntry {
    void*         game;
    std::uint32_t id;       // the jewel; its modifier is the jewel's plugin list, found by flag
    std::uint32_t prop;     // what was applied, for the console
    std::uint8_t  variant;
    bool          seen;
    bool          computed{};          // client, shown in tooltips: values below are known
    std::vector<StatValue> values{};   // the stats the game's apply gave it
};

constexpr std::int32_t JewelLoc = -1;  // a socketed jewel's bonus entry (not a body location)


// The client pass's entries while it runs (merged additions are plugin values
// to every count it makes); null otherwise.
const std::vector<BonusEntry>* MergedEntries{};

struct BonusSide {
    std::vector<BonusEntry> entries;
    std::vector<PrimordialEntry> primordial;
    std::uint64_t           generation{};
    std::uint64_t           updates{};
    std::uint64_t           lastMs{};
    std::vector<StatValue>  effectNow;  // client: each Gem Effect stat's value on the character
    std::mutex              mutex;    // the console reads entries from its own thread
};

BonusSide                  ServerBonus;
BonusSide                  ClientBonus;
std::atomic<std::uint64_t> Generation{ 1 };
std::atomic<std::uint64_t> ServerChain{};
std::atomic<std::uint64_t> ClientChain{};
std::atomic<std::uint64_t> ChainCounter{};

// The plugin's list is only ever touched while it is still in one of the
// item's two child chains (ex +90h and +98h, next at +68h).
auto ListAttached(void* item, void* list) noexcept -> bool {
    if (item == nullptr || list == nullptr) return false;
    const std::uint8_t* ex = ExOf(item);
    if (ex == nullptr) return false;
    for (const std::size_t chain : { ExChildren, ExChildren + 8 }) {
        const auto* node = Read<const std::uint8_t*>(ex, chain);
        for (std::size_t guard = 0; node != nullptr && guard < MaxChainNodes; ++guard) {
            if (node == list) return true;
            node = Read<const std::uint8_t*>(node, ListNext);
        }
    }
    return false;
}

// A list this plugin (or another plugin) made: the plugin flag and none of
// the game's item list flags (40h magic, 2000h set, 80000000h extended).
auto IsPluginFlagged(const void* list) noexcept -> bool {
    if (list == nullptr) return false;
    const auto flags = Read<std::uint32_t>(list, ListFlags);
    return (flags & BonusListFlags) != 0 && (flags & (0x80000000U | 0x2000U | 0x40U)) == 0;
}

// The list holds exactly what was last written into it.
auto HoldsExactly(const void* list, const std::vector<StatValue>& values) noexcept -> bool {
    const auto* entries = Read<const std::uint8_t*>(list, ListStats);
    const auto  count   = Read<std::uint64_t>(list, ListStatsCount);
    std::uint64_t expected = 0;
    for (const StatValue& v : values) expected += v.value != 0 ? 1U : 0U;
    if (count != expected || count > 8192 || (count != 0 && entries == nullptr)) return false;
    for (std::uint64_t i = 0; i < count; ++i) {
        const std::uint8_t* entry = entries + i * StatEntrySize;
        const std::int32_t  value = Read<std::int32_t>(entry, StatEntryValue);
        if (value == 0 || Lookup(values, Read<std::uint16_t>(entry, StatEntryStat),
                static_cast<std::int32_t>(Read<std::uint32_t>(entry, StatEntryLayer))) != value) {
            return false;
        }
    }
    return true;
}

// A list pointer kept from an earlier pass is only trusted while the list is
// still attached to the item, still carries the plugin flag and still holds
// exactly what was written into it. When the server sends an item again, the
// client frees the item's unit and lists and builds new ones, and the allocator
// hands the freed address to the new unit's own lists: the address alone can
// be the item's real modifier list by then.
auto OwnsList(void* item, void* list, const std::vector<StatValue>& values) noexcept -> bool {
    return ListAttached(item, list) && IsPluginFlagged(list) && HoldsExactly(list, values);
}

// A jewel's Primordial Jewels list: the only plugin list a jewel ever carries.
auto PrimordialList(void* jewel) noexcept -> void* {
    void* const list = FindStatList(jewel, 0, static_cast<std::int32_t>(BonusListFlags));
    return IsPluginFlagged(list) ? list : nullptr;
}

auto ListValue(const void* list, std::int32_t stat, std::int32_t layer) noexcept -> std::int32_t {
    const auto* entries = Read<const std::uint8_t*>(list, ListStats);
    const auto  count   = Read<std::uint64_t>(list, ListStatsCount);
    for (std::uint64_t i = 0; entries != nullptr && i < count && i < 8192; ++i) {
        const std::uint8_t* entry = entries + i * StatEntrySize;
        if (Read<std::uint16_t>(entry, StatEntryStat) == stat && static_cast<std::int32_t>(Read<std::uint32_t>(entry, StatEntryLayer)) == layer) {
            return Read<std::int32_t>(entry, StatEntryValue);
        }
    }
    return 0;
}

// The list an item's blue lines are drawn from (see TOOLTIPS in the header).
auto MagicList(void* item) noexcept -> void* {
    return TooltipList != nullptr ? TooltipList(item, 0, 0x40) : nullptr;
}

// A merged entry's additions are still in the item: the same magic list,
// attached, and every touched stat still at the value it was left at.
auto MergeIntact(void* item, const BonusEntry& entry) noexcept -> bool {
    if (!entry.merged || entry.list == nullptr || entry.list != MagicList(item) || !ListAttached(item, entry.list)) return false;
    for (const StatValue& x : entry.expect) {
        if (ListValue(entry.list, x.stat, x.layer) != x.value) return false;
    }
    return true;
}

// Takes a merged entry's additions back out of the item's magic list.
void Unmerge(void* item, BonusEntry& entry) noexcept {
    if (MergeIntact(item, entry)) {
        const std::uint8_t context = GetDataContext(item);
        for (const StatValue& v : entry.values) {
            SetListStat(context, entry.list, v.stat, ListValue(entry.list, v.stat, v.layer) - v.value, v.layer);
        }
    }
    entry.list = nullptr;
    entry.values.clear();
    entry.expect.clear();
    entry.merged = false;
}

void PluginListValues(void* item, std::vector<StatValue>& out) noexcept {
    out.clear();
    const std::uint8_t* ex = ExOf(item);
    if (ex == nullptr) return;
    // Client: what this plugin added into the item's own magic list (see TOOLTIPS).
    if (const auto* merges = MergedEntries) {
        const std::uint32_t id = Read<std::uint32_t>(item, UnitId);
        for (const BonusEntry& entry : *merges) {
            if (entry.merged && entry.id == id && entry.game == nullptr && MergeIntact(item, entry)) {
                for (const StatValue& v : entry.values) AddValue(out, v.stat, v.layer, v.value);
                break;
            }
        }
    }
    const auto* node = Read<const std::uint8_t*>(ex, ExChildren);
    for (std::size_t guard = 0; node != nullptr && guard < MaxChainNodes; ++guard) {
        const auto flags = Read<std::uint32_t>(node, ListFlags);
        if ((flags & BonusListFlags) != 0 && (flags & 0x80000000U) == 0) {
            const auto* entries = Read<const std::uint8_t*>(node, ListStats);
            const auto  count   = Read<std::uint64_t>(node, ListStatsCount);
            if (entries != nullptr && count <= 8192) {
                for (std::uint64_t i = 0; i < count; ++i) {
                    const std::uint8_t* entry = entries + i * StatEntrySize;
                    AddValue(out, Read<std::uint16_t>(entry, StatEntryStat),
                        static_cast<std::int32_t>(Read<std::uint32_t>(entry, StatEntryLayer)), Read<std::int32_t>(entry, StatEntryValue));
                }
            }
        }
        node = Read<const std::uint8_t*>(node, ListNext);
    }
}

void RemoveList(void* item, void* list) noexcept {
    if (!ListAttached(item, list) || !IsPluginFlagged(list)) return;
    const std::uint8_t context = GetDataContext(item);
    UnlinkStatList(item, list);
    FreeStatList(context, list);
    BonusListsRemoved.fetch_add(1, std::memory_order_relaxed);
}

auto Describe(const std::vector<StatValue>& values) -> std::string {
    std::string text;
    char part[48];
    for (const StatValue& v : values) {
        if (v.layer != 0) std::snprintf(part, sizeof(part), " %d:%d=%+d", v.stat, v.layer, v.value);
        else std::snprintf(part, sizeof(part), " %d=%+d", v.stat, v.value);
        text += part;
    }
    return text.empty() ? std::string(" none") : text;
}

void Reconcile(BonusSide& side, void* game, void* item, std::int32_t loc, const std::vector<StatValue>& wanted) noexcept {
    const std::uint32_t id = Read<std::uint32_t>(item, UnitId);
    BonusEntry* entry = nullptr;
    std::size_t index = 0;
    for (; index < side.entries.size(); ++index) {
        if (side.entries[index].id == id && side.entries[index].game == game) {
            entry = &side.entries[index];
            break;
        }
    }
    if (entry == nullptr && wanted.empty()) return;
    // On the client the bonus goes into the item's own magic list, the one
    // tooltips draw (see TOOLTIPS in the header); everywhere else it is a list of its own.
    void* const magic = game == nullptr && Settings.tooltips ? MagicList(item) : nullptr;
    if (entry != nullptr) {
        if (entry->merged) {
            // Its additions are gone when the item was rebuilt; then nothing
            // of the kept address or values is touched again.
            if (MergeIntact(item, *entry)) {
                if (magic == nullptr) Unmerge(item, *entry);
            } else {
                entry->list = nullptr;
                entry->values.clear();
                entry->expect.clear();
                entry->merged = false;
            }
        } else if (!OwnsList(item, entry->list, entry->values)) {
            // The item's lists were rebuilt; whatever was ours went with them.
            // The kept address is never touched again.
            entry->list = nullptr;
            entry->values.clear();
        } else if (magic != nullptr) {
            RemoveList(item, entry->list);
            entry->list = nullptr;
            entry->values.clear();
        }
    }
    if (wanted.empty()) {
        if (!entry->values.empty()) Diagnostic("Jeweler: %s item %u: bonus removed.", game != nullptr ? "host" : "client", id);
        if (entry->merged) Unmerge(item, *entry);
        else if (entry->list != nullptr) RemoveList(item, entry->list);
        side.entries.erase(side.entries.begin() + static_cast<std::ptrdiff_t>(index));
        return;
    }
    const std::uint8_t context = GetDataContext(item);
    if (entry == nullptr) {
        const std::uint8_t* record = ItemsRecord(context, Read<std::uint32_t>(item, UnitClassId));
        side.entries.push_back({ game, id, record != nullptr ? Read<std::uint32_t>(record, ItemCode) : 0, loc, nullptr, {}, false });
        entry = &side.entries.back();
    }
    entry->loc = loc;
    if (magic != nullptr) {
        // What is in the list now, minus what this plugin left there, plus what it wants there.
        const std::vector<StatValue> current = entry->merged ? entry->values : std::vector<StatValue>{};
        std::vector<StatValue> touched;
        const auto touch = [&touched](const StatValue& v) {
            for (const StatValue& t : touched) {
                if (t.stat == v.stat && t.layer == v.layer) return;
            }
            touched.push_back({ v.stat, v.layer, 0 });
        };
        for (const StatValue& v : current) touch(v);
        for (const StatValue& v : wanted) touch(v);
        bool changed = false;
        entry->expect.clear();
        for (const StatValue& t : touched) {
            const std::int32_t before = ListValue(magic, t.stat, t.layer);
            const std::int32_t after  = before - Lookup(current, t.stat, t.layer) + Lookup(wanted, t.stat, t.layer);
            if (after != before) {
                SetListStat(context, magic, t.stat, after, t.layer);
                BonusValuesWritten.fetch_add(1, std::memory_order_relaxed);
                changed = true;
            }
            entry->expect.push_back({ t.stat, t.layer, ListValue(magic, t.stat, t.layer) });
        }
        entry->list   = magic;
        entry->merged = true;
        if (changed) {
            Diagnostic("Jeweler: client item %u (body location %d), shown in its own lines:%s", id, loc, Describe(wanted).c_str());
        }
        entry->values = wanted;
        entry->seen   = true;
        return;
    }
    if (entry->list == nullptr) {
        void* const list = AllocStatList(BonusListFlags, 0, static_cast<std::int32_t>(UnitItem), id);
        if (list == nullptr) return;
        AttachStatList(item, list, 1);
        entry->list = list;
        entry->values.clear();
        BonusListsAdded.fetch_add(1, std::memory_order_relaxed);
    }
    bool changed = false;
    // Pairs no longer wanted go to 0 (the engine drops a stat at 0); the rest
    // are written only when they change.
    for (const StatValue& old : entry->values) {
        if (Lookup(wanted, old.stat, old.layer) == 0) {
            SetListStat(context, entry->list, old.stat, 0, old.layer);
            BonusValuesWritten.fetch_add(1, std::memory_order_relaxed);
            changed = true;
        }
    }
    for (const StatValue& v : wanted) {
        if (Lookup(entry->values, v.stat, v.layer) == v.value) continue;
        SetListStat(context, entry->list, v.stat, v.value, v.layer);
        BonusValuesWritten.fetch_add(1, std::memory_order_relaxed);
        changed = true;
    }
    if (changed) {
        Diagnostic("Jeweler: %s item %u (body location %d):%s", game != nullptr ? "host" : "client", id, loc,
            Describe(wanted).c_str());
    }
    entry->values = wanted;
    entry->seen = true;
}

// ---------------------------------------------------------------------------
// One character
// ---------------------------------------------------------------------------
struct Holder {
    void*        item;
    std::int32_t loc;     // body location 1..12, 0 for an item elsewhere (a charm)
    bool         counts;  // its stats reach the character
};

auto IsHolder(const std::vector<Holder>& holders, void* item) noexcept -> bool {
    for (const Holder& h : holders) {
        if (h.item == item) return true;
    }
    return false;
}

// ---------------------------------------------------------------------------
// Primordial Jewels
// ---------------------------------------------------------------------------
struct PrimordialCandidate {
    std::uint32_t prop;     // property id, kind 0
    std::int32_t  parMin;
    std::int32_t  parMax;
    std::int32_t  min;
    std::int32_t  max;
    bool          range;    // funcs 15-17: min and max are separate values, never a roll
};

struct PrimordialTable {
    const std::uint8_t*                              tables{};
    std::array<std::vector<PrimordialCandidate>, 8> byVariant;
    std::array<bool, 8>                              gemFound{};
};

std::mutex                      PrimordialMutex;
std::array<PrimordialTable, 4>  PrimordialCache{};

auto PackCode(const std::string& code) noexcept -> std::uint32_t {
    std::uint32_t packed = 0x20202020U;
    for (std::size_t i = 0; i < code.size() && i < 4; ++i) {
        packed &= ~(0xFFU << (i * 8));
        packed |= static_cast<std::uint32_t>(static_cast<std::uint8_t>(code[i])) << (i * 8);
    }
    return packed;
}

void AddCandidates(const std::uint8_t* tables, std::uint32_t prop, std::int32_t parMin, std::int32_t parMax, std::int32_t min,
    std::int32_t max, std::vector<PrimordialCandidate>& out, int depth) {
    const auto id = static_cast<std::int16_t>(prop & 0xFFFFU);
    const auto kind = static_cast<std::uint16_t>(prop >> 16);
    if (id < 0) return;
    if (kind == 1) {
        const auto* groups = Read<const std::uint8_t*>(tables, TablesGroups);
        if (depth > 4 || groups == nullptr || static_cast<std::uint64_t>(id) >= Read<std::uint64_t>(tables, TablesGroupCount)) return;
        const std::uint8_t* group = groups + static_cast<std::size_t>(id) * GroupRecordSize;
        for (std::size_t i = 0; i < 8; ++i) {
            const std::uint8_t* member = group + GroupMembers + i * GroupMemberSize;
            AddCandidates(tables, Read<std::uint32_t>(member, 0), Read<std::int32_t>(member, 4), Read<std::int32_t>(member, 8),
                Read<std::int32_t>(member, 12), Read<std::int32_t>(member, 16), out, depth + 1);
        }
        return;
    }
    if (kind != 0) return;
    const auto* properties = Read<const std::uint8_t*>(tables, TablesProperties);
    if (properties == nullptr || static_cast<std::uint64_t>(id) >= Read<std::uint64_t>(tables, TablesPropertyCount)) return;
    const std::uint8_t* record = properties + static_cast<std::size_t>(id) * PropertyRecordSize;
    bool range = false;
    bool any = false;
    for (std::size_t i = 0; i < 7; ++i) {
        const std::uint8_t func = Read<std::uint8_t>(record, PropertyFuncs + i);
        if (func == 0) break;
        any = true;
        const std::int32_t stat = Read<std::int16_t>(record, PropertyStats + i * 2);
        if (stat >= 0 && std::find(Settings.primordial.exclude.begin(), Settings.primordial.exclude.end(), stat) != Settings.primordial.exclude.end()) {
            return;
        }
        range = range || func == 15 || func == 16 || func == 17;
    }
    if (any) out.push_back({ prop, parMin, parMax, min, max, range });
}

auto Primordials(std::uint8_t context) -> const PrimordialTable* {
    if (context >= PrimordialCache.size()) return nullptr;
    const std::uint8_t* tables = GetDataTables(context);
    if (tables == nullptr) return nullptr;
    std::lock_guard lock(PrimordialMutex);
    PrimordialTable& cache = PrimordialCache[context];
    if (cache.tables != tables) {
        cache = {};
        cache.tables = tables;
        const auto* gems = Read<const std::uint8_t*>(tables, TablesGems);
        const auto  count = Read<std::uint64_t>(tables, TablesGemCount);
        const std::uint64_t items = std::min<std::uint64_t>(Read<std::uint64_t>(tables, TablesItemCount), 0x10000);
        for (std::size_t v = 0; v < Settings.primordial.gems.size() && v < cache.byVariant.size(); ++v) {
            // gems.txt keeps the gem as its items.txt row, so the code is found there first.
            const std::uint32_t code = PackCode(Settings.primordial.gems[v]);
            std::int64_t itemRow = -1;
            for (std::uint64_t i = 0; i < items && itemRow < 0; ++i) {
                const std::uint8_t* record = ItemsRecord(context, static_cast<std::uint32_t>(i));
                if (record != nullptr && Read<std::uint32_t>(record, ItemCode) == code) itemRow = static_cast<std::int64_t>(i);
            }
            for (std::uint64_t row = 0; itemRow >= 0 && gems != nullptr && row < count && row < 4096; ++row) {
                const std::uint8_t* gem = gems + row * GemRecordSize;
                if (Read<std::uint32_t>(gem, GemItem) != static_cast<std::uint32_t>(itemRow)) continue;
                cache.gemFound[v] = true;
                for (std::size_t m = 0; m < 9; ++m) {
                    const std::uint8_t* mod = gem + GemMods + m * GemModSize;
                    const std::int32_t param = Read<std::int32_t>(mod, 4);
                    AddCandidates(tables, Read<std::uint32_t>(mod, 0), param, param, Read<std::int32_t>(mod, 8), Read<std::int32_t>(mod, 12),
                        cache.byVariant[v], 0);
                }
                break;
            }
            if (!cache.gemFound[v] && Context != nullptr) {
                D2RL::LogWarnF(Context, "Jeweler: Primordial Jewels found %s for '%s' (sprite %zu).",
                    itemRow < 0 ? "no item with that code" : "no gems.txt row for that item", Settings.primordial.gems[v].c_str(), v + 1);
            }
            Diagnostic("Jeweler: Primordial Jewels sprite %zu, '%s' (item %lld): %zu choices.", v + 1, Settings.primordial.gems[v].c_str(),
                static_cast<long long>(itemRow), cache.byVariant[v].size());
        }
    }
    return &cache;
}

auto Mix(std::uint32_t x) noexcept -> std::uint32_t {
    x ^= x >> 16;
    x *= 0x7FEB352DU;
    x ^= x >> 15;
    x *= 0x846CA68BU;
    x ^= x >> 16;
    return x;
}

// A value between a and b (either order) fixed by the seed.
auto Between(std::int32_t a, std::int32_t b, std::uint32_t seed) noexcept -> std::int32_t {
    if (a == b) return a;
    const std::int64_t lo = std::min(a, b);
    const std::int64_t hi = std::max(a, b);
    return static_cast<std::int32_t>(lo + static_cast<std::int64_t>(seed % static_cast<std::uint32_t>(hi - lo + 1)));
}

constexpr std::uint32_t NoProperty = 0xFFFFFFFFU;

// The server's pick for a socketed jewel, by unit id, for the client to show.
struct ServerPick {
    std::uint32_t prop;
    std::int32_t  param;
    std::int32_t  min;
    std::int32_t  max;
    std::uint64_t generation;
};
std::mutex              ServerPicksMutex;
std::vector<std::pair<std::uint32_t, ServerPick>> ServerPicks;

void RecordServerPick(std::uint32_t id, const ServerPick* pick) noexcept {
    std::lock_guard lock(ServerPicksMutex);
    for (auto it = ServerPicks.begin(); it != ServerPicks.end(); ++it) {
        if (it->first == id) {
            if (pick != nullptr) it->second = *pick;
            else ServerPicks.erase(it);
            return;
        }
    }
    if (pick != nullptr) ServerPicks.emplace_back(id, *pick);
}

auto FindServerPick(std::uint32_t id, ServerPick& out) noexcept -> bool {
    std::lock_guard lock(ServerPicksMutex);
    const std::uint64_t generation = Generation.load();
    for (const auto& [key, pick] : ServerPicks) {
        if (key == id && pick.generation == generation) {
            out = pick;
            return true;
        }
    }
    return false;
}

struct PropertyArgs {
    std::uint32_t prop;
    std::int32_t  param;
    std::int32_t  min;
    std::int32_t  max;
};

// One jewel: its modifier is on (seen), or is put on now.
auto PickFor(void* jewel, const PrimordialTable& table, PropertyArgs& args, std::size_t& choices) noexcept -> bool {
    const std::uint8_t variant = VariantOf(jewel);
    if (variant >= table.byVariant.size() || table.byVariant[variant].empty()) return false;
    const std::vector<PrimordialCandidate>& list = table.byVariant[variant];
    const std::uint32_t seed = Read<std::uint32_t>(jewel, UnitInitSeed);
    const PrimordialCandidate& pick = list[Mix(seed ^ 0x9E3779B9U) % list.size()];
    args = { pick.prop, Between(pick.parMin, pick.parMax, Mix(seed ^ 0x85EBCA6BU)), pick.min, pick.max };
    if (!pick.range && pick.min != pick.max) args.min = args.max = Between(pick.min, pick.max, Mix(seed ^ 0xC2B2AE35U));
    choices = list.size();
    return true;
}

// Client, shown in tooltips: the game applies the modifier once on a list of
// its own to learn what it gives, and those stats go into the jewel's own
// magic list like any other bonus (Reconcile), so the item it sits in shows it.
auto MergePrimordial(BonusSide& side, void* jewel, PrimordialEntry* entry) noexcept -> bool {
    const std::uint32_t id = Read<std::uint32_t>(jewel, UnitId);
    if (entry == nullptr) {
        side.primordial.push_back({ nullptr, id, NoProperty, VariantOf(jewel), true });
        entry = &side.primordial.back();
    }
    entry->seen = true;
    if (!entry->computed) {
        for (std::size_t guard = 0; guard < 4; ++guard) {
            void* const stale = PrimordialList(jewel);
            if (stale == nullptr) break;
            RemoveList(jewel, stale);
        }
        entry->values.clear();
        ServerPick pick{};
        if (FindServerPick(id, pick)) {
            PropertyArgs args{ pick.prop, pick.param, pick.min, pick.max };
            ApplyProperty(GetDataContext(jewel), ApplyMode, 0, jewel, &args, 0, 0, 0, &args, 0, static_cast<std::int32_t>(BonusListFlags), nullptr);
            if (void* const scratch = PrimordialList(jewel)) {
                const auto* stats = Read<const std::uint8_t*>(scratch, ListStats);
                const auto  count = Read<std::uint64_t>(scratch, ListStatsCount);
                for (std::uint64_t i = 0; stats != nullptr && i < count && i < 8192; ++i) {
                    const std::uint8_t* e = stats + i * StatEntrySize;
                    AddValue(entry->values, Read<std::uint16_t>(e, StatEntryStat), static_cast<std::int32_t>(Read<std::uint32_t>(e, StatEntryLayer)),
                        Read<std::int32_t>(e, StatEntryValue));
                }
                RemoveList(jewel, scratch);
            }
            entry->prop = args.prop;
            entry->computed = true;
            PrimordialApplied.fetch_add(1, std::memory_order_relaxed);
            Diagnostic("Jeweler: client jewel %u (sprite %u): the server's Primordial Jewels property %u (param %d, %d-%d), shown in its lines:%s",
                id, VariantOf(jewel) + 1U, args.prop & 0xFFFFU, args.param, args.min, args.max, Describe(entry->values).c_str());
        }
    } else {
        // The server may have dropped the pick (the jewel left its socket there first).
        ServerPick pick{};
        if (!FindServerPick(id, pick) || pick.prop != entry->prop) {
            entry->computed = false;
            entry->values.clear();
        }
    }
    Reconcile(side, nullptr, jewel, JewelLoc, entry->values);
    return !entry->values.empty();
}

auto EnsurePrimordial(BonusSide& side, void* game, void* jewel, const PrimordialTable& table) noexcept -> bool {
    const std::uint32_t id = Read<std::uint32_t>(jewel, UnitId);
    PrimordialEntry* entry = nullptr;
    for (PrimordialEntry& e : side.primordial) {
        if (e.id == id && e.game == game) {
            entry = &e;
            break;
        }
    }
    if (game == nullptr && Settings.tooltips && MagicList(jewel) != nullptr) return MergePrimordial(side, jewel, entry);
    // The pick: the server's own from the jewel's seed; a client's from the server's record.
    PropertyArgs args{};
    std::size_t  choices = 0;
    bool         picked  = false;
    if (game != nullptr) {
        picked = PickFor(jewel, table, args, choices);
        const ServerPick record{ args.prop, args.param, args.min, args.max, Generation.load() };
        RecordServerPick(id, picked ? &record : nullptr);
    } else {
        ServerPick record{};
        picked = FindServerPick(id, record);
        args = { record.prop, record.param, record.min, record.max };
    }
    // Already on: the same jewel always gets the same modifier, so a list there is the right one.
    // A unit the client built again (same id) has none and gets it again.
    if (PrimordialList(jewel) != nullptr && entry != nullptr && picked && entry->prop == args.prop) {
        entry->seen = true;
        return true;
    }
    for (std::size_t guard = 0; guard < 4; ++guard) {
        void* const stale = PrimordialList(jewel);
        if (stale == nullptr) break;
        RemoveList(jewel, stale);
    }
    const std::uint8_t variant = VariantOf(jewel);
    PrimordialEntry fresh{ game, id, NoProperty, variant, true };
    if (picked) {
        ApplyProperty(GetDataContext(jewel), ApplyMode, 0, jewel, &args, 0, 0, 0, &args, 0, static_cast<std::int32_t>(BonusListFlags), nullptr);
        fresh.prop = args.prop;
        PrimordialApplied.fetch_add(1, std::memory_order_relaxed);
        Diagnostic("Jeweler: %s jewel %u (sprite %u): Primordial Jewels property %u (param %d, %d-%d)%s.",
            game != nullptr ? "host" : "client", id, variant + 1U, args.prop & 0xFFFFU, args.param, args.min, args.max,
            game != nullptr ? "" : ", the server's pick");
    }
    if (entry != nullptr) *entry = fresh;
    else side.primordial.push_back(fresh);
    return picked && PrimordialList(jewel) != nullptr;
}

// Every jewel socketed in any item the character carries: equipped, in the
// inventory, the cube or the stash, so its tooltip shows the modifier too.
void PrimordialPass(BonusSide& side, void* game, void* player, void* inventory, const TypeIds& types) noexcept {
    const std::uint8_t context = GetDataContext(player);
    const PrimordialTable* table = Primordials(context);
    if (table == nullptr) return;
    PrimordialContext.store(context, std::memory_order_relaxed);
    std::uint32_t jewels = 0;
    std::uint32_t carriers = 0;
    void* item = FirstItem(inventory);
    for (std::size_t guard = 0; item != nullptr && guard < MaxInventoryItems; ++guard) {
        if (UnitType(item) != UnitItem) break;
        void* const next = NextItem(item);
        void* const sockets = GetInventory(item, "jeweler", 3);
        void* child = sockets != nullptr ? FirstItem(sockets) : nullptr;
        for (std::size_t inner = 0; child != nullptr && inner < MaxSocketedItems; ++inner) {
            if (UnitType(child) != UnitItem) break;
            void* const nextChild = NextItem(child);
            if (MergedInto(child) == item && ItemMatches(child, types.primordial)) {
                carriers += EnsurePrimordial(side, game, child, *table) ? 1U : 0U;
                ++jewels;
            }
            child = nextChild;
        }
        item = next;
    }
    if (game == nullptr) {
        PrimordialJewelsNow.store(jewels, std::memory_order_relaxed);
        PrimordialCarriers.store(carriers, std::memory_order_relaxed);
    }
}

// A pool for the items that count; one of its own for an item that does not,
// and for every per-item stat.
auto PoolFor(const std::vector<Holder>& holders, std::uint32_t h, std::int32_t stat) noexcept -> std::uint32_t {
    return holders[h].counts && (MaskOf(stat) & MaskPerItem) == 0 ? GlobalPool : h;
}

struct Child {
    std::uint32_t holder;
    void*         item;
    bool          jewelOrGem;  // Amplification's types
    bool          gem;         // the gem types of Prismatic Resonance and Six Peas in a Pod
    std::int32_t  color;       // index into the color types, -1 for none
    std::int64_t  copies;      // 2 in a mirrored ring
    bool          primordial;  // a jewel whose Primordial Jewels modifier its percent also raises
    std::int64_t  effect;      // the values of the Gem Effect stats whose types it matches
};

void ProcessCharacter(BonusSide& side, void* game, void* player) noexcept {
    struct Scope {
        explicit Scope(const std::vector<BonusEntry>* entries) noexcept { MergedEntries = entries; }
        ~Scope() { MergedEntries = nullptr; }
    } scope(game == nullptr ? &side.entries : nullptr);
    const bool amulet   = HasPerk(player, Settings.amulet);
    const bool ring     = HasPerk(player, Settings.ring);
    const bool socketed = HasPerk(player, Settings.socketed);
    const bool prism    = HasPrism(player);
    const bool kin      = Settings.kin.perGem != 0 && !Settings.socketed.stats.empty()
        && HasStatPerk(player, Settings.kin.enabled, Settings.kin.stat);
    const bool mirror   = Settings.mirror.percent != 0 && HasStatPerk(player, Settings.mirror.enabled, Settings.mirror.stat);
    const bool primordial = PrimordialReady && HasStatPerk(player, Settings.primordial.enabled, Settings.primordial.stat);
    const bool pliers   = Settings.gemword.percent != 0 && !Settings.socketed.stats.empty()
        && HasStatPerk(player, Settings.gemword.enabled, Settings.gemword.stat);
    // Gem Effect: each stat's value on the character, in EffectStats' order.
    // An own_sockets stat is read from each host instead (0 here).
    thread_local std::vector<std::int32_t> effectValues;
    effectValues.clear();
    bool effect = false;
    bool ownSockets = false;
    for (const EffectStat& known : EffectStats) {
        effectValues.push_back(known.ownSockets ? 0 : EffectValue(player, known.stat));
        effect = effect || effectValues.back() != 0;
        ownSockets = ownSockets || (known.ownSockets && !Settings.socketed.stats.empty());
    }
    effect = effect || ownSockets;
    if (game == nullptr) {
        side.effectNow.clear();
        for (std::size_t e = 0; e < EffectStats.size(); ++e) side.effectNow.push_back({ EffectStats[e].stat, 0, effectValues[e] });
    }
    const bool sockets  = socketed || prism || kin || pliers || effect;
    if (!amulet && !ring && !sockets && !mirror && !primordial) return;
    void* const inventory = GetInventory(player, "jeweler", 0);
    const TypeIds* types = Types(GetDataContext(player));
    if (inventory == nullptr || types == nullptr) return;
    const std::uint64_t rows = StatRows(player);
    // Gem Effect blocks that name uniques test each host against their rows.
    bool hostRules = false;
    for (const std::vector<EffectRuleIds>& rules : types->effects) {
        for (const EffectRuleIds& rule : rules) hostRules = hostRules || !rule.anyHost;
    }
    if (game == nullptr) EffectContext.store(GetDataContext(player), std::memory_order_relaxed);
    // Primordial Jewels first, so every perk below already sees the modifiers.
    if (primordial) PrimordialPass(side, game, player, inventory, *types);
    else if (game == nullptr) {
        PrimordialJewelsNow.store(0, std::memory_order_relaxed);
        PrimordialCarriers.store(0, std::memory_order_relaxed);
    }
    // Mirrored Ring: the ring in the allowed slot, with the other slot empty.
    void* mirrored = nullptr;
    if (mirror && BodyLocItem(inventory, BlockedSlot()) == nullptr) {
        void* const worn = BodyLocItem(inventory, AllowedSlot());
        if (worn != nullptr && UnitType(worn) == UnitItem && ItemMatches(worn, types->mirror) && MergedInto(worn) == player) {
            mirrored = worn;
        }
    }

    thread_local std::vector<Holder> holders;
    holders.clear();
    for (std::int32_t loc = FirstBodyLoc; loc <= LastBodyLoc; ++loc) {
        void* const item = BodyLocItem(inventory, loc);
        if (item != nullptr && UnitType(item) == UnitItem) holders.push_back({ item, loc, MergedInto(item) == player });
    }
    if (sockets) {
        // Every other item whose stats reach the character: charms.
        void* item = FirstItem(inventory);
        for (std::size_t guard = 0; item != nullptr && guard < MaxInventoryItems; ++guard) {
            if (UnitType(item) != UnitItem) break;
            void* const next = NextItem(item);
            if (!IsHolder(holders, item) && MergedInto(item) == player) holders.push_back({ item, 0, true });
            item = next;
        }
    }

    thread_local std::vector<std::vector<StatValue>> wanted;
    thread_local std::vector<PoolTerm>               ringTerms;
    thread_local std::vector<PoolTerm>               gemTerms;
    thread_local std::vector<StatValue>              plugin;
    thread_local std::vector<StatKey>                pairs;
    thread_local std::vector<StatValue>              totals;
    thread_local std::vector<Child>                  children;
    thread_local std::vector<std::int32_t>           hostValues;
    thread_local std::vector<std::uint32_t>          carriers;
    children.clear();
    carriers.assign(EffectStats.size(), 0);
    std::uint32_t colorMask = 0;
    wanted.resize(holders.size());
    for (std::vector<StatValue>& w : wanted) w.clear();
    ringTerms.clear();
    gemTerms.clear();
    std::uint32_t pooledRings = 0;
    std::uint32_t pooledGems = 0;
    std::uint32_t mirroredRings = 0;
    std::uint32_t gemwords = 0;

    for (std::uint32_t h = 0; h < holders.size(); ++h) {
        void* const item = holders[h].item;
        const bool equipped = holders[h].loc != 0;
        const std::int64_t copies = item == mirrored ? 2 : 1;  // the mirror adds a second identical ring
        const auto poolFor = [&](std::int32_t stat) noexcept { return PoolFor(holders, h, stat); };
        // The item's lists and plugin bonuses, read once for the perks below.
        bool read = false;
        const auto readItem = [&]() noexcept {
            if (read) return;
            PluginListValues(item, plugin);
            ListTotals(item, totals);
            read = true;
        };
        if (item == mirrored) {
            readItem();
            AllPairs(totals, pairs);
            for (const StatKey& key : pairs) {
                if (static_cast<std::uint64_t>(key.stat) >= rows) continue;
                AddValue(wanted[h], key.stat, key.layer, OwnValue(totals, key, plugin) * Settings.mirror.percent / 100);
            }
            ++mirroredRings;
        }
        if (equipped && amulet && ItemMatches(item, types->amulet)) {
            readItem();
            ItemPairs(totals, MaskAmulet, pairs);
            for (const StatKey& key : pairs) {
                if (static_cast<std::uint64_t>(key.stat) >= rows) continue;
                AddValue(wanted[h], key.stat, key.layer, OwnValue(totals, key, plugin) * Settings.amulet.percent / 100);
            }
        }
        if (equipped && ring && ItemMatches(item, types->ring)) {
            readItem();
            ItemPairs(totals, MaskRing, pairs);
            for (const StatKey& key : pairs) {
                if (static_cast<std::uint64_t>(key.stat) >= rows) continue;
                const std::int64_t own = OwnValue(totals, key, plugin);
                if (own != 0) ringTerms.push_back({ poolFor(key.stat), key.stat, key.layer, h, own * Settings.ring.percent * copies });
            }
            if (holders[h].counts) pooledRings += static_cast<std::uint32_t>(copies);
        }
        if (sockets) {
            // Its uniqueitems.txt row, for the Gem Effect blocks that name uniques.
            const std::int32_t hostUnique = effect && hostRules ? UniqueRowOf(item) : -1;
            // Each Gem Effect stat's value for the items socketed here; own_sockets
            // stats come from this item itself.
            hostValues.assign(effectValues.begin(), effectValues.end());
            if (ownSockets) {
                readItem();
                for (std::size_t e = 0; e < EffectStats.size() && e < hostValues.size(); ++e) {
                    if (!EffectStats[e].ownSockets) continue;
                    hostValues[e] = HostEffectValue(totals, EffectStats[e].stat, plugin, rows);
                    if (hostValues[e] != 0) ++carriers[e];
                }
            }
            void* const socketInventory = GetInventory(item, "jeweler", 1);
            void* child = socketInventory != nullptr ? FirstItem(socketInventory) : nullptr;
            std::uint32_t socketedItems = 0;
            std::uint32_t gemwordItems = 0;
            for (std::size_t guard = 0; child != nullptr && guard < MaxSocketedItems; ++guard) {
                if (UnitType(child) != UnitItem) break;
                void* const next = NextItem(child);
                if (MergedInto(child) == item) {
                    ++socketedItems;
                    if (pliers && ItemMatches(child, types->gemwordSockets)) ++gemwordItems;
                    const bool isGem = ItemMatches(child, types->gems);
                    const bool jewelOrGem = socketed && ItemMatches(child, types->socketed);
                    const bool gem = (prism || kin) && isGem;
                    const bool primordialJewel = primordial && ItemMatches(child, types->primordial);
                    std::int64_t gemEffect = 0;
                    for (std::size_t e = 0; effect && e < hostValues.size() && e < types->effects.size(); ++e) {
                        if (hostValues[e] != 0 && EffectMatches(child, hostUnique, types->effects[e])) gemEffect += hostValues[e];
                    }
                    std::int32_t color = -1;
                    for (std::size_t c = 0; gem && color < 0 && c < types->colors.size(); ++c) {
                        if (CheckItemType(child, types->colors[c]) != 0) color = static_cast<std::int32_t>(c);
                    }
                    if (jewelOrGem || gem || gemEffect != 0) {
                        children.push_back({ h, child, jewelOrGem, gem, color, copies, primordialJewel, gemEffect });
                    }
                    if (holders[h].counts && color >= 0) colorMask |= 1U << color;
                }
                child = next;
            }
            // Steady Pliers: a runeword whose sockets hold gems only.
            if (pliers && socketedItems != 0 && gemwordItems == socketedItems) {
                void* const list = FindStatList(item, RunewordState, 0x40);
                if (list != nullptr && Read<std::int32_t>(list, ListFlags) >= 0) {
                    const auto* entries = Read<const std::uint8_t*>(list, ListStats);
                    const auto  count   = Read<std::uint64_t>(list, ListStatsCount);
                    for (std::uint64_t i = 0; entries != nullptr && count <= 4096 && i < count; ++i) {
                        const std::uint8_t* entry = entries + i * StatEntrySize;
                        const std::int32_t stat = Read<std::uint16_t>(entry, StatEntryStat);
                        const auto layer = static_cast<std::int32_t>(Read<std::uint32_t>(entry, StatEntryLayer));
                        const std::int64_t value = Read<std::int32_t>(entry, StatEntryValue);
                        if (value == 0 || (MaskOf(stat) & MaskSocketed) == 0 || static_cast<std::uint64_t>(stat) >= rows) continue;
                        gemTerms.push_back({ poolFor(stat), stat, layer, h, value * Settings.gemword.percent * copies });
                    }
                    if (holders[h].counts) ++gemwords;
                }
            }
        }
    }
    // The console's count for each own_sockets stat: the items carrying it now.
    if (game == nullptr) {
        for (std::size_t e = 0; e < EffectStats.size() && e < side.effectNow.size(); ++e) {
            if (EffectStats[e].ownSockets) side.effectNow[e].value = static_cast<std::int32_t>(carriers[e]);
        }
    }
    // Amplification, Prismatic Resonance, Six Peas in a Pod and Gem Effect: one percent per jewel or gem, one pool.
    std::uint32_t colors = 0;
    for (std::uint32_t mask = colorMask; mask != 0; mask &= mask - 1) ++colors;
    const std::int32_t prismPercent = prism ? Settings.prism.perColor * static_cast<std::int32_t>(colors) : 0;
    for (const Child& c : children) {
        // Six Peas in a Pod: the gems of this gem's color in the same item, itself included.
        std::int32_t kindred = 0;
        if (kin && c.gem && c.color >= 0) {
            for (const Child& other : children) kindred += other.holder == c.holder && other.gem && other.color == c.color ? 1 : 0;
        }
        const std::int64_t perks = static_cast<std::int64_t>(c.jewelOrGem ? Settings.socketed.percent : 0)
            + (c.gem && prism ? prismPercent : 0) + static_cast<std::int64_t>(Settings.kin.perGem) * kindred;
        const std::int32_t percent = GemPercent(perks + c.effect);
        // No gem effect stat takes Gem Effect's part: it would raise its own
        // percent, or another block's, pass after pass.
        const std::int32_t selfPercent = GemPercent(perks);
        if (percent == 0 && selfPercent == 0) continue;
        PluginListValues(c.item, plugin);
        ListTotals(c.item, totals);
        ItemPairs(totals, MaskSocketed, pairs);
        for (const StatKey& key : pairs) {
            if (static_cast<std::uint64_t>(key.stat) >= rows) continue;
            const std::int32_t keyPercent = (MaskOf(key.stat) & MaskEffect) != 0 ? selfPercent : percent;
            if (keyPercent == 0) continue;
            // A jewel's only plugin list is its Primordial Jewels modifier, which
            // its percent raises too (Amplification, and Gem Effect when its
            // types include jewels; a jewel gets no other gem perk).
            std::int64_t own = OwnValue(totals, key, plugin);
            if (c.primordial) own += Lookup(plugin, key.stat, key.layer);
            if (own != 0) gemTerms.push_back({ PoolFor(holders, c.holder, key.stat), key.stat, key.layer, c.holder, own * keyPercent * c.copies });
        }
        if (holders[c.holder].counts) pooledGems += static_cast<std::uint32_t>(c.copies);
    }
    Apportion(ringTerms, wanted);
    Apportion(gemTerms, wanted);
    for (std::uint32_t h = 0; h < holders.size(); ++h) Reconcile(side, game, holders[h].item, holders[h].loc, wanted[h]);
    if (game == nullptr) {
        PooledRings.store(pooledRings, std::memory_order_relaxed);
        PooledGems.store(pooledGems, std::memory_order_relaxed);
        GemColors.store(colors, std::memory_order_relaxed);
        MirrorActive.store(mirroredRings != 0, std::memory_order_relaxed);
        Gemwords.store(gemwords, std::memory_order_relaxed);
    }
}

// Every entry no character claimed this pass loses its list.
void Sweep(BonusSide& side, bool host) noexcept {
    // Primordial Jewels: a jewel not met this pass left its socket, its holder
    // left the character, or the perk is gone.
    for (std::size_t i = 0; i < side.primordial.size();) {
        PrimordialEntry& entry = side.primordial[i];
        if (entry.seen) {
            entry.seen = false;
            ++i;
            continue;
        }
        void* const jewel = host ? ServerUnit(entry.game, UnitItem, entry.id) : ClientUnit(entry.id, UnitItem);
        if (host) RecordServerPick(entry.id, nullptr);
        for (std::size_t guard = 0; jewel != nullptr && UnitType(jewel) == UnitItem && guard < 4; ++guard) {
            void* const list = PrimordialList(jewel);
            if (list == nullptr) break;
            RemoveList(jewel, list);
            PrimordialRemoved.fetch_add(1, std::memory_order_relaxed);
        }
        side.primordial.erase(side.primordial.begin() + static_cast<std::ptrdiff_t>(i));
    }
    for (std::size_t i = 0; i < side.entries.size();) {
        BonusEntry& entry = side.entries[i];
        if (entry.seen) {
            entry.seen = false;
            ++i;
            continue;
        }
        void* const item = host ? ServerUnit(entry.game, UnitItem, entry.id) : ClientUnit(entry.id, UnitItem);
        if (item != nullptr && UnitType(item) == UnitItem) {
            if (entry.merged) Unmerge(item, entry);
            else if (OwnsList(item, entry.list, entry.values)) RemoveList(item, entry.list);
        }
        side.entries.erase(side.entries.begin() + static_cast<std::ptrdiff_t>(i));
    }
}

void StartPass(BonusSide& side) noexcept {
    const std::uint64_t generation = Generation.load();
    if (side.generation != generation) {
        // A game was left or joined: the units behind the old entries are gone.
        side.entries.clear();
        side.primordial.clear();
        side.generation = generation;
    }
}

constexpr std::size_t MaxPlayers = 16;

void HostPass() noexcept {
    std::lock_guard lock(ServerBonus.mutex);
    StartPass(ServerBonus);
    if (Read<std::int32_t>(reinterpret_cast<const void*>(Base + RegistryReadyRva), 0) != 0) {
        const auto* const buckets = reinterpret_cast<const std::uint8_t* const*>(Base + RegistryRva);
        std::size_t players = 0;
        std::size_t visited = 0;
        for (std::size_t bucket = 0; bucket < RegistryBuckets && visited <= 256; ++bucket) {
            for (const std::uint8_t* client = buckets[bucket]; client != nullptr && ++visited <= 256;
                    client = Read<const std::uint8_t*>(client, RegistryNext)) {
                void* const game = Read<void*>(client, RegistryGame);
                if (game == nullptr || Read<std::uint32_t>(client, RegistryUnitType) != UnitPlayer) continue;
                void* const player = ServerUnit(game, UnitPlayer, Read<std::uint32_t>(client, RegistryUnitId));
                if (player == nullptr || UnitType(player) != UnitPlayer || ++players > MaxPlayers) continue;
                ProcessCharacter(ServerBonus, game, player);
            }
        }
    }
    Sweep(ServerBonus, true);
    HostPasses.fetch_add(1, std::memory_order_relaxed);
}

void ClientPass() noexcept {
    std::lock_guard lock(ClientBonus.mutex);
    StartPass(ClientBonus);
    void* const player = PlayerFromIndex(LocalPlayerIndex());
    if (player != nullptr && UnitType(player) == UnitPlayer) ProcessCharacter(ClientBonus, nullptr, player);
    Sweep(ClientBonus, false);
    std::uint32_t merged = 0;
    for (const BonusEntry& entry : ClientBonus.entries) merged += entry.merged ? 1U : 0U;
    MergedItemsNow.store(merged, std::memory_order_relaxed);
    ClientPasses.fetch_add(1, std::memory_order_relaxed);
}

void __cdecl HostTick(const D2RL::PluginContext* context, void* userData) noexcept {
    const auto id = static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(userData));
    if (context == nullptr || ServerChain.load() != id) return;
    if (++ServerBonus.updates % ServerPassUpdates == 0 && Settings.enabled) HostPass();
    if (ThreadSvc->runOnGameThread(context, &HostTick, userData) != D2RL::Threads::Result::Success) {
        std::uint64_t expected = id;
        (void)ServerChain.compare_exchange_strong(expected, 0);
    }
}

void __cdecl ClientTick(const D2RL::PluginContext* context, void* userData) noexcept {
    const auto id = static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(userData));
    if (context == nullptr || ClientChain.load() != id) return;
    const std::uint64_t now = GetTickCount64();
    if (now - ClientBonus.lastMs >= ClientPassMs && Settings.enabled) {
        ClientBonus.lastMs = now;
        ClientPass();
    }
    if (ThreadSvc->runOnUiThread(context, &ClientTick, userData) != D2RL::Threads::Result::Success) {
        std::uint64_t expected = id;
        (void)ClientChain.compare_exchange_strong(expected, 0);
    }
}

void StartChains(const D2RL::PluginContext* context) noexcept {
    if (!Ready || ThreadSvc == nullptr) return;
    if (ServerChain.load() == 0) {
        const std::uint64_t id = ChainCounter.fetch_add(1) + 1;
        ServerChain.store(id);
        // A remote TCP/IP client has no game thread here; only its UI pass runs.
        if (ThreadSvc->runOnGameThread(context, &HostTick, reinterpret_cast<void*>(static_cast<std::uintptr_t>(id)))
                != D2RL::Threads::Result::Success) {
            std::uint64_t expected = id;
            (void)ServerChain.compare_exchange_strong(expected, 0);
        }
    }
    if (ClientChain.load() == 0) {
        const std::uint64_t id = ChainCounter.fetch_add(1) + 1;
        ClientChain.store(id);
        if (ThreadSvc->runOnUiThread(context, &ClientTick, reinterpret_cast<void*>(static_cast<std::uintptr_t>(id)))
                != D2RL::Threads::Result::Success) {
            std::uint64_t expected = id;
            (void)ClientChain.compare_exchange_strong(expected, 0);
        }
    }
}

void __cdecl OnGameJoined(const D2RL::PluginContext*, const D2RL::Lifecycle::GameplayEvent*, void*) noexcept {
    Generation.fetch_add(1);
}

void __cdecl OnLocalPlayerReady(const D2RL::PluginContext* context, const D2RL::Lifecycle::GameplayEvent*, void*) noexcept {
    if (context != nullptr) StartChains(context);
}

void __cdecl OnGameLeft(const D2RL::PluginContext*, const D2RL::Lifecycle::GameplayEvent*, void*) noexcept {
    Generation.fetch_add(1);
    ServerChain.store(0);
    ClientChain.store(0);
}

// ---------------------------------------------------------------------------
// Installation
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

// A function the plugin only calls may carry another plugin's entry hook.
auto EntryHooked(std::uint64_t rva) noexcept -> bool {
    const auto* code = reinterpret_cast<const std::uint8_t*>(Base + rva);
    return code[0] == 0xE9 || (code[0] == 0xFF && code[1] == 0x25);
}

auto Verify() -> bool {
    const std::array<Witness, 12> callables{ {
        W(GetOwnerBodyRva, GetOwnerBody, "STATLIST_GetOwner"),
        W(FirstItemBodyRva, FirstItemBody, "INVENTORY_GetFirstItem"),
        W(NextItemBodyRva, NextItemBody, "INVENTORY_GetNextItem"),
        W(ItemsRecordBodyRva, ItemsRecordBody, "DATATBLS_GetItemsTxtRecord"),
        W(UnlinkStatListEntryRva, UnlinkStatListEntry, "STATLIST_UnlinkStatList"),
        W(FreeStatListEntryRva, FreeStatListEntry, "STATLIST_FreeStatList"),
        W(BodyLocItemEntryRva, BodyLocItemEntry, "INVENTORY_GetItemFromBodyLoc"),
        W(InventoryEntryRva, InventoryEntry, "UNITS_GetInventory"),
        W(ClientUnitEntryRva, ClientUnitEntry, "CLIENT_GetUnitByIdAndType"),
        W(ServerUnitEntryRva, ServerUnitEntry, "SUNIT_GetServerUnit"),
        W(LocalPlayerIndexEntryRva, LocalPlayerIndexEntry, "the local player index"),
        W(PlayerFromIndexEntryRva, PlayerFromIndexEntry, "CLIENT_GetLocalPlayer"),
    } };
    for (const Witness& witness : callables) {
        if (!Context->CheckExpectedBytes(witness.rva, witness.bytes, witness.size) && !EntryHooked(witness.rva)) {
            char text[256];
            std::snprintf(text, sizeof(text), "%s at 0x%llX does not match D2RLoader 1.3.1",
                witness.name, static_cast<unsigned long long>(witness.rva));
            InstallProblem = text;
            D2RL::LogErrorF(Context, "Jeweler: %s. Nothing was installed.", text);
            return false;
        }
    }
    const std::array<Witness, 5> windows{ {
        W(StatListWindowRva, StatListWindow, "the item stat list creation in 0x43EF10"),
        W(TypeCheckBodyRva, TypeCheckBody, "the item type check"),
        W(RegistryResetRva, RegistryReset, "the server client registry reset"),
        W(ClientLookupRva, ClientLookup, "the server client lookup"),
        W(ClientPlayerRva, ClientPlayer, "the server client fields"),
    } };
    for (const Witness& witness : windows) {
        if (!Context->CheckExpectedBytes(witness.rva, witness.bytes, witness.size)) {
            char text[256];
            std::snprintf(text, sizeof(text), "%s at 0x%llX does not match D2RLoader 1.3.1 (another plugin may own it)",
                witness.name, static_cast<unsigned long long>(witness.rva));
            InstallProblem = text;
            D2RL::LogErrorF(Context, "Jeweler: %s. Nothing was installed.", text);
            return false;
        }
    }
    return true;
}

void BindNatives() noexcept {
    ReadOwnStat      = At<ReadStatFn>(ReadOwnStatRva);
    ReadUnitStat     = At<ReadStatFn>(ReadUnitStatRva);
    GetDataContext   = At<DataContextFn>(GetDataContextRva);
    GetDataTables    = At<DataTablesFn>(GetDataTablesRva);
    CheckItemType    = At<CheckItemTypeFn>(CheckItemTypeRva);
    ItemsRecord      = At<ItemsRecordFn>(ItemsRecordRva);
    AllocStatList    = At<AllocListFn>(AllocStatListRva);
    AttachStatList   = At<AttachListFn>(AttachStatListRva);
    SetListStat      = At<SetListStatFn>(SetListStatRva);
    UnlinkStatList   = At<UnlinkListFn>(UnlinkStatListRva);
    FreeStatList     = At<FreeListFn>(FreeStatListRva);
    GetInventory     = At<InventoryFn>(InventoryRva);
    BodyLocItem      = At<BodyLocItemFn>(BodyLocItemRva);
    FirstItem        = At<FirstItemFn>(FirstItemRva);
    NextItem         = At<NextItemFn>(NextItemRva);
    GetOwner         = At<GetOwnerFn>(GetOwnerRva);
    FindStatList     = At<FindStatListFn>(FindStatListRva);
    ServerUnit       = At<ServerUnitFn>(ServerUnitRva);
    ClientUnit       = At<ClientUnitFn>(ClientUnitRva);
    LocalPlayerIndex = At<LocalIndexFn>(LocalPlayerIndexRva);
    PlayerFromIndex  = At<PlayerFromIndexFn>(PlayerFromIndexRva);
}

// ---------------------------------------------------------------------------
// Mirrored Ring, client side: the ghost in the blocked slot and its clicks
// ---------------------------------------------------------------------------
PairFn      OriginalPair{};
CanEquipFn  OriginalCanEquip{};
HandFn      OriginalHand{};
RequestFn   OriginalRequest{};
PlaySoundFn PlayVoice{};
std::uint8_t* RelayPage{};
bool          MirrorUiInstalled{};
std::string   MirrorUiProblem;

struct MirrorView {
    void* player{};
    void* allowed{};      // the ring in the allowed slot
    bool  rule{};         // the local character has the perk and the blocked slot is empty
    bool  ghost{};        // ... and the allowed slot holds a ring
};

auto IsMirrorRing(void* item) noexcept -> bool {
    if (item == nullptr || UnitType(item) != UnitItem) return false;
    const TypeIds* types = Types(GetDataContext(item));
    return types != nullptr && ItemMatches(item, types->mirror);
}

auto MirrorNow() noexcept -> MirrorView {
    MirrorView view;
    if (!Ready || !Settings.enabled) return view;
    view.player = PlayerFromIndex(LocalPlayerIndex());
    if (view.player == nullptr || UnitType(view.player) != UnitPlayer
            || !HasStatPerk(view.player, Settings.mirror.enabled, Settings.mirror.stat)) {
        return view;
    }
    void* const inventory = GetInventory(view.player, "jeweler", 2);
    if (inventory == nullptr || BodyLocItem(inventory, BlockedSlot()) != nullptr) return view;
    view.rule = true;
    void* const worn = BodyLocItem(inventory, AllowedSlot());
    if (IsMirrorRing(worn)) {
        view.allowed = worn;
        view.ghost = true;
    }
    return view;
}

// 2C99DD: the slot paired with this one. The blocked ring slot pairs with the
// allowed one while it shows the ghost.
auto __fastcall MirrorPair(std::int32_t slot) noexcept -> std::int32_t {
    const std::int32_t paired = OriginalPair(slot);
    if (paired != 0 || slot != BlockedSlot()) return paired;
    return MirrorNow().ghost ? AllowedSlot() : paired;
}

// 2C9C06: the paired item counts as two-handed when it is the mirrored ring,
// which makes the draw show it as a ghost (and its tooltip) in the empty slot.
auto __fastcall MirrorHandedness(void* unit, void* item, std::int32_t flag) noexcept -> std::int32_t {
    if (item != nullptr && UnitType(item) == UnitItem) {
        const MirrorView view = MirrorNow();
        if (view.ghost && unit == view.player && item == view.allowed) return 2;
    }
    return OriginalHand(unit, item, flag);
}

// 2C9BA5: the slot highlight for a held ring over the blocked slot follows
// what 0x15C600 will do with it: a swap with the worn ring, or "Impossible".
auto __fastcall MirrorCanEquip(void* item, std::int32_t slot) noexcept -> std::int32_t {
    if (slot == BlockedSlot() && IsMirrorRing(item)) {
        const MirrorView view = MirrorNow();
        if (view.rule) return view.ghost ? OriginalCanEquip(item, AllowedSlot()) : 0;
    }
    return OriginalCanEquip(item, slot);
}

// The five calls to 0x15C600 (held item, slot, ., unit id, .): on the blocked
// slot a pickup takes the worn ring, a held ring swaps with it, and with no
// ring worn a held ring is refused the way 0x15C600 refuses an item that does
// not fit. Requests for another unit (a mercenary: unit id other than -1) and
// every other slot pass unchanged.
auto __fastcall MirrorRequest(void* item, std::uint32_t slot, std::uint8_t a3, std::uint32_t unitId, std::uint8_t a5) noexcept
    -> std::int64_t {
    if (unitId == 0xFFFFFFFFU && static_cast<std::int32_t>(slot) == BlockedSlot()) {
        const MirrorView view = MirrorNow();
        if (view.rule) {
            const auto allowed = static_cast<std::uint32_t>(AllowedSlot());
            if (item == nullptr && view.ghost) {
                MirrorPickups.fetch_add(1, std::memory_order_relaxed);
                Diagnostic("Jeweler: Mirrored Ring, the ghost picks up the worn ring.");
                return OriginalRequest(nullptr, allowed, a3, unitId, a5);
            }
            if (IsMirrorRing(item)) {
                if (view.ghost) {
                    MirrorSwaps.fetch_add(1, std::memory_order_relaxed);
                    Diagnostic("Jeweler: Mirrored Ring, a held ring swaps with the worn ring.");
                    return OriginalRequest(item, allowed, a3, unitId, a5);
                }
                MirrorRefusals.fetch_add(1, std::memory_order_relaxed);
                Diagnostic("Jeweler: Mirrored Ring, a ring refused in the blocked slot.");
                PlayVoice(view.player, ImpossibleMessage);
                return 0;
            }
        }
    }
    return OriginalRequest(item, slot, a3, unitId, a5);
}

auto CanReach(std::uintptr_t from, std::uintptr_t to) noexcept -> bool {
    const auto delta = static_cast<std::int64_t>(to) - static_cast<std::int64_t>(from);
    return delta >= INT32_MIN && delta <= INT32_MAX;
}

auto AllocateNear(std::uintptr_t hint, std::size_t size) noexcept -> std::uint8_t* {
    SYSTEM_INFO info{};
    GetSystemInfo(&info);
    const auto step = static_cast<std::uintptr_t>(info.dwAllocationGranularity);
    if (step == 0) return nullptr;
    const std::uintptr_t aligned = hint & ~(step - 1);
    for (std::uintptr_t delta = step; delta < 0x7FF00000ULL; delta += step) {
        for (const std::uintptr_t candidate : { aligned + delta, aligned - delta }) {
            if (candidate < 0x10000 || !CanReach(hint, candidate) || !CanReach(hint, candidate + size)) continue;
            if (void* memory = VirtualAlloc(reinterpret_cast<void*>(candidate), size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE)) {
                return static_cast<std::uint8_t*>(memory);
            }
        }
    }
    return nullptr;
}

struct UiSite {
    std::uint64_t               windowRva;
    const std::uint8_t*         window;
    std::uint32_t               windowSize;
    std::uint64_t               callRva;    // the five-byte call inside the window
    std::uintptr_t              target;     // the plugin function the relay jumps to
    std::array<std::uint8_t, 5> stock{};
    std::array<std::uint8_t, 5> written{};
    bool                        patched{};
};

std::array<UiSite, 8> UiSites{};

void RestoreMirrorUi() noexcept {
    for (UiSite& site : UiSites) {
        if (site.patched && Context->PatchBytes(site.callRva, site.written.data(), 5, site.stock.data(), 5)) site.patched = false;
    }
    MirrorUiInstalled = false;
}

auto InstallMirrorUi() -> bool {
    OriginalPair     = At<PairFn>(PairFnRva);
    OriginalCanEquip = At<CanEquipFn>(CanEquipFnRva);
    OriginalHand     = At<HandFn>(HandFnRva);
    OriginalRequest  = At<RequestFn>(RequestFnRva);
    PlayVoice        = At<PlaySoundFn>(PlaySoundFnRva);
    UiSites = { {
        { PairWindowRva, PairWindow, sizeof(PairWindow), 0x2C99DD, reinterpret_cast<std::uintptr_t>(&MirrorPair) },
        { CanEquipWindowRva, CanEquipWindow, sizeof(CanEquipWindow), 0x2C9BA5, reinterpret_cast<std::uintptr_t>(&MirrorCanEquip) },
        { HandWindowRva, HandWindow, sizeof(HandWindow), 0x2C9C06, reinterpret_cast<std::uintptr_t>(&MirrorHandedness) },
        { RequestWindowARva, RequestWindowA, sizeof(RequestWindowA), 0x2AA937, reinterpret_cast<std::uintptr_t>(&MirrorRequest) },
        { RequestWindowBRva, RequestWindowB, sizeof(RequestWindowB), 0x2AAA79, reinterpret_cast<std::uintptr_t>(&MirrorRequest) },
        { RequestWindowCRva, RequestWindowC, sizeof(RequestWindowC), 0x2CABDC, reinterpret_cast<std::uintptr_t>(&MirrorRequest) },
        { RequestWindowDRva, RequestWindowD, sizeof(RequestWindowD), 0x2CAC6C, reinterpret_cast<std::uintptr_t>(&MirrorRequest) },
        { RequestWindowERva, RequestWindowE, sizeof(RequestWindowE), 0x2CB0AB, reinterpret_cast<std::uintptr_t>(&MirrorRequest) },
    } };
    if (!Context->CheckExpectedBytes(ImpossibleWindowRva, ImpossibleWindow, sizeof(ImpossibleWindow))) {
        MirrorUiProblem = "the Impossible voice call at 0x15C793 does not match D2RLoader 1.3.1";
        return false;
    }
    for (UiSite& site : UiSites) {
        if (!Context->CheckExpectedBytes(site.windowRva, site.window, site.windowSize)) {
            char text[160];
            std::snprintf(text, sizeof(text), "the inventory slot code at 0x%llX does not match D2RLoader 1.3.1 (another plugin may own it)",
                static_cast<unsigned long long>(site.callRva));
            MirrorUiProblem = text;
            return false;
        }
        std::memcpy(site.stock.data(), site.window + (site.callRva - site.windowRva), 5);
    }
    RelayPage = AllocateNear(Base + 0x2C99DD, 0x1000);
    if (RelayPage == nullptr) {
        MirrorUiProblem = "no relay page within reach of the game image";
        return false;
    }
    for (std::size_t i = 0; i < UiSites.size(); ++i) {
        UiSite& site = UiSites[i];
        std::uint8_t* const relay = RelayPage + i * RelaySize;
        relay[0] = 0xFF;
        relay[1] = 0x25;
        std::memset(relay + 2, 0, 4);
        std::memcpy(relay + 6, &site.target, sizeof(site.target));
        const auto from = Base + site.callRva + 5;
        const auto to = reinterpret_cast<std::uintptr_t>(relay);
        if (!CanReach(from, to)) {
            MirrorUiProblem = "a relay is out of rel32 reach";
            return false;
        }
        const auto rel = static_cast<std::int32_t>(static_cast<std::int64_t>(to) - static_cast<std::int64_t>(from));
        site.written[0] = 0xE8;
        std::memcpy(site.written.data() + 1, &rel, 4);
    }
    DWORD old = 0;
    VirtualProtect(RelayPage, 0x1000, PAGE_EXECUTE_READ, &old);
    FlushInstructionCache(GetCurrentProcess(), RelayPage, 0x1000);
    for (UiSite& site : UiSites) {
        site.patched = Context->PatchBytes(site.callRva, site.stock.data(), 5, site.written.data(), 5);
        if (!site.patched) {
            char text[128];
            std::snprintf(text, sizeof(text), "the call at 0x%llX could not be redirected", static_cast<unsigned long long>(site.callRva));
            MirrorUiProblem = text;
            RestoreMirrorUi();
            return false;
        }
    }
    MirrorUiInstalled = true;
    return true;
}

auto InstallTooltips() -> bool {
    if (!Context->CheckExpectedBytes(TooltipListBodyRva, TooltipListBody, static_cast<std::uint32_t>(sizeof(TooltipListBody)))) {
        TooltipProblem = "the tooltip's list finder at 0x2F5A00 does not match D2RLoader 1.3.1";
        return false;
    }
    TooltipList = At<TooltipListFn>(TooltipListBodyRva);
    return true;
}

auto InstallPrimordial() -> bool {
    struct Pin {
        std::uint64_t       rva;
        const std::uint8_t* bytes;
        std::size_t         size;
        const char*         what;
    };
    const std::array<Pin, 9> pins{ {
        { ApplyWrapperRva, ApplyWrapper, sizeof(ApplyWrapper), "the cube's property apply" },
        { PropertyLoopRva, PropertyLoop, sizeof(PropertyLoop), "the property func loop" },
        { PropertiesGetterRva, PropertiesGetter, sizeof(PropertiesGetter), "the properties.txt record getter" },
        { GroupsGetterRva, GroupsGetter, sizeof(GroupsGetter), "the propertygroups.txt record getter" },
        { GroupMemberReadRva, GroupMemberRead, sizeof(GroupMemberRead), "the property group member reader" },
        { GemsTableRva, GemsTable, sizeof(GemsTable), "the gems.txt table" },
        { GemsModsRva, GemsMods, sizeof(GemsMods), "the gems.txt mod layout" },
        { InitSeedGetterRva, InitSeedGetter, sizeof(InitSeedGetter), "a unit's starting seed" },
        { VariantGetterRva, VariantGetter, sizeof(VariantGetter), "an item's graphic variant" },
    } };
    for (const Pin& pin : pins) {
        if (!Context->CheckExpectedBytes(pin.rva, pin.bytes, static_cast<std::uint32_t>(pin.size))) {
            char text[160];
            std::snprintf(text, sizeof(text), "%s at 0x%llX does not match D2RLoader 1.3.1", pin.what, static_cast<unsigned long long>(pin.rva));
            PrimordialProblem = text;
            return false;
        }
    }
    ApplyProperty = At<ApplyPropertyFn>(ApplyPropertyRva);
    VariantOf     = At<VariantFn>(VariantRva);
    PrimordialReady = true;
    return true;
}

// Gem Effect's host_uniques: the two item getters and the uniqueitems.txt
// layout. Without them a block that names uniques matches no item.
auto InstallUniqueHosts() -> bool {
    const std::array<Witness, 5> pins{ {
        W(ItemQualityBodyRva, ItemQualityBody, "ITEMS_GetItemQuality"),
        W(ItemFileIndexBodyRva, ItemFileIndexBody, "the item file index getter"),
        W(UniqueTableReadRva, UniqueTableRead, "the uniqueitems.txt table read in 0x443910"),
        W(UniqueTableStepRva, UniqueTableStep, "the uniqueitems.txt row step in 0x443910"),
        W(UniqueIndexColumnRva, UniqueIndexColumn, "the uniqueitems.txt index column in 0x31F1D0"),
    } };
    for (std::size_t i = 0; i < pins.size(); ++i) {
        const Witness& pin = pins[i];
        // The two getters are only called, so another plugin's entry hook on them is fine.
        if (!Context->CheckExpectedBytes(pin.rva, pin.bytes, pin.size) && !(i < 2 && EntryHooked(pin.rva))) {
            char text[192];
            std::snprintf(text, sizeof(text), "%s at 0x%llX does not match D2RLoader 1.3.1", pin.name, static_cast<unsigned long long>(pin.rva));
            UniqueHostsProblem = text;
            return false;
        }
    }
    ItemQuality      = At<ItemValueFn>(ItemQualityBodyRva);
    ItemFileIndex    = At<ItemValueFn>(ItemFileIndexBodyRva);
    UniqueHostsReady = true;
    return true;
}

auto UsesHostUniques() noexcept -> bool {
    for (const EffectStat& effect : EffectStats) {
        for (const EffectRule& rule : effect.rules) {
            if (!rule.hosts.empty()) return true;
        }
    }
    return false;
}

auto Install() -> bool {
    if (!Verify()) return false;
    BindNatives();
    BuildMasks();
    if (HMODULE core = GetModuleHandleW(L"D2RCore.dll"); core != nullptr) {
        const auto* visit = reinterpret_cast<const std::uint8_t*>(GetProcAddress(core, "VisitWideUnitStats"));
        FullStatsLayout = visit != nullptr && std::memcmp(visit, VisitWideUnitStatsBytes, sizeof(VisitWideUnitStatsBytes)) == 0;
    }
    if (!FullStatsLayout) {
        InstallProblem = "D2RCore!VisitWideUnitStats does not match D2RCore 1.3.1, so the stat layout is unknown";
        Context->LogError("Jeweler: D2RCore!VisitWideUnitStats does not match D2RCore 1.3.1; nothing was installed.");
        return false;
    }
    if (Context->QueryService(&ThreadSvc) != D2RL::ServiceQueryResult::Success
            || !D2RL::HasThreadServiceField(ThreadSvc, D2RL::ThreadServiceRequiredSize)) {
        ThreadSvc = nullptr;
    }
    if (Context->QueryService(&LifecycleSvc) != D2RL::ServiceQueryResult::Success
            || !D2RL::HasLifecycleServiceField(LifecycleSvc, D2RL::LifecycleServiceRequiredSize)) {
        LifecycleSvc = nullptr;
    }
    if (ThreadSvc == nullptr || LifecycleSvc == nullptr) {
        InstallProblem = "D2RLoader's thread or lifecycle service is unavailable";
        Context->LogError("Jeweler: D2RLoader's thread or lifecycle service is unavailable; nothing was installed.");
        return false;
    }
    struct Listener {
        D2RL::Lifecycle::GameplayEventKind     kind;
        D2RL::Lifecycle::GameplayEventCallback callback;
    };
    const std::array<Listener, 3> listeners{ {
        { D2RL::Lifecycle::GameplayEventKind::GameJoined, &OnGameJoined },
        { D2RL::Lifecycle::GameplayEventKind::LocalPlayerReady, &OnLocalPlayerReady },
        { D2RL::Lifecycle::GameplayEventKind::GameLeft, &OnGameLeft },
    } };
    for (const Listener& listener : listeners) {
        const D2RL::Lifecycle::GameplayEventListener registration{
            .structSize = D2RL::Lifecycle::GameplayEventListenerSize,
            .kind       = listener.kind,
            .callback   = listener.callback,
        };
        D2RL::Lifecycle::ListenerHandle handle = D2RL::Lifecycle::InvalidHandle;
        if (LifecycleSvc->registerGameplayEventListener(Context, &registration, &handle) != D2RL::Lifecycle::Result::Success) {
            InstallProblem = "a game join/leave listener was refused";
            Context->LogError("Jeweler: a game join/leave listener was refused; nothing was installed.");
            return false;
        }
    }
    if (Settings.primordial.enabled && !InstallPrimordial()) {
        D2RL::LogWarnF(Context, "Jeweler: Primordial Jewels is off: %s. The other perks still work.", PrimordialProblem.c_str());
    }
    if (Settings.tooltips && !InstallTooltips()) {
        D2RL::LogWarnF(Context, "Jeweler: bonuses will not show in tooltips: %s. They still count.", TooltipProblem.c_str());
    }
    if (!InstallUniqueHosts() && UsesHostUniques()) {
        D2RL::LogWarnF(Context, "Jeweler: Gem Effect's host_uniques is off: %s. A block that names uniques raises nothing; the others still work.",
            UniqueHostsProblem.c_str());
    }
    Ready = true;
    StartChains(Context);
    if (Settings.mirror.enabled && !InstallMirrorUi()) {
        D2RL::LogWarnF(Context, "Jeweler: Mirrored Ring's inventory ghost is off: %s. Its bonus still works.", MirrorUiProblem.c_str());
    }
    return true;
}

// ---------------------------------------------------------------------------
// Console command
// ---------------------------------------------------------------------------
constexpr std::array<const char*, 13> LocNames{ "elsewhere", "head", "amulet", "body armor", "right hand", "left hand",
    "right ring", "left ring", "belt", "boots", "gloves", "swap right hand", "swap left hand" };

auto PerkLine(const char* name, const PerkConfig& perk, std::size_t stats) -> std::string {
    std::string types;
    for (const std::string& code : perk.types) {
        if (!types.empty()) types += ",";
        types += code;
    }
    char text[256];
    std::snprintf(text, sizeof(text), "%s: %s, stat %d, +%d%%, types [%s], %zu stats", name, perk.enabled ? "on" : "off",
        perk.stat, perk.percent, types.c_str(), stats);
    return text;
}

auto CodeText(std::uint32_t code) -> std::string {
    char text[5]{};
    std::memcpy(text, &code, 4);
    for (char& c : text) {
        if (c == ' ') c = '\0';
    }
    return text[0] != '\0' ? std::string(text) : std::string("?");
}

void WriteItems(const D2RL::PluginContext* plugin, const char* sideName, BonusSide& side) {
    std::vector<BonusEntry> copy;
    {
        std::lock_guard lock(side.mutex);
        copy = side.entries;
    }
    char text[1024];
    std::snprintf(text, sizeof(text), "%s: %zu items carry a jeweler bonus", sideName, copy.size());
    plugin->WriteConsoleMessage(text);
    std::size_t lines = 0;
    for (const BonusEntry& entry : copy) {
        if (++lines > 40) {
            plugin->WriteConsoleMessage("  ...");
            break;
        }
        const char* where = entry.loc >= 0 && static_cast<std::size_t>(entry.loc) < LocNames.size() ? LocNames[static_cast<std::size_t>(entry.loc)] : "?";
        std::snprintf(text, sizeof(text), "  item %u %s (%s):%s", entry.id, CodeText(entry.code).c_str(), where,
            Describe(entry.values).c_str());
        plugin->WriteConsoleMessage(text);
    }
}

// For the console: a host_uniques name's row as the last client pass found it
// (-1: no such index, -2: not looked up yet).
auto KnownUniqueRow(const std::string& name) -> std::int32_t {
    const std::int32_t context = EffectContext.load(std::memory_order_relaxed);
    if (!Ready || context < 0) return -2;
    const TypeIds* types = Types(static_cast<std::uint8_t>(context));
    if (types == nullptr) return -2;
    std::lock_guard lock(TypeMutex);
    for (const auto& [known, row] : types->uniques) {
        if (known == name) return row;
    }
    return -2;
}

auto __cdecl StatusCommand(D2R::Game::Client*, const D2RL::ConsoleCommandContext* command, void*) noexcept -> D2RL::ConsoleCommandResult {
    if (command == nullptr || command->plugin == nullptr) return D2RL::ConsoleCommandResult::Failed;
    const auto* plugin = command->plugin;
    const std::string_view args = command->args != nullptr ? Trim(std::string_view(command->args, command->argsLength)) : std::string_view{};
    char text[512];
    if (args == "items") {
        WriteItems(plugin, "Client (tooltips, character sheet)", ClientBonus);
        WriteItems(plugin, "Host (combat)", ServerBonus);
        plugin->WriteConsoleMessage("Values are stat=bonus, or stat:parameter=bonus; the ids are itemstatcost.txt rows.");
        return D2RL::ConsoleCommandResult::Handled;
    }
    std::snprintf(text, sizeof(text), "Jeweler %s: %s%s%s", PluginVersion, Settings.enabled ? "on" : "off",
        InstallProblem.empty() ? (Ready ? ", installed" : "") : ", ", InstallProblem.c_str());
    plugin->WriteConsoleMessage(text);
    std::snprintf(text, sizeof(text), "Tooltips: %s%s%s | items showing their bonus now: %u", !Settings.tooltips ? "off" : TooltipList != nullptr ? "bonuses shown" : "off",
        TooltipProblem.empty() ? "" : ": ", TooltipProblem.c_str(), MergedItemsNow.load());
    plugin->WriteConsoleMessage(text);
    plugin->WriteConsoleMessage(PerkLine("Arcane Infusion", Settings.amulet, StatCounts[0]).c_str());
    plugin->WriteConsoleMessage(PerkLine("Rings of Power", Settings.ring, StatCounts[1]).c_str());
    plugin->WriteConsoleMessage(PerkLine("Amplification", Settings.socketed, StatCounts[2]).c_str());
    std::snprintf(text, sizeof(text), "Prismatic Resonance: %s, stat %d, +%d%% per gem color, %zu colors | your character now has "
        "%u gem colors socketed", Settings.prism.enabled ? "on" : "off", Settings.prism.stat, Settings.prism.perColor,
        Settings.prism.colorTypes.size(), GemColors.load());
    plugin->WriteConsoleMessage(text);
    std::snprintf(text, sizeof(text), "Mirrored Ring: %s, stat %d, +%d%%, ring in slot %d | inventory ghost %s%s%s | your ring is %s | "
        "ghost pickups %llu, swaps %llu, refusals %llu", Settings.mirror.enabled ? "on" : "off", Settings.mirror.stat,
        Settings.mirror.percent, Settings.mirror.ringSlot, MirrorUiInstalled ? "installed" : "off", MirrorUiProblem.empty() ? "" : ": ",
        MirrorUiProblem.c_str(), MirrorActive.load() ? "mirrored" : "not mirrored",
        static_cast<unsigned long long>(MirrorPickups.load()), static_cast<unsigned long long>(MirrorSwaps.load()),
        static_cast<unsigned long long>(MirrorRefusals.load()));
    plugin->WriteConsoleMessage(text);
    std::snprintf(text, sizeof(text), "Six Peas in a Pod: %s, stat %d, +%d%% per gem of the same color in the same item",
        Settings.kin.enabled ? "on" : "off", Settings.kin.stat, Settings.kin.perGem);
    plugin->WriteConsoleMessage(text);
    std::size_t choices[8]{};
    {
        const std::int32_t context = PrimordialContext.load(std::memory_order_relaxed);
        const PrimordialTable* table = PrimordialReady && context >= 0 ? Primordials(static_cast<std::uint8_t>(context)) : nullptr;
        for (std::size_t v = 0; table != nullptr && v < 8; ++v) choices[v] = table->byVariant[v].size();
    }
    std::snprintf(text, sizeof(text), "Primordial Jewels: %s, stat %d, %s%s | choices per sprite 1-8: %zu %zu %zu %zu %zu %zu %zu %zu | "
        "your character has %u socketed jewels, %u with a modifier | applied %llu, removed %llu", Settings.primordial.enabled ? "on" : "off",
        Settings.primordial.stat, PrimordialReady ? "installed" : "off", PrimordialProblem.empty() ? "" : (": " + PrimordialProblem).c_str(),
        choices[0], choices[1], choices[2], choices[3], choices[4], choices[5], choices[6], choices[7], PrimordialJewelsNow.load(),
        PrimordialCarriers.load(),
        static_cast<unsigned long long>(PrimordialApplied.load()), static_cast<unsigned long long>(PrimordialRemoved.load()));
    plugin->WriteConsoleMessage(text);
    std::snprintf(text, sizeof(text), "Steady Pliers: %s, stat %d, +%d%% to gemword modifiers | your character now has %u gemwords",
        Settings.gemword.enabled ? "on" : "off", Settings.gemword.stat, Settings.gemword.percent, Gemwords.load());
    plugin->WriteConsoleMessage(text);
    std::vector<StatValue> effectNow;
    {
        std::lock_guard lock(ClientBonus.mutex);
        effectNow = ClientBonus.effectNow;
    }
    if (Settings.effects.empty()) plugin->WriteConsoleMessage("Gem Effect: no [[gem_effect]] blocks");
    for (std::size_t i = 0; i < Settings.effects.size(); ++i) {
        const GemEffectConfig& effect = Settings.effects[i];
        std::string effectTypes;
        for (const std::string& code : effect.types) {
            if (!effectTypes.empty()) effectTypes += ",";
            effectTypes += code;
        }
        std::string where;
        if (!effect.hosts.empty()) {
            where = ", only in uniques [";
            for (std::size_t n = 0; n < effect.hosts.size(); ++n) {
                if (n != 0) where += ", ";
                where += effect.hosts[n];
                const std::int32_t row = KnownUniqueRow(effect.hosts[n]);
                if (row >= 0) where += " (row " + std::to_string(row) + ")";
                else if (row == -1) where += " (not in uniqueitems.txt)";
            }
            where += "]";
            if (!UniqueHostsReady && Ready) where += " (off: " + UniqueHostsProblem + ")";
        }
        bool own = effect.ownSockets;
        for (const EffectStat& known : EffectStats) {
            if (known.stat == effect.stat) own = known.ownSockets;
        }
        if (own) where += ", own_sockets (each item's own value, for the items socketed in it)";
        if (effect.enabled && effect.stat >= 0 && own) {
            std::snprintf(text, sizeof(text), "Gem Effect %zu: on, stat %d, types [%s]%s | items carrying it now: %d", i + 1, effect.stat,
                effectTypes.c_str(), where.c_str(), Lookup(effectNow, effect.stat, 0));
        } else if (effect.enabled && effect.stat >= 0) {
            std::snprintf(text, sizeof(text), "Gem Effect %zu: on, stat %d, types [%s]%s | your character now has %+d%%", i + 1, effect.stat,
                effectTypes.c_str(), where.c_str(), Lookup(effectNow, effect.stat, 0));
        } else {
            std::snprintf(text, sizeof(text), "Gem Effect %zu: %s, stat %d, types [%s]%s", i + 1, effect.enabled ? "off (stat_id not set)" : "off",
                effect.stat, effectTypes.c_str(), where.c_str());
        }
        plugin->WriteConsoleMessage(text);
    }
    std::string perItem;
    for (const std::int32_t stat : Settings.perItemStats) {
        if (!perItem.empty()) perItem += ",";
        perItem += std::to_string(stat);
    }
    std::snprintf(text, sizeof(text), "Pooled per item: [%s] | your character now pools %u rings and %u jewels and gems",
        perItem.c_str(), PooledRings.load(), PooledGems.load());
    plugin->WriteConsoleMessage(text);
    std::snprintf(text, sizeof(text),
        "Passes: host %llu (%s), client %llu (%s) | lists added %llu, removed %llu, values written %llu",
        static_cast<unsigned long long>(HostPasses.load()), ServerChain.load() != 0 ? "running" : "stopped",
        static_cast<unsigned long long>(ClientPasses.load()), ClientChain.load() != 0 ? "running" : "stopped",
        static_cast<unsigned long long>(BonusListsAdded.load()), static_cast<unsigned long long>(BonusListsRemoved.load()),
        static_cast<unsigned long long>(BonusValuesWritten.load()));
    plugin->WriteConsoleMessage(text);
    plugin->WriteConsoleMessage("\"jeweler items\" lists the bonus on each item of your character.");
    if (!ConfigProblems.empty()) {
        std::snprintf(text, sizeof(text), "Config: %s", ConfigProblems.c_str());
        plugin->WriteConsoleMessage(text);
    }
    return D2RL::ConsoleCommandResult::Handled;
}

constexpr D2RL::PluginInfo Info{
    .infoSize    = D2RL::PluginInfoSize,
    .abiVersion  = D2RL_PLUGIN_ABI_VERSION,
    .id          = "celestialrayone.jeweler",
    .name        = "Jeweler",
    .version     = PluginVersion,
    .author      = "CelestialRayOne",
    .description = "Jeweler specialization perks: Arcane Infusion, Rings of Power, Amplification, Prismatic Resonance, Mirrored Ring, Six Peas in a Pod, Primordial Jewels and Steady Pliers; plus the Gem Effect stats.",
    .flags       = D2RL::PluginFlags::Shared,
};

}  // namespace
}  // namespace CelestialRayOne::Jeweler

using namespace CelestialRayOne::Jeweler;

D2RL_PLUGIN_EXPORT auto D2RLoaderGetPluginInfo() noexcept -> const D2RL::PluginInfo* {
    return &Info;
}

D2RL_PLUGIN_EXPORT auto D2RLoaderLoadPlugin(const D2RL::PluginContext* context) noexcept -> bool {
    if (!D2RL::HasContext(context)) return false;
    Context = context;
    Base = context->exeBase;
    ReadConfiguration();
    if (!context->RegisterConsoleCommand("jeweler", &StatusCommand,
            "Jeweler perks: settings and counters; \"jeweler items\" lists each item's bonus.")) {
        context->LogWarn("Jeweler: the console command could not be registered.");
    }
    if (!Settings.enabled) {
        InstallProblem = "disabled in the config";
        context->LogInfo("Jeweler: disabled in the config; nothing installed.");
        return true;
    }
    if (!Install()) return true;
    std::string effects;
    for (const EffectStat& effect : EffectStats) {
        effects += effects.empty() ? "stats " : ", ";
        effects += std::to_string(effect.stat);
    }
    D2RL::LogInfoF(context, "Jeweler %s: installed (amulet %d%%, rings %d%%, socketed jewels and gems %d%%, gem effect %s).", PluginVersion,
        Settings.amulet.percent, Settings.ring.percent, Settings.socketed.percent, effects.empty() ? "off" : effects.c_str());
    return true;
}

D2RL_PLUGIN_EXPORT void D2RLoaderUnloadPlugin() noexcept {
    ServerChain.store(0);
    ClientChain.store(0);
    if (Context != nullptr && MirrorUiInstalled) RestoreMirrorUi();
    Context = nullptr;
}
