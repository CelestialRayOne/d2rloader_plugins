// Rerolling Orb Storing
//
// A hotkey that feeds matching rings, amulets, jewels, charms and quivers into
// a Rerolling Orb through the Horadric Cube, one transmute per item. 3.3 port
// of the ESR-utils 2.4 rerolling orb pass. On a controller, an Autostore
// button on the inventory panel starts the same run. Nothing is clicked: the
// plugin sends the game's own item and transmute requests, and the server
// checks every one of them.
//
// Everything below was read out of the D2RLoader 1.3.1 process image (D2R
// 3.3, base 0x140000000) and every function used is checked byte for byte at
// load. Nothing is installed when a check fails. The plugin installs no hooks
// and writes no game memory.
//
// ---------------------------------------------------------------------------
// 1. Requests used (client builders, server handlers from the table 1D2A790)
// ---------------------------------------------------------------------------
//   0x19 pick up      Send17(0x19, item, x, y, page)     server 4AA100
//                     a stored item of the player onto the cursor
//   0x18 put down     Send17(0x18, item, x, y, page)     server 4BFF30
//                     the cursor item into a player page at an exact cell
//   0x46 shared take  Send13(0x46, item, owner, xy)      server 4C6480
//   0x45 shared put   Send13(0x45, item, owner, xy)      server 4C5570
//                     the same two for a shared stash tab; owner is the tab's
//                     auxiliary player (state 186, owned by the local player)
//   0x20 transmute    ED120(cube, cubePage, cubeXY, records, end)
//                     server 4C1590 -> 4F2490
//   Send17 = 0EC7D0 (17 bytes, opcode + 4 dwords), Send13 = 0EC780 (13 bytes,
//   opcode + 3 dwords). The native callers are 2C7080 (pick up: 0x19, or 0x46
//   with the selected tab's owner) and 15F8B0 (put down: 0x18, or 0x45).
//   Here the owner is always the owner the item actually lives in, so no stash
//   tab has to be selected.
//
// ---------------------------------------------------------------------------
// 2. Why one item at a time
// ---------------------------------------------------------------------------
//   The server transmute (4F2490) refuses unless the cursor is empty and the
//   request lists exactly the items in the cube, each at its cell, sorted by
//   row then column (strictly increasing x | y << 16). It then does not
//   transmute on the spot: it returns 4 and 4C1590 queues the work on a game
//   job (vtable 1D24730, run 4ADAD0) that re-runs the same check later. Moving
//   item 2 into the cube before item 1's job ran would break both checks, so
//   each item waits until the client sees it consumed. Per item the plugin
//   sends pick up + put down + transmute together; the transmute lists the
//   orb and the item at the cell the put down used.
//   Record format read from ED120: qword = guid | x << 32 | y << 48, sent as
//   guid + one byte (x & 0xF) | y << 4.
//
// ---------------------------------------------------------------------------
// 3. Finding items
// ---------------------------------------------------------------------------
//   Player items: 34A360 (inventory) -> 388C10 / 38ABA0, page from 36CFE0
//   (0 inventory, 3 cube, 4 personal stash). Shared stash tabs: the player's
//   inventory keeps a list of auxiliary owners (388E00, next 38CD70, active
//   byte 38CD50, owner id 2EF880, unit from 09A5D0 with type 0); 15F580 says
//   whether one is a shared stash tab owner of the local player, and 46DA50
//   names the Advanced Stash owner, which is left out. Their items sit on
//   page 4.
//   Quality 36CF60 (ItemData +0), base code 36EF50 ("box " is the cube), type
//   check 373890 (item, itemtypes row) with the game's equivalence table, so
//   child types match. Rows come from the SDK data-table service by code.
//   Free cube cell: 3865B0 (inventory, item, grid 34A410(player, 3,
//   08D3B0() == 0), &x, &y, 3), as 15F8B0 does. Cell of an item: 34A110,
//   x | y << 16. Cursor item: 388A70.
//
// ---------------------------------------------------------------------------
// 4. Threads
// ---------------------------------------------------------------------------
//   The hotkey is caught on the game window (a window procedure subclass, as
//   ESR-utils did in 2.4) and only queues work. Item type rows are looked up on
//   the game thread (the data-table service requires it), then the run itself
//   steps once per UI update on the UI thread, where the client units and the
//   request builders live. The controller button's click (section 5) queues
//   the same work as the hotkey.
//
// ---------------------------------------------------------------------------
// 5. The controller button
// ---------------------------------------------------------------------------
//   An Autostore button in the controller inventory. The button itself is
//   part of the mod's controller inventory layout (autostore_button), so its
//   place, size and d-pad links are set there. The plugin hands D2RCore the
//   button's art as plugin resources: the Rerolling Orb (ESR's reroll1
//   inventory art) in the frame of the Transfer to Stash controller button,
//   4 frames of 82x82 (41x41 lowend), frame 0 normal, 1 pressed, 2 disabled,
//   3 hovered.
//
//   A press. A on the focused button sends its click message; the plugin's
//   UI message listener consumes it and starts the same run as the hotkey.
//   When a service is missing or a registration fails, the hotkey still
//   works.

#include <D2RLPlugin/api.h>

#include "resource_ids.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <knownfolders.h>
#include <shlobj.h>

#ifdef _MSC_VER
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "uuid.lib")
#endif

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
#include <vector>

