// =============================================================================
//  Archaeology  -  celestialrayone.archaeology
//
//  The Archaeologist perks of the specialization system that need code:
//    Containers Opened  +5% total damage for every container the character
//                       opened in the past 15 seconds
//    Superior Finds     a dropped item whose quality roll ends low or normal
//                       is superior instead 25% of the time
//    Socketed Runes     +5% to the modifiers of every rune socketed in
//                       anything whose stats reach the character, pooled
//    Gold Find          every gold pile dropped for the character holds 20%
//                       more gold, on top of Gold Find
//    Identified Drops   every item dropped for the character is identified
//    Sockets            a dropped equippable item with no socket gets one 50%
//                       of the time
//    Sell Value         a dropped magic weapon or armor piece drops as its
//                       vendor price in gold
//    Coupon Ethereal    an item a coupon cube recipe makes turns ethereal 5%
//                       of the time
//    Mythical Uniques   a dropped item on a mythical base is unique 25% more
//                       often
//    Coupon Upgrade     a dropped normal coupon becomes a random exceptional
//                       one 25% of the time, an exceptional one a random
//                       elite one 15% of the time, chaining
//    Crystal Upgrade    a dropped chipped or flawed crystal is the normal one
//                       2% of the time
//    Magic Runewords    the character can make runewords in magic items
//    Mass Dig           the perk gives the skill (an oskill while the perk
//                       stat is held); its own server skill function runs
//                       Find Item on every monster corpse in a radius
//    Skill Scrolls      listed items cast a skill when used and are used up
//                       anywhere but in town, each entry on its skill's localdelay,
//                       with a sweep on its icon
//  (Archaeology perk 4, +10% quantity of items dropped, is the extra_items
//  stat the perk's node gives; the Item Quantity plugin acts on it.)
//  A perk counts while the character's own value of its stat is above 0.
//  Next to the perks, one stat that is not a perk:
//    Total Gold Found   every gold pile dropped for the character holds the
//                       character's total value of the stat in percent more
//                       gold, added to the Gold Find perk's percent
//
//  Built for D2R 3.3 under D2RLoader 1.3.1 (D2RCore 1.3.1-beta). Every native
//  function the plugin calls or patches was read out of the dumped D2RLoader
//  1.3.1 image (md5 8af7a47ce9077de0378c69af2930854d) and decoded before being
//  relied on. Byte witnesses pin each of them (the function's own body, or the
//  game's own call to it), and the stat array layout is pinned against
//  D2RCore!VisitWideUnitStats. On a different build nothing is installed and
//  the console command says why.
//
// -----------------------------------------------------------------------------
//  CONTAINERS OPENED: the one drop every looted object runs
// -----------------------------------------------------------------------------
//  The object operate table (1423A51C0, 75 slots, indexed by objects.txt
//  OperateFn) calls each object's operate function with an operate block:
//  +00 game, +08 the object, +10 the player who operates it. Every object that
//  drops treasure class loot when opened does it through one function:
//      0x5930B0 (operate, mode): picks the act's chest treasure class for the
//      area level and calls TreasureClassDropCore 0x4404F0 (game, object,
//      player, ...), the player standing in for the killer.
//  Its callers are the operate functions of caskets and sarcophagi (1), urns,
//  jars, baskets and bodies (3), chests (4, ten calls: one per pick), barrels
//  (5), corpses, crates, coffins and cocoons (14), the special chests (39, 40,
//  41, 57, 58, 59), trapped souls (48), the evil urn (68) and the tainted sun
//  altar (24), plus 0x592CF0 (a three-drop chest) and 0x593D90 / 0x5941E0,
//  which rebuild the operate block for the object's operator. Exploding
//  barrels (7), armor stands (19), weapon racks (20) and bookshelves (26) drop
//  their items another way and never reach it.
//  The plugin hooks the entry of 0x5930B0 (inline hook, the loader's own) and,
//  after the original returns, records the object for the player: one record
//  per object (a chest calls it once per pick), stamped with the game frame
//  (game +170h, the counter event 12 0x42EA80 hands to the stat list expiry).
//  A record counts while it is younger than window_seconds x 25 frames.
//
//  The bonus is one plain stat list of the plugin's on the character itself,
//  allocated and attached the way 0x43EF10 gives an item its quest-difficulty
//  list (43F407..43F487):
//      2F7300  alloc (10000h, 0, owner type 0, player id)
//      2F2CB0  attach (player, list, 1)     D2RCore AttachWideStatList
//      2F7C00  set (context, list, stat, value, 0)
//  and removed with 2F7920 unlink + 2F4180 free. D2RCore's attach (+3DA4F0)
//  takes any unit with an extended list and links the list into the chain at
//  ex +90h. D2RCore's death cleanup RemoveWideDeathStats (+3DAD30) frees every
//  list of that chain that an item does not own and whose low flag word has
//  none of 181h, so the list goes when the character dies: every pass checks
//  the kept list is still attached, still carries the plugin flag and still
//  holds exactly what was written before it touches it, and builds a new one
//  when the bonus is still due. Never saved: the character's save writes its
//  own stats, not child lists.
//  The host (the server game thread) owns the count and its list, so combat
//  always has it. The client's character sheet gets a list of its own on the
//  local player, built from the count the host publishes by unit id; that
//  only exists when the host runs in this process (single player, or this PC
//  hosts), as with Alchemy's cooldown sweep.
//
// -----------------------------------------------------------------------------
//  SUPERIOR FINDS: D2GAME_ITEMS_RollItemQuality 0x4421B0
// -----------------------------------------------------------------------------
//  Called only from the treasure class roll (0x440FD9). Its frame (7 pushes,
//  sub rsp,40h, homed arguments) keeps the dropping unit at [rsp+88h], the
//  killer at [rsp+90h] and the item class at [rsp+0A0h]; rdi holds the
//  dropping unit from 44253C/442567/4425D6 on, rsi the itemratio.txt record.
//  Items whose itemtypes.txt row is always normal (+16h) return 2 at 442299
//  and always-magic types (+14h) return 4 at 44256F, so only types the game
//  itself can roll superior get this far:
//      4425DE  mov  ebx,[rsi+30h] ...     ; superior range (HiQuality, divisor)
//      4425F4  call 34A1E0                ; UNITS_GetSeed (dropping unit)
//      4425FE  call 153B00                ; rand(range)
//      442603  cmp  eax,80h
//      442608  jb   44263D                ; superior
//      44260A  mov  ebx,[rsi+38h]         ; <- hook: the normal roll begins
//      44260D  mov  eax,r12d
//      442610  cdq                        ; <- resume
//      ...     normal (442652, 2) or low (442636, 1)
//      44263D  mov  eax,3                 ; superior
//  The six bytes at 44260A (reached only by the fallthrough from 442608, IDA
//  cross references) become a jump to a relay that calls DecideSuperior
//  (dropping unit, killer, class). On 1 it jumps to 44263D; otherwise it
//  replays the two instructions and resumes at 442610. Every volatile
//  register is dead at 44260A, and the relay keeps the frame's alignment. The
//  perk rolls rand(100) on the dropping unit's seed through 0x34A1E0 /
//  0x153B00, exactly as the rolls around it do. The item builder then gives the
//  item its superior modifiers like any superior drop.
//  The killer is credited the way the roll credits magic find: a player
//  killer, or the player owner of a minion through D2GAME_GetMinionOwner
//  0x4A53C0 (the roll's own call at 44237B). For a container the killer is the
//  player who opened it.
//
// -----------------------------------------------------------------------------
//  SOCKETED RUNES: the Jeweler's Amplification, for runes
// -----------------------------------------------------------------------------
//  Socketing (0x46EF30 -> 0x475140) writes a rune's gems.txt modifiers into the
//  rune's own modifier list (0x3D3670 mode 2, list flag 40h); a runeword's own
//  modifiers sit in a separate list on the host (state 171). The rune is then
//  merged into its host, so STATLIST_GetOwner 0x2F8120 on it returns the host.
//  The host's sockets are its own inventory (UNITS_GetInventory 0x34A360 on
//  the item, walked with 0x388C10 / 0x38ABA0). A socketed item is a rune
//  when its items.txt code (record +80h) is one of item_codes, the gems.txt
//  rows named Rune: ESR's crystals (Frozen Soul to Tainted Tourmarine) carry
//  the same misc.txt type "rune", so the item type cannot tell them apart.
//  A host counts while its stats reach the character (0x2F8120 returns the
//  character): equipped gear in the active weapon set, and every other item
//  the game merges into the character. The merge leaves out the inactive
//  weapon set (body locations 11 and 12) and broken items.
//  A rune's modifier value per (stat, layer) is its total (0x2F5020) minus its
//  base (0x2F48C0) minus any plugin bonus list on it, read from its full stats
//  array where D2RCore!VisitWideUnitStats reads it: ex = unit +88h, full stats
//  +A8h, count +B0h, 16-byte entries {layer u32, stat u16 at +4, value i32 at
//  +8}. Per (stat, layer) the numerators value x percent of every rune in a
//  counting host are summed and the bonus is sum / 100, rounded toward zero
//  like the game's own percents. It is split back over the hosts: each gets
//  floor(n / 100), and the points still missing go to the hosts with the
//  largest remainders, so the shares add up to the pooled bonus exactly. Stats
//  that only change their own item (itemstatcost op 13) pool per host. The
//  bonus goes on the host as one plain list of the plugin's (flag 10000h),
//  attached like the character's list above, so the host's tooltip shows it
//  and its stats reach the character exactly when the host's do.
//
//  TOOLTIPS (client): an item's blue lines are drawn from the first list of
//  its chain with state 0 and flag 40h (the finder 0x2F5A00), plus the
//  runeword list (state 171, flag 40h) and that same list of every item in its
//  sockets. A plugin list (10000h) is never drawn, so on the client the bonus
//  is written into the host's magic list, or into its runeword list when it
//  has no magic list (a normal base): the plugin remembers what it added and
//  each touched stat's value right after, and treats its additions as gone the
//  moment the list is a different one or any of those values changed (the
//  client rebuilt the item from a server packet). The server keeps its own
//  separate list, and client units are only ever drawn, never saved.
//
// -----------------------------------------------------------------------------
//  WHO A DROP BELONGS TO (Gold Find, Identified Drops)
// -----------------------------------------------------------------------------
//  TreasureClassDropCore 0x4404F0 has exactly two callers: 0x44148C inside
//  TREASURECLASS_GenerateDrops 0x441300 (game, dropping unit, killer, class,
//  six more arguments), which every monster death drop calls (0x447E17), and
//  0x59333D inside the container drop 0x5930B0. The plugin hooks both entries
//  (inline hooks, the loader's own; 0x4404F0 itself carries Extended Drop
//  Cap's hook) and keeps the credited player in a thread-local for exactly the
//  duration of that drop: a player killer, the player owner of a minion
//  killer through 0x4A53C0, or the player who opened the container. Items made
//  any other way (vendors, gambling, the Horadric Cube, quests) run with none.
//
// -----------------------------------------------------------------------------
//  GOLD FIND: the gold step of the treasure class roll
// -----------------------------------------------------------------------------
//  After the item is spawned (4410C7) a gold pile gets the treasure class
//  entry's multiplier (4410E9..441139), and then, when there is a killer:
//      441165  mov  r14,[rbp-80h]           ; the killer (homed at 44055B)
//      44117B  call 2F5C60 (killer, 79)     ; item_goldbonus, ebx = 100 + it
//      44118F  call 4A53C0 / 2F5C60         ; plus the minion owner's
//      4411B4  call 2F5020 (pile, 14)       ; the pile's gold
//      4411B9  ... imul / 51EB851Fh ...     ; ebx = gold * ebx / 100
//      4411CD  mov eax,ebx / shr eax,1Fh / add ebx,eax / jns 44121E  ; <- hook
//      44121E  ... 441242 mov r8d,ebx ... 4411E0 call 2F7D10 (pile, 14, ebx)
//  The nine bytes at 4411CD (reached only by fallthrough) become a jump to a
//  relay that replays the rounding, calls GoldAmount(gold, killer r14), puts
//  the result back in ebx and rcx = rdi (the pile, as 4411C5 left it), and
//  replays the sign test: 44121E for 0 or more, 4411D6 below. So the perk
//  multiplies the pile after Gold Find, which makes it multiplicative.
//  TOTAL GOLD FOUND: GoldAmount adds the credited character's total value of
//  the stat (0x2F5020 on the character, the way stat('x'.accr) reads it) to
//  the perk's percent, so the two are one multiplier on the pile, rounded
//  once. The sum is floored at -100: a negative value can empty a pile, never
//  take it below 0. A pile is an item, so 44121E writes ebx as it is
//  (0x34B9D0 is the unit type getter; only a player's own gold goes through
//  the 0x34B320 limit).
//
// -----------------------------------------------------------------------------
//  IDENTIFIED DROPS: the end of the item builder 0x43CD50
// -----------------------------------------------------------------------------
//  The spawn helper 0x444680 builds the drop request and calls the item
//  builder 0x43CD50, which rolls the item (0x43EF10, quality and affixes) and,
//  for a request that is not an explicit one (request +3Ch = 0):
//      43D4DF  cmp  dword [rsi+3Ch],0 / jne
//      43D4EA  call 43C910 (item, 0)        ; <- relayed: the item's serial
//      43D4F5  call 43EA10 / 43D500 call 43EAA0, then returns the item
//  The call at 43D4EA goes to a relay that jumps to FinishBuiltItem: the
//  original runs, and for a drop credited to a player with the perk the item
//  gets item flag 10h (identified) through ITEMS_SetItemFlag 0x36D8F0, the
//  setter the builder itself uses for the same flag at 43D1D7. Nothing has
//  been sent to a client yet, so the item drops identified.
//
// -----------------------------------------------------------------------------
//  LIVING NEXT TO THE OTHER PLUGINS
// -----------------------------------------------------------------------------
//  Two plugins cannot patch the same bytes, and a plugin that byte-checks a
//  window refuses to install when another plugin changed anything in it. In
//  0x4421B0 the Blacksmith checks 4421B0..4421D6, 442378..442385 and
//  442404..442453 and patches 442429; Magic Find Soft Tail checks and patches
//  4423CA..4423E5, 442467..442482 and 4424F4..44250F and writes trampolines
//  into the int3 padding at 442673 and 441FBB. This plugin checks
//  4425DE..442659 and patches 44260A..44260F, none of which any of them
//  touches. In 0x4404F0 Item Quantity checks 441157..441160 and hooks 4406A1,
//  the Blacksmith checks 440547..440563 and 4410AD..4410E4 and relays
//  4410C7; this plugin checks 441165..44124A and patches 4411CD..4411D5. In the
//  builder it checks 43D4DF..43D504 and patches 43D4EA..43D4EE. Cube Clone Item checks the cube generator's prologue
//  (5269C0..526A18) and Cow Set Drops the spawn helper's entry (444680..):
//  this plugin changes neither. Coupon Ethereal relays the transmute's call
//  to the generator (4F27BD) instead, and Sell Value, Coupon Upgrade and
//  Crystal Upgrade hook the item builder 0x43CD50 that the spawn helper calls,
//  whose entry nothing else checks (Chronicle Tracker works inside it at
//  43CF4D and 43D195). No published ESR plugin hooks or checks 0x5930B0 (Evil Urn
//  Enhancements works inside the urn's own 0x590960, Shrine Rework inside the
//  shrine functions, Extended Drop Cap on 0x4404F0's entry); the loader
//  refuses a second inline hook on a function, so a clash would show in the
//  console command instead of breaking anything.
//
//  Passes run on D2RLoader's thread service: on the host's game thread every
//  4 game updates for every player in the game (the server client registry),
//  and on the UI thread every 100 ms for the local player.
//
//  Console command "archaeology" shows the settings, what is installed and
//  counters; "archaeology items" lists the bonus on each item and on the
//  character. Settings: d2rloader/config/celestialrayone.archaeology.toml
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

