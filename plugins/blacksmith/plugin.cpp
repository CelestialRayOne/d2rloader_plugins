// =============================================================================
//  Blacksmith  -  celestialrayone.blacksmith
//
//  The Blacksmith perks of the specialization system that need code:
//    Tempered Steel    +10% to the damage modifiers on the character's weapons
//    Mithril Weaving   +10% base defense on the character's armor
//    Spectral Arsenal  dropped weapons roll ethereal at 10% instead of 5%
//    Blessed Gear      a dropped item's base can go up one tier after rolling
//    Relic Hunter      +5% total chance for weapons and armor to drop unique
//    The Secret Pocket (a Jeweler perk) +5% total chance for rings, amulets and
//                      jewels to drop unique: it shares Relic Hunter's hook,
//                      because only one plugin can patch the unique roll
//    Weightless        -25% total strength and dexterity requirements
//    Additional Modifier  magic, rare and crafted weapons and armor made for
//                      the character get one more affix 25% of the time
//    Hidden Potential  (a Jeweler perk) the same for magic, rare and crafted
//                      rings and amulets
//    Brilliant Quality (a Jeweler perk) a jewel made for the character has one
//                      of its modifiers doubled 25% of the time
//                      (both ride on Additional Modifier's hook on 0x43EF10,
//                      which only one plugin can hold)
//    Durability Defense  +1% base defense per point of durability on armor
//    Modifier Magnitude  +10% to every listed modifier on the weapons
//    Ethereal Weapons  equipped ethereal weapons get +75% base damage instead
//                      of +50%, and +10% total spell damage while one is equipped
//    Unbreakable Gear  equipped gear loses no durability; +1% total damage per
//                      25 durability across the equipped gear
//    Extended Reach    +3 melee range with any weapon but a bow or crossbow,
//                      +25% projectile speed
//  A perk counts while the character's own value of its stat is above 0.
//
//  Built for D2R 3.3 under D2RLoader 1.3.1. Every address below was read out
//  of the dumped D2RLoader 1.3.1 image (md5 8af7a47ce9077de0378c69af2930854d)
//  and disassembled before being relied on. Byte witnesses guard every site
//  and pin every native function the plugin calls (each witness contains the
//  game's own call to it), so a different build installs nothing.
//
// -----------------------------------------------------------------------------
//  WHO A DROP BELONGS TO
// -----------------------------------------------------------------------------
//  The treasure class roll 0x4404F0 (game, monster, killer, ...) homes the
//  killer once, at 0x44055B mov [rbp-80h],r8; rbp is its frame base for the
//  whole body. Each item it produces is built through one call:
//
//      4410B0  48 8B 54 24 58        mov  rdx, [rsp+58h]      ; dropping unit
//      4410B5  45 8B CE              mov  r9d, r14d           ; quality
//      4410B8  45 8B C7              mov  r8d, r15d           ; item class
//      4410BC  49 8B CC              mov  rcx, r12            ; game
//      4410BF  89 74 24 28           mov  [rsp+28h], esi      ; flags
//      4410C3  89 7C 24 20           mov  [rsp+20h], edi      ; set/unique row
//      4410C7  E8 B4 35 00 00        call 444680              ; spawn the item
//
//  0x444680 fills the drop request and calls the item builder 0x43CD50, which
//  allocates the unit and runs 0x43EF10 (base stats, then 0x442D60: quality,
//  set or unique row, affixes, ethereal, sockets). The killer is not passed
//  down, so the call at 4410C7 goes through a relay that reads [rbp-80h] and
//  calls SpawnWithKiller, which records the credited player (a player killer,
//  or the player owner of a minion killer through 0x4A53C0, the lookup the
//  quality roll itself uses for magic find) in a thread-local drop context
//  for exactly the duration of that one item. Every other way an item is made
//  (vendors, gambling, the cube, quests) runs with no context and is never
//  touched.
//
// -----------------------------------------------------------------------------
//  RELIC HUNTER: the unique roll in D2GAME_ITEMS_RollItemQuality 0x4421B0
// -----------------------------------------------------------------------------
//  Called only from the treasure class roll (0x440FD9) with the killer as its
//  third argument. After magic find and the treasure class modifier, ebx is
//  the unique range: the item is unique when rand(range) < 128, or always
//  when the range is 0 or below.
//
//      442427  2B D8                 sub  ebx, eax            ; final range
//      442429  85 DB                 test ebx, ebx            ; <- hook
//      44242B  0F 8E 1A 02 00 00     jle  44264B              ; return 7
//      442431  48 8B 8C 24 88 00..   mov  rcx, [rsp+88h]      ; <- continue
//      442439  E8 A2 7D F0 FF        call 34A1E0              ; seed
//      442443  E8 B8 16 D1 FF        call 153B00              ; rand(range)
//      442448  3D 80 00 00 00        cmp  eax, 80h
//
//  The 8 bytes at 442429 become a jump to a relay that calls
//  AdjustUniqueRange(range, killer [rsp+90h], class [rsp+0A0h]) and replays
//  the test and branch on the result. For a perk player and a matching item
//  the range becomes range * 100 / (100 + bonus), which multiplies the unique
//  chance by (100 + bonus) / 100. The Jeweler's The Secret Pocket (rings,
//  amulets and jewels) is a second entry in the same function: the roll can
//  carry only one patch, so a jeweler plugin of its own could not hook it.
//  Each perk whose types the item's class matches divides the range once. The frame (7 pushes, sub rsp,40h, homed
//  arguments) is pinned by the prologue witness. Only the fallthrough from
//  442429 enters 44242B (IDA cross references), and every volatile register
//  is dead at both 442431 and 44264B.
//
// -----------------------------------------------------------------------------
//  SPECTRAL ARSENAL: the ethereal roll in 0x442D60
// -----------------------------------------------------------------------------
//  rdi is the item and rbp the drop request. The gates before it (weapon or
//  armor, durability, not low quality, not a set item, not a quest item) are
//  untouched.
//
//      4434C9  E8 B2 97 F2 FF        call 36CC80              ; item seed
//      4434D1  BA 64 00 00 00        mov  edx, 64h
//      4434D6  E8 25 06 D1 FF        call 153B00              ; rand(100)
//      4434DB  33 C9                 xor  ecx, ecx            ; <- hook
//      4434DD  83 F8 05              cmp  eax, 5              ;    (5 bytes)
//      4434E0  41 0F 42 CD           cmovb ecx, r13d          ; ethereal
//
//  The 5 bytes at 4434DB become a call to a relay that asks
//  EtherealThreshold(item, request) for the threshold, then replays
//  xor ecx,ecx / cmp eax,threshold, so the cmovb right after reads the same
//  flags it always did. Only the fallthrough from 4434DB enters 4434DD.
//
// -----------------------------------------------------------------------------
//  BLESSED GEAR: the cube's own upgrade, applied after the drop has rolled
//  -----------------------------------------------------------------------------
//  The Horadric Cube's "useitem,mod,exc" and "useitem,mod,eli" outputs keep
//  the item and change only its base. The cube generator 0x5269C0 does it in
//  place with three calls:
//
//      526FEB  call 34DE10            ; class id = upgraded base (unit+4)
//      527009  call 43EF10            ; (game, &item, null, 0): base stats
//                                     ; only: block, speed, durability,
//                                     ; defense, damage, stack size
//      52702D  call 36E2D0            ; ethereal flag 400000h?
//      527043  call 3D3510            ; then the ethereal base conversion
//
//  The binder 0x529D50 takes the upgraded base from items.txt: ubercode
//  (record +88h) for exc, ultracode (+8Ch) for eli, an item being normal when
//  its code (+80h) equals normcode (+84h). 0x313EF0 turns a code into a class.
//
//  The plugin hooks the entry of 0x43EF10. After the original returns for a
//  drop with a perk player (the builder calls it with the request and 1),
//  the item has its final quality, set or unique row, affixes and ethereal
//  state. With chance_percent (rolled on the item's own seed through
//  0x36CC80 / 0x153B00, like every other item roll) it runs the cube's three
//  calls, and for an ethereal item also the durability step 0x442D60 runs
//  for a fresh ethereal drop (4434FF..443558): half the base max durability
//  plus one, and full current durability. The builder then finishes the item
//  as usual (Chronicle record, placement, packets), so the client only ever
//  sees the upgraded item.
//
// -----------------------------------------------------------------------------
//  WEIGHTLESS: the requirement percent
// -----------------------------------------------------------------------------
//  An item's strength and dexterity requirements are its items.txt reqstr
//  (record +11Ah) and reqdex (+11Ch) plus a percent of them, minus 10 for an
//  ethereal item. The percent is the character's weapon mastery requirement
//  bonus (D2RCore ReadWideWeaponMastery through the thunk 0x33D4F0, effect 6)
//  plus the item's item_req_percent (stat 91 through 0x2F5C60). It is built
//  in exactly two places:
//
//    ITEMS_CheckItemRequirementsForUnit 0x36BC50, the only requirement check
//    (equipping, mercenaries, the red inventory backgrounds, charms):
//      36BDE6  E8 05 17 FD FF        call 33D4F0     ; (unit, item, 0, 6)
//      36BDF4  41 8D 50 5B           lea  edx,[r8+5Bh]
//      36BDF8  E8 63 9E F8 FF        call 2F5C60     ; (item, 91, 0)
//      36BDFD  44 03 F0              add  r14d, eax  ; the percent
//    0x371FA0 (unit, item) = the same sum, which the tooltip builders
//    0x2BD480 and 0x2BF1B0 and the UI 0x1454B80 use for the numbers shown:
//      371FB6  E8 35 B5 FC FF        call 33D4F0     ; (unit, item, 0, 6)
//      371FC7  E8 94 3C F8 FF        call 2F5C60     ; (item, 91, 0)
//
//  Both mastery calls go to WeightlessMastery, which returns the mastery
//  value plus whatever turns the final percent p into
//      (100 + p) * (100 - reduction) / 100 - 100,
//  so the requirement before the ethereal 10 becomes
//      base * (100 + p) / 100 * (100 - reduction) / 100:
//  multiplied with every requirement modifier, as the perk says. The check,
//  the tooltip numbers and their colours therefore always agree. Only the
//  calls change; unusable-items keeps its entry hook on 0x36BC50.
//
// -----------------------------------------------------------------------------
//  TEMPERED STEEL AND MITHRIL WEAVING: a bonus stat list on the item itself
// -----------------------------------------------------------------------------
//  Each equipped weapon or armor piece of a character with the perk carries one
//  extra plain stat list of the plugin's, attached to the item the way 0x43EF10
//  gives an item its quest-difficulty list (43F407..43F487):
//      2F7300  alloc (flags, 0, owner type 4, item id)
//      2F2CB0  attach (item, list, 1)
//      2F7C00  set (data context, list, stat, value, 0)
//  and removed with 2F7920 unlink (item, list) + 2F4180 free (context, list).
//  The list is a child of the item, so the engine treats its stats exactly
//  like the item's own: the item's op 13 multiplies its base damage or defense
//  with the item's full enhanced damage, the item total goes to the character
//  only while the item's own list is attached (active weapon set, requirements
//  met), and the character's damagepercent, armor_override_percent and the rest
//  apply on top. The item's tooltip shows the raised values.
//
//  Never saved, never sent: the item serializer only writes lists found
//  through 2F5A00 with flag 40h or 2040h (37F031..37F069; the runeword list is
//  state 171 + 40h), and the plugin's list uses flag 10000h. The D2RCore
//  attach (+3DA4F0) links a list without 2000h into the parent's chain at +90h
//  and adds its stats; its death cleanup (+3DAD30) skips lists owned by items.
//  Server and client each keep their own lists, built from the same items, so
//  combat (server) and the character sheet and tooltips (client) agree.
//
//  The values, recomputed from the item's own stats (with the plugin's list's
//  values taken back out, so they never feed on themselves):
//    weapon, p = bonus_percent:
//      ED 17/18, elemental damage and every other scaled stat: own * p / 100
//      flat damage 21/22, 23/24, 159/160: (total - base - base * ED / 100 - ours)
//        * p / 100, base from 2F48C0, the ED term exactly as op 13 adds it
//    armor, p = bonus_percent:
//      armorclass: (b + b * ED16 / 100) - (base + base * ED16 / 100),
//        b = base + base * p / 100: the base defense raised by p, then the
//        item's own enhanced defense on it, as if the item had dropped that way
//  Every equipped item of a character in body locations 1 to 12 is checked
//  (INVENTORY_GetItemFromBodyLoc 0x3886D0 on UNITS_GetInventory 0x34A360), so
//  the swap weapons are ready when swapped in. An item that stops qualifying
//  (unequipped, given to a hireling, perk unlearned) loses its list on the next
//  pass; the plugin looks each one up again by id (SUNIT_GetServerUnit
//  0x48FE80 on the host, CLIENT_GetUnitByIdAndType 0x9A5D0 on a client) and
//  only touches a list it finds in that item's child chain.
//
//  Durability Defense and Modifier Magnitude use the same lists. Durability
//  Defense raises base defense by its percent per point of the piece's
//  durability (stat 72, or 73 with use_max_durability), multiplied with
//  Mithril Weaving's raise: base * (100 + mithril) / 100 * (100 + durability)
//  / 100. Modifier Magnitude raises every stat in its list on the weapons the
//  way Tempered Steel raises the damage stats; a stat both perks raise gets
//  both percents. Stats are handled per layer: for a stat with a parameter (a
//  skill, a class, an aura, a proc) only the value of each parameter rises,
//  the parameter stays, so +12 to a skill becomes +13 to the same skill. The
//  layers an item carries come from its full stats array, read where
//  D2RCore!VisitWideUnitStats reads it. item_charged_skill (204) is never
//  raised: its value packs the charges and the maximum charges.
//
//  Passes run on D2RLoader's thread service: on the host's game thread every
//  4 game updates for every player in the game (the server client registry,
//  as specializations reads it), and on the UI thread every 100 ms for the
//  local player.
//
// -----------------------------------------------------------------------------
//  ADDITIONAL MODIFIER: one more affix through the game's own affix roll
// -----------------------------------------------------------------------------
//  Every magic affix is rolled by 0x58AE00 (item, 1, must pick, apply, prefix,
//  forced id), which hands the item to D2RCore SelectExpansionMagicAffix (or
//  the classic one for a classic item). GenerateMagicAffixes calls it once
//  for the prefix and once for the suffix (442CB3, 442CFB) with apply = 1 and
//  stores the ids in slot 0 through 36DB40 / 36DBE0 (item, id, slot). The
//  selector takes only spawnable affixes of the item's types and affix level,
//  the rare column for rare, crafted and tempered items, and no group already
//  on the item: it collects the groups of the item's prefix and suffix slots
//  (36CD50 / 36CDE0) before it picks.
//
//  After 0x43EF10 has rolled a new item, for a magic (4), rare (6) or crafted
//  (8) weapon or armor piece made for a character with the perk, and with
//  chance_percent on the item's own seed, the plugin makes the same call with
//  must pick = 1 and apply = 1, for a prefix or a suffix (whichever kind has a
//  free slot; when both do, the seed decides; when the first kind has nothing
//  left to give, the other is tried). The id goes into the first free slot of
//  that kind: slots 0 to 2 for rare and crafted items, slot 0 for magic items
//  (a magic item saves only slot 0 of each kind). With no free slot the affix
//  still applies; its values are in the item's magic stat list, which is saved
//  with the item, so they stay.
//
//  Made for the character: a treasure-class drop credited to it (the drop
//  context above), or any item whose build request names it as the unit
//  (request +00h, which the builder 0x43CD50 itself reads as a unit): Horadric
//  Cube outputs (the cube builds its request with the player at +00h,
//  527526..52754F) and the other items the game creates for a player. Vendor
//  stock is not: D2GAME_NPC_GenerateStoreItem 0x540EA0 builds it through
//  0x43D530 with the vendor as the unit.
//
//  Which Horadric Cube outputs: only those that build a new item. The cube
//  generator 0x5269C0 runs each output by its type byte (output +67h):
//    item code FCh, item type FDh, usetype FFh: a new item through the builder
//      at 527837 (game, request with the player at +00h, 0), which runs
//      0x43EF10 with the request and 1, so the perks apply once to the fresh
//      roll (a usetype output replaces the input with a brand-new item);
//    useitem FEh: keeps the input item. Without "mod" (+5Eh bit 0) it never
//      reaches the builder or 0x43EF10 (52710B..527448: sockets, repair,
//      recharge and the recipe's own mods are applied to the item as it is);
//      with "mod" the base changes in place through 0x43EF10 (game, &item,
//      null, 0) at 527009, and the hook only acts when a request is passed;
//    cloneitem FBh (the clone-item plugin): copied through 0x43D660, serialize
//      and decode, which never calls 0x43EF10 or the builder.
//  The other callers of 0x43EF10 are the builder and preset map items
//  (0x48DC50, a request with no unit). So an item that already exists never
//  gains an affix or a doubled modifier, however often it goes through a
//  recipe.
//
//  Hidden Potential (jeweler_7) is a second entry of the same routine for
//  rings and amulets. Brilliant Quality (jeweler_8): for a jewel made for a
//  character with the perk, with chance_percent on the item's seed, one of its
//  modifiers is doubled. The modifiers are the stats of the jewel's own
//  modifier list, the one 0x43EF10 itself finds with 2F5B80 (item, state 0,
//  flag 40h) for stat 356 (43F407): affixes, or a unique's properties, which
//  the item saves. A modifier is what the tooltip shows as one line: the
//  damage pairs (17/18, 48/49, 50/51, 52/53, 54/55, 57/58, and 21-24/159/160
//  together), an itemstatcost dgrp group when every member is present with the
//  same value (all resistances, all attributes, ...), and every other stat and
//  parameter on its own. Encoded stats (encode 2, skill on event; 3, charges)
//  and stat 356 are left out. dgrp (word +3Ah) and encode (word +2Ch) come
//  from the itemstatcost record (tables +1258h, stride 144h, count +1260h, as
//  the record getter 0x2D9730 reads them), checked against the vanilla
//  groups once per data context. Each member is set to twice its value
//  through 2F7C00 on that list, so the change is saved with the jewel.
//  Gamble purchases count for these two while their gambling setting is on
//  (the default).
//
//  Gambling builds the item for the vendor too, so it needs its own context.
//  The purchase 0x53D850 (game, player, vendor, ...) homes the player at
//  [rsp+58h] and the vendor at [rsp+68h] (53D8AA..53D8B8) and makes the item
//  through 0x541AB0 (game, vendor, class, item level, seed), which rolls the
//  gamble quality and calls 0x43D530 with the vendor:
//      53EB3D  49 8B CD              mov  rcx, r13
//      53EB40  E8 6B 2F 00 00        call 541AB0            ; <- relay
//      53EBDC  49 8B CD              mov  rcx, r13
//      53EBDF  E8 CC 2E 00 00        call 541AB0            ; <- relay
//  Both calls go through a relay that adds [rsp+58h] as a sixth argument;
//  GambleWithPlayer records the player for exactly that item. The gamble
//  screen's own preview items (0x541880 -> 541A4C) are not touched.
//
// -----------------------------------------------------------------------------
//  ETHEREAL WEAPONS: the ethereal routine's +50%, raised to +75% while equipped
// -----------------------------------------------------------------------------
//  The ethereal routine 0x3D3510 sets the ethereal flag and each weapon base
//  stat (21/22 one-hand, 23/24 two-hand, 159/160 throw) to 3 * base / 2, once,
//  when the item is made (drop roll 0x4434FF, cube 0x527043, property 0x3CCCCB).
//  It knows nothing of who equips the item, so the perk lives on the item's
//  bonus list instead: the items.txt base comes back exactly as ceil(2b/3), the
//  list adds base * 175 / 100 - b with the weapon's enhanced damage (op 13, 18
//  for min, 17 for max) applied on top, as the engine applies it to its base.
//  The total spell damage stats ride on one ethereal weapon per weapon set.
//
// -----------------------------------------------------------------------------
//  UNBREAKABLE GEAR: no durability loss, total damage from the gear's durability
// -----------------------------------------------------------------------------
//  Durability is lost in two places only:
//    0x441B10 (game, unit, item), 4% weapon / 10% armor per hit, called from the
//      melee hit's durability step 0x4526C0 at 45270C (the attacker's weapon)
//      and 4528CD (a random armor piece of the defender);
//    0x5590C0 (game, unit, item, chance, amount), a skill's own loss, called at
//      55881B. A stackable item spends quantity there instead, which stays.
//  The three calls go to relays 6 and 7, which jump to the plugin. The calls
//  are redirected rather than the routines' entries so another plugin's entry
//  hook on them keeps working. The damage bonus: the current durability of the
//  active gear (body locations 1 to 10) is pooled, divided by points per 1%, and
//  carried on one active item's bonus list (body armor first).
//
// -----------------------------------------------------------------------------
//  EXTENDED REACH: melee range and projectile speed
// -----------------------------------------------------------------------------
//  weapons.txt rangeadder is items.txt +115h. Its only reader is 0x371F10,
//  called only by UNITS_GetMeleeRange 0x34A620 (unit): class bonus + the
//  rangeadder of INVENTORY_GetEquippedWeapon 0x385D20's weapon (right hand if it
//  is a weapon, else left). Server range checks and the client's walk to attack
//  both read it, so an inline hook on it adds the perk's range for a character
//  holding a weapon that is not a bow or crossbow. Projectile speed rides on
//  the same carrier as Unbreakable Gear's bonus.
//
// -----------------------------------------------------------------------------
//  ITEM TYPES
// -----------------------------------------------------------------------------
//  ITEMS_CheckItemTypeId 0x373890 (item, type) tests the item's two items.txt
//  types (+12Eh, +130h) in the itemtypes equivalence table of its data
//  context: row = 0x367220(tables + 1368h, type * dword [tables + 1360h]),
//  bit target. Relic Hunter only has the item class, so it runs the same test
//  on the class's record. itemtypes.txt codes are resolved against the live
//  table (tables + 1348h, count + 1350h, rows of E8h bytes, code at +0).
//
//  Console command "blacksmith" shows the settings, what is installed and
//  counters. Settings: d2rloader/config/celestialrayone.blacksmith.toml
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
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