namespace {

constexpr char PluginIdText[]  = "celestialrayone.reroll-orb-storing";
constexpr char PluginVersion[] = "1.5.0";
constexpr char SourceFile[]    = "reroll-orb-storing";

const D2RL::PluginContext* Context{};
std::uintptr_t             Base{};

// ---------------------------------------------------------------------------
// Addresses (RVAs into the D2RLoader 1.3.1 image)
// ---------------------------------------------------------------------------

constexpr std::uint64_t LocalDataContextRva   = 0x08B2D0;
constexpr std::uint64_t ExpansionFlagRva      = 0x08D3B0;
constexpr std::uint64_t LocalPlayerRva        = 0x09A480;
constexpr std::uint64_t UnitByIdRva           = 0x09A5D0;
constexpr std::uint64_t UiStateOpenRva        = 0x0CE500;
constexpr std::uint64_t Send13Rva             = 0x0EC780;
constexpr std::uint64_t Send17Rva             = 0x0EC7D0;
constexpr std::uint64_t TransmuteRva          = 0x0ED120;
constexpr std::uint64_t TransmuteBodyRva      = 0x0ED139;   // checked from here: engine-stability hooks the entry
constexpr std::uint64_t IsSharedStashOwnerRva = 0x15F580;
constexpr std::uint64_t StashOwnerIdRva       = 0x2EF880;
constexpr std::uint64_t PackedPositionRva     = 0x34A110;
constexpr std::uint64_t DataContextRva        = 0x34A0E0;
constexpr std::uint64_t UnitIdRva             = 0x34A330;
constexpr std::uint64_t UnitInventoryRva      = 0x34A360;
constexpr std::uint64_t InventoryGridRva      = 0x34A410;
constexpr std::uint64_t UnitTypeRva           = 0x34B9D0;
constexpr std::uint64_t ItemQualityRva        = 0x36CF60;
constexpr std::uint64_t ItemPageRva           = 0x36CFE0;
constexpr std::uint64_t ItemCodeRva           = 0x36EF50;
constexpr std::uint64_t CheckItemTypeRva      = 0x373890;
constexpr std::uint64_t FindFreePositionRva   = 0x3865B0;
constexpr std::uint64_t CursorItemRva         = 0x388A70;
constexpr std::uint64_t FirstItemRva          = 0x388C10;
constexpr std::uint64_t FirstStashOwnerRva    = 0x388E00;
constexpr std::uint64_t NextItemRva           = 0x38ABA0;
constexpr std::uint64_t StashOwnerActiveRva   = 0x38CD50;
constexpr std::uint64_t NextStashOwnerRva     = 0x38CD70;
constexpr std::uint64_t AdvancedStashOwnerRva = 0x46DA50;

// Game values
constexpr std::int32_t  StashUiState     = 0x18;
constexpr std::int32_t  PlayerUnitType   = 0;
constexpr std::int32_t  ItemUnitType     = 4;
constexpr std::uint8_t  InventoryPage    = 0;
constexpr std::uint8_t  CubePage         = 3;
constexpr std::uint8_t  StashPage        = 4;
constexpr std::uint8_t  OpPickUp         = 0x19;
constexpr std::uint8_t  OpPutDown        = 0x18;
constexpr std::uint8_t  OpSharedTake     = 0x46;
constexpr std::uint8_t  OpSharedPut      = 0x45;
constexpr std::uint32_t CubeCode         = 0x20786F62;   // "box "
constexpr std::uint32_t QualityMagic     = 4;
constexpr std::uint32_t QualitySet       = 5;
constexpr std::uint32_t QualityRare      = 6;
constexpr std::uint32_t QualityUnique    = 7;
constexpr std::size_t   MaxItemsWalked   = 65536;   // safety stops only; real lists end long before
constexpr std::size_t   MaxOwnersWalked  = 4096;
constexpr std::int32_t  ChatUiState      = 5;       // ChatPanel (0C7AA0): the chat input is open
constexpr std::uint64_t StepTimeoutMs    = 3000;

// ---------------------------------------------------------------------------
// The controller button (section 5)
// ---------------------------------------------------------------------------

constexpr char ButtonSpritePath[] = "data/hd/global/ui/d2rloader/celestialrayone.reroll-orb-storing/"
                                    "autostorebuttoncontroller.sprite";
constexpr char ButtonLowendPath[] = "data/hd/global/ui/d2rloader/celestialrayone.reroll-orb-storing/"
                                    "autostorebuttoncontroller.lowend.sprite";

constexpr std::string_view ClickTarget  = "PanelManager";
constexpr std::string_view ClickCommand = "OpenPanel";
constexpr std::string_view ClickText    = "CelestialRayOneRerollOrbStoringAutostore";


// ---------------------------------------------------------------------------
// Byte witnesses
// ---------------------------------------------------------------------------

constexpr std::uint8_t DataContextBytes[]{ 0x48,0x83,0xEC,0x28,0x48,0x85,0xC9,0x75,0x1A,0x88,0x4C,0x24,0x30,0x48,0x8D,0x4C,0x24,0x30,0xE8,0x49,0xC7,0xFF,0xFF,0x84,0xC0,0x74,0x01,0xCC,0x32,0xC0,0x48,0x83,0xC4,0x28,0xC3,0x0F,0xB6,0x81,0xBD,0x01,0x00,0x00,0x48,0x83,0xC4,0x28,0xC3 };  // 34A0E0, 47 bytes: the unit's data bank, byte +1BDh
constexpr std::uint8_t LocalDataContextBytes[]{ 0x8B,0x05,0x2E,0x84,0x99,0x02,0xC3 };  // 8B2D0, 7 bytes
constexpr std::uint8_t LocalPlayerBytes[]{ 0x48,0x89,0x5C,0x24,0x08,0x57,0x48,0x83,0xEC,0x20,0x83,0xF9,0x08,0x0F,0x83,0x85,0x00,0x00,0x00,0x8B,0xD9,0x48,0x89,0x5C,0x24,0x38,0x48,0x83,0xFB,0x08,0x72,0x19 };  // 9A480, 32 bytes
constexpr std::uint8_t UnitByIdBytes[]{ 0x4C,0x63,0xCA,0x48,0x8D,0x05,0x36,0x93,0x98,0x02,0x8B,0xD1,0x44,0x8B,0xC1,0x49,0x8B,0xC9,0x83,0xE2,0x7F,0x48,0xC1,0xE1,0x0A,0x48,0x03,0xC8,0xE9,0x7F,0x4C,0x00 };  // 9A5D0, 32 bytes
constexpr std::uint8_t UiStateOpenBytes[]{ 0x48,0x63,0xC1,0x48,0x8D,0x0D,0x96,0xC8,0x95,0x02,0x0F,0xB6,0x04,0x08,0xC3 };  // CE500, 15 bytes
constexpr std::uint8_t ExpansionFlagBytes[]{ 0x48,0x8B,0x05,0x29,0x63,0x99,0x02,0x48,0x85,0xC0,0x74,0x08,0x80,0x78,0x5C,0x01 };  // 8D3B0, 16 bytes
constexpr std::uint8_t IsSharedStashOwnerBytes[]{ 0x40,0x53,0x48,0x83,0xEC,0x20,0x48,0x8B,0xD9,0x48,0x85,0xC9,0x0F,0x84,0x88,0x00,0x00,0x00,0xE8,0x39,0xBD,0xF2,0xFF,0x8B,0xC8,0xE8,0xE2,0xAE,0xF3,0xFF,0x48,0x3B };  // 15F580, 32 bytes
constexpr std::uint8_t UnitInventoryBytes[]{ 0x48,0x89,0x5C,0x24,0x18,0x56,0x48,0x83,0xEC,0x20,0x48,0x8B,0xF1,0x48,0x85,0xC9,0x75,0x13,0x88,0x4C,0x24,0x30,0x48,0x8D,0x4C,0x24,0x30,0xE8,0x70,0xCC,0xFF,0xFF };  // 34A360, 32 bytes
constexpr std::uint8_t UnitTypeBytes[]{ 0x48,0x83,0xEC,0x28,0x48,0x85,0xC9,0x75,0x1D,0x88,0x4C,0x24,0x30,0x48,0x8D,0x4C,0x24,0x30,0xE8,0x39,0x9E,0xFF,0xFF,0x84,0xC0,0x74,0x01,0xCC,0xB8,0x06,0x00,0x00 };  // 34B9D0, 32 bytes
constexpr std::uint8_t UnitIdBytes[]{ 0x48,0x83,0xEC,0x28,0x48,0x85,0xC9,0x75,0x1D,0x88,0x4C,0x24,0x30,0x48,0x8D,0x4C };  // 34A330, 16 bytes
constexpr std::uint8_t PackedPositionBytes[]{ 0x40,0x53,0x48,0x83,0xEC,0x20,0x48,0x8B,0xD9,0x48,0x85,0xC9,0x75,0x13,0x88,0x4C,0x24,0x30,0x48,0x8D,0x4C,0x24,0x30,0xE8,0x04,0xC5,0xFF,0xFF,0x84,0xC0,0x74,0x01 };  // 34A110, 32 bytes
constexpr std::uint8_t InventoryGridBytes[]{ 0x48,0x89,0x5C,0x24,0x10,0x48,0x89,0x6C,0x24,0x18,0x48,0x89,0x74,0x24,0x20,0x57,0x48,0x83,0xEC,0x20,0x0F,0xB6,0xEA,0x41,0x8B,0xF0,0x48,0x8B,0xF9,0xBB,0xFF,0xFF };  // 34A410, 32 bytes
constexpr std::uint8_t ItemQualityBytes[]{ 0x40,0x53,0x48,0x83,0xEC,0x20,0x48,0x8B,0xD9,0x48,0x85,0xC9,0x74,0x0A,0xE8,0x5D,0xEA,0xFD,0xFF,0x83,0xF8,0x04,0x74,0x19,0x48,0x8D,0x4C,0x24,0x30,0xC6,0x44,0x24 };  // 36CF60, 32 bytes
constexpr std::uint8_t ItemPageBytes[]{ 0x40,0x53,0x48,0x83,0xEC,0x20,0x48,0x8B,0xD9,0x48,0x85,0xC9,0x74,0x0A,0xE8,0xDD,0xE9,0xFD,0xFF,0x83,0xF8,0x04,0x74,0x19,0x48,0x8D,0x4C,0x24,0x30,0xC6,0x44,0x24 };  // 36CFE0, 32 bytes
constexpr std::uint8_t ItemCodeBytes[]{ 0x48,0x89,0x5C,0x24,0x10,0x57,0x48,0x83,0xEC,0x20,0x48,0x8B,0xF9,0x48,0x85,0xC9,0x75,0x13,0x88,0x4C,0x24,0x30,0x48,0x8D,0x4C,0x24,0x30,0xE8,0x80,0x83,0xFF,0xFF };  // 36EF50, 32 bytes
constexpr std::uint8_t CheckItemTypeBytes[]{ 0x48,0x89,0x5C,0x24,0x10,0x48,0x89,0x6C,0x24,0x18,0x48,0x89,0x74,0x24,0x20,0x57,0x41,0x56,0x41,0x57,0x48,0x83,0xEC,0x20,0x48,0x63,0xF2,0x48,0x8B,0xF9,0x48,0x85 };  // 373890, 32 bytes
constexpr std::uint8_t FindFreePositionBytes[]{ 0x40,0x55,0x53,0x56,0x57,0x41,0x54,0x41,0x56,0x41,0x57,0x48,0x8B,0xEC,0x48,0x83,0xEC,0x60,0x48,0x8B,0x05,0xFF,0x4C,0x64,0x02,0x48,0x33,0xC4,0x48,0x89,0x45,0xF0 };  // 3865B0, 32 bytes
constexpr std::uint8_t CursorItemBytes[]{ 0x40,0x53,0x48,0x83,0xEC,0x20,0x48,0x8B,0xD9,0x48,0x85,0xC9,0x75,0x1B,0x88,0x4C,0x24,0x30,0x48,0x8D,0x4C,0x24,0x30,0xE8,0xD4,0xB4,0xFF,0xFF,0x84,0xC0,0x74,0x2D };  // 388A70, 32 bytes
constexpr std::uint8_t FirstItemBytes[]{ 0x40,0x53,0x48,0x83,0xEC,0x20,0x48,0x8B,0xD9,0x48,0x85,0xC9,0x74,0x2E,0x81,0x39,0x04,0x03,0x02,0x01,0x74,0x1C,0x48,0x8D,0x4C,0x24,0x30,0xC6,0x44,0x24,0x30,0x00 };  // 388C10, 32 bytes
constexpr std::uint8_t NextItemBytes[]{ 0x40,0x53,0x48,0x83,0xEC,0x20,0x48,0x8B,0xD9,0x48,0x85,0xC9,0x75,0x10,0x88,0x4C,0x24,0x30,0x48,0x8D,0x4C,0x24,0x30,0xE8,0x84,0x98,0xFF,0xFF,0xEB,0x67,0xE8,0x0D };  // 38ABA0, 32 bytes
constexpr std::uint8_t FirstStashOwnerBytes[]{ 0x48,0x8B,0x41,0x68,0xC3 };  // 388E00, 5 bytes
constexpr std::uint8_t StashOwnerActiveBytes[]{ 0x0F,0xB6,0x41,0x09,0xC3 };  // 38CD50, 5 bytes
constexpr std::uint8_t NextStashOwnerBytes[]{ 0x48,0x8B,0x41,0x10,0xC3 };  // 38CD70, 5 bytes
constexpr std::uint8_t StashOwnerIdBytes[]{ 0x8B,0x01,0xC3 };  // 2EF880, 3 bytes
constexpr std::uint8_t AdvancedStashOwnerBytes[]{ 0x40,0x53,0x48,0x83,0xEC,0x20,0x48,0x85,0xC9,0x75,0x1B,0x88,0x4C,0x24,0x30,0x48,0x8D,0x4C,0x24,0x30,0xE8,0xC7,0xF3,0xFF,0xFF,0x84,0xC0,0x74,0x01,0xCC,0x33,0xC0 };  // 46DA50, 32 bytes
constexpr std::uint8_t Send13Bytes[]{ 0x48,0x83,0xEC,0x48,0x48,0x8B,0x05,0x3D,0xEB,0x8D,0x02,0x48,0x33,0xC4,0x48,0x89,0x44,0x24,0x30,0x88,0x4C,0x24,0x20,0x48,0x8D,0x4C,0x24,0x20,0x89,0x54,0x24,0x21,0xBA,0x0D,0x00,0x00,0x00,0x44,0x89,0x44,0x24,0x25,0x44,0x89,0x4C,0x24,0x29,0xE8,0x88,0xEF,0xD3,0x03,0x48,0x8B,0x4C,0x24,0x30,0x48,0x33,0xCC,0xE8,0x8F,0x49,0x1E,0x01,0x48,0x83,0xC4,0x48,0xC3 };  // EC780, 70 bytes
constexpr std::uint8_t Send17Bytes[]{ 0x48,0x83,0xEC,0x48,0x48,0x8B,0x05,0xED,0xEA,0x8D,0x02,0x48,0x33,0xC4,0x48,0x89,0x44,0x24,0x38,0x8B,0x44,0x24,0x70,0x88,0x4C,0x24,0x20,0x48,0x8D,0x4C,0x24,0x20,0x89,0x54,0x24,0x21,0xBA,0x11,0x00,0x00,0x00,0x44,0x89,0x44,0x24,0x25,0x44,0x89,0x4C,0x24,0x29,0x89,0x44,0x24,0x2D,0xE8,0x30,0xEF,0xD3,0x03 };  // EC7D0, 60 bytes
constexpr std::uint8_t TransmuteBodyBytes[]{ 0x4C,0x8B,0x94,0x24,0x70,0x05,0x00,0x00,0x41,0x8B,0xC0,0xC1,0xE8,0x10,0x41,0x80,0xE0,0x0F,0xC0,0xE0,0x04,0x41,0x02,0xC0,0xC6,0x44,0x24,0x20,0x20,0x88,0x44,0x24,0x26,0x4C,0x8D,0x44,0x24,0x28,0x49,0x8B,0xC2,0x89,0x4C,0x24,0x21,0x49,0x2B,0xC1,0x88,0x54,0x24,0x25,0x48,0xC1,0xF8,0x03,0xFE,0xC8,0x88,0x44,0x24,0x27,0x4D,0x3B,0xCA,0x74,0x36,0x0F,0x1F,0x40,0x00,0x49,0x8B,0x01,0x49,0x83,0xC1,0x08,0x41,0x89,0x00,0x4D,0x8D,0x40,0x05,0x48,0x8B,0xD0,0x48,0x8B,0xC8,0x48,0xC1,0xEA,0x30,0x48,0xC1,0xE9,0x20,0xC0,0xE2,0x04,0x80,0xE1,0x0F,0x02,0xD1,0x41,0x88,0x50,0xFF,0x4D,0x3B,0xCA,0x75,0xD3,0x0F,0xB6,0x44,0x24,0x27,0x0F,0xB6,0xC0,0x48,0x8D,0x4C,0x24,0x20,0x8D,0x14,0x85,0x0D,0x00,0x00,0x00,0x03,0xD0 };  // ED139, 138 bytes

struct Witness {
    std::uint64_t       rva;
    const std::uint8_t* bytes;
    std::uint32_t       size;
    const char*         name;
};

template <std::size_t Size>
constexpr auto W(std::uint64_t rva, const std::uint8_t (&bytes)[Size], const char* name) noexcept -> Witness {
    return Witness{ rva, bytes, static_cast<std::uint32_t>(Size), name };
}

const std::array<Witness, 28> Witnesses{
    W(LocalDataContextRva, LocalDataContextBytes, "local data context"),
    W(LocalPlayerRva, LocalPlayerBytes, "local player"),
    W(UnitByIdRva, UnitByIdBytes, "client unit by id"),
    W(UiStateOpenRva, UiStateOpenBytes, "UI state open"),
    W(ExpansionFlagRva, ExpansionFlagBytes, "expansion flag"),
    W(IsSharedStashOwnerRva, IsSharedStashOwnerBytes, "shared stash owner check"),
    W(UnitInventoryRva, UnitInventoryBytes, "unit inventory"),
    W(UnitTypeRva, UnitTypeBytes, "unit type"),
    W(UnitIdRva, UnitIdBytes, "unit id"),
    W(DataContextRva, DataContextBytes, "unit data bank"),
    W(PackedPositionRva, PackedPositionBytes, "item cell"),
    W(InventoryGridRva, InventoryGridBytes, "inventory grid"),
    W(ItemQualityRva, ItemQualityBytes, "item quality"),
    W(ItemPageRva, ItemPageBytes, "item page"),
    W(ItemCodeRva, ItemCodeBytes, "item code"),
    W(CheckItemTypeRva, CheckItemTypeBytes, "item type check"),
    W(FindFreePositionRva, FindFreePositionBytes, "free cell search"),
    W(CursorItemRva, CursorItemBytes, "cursor item"),
    W(FirstItemRva, FirstItemBytes, "first inventory item"),
    W(NextItemRva, NextItemBytes, "next inventory item"),
    W(FirstStashOwnerRva, FirstStashOwnerBytes, "first stash owner record"),
    W(StashOwnerActiveRva, StashOwnerActiveBytes, "stash owner record active"),
    W(NextStashOwnerRva, NextStashOwnerBytes, "next stash owner record"),
    W(StashOwnerIdRva, StashOwnerIdBytes, "stash owner record id"),
    W(AdvancedStashOwnerRva, AdvancedStashOwnerBytes, "Advanced Stash owner"),
    W(Send13Rva, Send13Bytes, "13 byte request"),
    W(Send17Rva, Send17Bytes, "17 byte request"),
    W(TransmuteBodyRva, TransmuteBodyBytes, "transmute request"),
};

// ---------------------------------------------------------------------------
// Native functions
// ---------------------------------------------------------------------------

using LocalDataContextFn   = std::uint32_t(__fastcall*)();
using ExpansionFlagFn      = std::uint8_t(__fastcall*)();
using LocalPlayerFn        = void*(__fastcall*)(std::uint32_t index);
using UnitByIdFn           = void*(__fastcall*)(std::uint32_t id, std::int32_t type);
using UiStateOpenFn        = std::uint8_t(__fastcall*)(std::int32_t state);
using Send13Fn             = void(__fastcall*)(std::uint8_t op, std::uint32_t a, std::uint32_t b, std::uint32_t c);
using Send17Fn             = void(__fastcall*)(std::uint8_t op, std::uint32_t a, std::uint32_t b, std::uint32_t c,
                                               std::uint32_t d);
using TransmuteFn          = void(__fastcall*)(std::uint32_t cube, std::uint8_t cubePage, std::uint32_t cubeCell,
                                               const std::uint64_t* begin, const std::uint64_t* end);
using IsSharedStashOwnerFn = bool(__fastcall*)(void* unit);
using StashOwnerIdFn       = std::uint32_t(__fastcall*)(void* record);
using PackedPositionFn     = std::uint32_t(__fastcall*)(void* unit);
using UnitIdFn             = std::uint32_t(__fastcall*)(void* unit);
using DataContextFn        = std::uint8_t(__fastcall*)(void* unit);
using UnitInventoryFn      = void*(__fastcall*)(void* unit, const char* file, std::int32_t line);
using InventoryGridFn      = std::int32_t(__fastcall*)(void* unit, std::uint8_t page, bool flag);
using UnitTypeFn           = std::int32_t(__fastcall*)(void* unit);
using ItemQualityFn        = std::uint32_t(__fastcall*)(void* item);
using ItemPageFn           = std::uint8_t(__fastcall*)(void* item);
using ItemCodeFn           = std::uint32_t(__fastcall*)(void* item);
using CheckItemTypeFn      = std::int32_t(__fastcall*)(void* item, std::int32_t typeRow);
using FindFreePositionFn   = std::int32_t(__fastcall*)(void* inventory, void* item, std::int32_t grid,
                                                       std::uint32_t* x, std::uint32_t* y, std::uint8_t page);
using CursorItemFn         = void*(__fastcall*)(void* inventory);
using FirstItemFn          = void*(__fastcall*)(void* inventory);
using NextItemFn           = void*(__fastcall*)(void* item);
using FirstStashOwnerFn    = void*(__fastcall*)(void* inventory);
using StashOwnerActiveFn   = std::uint8_t(__fastcall*)(void* record);
using NextStashOwnerFn     = void*(__fastcall*)(void* record);
using AdvancedStashOwnerFn = void*(__fastcall*)(void* player);

LocalDataContextFn   LocalDataContext{};
ExpansionFlagFn      ExpansionFlag{};
LocalPlayerFn        LocalPlayer{};
UnitByIdFn           UnitById{};
UiStateOpenFn        UiStateOpen{};
Send13Fn             Send13{};
Send17Fn             Send17{};
TransmuteFn          SendTransmute{};
IsSharedStashOwnerFn IsSharedStashOwner{};
StashOwnerIdFn       StashOwnerId{};
PackedPositionFn     PackedPosition{};
UnitIdFn             UnitId{};
DataContextFn        DataContext{};
UnitInventoryFn      UnitInventory{};
InventoryGridFn      InventoryGrid{};
UnitTypeFn           UnitType{};
ItemQualityFn        ItemQuality{};
ItemPageFn           ItemPage{};
ItemCodeFn           ItemCode{};
CheckItemTypeFn      CheckItemType{};
FindFreePositionFn   FindFreePosition{};
CursorItemFn         CursorItem{};
FirstItemFn          FirstItem{};
NextItemFn           NextItem{};
FirstStashOwnerFn    FirstStashOwner{};
StashOwnerActiveFn   StashOwnerActive{};
NextStashOwnerFn     NextStashOwner{};
AdvancedStashOwnerFn AdvancedStashOwner{};

template <typename Function>
auto At(std::uint64_t rva) noexcept -> Function {
    return reinterpret_cast<Function>(Base + rva);
}

void BindNatives() {
    LocalDataContext   = At<LocalDataContextFn>(LocalDataContextRva);
    ExpansionFlag      = At<ExpansionFlagFn>(ExpansionFlagRva);
    LocalPlayer        = At<LocalPlayerFn>(LocalPlayerRva);
    UnitById           = At<UnitByIdFn>(UnitByIdRva);
    UiStateOpen        = At<UiStateOpenFn>(UiStateOpenRva);
    Send13             = At<Send13Fn>(Send13Rva);
    Send17             = At<Send17Fn>(Send17Rva);
    SendTransmute      = At<TransmuteFn>(TransmuteRva);
    IsSharedStashOwner = At<IsSharedStashOwnerFn>(IsSharedStashOwnerRva);
    StashOwnerId       = At<StashOwnerIdFn>(StashOwnerIdRva);
    PackedPosition     = At<PackedPositionFn>(PackedPositionRva);
    UnitId             = At<UnitIdFn>(UnitIdRva);
    DataContext        = At<DataContextFn>(DataContextRva);
    UnitInventory      = At<UnitInventoryFn>(UnitInventoryRva);
    InventoryGrid      = At<InventoryGridFn>(InventoryGridRva);
    UnitType           = At<UnitTypeFn>(UnitTypeRva);
    ItemQuality        = At<ItemQualityFn>(ItemQualityRva);
    ItemPage           = At<ItemPageFn>(ItemPageRva);
    ItemCode           = At<ItemCodeFn>(ItemCodeRva);
    CheckItemType      = At<CheckItemTypeFn>(CheckItemTypeRva);
    FindFreePosition   = At<FindFreePositionFn>(FindFreePositionRva);
    CursorItem         = At<CursorItemFn>(CursorItemRva);
    FirstItem          = At<FirstItemFn>(FirstItemRva);
    NextItem           = At<NextItemFn>(NextItemRva);
    FirstStashOwner    = At<FirstStashOwnerFn>(FirstStashOwnerRva);
    StashOwnerActive   = At<StashOwnerActiveFn>(StashOwnerActiveRva);
    NextStashOwner     = At<NextStashOwnerFn>(NextStashOwnerRva);
    AdvancedStashOwner = At<AdvancedStashOwnerFn>(AdvancedStashOwnerRva);
}

// ---------------------------------------------------------------------------
// Config
// ---------------------------------------------------------------------------

constexpr char DefaultConfigToml[] =
    "# Rerolling Orb Storing\n"
    "#\n"
    "# A hotkey that feeds rings, amulets, jewels, charms and quivers into a\n"
    "# Rerolling Orb through the Horadric Cube, one transmute per item, the same\n"
    "# way you would do it by hand. On a controller, the Autostore button on the\n"
    "# inventory starts the same run (see Controller below).\n"
    "#\n"
    "# Player settings live in ESRLauncher, not here\n"
    "#   The Rerolling Orb tab in ESRLauncher sets the hotkey and the checkboxes,\n"
    "#   and saves them per player in\n"
    "#     <Saved Games>\\Diablo II Resurrected\\mods\\<mod>\\reroll-orb-storing.json\n"
    "#   so updates never reset them. Everything applies at once, the hotkey\n"
    "#   included: a change saved in the launcher is picked up at the next key\n"
    "#   press, while the game runs. The hotkey still reaches the game too, and it\n"
    "#   does nothing while the chat input is open.\n"
    "#\n"
    "# How a run works\n"
    "#   1. Press the hotkey, or the Autostore button on a controller. The cube\n"
    "#      must be empty and nothing may be on the cursor, otherwise nothing\n"
    "#      happens (the reason goes to the plugin log).\n"
    "#   2. The plugin finds your Horadric Cube (inventory or personal stash) and a\n"
    "#      Rerolling Orb (any item of the orb_types below). Inventory is searched\n"
    "#      first, then the personal stash, then the shared stash tabs.\n"
    "#   3. The orb goes into the cube. Every matching item then goes into the cube\n"
    "#      next to it and is transmuted, one item at a time.\n"
    "#   4. The orb goes back to the exact cell and tab it came from.\n"
    "#   - No mouse movement and no clicking: the plugin sends the game's own\n"
    "#     item and transmute requests, so the cube and stash panels do not need to\n"
    "#     be open and screen size does not matter.\n"
    "#   - Stash items (personal and shared tabs) are only used while the stash is\n"
    "#     open, because the game only allows stash moves near the stash.\n"
    "#   - If an item is not consumed by the transmute (no recipe matched, for\n"
    "#     example a full orb), it goes back where it came from and the run stops.\n"
    "#   - The server checks every request, so it works in single player and as a\n"
    "#     TCP/IP host.\n"
    "#\n"
    "# Which items are stored\n"
    "#   An item is stored when its quality and category are both ticked in the\n"
    "#   launcher. Categories go by itemtypes.txt codes, child types included: a\n"
    "#   type listed here also matches every item type that has it as an\n"
    "#   Equiv1/Equiv2 parent.\n"
    "#\n"
    "# Controller\n"
    "#   An Autostore button on the controller inventory, showing the Rerolling\n"
    "#   Orb. Reach it with the d-pad and press A: it starts the same run as the\n"
    "#   hotkey, with the same launcher checkboxes.\n"
    "#   - The button is part of the mod's controller inventory layout, named\n"
    "#     autostore_button; its place, size and d-pad links are set there. This\n"
    "#     plugin gives it its art and its click:\n"
    "#       \"filename\": \"d2rloader/celestialrayone.reroll-orb-storing/autostorebuttoncontroller\"\n"
    "#       \"onClickMessage\": \"PanelManager:OpenPanel:CelestialRayOneRerollOrbStoringAutostore\"\n"
    "#   - The art is 4 frames of 82x82 (41x41 for lowend): normal, pressed,\n"
    "#     disabled, hovered. A mod can replace it with its own files under\n"
    "#       data/hd/global/ui/d2rloader/celestialrayone.reroll-orb-storing/\n"
    "#\n"
    "# Console command\n"
    "#   rerollorbstoring   status, the launcher settings it read, the resolved item\n"
    "#                      types, the controller button, and what the last run did\n"
    "#\n"
    "[reroll_orb_storing]\n"
    "\n"
    "# Master switch. false installs nothing.\n"
    "enabled = true\n"
    "\n"
    "# itemtypes.txt codes for each category and for the orb itself.\n"
    "ring_types   = [\"ring\"]\n"
    "amulet_types = [\"amul\"]\n"
    "jewel_types  = [\"jewl\"]\n"
    "charm_types  = [\"char\"]\n"
    "quiver_types = [\"bowq\", \"xboq\"]\n"
    "orb_types    = [\"can7\"]\n";

constexpr std::uint32_t MaximumConfigBytes = 64 * 1024;

enum Category : std::size_t { Rings, Amulets, Jewels, Charms, Quivers, CategoryCount };
constexpr std::array<const char*, CategoryCount> CategoryNames{ "rings", "amulets", "jewels", "charms", "quivers" };

// The mod's settings, from the plugin's TOML.
struct Settings {
    bool                                                enabled = true;
    std::array<std::vector<std::string>, CategoryCount> types{ {
        { "ring" }, { "amul" }, { "jewl" }, { "char" }, { "bowq", "xboq" } } };
    std::vector<std::string>                            orbTypes{ "can7" };
};
Settings    Config;
std::string ConfigProblems;

// The player's choices, from the file ESRLauncher's Rerolling Orb tab writes:
//   <Saved Games>\Diablo II Resurrected\mods\<mod>\reroll-orb-storing.json
// Read when the game starts (for the key) and again on every press (for the
// rest), so a change in the launcher applies at the next press.
struct PlayerChoices {
    D2RL::Input::Key                 hotkey = D2RL::Input::Key::None;
    D2RL::Input::Modifier            modifier = D2RL::Input::Modifier::None;
    bool                             skipPersonalTab = false;
    bool                             skipInventory = false;
    bool                             skipSharedStash = false;
    std::array<bool, CategoryCount>  magic{};
    std::array<bool, CategoryCount>  rare{};
    std::array<bool, CategoryCount>  setUnique{};   // jewels and charms have no Set/Unique box
    bool                             fileFound = false;
    std::string                      problem;        // why the file could not be read, or empty
};

auto Trim(std::string_view text) noexcept -> std::string_view {
    while (!text.empty() && (text.front() == ' ' || text.front() == '\t' || text.front() == '\r')) text.remove_prefix(1);
    while (!text.empty() && (text.back() == ' ' || text.back() == '\t' || text.back() == '\r')) text.remove_suffix(1);
    return text;
}

// Strips a comment, leaving any '#' inside a quoted string alone.
auto StripComment(std::string_view line) noexcept -> std::string_view {
    bool quoted = false;
    for (std::size_t i = 0; i < line.size(); ++i) {
        if (line[i] == '"') quoted = !quoted;
        else if (line[i] == '#' && !quoted) return line.substr(0, i);
    }
    return line;
}

auto Unquote(std::string_view value, std::string& out) -> bool {
    if (value.size() < 2 || value.front() != '"' || value.back() != '"') return false;
    out.assign(value.substr(1, value.size() - 2));
    return true;
}

void Problem(std::string_view key, const char* what) {
    ConfigProblems.append(key);
    ConfigProblems += ": ";
    ConfigProblems += what;
    ConfigProblems += ". ";
}

auto ParseBool(std::string_view key, std::string_view value, bool& out) -> void {
    if (value == "true") out = true;
    else if (value == "false") out = false;
    else Problem(key, "expected true or false");
}

auto ParseList(std::string_view key, std::string_view value, std::vector<std::string>& out) -> void {
    if (value.size() < 2 || value.front() != '[' || value.back() != ']') {
        Problem(key, "expected a list like [\"rin\"]");
        return;
    }
    std::vector<std::string> parsed;
    std::string_view rest = value.substr(1, value.size() - 2);
    while (!rest.empty()) {
        const std::size_t comma = rest.find(',');
        const std::string_view entry = Trim(rest.substr(0, comma));
        rest = comma == std::string_view::npos ? std::string_view{} : rest.substr(comma + 1);
        if (entry.empty()) continue;
        std::string code;
        if (!Unquote(entry, code) || code.empty() || code.size() > 4) {
            Problem(key, "every entry must be a quoted itemtypes.txt code of 1 to 4 characters");
            return;
        }
        parsed.push_back(code);
    }
    out = parsed;
}

struct KeyName {
    const char*      name;
    D2RL::Input::Key key;
};

auto KeyFromName(const std::string& name, D2RL::Input::Key& out) -> bool {
    using K = D2RL::Input::Key;
    if (name.empty() || name == "None") { out = K::None; return true; }
    if (name.size() == 1) {
        const char c = name[0];
        if (c >= 'A' && c <= 'Z') { out = static_cast<K>(0x41 + (c - 'A')); return true; }
        if (c >= 'a' && c <= 'z') { out = static_cast<K>(0x41 + (c - 'a')); return true; }
        if (c >= '0' && c <= '9') { out = static_cast<K>(0x30 + (c - '0')); return true; }
    }
    if ((name[0] == 'F' || name[0] == 'f') && name.size() <= 3) {
        int number = 0;
        bool digits = name.size() > 1;
        for (std::size_t i = 1; i < name.size(); ++i) {
            if (name[i] < '0' || name[i] > '9') digits = false;
            else number = number * 10 + (name[i] - '0');
        }
        if (digits && number >= 1 && number <= 24) { out = static_cast<K>(0x70 + number - 1); return true; }
    }
    if (name.size() == 7 && name.rfind("Numpad", 0) == 0 && name[6] >= '0' && name[6] <= '9') {
        out = static_cast<K>(0x60 + (name[6] - '0'));
        return true;
    }
    static constexpr KeyName Named[]{
        { "Space", K::Space }, { "Insert", K::Insert }, { "Delete", K::Delete }, { "Home", K::Home },
        { "End", K::End }, { "PageUp", K::PageUp }, { "PageDown", K::PageDown }, { "Backquote", K::Backquote },
        { "Minus", K::Minus }, { "Equals", K::Equals }, { "LeftBracket", K::LeftBracket },
        { "RightBracket", K::RightBracket }, { "Backslash", K::Backslash }, { "Semicolon", K::Semicolon },
        { "Quote", K::Quote }, { "Comma", K::Comma }, { "Period", K::Period }, { "Slash", K::Slash },
    };
    for (const KeyName& entry : Named) {
        if (name == entry.name) { out = entry.key; return true; }
    }
    return false;
}

auto ModifierFromName(const std::string& name, D2RL::Input::Modifier& out) -> bool {
    using M = D2RL::Input::Modifier;
    if (name.empty() || name == "None") out = M::None;
    else if (name == "Shift") out = M::Shift;
    else if (name == "Control" || name == "Ctrl") out = M::Control;
    else if (name == "Alt") out = M::Alt;
    else return false;
    return true;
}

void ParseConfig(std::string_view text) {
    std::string_view section;
    while (!text.empty()) {
        const std::size_t end = text.find('\n');
        const std::string_view line = Trim(StripComment(text.substr(0, end)));
        text = end == std::string_view::npos ? std::string_view{} : text.substr(end + 1);
        if (line.empty()) continue;
        if (line.front() == '[' && line.find('=') == std::string_view::npos) {
            if (line.back() == ']') section = Trim(line.substr(1, line.size() - 2));
            continue;
        }
        const std::size_t equals = line.find('=');
        if (equals == std::string_view::npos || section != "reroll_orb_storing") continue;
        const std::string_view key   = Trim(line.substr(0, equals));
        const std::string_view value = Trim(line.substr(equals + 1));
        if (key == "enabled") ParseBool(key, value, Config.enabled);
        else if (key == "ring_types") ParseList(key, value, Config.types[Rings]);
        else if (key == "amulet_types") ParseList(key, value, Config.types[Amulets]);
        else if (key == "jewel_types") ParseList(key, value, Config.types[Jewels]);
        else if (key == "charm_types") ParseList(key, value, Config.types[Charms]);
        else if (key == "quiver_types") ParseList(key, value, Config.types[Quivers]);
        else if (key == "orb_types") ParseList(key, value, Config.orbTypes);
        else Problem(key, "unknown setting");
    }
}

// ---------------------------------------------------------------------------
// The launcher's file: a small JSON reader for objects, strings and booleans
// (numbers, arrays and null are skipped)
// ---------------------------------------------------------------------------

struct JsonReader {
    std::string_view text;
    std::size_t      at = 0;
    bool             failed = false;

