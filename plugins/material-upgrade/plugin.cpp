// =============================================================================
//  Material Upgrade  -  celestialrayone.material-upgrade
//
//  Shift + click on a material in the stash's stack tabs upgrades it in place,
//  Alt + click downgrades it, following the recipe chains in the config file:
//    Shift + Left Click   one upgrade (3 Chipped Amethyst -> 1 Flawed Amethyst)
//    Shift + Right Click  the whole stack, the remainder stays
//    Alt + Left Click     one downgrade (1 Wo Rune -> 1 Nu Rune)
//    Alt + Right Click    the whole stack
//  The tooltip of an item that converts gets lines saying what each click
//  makes and uses, or what is missing.
//
// -----------------------------------------------------------------------------
//  CLIENT (UI thread)
// -----------------------------------------------------------------------------
//  The stack-tab slot widget keeps its stack item at widget+0x608. Three of its
//  methods are hooked:
//    2CF760  left click. Native: cursor item -> deposit, Ctrl -> inventory or
//            cube, Shift -> belt when 15A110 allows (rejuvenation potions),
//            anything else -> sound 3.
//    2CE2C0  right click. Native: every action needs Ctrl, without it sound 3.
//    2CEBC0  the grey hint lines of the tooltip ("Ctrl + Left Click to Move to
//            Inventory ..."), built into the string object at its 2nd argument.
//  A click hook acts only on Shift (upgrade) or Alt (downgrade), alone and
//  without Ctrl, with nothing on the cursor, on a non-empty stack whose item
//  code is in a chain of that kind. Everything else goes to the native method
//  untouched; natively Alt is ignored by both methods (a plain left click picks
//  the item up, a right click without Ctrl plays sound 3). The stash owner is picked the way 2CF760 picks it:
//  23B980 ? 46D9C0 : 46DA50. The request (player, owner and stack unit ids,
//  item code, one or all) goes to the host over the plugin's network channel.
//
//  CTRL + LEFT CLICK, BELT FIRST. Natively Ctrl + Left Click moves one unit to
//  the inventory (2CF833 15A2F0, then vtable +0xD8 mode 0 at 2CF84E, which
//  sends 159B30(item, owner, 1)), or with Shift to the cube when 23BAC0
//  allows (2CF802). The left-click hook takes the inventory case first: for an
//  item the game can put in the belt right now (23B820 and 15A110, the test of
//  the game's own Shift + Left Click belt move at 2CF872 / 2CF882) it sends
//  that same belt request, 159B30(item, owner, 3), then plays the item's sound
//  (1C3420), as 2CF8E2..2CF8F8 do. Not beltable, or a full belt: the game's
//  inventory move runs. The host handles the request in its own withdraw
//  (4C4AF0 operation 1 -> 4FB350, destination 3 = belt), so nothing is added
//  on the host and a host without the plugin accepts it too.
//  The hint lines follow the clicks. While Ctrl + Left Click goes to the belt,
//  the game's "Ctrl + Left Click to Move to Inventory" (2CED01: sprintf of
//  InventoryItemTooltipAppenderMoveInventory with
//  InventoryItemTooltipKeyComboCtrlLC) is rewritten with the string
//  MaterialUpgradeMoveToBelt. The count the game adds to that line for a
//  stack of 2 or more (2CED0A: InventoryItemTooltipAppenderBulkMovePostfix
//  with min(count, 3), the Ctrl + Right Click move) stays, since that click
//  goes to the belt first as well (below). When the game shows no such line
//  (15A2F0 says no), that text is added as the first line, with that same
//  count added the same way. On an item whose Shift + Left Click is the
//  plugin's (any item in an upgrade chain), the game's Shift belt hint
//  InventoryItemTooltipAppenderEquip (2CEE0E, added when 15A110 allows) is
//  removed. A line that cannot be matched is left as the game wrote it.
//
//  CTRL + RIGHT CLICK, BELT FIRST. Natively Ctrl + Right Click (2CE2C0) moves
//  n = min(count, 3) units (2CE2E9) to the inventory when 15A2F0 allows
//  (2CF590 mode 0 -> 1599A0(item, owner, 1, n): packet 0x63 operation 2), or
//  with Shift to the cube when 23BAC0 allows (1599A0(item, owner, 2, n) at
//  2CE392). The right-click hook takes the inventory case first, on the same
//  test as Ctrl + Left Click: of the n units, the k the belt takes go there,
//  1599A0(item, owner, 3, k), and the other n - k go to the inventory,
//  1599A0(item, owner, 1, n - k), when 15A2F0 allows; then the item's sound,
//  once, as 2CF643 plays it. The host runs its one-unit withdraw once per unit
//  (4C4AF0 operation 2 -> 4F6250 -> 4FB350) and stops at the first that
//  fails, and a unit the belt has no cell for fails there with a sound
//  (491960(player, 20)) and a log line, so k is counted before sending, with
//  the belt search of 3862D0 (INVENTORY_GetFreeBeltSlot, which 4FB350 runs
//  for each unit at 4FBB8F) run on a copy of the belt's cells, one unit after
//  another:
//    belt size   the belts.txt record (3C8400) of the belt in body cell 8
//                (349720), row 2 without a belt; its box count at +4
//    cells       grid 1 of the player's inventory (38B070 with the belt grid
//                info at 237B638): width +10h, height +11h, cells +18h
//    each unit   the first column whose bottom cell holds an item of the same
//                type (36A8D0) and that starts below the box count: its lowest
//                empty cell below that count; else the first column whose
//                bottom cell is empty; else the belt is full
//  Each unit counted is written into the copy, so the next one sees it, the
//  way the host's next withdraw sees the unit before it. The first cell must
//  be the one 3862D0 itself gives for the item; when it is not, only that one
//  unit goes to the belt and the console counts a mismatch. Both requests are
//  the game's own, so a host without the plugin accepts them too. The game
//  code only this click relies on is checked on its own (BatchWitnesses):
//  when it does not match, Ctrl + Right Click keeps the game's own move and
//  the rest of the plugin runs.
//
// -----------------------------------------------------------------------------
//  HOST (game thread)
// -----------------------------------------------------------------------------
//  The checks are the ones the game's own stack-tab request handler (packet
//  0x63, 4C4AF0) runs before any operation:
//    owner = 48FE80(game, 0, ownerId) with state 209 (3351B0)
//    388BA0(owner's inventory) == the player's unit id
//    node  = 388E10(player's inventory, owner id): +8 clear (38CD60) and
//            +9 set (38CD50)
//  then the stack must be that owner's slot for its class (46D8F0).
//
//  One unit out of a stack, the way the native withdraw (4FB350) does it:
//    382D20 captures the slot's packet state, 46DC90(owner, class) takes one,
//    then 36D8F0(slot, 0x4000, 0) / 34E460(slot, 1) /
//    535F60(game, owner, slot, 32, 4, mode, page, node, xy) / 34E460(slot, 0) /
//    36DEA0(slot, 0).
//  One unit into a stack, the way the native deposit (4F92F0) does it:
//    without a slot for the class, 43D530 creates the item with the native
//    withdraw's arguments and 46DAE0 files it as the slot (48FAA0 frees it if
//    that fails); then the credit call the deposit makes at 4F9CD3 (D2RLoader
//    relays it to D2RCore's IncrementAdvancedStashItemQuantity, which enforces
//    the stack-tab limit), then 382D00(state, 4) / 34E410(slot, 1) /
//    535F60(game, owner, slot, 0x2000, 2, mode, page, node, xy) /
//    34E410(slot, 0).
//    The client handles 0x2000 in 1C2B10: an item it does not know yet is
//    decoded with its count and filed as the slot, a known slot gets +1.
//  Each upgrade adds its results before it takes its inputs, so a full result
//  slot ends the conversion with nothing lost. Owner and player are marked
//  for saving (34DBE0), as both native operations do.
//
//  Counts are read as the DWORD at ItemData+0x9C, the width D2RLoader writes.
//  The game itself still treats that count as one byte in two places, which
//  breaks stacks of exactly 256, 512 and 768. The plugin rewrites both in
//  place at load, each checked byte for byte first:
//    46DCF9  take-one (46DC90, the host's withdraw and the client's debit-one
//            message): read, test, decrement and store the whole DWORD instead
//            of its low byte. Same refusal target 46DD1A.
//    46D9A0  count reader (15 callers, all read one byte): the DWORD, or 255
//            when it is higher. Fits the function's own 32 bytes, 12 of them
//            int3 padding before 46D9C0.
//  Both rewrites were assembled at their real addresses and decoded back.
//
//  CONTROLLER: the stack-tab slot's vtable (1CF4678) routes the select button
//  (A / Cross) through +100h (2CF760, tap) and +118h (2AA7E0, hold), and its
//  prompt through +110h (2A8380, "Pick Up" + " (Hold to ...)"). On a stack
//  slot the native hold is a quick move (belt for rejuvenation potions,
//  inventory otherwise, the same as X). Both 2AA7E0 and 2A8380 are hooked,
//  acting only for that vtable and an item that is in a chain: hold A
//  upgrades the whole stack, and the prompt says what it makes. Tap A, X and
//  Y keep their native actions. Controller support is optional: if these
//  hooks cannot be installed the mouse part still runs.
//
//  INVENTORY RESULTS: a chain with output = "inventory" makes its result in
//  the player's inventory instead of a stack-tab slot, for items that have no
//  slot there. The host creates each one through D2RLoader's ItemService
//  (createItem, automatic placement), using the player handle getPeerPlayer
//  gave for the request, and destroys it again when the stack cannot pay. A
//  full inventory stops the conversion like a full stack-tab slot does.
//
//  Text: every line is a string Key. The mod's own string files win over the
//  plugin's built-in strings.json, so the mod rewords a line by adding an
//  entry with the same Key.
//
//  Console command "materialupgrade". Settings and recipes:
//  d2rloader/config/celestialrayone.material-upgrade.toml
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
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "resource_ids.h"

