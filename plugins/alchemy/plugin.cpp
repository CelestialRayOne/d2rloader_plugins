// Alchemy  (celestialrayone.alchemy)
//
// The Alchemist specialization perks that need engine code. The other
// Alchemist perks are misc.txt, treasureclassex.txt and cubemain.txt
// formulas and do not involve this plugin, except that Find Potion below
// lets the treasureclassex.txt ones reach the potion Find Rejuvenation finds.
//
//   Blessed Vials       while a Healing Potion effect runs: less damage
//                       taken; while a Mana Potion effect runs: immune to Mana
//                       Burn. Both through the service below: Shrine Rework
//                       applies the reduction where it applies the Armor
//                       shrine's, Monster Affixes asks before every burn.
//   Compounded Synergy  each running potion effect is scaled by 100% + 5% per
//                       different running potion effect, live.
//   Savvy Elixirs       a utility potion has a chance not to be used up.
//   Endless Vials       Healing and Mana Potion effects no longer end when
//                       life or mana is full.
//   Bottomless Vials    Healing and Mana Potions are never used up.
//   Serendipity         a utility potion has a chance to give a random shrine.
//   Elixir of           a hit that would kill a player carrying the state of a
//   Reincarnation       [[reincarnation]] entry (the elixir's state is the
//                       first) instead restores full life and mana, ends the
//                       state, gives a short invulnerability and starts that
//                       entry's cooldown: its cooldown stat holds the seconds
//                       left and counts down to 0, and while it runs the state
//                       saves nobody, even when something gives it back (a
//                       skill proc, say). The entry's sweep items and sweep
//                       uniques (35p for the elixir) get the crossbow's
//                       cooldown sweep drawn over them meanwhile.
//   Find Potion         the potion Find Rejuvenation (srvdofunc 69, the
//                       game's Find Potion) finds drops through a treasure
//                       class instead of the game's fixed potion, so the
//                       class's ConditionCalc perks apply to it (Unstable
//                       Mixtures turning a rejuvenation potion into an elixir).
//
// D2R 3.3 on D2RLoader. Every address below was read out of the live
// D2RLoader.exe image and decoded before being relied on; all of them are
// byte-checked at load and the plugin installs nothing on a build that
// differs.
//
// ---------------------------------------------------------------------------
// Living next to the other ESR plugins
// ---------------------------------------------------------------------------
//   Two plugins cannot patch the same bytes, and a plugin that byte-checks a
//   window refuses to install when another plugin already changed anything in
//   it. 1.0.0 broke two plugins that way, and this version touches neither:
//     Crossbow Charges  checks 44CF78..44CFA3 (its hit bonus window). 1.0.0
//                       relayed the call at 44CF93 inside it, so Crossbow
//                       Charges installed nothing whenever Alchemy loaded
//                       first. Blessed Vials' reduction now lives in Shrine
//                       Rework instead (service below).
//     Whirlwind         hooks the use-item handler 4F40C0 itself and checks
//                       its first 128 bytes (potions while whirlwinding). This
//                       plugin no longer hooks 4F40C0; it relays the two calls
//                       to it instead.
//     Item Quantity, Extended Drop Cap, Cow Set Drops, ConditionCalc Killer
//                       patch the treasure class roll 4404F0 (4406A1, its
//                       entry, the item spawn helper 444680, the condition
//                       checks 440643 and 440903). Find Potion only calls
//                       441300 in front of it and checks windows none of them
//                       write (4405E5, 44113E, and 3972AA in the class lookup).
//   Every site below was checked against the byte windows of all ESR plugins
//   (Crossbow Charges, Forever States, Whirlwind, Shrine Rework, Monster
//   Affixes and the rest).
//
// ---------------------------------------------------------------------------
// Using an item: the two calls to 4F40C0 (game, player, request, flag)
// ---------------------------------------------------------------------------
//   4C1447  E8 -> 4F40C0   message 26h (use an item) from the client
//   4F2417  E8 -> 4F40C0   a queued use, processed later
//   4F40C0 is the server's use-item handler. request+01 is the "consumed" byte
//   (its own failure log prints it as isConsumed), request+02 the player's unit
//   id, request+06 the use target kind (0 = plain use), request+0A the item id,
//   request+0E the location (0 inventory, 2 belt). It refuses the use while
//   the player is dead (34C2C0) or Uninterruptible (state 54, 3351B0), finds
//   the item with 48FE80 (game, 4, id), checks it with
//     5817F0 (game, player, item, 0)  can-use: pSpell rules and the misc.txt
//                                     UsageConditionCalc
//   takes it out of the belt or inventory when request+01 is set, and then
//   applies it:
//     581680 (game, player, item)     the pSpell effect
//   A not-consumed request cannot be faked: the handler compares request+01
//   with the item's own answer from 308E80 and rejects a mismatch, and for a
//   belt slot it also rejects the position. So when a perk
//   saves the item, the relay runs the handler's dead and Uninterruptible
//   refusal (with Whirlwind's while-whirlwinding exception, through its
//   "potion-scope" service), then 5817F0 and 581680 itself, and returns
//   success without the removal. Every other use goes to 4F40C0 unchanged,
//   through Whirlwind's hook when it is installed.
//
// ---------------------------------------------------------------------------
// Taking damage: SUNITDMG_ExecuteEvents 0x44CE80 (not hooked)
// ---------------------------------------------------------------------------
//   44D08F  41 8D 51 06      lea  edx, [r9+6]
//   44D093  E8 78 AC EA FF   call 2F7D10   the life write after a hit
//     Relayed. r8d is the new life, already 0 when the hit is lethal (the
//     function writes 0 below 256). The relay replaces a lethal value for a
//     player carrying a [[reincarnation]] entry's state whose cooldown stat
//     is 0 (the first such entry saves them), and keeps life unchanged during
//     the invulnerability. The function then reads life back and only marks
//     the unit as dying when it is 0, so no death event fires.
//   0x44CE80 itself is left alone: Monster Affixes owns an inline hook there.
//
// ---------------------------------------------------------------------------
// Potion effects ending at full life or mana (Endless Vials)
// ---------------------------------------------------------------------------
//   Player regen 0x42E600, life part:
//     42E689  BA 64 00 00 00   mov  edx, 100       ; healthpot
//     42E693  E8 A8 72 EC FF   call 2F5940         ; the state's stat list
//   Mana regen 0x42E350:
//     42E4EA  BA 6A 00 00 00   mov  edx, 106       ; manapot
//     42E4F2  E8 49 74 EC FF   call 2F5940
//   Both run only when life (or mana) is full, and remove the list they get
//   (2F7920 + 2F4180). Both calls are relayed; with Endless Vials the relay
//   answers "no list", so nothing is removed and the potion keeps its full
//   duration.
//
// ---------------------------------------------------------------------------
// The per-frame tick
// ---------------------------------------------------------------------------
//   42E74D  E8 FE FB FF FF   call 42E350   mana regen, every frame per player
//   Relayed. After the real call the relay ends invulnerability windows,
//   counts every [[reincarnation]] cooldown stat down (entries sharing a stat
//   share one countdown, run by the first of them) and, every 5 frames, runs
//   Compounded Synergy. (42E634, the regen reschedule, belongs to Forever
//   States; 0x42E600 is hooked by Crossbow Charges. Neither is touched here.)
//
// ---------------------------------------------------------------------------
// Compounded Synergy
// ---------------------------------------------------------------------------
//   The potion effects are the states of the healing_mana and utility items;
//   their stats come from misc.txt stat1..stat6 (stat4..6 are Misc Extra
//   Stats' columns, which the item records do not hold), read with states.txt
//   and itemstatcost.txt from the mod's data\global\excel folder when the
//   tables load. A running effect is its state's stat list on the player
//   (2F5940); its values are read with 2F5DF0 and written with 2F7C00, the
//   same calls the potion handlers use on a list that is already on the
//   player (57ECB0 merging two potions, 57FD80 setting an elixir's stats).
//   1.1.0 wrote with 2F3030 instead: that is the bare list write, which
//   returns "changed" and leaves updating the unit to its caller (2F7D10 does
//   it after calling it), so the player's stats never moved.
//   Each effect remembers its unscaled values. Before every potion use they
//   are put back, so the game merges a new potion into true values; after it,
//   and every 5 frames, every effect is written at 100 + 5 x count percent.
//
// ---------------------------------------------------------------------------
// Serendipity
// ---------------------------------------------------------------------------
//   A shrine's effect is chart[Code].handler(dataContext, operate, record)
//   (the 24-slot chart at 1D3F1D0, as the shrine operate function 58F2A0 calls
//   it); operate is +00 game, +08 the source of the timed effect, +10 the
//   player, +20 the shrines.txt row. The handlers read only those. The record
//   is the first shrines.txt row with that Code (38FE00 row count, 38FE40 the
//   record). The player is both source and target; Shrine Rework's sharing
//   runs only from a real shrine, so the effect stays on the drinker.
//
// ---------------------------------------------------------------------------
// The cooldown sweep on the elixirs (client)
// ---------------------------------------------------------------------------
//   Every item icon (inventory, belt, stash, cube) is drawn by one function:
//     15BB80 (item, x | y << 32 screen top-left, float scale in xmm2, params)
//   15BB99 movaps xmm6, xmm2 keeps the scale; r8 is never read. On the HD path
//   it gets the icon's sprite (a GameSprite {hd, legacy}), stores it in slot
//   [2A8AF28] of a 250-slot ring at 2A8AF30 (32 bytes a slot, sprite at +18h),
//   draws it through 858510 -> 79DA50 and moves the slot on by one. Items are
//   not widgets, so nothing can go in a layout file: the plugin hooks 15BB80
//   inline and, after the game's draw, draws the sweep with the same HD draw
//   the loot filter uses, 79DA50 (hd sprite, int32 position[2], float scale,
//   params): tint at +0 as RGBA floats, opacity at +16, frame at +24. The
//   tint's fourth float must be 1 for the colour to apply; at 0 the sprite
//   keeps its own colours (the sweep sprite is plain white). Crossbow Charges
//   writes the same {r, g, b, 1} into its sweep widget's tint (+9Ch), with the
//   opacity at +94h, which is why its shadow is black. 79DA50 also shows the
//   HD sprite's fields: +CCh frames, +D0h one frame's width, +C0h height, +BCh
//   the texture's width (one frame's U span is frame width / texture width),
//   +C4h/+C8h the anchor, +E8h the texture. Its whole body reads only those
//   seven fields and keeps no pointer to the sprite: the command it queues
//   holds the texture and the corners x0 = x - anchor x scale, x1 = x0 + frame
//   width x scale, y1 = y0 + height x scale, with V always 0 to 1. One scale
//   serves both sides, so the sweep is handed to it as a copy of those seven
//   fields in the icon's size: the icon's frame width, height and anchor, the
//   sweep's frames and texture, and the sweep's texture width scaled like its
//   frame width, so one frame keeps its U span. Drawn at the icon's position
//   and scale, its corners are the icon's own: the sweep stretched to the
//   icon, whatever its shape. The sprite
//   loads once on the UI thread through 858990 (name under data/hd/global/ui/),
//   like an ImageWidget's (as written, then in lower case). An icon shows the
//   cooldown of the [[reincarnation]] entry that names its unique in
//   sweep_uniques or lists its item code in sweep_items, the unique first.
//   The unique is the item's own: quality 7 from 36CF60 (ItemData+00h), its
//   uniqueitems.txt row from 36CE70 (ItemData+34h), and that row's index
//   column in the tables of the item's data context (34A0E0, unit+1BDh;
//   300A90 gives the tables). 0x31F1D0 compiles uniqueitems.txt into 348-byte
//   rows at tables+13C8h, counted by the qword at +13D0h, writing each row's
//   own index as a word at +00h; the index column is a 31-character string at
//   +02h. The time left comes from this plugin's own server tick when
//   the game's server runs in this process (single player, the host): the
//   tick publishes each player's seconds per entry by unit id, and the
//   client's local player has the same id. A client in someone else's game
//   reads the entry's cooldown stat instead, which only reaches it with
//   itemstatcost fCallback = 1 (2F9BB0 sends a changed stat to the owner only
//   when the stat record's flag 0x800 is set). Frame 0 is the full sweep, as
//   on the skill buttons. The local player is 09A480(08B2D0()).
//
// ---------------------------------------------------------------------------
// Find Potion through a treasure class (server)
// ---------------------------------------------------------------------------
//   567BD0 is srvdofunc 69 (the srvdofunc table at 238EA00, slot 69), the
//   game's Find Potion; Find Rejuvenation runs it with Param4 = 100. After its
//   own chance roll (calc1) 56AE90 picks a code from a fixed 15-row table at
//   23A2D50 (act + 5 x difficulty; healing, mana or rejuvenation by Param3 and
//   Param4), and the potion is dropped by
//     567CD4  E8 -> 436340   (game, corpse, code, quality 2)
//   436340 turns the code into an item class (313EF0) and creates the item
//   next to the corpse (43CD50). No treasure class is involved, so no
//   ConditionCalc perk ever sees the potion.
//   The call is relayed. A code listed in [find_potion] drops its treasure
//   class through the game's own treasure class drop instead:
//     441300  (game, unit, killer, class id (word), quality, item level,
//              ignore NoDrop, item array, count, max items)
//   the entry Find Treasure (srvdofunc 72) reaches through 446E80 and 447B80
//   to drop a corpse's own class, and the TestTreasure command (40B730) calls
//   with an array of 20. The relay passes what Find Treasure passes: the corpse
//   as the unit, the caster as the killer and ignore NoDrop 1 (447B80 gets the
//   1 from 446E80 and stores it at 447B83). Quality 0 rolls each item's
//   quality as on a kill; the item level is 43E8A0(corpse, 0), the level
//   436340 gives the potion. The array has one slot and max items is 1: with
//   an array the roll 4404F0 stops at exactly max items (4405E5..44062C,
//   44113E..441165), and Item Quantity and Extended Drop Cap raise that cap
//   only when there is no array, so a find still drops exactly one item. The
//   item is still created on the ground at the unit (444680 -> 43CD50); the
//   array only receives its pointer (44114F).
//   The killer is the unit 4404F0 keeps at [rbp-80h] (44055B), which
//   ConditionCalc Killer reads at both condition checks, so the class's
//   ConditionCalc rows read the caster's perks.
//   The caster: srvdofunc 69 holds it in rbp (567BE8 mov rbp, rdx; nothing
//   writes rbp again before the call), and its frame (sub rsp, 30h) has a
//   fifth-argument slot at [rsp+20h] that only the 3B5160 call at 567C7B uses.
//   The relay's stub stores rbp there ([rsp+28h] once the call has pushed its
//   return address) and jumps on, so the relay gets the caster as a fifth
//   argument. The whole body 567BD0..567CFA is checked.
//   The class id: 397290 turns an id into the row at tables+D78h + id x 150h,
//   below the count at tables+D80h, id 0 = none; TestTreasure prints a row's
//   name from +50h (40BD28), a string that runs to the end of the 150h-byte
//   row. The relay finds the class by name there, in the tables of the game's
//   data context (300A90, game+106h), on every find (a few thousand rows).
//
// ---------------------------------------------------------------------------
// Natives
// ---------------------------------------------------------------------------
//   2F5020 stat read (unit, stat, layer)       loader thunk
//   2F7D10 stat write (unit, stat, value, layer) loader thunk
//   2F4D20 max life (unit), 2F4E20 max mana (unit), loader thunks; proven by
//          their calls in the regen code (42E680, 42E385)
//   2F5940 a state's stat list (unit, state); 2F7920 remove a list from its
//          unit (unit, list); 2F4180 free it (context byte, list): the exact
//          sequence the regen code uses to end a potion state
//   2F5DF0 / 2F7C00 list stat read / write (context, list, stat, [value,]
//          layer), loader thunks; 2F7C00 proven by its call at 580005
//   34B9D0 unit type, 34A330 unit id, 48FE80 server unit by id
//   436340 drop an item by code at a unit; 441300 the treasure class drop;
//   43E8A0 a new item's level; 300A90 the data tables of a context
//   Game +0x106 data context byte, Game +0x170 frame counter.

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
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <random>
#include <string>
#include <string_view>
#include <vector>

