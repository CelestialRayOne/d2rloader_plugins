// =============================================================================
//  Chronicle Tracker  -  celestialrayone.chronicle-tracker
//
//  1. A Set item, Unique item or Runeword completes its Chronicle entry the
//     moment a character comes into possession of it already identified:
//     picked up from the ground, or handed over by a Horadric Cube transmute.
//  2. A Set item, Unique item or Runeword on the ground whose entry the local
//     character has not completed gets a "Not in Chronicle" line under its
//     label (Alt labels and the hover name).
//
//  Everything below was read out of the dumped D2RLoader 1.3.1 image (md5
//  8af7a47c...). Every site is checked byte for byte at load; a part whose
//  check fails installs nothing and the game runs that part stock.
//
// -----------------------------------------------------------------------------
//  WHY A TRANSMUTED UNIQUE NEVER COMPLETED ITS ENTRY
// -----------------------------------------------------------------------------
//  The item builder 0x43CD50 (every drop and every cube output) marks a Set or
//  Unique item "Chronicle pending" when the game's eligibility check passes:
//      43CF6E  feature check 0x3B06C0(context, 4)
//      43CF7E  quality 0x36CF60: 5 set, 7 unique; a unique also needs its
//              items.txt record byte +13Ah clear and 0x374870(item) == 0
//      43CFBB  row record 0x3E2B2E6(context, row) (D2RCore thunk), then
//              0x375F80(record + 2Ch)
//      43CFDD  0x36C7B0 attaches the item's Chronicle record (ItemData+0xD0)
//      43CFF0  0x36D8F0(item, 0x10000000, 1), the pending flag
//  The server writes a Chronicle entry only through 0x4247D0(client, item),
//  which accepts a pending item or a runeword (flags 0x10000000 | 0x4000000):
//      46EA4C  identifying an item (0x46E8C0), pending items only
//      46EEA3  a runeword completed by socketing (0x46EB00)
//      424970  identifying for every player in the game, pending items only
//  The cube generator 0x5269C0 places each output (527E1B), sets it identified
//  itself (527E52) and sends it to the client (527E93). Identification never
//  runs, so nothing ever writes the entry: the output stays pending forever.
//
// -----------------------------------------------------------------------------
//  1. COMPLETING ENTRIES: the item updates that hand an item to a player
// -----------------------------------------------------------------------------
//  0x535F60(game, player, item, ...) sends a placed item to its player. Three
//  of its calls are the moments an item comes into a player's possession:
//      471E69  pickup 0x471950 (packet 0x16), after placement on the cursor,
//              in the inventory, the belt or an equip slot
//      527E93  cube generator, each output after placement and identification
//      5280F6  cube generator, each socketed item a recipe took out
//  Each call goes through a slot on a stub page next to the image. The hook
//  runs the native update first, so the client already knows the item, then:
//      - unidentified: nothing, the game completes it when it is identified
//      - its entry already in the player's Chronicle (0x3D87D0 on 0x347440):
//        nothing
//      - pending or a runeword: 0x4247D0(client 0x48FDE0(player), item), the
//        game's own entry write, exactly as identification calls it
//      - otherwise, only if the builder's own eligibility check above passes:
//        the item gets the record and flag the builder gives a fresh drop
//        (0x36C7B0, 0x36D8F0, dropped-by 27229 through 0x36C480 as the builder
//        writes for an item without a source), then 0x4247D0
//  0x4247D0 writes the entry, sends packet 0x87 to the client and releases the
//  item's pending record, as it does after identification.
//
// -----------------------------------------------------------------------------
//  2. THE GROUND LABEL LINE
// -----------------------------------------------------------------------------
//      1FAA18  ground label builder 0x1FA9F0 calls the label text 0xCBEB0
//              (item, text, 128, color); the text is measured after it, so a
//              second line makes the label two lines tall
//      0C08C5  hovered unit name 0xC0420 calls the GetItemName thunk
//              0x3E2B634 (item, 1024-byte text), colours it at 0C08D4 and
//              copies 128 bytes at 0C08EC
//  Both calls go through the stub page. The hooks call the original targets,
//  so the loot filter's hooks on 0xCBEB0 and on the thunk keep working, then
//  append "\n" + colour + text when the item needs it:
//      - an item on the ground (mode 3 or 5)
//      - a set or unique (ItemData quality 5 or 7, row at +0x34) or a runeword
//        (row from the game's own Chronicle record builder 0x425F60)
//      - not in the local player's Chronicle (0x8B2D0, 0x9A480, 0x347440,
//        0x3D87D0)
//      - pending, a runeword, or passing the builder's eligibility check with
//        the item's own data context 0x34A0E0
//  The text is the string Key ChronicleTrackerLabel; the colour marker is
//  the one 0xCD6E0 writes, read once from it as the loot filter does.
//
//  Console command "chronicletracker" shows the state and the counts.
//  Settings: d2rloader/config/celestialrayone.chronicle-tracker.toml
// =============================================================================

#include <D2RLPlugin/api.h>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

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

#include "resource_ids.h"

