// Superior Affixes (celestialrayone.superior-affixes)
//
// Replaces the stock superior affix picker (qualityitems.txt) with one that
// picks by the row's frequency, removes its 10-row limit, and adds optional
// frequency, itype and etype columns to qualityitems.txt. Nothing in the save
// format changes.
//
// Target: D2RLoader 1.3.1 running D2R 3.3. Every address and byte below was
// read out of the dumped D2RLoader.exe image (md5 8af7a47c...) and decoded
// before being relied on. RVAs are against image base 0x140000000.
//
// ---------------------------------------------------------------------------
// 1. The stock picker, 0x58B9E0 .. 0x58BBA0 (only caller: the wrapper 0x58A5A0,
//    which tail-jumps into it; it is the D2MOO 1.10 sub_6FC54690)
// ---------------------------------------------------------------------------
//   58BA09  call 34A0E0                 ; item -> data context (bank)
//   58BA15  call 313DE0                 ; bank -> qualityitems table {rows, count}
//   58BA2D  call 349860                 ; item class id
//   58BA38  call 314110                 ; weapons/armor/misc record
//   58BA52  mov  rbx, [rbx+8]           ; count = qualityitems row count
//   58BA5B  mov  [rsp+38h], rax / mov [rsp+40h], ax   ; the "tried" list: 10 bytes
//   58BA65  call 374710                 ; itemtypes.txt Throwable of the item's own type
//   58BA6E  cmp  [rdi+122h], al         ; items record nodurability
//   58BA76  mov  ebx, 4                 ; either one: only rows 0..3
//   58BAA0  call 36CC80                 ; item seed {low, high}
//   58BAB5  imul r8, rax, 6AC690C5h / add r8, rcx ; low*0x6AC690C5 + high
//   58BAD8  div  ebx                    ; row = low % count
//   58BAF8  cmp  byte [rsp+38h+row], 0 / jnz reroll
//   58BB03  call 313D70                 ; bank, row -> record (44 bytes)
//   58BB16  call 3D40D0                 ; may this item use the record?
//   58BB21  mov  byte [rsp+38h+row], 1  ; mark tried, reroll
//   58BB6F  call 36DC80                 ; store the row number on the item
//   58BB89  call 3D3670 (1, 0, item, record, 0, 0)    ; apply mod1, then mod2
//   58BB94  call 58A050 (item, drop)    ; itemtypes.txt StaffMods
//
//   The tried list is 10 bytes at [rsp+38h]; [rsp+48h] is the stack cookie and
//   [rsp+50h] up the saved r15, r14, r13, r12, rdi, rsi, rbp and the return
//   address. Row 10 and up therefore reads uninitialised padding (rows 10-15),
//   the cookie (16-23) and the saved registers (24+): a non-zero byte makes
//   the row unpickable, and a rejected row writes 1 into the cookie (fast-fail
//   crash on return) or into a caller's saved register.
//
// ---------------------------------------------------------------------------
// 2. Why no save change is needed
// ---------------------------------------------------------------------------
//   - 313D70 (row -> record) and 313DE0 (table) have exactly one caller each,
//     this picker; their other references are .pdata unwind entries. So no
//     code turns a stored row number back into affixes.
//   - 3D3670 type 1 writes the rolled values into the item's own magic stat
//     list (state 0, flag 0x40); the item serializer 37D140 saves that list
//     with every other item stat (loop at 37EF05, writer call 37F093).
//   - The row number itself is written as 3 bits clamped to 7 (37DB7B..37DBA0,
//     shared with low quality) and read back as 3 bits. It is a label nothing
//     reads, so rows past 7 keep their affixes through any save and reload.
//
// ---------------------------------------------------------------------------
// 3. Stock eligibility, 3D40D0 (unchanged, called as is)
// ---------------------------------------------------------------------------
//   own type = items record type, 372C90
//   weapon column: CheckItemType(item, 45) and own type not 24..27 or 35
//   armor column:  CheckItemType(item, 50) and own type not 2, 15, 16, 19
//   then exact own type: 2 shield, 24 scepter, 25 wand, 26 staff, 27 and 35
//   bow, 15 boots, 16 gloves, 19 belt. It reads only the item and the record.
//
// ---------------------------------------------------------------------------
// 4. frequency / itype / etype columns
// ---------------------------------------------------------------------------
//   CheckItemType is 373890 (item, itemtypes index): true when the item's
//   items.txt type (record+302) or type2 (record+304) has the index set in the
//   itemtypes inheritance matrix (data tables +4968, stride +4960, count
//   +4944). That is the test magic affixes use for their itype/etype columns
//   (descriptor type 15 resolved through the itemtypes linker at data tables
//   +0x1340, see the rareprefix/raresuffix loader 31B230).
//
//   The stock qualityitems loader 315300 passes a fixed descriptor list to the
//   txt parser (j_LoadExcelTable, record size 44) and the parser skips every
//   column not in that list. So the extra columns are read by this plugin, on
//   D2RLoader's tables-loaded event (game thread), from the same
//   qualityitems.txt the game loaded:
//     - candidates are <mod>\data\global\excel\[base\]qualityitems.txt and the
//       same under <mod>\<mod>.mpq\, from the context's mod and scope folders;
//     - a candidate is used for a table bank only when it matches what the game
//       compiled for that bank row by row: same row count, the ten item-class
//       bytes (record 0..9) and mod1/mod2 param, min, max (record 16, 20, 24,
//       32, 36, 40), each parsed like the game parses them;
//     - codes are resolved with the data table service's ItemTypes lookup in
//       that bank;
//     - the result is bound to that bank's compiled rows pointer, which is the
//       table 313DE0 hands the picker, so the right set is used per item.
//   Anything that does not match is reported and simply not used.
//
//   The frequency column (named after, and working like, the column of
//   magicprefix.txt and magicsuffix.txt) is read from that same matched file,
//   row by row, so a frequency can only ever land on the row it was written
//   on. Empty or 0 means the row never rolls. A bank with no matched file, or
//   a matched file with no frequency column, gives every row the same odds
//   (stock).
//
// ---------------------------------------------------------------------------
// 5. The hook
// ---------------------------------------------------------------------------
//   The whole body is verified byte for byte (it pins every callee below),
//   then an inline hook on its first instruction (5 bytes, mov [rsp+18h],rbx)
//   sends every call to PickSuperiorAffix, which never calls the original.
//   PickSuperiorAffix does what the stock body does, in the same order and
//   through the same native calls, except for the row choice:
//     - no tried list: two passes over the rows instead, the first summing
//       the frequencies of every row the item may use, the second walking to
//       the rolled one; eligibility only reads, so asking twice changes
//       nothing;
//     - one seed roll per Superior item (the stock picker rolls once per
//       attempt), with the stock generator: low*0x6AC690C5 + high, then
//       low % total;
//     - when no row is usable the stock picker returns 0 without applying
//       anything, and so does this one.
//   Frequencies are 0..65535 and rows at most 65535, so the total fits 32 bits.

