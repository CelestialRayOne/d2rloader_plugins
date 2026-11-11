// Weapon Swap Queue (celestialrayone.weapon-swap-queue)
//
// Stock D2R throws away a Swap Weapons press made while the character is
// busy. This plugin remembers one such press and performs the swap through
// the game's own Swap Weapons action on the first frame the stock client
// rules allow it, and keeps a held attack button from starting the next
// attack before that (A1, swap, A1 instead of A1, A1, A1, swap).
//
// How the stock game decides (D2R 3.3.93847 process image, D2RLoader 1.3.1):
//   Swap Weapons action 0x11BF00: needs world (or inventory) input focus,
//     asks the gate below, and on yes reads the active weapon set from the UI
//     model and tail-jumps into the request sender 0x1E33A0.
//   Swap gate 0x14B380 (inputManager, localDataContext) -> bool: refuses while
//     the player mode is A1, A2, SC, TH, KK, S1-S4 or SQ (jump table 0x14B4F8),
//     while a controller hold binding is active (input record +0x196C != -1),
//     or while PlayerData+0x1C8 is set.
//   Held mouse buttons: every client frame (0x8B3C0) the held left button
//     calls the mouse action dispatcher 0x0FE3B0 with kind 1 (call 0x8BA1A)
//     and the held right button with kind 4 (call 0x8BAA7). The left path
//     (0x1013B0, pre-check 0x1023D0) re-issues the left skill even while the
//     current attack animation is still playing, so a held button chains the
//     next attack straight after the current one and the gate never sees the
//     character idle.
//   Server reply 0x12E1E0: the only writer of the UI weapon-set value (the
//     client-local swap path 0x15B920 also flips it, synchronously). A changed
//     value therefore means the swap happened.
//
// The request sent to the server carries the weapon set it starts from, and
// the server refuses a request whose set is stale. Repeating the same request
// can never swap twice. The plugin relies on that to resend when the server
// turned the first request down because its own copy of the animation had not
// finished yet ("Attempted sWeaponSwap while using a skill").
//
// Every RVA is verified against the exact bytes below before anything is
// installed. Any mismatch refuses to load.

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <D2RLPlugin/api.h>

#include <atomic>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <string_view>

#if defined(_MSC_VER)
#include <intrin.h>
#pragma intrinsic(_ReturnAddress)
#define WSQ_CALLER() reinterpret_cast<std::uintptr_t>(_ReturnAddress())
#else
#define WSQ_CALLER() reinterpret_cast<std::uintptr_t>(__builtin_return_address(0))
#endif