namespace CelestialRayOne::ChronicleTracker {
namespace {

constexpr char PluginIdText[]  = "celestialrayone.chronicle-tracker";
constexpr char PluginVersion[] = "1.0.0";
constexpr char ConfigTable[]   = "chronicle-tracker";
constexpr char StringsPath[]   = "data/local/lng/strings/d2rloader/celestialrayone.chronicle-tracker/strings.json";
constexpr char LineKey[]       = "ChronicleTrackerLabel";
constexpr char FallbackLine[]  = "Not in Chronicle";

const D2RL::PluginContext* Context{};
std::uintptr_t             Base{};

// ---------------------------------------------------------------------------
// Native anchors
// ---------------------------------------------------------------------------

constexpr std::uint64_t SendItemUpdateRva     = 0x535F60;
constexpr std::uint64_t RegisterRva           = 0x4247D0;
constexpr std::uint64_t FillRecordRva         = 0x425F60;
constexpr std::uint64_t ChronicleOfRva        = 0x347440;
constexpr std::uint64_t LookupRva             = 0x3D87D0;
constexpr std::uint64_t ClientOfRva           = 0x48FDE0;
constexpr std::uint64_t FeatureEnabledRva     = 0x3B06C0;
constexpr std::uint64_t QualityRva            = 0x36CF60;
constexpr std::uint64_t UniqueExcludedRva     = 0x374870;
constexpr std::uint64_t FileIndexRva          = 0x36CE70;
constexpr std::uint64_t RowRecordRva          = 0x3E2B2E6;
constexpr std::uint64_t RowAllowedRva         = 0x375F80;
constexpr std::uint64_t InitRecordRva         = 0x36C7B0;
constexpr std::uint64_t SetItemFlagRva        = 0x36D8F0;
constexpr std::uint64_t SetDroppedByRva       = 0x36C480;
constexpr std::uint64_t ItemsRecordRva        = 0x314110;
constexpr std::uint64_t ItemContextRva        = 0x34A0E0;
constexpr std::uint64_t LocalIndexRva         = 0x08B2D0;
constexpr std::uint64_t LocalPlayerRva        = 0x09A480;
constexpr std::uint64_t ColorizeRva           = 0x0CD6E0;
constexpr std::uint64_t LabelTextRva          = 0x0CBEB0;
constexpr std::uint64_t GetItemNameThunkRva   = 0x3E2B634;

constexpr std::uint64_t RegisterWitnessRva        = 0x4247D0;
constexpr std::uint64_t EligibilityWitnessRva     = 0x43CF4D;
constexpr std::uint64_t NoSourceWitnessRva        = 0x43D195;
constexpr std::uint64_t ClientOfWitnessRva        = 0x48FDE0;
constexpr std::uint64_t PickupUpdateWitnessRva    = 0x471E5E;
constexpr std::uint64_t CubeOutputUpdateWitnessRva = 0x527E87;
constexpr std::uint64_t CubeSocketUpdateWitnessRva = 0x5280EA;
constexpr std::uint64_t LabelBuilderWitnessRva    = 0x1FA9F0;
constexpr std::uint64_t HoverNameWitnessRva       = 0x0C08BE;

constexpr std::uint64_t PickupUpdateCallRva     = 0x471E69;
constexpr std::uint64_t CubeOutputUpdateCallRva = 0x527E93;
constexpr std::uint64_t CubeSocketUpdateCallRva = 0x5280F6;
constexpr std::uint64_t LabelTextCallRva        = 0x1FAA18;
constexpr std::uint64_t HoverNameCallRva        = 0x0C08C5;

// Unit and item data, as the loot filter and the builder read them.
constexpr std::size_t UnitTypeOffset      = 0x00;
constexpr std::size_t UnitClassOffset     = 0x04;
constexpr std::size_t UnitModeOffset      = 0x0C;
constexpr std::size_t UnitDataOffset      = 0x10;
constexpr std::size_t ItemQualityOffset   = 0x00;
constexpr std::size_t ItemFlagsOffset     = 0x18;
constexpr std::size_t ItemRowOffset       = 0x34;
constexpr std::size_t GameContextOffset   = 0x106;
constexpr std::size_t ItemsRecordUniqueSkip = 0x13A;
constexpr std::size_t RowRecordFlags      = 0x2C;

constexpr std::uint32_t UnitPlayer = 0;
constexpr std::uint32_t UnitItem   = 4;
constexpr std::uint32_t ModeOnGround = 3;
constexpr std::uint32_t ModeDropping = 5;
constexpr std::uint32_t QualitySet    = 5;
constexpr std::uint32_t QualityUnique = 7;
constexpr std::uint32_t FlagIdentified       = 0x10;
constexpr std::uint32_t FlagRuneword         = 0x4000000;
constexpr std::uint32_t FlagChroniclePending = 0x10000000;
constexpr std::uint32_t ChronicleFeature     = 4;
constexpr std::uint32_t NoSourceString       = 27229;
constexpr std::uint8_t  CategorySet      = 0;
constexpr std::uint8_t  CategoryUnique   = 1;

// The record 0x425F60 fills (packet 0x87): row word +0x0A, category byte +0x0C.
constexpr std::size_t RecordRowOffset      = 0x0A;
constexpr std::size_t RecordCategoryOffset = 0x0C;

// ---------------------------------------------------------------------------
// Witnesses, each instruction-aligned and matched in full before any write
// ---------------------------------------------------------------------------

// register 0x4247D0, the whole body, 404 bytes at 0x4247D0
constexpr std::uint8_t RegisterWitness[]{
    0x48, 0x89, 0x5C, 0x24, 0x20, 0x57, 0x48, 0x83, 0xEC, 0x60, 0x48, 0x8B, 0x05, 0xE7, 0x6A, 0x5A,
    0x02, 0x48, 0x33, 0xC4, 0x48, 0x89, 0x44, 0x24, 0x50, 0x48, 0x8B, 0xDA, 0x48, 0x8B, 0xF9, 0x48,
    0x85, 0xC9, 0x74, 0x05, 0x48, 0x85, 0xD2, 0x75, 0x26, 0x48, 0x8D, 0x4C, 0x24, 0x30, 0xC6, 0x44,
    0x24, 0x30, 0x00, 0xE8, 0x08, 0xF8, 0xFF, 0xFF, 0x84, 0xC0, 0x74, 0x01, 0xCC, 0x48, 0x85, 0xFF,
    0x0F, 0x84, 0xF5, 0x00, 0x00, 0x00, 0x48, 0x85, 0xDB, 0x0F, 0x84, 0xEC, 0x00, 0x00, 0x00, 0x4C,
    0x8D, 0x0D, 0x4A, 0x42, 0x8F, 0x01, 0xBA, 0x00, 0x00, 0x00, 0x10, 0x41, 0xB8, 0x0D, 0x02, 0x00,
    0x00, 0x48, 0x8B, 0xCB, 0xE8, 0x97, 0x9A, 0xF4, 0xFF, 0x85, 0xC0, 0x75, 0x22, 0x4C, 0x8D, 0x0D,
    0x2C, 0x42, 0x8F, 0x01, 0xBA, 0x00, 0x00, 0x00, 0x04, 0x41, 0xB8, 0x0D, 0x02, 0x00, 0x00, 0x48,
    0x8B, 0xCB, 0xE8, 0x79, 0x9A, 0xF4, 0xFF, 0x85, 0xC0, 0x0F, 0x84, 0xAC, 0x00, 0x00, 0x00, 0x33,
    0xC0, 0x48, 0x89, 0xB4, 0x24, 0x80, 0x00, 0x00, 0x00, 0x0F, 0x57, 0xC0, 0x89, 0x44, 0x24, 0x48,
    0x48, 0x8D, 0x54, 0x24, 0x38, 0x88, 0x44, 0x24, 0x4C, 0x48, 0x8B, 0xCB, 0x0F, 0x11, 0x44, 0x24,
    0x38, 0xE8, 0xDA, 0x16, 0x00, 0x00, 0x33, 0xD2, 0x48, 0x8B, 0xCF, 0xE8, 0x00, 0x04, 0x06, 0x00,
    0x48, 0x8B, 0xF0, 0x48, 0x85, 0xC0, 0x75, 0x15, 0x48, 0x8D, 0x4C, 0x24, 0x30, 0x88, 0x44, 0x24,
    0x30, 0xE8, 0x9A, 0xF7, 0xFF, 0xFF, 0x84, 0xC0, 0x74, 0x59, 0xCC, 0xEB, 0x56, 0x48, 0x8B, 0xCE,
    0xE8, 0x8B, 0x2B, 0xF2, 0xFF, 0x48, 0x85, 0xC0, 0x75, 0x6C, 0x48, 0x8D, 0x4C, 0x24, 0x30, 0x88,
    0x44, 0x24, 0x30, 0xE8, 0x28, 0xF6, 0xFF, 0xFF, 0x84, 0xC0, 0x74, 0x01, 0xCC, 0x48, 0x8B, 0xCB,
    0xE8, 0xEB, 0x08, 0xF5, 0xFF, 0x41, 0xB8, 0x2A, 0x02, 0x00, 0x00, 0x48, 0x8D, 0x15, 0x8E, 0x41,
    0x8F, 0x01, 0x48, 0x8B, 0xCE, 0xE8, 0x46, 0x5A, 0xF2, 0xFF, 0x48, 0x8D, 0x54, 0x24, 0x38, 0x89,
    0x44, 0x24, 0x3A, 0x48, 0x8B, 0xCF, 0xE8, 0xB5, 0x35, 0x05, 0x00, 0x48, 0x8B, 0xCB, 0xE8, 0xBD,
    0x08, 0xF5, 0xFF, 0x48, 0x8B, 0xB4, 0x24, 0x80, 0x00, 0x00, 0x00, 0x48, 0x8B, 0x4C, 0x24, 0x50,
    0x48, 0x33, 0xCC, 0xE8, 0x38, 0xC8, 0xEA, 0x00, 0x48, 0x8B, 0x9C, 0x24, 0x88, 0x00, 0x00, 0x00,
    0x48, 0x83, 0xC4, 0x60, 0x5F, 0xC3, 0x44, 0x0F, 0xB7, 0x44, 0x24, 0x42, 0x48, 0x8B, 0xC8, 0x0F,
    0xB6, 0x54, 0x24, 0x44, 0xE8, 0x97, 0x3E, 0xFB, 0xFF, 0x48, 0x85, 0xC0, 0x75, 0x8F, 0x8B, 0x44,
    0x24, 0x45, 0x48, 0x8B, 0xCE, 0x44, 0x0F, 0xB7, 0x44, 0x24, 0x42, 0x44, 0x0F, 0xB7, 0x4C, 0x24,
    0x49, 0x0F, 0xB6, 0x54, 0x24, 0x44, 0x89, 0x44, 0x24, 0x20, 0xE8, 0x41, 0x28, 0xF2, 0xFF, 0xE9,
    0x71, 0xFF, 0xFF, 0xFF,
};

// item builder 0x43CD50, its Chronicle eligibility check, 168 bytes at 0x43CF4D
constexpr std::uint8_t EligibilityWitness[]{
    0x48, 0x69, 0x4D, 0x67, 0xC0, 0x01, 0x00, 0x00, 0x49, 0x8B, 0x04, 0x24, 0xBA, 0x04, 0x00, 0x00,
    0x00, 0x0F, 0xB6, 0x9C, 0x01, 0x3A, 0x01, 0x00, 0x00, 0x41, 0x0F, 0xB6, 0x8F, 0x06, 0x01, 0x00,
    0x00, 0xE8, 0x4D, 0x37, 0xF7, 0xFF, 0x84, 0xC0, 0x0F, 0x84, 0x3A, 0x02, 0x00, 0x00, 0x48, 0x8B,
    0xCF, 0xE8, 0xDD, 0xFF, 0xF2, 0xFF, 0x83, 0xF8, 0x05, 0x74, 0x21, 0x83, 0xF8, 0x07, 0x0F, 0x85,
    0x24, 0x02, 0x00, 0x00, 0x84, 0xDB, 0x0F, 0x85, 0x1C, 0x02, 0x00, 0x00, 0x48, 0x8B, 0xCF, 0xE8,
    0xCF, 0x78, 0xF3, 0xFF, 0x85, 0xC0, 0x0F, 0x85, 0x0C, 0x02, 0x00, 0x00, 0x48, 0x8B, 0xCF, 0xE8,
    0xBF, 0xFE, 0xF2, 0xFF, 0x41, 0x0F, 0xB6, 0x8F, 0x06, 0x01, 0x00, 0x00, 0x8B, 0xD0, 0xE8, 0x26,
    0xE3, 0x9E, 0x03, 0x48, 0x85, 0xC0, 0x0F, 0x84, 0xEC, 0x01, 0x00, 0x00, 0x48, 0x8D, 0x48, 0x2C,
    0xE8, 0xAE, 0x8F, 0xF3, 0xFF, 0x84, 0xC0, 0x0F, 0x84, 0xDB, 0x01, 0x00, 0x00, 0x48, 0x8B, 0xCF,
    0xE8, 0xCE, 0xF7, 0xF2, 0xFF, 0xBA, 0x00, 0x00, 0x00, 0x10, 0x41, 0xB8, 0x01, 0x00, 0x00, 0x00,
    0x48, 0x8B, 0xCF, 0xE8, 0xFB, 0x08, 0xF3, 0xFF,
};

// item builder 0x43CD50, dropped-by for an item with no source, 13 bytes at 0x43D195
constexpr std::uint8_t NoSourceWitness[]{
    0xBA, 0x5D, 0x6A, 0x00, 0x00, 0x48, 0x8B, 0xCF, 0xE8, 0xDE, 0xF2, 0xF2, 0xFF,
};

// client of a player 0x48FDE0, 23 bytes at 0x48FDE0
constexpr std::uint8_t ClientOfWitness[]{
    0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B, 0xD9, 0x48, 0x85, 0xC9, 0x74, 0x1E, 0xE8, 0xDD,
    0xBB, 0xEB, 0xFF, 0x85, 0xC0, 0x75, 0x15,
};

// pickup 0x471950, the item update after placement, 16 bytes at 0x471E5E
constexpr std::uint8_t PickupUpdateWitness[]{
    0x8B, 0x45, 0xDF, 0x89, 0x44, 0x24, 0x28, 0x89, 0x5C, 0x24, 0x20, 0xE8, 0xF2, 0x40, 0x0C, 0x00,
};

// cube generator 0x5269C0, the item update of an output, 17 bytes at 0x527E87
constexpr std::uint8_t CubeOutputUpdateWitness[]{
    0x89, 0x44, 0x24, 0x28, 0xC7, 0x44, 0x24, 0x20, 0x02, 0x00, 0x00, 0x00, 0xE8, 0xC8, 0xE0, 0x00,
    0x00,
};

// cube generator 0x5269C0, the item update of a socketed item a recipe took out, 17 bytes at 0x5280EA
constexpr std::uint8_t CubeSocketUpdateWitness[]{
    0x89, 0x44, 0x24, 0x28, 0xC7, 0x44, 0x24, 0x20, 0x02, 0x00, 0x00, 0x00, 0xE8, 0x65, 0xDE, 0x00,
    0x00,
};

// ground label builder 0x1FA9F0, up to its label text call, 45 bytes at 0x1FA9F0
constexpr std::uint8_t LabelBuilderWitness[]{
    0x40, 0x55, 0x53, 0x56, 0x41, 0x54, 0x41, 0x57, 0x48, 0x8B, 0xEC, 0x48, 0x83, 0xEC, 0x50, 0x45,
    0x8B, 0xE1, 0xC6, 0x02, 0x00, 0x49, 0x8B, 0xF0, 0x4C, 0x8D, 0x4D, 0xF0, 0x41, 0xB8, 0x80, 0x00,
    0x00, 0x00, 0x48, 0x8B, 0xDA, 0x4C, 0x8B, 0xF9, 0xE8, 0x93, 0x14, 0xED, 0xFF,
};

// hovered unit name 0xC0420, the item name and its colouring, 27 bytes at 0xC08BE
constexpr std::uint8_t HoverNameWitness[]{
    0x48, 0x8D, 0x95, 0xE0, 0x01, 0x00, 0x00, 0xE8, 0x6A, 0xAD, 0xD6, 0x03, 0x41, 0x8B, 0xD4, 0x48,
    0x8D, 0x8D, 0xE0, 0x01, 0x00, 0x00, 0xE8, 0x07, 0xCE, 0x00, 0x00,
};
// colouring 0xCD6E0, 39 bytes (as the loot filter checks it)
constexpr std::uint8_t ColorizeWitness[]{
    0x48, 0x89, 0x5C, 0x24, 0x18, 0x57, 0x48, 0x81, 0xEC, 0x50, 0x04, 0x00, 0x00, 0x48, 0x8B, 0x05,
    0xD4, 0xDB, 0x8F, 0x02, 0x48, 0x33, 0xC4, 0x48, 0x89, 0x84, 0x24, 0x40, 0x04, 0x00, 0x00, 0x80,
    0x39, 0x00, 0x8B, 0xFA, 0x48, 0x8B, 0xD9,
};
// local player index 0x8B2D0 and local player 0x9A480 (as the loot filter checks them)
constexpr std::uint8_t LocalIndexWitness[]{ 0x8B, 0x05, 0x2E, 0x84, 0x99, 0x02, 0xC3 };
constexpr std::uint8_t LocalPlayerWitness[]{
    0x48, 0x89, 0x5C, 0x24, 0x08, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x83, 0xF9, 0x08, 0x0F, 0x83, 0x85,
    0x00, 0x00, 0x00, 0x8B, 0xD9, 0x48, 0x89, 0x5C, 0x24, 0x38, 0x48, 0x83, 0xFB, 0x08, 0x72, 0x19,
};
// item data context 0x34A0E0 and items.txt record 0x314110 (as the loot filter checks them)
constexpr std::uint8_t ItemContextWitness[]{
    0x48, 0x83, 0xEC, 0x28, 0x48, 0x85, 0xC9, 0x75, 0x1A, 0x88, 0x4C, 0x24, 0x30, 0x48, 0x8D, 0x4C,
};
constexpr std::uint8_t ItemsRecordWitness[]{
    0x40, 0x57, 0x48, 0x83, 0xEC, 0x30, 0x8B, 0xFA, 0xE8, 0x73, 0xC9, 0xFE, 0xFF, 0x3B, 0xB8, 0xA8,
};

struct Witness {
    std::uint64_t       rva;
    const std::uint8_t* bytes;
    std::uint32_t       size;
    const char*         what;
};

#define CC_WITNESS(rva, bytes, what) Witness{ rva, bytes, static_cast<std::uint32_t>(sizeof(bytes)), what }

constexpr std::array<Witness, 8> ServerWitnesses{ {
    CC_WITNESS(RegisterWitnessRva, RegisterWitness, "Chronicle entry write"),
    CC_WITNESS(EligibilityWitnessRva, EligibilityWitness, "builder eligibility check"),
    CC_WITNESS(ItemsRecordRva, ItemsRecordWitness, "items.txt record"),
    CC_WITNESS(NoSourceWitnessRva, NoSourceWitness, "builder dropped-by"),
    CC_WITNESS(ClientOfWitnessRva, ClientOfWitness, "client of a player"),
    CC_WITNESS(PickupUpdateWitnessRva, PickupUpdateWitness, "pickup item update"),
    CC_WITNESS(CubeOutputUpdateWitnessRva, CubeOutputUpdateWitness, "cube output item update"),
    CC_WITNESS(CubeSocketUpdateWitnessRva, CubeSocketUpdateWitness, "cube socketed item update"),
} };

constexpr std::array<Witness, 9> ClientWitnesses{ {
    CC_WITNESS(RegisterWitnessRva, RegisterWitness, "Chronicle entry write (record builder, lookup)"),
    CC_WITNESS(EligibilityWitnessRva, EligibilityWitness, "builder eligibility check"),
    CC_WITNESS(LabelBuilderWitnessRva, LabelBuilderWitness, "ground label builder"),
    CC_WITNESS(HoverNameWitnessRva, HoverNameWitness, "hovered item name"),
    CC_WITNESS(ColorizeRva, ColorizeWitness, "text colouring"),
    CC_WITNESS(LocalIndexRva, LocalIndexWitness, "local player index"),
    CC_WITNESS(LocalPlayerRva, LocalPlayerWitness, "local player"),
    CC_WITNESS(ItemContextRva, ItemContextWitness, "item data context"),
    CC_WITNESS(ItemsRecordRva, ItemsRecordWitness, "items.txt record"),
} };

// ---------------------------------------------------------------------------
// Native functions
// ---------------------------------------------------------------------------

using SendItemUpdateFn  = std::uint64_t(__fastcall*)(void* game, void* player, void* item, std::uint64_t a4,
                                                     std::uint64_t a5, std::uint64_t a6, std::uint64_t a7,
                                                     std::uint64_t a8, std::uint64_t a9);
using RegisterFn        = std::uint64_t(__fastcall*)(void* client, void* item);
using FillRecordFn      = std::uint64_t(__fastcall*)(void* item, void* record);
using ChronicleOfFn     = void*(__fastcall*)(void* player);
using LookupFn          = void*(__fastcall*)(void* chronicle, std::uint8_t category, std::uint32_t row);
using ClientOfFn        = void*(__fastcall*)(void* player);
using FeatureEnabledFn  = std::uint8_t(__fastcall*)(std::uint8_t context, std::uint32_t feature);
using ItemValueFn       = std::uint32_t(__fastcall*)(void* item);
using RowRecordFn       = const std::uint8_t*(__fastcall*)(std::uint8_t context, std::uint32_t row);
using RowAllowedFn      = std::uint8_t(__fastcall*)(const void* flags);
using InitRecordFn      = void(__fastcall*)(void* item);
using SetItemFlagFn     = void(__fastcall*)(void* item, std::uint32_t flag, std::uint32_t value);
using SetDroppedByFn    = void(__fastcall*)(void* item, std::uint32_t stringId);
using ItemsRecordFn     = const std::uint8_t*(__fastcall*)(std::uint8_t context, std::uint32_t classId);
using ItemContextFn     = std::uint8_t(__fastcall*)(void* unit);
using LocalIndexFn      = std::uint32_t(__fastcall*)();
using LocalPlayerFn     = void*(__fastcall*)(std::uint32_t index);
using ColorizeFn        = char*(__fastcall*)(char* buffer, std::int32_t color);
using LabelTextFn       = void(__fastcall*)(void* item, char* buffer, std::int32_t size, std::uint32_t* color);
using GetItemNameFn     = void*(__fastcall*)(void* item, char* buffer);

SendItemUpdateFn NativeSendItemUpdate{};
RegisterFn       NativeRegister{};
FillRecordFn     NativeFillRecord{};
ChronicleOfFn    NativeChronicleOf{};
LookupFn         NativeLookup{};
ClientOfFn       NativeClientOf{};
FeatureEnabledFn NativeFeatureEnabled{};
ItemValueFn      NativeQuality{};
ItemValueFn      NativeUniqueExcluded{};
ItemValueFn      NativeFileIndex{};
RowRecordFn      NativeRowRecord{};
RowAllowedFn     NativeRowAllowed{};
InitRecordFn     NativeInitRecord{};
SetItemFlagFn    NativeSetItemFlag{};
SetDroppedByFn   NativeSetDroppedBy{};
ItemsRecordFn    NativeItemsRecord{};
ItemContextFn    NativeItemContext{};
LocalIndexFn     NativeLocalIndex{};
LocalPlayerFn    NativeLocalPlayer{};
ColorizeFn       NativeColorize{};
LabelTextFn      NativeLabelText{};
GetItemNameFn    NativeGetItemName{};

template <typename T>
auto At(std::uint64_t rva) noexcept -> T {
    return reinterpret_cast<T>(Base + static_cast<std::uintptr_t>(rva));
}

template <typename T>
auto Read(const void* base, std::size_t offset) noexcept -> T {
    T value{};
    std::memcpy(&value, static_cast<const std::uint8_t*>(base) + offset, sizeof(T));
    return value;
}

void BindNatives() noexcept {
    NativeSendItemUpdate = At<SendItemUpdateFn>(SendItemUpdateRva);
    NativeRegister       = At<RegisterFn>(RegisterRva);
    NativeFillRecord     = At<FillRecordFn>(FillRecordRva);
    NativeChronicleOf    = At<ChronicleOfFn>(ChronicleOfRva);
    NativeLookup         = At<LookupFn>(LookupRva);
    NativeClientOf       = At<ClientOfFn>(ClientOfRva);
    NativeFeatureEnabled = At<FeatureEnabledFn>(FeatureEnabledRva);
    NativeQuality        = At<ItemValueFn>(QualityRva);
    NativeUniqueExcluded = At<ItemValueFn>(UniqueExcludedRva);
    NativeFileIndex      = At<ItemValueFn>(FileIndexRva);
    NativeRowRecord      = At<RowRecordFn>(RowRecordRva);
    NativeRowAllowed     = At<RowAllowedFn>(RowAllowedRva);
    NativeInitRecord     = At<InitRecordFn>(InitRecordRva);
    NativeSetItemFlag    = At<SetItemFlagFn>(SetItemFlagRva);
    NativeSetDroppedBy   = At<SetDroppedByFn>(SetDroppedByRva);
    NativeItemsRecord    = At<ItemsRecordFn>(ItemsRecordRva);
    NativeItemContext    = At<ItemContextFn>(ItemContextRva);
    NativeLocalIndex     = At<LocalIndexFn>(LocalIndexRva);
    NativeLocalPlayer    = At<LocalPlayerFn>(LocalPlayerRva);
    NativeColorize       = At<ColorizeFn>(ColorizeRva);
    NativeLabelText      = At<LabelTextFn>(LabelTextRva);
    NativeGetItemName    = At<GetItemNameFn>(GetItemNameThunkRva);
}

// ---------------------------------------------------------------------------
// Settings
// ---------------------------------------------------------------------------

// The default config, written to d2rloader/config/celestialrayone.chronicle-tracker.toml on first load.
constexpr char DefaultConfig[] = R"toml(# celestialrayone.chronicle-tracker
#
# Chronicle Tracker
#
# 1. Completing entries
#   A Set item, Unique item or Runeword completes its Chronicle entry the moment
#   the character comes into possession of it already identified:
#     - picked up from the ground, to the cursor or straight into the inventory
#     - received from a Horadric Cube transmute, including socketed items a
#       recipe takes out of an item
#   The game itself completes an entry only when an item gets identified, or
#   when a Runeword is made by socketing. A transmute output arrives already
#   identified, so the game never completed it. That was the cube bug.
#   Unidentified items stay with the game: it completes them when they get
#   identified, as before.
#   The entry is written by the game's own routine, the same one identifying an
#   item uses, and is saved with the shared stash like any other entry.
#
# 2. The ground label
#   A Set item, Unique item or Runeword on the ground whose entry this
#   character has not completed gets a second line under its label:
#       Shako
#       Not in Chronicle
#   The line shows on the Alt labels and on the name shown while hovering the
#   item. An unidentified item keeps its base name, so the line never tells
#   which Set or Unique item it is.
#
# Which items count
#   Exactly the items the game itself puts in the Chronicle: the plugin runs
#   the check the game runs on every new drop. An item the game keeps out of
#   the Chronicle never gets the line and never gets an entry.
#
# Text
#   The line is the string Key ChronicleTrackerLabel, shipped in every
#   language. To reword it, add the same Key to the mod's string files.
#
# Console command
#   chronicletracker   installation state and the entries completed this
#                         session

[chronicle-tracker]
# The whole plugin. false installs nothing.
enabled = true

# Part 1: complete the entry on pickup and on Horadric Cube outputs.
complete_on_acquire = true

# Part 2: the "Not in Chronicle" line under ground labels.
mark_ground_labels = true

# Color of that line, as a color code from the string files.
label_color = ")toml" "\xC3\xBF" R"toml(c1"
)toml";

struct Settings {
    bool enabled           = true;
    bool completeOnAcquire = true;
    bool markGroundLabels  = true;
    char labelColor        = '1';
};

Settings    Config;
std::string ConfigProblems;

auto Embedded(int id, std::string_view& text) noexcept -> bool {
    HMODULE module{};
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCWSTR>(&Embedded), &module)) {
        return false;
    }
    HRSRC       found  = FindResourceW(module, MAKEINTRESOURCEW(id), MAKEINTRESOURCEW(10));   // RT_RCDATA
    HGLOBAL     loaded = found != nullptr ? LoadResource(module, found) : nullptr;
    const void* bytes  = loaded != nullptr ? LockResource(loaded) : nullptr;
    const DWORD size   = found != nullptr ? SizeofResource(module, found) : 0;
    if (bytes == nullptr || size == 0) return false;
    text = std::string_view(static_cast<const char*>(bytes), size);
    return true;
}