namespace CelestialRayOne::Blacksmith {
namespace {

constexpr char PluginVersion[] = "1.3.1";

constexpr const char DefaultConfigToml[] = R"TOML(# celestialrayone.blacksmith
#
# Blacksmith
#   The Blacksmith perks of the specialization system that need code. A perk
#   counts while the character's own value of its stat is above 0 (the value
#   the specializations plugin writes when the perk is learned). Each perk
#   below names its stat, so the stat ids follow your itemstatcost.txt.
#
# Who a drop belongs to
#   Drop perks look at the player who got the kill: the killer itself, or the
#   owner of the summon or hireling that got it (credit_minion_owner). Chests
#   and other objects count for the player who opened them. Items made any
#   other way (vendors, gambling, the Horadric Cube, quests) are never changed.
#
# Item types
#   item_types takes itemtypes.txt codes. A parent code covers every type
#   below it: "weap" is every weapon, "armo" is every equippable armor piece
#   (body armor, helm, gloves, belt, boots, shield).
#
#
# Built for D2RLoader 1.3.1 (D2R 3.3). Every native site is checked byte for
# byte before anything is written; on any other build the plugin changes
# nothing and the console command "blacksmith" says why.

[blacksmith]

# Master switch. false loads the plugin without changing the game.
enabled = true

# true: a summon's or hireling's kill counts for its owner.
# false: only the player's own kills count.
credit_minion_owner = true

# true writes the first perk events to the plugin log. Keep false for play.
diagnostics = false

# Tempered Steel (blacksmithing_1)
#   Every damage modifier on the character's equipped weapons is raised by
#   bonus_percent: enhanced damage, flat minimum and maximum damage (including
#   secondary and throwing damage), damage per level, and the stats listed in
#   total_damage_stats, spell_damage_stats and elemental_damage_stats (fire,
#   cold, lightning, poison and magic min/max damage; cold and poison length
#   stay as they are, so the whole hit or poison grows). Base damage is not a modifier
#   and is not raised; the character's own enhanced damage (damagepercent) and
#   anything not on the weapon are not touched either. Rounding follows the
#   game: 33% enhanced damage gains 3%.
#   The bonus sits on the weapon, so the weapon's tooltip shows it, it counts
#   only while the weapon is active, and everything the game multiplies weapon
#   damage by (damagepercent, strength or dexterity, skills) applies on top.
[tempered_steel]
enabled = true
perk_stat = 946
bonus_percent = 10
item_types = ["weap"]
total_damage_stats = [466, 467, 832, 833]
spell_damage_stats = [329, 330, 331, 332, 482, 483]
elemental_damage_stats = [48, 49, 54, 55, 50, 51, 57, 58, 52, 53]

# Mithril Weaving (blacksmithing_2)
#   The base defense of each equipped armor piece is raised by bonus_percent
#   before the piece's own enhanced defense, so a piece with 100 base defense
#   and +200% enhanced defense goes from 300 to 330. Everything the game
#   multiplies total defense by (armor_override_percent and the rest) applies
#   on top. The bonus sits on the item, so its tooltip shows it.
[mithril_weaving]
enabled = true
perk_stat = 947
bonus_percent = 10
item_types = ["armo"]

# Durability Defense (blacksmithing_8)
#   Each equipped armor piece gets percent_per_point % more base defense for
#   every point of its durability, multiplied with Mithril Weaving's raise: a
#   piece with 100 base defense, 30 durability and both perks gets
#   100 * 1.10 * 1.30 = 143 base defense, before its own enhanced defense.
#   use_max_durability = true counts the piece's maximum durability instead of
#   its current durability.
[durability_defense]
enabled = true
perk_stat = 953
percent_per_point = 1
use_max_durability = false
item_types = ["armo"]

# Modifier Magnitude (blacksmithing_9)
#   Every stat listed in stats is raised by bonus_percent on the character's
#   equipped weapons, the same way Tempered Steel raises damage: only the
#   weapon's modifiers count, never its base values, and the game's rounding
#   applies (values under 10 gain nothing at 10%). A stat Tempered Steel also
#   raises gets both percents. For a stat with a parameter (a skill, a class,
#   an aura, a proc) only the value rises and the parameter stays: +12 to a
#   skill becomes +13 to the same skill, a level 15 aura level 16, a 20% skill
#   proc 22%. item_charged_skill (204) can't be raised: its value packs the
#   charges and the maximum charges.
#   One itemstatcost.txt id per line; delete a line to stop raising that stat.
[modifier_magnitude]
enabled = true
perk_stat = 954
bonus_percent = 10
item_types = ["weap"]
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

# Weightless (blacksmithing_3)
#   The strength and dexterity requirements of the character's weapons and
#   armor are multiplied by (100 - reduction_percent) / 100, after every
#   requirement modifier (item_req_percent, weapon mastery). An ethereal
#   item's 10 points come off after that. With 25, a base 100 becomes 75 and
#   an item with +500% requirements goes from 600 to 450. The tooltip numbers
#   and the equip check use the same value.
[weightless]
enabled = true
perk_stat = 948
reduction_percent = 25
item_types = ["weap", "armo"]

# Additional Modifier (blacksmithing_7)
#   A magic, rare or crafted weapon or armor piece made for the character gets
#   one more affix with chance_percent: drops credited to the character,
#   Horadric Cube outputs, and anything else the game builds in the
#   character's name. Vendor stock is built for the vendor and never counts.
#   Normal, superior, set and unique items and runewords never do.
#   The affix comes from the game's own affix roll for that item, so it obeys
#   the same rules as the others (affix level, item type, the rare column for
#   rare and crafted items, and no group the item already has). A magic item
#   that had only a prefix or only a suffix fills the empty half of its name;
#   otherwise the affix takes a free affix slot, or, when none is left, adds
#   its stats without one (they are saved with the item either way).
#   Only cube outputs that make a new item count. "useitem" outputs keep the
#   item they were given and "cloneitem" outputs copy it, so neither ever
#   gains an affix, however often an item goes through a recipe.
[additional_modifier]
enabled = true
perk_stat = 952
chance_percent = 25
item_types = ["weap", "armo"]

# Hidden Potential (jeweler_7, a Jeweler perk)
#   Additional Modifier for rings and amulets: a magic, rare or crafted ring
#   or amulet made for the character gets one more affix with chance_percent.
#   Same rules as above: drops credited to the character, gamble purchases and
#   cube outputs that make a new item, never "useitem" or "cloneitem" outputs.
#   gambling = false leaves rings and amulets bought from a gamble screen out.
[hidden_potential]
enabled = true
perk_stat = 967
chance_percent = 25
item_types = ["ring", "amul"]
gambling = true

# Brilliant Quality (jeweler_8, a Jeweler perk)
#   A jewel made for the character has one of its modifiers doubled with
#   chance_percent: drops credited to the character, gamble purchases and cube
#   outputs that make a new item, never "useitem" or "cloneitem" outputs.
#   A modifier is one tooltip line: a damage range (fire, cold, ...), enhanced
#   damage, an itemstatcost dgrp group whose members all have the same value
#   (all resistances, all attributes), or any other single stat. Skill on
#   event procs and charges are never picked. The doubled value is saved
#   with the jewel. A crafted jewel's fixed recipe mods are added after this
#   roll and are not candidates.
#   qualities: which jewels can roll it (magic, rare, crafted, unique, set).
#   exclude_stats: itemstatcost ids that are never doubled.
#   gambling = false leaves jewels bought from a gamble screen out.
[brilliant_quality]
enabled = true
perk_stat = 968
chance_percent = 25
item_types = ["jewl"]
qualities = ["magic", "rare", "crafted", "unique", "set"]
exclude_stats = [
  356,  # questitemdifficulty
]
gambling = true

# Spectral Arsenal (blacksmithing_4)
#   Dropped weapons become ethereal with chance_percent instead of the vanilla
#   5%. The vanilla rules for which items can be ethereal still apply: the
#   item needs durability, and low quality and set items never roll ethereal.
[spectral_arsenal]
enabled = true
perk_stat = 949
chance_percent = 10
item_types = ["weap"]

# Blessed Gear (blacksmithing_5)
#   After a dropped item has finished rolling (quality, unique or set item,
#   affixes, ethereal), a normal base becomes its exceptional version and an
#   exceptional base its elite version with chance_percent. Only the base
#   changes: quality, the unique or set item, affixes, rolled values,
#   sockets and ethereal are kept, exactly like the Horadric Cube's
#   "useitem,mod,exc" and "useitem,mod,eli" upgrades. Defense, damage and
#   durability come from the new base. Elite bases do not change.
[blessed_gear]
enabled = true
perk_stat = 950
chance_percent = 10
item_types = ["weap", "armo"]

# Relic Hunter (blacksmithing_6)
#   The final chance of a dropped weapon or armor piece to be unique, after
#   magic find and the treasure class, is multiplied by (100 + bonus_percent)
#   / 100. With 5, a 2% chance becomes 2.1%.
[relic_hunter]
enabled = true
perk_stat = 951
bonus_percent = 5
item_types = ["weap", "armo"]

# The Secret Pocket (jeweler_6, a Jeweler perk)
#   Relic Hunter for rings, amulets and jewels: the final chance of a dropped
#   ring, amulet or jewel to be unique, after magic find and the treasure
#   class, is multiplied by (100 + bonus_percent) / 100. Like Relic Hunter it
#   multiplies with magic find and with anything that drops more items (item
#   quantity, Bountiful Jewelry): every item rolls its own unique chance.
#   It lives in this plugin because the unique roll can carry one patch only,
#   and Relic Hunter's patch already sits there.
[the_secret_pocket]
enabled = true
perk_stat = 966
bonus_percent = 5
item_types = ["ring", "amul", "jewl"]

# Ethereal Weapons (blacksmithing_10)
#   Ethereal weapons get +50% base damage from the game when they are made.
#   Each equipped ethereal weapon of the character gets base_bonus_percent more
#   of its items.txt base damage on top (25 makes it +75%), with the weapon's
#   own enhanced damage applied to the raise like to any base damage. One-hand,
#   two-hand and throw damage are all raised.
#   While an ethereal weapon is equipped, the character also gets
#   spell_damage_percent to every stat in spell_damage_stats. Two ethereal
#   weapons in hand still give it once; an ethereal weapon in the inactive
#   weapon set gives it only when that set is switched in.
[ethereal_weapons]
enabled = true
perk_stat = 955
base_bonus_percent = 25
spell_damage_percent = 10
spell_damage_stats = [
  398,  # generictotalmultiplier
  489,  # total_elemental_min_damage
  493,  # total_elemental_max_damage
  495,  # total_poison_magic_min_damage
  496,  # total_poison_magic_max_damage
]
item_types = ["weap"]

# Unbreakable Gear (blacksmithing_11)
#   The character's equipped gear loses no durability: neither from hits (4%
#   per hit for the weapon, 10% for an armor piece when struck) nor from
#   skills that wear the weapon down. Stackable throwing weapons still spend
#   quantity as usual.
#   The current durability of all active gear (both weapon hands of the
#   active set and every armor piece) is added up,
#   and every points_per_percent of it gives +1 to each stat in
#   total_damage_stats. Pooling avoids losing the remainder piece by piece.
#   item_types decides which gear counts toward the pool.
[unbreakable_gear]
enabled = true
perk_stat = 956
points_per_percent = 25
total_damage_stats = [
  398,   # generictotalmultiplier
  1012,
  1013,
  489,   # total_elemental_min_damage
  493,   # total_elemental_max_damage
  495,   # total_poison_magic_min_damage
  496,   # total_poison_magic_max_damage
]
item_types = ["weap", "armo"]

# Extended Reach (blacksmithing_12)
#   range_bonus is added to the weapon's weapons.txt rangeadder while the
#   character holds a weapon of item_types that is not one of
#   exclude_item_types. "miss" covers bows, crossbows and Amazon bows; throwing
#   weapons count when used in melee. Bare hands get nothing. The game's own
#   weapon pick decides which weapon counts (the right hand if it holds a
#   weapon, else the left), on both the server and the client.
#   The character also gets projectile_speed_percent to projectile_speed_stat
#   (itemstatcost.txt projectilespeed) while the perk is learned.
[extended_reach]
enabled = true
perk_stat = 957
range_bonus = 3
item_types = ["weap"]
exclude_item_types = ["miss"]
projectile_speed_stat = 516
projectile_speed_percent = 25
)TOML";

// ---------------------------------------------------------------------------
// Native anchors (D2RLoader 1.3.1 image, D2R 3.3)
// ---------------------------------------------------------------------------
// 440547..440564 (29 bytes)
constexpr std::uint8_t KillerHome[]{
    0x48, 0x89, 0x54, 0x24, 0x58, 0x48, 0x8D, 0x95, 0x98, 0x00, 0x00, 0x00, 0x4C, 0x89, 0x75, 0xA0,
    0x4D, 0x89, 0x7B, 0xC0, 0x4C, 0x89, 0x45, 0x80, 0x48, 0x89, 0x4C, 0x24, 0x70,
};
constexpr std::uint64_t KillerHomeRva = 0x440547;

// 4410AD..4410E5 (56 bytes)
constexpr std::uint8_t SpawnCallWindow[]{
    0x44, 0x8B, 0xF8, 0x48, 0x8B, 0x54, 0x24, 0x58, 0x45, 0x8B, 0xCE, 0x45, 0x8B, 0xC7, 0x90, 0x49,
    0x8B, 0xCC, 0x89, 0x74, 0x24, 0x28, 0x89, 0x7C, 0x24, 0x20, 0xE8, 0xB4, 0x35, 0x00, 0x00, 0x48,
    0x8B, 0xF8, 0x48, 0x85, 0xC0, 0x0F, 0x84, 0x79, 0x01, 0x00, 0x00, 0xBA, 0x04, 0x00, 0x00, 0x00,
    0x48, 0x8B, 0xC8, 0xE8, 0xAB, 0x27, 0xF3, 0xFF,
};
constexpr std::uint64_t SpawnCallWindowRva = 0x4410AD;

// 4421B0..4421D6 (38 bytes)
constexpr std::uint8_t QualityPrologue[]{
    0x48, 0x89, 0x5C, 0x24, 0x20, 0x4C, 0x89, 0x44, 0x24, 0x18, 0x48, 0x89, 0x54, 0x24, 0x10, 0x55,
    0x56, 0x57, 0x41, 0x54, 0x41, 0x55, 0x41, 0x56, 0x41, 0x57, 0x48, 0x83, 0xEC, 0x40, 0x44, 0x8B,
    0xAC, 0x24, 0xA0, 0x00, 0x00, 0x00,
};
constexpr std::uint64_t QualityPrologueRva = 0x4421B0;

// 442378..442385 (13 bytes)
constexpr std::uint8_t QualityOwnerCall[]{
    0x48, 0x8B, 0xCB, 0xE8, 0x40, 0x30, 0x06, 0x00, 0x48, 0x85, 0xC0, 0x74, 0x1A,
};
constexpr std::uint64_t QualityOwnerCallRva = 0x442378;

// 442404..442453 (79 bytes)
constexpr std::uint8_t UniqueWindow[]{
    0x3B, 0x5E, 0x08, 0x4C, 0x8B, 0xAC, 0x24, 0xA8, 0x00, 0x00, 0x00, 0x0F, 0x4E, 0x5E, 0x08, 0x41,
    0x0F, 0xB7, 0x45, 0x06, 0x0F, 0xAF, 0xC3, 0x99, 0x81, 0xE2, 0xFF, 0x03, 0x00, 0x00, 0x03, 0xC2,
    0xC1, 0xF8, 0x0A, 0x2B, 0xD8, 0x85, 0xDB, 0x0F, 0x8E, 0x1A, 0x02, 0x00, 0x00, 0x48, 0x8B, 0x8C,
    0x24, 0x88, 0x00, 0x00, 0x00, 0xE8, 0xA2, 0x7D, 0xF0, 0xFF, 0x48, 0x8B, 0xC8, 0x8B, 0xD3, 0xE8,
    0xB8, 0x16, 0xD1, 0xFF, 0x3D, 0x80, 0x00, 0x00, 0x00, 0x0F, 0x82, 0xF8, 0x01, 0x00, 0x00,
};
constexpr std::uint64_t UniqueWindowRva = 0x442404;

// 4434C9..44355B (146 bytes)
constexpr std::uint8_t EtherealWindow[]{
    0xE8, 0xB2, 0x97, 0xF2, 0xFF, 0x48, 0x8B, 0xC8, 0xBA, 0x64, 0x00, 0x00, 0x00, 0xE8, 0x25, 0x06,
    0xD1, 0xFF, 0x33, 0xC9, 0x83, 0xF8, 0x05, 0x41, 0x0F, 0x42, 0xCD, 0xF7, 0x45, 0x54, 0x00, 0x00,
    0x40, 0x00, 0x41, 0x0F, 0x45, 0xCD, 0xF6, 0x85, 0xC0, 0x00, 0x00, 0x00, 0x04, 0x75, 0x04, 0x85,
    0xC9, 0x74, 0x5C, 0x48, 0x8B, 0xCF, 0xE8, 0x0C, 0x00, 0xF9, 0xFF, 0x48, 0x8B, 0xCF, 0xE8, 0x34,
    0x00, 0xF3, 0xFF, 0x85, 0xC0, 0x74, 0x48, 0x45, 0x33, 0xC0, 0x48, 0x8B, 0xCF, 0x41, 0x8D, 0x50,
    0x49, 0xE8, 0xA1, 0x13, 0xEB, 0xFF, 0x99, 0x45, 0x33, 0xC9, 0x2B, 0xC2, 0x48, 0x8B, 0xCF, 0xD1,
    0xF8, 0x41, 0x8D, 0x51, 0x49, 0x44, 0x8D, 0x40, 0x01, 0xE8, 0x09, 0x44, 0xEB, 0xFF, 0x45, 0x33,
    0xC0, 0x48, 0x8B, 0xCF, 0x41, 0x8D, 0x50, 0x49, 0xE8, 0xDA, 0x1A, 0xEB, 0xFF, 0x45, 0x33, 0xC9,
    0x44, 0x8B, 0xC0, 0x48, 0x8B, 0xCF, 0x41, 0x8D, 0x51, 0x48, 0xE8, 0xE8, 0x43, 0xEB, 0xFF, 0x48,
    0x8B, 0x0B,
};
constexpr std::uint64_t EtherealWindowRva = 0x4434C9;

// 526FE0..527048 (104 bytes)
constexpr std::uint8_t CubeUpgradeWindow[]{
    0x49, 0x63, 0xC9, 0x48, 0x8B, 0x8C, 0xCD, 0x20, 0x01, 0x00, 0x00, 0xE8, 0x20, 0x6E, 0xE2, 0xFF,
    0x48, 0x63, 0x44, 0x24, 0x54, 0x48, 0x8D, 0x95, 0x20, 0x01, 0x00, 0x00, 0x45, 0x33, 0xC9, 0x45,
    0x33, 0xC0, 0x49, 0x8B, 0xCC, 0x48, 0x8D, 0x14, 0xC2, 0xE8, 0x02, 0x7F, 0xF1, 0xFF, 0x48, 0x63,
    0x4C, 0x24, 0x54, 0x4C, 0x8D, 0x0D, 0x56, 0x4F, 0x81, 0x01, 0xBA, 0x00, 0x00, 0x40, 0x00, 0x41,
    0xB8, 0x3A, 0x03, 0x00, 0x00, 0x48, 0x8B, 0x8C, 0xCD, 0x20, 0x01, 0x00, 0x00, 0xE8, 0x9E, 0x72,
    0xE4, 0xFF, 0x85, 0xC0, 0x74, 0x12, 0x48, 0x63, 0x4C, 0x24, 0x54, 0x48, 0x8B, 0x8C, 0xCD, 0x20,
    0x01, 0x00, 0x00, 0xE8, 0xC8, 0xC4, 0xEA, 0xFF,
};
constexpr std::uint64_t CubeUpgradeWindowRva = 0x526FE0;

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

// 43FDDA..43FDED (19 bytes)
constexpr std::uint8_t CodeToClassCall[]{
    0x0F, 0xB6, 0x8F, 0x06, 0x01, 0x00, 0x00, 0x8B, 0xD0, 0xE8, 0x08, 0x41, 0xED, 0xFF, 0x8B, 0xD8,
    0x83, 0xF8, 0xFF,
};
constexpr std::uint64_t CodeToClassCallRva = 0x43FDDA;

// 43EF10..43EF26 (22 bytes)
constexpr std::uint8_t InitItemEntry[]{
    0x48, 0x89, 0x5C, 0x24, 0x10, 0x48, 0x89, 0x6C, 0x24, 0x18, 0x56, 0x57, 0x41, 0x55, 0x41, 0x56,
    0x41, 0x57, 0x48, 0x83, 0xEC, 0x30,
};
constexpr std::uint64_t InitItemEntryRva = 0x43EF10;

// 36BDB5..36BE06 (81 bytes)
constexpr std::uint8_t RequirementWindow[]{
    0x44, 0x8B, 0xEE, 0x41, 0x0F, 0xB7, 0x86, 0x1A, 0x01, 0x00, 0x00, 0x41, 0xB9, 0x06, 0x00, 0x00,
    0x00, 0x89, 0x44, 0x24, 0x20, 0x45, 0x33, 0xC0, 0x41, 0x0F, 0xB7, 0x86, 0x1C, 0x01, 0x00, 0x00,
    0x48, 0x8B, 0xD7, 0x48, 0x8B, 0xCD, 0x89, 0x44, 0x24, 0x24, 0x89, 0x74, 0x24, 0x30, 0x44, 0x8B,
    0xFE, 0xE8, 0x05, 0x17, 0xFD, 0xFF, 0x45, 0x33, 0xC0, 0x48, 0x8B, 0xCF, 0x44, 0x8B, 0xF0, 0x41,
    0x8D, 0x50, 0x5B, 0xE8, 0x63, 0x9E, 0xF8, 0xFF, 0x44, 0x03, 0xF0, 0x0F, 0x84, 0xFB, 0x00, 0x00,
    0x00,
};
constexpr std::uint64_t RequirementWindowRva = 0x36BDB5;

// 371FA0..371FD9 (57 bytes)
constexpr std::uint8_t RequirementHelper[]{
    0x48, 0x89, 0x5C, 0x24, 0x08, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x41, 0xB9, 0x06, 0x00, 0x00, 0x00,
    0x45, 0x33, 0xC0, 0x48, 0x8B, 0xFA, 0xE8, 0x35, 0xB5, 0xFC, 0xFF, 0x45, 0x33, 0xC0, 0x48, 0x8B,
    0xCF, 0x8B, 0xD8, 0x41, 0x8D, 0x50, 0x5B, 0xE8, 0x94, 0x3C, 0xF8, 0xFF, 0x03, 0xC3, 0x48, 0x8B,
    0x5C, 0x24, 0x30, 0x48, 0x83, 0xC4, 0x20, 0x5F, 0xC3,
};
constexpr std::uint64_t RequirementHelperRva = 0x371FA0;

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

// DATATBLS_GetItemStatCostRecord (context, stat): records at tables +1258h,
// count +1260h, stride 144h (imul at 2D97AB)
// 2D9730..2D97C7 (151 bytes)
constexpr std::uint8_t IscRecordBody[]{
    0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x74, 0x24, 0x20, 0x57, 0x48, 0x83, 0xEC, 0x30, 0x48,
    0x63, 0xF2, 0xE8, 0x49, 0x73, 0x02, 0x00, 0x48, 0x8B, 0xF8, 0x48, 0x8B, 0xDE, 0x85, 0xF6, 0x78,
    0x09, 0x48, 0x3B, 0x98, 0x60, 0x12, 0x00, 0x00, 0x72, 0x18, 0x48, 0x8D, 0x4C, 0x24, 0x48, 0xC6,
    0x44, 0x24, 0x48, 0x00, 0xE8, 0x57, 0x7A, 0xE5, 0xFF, 0x84, 0xC0, 0x74, 0x01, 0xCC, 0x85, 0xF6,
    0x78, 0x55, 0x48, 0x3B, 0x9F, 0x60, 0x12, 0x00, 0x00, 0x73, 0x4C, 0x48, 0x81, 0xC7, 0x58, 0x12,
    0x00, 0x00, 0x48, 0x89, 0x5C, 0x24, 0x50, 0x48, 0x3B, 0x5F, 0x08, 0x72, 0x1E, 0x48, 0x8D, 0x44,
    0x24, 0x50, 0x48, 0x89, 0x7C, 0x24, 0x28, 0x48, 0x8D, 0x4C, 0x24, 0x20, 0x48, 0x89, 0x44, 0x24,
    0x20, 0xE8, 0xBA, 0x76, 0xE5, 0xFF, 0x84, 0xC0, 0x74, 0x01, 0xCC, 0x48, 0x69, 0x44, 0x24, 0x50,
    0x44, 0x01, 0x00, 0x00, 0x48, 0x03, 0x07, 0x48, 0x8B, 0x5C, 0x24, 0x40, 0x48, 0x8B, 0x74, 0x24,
    0x58, 0x48, 0x83, 0xC4, 0x30, 0x5F, 0xC3,
};
constexpr std::uint64_t IscRecordBodyRva = 0x2D9730;

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

// GenerateMagicAffixes 0x442C60: 58AE00 (item, 1, must pick, apply, prefix, forced) for the prefix
// and the suffix, stored by 36DB40 / 36DBE0 (item, id, slot)
// 442C9A..442D3A (160 bytes)
constexpr std::uint8_t MagicAffixWindow[]{
    0x48, 0x8B, 0x09, 0x85, 0xC0, 0x45, 0x8B, 0xC7, 0x89, 0x44, 0x24, 0x28, 0x41, 0x0F, 0x9F, 0xC0,
    0x89, 0x5C, 0x24, 0x20, 0x44, 0x8B, 0xCB, 0x8B, 0xD3, 0xE8, 0x48, 0x81, 0x14, 0x00, 0x48, 0x8B,
    0x0F, 0x45, 0x33, 0xC0, 0x8B, 0xD0, 0x90, 0x44, 0x8B, 0xF0, 0x90, 0xE8, 0x76, 0xAE, 0xF2, 0xFF,
    0x45, 0x85, 0xF6, 0x90, 0x75, 0x04, 0x8B, 0xEB, 0xEB, 0x08, 0x39, 0xAE, 0xB4, 0x00, 0x00, 0x00,
    0x7C, 0x44, 0x8B, 0x86, 0xB4, 0x00, 0x00, 0x00, 0x44, 0x8B, 0xCB, 0x48, 0x8B, 0x0F, 0x85, 0xC0,
    0x89, 0x44, 0x24, 0x28, 0x8B, 0xD3, 0x0F, 0x4F, 0xEB, 0x44, 0x89, 0x7C, 0x24, 0x20, 0x44, 0x8B,
    0xC5, 0xE8, 0x00, 0x81, 0x14, 0x00, 0x48, 0x8B, 0x0F, 0x45, 0x33, 0xC0, 0x8B, 0xD0, 0x90, 0x8B,
    0xE8, 0x90, 0xE8, 0xCF, 0xAE, 0xF2, 0xFF, 0x45, 0x85, 0xF6, 0x90, 0x75, 0x09, 0x85, 0xED, 0x90,
    0x75, 0x04, 0x33, 0xC0, 0xEB, 0x1C, 0x48, 0x8B, 0x0F, 0x45, 0x33, 0xC0, 0x41, 0x8D, 0x50, 0x10,
    0xE8, 0xC1, 0xAB, 0xF2, 0xFF, 0x48, 0x8B, 0x0F, 0x48, 0x8B, 0xD6, 0xE8, 0x16, 0x73, 0x14, 0x00,
};
constexpr std::uint64_t MagicAffixWindowRva = 0x442C9A;

// the expansion rare generator applying its affixes: 36CD50 / 36CDE0 (item, slot) -> id,
// 314250 (context, id) -> record, 3D3670 (0, 0, item, record, 0, 0)
// 58BE29..58BE94 (107 bytes)
constexpr std::uint8_t RareApplyWindow[]{
    0x8B, 0xDD, 0x0F, 0x1F, 0x44, 0x00, 0x00, 0x8B, 0xD3, 0x48, 0x8B, 0xCF, 0xE8, 0x16, 0x0F, 0xDE,
    0xFF, 0x8B, 0xD0, 0x90, 0x40, 0x0F, 0xB6, 0xCE, 0xE8, 0x0A, 0x84, 0xD8, 0xFF, 0x48, 0x85, 0xC0,
    0x74, 0x17, 0x89, 0x6C, 0x24, 0x28, 0x4C, 0x8B, 0xC8, 0x4C, 0x8B, 0xC7, 0x89, 0x6C, 0x24, 0x20,
    0x33, 0xD2, 0x33, 0xC9, 0xE8, 0x0E, 0x78, 0xE4, 0xFF, 0x8B, 0xD3, 0x48, 0x8B, 0xCF, 0xE8, 0x74,
    0x0F, 0xDE, 0xFF, 0x8B, 0xD0, 0x90, 0x40, 0x0F, 0xB6, 0xCE, 0xE8, 0xD8, 0x83, 0xD8, 0xFF, 0x48,
    0x85, 0xC0, 0x74, 0x17, 0x89, 0x6C, 0x24, 0x28, 0x4C, 0x8B, 0xC8, 0x4C, 0x8B, 0xC7, 0x89, 0x6C,
    0x24, 0x20, 0x33, 0xD2, 0x33, 0xC9, 0xE8, 0xDC, 0x77, 0xE4, 0xFF,
};
constexpr std::uint64_t RareApplyWindowRva = 0x58BE29;

// ITEMS_GetItemQuality (item)
// 36CF60..36CFA0 (64 bytes)
constexpr std::uint8_t ItemQualityEntry[]{
    0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B, 0xD9, 0x48, 0x85, 0xC9, 0x74, 0x0A, 0xE8, 0x5D,
    0xEA, 0xFD, 0xFF, 0x83, 0xF8, 0x04, 0x74, 0x19, 0x48, 0x8D, 0x4C, 0x24, 0x30, 0xC6, 0x44, 0x24,
    0x30, 0x00, 0xE8, 0x09, 0xA6, 0xFF, 0xFF, 0x84, 0xC0, 0x74, 0x01, 0xCC, 0x48, 0x85, 0xDB, 0x74,
    0x2D, 0x48, 0x8B, 0xCB, 0xE8, 0x37, 0xEA, 0xFD, 0xFF, 0x83, 0xF8, 0x04, 0x75, 0x20, 0x48, 0x8B,
};
constexpr std::uint64_t ItemQualityEntryRva = 0x36CF60;

// the gamble purchase in 0x53D850 (game, player, vendor, ...): 541AB0 (game, vendor, class, item level,
// seed), the player at [rsp+58h]
// 53EB22..53EB45 (35 bytes)
constexpr std::uint8_t GambleWindowA[]{
    0x48, 0x8B, 0x5C, 0x24, 0x68, 0x44, 0x3B, 0xF0, 0x45, 0x8B, 0x04, 0x24, 0x48, 0x8B, 0xD3, 0x41,
    0x0F, 0x4D, 0xC6, 0x48, 0x89, 0x7C, 0x24, 0x20, 0x44, 0x8B, 0xC8, 0x49, 0x8B, 0xCD, 0xE8, 0x6B,
    0x2F, 0x00, 0x00,
};
constexpr std::uint64_t GambleWindowARva = 0x53EB22;

// the second gamble purchase call (an upgraded base)
// 53EBC9..53EBE4 (27 bytes)
constexpr std::uint8_t GambleWindowB[]{
    0x44, 0x8B, 0xCB, 0x48, 0x89, 0x7C, 0x24, 0x20, 0x48, 0x8B, 0x5C, 0x24, 0x68, 0x44, 0x8B, 0xC0,
    0x48, 0x8B, 0xD3, 0x49, 0x8B, 0xCD, 0xE8, 0xCC, 0x2E, 0x00, 0x00,
};
constexpr std::uint64_t GambleWindowBRva = 0x53EBC9;

// 0x53D850 homing its arguments: [rsp+68h] = vendor (r8), [rsp+58h] = player (rdx)
// 53D8AB..53D8B8 (13 bytes)
constexpr std::uint8_t GambleFrame[]{
    0x89, 0x5C, 0x24, 0x68, 0x48, 0x89, 0x4D, 0xA0, 0x48, 0x89, 0x54, 0x24, 0x58,
};
constexpr std::uint64_t GambleFrameRva = 0x53D8AB;

// the melee hit's durability step 0x4526C0: the attacker's weapon (rax from 0x4242B0)
// goes to 0x441B10 (game r13, attacker rbx, weapon)
// 4526FE..452711 (19 bytes)
constexpr std::uint8_t CombatWindowA[]{
    0x48, 0x85, 0xC0, 0x74, 0x0E, 0x4C, 0x8B, 0xC0, 0x48, 0x8B, 0xD3, 0x49, 0x8B, 0xCD, 0xE8, 0xFF,
    0xF3, 0xFE, 0xFF,
};
constexpr std::uint64_t CombatWindowARva = 0x4526FE;

// the same step: the defender's randomly picked armor piece (r8) goes to 0x441B10
// (game r13, defender r12, item)
// 4528C7..4528D2 (11 bytes)
constexpr std::uint8_t CombatWindowB[]{
    0x49, 0x8B, 0xD4, 0x49, 0x8B, 0xCD, 0xE8, 0x3E, 0xF2, 0xFE, 0xFF,
};
constexpr std::uint64_t CombatWindowBRva = 0x4528C7;

// a skill's own durability loss: 0x5590C0 (game rsi, unit rdi, item r13, chance ebx,
// amount [rsp+20h])
// 55880B..558820 (21 bytes)
constexpr std::uint8_t SkillDurabilityWindow[]{
    0x44, 0x8B, 0xCB, 0x89, 0x44, 0x24, 0x20, 0x4D, 0x8B, 0xC5, 0x48, 0x8B, 0xD7, 0x48, 0x8B, 0xCE,
    0xE8, 0xA0, 0x08, 0x00, 0x00,
};
constexpr std::uint64_t SkillDurabilityWindowRva = 0x55880B;

// the combat durability loss 0x441B10 (game, unit, item): 4% weapon, 10% armor
// 441B10..441B24 (20 bytes)
constexpr std::uint8_t CombatDurabilityEntry[]{
    0x48, 0x89, 0x6C, 0x24, 0x10, 0x56, 0x57, 0x41, 0x54, 0x41, 0x56, 0x41, 0x57, 0x48, 0x83, 0xEC,
    0x30, 0x4C, 0x8B, 0xF2,
};
constexpr std::uint64_t CombatDurabilityEntryRva = 0x441B10;

// the skill durability loss 0x5590C0 (game, unit, item, chance, amount)
// 5590C0..5590D4 (20 bytes)
constexpr std::uint8_t SkillDurabilityEntry[]{
    0x48, 0x89, 0x5C, 0x24, 0x10, 0x48, 0x89, 0x6C, 0x24, 0x18, 0x48, 0x89, 0x74, 0x24, 0x20, 0x57,
    0x48, 0x83, 0xEC, 0x40,
};
constexpr std::uint64_t SkillDurabilityEntryRva = 0x5590C0;

// UNITS_GetMeleeRange 0x34A620 (unit): whole instructions, none relative
// 34A620..34A62E (14 bytes)
constexpr std::uint8_t MeleeRangeEntry[]{
    0x40, 0x55, 0x56, 0x57, 0x48, 0x83, 0xEC, 0x50, 0x48, 0x8B, 0xF9, 0x48, 0x85, 0xC9,
};
constexpr std::uint64_t MeleeRangeEntryRva = 0x34A620;

// UNITS_GetMeleeRange's weapon: INVENTORY_GetEquippedWeapon 0x385D20 (inventory,
// &item, &body location, &flag), then the item's rangeadder 0x371F10 (items.txt +115h)
// 34A76C..34A7B4 (72 bytes)
constexpr std::uint8_t MeleeRangeWeaponWindow[]{
    0x4C, 0x8D, 0x8C, 0x24, 0x80, 0x00, 0x00, 0x00, 0x49, 0x8B, 0xCE, 0x4C, 0x8D, 0x84, 0x24, 0x88,
    0x00, 0x00, 0x00, 0x48, 0x8D, 0x54, 0x24, 0x30, 0xE8, 0x97, 0xB5, 0x03, 0x00, 0x4C, 0x8B, 0x74,
    0x24, 0x40, 0x48, 0x8B, 0x5C, 0x24, 0x48, 0x85, 0xC0, 0x75, 0x13, 0x33, 0xC0, 0x48, 0x83, 0xC4,
    0x50, 0x5F, 0x5E, 0x5D, 0xC3, 0x4C, 0x8B, 0xB7, 0x90, 0x00, 0x00, 0x00, 0xEB, 0xC2, 0x48, 0x8B,
    0x4C, 0x24, 0x30, 0xE8, 0x5C, 0x77, 0x02, 0x00,
};
constexpr std::uint64_t MeleeRangeWeaponWindowRva = 0x34A76C;

// INVENTORY_GetEquippedWeapon 0x385D20: the right hand (4) if it holds a weapon, else
// the left hand (5)
// 385D20..385D34 (20 bytes)
constexpr std::uint8_t EquippedWeaponEntry[]{
    0x48, 0x89, 0x5C, 0x24, 0x20, 0x56, 0x41, 0x56, 0x41, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x4D, 0x8B,
    0xF1, 0x4D, 0x8B, 0xF8,
};
constexpr std::uint64_t EquippedWeaponEntryRva = 0x385D20;


constexpr std::uint64_t SpawnCallRva       = 0x4410C7;  // call 444680
constexpr std::uint64_t UniqueSiteRva      = 0x442429;  // test ebx,ebx / jle 44264B
constexpr std::uint64_t UniqueContinueRva  = 0x442431;
constexpr std::uint64_t UniqueHitRva       = 0x44264B;
constexpr std::uint64_t EtherealSiteRva    = 0x4434DB;  // xor ecx,ecx / cmp eax,5
constexpr std::uint64_t RequirementCallRva = 0x36BDE6;  // call 33D4F0 in the requirement check
constexpr std::uint64_t RequirementHelperCallRva = 0x371FB6;  // call 33D4F0 in the percent helper
constexpr std::uint64_t MasteryRva         = 0x33D4F0;  // thunk to D2RCore ReadWideWeaponMastery
constexpr std::uint64_t UnitStatValueRva   = 0x2F5C60;  // (unit, stat, layer), the percent's stat 91 read

constexpr std::uint64_t SpawnHelperRva     = 0x444680;
constexpr std::uint64_t InitItemRva        = 0x43EF10;
constexpr std::uint64_t GetOwnerRva        = 0x4A53C0;
constexpr std::uint64_t ReadOwnStatRva     = 0x2F48C0;  // base stat (unit, stat, layer)
constexpr std::uint64_t ReadUnitStatRva    = 0x2F5020;  // total stat (unit, stat, layer)
constexpr std::uint64_t SetUnitStatRva     = 0x2F7940;  // (unit, stat, value, layer)
constexpr std::uint64_t GetDataContextRva  = 0x34A0E0;
constexpr std::uint64_t GetDataTablesRva   = 0x300A90;
constexpr std::uint64_t ItemsRecordRva     = 0x314110;
constexpr std::uint64_t TypeRowRva         = 0x367220;
constexpr std::uint64_t CheckItemTypeRva   = 0x373890;
constexpr std::uint64_t CodeToClassRva     = 0x313EF0;
constexpr std::uint64_t ItemSeedRva        = 0x36CC80;
constexpr std::uint64_t RollRandomRva      = 0x153B00;
constexpr std::uint64_t SetClassIdRva      = 0x34DE10;
constexpr std::uint64_t CheckItemFlagRva   = 0x36E2D0;
constexpr std::uint64_t ApplyEtherealRva   = 0x3D3510;
constexpr std::uint64_t HasDurabilityRva   = 0x373540;
constexpr std::uint64_t CombatDurabilityRva = 0x441B10;  // (game, unit, item)
constexpr std::uint64_t CombatCallARva      = 0x45270C;  // call 441B10: the attacker's weapon
constexpr std::uint64_t CombatCallBRva      = 0x4528CD;  // call 441B10: the defender's armor piece
constexpr std::uint64_t SkillDurabilityRva  = 0x5590C0;  // (game, unit, item, chance, amount)
constexpr std::uint64_t SkillCallRva        = 0x55881B;  // call 5590C0
constexpr std::uint64_t MeleeRangeRva       = 0x34A620;  // UNITS_GetMeleeRange (unit)
constexpr std::uint64_t EquippedWeaponRva   = 0x385D20;  // (inventory, &item, &body location, &flag)
constexpr std::uint8_t  CombatCallAStock[]{ 0xE8, 0xFF, 0xF3, 0xFE, 0xFF };
constexpr std::uint8_t  CombatCallBStock[]{ 0xE8, 0x3E, 0xF2, 0xFE, 0xFF };
constexpr std::uint8_t  SkillCallStock[]{ 0xE8, 0xA0, 0x08, 0x00, 0x00 };
constexpr std::size_t   ItemStackable       = 0x142;     // items.txt stackable
constexpr std::int32_t  EtherealRoutinePct  = 150;       // 0x3D3510: base = 3 * base / 2

constexpr std::uint8_t SpawnCallStock[]{ 0xE8, 0xB4, 0x35, 0x00, 0x00 };
constexpr std::uint8_t UniqueSiteStock[]{ 0x85, 0xDB, 0x0F, 0x8E, 0x1A, 0x02, 0x00, 0x00 };
constexpr std::uint8_t EtherealSiteStock[]{ 0x33, 0xC9, 0x83, 0xF8, 0x05 };
constexpr std::uint8_t RequirementCallStock[]{ 0xE8, 0x05, 0x17, 0xFD, 0xFF };
constexpr std::uint8_t RequirementHelperCallStock[]{ 0xE8, 0x35, 0xB5, 0xFC, 0xFF };

constexpr std::uint64_t GambleCallARva      = 0x53EB40;  // call 541AB0 in the gamble purchase
constexpr std::uint64_t GambleCallBRva      = 0x53EBDF;
constexpr std::uint64_t GambleItemRva       = 0x541AB0;  // (game, vendor, class, item level, seed)
constexpr std::uint8_t  GambleCallAStock[]{ 0xE8, 0x6B, 0x2F, 0x00, 0x00 };
constexpr std::uint8_t  GambleCallBStock[]{ 0xE8, 0xCC, 0x2E, 0x00, 0x00 };
constexpr std::uint64_t SelectAffixRva      = 0x58AE00;  // (item, 1, must pick, apply, prefix, forced id)
constexpr std::uint64_t GetPrefixRva        = 0x36CD50;  // (item, slot) -> affix id
constexpr std::uint64_t GetSuffixRva        = 0x36CDE0;
constexpr std::uint64_t SetPrefixRva        = 0x36DB40;  // (item, id, slot)
constexpr std::uint64_t SetSuffixRva        = 0x36DBE0;
constexpr std::uint64_t ItemQualityRva      = 0x36CF60;  // (item)
constexpr std::size_t   RequestUnit         = 0x00;      // the build request's unit
constexpr std::uint32_t QualityMagic        = 4;
constexpr std::uint32_t QualityRare         = 6;
constexpr std::uint32_t QualityCrafted      = 8;
constexpr std::int32_t  AffixSlots          = 3;

constexpr std::uint64_t AllocStatListRva    = 0x2F7300;  // (flags, end frame, owner type, owner id)
constexpr std::uint64_t AttachStatListRva   = 0x2F2CB0;  // loader thunk, (unit, list, update)
constexpr std::uint64_t SetListStatRva      = 0x2F7C00;  // loader thunk, (context, list, stat, value, layer)
constexpr std::uint64_t FindStatListRva     = 0x2F5B80;  // (unit, state, flag), called in the window at 43F40F
constexpr std::uint64_t UnlinkStatListRva   = 0x2F7920;  // (unit, list)
constexpr std::uint64_t FreeStatListRva     = 0x2F4180;  // (context, list)
constexpr std::uint64_t InventoryRva        = 0x34A360;  // (unit, file, line)
constexpr std::uint64_t BodyLocItemRva      = 0x3886D0;  // (inventory, body location)
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
constexpr std::size_t   UnitId              = 0x08;
constexpr std::size_t   UnitStatListEx      = 0x88;
constexpr std::size_t   ListFlags           = 0x1C;
constexpr std::size_t   ExChildren          = 0x90;
constexpr std::size_t   ListNext            = 0x68;
constexpr std::size_t   ExFullStats         = 0xA8;
constexpr std::size_t   ExFullStatsCount    = 0xB0;
constexpr std::size_t   FullStatEntrySize   = 0x10;
constexpr std::size_t   FullStatLayer       = 0x00;
constexpr std::size_t   FullStatStat        = 0x04;
constexpr std::size_t   FullStatValue       = 0x08;
constexpr std::size_t   ListStats           = 0x30;    // a plain list's own stats (the pinned VisitWideUnitStats' +30h case)
constexpr std::size_t   ListStatsCount      = 0x38;
// D2RCore!VisitWideUnitStats (1.3.1), the 78 bytes that read the full stats
// array: ex = unit +88h; extended (+1Ch < 0) -> array +A8h, count +B0h;
// entries of 10h bytes.
constexpr std::uint8_t VisitWideUnitStatsBytes[]{
    0x41, 0x57, 0x41, 0x56, 0x56, 0x57, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x85, 0xD2, 0x74, 0x7A,
    0x48, 0x89, 0xD6, 0x48, 0x85, 0xC9, 0x74, 0x5D, 0x48, 0x8B, 0xB9, 0x88, 0x00, 0x00, 0x00, 0x48,
    0x85, 0xFF, 0x74, 0x51, 0x83, 0x7F, 0x1C, 0x00, 0xB8, 0xA8, 0x00, 0x00, 0x00, 0xBB, 0x30, 0x00,
    0x00, 0x00, 0x48, 0x0F, 0x48, 0xD8, 0x4C, 0x8B, 0x74, 0x1F, 0x08, 0x4D, 0x85, 0xF6, 0x74, 0x35,
    0x45, 0x31, 0xFF, 0x66, 0x66, 0x66, 0x66, 0x2E, 0x0F, 0x1F, 0x84, 0x00, 0x00, 0x00,
};
constexpr std::uint32_t BonusListFlags      = 0x10000;
constexpr std::int32_t  FirstBodyLoc        = 1;
constexpr std::int32_t  LastBodyLoc         = 12;
constexpr std::uint64_t ServerPassUpdates   = 4;
constexpr std::uint64_t ClientPassMs        = 100;
constexpr std::int32_t  StatArmorPercent    = 16;
constexpr std::int32_t  StatMaxDamagePct    = 17;
constexpr std::int32_t  StatMinDamagePct    = 18;
constexpr std::int32_t  StatArmorClass      = 31;
constexpr std::int32_t  StatDurabilityNow   = 72;
constexpr std::int32_t  StatDurabilityMax   = 73;

// Data layout (read by the functions pinned above)
constexpr std::size_t   TablesItemTypes      = 0x1348;  // itemtypes rows
constexpr std::size_t   TablesItemTypeCount  = 0x1350;
constexpr std::size_t   TablesTypeRowSize    = 0x1360;  // dwords per equivalence row
constexpr std::size_t   TablesTypeRows       = 0x1368;  // equivalence vector
constexpr std::size_t   TablesStatCount      = 0x1260;  // itemstatcost rows
constexpr std::size_t   TablesIscRecords     = 0x1258;  // itemstatcost records (0x2D9730)
constexpr std::size_t   IscRecordSize        = 0x144;
constexpr std::size_t   IscEncode            = 0x2C;    // word: 2 skill on event, 3 charges
constexpr std::size_t   IscDgrp              = 0x3A;    // word: tooltip group
constexpr std::uint32_t ModifierListFlag     = 0x40;    // the item's saved modifier list
constexpr std::size_t   ItemTypeStride       = 0xE8;
constexpr std::size_t   ItemCode             = 0x80;
constexpr std::size_t   ItemNormCode         = 0x84;
constexpr std::size_t   ItemUberCode         = 0x88;
constexpr std::size_t   ItemUltraCode        = 0x8C;
constexpr std::size_t   ItemVersion          = 0xFE;
constexpr std::size_t   ItemType1            = 0x12E;
constexpr std::size_t   ItemType2            = 0x130;
constexpr std::size_t   UnitClassId          = 0x04;
constexpr std::uint32_t BlankCode            = 0x20202020;
constexpr std::uint32_t EtherealFlag         = 0x400000;
constexpr std::int32_t  StatDurability       = 72;
constexpr std::int32_t  StatMaxDurability    = 73;
constexpr std::uint32_t VanillaEtherealPct   = 5;
constexpr std::uint32_t UnitPlayer           = 0;
constexpr std::uint32_t UnitMonster          = 1;
constexpr std::uint32_t UnitItem             = 4;
constexpr std::int32_t  StatItemReqPercent   = 91;

// ---------------------------------------------------------------------------
// Relays (assembled with keystone, decoded back with capstone; every rip
// relative operand lands on its own qword slot after the code)
// ---------------------------------------------------------------------------
// Relay 1, entered by the call at 4410C7 in place of 0x444680. rcx, rdx, r8
// and r9 pass through; the two stack arguments are copied and the killer
// [rbp-80h] of the drop roll's frame is added as a seventh.
//   mov rax,[rbp-80h] / sub rsp,48h / mov r10,[rsp+70h] / mov [rsp+20h],r10
//   mov r10,[rsp+78h] / mov [rsp+28h],r10 / mov [rsp+30h],rax
//   call [slot SpawnWithKiller] / add rsp,48h / ret
constexpr std::uint8_t SpawnRelay[]{
    0x48, 0x8B, 0x45, 0x80, 0x48, 0x83, 0xEC, 0x48, 0x4C, 0x8B, 0x54, 0x24, 0x70, 0x4C, 0x89, 0x54,
    0x24, 0x20, 0x4C, 0x8B, 0x54, 0x24, 0x78, 0x4C, 0x89, 0x54, 0x24, 0x28, 0x48, 0x89, 0x44, 0x24,
    0x30, 0xFF, 0x15, 0x0B, 0x00, 0x00, 0x00, 0x48, 0x83, 0xC4, 0x48, 0xC3, 0x90, 0x90, 0x90, 0x90,
    0x90, 0x90, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};
constexpr std::size_t SpawnRelayOffset = 0x000;
constexpr std::size_t SpawnRelaySlot = 50;

// Relay 2, entered by the jump at 442429 with ebx = the unique range.
//   sub rsp,20h / mov ecx,ebx / mov rdx,[rsp+0B0h] (killer) / mov r8d,[rsp+0C0h] (class)
//   call [slot AdjustUniqueRange] / add rsp,20h / mov ebx,eax / test ebx,ebx
//   jle hit / jmp [slot 442431] / hit: jmp [slot 44264B]
constexpr std::uint8_t UniqueRelay[]{
    0x48, 0x83, 0xEC, 0x20, 0x89, 0xD9, 0x48, 0x8B, 0x94, 0x24, 0xB0, 0x00, 0x00, 0x00, 0x44, 0x8B,
    0x84, 0x24, 0xC0, 0x00, 0x00, 0x00, 0xFF, 0x15, 0x16, 0x00, 0x00, 0x00, 0x48, 0x83, 0xC4, 0x20,
    0x89, 0xC3, 0x85, 0xDB, 0x7E, 0x06, 0xFF, 0x25, 0x0E, 0x00, 0x00, 0x00, 0xFF, 0x25, 0x10, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};
constexpr std::size_t UniqueRelayOffset = 0x080;
constexpr std::size_t UniqueRelaySlotAdjust = 50;
constexpr std::size_t UniqueRelaySlotContinue = 58;
constexpr std::size_t UniqueRelaySlotHit = 66;

// Relay 3, entered by the call at 4434DB with eax = rand(100), rdi = the item
// and rbp = the drop request.
//   push rax / sub rsp,20h / mov rcx,rdi / mov rdx,rbp / call [slot EtherealThreshold]
//   mov edx,eax / add rsp,20h / pop rax / xor ecx,ecx / cmp eax,edx / ret
constexpr std::uint8_t EtherealRelay[]{
    0x50, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x89, 0xF9, 0x48, 0x89, 0xEA, 0xFF, 0x15, 0x0C, 0x00, 0x00,
    0x00, 0x89, 0xC2, 0x48, 0x83, 0xC4, 0x20, 0x58, 0x31, 0xC9, 0x39, 0xD0, 0xC3, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00,
};
constexpr std::size_t EtherealRelayOffset = 0x100;
constexpr std::size_t EtherealRelaySlot = 29;

// Relay 5, entered by the two gamble purchase calls in place of 0x541AB0. rcx,
// rdx, r8 and r9 pass through, the seed is copied and the purchase's player
// [rsp+58h] is added as a sixth argument.
//   mov rax,[rsp+60h] / sub rsp,38h / mov r10,[rsp+60h] / mov [rsp+20h],r10
//   mov [rsp+28h],rax / call [slot GambleWithPlayer] / add rsp,38h / ret
constexpr std::uint8_t GambleRelay[]{
    0x48, 0x8B, 0x44, 0x24, 0x60, 0x48, 0x83, 0xEC, 0x38, 0x4C, 0x8B, 0x54, 0x24, 0x60, 0x4C, 0x89,
    0x54, 0x24, 0x20, 0x48, 0x89, 0x44, 0x24, 0x28, 0xFF, 0x15, 0x09, 0x00, 0x00, 0x00, 0x48, 0x83,
    0xC4, 0x38, 0xC3, 0x90, 0x90, 0x90, 0x90, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};
constexpr std::size_t GambleRelayOffset = 0x200;
constexpr std::size_t GambleRelaySlot = 39;

// Relay 4, the target of the two requirement percent calls. Their arguments
// are exactly WeightlessMastery's (unit, item, 0, 6), so it only jumps.
//   jmp [slot WeightlessMastery]
constexpr std::uint8_t MasteryRelay[]{
    0xFF, 0x25, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};
constexpr std::size_t MasteryRelayOffset = 0x180;
constexpr std::size_t MasteryRelaySlot = 6;
// Relay 6, the target of the two combat durability calls (45270C, 4528CD) and
// relay 7, the target of the skill durability call (55881B). Their arguments
// are exactly the hook's, so each only jumps.
//   jmp [slot KeepCombatDurability] / jmp [slot KeepSkillDurability]
constexpr std::uint8_t CombatDurabilityRelay[]{
    0xFF, 0x25, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};
constexpr std::uint8_t SkillDurabilityRelay[]{
    0xFF, 0x25, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};
constexpr std::size_t CombatDurabilityRelayOffset = 0x280;
constexpr std::size_t SkillDurabilityRelayOffset = 0x2C0;
constexpr std::size_t JumpRelaySlot = 6;


// ---------------------------------------------------------------------------
// Native function types
// ---------------------------------------------------------------------------
using SpawnFn         = void* (__fastcall*)(void*, void*, std::uint32_t, std::uint32_t, std::int32_t, std::uint32_t);
using InitItemFn      = std::int64_t (__fastcall*)(void*, void**, void*, std::int32_t);
using UnitFn          = void* (__fastcall*)(void*);
using ReadStatFn      = std::int32_t (__fastcall*)(void*, std::int32_t, std::int32_t);
using SetStatFn       = void (__fastcall*)(void*, std::int32_t, std::int32_t, std::int32_t);
using DataContextFn   = std::uint8_t (__fastcall*)(void*);
using DataTablesFn    = std::uint8_t* (__fastcall*)(std::uint8_t);
using ItemsRecordFn   = std::uint8_t* (__fastcall*)(std::uint8_t, std::uint32_t);
using TypeRowFn       = std::uint32_t* (__fastcall*)(void*, std::int64_t);
using CheckItemTypeFn = std::int32_t (__fastcall*)(void*, std::int32_t);
using CodeToClassFn   = std::uint32_t (__fastcall*)(std::uint8_t, std::uint32_t);
using RollRandomFn    = std::uint32_t (__fastcall*)(void*, std::uint32_t);
using SetClassIdFn    = void (__fastcall*)(void*, std::uint32_t);
using CheckFlagFn     = std::int32_t (__fastcall*)(void*, std::uint32_t, std::int32_t, const char*);
using ItemFn          = std::int32_t (__fastcall*)(void*);
using VoidItemFn      = void (__fastcall*)(void*);
using MasteryFn       = std::int32_t (__fastcall*)(void*, void*, std::int32_t, std::int32_t);
using GambleItemFn    = void* (__fastcall*)(void*, void*, std::uint32_t, std::int32_t, void*);
using SelectAffixFn   = std::uint32_t (__fastcall*)(void*, std::uint32_t, std::uint32_t, std::uint32_t, std::int32_t, std::int32_t);
using AffixGetFn      = std::uint32_t (__fastcall*)(void*, std::int32_t);
using AffixSetFn      = void (__fastcall*)(void*, std::uint32_t, std::int32_t);
using QualityFn       = std::uint32_t (__fastcall*)(void*);
using AllocListFn     = void* (__fastcall*)(std::uint32_t, std::int32_t, std::int32_t, std::uint32_t);
using AttachListFn    = void (__fastcall*)(void*, void*, std::int32_t);
using SetListStatFn   = void (__fastcall*)(std::uint8_t, void*, std::int32_t, std::int32_t, std::int32_t);
using FindStatListFn  = void* (__fastcall*)(void*, std::int32_t, std::int32_t);
using UnlinkListFn    = void (__fastcall*)(void*, void*);
using FreeListFn      = void (__fastcall*)(std::uint8_t, void*);
using InventoryFn     = void* (__fastcall*)(void*, const char*, std::int32_t);
using BodyLocItemFn   = void* (__fastcall*)(void*, std::int32_t);
using ServerUnitFn    = void* (__fastcall*)(void*, std::uint32_t, std::uint32_t);
using ClientUnitFn    = void* (__fastcall*)(std::uint32_t, std::uint32_t);
using LocalIndexFn    = std::uint32_t (__fastcall*)();
using PlayerFromIndexFn = void* (__fastcall*)(std::uint32_t);
using CombatDurabilityFn = std::int64_t (__fastcall*)(void*, void*, void*);
using SkillDurabilityFn  = std::int64_t (__fastcall*)(void*, void*, void*, std::int32_t, std::int32_t);
using MeleeRangeFn       = std::int64_t (__fastcall*)(void*);
using EquippedWeaponFn   = std::int32_t (__fastcall*)(void*, void**, std::int32_t*, std::int32_t*);

// ---------------------------------------------------------------------------
// Configuration
// ---------------------------------------------------------------------------
struct PerkConfig {
    bool                     enabled{ true };
    std::int32_t             stat{ -1 };
    std::int32_t             percent{ 0 };
    std::vector<std::string> types;
};

struct Config {
    bool       enabled{ true };
    bool       creditMinionOwner{ true };
    bool       diagnostics{ false };
    PerkConfig spectral{ true, 949, 10, { "weap" } };
    PerkConfig blessed{ true, 950, 10, { "weap", "armo" } };
    PerkConfig relic{ true, 951, 5, { "weap", "armo" } };
    PerkConfig secretPocket{ true, 966, 5, { "ring", "amul", "jewl" } };
    PerkConfig weightless{ true, 948, 25, { "weap", "armo" } };
    PerkConfig tempered{ true, 946, 10, { "weap" } };
    PerkConfig mithril{ true, 947, 10, { "armo" } };
    PerkConfig modifier{ true, 952, 25, { "weap", "armo" } };
    PerkConfig hidden{ true, 967, 25, { "ring", "amul" } };
    bool       hiddenGambling{ true };
    PerkConfig brilliant{ true, 968, 25, { "jewl" } };
    bool       brilliantGambling{ true };
    std::vector<std::string>  brilliantQualities{ "magic", "rare", "crafted", "unique", "set" };
    std::vector<std::int32_t> brilliantExclude{ 356 };
    PerkConfig durability{ true, 953, 1, { "armo" } };
    bool       useMaxDurability{ false };
    PerkConfig magnitude{ true, 954, 10, { "weap" } };
    std::vector<std::int32_t> magnitudeStats{ 0, 1, 2, 3, 7, 9, 11, 16, 17, 18, 19, 20, 21, 22, 23, 24, 27, 28, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 48, 49, 50, 51, 52, 53, 54, 55, 57, 58, 60, 62, 67, 68, 74, 76, 77, 78, 79, 80, 83, 85, 86, 87, 89, 93, 96, 97, 99, 102, 105, 107, 109, 110, 111, 112, 114, 116, 119, 120, 121, 122, 123, 124, 126, 127, 128, 134, 135, 136, 138, 141, 142, 143, 144, 145, 146, 147, 148, 149, 150, 151, 155, 156, 158, 159, 160, 182, 187, 188, 189, 190, 191, 192, 193, 195, 196, 197, 198, 200, 201, 214, 216, 217, 218, 219, 220, 221, 222, 223, 224, 225, 226, 227, 228, 229, 230, 231, 232, 233, 238, 239, 240, 242, 244, 247, 250, 255, 256, 257, 258, 265, 266, 267, 287, 288, 303, 329, 330, 331, 332, 333, 334, 335, 336, 338, 339, 340, 358, 366, 371, 372, 373, 374, 376, 377, 378, 379, 391, 392, 393, 394, 398, 400, 404, 405, 409, 413, 415, 422, 423, 426, 434, 435, 436, 466, 467, 482, 483, 486, 489, 493, 495, 496, 498, 499, 500, 501, 502, 505, 506, 516, 519, 520, 522 };
    std::vector<std::int32_t> totalDamageStats{ 466, 467, 832, 833 };
    std::vector<std::int32_t> spellDamageStats{ 329, 330, 331, 332, 482, 483 };
    std::vector<std::int32_t> elementalDamageStats{ 48, 49, 54, 55, 50, 51, 57, 58, 52, 53 };
    PerkConfig ethereal{ true, 955, 25, { "weap" } };
    std::int32_t etherealSpellPct{ 10 };
    std::vector<std::int32_t> etherealSpellStats{ 398, 489, 493, 495, 496 };
    PerkConfig unbreakable{ true, 956, 25, { "weap", "armo" } };
    std::vector<std::int32_t> unbreakableDamageStats{ 398, 1012, 1013, 489, 493, 495, 496 };
    PerkConfig reach{ true, 957, 3, { "weap" } };
    std::vector<std::string> reachExcludeTypes{ "miss" };
    std::int32_t reachProjectileStat{ 516 };
    std::int32_t reachProjectilePct{ 25 };
};

// ---------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------
const D2RL::PluginContext* Context{};
std::uintptr_t             Base{};
Config                     Settings{};
std::string                ConfigProblems;
std::string                InstallProblem;
std::uint8_t*              RelayPage{};
bool                       SpawnPatched{};
bool                       UniquePatched{};
bool                       EtherealPatched{};
bool                       InitHooked{};
bool                       RequirementPatched{};
bool                       GambleAPatched{};
bool                       GambleBPatched{};
std::array<std::uint8_t, 5> GambleAWritten{};
std::array<std::uint8_t, 5> GambleBWritten{};
bool                       RequirementHelperPatched{};
std::array<std::uint8_t, 5> SpawnWritten{};
std::array<std::uint8_t, 8> UniqueWritten{};
std::array<std::uint8_t, 5> EtherealWritten{};
std::array<std::uint8_t, 5> RequirementWritten{};
std::array<std::uint8_t, 5> RequirementHelperWritten{};
bool                       CombatAPatched{};
bool                       CombatBPatched{};
bool                       SkillPatched{};
bool                       ReachHooked{};
std::array<std::uint8_t, 5> CombatAWritten{};
std::array<std::uint8_t, 5> CombatBWritten{};
std::array<std::uint8_t, 5> SkillWritten{};

SpawnFn         OriginalSpawn{};
InitItemFn      OriginalInitItem{};
UnitFn          GetOwner{};
ReadStatFn      ReadOwnStat{};
ReadStatFn      ReadUnitStat{};
SetStatFn       SetUnitStat{};
DataContextFn   GetDataContext{};
DataTablesFn    GetDataTables{};
ItemsRecordFn   ItemsRecord{};
TypeRowFn       TypeRow{};
CheckItemTypeFn CheckItemType{};
CodeToClassFn   CodeToClass{};
UnitFn          ItemSeed{};
RollRandomFn    RollRandom{};
SetClassIdFn    SetClassId{};
CheckFlagFn     CheckItemFlag{};
VoidItemFn      ApplyEthereal{};
ItemFn          HasDurability{};
MasteryFn       OriginalMastery{};
ReadStatFn      UnitStatValue{};
GambleItemFn    OriginalGamble{};
SelectAffixFn   SelectAffix{};
AffixGetFn      GetPrefix{};
AffixGetFn      GetSuffix{};
AffixSetFn      SetPrefix{};
AffixSetFn      SetSuffix{};
QualityFn       ItemQuality{};
AllocListFn     AllocStatList{};
AttachListFn    AttachStatList{};
SetListStatFn   SetListStat{};
FindStatListFn  FindStatList{};
UnlinkListFn    UnlinkStatList{};
FreeListFn      FreeStatList{};
InventoryFn     GetInventory{};
BodyLocItemFn   BodyLocItem{};
ServerUnitFn    ServerUnit{};
ClientUnitFn    ClientUnit{};
LocalIndexFn    LocalPlayerIndex{};
PlayerFromIndexFn PlayerFromIndex{};
CombatDurabilityFn OriginalCombatDurability{};
SkillDurabilityFn  OriginalSkillDurability{};
MeleeRangeFn       OriginalMeleeRange{};
EquippedWeaponFn   EquippedWeapon{};
const D2RL::ThreadService*    ThreadSvc{};
const D2RL::LifecycleService* LifecycleSvc{};
bool                          ItemBonusReady{};

std::atomic<std::uint64_t> DropsSeen{};
std::atomic<std::uint64_t> DropsWithPerkPlayer{};
std::atomic<std::uint64_t> EtherealRaised{};
std::atomic<std::uint64_t> UpgradesExceptional{};
std::atomic<std::uint64_t> UpgradesElite{};
std::atomic<std::uint64_t> UniqueRangesRaised{};
std::atomic<std::uint64_t> PocketRangesRaised{};
std::atomic<std::uint64_t> RequirementsReduced{};
std::atomic<std::uint64_t> GambleItems{};
std::atomic<std::uint64_t> ModifiersAdded{};
std::atomic<std::uint64_t> ModifiersWithoutSlot{};
std::atomic<std::uint64_t> HiddenAdded{};
std::atomic<std::uint64_t> JewelsDoubled{};
std::atomic<std::uint64_t> BonusListsAdded{};
std::atomic<std::uint64_t> BonusListsRemoved{};
std::atomic<std::uint64_t> BonusValuesWritten{};
std::atomic<std::uint64_t> DurabilityLossesKept{};
std::atomic<std::uint32_t> ServerBonusItems{};
std::atomic<std::uint32_t> ClientBonusItems{};
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
    if (!Settings.diagnostics || DiagnosticLines.fetch_add(1) >= 200) return;
    char text[512];
    va_list args;
    va_start(args, format);
    std::vsnprintf(text, sizeof(text), format, args);
    va_end(args);
    Context->LogInfo(text);
}