namespace {

constexpr char PluginIdText[]  = "celestialrayone.material-upgrade";
constexpr char PluginVersion[] = "1.3.2";
constexpr char SourceTag[]     = "material-upgrade";
constexpr char StringsPath[]   = "data/local/lng/strings/d2rloader/celestialrayone.material-upgrade/strings.json";

const D2RL::PluginContext* Context{};
std::uintptr_t             Base{};

// ---------------------------------------------------------------------------
// Addresses (RVAs into the D2RLoader 1.3.1 process image)
// ---------------------------------------------------------------------------

constexpr std::uint64_t SlotLeftClickRva    = 0x2CF760;
constexpr std::uint64_t SlotRightClickRva   = 0x2CE2C0;
constexpr std::uint64_t SlotHintLinesRva    = 0x2CEBC0;
constexpr std::uint64_t CursorItemRva       = 0x15F100;
constexpr std::uint64_t StringAppendRva     = 0x1732D0;
constexpr std::uint64_t StashViewRva        = 0x23B980;
constexpr std::uint64_t StashOwnerARva      = 0x46D9C0;
constexpr std::uint64_t StashOwnerBRva      = 0x46DA50;
constexpr std::uint64_t LocalPlayerIndexRva = 0x08B2D0;
constexpr std::uint64_t PlayerFromIndexRva  = 0x09A480;
constexpr std::uint64_t PlaySoundRva        = 0x1A0780;
constexpr std::uint64_t UnitIdRva           = 0x34A330;
constexpr std::uint64_t UnitTypeRva         = 0x34B9D0;
constexpr std::uint64_t ClassIdRva          = 0x349860;
constexpr std::uint64_t ItemBankRva         = 0x34A0E0;   // proven by the call at 46DCD9
constexpr std::uint64_t ItemBankCallRva     = 0x46DCD9;
constexpr std::uint64_t ItemRecordRva       = 0x314110;   // proven by the call at 46DCE3
constexpr std::uint64_t ItemRecordCallRva   = 0x46DCE3;
constexpr std::uint64_t RecordFromCodeRva   = 0x314070;
constexpr std::uint64_t ItemTypeRecordRva   = 0x36A4B0;   // (bank, type) -> ItemTypes record, null when out of range
constexpr std::uint64_t ServerUnitRva       = 0x48FE80;
constexpr std::uint64_t CheckStateRva       = 0x3351B0;
constexpr std::uint64_t UnitInventoryRva    = 0x34A360;
constexpr std::uint64_t InventoryOwnerRva   = 0x388BA0;
constexpr std::uint64_t InventoryNodeRva    = 0x388E10;
constexpr std::uint64_t NodeByte9Rva        = 0x38CD50;
constexpr std::uint64_t NodeByte8Rva        = 0x38CD60;
constexpr std::uint64_t StashSlotRva        = 0x46D8F0;
constexpr std::uint64_t StashDebitRva       = 0x46DC90;
constexpr std::uint64_t StashInsertRva      = 0x46DAE0;
constexpr std::uint64_t CreateItemRva       = 0x43D530;
constexpr std::uint64_t FreeItemRva         = 0x48FAA0;
constexpr std::uint64_t InitStateRva        = 0x382D00;
constexpr std::uint64_t CaptureStateRva     = 0x382D20;
constexpr std::uint64_t SendUpdateRva       = 0x535F60;
constexpr std::uint64_t AddFlagRva          = 0x34E410;
constexpr std::uint64_t UpdateFlagRva       = 0x34E460;
constexpr std::uint64_t ItemFlagRva         = 0x36D8F0;
constexpr std::uint64_t ItemPageRva         = 0x36DEA0;
constexpr std::uint64_t MarkDirtyRva        = 0x34DBE0;
constexpr std::uint64_t CreditCallRva       = 0x4F9CD3;   // E8 rel32 inside the native deposit
constexpr std::uint64_t RegistryResetRva    = 0x485290;
constexpr std::uint64_t ClientPlayerRva     = 0x484CDB;
constexpr std::uint64_t HoldActionRva       = 0x2AA7E0;   // controller: hold select
constexpr std::uint64_t SelectPromptRva     = 0x2A8380;   // controller: select prompt
constexpr std::uint64_t StashSlotVtableRva  = 0x1CF4678;  // AdvancedStashSlotWidget vtable
constexpr std::uint64_t TakeOneWidthRva     = 0x46DCF9;   // count fix
constexpr std::uint64_t CountReaderRva      = 0x46D9A0;   // count fix
constexpr std::uint64_t StackTabViewRva     = 0x23B820;   // the stash shows the stack tab
constexpr std::uint64_t CubeRouteRva        = 0x23BAC0;   // Ctrl + Shift + Left Click goes to the cube
constexpr std::uint64_t BeltAcceptsRva      = 0x15A110;   // (item) beltable, and the belt has room
constexpr std::uint64_t InventoryAcceptsRva = 0x15A2F0;   // (item) Ctrl + Left Click can go to the inventory
constexpr std::uint64_t WithdrawRequestRva  = 0x159B30;   // (item, owner, destination): packet 0x63 operation 1
constexpr std::uint64_t ItemSoundRva        = 0x1C3420;   // (item) -> the sound the game plays for it
constexpr std::uint64_t CtrlBranchRva       = 0x2CF7EA;   // 2CF760's Ctrl branch
constexpr std::uint64_t BeltTestRva         = 0x2CF865;   // 2CF760's Shift belt test
constexpr std::uint64_t BeltRequestRva      = 0x2CF8E2;   // 2CF760's belt request
constexpr std::uint64_t BatchWithdrawRva    = 0x1599A0;   // (item, owner, destination, count): packet 0x63 operation 2
constexpr std::uint64_t FreeBeltSlotRva     = 0x3862D0;   // (inventory, item, &cell, 1): INVENTORY_GetFreeBeltSlot
constexpr std::uint64_t InventoryGridRva    = 0x38B070;   // (inventory, grid, grid info) -> the grid
constexpr std::uint64_t BeltTypeRva         = 0x349720;   // (belt item) -> its belts.txt row
constexpr std::uint64_t BeltRecordRva       = 0x3C8400;   // (bank, row, 0, out) copies the belts.txt record
constexpr std::uint64_t SameBeltItemRva     = 0x36A8D0;   // (item, item): same item type, the belt column test
constexpr std::uint64_t RightClickBodyRva   = 0x2CE2D6;   // 2CE2C0 after the hook bytes
constexpr std::uint64_t BeltSearchRva       = 0x38643D;   // 3862D0's belt size and search
constexpr std::uint64_t GridLayoutRva       = 0x38B101;   // 38B070's new grid: width, height, cells
constexpr std::uint64_t BeltTypeReadRva     = 0x349795;   // 349720's items.txt read
constexpr std::uint64_t HostBeltSearchRva   = 0x4FBB60;   // 4FB350's belt search before each unit

// Data, proven by the instructions inside RegistryResetBytes and ClientPlayerBytes.
constexpr std::uint64_t RegistryRva          = 0x2AAECA0;
constexpr std::uint64_t RegistryReadyRva     = 0x2AAF4A0;
constexpr std::size_t   RegistryBuckets      = 256;
constexpr std::size_t   ClientNextOffset     = 0x630;
constexpr std::size_t   ClientUnitTypeOffset = 0x26C;
constexpr std::size_t   ClientUnitIdOffset   = 0x270;
constexpr std::size_t   ClientGameOffset     = 0x2B0;

// Data: the grid infos 3862D0 passes to 38B070 (its lea at 38643D and 3864A7).
constexpr std::uint64_t BodyGridInfoRva = 0x237B620;
constexpr std::uint64_t BeltGridInfoRva = 0x237B638;

constexpr std::size_t SlotItemOffset   = 0x608;   // stack-tab slot widget -> its stack item
constexpr std::size_t ItemDataOffset   = 0x10;    // unit -> ItemData
constexpr std::size_t StackCountOffset = 0x9C;    // ItemData -> stack-tab count (DWORD)
constexpr std::size_t RecordCodeOffset = 0x80;    // ItemsTxt record -> packed item code

// The belt, as 3862D0 reads it.
constexpr std::size_t   InventoryOwnerOffset = 0x08;    // inventory -> its owner unit
constexpr std::size_t   GridWidthOffset      = 0x10;    // inventory grid -> width (byte)
constexpr std::size_t   GridHeightOffset     = 0x11;    // inventory grid -> height (byte)
constexpr std::size_t   GridCellsOffset      = 0x18;    // inventory grid -> its cells (item pointers)
constexpr std::size_t   BeltRecordSize       = 0x120;   // 3C8400 copies this many bytes
constexpr std::size_t   BeltBoxesOffset      = 0x04;    // belts.txt record -> box count (byte)
constexpr std::uint64_t BodyGrid             = 0;
constexpr std::uint64_t BeltGrid             = 1;
constexpr std::size_t   BeltBodyCell         = 8;       // the body grid cell of the belt
constexpr std::int32_t  NoBeltRow            = 2;       // the belts.txt row without a belt
constexpr std::uint32_t BeltColumns          = 4;
constexpr std::size_t   MaxBeltCells         = 64;      // room in the copy; a bigger belt grid counts as a mismatch

constexpr std::uint32_t UnitTypePlayer = 0;
constexpr std::uint32_t UnitTypeItem   = 4;
constexpr std::int32_t  StashState     = 209;
constexpr std::int32_t  RefusalSound   = 3;
constexpr std::uint8_t  BeltDestination = 3;   // 159B30: 0 cursor, 1 inventory, 2 cube, 3 belt

constexpr std::uint8_t  InventoryDestination = 1;   // the same values for 1599A0
constexpr std::uint32_t BatchMoveLimit       = 3;   // 2CE2E9: the game's Ctrl + Right Click moves at most 3

// ---------------------------------------------------------------------------
// Expected bytes
// ---------------------------------------------------------------------------

// Hook entries: whole instructions, none RIP-relative.
// 2CF760: mov [rsp+18h],rbx / mov [rsp+10h],rdx / push rbp,rsi,rdi,r14,r15 /
// sub rsp,30h / mov rbx,rcx
constexpr std::uint8_t SlotLeftClickBytes[]{
    0x48,0x89,0x5C,0x24,0x18,0x48,0x89,0x54,0x24,0x10,0x55,0x56,0x57,0x41,0x56,0x41,
    0x57,0x48,0x83,0xEC,0x30,0x48,0x8B,0xD9 };
// 2CE2C0: mov [rsp+18h],rbx / push rbp,rsi,rdi / sub rsp,30h /
// mov rsi,[rcx+608h] / mov rbx,rcx
constexpr std::uint8_t SlotRightClickBytes[]{
    0x48,0x89,0x5C,0x24,0x18,0x55,0x56,0x57,0x48,0x83,0xEC,0x30,0x48,0x8B,0xB1,0x08,
    0x06,0x00,0x00,0x48,0x8B,0xD9 };
// 2CEBC0: mov [rsp+8],rbx / push rbp,rsi,rdi,r12,r13,r14,r15
constexpr std::uint8_t SlotHintLinesBytes[]{
    0x48,0x89,0x5C,0x24,0x08,0x55,0x56,0x57,0x41,0x54,0x41,0x55,0x41,0x56,0x41,0x57 };

// 2AA7E0: mov [rsp+20h],rbx / push rdi / sub rsp,50h (the next instruction is
// RIP-relative, so only these 10 bytes are taken)
constexpr std::uint8_t HoldActionBytes[]{ 0x48,0x89,0x5C,0x24,0x20,0x57,0x48,0x83,0xEC,0x50 };
// 2A8380: mov [rsp+18h],rbx / mov [rsp+20h],rsi / push rbp,rdi,r12,r14,r15
constexpr std::uint8_t SelectPromptBytes[]{
    0x48,0x89,0x5C,0x24,0x18,0x48,0x89,0x74,0x24,0x20,0x55,0x57,0x41,0x54,0x41,0x56,0x41,0x57 };

// 15F100: sub rsp,28h / call 8B2D0 / mov ecx,eax / call 9A480 / test rax,rax / je
constexpr std::uint8_t CursorItemBytes[]{
    0x48,0x83,0xEC,0x28,0xE8,0xC7,0xC1,0xF2,0xFF,0x8B,0xC8,0xE8,0x70,0xB3,0xF3,0xFF,
    0x48,0x85,0xC0,0x74,0x21 };
// 1732D0 (string, text, length): size [rcx+8] + length, source offset rdx - [rcx]
constexpr std::uint8_t StringAppendBytes[]{
    0x48,0x89,0x5C,0x24,0x08,0x48,0x89,0x6C,0x24,0x10,0x48,0x89,0x74,0x24,0x18,0x48,
    0x89,0x7C,0x24,0x20,0x41,0x56,0x48,0x83,0xEC,0x20,0x48,0x8B,0x79,0x08,0x48,0x8B,
    0xF2,0x48,0x2B,0x31 };
// 23B980: mov [rsp+8],rbx / push rdi / sub rsp,20h / mov ecx,18h (the stash panel)
constexpr std::uint8_t StashViewBytes[]{
    0x48,0x89,0x5C,0x24,0x08,0x57,0x48,0x83,0xEC,0x20,0xB9,0x18,0x00,0x00,0x00 };
// 46D9C0 and 46DA50 (player): null -> assert path
constexpr std::uint8_t StashOwnerABytes[]{
    0x40,0x53,0x48,0x83,0xEC,0x20,0x48,0x85,0xC9,0x75,0x1B,0x88,0x4C,0x24,0x30,0x48,
    0x8D,0x4C,0x24,0x30,0xE8,0xB7,0xF7,0xFF,0xFF };
constexpr std::uint8_t StashOwnerBBytes[]{
    0x40,0x53,0x48,0x83,0xEC,0x20,0x48,0x85,0xC9,0x75,0x1B,0x88,0x4C,0x24,0x30,0x48,
    0x8D,0x4C,0x24,0x30,0xE8,0xC7,0xF3,0xFF,0xFF };
// 8B2D0: mov eax, [local player index]; ret
constexpr std::uint8_t LocalPlayerIndexBytes[]{ 0x8B,0x05,0x2E,0x84,0x99,0x02,0xC3 };
// 9A480 (index): an index of 8 or more gives no player
constexpr std::uint8_t PlayerFromIndexBytes[]{
    0x48,0x89,0x5C,0x24,0x08,0x57,0x48,0x83,0xEC,0x20,0x83,0xF9,0x08 };
// 1A0780 (sound, unit, delay, ...), the same entry Transfer to Stash checks
constexpr std::uint8_t PlaySoundBytes[]{
    0x40,0x55,0x56,0x57,0x41,0x56,0x41,0x57,0x48,0x8D,0x6C,0x24,0xD1,0x48,0x81,0xEC,
    0x00,0x01,0x00,0x00,0x48,0x8B,0x05,0x2D,0xAB,0x82,0x02,0x48,0x33,0xC4,0x48,0x89 };
// 34A330, the whole function: null -> -1, else [rcx+8]
constexpr std::uint8_t UnitIdBytes[]{
    0x48,0x83,0xEC,0x28,0x48,0x85,0xC9,0x75,0x1D,0x88,0x4C,0x24,0x30,0x48,0x8D,0x4C,
    0x24,0x30,0xE8,0x39,0xCA,0xFF,0xFF,0x84,0xC0,0x74,0x01,0xCC,0xB8,0xFF,0xFF,0xFF,
    0xFF,0x48,0x83,0xC4,0x28,0xC3,0x8B,0x41,0x08,0x48,0x83,0xC4,0x28,0xC3 };
// 34B9D0, the whole function: null -> 6, else [rcx]
constexpr std::uint8_t UnitTypeBytes[]{
    0x48,0x83,0xEC,0x28,0x48,0x85,0xC9,0x75,0x1D,0x88,0x4C,0x24,0x30,0x48,0x8D,0x4C,
    0x24,0x30,0xE8,0x39,0x9E,0xFF,0xFF,0x84,0xC0,0x74,0x01,0xCC,0xB8,0x06,0x00,0x00,
    0x00,0x48,0x83,0xC4,0x28,0xC3,0x8B,0x01,0x48,0x83,0xC4,0x28,0xC3 };
// 349860 (unit, file, line): null -> -1, else the class id
constexpr std::uint8_t ClassIdBytes[]{
    0x48,0x83,0xEC,0x28,0x48,0x85,0xC9,0x75,0x1D,0x88,0x4C,0x24,0x30,0x48,0x8D,0x4C,
    0x24,0x30,0xE8,0x49,0xCB,0xFF,0xFF,0x84,0xC0,0x74,0x01,0xCC,0xB8,0xFF,0xFF,0xFF,
    0xFF };
// 46DCD9: call 34A0E0 (the item's table bank) inside the native take-one
constexpr std::uint8_t ItemBankCallBytes[]{ 0xE8,0x02,0xC4,0xED,0xFF };
// 46DCE3: call 314110 (bank, class -> ItemsTxt record) inside the native take-one
constexpr std::uint8_t ItemRecordCallBytes[]{ 0xE8,0x28,0x64,0xEA,0xFF };
// 36A4B0 (bank, type) -> ItemTypes record: mov [rsp+8],rbx / mov [rsp+20h],rsi /
// push rdi / sub rsp,30h / movsxd rsi,edx / call 300A90
constexpr std::uint8_t ItemTypeRecordBytes[]{
    0x48,0x89,0x5C,0x24,0x08,0x48,0x89,0x74,0x24,0x20,0x57,0x48,0x83,0xEC,0x30,0x48,
    0x63,0xF2,0xE8,0xC9,0x65,0xF9,0xFF };
// 314070 (bank, code, &class) -> ItemsTxt record
constexpr std::uint8_t RecordFromCodeBytes[]{
    0x48,0x89,0x5C,0x24,0x08,0x48,0x89,0x74,0x24,0x10,0x57,0x48,0x83,0xEC,0x30,0x49,
    0x8B,0xF8,0x8B,0xDA };
// 48FE80 (game, type, id)
constexpr std::uint8_t ServerUnitBytes[]{
    0x48,0x89,0x5C,0x24,0x08,0x48,0x89,0x74,0x24,0x18,0x57,0x48,0x83,0xEC,0x20,0x41,
    0x8B,0xD8,0x8B,0xF2,0x48,0x8B,0xF9,0x48,0x85,0xC9 };
// 3351B0 (unit, state)
constexpr std::uint8_t CheckStateBytes[]{
    0x48,0x89,0x5C,0x24,0x08,0x48,0x89,0x74,0x24,0x10,0x57,0x48,0x83,0xEC,0x20,0x8B,
    0xDA,0x48,0x8B,0xF1 };
// 34A360 (unit, file, line) -> inventory
constexpr std::uint8_t UnitInventoryBytes[]{
    0x48,0x89,0x5C,0x24,0x18,0x56,0x48,0x83,0xEC,0x20,0x48,0x8B,0xF1,0x48,0x85,0xC9,
    0x75,0x13 };
// 388BA0 (inventory) -> owner unit id
constexpr std::uint8_t InventoryOwnerBytes[]{
    0x40,0x53,0x48,0x83,0xEC,0x20,0x48,0x8B,0xD9,0x48,0x85,0xC9,0x75,0x1E };
// 388E10 (inventory, id), the whole function: the node list at +68h, id at +0,
// next node at +10h
constexpr std::uint8_t InventoryNodeBytes[]{
    0x48,0x8B,0x41,0x68,0x48,0x85,0xC0,0x74,0x14,0x0F,0x1F,0x80,0x00,0x00,0x00,0x00,
    0x39,0x10,0x74,0x09,0x48,0x8B,0x40,0x10,0x48,0x85,0xC0,0x75,0xF3,0xC3 };
// 38CD50: movzx eax, byte [rcx+9]; ret      38CD60: movzx eax, byte [rcx+8]; ret
constexpr std::uint8_t NodeByte9Bytes[]{ 0x0F,0xB6,0x41,0x09,0xC3 };
constexpr std::uint8_t NodeByte8Bytes[]{ 0x0F,0xB6,0x41,0x08,0xC3 };
// 46D8F0 (owner, class) -> the owner's slot item for that class
constexpr std::uint8_t StashSlotBytes[]{
    0x48,0x89,0x5C,0x24,0x08,0x57,0x48,0x83,0xEC,0x20,0x8B,0xFA,0x48,0x8B,0xD9,0xBA,
    0xD1,0x00,0x00,0x00,0xE8,0xA7,0x78,0xEC,0xFF };
// 46DC90 (owner, class) -> slot after taking one. Only the entry is checked:
// the static patch rewrites 46DCF9.
constexpr std::uint8_t StashDebitBytes[]{
    0x48,0x89,0x5C,0x24,0x08,0x57,0x48,0x83,0xEC,0x20,0x8B,0xFA,0x48,0x8B,0xD9,0xBA,
    0xD1,0x00,0x00,0x00,0xE8,0x07,0x75,0xEC,0xFF,0x85,0xC0,0x74,0x6D };
// 46DAE0 (owner, item) -> files the item as the owner's slot for its class
constexpr std::uint8_t StashInsertBytes[]{
    0x48,0x89,0x5C,0x24,0x08,0x48,0x89,0x6C,0x24,0x10,0x48,0x89,0x74,0x24,0x20,0x57,
    0x48,0x83,0xEC,0x40,0x48,0x8B,0xFA,0x48,0x8B,0xE9 };
// 43D530 (player, class, game, 4, 2, 1, 1, 1, 0, 0, 0) -> new item unit
constexpr std::uint8_t CreateItemBytes[]{
    0x40,0x55,0x53,0x56,0x57,0x48,0x8D,0x6C,0x24,0xF8,0x48,0x81,0xEC,0x08,0x01,0x00,
    0x00,0x48,0x8B,0x05,0x80,0xDD,0x58,0x02 };
// 48FAA0 (game, item) frees an item that was never placed
constexpr std::uint8_t FreeItemBytes[]{
    0x48,0x89,0x74,0x24,0x18,0x57,0x48,0x83,0xEC,0x20,0x48,0x8B,0xFA,0x48,0x8B,0xF1,
    0x48,0x85,0xC9,0x75,0x13 };
// 382D00 (state, word), the whole function: mode 2, page FFh, x = word, y 0, node 0
constexpr std::uint8_t InitStateBytes[]{
    0x33,0xC0,0xC7,0x01,0x02,0x00,0x00,0x00,0x66,0x89,0x41,0x0A,0x89,0x41,0x0C,0x48,
    0x8B,0xC1,0xC7,0x41,0x04,0xFF,0x00,0x00,0x00,0x66,0x89,0x51,0x08,0xC3 };
// 382D20 (state, item)
constexpr std::uint8_t CaptureStateBytes[]{
    0x48,0x89,0x5C,0x24,0x10,0x48,0x89,0x74,0x24,0x18,0x57,0x48,0x83,0xEC,0x20,0x48,
    0x8B,0xF1,0x48,0x8B,0xDA,0x48,0x8B,0xCA };
// 535F60 (game, owner, item, action, flags, mode, page, node, xy)
constexpr std::uint8_t SendUpdateBytes[]{
    0x48,0x89,0x5C,0x24,0x20,0x55,0x56,0x57,0x41,0x55,0x41,0x57,0x48,0x83,0xEC,0x60,
    0x4C,0x8B,0xE9,0x41,0x8B,0xF9,0x49,0x8B,0xC8,0x4D,0x8B,0xF8,0x48,0x8B,0xDA };
// 34E410 (unit, on): bit 4 of [unit+128h]
constexpr std::uint8_t AddFlagBytes[]{
    0x48,0x83,0xEC,0x28,0x48,0x85,0xC9,0x75,0x18,0x88,0x4C,0x24,0x30,0x48,0x8D,0x4C,
    0x24,0x30,0xE8,0xB9,0x6A,0xFF,0xFF,0x84,0xC0,0x74,0x27,0xCC,0x48,0x83,0xC4,0x28,
    0xC3,0x8B,0x81,0x28,0x01,0x00,0x00,0x85,0xD2,0x74,0x0E,0x83,0xC8,0x04 };
// 34E460 (unit, on)
constexpr std::uint8_t UpdateFlagBytes[]{
    0x48,0x83,0xEC,0x28,0x48,0x85,0xC9,0x75,0x18,0x88,0x4C,0x24,0x30,0x48,0x8D,0x4C,
    0x24,0x30,0xE8,0x69,0x6A,0xFF,0xFF,0x84,0xC0,0x74,0x27,0xCC,0x48,0x83,0xC4,0x28 };
// 36D8F0 (item, flag, on)
constexpr std::uint8_t ItemFlagBytes[]{
    0x48,0x89,0x5C,0x24,0x10,0x48,0x89,0x74,0x24,0x18,0x57,0x48,0x83,0xEC,0x20,0x41,
    0x8B,0xF0,0x8B,0xDA,0x48,0x8B,0xF9,0x48,0x85,0xC9,0x74,0x0A };
// 36DEA0 (item, page byte)
constexpr std::uint8_t ItemPageBytes[]{
    0x48,0x89,0x5C,0x24,0x10,0x57,0x48,0x83,0xEC,0x20,0x0F,0xB6,0xFA,0x48,0x8B,0xD9,
    0x48,0x85,0xC9,0x74,0x0A };
// 34DBE0 (unit, on): marks the unit for saving
constexpr std::uint8_t MarkDirtyBytes[]{
    0x48,0x89,0x5C,0x24,0x08,0x57,0x48,0x83,0xEC,0x20,0x8B,0xFA,0x48,0x8B,0xD9,0x85,
    0xD2,0x74,0x05 };
// 4F9CD0..4F9CE4: mov rcx,rdi / call <credit> / mov r15,rax / test rax,rax /
// jne. The rel32 is rewritten by D2RLoader at run time, so it is read live and
// only the bytes around it are checked.
constexpr std::uint8_t CreditCallHeadBytes[]{ 0x48,0x8B,0xCF,0xE8 };
constexpr std::uint8_t CreditCallTailBytes[]{
    0x4C,0x8B,0xF8,0x48,0x85,0xC0,0x0F,0x85,0xB7,0x01,0x00,0x00 };
// 485290, the whole function: lea rax, [2AAECA0]; 32 rounds of 8 qword stores;
// mov dword [2AAF4A0], 1; ret.
constexpr std::uint8_t RegistryResetBytes[]{
    0x33,0xD2,0x48,0x8D,0x05,0x07,0x9A,0x62,0x02,0x8D,0x4A,0x20,0x0F,0x1F,0x40,0x00,
    0x48,0x89,0x10,0x48,0x89,0x50,0x08,0x48,0x89,0x50,0x10,0x48,0x8D,0x40,0x40,0x48,
    0x89,0x50,0xD8,0x48,0x89,0x50,0xE0,0x48,0x89,0x50,0xE8,0x48,0x89,0x50,0xF0,0x48,
    0x89,0x50,0xF8,0x48,0x83,0xE9,0x01,0x75,0xD7,0xC7,0x05,0xCD,0xA1,0x62,0x02,0x01,
    0x00,0x00,0x00,0xC3 };
// 484CDB: mov r8d,[rbx+270h] / mov edx,[rbx+26Ch] / mov rcx,[rbx+2B0h] / call 48FE80
constexpr std::uint8_t ClientPlayerBytes[]{
    0x44,0x8B,0x83,0x70,0x02,0x00,0x00,0x8B,0x93,0x6C,0x02,0x00,0x00,0x48,0x8B,0x8B,
    0xB0,0x02,0x00,0x00,0xE8,0x8C,0xB1,0x00,0x00 };

// 2CF7EA..2CF854, the native left click's Ctrl branch: GetKeyState wrapper
// 120A100 with 11h (Ctrl), then 10h (Shift); Shift and 23BAC0 -> vtable +0xD8
// mode 3 (cube); else 15A2F0(item) -> vtable +0xD8 mode 0 (inventory).
constexpr std::uint8_t CtrlBranchBytes[]{
    0x8D,0x48,0x11,0xE8,0x0E,0xA9,0xF3,0x00,0x8D,0x4D,0x10,0x85,0xC0,0x74,0x6C,0xE8,
    0x02,0xA9,0xF3,0x00,0x85,0xC0,0x74,0x2E,0xE8,0xB9,0xC2,0xF6,0xFF,0x84,0xC0,0x74,
    0x25,0x48,0x8B,0x03,0x48,0x8D,0x54,0x24,0x68,0x41,0xB0,0x03,0x48,0x8B,0xCB,0xFF,
    0x90,0xD8,0x00,0x00,0x00,0x48,0x8B,0x5C,0x24,0x70,0x48,0x83,0xC4,0x30,0x41,0x5F,
    0x41,0x5E,0x5F,0x5E,0x5D,0xC3,0x49,0x8B,0xCE,0xE8,0xB8,0xAA,0xE8,0xFF,0x84,0xC0,
    0x0F,0x84,0xBF,0x00,0x00,0x00,0x48,0x8B,0x03,0x48,0x8D,0x54,0x24,0x68,0x45,0x33,
    0xC0,0x48,0x8B,0xCB,0xFF,0x90,0xD8,0x00,0x00,0x00 };
// 2CF865..2CF88B, its Shift belt test: 120A100(10h), then 23B820(), then
// 15A110(item); a no from either plays sound 3.
constexpr std::uint8_t BeltTestBytes[]{
    0xE8,0x96,0xA8,0xF3,0x00,0x85,0xC0,0x0F,0x84,0x99,0x00,0x00,0x00,0xE8,0xA9,0xBF,
    0xF6,0xFF,0x84,0xC0,0x0F,0x84,0x80,0x00,0x00,0x00,0x49,0x8B,0xCE,0xE8,0x89,0xA8,
    0xE8,0xFF,0x84,0xC0,0x74,0x74 };
// 2CF8E2..2CF8F8, its belt request: mov r8b,3 / mov rdx,rsi (owner) /
// mov rcx,rdi (item) / call 159B30 / mov rcx,rdi / call 1C3420 (the sound,
// played through 1A0780 at 2CF975).
constexpr std::uint8_t BeltRequestBytes[]{
    0x41,0xB0,0x03,0x48,0x8B,0xD6,0x48,0x8B,0xCF,0xE8,0x40,0xA2,0xE8,0xFF,0x48,0x8B,
    0xCF,0xE8,0x28,0x3B,0xEF,0xFF };

// 3862D0 (inventory, item, &cell, any), INVENTORY_GetFreeBeltSlot: push rbx,
// rbp, rsi, rdi, r12, r14, r15 / sub rsp,170h / mov rax,[security cookie] /
// xor rax,rsp / mov [rsp+160h],rax / movzx r12d,r9b (any) / mov r15,r8
// (&cell) / mov rbp,rdx (item)
constexpr std::uint8_t FreeBeltSlotBytes[]{
    0x40,0x53,0x55,0x56,0x57,0x41,0x54,0x41,0x56,0x41,0x57,0x48,0x81,0xEC,0x70,0x01,
    0x00,0x00,0x48,0x8B,0x05,0xDF,0x4F,0x64,0x02,0x48,0x33,0xC4,0x48,0x89,0x84,0x24,
    0x60,0x01,0x00,0x00,0x45,0x0F,0xB6,0xE1,0x4D,0x8B,0xF8,0x48,0x8B,0xEA };

// 3862D0's belt size and search (38643D..3865A6): grid 0 of the inventory
// (38B070 with the body grid info at 237B620), the belt in its cell 8
// ([cells+40h]) -> 349720, row 2 without one / [inventory+8] -> 34A0E0 ->
// 3C8400(bank, row, 0, rsp+40h), the box count at rsp+44h / grid 1 (38B070
// with the belt grid info at 237B638) / for columns 0..3: bottom cell
// [cells+column*8] set, 36A8D0(item, bottom) and column < boxes: the first
// empty cell of column, column+4, ... below boxes / any == 0 only: items.txt
// +141h / else the first empty bottom cell 0..3
constexpr std::uint8_t BeltSearchBytes[]{
    0x4C,0x8D,0x05,0xDC,0x51,0xFF,0x01,0x33,0xD2,0x48,0x8B,0xCF,0xE8,0x22,0x4C,0x00,
    0x00,0x48,0x85,0xC0,0x75,0x05,0x8D,0x58,0x02,0xEB,0x19,0x48,0x8B,0x40,0x18,0x48,
    0x8B,0x48,0x40,0x48,0x85,0xC9,0x75,0x05,0x8D,0x59,0x02,0xEB,0x07,0xE8,0xB1,0x32,
    0xFC,0xFF,0x8B,0xD8,0x48,0x83,0x7F,0x08,0x00,0x75,0x14,0x48,0x8D,0x4C,0x24,0x30,
    0xC6,0x44,0x24,0x30,0x00,0xE8,0x09,0xE2,0xFF,0xFF,0x84,0xC0,0x74,0x01,0xCC,0x48,
    0x8B,0x4F,0x08,0xE8,0x4B,0x3C,0xFC,0xFF,0x0F,0xB6,0xC8,0x4C,0x8D,0x4C,0x24,0x40,
    0x45,0x33,0xC0,0x8B,0xD3,0xE8,0x59,0x1F,0x04,0x00,0x4C,0x8D,0x05,0x8A,0x51,0xFF,
    0x01,0xBA,0x01,0x00,0x00,0x00,0x48,0x8B,0xCF,0xE8,0xB5,0x4B,0x00,0x00,0x4C,0x8B,
    0xF0,0x48,0x85,0xC0,0x0F,0x84,0x52,0xFE,0xFF,0xFF,0x33,0xF6,0x8B,0xDE,0x8B,0xFE,
    0x0F,0x1F,0x00,0x49,0x8B,0x4E,0x18,0x48,0x8B,0x14,0xF9,0x48,0x85,0xD2,0x74,0x40,
    0x48,0x8B,0xCD,0xE8,0xEB,0x43,0xFE,0xFF,0x85,0xC0,0x74,0x34,0x0F,0xB6,0x44,0x24,
    0x44,0x44,0x8B,0xC3,0x48,0x8B,0xD7,0x3B,0xD8,0x73,0x25,0x44,0x8B,0xC8,0x49,0x8B,
    0x46,0x18,0x48,0x8D,0x0C,0xF8,0x48,0x39,0x31,0x0F,0x84,0x80,0x00,0x00,0x00,0x41,
    0x83,0xC0,0x04,0x48,0x83,0xC2,0x04,0x48,0x83,0xC1,0x20,0x49,0x3B,0xD1,0x7C,0xE6,
    0xFF,0xC3,0x48,0xFF,0xC7,0x83,0xFB,0x04,0x7C,0xA9,0x45,0x84,0xE4,0x75,0x3F,0x41,
    0xB8,0xBD,0x0B,0x00,0x00,0x48,0x8D,0x15,0x57,0xEB,0x97,0x01,0x48,0x8B,0xCD,0xE8,
    0x1F,0x33,0xFC,0xFF,0x48,0x8B,0xCD,0x8B,0xD8,0xE8,0x95,0x3B,0xFC,0xFF,0x8B,0xD3,
    0x0F,0xB6,0xC8,0xE8,0xBB,0xDB,0xF8,0xFF,0x48,0x85,0xC0,0x0F,0x84,0xBB,0xFD,0xFF,
    0xFF,0x40,0x38,0xB0,0x41,0x01,0x00,0x00,0x0F,0x84,0xAE,0xFD,0xFF,0xFF,0x49,0x8B,
    0x46,0x18,0x48,0x8B,0xCE,0x48,0x83,0x38,0x00,0x74,0x21,0xFF,0xC6,0x48,0xFF,0xC1,
    0x48,0x83,0xC0,0x08,0x48,0x83,0xF9,0x04,0x7C,0xEB,0xE9,0x8D,0xFD,0xFF,0xFF,0x45,
    0x89,0x07,0xB8,0x01,0x00,0x00,0x00,0xE9,0x82,0xFD,0xFF,0xFF,0x41,0x89,0x37,0xB8,
    0x01,0x00,0x00,0x00,0xE9,0x75,0xFD,0xFF,0xFF };

// 2CE2C0 after the hook bytes (2CE2D6..2CE3CF): n = 46D9A0(item), at most 3
// (mov ebp,3 / cmova) / a cursor item (15F100) or n == 0: return / no Ctrl
// (120A100(17)): sound 3 / Shift (120A100(16)) and 23BAC0: the owner (23B980
// ? 46D9C0 : 46DA50), 1599A0(item, owner, 2, n) at 2CE392, the item's sound /
// else 15A2F0(item): jmp 2CF590(widget, &tmp, 0, n), else sound 3
constexpr std::uint8_t RightClickBodyBytes[]{
    0x48,0x8B,0xCE,0xE8,0xC2,0xF6,0x19,0x00,0xBD,0x03,0x00,0x00,0x00,0x0F,0xB6,0xF8,
    0x40,0x3A,0xC5,0x0F,0x47,0xFD,0xE8,0x0F,0x0E,0xE9,0xFF,0x48,0x85,0xC0,0x0F,0x85,
    0xE9,0x00,0x00,0x00,0x40,0x84,0xFF,0x0F,0x84,0xE0,0x00,0x00,0x00,0x8D,0x4D,0x0E,
    0xE8,0xF5,0xBD,0xF3,0x00,0x85,0xC0,0x0F,0x84,0xBC,0x00,0x00,0x00,0x8D,0x4D,0x0D,
    0xE8,0xE5,0xBD,0xF3,0x00,0x85,0xC0,0x0F,0x84,0x80,0x00,0x00,0x00,0xE8,0x98,0xD7,
    0xF6,0xFF,0x84,0xC0,0x74,0x77,0x48,0x8B,0xB3,0x08,0x06,0x00,0x00,0xE8,0x98,0xCF,
    0xDB,0xFF,0x8B,0xC8,0xE8,0x41,0xC1,0xDC,0xFF,0x48,0x8B,0xD8,0x48,0x85,0xC0,0x75,
    0x17,0x48,0x8D,0x4C,0x24,0x50,0x88,0x44,0x24,0x50,0xE8,0x4B,0xFD,0xFF,0xFF,0x84,
    0xC0,0x74,0x01,0xCC,0x33,0xDB,0xEB,0x1B,0xE8,0x1D,0xD6,0xF6,0xFF,0x48,0x8B,0xCB,
    0x84,0xC0,0x74,0x07,0xE8,0x51,0xF6,0x19,0x00,0xEB,0x05,0xE8,0xDA,0xF6,0x19,0x00,
    0x48,0x8B,0xD8,0x48,0x8B,0xCE,0xE8,0x1F,0xF6,0x19,0x00,0x84,0xC0,0x74,0x5E,0x44,
    0x0F,0xB6,0xCF,0x41,0xB0,0x02,0x48,0x8B,0xD3,0x48,0x8B,0xCE,0xE8,0x09,0xB6,0xE8,
    0xFF,0x48,0x8B,0xCE,0xE8,0x81,0x50,0xEF,0xFF,0x8B,0xC8,0xEB,0x2E,0x48,0x8B,0xCE,
    0xE8,0x45,0xBF,0xE8,0xFF,0x84,0xC0,0x74,0x20,0x44,0x0F,0xB6,0xCF,0x48,0x8D,0x54,
    0x24,0x58,0x45,0x33,0xC0,0x48,0x8B,0xCB,0x48,0x8B,0x5C,0x24,0x60,0x48,0x83,0xC4,
    0x30,0x5F,0x5E,0x5D,0xE9,0xC1,0x11,0x00,0x00 };

// 1599A0 (item, owner, destination, count), the whole function: 34A330 of
// both, then 0EC880(0x63, 2, item id, owner id, destination, count, 0)
constexpr std::uint8_t BatchWithdrawBytes[]{
    0x48,0x89,0x5C,0x24,0x08,0x48,0x89,0x6C,0x24,0x10,0x48,0x89,0x74,0x24,0x18,0x57,
    0x48,0x83,0xEC,0x40,0x48,0x8B,0xC2,0x41,0x0F,0xB6,0xE8,0x48,0x8B,0xD9,0x41,0x0F,
    0xB6,0xF1,0x48,0x8B,0xC8,0x48,0x8D,0x15,0x64,0x38,0xB7,0x01,0x41,0xB8,0xFD,0x06,
    0x00,0x00,0xE8,0x59,0x09,0x1F,0x00,0x41,0xB8,0xFD,0x06,0x00,0x00,0x48,0x8D,0x15,
    0x4C,0x38,0xB7,0x01,0x48,0x8B,0xCB,0x8B,0xF8,0xE8,0x42,0x09,0x1F,0x00,0xC7,0x44,
    0x24,0x30,0x00,0x00,0x00,0x00,0x44,0x8B,0xCF,0x89,0x74,0x24,0x28,0x44,0x8B,0xC0,
    0xBA,0x02,0x00,0x00,0x00,0x89,0x6C,0x24,0x20,0xB1,0x63,0xE8,0x70,0x2E,0xF9,0xFF,
    0x48,0x8B,0x5C,0x24,0x50,0x48,0x8B,0x6C,0x24,0x58,0x48,0x8B,0x74,0x24,0x60,0x48,
    0x83,0xC4,0x40,0x5F,0xC3 };

// 38B070 (inventory, grid, grid info): the index below [inventory+28h], the
// grids at [inventory+20h], 20h bytes each
constexpr std::uint8_t InventoryGridEntryBytes[]{
    0x4C,0x8B,0xDC,0x49,0x89,0x5B,0x20,0x57,0x48,0x83,0xEC,0x30,0x49,0x8B,0xF8,0x48,
    0x39,0x51,0x28,0x0F,0x86,0x01,0x01,0x00,0x00,0x49,0x89,0x73,0x18,0x48,0x8D,0x71,
    0x20,0x49,0x89,0x53,0x10,0x48,0x3B,0x56,0x08,0x72,0x1A,0x49,0x8D,0x43,0x10,0x49,
    0x89,0x73,0xF0,0x49,0x8D,0x4B,0xE8,0x49,0x89,0x43,0xE8,0xE8,0xA0,0x83,0xFF,0xFF,
    0x84,0xC0,0x74,0x01,0xCC,0x48,0x8B,0x5C,0x24,0x48,0x48,0xC1,0xE3,0x05,0x48,0x03,
    0x1E };

// 38B070 on a grid without cells (38B101..38B132): width +10h and height +11h
// from the grid info's first two bytes, cells +18h, width * height of them
constexpr std::uint8_t InventoryGridLayoutBytes[]{
    0x48,0x83,0x7B,0x18,0x00,0x75,0x2A,0x0F,0xB6,0x07,0x88,0x43,0x10,0x0F,0xB6,0x47,
    0x01,0x88,0x43,0x11,0xE8,0x96,0x5A,0x69,0x00,0x0F,0xB6,0x4B,0x10,0x0F,0xB6,0x53,
    0x11,0x48,0x0F,0xAF,0xD1,0x48,0x8B,0xC8,0xE8,0x72,0x7B,0xFF,0xFF,0x48,0x89,0x43,
    0x18 };

// 349720 (belt item) -> its belts.txt row: the entry, then (349795..3497DA)
// the items.txt record (314110 with the bank at +1BDh and the class at +4)
// and its byte +140h
constexpr std::uint8_t BeltTypeEntryBytes[]{
    0x48,0x89,0x5C,0x24,0x10,0x57,0x48,0x83,0xEC,0x20,0x48,0x8B,0xD9 };
constexpr std::uint8_t BeltTypeReadBytes[]{
    0x8B,0x7B,0x04,0x0F,0xB6,0x8B,0xBD,0x01,0x00,0x00,0x8B,0xD7,0xE8,0x6A,0xA9,0xFC,
    0xFF,0x48,0x8B,0xD8,0x48,0x85,0xC0,0x75,0x25,0x48,0x8D,0x4C,0x24,0x30,0x88,0x44,
    0x24,0x30,0xE8,0xB4,0xC4,0xFF,0xFF,0x84,0xC0,0x74,0x01,0xCC,0x0F,0xB6,0x83,0x40,
    0x01,0x00,0x00,0x48,0x8B,0x5C,0x24,0x38,0x48,0x83,0xC4,0x20,0x5F,0xC3,0x0F,0xB6,
    0x80,0x40,0x01,0x00,0x00 };

// 3C8400 (bank, row, layout, out), the whole function: record row + 7 *
// layout of the table at +1558h of 300A90(bank), 120h bytes each, copied to
// out with movups
constexpr std::uint8_t BeltRecordBytes[]{
    0x48,0x89,0x5C,0x24,0x08,0x48,0x89,0x74,0x24,0x10,0x48,0x89,0x7C,0x24,0x18,0x41,
    0x56,0x48,0x83,0xEC,0x30,0x49,0x8B,0xF1,0x41,0x8B,0xD8,0x8B,0xFA,0xE8,0x6E,0x86,
    0xF3,0xFF,0x4C,0x8D,0xB0,0x58,0x15,0x00,0x00,0x6B,0xC3,0x07,0x03,0xC7,0x48,0x63,
    0xC8,0x48,0x89,0x4C,0x24,0x58,0x49,0x3B,0x4E,0x08,0x72,0x1E,0x48,0x8D,0x44,0x24,
    0x58,0x4C,0x89,0x74,0x24,0x28,0x48,0x8D,0x4C,0x24,0x20,0x48,0x89,0x44,0x24,0x20,
    0xE8,0x9B,0xFB,0xFF,0xFF,0x84,0xC0,0x74,0x01,0xCC,0x48,0x8B,0x44,0x24,0x58,0x48,
    0x8D,0x0C,0xC0,0x49,0x8B,0x06,0x48,0xC1,0xE1,0x05,0x48,0x03,0xC1,0xB9,0x02,0x00,
    0x00,0x00,0x48,0x8D,0xB6,0x80,0x00,0x00,0x00,0x0F,0x10,0x00,0x48,0x8D,0x80,0x80,
    0x00,0x00,0x00,0x0F,0x11,0x46,0x80,0x0F,0x10,0x48,0x90,0x0F,0x11,0x4E,0x90,0x0F,
    0x10,0x40,0xA0,0x0F,0x11,0x46,0xA0,0x0F,0x10,0x48,0xB0,0x0F,0x11,0x4E,0xB0,0x0F,
    0x10,0x40,0xC0,0x0F,0x11,0x46,0xC0,0x0F,0x10,0x48,0xD0,0x0F,0x11,0x4E,0xD0,0x0F,
    0x10,0x40,0xE0,0x0F,0x11,0x46,0xE0,0x0F,0x10,0x48,0xF0,0x0F,0x11,0x4E,0xF0,0x48,
    0x83,0xE9,0x01,0x75,0xAD,0x0F,0x10,0x00,0x48,0x8B,0x5C,0x24,0x40,0x48,0x8B,0x7C,
    0x24,0x50,0x0F,0x11,0x06,0x0F,0x10,0x48,0x10,0x0F,0x11,0x4E,0x10,0x48,0x8B,0x74,
    0x24,0x48,0x48,0x83,0xC4,0x30,0x41,0x5E,0xC3 };

// 36A8D0 (item, other): both set, then their item types (372C90) equal; types
// 18 and 22 compare one more field
constexpr std::uint8_t SameBeltItemBytes[]{
    0x48,0x89,0x74,0x24,0x20,0x57,0x48,0x83,0xEC,0x20,0x48,0x8B,0xF2,0x48,0x8B,0xF9,
    0x48,0x85,0xC9,0x0F,0x84,0x06,0x01,0x00,0x00,0x48,0x85,0xD2,0x0F,0x84,0xFD,0x00,
    0x00,0x00,0x48,0x89,0x5C,0x24,0x38,0x48,0x89,0x6C,0x24,0x40,0xE8,0x8F,0x83,0x00,
    0x00,0x48,0x8B,0xCE,0x8B,0xE8,0xE8,0x85,0x83,0x00,0x00,0x8D,0x55,0xEE,0x33,0xDB,
    0xF7,0xC2,0xFB,0xFF,0xFF,0xFF,0x74,0x1C,0x3B,0xE8,0x0F,0x94,0xC3,0x48,0x8B,0x6C,
    0x24,0x40,0x8B,0xC3,0x48,0x8B,0x5C,0x24,0x38,0x48,0x8B,0x74,0x24,0x48,0x48,0x83,
    0xC4,0x20,0x5F,0xC3 };

// 4FB350's belt destination (4FBB60..4FBBA1), run before each unit: cell = 0,
// 3862D0(the player's inventory (34A360), the stash item, &cell, 1), then the
// cell's position (382D00)
constexpr std::uint8_t HostBeltSearchBytes[]{
    0x80,0xF9,0x03,0x0F,0x85,0x11,0x02,0x00,0x00,0x41,0xB8,0xC4,0x0C,0x00,0x00,0x44,
    0x89,0x75,0x9F,0x48,0x8D,0x15,0x46,0x7F,0x83,0x01,0x49,0x8B,0xCD,0xE8,0xDE,0xE7,
    0xE4,0xFF,0x48,0x8B,0xC8,0x4C,0x8D,0x45,0x9F,0x41,0xB1,0x01,0x48,0x8B,0xD3,0xE8,
    0x3C,0xA7,0xE8,0xFF,0x0F,0xB7,0x55,0x9F,0x48,0x8D,0x4D,0xC7,0xE8,0x5F,0x71,0xE8,
    0xFF };

// Count fix: the native bytes and what the plugin writes over them.
// 46DCF9: movzx ecx,byte [rax+9Ch] / test cl,cl / je 46DD1A / dec cl / mov [rax+9Ch],cl
//      -> mov ecx,[rax+9Ch] / test ecx,ecx / je 46DD1A / dec ecx / mov [rax+9Ch],ecx / nop
constexpr std::uint8_t TakeOneNativeBytes[]{
    0x0F,0xB6,0x88,0x9C,0x00,0x00,0x00,0x84,0xC9,0x74,0x16,0xFE,0xC9,0x88,0x88,0x9C,
    0x00,0x00,0x00 };
constexpr std::uint8_t TakeOneFixedBytes[]{
    0x8B,0x88,0x9C,0x00,0x00,0x00,0x85,0xC9,0x74,0x17,0xFF,0xC9,0x89,0x88,0x9C,0x00,
    0x00,0x00,0x90 };
// 46D9A0: test rcx,rcx / jne / xor al,al / ret / mov rax,[rcx+10h] / movzx eax,byte [rax+9Ch] / ret / 12 x int3
//      -> test rcx,rcx / je 46D9BA / mov rax,[rcx+10h] / mov eax,[rax+9Ch] / mov ecx,0FFh /
//         cmp eax,ecx / cmova eax,ecx / ret / (46D9BA) xor eax,eax / ret / 3 x int3
constexpr std::uint8_t CountReaderNativeBytes[]{
    0x48,0x85,0xC9,0x75,0x03,0x32,0xC0,0xC3,0x48,0x8B,0x41,0x10,0x0F,0xB6,0x80,0x9C,
    0x00,0x00,0x00,0xC3,0xCC,0xCC,0xCC,0xCC,0xCC,0xCC,0xCC,0xCC,0xCC,0xCC,0xCC,0xCC };
constexpr std::uint8_t CountReaderFixedBytes[]{
    0x48,0x85,0xC9,0x74,0x15,0x48,0x8B,0x41,0x10,0x8B,0x80,0x9C,0x00,0x00,0x00,0xB9,
    0xFF,0x00,0x00,0x00,0x39,0xC8,0x0F,0x47,0xC1,0xC3,0x31,0xC0,0xC3,0xCC,0xCC,0xCC };
static_assert(sizeof(TakeOneNativeBytes) == sizeof(TakeOneFixedBytes));
static_assert(sizeof(CountReaderNativeBytes) == sizeof(CountReaderFixedBytes));

struct FixSite {
    std::uint64_t       rva;
    const std::uint8_t* native;
    const std::uint8_t* fixed;
    std::uint32_t       size;
    const char*         name;
    const char*         status;
};

std::array<FixSite, 2> FixSites{ {
    { TakeOneWidthRva, TakeOneNativeBytes, TakeOneFixedBytes, sizeof(TakeOneNativeBytes), "take-one", "not checked" },
    { CountReaderRva, CountReaderNativeBytes, CountReaderFixedBytes, sizeof(CountReaderNativeBytes), "count reader",
        "not checked" },
} };

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

const std::array<Witness, 43> Witnesses{
    W(SlotLeftClickRva, SlotLeftClickBytes, "stack slot left click"),
    W(SlotRightClickRva, SlotRightClickBytes, "stack slot right click"),
    W(SlotHintLinesRva, SlotHintLinesBytes, "stack slot hint lines"),
    W(CursorItemRva, CursorItemBytes, "cursor item"),
    W(StringAppendRva, StringAppendBytes, "string append"),
    W(StashViewRva, StashViewBytes, "stash view"),
    W(StashOwnerARva, StashOwnerABytes, "stash owner A"),
    W(StashOwnerBRva, StashOwnerBBytes, "stash owner B"),
    W(LocalPlayerIndexRva, LocalPlayerIndexBytes, "local player index"),
    W(PlayerFromIndexRva, PlayerFromIndexBytes, "local player"),
    W(PlaySoundRva, PlaySoundBytes, "play sound"),
    W(UnitIdRva, UnitIdBytes, "unit id"),
    W(UnitTypeRva, UnitTypeBytes, "unit type"),
    W(ClassIdRva, ClassIdBytes, "class id"),
    W(ItemBankCallRva, ItemBankCallBytes, "item bank call"),
    W(ItemRecordCallRva, ItemRecordCallBytes, "item record call"),
    W(RecordFromCodeRva, RecordFromCodeBytes, "record from code"),
    W(ItemTypeRecordRva, ItemTypeRecordBytes, "item type record"),
    W(ServerUnitRva, ServerUnitBytes, "server unit lookup"),
    W(CheckStateRva, CheckStateBytes, "check state"),
    W(UnitInventoryRva, UnitInventoryBytes, "unit inventory"),
    W(InventoryOwnerRva, InventoryOwnerBytes, "inventory owner"),
    W(InventoryNodeRva, InventoryNodeBytes, "inventory node"),
    W(NodeByte9Rva, NodeByte9Bytes, "node byte 9"),
    W(NodeByte8Rva, NodeByte8Bytes, "node byte 8"),
    W(StashSlotRva, StashSlotBytes, "stash slot"),
    W(StashDebitRva, StashDebitBytes, "stash take one"),
    W(StashInsertRva, StashInsertBytes, "stash file slot"),
    W(CreateItemRva, CreateItemBytes, "create item"),
    W(FreeItemRva, FreeItemBytes, "free item"),
    W(InitStateRva, InitStateBytes, "packet state init"),
    W(CaptureStateRva, CaptureStateBytes, "packet state capture"),
    W(SendUpdateRva, SendUpdateBytes, "item update"),
    W(AddFlagRva, AddFlagBytes, "add flag"),
    W(UpdateFlagRva, UpdateFlagBytes, "update flag"),
    W(ItemFlagRva, ItemFlagBytes, "item flag"),
    W(ItemPageRva, ItemPageBytes, "item page"),
    W(MarkDirtyRva, MarkDirtyBytes, "mark for saving"),
    W(RegistryResetRva, RegistryResetBytes, "client registry reset"),
    W(ClientPlayerRva, ClientPlayerBytes, "client player fields"),
    W(CtrlBranchRva, CtrlBranchBytes, "left click Ctrl branch"),
    W(BeltTestRva, BeltTestBytes, "left click belt test"),
    W(BeltRequestRva, BeltRequestBytes, "left click belt request"),
};

// Checked on their own: only Ctrl + Right Click belt first relies on these.
const std::array<Witness, 11> BatchWitnesses{
    W(FreeBeltSlotRva, FreeBeltSlotBytes, "belt search entry"),
    W(BeltSearchRva, BeltSearchBytes, "belt search"),
    W(RightClickBodyRva, RightClickBodyBytes, "right click body"),
    W(BatchWithdrawRva, BatchWithdrawBytes, "batch withdraw request"),
    W(InventoryGridRva, InventoryGridEntryBytes, "inventory grid entry"),
    W(GridLayoutRva, InventoryGridLayoutBytes, "inventory grid layout"),
    W(BeltTypeRva, BeltTypeEntryBytes, "belt type entry"),
    W(BeltTypeReadRva, BeltTypeReadBytes, "belt type read"),
    W(BeltRecordRva, BeltRecordBytes, "belts.txt record"),
    W(SameBeltItemRva, SameBeltItemBytes, "belt column test"),
    W(HostBeltSearchRva, HostBeltSearchBytes, "host belt search"),
};

// ---------------------------------------------------------------------------
// Native functions
// ---------------------------------------------------------------------------

// The string object the hint-line builder fills: data, size, capacity.
struct NativeString {
    char*         data;
    std::uint64_t size;
    std::uint64_t capacity;
};

// 382D00 / 382D20: mode +0, page +4, x +8, y +0Ah, node +0Ch.
struct PacketState {
    std::int32_t  mode;
    std::int32_t  page;
    std::uint32_t xy;
    std::int32_t  node;
};
static_assert(sizeof(PacketState) == 16);

using SlotLeftClickFn  = std::int64_t(__fastcall*)(void* widget, std::int64_t event);
using SlotRightClickFn = std::int64_t(__fastcall*)(void* widget);
using SlotHintLinesFn  = NativeString*(__fastcall*)(void* self, NativeString* out, void* item);
using HoldActionFn     = std::int64_t(__fastcall*)(void* widget, void* item);
using SelectPromptFn   = NativeString*(__fastcall*)(void* widget, NativeString* out);
using CursorItemFn     = void*(__fastcall*)();
using StringAppendFn   = void*(__fastcall*)(NativeString* string, const char* text, std::uint64_t length);
using StashViewFn      = std::uint8_t(__fastcall*)();
using StashOwnerFn     = void*(__fastcall*)(void* player);
using PlayerIndexFn    = std::int32_t(__fastcall*)();
using PlayerFromFn     = void*(__fastcall*)(std::int32_t index);
using PlaySoundFn      = std::uint64_t(__fastcall*)(std::int32_t sound, void* unit, std::int32_t delay, std::int32_t, bool);
using UnitIdFn         = std::uint32_t(__fastcall*)(void* unit, const char* file, std::int32_t line);
using UnitTypeFn       = std::uint32_t(__fastcall*)(void* unit);
using ClassIdFn        = std::int32_t(__fastcall*)(void* unit, const char* file, std::int32_t line);
using ItemBankFn       = std::uint8_t(__fastcall*)(void* item);
using ItemRecordFn     = const std::uint8_t*(__fastcall*)(std::uint32_t bank, std::uint32_t classId);
using RecordFromCodeFn = const std::uint8_t*(__fastcall*)(std::uint32_t bank, std::uint32_t code, std::int32_t* classId);
using ItemTypeRecordFn = const std::uint8_t*(__fastcall*)(std::uint32_t bank, std::int32_t type);
using ServerUnitFn     = void*(__fastcall*)(void* game, std::uint32_t type, std::uint32_t id);
using CheckStateFn     = std::int32_t(__fastcall*)(void* unit, std::int32_t state);
using UnitInventoryFn  = void*(__fastcall*)(void* unit, const char* file, std::int32_t line);
using InventoryOwnerFn = std::uint32_t(__fastcall*)(void* inventory);
using InventoryNodeFn  = void*(__fastcall*)(void* inventory, std::uint32_t id);
using NodeByteFn       = std::uint8_t(__fastcall*)(void* node);
using StashSlotFn      = void*(__fastcall*)(void* owner, std::uint32_t classId);
using StashInsertFn    = std::uint8_t(__fastcall*)(void* owner, void* item);
using CreateItemFn     = void*(__fastcall*)(void* player, std::uint32_t classId, void* game, std::int64_t spawn,
    std::int64_t quality, std::int64_t a6, std::int64_t a7, std::int64_t a8, std::int64_t a9, std::int64_t a10,
    std::int64_t a11);
using FreeItemFn       = void(__fastcall*)(void* game, void* item);
using InitStateFn      = PacketState*(__fastcall*)(PacketState* state, std::uint16_t x);
using CaptureStateFn   = PacketState*(__fastcall*)(PacketState* state, void* item);
using SendUpdateFn     = void(__fastcall*)(void* game, void* owner, void* item, std::int32_t action, std::int32_t flags,
    std::int32_t mode, std::int32_t page, std::int32_t node, std::uint32_t xy);
using UnitFlagFn       = void(__fastcall*)(void* unit, std::int32_t on);
using ItemFlagFn       = void(__fastcall*)(void* item, std::uint32_t flag, std::int32_t on);
using ItemPageFn       = void(__fastcall*)(void* item, std::uint8_t page);
using ItemTestFn       = std::uint8_t(__fastcall*)(void* item);
using WithdrawFn       = void(__fastcall*)(void* item, void* owner, std::uint8_t destination);
using ItemSoundFn      = std::int32_t(__fastcall*)(void* item);
using BatchWithdrawFn  = void(__fastcall*)(void* item, void* owner, std::uint8_t destination, std::uint8_t count);
using FreeBeltSlotFn   = std::int32_t(__fastcall*)(void* inventory, void* item, std::int32_t* cell, std::uint8_t any);
using InventoryGridFn  = const std::uint8_t*(__fastcall*)(void* inventory, std::uint64_t grid, const void* info);
using BeltTypeFn       = std::int32_t(__fastcall*)(void* belt);
using BeltRecordFn     = void*(__fastcall*)(std::uint8_t bank, std::int32_t row, std::int32_t layout, void* out);
using SameBeltItemFn   = std::int32_t(__fastcall*)(void* item, void* other);

SlotLeftClickFn  OriginalLeftClick{};
SlotRightClickFn OriginalRightClick{};
SlotHintLinesFn  OriginalHintLines{};
HoldActionFn     OriginalHoldAction{};
SelectPromptFn   OriginalSelectPrompt{};
CursorItemFn     CursorItem{};
StringAppendFn   StringAppend{};
StashViewFn      StashView{};
StashOwnerFn     StashOwnerA{};
StashOwnerFn     StashOwnerB{};
PlayerIndexFn    LocalPlayerIndex{};
PlayerFromFn     PlayerFromIndex{};
PlaySoundFn      PlaySound{};
UnitIdFn         UnitIdOf{};
UnitTypeFn       UnitType{};
ClassIdFn        ClassIdOf{};
ItemBankFn       ItemBank{};
ItemRecordFn     ItemRecord{};
RecordFromCodeFn RecordFromCode{};
ItemTypeRecordFn ItemTypeRecord{};
ServerUnitFn     ServerUnit{};
CheckStateFn     CheckState{};
UnitInventoryFn  UnitInventory{};
InventoryOwnerFn InventoryOwner{};
InventoryNodeFn  InventoryNode{};
NodeByteFn       NodeByte9{};
NodeByteFn       NodeByte8{};
StashSlotFn      StashSlot{};
StashSlotFn      StashDebit{};
StashSlotFn      StashCredit{};
StashInsertFn    StashInsert{};
CreateItemFn     CreateItem{};
FreeItemFn       FreeItem{};
InitStateFn      InitState{};
CaptureStateFn   CaptureState{};
SendUpdateFn     SendUpdate{};
UnitFlagFn       AddFlag{};
UnitFlagFn       UpdateFlag{};
ItemFlagFn       ItemFlag{};
ItemPageFn       ItemPage{};
UnitFlagFn       MarkDirty{};
StashViewFn      StackTabView{};
StashViewFn      CubeRoute{};
ItemTestFn       BeltAccepts{};
ItemTestFn       InventoryAccepts{};
WithdrawFn       WithdrawRequest{};
ItemSoundFn      ItemSound{};
BatchWithdrawFn  BatchWithdraw{};
FreeBeltSlotFn   FreeBeltSlot{};
InventoryGridFn  InventoryGrid{};
BeltTypeFn       BeltType{};
BeltRecordFn     BeltRecord{};
SameBeltItemFn   SameBeltItem{};

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

// ---------------------------------------------------------------------------
// Services and state
// ---------------------------------------------------------------------------

const D2RL::NetworkService*      NetworkSvc{};
const D2RL::ThreadService*       ThreadSvc{};
const D2RL::ItemService*         ItemSvc{};   // inventory results only
const D2RL::LifecycleService*    LifecycleSvc{};
const D2RL::LocalizationService* LocalizationSvc{};
const D2RL::ResourceService*     ResourceSvc{};

enum class PluginState {
    NotLoaded, DisabledByConfig, UnsupportedBuild, ServicesMissing, InstallFailed, NoRecipes, Active
};
PluginState       State{ PluginState::NotLoaded };
std::atomic<bool> Active{};
std::string       VerifyFailures;
std::string       CreditTarget = "not resolved";
std::atomic<bool> ControllerActive{};
const char*       ControllerStatus = "not installed";
std::atomic<bool> BatchBeltActive{};               // Ctrl + Right Click belt first
std::string       BatchBeltStatus = "not checked";

D2RL::Network::ChannelHandle        Channel{ D2RL::Network::InvalidChannelHandle };
D2RL::Resources::RegistrationHandle StringsHandle{};

constexpr std::uint16_t ChannelLocalId     = 1;
constexpr std::uint64_t CompatibilityToken = 0x4D41545550473032ULL;  // "MATUPG02": the request carries flags
constexpr std::uint16_t UpgradeMessageId   = 1;

// One click: who clicked which stack of which stash owner, and how much.
struct UpgradeMessage {
    std::uint32_t playerId;
    std::uint32_t ownerId;
    std::uint32_t stackId;
    std::uint32_t code;
    std::uint32_t flags;   // FlagAll, FlagDowngrade
};
static_assert(sizeof(UpgradeMessage) == 20);

constexpr std::uint32_t FlagAll       = 1;   // the whole stack instead of one
constexpr std::uint32_t FlagDowngrade = 2;   // a [[downgrade]] step instead of a [[chain]] step

std::atomic<std::uint64_t> ClicksSent{};
std::atomic<std::uint64_t> ClicksRefused{};
std::atomic<std::uint64_t> ClicksDropped{};
std::atomic<std::uint64_t> RequestsReceived{};
std::atomic<std::uint64_t> RequestsRefused{};
std::atomic<std::uint64_t> UpgradesDone{};
std::atomic<std::uint64_t> DowngradesDone{};
std::atomic<std::uint64_t> UpgradesStoppedFull{};
std::atomic<std::uint64_t> BeltMoves{};    // Ctrl + Left Clicks sent to the belt
std::atomic<std::uint64_t> BatchMoves{};        // Ctrl + Right Clicks sent belt first
std::atomic<std::uint64_t> BatchToBelt{};       // their units sent to the belt
std::atomic<std::uint64_t> BatchToInventory{};  // their units sent to the inventory
std::atomic<std::uint64_t> BeltMismatches{};    // belt copies whose first cell was not 3862D0's
std::atomic<std::uint64_t> HintMisses{};   // Ctrl lines that could not be matched
std::atomic<std::uint32_t> ConnectionNow{ static_cast<std::uint32_t>(D2RL::Network::ConnectionState::Disconnected) };

std::mutex  LastMutex;
std::string LastResult = "none yet";

void SetLast(const char* text) {
    std::lock_guard lock(LastMutex);
    LastResult = text;
}

// ---------------------------------------------------------------------------
// Embedded files
// ---------------------------------------------------------------------------

auto Embedded(int id, std::string_view& text) -> bool {
    HMODULE module{};
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCWSTR>(&Embedded), &module)) {
        return false;
    }
    HRSRC   found  = FindResourceW(module, MAKEINTRESOURCEW(id), MAKEINTRESOURCEW(10));   // RT_RCDATA
    HGLOBAL loaded = found != nullptr ? LoadResource(module, found) : nullptr;
    const void* bytes = loaded != nullptr ? LockResource(loaded) : nullptr;
    const DWORD size  = found != nullptr ? SizeofResource(module, found) : 0;
    if (bytes == nullptr || size == 0) return false;
    text = std::string_view(static_cast<const char*>(bytes), size);
    return true;
}