auto Trim(std::string_view text) -> std::string_view {
    while (!text.empty() && (text.front() == ' ' || text.front() == '\t' || text.front() == '\r')) text.remove_prefix(1);
    while (!text.empty() && (text.back() == ' ' || text.back() == '\t' || text.back() == '\r')) text.remove_suffix(1);
    return text;
}

// The colour code of label_color: the string files' colour code (C3 BF 63 31 for red) or just "1".
auto ColorFromText(std::string_view text, char& out) -> bool {
    static constexpr std::string_view code = "\xC3\xBF" "c";
    if (text.size() >= code.size() + 1 && text.substr(0, code.size()) == code) text.remove_prefix(code.size());
    if (text.size() != 1 || static_cast<unsigned char>(text[0]) < 0x21 || static_cast<unsigned char>(text[0]) > 0x7E) return false;
    out = text[0];
    return true;
}

void ParseConfig(std::string_view text) {
    std::string table;
    int         lineNumber = 0;
    while (!text.empty()) {
        const std::size_t end  = text.find('\n');
        std::string_view  line = text.substr(0, end);
        text = end == std::string_view::npos ? std::string_view{} : text.substr(end + 1);
        ++lineNumber;
        bool        quoted = false;
        std::size_t cut    = line.size();
        for (std::size_t i = 0; i < line.size(); ++i) {
            if (line[i] == '"') quoted = !quoted;
            if (line[i] == '#' && !quoted) { cut = i; break; }
        }
        line = Trim(line.substr(0, cut));
        if (line.empty()) continue;
        if (line.front() == '[') {
            table.assign(Trim(line.substr(1, line.find(']') == std::string_view::npos ? std::string_view::npos : line.find(']') - 1)));
            continue;
        }
        if (table != ConfigTable) continue;
        const std::size_t equals = line.find('=');
        if (equals == std::string_view::npos) continue;
        const std::string_view key   = Trim(line.substr(0, equals));
        std::string_view       value = Trim(line.substr(equals + 1));
        const auto boolean = [&](bool& out) {
            if (value == "true") out = true;
            else if (value == "false") out = false;
            else ConfigProblems += "line " + std::to_string(lineNumber) + ": " + std::string(key) + " expects true or false. ";
        };
        if (key == "enabled") boolean(Config.enabled);
        else if (key == "complete_on_acquire") boolean(Config.completeOnAcquire);
        else if (key == "mark_ground_labels") boolean(Config.markGroundLabels);
        else if (key == "label_color") {
            if (value.size() >= 2 && value.front() == '"' && value.back() == '"') value = value.substr(1, value.size() - 2);
            char color = 0;
            if (ColorFromText(value, color)) Config.labelColor = color;
            else ConfigProblems += "line " + std::to_string(lineNumber) + ": label_color expects a colour code such as \"\xC3\xBF" "c1\". ";
        } else {
            ConfigProblems += "line " + std::to_string(lineNumber) + ": unknown setting " + std::string(key) + ". ";
        }
    }
}

