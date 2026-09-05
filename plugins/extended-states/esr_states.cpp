//
// d2rl-esr-extended-states - raises the D2R states.txt limit.
//
// 2.3.0  SIDE ALLOCATION, with an OWNER TAG
//
// THE BUG THIS FIXES, and it was mine
//
// sub_1402F0D80 copies a ptStats struct field by field, dword by dword, and
// the allocator sub_1402F65D0 calls it on EVERY new stats block. The copy
// walks straight through the region I was using for the slot:
//
//   mov eax,[r13+0B60h] ; mov [rbp+0B60h],eax   <- the slot tag
//   mov eax,[r13+0B64h] ; mov [rbp+0B64h],eax
//   mov eax,[r13+0B68h] ; mov [rbp+0B68h],eax   <- the bitmap pointer
//
// So a brand new stat list inherited the source block's bitmap pointer. Two
// units then shared one bitmap, and the new one instantly had every state the
// source had, with nothing ever applying them. That is why weaken, stunned,
// dimvision, might and skill_move turned up on town NPCs that were never
// touched, and why they never went away.
//
// The fix is to make the slot self validating. +0xB60 now holds the ADDRESS OF
// THE STAT LIST THAT OWNS IT rather than a constant. An inherited slot carries
// the source block's address, which cannot match the block reading it, so it
// is treated as absent and a fresh bitmap is allocated. Freeing is guarded the
// same way, so a block holding an inherited slot can never free the real
// owner's buffer. The console command reports how often this fires.
//
// This also removes a second, quieter failure: with a constant tag, a copied
// slot could be freed twice, putting one buffer on the free list twice and
// handing it to two different units.
//
// D2MOO CORRECTION, from the leaked-lineage source
//
// The second array is NOT a change mirror. It is the GFX STATE FLAGS, a second
// independent per unit bitmap that drives overlays and UI. D2MOO names both:
//
//   uint32_t* STATES_GetStatFlags(pUnit)       -> pStatListEx->StatFlags
//   uint32_t* STATES_GetGfxStateFlags(pUnit)   -> &StatFlags[(rows + 31) / 32]
//
// So the layout is one contiguous run of 2*FLAG dwords, stat flags first and
// gfx flags immediately after, which is exactly what this file allocates and
// hands out. The geometry was right; the name and my reasoning about it were
// wrong for several rounds. Everything called a "mirror" below is the gfx half.
//
// It also explains which symptom belongs to which array: overlays and the
// potion globe read the GFX half, so anything that breaks that pointer breaks
// them, and nothing about the stat half will show it.
//
// The exported wrappers are thunks: sub_1403352A0 -> sub_1402F6370. So hooking
// the internal getters covers the wrappers too, which the call graph walk had
// already suggested and D2MOO confirms.
//
// 2.2.0  SIDE ALLOCATION, with state tracing
//
// 2.1.0 moved the change mirror to sit adjacent at 4*FLAG past the live half,
// reproducing the stock layout, after I read "mirror identical to live" as a
// symptom. It is not. The v2 probe run, taken with the relocation OFF on an
// essentially stock game, showed the same thing. The mirror is simply not
// drained often in this build. The adjacency is kept because it is more
// faithful, but it was not the bug.
//
// The real delta against that pre-relocation run is five states now set that
// were not before: 18, 19, 21, 23 and 33. Those turned out to be 530, 531,
// 533, 535 and 545 truncated by the packet writer's nine bit state id field,
// which is why the sender is replaced rather than patched.

//
// One input: StateBits. 12 gives 4096 states.
//
// -------------------------------------------------------------------------
// Why the previous approach could not work
// -------------------------------------------------------------------------
// The state array lives inside ptStats, which is one element of a fixed size
// pool. Growing the element to make room is what 0.5.x and 1.0.x did, and it
// fails on memory. The crash report named it:
//
//   D2Render\src\renderer_allocator.cpp:31
//   BC_ASSERT: p != nullptr   "Failed to allocate from Renderer pool!"
//
// sub_140F6F060 is a thin wrapper over the renderer pool object at
// off_142810FF0 that asserts on a null result, so every "wrote into a null
// buffer" access violation was an unchecked allocation failure, never heap
// corruption. Measured against that ceiling:
//
//   stock  0xB80 * 512 = 0x170000   stable
//   0.4.0  0xC00 * 512 = 0x180000   stable
//   0.5.0  0xC80 * 512 = 0x190000   crash
//   1.0.0  0xF80 * 396 = 0x17FA00   crash, needs a second chunk for 512
//                                   stat lists, 0x2FF400 total
//
// The pool gets about 0x180000 and must hold 512 stat lists, so the element
// caps at 0xC00, which leaves 128 bytes, which is 512 states. 4096 needs 1024
// bytes per unit and the game does not have it.
//
// -------------------------------------------------------------------------
// What this build does instead
// -------------------------------------------------------------------------
// The bitmaps move OUT of the game's memory entirely, into pages this plugin
// gets from VirtualAlloc. The game's pool geometry is left byte for byte
// stock: no element size change, no perPage change, no memset change. The
// game's memory picture is identical to running without the plugin.
//
// Each ptStats keeps a pointer to its bitmap in the 128 bytes the old array
// used to occupy, which are dead once the accessors stop reading them:
//
//   +0xB60  magic, proves the slot is ours and not leftover bits
//   +0xB68  pointer to this unit's bitmap
//
// Both are inside the old array, so sub_1402F65D0's memset(block, 0, 0xB80)
// clears them for free on every allocation. A recycled block therefore always
// starts with no bitmap, which removes the whole class of stale pointer bugs.
//
// The bitmap is one flat buffer: FLAG live dwords then FLAG mirror dwords,
// FLAG fixed at the cap rather than at the row count. Nothing in the engine
// requires the two halves to be adjacent at any particular distance, verified
// below, so a fixed split is safe and removes a runtime dependency.
//
// -------------------------------------------------------------------------
// Every function that touches the array, found by walking the call graph
// -------------------------------------------------------------------------
// FOUR touch it directly. The first two hold the displacement inline, the
// other two hand a pointer to callers. My earlier sweep found only three and
// missed sub_1402F6370, which is the live-half getter and would have left the
// state sifter reading a dead array. All four are replaced here:
//
//   2F6220  StatsGetState(hUnit, nState)            mov ecx,[..+0AF0h]
//   2F7EA0  StatsSetState(hUnit, nState, bSet)      3 sites at +0AF0h
//   2F62D0  StatsGetStateChangeFlags(hUnit)         statlist + 4*(FLAG+700)
//   2F6370  StatsGetStateFlags(hUnit)               statlist + 2800
//
// SEVEN more consume those two pointers. All of them derive every position
// from the returned pointer plus FLAG, and none assumes the two halves sit a
// fixed distance apart, so all seven follow the relocation for free:
//
//   140335130  memset(mirror, 0, 4*FLAG)
//   1403355A0  set one mirror bit
//   1403356C0  the sync sifter, takes BOTH pointers and uses their difference
//   140335C60  is any mirror bit set
//   140335D00  read one mirror bit
//   140336330  move live bits into the mirror through a group mask, BOTH
//   140336440  does live intersect a group mask
//
// -------------------------------------------------------------------------
// Behaviour reproduced exactly, from the decompiled originals
// -------------------------------------------------------------------------
// StatsGetState returns the masked word, not a bool, and callers rely on
// nonzero rather than 1. gdwBitMasks at 141D996D0 is 1<<i and the inverse
// table at 141D99750 is ~(1<<i), both read out and confirmed, so the masks
// are computed rather than loaded.
//
// StatsSetState's tail is the part that matters and is reproduced in full:
// after a bit actually changes it marks the mirror, then looks up the
// states.txt record (records pointer at table+0x290, row count at +0x298,
// stride 68) and tests bit 1 of the byte at record+18. If that bit is set it
// calls sub_14034E140(hUnit, 8, bSet), except when clearing while
// sub_140336280(hUnit, 17) is nonzero. Miss that and states stop driving
// whatever event 8 is.
//
// The statlist getter sub_14034B870 is just *(hUnit+0x88), so it is inlined
// here rather than called. The gate at statlist+0x1C must be negative, which
// is also what distinguishes a primary stats block from a 144 byte child node
// in the free path.
//
// -------------------------------------------------------------------------
// Only three byte patches remain
// -------------------------------------------------------------------------
//   30849F  LoadStatesTxt MAX_STATES  1FFh -> NumStates-1
//   3084BB  LoadStatesTxt block cap   200h -> NumStates
//   2F69B8  sub_1402F65D0 asserts 2*FLAG <= 32 on EVERY allocation at
//           2F69B5. At 4096 states 2*FLAG is 256, so the jbe becomes jmp.
//           The imm8 at 2F69B7 only reaches 127 and cannot hold 256.
//
// The id bounds at 2F6281/2F629D/2F7F32/2F7F4E are NOT patched. Those live
// inside the two functions this build replaces outright, so they are
// unreachable, and the bound is enforced here instead.
//
// The old array is deliberately left dead apart from the two slots at +0xB60.
// If some fifth writer exists that the call graph walk missed, it shows up as
// nonzero bytes in that region and the Cheat Engine probe reports it. A
// missed reader degrades to reading zeros, which loses a state rather than
// corrupting a neighbour.
//
#include <D2RLPlugin/api.h>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>