#include <D2RLPlugin/api.h>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#endif

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace CelestialRayOne::SuperiorAffixes {
namespace {

// ---------------------------------------------------------------------------
// Native anchors
// ---------------------------------------------------------------------------

constexpr std::uint64_t PickerRva                = 0x58B9E0;
constexpr std::uint64_t GetItemBankRva           = 0x34A0E0;
constexpr std::uint64_t GetQualityItemsTableRva  = 0x313DE0;
constexpr std::uint64_t GetClassIdRva            = 0x349860;
constexpr std::uint64_t ClassIdSourceFileRva     = 0x1D3EE00;
constexpr std::uint32_t ClassIdSourceLine        = 0x5D2;
constexpr std::uint64_t GetItemsRecordRva        = 0x314110;
constexpr std::uint64_t IsThrowableRva           = 0x374710;
constexpr std::uint64_t GetItemSeedRva           = 0x36CC80;
constexpr std::uint64_t GetQualityItemsRecordRva = 0x313D70;
constexpr std::uint64_t CanBeSuperiorRva         = 0x3D40D0;
constexpr std::uint64_t SetFileIndexRva          = 0x36DC80;
constexpr std::uint64_t AssignPropertiesRva      = 0x3D3670;
constexpr std::uint64_t StaffModsRva             = 0x58A050;
constexpr std::uint64_t CheckItemTypeRva         = 0x373890;

constexpr std::uint32_t ItemsNoDurabilityOffset  = 0x122;
constexpr std::int32_t  QualityItemsPropertyMode = 1;
constexpr std::uint64_t SeedMultiplier           = 0x6AC690C5;
constexpr std::uint32_t StockFirstRows           = 4;
constexpr std::uint32_t HookSize                 = 5;
constexpr std::uint32_t QualityItemsRowSize      = 44;
constexpr std::int32_t  WeaponTypeIndex          = 45;   // hardcoded in 3D40D0
constexpr std::int32_t  ArmorTypeIndex           = 50;   // hardcoded in 3D40D0

// The complete stock picker, 0x58B9E0..0x58BBA0.
constexpr std::uint8_t PickerWitness[] = {
    0x48, 0x89, 0x5C, 0x24, 0x18, 0x55, 0x56, 0x57, 0x41, 0x54, 0x41, 0x55, 0x41, 0x56, 0x41, 0x57,  // 58B9E0
    0x48, 0x83, 0xEC, 0x50, 0x48, 0x8B, 0x05, 0xCD, 0xF8, 0x43, 0x02, 0x48, 0x33, 0xC4, 0x48, 0x89,  // 58B9F0
    0x44, 0x24, 0x48, 0x4C, 0x8B, 0xEA, 0x4C, 0x8B, 0xF1, 0xE8, 0xD2, 0xE6, 0xDB, 0xFF, 0x0F, 0xB6,  // 58BA00
    0xC8, 0x44, 0x0F, 0xB6, 0xE0, 0xE8, 0xC6, 0x83, 0xD8, 0xFF, 0x41, 0xB8, 0xD2, 0x05, 0x00, 0x00,  // 58BA10
    0x48, 0x8D, 0x15, 0xD9, 0x33, 0x7B, 0x01, 0x49, 0x8B, 0xCE, 0x48, 0x8B, 0xD8, 0xE8, 0x2E, 0xDE,  // 58BA20
    0xDB, 0xFF, 0x8B, 0xD0, 0x41, 0x0F, 0xB6, 0xCC, 0xE8, 0xD3, 0x86, 0xD8, 0xFF, 0x48, 0x8B, 0xF8,  // 58BA30
    0x48, 0x85, 0xDB, 0x0F, 0x84, 0xFA, 0x00, 0x00, 0x00, 0x48, 0x85, 0xC0, 0x0F, 0x84, 0xF1, 0x00,  // 58BA40
    0x00, 0x00, 0x48, 0x8B, 0x5B, 0x08, 0x33, 0xC0, 0x49, 0x8B, 0xCE, 0x48, 0x89, 0x44, 0x24, 0x38,  // 58BA50
    0x66, 0x89, 0x44, 0x24, 0x40, 0xE8, 0xA6, 0x8C, 0xDE, 0xFF, 0x85, 0xC0, 0x75, 0x08, 0x38, 0x87,  // 58BA60
    0x22, 0x01, 0x00, 0x00, 0x74, 0x05, 0xBB, 0x04, 0x00, 0x00, 0x00, 0x4C, 0x63, 0xFB, 0x66, 0x90,  // 58BA70
    0x4C, 0x3B, 0xFB, 0x75, 0x04, 0x85, 0xDB, 0x79, 0x14, 0x48, 0x8D, 0x4C, 0x24, 0x30, 0xC6, 0x44,  // 58BA80
    0x24, 0x30, 0x00, 0xE8, 0xC8, 0xAE, 0xAF, 0xFF, 0x84, 0xC0, 0x74, 0x01, 0xCC, 0x49, 0x8B, 0xCE,  // 58BA90
    0xE8, 0xDB, 0x11, 0xDE, 0xFF, 0x48, 0x8B, 0xD0, 0x85, 0xDB, 0x7F, 0x04, 0x33, 0xFF, 0xEB, 0x3E,  // 58BAA0
    0x8B, 0x00, 0x8B, 0x4A, 0x04, 0x4C, 0x69, 0xC0, 0xC5, 0x90, 0xC6, 0x6A, 0x4C, 0x03, 0xC1, 0x8D,  // 58BAB0
    0x4B, 0xFF, 0x44, 0x89, 0x02, 0x85, 0xCB, 0x74, 0x15, 0x49, 0x8B, 0xC8, 0x41, 0x8B, 0xC0, 0x48,  // 58BAC0
    0xC1, 0xE9, 0x20, 0x89, 0x4A, 0x04, 0x33, 0xD2, 0xF7, 0xF3, 0x8B, 0xFA, 0xEB, 0x10, 0x49, 0x8B,  // 58BAD0
    0xC0, 0x8D, 0x7B, 0xFF, 0x48, 0xC1, 0xE8, 0x20, 0x89, 0x42, 0x04, 0x41, 0x23, 0xF8, 0x8B, 0xC7,  // 58BAE0
    0x48, 0x8D, 0x74, 0x24, 0x38, 0x48, 0x03, 0xF0, 0x80, 0x3E, 0x00, 0x75, 0x83, 0x8B, 0xD7, 0x41,  // 58BAF0
    0x0F, 0xB6, 0xCC, 0xE8, 0x68, 0x82, 0xD8, 0xFF, 0x48, 0x8B, 0xE8, 0x48, 0x85, 0xC0, 0x74, 0x33,  // 58BB00
    0x48, 0x8B, 0xD0, 0x49, 0x8B, 0xCE, 0xE8, 0xB5, 0x85, 0xE4, 0xFF, 0x85, 0xC0, 0x75, 0x4B, 0x33,  // 58BB10
    0xC0, 0xC6, 0x06, 0x01, 0x48, 0x85, 0xDB, 0x74, 0x1A, 0x0F, 0x1F, 0x80, 0x00, 0x00, 0x00, 0x00,  // 58BB20
    0x80, 0x7C, 0x04, 0x38, 0x00, 0x0F, 0x84, 0x45, 0xFF, 0xFF, 0xFF, 0x48, 0xFF, 0xC0, 0x48, 0x3B,  // 58BB30
    0xC3, 0x72, 0xED, 0x33, 0xC0, 0x48, 0x8B, 0x4C, 0x24, 0x48, 0x48, 0x33, 0xCC, 0xE8, 0xFE, 0x55,  // 58BB40
    0xD4, 0x00, 0x48, 0x8B, 0x9C, 0x24, 0xA0, 0x00, 0x00, 0x00, 0x48, 0x83, 0xC4, 0x50, 0x41, 0x5F,  // 58BB50
    0x41, 0x5E, 0x41, 0x5D, 0x41, 0x5C, 0x5F, 0x5E, 0x5D, 0xC3, 0x8B, 0xD7, 0x49, 0x8B, 0xCE, 0xE8,  // 58BB60
    0x0C, 0x21, 0xDE, 0xFF, 0x33, 0xC0, 0x4C, 0x8B, 0xCD, 0x89, 0x44, 0x24, 0x28, 0x4D, 0x8B, 0xC6,  // 58BB70
    0x33, 0xD2, 0x89, 0x44, 0x24, 0x20, 0x8D, 0x48, 0x01, 0xE8, 0xE2, 0x7A, 0xE4, 0xFF, 0x49, 0x8B,  // 58BB80
    0xD5, 0x49, 0x8B, 0xCE, 0xE8, 0xB7, 0xE4, 0xFF, 0xFF, 0xB8, 0x01, 0x00, 0x00, 0x00, 0xEB, 0xA5,  // 58BB90
};
static_assert(sizeof(PickerWitness) == 0x1C0);

// CheckItemType 373890: mov [rsp+10h],rbx / mov [rsp+18h],rbp / mov [rsp+20h],rsi /
// push rdi, r14, r15 / sub rsp,20h / movsxd rsi,edx / mov rdi,rcx / test rcx,rcx /
// jz ... (the item null check).
constexpr std::uint8_t CheckItemTypeWitness[] = {
    0x48, 0x89, 0x5C, 0x24, 0x10, 0x48, 0x89, 0x6C, 0x24, 0x18, 0x48, 0x89, 0x74, 0x24, 0x20, 0x57,
    0x41, 0x56, 0x41, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x63, 0xF2, 0x48, 0x8B, 0xF9, 0x48, 0x85,
    0xC9, 0x0F, 0x84, 0x9E, 0x00, 0x00, 0x00,
};

// Every native call target and constant the hook relies on, decoded from the
// witness at compile time, so the witness check also proves them.
constexpr auto Rel32Target(std::uint32_t instructionOffset, std::uint32_t displacementOffset,
        std::uint32_t instructionSize) -> std::uint64_t {
    const std::uint32_t at = instructionOffset + displacementOffset;
    const auto raw = static_cast<std::uint32_t>(PickerWitness[at])
        | (static_cast<std::uint32_t>(PickerWitness[at + 1]) << 8)
        | (static_cast<std::uint32_t>(PickerWitness[at + 2]) << 16)
        | (static_cast<std::uint32_t>(PickerWitness[at + 3]) << 24);
    const auto displacement = static_cast<std::int64_t>(static_cast<std::int32_t>(raw));
    return static_cast<std::uint64_t>(static_cast<std::int64_t>(PickerRva + instructionOffset
        + instructionSize) + displacement);
}
constexpr auto CallTarget(std::uint32_t offset) -> std::uint64_t {
    return PickerWitness[offset] == 0xE8 ? Rel32Target(offset, 1, 5) : 0;
}
constexpr auto ReadU32(std::uint32_t at) -> std::uint32_t {
    return static_cast<std::uint32_t>(PickerWitness[at])
        | (static_cast<std::uint32_t>(PickerWitness[at + 1]) << 8)
        | (static_cast<std::uint32_t>(PickerWitness[at + 2]) << 16)
        | (static_cast<std::uint32_t>(PickerWitness[at + 3]) << 24);
}

static_assert(CallTarget(0x029) == GetItemBankRva);
static_assert(CallTarget(0x035) == GetQualityItemsTableRva);
static_assert(CallTarget(0x04D) == GetClassIdRva);
static_assert(CallTarget(0x058) == GetItemsRecordRva);
static_assert(CallTarget(0x085) == IsThrowableRva);
static_assert(CallTarget(0x0C0) == GetItemSeedRva);
static_assert(CallTarget(0x123) == GetQualityItemsRecordRva);
static_assert(CallTarget(0x136) == CanBeSuperiorRva);
static_assert(CallTarget(0x18F) == SetFileIndexRva);
static_assert(CallTarget(0x1A9) == AssignPropertiesRva);
static_assert(CallTarget(0x1B4) == StaffModsRva);
static_assert(Rel32Target(0x040, 3, 7) == ClassIdSourceFileRva);       // lea rdx, file
static_assert(ReadU32(0x03C) == ClassIdSourceLine);                    // mov r8d, 5D2h
static_assert(ReadU32(0x090) == ItemsNoDurabilityOffset);              // cmp [rdi+122h], al
static_assert(ReadU32(0x097) == StockFirstRows);                       // mov ebx, 4
static_assert(ReadU32(0x0D8) == SeedMultiplier);                       // imul r8, rax, 6AC690C5h
static_assert(PickerWitness[0] == 0x48 && PickerWitness[4] == 0x18);   // mov [rsp+18h], rbx

// ---------------------------------------------------------------------------
// Native calls (Windows x64, one calling convention)
// ---------------------------------------------------------------------------

struct QualityItemsTable {
    const void*   rows;
    std::uint64_t count;
};

using GetItemBankFn           = std::uint8_t(*)(void* unit);
using GetQualityItemsTableFn  = const QualityItemsTable*(*)(std::uint8_t bank);
using GetClassIdFn            = std::uint32_t(*)(void* unit, const char* file, std::uint32_t line);
using GetItemsRecordFn        = const std::uint8_t*(*)(std::uint8_t bank, std::uint32_t classId);
using IsThrowableFn           = std::uint32_t(*)(void* item);
using GetItemSeedFn           = std::uint32_t*(*)(void* item);
using GetQualityItemsRecordFn = const void*(*)(std::uint8_t bank, std::uint32_t row);
using CanBeSuperiorFn         = std::uint32_t(*)(void* item, const void* record);
using SetFileIndexFn          = void(*)(void* item, std::uint32_t row);
using AssignPropertiesFn      = std::uint8_t(*)(std::int32_t mode, void* unit, void* item,
                                    const void* record, std::uint32_t propertySet,
                                    std::int32_t applyType);
using StaffModsFn             = void(*)(void* item, void* drop);
using CheckItemTypeFn         = std::uint32_t(*)(void* item, std::int32_t itemType);
using PickerFn                = std::int64_t(*)(void* item, void* drop);

struct NativeCalls {
    GetItemBankFn           getItemBank{};
    GetQualityItemsTableFn  getTable{};
    GetClassIdFn            getClassId{};
    const char*             classIdSourceFile{};
    GetItemsRecordFn        getItemsRecord{};
    IsThrowableFn           isThrowable{};
    GetItemSeedFn           getItemSeed{};
    GetQualityItemsRecordFn getRecord{};
    CanBeSuperiorFn         canBeSuperior{};
    SetFileIndexFn          setFileIndex{};
    AssignPropertiesFn      assignProperties{};
    StaffModsFn             staffMods{};
    CheckItemTypeFn         checkItemType{};
};

// ---------------------------------------------------------------------------
// Configuration
// ---------------------------------------------------------------------------

constexpr std::uint32_t MaxRows                = 65'535;
constexpr std::uint32_t MaxFrequency           = 65'535;
constexpr std::uint32_t EqualOddsFrequency     = 1;   // every row, when there is no frequency data
constexpr char          MainSection[]          = "superior_affixes";
// Removed in 1.2.0 (row odds moved to the frequency column of qualityitems.txt).
// Only recognised so that an old config says plainly that it is ignored.
constexpr char          ObsoleteWeightsSection[] = "superior_affixes.weights";
constexpr char          ObsoleteDefaultWeight[]  = "default_weight";
constexpr std::size_t   InitialConfigBytes     = 16'384;
constexpr std::size_t   MaximumConfigBytes     = 4'194'304;
constexpr std::size_t   ProblemSampleSize      = 6;
constexpr std::size_t   RowListSampleSize      = 24;

static_assert(static_cast<std::uint64_t>(MaxRows) * MaxFrequency <= 0xFFFF'FFFFull,
    "The frequency total must fit 32 bits.");

constexpr char DefaultConfigToml[] =
    "# Superior Affixes\n"
    "#\n"
    "# Picks the superior affix row (qualityitems.txt) of every Superior item the\n"
    "# game creates by the row's frequency, and removes the stock row limit.\n"
    "#\n"
    "# ---------------------------------------------------------------------------\n"
    "# What happens when an item rolls Superior\n"
    "# ---------------------------------------------------------------------------\n"
    "#   The server picks ONE qualityitems.txt row the item is allowed to use and\n"
    "#   applies that row's mod1 and mod2 to the item. Each value is rolled once\n"
    "#   between min and max and stored on the item as an ordinary stat, in the\n"
    "#   same place as magic and rare affixes. Nothing ever re-reads\n"
    "#   qualityitems.txt for that item afterwards.\n"
    "#\n"
    "#   Stock D2R gives every allowed row the same odds. This plugin gives each\n"
    "#   row the odds of its frequency column (below). Without a frequency\n"
    "#   column, every row has the same odds, exactly the stock ones.\n"
    "#\n"
    "# ---------------------------------------------------------------------------\n"
    "# frequency column in qualityitems.txt\n"
    "# ---------------------------------------------------------------------------\n"
    "#   Works like frequency in magicprefix.txt and magicsuffix.txt: a row's\n"
    "#   odds on a given item are its frequency divided by the sum of the\n"
    "#   frequencies of every row that item may use.\n"
    "#   - A whole number from 0 to 65535.\n"
    "#   - 0 or an empty cell: the row never rolls.\n"
    "#   - Anything else (text, a sign, a decimal point, more than 65535): the\n"
    "#     row never rolls, and the log names the row and the cell.\n"
    "#   - No frequency column in the file: every row has the same odds (stock).\n"
    "#   Example: an item may use three rows, with frequency 100, 25 and 0. They\n"
    "#   roll 80%, 20% and never.\n"
    "#   If every row an item may use has frequency 0, the item drops as a\n"
    "#   Normal item instead of Superior (the game's own fallback when no row\n"
    "#   fits).\n"
    "#\n"
    "# ---------------------------------------------------------------------------\n"
    "# Row limit\n"
    "# ---------------------------------------------------------------------------\n"
    "#   The stock picker remembers the rows it already tried in a 10-slot list.\n"
    "#   With more than 10 rows it reads and writes past that list: rows 10 to\n"
    "#   15 get skipped at random, and rows 16 and up can crash the game or\n"
    "#   corrupt it silently. This plugin keeps no such list. qualityitems.txt\n"
    "#   can have up to 65535 rows; rows past that are never picked, and the log\n"
    "#   says so.\n"
    "#\n"
    "# ---------------------------------------------------------------------------\n"
    "# Saves\n"
    "# ---------------------------------------------------------------------------\n"
    "#   Nothing in the save format changes, and existing characters and stashes\n"
    "#   load exactly as before.\n"
    "#   - The affixes are saved as ordinary item stats, as they always were.\n"
    "#   - The item also saves which row it rolled, in a 3-bit field (0 to 7;\n"
    "#     any higher row is written as 7). The game never reads that number back\n"
    "#     for anything: the only code that turns a row number into affixes is\n"
    "#     the picker itself, when the item is created. So a Superior item that\n"
    "#     rolled row 200 keeps its row-200 affixes after a reload; only that\n"
    "#     unused label says 7.\n"
    "#\n"
    "# ---------------------------------------------------------------------------\n"
    "# Which rows an item may use (stock rules, unchanged by this plugin)\n"
    "# ---------------------------------------------------------------------------\n"
    "#   The columns test the item's OWN type, the \"type\" column of weapons.txt or\n"
    "#   armor.txt, not its parent types:\n"
    "#   - weapon: every weapon EXCEPT own type scep, wand, staf, bow, xbow. So\n"
    "#     Amazon bows (abow), orbs, claws, Amazon spears and javelins, and\n"
    "#     throwing weapons all use the weapon column.\n"
    "#   - armor: every armor EXCEPT own type shie, boot, glov, belt. So Paladin\n"
    "#     shields, Necromancer heads, helms, circlets, pelts and body armor all\n"
    "#     use the armor column.\n"
    "#   - shield, scepter, wand, staff, boots, gloves, belt: only items whose own\n"
    "#     type is exactly shie, scep, wand, staf, boot, glov, belt.\n"
    "#   - bow: own type exactly bow or xbow. Amazon bows (abow) do NOT count.\n"
    "#   (The game checks these by itemtypes.txt row position. The codes above\n"
    "#   are the vanilla rows at those positions.)\n"
    "#\n"
    "#   mod2 is applied only when mod1code is filled in. A row with an empty\n"
    "#   mod1code still makes the item Superior, with no bonus at all.\n"
    "#\n"
    "# ---------------------------------------------------------------------------\n"
    "# Rows are numbered from 0\n"
    "# ---------------------------------------------------------------------------\n"
    "#   The console command and the log name rows by number. Row 0 is the first\n"
    "#   line under the header of qualityitems.txt, row 1 the second, and so on.\n"
    "#\n"
    "# ---------------------------------------------------------------------------\n"
    "# Item-type columns in qualityitems.txt (optional, no setting needed)\n"
    "# ---------------------------------------------------------------------------\n"
    "#   Add any of these columns to qualityitems.txt:\n"
    "#     itype1 itype2 itype3 itype4 itype5 itype6 itype7\n"
    "#     etype1 etype2 etype3 etype4 etype5\n"
    "#   They take itemtypes.txt codes and work exactly like the same columns of\n"
    "#   magicprefix.txt: an item matches a code when its type, or its type2, is\n"
    "#   that type or any type below it in itemtypes.txt (Equiv1/Equiv2).\n"
    "#   - A row with any itype filled rolls only on items matching one of its\n"
    "#     itypes; its armor..belt columns are then ignored.\n"
    "#   - A row with no itype keeps the armor..belt columns.\n"
    "#   - etype always excludes: a row never rolls on an item matching one of its\n"
    "#     etypes, whichever way the row includes items.\n"
    "#   Example, spears only, without Amazon spears: itype1 = spea, etype1 = aspe.\n"
    "#   Example, every weapon the weapon column allows except spears:\n"
    "#     weapon = 1, etype1 = spea.\n"
    "#   An unknown code is skipped, and the log names its row and column. A row\n"
    "#   whose itypes are all unknown never rolls.\n"
    "#\n"
    "# ---------------------------------------------------------------------------\n"
    "# How the plugin reads the frequency, itype and etype columns\n"
    "# ---------------------------------------------------------------------------\n"
    "#   The game itself skips columns it does not know, so the file stays valid.\n"
    "#   The plugin reads the same file the game loaded and, before using it,\n"
    "#   checks it row by row against what the game loaded (row count, the ten\n"
    "#   item-class columns and every mod param/min/max). If anything differs,\n"
    "#   the log says exactly where, and none of these columns are used for that\n"
    "#   table: every row has the same odds and the armor..belt columns decide.\n"
    "#   Console command superior shows the result.\n"
    "#\n"
    "# Console command: superior (status, rows seen, items made, rolls per row)\n"
    "\n"
    "[superior_affixes]\n"
    "\n"
    "# Master switch. false installs nothing: the stock picker runs, with its\n"
    "# 10-row limit, and the frequency, itype and etype columns are not used.\n"
    "enabled = true\n"
    "\n"
    "# Stock rule: items whose own type has Throwable = 1 in itemtypes.txt, and\n"
    "# items with nodurability = 1 in weapons.txt or armor.txt, only ever look at\n"
    "# rows 0 to 3. true keeps the stock behaviour. false lets them use every row,\n"
    "# by the same rules and frequencies as any other item.\n"
    "first_four_rows_for_throwing_and_nodurability = true\n";

struct Settings {
    bool enabled       = true;
    bool firstFourRule = true;
};

struct ParseReport {
    std::uint32_t obsoleteLines = 0;   // default_weight and [superior_affixes.weights] (pre-1.2.0)
    std::uint32_t problems      = 0;
    std::string   sample;
};

// ---------------------------------------------------------------------------
// frequency / itype / etype data
// ---------------------------------------------------------------------------

constexpr std::size_t ItypeColumns = 7;
constexpr std::size_t EtypeColumns = 5;
constexpr std::size_t FlagColumns  = 10;
constexpr std::size_t ModColumns   = 6;

constexpr const char* FlagColumnNames[FlagColumns] = {
    "armor", "weapon", "shield", "scepter", "wand", "staff", "bow", "boots", "gloves", "belt",
};
constexpr const char* ModColumnNames[ModColumns] = {
    "mod1param", "mod1min", "mod1max", "mod2param", "mod2min", "mod2max",
};
// Record offsets of the six mod numbers, from the stock loader's descriptors.
constexpr std::uint32_t ModColumnOffsets[ModColumns] = { 16, 20, 24, 32, 36, 40 };

constexpr char FrequencyColumnName[] = "frequency";

struct ParsedRow {
    std::array<std::int32_t, FlagColumns>  flags{};
    std::array<std::int32_t, ModColumns>   mods{};
    std::array<std::string, ItypeColumns>  itypes;
    std::array<std::string, EtypeColumns>  etypes;
    std::uint32_t                          frequency = 0;   // 0 also when the cell is invalid
    bool                                   frequencyInvalid = false;
    std::string                            frequencyCell;   // the cell as written, when invalid
};

struct ParsedFile {
    std::string            path;            // UTF-8, for messages
    std::vector<ParsedRow> rows;
    bool                   hasTypeColumns = false;
    bool                   hasFrequencyColumn = false;
};

struct RowTypes {
    std::array<std::int16_t, ItypeColumns> itypes{};
    std::array<std::int16_t, EtypeColumns> etypes{};
    std::uint8_t                           itypeCount = 0;
    std::uint8_t                           etypeCount = 0;
    bool                                   includeByItype = false;   // any itype cell filled
};

struct BankBinding {
    D2RL::DataTables::Bank             bank{};
    const void*                        rows = nullptr;
    std::uint32_t                      rowCount = 0;
    std::vector<RowTypes>              types;
    std::uint32_t                      rowsUsingTypes = 0;
    std::vector<std::uint16_t>         frequency;              // per row; empty: no frequency column
    std::uint32_t                      rowsThatCanRoll = 0;    // frequency above 0
    std::uint32_t                      invalidFrequency = 0;   // cells that are not 0..65535
    std::string                        source;
    mutable std::atomic<const void*>   gameRows{};
};

struct BindingSet {
    std::vector<std::unique_ptr<BankBinding>> banks;
    std::string                               status;
};

// ---------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------

enum class HookState : std::uint8_t {
    NotAttempted,
    DisabledByConfig,
    UnsupportedBuild,
    InstallFailed,
    Installed,
};

const D2RL::PluginContext*                       Context{};
std::uintptr_t                                   Base{};
NativeCalls                                      Native{};
PickerFn                                         OriginalPicker{};
Settings                                         Config{};
bool                                             ConfigFileWasRead{};
std::atomic<HookState>                           State{ HookState::NotAttempted };
std::atomic<std::uint64_t>                       TableRowsSeen{};
std::atomic<bool>                                TableLogged{};
std::atomic<std::uint64_t>                       ItemsMade{};
std::atomic<std::uint64_t>                       NoUsableRow{};
std::atomic<std::uint64_t>                       NativeLookupFailed{};
std::array<std::atomic<std::uint32_t>, MaxRows>  RowRolls{};
const D2RL::LifecycleService*                    LifecycleApi{};
const D2RL::DataTableService*                    DataTableApi{};
D2RL::Lifecycle::ListenerHandle                  TablesListener{};
std::atomic<const BindingSet*>                   Bindings{};
std::vector<const BindingSet*>                   RetiredBindings;   // game thread only

// ---------------------------------------------------------------------------
// Text helpers
// ---------------------------------------------------------------------------

auto Trim(std::string_view value) noexcept -> std::string_view {
    std::size_t first = 0;
    while (first < value.size()
            && (value[first] == ' ' || value[first] == '\t'
                || value[first] == '\r' || value[first] == '\n')) {
        ++first;
    }
    std::size_t last = value.size();
    while (last > first
            && (value[last - 1] == ' ' || value[last - 1] == '\t'
                || value[last - 1] == '\r' || value[last - 1] == '\n')) {
        --last;
    }
    return value.substr(first, last - first);
}

auto EqualsNoCase(std::string_view left, std::string_view right) noexcept -> bool {
    if (left.size() != right.size()) return false;
    for (std::size_t index = 0; index < left.size(); ++index) {
        char a = left[index];
        char b = right[index];
        if (a >= 'A' && a <= 'Z') a = static_cast<char>(a - 'A' + 'a');
        if (b >= 'A' && b <= 'Z') b = static_cast<char>(b - 'A' + 'a');
        if (a != b) return false;
    }
    return true;
}

// The game's txt number rule: leading blanks, an optional sign, then digits
// up to the first character that is not one. An empty cell is 0.
auto ParseTxtInt(std::string_view cell) noexcept -> std::int32_t {
    std::size_t index = 0;
    while (index < cell.size() && (cell[index] == ' ' || cell[index] == '\t')) ++index;
    bool negative = false;
    if (index < cell.size() && (cell[index] == '-' || cell[index] == '+')) {
        negative = cell[index] == '-';
        ++index;
    }
    std::uint32_t value = 0;
    while (index < cell.size() && cell[index] >= '0' && cell[index] <= '9') {
        value = value * 10U + static_cast<std::uint32_t>(cell[index] - '0');
        ++index;
    }
    return negative ? static_cast<std::int32_t>(0U - value) : static_cast<std::int32_t>(value);
}

// ---------------------------------------------------------------------------
// Config parsing (small TOML subset, no external dependency)
// ---------------------------------------------------------------------------

auto ParseBool(std::string_view value, bool& out) noexcept -> bool {
    if (value == "true") { out = true; return true; }
    if (value == "false") { out = false; return true; }
    return false;
}

auto ParseUnsigned(std::string_view value, std::uint32_t maximum, std::uint32_t& out) noexcept
        -> bool {
    if (value.empty() || value.size() > 10) return false;
    std::uint64_t result = 0;
    for (const char c : value) {
        if (c < '0' || c > '9') return false;
        result = result * 10 + static_cast<std::uint64_t>(c - '0');
    }
    if (result > maximum) return false;
    out = static_cast<std::uint32_t>(result);
    return true;
}

void NoteProblem(ParseReport& report, std::string_view line) noexcept {
    ++report.problems;
    if (report.problems > ProblemSampleSize) return;
    if (!report.sample.empty()) report.sample += " | ";
    report.sample.append(line.data(), line.size() > 48 ? 48 : line.size());
}

void ParseConfig(std::string_view text, ParseReport& report) noexcept {
    std::string_view section;
    std::size_t cursor = 0;
    while (cursor < text.size()) {
        const std::size_t breakAt = text.find('\n', cursor);
        const std::size_t end = breakAt == std::string_view::npos ? text.size() : breakAt;
        std::string_view line = text.substr(cursor, end - cursor);
        cursor = end + 1;

        const std::size_t comment = line.find('#');
        if (comment != std::string_view::npos) line = line.substr(0, comment);
        line = Trim(line);
        if (line.empty()) continue;

        if (line.front() == '[') {
            if (line.back() != ']') { NoteProblem(report, line); section = {}; continue; }
            section = Trim(line.substr(1, line.size() - 2));
            continue;
        }

        const std::size_t equals = line.find('=');
        if (equals == std::string_view::npos) { NoteProblem(report, line); continue; }
        const std::string_view key = Trim(line.substr(0, equals));
        const std::string_view value = Trim(line.substr(equals + 1));

        if (section == MainSection) {
            if (key == "enabled") {
                if (!ParseBool(value, Config.enabled)) NoteProblem(report, line);
            } else if (key == "first_four_rows_for_throwing_and_nodurability") {
                if (!ParseBool(value, Config.firstFourRule)) NoteProblem(report, line);
            } else if (key == ObsoleteDefaultWeight) {
                ++report.obsoleteLines;
            } else {
                NoteProblem(report, line);
            }
        } else if (section == ObsoleteWeightsSection) {
            ++report.obsoleteLines;
        }
    }
}

auto ReadConfigText(std::string& out) noexcept -> bool {
    std::string buffer(InitialConfigBytes, '\0');
    std::uint32_t required = 0;
    if (!Context->ReadConfig(buffer.data(), static_cast<std::uint32_t>(buffer.size()), &required)) {
        if (required < buffer.size() || required >= MaximumConfigBytes) return false;
        buffer.assign(static_cast<std::size_t>(required) + 1, '\0');
        if (!Context->ReadConfig(buffer.data(), static_cast<std::uint32_t>(buffer.size()), &required)) {
            return false;
        }
    }
    buffer.resize(std::strlen(buffer.c_str()));
    out = std::move(buffer);
    return true;
}

void ReadConfiguration() noexcept {
    std::string text;
    if (!Context->EnsureConfig(DefaultConfigToml)) {
        Context->LogWarn("SuperiorAffixes: the config file could not be created; using the "
                         "embedded defaults.");
        text = DefaultConfigToml;
    } else if (!ReadConfigText(text)) {
        Context->LogWarn("SuperiorAffixes: the config file could not be read; using the "
                         "embedded defaults.");
        text = DefaultConfigToml;
    } else {
        ConfigFileWasRead = true;
    }

    ParseReport report{};
    ParseConfig(text, report);
    if (report.problems > 0) {
        D2RL::LogWarnF(Context,
            "SuperiorAffixes: %u config line%s ignored (%s%s). The settings are true or false.",
            report.problems, report.problems == 1 ? "" : "s", report.sample.c_str(),
            report.problems > ProblemSampleSize ? " | ..." : "");
    }
    if (report.obsoleteLines > 0) {
        D2RL::LogWarnF(Context,
            "SuperiorAffixes: %u config line%s ignored: default_weight and "
            "[superior_affixes.weights] are no longer read. Row odds come from the frequency "
            "column of qualityitems.txt.",
            report.obsoleteLines, report.obsoleteLines == 1 ? "" : "s");
    }
    D2RL::LogInfoF(Context,
        "SuperiorAffixes: config %s. First-four-rows rule %s. Row odds come from the frequency "
        "column of qualityitems.txt.",
        ConfigFileWasRead ? "loaded" : "NOT READ (embedded defaults)",
        Config.firstFourRule ? "on (stock)" : "off");
}

// ---------------------------------------------------------------------------
// qualityitems.txt: parse, prove it is the table the game loaded, resolve codes
// ---------------------------------------------------------------------------

// Splits one txt line on tabs into views over the line.
void SplitTabs(std::string_view line, std::vector<std::string_view>& cells) {
    cells.clear();
    std::size_t start = 0;
    while (true) {
        const std::size_t tab = line.find('\t', start);
        if (tab == std::string_view::npos) {
            cells.push_back(line.substr(start));
            return;
        }
        cells.push_back(line.substr(start, tab - start));
        start = tab + 1;
    }
}

auto CellAt(const std::vector<std::string_view>& cells, int column) -> std::string_view {
    if (column < 0 || static_cast<std::size_t>(column) >= cells.size()) return {};
    return cells[static_cast<std::size_t>(column)];
}

// A frequency cell: empty is 0; otherwise a whole number from 0 to 65535 and
// nothing else (no sign, decimal point or text), blanks around it allowed.
auto ParseFrequency(std::string_view cell, std::uint32_t& out) noexcept -> bool {
    out = 0;
    const std::string_view value = Trim(cell);
    return value.empty() || ParseUnsigned(value, MaxFrequency, out);
}

// Header first, then one record per non-empty line, as the game reads it.
void ParseQualityItemsText(std::string_view text, ParsedFile& out) {
    std::vector<std::string_view> cells;
    std::size_t cursor = 0;
    bool header = true;
    std::array<int, FlagColumns>  flagColumn{};
    std::array<int, ModColumns>   modColumn{};
    std::array<int, ItypeColumns> itypeColumn{};
    std::array<int, EtypeColumns> etypeColumn{};
    int frequencyColumn = -1;
    flagColumn.fill(-1);
    modColumn.fill(-1);
    itypeColumn.fill(-1);
    etypeColumn.fill(-1);

    while (cursor < text.size()) {
        const std::size_t breakAt = text.find('\n', cursor);
        const std::size_t end = breakAt == std::string_view::npos ? text.size() : breakAt;
        std::string_view line = text.substr(cursor, end - cursor);
        cursor = end + 1;
        if (!line.empty() && line.back() == '\r') line.remove_suffix(1);

        if (header) {
            header = false;
            SplitTabs(line, cells);
            for (std::size_t column = 0; column < cells.size(); ++column) {
                const std::string_view name = Trim(cells[column]);
                const int index = static_cast<int>(column);
                for (std::size_t flag = 0; flag < FlagColumns; ++flag) {
                    if (EqualsNoCase(name, FlagColumnNames[flag])) flagColumn[flag] = index;
                }
                for (std::size_t mod = 0; mod < ModColumns; ++mod) {
                    if (EqualsNoCase(name, ModColumnNames[mod])) modColumn[mod] = index;
                }
                char columnName[8]{};
                for (std::size_t type = 0; type < ItypeColumns; ++type) {
                    std::snprintf(columnName, sizeof(columnName), "itype%zu", type + 1);
                    if (EqualsNoCase(name, columnName)) { itypeColumn[type] = index; out.hasTypeColumns = true; }
                }
                for (std::size_t type = 0; type < EtypeColumns; ++type) {
                    std::snprintf(columnName, sizeof(columnName), "etype%zu", type + 1);
                    if (EqualsNoCase(name, columnName)) { etypeColumn[type] = index; out.hasTypeColumns = true; }
                }
                if (EqualsNoCase(name, FrequencyColumnName)) {
                    frequencyColumn = index;
                    out.hasFrequencyColumn = true;
                }
            }
            continue;
        }
        if (line.empty()) continue;

        SplitTabs(line, cells);
        ParsedRow row;
        for (std::size_t flag = 0; flag < FlagColumns; ++flag) {
            row.flags[flag] = ParseTxtInt(CellAt(cells, flagColumn[flag]));
        }
        for (std::size_t mod = 0; mod < ModColumns; ++mod) {
            row.mods[mod] = ParseTxtInt(CellAt(cells, modColumn[mod]));
        }
        for (std::size_t type = 0; type < ItypeColumns; ++type) {
            row.itypes[type] = std::string(Trim(CellAt(cells, itypeColumn[type])));
        }
        for (std::size_t type = 0; type < EtypeColumns; ++type) {
            row.etypes[type] = std::string(Trim(CellAt(cells, etypeColumn[type])));
        }
        if (frequencyColumn >= 0) {
            const std::string_view cell = Trim(CellAt(cells, frequencyColumn));
            if (!ParseFrequency(cell, row.frequency)) {
                row.frequency = 0;
                row.frequencyInvalid = true;
                row.frequencyCell = std::string(cell.substr(0, cell.size() > 24 ? 24 : cell.size()));
            }
        }
        out.rows.push_back(std::move(row));
    }
}

// True when the file holds exactly the rows the game compiled into `rows`.
auto FileMatchesTable(const ParsedFile& file, const std::uint8_t* rows, std::uint32_t rowCount,
        std::string& why) -> bool {
    char text[256]{};
    if (file.rows.size() != rowCount) {
        std::snprintf(text, sizeof(text), "%zu data rows, the game loaded %u",
            file.rows.size(), rowCount);
        why = text;
        return false;
    }
    for (std::uint32_t index = 0; index < rowCount; ++index) {
        const std::uint8_t* record = rows + static_cast<std::size_t>(index) * QualityItemsRowSize;
        const ParsedRow& row = file.rows[index];
        for (std::size_t flag = 0; flag < FlagColumns; ++flag) {
            const auto expected = static_cast<std::uint8_t>(row.flags[flag] & 0xFF);
            if (record[flag] != expected) {
                std::snprintf(text, sizeof(text), "row %u %s: file %d, game %u",
                    index, FlagColumnNames[flag], row.flags[flag], record[flag]);
                why = text;
                return false;
            }
        }
        for (std::size_t mod = 0; mod < ModColumns; ++mod) {
            std::int32_t loaded = 0;
            std::memcpy(&loaded, record + ModColumnOffsets[mod], sizeof(loaded));
            if (loaded != row.mods[mod]) {
                std::snprintf(text, sizeof(text), "row %u %s: file %d, game %d",
                    index, ModColumnNames[mod], row.mods[mod], loaded);
                why = text;
                return false;
            }
        }
    }
    return true;
}

auto PackCode(std::string_view code, char pad) noexcept -> std::uint32_t {
    std::uint32_t packed = 0;
    for (std::size_t index = 0; index < 4; ++index) {
        const char c = index < code.size() ? code[index] : pad;
        packed |= static_cast<std::uint32_t>(static_cast<std::uint8_t>(c)) << (8U * index);
    }
    return packed;
}

// itemtypes.txt code -> itemtypes index in `bank`, or -1.
auto ResolveItemType(D2RL::DataTables::Bank bank, std::string_view code) noexcept -> std::int32_t {
    if (DataTableApi == nullptr || code.empty() || code.size() > 4) return -1;
    for (const char pad : { ' ', '\0' }) {
        D2RL::DataTables::RowView view{};
        view.structSize = D2RL::DataTables::RowViewSize;
        if (DataTableApi->findRowByCode(Context, bank, D2RL::DataTables::TableId::ItemTypes,
                PackCode(code, pad), &view) == D2RL::DataTables::Result::Success
                && D2RL::DataTables::HasRowViewField(&view, D2RL::DataTables::RowViewRequiredSize)
                && view.rowIndex <= 0x7FFF) {
            return static_cast<std::int32_t>(view.rowIndex);
        }
    }
    return -1;
}

auto BankName(D2RL::DataTables::Bank bank) noexcept -> const char* {
    switch (bank) {
    case D2RL::DataTables::Bank::Classic: return "Classic";
    case D2RL::DataTables::Bank::Lod:     return "LoD";
    case D2RL::DataTables::Bank::Rotw:    return "RotW";
    default:                              return "?";
    }
}

auto WideToUtf8(const std::wstring& text) -> std::string {
#ifdef _WIN32
    if (text.empty()) return {};
    const int size = WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()),
        nullptr, 0, nullptr, nullptr);
    if (size <= 0) return {};
    std::string out(static_cast<std::size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), out.data(), size,
        nullptr, nullptr);
    return out;