// ---------------------------------------------------------------------------
// TOML subset: [section], key = integer | true | false | ["text", ...]
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
    else if (key == "chance_percent" || key == "bonus_percent" || key == "reduction_percent" || key == "percent_per_point"
             || key == "base_bonus_percent" || key == "points_per_percent" || key == "range_bonus") ok = ParseInt(value, perk.percent);
    else if (key == "item_types") ok = ParseStrings(value, perk.types);
    else ok = true;
    if (!ok) Problem(std::string(section) + "." + std::string(key) + " has an invalid value");
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
            section = std::string(Trim(line.substr(1, line.find(']') == std::string_view::npos ? 0 : line.find(']') - 1)));
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
        const std::string_view value = joined;
        if (section == "blacksmith") {
            bool ok = true;
            if (key == "enabled") ok = ParseBool(value, Settings.enabled);
            else if (key == "credit_minion_owner") ok = ParseBool(value, Settings.creditMinionOwner);
            else if (key == "diagnostics") ok = ParseBool(value, Settings.diagnostics);
            if (!ok) Problem("blacksmith." + std::string(key) + " has an invalid value");
        } else if (section == "spectral_arsenal") {
            ApplyPerkKey(Settings.spectral, section, key, value);
        } else if (section == "blessed_gear") {
            ApplyPerkKey(Settings.blessed, section, key, value);
        } else if (section == "relic_hunter") {
            ApplyPerkKey(Settings.relic, section, key, value);
        } else if (section == "the_secret_pocket") {
            ApplyPerkKey(Settings.secretPocket, section, key, value);
        } else if (section == "weightless") {
            ApplyPerkKey(Settings.weightless, section, key, value);
        } else if (section == "tempered_steel") {
            if (key == "total_damage_stats" || key == "spell_damage_stats" || key == "elemental_damage_stats") {
                std::vector<std::int32_t>& list = key == "total_damage_stats" ? Settings.totalDamageStats
                    : key == "spell_damage_stats" ? Settings.spellDamageStats : Settings.elementalDamageStats;
                if (!ParseInts(value, list)) {
                    Problem("tempered_steel." + std::string(key) + " has an invalid value");
                }
            } else {
                ApplyPerkKey(Settings.tempered, section, key, value);
            }
        } else if (section == "mithril_weaving") {
            ApplyPerkKey(Settings.mithril, section, key, value);
        } else if (section == "additional_modifier") {
            ApplyPerkKey(Settings.modifier, section, key, value);
        } else if (section == "hidden_potential") {
            if (key == "gambling") {
                if (!ParseBool(value, Settings.hiddenGambling)) Problem("hidden_potential.gambling has an invalid value");
            } else {
                ApplyPerkKey(Settings.hidden, section, key, value);
            }
        } else if (section == "brilliant_quality") {
            bool ok = true;
            if (key == "gambling") ok = ParseBool(value, Settings.brilliantGambling);
            else if (key == "qualities") ok = ParseStrings(value, Settings.brilliantQualities);
            else if (key == "exclude_stats") ok = ParseInts(value, Settings.brilliantExclude);
            else ApplyPerkKey(Settings.brilliant, section, key, value);
            if (!ok) Problem("brilliant_quality." + std::string(key) + " has an invalid value");
        } else if (section == "durability_defense") {
            if (key == "use_max_durability") {
                if (!ParseBool(value, Settings.useMaxDurability)) Problem("durability_defense.use_max_durability has an invalid value");
            } else {
                ApplyPerkKey(Settings.durability, section, key, value);
            }
        } else if (section == "modifier_magnitude") {
            if (key == "stats") {
                if (!ParseInts(value, Settings.magnitudeStats)) Problem("modifier_magnitude.stats has an invalid value");
            } else {
                ApplyPerkKey(Settings.magnitude, section, key, value);
            }
        } else if (section == "ethereal_weapons") {
            if (key == "spell_damage_percent") {
                if (!ParseInt(value, Settings.etherealSpellPct)) Problem("ethereal_weapons.spell_damage_percent has an invalid value");
            } else if (key == "spell_damage_stats") {
                if (!ParseInts(value, Settings.etherealSpellStats)) Problem("ethereal_weapons.spell_damage_stats has an invalid value");
            } else {
                ApplyPerkKey(Settings.ethereal, section, key, value);
            }
        } else if (section == "unbreakable_gear") {
            if (key == "total_damage_stats") {
                if (!ParseInts(value, Settings.unbreakableDamageStats)) Problem("unbreakable_gear.total_damage_stats has an invalid value");
            } else {
                ApplyPerkKey(Settings.unbreakable, section, key, value);
            }
        } else if (section == "extended_reach") {
            if (key == "exclude_item_types") {
                if (!ParseStrings(value, Settings.reachExcludeTypes)) Problem("extended_reach.exclude_item_types has an invalid value");
            } else if (key == "projectile_speed_stat") {
                if (!ParseInt(value, Settings.reachProjectileStat)) Problem("extended_reach.projectile_speed_stat has an invalid value");
            } else if (key == "projectile_speed_percent") {
                if (!ParseInt(value, Settings.reachProjectilePct)) Problem("extended_reach.projectile_speed_percent has an invalid value");
            } else {
                ApplyPerkKey(Settings.reach, section, key, value);
            }
        }
    }
    for (PerkConfig* perk : { &Settings.spectral, &Settings.blessed, &Settings.relic, &Settings.secretPocket, &Settings.weightless,
             &Settings.tempered, &Settings.mithril, &Settings.modifier, &Settings.hidden, &Settings.brilliant,
             &Settings.durability, &Settings.magnitude,
             &Settings.ethereal, &Settings.unbreakable, &Settings.reach }) {
        if (perk->percent < 0) perk->percent = 0;
        if (perk != &Settings.relic && perk != &Settings.secretPocket && perk != &Settings.unbreakable && perk->percent > 100) {
            perk->percent = 100;
        }
    }
}