namespace {

constexpr char PluginIdText[]  = "celestialrayone.alchemy";
constexpr char PluginVersion[] = "1.5.0";
constexpr char ServiceName[]   = "mana-burn-immunity";

const D2RL::PluginContext*    Context{};
const D2RL::DataTableService* TableSvc{};
std::uintptr_t                Base{};

// ---------------------------------------------------------------------------
// Addresses
// ---------------------------------------------------------------------------

constexpr std::uint64_t ItemUseRva     = 0x4F40C0;  // called, not hooked (Whirlwind hooks it)
constexpr std::uint64_t CheckStateRva  = 0x3351B0;
constexpr std::uint64_t IsDeadRva      = 0x34C2C0;
constexpr std::uint64_t CanUseRva      = 0x5817F0;
constexpr std::uint64_t UseEffectRva   = 0x581680;
constexpr std::uint64_t ServerUnitRva  = 0x48FE80;
constexpr std::uint64_t ReadStatRva    = 0x2F5020;
constexpr std::uint64_t WriteStatRva   = 0x2F7D10;
constexpr std::uint64_t MaxLifeRva     = 0x2F4D20;
constexpr std::uint64_t MaxManaRva     = 0x2F4E20;
constexpr std::uint64_t StateListRva   = 0x2F5940;
constexpr std::uint64_t RemoveListRva  = 0x2F7920;
constexpr std::uint64_t FreeListRva    = 0x2F4180;
constexpr std::uint64_t UnitTypeRva    = 0x34B9D0;
constexpr std::uint64_t UnitIdRva      = 0x34A330;
constexpr std::uint64_t ManaRegenRva   = 0x42E350;
constexpr std::uint64_t ReadListStatRva  = 0x2F5DF0;
constexpr std::uint64_t SetListStatRva   = 0x2F7C00;  // the list write the potion code itself uses
constexpr std::uint64_t ShrineRowCountRva = 0x38FE00;
constexpr std::uint64_t ShrineRecordRva  = 0x38FE40;
constexpr std::uint64_t ShrineChartRva   = 0x1D3F1D0;  // 24 x { handler, stat, state }

// The cooldown sweep (client side).
constexpr std::uint64_t RenderItemIconRva = 0x15BB80;   // (item, packed x|y<<32, float scale, params), inline hook
constexpr std::uint64_t IconSlotIndexRva  = 0x2A8AF28;  // int32: next slot of 15BB80's 250-slot sprite ring
constexpr std::uint64_t IconSlotsRva      = 0x2A8AF30;  // 250 x 32 bytes; +18h the GameSprite* just drawn
constexpr std::uint64_t LocalIndexRva     = 0x08B2D0;
constexpr std::uint64_t LocalPlayerRva    = 0x09A480;
constexpr std::uint64_t SpriteLoadRva     = 0x858990;   // (GameSprite*, name under data/hd/global/ui/, flags)
constexpr std::uint64_t SpriteDrawHdRva   = 0x79DA50;   // (hd sprite, int32 position[2], float scale, params)
constexpr std::int32_t  IconSlotCount     = 250;
constexpr std::size_t   IconSlotStride    = 32;
constexpr std::size_t   IconSlotSprite    = 0x18;
constexpr std::size_t   HdSpriteHeight    = 0xC0;       // 79DA50: quad height
constexpr std::size_t   HdSpriteFrames    = 0xCC;       // 79DA50: 1 / frames is one frame's share of U
constexpr std::size_t   HdSpriteWidth     = 0xD0;       // 79DA50: one frame's width (quad width)
constexpr std::size_t   HdSpriteTextureWidth = 0xBC;    // 79DA50: one frame's U span = frame width / this
constexpr std::size_t   HdSpriteAnchorX   = 0xC4;       // 79DA50: the quad starts at position - anchor x scale
constexpr std::size_t   HdSpriteAnchorY   = 0xC8;
constexpr std::size_t   HdSpriteTexture   = 0xE8;       // 79DA50: the texture its command holds
constexpr std::size_t   HdSpriteDrawBytes = 0xF0;       // up to and with the texture: all 79DA50 reads

// The sweep on uniques (client side): the item's quality and unique row, and
// uniqueitems.txt as the item's own game loaded it.
constexpr std::uint64_t ItemQualityRva    = 0x36CF60;   // (item) -> quality, ItemData+00h; 7 = unique
constexpr std::uint64_t ItemFileIndexRva  = 0x36CE70;   // (item) -> its uniqueitems.txt row, ItemData+34h
constexpr std::uint64_t ItemContextRva    = 0x34A0E0;   // (unit) -> data context byte, unit+1BDh
constexpr std::uint64_t DataTablesRva     = 0x300A90;   // (context 0..3) -> the data tables
constexpr std::size_t   TablesUniqueRows  = 0x13C8;     // uniqueitems.txt rows
constexpr std::size_t   TablesUniqueCount = 0x13D0;     // their count, a qword
constexpr std::size_t   UniqueRowBytes    = 348;
constexpr std::size_t   UniqueRowIndex    = 0x00;       // word: the row's own index
constexpr std::size_t   UniqueNameOffset  = 0x02;       // the index column
constexpr std::size_t   UniqueNameBytes   = 32;         // 31 characters and a NUL
constexpr std::uint32_t QualityUnique     = 7;

// Relayed calls: the 5-byte E8 at each site.
constexpr std::uint64_t UseItemCallRva    = 0x4C1447;  // message 26h handler -> 4F40C0
constexpr std::uint64_t QueuedUseCallRva  = 0x4F2417;  // queued use -> 4F40C0
constexpr std::uint64_t LifeWriteCallRva  = 0x44D093;
constexpr std::uint64_t HealthPotCallRva  = 0x42E693;
constexpr std::uint64_t ManaPotCallRva    = 0x42E4F2;
constexpr std::uint64_t ManaRegenCallRva  = 0x42E74D;

// Find Potion (server side): srvdofunc 69 and the treasure class drop.
constexpr std::uint64_t FindPotionRva       = 0x567BD0;   // srvdofunc 69, checked whole (567BD0..567CFA)
constexpr std::uint64_t FindPotionCallRva   = 0x567CD4;   // call 436340: the found potion's drop
constexpr std::uint64_t DropItemWithCodeRva = 0x436340;   // (game, unit, code, quality) -> item
constexpr std::uint64_t GenerateDropsRva    = 0x441300;   // TREASURECLASS_GenerateDrops
constexpr std::uint64_t ItemLevelRva        = 0x43E8A0;   // (unit, level id) -> a new item's level
constexpr std::uint64_t TreasureLookupRva   = 0x3972AA;   // inside 397290, class id -> row
constexpr std::uint64_t TreasureNameSiteRva = 0x40BD1E;   // TestTreasure printing a class name
constexpr std::uint64_t DropCapRva          = 0x4405E5;   // 4404F0: the count and the item cap
constexpr std::uint64_t DropStoreRva        = 0x44113E;   // 4404F0: the array store and the cap check
constexpr std::size_t   TablesTreasureRows  = 0xD78;      // treasureclassex.txt runtime rows
constexpr std::size_t   TablesTreasureCount = 0xD80;      // their count, a qword
constexpr std::size_t   TreasureRowBytes    = 0x150;
constexpr std::size_t   TreasureNameOffset  = 0x50;
constexpr std::size_t   TreasureNameBytes   = TreasureRowBytes - TreasureNameOffset;   // the name ends the row
constexpr std::uint64_t TreasureIdLimit     = 0xFFFF;     // ids are words; 0 = none

constexpr std::size_t GameContextOffset = 0x106;
constexpr std::size_t GameFrameOffset   = 0x170;

constexpr std::size_t RequestConsumed = 0x01;
constexpr std::size_t RequestPlayerId = 0x02;
constexpr std::size_t RequestTarget   = 0x06;
constexpr std::size_t RequestItemId   = 0x0A;
constexpr std::size_t RequestLocation = 0x0E;   // 0 inventory, 2 belt
constexpr std::size_t UnitClassOffset = 0x04;

constexpr std::int32_t StateUninterruptible = 54;
constexpr std::int32_t UnitPlayer = 0;
constexpr std::int32_t UnitItem   = 4;
constexpr std::int32_t StatLife    = 6;
constexpr std::int32_t StatMana    = 8;
constexpr std::int32_t LethalBelow = 256;   // life is in 1/256 points

constexpr std::size_t ChartStride       = 16;
constexpr std::size_t ChartSlots        = 24;
constexpr std::size_t OperateBytes      = 0x28;  // game, source, player, object control, row
constexpr std::size_t OperateGame       = 0x00;
constexpr std::size_t OperateSource     = 0x08;
constexpr std::size_t OperatePlayer     = 0x10;
constexpr std::size_t OperateRow        = 0x20;
constexpr std::size_t ShrineRecordCode  = 0x00;

// ---------------------------------------------------------------------------
// Witnesses
// ---------------------------------------------------------------------------

constexpr std::uint8_t ItemUseBytes[]{
    0x40,0x55,0x53,0x56,0x57,0x41,0x54,0x41,0x56,0x48,0x8D,0xAC,0x24,0x58,0xFF,0xFF,
    0xFF,0x48,0x81,0xEC,0xA8,0x01,0x00,0x00 };
constexpr std::uint8_t CheckStateBytes[]{
    0x48,0x89,0x5C,0x24,0x08,0x48,0x89,0x74,0x24,0x10,0x57,0x48,0x83,0xEC,0x20,0x8B,0xDA,0x48,0x8B,0xF1 };
constexpr std::uint8_t IsDeadBytes[]{
    0x48,0x83,0xEC,0x28,0x48,0x85,0xC9,0x75,0x1D,0x88,0x4C,0x24,0x30,0x48,0x8D,0x4C,0x24,0x30,0xE8,0x59,
    0x94,0xFF,0xFF,0x84,0xC0,0x74 };
// 57FFF3: the elixir handler (pSpell 9) writing a stat on its state's list:
// movzx ecx, byte [rbp+106h] / mov r9d, edi / mov rdx, r14 / nop / mov [rsp+20h], eax / call 2F7C00.
constexpr std::uint8_t PotionListWriteSite[]{ 0x0F,0xB6,0x8D,0x06,0x01,0x00,0x00,0x44,0x8B,0xCF,0x49,0x8B,0xD6,0x90,
                                              0x89,0x44,0x24,0x20,0xE8,0xF6,0x7B,0xD7,0xFF };
// 4C143C: mov rdx, rbp / mov [rsp+28h], rax / mov rcx, rbx / call 4F40C0 / cmp eax, 4.
constexpr std::uint8_t UseItemSite[]{ 0x48,0x8B,0xD5,0x48,0x89,0x44,0x24,0x28,0x48,0x8B,0xCB,0xE8,0x74,0x2C,0x03,
                                      0x00,0x83,0xF8,0x04 };
// 4F2409: mov rcx, [rbx+10h] / lea r8, [rbx+18h] / mov r9b, 1 / mov rdx, rax / call 4F40C0.
constexpr std::uint8_t QueuedUseSite[]{ 0x48,0x8B,0x4B,0x10,0x4C,0x8D,0x43,0x18,0x41,0xB1,0x01,0x48,0x8B,0xD0,0xE8,
                                        0xA4,0x1C,0x00,0x00 };
constexpr std::uint8_t CanUseBytes[]{
    0x48,0x89,0x5C,0x24,0x08,0x48,0x89,0x6C,0x24,0x10,0x48,0x89,0x74,0x24,0x18,0x57,
    0x41,0x56,0x41,0x57,0x48,0x83,0xEC,0x30 };
constexpr std::uint8_t UseEffectBytes[]{
    0x48,0x89,0x5C,0x24,0x18,0x48,0x89,0x6C,0x24,0x20,0x56,0x57,0x41,0x57,0x48,0x83,
    0xEC,0x30,0x48,0x8B,0xEA,0x48,0x8B,0xF1 };
constexpr std::uint8_t ServerUnitBytes[]{
    0x48,0x89,0x5C,0x24,0x08,0x48,0x89,0x74,0x24,0x18,0x57,0x48,0x83,0xEC,0x20,0x41,
    0x8B,0xD8,0x8B,0xF2,0x48,0x8B,0xF9,0x48,0x85,0xC9 };
constexpr std::uint8_t ThunkHead[]{ 0xFF,0x25 };
constexpr std::uint8_t StateListBytes[]{
    0x48,0x89,0x5C,0x24,0x10,0x57,0x48,0x83,0xEC,0x20,0x8B,0xDA,0x48,0x8B,0xF9,0x48,
    0x85,0xC9,0x75,0x13 };
constexpr std::uint8_t RemoveListBytes[]{ 0x40,0x53,0x48,0x83,0xEC,0x20,0x48,0x8B,0xDA,0xE8,0xB2,0x27,0x05,0x00 };
constexpr std::uint8_t FreeListBytes[]{ 0x48,0x85,0xD2,0x74,0x0A,0x83,0x7A,0x1C,0x00,0x0F,0x8D,0x21,0x5D,0x00,0x00 };
constexpr std::uint8_t UnitTypeBytes[]{
    0x48,0x83,0xEC,0x28,0x48,0x85,0xC9,0x75,0x1D,0x88,0x4C,0x24,0x30,0x48,0x8D,0x4C,
    0x24,0x30,0xE8,0x39,0x9E,0xFF,0xFF,0x84,0xC0,0x74,0x01,0xCC,0xB8,0x06,0x00,0x00,
    0x00,0x48,0x83,0xC4,0x28,0xC3,0x8B,0x01,0x48,0x83,0xC4,0x28,0xC3 };
constexpr std::uint8_t UnitIdBytes[]{
    0x48,0x83,0xEC,0x28,0x48,0x85,0xC9,0x75,0x1D,0x88,0x4C,0x24,0x30,0x48,0x8D,0x4C,
    0x24,0x30,0xE8,0x39,0xCA,0xFF,0xFF,0x84,0xC0,0x74,0x01,0xCC,0xB8,0xFF,0xFF,0xFF,
    0xFF,0x48,0x83,0xC4,0x28,0xC3,0x8B,0x41,0x08,0x48,0x83,0xC4,0x28,0xC3 };
// 42E67A: mov rcx, rbx / lea edi, [rsi+rax] / call 2F4D20 / cmp edi, eax: max life.
constexpr std::uint8_t MaxLifeCallBytes[]{ 0x48,0x8B,0xCB,0x8D,0x3C,0x06,0xE8,0x9B,0x66,0xEC,0xFF,0x3B,0xF8 };
// 42E385: call 2F4E20 / xor ebx, ebx / mov rcx, rdi / mov ebp, eax: max mana.
constexpr std::uint8_t MaxManaCallBytes[]{ 0xE8,0x96,0x6A,0xEC,0xFF,0x33,0xDB,0x48,0x8B,0xCF,0x8B,0xE8 };
// 38FE00..38FE34: data tables, then the shrines.txt count at +19B8h.
constexpr std::uint8_t ShrineRowCountBytes[]{
    0x40,0x53,0x48,0x83,0xEC,0x20,0xE8,0x85,0x0C,0xF7,0xFF,0x48,0x8B,0x98,0xB8,0x19,0x00,0x00,0x8B,0xCB,
    0x48,0x3B,0xCB,0x74,0x14,0x48,0x8D,0x4C,0x24,0x38,0xC6,0x44,0x24,0x38,0x00,0xE8,0x08,0xE4,0xD1,0xFF,
    0x84,0xC0,0x74,0x01,0xCC,0x8B,0xC3,0x48,0x83,0xC4,0x20,0x5B,0xC3 };
// 38FE40: bounds check against +19B8h, base at +19B0h, stride 1Ch after it.
constexpr std::uint8_t ShrineRecordBytes[]{
    0x48,0x89,0x5C,0x24,0x08,0x48,0x89,0x74,0x24,0x20,0x57,0x48,0x83,0xEC,0x30,0x8B,0xDA,0xE8,0x3A,0x0C,
    0xF7,0xFF,0x48,0x8B,0xF0,0x8B,0xFB,0x48,0x3B,0x98,0xB8,0x19,0x00,0x00,0x72,0x14,0x48,0x8D,0x4C,0x24,
    0x48,0xC6,0x44,0x24,0x48,0x00,0xE8,0x1D,0xF6,0xFF,0xFF,0x84,0xC0,0x74,0x01,0xCC,0x48,0x8D,0x9E,0xB0,
    0x19,0x00,0x00 };
// 44D086: cmp eax, 100h / cmovge r8d, eax / lea edx, [r9+6] / call 2F7D10.
constexpr std::uint8_t LifeWriteSite[]{ 0x3D,0x00,0x01,0x00,0x00,0x44,0x0F,0x4D,0xC0,0x41,0x8D,0x51,0x06,
                                        0xE8,0x78,0xAC,0xEA,0xFF };
// 42E689: mov edx, 100 / mov rcx, rbx / mov edi, eax / call 2F5940.
constexpr std::uint8_t HealthPotSite[]{ 0xBA,0x64,0x00,0x00,0x00,0x48,0x8B,0xCB,0x8B,0xF8,0xE8,0xA8,0x72,0xEC,0xFF };
// 42E4EA: mov edx, 106 / mov rcx, rdi / call 2F5940.
constexpr std::uint8_t ManaPotSite[]{ 0xBA,0x6A,0x00,0x00,0x00,0x48,0x8B,0xCF,0xE8,0x49,0x74,0xEC,0xFF };
// 42E747: mov rdx, rbx / mov rcx, rbp / call 42E350.
constexpr std::uint8_t ManaRegenSite[]{ 0x48,0x8B,0xD3,0x48,0x8B,0xCD,0xE8,0xFE,0xFB,0xFF,0xFF };

// 15BB80: push rbx, rbp, rsi / sub rsp,140h / movaps [rsp+120h],xmm6 (19 bytes, then the cookie load).
constexpr std::uint8_t RenderItemIconBytes[]{
    0x40,0x53,0x55,0x56,0x48,0x81,0xEC,0x40,0x01,0x00,0x00,0x0F,0x29,0xB4,0x24,0x20,0x01,0x00,0x00 };
// 15BC11: movsxd rdi, [2A8AF28] / lea r14, [2A8AF30] / shl rdi, 5: the slot index and the ring.
constexpr std::uint8_t IconSlotSiteBytes[]{
    0x48,0x63,0x3D,0x10,0xF3,0x92,0x02,0x4C,0x8D,0x35,0x11,0xF3,0x92,0x02,0x48,0xC1,0xE7,0x05 };
constexpr std::uint8_t LocalIndexBytes[]{ 0x8B,0x05,0x2E,0x84,0x99,0x02,0xC3 };
constexpr std::uint8_t LocalPlayerBytes[]{
    0x48,0x89,0x5C,0x24,0x08,0x57,0x48,0x83,0xEC,0x20,0x83,0xF9,0x08,0x0F,0x83,0x85,
    0x00,0x00,0x00,0x8B,0xD9,0x48,0x89,0x5C,0x24,0x38,0x48,0x83,0xFB,0x08,0x72,0x19 };
constexpr std::uint8_t SpriteLoadBytes[]{
    0x40,0x53,0x56,0x57,0x48,0x81,0xEC,0x40,0x01,0x00,0x00,0x48,0x8B,0x05,0x26,0x29,
    0x17,0x02,0x48,0x33,0xC4,0x48,0x89,0x84,0x24,0x30,0x01,0x00,0x00,0x48,0x8B,0xD9 };
// 36CF60 ITEMS_GetItemQuality: push rbx / sub rsp, 20h / mov rbx, rcx / test rcx, rcx / jz / call 34B9D0 / cmp eax, 4 / jz.
constexpr std::uint8_t ItemQualityBytes[]{
    0x40,0x53,0x48,0x83,0xEC,0x20,0x48,0x8B,0xD9,0x48,0x85,0xC9,0x74,0x0A,0xE8,0x5D,0xEA,0xFD,0xFF,0x83,0xF8,0x04,
    0x74,0x19 };
// 36CE70: an item's file index (ItemData+34h), its uniqueitems.txt row for a unique.
constexpr std::uint8_t ItemFileIndexBytes[]{
    0x40,0x53,0x48,0x83,0xEC,0x20,0x48,0x8B,0xD9,0x48,0x85,0xC9,0x75,0x13,0x88,0x4C,0x24,0x30,0x48,0x8D,0x4C,0x24,
    0x30,0xE8,0xB4,0xB7,0xFF,0xFF };
// 34A0E0: a unit's data context byte (unit+1BDh).
constexpr std::uint8_t ItemContextBytes[]{
    0x48,0x83,0xEC,0x28,0x48,0x85,0xC9,0x75,0x1A,0x88,0x4C,0x24,0x30,0x48,0x8D,0x4C,0x24,0x30,0xE8,0x49,0xC7,0xFF,
    0xFF };
// 300A90: sub rsp, 28h / movzx eax, cl / mov [rsp+38h], rax / cmp rax, 4 / jb: the tables of context 0..3.
constexpr std::uint8_t DataTablesBytes[]{
    0x48,0x83,0xEC,0x28,0x0F,0xB6,0xC1,0x48,0x89,0x44,0x24,0x38,0x48,0x83,0xF8,0x04,0x72,0x19 };
// 0x31F1D0, the uniqueitems.txt compiler. 31F205: mov r15, rax (the tables) / mov dword [rbp-48h], 1 /
// xor r12d, r12d / mov dword [rbp-44h], 31 / lea rax, "index" / mov qword [rbp-40h], 2 / mov [rbp-50h], rax:
// the index column, a 31-character string at +02h.
constexpr std::uint8_t UniqueNameSiteBytes[]{
    0x4C,0x8B,0xF8,0xC7,0x45,0xB8,0x01,0x00,0x00,0x00,0x45,0x33,0xE4,0xC7,0x45,0xBC,0x1F,0x00,0x00,0x00,0x48,0x8D,
    0x05,0x64,0xC1,0x9B,0x01,0x48,0xC7,0x45,0xC0,0x02,0x00,0x00,0x00,0x48,0x89,0x45,0xB0 };
// 31FF2C: lea r14, [r15+13C8h], the rows.
constexpr std::uint8_t UniqueRowsSiteBytes[]{ 0x4D,0x8D,0xB7,0xC8,0x13,0x00,0x00 };
// 31FFA7: cmp [r15+13D0h], r12, the count.
constexpr std::uint8_t UniqueCountSiteBytes[]{ 0x4D,0x39,0xA7,0xD0,0x13,0x00,0x00 };
// 31FFE9: imul rsi, [rsp+78h], 15Ch / add rsi, [r14] / mov [rsi], di: 348-byte rows, the row index at +00h.
constexpr std::uint8_t UniqueStrideSiteBytes[]{ 0x48,0x69,0x74,0x24,0x78,0x5C,0x01,0x00,0x00,0x49,0x03,0x36,0x66,0x89 };
// 79DA50 ... movaps xmm8, xmm2 / mov rsi, r9: the scale and the params.
constexpr std::uint8_t SpriteDrawHdBytes[]{
    0x48,0x89,0x5C,0x24,0x18,0x55,0x56,0x41,0x56,0x48,0x8D,0xAC,0x24,0x70,0xFF,0xFF,
    0xFF,0x48,0x81,0xEC,0x90,0x01,0x00,0x00,0x44,0x0F,0x29,0x84,0x24,0x60,0x01,0x00,
    0x00,0x48,0x8B,0x05,0x50,0xD8,0x22,0x02,0x48,0x33,0xC4,0x48,0x89,0x45,0xE0,0x44,
    0x0F,0x28,0xC2,0x49,0x8B,0xF1 };

struct Witness {
    std::uint64_t       rva;
    const std::uint8_t* bytes;
    std::uint32_t       size;
    bool                callable;   // a function this plugin calls; another plugin may hook it
    const char*         name;
};

#define W(rva, bytes, callable, name) Witness{ rva, bytes, static_cast<std::uint32_t>(sizeof(bytes)), callable, name }
constexpr Witness Witnesses[]{
    W(ItemUseRva, ItemUseBytes, true, "use-item handler"),
    W(CheckStateRva, CheckStateBytes, true, "state check"),
    W(IsDeadRva, IsDeadBytes, true, "dead check"),
    W(UseItemCallRva - 11, UseItemSite, false, "use-item call in the message handler"),
    W(QueuedUseCallRva - 14, QueuedUseSite, false, "use-item call for a queued use"),
    W(CanUseRva, CanUseBytes, true, "can-use check"),
    W(UseEffectRva, UseEffectBytes, true, "item effect dispatcher"),
    W(ServerUnitRva, ServerUnitBytes, true, "server unit lookup"),
    W(ReadStatRva, ThunkHead, false, "stat read"),
    W(WriteStatRva, ThunkHead, false, "stat write"),
    W(MaxLifeRva, ThunkHead, false, "max life"),
    W(MaxManaRva, ThunkHead, false, "max mana"),
    W(StateListRva, StateListBytes, true, "state list lookup"),
    W(ReadListStatRva, ThunkHead, false, "list stat read"),
    W(SetListStatRva, ThunkHead, false, "list stat write"),
    W(0x57FFF3, PotionListWriteSite, false, "elixir handler's list stat write"),
    W(ShrineRowCountRva, ShrineRowCountBytes, true, "shrines.txt row count"),
    W(ShrineRecordRva, ShrineRecordBytes, true, "shrines.txt record"),
    W(RemoveListRva, RemoveListBytes, true, "state list removal"),
    W(FreeListRva, FreeListBytes, true, "state list free"),
    W(UnitTypeRva, UnitTypeBytes, true, "unit type"),
    W(UnitIdRva, UnitIdBytes, true, "unit id"),
    W(0x42E67A, MaxLifeCallBytes, false, "max life call in the player regen"),
    W(0x42E385, MaxManaCallBytes, false, "max mana call in the mana regen"),
    W(LifeWriteCallRva - 13, LifeWriteSite, false, "life write after a hit"),
    W(HealthPotCallRva - 10, HealthPotSite, false, "Healing Potion state lookup"),
    W(ManaPotCallRva - 8, ManaPotSite, false, "Mana Potion state lookup"),
    W(ManaRegenCallRva - 6, ManaRegenSite, false, "mana regen call"),
};
#undef W

#define W(rva, bytes, callable, name) Witness{ rva, bytes, static_cast<std::uint32_t>(sizeof(bytes)), callable, name }
constexpr Witness SweepWitnesses[]{
    W(RenderItemIconRva, RenderItemIconBytes, false, "item icon renderer"),
    W(0x15BC11, IconSlotSiteBytes, false, "item icon sprite ring"),
    W(LocalIndexRva, LocalIndexBytes, true, "local data context"),
    W(LocalPlayerRva, LocalPlayerBytes, true, "local player"),
    W(SpriteLoadRva, SpriteLoadBytes, true, "sprite loader"),
    W(SpriteDrawHdRva, SpriteDrawHdBytes, true, "HD sprite draw"),
    W(ItemQualityRva, ItemQualityBytes, true, "item quality"),
    W(ItemFileIndexRva, ItemFileIndexBytes, true, "item file index"),
    W(ItemContextRva, ItemContextBytes, true, "unit data context"),
    W(DataTablesRva, DataTablesBytes, true, "data tables of a context"),
    W(0x31F205, UniqueNameSiteBytes, false, "uniqueitems.txt index column"),
    W(0x31FF2C, UniqueRowsSiteBytes, false, "uniqueitems.txt rows"),
    W(0x31FFA7, UniqueCountSiteBytes, false, "uniqueitems.txt row count"),
    W(0x31FFE9, UniqueStrideSiteBytes, false, "uniqueitems.txt row size"),
};
#undef W

// 567BD0..567CFA: srvdofunc 69, the whole body. 567BE8 mov rbp, rdx (the caster; nothing
// writes rbp again before the potion drop), sub rsp, 30h: [rsp+20h] is a fifth-argument slot that
// only the 3B5160 call at 567C7B uses. 567CD4 call 436340, the potion drop, then the epilogue.
constexpr std::uint8_t FindPotionBytes[]{
    0x48,0x89,0x5C,0x24,0x10,0x48,0x89,0x6C,0x24,0x18,0x48,0x89,0x74,0x24,0x20,0x57,0x41,0x56,0x41,0x57,
    0x48,0x83,0xEC,0x30,0x48,0x8B,0xEA,0x48,0x8B,0xF1,0x0F,0xB6,0x89,0x06,0x01,0x00,0x00,0x41,0x8B,0xD0,
    0x45,0x8B,0xF9,0x45,0x8B,0xF0,0xE8,0x8D,0xFB,0xB2,0xFF,0x48,0x8B,0xD8,0x48,0x85,0xC0,0x0F,0x84,0xD1,
    0x00,0x00,0x00,0x48,0x8B,0xD5,0x48,0x8B,0xCE,0xE8,0x06,0x82,0xF2,0xFF,0x48,0x8B,0xF8,0x48,0x85,0xC0,
    0x0F,0x84,0xBA,0x00,0x00,0x00,0x48,0x8B,0xC8,0xE8,0xE2,0x28,0xDD,0xFF,0x85,0xC0,0x0F,0x84,0xAA,0x00,
    0x00,0x00,0xBA,0x76,0x00,0x00,0x00,0x48,0x8B,0xCF,0xE8,0x6D,0xD5,0xDC,0xFF,0x85,0xC0,0x0F,0x85,0x95,
    0x00,0x00,0x00,0x8D,0x50,0x76,0x48,0x8B,0xCF,0x44,0x8D,0x40,0x01,0xE8,0x66,0xD8,0xDC,0xFF,0x48,0x8B,
    0xCF,0xE8,0x8E,0x6F,0xE2,0xFF,0x44,0x8B,0x83,0x90,0x01,0x00,0x00,0x45,0x8B,0xCE,0x0F,0xB6,0x8E,0x06,
    0x01,0x00,0x00,0x48,0x8B,0xD5,0x44,0x89,0x7C,0x24,0x20,0xE8,0xE0,0xD4,0xE4,0xFF,0x48,0x8B,0xCD,0x8B,
    0xD8,0xE8,0x56,0x25,0xDE,0xFF,0x48,0x8B,0xC8,0xBA,0x64,0x00,0x00,0x00,0xE8,0x69,0xBE,0xBE,0xFF,0x3B,
    0xC3,0x7D,0x3E,0x45,0x8B,0xCF,0x45,0x8B,0xC6,0x48,0x8B,0xD5,0x48,0x8B,0xCE,0xE8,0xE4,0x31,0x00,0x00,
    0x8B,0xD8,0x85,0xC0,0x75,0x13,0x48,0x8D,0x4C,0x24,0x50,0x88,0x44,0x24,0x50,0xE8,0x10,0xE7,0xFF,0xFF,
    0x84,0xC0,0x74,0x01,0xCC,0x41,0xB9,0x02,0x00,0x00,0x00,0x44,0x8B,0xC3,0x48,0x8B,0xD7,0x48,0x8B,0xCE,
    0xE8,0x67,0xE6,0xEC,0xFF,0xB8,0x01,0x00,0x00,0x00,0xEB,0x02,0x33,0xC0,0x48,0x8B,0x5C,0x24,0x58,0x48,
    0x8B,0x6C,0x24,0x60,0x48,0x8B,0x74,0x24,0x68,0x48,0x83,0xC4,0x30,0x41,0x5F,0x41,0x5E,0x5F,0xC3 };
// 436340 (game, unit, code, quality): rdi = unit, rbx = game, edx = code, r14d = quality.
constexpr std::uint8_t DropItemWithCodeBytes[]{
    0x40,0x55,0x53,0x56,0x57,0x41,0x56,0x48,0x8D,0x6C,0x24,0xE0,0x48,0x81,0xEC,0x20,0x01,0x00,0x00,0x48,
    0x8B,0x05,0x6E,0x4F,0x59,0x02,0x48,0x33,0xC4,0x48,0x89,0x45,0x10,0x48,0x8B,0xFA,0x48,0x8B,0xD9,0x0F,
    0xB6,0x89,0x06,0x01,0x00,0x00,0x41,0x8B,0xD0,0x45,0x8B,0xF1 };
// 441300 TREASURECLASS_GenerateDrops: r15 = item array (8th), r12 = count (9th), ebp = class id (r9w), r14 = killer.
constexpr std::uint8_t GenerateDropsBytes[]{
    0x40,0x53,0x55,0x56,0x57,0x41,0x54,0x41,0x55,0x41,0x56,0x41,0x57,0x48,0x81,0xEC,0x98,0x00,0x00,0x00,
    0x48,0x8B,0x05,0xAD,0x9F,0x58,0x02,0x48,0x33,0xC4,0x48,0x89,0x84,0x24,0x88,0x00,0x00,0x00,0x4C,0x8B,
    0xBC,0x24,0x18,0x01,0x00,0x00,0x45,0x33,0xED,0x4C,0x8B,0xA4,0x24,0x20,0x01,0x00,0x00,0x41,0x0F,0xB7,
    0xE9,0x4D,0x8B,0xF0 };
// 43E8A0 (unit, level id): a monster gets its level, stat 12, the level 436340 gives a found potion.
constexpr std::uint8_t ItemLevelBytes[]{
    0x48,0x89,0x5C,0x24,0x08,0x57,0x48,0x83,0xEC,0x20,0x8B,0xFA,0x48,0x8B,0xD9,0xE8,0x1C,0xD1,0xF0,0xFF,
    0x83,0xF8,0x01,0x75,0x10,0x45,0x33,0xC0,0x8D,0x50,0x0B,0x48,0x8B,0xCB,0xE8,0x59,0x67,0xEB,0xFF,0xEB,
    0x53 };
// 3972AA, inside 397290 (class id -> row): count at tables+D80h, rows at tables+D78h, 150h bytes a row, id 0 = none.
constexpr std::uint8_t TreasureLookupBytes[]{
    0x48,0x3B,0xA8,0x80,0x0D,0x00,0x00,0x72,0x14,0x48,0x8D,0x4C,0x24,0x58,0xC6,0x44,0x24,0x58,0x00,0xE8,
    0xBE,0xBB,0xFF,0xFF,0x84,0xC0,0x74,0x01,0xCC,0x66,0x85,0xED,0x0F,0x84,0x8D,0x00,0x00,0x00,0x48,0x3B,
    0xAF,0x80,0x0D,0x00,0x00,0x0F,0x83,0x80,0x00,0x00,0x00,0x48,0x81,0xC7,0x78,0x0D,0x00,0x00,0x48,0x89,
    0x6C,0x24,0x68,0x48,0x3B,0x6F,0x08,0x72,0x1E,0x48,0x8D,0x44,0x24,0x68,0x48,0x89,0x7C,0x24,0x28,0x48,
    0x8D,0x4C,0x24,0x20,0x48,0x89,0x44,0x24,0x20,0xE8,0xD8,0xC9,0xFF,0xFF,0x84,0xC0,0x74,0x01,0xCC,0x4C,
    0x69,0x44,0x24,0x68,0x50,0x01,0x00,0x00,0x4C,0x03,0x07 };
// 40BD1E, TestTreasure: call 397290 / test rax, rax / jz / add rax, 50h: the class name it prints is at row+50h.
constexpr std::uint8_t TreasureNameSiteBytes[]{
    0xE8,0x6D,0xB5,0xF8,0xFF,0x48,0x85,0xC0,0x74,0x1E,0x48,0x83,0xC0,0x50,0x48,0x8D,0x15,0xCD,0x86,0x90,
    0x01,0x44,0x8B,0xCB,0x48,0x89,0x44,0x24,0x20 };
// 4405E5, the roll 4404F0: no count pointer -> its own; an item array -> the cap is max items as given
// (no array: max items, or 6 when 0).
constexpr std::uint8_t DropCapBytes[]{
    0x48,0x85,0xFF,0x48,0x8D,0x45,0x98,0x48,0x0F,0x45,0xC7,0x48,0x89,0x45,0x90,0x44,0x89,0x28,0x8B,0x85,
    0x48,0x09,0x00,0x00,0x89,0x44,0x24,0x68,0x4D,0x85,0xF6,0x75,0x0E,0x85,0xC0,0x7F,0x22,0xC7,0x44,0x24,
    0x68,0x06,0x00,0x00,0x00,0xEB,0x18,0x85,0xC0,0x75,0x14,0x48,0x8D,0x4C,0x24,0x45,0x44,0x88,0x6C,0x24,
    0x45,0xE8,0xA9,0xB6,0xFF,0xFF,0x84,0xC0,0x74,0x01,0xCC };
// 44113E, the roll 4404F0 after each item: array[count] = item, ++count, stop at the cap.
constexpr std::uint8_t DropStoreBytes[]{
    0x48,0x8B,0x4D,0xA0,0x4C,0x8B,0x6D,0x90,0x48,0x85,0xC9,0x74,0x08,0x49,0x63,0x45,0x00,0x48,0x89,0x3C,
    0xC1,0x41,0xFF,0x45,0x00,0x8B,0x44,0x24,0x68,0x41,0x39,0x45,0x00,0x0F,0x8D,0x0E,0x01,0x00,0x00 };

#define W(rva, bytes, callable, name) Witness{ rva, bytes, static_cast<std::uint32_t>(sizeof(bytes)), callable, name }
constexpr Witness FindPotionWitnesses[]{
    W(FindPotionRva, FindPotionBytes, false, "srvdofunc 69 (Find Potion)"),
    W(DropItemWithCodeRva, DropItemWithCodeBytes, true, "item drop by code"),
    W(GenerateDropsRva, GenerateDropsBytes, true, "treasure class drop"),
    W(ItemLevelRva, ItemLevelBytes, true, "new item level"),
    W(DataTablesRva, DataTablesBytes, true, "data tables of a context"),
    W(TreasureLookupRva, TreasureLookupBytes, false, "treasure class row lookup"),
    W(TreasureNameSiteRva, TreasureNameSiteBytes, false, "treasure class name in TestTreasure"),
    W(DropCapRva, DropCapBytes, false, "treasure class roll item cap"),
    W(DropStoreRva, DropStoreBytes, false, "treasure class roll item store"),
};
#undef W

// ---------------------------------------------------------------------------
// Settings
// ---------------------------------------------------------------------------

constexpr std::int32_t NoStat = -1;
constexpr std::size_t  MaxReincarnations = 8;   // [[reincarnation]] entries read from the config

// One [[reincarnation]] entry: a state that saves a player from a lethal hit.
struct ReincarnationEntry {
    std::int32_t             state = -1;
    std::int32_t             cooldownStat = NoStat;
    std::int32_t             cooldownSeconds = 300;
    std::int32_t             invulnerableFrames = 25;
    std::vector<std::string> sweepCodes;            // items whose icon shows this entry's cooldown
    std::vector<std::string> sweepUniques;          // uniqueitems.txt index names, the same
    bool                     stateGiven = false;
    bool                     sweepGiven = false;
    // Worked out once the config is read (FinishReincarnation):
    std::size_t              owner = 0;             // the entry running its countdown: the first with its cooldown stat
    std::int32_t             longest = 300;         // the longest cooldown_seconds of the entries sharing that stat
};

// One [find_potion] line: a potion Find Potion picks, and the treasure class that drops instead.
struct FindPotionClass {
    std::string code;   // misc.txt code
    std::string name;   // treasureclassex.txt Treasure Class
};
constexpr std::size_t MaxFindPotionClasses = 32;

struct SettingsData {
    bool         enabled = true;
    std::int32_t blessedVials = 933;
    std::int32_t savvyElixirs = 936;
    std::int32_t endlessVials = 937;
    std::int32_t bottomlessVials = 940;
    std::int32_t compoundedSynergy = 935;
    std::int32_t serendipity = 941;
    std::int32_t blessedReduction = 5;
    std::int32_t savvyChance = 25;
    std::int32_t synergyPercent = 5;
    std::int32_t serendipityChance = 15;
    std::vector<std::int32_t> serendipityShrines{ 1, 6, 7, 8, 9, 10, 11, 12, 13, 15 };
    std::int32_t healingState = 100;
    std::int32_t manaState = 106;
    std::vector<ReincarnationEntry> reincarnation{ ReincarnationEntry{ .state = 677, .cooldownStat = 945,
        .cooldownSeconds = 300, .invulnerableFrames = 25, .sweepCodes = { "35p" }, .sweepUniques = {},
        .stateGiven = true } };
    std::vector<std::string> healingManaCodes{ "hp1","hp2","hp3","hp4","hp5","mp1","mp2","mp3","mp4","mp5" };
    std::vector<std::string> utilityCodes{
        "01p","02p","03p","04p","05p","06p","07p","08p","09p","10p","11p","12p","13p","14p","15p","16p","17p",
        "18p","19p","20p","21p","22p","23p","24p","25p","26p","27p","28p","29p","30p","31p","32p","33p","34p",
        "vps","yps","wms" };
    std::vector<std::string> rejuvenationCodes{ "rvs", "rvl" };
    bool                     sweepEnabled = true;
    std::string              sweepSprite = "PANEL/CrossbowCharges/Sweep";
    std::int32_t             sweepColor[3]{ 0, 0, 0 };
    std::int32_t             sweepOpacity = 45;
    bool                         findPotionEnabled = true;
    std::vector<FindPotionClass> findPotionClasses{ FindPotionClass{ "rvs", "Rejuvenation Regular" },
                                                    FindPotionClass{ "rvl", "Rejuvenation Full" } };
};
SettingsData Settings;
std::string  ConfigProblems;

constexpr char DefaultConfigToml[] = R"toml(# Alchemy (celestialrayone.alchemy)
#
# The Alchemist perks that need engine code. Each perk is the saved perk stat
# the Specializations plugin writes; a perk works while its stat is above 0.
# Set a perk stat to -1 to switch that perk off.
#
#   Blessed Vials       While a Healing Potion effect runs: less damage taken.
#                       Shrine Rework applies it at the same point as the Armor
#                       shrine's reduction, so it needs Shrine Rework 1.2.0 with
#                       its Armor effect on. While a Mana Potion effect runs:
#                       immune to Mana Burn, which needs Monster Affixes 1.6.0.
#   Compounded Synergy  Every potion effect is stronger by
#                       compounded_synergy_percent for each different potion
#                       effect running on the player, itself included. Effects
#                       already running grow and shrink as other potion effects
#                       start and end. The potion effects are the states of the
#                       healing_mana and utility items below. Rejuvenation
#                       potions heal that much more too.
#   Savvy Elixirs       A utility potion has a chance not to be used up; its
#                       effect still applies.
#   Endless Vials       Healing and Mana Potion effects no longer end when life
#                       or mana is full; they run their whole duration.
#   Bottomless Vials    Healing and Mana Potions are never used up.
#   Serendipity         A utility potion has a chance to also give the effect of
#                       one random shrine from [serendipity] shrine_codes.
#
# Elixir of Reincarnation needs no perk stat here, and other states can work
# the same way: each [[reincarnation]] block below is one state. While an
# entry's state is on a player, a hit that would kill instead restores full
# life and mana, removes that state (and the stats on it), makes the player
# invulnerable for a moment and starts the entry's cooldown: its cooldown stat
# holds the seconds left and counts down to 0, also after a reload when the
# stat is saved. While the cooldown runs, the state saves nobody, even when
# something gives it again, so a state from a skill proc works too.
# misc.txt's UsageConditionCalc does the rest for potions; [cooldown_sweep]
# shows the cooldown on the entry's sweep_items and sweep_uniques.
#
# [find_potion] sends the potion Find Rejuvenation finds through a treasure
# class, so the treasureclassex.txt perks (Unstable Mixtures) reach it too.
#
# Console: "alchemy" prints the plugin state and the counters.

enabled = true

[perks]
blessed_vials      = 933
compounded_synergy = 935
savvy_elixirs      = 936
endless_vials      = 937
bottomless_vials   = 940
serendipity        = 941

[values]
blessed_vials_reduction    = 5   # percent less damage taken (1 to 100)
compounded_synergy_percent = 5   # percent stronger per running potion effect (1 to 100)
savvy_elixirs_chance       = 25  # percent chance a utility potion is kept (1 to 100)
serendipity_chance         = 15  # percent chance of a shrine effect (1 to 100)

[serendipity]
# shrines.txt Code values Serendipity picks from, each equally likely.
#   1 Refilling   6 Armor   7 Combat   8 Fire resist   9 Cold resist
#   10 Lightning resist   11 Poison resist   12 Skill   13 Mana Recharge
#   14 Stamina   15 Experience
# The effect is the one the shrine itself gives (with Shrine Rework's changes),
# on the drinking player only.
shrine_codes = [1, 6, 7, 8, 9, 10, 11, 12, 13, 15]

[states]
# The potion effect states Blessed Vials looks for. (Endless Vials works inside
# the game's own potion code, which always uses healthpot 100 and manapot 106.)
healing_potion = 100  # states.txt healthpot
mana_potion    = 106  # states.txt manapot

[[reincarnation]]
# One block per state, up to 8: copy the block to add a state. A player
# carrying the states of several entries is saved by the first of them whose
# cooldown is ready; the other states stay for the next hit that would kill.
#   state               the state that saves the player (-1 = entry off)
#   cooldown_stat       the seconds of cooldown left: cooldown_seconds when the
#                       entry saves a player, 0 when ready (-1 = no cooldown).
#                       Above 0, the entry's state does not save. Entries with
#                       the same cooldown_stat share one cooldown.
#   cooldown_seconds    0 to 86400 (default 300)
#   invulnerable_frames 0 to 750, 25 frames = 1 second (default 25)
#   sweep_items         base item codes (weapons.txt, armor.txt or misc.txt)
#                       that get the cooldown sweep while the entry's cooldown
#                       runs (default none)
#   sweep_uniques       uniqueitems.txt index names, as written there, that get
#                       the cooldown sweep while the entry's cooldown runs
#                       (default none). Only that unique, not its base item.
state               = 677  # the elixir's state
cooldown_stat       = 945
cooldown_seconds    = 300
invulnerable_frames = 25
sweep_items         = ["35p"]

[cooldown_sweep]
# While an entry's cooldown runs, a cooldown sweep is drawn over its
# sweep_items and sweep_uniques wherever the game draws their icon (inventory,
# belt, stash, cube), shrinking as the seconds run out. By default it is the
# Crossbow Charges sweep with the same color and opacity, so it looks like the
# skill buttons' one. HD graphics only. In someone else's game the sweep
# follows the cooldown stat, which reaches that client only with
# itemstatcost.txt fCallback = 1.
enabled = true
sprite  = "PANEL/CrossbowCharges/Sweep"  # under data/hd/global/ui/, like an ImageWidget filename
color   = [0, 0, 0]                      # RGB, 0 to 255
opacity = 45                             # percent

[items]
# misc.txt codes the potion perks act on.
# healing_mana: Endless Vials, Bottomless Vials, Compounded Synergy.
# utility: Savvy Elixirs, Serendipity, Compounded Synergy.
# rejuvenation: Compounded Synergy's extra healing only.
healing_mana = ["hp1", "hp2", "hp3", "hp4", "hp5", "mp1", "mp2", "mp3", "mp4", "mp5"]
utility = ["01p", "02p", "03p", "04p", "05p", "06p", "07p", "08p", "09p", "10p", "11p", "12p",
           "13p", "14p", "15p", "16p", "17p", "18p", "19p", "20p", "21p", "22p", "23p", "24p",
           "25p", "26p", "27p", "28p", "29p", "30p", "31p", "32p", "33p", "34p", "vps", "yps", "wms"]
rejuvenation = ["rvs", "rvl"]

[find_potion]
# Find Rejuvenation (srvdofunc 69, the game's Find Potion) picks its potion
# from a fixed table and drops it straight onto the corpse, so no treasure
# class ever sees it. Each line below sends one potion it picks to a
# treasureclassex.txt class instead:  misc.txt code = "Treasure Class".
# The class drops at the corpse with the caster as the killer, so (with
# ConditionCalc Killer) its ConditionCalc rows read the caster's perks exactly
# as on a kill: Unstable Mixtures, and Hidden Elixirs and Alchemical
# Excellence on the elixir it rolls.
#   One item per find, as before: Item Quantity adds nothing to a find.
#   NoDrop is ignored, as Find Treasure ignores it, so a find always drops
#   something: put a ConditionCalc only on classes inside the listed one.
#   A potion with no line, or whose class is not in the tables (the log names
#   it), drops as before.
#   Listing any line replaces these defaults; enabled = false turns it off.
enabled = true
rvs = "Rejuvenation Regular"
rvl = "Rejuvenation Full"
)toml";