    void Space() {
        while (at < text.size() && (text[at] == ' ' || text[at] == '\t' || text[at] == '\r' || text[at] == '\n')) ++at;
    }
    auto Peek() -> char {
        Space();
        return at < text.size() ? text[at] : '\0';
    }
    auto Eat(char c) -> bool {
        if (Peek() != c) return false;
        ++at;
        return true;
    }
    auto String(std::string& out) -> bool {
        out.clear();
        if (!Eat('"')) return false;
        while (at < text.size() && text[at] != '"') {
            if (text[at] == '\\' && at + 1 < text.size()) {
                ++at;
                const char e = text[at];
                if (e == 'u') { at += 4; out += '?'; }   // no key name needs one
                else out += (e == 'n' ? '\n' : e == 't' ? '\t' : e);
                ++at;
                continue;
            }
            out += text[at++];
        }
        if (at >= text.size()) return false;
        ++at;
        return true;
    }
    // Skips any value.
    void Skip() {
        const char c = Peek();
        std::string ignored;
        if (c == '"') { if (!String(ignored)) failed = true; return; }
        if (c == '{' || c == '[') {
            const char close = c == '{' ? '}' : ']';
            ++at;
            if (Eat(close)) return;
            do {
                if (c == '{') {
                    if (!String(ignored) || !Eat(':')) { failed = true; return; }
                }
                Skip();
                if (failed) return;
            } while (Eat(','));
            if (!Eat(close)) failed = true;
            return;
        }
        while (at < text.size() && text[at] != ',' && text[at] != '}' && text[at] != ']') ++at;
    }
    auto Bool(bool& out) -> bool {
        Space();
        if (text.substr(at, 4) == "true") { at += 4; out = true; return true; }
        if (text.substr(at, 5) == "false") { at += 5; out = false; return true; }
        Skip();
        return false;
    }
    // Calls field(name) for every member of an object; field reads the value.
    template <typename Field>
    auto Object(Field&& field) -> bool {
        if (!Eat('{')) return false;
        if (Eat('}')) return true;
        do {
            std::string name;
            if (!String(name) || !Eat(':')) return false;
            field(name);
            if (failed) return false;
        } while (Eat(','));
        return Eat('}');
    }
};

auto CategoryIndex(const std::string& name, std::size_t& index) -> bool {
    for (std::size_t i = 0; i < CategoryCount; ++i) {
        if (name == CategoryNames[i]) { index = i; return true; }
    }
    return false;
}

auto ReadBoxes(JsonReader& json, std::array<bool, CategoryCount>& boxes) -> void {
    if (!json.Object([&](const std::string& name) {
            std::size_t index = 0;
            if (CategoryIndex(name, index)) json.Bool(boxes[index]);
            else json.Skip();
        })) {
        json.failed = true;
    }
}

auto PlayerFilePath() -> std::wstring {
    PWSTR saved = nullptr;
    std::wstring path;
    if (SHGetKnownFolderPath(FOLDERID_SavedGames, 0, nullptr, &saved) == S_OK && saved != nullptr) path = saved;
    if (saved != nullptr) CoTaskMemFree(saved);
    if (path.empty()) return {};
    const char* mod = Context->activeMod;
    if (mod == nullptr || *mod == '\0') return {};
    std::wstring modName;
    for (const char* c = mod; *c != '\0'; ++c) modName += static_cast<wchar_t>(static_cast<unsigned char>(*c));
    return path + L"\\Diablo II Resurrected\\mods\\" + modName + L"\\reroll-orb-storing.json";
}

auto ReadPlayerChoices() -> PlayerChoices {
    PlayerChoices choices;
    const std::wstring path = PlayerFilePath();
    if (path.empty()) {
        choices.problem = "the Saved Games folder or the mod name is unknown";
        return choices;
    }
    const HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        choices.problem = "no settings yet (set them in ESRLauncher's Rerolling Orb tab)";
        return choices;
    }
    std::string text(MaximumConfigBytes, '\0');
    DWORD read = 0;
    const BOOL ok = ReadFile(file, text.data(), static_cast<DWORD>(text.size()), &read, nullptr);
    CloseHandle(file);
    if (!ok) {
        choices.problem = "the settings file could not be read";
        return choices;
    }
    text.resize(read);
    if (text.size() >= 3 && static_cast<unsigned char>(text[0]) == 0xEF) text.erase(0, 3);   // UTF-8 mark