namespace {

// ---------------------------------------------------------------------------
// Native addresses (RVAs)
// ---------------------------------------------------------------------------

constexpr std::uint64_t kSwapActionRva        = 0x11BF00; // Swap Weapons action, no arguments
constexpr std::uint64_t kSwapGateReturnRva    = 0x11BF3F; // return address of its gate call (0x11BF3A)
constexpr std::uint64_t kSwapGateRva          = 0x14B380; // client swap gate
constexpr std::uint64_t kSwapGateModeTableRva = 0x14B4F8; // gate jump table, modes 7..18
constexpr std::uint64_t kReadUiModelBoolRva   = 0x11C860; // (model, key) -> byte, lookup only
constexpr std::uint64_t kMouseActionRva       = 0x0FE3B0; // (player, kind, x, y, flags) mouse action dispatcher
constexpr std::uint64_t kHeldButtonsRva       = 0x08B990; // held left/right button block in the client frame
constexpr std::uint64_t kHeldLeftReturnRva    = 0x08BA1F; // after the held left button call at 0x8BA1A (kind 1)
constexpr std::uint64_t kHeldRightReturnRva   = 0x08BAAC; // after the held right button call at 0x8BAA7 (kind 4)
constexpr std::uint64_t kGetInputManagerRva   = 0x144640; // called by the action, passed to the gate
constexpr std::uint64_t kGetLocalContextRva   = 0x08B2D0; // CLIENT_GetLocalDataContext
constexpr std::uint64_t kGetLocalPlayerRva    = 0x09A480; // CLIENT_GetLocalPlayer(context)
constexpr std::uint64_t kGetUnitModeRva       = 0x34AB60; // UNITS_GetUnitMode(unit)
constexpr std::uint64_t kGetUiModelRva        = 0x8461C0; // UI model root, used by the action

// UI model key of the active weapon set (0 or 1).
constexpr std::uint64_t kWeaponSetKey = 0xF2D7CF8E9CC08212ULL;

// Input manager: one 0xB8-byte record per local data context; the held
// controller binding id sits at +0x196C from the manager base (-1 = none).
constexpr std::uintptr_t kInputRecordStride = 0xB8;
constexpr std::uintptr_t kHeldInputOffset   = 0x196C;
constexpr std::uint32_t  kMaxLocalContexts  = 8;     // the gate asserts context < 8
constexpr std::int32_t   kNoHeldInput       = -1;
constexpr std::int32_t   kHeldInputUnknown  = INT32_MIN;

// Player modes (plrmode.txt order).
constexpr std::int32_t kModeDeath = 0;  // DT
constexpr std::int32_t kModeDead  = 17; // DD

// Queue timing. Internal design constants, not settings.
constexpr std::uint64_t kQueueLifetimeMs  = 3000; // an unsent swap must become sendable within this
constexpr std::uint64_t kIdleGiveUpMs     = 300;  // idle but still refused by the game: give up
constexpr std::uint64_t kResendIntervalMs = 80;   // resend while unconfirmed (two server frames)
constexpr std::uint64_t kConfirmWindowMs  = 1500; // stop resending after this

// ---------------------------------------------------------------------------
// Exact bytes for D2R 3.3.93847 under D2RLoader 1.3.1
// ---------------------------------------------------------------------------

// Whole Swap Weapons action 0x11BF00..0x11BF6F. Pins the gate call site and
// every callee this plugin also calls (input manager, local context, gate, UI
// model root, weapon-set reader) plus the weapon-set key.
constexpr std::uint8_t kSwapActionBytes[] = {
    0x48, 0x83, 0xEC, 0x28, 0xE8, 0x87, 0x0F, 0x02, 0x00, 0x48, 0x8B, 0xC8,
    0xE8, 0x4F, 0x1F, 0x02, 0x00, 0x84, 0xC0, 0x75, 0x0E, 0xB9, 0x01, 0x00,
    0x00, 0x00, 0xE8, 0xE1, 0x25, 0xFB, 0xFF, 0x84, 0xC0, 0x74, 0x48, 0x48,
    0x89, 0x5C, 0x24, 0x20, 0xE8, 0x13, 0x87, 0x02, 0x00, 0x48, 0x8B, 0xD8,
    0xE8, 0x9B, 0xF3, 0xF6, 0xFF, 0x8B, 0xD0, 0x48, 0x8B, 0xCB, 0xE8, 0x41,
    0xF4, 0x02, 0x00, 0x48, 0x8B, 0x5C, 0x24, 0x20, 0x84, 0xC0, 0x74, 0x23,
    0xE8, 0x73, 0xA2, 0x72, 0x00, 0x48, 0xBA, 0x12, 0x82, 0xC0, 0x9C, 0x8E,
    0xCF, 0xD7, 0xF2, 0x48, 0x8B, 0xC8, 0xE8, 0x01, 0x09, 0x00, 0x00, 0x0F,
    0xB6, 0xC8, 0x48, 0x83, 0xC4, 0x28, 0xE9, 0x35, 0x74, 0x0C, 0x00, 0x48,
    0x83, 0xC4, 0x28, 0xC3,
};

// Swap gate 0x14B380..0x14B4EF. Pins the held-input offset 0x196C, the
// PlayerData+0x1C8 read, the local-player and unit-mode callees and the jump
// table base. The first 16 bytes (register saves and stack setup, no relative
// operands) are where the hook goes.
constexpr std::uint8_t kSwapGateBytes[] = {
    0x48, 0x89, 0x5C, 0x24, 0x20, 0x55, 0x56, 0x57, 0x41, 0x56, 0x41, 0x57,
    0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B, 0xF1, 0x8B, 0xDA, 0x8B, 0xCA, 0xE8,
    0xE4, 0xF0, 0xF4, 0xFF, 0x48, 0x89, 0x5C, 0x24, 0x50, 0x48, 0x8B, 0xF8,
    0x48, 0x83, 0xFB, 0x08, 0x72, 0x19, 0x48, 0x8D, 0x44, 0x24, 0x50, 0x48,
    0x8D, 0x4C, 0x24, 0x60, 0x48, 0x89, 0x44, 0x24, 0x60, 0xE8, 0x32, 0x7B,
    0xFF, 0xFF, 0x84, 0xC0, 0x74, 0x01, 0xCC, 0x48, 0x69, 0x6C, 0x24, 0x50,
    0xB8, 0x00, 0x00, 0x00, 0x44, 0x8B, 0xBC, 0x2E, 0x6C, 0x19, 0x00, 0x00,
    0x48, 0x85, 0xFF, 0x74, 0x0C, 0x48, 0x8B, 0xCF, 0xE8, 0xEF, 0x05, 0x20,
    0x00, 0x85, 0xC0, 0x74, 0x19, 0x48, 0x8D, 0x4C, 0x24, 0x58, 0xC6, 0x44,
    0x24, 0x58, 0x00, 0xE8, 0x6C, 0xE1, 0xFA, 0xFF, 0x84, 0xC0, 0x74, 0x01,
    0xCC, 0x48, 0x85, 0xFF, 0x74, 0x37, 0x48, 0x8B, 0xCF, 0xE8, 0xCA, 0x05,
    0x20, 0x00, 0x85, 0xC0, 0x75, 0x2B, 0x48, 0x83, 0x7F, 0x10, 0x00, 0x75,
    0x13, 0x48, 0x8D, 0x4C, 0x24, 0x58, 0x88, 0x44, 0x24, 0x58, 0xE8, 0x71,
    0xDE, 0xFA, 0xFF, 0x84, 0xC0, 0x74, 0x01, 0xCC, 0x48, 0x8B, 0x47, 0x10,
    0x48, 0x85, 0xC0, 0x74, 0x08, 0x8B, 0x98, 0xC8, 0x01, 0x00, 0x00, 0xEB,
    0x02, 0x33, 0xDB, 0x48, 0x8B, 0xCF, 0x45, 0x32, 0xF6, 0xE8, 0x1E, 0xF7,
    0x1F, 0x00, 0x83, 0xC0, 0xF9, 0x83, 0xF8, 0x0B, 0x77, 0x16, 0x48, 0x8D,
    0x15, 0xAF, 0x4B, 0xEB, 0xFF, 0x8B, 0x8C, 0x82, 0xF8, 0xB4, 0x14, 0x00,
    0x48, 0x03, 0xCA, 0xFF, 0xE1, 0x41, 0xB6, 0x01, 0x41, 0x83, 0xFF, 0xFF,
    0x75, 0x10, 0x85, 0xDB, 0x75, 0x0C, 0x45, 0x84, 0xF6, 0x75, 0x07, 0xB3,
    0x01, 0x0F, 0xB6, 0xFB, 0xEB, 0x28, 0x32, 0xD2, 0x40, 0x32, 0xFF, 0x41,
    0x83, 0xFF, 0xFF, 0x74, 0x17, 0x8B, 0x8C, 0x2E, 0x6C, 0x19, 0x00, 0x00,
    0xE8, 0x53, 0xC9, 0xFC, 0xFF, 0x84, 0xC0, 0x0F, 0x94, 0xC2, 0x84, 0xC0,
    0x40, 0x0F, 0x95, 0xC7, 0x32, 0xDB, 0x84, 0xD2, 0x74, 0x1F, 0x4C, 0x8D,
    0x85, 0x8C, 0x19, 0x00, 0x00, 0x41, 0xB1, 0x01, 0x48, 0x8D, 0x95, 0x7C,
    0x19, 0x00, 0x00, 0x4C, 0x03, 0xC6, 0x48, 0x03, 0xD6, 0x48, 0x8B, 0xCE,
    0xE8, 0xF3, 0xEC, 0xFF, 0xFF, 0x40, 0x84, 0xFF, 0x74, 0x1F, 0x4C, 0x8D,
    0x85, 0x90, 0x19, 0x00, 0x00, 0x45, 0x33, 0xC9, 0x48, 0x8D, 0x95, 0x84,
    0x19, 0x00, 0x00, 0x4C, 0x03, 0xC6, 0x48, 0x03, 0xD6, 0x48, 0x8B, 0xCE,
    0xE8, 0xCF, 0xEC, 0xFF, 0xFF, 0x0F, 0xB6, 0xC3, 0x48, 0x8B, 0x5C, 0x24,
    0x68, 0x48, 0x83, 0xC4, 0x20, 0x41, 0x5F, 0x41,
};
constexpr std::uint32_t kSwapGateHookBytes = 16;

// Gate jump table for modes 7..18: 0x14B45D = refuse, 0x14B460 = allow.
// Refuse: 7 A1, 8 A2, 10 SC, 11 TH, 12 KK, 13 S1, 14 S2, 15 S3, 16 S4, 18 SQ.
// Allow:  9 BL, 17 DD.
constexpr std::uint8_t kSwapGateModeTable[] = {
    0x5D, 0xB4, 0x14, 0x00, 0x5D, 0xB4, 0x14, 0x00, 0x60, 0xB4, 0x14, 0x00,
    0x5D, 0xB4, 0x14, 0x00, 0x5D, 0xB4, 0x14, 0x00, 0x5D, 0xB4, 0x14, 0x00,
    0x5D, 0xB4, 0x14, 0x00, 0x5D, 0xB4, 0x14, 0x00, 0x5D, 0xB4, 0x14, 0x00,
    0x5D, 0xB4, 0x14, 0x00, 0x60, 0xB4, 0x14, 0x00, 0x5D, 0xB4, 0x14, 0x00,
};

// Weapon-set reader 0x11C860..0x11C8EF: hash lookup, no insertion.
constexpr std::uint8_t kReadUiModelBoolBytes[] = {
    0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x4C, 0x8B, 0x49, 0x08, 0x4C, 0x8B,
    0xC2, 0x4D, 0x85, 0xC9, 0x0F, 0x84, 0x98, 0x00, 0x00, 0x00, 0x48, 0x8B,
    0xC2, 0x48, 0xC1, 0xE8, 0x21, 0x48, 0x33, 0xC2, 0x48, 0xBA, 0xCD, 0x8C,
    0x55, 0xED, 0xD7, 0xAF, 0x51, 0xFF, 0x48, 0x0F, 0xAF, 0xC2, 0x48, 0x8B,
    0xD0, 0x48, 0xC1, 0xEA, 0x21, 0x48, 0x33, 0xD0, 0x48, 0xB8, 0x53, 0xEC,
    0x85, 0x1A, 0xFE, 0xB9, 0xCE, 0xC4, 0x48, 0x0F, 0xAF, 0xD0, 0x48, 0x8B,
    0xC2, 0x48, 0xC1, 0xE8, 0x21, 0x48, 0x33, 0xD0, 0x48, 0x8B, 0x01, 0x48,
    0xFF, 0xC8, 0x48, 0x23, 0xC2, 0x49, 0x8D, 0x0C, 0xC1, 0x49, 0x8B, 0x04,
    0xC1, 0x48, 0x85, 0xC0, 0x74, 0x11, 0x4C, 0x3B, 0x40, 0x08, 0x74, 0x0B,
    0x48, 0x8B, 0xC8, 0x48, 0x8B, 0x00, 0x48, 0x85, 0xC0, 0x75, 0xEF, 0x48,
    0x8B, 0x19, 0x48, 0x85, 0xDB, 0x74, 0x2F, 0x48, 0x83, 0x7B, 0x10, 0x00,
    0x75, 0x14, 0x48, 0x8D, 0x4C, 0x24, 0x30, 0xC6, 0x44, 0x24, 0x30, 0x00,
};

// Mouse action dispatcher entry 0x0FE3B0..0x0FE3EF. The first 16 bytes
// (register saves, no relative operands) are where the hook goes.
constexpr std::uint8_t kMouseActionBytes[] = {
    0x48, 0x89, 0x5C, 0x24, 0x10, 0x55, 0x56, 0x57, 0x41, 0x54, 0x41, 0x55,
    0x41, 0x56, 0x41, 0x57, 0x48, 0x8D, 0xAC, 0x24, 0xC0, 0xFB, 0xFF, 0xFF,
    0x48, 0x81, 0xEC, 0x40, 0x05, 0x00, 0x00, 0x48, 0x8B, 0x05, 0xF2, 0xCE,
    0x8C, 0x02, 0x48, 0x33, 0xC4, 0x48, 0x89, 0x85, 0x30, 0x04, 0x00, 0x00,
    0x41, 0x8B, 0xD9, 0x4C, 0x63, 0xF2, 0x89, 0x5D, 0x90, 0x45, 0x8B, 0xE8,
    0x4C, 0x8B, 0xE1, 0xE8,
};
constexpr std::uint32_t kMouseActionHookBytes = 16;

// Held mouse button block of the client frame 0x8B990..0x8BAAF: pins the two
// calls into the dispatcher (0x8BA1A kind 1, 0x8BAA7 kind 4) and therefore the
// two return addresses the hook filters on.
constexpr std::uint8_t kHeldButtonsBytes[] = {
    0x0F, 0x84, 0x89, 0x00, 0x00, 0x00, 0x83, 0x3D, 0x27, 0x7A, 0x99, 0x02,
    0x00, 0xC7, 0x05, 0xA1, 0x77, 0x20, 0x02, 0x08, 0x00, 0x00, 0x00, 0x74,
    0x76, 0x83, 0x3D, 0xCC, 0x7A, 0x99, 0x02, 0x00, 0x75, 0x6D, 0x8B, 0x0D,
    0x4C, 0x7D, 0x99, 0x02, 0xC7, 0x05, 0xBE, 0x7A, 0x99, 0x02, 0x01, 0x00,
    0x00, 0x00, 0xE8, 0xB9, 0xEA, 0x00, 0x00, 0x48, 0x8B, 0xF8, 0xE8, 0x01,
    0xF6, 0xFF, 0xFF, 0x48, 0x8B, 0xCF, 0x0F, 0xB7, 0xD8, 0xE8, 0xF6, 0xFF,
    0x2B, 0x00, 0x85, 0xC0, 0x75, 0x41, 0xE8, 0x0D, 0x38, 0x0C, 0x00, 0x89,
    0x44, 0x24, 0x50, 0xE8, 0x14, 0x38, 0x0C, 0x00, 0x48, 0x8D, 0x54, 0x24,
    0x4C, 0x89, 0x44, 0x24, 0x4C, 0x48, 0x8D, 0x4C, 0x24, 0x50, 0xE8, 0xD1,
    0x39, 0x00, 0x00, 0x84, 0xC0, 0x74, 0x1C, 0x44, 0x8B, 0x4C, 0x24, 0x4C,
    0xBA, 0x01, 0x00, 0x00, 0x00, 0x44, 0x8B, 0x44, 0x24, 0x50, 0x48, 0x8B,
    0xCF, 0x66, 0x89, 0x5C, 0x24, 0x20, 0xE8, 0x91, 0x29, 0x07, 0x00, 0x83,
    0x3D, 0x52, 0x7A, 0x99, 0x02, 0x00, 0x0F, 0x84, 0x80, 0x00, 0x00, 0x00,
    0x83, 0x3D, 0x91, 0x79, 0x99, 0x02, 0x00, 0xC7, 0x05, 0x57, 0x7A, 0x20,
    0x02, 0x08, 0x00, 0x00, 0x00, 0x74, 0x6D, 0x8B, 0x0D, 0xBF, 0x7C, 0x99,
    0x02, 0xC7, 0x05, 0x35, 0x7A, 0x99, 0x02, 0x01, 0x00, 0x00, 0x00, 0xE8,
    0x2C, 0xEA, 0x00, 0x00, 0x48, 0x8B, 0xF8, 0xE8, 0x74, 0xF5, 0xFF, 0xFF,
    0x48, 0x8B, 0xCF, 0x0F, 0xB7, 0xD8, 0xE8, 0x69, 0xFF, 0x2B, 0x00, 0x85,
    0xC0, 0x75, 0x41, 0xE8, 0x80, 0x37, 0x0C, 0x00, 0x89, 0x44, 0x24, 0x58,
    0xE8, 0x87, 0x37, 0x0C, 0x00, 0x48, 0x8D, 0x54, 0x24, 0x54, 0x89, 0x44,
    0x24, 0x54, 0x48, 0x8D, 0x4C, 0x24, 0x58, 0xE8, 0x44, 0x39, 0x00, 0x00,
    0x84, 0xC0, 0x74, 0x1C, 0x44, 0x8B, 0x4C, 0x24, 0x54, 0xBA, 0x04, 0x00,
    0x00, 0x00, 0x44, 0x8B, 0x44, 0x24, 0x58, 0x48, 0x8B, 0xCF, 0x66, 0x89,
    0x5C, 0x24, 0x20, 0xE8, 0x04, 0x29, 0x07, 0x00, 0xBA, 0x01, 0x00, 0x00,
};

constexpr auto IsBusyMode(std::int32_t mode) noexcept -> bool {
    switch (mode) {
        case 7:  // A1
        case 8:  // A2
        case 10: // SC
        case 11: // TH
        case 12: // KK
        case 13: // S1
        case 14: // S2
        case 15: // S3
        case 16: // S4
        case 18: // SQ
            return true;
        default:
            return false;
    }
}

constexpr auto ModeName(std::int32_t mode) noexcept -> const char* {
    constexpr const char* names[] = {"DT", "NU", "WL", "RN", "GH", "TN", "TW", "A1", "A2", "BL",
                                     "SC", "TH", "KK", "S1", "S2", "S3", "S4", "DD", "SQ", "KB"};
    return (mode >= 0 && mode < 20) ? names[mode] : "??";
}

constexpr char kDefaultToml[] = R"TOML(# Weapon Swap Queue
# Plugin id: celestialrayone.weapon-swap-queue
#
# WHAT IT DOES
#   Stock D2R ignores the Swap Weapons key while your character is busy, so
#   the press is lost and you end up pressing it several times, which risks
#   swapping back and forth. With this plugin one press made while busy is
#   remembered, and the swap happens as soon as the current animation ends.
#
#   "Busy" means exactly the player modes where the stock game drops the
#   press: A1, A2 (attacks), SC (cast), TH (throw), KK (kick), S1, S2, S3,
#   S4 (skill animations) and SQ (sequences such as Whirlwind).
#
#   Holding an attack button does not delay the swap. While a swap is
#   waiting, the held left or right mouse button stops repeating, the swap
#   fires when the current animation ends, and the held button carries on
#   with the new weapon: A1, swap, A1.
#
# RULES
#   - One swap at most. Extra presses while a swap is waiting do nothing.
#   - The swap remembers the weapon set you were on when you pressed the
#     key. If the set changes before it fires (any other swap), it is
#     dropped, so it can never swap you back.
#   - Every swap request is followed until the server confirms it. If the
#     server turns it down (its copy of your animation can end a little
#     later than yours), the same request is sent again every 80 ms, for at
#     most 1.5 seconds. The request carries the weapon set it starts from
#     and the server rejects stale ones, so a repeat can never cause a
#     second swap.
#   - A waiting swap is dropped if it still cannot be sent 3 seconds after
#     the press, if the character stands idle for 0.3 seconds while the
#     game still refuses it, if you die, or if you leave the game.
#   - Server rules are untouched.
#   - Any key bound to Swap Weapons works: the plugin hooks the game's Swap
#     Weapons action, not a specific key. The controller swap button keeps
#     stock behaviour.

[weapon-swap-queue]

# true  = queue presses made while busy.
# false = stock behaviour; the plugin loads but installs nothing.
enabled = true

# true = write each queue event (queued, sent, held button paused,
# confirmed, dropped) to this plugin's log in d2rloader/logs. It only writes
# when you press Swap Weapons, so it is cheap to leave on.
debug_log = true
)TOML";