constexpr std::size_t MaximumConfigBytes = 64 * 1024;

// ---------------------------------------------------------------------------
// TOML
// ---------------------------------------------------------------------------

auto Trim(std::string_view value) noexcept -> std::string_view {
    std::size_t first = 0;
    while (first < value.size() && (value[first] == ' ' || value[first] == '\t' || value[first] == '\r'
            || value[first] == '\n')) {
        ++first;
    }
    std::size_t last = value.size();
    while (last > first && (value[last - 1] == ' ' || value[last - 1] == '\t' || value[last - 1] == '\r'
            || value[last - 1] == '\n')) {
        --last;
    }
    return value.substr(first, last - first);
}

// Drops a trailing # comment that is not inside a quoted string.
auto StripComment(std::string_view line) noexcept -> std::string_view {
    bool quoted = false;
    for (std::size_t i = 0; i < line.size(); ++i) {
        if (quoted && line[i] == '\\') { ++i; continue; }
        if (line[i] == '"') quoted = !quoted;
        if (line[i] == '#' && !quoted) return line.substr(0, i);
    }
    return line;
}

// True when the text holds a ']' outside quoted strings.
auto ClosesArray(std::string_view text) noexcept -> bool {
    bool quoted = false;
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (quoted && text[i] == '\\') { ++i; continue; }
        if (text[i] == '"') quoted = !quoted;
        if (text[i] == ']' && !quoted) return true;
    }
    return false;
}

