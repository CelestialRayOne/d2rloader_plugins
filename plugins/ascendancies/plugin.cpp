// Ascendancies
//
// The ascendancy system in one plugin:
//   - the Ascendancy window: a plugin panel, one page per ascendancy (portrait,
//     the five tiers, the next-tier line), with Ascend / Renounce Ascendancy.
//   - an "Ascendancy" entry on the character sheet: a + while the Orb of
//     Creation is consumed and no ascendancy is chosen, a - afterwards.
//   - a "New ascension tier unlocked" notice on the HUD.
//   - the host: ascend and renounce requests, tier upgrades on boss kills and
//     Primordial Soulstones, the old Just Killed kill credit (Kill Ledger,
//     Rathma's Eternal Skull), and the one-time migration of Ascendancy Stones.
// The tier bonuses are applied with the game's own property code, the same way
// monprop.txt applies monster properties, into a stat list of their own state
// on the character. Nothing of it is saved: the plugin builds it from the
// saved stats (which ascendancy, which tiers) every time.
//
// ---------------------------------------------------------------------------
// Saved stats (itemstatcost.txt, Saved 1, CSvBits above 0)
// ---------------------------------------------------------------------------
//   ascendancy_stat   which ascendancy, 1-based [[ascendancy]] block, 0 none
//   progress_stat     bit 0-4 tiers 1-5, bit 5 the Lich King bonus, bit 6 the
//                     character has ascended at least once (the sheet keeps a -)
//   soulstone_stat    Primordial Soulstones collected, up to soulstone_cap
//   orb_stat          the Orb of Creation is consumed (the mod's softcode sets
//                     it; renouncing sets it back to 0)
//
// ---------------------------------------------------------------------------
// Bonus list (host)
// ---------------------------------------------------------------------------
//   3D4620 (target, property, state, flags) is the property call monprop uses
//   (0, null, unit, null, 0, 0, property, 0, 40h, null). With a state id its
//   property functions find or create that state's own stat list on the unit
//   (3D5230) and write into it, and the stat change runtime arms auras and
//   on-hit events as for monprop. A property is 16 bytes: int16 id, int16 type
//   (0 property, 1 property group), int32 param, int32 min, int32 max.
//   Property codes resolve through the game's own name linkers, as the cube
//   and monprop rows resolve theirs: Properties at data tables +270h, then
//   PropertyGroups at +278h, A121F0 (linker, name, 0) -> id or below 0.
//   Removing the list: 2F5940 (unit, state) finds it, 3354C0 turns the state
//   off, 2F7920 unlinks it, 2F4180 (data context, list) frees it.
//   Death deletes every aura event (type 8/9) but not this list (its owner type
//   is 4), so the list is rebuilt after a resurrect. A rebuild takes two frames:
//   the state goes off in one, and in the next the old list is swapped for the
//   new one with the state back on, so the clients always receive the new stats
//   and the character's totals never drop for a frame.
//
// ---------------------------------------------------------------------------
// Kill credit (host)
// ---------------------------------------------------------------------------
//   The old Just Killed curses applied their state through 433D20, the timed
//   state effect (D2RCore), with a 30h-byte block: +0 source unit, +8 target
//   unit, +10h skill, +14h level, +18h frames, +1Ch stat (-1 none), +20h stat
//   value, +24h state, +28h remove callback (null: the default one). Open
//   Wounds (5842F3) passes its stat in the block the same way. The plugin
//   calls it with the curse's own skill, level, state, frames and kill_check
//   value for every living player in the level of the dead boss.
//
// ---------------------------------------------------------------------------
// Native pieces, D2RLoader 1.3.1 process image, all checked byte for byte at
// load. Nothing is installed when a check fails. No code is hooked.
// ---------------------------------------------------------------------------
//   3D4620  property apply          A121F0  name linker lookup
//   300A90  data tables of context  34A0E0  data context of unit (+1BDh)
//   433D20  timed state effect (thunk into D2RCore)
//   2F5940  stat list by state      2F7920  unlink   2F4180  free
//   3354C0  state toggle            34B440  unit room
//   2EFC10  level id of room        34C2C0  unit is dead
//   846170  top-level panel by name (the character sheet)
//   856220  child widget by name    86DD50  text box set text
//   86EFB0  button enable           870200  button frame fields
//   1A0C00  play sound by name      846020  UI state (hovered, focused)
//   2F5020 / 2F48C0 / 2F7D10  stat total, own value read, own value write
//   34B9D0 unit type   34A330 unit id   8B2D0 / 9A480 local player
//   48FE80 server unit lookup   485290 / 485320 / 484CDB the client registry

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

namespace {

constexpr char PluginIdText[]  = "celestialrayone.ascendancies";
constexpr char PluginVersion[] = "1.0.13";
constexpr char PanelLocalId[]  = "AscendancyPanel";
constexpr char PanelRootName[] = "celestialrayone.ascendancies/AscendancyPanel";
constexpr char PanelResourcePath[] = "data/global/ui/layouts/celestialrayone.ascendancies/AscendancyPanelhd.json";
#include "AscendancyPanelLayout.inc"

auto PanelLayoutBytes(std::string_view& out) noexcept -> bool {
    static std::string layout;
    if (layout.empty()) {
        for (const char* const chunk : PanelLayoutChunks) layout += chunk;
    }
    out = layout;
    return !layout.empty();
}

const D2RL::PluginContext* Context{};
std::uintptr_t             Base{};

// ---------------------------------------------------------------------------
// Addresses (RVAs into the D2RLoader 1.3.1 process image)
// ---------------------------------------------------------------------------

constexpr std::uint64_t AssignPropertyRva   = 0x3D4620;
constexpr std::uint64_t LinkerLookupRva     = 0xA121F0;
constexpr std::uint64_t DataTablesRva       = 0x300A90;
constexpr std::uint64_t DataContextRva      = 0x34A0E0;
constexpr std::uint64_t TimedEffectRva      = 0x433D20;
constexpr std::uint64_t ListByStateRva      = 0x2F5940;
constexpr std::uint64_t UnlinkListRva       = 0x2F7920;
constexpr std::uint64_t FreeListRva         = 0x2F4180;
constexpr std::uint64_t ToggleStateRva      = 0x3354C0;
constexpr std::uint64_t UnitRoomRva         = 0x34B440;
constexpr std::uint64_t RoomLevelRva        = 0x2EFC10;
constexpr std::uint64_t UnitDeadRva         = 0x34C2C0;
constexpr std::uint64_t UnitInventoryRva    = 0x34A360;
constexpr std::uint64_t FirstItemRva        = 0x388C10;
constexpr std::uint64_t NextItemRva         = 0x38ABA0;
constexpr std::uint64_t ItemCodeRva         = 0x36EF50;
constexpr std::uint64_t ItemFileIndexRva    = 0x36CE70;
constexpr std::uint64_t TopPanelRva         = 0x846170;
constexpr std::uint64_t FindChildRva        = 0x856220;
constexpr std::uint64_t SetTextRva          = 0x86DD50;
constexpr std::uint64_t UiStateRva          = 0x846020;
constexpr std::uint64_t ReadStatRva         = 0x2F5020;
constexpr std::uint64_t ReadOwnStatRva      = 0x2F48C0;
constexpr std::uint64_t WriteOwnStatRva     = 0x2F7D10;
constexpr std::uint64_t UnitTypeRva         = 0x34B9D0;
constexpr std::uint64_t UnitIdRva           = 0x34A330;
constexpr std::uint64_t LocalPlayerIndexRva = 0x08B2D0;
constexpr std::uint64_t PlayerFromIndexRva  = 0x09A480;
constexpr std::uint64_t ServerUnitRva       = 0x48FE80;
constexpr std::uint64_t RegistryResetRva    = 0x485290;
constexpr std::uint64_t ClientLookupRva     = 0x485320;
constexpr std::uint64_t ClientPlayerRva     = 0x484CDB;
constexpr std::uint64_t WidgetChildrenRva   = 0x85622F;
constexpr std::uint64_t WidgetFocusFieldsRva = 0x87598E;
constexpr std::uint64_t ButtonPressRva      = 0x870200;
constexpr std::uint64_t ButtonEnableRva     = 0x86EFB0;
constexpr std::uint64_t PlaySoundByNameRva  = 0x1A0C00;

// Data, proven by the instructions checked below: the lea and the mov inside
// RegistryResetBytes, the lea inside ClientLookupBytes.
constexpr std::uint64_t RegistryRva      = 0x2AAECA0;
constexpr std::uint64_t RegistryReadyRva = 0x2AAF4A0;
constexpr std::size_t   RegistryBuckets  = 256;

constexpr std::size_t ClientNextOffset     = 0x630;
constexpr std::size_t ClientUnitTypeOffset = 0x26C;
constexpr std::size_t ClientUnitIdOffset   = 0x270;
constexpr std::size_t ClientGameOffset     = 0x2B0;

constexpr std::uint32_t UnitTypePlayer  = 0;
constexpr std::uint32_t UnitTypeMonster = 1;
constexpr std::size_t   UnitClassOffset = 0x04;   // unit +0 type, +4 class, +8 id
constexpr std::size_t   GameFrameOffset = 0x170;  // the curse code reads [game+170h] + 1 for its events

// Data tables.
constexpr std::size_t PropertiesLinkerOffset     = 0x270;  // 31A4F6: v3[78] = LINKER_Allocate()
constexpr std::size_t PropertyGroupsLinkerOffset = 0x278;  // 31AEE6: v3[79] = LINKER_Allocate()

// Widgets.
constexpr std::size_t SetVisibleSlot          = 0x50 / 8;
constexpr std::size_t TypeDescSlot            = 0x58 / 8;
constexpr std::size_t TypeDescNameOffset      = 0x08;
constexpr std::size_t TypeDescParentOffset    = 0x10;
constexpr std::size_t WidgetParentOffset      = 0x30;
constexpr std::size_t WidgetNameOffset        = 0x08;
constexpr std::size_t WidgetChildrenOffset    = 0x58;
constexpr std::size_t WidgetChildCountOffset  = 0x60;
constexpr std::size_t UiHoveredOffset         = 0x178;
constexpr std::size_t UiFocusedOffset         = 0x190;
constexpr std::size_t ButtonNormalFrameOffset   = 0x7E8;
constexpr std::size_t ButtonPressedFrameOffset  = 0x7EC;
constexpr std::size_t ButtonHoveredFrameOffset  = 0x7F0;
constexpr std::size_t ButtonDisabledFrameOffset = 0x7F4;
constexpr std::size_t ButtonShownFrameOffset    = 0x710;
constexpr std::size_t ButtonPressedOffset       = 0x679;
constexpr std::size_t ButtonDisabledOffset      = 0x67A;

// itemstatcost.txt record.
constexpr std::size_t   IscFlagsOffset    = 0x04;
constexpr std::uint32_t IscFlagSaved      = 0x800;
constexpr std::size_t   IscCsvBitsOffset  = 0x0A;
constexpr std::size_t   IscCsvParamOffset = 0x0B;

// Stat ids the plugin reads on any unit.
constexpr std::int32_t StatMaxHp = 7;  // maxhp, in 256ths

// ---------------------------------------------------------------------------
// Expected bytes
// ---------------------------------------------------------------------------

// 3D4620, the property apply: mov rax, rsp / push rbx / push rsi / push r12 /
// push r13 / push r15 / sub rsp, 60h / mov rsi, [rsp+0C0h] (the property, 7th
// argument) / mov rbx, r9 / mov r15, r8 (the target) / mov r12, rdx /
// mov r13d, ecx / test rsi, rsi.
constexpr std::uint8_t AssignPropertyBytes[]{
    0x48,0x8B,0xC4,0x53,0x56,0x41,0x54,0x41,0x55,0x41,0x57,0x48,0x83,0xEC,0x60,0x48,
    0x8B,0xB4,0x24,0xC0,0x00,0x00,0x00,0x49,0x8B,0xD9,0x4D,0x8B,0xF8,0x4C,0x8B,0xE2,
    0x44,0x8B,0xE9,0x48,0x85,0xF6 };
// A121F0, the name linker lookup (linker, name, mode).
constexpr std::uint8_t LinkerLookupBytes[]{
    0x40,0x55,0x56,0x57,0x41,0x54,0x48,0x8B,0xEC,0x48,0x83,0xEC,0x78,0x48,0x8B,0x05,
    0xC4,0x90,0xFB,0x01,0x48,0x33,0xC4,0x48,0x89,0x45,0xE8,0x44,0x89,0x45,0xAC };
// 300A90, the data tables of a data context (movzx eax, cl / bounds check).
constexpr std::uint8_t DataTablesBytes[]{
    0x48,0x83,0xEC,0x28,0x0F,0xB6,0xC1,0x48,0x89,0x44,0x24,0x38,0x48,0x83,0xF8,0x04,
    0x72,0x19 };
// 34A0E0, the data context of a unit (null check, then the byte at +1BDh).
constexpr std::uint8_t DataContextBytes[]{
    0x48,0x83,0xEC,0x28,0x48,0x85,0xC9,0x75,0x1A,0x88,0x4C,0x24,0x30,0x48,0x8D,0x4C,
    0x24,0x30,0xE8,0x49,0xC7,0xFF,0xFF,0x84,0xC0,0x74,0x01,0xCC,0x32,0xC0 };
// 433D20: jmp [rip+slot] into D2RCore (ApplyWideTimedStatEffect).
constexpr std::uint8_t TimedEffectBytes[]{ 0xFF,0x25 };
// 2F5940, the stat list of a state (unit, state).
constexpr std::uint8_t ListByStateBytes[]{
    0x48,0x89,0x5C,0x24,0x10,0x57,0x48,0x83,0xEC,0x20,0x8B,0xDA,0x48,0x8B,0xF9,0x48,
    0x85,0xC9,0x75,0x13 };
// 2F7920, unlink (unit, list): mov rbx, rdx / call 34A0E0 / movzx ecx, al /
// mov rdx, rbx / ... / jmp.
constexpr std::uint8_t UnlinkListBytes[]{
    0x40,0x53,0x48,0x83,0xEC,0x20,0x48,0x8B,0xDA,0xE8,0xB2,0x27,0x05,0x00,0x0F,0xB6,
    0xC8,0x48,0x8B,0xD3,0x48,0x83,0xC4,0x20,0x5B,0xE9 };
// 2F4180, free (data context, list): skips a null list and an extended list.
constexpr std::uint8_t FreeListBytes[]{
    0x48,0x85,0xD2,0x74,0x0A,0x83,0x7A,0x1C,0x00,0x0F,0x8D,0x21,0x5D,0x00,0x00,0xC3 };
// 3354C0, the state toggle (unit, state, on).
constexpr std::uint8_t ToggleStateBytes[]{
    0x48,0x89,0x5C,0x24,0x08,0x48,0x89,0x6C,0x24,0x18,0x48,0x89,0x74,0x24,0x20,0x57,
    0x48,0x83,0xEC,0x20,0x41,0x8B,0xE8,0x8B,0xDA,0x48,0x8B,0xF1,0x85,0xD2,0x78,0x38 };
// 34B440, the room of a unit.
constexpr std::uint8_t UnitRoomBytes[]{
    0x40,0x53,0x48,0x83,0xEC,0x20,0x48,0x8B,0xD9,0x48,0x85,0xC9,0x75,0x13 };
// 2EFC10, the level id of a room: null -> 0, else [room+18h] and the DRLG
// room's level id (jmp 360FC0).
constexpr std::uint8_t RoomLevelBytes[]{
    0x48,0x85,0xC9,0x75,0x03,0x33,0xC0,0xC3,0x48,0x8B,0x49,0x18,0xE9,0x9F,0x13,0x07,0x00 };
// 34C2C0, the death check (unit).
// 34A360, the inventory of a unit (null check, then the +A0/+98/+90 path).
constexpr std::uint8_t UnitInventoryBytes[]{
    0x48,0x89,0x5C,0x24,0x18,0x56,0x48,0x83,0xEC,0x20,0x48,0x8B,0xF1,0x48,0x85,0xC9,0x75,0x13 };
// 388C10, the first item of an inventory: checks the 01020304h header.
constexpr std::uint8_t FirstItemBytes[]{
    0x40,0x53,0x48,0x83,0xEC,0x20,0x48,0x8B,0xD9,0x48,0x85,0xC9,0x74,0x2E,0x81,0x39,0x04,0x03,0x02,0x01,0x74,0x1C };
// 38ABA0, the next item (ItemData+B0h).
constexpr std::uint8_t NextItemBytes[]{
    0x40,0x53,0x48,0x83,0xEC,0x20,0x48,0x8B,0xD9,0x48,0x85,0xC9,0x75,0x10 };
// 36EF50, the four-byte base item code of an item.
constexpr std::uint8_t ItemCodeBytes[]{
    0x48,0x89,0x5C,0x24,0x10,0x57,0x48,0x83,0xEC,0x20,0x48,0x8B,0xF9,0x48,0x85,0xC9,0x75,0x13 };
// 36CE70, the SetItems/UniqueItems row of an item (it checks the unit is an
// item: call 34B9D0, cmp eax, 4). 46D750 reads unique rows through it.
constexpr std::uint8_t ItemFileIndexBytes[]{
    0x40,0x53,0x48,0x83,0xEC,0x20,0x48,0x8B,0xD9,0x48,0x85,0xC9,0x75,0x13,0x88,0x4C,
    0x24,0x30,0x48,0x8D,0x4C,0x24,0x30,0xE8,0xB4,0xB7,0xFF,0xFF,0x84,0xC0,0x74,0x01,
    0xCC,0x48,0x8B,0xCB,0xE8,0x37,0xEB,0xFD,0xFF,0x83,0xF8,0x04 };
constexpr std::uint8_t UnitDeadBytes[]{
    0x48,0x83,0xEC,0x28,0x48,0x85,0xC9,0x75,0x1D,0x88,0x4C,0x24,0x30,0x48,0x8D,0x4C,
    0x24,0x30,0xE8,0x59,0x94,0xFF,0xFF,0x84,0xC0,0x74,0x4D,0xCC,0xB8,0x01,0x00,0x00 };
// 846170, a top-level panel by name: mov rdx, rcx / mov rcx, [2A40170h] (the UI
// manager) / test rcx, rcx / jnz 89F760 / xor eax, eax / ret.
constexpr std::uint8_t TopPanelBytes[]{
    0x48,0x8B,0xD1,0x48,0x8B,0x0D,0xF6,0x9F,0xBF,0x02,0x48,0x85,0xC9,0x0F,0x85,0xDD,
    0x95,0x05,0x00,0x33,0xC0,0xC3 };

// The specializations plugin's checked pieces, the same bytes:
constexpr std::uint8_t FindChildBytes[]{
    0x48,0x89,0x5C,0x24,0x10,0x48,0x89,0x74,0x24,0x18,0x57,0x48,0x83,0xEC,0x20,0x48,
    0x8B,0x59,0x58 };
constexpr std::uint8_t SetTextBytes[]{
    0x40,0x53,0x48,0x83,0xEC,0x20,0x4C,0x8B,0x42,0x08,0x48,0x8B,0xD9,0x48,0x8B,0x12,
    0x48,0x81,0xC1,0x88,0x00,0x00,0x00 };
constexpr std::uint8_t UiStateBytes[]{
    0x48,0x83,0xEC,0x28,0x48,0x8B,0x05,0x45,0xA1,0xBF,0x02,0x48,0x85,0xC0,0x75,0x1A,
    0x48,0x8D,0x4C,0x24,0x30,0x88,0x44,0x24,0x30,0xE8,0x82,0xD8,0xFF,0xFF,0x84,0xC0,
    0x74,0x01,0xCC,0x48,0x8B,0x05,0x26,0xA1,0xBF,0x02 };
constexpr std::uint8_t WidgetFocusFieldsBytes[]{
    0x48,0x39,0xB8,0x78,0x01,0x00,0x00,0x74,0x12,0xE8,0x84,0x06,0xFD,0xFF,0x48,0x39,
    0xB8,0x90,0x01,0x00,0x00,0x0F,0x85,0xB7,0x00,0x00,0x00 };
constexpr std::uint8_t WidgetChildrenBytes[]{
    0x48,0x8B,0x59,0x58,0x48,0x8B,0xF2,0x48,0x8B,0x41,0x60,0x48,0x8D,0x3C,0xC3,0x48,
    0x3B,0xDF,0x74,0x36,0x48,0x83,0x3B,0x00,0x75,0x14,0x48,0x8D,0x4C,0x24,0x30,0xC6,
    0x44,0x24,0x30,0x00,0xE8,0xF8,0xFA,0x98,0xFF,0x84,0xC0,0x74,0x01,0xCC,0x48,0x8B,
    0x0B,0x48,0x8B,0xD6,0x48,0x8B,0x49,0x08 };
constexpr std::uint8_t ButtonPressBytes[]{
    0x40,0x53,0x48,0x83,0xEC,0x20,0x48,0x8B,0xD9,0x48,0x81,0xC1,0x80,0x06,0x00,0x00,
    0x8B,0x93,0xEC,0x07,0x00,0x00,0xE8,0xE5,0x94,0xFE,0xFF,0x48,0x8B,0xCB,0xE8,0x3D,
    0x02,0x00,0x00,0x48,0x8B,0xCB,0xC6,0x83,0x79,0x06,0x00,0x00,0x01 };
constexpr std::uint8_t ButtonEnableBytes[]{
    0x40,0x53,0x48,0x83,0xEC,0x20,0x8B,0x81,0xF4,0x07,0x00,0x00,0x48,0x8B,0xD9,0xC6,
    0x81,0x7A,0x06,0x00,0x00,0x00,0x39,0x81,0x10,0x07,0x00,0x00 };
constexpr std::uint8_t PlaySoundByNameBytes[]{
    0x48,0x89,0x5C,0x24,0x08,0x48,0x89,0x74,0x24,0x10,0x57,0x48,0x83,0xEC,0x30,0x41,
    0x8B,0xD9,0x41,0x8B,0xF8,0x48,0x8B,0xF2 };
constexpr std::uint8_t ThunkHead[]{ 0xFF,0x25 };
constexpr std::uint8_t ThunkTail[]{ 0x90,0x90,0x90,0x90 };
constexpr std::uint8_t UnitTypeBytes[]{
    0x48,0x83,0xEC,0x28,0x48,0x85,0xC9,0x75,0x1D,0x88,0x4C,0x24,0x30,0x48,0x8D,0x4C,
    0x24,0x30,0xE8,0x39,0x9E,0xFF,0xFF,0x84,0xC0,0x74,0x01,0xCC,0xB8,0x06,0x00,0x00,
    0x00,0x48,0x83,0xC4,0x28,0xC3,0x8B,0x01,0x48,0x83,0xC4,0x28,0xC3 };
constexpr std::uint8_t UnitIdBytes[]{
    0x48,0x83,0xEC,0x28,0x48,0x85,0xC9,0x75,0x1D,0x88,0x4C,0x24,0x30,0x48,0x8D,0x4C,
    0x24,0x30,0xE8,0x39,0xCA,0xFF,0xFF,0x84,0xC0,0x74,0x01,0xCC,0xB8,0xFF,0xFF,0xFF,
    0xFF,0x48,0x83,0xC4,0x28,0xC3,0x8B,0x41,0x08,0x48,0x83,0xC4,0x28,0xC3 };
constexpr std::uint8_t LocalPlayerIndexBytes[]{ 0x8B,0x05,0x2E,0x84,0x99,0x02,0xC3 };
constexpr std::uint8_t PlayerFromIndexBytes[]{
    0x48,0x89,0x5C,0x24,0x08,0x57,0x48,0x83,0xEC,0x20,0x83,0xF9,0x08 };
constexpr std::uint8_t ServerUnitBytes[]{
    0x48,0x89,0x5C,0x24,0x08,0x48,0x89,0x74,0x24,0x18,0x57,0x48,0x83,0xEC,0x20,0x41,
    0x8B,0xD8,0x8B,0xF2,0x48,0x8B,0xF9,0x48,0x85,0xC9 };
constexpr std::uint8_t RegistryResetBytes[]{
    0x33,0xD2,0x48,0x8D,0x05,0x07,0x9A,0x62,0x02,0x8D,0x4A,0x20,0x0F,0x1F,0x40,0x00,
    0x48,0x89,0x10,0x48,0x89,0x50,0x08,0x48,0x89,0x50,0x10,0x48,0x8D,0x40,0x40,0x48,
    0x89,0x50,0xD8,0x48,0x89,0x50,0xE0,0x48,0x89,0x50,0xE8,0x48,0x89,0x50,0xF0,0x48,
    0x89,0x50,0xF8,0x48,0x83,0xE9,0x01,0x75,0xD7,0xC7,0x05,0xCD,0xA1,0x62,0x02,0x01,
    0x00,0x00,0x00,0xC3 };
constexpr std::uint8_t ClientLookupBytes[]{
    0x0F,0xB6,0xC1,0x48,0x8D,0x15,0x76,0x99,0x62,0x02,0x48,0x8B,0x04,0xC2,0x48,0x85,
    0xC0,0x74,0x10,0x39,0x08,0x74,0x0E,0x48,0x8B,0x80,0x30,0x06,0x00,0x00,0x48,0x85,
    0xC0,0x75,0xF0,0x33,0xC0,0xC3 };
constexpr std::uint8_t ClientPlayerBytes[]{
    0x44,0x8B,0x83,0x70,0x02,0x00,0x00,0x8B,0x93,0x6C,0x02,0x00,0x00,0x48,0x8B,0x8B,
    0xB0,0x02,0x00,0x00,0xE8,0x8C,0xB1,0x00,0x00 };

// callable: a native function the plugin only calls. Another plugin may hook it
// (its first bytes become a jump), which is fine: the call goes through that hook.
struct Witness {
    std::uint64_t       rva;
    const std::uint8_t* bytes;
    std::uint32_t       size;
    const char*         name;
    bool                callable;
};

template <std::size_t N>
constexpr auto W(std::uint64_t rva, const std::uint8_t (&bytes)[N], const char* name) noexcept -> Witness {
    return { rva, bytes, static_cast<std::uint32_t>(N), name, false };
}

template <std::size_t N>
constexpr auto WC(std::uint64_t rva, const std::uint8_t (&bytes)[N], const char* name) noexcept -> Witness {
    return { rva, bytes, static_cast<std::uint32_t>(N), name, true };
}

const std::array<Witness, 40> Witnesses{
    WC(AssignPropertyRva, AssignPropertyBytes, "property apply"),
    WC(LinkerLookupRva, LinkerLookupBytes, "name linker lookup"),
    WC(DataTablesRva, DataTablesBytes, "data tables"),
    WC(DataContextRva, DataContextBytes, "unit data context"),
    W(TimedEffectRva, TimedEffectBytes, "timed state effect"),
    WC(ListByStateRva, ListByStateBytes, "stat list of a state"),
    WC(UnlinkListRva, UnlinkListBytes, "stat list unlink"),
    WC(FreeListRva, FreeListBytes, "stat list free"),
    WC(ToggleStateRva, ToggleStateBytes, "state toggle"),
    WC(UnitRoomRva, UnitRoomBytes, "unit room"),
    WC(RoomLevelRva, RoomLevelBytes, "level of a room"),
    WC(UnitDeadRva, UnitDeadBytes, "unit death check"),
    WC(UnitInventoryRva, UnitInventoryBytes, "unit inventory"),
    WC(FirstItemRva, FirstItemBytes, "first inventory item"),
    WC(NextItemRva, NextItemBytes, "next inventory item"),
    WC(ItemCodeRva, ItemCodeBytes, "item code"),
    WC(ItemFileIndexRva, ItemFileIndexBytes, "item unique row"),
    WC(TopPanelRva, TopPanelBytes, "top-level panel lookup"),
    WC(FindChildRva, FindChildBytes, "child widget lookup"),
    W(ButtonPressRva, ButtonPressBytes, "button press and frame fields"),
    WC(ButtonEnableRva, ButtonEnableBytes, "button enable"),
    WC(PlaySoundByNameRva, PlaySoundByNameBytes, "play sound by name"),
    WC(SetTextRva, SetTextBytes, "text box text"),
    WC(UiStateRva, UiStateBytes, "UI state"),
    W(WidgetFocusFieldsRva, WidgetFocusFieldsBytes, "UI hover and focus fields"),
    W(WidgetChildrenRva, WidgetChildrenBytes, "widget children and names"),
    WC(ReadStatRva, ThunkHead, "stat read"),
    W(ReadStatRva + 6, ThunkTail, "stat read"),
    WC(ReadOwnStatRva, ThunkHead, "own stat read"),
    W(ReadOwnStatRva + 6, ThunkTail, "own stat read"),
    WC(WriteOwnStatRva, ThunkHead, "own stat write"),
    W(WriteOwnStatRva + 6, ThunkTail, "own stat write"),
    WC(UnitTypeRva, UnitTypeBytes, "unit type"),
    WC(UnitIdRva, UnitIdBytes, "unit id"),
    WC(LocalPlayerIndexRva, LocalPlayerIndexBytes, "local player index"),
    WC(PlayerFromIndexRva, PlayerFromIndexBytes, "local player"),
    WC(ServerUnitRva, ServerUnitBytes, "server unit lookup"),
    W(RegistryResetRva, RegistryResetBytes, "client registry reset"),
    W(ClientLookupRva, ClientLookupBytes, "client lookup"),
    W(ClientPlayerRva, ClientPlayerBytes, "client player fields"),
};

// ---------------------------------------------------------------------------
// Native functions
// ---------------------------------------------------------------------------

struct StringView {
    const char* data;
    std::size_t size;
};

// The property record handed to 3D4620.
struct NativeProperty {
    std::int16_t id;
    std::int16_t type;   // 0 property, 1 property group
    std::int32_t param;
    std::int32_t min;
    std::int32_t max;
};
static_assert(sizeof(NativeProperty) == 16);

// The block handed to 433D20 (see the top of the file).
struct TimedEffect {
    void*        source;
    void*        target;
    std::int32_t skill;
    std::int32_t level;
    std::int32_t frames;
    std::int32_t stat;
    std::int32_t value;
    std::int32_t state;
    void*        removeCallback;
};
static_assert(sizeof(TimedEffect) == 0x30);
static_assert(offsetof(TimedEffect, frames) == 0x18 && offsetof(TimedEffect, state) == 0x24
    && offsetof(TimedEffect, removeCallback) == 0x28);

using AssignPropertyFn = std::int64_t(__fastcall*)(std::uint32_t type, void* unit, void* target, void* mods,
    std::int32_t index, std::int32_t value, const NativeProperty* property, std::int32_t state, std::int32_t flags,
    void* list);
using LinkerLookupFn = std::int32_t(__fastcall*)(const void* linker, const char* name, std::int32_t mode);
using DataTablesFn   = void*(__fastcall*)(std::uint8_t context);
using DataContextFn  = std::uint8_t(__fastcall*)(void* unit);
using TimedEffectFn  = void*(__fastcall*)(const TimedEffect* effect);
using ListByStateFn  = void*(__fastcall*)(void* unit, std::int32_t state);
using UnlinkListFn   = void(__fastcall*)(void* unit, void* list);
using FreeListFn     = void(__fastcall*)(std::uint8_t context, void* list);
using ToggleStateFn  = void(__fastcall*)(void* unit, std::int32_t state, std::int32_t on);
using UnitRoomFn     = void*(__fastcall*)(void* unit);
using RoomLevelFn    = std::int32_t(__fastcall*)(void* room);
using UnitDeadFn     = std::int32_t(__fastcall*)(void* unit);
using UnitInventoryFn = void*(__fastcall*)(void* unit);
using NextItemFn     = void*(__fastcall*)(void* item);
using ItemCodeFn     = std::uint32_t(__fastcall*)(void* item);
using ItemIndexFn    = std::int32_t(__fastcall*)(void* item);
using TopPanelFn     = void*(__fastcall*)(const char* name);
using FindChildFn    = void*(__fastcall*)(void* widget, const char* name);
using ButtonEnableFn = void(__fastcall*)(void* button);
using PlaySoundFn    = std::int64_t(__fastcall*)(const char* name, void*, std::int32_t, std::int32_t);
using SetTextFn      = void*(__fastcall*)(void* textBox, const StringView* text);
using ReadStatFn     = std::int32_t(__fastcall*)(void* unit, std::int32_t stat, std::int32_t layer);
using WriteStatFn    = void(__fastcall*)(void* unit, std::int32_t stat, std::int32_t value, std::int32_t layer);
using UnitTypeFn     = std::uint32_t(__fastcall*)(void* unit);
using UnitIdFn       = std::uint32_t(__fastcall*)(void* unit);
using PlayerIndexFn  = std::int32_t(__fastcall*)();
using PlayerFromFn   = void*(__fastcall*)(std::int32_t index);
using ServerUnitFn   = void*(__fastcall*)(void* game, std::uint32_t type, std::uint32_t id);
using TypeDescFn     = void*(__fastcall*)(void* widget);
using SetVisibleFn   = void(__fastcall*)(void* widget, std::uint64_t visible);
using UiStateFn      = void*(__fastcall*)();

AssignPropertyFn AssignProperty{};
LinkerLookupFn   LinkerLookup{};
DataTablesFn     DataTables{};
DataContextFn    DataContext{};
TimedEffectFn    ApplyTimedEffect{};
ListByStateFn    ListByState{};
UnlinkListFn     UnlinkList{};
FreeListFn       FreeList{};
ToggleStateFn    ToggleState{};
UnitRoomFn       UnitRoom{};
RoomLevelFn      RoomLevel{};
UnitDeadFn       UnitDead{};
UnitInventoryFn  UnitInventory{};
NextItemFn       FirstItem{};
NextItemFn       NextItem{};
ItemCodeFn       ItemCode4{};
ItemIndexFn      ItemFileIndex{};
TopPanelFn       TopPanel{};
FindChildFn      FindChild{};
ButtonEnableFn   ButtonEnable{};
PlaySoundFn      PlaySound{};
SetTextFn        NativeSetText{};
ReadStatFn       ReadStat{};
ReadStatFn       ReadOwnStat{};
WriteStatFn      WriteOwnStat{};
UnitTypeFn       UnitType{};
UnitIdFn         UnitId{};
PlayerIndexFn    LocalPlayerIndex{};
PlayerFromFn     PlayerFromIndex{};
ServerUnitFn     ServerUnit{};
UiStateFn        UiState{};

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

template <typename T>
void Write(void* base, std::size_t offset, T value) noexcept {
    std::memcpy(static_cast<std::uint8_t*>(base) + offset, &value, sizeof(T));
}

auto LocalPlayer() noexcept -> void* {
    return PlayerFromIndex(LocalPlayerIndex());
}

// ---------------------------------------------------------------------------
// Services and state
// ---------------------------------------------------------------------------

const D2RL::PanelService*        PanelSvc{};
const D2RL::WidgetService*       WidgetSvc{};
const D2RL::SharedEventService*  EventSvc{};
const D2RL::NetworkService*      NetworkSvc{};
const D2RL::ThreadService*       ThreadSvc{};
const D2RL::LifecycleService*    LifecycleSvc{};
const D2RL::DataTableService*    TableSvc{};
const D2RL::LocalizationService* TextSvc{};
const D2RL::ResourceService*     ResourceSvc{};
const D2RL::InventoryService*    InventorySvc{};
const D2RL::ItemService*         ItemSvc{};

enum class PluginState { NotLoaded, DisabledByConfig, BadConfig, UnsupportedBuild, ServicesMissing, InstallFailed, Active };
PluginState       State{ PluginState::NotLoaded };
std::atomic<bool> Active{};

D2RL::Panels::RegistrationHandle   Panel{ D2RL::Panels::InvalidHandle };
D2RL::Network::ChannelHandle       Channel{ D2RL::Network::InvalidChannelHandle };
D2RL::SharedEvents::ListenerHandle ClickListener{ D2RL::SharedEvents::InvalidHandle };

constexpr std::uint16_t ChannelLocalId     = 1;
constexpr std::uint64_t CompatibilityToken = 0x415343454E443031ULL;  // "ASCEND01"
constexpr std::uint16_t RequestMessageId   = 1;

enum class Op : std::uint8_t { Hello = 0, Ascend = 1, Renounce = 2 };

struct Message {
    std::uint32_t playerId;
    std::uint8_t  op;
    std::uint8_t  index;  // Ascend: 0-based ascendancy
    std::uint16_t reserved;
};
static_assert(sizeof(Message) == 8);

// progress_stat bits.
constexpr std::uint32_t TierBits       = 0x1F;
constexpr std::uint32_t LichKingBit    = 0x20;
constexpr std::uint32_t AscendedBit    = 0x40;
constexpr std::size_t   TierCount      = 5;

// Counters, shown by the console command.
std::atomic<std::uint64_t> ClicksIgnored{};
std::atomic<std::uint64_t> RequestsSent{};
std::atomic<std::uint64_t> RequestsDropped{};
std::atomic<std::uint64_t> RequestsReceived{};
std::atomic<std::uint64_t> RequestsRefused{};
std::atomic<std::uint64_t> Ascended{};
std::atomic<std::uint64_t> Renounced{};
std::atomic<std::uint64_t> TiersGranted{};
std::atomic<std::uint64_t> LichKingGranted{};
std::atomic<std::uint64_t> KillCredits{};
std::atomic<std::uint64_t> KillCreditFailures{};
std::atomic<std::uint64_t> DeathsSeen{};
std::atomic<std::uint64_t> BonusApplied{};
std::atomic<std::uint64_t> BonusRemoved{};
std::atomic<std::uint64_t> PropertiesApplied{};
std::atomic<std::uint64_t> PropertiesUnknown{};
std::atomic<std::uint64_t> Migrations{};
std::atomic<std::uint64_t> SoulstonesTaken{};
std::atomic<std::uint64_t> TextOverflow{};
std::atomic<std::uint32_t> ConnectionNow{ static_cast<std::uint32_t>(D2RL::Network::ConnectionState::Disconnected) };

// ---------------------------------------------------------------------------
// Config
// ---------------------------------------------------------------------------

constexpr std::size_t MaxAscendancies    = 64;
constexpr std::size_t MaxModifiers       = 32;
constexpr std::size_t MaximumConfigBytes = 262'144;

// One property: code, optional [param], value or min..max. "class-skills" is
// the class skill property of the character's own class.
struct Modifier {
    std::string  code;
    std::int32_t param{};
    std::int32_t min{};
    std::int32_t max{};
    bool         classSkills{};
};

// A text: "@d2r:Key" (looked up) or plain text. A leading ^ marks an item
// tooltip string, whose lines are stored bottom-up; they are shown reversed.
struct TextRef {
    std::string text;
    bool        reversed{};
};

struct AscendancyConfig {
    std::string                                   name;
    std::string                                   title;
    bool                                          imageRight{ true };
    std::int32_t                                  legacyUnique{ -1 };
    std::array<std::vector<Modifier>, TierCount>  tiers;
    std::array<std::vector<TextRef>, TierCount>   texts;
};

struct TriggerConfig {
    std::string               name;
    std::int32_t              tier{};        // 1-5, 0 with lich_king
    bool                      lichKing{};
    std::vector<std::int32_t> monsters;
    std::int32_t              difficulty{ -1 };
    std::int32_t              allWithinFrames{};
    std::int32_t              minMaxHp{};    // 256ths; above it (lich_king)
};

struct CreditConfig {
    std::string               name;
    std::vector<std::int32_t> monsters;
    std::int32_t              difficulty{ -1 };
    std::int32_t              skill{};
    std::int32_t              level{ 1 };
    std::int32_t              state{ -1 };
    std::int32_t              value{};
    std::int32_t              valueAbove{};  // with maxhp_above
    std::int32_t              maxHpAbove{};  // 256ths; 0 off
};

struct Config {
    bool                          enabled{ true };
    std::int32_t                  ascendancyStat{ -1 };
    std::int32_t                  progressStat{ -1 };
    std::int32_t                  soulstoneStat{ -1 };
    std::int32_t                  orbStat{ -1 };
    std::int32_t                  bonusState{ -1 };
    std::string                   soulstoneItem{ "u19" };
    std::int32_t                  soulstoneCap{ 10 };
    std::int32_t                  soulstoneTier{ 4 };
    bool                          renounceClearsSoulstones{ true };
    bool                          renounceClearsLichKing{ true };
    std::vector<std::string>      classSkillProperties{ "ama", "sor", "nec", "pal", "bar", "dru", "ass", "war" };
    std::vector<Modifier>         lichKing;
    std::int32_t                  killCheckStat{ 508 };
    std::int32_t                  killCreditFrames{ 1500 };
    std::string                   legacyStoneItem{ "ast" };
    std::vector<std::int32_t>     legacyTierStats;     // tiers 2, 3, 4, 5 on the old stone
    std::int32_t                  legacySoulstoneStat{ -1 };
    std::int32_t                  legacyLichKingStat{ -1 };
    std::string                   tierHeader{ "@d2r:AscendancyTier" };
    std::vector<std::string>      nextTier;            // 5 texts, tier 1 unused
    std::string                   lichKingTip{ "@d2r:AscendancyLichKingTip" };
    std::string                   ascendLabel{ "@d2r:AscendancyAscend" };
    std::string                   renounceLabel{ "@d2r:AscendancyRenounce" };
    std::string                   confirmText{ "@d2r:AscendancyConfirm" };
    std::int32_t                  noticeSeconds{ 5 };
    std::string                   ascendSound{ "cursor_stat_point_one_hd" };
    std::string                   renounceSound{ "cursor_level_up" };
    std::vector<AscendancyConfig> ascendancies;
    std::vector<TriggerConfig>    triggers;
    std::vector<CreditConfig>     credits;
};

Config      Settings;
std::string ConfigProblems;

void Problem(std::string_view text) {
    if (!ConfigProblems.empty()) ConfigProblems += "; ";
    ConfigProblems.append(text);
}

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
        if (!ParseInt(item, 0, 65535, number)) return false;
        out.push_back(number);
    }
    return true;
}