// ---------------------------------------------------------------------------
// The only input. 9 = 512, 10 = 1024, 11 = 2048, 12 = 4096.
// Nothing here is bounded by the game's memory any more, so this is free to
// move. It costs BitmapBytes of our own memory per live stat list.
// ---------------------------------------------------------------------------
static constexpr int StateBits = 12;

static constexpr std::uint32_t NumStates    = 1U << StateBits;
static constexpr std::uint32_t MaxStates    = NumStates - 1U;
static constexpr std::uint32_t BlockCap     = NumStates;
static constexpr std::uint32_t IdBound      = NumStates - 2U;
static constexpr std::uint32_t FlagDwords   = NumStates / 32U;   // per half, at the cap
static constexpr std::uint32_t BitmapDwords = 2U * FlagDwords;
static constexpr std::uint32_t BitmapBytes  = BitmapDwords * 4U;

static_assert(StateBits >= 9 && StateBits <= 16, "StateBits outside the sane range");

// ---------------------------------------------------------------------------
// Guarded reads. No heap API, no allocation, no locks. Every engine pointer
// this file touches is read behind a structured exception guard.
// ---------------------------------------------------------------------------
static auto SafeCopy(void* destination, const void* source, std::uint32_t size) noexcept -> bool {
#if defined(_MSC_VER)
        __try {
#endif
                std::uint8_t*       out = static_cast<std::uint8_t*>(destination);
                const std::uint8_t* in  = static_cast<const std::uint8_t*>(source);
                for (std::uint32_t i = 0; i < size; ++i) {
                        out[i] = in[i];
                }
                return true;
#if defined(_MSC_VER)
        } __except (1) {
                return false;
        }
#endif
}

// ---------------------------------------------------------------------------
// Image layout
// ---------------------------------------------------------------------------
static constexpr std::uint64_t RvaGetState        = 0x002F6220;
static constexpr std::uint64_t RvaGetChangeFlags  = 0x002F62D0;
static constexpr std::uint64_t RvaGetStateFlags   = 0x002F6370;
static constexpr std::uint64_t RvaSetState        = 0x002F7EA0;
static constexpr std::uint64_t RvaFreeStatList    = 0x002F9EB0;
static constexpr std::uint64_t RvaSendStateChanges = 0x0053A210;   // per state sender
static constexpr std::uint64_t RvaBulkStateSender  = 0x00539890;   // bulk 0xAA list sender

static constexpr std::uint64_t RvaGfxFlagsWrapper = 0x003352B0;

// Everything the high state relay needs, all taken from the decompiles of
// sub_140539890 and sub_14053A210.
static constexpr std::uint64_t RvaUnitTypeOf      = 0x0034B9D0;
static constexpr std::uint64_t RvaUnitIdOf        = 0x0034A330;
static constexpr std::uint64_t RvaGetStateWrapper = 0x003351B0;
static constexpr std::uint64_t RvaStateStatListOf = 0x002F5940;
static constexpr std::uint64_t RvaCollectStats    = 0x002F64E0;
static constexpr std::uint64_t RvaStatRecordOf    = 0x002D9730;
static constexpr std::uint64_t RvaBitWriterInit   = 0x00A1B620;
static constexpr std::uint64_t RvaBitWrite        = 0x00A1B710;
static constexpr std::uint64_t RvaBitLength       = 0x00A1B610;
static constexpr std::uint64_t RvaSendPacket      = 0x00477310;
static constexpr std::uint64_t RvaSendStateSet    = 0x004801B0;   // packet 0xA7
static constexpr std::uint64_t RvaSendStateClear  = 0x004800A0;   // packet 0xA9

static constexpr std::uint64_t RvaGetGameVersion  = 0x0034A0E0;
static constexpr std::uint64_t RvaGetStateCount   = 0x002141A0;
static constexpr std::uint64_t RvaGetStatesTable  = 0x00300A90;
static constexpr std::uint64_t RvaUnitHasFlag     = 0x00336280;   // (hUnit, 17)
static constexpr std::uint64_t RvaUnitStateEvent  = 0x0034E140;   // (hUnit, 8, bSet)

static constexpr std::uint64_t RvaLoadMaxStates   = 0x0030849F;
static constexpr std::uint64_t RvaLoadBlockCap    = 0x003084BB;
static constexpr std::uint64_t RvaBlockAssertJbe  = 0x002F69B8;

static constexpr std::uintptr_t UnitStatListOffset = 0x88;
static constexpr std::uintptr_t StatListGate       = 0x1C;
// +0xB60 holds the address of the stat list that owns the slot, NOT a magic
// constant. sub_1402F0D80 copies the whole struct dword by dword through this
// region, so a fresh block inherits whatever the source block had here. An
// owner tag makes that harmless: the inherited value is the SOURCE's address,
// it will not match the block reading it, and the slot is treated as absent.
static constexpr std::uintptr_t SlotOwnerOffset    = 0xB60;
static constexpr std::uintptr_t SlotPointerOffset  = 0xB68;

static constexpr std::uintptr_t StatesRecordsPtr   = 0x290;
static constexpr std::uintptr_t StatesRowCount     = 0x298;
static constexpr std::size_t    StatesRecordStride = 68;
static constexpr std::uintptr_t StatesRecordFlags  = 18;
static constexpr std::uint8_t   StatesEventBit     = 0x02;   // gdwBitMasks[1]