// ---------------------------------------------------------------------------
// Native function types
// ---------------------------------------------------------------------------

using SwapGateFn        = std::uint8_t (*)(void* inputManager, std::uint32_t localContext) noexcept;
using SwapActionFn      = void (*)() noexcept;
using MouseActionFn     = std::int64_t (*)(void* player, std::int32_t kind, std::uint32_t x, std::uint32_t y,
                                           std::uint16_t flags) noexcept;
using GetPointerFn      = void* (*)() noexcept;
using GetContextFn      = std::uint32_t (*)() noexcept;
using GetLocalPlayerFn  = void* (*)(std::uint32_t localContext) noexcept;
using GetUnitModeFn     = std::int32_t (*)(void* unit) noexcept;
using ReadUiModelBoolFn = std::uint8_t (*)(void* model, std::uint64_t key) noexcept;

// ---------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------

struct QueuedSwap {
    bool          pending     = false;
    void*         player      = nullptr; // local player unit at the press
    std::uint8_t  weaponSet   = 0;       // weapon set at the press
    std::uint64_t queuedAt    = 0;
    std::uint64_t idleSince   = 0;       // first moment seen idle while still unsent
    std::uint64_t firstSendAt = 0;
    std::uint64_t lastSendAt  = 0;
    std::uint32_t sends       = 0;
    bool          heldLogged  = false;
};