auto ParseBool(std::string_view value, bool& out) noexcept -> bool {
    if (value == "true") { out = true; return true; }
    if (value == "false") { out = false; return true; }
    return false;
}

auto ParseInt(std::string_view value, std::int64_t minimum, std::int64_t maximum, std::int32_t& out) noexcept -> bool {
    value = Trim(value);
    if (value.empty()) return false;
    std::int64_t result   = 0;
    std::size_t  i        = 0;
    bool         negative = false;
    if (value[0] == '-' || value[0] == '+') {
        negative = value[0] == '-';
        i = 1;
    }
    if (i >= value.size()) return false;
    for (; i < value.size(); ++i) {
        if (value[i] == '_') continue;
        if (value[i] < '0' || value[i] > '9') return false;
        result = result * 10 + (value[i] - '0');
        if (result > 4'000'000'000LL) return false;
    }
    if (negative) result = -result;
    if (result < minimum || result > maximum) return false;
    out = static_cast<std::int32_t>(result);
    return true;
}

auto ParseString(std::string_view value, std::string& out) -> bool {
    value = Trim(value);
    if (value.size() < 2 || value.front() != '"' || value.back() != '"') return false;
    out.assign(value.substr(1, value.size() - 2));
    return true;
}

auto ParseIntArray(std::string_view value, std::vector<std::int32_t>& out) -> bool {
    value = Trim(value);
    if (value.size() < 2 || value.front() != '[' || value.back() != ']') return false;
    value = value.substr(1, value.size() - 2);
    out.clear();
    while (!value.empty()) {
        const std::size_t comma = value.find(',');
        const std::string_view item = Trim(value.substr(0, comma));
        value = comma == std::string_view::npos ? std::string_view{} : value.substr(comma + 1);
        if (item.empty()) continue;
        std::int32_t number = 0;
        if (!ParseInt(item, 0, 32767, number)) return false;
        out.push_back(number);
    }
    return true;
}

// A list of quoted strings: ["a", "b\\n c"]. Escapes: \\" \\\\ \\n \\t.
auto ParseStringArray(std::string_view value, std::vector<std::string>& out) -> bool {
    value = Trim(value);
    if (value.size() < 2 || value.front() != '[' || value.back() != ']') return false;
    out.clear();
    std::size_t i = 1;
    const std::size_t end = value.size() - 1;
    while (i < end) {
        const char c = value[i];
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == ',') { ++i; continue; }
        if (c != '"') return false;
        std::string item;
        ++i;
        bool closed = false;
        while (i < end) {
            const char d = value[i++];
            if (d == '"') { closed = true; break; }
            if (d == '\\' && i < end) {
                const char e = value[i++];
                item += e == 'n' ? '\n' : (e == 't' ? '\t' : e);
                continue;
            }
            item += d;
        }
        if (!closed) return false;
        out.push_back(std::move(item));
    }
    return true;
}

void Problem(const std::string& text) {
    if (!ConfigProblems.empty()) ConfigProblems += " ";
    ConfigProblems += text;
}

// [[reincarnation]] while the config is read. The first entry in the file
// replaces the built-in one. A 1.2 file still reads the same: its single
// [reincarnation] section is the first entry, and its [cooldown_sweep] items
// are that entry's sweep_items unless the entry lists its own.
bool                     ReincarnationListed = false;
std::size_t              ReincarnationBlocks = 0;    // entries met in the file, kept or not
int                      ReincarnationTarget = -1;   // the entry keys go to; -1 = skipped
std::vector<std::string> LegacySweepCodes;
bool                     LegacySweepGiven = false;
bool                     SweepUniquesListed = false;  // some entry has sweep_uniques: icons check their unique
bool                     FindPotionListed = false;    // a [find_potion] line replaced the built-in ones

void BeginReincarnation() {
    if (!ReincarnationListed) {
        Settings.reincarnation.clear();
        ReincarnationListed = true;
    }
    ++ReincarnationBlocks;
    if (Settings.reincarnation.size() >= MaxReincarnations) {
        ReincarnationTarget = -1;
        Problem("[[reincarnation]] entry " + std::to_string(ReincarnationBlocks) + ": only "
            + std::to_string(MaxReincarnations) + " entries are read; this one is skipped.");
        return;
    }
    Settings.reincarnation.emplace_back();
    ReincarnationTarget = static_cast<int>(Settings.reincarnation.size()) - 1;
}

// After the config is read: the 1.2 sweep items, the entry checks, and the
// shared cooldowns. Entries with the same cooldown stat run one countdown, the
// first entry's; read back from a save it runs at most the longest of their
// cooldown_seconds.
void FinishReincarnation() {
    std::vector<ReincarnationEntry>& entries = Settings.reincarnation;
    if (LegacySweepGiven && !entries.empty() && !entries[0].sweepGiven) entries[0].sweepCodes = LegacySweepCodes;
    for (std::size_t i = 0; i < entries.size(); ++i) {
        ReincarnationEntry& entry = entries[i];
        const std::string   name  = "[[reincarnation]] entry " + std::to_string(i + 1);
        if (!entry.stateGiven) Problem(name + " has no state; it does nothing.");
        for (std::size_t j = 0; j < i && entry.state >= 0; ++j) {
            if (entries[j].state != entry.state) continue;
            Problem(name + ": state " + std::to_string(entry.state) + " is also entry " + std::to_string(j + 1)
                + "'s, which saves first; this entry never does.");
            break;
        }
        entry.owner   = i;
        entry.longest = entry.cooldownSeconds;
        if (entry.cooldownStat == NoStat) continue;
        for (std::size_t j = 0; j < i; ++j) {
            if (entries[j].cooldownStat == entry.cooldownStat) { entry.owner = entries[j].owner; break; }
        }
        entries[entry.owner].longest = std::max(entries[entry.owner].longest, entry.cooldownSeconds);
    }
    for (ReincarnationEntry& entry : entries) entry.longest = entries[entry.owner].longest;
    SweepUniquesListed = std::any_of(entries.begin(), entries.end(),
        [](const ReincarnationEntry& entry) { return !entry.sweepUniques.empty(); });
}

void ApplySetting(std::string_view section, std::string_view key, std::string_view value) {
    const auto bad = [&](const char* what) {
        Problem("[" + std::string(section) + "] " + std::string(key) + ": " + what + "; the default is kept.");
    };
    const auto stat = [&](std::int32_t& out) {
        if (!ParseInt(value, -1, 65535, out)) bad("expects a stat id or -1");
    };
    if (section.empty()) {
        if (key == "enabled") { if (!ParseBool(value, Settings.enabled)) bad("expects true or false"); }
        return;
    }
    if (section == "perks") {
        if (key == "blessed_vials") stat(Settings.blessedVials);
        else if (key == "compounded_synergy") stat(Settings.compoundedSynergy);
        else if (key == "serendipity") stat(Settings.serendipity);
        else if (key == "savvy_elixirs") stat(Settings.savvyElixirs);
        else if (key == "endless_vials") stat(Settings.endlessVials);
        else if (key == "bottomless_vials") stat(Settings.bottomlessVials);
        return;
    }
    if (section == "values") {
        if (key == "blessed_vials_reduction") {
            if (!ParseInt(value, 1, 100, Settings.blessedReduction)) bad("expects 1 to 100");
        } else if (key == "savvy_elixirs_chance") {
            if (!ParseInt(value, 1, 100, Settings.savvyChance)) bad("expects 1 to 100");
        } else if (key == "compounded_synergy_percent") {
            if (!ParseInt(value, 1, 100, Settings.synergyPercent)) bad("expects 1 to 100");
        } else if (key == "serendipity_chance") {
            if (!ParseInt(value, 1, 100, Settings.serendipityChance)) bad("expects 1 to 100");
        }
        return;
    }
    if (section == "serendipity") {
        if (key == "shrine_codes") {
            std::vector<std::int32_t> codes;
            if (!ParseIntArray(value, codes)) { bad("expects a list of shrine codes"); return; }
            std::vector<std::int32_t> kept;
            for (const std::int32_t code : codes) {
                if (code > 0 && code < static_cast<std::int32_t>(ChartSlots)) kept.push_back(code);
                else Problem("[serendipity] shrine_codes: " + std::to_string(code) + " is not a shrine code; skipped.");
            }
            Settings.serendipityShrines = std::move(kept);
        }
        return;
    }
    if (section == "states") {
        if (key == "healing_potion") { if (!ParseInt(value, 0, 65535, Settings.healingState)) bad("expects a state id"); }
        else if (key == "mana_potion") { if (!ParseInt(value, 0, 65535, Settings.manaState)) bad("expects a state id"); }
        return;
    }
    if (section == "reincarnation") {
        if (ReincarnationTarget < 0) return;   // an entry past the limit
        ReincarnationEntry& entry = Settings.reincarnation[static_cast<std::size_t>(ReincarnationTarget)];
        const auto entryBad = [&](const char* what) {
            Problem("[[reincarnation]] entry " + std::to_string(ReincarnationTarget + 1) + " " + std::string(key) + ": "
                + what + "; the default is kept.");
        };
        if (key == "state") {
            entry.stateGiven = true;
            if (!ParseInt(value, -1, 65535, entry.state)) entryBad("expects a state id or -1");
        } else if (key == "cooldown_stat") {
            if (!ParseInt(value, -1, 65535, entry.cooldownStat)) entryBad("expects a stat id or -1");
        } else if (key == "cooldown_seconds") {
            if (!ParseInt(value, 0, 86400, entry.cooldownSeconds)) entryBad("expects 0 to 86400");
        } else if (key == "invulnerable_frames") {
            if (!ParseInt(value, 0, 750, entry.invulnerableFrames)) entryBad("expects 0 to 750");
        } else if (key == "sweep_items") {
            std::vector<std::string> codes;
            if (ParseStringArray(value, codes)) {
                entry.sweepCodes = std::move(codes);
                entry.sweepGiven = true;
            } else {
                entryBad("expects a list of item codes");
            }
        } else if (key == "sweep_uniques") {
            std::vector<std::string> names;
            if (!ParseStringArray(value, names)) { entryBad("expects a list of uniqueitems.txt index names"); return; }
            entry.sweepUniques.clear();
            for (std::string& name : names) {
                if (name.empty() || name.size() >= UniqueNameBytes) {
                    Problem("[[reincarnation]] entry " + std::to_string(ReincarnationTarget + 1) + " sweep_uniques: \""
                        + name + "\" is not a uniqueitems.txt index name (1 to 31 characters); skipped.");
                    continue;
                }
                entry.sweepUniques.push_back(std::move(name));
            }
        }
        return;
    }
    if (section == "cooldown_sweep") {
        if (key == "enabled") { if (!ParseBool(value, Settings.sweepEnabled)) bad("expects true or false"); }
        else if (key == "items") {   // 1.2's key: the first entry's sweep_items
            std::vector<std::string> codes;
            if (ParseStringArray(value, codes)) {
                LegacySweepCodes = std::move(codes);
                LegacySweepGiven = true;
            } else {
                bad("expects a list of item codes");
            }
        } else if (key == "sprite") {
            std::string sprite;
            if (ParseString(value, sprite) && !sprite.empty()) Settings.sweepSprite = std::move(sprite);
            else bad("expects a sprite path");
        } else if (key == "color") {
            std::vector<std::int32_t> rgb;
            if (ParseIntArray(value, rgb) && rgb.size() == 3 && rgb[0] >= 0 && rgb[0] <= 255 && rgb[1] >= 0
                && rgb[1] <= 255 && rgb[2] >= 0 && rgb[2] <= 255) {
                for (int i = 0; i < 3; ++i) Settings.sweepColor[i] = rgb[static_cast<std::size_t>(i)];
            } else {
                bad("expects [r, g, b] from 0 to 255");
            }
        } else if (key == "opacity") {
            if (!ParseInt(value, 0, 100, Settings.sweepOpacity)) bad("expects 0 to 100");
        }
        return;
    }
    if (section == "find_potion") {
        if (key == "enabled") {
            if (!ParseBool(value, Settings.findPotionEnabled)) bad("expects true or false");
            return;
        }
        std::string_view code = key;
        if (code.size() >= 2 && code.front() == '"' && code.back() == '"') code = code.substr(1, code.size() - 2);
        if (code.empty() || code.size() > 4) {
            Problem("[find_potion] " + std::string(key) + ": not a misc.txt code (1 to 4 characters); skipped.");
            return;
        }
        std::string name;
        if (!ParseString(value, name) || name.empty() || name.size() >= TreasureNameBytes) {
            Problem("[find_potion] " + std::string(code) + ": expects a treasure class name in quotes; skipped.");
            return;
        }
        if (!FindPotionListed) {
            Settings.findPotionClasses.clear();
            FindPotionListed = true;
        }
        for (FindPotionClass& entry : Settings.findPotionClasses) {
            if (entry.code == code) { entry.name = std::move(name); return; }
        }
        if (Settings.findPotionClasses.size() >= MaxFindPotionClasses) {
            Problem("[find_potion]: only " + std::to_string(MaxFindPotionClasses) + " lines are read; "
                + std::string(code) + " is skipped.");
            return;
        }
        Settings.findPotionClasses.push_back(FindPotionClass{ std::string(code), std::move(name) });
        return;
    }
    if (section == "items") {
        std::vector<std::string> codes;
        if (!ParseStringArray(value, codes)) { bad("expects a list of item codes"); return; }
        if (key == "healing_mana") Settings.healingManaCodes = std::move(codes);
        else if (key == "utility") Settings.utilityCodes = std::move(codes);
        else if (key == "rejuvenation") Settings.rejuvenationCodes = std::move(codes);
    }
}

void ParseConfig(std::string_view text) {
    std::string_view section;
    std::string      pending;
    std::string      pendingKey;
    std::size_t      position = 0;
    while (position <= text.size()) {
        std::size_t end = text.find('\n', position);
        if (end == std::string_view::npos) end = text.size();
        const std::string_view line = Trim(StripComment(text.substr(position, end - position)));
        position = end + 1;
        if (!pendingKey.empty()) {
            pending += ' ';
            pending += line;
            if (ClosesArray(line)) {
                ApplySetting(section, pendingKey, Trim(pending));
                pendingKey.clear();
                pending.clear();
            }
            continue;
        }
        if (line.empty()) continue;
        if (line.front() == '[') {
            // [name] or [[name]]; every reincarnation header starts a new entry.
            const std::size_t open  = line.size() > 1 && line[1] == '[' ? 2 : 1;
            const std::size_t close = line.find(']', open);
            section = close == std::string_view::npos ? std::string_view{} : Trim(line.substr(open, close - open));
            if (section == "reincarnation") BeginReincarnation();
            continue;
        }
        const std::size_t equals = line.find('=');
        if (equals == std::string_view::npos) continue;
        const std::string_view key   = Trim(line.substr(0, equals));
        const std::string_view value = Trim(line.substr(equals + 1));
        if (!value.empty() && value.front() == '[' && !ClosesArray(value.substr(1))) {
            pendingKey = std::string(key);
            pending    = std::string(value);
            continue;
        }
        ApplySetting(section, key, value);
    }
    FinishReincarnation();
}

void ReadConfiguration() {
    if (!Context->EnsureConfig(DefaultConfigToml)) {
        Context->LogWarn("Alchemy: the config file could not be created; using the defaults.");
        ParseConfig(DefaultConfigToml);
        return;
    }
    std::string buffer(MaximumConfigBytes, '\0');
    if (Context->ReadConfig(buffer.data(), static_cast<std::uint32_t>(buffer.size()), nullptr)) {
        buffer.resize(std::strlen(buffer.c_str()));
        ParseConfig(buffer);
    } else {
        Context->LogWarn("Alchemy: the config file could not be read; using the defaults.");
        ParseConfig(DefaultConfigToml);
    }
}

// ---------------------------------------------------------------------------
// Natives
// ---------------------------------------------------------------------------

using ItemUseFn    = std::int64_t(__fastcall*)(void* game, void* player, std::uint8_t* request, std::uint64_t flag,
                                               std::uint64_t arg5, std::uint64_t arg6);
using CheckStateFn = std::int32_t(__fastcall*)(void* unit, std::int32_t state);
using IsDeadFn     = std::int64_t(__fastcall*)(void* unit);
using CanUseFn     = bool(__fastcall*)(void* game, void* player, void* item, void* target);
using UseEffectFn  = std::int64_t(__fastcall*)(void* game, void* player, void* item);
using ServerUnitFn = void*(__fastcall*)(void* game, std::int32_t type, std::uint32_t id);
using ReadStatFn   = std::int32_t(__fastcall*)(void* unit, std::int32_t stat, std::int32_t layer);
using WriteStatFn  = void(__fastcall*)(void* unit, std::int32_t stat, std::int32_t value, std::int32_t layer);
using MaxPoolFn    = std::int32_t(__fastcall*)(void* unit);
using StateListFn  = void*(__fastcall*)(void* unit, std::int32_t state);
using RemoveListFn = void(__fastcall*)(void* unit, void* list);
using FreeListFn   = void(__fastcall*)(std::uint32_t context, void* list);
using ReadListStatFn = std::int32_t(__fastcall*)(std::uint32_t context, void* list, std::int32_t stat, std::int32_t layer);
using SetListStatFn  = void(__fastcall*)(std::uint32_t context, void* list, std::int32_t stat, std::int32_t value,
                                         std::int32_t layer);
using ShrineRowCountFn = std::int32_t(__fastcall*)(std::uint32_t context);
using ShrineRecordFn   = const std::uint8_t*(__fastcall*)(std::uint32_t context, std::uint32_t row);
using ShrineHandlerFn  = std::uint64_t(__fastcall*)(std::uint32_t context, std::uint8_t* operate, const std::uint8_t* record);

// The game's sprite (16 bytes) and the draw settings its icon draws pass.
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
using RenderItemIconFn = void(__fastcall*)(void* item, std::uint64_t packedXY, float scale, void* params);
using LocalIndexFn     = std::int32_t(__fastcall*)();
using LocalPlayerFn    = void*(__fastcall*)(std::int32_t index);
using SpriteLoadFn     = std::uint64_t(__fastcall*)(GameSprite* sprite, const char* name, std::uint8_t flags);
using SpriteDrawHdFn   = std::uint64_t(__fastcall*)(void* hdSprite, const std::int32_t* position, float scale,
                                                    const SpriteDrawParams* params);
using ItemValueFn   = std::uint32_t(__fastcall*)(void* item);
using ItemContextFn = std::uint8_t(__fastcall*)(void* unit);
using DataTablesFn  = const std::uint8_t*(__fastcall*)(std::uint8_t context);
using UnitTypeFn   = std::int32_t(__fastcall*)(void* unit);
using UnitIdFn     = std::uint32_t(__fastcall*)(void* unit);
using ManaRegenFn  = char(__fastcall*)(void* game, void* player);
using DropItemWithCodeFn = void*(__fastcall*)(void* game, void* unit, std::uint32_t code, std::int32_t quality);
using GenerateDropsFn    = std::int64_t(__fastcall*)(void* game, void* unit, void* killer, std::uint32_t treasureClass,
                                                     std::int32_t quality, std::int32_t itemLevel, std::int32_t ignoreNoDrop,
                                                     void** items, std::int32_t* count, std::int32_t maxItems);
using ItemLevelFn        = std::int32_t(__fastcall*)(void* unit, std::int32_t levelId);

ItemUseFn    OriginalItemUse{};
CheckStateFn CheckState{};
IsDeadFn     IsDead{};
CanUseFn     CanUse{};
UseEffectFn  UseEffect{};
ServerUnitFn ServerUnit{};
ReadStatFn   ReadStat{};
WriteStatFn  WriteStat{};
MaxPoolFn    MaxLife{};
MaxPoolFn    MaxMana{};
StateListFn  StateList{};
RemoveListFn RemoveList{};
FreeListFn   FreeList{};
ReadListStatFn   ReadListStat{};
SetListStatFn    SetListStat{};
ShrineRowCountFn ShrineRowCount{};
ShrineRecordFn   ShrineRecord{};
RenderItemIconFn OriginalRenderItemIcon{};
LocalIndexFn     LocalIndex{};
LocalPlayerFn    LocalPlayer{};
SpriteLoadFn     SpriteLoad{};
SpriteDrawHdFn   SpriteDrawHd{};
ItemValueFn      ItemQuality{};
ItemValueFn      ItemFileIndex{};
ItemContextFn    ItemContext{};
DataTablesFn     DataTables{};
UnitTypeFn   UnitType{};
UnitIdFn     UnitId{};
ManaRegenFn  ManaRegen{};
DropItemWithCodeFn DropItemWithCode{};
GenerateDropsFn    GenerateDrops{};
ItemLevelFn        NewItemLevel{};