#else
    std::string out;
    for (const wchar_t c : text) out.push_back(c < 128 ? static_cast<char>(c) : '?');
    return out;
#endif
}

auto Utf8ToWide(const char* text) -> std::wstring {
    if (text == nullptr || *text == '\0') return {};
#ifdef _WIN32
    const int size = MultiByteToWideChar(CP_UTF8, 0, text, -1, nullptr, 0);
    if (size <= 1) return {};
    std::wstring out(static_cast<std::size_t>(size - 1), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text, -1, out.data(), size);
    return out;
#else
    std::wstring out;
    for (const char* c = text; *c; ++c) out.push_back(static_cast<wchar_t>(*c));
    return out;
#endif
}

auto ReadWholeFile(const std::wstring& path, std::string& out) -> bool {
#ifdef _WIN32
    const HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
        nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER size{};
    bool ok = GetFileSizeEx(file, &size) != 0 && size.QuadPart >= 0 && size.QuadPart < (64LL << 20);
    if (ok) {
        out.assign(static_cast<std::size_t>(size.QuadPart), '\0');
        DWORD read = 0;
        ok = out.empty() || (ReadFile(file, out.data(), static_cast<DWORD>(out.size()), &read, nullptr) != 0
            && read == out.size());
    }
    CloseHandle(file);
    return ok;
#else
    (void)path;
    (void)out;
    return false;
#endif
}