// ---------------------------------------------------------------------------
// Config: a small TOML reader for this file's shape
// ---------------------------------------------------------------------------

struct StringPair {
    std::string item;
    std::string key;
};

struct ChainSpec {
    std::string             name;
    std::int64_t            consume = -1;
    std::int64_t            produce = 1;
    std::vector<std::string> items;
    std::vector<StringPair>  strings;
    int                      line = 0;
    bool                     downgrade = false;   // [[downgrade]] instead of [[chain]]
    std::string              output;              // "stash" (default) or "inventory"
};

struct Settings {
    bool                   enabled = true;
    std::vector<ChainSpec> chains;
};

Settings    Config;
std::string ConfigProblems;

void Problem(int line, const std::string& text) {
    char prefix[32];
    std::snprintf(prefix, sizeof(prefix), "line %d: ", line);
    ConfigProblems += prefix;
    ConfigProblems += text;
    ConfigProblems += " ";
}

struct TomlValue {
    enum class Kind { None, String, Integer, Boolean, Array, Table } kind = Kind::None;
    std::string                                      text;
    std::int64_t                                     number = 0;
    bool                                             flag   = false;
    std::vector<TomlValue>                           items;
    std::vector<std::pair<std::string, TomlValue>>   fields;
};

class TomlReader {
public:
    explicit TomlReader(std::string_view text) : text_(text) {}