// ---------------------------------------------------------------------------
// Prologue signatures, read out of the 3.3.0 build 93847 image
// ---------------------------------------------------------------------------
static constexpr std::uint8_t SigGetState[] {
        0x48, 0x89, 0x5C, 0x24, 0x10, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x63, 0xFA
};
static constexpr std::uint8_t SigGetChangeFlags[] {
        0x48, 0x89, 0x5C, 0x24, 0x10, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B, 0xF9
};
static constexpr std::uint8_t SigGetStateFlags[] {
        0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x85, 0xC9
};
static constexpr std::uint8_t SigSetState[] {
        0x40, 0x53, 0x56, 0x41, 0x57, 0x48, 0x83, 0xEC, 0x50, 0x48, 0x63, 0xDA
};
static constexpr std::uint8_t SigFreeStatList[] {
        0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x6C, 0x24, 0x18, 0x48, 0x89, 0x74, 0x24, 0x20, 0x57
};
static constexpr std::uint8_t SigSendStateChanges[] {
        0x40, 0x55, 0x53, 0x56, 0x41, 0x55, 0x48, 0x8D, 0xAC, 0x24, 0x88, 0xFE, 0xFF, 0xFF
};
// Read out of the image, not guessed:
//   40 55                     push rbp
//   53 56 57                  push rbx, rsi, rdi
//   48 8D AC 24 78 FC FF FF   lea rbp,[rsp-388h]
static constexpr std::uint8_t SigBulkStateSender[] {
        0x40, 0x55, 0x53, 0x56, 0x57, 0x48, 0x8D, 0xAC, 0x24, 0x78, 0xFC, 0xFF, 0xFF
};

// states.txt record, stride 68
static constexpr std::uintptr_t StateRecordFlags = 16;    // bit 0 is nosend
static constexpr std::uint8_t   StateNoSendBit   = 0x01;

// itemstatcost record, stride 324
static constexpr std::uintptr_t StatSendBits  = 8;
static constexpr std::uintptr_t StatParamBits = 9;
static constexpr std::uintptr_t StatFlags     = 4;
static constexpr std::uint8_t   StatSignedBit = 0x02;

using SendStateChangesFn = std::int64_t(__fastcall*)(void* hUnit, void* client) noexcept;
using UnitTypeOfFn       = std::uint8_t(__fastcall*)(void* hUnit) noexcept;
using UnitIdOfFn         = std::uint32_t(__fastcall*)(void* hUnit, const char* file, int line) noexcept;
using GfxFlagsOfFn       = std::uint32_t*(__fastcall*)(void* hUnit) noexcept;
using GetStateOfFn       = std::uint32_t(__fastcall*)(void* hUnit, std::uint32_t state) noexcept;
using StateStatListOfFn  = void*(__fastcall*)(void* hUnit, std::uint32_t state) noexcept;
using CollectStatsFn     = int(__fastcall*)(void* statList, std::uint32_t* out, int max) noexcept;
using StatRecordOfFn     = std::uint8_t*(__fastcall*)(std::uint32_t version, std::uint32_t stat) noexcept;
using BitWriterInitFn    = void(__fastcall*)(void* ctx, void* buffer, int size) noexcept;
using BitWriteFn         = void(__fastcall*)(void* ctx, std::uint32_t value, std::uint32_t bits) noexcept;
using BitLengthFn        = std::uint32_t(__fastcall*)(void* ctx) noexcept;
using SendPacketFn       = void(__fastcall*)(void* client, void* packet, std::uint32_t length) noexcept;
using SendStateFlagFn    = void(__fastcall*)(void* client, std::uint8_t unitType, std::uint32_t unitId,
                                             std::int16_t state) noexcept;

static SendStateChangesFn OriginalSendStateChanges = nullptr;
static SendStateChangesFn OriginalBulkStateSender  = nullptr;

static GfxFlagsOfFn      GfxFlagsOf      = nullptr;

static std::atomic<std::uint32_t> PacketsSent  { 0 };
static std::atomic<std::uint32_t> StatesSent   { 0 };
static std::atomic<std::uint32_t> SenderRefused { 0 };

// ---------------------------------------------------------------------------
// Game entry points
// ---------------------------------------------------------------------------
using GetGameVersionFn = std::uint8_t(__fastcall*)(void* hUnit) noexcept;
using GetStateCountFn  = int(__fastcall*)(std::uint32_t version) noexcept;
using GetStatesTableFn = void*(__fastcall*)(std::uint32_t version) noexcept;
using UnitHasFlagFn    = int(__fastcall*)(void* hUnit, int flag) noexcept;
using UnitStateEventFn = std::int64_t(__fastcall*)(void* hUnit, int event, std::int64_t value) noexcept;

static GetGameVersionFn GameVersionOf   = nullptr;
static GetStateCountFn  StateCountOf    = nullptr;
static GetStatesTableFn StatesTableOf   = nullptr;
static UnitHasFlagFn    UnitHasFlag     = nullptr;
static UnitStateEventFn UnitStateEvent  = nullptr;

using FreeStatListFn = void(__fastcall*)(std::uint8_t version, void* statList) noexcept;
static FreeStatListFn OriginalFreeStatList = nullptr;


using UnitTypeOfFn      = std::uint8_t(__fastcall*)(void* hUnit) noexcept;
using UnitIdOfFn        = std::uint32_t(__fastcall*)(void* hUnit, const char* file, int line) noexcept;
using GetStateFn        = std::uint32_t(__fastcall*)(void* hUnit, std::uint32_t state) noexcept;
using StateStatListOfFn = void*(__fastcall*)(void* hUnit, std::uint32_t state) noexcept;
using CollectStatsFn    = int(__fastcall*)(void* statList, std::uint32_t* out, int max) noexcept;
using StatRecordOfFn    = std::uint8_t*(__fastcall*)(std::uint32_t version, std::uint32_t stat) noexcept;
using BitWriterInitFn   = void(__fastcall*)(void* ctx, void* buffer, int size) noexcept;
using BitWriteFn        = void(__fastcall*)(void* ctx, std::uint32_t value, std::uint32_t bits) noexcept;
using BitLengthFn       = std::uint32_t(__fastcall*)(void* ctx) noexcept;
using SendPacketFn      = void(__fastcall*)(void* client, void* packet, std::uint32_t length) noexcept;
using SendStateFlagFn   = void(__fastcall*)(void* client, std::uint8_t unitType, std::uint32_t unitId,
                                            std::int16_t state) noexcept;

static UnitTypeOfFn      UnitTypeOf      = nullptr;
static UnitIdOfFn        UnitIdOf        = nullptr;
static GetStateFn        GetStateOf      = nullptr;
static StateStatListOfFn StateStatListOf = nullptr;
static CollectStatsFn    CollectStats    = nullptr;
static StatRecordOfFn    StatRecordOf    = nullptr;
static BitWriterInitFn   BitWriterInit   = nullptr;
static BitWriteFn        BitWrite        = nullptr;
static BitLengthFn       BitLength       = nullptr;
static SendPacketFn      SendPacket      = nullptr;
static SendStateFlagFn   SendStateSet    = nullptr;
static SendStateFlagFn   SendStateClear  = nullptr;

static std::atomic<std::uint32_t> SenderClamped { 0 };
static std::atomic<std::uint32_t> HighStatesSent { 0 };

static const D2RL::PluginContext* Context = nullptr;

// ---------------------------------------------------------------------------
// Bitmap storage. Our pages, not the game's.
// ---------------------------------------------------------------------------
static constexpr std::uint32_t SlotsPerSlab = 512;
static constexpr std::size_t   SlabBytes    = static_cast<std::size_t>(SlotsPerSlab) * BitmapBytes;
static constexpr int           MaxSlabs     = 64;