struct Globals {
    const D2RL::PluginContext* ctx      = nullptr;
    const D2RL::ThreadService* threads  = nullptr;
    std::uintptr_t             exeBase  = 0;
    bool                       debugLog = true;

    SwapGateFn        originalGate        = nullptr;
    MouseActionFn     originalMouseAction = nullptr;
    SwapActionFn      swapAction          = nullptr;
    GetPointerFn      getInputManager     = nullptr;
    GetContextFn      getLocalContext     = nullptr;
    GetLocalPlayerFn  getLocalPlayer      = nullptr;
    GetUnitModeFn     getUnitMode         = nullptr;
    GetPointerFn      getUiModel          = nullptr;
    ReadUiModelBoolFn readUiModelBool     = nullptr;

    std::atomic<bool> pollScheduled{false};
    bool              firing = false; // the queue itself is running the action
    QueuedSwap        queue;

    std::uintptr_t loggedCallers[8] = {};
};

Globals g;

// ---------------------------------------------------------------------------
// Logging
// ---------------------------------------------------------------------------

void Write(int level, const char* format, va_list args) noexcept {
    if (g.ctx == nullptr) {
        return;
    }
    char buffer[512];
    std::snprintf(buffer, sizeof(buffer), "WeaponSwapQueue: ");
    const std::size_t prefix = std::strlen(buffer);
    std::vsnprintf(buffer + prefix, sizeof(buffer) - prefix, format, args);
    if (level == 2) {
        g.ctx->LogError(buffer);
    } else if (level == 1) {
        g.ctx->LogWarn(buffer);
    } else {
        g.ctx->LogInfo(buffer);
    }
}