    void Parse(Settings& out) {
        std::string table;
        bool        arrayTable = false;
        while (true) {
            SkipBlank(true);
            if (AtEnd()) return;
            const int line = line_;
            if (Peek() == '[') {
                arrayTable = Peek(1) == '[';
                pos_ += arrayTable ? 2 : 1;
                SkipBlank(false);
                table = ReadKey();
                SkipBlank(false);
                if (!Take(']') || (arrayTable && !Take(']'))) {
                    Problem(line, "a table header is not closed.");
                    SkipLine();
                    continue;
                }
                if (arrayTable && (table == "chain" || table == "downgrade")) {
                    out.chains.push_back({});
                    out.chains.back().line      = line;
                    out.chains.back().downgrade = table == "downgrade";
                } else if (arrayTable || (table != "material_upgrade")) {
                    Problem(line, "unknown table [" + table + "], ignored.");
                }
                continue;
            }
            const std::string key = ReadKey();
            SkipBlank(false);
            if (key.empty() || !Take('=')) {
                Problem(line, "expected key = value.");
                SkipLine();
                continue;
            }
            SkipBlank(false);
            TomlValue value;
            if (!ReadValue(value)) {
                Problem(line, "the value of " + key + " could not be read.");
                SkipLine();
                continue;
            }
            Apply(out, table, arrayTable, key, value, line);
        }
    }

private:
    std::string_view text_;
    std::size_t      pos_  = 0;
    int              line_ = 1;