void AddCandidateRoot(std::vector<std::wstring>& roots, std::wstring root) {
    while (!root.empty() && (root.back() == L'\\' || root.back() == L'/')) root.pop_back();
    if (root.empty()) return;
    for (const std::wstring& existing : roots) {
        if (existing.size() == root.size()) {
            bool same = true;
            for (std::size_t index = 0; index < root.size() && same; ++index) {
                wchar_t a = existing[index];
                wchar_t b = root[index];
                if (a >= L'A' && a <= L'Z') a = static_cast<wchar_t>(a - L'A' + L'a');
                if (b >= L'A' && b <= L'Z') b = static_cast<wchar_t>(b - L'A' + L'a');
                if (a == L'/') a = L'\\';
                if (b == L'/') b = L'\\';
                same = a == b;
            }
            if (same) return;
        }
    }
    roots.push_back(std::move(root));
}

// Every qualityitems.txt the game can have loaded for this mod, parsed.
void LoadCandidateFiles(std::vector<ParsedFile>& files, std::string& searched) {
    std::vector<std::wstring> roots;
    const std::wstring mod = Utf8ToWide(Context->activeMod);
    for (const wchar_t* folder : { Context->modDirectory, Context->scopeRootDirectory }) {
        if (folder == nullptr || *folder == L'\0') continue;
        const std::wstring root(folder);
        AddCandidateRoot(roots, root);
        if (!mod.empty()) AddCandidateRoot(roots, root + L"\\" + mod + L".mpq");
    }
    for (const std::wstring& root : roots) {
        for (const wchar_t* relative : { L"\\data\\global\\excel\\qualityitems.txt",
                                         L"\\data\\global\\excel\\base\\qualityitems.txt" }) {
            const std::wstring path = root + relative;
            if (!searched.empty()) searched += " | ";
            searched += WideToUtf8(path);
            std::string text;
            if (!ReadWholeFile(path, text)) continue;
            ParsedFile file;
            file.path = WideToUtf8(path);
            ParseQualityItemsText(text, file);
            files.push_back(std::move(file));
        }
    }
}