    choices.fileFound = true;
    JsonReader json{ text };
    std::string hotkey;
    std::string modifier;
    const bool parsed = json.Object([&](const std::string& name) {
        if (name == "storeHotkey") { if (!json.String(hotkey)) json.Skip(); }
        else if (name == "storeHotkeyModifier") { if (!json.String(modifier)) json.Skip(); }
        else if (name == "skipPersonalTab") json.Bool(choices.skipPersonalTab);
        else if (name == "skipInventory") json.Bool(choices.skipInventory);
        else if (name == "skipSharedStash") json.Bool(choices.skipSharedStash);
        else if (name == "magic") ReadBoxes(json, choices.magic);
        else if (name == "rare") ReadBoxes(json, choices.rare);
        else if (name == "setUnique") ReadBoxes(json, choices.setUnique);
        else json.Skip();
    });
    if (!parsed || json.failed) {
        choices = PlayerChoices{};
        choices.fileFound = true;
        choices.problem = "the settings file is not valid JSON; nothing is stored until the launcher saves it again";
        return choices;
    }
    if (!KeyFromName(hotkey, choices.hotkey)) {
        choices.hotkey = D2RL::Input::Key::None;
        choices.problem = "unknown storeHotkey name, no key is set";
    }
    if (!ModifierFromName(modifier, choices.modifier)) choices.modifier = D2RL::Input::Modifier::None;
    choices.setUnique[Jewels] = false;
    choices.setUnique[Charms] = false;
    return choices;
}

void ReadConfiguration() {
    if (!Context->EnsureConfig(DefaultConfigToml)) {
        Context->LogWarn("RerollOrbStoring: the config file could not be created; using defaults.");
        return;
    }
    std::string buffer(MaximumConfigBytes, '\0');
    if (!Context->ReadConfig(buffer.data(), static_cast<std::uint32_t>(buffer.size()), nullptr)) {
        Context->LogWarn("RerollOrbStoring: the config file could not be read; using defaults.");
        return;
    }
    buffer.resize(std::strlen(buffer.c_str()));
    ParseConfig(buffer);
    if (!ConfigProblems.empty()) D2RL::LogWarnF(Context, "RerollOrbStoring: %s", ConfigProblems.c_str());
}