    auto AtEnd() const -> bool { return pos_ >= text_.size(); }
    auto Peek(std::size_t ahead = 0) const -> char {
        return pos_ + ahead < text_.size() ? text_[pos_ + ahead] : '\0';
    }
    auto Take(char c) -> bool {
        if (Peek() != c) return false;
        ++pos_;
        return true;
    }
    void SkipLine() {
        while (!AtEnd() && Peek() != '\n') ++pos_;
    }
    void SkipBlank(bool newlines) {
        while (!AtEnd()) {
            const char c = Peek();
            if (c == ' ' || c == '\t' || c == '\r') {
                ++pos_;
            } else if (c == '#') {
                SkipLine();
            } else if (c == '\n' && newlines) {
                ++pos_;
                ++line_;
            } else {
                return;
            }
        }
    }
    auto ReadKey() -> std::string {
        if (Peek() == '"') {
            TomlValue value;
            return ReadString(value) ? value.text : std::string{};
        }
        std::string key;
        while (!AtEnd()) {
            const char c = Peek();
            if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-') {
                key += c;
                ++pos_;
            } else {
                break;
            }
        }
        return key;
    }
    auto ReadString(TomlValue& value) -> bool {
        if (!Take('"')) return false;
        value.kind = TomlValue::Kind::String;
        while (!AtEnd()) {
            const char c = Peek();
            if (c == '\n') return false;
            ++pos_;
            if (c == '"') return true;
            if (c == '\\' && !AtEnd()) {
                const char e = Peek();
                ++pos_;
                value.text += e == 'n' ? '\n' : e == 't' ? '\t' : e;
                continue;
            }
            value.text += c;
        }
        return false;
    }
    auto ReadValue(TomlValue& value) -> bool {
        const char c = Peek();
        if (c == '"') return ReadString(value);
        if (c == '[') {
            ++pos_;
            value.kind = TomlValue::Kind::Array;
            while (true) {
                SkipBlank(true);
                if (Take(']')) return true;
                TomlValue item;
                if (!ReadValue(item)) return false;
                value.items.push_back(std::move(item));
                SkipBlank(true);
                if (Take(',')) continue;
                SkipBlank(true);
                if (Take(']')) return true;
                return false;
            }
        }
        if (c == '{') {
            ++pos_;
            value.kind = TomlValue::Kind::Table;
            while (true) {
                SkipBlank(false);
                if (Take('}')) return true;
                const std::string key = ReadKey();
                SkipBlank(false);
                if (key.empty() || !Take('=')) return false;
                SkipBlank(false);
                TomlValue field;
                if (!ReadValue(field)) return false;
                value.fields.emplace_back(key, std::move(field));
                SkipBlank(false);
                if (Take(',')) continue;
                SkipBlank(false);
                if (Take('}')) return true;
                return false;
            }
        }
        if (text_.substr(pos_, 4) == "true") {
            pos_ += 4;
            value.kind = TomlValue::Kind::Boolean;
            value.flag = true;
            return true;
        }
        if (text_.substr(pos_, 5) == "false") {
            pos_ += 5;
            value.kind = TomlValue::Kind::Boolean;
            value.flag = false;
            return true;
        }
        bool negative = false;
        if (c == '+' || c == '-') {
            negative = c == '-';
            ++pos_;
        }
        if (Peek() < '0' || Peek() > '9') return false;
        std::int64_t number = 0;
        while (Peek() >= '0' && Peek() <= '9') {
            number = number * 10 + (Peek() - '0');
            if (number > 1'000'000'000) return false;
            ++pos_;
        }
        value.kind   = TomlValue::Kind::Integer;
        value.number = negative ? -number : number;
        return true;
    }

    static void Apply(Settings& out, const std::string& table, bool arrayTable, const std::string& key,
            const TomlValue& value, int line) {
        if (!arrayTable && table == "material_upgrade") {
            if (key == "enabled") {
                if (value.kind == TomlValue::Kind::Boolean) out.enabled = value.flag;
                else Problem(line, "enabled: expected true or false.");
            } else {
                Problem(line, "unknown setting " + key + ", ignored.");
            }
            return;
        }
        if (!arrayTable || (table != "chain" && table != "downgrade") || out.chains.empty()) {
            Problem(line, key + " is outside [material_upgrade], [[chain]] and [[downgrade]], ignored.");
            return;
        }
        ChainSpec& chain = out.chains.back();
        if (key == "name") {
            if (value.kind == TomlValue::Kind::String) chain.name = value.text;
            else Problem(line, "name: expected a string.");
        } else if (key == "consume" || key == "produce") {
            if (value.kind != TomlValue::Kind::Integer) {
                Problem(line, key + ": expected a number.");
                return;
            }
            (key == "consume" ? chain.consume : chain.produce) = value.number;
        } else if (key == "items") {
            if (value.kind != TomlValue::Kind::Array) {
                Problem(line, "items: expected a list of item codes.");
                return;
            }
            for (const TomlValue& item : value.items) {
                if (item.kind == TomlValue::Kind::String) chain.items.push_back(item.text);
                else Problem(line, "items: every entry must be a quoted item code.");
            }
        } else if (key == "strings") {
            if (value.kind != TomlValue::Kind::Array) {
                Problem(line, "strings: expected a list of { item = ..., key = ... } pairs.");
                return;
            }
            for (const TomlValue& pair : value.items) {
                StringPair entry;
                if (pair.kind == TomlValue::Kind::Table) {
                    for (const auto& [name, field] : pair.fields) {
                        if (field.kind != TomlValue::Kind::String) continue;
                        if (name == "item") entry.item = field.text;
                        else if (name == "key") entry.key = field.text;
                    }
                }
                if (entry.item.empty() || entry.key.empty()) {
                    Problem(line, "strings: every entry needs item and key.");
                    continue;
                }
                chain.strings.push_back(std::move(entry));
            }
        } else if (key == "output") {
            if (value.kind == TomlValue::Kind::String) chain.output = value.text;
            else Problem(line, "output: expected \"stash\" or \"inventory\".");
        } else {
            Problem(line, "unknown chain setting " + key + ", ignored.");
        }
    }
};

// ---------------------------------------------------------------------------
// Recipes
// ---------------------------------------------------------------------------

struct Step {
    std::uint32_t inCode  = 0;
    std::uint32_t outCode = 0;
    std::uint32_t consume = 0;
    std::uint32_t produce = 0;
    std::string   key;      // string key naming the result, empty when the pair is missing
    std::string   chain;
    bool          toInventory = false;   // output = "inventory": the result goes to the player's inventory
};

std::unordered_map<std::uint32_t, Step> Steps;        // upgrades, from [[chain]]
std::unordered_map<std::uint32_t, Step> DownSteps;    // downgrades, from [[downgrade]]
std::unordered_set<std::uint32_t>       Listed;       // every code of every [[chain]]: Shift belongs to the plugin there
std::unordered_set<std::uint32_t>       ListedDown;   // every code of every [[downgrade]]: Alt belongs to the plugin there
std::size_t                             MissingKeys{};

auto PackCode(const std::string& code, std::uint32_t& packed) -> bool {
    if (code.empty() || code.size() > 4) return false;
    std::uint32_t value = 0;
    for (std::size_t i = 0; i < 4; ++i) {
        const char c = i < code.size() ? code[i] : ' ';
        if (i < code.size() && (c <= ' ' || c > '~')) return false;
        value |= static_cast<std::uint32_t>(static_cast<std::uint8_t>(c)) << (8 * i);
    }
    packed = value;
    return true;
}

auto CodeText(std::uint32_t code) -> std::string {
    std::string text;
    for (std::size_t i = 0; i < 4; ++i) {
        const char c = static_cast<char>((code >> (8 * i)) & 0xFF);
        if (c != ' ' && c != '\0') text += c;
    }
    return text;
}

void BuildRecipes() {
    Steps.clear();
    DownSteps.clear();
    Listed.clear();
    ListedDown.clear();
    MissingKeys = 0;
    for (const ChainSpec& chain : Config.chains) {
        const std::string label = chain.name.empty() ? (chain.downgrade ? "unnamed downgrade" : "unnamed chain") : chain.name;
        auto&             steps  = chain.downgrade ? DownSteps : Steps;
        auto&             listed = chain.downgrade ? ListedDown : Listed;
        const std::string verb   = chain.downgrade ? "downgrades" : "upgrades";
        if (chain.consume < 1 || chain.consume > 999 || chain.produce < 1 || chain.produce > 999) {
            Problem(chain.line, label + ": consume and produce must be 1 to 999; chain skipped.");
            continue;
        }
        if (chain.items.size() < 2) {
            Problem(chain.line, label + ": items needs at least two codes; chain skipped.");
            continue;
        }
        if (!chain.output.empty() && chain.output != "stash" && chain.output != "inventory") {
            Problem(chain.line, label + ": output must be \"stash\" or \"inventory\"; chain skipped.");
            continue;
        }
        std::vector<std::uint32_t> codes;
        bool                       valid = true;
        for (const std::string& item : chain.items) {
            std::uint32_t packed = 0;
            if (!PackCode(item, packed)) {
                Problem(chain.line, label + ": \"" + item + "\" is not an item code; chain skipped.");
                valid = false;
                break;
            }
            codes.push_back(packed);
        }
        if (!valid) continue;
        for (const StringPair& pair : chain.strings) {
            const bool upgrades = std::find(chain.items.begin(), chain.items.end() - 1, pair.item) != chain.items.end() - 1;
            if (!upgrades) Problem(chain.line, label + ": strings pair for \"" + pair.item + "\", which does not convert in this chain, ignored.");
        }
        for (std::size_t i = 0; i + 1 < codes.size(); ++i) {
            if (codes[i] == codes[i + 1]) {
                Problem(chain.line, label + ": \"" + chain.items[i] + "\" turns into itself; step skipped.");
                continue;
            }
            if (steps.contains(codes[i])) {
                Problem(chain.line, label + ": \"" + chain.items[i] + "\" already " + verb + " in \""
                    + steps[codes[i]].chain + "\"; this step skipped.");
                continue;
            }
            Step step;
            step.inCode  = codes[i];
            step.outCode = codes[i + 1];
            step.consume = static_cast<std::uint32_t>(chain.consume);
            step.produce = static_cast<std::uint32_t>(chain.produce);
            step.chain       = label;
            step.toInventory = chain.output == "inventory";
            for (const StringPair& pair : chain.strings) {
                if (pair.item == chain.items[i]) {
                    step.key = pair.key;
                    break;
                }
            }
            if (step.key.empty()) {
                ++MissingKeys;
                Problem(chain.line, label + ": \"" + chain.items[i] + "\" has no strings pair; its line shows the code \""
                    + chain.items[i + 1] + "\".");
            }
            steps.emplace(codes[i], std::move(step));
            listed.insert(codes[i]);
            listed.insert(codes[i + 1]);
        }
    }
}

void ReadConfiguration() {
    std::string_view embedded;
    std::string      defaults;
    if (Embedded(MATERIAL_UPGRADE_DEFAULT_CONFIG_ID, embedded)) defaults.assign(embedded);
    if (!Context->EnsureConfig(defaults.c_str())) {
        Context->LogWarn("MaterialUpgrade: the config file could not be created; using the built-in recipes.");
    }
    std::string   buffer(512 * 1024, '\0');
    std::uint32_t required = 0;
    if (!Context->ReadConfig(buffer.data(), static_cast<std::uint32_t>(buffer.size()), &required)) {
        if (required > buffer.size() && required < 16u * 1024 * 1024) {
            buffer.assign(required + 1, '\0');
            if (!Context->ReadConfig(buffer.data(), static_cast<std::uint32_t>(buffer.size()), nullptr)) buffer.clear();
        } else {
            buffer.clear();
        }
    }
    if (buffer.empty()) {
        Context->LogWarn("MaterialUpgrade: the config file could not be read; using the built-in recipes.");
        buffer = defaults;
    }
    buffer.resize(std::strlen(buffer.c_str()));
    TomlReader reader(buffer);
    reader.Parse(Config);
    BuildRecipes();
    if (!ConfigProblems.empty()) D2RL::LogWarnF(Context, "MaterialUpgrade: config: %s", ConfigProblems.c_str());
}

// ---------------------------------------------------------------------------
// Text
// ---------------------------------------------------------------------------

auto Localized(const char* key, std::string& out) -> bool {
    if (LocalizationSvc == nullptr) return false;
    char          buffer[1024];
    std::uint32_t required = 0;
    buffer[0] = '\0';
    const auto result = LocalizationSvc->getStringByKey(Context, key, buffer, sizeof(buffer), &required);
    if (result != D2RL::Localization::Result::Success || buffer[0] == '\0') return false;
    out = buffer;
    return true;
}

// The mod's own strings (the game's string files) first, then the plugin's
// built-in strings.json, so the mod rewords any line by adding the same Key.
auto TextForKey(const std::string& key, std::string& out) -> bool {
    const std::string native = "d2r:" + key;
    if (Localized(native.c_str(), out)) return true;
    return Localized(key.c_str(), out);
}

constexpr const char* FallbackMakeOne   = "Shift + Left Click to Make {made} {result} (Uses {used})";
constexpr const char* FallbackMakeAll   = "Shift + Right Click to Make {made} {result} (Uses {used})";
constexpr const char* FallbackNotEnough = "Shift + Left Click to Make {made} {result} (Needs {need}, Have {have})";
constexpr const char* FallbackDownOne   = "Alt + Left Click to Make {made} {result} (Uses {used})";
constexpr const char* FallbackDownAll   = "Alt + Right Click to Make {made} {result} (Uses {used})";
constexpr const char* FallbackDownShort = "Alt + Left Click to Make {made} {result} (Needs {need}, Have {have})";
constexpr const char* FallbackHoldMake  = " (Hold to Make {made} {result})";
constexpr const char* FallbackHoldShort = " (Hold to Make {made} {result}: Needs {need}, Have {have})";
constexpr const char* FallbackMoveToBelt = "{combo} to Move to Belt";
constexpr const char* FallbackCtrlCombo  = "Ctrl + Left Click";

auto Pattern(const char* key, const char* fallback) -> std::string {
    std::string text;
    return TextForKey(key, text) ? text : std::string(fallback);
}

void Fill(std::string& text, std::string_view token, const std::string& value) {
    for (std::size_t at = text.find(token); at != std::string::npos; at = text.find(token, at + value.size())) {
        text.replace(at, token.size(), value);
    }
}

auto Line(const char* key, const char* fallback, const std::string& result, std::uint64_t made, std::uint64_t used,
        std::uint64_t need, std::uint64_t have) -> std::string {
    std::string text = Pattern(key, fallback);
    Fill(text, "{result}", result);
    Fill(text, "{made}", std::to_string(made));
    Fill(text, "{used}", std::to_string(used));
    Fill(text, "{need}", std::to_string(need));
    Fill(text, "{have}", std::to_string(have));
    return text;
}

auto ResultText(const Step& step) -> std::string {
    std::string text;
    if (!step.key.empty() && TextForKey(step.key, text)) return text;
    return step.key.empty() ? CodeText(step.outCode) : step.key;
}

// ---------------------------------------------------------------------------
// Items
// ---------------------------------------------------------------------------

auto StackCount(void* item) noexcept -> std::uint32_t {
    if (item == nullptr) return 0;
    const void* data = Read<const void*>(item, ItemDataOffset);
    return data != nullptr ? Read<std::uint32_t>(data, StackCountOffset) : 0;
}

auto ItemCodeOf(void* item, std::uint32_t& code) noexcept -> bool {
    if (item == nullptr) return false;
    const std::int32_t classId = ClassIdOf(item, SourceTag, __LINE__);
    if (classId < 0) return false;
    const std::uint8_t* record = ItemRecord(ItemBank(item), static_cast<std::uint32_t>(classId));
    if (record == nullptr) return false;
    code = Read<std::uint32_t>(record, RecordCodeOffset);
    return true;
}

auto SlotItem(void* widget) noexcept -> void* {
    return widget != nullptr ? Read<void*>(widget, SlotItemOffset) : nullptr;
}

auto LocalPlayer() noexcept -> void* {
    return PlayerFromIndex(LocalPlayerIndex());
}

void Refuse() noexcept {
    ClicksRefused.fetch_add(1);
    (void)PlaySound(RefusalSound, nullptr, 0, 0, false);
}

// ---------------------------------------------------------------------------
// Network: client side
// ---------------------------------------------------------------------------

std::mutex     PendingMutex;
UpgradeMessage Pending{};
bool           HasPending{};
std::uint64_t  PendingMs{};
constexpr std::uint64_t PendingLifetimeMs = 5000;

auto Send(const UpgradeMessage& message) noexcept -> bool {
    return NetworkSvc->sendToHost(Context, Channel, UpgradeMessageId, &message, sizeof(message))
        == D2RL::Network::Result::Success;
}

void Connect() noexcept {
    (void)NetworkSvc->connectToHost(Context, Channel);
}

void FlushPending() noexcept {
    UpgradeMessage message{};
    {
        std::lock_guard lock(PendingMutex);
        if (!HasPending) return;
        HasPending = false;
        if (GetTickCount64() - PendingMs > PendingLifetimeMs) {
            ClicksDropped.fetch_add(1);
            return;
        }
        message = Pending;
    }
    (Send(message) ? ClicksSent : ClicksDropped).fetch_add(1);
}

void SendUpgrade(const UpgradeMessage& message) noexcept {
    if (Send(message)) {
        ClicksSent.fetch_add(1);
        return;
    }
    {
        std::lock_guard lock(PendingMutex);
        if (HasPending) ClicksDropped.fetch_add(1);
        Pending    = message;
        HasPending = true;
        PendingMs  = GetTickCount64();
    }
    Connect();
}

// ---------------------------------------------------------------------------
// Client hooks (UI thread)
// ---------------------------------------------------------------------------

enum class Direction { None, Up, Down };

// Shift alone upgrades, Alt alone downgrades; Ctrl, or both together, leaves
// the click to the game.
auto ClickDirection() noexcept -> Direction {
    const bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
    const bool ctrl  = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
    const bool alt   = (GetKeyState(VK_MENU) & 0x8000) != 0;
    if (ctrl || shift == alt) return Direction::None;
    return shift ? Direction::Up : Direction::Down;
}

// True when the click was the plugin's.
auto ModifiedClick(void* widget, bool all) noexcept -> bool {
    if (!Active.load()) return false;
    const Direction direction = ClickDirection();
    if (direction == Direction::None) return false;
    const bool  down = direction == Direction::Down;
    void* const item = SlotItem(widget);
    if (item == nullptr || CursorItem() != nullptr) return false;
    std::uint32_t code = 0;
    if (!ItemCodeOf(item, code) || !(down ? ListedDown : Listed).contains(code)) return false;
    const std::uint32_t have = StackCount(item);
    if (have == 0) return false;
    const auto& steps = down ? DownSteps : Steps;
    const auto  found = steps.find(code);
    if (found == steps.end() || have < found->second.consume) {
        Refuse();
        return true;
    }
    void* const player = LocalPlayer();
    void* const owner  = player != nullptr ? (StashView() != 0 ? StashOwnerA(player) : StashOwnerB(player)) : nullptr;
    if (owner == nullptr) {
        Refuse();
        return true;
    }
    const UpgradeMessage message{
        UnitIdOf(player, SourceTag, __LINE__),
        UnitIdOf(owner, SourceTag, __LINE__),
        UnitIdOf(item, SourceTag, __LINE__),
        code,
        (all ? FlagAll : 0u) | (down ? FlagDowngrade : 0u),
    };
    SendUpgrade(message);
    return true;
}