void PublishBindings(std::unique_ptr<BindingSet> set) {
    const BindingSet* previous = Bindings.exchange(set.release(), std::memory_order_acq_rel);
    if (previous != nullptr) RetiredBindings.push_back(previous);
}

struct TableBankView {
    D2RL::DataTables::Bank bank{};
    const void*            rows = nullptr;
    std::uint32_t          rowCount = 0;
    std::uint32_t          rowSize = 0;
};

// The qualityitems table of every bank the game has loaded.
void CollectTableViews(std::vector<TableBankView>& views) {
    constexpr D2RL::DataTables::Bank Banks[] = {
        D2RL::DataTables::Bank::Classic, D2RL::DataTables::Bank::Lod, D2RL::DataTables::Bank::Rotw,
    };
    for (const D2RL::DataTables::Bank bank : Banks) {
        D2RL::DataTables::TableView view{};
        view.structSize = D2RL::DataTables::TableViewSize;
        if (DataTableApi->getTable(Context, bank, D2RL::DataTables::TableId::QualityItems, &view)
                != D2RL::DataTables::Result::Success
                || !D2RL::DataTables::HasTableViewField(&view, D2RL::DataTables::TableViewRequiredSize)
                || view.rows == nullptr || view.rowCount == 0) {
            continue;
        }
        views.push_back({ bank, view.rows, view.rowCount, view.rowSize });
    }
}