template <typename T>
auto At(std::uint64_t rva) noexcept -> T {
    return reinterpret_cast<T>(Base + rva);
}

template <typename T>
auto Read(const void* base, std::size_t offset) noexcept -> T {
    T value{};
    std::memcpy(&value, static_cast<const std::uint8_t*>(base) + offset, sizeof(T));
    return value;
}

void BindNatives() noexcept {
    OriginalItemUse = At<ItemUseFn>(ItemUseRva);
    CheckState = At<CheckStateFn>(CheckStateRva);
    IsDead     = At<IsDeadFn>(IsDeadRva);
    CanUse     = At<CanUseFn>(CanUseRva);
    UseEffect  = At<UseEffectFn>(UseEffectRva);
    ServerUnit = At<ServerUnitFn>(ServerUnitRva);
    ReadStat   = At<ReadStatFn>(ReadStatRva);
    WriteStat  = At<WriteStatFn>(WriteStatRva);
    MaxLife    = At<MaxPoolFn>(MaxLifeRva);
    MaxMana    = At<MaxPoolFn>(MaxManaRva);
    StateList  = At<StateListFn>(StateListRva);
    RemoveList = At<RemoveListFn>(RemoveListRva);
    FreeList   = At<FreeListFn>(FreeListRva);
    ReadListStat   = At<ReadListStatFn>(ReadListStatRva);
    SetListStat    = At<SetListStatFn>(SetListStatRva);
    ShrineRowCount = At<ShrineRowCountFn>(ShrineRowCountRva);
    ShrineRecord   = At<ShrineRecordFn>(ShrineRecordRva);
    LocalIndex     = At<LocalIndexFn>(LocalIndexRva);
    LocalPlayer    = At<LocalPlayerFn>(LocalPlayerRva);
    SpriteLoad     = At<SpriteLoadFn>(SpriteLoadRva);
    SpriteDrawHd   = At<SpriteDrawHdFn>(SpriteDrawHdRva);
    ItemQuality    = At<ItemValueFn>(ItemQualityRva);
    ItemFileIndex  = At<ItemValueFn>(ItemFileIndexRva);
    ItemContext    = At<ItemContextFn>(ItemContextRva);
    DataTables     = At<DataTablesFn>(DataTablesRva);
    UnitType   = At<UnitTypeFn>(UnitTypeRva);
    UnitId     = At<UnitIdFn>(UnitIdRva);
    ManaRegen  = At<ManaRegenFn>(ManaRegenRva);
    DropItemWithCode = At<DropItemWithCodeFn>(DropItemWithCodeRva);
    GenerateDrops    = At<GenerateDropsFn>(GenerateDropsRva);
    NewItemLevel     = At<ItemLevelFn>(ItemLevelRva);
}

auto HookedByOtherPlugin(const Witness& witness) noexcept -> bool {
    const auto* live = reinterpret_cast<const std::uint8_t*>(Base + witness.rva);
    const bool jump = live[0] == 0xE9 || (live[0] == 0xFF && live[1] == 0x25) || (live[0] == 0x48 && live[1] == 0xB8);
    if (!jump) return false;
    constexpr std::uint32_t Rewritten = 14;
    return witness.size <= Rewritten || std::memcmp(live + Rewritten, witness.bytes + Rewritten, witness.size - Rewritten) == 0;
}

template <std::size_t Count>
auto VerifyList(const Witness (&list)[Count]) noexcept -> bool {
    bool ok = true;
    for (const Witness& witness : list) {
        if (Context->CheckExpectedBytes(witness.rva, witness.bytes, witness.size)) continue;
        if (witness.callable && HookedByOtherPlugin(witness)) {
            D2RL::LogInfoF(Context, "Alchemy: %s at RVA 0x%llX is hooked by another plugin; calls go through that hook.",
                witness.name, static_cast<unsigned long long>(witness.rva));
            continue;
        }
        D2RL::LogErrorF(Context, "Alchemy: %s at RVA 0x%llX does not match this build.", witness.name,
            static_cast<unsigned long long>(witness.rva));
        ok = false;
    }
    return ok;
}

auto VerifyAll() noexcept -> bool {
    return VerifyList(Witnesses);
}

// ---------------------------------------------------------------------------
// Runtime state
// ---------------------------------------------------------------------------

enum class PluginState { Loading, DisabledByConfig, UnsupportedBuild, InstallFailed, Active };
PluginState       State = PluginState::Loading;
std::atomic<bool> Active{ false };

// Item class ids (rows of the combined Items table) per perk group, resolved
// from the codes on first use, on the game thread.
std::atomic<bool>         ItemsResolved{ false };
std::vector<std::int32_t> HealingManaItems;
std::vector<std::int32_t> UtilityItems;
std::vector<std::string>  UnknownCodes;

// Compounded Synergy: one running potion effect on one player.
constexpr std::size_t MaxEffectStats = 6;
struct SynergyEntry {
    std::int32_t                              state = -1;
    void*                                     list  = nullptr;
    std::int32_t                              percent = 100;   // what the written values are scaled to
    std::uint8_t                              count = 0;
    std::array<std::int32_t, MaxEffectStats>  stats{};
    std::array<std::int32_t, MaxEffectStats>  base{};
    std::array<std::int32_t, MaxEffectStats>  written{};
};

struct PlayerData {
    std::uint32_t                               id = 0;
    std::array<std::int32_t, MaxReincarnations> cooldownEnd{};      // frame, per entry running a countdown; 0 = none
    std::array<std::int32_t, MaxReincarnations> cooldownTotal{};    // the seconds that countdown started from
    std::int32_t                                invulnerableEnd = 0; // frame; 0 = none
    std::vector<SynergyEntry>                   synergy;
};
std::mutex              TimerLock;
void*                   TimerGame = nullptr;
std::vector<PlayerData> Timers;
std::atomic<void*>      ServerGame{ nullptr };

// Potion effects Compounded Synergy scales: a state and the stats its items put on it.
struct PotionEffect {
    std::int32_t              state = -1;
    std::vector<std::int32_t> stats;
};
std::vector<PotionEffect> PotionEffects;   // guarded by ItemsLock
std::vector<std::int32_t> RejuvenationItems;

// Item rows the cooldown sweep is drawn on, each packed with the
// [[reincarnation]] entry whose cooldown it shows (row | entry << 16), readable
// from the render thread without a lock (-1 = empty).
constexpr std::size_t                          MaxSweepItems   = 32;
constexpr std::int32_t                         SweepRowMask    = 0xFFFF;
constexpr int                                  SweepEntryShift = 16;
std::array<std::atomic<std::int32_t>, MaxSweepItems> SweepItems{};
std::atomic<std::uint64_t> SweepDraws{ 0 };
std::atomic<std::uint64_t> SweepSkipped{ 0 };
std::atomic<std::uint64_t> SweepIconsSeen{ 0 };
std::atomic<std::int32_t>  SweepLastSeconds{ -1 };
std::atomic<std::int32_t>  SweepLoadState{ 0 };   // 0 not tried, 1 loaded, 2 failed

// The seconds of cooldown the server tick computed, per player unit id and
// [[reincarnation]] entry, with the seconds each countdown started from. In
// single player and on the host the drawing client runs in the same process,
// so the sweep reads these and does not wait for the stat to reach the client.
// A client in someone else's game finds nothing here and reads the stat.
struct PublishedCooldown {
    std::atomic<std::uint32_t>                               id{ 0xFFFFFFFFu };
    std::array<std::atomic<std::int32_t>, MaxReincarnations> seconds{};
    std::array<std::atomic<std::int32_t>, MaxReincarnations> total{};
};
std::array<PublishedCooldown, 8> PublishedCooldowns;

void ClearPublishedCooldowns() noexcept {
    for (PublishedCooldown& slot : PublishedCooldowns) {
        for (std::size_t entry = 0; entry < MaxReincarnations; ++entry) {
            slot.seconds[entry].store(0, std::memory_order_relaxed);
            slot.total[entry].store(0, std::memory_order_relaxed);
        }
        slot.id.store(0xFFFFFFFFu, std::memory_order_release);
    }
}

void PublishCooldown(std::uint32_t id, std::size_t entry, std::int32_t seconds, std::int32_t total) noexcept {
    PublishedCooldown* free = nullptr;
    for (PublishedCooldown& slot : PublishedCooldowns) {
        const std::uint32_t holder = slot.id.load(std::memory_order_acquire);
        if (holder == id) {
            slot.total[entry].store(total, std::memory_order_relaxed);
            slot.seconds[entry].store(seconds, std::memory_order_release);
            return;
        }
        if (holder == 0xFFFFFFFFu && free == nullptr) free = &slot;
    }
    if (free == nullptr || seconds <= 0) return;
    for (std::size_t other = 0; other < MaxReincarnations; ++other) {
        free->seconds[other].store(0, std::memory_order_relaxed);
        free->total[other].store(0, std::memory_order_relaxed);
    }
    free->total[entry].store(total, std::memory_order_relaxed);
    free->seconds[entry].store(seconds, std::memory_order_relaxed);
    free->id.store(id, std::memory_order_release);
}

// -1 when the server tick of this process does not know the player.
auto PublishedSeconds(std::uint32_t id, std::size_t entry, std::int32_t& total) noexcept -> std::int32_t {
    for (const PublishedCooldown& slot : PublishedCooldowns) {
        if (slot.id.load(std::memory_order_acquire) != id) continue;
        const std::int32_t seconds = slot.seconds[entry].load(std::memory_order_acquire);
        total = slot.total[entry].load(std::memory_order_relaxed);
        return seconds;
    }
    return -1;
}

std::mt19937 Random{ static_cast<std::uint32_t>(GetTickCount64()) };

std::atomic<std::uint64_t> HealingManaUses{ 0 };
std::atomic<std::uint64_t> UtilityUses{ 0 };
std::atomic<std::uint64_t> CanUseRefused{ 0 };
std::atomic<std::uint64_t> BottomlessSaves{ 0 };
std::atomic<std::uint64_t> SavvySaves{ 0 };
std::atomic<std::uint64_t> SerendipityShrines{ 0 };
std::atomic<std::uint64_t> SerendipityMisses{ 0 };
std::atomic<std::uint64_t> SynergyRescales{ 0 };
std::atomic<std::uint64_t> RejuvenationBoosts{ 0 };
std::atomic<std::uint64_t> BlessedHits{ 0 };
std::atomic<std::uint64_t> ManaBurnsBlocked{ 0 };
std::atomic<std::uint64_t> EndlessKeeps{ 0 };
std::atomic<std::uint64_t> Reincarnations{ 0 };
std::atomic<std::uint64_t> InvulnerableHits{ 0 };
std::array<std::atomic<std::uint64_t>, MaxReincarnations> EntrySaves{};     // per [[reincarnation]] entry
std::array<std::atomic<std::uint64_t>, MaxReincarnations> EntryRefusals{};  // lethal hits its state met during the cooldown

auto Roll(std::int32_t percent) noexcept -> bool {
    if (percent >= 100) return true;
    if (percent <= 0) return false;
    return static_cast<std::int32_t>(Random() % 100U) < percent;
}

auto GameFrame(void* game) noexcept -> std::int32_t {
    return Read<std::int32_t>(game, GameFrameOffset);
}

auto IsPlayer(void* unit) noexcept -> bool {
    return unit != nullptr && UnitType(unit) == UnitPlayer;
}

auto HasPerk(void* player, std::int32_t stat) noexcept -> bool {
    return stat != NoStat && ReadStat(player, stat, 0) > 0;
}

auto HasState(void* unit, std::int32_t state) noexcept -> bool {
    return state >= 0 && StateList(unit, state) != nullptr;
}

auto Contains(const std::vector<std::int32_t>& list, std::int32_t value) noexcept -> bool {
    for (const std::int32_t entry : list) {
        if (entry == value) return true;
    }
    return false;
}

auto CodeValue(const std::string& code) noexcept -> std::uint32_t {
    char padded[4]{ ' ', ' ', ' ', ' ' };
    for (std::size_t i = 0; i < code.size() && i < 4; ++i) padded[i] = code[i];
    return D2RL::Items::MakeItemCode(padded[0], padded[1], padded[2], padded[3]);
}

auto ItemRow(const std::string& code, std::int32_t& row) noexcept -> bool {
    if (TableSvc == nullptr || code.empty() || code.size() > 4) return false;
    for (const D2RL::DataTables::Bank bank : { D2RL::DataTables::Bank::Rotw, D2RL::DataTables::Bank::Lod }) {
        D2RL::DataTables::RowView view{};
        view.structSize = D2RL::DataTables::RowViewSize;
        if (TableSvc->findRowByCode(Context, bank, D2RL::DataTables::TableId::Items, CodeValue(code), &view)
                == D2RL::DataTables::Result::Success
            && D2RL::DataTables::HasRowViewField(&view, D2RL::DataTables::RowViewRequiredSize) && view.row != nullptr) {
            row = static_cast<std::int32_t>(view.rowIndex);
            return true;
        }
    }
    return false;
}

// ---------------------------------------------------------------------------
// Reading the mod's txt files (Compounded Synergy needs every stat a potion
// grants, stat4..stat6 of Misc Extra Stats included, and the item records do
// not hold those). The same files the game loads for RotW games:
// data\global\excel\ in the mod folder.
// ---------------------------------------------------------------------------

auto Widen(const char* text) -> std::wstring {
    if (text == nullptr || *text == '\0') return {};
    const int size = MultiByteToWideChar(CP_UTF8, 0, text, -1, nullptr, 0);
    if (size <= 1) return {};
    std::wstring out(static_cast<std::size_t>(size - 1), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text, -1, out.data(), size);
    return out;
}

auto ReadWholeFile(const std::wstring& path, std::string& out) -> bool {
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER size{};
    bool ok = GetFileSizeEx(file, &size) && size.QuadPart > 0 && size.QuadPart < 64LL * 1024 * 1024;
    if (ok) {
        out.assign(static_cast<std::size_t>(size.QuadPart), '\0');
        DWORD read = 0;
        ok = ReadFile(file, out.data(), static_cast<DWORD>(out.size()), &read, nullptr) && read == out.size();
    }
    CloseHandle(file);
    return ok;
}

auto ReadExcel(const wchar_t* name, std::string& out) -> bool {
    std::vector<std::wstring> roots;
    const std::wstring mod = Widen(Context->activeMod);
    for (const wchar_t* base : { Context->modDirectory, Context->scopeRootDirectory }) {
        if (base == nullptr || *base == L'\0') continue;
        std::wstring root(base);
        while (!root.empty() && (root.back() == L'\\' || root.back() == L'/')) root.pop_back();
        roots.push_back(root);
        if (!mod.empty()) roots.push_back(root + L"\\" + mod + L".mpq");
    }
    for (const std::wstring& root : roots) {
        if (ReadWholeFile(root + L"\\data\\global\\excel\\" + name, out)) return true;
    }
    return false;
}

struct Table {
    std::vector<std::string>              header;
    std::vector<std::vector<std::string>> rows;
    auto Column(std::string_view name) const noexcept -> int {
        for (std::size_t i = 0; i < header.size(); ++i) {
            if (header[i] == name) return static_cast<int>(i);
        }
        return -1;
    }
    auto Cell(std::size_t row, int column) const noexcept -> std::string_view {
        if (column < 0 || row >= rows.size() || static_cast<std::size_t>(column) >= rows[row].size()) return {};
        return rows[row][static_cast<std::size_t>(column)];
    }
};

auto ParseTable(const std::string& text) -> Table {
    Table table;
    std::size_t position = 0;
    bool        first    = true;
    while (position < text.size()) {
        std::size_t end = text.find('\n', position);
        if (end == std::string::npos) end = text.size();
        std::string_view line(text.data() + position, end - position);
        position = end + 1;
        if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
        std::vector<std::string> cells;
        std::size_t start = 0;
        while (true) {
            const std::size_t tab = line.find('\t', start);
            cells.emplace_back(line.substr(start, tab == std::string_view::npos ? std::string_view::npos : tab - start));
            if (tab == std::string_view::npos) break;
            start = tab + 1;
        }
        if (first) { table.header = std::move(cells); first = false; }
        else table.rows.push_back(std::move(cells));
    }
    return table;
}

// Name -> id through the "*ID" column (row order when it is missing).
auto IdByName(const Table& table, const char* nameColumn, std::string_view name) noexcept -> std::int32_t {
    const int nameAt = table.Column(nameColumn);
    const int idAt   = table.Column("*ID");
    if (nameAt < 0 || name.empty()) return -1;
    for (std::size_t row = 0; row < table.rows.size(); ++row) {
        if (table.Cell(row, nameAt) != name) continue;
        if (idAt < 0) return static_cast<std::int32_t>(row);
        std::int32_t id = -1;
        return ParseInt(table.Cell(row, idAt), 0, 65535, id) ? id : static_cast<std::int32_t>(row);
    }
    return -1;
}

// Game thread, tables loaded.
void BuildPotionEffects(std::vector<PotionEffect>& effects, std::vector<std::string>& problems) {
    std::string miscText, statesText, statsText;
    if (!ReadExcel(L"misc.txt", miscText) || !ReadExcel(L"states.txt", statesText)
        || !ReadExcel(L"itemstatcost.txt", statsText)) {
        problems.emplace_back("misc.txt, states.txt or itemstatcost.txt could not be read from the mod's "
                              "data\\global\\excel folder; Compounded Synergy has no potion effects");
        return;
    }
    const Table misc   = ParseTable(miscText);
    const Table states = ParseTable(statesText);
    const Table stats  = ParseTable(statsText);
    const int codeAt  = misc.Column("code");
    const int stateAt = misc.Column("state");
    if (codeAt < 0 || stateAt < 0) {
        problems.emplace_back("misc.txt has no code or state column; Compounded Synergy has no potion effects");
        return;
    }
    const char* statColumns[MaxEffectStats]{ "stat1", "stat2", "stat3", "stat4", "stat5", "stat6" };
    std::vector<std::string> codes = Settings.healingManaCodes;
    codes.insert(codes.end(), Settings.utilityCodes.begin(), Settings.utilityCodes.end());
    for (const std::string& code : codes) {
        std::size_t row = 0;
        while (row < misc.rows.size() && misc.Cell(row, codeAt) != code) ++row;
        if (row == misc.rows.size()) continue;   // already reported by the item lookup
        const std::string_view stateName = misc.Cell(row, stateAt);
        if (stateName.empty()) continue;
        const std::int32_t state = IdByName(states, "state", stateName);
        if (state < 0) {
            problems.push_back(code + ": state " + std::string(stateName) + " is not in states.txt");
            continue;
        }
        PotionEffect* effect = nullptr;
        for (PotionEffect& existing : effects) {
            if (existing.state == state) { effect = &existing; break; }
        }
        if (effect == nullptr) {
            effects.push_back(PotionEffect{ state, {} });
            effect = &effects.back();
        }
        for (const char* column : statColumns) {
            const std::string_view statName = misc.Cell(row, misc.Column(column));
            if (statName.empty()) continue;
            const std::int32_t stat = IdByName(stats, "Stat", statName);
            if (stat < 0) {
                problems.push_back(code + ": stat " + std::string(statName) + " is not in itemstatcost.txt");
                continue;
            }
            if (!Contains(effect->stats, stat) && effect->stats.size() < MaxEffectStats) effect->stats.push_back(stat);
        }
    }
}

// uniqueitems.txt index names compare like the game's own: up to the NUL,
// letter case ignored.
auto SameUniqueName(const char* row, std::string_view name) noexcept -> bool {
    std::size_t length = 0;
    while (length < UniqueNameBytes && row[length] != '\0') ++length;
    if (length != name.size()) return false;
    for (std::size_t i = 0; i < length; ++i) {
        if (std::tolower(static_cast<unsigned char>(row[i])) != std::tolower(static_cast<unsigned char>(name[i]))) {
            return false;
        }
    }
    return true;
}