auto KeyDown(int key) noexcept -> bool {
    return (GetKeyState(key) & 0x8000) != 0;
}

// The test of the game's own Shift + Left Click belt move (2CF872, 2CF882):
// the stash shows the stack tab, the item is beltable and the belt has room.
auto BeltFirstApplies(void* item) noexcept -> bool {
    return item != nullptr && StackTabView() != 0 && BeltAccepts(item) != 0;
}

// Ctrl + Left Click, belt first (see the header): the game's belt request in
// place of its inventory move, for an item the belt takes right now. True
// when the click was sent to the belt.
auto BeltFirstClick(void* widget) noexcept -> bool {
    if (!Active.load() || !KeyDown(VK_CONTROL)) return false;
    void* const item = SlotItem(widget);
    if (item == nullptr || CursorItem() != nullptr || StackCount(item) == 0) return false;
    if (KeyDown(VK_SHIFT) && CubeRoute() != 0) return false;   // the game's move to the cube
    if (!BeltFirstApplies(item)) return false;                 // the game's move to the inventory
    void* const player = LocalPlayer();
    void* const owner  = player != nullptr ? (StashView() != 0 ? StashOwnerA(player) : StashOwnerB(player)) : nullptr;
    if (owner == nullptr) return false;
    WithdrawRequest(item, owner, BeltDestination);
    (void)PlaySound(ItemSound(item), nullptr, 0, 0, false);
    BeltMoves.fetch_add(1);
    return true;
}

// The next cell of the belt search of 3862D0 (3864C7..3865A1) for a unit of
// item, on a copy of the belt's cells; -1 when the belt is full. same is the
// column test, 36A8D0(item, bottom). A cell past the end of the copy counts
// as taken.
template <typename Same>
auto NextBeltCell(const std::array<void*, MaxBeltCells>& cells, std::size_t size, std::uint32_t boxes, void* item,
        Same&& same) -> std::int32_t {
    for (std::uint32_t column = 0; column < BeltColumns; ++column) {
        if (column >= size || cells[column] == nullptr) continue;
        if (!same(item, cells[column]) || column >= boxes) continue;
        for (std::uint32_t at = column; at < boxes; at += BeltColumns) {
            if (at < size && cells[at] == nullptr) return static_cast<std::int32_t>(at);
        }
    }
    for (std::uint32_t column = 0; column < BeltColumns; ++column) {
        if (column < size && cells[column] == nullptr) return static_cast<std::int32_t>(column);
    }
    return -1;
}

// The belt's box count, as 3862D0 reads it (38643D..3864A2): the belts.txt
// record of the belt in body cell 8, row 2 without a belt.
auto BeltBoxes(void* inventory) noexcept -> std::uint32_t {
    std::int32_t row = NoBeltRow;
    if (const std::uint8_t* const body = InventoryGrid(inventory, BodyGrid, At<const void*>(BodyGridInfoRva))) {
        void* const* const bodyCells = Read<void* const*>(body, GridCellsOffset);
        void* const belt = bodyCells != nullptr ? bodyCells[BeltBodyCell] : nullptr;
        if (belt != nullptr) row = BeltType(belt);
    }
    void* const owner = Read<void*>(inventory, InventoryOwnerOffset);
    if (owner == nullptr) return 0;
    alignas(16) std::uint8_t record[BeltRecordSize]{};
    (void)BeltRecord(ItemBank(owner), row, 0, record);
    return record[BeltBoxesOffset];
}

// How many of want units of item the belt takes, each where the game's belt
// search puts it (see the header). Called once BeltAccepts said the belt
// takes one.
auto BeltRoom(void* item, std::uint32_t want) noexcept -> std::uint32_t {
    void* const player = LocalPlayer();
    void* const inventory = player != nullptr ? UnitInventory(player, SourceTag, __LINE__) : nullptr;
    if (inventory == nullptr) return 0;
    std::int32_t first = -1;
    if (FreeBeltSlot(inventory, item, &first, 1) == 0) return 0;
    const std::uint32_t boxes = BeltBoxes(inventory);
    const std::uint8_t* const grid = InventoryGrid(inventory, BeltGrid, At<const void*>(BeltGridInfoRva));
    void* const* const source = grid != nullptr ? Read<void* const*>(grid, GridCellsOffset) : nullptr;
    const std::size_t size = grid != nullptr ? std::size_t{ grid[GridWidthOffset] } * grid[GridHeightOffset] : 0;
    if (source == nullptr || size == 0 || size > MaxBeltCells) {
        BeltMismatches.fetch_add(1);
        return 1;
    }
    std::array<void*, MaxBeltCells> cells{};
    std::copy(source, source + size, cells.begin());
    const auto same = [](void* a, void* b) noexcept { return SameBeltItem(a, b) != 0; };
    std::uint32_t taken = 0;
    while (taken < want) {
        const std::int32_t at = NextBeltCell(cells, size, boxes, item, same);
        if (at < 0) break;
        if (taken == 0 && at != first) break;
        cells[static_cast<std::size_t>(at)] = item;   // the unit the host puts there is of the same item
        ++taken;
    }
    if (taken == 0) {
        BeltMismatches.fetch_add(1);
        return 1;
    }
    return taken;
}

// Ctrl + Right Click, belt first (see the header): the game's Ctrl + Right
// Click count, to the belt as far as it takes them and the rest to the
// inventory. True when the click was sent from here.
auto BeltFirstBatchClick(void* widget) noexcept -> bool {
    if (!Active.load() || !BatchBeltActive.load() || !KeyDown(VK_CONTROL)) return false;
    void* const item = SlotItem(widget);
    if (item == nullptr || CursorItem() != nullptr) return false;
    const std::uint32_t units = std::min(StackCount(item), BatchMoveLimit);
    if (units == 0) return false;
    if (KeyDown(VK_SHIFT) && CubeRoute() != 0) return false;   // the game's move to the cube
    if (!BeltFirstApplies(item)) return false;                 // the game's move to the inventory
    void* const player = LocalPlayer();
    void* const owner  = player != nullptr ? (StashView() != 0 ? StashOwnerA(player) : StashOwnerB(player)) : nullptr;
    if (owner == nullptr) return false;
    const std::uint32_t belt = std::min(BeltRoom(item, units), units);
    if (belt == 0) return false;
    BatchWithdraw(item, owner, BeltDestination, static_cast<std::uint8_t>(belt));
    const std::uint32_t rest = units - belt;
    if (rest != 0 && InventoryAccepts(item) != 0) {
        BatchWithdraw(item, owner, InventoryDestination, static_cast<std::uint8_t>(rest));
        BatchToInventory.fetch_add(rest);
    }
    (void)PlaySound(ItemSound(item), nullptr, 0, 0, false);
    BatchMoves.fetch_add(1);
    BatchToBelt.fetch_add(belt);
    return true;
}

std::int64_t __fastcall HookedLeftClick(void* widget, std::int64_t event) {
    bool mine = false;
    try {
        mine = ModifiedClick(widget, false) || BeltFirstClick(widget);
    } catch (...) {
        mine = false;
    }
    return mine ? 0 : OriginalLeftClick(widget, event);
}

std::int64_t __fastcall HookedRightClick(void* widget) {
    bool mine = false;
    try {
        mine = ModifiedClick(widget, true) || BeltFirstBatchClick(widget);
    } catch (...) {
        mine = false;
    }
    return mine ? 0 : OriginalRightClick(widget);
}

std::string Separator;

void AppendText(NativeString* out, const std::string& text) noexcept {
    if (out == nullptr || text.empty()) return;
    if (out->size != 0 && !Separator.empty()) (void)StringAppend(out, Separator.data(), Separator.size());
    (void)StringAppend(out, text.data(), text.size());
}

struct LineKeys {
    const char* one;
    const char* oneFallback;
    const char* all;
    const char* allFallback;
    const char* shortKey;
    const char* shortFallback;
};

constexpr LineKeys UpgradeLines{ "MaterialUpgradeMakeOne", FallbackMakeOne, "MaterialUpgradeMakeAll", FallbackMakeAll,
    "MaterialUpgradeNotEnough", FallbackNotEnough };
constexpr LineKeys DowngradeLines{ "MaterialUpgradeDowngradeOne", FallbackDownOne, "MaterialUpgradeDowngradeAll",
    FallbackDownAll, "MaterialUpgradeDowngradeNotEnough", FallbackDownShort };

void AppendStepLines(NativeString* out, const Step& step, std::uint64_t have, const LineKeys& keys) {
    const std::string result = ResultText(step);
    if (have < step.consume) {
        AppendText(out, Line(keys.shortKey, keys.shortFallback, result, step.produce, 0, step.consume, have));
        return;
    }
    AppendText(out, Line(keys.one, keys.oneFallback, result, step.produce, step.consume, step.consume, have));
    const std::uint64_t times = have / step.consume;
    if (times >= 2) {
        AppendText(out, Line(keys.all, keys.allFallback, result, times * step.produce, times * step.consume,
            step.consume, have));
    }
}

void AppendHints(NativeString* out, void* item) {
    std::uint32_t code = 0;
    if (!ItemCodeOf(item, code)) return;
    const auto up   = Steps.find(code);
    const auto down = DownSteps.find(code);
    if (up == Steps.end() && down == DownSteps.end()) return;
    const std::uint64_t have = StackCount(item);
    if (Separator.empty() && !Localized("d2r:newline", Separator)) Separator = "\n";
    if (up != Steps.end()) AppendStepLines(out, up->second, have, UpgradeLines);
    if (down != DownSteps.end()) AppendStepLines(out, down->second, have, DowngradeLines);
}

// sprintf with exactly one %s, the way the game builds its hint line
// (1400CE510 is a vsnprintf wrapper); false for any other conversion.
auto FormatOneString(const std::string& pattern, const std::string& value, std::string& out) -> bool {
    out.clear();
    bool used = false;
    for (std::size_t i = 0; i < pattern.size(); ++i) {
        if (pattern[i] != '%') {
            out += pattern[i];
            continue;
        }
        if (i + 1 >= pattern.size()) return false;
        const char c = pattern[++i];
        if (c == '%') {
            out += '%';
        } else if (c == 's' && !used) {
            out += value;
            used = true;
        } else {
            return false;
        }
    }
    return used;
}

// sprintf with exactly one %d, %i or %u, the way the game adds its count to a
// move line (1400783D0 appends a vsnprintf); false for any other conversion.
auto FormatOneNumber(const std::string& pattern, std::uint64_t value, std::string& out) -> bool {
    out.clear();
    bool used = false;
    for (std::size_t i = 0; i < pattern.size(); ++i) {
        if (pattern[i] != '%') {
            out += pattern[i];
            continue;
        }
        if (i + 1 >= pattern.size()) return false;
        const char c = pattern[++i];
        if (c == '%') {
            out += '%';
        } else if ((c == 'd' || c == 'i' || c == 'u') && !used) {
            out += std::to_string(value);
            used = true;
        } else {
            return false;
        }
    }
    return used;
}

// The count the game adds to its inventory line (2CED0A): on a stack of 2 or
// more, InventoryItemTooltipAppenderBulkMovePostfix with min(count, 3).
auto BatchSuffix(void* item) -> std::string {
    const std::uint32_t have = StackCount(item);
    if (have < 2) return {};
    std::string pattern;
    std::string text;
    if (!TextForKey("InventoryItemTooltipAppenderBulkMovePostfix", pattern)
            || !FormatOneNumber(pattern, std::min(have, BatchMoveLimit), text)) {
        HintMisses.fetch_add(1);
        return {};
    }
    return text;
}

auto SplitText(const std::string& text, const std::string& separator) -> std::vector<std::string> {
    std::vector<std::string> lines;
    if (text.empty()) return lines;
    std::size_t start = 0;
    for (std::size_t at = text.find(separator); at != std::string::npos; at = text.find(separator, start)) {
        lines.push_back(text.substr(start, at - start));
        start = at + separator.size();
    }
    lines.push_back(text.substr(start));
    return lines;
}

auto JoinText(const std::vector<std::string>& lines, const std::string& separator) -> std::string {
    std::string text;
    for (std::size_t i = 0; i < lines.size(); ++i) {
        if (i != 0) text += separator;
        text += lines[i];
    }
    return text;
}

// The game's hint lines whose clicks this plugin changes (see the header).
// Runs before the plugin's own lines are added, so out holds only the game's.
void RewriteNativeHints(NativeString* out, void* item) {
    if (out->data == nullptr) return;
    std::uint32_t code = 0;
    const bool shiftIsMine = ItemCodeOf(item, code) && Listed.contains(code);
    const bool beltFirst   = BeltFirstApplies(item);
    if (!shiftIsMine && !beltFirst) return;
    if (Separator.empty() && !Localized("d2r:newline", Separator)) Separator = "\n";
    std::vector<std::string> lines = SplitText(std::string(out->data, out->size), Separator);
    bool changed = false;
    std::string equip;
    if (shiftIsMine && TextForKey("InventoryItemTooltipAppenderEquip", equip) && !equip.empty()) {
        changed = std::erase(lines, equip) != 0;
    }
    if (beltFirst) {
        std::string combo;
        std::string pattern;
        std::string inventory;
        const bool haveCombo = TextForKey("InventoryItemTooltipKeyComboCtrlLC", combo) && !combo.empty();
        const bool built     = haveCombo && TextForKey("InventoryItemTooltipAppenderMoveInventory", pattern)
            && FormatOneString(pattern, combo, inventory) && !inventory.empty();
        std::string belt = Pattern("MaterialUpgradeMoveToBelt", FallbackMoveToBelt);
        Fill(belt, "{combo}", haveCombo ? combo : std::string(FallbackCtrlCombo));
        bool done = false;
        for (std::string& line : lines) {
            const std::size_t at = built ? line.find(inventory) : std::string::npos;
            if (at == std::string::npos) continue;
            line.replace(at, inventory.size(), belt);
            done = true;
            break;
        }
        if (!done && InventoryAccepts(item) == 0) {
            lines.insert(lines.begin(), BatchBeltActive.load() ? belt + BatchSuffix(item) : belt);
            done = true;
        }
        if (done) {
            changed = true;
        } else {
            HintMisses.fetch_add(1);
        }
    }
    if (!changed) return;
    const std::string text = JoinText(lines, Separator);
    out->size    = 0;
    out->data[0] = '\0';
    if (!text.empty()) (void)StringAppend(out, text.data(), text.size());
}

NativeString* __fastcall HookedHintLines(void* self, NativeString* out, void* item) {
    NativeString* const result = OriginalHintLines(self, out, item);
    if (!Active.load() || out == nullptr || item == nullptr) return result;
    try {
        RewriteNativeHints(out, item);
    } catch (...) {
    }
    try {
        AppendHints(out, item);
    } catch (...) {
    }
    return result;
}

// Controller: the stack-tab slot widget, by its vtable.
auto IsStackSlot(void* widget) noexcept -> bool {
    return widget != nullptr && Read<std::uintptr_t>(widget, 0) == Base + StashSlotVtableRva;
}

// Hold select on a stack slot: the whole stack, like Shift + Right Click.
std::int64_t __fastcall HookedHoldAction(void* widget, void* item) {
    bool mine = false;
    try {
        std::uint32_t code = 0;
        if (ControllerActive.load() && Active.load() && IsStackSlot(widget) && item != nullptr && ItemCodeOf(item, code)
                && Listed.contains(code)) {
            mine = true;
            const std::uint32_t have  = StackCount(item);
            const auto          found = Steps.find(code);
            void* const player = LocalPlayer();
            void* const owner  = player != nullptr ? (StashView() != 0 ? StashOwnerA(player) : StashOwnerB(player)) : nullptr;
            if (found == Steps.end() || have < found->second.consume || owner == nullptr) {
                Refuse();
            } else {
                SendUpgrade({ UnitIdOf(player, SourceTag, __LINE__), UnitIdOf(owner, SourceTag, __LINE__),
                    UnitIdOf(item, SourceTag, __LINE__), code, FlagAll });
            }
        }
    } catch (...) {
    }
    return mine ? 0 : OriginalHoldAction(widget, item);
}

// The select prompt of a stack slot whose item is in a chain: the native
// "Pick Up" stays, its hold part says what holding makes (nothing for a top
// tier, whose hold now does nothing).
void RewriteSelectPrompt(void* widget, NativeString* out) {
    if (out == nullptr || out->data == nullptr || !IsStackSlot(widget) || CursorItem() != nullptr) return;
    void* const   item = SlotItem(widget);
    std::uint32_t code = 0;
    if (item == nullptr || !ItemCodeOf(item, code) || !Listed.contains(code)) return;
    std::string base;
    if (!TextForKey("ControllerPromptItemPickUpFromInventory", base) || out->size < base.size()
            || std::memcmp(out->data, base.data(), base.size()) != 0) {
        return;
    }
    out->size             = base.size();
    out->data[base.size()] = '\0';
    const auto found = Steps.find(code);
    if (found == Steps.end()) return;
    const Step&         step   = found->second;
    const std::uint64_t have   = StackCount(item);
    const std::string   result = ResultText(step);
    const std::string   suffix = have < step.consume
        ? Line("MaterialUpgradeHoldNotEnough", FallbackHoldShort, result, step.produce, 0, step.consume, have)
        : Line("MaterialUpgradeHoldMake", FallbackHoldMake, result, (have / step.consume) * step.produce,
              (have / step.consume) * step.consume, step.consume, have);
    (void)StringAppend(out, suffix.data(), suffix.size());
}

NativeString* __fastcall HookedSelectPrompt(void* widget, NativeString* out) {
    NativeString* const result = OriginalSelectPrompt(widget, out);
    if (!ControllerActive.load() || !Active.load()) return result;
    try {
        RewriteSelectPrompt(widget, out);
    } catch (...) {
    }
    return result;
}

// ---------------------------------------------------------------------------
// Host: players (game thread)
// ---------------------------------------------------------------------------

struct PlayerRef {
    std::uint32_t id;
    void*         unit;
    void*         game;
};

constexpr std::size_t MaxPlayers        = 16;
constexpr std::size_t MaxClientsVisited = 256;

// Every player of the local server: each client in the registry, its player
// resolved through the game's own lookup.
auto CollectPlayers(std::array<PlayerRef, MaxPlayers>& players) noexcept -> std::size_t {
    if (Read<std::int32_t>(reinterpret_cast<const void*>(Base + RegistryReadyRva), 0) == 0) return 0;
    const auto* const buckets = reinterpret_cast<const std::uint8_t* const*>(Base + RegistryRva);
    std::size_t count   = 0;
    std::size_t visited = 0;
    for (std::size_t bucket = 0; bucket < RegistryBuckets; ++bucket) {
        for (const std::uint8_t* client = buckets[bucket]; client != nullptr;
                client = Read<const std::uint8_t*>(client, ClientNextOffset)) {
            if (++visited > MaxClientsVisited) return count;
            void* const game = Read<void*>(client, ClientGameOffset);
            if (game == nullptr || Read<std::uint32_t>(client, ClientUnitTypeOffset) != UnitTypePlayer) continue;
            const std::uint32_t id   = Read<std::uint32_t>(client, ClientUnitIdOffset);
            void* const         unit = ServerUnit(game, UnitTypePlayer, id);
            if (unit == nullptr || UnitType(unit) != UnitTypePlayer || count >= players.size()) continue;
            players[count++] = { id, unit, game };
        }
    }
    return count;
}