auto AppendUnknownCode(std::string& sample, std::uint32_t& unknown, std::uint32_t row,
        const char* column, std::size_t columnNumber, const std::string& code) -> void {
    if (++unknown > ProblemSampleSize) return;
    char text[96]{};
    std::snprintf(text, sizeof(text), "%srow %u %s%zu '%s'", sample.empty() ? "" : ", ", row,
        column, columnNumber, code.c_str());
    sample += text;
}

// Binds each bank's table to the qualityitems.txt that matches it, codes resolved.
void BuildFromViews(const std::vector<TableBankView>& views, const std::vector<ParsedFile>& files,
        const std::string& searched) {
    auto set = std::make_unique<BindingSet>();
    std::string status = "qualityitems columns:";
    const auto separator = [&status]() { status += status.back() == ':' ? " " : " | "; };
    for (const TableBankView& view : views) {
        const char* bankName = BankName(view.bank);
        if (view.rowSize != QualityItemsRowSize) {
            D2RL::LogWarnF(Context, "SuperiorAffixes: %s qualityitems rows are %u bytes, not %u; its "
                "frequency, itype and etype columns are not used, so every row has the same odds.",
                bankName, view.rowSize, QualityItemsRowSize);
            separator();
            status += bankName;
            status += " unexpected row size, not used (equal odds)";
            continue;
        }

        const auto* rows = static_cast<const std::uint8_t*>(view.rows);
        const ParsedFile* match = nullptr;
        std::string reasons;
        for (const ParsedFile& file : files) {
            std::string why;
            if (FileMatchesTable(file, rows, view.rowCount, why)) { match = &file; break; }
            if (!reasons.empty()) reasons += "; ";
            reasons += file.path + ": " + why;
        }
        if (match == nullptr) {
            const std::string detail = files.empty() ? "Looked in: " + searched : reasons;
            D2RL::LogWarnF(Context, "SuperiorAffixes: %s qualityitems: no qualityitems.txt matched "
                "what the game loaded, so its frequency, itype and etype columns are not used and "
                "every row has the same odds. %s", bankName, detail.c_str());
            separator();
            status += bankName;
            status += files.empty() ? " file not found, see the log (equal odds)"
                                    : " file differs from the game's table, see the log (equal odds)";
            continue;
        }

        auto binding = std::make_unique<BankBinding>();
        binding->bank = view.bank;
        binding->rows = view.rows;
        binding->rowCount = view.rowCount;
        binding->source = match->path;
        binding->types.resize(view.rowCount);

        std::uint32_t unknownCodes = 0;
        std::string unknownSample;
        for (std::uint32_t index = 0; index < view.rowCount; ++index) {
            const ParsedRow& row = match->rows[index];
            RowTypes& types = binding->types[index];
            for (std::size_t column = 0; column < ItypeColumns; ++column) {
                const std::string& code = row.itypes[column];
                if (code.empty()) continue;
                types.includeByItype = true;
                const std::int32_t resolved = ResolveItemType(view.bank, code);
                if (resolved < 0) {
                    AppendUnknownCode(unknownSample, unknownCodes, index, "itype", column + 1, code);
                    continue;
                }
                types.itypes[types.itypeCount++] = static_cast<std::int16_t>(resolved);
            }
            for (std::size_t column = 0; column < EtypeColumns; ++column) {
                const std::string& code = row.etypes[column];
                if (code.empty()) continue;
                const std::int32_t resolved = ResolveItemType(view.bank, code);
                if (resolved < 0) {
                    AppendUnknownCode(unknownSample, unknownCodes, index, "etype", column + 1, code);
                    continue;
                }
                types.etypes[types.etypeCount++] = static_cast<std::int16_t>(resolved);
            }
            if (types.includeByItype || types.etypeCount > 0) ++binding->rowsUsingTypes;
        }

        if (unknownCodes > 0) {
            D2RL::LogErrorF(Context, "SuperiorAffixes: %s qualityitems: %u itemtypes code%s not found "
                "(%s%s). An unknown itype is skipped, so a row whose itypes are all unknown never rolls; "
                "an unknown etype is skipped.", bankName, unknownCodes,
                unknownCodes == 1 ? " was" : "s were", unknownSample.c_str(),
                unknownCodes > ProblemSampleSize ? ", ..." : "");
        }
        const std::int32_t weapon = ResolveItemType(view.bank, "weap");
        const std::int32_t armor = ResolveItemType(view.bank, "armo");
        if (weapon != WeaponTypeIndex || armor != ArmorTypeIndex) {
            D2RL::LogWarnF(Context, "SuperiorAffixes: %s itemtypes.txt has weap at %d and armo at %d; "
                "the game's own weapon/armor columns are hardcoded to %d and %d.", bankName, weapon,
                armor, WeaponTypeIndex, ArmorTypeIndex);
        }
        std::uint32_t neverRoll = 0;
        std::string neverRollSample;
        std::string invalidSample;
        if (match->hasFrequencyColumn) {
            binding->frequency.resize(view.rowCount);
            for (std::uint32_t index = 0; index < view.rowCount; ++index) {
                const ParsedRow& row = match->rows[index];
                binding->frequency[index] = static_cast<std::uint16_t>(row.frequency);
                if (row.frequency > 0) {
                    ++binding->rowsThatCanRoll;
                    continue;
                }
                if (++neverRoll <= RowListSampleSize) {
                    char entry[16]{};
                    std::snprintf(entry, sizeof(entry), "%s%u", neverRollSample.empty() ? "" : ", ",
                        index);
                    neverRollSample += entry;
                }
                if (row.frequencyInvalid && ++binding->invalidFrequency <= ProblemSampleSize) {
                    char entry[64]{};
                    std::snprintf(entry, sizeof(entry), "%srow %u '%s'",
                        invalidSample.empty() ? "" : ", ", index, row.frequencyCell.c_str());
                    invalidSample += entry;
                }
            }
        }
        if (binding->invalidFrequency > 0) {
            D2RL::LogErrorF(Context, "SuperiorAffixes: %s qualityitems: %u frequency cell%s not a "
                "whole number from 0 to 65535 (%s%s). Such a row never rolls.", bankName,
                binding->invalidFrequency, binding->invalidFrequency == 1 ? " is" : "s are",
                invalidSample.c_str(), binding->invalidFrequency > ProblemSampleSize ? ", ..." : "");
        }
        if (neverRoll > 0) {
            D2RL::LogInfoF(Context, "SuperiorAffixes: %s qualityitems: %u row%s never roll%s "
                "(frequency 0, empty or invalid): %s%s.", bankName, neverRoll,
                neverRoll == 1 ? "" : "s", neverRoll == 1 ? "s" : "", neverRollSample.c_str(),
                neverRoll > RowListSampleSize ? ", ..." : "");
        }

        char frequencyText[96]{};
        if (match->hasFrequencyColumn) {
            std::snprintf(frequencyText, sizeof(frequencyText), "frequency: %u of %u rows can roll",
                binding->rowsThatCanRoll, binding->rowCount);
        } else {
            std::snprintf(frequencyText, sizeof(frequencyText),
                "no frequency column, so every row has the same odds");
        }
        D2RL::LogInfoF(Context, "SuperiorAffixes: %s qualityitems: %s matches the loaded table "
            "(%u rows); %s; %u row%s use itype/etype%s.", bankName, binding->source.c_str(),
            binding->rowCount, frequencyText, binding->rowsUsingTypes,
            binding->rowsUsingTypes == 1 ? "" : "s",
            match->hasTypeColumns ? "" : " (no itype/etype columns)");

        char text[192]{};
        separator();
        if (match->hasFrequencyColumn) {
            std::snprintf(text, sizeof(text), "%s frequency %u of %u rows can roll%s, %u row%s use "
                "itype/etype%s", bankName, binding->rowsThatCanRoll, binding->rowCount,
                binding->invalidFrequency > 0 ? " (invalid cells, see the log)" : "",
                binding->rowsUsingTypes, binding->rowsUsingTypes == 1 ? "" : "s",
                unknownCodes > 0 ? " (unknown codes, see the log)" : "");
        } else {
            std::snprintf(text, sizeof(text), "%s no frequency column (equal odds), %u row%s use "
                "itype/etype%s", bankName, binding->rowsUsingTypes,
                binding->rowsUsingTypes == 1 ? "" : "s",
                unknownCodes > 0 ? " (unknown codes, see the log)" : "");
        }
        status += text;
        set->banks.push_back(std::move(binding));
    }
    set->status = std::move(status);
    PublishBindings(std::move(set));
}