// Game thread, after a table load: every sweep_uniques name against
// uniqueitems.txt as loaded, so a misspelt name shows up in the log. The icons
// themselves look the name up in the item's own tables when they are drawn.
void CheckSweepUniques() noexcept {
    if (!SweepUniquesListed || TableSvc == nullptr) return;
    std::vector<std::pair<const std::uint8_t*, std::uint32_t>> banks;
    for (const D2RL::DataTables::Bank bank : { D2RL::DataTables::Bank::Rotw, D2RL::DataTables::Bank::Lod,
                                               D2RL::DataTables::Bank::Classic }) {
        D2RL::DataTables::TableView view{};
        view.structSize = D2RL::DataTables::TableViewSize;
        if (TableSvc->getTable(Context, bank, D2RL::DataTables::TableId::UniqueItems, &view)
                == D2RL::DataTables::Result::Success
            && D2RL::DataTables::HasTableViewField(&view, D2RL::DataTables::TableViewRequiredSize)
            && view.rows != nullptr && view.rowSize == UniqueRowBytes) {
            banks.emplace_back(static_cast<const std::uint8_t*>(view.rows), view.rowCount);
        }
    }
    if (banks.empty()) {
        Context->LogWarn("Alchemy: uniqueitems.txt could not be read to check sweep_uniques; the names are only "
                         "matched when the icons are drawn.");
        return;
    }
    std::string missing;
    for (const ReincarnationEntry& entry : Settings.reincarnation) {
        for (const std::string& name : entry.sweepUniques) {
            bool found = false;
            for (const auto& [rows, count] : banks) {
                for (std::uint32_t row = 0; row < count && !found; ++row) {
                    found = SameUniqueName(reinterpret_cast<const char*>(rows + static_cast<std::size_t>(row) * UniqueRowBytes
                        + UniqueNameOffset), name);
                }
                if (found) break;
            }
            if (!found) missing += (missing.empty() ? "" : ", ") + name;
        }
    }
    if (!missing.empty()) {
        D2RL::LogWarnF(Context, "Alchemy: these sweep_uniques are not uniqueitems.txt index names and get no sweep: %s.",
            missing.c_str());
    }
}

// Game thread: the data-tables-loaded listener, after every table load.
// Item rows are table indexes, so the lists are rebuilt from the codes each time.
std::mutex ItemsLock;

void ResolveItems() noexcept {
    std::vector<std::int32_t> healing;
    std::vector<std::int32_t> utility;
    std::vector<std::int32_t> rejuvenation;
    std::vector<std::string>  unknown;
    const auto resolve = [&unknown](const std::vector<std::string>& codes, std::vector<std::int32_t>& rows) {
        for (const std::string& code : codes) {
            std::int32_t row = -1;
            if (ItemRow(code, row)) rows.push_back(row);
            else unknown.push_back(code);
        }
    };
    resolve(Settings.healingManaCodes, healing);
    resolve(Settings.utilityCodes, utility);
    resolve(Settings.rejuvenationCodes, rejuvenation);
    // Each sweep item's row, packed with the entry whose cooldown it shows.
    std::vector<std::int32_t> sweep;
    for (std::size_t entry = 0; entry < Settings.reincarnation.size(); ++entry) {
        std::vector<std::int32_t> rows;
        resolve(Settings.reincarnation[entry].sweepCodes, rows);
        for (const std::int32_t row : rows) {
            if (row >= 0 && row <= SweepRowMask) {
                sweep.push_back(row | (static_cast<std::int32_t>(entry) << SweepEntryShift));
            }
        }
    }
    if (sweep.size() > MaxSweepItems) {
        D2RL::LogWarnF(Context, "Alchemy: %zu sweep_items are listed; only the first %zu get the cooldown sweep.",
            sweep.size(), MaxSweepItems);
    }
    CheckSweepUniques();
    for (std::size_t i = 0; i < MaxSweepItems; ++i) {
        SweepItems[i].store(i < sweep.size() ? sweep[i] : -1, std::memory_order_relaxed);
    }
    if (!unknown.empty()) {
        std::string list;
        for (const std::string& code : unknown) list += (list.empty() ? "" : ", ") + code;
        D2RL::LogWarnF(Context, "Alchemy: these [items] codes are not in the item tables and are ignored: %s.",
            list.c_str());
    }
    std::vector<PotionEffect> effects;
    std::vector<std::string>  problems;
    BuildPotionEffects(effects, problems);
    for (const std::string& problem : problems) D2RL::LogWarnF(Context, "Alchemy: %s.", problem.c_str());
    {
        std::lock_guard lock(ItemsLock);
        HealingManaItems  = std::move(healing);
        UtilityItems      = std::move(utility);
        RejuvenationItems = std::move(rejuvenation);
        UnknownCodes      = std::move(unknown);
        PotionEffects     = std::move(effects);
    }
    ItemsResolved.store(true, std::memory_order_release);
}

// Find Potion: on once its call is relayed; the counters the console prints.
std::atomic<bool>          FindPotionPartActive{ false };
std::atomic<std::uint64_t> FindPotionClassFinds{ 0 };   // finds whose potion has a [find_potion] class
std::atomic<std::uint64_t> FindPotionItems{ 0 };        // items those classes dropped
std::atomic<std::uint64_t> FindPotionEmpty{ 0 };        // a class that dropped nothing
std::atomic<std::uint64_t> FindPotionMissing{ 0 };      // no such class: the potion dropped as before
std::array<std::atomic<bool>, MaxFindPotionClasses> FindPotionMissLogged{};
std::array<std::atomic<bool>, MaxFindPotionClasses> FindPotionEmptyLogged{};

// A treasure class name as its runtime row holds it (+50h, to the end of the
// row) against a [find_potion] name, letter case ignored.
auto SameTreasureClassName(const std::uint8_t* row, std::string_view name) noexcept -> bool {
    if (name.empty() || name.size() >= TreasureNameBytes) return false;
    const auto* text = reinterpret_cast<const char*>(row + TreasureNameOffset);
    for (std::size_t i = 0; i < name.size(); ++i) {
        if (text[i] == '\0') return false;
        if (std::tolower(static_cast<unsigned char>(text[i])) != std::tolower(static_cast<unsigned char>(name[i]))) {
            return false;
        }
    }
    return text[name.size()] == '\0';
}

// Game thread, after a table load: every [find_potion] class against
// treasureclassex.txt as loaded, so a misspelt name shows up in the log. The
// finds themselves look the class up in their own game's tables.
void CheckFindPotionClasses() noexcept {
    for (std::atomic<bool>& flag : FindPotionMissLogged) flag.store(false, std::memory_order_relaxed);
    for (std::atomic<bool>& flag : FindPotionEmptyLogged) flag.store(false, std::memory_order_relaxed);
    if (!FindPotionPartActive.load(std::memory_order_relaxed) || TableSvc == nullptr) return;
    for (const D2RL::DataTables::Bank bank : { D2RL::DataTables::Bank::Rotw, D2RL::DataTables::Bank::Lod }) {
        D2RL::DataTables::TableView view{};
        view.structSize = D2RL::DataTables::TableViewSize;
        if (TableSvc->getTable(Context, bank, D2RL::DataTables::TableId::TreasureClasses, &view)
                != D2RL::DataTables::Result::Success
            || !D2RL::DataTables::HasTableViewField(&view, D2RL::DataTables::TableViewRequiredSize)
            || view.rows == nullptr) {
            continue;
        }
        if (view.rowSize != TreasureRowBytes) {
            D2RL::LogWarnF(Context, "Alchemy: treasure class rows are %u bytes in this build, not %zu; [find_potion] is "
                "not checked at load.", view.rowSize, TreasureRowBytes);
            return;
        }
        const auto*         rows  = static_cast<const std::uint8_t*>(view.rows);
        const std::uint32_t count = std::min<std::uint32_t>(view.rowCount, static_cast<std::uint32_t>(TreasureIdLimit));
        std::string missing;
        for (const FindPotionClass& entry : Settings.findPotionClasses) {
            bool found = false;
            for (std::uint32_t row = 1; row < count && !found; ++row) {
                found = SameTreasureClassName(rows + static_cast<std::size_t>(row) * TreasureRowBytes, entry.name);
            }
            if (!found) missing += (missing.empty() ? "" : ", ") + entry.code + " = \"" + entry.name + "\"";
        }
        if (!missing.empty()) {
            D2RL::LogWarnF(Context, "Alchemy: these [find_potion] classes are not in treasureclassex.txt, so those potions "
                "drop as before: %s.", missing.c_str());
        }
        return;
    }
    Context->LogWarn("Alchemy: treasureclassex.txt could not be read to check [find_potion]; the classes are only looked "
                     "up when a find drops.");
}

void __cdecl OnTablesLoaded(const D2RL::PluginContext*, const D2RL::Lifecycle::DataTablesLoadedEvent*, void*) noexcept {
    ResolveItems();
    CheckFindPotionClasses();
}

// 0 = none of the lists, 1 = healing or mana potion, 2 = utility potion, 3 = rejuvenation potion.
auto ItemGroup(std::int32_t row) noexcept -> int {
    std::lock_guard lock(ItemsLock);
    if (Contains(HealingManaItems, row)) return 1;
    if (Contains(UtilityItems, row)) return 2;
    if (Contains(RejuvenationItems, row)) return 3;
    return 0;
}

void WatchTables() noexcept {
    const D2RL::LifecycleService* lifecycle = nullptr;
    if (Context->QueryService(&lifecycle) != D2RL::ServiceQueryResult::Success
        || !D2RL::HasLifecycleServiceField(lifecycle, D2RL::LifecycleServiceRequiredSize)) {
        Context->LogWarn("Alchemy: no lifecycle service; the potion perks find no items.");
        return;
    }
    D2RL::Lifecycle::DataTablesLoadedListener tables{};
    tables.structSize = D2RL::Lifecycle::DataTablesLoadedListenerSize;
    tables.callback   = &OnTablesLoaded;
    D2RL::Lifecycle::ListenerHandle handle = D2RL::Lifecycle::InvalidHandle;
    if (lifecycle->registerDataTablesLoadedListener(Context, &tables, &handle) != D2RL::Lifecycle::Result::Success) {
        Context->LogWarn("Alchemy: table loads could not be watched; the potion perks find no items.");
    }
}

// ---------------------------------------------------------------------------
// Per-player records (server thread). Records belong to one server game; a
// new game starts empty, so frame numbers and unit ids never leak across.
// ---------------------------------------------------------------------------

// Caller holds TimerLock.
auto DataFor(void* game, std::uint32_t id, bool create) noexcept -> PlayerData* {
    if (game != TimerGame) {
        Timers.clear();
        TimerGame = game;
        ClearPublishedCooldowns();
    }
    for (PlayerData& data : Timers) {
        if (data.id == id) return &data;
    }
    if (!create) return nullptr;
    PlayerData& added = Timers.emplace_back();
    added.id = id;
    return &added;
}

// Caller holds TimerLock.
void DropIfEmpty(std::uint32_t id) noexcept {
    for (std::size_t i = 0; i < Timers.size(); ++i) {
        const PlayerData& data = Timers[i];
        if (data.id != id) continue;
        const bool counting = std::any_of(data.cooldownEnd.begin(), data.cooldownEnd.end(),
            [](std::int32_t end) { return end != 0; });
        if (!counting && data.invulnerableEnd == 0 && data.synergy.empty()) {
            Timers.erase(Timers.begin() + static_cast<std::ptrdiff_t>(i));
        }
        return;
    }
}

auto Invulnerable(void* game, void* player) noexcept -> bool {
    std::lock_guard lock(TimerLock);
    const PlayerData* data = DataFor(game, UnitId(player), false);
    return data != nullptr && data->invulnerableEnd != 0 && GameFrame(game) < data->invulnerableEnd;
}

auto ContextOf(void* game) noexcept -> std::uint32_t {
    return Read<std::uint8_t>(game, GameContextOffset);
}

void EndState(void* game, void* unit, std::int32_t state) noexcept {
    void* list = StateList(unit, state);
    if (list == nullptr) return;
    RemoveList(unit, list);
    FreeList(ContextOf(game), list);
}

// ---------------------------------------------------------------------------
// Compounded Synergy
// ---------------------------------------------------------------------------

auto Scaled(std::int32_t base, std::int32_t percent) noexcept -> std::int32_t {
    return static_cast<std::int32_t>(static_cast<std::int64_t>(base) * percent / 100);
}

// Puts every running effect of the player back to its unscaled values (before
// a potion is used, so the game merges a new potion into true values; and when
// the perk is gone). Caller holds TimerLock.
void RestoreSynergy(void* game, void* player, PlayerData& data) noexcept {
    const std::uint32_t context = ContextOf(game);
    for (SynergyEntry& entry : data.synergy) {
        if (StateList(player, entry.state) != entry.list || entry.list == nullptr) continue;
        for (std::uint8_t k = 0; k < entry.count; ++k) {
            if (entry.written[k] == entry.base[k]) continue;
            const std::int32_t live = ReadListStat(context, entry.list, entry.stats[k], 0);
            if (live == entry.written[k]) SetListStat(context, entry.list, entry.stats[k], entry.base[k], 0);
            entry.written[k] = entry.base[k];
        }
        entry.percent = 100;
    }
}

// Counts the player's running potion effects and scales each one's stats to
// 100 + percent x count. A value the game changed since the last write (a new
// potion, a merge) becomes that stat's new unscaled value. Caller holds TimerLock.
void UpdateSynergy(void* game, void* player, PlayerData& data) noexcept {
    std::vector<PotionEffect> effects;
    {
        std::lock_guard lock(ItemsLock);
        effects = PotionEffects;
    }
    const bool learned = HasPerk(player, Settings.compoundedSynergy);
    if (!learned) {
        if (!data.synergy.empty()) {
            RestoreSynergy(game, player, data);
            data.synergy.clear();
        }
        return;
    }
    struct Running { const PotionEffect* effect; void* list; };
    std::vector<Running> running;
    for (const PotionEffect& effect : effects) {
        if (void* list = StateList(player, effect.state)) running.push_back(Running{ &effect, list });
    }
    const std::int32_t percent = 100 + Settings.synergyPercent * static_cast<std::int32_t>(running.size());
    const std::uint32_t context = ContextOf(game);

    std::vector<SynergyEntry> next;
    next.reserve(running.size());
    for (const Running& run : running) {
        SynergyEntry entry;
        bool         known = false;
        for (const SynergyEntry& old : data.synergy) {
            if (old.state == run.effect->state && old.list == run.list) { entry = old; known = true; break; }
        }
        if (!known) {
            entry.state   = run.effect->state;
            entry.list    = run.list;
            entry.percent = 100;
            entry.count   = static_cast<std::uint8_t>(run.effect->stats.size());
            for (std::uint8_t k = 0; k < entry.count; ++k) {
                entry.stats[k]   = run.effect->stats[k];
                entry.base[k]    = ReadListStat(context, run.list, entry.stats[k], 0);
                entry.written[k] = entry.base[k];
            }
        }
        bool changed = entry.percent != percent;
        for (std::uint8_t k = 0; k < entry.count; ++k) {
            const std::int32_t live = ReadListStat(context, run.list, entry.stats[k], 0);
            if (live != entry.written[k]) {   // the game wrote it: new unscaled value
                entry.base[k]    = live;
                entry.written[k] = live;
                entry.percent    = 100;
                changed          = true;
            }
        }
        if (changed) {
            for (std::uint8_t k = 0; k < entry.count; ++k) {
                if (entry.base[k] == 0) continue;
                const std::int32_t value = Scaled(entry.base[k], percent);
                if (value != entry.written[k]) SetListStat(context, run.list, entry.stats[k], value, 0);
                entry.written[k] = value;
            }
            entry.percent = percent;
            SynergyRescales.fetch_add(1, std::memory_order_relaxed);
        }
        next.push_back(entry);
    }
    data.synergy = std::move(next);
}

auto SynergyPercent(void* player) noexcept -> std::int32_t {
    if (!HasPerk(player, Settings.compoundedSynergy)) return 100;
    std::lock_guard lock(ItemsLock);
    std::int32_t running = 0;
    for (const PotionEffect& effect : PotionEffects) {
        if (StateList(player, effect.state) != nullptr) ++running;
    }
    return 100 + Settings.synergyPercent * running;
}

// ---------------------------------------------------------------------------
// Serendipity
// ---------------------------------------------------------------------------