// ---------------------------------------------------------------------------
// Checks
// ---------------------------------------------------------------------------

std::string VerifyFailures;

auto VerifyAll() -> bool {
    bool ok = true;
    for (const Witness& witness : Witnesses) {
        if (Context->CheckExpectedBytes(witness.rva, witness.bytes, witness.size)) continue;
        ok = false;
        char line[160];
        std::snprintf(line, sizeof(line), "%s (%llX); ", witness.name, static_cast<unsigned long long>(witness.rva));
        VerifyFailures += line;
    }
    if (!ok) D2RL::LogErrorF(Context, "RerollOrbStoring: byte check failed: %s", VerifyFailures.c_str());
    return ok;
}

// ---------------------------------------------------------------------------
// Services
// ---------------------------------------------------------------------------

const D2RL::ThreadService*    ThreadSvc{};
const D2RL::LifecycleService* LifecycleSvc{};
const D2RL::DataTableService* DataSvc{};

std::atomic<bool>                      Active{};

auto QueryServices() -> bool {
    bool ok = true;
    if (Context->QueryService(&ThreadSvc) != D2RL::ServiceQueryResult::Success
            || !D2RL::HasThreadServiceField(ThreadSvc, D2RL::ThreadServiceRequiredSize)) {
        Context->LogError("RerollOrbStoring: D2RLoader has no thread service.");
        ok = false;
    }
    if (Context->QueryService(&DataSvc) != D2RL::ServiceQueryResult::Success
            || !D2RL::HasDataTableServiceField(DataSvc, D2RL::DataTableServiceRequiredSize)) {
        Context->LogError("RerollOrbStoring: D2RLoader has no data table service.");
        ok = false;
    }
    if (Context->QueryService(&LifecycleSvc) != D2RL::ServiceQueryResult::Success
            || !D2RL::HasLifecycleServiceField(LifecycleSvc, D2RL::LifecycleServiceRequiredSize)) {
        Context->LogError("RerollOrbStoring: D2RLoader has no lifecycle service.");
        ok = false;
    }
    return ok;
}

// ---------------------------------------------------------------------------
// Item types: codes from the config resolved to itemtypes rows
// ---------------------------------------------------------------------------

// itemtypes rows per data bank (1 Classic, 2 LoD, 3 RotW): an item is checked
// against the rows of its own bank (34A0E0), as the game does.
constexpr std::size_t BankCount = 4;   // index 0 unused

struct BankRows {
    std::array<std::vector<std::int32_t>, CategoryCount> categories;
    std::vector<std::int32_t>                            orb;
};

struct TypeRows {
    std::array<BankRows, BankCount> banks;
    std::string                      missing;   // codes no bank has, for the log and console
};

std::mutex TypeMutex;
TypeRows      ResolvedTypes;   // written on the game thread, copied out by the UI thread under TypeMutex
bool          TypesResolved{};
PlayerChoices PressChoices;    // the launcher file as read on the last press, also under TypeMutex

auto PackTypeCode(const std::string& code, char pad) noexcept -> std::uint32_t {
    std::uint32_t packed = 0;
    for (std::size_t i = 0; i < 4; ++i) {
        const char c = i < code.size() ? code[i] : pad;
        packed |= static_cast<std::uint32_t>(static_cast<std::uint8_t>(c)) << (8U * i);
    }
    return packed;
}

// Game thread only. Short codes are tried zero padded, then space padded.
auto FindTypeRow(D2RL::DataTables::Bank bank, const std::string& code, std::int32_t& row) -> bool {
    for (const char pad : { '\0', ' ' }) {
        D2RL::DataTables::RowView view{};
        view.structSize = D2RL::DataTables::RowViewSize;
        if (DataSvc->findRowByCode(Context, bank, D2RL::DataTables::TableId::ItemTypes, PackTypeCode(code, pad), &view)
                == D2RL::DataTables::Result::Success
                && D2RL::DataTables::HasRowViewField(&view, D2RL::DataTables::RowViewRequiredSize)) {
            row = static_cast<std::int32_t>(view.rowIndex);
            return true;
        }
        if (code.size() >= 4) break;
    }
    return false;
}

// Resolves a code list in every bank; a code no bank knows is reported once.
void ResolveList(const std::vector<std::string>& codes, TypeRows& rows,
                 std::vector<std::int32_t> BankRows::* list, std::size_t category, std::string& missing) {
    for (const std::string& code : codes) {
        bool anywhere = false;
        for (std::size_t bank = 1; bank < BankCount; ++bank) {
            std::int32_t row = -1;
            if (!FindTypeRow(static_cast<D2RL::DataTables::Bank>(bank), code, row)) continue;
            anywhere = true;
            if (list != nullptr) (rows.banks[bank].*list).push_back(row);
            else rows.banks[bank].categories[category].push_back(row);
        }
        if (!anywhere) {
            missing += code;
            missing += ' ';
        }
    }
}

// ---------------------------------------------------------------------------
// The run
// ---------------------------------------------------------------------------

enum class Phase {
    Idle,
    OrbIn,        // orb sent into the cube, waiting for it to land
    NextItem,
    Transmuting,  // item sent in and transmuted, waiting for it to be consumed
    Restoring,    // a refused item sent back, waiting for it to land
    OrbOut,       // orb sent back, waiting for it to land
};

struct Origin {
    std::uint32_t id{};
    std::uint32_t owner{};    // 0: the player; otherwise the shared stash tab owner's unit id
    std::uint8_t  page{};
    std::uint32_t cell{};     // x | y << 16
};

struct RunView {
    bool          ran{};
    const char*   stopped{};      // why the run ended early, or null
    std::uint32_t candidates{};
    std::uint32_t stored{};
    std::uint32_t returned{};     // items a transmute refused, sent back
    bool          stashOpen{};
    bool          orbReturned{};
};

struct Run {
    Phase                 phase = Phase::Idle;
    TypeRows              types;
    PlayerChoices         choices;
    std::uint32_t         cube{};
    std::uint8_t          cubePage{};
    std::uint32_t         cubeCell{};
    Origin                orb;
    std::uint32_t         orbNow{};     // the orb's current id (a transmute may replace the orb)
    std::vector<Origin>   items;
    std::size_t           next{};
    Origin                current;
    std::uint32_t         currentCell{};
    std::uint64_t         deadline{};
    RunView               view;
};

Run              Current;
std::atomic<bool> RunQueued{};
std::mutex       ViewMutex;
RunView          LastRun;
std::atomic<std::uint64_t> Runs{};
std::atomic<std::uint64_t> ItemsStored{};

auto Now() noexcept -> std::uint64_t {
    return GetTickCount64();
}

auto X(std::uint32_t cell) noexcept -> std::uint32_t { return cell & 0xFFFF; }
auto Y(std::uint32_t cell) noexcept -> std::uint32_t { return cell >> 16; }

auto Player() noexcept -> void* {
    return LocalPlayer(LocalDataContext());
}

auto PlayerInventory(void* player) noexcept -> void* {
    return player != nullptr ? UnitInventory(player, SourceFile, __LINE__) : nullptr;
}

// The rows of the item's own data bank.
auto RowsFor(void* item, const TypeRows& types) noexcept -> const BankRows* {
    const std::uint8_t bank = DataContext(item);
    return bank >= 1 && bank < BankCount ? &types.banks[bank] : nullptr;
}

auto IsType(void* item, const std::vector<std::int32_t>& rows) noexcept -> bool {
    for (const std::int32_t row : rows) {
        if (CheckItemType(item, row) != 0) return true;
    }
    return false;
}

auto ItemById(std::uint32_t id) noexcept -> void* {
    void* item = UnitById(id, ItemUnitType);
    return item != nullptr && UnitType(item) == ItemUnitType ? item : nullptr;
}

// Every item on the player's pages 0, 3 and 4, then every item on the shared
// stash tabs (page 4 of each tab owner). owner is 0 for the player.
template <typename Visit>
auto ForEachStoredItem(void* player, void* inventory, Visit&& visit) -> const char* {
    std::size_t walked = 0;
    for (void* item = FirstItem(inventory); item != nullptr; item = NextItem(item)) {
        if (++walked > MaxItemsWalked) return "the player's item list did not end";
        if (UnitType(item) != ItemUnitType) continue;
        const std::uint8_t page = ItemPage(item);
        if (page != InventoryPage && page != CubePage && page != StashPage) continue;
        visit(item, 0U, page);
    }
    void* const advanced = AdvancedStashOwner(player);
    std::size_t owners = 0;
    for (void* record = FirstStashOwner(inventory); record != nullptr; record = NextStashOwner(record)) {
        if (++owners > MaxOwnersWalked) return "the stash owner list did not end";
        if (StashOwnerActive(record) == 0) continue;
        const std::uint32_t ownerId = StashOwnerId(record);
        void* const owner = UnitById(ownerId, PlayerUnitType);
        if (owner == nullptr || owner == player || owner == advanced || !IsSharedStashOwner(owner)) continue;
        void* const tab = UnitInventory(owner, SourceFile, __LINE__);
        if (tab == nullptr) continue;
        std::size_t tabWalked = 0;
        for (void* item = FirstItem(tab); item != nullptr; item = NextItem(item)) {
            if (++tabWalked > MaxItemsWalked) return "a shared tab's item list did not end";
            if (UnitType(item) != ItemUnitType || ItemPage(item) != StashPage) continue;
            visit(item, ownerId, StashPage);
        }
    }
    return nullptr;
}

auto Wanted(void* item, const TypeRows& types, const PlayerChoices& choices) noexcept -> bool {
    const std::uint32_t quality = ItemQuality(item);
    for (std::size_t category = 0; category < CategoryCount; ++category) {
        bool ticked = false;
        if (quality == QualityMagic) ticked = choices.magic[category];
        else if (quality == QualityRare) ticked = choices.rare[category];
        else if (quality == QualitySet || quality == QualityUnique) ticked = choices.setUnique[category];
        const BankRows* rows = RowsFor(item, types);
        if (ticked && rows != nullptr && IsType(item, rows->categories[category])) return true;
    }
    return false;
}

// Sends an item from where it is now onto the cursor.
void PickUp(std::uint32_t id, std::uint32_t owner, std::uint8_t page, std::uint32_t cell) {
    if (owner != 0) Send13(OpSharedTake, id, owner, cell);
    else Send17(OpPickUp, id, X(cell), Y(cell), page);
}

// Sends the cursor item to an exact cell.
void PutDown(std::uint32_t id, std::uint32_t owner, std::uint8_t page, std::uint32_t cell) {
    if (owner != 0) Send13(OpSharedPut, id, owner, cell);
    else Send17(OpPutDown, id, X(cell), Y(cell), page);
}

// A free cube cell for the item, from the client's own grid (only the orb is in it).
auto FreeCubeCell(void* player, void* inventory, void* item, std::uint32_t& cell) -> bool {
    const std::int32_t grid = InventoryGrid(player, CubePage, ExpansionFlag() == 0);
    std::uint32_t x = 0;
    std::uint32_t y = 0;
    if (FindFreePosition(inventory, item, grid, &x, &y, CubePage) == 0) return false;
    cell = (x & 0xFFFF) | (y << 16);
    return true;
}

auto Record(std::uint32_t id, std::uint32_t cell) noexcept -> std::uint64_t {
    return static_cast<std::uint64_t>(id) | (static_cast<std::uint64_t>(X(cell)) << 32)
         | (static_cast<std::uint64_t>(Y(cell)) << 48);
}

void Finish(const char* stopped) {
    Run& run = Current;
    run.view.ran = true;
    if (stopped != nullptr && run.view.stopped == nullptr) run.view.stopped = stopped;
    run.phase = Phase::Idle;
    ItemsStored.fetch_add(run.view.stored, std::memory_order_relaxed);
    {
        std::lock_guard lock(ViewMutex);
        LastRun = run.view;
    }
    if (run.view.stopped != nullptr) {
        D2RL::LogInfoF(Context, "RerollOrbStoring: stopped, %s. %u of %u items stored, %u sent back, orb %s.",
            run.view.stopped, run.view.stored, run.view.candidates, run.view.returned,
            run.view.orbReturned ? "returned" : "not moved back");
    } else {
        D2RL::LogInfoF(Context, "RerollOrbStoring: %u of %u items stored, orb returned.", run.view.stored,
            run.view.candidates);
    }
}