// A list of quoted strings: ["a", "b\n c"]. Escapes: \" \\ \n \t.
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

// "code", "code=v", "code=min..max", "code[param]=v", "code[param]=min..max".
auto ParseModifier(std::string_view text, Modifier& out) -> bool {
    text = Trim(text);
    const std::size_t equals = text.find('=');
    std::string_view head  = Trim(text.substr(0, equals));
    std::string_view value = equals == std::string_view::npos ? std::string_view{ "1" } : Trim(text.substr(equals + 1));
    out = {};
    const std::size_t open = head.find('[');
    if (open != std::string_view::npos) {
        if (head.back() != ']') return false;
        if (!ParseInt(head.substr(open + 1, head.size() - open - 2), -2'147'483'647LL, 2'147'483'647LL, out.param)) {
            return false;
        }
        head = Trim(head.substr(0, open));
    }
    if (head.empty()) return false;
    out.code.assign(head);
    out.classSkills = out.code == "class-skills";
    const std::size_t dots = value.find("..");
    if (dots == std::string_view::npos) {
        if (!ParseInt(value, -2'147'483'647LL, 2'147'483'647LL, out.min)) return false;
        out.max = out.min;
        return true;
    }
    return ParseInt(value.substr(0, dots), -2'147'483'647LL, 2'147'483'647LL, out.min)
        && ParseInt(value.substr(dots + 2), -2'147'483'647LL, 2'147'483'647LL, out.max);
}

auto ParseModifiers(std::string_view value, std::vector<Modifier>& out) -> bool {
    std::vector<std::string> items;
    if (!ParseStringArray(value, items) || items.size() > MaxModifiers) return false;
    out.clear();
    for (const std::string& item : items) {
        Modifier modifier;
        if (!ParseModifier(item, modifier)) return false;
        out.push_back(std::move(modifier));
    }
    return true;
}

auto ParseTexts(std::string_view value, std::vector<TextRef>& out) -> bool {
    std::vector<std::string> items;
    if (!ParseStringArray(value, items)) return false;
    out.clear();
    for (std::string& item : items) {
        TextRef ref;
        if (!item.empty() && item.front() == '^') {
            ref.reversed = true;
            item.erase(0, 1);
        }
        ref.text = std::move(item);
        out.push_back(std::move(ref));
    }
    return true;
}