void ReadConfiguration() {
    if (!Context->EnsureConfig(DefaultConfigToml)) {
        Context->LogWarn("Blacksmith: the config file could not be created; using the defaults.");
        return;
    }
    std::uint32_t required = 0;
    std::string buffer(65536, '\0');
    if (!Context->ReadConfig(buffer.data(), static_cast<std::uint32_t>(buffer.size()), &required)) {
        Context->LogWarn("Blacksmith: the config file could not be read; using the defaults.");
        return;
    }
    buffer.resize(std::strlen(buffer.c_str()));
    ParseConfig(buffer);
    if (!ConfigProblems.empty()) D2RL::LogWarnF(Context, "Blacksmith: config: %s.", ConfigProblems.c_str());
}

// ---------------------------------------------------------------------------
// Units, stats and item types
// ---------------------------------------------------------------------------
auto UnitType(void* unit) noexcept -> std::uint32_t {
    return unit != nullptr ? Read<std::uint32_t>(unit, 0) : 6;
}

// itemstatcost row count of a unit's data tables. Reading a stat past the
// table asserts in the game's stat reader, so every perk stat is bounded.
auto StatInTable(void* unit, std::int32_t stat) noexcept -> bool {
    if (stat < 0) return false;
    const std::uint8_t context = GetDataContext(unit);
    if (context >= 4) return false;
    const std::uint8_t* tables = GetDataTables(context);
    return tables != nullptr && static_cast<std::uint64_t>(stat) < Read<std::uint64_t>(tables, TablesStatCount);
}

