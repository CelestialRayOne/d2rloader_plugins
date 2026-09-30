// Restrict State Block (celestialrayone.restrict-state-block)
//
// Adds a fourth value to the skills.txt "restrict" column:
//
//   restrict 4   the skill cannot be used while the unit has any of the states
//                named in State1, State2 or State3. Empty State columns are
//                skipped. At every other time the skill is usable, exactly as
//                with restrict 1. The blocking states do not need the
//                states.txt restrict flag, so nothing else in the game changes
//                when they are applied, and value 4 never strips a state.
//
// Value 4 was inert before this plugin: every reader of the column treats any
// value other than 0 and 2 as "no check", and the only state-stripping reader
// compares against 3 exactly. Everything below was read out of the live
// D2RLoader.exe image (D2R 3.3) and disassembled before being relied on.
//
// ---------------------------------------------------------------------------
// The column
// ---------------------------------------------------------------------------
//   skills.txt loader 0x302380 builds its column descriptors inline:
//     30692B / 306936   "restrict"  type 4 (byte)          record +2D0h
//     306948 / 306953   "state1"    type 16h (state name)  record +2D2h
//     306965 / 306970   "state2"    type 16h               record +2D4h
//     306982 / 30698D   "state3"    type 16h               record +2D6h
//   states.txt loader 0x3074D0 makes the states.txt "restrict" column flag
//   bit 18 (3079CA); its group mask is DataTables+358h, the mask 0x336250
//   ("unit has any restrict-flagged state") tests.
//
// ---------------------------------------------------------------------------
// Readers of the column, and what value 4 does in each today
// ---------------------------------------------------------------------------
//   0x33F360  SKILLS_GetUseState(unit, skill) -> use state. Used by the client
//             (skill button tint, cast refusal and "Impossible" voice line)
//             and the server (player skill command validation). The restrict
//             block returns use state 4 on refusal:
//
//     33F496  48 8B 1F              mov   rbx, [rdi]            ; skills record
//     33F499  48 85 DB              test  rbx, rbx
//     33F49C  74 52                 je    33F4F0
//     33F49E  0F B6 8B D0 02 00 00  movzx ecx, byte [rbx+2D0h]  ; restrict
//     33F4A5  85 C9                 test  ecx, ecx
//     33F4A7  74 51                 je    33F4FA                ; 0: any restrict state refuses
//     33F4A9  83 E9 01              sub   ecx, 1                ; <- hook window, 10 bytes
//     33F4AC  74 58                 je    33F506                ; 1: carry on
//     33F4AE  83 F9 01              cmp   ecx, 1
//     33F4B1  75 53                 jne   33F506                ; 3, 4, 5...: carry on
//     33F4B3  48 8B CE              mov   rcx, rsi              ; 2: the State1-3 whitelist
//     ...                                                       ;    loop, calls 3351B0
//     33F4F0  B8 04 00 00 00        mov   eax, 4                ; refused
//     33F4F5  E9 28 FF FF FF        jmp   33F422                ; epilogue
//     33F4FA  ...                                               ; restrict 0
//     33F506  48 8B CE              mov   rcx, rsi              ; carry on with the next gates
//
//   0x33F6A0  the same check by skill id, (unit, skillId) -> bool. Its only
//             caller is SKILLITEM_HandleItemEffectSkill 0x589930 (item procs),
//             at 589A1C, and only when the row has ItemUseRestrict = 1:
//
//     33F6D0  0F B6 90 D0 02 00 00  movzx edx, byte [rax+2D0h]  ; restrict
//     33F6D7  85 D2                 test  edx, edx
//     33F6D9  74 69                 je    33F744                ; 0
//     33F6DB  83 EA 01              sub   edx, 1                ; <- hook window, 10 bytes
//     33F6DE  74 52                 je    33F732                ; 1: usable
//     33F6E0  83 EA 01              sub   edx, 1
//     33F6E3  75 4D                 jne   33F732                ; 3, 4, 5...: usable
//     33F6E5  48 8B CE              mov   rcx, rsi              ; 2: whitelist loop
//     33F720  32 C0 ...             xor   al, al / epilogue     ; refused
//     33F732  B0 01 ...             mov   al, 1 / epilogue      ; usable
//
//   0x436480 (server skill start, 4365FE) and 0x218030 (client, 218085) strip
//             every restrict-flagged state only when the byte is exactly 3.
//
// ---------------------------------------------------------------------------
// The hooks
// ---------------------------------------------------------------------------
//   Each 10-byte window becomes a jmp into a stub in a private region next to
//   the image, the rest NOPed. The window starts and ends on instruction
//   boundaries, is reached only by falling through from the je above it, and
//   no branch in either function lands inside it.
//
//   A stub starts with a dispatch through an 8-aligned qword:
//     armed     value 4 runs the new check, 2 goes to the stock whitelist, any
//               other value carries on, as stock;
//     vanilla   exactly the replaced instructions: 2 goes to the whitelist,
//               anything else carries on. The plugin points the dispatch here
//               on unload, with one atomic store, before restoring the window.
//   The value-4 check walks State1-3, skips a negative (empty) entry, and calls
//   the game's own STATES_CheckState 0x3351B0(unit, state), the function the
//   stock whitelist loop calls, so states above 511 work through whatever
//   relocation is installed. A state the unit has leaves through the stock
//   refusal (33F4F0 / 33F720); none leaves through the stock carry-on
//   (33F506 / 33F732).
//
//   Register use, checked against both functions:
//     use check   rsi = unit, rdi = skill, rbx = record, r14 = 0 (must stay 0,
//                 it is read at 33F515 and 33F58A). The stub uses ebp as the
//                 index and keeps rbx; the stock whitelist loop clobbers both,
//                 and both are dead on every exit (reloaded at 33F569/33F582/
//                 33F659 or restored by the epilogue). ecx and the flags are
//                 dead on every exit.
//     item check  rsi = unit, rdi = record, rbx = the skill id argument, dead
//                 here (the stock loop uses it as its index too). edx and the
//                 flags are dead on every exit.
//   Both functions keep rsp 16-aligned with 20h bytes of home space below
//   their saved registers, and call 3351B0 from exactly that frame, so the
//   stubs call it from the same frame without touching rsp.
//
//   Unwind data: the stubs run inside the game function's frame, so the region
//   registers UNWIND_INFO that describes that frame (the same codes as the
//   game's own chained entries 33F438-33F68E -> 33F360, and 33F6A0), and a
//   stack walk through a stub reaches the game function's caller correctly.
//
//   The stubs were assembled with keystone, decoded back with capstone, and
//   emulated with unicorn over the original 826 and 193 bytes: restrict 0-5
//   and 255, every State1-3 combination of {empty, 0, 139, 140, 794}, with and
//   without a restrict-flagged state, against six unit state sets (10,500
//   cases, 63,000 runs). Vanilla matches the stock code exactly in exit, call
//   sequence and registers for every value; armed matches it for every value
//   but 4, and for 4 refuses exactly when a listed state is present, with the
//   stack aligned at every call and rsi, rdi, rsp and r12-r15 preserved.
//
// ---------------------------------------------------------------------------
// Other plugins
// ---------------------------------------------------------------------------
//   petmax-fix hooks the entry of 0x33F360 and crossbow-charges the cooldown
//   gate 0x3404A0 it calls. Neither touches these windows, and no witness here
//   covers the entry of 0x33F360.