namespace CelestialRayOne::Archaeology {
namespace {

constexpr char PluginVersion[] = "1.6.0";

constexpr const char DefaultConfigToml[] = R"TOML(# celestialrayone.archaeology
#
# Archaeology
#   The Archaeologist perks of the specialization system that need code. A
#   perk counts while the character's own value of its stat is above 0 (the
#   value the specializations plugin writes when the perk is learned). Each
#   perk below names its stat, so the stat ids follow your itemstatcost.txt.
#   Total Gold Found, after Gold Find, is a stat rather than a perk: its value
#   is the percent, from every source.
#
# Built for D2RLoader 1.3.1 (D2R 3.3). Every native site is checked byte for
# byte before anything is written; on any other build the plugin changes
# nothing and the console command "archaeology" says why. "archaeology items"
# lists the bonus on each item and on your character.

[archaeology]

# Master switch. false loads the plugin without changing the game.
enabled = true

# true writes the first perk events to the plugin log (the first 200 lines).
# Keep false for play.
diagnostics = false

# Who a drop belongs to (Superior Finds, Gold Find, Identified Drops): the
# player who got the kill or opened the container. true: a summon's or
# hireling's kill counts for its owner. false: only the player's own kills.
credit_minion_owner = true

# Archaeology perk 4 (+10% quantity of items dropped) needs no setting here:
# the perk's node gives extra_items (426), which Item Quantity acts on.

# Containers Opened (archaeology_1)
#   Every container the character opens adds percent_per_container to each
#   stat in total_damage_stats for window_seconds: three containers opened
#   within the window give three times the bonus. A container counts once,
#   when it is opened and rolls its loot: chests, caskets, sarcophagi, urns,
#   jars, baskets, barrels, bodies, corpses, crates, coffins, the evil urn and
#   the other looted objects. Exploding barrels, armor stands, weapon racks and
#   bookshelves drop their items another way and do not count.
#   The bonus sits on the character itself, as a hidden stat list that is
#   never saved. Combat always uses it. show_on_character_sheet: the character
#   sheet shows it too, when the game's host runs on this PC (single player,
#   or you host the game).
#   total_damage_stats: the stats that make up "total damage", the same ones
#   Unbreakable Gear raises. Each is its own multiplier layer.
[containers]
enabled = true
perk_stat = 976
percent_per_container = 5
window_seconds = 15
show_on_character_sheet = true
total_damage_stats = [
  398,  # generictotalmultiplier
  1012, # item_maxdamage_percent4
  1013, # item_mindamage_percent4
  489,  # total_elemental_min_damage
  493,  # total_elemental_max_damage
  495,  # total_poison_magic_min_damage
  496,  # total_poison_magic_max_damage
]

# Superior Finds (archaeology_2)
#   A dropped item whose quality roll ends in low or normal quality is
#   superior instead chance_percent of the time. The perk steps into the
#   game's own quality roll right after its superior roll fails, so only items
#   the game can roll superior are affected (weapons, armor and any other type
#   without the itemtypes.txt Normal or Magic flag); gems, runes, potions and
#   other always-normal items never are, and neither is anything made by
#   vendors, gambling, the Horadric Cube or quests. The item then gets its
#   superior modifiers (qualityitems.txt) like any superior drop.
#   Drops count for the player who got the kill or opened the container.
[superior_drops]
enabled = true
perk_stat = 977
chance_percent = 25

# Socketed Runes (archaeology_3)
#   Every stat listed in stats is raised by bonus_percent on every rune
#   socketed in anything whose stats reach the character: equipped gear in the
#   active weapon set and anything else the game merges into the character.
#   Only the runes' own modifiers (gems.txt) count, never a runeword's bonus or
#   the host item's own modifiers.
#   Pooling: the runes' values are added up per stat and parameter, the
#   percent is taken once from the sum and rounded once, so nothing is lost to
#   rounding rune by rune: three runes with +7 strength give +1 at 5%, where
#   each alone gives 0. The pooled bonus is split back over the host items so
#   each tooltip shows its own share, and the point the pooling gains goes to
#   the item that lost the most to rounding. Rounding follows the game: toward
#   zero. An equipped item that does not count right now (the inactive weapon
#   set, a broken item) still gets its own bonus but adds nothing to the pool.
#   For a stat with a parameter (a skill, a class, an aura, a proc) only the
#   value rises and the parameter stays. item_charged_skill (204) is never
#   raised: its value packs the charges and the maximum charges.
#   item_codes: the runes that count, by their misc.txt code: the gems.txt rows
#   named Rune. The crystals in gems.txt (Frozen Soul to Tainted Tourmarine,
#   every grade) carry the same misc.txt type "rune", so the item type cannot
#   tell them apart; they are not listed and never raised. Delete a code to
#   leave that rune out.
#   show_in_tooltips = true shows the bonus in the host item's tooltip: the
#   game draws an item's blue lines only from the item's own modifier lists,
#   so on your screen the bonus is written into them (and taken back out the
#   moment the perk or the item no longer counts). The game itself keeps the
#   bonus in a list that is never saved. false keeps it apart on screen too: it
#   still counts, it just does not show.
#   per_item_stats: stats that only change the item they sit in (itemstatcost
#   op 13: enhanced defense, enhanced damage, maximum durability). Pooling
#   never moves them between items: each host pools its own.
#   stats: one itemstatcost.txt id per line; delete a line to stop raising
#   that stat.
[socketed_runes]
enabled = true
perk_stat = 978
bonus_percent = 5
item_codes = [
  "r01", "r02", "r03", "r04", "r05", "r06", "r07", "r08",   # I U Shi Ka N Ku Yo Ki
  "r09", "r10", "r11", "r12", "r13", "r14", "r15", "r16",   # Ri Mi Ya A Tsu Chi Sa Yu
  "r17", "r18", "r19", "r20", "r21", "r22", "r23", "r24",   # Ke E Ko Ra O Ho Me Ru
  "r25", "r26", "r27", "r28", "r29", "r30", "r31", "r32",   # Ta To Wa Ha Na Ni Se Fu
  "r33", "r34", "r35", "r36", "r37", "r38", "r39", "r40",   # Ma Hi Mo No Te Ro So Mu
  "r41", "r42", "r43", "r44", "r45", "r46", "r50", "r51",   # Ne Re Su He Nu Wo Nul El
  "r52", "r53", "r54", "r55", "r56", "r57", "r58", "r59",   # Eld Tir Nef Eth Ith Tal Ral Ort
  "r60", "r61", "r62", "r63", "r64", "r65", "r66", "r67",   # Thul Amn Sol Shael Dol Hel Io Lum
  "r68", "r69", "r70", "r71", "r72", "r73", "r74", "r75",   # Ko Fal Lem Pul Um Mal Ist Gul
  "r76", "r77", "r78", "r79", "r80", "r81", "r82", "r83",   # Vex Ohm Lo Sur Ber Jah Cham Zod
  "r84", "r85", "r86", "r87", "r88", "r89", "r90", "r91",   # Moon Fire Water Wood Metal Earth Sun Thunder
  "r92", "r93", "r94", "r95", "r96", "r97",                 # Wind Dragon Life Death Heaven God
]
show_in_tooltips = true
per_item_stats = [
  16,   # item_armor_percent
  17,   # item_maxdamage_percent
  18,   # item_mindamage_percent
  75,   # item_maxdurability_percent
]
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

# Gold Find (archaeology_5)
#   Every gold pile dropped for the character holds percent more gold. The
#   game first adds Gold Find (item_goldbonus, the killer's and its owner's)
#   and this multiplies the result, so the two multiply: +100% Gold Find and
#   +20% here turn 1000 gold into 2400. Monster kills and opened containers
#   count; gold from anything else (vendors, quests, the cube) does not.
[gold_find]
enabled = true
perk_stat = 980
percent = 20

# Total Gold Found (a stat, not a perk)
#   Every gold pile dropped for the character holds the character's value of
#   this stat in percent more gold. It joins the Gold Find perk above: the
#   two add up and multiply the pile after the game's Gold Find, so the
#   perk's 20 and this stat at 10 make +30%, and +100% Gold Find turns 1000
#   gold into 2600. The value is the character's total from every source
#   (gear, charms, skills), as stat('...'.accr) reads it, so it works without
#   the perk; a minion's kill uses its owner's value when credit_minion_owner
#   is true. Like the perk, only monster kills and opened containers count,
#   and Sell Value piles are left alone. A negative value lowers the gold;
#   lowered by 100% or more in total, a pile holds nothing.
#   stat_id: the itemstatcost.txt row of the stat (0 to 32767). Add the row
#   and put its id here; -1 leaves it off.
[total_gold_found]
enabled = true
stat_id = -1

# Identified Drops (archaeology_6)
#   Every item dropped for the character (monster kills and opened
#   containers) drops identified, so magic, rare, set and unique items show
#   their modifiers on the ground. Items made any other way are untouched.
[identified_drops]
enabled = true
perk_stat = 981

# Magic Runewords (archaeology_13)
#   The character can make runewords in magic items exactly as in normal and
#   superior ones: the game's runeword check (ITEMS_GetRunesTxtRecordFromItem)
#   lets a magic item through while this character socket items. The item
#   keeps its magic modifiers and gains the runeword's. Once made, a magic
#   runeword stays one for everyone (saved, traded, shown).
[magic_runewords]
enabled = true
perk_stat = 988

# Mass Dig (archaeology_14)
#   A server skill function of its own, srvdofunc below (D2R 3.3 fills 0 to
#   171; the plugin puts Mass Dig in this empty slot of the same table). Give
#   the Mass Dig row of skills.txt this srvdofunc. When it is cast, every
#   monster corpse within radius is searched exactly as Find Item searches
#   one, with Find Item's own routines: the corpse must be searchable and not
#   searched before (Find Item and Mass Dig share the mark), the find chance
#   is the Mass Dig row's calc1, and what drops is picked by its Param1 to
#   Param4, so copying Find Item's calc1 and Params gives Find Item's odds.
#   Find Item itself is untouched.
#   radius: in subtiles (the unit of skills.txt aura ranges). -1 uses the
#   Mass Dig row's own aurarangecalc at the skill's level instead, evaluated the
#   way Static Field and the curses evaluate theirs; a row without one searches
#   nothing. Either way the radius stays within 1 to 100.
#   chance_percent: -1 uses the row's calc1; 0 to 100 overrides it.
#   The perk gives the skill: while the character has perk_stat, it holds
#   item_nonclassskill (97) for skill_id at skill_level, the way an item gives
#   an oskill, so the skill shows up and can be bound like one. Nothing is
#   saved; unlearning the perk takes the skill away. skill_id is the Mass Dig
#   row's *Id in skills.txt (0 gives no skill).
[mass_dig]
enabled = true
srvdofunc = 172
radius = 10
chance_percent = -1
perk_stat = 989
skill_id = 0
skill_level = 1

# Skill Scrolls
#   Using one of these items casts a skill and uses the item up, as a scroll
#   is. Each entry is "item code,skill id,skill level", for example
#   scrolls = ["a01,479,1", "a02,623,20"]. The skill is cast the way an
#   item's chance to cast is, so every client plays it (its missiles,
#   overlays and sounds): at the player's target, else at the point he aims
#   at; novas, buffs and summons center on the player. Like any chance-to-cast
#   skill it needs a skills.txt ItemEffect; while it has none, or while the
#   player is uninterruptable, the item can't be used (it isn't used up).
#   Give the item a misc.txt pSpell the
#   client uses at once, with no target cursor (Town Portal's 2 does); the
#   plugin replaces that pSpell's effect and use rule for these items only:
#   they can be used anywhere but in town (levels.txt Town), also where a
#   Town Portal can't be opened. misc.txt UsageConditionCalc still applies.
#   Give the items an item type of their own, not Scroll or Book (nor one
#   with them as Equiv): the game names every normal Scroll or Book after
#   Town Portal or Identify and ignores its NameStr.
#   Cooldown: once used, an entry's items can't be used again until its
#   skill's skills.txt localdelay (at the entry's level) has run out. Every
#   entry has its own cooldown per character, apart from the other entries and
#   from the character's own skills. A skill with no localdelay has none.
#   sweep: while a cooldown runs, a sweep is drawn over that entry's items
#   wherever the game draws their icon, like the Elixir of Reincarnation's:
#   the same sprite, color and opacity by default. HD graphics only.
[skill_scrolls]
enabled = true
scrolls = []
sweep = true
sweep_sprite = "PANEL/CrossbowCharges/Sweep"
sweep_color = [0, 0, 0]
sweep_opacity = 45

# Sockets (archaeology_7)
#   A dropped item of item_types with no socket gets one chance_percent of
#   the time, through the game's own socket routine (ITEMS_AddSockets), so the
#   base's and the quality's socket limits still hold: an item that can take
#   no socket gets none. Items that already rolled sockets keep them.
#   item_types: the equippable types that count (charms are left out):
#   weapons, armor, rings, amulets, the four quiver types, the mercenary
#   helms, the caster and combat scepters and the combat staves.
#   socketed_twins: "base:twin" pairs for bases that cannot hold a socket
#   themselves (items.txt gemsockets 0, no hasinv) but have a socketed twin, as
#   the quivers do: when the roll hits, the twin drops instead, with its
#   socket, exactly what the Adds Socket cube recipes turn them into.
[sockets]
enabled = true
perk_stat = 982
chance_percent = 50
item_types = ["weap", "armo", "ring", "amul", "bowq", "xboq", "arrm", "boqm", "merc", "casce", "cosce", "csta"]
socketed_twins = ["aq2:aq0", "cq2:cq0"]

# Sell Value (archaeology_8)
#   A dropped magic item of item_types drops as a gold pile worth what a
#   vendor pays for it, computed by the game's own vendor price routine (the
#   call selling uses): the item's price, npc.txt "buy mult" of that vendor
#   (256 for every ESR vendor, a quarter of the price) and its "max buy" cap
#   for the difficulty. The pile holds exactly that: neither Gold Find nor the
#   Gold Find perk above is added to it.
#   vendor: the monstats.txt row (hcIdx) of the vendor whose prices count.
#   511 is Larzuk; every ESR vendor has the same buy mult, and the Act 5
#   vendors have the highest max buy (25000 / 50000 / 50000).
[sell_value]
enabled = true
perk_stat = 983
vendor = 511
item_types = ["weap", "armo"]

# Coupon Ethereal (archaeology_9)
#   An item made by a Horadric Cube recipe with a coupon or wild card among
#   its inputs turns ethereal chance_percent of the time. Only items the game
#   itself lets drop ethereal can turn: weapons and armor with durability,
#   not low quality, not set items; jewelry never does. It goes through the
#   game's own ethereal routine, so it gets the ethereal damage or defense
#   and half its maximum durability, like an ethereal drop.
#   coupon_types: input item types that make a recipe a coupon recipe.
#   exclude_item_types: types that never turn ethereal ("miss" is every bow
#   and crossbow).
[coupon_ethereal]
enabled = true
perk_stat = 984
chance_percent = 5
coupon_types = ["coup"]
exclude_item_types = ["miss"]

# Mythical Uniques (archaeology_10)
#   A dropped item on a mythical base is unique bonus_percent more often:
#   after the game's unique roll fails, a second roll on the same seed gives
#   exactly bonus_percent more of the unique chance (capped at 100%). It
#   multiplies with magic find and Relic Hunter.
#   item_codes: the mythical bases (every base named "Mythical", plus mrn
#   and mam).
[mythical_uniques]
enabled = true
perk_stat = 985
bonus_percent = 25
item_codes = [
  "7wd", "m01", "m02", "m03",                         # Sword, Marksman Bow, Odysseus Crossbow, Titan's Maul
  "m04", "m05", "m07", "m08",                         # Demonic Wand, Soul Destroyer, Dimensional Shard, Balanced Dagger
  "m09", "m11", "m12", "m13",                         # Magic Blade, Arcane Staff, Seraphic Scepter, Scissors Suwayyah
  "m14", "m16", "m17", "m18",                         # Ceremonial Dagger, Rapier, Celestial Scepter, Death Blade
  "dd1", "m21", "m22", "m23",                         # Death Blade Frostmourne, Razor Axe, Hellfire Sword, Sepulcher Wand
  "m24", "m25", "m26", "m27",                         # Shade Bow, Marksman Crossbow, Balanced Hatchet, Balanced Dagger
  "m28", "m29", "m30", "m31",                         # Whale Hunter, Petrified Staff, Ancient Bow, Matriarchal Javelin
  "m32", "m33", "m34", "m35",                         # Whale Hunter, Ancient Shards, Shillelagh, Celestial Blade
  "m36", "m37", "m38", "m39",                         # Mage Staff, Katana, Arreat's Hatchet, Kagekiri
  "m40", "m41", "m42", "m43",                         # Slayer Axe, Thorned Club, Blessed Scepter, Dire Flail
  "m44", "m45", "m46", "m47",                         # Military Hammer, Ancient Maul, Jeweled Edge, Ancient Spike
  "m48", "m49", "m50", "m51",                         # Impaler, Death Reaper, Mystic Staff, Raging Knuckle
  "m52", "m53", "m54", "m55",                         # Wrecking Arm, Matriarchal Bow, Grand Matron Bow, Elven Bow
  "m56", "m57", "m58", "m59",                         # Matriarchal Spear, Matriarchal Pike, Runic Talons, Demonic Scythe
  "m61", "m06", "m10", "m15",                         # Abyssal Blade, Gemstoned Armor, Hemp Band, Dream Spirit
  "m20", "m60", "mam", "mrn",                         # Diadem, Light Plate, Amulet, Ring
  "mjw",                                              # Jewel
]

# Coupon Upgrade (archaeology_11)
#   A dropped normal coupon becomes a random exceptional coupon
#   exceptional_chance of the time, and an exceptional coupon (dropped as one
#   or just upgraded) becomes a random elite coupon elite_chance of the time.
#   Every coupon of the tier has the same chance; the tiers are the
#   itemtypes.txt types below (blank coupons are part of their tier, the
#   wild card of none).
[coupon_upgrade]
enabled = true
perk_stat = 986
exceptional_chance = 25
elite_chance = 15
normal_type = "cpn1"
exceptional_type = "cpn2"
elite_type = "cpn3"

# Crystal Upgrade (archaeology_12)
#   A dropped chipped or flawed crystal is the normal crystal of the same kind
#   chance_percent of the time: a code xr* or xl* becomes xt* with the same
#   last character. item_codes: the crystals that can upgrade.
[crystal_upgrade]
enabled = true
perk_stat = 987
chance_percent = 2
item_codes = [
  "xr0", "xr1", "xr2", "xr3", "xr4", "xr5",           # Chipped Crystal 1, Chipped Crystal 2, Chipped Crystal 3, Chipped Crystal 4, Chipped Crystal 5, Chipped Crystal 6
  "xr6", "xr7", "xr8", "xr9", "xrx", "xry",           # Chipped Crystal 7, Chipped Crystal 8, Chipped Crystal 9, Chipped Crystal 10, Chipped Crystal 11, Chipped Crystal 12
  "xl0", "xl1", "xl2", "xl3", "xl4", "xl5",           # Flawed Crystal 1, Flawed Crystal 2, Flawed Crystal 3, Flawed Crystal 4, Flawed Crystal 5, Flawed Crystal 6
  "xl6", "xl7", "xl8", "xl9", "xlx", "xly",           # Flawed Crystal 7, Flawed Crystal 8, Flawed Crystal 9, Flawed Crystal 10, Flawed Crystal 11, Flawed Crystal 12
]
)TOML";

constexpr const char* DefaultRuneCodes[]{ "r01", "r02", "r03", "r04", "r05", "r06", "r07", "r08", "r09", "r10", "r11", "r12", "r13", "r14", "r15", "r16", "r17", "r18", "r19", "r20", "r21", "r22", "r23", "r24", "r25", "r26", "r27", "r28", "r29", "r30", "r31", "r32", "r33", "r34", "r35", "r36", "r37", "r38", "r39", "r40", "r41", "r42", "r43", "r44", "r45", "r46", "r50", "r51", "r52", "r53", "r54", "r55", "r56", "r57", "r58", "r59", "r60", "r61", "r62", "r63", "r64", "r65", "r66", "r67", "r68", "r69", "r70", "r71", "r72", "r73", "r74", "r75", "r76", "r77", "r78", "r79", "r80", "r81", "r82", "r83", "r84", "r85", "r86", "r87", "r88", "r89", "r90", "r91", "r92", "r93", "r94", "r95", "r96", "r97" };

constexpr const char* DefaultMythicalCodes[]{ "7wd", "m01", "m02", "m03", "m04", "m05", "m07", "m08", "m09", "m11", "m12", "m13", "m14", "m16", "m17", "m18", "dd1", "m21", "m22", "m23", "m24", "m25", "m26", "m27", "m28", "m29", "m30", "m31", "m32", "m33", "m34", "m35", "m36", "m37", "m38", "m39", "m40", "m41", "m42", "m43", "m44", "m45", "m46", "m47", "m48", "m49", "m50", "m51", "m52", "m53", "m54", "m55", "m56", "m57", "m58", "m59", "m61", "m06", "m10", "m15", "m20", "m60", "mam", "mrn", "mjw" };
constexpr const char* DefaultCrystalCodes[]{ "xr0", "xr1", "xr2", "xr3", "xr4", "xr5", "xr6", "xr7", "xr8", "xr9", "xrx", "xry", "xl0", "xl1", "xl2", "xl3", "xl4", "xl5", "xl6", "xl7", "xl8", "xl9", "xlx", "xly" };

constexpr std::int32_t DefaultStats[]{ 0, 1, 2, 3, 7, 9, 11, 16, 17, 18, 19, 20, 21, 22, 23, 24, 27, 28, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 48, 49, 50, 51, 52, 53, 54, 55, 57, 58, 60, 62, 67, 68, 74, 76, 77, 78, 79, 80, 83, 85, 86, 87, 89, 93, 96, 97, 99, 102, 105, 107, 109, 110, 111, 112, 114, 116, 119, 120, 121, 122, 123, 124, 126, 127, 128, 134, 135, 136, 138, 141, 142, 143, 144, 145, 146, 147, 148, 149, 150, 151, 155, 156, 158, 159, 160, 182, 187, 188, 189, 190, 191, 192, 193, 195, 196, 197, 198, 200, 201, 214, 216, 217, 218, 219, 220, 221, 222, 223, 224, 225, 226, 227, 228, 229, 230, 231, 232, 233, 238, 239, 240, 242, 244, 247, 250, 255, 256, 257, 258, 265, 266, 267, 287, 288, 303, 329, 330, 331, 332, 333, 334, 335, 336, 338, 339, 340, 358, 366, 371, 372, 373, 374, 376, 377, 378, 379, 391, 392, 393, 394, 398, 400, 404, 405, 409, 413, 415, 422, 423, 426, 434, 435, 436, 466, 467, 482, 483, 486, 489, 493, 495, 496, 498, 499, 500, 501, 502, 505, 506, 516, 519, 520, 522 };
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
constexpr std::int32_t  StatMaxDamagePct    = 17;
constexpr std::int32_t  StatMinDamagePct    = 18;
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

// ---------------------------------------------------------------------------
// Archaeology anchors (D2RLoader 1.3.1 image, D2R 3.3)
// ---------------------------------------------------------------------------
// The container drop 0x5930B0 (operate, mode): push rbp/rsi/rdi/r14, the frame
// and its 0D8h bytes, as the inline hook expects them (see the header).
// 5930B0..5930C2 (18 bytes)
constexpr std::uint8_t ContainerDropEntry[]{
    0x40, 0x55, 0x56, 0x57, 0x41, 0x56, 0x48, 0x8D, 0x6C, 0x24, 0xC1, 0x48, 0x81, 0xEC, 0xD8, 0x00,
    0x00, 0x00,
};
constexpr std::uint64_t ContainerDropRva = 0x5930B0;

// Event 12 0x42EA80: ExpireUnitStatLists(unit, [game+170h], 1.0). Pins the game
// frame counter at game +170h.
// 42EA80..42EA99 (25 bytes)
constexpr std::uint8_t FrameWitness[]{
    0xF3, 0x0F, 0x10, 0x15, 0x7C, 0xBE, 0x88, 0x01, 0x48, 0x8B, 0xC2, 0x8B, 0x91, 0x70, 0x01, 0x00,
    0x00, 0x48, 0x8B, 0xC8, 0xE9, 0x57, 0x98, 0xEC, 0xFF,
};
constexpr std::uint64_t FrameWitnessRva = 0x42EA80;

// The quality roll's minion owner lookup: mov rcx,rbx / call 4A53C0 / test rax,rax / je
// 442378..442385 (13 bytes)
constexpr std::uint8_t QualityOwnerCall[]{
    0x48, 0x8B, 0xCB, 0xE8, 0x40, 0x30, 0x06, 0x00, 0x48, 0x85, 0xC0, 0x74, 0x1A,
};
constexpr std::uint64_t QualityOwnerCallRva = 0x442378;

// D2GAME_ITEMS_RollItemQuality 0x4421B0 from the superior roll to the normal
// quality return: superior range, 34A1E0 / 153B00, jb 44263D; the normal roll
// at 44260A, low 442636, superior 44263D, set 442644, unique 44264B, normal 442652.
// 4425DE..442659 (123 bytes)
constexpr std::uint8_t SuperiorWindow[]{
    0x8B, 0x5E, 0x30, 0x41, 0x8B, 0xC4, 0x99, 0xF7, 0x7E, 0x34, 0x2B, 0xD8, 0xC1, 0xE3, 0x07, 0x85,
    0xDB, 0x7E, 0x4C, 0x48, 0x8B, 0xCF, 0xE8, 0xE7, 0x7B, 0xF0, 0xFF, 0x48, 0x8B, 0xC8, 0x8B, 0xD3,
    0xE8, 0xFD, 0x14, 0xD1, 0xFF, 0x3D, 0x80, 0x00, 0x00, 0x00, 0x72, 0x33, 0x8B, 0x5E, 0x38, 0x41,
    0x8B, 0xC4, 0x99, 0xF7, 0x7E, 0x3C, 0x2B, 0xD8, 0xC1, 0xE3, 0x07, 0x85, 0xDB, 0x7E, 0x35, 0x48,
    0x8B, 0xCF, 0xE8, 0xBB, 0x7B, 0xF0, 0xFF, 0x48, 0x8B, 0xC8, 0x8B, 0xD3, 0xE8, 0xD1, 0x14, 0xD1,
    0xFF, 0x3D, 0x80, 0x00, 0x00, 0x00, 0x72, 0x1C, 0xB8, 0x01, 0x00, 0x00, 0x00, 0xEB, 0x1E, 0xB8,
    0x03, 0x00, 0x00, 0x00, 0xEB, 0x17, 0xB8, 0x05, 0x00, 0x00, 0x00, 0xEB, 0x10, 0xB8, 0x07, 0x00,
    0x00, 0x00, 0xEB, 0x09, 0xB8, 0x02, 0x00, 0x00, 0x00, 0xEB, 0x02,
};
constexpr std::uint64_t SuperiorWindowRva = 0x4425DE;
constexpr std::uint64_t SuperiorSiteRva   = 0x44260A;  // mov ebx,[rsi+38h] / mov eax,r12d: the normal roll begins
constexpr std::uint64_t SuperiorResumeRva = 0x442610;  // cdq
constexpr std::uint64_t SuperiorHitRva    = 0x44263D;  // mov eax,3 / jmp to the epilogue
constexpr std::uint32_t SuperiorSiteSize  = 6;
constexpr std::uint8_t  SuperiorSiteStock[]{ 0x8B, 0x5E, 0x38, 0x41, 0x8B, 0xC4 };
constexpr std::uint64_t UnitSeedRva       = 0x34A1E0;  // UNITS_GetSeed (unit) -> unit +28h
constexpr std::uint64_t RollRandomRva     = 0x153B00;  // (seed, range) -> [0, range)
constexpr std::uint64_t MinionOwnerRva    = 0x4A53C0;  // D2GAME_GetMinionOwner (unit) -> owner or null

constexpr auto CallTarget(const std::uint8_t* bytes, std::uint64_t windowRva, std::uint64_t callRva) noexcept -> std::uint64_t {
    const std::size_t at = static_cast<std::size_t>(callRva - windowRva);
    const std::uint32_t raw = static_cast<std::uint32_t>(bytes[at + 1]) | static_cast<std::uint32_t>(bytes[at + 2]) << 8
        | static_cast<std::uint32_t>(bytes[at + 3]) << 16 | static_cast<std::uint32_t>(bytes[at + 4]) << 24;
    return static_cast<std::uint64_t>(static_cast<std::int64_t>(callRva + 5) + static_cast<std::int32_t>(raw));
}
static_assert(sizeof(SuperiorWindow) == 0x442659 - 0x4425DE);
static_assert(SuperiorWindow[0x4425F4 - SuperiorWindowRva] == 0xE8 && CallTarget(SuperiorWindow, SuperiorWindowRva, 0x4425F4) == UnitSeedRva);
static_assert(SuperiorWindow[0x4425FE - SuperiorWindowRva] == 0xE8 && CallTarget(SuperiorWindow, SuperiorWindowRva, 0x4425FE) == RollRandomRva);
static_assert(QualityOwnerCall[3] == 0xE8 && CallTarget(QualityOwnerCall, QualityOwnerCallRva, 0x44237B) == MinionOwnerRva);
static_assert(SuperiorWindow[SuperiorSiteRva - SuperiorWindowRva] == 0x8B && SuperiorWindow[SuperiorSiteRva - SuperiorWindowRva + 5] == 0xC4);
static_assert(SuperiorWindow[SuperiorResumeRva - SuperiorWindowRva] == 0x99);
static_assert(SuperiorWindow[SuperiorHitRva - SuperiorWindowRva] == 0xB8 && SuperiorWindow[SuperiorHitRva - SuperiorWindowRva + 1] == 0x03);
static_assert(SuperiorWindow[0x442608 - SuperiorWindowRva] == 0x72
    && 0x442608 + 2 + SuperiorWindow[0x442608 - SuperiorWindowRva + 1] == SuperiorHitRva);

// Relay entered by the jump at 44260A, inside 0x4421B0's frame (rsp 16-aligned):
//   sub rsp,20h / mov rcx,rdi / mov rdx,[rsp+0B0h] (killer) / mov r8d,[rsp+0C0h] (class)
//   call [slot DecideSuperior] / add rsp,20h / test eax,eax / jne hit
//   mov ebx,[rsi+38h] / mov eax,r12d / jmp [slot 442610] / hit: jmp [slot 44263D]
// Assembled with keystone, decoded back with capstone; each rip relative
// operand lands on its own qword slot after the code.
constexpr std::uint8_t SuperiorRelay[]{
    0x48, 0x83, 0xEC, 0x20, 0x48, 0x89, 0xF9, 0x48, 0x8B, 0x94, 0x24, 0xB0, 0x00, 0x00, 0x00, 0x44,
    0x8B, 0x84, 0x24, 0xC0, 0x00, 0x00, 0x00, 0xFF, 0x15, 0x1B, 0x00, 0x00, 0x00, 0x48, 0x83, 0xC4,
    0x20, 0x85, 0xC0, 0x75, 0x0C, 0x8B, 0x5E, 0x38, 0x44, 0x89, 0xE0, 0xFF, 0x25, 0x0F, 0x00, 0x00,
    0x00, 0xFF, 0x25, 0x11, 0x00, 0x00, 0x00, 0x90, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};
constexpr std::size_t SuperiorRelaySlotDecide = 56;
constexpr std::size_t SuperiorRelaySlotResume = 64;
constexpr std::size_t SuperiorRelaySlotHit    = 72;
static_assert(sizeof(SuperiorRelay) == 80);

// TREASURECLASS_GenerateDrops 0x441300: push rbx/rbp/rsi/rdi/r12-r15, sub rsp,98h
// 441300..441314 (20 bytes)
constexpr std::uint8_t GenerateDropsEntry[]{
    0x40, 0x53, 0x55, 0x56, 0x57, 0x41, 0x54, 0x41, 0x55, 0x41, 0x56, 0x41, 0x57, 0x48, 0x81, 0xEC,
    0x98, 0x00, 0x00, 0x00,
};
constexpr std::uint64_t GenerateDropsRva = 0x441300;

// The gold step of the treasure class roll, from the killer load to the
// pile's final stat write (see GOLD FIND in the header).
// 441165..44124B (230 bytes)
constexpr std::uint8_t GoldWindow[]{
    0x4C, 0x8B, 0x75, 0x80, 0x4D, 0x85, 0xF6, 0x74, 0x77, 0x48, 0x8B, 0xCF, 0xE8, 0x1A, 0x1B, 0xF3,
    0xFF, 0x83, 0xF8, 0x04, 0x75, 0x6A, 0x45, 0x33, 0xC0, 0x8D, 0x50, 0x4B, 0x49, 0x8B, 0xCE, 0xE8,
    0xD7, 0x4A, 0xEB, 0xFF, 0x49, 0x8B, 0xCE, 0x8D, 0x58, 0x64, 0xE8, 0x2C, 0x42, 0x06, 0x00, 0x48,
    0x85, 0xC0, 0x74, 0x11, 0x45, 0x33, 0xC0, 0x48, 0x8B, 0xC8, 0x41, 0x8D, 0x50, 0x4F, 0xE8, 0xB8,
    0x4A, 0xEB, 0xFF, 0x03, 0xD8, 0x45, 0x33, 0xC0, 0x48, 0x8B, 0xCF, 0x41, 0x8D, 0x50, 0x0E, 0xE8,
    0x67, 0x3E, 0xEB, 0xFF, 0x8B, 0xC8, 0xB8, 0x1F, 0x85, 0xEB, 0x51, 0x0F, 0xAF, 0xCB, 0xF7, 0xE9,
    0x48, 0x8B, 0xCF, 0x8B, 0xDA, 0xC1, 0xFB, 0x05, 0x8B, 0xC3, 0xC1, 0xE8, 0x1F, 0x03, 0xD8, 0x79,
    0x48, 0x45, 0x33, 0xC9, 0x45, 0x33, 0xC0, 0x41, 0x8D, 0x51, 0x0E, 0xE8, 0x2B, 0x6B, 0xEB, 0xFF,
    0x48, 0x8B, 0x74, 0x24, 0x50, 0x4C, 0x8B, 0x74, 0x24, 0x38, 0x83, 0xBC, 0x35, 0x88, 0x00, 0x00,
    0x00, 0x00, 0xBA, 0x01, 0x00, 0x00, 0x00, 0x44, 0x0F, 0xB6, 0x7C, 0x24, 0x30, 0x49, 0xB8, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x07, 0x49, 0xB9, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0x3F, 0x0F, 0x85, 0xF4, 0xF4, 0xFF, 0xFF, 0xEB, 0x42, 0xE8, 0xAD, 0xA7, 0xF0, 0xFF, 0x85, 0xC0,
    0x75, 0x1B, 0x48, 0x8B, 0xCF, 0xE8, 0xF1, 0xA0, 0xF0, 0xFF, 0x3B, 0xD8, 0x7E, 0x0F, 0x45, 0x33,
    0xC9, 0x48, 0x8B, 0xCF, 0x45, 0x33, 0xC0, 0x41, 0x8D, 0x51, 0x0E, 0xEB, 0x9E, 0x45, 0x33, 0xC9,
    0x44, 0x8B, 0xC3, 0x48, 0x8B, 0xCF,
};
constexpr std::uint64_t GoldWindowRva   = 0x441165;
constexpr std::uint64_t GoldSiteRva     = 0x4411CD;  // mov eax,ebx / shr eax,1Fh / add ebx,eax / jns 44121E
constexpr std::uint32_t GoldSiteSize    = 9;
constexpr std::uint8_t  GoldSiteStock[]{ 0x8B, 0xC3, 0xC1, 0xE8, 0x1F, 0x03, 0xD8, 0x79, 0x48 };
constexpr std::uint64_t GoldNegativeRva = 0x4411D6;  // the pile gets 0
constexpr std::uint64_t GoldPositiveRva = 0x44121E;  // the pile gets ebx
static_assert(sizeof(GoldWindow) == 0x44124B - 0x441165);
static_assert(GoldWindow[GoldSiteRva - GoldWindowRva] == 0x8B && GoldWindow[GoldSiteRva - GoldWindowRva + 7] == 0x79
    && GoldSiteRva + 9 + GoldWindow[GoldSiteRva - GoldWindowRva + 8] == GoldPositiveRva);
static_assert(GoldWindow[0x441165 - GoldWindowRva] == 0x4C && GoldWindow[0x441168 - GoldWindowRva] == 0x80);  // mov r14,[rbp-80h]
static_assert(CallTarget(GoldWindow, GoldWindowRva, 0x44118F) == 0x4A53C0);
static_assert(GoldWindow[0x44117E - GoldWindowRva] == 0x8D && GoldWindow[0x441180 - GoldWindowRva] == 0x4B);  // lea edx,[rax+4Bh]: stat 79

// Relay entered by the jump at 4411CD, inside 0x4404F0's frame (rsp 16-aligned):
//   mov eax,ebx / shr eax,1Fh / add ebx,eax (replayed) / sub rsp,20h / mov ecx,ebx
//   mov rdx,r14 (killer) / mov r8,rdi (the pile) / call [slot GoldAmount] / add rsp,20h
//   mov ebx,eax / mov rcx,rdi
//   test ebx,ebx / jns positive / jmp [slot 4411D6] / positive: jmp [slot 44121E]
constexpr std::uint8_t GoldRelay[]{
    0x89, 0xD8, 0xC1, 0xE8, 0x1F, 0x01, 0xC3, 0x48, 0x83, 0xEC, 0x20, 0x89, 0xD9, 0x4C, 0x89, 0xF2,
    0x49, 0x89, 0xF8, 0xFF, 0x15, 0x1F, 0x00, 0x00, 0x00, 0x48, 0x83, 0xC4, 0x20, 0x89, 0xC3, 0x48,
    0x89, 0xF9, 0x85, 0xDB, 0x79, 0x06, 0xFF, 0x25, 0x14, 0x00, 0x00, 0x00, 0xFF, 0x25, 0x16, 0x00,
    0x00, 0x00, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};
constexpr std::size_t GoldRelaySlotAmount   = 56;
constexpr std::size_t GoldRelaySlotNegative = 64;
constexpr std::size_t GoldRelaySlotPositive = 72;
static_assert(sizeof(GoldRelay) == 80);

// The end of the item builder 0x43CD50 for a request that is not explicit:
// cmp [rsi+3Ch],0 / jne / xor edx,edx / mov rcx,rdi / call 43C910 / then 43EA10, 43EAA0.
// 43D4DF..43D505 (38 bytes)
constexpr std::uint8_t BuilderWindow[]{
    0x83, 0x7E, 0x3C, 0x00, 0x75, 0x0A, 0x33, 0xD2, 0x48, 0x8B, 0xCF, 0xE8, 0x21, 0xF4, 0xFF, 0xFF,
    0x48, 0x8B, 0xD7, 0x49, 0x8B, 0xCF, 0xE8, 0x16, 0x15, 0x00, 0x00, 0x48, 0x8B, 0xD7, 0x49, 0x8B,
    0xCF, 0xE8, 0x9B, 0x15, 0x00, 0x00,
};
constexpr std::uint64_t BuilderWindowRva = 0x43D4DF;
constexpr std::uint64_t FinishCallRva    = 0x43D4EA;  // call 43C910 (item, 0)
constexpr std::uint8_t  FinishCallStock[]{ 0xE8, 0x21, 0xF4, 0xFF, 0xFF };
constexpr std::uint64_t FinishItemRva    = 0x43C910;
static_assert(sizeof(BuilderWindow) == 0x43D505 - 0x43D4DF);
static_assert(CallTarget(BuilderWindow, BuilderWindowRva, FinishCallRva) == FinishItemRva);

// Relay entered by the call at 43D4EA: rcx and edx are FinishBuiltItem's.
//   jmp [slot FinishBuiltItem]
constexpr std::uint8_t IdentifyRelay[]{
    0xFF, 0x25, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};
constexpr std::size_t IdentifyRelaySlot = 8;

// ITEMS_SetItemFlag (item, flag, value), as Material Upgrade pins it
// 36D8F0..36D90C (28 bytes)
constexpr std::uint8_t ItemFlagEntry[]{
    0x48, 0x89, 0x5C, 0x24, 0x10, 0x48, 0x89, 0x74, 0x24, 0x18, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x41,
    0x8B, 0xF0, 0x8B, 0xDA, 0x48, 0x8B, 0xF9, 0x48, 0x85, 0xC9, 0x74, 0x0A,
};
constexpr std::uint64_t ItemFlagRva    = 0x36D8F0;
constexpr std::uint32_t IdentifiedFlag = 0x10;

// The item builder 0x43CD50 (game, request, flag), which the spawn helper
// 0x444680 calls for every treasure class item (444866):
// push rbp,rbx,rsi,r12,r15 / lea rbp,[rsp-37h] / sub rsp,90h. Its entry is hooked,
// not the spawn helper's: Cow Set Drops checks the spawn helper's entry.
// 43CD50..43CD64 (20 bytes)
constexpr std::uint8_t BuilderEntry[]{
    0x40, 0x55, 0x53, 0x56, 0x41, 0x54, 0x41, 0x57, 0x48, 0x8D, 0x6C, 0x24, 0xC9, 0x48, 0x81, 0xEC,
    0x90, 0x00, 0x00, 0x00,
};
constexpr std::uint64_t BuilderRva = 0x43CD50;
// The drop request the spawn helper fills: +00h the dropping unit, +20h the
// class, +40h the quality, +50h the set or unique row. A copy of D0h bytes
// covers every field the builder reads (the helper's request ends at +C8h).
constexpr std::size_t RequestSource  = 0x00;
constexpr std::size_t RequestClass   = 0x20;
constexpr std::size_t RequestQuality = 0x40;
constexpr std::size_t RequestRow     = 0x50;
constexpr std::size_t RequestSize    = 0xD0;

// The transmute 0x4F2490 calling the cube generator 0x5269C0 (game, player,
// cubemain record, match). The call is relayed, not the generator's entry:
// Cube Clone Item checks the generator's prologue.
//   mov edx,[rsp+58h] / movzx ecx,byte [r13+106h] / call 3E3F60 (the record)
//   mov r8,rax / lea r9,[rsp+60h] / mov rdx,r14 / mov rcx,r13 / call 5269C0
// 4F279E..4F27C2 (36 bytes)
constexpr std::uint8_t CubeCallWindow[]{
    0x8B, 0x54, 0x24, 0x58, 0x41, 0x0F, 0xB6, 0x8D, 0x06, 0x01, 0x00, 0x00, 0xE8, 0xB1, 0x17, 0xEF,
    0xFF, 0x4C, 0x8B, 0xC0, 0x4C, 0x8D, 0x4C, 0x24, 0x60, 0x49, 0x8B, 0xD6, 0x49, 0x8B, 0xCD, 0xE8,
    0xFE, 0x41, 0x03, 0x00,
};
constexpr std::uint64_t CubeCallWindowRva = 0x4F279E;
constexpr std::uint64_t CubeCallRva       = 0x4F27BD;
constexpr std::uint8_t  CubeCallStock[]{ 0xE8, 0xFE, 0x41, 0x03, 0x00 };
constexpr std::uint64_t CubeRva           = 0x5269C0;
static_assert(CallTarget(CubeCallWindow, CubeCallWindowRva, CubeCallRva) == CubeRva);

// The transmute check 0x4F2490 counting the cube's items: anim mode 34AB60
// == 0 and item page 36CFE0 == 3. 4F2610..4F262B (27 bytes)
constexpr std::uint8_t CubePageWindow[]{
    0x48, 0x8B, 0xCB, 0xE8, 0x48, 0x85, 0xE5, 0xFF, 0x85, 0xC0, 0x75, 0x0F, 0x48, 0x8B, 0xCB, 0xE8,
    0xBC, 0xA9, 0xE7, 0xFF, 0x3C, 0x03, 0x75, 0x03, 0x48, 0xFF, 0xC6,
};
constexpr std::uint64_t CubePageWindowRva = 0x4F2610;
constexpr std::uint64_t AnimModeRva       = 0x34AB60;
constexpr std::uint64_t ItemPageRva       = 0x36CFE0;
constexpr std::uint8_t  CubePage          = 3;
static_assert(CallTarget(CubePageWindow, CubePageWindowRva, 0x4F2613) == AnimModeRva);
static_assert(CallTarget(CubePageWindow, CubePageWindowRva, 0x4F261F) == ItemPageRva);

// The builder's socket step for an explicit request: item flags 36CBA0,
// ITEMS_GetMaxSockets 36EAD0, ITEMS_AddSockets 375560. 43D228..43D26D (69 bytes)
constexpr std::uint8_t SocketWindow[]{
    0x48, 0x8B, 0xCF, 0xE8, 0x70, 0xF9, 0xF2, 0xFF, 0x8B, 0xC8, 0xBA, 0x00, 0x08, 0x00, 0x00, 0xE8,
    0xB4, 0x86, 0xF3, 0xFF, 0x85, 0xC0, 0x75, 0x3C, 0x48, 0x8B, 0xCF, 0xE8, 0x88, 0x18, 0xF3, 0xFF,
    0x48, 0x8B, 0xCF, 0x84, 0xC0, 0x74, 0x20, 0xE8, 0x7C, 0x18, 0xF3, 0xFF, 0x48, 0x8B, 0xCF, 0x0F,
    0xB6, 0xD8, 0xE8, 0x91, 0xFC, 0xF2, 0xFF, 0x33, 0xD2, 0x48, 0x8B, 0xCF, 0xF7, 0xF3, 0xFF, 0xC2,
    0xE8, 0xF3, 0x82, 0xF3, 0xFF,
};
constexpr std::uint64_t SocketWindowRva = 0x43D228;
constexpr std::uint64_t ItemFlagsRva    = 0x36CBA0;
constexpr std::uint64_t MaxSocketsRva   = 0x36EAD0;
constexpr std::uint64_t AddSocketsRva   = 0x375560;
static_assert(sizeof(SocketWindow) == 0x43D26D - 0x43D228);
static_assert(CallTarget(SocketWindow, SocketWindowRva, 0x43D22B) == ItemFlagsRva);
static_assert(CallTarget(SocketWindow, SocketWindowRva, 0x43D243) == MaxSocketsRva);
static_assert(CallTarget(SocketWindow, SocketWindowRva, 0x43D268) == AddSocketsRva);

// The builder freeing an item it could not finish: mov rdx,rdi / mov rcx,r15 /
// call 48FAA0 (game, unit). 43D4A4..43D4AF (11 bytes)
constexpr std::uint8_t FreeUnitWindow[]{ 0x48, 0x8B, 0xD7, 0x49, 0x8B, 0xCF, 0xE8, 0xF1, 0x25, 0x05, 0x00 };
constexpr std::uint64_t FreeUnitWindowRva = 0x43D4A4;
constexpr std::uint64_t FreeUnitRva       = 0x48FAA0;
static_assert(CallTarget(FreeUnitWindow, FreeUnitWindowRva, 0x43D4AA) == FreeUnitRva);

// The vendor sell handler 0x53F1A0 pricing an item: player data 34B240,
// difficulty game+104h, quest flags [data + difficulty*8 + 40h], the vendor's
// class, transaction type 1, ITEMS_GetTransactionCost 36F0B0.
// 53F2B5..53F2EB (54 bytes)
constexpr std::uint8_t SellWindow[]{
    0x48, 0x8B, 0x4C, 0x24, 0x58, 0x8B, 0xF0, 0xE8, 0x7F, 0xBF, 0xE0, 0xFF, 0x41, 0x0F, 0xB6, 0x8C,
    0x24, 0x04, 0x01, 0x00, 0x00, 0x49, 0x8B, 0xD6, 0x44, 0x8B, 0xC1, 0xC7, 0x44, 0x24, 0x28, 0x01,
    0x00, 0x00, 0x00, 0x89, 0x74, 0x24, 0x20, 0x4C, 0x8B, 0x4C, 0xC8, 0x40, 0x48, 0x8B, 0x4C, 0x24,
    0x58, 0xE8, 0xC5, 0xFD, 0xE2, 0xFF,
};
constexpr std::uint64_t SellWindowRva    = 0x53F2B5;
constexpr std::uint64_t PlayerDataRva    = 0x34B240;
constexpr std::uint64_t TransactionRva   = 0x36F0B0;
constexpr std::size_t   GameDifficulty   = 0x104;
constexpr std::size_t   PlayerQuestFlags = 0x40;
constexpr std::int32_t  TransactionSell  = 1;
static_assert(sizeof(SellWindow) == 0x53F2EB - 0x53F2B5);
static_assert(CallTarget(SellWindow, SellWindowRva, 0x53F2BC) == PlayerDataRva);
static_assert(CallTarget(SellWindow, SellWindowRva, 0x53F2E6) == TransactionRva);
static_assert(SellWindow[0x53F2D0 - SellWindowRva + 3] == 0x28 && SellWindow[0x53F2D0 - SellWindowRva + 4] == TransactionSell);

// The item roll's ethereal gate: ITEMS_GetItemQuality 36CF60 (< 10), the
// version 3730D0 (>= 100), request flag 2, weapon (45) or any armor (50).
// 44329A..4432EB (81 bytes)
constexpr std::uint8_t EtherealGateWindow[]{
    0x48, 0x8B, 0x0B, 0xE8, 0xBE, 0x9C, 0xF2, 0xFF, 0x83, 0xF8, 0x0A, 0x0F, 0x8D, 0x5D, 0xFB, 0xFF,
    0xFF, 0x48, 0x8B, 0x0B, 0xE8, 0x1D, 0xFE, 0xF2, 0xFF, 0x66, 0x83, 0xF8, 0x64, 0x0F, 0x82, 0x9B,
    0x02, 0x00, 0x00, 0xF6, 0x85, 0xC0, 0x00, 0x00, 0x00, 0x02, 0x48, 0x8B, 0x3B, 0x0F, 0x85, 0x8B,
    0x02, 0x00, 0x00, 0xBA, 0x2D, 0x00, 0x00, 0x00, 0x48, 0x8B, 0xCF, 0xE8, 0xB6, 0x05, 0xF3, 0xFF,
    0x85, 0xC0, 0x75, 0x13, 0x8D, 0x50, 0x32, 0x48, 0x8B, 0xCF, 0xE8, 0xA7, 0x05, 0xF3, 0xFF, 0x85,
    0xC0,
};
constexpr std::uint64_t EtherealGateWindowRva = 0x44329A;
constexpr std::uint64_t ItemQualityRva        = 0x36CF60;
constexpr std::uint64_t ItemVersionRva        = 0x3730D0;
static_assert(sizeof(EtherealGateWindow) == 0x4432EB - 0x44329A);
static_assert(CallTarget(EtherealGateWindow, EtherealGateWindowRva, 0x44329D) == ItemQualityRva);
static_assert(CallTarget(EtherealGateWindow, EtherealGateWindowRva, 0x4432AE) == ItemVersionRva);

// The drop's ethereal roll and application: ITEMS_GetItemSeed 36CC80, rand,
// then (after the Blacksmith's site at 4434DB) the ethereal routine 3D3510,
// has durability 373540, and 2F48C0 / 2F7940 / 2F5020 halving durability.
// 4434C9..4434DB (18 bytes) and 4434FC..443558 (92 bytes)
constexpr std::uint8_t ItemSeedWindow[]{
    0xE8, 0xB2, 0x97, 0xF2, 0xFF, 0x48, 0x8B, 0xC8, 0xBA, 0x64, 0x00, 0x00, 0x00, 0xE8, 0x25, 0x06,
    0xD1, 0xFF,
};
constexpr std::uint64_t ItemSeedWindowRva = 0x4434C9;
constexpr std::uint64_t ItemSeedRva       = 0x36CC80;
static_assert(CallTarget(ItemSeedWindow, ItemSeedWindowRva, 0x4434C9) == ItemSeedRva);
static_assert(CallTarget(ItemSeedWindow, ItemSeedWindowRva, 0x4434D6) == RollRandomRva);
constexpr std::uint8_t EtherealApplyWindow[]{
    0x48, 0x8B, 0xCF, 0xE8, 0x0C, 0x00, 0xF9, 0xFF, 0x48, 0x8B, 0xCF, 0xE8, 0x34, 0x00, 0xF3, 0xFF,
    0x85, 0xC0, 0x74, 0x48, 0x45, 0x33, 0xC0, 0x48, 0x8B, 0xCF, 0x41, 0x8D, 0x50, 0x49, 0xE8, 0xA1,
    0x13, 0xEB, 0xFF, 0x99, 0x45, 0x33, 0xC9, 0x2B, 0xC2, 0x48, 0x8B, 0xCF, 0xD1, 0xF8, 0x41, 0x8D,
    0x51, 0x49, 0x44, 0x8D, 0x40, 0x01, 0xE8, 0x09, 0x44, 0xEB, 0xFF, 0x45, 0x33, 0xC0, 0x48, 0x8B,
    0xCF, 0x41, 0x8D, 0x50, 0x49, 0xE8, 0xDA, 0x1A, 0xEB, 0xFF, 0x45, 0x33, 0xC9, 0x44, 0x8B, 0xC0,
    0x48, 0x8B, 0xCF, 0x41, 0x8D, 0x51, 0x48, 0xE8, 0xE8, 0x43, 0xEB, 0xFF,
};
constexpr std::uint64_t EtherealApplyWindowRva = 0x4434FC;
constexpr std::uint64_t ApplyEtherealRva       = 0x3D3510;
constexpr std::uint64_t HasDurabilityRva       = 0x373540;
constexpr std::uint64_t SetUnitStatRva         = 0x2F7940;
static_assert(sizeof(EtherealApplyWindow) == 0x443558 - 0x4434FC);
static_assert(CallTarget(EtherealApplyWindow, EtherealApplyWindowRva, 0x4434FF) == ApplyEtherealRva);
static_assert(CallTarget(EtherealApplyWindow, EtherealApplyWindowRva, 0x443507) == HasDurabilityRva);
static_assert(CallTarget(EtherealApplyWindow, EtherealApplyWindowRva, 0x44351A) == 0x2F48C0);
static_assert(CallTarget(EtherealApplyWindow, EtherealApplyWindowRva, 0x443532) == SetUnitStatRva);
static_assert(CallTarget(EtherealApplyWindow, EtherealApplyWindowRva, 0x443541) == 0x2F5020);
static_assert(CallTarget(GoldWindow, GoldWindowRva, 0x4411E0) == 0x2F7D10);  // the pile's gold write
constexpr std::uint64_t StatWriteRva      = 0x2F7D10;
constexpr std::uint32_t EtherealFlag      = 0x400000;
constexpr std::uint32_t SocketedFlag      = 0x800;
constexpr std::int32_t  StatGold          = 14;
constexpr std::int32_t  StatDurability    = 72;
constexpr std::int32_t  StatMaxDurability = 73;
constexpr std::int32_t  QualityLow = 1, QualityNormal = 2, QualityMagic = 4, QualitySet = 5;
constexpr std::size_t   ItemType1 = 0x12E, ItemType2 = 0x130;   // items.txt record: the two types (int16)

// The set roll right after the unique roll failed (the Blacksmith's window
// ends at 442453): mov ebx,[rsi+18h] / mov eax,r12d / cdq / idiv [rsi+1Ch] ...
// 442453..442467 (20 bytes)
constexpr std::uint8_t MythicalWindow[]{
    0x8B, 0x5E, 0x18, 0x41, 0x8B, 0xC4, 0x99, 0xF7, 0x7E, 0x1C, 0x2B, 0xD8, 0xC1, 0xE3, 0x07, 0x45,
    0x85, 0xF6, 0x74, 0x27,
};
constexpr std::uint64_t MythicalWindowRva = 0x442453;
constexpr std::uint64_t MythicalSiteRva   = 0x442453;
constexpr std::uint64_t MythicalResumeRva = 0x442459;
constexpr std::uint64_t UniqueHitRva      = 0x44264B;  // mov eax,7
constexpr std::uint32_t MythicalSiteSize  = 6;
constexpr std::uint8_t  MythicalSiteStock[]{ 0x8B, 0x5E, 0x18, 0x41, 0x8B, 0xC4 };
static_assert(SuperiorWindow[UniqueHitRva - SuperiorWindowRva] == 0xB8 && SuperiorWindow[UniqueHitRva - SuperiorWindowRva + 1] == 0x07);
static_assert(MythicalWindow[MythicalResumeRva - MythicalWindowRva] == 0x99);

// Relay entered by the jump at 442453, inside 0x4421B0's frame:
//   sub rsp,20h / mov ecx,ebx (unique range) / mov rdx,[rsp+0A8h] (dropping unit)
//   mov r8,[rsp+0B0h] (killer) / mov r9d,[rsp+0C0h] (class) / call [slot MythicalRoll]
//   add rsp,20h / test eax,eax / jne hit / mov ebx,[rsi+18h] / mov eax,r12d
//   jmp [slot 442459] / hit: jmp [slot 44264B]
constexpr std::uint8_t MythicalRelay[]{
    0x48, 0x83, 0xEC, 0x20, 0x89, 0xD9, 0x48, 0x8B, 0x94, 0x24, 0xA8, 0x00, 0x00, 0x00, 0x4C, 0x8B,
    0x84, 0x24, 0xB0, 0x00, 0x00, 0x00, 0x44, 0x8B, 0x8C, 0x24, 0xC0, 0x00, 0x00, 0x00, 0xFF, 0x15,
    0x1C, 0x00, 0x00, 0x00, 0x48, 0x83, 0xC4, 0x20, 0x85, 0xC0, 0x75, 0x0C, 0x8B, 0x5E, 0x18, 0x44,
    0x89, 0xE0, 0xFF, 0x25, 0x10, 0x00, 0x00, 0x00, 0xFF, 0x25, 0x12, 0x00, 0x00, 0x00, 0x90, 0x90,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};
constexpr std::size_t MythicalRelaySlotRoll   = 64;
constexpr std::size_t MythicalRelaySlotResume = 72;
constexpr std::size_t MythicalRelaySlotHit    = 80;
static_assert(sizeof(MythicalRelay) == 88);
constexpr std::int32_t UniqueThreshold = 128;  // the roll's "rand(range) < 128"

// The server's socket insertion 0x46EB00 (game, player, socketable id, host
// id, flag), called by the insert-socket handler 0x4F9F40 at 0x4FA297:
// push rbx,rbp,rsi,rdi,r14 / sub rsp,70h. 46EB00..46EB0B (11 bytes)
constexpr std::uint8_t SocketInsertEntry[]{ 0x40, 0x53, 0x55, 0x56, 0x57, 0x41, 0x56, 0x48, 0x83, 0xEC, 0x70 };
constexpr std::uint64_t SocketInsertRva = 0x46EB00;

// ITEMS_GetRunesTxtRecordFromItem 0x372260 (item in rdi): ItemData through
// 34A500, and its quality gate: qualities 4 to 9 go through the jump table to
// "return 0", everything else continues at 3722E4.
// 3722C2..3722D3 (17 bytes) and 37246C..37248F (35 bytes)
constexpr std::uint8_t MatcherItemData[]{
    0x48, 0x8B, 0xCF, 0xE8, 0x36, 0x82, 0xFD, 0xFF, 0x48, 0x85, 0xC0, 0x0F, 0x85, 0x99, 0x01, 0x00, 0x00,
};
constexpr std::uint64_t MatcherItemDataRva = 0x3722C2;
constexpr std::uint8_t RunewordGate[]{
    0x8B, 0x00, 0x83, 0xE8, 0x04, 0x83, 0xF8, 0x05, 0x0F, 0x87, 0x6A, 0xFE, 0xFF, 0xFF, 0x48, 0x8D,
    0x15, 0x7F, 0xDB, 0xC8, 0xFF, 0x48, 0x98, 0x8B, 0x8C, 0x82, 0x38, 0x26, 0x37, 0x00, 0x48, 0x03,
    0xCA, 0xFF, 0xE1,
};
constexpr std::uint64_t RunewordGateRva    = 0x37246C;  // mov eax,[rax] / sub eax,4 / cmp eax,5 / ja 3722E4
constexpr std::uint64_t RunewordSwitchRva  = 0x37247A;  // the jump table for qualities 4 to 9
constexpr std::uint64_t RunewordGoOnRva    = 0x3722E4;  // the rune match
constexpr std::uint32_t RunewordSiteSize   = 14;
constexpr std::uint32_t RunewordFlag       = 0x4000000;
static_assert(CallTarget(MatcherItemData, MatcherItemDataRva, 0x3722C5) == 0x34A500);
static_assert(MatcherItemDataRva + 17 + 0x199 == RunewordGateRva);
static_assert(RunewordGateRva + 14 + static_cast<std::int32_t>(0xFFFFFE6A) == RunewordGoOnRva);

// Relay entered by the jump at 37246C, rax = ItemData, rdi = the item:
//   mov eax,[rax] / cmp eax,4 / jne range / push rbx / mov rbx,rsp / and rsp,-16
//   sub rsp,20h / mov rcx,rdi / call [slot AllowMagicRuneword] / mov rsp,rbx / pop rbx
//   test eax,eax / jne go_on / mov eax,4 / range: sub eax,4 / cmp eax,5 / ja go_on
//   jmp [slot 37247A] / go_on: jmp [slot 3722E4]
constexpr std::uint8_t RunewordRelay[]{
    0x8B, 0x00, 0x83, 0xF8, 0x04, 0x75, 0x22, 0x53, 0x48, 0x89, 0xE3, 0x48, 0x83, 0xE4, 0xF0, 0x48,
    0x83, 0xEC, 0x20, 0x48, 0x89, 0xF9, 0xFF, 0x15, 0x24, 0x00, 0x00, 0x00, 0x48, 0x89, 0xDC, 0x5B,
    0x85, 0xC0, 0x75, 0x13, 0xB8, 0x04, 0x00, 0x00, 0x00, 0x83, 0xE8, 0x04, 0x83, 0xF8, 0x05, 0x77,
    0x06, 0xFF, 0x25, 0x11, 0x00, 0x00, 0x00, 0xFF, 0x25, 0x13, 0x00, 0x00, 0x00, 0x90, 0x90, 0x90,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};
constexpr std::size_t RunewordRelaySlotAllow  = 64;
constexpr std::size_t RunewordRelaySlotSwitch = 72;
constexpr std::size_t RunewordRelaySlotGoOn   = 80;
static_assert(sizeof(RunewordRelay) == 88);

// The server skill do-dispatch (0x43ACB0) calls table 0x238EA00 [skills.txt
// srvdofunc] with (game, unit, skill id, level) and no bounds check: 0 is
// empty, 1 to 171 hold the native functions, 172 to 200 are empty.
constexpr std::uint64_t SrvDoTableRva   = 0x238EA00;
constexpr std::int32_t  FirstFreeSrvDo  = 172;
constexpr std::int32_t  LastFreeSrvDo   = 200;
constexpr std::uint8_t  SrvDoDispatch[]{
    0x48, 0x63, 0xC1, 0x48, 0x8D, 0x0D, 0x07, 0x51, 0xBC, 0xFF, 0x4C, 0x8B, 0x94, 0xC1, 0x00, 0xEA,
    0x38, 0x02, 0x4D, 0x85, 0xD2, 0x74, 0x17, 0x44, 0x8B, 0x8C, 0x24, 0xD8, 0x00, 0x00, 0x00, 0x45,
    0x8B, 0xC5, 0x48, 0x8B, 0xD3, 0x48, 0x8B, 0xCF, 0x41, 0xFF, 0xD2,
};
constexpr std::uint64_t SrvDoDispatchRva = 0x43AEEF;   // movsxd rax,ecx / lea rcx,[base] / mov r10,[rcx+rax*8+238EA00h] / ... / call r10

// Find Item's server function 0x567D00 (srvdofunc 72) after its prologue:
// skills record 97790, target 48FE20, corpse check 33A5C0, state 118 (3351B0
// / 3354C0), 38EBF0, calc1 (record +190h) through 3B5160, two rand(100) on the
// caster's seed, Param1..4 (record +1B8h..+1C4h), the find drop 446E80.
// 567D2E..567E37 (265 bytes)
constexpr std::uint8_t FindTreasureWindow[]{
    0xE8, 0x5D, 0xFA, 0xB2, 0xFF, 0x48, 0x8B, 0xF8, 0x48, 0x85, 0xC0, 0x0F, 0x84, 0x22, 0x01, 0x00,
    0x00, 0x48, 0x8B, 0xD5, 0x49, 0x8B, 0xCE, 0xE8, 0xD6, 0x80, 0xF2, 0xFF, 0x48, 0x8B, 0xF0, 0x48,
    0x85, 0xC0, 0x0F, 0x84, 0x0B, 0x01, 0x00, 0x00, 0x48, 0x8B, 0xC8, 0xE8, 0x62, 0x28, 0xDD, 0xFF,
    0x85, 0xC0, 0x0F, 0x84, 0xFB, 0x00, 0x00, 0x00, 0xBA, 0x76, 0x00, 0x00, 0x00, 0x48, 0x8B, 0xCE,
    0xE8, 0x3D, 0xD4, 0xDC, 0xFF, 0x85, 0xC0, 0x0F, 0x85, 0xE6, 0x00, 0x00, 0x00, 0x8D, 0x50, 0x76,
    0x48, 0x8B, 0xCE, 0x44, 0x8D, 0x40, 0x01, 0xE8, 0x36, 0xD7, 0xDC, 0xFF, 0x48, 0x8B, 0xCE, 0xE8,
    0x5E, 0x6E, 0xE2, 0xFF, 0x44, 0x8B, 0x87, 0x90, 0x01, 0x00, 0x00, 0x44, 0x8B, 0xCB, 0x41, 0x0F,
    0xB6, 0x8E, 0x06, 0x01, 0x00, 0x00, 0x48, 0x8B, 0xD5, 0x44, 0x89, 0x7C, 0x24, 0x20, 0xE8, 0xAF,
    0xD3, 0xE4, 0xFF, 0x48, 0x8B, 0xCD, 0x8B, 0xD8, 0xE8, 0x25, 0x24, 0xDE, 0xFF, 0x48, 0x8B, 0xC8,
    0xBA, 0x64, 0x00, 0x00, 0x00, 0xE8, 0x38, 0xBD, 0xBE, 0xFF, 0x3B, 0xC3, 0x0F, 0x8D, 0x91, 0x00,
    0x00, 0x00, 0x48, 0x8B, 0xCD, 0xE8, 0x08, 0x24, 0xDE, 0xFF, 0x48, 0x8B, 0xC8, 0xBA, 0x64, 0x00,
    0x00, 0x00, 0xE8, 0x1B, 0xBD, 0xBE, 0xFF, 0x8B, 0x97, 0xB8, 0x01, 0x00, 0x00, 0x44, 0x8B, 0xC0,
    0x44, 0x8B, 0x97, 0xBC, 0x01, 0x00, 0x00, 0x44, 0x03, 0xD2, 0x3B, 0xC2, 0x7C, 0x42, 0x41, 0x3B,
    0xC2, 0x7D, 0x3D, 0x8B, 0x87, 0xC0, 0x01, 0x00, 0x00, 0x41, 0xB9, 0x02, 0x00, 0x00, 0x00, 0x41,
    0x03, 0xC2, 0x8B, 0xD0, 0x44, 0x3B, 0xC0, 0x7C, 0x12, 0x03, 0x97, 0xC4, 0x01, 0x00, 0x00, 0xB9,
    0x04, 0x00, 0x00, 0x00, 0x44, 0x3B, 0xC2, 0x44, 0x0F, 0x4C, 0xC9, 0x4C, 0x8B, 0xC5, 0x48, 0x8B,
    0xD6, 0x49, 0x8B, 0xCE, 0xE8, 0x49, 0xF0, 0xED, 0xFF,
};
constexpr std::uint64_t FindTreasureWindowRva = 0x567D2E;
constexpr std::uint64_t SkillsRecordRva   = 0x097790;  // (context, skill id)
constexpr std::uint64_t CorpseUsableRva   = 0x33A5C0;  // (unit)
constexpr std::uint64_t HasStateRva       = 0x3351B0;  // (unit, state)
constexpr std::uint64_t SetStateRva       = 0x3354C0;  // (unit, state, on)
constexpr std::uint64_t CorpseRefreshRva  = 0x38EBF0;  // (unit)
constexpr std::uint64_t SkillCalcRva      = 0x3B5160;  // (context, unit, calc, skill id, level)
constexpr std::uint64_t FindDropRva       = 0x446E80;  // (game, corpse, caster, kind)
constexpr std::int32_t  StateSearched     = 118;
constexpr std::size_t   SkillCalc1        = 0x190;
// aurarangecalc: Static Field's handler 0x5546B0 evaluates the calc at record
// +84h through 3B5160 and passes it to the area search as the radius (its row's
// aurarangecalc is min(ln12/3,30)); aurastatcalc4 sits at +94h (Hex Debuff
// Installer), the same column order. 0 means the column is empty.
constexpr std::size_t   SkillAuraRangeCalc = 0x84;
constexpr std::size_t   SkillParam1       = 0x1B8;
static_assert(CallTarget(FindTreasureWindow, FindTreasureWindowRva, 0x567D2E) == SkillsRecordRva);
static_assert(CallTarget(FindTreasureWindow, FindTreasureWindowRva, 0x567D59) == CorpseUsableRva);
static_assert(CallTarget(FindTreasureWindow, FindTreasureWindowRva, 0x567D6E) == HasStateRva);
static_assert(CallTarget(FindTreasureWindow, FindTreasureWindowRva, 0x567D85) == SetStateRva);
static_assert(CallTarget(FindTreasureWindow, FindTreasureWindowRva, 0x567D8D) == CorpseRefreshRva);
static_assert(CallTarget(FindTreasureWindow, FindTreasureWindowRva, 0x567DAC) == SkillCalcRva);
static_assert(CallTarget(FindTreasureWindow, FindTreasureWindowRva, 0x567E32) == FindDropRva);

// The area search 0x4327D0 (game, unit, dx, dy, radius, filter, callback,
// context, 0, file, line) Static Field uses; the callback gets {game, caster,
// count, context} and the unit. Filter 2 | 1000h: monsters in their dead mode
// (12), as the filter 0x439C10 reads it.
// 4327D0..4327E4 (20 bytes)
constexpr std::uint8_t AreaSearchEntry[]{
    0x48, 0x89, 0x5C, 0x24, 0x10, 0x48, 0x89, 0x6C, 0x24, 0x18, 0x48, 0x89, 0x74, 0x24, 0x20, 0x57,
    0x48, 0x83, 0xEC, 0x60,
};
constexpr std::uint64_t AreaSearchRva = 0x4327D0;

constexpr std::int32_t  CorpseFilter  = 0x1002;
constexpr std::int32_t  StatNonClassSkill = 97;  // item_nonclassskill: an oskill, layer = skill id

// D2GAME_SKILLS_ServerDoHandler 0x43ACB0 (game, unit, skill id, level,
// consume resources, item cast, item effect). 43ACB0..43ACC8 (24 bytes)
constexpr std::uint8_t ServerDoEntry[]{
    0x48, 0x89, 0x5C, 0x24, 0x10, 0x44, 0x89, 0x4C, 0x24, 0x20, 0x44, 0x89, 0x44, 0x24, 0x18, 0x55,
    0x56, 0x57, 0x41, 0x54, 0x41, 0x55, 0x41, 0x56,
};
constexpr std::uint64_t ServerDoRva = 0x43ACB0;

// The item use effect 0x581680 (game, player, item): the item in rbx, the use
// callback table (16 slots) through r9, called with (game, player, target).
// 58176C..581795 (41 bytes)
constexpr std::uint8_t UseDispatchWindow[]{
    0x48, 0x8B, 0x44, 0x24, 0x50, 0x4C, 0x8D, 0x0D, 0x18, 0xD3, 0x7B, 0x01, 0x41, 0x80, 0xFE, 0xFF,
    0x4C, 0x8B, 0xC3, 0x48, 0x8B, 0xD5, 0x48, 0x8B, 0xCE, 0x4D, 0x0F, 0x45, 0xC7, 0x4D, 0x8B, 0x0C,
    0xC1, 0x41, 0xFF, 0xD1, 0x4C, 0x8B, 0x74, 0x24, 0x58,
};
constexpr std::uint64_t UseDispatchWindowRva = 0x58176C;
constexpr std::uint64_t UseCallRva           = 0x581789;  // mov r9,[r9+rax*8] / call r9
constexpr std::uint8_t  UseCallStock[]{ 0x4D, 0x8B, 0x0C, 0xC1, 0x41, 0xFF, 0xD1 };
static_assert(UseDispatchWindow[UseCallRva - UseDispatchWindowRva] == 0x4D && UseDispatchWindow[UseCallRva - UseDispatchWindowRva + 6] == 0xD1);

// Relay entered by the call at 581789 (replaces the callback load and call):
//   mov r9,[r9+rax*8] / sub rsp,28h / mov [rsp+20h],rbx (the item)
//   call [slot UseDispatch] / add rsp,28h / ret
constexpr std::uint8_t ScrollRelay[]{
    0x4D, 0x8B, 0x0C, 0xC1, 0x48, 0x83, 0xEC, 0x28, 0x48, 0x89, 0x5C, 0x24, 0x20, 0xFF, 0x15, 0x05,
    0x00, 0x00, 0x00, 0x48, 0x83, 0xC4, 0x28, 0xC3, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};
constexpr std::size_t ScrollRelaySlot = 24;

// Where each relay sits in the plugin's relay page.
constexpr std::size_t SuperiorRelayOffset = 0x000;
constexpr std::size_t GoldRelayOffset     = 0x080;
constexpr std::size_t IdentifyRelayOffset = 0x100;
constexpr std::size_t MythicalRelayOffset = 0x180;
constexpr std::size_t RunewordRelayOffset = 0x200;
constexpr std::size_t ScrollRelayOffset   = 0x280;
constexpr std::size_t SweepRelayOffset      = 0x400;
constexpr std::size_t LocalDelayRelayOffset = 0x480;
constexpr std::size_t UseRuleRelayOffset    = 0x4C0;

// Artefact use rule and cooldowns (Skill Scrolls)
// 5817F0 (game, player, item, target) -> bool is the use check every item use
// request passes before the item is taken out of the belt or inventory; its
// one caller is the use request handler 4F40C0, so a refusal leaves the item
// where it is. After the generic checks (quantity, target, misc.txt
// UsageConditionCalc) it calls the item's pSpell rule from the table at
// 1D3EA30 with (game, player, item), or the target in place of the item when
// the misc record's byte +144h is not FFh. Town Portal's rule (pSpell 2,
// 46FA70) refuses in town, in a level whose record +184h is set and where no
// portal fits; otherwise it is what opens the portal: it creates the portal
// pair (D2GAME_CreatePortalObject 432CE0, object 59) and stores the new
// portal's unit id in the player data at +164. The pSpell 2 use effect 470AE0
// only finishes it (the old portal removed, owner set, both ends activated).
// For an artefact the relay at 58195B runs the artefact's own rule instead,
// so no portal is made: not in town, and its cooldown not running. 581808..581967 is checked whole, because the relay
// relies on it: rbp = player, rsi = game, r14 = target, r15b = misc +144h,
// and rbx = the item, written once at 581819 and only read until the cmovne.
constexpr std::uint64_t UseRuleWindowRva = 0x581808;
constexpr std::uint8_t  UseRuleWindow[]{
    0x48, 0x8B, 0xEA, 0x48, 0x8B, 0xF1, 0xBA, 0x12, 0x00, 0x00, 0x00, 0x49, 0x8B, 0xC8, 0x4D, 0x8B,
    0xF1, 0x49, 0x8B, 0xD8, 0xE8, 0x6F, 0x20, 0xDF, 0xFF, 0x85, 0xC0, 0x74, 0x17, 0x45, 0x33, 0xC0,
    0x48, 0x8B, 0xCB, 0x41, 0x8D, 0x50, 0x46, 0xE8, 0xEC, 0x37, 0xD7, 0xFF, 0x85, 0xC0, 0x0F, 0x8E,
    0xC3, 0x00, 0x00, 0x00, 0x0F, 0xB6, 0x8E, 0x06, 0x01, 0x00, 0x00, 0x48, 0x8B, 0xD3, 0xE8, 0x95,
    0xF2, 0xD7, 0xFF, 0x48, 0x8B, 0xF8, 0x48, 0x85, 0xC0, 0x0F, 0x84, 0xA8, 0x00, 0x00, 0x00, 0x8B,
    0x00, 0x85, 0xC0, 0x0F, 0x88, 0x9E, 0x00, 0x00, 0x00, 0x83, 0xF8, 0x0B, 0x0F, 0x83, 0x95, 0x00,
    0x00, 0x00, 0x41, 0xB8, 0x96, 0x03, 0x00, 0x00, 0x48, 0x8D, 0x15, 0x39, 0xCF, 0x7B, 0x01, 0x48,
    0x8B, 0xCB, 0xE8, 0xE1, 0x7F, 0xDC, 0xFF, 0x0F, 0xB6, 0x8E, 0x06, 0x01, 0x00, 0x00, 0x8B, 0xD0,
    0xE8, 0x83, 0x28, 0xD9, 0xFF, 0x44, 0x0F, 0xB6, 0xB8, 0x44, 0x01, 0x00, 0x00, 0x41, 0x80, 0xFF,
    0xFF, 0x0F, 0x95, 0xC1, 0x4D, 0x85, 0xF6, 0x0F, 0x95, 0xC0, 0x3A, 0xC8, 0x75, 0x59, 0x41, 0xB8,
    0x9B, 0x03, 0x00, 0x00, 0x48, 0x8D, 0x15, 0xFD, 0xCE, 0x7B, 0x01, 0x48, 0x8B, 0xCB, 0xE8, 0xA5,
    0x7F, 0xDC, 0xFF, 0x0F, 0xB6, 0x8E, 0x06, 0x01, 0x00, 0x00, 0x8B, 0xD0, 0xE8, 0x47, 0x28, 0xD9,
    0xFF, 0x48, 0x85, 0xC0, 0x74, 0x4C, 0x44, 0x8B, 0x88, 0x08, 0x01, 0x00, 0x00, 0x41, 0x83, 0xF9,
    0xFF, 0x74, 0x3F, 0x0F, 0xB6, 0x8E, 0x06, 0x01, 0x00, 0x00, 0x4C, 0x8B, 0xC3, 0x48, 0x8B, 0xD5,
    0xE8, 0x93, 0x2F, 0xE3, 0xFF, 0x85, 0xC0, 0x75, 0x29, 0x8D, 0x50, 0x14, 0x45, 0x33, 0xC0, 0x48,
    0x8B, 0xCD, 0xE8, 0x61, 0x00, 0xF1, 0xFF, 0x32, 0xC0, 0x48, 0x8B, 0x5C, 0x24, 0x50, 0x48, 0x8B,
    0x6C, 0x24, 0x58, 0x48, 0x8B, 0x74, 0x24, 0x60, 0x48, 0x83, 0xC4, 0x30, 0x41, 0x5F, 0x41, 0x5E,
    0x5F, 0xC3, 0x48, 0x63, 0x07, 0x48, 0x89, 0x44, 0x24, 0x20, 0x48, 0x83, 0xF8, 0x0B, 0x72, 0x19,
    0x48, 0x8D, 0x44, 0x24, 0x20, 0x48, 0x8D, 0x4C, 0x24, 0x28, 0x48, 0x89, 0x44, 0x24, 0x28, 0xE8,
    0xC4, 0xFA, 0xFF, 0xFF, 0x84, 0xC0, 0x74, 0x01, 0xCC, 0x48, 0x8B, 0x44, 0x24, 0x20, 0x4C, 0x8D,
    0x0D, 0xE3, 0xD0, 0x7B, 0x01, 0x41, 0x80, 0xFF, 0xFF, 0x48, 0x8B, 0xD5, 0x48, 0x8B, 0xCE, 0x49,
    0x0F, 0x45, 0xDE, 0x4D, 0x8B, 0x0C, 0xC1, 0x4C, 0x8B, 0xC3, 0x41, 0xFF, 0xD1, 0xEB, 0x9A,
};
constexpr std::uint64_t UseRuleSiteRva = 0x58195B;   // mov r9,[r9+rax*8] / mov r8,rbx / call r9; then jmp 581901
constexpr std::uint8_t  UseRuleStock[]{ 0x4D, 0x8B, 0x0C, 0xC1, 0x4C, 0x8B, 0xC3, 0x41, 0xFF, 0xD1 };
static_assert(UseRuleWindow[UseRuleSiteRva - UseRuleWindowRva] == 0x4D && UseRuleWindow[UseRuleSiteRva - UseRuleWindowRva + 9] == 0xD1);
static_assert(sizeof(UseRuleWindow) == 0x581967 - UseRuleWindowRva);
// UNITS_GetRoom (unit) -> room, and DUNGEON_IsRoomInTown (room), null-safe:
// room +18h to the DRLG room, then the level record's Town field (levels.txt
// Town). The same pair the monster regen tick uses; names from ruff's corpus.
constexpr std::uint64_t UnitRoomRva   = 0x34B440;
constexpr std::uint8_t  UnitRoomEntry[]{
    0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B, 0xD9, 0x48, 0x85, 0xC9, 0x75, 0x13, 0x88, 0x4C,
    0x24, 0x30, 0x48, 0x8D, 0x4C, 0x24, 0x30, 0xE8, 0x54, 0xA7, 0xFF, 0xFF, 0x84, 0xC0, 0x74, 0x01,
};
constexpr std::uint64_t RoomInTownRva = 0x2F0750;
constexpr std::uint8_t  RoomInTownEntry[]{
    0x48, 0x83, 0xEC, 0x28, 0x48, 0x85, 0xC9, 0x75, 0x07, 0x33, 0xC0, 0x48, 0x83, 0xC4, 0x28, 0xC3,
    0x48, 0x8B, 0x49, 0x18, 0xE8, 0x57, 0x08, 0x07, 0x00, 0x8B, 0xC8, 0x48, 0x83, 0xC4, 0x28, 0xE9,
    0xDC, 0x70, 0x03, 0x00,
};
// 43B1B3..43B1C3: the server skill handler's localdelay call for players
// (43AD17 sets its player byte from UnitType alone, so item casts get it too):
//   mov r9d,r13d (skill id) / mov r8d,eax (frames) / mov rdx,rbx (unit) /
//   mov rcx,rdi (game) / call 439500, the skill cooldown setter. Shadow Warrior
//   Fixes reads 43B192..43B1BE and Crossbow Charges relays 43B18D; neither
//   touches the call at 43B1BF.
constexpr std::uint64_t LocalDelayWindowRva = 0x43B1B3;
constexpr std::uint8_t  LocalDelayWindow[]{
    0x45, 0x8B, 0xCD, 0x44, 0x8B, 0xC0, 0x48, 0x8B, 0xD3, 0x48, 0x8B, 0xCF, 0xE8, 0x3C, 0xE3, 0xFF,
    0xFF,
};
constexpr std::uint64_t LocalDelayCallRva   = 0x43B1BF;
constexpr std::uint8_t  LocalDelayStock[]{ 0xE8, 0x3C, 0xE3, 0xFF, 0xFF };
constexpr std::uint64_t LocalDelaySetRva    = 0x439500;  // (game, unit, frames, skill id)
constexpr std::size_t   SkillLocalDelayCalc = 0x238;     // skills.txt localdelay, compiled to a calc
static_assert(LocalDelayWindow[LocalDelayCallRva - LocalDelayWindowRva] == 0xE8);

// 15BCD0..15BCF8 in the item icon renderer 15BB80, the one renderer of every
// item icon (inventory, belt, stash, cube), on its HD path: the icon's draw
// call 0C7020 (rcx = the item from rbp, rdx = the icon's sprite from its ring
// slot [rdi+r14+18h], r8 = x | y << 32 from rbx, xmm3 = the scale from xmm6),
// then mov ecx,[2A8AF28] / mov eax,10624DD3h. Every volatile register is
// written again before it is read, so the relay at 15BCED may use them all.
// Alchemy hooks 15BB80's entry; this window is untouched by it.
constexpr std::uint64_t IconDrawnWindowRva = 0x15BCD0;
constexpr std::uint8_t  IconDrawnWindow[]{
    0x4A, 0x8B, 0x54, 0x37, 0x18, 0x48, 0x8D, 0x44, 0x24, 0x38, 0x0F, 0x28, 0xDE, 0x48, 0x89, 0x44,
    0x24, 0x20, 0x4C, 0x8B, 0xC3, 0x48, 0x8B, 0xCD, 0xE8, 0x33, 0xB3, 0xF6, 0xFF, 0x8B, 0x0D, 0x35,
    0xF2, 0x92, 0x02, 0xB8, 0xD3, 0x4D, 0x62, 0x10,
};
constexpr std::uint64_t IconDrawnSiteRva = 0x15BCED;   // mov ecx,[2A8AF28], the ring's slot index
constexpr std::uint8_t  IconDrawnStock[]{ 0x8B, 0x0D, 0x35, 0xF2, 0x92, 0x02 };
constexpr std::uint64_t IconSlotIndexRva = 0x2A8AF28;
static_assert(IconDrawnWindow[IconDrawnSiteRva - IconDrawnWindowRva] == 0x8B);
constexpr std::uint64_t SpriteLoadRva    = 0x858990;   // (GameSprite*, name under data/hd/global/ui/, flags)
constexpr std::uint64_t SpriteDrawHdRva  = 0x79DA50;   // (hd sprite, int32 position[2], float scale, params)
constexpr std::uint8_t  SpriteLoadBytes[]{
    0x40, 0x53, 0x56, 0x57, 0x48, 0x81, 0xEC, 0x40, 0x01, 0x00, 0x00, 0x48, 0x8B, 0x05, 0x26, 0x29,
    0x17, 0x02, 0x48, 0x33, 0xC4, 0x48, 0x89, 0x84, 0x24, 0x30, 0x01, 0x00, 0x00, 0x48, 0x8B, 0xD9,
};
// 79DA50 ... movaps xmm8, xmm2 / mov rsi, r9: the scale and the params.
constexpr std::uint8_t  SpriteDrawHdBytes[]{
    0x48, 0x89, 0x5C, 0x24, 0x18, 0x55, 0x56, 0x41, 0x56, 0x48, 0x8D, 0xAC, 0x24, 0x70, 0xFF, 0xFF,
    0xFF, 0x48, 0x81, 0xEC, 0x90, 0x01, 0x00, 0x00, 0x44, 0x0F, 0x29, 0x84, 0x24, 0x60, 0x01, 0x00,
    0x00, 0x48, 0x8B, 0x05, 0x50, 0xD8, 0x22, 0x02, 0x48, 0x33, 0xC4, 0x48, 0x89, 0x45, 0xE0, 0x44,
    0x0F, 0x28, 0xC2, 0x49, 0x8B, 0xF1,
};
constexpr std::size_t   HdSpriteFrames = 0xCC;   // 79DA50: 1 / frames is one frame's share of U
constexpr std::size_t   HdSpriteWidth  = 0xD0;   // 79DA50: one frame's width

// Relay entered by the call at 15BCED (replaces mov ecx,[2A8AF28]):
//   sub rsp,28h / mov rcx,rbp / mov rdx,rbx / movaps xmm2,xmm6 /
//   mov r9,[rdi+r14+18h] / call [slot ArtefactSweep] / add rsp,28h /
//   mov rax,[slot 2A8AF28] / mov ecx,[rax] / ret
constexpr std::uint8_t SweepRelay[]{
    0x48, 0x83, 0xEC, 0x28, 0x48, 0x89, 0xE9, 0x48, 0x89, 0xDA, 0x0F, 0x28, 0xD6, 0x4E, 0x8B, 0x4C,
    0x37, 0x18, 0xFF, 0x15, 0x10, 0x00, 0x00, 0x00, 0x48, 0x83, 0xC4, 0x28, 0x48, 0x8B, 0x05, 0x0D,
    0x00, 0x00, 0x00, 0x8B, 0x08, 0xC3, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};
constexpr std::size_t SweepRelaySlotDraw  = 40;
constexpr std::size_t SweepRelaySlotIndex = 48;
// Relay entered by the call at 43B1BF: jmp [slot LocalDelayGate], which has
// the setter's own arguments and returns to 43B1C4.
constexpr std::uint8_t LocalDelayRelay[]{
    0xFF, 0x25, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};
constexpr std::size_t LocalDelayRelaySlot = 6;
// Relay entered by the call at 58195B (replaces the rule's load and call, NOPs
// up to the jmp at 581965):
//   mov r9,[r9+rax*8] / mov r8,rbx / cmp r15b,FFh / jne rule /
//   sub rsp,28h / call [slot ArtefactUseRule] / add rsp,28h / ret /
//   rule: jmp r9   (an item that takes a target keeps its rule as it is)
constexpr std::uint8_t UseRuleRelay[]{
    0x4D, 0x8B, 0x0C, 0xC1, 0x49, 0x89, 0xD8, 0x41, 0x80, 0xFF, 0xFF, 0x75, 0x0F, 0x48, 0x83, 0xEC,
    0x28, 0xFF, 0x15, 0x09, 0x00, 0x00, 0x00, 0x48, 0x83, 0xC4, 0x28, 0xC3, 0x41, 0xFF, 0xE1, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};
constexpr std::size_t UseRuleRelaySlot = 32;

// The game's item skill casters, the path chance-to-cast items take (item
// event function 20, 583B30):
//   5896E0 (unit, skill, level, target unit, flag)  at a unit
//   589820 (unit, skill, level, x, y, flag)          at a spot
// Both refuse while the caster has state 54 (uninterruptable), cast through
// SKILLITEM_HandleItemEffectSkill 589930 (the skill's ItemTarget, a fresh cast
// id, ItemCheckStart, then the skill handler 43ACB0 with itemCast 1 and
// itemEffect = flag), and then queue the cast for the clients with 5390D0
// (packet 99h, at a unit) or 5391A0 (packet 9Ah, at a spot). Those packets are
// what make every client play the skill: its client missiles, overlays and
// sounds. A plain call of the skill handler, as up to 1.5.1, skips them.
// Event function 20 passes flag 1 for an item's chance to cast.
constexpr std::uint64_t CastOnTargetRva       = 0x5896E0;
constexpr std::uint8_t  CastOnTargetEntry[]{
    0x48, 0x89, 0x5C, 0x24, 0x10, 0x48, 0x89, 0x6C, 0x24, 0x18, 0x48, 0x89, 0x74, 0x24, 0x20, 0x57,
    0x41, 0x56, 0x41, 0x57, 0x48, 0x83, 0xEC, 0x70,
};
// 5896F8..589735: target r9, level r8d, skill edx, unit rcx, both null tests,
// 48FF00 (the unit's game), then STATES_CheckState (unit, 36h).
constexpr std::uint64_t CastOnTargetBodyRva   = 0x5896F8;
constexpr std::uint8_t  CastOnTargetBody[]{
    0x49, 0x8B, 0xF1, 0x41, 0x8B, 0xE8, 0x44, 0x8B, 0xF2, 0x48, 0x8B, 0xD9, 0x48, 0x85, 0xC9, 0x0F,
    0x84, 0xF0, 0x00, 0x00, 0x00, 0x4D, 0x85, 0xC9, 0x0F, 0x84, 0xE7, 0x00, 0x00, 0x00, 0xE8, 0xE5,
    0x67, 0xF0, 0xFF, 0x48, 0x8B, 0xF8, 0x48, 0x85, 0xC0, 0x0F, 0x84, 0xD6, 0x00, 0x00, 0x00, 0xBA,
    0x36, 0x00, 0x00, 0x00, 0x48, 0x8B, 0xCB, 0xE8, 0x7C, 0xBA, 0xDA, 0xFF, 0x85, 0xC0,
};
constexpr std::uint64_t CastAtPositionRva     = 0x589820;
constexpr std::uint8_t  CastAtPositionEntry[]{
    0x48, 0x89, 0x5C, 0x24, 0x10, 0x48, 0x89, 0x6C, 0x24, 0x18, 0x56, 0x57, 0x41, 0x54, 0x41, 0x56,
    0x41, 0x57, 0x48, 0x83, 0xEC, 0x70,
};
// 589836..58986A: y r9d, level r8d, skill edx, unit rcx, 48FF00, then
// STATES_CheckState (unit, 36h).
constexpr std::uint64_t CastAtPositionBodyRva = 0x589836;
constexpr std::uint8_t  CastAtPositionBody[]{
    0x41, 0x8B, 0xE9, 0x45, 0x8B, 0xF0, 0x44, 0x8B, 0xFA, 0x48, 0x8B, 0xD9, 0x48, 0x85, 0xC9, 0x0F,
    0x84, 0xCA, 0x00, 0x00, 0x00, 0xE8, 0xB0, 0x66, 0xF0, 0xFF, 0x48, 0x8B, 0xF0, 0x48, 0x85, 0xC0,
    0x0F, 0x84, 0xB9, 0x00, 0x00, 0x00, 0xBA, 0x36, 0x00, 0x00, 0x00, 0x48, 0x8B, 0xCB, 0xE8, 0x47,
    0xB9, 0xDA, 0xFF, 0x85, 0xC0,
};
// 5899D9..589A26 in 589930: it refuses a skill whose ItemEffect (record +20Ah,
// skills.txt ItemEffect) is 0, and one whose record +28h has the bit 2 (the
// mask in 1D996D4) unless 33F6A0 (unit, skill) allows it.
constexpr std::uint64_t ItemEffectChecksRva   = 0x5899D9;
constexpr std::uint8_t  ItemEffectChecks[]{
    0x66, 0x44, 0x39, 0xAF, 0x0A, 0x02, 0x00, 0x00, 0x75, 0x28, 0x48, 0x8D, 0x8C, 0x24, 0xB0, 0x00,
    0x00, 0x00, 0x44, 0x88, 0xAC, 0x24, 0xB0, 0x00, 0x00, 0x00, 0xE8, 0xB8, 0xFC, 0xFF, 0xFF, 0x84,
    0xC0, 0x74, 0x01, 0xCC, 0x66, 0x44, 0x39, 0xAF, 0x0A, 0x02, 0x00, 0x00, 0x0F, 0x84, 0x0A, 0x03,
    0x00, 0x00, 0x0F, 0xB6, 0x47, 0x28, 0x84, 0x05, 0xBF, 0xFC, 0x80, 0x01, 0x74, 0x12, 0x8B, 0xD5,
    0x48, 0x8B, 0xCB, 0xE8, 0x7F, 0x5C, 0xDB, 0xFF, 0x84, 0xC0, 0x0F, 0x84, 0xEC, 0x02, 0x00, 0x00,
};
constexpr std::uint64_t ItemRestrictRva       = 0x33F6A0;  // (unit, skill) -> bool
constexpr std::uint8_t  ItemRestrictEntry[]{
    0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x74, 0x24, 0x10, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x8B,
    0xDA, 0x48, 0x8B, 0xF1,
};
constexpr std::size_t   SkillItemEffect       = 0x20A;
constexpr std::size_t   SkillItemFlags        = 0x28;
constexpr std::uint8_t  SkillItemRestrictBit  = 0x02;
constexpr std::int32_t  StateUninterruptable  = 54;
// The aim the item casters take when an item casts with no hit to aim at:
// the unit's target unit (48FE20 (game, unit)), else the point its path aims
// at (34B8F0 / 34B940 (unit): the path's first point x / y, 341CC0 / 341CD0).
constexpr std::uint64_t TargetUnitRva = 0x48FE20;
constexpr std::uint8_t  TargetUnitEntry[]{
    0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B, 0xDA,
};
constexpr std::uint64_t TargetXRva = 0x34B8F0;
constexpr std::uint8_t  TargetXBody[]{
    0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B, 0xD9, 0x48, 0x85, 0xC9, 0x75, 0x13, 0x88, 0x4C,
    0x24, 0x30, 0x48, 0x8D, 0x4C, 0x24, 0x30, 0xE8, 0x64, 0xAF, 0xFF, 0xFF, 0x84, 0xC0, 0x74, 0x01,
    0xCC, 0x83, 0x3B, 0x05, 0x75, 0x14, 0x48, 0x8D, 0x4C, 0x24, 0x30, 0xC6, 0x44, 0x24, 0x30, 0x00,
    0xE8, 0x7B, 0x9A, 0xFF, 0xFF, 0x84, 0xC0, 0x74, 0x01, 0xCC, 0x48, 0x8B, 0x4B, 0x38, 0x48, 0x83,
    0xC4, 0x20, 0x5B, 0xE9, 0x88, 0x63, 0xFF, 0xFF,
};
constexpr std::uint64_t TargetYRva = 0x34B940;
constexpr std::uint8_t  TargetYBody[]{
    0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B, 0xD9, 0x48, 0x85, 0xC9, 0x75, 0x13, 0x88, 0x4C,
    0x24, 0x30, 0x48, 0x8D, 0x4C, 0x24, 0x30, 0xE8, 0x14, 0xAF, 0xFF, 0xFF, 0x84, 0xC0, 0x74, 0x01,
    0xCC, 0x83, 0x3B, 0x05, 0x75, 0x14, 0x48, 0x8D, 0x4C, 0x24, 0x30, 0xC6, 0x44, 0x24, 0x30, 0x00,
    0xE8, 0x2B, 0x9A, 0xFF, 0xFF, 0x84, 0xC0, 0x74, 0x01, 0xCC, 0x48, 0x8B, 0x4B, 0x38, 0x48, 0x83,
    0xC4, 0x20, 0x5B, 0xE9, 0x48, 0x63, 0xFF, 0xFF,
};
constexpr std::size_t CubeRelayOffset     = 0x300;  // the identify relay's shape: jmp [slot]

constexpr std::size_t   GameFrameOffset   = 0x170;
constexpr std::int32_t  FramesPerSecond   = 25;
constexpr std::uint32_t UnitMonster       = 1;
constexpr std::uint32_t UnitObject        = 2;

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
using TooltipListFn     = void* (__fastcall*)(void*, std::int32_t, std::int32_t);
using UnitFn            = void* (__fastcall*)(void*);
using RollRandomFn      = std::uint32_t (__fastcall*)(void*, std::uint32_t);
using ContainerDropFn   = std::int64_t (__fastcall*)(void**, std::int32_t);
using GenerateDropsFn   = std::int64_t (__fastcall*)(void*, void*, void*, std::uint64_t, std::uint64_t, std::uint64_t,
    std::uint64_t, void*, void*, std::uint64_t);
using FinishItemFn      = void (__fastcall*)(void*, std::uint8_t);
using ItemFlagFn        = void (__fastcall*)(void*, std::uint32_t, std::int32_t);
using BuildFn           = void* (__fastcall*)(void*, std::uint8_t*, std::int32_t);
using CubeGenerateFn    = std::int64_t (__fastcall*)(void*, void*, void*, void*);
using UnitIntFn         = std::int32_t (__fastcall*)(void*);
using UnitByteFn        = std::uint8_t (__fastcall*)(void*);
using UnitWordFn        = std::uint16_t (__fastcall*)(void*);
using UnitFlagsFn       = std::uint32_t (__fastcall*)(void*);
using AddSocketsFn      = void (__fastcall*)(void*, std::int32_t);
using FreeUnitFn        = void (__fastcall*)(void*, void*);
using TransactionFn     = std::int32_t (__fastcall*)(void*, void*, std::uint32_t, void*, std::int32_t, std::int32_t);
using SetStatFn         = void (__fastcall*)(void*, std::int32_t, std::int32_t, std::int32_t);
using SocketInsertFn    = std::int32_t (__fastcall*)(void*, void*, std::uint32_t, std::uint32_t, std::int32_t);
using SkillsRecordFn    = const std::uint8_t* (__fastcall*)(std::uint8_t, std::int32_t);
using UnitStateFn       = std::int32_t (__fastcall*)(void*, std::int32_t);
using SetStateFn        = void (__fastcall*)(void*, std::int32_t, std::uint32_t);
using SkillCalcFn       = std::int32_t (__fastcall*)(std::uint8_t, void*, std::int32_t, std::int32_t, std::int32_t);
using FindDropFn        = void (__fastcall*)(void*, void*, void*, std::int32_t);
using AreaVisitFn       = std::uint32_t (__fastcall*)(void**, void*);
using AreaSearchFn      = void (__fastcall*)(void*, void*, std::int32_t, std::int32_t, std::int32_t, std::int32_t, AreaVisitFn, void*,
    std::int32_t, const char*, std::int32_t);
using ServerDoFn        = std::int32_t (__fastcall*)(void*, void*, std::int32_t, std::int32_t, std::int32_t, std::int32_t, std::int32_t);
using UseCallbackFn     = std::uint8_t (__fastcall*)(void*, void*, void*);

// ---------------------------------------------------------------------------
// Configuration
// ---------------------------------------------------------------------------
struct ContainerConfig {
    bool                      enabled{ true };
    std::int32_t              stat{ 976 };
    std::int32_t              percent{ 5 };
    std::int32_t              seconds{ 15 };
    bool                      sheet{ true };
    std::vector<std::int32_t> stats{ 398, 1012, 1013, 489, 493, 495, 496 };
};

struct SuperiorConfig {
    bool         enabled{ true };
    std::int32_t stat{ 977 };
    std::int32_t chance{ 25 };
};

struct GoldConfig {
    bool         enabled{ true };
    std::int32_t stat{ 980 };
    std::int32_t percent{ 20 };
};

struct TotalGoldConfig {
    bool         enabled{ true };
    std::int32_t stat{ -1 };   // -1: not set, the stat is off
};

struct IdentifyConfig {
    bool         enabled{ true };
    std::int32_t stat{ 981 };
};

struct SocketConfig {
    bool                     enabled{ true };
    std::int32_t             stat{ 982 };
    std::int32_t             chance{ 50 };
    std::vector<std::string> types{ "weap", "armo", "ring", "amul", "bowq", "xboq", "arrm", "boqm", "merc", "casce", "cosce", "csta" };
    std::vector<std::string> twins{ "aq2:aq0", "cq2:cq0" };
};

struct MassDigConfig {
    bool         enabled{ true };
    std::int32_t srvdofunc{ 172 };
    std::int32_t radius{ 10 };
    std::int32_t chance{ -1 };
    std::int32_t stat{ 989 };
    std::int32_t skill{ 0 };
    std::int32_t level{ 1 };
};

struct ScrollEntry {
    std::uint32_t code;
    std::int32_t  skill;
    std::int32_t  level;
};

struct ScrollConfig {
    bool                        enabled{ true };
    std::vector<std::string>    entries;
    bool                        sweep{ true };
    std::string                 sweepSprite{ "PANEL/CrossbowCharges/Sweep" };
    std::array<std::int32_t, 3> sweepColor{ 0, 0, 0 };
    std::int32_t                sweepOpacity{ 45 };
};

struct RunewordConfig {
    bool         enabled{ true };
    std::int32_t stat{ 988 };
};

struct SellConfig {
    bool                     enabled{ true };
    std::int32_t             stat{ 983 };
    std::int32_t             vendor{ 511 };
    std::vector<std::string> types{ "weap", "armo" };
};

struct CouponEtherealConfig {
    bool                     enabled{ true };
    std::int32_t             stat{ 984 };
    std::int32_t             chance{ 5 };
    std::vector<std::string> couponTypes{ "coup" };
    std::vector<std::string> excludeTypes{ "miss" };
};

struct MythicalConfig {
    bool                     enabled{ true };
    std::int32_t             stat{ 985 };
    std::int32_t             percent{ 25 };
    std::vector<std::string> codes{ std::begin(DefaultMythicalCodes), std::end(DefaultMythicalCodes) };
};

struct CouponUpgradeConfig {
    bool         enabled{ true };
    std::int32_t stat{ 986 };
    std::int32_t exceptionalChance{ 25 };
    std::int32_t eliteChance{ 15 };
    std::string  normalType{ "cpn1" };
    std::string  exceptionalType{ "cpn2" };
    std::string  eliteType{ "cpn3" };
};

struct CrystalConfig {
    bool                     enabled{ true };
    std::int32_t             stat{ 987 };
    std::int32_t             chance{ 2 };
    std::vector<std::string> codes{ std::begin(DefaultCrystalCodes), std::end(DefaultCrystalCodes) };
};

struct RuneConfig {
    bool                      enabled{ true };
    std::int32_t              stat{ 978 };
    std::int32_t              percent{ 5 };
    std::vector<std::string>  codes{ std::begin(DefaultRuneCodes), std::end(DefaultRuneCodes) };
    bool                      tooltips{ true };
    std::vector<std::int32_t> perItemStats{ 16, 17, 18, 75 };
    std::vector<std::int32_t> stats{ std::begin(DefaultStats), std::end(DefaultStats) };
};

struct Config {
    bool            enabled{ true };
    bool            diagnostics{ false };
    bool            creditMinionOwner{ true };
    ContainerConfig containers{};
    SuperiorConfig  superior{};
    RuneConfig      runes{};
    GoldConfig      gold{};
    TotalGoldConfig totalGold{};
    IdentifyConfig  identify{};
    SocketConfig         sockets{};
    SellConfig           sell{};
    CouponEtherealConfig couponEthereal{};
    MythicalConfig       mythical{};
    CouponUpgradeConfig  couponUpgrade{};
    CrystalConfig        crystal{};
    RunewordConfig       runewords{};
    MassDigConfig        massDig{};
    ScrollConfig         scrolls{};
};

// ---------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------
const D2RL::PluginContext* Context{};
std::uintptr_t             Base{};
Config                     Settings{};
std::string                ConfigProblems;
std::string                InstallProblem;
std::string                ContainerProblem;
std::string                SuperiorProblem;
std::string                TooltipProblem;
std::string                GoldProblem;
std::string                IdentifyProblem;
std::string                RelayProblem;
std::string                BuilderProblem;
std::string                CubeProblem;
std::string                MythicalProblem;
std::string                SellProblem;
std::string                SocketProblem;
std::string                RunewordProblem;
std::string                MassDigProblem;
std::string                ScrollProblem;
std::string                CooldownProblem;
std::string                SweepProblem;

ReadStatFn        ReadOwnStat{};
ReadStatFn        ReadUnitStat{};
DataContextFn     GetDataContext{};
DataTablesFn      GetDataTables{};
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
ServerUnitFn      ServerUnit{};
ClientUnitFn      ClientUnit{};
LocalIndexFn      LocalPlayerIndex{};
PlayerFromIndexFn PlayerFromIndex{};
TooltipListFn     TooltipList{};
UnitFn            UnitSeed{};
UnitFn            MinionOwner{};
RollRandomFn      RollRandom{};
ContainerDropFn   OriginalContainerDrop{};
GenerateDropsFn   OriginalGenerateDrops{};
FinishItemFn      OriginalFinishItem{};
ItemFlagFn        SetItemFlag{};
BuildFn           OriginalBuild{};
CubeGenerateFn    OriginalCubeGenerate{};
SocketInsertFn    OriginalSocketInsert{};
UnitIntFn         AnimMode{};
UnitByteFn        ItemPage{};
UnitIntFn         ItemQuality{};
UnitWordFn        ItemVersion{};
UnitFlagsFn       ItemFlags{};
UnitByteFn        MaxSockets{};
AddSocketsFn      AddSockets{};
FreeUnitFn        FreeUnit{};
TransactionFn     TransactionCost{};
UnitFn            PlayerData{};
UnitFn            ItemSeed{};
UnitFn            ApplyEthereal{};
UnitIntFn         HasDurability{};
SetStatFn         SetUnitStat{};
SetStatFn         StatWrite{};
CheckItemTypeFn   CheckItemType{};
const D2RL::ThreadService*    ThreadSvc{};
const D2RL::LifecycleService* LifecycleSvc{};
bool                          Ready{};
bool                          FullStatsLayout{};
bool                          ContainerHooked{};
bool                          SuperiorPatched{};
bool                          GenerateDropsHooked{};
bool                          GoldPatched{};
bool                          IdentifyPatched{};
bool                          BuilderHooked{};
bool                          CubePatched{};
std::array<std::uint8_t, 5>   CubeWritten{};
bool                          MythicalPatched{};
bool                          SocketsReady{};
bool                          SellReady{};
bool                          EtherealReady{};
bool                          SocketInsertHooked{};
bool                          RunewordPatched{};
bool                          MassDigInstalled{};
bool                          ScrollPatched{};
bool                          UseRulePatched{};
std::array<std::uint8_t, 10>  UseRuleWritten{};
bool                          LocalDelayPatched{};
std::array<std::uint8_t, 5>   LocalDelayWritten{};
bool                          SweepPatched{};
std::array<std::uint8_t, 6>   SweepWritten{};
std::array<std::uint8_t, 7>   ScrollWritten{};
std::vector<ScrollEntry>      Scrolls;
std::array<std::uint8_t, RunewordSiteSize> RunewordWritten{};
std::array<std::uint8_t, MythicalSiteSize> MythicalWritten{};
std::array<std::uint8_t, GoldSiteSize> GoldWritten{};
std::array<std::uint8_t, 5>   IdentifyWritten{};
std::uint8_t*                 RelayPage{};
std::array<std::uint8_t, SuperiorSiteSize> SuperiorWritten{};

std::atomic<std::uint64_t> BonusListsAdded{};
std::atomic<std::uint64_t> BonusListsRemoved{};
std::atomic<std::uint64_t> BonusValuesWritten{};
std::atomic<std::uint64_t> HostPasses{};
std::atomic<std::uint64_t> ClientPasses{};
std::atomic<std::uint64_t> ContainersCounted{};
std::atomic<std::uint64_t> ContainerDrops{};
std::atomic<std::uint64_t> SuperiorRollsSeen{};
std::atomic<std::uint64_t> SuperiorPerkRolls{};
std::atomic<std::uint64_t> SuperiorUpgrades{};
std::atomic<std::uint32_t> PooledRunes{};
std::atomic<std::uint64_t> GoldPilesRaised{};
std::atomic<std::int64_t>  GoldAdded{};
std::atomic<std::uint64_t> TotalGoldPiles{};
std::atomic<std::int32_t>  TotalGoldLast{};
std::atomic<std::uint64_t> ItemsIdentified{};
std::atomic<std::uint64_t> SocketsAdded{};
std::atomic<std::uint64_t> SocketTwinsDropped{};
std::atomic<std::uint64_t> ItemsSold{};
std::atomic<std::uint64_t> GoldFromSales{};
std::atomic<std::uint64_t> CouponOutputs{};
std::atomic<std::uint64_t> CouponEthereals{};
std::atomic<std::uint64_t> MythicalSecondRolls{};
std::atomic<std::uint64_t> MythicalUniques{};
std::atomic<std::uint64_t> CouponsUpgraded{};
std::atomic<std::uint64_t> CouponsToElite{};
std::atomic<std::uint64_t> CrystalsUpgraded{};
std::atomic<std::uint64_t> MagicRunewordsAllowed{};
std::atomic<std::uint64_t> MassDigCasts{};
std::atomic<std::uint64_t> MassDigCorpses{};
std::atomic<std::uint64_t> MassDigFinds{};
std::atomic<std::uint64_t> MassDigNoRange{};
std::atomic<std::uint64_t> ScrollsUsed{};
std::vector<std::uint8_t>  ScrollWarned;   // per entry: the missing ItemEffect was logged
std::atomic<std::uint64_t> ArtefactCooldownsStarted{};
std::atomic<std::uint64_t> ArtefactUsesRefused{};
std::atomic<std::uint64_t> ArtefactTownRefusals{};
std::atomic<std::uint64_t> ArtefactCastRefusals{};
std::atomic<std::uint64_t> ArtefactCastsAtUnit{};
std::atomic<std::uint64_t> ArtefactCastsAtSpot{};
std::atomic<std::uint64_t> ArtefactCastsFailed{};
std::atomic<std::uint64_t> NativeCooldownsSkipped{};
std::atomic<std::uint64_t> ArtefactSweepDraws{};
std::atomic<std::uint32_t> MergedItemsNow{};
std::atomic<std::int32_t>  LocalContainers{};
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

// One quoted string: "text". Sprite paths need no escapes.
auto ParseString(std::string_view value, std::string& out) -> bool {
    value = Trim(value);
    if (value.size() < 2 || value.front() != '"' || value.back() != '"') return false;
    out.assign(value.substr(1, value.size() - 2));
    return out.find('"') == std::string::npos;
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
            const std::size_t close = line.find(']');
            section = std::string(Trim(line.substr(1, close == std::string_view::npos ? 0 : close - 1)));
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
        bool ok = true;
        bool known = true;
        if (section == "archaeology") {
            if (key == "enabled") ok = ParseBool(value, Settings.enabled);
            else if (key == "diagnostics") ok = ParseBool(value, Settings.diagnostics);
            else if (key == "credit_minion_owner") ok = ParseBool(value, Settings.creditMinionOwner);
            else known = false;
        } else if (section == "containers") {
            ContainerConfig& c = Settings.containers;
            if (key == "enabled") ok = ParseBool(value, c.enabled);
            else if (key == "perk_stat") ok = ParseInt(value, c.stat);
            else if (key == "percent_per_container") ok = ParseInt(value, c.percent);
            else if (key == "window_seconds") ok = ParseInt(value, c.seconds);
            else if (key == "show_on_character_sheet") ok = ParseBool(value, c.sheet);
            else if (key == "total_damage_stats") ok = ParseInts(value, c.stats);
            else known = false;
        } else if (section == "superior_drops") {
            SuperiorConfig& s = Settings.superior;
            if (key == "enabled") ok = ParseBool(value, s.enabled);
            else if (key == "perk_stat") ok = ParseInt(value, s.stat);
            else if (key == "chance_percent") ok = ParseInt(value, s.chance);
            else known = false;
        } else if (section == "gold_find") {
            GoldConfig& g = Settings.gold;
            if (key == "enabled") ok = ParseBool(value, g.enabled);
            else if (key == "perk_stat") ok = ParseInt(value, g.stat);
            else if (key == "percent") ok = ParseInt(value, g.percent);
            else known = false;
        } else if (section == "total_gold_found") {
            TotalGoldConfig& t = Settings.totalGold;
            if (key == "enabled") ok = ParseBool(value, t.enabled);
            else if (key == "stat_id") ok = ParseInt(value, t.stat);
            else known = false;
        } else if (section == "identified_drops") {
            IdentifyConfig& d = Settings.identify;
            if (key == "enabled") ok = ParseBool(value, d.enabled);
            else if (key == "perk_stat") ok = ParseInt(value, d.stat);
            else known = false;
        } else if (section == "sockets") {
            SocketConfig& k = Settings.sockets;
            if (key == "enabled") ok = ParseBool(value, k.enabled);
            else if (key == "perk_stat") ok = ParseInt(value, k.stat);
            else if (key == "chance_percent") ok = ParseInt(value, k.chance);
            else if (key == "item_types") ok = ParseStrings(value, k.types);
            else if (key == "socketed_twins") ok = ParseStrings(value, k.twins);
            else known = false;
        } else if (section == "sell_value") {
            SellConfig& k = Settings.sell;
            if (key == "enabled") ok = ParseBool(value, k.enabled);
            else if (key == "perk_stat") ok = ParseInt(value, k.stat);
            else if (key == "vendor") ok = ParseInt(value, k.vendor);
            else if (key == "item_types") ok = ParseStrings(value, k.types);
            else known = false;
        } else if (section == "coupon_ethereal") {
            CouponEtherealConfig& k = Settings.couponEthereal;
            if (key == "enabled") ok = ParseBool(value, k.enabled);
            else if (key == "perk_stat") ok = ParseInt(value, k.stat);
            else if (key == "chance_percent") ok = ParseInt(value, k.chance);
            else if (key == "coupon_types") ok = ParseStrings(value, k.couponTypes);
            else if (key == "exclude_item_types") ok = ParseStrings(value, k.excludeTypes);
            else known = false;
        } else if (section == "mythical_uniques") {
            MythicalConfig& k = Settings.mythical;
            if (key == "enabled") ok = ParseBool(value, k.enabled);
            else if (key == "perk_stat") ok = ParseInt(value, k.stat);
            else if (key == "bonus_percent") ok = ParseInt(value, k.percent);
            else if (key == "item_codes") ok = ParseStrings(value, k.codes);
            else known = false;
        } else if (section == "coupon_upgrade") {
            CouponUpgradeConfig& k = Settings.couponUpgrade;
            std::vector<std::string> one;
            if (key == "enabled") ok = ParseBool(value, k.enabled);
            else if (key == "perk_stat") ok = ParseInt(value, k.stat);
            else if (key == "exceptional_chance") ok = ParseInt(value, k.exceptionalChance);
            else if (key == "elite_chance") ok = ParseInt(value, k.eliteChance);
            else if (key == "normal_type" || key == "exceptional_type" || key == "elite_type") {
                ok = ParseStrings("[" + std::string(value) + "]", one) && one.size() == 1;
                if (ok) (key == "normal_type" ? k.normalType : key == "exceptional_type" ? k.exceptionalType : k.eliteType) = one[0];
            } else known = false;
        } else if (section == "mass_dig") {
            MassDigConfig& k = Settings.massDig;
            if (key == "enabled") ok = ParseBool(value, k.enabled);
            else if (key == "srvdofunc") ok = ParseInt(value, k.srvdofunc);
            else if (key == "radius") ok = ParseInt(value, k.radius);
            else if (key == "chance_percent") ok = ParseInt(value, k.chance);
            else if (key == "perk_stat") ok = ParseInt(value, k.stat);
            else if (key == "skill_id") ok = ParseInt(value, k.skill);
            else if (key == "skill_level") ok = ParseInt(value, k.level);
            else known = false;
        } else if (section == "skill_scrolls") {
            ScrollConfig& k = Settings.scrolls;
            if (key == "enabled") ok = ParseBool(value, k.enabled);
            else if (key == "scrolls") ok = ParseStrings(value, k.entries);
            else if (key == "sweep") ok = ParseBool(value, k.sweep);
            else if (key == "sweep_sprite") {
                std::string sprite;
                ok = ParseString(value, sprite) && !sprite.empty();
                if (ok) k.sweepSprite = std::move(sprite);
            } else if (key == "sweep_color") {
                std::vector<std::int32_t> rgb;
                ok = ParseInts(value, rgb) && rgb.size() == 3
                    && std::all_of(rgb.begin(), rgb.end(), [](std::int32_t c) { return c >= 0 && c <= 255; });
                if (ok) std::copy(rgb.begin(), rgb.end(), k.sweepColor.begin());
            } else if (key == "sweep_opacity") ok = ParseInt(value, k.sweepOpacity) && k.sweepOpacity >= 0 && k.sweepOpacity <= 100;
            else known = false;
        } else if (section == "magic_runewords") {
            RunewordConfig& k = Settings.runewords;
            if (key == "enabled") ok = ParseBool(value, k.enabled);
            else if (key == "perk_stat") ok = ParseInt(value, k.stat);
            else known = false;
        } else if (section == "crystal_upgrade") {
            CrystalConfig& k = Settings.crystal;
            if (key == "enabled") ok = ParseBool(value, k.enabled);
            else if (key == "perk_stat") ok = ParseInt(value, k.stat);
            else if (key == "chance_percent") ok = ParseInt(value, k.chance);
            else if (key == "item_codes") ok = ParseStrings(value, k.codes);
            else known = false;
        } else if (section == "socketed_runes") {
            RuneConfig& r = Settings.runes;
            if (key == "enabled") ok = ParseBool(value, r.enabled);
            else if (key == "perk_stat") ok = ParseInt(value, r.stat);
            else if (key == "bonus_percent") ok = ParseInt(value, r.percent);
            else if (key == "item_codes") ok = ParseStrings(value, r.codes);
            else if (key == "show_in_tooltips") ok = ParseBool(value, r.tooltips);
            else if (key == "per_item_stats") ok = ParseInts(value, r.perItemStats);
            else if (key == "stats") ok = ParseInts(value, r.stats);
            else known = false;
        } else {
            known = false;
        }
        if (!ok) Problem(section + "." + std::string(key) + " has an invalid value");
        else if (!known && !section.empty()) Problem(section + "." + std::string(key) + " is not a known setting");
    }
    Settings.containers.percent = std::clamp(Settings.containers.percent, 0, 1000);
    Settings.containers.seconds = std::clamp(Settings.containers.seconds, 1, 3600);
    Settings.superior.chance    = std::clamp(Settings.superior.chance, 0, 100);
    Settings.runes.percent      = std::clamp(Settings.runes.percent, 0, 1000);
    Settings.gold.percent       = std::clamp(Settings.gold.percent, 0, 1000);
    if (Settings.totalGold.stat < -1 || Settings.totalGold.stat >= MaxStatId) {
        Problem("total_gold_found.stat_id must be -1 or 0 to 32767");
        Settings.totalGold.stat = -1;
    }
    Settings.sockets.chance     = std::clamp(Settings.sockets.chance, 0, 100);
    Settings.couponEthereal.chance = std::clamp(Settings.couponEthereal.chance, 0, 100);
    Settings.mythical.percent   = std::clamp(Settings.mythical.percent, 0, 1000);
    Settings.couponUpgrade.exceptionalChance = std::clamp(Settings.couponUpgrade.exceptionalChance, 0, 100);
    Settings.couponUpgrade.eliteChance       = std::clamp(Settings.couponUpgrade.eliteChance, 0, 100);
    Settings.crystal.chance     = std::clamp(Settings.crystal.chance, 0, 100);
    Settings.massDig.radius     = Settings.massDig.radius < 1 ? -1 : std::min(Settings.massDig.radius, 100);
    Settings.massDig.chance     = std::clamp(Settings.massDig.chance, -1, 100);
    Settings.massDig.level      = std::clamp(Settings.massDig.level, 1, 99);
}