auto HasPerk(void* player, const PerkConfig& perk) noexcept -> bool {
    return perk.enabled && player != nullptr && StatInTable(player, perk.stat) && ReadOwnStat(player, perk.stat, 0) > 0;
}

auto ResolvePlayer(void* killer) noexcept -> void* {
    const std::uint32_t type = UnitType(killer);
    if (type == UnitPlayer) return killer;
    if (type == UnitMonster && Settings.creditMinionOwner) {
        void* const owner = GetOwner(killer);
        if (owner != nullptr && UnitType(owner) == UnitPlayer) return owner;
    }
    return nullptr;
}

struct TypeIds {
    const std::uint8_t* tables{};
    std::vector<std::int32_t> spectral;
    std::vector<std::int32_t> blessed;
    std::vector<std::int32_t> relic;
    std::vector<std::int32_t> secretPocket;
    std::vector<std::int32_t> weightless;
    std::vector<std::int32_t> tempered;
    std::vector<std::int32_t> mithril;
    std::vector<std::int32_t> modifier;
    std::vector<std::int32_t> hidden;
    std::vector<std::int32_t> brilliant;
    std::vector<std::int32_t> durability;
    std::vector<std::int32_t> magnitude;
    std::vector<std::int32_t> ethereal;
    std::vector<std::int32_t> unbreakable;
    std::vector<std::int32_t> reach;
    std::vector<std::int32_t> reachExclude;
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
            D2RL::LogWarnF(Context, "Blacksmith: item type code \"%s\" is not in itemtypes.txt; it is ignored.", code.c_str());
        }
    }
    return ids;
}

auto Types(std::uint8_t context) -> const TypeIds* {
    if (context >= TypeCache.size()) return nullptr;
    const std::uint8_t* tables = GetDataTables(context);
    if (tables == nullptr) return nullptr;
    std::lock_guard lock(TypeMutex);
    TypeIds& cache = TypeCache[context];
    if (cache.tables != tables) {
        cache.tables   = tables;
        cache.spectral = ResolveCodes(tables, Settings.spectral.types);
        cache.blessed  = ResolveCodes(tables, Settings.blessed.types);
        cache.relic    = ResolveCodes(tables, Settings.relic.types);
        cache.secretPocket = ResolveCodes(tables, Settings.secretPocket.types);
        cache.weightless = ResolveCodes(tables, Settings.weightless.types);
        cache.tempered = ResolveCodes(tables, Settings.tempered.types);
        cache.mithril  = ResolveCodes(tables, Settings.mithril.types);
        cache.modifier = ResolveCodes(tables, Settings.modifier.types);
        cache.hidden   = ResolveCodes(tables, Settings.hidden.types);
        cache.brilliant = ResolveCodes(tables, Settings.brilliant.types);
        cache.durability = ResolveCodes(tables, Settings.durability.types);
        cache.magnitude = ResolveCodes(tables, Settings.magnitude.types);
        cache.ethereal = ResolveCodes(tables, Settings.ethereal.types);
        cache.unbreakable = ResolveCodes(tables, Settings.unbreakable.types);
        cache.reach = ResolveCodes(tables, Settings.reach.types);
        cache.reachExclude = ResolveCodes(tables, Settings.reachExcludeTypes);
    }
    return &cache;
}

auto ItemMatches(void* item, const std::vector<std::int32_t>& types) noexcept -> bool {
    for (const std::int32_t type : types) {
        if (CheckItemType(item, type) != 0) return true;
    }
    return false;
}

// The equivalence test ITEMS_CheckItemTypeId runs (3738DC..3739C4), on an
// items.txt class instead of an item unit.
auto TypeBit(std::uint8_t* tables, std::int32_t type, std::int32_t target) noexcept -> bool {
    const auto count = Read<std::uint64_t>(tables, TablesItemTypeCount);
    if (type < 0 || target < 0 || static_cast<std::uint64_t>(type) >= count || static_cast<std::uint64_t>(target) >= count) return false;
    const auto rowSize = Read<std::uint32_t>(tables, TablesTypeRowSize);
    const std::int32_t index = static_cast<std::int32_t>(static_cast<std::uint32_t>(type) * rowSize);
    const std::uint32_t* row = TypeRow(tables + TablesTypeRows, index);
    return row != nullptr && (row[static_cast<std::uint32_t>(target) >> 5] & (1U << (static_cast<std::uint32_t>(target) & 31U))) != 0;
}

auto ClassMatches(std::uint8_t context, std::uint32_t classId, const std::vector<std::int32_t>& types) noexcept -> bool {
    const std::uint8_t* record = ItemsRecord(context, classId);
    std::uint8_t* tables = GetDataTables(context);
    if (record == nullptr || tables == nullptr) return false;
    const std::int32_t type1 = Read<std::int16_t>(record, ItemType1);
    const std::int32_t type2 = Read<std::int16_t>(record, ItemType2);
    for (const std::int32_t target : types) {
        if (type1 != -1 && TypeBit(tables, type1, target)) return true;
        if (type2 > 0 && TypeBit(tables, type2, target)) return true;
    }
    return false;
}

// ---------------------------------------------------------------------------
// The drop context
// ---------------------------------------------------------------------------
// Who an item being built belongs to: a treasure-class drop credited to a
// player, or a gamble purchase.
enum class BuildSource : std::uint8_t { Drop, Gamble };

struct DropContext {
    void*       game{};
    void*       player{};
    BuildSource source{ BuildSource::Drop };
};

thread_local DropContext* CurrentDrop{};

// Entered from relay 1 in place of the call to 0x444680 at 4410C7.
auto __fastcall SpawnWithKiller(void* game, void* monster, std::uint32_t classId, std::uint32_t quality,
        std::int32_t row, std::uint32_t flags, void* killer) noexcept -> void* {
    DropContext context{ game, Settings.enabled ? ResolvePlayer(killer) : nullptr, BuildSource::Drop };
    DropsSeen.fetch_add(1, std::memory_order_relaxed);
    if (context.player != nullptr) DropsWithPerkPlayer.fetch_add(1, std::memory_order_relaxed);
    DropContext* const previous = CurrentDrop;
    CurrentDrop = &context;
    void* const item = OriginalSpawn(game, monster, classId, quality, row, flags);
    CurrentDrop = previous;
    return item;
}

// Entered from relay 5 in place of the calls to 0x541AB0 at 53EB40 and 53EBDF.
auto __fastcall GambleWithPlayer(void* game, void* vendor, std::uint32_t classId, std::int32_t itemLevel, void* seed,
        void* player) noexcept -> void* {
    DropContext context{ game, Settings.enabled && UnitType(player) == UnitPlayer ? player : nullptr, BuildSource::Gamble };
    GambleItems.fetch_add(1, std::memory_order_relaxed);
    DropContext* const previous = CurrentDrop;
    CurrentDrop = &context;
    void* const item = OriginalGamble(game, vendor, classId, itemLevel, seed);
    CurrentDrop = previous;
    return item;
}

// Entered from relay 2 at 442429 with the final unique range.
auto __fastcall AdjustUniqueRange(std::int32_t range, void* killer, std::uint32_t classId) noexcept -> std::int32_t {
    if (range <= 0 || !Settings.enabled) return range;
    const bool relic  = Settings.relic.enabled && Settings.relic.percent > 0;
    const bool pocket = Settings.secretPocket.enabled && Settings.secretPocket.percent > 0;
    if (!relic && !pocket) return range;
    void* const player = ResolvePlayer(killer);
    const bool relicPerk  = relic && HasPerk(player, Settings.relic);
    const bool pocketPerk = pocket && HasPerk(player, Settings.secretPocket);
    if (!relicPerk && !pocketPerk) return range;
    const std::uint8_t context = GetDataContext(player);
    const TypeIds* types = Types(context);
    if (types == nullptr) return range;
    std::int64_t adjusted = range;
    if (relicPerk && ClassMatches(context, classId, types->relic)) {
        adjusted = adjusted * 100 / (100 + Settings.relic.percent);
        UniqueRangesRaised.fetch_add(1, std::memory_order_relaxed);
        Diagnostic("Blacksmith: Relic Hunter, class %u, unique range %d -> %lld.", classId, range, adjusted);
    }
    if (pocketPerk && ClassMatches(context, classId, types->secretPocket)) {
        const std::int64_t before = adjusted;
        adjusted = adjusted * 100 / (100 + Settings.secretPocket.percent);
        PocketRangesRaised.fetch_add(1, std::memory_order_relaxed);
        Diagnostic("Blacksmith: The Secret Pocket, class %u, unique range %lld -> %lld.", classId, before, adjusted);
    }
    if (adjusted < 1) adjusted = 1;
    return static_cast<std::int32_t>(adjusted);
}

// Entered from relay 3 at 4434DB; returns the threshold for rand(100).
auto __fastcall EtherealThreshold(void* item, void*) noexcept -> std::uint32_t {
    DropContext* const drop = CurrentDrop;
    if (drop == nullptr || drop->source != BuildSource::Drop || drop->player == nullptr || !Settings.enabled
            || !HasPerk(drop->player, Settings.spectral)) {
        return VanillaEtherealPct;
    }
    const TypeIds* types = Types(GetDataContext(item));
    if (types == nullptr || !ItemMatches(item, types->spectral)) return VanillaEtherealPct;
    EtherealRaised.fetch_add(1, std::memory_order_relaxed);
    return static_cast<std::uint32_t>(Settings.spectral.percent);
}

void BlessedGear(void* game, void* item, void* player) noexcept {
    if (!HasPerk(player, Settings.blessed) || Settings.blessed.percent <= 0) return;
    const std::uint8_t context = GetDataContext(item);
    const TypeIds* types = Types(context);
    if (types == nullptr || !ItemMatches(item, types->blessed)) return;
    const std::uint32_t classId = Read<std::uint32_t>(item, UnitClassId);
    const std::uint8_t* record = ItemsRecord(context, classId);
    if (record == nullptr) return;
    const auto code  = Read<std::uint32_t>(record, ItemCode);
    const auto norm  = Read<std::uint32_t>(record, ItemNormCode);
    const auto uber  = Read<std::uint32_t>(record, ItemUberCode);
    const auto ultra = Read<std::uint32_t>(record, ItemUltraCode);
    const auto usable = [&](std::uint32_t candidate) noexcept { return candidate != 0 && candidate != BlankCode && candidate != code; };
    std::uint32_t target = 0;
    bool toElite = false;
    if (code == norm && usable(uber)) {
        target = uber;
    } else if (code == uber && usable(ultra)) {
        target = ultra;
        toElite = true;
    }
    if (target == 0) return;
    void* const seed = ItemSeed(item);
    if (seed == nullptr || RollRandom(seed, 100) >= static_cast<std::uint32_t>(Settings.blessed.percent)) return;
    const std::uint32_t upgraded = CodeToClass(context, target);
    if (upgraded == 0xFFFFFFFFU) return;
    const std::uint8_t* upgradedRecord = ItemsRecord(context, upgraded);
    if (upgradedRecord == nullptr) return;
    if (context == 1 && Read<std::uint16_t>(upgradedRecord, ItemVersion) >= 100) return;

    // The cube's own in-place upgrade (526FEB, 527009, 52702D, 527043).
    SetClassId(item, upgraded);
    void* reference = item;
    (void)OriginalInitItem(game, &reference, nullptr, 0);
    if (CheckItemFlag(item, EtherealFlag, __LINE__, "blacksmith") != 0) {
        ApplyEthereal(item);
        // A fresh ethereal drop's durability (4434FF..443558).
        if (HasDurability(item) != 0) {
            const std::int32_t baseMax = ReadOwnStat(item, StatMaxDurability, 0);
            SetUnitStat(item, StatMaxDurability, baseMax / 2 + 1, 0);
            SetUnitStat(item, StatDurability, ReadUnitStat(item, StatMaxDurability, 0), 0);
        }
    }
    (toElite ? UpgradesElite : UpgradesExceptional).fetch_add(1, std::memory_order_relaxed);
    Diagnostic("Blacksmith: Blessed Gear, class %u -> %u (%s).", classId, upgraded, toElite ? "elite" : "exceptional");
}

// Entered from the two requirement percent calls (36BDE6, 371FB6) in place of
// the mastery thunk 0x33D4F0; both callers add the item's stat 91 right after.
auto __fastcall WeightlessMastery(void* unit, void* item, std::int32_t a3, std::int32_t effect) noexcept -> std::int32_t {
    const std::int32_t mastery = OriginalMastery(unit, item, a3, effect);
    if (!Settings.enabled || !Settings.weightless.enabled || Settings.weightless.percent <= 0) return mastery;
    if (UnitType(unit) != UnitPlayer || UnitType(item) != UnitItem || !HasPerk(unit, Settings.weightless)) return mastery;
    const TypeIds* types = Types(GetDataContext(item));
    if (types == nullptr || !ItemMatches(item, types->weightless)) return mastery;
    const std::int64_t itemPercent = UnitStatValue(item, StatItemReqPercent, 0);
    const std::int64_t percent = static_cast<std::int64_t>(mastery) + itemPercent;
    const std::int64_t reduced = (100 + percent) * (100 - Settings.weightless.percent) / 100 - 100;
    RequirementsReduced.fetch_add(1, std::memory_order_relaxed);
    return static_cast<std::int32_t>(reduced - itemPercent);
}

// The character an item is being made for when it is not a credited drop: the
// unit at request +00h, when that is a player.
auto RequestPlayer(void* request) noexcept -> void* {
    void* const unit = Read<void*>(request, RequestUnit);
    return unit != nullptr && UnitType(unit) == UnitPlayer ? unit : nullptr;
}

auto FreeSlot(AffixGetFn get, void* item, std::int32_t slots) noexcept -> std::int32_t {
    for (std::int32_t slot = 0; slot < slots; ++slot) {
        if (get(item, slot) == 0) return slot;
    }
    return -1;
}