// The checks of the native stack-tab request handler (4C4AF0).
auto StashOwnerFor(void* game, void* player, std::uint32_t ownerId) noexcept -> void* {
    void* const owner = ServerUnit(game, UnitTypePlayer, ownerId);
    if (owner == nullptr || CheckState(owner, StashState) == 0) return nullptr;
    void* const ownerInventory = UnitInventory(owner, SourceTag, __LINE__);
    if (ownerInventory == nullptr || InventoryOwner(ownerInventory) != UnitIdOf(player, SourceTag, __LINE__)) return nullptr;
    void* const playerInventory = UnitInventory(player, SourceTag, __LINE__);
    void* const node = playerInventory != nullptr ? InventoryNode(playerInventory, UnitIdOf(owner, SourceTag, __LINE__)) : nullptr;
    if (node == nullptr || NodeByte8(node) != 0 || NodeByte9(node) == 0) return nullptr;
    return owner;
}

// One unit out of the owner's stack for this class, as the native withdraw.
auto TakeOne(void* game, void* owner, std::uint32_t classId) noexcept -> bool {
    void* const slot = StashSlot(owner, classId);
    if (slot == nullptr) return false;
    PacketState state{};
    CaptureState(&state, slot);
    if (StashDebit(owner, classId) == nullptr) return false;
    ItemFlag(slot, 0x4000, 0);
    UpdateFlag(slot, 1);
    SendUpdate(game, owner, slot, 32, 4, state.mode, state.page, state.node, state.xy);
    UpdateFlag(slot, 0);
    ItemPage(slot, 0);
    return true;
}

// One unit into the owner's stack for this class, as the native deposit.
auto AddOne(void* game, void* player, void* owner, std::uint32_t classId) noexcept -> bool {
    if (StashSlot(owner, classId) == nullptr) {
        void* const item = CreateItem(player, classId, game, 4, 2, 1, 1, 1, 0, 0, 0);
        if (item == nullptr) return false;
        if (StashInsert(owner, item) == 0) {
            FreeItem(game, item);
            return false;
        }
    }
    void* const slot = StashCredit(owner, classId);
    if (slot == nullptr) return false;
    PacketState state{};
    InitState(&state, 4);
    AddFlag(slot, 1);
    SendUpdate(game, owner, slot, 0x2000, 2, state.mode, state.page, state.node, state.xy);
    AddFlag(slot, 0);
    return true;
}

// Upgrades one at a time, results first; returns how many were done.
auto Convert(void* game, void* player, void* owner, std::uint32_t inClass, std::uint32_t outClass, const Step& step,
        std::uint64_t times, bool& stoppedFull) noexcept -> std::uint64_t {
    std::uint64_t done = 0;
    stoppedFull = false;
    for (; done < times; ++done) {
        std::uint32_t added = 0;
        while (added < step.produce && AddOne(game, player, owner, outClass)) ++added;
        if (added < step.produce) {
            for (std::uint32_t i = 0; i < added; ++i) (void)TakeOne(game, owner, outClass);
            stoppedFull = true;
            break;
        }
        std::uint32_t taken = 0;
        while (taken < step.consume && TakeOne(game, owner, inClass)) ++taken;
        if (taken < step.consume) {
            for (std::uint32_t i = 0; i < taken; ++i) (void)AddOne(game, player, owner, inClass);
            for (std::uint32_t i = 0; i < step.produce; ++i) (void)TakeOne(game, owner, outClass);
            break;
        }
    }
    return done;
}

// The item service's answer to the last failed createItem, for the log.
D2RL::Items::Result LastCreateResult = D2RL::Items::Result::Success;

auto ItemResultText(D2RL::Items::Result result) noexcept -> const char* {
    switch (result) {
    case D2RL::Items::Result::Success:          return "Success";
    case D2RL::Items::Result::InvalidArgument:  return "InvalidArgument";
    case D2RL::Items::Result::Unsupported:      return "Unsupported";
    case D2RL::Items::Result::Unavailable:      return "Unavailable";
    case D2RL::Items::Result::Conflict:         return "Conflict";
    case D2RL::Items::Result::NotFound:         return "NotFound";
    case D2RL::Items::Result::Busy:             return "Busy";
    case D2RL::Items::Result::OwnerInactive:    return "OwnerInactive";
    case D2RL::Items::Result::OwnerMismatch:    return "OwnerMismatch";
    case D2RL::Items::Result::StaleHandle:      return "StaleHandle";
    case D2RL::Items::Result::CallbackFault:    return "CallbackFault";
    case D2RL::Items::Result::PolicyRejected:   return "PolicyRejected";
    case D2RL::Items::Result::NotAuthoritative: return "NotAuthoritative";
    }
    return "unknown";
}

// The quality the game gives a new item of this class when Normal is asked
// for. The game's quality step (442D60) turns an item whose type is always
// magic (ItemTypes record +14h, the Magic column, read by 374270 through the
// ItemsTxt type at +12Eh) into a magic item, and D2RLoader's createItem
// refuses any item that comes out with another quality than asked (D2RCore
// 4242B0 returns Unsupported), so the request has to ask for that quality.
constexpr std::size_t ItemsTxtTypeOffset     = 0x12E;   // int16, the type column
constexpr std::size_t ItemTypesMagicOffset   = 0x14;    // byte, the Magic column
auto CreationQuality(std::uint8_t bank, const std::uint8_t* record) noexcept -> D2RL::Items::Quality {
    if (record == nullptr) return D2RL::Items::Quality::Normal;
    const std::int16_t type = Read<std::int16_t>(record, ItemsTxtTypeOffset);
    const std::uint8_t* typeRecord = type >= 0 ? ItemTypeRecord(bank, type) : nullptr;
    return typeRecord != nullptr && Read<std::uint8_t>(typeRecord, ItemTypesMagicOffset) != 0
        ? D2RL::Items::Quality::Magic : D2RL::Items::Quality::Normal;
}

// One result in the player's inventory, through D2RLoader's item service:
// automatic placement, item level 1 like an item taken out of a stack tab.
auto CreateInInventory(D2RL::PlayerHandle player, std::uint32_t code, D2RL::Items::Quality quality,
        D2RL::ItemHandle& item) noexcept -> bool {
    D2RL::Items::ItemCreateSpec spec{};
    spec.structSize             = D2RL::Items::ItemCreateSpecSize;
    spec.code                   = code;
    spec.quality                = quality;
    spec.qualityRecordId        = D2RL::Items::RandomQualityRecord;
    spec.itemLevel              = 1;
    spec.seedMode               = D2RL::Items::SeedMode::Random;
    spec.quantity               = D2RL::Items::DefaultValue;
    spec.durability             = D2RL::Items::DefaultValue;
    spec.stateFlags             = D2RL::Items::ItemStateIdentified;
    spec.destination.structSize = D2RL::Items::ItemDestinationSize;
    spec.destination.container  = D2RL::Items::ItemContainer::Inventory;
    spec.destination.placement  = D2RL::Items::Placement::Automatic;
    item = D2RL::InvalidItemHandle;
    if (ItemSvc == nullptr) return false;
    const D2RL::Items::Result result = ItemSvc->createItem(Context, player, &spec, &item);
    if (result == D2RL::Items::Result::Success && item != D2RL::InvalidItemHandle) return true;
    LastCreateResult = result;
    return false;
}

void DestroyCreated(D2RL::PlayerHandle player, const std::vector<D2RL::ItemHandle>& items) noexcept {
    for (const D2RL::ItemHandle item : items) {
        (void)ItemSvc->destroyItem(Context, player, item, D2RL::Items::SocketedItemPolicy::RejectIfNotEmpty);
    }
}

// Like Convert, with the results made in the player's inventory. A result that
// finds no room stops it; a stack that cannot pay takes its results back.
auto ConvertToInventory(void* game, void* player, void* owner, std::uint32_t inClass, const Step& step,
        D2RL::Items::Quality quality, std::uint64_t times, D2RL::PlayerHandle handle, bool& stoppedFull)
        -> std::uint64_t {
    std::uint64_t                 done = 0;
    std::vector<D2RL::ItemHandle> made;
    made.reserve(step.produce);
    stoppedFull = false;
    for (; done < times; ++done) {
        made.clear();
        D2RL::ItemHandle item = D2RL::InvalidItemHandle;
        while (made.size() < step.produce && CreateInInventory(handle, step.outCode, quality, item)) made.push_back(item);
        if (made.size() < step.produce) {
            DestroyCreated(handle, made);
            stoppedFull = true;
            break;
        }
        std::uint32_t taken = 0;
        while (taken < step.consume && TakeOne(game, owner, inClass)) ++taken;
        if (taken < step.consume) {
            for (std::uint32_t i = 0; i < taken; ++i) (void)AddOne(game, player, owner, inClass);
            DestroyCreated(handle, made);
            break;
        }
    }
    return done;
}

struct Request {
    D2RL::Network::PeerHandle peer;
    UpgradeMessage            message;
};

constexpr std::size_t MaxRequests = 64;
std::mutex             RequestMutex;
std::vector<Request>   RequestQueue;
std::atomic<bool>      DrainQueued{};

auto HostLogAllowed() noexcept -> bool {
    static std::uint64_t window = 0;
    static std::uint32_t lines  = 0;
    const std::uint64_t now = GetTickCount64() / 10'000;
    if (now != window) {
        window = now;
        lines  = 0;
    }
    return ++lines <= 20;
}

void RefuseRequest(const char* why, const UpgradeMessage& message) noexcept {
    RequestsRefused.fetch_add(1);
    char line[200];
    std::snprintf(line, sizeof(line), "refused: %s (player %u, stack %u, item %s).", why, message.playerId,
        message.stackId, CodeText(message.code).c_str());
    SetLast(line);
    if (HostLogAllowed()) D2RL::LogInfoF(Context, "MaterialUpgrade: %s", line);
}

void Process(const Request& request, const std::array<PlayerRef, MaxPlayers>& players, std::size_t count) {
    const UpgradeMessage& message = request.message;
    D2RL::PlayerHandle    sender  = D2RL::InvalidPlayerHandle;
    if (NetworkSvc->getPeerPlayer(Context, Channel, request.peer, &sender) != D2RL::Network::Result::Success
            || sender == D2RL::InvalidPlayerHandle) {
        RefuseRequest("the sender is not a player of this game", message);
        return;
    }
    const PlayerRef* ref = nullptr;
    for (std::size_t i = 0; i < count; ++i) {
        if (players[i].id == message.playerId) ref = &players[i];
    }
    if (ref == nullptr) {
        RefuseRequest("no such player", message);
        return;
    }
    void* const owner = StashOwnerFor(ref->game, ref->unit, message.ownerId);
    if (owner == nullptr) {
        RefuseRequest("not the player's stash", message);
        return;
    }
    void* const stack = ServerUnit(ref->game, UnitTypeItem, message.stackId);
    const std::int32_t inClass = stack != nullptr ? ClassIdOf(stack, SourceTag, __LINE__) : -1;
    if (inClass < 0 || StashSlot(owner, static_cast<std::uint32_t>(inClass)) != stack) {
        RefuseRequest("not a stack of that stash", message);
        return;
    }
    std::uint32_t code = 0;
    if (!ItemCodeOf(stack, code) || code != message.code) {
        RefuseRequest("the stack changed", message);
        return;
    }
    const bool  down  = (message.flags & FlagDowngrade) != 0;
    const auto& steps = down ? DownSteps : Steps;
    const auto  found = steps.find(code);
    if (found == steps.end()) {
        RefuseRequest(down ? "no downgrade recipe on the host" : "no upgrade recipe on the host", message);
        return;
    }
    const Step&  step     = found->second;
    std::int32_t outClass = -1;
    const std::uint8_t        bank      = ItemBank(stack);
    const std::uint8_t* const outRecord = RecordFromCode(bank, step.outCode, &outClass);
    if (outRecord == nullptr || outClass < 0) {
        RefuseRequest("the result's item code does not exist", message);
        return;
    }
    const std::uint64_t have  = StackCount(stack);
    const std::uint64_t times = (message.flags & FlagAll) != 0 ? have / step.consume : (have >= step.consume ? 1 : 0);
    if (times == 0) {
        RefuseRequest("not enough items", message);
        return;
    }
    if (step.toInventory && ItemSvc == nullptr) {
        RefuseRequest("D2RLoader's item service is not available for an inventory result", message);
        return;
    }
    bool stoppedFull = false;
    LastCreateResult = D2RL::Items::Result::Success;
    const std::uint64_t done = step.toInventory
        ? ConvertToInventory(ref->game, ref->unit, owner, static_cast<std::uint32_t>(inClass), step,
              CreationQuality(bank, outRecord), times, sender, stoppedFull)
        : Convert(ref->game, ref->unit, owner, static_cast<std::uint32_t>(inClass), static_cast<std::uint32_t>(outClass),
              step, times, stoppedFull);
    if (done > 0) {
        MarkDirty(ref->unit, 1);
        MarkDirty(owner, 1);
    }
    (down ? DowngradesDone : UpgradesDone).fetch_add(done);
    if (stoppedFull) UpgradesStoppedFull.fetch_add(1);
    char stopped[120] = "";
    if (stoppedFull && step.toInventory) {
        std::snprintf(stopped, sizeof(stopped), ", stopped: the item service did not make the result (%s)",
            ItemResultText(LastCreateResult));
    } else if (stoppedFull) {
        std::snprintf(stopped, sizeof(stopped), ", stopped: the result's stack is full");
    }
    char line[320];
    std::snprintf(line, sizeof(line), "player %u: %llu of %llu %s %s -> %s (%u for %u each)%s.",
        message.playerId, static_cast<unsigned long long>(done), static_cast<unsigned long long>(times),
        down ? "downgrades" : "upgrades",
        CodeText(step.inCode).c_str(), CodeText(step.outCode).c_str(), step.produce, step.consume,
        stopped);
    SetLast(line);
    if (HostLogAllowed()) D2RL::LogInfoF(Context, "MaterialUpgrade: %s", line);
}

void __cdecl HostDrain(const D2RL::PluginContext* context, void*) noexcept {
    DrainQueued.store(false);
    if (context == nullptr || !Active.load()) return;
    std::vector<Request> requests;
    {
        std::lock_guard lock(RequestMutex);
        requests.swap(RequestQueue);
    }
    if (requests.empty()) return;
    std::array<PlayerRef, MaxPlayers> players{};
    const std::size_t count = CollectPlayers(players);
    for (const Request& request : requests) {
        try {
            Process(request, players, count);
        } catch (...) {
            RequestsRefused.fetch_add(1);
        }
    }
}

void __cdecl OnHostMessage(const D2RL::PluginContext* context, D2RL::Network::ChannelHandle,
        D2RL::Network::PeerHandle peer, std::uint16_t messageId, const void* data, std::uint32_t size,
        void*) noexcept {
    if (context == nullptr || !Active.load()) return;
    if (messageId != UpgradeMessageId || data == nullptr || size != sizeof(UpgradeMessage)) {
        RequestsRefused.fetch_add(1);
        return;
    }
    Request request{ peer, {} };
    std::memcpy(&request.message, data, sizeof(UpgradeMessage));
    try {
        std::lock_guard lock(RequestMutex);
        if (RequestQueue.size() >= MaxRequests) {
            RequestsRefused.fetch_add(1);
            return;
        }
        RequestQueue.push_back(request);
    } catch (...) {
        RequestsRefused.fetch_add(1);
        return;
    }
    RequestsReceived.fetch_add(1);
    if (!DrainQueued.exchange(true)
            && ThreadSvc->runOnGameThread(context, &HostDrain, nullptr) != D2RL::Threads::Result::Success) {
        DrainQueued.store(false);
    }
}

// The host never answers; the item updates reach the client by themselves.
void __cdecl OnClientMessage(const D2RL::PluginContext*, D2RL::Network::ChannelHandle, std::uint16_t, const void*,
        std::uint32_t, void*) noexcept {}

void __cdecl OnConnectionState(const D2RL::PluginContext* context, const D2RL::Network::ConnectionEvent* event,
        void*) noexcept {
    if (context == nullptr || !D2RL::Network::HasConnectionEventField(event, D2RL::Network::ConnectionEventRequiredSize)) {
        return;
    }
    ConnectionNow.store(static_cast<std::uint32_t>(event->state));
    if (event->state == D2RL::Network::ConnectionState::Rejected) {
        D2RL::LogWarnF(context, "MaterialUpgrade: the host refused the plugin channel (reason %u); upgrades cannot "
            "reach it. The host needs this plugin, the same version.", static_cast<unsigned>(event->reason));
        return;
    }
    if (event->state == D2RL::Network::ConnectionState::Connected) FlushPending();
}

// ---------------------------------------------------------------------------
// Game join and leave
// ---------------------------------------------------------------------------

void ClearSession() noexcept {
    {
        std::lock_guard lock(PendingMutex);
        HasPending = false;
    }
    std::lock_guard lock(RequestMutex);
    RequestQueue.clear();
}

void __cdecl OnGameJoined(const D2RL::PluginContext*, const D2RL::Lifecycle::GameplayEvent*, void*) noexcept {
    ClearSession();
    ConnectionNow.store(static_cast<std::uint32_t>(D2RL::Network::ConnectionState::Disconnected));
}

void __cdecl OnLocalPlayerReady(const D2RL::PluginContext* context, const D2RL::Lifecycle::GameplayEvent*,
        void*) noexcept {
    if (context != nullptr && Active.load()) Connect();
}

void __cdecl OnGameLeft(const D2RL::PluginContext*, const D2RL::Lifecycle::GameplayEvent*, void*) noexcept {
    ClearSession();
    ConnectionNow.store(static_cast<std::uint32_t>(D2RL::Network::ConnectionState::Disconnected));
}

// ---------------------------------------------------------------------------
// Checks and installation
// ---------------------------------------------------------------------------

auto VerifyAll() -> bool {
    bool ok = true;
    for (const Witness& witness : Witnesses) {
        if (Context->CheckExpectedBytes(witness.rva, witness.bytes, witness.size)) continue;
        ok = false;
        char line[160];
        std::snprintf(line, sizeof(line), "%s (%llX); ", witness.name, static_cast<unsigned long long>(witness.rva));
        VerifyFailures += line;
    }
    if (!Context->CheckExpectedBytes(CreditCallRva - 3, CreditCallHeadBytes, sizeof(CreditCallHeadBytes))
            || !Context->CheckExpectedBytes(CreditCallRva + 5, CreditCallTailBytes, sizeof(CreditCallTailBytes))) {
        ok = false;
        VerifyFailures += "deposit credit call (4F9CD3); ";
    }
    for (const FixSite& site : FixSites) {
        if (Context->CheckExpectedBytes(site.rva, site.native, site.size)
                || Context->CheckExpectedBytes(site.rva, site.fixed, site.size)) {
            continue;
        }
        ok = false;
        char line[160];
        std::snprintf(line, sizeof(line), "%s count fix site (%llX); ", site.name, static_cast<unsigned long long>(site.rva));
        VerifyFailures += line;
    }
    if (!ok) D2RL::LogErrorF(Context, "MaterialUpgrade: byte check failed: %s", VerifyFailures.c_str());
    return ok;
}

// The game code only Ctrl + Right Click belt first relies on. A mismatch turns
// that click off and leaves the rest of the plugin running.
void VerifyBatch() {
    std::string failures;
    for (const Witness& witness : BatchWitnesses) {
        if (Context->CheckExpectedBytes(witness.rva, witness.bytes, witness.size)) continue;
        char line[160];
        std::snprintf(line, sizeof(line), "%s (%llX); ", witness.name, static_cast<unsigned long long>(witness.rva));
        failures += line;
    }
    if (failures.empty()) {
        BatchBeltStatus = "on";
        BatchBeltActive.store(true);
        return;
    }
    BatchBeltStatus = "off, byte check failed: " + failures;
    D2RL::LogWarnF(Context, "MaterialUpgrade: Ctrl + Right Click belt first is off, its byte check failed: %s; the "
        "game's own Ctrl + Right Click runs.", failures.c_str());
}