void ApplyConfigLine(std::string_view section, std::string_view key, std::string_view value) {
    char note[240];
    const auto bad = [&](const char* what) {
        std::snprintf(note, sizeof(note), "[%.*s] %.*s: %s", static_cast<int>(section.size()), section.data(),
            static_cast<int>(key.size()), key.data(), what);
        Problem(note);
    };
    const auto stat = [&](std::int32_t& out) {
        if (!ParseInt(value, -1, 65535, out)) bad("expected an itemstatcost row");
    };
    if (section == "ascendancies") {
        if (key == "enabled") {
            if (!ParseBool(value, Settings.enabled)) bad("expected true or false, default kept");
        } else if (key == "ascendancy_stat") { stat(Settings.ascendancyStat);
        } else if (key == "progress_stat") { stat(Settings.progressStat);
        } else if (key == "soulstone_stat") { stat(Settings.soulstoneStat);
        } else if (key == "orb_stat") { stat(Settings.orbStat);
        } else if (key == "bonus_state") {
            if (!ParseInt(value, -1, 65535, Settings.bonusState)) bad("expected a states.txt row");
        } else if (key == "soulstone_item") {
            if (!ParseString(value, Settings.soulstoneItem) || Settings.soulstoneItem.size() > 4) bad("expected an item code");
        } else if (key == "soulstone_cap") {
            if (!ParseInt(value, 1, 255, Settings.soulstoneCap)) bad("expected 1 to 255, default kept");
        } else if (key == "soulstone_tier") {
            if (!ParseInt(value, 0, 5, Settings.soulstoneTier)) bad("expected 0 to 5, default kept");
        } else if (key == "renounce_clears_soulstones") {
            if (!ParseBool(value, Settings.renounceClearsSoulstones)) bad("expected true or false, default kept");
        } else if (key == "renounce_clears_lich_king") {
            if (!ParseBool(value, Settings.renounceClearsLichKing)) bad("expected true or false, default kept");
        } else if (key == "class_skill_properties") {
            if (!ParseStringArray(value, Settings.classSkillProperties)) bad("expected a list of property codes");
        } else if (key == "lich_king") {
            if (!ParseModifiers(value, Settings.lichKing)) bad("expected a list of properties");
        } else if (key == "kill_check_stat") { stat(Settings.killCheckStat);
        } else if (key == "kill_credit_frames") {
            if (!ParseInt(value, 1, 1'000'000, Settings.killCreditFrames)) bad("expected 1 or more, default kept");
        } else if (key == "legacy_stone_item") {
            if (!ParseString(value, Settings.legacyStoneItem) || Settings.legacyStoneItem.size() > 4) bad("expected an item code");
        } else if (key == "legacy_tier_stats") {
            if (!ParseIntArray(value, Settings.legacyTierStats) || Settings.legacyTierStats.size() != 4) {
                bad("expected 4 itemstatcost rows (tiers 2, 3, 4, 5)");
                Settings.legacyTierStats.clear();
            }
        } else if (key == "legacy_soulstone_stat") { stat(Settings.legacySoulstoneStat);
        } else if (key == "legacy_lich_king_stat") { stat(Settings.legacyLichKingStat);
        } else if (key == "tier_header") {
            if (!ParseString(value, Settings.tierHeader)) bad("expected a quoted text");
        } else if (key == "next_tier") {
            if (!ParseStringArray(value, Settings.nextTier) || Settings.nextTier.size() != TierCount) {
                bad("expected 5 quoted texts (tier 1 to 5)");
                Settings.nextTier.clear();
            }
        } else if (key == "ascend_label") {
            if (!ParseString(value, Settings.ascendLabel)) bad("expected a quoted text");
        } else if (key == "lich_king_tip") {
            if (!ParseString(value, Settings.lichKingTip)) bad("expected a quoted text");
        } else if (key == "renounce_label") {
            if (!ParseString(value, Settings.renounceLabel)) bad("expected a quoted text");
        } else if (key == "confirm_text") {
            if (!ParseString(value, Settings.confirmText)) bad("expected a quoted text");
        } else if (key == "notice_seconds") {
            if (!ParseInt(value, 0, 600, Settings.noticeSeconds)) bad("expected 0 to 600, default kept");
        } else if (key == "ascend_sound") {
            if (!ParseString(value, Settings.ascendSound)) bad("expected a quoted sounds.txt name");
        } else if (key == "renounce_sound") {
            if (!ParseString(value, Settings.renounceSound)) bad("expected a quoted sounds.txt name");
        }
        return;
    }
    if (section == "ascendancy" && !Settings.ascendancies.empty()) {
        AscendancyConfig& a = Settings.ascendancies.back();
        if (key == "name") {
            if (!ParseString(value, a.name)) bad("expected a quoted name");
        } else if (key == "title") {
            if (!ParseString(value, a.title)) bad("expected a quoted text");
        } else if (key == "image_side") {
            std::string side;
            if (!ParseString(value, side) || (side != "left" && side != "right")) bad("expected \"left\" or \"right\"");
            else a.imageRight = side == "right";
        } else if (key == "legacy_unique") {
            if (!ParseInt(value, -1, 65535, a.legacyUnique)) bad("expected a uniqueitems.txt row");
        } else if (key.size() == 5 && key.substr(0, 4) == "tier" && key[4] >= '1' && key[4] <= '5') {
            if (!ParseModifiers(value, a.tiers[static_cast<std::size_t>(key[4] - '1')])) {
                bad("expected a list of properties: \"code=value\", \"code[param]=value\", \"code=min..max\"");
            }
        } else if (key.size() == 5 && key.substr(0, 4) == "text" && key[4] >= '1' && key[4] <= '5') {
            if (!ParseTexts(value, a.texts[static_cast<std::size_t>(key[4] - '1')])) bad("expected a list of quoted texts");
        }
        return;
    }
    if (section == "trigger" && !Settings.triggers.empty()) {
        TriggerConfig& t = Settings.triggers.back();
        if (key == "name") {
            if (!ParseString(value, t.name)) bad("expected a quoted name");
        } else if (key == "tier") {
            if (!ParseInt(value, 1, 5, t.tier)) bad("expected 1 to 5");
        } else if (key == "lich_king") {
            if (!ParseBool(value, t.lichKing)) bad("expected true or false");
        } else if (key == "monsters") {
            if (!ParseIntArray(value, t.monsters)) bad("expected a list of monstats ids");
        } else if (key == "difficulty") {
            if (!ParseInt(value, -1, 2, t.difficulty)) bad("expected -1 (any), 0, 1 or 2");
        } else if (key == "all_within_frames") {
            if (!ParseInt(value, 0, 1'000'000, t.allWithinFrames)) bad("expected 0 or more");
        } else if (key == "min_maxhp") {
            if (!ParseInt(value, 0, 2'147'483'647LL, t.minMaxHp)) bad("expected 0 to 2147483647 (256ths)");
        }
        return;
    }
    if (section == "kill_credit" && !Settings.credits.empty()) {
        CreditConfig& c = Settings.credits.back();
        if (key == "name") {
            if (!ParseString(value, c.name)) bad("expected a quoted name");
        } else if (key == "monsters") {
            if (!ParseIntArray(value, c.monsters)) bad("expected a list of monstats ids");
        } else if (key == "difficulty") {
            if (!ParseInt(value, -1, 2, c.difficulty)) bad("expected -1 (any), 0, 1 or 2");
        } else if (key == "skill") {
            if (!ParseInt(value, 0, 65535, c.skill)) bad("expected a skills.txt row");
        } else if (key == "level") {
            if (!ParseInt(value, 1, 65535, c.level)) bad("expected 1 or more");
        } else if (key == "state") {
            if (!ParseInt(value, 0, 65535, c.state)) bad("expected a states.txt row");
        } else if (key == "value") {
            if (!ParseInt(value, -2'147'483'647LL, 2'147'483'647LL, c.value)) bad("expected a number");
        } else if (key == "value_above") {
            if (!ParseInt(value, -2'147'483'647LL, 2'147'483'647LL, c.valueAbove)) bad("expected a number");
        } else if (key == "maxhp_above") {
            if (!ParseInt(value, 0, 2'147'483'647LL, c.maxHpAbove)) bad("expected 0 to 2147483647 (256ths)");
        }
    }
}

void ParseConfig(std::string_view text) {
    std::string_view section;
    std::string      pendingKey;
    std::string      pendingValue;
    bool             collecting = false;
    while (!text.empty()) {
        const std::size_t end = text.find('\n');
        const std::string_view line = Trim(StripComment(text.substr(0, end)));
        text = end == std::string_view::npos ? std::string_view{} : text.substr(end + 1);
        if (collecting) {
            pendingValue += ' ';
            pendingValue.append(line);
            if (ClosesArray(pendingValue)) {
                collecting = false;
                ApplyConfigLine(section, pendingKey, pendingValue);
            }
            continue;
        }
        if (line.empty()) continue;
        if (line.size() >= 4 && line.substr(0, 2) == "[[" && line.substr(line.size() - 2) == "]]") {
            section = Trim(line.substr(2, line.size() - 4));
            if (section == "ascendancy") {
                if (Settings.ascendancies.size() < MaxAscendancies) {
                    Settings.ascendancies.emplace_back();
                } else {
                    Problem("more than 64 [[ascendancy]] blocks; the extra ones are ignored");
                    section = "ignored";
                }
            } else if (section == "trigger") {
                Settings.triggers.emplace_back();
            } else if (section == "kill_credit") {
                Settings.credits.emplace_back();
            }
            continue;
        }
        if (line.front() == '[') {
            if (line.back() == ']') section = Trim(line.substr(1, line.size() - 2));
            continue;
        }
        const std::size_t equals = line.find('=');
        if (equals == std::string_view::npos) continue;
        const std::string_view key   = Trim(line.substr(0, equals));
        const std::string_view value = Trim(line.substr(equals + 1));
        if (!value.empty() && value.front() == '[' && !ClosesArray(value)) {
            collecting = true;
            pendingKey.assign(key);
            pendingValue.assign(value);
            continue;
        }
        ApplyConfigLine(section, key, value);
    }
}

// Checks the parsed config. Returns false when the plugin cannot run with it.
auto ValidateConfig() -> bool {
    bool ok = true;
    char note[240];
    for (const auto& [value, name] : { std::pair{ Settings.ascendancyStat, "ascendancy_stat" },
            std::pair{ Settings.progressStat, "progress_stat" }, std::pair{ Settings.soulstoneStat, "soulstone_stat" },
            std::pair{ Settings.orbStat, "orb_stat" }, std::pair{ Settings.bonusState, "bonus_state" } }) {
        if (value < 0) {
            std::snprintf(note, sizeof(note), "%s is missing", name);
            Problem(note);
            ok = false;
        }
    }
    if (Settings.ascendancies.empty()) {
        Problem("no [[ascendancy]] block");
        ok = false;
    }
    if (Settings.nextTier.size() != TierCount) Settings.nextTier.assign(TierCount, std::string{});
    for (std::size_t a = 0; a < Settings.ascendancies.size(); ++a) {
        AscendancyConfig& asc = Settings.ascendancies[a];
        if (asc.name.empty()) asc.name = "ascendancy " + std::to_string(a + 1);
        if (asc.title.empty()) asc.title = asc.name;
    }
    for (std::size_t i = 0; i < Settings.triggers.size(); ++i) {
        TriggerConfig& t = Settings.triggers[i];
        if (t.name.empty()) t.name = "trigger " + std::to_string(i + 1);
        if (t.monsters.empty() || (t.tier == 0 && !t.lichKing)) {
            std::snprintf(note, sizeof(note), "%s needs monsters and a tier (or lich_king = true); it is ignored",
                t.name.c_str());
            Problem(note);
            t.monsters.clear();
        }
    }
    for (std::size_t i = 0; i < Settings.credits.size(); ++i) {
        CreditConfig& c = Settings.credits[i];
        if (c.name.empty()) c.name = "kill credit " + std::to_string(i + 1);
        if (c.monsters.empty() || c.state < 0) {
            std::snprintf(note, sizeof(note), "%s needs monsters and a state; it is ignored", c.name.c_str());
            Problem(note);
            c.monsters.clear();
        }
    }
    return ok;
}

// The default config, identical to celestialrayone.ascendancies.toml.
constexpr char DefaultConfigToml[] =
    "# Ascendancies\n"
    "#\n"
    "# The ascendancy system: an \"Ascendancy\" entry on the character sheet opens the\n"
    "# Ascendancy window, one page per ascendancy (portrait, the five tiers, how to\n"
    "# reach the next tier). After the Orb of Creation is consumed (orb_stat at 1 or\n"
    "# more, set by the mod's softcode), Ascend picks the ascendancy on screen and\n"
    "# grants tier 1. The other tiers come by themselves (see [[trigger]]), in any\n"
    "# order. Renounce Ascendancy (with an \"are you sure\" step) takes the ascendancy,\n"
    "# its tiers and bonuses away and sets orb_stat back to 0.\n"
    "# A \"New ascension tier unlocked\" notice shows on the HUD for a few seconds after\n"
    "# a new tier; its button opens the window.\n"
    "#\n"
    "# Needs D2RLoader 1.3.1, on the host and every client, the same version.\n"
    "# Mouse and keyboard.\n"
    "#\n"
    "# How the bonuses work\n"
    "#   The saved stats only say which ascendancy and which tiers. On the host the\n"
    "#   plugin applies every unlocked tier's properties (below) to the character\n"
    "#   with the game's own property code, the way monprop.txt applies monster\n"
    "#   properties, into the stat list of bonus_state. Auras (aura-hidden) and\n"
    "#   procs (hit-skill, gethit-skill, att-skill) work as they do from an item.\n"
    "#   The list is rebuilt when a tier changes and after a resurrect. Nothing of it\n"
    "#   is saved: it is built again in every game.\n"
    "#\n"
    "# itemstatcost.txt rows (ascendancy_stat, progress_stat, soulstone_stat, orb_stat)\n"
    "#   - Saved = 1 and CSvBits above 0, CSvParam blank. Add new rows at the end of\n"
    "#     the file. The plugin log reports this check every time the tables load.\n"
    "#   - progress_stat: bit 0-4 = tiers 1-5, bit 5 = the Lich King bonus, bit 6 =\n"
    "#     the character has ascended at least once (the sheet entry stays a -).\n"
    "#\n"
    "# Property format (tier1 to tier5, lich_king)\n"
    "#   \"code=value\", \"code[param]=value\", \"code=min..max\", \"code[param]=min..max\":\n"
    "#   code is a properties.txt code (or a propertygroups.txt code), param, min and\n"
    "#   max are the cube row's mod param / mod min / mod max. \"class-skills\" is the\n"
    "#   class skill property of the character's own class (class_skill_properties).\n"
    "#\n"
    "# Text format (text1 to text5, and every text setting)\n"
    "#   \"@d2r:Key\" shows a string of the mod (any string file), plain text shows as\n"
    "#   written. A leading ^ (\"^@d2r:Key\") marks an item tooltip string: its lines\n"
    "#   are stored bottom-up, the window shows them top-down. Trailing note\n"
    "#   markers (*, **) are dropped.\n"
    "#\n"
    "# Window layout (data/global/ui/layouts/celestialrayone.ascendancies/AscendancyPanelhd.json)\n"
    "#   Built into the plugin; a mod can replace it. Every text in it must be a full\n"
    "#   reference such as @d2r:Key. Widgets the plugin uses: Name, Portraits\n"
    "#   (Portrait1 to PortraitN, one per [[ascendancy]], in order), the text columns\n"
    "#   L and R (slots L1, L2, ... and R1, R2, ..., each with the text boxes HeadOn,\n"
    "#   HeadOff, LineOn and LineOff), Footer, Action and ActionLabel, Confirm\n"
    "#   (ConfirmLine1-3, ConfirmYes, ConfirmNo). The text column is the side\n"
    "#   opposite image_side. A page's lines sit in the middle slots of its column.\n"
    "#   Click messages, PanelManager:OpenPanel:<text>: AscendanciesOpen,\n"
    "#   AscendanciesClose, AscendanciesPrevious, AscendanciesNext, AscendanciesAction,\n"
    "#   AscendanciesYes, AscendanciesNo, AscendanciesNotice.\n"
    "#\n"
    "# Character sheet (characterstatspanelhd.json): AscendancyButton, a ButtonWidget\n"
    "#   with the six frames of the Specialization button (+ then -), and\n"
    "#   AscendancyTitle. Hidden until the orb is consumed.\n"
    "# HUD (hudpanelhd.json): AscendancyNotice, with the button AscendancyNoticeButton.\n"
    "#\n"
    "# Old Ascendancy Stones\n"
    "#   When a character with no ascendancy enters a game holding an Ascendancy\n"
    "#   Stone (legacy_stone_item), the stone becomes the saved stats (the stone's\n"
    "#   uniqueitems row picks the ascendancy, legacy_tier_stats the tiers) and is\n"
    "#   removed. Once, by itself.\n"
    "#\n"
    "# Console command\n"
    "#   ascendancies   status, the row check, your ascendancy and tiers, counters.\n"
    "\n"
    "[ascendancies]\n"
    "\n"
    "# Master switch.\n"
    "enabled = true\n"
    "\n"
    "# itemstatcost.txt rows of the saved stats.\n"
    "ascendancy_stat = 1064\n"
    "progress_stat = 1065\n"
    "soulstone_stat = 1066\n"
    "orb_stat = 1067\n"
    "\n"
    "# states.txt row of the hidden state that holds the bonus list.\n"
    "bonus_state = 697\n"
    "\n"
    "# Primordial Soulstones: every one the character holds (inventory, cube,\n"
    "# stash, cursor) goes into soulstone_stat, up to soulstone_cap. At the cap the\n"
    "# character gets soulstone_tier, with no other tier needed (0: none).\n"
    "soulstone_item = \"u19\"\n"
    "soulstone_cap = 10\n"
    "soulstone_tier = 4\n"
    "\n"
    "# Renouncing always takes the ascendancy, its tiers and orb_stat. These take the\n"
    "# soulstone count and the Lich King bonus too.\n"
    "renounce_clears_soulstones = true\n"
    "renounce_clears_lich_king = true\n"
    "\n"
    "# The class skill property of each class, in charstats.txt class order.\n"
    "class_skill_properties = [\"ama\", \"sor\", \"nec\", \"pal\", \"bar\", \"dru\", \"ass\", \"war\"]\n"
    "\n"
    "# The Lich King bonus: these properties join the list once the character has\n"
    "# earned it (see the lich_king trigger).\n"
    "lich_king = [\"lich-king-bonus=1\"]\n"
    "\n"
    "# Kill credit ([[kill_credit]]): kill_check_stat on a timed state, for this many\n"
    "# frames (1500 = 60 seconds), as the Just Killed curses gave it.\n"
    "kill_check_stat = 508\n"
    "kill_credit_frames = 1500\n"
    "\n"
    "# Old Ascendancy Stones: the item, and the stone's stats for tiers 2, 3, 4, 5,\n"
    "# its soulstone count and the Lich King bonus.\n"
    "legacy_stone_item = \"ast\"\n"
    "legacy_tier_stats = [676, 677, 678, 680]\n"
    "legacy_soulstone_stat = 919\n"
    "legacy_lich_king_stat = 826\n"
    "\n"
    "# Window texts. %d in tier_header is the tier; %d in a next_tier text is the\n"
    "# soulstone count. next_tier is shown on the character's own page for the\n"
    "# first tier not unlocked yet (tier 1 comes with Ascend).\n"
    "tier_header = \"@d2r:AscendancyTier\"\n"
    "next_tier = [\"\", \"@d2r:AscendancyNextTier2\", \"@d2r:AscendancyNextTier3\", \"@d2r:AscendancyNextTier4\", \"@d2r:AscendancyNextTier5\"]\n"
    "# Shown in the same place once every tier is earned, until the Lich King bonus\n"
    "# is earned too. \"\" shows nothing.\n"
    "lich_king_tip = \"@d2r:AscendancyLichKingTip\"\n"
    "ascend_label = \"@d2r:AscendancyAscend\"\n"
    "renounce_label = \"@d2r:AscendancyRenounce\"\n"
    "confirm_text = \"@d2r:AscendancyConfirm\"\n"
    "\n"
    "# Seconds the \"New ascension tier unlocked\" notice stays (0: off).\n"
    "notice_seconds = 5\n"
    "\n"
    "# sounds.txt rows played on Ascend and on Renounce (the level-up sound).\n"
    "ascend_sound = \"cursor_stat_point_one_hd\"\n"
    "renounce_sound = \"cursor_level_up\"\n"
    "\n"
    "# ---------------------------------------------------------------------------\n"
    "# Ascendancies, in window order. The order is saved with every character\n"
    "# (ascendancy_stat): never reorder or remove a block, add new ones at the end.\n"
    "#   title         window title of the page\n"
    "#   image_side    the portrait's side; the text takes the other one\n"
    "#   legacy_unique uniqueitems.txt row of its old Ascendancy Stone\n"
    "#   tier1..tier5  the properties of each tier\n"
    "#   text1..text5  the lines of each tier in the window\n"
    "# ---------------------------------------------------------------------------\n"
    "\n"
    "[[ascendancy]]\n"
    "name = \"Battlemage\"\n"
    "title = \"@d2r:AscendancyNameBattlemage\"\n"
    "image_side = \"right\"\n"
    "legacy_unique = 1100\n"
    "tier1 = [\"class-skills=1\", \"str=60\", \"enr=60\"]\n"
    "tier2 = [\"class-skills=1\", \"strpercent=25\", \"enepercent=25\", \"ascendancy-tier-2=1\"]\n"
    "tier3 = [\"aura-hidden[787]=1\", \"deadly=25\", \"crush=25\", \"ascendancy-tier-3=1\"]\n"
    "tier4 = [\"ascendancy-tier-4=1\", \"life-on-striking=10\", \"manasteal=15\", \"aura-hidden[1508]=1\"]\n"
    "tier5 = [\"class-skills=3\", \"all-speeds=50\", \"ascendancy-tier-5=1\"]\n"
    "text1 = [\"@d2r:AscendancyBattlemageTier1\"]\n"
    "text2 = [\"@d2r:AscendancyBattlemageTier2\"]\n"
    "text3 = [\"^@d2r:battlemagetierone\", \"@d2r:AscendancyBattlemageTier3\"]\n"
    "text4 = [\"^@d2r:battlemagetiertwo\", \"@d2r:AscendancyBattlemageTier4\"]\n"
    "text5 = [\"@d2r:AscendancyBattlemageTier5\"]\n"
    "\n"
    "[[ascendancy]]\n"
    "name = \"Blademaster\"\n"
    "title = \"@d2r:AscendancyNameBlademaster\"\n"
    "image_side = \"left\"\n"
    "legacy_unique = 1102\n"
    "tier1 = [\"class-skills=1\", \"str=60\", \"dex=60\"]\n"
    "tier2 = [\"class-skills=1\", \"strpercent=25\", \"dexpercent=25\", \"ascendancy-tier-2=1\"]\n"
    "tier3 = [\"aura-hidden[789]=1\", \"red-dmg%=20\", \"ascendancy-tier-3=1\"]\n"
    "tier4 = [\"ascendancy-tier-4=1\", \"dmg-norm=40..50\", \"aura-hidden[1510]=1\"]\n"
    "tier5 = [\"class-skills=3\", \"hit-skill[127]=15..20\", \"ascendancy-tier-5=1\"]\n"
    "text1 = [\"@d2r:AscendancyBlademasterTier1\"]\n"
    "text2 = [\"@d2r:AscendancyBlademasterTier2\"]\n"
    "text3 = [\"^@d2r:blademastertierone\", \"@d2r:AscendancyBlademasterTier3\"]\n"
    "text4 = [\"^@d2r:blademastertiertwo\", \"@d2r:AscendancyBlademasterTier4\"]\n"
    "text5 = [\"@d2r:AscendancyBlademasterTier5\"]\n"
    "\n"
    "[[ascendancy]]\n"
    "name = \"Arcanist\"\n"
    "title = \"@d2r:AscendancyNameArcanist\"\n"
    "image_side = \"right\"\n"
    "legacy_unique = 1105\n"
    "tier1 = [\"class-skills=1\", \"dex=60\", \"enr=60\"]\n"
    "tier2 = [\"class-skills=1\", \"dexpercent=25\", \"enepercent=25\", \"ascendancy-tier-2=1\"]\n"
    "tier3 = [\"aura-hidden[792]=1\", \"all-speeds=30\", \"ascendancy-tier-3=1\"]\n"
    "tier4 = [\"ascendancy-tier-4=1\", \"oskill[487]=3\", \"skill[487]=17\", \"res-all-max=5\", \"aura-hidden[1513]=1\"]\n"
    "tier5 = [\"class-skills=3\", \"hit-skill[129]=5..20\", \"hit-skill[130]=5..20\", \"hit-skill[131]=5..20\", \"ascendancy-tier-5=1\"]\n"
    "text1 = [\"@d2r:AscendancyArcanistTier1\"]\n"
    "text2 = [\"@d2r:AscendancyArcanistTier2\"]\n"
    "text3 = [\"^@d2r:arcanisttierone\", \"@d2r:AscendancyArcanistTier3\"]\n"
    "text4 = [\"^@d2r:arcanisttiertwo\", \"@d2r:AscendancyArcanistTier4\"]\n"
    "text5 = [\"@d2r:AscendancyArcanistTier5\"]\n"
    "\n"
    "[[ascendancy]]\n"
    "name = \"Bloodmage\"\n"
    "title = \"@d2r:AscendancyNameBloodmage\"\n"
    "image_side = \"left\"\n"
    "legacy_unique = 1109\n"
    "tier1 = [\"class-skills=1\", \"str=45\", \"vit=45\", \"enr=45\"]\n"
    "tier2 = [\"class-skills=1\", \"strpercent=20\", \"vitpercent=20\", \"enepercent=20\", \"ascendancy-tier-2=1\"]\n"
    "tier3 = [\"aura-hidden[796]=1\", \"hp=200\", \"ascendancy-tier-3=1\"]\n"
    "tier4 = [\"ascendancy-tier-4=1\", \"red-dmg%=25\", \"balance2=50\", \"aura-hidden[1517]=1\"]\n"
    "tier5 = [\"class-skills=3\", \"hp%=50\", \"passive-summ-life=25\", \"ascendancy-tier-5=1\"]\n"
    "text1 = [\"@d2r:AscendancyBloodmageTier1\"]\n"
    "text2 = [\"@d2r:AscendancyBloodmageTier2\"]\n"
    "text3 = [\"^@d2r:bloodmagetierone\", \"@d2r:AscendancyBloodmageTier3\"]\n"
    "text4 = [\"^@d2r:bloodmagetiertwo\", \"@d2r:AscendancyBloodmageTier4\"]\n"
    "text5 = [\"@d2r:AscendancyBloodmageTier5\"]\n"
    "\n"
    "[[ascendancy]]\n"
    "name = \"Awakened\"\n"
    "title = \"@d2r:AscendancyNameAwakened\"\n"
    "image_side = \"right\"\n"
    "legacy_unique = 1101\n"
    "tier1 = [\"class-skills=1\", \"all-stats=30\"]\n"
    "tier2 = [\"class-skills=1\", \"all-stats-percent=15\", \"ascendancy-tier-2=1\"]\n"
    "tier3 = [\"aura-hidden[788]=1\", \"all-stats=30\", \"all-speeds=20\", \"ascendancy-tier-3=1\"]\n"
    "tier4 = [\"ascendancy-tier-4=1\", \"oskill[480]=3\", \"skill[480]=17\", \"res-pois-len=75\", \"aura-hidden[1509]=1\"]\n"
    "tier5 = [\"class-skills=3\", \"gethit-skill[815]=10..60\", \"ascendancy-tier-5=1\"]\n"
    "text1 = [\"@d2r:AscendancyAwakenedTier1\"]\n"
    "text2 = [\"@d2r:AscendancyAwakenedTier2\"]\n"
    "text3 = [\"^@d2r:awakenedtierone\", \"@d2r:AscendancyAwakenedTier3\"]\n"
    "text4 = [\"^@d2r:awakenedtiertwo\", \"@d2r:AscendancyAwakenedTier4\"]\n"
    "text5 = [\"@d2r:AscendancyAwakenedTier5\"]\n"
    "\n"
    "[[ascendancy]]\n"
    "name = \"Ironbolt\"\n"
    "title = \"@d2r:AscendancyNameIronbolt\"\n"
    "image_side = \"left\"\n"
    "legacy_unique = 1104\n"
    "tier1 = [\"class-skills=1\", \"str=60\", \"vit=60\"]\n"
    "tier2 = [\"class-skills=1\", \"strpercent=25\", \"vitpercent=25\", \"ascendancy-tier-2=1\"]\n"
    "tier3 = [\"aura-hidden[791]=1\", \"str=150\", \"ascendancy-tier-3=1\"]\n"
    "tier4 = [\"ascendancy-tier-4=1\", \"crossbow-reload-speed=20\", \"crossbow-reload-speed-limit=20\", \"att=500\", \"aura-hidden[1512]=1\"]\n"
    "tier5 = [\"class-skills=3\", \"hit-skill[426]=2..40\", \"oskill[433]=3\", \"skill[433]=22\", \"ascendancy-tier-5=1\"]\n"
    "text1 = [\"@d2r:AscendancyIronboltTier1\"]\n"
    "text2 = [\"@d2r:AscendancyIronboltTier2\"]\n"
    "text3 = [\"^@d2r:ironbolttierone\", \"@d2r:AscendancyIronboltTier3\"]\n"
    "text4 = [\"^@d2r:ironbolttiertwo\", \"@d2r:AscendancyIronboltTier4\"]\n"
    "text5 = [\"@d2r:AscendancyIronboltTier5\"]\n"
    "\n"
    "[[ascendancy]]\n"
    "name = \"Starborn\"\n"
    "title = \"@d2r:AscendancyNameStarborn\"\n"
    "image_side = \"right\"\n"
    "legacy_unique = 1108\n"
    "tier1 = [\"class-skills=1\", \"enr=75\"]\n"
    "tier2 = [\"class-skills=1\", \"enepercent=30\", \"ascendancy-tier-2=1\"]\n"
    "tier3 = [\"aura-hidden[795]=1\", \"mana=400\", \"ascendancy-tier-3=1\"]\n"
    "tier4 = [\"ascendancy-tier-4=1\", \"oskill[482]=3\", \"skill[482]=17\", \"all-speeds=25\", \"aura-hidden[1516]=1\"]\n"
    "tier5 = [\"class-skills=3\", \"oskill[479]=1\", \"allskills=1\", \"ascendancy-tier-5=1\"]\n"
    "text1 = [\"@d2r:AscendancyStarbornTier1\"]\n"
    "text2 = [\"@d2r:AscendancyStarbornTier2\"]\n"
    "text3 = [\"^@d2r:starborntierone\", \"@d2r:AscendancyStarbornTier3\"]\n"
    "text4 = [\"^@d2r:starborntiertwo\", \"@d2r:AscendancyStarbornTier4\"]\n"
    "text5 = [\"@d2r:AscendancyStarbornTier5\"]\n"
    "\n"
    "[[ascendancy]]\n"
    "name = \"Lifebreaker\"\n"
    "title = \"@d2r:AscendancyNameLifebreaker\"\n"
    "image_side = \"left\"\n"
    "legacy_unique = 1110\n"
    "tier1 = [\"class-skills=1\", \"vit=75\"]\n"
    "tier2 = [\"class-skills=1\", \"vitpercent=30\", \"ascendancy-tier-2=1\"]\n"
    "tier3 = [\"aura-hidden[797]=1\", \"hp%=30\", \"ascendancy-tier-3=1\"]\n"
    "tier4 = [\"ascendancy-tier-4=1\", \"red-dmg=50\", \"red-mag=50\", \"trap-limit=1\", \"aura-hidden[1518]=1\"]\n"
    "tier5 = [\"class-skills=3\", \"oskill[823]=10\", \"red-dmg%=15\", \"res-mag=15\", \"ascendancy-tier-5=1\"]\n"
    "text1 = [\"@d2r:AscendancyLifebreakerTier1\"]\n"
    "text2 = [\"@d2r:AscendancyLifebreakerTier2\"]\n"
    "text3 = [\"^@d2r:lifebreakertierone\", \"@d2r:AscendancyLifebreakerTier3\"]\n"
    "text4 = [\"^@d2r:lifebreakertiertwo\", \"@d2r:AscendancyLifebreakerTier4\"]\n"
    "text5 = [\"@d2r:AscendancyLifebreakerTier5\"]\n"
    "\n"
    "[[ascendancy]]\n"
    "name = \"Worldshaper\"\n"
    "title = \"@d2r:AscendancyNameWorldshaper\"\n"
    "image_side = \"right\"\n"
    "legacy_unique = 1111\n"
    "tier1 = [\"class-skills=1\", \"str=45\", \"dex=45\", \"vit=45\"]\n"
    "tier2 = [\"class-skills=1\", \"strpercent=20\", \"dexpercent=20\", \"vitpercent=20\", \"ascendancy-tier-2=1\"]\n"
    "tier3 = [\"aura-hidden[798]=1\", \"res-elem=50\", \"ascendancy-tier-3=1\"]\n"
    "tier4 = [\"ascendancy-tier-4=1\", \"spell-damage=50\", \"aura-hidden[1519]=1\"]\n"
    "tier5 = [\"class-skills=3\", \"hit-skill[824]=2..1\", \"ascendancy-tier-5=1\"]\n"
    "text1 = [\"@d2r:AscendancyWorldshaperTier1\"]\n"
    "text2 = [\"@d2r:AscendancyWorldshaperTier2\"]\n"
    "text3 = [\"^@d2r:worldshapertierone\", \"@d2r:AscendancyWorldshaperTier3\"]\n"
    "text4 = [\"^@d2r:worldshapertiertwo\", \"@d2r:AscendancyWorldshaperTier4\"]\n"
    "text5 = [\"@d2r:AscendancyWorldshaperTier5\"]\n"
    "\n"
    "[[ascendancy]]\n"
    "name = \"Soul Warden\"\n"
    "title = \"@d2r:AscendancyNameSoulWarden\"\n"
    "image_side = \"left\"\n"
    "legacy_unique = 1106\n"
    "tier1 = [\"class-skills=1\", \"vit=60\", \"enr=60\"]\n"
    "tier2 = [\"class-skills=1\", \"vitpercent=25\", \"enepercent=25\", \"ascendancy-tier-2=1\"]\n"
    "tier3 = [\"aura-hidden[793]=1\", \"enr=150\", \"ascendancy-tier-3=1\"]\n"
    "tier4 = [\"ascendancy-tier-4=1\", \"nofreeze=1\", \"abs-mag%=10\", \"aura-hidden[1514]=1\"]\n"
    "tier5 = [\"class-skills=3\", \"passive-summ-limit=1\", \"red-dmg=50\", \"red-mag=50\", \"ascendancy-tier-5=1\"]\n"
    "text1 = [\"@d2r:AscendancySoulWardenTier1\"]\n"
    "text2 = [\"@d2r:AscendancySoulWardenTier2\"]\n"
    "text3 = [\"^@d2r:soulwardentierone\", \"@d2r:AscendancySoulWardenTier3\"]\n"
    "text4 = [\"^@d2r:soulwardentiertwo\", \"@d2r:AscendancySoulWardenTier4\"]\n"
    "text5 = [\"@d2r:AscendancySoulWardenTier5\"]\n"
    "\n"
    "[[ascendancy]]\n"
    "name = \"Stance Dancer\"\n"
    "title = \"@d2r:AscendancyNameStanceDancer\"\n"
    "image_side = \"right\"\n"
    "legacy_unique = 1112\n"
    "tier1 = [\"class-skills=1\", \"str=45\", \"dex=45\", \"enr=45\"]\n"
    "tier2 = [\"class-skills=1\", \"strpercent=20\", \"dexpercent=20\", \"enepercent=20\", \"ascendancy-tier-2=1\"]\n"
    "tier3 = [\"aura-hidden[799]=1\", \"str-dex-enr=90\", \"ascendancy-tier-3=1\", \"stance-dance-control=1\"]\n"
    "tier4 = [\"ascendancy-tier-4=1\", \"strpercent=30\", \"dexpercent=30\", \"enepercent=30\"]\n"
    "tier5 = [\"class-skills=3\", \"swing2=50\", \"cast2=50\", \"heal-kill=100\", \"mana-kill=50\", \"ascendancy-tier-5=1\"]\n"
    "text1 = [\"@d2r:AscendancyStanceDancerTier1\"]\n"
    "text2 = [\"@d2r:AscendancyStanceDancerTier2\"]\n"
    "text3 = [\"^@d2r:stancedancertierone\", \"^@d2r:stancedancertieronetwo\", \"@d2r:AscendancyStanceDancerTier3\"]\n"
    "text4 = [\"^@d2r:stancedancertiertwo\", \"@d2r:AscendancyStanceDancerTier4\"]\n"
    "text5 = [\"@d2r:AscendancyStanceDancerTier5\"]\n"
    "\n"
    "[[ascendancy]]\n"
    "name = \"Nomad\"\n"
    "title = \"@d2r:AscendancyNameNomad\"\n"
    "image_side = \"left\"\n"
    "legacy_unique = 1103\n"
    "tier1 = [\"class-skills=1\", \"dex=60\", \"vit=60\"]\n"
    "tier2 = [\"class-skills=1\", \"dexpercent=25\", \"vitpercent=25\", \"ascendancy-tier-2=1\"]\n"
    "tier3 = [\"aura-hidden[790]=1\", \"move2=75\", \"dmg-bleed[25]=150..250\", \"ascendancy-tier-3=1\"]\n"
    "tier4 = [\"ascendancy-tier-4=1\", \"hp=300\", \"aura-hidden[1511]=1\"]\n"
    "tier5 = [\"class-skills=3\", \"gethit-skill[822]=20..1\", \"ascendancy-tier-5=1\"]\n"
    "text1 = [\"@d2r:AscendancyNomadTier1\"]\n"
    "text2 = [\"@d2r:AscendancyNomadTier2\"]\n"
    "text3 = [\"^@d2r:nomadtierone\", \"@d2r:AscendancyNomadTier3\"]\n"
    "text4 = [\"^@d2r:nomadtiertwo\", \"@d2r:AscendancyNomadTier4\"]\n"
    "text5 = [\"@d2r:AscendancyNomadTier5\"]\n"
    "\n"
    "[[ascendancy]]\n"
    "name = \"Sharpshooter\"\n"
    "title = \"@d2r:AscendancyNameSharpshooter\"\n"
    "image_side = \"right\"\n"
    "legacy_unique = 1107\n"
    "tier1 = [\"class-skills=1\", \"dex=75\"]\n"
    "tier2 = [\"class-skills=1\", \"dexpercent=30\", \"ascendancy-tier-2=1\"]\n"
    "tier3 = [\"aura-hidden[794]=1\", \"oskill[450]=3\", \"skill[450]=17\", \"dmg-min=30\", \"ascendancy-tier-3=1\"]\n"
    "tier4 = [\"ascendancy-tier-4=1\", \"move2=75\", \"oskill[431]=3\", \"skill[431]=17\", \"aura-hidden[1515]=1\"]\n"
    "tier5 = [\"class-skills=3\", \"hit-skill[373]=15..20\", \"ascendancy-tier-5=1\"]\n"
    "text1 = [\"@d2r:AscendancySharpshooterTier1\"]\n"
    "text2 = [\"@d2r:AscendancySharpshooterTier2\"]\n"
    "text3 = [\"^@d2r:sharpshootertierone\", \"@d2r:AscendancySharpshooterTier3\"]\n"
    "text4 = [\"^@d2r:sharpshootertiertwo\", \"@d2r:AscendancySharpshooterTier4\"]\n"
    "text5 = [\"@d2r:AscendancySharpshooterTier5\"]\n"
    "\n"
    "[[ascendancy]]\n"
    "name = \"Mana Warden\"\n"
    "title = \"@d2r:AscendancyNameManaWarden\"\n"
    "image_side = \"left\"\n"
    "legacy_unique = 1113\n"
    "tier1 = [\"class-skills=1\", \"dex=45\", \"vit=45\", \"enr=45\"]\n"
    "tier2 = [\"class-skills=1\", \"dexpercent=20\", \"vitpercent=20\", \"enepercent=20\", \"ascendancy-tier-2=1\"]\n"
    "tier3 = [\"aura-hidden[800]=1\", \"mana-on-striking=5\", \"regen-mana=100\", \"ascendancy-tier-3=1\"]\n"
    "tier4 = [\"ascendancy-tier-4=1\", \"oskill[469]=3\", \"skill[469]=17\", \"res-pois-max=10\", \"aura-hidden[1521]=1\"]\n"
    "tier5 = [\"class-skills=3\", \"gethit-skill[481]=20..50\", \"light=20\", \"res-pois=75\", \"ascendancy-tier-5=1\"]\n"
    "text1 = [\"@d2r:AscendancyManaWardenTier1\"]\n"
    "text2 = [\"@d2r:AscendancyManaWardenTier2\"]\n"
    "text3 = [\"^@d2r:manawardentierone\", \"@d2r:AscendancyManaWardenTier3\"]\n"
    "text4 = [\"^@d2r:manawardentiertwo\", \"@d2r:AscendancyManaWardenTier4\"]\n"
    "text5 = [\"@d2r:AscendancyManaWardenTier5\"]\n"
    "\n"
    "[[ascendancy]]\n"
    "name = \"Berserker\"\n"
    "title = \"@d2r:AscendancyNameBerserker\"\n"
    "image_side = \"right\"\n"
    "legacy_unique = 1114\n"
    "tier1 = [\"class-skills=1\", \"str=75\"]\n"
    "tier2 = [\"class-skills=1\", \"strpercent=30\", \"ascendancy-tier-2=1\"]\n"
    "tier3 = [\"aura-hidden[801]=1\", \"lifesteal=25\", \"dmg-max=30\", \"ascendancy-tier-3=1\"]\n"
    "tier4 = [\"ascendancy-tier-4=1\", \"curse-res=100\", \"aura-hidden[1522]=1\"]\n"
    "tier5 = [\"class-skills=3\", \"att-skill[825]=25..1\", \"ascendancy-tier-5=1\"]\n"
    "text1 = [\"@d2r:AscendancyBerserkerTier1\"]\n"
    "text2 = [\"@d2r:AscendancyBerserkerTier2\"]\n"
    "text3 = [\"^@d2r:berserkertierone\", \"@d2r:AscendancyBerserkerTier3\"]\n"
    "text4 = [\"^@d2r:berserkertiertwo\", \"@d2r:AscendancyBerserkerTier4\"]\n"
    "text5 = [\"@d2r:AscendancyBerserkerTier5\"]\n"
    "\n"
    "# ---------------------------------------------------------------------------\n"
    "# Triggers: tiers that come from boss kills, checked for every living player\n"
    "# in the level when the boss dies.\n"
    "#   monsters           monstats.txt ids (hcIdx)\n"
    "#   difficulty         -1 any, 0 Normal, 1 Nightmare, 2 Hell\n"
    "#   all_within_frames  every listed monster must die within this many frames,\n"
    "#                      the player in the level each time and not dying between\n"
    "#   lich_king = true   grants the Lich King bonus instead of a tier, when the\n"
    "#                      monster's maxhp (256ths) is above min_maxhp\n"
    "# ---------------------------------------------------------------------------\n"
    "\n"
    "[[trigger]]\n"
    "name = \"Nihlathak, Hell\"\n"
    "tier = 2\n"
    "monsters = [526]  # nihlathakboss\n"
    "difficulty = 2\n"
    "\n"
    "[[trigger]]\n"
    "name = \"Uber Tristram\"\n"
    "tier = 3\n"
    "monsters = [705, 704, 709]  # uberdiablo, ubermephisto, uberbaal\n"
    "difficulty = 2\n"
    "all_within_frames = 1500\n"
    "\n"
    "[[trigger]]\n"
    "name = \"Tier 5 map bosses\"\n"
    "tier = 5\n"
    "# mephistotheinvincible, diablotheinvincible, baaltheinvincible, thelichking,\n"
    "# korlictheeternal, talictheeternal, madawctheeternal, lucion, rathma\n"
    "monsters = [973, 974, 972, 980, 978, 976, 977, 1085, 1068]\n"
    "\n"
    "[[trigger]]\n"
    "name = \"The Lich King, players 8\"\n"
    "lich_king = true\n"
    "monsters = [980]  # thelichking\n"
    "min_maxhp = 2000000000\n"
    "\n"
    "# ---------------------------------------------------------------------------\n"
    "# Kill credit: what the Just Killed curses gave, for the Kill Ledger and\n"
    "# Rathma's Eternal Skull (the ascendancy tiers use the triggers above, not\n"
    "# this). Every living player in the level of the dead monster\n"
    "# gets state with kill_check_stat = value (value_above when the monster's\n"
    "# maxhp, in 256ths, is above maxhp_above) for kill_credit_frames. skill and\n"
    "# level are the old curse's.\n"
    "# ---------------------------------------------------------------------------\n"
    "\n"
    "[[kill_credit]]\n"
    "name = \"Lucion's Whisper\"\n"
    "monsters = [958]\n"
    "skill = 811\n"
    "level = 50\n"
    "state = 292\n"
    "value = 50\n"
    "\n"
    "[[kill_credit]]\n"
    "name = \"Diablo's Hellhound\"\n"
    "monsters = [956]\n"
    "skill = 811\n"
    "level = 51\n"
    "state = 292\n"
    "value = 51\n"
    "\n"
    "[[kill_credit]]\n"
    "name = \"Ancient Fallen Angel\"\n"
    "monsters = [957]\n"
    "skill = 811\n"
    "level = 52\n"
    "state = 292\n"
    "value = 52\n"
    "\n"
    "[[kill_credit]]\n"
    "name = \"Terror in the Shadows\"\n"
    "monsters = [959]\n"
    "skill = 811\n"
    "level = 53\n"
    "state = 292\n"
    "value = 53\n"
    "\n"
    "[[kill_credit]]\n"
    "name = \"Avatar of the Night\"\n"
    "monsters = [961]\n"
    "skill = 811\n"
    "level = 54\n"
    "state = 292\n"
    "value = 54\n"
    "\n"
    "[[kill_credit]]\n"
    "name = \"Herald of Doom\"\n"
    "monsters = [962]\n"
    "skill = 811\n"
    "level = 55\n"
    "state = 292\n"
    "value = 55\n"
    "\n"
    "[[kill_credit]]\n"
    "name = \"Glacial Behemoth\"\n"
    "monsters = [964]\n"
    "skill = 811\n"
    "level = 56\n"
    "state = 292\n"
    "value = 56\n"
    "\n"
    "[[kill_credit]]\n"
    "name = \"The Taskmaster\"\n"
    "monsters = [965]\n"
    "skill = 811\n"
    "level = 57\n"
    "state = 292\n"
    "value = 57\n"
    "\n"
    "[[kill_credit]]\n"
    "name = \"Baal, Disfigured Harbinger\"\n"
    "monsters = [968]\n"
    "skill = 811\n"
    "level = 58\n"
    "state = 292\n"
    "value = 58\n"
    "\n"
    "[[kill_credit]]\n"
    "name = \"Arbiter of Souls\"\n"
    "monsters = [970]\n"
    "skill = 811\n"
    "level = 59\n"
    "state = 292\n"
    "value = 59\n"
    "\n"
    "[[kill_credit]]\n"
    "name = \"Mephisto the Invincible\"\n"
    "monsters = [973]\n"
    "skill = 811\n"
    "level = 20\n"
    "state = 292\n"
    "value = 20\n"
    "\n"
    "[[kill_credit]]\n"
    "name = \"Diablo the Invincible\"\n"
    "monsters = [974]\n"
    "skill = 811\n"
    "level = 21\n"
    "state = 292\n"
    "value = 21\n"
    "\n"
    "[[kill_credit]]\n"
    "name = \"Baal the Invincible\"\n"
    "monsters = [972]\n"
    "skill = 811\n"
    "level = 22\n"
    "state = 292\n"
    "value = 22\n"
    "\n"
    "[[kill_credit]]\n"
    "name = \"Eternal Ancients\"\n"
    "monsters = [978, 976, 977]\n"
    "skill = 811\n"
    "level = 60\n"
    "state = 292\n"
    "value = 60\n"
    "\n"
    "[[kill_credit]]\n"
    "name = \"Lucion\"\n"
    "monsters = [1085]\n"
    "skill = 811\n"
    "level = 61\n"
    "state = 292\n"
    "value = 61\n"
    "\n"
    "[[kill_credit]]\n"
    "name = \"The Lich King\"\n"
    "monsters = [980]\n"
    "skill = 814\n"
    "level = 1\n"
    "state = 292\n"
    "value = 40\n"
    "value_above = 48\n"
    "maxhp_above = 2000000000\n"
    "\n"
    "[[kill_credit]]\n"
    "name = \"Rathma\"\n"
    "monsters = [1068]\n"
    "skill = 381\n"
    "level = 1\n"
    "state = 292\n"
    "value = 99\n"
    "value_above = 100\n"
    "maxhp_above = 2000000000\n"
    "\n"
    "[[kill_credit]]\n"
    "name = \"Kor'nac the Hollow Apostle\"\n"
    "monsters = [1079]\n"
    "skill = 382\n"
    "level = 1\n"
    "state = 294\n"
    "value = 109\n"
    "value_above = 110\n"
    "maxhp_above = 2000000000\n";

void ReadConfiguration() {
    if (!Context->EnsureConfig(DefaultConfigToml)) {
        Context->LogWarn("Ascendancies: the config file could not be created; using the defaults.");
        ParseConfig(DefaultConfigToml);
        return;
    }
    std::string buffer(MaximumConfigBytes, '\0');
    if (Context->ReadConfig(buffer.data(), static_cast<std::uint32_t>(buffer.size()), nullptr)) {
        buffer.resize(std::strlen(buffer.c_str()));
        ParseConfig(buffer);
    } else {
        Context->LogWarn("Ascendancies: the config file could not be read; using the defaults.");
        ParseConfig(DefaultConfigToml);
    }
}

auto Contains(const std::vector<std::int32_t>& list, std::int32_t value) noexcept -> bool {
    return std::find(list.begin(), list.end(), value) != list.end();
}

auto ItemCode(const std::string& code) noexcept -> std::uint32_t {
    char c[4]{ ' ', ' ', ' ', ' ' };
    for (std::size_t i = 0; i < code.size() && i < 4; ++i) c[i] = code[i];
    return D2RL::Items::MakeItemCode(c[0], c[1], c[2], c[3]);
}

// The character's own saved values.
struct Snapshot {
    std::int32_t  ascendancy{};  // 1-based, 0 none
    std::uint32_t progress{};
    std::int32_t  soulstones{};
    std::int32_t  orb{};
};

auto ReadSnapshot(void* unit) noexcept -> Snapshot {
    Snapshot snap{};
    snap.ascendancy = ReadOwnStat(unit, Settings.ascendancyStat, 0);
    snap.progress   = static_cast<std::uint32_t>(ReadOwnStat(unit, Settings.progressStat, 0));
    snap.soulstones = ReadOwnStat(unit, Settings.soulstoneStat, 0);
    snap.orb        = ReadOwnStat(unit, Settings.orbStat, 0);
    if (snap.ascendancy < 0 || static_cast<std::size_t>(snap.ascendancy) > Settings.ascendancies.size()) {
        snap.ascendancy = 0;
    }
    return snap;
}

// ---------------------------------------------------------------------------
// Host: players
// ---------------------------------------------------------------------------

struct PlayerRef {
    std::uint32_t id;
    void*         unit;
    void*         game;
};

constexpr std::size_t MaxPlayers        = 16;
constexpr std::size_t MaxClientsVisited = 256;

// Every player of the games this process hosts (empty on a TCP/IP client).
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

auto LevelOf(void* unit) noexcept -> std::int32_t {
    void* const room = UnitRoom(unit);
    return room != nullptr ? RoomLevel(room) : -1;
}

struct HostPlayer {
    void*              unit{};
    void*              game{};
    D2RL::PlayerHandle handle{ D2RL::InvalidPlayerHandle };
    bool               migrated{};
    bool               dead{};
    bool               rebuild{};      // rebuild the list (resurrect)
    bool               removing{};     // the list went off; the new one comes in a later frame
    std::uint32_t      removingFrame{};
    bool               handleLogged{};
    bool               migrateWarned{};
    std::uint32_t      migrateFrame{};
    std::uint64_t      appliedKey{ ~0ULL };
    std::uint32_t      lastScanFrame{};
    std::uint64_t      seenTick{};
    // One record per [[trigger]] with all_within_frames: the frame each listed
    // monster died with the player in its level (0 none).
    std::vector<std::vector<std::uint32_t>> combos;
};

std::mutex                                    HostMutex;
std::unordered_map<std::uint32_t, HostPlayer> HostPlayers;
std::uint64_t                                 HostTickCount{};

// ---------------------------------------------------------------------------
// Host: the bonus list
// ---------------------------------------------------------------------------

std::mutex                      UnknownMutex;
std::unordered_set<std::string> UnknownCodes;  // reported once each

void ReportUnknown(const std::string& code) {
    PropertiesUnknown.fetch_add(1);
    std::lock_guard lock(UnknownMutex);
    if (UnknownCodes.insert(code).second) {
        D2RL::LogWarnF(Context, "Ascendancies: \"%s\" is neither a properties.txt nor a propertygroups.txt code; "
            "it is skipped.", code.c_str());
    }
}

// The property record for a code: the Properties linker first, then the
// PropertyGroups linker, as the cube and monprop rows resolve theirs.
auto ResolveProperty(void* unit, const std::string& code, NativeProperty& out) noexcept -> bool {
    void* const tables = DataTables(DataContext(unit));
    if (tables == nullptr) return false;
    const void* const properties = Read<const void*>(tables, PropertiesLinkerOffset);
    const void* const groups     = Read<const void*>(tables, PropertyGroupsLinkerOffset);
    std::int32_t id = properties != nullptr ? LinkerLookup(properties, code.c_str(), 0) : -1;
    std::int16_t type = 0;
    if (id < 0 && groups != nullptr) {
        id   = LinkerLookup(groups, code.c_str(), 0);
        type = 1;
    }
    if (id < 0 || id > 32767) return false;
    out.id   = static_cast<std::int16_t>(id);
    out.type = type;
    return true;
}

void ApplyModifier(void* unit, const Modifier& modifier) {
    std::string code = modifier.code;
    if (modifier.classSkills) {
        const std::uint32_t playerClass = Read<std::uint32_t>(unit, UnitClassOffset);
        if (playerClass >= Settings.classSkillProperties.size()) return;
        code = Settings.classSkillProperties[playerClass];
    }
    NativeProperty property{};
    if (!ResolveProperty(unit, code, property)) {
        ReportUnknown(code);
        return;
    }
    property.param = modifier.param;
    property.min   = modifier.min;
    property.max   = modifier.max;
    AssignProperty(0, nullptr, unit, nullptr, 0, 0, &property, Settings.bonusState, 0x40, nullptr);
    PropertiesApplied.fetch_add(1);
}

auto HasBonusList(void* unit) noexcept -> bool {
    return ListByState(unit, Settings.bonusState) != nullptr;
}

void RemoveBonus(void* unit) noexcept {
    const std::uint8_t context = DataContext(unit);
    bool removed = false;
    for (int guard = 0; guard < 8; ++guard) {
        void* const list = ListByState(unit, Settings.bonusState);
        if (list == nullptr) break;
        if (!removed) ToggleState(unit, Settings.bonusState, 0);
        UnlinkList(unit, list);
        FreeList(context, list);
        removed = true;
    }
    if (removed) BonusRemoved.fetch_add(1);
}

void ApplyBonus(void* unit, const Snapshot& snap) {
    if (snap.ascendancy <= 0) return;
    const AscendancyConfig& asc = Settings.ascendancies[static_cast<std::size_t>(snap.ascendancy - 1)];
    for (std::size_t t = 0; t < TierCount; ++t) {
        if ((snap.progress & (1U << t)) == 0) continue;
        for (const Modifier& modifier : asc.tiers[t]) ApplyModifier(unit, modifier);
    }
    if ((snap.progress & LichKingBit) != 0) {
        for (const Modifier& modifier : Settings.lichKing) ApplyModifier(unit, modifier);
    }
    if (HasBonusList(unit)) ToggleState(unit, Settings.bonusState, 1);
    BonusApplied.fetch_add(1);
}

// What the list is built from: which ascendancy, its tiers and the class.
auto BonusKey(void* unit, const Snapshot& snap) noexcept -> std::uint64_t {
    if (snap.ascendancy <= 0) return 0;
    const std::uint64_t playerClass = Read<std::uint32_t>(unit, UnitClassOffset) & 0xFF;
    return (static_cast<std::uint64_t>(snap.ascendancy) << 16) | ((snap.progress & (TierBits | LichKingBit)) << 8)
        | playerClass | (1ULL << 40);
}

// ---------------------------------------------------------------------------
// Host: tiers
// ---------------------------------------------------------------------------

void WriteProgress(void* unit, std::uint32_t progress) noexcept {
    WriteOwnStat(unit, Settings.progressStat, static_cast<std::int32_t>(progress), 0);
}

// Grants tier (1-5) to a character with an ascendancy. Returns true when new.
auto GrantTier(void* unit, std::uint32_t playerId, std::int32_t tier, const char* why) noexcept -> bool {
    if (tier < 1 || tier > static_cast<std::int32_t>(TierCount)) return false;
    const Snapshot snap = ReadSnapshot(unit);
    if (snap.ascendancy <= 0) return false;
    const std::uint32_t bit = 1U << (tier - 1);
    if ((snap.progress & bit) != 0) return false;
    WriteProgress(unit, snap.progress | bit);
    TiersGranted.fetch_add(1);
    D2RL::LogInfoF(Context, "Ascendancies: player %u reached tier %d (%s).", playerId, tier, why);
    return true;
}

auto GrantLichKing(void* unit, std::uint32_t playerId) noexcept -> bool {
    const Snapshot snap = ReadSnapshot(unit);
    if (snap.ascendancy <= 0 || (snap.progress & LichKingBit) != 0) return false;
    WriteProgress(unit, snap.progress | LichKingBit);
    LichKingGranted.fetch_add(1);
    D2RL::LogInfoF(Context, "Ascendancies: player %u earned the Lich King bonus.", playerId);
    return true;
}

// Reaching the soulstone cap grants soulstone_tier (independent of the others).
void CheckSoulstoneTier(void* unit, std::uint32_t playerId) noexcept {
    if (Settings.soulstoneTier <= 0) return;
    const Snapshot snap = ReadSnapshot(unit);
    if (snap.soulstones >= Settings.soulstoneCap) (void)GrantTier(unit, playerId, Settings.soulstoneTier, "soulstones");
}

// ---------------------------------------------------------------------------
// Host: boss deaths
// ---------------------------------------------------------------------------

struct Death {
    void*         game{};
    std::uint32_t monsterId{};
    std::int32_t  classId{};
    std::int32_t  difficulty{};
    std::int32_t  maxHp{};
    std::uint32_t frame{};
    std::array<std::uint32_t, MaxPlayers> players{};  // alive and in the level when it died
    std::size_t   playerCount{};
};

constexpr std::size_t MaxDeaths = 32;
std::mutex                       DeathMutex;
std::array<Death, MaxDeaths>     DeathQueue{};
std::size_t                      DeathCount{};

auto MonsterMatters(std::int32_t classId) noexcept -> bool {
    for (const TriggerConfig& t : Settings.triggers) if (Contains(t.monsters, classId)) return true;
    for (const CreditConfig& c : Settings.credits) if (Contains(c.monsters, classId)) return true;
    return false;
}

void __cdecl ProcessDeathsTask(const D2RL::PluginContext* context, void*) noexcept;

// Server thread, observation only: who was there, and the boss's maximum life
// while it still exists. The work happens on the game thread.
void __cdecl OnMonsterDeath(const D2RL::PluginContext* context, const D2RL::Lifecycle::MonsterDeathEvent* event,
        void*) noexcept {
    if (context == nullptr || event == nullptr || !Active.load()
            || event->structSize < D2RL::Lifecycle::MonsterDeathEventRequiredSize) {
        return;
    }
    const auto classId = static_cast<std::int32_t>(event->monster.classId);
    if (!MonsterMatters(classId)) return;
    DeathsSeen.fetch_add(1);
    std::array<PlayerRef, MaxPlayers> players{};
    const std::size_t count = CollectPlayers(players);
    if (count == 0) return;
    Death death{};
    death.classId    = classId;
    death.monsterId  = event->monster.id;
    death.difficulty = static_cast<std::int32_t>(event->difficulty);
    for (std::size_t i = 0; i < count && death.game == nullptr; ++i) {
        void* const monster = ServerUnit(players[i].game, UnitTypeMonster, event->monster.id);
        if (monster != nullptr) {
            death.game  = players[i].game;
            death.maxHp = ReadStat(monster, StatMaxHp, 0);
        }
    }
    if (death.game == nullptr) death.game = players[0].game;
    death.frame = Read<std::uint32_t>(death.game, GameFrameOffset);
    for (std::size_t i = 0; i < count; ++i) {
        if (players[i].game != death.game || UnitDead(players[i].unit) != 0) continue;
        if (LevelOf(players[i].unit) != event->levelId) continue;
        death.players[death.playerCount++] = players[i].id;
    }
    if (death.playerCount == 0) return;
    {
        std::lock_guard lock(DeathMutex);
        if (DeathCount >= DeathQueue.size()) return;
        DeathQueue[DeathCount++] = death;
    }
    (void)ThreadSvc->runOnGameThread(context, &ProcessDeathsTask, nullptr);
}

// 433D20 jumps through a slot D2RCore fills.
auto TimedEffectReady() noexcept -> bool {
    const auto displacement = Read<std::int32_t>(reinterpret_cast<const void*>(Base), TimedEffectRva + 2);
    const auto slot = Base + TimedEffectRva + 6 + static_cast<std::intptr_t>(displacement);
    const auto target = Read<std::uintptr_t>(reinterpret_cast<const void*>(slot), 0);
    return target != 0 && target != ~static_cast<std::uintptr_t>(0);
}

void ApplyKillCredit(void* game, const Death& death, void* player) noexcept {
    if (!TimedEffectReady()) {
        KillCreditFailures.fetch_add(1);
        return;
    }
    void* const monster = ServerUnit(game, UnitTypeMonster, death.monsterId);
    for (const CreditConfig& credit : Settings.credits) {
        if (!Contains(credit.monsters, death.classId)) continue;
        if (credit.difficulty >= 0 && credit.difficulty != death.difficulty) continue;
        const bool above = credit.maxHpAbove > 0 && death.maxHp > credit.maxHpAbove;
        const TimedEffect effect{
            monster != nullptr ? monster : player, player, credit.skill, credit.level, Settings.killCreditFrames,
            Settings.killCheckStat, above ? credit.valueAbove : credit.value, credit.state, nullptr };
        (ApplyTimedEffect(&effect) != nullptr ? KillCredits : KillCreditFailures).fetch_add(1);
    }
}

void ApplyTriggers(const Death& death, void* player, std::uint32_t playerId, HostPlayer* host) noexcept {
    for (std::size_t i = 0; i < Settings.triggers.size(); ++i) {
        const TriggerConfig& trigger = Settings.triggers[i];
        const auto found = std::find(trigger.monsters.begin(), trigger.monsters.end(), death.classId);
        if (found == trigger.monsters.end()) continue;
        if (trigger.difficulty >= 0 && trigger.difficulty != death.difficulty) continue;
        if (trigger.lichKing) {
            if (death.maxHp > trigger.minMaxHp) (void)GrantLichKing(player, playerId);
            continue;
        }
        if (trigger.allWithinFrames <= 0) {
            (void)GrantTier(player, playerId, trigger.tier, trigger.name.c_str());
            continue;
        }
        if (host == nullptr) continue;
        if (host->combos.size() < Settings.triggers.size()) host->combos.resize(Settings.triggers.size());
        std::vector<std::uint32_t>& record = host->combos[i];
        record.resize(trigger.monsters.size(), 0);
        record[static_cast<std::size_t>(found - trigger.monsters.begin())] = death.frame == 0 ? 1 : death.frame;
        std::uint32_t earliest = 0xFFFFFFFFU;
        bool all = true;
        for (const std::uint32_t frame : record) {
            if (frame == 0) { all = false; break; }
            earliest = std::min(earliest, frame);
        }
        if (all && death.frame - earliest <= static_cast<std::uint32_t>(trigger.allWithinFrames)) {
            (void)GrantTier(player, playerId, trigger.tier, trigger.name.c_str());
            std::fill(record.begin(), record.end(), 0);
        }
    }
}

void ProcessDeaths() noexcept {
    std::array<Death, MaxDeaths> deaths{};
    std::size_t count = 0;
    {
        std::lock_guard lock(DeathMutex);
        count = DeathCount;
        std::copy_n(DeathQueue.begin(), count, deaths.begin());
        DeathCount = 0;
    }
    if (count == 0) return;
    std::array<PlayerRef, MaxPlayers> players{};
    const std::size_t playerCount = CollectPlayers(players);
    std::lock_guard lock(HostMutex);
    for (std::size_t d = 0; d < count; ++d) {
        const Death& death = deaths[d];
        for (std::size_t p = 0; p < death.playerCount; ++p) {
            void* unit = nullptr;
            void* game = nullptr;
            for (std::size_t k = 0; k < playerCount; ++k) {
                if (players[k].id == death.players[p]) {
                    unit = players[k].unit;
                    game = players[k].game;
                }
            }
            if (unit == nullptr || game != death.game) continue;
            ApplyKillCredit(game, death, unit);
            const auto it = HostPlayers.find(death.players[p]);
            ApplyTriggers(death, unit, death.players[p], it != HostPlayers.end() ? &it->second : nullptr);
        }
    }
}

void __cdecl ProcessDeathsTask(const D2RL::PluginContext* context, void*) noexcept {
    if (context != nullptr && Active.load()) ProcessDeaths();
}

// ---------------------------------------------------------------------------
// Host: items (migration and soulstones), game thread
// ---------------------------------------------------------------------------

// What the item service lists for a player: a copy of the fields used here.
struct ListedItem {
    D2RL::ItemHandle             handle;
    std::uint32_t                code;
    std::uint32_t                runtimeId;
    D2RL::Items::ItemContainer   container;
    std::int32_t                 page;
};

struct ItemListing {
    std::vector<ListedItem> items;
    std::uint32_t           infoSize{};
};

// The fields up to and including inventoryPage are all this plugin reads, so a
// loader whose ItemInfo is shorter than this SDK's still lists every item.
constexpr std::uint32_t ListedInfoSize = static_cast<std::uint32_t>(offsetof(D2RL::Items::ItemInfo, inventoryPage)
    + sizeof(std::int32_t));

auto __cdecl ListItem(const D2RL::PluginContext*, const D2RL::Items::ItemInfo* info, void* userData) noexcept
        -> D2RL::Inventory::IterationAction {
    auto* const listing = static_cast<ItemListing*>(userData);
    if (info == nullptr) return D2RL::Inventory::IterationAction::Continue;
    listing->infoSize = info->structSize;
    if (info->structSize >= ListedInfoSize && listing->items.size() < 4096) {
        listing->items.push_back({ info->handle, info->code, info->runtimeId, info->container, info->inventoryPage });
    }
    return D2RL::Inventory::IterationAction::Continue;
}

auto ListItems(const D2RL::PluginContext* context, D2RL::PlayerHandle player, ItemListing& listing)
        -> D2RL::Inventory::Result {
    const D2RL::Inventory::ItemFilter filter{
        .structSize    = D2RL::Inventory::ItemFilterSize,
        .flags         = 0,
        .containerMask = D2RL::Items::AllItemContainers,
        .reserved      = 0,
    };
    const D2RL::Inventory::Result result = InventorySvc->forEachInventoryItem(context, player, &filter, &ListItem,
        &listing);
    if (result != D2RL::Inventory::Result::Success) listing.items.clear();
    return result;
}

// Where a soulstone counts as the character's: inventory, Cube, personal
// stash, a custom page or the cursor.
auto Owned(D2RL::Items::ItemContainer container) noexcept -> bool {
    switch (container) {
    case D2RL::Items::ItemContainer::Inventory:
    case D2RL::Items::ItemContainer::Cube:
    case D2RL::Items::ItemContainer::PersonalStash:
    case D2RL::Items::ItemContainer::CustomPage:
    case D2RL::Items::ItemContainer::Cursor:
        return true;
    default:
        return false;
    }
}

// The character's own inventory list, read natively: every item it holds,
// wherever it is stored.
struct NativeItem {
    void*         item;
    std::uint32_t runtimeId;
    std::int32_t  record;
};

auto WalkInventory(void* unit, std::uint32_t code, std::vector<NativeItem>& found) noexcept -> std::size_t {
    void* const inventory = UnitInventory(unit);
    if (inventory == nullptr) return 0;
    std::size_t total = 0;
    for (void* item = FirstItem(inventory); item != nullptr && total < 4096; item = NextItem(item)) {
        ++total;
        if (ItemCode4(item) == code) found.push_back({ item, UnitId(item), ItemFileIndex(item) });
    }
    return total;
}

auto ContainerName(D2RL::Items::ItemContainer container) noexcept -> const char* {
    switch (container) {
    case D2RL::Items::ItemContainer::Equipment:     return "equipment";
    case D2RL::Items::ItemContainer::Cursor:        return "cursor";
    case D2RL::Items::ItemContainer::Belt:          return "belt";
    case D2RL::Items::ItemContainer::Inventory:     return "inventory";
    case D2RL::Items::ItemContainer::Cube:          return "cube";
    case D2RL::Items::ItemContainer::Trade:         return "trade";
    case D2RL::Items::ItemContainer::PersonalStash: return "stash";
    case D2RL::Items::ItemContainer::SharedStash:   return "shared stash";
    case D2RL::Items::ItemContainer::CustomPage:    return "custom page";
    case D2RL::Items::ItemContainer::Ground:        return "ground";
    default:                                        return "unknown";
    }
}

// The old Ascendancy Stone becomes the saved stats, once, when the character
// enters a game. The stone is found in the character's own inventory list
// (wherever it is stored), read there, and removed through the item service.
void Migrate(const D2RL::PluginContext* context, std::uint32_t playerId, HostPlayer& host) {
    std::vector<NativeItem> stones;
    const std::size_t nativeTotal = WalkInventory(host.unit, ItemCode(Settings.legacyStoneItem), stones);
    ItemListing listing;
    const D2RL::Inventory::Result listed = ListItems(context, host.handle, listing);
    if (!host.migrateWarned) {
        host.migrateWarned = true;
        std::array<std::uint32_t, 11> perContainer{};
        for (const ListedItem& item : listing.items) {
            const auto c = static_cast<std::uint32_t>(item.container);
            ++perContainer[c < perContainer.size() ? c : 0];
        }
        D2RL::LogInfoF(Context, "Ascendancies: player %u holds %zu items; the item service lists %zu (result %u, info "
            "size %u: inventory %u, cube %u, stash %u, custom page %u, cursor %u, equipment %u, belt %u). Ascendancy "
            "Stones held: %zu.", playerId, nativeTotal, listing.items.size(), static_cast<unsigned>(listed),
            listing.infoSize, perContainer[4], perContainer[5], perContainer[7], perContainer[9], perContainer[2],
            perContainer[1], perContainer[3], stones.size());
    }
    if (listed != D2RL::Inventory::Result::Success) {
        host.migrated = false;  // retried every second
        return;
    }
    if (stones.empty()) return;
    const Snapshot snap = ReadSnapshot(host.unit);
    if (snap.ascendancy > 0) {
        D2RL::LogWarnF(Context, "Ascendancies: player %u already has an ascendancy and still holds an Ascendancy "
            "Stone; the stone is left alone.", playerId);
        return;
    }
    for (const NativeItem& stone : stones) {
        std::size_t index = Settings.ascendancies.size();
        for (std::size_t a = 0; a < Settings.ascendancies.size(); ++a) {
            if (Settings.ascendancies[a].legacyUnique == stone.record) index = a;
        }
        if (index == Settings.ascendancies.size()) {
            D2RL::LogWarnF(Context, "Ascendancies: player %u holds a %s item whose uniqueitems row %d is no "
                "[[ascendancy]] legacy_unique; it is left alone.", playerId, Settings.legacyStoneItem.c_str(),
                stone.record);
            continue;
        }
        const ListedItem* listedStone = nullptr;
        for (const ListedItem& item : listing.items) {
            if (item.runtimeId == stone.runtimeId) listedStone = &item;
        }
        if (listedStone == nullptr) {
            D2RL::LogWarnF(Context, "Ascendancies: player %u's %s Ascendancy Stone (item %u) is in a place the item "
                "service does not list, so it cannot be removed; nothing changed.", playerId,
                Settings.ascendancies[index].name.c_str(), stone.runtimeId);
            return;
        }
        // Read before it is removed.
        std::array<std::int32_t, 4> tiers{};
        for (std::size_t i = 0; i < Settings.legacyTierStats.size() && i < tiers.size(); ++i) {
            tiers[i] = ReadStat(stone.item, Settings.legacyTierStats[i], 0);
        }
        const std::int32_t stoneSoulstones = Settings.legacySoulstoneStat >= 0
            ? ReadStat(stone.item, Settings.legacySoulstoneStat, 0) : 0;
        const std::int32_t stoneLichKing = Settings.legacyLichKingStat >= 0
            ? ReadStat(stone.item, Settings.legacyLichKingStat, 0) : 0;
        std::uint32_t progress = 1U | AscendedBit;  // tier 1
        for (std::size_t i = 0; i < tiers.size(); ++i) {
            if (tiers[i] > 0) progress |= 1U << (i + 1);
        }
        if (stoneLichKing > 0) progress |= LichKingBit;
        std::int32_t soulstones = std::clamp(stoneSoulstones, 0, Settings.soulstoneCap);
        if (Settings.soulstoneTier > 0 && (progress & (1U << (Settings.soulstoneTier - 1))) != 0) {
            soulstones = std::max(soulstones, Settings.soulstoneCap);
        }
        const D2RL::Items::Result destroyed = ItemSvc->destroyItem(context, host.handle, listedStone->handle,
            D2RL::Items::SocketedItemPolicy::DestroyContents);
        if (destroyed != D2RL::Items::Result::Success) {
            D2RL::LogWarnF(Context, "Ascendancies: player %u's Ascendancy Stone (%s) could not be removed (result "
                "%u); nothing changed.", playerId, ContainerName(listedStone->container),
                static_cast<unsigned>(destroyed));
            return;
        }
        WriteOwnStat(host.unit, Settings.ascendancyStat, static_cast<std::int32_t>(index + 1), 0);
        WriteProgress(host.unit, progress);
        WriteOwnStat(host.unit, Settings.soulstoneStat, soulstones, 0);
        WriteOwnStat(host.unit, Settings.orbStat, std::max(1, ReadOwnStat(host.unit, Settings.orbStat, 0)), 0);
        Migrations.fetch_add(1);
        D2RL::LogInfoF(Context, "Ascendancies: player %u's %s Ascendancy Stone (%s) became the saved ascendancy "
            "(tiers 0x%X, %d soulstones) and was removed.", playerId, Settings.ascendancies[index].name.c_str(),
            ContainerName(listedStone->container), static_cast<unsigned>(progress & (TierBits | LichKingBit)),
            soulstones);
        return;  // a character has one stone; any other is left alone
    }
}

// Primordial Soulstones the character holds go into the counter, up to the cap.
void TakeSoulstones(const D2RL::PluginContext* context, std::uint32_t playerId, HostPlayer& host) {
    std::int32_t count = ReadOwnStat(host.unit, Settings.soulstoneStat, 0);
    if (count >= Settings.soulstoneCap) {
        CheckSoulstoneTier(host.unit, playerId);
        return;
    }
    ItemListing listing;
    if (ListItems(context, host.handle, listing) != D2RL::Inventory::Result::Success) return;
    const std::uint32_t code = ItemCode(Settings.soulstoneItem);
    for (const ListedItem& item : listing.items) {
        if (count >= Settings.soulstoneCap) break;
        if (item.code != code || !Owned(item.container)) continue;
        if (ItemSvc->destroyItem(context, host.handle, item.handle, D2RL::Items::SocketedItemPolicy::DestroyContents)
                != D2RL::Items::Result::Success) {
            continue;
        }
        ++count;
        SoulstonesTaken.fetch_add(1);
        WriteOwnStat(host.unit, Settings.soulstoneStat, count, 0);
    }
    CheckSoulstoneTier(host.unit, playerId);
}

// ---------------------------------------------------------------------------
// Host: requests
// ---------------------------------------------------------------------------

struct Request {
    D2RL::Network::PeerHandle peer;
    Message                   message;
};

constexpr std::size_t            MaxRequests = 64;
std::mutex                       RequestMutex;
std::array<Request, MaxRequests> RequestQueue{};
std::size_t                      RequestCount{};

void Ascend(void* unit, std::uint32_t playerId, std::size_t index) {
    const Snapshot snap = ReadSnapshot(unit);
    if (snap.orb < 1 || snap.ascendancy > 0 || index >= Settings.ascendancies.size()) {
        RequestsRefused.fetch_add(1);
        return;
    }
    WriteOwnStat(unit, Settings.ascendancyStat, static_cast<std::int32_t>(index + 1), 0);
    WriteProgress(unit, (snap.progress & ~(TierBits)) | 1U | AscendedBit);
    Ascended.fetch_add(1);
    D2RL::LogInfoF(Context, "Ascendancies: player %u ascended as %s.", playerId, Settings.ascendancies[index].name.c_str());
    CheckSoulstoneTier(unit, playerId);
}

void Renounce(void* unit, std::uint32_t playerId) {
    const Snapshot snap = ReadSnapshot(unit);
    if (snap.ascendancy <= 0) {
        RequestsRefused.fetch_add(1);
        return;
    }
    std::uint32_t progress = AscendedBit | (snap.progress & LichKingBit);
    if (Settings.renounceClearsLichKing) progress &= ~LichKingBit;
    WriteOwnStat(unit, Settings.ascendancyStat, 0, 0);
    WriteProgress(unit, progress);
    if (Settings.renounceClearsSoulstones) WriteOwnStat(unit, Settings.soulstoneStat, 0, 0);
    WriteOwnStat(unit, Settings.orbStat, 0, 0);
    Renounced.fetch_add(1);
    D2RL::LogInfoF(Context, "Ascendancies: player %u renounced %s.", playerId,
        Settings.ascendancies[static_cast<std::size_t>(snap.ascendancy - 1)].name.c_str());
}

void ProcessRequests(const D2RL::PluginContext* context, const std::array<PlayerRef, MaxPlayers>& players,
        std::size_t playerCount) {
    std::array<Request, MaxRequests> requests{};
    std::size_t count = 0;
    {
        std::lock_guard lock(RequestMutex);
        count = RequestCount;
        std::copy_n(RequestQueue.begin(), count, requests.begin());
        RequestCount = 0;
    }
    for (std::size_t i = 0; i < count; ++i) {
        const Message& message = requests[i].message;
        D2RL::PlayerHandle sender = D2RL::InvalidPlayerHandle;
        if (NetworkSvc->getPeerPlayer(context, Channel, requests[i].peer, &sender) != D2RL::Network::Result::Success
                || sender == D2RL::InvalidPlayerHandle) {
            RequestsRefused.fetch_add(1);
            continue;
        }
        void* unit = nullptr;
        for (std::size_t k = 0; k < playerCount; ++k) {
            if (players[k].id == message.playerId) unit = players[k].unit;
        }
        if (unit == nullptr) {
            RequestsRefused.fetch_add(1);
            continue;
        }
        switch (static_cast<Op>(message.op)) {
        case Op::Hello: {
            std::lock_guard lock(HostMutex);
            HostPlayer& host = HostPlayers[message.playerId];
            host.handle = sender;
            break;
        }
        case Op::Ascend:
            Ascend(unit, message.playerId, message.index);
            break;
        case Op::Renounce:
            Renounce(unit, message.playerId);
            break;
        default:
            RequestsRefused.fetch_add(1);
            break;
        }
    }
}

// ---------------------------------------------------------------------------
// Host: the per-frame task
// ---------------------------------------------------------------------------

constexpr std::uint32_t SoulstoneScanFrames = 12;

void HostUpdate(const D2RL::PluginContext* context) {
    std::array<PlayerRef, MaxPlayers> players{};
    const std::size_t count = CollectPlayers(players);
    if (count == 0) {
        std::lock_guard lock(HostMutex);
        HostPlayers.clear();
        return;
    }
    ProcessRequests(context, players, count);
    ProcessDeaths();
    std::lock_guard lock(HostMutex);
    ++HostTickCount;
    for (std::size_t i = 0; i < count; ++i) {
        const std::uint32_t id   = players[i].id;
        void* const         unit = players[i].unit;
        HostPlayer& host = HostPlayers[id];
        if (host.unit != unit) {  // a new player, or the same id on a new unit
            const D2RL::PlayerHandle handle = host.handle;
            host = HostPlayer{};
            host.handle = handle;
            host.dead   = UnitDead(unit) != 0;
        }
        host.unit     = unit;
        host.game     = players[i].game;
        host.seenTick = HostTickCount;
        const bool dead = UnitDead(unit) != 0;
        if (dead && !host.dead) {
            for (auto& record : host.combos) std::fill(record.begin(), record.end(), 0);
        }
        if (!dead && host.dead) host.rebuild = true;  // resurrected: the aura events died with the character
        host.dead = dead;
        // A local game or a host playing alone: the local player's own handle,
        // without waiting for the plugin channel.
        if (host.handle == D2RL::InvalidPlayerHandle && count == 1) {
            D2RL::PlayerHandle local = D2RL::InvalidPlayerHandle;
            if (InventorySvc->getLocalPlayer(context, &local) == D2RL::Inventory::Result::Success) host.handle = local;
        }
        if (host.handle != D2RL::InvalidPlayerHandle && !host.handleLogged) {
            host.handleLogged = true;
            D2RL::LogInfoF(Context, "Ascendancies: player %u is known to the host; stone and soulstone checks run.", id);
        }
        if (host.handle != D2RL::InvalidPlayerHandle) {
            const std::uint32_t now = Read<std::uint32_t>(players[i].game, GameFrameOffset);
            if (!host.migrated && (host.migrateFrame == 0 || now - host.migrateFrame >= 25)) {
                host.migrated     = true;
                host.migrateFrame = now == 0 ? 1 : now;
                Migrate(context, id, host);
            }
            const std::uint32_t frame = Read<std::uint32_t>(players[i].game, GameFrameOffset);
            if (frame - host.lastScanFrame >= SoulstoneScanFrames) {
                host.lastScanFrame = frame;
                TakeSoulstones(context, id, host);
            }
        }
        if (dead) continue;
        const Snapshot      snap = ReadSnapshot(unit);
        const std::uint64_t key  = BonusKey(unit, snap);
        // A rebuild takes two frames: the state goes off first (the clients drop
        // their copy of the list), then the old list goes and the new one comes
        // with the state back on, so the clients receive the new stats. The old
        // list stays until the new one is in, so the character's totals never
        // drop for a frame (the Stance Dance plugin reads them every frame).
        const std::uint32_t gameFrame = Read<std::uint32_t>(players[i].game, GameFrameOffset);
        if (host.removing) {
            if (gameFrame == host.removingFrame) continue;  // the state went off this frame
            host.removing = false;
            RemoveBonus(unit);
            ApplyBonus(unit, snap);
            host.appliedKey = key;
            continue;
        }
        if (key == host.appliedKey && !host.rebuild) continue;
        host.rebuild = false;
        if (HasBonusList(unit)) {
            ToggleState(unit, Settings.bonusState, 0);
            host.removing      = true;
            host.removingFrame = gameFrame;
            continue;
        }
        ApplyBonus(unit, snap);
        host.appliedKey = key;
    }
    for (auto it = HostPlayers.begin(); it != HostPlayers.end();) {
        it = it->second.seenTick != HostTickCount ? HostPlayers.erase(it) : std::next(it);
    }
}

// The host task runs once per game update. It re-arms itself through a UI
// task, so it never queues itself on the game queue that is running it, and the
// UI task restarts it when it stops arriving (a session change drops queued game
// work). A TCP/IP client has no game thread: runOnGameThread is refused there.
std::atomic<std::uint64_t> HostChain{};
std::atomic<std::uint64_t> HostChainCounter{};
std::atomic<std::uint64_t> HostChainStartedMs{};
std::atomic<std::uint64_t> HostLastRunMs{};
std::atomic<std::uint64_t> HostRuns{};
std::atomic<bool>          HostUnavailable{};

void __cdecl HostTick(const D2RL::PluginContext* context, void* userData) noexcept;

void __cdecl HostRearm(const D2RL::PluginContext* context, void* userData) noexcept {
    const std::uint64_t id = static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(userData));
    if (context == nullptr || HostChain.load() != id || !Active.load()) return;
    if (ThreadSvc->runOnGameThread(context, &HostTick, userData) != D2RL::Threads::Result::Success) {
        std::uint64_t expected = id;
        (void)HostChain.compare_exchange_strong(expected, 0);
    }
}

void __cdecl HostTick(const D2RL::PluginContext* context, void* userData) noexcept {
    const std::uint64_t id = static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(userData));
    if (context == nullptr || HostChain.load() != id || !Active.load()) return;
    HostUpdate(context);
    HostRuns.fetch_add(1);
    HostLastRunMs.store(GetTickCount64());
    if (ThreadSvc->runOnUiThread(context, &HostRearm, userData) != D2RL::Threads::Result::Success) {
        std::uint64_t expected = id;
        (void)HostChain.compare_exchange_strong(expected, 0);
    }
}

void StartHostChain(const D2RL::PluginContext* context) noexcept {
    const std::uint64_t id = HostChainCounter.fetch_add(1) + 1;
    HostChain.store(id);
    HostChainStartedMs.store(GetTickCount64());
    const D2RL::Threads::Result result = ThreadSvc->runOnGameThread(context, &HostTick,
        reinterpret_cast<void*>(static_cast<std::uintptr_t>(id)));
    HostUnavailable.store(result == D2RL::Threads::Result::Unavailable);
    if (result != D2RL::Threads::Result::Success) {
        std::uint64_t expected = id;
        (void)HostChain.compare_exchange_strong(expected, 0);
    }
}

// UI thread, every update while in a game.
void EnsureHostChain(const D2RL::PluginContext* context) noexcept {
    const std::uint64_t now = GetTickCount64();
    if (now - HostChainStartedMs.load() < 1000) return;
    if (HostChain.load() != 0 && now - HostLastRunMs.load() < 1000) return;
    if (HostUnavailable.load() && now - HostChainStartedMs.load() < 10'000) return;  // a TCP/IP client
    StartHostChain(context);
}

void __cdecl HostKick(const D2RL::PluginContext* context, void*) noexcept {
    if (context != nullptr && Active.load()) HostUpdate(context);
}

void __cdecl OnHostMessage(const D2RL::PluginContext* context, D2RL::Network::ChannelHandle,
        D2RL::Network::PeerHandle peer, std::uint16_t messageId, const void* data, std::uint32_t size, void*) noexcept {
    if (context == nullptr || !Active.load()) return;
    if (messageId != RequestMessageId || data == nullptr || size != sizeof(Message)) {
        RequestsRefused.fetch_add(1);
        return;
    }
    Request request{ peer, {} };
    std::memcpy(&request.message, data, sizeof(Message));
    {
        std::lock_guard lock(RequestMutex);
        if (RequestCount >= RequestQueue.size()) {
            RequestsRefused.fetch_add(1);
            return;
        }
        RequestQueue[RequestCount++] = request;
    }
    RequestsReceived.fetch_add(1);
    (void)ThreadSvc->runOnGameThread(context, &HostKick, nullptr);
}

// ---------------------------------------------------------------------------
// Text (UI thread)
// ---------------------------------------------------------------------------

// "@namespace:Key" is looked up; anything else is shown as written. A key that
// is not found shows itself, so a typo is visible in game.
auto ResolveText(const std::string& value) -> std::string {
    if (value.size() < 2 || value[0] != '@' || TextSvc == nullptr) return value;
    const std::string key = value.substr(1);
    std::uint32_t required = 0;
    char          small[1024]{};
    const D2RL::Localization::Result result = TextSvc->getStringByKey(Context, key.c_str(), small, sizeof(small),
        &required);
    if (result == D2RL::Localization::Result::Success) return small;
    if (result == D2RL::Localization::Result::BufferTooSmall && required > sizeof(small) && required < 65'536) {
        std::string big(required, '\0');
        if (TextSvc->getStringByKey(Context, key.c_str(), big.data(), required, &required)
                == D2RL::Localization::Result::Success) {
            big.resize(std::strlen(big.c_str()));
            return big;
        }
    }
    return value;
}

// The window shows no notes, so the note markers some lines end with ("*",
// "**") are dropped, with the spaces before them, line by line.
auto StripNoteMarkers(const std::string& text) -> std::string {
    std::string clean;
    clean.reserve(text.size());
    std::size_t start = 0;
    while (start <= text.size()) {
        const std::size_t end = text.find('\n', start);
        std::string line = text.substr(start, end == std::string::npos ? std::string::npos : end - start);
        while (!line.empty() && (line.back() == '*' || line.back() == ' ' || line.back() == '\r')) line.pop_back();
        clean += line;
        if (end == std::string::npos) break;
        clean += '\n';
        start = end + 1;
    }
    return clean;
}

auto SplitLines(const std::string& text) -> std::vector<std::string> {
    std::vector<std::string> lines;
    std::size_t start = 0;
    while (start <= text.size()) {
        const std::size_t end = text.find('\n', start);
        std::string line = text.substr(start, end == std::string::npos ? std::string::npos : end - start);
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
        lines.push_back(std::move(line));
        if (end == std::string::npos) break;
        start = end + 1;
    }
    while (!lines.empty() && lines.back().empty()) lines.pop_back();
    while (!lines.empty() && lines.front().empty()) lines.erase(lines.begin());
    return lines;
}

auto WithNumber(std::string text, std::int64_t value) -> std::string {
    const std::size_t at = text.find("%d");
    if (at != std::string::npos) text.replace(at, 2, std::to_string(value));
    return text;
}

auto DecodeUtf8(const std::string& text, std::size_t i, std::uint32_t& cp) noexcept -> std::size_t {
    const auto lead = static_cast<unsigned char>(text[i]);
    std::size_t n = 1;
    if ((lead & 0xE0U) == 0xC0U) n = 2;
    else if ((lead & 0xF0U) == 0xE0U) n = 3;
    else if ((lead & 0xF8U) == 0xF0U) n = 4;
    if (n == 1 || i + n > text.size()) {
        cp = lead;
        return 1;
    }
    cp = n == 2 ? (lead & 0x1FU) : n == 3 ? (lead & 0x0FU) : (lead & 0x07U);
    for (std::size_t k = 1; k < n; ++k) cp = (cp << 6) | (static_cast<unsigned char>(text[i + k]) & 0x3FU);
    return n;
}

auto IsWide(std::uint32_t c) noexcept -> bool {
    return (c >= 0x1100 && c <= 0x115F) || (c >= 0x2E80 && c <= 0xA4CF) || (c >= 0xAC00 && c <= 0xD7A3)
        || (c >= 0xF900 && c <= 0xFAFF) || (c >= 0xFE30 && c <= 0xFE4F) || (c >= 0xFF00 && c <= 0xFF60)
        || (c >= 0xFFE0 && c <= 0xFFE6);
}

// Width of one character at $XMediumFontSize (28), in layout units, measured
// from the game's own rendering of this font (the specializations tooltip).
// Other sizes scale it.
auto GlyphWidth(std::uint32_t c) noexcept -> int {
    if (c == ' ') return 22;
    if (IsWide(c)) return 30;
    switch (c) {
    case 'i': case 'l': case 'j': case 't': case 'f': case 'r': case 'I': case '1':
    case '.': case ',': case '\'': case ':': case ';': case '!': case '|': case '(': case ')':
        return 18;
    case 'm': case 'w': case 'M': case 'W': case '%':
        return 32;
    default:
        return 21;
    }
}

// Chinese and Japanese punctuation that must not start a line: it stays at
// the end of the line before, even past the limit.
auto ClosesLine(std::uint32_t c) noexcept -> bool {
    switch (c) {
    case 0x3001: case 0x3002: case 0xFF0C: case 0xFF0E: case 0xFF1A: case 0xFF1B: case 0xFF01: case 0xFF1F:
    case 0xFF09: case 0x300D: case 0x300F: case 0x3011: case 0x3009: case 0x300B: case 0x30FC: case 0xFF5E:
        return true;
    default:
        return false;
    }
}

// Breaks one line into lines no wider than limit (units at 28 points): at
// spaces, and between Chinese, Japanese and Korean characters.
auto WrapLine(const std::string& line, int limit) -> std::vector<std::string> {
    struct Token { std::string bytes; int width{}; bool space{}; };
    std::vector<Token> tokens;
    for (std::size_t i = 0; i < line.size();) {
        std::uint32_t c = 0;
        const std::size_t n = DecodeUtf8(line, i, c);
        const int width = GlyphWidth(c);
        if (c == ' ') {
            tokens.push_back({ " ", width, true });
        } else if (ClosesLine(c) && !tokens.empty() && !tokens.back().space) {
            tokens.back().bytes.append(line, i, n);  // never begins a line
            tokens.back().width += width;
        } else if (IsWide(c) || tokens.empty() || tokens.back().space) {
            tokens.push_back({ line.substr(i, n), width, false });
        } else {
            tokens.back().bytes.append(line, i, n);
            tokens.back().width += width;
        }
        i += n;
    }
    std::vector<std::string> out;
    std::string current;
    int         width = 0;
    for (const Token& token : tokens) {
        if (token.space) {
            if (!current.empty()) {
                current += ' ';
                width += token.width;
            }
            continue;
        }
        if (!current.empty() && width + token.width > limit) {
            while (!current.empty() && current.back() == ' ') current.pop_back();
            out.push_back(current);
            current.clear();
            width = 0;
        }
        current += token.bytes;
        width += token.width;
    }
    while (!current.empty() && current.back() == ' ') current.pop_back();
    if (!current.empty() || out.empty()) out.push_back(current);
    return out;
}

// ---------------------------------------------------------------------------
// Native widgets (UI thread)
// ---------------------------------------------------------------------------

auto IsType(void* widget, const char* typeName) noexcept -> bool {
    if (widget == nullptr) return false;
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

void SetNativeVisible(void* widget, bool visible) noexcept {
    auto** table = Read<void**>(widget, 0);
    reinterpret_cast<SetVisibleFn>(table[SetVisibleSlot])(widget, visible ? 1U : 0U);
}

void SetNativeText(void* box, const std::string& text) noexcept {
    const StringView view{ text.c_str(), text.size() };
    NativeSetText(box, &view);
}

std::unordered_map<void*, std::uint8_t> NativeVisible;
std::unordered_map<void*, std::string>  NativeText;
std::unordered_map<void*, std::uint8_t> NativeEnabled;

void Show(void* widget, bool visible) noexcept {
    if (widget == nullptr) return;
    const std::uint8_t want = visible ? 1 : 0;
    const auto it = NativeVisible.find(widget);
    if (it != NativeVisible.end() && it->second == want) return;
    SetNativeVisible(widget, visible);
    NativeVisible[widget] = want;
}

void SetBoxText(void* box, const std::string& text) {
    if (box == nullptr) return;
    const auto it = NativeText.find(box);
    if (it != NativeText.end() && it->second == text) return;
    SetNativeText(box, text);
    NativeText[box] = text;
}

// A hidden button still takes the mouse: while hidden it is also disabled, so
// its spot takes no press, no sound and no click.
void EnableButton(void* button, bool enabled) noexcept {
    if (button == nullptr) return;
    const std::uint8_t want = enabled ? 1 : 0;
    const auto it = NativeEnabled.find(button);
    if (it != NativeEnabled.end() && it->second == want) return;
    if (enabled) {
        if (Read<std::uint8_t>(button, ButtonDisabledOffset) != 0) ButtonEnable(button);
    } else {
        Write<std::uint8_t>(button, ButtonDisabledOffset, 1);
    }
    NativeEnabled[button] = want;
}

auto WidgetName(void* widget) noexcept -> const char* {
    return Read<const char*>(widget, WidgetNameOffset);
}

auto Child(void* parent, const char* name) noexcept -> void* {
    return parent != nullptr ? FindChild(parent, name) : nullptr;
}

auto TextBox(void* widget) noexcept -> void* {
    return widget != nullptr && IsType(widget, "TextBoxWidget") ? widget : nullptr;
}

auto Button(void* widget) noexcept -> void* {
    return widget != nullptr && IsType(widget, "ButtonWidget") ? widget : nullptr;
}

auto HoveredWidget() noexcept -> void* {
    void* const ui = UiState();
    return ui != nullptr ? Read<void*>(ui, UiHoveredOffset) : nullptr;
}

// A text box (the chat line) has the keyboard: Space types, it does not close.
auto TypingText() noexcept -> bool {
    void* const ui = UiState();
    void* const focused = ui != nullptr ? Read<void*>(ui, UiFocusedOffset) : nullptr;
    return focused != nullptr && IsType(focused, "InputTextBoxWidget");
}

auto GameInFront() noexcept -> bool {
    DWORD process = 0;
    const HWND front = GetForegroundWindow();
    return front != nullptr && GetWindowThreadProcessId(front, &process) != 0 && process == GetCurrentProcessId();
}

// ---------------------------------------------------------------------------
// The window (UI thread)
// ---------------------------------------------------------------------------

constexpr std::size_t MaxSlots  = 64;
constexpr std::size_t SlotKinds = 4;
constexpr std::array<const char*, SlotKinds> SlotSuffix{ "HeadOn", "HeadOff", "LineOn", "LineOff" };
constexpr std::array<const char*, 2>         ColumnPrefix{ "L", "R" };

// Line widths for the wrap, in units at 28 points: a text slot is 784 units
// wide (20 of them kept free) at 24 points; the confirmation lines are 680
// wide at 26 points.
constexpr int LineLimit    = (764 * 28) / 24;
constexpr int ConfirmLimit = (660 * 28) / 26;

struct WindowWidgets {
    void*                window{};
    void*                name{};
    void*                action{};
    void*                actionLabel{};
    void*                footer{};
    void*                confirm{};
    std::array<void*, 5> confirmLines{};
    void*                yes{};
    void*                no{};
    std::vector<void*>   portraits;
    std::array<void*, 2> columnWidgets{};
    std::array<std::vector<std::array<void*, SlotKinds>>, 2> columns;
};

WindowWidgets Native;
std::size_t   Viewed{};          // 0-based
int           PendingOpenTries{};
bool          ConfirmOpen{};
std::uint64_t ContentKey{ ~0ULL };

void ResetWindowCaches() {
    Native = {};
    NativeVisible.clear();
    NativeText.clear();
    NativeEnabled.clear();
    ContentKey = ~0ULL;
}

auto WindowOpen() noexcept -> bool {
    if (Panel == D2RL::Panels::InvalidHandle) return false;
    D2RL::Panels::PanelInfo info{};
    info.structSize = D2RL::Panels::PanelInfoSize;
    return PanelSvc->getPanelInfo(Context, Panel, &info) == D2RL::Panels::Result::Success
        && info.presentationState == D2RL::Panels::PresentationState::Open;
}

void ResolveNative(void* window) {
    ResetWindowCaches();
    Native.window      = window;
    Native.name        = TextBox(Child(window, "Name"));
    Native.action      = Button(Child(window, "Action"));
    Native.actionLabel = TextBox(Child(window, "ActionLabel"));
    Native.footer      = TextBox(Child(window, "Footer"));
    Native.confirm     = Child(window, "Confirm");
    for (std::size_t i = 0; i < Native.confirmLines.size(); ++i) {
        const std::string name = "ConfirmLine" + std::to_string(i + 1);
        Native.confirmLines[i] = TextBox(Child(Native.confirm, name.c_str()));
    }
    Native.yes = Button(Child(Native.confirm, "ConfirmYes"));
    Native.no  = Button(Child(Native.confirm, "ConfirmNo"));
    void* const portraits = Child(window, "Portraits");
    for (std::size_t a = 0; a < Settings.ascendancies.size(); ++a) {
        const std::string name = "Portrait" + std::to_string(a + 1);
        Native.portraits.push_back(Child(portraits != nullptr ? portraits : window, name.c_str()));
    }
    // Two columns ("L", "R"), each a list of slot widgets ("L1", "L2", ...)
    // holding one text box per kind of line.
    for (std::size_t c = 0; c < 2; ++c) {
        void* const column = Child(window, ColumnPrefix[c]);
        Native.columnWidgets[c] = column;
        if (column == nullptr) continue;
        for (std::size_t k = 1; k <= MaxSlots; ++k) {
            const std::string slotName = std::string(ColumnPrefix[c]) + std::to_string(k);
            void* const slotWidget = Child(column, slotName.c_str());
            if (slotWidget == nullptr) break;
            std::array<void*, SlotKinds> slot{};
            for (std::size_t kind = 0; kind < SlotKinds; ++kind) slot[kind] = TextBox(Child(slotWidget, SlotSuffix[kind]));
            Native.columns[c].push_back(slot);
        }
    }
}

// The open window: an ancestor of the hovered widget, else a top-level panel.
auto LocateWindow() noexcept -> void* {
    void* w = HoveredWidget();
    for (int depth = 0; w != nullptr && depth < 64; ++depth) {
        const char* const name = WidgetName(w);
        if (name != nullptr && std::strcmp(name, PanelRootName) == 0) return w;
        w = Read<void*>(w, WidgetParentOffset);
    }
    return TopPanel(PanelRootName);
}

struct Line {
    std::size_t kind;  // index into SlotSuffix
    std::string text;
};

auto TierUnlocked(const Snapshot& snap, std::size_t index, std::size_t tier) noexcept -> bool {
    return snap.ascendancy == static_cast<std::int32_t>(index + 1) && (snap.progress & (1U << tier)) != 0;
}

// The page's lines: per tier its header and its texts. An empty line goes
// between the tiers only when the page still fits the column with them.
auto BuildLines(std::size_t index, const Snapshot& snap, std::size_t capacity) -> std::vector<Line> {
    const AscendancyConfig& asc = Settings.ascendancies[index];
    const std::string header = ResolveText(Settings.tierHeader);
    std::array<std::vector<Line>, TierCount> tiers;
    std::size_t used = 0;
    for (std::size_t t = 0; t < TierCount; ++t) {
        const bool on = TierUnlocked(snap, index, t);
        tiers[t].push_back({ on ? 0U : 1U, StripNoteMarkers(WithNumber(header, static_cast<std::int64_t>(t + 1))) });
        for (const TextRef& ref : asc.texts[t]) {
            std::vector<std::string> parts = SplitLines(StripNoteMarkers(ResolveText(ref.text)));
            if (ref.reversed) std::reverse(parts.begin(), parts.end());
            for (const std::string& part : parts) {
                for (std::string& wrapped : WrapLine(part, LineLimit)) tiers[t].push_back({ on ? 2U : 3U, std::move(wrapped) });
            }
        }
        used += tiers[t].size();
    }
    const bool gaps = used + (TierCount - 1) <= capacity;
    std::vector<Line> lines;
    for (std::size_t t = 0; t < TierCount; ++t) {
        if (t > 0 && gaps) lines.push_back({ SlotKinds, {} });
        for (Line& line : tiers[t]) lines.push_back(std::move(line));
    }
    return lines;
}

// The page's lines go in the middle slots of its column, so the text sits in
// the middle of the window's height like the portrait.
void FillColumns(std::size_t index, const Snapshot& snap) {
    const std::size_t column = Settings.ascendancies[index].imageRight ? 0 : 1;
    const std::size_t slots  = Native.columns[column].size();
    const std::vector<Line> lines = BuildLines(index, snap, slots);
    const std::size_t first = lines.size() < slots ? (slots - lines.size()) / 2 : 0;
    for (std::size_t c = 0; c < 2; ++c) {
        Show(Native.columnWidgets[c], c == column);
        if (c != column) continue;
        for (std::size_t k = 0; k < slots; ++k) {
            const bool        used = k >= first && k - first < lines.size() && lines[k - first].kind < SlotKinds;
            const std::size_t kind = used ? lines[k - first].kind : SlotKinds;
            for (std::size_t box = 0; box < SlotKinds; ++box) {
                void* const widget = Native.columns[c][k][box];
                if (widget == nullptr) continue;
                const bool show = used && kind == box;
                if (show) SetBoxText(widget, lines[k - first].text);
                Show(widget, show);
            }
        }
    }
    if (lines.size() > slots) TextOverflow.fetch_add(1);
}

// The footer on the character's own page: what earns the next tier, and once
// every tier is earned, how to earn the Lich King bonus until it is earned.
auto NextTierText(const Snapshot& snap) -> std::string {
    bool allTiers = true;
    for (std::size_t t = 0; t < TierCount; ++t) {
        if ((snap.progress & (1U << t)) != 0) continue;
        allTiers = false;
        if (Settings.nextTier[t].empty()) continue;
        return StripNoteMarkers(WithNumber(ResolveText(Settings.nextTier[t]), snap.soulstones));
    }
    if (allTiers && (snap.progress & LichKingBit) == 0 && !Settings.lichKingTip.empty()) {
        return StripNoteMarkers(ResolveText(Settings.lichKingTip));
    }
    return {};
}

void RefreshWindow() {
    if (!Active.load() || !WindowOpen()) return;
    void* const window = LocateWindow();
    if (window == nullptr) return;
    if (window != Native.window) ResolveNative(window);
    void* const player = LocalPlayer();
    if (player == nullptr || Settings.ascendancies.empty()) return;
    const Snapshot snap = ReadSnapshot(player);
    if (Viewed >= Settings.ascendancies.size()) Viewed = 0;
    const AscendancyConfig& asc = Settings.ascendancies[Viewed];
    const std::uint64_t key = (static_cast<std::uint64_t>(Viewed) << 48)
        | (static_cast<std::uint64_t>(snap.ascendancy & 0xFF) << 40)
        | (static_cast<std::uint64_t>(snap.progress & 0xFF) << 32);
    if (key != ContentKey) {
        ContentKey = key;
        FillColumns(Viewed, snap);
        SetBoxText(Native.name, StripNoteMarkers(ResolveText(asc.title)));
        for (std::size_t a = 0; a < Native.portraits.size(); ++a) Show(Native.portraits[a], a == Viewed);
    }
    const bool chosenPage = snap.ascendancy == static_cast<std::int32_t>(Viewed + 1);
    const std::string next = chosenPage ? NextTierText(snap) : std::string{};
    if (!next.empty()) SetBoxText(Native.footer, next);
    Show(Native.footer, !next.empty());
    const bool choose   = snap.orb >= 1 && snap.ascendancy == 0;
    const bool renounce = chosenPage;
    if (!renounce) ConfirmOpen = false;
    if (choose || renounce) {
        SetBoxText(Native.actionLabel, StripNoteMarkers(ResolveText(choose ? Settings.ascendLabel : Settings.renounceLabel)));
    }
    Show(Native.action, (choose || renounce) && !ConfirmOpen);
    Show(Native.actionLabel, (choose || renounce) && !ConfirmOpen);
    EnableButton(Native.action, (choose || renounce) && !ConfirmOpen);
    if (ConfirmOpen) {
        // The question's lines sit in the middle of the dialog's line boxes.
        std::vector<std::string> wrapped;
        for (const std::string& part : SplitLines(StripNoteMarkers(ResolveText(Settings.confirmText)))) {
            for (std::string& line : WrapLine(part, ConfirmLimit)) wrapped.push_back(std::move(line));
        }
        const std::size_t boxes = Native.confirmLines.size();
        const std::size_t first = wrapped.size() < boxes ? (boxes - wrapped.size()) / 2 : 0;
        for (std::size_t i = 0; i < boxes; ++i) {
            const bool used = i >= first && i - first < wrapped.size();
            SetBoxText(Native.confirmLines[i], used ? wrapped[i - first] : std::string{});
        }
    }
    Show(Native.confirm, ConfirmOpen);
    EnableButton(Native.yes, ConfirmOpen);
    EnableButton(Native.no, ConfirmOpen);
}

// ---------------------------------------------------------------------------
// The character sheet entry and the HUD notice (UI thread)
// ---------------------------------------------------------------------------

std::unordered_map<void*, int> SheetButtonLook;

// look: first frame of the look to show (0 the +, 3 the -), or -1 for none.
void UpdateSheet(const Snapshot& snap) noexcept {
    void* const panel = TopPanel("CharacterStatsPanel");
    if (panel == nullptr) return;
    void* const button = Button(Child(panel, "AscendancyButton"));
    void* const title  = Child(panel, "AscendancyTitle");
    int look = -1;
    if (snap.orb >= 1 && snap.ascendancy == 0) look = 0;
    else if (snap.ascendancy > 0 || (snap.progress & AscendedBit) != 0) look = 3;
    if (title != nullptr) Show(title, look >= 0);
    if (button == nullptr) return;
    const auto it = SheetButtonLook.find(button);
    if (it != SheetButtonLook.end() && it->second == look) return;
    SheetButtonLook[button] = look;
    if (look < 0) {
        Show(button, false);
        Write<std::uint8_t>(button, ButtonDisabledOffset, 1);
        return;
    }
    Write<std::uint32_t>(button, ButtonNormalFrameOffset, static_cast<std::uint32_t>(look));
    Write<std::uint32_t>(button, ButtonHoveredFrameOffset, static_cast<std::uint32_t>(look + 1));
    Write<std::uint32_t>(button, ButtonPressedFrameOffset, static_cast<std::uint32_t>(look + 2));
    Write<std::uint32_t>(button, ButtonDisabledFrameOffset, static_cast<std::uint32_t>(look));
    if (Read<std::uint8_t>(button, ButtonPressedOffset) == 0) {
        Write<std::uint32_t>(button, ButtonShownFrameOffset, static_cast<std::uint32_t>(look));
    }
    if (Read<std::uint8_t>(button, ButtonDisabledOffset) != 0) ButtonEnable(button);
    Show(button, true);
}

using WidgetHandle = D2RL::Widgets::WidgetHandle;

WidgetHandle  HudPanelHandle{ D2RL::Widgets::InvalidHandle };
WidgetHandle  NoticeHandle{ D2RL::Widgets::InvalidHandle };
WidgetHandle  NoticeButtonHandle{ D2RL::Widgets::InvalidHandle };
int           NoticeShown{ -1 };
std::uint64_t NoticeUntilMs{};
std::uint64_t NoticeLookupMs{};
std::uint32_t NoticeLookups{};
std::uint64_t NoticeApplyMs{};
std::int64_t  SeenTiers{ -1 };  // tier bits the notice already knows about
std::int64_t  SeenOrb{ -1 };    // the orb stat the window already knows about

void UpdateNotice(const Snapshot& snap, std::uint64_t now) {
    const auto tiers = static_cast<std::int64_t>(snap.progress & TierBits & ~1U);  // tier 1 comes from Ascend
    if (SeenTiers < 0 || snap.ascendancy == 0) {
        SeenTiers = tiers;
    } else if (tiers != SeenTiers) {
        if ((tiers & ~SeenTiers) != 0 && Settings.noticeSeconds > 0 && !WindowOpen()) {
            NoticeUntilMs = now + static_cast<std::uint64_t>(Settings.noticeSeconds) * 1000ULL;
        }
        SeenTiers = tiers;
    }
    if (NoticeHandle == D2RL::Widgets::InvalidHandle && now - NoticeLookupMs >= 500) {
        NoticeLookupMs = now;
        if (HudPanelHandle == D2RL::Widgets::InvalidHandle
                && WidgetSvc->findPanel(Context, "HUDPanel", &HudPanelHandle) != D2RL::Widgets::Result::Success) {
            HudPanelHandle = D2RL::Widgets::InvalidHandle;
        }
        if (HudPanelHandle != D2RL::Widgets::InvalidHandle) {
            if (WidgetSvc->findWidget(Context, HudPanelHandle, "AscendancyNotice", &NoticeHandle)
                    != D2RL::Widgets::Result::Success) {
                NoticeHandle = D2RL::Widgets::InvalidHandle;
            } else if (WidgetSvc->findWidget(Context, NoticeHandle, "AscendancyNoticeButton", &NoticeButtonHandle)
                    != D2RL::Widgets::Result::Success) {
                NoticeButtonHandle = D2RL::Widgets::InvalidHandle;
            }
        }
        if (NoticeHandle == D2RL::Widgets::InvalidHandle && ++NoticeLookups == 20) {
            Context->LogWarn("Ascendancies: hudpanelhd.json has no AscendancyNotice widget, so there is no tier notice.");
        }
    }
    if (NoticeHandle == D2RL::Widgets::InvalidHandle) return;
    const bool show = now < NoticeUntilMs && !WindowOpen();
    if ((show ? 1 : 0) == NoticeShown && now - NoticeApplyMs < 250) return;
    const D2RL::Widgets::Result r = WidgetSvc->setWidgetVisible(Context, NoticeHandle, show);
    if (r == D2RL::Widgets::Result::StaleHandle || r == D2RL::Widgets::Result::NotFound) {
        NoticeHandle       = D2RL::Widgets::InvalidHandle;
        NoticeButtonHandle = D2RL::Widgets::InvalidHandle;
        HudPanelHandle     = D2RL::Widgets::InvalidHandle;
        return;
    }
    if (NoticeButtonHandle != D2RL::Widgets::InvalidHandle) {
        (void)WidgetSvc->setWidgetEnabled(Context, NoticeButtonHandle, show);
    }
    NoticeShown   = show ? 1 : 0;
    NoticeApplyMs = now;
}

// ---------------------------------------------------------------------------
// Network: client side
// ---------------------------------------------------------------------------

constexpr std::size_t MaxPending = 8;
std::mutex                        PendingMutex;
std::array<Message, MaxPending>   Pending{};
std::size_t                       PendingCount{};
std::uint64_t                     PendingSinceMs{};

auto Send(const D2RL::PluginContext* context, const Message& message) noexcept -> bool {
    return NetworkSvc->sendToHost(context, Channel, RequestMessageId, &message, sizeof(message))
        == D2RL::Network::Result::Success;
}

void Connect(const D2RL::PluginContext* context) noexcept {
    (void)NetworkSvc->connectToHost(context, Channel);
}

void Queue(const D2RL::PluginContext* context, const Message& message) noexcept {
    if (Send(context, message)) {
        RequestsSent.fetch_add(1);
        return;
    }
    {
        std::lock_guard lock(PendingMutex);
        if (PendingCount < MaxPending) {
            if (PendingCount == 0) PendingSinceMs = GetTickCount64();
            Pending[PendingCount++] = message;
        } else {
            RequestsDropped.fetch_add(1);
        }
    }
    Connect(context);
}

void FlushPending(const D2RL::PluginContext* context) noexcept {
    std::array<Message, MaxPending> taken{};
    std::size_t count = 0;
    std::uint64_t since = 0;
    {
        std::lock_guard lock(PendingMutex);
        taken        = Pending;
        count        = PendingCount;
        since        = PendingSinceMs;
        PendingCount = 0;
    }
    if (count == 0) return;
    if (GetTickCount64() - since > 5000) {
        RequestsDropped.fetch_add(count);
        return;
    }
    for (std::size_t i = 0; i < count; ++i) (Send(context, taken[i]) ? RequestsSent : RequestsDropped).fetch_add(1);
}

void SendHello(const D2RL::PluginContext* context) noexcept {
    void* const player = LocalPlayer();
    if (player == nullptr) return;
    Queue(context, Message{ UnitId(player), static_cast<std::uint8_t>(Op::Hello), 0, 0 });
}

void __cdecl OnConnectionState(const D2RL::PluginContext* context, const D2RL::Network::ConnectionEvent* event,
        void*) noexcept {
    if (context == nullptr || !D2RL::Network::HasConnectionEventField(event, D2RL::Network::ConnectionEventRequiredSize)) {
        return;
    }
    ConnectionNow.store(static_cast<std::uint32_t>(event->state));
    if (event->state == D2RL::Network::ConnectionState::Rejected) {
        D2RL::LogWarnF(context, "Ascendancies: the host refused the plugin channel (reason %u). The host needs this "
            "plugin, the same version.", static_cast<unsigned>(event->reason));
        return;
    }
    if (event->state == D2RL::Network::ConnectionState::Connected) {
        FlushPending(context);
        SendHello(context);
    }
}

// The host never answers: the saved stats themselves reach the client.
void __cdecl OnClientMessage(const D2RL::PluginContext*, D2RL::Network::ChannelHandle, std::uint16_t, const void*,
        std::uint32_t, void*) noexcept {}

// ---------------------------------------------------------------------------
// The UI task: sheet entry, notice and window, on every root widget update
// ---------------------------------------------------------------------------

void TryOpenWindow(const D2RL::PluginContext* context);
void OpenWindow(const D2RL::PluginContext* context);

// Consuming the Orb of Creation opens the window: the orb stat rising from 0
// while no ascendancy is chosen. The first look in a game only takes the
// value, so entering a game with an unused orb does not open it.
void OpenOnOrb(const D2RL::PluginContext* context, const Snapshot& snap) {
    const std::int64_t orb = snap.orb;
    if (SeenOrb >= 0 && SeenOrb < 1 && orb >= 1 && snap.ascendancy == 0 && !WindowOpen()) {
        context->LogInfo("Ascendancies: the Orb of Creation was consumed; opening the window.");
        OpenWindow(context);
    }
    SeenOrb = orb;
}

std::atomic<std::uint64_t> UiChain{};
std::atomic<std::uint64_t> UiChainCounter{};
std::uint64_t              UiChainSeen{};
std::uint64_t              GuardUi{};
std::uint32_t              GuardUiCount{};
bool                       SpaceWasDown{ true };

void __cdecl UiTick(const D2RL::PluginContext* context, void* userData) noexcept {
    const std::uint64_t id = static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(userData));
    if (context == nullptr || UiChain.load() != id || !Active.load()) return;
    const std::uint64_t now = GetTickCount64();
    if (UiChainSeen != id) {
        UiChainSeen        = id;
        HudPanelHandle     = D2RL::Widgets::InvalidHandle;
        NoticeHandle       = D2RL::Widgets::InvalidHandle;
        NoticeButtonHandle = D2RL::Widgets::InvalidHandle;
        NoticeShown        = -1;
        NoticeLookups      = 0;
        NoticeUntilMs      = 0;
        SeenTiers          = -1;
        SeenOrb            = -1;
        SheetButtonLook.clear();
    }
    if (now / 25 != GuardUi) {
        GuardUi      = now / 25;
        GuardUiCount = 0;
    }
    if (++GuardUiCount > 50) {
        UiChain.store(0);
        context->LogError("Ascendancies: the UI refresh ran 50 times inside 25 ms; it is off for this game.");
        return;
    }
    EnsureHostChain(context);
    if (void* const player = LocalPlayer(); player != nullptr) {
        const Snapshot snap = ReadSnapshot(player);
        UpdateSheet(snap);
        UpdateNotice(snap, now);
        OpenOnOrb(context, snap);
    }
    if (PendingOpenTries > 0 && !WindowOpen()) TryOpenWindow(context);
    if (WindowOpen()) {
        const bool space = (GetAsyncKeyState(VK_SPACE) & 0x8000) != 0 && GameInFront() && !TypingText();
        if (space && !SpaceWasDown) {
            ConfirmOpen = false;
            (void)PanelSvc->closePanel(context, Panel);
        } else {
            RefreshWindow();
        }
        SpaceWasDown = space;
    } else {
        SpaceWasDown = true;
        ConfirmOpen  = false;
    }
    if (ThreadSvc->runOnUiThread(context, &UiTick, userData) != D2RL::Threads::Result::Success) {
        std::uint64_t expected = id;
        (void)UiChain.compare_exchange_strong(expected, 0);
    }
}

void StartUiChain(const D2RL::PluginContext* context) noexcept {
    const std::uint64_t id = UiChainCounter.fetch_add(1) + 1;
    UiChain.store(id);
    if (ThreadSvc->runOnUiThread(context, &UiTick, reinterpret_cast<void*>(static_cast<std::uintptr_t>(id)))
            != D2RL::Threads::Result::Success) {
        std::uint64_t expected = id;
        (void)UiChain.compare_exchange_strong(expected, 0);
    }
}

// ---------------------------------------------------------------------------
// Clicks (UI messages)
// ---------------------------------------------------------------------------

constexpr std::string_view ClickTarget  = "PanelManager";
constexpr std::string_view ClickCommand = "OpenPanel";
constexpr std::string_view ClickPrefix  = "Ascendancies";

enum ClickKind : std::uintptr_t { ClickOpen = 1, ClickClose, ClickPrevious, ClickNext, ClickAction, ClickYes, ClickNo,
    ClickNotice };

void PlayNamedSound(const std::string& name) noexcept {
    if (!name.empty()) PlaySound(name.c_str(), nullptr, 0, 0);
}

auto PanelResultName(D2RL::Panels::Result result) noexcept -> const char* {
    switch (result) {
    case D2RL::Panels::Result::Success:         return "Success";
    case D2RL::Panels::Result::InvalidArgument: return "InvalidArgument";
    case D2RL::Panels::Result::Unsupported:     return "Unsupported";
    case D2RL::Panels::Result::Unavailable:     return "Unavailable (no root UI, or out of panels)";
    case D2RL::Panels::Result::Conflict:        return "Conflict";
    case D2RL::Panels::Result::NotFound:        return "NotFound (the window layout could not load)";
    case D2RL::Panels::Result::Busy:            return "Busy (D2R did not show it yet)";
    case D2RL::Panels::Result::OwnerInactive:   return "OwnerInactive";
    case D2RL::Panels::Result::OwnerMismatch:   return "OwnerMismatch";
    case D2RL::Panels::Result::StaleHandle:     return "StaleHandle";
    case D2RL::Panels::Result::CallbackFault:   return "CallbackFault";
    }
    return "unknown";
}

std::atomic<std::uint32_t> LastOpenResult{ 0xFFFFFFFFU };

// Opens the window; Busy is tried again on the next UI updates.
void TryOpenWindow(const D2RL::PluginContext* context) {
    const D2RL::Panels::Result result = PanelSvc->openPanel(context, Panel);
    LastOpenResult.store(static_cast<std::uint32_t>(result));
    if (result == D2RL::Panels::Result::Success) {
        PendingOpenTries = 0;
        ResetWindowCaches();
        ConfirmOpen  = false;
        SpaceWasDown = true;
        if (void* const player = LocalPlayer(); player != nullptr) {
            const Snapshot snap = ReadSnapshot(player);
            if (snap.ascendancy > 0) Viewed = static_cast<std::size_t>(snap.ascendancy - 1);
        }
        NoticeUntilMs = 0;
        RefreshWindow();
        return;
    }
    if (result == D2RL::Panels::Result::Busy && PendingOpenTries > 0) {
        --PendingOpenTries;
        if (PendingOpenTries > 0) return;
    }
    PendingOpenTries = 0;
    D2RL::LogWarnF(context, "Ascendancies: the window did not open: %s. D2RLoader's log, just above, names the "
        "problem; with NotFound check that every sprite the layout uses is in place "
        "(data/hd/global/ui/panel/ascendancies/ and data/hd/global/ui/ascendancies/).", PanelResultName(result));
}

void OpenWindow(const D2RL::PluginContext* context) {
    void* const player = LocalPlayer();
    if (player == nullptr) {
        context->LogWarn("Ascendancies: the window was asked to open with no local player.");
        return;
    }
    const Snapshot snap = ReadSnapshot(player);
    // Hidden buttons still take clicks: the sheet's spot of the hidden entry
    // must not open the window.
    if (snap.orb < 1 && snap.ascendancy == 0 && (snap.progress & AscendedBit) == 0) {
        ClicksIgnored.fetch_add(1);
        return;
    }
    PendingOpenTries = 30;
    TryOpenWindow(context);
}

void __cdecl HandleClick(const D2RL::PluginContext* context, void* userData) noexcept {
    if (context == nullptr || !Active.load()) return;
    const auto kind = reinterpret_cast<std::uintptr_t>(userData);
    void* const player = LocalPlayer();
    switch (kind) {
    case ClickOpen:
        if (WindowOpen()) {
            context->LogInfo("Ascendancies: the window is open already; this click closes it.");
            (void)PanelSvc->closePanel(context, Panel);
            return;
        }
        OpenWindow(context);
        return;
    case ClickNotice:
        NoticeUntilMs = 0;
        if (!WindowOpen()) OpenWindow(context);
        return;
    case ClickClose:
        ConfirmOpen = false;
        (void)PanelSvc->closePanel(context, Panel);
        return;
    case ClickPrevious:
    case ClickNext:
        if (!WindowOpen() || Settings.ascendancies.empty()) return;
        ConfirmOpen = false;
        Viewed = kind == ClickNext ? (Viewed + 1) % Settings.ascendancies.size()
                                   : (Viewed + Settings.ascendancies.size() - 1) % Settings.ascendancies.size();
        RefreshWindow();
        return;
    case ClickAction: {
        if (!WindowOpen() || player == nullptr || ConfirmOpen) return;
        const Snapshot snap = ReadSnapshot(player);
        if (snap.orb >= 1 && snap.ascendancy == 0) {
            PlayNamedSound(Settings.ascendSound);
            Queue(context, Message{ UnitId(player), static_cast<std::uint8_t>(Op::Ascend),
                static_cast<std::uint8_t>(Viewed), 0 });
        } else if (snap.ascendancy == static_cast<std::int32_t>(Viewed + 1)) {
            ConfirmOpen = true;
        } else {
            ClicksIgnored.fetch_add(1);
        }
        RefreshWindow();
        return;
    }
    case ClickYes:
        if (!WindowOpen() || player == nullptr || !ConfirmOpen) {
            ClicksIgnored.fetch_add(1);
            return;
        }
        ConfirmOpen = false;
        PlayNamedSound(Settings.renounceSound);
        Queue(context, Message{ UnitId(player), static_cast<std::uint8_t>(Op::Renounce), 0, 0 });
        RefreshWindow();
        return;
    case ClickNo:
        if (!ConfirmOpen) {
            ClicksIgnored.fetch_add(1);
            return;
        }
        ConfirmOpen = false;
        RefreshWindow();
        return;
    default:
        return;
    }
}

std::atomic<std::uint64_t> UiMessagesSeen{};
std::atomic<std::uint64_t> UiMessagesOurs{};
std::atomic<std::uint32_t> UiMessagesLogged{};

// Every UI message naming an ascendancy is logged (the first 20), whatever its
// target or command, so a button sending something else shows in the log.
void NoteUiMessage(const D2RL::SharedEvents::UiMessageEvent* event) noexcept {
    const char* const text = event->text != nullptr ? event->text : "";
    if (std::strstr(text, "scendanc") == nullptr || UiMessagesLogged.fetch_add(1) >= 20) return;
    D2RL::LogInfoF(Context, "Ascendancies: UI message %s:%s:%s.", event->target != nullptr ? event->target : "",
        event->command != nullptr ? event->command : "", text);
}

auto __cdecl OnUiMessage(const D2RL::PluginContext* context, const D2RL::SharedEvents::UiMessageEvent* event, void*) noexcept
        -> D2RL::SharedEvents::UiMessageAction {
    if (event != nullptr) UiMessagesSeen.fetch_add(1);
    if (context == nullptr || event == nullptr || event->structSize < D2RL::SharedEvents::UiMessageEventRequiredSize) {
        return D2RL::SharedEvents::UiMessageAction::Continue;
    }
    NoteUiMessage(event);
    if (event->target == nullptr || event->command == nullptr || event->text == nullptr
            || ClickTarget != event->target || ClickCommand != event->command) {
        return D2RL::SharedEvents::UiMessageAction::Continue;
    }
    const std::string_view text(event->text);
    if (text.substr(0, ClickPrefix.size()) != ClickPrefix) return D2RL::SharedEvents::UiMessageAction::Continue;
    const std::string_view rest = text.substr(ClickPrefix.size());
    std::uintptr_t kind = 0;
    if (rest == "Open") kind = ClickOpen;
    else if (rest == "Close") kind = ClickClose;
    else if (rest == "Previous") kind = ClickPrevious;
    else if (rest == "Next") kind = ClickNext;
    else if (rest == "Action") kind = ClickAction;
    else if (rest == "Yes") kind = ClickYes;
    else if (rest == "No") kind = ClickNo;
    else if (rest == "Notice") kind = ClickNotice;
    UiMessagesOurs.fetch_add(1);
    if (kind != 0 && Active.load()) (void)ThreadSvc->runOnUiThread(context, &HandleClick, reinterpret_cast<void*>(kind));
    // The message is private to this plugin, so it never goes on to PanelManager.
    return D2RL::SharedEvents::UiMessageAction::Consume;
}

// ---------------------------------------------------------------------------
// Game join and leave
// ---------------------------------------------------------------------------

void ClearSession() noexcept {
    UiChain.store(0);
    HostChain.store(0);
    ResetWindowCaches();
    ConfirmOpen = false;
    {
        std::lock_guard lock(PendingMutex);
        PendingCount = 0;
    }
    {
        std::lock_guard lock(RequestMutex);
        RequestCount = 0;
    }
    {
        std::lock_guard lock(DeathMutex);
        DeathCount = 0;
    }
    std::lock_guard lock(HostMutex);
    HostPlayers.clear();
}

// Table rows can only be read on the game thread, so the row check is queued
// there when a game starts after the tables (re)loaded.
std::atomic<bool> RowCheckPending{ true };
void __cdecl RunRowCheck(const D2RL::PluginContext* context, void*) noexcept;

void __cdecl OnGameJoined(const D2RL::PluginContext*, const D2RL::Lifecycle::GameplayEvent*, void*) noexcept {
    ClearSession();
    ConnectionNow.store(static_cast<std::uint32_t>(D2RL::Network::ConnectionState::Disconnected));
}

void __cdecl OnLocalPlayerReady(const D2RL::PluginContext* context, const D2RL::Lifecycle::GameplayEvent*,
        void*) noexcept {
    if (context == nullptr || !Active.load()) return;
    Connect(context);
    SendHello(context);
    StartUiChain(context);
    StartHostChain(context);
    if (TableSvc != nullptr && RowCheckPending.exchange(false)
            && ThreadSvc->runOnGameThread(context, &RunRowCheck, nullptr) != D2RL::Threads::Result::Success) {
        RowCheckPending.store(true);
    }
}

void __cdecl OnGameLeft(const D2RL::PluginContext*, const D2RL::Lifecycle::GameplayEvent*, void*) noexcept {
    ClearSession();
    ConnectionNow.store(static_cast<std::uint32_t>(D2RL::Network::ConnectionState::Disconnected));
}

// ---------------------------------------------------------------------------
// itemstatcost and states check, every time the tables load
// ---------------------------------------------------------------------------

struct RowReport {
    bool        busy{};
    std::string text;
    bool        statsOk{};
    bool        stateOk{};
};

std::mutex RowReportMutex;
RowReport  LastRowReport;
bool       RowReportReady{};

auto TableRow(const D2RL::PluginContext* context, D2RL::DataTables::TableId table, std::int32_t index,
        D2RL::DataTables::RowView& row) noexcept -> D2RL::DataTables::Result {
    D2RL::DataTables::Result result = D2RL::DataTables::Result::NotFound;
    if (index < 0) return result;
    for (const D2RL::DataTables::Bank bank : { D2RL::DataTables::Bank::Rotw, D2RL::DataTables::Bank::Lod }) {
        row            = {};
        row.structSize = D2RL::DataTables::RowViewSize;
        result = TableSvc->getRow(context, bank, table, static_cast<std::uint32_t>(index), &row);
        if (result == D2RL::DataTables::Result::Busy) return result;
        if (result == D2RL::DataTables::Result::Success
                && D2RL::DataTables::HasRowViewField(&row, D2RL::DataTables::RowViewRequiredSize) && row.row != nullptr) {
            return result;
        }
    }
    return D2RL::DataTables::Result::NotFound;
}

void __cdecl OnDataTablesLoaded(const D2RL::PluginContext*, const D2RL::Lifecycle::DataTablesLoadedEvent*,
        void*) noexcept {
    RowCheckPending.store(true);
}

void __cdecl RunRowCheck(const D2RL::PluginContext* context, void*) noexcept {
    if (context == nullptr || TableSvc == nullptr) return;
    RowReport report{};
    report.statsOk = true;
    char line[200];
    for (const auto& [stat, name] : { std::pair{ Settings.ascendancyStat, "ascendancy_stat" },
            std::pair{ Settings.progressStat, "progress_stat" }, std::pair{ Settings.soulstoneStat, "soulstone_stat" },
            std::pair{ Settings.orbStat, "orb_stat" } }) {
        D2RL::DataTables::RowView row{};
        const D2RL::DataTables::Result result = TableRow(context, D2RL::DataTables::TableId::ItemStatCost, stat, row);
        if (result == D2RL::DataTables::Result::Busy) {
            RowCheckPending.store(true);
            return;
        }
        if (result != D2RL::DataTables::Result::Success || row.rowSize <= IscCsvParamOffset) {
            std::snprintf(line, sizeof(line), "%s %d: no itemstatcost row. ", name, stat);
            report.text += line;
            report.statsOk = false;
            continue;
        }
        const bool saved = (Read<std::uint32_t>(row.row, IscFlagsOffset) & IscFlagSaved) != 0;
        const std::uint8_t bits = Read<std::uint8_t>(row.row, IscCsvBitsOffset);
        if (!saved || bits == 0) {
            std::snprintf(line, sizeof(line), "%s %d: needs Saved 1 and CSvBits above 0 (Saved %d, CSvBits %u). ",
                name, stat, saved ? 1 : 0, static_cast<unsigned>(bits));
            report.text += line;
            report.statsOk = false;
        }
    }
    D2RL::DataTables::RowView stateRow{};
    report.stateOk = TableRow(context, D2RL::DataTables::TableId::States, Settings.bonusState, stateRow)
        == D2RL::DataTables::Result::Success;
    if (!report.stateOk) {
        std::snprintf(line, sizeof(line), "bonus_state %d: no states.txt row. ", Settings.bonusState);
        report.text += line;
    }
    if (report.text.empty()) report.text = "every stat row and the bonus state are in order.";
    {
        std::lock_guard lock(RowReportMutex);
        LastRowReport  = report;
        RowReportReady = true;
    }
    if (!report.statsOk || !report.stateOk) {
        D2RL::LogErrorF(context, "Ascendancies: %s", report.text.c_str());
    } else {
        D2RL::LogInfoF(context, "Ascendancies: %s", report.text.c_str());
    }
}

// ---------------------------------------------------------------------------
// Console command
// ---------------------------------------------------------------------------

auto StateName(PluginState state) noexcept -> const char* {
    switch (state) {
    case PluginState::NotLoaded:        return "not loaded";
    case PluginState::DisabledByConfig: return "disabled by config";
    case PluginState::BadConfig:        return "bad config";
    case PluginState::UnsupportedBuild: return "unsupported D2R build";
    case PluginState::ServicesMissing:  return "D2RLoader services missing";
    case PluginState::InstallFailed:    return "registration failed";
    case PluginState::Active:           return "active";
    }
    return "?";
}

auto Count(const std::atomic<std::uint64_t>& counter) noexcept -> unsigned long long {
    return static_cast<unsigned long long>(counter.load());
}

auto __cdecl StatusCommand(D2R::Game::Client*, const D2RL::ConsoleCommandContext* command, void*) noexcept
        -> D2RL::ConsoleCommandResult {
    if (command == nullptr || command->plugin == nullptr) return D2RL::ConsoleCommandResult::Failed;
    const auto print = [command](const char* text) { command->plugin->WriteConsoleMessage(text); };
    char line[512];
    std::snprintf(line, sizeof(line), "Ascendancies %s: %s. %zu ascendancies, %zu triggers, %zu kill credit rows.",
        PluginVersion, StateName(State), Settings.ascendancies.size(), Settings.triggers.size(), Settings.credits.size());
    print(line);
    if (!ConfigProblems.empty()) {
        std::snprintf(line, sizeof(line), "Config: %.440s", ConfigProblems.c_str());
        print(line);
    }
    {
        std::lock_guard lock(RowReportMutex);
        if (RowReportReady) {
            std::snprintf(line, sizeof(line), "Rows: %.480s", LastRowReport.text.c_str());
            print(line);
        }
    }
    if (State != PluginState::Active) return D2RL::ConsoleCommandResult::Handled;
    if (void* const player = LocalPlayer(); player != nullptr) {
        const Snapshot snap = ReadSnapshot(player);
        const char* const name = snap.ascendancy > 0
            ? Settings.ascendancies[static_cast<std::size_t>(snap.ascendancy - 1)].name.c_str() : "none";
        std::string tiers;
        for (std::size_t t = 0; t < TierCount; ++t) {
            if ((snap.progress & (1U << t)) != 0) tiers += (tiers.empty() ? "" : ",") + std::to_string(t + 1);
        }
        std::snprintf(line, sizeof(line), "You: ascendancy %s, tiers [%s], Lich King bonus %s, soulstones %d/%d, "
            "orb %d, ascended before %s. Bonus state %d on this unit: %s.", name, tiers.c_str(),
            (snap.progress & LichKingBit) != 0 ? "yes" : "no", snap.soulstones, Settings.soulstoneCap, snap.orb,
            (snap.progress & AscendedBit) != 0 ? "yes" : "no", Settings.bonusState,
            ListByState(player, Settings.bonusState) != nullptr ? "has its stat list" : "no stat list");
        print(line);
    }
    {
        std::lock_guard lock(HostMutex);
        std::snprintf(line, sizeof(line), "Host: %zu players tracked. Lists applied %llu, removed %llu, properties "
            "%llu (unknown codes %llu). Tiers granted %llu, Lich King %llu. Boss deaths %llu, kill credits %llu "
            "(failed %llu). Stones migrated %llu, soulstones taken %llu.", HostPlayers.size(), Count(BonusApplied),
            Count(BonusRemoved), Count(PropertiesApplied), Count(PropertiesUnknown), Count(TiersGranted),
            Count(LichKingGranted), Count(DeathsSeen), Count(KillCredits), Count(KillCreditFailures), Count(Migrations),
            Count(SoulstonesTaken));
        print(line);
    }
    std::snprintf(line, sizeof(line), "Requests: sent %llu, dropped %llu, received %llu, refused %llu. Ascended %llu, "
        "renounced %llu. Clicks ignored %llu, pages too long for the window %llu. Channel state %u.",
        Count(RequestsSent), Count(RequestsDropped), Count(RequestsReceived), Count(RequestsRefused), Count(Ascended),
        Count(Renounced), Count(ClicksIgnored), Count(TextOverflow), ConnectionNow.load());
    print(line);
    const std::uint64_t lastRun = HostLastRunMs.load();
    std::snprintf(line, sizeof(line), "UI messages: %llu seen, %llu for this plugin.", Count(UiMessagesSeen),
        Count(UiMessagesOurs));
    print(line);
    std::snprintf(line, sizeof(line), "Host task: %s, %llu runs, last %s. Last window open: %s.",
        HostChain.load() != 0 ? "running" : (HostUnavailable.load() ? "not here (TCP/IP client)" : "stopped"),
        Count(HostRuns), lastRun == 0 ? "never" : (GetTickCount64() - lastRun < 1000 ? "this second" : "over a second ago"),
        LastOpenResult.load() == 0xFFFFFFFFU ? "not tried"
            : PanelResultName(static_cast<D2RL::Panels::Result>(LastOpenResult.load())));
    print(line);
    return D2RL::ConsoleCommandResult::Handled;
}

// ---------------------------------------------------------------------------
// Load
// ---------------------------------------------------------------------------

// A hook placed by another plugin starts with a jump: E9 rel32, FF 25 [rip], or
// mov rax, imm64 / jmp rax. Hooks rewrite at most the first 14 bytes.
auto HookedByOtherPlugin(const Witness& witness) noexcept -> bool {
    const auto* live = reinterpret_cast<const std::uint8_t*>(Base + witness.rva);
    const bool jump = live[0] == 0xE9 || (live[0] == 0xFF && live[1] == 0x25) || (live[0] == 0x48 && live[1] == 0xB8);
    if (!jump) return false;
    constexpr std::uint32_t Rewritten = 14;
    return witness.size <= Rewritten || std::memcmp(live + Rewritten, witness.bytes + Rewritten, witness.size - Rewritten) == 0;
}

auto VerifyAll() noexcept -> bool {
    bool ok = true;
    for (const Witness& witness : Witnesses) {
        if (Context->CheckExpectedBytes(witness.rva, witness.bytes, witness.size)) continue;
        if (witness.callable && HookedByOtherPlugin(witness)) {
            D2RL::LogInfoF(Context, "Ascendancies: %s at RVA 0x%llX is hooked by another plugin; calls go through that "
                "hook.", witness.name, static_cast<unsigned long long>(witness.rva));
            continue;
        }
        D2RL::LogErrorF(Context, "Ascendancies: %s at RVA 0x%llX does not match this build.", witness.name,
            static_cast<unsigned long long>(witness.rva));
        ok = false;
    }
    return ok;
}

void BindNatives() noexcept {
    AssignProperty   = At<AssignPropertyFn>(AssignPropertyRva);
    LinkerLookup     = At<LinkerLookupFn>(LinkerLookupRva);
    DataTables       = At<DataTablesFn>(DataTablesRva);
    DataContext      = At<DataContextFn>(DataContextRva);
    ApplyTimedEffect = At<TimedEffectFn>(TimedEffectRva);
    ListByState      = At<ListByStateFn>(ListByStateRva);
    UnlinkList       = At<UnlinkListFn>(UnlinkListRva);
    FreeList         = At<FreeListFn>(FreeListRva);
    ToggleState      = At<ToggleStateFn>(ToggleStateRva);
    UnitRoom         = At<UnitRoomFn>(UnitRoomRva);
    RoomLevel        = At<RoomLevelFn>(RoomLevelRva);
    UnitDead         = At<UnitDeadFn>(UnitDeadRva);
    UnitInventory    = At<UnitInventoryFn>(UnitInventoryRva);
    FirstItem        = At<NextItemFn>(FirstItemRva);
    NextItem         = At<NextItemFn>(NextItemRva);
    ItemCode4        = At<ItemCodeFn>(ItemCodeRva);
    ItemFileIndex    = At<ItemIndexFn>(ItemFileIndexRva);
    TopPanel         = At<TopPanelFn>(TopPanelRva);
    FindChild        = At<FindChildFn>(FindChildRva);
    ButtonEnable     = At<ButtonEnableFn>(ButtonEnableRva);
    PlaySound        = At<PlaySoundFn>(PlaySoundByNameRva);
    NativeSetText    = At<SetTextFn>(SetTextRva);
    ReadStat         = At<ReadStatFn>(ReadStatRva);
    ReadOwnStat      = At<ReadStatFn>(ReadOwnStatRva);
    WriteOwnStat     = At<WriteStatFn>(WriteOwnStatRva);
    UnitType         = At<UnitTypeFn>(UnitTypeRva);
    UnitId           = At<UnitIdFn>(UnitIdRva);
    LocalPlayerIndex = At<PlayerIndexFn>(LocalPlayerIndexRva);
    PlayerFromIndex  = At<PlayerFromFn>(PlayerFromIndexRva);
    ServerUnit       = At<ServerUnitFn>(ServerUnitRva);
    UiState          = At<UiStateFn>(UiStateRva);
}

template <typename Service>
auto Need(const Service*& service, bool usable, const char* name) noexcept -> bool {
    if (service != nullptr && usable) return true;
    service = nullptr;
    D2RL::LogErrorF(Context, "Ascendancies: the %s service is unavailable.", name);
    return false;
}

auto QueryServices() noexcept -> bool {
    bool ok = true;
    if (Context->QueryService(&PanelSvc) != D2RL::ServiceQueryResult::Success) PanelSvc = nullptr;
    ok &= Need(PanelSvc, D2RL::HasPanelServiceField(PanelSvc, D2RL::PanelServiceRequiredSize), "panel");
    if (Context->QueryService(&WidgetSvc) != D2RL::ServiceQueryResult::Success) WidgetSvc = nullptr;
    ok &= Need(WidgetSvc, D2RL::HasWidgetServiceField(WidgetSvc, D2RL::WidgetServiceRequiredSize), "widget");
    if (Context->QueryService(&EventSvc) != D2RL::ServiceQueryResult::Success) EventSvc = nullptr;
    ok &= Need(EventSvc, D2RL::HasSharedEventServiceField(EventSvc, D2RL::SharedEventServiceRequiredSize), "UI message");
    if (Context->QueryService(&NetworkSvc) != D2RL::ServiceQueryResult::Success) NetworkSvc = nullptr;
    ok &= Need(NetworkSvc, D2RL::HasNetworkServiceField(NetworkSvc, D2RL::NetworkServiceRequiredSize), "network");
    if (Context->QueryService(&ThreadSvc) != D2RL::ServiceQueryResult::Success) ThreadSvc = nullptr;
    ok &= Need(ThreadSvc, D2RL::HasThreadServiceField(ThreadSvc, D2RL::ThreadServiceRequiredSize), "thread");
    if (Context->QueryService(&ResourceSvc) != D2RL::ServiceQueryResult::Success) ResourceSvc = nullptr;
    ok &= Need(ResourceSvc, D2RL::HasResourceServiceField(ResourceSvc, D2RL::ResourceServiceRequiredSize), "resource");
    if (Context->QueryService(&LifecycleSvc) != D2RL::ServiceQueryResult::Success) LifecycleSvc = nullptr;
    ok &= Need(LifecycleSvc, D2RL::HasLifecycleServiceField(LifecycleSvc, D2RL::LifecycleServiceRequiredSize),
        "lifecycle");
    if (Context->QueryService(&InventorySvc) != D2RL::ServiceQueryResult::Success) InventorySvc = nullptr;
    ok &= Need(InventorySvc, D2RL::HasInventoryServiceField(InventorySvc, D2RL::InventoryServiceRequiredSize),
        "inventory");
    if (Context->QueryService(&ItemSvc) != D2RL::ServiceQueryResult::Success) ItemSvc = nullptr;
    ok &= Need(ItemSvc, D2RL::HasItemServiceField(ItemSvc, D2RL::ItemServiceRequiredSize), "item");
    if (Context->QueryService(&TableSvc) != D2RL::ServiceQueryResult::Success
            || !D2RL::HasDataTableServiceField(TableSvc, D2RL::DataTableServiceRequiredSize)) {
        TableSvc = nullptr;
        Context->LogWarn("Ascendancies: the data table service is unavailable; the row check is off.");
    }
    if (Context->QueryService(&TextSvc) != D2RL::ServiceQueryResult::Success
            || !D2RL::HasLocalizationServiceField(TextSvc, D2RL::LocalizationServiceRequiredSize)) {
        TextSvc = nullptr;
        Context->LogWarn("Ascendancies: the localization service is unavailable; texts show their keys.");
    }
    return ok;
}

auto RegisterEverything() noexcept -> bool {
    if (TableSvc != nullptr) {
        const D2RL::Lifecycle::DataTablesLoadedListener tables{
            .structSize = D2RL::Lifecycle::DataTablesLoadedListenerSize,
            .callback   = &OnDataTablesLoaded,
        };
        D2RL::Lifecycle::ListenerHandle handle = D2RL::Lifecycle::InvalidHandle;
        if (LifecycleSvc->registerDataTablesLoadedListener(Context, &tables, &handle) != D2RL::Lifecycle::Result::Success) {
            Context->LogWarn("Ascendancies: the table listener was refused; the row check is off.");
        }
    }
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
            Context->LogError("Ascendancies: a game join/leave listener was refused.");
            return false;
        }
    }
    const D2RL::Lifecycle::MonsterDeathListener deaths{
        .structSize = D2RL::Lifecycle::MonsterDeathListenerSize,
        .flags      = 0,
        .callback   = &OnMonsterDeath,
        .userData   = nullptr,
    };
    D2RL::Lifecycle::ListenerHandle deathHandle = D2RL::Lifecycle::InvalidHandle;
    if (LifecycleSvc->registerMonsterDeathListener(Context, &deaths, &deathHandle) != D2RL::Lifecycle::Result::Success) {
        Context->LogError("Ascendancies: the monster death listener was refused; no tier comes from boss kills.");
    }
    const D2RL::Network::ChannelRegistration channel{
        .structSize         = D2RL::Network::ChannelRegistrationSize,
        .localChannelId     = ChannelLocalId,
        .compatibilityToken = CompatibilityToken,
        .hostMessage        = &OnHostMessage,
        .clientMessage      = &OnClientMessage,
        .connectionState    = &OnConnectionState,
    };
    const D2RL::Network::Result channelResult = NetworkSvc->registerChannel(Context, &channel, &Channel);
    if (channelResult != D2RL::Network::Result::Success || Channel == D2RL::Network::InvalidChannelHandle) {
        D2RL::LogErrorF(Context, "Ascendancies: the network channel was refused (result %u).",
            static_cast<unsigned>(channelResult));
        return false;
    }
    std::string_view layoutBytes;
    if (!PanelLayoutBytes(layoutBytes)) {
        Context->LogError("Ascendancies: the window layout is missing from the plugin (AscendancyPanelLayout.inc).");
        return false;
    }
    const D2RL::Resources::ResourceRegistration layout{
        .structSize = D2RL::Resources::ResourceRegistrationSize,
        .flags      = 0,
        .path       = PanelResourcePath,
        .bytes      = reinterpret_cast<decltype(D2RL::Resources::ResourceRegistration::bytes)>(layoutBytes.data()),
        .byteCount  = static_cast<decltype(D2RL::Resources::ResourceRegistration::byteCount)>(layoutBytes.size()),
    };
    D2RL::Resources::RegistrationHandle layoutHandle = D2RL::Resources::InvalidHandle;
    const D2RL::Resources::Result layoutResult = ResourceSvc->registerResource(Context, &layout, &layoutHandle);
    if (layoutResult != D2RL::Resources::Result::Success) {
        D2RL::LogErrorF(Context, "Ascendancies: the window layout could not be handed to D2RLoader (result %u).",
            static_cast<unsigned>(layoutResult));
        return false;
    }
    const D2RL::Panels::PanelRegistration panel{
        .structSize = D2RL::Panels::PanelRegistrationSize,
        .flags      = D2RL::Panels::PanelFlags::CloseOnEscape,
        .localId    = PanelLocalId,
    };
    const D2RL::Panels::Result panelResult = PanelSvc->registerPanel(Context, &panel, &Panel);
    if (panelResult != D2RL::Panels::Result::Success || Panel == D2RL::Panels::InvalidHandle) {
        D2RL::LogErrorF(Context, "Ascendancies: the window panel was refused (result %u).",
            static_cast<unsigned>(panelResult));
        return false;
    }
    const D2RL::SharedEvents::UiMessageListener clicks{
        .structSize = D2RL::SharedEvents::UiMessageListenerSize,
        .flags      = 0,
        .priority   = 0,
        .reserved   = 0,
        .callback   = &OnUiMessage,
        .userData   = nullptr,
    };
    if (EventSvc->registerUiMessageListener(Context, &clicks, &ClickListener) != D2RL::SharedEvents::Result::Success) {
        Context->LogError("Ascendancies: the click listener was refused.");
        return false;
    }
    return true;
}

constexpr D2RL::PluginInfo PluginInfoData{
    .infoSize    = D2RL::PluginInfoSize,
    .abiVersion  = D2RL_PLUGIN_ABI_VERSION,
    .id          = PluginIdText,
    .name        = "Ascendancies",
    .version     = PluginVersion,
    .author      = "CelestialRayOne",
    .description = "Ascendancy window, tiers from boss kills and Primordial Soulstones, saved as character stats; "
                   "Ascendancy Stones migrate once.",
    .flags       = D2RL::PluginFlags::Shared,
};

} // namespace