void Info(const char* format, ...) noexcept {
    va_list args;
    va_start(args, format);
    Write(0, format, args);
    va_end(args);
}

void Error(const char* format, ...) noexcept {
    va_list args;
    va_start(args, format);
    Write(2, format, args);
    va_end(args);
}

void Debug(const char* format, ...) noexcept {
    if (!g.debugLog) {
        return;
    }
    va_list args;
    va_start(args, format);
    Write(0, format, args);
    va_end(args);
}

// ---------------------------------------------------------------------------
// Game reads
// ---------------------------------------------------------------------------

auto ReadHeldInput(void* inputManager, std::uint32_t localContext) noexcept -> std::int32_t {
    if (inputManager == nullptr || localContext >= kMaxLocalContexts) {
        return kHeldInputUnknown;
    }
    const auto address = reinterpret_cast<std::uintptr_t>(inputManager)
        + static_cast<std::uintptr_t>(localContext) * kInputRecordStride + kHeldInputOffset;
    return *reinterpret_cast<const std::int32_t*>(address);
}

auto LocalPlayer(std::uint32_t localContext) noexcept -> void* {
    return localContext < kMaxLocalContexts ? g.getLocalPlayer(localContext) : nullptr;
}

auto ReadWeaponSet(std::uint8_t& weaponSet) noexcept -> bool {
    void* model = g.getUiModel();
    if (model == nullptr) {
        return false;
    }
    weaponSet = g.readUiModelBool(model, kWeaponSetKey);
    return true;
}