void GrantRandomShrine(void* game, void* player) noexcept {
    if (Settings.serendipityShrines.empty()) return;
    const std::int32_t code = Settings.serendipityShrines[Random() % Settings.serendipityShrines.size()];
    const std::uint32_t context = ContextOf(game);
    ShrineHandlerFn handler{};
    std::memcpy(&handler, reinterpret_cast<const void*>(Base + ShrineChartRva + ChartStride * static_cast<std::size_t>(code)),
        sizeof(handler));
    const std::int32_t rows = ShrineRowCount(context);
    const std::uint8_t* record = nullptr;
    std::int32_t        row    = -1;
    for (std::int32_t index = 0; index < rows; ++index) {
        const std::uint8_t* candidate = ShrineRecord(context, static_cast<std::uint32_t>(index));
        if (candidate != nullptr && candidate[ShrineRecordCode] == code) { record = candidate; row = index; break; }
    }
    if (handler == nullptr || record == nullptr) {
        SerendipityMisses.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    // The operate record a shrine object gets (58F2A0): the player is both the
    // source of the timed effect and its target.
    std::uint8_t operate[OperateBytes]{};
    std::memcpy(operate + OperateGame, &game, sizeof(game));
    std::memcpy(operate + OperateSource, &player, sizeof(player));
    std::memcpy(operate + OperatePlayer, &player, sizeof(player));
    std::memcpy(operate + OperateRow, &row, sizeof(row));
    handler(context, operate, record);
    SerendipityShrines.fetch_add(1, std::memory_order_relaxed);
}

// ---------------------------------------------------------------------------
// Using an item (Bottomless Vials, Savvy Elixirs, Serendipity, Compounded Synergy)
// ---------------------------------------------------------------------------

// Whirlwind (celestialrayone.whirlwind) lets a potion be used while
// whirlwinding, when the player is Uninterruptible: its "potion-scope" service
// says whether that is happening for this player right now. Looked up on the
// first potion and again every 64 until found.
struct WhirlwindScopeService {
    std::uint32_t serviceSize;
    std::uint32_t serviceVersion;
    bool(__cdecl* potionUseOpen)(void* player) noexcept;
};
const WhirlwindScopeService* WhirlwindScope = nullptr;
std::uint32_t                WhirlwindLookups = 0;

auto WhirlwindAllowsPotion(void* player) noexcept -> bool {
    if (WhirlwindScope == nullptr && (WhirlwindLookups++ % 64) == 0) {
        const D2RL::PluginCommunicationService* communication = nullptr;
        if (Context->QueryService(&communication) == D2RL::ServiceQueryResult::Success
            && D2RL::HasPluginCommunicationServiceField(communication, D2RL::PluginCommunicationServiceRequiredSize)) {
            const D2RL::PluginCommunication::AcquireServiceRequest request{
                .structSize       = D2RL::PluginCommunication::AcquireServiceRequestSize,
                .flags            = 0,
                .providerPluginId = "celestialrayone.whirlwind",
                .name             = "potion-scope",
                .minimumVersion   = 1,
                .minimumTableSize = static_cast<std::uint32_t>(sizeof(WhirlwindScopeService)),
            };
            D2RL::PluginCommunication::AcquiredService acquired{};
            acquired.structSize = D2RL::PluginCommunication::AcquiredServiceSize;
            if (communication->acquireService(Context, &request, &acquired) == D2RL::PluginCommunication::Result::Success
                && acquired.table != nullptr) {
                WhirlwindScope = static_cast<const WhirlwindScopeService*>(acquired.table);
            }
        }
    }
    return WhirlwindScope != nullptr && WhirlwindScope->potionUseOpen != nullptr && WhirlwindScope->potionUseOpen(player);
}

// The use-item handler 4F40C0 refuses the use while the player is dead or
// Uninterruptible (its first check); a kept potion follows the same rule.
auto MayUseNow(void* player) noexcept -> bool {
    if (IsDead(player) != 0) return false;
    return CheckState(player, StateUninterruptible) == 0 || WhirlwindAllowsPotion(player);
}

// Relay for the two calls to the use-item handler (4C1447, 4F2417). The
// handler itself stays unhooked: Whirlwind owns its entry.
std::int64_t __fastcall RelayItemUse(void* game, void* player, std::uint8_t* request, std::uint64_t flag,
                                     std::uint64_t arg5, std::uint64_t arg6) noexcept {
    if (!Active.load(std::memory_order_relaxed) || game == nullptr || request == nullptr || !IsPlayer(player)
        || request[RequestConsumed] != 1 || request[RequestTarget] != 0 || (request[RequestLocation] & 0xFD) != 0
        || Read<std::uint32_t>(request, RequestPlayerId) != UnitId(player)) {
        return OriginalItemUse(game, player, request, flag, arg5, arg6);
    }
    ServerGame.store(game, std::memory_order_relaxed);
    void* item = ServerUnit(game, UnitItem, Read<std::uint32_t>(request, RequestItemId));
    const int group = item != nullptr ? ItemGroup(Read<std::int32_t>(item, UnitClassOffset)) : 0;
    if (group == 0) return OriginalItemUse(game, player, request, flag, arg5, arg6);
    if (group == 1) HealingManaUses.fetch_add(1, std::memory_order_relaxed);
    if (group == 2) UtilityUses.fetch_add(1, std::memory_order_relaxed);

    // Running effects go back to their true values so the game merges a new
    // potion into them correctly; UpdateSynergy scales everything again after.
    {
        std::lock_guard lock(TimerLock);
        if (PlayerData* data = DataFor(game, UnitId(player), false)) RestoreSynergy(game, player, *data);
    }
    const std::int32_t lifeBefore = ReadStat(player, StatLife, 0);
    const std::int32_t manaBefore = ReadStat(player, StatMana, 0);
    const std::int32_t percent    = group == 3 ? SynergyPercent(player) : 100;

    bool keep  = false;
    bool savvy = false;
    if (group == 1) keep = HasPerk(player, Settings.bottomlessVials);
    else if (group == 2) keep = savvy = HasPerk(player, Settings.savvyElixirs) && Roll(Settings.savvyChance);

    std::int64_t result = 0;
    if (keep && MayUseNow(player) && CanUse(game, player, item, nullptr)) {
        UseEffect(game, player, item);
        (savvy ? SavvySaves : BottomlessSaves).fetch_add(1, std::memory_order_relaxed);
    } else {
        if (keep) CanUseRefused.fetch_add(1, std::memory_order_relaxed);
        result = OriginalItemUse(game, player, request, flag, arg5, arg6);   // 0 = used
    }

    if (result == 0) {
        if (group == 2 && HasPerk(player, Settings.serendipity) && Roll(Settings.serendipityChance)) {
            GrantRandomShrine(game, player);
        }
        if (group == 3 && percent > 100) {
            const std::int32_t life = ReadStat(player, StatLife, 0);
            const std::int32_t mana = ReadStat(player, StatMana, 0);
            if (life > lifeBefore) {
                const std::int64_t extra = static_cast<std::int64_t>(life - lifeBefore) * (percent - 100) / 100;
                WriteStat(player, StatLife, static_cast<std::int32_t>(std::min<std::int64_t>(life + extra, MaxLife(player))), 0);
            }
            if (mana > manaBefore) {
                const std::int64_t extra = static_cast<std::int64_t>(mana - manaBefore) * (percent - 100) / 100;
                WriteStat(player, StatMana, static_cast<std::int32_t>(std::min<std::int64_t>(mana + extra, MaxMana(player))), 0);
            }
            RejuvenationBoosts.fetch_add(1, std::memory_order_relaxed);
        }
    }
    {
        std::lock_guard lock(TimerLock);
        if (PlayerData* data = DataFor(game, UnitId(player), true)) {
            UpdateSynergy(game, player, *data);
            DropIfEmpty(data->id);
        }
    }
    return result;
}

// ---------------------------------------------------------------------------
// Taking damage (Elixir of Reincarnation; Blessed Vials through the service)
// ---------------------------------------------------------------------------

// Relay for 44D093: the life write after a hit. value is the new life, 0 when the hit is lethal.
void __fastcall RelayLifeWrite(void* unit, std::int32_t stat, std::int32_t value, std::int32_t layer) noexcept {
    void* game = ServerGame.load(std::memory_order_relaxed);
    if (Active.load(std::memory_order_relaxed) && game != nullptr && stat == StatLife && IsPlayer(unit)) {
        const std::int32_t life = ReadStat(unit, StatLife, 0);
        if (value < life && Invulnerable(game, unit)) {
            InvulnerableHits.fetch_add(1, std::memory_order_relaxed);
            value = life;
        } else if (value < LethalBelow) {
            // The first entry whose state the player carries and whose cooldown is ready saves them.
            for (std::size_t index = 0; index < Settings.reincarnation.size(); ++index) {
                const ReincarnationEntry& entry = Settings.reincarnation[index];
                if (!HasState(unit, entry.state)) continue;
                // While the cooldown runs the state saves nobody, even when something gave it back (a proc, say).
                if (entry.cooldownStat != NoStat && ReadStat(unit, entry.cooldownStat, 0) > 0) {
                    EntryRefusals[index].fetch_add(1, std::memory_order_relaxed);
                    continue;
                }
                const std::int32_t frame    = GameFrame(game);
                const bool         cooldown = entry.cooldownStat != NoStat && entry.cooldownSeconds > 0;
                EndState(game, unit, entry.state);
                value = MaxLife(unit);
                WriteStat(unit, StatMana, MaxMana(unit), 0);
                if (cooldown) WriteStat(unit, entry.cooldownStat, entry.cooldownSeconds, 0);
                {
                    std::lock_guard lock(TimerLock);
                    if (PlayerData* data = DataFor(game, UnitId(unit), true)) {
                        data->invulnerableEnd = entry.invulnerableFrames > 0 ? frame + entry.invulnerableFrames : 0;
                        if (cooldown) {   // entries sharing the cooldown stat share this countdown
                            data->cooldownEnd[entry.owner]   = frame + entry.cooldownSeconds * 25;
                            data->cooldownTotal[entry.owner] = entry.cooldownSeconds;
                        }
                    }
                }
                Reincarnations.fetch_add(1, std::memory_order_relaxed);
                EntrySaves[index].fetch_add(1, std::memory_order_relaxed);
                break;
            }
        }
    }
    WriteStat(unit, stat, value, layer);
}

// ---------------------------------------------------------------------------
// Potion effects at full life or mana (Endless Vials)
// ---------------------------------------------------------------------------

// Relay for 42E693 (state 100) and 42E4F2 (state 106).
void* __fastcall RelayPotionState(void* unit, std::int32_t state) noexcept {
    void* list = StateList(unit, state);
    if (list != nullptr && Active.load(std::memory_order_relaxed) && IsPlayer(unit)
        && HasPerk(unit, Settings.endlessVials)) {
        EndlessKeeps.fetch_add(1, std::memory_order_relaxed);
        return nullptr;
    }
    return list;
}

// ---------------------------------------------------------------------------
// Find Potion through a treasure class (server)
// ---------------------------------------------------------------------------

// The class id 441300 takes: the row's index in the game's own tables (397290
// indexes the same rows), or 0 when no row has that name.
auto TreasureClassId(void* game, std::string_view name) noexcept -> std::uint32_t {
    const std::uint32_t context = ContextOf(game);
    if (context >= 4) return 0;
    const std::uint8_t* tables = DataTables(static_cast<std::uint8_t>(context));
    if (tables == nullptr) return 0;
    const auto* rows  = Read<const std::uint8_t*>(tables, TablesTreasureRows);
    const auto  count = std::min<std::uint64_t>(Read<std::uint64_t>(tables, TablesTreasureCount), TreasureIdLimit);
    if (rows == nullptr) return 0;
    for (std::uint64_t id = 1; id < count; ++id) {
        if (SameTreasureClassName(rows + id * TreasureRowBytes, name)) return static_cast<std::uint32_t>(id);
    }
    return 0;
}

// Relay for 567CD4, srvdofunc 69's potion drop (436340). The stub in front of
// it hands over the caster as the fifth argument (see the header). A potion
// with a [find_potion] class drops that class instead; any other drops as before.
void* __fastcall RelayFindPotionDrop(void* game, void* corpse, std::uint32_t code, std::int32_t quality,
                                     void* caster) noexcept {
    if (!FindPotionPartActive.load(std::memory_order_relaxed) || !Active.load(std::memory_order_relaxed)
        || game == nullptr || corpse == nullptr || caster == nullptr) {
        return DropItemWithCode(game, corpse, code, quality);
    }
    std::size_t index = 0;
    while (index < Settings.findPotionClasses.size() && CodeValue(Settings.findPotionClasses[index].code) != code) ++index;
    if (index == Settings.findPotionClasses.size()) return DropItemWithCode(game, corpse, code, quality);
    const FindPotionClass& entry = Settings.findPotionClasses[index];
    FindPotionClassFinds.fetch_add(1, std::memory_order_relaxed);

    const std::uint32_t id = TreasureClassId(game, entry.name);
    if (id == 0) {
        FindPotionMissing.fetch_add(1, std::memory_order_relaxed);
        if (!FindPotionMissLogged[index].exchange(true)) {
            D2RL::LogWarnF(Context, "Alchemy: [find_potion] %s: this game has no treasure class \"%s\"; that potion "
                "drops as before.", entry.code.c_str(), entry.name.c_str());
        }
        return DropItemWithCode(game, corpse, code, quality);
    }
    // As Find Treasure drops a corpse's class (447B80): the corpse drops, the
    // caster is the killer, NoDrop is ignored. Quality 0 rolls each item's
    // quality as on a kill. One slot and max 1: one item per find, as before.
    void*        items[1]{};
    std::int32_t dropped = 0;
    GenerateDrops(game, corpse, caster, id, 0, NewItemLevel(corpse, 0), 1, items, &dropped, 1);
    if (dropped <= 0 || items[0] == nullptr) {
        FindPotionEmpty.fetch_add(1, std::memory_order_relaxed);
        if (!FindPotionEmptyLogged[index].exchange(true)) {
            D2RL::LogWarnF(Context, "Alchemy: [find_potion] %s: treasure class \"%s\" dropped nothing (a ConditionCalc "
                "on the class itself?).", entry.code.c_str(), entry.name.c_str());
        }
        return nullptr;
    }
    FindPotionItems.fetch_add(1, std::memory_order_relaxed);
    return items[0];
}

// ---------------------------------------------------------------------------
// Per-frame tick (invulnerability and cooldown ends, Compounded Synergy)
// ---------------------------------------------------------------------------

constexpr std::int32_t SynergyTickFrames = 5;

// The entries that run a countdown: one per cooldown stat, the first entry using it.
auto RunsCooldown(std::size_t index) noexcept -> bool {
    const ReincarnationEntry& entry = Settings.reincarnation[index];
    return entry.cooldownStat != NoStat && entry.owner == index;
}

// Relay for 42E74D: the mana regen call, every frame for every player.
char __fastcall RelayManaRegen(void* game, void* player) noexcept {
    const char result = ManaRegen(game, player);
    if (!Active.load(std::memory_order_relaxed) || game == nullptr || !IsPlayer(player)) return result;
    ServerGame.store(game, std::memory_order_relaxed);

    const std::int32_t  frame   = GameFrame(game);
    const std::uint32_t id      = UnitId(player);
    const std::size_t   entries = Settings.reincarnation.size();
    // Each cooldown stat holds the seconds left; the client draws the sweep from it.
    std::array<std::int32_t, MaxReincarnations> onPlayer{};
    std::array<std::int32_t, MaxReincarnations> secondsNow{};
    std::array<std::int32_t, MaxReincarnations> total{};
    bool                                        anyOnPlayer = false;
    for (std::size_t i = 0; i < entries; ++i) {
        if (!RunsCooldown(i)) continue;
        onPlayer[i]   = ReadStat(player, Settings.reincarnation[i].cooldownStat, 0);
        secondsNow[i] = onPlayer[i];
        total[i]      = Settings.reincarnation[i].longest;
        anyOnPlayer   = anyOnPlayer || onPlayer[i] > 0;
    }
    {
        std::lock_guard lock(TimerLock);
        PlayerData* data = DataFor(game, id, false);
        if (data == nullptr && anyOnPlayer) data = DataFor(game, id, true);
        if (data == nullptr && frame % SynergyTickFrames == 0 && HasPerk(player, Settings.compoundedSynergy)) {
            data = DataFor(game, id, true);
        }
        if (data != nullptr) {
            if (data->invulnerableEnd != 0 && frame >= data->invulnerableEnd) data->invulnerableEnd = 0;
            for (std::size_t i = 0; i < entries; ++i) {
                if (!RunsCooldown(i)) continue;
                std::int32_t& end = data->cooldownEnd[i];
                if (end == 0 && onPlayer[i] > 0) {
                    // Saved with the character, or set without this plugin: run on from the seconds it holds.
                    const std::int32_t longest = std::max(Settings.reincarnation[i].longest, 1);
                    end                    = frame + std::min(onPlayer[i], longest) * 25;
                    data->cooldownTotal[i] = longest;
                }
                if (end == 0) continue;
                const std::int32_t left = end - frame;
                if (left <= 0) {
                    end           = 0;
                    secondsNow[i] = 0;
                } else {
                    secondsNow[i] = (left + 24) / 25;
                }
                total[i] = data->cooldownTotal[i];
            }
            if (frame % SynergyTickFrames == 0) UpdateSynergy(game, player, *data);
            DropIfEmpty(id);
        }
    }
    for (std::size_t i = 0; i < entries; ++i) {
        if (!RunsCooldown(i)) continue;
        if (secondsNow[i] != onPlayer[i]) WriteStat(player, Settings.reincarnation[i].cooldownStat, secondsNow[i], 0);
        if (secondsNow[i] > 0 || onPlayer[i] > 0) PublishCooldown(id, i, secondsNow[i], total[i]);
    }
    return result;
}

// ---------------------------------------------------------------------------
// The cooldown sweep (client)
// ---------------------------------------------------------------------------

std::atomic<bool>          SweepPartActive{ false };
const D2RL::ThreadService* ThreadSvc{};
GameSprite                 SweepSprite;
std::atomic<bool>          SweepLoadQueued{ false };
std::atomic<bool>          SweepLoadTried{ false };

// UI thread: the sprite loads like an ImageWidget's, outside any draw pass.
// 858990 puts data/hd/global/ui/ in front of the name; the name is tried as
// written and then in lower case.
void __cdecl LoadSweepSprite(const D2RL::PluginContext*, void*) noexcept {
    if (SweepLoadTried.exchange(true)) return;
    std::string name = Settings.sweepSprite;
    SpriteLoad(&SweepSprite, name.c_str(), 0);
    if (SweepSprite.hd == nullptr) {
        for (char& c : name) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (name != Settings.sweepSprite) SpriteLoad(&SweepSprite, name.c_str(), 0);
    }
    if (SweepSprite.hd == nullptr) {
        SweepLoadState.store(2);
        D2RL::LogWarnF(Context, "Alchemy: the cooldown sweep sprite data/hd/global/ui/%s did not load (legacy "
            "graphics, or no such sprite); no sweep is drawn.", Settings.sweepSprite.c_str());
        return;
    }
    SweepLoadState.store(1);
    D2RL::LogInfoF(Context, "Alchemy: cooldown sweep sprite %s loaded (%d frames, %d px wide).", name.c_str(),
        Read<std::int32_t>(SweepSprite.hd, HdSpriteFrames), Read<std::int32_t>(SweepSprite.hd, HdSpriteWidth));
}

// The entry whose sweep_uniques names the item's unique, or -1. The row and
// its name come from the tables of the item's own data context.
auto UniqueSweepEntry(void* item) noexcept -> std::int32_t {
    const std::uint8_t context = ItemContext(item);
    if (context >= 4) return -1;
    const std::uint8_t* tables = DataTables(context);
    if (tables == nullptr) return -1;
    const auto*         rows  = Read<const std::uint8_t*>(tables, TablesUniqueRows);
    const auto          count = Read<std::uint64_t>(tables, TablesUniqueCount);
    const std::uint32_t row   = ItemFileIndex(item);
    if (rows == nullptr || row >= count) return -1;
    const std::uint8_t* record = rows + static_cast<std::size_t>(row) * UniqueRowBytes;
    if (Read<std::uint16_t>(record, UniqueRowIndex) != static_cast<std::uint16_t>(row)) return -1;
    const auto* name = reinterpret_cast<const char*>(record + UniqueNameOffset);
    for (std::size_t entry = 0; entry < Settings.reincarnation.size(); ++entry) {
        for (const std::string& wanted : Settings.reincarnation[entry].sweepUniques) {
            if (SameUniqueName(name, wanted)) return static_cast<std::int32_t>(entry);
        }
    }
    return -1;
}

// The [[reincarnation]] entry whose cooldown the item's icon shows, or -1: a
// unique named in sweep_uniques first, then the item codes of sweep_items.
auto SweepEntry(void* item) noexcept -> std::int32_t {
    if (SweepUniquesListed && ItemQuality(item) == QualityUnique) {
        const std::int32_t entry = UniqueSweepEntry(item);
        if (entry >= 0) return entry;
    }
    const std::int32_t row = Read<std::int32_t>(item, UnitClassOffset);
    for (const auto& slot : SweepItems) {
        const std::int32_t value = slot.load(std::memory_order_relaxed);
        if (value < 0) return -1;
        if ((value & SweepRowMask) == row) return value >> SweepEntryShift;
    }
    return -1;
}

auto LocalPlayerUnit() noexcept -> void* {
    return LocalPlayer(LocalIndex());
}

// Inline hook on 15BB80, the one renderer of every item icon (inventory, belt,
// stash, cube). The game draws the icon first; the sweep goes on top, at the
// icon's corner and scaled from the sweep's frame width to the icon's.
void __fastcall HookRenderItemIcon(void* item, std::uint64_t packedXY, float scale, void* params) noexcept {
    const std::int32_t slot = Read<std::int32_t>(reinterpret_cast<const void*>(Base + IconSlotIndexRva), 0);
    OriginalRenderItemIcon(item, packedXY, scale, params);
    if (!SweepPartActive.load(std::memory_order_relaxed) || item == nullptr) return;
    const std::int32_t index = SweepEntry(item);
    if (index < 0 || static_cast<std::size_t>(index) >= Settings.reincarnation.size()) return;
    SweepIconsSeen.fetch_add(1, std::memory_order_relaxed);
    // The HD branch puts the icon's sprite in the slot it started at and moves on by one.
    const std::int32_t next = Read<std::int32_t>(reinterpret_cast<const void*>(Base + IconSlotIndexRva), 0);
    if (slot < 0 || slot >= IconSlotCount || next != (slot + 1) % IconSlotCount) {
        SweepSkipped.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    // The countdown of the entry's cooldown stat, run by the first entry using it.
    const std::size_t         owner = Settings.reincarnation[static_cast<std::size_t>(index)].owner;
    const ReincarnationEntry& timer = Settings.reincarnation[owner];
    void* player = LocalPlayerUnit();
    if (player == nullptr || timer.cooldownStat == NoStat) return;
    std::int32_t total   = 0;
    std::int32_t seconds = PublishedSeconds(UnitId(player), owner, total);
    if (seconds < 0) {
        seconds = ReadStat(player, timer.cooldownStat, 0);
        total   = timer.longest;
    }
    SweepLastSeconds.store(seconds, std::memory_order_relaxed);
    if (seconds <= 0) return;
    if (SweepSprite.hd == nullptr) {
        if (!SweepLoadTried.load() && ThreadSvc != nullptr && !SweepLoadQueued.exchange(true)) {
            if (ThreadSvc->runOnUiThread(Context, &LoadSweepSprite, nullptr) != D2RL::Threads::Result::Success) {
                SweepLoadQueued.store(false);
            }
        }
        return;
    }
    const auto* iconSlot = reinterpret_cast<const std::uint8_t*>(Base + IconSlotsRva) + IconSlotStride * static_cast<std::size_t>(slot);
    const GameSprite* icon = Read<const GameSprite*>(iconSlot, IconSlotSprite);
    if (icon == nullptr || icon->hd == nullptr) { SweepSkipped.fetch_add(1, std::memory_order_relaxed); return; }
    const std::int32_t frames      = Read<std::int32_t>(SweepSprite.hd, HdSpriteFrames);
    const std::int32_t sweepWide   = Read<std::int32_t>(SweepSprite.hd, HdSpriteWidth);
    const std::int32_t textureWide = Read<std::int32_t>(SweepSprite.hd, HdSpriteTextureWidth);
    const std::int32_t iconWide    = Read<std::int32_t>(icon->hd, HdSpriteWidth);
    const std::int32_t iconHigh    = Read<std::int32_t>(icon->hd, HdSpriteHeight);
    if (frames <= 0 || sweepWide <= 0 || textureWide <= 0 || iconWide <= 0 || iconHigh <= 0) {
        SweepSkipped.fetch_add(1, std::memory_order_relaxed);
        return;
    }

    total = std::max(total, 1);
    const std::int32_t left = std::min(seconds, total);
    // Frame 0 is the full sweep; the last frame is nearly gone.
    std::int32_t frame = static_cast<std::int32_t>(static_cast<std::int64_t>(total - left) * frames / total);
    frame = std::clamp(frame, 0, frames - 1);

    SpriteDrawParams draw{};
    draw.tint[0] = static_cast<float>(Settings.sweepColor[0]) / 255.0f;
    draw.tint[1] = static_cast<float>(Settings.sweepColor[1]) / 255.0f;
    draw.tint[2] = static_cast<float>(Settings.sweepColor[2]) / 255.0f;
    draw.tint[3] = 1.0f;   // as Crossbow Charges writes its sweep widget's tint: colour applied
    draw.fade    = static_cast<float>(Settings.sweepOpacity) / 100.0f;
    draw.frame   = frame;
    const std::int32_t position[2]{
        static_cast<std::int32_t>(static_cast<std::uint32_t>(packedXY & 0xFFFFFFFFULL)),
        static_cast<std::int32_t>(static_cast<std::uint32_t>(packedXY >> 32)),
    };
    // The sweep in the icon's size (see the header): the seven fields 79DA50 reads, with the
    // icon's frame width, height and anchor, and the sweep's texture width scaled like its
    // frame width so a frame keeps its U span. At the icon's position and scale its corners
    // are the icon's.
    alignas(16) std::uint8_t shape[HdSpriteDrawBytes]{};
    const auto* sweep    = static_cast<const std::uint8_t*>(SweepSprite.hd);
    const auto* iconHd   = static_cast<const std::uint8_t*>(icon->hd);
    const auto  stripped = static_cast<std::int32_t>((static_cast<std::int64_t>(textureWide) * iconWide * 2 + sweepWide)
        / (2 * static_cast<std::int64_t>(sweepWide)));
    std::memcpy(shape + HdSpriteTextureWidth, &stripped, sizeof(stripped));
    std::memcpy(shape + HdSpriteHeight, iconHd + HdSpriteHeight, sizeof(std::int32_t));
    std::memcpy(shape + HdSpriteAnchorX, iconHd + HdSpriteAnchorX, sizeof(std::int32_t));
    std::memcpy(shape + HdSpriteAnchorY, iconHd + HdSpriteAnchorY, sizeof(std::int32_t));
    std::memcpy(shape + HdSpriteFrames, sweep + HdSpriteFrames, sizeof(std::int32_t));
    std::memcpy(shape + HdSpriteWidth, iconHd + HdSpriteWidth, sizeof(std::int32_t));
    std::memcpy(shape + HdSpriteTexture, sweep + HdSpriteTexture, sizeof(void*));
    SpriteDrawHd(shape, position, scale, &draw);
    SweepDraws.fetch_add(1, std::memory_order_relaxed);
}

// ---------------------------------------------------------------------------
// Service for Monster Affixes (Mana Burn) and Shrine Rework (damage taken)
// ---------------------------------------------------------------------------

struct AlchemyService {
    std::uint32_t serviceSize;
    std::uint32_t serviceVersion;
    bool(__cdecl* manaBurnImmune)(void* player) noexcept;
    std::int32_t(__cdecl* incomingDamagePercent)(void* unit) noexcept;   // version 2
};

bool __cdecl ManaBurnImmune(void* player) noexcept {
    if (!Active.load(std::memory_order_relaxed) || !IsPlayer(player)) return false;
    if (!HasPerk(player, Settings.blessedVials) || !HasState(player, Settings.manaState)) return false;
    ManaBurnsBlocked.fetch_add(1, std::memory_order_relaxed);
    return true;
}

// Percent of the incoming damage the unit takes: 100, or less with Blessed Vials.
std::int32_t __cdecl IncomingDamagePercent(void* unit) noexcept {
    if (!Active.load(std::memory_order_relaxed) || !IsPlayer(unit)) return 100;
    if (!HasPerk(unit, Settings.blessedVials) || !HasState(unit, Settings.healingState)) return 100;
    BlessedHits.fetch_add(1, std::memory_order_relaxed);
    return 100 - Settings.blessedReduction;
}

constexpr AlchemyService ServiceTable{
    .serviceSize           = sizeof(AlchemyService),
    .serviceVersion        = 2,
    .manaBurnImmune        = &ManaBurnImmune,
    .incomingDamagePercent = &IncomingDamagePercent,
};

void PublishService() noexcept {
    const D2RL::PluginCommunicationService* communication = nullptr;
    if (Context->QueryService(&communication) != D2RL::ServiceQueryResult::Success
        || !D2RL::HasPluginCommunicationServiceField(communication, D2RL::PluginCommunicationServiceRequiredSize)) {
        Context->LogWarn("Alchemy: no plugin communication service; Blessed Vials does nothing.");
        return;
    }
    const D2RL::PluginCommunication::PublishServiceRequest request{
        .structSize     = D2RL::PluginCommunication::PublishServiceRequestSize,
        .flags          = 0,
        .name           = ServiceName,
        .serviceVersion = 2,
        .tableSize      = static_cast<std::uint32_t>(sizeof(ServiceTable)),
        .table          = &ServiceTable,
    };
    if (communication->publishService(Context, &request) != D2RL::PluginCommunication::Result::Success) {
        Context->LogWarn("Alchemy: the alchemy service was refused; Blessed Vials does nothing.");
    }
}

// ---------------------------------------------------------------------------
// Installing
// ---------------------------------------------------------------------------

constexpr std::size_t RelaySlotBytes   = 16;
constexpr std::size_t RelaySlots       = 6;
constexpr std::size_t RelayBytes       = 4096;
constexpr std::size_t CasterStubOffset = 0x100;   // Find Potion's stub, after the plain slots
void*                 RelayPage        = nullptr;

auto CanEncodeRel32(std::uintptr_t from, std::uintptr_t to) noexcept -> bool {
    const auto distance = static_cast<std::int64_t>(to) - static_cast<std::int64_t>(from);
    return distance >= INT32_MIN + 16 && distance <= INT32_MAX - 16;
}

auto AllocateNear(std::uintptr_t hint, std::size_t size) noexcept -> void* {
    SYSTEM_INFO systemInfo{};
    GetSystemInfo(&systemInfo);
    const auto granularity = static_cast<std::uintptr_t>(systemInfo.dwAllocationGranularity);
    const auto aligned     = hint & ~(granularity - 1U);
    for (std::uintptr_t delta = granularity; delta < 0x7000'0000ULL; delta += granularity) {
        const std::uintptr_t candidates[2]{ aligned + delta, aligned > delta ? aligned - delta : 0 };
        for (const std::uintptr_t candidate : candidates) {
            if (candidate == 0 || !CanEncodeRel32(hint, candidate) || !CanEncodeRel32(hint, candidate + size)) continue;
            if (void* memory = VirtualAlloc(reinterpret_cast<void*>(candidate), size, MEM_COMMIT | MEM_RESERVE,
                    PAGE_READWRITE)) {
                return memory;
            }
        }
    }
    return nullptr;
}

struct RelayedCall {
    std::uint64_t rva;
    std::uint8_t  original[5];
    void*         target;
    const char*   name;
};

const RelayedCall RelayedCalls[RelaySlots]{
    { UseItemCallRva,    { 0xE8,0x74,0x2C,0x03,0x00 }, reinterpret_cast<void*>(&RelayItemUse),    "use item 0x4C1447" },
    { QueuedUseCallRva,  { 0xE8,0xA4,0x1C,0x00,0x00 }, reinterpret_cast<void*>(&RelayItemUse),    "queued use item 0x4F2417" },
    { LifeWriteCallRva,  { 0xE8,0x78,0xAC,0xEA,0xFF }, reinterpret_cast<void*>(&RelayLifeWrite),  "life write 0x44D093" },
    { HealthPotCallRva,  { 0xE8,0xA8,0x72,0xEC,0xFF }, reinterpret_cast<void*>(&RelayPotionState), "Healing Potion state 0x42E693" },
    { ManaPotCallRva,    { 0xE8,0x49,0x74,0xEC,0xFF }, reinterpret_cast<void*>(&RelayPotionState), "Mana Potion state 0x42E4F2" },
    { ManaRegenCallRva,  { 0xE8,0xFE,0xFB,0xFF,0xFF }, reinterpret_cast<void*>(&RelayManaRegen),  "mana regen 0x42E74D" },
};

auto BuildRelayPage() noexcept -> bool {
    // The lowest and highest relayed sites are 0x42E4F2 and 0x567CD4 (Find Potion); one page reaches all of them.
    void* page = AllocateNear(Base + ManaPotCallRva, RelayBytes);
    if (page == nullptr) return false;
    auto* code = static_cast<std::uint8_t*>(page);
    std::memset(code, 0xCC, RelayBytes);
    for (std::size_t slot = 0; slot < RelaySlots; ++slot) {
        const std::uint8_t jump[6]{ 0xFF, 0x25, 0x00, 0x00, 0x00, 0x00 };
        std::memcpy(code + slot * RelaySlotBytes, jump, sizeof(jump));
        const auto absolute = reinterpret_cast<std::uint64_t>(RelayedCalls[slot].target);
        std::memcpy(code + slot * RelaySlotBytes + sizeof(jump), &absolute, sizeof(absolute));
    }
    // Find Potion: mov [rsp+28h], rbp (the caster, into the fifth-argument slot) / jmp [rip+0] / the relay.
    const std::uint8_t casterStub[11]{ 0x48, 0x89, 0x6C, 0x24, 0x28, 0xFF, 0x25, 0x00, 0x00, 0x00, 0x00 };
    std::memcpy(code + CasterStubOffset, casterStub, sizeof(casterStub));
    const auto findPotionRelay = reinterpret_cast<std::uint64_t>(&RelayFindPotionDrop);
    std::memcpy(code + CasterStubOffset + sizeof(casterStub), &findPotionRelay, sizeof(findPotionRelay));
    DWORD previous = 0;
    if (!VirtualProtect(code, RelayBytes, PAGE_EXECUTE_READ, &previous)) {
        VirtualFree(page, 0, MEM_RELEASE);
        return false;
    }
    FlushInstructionCache(GetCurrentProcess(), code, RelayBytes);
    RelayPage = page;
    return true;
}

auto PatchRelayedCalls() noexcept -> bool {
    for (std::size_t slot = 0; slot < RelaySlots; ++slot) {
        const RelayedCall& call  = RelayedCalls[slot];
        const std::uintptr_t site = Base + call.rva;
        const std::uintptr_t to   = reinterpret_cast<std::uintptr_t>(RelayPage) + slot * RelaySlotBytes;
        if (!CanEncodeRel32(site + 5, to)) {
            D2RL::LogErrorF(Context, "Alchemy: the relay page is out of reach of the %s call.", call.name);
            return false;
        }
        std::uint8_t bytes[5]{ 0xE8 };
        const auto displacement = static_cast<std::int32_t>(static_cast<std::int64_t>(to)
            - static_cast<std::int64_t>(site + 5));
        std::memcpy(bytes + 1, &displacement, sizeof(displacement));
        if (!Context->PatchBytes(call.rva, call.original, 5, bytes, 5)) {
            D2RL::LogErrorF(Context, "Alchemy: the %s call could not be relayed.", call.name);
            return false;
        }
    }
    return true;
}

// The handlers Serendipity calls through the shrine chart, as this build holds them.
constexpr std::uint64_t ChartHandlerRva[ChartSlots]{
    0, 0x590DB0, 0x590D10, 0x590D60, 0x590E30, 0x590EC0, 0x591E20, 0x591F70, 0x591E20, 0x591E20, 0x591E20, 0x591E20,
    0x591F00, 0x591E20, 0x5920D0, 0x591E20, 0, 0, 0, 0, 0, 0, 0, 0 };

auto CheckShrineChart() noexcept -> bool {
    bool ok = true;
    for (const std::int32_t code : Settings.serendipityShrines) {
        std::uint64_t live = 0;
        std::memcpy(&live, reinterpret_cast<const void*>(Base + ShrineChartRva + ChartStride * static_cast<std::size_t>(code)),
            sizeof(live));
        if (ChartHandlerRva[code] == 0 || live != Base + ChartHandlerRva[code]) {
            D2RL::LogErrorF(Context, "Alchemy: shrine code %d has no effect handler this plugin knows; Serendipity is "
                "off.", code);
            ok = false;
        }
    }
    return ok;
}

auto InstallEverything() noexcept -> bool {
    if (!BuildRelayPage()) {
        Context->LogError("Alchemy: no relay page could be placed near the game code; nothing was installed.");
        return false;
    }
    return PatchRelayedCalls();
}

// The cooldown sweep is its own part: when its checks fail, the perks still install.
void InstallSweep() noexcept {
    const bool anyCooldown = std::any_of(Settings.reincarnation.begin(), Settings.reincarnation.end(),
        [](const ReincarnationEntry& entry) { return entry.cooldownStat != NoStat; });
    if (!Settings.sweepEnabled || !anyCooldown) return;
    if (!VerifyList(SweepWitnesses)) {
        Context->LogError("Alchemy: the cooldown sweep's checks failed on this build; no sweep is drawn.");
        return;
    }
    if (Context->QueryService(&ThreadSvc) != D2RL::ServiceQueryResult::Success
        || !D2RL::HasThreadServiceField(ThreadSvc, D2RL::ThreadServiceRequiredSize)) {
        ThreadSvc = nullptr;
        Context->LogError("Alchemy: no thread service to load the sweep sprite; no sweep is drawn.");
        return;
    }
    if (!Context->InstallInlineHook(RenderItemIconRva, RenderItemIconBytes, static_cast<std::uint32_t>(sizeof(RenderItemIconBytes)),
            reinterpret_cast<void*>(&HookRenderItemIcon), reinterpret_cast<void**>(&OriginalRenderItemIcon))) {
        Context->LogError("Alchemy: the item icon hook at 0x15BB80 failed to install; no sweep is drawn.");
        return;
    }
    SweepPartActive.store(true);
}

// Find Potion is its own part too: when its checks fail, the finds drop their
// potion as before and everything else stays installed.
constexpr std::uint8_t FindPotionCallOriginal[5]{ 0xE8, 0x67, 0xE6, 0xEC, 0xFF };   // call 436340

void InstallFindPotion() noexcept {
    if (!Settings.findPotionEnabled || Settings.findPotionClasses.empty() || RelayPage == nullptr) return;
    if (!VerifyList(FindPotionWitnesses)) {
        Context->LogError("Alchemy: Find Potion's checks failed on this build; finds drop their potion as before.");
        return;
    }
    const std::uintptr_t site = Base + FindPotionCallRva;
    const std::uintptr_t to   = reinterpret_cast<std::uintptr_t>(RelayPage) + CasterStubOffset;
    if (!CanEncodeRel32(site + 5, to)) {
        Context->LogError("Alchemy: the relay page is out of reach of the Find Potion call; finds drop their potion as before.");
        return;
    }
    std::uint8_t bytes[5]{ 0xE8 };
    const auto displacement = static_cast<std::int32_t>(static_cast<std::int64_t>(to) - static_cast<std::int64_t>(site + 5));
    std::memcpy(bytes + 1, &displacement, sizeof(displacement));
    // On before the patch: until then the relay is unreachable, and it forwards untouched when off.
    FindPotionPartActive.store(true);
    if (!Context->PatchBytes(FindPotionCallRva, FindPotionCallOriginal, 5, bytes, 5)) {
        FindPotionPartActive.store(false);
        Context->LogError("Alchemy: the Find Potion call 0x567CD4 could not be relayed; finds drop their potion as before.");
    }
}

// ---------------------------------------------------------------------------
// Console
// ---------------------------------------------------------------------------

auto StateName(PluginState state) noexcept -> const char* {
    switch (state) {
    case PluginState::Loading:          return "loading";
    case PluginState::DisabledByConfig: return "disabled by config";
    case PluginState::UnsupportedBuild: return "unsupported build, nothing installed";
    case PluginState::InstallFailed:    return "install failed, see the log";
    case PluginState::Active:           return "active";
    }
    return "unknown";
}

auto __cdecl StatusCommand(D2R::Game::Client*, const D2RL::ConsoleCommandContext* command, void*) noexcept
        -> D2RL::ConsoleCommandResult {
    if (command == nullptr || command->plugin == nullptr) return D2RL::ConsoleCommandResult::Failed;
    char line[512];
    std::snprintf(line, sizeof(line), "Alchemy %s: %s.", PluginVersion, StateName(State));
    command->plugin->WriteConsoleMessage(line);
    std::snprintf(line, sizeof(line),
        "Potion uses seen: healing/mana %llu, utility %llu. Kept: Bottomless %llu, Savvy %llu; a kept potion the "
        "game refused %llu times.",
        static_cast<unsigned long long>(HealingManaUses.load()), static_cast<unsigned long long>(UtilityUses.load()),
        static_cast<unsigned long long>(BottomlessSaves.load()), static_cast<unsigned long long>(SavvySaves.load()),
        static_cast<unsigned long long>(CanUseRefused.load()));
    command->plugin->WriteConsoleMessage(line);
    std::snprintf(line, sizeof(line),
        "Serendipity shrines %llu (no shrine found %llu). Compounded Synergy rescales %llu, rejuvenation boosts %llu. "
        "Endless Vials frames kept %llu.",
        static_cast<unsigned long long>(SerendipityShrines.load()), static_cast<unsigned long long>(SerendipityMisses.load()),
        static_cast<unsigned long long>(SynergyRescales.load()), static_cast<unsigned long long>(RejuvenationBoosts.load()),
        static_cast<unsigned long long>(EndlessKeeps.load()));
    command->plugin->WriteConsoleMessage(line);
    void* player = (SweepPartActive.load() && LocalIndex != nullptr) ? LocalPlayerUnit() : nullptr;
    {
        const std::int32_t loadState = SweepLoadState.load();
        std::snprintf(line, sizeof(line),
            "Cooldown sweep: %s. Sweep items drawn %llu (last saw %d s), sweeps drawn %llu, skipped %llu. Sprite: %s.",
            SweepPartActive.load() ? "on" : "off",
            static_cast<unsigned long long>(SweepIconsSeen.load()), SweepLastSeconds.load(),
            static_cast<unsigned long long>(SweepDraws.load()), static_cast<unsigned long long>(SweepSkipped.load()),
            loadState == 1 ? "loaded" : (loadState == 2 ? "FAILED to load, see the log" : "not loaded yet"));
    }
    command->plugin->WriteConsoleMessage(line);
    std::snprintf(line, sizeof(line),
        "Blessed Vials hits %llu, Mana Burns stopped %llu. Reincarnations %llu, hits during invulnerability %llu.",
        static_cast<unsigned long long>(BlessedHits.load()), static_cast<unsigned long long>(ManaBurnsBlocked.load()),
        static_cast<unsigned long long>(Reincarnations.load()), static_cast<unsigned long long>(InvulnerableHits.load()));
    command->plugin->WriteConsoleMessage(line);
    // One line per [[reincarnation]] entry. Entries sharing a cooldown stat show the same countdown.
    for (std::size_t index = 0; index < Settings.reincarnation.size(); ++index) {
        const ReincarnationEntry& entry = Settings.reincarnation[index];
        const unsigned long long  saves    = EntrySaves[index].load();
        const unsigned long long  refusals = EntryRefusals[index].load();
        if (entry.cooldownStat == NoStat) {
            std::snprintf(line, sizeof(line), "Reincarnation %zu: state %d, saves %llu, no cooldown.", index + 1,
                entry.state, saves);
        } else {
            std::int32_t published = -1, onClient = -1, total = 0;
            if (player != nullptr) {
                published = PublishedSeconds(UnitId(player), entry.owner, total);
                onClient  = ReadStat(player, entry.cooldownStat, 0);
            }
            std::snprintf(line, sizeof(line), "Reincarnation %zu: state %d, saves %llu, lethal hits during the "
                "cooldown %llu. Cooldown stat %d now: %d s from this game's server, %d s in the client's stat.",
                index + 1, entry.state, saves, refusals, entry.cooldownStat, published, onClient);
        }
        command->plugin->WriteConsoleMessage(line);
    }
    std::snprintf(line, sizeof(line), "Find Potion: %s, %zu classes. Finds through a class %llu: items dropped %llu, "
        "nothing dropped %llu, class not found %llu (the potion dropped as before).",
        FindPotionPartActive.load() ? "on" : "off", Settings.findPotionClasses.size(),
        static_cast<unsigned long long>(FindPotionClassFinds.load()), static_cast<unsigned long long>(FindPotionItems.load()),
        static_cast<unsigned long long>(FindPotionEmpty.load()), static_cast<unsigned long long>(FindPotionMissing.load()));
    command->plugin->WriteConsoleMessage(line);
    if (ItemsResolved.load()) {
        std::lock_guard lock(ItemsLock);
        std::snprintf(line, sizeof(line), "Items: %zu healing/mana, %zu utility, %zu rejuvenation, %zu unknown codes; "
            "%zu potion effects for Compounded Synergy.", HealingManaItems.size(), UtilityItems.size(),
            RejuvenationItems.size(), UnknownCodes.size(), PotionEffects.size());
    } else {
        std::snprintf(line, sizeof(line), "Items: not resolved yet; the item tables have not finished loading.");
    }
    command->plugin->WriteConsoleMessage(line);
    if (!ConfigProblems.empty()) command->plugin->WriteConsoleMessage(ConfigProblems.c_str());
    return D2RL::ConsoleCommandResult::Handled;
}

constexpr D2RL::PluginInfo PluginInfoData{
    .infoSize    = D2RL::PluginInfoSize,
    .abiVersion  = D2RL_PLUGIN_ABI_VERSION,
    .id          = PluginIdText,
    .name        = "Alchemy",
    .version     = PluginVersion,
    .author      = "CelestialRayOne",
    .description = "Alchemist perks: Blessed Vials, Compounded Synergy, Savvy Elixirs, Endless Vials, Bottomless "
                   "Vials, Serendipity and the Elixir of Reincarnation; Find Potion drops through treasure classes.",
    .flags       = D2RL::PluginFlags::Shared | D2RL::PluginFlags::NativeHooks,
};

} // namespace

D2RL_PLUGIN_EXPORT auto D2RLoaderGetPluginInfo() noexcept -> const D2RL::PluginInfo* {
    return &PluginInfoData;
}

D2RL_PLUGIN_EXPORT auto D2RLoaderLoadPlugin(const D2RL::PluginContext* context) noexcept -> bool {
    Context = context;
    if (Context == nullptr || Context->exeBase == 0) return false;
    Base = Context->exeBase;

    if (!Context->RegisterConsoleCommand("alchemy", &StatusCommand, "Alchemy status and counters.")) {
        Context->LogWarn("Alchemy: the console command could not be registered.");
    }
    ReadConfiguration();
    if (!ConfigProblems.empty()) D2RL::LogWarnF(Context, "Alchemy: %s", ConfigProblems.c_str());
    if (!Settings.enabled) {
        State = PluginState::DisabledByConfig;
        Context->LogInfo("Alchemy: disabled by config.");
        return true;
    }
    if (!VerifyAll()) {
        State = PluginState::UnsupportedBuild;
        Context->LogError("Alchemy: this D2R build does not match; nothing was installed.");
        return true;
    }
    BindNatives();
    if (!CheckShrineChart()) Settings.serendipityShrines.clear();
    if (Context->QueryService(&TableSvc) != D2RL::ServiceQueryResult::Success) TableSvc = nullptr;
    if (TableSvc == nullptr) Context->LogWarn("Alchemy: no data table service; the potion perks find no items.");
    else WatchTables();
    PublishService();
    // Active before the code is patched: every hook and relay forwards untouched until then.
    Active.store(true);
    if (!InstallEverything()) {
        State = PluginState::InstallFailed;
        return true;
    }
    State = PluginState::Active;
    InstallSweep();
    InstallFindPotion();
    D2RL::LogInfoF(Context, "Alchemy %s: active (reincarnation entries %zu, cooldown sweep %s, find potion %s).",
        PluginVersion, Settings.reincarnation.size(), SweepPartActive.load() ? "on" : "off",
        FindPotionPartActive.load() ? "on" : "off");
    return true;
}

D2RL_PLUGIN_EXPORT void D2RLoaderUnloadPlugin() noexcept {
    Active.store(false);
}