D2RL_PLUGIN_EXPORT auto D2RLoaderGetPluginInfo() noexcept -> const D2RL::PluginInfo* {
    return &PluginInfoData;
}

D2RL_PLUGIN_EXPORT auto D2RLoaderLoadPlugin(const D2RL::PluginContext* context) noexcept -> bool {
    Context = context;
    if (Context == nullptr || Context->exeBase == 0) return false;
    Base = Context->exeBase;

    if (!Context->RegisterConsoleCommand("ascendancies", &StatusCommand,
            "Ascendancies status: config and row check, your ascendancy and tiers, host counters.")) {
        Context->LogWarn("Ascendancies: the console command could not be registered.");
    }

    ReadConfiguration();
    const bool configOk = ValidateConfig();
    if (!ConfigProblems.empty()) D2RL::LogWarnF(Context, "Ascendancies: %s", ConfigProblems.c_str());
    if (!Settings.enabled) {
        State = PluginState::DisabledByConfig;
        Context->LogInfo("Ascendancies: disabled by config.");
        return true;
    }
    if (!configOk) {
        State = PluginState::BadConfig;
        Context->LogError("Ascendancies: the config cannot be used; nothing was installed.");
        return true;
    }
    if (!VerifyAll()) {
        State = PluginState::UnsupportedBuild;
        Context->LogError("Ascendancies: this D2R build does not match; nothing was installed.");
        return true;
    }
    BindNatives();
    if (!QueryServices()) {
        State = PluginState::ServicesMissing;
        return true;
    }
    if (!RegisterEverything()) {
        State = PluginState::InstallFailed;
        return true;
    }
    Active.store(true);
    State = PluginState::Active;
    D2RL::LogInfoF(Context, "Ascendancies %s: active, %zu ascendancies, %zu triggers, %zu kill credit rows.",
        PluginVersion, Settings.ascendancies.size(), Settings.triggers.size(), Settings.credits.size());
    return true;
}

D2RL_PLUGIN_EXPORT void D2RLoaderUnloadPlugin() noexcept {
    Active.store(false);
    UiChain.store(0);
    HostChain.store(0);
}