// ---------------------------------------------------------------------------
// Queue
// ---------------------------------------------------------------------------

void __cdecl PollQueuedSwap(const D2RL::PluginContext* context, void* userData) noexcept;

void Clear() noexcept {
    g.queue = QueuedSwap{};
}

void SchedulePoll() noexcept {
    if (g.pollScheduled.exchange(true)) {
        return;
    }
    if (g.threads->runOnUiThread(g.ctx, PollQueuedSwap, nullptr) != D2RL::Threads::Result::Success) {
        g.pollScheduled.store(false);
        Clear();
        Error("could not schedule the per-frame check; queued swap dropped.");
    }
}

void Drop(const char* reason) noexcept {
    Debug("swap dropped: %s.", reason);
    Clear();
}

// Advances a pending swap: drops or finishes it, or runs the stock Swap
// Weapons action when a (re)send is due. Returns true while it stays pending.
// Called from the UI-thread poll and from the held mouse button hook, both on
// the client's main thread.
auto ServiceQueue() noexcept -> bool {
    QueuedSwap& q = g.queue;
    if (!q.pending) {
        return false;
    }

    const std::uint64_t now          = GetTickCount64();
    const std::uint32_t localContext = g.getLocalContext();
    void*               player       = LocalPlayer(localContext);
    if (player == nullptr || player != q.player) {
        Drop("the character left the game");
        return false;
    }

    const std::int32_t mode = g.getUnitMode(player);
    if (mode == kModeDeath || mode == kModeDead) {
        Drop("the character died");
        return false;
    }

    std::uint8_t weaponSet = 0;
    if (!ReadWeaponSet(weaponSet)) {
        Drop("the weapon set could not be read");
        return false;
    }
    if (weaponSet != q.weaponSet) {
        if (q.sends == 0) {
            Debug("weapon set changed by another swap before the queued one fired; dropped.");
        } else {
            Debug("swap confirmed by the server, %llu ms after the press (%u request(s)).",
                static_cast<unsigned long long>(now - q.queuedAt), q.sends);
        }
        Clear();
        return false;
    }

    if (q.sends == 0) {
        if (now - q.queuedAt > kQueueLifetimeMs) {
            Drop("still busy 3 seconds after the press");
            return false;
        }
        if (IsBusyMode(mode)) {
            q.idleSince = 0;
        } else if (q.idleSince == 0) {
            q.idleSince = now;
        } else if (now - q.idleSince > kIdleGiveUpMs) {
            Drop("the character stood idle and the game still refused the swap");
            return false;
        }
    } else if (now - q.firstSendAt > kConfirmWindowMs) {
        Drop("the server did not confirm within 1.5 seconds");
        return false;
    }

    // Run the stock action only with no held controller binding: in that
    // state a refusal by the gate changes nothing, and an approval is exactly
    // the stock swap.
    const bool due = q.sends == 0 || now - q.lastSendAt >= kResendIntervalMs;
    if (due && ReadHeldInput(g.getInputManager(), localContext) == kNoHeldInput) {
        g.firing = true;
        g.swapAction();
        g.firing = false;
    }
    return g.queue.pending;
}

// Runs on the UI thread, once per root-widget update while a swap is pending.
void __cdecl PollQueuedSwap(const D2RL::PluginContext* context, void* userData) noexcept {
    (void)context;
    (void)userData;
    g.pollScheduled.store(false);
    if (ServiceQueue()) {
        SchedulePoll();
    }
}