#include <D2RLPlugin/api.h>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <string_view>

namespace CelestialRayOne::RestrictStateBlock {
namespace {

// ---------------------------------------------------------------------------
// Native anchors (D2RLoader process image, D2R 3.3)
// ---------------------------------------------------------------------------

constexpr std::uint64_t UseCheckSavesRva     = 0x33F3A1;  // home saves of rbx, rbp, r14
constexpr std::uint64_t UseCheckEpilogueRva  = 0x33F422;  // shared epilogue
constexpr std::uint64_t UseCheckWitnessRva   = 0x33F496;
constexpr std::uint64_t UseCheckSiteRva      = 0x33F4A9;
constexpr std::uint64_t UseCheckMode2Rva     = 0x33F4B3;
constexpr std::uint64_t UseCheckRefuseRva    = 0x33F4F0;  // mov eax, 4
constexpr std::uint64_t UseCheckCarryOnRva   = 0x33F506;
constexpr std::uint64_t ItemCheckWitnessRva  = 0x33F6C3;
constexpr std::uint64_t ItemCheckSiteRva     = 0x33F6DB;
constexpr std::uint64_t ItemCheckMode2Rva    = 0x33F6E5;
constexpr std::uint64_t ItemCheckRefuseRva   = 0x33F720;  // xor al, al
constexpr std::uint64_t ItemCheckUsableRva   = 0x33F732;  // mov al, 1
constexpr std::uint64_t CheckStateRva        = 0x3351B0;  // STATES_CheckState(unit, state)

constexpr std::uint32_t SiteSize = 10;
constexpr std::uint32_t JumpSize = 5;

// 33F3A1: mov [rsp+40h],rbx / mov [rsp+50h],rbp / mov [rsp+20h],r14.
constexpr std::uint8_t UseCheckSavesWitness[]{
    0x48, 0x89, 0x5C, 0x24, 0x40, 0x48, 0x89, 0x6C, 0x24, 0x50, 0x4C, 0x89,
    0x74, 0x24, 0x20,
};

// 33F422: mov rbp,[rsp+50h] / mov rbx,[rsp+40h] / mov r14,[rsp+20h] /
// add rsp,28h / pop rdi / pop rsi / ret.
constexpr std::uint8_t UseCheckEpilogueWitness[]{
    0x48, 0x8B, 0x6C, 0x24, 0x50, 0x48, 0x8B, 0x5C, 0x24, 0x40, 0x4C, 0x8B,
    0x74, 0x24, 0x20, 0x48, 0x83, 0xC4, 0x28, 0x5F, 0x5E, 0xC3,
};

// 33F496..33F50E: record load, the whole restrict block, and the start of the
// carry-on path. The whitelist loop calls 3351B0 at 33F4DA.
constexpr std::uint8_t UseCheckWitness[]{
    0x48, 0x8B, 0x1F, 0x48, 0x85, 0xDB, 0x74, 0x52, 0x0F, 0xB6, 0x8B, 0xD0,
    0x02, 0x00, 0x00, 0x85, 0xC9, 0x74, 0x51, 0x83, 0xE9, 0x01, 0x74, 0x58,
    0x83, 0xF9, 0x01, 0x75, 0x53, 0x48, 0x8B, 0xCE, 0xE8, 0x95, 0x6D, 0xFF,
    0xFF, 0x85, 0xC0, 0x74, 0x31, 0x49, 0x8B, 0xEE, 0x48, 0x81, 0xC3, 0xD2,
    0x02, 0x00, 0x00, 0x0F, 0x1F, 0x80, 0x00, 0x00, 0x00, 0x00, 0x0F, 0xBF,
    0x13, 0x85, 0xD2, 0x78, 0x19, 0x48, 0x8B, 0xCE, 0xE8, 0xD1, 0x5C, 0xFF,
    0xFF, 0x85, 0xC0, 0x75, 0x23, 0x48, 0xFF, 0xC5, 0x48, 0x83, 0xC3, 0x02,
    0x48, 0x83, 0xFD, 0x03, 0x7C, 0xE0, 0xB8, 0x04, 0x00, 0x00, 0x00, 0xE9,
    0x28, 0xFF, 0xFF, 0xFF, 0x48, 0x8B, 0xCE, 0xE8, 0x4E, 0x6D, 0xFF, 0xFF,
    0x85, 0xC0, 0x75, 0xEA, 0x48, 0x8B, 0xCE, 0xE8, 0xC2, 0xC4, 0x00, 0x00,
};

// 33F6C3..33F761: from the record lookup's result to the end of the function,
// both returns included. The whitelist loop calls 3351B0 at 33F70A.
constexpr std::uint8_t ItemCheckWitness[]{
    0x48, 0x8B, 0xF8, 0x48, 0x85, 0xF6, 0x74, 0x55, 0x48, 0x85, 0xC0, 0x74,
    0x50, 0x0F, 0xB6, 0x90, 0xD0, 0x02, 0x00, 0x00, 0x85, 0xD2, 0x74, 0x69,
    0x83, 0xEA, 0x01, 0x74, 0x52, 0x83, 0xEA, 0x01, 0x75, 0x4D, 0x48, 0x8B,
    0xCE, 0xE8, 0x63, 0x6B, 0xFF, 0xFF, 0x85, 0xC0, 0x74, 0x2F, 0x33, 0xDB,
    0x48, 0x81, 0xC7, 0xD2, 0x02, 0x00, 0x00, 0x66, 0x0F, 0x1F, 0x44, 0x00,
    0x00, 0x0F, 0xBF, 0x17, 0x85, 0xD2, 0x78, 0x19, 0x48, 0x8B, 0xCE, 0xE8,
    0xA1, 0x5A, 0xFF, 0xFF, 0x85, 0xC0, 0x75, 0x1F, 0x48, 0xFF, 0xC3, 0x48,
    0x83, 0xC7, 0x02, 0x48, 0x83, 0xFB, 0x03, 0x7C, 0xE0, 0x32, 0xC0, 0x48,
    0x8B, 0x5C, 0x24, 0x30, 0x48, 0x8B, 0x74, 0x24, 0x38, 0x48, 0x83, 0xC4,
    0x20, 0x5F, 0xC3, 0xB0, 0x01, 0x48, 0x8B, 0x5C, 0x24, 0x30, 0x48, 0x8B,
    0x74, 0x24, 0x38, 0x48, 0x83, 0xC4, 0x20, 0x5F, 0xC3, 0x48, 0x8B, 0xCE,
    0xE8, 0x04, 0x6B, 0xFF, 0xFF, 0x48, 0x8B, 0x5C, 0x24, 0x30, 0x85, 0xC0,
    0x48, 0x8B, 0x74, 0x24, 0x38, 0x0F, 0x94, 0xC0, 0x48, 0x83, 0xC4, 0x20,
    0x5F, 0xC3,
};

constexpr std::size_t UseSiteInWitness   = UseCheckSiteRva - UseCheckWitnessRva;
constexpr std::size_t ItemSiteInWitness  = ItemCheckSiteRva - ItemCheckWitnessRva;
constexpr std::size_t UseCallInWitness   = 0x33F4DA - UseCheckWitnessRva;
constexpr std::size_t ItemCallInWitness  = 0x33F70A - ItemCheckWitnessRva;
constexpr const std::uint8_t* UseSiteOriginal  = UseCheckWitness + UseSiteInWitness;
constexpr const std::uint8_t* ItemSiteOriginal = ItemCheckWitness + ItemSiteInWitness;

constexpr auto ReadI32(const std::uint8_t* at) noexcept -> std::int32_t {
    return static_cast<std::int32_t>(static_cast<std::uint32_t>(at[0])
        | (static_cast<std::uint32_t>(at[1]) << 8)
        | (static_cast<std::uint32_t>(at[2]) << 16)
        | (static_cast<std::uint32_t>(at[3]) << 24));
}

// Target RVA of the E8 rel32 at `at` inside a witness that starts at `baseRva`.
constexpr auto CallTarget(const std::uint8_t* witness, std::uint64_t baseRva, std::size_t at) noexcept
        -> std::uint64_t {
    return static_cast<std::uint64_t>(static_cast<std::int64_t>(baseRva + at + 5) + ReadI32(witness + at + 1));
}

static_assert(sizeof(UseCheckWitness) == 0x33F50E - UseCheckWitnessRva);
static_assert(sizeof(ItemCheckWitness) == 0x33F761 - ItemCheckWitnessRva);
static_assert(UseCheckSiteRva + SiteSize == UseCheckMode2Rva);
static_assert(ItemCheckSiteRva + SiteSize == ItemCheckMode2Rva);
static_assert(UseSiteOriginal[0] == 0x83 && UseSiteOriginal[1] == 0xE9 && UseSiteOriginal[2] == 0x01
    && UseSiteOriginal[3] == 0x74 && UseSiteOriginal[5] == 0x83 && UseSiteOriginal[8] == 0x75);
static_assert(ItemSiteOriginal[0] == 0x83 && ItemSiteOriginal[1] == 0xEA && ItemSiteOriginal[2] == 0x01
    && ItemSiteOriginal[3] == 0x74 && ItemSiteOriginal[5] == 0x83 && ItemSiteOriginal[8] == 0x75);
// movzx ecx, byte [rbx+2D0h] / test ecx,ecx / je, then the site.
static_assert(UseCheckWitness[UseSiteInWitness - 11] == 0x0F && UseCheckWitness[UseSiteInWitness - 10] == 0xB6
    && UseCheckWitness[UseSiteInWitness - 9] == 0x8B && UseCheckWitness[UseSiteInWitness - 8] == 0xD0
    && UseCheckWitness[UseSiteInWitness - 7] == 0x02, "the byte the site dispatches on must be record +2D0h");
// movzx edx, byte [rax+2D0h] / test edx,edx / je, then the site.
static_assert(ItemCheckWitness[ItemSiteInWitness - 11] == 0x0F && ItemCheckWitness[ItemSiteInWitness - 10] == 0xB6
    && ItemCheckWitness[ItemSiteInWitness - 9] == 0x90 && ItemCheckWitness[ItemSiteInWitness - 8] == 0xD0
    && ItemCheckWitness[ItemSiteInWitness - 7] == 0x02, "the byte the site dispatches on must be record +2D0h");
static_assert(UseCheckWitness[UseCallInWitness] == 0xE8
    && CallTarget(UseCheckWitness, UseCheckWitnessRva, UseCallInWitness) == CheckStateRva,
    "the stock whitelist loop must call the state check the stub calls");
static_assert(ItemCheckWitness[ItemCallInWitness] == 0xE8
    && CallTarget(ItemCheckWitness, ItemCheckWitnessRva, ItemCallInWitness) == CheckStateRva,
    "the stock whitelist loop must call the state check the stub calls");
// The exits the stubs take, as they appear in the witnesses.
static_assert(UseCheckWitness[UseCheckRefuseRva - UseCheckWitnessRva] == 0xB8
    && UseCheckWitness[UseCheckRefuseRva - UseCheckWitnessRva + 1] == 0x04);
static_assert(UseCheckWitness[UseCheckCarryOnRva - UseCheckWitnessRva] == 0x48
    && UseCheckWitness[UseCheckCarryOnRva - UseCheckWitnessRva + 3] == 0xE8);
static_assert(ItemCheckWitness[ItemCheckRefuseRva - ItemCheckWitnessRva] == 0x32
    && ItemCheckWitness[ItemCheckRefuseRva - ItemCheckWitnessRva + 1] == 0xC0);
static_assert(ItemCheckWitness[ItemCheckUsableRva - ItemCheckWitnessRva] == 0xB0
    && ItemCheckWitness[ItemCheckUsableRva - ItemCheckWitnessRva + 1] == 0x01);

// ---------------------------------------------------------------------------
// Hook region
// ---------------------------------------------------------------------------
//   One allocation within rel32 reach of both windows, never freed:
//     +0000  code page, PAGE_EXECUTE_READ
//              +010  UNWIND_INFO for the use check stub
//              +028  UNWIND_INFO for the item check stub
//              +040  RUNTIME_FUNCTION table, 2 entries
//              +100  use check stub
//              +200  item check stub
//     +1000  counters page, PAGE_READWRITE, written by the stubs
//   The stubs read and write nothing but the game and this region, so they
//   never depend on this DLL being loaded.

constexpr std::size_t PageBytes           = 0x1000;
constexpr std::size_t UseUnwindOffset     = 0x010;   // never 0, so no entry's UnwindData is 0
constexpr std::size_t ItemUnwindOffset    = 0x028;
constexpr std::size_t FunctionTableOffset = 0x040;
constexpr std::size_t UseStubOffset       = 0x100;
constexpr std::size_t ItemStubOffset      = 0x200;
constexpr std::size_t CountersOffset      = 0x1000;
constexpr std::size_t RegionBytes         = 0x2000;

struct Counters {
    std::uint64_t useChecks;       // +00  restrict 4 rows reaching the use check
    std::uint64_t useRefused;      // +08  of those, refused
    std::uint64_t itemChecks;      // +10  restrict 4 rows reaching the item proc check
    std::uint64_t itemRefused;     // +18  of those, refused
    std::int32_t  lastUseSkill;    // +20  last refused skill id, use check
    std::int32_t  lastUseState;    // +24  the state that refused it
    std::int32_t  lastItemSkill;   // +28  last refused skill id, item procs
    std::int32_t  lastItemState;   // +2C
};
static_assert(offsetof(Counters, useChecks) == 0x00 && offsetof(Counters, useRefused) == 0x08
    && offsetof(Counters, itemChecks) == 0x10 && offsetof(Counters, itemRefused) == 0x18
    && offsetof(Counters, lastUseSkill) == 0x20 && offsetof(Counters, lastUseState) == 0x24
    && offsetof(Counters, lastItemSkill) == 0x28 && offsetof(Counters, lastItemState) == 0x2C);

// The use check stub, at region +100 (offsets below are from the stub start):
//   +00  66 90                    nop, puts the dispatch qword on an 8-byte boundary
//   +02  FF 25 00 00 00 00        jmp  qword [rip+0]
//   +08  dq                       dispatch: +21 armed, +10 vanilla
//   +10  83 F9 02                 cmp  ecx, 2                    ; vanilla
//   +13  74 06                    je   +1B
//   +15  FF 25 ..                 jmp  qword [+80]               ; 33F506 carry on
//   +1B  FF 25 ..                 jmp  qword [+88]               ; 33F4B3 whitelist
//   +21  83 F9 04                 cmp  ecx, 4                    ; armed
//   +24  74 07                    je   +2D
//   +26  83 F9 02                 cmp  ecx, 2
//   +29  74 F0                    je   +1B
//   +2B  EB E8                    jmp  +15
//   +2D  F0 48 FF 05 ..           lock inc qword [counters+00]
//   +35  31 ED                    xor  ebp, ebp
//   +37  0F BF 94 6B D2 02 00 00  movsx edx, word [rbx+rbp*2+2D2h] ; State1+i
//   +3F  85 D2                    test edx, edx
//   +41  78 0D                    js   +50                       ; empty: skip
//   +43  48 89 F1                 mov  rcx, rsi                  ; unit
//   +46  FF 15 ..                 call qword [+98]               ; 3351B0
//   +4C  85 C0                    test eax, eax
//   +4E  75 09                    jne  +59
//   +50  FF C5                    inc  ebp
//   +52  83 FD 03                 cmp  ebp, 3
//   +55  72 E0                    jb   +37
//   +57  EB BC                    jmp  +15                       ; none present: carry on
//   +59  F0 48 FF 05 ..           lock inc qword [counters+08]
//   +61  0F BF 03                 movsx eax, word [rbx]          ; skill id
//   +64  89 05 ..                 mov  [counters+20], eax
//   +6A  0F BF 84 6B D2 02 00 00  movsx eax, word [rbx+rbp*2+2D2h]
//   +72  89 05 ..                 mov  [counters+24], eax
//   +78  FF 25 ..                 jmp  qword [+90]               ; 33F4F0 refused
//   +7E  CC CC
//   +80  dq 33F506 / +88 dq 33F4B3 / +90 dq 33F4F0 / +98 dq 3351B0
//
// The item check stub, at region +200, is the same with edx for ecx, rdi as
// the record, ebx as the index, the item counters, and the exits 33F732
// (usable), 33F6E5 (whitelist) and 33F720 (refused).
constexpr std::uint8_t UseCheckStub[]{
    0x66, 0x90, 0xFF, 0x25, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x83, 0xF9, 0x02, 0x74, 0x06, 0xFF, 0x25, 0x65,
    0x00, 0x00, 0x00, 0xFF, 0x25, 0x67, 0x00, 0x00, 0x00, 0x83, 0xF9, 0x04,
    0x74, 0x07, 0x83, 0xF9, 0x02, 0x74, 0xF0, 0xEB, 0xE8, 0xF0, 0x48, 0xFF,
    0x05, 0xCB, 0x0E, 0x00, 0x00, 0x31, 0xED, 0x0F, 0xBF, 0x94, 0x6B, 0xD2,
    0x02, 0x00, 0x00, 0x85, 0xD2, 0x78, 0x0D, 0x48, 0x89, 0xF1, 0xFF, 0x15,
    0x4C, 0x00, 0x00, 0x00, 0x85, 0xC0, 0x75, 0x09, 0xFF, 0xC5, 0x83, 0xFD,
    0x03, 0x72, 0xE0, 0xEB, 0xBC, 0xF0, 0x48, 0xFF, 0x05, 0xA7, 0x0E, 0x00,
    0x00, 0x0F, 0xBF, 0x03, 0x89, 0x05, 0xB6, 0x0E, 0x00, 0x00, 0x0F, 0xBF,
    0x84, 0x6B, 0xD2, 0x02, 0x00, 0x00, 0x89, 0x05, 0xAC, 0x0E, 0x00, 0x00,
    0xFF, 0x25, 0x12, 0x00, 0x00, 0x00, 0xCC, 0xCC, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00,
};

constexpr std::uint8_t ItemCheckStub[]{
    0x66, 0x90, 0xFF, 0x25, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x83, 0xFA, 0x02, 0x74, 0x06, 0xFF, 0x25, 0x65,
    0x00, 0x00, 0x00, 0xFF, 0x25, 0x67, 0x00, 0x00, 0x00, 0x83, 0xFA, 0x04,
    0x74, 0x07, 0x83, 0xFA, 0x02, 0x74, 0xF0, 0xEB, 0xE8, 0xF0, 0x48, 0xFF,
    0x05, 0xDB, 0x0D, 0x00, 0x00, 0x31, 0xDB, 0x0F, 0xBF, 0x94, 0x5F, 0xD2,
    0x02, 0x00, 0x00, 0x85, 0xD2, 0x78, 0x0D, 0x48, 0x89, 0xF1, 0xFF, 0x15,
    0x4C, 0x00, 0x00, 0x00, 0x85, 0xC0, 0x75, 0x09, 0xFF, 0xC3, 0x83, 0xFB,
    0x03, 0x72, 0xE0, 0xEB, 0xBC, 0xF0, 0x48, 0xFF, 0x05, 0xB7, 0x0D, 0x00,
    0x00, 0x0F, 0xBF, 0x07, 0x89, 0x05, 0xBE, 0x0D, 0x00, 0x00, 0x0F, 0xBF,
    0x84, 0x5F, 0xD2, 0x02, 0x00, 0x00, 0x89, 0x05, 0xB4, 0x0D, 0x00, 0x00,
    0xFF, 0x25, 0x12, 0x00, 0x00, 0x00, 0xCC, 0xCC, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00,
};

constexpr std::size_t StubDispatchSlot = 0x08;
constexpr std::size_t StubVanilla      = 0x10;
constexpr std::size_t StubArmed        = 0x21;
constexpr std::size_t StubCarryOnSlot  = 0x80;
constexpr std::size_t StubMode2Slot    = 0x88;
constexpr std::size_t StubRefuseSlot   = 0x90;
constexpr std::size_t StubCheckSlot    = 0x98;
constexpr std::size_t StubBytes        = 0xA0;

// Where the rip-relative operand of an instruction points, as a region offset.
constexpr auto RipTarget(const std::uint8_t* stub, std::size_t stubOffset, std::size_t instruction,
        std::size_t length, std::size_t displacementAt) noexcept -> std::size_t {
    return static_cast<std::size_t>(static_cast<std::int64_t>(stubOffset + instruction + length)
        + ReadI32(stub + displacementAt));
}

template <std::size_t N>
constexpr auto StubLayoutIsSound(const std::uint8_t (&stub)[N], std::size_t stubOffset, std::uint8_t reg,
        std::size_t checks, std::size_t refused, std::size_t lastSkill, std::size_t lastState) noexcept -> bool {
    return N == StubBytes
        && stub[0x02] == 0xFF && stub[0x03] == 0x25 && ReadI32(stub + 0x04) == 0          // dispatch
        && (stubOffset + StubDispatchSlot) % 8 == 0
        && stub[StubVanilla] == 0x83 && stub[StubVanilla + 1] == reg && stub[StubVanilla + 2] == 0x02
        && stub[StubArmed] == 0x83 && stub[StubArmed + 1] == reg && stub[StubArmed + 2] == 0x04
        && RipTarget(stub, stubOffset, 0x15, 6, 0x17) == stubOffset + StubCarryOnSlot
        && RipTarget(stub, stubOffset, 0x1B, 6, 0x1D) == stubOffset + StubMode2Slot
        && RipTarget(stub, stubOffset, 0x2D, 8, 0x31) == CountersOffset + checks
        && RipTarget(stub, stubOffset, 0x46, 6, 0x48) == stubOffset + StubCheckSlot
        && RipTarget(stub, stubOffset, 0x59, 8, 0x5D) == CountersOffset + refused
        && RipTarget(stub, stubOffset, 0x64, 6, 0x66) == CountersOffset + lastSkill
        && RipTarget(stub, stubOffset, 0x72, 6, 0x74) == CountersOffset + lastState
        && RipTarget(stub, stubOffset, 0x78, 6, 0x7A) == stubOffset + StubRefuseSlot
        && stub[0x4C] == 0x85 && stub[0x4D] == 0xC0;   // the call returns onto a test, never an epilogue
}

static_assert(StubLayoutIsSound(UseCheckStub, UseStubOffset, 0xF9, offsetof(Counters, useChecks),
    offsetof(Counters, useRefused), offsetof(Counters, lastUseSkill), offsetof(Counters, lastUseState)));
static_assert(StubLayoutIsSound(ItemCheckStub, ItemStubOffset, 0xFA, offsetof(Counters, itemChecks),
    offsetof(Counters, itemRefused), offsetof(Counters, lastItemSkill), offsetof(Counters, lastItemState)));
static_assert(UseCheckStub[0x37] == 0x0F && UseCheckStub[0x39] == 0x94 && UseCheckStub[0x3A] == 0x6B
    && UseCheckStub[0x3B] == 0xD2 && UseCheckStub[0x3C] == 0x02, "use check reads [rbx+rbp*2+2D2h]");
static_assert(ItemCheckStub[0x37] == 0x0F && ItemCheckStub[0x39] == 0x94 && ItemCheckStub[0x3A] == 0x5F
    && ItemCheckStub[0x3B] == 0xD2 && ItemCheckStub[0x3C] == 0x02, "item check reads [rdi+rbx*2+2D2h]");
static_assert(UseStubOffset + StubBytes <= ItemStubOffset && ItemStubOffset + StubBytes <= PageBytes);

// UNWIND_INFO for code running inside 0x33F360's frame after its prologue and
// home saves: the codes of the game's own entries 33F438-33F68E (chained to
// 33F360), which the stub's position inside that frame requires.
constexpr std::uint8_t UseCheckUnwind[]{
    0x01, 0x00, 0x09, 0x00,   // version 1, no flags, prolog 0, 9 slots, no frame register
    0x00, 0xE4, 0x04, 0x00,   // SAVE_NONVOL r14, [rsp+20h]
    0x00, 0x54, 0x0A, 0x00,   // SAVE_NONVOL rbp, [rsp+50h]
    0x00, 0x34, 0x08, 0x00,   // SAVE_NONVOL rbx, [rsp+40h]
    0x00, 0x42,               // ALLOC_SMALL 28h
    0x00, 0x70,               // PUSH_NONVOL rdi
    0x00, 0x60,               // PUSH_NONVOL rsi
    0x00, 0x00,               // pads the slot count to even
};

// The same for 0x33F6A0's frame: the codes of its own entry 33F6A0-33F761.
constexpr std::uint8_t ItemCheckUnwind[]{
    0x01, 0x00, 0x06, 0x00,   // version 1, no flags, prolog 0, 6 slots, no frame register
    0x00, 0x64, 0x07, 0x00,   // SAVE_NONVOL rsi, [rsp+38h]
    0x00, 0x34, 0x06, 0x00,   // SAVE_NONVOL rbx, [rsp+30h]
    0x00, 0x32,               // ALLOC_SMALL 20h
    0x00, 0x70,               // PUSH_NONVOL rdi
};

static_assert(UseUnwindOffset + sizeof(UseCheckUnwind) <= ItemUnwindOffset && ItemUnwindOffset % 4 == 0);
static_assert(ItemUnwindOffset + sizeof(ItemCheckUnwind) <= FunctionTableOffset && FunctionTableOffset % 4 == 0);
static_assert(FunctionTableOffset + 2 * sizeof(RUNTIME_FUNCTION) <= UseStubOffset);

// ---------------------------------------------------------------------------
// Configuration
// ---------------------------------------------------------------------------

constexpr char        ConfigSection[]    = "restrict_state_block";
constexpr std::size_t InitialConfigBytes = 8'192;
constexpr std::size_t MaximumConfigBytes = 1'048'576;

constexpr char DefaultConfigToml[] =
    "# Restrict State Block (celestialrayone.restrict-state-block)\n"
    "#\n"
    "# Adds a new value to the skills.txt \"restrict\" column:\n"
    "#\n"
    "#   restrict = 4   The skill cannot be used while the unit has any of the\n"
    "#                  states named in its State1, State2 or State3 columns.\n"
    "#                  At every other time it is usable, exactly like restrict 1,\n"
    "#                  including in wereform and under any other restrict state.\n"
    "#\n"
    "# How to set up a skill (skills.txt, on the skill's row)\n"
    "#   restrict = 4\n"
    "#   State1   = a states.txt state name that blocks the skill\n"
    "#   State2   = optional second blocking state\n"
    "#   State3   = optional third blocking state\n"
    "#   Empty State columns are skipped, so any of the three may be used.\n"
    "#   The blocking states do NOT need restrict = 1 in states.txt, so no other\n"
    "#   skill is affected when they are applied.\n"
    "#\n"
    "# What the player sees while a blocking state is on\n"
    "#   The same thing the stock game shows for a skill tried in wereform: the\n"
    "#   skill button gets the invalid tint, the character says \"Impossible\" and\n"
    "#   the server refuses the skill. If the row has AttackNoMana = 1, a normal\n"
    "#   attack is made instead, as the game does for any refused skill.\n"
    "#\n"
    "# Limits (all of them are how the game treats every restrict value)\n"
    "#   - Auras and passives are never blocked: the game classifies them as an\n"
    "#     aura or a passive before it reads the restrict column.\n"
    "#   - Item procs (chance to cast on striking, when struck, on kill and so on)\n"
    "#     are only checked when the row also has ItemUseRestrict = 1. The same\n"
    "#     State1-3 list then applies to them.\n"
    "#   - restrict 4 never removes a state. Only restrict 3 does that.\n"
    "#   - restrict 0, 1, 2 and 3 keep their stock behaviour.\n"
    "#\n"
    "# Install it on every machine taking part in a TCP/IP game: the skill button\n"
    "# and the \"Impossible\" refusal come from the client, the actual refusal from\n"
    "# the server.\n"
    "#\n"
    "# Console command: restrict4 (status, and how often restrict 4 refused a skill)\n"
    "\n"
    "[restrict_state_block]\n"
    "\n"
    "# Master switch. false installs nothing at all.\n"
    "enabled = true\n";

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

const D2RL::PluginContext* Context{};
std::uintptr_t             Base{};
bool                       Enabled{ true };
bool                       ConfigFileWasRead{};
std::atomic<HookState>     State{ HookState::NotAttempted };
std::uint8_t*              Region{};
bool                       UnwindRegistered{};
bool                       UseSitePatched{};
bool                       ItemSitePatched{};

// ---------------------------------------------------------------------------
// Config parsing (the one key, no external dependency)
// ---------------------------------------------------------------------------

auto Trim(std::string_view value) noexcept -> std::string_view {
    std::size_t first = 0;
    while (first < value.size()
            && (value[first] == ' ' || value[first] == '\t' || value[first] == '\r' || value[first] == '\n')) {
        ++first;
    }
    std::size_t last = value.size();
    while (last > first
            && (value[last - 1] == ' ' || value[last - 1] == '\t' || value[last - 1] == '\r'
                || value[last - 1] == '\n')) {
        --last;
    }
    return value.substr(first, last - first);
}

auto StripComment(std::string_view line) noexcept -> std::string_view {
    const std::size_t hash = line.find('#');
    return hash == std::string_view::npos ? line : line.substr(0, hash);
}

void ParseConfig(std::string_view text) noexcept {
    std::string_view section;
    std::size_t cursor = 0;
    while (cursor < text.size()) {
        std::size_t end = text.find('\n', cursor);
        if (end == std::string_view::npos) end = text.size();
        const std::string_view line = Trim(StripComment(text.substr(cursor, end - cursor)));
        cursor = end + 1;
        if (line.empty()) continue;
        if (line.front() == '[') {
            const std::size_t close = line.find(']');
            section = Trim(line.substr(1, close == std::string_view::npos ? close : close - 1));
            continue;
        }
        const std::size_t equals = line.find('=');
        if (equals == std::string_view::npos || section != ConfigSection) continue;
        const std::string_view key = Trim(line.substr(0, equals));
        const std::string_view value = Trim(line.substr(equals + 1));
        if (key != "enabled") continue;
        if (value == "true") {
            Enabled = true;
        } else if (value == "false") {
            Enabled = false;
        } else {
            D2RL::LogWarnF(Context, "RestrictStateBlock: enabled must be true or false; keeping %s.",
                Enabled ? "true" : "false");
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
        Context->LogWarn("RestrictStateBlock: the config file could not be created; using the embedded defaults.");
        text = DefaultConfigToml;
    } else if (!ReadConfigText(text)) {
        Context->LogWarn("RestrictStateBlock: the config file could not be read; using the embedded defaults.");
        text = DefaultConfigToml;
    } else {
        ConfigFileWasRead = true;
    }
    ParseConfig(text);
}

// ---------------------------------------------------------------------------
// Hook region and sites
// ---------------------------------------------------------------------------

auto CanEncodeRel32(std::uintptr_t from, std::uintptr_t to) noexcept -> bool {
    const std::int64_t delta = static_cast<std::int64_t>(to) - static_cast<std::int64_t>(from) - JumpSize;
    return delta >= INT32_MIN && delta <= INT32_MAX;
}

// Searches upward from the window for a free block within rel32 reach.
auto AllocateNear(std::uintptr_t hint, std::size_t size) noexcept -> std::uint8_t* {
    SYSTEM_INFO systemInfo{};
    GetSystemInfo(&systemInfo);
    const auto granularity = static_cast<std::uintptr_t>(systemInfo.dwAllocationGranularity);
    const auto aligned = hint & ~(granularity - 1U);
    for (std::uintptr_t delta = granularity; delta < 0x7000'0000ULL; delta += granularity) {
        const auto candidate = aligned + delta;
        if (!CanEncodeRel32(hint, candidate + size)) break;
        if (auto* memory = VirtualAlloc(reinterpret_cast<void*>(candidate), size, MEM_COMMIT | MEM_RESERVE,
                PAGE_READWRITE)) {
            return static_cast<std::uint8_t*>(memory);
        }
    }
    return nullptr;
}

auto RegionAddress(std::size_t offset) noexcept -> std::uintptr_t {
    return reinterpret_cast<std::uintptr_t>(Region) + offset;
}

void WriteQword(std::uint8_t* at, std::uint64_t value) noexcept {
    std::memcpy(at, &value, sizeof(value));
}

void WriteDword(std::uint8_t* at, std::uint32_t value) noexcept {
    std::memcpy(at, &value, sizeof(value));
}

// E9 rel32 to the stub, the rest of the window NOP.
void EncodeSiteJump(std::uint64_t siteRva, std::size_t stubOffset, std::uint8_t (&out)[SiteSize]) noexcept {
    const std::uintptr_t site = Base + siteRva;
    const std::uintptr_t target = RegionAddress(stubOffset);
    out[0] = 0xE9;
    const auto displacement = static_cast<std::int32_t>(
        static_cast<std::int64_t>(target) - static_cast<std::int64_t>(site + JumpSize));
    std::memcpy(out + 1, &displacement, sizeof(displacement));
    std::memset(out + JumpSize, 0x90, SiteSize - JumpSize);
}

void EmitStub(std::uint8_t* code, std::size_t stubOffset, const std::uint8_t* stub, std::uint64_t carryOnRva,
        std::uint64_t mode2Rva, std::uint64_t refuseRva) noexcept {
    std::uint8_t* at = code + stubOffset;
    std::memcpy(at, stub, StubBytes);
    WriteQword(at + StubDispatchSlot, RegionAddress(stubOffset + StubArmed));
    WriteQword(at + StubCarryOnSlot, Base + carryOnRva);
    WriteQword(at + StubMode2Slot, Base + mode2Rva);
    WriteQword(at + StubRefuseSlot, Base + refuseRva);
    WriteQword(at + StubCheckSlot, Base + CheckStateRva);
}

void EmitUnwind(std::uint8_t* code) noexcept {
    std::memcpy(code + UseUnwindOffset, UseCheckUnwind, sizeof(UseCheckUnwind));
    std::memcpy(code + ItemUnwindOffset, ItemCheckUnwind, sizeof(ItemCheckUnwind));
    std::uint8_t* table = code + FunctionTableOffset;
    const std::uint32_t entries[2][3]{
        { static_cast<std::uint32_t>(UseStubOffset), static_cast<std::uint32_t>(UseStubOffset + StubBytes),
          static_cast<std::uint32_t>(UseUnwindOffset) },
        { static_cast<std::uint32_t>(ItemStubOffset), static_cast<std::uint32_t>(ItemStubOffset + StubBytes),
          static_cast<std::uint32_t>(ItemUnwindOffset) },
    };
    for (std::size_t entry = 0; entry < 2; ++entry) {
        for (std::size_t field = 0; field < 3; ++field) {
            WriteDword(table + entry * sizeof(RUNTIME_FUNCTION) + field * 4, entries[entry][field]);
        }
    }
}

auto BuildRegion() noexcept -> bool {
    Region = AllocateNear(Base + UseCheckSiteRva, RegionBytes);
    if (Region == nullptr) {
        Context->LogError("RestrictStateBlock: no hook region was available within rel32 reach of the skill "
                          "use check.");
        return false;
    }
    if (!CanEncodeRel32(Base + UseCheckSiteRva, RegionAddress(UseStubOffset))
            || !CanEncodeRel32(Base + ItemCheckSiteRva, RegionAddress(ItemStubOffset))) {
        Context->LogError("RestrictStateBlock: hook region displacement validation failed.");
        VirtualFree(Region, 0, MEM_RELEASE);
        Region = nullptr;
        return false;
    }

    std::uint8_t* code = Region;
    std::memset(code, 0xCC, PageBytes);
    std::memset(code, 0, UseStubOffset);
    EmitUnwind(code);
    EmitStub(code, UseStubOffset, UseCheckStub, UseCheckCarryOnRva, UseCheckMode2Rva, UseCheckRefuseRva);
    EmitStub(code, ItemStubOffset, ItemCheckStub, ItemCheckUsableRva, ItemCheckMode2Rva, ItemCheckRefuseRva);

    auto* counters = reinterpret_cast<Counters*>(Region + CountersOffset);
    std::memset(Region + CountersOffset, 0, PageBytes);
    counters->lastUseSkill = counters->lastUseState = -1;
    counters->lastItemSkill = counters->lastItemState = -1;

    DWORD previous = 0;
    if (!VirtualProtect(code, PageBytes, PAGE_EXECUTE_READ, &previous)) {
        Context->LogError("RestrictStateBlock: hook region protection could not be finalized.");
        VirtualFree(Region, 0, MEM_RELEASE);
        Region = nullptr;
        return false;
    }
    FlushInstructionCache(GetCurrentProcess(), code, PageBytes);

    UnwindRegistered = RtlAddFunctionTable(reinterpret_cast<PRUNTIME_FUNCTION>(code + FunctionTableOffset), 2,
                           static_cast<DWORD64>(RegionAddress(0))) != FALSE;
    if (!UnwindRegistered) {
        Context->LogWarn("RestrictStateBlock: unwind data for the hook region was not registered; stack walks "
                         "stop inside it.");
    }
    return true;
}

// Points a stub's dispatch at its vanilla path, which replays the replaced
// instructions exactly. The qword is 8-aligned, so this is one atomic store
// even while a game thread runs the stub.
auto RetargetToVanilla(std::size_t stubOffset) noexcept -> bool {
    if (Region == nullptr) return true;
    DWORD previous = 0;
    if (!VirtualProtect(Region, PageBytes, PAGE_EXECUTE_READWRITE, &previous)) return false;
    InterlockedExchange64(reinterpret_cast<volatile LONG64*>(Region + stubOffset + StubDispatchSlot),
        static_cast<LONG64>(RegionAddress(stubOffset + StubVanilla)));
    DWORD ignored = 0;
    VirtualProtect(Region, PageBytes, previous, &ignored);
    FlushInstructionCache(GetCurrentProcess(), Region, PageBytes);
    return true;
}

auto PatchSite(std::uint64_t siteRva, const std::uint8_t* original, std::size_t stubOffset) noexcept -> bool {
    std::uint8_t jump[SiteSize]{};
    EncodeSiteJump(siteRva, stubOffset, jump);
    return Context->PatchBytes(siteRva, original, SiteSize, jump, SiteSize);
}

auto RestoreSite(std::uint64_t siteRva, const std::uint8_t* original, std::size_t stubOffset, bool& patched) noexcept
        -> bool {
    if (!patched) return true;
    std::uint8_t jump[SiteSize]{};
    EncodeSiteJump(siteRva, stubOffset, jump);
    if (!Context->PatchBytes(siteRva, jump, SiteSize, original, SiteSize)) return false;
    patched = false;
    return true;
}

auto ReadCounters() noexcept -> Counters {
    Counters copy{};
    copy.lastUseSkill = copy.lastUseState = copy.lastItemSkill = copy.lastItemState = -1;
    if (Region == nullptr) return copy;
    const auto* live = reinterpret_cast<const volatile Counters*>(Region + CountersOffset);
    copy.useChecks     = live->useChecks;
    copy.useRefused    = live->useRefused;
    copy.itemChecks    = live->itemChecks;
    copy.itemRefused   = live->itemRefused;
    copy.lastUseSkill  = live->lastUseSkill;
    copy.lastUseState  = live->lastUseState;
    copy.lastItemSkill = live->lastItemSkill;
    copy.lastItemState = live->lastItemState;
    return copy;
}

// ---------------------------------------------------------------------------
// Verification and installation
// ---------------------------------------------------------------------------

auto Verify(std::uint64_t rva, const std::uint8_t* expected, std::size_t size, const char* label) noexcept -> bool {
    if (Context->CheckExpectedBytes(rva, expected, static_cast<std::uint32_t>(size))) return true;
    D2RL::LogErrorF(Context,
        "RestrictStateBlock: NOT installed. %s at 0x%llX does not match the verified D2R image, or another "
        "plugin already owns it. Nothing was patched.",
        label, static_cast<unsigned long long>(rva));
    return false;
}

auto VerifyNativeContract() noexcept -> bool {
    return Verify(UseCheckSavesRva, UseCheckSavesWitness, sizeof(UseCheckSavesWitness),
               "The skill use check's register saves")
        && Verify(UseCheckEpilogueRva, UseCheckEpilogueWitness, sizeof(UseCheckEpilogueWitness),
               "The skill use check's epilogue")
        && Verify(UseCheckWitnessRva, UseCheckWitness, sizeof(UseCheckWitness),
               "The skill use check's restrict block")
        && Verify(ItemCheckWitnessRva, ItemCheckWitness, sizeof(ItemCheckWitness),
               "The item proc restrict check");
}

void Install() noexcept {
    if (!Enabled) {
        State.store(HookState::DisabledByConfig, std::memory_order_release);
        Context->LogInfo("RestrictStateBlock: not installed, turned off in the config file.");
        return;
    }
    if (!VerifyNativeContract()) {
        State.store(HookState::UnsupportedBuild, std::memory_order_release);
        return;
    }
    if (!BuildRegion()) {
        State.store(HookState::InstallFailed, std::memory_order_release);
        return;
    }
    if (!PatchSite(UseCheckSiteRva, UseSiteOriginal, UseStubOffset)) {
        // Nothing points at the region yet.
        State.store(HookState::InstallFailed, std::memory_order_release);
        Context->LogError("RestrictStateBlock: NOT installed. The skill use check at 0x33F4A9 could not be "
                          "redirected.");
        return;
    }
    UseSitePatched = true;
    if (!PatchSite(ItemCheckSiteRva, ItemSiteOriginal, ItemStubOffset)) {
        // All or nothing: take the use check back out too.
        RetargetToVanilla(UseStubOffset);
        const bool restored = RestoreSite(UseCheckSiteRva, UseSiteOriginal, UseStubOffset, UseSitePatched);
        State.store(HookState::InstallFailed, std::memory_order_release);
        Context->LogError(restored
            ? "RestrictStateBlock: NOT installed. The item proc check at 0x33F6DB could not be redirected; the "
              "skill use check was restored."
            : "RestrictStateBlock: NOT installed. The item proc check at 0x33F6DB could not be redirected, and "
              "the skill use check keeps running its stock logic through the hook region.");
        return;
    }
    ItemSitePatched = true;
    State.store(HookState::Installed, std::memory_order_release);
    Context->LogInfo("RestrictStateBlock: installed. skills.txt restrict 4 refuses a skill while the unit has "
                     "State1, State2 or State3 (use check 0x33F4A9, item procs 0x33F6DB).");
}

// ---------------------------------------------------------------------------
// Console command
// ---------------------------------------------------------------------------

auto StateText(HookState state) noexcept -> const char* {
    switch (state) {
    case HookState::Installed:        return "active";
    case HookState::DisabledByConfig: return "off in the config file";
    case HookState::UnsupportedBuild: return "NOT ACTIVE, unrecognised game build or the check is already hooked, see the log";
    case HookState::InstallFailed:    return "NOT ACTIVE, the hook failed, see the log";
    case HookState::NotAttempted:
    default:                          return "NOT ACTIVE, never attempted";
    }
}

void AppendLast(char* out, std::size_t size, std::int32_t skill, std::int32_t state) noexcept {
    if (skill < 0) {
        std::snprintf(out, size, "none refused yet");
    } else {
        std::snprintf(out, size, "last refused: skill %d, blocked by state %d", skill, state);
    }
}

auto __cdecl StatusCommand(D2R::Game::Client*, const D2RL::ConsoleCommandContext* command, void*) noexcept
        -> D2RL::ConsoleCommandResult {
    if (command == nullptr || command->plugin == nullptr) return D2RL::ConsoleCommandResult::Failed;
    const D2RL::PluginContext* plugin = command->plugin;
    const Counters counters = ReadCounters();

    char line[320]{};
    std::snprintf(line, sizeof(line), "Restrict State Block: %s | config %s",
        StateText(State.load(std::memory_order_acquire)),
        ConfigFileWasRead ? "loaded" : "NOT READ (embedded defaults)");
    plugin->WriteConsoleMessage(line);

    char last[96]{};
    AppendLast(last, sizeof(last), counters.lastUseSkill, counters.lastUseState);
    std::snprintf(line, sizeof(line), "  skill use: restrict 4 checks %llu, refused %llu, %s",
        static_cast<unsigned long long>(counters.useChecks), static_cast<unsigned long long>(counters.useRefused),
        last);
    plugin->WriteConsoleMessage(line);

    AppendLast(last, sizeof(last), counters.lastItemSkill, counters.lastItemState);
    std::snprintf(line, sizeof(line), "  item procs (ItemUseRestrict 1): restrict 4 checks %llu, refused %llu, %s",
        static_cast<unsigned long long>(counters.itemChecks), static_cast<unsigned long long>(counters.itemRefused),
        last);
    plugin->WriteConsoleMessage(line);
    return D2RL::ConsoleCommandResult::Handled;
}

constexpr D2RL::PluginInfo Info{
    .infoSize    = D2RL::PluginInfoSize,
    .abiVersion  = D2RL_PLUGIN_ABI_VERSION,
    .id          = "celestialrayone.restrict-state-block",
    .name        = "Restrict State Block",
    .version     = "1.0.0",
    .author      = "CelestialRayOne",
    .description = "skills.txt restrict 4: the skill cannot be used while the unit has State1, State2 or State3.",
    // Shared: the use check runs on the client (button tint, cast refusal) and
    // on the server (command validation, item procs).
    .flags       = D2RL::PluginFlags::Shared | D2RL::PluginFlags::NativeHooks,
};

static_assert(D2RL::HasValidPluginRole(Info.flags), "Exactly one execution role must be set.");
static_assert(D2RL::HasOnlyKnownPluginFlags(Info.flags), "Unknown plugin flag set.");

}  // namespace

D2RL_PLUGIN_EXPORT auto D2RLoaderGetPluginInfo() noexcept -> const D2RL::PluginInfo* {
    return &Info;
}

D2RL_PLUGIN_EXPORT auto D2RLoaderLoadPlugin(const D2RL::PluginContext* context) noexcept -> bool {
    if (!D2RL::HasContext(context) || context->exeBase == 0) return false;
    Context = context;
    Base = context->exeBase;

    // Registered first, and the load always succeeds: returning false would
    // unload the DLL and take the command with it, leaving no in-game way to
    // see why nothing is refused.
    if (!Context->RegisterConsoleCommand("restrict4", &StatusCommand,
            "Reports Restrict State Block status and how often restrict 4 refused a skill.")) {
        Context->LogWarn("RestrictStateBlock: the status console command was refused.");
    }

    ReadConfiguration();
    Install();
    return true;
}

D2RL_PLUGIN_EXPORT void D2RLoaderUnloadPlugin() noexcept {
    if (Context == nullptr || (!UseSitePatched && !ItemSitePatched)) return;
    const bool useRetargeted = RetargetToVanilla(UseStubOffset);
    const bool itemRetargeted = RetargetToVanilla(ItemStubOffset);
    const bool useRestored = RestoreSite(UseCheckSiteRva, UseSiteOriginal, UseStubOffset, UseSitePatched);
    const bool itemRestored = RestoreSite(ItemCheckSiteRva, ItemSiteOriginal, ItemStubOffset, ItemSitePatched);
    if (!useRestored || !itemRestored) {
        Context->LogError(useRetargeted && itemRetargeted
            ? "RestrictStateBlock: a check could not be restored; it keeps running its stock logic through the "
              "hook region, without restrict 4."
            : "RestrictStateBlock: a check could not be restored or retargeted; restrict 4 may stay in effect.");
        return;
    }
    if (UnwindRegistered) {
        RtlDeleteFunctionTable(reinterpret_cast<PRUNTIME_FUNCTION>(Region + FunctionTableOffset));
        UnwindRegistered = false;
    }
    // The region itself is deliberately kept: a thread may be inside a stub
    // right now.
}

}  // namespace CelestialRayOne::RestrictStateBlock