void ReadConfiguration() {
    if (!Context->EnsureConfig(DefaultConfigToml)) {
        Context->LogWarn("Archaeology: the config file could not be created; using the defaults.");
        return;
    }
    std::uint32_t required = 0;
    std::string buffer(131072, '\0');
    if (!Context->ReadConfig(buffer.data(), static_cast<std::uint32_t>(buffer.size()), &required)) {
        Context->LogWarn("Archaeology: the config file could not be read; using the defaults.");
        return;
    }
    buffer.resize(std::strlen(buffer.c_str()));
    ParseConfig(buffer);
    if (!ConfigProblems.empty()) D2RL::LogWarnF(Context, "Archaeology: config: %s.", ConfigProblems.c_str());
}

// ---------------------------------------------------------------------------
// Stat sets: one mask byte per stat id
// ---------------------------------------------------------------------------
constexpr std::uint8_t MaskRunes   = 0x01;
constexpr std::uint8_t MaskPerItem = 0x02;

std::array<std::uint8_t, MaxStatId> StatMask{};
std::size_t                         RuneStatCount{};

void BuildMasks() noexcept {
    StatMask.fill(0);
    RuneStatCount = 0;
    for (const std::int32_t stat : Settings.runes.stats) {
        if (stat == ChargedSkill) {
            if (Context != nullptr) {
                Context->LogWarn("Archaeology: socketed_runes.stats names 204 (item_charged_skill); its value packs charges and "
                    "maximum charges, so it is not raised.");
            }
            continue;
        }
        if (stat < 0 || stat >= MaxStatId) continue;
        if ((StatMask[static_cast<std::size_t>(stat)] & MaskRunes) == 0) ++RuneStatCount;
        StatMask[static_cast<std::size_t>(stat)] |= MaskRunes;
    }
    for (const std::int32_t stat : Settings.runes.perItemStats) {
        if (stat >= 0 && stat < MaxStatId) StatMask[static_cast<std::size_t>(stat)] |= MaskPerItem;
    }
}