// Runs on the game thread when D2RLoader has finished loading the tables.
void BuildBindings() {
    if (DataTableApi == nullptr) {
        auto set = std::make_unique<BindingSet>();
        set->status = "qualityitems columns: off, D2RLoader's data table service is unavailable "
                      "(equal odds)";
        Context->LogWarn("SuperiorAffixes: the data table service is unavailable, so the frequency, "
                         "itype and etype columns cannot be used; every row has the same odds.");
        PublishBindings(std::move(set));
        return;
    }
    std::vector<TableBankView> views;
    CollectTableViews(views);
    if (views.empty()) {
        // Tables not loaded yet, or not on the game thread: the listener builds them.
        if (Bindings.load(std::memory_order_acquire) == nullptr) {
            auto set = std::make_unique<BindingSet>();
            set->status = "qualityitems columns: waiting for the game's tables";
            PublishBindings(std::move(set));
        }
        return;
    }
    std::vector<ParsedFile> files;
    std::string searched;
    LoadCandidateFiles(files, searched);
    BuildFromViews(views, files, searched);
}

void __cdecl OnDataTablesLoaded(const D2RL::PluginContext*, const D2RL::Lifecycle::DataTablesLoadedEvent*,
        void*) noexcept {
    try {
        BuildBindings();
    } catch (...) {
        Context->LogError("SuperiorAffixes: reading the frequency, itype and etype columns failed "
                          "(out of memory); equal odds and the stock item-class columns are used.");
    }
}

// The binding for the table the picker got, matched by the compiled rows.
auto FindBinding(const QualityItemsTable* table) noexcept -> const BankBinding* {
    const BindingSet* set = Bindings.load(std::memory_order_acquire);
    if (set == nullptr || table == nullptr || table->rows == nullptr) return nullptr;
    for (const auto& binding : set->banks) {
        if (binding->rowCount != table->count) continue;
        if (binding->rows == table->rows
                || binding->gameRows.load(std::memory_order_relaxed) == table->rows) {
            return binding.get();
        }
    }
    for (const auto& binding : set->banks) {
        if (binding->rowCount != table->count) continue;
        if (std::memcmp(binding->rows, table->rows,
                static_cast<std::size_t>(binding->rowCount) * QualityItemsRowSize) == 0) {
            binding->gameRows.store(table->rows, std::memory_order_relaxed);
            return binding.get();
        }
    }
    return nullptr;
}

// ---------------------------------------------------------------------------
// The picker
// ---------------------------------------------------------------------------

// The stock generator, 58BAB0..58BAEB: low*0x6AC690C5 + high, keep both halves.
auto NextRandom(std::uint32_t* seed) noexcept -> std::uint32_t {
    const std::uint64_t next = static_cast<std::uint64_t>(seed[0]) * SeedMultiplier + seed[1];
    seed[0] = static_cast<std::uint32_t>(next);
    seed[1] = static_cast<std::uint32_t>(next >> 32);
    return seed[0];
}

void NoteTableSize(std::uint64_t rows) noexcept {
    TableRowsSeen.store(rows, std::memory_order_relaxed);
    if (TableLogged.exchange(true, std::memory_order_acq_rel)) return;
    if (rows > MaxRows) {
        D2RL::LogErrorF(Context,
            "SuperiorAffixes: qualityitems.txt has %llu rows; only rows 0 to %u can be picked.",
            static_cast<unsigned long long>(rows), MaxRows - 1);
    } else {
        D2RL::LogInfoF(Context, "SuperiorAffixes: first Superior item of the session; "
            "qualityitems.txt has %llu rows.", static_cast<unsigned long long>(rows));
    }
}

auto AnyTypeMatches(void* item, const std::int16_t* types, std::uint8_t count) noexcept -> bool {
    for (std::uint8_t index = 0; index < count; ++index) {
        if (Native.checkItemType(item, types[index]) != 0) return true;
    }
    return false;
}

auto RowUsable(std::uint8_t bank, void* item, std::uint32_t row, const BankBinding* binding,
        const void*& record) noexcept -> bool {
    record = Native.getRecord(bank, row);
    if (record == nullptr) return false;
    if (binding == nullptr || row >= binding->types.size()) {
        return Native.canBeSuperior(item, record) != 0;
    }
    const RowTypes& types = binding->types[row];
    const bool included = types.includeByItype
        ? AnyTypeMatches(item, types.itypes.data(), types.itypeCount)
        : Native.canBeSuperior(item, record) != 0;
    if (!included) return false;
    return !AnyTypeMatches(item, types.etypes.data(), types.etypeCount);
}

// The odds of `row` in this table: its frequency cell, or the same for every
// row when no qualityitems.txt matched the table or the matched file has no
// frequency column (stock odds).
auto RowFrequency(const BankBinding* binding, std::uint32_t row) noexcept -> std::uint32_t {
    if (binding == nullptr || binding->frequency.empty() || row >= binding->frequency.size()) {
        return EqualOddsFrequency;
    }
    return binding->frequency[row];
}

auto PickSuperiorAffix(void* item, void* drop) noexcept -> std::int64_t {
    const std::uint8_t bank = Native.getItemBank(item);
    const QualityItemsTable* table = Native.getTable(bank);
    const std::uint32_t classId = Native.getClassId(item, Native.classIdSourceFile,
                                                    ClassIdSourceLine);
    const std::uint8_t* itemsRecord = Native.getItemsRecord(bank, classId);
    if (table == nullptr || itemsRecord == nullptr) {
        NativeLookupFailed.fetch_add(1, std::memory_order_relaxed);
        return 0;
    }

    NoteTableSize(table->count);
    const BankBinding* binding = FindBinding(table);
    std::uint64_t rows = table->count < MaxRows ? table->count : MaxRows;
    if (Config.firstFourRule
            && (Native.isThrowable(item) != 0 || itemsRecord[ItemsNoDurabilityOffset] != 0)
            && rows > StockFirstRows) {
        rows = StockFirstRows;
    }
    const auto rowCount = static_cast<std::uint32_t>(rows);

    std::uint32_t total = 0;
    const void* record = nullptr;
    for (std::uint32_t row = 0; row < rowCount; ++row) {
        const std::uint32_t frequency = RowFrequency(binding, row);
        if (frequency != 0 && RowUsable(bank, item, row, binding, record)) total += frequency;
    }
    if (total == 0) {
        NoUsableRow.fetch_add(1, std::memory_order_relaxed);
        return 0;
    }

    std::uint32_t* seed = Native.getItemSeed(item);
    if (seed == nullptr) {
        NativeLookupFailed.fetch_add(1, std::memory_order_relaxed);
        return 0;
    }
    std::uint32_t roll = NextRandom(seed) % total;

    for (std::uint32_t row = 0; row < rowCount; ++row) {
        const std::uint32_t frequency = RowFrequency(binding, row);
        if (frequency == 0 || !RowUsable(bank, item, row, binding, record)) continue;
        if (roll >= frequency) {
            roll -= frequency;
            continue;
        }
        Native.setFileIndex(item, row);
        Native.assignProperties(QualityItemsPropertyMode, nullptr, item, record, 0, 0);
        Native.staffMods(item, drop);
        RowRolls[row].fetch_add(1, std::memory_order_relaxed);
        ItemsMade.fetch_add(1, std::memory_order_relaxed);
        return 1;
    }

    // Unreachable: the second pass sees exactly the rows the first one summed.
    NativeLookupFailed.fetch_add(1, std::memory_order_relaxed);
    return 0;
}

// ---------------------------------------------------------------------------
// Installation
// ---------------------------------------------------------------------------