void ReadConfiguration() {
    const std::string defaults = DefaultConfig;
    if (!Context->EnsureConfig(defaults.c_str())) {
        Context->LogWarn("ChronicleTracker: the config file could not be created; using the defaults.");
    }
    std::string   buffer(64 * 1024, '\0');
    std::uint32_t required = 0;
    if (!Context->ReadConfig(buffer.data(), static_cast<std::uint32_t>(buffer.size()), &required)) {
        if (required > buffer.size() && required < 4u * 1024 * 1024) {
            buffer.assign(required + 1, '\0');
            if (!Context->ReadConfig(buffer.data(), static_cast<std::uint32_t>(buffer.size()), nullptr)) buffer.clear();
        } else {
            buffer.clear();
        }
    }
    if (buffer.empty()) {
        Context->LogWarn("ChronicleTracker: the config file could not be read; using the defaults.");
        buffer = defaults;
    }
    buffer.resize(std::strlen(buffer.c_str()));
    ParseConfig(buffer);
    if (!ConfigProblems.empty()) D2RL::LogWarnF(Context, "ChronicleTracker: config: %s", ConfigProblems.c_str());
}

// ---------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------

const D2RL::LocalizationService*    LocalizationSvc{};
const D2RL::ResourceService*        ResourceSvc{};
D2RL::Resources::RegistrationHandle StringsHandle{};