// The gate just answered for the Swap Weapons action.
void OnSwapActionGate(bool allowed, std::uint32_t localContext) noexcept {
    QueuedSwap&         q   = g.queue;
    const std::uint64_t now = GetTickCount64();

    if (allowed) {
        if (q.pending) {
            if (q.sends == 0) {
                q.firstSendAt = now;
            }
            q.lastSendAt = now;
            ++q.sends;
            Debug("swap request sent by %s (request %u, %llu ms after the press).",
                g.firing ? "the queue" : "a key press", q.sends,
                static_cast<unsigned long long>(now - q.queuedAt));
            return;
        }

        // A stock swap starts right now. Follow it until the server confirms
        // it, so a held attack button cannot start the next attack in between
        // and a refusal by the server is sent again.
        void*        player    = LocalPlayer(localContext);
        std::uint8_t weaponSet = 0;
        if (player == nullptr || !ReadWeaponSet(weaponSet)) {
            return;
        }
        q             = QueuedSwap{};
        q.pending     = true;
        q.player      = player;
        q.weaponSet   = weaponSet;
        q.queuedAt    = now;
        q.firstSendAt = now;
        q.lastSendAt  = now;
        q.sends       = 1;
        Debug("swap request sent by a key press while idle; waiting for the server (weapon set %u).",
            static_cast<unsigned>(weaponSet));
        SchedulePoll();
        return;
    }

    if (g.firing) {
        return; // still busy, the queue keeps waiting
    }
    if (q.pending) {
        Debug("press ignored: a swap is already pending.");
        return;
    }

    void* player = LocalPlayer(localContext);
    if (player == nullptr) {
        return;
    }
    const std::int32_t mode = g.getUnitMode(player);
    if (!IsBusyMode(mode)) {
        Debug("press refused by the stock gate in mode %s; not queued.", ModeName(mode));
        return;
    }

    std::uint8_t weaponSet = 0;
    if (!ReadWeaponSet(weaponSet)) {
        Debug("press not queued: the weapon set could not be read.");
        return;
    }

    q           = QueuedSwap{};
    q.pending   = true;
    q.player    = player;
    q.weaponSet = weaponSet;
    q.queuedAt  = now;
    Debug("swap queued: mode %s, weapon set %u.", ModeName(mode), static_cast<unsigned>(weaponSet));
    SchedulePoll();
}

std::uint8_t HookSwapGate(void* inputManager, std::uint32_t localContext) noexcept {
    const std::uintptr_t caller = WSQ_CALLER();
    if (caller != g.exeBase + kSwapGateReturnRva) {
        if (g.debugLog) {
            for (std::uintptr_t& slot : g.loggedCallers) {
                if (slot == caller) {
                    break;
                }
                if (slot == 0) {
                    slot = caller;
                    Debug("swap gate also called from %s 0x%llX (left stock).",
                        caller >= g.exeBase ? "RVA" : "address",
                        static_cast<unsigned long long>(caller >= g.exeBase ? caller - g.exeBase : caller));
                    break;
                }
            }
        }
        return g.originalGate(inputManager, localContext);
    }

    const std::uint8_t allowed = g.originalGate(inputManager, localContext);
    OnSwapActionGate(allowed != 0, localContext);
    return allowed;
}

// Every client frame a held left or right mouse button re-issues its skill
// through this dispatcher, even while the current attack is still playing.
// While a swap is pending that repeat is held back, so the current animation
// ends with the character idle and the swap goes out (from here or from the
// poll). Once the swap is confirmed or dropped, the repeat continues.
std::int64_t HookMouseAction(void* player, std::int32_t kind, std::uint32_t x, std::uint32_t y,
    std::uint16_t flags) noexcept {
    const std::uintptr_t caller = WSQ_CALLER();
    const bool held = caller == g.exeBase + kHeldLeftReturnRva || caller == g.exeBase + kHeldRightReturnRva;
    if (held && g.queue.pending && ServiceQueue()) {
        if (!g.queue.heldLogged) {
            g.queue.heldLogged = true;
            Debug("held %s button paused until the swap is done.", kind == 4 ? "right" : "left");
        }
        return 0;
    }
    return g.originalMouseAction(player, kind, x, y, flags);
}

// ---------------------------------------------------------------------------
// Config
// ---------------------------------------------------------------------------

struct Settings {
    bool enabled  = true;
    bool debugLog = true;
};

auto Trim(std::string_view text) noexcept -> std::string_view {
    while (!text.empty() && (text.front() == ' ' || text.front() == '\t' || text.front() == '\r')) {
        text.remove_prefix(1);
    }
    while (!text.empty() && (text.back() == ' ' || text.back() == '\t' || text.back() == '\r')) {
        text.remove_suffix(1);
    }
    return text;
}

void ParseSettings(std::string_view text, Settings& settings) noexcept {
    while (!text.empty()) {
        const std::size_t end  = text.find('\n');
        std::string_view  line = text.substr(0, end);
        text                   = end == std::string_view::npos ? std::string_view{} : text.substr(end + 1);

        const std::size_t comment = line.find('#');
        if (comment != std::string_view::npos) {
            line = line.substr(0, comment);
        }
        line = Trim(line);
        if (line.empty() || line.front() == '[') {
            continue;
        }
        const std::size_t equals = line.find('=');
        if (equals == std::string_view::npos) {
            continue;
        }
        const std::string_view key    = Trim(line.substr(0, equals));
        const std::string_view value  = Trim(line.substr(equals + 1));
        bool*                  target = nullptr;
        if (key == "enabled") {
            target = &settings.enabled;
        } else if (key == "debug_log") {
            target = &settings.debugLog;
        }
        if (target == nullptr) {
            continue;
        }
        if (value == "true") {
            *target = true;
        } else if (value == "false") {
            *target = false;
        } else {
            Error("config value for '%.*s' must be true or false; keeping %s.", static_cast<int>(key.size()),
                key.data(), *target ? "true" : "false");
        }
    }
}