// One more affix through the game's own roll, for each perk entry (the
// blacksmith's Additional Modifier, the Jeweler's Hidden Potential) whose
// types the item matches.
void AdditionalModifier(void* item, void* player, const PerkConfig& perk, std::vector<std::int32_t> TypeIds::*typeField,
    std::atomic<std::uint64_t>& counter, const char* name) noexcept {
    if (perk.percent <= 0 || !HasPerk(player, perk)) return;
    const std::uint32_t quality = ItemQuality(item);
    if (quality != QualityMagic && quality != QualityRare && quality != QualityCrafted) return;
    const TypeIds* types = Types(GetDataContext(item));
    if (types == nullptr || !ItemMatches(item, types->*typeField)) return;
    void* const seed = ItemSeed(item);
    if (seed == nullptr || RollRandom(seed, 100) >= static_cast<std::uint32_t>(perk.percent)) return;

    const std::int32_t slots = quality == QualityMagic ? 1 : AffixSlots;
    const std::int32_t freePrefix = FreeSlot(GetPrefix, item, slots);
    const std::int32_t freeSuffix = FreeSlot(GetSuffix, item, slots);
    bool prefixFirst = false;
    if (freePrefix >= 0 && freeSuffix >= 0) prefixFirst = RollRandom(seed, 2) == 0;
    else if (freePrefix >= 0) prefixFirst = true;
    else if (freeSuffix < 0) prefixFirst = RollRandom(seed, 2) == 0;
    for (const bool prefix : { prefixFirst, !prefixFirst }) {
        const std::uint32_t id = SelectAffix(item, 1, 1, 1, prefix ? 1 : 0, 0);
        if (id == 0) continue;
        const std::int32_t slot = prefix ? freePrefix : freeSuffix;
        if (slot >= 0) {
            (prefix ? SetPrefix : SetSuffix)(item, id, slot);
        } else {
            ModifiersWithoutSlot.fetch_add(1, std::memory_order_relaxed);
        }
        counter.fetch_add(1, std::memory_order_relaxed);
        Diagnostic("Blacksmith: %s, quality %u, %s %u in slot %d.", name, quality, prefix ? "prefix" : "suffix", id, slot);
        return;
    }
}

// ---------------------------------------------------------------------------
// Brilliant Quality: one modifier of a new jewel doubled
// ---------------------------------------------------------------------------
auto IscRecord(std::uint8_t context, std::int32_t stat) noexcept -> const std::uint8_t* {
    const std::uint8_t* tables = context < 4 ? GetDataTables(context) : nullptr;
    if (tables == nullptr || stat < 0) return nullptr;
    const auto* records = Read<const std::uint8_t*>(tables, TablesIscRecords);
    if (records == nullptr || static_cast<std::uint64_t>(stat) >= Read<std::uint64_t>(tables, TablesStatCount)) return nullptr;
    return records + static_cast<std::size_t>(stat) * IscRecordSize;
}

auto IscWord(std::uint8_t context, std::int32_t stat, std::size_t offset) noexcept -> std::uint16_t {
    const std::uint8_t* record = IscRecord(context, stat);
    return record != nullptr ? Read<std::uint16_t>(record, offset) : 0;
}

// dgrp and encode read back the vanilla groups (attributes, resistances) and
// encodings (skill on attack 2, charges 3): checked once per data context.
// Without it no dgrp group forms and stats 195 to 205 count as encoded.
std::array<std::int8_t, 4> IscFieldsChecked{ -1, -1, -1, -1 };

auto IscFieldsValid(std::uint8_t context) noexcept -> bool {
    if (context >= IscFieldsChecked.size()) return false;
    if (IscFieldsChecked[context] >= 0) return IscFieldsChecked[context] != 0;
    const auto dgrp = [&](std::int32_t stat) noexcept { return IscWord(context, stat, IscDgrp); };
    const auto encode = [&](std::int32_t stat) noexcept { return IscWord(context, stat, IscEncode); };
    const bool ok = dgrp(0) != 0 && dgrp(0) == dgrp(1) && dgrp(0) == dgrp(2) && dgrp(0) == dgrp(3)
        && dgrp(39) != 0 && dgrp(39) == dgrp(41) && dgrp(39) == dgrp(43) && dgrp(39) == dgrp(45) && dgrp(0) != dgrp(39)
        && encode(204) == 3 && encode(195) == 2 && encode(0) == 0 && encode(39) == 0;
    IscFieldsChecked[context] = ok ? 1 : 0;
    if (!ok && Context != nullptr) {
        Context->LogWarn("Blacksmith: the itemstatcost dgrp/encode fields did not read back as expected; Brilliant Quality "
            "doubles single stats only and skips stats 195 to 205.");
    }
    return ok;
}

struct ListStat {
    std::int32_t stat;
    std::int32_t layer;
    std::int32_t value;
};

constexpr std::array<std::array<std::int16_t, 6>, 7> DamageGroups{ { { 17, 18, -1, -1, -1, -1 }, { 48, 49, -1, -1, -1, -1 },
    { 50, 51, -1, -1, -1, -1 }, { 52, 53, -1, -1, -1, -1 }, { 54, 55, -1, -1, -1, -1 }, { 57, 58, -1, -1, -1, -1 },
    { 21, 22, 23, 24, 159, 160 } } };

auto QualityId(const std::string& name) noexcept -> std::uint32_t {
    if (name == "magic") return QualityMagic;
    if (name == "set") return 5;
    if (name == "rare") return QualityRare;
    if (name == "unique") return 7;
    if (name == "crafted") return QualityCrafted;
    return 0;
}

// The modifier groups of a list's stats, as indexes into stats. Every stat
// with a value lands in exactly one group.
void ModifierGroups(std::uint8_t context, const std::vector<ListStat>& stats, std::vector<std::vector<std::size_t>>& groups) {
    groups.clear();
    std::vector<bool> used(stats.size(), false);
    const auto find = [&](std::int32_t stat) noexcept -> std::ptrdiff_t {
        for (std::size_t i = 0; i < stats.size(); ++i) {
            if (!used[i] && stats[i].stat == stat && stats[i].layer == 0) return static_cast<std::ptrdiff_t>(i);
        }
        return -1;
    };
    for (const auto& damage : DamageGroups) {
        std::vector<std::size_t> group;
        for (const std::int16_t stat : damage) {
            if (stat < 0) continue;
            const std::ptrdiff_t at = find(stat);
            if (at >= 0) group.push_back(static_cast<std::size_t>(at));
        }
        if (group.empty()) continue;
        for (const std::size_t at : group) used[at] = true;
        groups.push_back(std::move(group));
    }
    if (IscFieldsValid(context)) {
        const std::uint8_t* tables = GetDataTables(context);
        const std::uint64_t rows = tables != nullptr ? std::min<std::uint64_t>(Read<std::uint64_t>(tables, TablesStatCount), 0x8000) : 0;
        for (std::size_t i = 0; i < stats.size(); ++i) {
            if (used[i] || stats[i].layer != 0) continue;
            const std::uint16_t dgrp = IscWord(context, stats[i].stat, IscDgrp);
            if (dgrp == 0) continue;
            // Every row with this dgrp must be on the list with the same value,
            // exactly as the tooltip joins them into one line.
            std::vector<std::size_t> group;
            bool whole = true;
            for (std::uint64_t row = 0; row < rows && whole; ++row) {
                if (IscWord(context, static_cast<std::int32_t>(row), IscDgrp) != dgrp) continue;
                const std::ptrdiff_t at = find(static_cast<std::int32_t>(row));
                if (at < 0 || stats[static_cast<std::size_t>(at)].value != stats[i].value) whole = false;
                else group.push_back(static_cast<std::size_t>(at));
            }
            if (!whole || group.size() < 2) continue;
            for (const std::size_t at : group) used[at] = true;
            groups.push_back(std::move(group));
        }
    }
    for (std::size_t i = 0; i < stats.size(); ++i) {
        if (!used[i]) groups.push_back({ i });
    }
}

void BrilliantQuality(void* item, void* player) noexcept {
    if (Settings.brilliant.percent <= 0 || !HasPerk(player, Settings.brilliant) || FindStatList == nullptr) return;
    const std::uint32_t quality = ItemQuality(item);
    bool qualityOk = false;
    for (const std::string& name : Settings.brilliantQualities) qualityOk = qualityOk || QualityId(name) == quality;
    if (!qualityOk) return;
    const std::uint8_t context = GetDataContext(item);
    const TypeIds* types = Types(context);
    if (types == nullptr || !ItemMatches(item, types->brilliant)) return;
    void* const seed = ItemSeed(item);
    if (seed == nullptr || RollRandom(seed, 100) >= static_cast<std::uint32_t>(Settings.brilliant.percent)) return;
    void* const list = FindStatList(item, 0, static_cast<std::int32_t>(ModifierListFlag));
    if (list == nullptr || Read<std::int32_t>(list, ListFlags) < 0) return;

    const bool iscOk = IscFieldsValid(context);
    std::vector<ListStat> stats;
    const auto* entries = Read<const std::uint8_t*>(list, ListStats);
    const auto  count   = Read<std::uint64_t>(list, ListStatsCount);
    if (entries == nullptr || count > 4096) return;
    for (std::uint64_t i = 0; i < count; ++i) {
        const std::uint8_t* entry = entries + i * FullStatEntrySize;
        const ListStat stat{ Read<std::uint16_t>(entry, FullStatStat), static_cast<std::int32_t>(Read<std::uint32_t>(entry, FullStatLayer)),
            Read<std::int32_t>(entry, FullStatValue) };
        if (stat.value == 0) continue;
        if (std::find(Settings.brilliantExclude.begin(), Settings.brilliantExclude.end(), stat.stat) != Settings.brilliantExclude.end()) continue;
        if (iscOk) {
            const std::uint16_t encode = IscWord(context, stat.stat, IscEncode);
            if (encode == 2 || encode == 3) continue;
        } else if (stat.stat >= 195 && stat.stat <= 205) {
            continue;
        }
        stats.push_back(stat);
    }
    if (stats.empty()) return;
    std::vector<std::vector<std::size_t>> groups;
    ModifierGroups(context, stats, groups);
    if (groups.empty()) return;
    const std::uint32_t pick = groups.size() > 1 ? RollRandom(seed, static_cast<std::uint32_t>(groups.size())) : 0;
    if (pick >= groups.size()) return;
    for (const std::size_t at : groups[pick]) {
        const ListStat& stat = stats[at];
        const std::int64_t doubled = std::clamp<std::int64_t>(static_cast<std::int64_t>(stat.value) * 2, INT32_MIN, INT32_MAX);
        SetListStat(context, list, stat.stat, static_cast<std::int32_t>(doubled), stat.layer);
        Diagnostic("Blacksmith: Brilliant Quality, stat %d:%d %d -> %lld.", stat.stat, stat.layer, stat.value, doubled);
    }
    JewelsDoubled.fetch_add(1, std::memory_order_relaxed);
}

// Inline hook on 0x43EF10 (game, &item, request, full).
auto __fastcall HookInitItem(void* game, void** itemRef, void* request, std::int32_t full) noexcept -> std::int64_t {
    const std::int64_t result = OriginalInitItem(game, itemRef, request, full);
    if (static_cast<std::uint32_t>(result) != 0 && full != 0 && request != nullptr && itemRef != nullptr && *itemRef != nullptr
            && Settings.enabled) {
        DropContext* const build = CurrentDrop;
        const bool inContext = build != nullptr && build->game == game;
        void* const player = inContext ? build->player : RequestPlayer(request);
        if (player != nullptr) {
            const bool gamble = inContext && build->source == BuildSource::Gamble;
            if (Settings.modifier.enabled) {
                AdditionalModifier(*itemRef, player, Settings.modifier, &TypeIds::modifier, ModifiersAdded, "Additional Modifier");
            }
            if (Settings.hidden.enabled && (!gamble || Settings.hiddenGambling)) {
                AdditionalModifier(*itemRef, player, Settings.hidden, &TypeIds::hidden, HiddenAdded, "Hidden Potential");
            }
            if (Settings.brilliant.enabled && (!gamble || Settings.brilliantGambling)) BrilliantQuality(*itemRef, player);
        }
        if (inContext && build->source == BuildSource::Drop && build->player != nullptr && Settings.blessed.enabled) {
            BlessedGear(game, *itemRef, build->player);
        }
    }
    return result;
}

// ---------------------------------------------------------------------------
// Unbreakable Gear (durability kept) and Extended Reach (melee range)
// ---------------------------------------------------------------------------
auto KeepsDurability(void* unit) noexcept -> bool {
    return Settings.enabled && UnitType(unit) == UnitPlayer && HasPerk(unit, Settings.unbreakable);
}

// Entered through relay 6 from the melee hit's durability step: 45270C (the
// attacker's weapon) and 4528CD (the defender's armor piece).
auto __fastcall KeepCombatDurability(void* game, void* unit, void* item) noexcept -> std::int64_t {
    if (KeepsDurability(unit)) {
        DurabilityLossesKept.fetch_add(1, std::memory_order_relaxed);
        return 0;
    }
    return OriginalCombatDurability(game, unit, item);
}

auto IsStackable(void* item) noexcept -> bool {
    if (item == nullptr || UnitType(item) != UnitItem) return false;
    const std::uint8_t* record = ItemsRecord(GetDataContext(item), Read<std::uint32_t>(item, UnitClassId));
    return record != nullptr && record[ItemStackable] != 0;
}

// Entered through relay 7 from a skill's own durability loss (55881B). A
// stackable item spends quantity there instead of durability; that stays.
auto __fastcall KeepSkillDurability(void* game, void* unit, void* item, std::int32_t chance, std::int32_t amount) noexcept
    -> std::int64_t {
    if (KeepsDurability(unit) && !IsStackable(item)) {
        DurabilityLossesKept.fetch_add(1, std::memory_order_relaxed);
        return 0;
    }
    return OriginalSkillDurability(game, unit, item, chance, amount);
}

// Inline hook on UNITS_GetMeleeRange 0x34A620: the weapon it measures is
// INVENTORY_GetEquippedWeapon's, so the same lookup decides the perk.
auto __fastcall HookMeleeRange(void* unit) noexcept -> std::int64_t {
    const std::int64_t range = OriginalMeleeRange(unit);
    if (!Settings.enabled || Settings.reach.percent == 0 || UnitType(unit) != UnitPlayer || !HasPerk(unit, Settings.reach)) {
        return range;
    }
    void* const inventory = GetInventory(unit, "blacksmith", 0);
    if (inventory == nullptr) return range;
    void* weapon = nullptr;
    std::int32_t location = 0;
    std::int32_t flag = 0;
    if (EquippedWeapon(inventory, &weapon, &location, &flag) == 0 || weapon == nullptr || UnitType(weapon) != UnitItem) {
        return range;
    }
    const TypeIds* types = Types(GetDataContext(unit));
    if (types == nullptr || !ItemMatches(weapon, types->reach) || ItemMatches(weapon, types->reachExclude)) return range;
    return range + Settings.reach.percent;
}

// ---------------------------------------------------------------------------
// Tempered Steel and Mithril Weaving
// ---------------------------------------------------------------------------
enum class SlotKind : std::uint8_t { Plain, FlatMax, FlatMin };

// One slot per stat id: how a weapon's own value of it is read, which weapon
// perks raise it, and whether it is armorclass (the armor perks).
struct Slot {
    std::int32_t stat;
    SlotKind     kind;
    bool         tempered;
    bool         magnitude;
    bool         armor;
};

constexpr std::size_t   MaxSlots     = 512;
constexpr std::int32_t  MaxStatId    = 4096;
constexpr std::int32_t  ChargedSkill = 204;  // item_charged_skill: its value packs charges and maximum charges
std::array<Slot, MaxSlots>             Slots{};
std::size_t                            SlotCount{};
std::array<std::int16_t, MaxStatId>    SlotOfStat{};

auto WeaponKind(std::int32_t stat) noexcept -> SlotKind {
    switch (stat) {
    case 21: case 23: case 159: return SlotKind::FlatMin;  // mindamage, secondary_mindamage, item_throw_mindamage
    case 22: case 24: case 160: return SlotKind::FlatMax;  // maxdamage, secondary_maxdamage, item_throw_maxdamage
    default: return SlotKind::Plain;
    }
}

auto SlotFor(std::int32_t stat) noexcept -> Slot* {
    if (stat < 0 || stat >= MaxStatId || stat == ChargedSkill) return nullptr;
    if (SlotOfStat[static_cast<std::size_t>(stat)] >= 0) return &Slots[static_cast<std::size_t>(SlotOfStat[static_cast<std::size_t>(stat)])];
    if (SlotCount >= MaxSlots) return nullptr;
    Slots[SlotCount] = { stat, WeaponKind(stat), false, false, false };
    SlotOfStat[static_cast<std::size_t>(stat)] = static_cast<std::int16_t>(SlotCount);
    return &Slots[SlotCount++];
}

void BuildSlots() noexcept {
    SlotCount = 0;
    SlotOfStat.fill(-1);
    for (const std::int32_t stat : { StatMaxDamagePct, StatMinDamagePct, 21, 22, 23, 24, 159, 160, 218, 219 }) {
        if (Slot* slot = SlotFor(stat)) slot->tempered = true;
    }
    for (const auto* list : { &Settings.totalDamageStats, &Settings.spellDamageStats, &Settings.elementalDamageStats }) {
        for (const std::int32_t stat : *list) {
            if (Slot* slot = SlotFor(stat)) slot->tempered = true;
        }
    }
    for (const std::int32_t stat : Settings.magnitudeStats) {
        if (stat == ChargedSkill) {
            Context->LogWarn("Blacksmith: modifier_magnitude.stats lists 204 (item_charged_skill); its value packs charges "
                "and maximum charges, so it is not raised.");
            continue;
        }
        if (Slot* slot = SlotFor(stat)) slot->magnitude = true;
    }
    if (Slot* slot = SlotFor(StatArmorClass)) slot->armor = true;
}

auto SlotOf(std::int32_t stat) noexcept -> const Slot* {
    if (stat < 0 || stat >= MaxStatId) return nullptr;
    const std::int16_t index = SlotOfStat[static_cast<std::size_t>(stat)];
    return index >= 0 ? &Slots[static_cast<std::size_t>(index)] : nullptr;
}

// A value on the plugin's list: stat, layer (the stat's parameter: a skill,
// a class, an aura...) and value.
struct StatValue {
    std::int32_t stat;
    std::int32_t layer;
    std::int32_t value;
};