std::atomic<bool> CompleteActive{};
std::atomic<bool> LabelsActive{};
std::string       CompleteStatus = "not installed";
std::string       LabelsStatus   = "not installed";
std::string       VerifyFailures;

std::atomic<std::uint64_t> CompletedPickup{};
std::atomic<std::uint64_t> CompletedCube{};
std::atomic<std::uint64_t> CompletedMarked{};   // of those, items the plugin first marked pending
std::atomic<std::uint64_t> SkippedIneligible{};

// ---------------------------------------------------------------------------
// The game's own Chronicle rules
// ---------------------------------------------------------------------------

struct Entry {
    std::uint8_t  category = 0;
    std::uint32_t row      = 0;
    bool          runeword = false;
    bool          pending  = false;
};

// Category and row exactly as 0x4247D0 keys the entry; false for anything that
// is not a set item, a unique item or a runeword.
auto EntryOf(void* item, Entry& entry) noexcept -> bool {
    if (item == nullptr || Read<std::uint32_t>(item, UnitTypeOffset) != UnitItem) return false;
    const void* data = Read<const void*>(item, UnitDataOffset);
    if (data == nullptr) return false;
    const auto flags = Read<std::uint32_t>(data, ItemFlagsOffset);
    entry.runeword = (flags & FlagRuneword) != 0;
    entry.pending  = (flags & FlagChroniclePending) != 0;
    if (entry.runeword) {
        std::uint8_t record[32]{};
        NativeFillRecord(item, record);
        entry.category = record[RecordCategoryOffset];
        entry.row      = Read<std::uint16_t>(record, RecordRowOffset);
        return true;
    }
    const auto quality = Read<std::uint32_t>(data, ItemQualityOffset);
    if (quality == QualityUnique) entry.category = CategoryUnique;
    else if (quality == QualitySet) entry.category = CategorySet;
    else return false;
    entry.row = Read<std::uint32_t>(data, ItemRowOffset) & 0xFFFFu;
    return true;
}