auto MaskOf(std::int32_t stat) noexcept -> std::uint8_t {
    return stat >= 0 && stat < MaxStatId ? StatMask[static_cast<std::size_t>(stat)] : 0;
}

// ---------------------------------------------------------------------------
// Units, perks
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

auto HasStatPerk(void* player, bool enabled, std::int32_t stat) noexcept -> bool {
    return enabled && player != nullptr && UnitType(player) == UnitPlayer && stat >= 0
        && static_cast<std::uint64_t>(stat) < StatRows(player) && ReadOwnStat(player, stat, 0) > 0;
}

std::vector<std::uint32_t> RuneCodes;   // packed items.txt codes, sorted

auto HasRunePerk(void* player) noexcept -> bool {
    const RuneConfig& r = Settings.runes;
    return r.percent != 0 && RuneStatCount != 0 && !RuneCodes.empty() && HasStatPerk(player, r.enabled, r.stat);
}

// The player a drop is credited to: a player killer, or the player owner of a
// minion killer, the way the quality roll credits magic find.
auto ResolvePlayer(void* killer) noexcept -> void* {
    const std::uint32_t type = UnitType(killer);
    if (type == UnitPlayer) return killer;
    if (type == UnitMonster && Settings.creditMinionOwner) {
        void* const owner = MinionOwner(killer);
        if (owner != nullptr && UnitType(owner) == UnitPlayer) return owner;
    }
    return nullptr;
}