// What each perk adds to one item: percents for the weapon perks, the two
// raises of base defense for the armor perks (which multiply), Ethereal
// Weapons' extra base damage, and flat stats the item carries for its
// character (spell damage, the durability damage bonus, projectile speed).
struct Roles {
    std::int32_t temperedPct{};
    std::int32_t magnitudePct{};
    std::int32_t mithrilPct{};
    std::int32_t durabilityPct{};
    std::int32_t etherealBasePct{};
    std::vector<StatValue> extra;
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

struct BonusEntry {
    void*                  game{};   // host: the item's game; client: null
    std::uint32_t          id{};
    void*                  list{};
    std::vector<StatValue> values;
    bool                   seen{};
};

struct BonusSide {
    std::vector<BonusEntry> entries;
    std::uint64_t           generation{};
    std::uint64_t           updates{};
    std::uint64_t           lastMs{};
};

BonusSide                  ServerBonus;
BonusSide                  ClientBonus;
std::atomic<std::uint64_t> Generation{ 1 };
std::atomic<std::uint64_t> ServerChain{};
std::atomic<std::uint64_t> ClientChain{};
std::atomic<std::uint64_t> ChainCounter{};

// The plugin's list is only ever touched when it is still in one of the
// item's two child chains (statlist ex at +88h, chains at +90h and +98h, next
// at +68h; the D2RCore attach picks the chain by the list's 8h flag).
auto ListAttached(void* item, void* list) noexcept -> bool {
    if (item == nullptr || list == nullptr) return false;
    const auto* ex = Read<const std::uint8_t*>(item, UnitStatListEx);
    if (ex == nullptr || Read<std::int32_t>(ex, ListFlags) >= 0) return false;
    for (const std::size_t chain : { ExChildren, ExChildren + 8 }) {
        const auto* node = Read<const std::uint8_t*>(ex, chain);
        for (std::size_t guard = 0; node != nullptr && guard < 512; ++guard) {
            if (node == list) return true;
            node = Read<const std::uint8_t*>(node, ListNext);
        }
    }
    return false;
}

// The (stat, layer) pairs an item carries, read where D2RCore's
// VisitWideUnitStats reads them (its body is compared at load): the item's
// statlist ex (+88h, extended flag in +1Ch) keeps its full stats at +A8h with
// the count at +B0h, 16 bytes each: layer +0, stat id +4, value +8.
bool FullStatsLayout{};

auto ItemStatPairs(void* item, std::vector<StatValue>& out) noexcept -> void {
    out.clear();
    if (!FullStatsLayout) return;
    const auto* ex = Read<const std::uint8_t*>(item, UnitStatListEx);
    if (ex == nullptr || Read<std::int32_t>(ex, ListFlags) >= 0) return;
    const auto* entries = Read<const std::uint8_t*>(ex, ExFullStats);
    const auto  count   = Read<std::uint64_t>(ex, ExFullStatsCount);
    if (entries == nullptr || count > 4096) return;
    for (std::uint64_t i = 0; i < count; ++i) {
        const std::uint8_t* entry = entries + i * FullStatEntrySize;
        const std::int32_t stat = Read<std::uint16_t>(entry, FullStatStat);
        if (SlotOf(stat) == nullptr) continue;
        out.push_back({ stat, static_cast<std::int32_t>(Read<std::uint32_t>(entry, FullStatLayer)), 0 });
    }
}

auto Lookup(const std::vector<StatValue>& values, std::int32_t stat, std::int32_t layer) noexcept -> std::int32_t {
    for (const StatValue& v : values) {
        if (v.stat == stat && v.layer == layer) return v.value;
    }
    return 0;
}

void Desired(void* item, const Roles& roles, const std::vector<StatValue>& ours, std::vector<StatValue>& out) noexcept {
    out.clear();
    const std::uint8_t context = GetDataContext(item);
    const std::uint8_t* tables = context < 4 ? GetDataTables(context) : nullptr;
    if (tables == nullptr) return;
    const auto rows = Read<std::uint64_t>(tables, TablesStatCount);
    if (roles.temperedPct != 0 || roles.magnitudePct != 0) {
        thread_local std::vector<StatValue> pairs;
        ItemStatPairs(item, pairs);
        // The plugin's own pairs too: one whose modifier is gone must go to 0.
        for (const StatValue& v : ours) {
            if (Lookup(pairs, v.stat, v.layer) == 0) pairs.push_back({ v.stat, v.layer, 1 });
        }
        const std::int64_t edMax = ReadUnitStat(item, StatMaxDamagePct, 0);
        const std::int64_t edMin = ReadUnitStat(item, StatMinDamagePct, 0);
        for (const StatValue& pair : pairs) {
            const Slot* slot = SlotOf(pair.stat);
            if (slot == nullptr || static_cast<std::uint64_t>(pair.stat) >= rows) continue;
            const std::int32_t percent = (slot->tempered ? roles.temperedPct : 0) + (slot->magnitude ? roles.magnitudePct : 0);
            if (percent == 0) continue;
            // The item's own modifiers: its total without its base value and
            // without what this plugin's list already adds, per layer.
            const std::int64_t base = ReadOwnStat(item, pair.stat, pair.layer);
            std::int64_t own = static_cast<std::int64_t>(ReadUnitStat(item, pair.stat, pair.layer)) - base
                - Lookup(ours, pair.stat, pair.layer);
            if (pair.layer == 0 && slot->kind == SlotKind::FlatMax) own -= base * edMax / 100;  // what op 13 adds
            if (pair.layer == 0 && slot->kind == SlotKind::FlatMin) own -= base * edMin / 100;
            const std::int64_t value = own * percent / 100;
            if (value != 0) out.push_back({ pair.stat, pair.layer, static_cast<std::int32_t>(value) });
        }
    }
    if ((roles.mithrilPct != 0 || roles.durabilityPct != 0) && static_cast<std::uint64_t>(StatArmorClass) < rows) {
        const std::int64_t base   = ReadOwnStat(item, StatArmorClass, 0);
        const std::int64_t ed     = ReadUnitStat(item, StatArmorPercent, 0);
        std::int64_t       raised = base + base * roles.mithrilPct / 100;
        raised += raised * roles.durabilityPct / 100;
        const std::int64_t value = (raised + raised * ed / 100) - (base + base * ed / 100);
        if (value != 0) {
            bool merged = false;
            for (StatValue& v : out) {
                if (v.stat == StatArmorClass && v.layer == 0) {
                    v.value += static_cast<std::int32_t>(value);
                    merged = true;
                }
            }
            if (!merged) out.push_back({ StatArmorClass, 0, static_cast<std::int32_t>(value) });
        }
    }
    if (roles.etherealBasePct != 0) {
        // 0x3D3510 left each base at 3 * base / 2; ceil(2b/3) gives the items.txt
        // base back exactly. Raise it to base * (150 + perk) / 100, with the
        // weapon's enhanced damage on the raise as the engine puts it on a base.
        constexpr std::array<std::array<std::int32_t, 2>, 6> bases{ { { 21, StatMinDamagePct }, { 22, StatMaxDamagePct },
            { 23, StatMinDamagePct }, { 24, StatMaxDamagePct }, { 159, StatMinDamagePct }, { 160, StatMaxDamagePct } } };
        for (const auto& [stat, edStat] : bases) {
            if (static_cast<std::uint64_t>(stat) >= rows) continue;
            const std::int64_t base = ReadOwnStat(item, stat, 0);
            if (base <= 0) continue;
            const std::int64_t original = (2 * base + 2) / 3;
            const std::int64_t delta = original * (EtherealRoutinePct + roles.etherealBasePct) / 100 - base;
            if (delta <= 0) continue;
            const std::int64_t ed = ReadUnitStat(item, edStat, 0);
            AddValue(out, stat, 0, delta + delta * ed / 100);
        }
    }
    for (const StatValue& v : roles.extra) {
        if (v.stat >= 0 && static_cast<std::uint64_t>(v.stat) < rows) AddValue(out, v.stat, v.layer, v.value);
    }
}

void RemoveList(void* item, void* list) noexcept {
    if (!ListAttached(item, list)) return;
    const std::uint8_t context = GetDataContext(item);
    UnlinkStatList(item, list);
    FreeStatList(context, list);
    BonusListsRemoved.fetch_add(1, std::memory_order_relaxed);
}

void Reconcile(BonusSide& side, void* game, void* item, const Roles& roles) noexcept {
    const std::uint32_t id = Read<std::uint32_t>(item, UnitId);
    BonusEntry* entry = nullptr;
    std::size_t index = 0;
    for (; index < side.entries.size(); ++index) {
        if (side.entries[index].id == id && side.entries[index].game == game) {
            entry = &side.entries[index];
            break;
        }
    }
    if (entry != nullptr && !ListAttached(item, entry->list)) {
        // The item's lists were rebuilt; whatever was ours went with them.
        entry->list = nullptr;
        entry->values.clear();
    }
    thread_local std::vector<StatValue> wanted;
    static const std::vector<StatValue> none;
    Desired(item, roles, entry != nullptr ? entry->values : none, wanted);
    if (wanted.empty()) {
        if (entry != nullptr) {
            RemoveList(item, entry->list);
            side.entries.erase(side.entries.begin() + static_cast<std::ptrdiff_t>(index));
        }
        return;
    }
    if (entry == nullptr) {
        side.entries.push_back({ game, id, nullptr, {}, false });
        entry = &side.entries.back();
    }
    const std::uint8_t context = GetDataContext(item);
    if (entry->list == nullptr) {
        void* const list = AllocStatList(BonusListFlags, 0, static_cast<std::int32_t>(UnitItem), id);
        if (list == nullptr) return;
        AttachStatList(item, list, 1);
        entry->list = list;
        entry->values.clear();
        BonusListsAdded.fetch_add(1, std::memory_order_relaxed);
    }
    // Pairs no longer wanted go to 0 (the engine drops a stat at 0); the rest
    // are written only when they change.
    for (const StatValue& old : entry->values) {
        if (Lookup(wanted, old.stat, old.layer) == 0) {
            SetListStat(context, entry->list, old.stat, 0, old.layer);
            BonusValuesWritten.fetch_add(1, std::memory_order_relaxed);
        }
    }
    for (const StatValue& v : wanted) {
        if (Lookup(entry->values, v.stat, v.layer) == v.value) continue;
        SetListStat(context, entry->list, v.stat, v.value, v.layer);
        BonusValuesWritten.fetch_add(1, std::memory_order_relaxed);
    }
    entry->values = wanted;
    entry->seen = true;
}

// A character bonus rides on one active item, armor first so that swapping
// weapons does not move it. Body locations 11 and 12 hold the inactive set.
constexpr std::array<std::int32_t, 10> CarrierOrder{ 3, 1, 10, 9, 8, 2, 6, 7, 4, 5 };
constexpr std::int32_t LastActiveBodyLoc = 10;

auto IsEthereal(void* item) noexcept -> bool {
    return CheckItemFlag(item, EtherealFlag, __LINE__, "blacksmith") != 0;
}

void ProcessCharacter(BonusSide& side, void* game, void* player) noexcept {
    const auto has = [player](const PerkConfig& perk) noexcept { return perk.enabled && perk.percent != 0 && HasPerk(player, perk); };
    const bool tempered      = has(Settings.tempered);
    const bool magnitude     = has(Settings.magnitude);
    const bool mithril       = has(Settings.mithril);
    const bool durability    = has(Settings.durability);
    const bool etherealPerk  = Settings.ethereal.enabled && HasPerk(player, Settings.ethereal);
    const bool etherealBase  = etherealPerk && Settings.ethereal.percent != 0;
    const bool etherealSpell = etherealPerk && Settings.etherealSpellPct != 0 && !Settings.etherealSpellStats.empty();
    const bool unbreakable   = has(Settings.unbreakable) && !Settings.unbreakableDamageStats.empty();
    const bool projectile    = Settings.reach.enabled && Settings.reachProjectilePct != 0 && HasPerk(player, Settings.reach);
    if (!tempered && !magnitude && !mithril && !durability && !etherealBase && !etherealSpell && !unbreakable && !projectile) return;
    void* const inventory = GetInventory(player, "blacksmith", 0);
    const TypeIds* types = Types(GetDataContext(player));
    if (inventory == nullptr || types == nullptr) return;
    std::array<void*, static_cast<std::size_t>(LastBodyLoc) + 1> items{};
    for (std::int32_t loc = FirstBodyLoc; loc <= LastBodyLoc; ++loc) {
        void* const item = BodyLocItem(inventory, loc);
        if (item != nullptr && UnitType(item) == UnitItem) items[static_cast<std::size_t>(loc)] = item;
    }
    // Ethereal Weapons' spell damage: one ethereal weapon per weapon set.
    std::array<void*, 2> spellCarriers{};
    if (etherealSpell) {
        constexpr std::array<std::array<std::int32_t, 2>, 2> sets{ { { 4, 5 }, { 11, 12 } } };
        for (std::size_t set = 0; set < sets.size(); ++set) {
            for (const std::int32_t loc : sets[set]) {
                void* const item = items[static_cast<std::size_t>(loc)];
                if (item != nullptr && ItemMatches(item, types->ethereal) && IsEthereal(item)) {
                    spellCarriers[set] = item;
                    break;
                }
            }
        }
    }
    // Unbreakable Gear's pooled durability and the carrier of the character's
    // own bonuses.
    void* carrier = nullptr;
    std::int64_t pool = 0;
    if (unbreakable || projectile) {
        for (const std::int32_t loc : CarrierOrder) {
            if (items[static_cast<std::size_t>(loc)] != nullptr) {
                carrier = items[static_cast<std::size_t>(loc)];
                break;
            }
        }
        if (unbreakable) {
            for (std::int32_t loc = FirstBodyLoc; loc <= LastActiveBodyLoc; ++loc) {
                void* const item = items[static_cast<std::size_t>(loc)];
                if (item == nullptr || !ItemMatches(item, types->unbreakable)) continue;
                const std::int32_t points = ReadUnitStat(item, StatDurabilityNow, 0);
                if (points > 0) pool += points;
            }
        }
    }
    for (std::int32_t loc = FirstBodyLoc; loc <= LastBodyLoc; ++loc) {
        void* const item = items[static_cast<std::size_t>(loc)];
        if (item == nullptr) continue;
        Roles roles{};
        if (tempered && ItemMatches(item, types->tempered)) roles.temperedPct = Settings.tempered.percent;
        if (magnitude && ItemMatches(item, types->magnitude)) roles.magnitudePct = Settings.magnitude.percent;
        if (mithril && ItemMatches(item, types->mithril)) roles.mithrilPct = Settings.mithril.percent;
        if (durability && ItemMatches(item, types->durability)) {
            const std::int32_t points = ReadUnitStat(item, Settings.useMaxDurability ? StatDurabilityMax : StatDurabilityNow, 0);
            if (points > 0) roles.durabilityPct = points * Settings.durability.percent;
        }
        if (etherealBase && ItemMatches(item, types->ethereal) && IsEthereal(item)) roles.etherealBasePct = Settings.ethereal.percent;
        if (item == spellCarriers[0] || item == spellCarriers[1]) {
            for (const std::int32_t stat : Settings.etherealSpellStats) roles.extra.push_back({ stat, 0, Settings.etherealSpellPct });
        }
        if (item == carrier) {
            if (unbreakable) {
                const std::int64_t bonus = pool / Settings.unbreakable.percent;
                if (bonus > 0) {
                    for (const std::int32_t stat : Settings.unbreakableDamageStats) {
                        roles.extra.push_back({ stat, 0, static_cast<std::int32_t>(bonus) });
                    }
                }
            }
            if (projectile) roles.extra.push_back({ Settings.reachProjectileStat, 0, Settings.reachProjectilePct });
        }
        if (roles.temperedPct != 0 || roles.magnitudePct != 0 || roles.mithrilPct != 0 || roles.durabilityPct != 0
                || roles.etherealBasePct != 0 || !roles.extra.empty()) {
            Reconcile(side, game, item, roles);
        }
    }
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
        if (item != nullptr && UnitType(item) == UnitItem) RemoveList(item, entry.list);
        side.entries.erase(side.entries.begin() + static_cast<std::ptrdiff_t>(i));
    }
}

void StartPass(BonusSide& side) noexcept {
    const std::uint64_t generation = Generation.load();
    if (side.generation != generation) {
        // A game was left or joined: the units behind the old entries are gone.
        side.entries.clear();
        side.generation = generation;
    }
}

constexpr std::size_t MaxPlayers = 16;

void HostPass() noexcept {
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
    ServerBonusItems.store(static_cast<std::uint32_t>(ServerBonus.entries.size()));
}