// The builder's eligibility check (0x43CF4D..0x43CFDA), run for this item.
auto Eligible(void* item, std::uint8_t context) noexcept -> bool {
    if (NativeFeatureEnabled(context, ChronicleFeature) == 0) return false;
    const auto quality = NativeQuality(item);
    if (quality == QualityUnique) {
        const std::uint8_t* record = NativeItemsRecord(context, Read<std::uint32_t>(item, UnitClassOffset));
        if (record == nullptr || record[ItemsRecordUniqueSkip] != 0) return false;
        if (NativeUniqueExcluded(item) != 0) return false;
    } else if (quality != QualitySet) {
        return false;
    }
    const std::uint8_t* row = NativeRowRecord(context, NativeFileIndex(item));
    return row != nullptr && NativeRowAllowed(row + RowRecordFlags) != 0;
}

auto ChronicleOf(void* player) noexcept -> void* {
    if (player == nullptr || Read<std::uint32_t>(player, UnitTypeOffset) != UnitPlayer) return nullptr;
    if (Read<void*>(player, UnitDataOffset) == nullptr) return nullptr;
    return NativeChronicleOf(player);
}

// ---------------------------------------------------------------------------
// 1. Completing entries
// ---------------------------------------------------------------------------

enum class Source { Pickup, Cube };

void CompleteAcquired(void* game, void* player, void* item, Source source) noexcept {
    if (game == nullptr || item == nullptr) return;
    void* chronicle = ChronicleOf(player);
    if (chronicle == nullptr) return;
    const void* data = Read<const void*>(item, UnitDataOffset);
    if (Read<std::uint32_t>(item, UnitTypeOffset) != UnitItem || data == nullptr) return;
    if ((Read<std::uint32_t>(data, ItemFlagsOffset) & FlagIdentified) == 0) return;
    Entry entry;
    if (!EntryOf(item, entry)) return;
    if (NativeLookup(chronicle, entry.category, entry.row) != nullptr) return;
    bool marked = false;
    if (!entry.runeword && !entry.pending) {
        if (!Eligible(item, Read<std::uint8_t>(game, GameContextOffset))) {
            SkippedIneligible.fetch_add(1);
            return;
        }
        NativeInitRecord(item);
        NativeSetItemFlag(item, FlagChroniclePending, 1);
        NativeSetDroppedBy(item, NoSourceString);
        marked = true;
    }
    void* client = NativeClientOf(player);
    if (client == nullptr) return;
    NativeRegister(client, item);
    (source == Source::Pickup ? CompletedPickup : CompletedCube).fetch_add(1);
    if (marked) CompletedMarked.fetch_add(1);
}

auto UpdateThen(Source source, void* game, void* player, void* item, std::uint64_t a4, std::uint64_t a5,
                std::uint64_t a6, std::uint64_t a7, std::uint64_t a8, std::uint64_t a9) noexcept -> std::uint64_t {
    const std::uint64_t result = NativeSendItemUpdate(game, player, item, a4, a5, a6, a7, a8, a9);
    if (CompleteActive.load(std::memory_order_relaxed)) CompleteAcquired(game, player, item, source);
    return result;
}

std::uint64_t __fastcall HookPickupUpdate(void* game, void* player, void* item, std::uint64_t a4, std::uint64_t a5,
                                          std::uint64_t a6, std::uint64_t a7, std::uint64_t a8, std::uint64_t a9) noexcept {
    return UpdateThen(Source::Pickup, game, player, item, a4, a5, a6, a7, a8, a9);
}

std::uint64_t __fastcall HookCubeUpdate(void* game, void* player, void* item, std::uint64_t a4, std::uint64_t a5,
                                        std::uint64_t a6, std::uint64_t a7, std::uint64_t a8, std::uint64_t a9) noexcept {
    return UpdateThen(Source::Cube, game, player, item, a4, a5, a6, a7, a8, a9);
}

// ---------------------------------------------------------------------------
// 2. The ground label line
// ---------------------------------------------------------------------------

auto NeedsLine(void* item) noexcept -> bool {
    if (item == nullptr || Read<std::uint32_t>(item, UnitTypeOffset) != UnitItem) return false;
    const auto mode = Read<std::uint32_t>(item, UnitModeOffset);
    if (mode != ModeOnGround && mode != ModeDropping) return false;
    Entry entry;
    if (!EntryOf(item, entry)) return false;
    void* chronicle = ChronicleOf(NativeLocalPlayer(NativeLocalIndex()));
    if (chronicle == nullptr) return false;
    if (NativeLookup(chronicle, entry.category, entry.row) != nullptr) return false;
    if (entry.runeword || entry.pending) return true;
    return Eligible(item, NativeItemContext(item));
}

auto Localized(const char* key, std::string& out) noexcept -> bool {
    if (LocalizationSvc == nullptr) return false;
    char          buffer[256];
    std::uint32_t required = 0;
    buffer[0] = '\0';
    const auto result = LocalizationSvc->getStringByKey(Context, key, buffer, sizeof(buffer), &required);
    if (result != D2RL::Localization::Result::Success || buffer[0] == '\0') return false;
    out = buffer;
    return true;
}

// The mod's own strings first, then the plugin's strings.json.
auto LineText() -> std::string {
    std::string text;
    const std::string native = std::string("d2r:") + LineKey;
    if (Localized(native.c_str(), text)) return text;
    if (Localized(LineKey, text)) return text;
    return FallbackLine;
}

std::mutex  LineLock;
std::string ColorMarker;
std::string CachedLine;
ULONGLONG   CachedAt{};

// The game's colour marker, read once from the game's own colouring function.
// Called with LineLock held.
auto Marker() -> const std::string& {
    if (ColorMarker.empty()) {
        char buffer[1024]{ 'x', '\0' };
        NativeColorize(buffer, 0);
        const std::size_t length = std::strlen(buffer);
        if (length > 2 && buffer[length - 1] == 'x' && buffer[length - 2] == '0') ColorMarker.assign(buffer, length - 2);
    }
    return ColorMarker;
}

// "\n" + colour + text, rebuilt every two seconds so a language change shows.
// Called with LineLock held.
auto Line() -> const std::string& {
    const ULONGLONG now = GetTickCount64();
    if (!CachedLine.empty() && now - CachedAt < 2000) return CachedLine;
    const std::string& marker = Marker();
    std::string        text   = LineText();
    if (!marker.empty()) {
        static const std::string code = "\xC3\xBF" "c";
        for (std::size_t at = text.find(code); at != std::string::npos; at = text.find(code, at + marker.size())) {
            text.replace(at, code.size(), marker);
        }
    }
    std::string line = "\n";
    if (!marker.empty()) {
        line += marker;
        line += Config.labelColor;
    }
    line += text;
    CachedLine = std::move(line);
    CachedAt   = now;
    return CachedLine;
}