// The unit an item's stat list is merged into (the character for counting
// gear, the host for a socketed item), or null.
auto MergedInto(void* item) noexcept -> void* {
    std::uint32_t mode = 0;
    return GetOwner(item, &mode);
}

// items.txt codes are four characters padded with spaces (NUL treated the same).
auto PackCode(std::string_view code) noexcept -> std::uint32_t {
    std::uint32_t packed = 0;
    for (std::size_t i = 0; i < 4; ++i) {
        const char c = i < code.size() ? code[i] : ' ';
        packed |= static_cast<std::uint32_t>(static_cast<std::uint8_t>(c == '\0' ? ' ' : c)) << (8 * i);
    }
    return packed;
}

auto NormalizedCode(std::uint32_t code) noexcept -> std::uint32_t {
    for (std::size_t i = 0; i < 4; ++i) {
        if (((code >> (8 * i)) & 0xFFU) == 0) code |= 0x20U << (8 * i);
    }
    return code;
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

// The (stat, layer) pairs of an item's full stats array whose stat is in the
// mask's set.
void ItemPairs(void* item, std::uint8_t mask, std::vector<StatKey>& out) noexcept {
    out.clear();
    const std::uint8_t* ex = ExOf(item);
    if (ex == nullptr) return;
    const auto* entries = Read<const std::uint8_t*>(ex, ExFullStats);
    const auto  count   = Read<std::uint64_t>(ex, ExFullStatsCount);
    if (entries == nullptr || count > 8192) return;
    for (std::uint64_t i = 0; i < count; ++i) {
        const std::uint8_t* entry = entries + i * StatEntrySize;
        const std::int32_t stat = Read<std::uint16_t>(entry, StatEntryStat);
        if ((MaskOf(stat) & mask) == 0) continue;
        out.push_back({ stat, static_cast<std::int32_t>(Read<std::uint32_t>(entry, StatEntryLayer)) });
    }
}

// Every plugin bonus list (plain, flag 10000h) in the unit's active child
// chain, summed per pair: this plugin's and any other plugin's.
void PluginListValues(void* unit, std::vector<StatValue>& out) noexcept {
    out.clear();
    const std::uint8_t* ex = ExOf(unit);
    if (ex == nullptr) return;
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

// A pair's modifier value on a rune: its total without its base and without
// any plugin bonus list on it. A rune's base damage is 0, so op 13 adds nothing
// that would have to come off.
auto OwnValue(void* item, const StatKey& key, const std::vector<StatValue>& plugin) noexcept -> std::int64_t {
    const std::int64_t base = ReadOwnStat(item, key.stat, key.layer);
    return static_cast<std::int64_t>(ReadUnitStat(item, key.stat, key.layer)) - base - Lookup(plugin, key.stat, key.layer);
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
    std::int32_t           loc{};    // body location, 0 for an item elsewhere
    void*                  list{};    // its own list, or (merged) the list its tooltip draws
    std::vector<StatValue> values;    // what this plugin has in that list
    bool                   seen{};
    bool                   merged{};  // client: values were added into a list the tooltip draws
    std::vector<StatValue> expect;    // merged: each touched stat's value right after
};

// The character's own list (Containers Opened).
struct PlayerEntry {
    void*                  game{};   // host: the character's game; client: null
    std::uint32_t          id{};
    void*                  list{};
    std::vector<StatValue> values;
    std::int32_t           count{};  // containers behind the values, for the console
    bool                   seen{};
};

struct BonusSide {
    std::vector<BonusEntry>  entries;
    std::vector<PlayerEntry> players;
    std::uint64_t            generation{};
    std::uint64_t            updates{};
    std::uint64_t            lastMs{};
    std::mutex               mutex;    // the console reads entries from its own thread
};

BonusSide                  ServerBonus;
BonusSide                  ClientBonus;
std::atomic<std::uint64_t> Generation{ 1 };
std::atomic<std::uint64_t> ServerChain{};
std::atomic<std::uint64_t> ClientChain{};
std::atomic<std::uint64_t> ChainCounter{};

// A list is only ever touched while it is still in one of the unit's two
// child chains (ex +90h and +98h, next at +68h).
auto ListAttached(void* unit, void* list) noexcept -> bool {
    if (unit == nullptr || list == nullptr) return false;
    const std::uint8_t* ex = ExOf(unit);
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
// still attached to the unit, still carries the plugin flag and still holds
// exactly what was written into it. A list can be freed behind the plugin's
// back (the death cleanup, a client rebuilding an item from a server packet)
// and the allocator can hand the freed address to another list of the same
// unit.
auto OwnsList(void* unit, void* list, const std::vector<StatValue>& values) noexcept -> bool {
    return ListAttached(unit, list) && IsPluginFlagged(list) && HoldsExactly(list, values);
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

// The list a host's tooltip draws that the bonus can go into: its magic list
// (state 0, flag 40h), or its runeword list (state 171, flag 40h) when it has
// no magic list.
auto TooltipTarget(void* item) noexcept -> void* {
    if (TooltipList == nullptr) return nullptr;
    void* const magic = TooltipList(item, 0, 0x40);
    if (magic != nullptr && Read<std::int32_t>(magic, ListFlags) >= 0) return magic;
    void* const runeword = TooltipList(item, RunewordState, 0x40);
    if (runeword != nullptr && Read<std::int32_t>(runeword, ListFlags) >= 0) return runeword;
    return nullptr;
}

// A merged entry's additions are still in the item: the same list, attached,
// and every touched stat still at the value it was left at.
auto MergeIntact(void* item, const BonusEntry& entry) noexcept -> bool {
    if (!entry.merged || entry.list == nullptr || entry.list != TooltipTarget(item) || !ListAttached(item, entry.list)) return false;
    for (const StatValue& x : entry.expect) {
        if (ListValue(entry.list, x.stat, x.layer) != x.value) return false;
    }
    return true;
}

// Takes a merged entry's additions back out of the item's list.
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

void RemoveList(void* unit, void* list) noexcept {
    if (!ListAttached(unit, list) || !IsPluginFlagged(list)) return;
    const std::uint8_t context = GetDataContext(unit);
    UnlinkStatList(unit, list);
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

// Writes wanted into a list of the plugin's own on unit (allocated and attached
// when missing): pairs no longer wanted go to 0 (the engine drops a stat at 0),
// the rest are written only when they change.
auto WriteOwnList(void* unit, std::uint32_t ownerType, std::uint32_t id, void*& list, std::vector<StatValue>& values,
        const std::vector<StatValue>& wanted) noexcept -> bool {
    const std::uint8_t context = GetDataContext(unit);
    if (list == nullptr) {
        void* const created = AllocStatList(BonusListFlags, 0, static_cast<std::int32_t>(ownerType), id);
        if (created == nullptr) return false;
        AttachStatList(unit, created, 1);
        list = created;
        values.clear();
        BonusListsAdded.fetch_add(1, std::memory_order_relaxed);
    }
    bool changed = false;
    for (const StatValue& old : values) {
        if (Lookup(wanted, old.stat, old.layer) == 0) {
            SetListStat(context, list, old.stat, 0, old.layer);
            BonusValuesWritten.fetch_add(1, std::memory_order_relaxed);
            changed = true;
        }
    }
    for (const StatValue& v : wanted) {
        if (Lookup(values, v.stat, v.layer) == v.value) continue;
        SetListStat(context, list, v.stat, v.value, v.layer);
        BonusValuesWritten.fetch_add(1, std::memory_order_relaxed);
        changed = true;
    }
    values = wanted;
    return changed;
}

// Socketed Runes: the bonus of one host item.
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
    // On the client the bonus goes into a list the item's tooltip draws (see
    // TOOLTIPS in the header); everywhere else it is a list of its own.
    void* const target = game == nullptr && Settings.runes.tooltips ? TooltipTarget(item) : nullptr;
    if (entry != nullptr) {
        if (entry->merged) {
            // Its additions are gone when the item was rebuilt; then nothing
            // of the kept address or values is touched again.
            if (MergeIntact(item, *entry)) {
                if (target == nullptr || target != entry->list) Unmerge(item, *entry);
            } else {
                entry->list = nullptr;
                entry->values.clear();
                entry->expect.clear();
                entry->merged = false;
            }
        } else if (!OwnsList(item, entry->list, entry->values)) {
            // The item's lists were rebuilt; whatever was ours went with them.
            entry->list = nullptr;
            entry->values.clear();
        } else if (target != nullptr) {
            RemoveList(item, entry->list);
            entry->list = nullptr;
            entry->values.clear();
        }
    }
    if (wanted.empty()) {
        if (!entry->values.empty()) Diagnostic("Archaeology: %s item %u: rune bonus removed.", game != nullptr ? "host" : "client", id);
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
    if (target != nullptr) {
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
            const std::int32_t before = ListValue(target, t.stat, t.layer);
            const std::int32_t after  = before - Lookup(current, t.stat, t.layer) + Lookup(wanted, t.stat, t.layer);
            if (after != before) {
                SetListStat(context, target, t.stat, after, t.layer);
                BonusValuesWritten.fetch_add(1, std::memory_order_relaxed);
                changed = true;
            }
            entry->expect.push_back({ t.stat, t.layer, ListValue(target, t.stat, t.layer) });
        }
        entry->list   = target;
        entry->merged = true;
        if (changed) {
            Diagnostic("Archaeology: client item %u (body location %d), shown in its own lines:%s", id, loc, Describe(wanted).c_str());
        }
        entry->values = wanted;
        entry->seen   = true;
        return;
    }
    if (WriteOwnList(item, UnitItem, id, entry->list, entry->values, wanted)) {
        Diagnostic("Archaeology: %s item %u (body location %d):%s", game != nullptr ? "host" : "client", id, loc, Describe(wanted).c_str());
    }
    entry->seen = true;
}

// Containers Opened: the character's own list.
void ReconcilePlayer(BonusSide& side, void* game, void* player, std::int32_t count, const std::vector<StatValue>& wanted) noexcept {
    const std::uint32_t id = Read<std::uint32_t>(player, UnitId);
    PlayerEntry* entry = nullptr;
    std::size_t index = 0;
    for (; index < side.players.size(); ++index) {
        if (side.players[index].id == id && side.players[index].game == game) {
            entry = &side.players[index];
            break;
        }
    }
    if (entry == nullptr && wanted.empty()) return;
    if (entry != nullptr && entry->list != nullptr && !OwnsList(player, entry->list, entry->values)) {
        // Freed behind the plugin's back (the death cleanup): never touched again.
        entry->list = nullptr;
        entry->values.clear();
    }
    if (wanted.empty()) {
        if (entry->list != nullptr) RemoveList(player, entry->list);
        if (!entry->values.empty()) Diagnostic("Archaeology: %s character %u: container bonus removed.", game != nullptr ? "host" : "client", id);
        side.players.erase(side.players.begin() + static_cast<std::ptrdiff_t>(index));
        return;
    }
    if (entry == nullptr) {
        side.players.push_back({ game, id, nullptr, {}, 0, false });
        entry = &side.players.back();
    }
    if (WriteOwnList(player, UnitPlayer, id, entry->list, entry->values, wanted)) {
        Diagnostic("Archaeology: %s character %u, %d containers:%s", game != nullptr ? "host" : "client", id, count,
            Describe(wanted).c_str());
    }
    entry->count = count;
    entry->seen = true;
}

// ---------------------------------------------------------------------------
// Containers Opened: who opened what, and when
// ---------------------------------------------------------------------------
struct OpenedContainer {
    std::uint32_t object;
    std::int32_t  frame;
};

struct ContainerTrack {
    void*                        game{};
    std::uint32_t                player{};
    std::vector<OpenedContainer> opened;
};

struct PublishedCount {
    std::uint32_t player;
    std::int32_t  count;
};

std::mutex                  TrackMutex;          // the game thread writes, the console and the UI thread read
std::vector<ContainerTrack> Tracks;
std::uint64_t               TracksGeneration{};
std::vector<PublishedCount> Published;           // the host's counts, by unit id
std::uint64_t               PublishedGeneration{};

auto WindowFrames() noexcept -> std::int32_t {
    return Settings.containers.seconds * FramesPerSecond;
}

auto GameFrame(void* game) noexcept -> std::int32_t {
    return Read<std::int32_t>(game, GameFrameOffset);
}

// Under TrackMutex.
auto FindTrack(void* game, std::uint32_t player, bool create) -> ContainerTrack* {
    const std::uint64_t generation = Generation.load();
    if (TracksGeneration != generation) {
        Tracks.clear();
        TracksGeneration = generation;
    }
    for (ContainerTrack& track : Tracks) {
        if (track.game == game && track.player == player) return &track;
    }
    if (!create) return nullptr;
    Tracks.push_back({ game, player, {} });
    return &Tracks.back();
}

// Drops what is older than the window (and anything stamped in the future: a
// frame counter that went backwards belongs to another game).
void Prune(ContainerTrack& track, std::int32_t now) noexcept {
    const std::int32_t window = WindowFrames();
    track.opened.erase(std::remove_if(track.opened.begin(), track.opened.end(), [&](const OpenedContainer& c) noexcept {
        return c.frame > now || now - c.frame >= window;
    }), track.opened.end());
}

// Server game thread, right after the container's loot was rolled.
void RecordContainer(void* game, void* object, void* player) noexcept {
    ContainerDrops.fetch_add(1, std::memory_order_relaxed);
    if (game == nullptr || UnitType(object) != UnitObject || !HasStatPerk(player, true, Settings.containers.stat)) return;
    const std::int32_t  now      = GameFrame(game);
    const std::uint32_t playerId = Read<std::uint32_t>(player, UnitId);
    const std::uint32_t objectId = Read<std::uint32_t>(object, UnitId);
    std::size_t count = 0;
    {
        std::lock_guard lock(TrackMutex);
        ContainerTrack* track = FindTrack(game, playerId, true);
        Prune(*track, now);
        for (const OpenedContainer& c : track->opened) {
            if (c.object == objectId) return;  // the same container, another pick
        }
        track->opened.push_back({ objectId, now });
        count = track->opened.size();
    }
    ContainersCounted.fetch_add(1, std::memory_order_relaxed);
    Diagnostic("Archaeology: character %u opened object %u (class %u) at frame %d: %zu containers in the window.", playerId,
        objectId, Read<std::uint32_t>(object, UnitClassId), now, count);
}

auto ContainerCount(void* game, std::uint32_t player) noexcept -> std::int32_t {
    const std::int32_t now = GameFrame(game);
    std::lock_guard lock(TrackMutex);
    ContainerTrack* track = FindTrack(game, player, false);
    if (track == nullptr) return 0;
    Prune(*track, now);
    return static_cast<std::int32_t>(track->opened.size());
}

void ForgetContainers(void* game, std::uint32_t player) noexcept {
    std::lock_guard lock(TrackMutex);
    if (ContainerTrack* track = FindTrack(game, player, false)) track->opened.clear();
}

auto PublishedFor(std::uint32_t player) noexcept -> std::int32_t {
    std::lock_guard lock(TrackMutex);
    if (PublishedGeneration != Generation.load()) return 0;
    for (const PublishedCount& p : Published) {
        if (p.player == player) return p.count;
    }
    return 0;
}

// ---------------------------------------------------------------------------
// Who a drop belongs to (see the header)
// ---------------------------------------------------------------------------
// The player a treasure class drop is credited to, for exactly the duration
// of that drop on this thread; null everywhere else.
thread_local void* DropPlayer{};

// The inline hook on 0x441300 (monster drops). All ten arguments go through
// unchanged.
auto __fastcall HookGenerateDrops(void* game, void* source, void* killer, std::uint64_t treasureClass, std::uint64_t a5,
        std::uint64_t a6, std::uint64_t a7, void* items, void* count, std::uint64_t maxItems) noexcept -> std::int64_t {
    void* const previous = DropPlayer;
    DropPlayer = Ready && Settings.enabled ? ResolvePlayer(killer) : nullptr;
    const std::int64_t result = OriginalGenerateDrops(game, source, killer, treasureClass, a5, a6, a7, items, count, maxItems);
    DropPlayer = previous;
    return result;
}

// The inline hook on 0x5930B0 (containers).
auto __fastcall HookContainerDrop(void** operate, std::int32_t mode) noexcept -> std::int64_t {
    void* const previous = DropPlayer;
    void* const opener = operate != nullptr ? operate[2] : nullptr;
    DropPlayer = Ready && Settings.enabled && UnitType(opener) == UnitPlayer ? opener : nullptr;
    const std::int64_t result = OriginalContainerDrop(operate, mode);
    DropPlayer = previous;
    if (Ready && Settings.enabled && Settings.containers.enabled && ContainerHooked && operate != nullptr) {
        RecordContainer(operate[0], operate[1], operate[2]);
    }
    return result;
}

auto ContainerValues(std::int32_t count, std::uint64_t rows, std::vector<StatValue>& out) noexcept -> void {
    out.clear();
    if (count <= 0) return;
    const std::int64_t value = static_cast<std::int64_t>(count) * Settings.containers.percent;
    if (value == 0) return;
    for (const std::int32_t stat : Settings.containers.stats) {
        if (stat < 0 || static_cast<std::uint64_t>(stat) >= rows || stat >= MaxStatId) continue;
        AddValue(out, stat, 0, std::min<std::int64_t>(value, INT32_MAX));
    }
}

auto ItemCodeOf(void* item) noexcept -> std::uint32_t {
    const std::uint8_t* record = ItemsRecord(GetDataContext(item), Read<std::uint32_t>(item, UnitClassId));
    return record != nullptr ? NormalizedCode(Read<std::uint32_t>(record, ItemCode)) : 0;
}

// ---------------------------------------------------------------------------
// Drop tables: the item types and classes the drop perks use, per data context
// ---------------------------------------------------------------------------
auto CodeMatches(const std::uint8_t* row, const std::string& code) noexcept -> bool {
    if (code.empty() || code.size() > 4) return false;
    for (std::size_t i = 0; i < 4; ++i) {
        const char want = i < code.size() ? code[i] : ' ';
        const char have = static_cast<char>(row[i]);
        if (i < code.size() ? (have != want) : (have != ' ' && have != '\0')) return false;
    }
    return true;
}

std::atomic<std::uint32_t> UnknownTypeCodes{};

auto ResolveCode(const std::uint8_t* tables, const std::string& code) -> std::int32_t {
    const auto* rows = Read<const std::uint8_t*>(tables, TablesItemTypes);
    const auto count = Read<std::uint64_t>(tables, TablesItemTypeCount);
    if (rows == nullptr || count == 0 || count > 4096) return -1;
    for (std::uint64_t i = 0; i < count; ++i) {
        if (CodeMatches(rows + i * ItemTypeStride, code)) return static_cast<std::int32_t>(i);
    }
    if (UnknownTypeCodes.fetch_add(1) < 16) {
        D2RL::LogWarnF(Context, "Archaeology: item type code \"%s\" is not in itemtypes.txt; it is ignored.", code.c_str());
    }
    return -1;
}

auto ResolveCodes(const std::uint8_t* tables, const std::vector<std::string>& codes) -> std::vector<std::int32_t> {
    std::vector<std::int32_t> ids;
    for (const std::string& code : codes) {
        const std::int32_t id = ResolveCode(tables, code);
        if (id >= 0) ids.push_back(id);
    }
    return ids;
}

auto ItemMatches(void* item, const std::vector<std::int32_t>& types) noexcept -> bool {
    for (const std::int32_t type : types) {
        if (CheckItemType(item, type) != 0) return true;
    }
    return false;
}

struct DropTables {
    const std::uint8_t*        tables{};
    std::vector<std::int32_t>  socketTypes, sellTypes, couponTypes, excludeEtherealTypes;
    std::int32_t               weapon{ -1 }, armor{ -1 };
    std::int32_t               goldClass{ -1 };
    std::vector<std::uint32_t> normalCoupons, exceptionalCoupons, eliteCoupons;  // sorted classes
    std::vector<std::pair<std::uint32_t, std::uint32_t>> crystals;               // (chipped or flawed, normal)
    std::vector<std::pair<std::uint32_t, std::uint32_t>> socketTwins;            // (base, socketed twin)
    std::vector<std::uint32_t> mythical;                                        // sorted classes
};

std::mutex                DropMutex;
std::array<DropTables, 4> DropCache{};

auto RecordHasType(const std::uint8_t* record, std::int32_t type) noexcept -> bool {
    return type >= 0 && (Read<std::int16_t>(record, ItemType1) == type || Read<std::int16_t>(record, ItemType2) == type);
}

auto DropTablesFor(std::uint8_t context) -> const DropTables* {
    if (context >= DropCache.size()) return nullptr;
    const std::uint8_t* tables = GetDataTables(context);
    if (tables == nullptr) return nullptr;
    std::lock_guard lock(DropMutex);
    DropTables& d = DropCache[context];
    if (d.tables == tables) return &d;
    d = DropTables{};
    d.tables               = tables;
    d.socketTypes          = ResolveCodes(tables, Settings.sockets.types);
    d.sellTypes            = ResolveCodes(tables, Settings.sell.types);
    d.couponTypes          = ResolveCodes(tables, Settings.couponEthereal.couponTypes);
    d.excludeEtherealTypes = ResolveCodes(tables, Settings.couponEthereal.excludeTypes);
    d.weapon               = ResolveCode(tables, "weap");
    d.armor                = ResolveCode(tables, "armo");
    const std::int32_t normal      = ResolveCode(tables, Settings.couponUpgrade.normalType);
    const std::int32_t exceptional = ResolveCode(tables, Settings.couponUpgrade.exceptionalType);
    const std::int32_t elite       = ResolveCode(tables, Settings.couponUpgrade.eliteType);
    std::vector<std::uint32_t> mythicalCodes;
    for (const std::string& code : Settings.mythical.codes) mythicalCodes.push_back(PackCode(code));
    std::sort(mythicalCodes.begin(), mythicalCodes.end());
    std::vector<std::pair<std::uint32_t, std::uint32_t>> byCode;  // (code, class)
    const auto count = Read<std::uint64_t>(tables, TablesItemCount);
    for (std::uint32_t cls = 0; cls < count && cls < 65536; ++cls) {
        const std::uint8_t* record = ItemsRecord(context, cls);
        if (record == nullptr) continue;
        const std::uint32_t code = NormalizedCode(Read<std::uint32_t>(record, ItemCode));
        byCode.emplace_back(code, cls);
        if (code == PackCode("gld") && d.goldClass < 0) d.goldClass = static_cast<std::int32_t>(cls);
        if (RecordHasType(record, normal)) d.normalCoupons.push_back(cls);
        if (RecordHasType(record, exceptional)) d.exceptionalCoupons.push_back(cls);
        if (RecordHasType(record, elite)) d.eliteCoupons.push_back(cls);
        if (std::binary_search(mythicalCodes.begin(), mythicalCodes.end(), code)) d.mythical.push_back(cls);
    }
    std::sort(byCode.begin(), byCode.end());
    const auto classOf = [&byCode](std::uint32_t code) -> std::int64_t {
        const auto it = std::lower_bound(byCode.begin(), byCode.end(), std::make_pair(code, 0U));
        return it != byCode.end() && it->first == code ? static_cast<std::int64_t>(it->second) : -1;
    };
    for (const std::string& code : Settings.crystal.codes) {
        if (code.size() != 3 || code[0] != 'x') continue;
        const std::string upgraded{ 'x', 't', code[2] };
        const std::int64_t from = classOf(PackCode(code));
        const std::int64_t to   = classOf(PackCode(upgraded));
        if (from >= 0 && to >= 0 && from != to) d.crystals.emplace_back(static_cast<std::uint32_t>(from), static_cast<std::uint32_t>(to));
    }
    for (const std::string& pair : Settings.sockets.twins) {
        const std::size_t colon = pair.find(':');
        if (colon == std::string::npos) continue;
        const std::int64_t from = classOf(PackCode(std::string_view(pair).substr(0, colon)));
        const std::int64_t to   = classOf(PackCode(std::string_view(pair).substr(colon + 1)));
        if (from >= 0 && to >= 0 && from != to) d.socketTwins.emplace_back(static_cast<std::uint32_t>(from), static_cast<std::uint32_t>(to));
    }
    for (auto* v : { &d.normalCoupons, &d.exceptionalCoupons, &d.eliteCoupons, &d.mythical }) std::sort(v->begin(), v->end());
    std::sort(d.crystals.begin(), d.crystals.end());
    std::sort(d.socketTwins.begin(), d.socketTwins.end());
    return &d;
}

auto Contains(const std::vector<std::uint32_t>& sorted, std::uint32_t value) noexcept -> bool {
    return std::binary_search(sorted.begin(), sorted.end(), value);
}

// rand(range) on the dropping unit's seed, as the drop's own rolls; range when
// there is no seed (a miss for every chance below).
auto RollOn(void* unit, std::uint32_t range) noexcept -> std::uint32_t {
    void* const seed = unit != nullptr && range != 0 ? UnitSeed(unit) : nullptr;
    return seed != nullptr ? RollRandom(seed, range) : range;
}

// Sockets for a base that cannot hold one: the roll is made before the item is
// built, the socketed twin is built instead, and the socket follows without a
// second roll (FinishBuiltItem, inside that same build).
thread_local bool ForceSocket{};

auto SocketTwinFor(void* player, void* source, std::uint32_t cls, const DropTables& d) noexcept -> std::uint32_t {
    const SocketConfig& k = Settings.sockets;
    if (!SocketsReady || !k.enabled || k.chance <= 0 || d.socketTwins.empty() || !HasStatPerk(player, true, k.stat)) return cls;
    const auto it = std::lower_bound(d.socketTwins.begin(), d.socketTwins.end(), std::make_pair(cls, 0U));
    if (it == d.socketTwins.end() || it->first != cls) return cls;
    if (k.chance < 100 && RollOn(source, 100) >= static_cast<std::uint32_t>(k.chance)) return cls;
    ForceSocket = true;
    SocketTwinsDropped.fetch_add(1, std::memory_order_relaxed);
    return it->second;
}

// Sockets: one socket for an item of the types that has none and can take one.
void MaybeAddSocket(void* item, void* player) noexcept {
    if (ForceSocket) {
        ForceSocket = false;
        if ((ItemFlags(item) & SocketedFlag) == 0 && MaxSockets(item) != 0) {
            AddSockets(item, 1);
            SocketsAdded.fetch_add(1, std::memory_order_relaxed);
        }
        return;
    }
    const SocketConfig& k = Settings.sockets;
    if (!SocketsReady || !k.enabled || k.chance <= 0 || !HasStatPerk(player, true, k.stat)) return;
    const DropTables* d = DropTablesFor(GetDataContext(item));
    if (d == nullptr || !ItemMatches(item, d->socketTypes) || (ItemFlags(item) & SocketedFlag) != 0 || MaxSockets(item) == 0) return;
    if (k.chance < 100) {
        void* const seed = ItemSeed(item);
        if (seed == nullptr || RollRandom(seed, 100) >= static_cast<std::uint32_t>(k.chance)) return;
    }
    AddSockets(item, 1);
    SocketsAdded.fetch_add(1, std::memory_order_relaxed);
}

// Coupon Ethereal: the player of a cube recipe with a coupon among its inputs,
// for exactly the duration of that recipe's output on this thread.
thread_local void* CubeCouponPlayer{};

// The item roll's own ethereal gate (see the anchors), plus the excluded types.
auto EtherealEligible(void* item, const DropTables& d) noexcept -> bool {
    const std::int32_t quality = ItemQuality(item);
    if (quality >= 10 || quality == QualityLow || quality == QualitySet || ItemVersion(item) < 100) return false;
    const bool gear = (d.weapon >= 0 && CheckItemType(item, d.weapon) != 0) || (d.armor >= 0 && CheckItemType(item, d.armor) != 0);
    return gear && HasDurability(item) != 0 && !ItemMatches(item, d.excludeEtherealTypes) && (ItemFlags(item) & EtherealFlag) == 0;
}

void MaybeEthereal(void* item) noexcept {
    const CouponEtherealConfig& k = Settings.couponEthereal;
    if (!EtherealReady || !k.enabled || k.chance <= 0) return;
    const DropTables* d = DropTablesFor(GetDataContext(item));
    if (d == nullptr || !EtherealEligible(item, *d)) return;
    CouponOutputs.fetch_add(1, std::memory_order_relaxed);
    if (k.chance < 100) {
        void* const seed = ItemSeed(item);
        if (seed == nullptr || RollRandom(seed, 100) >= static_cast<std::uint32_t>(k.chance)) return;
    }
    // The drop's own sequence (4434FC..443558): the ethereal routine, then half
    // the base maximum durability plus one, and full durability.
    ApplyEthereal(item);
    if (HasDurability(item) != 0) {
        const std::int32_t base = ReadOwnStat(item, StatMaxDurability, 0);
        SetUnitStat(item, StatMaxDurability, base / 2 + 1, 0);
        SetUnitStat(item, StatDurability, ReadUnitStat(item, StatMaxDurability, 0), 0);
    }
    CouponEthereals.fetch_add(1, std::memory_order_relaxed);
}

auto CubeHoldsCoupon(void* player, const DropTables& d) noexcept -> bool {
    void* const inventory = GetInventory(player, "archaeology", 0);
    void* item = inventory != nullptr ? FirstItem(inventory) : nullptr;
    for (std::size_t guard = 0; item != nullptr && guard < MaxInventoryItems; ++guard) {
        if (UnitType(item) != UnitItem) break;
        void* const next = NextItem(item);
        if (AnimMode(item) == 0 && ItemPage(item) == CubePage && ItemMatches(item, d.couponTypes)) return true;
        item = next;
    }
    return false;
}

// Entered from the relay at 4F27BD in place of the cube generator 0x5269C0
// (game, player, recipe, match).
auto __fastcall HookCubeGenerate(void* game, void* player, void* record, void* match) noexcept -> std::int64_t {
    void* const previous = CubeCouponPlayer;
    void* crafter = nullptr;
    const CouponEtherealConfig& k = Settings.couponEthereal;
    if (Ready && Settings.enabled && EtherealReady && k.enabled && k.chance > 0 && HasStatPerk(player, true, k.stat)) {
        const DropTables* d = DropTablesFor(GetDataContext(player));
        if (d != nullptr && CubeHoldsCoupon(player, *d)) crafter = player;
    }
    CubeCouponPlayer = crafter;
    const std::int64_t result = OriginalCubeGenerate(game, player, record, match);
    CubeCouponPlayer = previous;
    return result;
}

// Coupon Upgrade and Crystal Upgrade: the class the drop is made as.
auto UpgradeDrop(void* player, void* source, std::uint32_t cls, const DropTables& d) noexcept -> std::uint32_t {
    const CouponUpgradeConfig& u = Settings.couponUpgrade;
    if (u.enabled && HasStatPerk(player, true, u.stat)) {
        if (u.exceptionalChance > 0 && !d.exceptionalCoupons.empty() && Contains(d.normalCoupons, cls)
                && RollOn(source, 100) < static_cast<std::uint32_t>(u.exceptionalChance)) {
            const auto size = static_cast<std::uint32_t>(d.exceptionalCoupons.size());
            cls = d.exceptionalCoupons[RollOn(source, size) % size];
            CouponsUpgraded.fetch_add(1, std::memory_order_relaxed);
        }
        if (u.eliteChance > 0 && !d.eliteCoupons.empty() && Contains(d.exceptionalCoupons, cls)
                && RollOn(source, 100) < static_cast<std::uint32_t>(u.eliteChance)) {
            const auto size = static_cast<std::uint32_t>(d.eliteCoupons.size());
            cls = d.eliteCoupons[RollOn(source, size) % size];
            CouponsToElite.fetch_add(1, std::memory_order_relaxed);
        }
    }
    const CrystalConfig& c = Settings.crystal;
    if (c.enabled && c.chance > 0 && HasStatPerk(player, true, c.stat)) {
        const auto it = std::lower_bound(d.crystals.begin(), d.crystals.end(), std::make_pair(cls, 0U));
        if (it != d.crystals.end() && it->first == cls && RollOn(source, 100) < static_cast<std::uint32_t>(c.chance)) {
            cls = it->second;
            CrystalsUpgraded.fetch_add(1, std::memory_order_relaxed);
        }
    }
    return cls;
}

// Sell Value's pile, for exactly the time between its spawn and the gold
// step that follows it in the treasure class roll.
thread_local void*        SellPile{};
thread_local std::int32_t SellPrice{};

// Sell Value: a magic item of the types leaves a gold pile worth its vendor
// price instead. The pile is built from the request the item was built from,
// so it lands where the item would have.
auto SellInsteadOfDrop(void* game, void* player, void* item, const std::array<std::uint8_t, RequestSize>& request, std::int32_t flag,
        const DropTables& d) noexcept -> void* {
    const SellConfig& s = Settings.sell;
    if (!SellReady || !s.enabled || d.goldClass < 0 || !HasStatPerk(player, true, s.stat)) return item;
    if (UnitType(item) != UnitItem || ItemQuality(item) != QualityMagic || !ItemMatches(item, d.sellTypes)) return item;
    const std::uint8_t difficulty = Read<std::uint8_t>(game, GameDifficulty);
    void* const data = difficulty < 3 ? PlayerData(player) : nullptr;
    void* const quests = data != nullptr ? Read<void*>(data, PlayerQuestFlags + difficulty * 8U) : nullptr;
    const std::int32_t price = TransactionCost(player, item, difficulty, quests, s.vendor, TransactionSell);
    FreeUnit(game, item);
    ItemsSold.fetch_add(1, std::memory_order_relaxed);
    if (price <= 0) return nullptr;
    std::array<std::uint8_t, RequestSize> pile = request;
    const std::int32_t gold = d.goldClass, normal = QualityNormal, row = 0;
    std::memcpy(pile.data() + RequestClass, &gold, 4);
    std::memcpy(pile.data() + RequestQuality, &normal, 4);
    std::memcpy(pile.data() + RequestRow, &row, 4);
    void* const built = OriginalBuild(game, pile.data(), flag);
    if (built == nullptr) return nullptr;
    StatWrite(built, StatGold, price, 0);
    SellPile  = built;
    SellPrice = price;
    GoldFromSales.fetch_add(static_cast<std::uint64_t>(price), std::memory_order_relaxed);
    Diagnostic("Archaeology: Sell Value, a magic item drops as %d gold for character %u.", price, Read<std::uint32_t>(player, UnitId));
    return built;
}

// The inline hook on the item builder 0x43CD50 (game, request, flag).
auto __fastcall HookBuildItem(void* game, std::uint8_t* request, std::int32_t flag) noexcept -> void* {
    void* const player = DropPlayer;
    if (player == nullptr || request == nullptr || !Ready || !Settings.enabled) return OriginalBuild(game, request, flag);
    const DropTables* d = DropTablesFor(GetDataContext(player));
    std::array<std::uint8_t, RequestSize> pristine{};
    std::memcpy(pristine.data(), request, RequestSize);
    const std::int32_t cls = Read<std::int32_t>(request, RequestClass);
    ForceSocket = false;
    if (d != nullptr && cls >= 0) {
        void* const source = Read<void*>(request, RequestSource);
        auto upgraded = UpgradeDrop(player, source, static_cast<std::uint32_t>(cls), *d);
        upgraded = SocketTwinFor(player, source, upgraded, *d);
        if (static_cast<std::int32_t>(upgraded) != cls) std::memcpy(request + RequestClass, &upgraded, 4);
    }
    void* const item = OriginalBuild(game, request, flag);
    ForceSocket = false;
    return item != nullptr && d != nullptr ? SellInsteadOfDrop(game, player, item, pristine, flag, *d) : item;
}

// Magic Runewords: the player socketing an item on the server, for exactly
// the duration of that insertion on this thread.
thread_local void* SocketingPlayer{};

auto __fastcall HookSocketInsert(void* game, void* player, std::uint32_t socketable, std::uint32_t host, std::int32_t flag) noexcept
        -> std::int32_t {
    void* const previous = SocketingPlayer;
    SocketingPlayer = player;
    const std::int32_t result = OriginalSocketInsert(game, player, socketable, host, flag);
    SocketingPlayer = previous;
    return result;
}

// Entered from the relay at 37246C for a magic item. 1 lets the rune match go on.
auto __fastcall AllowMagicRuneword(void* item) noexcept -> std::int32_t {
    if (!Ready || !Settings.enabled || !Settings.runewords.enabled || item == nullptr) return 0;
    if ((ItemFlags(item) & RunewordFlag) != 0) return 1;  // already a magic runeword: keep resolving it
    void* const player = SocketingPlayer;
    if (!HasStatPerk(player, true, Settings.runewords.stat)) return 0;
    MagicRunewordsAllowed.fetch_add(1, std::memory_order_relaxed);
    return 1;
}

// ---------------------------------------------------------------------------
// Mass Dig: Find Item on every corpse in reach
// ---------------------------------------------------------------------------
SkillsRecordFn SkillsRecord{};
UnitIntFn      CorpseUsable{};
UnitStateFn    HasState{};
SetStateFn     SetState{};
UnitFn         CorpseRefresh{};
SkillCalcFn    SkillCalc{};
FindDropFn     FindDrop{};
AreaSearchFn   AreaSearch{};

struct MassDigContext {
    std::int32_t        skill;
    std::int32_t        level;
    const std::uint8_t* record;
    std::uint8_t        context;
};

// One corpse, step by step as Find Item's 0x567D00 does it for its target.
auto __fastcall MassDigVisit(void** search, void* corpse) noexcept -> std::uint32_t {
    auto* const dig = static_cast<MassDigContext*>(search[3]);
    void* const game = search[0];
    void* const caster = search[1];
    if (dig == nullptr || corpse == nullptr || CorpseUsable(corpse) == 0 || HasState(corpse, StateSearched) != 0) return 0;
    SetState(corpse, StateSearched, 1);
    CorpseRefresh(corpse);
    MassDigCorpses.fetch_add(1, std::memory_order_relaxed);
    const std::int32_t chance = Settings.massDig.chance >= 0 ? Settings.massDig.chance
        : SkillCalc(dig->context, caster, Read<std::int32_t>(dig->record, SkillCalc1), dig->skill, dig->level);
    void* const seed = UnitSeed(caster);
    if (seed == nullptr || static_cast<std::int32_t>(RollRandom(seed, 100)) >= chance) return 1;
    const auto roll = static_cast<std::int32_t>(RollRandom(seed, 100));
    const std::int32_t p1 = Read<std::int32_t>(dig->record, SkillParam1);
    const std::int32_t p2 = p1 + Read<std::int32_t>(dig->record, SkillParam1 + 4);
    const std::int32_t p3 = p2 + Read<std::int32_t>(dig->record, SkillParam1 + 8);
    const std::int32_t p4 = p3 + Read<std::int32_t>(dig->record, SkillParam1 + 12);
    std::int32_t kind = 1;
    if (roll >= p1 && roll < p2) kind = 2;
    else if (roll >= p2 && roll < p3) kind = 3;
    else if (roll >= p3 && roll < p4) kind = 4;
    FindDrop(game, corpse, caster, kind);
    MassDigFinds.fetch_add(1, std::memory_order_relaxed);
    return 1;
}

// Installed as srvdofunc [mass_dig] srvdofunc: (game, unit, skill id, level).
auto __fastcall MassDigDo(void* game, void* unit, std::int32_t skill, std::int32_t level) noexcept -> std::int32_t {
    if (!Ready || !Settings.enabled || !Settings.massDig.enabled || game == nullptr || unit == nullptr) return 0;
    const std::uint8_t context = Read<std::uint8_t>(game, 0x106);
    const std::uint8_t* const record = SkillsRecord(context, skill);
    if (record == nullptr) return 0;
    std::int32_t radius = Settings.massDig.radius;
    if (radius < 1) {
        // The row's aurarangecalc, as Static Field evaluates its own.
        const std::int32_t calc = Read<std::int32_t>(record, SkillAuraRangeCalc);
        radius = calc != 0 ? SkillCalc(context, unit, calc, skill, level) : 0;
        if (radius < 1) {
            MassDigNoRange.fetch_add(1, std::memory_order_relaxed);
            return 0;
        }
        radius = std::min(radius, 100);
    }
    MassDigContext dig{ skill, level, record, context };
    MassDigCasts.fetch_add(1, std::memory_order_relaxed);
    AreaSearch(game, unit, 0, 0, radius, CorpseFilter, &MassDigVisit, &dig, 0, "archaeology", __LINE__);
    return 1;
}

// ---------------------------------------------------------------------------
// Skill Scrolls
// ---------------------------------------------------------------------------
ServerDoFn ServerDo{};

void BuildScrolls() {
    Scrolls.clear();
    for (const std::string& entry : Settings.scrolls.entries) {
        const std::size_t a = entry.find(',');
        const std::size_t b = a == std::string::npos ? a : entry.find(',', a + 1);
        std::int32_t skill = 0, level = 0;
        const std::string code(Trim(std::string_view(entry).substr(0, a)));
        if (b == std::string::npos || code.empty() || code.size() > 4
                || !ParseInt(Trim(std::string_view(entry).substr(a + 1, b - a - 1)), skill)
                || !ParseInt(Trim(std::string_view(entry).substr(b + 1)), level) || skill < 0 || level < 1) {
            Problem("skill_scrolls.scrolls: \"" + entry + "\" is not \"item code,skill id,skill level\"");
            continue;
        }
        Scrolls.push_back({ PackCode(code), skill, level });
    }
    ScrollWarned.assign(Scrolls.size(), 0);
}

// The item casters (see CastOnTargetRva).
using CastOnTargetFn   = std::int32_t (__fastcall*)(void* unit, std::int32_t skill, std::int32_t level, void* target, std::int32_t flag);
using CastAtPositionFn = std::int32_t (__fastcall*)(void* unit, std::int32_t skill, std::int32_t level, std::int32_t x, std::int32_t y,
                                                    std::int32_t flag);
using TargetUnitFn     = void* (__fastcall*)(void* game, void* unit);
using TargetCoordFn    = std::int32_t (__fastcall*)(void* unit);
using ItemRestrictFn   = bool (__fastcall*)(void* unit, std::int32_t skill);
CastOnTargetFn   CastOnTarget{};
CastAtPositionFn CastAtPosition{};
TargetUnitFn     TargetUnit{};
TargetCoordFn    TargetX{};
TargetCoordFn    TargetY{};
ItemRestrictFn   ItemRestrict{};
constexpr std::int32_t ItemCastFlag = 1;   // as item event function 20 passes it

// What the item casters refuse without casting, checked before the item is
// used up: an uninterruptable caster, a skill with no ItemEffect, and a skill
// whose item restriction the unit fails. Game thread.
auto ArtefactCastable(void* game, void* player, std::size_t index) noexcept -> bool {
    const ScrollEntry& scroll = Scrolls[index];
    if (HasState(player, StateUninterruptable) != 0) return false;
    const std::uint8_t* const record = SkillsRecord(Read<std::uint8_t>(game, 0x106), scroll.skill);
    if (record == nullptr || Read<std::uint16_t>(record, SkillItemEffect) == 0) {
        if (index < ScrollWarned.size() && ScrollWarned[index] == 0) {
            ScrollWarned[index] = 1;
            D2RL::LogWarnF(Context, "Archaeology: skill %d has no skills.txt ItemEffect, so the game cannot cast it from an "
                "item; its artefacts can't be used until it has one.", scroll.skill);
        }
        return false;
    }
    if ((Read<std::uint8_t>(record, SkillItemFlags) & SkillItemRestrictBit) != 0 && !ItemRestrict(player, scroll.skill)) return false;
    return true;
}

auto ScrollIndexOf(void* item) noexcept -> std::int32_t {
    const std::uint32_t code = ItemCodeOf(item);
    if (code == 0) return -1;
    for (std::size_t i = 0; i < Scrolls.size(); ++i) {
        if (Scrolls[i].code == code) return static_cast<std::int32_t>(i);
    }
    return -1;
}

// Artefact cooldowns: one per character and entry, in server game frames.
// The game thread writes them; the host pass publishes what is left for the
// UI thread's sweep. A game joined or left (Generation) clears them all.
struct ArtefactCooldown {
    void*         game;
    std::uint32_t player;   // unit id
    std::uint32_t scroll;   // index in Scrolls
    std::int32_t  end;      // the game frame it is ready again
    std::int32_t  total;    // the frames it started with
};
struct PublishedCooldown {
    std::uint32_t player;
    std::uint32_t scroll;
    std::int32_t  left;
    std::int32_t  total;
};
std::mutex                     CooldownMutex;
std::vector<ArtefactCooldown>  Cooldowns;
std::uint64_t                  CooldownsGeneration{};
std::vector<PublishedCooldown> PublishedCooldowns;
std::uint64_t                  PublishedCooldownsGeneration{};
std::uint64_t                  PublishedCooldownsMs{};
thread_local bool              ArtefactCasting{};   // set on the game thread while an artefact's skill runs

// Under CooldownMutex.
void ForgetOldCooldowns() noexcept {
    const std::uint64_t generation = Generation.load();
    if (CooldownsGeneration != generation) {
        Cooldowns.clear();
        CooldownsGeneration = generation;
    }
}

auto CooldownLeft(void* game, std::uint32_t player, std::uint32_t scroll) noexcept -> std::int32_t {
    std::lock_guard lock(CooldownMutex);
    ForgetOldCooldowns();
    for (const ArtefactCooldown& c : Cooldowns) {
        if (c.game == game && c.player == player && c.scroll == scroll) return std::max(c.end - GameFrame(game), 0);
    }
    return 0;
}

void StartCooldown(void* game, std::uint32_t player, std::uint32_t scroll, std::int32_t frames) {
    std::lock_guard lock(CooldownMutex);
    ForgetOldCooldowns();
    const std::int32_t end = GameFrame(game) + frames;
    for (ArtefactCooldown& c : Cooldowns) {
        if (c.game == game && c.player == player && c.scroll == scroll) {
            c.end = end;
            c.total = frames;
            return;
        }
    }
    Cooldowns.push_back({ game, player, scroll, end, frames });
}

// Game thread, from the host pass: what is left of a live character's
// cooldowns. Finished ones are dropped.
void CollectCooldowns(void* game, std::uint32_t player, std::vector<PublishedCooldown>& out) {
    std::lock_guard lock(CooldownMutex);
    ForgetOldCooldowns();
    if (Cooldowns.empty()) return;
    const std::int32_t now = GameFrame(game);
    for (auto it = Cooldowns.begin(); it != Cooldowns.end();) {
        if (it->game == game && it->player == player) {
            const std::int32_t left = it->end - now;
            if (left <= 0) {
                it = Cooldowns.erase(it);
                continue;
            }
            out.push_back({ player, it->scroll, left, it->total });
        }
        ++it;
    }
}

// The entry's skill's localdelay at the entry's level, in frames, evaluated
// the way the server skill handler evaluates it for the player's own casts.
auto ArtefactDelay(void* game, void* player, const ScrollEntry& scroll) noexcept -> std::int32_t {
    const std::uint8_t context = Read<std::uint8_t>(game, 0x106);
    const std::uint8_t* const record = SkillsRecord(context, scroll.skill);
    if (record == nullptr) return 0;
    return std::max(SkillCalc(context, player, Read<std::int32_t>(record, SkillLocalDelayCalc), scroll.skill, scroll.level), 0);
}

// Entered from the relay at 581789 in place of the use callback.
auto __fastcall UseDispatch(void* game, void* player, void* target, UseCallbackFn original, void* item) noexcept -> std::uint8_t {
    if (Ready && Settings.enabled && Settings.scrolls.enabled && item != nullptr && UnitType(item) == UnitItem
            && UnitType(player) == UnitPlayer) {
        const std::int32_t index = ScrollIndexOf(item);
        if (index >= 0) {
            const ScrollEntry& scroll = Scrolls[static_cast<std::size_t>(index)];
            // Cast the way an item's chance to cast does: at the player's
            // target, else at the point he aims at. The caster queues the
            // packet that makes every client play the skill. The skill handler
            // would put the skill itself on its localdelay too; LocalDelayGate
            // skips that while this flag is set.
            void* const aim = TargetUnit(game, player);
            ArtefactCasting = true;
            const std::int32_t cast = aim != nullptr
                ? CastOnTarget(player, scroll.skill, scroll.level, aim, ItemCastFlag)
                : CastAtPosition(player, scroll.skill, scroll.level, TargetX(player), TargetY(player), ItemCastFlag);
            ArtefactCasting = false;
            ScrollsUsed.fetch_add(1, std::memory_order_relaxed);
            if (cast == 0) {
                ArtefactCastsFailed.fetch_add(1, std::memory_order_relaxed);
                Diagnostic("Archaeology: scroll %u: the game did not cast skill %d.", Read<std::uint32_t>(item, UnitId), scroll.skill);
                return 1;
            }
            (aim != nullptr ? ArtefactCastsAtUnit : ArtefactCastsAtSpot).fetch_add(1, std::memory_order_relaxed);
            const std::int32_t delay = UseRulePatched ? ArtefactDelay(game, player, scroll) : 0;
            if (delay > 0) {
                StartCooldown(game, Read<std::uint32_t>(player, UnitId), static_cast<std::uint32_t>(index), delay);
                ArtefactCooldownsStarted.fetch_add(1, std::memory_order_relaxed);
            }
            Diagnostic("Archaeology: scroll %u casts skill %d level %d for character %u; cooldown %d frames.",
                Read<std::uint32_t>(item, UnitId), scroll.skill, scroll.level, Read<std::uint32_t>(player, UnitId), delay);
            return 1;
        }
    }
    return original != nullptr ? original(game, player, target) : 0;
}

// Entered from the relay at 58195B in the use check 5817F0, once its generic
// checks passed, for an item that takes no target, in place of the item's
// pSpell rule. An artefact can be used anywhere but in town (levels.txt Town),
// also where a Town Portal can't be opened, while its cooldown is not running
// and the item casters would cast its skill (ArtefactCastable).
// Every other item keeps its pSpell's rule.
using PSpellRuleFn = bool (__fastcall*)(void* game, void* player, void* item);
using UnitRoomFn   = void* (__fastcall*)(void* unit);
using RoomInTownFn = std::int32_t (__fastcall*)(void* room);
UnitRoomFn   UnitRoom{};
RoomInTownFn RoomInTown{};

auto __fastcall ArtefactUseRule(void* game, void* player, void* item, PSpellRuleFn rule) noexcept -> bool {
    if (Ready && Settings.enabled && Settings.scrolls.enabled && game != nullptr && player != nullptr && item != nullptr
            && UnitType(player) == UnitPlayer && UnitType(item) == UnitItem) {
        const std::int32_t index = ScrollIndexOf(item);
        if (index >= 0) {
            void* const room = UnitRoom(player);
            if (room == nullptr || RoomInTown(room) != 0) {
                ArtefactTownRefusals.fetch_add(1, std::memory_order_relaxed);
                return false;
            }
            if (CooldownLeft(game, Read<std::uint32_t>(player, UnitId), static_cast<std::uint32_t>(index)) > 0) {
                ArtefactUsesRefused.fetch_add(1, std::memory_order_relaxed);
                return false;
            }
            if (ScrollPatched && !ArtefactCastable(game, player, static_cast<std::size_t>(index))) {
                ArtefactCastRefusals.fetch_add(1, std::memory_order_relaxed);
                return false;
            }
            return true;
        }
    }
    return rule(game, player, item);
}

// Entered from the relay at 43B1BF with the skill cooldown setter's own
// arguments: an artefact's cast leaves the character's skill cooldowns alone.
using LocalDelaySetFn = std::uint64_t (__fastcall*)(void* game, void* unit, std::int32_t frames, std::uint32_t skill);
LocalDelaySetFn LocalDelaySet{};

auto __fastcall LocalDelayGate(void* game, void* unit, std::int32_t frames, std::uint32_t skill) noexcept -> std::uint64_t {
    if (ArtefactCasting) {
        NativeCooldownsSkipped.fetch_add(1, std::memory_order_relaxed);
        return 0;
    }
    return LocalDelaySet(game, unit, frames, skill);
}

// Mythical Uniques: entered from the relay at 442453 after the unique roll
// failed with the final unique range. 1 makes the item unique.
auto __fastcall MythicalRoll(std::int32_t range, void* source, void* killer, std::uint32_t classId) noexcept -> std::int32_t {
    const MythicalConfig& m = Settings.mythical;
    if (!Ready || !Settings.enabled || !m.enabled || m.percent <= 0 || range <= UniqueThreshold || source == nullptr) return 0;
    void* const player = ResolvePlayer(killer);
    if (!HasStatPerk(player, true, m.stat)) return 0;
    const DropTables* d = DropTablesFor(GetDataContext(player));
    if (d == nullptr || !Contains(d->mythical, classId)) return 0;
    MythicalSecondRolls.fetch_add(1, std::memory_order_relaxed);
    // The first roll failed with probability (range - 128) / range; winning
    // want of the range - 128 outcomes left adds want / range, which is
    // percent of the first roll's 128 / range.
    const std::int64_t rest = range - UniqueThreshold;
    const std::int64_t want = static_cast<std::int64_t>(UniqueThreshold) * m.percent / 100;
    if (want <= 0) return 0;
    if (want < rest && RollOn(source, static_cast<std::uint32_t>(rest)) >= static_cast<std::uint32_t>(want)) return 0;
    MythicalUniques.fetch_add(1, std::memory_order_relaxed);
    return 1;
}

// ---------------------------------------------------------------------------
// Gold Find and Identified Drops
// ---------------------------------------------------------------------------
// Entered from the relay at 4411CD with the pile's gold after Gold Find.
auto __fastcall GoldAmount(std::int32_t gold, void* killer, void* pile) noexcept -> std::int32_t {
    if (pile != nullptr && pile == SellPile) {
        // Sell Value: exactly the vendor price, no Gold Find on top.
        SellPile = nullptr;
        return SellPrice;
    }
    const GoldConfig& g = Settings.gold;
    const TotalGoldConfig& t = Settings.totalGold;
    const bool perk = g.enabled && g.percent != 0;
    const bool stat = t.enabled && t.stat >= 0;
    if (!Ready || !Settings.enabled || (!perk && !stat) || gold <= 0) return gold;
    void* const player = ResolvePlayer(killer);
    if (player == nullptr) return gold;
    // The perk's percent and Total Gold Found's value are one multiplier on
    // the pile after Gold Find, never below -100 together.
    const std::int64_t perkPercent = perk && HasStatPerk(player, true, g.stat) ? g.percent : 0;
    const std::int32_t statPercent = stat && static_cast<std::uint64_t>(t.stat) < StatRows(player) ? ReadUnitStat(player, t.stat, 0) : 0;
    if (statPercent != 0) {
        TotalGoldPiles.fetch_add(1, std::memory_order_relaxed);
        TotalGoldLast.store(statPercent, std::memory_order_relaxed);
    }
    const std::int64_t percent = std::max<std::int64_t>(perkPercent + statPercent, -100);
    if (percent == 0) return gold;
    const std::int64_t changed = std::min<std::int64_t>(static_cast<std::int64_t>(gold) * (100 + percent) / 100, INT32_MAX);
    GoldPilesRaised.fetch_add(1, std::memory_order_relaxed);
    GoldAdded.fetch_add(changed - gold, std::memory_order_relaxed);
    Diagnostic("Archaeology: Gold Find, a pile of %d becomes %lld for character %u (perk %+lld%%, Total Gold Found %+d%%).", gold,
        static_cast<long long>(changed), Read<std::uint32_t>(player, UnitId), static_cast<long long>(perkPercent), statPercent);
    return static_cast<std::int32_t>(changed);
}

// Entered from the relay at 43D4EA in place of 0x43C910 (item, 0): the item is
// rolled and nothing has been sent yet.
void __fastcall FinishBuiltItem(void* item, std::uint8_t flag) noexcept {
    OriginalFinishItem(item, flag);
    if (item == nullptr || !Ready || !Settings.enabled || UnitType(item) != UnitItem) return;
    if (void* const player = DropPlayer) {
        if (Settings.identify.enabled && HasStatPerk(player, true, Settings.identify.stat)) {
            SetItemFlag(item, IdentifiedFlag, 1);
            ItemsIdentified.fetch_add(1, std::memory_order_relaxed);
        }
        MaybeAddSocket(item, player);
    }
    if (CubeCouponPlayer != nullptr) MaybeEthereal(item);
}

// ---------------------------------------------------------------------------
// Superior Finds
// ---------------------------------------------------------------------------
// Entered from the relay at 44260A after the superior roll failed. 1 makes the
// item superior.
auto __fastcall DecideSuperior(void* source, void* killer, std::uint32_t classId) noexcept -> std::int32_t {
    SuperiorRollsSeen.fetch_add(1, std::memory_order_relaxed);
    const SuperiorConfig& s = Settings.superior;
    if (!Ready || !Settings.enabled || !s.enabled || s.chance <= 0 || source == nullptr) return 0;
    void* const player = ResolvePlayer(killer);
    if (!HasStatPerk(player, true, s.stat)) return 0;
    SuperiorPerkRolls.fetch_add(1, std::memory_order_relaxed);
    if (s.chance < 100) {
        void* const seed = UnitSeed(source);
        if (seed == nullptr || RollRandom(seed, 100) >= static_cast<std::uint32_t>(s.chance)) return 0;
    }
    SuperiorUpgrades.fetch_add(1, std::memory_order_relaxed);
    Diagnostic("Archaeology: Superior Finds, class %u becomes superior for character %u.", classId, Read<std::uint32_t>(player, UnitId));
    return 1;
}

// ---------------------------------------------------------------------------
// Socketed Runes
// ---------------------------------------------------------------------------
void BuildRuneCodes() {
    RuneCodes.clear();
    for (const std::string& code : Settings.runes.codes) {
        if (code.empty() || code.size() > 4) {
            Problem("socketed_runes.item_codes: \"" + code + "\" is not an item code");
            continue;
        }
        RuneCodes.push_back(PackCode(code));
    }
    std::sort(RuneCodes.begin(), RuneCodes.end());
    RuneCodes.erase(std::unique(RuneCodes.begin(), RuneCodes.end()), RuneCodes.end());
}

auto IsRune(void* item) noexcept -> bool {
    const std::uint32_t code = ItemCodeOf(item);
    return code != 0 && std::binary_search(RuneCodes.begin(), RuneCodes.end(), code);
}

struct Holder {
    void*        item;
    std::int32_t loc;     // body location 1..12, 0 for an item elsewhere
    bool         counts;  // its stats reach the character
};

auto IsHolder(const std::vector<Holder>& holders, void* item) noexcept -> bool {
    for (const Holder& h : holders) {
        if (h.item == item) return true;
    }
    return false;
}

auto PoolFor(const std::vector<Holder>& holders, std::uint32_t h, std::int32_t stat) noexcept -> std::uint32_t {
    return holders[h].counts && (MaskOf(stat) & MaskPerItem) == 0 ? GlobalPool : h;
}

void ProcessRunes(BonusSide& side, void* game, void* player, std::uint64_t rows) noexcept {
    if (!HasRunePerk(player)) {
        if (game == nullptr) PooledRunes.store(0, std::memory_order_relaxed);
        return;  // the sweep takes away what the hosts still carry
    }
    void* const inventory = GetInventory(player, "archaeology", 0);
    if (inventory == nullptr) return;
    thread_local std::vector<Holder> holders;
    holders.clear();
    for (std::int32_t loc = FirstBodyLoc; loc <= LastBodyLoc; ++loc) {
        void* const item = BodyLocItem(inventory, loc);
        if (item != nullptr && UnitType(item) == UnitItem) holders.push_back({ item, loc, MergedInto(item) == player });
    }
    // Every other item whose stats reach the character.
    void* item = FirstItem(inventory);
    for (std::size_t guard = 0; item != nullptr && guard < MaxInventoryItems; ++guard) {
        if (UnitType(item) != UnitItem) break;
        void* const next = NextItem(item);
        if (!IsHolder(holders, item) && MergedInto(item) == player) holders.push_back({ item, 0, true });
        item = next;
    }

    thread_local std::vector<std::vector<StatValue>> wanted;
    thread_local std::vector<PoolTerm>               terms;
    thread_local std::vector<StatValue>              plugin;
    thread_local std::vector<StatKey>                pairs;
    wanted.resize(holders.size());
    for (std::vector<StatValue>& w : wanted) w.clear();
    terms.clear();
    std::uint32_t pooled = 0;
    const std::int64_t percent = Settings.runes.percent;
    for (std::uint32_t h = 0; h < holders.size(); ++h) {
        void* const host = holders[h].item;
        void* const sockets = GetInventory(host, "archaeology", 1);
        void* child = sockets != nullptr ? FirstItem(sockets) : nullptr;
        for (std::size_t guard = 0; child != nullptr && guard < MaxSocketedItems; ++guard) {
            if (UnitType(child) != UnitItem) break;
            void* const next = NextItem(child);
            if (MergedInto(child) == host && IsRune(child)) {
                PluginListValues(child, plugin);
                ItemPairs(child, MaskRunes, pairs);
                for (const StatKey& key : pairs) {
                    if (static_cast<std::uint64_t>(key.stat) >= rows) continue;
                    const std::int64_t own = OwnValue(child, key, plugin);
                    if (own != 0) terms.push_back({ PoolFor(holders, h, key.stat), key.stat, key.layer, h, own * percent });
                }
                if (holders[h].counts) ++pooled;
            }
            child = next;
        }
    }
    Apportion(terms, wanted);
    for (std::uint32_t h = 0; h < holders.size(); ++h) Reconcile(side, game, holders[h].item, holders[h].loc, wanted[h]);
    if (game == nullptr) PooledRunes.store(pooled, std::memory_order_relaxed);
}

// ---------------------------------------------------------------------------
// One character
// ---------------------------------------------------------------------------
// Returns the containers behind the character's bonus (host: for publishing).
auto ProcessCharacter(BonusSide& side, void* game, void* player) noexcept -> std::int32_t {
    const std::uint64_t rows = StatRows(player);
    ProcessRunes(side, game, player, rows);
    const std::uint32_t id = Read<std::uint32_t>(player, UnitId);
    std::int32_t count = 0;
    if (game != nullptr) {
        const ContainerConfig& c = Settings.containers;
        if (ContainerHooked && c.enabled && c.percent != 0 && HasStatPerk(player, true, c.stat)) count = ContainerCount(game, id);
        else ForgetContainers(game, id);
    } else {
        count = Settings.containers.sheet ? PublishedFor(id) : 0;
        LocalContainers.store(count, std::memory_order_relaxed);
    }
    thread_local std::vector<StatValue> values;
    ContainerValues(count, rows, values);
    // Mass Dig: the perk holds the skill as an oskill on the same list.
    const MassDigConfig& dig = Settings.massDig;
    if (dig.enabled && dig.skill > 0 && static_cast<std::uint64_t>(StatNonClassSkill) < rows && HasStatPerk(player, true, dig.stat)) {
        AddValue(values, StatNonClassSkill, dig.skill, dig.level);
    }
    ReconcilePlayer(side, game, player, count, values);
    return count;
}

// Every entry no character claimed this pass loses its list.
void Sweep(BonusSide& side, bool host) noexcept {
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
    for (std::size_t i = 0; i < side.players.size();) {
        PlayerEntry& entry = side.players[i];
        if (entry.seen) {
            entry.seen = false;
            ++i;
            continue;
        }
        void* const player = host ? ServerUnit(entry.game, UnitPlayer, entry.id) : ClientUnit(entry.id, UnitPlayer);
        if (player != nullptr && UnitType(player) == UnitPlayer && OwnsList(player, entry.list, entry.values)) {
            RemoveList(player, entry.list);
        }
        side.players.erase(side.players.begin() + static_cast<std::ptrdiff_t>(i));
    }
}

void StartPass(BonusSide& side) noexcept {
    const std::uint64_t generation = Generation.load();
    if (side.generation != generation) {
        // A game was left or joined: the units behind the old entries are gone.
        side.entries.clear();
        side.players.clear();
        side.generation = generation;
    }
}

constexpr std::size_t MaxPlayers = 16;

// ---------------------------------------------------------------------------
// The artefact cooldown sweep (client)
// ---------------------------------------------------------------------------
// Drawn the way Alchemy draws the Elixir of Reincarnation's: after the game
// draws an item icon on the HD path, the sweep sprite goes on top at the
// icon's corner, scaled from the sweep's frame width to the icon's. Frame 0
// is the full sweep. The time left comes from the host pass, so it shows
// where the game's server runs in this process (single player, the host).
struct GameSprite {
    void* hd{};
    void* legacy{};
};
struct SpriteDrawParams {
    float        tint[4]{ 1.0f, 1.0f, 1.0f, 0.0f };  // +0  RGB; +12 0 = the sprite's own colours, 1 = tinted
    float        fade{ 1.0f };                       // +16 opacity
    std::int32_t mode{};                             // +20
    std::int32_t frame{};                            // +24
    std::uint8_t rest[140]{};                        // +28: flags and source rectangle, all zero
};
static_assert(sizeof(SpriteDrawParams) == 168, "79DA50 reads up to +0xA4");
using SpriteLoadFn   = std::uint64_t (__fastcall*)(GameSprite* sprite, const char* name, std::uint8_t flags);
using SpriteDrawHdFn = std::uint64_t (__fastcall*)(void* hdSprite, const std::int32_t* position, float scale,
                                                   const SpriteDrawParams* params);
SpriteLoadFn      SpriteLoad{};
SpriteDrawHdFn    SpriteDrawHd{};
GameSprite        SweepSprite;
std::atomic<bool> SweepLoadQueued{ false };
std::atomic<bool> SweepLoadTried{ false };

// UI thread: the sprite loads like an ImageWidget's, outside any draw pass.
// 858990 puts data/hd/global/ui/ in front of the name; the name is tried as
// written and then in lower case.
void __cdecl LoadSweepSprite(const D2RL::PluginContext*, void*) noexcept {
    if (SweepLoadTried.exchange(true)) return;
    std::string name = Settings.scrolls.sweepSprite;
    SpriteLoad(&SweepSprite, name.c_str(), 0);
    if (SweepSprite.hd == nullptr) {
        for (char& c : name) {
            if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
        }
        if (name != Settings.scrolls.sweepSprite) SpriteLoad(&SweepSprite, name.c_str(), 0);
    }
    if (SweepSprite.hd == nullptr) {
        D2RL::LogWarnF(Context, "Archaeology: the artefact sweep sprite data/hd/global/ui/%s did not load (legacy "
            "graphics, or no such sprite); no sweep is drawn.", Settings.scrolls.sweepSprite.c_str());
        return;
    }
    D2RL::LogInfoF(Context, "Archaeology: artefact sweep sprite %s loaded (%d frames, %d px wide).", name.c_str(),
        Read<std::int32_t>(SweepSprite.hd, HdSpriteFrames), Read<std::int32_t>(SweepSprite.hd, HdSpriteWidth));
}

// The frames left of the local character's cooldown for an entry, as the host
// pass last published them, counted down since at 25 a second for at most one
// second (the pass runs several times a second; a paused game stops it).
auto PublishedCooldownFor(std::uint32_t player, std::uint32_t scroll, std::int32_t& total) noexcept -> std::int32_t {
    std::lock_guard lock(CooldownMutex);
    if (PublishedCooldownsGeneration != Generation.load()) return 0;
    for (const PublishedCooldown& p : PublishedCooldowns) {
        if (p.player != player || p.scroll != scroll) continue;
        const std::uint64_t elapsed = std::min<std::uint64_t>(GetTickCount64() - PublishedCooldownsMs, 1000);
        total = p.total;
        return p.left - static_cast<std::int32_t>(elapsed * FramesPerSecond / 1000);
    }
    return 0;
}

// Entered from the relay at 15BCED right after the game drew an item icon on
// the HD path: the item, its corner as x | y << 32, its scale, its sprite.
void __fastcall ArtefactSweep(void* item, std::uint64_t packedXY, float scale, const GameSprite* icon) noexcept {
    if (!Ready || !Settings.enabled || !Settings.scrolls.enabled || !Settings.scrolls.sweep || item == nullptr
            || Scrolls.empty() || UnitType(item) != UnitItem) {
        return;
    }
    const std::int32_t index = ScrollIndexOf(item);
    if (index < 0) return;
    void* const player = PlayerFromIndex(LocalPlayerIndex());
    if (player == nullptr || UnitType(player) != UnitPlayer) return;
    std::int32_t total = 0;
    const std::int32_t left = PublishedCooldownFor(Read<std::uint32_t>(player, UnitId), static_cast<std::uint32_t>(index), total);
    if (left <= 0 || total <= 0) return;
    if (SweepSprite.hd == nullptr) {
        if (!SweepLoadTried.load() && ThreadSvc != nullptr && !SweepLoadQueued.exchange(true)) {
            if (ThreadSvc->runOnUiThread(Context, &LoadSweepSprite, nullptr) != D2RL::Threads::Result::Success) {
                SweepLoadQueued.store(false);
            }
        }
        return;
    }
    if (icon == nullptr || icon->hd == nullptr) return;
    const std::int32_t frames    = Read<std::int32_t>(SweepSprite.hd, HdSpriteFrames);
    const std::int32_t sweepWide = Read<std::int32_t>(SweepSprite.hd, HdSpriteWidth);
    const std::int32_t iconWide  = Read<std::int32_t>(icon->hd, HdSpriteWidth);
    if (frames <= 0 || sweepWide <= 0 || iconWide <= 0) return;
    const std::int32_t remaining = std::min(left, total);
    // Frame 0 is the full sweep; the last frame is nearly gone.
    std::int32_t frame = static_cast<std::int32_t>(static_cast<std::int64_t>(total - remaining) * frames / total);
    frame = std::clamp(frame, 0, frames - 1);
    SpriteDrawParams draw{};
    draw.tint[0] = static_cast<float>(Settings.scrolls.sweepColor[0]) / 255.0f;
    draw.tint[1] = static_cast<float>(Settings.scrolls.sweepColor[1]) / 255.0f;
    draw.tint[2] = static_cast<float>(Settings.scrolls.sweepColor[2]) / 255.0f;
    draw.tint[3] = 1.0f;   // colour applied, as Crossbow Charges and Alchemy write it
    draw.fade    = static_cast<float>(Settings.scrolls.sweepOpacity) / 100.0f;
    draw.frame   = frame;
    const std::int32_t position[2]{
        static_cast<std::int32_t>(static_cast<std::uint32_t>(packedXY & 0xFFFFFFFFULL)),
        static_cast<std::int32_t>(static_cast<std::uint32_t>(packedXY >> 32)),
    };
    SpriteDrawHd(SweepSprite.hd, position, scale * static_cast<float>(iconWide) / static_cast<float>(sweepWide), &draw);
    ArtefactSweepDraws.fetch_add(1, std::memory_order_relaxed);
}

void HostPass() noexcept {
    std::lock_guard lock(ServerBonus.mutex);
    StartPass(ServerBonus);
    thread_local std::vector<PublishedCount> counts;
    thread_local std::vector<PublishedCooldown> cooldowns;
    counts.clear();
    cooldowns.clear();
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
                const std::int32_t count = ProcessCharacter(ServerBonus, game, player);
                if (count > 0) counts.push_back({ Read<std::uint32_t>(player, UnitId), count });
                CollectCooldowns(game, Read<std::uint32_t>(player, UnitId), cooldowns);
            }
        }
    }
    Sweep(ServerBonus, true);
    {
        std::lock_guard track(TrackMutex);
        Published = counts;
        PublishedGeneration = Generation.load();
    }
    {
        std::lock_guard cooldown(CooldownMutex);
        PublishedCooldowns = cooldowns;
        PublishedCooldownsGeneration = Generation.load();
        PublishedCooldownsMs = GetTickCount64();
    }
    HostPasses.fetch_add(1, std::memory_order_relaxed);
}

void ClientPass() noexcept {
    std::lock_guard lock(ClientBonus.mutex);
    StartPass(ClientBonus);
    void* const player = PlayerFromIndex(LocalPlayerIndex());
    if (player != nullptr && UnitType(player) == UnitPlayer) ProcessCharacter(ClientBonus, nullptr, player);
    else LocalContainers.store(0, std::memory_order_relaxed);
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
            D2RL::LogErrorF(Context, "Archaeology: %s. Nothing was installed.", text);
            return false;
        }
    }
    const std::array<Witness, 7> windows{ {
        W(StatListWindowRva, StatListWindow, "the item stat list creation in 0x43EF10"),
        W(TypeCheckBodyRva, TypeCheckBody, "the data context and tables lookups in the item type check"),
        W(RegistryResetRva, RegistryReset, "the server client registry reset"),
        W(ClientLookupRva, ClientLookup, "the server client lookup"),
        W(ClientPlayerRva, ClientPlayer, "the server client fields"),
        W(FrameWitnessRva, FrameWitness, "the stat list expiry event (the game frame counter)"),
        W(QualityOwnerCallRva, QualityOwnerCall, "the quality roll's minion owner lookup"),
    } };
    for (const Witness& witness : windows) {
        if (!Context->CheckExpectedBytes(witness.rva, witness.bytes, witness.size)) {
            char text[256];
            std::snprintf(text, sizeof(text), "%s at 0x%llX does not match D2RLoader 1.3.1 (another plugin may own it)",
                witness.name, static_cast<unsigned long long>(witness.rva));
            InstallProblem = text;
            D2RL::LogErrorF(Context, "Archaeology: %s. Nothing was installed.", text);
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
    ServerUnit       = At<ServerUnitFn>(ServerUnitRva);
    ClientUnit       = At<ClientUnitFn>(ClientUnitRva);
    LocalPlayerIndex = At<LocalIndexFn>(LocalPlayerIndexRva);
    PlayerFromIndex  = At<PlayerFromIndexFn>(PlayerFromIndexRva);
    UnitSeed         = At<UnitFn>(UnitSeedRva);
    OriginalFinishItem = At<FinishItemFn>(FinishItemRva);
    SetItemFlag      = At<ItemFlagFn>(ItemFlagRva);
    AnimMode         = At<UnitIntFn>(AnimModeRva);
    ItemPage         = At<UnitByteFn>(ItemPageRva);
    ItemQuality      = At<UnitIntFn>(ItemQualityRva);
    ItemVersion      = At<UnitWordFn>(ItemVersionRva);
    ItemFlags        = At<UnitFlagsFn>(ItemFlagsRva);
    MaxSockets       = At<UnitByteFn>(MaxSocketsRva);
    AddSockets       = At<AddSocketsFn>(AddSocketsRva);
    FreeUnit         = At<FreeUnitFn>(FreeUnitRva);
    TransactionCost  = At<TransactionFn>(TransactionRva);
    PlayerData       = At<UnitFn>(PlayerDataRva);
    ItemSeed         = At<UnitFn>(ItemSeedRva);
    ApplyEthereal    = At<UnitFn>(ApplyEtherealRva);
    HasDurability    = At<UnitIntFn>(HasDurabilityRva);
    SetUnitStat      = At<SetStatFn>(SetUnitStatRva);
    StatWrite        = At<SetStatFn>(StatWriteRva);
    CheckItemType    = At<CheckItemTypeFn>(CheckItemTypeRva);
    OriginalCubeGenerate = At<CubeGenerateFn>(CubeRva);
    SkillsRecord     = At<SkillsRecordFn>(SkillsRecordRva);
    CorpseUsable     = At<UnitIntFn>(CorpseUsableRva);
    HasState         = At<UnitStateFn>(HasStateRva);
    SetState         = At<SetStateFn>(SetStateRva);
    CorpseRefresh    = At<UnitFn>(CorpseRefreshRva);
    SkillCalc        = At<SkillCalcFn>(SkillCalcRva);
    FindDrop         = At<FindDropFn>(FindDropRva);
    AreaSearch       = At<AreaSearchFn>(AreaSearchRva);
    ServerDo         = At<ServerDoFn>(ServerDoRva);
    MinionOwner      = At<UnitFn>(MinionOwnerRva);
    RollRandom       = At<RollRandomFn>(RollRandomRva);
}

auto InstallTooltips() -> bool {
    if (!Context->CheckExpectedBytes(TooltipListBodyRva, TooltipListBody, static_cast<std::uint32_t>(sizeof(TooltipListBody)))) {
        TooltipProblem = "the tooltip's list finder at 0x2F5A00 does not match D2RLoader 1.3.1";
        return false;
    }
    TooltipList = At<TooltipListFn>(TooltipListBodyRva);
    return true;
}

auto InstallContainers() -> bool {
    ContainerHooked = Context->InstallInlineHook<ContainerDropFn>(ContainerDropRva, ContainerDropEntry,
        static_cast<std::uint32_t>(sizeof(ContainerDropEntry)), &HookContainerDrop, &OriginalContainerDrop);
    if (!ContainerHooked) {
        ContainerProblem = "the container drop hook at 0x5930B0 was refused (another plugin may hook it, or the bytes differ)";
        return false;
    }
    return true;
}

// Byte windows a perk needs before it installs: a window another plugin
// changed turns that perk off, never the others.
auto WindowsMatch(std::initializer_list<Witness> windows, std::string& problem) -> bool {
    for (const Witness& w : windows) {
        if (!Context->CheckExpectedBytes(w.rva, w.bytes, w.size)) {
            char text[256];
            std::snprintf(text, sizeof(text), "%s at 0x%llX does not match D2RLoader 1.3.1 (another plugin may own it)", w.name,
                static_cast<unsigned long long>(w.rva));
            problem = text;
            return false;
        }
    }
    return true;
}

auto InstallBuilder() -> bool {
    BuilderHooked = Context->InstallInlineHook<BuildFn>(BuilderRva, BuilderEntry, static_cast<std::uint32_t>(sizeof(BuilderEntry)),
        &HookBuildItem, &OriginalBuild);
    if (!BuilderHooked) BuilderProblem = "the item builder hook at 0x43CD50 was refused (another plugin may hook it)";
    return BuilderHooked;
}

auto InstallGenerateDrops() -> bool {
    GenerateDropsHooked = Context->InstallInlineHook<GenerateDropsFn>(GenerateDropsRva, GenerateDropsEntry,
        static_cast<std::uint32_t>(sizeof(GenerateDropsEntry)), &HookGenerateDrops, &OriginalGenerateDrops);
    return GenerateDropsHooked;
}

auto CanReach(std::uintptr_t from, std::uintptr_t to) noexcept -> bool {
    const std::int64_t delta = static_cast<std::int64_t>(to) - static_cast<std::int64_t>(from);
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

void WriteSlot(std::uint8_t* relay, std::size_t offset, std::uintptr_t value) noexcept {
    std::memcpy(relay + offset, &value, sizeof(value));
}

// One page in reach of the image holds the three relays.
auto BuildRelayPage() -> bool {
    RelayPage = AllocateNear(Base + SuperiorSiteRva, 0x1000);
    if (RelayPage == nullptr) {
        RelayProblem = "no memory within reach of the game for the relays";
        return false;
    }
    std::memcpy(RelayPage + SuperiorRelayOffset, SuperiorRelay, sizeof(SuperiorRelay));
    WriteSlot(RelayPage + SuperiorRelayOffset, SuperiorRelaySlotDecide, reinterpret_cast<std::uintptr_t>(&DecideSuperior));
    WriteSlot(RelayPage + SuperiorRelayOffset, SuperiorRelaySlotResume, Base + SuperiorResumeRva);
    WriteSlot(RelayPage + SuperiorRelayOffset, SuperiorRelaySlotHit, Base + SuperiorHitRva);
    std::memcpy(RelayPage + GoldRelayOffset, GoldRelay, sizeof(GoldRelay));
    WriteSlot(RelayPage + GoldRelayOffset, GoldRelaySlotAmount, reinterpret_cast<std::uintptr_t>(&GoldAmount));
    WriteSlot(RelayPage + GoldRelayOffset, GoldRelaySlotNegative, Base + GoldNegativeRva);
    WriteSlot(RelayPage + GoldRelayOffset, GoldRelaySlotPositive, Base + GoldPositiveRva);
    std::memcpy(RelayPage + MythicalRelayOffset, MythicalRelay, sizeof(MythicalRelay));
    WriteSlot(RelayPage + MythicalRelayOffset, MythicalRelaySlotRoll, reinterpret_cast<std::uintptr_t>(&MythicalRoll));
    WriteSlot(RelayPage + MythicalRelayOffset, MythicalRelaySlotResume, Base + MythicalResumeRva);
    WriteSlot(RelayPage + MythicalRelayOffset, MythicalRelaySlotHit, Base + UniqueHitRva);
    std::memcpy(RelayPage + RunewordRelayOffset, RunewordRelay, sizeof(RunewordRelay));
    WriteSlot(RelayPage + RunewordRelayOffset, RunewordRelaySlotAllow, reinterpret_cast<std::uintptr_t>(&AllowMagicRuneword));
    WriteSlot(RelayPage + RunewordRelayOffset, RunewordRelaySlotSwitch, Base + RunewordSwitchRva);
    WriteSlot(RelayPage + RunewordRelayOffset, RunewordRelaySlotGoOn, Base + RunewordGoOnRva);
    std::memcpy(RelayPage + CubeRelayOffset, IdentifyRelay, sizeof(IdentifyRelay));
    WriteSlot(RelayPage + CubeRelayOffset, IdentifyRelaySlot, reinterpret_cast<std::uintptr_t>(&HookCubeGenerate));
    std::memcpy(RelayPage + ScrollRelayOffset, ScrollRelay, sizeof(ScrollRelay));
    WriteSlot(RelayPage + ScrollRelayOffset, ScrollRelaySlot, reinterpret_cast<std::uintptr_t>(&UseDispatch));
    std::memcpy(RelayPage + SweepRelayOffset, SweepRelay, sizeof(SweepRelay));
    WriteSlot(RelayPage + SweepRelayOffset, SweepRelaySlotDraw, reinterpret_cast<std::uintptr_t>(&ArtefactSweep));
    WriteSlot(RelayPage + SweepRelayOffset, SweepRelaySlotIndex, Base + IconSlotIndexRva);
    std::memcpy(RelayPage + LocalDelayRelayOffset, LocalDelayRelay, sizeof(LocalDelayRelay));
    WriteSlot(RelayPage + LocalDelayRelayOffset, LocalDelayRelaySlot, reinterpret_cast<std::uintptr_t>(&LocalDelayGate));
    std::memcpy(RelayPage + UseRuleRelayOffset, UseRuleRelay, sizeof(UseRuleRelay));
    WriteSlot(RelayPage + UseRuleRelayOffset, UseRuleRelaySlot, reinterpret_cast<std::uintptr_t>(&ArtefactUseRule));
    std::memcpy(RelayPage + IdentifyRelayOffset, IdentifyRelay, sizeof(IdentifyRelay));
    WriteSlot(RelayPage + IdentifyRelayOffset, IdentifyRelaySlot, reinterpret_cast<std::uintptr_t>(&FinishBuiltItem));
    DWORD previous = 0;
    if (!VirtualProtect(RelayPage, 0x1000, PAGE_EXECUTE_READ, &previous)) {
        RelayProblem = "the relay page could not be made executable";
        RelayPage = nullptr;
        return false;
    }
    FlushInstructionCache(GetCurrentProcess(), RelayPage, 0x1000);
    return true;
}

// A jump (E9) or call (E8) from site to a relay, padded with NOPs.
template <std::size_t N>
auto BranchBytes(std::uint8_t opcode, std::uint64_t site, std::size_t relayOffset, std::array<std::uint8_t, N>& out) noexcept -> bool {
    if (RelayPage == nullptr) return false;
    const auto from = Base + site + 5;
    const auto to = reinterpret_cast<std::uintptr_t>(RelayPage + relayOffset);
    if (!CanReach(from, to)) return false;
    const auto rel = static_cast<std::int32_t>(static_cast<std::int64_t>(to) - static_cast<std::int64_t>(from));
    out.fill(0x90);
    out[0] = opcode;
    std::memcpy(out.data() + 1, &rel, 4);
    return true;
}

auto InstallCube() -> bool {
    if (!WindowsMatch({ W(CubePageWindowRva, CubePageWindow, "the transmute check's cube count"),
                        W(EtherealGateWindowRva, EtherealGateWindow, "the item roll's ethereal gate"),
                        W(EtherealApplyWindowRva, EtherealApplyWindow, "the item roll's ethereal application"),
                        W(ItemSeedWindowRva, ItemSeedWindow, "the item roll's ethereal seed") }, CubeProblem)) {
        return false;
    }
    if (!WindowsMatch({ W(CubeCallWindowRva, CubeCallWindow, "the transmute's call to the cube generator") }, CubeProblem)) return false;
    if (!BranchBytes(0xE8, CubeCallRva, CubeRelayOffset, CubeWritten)) {
        CubeProblem = RelayProblem.empty() ? "the relay is out of reach of the transmute" : RelayProblem;
        return false;
    }
    CubePatched = Context->PatchBytes(CubeCallRva, CubeCallStock, 5, CubeWritten.data(), 5);
    if (!CubePatched) CubeProblem = "the call at 0x4F27BD could not be redirected";
    return CubePatched;
}

auto InstallSuperior() -> bool {
    if (!Context->CheckExpectedBytes(SuperiorWindowRva, SuperiorWindow, static_cast<std::uint32_t>(sizeof(SuperiorWindow)))) {
        SuperiorProblem = "the superior and normal rolls at 0x4425DE do not match D2RLoader 1.3.1 (another plugin may own them)";
        return false;
    }
    if (!BranchBytes(0xE9, SuperiorSiteRva, SuperiorRelayOffset, SuperiorWritten)) {
        SuperiorProblem = RelayProblem.empty() ? "the relay is out of reach of the quality roll" : RelayProblem;
        return false;
    }
    SuperiorPatched = Context->PatchBytes(SuperiorSiteRva, SuperiorSiteStock, SuperiorSiteSize, SuperiorWritten.data(), SuperiorSiteSize);
    if (!SuperiorPatched) {
        SuperiorProblem = "the jump at 0x44260A could not be written";
        return false;
    }
    return true;
}

auto InstallGold() -> bool {
    if (!Context->CheckExpectedBytes(GoldWindowRva, GoldWindow, static_cast<std::uint32_t>(sizeof(GoldWindow)))) {
        GoldProblem = "the gold step at 0x441165 does not match D2RLoader 1.3.1 (another plugin may own it)";
        return false;
    }
    if (!BranchBytes(0xE9, GoldSiteRva, GoldRelayOffset, GoldWritten)) {
        GoldProblem = RelayProblem.empty() ? "the relay is out of reach of the gold step" : RelayProblem;
        return false;
    }
    GoldPatched = Context->PatchBytes(GoldSiteRva, GoldSiteStock, GoldSiteSize, GoldWritten.data(), GoldSiteSize);
    if (!GoldPatched) {
        GoldProblem = "the jump at 0x4411CD could not be written";
        return false;
    }
    return true;
}

auto InstallIdentify() -> bool {
    if (!Context->CheckExpectedBytes(BuilderWindowRva, BuilderWindow, static_cast<std::uint32_t>(sizeof(BuilderWindow)))
            || !Context->CheckExpectedBytes(ItemFlagRva, ItemFlagEntry, static_cast<std::uint32_t>(sizeof(ItemFlagEntry)))) {
        IdentifyProblem = "the item builder's end at 0x43D4DF or ITEMS_SetItemFlag do not match D2RLoader 1.3.1 (another plugin may own them)";
        return false;
    }
    if (!BranchBytes(0xE8, FinishCallRva, IdentifyRelayOffset, IdentifyWritten)) {
        IdentifyProblem = RelayProblem.empty() ? "the relay is out of reach of the item builder" : RelayProblem;
        return false;
    }
    IdentifyPatched = Context->PatchBytes(FinishCallRva, FinishCallStock, 5, IdentifyWritten.data(), 5);
    if (!IdentifyPatched) {
        IdentifyProblem = "the call at 0x43D4EA could not be redirected";
        return false;
    }
    return true;
}

auto InstallRunewords() -> bool {
    if (!WindowsMatch({ W(MatcherItemDataRva, MatcherItemData, "the runeword check's item data"),
                        W(RunewordGateRva, RunewordGate, "the runeword check's quality gate"),
                        W(SocketWindowRva, SocketWindow, "the item flags getter") }, RunewordProblem)) {
        return false;
    }
    SocketInsertHooked = Context->InstallInlineHook<SocketInsertFn>(SocketInsertRva, SocketInsertEntry,
        static_cast<std::uint32_t>(sizeof(SocketInsertEntry)), &HookSocketInsert, &OriginalSocketInsert);
    if (!SocketInsertHooked) {
        RunewordProblem = "the socket insertion hook at 0x46EB00 was refused (another plugin may hook it)";
        return false;
    }
    if (!BranchBytes(0xE9, RunewordGateRva, RunewordRelayOffset, RunewordWritten)) {
        RunewordProblem = RelayProblem.empty() ? "the relay is out of reach of the runeword check" : RelayProblem;
        return false;
    }
    static constexpr std::uint8_t Stock[]{ 0x8B, 0x00, 0x83, 0xE8, 0x04, 0x83, 0xF8, 0x05, 0x0F, 0x87, 0x6A, 0xFE, 0xFF, 0xFF };
    RunewordPatched = Context->PatchBytes(RunewordGateRva, Stock, RunewordSiteSize, RunewordWritten.data(), RunewordSiteSize);
    if (!RunewordPatched) RunewordProblem = "the jump at 0x37246C could not be written";
    return RunewordPatched;
}

auto InstallMassDig() -> bool {
    const std::int32_t slot = Settings.massDig.srvdofunc;
    if (slot < FirstFreeSrvDo || slot > LastFreeSrvDo) {
        MassDigProblem = "mass_dig.srvdofunc must be 172 to 200, the empty slots of D2R 3.3's table";
        return false;
    }
    if (!WindowsMatch({ W(SrvDoDispatchRva, SrvDoDispatch, "the server skill do-dispatch"),
                        W(FindTreasureWindowRva, FindTreasureWindow, "Find Item's server function"),
                        W(SuperiorWindowRva, SuperiorWindow, "the quality roll's seed and rand calls") }, MassDigProblem)) {
        return false;
    }
    if (!Context->CheckExpectedBytes(AreaSearchRva, AreaSearchEntry, static_cast<std::uint32_t>(sizeof(AreaSearchEntry)))
            && !EntryHooked(AreaSearchRva)) {
        MassDigProblem = "the area search at 0x4327D0 does not match D2RLoader 1.3.1";
        return false;
    }
    static constexpr std::uint8_t Empty[8]{};
    const auto handler = reinterpret_cast<std::uintptr_t>(&MassDigDo);
    std::uint8_t written[8];
    std::memcpy(written, &handler, sizeof(written));
    MassDigInstalled = Context->PatchBytes(SrvDoTableRva + static_cast<std::uint64_t>(slot) * 8, Empty, 8, written, 8);
    if (!MassDigInstalled) MassDigProblem = "srvdofunc slot is not empty (another plugin may use it)";
    return MassDigInstalled;
}

auto InstallScrolls() -> bool {
    if (!WindowsMatch({ W(UseDispatchWindowRva, UseDispatchWindow, "the item use dispatch"),
                        W(CastOnTargetBodyRva, CastOnTargetBody, "the item skill caster at a unit"),
                        W(CastAtPositionBodyRva, CastAtPositionBody, "the item skill caster at a spot"),
                        W(ItemEffectChecksRva, ItemEffectChecks, "the item skill checks in 0x589930") }, ScrollProblem)) {
        return false;
    }
    for (const Witness& w : { W(CastOnTargetRva, CastOnTargetEntry, "the item skill caster at a unit"),
                              W(CastAtPositionRva, CastAtPositionEntry, "the item skill caster at a spot"),
                              W(ItemRestrictRva, ItemRestrictEntry, "the item skill restriction check"),
                              W(TargetUnitRva, TargetUnitEntry, "the target unit lookup"),
                              W(TargetXRva, TargetXBody, "the target point x"),
                              W(TargetYRva, TargetYBody, "the target point y") }) {
        if (!Context->CheckExpectedBytes(w.rva, w.bytes, w.size) && !EntryHooked(w.rva)) {
            ScrollProblem = std::string(w.name) + " does not match D2RLoader 1.3.1";
            return false;
        }
    }
    CastOnTarget   = At<CastOnTargetFn>(CastOnTargetRva);
    CastAtPosition = At<CastAtPositionFn>(CastAtPositionRva);
    TargetUnit     = At<TargetUnitFn>(TargetUnitRva);
    TargetX        = At<TargetCoordFn>(TargetXRva);
    TargetY        = At<TargetCoordFn>(TargetYRva);
    ItemRestrict   = At<ItemRestrictFn>(ItemRestrictRva);
    if (!Context->CheckExpectedBytes(ServerDoRva, ServerDoEntry, static_cast<std::uint32_t>(sizeof(ServerDoEntry)))
            && !EntryHooked(ServerDoRva)) {
        ScrollProblem = "the server skill handler at 0x43ACB0 does not match D2RLoader 1.3.1";
        return false;
    }
    std::array<std::uint8_t, 5> call{};
    if (!BranchBytes(0xE8, UseCallRva, ScrollRelayOffset, call)) {
        ScrollProblem = RelayProblem.empty() ? "the relay is out of reach of the item use" : RelayProblem;
        return false;
    }
    std::copy(call.begin(), call.end(), ScrollWritten.begin());
    ScrollWritten[5] = 0x90;
    ScrollWritten[6] = 0x90;
    ScrollPatched = Context->PatchBytes(UseCallRva, UseCallStock, 7, ScrollWritten.data(), 7);
    if (!ScrollPatched) ScrollProblem = "the call at 0x581789 could not be redirected";
    return ScrollPatched;
}

// Artefact use rule and cooldowns: the use check's pSpell rule call goes to
// ArtefactUseRule, and the skill handler's localdelay call skips the
// artefact's own cast.
auto InstallCooldowns() -> bool {
    if (!WindowsMatch({ W(UseRuleWindowRva, UseRuleWindow, "the item use check"),
                        W(LocalDelayWindowRva, LocalDelayWindow, "the server skill handler's localdelay call") }, CooldownProblem)) {
        return false;
    }
    if ((!Context->CheckExpectedBytes(UnitRoomRva, UnitRoomEntry, static_cast<std::uint32_t>(sizeof(UnitRoomEntry)))
                && !EntryHooked(UnitRoomRva))
            || (!Context->CheckExpectedBytes(RoomInTownRva, RoomInTownEntry, static_cast<std::uint32_t>(sizeof(RoomInTownEntry)))
                && !EntryHooked(RoomInTownRva))) {
        CooldownProblem = "UNITS_GetRoom or DUNGEON_IsRoomInTown does not match D2RLoader 1.3.1";
        return false;
    }
    std::array<std::uint8_t, 5> rule{};
    std::array<std::uint8_t, 5> call{};
    if (!BranchBytes(0xE8, UseRuleSiteRva, UseRuleRelayOffset, rule) || !BranchBytes(0xE8, LocalDelayCallRva, LocalDelayRelayOffset, call)) {
        CooldownProblem = RelayProblem.empty() ? "the relay is out of reach of the use check or the skill handler" : RelayProblem;
        return false;
    }
    UnitRoom      = At<UnitRoomFn>(UnitRoomRva);
    RoomInTown    = At<RoomInTownFn>(RoomInTownRva);
    LocalDelaySet = At<LocalDelaySetFn>(LocalDelaySetRva);
    UseRuleWritten.fill(0x90);
    std::copy(rule.begin(), rule.end(), UseRuleWritten.begin());
    UseRulePatched = Context->PatchBytes(UseRuleSiteRva, UseRuleStock, 10, UseRuleWritten.data(), 10);
    if (!UseRulePatched) {
        CooldownProblem = "the pSpell rule call at 0x58195B could not be redirected";
        return false;
    }
    std::copy(call.begin(), call.end(), LocalDelayWritten.begin());
    LocalDelayPatched = Context->PatchBytes(LocalDelayCallRva, LocalDelayStock, 5, LocalDelayWritten.data(), 5);
    if (!LocalDelayPatched) {
        D2RL::LogWarnF(Context, "Archaeology: the call at 0x43B1BF could not be redirected, so an artefact also puts its skill "
            "on the character's own cooldown. The artefact cooldowns still work.");
    }
    return true;
}

auto InstallSweep() -> bool {
    if (!WindowsMatch({ W(IconDrawnWindowRva, IconDrawnWindow, "the item icon renderer's HD draw") }, SweepProblem)) return false;
    if ((!Context->CheckExpectedBytes(SpriteLoadRva, SpriteLoadBytes, static_cast<std::uint32_t>(sizeof(SpriteLoadBytes)))
                && !EntryHooked(SpriteLoadRva))
            || (!Context->CheckExpectedBytes(SpriteDrawHdRva, SpriteDrawHdBytes, static_cast<std::uint32_t>(sizeof(SpriteDrawHdBytes)))
                && !EntryHooked(SpriteDrawHdRva))) {
        SweepProblem = "the sprite loader or the HD sprite draw does not match D2RLoader 1.3.1";
        return false;
    }
    if (ThreadSvc == nullptr) {
        SweepProblem = "no thread service to load the sprite on";
        return false;
    }
    SpriteLoad   = At<SpriteLoadFn>(SpriteLoadRva);
    SpriteDrawHd = At<SpriteDrawHdFn>(SpriteDrawHdRva);
    std::array<std::uint8_t, 5> call{};
    if (!BranchBytes(0xE8, IconDrawnSiteRva, SweepRelayOffset, call)) {
        SweepProblem = RelayProblem.empty() ? "the relay is out of reach of the item icon renderer" : RelayProblem;
        return false;
    }
    std::copy(call.begin(), call.end(), SweepWritten.begin());
    SweepWritten[5] = 0x90;
    SweepPatched = Context->PatchBytes(IconDrawnSiteRva, IconDrawnStock, 6, SweepWritten.data(), 6);
    if (!SweepPatched) SweepProblem = "the instruction at 0x15BCED could not be redirected";
    return SweepPatched;
}

auto InstallMythical() -> bool {
    if (!WindowsMatch({ W(MythicalWindowRva, MythicalWindow, "the set roll after the unique roll"),
                        W(SuperiorWindowRva, SuperiorWindow, "the quality roll's returns") }, MythicalProblem)) {
        return false;
    }
    if (!BranchBytes(0xE9, MythicalSiteRva, MythicalRelayOffset, MythicalWritten)) {
        MythicalProblem = RelayProblem.empty() ? "the relay is out of reach of the quality roll" : RelayProblem;
        return false;
    }
    MythicalPatched = Context->PatchBytes(MythicalSiteRva, MythicalSiteStock, MythicalSiteSize, MythicalWritten.data(), MythicalSiteSize);
    if (!MythicalPatched) MythicalProblem = "the jump at 0x442453 could not be written";
    return MythicalPatched;
}

void Restore() noexcept {
    if (CubePatched && Context->PatchBytes(CubeCallRva, CubeWritten.data(), 5, CubeCallStock, 5)) CubePatched = false;

    if (ScrollPatched && Context->PatchBytes(UseCallRva, ScrollWritten.data(), 7, UseCallStock, 7)) ScrollPatched = false;
    if (SweepPatched && Context->PatchBytes(IconDrawnSiteRva, SweepWritten.data(), 6, IconDrawnStock, 6)) SweepPatched = false;
    if (UseRulePatched && Context->PatchBytes(UseRuleSiteRva, UseRuleWritten.data(), 10, UseRuleStock, 10)) UseRulePatched = false;
    if (LocalDelayPatched && Context->PatchBytes(LocalDelayCallRva, LocalDelayWritten.data(), 5, LocalDelayStock, 5)) {
        LocalDelayPatched = false;
    }
    if (MassDigInstalled) {
        static constexpr std::uint8_t Empty[8]{};
        const auto handler = reinterpret_cast<std::uintptr_t>(&MassDigDo);
        std::uint8_t written[8];
        std::memcpy(written, &handler, sizeof(written));
        if (Context->PatchBytes(SrvDoTableRva + static_cast<std::uint64_t>(Settings.massDig.srvdofunc) * 8, written, 8, Empty, 8)) {
            MassDigInstalled = false;
        }
    }
    static constexpr std::uint8_t RunewordStock[]{ 0x8B, 0x00, 0x83, 0xE8, 0x04, 0x83, 0xF8, 0x05, 0x0F, 0x87, 0x6A, 0xFE, 0xFF, 0xFF };
    if (RunewordPatched && Context->PatchBytes(RunewordGateRva, RunewordWritten.data(), RunewordSiteSize, RunewordStock, RunewordSiteSize)) {
        RunewordPatched = false;
    }
    if (MythicalPatched && Context->PatchBytes(MythicalSiteRva, MythicalWritten.data(), MythicalSiteSize, MythicalSiteStock, MythicalSiteSize)) {
        MythicalPatched = false;
    }
    if (IdentifyPatched && Context->PatchBytes(FinishCallRva, IdentifyWritten.data(), 5, FinishCallStock, 5)) IdentifyPatched = false;
    if (GoldPatched && Context->PatchBytes(GoldSiteRva, GoldWritten.data(), GoldSiteSize, GoldSiteStock, GoldSiteSize)) GoldPatched = false;
    if (SuperiorPatched && Context->PatchBytes(SuperiorSiteRva, SuperiorWritten.data(), SuperiorSiteSize, SuperiorSiteStock, SuperiorSiteSize)) {
        SuperiorPatched = false;
    }
}

auto Install() -> bool {
    if (!Verify()) return false;
    BindNatives();
    BuildMasks();
    BuildRuneCodes();
    if (HMODULE core = GetModuleHandleW(L"D2RCore.dll"); core != nullptr) {
        const auto* visit = reinterpret_cast<const std::uint8_t*>(GetProcAddress(core, "VisitWideUnitStats"));
        FullStatsLayout = visit != nullptr && std::memcmp(visit, VisitWideUnitStatsBytes, sizeof(VisitWideUnitStatsBytes)) == 0;
    }
    if (!FullStatsLayout) {
        InstallProblem = "D2RCore!VisitWideUnitStats does not match D2RCore 1.3.1, so the stat layout is unknown";
        Context->LogError("Archaeology: D2RCore!VisitWideUnitStats does not match D2RCore 1.3.1; nothing was installed.");
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
        Context->LogError("Archaeology: D2RLoader's thread or lifecycle service is unavailable; nothing was installed.");
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
            Context->LogError("Archaeology: a game join/leave listener was refused; nothing was installed.");
            return false;
        }
    }
    if (Settings.runes.enabled && Settings.runes.tooltips && !InstallTooltips()) {
        D2RL::LogWarnF(Context, "Archaeology: rune bonuses will not show in tooltips: %s. They still count.", TooltipProblem.c_str());
    }
    // Who a drop belongs to: Identified Drops, Sockets, Sell Value, Coupon
    // Upgrade and Crystal Upgrade read it; containers also count for perk 1.
    const bool credit = Settings.identify.enabled || Settings.sockets.enabled || Settings.sell.enabled
        || Settings.couponUpgrade.enabled || Settings.crystal.enabled;
    if ((Settings.containers.enabled || credit) && !InstallContainers()) {
        D2RL::LogWarnF(Context, "Archaeology: %s; Containers Opened is off and container drops get no drop perks. The other "
            "perks still work.", ContainerProblem.c_str());
    }
    if (credit && !InstallGenerateDrops()) {
        D2RL::LogWarnF(Context, "Archaeology: the monster drop hook at 0x441300 was refused (another plugin may hook it); "
            "monster drops get no drop perks.");
    }
    const bool finish = Settings.identify.enabled || Settings.sockets.enabled || Settings.couponEthereal.enabled;
    BuildScrolls();
    const bool totalGold = Settings.totalGold.enabled && Settings.totalGold.stat >= 0;
    if ((Settings.superior.enabled || Settings.gold.enabled || totalGold || Settings.sell.enabled || finish || Settings.mythical.enabled
            || Settings.runewords.enabled || Settings.scrolls.enabled) && !BuildRelayPage()) {
        D2RL::LogWarnF(Context, "Archaeology: %s; the perks that need a relay are off.", RelayProblem.c_str());
    }
    if (Settings.sockets.enabled) {
        SocketsReady = WindowsMatch({ W(SocketWindowRva, SocketWindow, "the builder's socket step"),
                                      W(ItemSeedWindowRva, ItemSeedWindow, "the item roll's ethereal seed") }, SocketProblem);
    }
    if (Settings.sell.enabled) {
        SellReady = WindowsMatch({ W(SellWindowRva, SellWindow, "the vendor sell price call"),
                                   W(FreeUnitWindowRva, FreeUnitWindow, "the builder's unit free"),
                                   W(EtherealGateWindowRva, EtherealGateWindow, "the item quality getter") }, SellProblem);
    }
    if ((Settings.sell.enabled || Settings.couponUpgrade.enabled || Settings.crystal.enabled) && !InstallBuilder()) {
        D2RL::LogWarnF(Context, "Archaeology: %s; Sell Value, Coupon Upgrade and Crystal Upgrade are off.", BuilderProblem.c_str());
    }
    if (Settings.couponEthereal.enabled) {
        EtherealReady = WindowsMatch({ W(EtherealGateWindowRva, EtherealGateWindow, "the item roll's ethereal gate"),
                                       W(EtherealApplyWindowRva, EtherealApplyWindow, "the item roll's ethereal application"),
                                       W(SocketWindowRva, SocketWindow, "the item flags getter") }, CubeProblem);
        if (EtherealReady && !InstallCube()) EtherealReady = false;
        if (!EtherealReady) D2RL::LogWarnF(Context, "Archaeology: Coupon Ethereal is off: %s.", CubeProblem.c_str());
    }
    if (Settings.massDig.enabled && !InstallMassDig()) {
        D2RL::LogWarnF(Context, "Archaeology: Mass Dig is off: %s. The other perks still work.", MassDigProblem.c_str());
    }
    if (Settings.scrolls.enabled && !InstallScrolls()) {
        D2RL::LogWarnF(Context, "Archaeology: Skill Scrolls is off: %s. The other perks still work.", ScrollProblem.c_str());
    }
    if (Settings.scrolls.enabled && ScrollPatched) {
        if (!InstallCooldowns()) {
            D2RL::LogWarnF(Context, "Archaeology: the artefact use rule and cooldowns are off: %s. The scrolls still work, with "
                "their pSpell's own use rule and no cooldown.", CooldownProblem.c_str());
        }
        if (Settings.scrolls.sweep && !InstallSweep()) {
            D2RL::LogWarnF(Context, "Archaeology: the artefact cooldown sweep is off: %s. The cooldowns still work.", SweepProblem.c_str());
        }
    }
    if (Settings.runewords.enabled && !InstallRunewords()) {
        D2RL::LogWarnF(Context, "Archaeology: Magic Runewords is off: %s. The other perks still work.", RunewordProblem.c_str());
    }
    if (Settings.mythical.enabled && !InstallMythical()) {
        D2RL::LogWarnF(Context, "Archaeology: Mythical Uniques is off: %s. The other perks still work.", MythicalProblem.c_str());
    }
    if (Settings.superior.enabled && !InstallSuperior()) {
        D2RL::LogWarnF(Context, "Archaeology: Superior Finds is off: %s. The other perks still work.", SuperiorProblem.c_str());
    }
    // Sell Value needs the gold step too: it sets its pile to exactly the price there.
    if ((Settings.gold.enabled || totalGold || Settings.sell.enabled) && !InstallGold()) {
        D2RL::LogWarnF(Context, "Archaeology: Gold Find and Total Gold Found are off: %s. The other perks still work.",
            GoldProblem.c_str());
    }
    if (finish && !InstallIdentify()) {
        D2RL::LogWarnF(Context, "Archaeology: Identified Drops, Sockets and Coupon Ethereal are off: %s.", IdentifyProblem.c_str());
    }
    Ready = true;
    StartChains(Context);
    return true;
}

// ---------------------------------------------------------------------------
// Console command
// ---------------------------------------------------------------------------
constexpr std::array<const char*, 13> LocNames{ "elsewhere", "head", "amulet", "body armor", "right hand", "left hand",
    "right ring", "left ring", "belt", "boots", "gloves", "swap right hand", "swap left hand" };

auto CodeText(std::uint32_t code) -> std::string {
    char text[5]{};
    std::memcpy(text, &code, 4);
    for (char& c : text) {
        if (c == ' ') c = '\0';
    }
    return text[0] != '\0' ? std::string(text) : std::string("?");
}

auto StateText(bool enabled, bool installed, const std::string& problem) -> std::string {
    if (!enabled) return "off";
    if (installed) return "on";
    return problem.empty() ? std::string("not installed") : "off: " + problem;
}

void WriteItems(const D2RL::PluginContext* plugin, const char* sideName, BonusSide& side) {
    std::vector<BonusEntry>  items;
    std::vector<PlayerEntry> players;
    {
        std::lock_guard lock(side.mutex);
        items = side.entries;
        players = side.players;
    }
    char text[1024];
    for (const PlayerEntry& p : players) {
        std::snprintf(text, sizeof(text), "%s: character %u, %d containers in the window:%s", sideName, p.id, p.count,
            Describe(p.values).c_str());
        plugin->WriteConsoleMessage(text);
    }
    std::snprintf(text, sizeof(text), "%s: %zu items carry a socketed rune bonus", sideName, items.size());
    plugin->WriteConsoleMessage(text);
    std::size_t lines = 0;
    for (const BonusEntry& entry : items) {
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
    std::snprintf(text, sizeof(text), "Archaeology %s: %s%s%s", PluginVersion, Settings.enabled ? "on" : "off",
        InstallProblem.empty() ? (Ready ? ", installed" : "") : ", ", InstallProblem.c_str());
    plugin->WriteConsoleMessage(text);
    const ContainerConfig& c = Settings.containers;
    std::snprintf(text, sizeof(text), "Containers Opened: %s, stat %d, +%d per container to %zu stats for %d s | containers counted "
        "%llu (looted objects seen %llu) | your character: %d containers in the window%s",
        StateText(c.enabled, ContainerHooked, ContainerProblem).c_str(), c.stat, c.percent, c.stats.size(), c.seconds,
        static_cast<unsigned long long>(ContainersCounted.load()), static_cast<unsigned long long>(ContainerDrops.load()),
        LocalContainers.load(), c.sheet ? "" : " (character sheet off)");
    plugin->WriteConsoleMessage(text);
    const SuperiorConfig& s = Settings.superior;
    std::snprintf(text, sizeof(text), "Superior Finds: %s, stat %d, %d%% | quality rolls past the superior roll %llu, for perk "
        "players %llu, made superior %llu", StateText(s.enabled, SuperiorPatched, SuperiorProblem).c_str(), s.stat, s.chance,
        static_cast<unsigned long long>(SuperiorRollsSeen.load()), static_cast<unsigned long long>(SuperiorPerkRolls.load()),
        static_cast<unsigned long long>(SuperiorUpgrades.load()));
    plugin->WriteConsoleMessage(text);
    const GoldConfig& g = Settings.gold;
    std::snprintf(text, sizeof(text), "Gold Find: %s, stat %d, +%d%% | piles changed %llu, gold added %lld (with Total Gold Found)",
        StateText(g.enabled, GoldPatched, GoldProblem).c_str(), g.stat, g.percent,
        static_cast<unsigned long long>(GoldPilesRaised.load()), static_cast<long long>(GoldAdded.load()));
    plugin->WriteConsoleMessage(text);
    const TotalGoldConfig& t = Settings.totalGold;
    std::snprintf(text, sizeof(text), "Total Gold Found: %s, stat %d | piles it changed %llu, last value %+d%%",
        t.enabled && t.stat < 0 ? "off (stat_id not set)" : StateText(t.enabled, GoldPatched, GoldProblem).c_str(), t.stat,
        static_cast<unsigned long long>(TotalGoldPiles.load()), TotalGoldLast.load());
    plugin->WriteConsoleMessage(text);
    const IdentifyConfig& d = Settings.identify;
    std::snprintf(text, sizeof(text), "Identified Drops: %s, stat %d | monster drops %s, containers %s | items identified %llu",
        StateText(d.enabled, IdentifyPatched, IdentifyProblem).c_str(), d.stat, GenerateDropsHooked ? "hooked" : "not hooked",
        ContainerHooked ? "hooked" : "not hooked", static_cast<unsigned long long>(ItemsIdentified.load()));
    plugin->WriteConsoleMessage(text);
    std::snprintf(text, sizeof(text), "Sockets: %s, stat %d, %d%% | sockets added %llu, of them socketed twins (quivers) %llu",
        StateText(Settings.sockets.enabled, SocketsReady && IdentifyPatched, SocketProblem.empty() ? IdentifyProblem : SocketProblem).c_str(),
        Settings.sockets.stat, Settings.sockets.chance, static_cast<unsigned long long>(SocketsAdded.load()),
        static_cast<unsigned long long>(SocketTwinsDropped.load()));
    plugin->WriteConsoleMessage(text);
    std::snprintf(text, sizeof(text), "Sell Value: %s, stat %d, vendor %d | items turned to gold %llu, gold %llu",
        StateText(Settings.sell.enabled, SellReady && BuilderHooked, SellProblem.empty() ? BuilderProblem : SellProblem).c_str(),
        Settings.sell.stat, Settings.sell.vendor, static_cast<unsigned long long>(ItemsSold.load()),
        static_cast<unsigned long long>(GoldFromSales.load()));
    plugin->WriteConsoleMessage(text);
    std::snprintf(text, sizeof(text), "Coupon Ethereal: %s, stat %d, %d%% | coupon outputs that could turn %llu, turned %llu",
        StateText(Settings.couponEthereal.enabled, EtherealReady && IdentifyPatched, CubeProblem).c_str(), Settings.couponEthereal.stat,
        Settings.couponEthereal.chance, static_cast<unsigned long long>(CouponOutputs.load()),
        static_cast<unsigned long long>(CouponEthereals.load()));
    plugin->WriteConsoleMessage(text);
    std::snprintf(text, sizeof(text), "Mythical Uniques: %s, stat %d, +%d%%, %zu bases | second rolls %llu, uniques %llu",
        StateText(Settings.mythical.enabled, MythicalPatched, MythicalProblem).c_str(), Settings.mythical.stat, Settings.mythical.percent,
        Settings.mythical.codes.size(), static_cast<unsigned long long>(MythicalSecondRolls.load()),
        static_cast<unsigned long long>(MythicalUniques.load()));
    plugin->WriteConsoleMessage(text);
    std::snprintf(text, sizeof(text), "Coupon Upgrade: %s, stat %d, %d%% to exceptional, %d%% to elite | upgraded %llu, to elite %llu",
        StateText(Settings.couponUpgrade.enabled, BuilderHooked, BuilderProblem).c_str(), Settings.couponUpgrade.stat,
        Settings.couponUpgrade.exceptionalChance, Settings.couponUpgrade.eliteChance,
        static_cast<unsigned long long>(CouponsUpgraded.load()), static_cast<unsigned long long>(CouponsToElite.load()));
    plugin->WriteConsoleMessage(text);
    std::snprintf(text, sizeof(text), "Crystal Upgrade: %s, stat %d, %d%% | upgraded %llu",
        StateText(Settings.crystal.enabled, BuilderHooked, BuilderProblem).c_str(), Settings.crystal.stat, Settings.crystal.chance,
        static_cast<unsigned long long>(CrystalsUpgraded.load()));
    plugin->WriteConsoleMessage(text);
    std::snprintf(text, sizeof(text), "Magic Runewords: %s, stat %d | magic items let through %llu",
        StateText(Settings.runewords.enabled, RunewordPatched && SocketInsertHooked, RunewordProblem).c_str(), Settings.runewords.stat,
        static_cast<unsigned long long>(MagicRunewordsAllowed.load()));
    plugin->WriteConsoleMessage(text);
    std::snprintf(text, sizeof(text), "Mass Dig: %s, srvdofunc %d, radius %s, chance %s, skill %d level %d with stat %d | casts %llu, "
        "corpses searched %llu, finds %llu, casts with no aurarangecalc radius %llu",
        StateText(Settings.massDig.enabled, MassDigInstalled, MassDigProblem).c_str(), Settings.massDig.srvdofunc,
        Settings.massDig.radius < 1 ? "aurarangecalc" : std::to_string(Settings.massDig.radius).c_str(),
        Settings.massDig.chance < 0 ? "calc1" : std::to_string(Settings.massDig.chance).c_str(),
        Settings.massDig.skill, Settings.massDig.level, Settings.massDig.stat,
        static_cast<unsigned long long>(MassDigCasts.load()), static_cast<unsigned long long>(MassDigCorpses.load()),
        static_cast<unsigned long long>(MassDigFinds.load()), static_cast<unsigned long long>(MassDigNoRange.load()));
    plugin->WriteConsoleMessage(text);
    std::snprintf(text, sizeof(text), "Skill Scrolls: %s, %zu scrolls | used %llu (cast at a unit %llu, at a spot %llu, not cast %llu) | use rule and "
        "cooldowns %s: refused in town %llu, refused on cooldown %llu, refused because the game would not cast %llu, cooldowns "
        "started %llu, own skill cooldowns skipped %llu | sweep %s: drawn %llu",
        StateText(Settings.scrolls.enabled, ScrollPatched, ScrollProblem).c_str(), Scrolls.size(),
        static_cast<unsigned long long>(ScrollsUsed.load()), static_cast<unsigned long long>(ArtefactCastsAtUnit.load()),
        static_cast<unsigned long long>(ArtefactCastsAtSpot.load()), static_cast<unsigned long long>(ArtefactCastsFailed.load()),
        StateText(Settings.scrolls.enabled, UseRulePatched, CooldownProblem).c_str(),
        static_cast<unsigned long long>(ArtefactTownRefusals.load()), static_cast<unsigned long long>(ArtefactUsesRefused.load()),
        static_cast<unsigned long long>(ArtefactCastRefusals.load()), static_cast<unsigned long long>(ArtefactCooldownsStarted.load()), static_cast<unsigned long long>(NativeCooldownsSkipped.load()),
        StateText(Settings.scrolls.enabled && Settings.scrolls.sweep, SweepPatched, SweepProblem).c_str(),
        static_cast<unsigned long long>(ArtefactSweepDraws.load()));
    plugin->WriteConsoleMessage(text);
    std::snprintf(text, sizeof(text), "Drop credit: %s", Settings.creditMinionOwner ? "the killer, or the owner of a summon or hireling "
        "that got the kill" : "the player's own kills only");
    plugin->WriteConsoleMessage(text);
    const RuneConfig& r = Settings.runes;
    std::snprintf(text, sizeof(text), "Socketed Runes: %s, stat %d, +%d%%, %zu rune codes, %zu stats | tooltips %s%s%s | your "
        "character now pools %u runes, %u items show their bonus", r.enabled ? "on" : "off", r.stat, r.percent, RuneCodes.size(),
        RuneStatCount, !r.tooltips ? "off" : TooltipList != nullptr ? "on" : "off", TooltipProblem.empty() ? "" : ": ",
        TooltipProblem.c_str(), PooledRunes.load(), MergedItemsNow.load());
    plugin->WriteConsoleMessage(text);
    std::snprintf(text, sizeof(text),
        "Passes: host %llu (%s), client %llu (%s) | lists added %llu, removed %llu, values written %llu",
        static_cast<unsigned long long>(HostPasses.load()), ServerChain.load() != 0 ? "running" : "stopped",
        static_cast<unsigned long long>(ClientPasses.load()), ClientChain.load() != 0 ? "running" : "stopped",
        static_cast<unsigned long long>(BonusListsAdded.load()), static_cast<unsigned long long>(BonusListsRemoved.load()),
        static_cast<unsigned long long>(BonusValuesWritten.load()));
    plugin->WriteConsoleMessage(text);
    plugin->WriteConsoleMessage("\"archaeology items\" lists the bonus on your character and on each item.");
    if (!ConfigProblems.empty()) {
        std::snprintf(text, sizeof(text), "Config: %s", ConfigProblems.c_str());
        plugin->WriteConsoleMessage(text);
    }
    return D2RL::ConsoleCommandResult::Handled;
}

constexpr D2RL::PluginInfo Info{
    .infoSize    = D2RL::PluginInfoSize,
    .abiVersion  = D2RL_PLUGIN_ABI_VERSION,
    .id          = "celestialrayone.archaeology",
    .name        = "Archaeology",
    .version     = PluginVersion,
    .author      = "CelestialRayOne",
    .description = "Archaeologist specialization perks: Containers Opened, Superior Finds, Socketed Runes, Gold Find, "
                   "Identified Drops, Sockets, Sell Value, Coupon Ethereal, Mythical Uniques, Coupon Upgrade, Crystal Upgrade and "
                   "Magic Runewords; plus the Total Gold Found stat.",
    .flags       = D2RL::PluginFlags::Shared | D2RL::PluginFlags::NativeHooks,
};

}  // namespace
}  // namespace CelestialRayOne::Archaeology