auto LoadSettings(const D2RL::PluginContext* context) noexcept -> Settings {
    Settings settings;
    if (!context->EnsureConfig(kDefaultToml)) {
        Error("could not create the default config; using defaults.");
        return settings;
    }
    std::string   text(8192, '\0');
    std::uint32_t required = 0;
    if (!context->ReadConfig(text.data(), static_cast<std::uint32_t>(text.size()), &required)) {
        if (required <= text.size()) {
            Error("could not read the config; using defaults.");
            return settings;
        }
        text.assign(required, '\0');
        if (!context->ReadConfig(text.data(), static_cast<std::uint32_t>(text.size()), &required)) {
            Error("could not read the config; using defaults.");
            return settings;
        }
    }
    text.resize(std::strlen(text.c_str()));
    ParseSettings(text, settings);
    return settings;
}

// ---------------------------------------------------------------------------
// Load
// ---------------------------------------------------------------------------

auto VerifyBytes(const D2RL::PluginContext* context) noexcept -> bool {
    struct Range {
        std::uint64_t       rva;
        const std::uint8_t* bytes;
        std::uint32_t       size;
        const char*         name;
    };
    const Range ranges[] = {
        {kSwapActionRva, kSwapActionBytes, sizeof(kSwapActionBytes), "Swap Weapons action"},
        {kSwapGateRva, kSwapGateBytes, sizeof(kSwapGateBytes), "swap gate"},
        {kSwapGateModeTableRva, kSwapGateModeTable, sizeof(kSwapGateModeTable), "swap gate mode table"},
        {kReadUiModelBoolRva, kReadUiModelBoolBytes, sizeof(kReadUiModelBoolBytes), "weapon-set reader"},
        {kMouseActionRva, kMouseActionBytes, sizeof(kMouseActionBytes), "mouse action dispatcher"},
        {kHeldButtonsRva, kHeldButtonsBytes, sizeof(kHeldButtonsBytes), "held mouse button block"},
    };
    for (const Range& range : ranges) {
        if (!context->CheckExpectedBytes(range.rva, range.bytes, range.size)) {
            Error("%s at RVA 0x%llX does not match D2R 3.3.93847 (other game build, or another plugin "
                  "changed it). Nothing installed.",
                range.name, static_cast<unsigned long long>(range.rva));
            return false;
        }
    }
    return true;
}

template <typename Fn>
auto At(std::uint64_t rva) noexcept -> Fn {
    return reinterpret_cast<Fn>(g.exeBase + static_cast<std::uintptr_t>(rva));
}

constexpr D2RL::PluginInfo kPluginInfo{
    .infoSize    = D2RL::PluginInfoSize,
    .abiVersion  = D2RL_PLUGIN_ABI_VERSION,
    .id          = "celestialrayone.weapon-swap-queue",
    .name        = "Weapon Swap Queue",
    .version     = "1.1.0",
    .author      = "CelestialRayOne",
    .description = "Queues one Swap Weapons press made while busy and swaps as soon as the animation ends.",
    .flags       = D2RL::PluginFlags::Client | D2RL::PluginFlags::NativeHooks,
};

} // namespace

D2RL_PLUGIN_EXPORT auto D2RLoaderGetPluginInfo() noexcept -> const D2RL::PluginInfo* {
    return &kPluginInfo;
}

D2RL_PLUGIN_EXPORT auto D2RLoaderLoadPlugin(const D2RL::PluginContext* context) noexcept -> bool {
    if (context == nullptr || context->GetApi() == nullptr) {
        return false;
    }
    g.ctx     = context;
    g.exeBase = context->exeBase;

    const Settings settings = LoadSettings(context);
    g.debugLog              = settings.debugLog;
    if (!settings.enabled) {
        Info("disabled in the config; nothing installed.");
        return true;
    }

    if (!VerifyBytes(context)) {
        return false;
    }

    if (context->QueryService(&g.threads) != D2RL::ServiceQueryResult::Success
        || !D2RL::HasThreadServiceField(g.threads, D2RL::ThreadServiceRequiredSize)
        || g.threads->runOnUiThread == nullptr) {
        Error("the thread service is unavailable; nothing installed.");
        return false;
    }

    g.swapAction      = At<SwapActionFn>(kSwapActionRva);
    g.getInputManager = At<GetPointerFn>(kGetInputManagerRva);
    g.getLocalContext = At<GetContextFn>(kGetLocalContextRva);
    g.getLocalPlayer  = At<GetLocalPlayerFn>(kGetLocalPlayerRva);
    g.getUnitMode     = At<GetUnitModeFn>(kGetUnitModeRva);
    g.getUiModel      = At<GetPointerFn>(kGetUiModelRva);
    g.readUiModelBool = At<ReadUiModelBoolFn>(kReadUiModelBoolRva);

    if (!context->InstallInlineHook(kSwapGateRva, kSwapGateBytes, kSwapGateHookBytes, HookSwapGate, &g.originalGate)
        || g.originalGate == nullptr) {
        Error("could not hook the swap gate at RVA 0x%llX (already owned or unavailable).",
            static_cast<unsigned long long>(kSwapGateRva));
        return false;
    }
    if (!context->InstallInlineHook(kMouseActionRva, kMouseActionBytes, kMouseActionHookBytes, HookMouseAction,
            &g.originalMouseAction)
        || g.originalMouseAction == nullptr) {
        Error("could not hook the mouse action dispatcher at RVA 0x%llX (already owned or unavailable).",
            static_cast<unsigned long long>(kMouseActionRva));
        return false;
    }

    Info("loaded. Swap Weapons presses made while busy are queued once%s.",
        g.debugLog ? " (debug_log on)" : "");
    return true;
}

D2RL_PLUGIN_EXPORT void D2RLoaderUnloadPlugin() noexcept {
    Clear();
    g.pollScheduled.store(false);
}