// Sends the orb back to where it started, or ends the run if it never left.
void ReturnOrb(const char* stopped) {
    Run& run = Current;
    if (stopped != nullptr && run.view.stopped == nullptr) run.view.stopped = stopped;
    void* const orb = ItemById(run.orbNow);
    if (orb == nullptr || ItemPage(orb) != CubePage) {
        Finish(nullptr);
        return;
    }
    PickUp(run.orbNow, 0, CubePage, PackedPosition(orb));
    PutDown(run.orbNow, run.orb.owner, run.orb.page, run.orb.cell);
    run.phase    = Phase::OrbOut;
    run.deadline = Now() + StepTimeoutMs;
}

// The only item in the cube that is an orb, after a transmute.
auto FindOrbInCube(void* inventory, const TypeRows& types, std::uint32_t& id, std::size_t& cubeCount) -> bool {
    id = 0;
    cubeCount = 0;
    std::size_t walked = 0;
    for (void* item = FirstItem(inventory); item != nullptr; item = NextItem(item)) {
        if (++walked > MaxItemsWalked) return false;
        if (UnitType(item) != ItemUnitType || ItemPage(item) != CubePage) continue;
        ++cubeCount;
        const BankRows* rows = RowsFor(item, types);
        if (rows != nullptr && IsType(item, rows->orb)) id = UnitId(item);
    }
    return id != 0;
}

// Starts a run. UI thread.
auto Start() -> const char* {
    Run& run = Current;
    run = Run{};
    run.view.stashOpen = UiStateOpen(StashUiState) != 0;
    {
        std::lock_guard lock(TypeMutex);
        if (!TypesResolved) return "the item types are not resolved";
        run.types   = ResolvedTypes;
        run.choices = PressChoices;
    }
    if (!run.choices.fileFound) return "no launcher settings yet (ESRLauncher, Rerolling Orb tab)";
    if (!run.choices.problem.empty() && run.choices.problem.rfind("unknown storeHotkey", 0) != 0) return "the launcher settings file could not be read";
    bool anyOrbType = false;
    for (std::size_t bank = 1; bank < BankCount; ++bank) anyOrbType = anyOrbType || !run.types.banks[bank].orb.empty();
    if (!anyOrbType) return "none of the orb_types codes exist in itemtypes.txt";

    void* const player = Player();
    if (player == nullptr) return "there is no local player";
    void* const inventory = PlayerInventory(player);
    if (inventory == nullptr) return "the player has no inventory";
    if (CursorItem(inventory) != nullptr) return "an item is on the cursor";

    void*        cube = nullptr;
    std::size_t  inCube = 0;
    Origin       orb{};
    std::uint8_t orbRank = 0xFF;   // 0 inventory, 1 personal stash, 2 shared tab
    const char* const walkProblem = ForEachStoredItem(player, inventory, [&](void* item, std::uint32_t owner, std::uint8_t page) {
        if (owner == 0 && page == CubePage) {
            ++inCube;
            return;
        }
        if (page == StashPage && !run.view.stashOpen) return;
        if (owner == 0 && ItemCode(item) == CubeCode) {
            if (cube == nullptr) cube = item;
            return;
        }
        const BankRows* rows = RowsFor(item, run.types);
        if (rows != nullptr && IsType(item, rows->orb)) {
            const std::uint8_t rank = owner != 0 ? 2 : (page == InventoryPage ? 0 : 1);
            if (rank < orbRank) {
                orbRank = rank;
                orb = Origin{ UnitId(item), owner, page, PackedPosition(item) };
            }
            return;
        }
        if (page == InventoryPage && run.choices.skipInventory) return;
        if (owner == 0 && page == StashPage && run.choices.skipPersonalTab) return;
        if (owner != 0 && run.choices.skipSharedStash) return;   // the orb above is exempt from every skip
        if (Wanted(item, run.types, run.choices)) run.items.push_back(Origin{ UnitId(item), owner, page, PackedPosition(item) });
    });
    if (walkProblem != nullptr) return walkProblem;
    if (cube == nullptr) return "no Horadric Cube in the inventory or the open personal stash";
    if (inCube != 0) return "the Horadric Cube is not empty";
    if (orbRank == 0xFF) return run.view.stashOpen ? "no Rerolling Orb found" : "no Rerolling Orb in the inventory (open the stash to use one from there)";
    run.view.candidates = static_cast<std::uint32_t>(run.items.size());
    if (run.items.empty()) return "no matching items";

    run.cube     = UnitId(cube);
    run.cubePage = ItemPage(cube);
    run.cubeCell = PackedPosition(cube);
    run.orb      = orb;
    run.orbNow   = orb.id;

    void* const orbItem = ItemById(orb.id);
    std::uint32_t cell = 0;
    if (orbItem == nullptr || !FreeCubeCell(player, inventory, orbItem, cell)) return "the orb does not fit in the cube";
    PickUp(orb.id, orb.owner, orb.page, orb.cell);
    PutDown(orb.id, 0, CubePage, cell);
    run.phase    = Phase::OrbIn;
    run.deadline = Now() + StepTimeoutMs;
    Runs.fetch_add(1, std::memory_order_relaxed);
    D2RL::LogInfoF(Context, "RerollOrbStoring: run started, %u items, orb %08X, stash %s.", run.view.candidates,
        orb.id, run.view.stashOpen ? "open" : "closed");
    return nullptr;
}

// One step of the run. UI thread. Returns true while the run goes on.
auto Step() -> bool {
    Run& run = Current;
    void* const player = Player();
    void* const inventory = PlayerInventory(player);
    if (inventory == nullptr) {
        Finish("the player left the game");
        return false;
    }
    const bool late = Now() > run.deadline;

    switch (run.phase) {
    case Phase::Idle:
        return false;

    case Phase::OrbIn: {
        void* const orb = ItemById(run.orbNow);
        if (orb != nullptr && ItemPage(orb) == CubePage && CursorItem(inventory) == nullptr) {
            run.phase = Phase::NextItem;
            return true;
        }
        if (!late) return true;
        if (orb != nullptr && CursorItem(inventory) == orb) {
            PutDown(run.orb.id, run.orb.owner, run.orb.page, run.orb.cell);
        }
        Finish("the orb did not reach the cube");
        return false;
    }

    case Phase::NextItem: {
        void* const orb = ItemById(run.orbNow);
        if (orb == nullptr || ItemPage(orb) != CubePage) {
            Finish("the orb left the cube");
            return false;
        }
        while (run.next < run.items.size()) {
            const Origin origin = run.items[run.next++];
            void* const item = ItemById(origin.id);
            if (item == nullptr || ItemPage(item) != origin.page || PackedPosition(item) != origin.cell) continue;
            if (origin.page == StashPage && UiStateOpen(StashUiState) == 0) {
                ReturnOrb("the stash was closed");
                return true;
            }
            std::uint32_t cell = 0;
            if (!FreeCubeCell(player, inventory, item, cell)) {
                ReturnOrb("an item does not fit in the cube next to the orb");
                return true;
            }
            const std::uint32_t orbCell = PackedPosition(orb);
            std::array<std::uint64_t, 2> records{ Record(run.orbNow, orbCell), Record(origin.id, cell) };
            if ((Y(cell) << 16 | X(cell)) < (Y(orbCell) << 16 | X(orbCell))) std::swap(records[0], records[1]);
            PickUp(origin.id, origin.owner, origin.page, origin.cell);
            PutDown(origin.id, 0, CubePage, cell);
            SendTransmute(run.cube, run.cubePage, run.cubeCell, records.data(), records.data() + records.size());
            run.current     = origin;
            run.currentCell = cell;
            run.phase       = Phase::Transmuting;
            run.deadline    = Now() + StepTimeoutMs;
            return true;
        }
        ReturnOrb(nullptr);
        return true;
    }

    case Phase::Transmuting: {
        void* const item = ItemById(run.current.id);
        std::uint32_t orbId = 0;
        std::size_t   cubeCount = 0;
        const bool orbFound = FindOrbInCube(inventory, run.types, orbId, cubeCount);
        if (item == nullptr && CursorItem(inventory) == nullptr) {
            if (!orbFound || cubeCount != 1) {
                Finish("the cube does not hold only the orb after a transmute");
                return false;
            }
            run.orbNow = orbId;
            ++run.view.stored;
            run.phase = Phase::NextItem;
            return true;
        }
        if (!late) return true;
        // Not consumed: send it back to where it came from and stop.
        if (item != nullptr && ItemPage(item) == CubePage) {
            PickUp(run.current.id, 0, CubePage, PackedPosition(item));
            PutDown(run.current.id, run.current.owner, run.current.page, run.current.cell);
        } else if (item != nullptr && CursorItem(inventory) == item) {
            PutDown(run.current.id, run.current.owner, run.current.page, run.current.cell);
        } else {
            if (orbFound) run.orbNow = orbId;
            ReturnOrb("an item was not consumed by the transmute");
            return true;
        }
        if (orbFound) run.orbNow = orbId;
        run.view.stopped = "an item was not consumed by the transmute";
        run.phase    = Phase::Restoring;
        run.deadline = Now() + StepTimeoutMs;
        return true;
    }

    case Phase::Restoring: {
        void* const item = ItemById(run.current.id);
        const bool home = item != nullptr && ItemPage(item) == run.current.page && PackedPosition(item) == run.current.cell
                       && CursorItem(inventory) == nullptr;
        if (home) ++run.view.returned;
        if (home || late) {
            ReturnOrb(home ? nullptr : "a refused item could not be sent back");
            return run.phase != Phase::Idle;
        }
        return true;
    }

    case Phase::OrbOut: {
        void* const orb = ItemById(run.orbNow);
        if (orb != nullptr && ItemPage(orb) == run.orb.page && PackedPosition(orb) == run.orb.cell
                && CursorItem(inventory) == nullptr) {
            run.view.orbReturned = true;
            Finish(nullptr);
            return false;
        }
        if (!late) return true;
        Finish("the orb did not get back to its cell");
        return false;
    }
    }
    return false;
}

void __cdecl RunStep(const D2RL::PluginContext*, void*) noexcept;

// The next step runs on the next UI update. A callback cannot queue itself on
// its own thread, so each step hands over to the game thread, which queues the
// next UI step (the same relay Stance Dance uses).
void __cdecl RelayStep(const D2RL::PluginContext*, void*) noexcept {
    if (ThreadSvc->runOnUiThread(Context, &RunStep, nullptr) != D2RL::Threads::Result::Success) {
        RunQueued.store(false);
        Context->LogError("RerollOrbStoring: the next UI step could not be queued; the run stopped where it was.");
    }
}

auto QueueStep() -> bool {
    if (ThreadSvc->runOnGameThread(Context, &RelayStep, nullptr) == D2RL::Threads::Result::Success) return true;
    Finish("the next step could not be queued");
    RunQueued.store(false);
    return false;
}

void __cdecl RunStep(const D2RL::PluginContext*, void*) noexcept {
    if (!Active.load(std::memory_order_relaxed)) {
        RunQueued.store(false);
        return;
    }
    if (Step()) {
        (void)QueueStep();
        return;
    }
    RunQueued.store(false);
}

void __cdecl RunStart(const D2RL::PluginContext*, void*) noexcept {
    if (!Active.load(std::memory_order_relaxed)) {
        RunQueued.store(false);
        return;
    }
    if (const char* refused = Start(); refused != nullptr) {
        Finish(refused);
        RunQueued.store(false);
        return;
    }
    (void)QueueStep();
}

// Game thread: resolve the configured codes, then hand over to the UI thread.
void __cdecl ResolveTypes(const D2RL::PluginContext*, void*) noexcept {
    TypeRows rows;
    for (std::size_t category = 0; category < CategoryCount; ++category) {
        ResolveList(Config.types[category], rows, nullptr, category, rows.missing);
    }
    ResolveList(Config.orbTypes, rows, &BankRows::orb, 0, rows.missing);
    PlayerChoices choices = ReadPlayerChoices();
    if (!choices.problem.empty()) D2RL::LogWarnF(Context, "RerollOrbStoring: launcher settings: %s", choices.problem.c_str());
    if (!rows.missing.empty()) {
        D2RL::LogWarnF(Context, "RerollOrbStoring: these item type codes are not in itemtypes.txt: %s",
            rows.missing.c_str());
    }
    {
        std::lock_guard lock(TypeMutex);
        ResolvedTypes = rows;
        TypesResolved = true;
        PressChoices  = choices;
    }
    if (ThreadSvc->runOnUiThread(Context, &RunStart, nullptr) != D2RL::Threads::Result::Success) {
        Context->LogWarn("RerollOrbStoring: the run could not be queued on the UI thread.");
        RunQueued.store(false);
    }
}

std::atomic<std::uint64_t> Presses{};