// The count fix: each site is rewritten unless it already holds the fixed bytes.
auto ApplyCountFix() -> bool {
    for (FixSite& site : FixSites) {
        if (Context->CheckExpectedBytes(site.rva, site.fixed, site.size)) {
            site.status = "already in place";
            continue;
        }
        if (Context->PatchBytes(site.rva, site.native, site.size, site.fixed, site.size)) {
            site.status = "applied";
            continue;
        }
        site.status = "FAILED";
        D2RL::LogErrorF(Context, "MaterialUpgrade: the %s count fix at %llX could not be written.", site.name,
            static_cast<unsigned long long>(site.rva));
        return false;
    }
    return true;
}

// The credit the native deposit calls, through whatever relay D2RLoader put on
// that call: the rel32 is read from the live instruction.
auto ResolveCredit() -> bool {
    const std::int32_t rel    = Read<std::int32_t>(reinterpret_cast<const void*>(Base + CreditCallRva + 1), 0);
    const std::uintptr_t target = Base + CreditCallRva + 5 + static_cast<std::intptr_t>(rel);
    MEMORY_BASIC_INFORMATION info{};
    if (VirtualQuery(reinterpret_cast<const void*>(target), &info, sizeof(info)) == 0 || info.State != MEM_COMMIT
            || (info.Protect & (PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY)) == 0) {
        Context->LogError("MaterialUpgrade: the deposit's credit call does not lead to code; nothing was installed.");
        return false;
    }
    StashCredit = reinterpret_cast<StashSlotFn>(target);
    const auto* head = reinterpret_cast<const std::uint8_t*>(target);
    char line[96];
    std::snprintf(line, sizeof(line), "%s at image+%llX", head[0] == 0xFF && head[1] == 0x25 ? "loader relay" : "direct",
        static_cast<unsigned long long>(target - Base));
    CreditTarget = line;
    return true;
}

void BindNatives() noexcept {
    CursorItem       = At<CursorItemFn>(CursorItemRva);
    StringAppend     = At<StringAppendFn>(StringAppendRva);
    StashView        = At<StashViewFn>(StashViewRva);
    StashOwnerA      = At<StashOwnerFn>(StashOwnerARva);
    StashOwnerB      = At<StashOwnerFn>(StashOwnerBRva);
    LocalPlayerIndex = At<PlayerIndexFn>(LocalPlayerIndexRva);
    PlayerFromIndex  = At<PlayerFromFn>(PlayerFromIndexRva);
    PlaySound        = At<PlaySoundFn>(PlaySoundRva);
    UnitIdOf         = At<UnitIdFn>(UnitIdRva);
    UnitType         = At<UnitTypeFn>(UnitTypeRva);
    ClassIdOf        = At<ClassIdFn>(ClassIdRva);
    ItemBank         = At<ItemBankFn>(ItemBankRva);
    ItemRecord       = At<ItemRecordFn>(ItemRecordRva);
    RecordFromCode   = At<RecordFromCodeFn>(RecordFromCodeRva);
    ItemTypeRecord   = At<ItemTypeRecordFn>(ItemTypeRecordRva);
    ServerUnit       = At<ServerUnitFn>(ServerUnitRva);
    CheckState       = At<CheckStateFn>(CheckStateRva);
    UnitInventory    = At<UnitInventoryFn>(UnitInventoryRva);
    InventoryOwner   = At<InventoryOwnerFn>(InventoryOwnerRva);
    InventoryNode    = At<InventoryNodeFn>(InventoryNodeRva);
    NodeByte9        = At<NodeByteFn>(NodeByte9Rva);
    NodeByte8        = At<NodeByteFn>(NodeByte8Rva);
    StashSlot        = At<StashSlotFn>(StashSlotRva);
    StashDebit       = At<StashSlotFn>(StashDebitRva);
    StashInsert      = At<StashInsertFn>(StashInsertRva);
    CreateItem       = At<CreateItemFn>(CreateItemRva);
    FreeItem         = At<FreeItemFn>(FreeItemRva);
    InitState        = At<InitStateFn>(InitStateRva);
    CaptureState     = At<CaptureStateFn>(CaptureStateRva);
    SendUpdate       = At<SendUpdateFn>(SendUpdateRva);
    AddFlag          = At<UnitFlagFn>(AddFlagRva);
    UpdateFlag       = At<UnitFlagFn>(UpdateFlagRva);
    ItemFlag         = At<ItemFlagFn>(ItemFlagRva);
    ItemPage         = At<ItemPageFn>(ItemPageRva);
    MarkDirty        = At<UnitFlagFn>(MarkDirtyRva);
    StackTabView     = At<StashViewFn>(StackTabViewRva);
    CubeRoute        = At<StashViewFn>(CubeRouteRva);
    BeltAccepts      = At<ItemTestFn>(BeltAcceptsRva);
    InventoryAccepts = At<ItemTestFn>(InventoryAcceptsRva);
    WithdrawRequest  = At<WithdrawFn>(WithdrawRequestRva);
    ItemSound        = At<ItemSoundFn>(ItemSoundRva);
    BatchWithdraw    = At<BatchWithdrawFn>(BatchWithdrawRva);
    FreeBeltSlot     = At<FreeBeltSlotFn>(FreeBeltSlotRva);
    InventoryGrid    = At<InventoryGridFn>(InventoryGridRva);
    BeltType         = At<BeltTypeFn>(BeltTypeRva);
    BeltRecord       = At<BeltRecordFn>(BeltRecordRva);
    SameBeltItem     = At<SameBeltItemFn>(SameBeltItemRva);
}

auto QueryServices() -> bool {
    if (Context->QueryService(&NetworkSvc) != D2RL::ServiceQueryResult::Success
            || !D2RL::HasNetworkServiceField(NetworkSvc, D2RL::NetworkServiceRequiredSize)) {
        Context->LogError("MaterialUpgrade: the network service is unavailable.");
        return false;
    }
    if (Context->QueryService(&ItemSvc) != D2RL::ServiceQueryResult::Success
            || !D2RL::HasItemServiceField(ItemSvc, D2RL::ItemServiceRequiredSize)) {
        ItemSvc = nullptr;
        D2RL::LogWarnF(Context, "MaterialUpgrade: D2RLoader's item service is not available; recipes with output = "
            "\"inventory\" are refused.");
    }
    if (Context->QueryService(&ThreadSvc) != D2RL::ServiceQueryResult::Success
            || !D2RL::HasThreadServiceField(ThreadSvc, D2RL::ThreadServiceRequiredSize)) {
        Context->LogError("MaterialUpgrade: the thread service is unavailable.");
        return false;
    }
    if (Context->QueryService(&LifecycleSvc) != D2RL::ServiceQueryResult::Success
            || !D2RL::HasLifecycleServiceField(LifecycleSvc, D2RL::LifecycleServiceRequiredSize)) {
        Context->LogError("MaterialUpgrade: the lifecycle service is unavailable.");
        return false;
    }
    if (Context->QueryService(&LocalizationSvc) != D2RL::ServiceQueryResult::Success
            || !D2RL::HasLocalizationServiceField(LocalizationSvc, D2RL::LocalizationServiceRequiredSize)) {
        LocalizationSvc = nullptr;
        Context->LogWarn("MaterialUpgrade: the localization service is unavailable; the hover text is English only.");
    }
    if (Context->QueryService(&ResourceSvc) != D2RL::ServiceQueryResult::Success
            || !D2RL::HasResourceServiceField(ResourceSvc, D2RL::ResourceServiceRequiredSize)) {
        ResourceSvc = nullptr;
        Context->LogWarn("MaterialUpgrade: the resource service is unavailable; the plugin's strings are not loaded.");
    }
    return true;
}

void RegisterStrings() {
    std::string_view json;
    if (ResourceSvc == nullptr || !Embedded(MATERIAL_UPGRADE_STRINGS_ID, json)) {
        Context->LogWarn("MaterialUpgrade: the string file is missing from the DLL; the hover text is English only.");
        return;
    }
    const D2RL::Resources::ResourceRegistration resource{
        .structSize = D2RL::Resources::ResourceRegistrationSize,
        .flags      = 0,
        .path       = StringsPath,
        .bytes      = json.data(),
        .byteCount  = json.size(),
    };
    const auto result = ResourceSvc->registerResource(Context, &resource, &StringsHandle);
    if (result != D2RL::Resources::Result::Success) {
        D2RL::LogWarnF(Context, "MaterialUpgrade: %s could not be registered (result %u).", StringsPath,
            static_cast<unsigned>(result));
    }
}

auto RegisterListeners() -> bool {
    struct Game {
        D2RL::Lifecycle::GameplayEventKind     kind;
        D2RL::Lifecycle::GameplayEventCallback callback;
    };
    const std::array<Game, 3> games{ {
        { D2RL::Lifecycle::GameplayEventKind::GameJoined, &OnGameJoined },
        { D2RL::Lifecycle::GameplayEventKind::LocalPlayerReady, &OnLocalPlayerReady },
        { D2RL::Lifecycle::GameplayEventKind::GameLeft, &OnGameLeft },
    } };
    for (const Game& game : games) {
        const D2RL::Lifecycle::GameplayEventListener listener{
            .structSize = D2RL::Lifecycle::GameplayEventListenerSize,
            .kind       = game.kind,
            .callback   = game.callback,
        };
        D2RL::Lifecycle::ListenerHandle handle = D2RL::Lifecycle::InvalidHandle;
        if (LifecycleSvc->registerGameplayEventListener(Context, &listener, &handle) != D2RL::Lifecycle::Result::Success) {
            Context->LogError("MaterialUpgrade: a game join/leave listener was refused.");
            return false;
        }
    }
    return true;
}

auto RegisterChannel() -> bool {
    const D2RL::Network::ChannelRegistration registration{
        .structSize         = D2RL::Network::ChannelRegistrationSize,
        .localChannelId     = ChannelLocalId,
        .compatibilityToken = CompatibilityToken,
        .hostMessage        = &OnHostMessage,
        .clientMessage      = &OnClientMessage,
        .connectionState    = &OnConnectionState,
    };
    const D2RL::Network::Result result = NetworkSvc->registerChannel(Context, &registration, &Channel);
    if (result != D2RL::Network::Result::Success || Channel == D2RL::Network::InvalidChannelHandle) {
        D2RL::LogErrorF(Context, "MaterialUpgrade: the network channel was refused (result %u).",
            static_cast<unsigned>(result));
        return false;
    }
    return true;
}

// The vtable slots the controller hooks rely on, read live (they hold
// relocated addresses).
auto StackSlotVtableMatches() noexcept -> bool {
    const auto* vtable = reinterpret_cast<const std::uintptr_t*>(Base + StashSlotVtableRva);
    return vtable[0x100 / 8] == Base + SlotLeftClickRva && vtable[0x108 / 8] == Base + SlotRightClickRva
        && vtable[0x110 / 8] == Base + SelectPromptRva && vtable[0x118 / 8] == Base + HoldActionRva;
}

void InstallControllerHooks() {
    if (!StackSlotVtableMatches()) {
        ControllerStatus = "off: the stack slot vtable does not match";
    } else if (!Context->InstallInlineHook(HoldActionRva, HoldActionBytes, sizeof(HoldActionBytes), &HookedHoldAction,
                   &OriginalHoldAction)) {
        ControllerStatus = "off: the hold hook could not be installed";
    } else if (!Context->InstallInlineHook(SelectPromptRva, SelectPromptBytes, sizeof(SelectPromptBytes),
                   &HookedSelectPrompt, &OriginalSelectPrompt)) {
        ControllerStatus = "off: the prompt hook could not be installed";
    } else {
        ControllerStatus = "on (hold A / Cross)";
        ControllerActive.store(true);
        return;
    }
    D2RL::LogWarnF(Context, "MaterialUpgrade: controller support %s; mouse and keyboard still work.", ControllerStatus);
}

auto InstallHooks() -> bool {
    if (!Context->InstallInlineHook(SlotLeftClickRva, SlotLeftClickBytes, sizeof(SlotLeftClickBytes),
            &HookedLeftClick, &OriginalLeftClick)) {
        Context->LogError("MaterialUpgrade: the stack slot left-click hook could not be installed.");
        return false;
    }
    if (!Context->InstallInlineHook(SlotRightClickRva, SlotRightClickBytes, sizeof(SlotRightClickBytes),
            &HookedRightClick, &OriginalRightClick)) {
        Context->LogError("MaterialUpgrade: the stack slot right-click hook could not be installed.");
        return false;
    }
    if (!Context->InstallInlineHook(SlotHintLinesRva, SlotHintLinesBytes, sizeof(SlotHintLinesBytes),
            &HookedHintLines, &OriginalHintLines)) {
        Context->LogError("MaterialUpgrade: the hint-line hook could not be installed.");
        return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// Console command
// ---------------------------------------------------------------------------

auto Count(const std::atomic<std::uint64_t>& counter) noexcept -> unsigned long long {
    return static_cast<unsigned long long>(counter.load());
}

auto StateText() noexcept -> const char* {
    switch (State) {
    case PluginState::NotLoaded:        return "not loaded";
    case PluginState::DisabledByConfig: return "disabled by config";
    case PluginState::UnsupportedBuild: return "unsupported build (byte check failed)";
    case PluginState::ServicesMissing:  return "a D2RLoader service is missing";
    case PluginState::InstallFailed:    return "installation failed";
    case PluginState::NoRecipes:        return "no usable recipes in the config";
    case PluginState::Active:           return "active";
    }
    return "?";
}

auto ConnectionText(std::uint32_t state) noexcept -> const char* {
    switch (static_cast<D2RL::Network::ConnectionState>(state)) {
    case D2RL::Network::ConnectionState::Disconnected: return "not connected";
    case D2RL::Network::ConnectionState::Connecting:   return "connecting";
    case D2RL::Network::ConnectionState::Connected:    return "connected";
    case D2RL::Network::ConnectionState::Rejected:     return "refused by the host";
    }
    return "?";
}

auto __cdecl StatusCommand(D2R::Game::Client*, const D2RL::ConsoleCommandContext* command, void*) noexcept
        -> D2RL::ConsoleCommandResult {
    if (command == nullptr || command->plugin == nullptr) return D2RL::ConsoleCommandResult::Failed;
    char       line[512];
    const auto say = [&](const char* text) { command->plugin->WriteConsoleMessage(text); };
    std::snprintf(line, sizeof(line), "Material Upgrade %s: %s.", PluginVersion, StateText());
    say(line);
    if (!VerifyFailures.empty()) {
        std::snprintf(line, sizeof(line), "  byte check: %s", VerifyFailures.c_str());
        say(line);
    }
    std::snprintf(line, sizeof(line), "  recipes: %zu upgrades and %zu downgrades from %zu chains; Shift owned on %zu "
        "items, Alt on %zu; %zu steps without a strings pair.", Steps.size(), DownSteps.size(), Config.chains.size(),
        Listed.size(), ListedDown.size(), MissingKeys);
    say(line);
    if (!ConfigProblems.empty()) {
        std::snprintf(line, sizeof(line), "  config: %.440s", ConfigProblems.c_str());
        say(line);
    }
    if (State == PluginState::Active || State == PluginState::NoRecipes) {
        std::snprintf(line, sizeof(line), "  count fix: take-one %s, count reader %s.", FixSites[0].status,
            FixSites[1].status);
        say(line);
        std::snprintf(line, sizeof(line), "  credit: %s. Channel: %s. Controller: %s. Item service: %s.",
            CreditTarget.c_str(), ConnectionText(ConnectionNow.load()), ControllerStatus,
            ItemSvc != nullptr ? "available" : "missing");
        say(line);
    }
    std::snprintf(line, sizeof(line), "  clicks: %llu sent, %llu refused here, %llu dropped. Host: %llu requests, "
        "%llu refused, %llu upgrades and %llu downgrades done, %llu stopped by a full stack.", Count(ClicksSent),
        Count(ClicksRefused), Count(ClicksDropped), Count(RequestsReceived), Count(RequestsRefused),
        Count(UpgradesDone), Count(DowngradesDone), Count(UpgradesStoppedFull));
    say(line);
    std::snprintf(line, sizeof(line), "  Ctrl + Left Click: %llu sent to the belt first. Hint lines not matched: %llu.",
        Count(BeltMoves), Count(HintMisses));
    say(line);
    std::snprintf(line, sizeof(line), "  Ctrl + Right Click belt first: %.200s. %llu sent, %llu units to the belt and "
        "%llu to the inventory; belt counts not matching the game: %llu.", BatchBeltStatus.c_str(), Count(BatchMoves),
        Count(BatchToBelt), Count(BatchToInventory), Count(BeltMismatches));
    say(line);
    {
        std::lock_guard lock(LastMutex);
        std::snprintf(line, sizeof(line), "  last host request: %s", LastResult.c_str());
    }
    say(line);
    return D2RL::ConsoleCommandResult::Handled;
}

constexpr D2RL::PluginInfo PluginInfoData{
    .infoSize    = D2RL::PluginInfoSize,
    .abiVersion  = D2RL_PLUGIN_ABI_VERSION,
    .id          = PluginIdText,
    .name        = "Material Upgrade",
    .version     = PluginVersion,
    .author      = "CelestialRayOne",
    .description = "Shift + click upgrades and Alt + click downgrades materials in the stash's stack tabs in place, by "
                   "recipe chains from the config file; with the right button, the whole stack. Ctrl + click puts "
                   "potions in the belt first.",
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

    if (!Context->RegisterConsoleCommand("materialupgrade", &StatusCommand,
            "Material Upgrade status: recipes, installation, connection and the last upgrade request.")) {
        Context->LogWarn("MaterialUpgrade: the console command could not be registered.");
    }

    try {
        ReadConfiguration();
    } catch (...) {
        Context->LogError("MaterialUpgrade: the config could not be read.");
        return true;
    }
    if (!Config.enabled) {
        State = PluginState::DisabledByConfig;
        Context->LogInfo("MaterialUpgrade: disabled by config.");
        return true;
    }
    if (Steps.empty() && DownSteps.empty()) {
        State = PluginState::NoRecipes;
        Context->LogError("MaterialUpgrade: the config has no usable recipe; nothing was installed.");
        return true;
    }
    if (!VerifyAll()) {
        State = PluginState::UnsupportedBuild;
        Context->LogError("MaterialUpgrade: this D2R build does not match; nothing was installed.");
        return true;
    }
    VerifyBatch();
    BindNatives();
    if (!ResolveCredit()) {
        State = PluginState::UnsupportedBuild;
        return true;
    }
    if (!QueryServices()) {
        State = PluginState::ServicesMissing;
        return true;
    }
    RegisterStrings();
    if (!RegisterListeners() || !RegisterChannel()) {
        State = PluginState::ServicesMissing;
        return true;
    }
    if (!ApplyCountFix() || !InstallHooks()) {
        State = PluginState::InstallFailed;
        return true;
    }
    InstallControllerHooks();
    Active.store(true);
    State = PluginState::Active;
    D2RL::LogInfoF(Context, "MaterialUpgrade %s: active, %zu upgrades and %zu downgrades from %zu chains; credit %s; "
        "count fix: take-one %s, count reader %s; controller %s.", PluginVersion, Steps.size(), DownSteps.size(),
        Config.chains.size(),
        CreditTarget.c_str(), FixSites[0].status, FixSites[1].status, ControllerStatus);
    return true;
}

D2RL_PLUGIN_EXPORT void D2RLoaderUnloadPlugin() noexcept {
    Active.store(false);
    ControllerActive.store(false);
    BatchBeltActive.store(false);
    if (Context == nullptr) return;
    if (NetworkSvc != nullptr && Channel != D2RL::Network::InvalidChannelHandle) {
        (void)NetworkSvc->unregisterChannel(Context, Channel);
        Channel = D2RL::Network::InvalidChannelHandle;
    }
}