void BindNatives() noexcept {
    Native.getItemBank       = reinterpret_cast<GetItemBankFn>(Base + GetItemBankRva);
    Native.getTable          = reinterpret_cast<GetQualityItemsTableFn>(Base + GetQualityItemsTableRva);
    Native.getClassId        = reinterpret_cast<GetClassIdFn>(Base + GetClassIdRva);
    Native.classIdSourceFile = reinterpret_cast<const char*>(Base + ClassIdSourceFileRva);
    Native.getItemsRecord    = reinterpret_cast<GetItemsRecordFn>(Base + GetItemsRecordRva);
    Native.isThrowable       = reinterpret_cast<IsThrowableFn>(Base + IsThrowableRva);
    Native.getItemSeed       = reinterpret_cast<GetItemSeedFn>(Base + GetItemSeedRva);
    Native.getRecord         = reinterpret_cast<GetQualityItemsRecordFn>(Base + GetQualityItemsRecordRva);
    Native.canBeSuperior     = reinterpret_cast<CanBeSuperiorFn>(Base + CanBeSuperiorRva);
    Native.setFileIndex      = reinterpret_cast<SetFileIndexFn>(Base + SetFileIndexRva);
    Native.assignProperties  = reinterpret_cast<AssignPropertiesFn>(Base + AssignPropertiesRva);
    Native.staffMods         = reinterpret_cast<StaffModsFn>(Base + StaffModsRva);
    Native.checkItemType     = reinterpret_cast<CheckItemTypeFn>(Base + CheckItemTypeRva);
}

void RegisterTablesListener() noexcept {
    if (Context->QueryService(&DataTableApi) != D2RL::ServiceQueryResult::Success
            || !D2RL::HasDataTableServiceField(DataTableApi, D2RL::DataTableServiceRequiredSize)) {
        DataTableApi = nullptr;
    }
    if (Context->QueryService(&LifecycleApi) != D2RL::ServiceQueryResult::Success
            || !D2RL::HasLifecycleServiceField(LifecycleApi, D2RL::LifecycleServiceRequiredSize)) {
        LifecycleApi = nullptr;
        Context->LogWarn("SuperiorAffixes: D2RLoader's lifecycle service is unavailable, so the "
                         "frequency, itype and etype columns are not read; equal odds and the stock "
                         "item-class columns are used.");
        return;
    }
    const D2RL::Lifecycle::DataTablesLoadedListener tables{
        .structSize = D2RL::Lifecycle::DataTablesLoadedListenerSize,
        .flags      = 0,
        .callback   = &OnDataTablesLoaded,
        .userData   = nullptr,
    };
    if (LifecycleApi->registerDataTablesLoadedListener(Context, &tables, &TablesListener)
            != D2RL::Lifecycle::Result::Success) {
        TablesListener = {};
        Context->LogWarn("SuperiorAffixes: the table-load listener was refused, so the frequency, "
                         "itype and etype columns are not read; equal odds and the stock item-class "
                         "columns are used.");
    }
}

void InstallHook() noexcept {
    if (!Config.enabled) {
        State.store(HookState::DisabledByConfig, std::memory_order_release);
        Context->LogInfo("SuperiorAffixes: not installed, turned off in the config file. The "
                         "stock picker runs, with its 10-row limit.");
        return;
    }
    if (!Context->CheckExpectedBytes(PickerRva, PickerWitness,
            static_cast<std::uint32_t>(sizeof(PickerWitness)))
            || !Context->CheckExpectedBytes(CheckItemTypeRva, CheckItemTypeWitness,
            static_cast<std::uint32_t>(sizeof(CheckItemTypeWitness)))) {
        State.store(HookState::UnsupportedBuild, std::memory_order_release);
        Context->LogError("SuperiorAffixes: NOT installed. The superior affix picker at 0x58B9E0 "
                          "or the item type check at 0x373890 does not match the verified D2R "
                          "image, or another plugin already patched it. Nothing was changed.");
        return;
    }
    BindNatives();
    if (!Context->InstallInlineHook(PickerRva, PickerWitness, HookSize,
            reinterpret_cast<void*>(&PickSuperiorAffix),
            reinterpret_cast<void**>(&OriginalPicker))) {
        State.store(HookState::InstallFailed, std::memory_order_release);
        Context->LogError("SuperiorAffixes: NOT installed. The inline hook on the superior "
                          "affix picker at 0x58B9E0 was refused.");
        return;
    }
    State.store(HookState::Installed, std::memory_order_release);
    Context->LogInfo("SuperiorAffixes: installed. Superior items now pick their "
                     "qualityitems.txt row by its frequency column, with no row limit below 65535.");
    RegisterTablesListener();
    try {
        BuildBindings();   // tables already loaded: built now; otherwise the listener builds them
    } catch (...) {
        Context->LogError("SuperiorAffixes: reading the frequency, itype and etype columns failed "
                          "(out of memory).");
    }
}

// ---------------------------------------------------------------------------
// Console command
// ---------------------------------------------------------------------------

auto StateText(HookState state) noexcept -> const char* {
    switch (state) {
    case HookState::Installed:        return "active";
    case HookState::DisabledByConfig: return "off in the config file (stock picker, 10-row limit)";
    case HookState::UnsupportedBuild: return "NOT ACTIVE, unrecognised game build or the picker is already patched, see the log";
    case HookState::InstallFailed:    return "NOT ACTIVE, the hook was refused, see the log";
    case HookState::NotAttempted:
    default:                          return "NOT ACTIVE, never attempted";
    }
}

auto __cdecl StatusCommand(D2R::Game::Client*, const D2RL::ConsoleCommandContext* command,
        void*) noexcept -> D2RL::ConsoleCommandResult {
    if (command == nullptr || command->plugin == nullptr) return D2RL::ConsoleCommandResult::Failed;
    const D2RL::PluginContext* plugin = command->plugin;

    const std::uint64_t rowsSeen = TableRowsSeen.load(std::memory_order_relaxed);
    char rowsText[48]{};
    if (TableLogged.load(std::memory_order_acquire)) {
        std::snprintf(rowsText, sizeof(rowsText), "%llu", static_cast<unsigned long long>(rowsSeen));
    } else {
        std::snprintf(rowsText, sizeof(rowsText), "not seen yet");
    }

    char header[512]{};
    std::snprintf(header, sizeof(header),
        "Superior Affixes: %s | config %s | first-four-rows rule %s | qualityitems rows %s | "
        "Superior items made %llu | no usable row %llu | native lookups failed %llu",
        StateText(State.load(std::memory_order_acquire)),
        ConfigFileWasRead ? "loaded" : "NOT READ (embedded defaults)",
        Config.firstFourRule ? "on" : "off", rowsText,
        static_cast<unsigned long long>(ItemsMade.load(std::memory_order_relaxed)),
        static_cast<unsigned long long>(NoUsableRow.load(std::memory_order_relaxed)),
        static_cast<unsigned long long>(NativeLookupFailed.load(std::memory_order_relaxed)));
    plugin->WriteConsoleMessage(header);

    const BindingSet* set = Bindings.load(std::memory_order_acquire);
    plugin->WriteConsoleMessage(set != nullptr ? set->status.c_str()
                                               : "qualityitems columns: not read yet");

    try {
        std::string line = "rolls by row:";
        bool any = false;
        const std::uint64_t limit = rowsSeen < MaxRows ? rowsSeen : MaxRows;
        for (std::uint32_t row = 0; row < limit; ++row) {
            const std::uint32_t rolls = RowRolls[row].load(std::memory_order_relaxed);
            if (rolls == 0) continue;
            char entry[40]{};
            std::snprintf(entry, sizeof(entry), " %u=%u", row, rolls);
            if (line.size() + std::strlen(entry) > 380) {
                plugin->WriteConsoleMessage(line.c_str());
                line = "rolls by row:";
            }
            line += entry;
            any = true;
        }
        plugin->WriteConsoleMessage(any ? line.c_str() : "rolls by row: none yet");
    } catch (...) {
        return D2RL::ConsoleCommandResult::Failed;
    }
    return D2RL::ConsoleCommandResult::Handled;
}

constexpr D2RL::PluginInfo Info{
    .infoSize    = D2RL::PluginInfoSize,
    .abiVersion  = D2RL_PLUGIN_ABI_VERSION,
    .id          = "celestialrayone.superior-affixes",
    .name        = "Superior Affixes",
    .version     = "1.2.0",
    .author      = "CelestialRayOne",
    .description = "Superior items pick their qualityitems.txt row by its frequency column, "
                   "with no row limit and optional itype/etype columns. Saves are unchanged.",
    // Server: items are generated by the server only.
    .flags       = D2RL::PluginFlags::Server | D2RL::PluginFlags::NativeHooks,
};

static_assert(D2RL::HasValidPluginRole(Info.flags), "Exactly one execution role must be set.");
static_assert(D2RL::HasOnlyKnownPluginFlags(Info.flags), "Unknown plugin flag set.");

}  // namespace

D2RL_PLUGIN_EXPORT auto D2RLoaderGetPluginInfo() noexcept -> const D2RL::PluginInfo* {
    return &Info;
}

D2RL_PLUGIN_EXPORT auto D2RLoaderLoadPlugin(const D2RL::PluginContext* context) noexcept -> bool {
    if (!D2RL::HasContext(context)) return false;
    Context = context;
    Base = context->exeBase;

    // Registered first, and the load always succeeds, so the command can always
    // explain the state.
    if (!Context->RegisterConsoleCommand("superior", &StatusCommand,
            "Reports Superior Affixes status, row count, qualityitems columns and rolls per row.")) {
        Context->LogWarn("SuperiorAffixes: the status console command was refused.");
    }

    ReadConfiguration();
    InstallHook();
    return true;
}

D2RL_PLUGIN_EXPORT void D2RLoaderUnloadPlugin() noexcept {
    if (Context == nullptr) return;
    if (LifecycleApi != nullptr && TablesListener != 0) {
        LifecycleApi->unregisterDataTablesLoadedListener(Context, TablesListener);
        TablesListener = 0;
    }
}

}  // namespace CelestialRayOne::SuperiorAffixes