// Appends the line when it fits. colouredAfter: the caller still puts the
// colour marker and one character in front of the whole text.
void AppendLine(char* buffer, std::size_t capacity, bool colouredAfter) {
    std::lock_guard<std::mutex> lock(LineLock);
    const std::string& line    = Line();
    const std::size_t  reserve = colouredAfter ? Marker().size() + 1 : 0;
    const std::size_t  length  = strnlen(buffer, capacity);
    if (length == 0 || length >= capacity || length + line.size() + reserve + 1 > capacity) return;
    std::memcpy(buffer + length, line.data(), line.size());
    buffer[length + line.size()] = '\0';
}

void __fastcall HookLabelText(void* item, char* buffer, std::int32_t size, std::uint32_t* color) noexcept {
    NativeLabelText(item, buffer, size, color);
    if (!LabelsActive.load(std::memory_order_relaxed) || buffer == nullptr || size <= 0) return;
    try {
        if (NeedsLine(item)) AppendLine(buffer, static_cast<std::size_t>(size), false);
    } catch (...) {
    }
}

// The hover name is coloured after this (marker + one character in front) and
// then copied into 128 bytes, so the line is added only when all of it fits.
constexpr std::size_t HoverCopyBytes = 128;

void* __fastcall HookHoverName(void* item, char* buffer) noexcept {
    void* result = NativeGetItemName(item, buffer);
    if (!LabelsActive.load(std::memory_order_relaxed) || buffer == nullptr) return result;
    try {
        if (NeedsLine(item)) AppendLine(buffer, HoverCopyBytes, true);
    } catch (...) {
    }
    return result;
}

// ---------------------------------------------------------------------------
// The stub page next to the image and the call sites
// ---------------------------------------------------------------------------

std::uint8_t*         StubPage{};
constexpr std::size_t StubPageBytes = 0x1000;

enum Slot : std::size_t { SlotPickup = 0, SlotCube = 16, SlotLabel = 32, SlotHover = 48 };

struct CallSite {
    std::uint64_t               rva;
    std::uint64_t               nativeTarget;
    std::size_t                 slot;
    const char*                 what;
    std::array<std::uint8_t, 5> original{};
    std::array<std::uint8_t, 5> written{};
    bool                        applied = false;
};

std::array<CallSite, 3> ServerSites{ {
    { PickupUpdateCallRva, SendItemUpdateRva, SlotPickup, "pickup" },
    { CubeOutputUpdateCallRva, SendItemUpdateRva, SlotCube, "cube output" },
    { CubeSocketUpdateCallRva, SendItemUpdateRva, SlotCube, "cube socketed item" },
} };

std::array<CallSite, 2> ClientSites{ {
    { LabelTextCallRva, LabelTextRva, SlotLabel, "ground label" },
    { HoverNameCallRva, GetItemNameThunkRva, SlotHover, "hover name" },
} };

auto ReadU32(const void* base, std::size_t offset) noexcept -> std::uint32_t {
    return Read<std::uint32_t>(base, offset);
}

auto AllocateStubPage() noexcept -> std::uint8_t* {
    const auto* image     = reinterpret_cast<const std::uint8_t*>(Base);
    const auto  ntOffset  = static_cast<std::size_t>(ReadU32(image, 0x3C));
    const auto  imageSize = ReadU32(image, ntOffset + 0x50);
    SYSTEM_INFO systemInfo{};
    GetSystemInfo(&systemInfo);
    const std::uintptr_t granularity = systemInfo.dwAllocationGranularity != 0 ? systemInfo.dwAllocationGranularity : 0x10000;
    const auto alignUp = [granularity](std::uintptr_t value) noexcept { return (value + granularity - 1) & ~(granularity - 1); };
    const std::uintptr_t limit = Base + HoverNameCallRva + 0x7FF00000ULL;   // the lowest call site
    std::uintptr_t       address = alignUp(Base + imageSize);
    while (address + StubPageBytes < limit) {
        MEMORY_BASIC_INFORMATION info{};
        if (VirtualQuery(reinterpret_cast<LPCVOID>(address), &info, sizeof(info)) == 0) break;
        const auto regionEnd = reinterpret_cast<std::uintptr_t>(info.BaseAddress) + info.RegionSize;
        if (info.State == MEM_FREE && address + StubPageBytes <= regionEnd) {
            if (auto* page = VirtualAlloc(reinterpret_cast<LPVOID>(address), StubPageBytes, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE)) {
                return static_cast<std::uint8_t*>(page);
            }
        }
        const auto next = alignUp(regionEnd);
        if (next <= address) break;
        address = next;
    }
    return nullptr;
}

// One absolute jump per 16-byte slot, all written before the page turns
// executable, so no slot changes while the game may run another.
auto PrepareStubPage() noexcept -> bool {
    StubPage = AllocateStubPage();
    if (StubPage == nullptr) return false;
    std::memset(StubPage, 0xCC, StubPageBytes);
    const auto put = [](std::size_t offset, const void* target) {
        const std::uint8_t jump[6]{ 0xFF, 0x25, 0x00, 0x00, 0x00, 0x00 };
        std::memcpy(StubPage + offset, jump, sizeof(jump));
        const auto address = reinterpret_cast<std::uint64_t>(target);
        std::memcpy(StubPage + offset + sizeof(jump), &address, sizeof(address));
    };
    put(SlotPickup, reinterpret_cast<const void*>(&HookPickupUpdate));
    put(SlotCube, reinterpret_cast<const void*>(&HookCubeUpdate));
    put(SlotLabel, reinterpret_cast<const void*>(&HookLabelText));
    put(SlotHover, reinterpret_cast<const void*>(&HookHoverName));
    DWORD previous = 0;
    if (!VirtualProtect(StubPage, StubPageBytes, PAGE_EXECUTE_READ, &previous)) {
        VirtualFree(StubPage, 0, MEM_RELEASE);
        StubPage = nullptr;
        return false;
    }
    FlushInstructionCache(GetCurrentProcess(), StubPage, StubPageBytes);
    return true;
}

auto CallBytes(std::uintptr_t from, std::uintptr_t to, std::array<std::uint8_t, 5>& out) noexcept -> bool {
    const auto rel = static_cast<std::int64_t>(to) - static_cast<std::int64_t>(from + 5);
    if (rel < INT32_MIN || rel > INT32_MAX) return false;
    const auto rel32 = static_cast<std::int32_t>(rel);
    out[0] = 0xE8;
    std::memcpy(out.data() + 1, &rel32, sizeof(rel32));
    return true;
}

auto PrepareSite(CallSite& site) noexcept -> bool {
    const std::uintptr_t from = Base + static_cast<std::uintptr_t>(site.rva);
    return CallBytes(from, Base + static_cast<std::uintptr_t>(site.nativeTarget), site.original)
        && CallBytes(from, reinterpret_cast<std::uintptr_t>(StubPage + site.slot), site.written)
        && Context->CheckExpectedBytes(site.rva, site.original.data(), 5);
}

auto ApplySite(CallSite& site) noexcept -> bool {
    if (!Context->PatchBytes(site.rva, site.original.data(), 5, site.written.data(), 5)) return false;
    site.applied = std::memcmp(reinterpret_cast<const void*>(Base + static_cast<std::uintptr_t>(site.rva)), site.written.data(), 5) == 0;
    return site.applied;
}

void RestoreSite(CallSite& site) noexcept {
    if (!site.applied) return;
    if (Context->PatchBytes(site.rva, site.written.data(), 5, site.original.data(), 5)) {
        site.applied = false;
    } else {
        D2RL::LogErrorF(Context, "ChronicleTracker: could not restore the %s call at RVA 0x%llX.", site.what,
            static_cast<unsigned long long>(site.rva));
    }
}

template <std::size_t N>
auto VerifyWitnesses(const std::array<Witness, N>& list) -> bool {
    bool ok = true;
    for (const Witness& witness : list) {
        if (Context->CheckExpectedBytes(witness.rva, witness.bytes, witness.size)) continue;
        char line[160];
        std::snprintf(line, sizeof(line), "%s (0x%llX) ", witness.what, static_cast<unsigned long long>(witness.rva));
        if (VerifyFailures.find(line) == std::string::npos) VerifyFailures += line;
        ok = false;
    }
    return ok;
}