using namespace CelestialRayOne::Archaeology;

D2RL_PLUGIN_EXPORT auto D2RLoaderGetPluginInfo() noexcept -> const D2RL::PluginInfo* {
    return &Info;
}

D2RL_PLUGIN_EXPORT auto D2RLoaderLoadPlugin(const D2RL::PluginContext* context) noexcept -> bool {
    if (!D2RL::HasContext(context)) return false;
    Context = context;
    Base = context->exeBase;
    ReadConfiguration();
    if (!context->RegisterConsoleCommand("archaeology", &StatusCommand,
            "Archaeology perks: settings and counters; \"archaeology items\" lists each bonus.")) {
        context->LogWarn("Archaeology: the console command could not be registered.");
    }
    if (!Settings.enabled) {
        InstallProblem = "disabled in the config";
        context->LogInfo("Archaeology: disabled in the config; nothing installed.");
        return true;
    }
    if (!Install()) return true;
    char totalGold[48];
    if (!Settings.totalGold.enabled || !GoldPatched) std::snprintf(totalGold, sizeof(totalGold), "off");
    else if (Settings.totalGold.stat < 0) std::snprintf(totalGold, sizeof(totalGold), "off, stat_id not set");
    else std::snprintf(totalGold, sizeof(totalGold), "stat %d", Settings.totalGold.stat);
    D2RL::LogInfoF(context, "Archaeology %s: installed (containers %s, superior finds %s, socketed runes %d%%, gold find %s, "
        "total gold found %s, identified drops %s).", PluginVersion, ContainerHooked && Settings.containers.enabled ? "on" : "off",
        SuperiorPatched ? "on" : "off", Settings.runes.percent, GoldPatched ? "on" : "off", totalGold, IdentifyPatched ? "on" : "off");
    return true;
}

D2RL_PLUGIN_EXPORT void D2RLoaderUnloadPlugin() noexcept {
    ServerChain.store(0);
    ClientChain.store(0);
    Ready = false;
    if (Context != nullptr) Restore();
    Context = nullptr;
}