// ---------------------------------------------------------------------------
// The hotkey: read from the launcher's file and caught from the game window,
// like ESR-utils did in 2.4. A change saved in the launcher applies at the
// next key press: the file's write time is checked as keys come in (at most
// every 250 ms) and the key is reloaded when it changed. The key still reaches
// the game afterwards. It does nothing while the chat input is open
// (UI state 5, ChatPanel). The window is hooked from the game join and player
// ready events (UI thread); work queued during plugin load is not accepted.
// ---------------------------------------------------------------------------

std::atomic<std::uint32_t> HotkeyVk{};          // 0: no key
std::atomic<std::uint32_t> HotkeyModifier{};    // D2RL::Input::Modifier
std::mutex                 FileMutex;
FILETIME                   FileWritten{};
bool                       FileSeen{};
std::uint64_t              FileCheckedAt{};
HWND                       GameWindow{};
WNDPROC                    OriginalWndProc{};
std::atomic<std::uint64_t> KeyChecks{};
std::atomic<std::uint64_t> ChatBlocked{};

void ApplyHotkey(const PlayerChoices& choices, bool announce) {
    const auto vk = static_cast<std::uint32_t>(choices.hotkey);
    const auto modifier = vk == 0 ? 0U : static_cast<std::uint32_t>(choices.modifier);
    const bool changed = HotkeyVk.exchange(vk) != vk;
    const bool modifierChanged = HotkeyModifier.exchange(modifier) != modifier;
    if (announce && (changed || modifierChanged)) {
        D2RL::LogInfoF(Context, "RerollOrbStoring: hotkey is now %s (vk %02X, modifier %u).",
            vk == 0 ? "not set" : "set", vk, modifier);
    }
}

// Reloads the key when the launcher's file changed. Cheap enough for the
// window procedure: one attribute read, at most every 250 ms.
void RefreshHotkey(bool force) {
    std::lock_guard lock(FileMutex);
    const std::uint64_t now = Now();
    if (!force && now - FileCheckedAt < 250) return;
    FileCheckedAt = now;
    const std::wstring path = PlayerFilePath();
    WIN32_FILE_ATTRIBUTE_DATA data{};
    const bool exists = !path.empty() && GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &data) != 0;
    if (!force && exists == FileSeen
            && (!exists || CompareFileTime(&data.ftLastWriteTime, &FileWritten) == 0)) {
        return;
    }
    FileSeen = exists;
    if (exists) FileWritten = data.ftLastWriteTime;
    const PlayerChoices choices = ReadPlayerChoices();
    if (!choices.problem.empty()) D2RL::LogWarnF(Context, "RerollOrbStoring: launcher settings: %s", choices.problem.c_str());
    ApplyHotkey(choices, !force);
}

auto ModifiersHeld() noexcept -> std::uint32_t {
    const bool control = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
    const bool alt     = (GetKeyState(VK_MENU) & 0x8000) != 0;
    const bool shift   = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
    if (control + alt + shift > 1) return 0xFF;   // never matches
    if (control) return static_cast<std::uint32_t>(D2RL::Input::Modifier::Control);
    if (alt) return static_cast<std::uint32_t>(D2RL::Input::Modifier::Alt);
    if (shift) return static_cast<std::uint32_t>(D2RL::Input::Modifier::Shift);
    return static_cast<std::uint32_t>(D2RL::Input::Modifier::None);
}

// With NumLock off (or Shift held) the keypad sends the navigation keys; a
// navigation key without the extended-key flag (lParam bit 24) came from the
// keypad. Same mapping ESR-utils used.
auto KeypadKey(WPARAM vk, LPARAM lParam) noexcept -> std::uint32_t {
    if ((lParam & 0x01000000) != 0) return static_cast<std::uint32_t>(vk);
    switch (vk) {
    case VK_INSERT: return VK_NUMPAD0;
    case VK_END:    return VK_NUMPAD1;
    case VK_DOWN:   return VK_NUMPAD2;
    case VK_NEXT:   return VK_NUMPAD3;
    case VK_LEFT:   return VK_NUMPAD4;
    case VK_CLEAR:  return VK_NUMPAD5;
    case VK_RIGHT:  return VK_NUMPAD6;
    case VK_HOME:   return VK_NUMPAD7;
    case VK_UP:     return VK_NUMPAD8;
    case VK_PRIOR:  return VK_NUMPAD9;
    default:        return static_cast<std::uint32_t>(vk);
    }
}

// source names the press in the log: the hotkey or the controller button.
void StartRun(const char* source) {
    Presses.fetch_add(1, std::memory_order_relaxed);
    if (RunQueued.exchange(true)) {
        D2RL::LogInfoF(Context, "RerollOrbStoring: %s pressed while a run is going; ignored.", source);
        return;
    }
    D2RL::LogInfoF(Context, "RerollOrbStoring: %s pressed, starting a run.", source);
    if (ThreadSvc->runOnGameThread(Context, &ResolveTypes, nullptr) != D2RL::Threads::Result::Success) {
        RunQueued.store(false);
        Context->LogWarn("RerollOrbStoring: no local game thread (a remote TCP/IP client cannot run this).");
    }
}

LRESULT CALLBACK HookedWndProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    // WM_SYSKEYDOWN carries Alt+key and F10. Bit 30 set means a key repeat.
    if ((message == WM_KEYDOWN || message == WM_SYSKEYDOWN) && (lParam & 0x40000000) == 0
            && Active.load(std::memory_order_relaxed)) {
        KeyChecks.fetch_add(1, std::memory_order_relaxed);
        RefreshHotkey(false);
        const std::uint32_t wanted = HotkeyVk.load(std::memory_order_relaxed);
        if (wanted != 0) {
            const std::uint32_t vk = KeypadKey(wParam, lParam);
            if ((vk == wanted || static_cast<std::uint32_t>(wParam) == wanted)
                    && ModifiersHeld() == HotkeyModifier.load(std::memory_order_relaxed)) {
                if (UiStateOpen(ChatUiState) != 0) ChatBlocked.fetch_add(1, std::memory_order_relaxed);
                else StartRun("key");
            }
        }
    }
    return CallWindowProcW(OriginalWndProc, window, message, wParam, lParam);
}

struct WindowSearch {
    DWORD process;
    HWND  game;      // class OsWindow
    HWND  largest;   // the largest visible top-level window, as a fallback
    LONG  area;
};

BOOL CALLBACK FindGameWindow(HWND window, LPARAM parameter) {
    auto* search = reinterpret_cast<WindowSearch*>(parameter);
    DWORD process = 0;
    GetWindowThreadProcessId(window, &process);
    if (process != search->process || !IsWindowVisible(window)) return TRUE;
    wchar_t name[64]{};
    if (GetClassNameW(window, name, 64) != 0 && std::wcscmp(name, L"OsWindow") == 0) {
        search->game = window;
        return FALSE;
    }
    RECT rect{};
    if (GetWindowRect(window, &rect)) {
        const LONG area = (rect.right - rect.left) * (rect.bottom - rect.top);
        if (area > search->area) {
            search->area    = area;
            search->largest = window;
        }
    }
    return TRUE;
}

std::atomic<std::uint64_t> HookAttempts{};

// Hooks the game window once. Runs on the UI thread: from the game join and
// player ready events, and from the console command.
void InstallWindowHook() {
    if (!Active.load(std::memory_order_relaxed) || OriginalWndProc != nullptr) return;
    HookAttempts.fetch_add(1, std::memory_order_relaxed);
    WindowSearch search{ GetCurrentProcessId(), nullptr, nullptr, 0 };
    EnumWindows(&FindGameWindow, reinterpret_cast<LPARAM>(&search));
    HWND window = search.game != nullptr ? search.game : search.largest;
    if (window == nullptr) {
        const HWND active = GetActiveWindow();
        DWORD process = 0;
        if (active != nullptr) GetWindowThreadProcessId(active, &process);
        if (process == GetCurrentProcessId()) window = active;
    }
    if (window == nullptr) {
        Context->LogWarn("RerollOrbStoring: no game window yet; the hotkey is hooked when a game is joined.");
        return;
    }
    wchar_t name[64]{};
    GetClassNameW(window, name, 64);
    char className[64]{};
    for (int i = 0; i < 63 && name[i] != 0; ++i) className[i] = name[i] < 128 ? static_cast<char>(name[i]) : '?';
    SetLastError(0);
    const LONG_PTR previous = SetWindowLongPtrW(window, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&HookedWndProc));
    if (previous == 0 && GetLastError() != 0) {
        D2RL::LogErrorF(Context, "RerollOrbStoring: the game window (class %s) could not be hooked (error %lu).",
            className, GetLastError());
        return;
    }
    GameWindow      = window;
    OriginalWndProc = reinterpret_cast<WNDPROC>(previous);
    D2RL::LogInfoF(Context, "RerollOrbStoring: listening for the hotkey on the game window (class %s).", className);
}

void __cdecl OnGameEvent(const D2RL::PluginContext*, const D2RL::Lifecycle::GameplayEvent*, void*) noexcept {
    InstallWindowHook();
}

auto RegisterGameEvents() -> bool {
    for (const auto kind : { D2RL::Lifecycle::GameplayEventKind::GameJoined,
                             D2RL::Lifecycle::GameplayEventKind::LocalPlayerReady }) {
        const D2RL::Lifecycle::GameplayEventListener listener{
            .structSize = D2RL::Lifecycle::GameplayEventListenerSize,
            .kind       = kind,
            .callback   = &OnGameEvent,
        };
        D2RL::Lifecycle::ListenerHandle handle = D2RL::Lifecycle::InvalidHandle;
        if (LifecycleSvc->registerGameplayEventListener(Context, &listener, &handle) != D2RL::Lifecycle::Result::Success) {
            Context->LogError("RerollOrbStoring: a game join listener was refused; the hotkey is hooked by the console "
                              "command instead.");
            return false;
        }
    }
    return true;
}

void RemoveWindowHook() {
    if (GameWindow == nullptr || OriginalWndProc == nullptr) return;
    // Only put the old procedure back if nothing hooked the window after us;
    // otherwise that hook would be cut out of the chain.
    if (GetWindowLongPtrW(GameWindow, GWLP_WNDPROC) == reinterpret_cast<LONG_PTR>(&HookedWndProc)) {
        SetWindowLongPtrW(GameWindow, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(OriginalWndProc));
        GameWindow = nullptr;
        OriginalWndProc = nullptr;
    }
}

// ---------------------------------------------------------------------------
// The controller button (section 5): resources, click listener, child layout
// ---------------------------------------------------------------------------

const D2RL::ResourceService*    ResourceSvc{};
const D2RL::SharedEventService* EventSvc{};

D2RL::Resources::RegistrationHandle ButtonSpriteHandle = D2RL::Resources::InvalidHandle;
D2RL::Resources::RegistrationHandle ButtonLowendHandle = D2RL::Resources::InvalidHandle;
D2RL::SharedEvents::ListenerHandle  ClickListener      = D2RL::SharedEvents::InvalidHandle;
const char*                         ButtonProblem      = "the plugin is not active";   // null once the button is in
std::atomic<std::uint64_t>          ButtonPresses{};

// The button's click. The message is private to this plugin, so it never goes
// on to PanelManager.
auto __cdecl OnUiMessage(const D2RL::PluginContext*, const D2RL::SharedEvents::UiMessageEvent* event, void*) noexcept
        -> D2RL::SharedEvents::UiMessageAction {
    if (event == nullptr || event->structSize < D2RL::SharedEvents::UiMessageEventRequiredSize
            || event->target == nullptr || event->command == nullptr || event->text == nullptr
            || ClickTarget != event->target || ClickCommand != event->command || ClickText != event->text) {
        return D2RL::SharedEvents::UiMessageAction::Continue;
    }
    ButtonPresses.fetch_add(1, std::memory_order_relaxed);
    if (Active.load(std::memory_order_relaxed)) StartRun("controller button");
    return D2RL::SharedEvents::UiMessageAction::Consume;
}

void RemoveControllerButton() {
    if (ClickListener != D2RL::SharedEvents::InvalidHandle && EventSvc != nullptr) {
        (void)EventSvc->unregisterUiMessageListener(Context, ClickListener);
    }
    ClickListener = D2RL::SharedEvents::InvalidHandle;
    for (auto* handle : { &ButtonSpriteHandle, &ButtonLowendHandle }) {
        if (*handle != D2RL::Resources::InvalidHandle && ResourceSvc != nullptr) {
            (void)ResourceSvc->unregisterResource(Context, *handle);
        }
        *handle = D2RL::Resources::InvalidHandle;
    }
}