template <std::size_t N>
auto InstallSites(std::array<CallSite, N>& sites, std::string& status) -> bool {
    for (CallSite& site : sites) {
        if (!PrepareSite(site)) {
            status = std::string("the ") + site.what + " call does not match D2RLoader 1.3.1";
            return false;
        }
    }
    for (CallSite& site : sites) {
        if (!ApplySite(site)) {
            for (CallSite& undo : sites) RestoreSite(undo);
            status = std::string("the loader refused the ") + site.what + " call";
            return false;
        }
    }
    status = "active";
    return true;
}

// ---------------------------------------------------------------------------
// Services
// ---------------------------------------------------------------------------

void QueryServices() noexcept {
    if (Context->QueryService(&LocalizationSvc) != D2RL::ServiceQueryResult::Success
            || !D2RL::HasLocalizationServiceField(LocalizationSvc, D2RL::LocalizationServiceRequiredSize)) {
        LocalizationSvc = nullptr;
        Context->LogWarn("ChronicleTracker: the localization service is unavailable; the label line is English only.");
    }
    if (Context->QueryService(&ResourceSvc) != D2RL::ServiceQueryResult::Success
            || !D2RL::HasResourceServiceField(ResourceSvc, D2RL::ResourceServiceRequiredSize)) {
        ResourceSvc = nullptr;
        Context->LogWarn("ChronicleTracker: the resource service is unavailable; the plugin's strings are not loaded.");
    }
}

void RegisterStrings() noexcept {
    std::string_view json;
    if (ResourceSvc == nullptr || !Embedded(CHRONICLE_TRACKER_STRINGS_ID, json)) return;
    const D2RL::Resources::ResourceRegistration resource{
        .structSize = D2RL::Resources::ResourceRegistrationSize,
        .flags      = 0,
        .path       = StringsPath,
        .bytes      = json.data(),
        .byteCount  = json.size(),
    };
    const auto result = ResourceSvc->registerResource(Context, &resource, &StringsHandle);
    if (result != D2RL::Resources::Result::Success) {
        D2RL::LogWarnF(Context, "ChronicleTracker: %s could not be registered (result %u).", StringsPath,
            static_cast<unsigned>(result));
    }
}

// ---------------------------------------------------------------------------
// Console command
// ---------------------------------------------------------------------------

auto Count(const std::atomic<std::uint64_t>& counter) noexcept -> unsigned long long {
    return static_cast<unsigned long long>(counter.load());
}

auto __cdecl StatusCommand(D2R::Game::Client*, const D2RL::ConsoleCommandContext* command, void*) noexcept
        -> D2RL::ConsoleCommandResult {
    if (command == nullptr || command->plugin == nullptr) return D2RL::ConsoleCommandResult::Failed;
    char       line[512];
    const auto say = [&](const char* text) { command->plugin->WriteConsoleMessage(text); };
    std::snprintf(line, sizeof(line), "Chronicle Tracker %s%s.", PluginVersion, Config.enabled ? "" : ": disabled by config");
    say(line);
    std::snprintf(line, sizeof(line), "  completing on pickup and cube outputs: %s.", CompleteStatus.c_str());
    say(line);
    std::snprintf(line, sizeof(line), "  ground label line: %s.", LabelsStatus.c_str());
    say(line);
    if (!VerifyFailures.empty()) {
        std::snprintf(line, sizeof(line), "  byte check failed: %.440s", VerifyFailures.c_str());
        say(line);
    }
    if (!ConfigProblems.empty()) {
        std::snprintf(line, sizeof(line), "  config: %.440s", ConfigProblems.c_str());
        say(line);
    }
    std::snprintf(line, sizeof(line), "  entries completed: %llu on pickup, %llu from the cube (%llu of them first marked "
        "as the builder marks a drop); %llu items the game keeps out of the Chronicle skipped.", Count(CompletedPickup),
        Count(CompletedCube), Count(CompletedMarked), Count(SkippedIneligible));
    say(line);
    return D2RL::ConsoleCommandResult::Handled;
}

constexpr D2RL::PluginInfo PluginInfoData{
    .infoSize    = D2RL::PluginInfoSize,
    .abiVersion  = D2RL_PLUGIN_ABI_VERSION,
    .id          = PluginIdText,
    .name        = "Chronicle Tracker",
    .version     = PluginVersion,
    .author      = "CelestialRayOne",
    .description = "Completes the Chronicle entry of identified Set items, Unique items and Runewords on pickup and from "
                   "the Horadric Cube, and marks ground items missing from the Chronicle with a second label line.",
    .flags       = D2RL::PluginFlags::Shared | D2RL::PluginFlags::NativeHooks,
};

} // namespace
} // namespace CelestialRayOne::ChronicleTracker

namespace CC = CelestialRayOne::ChronicleTracker;

D2RL_PLUGIN_EXPORT auto D2RLoaderGetPluginInfo() noexcept -> const D2RL::PluginInfo* {
    return &CC::PluginInfoData;
}

D2RL_PLUGIN_EXPORT auto D2RLoaderLoadPlugin(const D2RL::PluginContext* context) noexcept -> bool {
    using namespace CC;
    Context = context;
    if (Context == nullptr || Context->exeBase == 0) return false;
    Base = Context->exeBase;

    if (!Context->RegisterConsoleCommand("chronicletracker", &StatusCommand,
            "Chronicle Tracker status: installation and the entries completed this session.")) {
        Context->LogWarn("ChronicleTracker: the console command could not be registered.");
    }
    try {
        ReadConfiguration();
    } catch (...) {
        Context->LogError("ChronicleTracker: the config could not be read; nothing was installed.");
        return true;
    }
    if (!Config.enabled) {
        CompleteStatus = LabelsStatus = "disabled by config";
        Context->LogInfo("ChronicleTracker: disabled by config.");
        return true;
    }
    BindNatives();
    if (!PrepareStubPage()) {
        CompleteStatus = LabelsStatus = "no free page within jump range of the game image";
        Context->LogError("ChronicleTracker: no free page within jump range of the game image; nothing was installed.");
        return true;
    }
    if (!Config.completeOnAcquire) {
        CompleteStatus = "off in the config";
    } else if (!VerifyWitnesses(ServerWitnesses)) {
        CompleteStatus = "this D2R build does not match";
    } else if (InstallSites(ServerSites, CompleteStatus)) {
        CompleteActive.store(true);
    }
    if (!Config.markGroundLabels) {
        LabelsStatus = "off in the config";
    } else {
        QueryServices();
        RegisterStrings();
        if (!VerifyWitnesses(ClientWitnesses)) {
            LabelsStatus = "this D2R build does not match";
        } else if (InstallSites(ClientSites, LabelsStatus)) {
            LabelsActive.store(true);
        }
    }
    if (!VerifyFailures.empty()) D2RL::LogErrorF(Context, "ChronicleTracker: byte check failed: %s", VerifyFailures.c_str());
    D2RL::LogInfoF(Context, "ChronicleTracker %s: completing on pickup and cube outputs %s; ground label line %s.",
        PluginVersion, CompleteStatus.c_str(), LabelsStatus.c_str());
    return true;
}

D2RL_PLUGIN_EXPORT void D2RLoaderUnloadPlugin() noexcept {
    using namespace CC;
    CompleteActive.store(false);
    LabelsActive.store(false);
    if (Context == nullptr) return;
    for (CallSite& site : ServerSites) RestoreSite(site);
    for (CallSite& site : ClientSites) RestoreSite(site);
    if (ResourceSvc != nullptr && StringsHandle != D2RL::Resources::RegistrationHandle{}) {
        (void)ResourceSvc->unregisterResource(Context, StringsHandle);
    }
}