void ClientPass() noexcept {
    StartPass(ClientBonus);
    void* const player = PlayerFromIndex(LocalPlayerIndex());
    if (player != nullptr && UnitType(player) == UnitPlayer) ProcessCharacter(ClientBonus, nullptr, player);
    Sweep(ClientBonus, false);
    ClientBonusItems.store(static_cast<std::uint32_t>(ClientBonus.entries.size()));
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
    if (!ItemBonusReady || ThreadSvc == nullptr) return;
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

auto InstallItemBonus() -> bool {
    if (!Settings.tempered.enabled && !Settings.mithril.enabled && !Settings.durability.enabled && !Settings.magnitude.enabled
            && !Settings.ethereal.enabled && !Settings.unbreakable.enabled && !Settings.reach.enabled) {
        return true;
    }
    AllocStatList    = At<AllocListFn>(AllocStatListRva);
    AttachStatList   = At<AttachListFn>(AttachStatListRva);
    SetListStat      = At<SetListStatFn>(SetListStatRva);
    UnlinkStatList   = At<UnlinkListFn>(UnlinkStatListRva);
    FreeStatList     = At<FreeListFn>(FreeStatListRva);
    GetInventory     = At<InventoryFn>(InventoryRva);
    BodyLocItem      = At<BodyLocItemFn>(BodyLocItemRva);
    ServerUnit       = At<ServerUnitFn>(ServerUnitRva);
    ClientUnit       = At<ClientUnitFn>(ClientUnitRva);
    LocalPlayerIndex = At<LocalIndexFn>(LocalPlayerIndexRva);
    PlayerFromIndex  = At<PlayerFromIndexFn>(PlayerFromIndexRva);
    BuildSlots();
    if (HMODULE core = GetModuleHandleW(L"D2RCore.dll"); core != nullptr) {
        const auto* visit = reinterpret_cast<const std::uint8_t*>(GetProcAddress(core, "VisitWideUnitStats"));
        FullStatsLayout = visit != nullptr && std::memcmp(visit, VisitWideUnitStatsBytes, sizeof(VisitWideUnitStatsBytes)) == 0;
    }
    if (!FullStatsLayout) {
        Context->LogWarn("Blacksmith: D2RCore!VisitWideUnitStats does not match D2RCore 1.3.1; Tempered Steel and Modifier "
            "Magnitude are off (Mithril Weaving and Durability Defense still work).");
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
        InstallProblem = "D2RLoader's thread or lifecycle service is unavailable, so the item bonus perks are off";
        Context->LogError("Blacksmith: D2RLoader's thread or lifecycle service is unavailable; the item bonus perks are off.");
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
            InstallProblem = "a game join/leave listener was refused, so the item bonus perks are off";
            Context->LogError("Blacksmith: a game join/leave listener was refused; the item bonus perks are off.");
            return false;
        }
    }
    ItemBonusReady = true;
    StartChains(Context);
    return true;
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
    const std::array<Witness, 14> callables{ {
        W(IscRecordBodyRva, IscRecordBody, "DATATBLS_GetItemStatCostRecord"),
        W(CombatDurabilityEntryRva, CombatDurabilityEntry, "the combat durability loss"),
        W(SkillDurabilityEntryRva, SkillDurabilityEntry, "the skill durability loss"),
        W(EquippedWeaponEntryRva, EquippedWeaponEntry, "INVENTORY_GetEquippedWeapon"),
        W(ItemQualityEntryRva, ItemQualityEntry, "ITEMS_GetItemQuality"),
        W(UnlinkStatListEntryRva, UnlinkStatListEntry, "STATLIST_UnlinkStatList"),
        W(FreeStatListEntryRva, FreeStatListEntry, "STATLIST_FreeStatList"),
        W(BodyLocItemEntryRva, BodyLocItemEntry, "INVENTORY_GetItemFromBodyLoc"),
        W(InventoryEntryRva, InventoryEntry, "UNITS_GetInventory"),
        W(ClientUnitEntryRva, ClientUnitEntry, "CLIENT_GetUnitByIdAndType"),
        W(ServerUnitEntryRva, ServerUnitEntry, "SUNIT_GetServerUnit"),
        W(LocalPlayerIndexEntryRva, LocalPlayerIndexEntry, "the local player index"),
        W(PlayerFromIndexEntryRva, PlayerFromIndexEntry, "CLIENT_GetLocalPlayer"),
        W(StatListWindowRva, StatListWindow, "the item stat list creation in 0x43EF10"),
    } };
    for (const Witness& witness : callables) {
        if (!Context->CheckExpectedBytes(witness.rva, witness.bytes, witness.size)
                && (witness.rva == StatListWindowRva || !EntryHooked(witness.rva))) {
            char text[256];
            std::snprintf(text, sizeof(text), "%s at 0x%llX does not match D2RLoader 1.3.1",
                witness.name, static_cast<unsigned long long>(witness.rva));
            InstallProblem = text;
            D2RL::LogErrorF(Context, "Blacksmith: %s. Nothing was installed.", text);
            return false;
        }
    }
    const std::array<Witness, 24> witnesses{ {
        W(CombatWindowARva, CombatWindowA, "the melee hit's weapon durability call"),
        W(CombatWindowBRva, CombatWindowB, "the melee hit's armor durability call"),
        W(SkillDurabilityWindowRva, SkillDurabilityWindow, "the skill durability call"),
        W(MeleeRangeWeaponWindowRva, MeleeRangeWeaponWindow, "UNITS_GetMeleeRange's weapon lookup"),
        W(MagicAffixWindowRva, MagicAffixWindow, "the magic affix roll"),
        W(GambleWindowARva, GambleWindowA, "the gamble purchase"),
        W(GambleWindowBRva, GambleWindowB, "the gamble purchase (upgraded base)"),
        W(GambleFrameRva, GambleFrame, "the gamble purchase's frame"),
        W(RareApplyWindowRva, RareApplyWindow, "the rare affix slots"),
        W(KillerHomeRva, KillerHome, "the drop roll's killer store"),
        W(SpawnCallWindowRva, SpawnCallWindow, "the item spawn call in the drop roll"),
        W(QualityPrologueRva, QualityPrologue, "the quality roll's frame"),
        W(QualityOwnerCallRva, QualityOwnerCall, "the quality roll's minion owner lookup"),
        W(UniqueWindowRva, UniqueWindow, "the unique roll"),
        W(EtherealWindowRva, EtherealWindow, "the ethereal roll"),
        W(CubeUpgradeWindowRva, CubeUpgradeWindow, "the cube's in-place base upgrade"),
        W(TypeCheckBodyRva, TypeCheckBody, "the item type check"),
        W(CodeToClassCallRva, CodeToClassCall, "the item code lookup"),
        W(InitItemEntryRva, InitItemEntry, "the item base initializer"),
        W(RequirementWindowRva, RequirementWindow, "the requirement percent in the requirement check"),
        W(RequirementHelperRva, RequirementHelper, "the requirement percent helper"),
        W(RegistryResetRva, RegistryReset, "the server client registry reset"),
        W(ClientLookupRva, ClientLookup, "the server client lookup"),
        W(ClientPlayerRva, ClientPlayer, "the server client fields"),
    } };
    for (const Witness& witness : witnesses) {
        if (!Context->CheckExpectedBytes(witness.rva, witness.bytes, witness.size)) {
            char text[256];
            std::snprintf(text, sizeof(text), "%s at 0x%llX does not match D2RLoader 1.3.1 (another plugin may own it)",
                witness.name, static_cast<unsigned long long>(witness.rva));
            InstallProblem = text;
            D2RL::LogErrorF(Context, "Blacksmith: %s. Nothing was installed.", text);
            return false;
        }
    }
    return true;
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

template <std::size_t N>
auto BranchBytes(std::uint8_t opcode, std::uint64_t site, const std::uint8_t* target, std::array<std::uint8_t, N>& out) noexcept -> bool {
    const auto from = Base + site + 5;
    const auto to = reinterpret_cast<std::uintptr_t>(target);
    if (!CanReach(from, to)) return false;
    const auto rel = static_cast<std::int32_t>(static_cast<std::int64_t>(to) - static_cast<std::int64_t>(from));
    out.fill(0x90);
    out[0] = opcode;
    std::memcpy(out.data() + 1, &rel, 4);
    return true;
}

void WriteSlot(std::uint8_t* relay, std::size_t offset, std::uintptr_t value) noexcept {
    std::memcpy(relay + offset, &value, sizeof(value));
}

void BindNatives() noexcept {
    OriginalSpawn  = At<SpawnFn>(SpawnHelperRva);
    GetOwner       = At<UnitFn>(GetOwnerRva);
    ReadOwnStat    = At<ReadStatFn>(ReadOwnStatRva);
    ReadUnitStat   = At<ReadStatFn>(ReadUnitStatRva);
    SetUnitStat    = At<SetStatFn>(SetUnitStatRva);
    GetDataContext = At<DataContextFn>(GetDataContextRva);
    GetDataTables  = At<DataTablesFn>(GetDataTablesRva);
    ItemsRecord    = At<ItemsRecordFn>(ItemsRecordRva);
    TypeRow        = At<TypeRowFn>(TypeRowRva);
    CheckItemType  = At<CheckItemTypeFn>(CheckItemTypeRva);
    CodeToClass    = At<CodeToClassFn>(CodeToClassRva);
    ItemSeed       = At<UnitFn>(ItemSeedRva);
    RollRandom     = At<RollRandomFn>(RollRandomRva);
    SetClassId     = At<SetClassIdFn>(SetClassIdRva);
    CheckItemFlag  = At<CheckFlagFn>(CheckItemFlagRva);
    ApplyEthereal  = At<VoidItemFn>(ApplyEtherealRva);
    HasDurability  = At<ItemFn>(HasDurabilityRva);
    OriginalMastery = At<MasteryFn>(MasteryRva);
    OriginalGamble = At<GambleItemFn>(GambleItemRva);
    SelectAffix    = At<SelectAffixFn>(SelectAffixRva);
    GetPrefix      = At<AffixGetFn>(GetPrefixRva);
    GetSuffix      = At<AffixGetFn>(GetSuffixRva);
    SetPrefix      = At<AffixSetFn>(SetPrefixRva);
    SetSuffix      = At<AffixSetFn>(SetSuffixRva);
    ItemQuality    = At<QualityFn>(ItemQualityRva);
    UnitStatValue  = At<ReadStatFn>(UnitStatValueRva);
    OriginalCombatDurability = At<CombatDurabilityFn>(CombatDurabilityRva);
    OriginalSkillDurability  = At<SkillDurabilityFn>(SkillDurabilityRva);
    EquippedWeapon = At<EquippedWeaponFn>(EquippedWeaponRva);
    GetInventory   = At<InventoryFn>(InventoryRva);
    FindStatList   = At<FindStatListFn>(FindStatListRva);
    SetListStat    = At<SetListStatFn>(SetListStatRva);
}

void Restore() noexcept {
    if (SkillPatched && Context->PatchBytes(SkillCallRva, SkillWritten.data(), 5, SkillCallStock, 5)) SkillPatched = false;
    if (CombatBPatched && Context->PatchBytes(CombatCallBRva, CombatBWritten.data(), 5, CombatCallBStock, 5)) CombatBPatched = false;
    if (CombatAPatched && Context->PatchBytes(CombatCallARva, CombatAWritten.data(), 5, CombatCallAStock, 5)) CombatAPatched = false;
    if (GambleBPatched && Context->PatchBytes(GambleCallBRva, GambleBWritten.data(), 5, GambleCallBStock, 5)) GambleBPatched = false;
    if (GambleAPatched && Context->PatchBytes(GambleCallARva, GambleAWritten.data(), 5, GambleCallAStock, 5)) GambleAPatched = false;
    if (RequirementHelperPatched && Context->PatchBytes(RequirementHelperCallRva, RequirementHelperWritten.data(), 5, RequirementHelperCallStock, 5)) RequirementHelperPatched = false;
    if (RequirementPatched && Context->PatchBytes(RequirementCallRva, RequirementWritten.data(), 5, RequirementCallStock, 5)) RequirementPatched = false;
    if (EtherealPatched && Context->PatchBytes(EtherealSiteRva, EtherealWritten.data(), 5, EtherealSiteStock, 5)) EtherealPatched = false;
    if (UniquePatched && Context->PatchBytes(UniqueSiteRva, UniqueWritten.data(), 8, UniqueSiteStock, 8)) UniquePatched = false;
    if (SpawnPatched && Context->PatchBytes(SpawnCallRva, SpawnWritten.data(), 5, SpawnCallStock, 5)) SpawnPatched = false;
}

auto Install() -> bool {
    if (!Verify()) return false;
    BindNatives();

    const bool wantSpectral = Settings.spectral.enabled;
    const bool wantBlessed  = Settings.blessed.enabled;
    const bool wantRelic    = Settings.relic.enabled || Settings.secretPocket.enabled;
    const bool wantWeightless = Settings.weightless.enabled;
    const bool wantModifier = Settings.modifier.enabled || Settings.hidden.enabled || Settings.brilliant.enabled;
    const bool wantDurability = Settings.unbreakable.enabled;
    const bool wantReach    = Settings.reach.enabled && Settings.reach.percent != 0;
    if (!wantSpectral && !wantBlessed && !wantRelic && !wantWeightless && !wantModifier && !wantDurability && !wantReach) {
        if (Settings.tempered.enabled || Settings.mithril.enabled || Settings.durability.enabled || Settings.magnitude.enabled
                || Settings.ethereal.enabled || Settings.reach.enabled) {
            return true;
        }
        InstallProblem = "every perk is disabled in the config";
        return true;
    }

    RelayPage = AllocateNear(Base + SpawnCallRva, 0x1000);
    if (RelayPage == nullptr) {
        InstallProblem = "no relay page within reach of the game image";
        Context->LogError("Blacksmith: no relay page within reach of the game image. Nothing was installed.");
        return false;
    }
    std::memcpy(RelayPage + SpawnRelayOffset, SpawnRelay, sizeof(SpawnRelay));
    std::memcpy(RelayPage + UniqueRelayOffset, UniqueRelay, sizeof(UniqueRelay));
    std::memcpy(RelayPage + EtherealRelayOffset, EtherealRelay, sizeof(EtherealRelay));
    WriteSlot(RelayPage + SpawnRelayOffset, SpawnRelaySlot, reinterpret_cast<std::uintptr_t>(&SpawnWithKiller));
    WriteSlot(RelayPage + UniqueRelayOffset, UniqueRelaySlotAdjust, reinterpret_cast<std::uintptr_t>(&AdjustUniqueRange));
    WriteSlot(RelayPage + UniqueRelayOffset, UniqueRelaySlotContinue, Base + UniqueContinueRva);
    WriteSlot(RelayPage + UniqueRelayOffset, UniqueRelaySlotHit, Base + UniqueHitRva);
    WriteSlot(RelayPage + EtherealRelayOffset, EtherealRelaySlot, reinterpret_cast<std::uintptr_t>(&EtherealThreshold));
    std::memcpy(RelayPage + GambleRelayOffset, GambleRelay, sizeof(GambleRelay));
    WriteSlot(RelayPage + GambleRelayOffset, GambleRelaySlot, reinterpret_cast<std::uintptr_t>(&GambleWithPlayer));
    std::memcpy(RelayPage + MasteryRelayOffset, MasteryRelay, sizeof(MasteryRelay));
    WriteSlot(RelayPage + MasteryRelayOffset, MasteryRelaySlot, reinterpret_cast<std::uintptr_t>(&WeightlessMastery));
    std::memcpy(RelayPage + CombatDurabilityRelayOffset, CombatDurabilityRelay, sizeof(CombatDurabilityRelay));
    WriteSlot(RelayPage + CombatDurabilityRelayOffset, JumpRelaySlot, reinterpret_cast<std::uintptr_t>(&KeepCombatDurability));
    std::memcpy(RelayPage + SkillDurabilityRelayOffset, SkillDurabilityRelay, sizeof(SkillDurabilityRelay));
    WriteSlot(RelayPage + SkillDurabilityRelayOffset, JumpRelaySlot, reinterpret_cast<std::uintptr_t>(&KeepSkillDurability));
    DWORD previous = 0;
    if (!VirtualProtect(RelayPage, 0x1000, PAGE_EXECUTE_READ, &previous)) {
        InstallProblem = "the relay page could not be made executable";
        Context->LogError("Blacksmith: the relay page could not be made executable. Nothing was installed.");
        return false;
    }
    FlushInstructionCache(GetCurrentProcess(), RelayPage, 0x1000);

    if (!BranchBytes(0xE8, SpawnCallRva, RelayPage + SpawnRelayOffset, SpawnWritten)
            || !BranchBytes(0xE9, UniqueSiteRva, RelayPage + UniqueRelayOffset, UniqueWritten)
            || !BranchBytes(0xE8, EtherealSiteRva, RelayPage + EtherealRelayOffset, EtherealWritten)
            || !BranchBytes(0xE8, RequirementCallRva, RelayPage + MasteryRelayOffset, RequirementWritten)
            || !BranchBytes(0xE8, RequirementHelperCallRva, RelayPage + MasteryRelayOffset, RequirementHelperWritten)
            || !BranchBytes(0xE8, GambleCallARva, RelayPage + GambleRelayOffset, GambleAWritten)
            || !BranchBytes(0xE8, GambleCallBRva, RelayPage + GambleRelayOffset, GambleBWritten)
            || !BranchBytes(0xE8, CombatCallARva, RelayPage + CombatDurabilityRelayOffset, CombatAWritten)
            || !BranchBytes(0xE8, CombatCallBRva, RelayPage + CombatDurabilityRelayOffset, CombatBWritten)
            || !BranchBytes(0xE8, SkillCallRva, RelayPage + SkillDurabilityRelayOffset, SkillWritten)) {
        InstallProblem = "a relay is out of rel32 reach";
        Context->LogError("Blacksmith: a relay is out of rel32 reach. Nothing was installed.");
        return false;
    }

    if (wantBlessed || wantModifier) {
        InitHooked = Context->InstallInlineHook<InitItemFn>(InitItemRva, InitItemEntry,
            static_cast<std::uint32_t>(sizeof(InitItemEntry)), &HookInitItem, &OriginalInitItem);
        if (!InitHooked) {
            InstallProblem = "the item base initializer hook at 0x43EF10 was refused";
            Context->LogError("Blacksmith: the item base initializer hook at 0x43EF10 was refused. Nothing was installed.");
            return false;
        }
    }
    if (wantSpectral || wantBlessed || wantModifier) {
        SpawnPatched = Context->PatchBytes(SpawnCallRva, SpawnCallStock, 5, SpawnWritten.data(), 5);
        if (!SpawnPatched) {
            InstallProblem = "the item spawn call at 0x4410C7 could not be redirected";
            Context->LogError("Blacksmith: the item spawn call at 0x4410C7 could not be redirected.");
            Restore();
            return false;
        }
    }
    if (wantRelic) {
        UniquePatched = Context->PatchBytes(UniqueSiteRva, UniqueSiteStock, 8, UniqueWritten.data(), 8);
        if (!UniquePatched) {
            InstallProblem = "the unique roll at 0x442429 could not be redirected";
            Context->LogError("Blacksmith: the unique roll at 0x442429 could not be redirected.");
            Restore();
            return false;
        }
    }
    if (wantSpectral) {
        EtherealPatched = Context->PatchBytes(EtherealSiteRva, EtherealSiteStock, 5, EtherealWritten.data(), 5);
        if (!EtherealPatched) {
            InstallProblem = "the ethereal roll at 0x4434DB could not be redirected";
            Context->LogError("Blacksmith: the ethereal roll at 0x4434DB could not be redirected.");
            Restore();
            return false;
        }
    }
    if (wantModifier) {
        GambleAPatched = Context->PatchBytes(GambleCallARva, GambleCallAStock, 5, GambleAWritten.data(), 5);
        GambleBPatched = GambleAPatched && Context->PatchBytes(GambleCallBRva, GambleCallBStock, 5, GambleBWritten.data(), 5);
        if (!GambleAPatched || !GambleBPatched) {
            InstallProblem = "the gamble purchase calls at 0x53EB40 and 0x53EBDF could not be redirected";
            Context->LogError("Blacksmith: the gamble purchase calls at 0x53EB40 and 0x53EBDF could not be redirected.");
            Restore();
            return false;
        }
    }
    if (wantWeightless) {
        RequirementPatched = Context->PatchBytes(RequirementCallRva, RequirementCallStock, 5, RequirementWritten.data(), 5);
        RequirementHelperPatched = RequirementPatched
            && Context->PatchBytes(RequirementHelperCallRva, RequirementHelperCallStock, 5, RequirementHelperWritten.data(), 5);
        if (!RequirementPatched || !RequirementHelperPatched) {
            InstallProblem = "the requirement percent calls at 0x36BDE6 and 0x371FB6 could not be redirected";
            Context->LogError("Blacksmith: the requirement percent calls at 0x36BDE6 and 0x371FB6 could not be redirected.");
            Restore();
            return false;
        }
    }
    if (wantDurability) {
        CombatAPatched = Context->PatchBytes(CombatCallARva, CombatCallAStock, 5, CombatAWritten.data(), 5);
        CombatBPatched = CombatAPatched && Context->PatchBytes(CombatCallBRva, CombatCallBStock, 5, CombatBWritten.data(), 5);
        SkillPatched = CombatBPatched && Context->PatchBytes(SkillCallRva, SkillCallStock, 5, SkillWritten.data(), 5);
        if (!SkillPatched) {
            InstallProblem = "the durability loss calls at 0x45270C, 0x4528CD and 0x55881B could not be redirected";
            Context->LogError("Blacksmith: the durability loss calls at 0x45270C, 0x4528CD and 0x55881B could not be redirected.");
            Restore();
            return false;
        }
    }
    if (wantReach) {
        ReachHooked = Context->InstallInlineHook<MeleeRangeFn>(MeleeRangeRva, MeleeRangeEntry,
            static_cast<std::uint32_t>(sizeof(MeleeRangeEntry)), &HookMeleeRange, &OriginalMeleeRange);
        if (!ReachHooked) {
            InstallProblem = "the melee range hook at 0x34A620 was refused, so Extended Reach's range is off";
            Context->LogWarn("Blacksmith: the melee range hook at 0x34A620 was refused (another plugin may own it); Extended "
                "Reach's range is off, its projectile speed still works.");
        }
    }
    return true;
}

// ---------------------------------------------------------------------------
// Console command
// ---------------------------------------------------------------------------
auto PerkLine(const char* name, const PerkConfig& perk, const char* valueName, bool installed, const char* suffix = "%")
    -> std::string {
    std::string types;
    for (const std::string& code : perk.types) {
        if (!types.empty()) types += ",";
        types += code;
    }
    char text[256];
    std::snprintf(text, sizeof(text), "%s: %s, stat %d, %s %d%s, types [%s], %s", name, perk.enabled ? "on" : "off", perk.stat,
        valueName, perk.percent, suffix, types.c_str(), installed ? "installed" : "not installed");
    return text;
}

auto __cdecl StatusCommand(D2R::Game::Client*, const D2RL::ConsoleCommandContext* command, void*) noexcept -> D2RL::ConsoleCommandResult {
    if (command == nullptr || command->plugin == nullptr) return D2RL::ConsoleCommandResult::Failed;
    const auto* plugin = command->plugin;
    char text[512];
    std::snprintf(text, sizeof(text), "Blacksmith %s: %s%s%s", PluginVersion, Settings.enabled ? "on" : "off",
        InstallProblem.empty() ? "" : ", ", InstallProblem.c_str());
    plugin->WriteConsoleMessage(text);
    plugin->WriteConsoleMessage(PerkLine("Spectral Arsenal", Settings.spectral, "ethereal chance", EtherealPatched && SpawnPatched).c_str());
    plugin->WriteConsoleMessage(PerkLine("Blessed Gear", Settings.blessed, "upgrade chance", InitHooked && SpawnPatched).c_str());
    plugin->WriteConsoleMessage(PerkLine("Relic Hunter", Settings.relic, "unique bonus", UniquePatched).c_str());
    plugin->WriteConsoleMessage(PerkLine("The Secret Pocket (Jeweler)", Settings.secretPocket, "unique bonus", UniquePatched).c_str());
    plugin->WriteConsoleMessage(PerkLine("Weightless", Settings.weightless, "requirement reduction", RequirementPatched && RequirementHelperPatched).c_str());
    plugin->WriteConsoleMessage(PerkLine("Additional Modifier", Settings.modifier, "chance", InitHooked && SpawnPatched).c_str());
    plugin->WriteConsoleMessage(PerkLine("Hidden Potential (Jeweler)", Settings.hidden, "chance", InitHooked && SpawnPatched).c_str());
    plugin->WriteConsoleMessage(PerkLine("Brilliant Quality (Jeweler)", Settings.brilliant, "chance", InitHooked && SpawnPatched).c_str());
    std::snprintf(text, sizeof(text), "Additional modifiers: %llu added (rings and amulets %llu), %llu of them without a free affix "
        "slot | jewels with a doubled modifier %llu | gamble purchases %llu",
        static_cast<unsigned long long>(ModifiersAdded.load() + HiddenAdded.load()), static_cast<unsigned long long>(HiddenAdded.load()),
        static_cast<unsigned long long>(ModifiersWithoutSlot.load()), static_cast<unsigned long long>(JewelsDoubled.load()),
        static_cast<unsigned long long>(GambleItems.load()));
    plugin->WriteConsoleMessage(text);
    plugin->WriteConsoleMessage(PerkLine("Tempered Steel", Settings.tempered, "modifier bonus", ItemBonusReady).c_str());
    plugin->WriteConsoleMessage(PerkLine("Mithril Weaving", Settings.mithril, "base defense bonus", ItemBonusReady).c_str());
    plugin->WriteConsoleMessage(PerkLine("Durability Defense", Settings.durability, "base defense per point", ItemBonusReady).c_str());
    plugin->WriteConsoleMessage(PerkLine("Modifier Magnitude", Settings.magnitude, "modifier bonus", ItemBonusReady).c_str());
    std::snprintf(text, sizeof(text), "Modifier Magnitude raises %zu stats; %zu stats in all are kept on item bonus lists",
        Settings.magnitudeStats.size(), SlotCount);
    plugin->WriteConsoleMessage(text);
    plugin->WriteConsoleMessage(PerkLine("Ethereal Weapons", Settings.ethereal, "extra base damage", ItemBonusReady).c_str());
    plugin->WriteConsoleMessage(PerkLine("Unbreakable Gear", Settings.unbreakable, "durability per +1% total damage",
        ItemBonusReady && SkillPatched, "").c_str());
    plugin->WriteConsoleMessage(PerkLine("Extended Reach", Settings.reach, "melee range", ReachHooked, "").c_str());
    std::snprintf(text, sizeof(text), "Durability losses kept: %llu | projectile speed +%d to stat %d",
        static_cast<unsigned long long>(DurabilityLossesKept.load()), Settings.reachProjectilePct, Settings.reachProjectileStat);
    plugin->WriteConsoleMessage(text);
    std::snprintf(text, sizeof(text),
        "Item bonus lists: host %u items, client %u items | passes running: host %s, client %s | added %llu, removed %llu, "
        "values written %llu",
        ServerBonusItems.load(), ClientBonusItems.load(), ServerChain.load() != 0 ? "yes" : "no",
        ClientChain.load() != 0 ? "yes" : "no", static_cast<unsigned long long>(BonusListsAdded.load()),
        static_cast<unsigned long long>(BonusListsRemoved.load()), static_cast<unsigned long long>(BonusValuesWritten.load()));
    plugin->WriteConsoleMessage(text);
    std::snprintf(text, sizeof(text),
        "Drops %llu (%llu for a perk player) | ethereal at the perk chance %llu | upgrades to exceptional %llu, to elite %llu "
        "| unique chances raised %llu (rings, amulets and jewels %llu) | requirements reduced %llu",
        static_cast<unsigned long long>(DropsSeen.load()), static_cast<unsigned long long>(DropsWithPerkPlayer.load()),
        static_cast<unsigned long long>(EtherealRaised.load()), static_cast<unsigned long long>(UpgradesExceptional.load()),
        static_cast<unsigned long long>(UpgradesElite.load()), static_cast<unsigned long long>(UniqueRangesRaised.load()),
        static_cast<unsigned long long>(PocketRangesRaised.load()), static_cast<unsigned long long>(RequirementsReduced.load()));
    plugin->WriteConsoleMessage(text);
    if (!ConfigProblems.empty()) {
        std::snprintf(text, sizeof(text), "Config: %s", ConfigProblems.c_str());
        plugin->WriteConsoleMessage(text);
    }
    return D2RL::ConsoleCommandResult::Handled;
}

constexpr D2RL::PluginInfo Info{
    .infoSize    = D2RL::PluginInfoSize,
    .abiVersion  = D2RL_PLUGIN_ABI_VERSION,
    .id          = "celestialrayone.blacksmith",
    .name        = "Blacksmith",
    .version     = PluginVersion,
    .author      = "CelestialRayOne",
    .description = "Blacksmith specialization perks: Tempered Steel, Mithril Weaving, Weightless, Spectral Arsenal, "
                   "Blessed Gear, Relic Hunter, Additional Modifier, Durability Defense, Modifier Magnitude, "
                   "Ethereal Weapons, Unbreakable Gear and Extended Reach, plus the Jeweler's The Secret Pocket, Hidden Potential and Brilliant Quality.",
    .flags       = D2RL::PluginFlags::Shared | D2RL::PluginFlags::NativeHooks,
};

}  // namespace
}  // namespace CelestialRayOne::Blacksmith

using namespace CelestialRayOne::Blacksmith;

D2RL_PLUGIN_EXPORT auto D2RLoaderGetPluginInfo() noexcept -> const D2RL::PluginInfo* {
    return &Info;
}

D2RL_PLUGIN_EXPORT auto D2RLoaderLoadPlugin(const D2RL::PluginContext* context) noexcept -> bool {
    if (!D2RL::HasContext(context)) return false;
    Context = context;
    Base = context->exeBase;
    ReadConfiguration();
    if (!context->RegisterConsoleCommand("blacksmith", &StatusCommand, "Blacksmith perks: settings, installed parts and counters.")) {
        context->LogWarn("Blacksmith: the console command could not be registered.");
    }
    if (!Settings.enabled) {
        InstallProblem = "disabled in the config";
        context->LogInfo("Blacksmith: disabled in the config; nothing installed.");
        return true;
    }
    if (!Install()) return true;
    (void)InstallItemBonus();
    D2RL::LogInfoF(context, "Blacksmith %s: item bonus perks %s, Weightless %s, Spectral Arsenal %s, Blessed Gear %s, "
        "Relic Hunter and The Secret Pocket %s, Unbreakable Gear durability %s, Extended Reach range %s.", PluginVersion,
        ItemBonusReady ? "installed" : "off", RequirementPatched ? "installed" : "off", EtherealPatched ? "installed" : "off",
        InitHooked ? "installed" : "off", UniquePatched ? "installed" : "off", SkillPatched ? "installed" : "off",
        ReachHooked ? "installed" : "off");
    return true;
}

D2RL_PLUGIN_EXPORT void D2RLoaderUnloadPlugin() noexcept {
    ServerChain.store(0);
    ClientChain.store(0);
    if (Context != nullptr) Restore();
    Context = nullptr;
}