static SRWLOCK        SlabLock = SRWLOCK_INIT;
static std::uint8_t*  Slabs[MaxSlabs] {};
static int            SlabCount = 0;
static std::uint32_t* FreeList  = nullptr;      // threaded through the slot's first 8 bytes

static std::atomic<std::uint32_t> LiveSlots { 0 };
static std::atomic<std::uint32_t> PeakSlots { 0 };
static std::atomic<std::uint32_t> FailedAllocs { 0 };
static std::atomic<std::uint32_t> InheritedSlots { 0 };

static auto GrowSlabs() noexcept -> bool {
        if (SlabCount >= MaxSlabs) {
                return false;
        }

        auto* slab = static_cast<std::uint8_t*>(
                VirtualAlloc(nullptr, SlabBytes, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
        if (slab == nullptr) {
                return false;
        }

        Slabs[SlabCount++] = slab;
        for (std::uint32_t i = 0; i < SlotsPerSlab; ++i) {
                auto* slot = reinterpret_cast<std::uint32_t*>(slab + static_cast<std::size_t>(i) * BitmapBytes);
                *reinterpret_cast<std::uint32_t**>(slot) = FreeList;
                FreeList = slot;
        }
        return true;
}

// Everything that mutates a slot or the free list runs under SlabLock. The
// read path in BitmapOf stays lock free, which matters because GetState is
// called thousands of times a frame.
static auto SlabAllocateLocked() noexcept -> std::uint32_t* {
        if (FreeList == nullptr && !GrowSlabs()) {
                FailedAllocs.fetch_add(1, std::memory_order_relaxed);
                return nullptr;
        }

        std::uint32_t* slot = FreeList;
        FreeList = *reinterpret_cast<std::uint32_t**>(slot);

        for (std::uint32_t i = 0; i < BitmapDwords; ++i) {
                slot[i] = 0;
        }

        const std::uint32_t live = LiveSlots.fetch_add(1, std::memory_order_relaxed) + 1;
        std::uint32_t peak = PeakSlots.load(std::memory_order_relaxed);
        while (live > peak && !PeakSlots.compare_exchange_weak(peak, live, std::memory_order_relaxed)) {
        }
        return slot;
}

// Only ever hand back something that really came out of one of our slabs and
// sits on a slot boundary. A pointer that fails this is leftover bits, not a
// bitmap, and returning it to the free list would poison every later unit.
static auto OwnsSlot(const std::uint32_t* slot) noexcept -> bool {
        const auto* p = reinterpret_cast<const std::uint8_t*>(slot);
        for (int i = 0; i < SlabCount; ++i) {
                const std::uint8_t* base = Slabs[i];
                if (p >= base && p < base + SlabBytes) {
                        return (static_cast<std::size_t>(p - base) % BitmapBytes) == 0;
                }
        }
        return false;
}

// ---------------------------------------------------------------------------
// statlist helpers
// ---------------------------------------------------------------------------
static inline auto StatListOf(void* hUnit) noexcept -> std::uint8_t* {
        if (hUnit == nullptr) {
                return nullptr;
        }
        auto* statList = *reinterpret_cast<std::uint8_t**>(static_cast<std::uint8_t*>(hUnit) + UnitStatListOffset);
        if (statList == nullptr) {
                return nullptr;
        }
        // The engine gates every bitmap access on this being negative.
        if (*reinterpret_cast<const std::int32_t*>(statList + StatListGate) >= 0) {
                return nullptr;
        }
        return statList;
}

// The two slot words are read without the lock on the hot path and written
// under it, so they are accessed through atomic_ref rather than plain loads.
// On x64 this generates the same instructions a plain access would; it is
// there to make the ordering correct by the memory model instead of by
// accident of the architecture.
static inline auto OwnerRef(std::uint8_t* statList) noexcept -> std::atomic_ref<std::uintptr_t> {
        return std::atomic_ref<std::uintptr_t>(*reinterpret_cast<std::uintptr_t*>(statList + SlotOwnerOffset));
}

static inline auto SlotRef(std::uint8_t* statList) noexcept -> std::atomic_ref<std::uint32_t*> {
        return std::atomic_ref<std::uint32_t*>(*reinterpret_cast<std::uint32_t**>(statList + SlotPointerOffset));
}

static inline auto BitmapOf(std::uint8_t* statList, bool create) noexcept -> std::uint32_t* {
        const auto self = reinterpret_cast<std::uintptr_t>(statList);

        // The acquire on the owner pairs with the release below, so a reader
        // that sees its own address is guaranteed to see the pointer written
        // before it. A slot inherited through sub_1402F0D80 carries the source
        // block's address and fails here, which is the whole point.
        if (OwnerRef(statList).load(std::memory_order_acquire) == self) {
                return SlotRef(statList).load(std::memory_order_relaxed);
        }
        if (!create) {
                return nullptr;
        }

        AcquireSRWLockExclusive(&SlabLock);
        if (OwnerRef(statList).load(std::memory_order_relaxed) == self) {
                std::uint32_t* existing = SlotRef(statList).load(std::memory_order_relaxed);
                ReleaseSRWLockExclusive(&SlabLock);
                return existing;            // another thread got there first
        }

        if (SlotRef(statList).load(std::memory_order_relaxed) != nullptr) {
                InheritedSlots.fetch_add(1, std::memory_order_relaxed);
        }

        std::uint32_t* fresh = SlabAllocateLocked();
        if (fresh != nullptr) {
                SlotRef(statList).store(fresh, std::memory_order_relaxed);
                OwnerRef(statList).store(self, std::memory_order_release);
        }
        ReleaseSRWLockExclusive(&SlabLock);
        return fresh;
}

// The owner is cleared before the pointer so a concurrent reader sees either a
// valid slot or no slot, never a stale one. Crucially this only ever frees a
// bitmap the block actually owns: a block holding an inherited slot leaves the
// real owner's buffer alone.
static void ReleaseBitmap(std::uint8_t* statList) noexcept {
        const auto self = reinterpret_cast<std::uintptr_t>(statList);
        AcquireSRWLockExclusive(&SlabLock);
        if (OwnerRef(statList).load(std::memory_order_relaxed) == self) {
                std::uint32_t* bitmap = SlotRef(statList).load(std::memory_order_relaxed);
                OwnerRef(statList).store(0, std::memory_order_release);
                SlotRef(statList).store(nullptr, std::memory_order_relaxed);
                if (bitmap != nullptr && OwnsSlot(bitmap)) {
                        *reinterpret_cast<std::uint32_t**>(bitmap) = FreeList;
                        FreeList = bitmap;
                        LiveSlots.fetch_sub(1, std::memory_order_relaxed);
                }
        }
        ReleaseSRWLockExclusive(&SlabLock);
}

// ---------------------------------------------------------------------------
// The four replacements
// ---------------------------------------------------------------------------

// Returns the masked word, not a bool. Callers test for nonzero.
static auto __fastcall HookGetState(void* hUnit, int nState) noexcept -> int {
        std::uint8_t* statList = StatListOf(hUnit);
        if (statList == nullptr) {
                return 0;
        }
        if (static_cast<std::uint32_t>(nState) > IdBound) {
                return 0;
        }

        std::uint32_t* bitmap = BitmapOf(statList, false);
        if (bitmap == nullptr) {
                return 0;   // no bitmap yet means no state is set
        }
        return static_cast<int>(bitmap[static_cast<std::uint32_t>(nState) >> 5]
                                & (1U << (static_cast<std::uint32_t>(nState) & 31U)));
}

// The live half.
static auto __fastcall HookGetStateFlags(void* hUnit) noexcept -> void* {
        std::uint8_t* statList = StatListOf(hUnit);
        if (statList == nullptr) {
                return nullptr;
        }
        return BitmapOf(statList, true);
}

// The runtime FLAG, straight from the states table, exactly as every engine
// function computes it. The mirror sits this many dwords past the live half,
// which reproduces the stock adjacency byte for byte.
static inline auto RuntimeFlagDwords(void* hUnit) noexcept -> std::uint32_t {
        if (GameVersionOf == nullptr || StateCountOf == nullptr) {
                return 0;
        }
        const int count = StateCountOf(GameVersionOf(hUnit));
        if (count <= 0) {
                return 0;
        }
        const std::uint32_t flag = (static_cast<std::uint32_t>(count) + 31U) / 32U;
        return (2U * flag <= BitmapDwords) ? flag : 0;   // states.txt bigger than this build
}

// The change mirror. Seven engine functions consume this pointer, and none of
// them needs the two halves adjacent. It is placed adjacent anyway, at exactly
// 4*FLAG past the live half, because that is where stock puts it: anything
// that treats the pair as one contiguous 8*FLAG run then keeps working.
static auto __fastcall HookGetStateChangeFlags(void* hUnit) noexcept -> void* {
        std::uint8_t* statList = StatListOf(hUnit);
        if (statList == nullptr) {
                return nullptr;
        }
        const std::uint32_t flag = RuntimeFlagDwords(hUnit);
        if (flag == 0) {
                return nullptr;
        }
        std::uint32_t* bitmap = BitmapOf(statList, true);
        return bitmap != nullptr ? bitmap + flag : nullptr;
}

static auto __fastcall HookSetState(void* hUnit, int nState, int bSet) noexcept -> std::int64_t {
        std::uint8_t* statList = StatListOf(hUnit);
        if (statList == nullptr) {
                return 0;
        }
        if (static_cast<std::uint32_t>(nState) > IdBound) {
                return 0;
        }

        std::uint32_t* bitmap = BitmapOf(statList, true);
        if (bitmap == nullptr) {
                return 0;
        }

        const std::uint32_t index    = static_cast<std::uint32_t>(nState) >> 5;
        const std::uint32_t mask     = 1U << (static_cast<std::uint32_t>(nState) & 31U);
        const std::uint32_t previous = bitmap[index];
        const bool          wasSet   = (previous & mask) != 0;

        bitmap[index] = bSet ? (previous | mask) : (previous & ~mask);

        // The original returns here when the bit did not actually change.
        if (wasSet == (bSet != 0)) {
                return 0;      // not a transition, nothing to say about it
        }

        // The original fetches the version and the state count, then marks the
        // mirror at 4*FLAG past the live word. Same order, same place.
        if (GameVersionOf == nullptr || StateCountOf == nullptr || StatesTableOf == nullptr) {
                return 0;
        }

        const std::uint32_t version = GameVersionOf(hUnit);
        const int           count   = StateCountOf(version);
        const std::uint32_t flag    = (static_cast<std::uint32_t>(count) + 31U) / 32U;
        if (count > 0 && 2U * flag <= BitmapDwords) {
                bitmap[flag + index] |= mask;
        }

        auto* table = static_cast<std::uint8_t*>(StatesTableOf(version));
        if (table == nullptr) {
                return 0;
        }

        const std::uint64_t rows = *reinterpret_cast<const std::uint64_t*>(table + StatesRowCount);
        if (static_cast<std::uint64_t>(static_cast<std::uint32_t>(nState)) >= rows) {
                return 0;
        }

        auto* records = *reinterpret_cast<std::uint8_t**>(table + StatesRecordsPtr);
        if (records == nullptr) {
                return 0;
        }

        const std::uint8_t* record = records + StatesRecordStride * static_cast<std::size_t>(nState);
        if ((record[StatesRecordFlags] & StatesEventBit) == 0) {
                return 0;
        }

        int suppressed = 0;
        if (bSet == 0) {
                if (UnitHasFlag != nullptr && UnitHasFlag(hUnit, 17) != 0) {
                        suppressed = 1;
                }
        }


        if (suppressed != 0) {
                return 0;
        }

        return UnitStateEvent != nullptr ? UnitStateEvent(hUnit, 8, bSet != 0 ? 1 : 0) : 0;
}

// ---------------------------------------------------------------------------
// Lifetime. The allocator's memset(block, 0, 0xB80) clears the slot on every
// fresh block, so the only thing needed here is handing the bitmap back.
// The gate check keeps this off the 144 byte child nodes, which this function
// also frees.
// ---------------------------------------------------------------------------
static void __fastcall HookFreeStatList(std::uint8_t version, void* statList) noexcept {
        if (statList != nullptr) {
                auto* list = static_cast<std::uint8_t*>(statList);
                if (*reinterpret_cast<const std::int32_t*>(list + StatListGate) < 0) {
                        ReleaseBitmap(list);
                }
        }

        const FreeStatListFn original = OriginalFreeStatList;
        if (original != nullptr) {
                original(version, statList);
        }
}

// ---------------------------------------------------------------------------
// THE STATE SENDER, replaced outright
//
// Both engine senders carry the same defect. Each declares a SIXTEEN dword
// stack array for its copy of the gfx flags, then copies 4*FLAG bytes into it,
// then lets the stat collector write over the bytes that spilled past the end,
// then reads those bytes back as flags:
//
//   sub_14053A210   v65[16] at rbp+90h,  v66[32] at rbp+D0h   adjacent
//   sub_140539890   v112[16] at rbp+280h, v113[32] at rbp+2C0h adjacent
//
// At 795 rows FLAG is 25, so dwords 16..24 are read back as stat data and
// every bit set in them is sent to the client as a real state change. Ids 512
// to 799. That is where the spurious overlays came from.
//
// The two arrays are the last locals in both frames with nothing between them,
// so there is nowhere to move the stat buffer to. The copy has to live in a
// buffer sized from FLAG, and that means the sender has to be ours.
//
// This replaces BOTH. It reproduces sub_14053A210 exactly, which is the
// simpler of the two and needs no wire format change, because its packets
// carry the state id as a SIXTEEN bit field:
//
//   0xA9  sub_1404800A0   state cleared
//   0xA7  sub_1404801B0   state set, no stats
//   0xA8  built here      state set with stats:
//         +0 A8  +1 unitType  +2 length  +3 stateId:16  +5 unitId:32
//         +9 bitstream {statId:9, param:record[9] bits if any, value:record[8] bits}*
//            then 511 in 9 bits to close the list
//
// Using it for the bulk case too retires the 0xAA list entirely. That list was
// the only thing with a nine bit state id and the only thing that needed the
// packet splitting, the heap staging vector and the space term computed from
// sub_1412E8C80 that I could not account for. None of it is reachable now, so
// none of it has to be reproduced or patched. The cost is more packets and no
// behavioural difference: the client already handles all three types.
// ---------------------------------------------------------------------------
static constexpr std::uint32_t MaxFlagDwords = NumStates / 32U;
static constexpr int  StatPairsPerState = 16;
static constexpr int  PacketPayloadMax  = 244;
static constexpr std::uint32_t PacketHeaderBytes = 9;

static inline auto LowestSetBit(std::uint32_t word) noexcept -> int {
        if (word == 0) {
                return -1;
        }
        int bit = 0;
        while (((word >> bit) & 1U) == 0U) {
                ++bit;
        }
        return bit;
}

// Reproduces the engine's clamp before a value is packed, keyed on the signed
// flag in the itemstatcost record.
static inline auto ClampStatValue(std::int32_t value, std::uint8_t sendBits, std::uint8_t flags) noexcept -> std::uint32_t {
        if (sendBits >= 32) {
                return static_cast<std::uint32_t>(value);
        }
        if ((flags & StatSignedBit) != 0) {
                const std::int32_t low  = -(1 << (sendBits - 1));
                const std::int32_t high = (1 << (sendBits - 1)) - 1;
                if (value <= low)  { return static_cast<std::uint32_t>(low); }
                if (value >= high) { return static_cast<std::uint32_t>(high); }
                return static_cast<std::uint32_t>(value);
        }
        const std::int32_t high = (1 << sendBits) - 1;
        if (value < 0)    { return 0; }
        if (value > high) { return static_cast<std::uint32_t>(high); }
        return static_cast<std::uint32_t>(value);
}

static void SendStateWithStats(void* client, std::uint32_t version, std::uint8_t unitType,
                               std::uint32_t unitId, std::uint32_t state,
                               const std::uint32_t* stats, int count) noexcept {
        std::uint8_t packet[256] {};
        std::uint8_t writer[64] {};

        packet[0] = 0xA8;
        packet[1] = unitType;
        const std::int16_t stateField = static_cast<std::int16_t>(state);
        std::memcpy(packet + 3, &stateField, sizeof(stateField));
        std::memcpy(packet + 5, &unitId, sizeof(unitId));

        // The payload sits directly after the header, exactly as the engine
        // lays it out: v64 begins at v61 + 9.
        BitWriterInit(writer, packet + PacketHeaderBytes, PacketPayloadMax);

        bool wroteAny = false;
        for (int i = 0; i < count; ++i) {
                const std::uint32_t packed = stats[2 * i];
                const auto          value  = static_cast<std::int32_t>(stats[2 * i + 1]);
                const std::uint32_t statId = packed >> 16;
                const std::uint32_t param  = packed & 0xFFFFU;

                std::uint8_t* record = StatRecordOf(version, statId);
                if (record == nullptr) {
                        continue;
                }
                const std::uint8_t sendBits = record[StatSendBits];
                if (sendBits == 0) {
                        continue;
                }

                BitWrite(writer, statId, 9);
                if (record[StatParamBits] != 0) {
                        BitWrite(writer, param, record[StatParamBits]);
                }
                BitWrite(writer, ClampStatValue(value, sendBits, record[StatFlags]), sendBits);
                wroteAny = true;
        }

        if (!wroteAny) {
                SendStateSet(client, unitType, unitId, stateField);
                return;
        }

        BitWrite(writer, 511, 9);      // closes the stat list, nine bits by design

        const std::uint32_t length = BitLength(writer) + PacketHeaderBytes;
        if (length >= 0xFF) {
                // The length field is one byte. The engine asserts here and
                // carries on regardless; sending the flag alone is correct and
                // cannot produce a malformed packet.
                SendStateSet(client, unitType, unitId, stateField);
                return;
        }

        packet[2] = static_cast<std::uint8_t>(length);
        SendPacket(client, packet, length);
        PacketsSent.fetch_add(1, std::memory_order_relaxed);
}

static auto SendStateChanges(void* hUnit, void* client) noexcept -> std::int64_t {
        if (hUnit == nullptr || client == nullptr) {
                return 0;
        }
        if (GameVersionOf == nullptr || StatesTableOf == nullptr || GfxFlagsOf == nullptr
            || UnitTypeOf == nullptr || UnitIdOf == nullptr || GetStateOf == nullptr
            || StateStatListOf == nullptr || CollectStats == nullptr || StatRecordOf == nullptr
            || BitWriterInit == nullptr || BitWrite == nullptr || BitLength == nullptr
            || SendPacket == nullptr || SendStateSet == nullptr || SendStateClear == nullptr) {
                SenderRefused.fetch_add(1, std::memory_order_relaxed);
                return 0;
        }

        const std::uint32_t version = GameVersionOf(hUnit);
        auto* table = static_cast<std::uint8_t*>(StatesTableOf(version));
        if (table == nullptr) {
                return 0;
        }

        const std::uint64_t rows = *reinterpret_cast<const std::uint64_t*>(table + StatesRowCount);
        if (rows == 0) {
                return 0;
        }
        const std::uint32_t flag = static_cast<std::uint32_t>((rows + 31U) / 32U);
        if (flag > MaxFlagDwords) {
                // states.txt is larger than this build. Refuse rather than
                // truncate, which is the mistake the engine makes.
                SenderRefused.fetch_add(1, std::memory_order_relaxed);
                return 0;
        }

        std::uint32_t* gfx = GfxFlagsOf(hUnit);
        if (gfx == nullptr) {
                return 0;
        }

        // The whole point: a copy sized from FLAG, not a fixed sixteen dwords.
        std::uint32_t copy[MaxFlagDwords] {};
        for (std::uint32_t i = 0; i < flag; ++i) {
                copy[i] = gfx[i];
        }

        auto* records = *reinterpret_cast<std::uint8_t**>(table + StatesRecordsPtr);
        const std::uint8_t  unitType = UnitTypeOf(hUnit);
        const std::uint32_t unitId   = UnitIdOf(hUnit, "esr.extended-states", 0);

        std::uint32_t stats[2 * StatPairsPerState] {};

        for (std::uint32_t d = 0; d < flag; ++d) {
                for (int bit = LowestSetBit(copy[d]); bit >= 0; bit = LowestSetBit(copy[d])) {
                        copy[d] &= ~(1U << bit);

                        const std::uint32_t state = d * 32U + static_cast<std::uint32_t>(bit);
                        if (static_cast<std::uint64_t>(state) >= rows || records == nullptr) {
                                continue;
                        }

                        const std::uint8_t* record = records + StatesRecordStride * static_cast<std::size_t>(state);
                        if ((record[StateRecordFlags] & StateNoSendBit) != 0) {
                                continue;      // nosend
                        }

                        StatesSent.fetch_add(1, std::memory_order_relaxed);

                        if (GetStateOf(hUnit, state) == 0) {
                                SendStateClear(client, unitType, unitId, static_cast<std::int16_t>(state));
                                continue;
                        }

                        void* stateStats = StateStatListOf(hUnit, state);
                        if (stateStats == nullptr) {
                                SendStateSet(client, unitType, unitId, static_cast<std::int16_t>(state));
                                continue;
                        }

                        const int count = CollectStats(stateStats, stats, StatPairsPerState);
                        if (count <= 0) {
                                SendStateSet(client, unitType, unitId, static_cast<std::int16_t>(state));
                                continue;
                        }

                        SendStateWithStats(client, version, unitType, unitId, state, stats, count);
                }
        }

        return 0;
}

static auto __fastcall HookSendStateChanges(void* hUnit, void* client) noexcept -> std::int64_t {
        return SendStateChanges(hUnit, client);
}

static auto __fastcall HookBulkStateSender(void* hUnit, void* client) noexcept -> std::int64_t {
        return SendStateChanges(hUnit, client);
}

// ---------------------------------------------------------------------------
// Install
// ---------------------------------------------------------------------------
struct Patch {
        std::uint64_t rva;
        std::uint32_t width;
        std::uint32_t expected;
        std::uint32_t value;
        const char*   what;
};

// The state id on the wire. sub_140539890 packs it and sub_14012E9F0 unpacks
// it, both as a NINE bit field with 511 as the end of list marker. That is
// where the wrong states came from: at 795 rows the writer packed id & 511, so
// 530 arrived as 18, 531 as 19, 533 as 21, 535 as 23 and 545 as 33. Exactly
// the five that turned up on town NPCs.
//
// StateIdBits is widened to cover NumStates and the marker moves with it. The
// stat id fields at 12EAD2, 12EAE6 and 12EC0E stay at nine bits on purpose:
// itemstatcost ids genuinely fit, and widening them would desynchronise the
// stat sublist for no gain.
// The complete set, every site read out of the image byte by byte.
//
// WRITER sub_140539890
//   539FFA  mov r8d,9      state id width, with mov edx,edi (the id) after it
//   53A133  mov edx,1FFh   end of list value
//   53A13D  mov r8d,9      end of list width, right before the length calc
// READER sub_14012E9F0
//   12EA73  mov edx,9      state id width, first read
//   12EA84  cmp eax,1FFh   end of list test, first read
//   12EC23  mov edx,9      state id width, loop re-read
//   12EC34  cmp eax,1FFh   end of list test, loop re-read
//
// DELIBERATELY LEFT AT NINE BITS, these are STAT id fields on the same stream:
//   53A059 stat id width      53A09D stat list terminator   (writer)
//   12EAD2 12EAE6 12EC0E                                    (reader)
// itemstatcost ids fit in nine bits, and widening them desynchronises the stat
// sublist. My first attempt patched a signature that matched 53A09D, the stat
// terminator, which left the two sides disagreeing. The reader then ran off
// the end of the bit buffer, read zeros forever, and 0 < marker never ends.
// That was the freeze. The set below is enumerated, not pattern matched.
static constexpr std::uint32_t StateIdBits =
        (StateBits <= 9) ? 9U : static_cast<std::uint32_t>(StateBits);
static constexpr std::uint32_t StateIdMarker = (1U << StateIdBits) - 1U;

// The 0xAA bulk list is no longer emitted, so its nine bit state id field is
// unreachable and needs no patching. Every packet this build sends carries the
// id in a sixteen bit header field. Left here as a record of the format.
static_assert(StateIdBits >= 9U, "sanity");

static constexpr Patch Patches[] {
        { RvaLoadMaxStates,  4, 0x1FF, MaxStates,     "LoadStatesTxt MAX_STATES" },
        { RvaLoadBlockCap,   4, 0x200, BlockCap,      "LoadStatesTxt block cap"  },
        { RvaBlockAssertJbe, 1, 0x76,  0xEB,          "allocator block assert, jbe -> jmp" },


};

// The writer's end of list, WriteBits(w, 511, 9), is found by signature rather
// than by a fixed address because it sits among several identical nine bit
// stat writes. This pair is unique inside sub_140539890.
static constexpr std::uint8_t TerminatorPattern[] {
        0xBA, 0xFF, 0x01, 0x00, 0x00,          // mov edx, 1FFh
        0x41, 0xB8, 0x09, 0x00, 0x00, 0x00     // mov r8d, 9
};
static constexpr std::uint64_t RvaBulkSenderStart = 0x00539890;
static constexpr std::uint32_t BulkSenderSize     = 0x973;

static void ExpectedBytes(std::uint32_t value, std::uint32_t width, std::uint8_t* out) noexcept {
        for (std::uint32_t i = 0; i < width; ++i) {
                out[i] = static_cast<std::uint8_t>((value >> (8U * i)) & 0xFFU);
        }
}

static auto PatchTargetsMatch(const D2RL::PluginContext* context) noexcept -> bool {
        for (const Patch& patch : Patches) {
                std::uint8_t expected[4] {};
                ExpectedBytes(patch.expected, patch.width, expected);
                if (!context->CheckExpectedBytes(patch.rva, expected, patch.width)) {
                        D2RL::LogErrorF(context, "%s at %llX does not hold the expected %u. Nothing was installed.",
                                        patch.what, static_cast<unsigned long long>(patch.rva), patch.expected);
                        return false;
                }
        }
        return true;
}

static auto ApplyPatches(const D2RL::PluginContext* context) noexcept -> bool {
        for (const Patch& patch : Patches) {
                if (patch.value == patch.expected) {
                        continue;
                }
                std::uint8_t expected[4] {};
                ExpectedBytes(patch.expected, patch.width, expected);
                const bool ok = (patch.width == 1)
                        ? context->PatchWriteU8(patch.rva, expected, patch.width, static_cast<std::uint8_t>(patch.value))
                        : context->PatchWriteU32(patch.rva, expected, patch.width, patch.value);
                if (!ok) {
                        D2RL::LogErrorF(context, "%s at %llX failed to write.",
                                        patch.what, static_cast<unsigned long long>(patch.rva));
                        return false;
                }
        }
        return true;
}

template <std::size_t Size>
static auto SignatureMatches(const D2RL::PluginContext* context, std::uint64_t rva,
                             const std::uint8_t (&signature)[Size], const char* what) noexcept -> bool {
        if (context->CheckExpectedBytes(rva, signature, static_cast<std::uint32_t>(Size))) {
                return true;
        }
        D2RL::LogErrorF(context, "%s prologue at %llX did not match. NOTHING was installed, and with a "
                                 "states.txt above 511 rows the game will not survive that. Re-derive the RVA.",
                        what, static_cast<unsigned long long>(rva));
        return false;
}

template <typename Function, std::size_t Size>
static auto Hook(const D2RL::PluginContext* context, std::uint64_t rva, const std::uint8_t (&signature)[Size],
                 Function target, const char* what, Function* original = nullptr) noexcept -> bool {
        if (!context->InstallInlineHook(rva, signature, static_cast<std::uint32_t>(Size), target, original)) {
                D2RL::LogErrorF(context, "%s hook failed to install.", what);
                return false;
        }
        return true;
}

static auto Resolve(const D2RL::PluginContext* context) noexcept -> void {
        const std::uintptr_t base = context->exeBase;
        GameVersionOf  = reinterpret_cast<GetGameVersionFn>(base + RvaGetGameVersion);
        StateCountOf   = reinterpret_cast<GetStateCountFn>(base + RvaGetStateCount);
        StatesTableOf  = reinterpret_cast<GetStatesTableFn>(base + RvaGetStatesTable);
        UnitHasFlag    = reinterpret_cast<UnitHasFlagFn>(base + RvaUnitHasFlag);
        UnitStateEvent = reinterpret_cast<UnitStateEventFn>(base + RvaUnitStateEvent);

        UnitTypeOf      = reinterpret_cast<UnitTypeOfFn>(base + RvaUnitTypeOf);
        UnitIdOf        = reinterpret_cast<UnitIdOfFn>(base + RvaUnitIdOf);
        GfxFlagsOf      = reinterpret_cast<GfxFlagsOfFn>(base + RvaGfxFlagsWrapper);
        GetStateOf      = reinterpret_cast<GetStateOfFn>(base + RvaGetStateWrapper);
        StateStatListOf = reinterpret_cast<StateStatListOfFn>(base + RvaStateStatListOf);
        CollectStats    = reinterpret_cast<CollectStatsFn>(base + RvaCollectStats);
        StatRecordOf    = reinterpret_cast<StatRecordOfFn>(base + RvaStatRecordOf);
        BitWriterInit   = reinterpret_cast<BitWriterInitFn>(base + RvaBitWriterInit);
        BitWrite        = reinterpret_cast<BitWriteFn>(base + RvaBitWrite);
        BitLength       = reinterpret_cast<BitLengthFn>(base + RvaBitLength);
        SendPacket      = reinterpret_cast<SendPacketFn>(base + RvaSendPacket);
        SendStateSet    = reinterpret_cast<SendStateFlagFn>(base + RvaSendStateSet);
        SendStateClear  = reinterpret_cast<SendStateFlagFn>(base + RvaSendStateClear);

        UnitTypeOf      = reinterpret_cast<UnitTypeOfFn>(base + RvaUnitTypeOf);
        UnitIdOf        = reinterpret_cast<UnitIdOfFn>(base + RvaUnitIdOf);
        GetStateOf      = reinterpret_cast<GetStateFn>(base + RvaGetStateWrapper);
        StateStatListOf = reinterpret_cast<StateStatListOfFn>(base + RvaStateStatListOf);
        CollectStats    = reinterpret_cast<CollectStatsFn>(base + RvaCollectStats);
        StatRecordOf    = reinterpret_cast<StatRecordOfFn>(base + RvaStatRecordOf);
        BitWriterInit   = reinterpret_cast<BitWriterInitFn>(base + RvaBitWriterInit);
        BitWrite        = reinterpret_cast<BitWriteFn>(base + RvaBitWrite);
        BitLength       = reinterpret_cast<BitLengthFn>(base + RvaBitLength);
        SendPacket      = reinterpret_cast<SendPacketFn>(base + RvaSendPacket);
        SendStateSet    = reinterpret_cast<SendStateFlagFn>(base + RvaSendStateSet);
        SendStateClear  = reinterpret_cast<SendStateFlagFn>(base + RvaSendStateClear);
}

// ---------------------------------------------------------------------------

static constexpr D2RL::PluginInfo StatesInfo {
        .infoSize    = D2RL::PluginInfoSize,
        .apiVersion  = D2RL_PLUGIN_API_VERSION,
        .id          = "esr.extended-states",
        .name        = "ESR Extended States",
        .version     = "2.4.0",
        .author      = "Bogdan Bulai",
        .description = "Moves the unit state bitmap into plugin memory and raises the states.txt limit.",
        .flags       = D2RL::PluginFlags::Shared | D2RL::PluginFlags::NativeHooks,
};

static auto StatesCommand(D2R::Game::Client* client, const D2RL::ConsoleCommandContext* cmd, void* userData) noexcept -> D2RL::ConsoleCommandResult {
        (void)client;
        (void)userData;

        if (cmd == nullptr || cmd->plugin == nullptr) {
                return D2RL::ConsoleCommandResult::Failed;
        }

        char message[192];
        std::snprintf(message, sizeof(message), "%u states, id bound %u, %u dwords per half",
                      NumStates, IdBound, FlagDwords);
        cmd->plugin->WriteConsoleMessage(message);

        std::snprintf(message, sizeof(message), "bitmaps live in plugin memory, %u bytes each, slot at statlist+%llX",
                      BitmapBytes, static_cast<unsigned long long>(SlotPointerOffset));
        cmd->plugin->WriteConsoleMessage(message);

        std::snprintf(message, sizeof(message), "sender: %u packets, %u states, %u refusals; id is 16 bit in the header",
                      PacketsSent.load(std::memory_order_relaxed),
                      StatesSent.load(std::memory_order_relaxed),
                      SenderRefused.load(std::memory_order_relaxed));
        cmd->plugin->WriteConsoleMessage(message);

        std::snprintf(message, sizeof(message), "slabs %d, in use %u, peak %u, failed %u, inherited slots rejected %u",
                      SlabCount, LiveSlots.load(std::memory_order_relaxed),
                      PeakSlots.load(std::memory_order_relaxed),
                      FailedAllocs.load(std::memory_order_relaxed),
                      InheritedSlots.load(std::memory_order_relaxed));
        cmd->plugin->WriteConsoleMessage(message);

        if (StatesTableOf != nullptr && GameVersionOf != nullptr) {
                auto* table = static_cast<std::uint8_t*>(StatesTableOf(1));
                if (table != nullptr) {
                        const std::uint64_t rows = *reinterpret_cast<const std::uint64_t*>(table + StatesRowCount);
                        std::snprintf(message, sizeof(message), "states.txt rows %llu, runtime FLAG %llu",
                                      static_cast<unsigned long long>(rows),
                                      static_cast<unsigned long long>((rows + 31) / 32));
                        cmd->plugin->WriteConsoleMessage(message);
                }
        }

        return D2RL::ConsoleCommandResult::Handled;
}

D2RL_PLUGIN_EXPORT auto D2RLoaderGetPluginInfo() noexcept -> const D2RL::PluginInfo* {
        return &StatesInfo;
}

D2RL_PLUGIN_EXPORT auto D2RLoaderLoadPlugin(const D2RL::PluginContext* context) noexcept -> bool {
        if (context == nullptr) {
                return false;
        }

        Context = context;
        Resolve(context);

        D2RL::LogInfoF(context, "esr.extended-states 2.4.0: %u states, %u byte bitmaps in plugin memory, game pool untouched.",
                       NumStates, BitmapBytes);

        if (!context->RegisterConsoleCommand("esr-states", StatesCommand, "Show the ESR state limit and bitmap storage.")) {
                context->LogWarn("esr-states console command was not registered.");
        }

        // Verify every prologue and every patch target before touching anything.
        // A half installed set, four accessors replaced and one still reading
        // the dead array, would be worse than not installing at all.
        if (!SignatureMatches(context, RvaGetState, SigGetState, "StatsGetState")
            || !SignatureMatches(context, RvaGetStateFlags, SigGetStateFlags, "StatsGetStateFlags")
            || !SignatureMatches(context, RvaGetChangeFlags, SigGetChangeFlags, "StatsGetStateChangeFlags")
            || !SignatureMatches(context, RvaSetState, SigSetState, "StatsSetState")
            || !SignatureMatches(context, RvaFreeStatList, SigFreeStatList, "FreeStatList")
            || !SignatureMatches(context, RvaSendStateChanges, SigSendStateChanges, "state sender")
            || !SignatureMatches(context, RvaBulkStateSender, SigBulkStateSender, "bulk state sender")

            || !PatchTargetsMatch(context)) {
                return false;
        }

        if (!Hook(context, RvaGetState, SigGetState, HookGetState, "StatsGetState")
            || !Hook(context, RvaGetStateFlags, SigGetStateFlags, HookGetStateFlags, "StatsGetStateFlags")
            || !Hook(context, RvaGetChangeFlags, SigGetChangeFlags, HookGetStateChangeFlags, "StatsGetStateChangeFlags")
            || !Hook(context, RvaSetState, SigSetState, HookSetState, "StatsSetState")
            || !Hook(context, RvaFreeStatList, SigFreeStatList, HookFreeStatList, "FreeStatList", &OriginalFreeStatList)
            || !Hook(context, RvaSendStateChanges, SigSendStateChanges, HookSendStateChanges, "state sender", &OriginalSendStateChanges)
            || !Hook(context, RvaBulkStateSender, SigBulkStateSender, HookBulkStateSender, "bulk state sender", &OriginalBulkStateSender)) {
                return false;
        }

        if (!ApplyPatches(context)) {
                return false;
        }

        context->LogInfo("Hooks installed and cap patches applied.");
        return true;
}

D2RL_PLUGIN_EXPORT void D2RLoaderUnloadPlugin() noexcept {
        Context = nullptr;
}