// An RCDATA resource of this DLL (plugin.rc), checked to be a sprite.
auto EmbeddedSprite(int id, const void*& bytes, std::uint64_t& size) -> bool {
    HMODULE module{};
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCWSTR>(&EmbeddedSprite), &module)) {
        return false;
    }
    HRSRC found = FindResourceW(module, MAKEINTRESOURCEW(id), MAKEINTRESOURCEW(10));   // RT_RCDATA
    HGLOBAL loaded = found != nullptr ? LoadResource(module, found) : nullptr;
    bytes = loaded != nullptr ? LockResource(loaded) : nullptr;
    size  = found != nullptr ? SizeofResource(module, found) : 0;
    return bytes != nullptr && size > 40 && std::memcmp(bytes, "SpA1", 4) == 0;
}

auto RegisterResource(const char* path, const void* bytes, std::uint64_t size,
                      D2RL::Resources::RegistrationHandle& handle) -> bool {
    const D2RL::Resources::ResourceRegistration resource{
        .structSize = D2RL::Resources::ResourceRegistrationSize,
        .flags      = 0,
        .path       = path,
        .bytes      = bytes,
        .byteCount  = size,
    };
    return ResourceSvc->registerResource(Context, &resource, &handle) == D2RL::Resources::Result::Success;
}

// Runs at load: the art and the click listener for the layout's
// autostore_button. On any failure nothing stays registered and the hotkey
// still works.
void AddControllerButton() {
    ButtonProblem = nullptr;
    if (Context->QueryService(&ResourceSvc) != D2RL::ServiceQueryResult::Success
            || !D2RL::HasResourceServiceField(ResourceSvc, D2RL::ResourceServiceRequiredSize)) {
        ButtonProblem = "D2RLoader has no resource service";
    } else if (Context->QueryService(&EventSvc) != D2RL::ServiceQueryResult::Success
            || !D2RL::HasSharedEventServiceField(EventSvc, D2RL::SharedEventServiceRequiredSize)) {
        ButtonProblem = "D2RLoader has no shared event service";
    }
    if (ButtonProblem == nullptr) {
        const void*   sprite{};
        const void*   lowend{};
        std::uint64_t spriteSize{};
        std::uint64_t lowendSize{};
        if (!EmbeddedSprite(AUTOSTORE_BUTTON_CONTROLLER_SPRITE_ID, sprite, spriteSize)
                || !EmbeddedSprite(AUTOSTORE_BUTTON_CONTROLLER_LOWEND_SPRITE_ID, lowend, lowendSize)) {
            ButtonProblem = "the button art is missing from the DLL";
        } else if (!RegisterResource(ButtonSpritePath, sprite, spriteSize, ButtonSpriteHandle)
                || !RegisterResource(ButtonLowendPath, lowend, lowendSize, ButtonLowendHandle)) {
            ButtonProblem = "the button art could not be registered";
        }
    }
    if (ButtonProblem == nullptr) {
        const D2RL::SharedEvents::UiMessageListener listener{
            .structSize = D2RL::SharedEvents::UiMessageListenerSize,
            .flags      = 0,
            .priority   = 10000,
            .reserved   = 0,
            .callback   = &OnUiMessage,
            .userData   = nullptr,
        };
        if (EventSvc->registerUiMessageListener(Context, &listener, &ClickListener) != D2RL::SharedEvents::Result::Success) {
            ButtonProblem = "the button click listener could not be registered";
        }
    }
    if (ButtonProblem != nullptr) {
        RemoveControllerButton();
        D2RL::LogWarnF(Context, "RerollOrbStoring: no controller button, %s; the hotkey still works.", ButtonProblem);
    }
}

auto HotkeyText() -> std::string {
    const std::uint32_t vk = HotkeyVk.load();
    if (vk == 0) return "not set";
    std::string name;
    if (vk >= 0x41 && vk <= 0x5A) name = std::string(1, static_cast<char>(vk));
    else if (vk >= 0x30 && vk <= 0x39) name = std::string(1, static_cast<char>(vk));
    else if (vk >= 0x70 && vk <= 0x87) name = "F" + std::to_string(vk - 0x70 + 1);
    else if (vk >= 0x60 && vk <= 0x69) name = "Numpad" + std::to_string(vk - 0x60);
    else {
        char hex[16];
        std::snprintf(hex, sizeof(hex), "key %02X", vk);
        name = hex;
    }
    switch (static_cast<D2RL::Input::Modifier>(HotkeyModifier.load())) {
    case D2RL::Input::Modifier::Shift:   return "Shift + " + name;
    case D2RL::Input::Modifier::Control: return "Control + " + name;
    case D2RL::Input::Modifier::Alt:     return "Alt + " + name;
    default:                             return name;
    }
}

// ---------------------------------------------------------------------------
// Console command
// ---------------------------------------------------------------------------

enum class PluginState { Loading, DisabledByConfig, UnsupportedBuild, ServicesMissing, Active };
PluginState State = PluginState::Loading;

auto StateName(PluginState state) noexcept -> const char* {
    switch (state) {
    case PluginState::Loading:          return "loading";
    case PluginState::DisabledByConfig: return "disabled in the config";
    case PluginState::UnsupportedBuild: return "not installed, this D2R build does not match";
    case PluginState::ServicesMissing:  return "not installed, D2RLoader lacks a needed service";
    case PluginState::Active:           return "active";
    }
    return "unknown";
}

auto __cdecl StatusCommand(D2R::Game::Client*, const D2RL::ConsoleCommandContext* command, void*) noexcept
        -> D2RL::ConsoleCommandResult {
    if (command == nullptr || command->plugin == nullptr) return D2RL::ConsoleCommandResult::Failed;
    InstallWindowHook();
    RefreshHotkey(true);
    {
        const PlayerChoices fresh = ReadPlayerChoices();
        std::lock_guard lock(TypeMutex);
        PressChoices = fresh;
    }
    char line[512];
    const auto say = [&](const char* text) { command->plugin->WriteConsoleMessage(text); };

    std::snprintf(line, sizeof(line), "Rerolling Orb Storing %s: %s.", PluginVersion, StateName(State));
    say(line);
    if (!VerifyFailures.empty()) {
        std::snprintf(line, sizeof(line), "  byte check failed: %s", VerifyFailures.c_str());
        say(line);
    }
    if (!ConfigProblems.empty()) {
        std::snprintf(line, sizeof(line), "  config: %s", ConfigProblems.c_str());
        say(line);
    }
    {
        std::lock_guard lock(TypeMutex);
        if (!TypesResolved) {
            say("  item types: resolved on the first press.");
        } else {
            std::string rows;
            for (std::size_t bank = 1; bank < BankCount; ++bank) {
                const BankRows& found = ResolvedTypes.banks[bank];
                rows += bank == 1 ? "classic[" : bank == 2 ? "lod[" : "rotw[";
                for (std::size_t category = 0; category < CategoryCount; ++category) {
                    rows += std::to_string(found.categories[category].size());
                    rows += ' ';
                }
                rows += "orb ";
                rows += std::to_string(found.orb.size());
                rows += "] ";
            }
            std::snprintf(line, sizeof(line), "  item type rows (rings amulets jewels charms quivers): %s%s%s", rows.c_str(),
                ResolvedTypes.missing.empty() ? "" : "; not in itemtypes.txt: ", ResolvedTypes.missing.c_str());
            say(line);
        }
    }
    {
        std::lock_guard lock(TypeMutex);
        const PlayerChoices& c = PressChoices;
        std::string boxes;
        for (std::size_t i = 0; i < CategoryCount; ++i) {
            if (c.magic[i]) { boxes += "magic "; boxes += CategoryNames[i]; boxes += ", "; }
            if (c.rare[i]) { boxes += "rare "; boxes += CategoryNames[i]; boxes += ", "; }
            if (c.setUnique[i]) { boxes += "set/unique "; boxes += CategoryNames[i]; boxes += ", "; }
        }
        std::snprintf(line, sizeof(line), "  launcher settings: %s%s%sskip personal tab %s, skip shared stash %s, skip inventory %s; %s",
            c.fileFound ? "found" : "not found", c.problem.empty() ? "" : " (", c.problem.empty() ? ", " : (c.problem + "), ").c_str(),
            c.skipPersonalTab ? "on" : "off", c.skipSharedStash ? "on" : "off", c.skipInventory ? "on" : "off", boxes.empty() ? "nothing ticked" : boxes.c_str());
        say(line);
    }
    RunView view;
    {
        std::lock_guard lock(ViewMutex);
        view = LastRun;
    }
    if (!view.ran) {
        say("  last run: none yet.");
    } else {
        std::snprintf(line, sizeof(line), "  last run: %u of %u items stored, %u sent back, orb %s, stash %s%s%s.",
            view.stored, view.candidates, view.returned, view.orbReturned ? "returned" : "not moved back",
            view.stashOpen ? "open" : "closed", view.stopped != nullptr ? "; stopped: " : "",
            view.stopped != nullptr ? view.stopped : "");
        say(line);
    }
    std::snprintf(line, sizeof(line), "  hotkey: %s, %s (%llu hook attempts); %llu key presses seen, %llu ignored while "
        "chat was open.", HotkeyText().c_str(), GameWindow != nullptr ? "listening on the game window" : "game window not hooked",
        static_cast<unsigned long long>(HookAttempts.load()), static_cast<unsigned long long>(KeyChecks.load()),
        static_cast<unsigned long long>(ChatBlocked.load()));
    say(line);
    std::snprintf(line, sizeof(line), "  controller button: %s%s; %llu presses.",
        ButtonProblem == nullptr ? "art and click listener ready for autostore_button" : "not available, ",
        ButtonProblem == nullptr ? "" : ButtonProblem, static_cast<unsigned long long>(ButtonPresses.load()));
    say(line);
    std::snprintf(line, sizeof(line), "  totals: %llu presses (hotkey and controller button), %llu runs, %llu items stored.",
        static_cast<unsigned long long>(Presses.load()), static_cast<unsigned long long>(Runs.load()),
        static_cast<unsigned long long>(ItemsStored.load()));
    say(line);
    return D2RL::ConsoleCommandResult::Handled;
}

constexpr D2RL::PluginInfo PluginInfoData{
    .infoSize    = D2RL::PluginInfoSize,
    .abiVersion  = D2RL_PLUGIN_ABI_VERSION,
    .id          = PluginIdText,
    .name        = "Rerolling Orb Storing",
    .version     = PluginVersion,
    .author      = "CelestialRayOne",
    .description = "A hotkey, set in ESRLauncher, that transmutes matching rings, amulets, jewels, charms and "
                   "quivers into a Rerolling Orb, one item at a time, without clicking. On a controller, an "
                   "Autostore button on the inventory does the same.",
    .flags       = D2RL::PluginFlags::Client | D2RL::PluginFlags::NativeHooks,
};

} // namespace

D2RL_PLUGIN_EXPORT auto D2RLoaderGetPluginInfo() noexcept -> const D2RL::PluginInfo* {
    return &PluginInfoData;
}

D2RL_PLUGIN_EXPORT auto D2RLoaderLoadPlugin(const D2RL::PluginContext* context) noexcept -> bool {
    Context = context;
    if (Context == nullptr || Context->exeBase == 0) return false;
    Base = Context->exeBase;

    if (!Context->RegisterConsoleCommand("rerollorbstoring", &StatusCommand,
            "Rerolling Orb Storing status: resolved item types and what the last run did.")) {
        Context->LogWarn("RerollOrbStoring: the console command could not be registered.");
    }

    ReadConfiguration();
    if (!Config.enabled) {
        State = PluginState::DisabledByConfig;
        Context->LogInfo("RerollOrbStoring: disabled by config.");
        return true;
    }
    if (!VerifyAll()) {
        State = PluginState::UnsupportedBuild;
        Context->LogError("RerollOrbStoring: this D2R build does not match; nothing was installed.");
        return true;
    }
    BindNatives();
    if (!QueryServices()) {
        State = PluginState::ServicesMissing;
        return true;
    }
    Active.store(true);
    RefreshHotkey(true);
    (void)RegisterGameEvents();
    AddControllerButton();
    State = PluginState::Active;
    D2RL::LogInfoF(Context, "RerollOrbStoring %s: active; hotkey %s, set in ESRLauncher's Rerolling Orb tab; "
        "controller button %s.", PluginVersion, HotkeyText().c_str(), ButtonProblem == nullptr ? "ready" : "not available");
    return true;
}

D2RL_PLUGIN_EXPORT void D2RLoaderUnloadPlugin() noexcept {
    Active.store(false);
    if (Context == nullptr) return;
    RemoveWindowHook();
    RemoveControllerButton();
}
