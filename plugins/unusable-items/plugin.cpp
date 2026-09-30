// =============================================================================
//  Unusable Items  -  celestialrayone.unusable-items
//
//  Any item that carries a configured stat (default 531) is treated like an
//  item whose requirements are not met: red background, it cannot be
//  equipped by the player or a mercenary, and it gives no bonuses while it
//  stays equipped or sits in the inventory as a charm.
//
//  Built for D2R 3.3 under D2RLoader 1.3.1. Every address below was read out
//  of the dumped D2RLoader 1.3.1 image (md5 8af7a47ce9077de0378c69af2930854d)
//  or D2RCore.dll 1.3.1-beta (sha256 2a868d013d2e0830bd2d9e04b918b19e
//  46a73cf726c833e70d089b948fdeb5a2) and disassembled before being relied on.
//  Byte witnesses guard every game address, so a different build refuses
//  cleanly and the game runs stock.
//
// -----------------------------------------------------------------------------
//  THE ONE CHECK EVERY PATH ASKS
// -----------------------------------------------------------------------------
//  ITEMS_CheckItemRequirementsForUnit, 0x36BC50 (D2MOO ITEMS_CheckRequirements
//  with a seventh, class argument):
//
//      int32 (item, unit, int32 equipping, int32* strengthMet,
//             int32* dexterityMet, int32* levelMet, uint32 classOption)
//
//      36BC50  4C 89 4C 24 20        mov  [rsp+20h], r9     ; strengthMet
//      36BC55  44 89 44 24 18        mov  [rsp+18h], r8d    ; equipping
//      36BC5A  55 56 57              push rbp / rsi / rdi
//      36BC5D  48 83 EC 60           sub  rsp, 60h
//      36BC61  33 F6                 xor  esi, esi
//      36BC63  48 8B EA              mov  rbp, rdx          ; unit
//      36BC66  48 8B F9              mov  rdi, rcx          ; item
//
//  It clears the three out flags, fails anything that is not an item, then
//  answers 0 when strength, dexterity or level is short, when the item is
//  unidentified, for an empty tome and on a class mismatch, 1 otherwise. The
//  seventh argument is read as a dword (36C111 mov r8d, [rsp+78h+arg_30]).
//  Nothing branches back into the first 25 bytes, and none of them is
//  RIP-relative, so they relocate as they are.
//
//  It is the only requirement check in the game. Its 18 callers in the image
//  include the server inventory refresh 0x470CA0 (through 0x475760, which
//  adds the quiver test), the pickup auto-equip 0x36B6A0, the weapon-set
//  batch 0x471E90, the mercenary equip checks on both sides (0x2CB10D client,
//  0x4C1031 server) and the client inventory slot widget code 0x2C9940 (next
//  to the InventorySlotWidget factory 0x2C9850). D2RCore.dll
//  reaches it too: IsCharmUsable, CheckInventoryItemRequirementsForDisplay
//  (the red inventory background) and CheckInventorySlotItemRequirements
//  (the equipment slots) all load one game-symbol record and call through it:
//
//      D2RCore+815376  48 8B 05 E3 A6 EE FF   mov rax, [record]   ; display
//      D2RCore+8168A0  48 8B 05 B9 91 EE FF   mov rax, [record]   ; charms
//      D2RCore+817806  48 8B 05 53 82 EE FF   mov rax, [record]   ; slots
//      record D2RCore+6FFA60 = { pointer, 0x36BC50, 0 }
//
//  D2RCore's resolver (+1C9100: add rdx, [rcx+8] / mov [rcx], rdx) stores
//  exeBase + 0x36BC50 in the pointer, so those calls enter 0x36BC50 at its
//  first byte and pass through this plugin's hook like the game's own calls.
//  No other D2RCore code references that record.
//
//  What the answer controls on the server (0x470CA0, run after every item
//  transaction): every equipped item is first switched off (flag 0x4000, stat
//  list detached), then switched back on only while 0x475760 says yes. Charms
//  are switched on only while IsCharmUsable says yes. So an item the check
//  refuses gives no stats, skills or auras, on the body or as a charm, the
//  same as gear whose requirements are no longer met.
//
// -----------------------------------------------------------------------------
//  THE RULE
// -----------------------------------------------------------------------------
//  The hook runs the stock check first, so the three out flags keep their
//  real values and the tooltip's requirement lines keep their colours. Only a
//  yes is overridden: if the item carries the stat, the answer becomes 0.
//
//  "Carries the stat" means a non-zero value with param (layer) 0, read on
//  the item through 0x2F5020, the game's stat reader, which D2RLoader turned
//  into a thunk to D2RCore!ReadWideUnitStat (so ids past 511 work):
//
//      2F5020  FF 25 F2 51 B3 03 90 90 90 90   jmp [rip+slot] / nop x4
//
//  0x36BC50 reads the item's own stats the same way: the tome quantity
//  (36C0FE lea edx, [rax+34h] = stat 70, 36C104 call 2F5020) and
//  item_req_percent (36BDF4 lea edx, [r8+5Bh] = stat 91, 36BDF8 call 2F5C60,
//  the same D2RCore body). Items carry an extended stat list, which D2RCore
//  reads as totals, so the stat also counts when it comes from a socketed
//  gem, rune or jewel, the way a "-15% Requirements" jewel lowers the
//  requirements of the item it sits in.
//
//  The id is bounded first. D2RCore's reader looks the id up with 0x2D9730,
//  which asserts for an id past the itemstatcost table:
//
//      2D9742  E8 49 73 02 00          call 300A90            ; data tables
//      2D9747  48 8B F8                mov  rdi, rax
//      2D974A  48 8B DE                mov  rbx, rsi          ; stat id
//      2D974D  85 F6                   test esi, esi
//      2D974F  78 09                   js   assert
//      2D9751  48 3B 98 60 12 00 00    cmp  rbx, [rax+1260h]  ; row count
//
//  The plugin reads the row count the same way: the item's data context
//  (0x34A0E0, byte [unit+1BDh]), that context's data tables (0x300A90, which
//  asserts for a context of 4 or more) and the qword at +1260h. An id outside
//  the table refuses nothing and is reported once in the log.
//
//  Console command "unusableitems" shows the settings, what is installed,
//  which D2RCore checks go through the rule, and counters.
//  Settings: d2rloader/config/celestialrayone.unusable-items.toml
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
#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <string_view>

namespace CelestialRayOne::UnusableItems {
namespace {

constexpr char PluginVersion[] = "1.0.0";

// ---------------------------------------------------------------------------
//  Default configuration. EnsureConfig writes this text on first load, and it
//  is the plugin's documentation as shipped.
// ---------------------------------------------------------------------------
constexpr const char DefaultConfigToml[] = R"TOML(# celestialrayone.unusable-items
#
# Unusable Items
#   Any item that carries the stat set below is treated exactly like an item
#   whose requirements you do not meet:
#     - it gets the red background in the inventory, stash and cube;
#     - you cannot equip it, and neither can your mercenary;
#     - an item that is already equipped stays where it is but gives no
#       bonuses (no stats, skills or auras), and the same goes for a charm
#       in your inventory.
#   This works for every kind of item. The stat counts wherever the item gets
#   it, socketed gems, runes and jewels included, the same way a
#   "-15% Requirements" jewel lowers the requirements of the item it is in.
#
#   The tooltip's requirement lines keep their normal colours. Give the stat
#   a description in itemstatcost.txt (descfunc, descstrpos) if players should
#   see why the item cannot be used.
#
#   Give the stat's itemstatcost.txt row Save Bits above 0. Otherwise the stat
#   is dropped when the item is saved, and the item is usable again after the
#   character is reloaded.
#
# Changes are read when the plugin loads. Restart the game after editing.
# This is a Shared plugin: TCP/IP clients need it too.
# Built for D2RLoader 1.3.1 (Diablo II: Resurrected 3.3). On any other build
# the byte checks fail and the plugin loads without changing the game.

# Master switch. false loads the plugin without touching the game.
enabled = true

# -----------------------------------------------------------------------------
# stat_id
#
# The *ID of the itemstatcost.txt row, 0 to 32767. Any value other than 0 on
# an item makes it unusable. Only the value stored with param 0 is read, which
# is where every stat without a param lives, so leave Save Param Bits empty on
# this row.
#
# If the id is not a row of your itemstatcost.txt, no item is affected and the
# plugin log says so.
stat_id = 531

# -----------------------------------------------------------------------------
# diagnostics
#
# Writes one line to the plugin log for each of the first 64 different items
# the rule makes unusable (item code, unit id, stat value). Keep false for
# normal play. The console command "unusableitems" always shows the settings,
# what is installed and the counters.
diagnostics = false
)TOML";

// LOGIC-BEGIN
// ---------------------------------------------------------------------------
//  Settings
// ---------------------------------------------------------------------------
constexpr std::uint32_t MaximumStatId = 0x7FFF;  // D2RCore's stat reader answers 0 from 0x8000 up

struct Settings {
    bool          enabled{true};
    std::uint32_t statId{531};
    bool          diagnostics{false};
};

// ---------------------------------------------------------------------------
//  Configuration parser. Accepts the subset of TOML the default file uses:
//  key = true|false and key = unsigned integer. Anything it does not
//  understand is an error, never a silent default.
// ---------------------------------------------------------------------------
struct ConfigCursor {
    std::string_view text;
    std::size_t      position{0};
    std::size_t      line{1};
};

inline auto CursorPeek(const ConfigCursor& cursor) noexcept -> char {
    return cursor.position < cursor.text.size() ? cursor.text[cursor.position] : '\0';
}

inline auto CursorAtEnd(const ConfigCursor& cursor) noexcept -> bool {
    return cursor.position >= cursor.text.size();
}

inline void CursorAdvance(ConfigCursor& cursor) noexcept {
    if (cursor.position < cursor.text.size()) {
        if (cursor.text[cursor.position] == '\n') {
            ++cursor.line;
        }
        ++cursor.position;
    }
}

// Skips blanks and comments. Line breaks are skipped only when allowed.
inline void SkipBlank(ConfigCursor& cursor, bool acrossLines) noexcept {
    while (!CursorAtEnd(cursor)) {
        const char current = CursorPeek(cursor);
        if (current == ' ' || current == '\t' || current == '\r') {
            CursorAdvance(cursor);
        } else if (current == '#') {
            while (!CursorAtEnd(cursor) && CursorPeek(cursor) != '\n') {
                CursorAdvance(cursor);
            }
        } else if (current == '\n' && acrossLines) {
            CursorAdvance(cursor);
        } else {
            return;
        }
    }
}

inline auto FormatError(std::string& error, const char* format, ...) noexcept -> bool {
    char message[256]{};
    va_list args;
    va_start(args, format);
    std::vsnprintf(message, sizeof(message), format, args);
    va_end(args);
    error.assign(message);
    return false;
}

inline auto IsValueEnd(char next) noexcept -> bool {
    return next == '\0' || next == ' ' || next == '\t' || next == '\r' || next == '\n' || next == '#';
}

inline auto ParseBool(ConfigCursor& cursor, bool* value, std::string& error, const char* key) noexcept -> bool {
    const auto rest = cursor.text.substr(cursor.position);
    const auto endAt = [&rest](std::size_t at) noexcept {
        return IsValueEnd(at < rest.size() ? rest[at] : '\0');
    };
    if (rest.substr(0, 4) == "true" && endAt(4)) {
        *value = true;
        cursor.position += 4;
        return true;
    }
    if (rest.substr(0, 5) == "false" && endAt(5)) {
        *value = false;
        cursor.position += 5;
        return true;
    }
    return FormatError(error, "line %zu: %s must be true or false", cursor.line, key);
}

inline auto ParseStatId(ConfigCursor& cursor, std::uint32_t* value, std::string& error) noexcept -> bool {
    const auto line = cursor.line;
    std::uint32_t parsed = 0;
    std::size_t   digits = 0;
    while (!CursorAtEnd(cursor)) {
        const char current = CursorPeek(cursor);
        if (current < '0' || current > '9') {
            break;
        }
        parsed = parsed * 10 + static_cast<std::uint32_t>(current - '0');
        ++digits;
        CursorAdvance(cursor);
        if (parsed > MaximumStatId) {
            return FormatError(error, "line %zu: stat_id must be a number from 0 to %u", line, MaximumStatId);
        }
    }
    if (digits == 0 || !IsValueEnd(CursorPeek(cursor))) {
        return FormatError(error, "line %zu: stat_id must be a number from 0 to %u", line, MaximumStatId);
    }
    *value = parsed;
    return true;
}

// Parses the whole file into result. On failure result is untouched and error
// names the line. Unknown keys are reported in unknown and otherwise ignored.
inline auto ParseConfig(std::string_view text, Settings& result, std::string& error, std::string& unknown) noexcept -> bool {
    Settings parsed{};
    ConfigCursor cursor{text};
    bool seenEnabled = false;
    bool seenStatId = false;
    bool seenDiagnostics = false;

    for (;;) {
        SkipBlank(cursor, true);
        if (CursorAtEnd(cursor)) {
            break;
        }
        const auto keyStart = cursor.position;
        while (!CursorAtEnd(cursor)) {
            const char current = CursorPeek(cursor);
            const bool keyChar = (current >= 'a' && current <= 'z') || (current >= 'A' && current <= 'Z')
                              || (current >= '0' && current <= '9') || current == '_' || current == '-';
            if (!keyChar) {
                break;
            }
            CursorAdvance(cursor);
        }
        const auto key = text.substr(keyStart, cursor.position - keyStart);
        if (key.empty()) {
            return FormatError(error, "line %zu: expected a setting name", cursor.line);
        }
        SkipBlank(cursor, false);
        if (CursorPeek(cursor) != '=') {
            return FormatError(error, "line %zu: expected = after %.*s", cursor.line, static_cast<int>(key.size()), key.data());
        }
        CursorAdvance(cursor);
        SkipBlank(cursor, false);

        const auto duplicate = [](bool& seen) noexcept {
            if (seen) {
                return true;
            }
            seen = true;
            return false;
        };

        if (key == "enabled") {
            if (duplicate(seenEnabled)) {
                return FormatError(error, "line %zu: enabled appears more than once", cursor.line);
            }
            if (!ParseBool(cursor, &parsed.enabled, error, "enabled")) {
                return false;
            }
        } else if (key == "stat_id") {
            if (duplicate(seenStatId)) {
                return FormatError(error, "line %zu: stat_id appears more than once", cursor.line);
            }
            if (!ParseStatId(cursor, &parsed.statId, error)) {
                return false;
            }
        } else if (key == "diagnostics") {
            if (duplicate(seenDiagnostics)) {
                return FormatError(error, "line %zu: diagnostics appears more than once", cursor.line);
            }
            if (!ParseBool(cursor, &parsed.diagnostics, error, "diagnostics")) {
                return false;
            }
        } else {
            if (!unknown.empty()) {
                unknown.append(", ");
            }
            unknown.append(key.data(), key.size());
            // Skip the value: a bracketed list may span lines, anything else ends with the line.
            if (CursorPeek(cursor) == '[') {
                int  depth  = 0;
                bool quoted = false;
                while (!CursorAtEnd(cursor)) {
                    const char current = CursorPeek(cursor);
                    if (quoted) {
                        quoted = current != '"';
                    } else if (current == '"') {
                        quoted = true;
                    } else if (current == '#') {
                        while (!CursorAtEnd(cursor) && CursorPeek(cursor) != '\n') {
                            CursorAdvance(cursor);
                        }
                        continue;
                    } else if (current == '[') {
                        ++depth;
                    } else if (current == ']' && --depth == 0) {
                        CursorAdvance(cursor);
                        break;
                    }
                    CursorAdvance(cursor);
                }
            } else {
                while (!CursorAtEnd(cursor) && CursorPeek(cursor) != '\n') {
                    CursorAdvance(cursor);
                }
            }
        }

        SkipBlank(cursor, false);
        if (!CursorAtEnd(cursor) && CursorPeek(cursor) != '\n') {
            return FormatError(error, "line %zu: unexpected text after the value", cursor.line);
        }
    }

    result = parsed;
    return true;
}

// ---------------------------------------------------------------------------
//  The rule, for an item the stock check already accepted. Env supplies the
//  native queries, so the rule is testable off the game.
// ---------------------------------------------------------------------------
enum class Outcome : std::uint8_t {
    Stock,       // the item does not carry the stat: the stock answer stands
    Refused,     // the item carries the stat: the answer becomes 0
    NoTable,     // no itemstatcost table for the item's data context: stock answer
    MissingRow,  // the stat id is not a row of the loaded itemstatcost table: stock answer
};

struct Evaluation {
    Outcome       outcome{Outcome::Stock};
    std::int32_t  value{0};
    std::uint64_t rows{0};
};

template <typename Env>
inline auto EvaluateItem(Env& env, void* item, std::uint32_t statId) noexcept -> Evaluation {
    Evaluation result{};
    if (!env.StatRowCount(item, &result.rows)) {
        result.outcome = Outcome::NoTable;
        return result;
    }
    if (statId >= result.rows) {
        result.outcome = Outcome::MissingRow;
        return result;
    }
    result.value   = env.ReadStat(item, statId);
    result.outcome = result.value != 0 ? Outcome::Refused : Outcome::Stock;
    return result;
}

// D2 packs a base item code into four bytes, space padded: "amu" -> 'a','m','u',' '.
inline void UnpackItemCode(std::uint32_t code, char output[5]) noexcept {
    for (std::size_t index = 0; index < 4; ++index) {
        const auto byte = static_cast<char>((code >> (8 * index)) & 0xFF);
        output[index] = (byte >= 0x21 && byte <= 0x7E) ? byte : ' ';
    }
    output[4] = '\0';
    for (std::size_t index = 4; index > 0 && output[index - 1] == ' '; --index) {
        output[index - 1] = '\0';
    }
}
// LOGIC-END

// ---------------------------------------------------------------------------
//  Native layout (D2R 3.3, D2RLoader 1.3.1 process image)
// ---------------------------------------------------------------------------
constexpr const char    SourceFile[]           = "unusable-items";
constexpr std::uint8_t  DataContextCount       = 4;       // 0x300A90 asserts from 4 up
constexpr std::size_t   StatRowCountOffset     = 0x1260;  // qword in the data tables, see 2D9751
constexpr std::uint32_t NoUnit                 = 0xFFFFFFFFU;

constexpr std::uint64_t RequirementCheckRva    = 0x36BC50;  // ITEMS_CheckItemRequirementsForUnit, hooked
constexpr std::uint64_t ReadUnitStatRva        = 0x2F5020;  // STATLIST_GetUnitStat, thunk to ReadWideUnitStat
constexpr std::uint64_t GetDataContextRva      = 0x34A0E0;  // UNITS_GetDataContext(unit) -> byte [unit+1BDh]
constexpr std::uint64_t GetDataTablesRva       = 0x300A90;  // DATATBLS_GetTables(context)
constexpr std::uint64_t StatRowCountWitnessRva = 0x2D9742;  // inside the itemstatcost record getter 0x2D9730
constexpr std::uint64_t GetItemCodeRva         = 0x36EF50;  // ITEMS_GetItemCode(item), diagnostics only
constexpr std::uint64_t GetUnitIdRva           = 0x34A330;  // UNITS_GetUnitId(unit, file, line), diagnostics only

// The hook site: the whole prologue up to the first branch (0x36BC69 test r9, r9).
constexpr auto RequirementCheckEntry = std::to_array<std::uint8_t>({
    0x4C, 0x89, 0x4C, 0x24, 0x20, 0x44, 0x89, 0x44, 0x24, 0x18, 0x55, 0x56,
    0x57, 0x48, 0x83, 0xEC, 0x60, 0x33, 0xF6, 0x48, 0x8B, 0xEA, 0x48, 0x8B,
    0xF9,
});
constexpr auto ReadUnitStatThunk = std::to_array<std::uint8_t>({
    0xFF, 0x25, 0xF2, 0x51, 0xB3, 0x03, 0x90, 0x90, 0x90, 0x90,
});
constexpr auto GetDataContextBody = std::to_array<std::uint8_t>({
    0x48, 0x83, 0xEC, 0x28, 0x48, 0x85, 0xC9, 0x75, 0x1A, 0x88, 0x4C, 0x24,
    0x30, 0x48, 0x8D, 0x4C, 0x24, 0x30, 0xE8, 0x49, 0xC7, 0xFF, 0xFF, 0x84,
    0xC0, 0x74, 0x01, 0xCC, 0x32, 0xC0, 0x48, 0x83, 0xC4, 0x28, 0xC3, 0x0F,
    0xB6, 0x81, 0xBD, 0x01, 0x00, 0x00, 0x48, 0x83, 0xC4, 0x28, 0xC3,
});
constexpr auto GetDataTablesBody = std::to_array<std::uint8_t>({
    0x48, 0x83, 0xEC, 0x28, 0x0F, 0xB6, 0xC1, 0x48, 0x89, 0x44, 0x24, 0x38,
    0x48, 0x83, 0xF8, 0x04, 0x72, 0x19, 0x48, 0x8D, 0x44, 0x24, 0x38, 0x48,
    0x8D, 0x4C, 0x24, 0x40, 0x48, 0x89, 0x44, 0x24, 0x40, 0xE8, 0x7A, 0xD3,
    0xFF, 0xFF, 0x84, 0xC0, 0x74, 0x01, 0xCC, 0x48, 0x8B, 0x44, 0x24, 0x38,
    0x48, 0x8D, 0x0D, 0xB9, 0x9A, 0x79, 0x02, 0x48, 0x03, 0xC0, 0x48, 0x8B,
    0x04, 0xC1, 0x48, 0x83, 0xC4, 0x28, 0xC3,
});
// call 300A90 / mov rdi, rax / mov rbx, rsi / test esi, esi / js / cmp rbx, [rax+1260h]
constexpr auto StatRowCountWitness = std::to_array<std::uint8_t>({
    0xE8, 0x49, 0x73, 0x02, 0x00, 0x48, 0x8B, 0xF8, 0x48, 0x8B, 0xDE, 0x85,
    0xF6, 0x78, 0x09, 0x48, 0x3B, 0x98, 0x60, 0x12, 0x00, 0x00,
});
constexpr auto GetItemCodeEntry = std::to_array<std::uint8_t>({
    0x48, 0x89, 0x5C, 0x24, 0x10, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B,
    0xF9, 0x48, 0x85, 0xC9, 0x75, 0x13, 0x88, 0x4C, 0x24, 0x30, 0x48, 0x8D,
    0x4C, 0x24, 0x30, 0xE8, 0x80, 0x83, 0xFF, 0xFF,
});
constexpr auto GetUnitIdBody = std::to_array<std::uint8_t>({
    0x48, 0x83, 0xEC, 0x28, 0x48, 0x85, 0xC9, 0x75, 0x1D, 0x88, 0x4C, 0x24,
    0x30, 0x48, 0x8D, 0x4C, 0x24, 0x30, 0xE8, 0x39, 0xCA, 0xFF, 0xFF, 0x84,
    0xC0, 0x74, 0x01, 0xCC, 0xB8, 0xFF, 0xFF, 0xFF, 0xFF, 0x48, 0x83, 0xC4,
    0x28, 0xC3, 0x8B, 0x41, 0x08, 0x48, 0x83, 0xC4, 0x28, 0xC3,
});

// D2RCore.dll: the three usability exports that call the requirement check
// through a game-symbol record, and the offset of their "mov rax, [record]".
constexpr wchar_t CoreModuleName[] = L"D2RCore.dll";

struct CoreRoute {
    const char*   exportName;
    const char*   label;
    std::uint32_t loadOffset;
};

constexpr std::array<CoreRoute, 3> CoreRoutes{{
    {"CheckInventoryItemRequirementsForDisplay", "inventory background", 0x36},
    {"IsCharmUsable", "charms", 0x50},
    {"CheckInventorySlotItemRequirements", "equipment slots", 0x26},
}};

// ---------------------------------------------------------------------------
//  Native signatures
// ---------------------------------------------------------------------------
using RequirementCheckFn = std::int32_t(__fastcall*)(void* item, void* unit, std::int32_t equipping,
                                                     std::int32_t* strengthMet, std::int32_t* dexterityMet,
                                                     std::int32_t* levelMet, std::uint32_t classOption) noexcept;
using ReadUnitStatFn     = std::int32_t(__fastcall*)(void* unit, std::uint32_t statId, std::uint32_t layer) noexcept;
using GetDataContextFn   = std::uint8_t(__fastcall*)(void* unit) noexcept;
using GetDataTablesFn    = std::uintptr_t(__fastcall*)(std::uint8_t context) noexcept;
using GetItemCodeFn      = std::uint32_t(__fastcall*)(void* item) noexcept;
using GetUnitIdFn        = std::uint32_t(__fastcall*)(void* unit, const char* file, std::int32_t line) noexcept;

// ---------------------------------------------------------------------------
//  State
// ---------------------------------------------------------------------------
const D2RL::PluginContext* Context{};
std::uintptr_t             Base{};
Settings                   Config{};
bool                       HookInstalled{false};
char                       ConfigSource[48]{"built-in defaults"};
char                       InactiveReason[160]{};

RequirementCheckFn OriginalRequirementCheck{};
ReadUnitStatFn     ReadUnitStat{};
GetDataContextFn   GetDataContext{};
GetDataTablesFn    GetDataTables{};
GetItemCodeFn      GetItemCode{};
GetUnitIdFn        GetUnitId{};

std::atomic<bool> RuleActive{false};
std::atomic<bool> DiagnosticsActive{false};

std::atomic<std::uint64_t> Checks{};
std::atomic<std::uint64_t> Refused{};
std::atomic<std::uint64_t> MissingRowChecks{};
std::atomic<std::uint64_t> MissingRowCount{};
std::atomic<bool>          MissingRowLogged{false};

constexpr std::size_t DiagnosticsSlots = 64;
std::array<std::atomic<std::uint32_t>, DiagnosticsSlots> LoggedItems{};

std::array<const std::uint8_t*, CoreRoutes.size()> CoreRecords{};

template <typename T>
auto At(std::uint64_t rva) noexcept -> T {
    return reinterpret_cast<T>(Base + static_cast<std::uintptr_t>(rva));
}

auto ReadU32(const void* base, std::size_t offset) noexcept -> std::uint32_t {
    std::uint32_t value = 0;
    std::memcpy(&value, static_cast<const std::uint8_t*>(base) + offset, sizeof(value));
    return value;
}

auto ReadU64(const void* base, std::size_t offset) noexcept -> std::uint64_t {
    std::uint64_t value = 0;
    std::memcpy(&value, static_cast<const std::uint8_t*>(base) + offset, sizeof(value));
    return value;
}

// ---------------------------------------------------------------------------
//  Logging
// ---------------------------------------------------------------------------
void LogInfo(const char* format, ...) noexcept {
    char message[512]{};
    va_list args;
    va_start(args, format);
    std::vsnprintf(message, sizeof(message), format, args);
    va_end(args);
    Context->LogInfo(message);
}

void LogWarn(const char* format, ...) noexcept {
    char message[512]{};
    va_list args;
    va_start(args, format);
    std::vsnprintf(message, sizeof(message), format, args);
    va_end(args);
    Context->LogWarn(message);
}

void LogError(const char* format, ...) noexcept {
    char message[512]{};
    va_list args;
    va_start(args, format);
    std::vsnprintf(message, sizeof(message), format, args);
    va_end(args);
    Context->LogError(message);
}

void SetInactive(const char* reason) noexcept {
    std::snprintf(InactiveReason, sizeof(InactiveReason), "%s", reason);
}

// ---------------------------------------------------------------------------
//  Configuration
// ---------------------------------------------------------------------------
auto LoadConfig() noexcept -> bool {
    Config = Settings{};
    std::string error;
    std::string unknown;
    if (!ParseConfig(DefaultConfigToml, Config, error, unknown)) {
        LogError("UnusableItems: the built-in default configuration does not parse (%s).", error.c_str());
        return false;
    }
    std::snprintf(ConfigSource, sizeof(ConfigSource), "%s", "built-in defaults");

    if (!Context->EnsureConfig(DefaultConfigToml)) {
        LogWarn("UnusableItems: could not create the default config, using built-in defaults.");
        return true;
    }

    std::string buffer(8192, '\0');
    std::uint32_t required = 0;
    if (!Context->ReadConfig(buffer.data(), static_cast<std::uint32_t>(buffer.size()), &required)) {
        if (required <= buffer.size()) {
            LogWarn("UnusableItems: could not read the config, using built-in defaults.");
            return true;
        }
        buffer.assign(required, '\0');
        if (!Context->ReadConfig(buffer.data(), static_cast<std::uint32_t>(buffer.size()), &required)) {
            LogWarn("UnusableItems: could not read the config, using built-in defaults.");
            return true;
        }
    }

    Settings parsed{};
    error.clear();
    unknown.clear();
    if (!ParseConfig(std::string_view(buffer.c_str()), parsed, error, unknown)) {
        LogError("UnusableItems: config rejected (%s). Nothing is changed so a setting you meant is not silently "
                 "ignored; fix the file and restart.", error.c_str());
        return false;
    }
    if (!unknown.empty()) {
        LogWarn("UnusableItems: ignoring unknown config setting(s): %s.", unknown.c_str());
    }
    Config = parsed;
    std::snprintf(ConfigSource, sizeof(ConfigSource), "%s", "config file");
    return true;
}

// ---------------------------------------------------------------------------
//  Image checks
// ---------------------------------------------------------------------------
auto IsTrackedInlineHook(std::uint64_t rva, const std::uint8_t* bytes, std::size_t size) noexcept -> bool {
    const D2RL::DiagnosticsService* diagnostics = nullptr;
    if (Context->QueryService(&diagnostics) != D2RL::ServiceQueryResult::Success
        || !D2RL::HasDiagnosticsServiceField(diagnostics, D2RL::DiagnosticsServiceRequiredSize)
        || diagnostics->queryHookStatus == nullptr) {
        return false;
    }
    D2RL::Diagnostics::HookQuery query{
        .structSize   = D2RL::Diagnostics::HookQuerySize,
        .flags        = 0,
        .rva          = rva,
        .expected     = bytes,
        .expectedSize = static_cast<std::uint32_t>(size),
        .reserved     = 0,
    };
    D2RL::Diagnostics::HookStatus status{
        .structSize = D2RL::Diagnostics::HookStatusSize,
    };
    return diagnostics->queryHookStatus(Context, &query, &status) == D2RL::Diagnostics::Result::Success
        && status.state == D2RL::Diagnostics::ModificationState::Tracked
        && status.kind == D2RL::Diagnostics::ModificationKind::InlineHook;
}

auto CheckExact(std::uint64_t rva, const std::uint8_t* bytes, std::size_t size, const char* name) noexcept -> bool {
    if (Context->CheckExpectedBytes(rva, bytes, static_cast<std::uint32_t>(size))) {
        return true;
    }
    LogError("UnusableItems: %s at RVA 0x%llX does not match D2RLoader 1.3.1.", name, static_cast<unsigned long long>(rva));
    return false;
}

// A function the plugin only calls may already carry another plugin's inline
// hook at its entry. The loader tracks those, and calling through one is fine.
auto CheckCallable(std::uint64_t rva, const std::uint8_t* bytes, std::size_t size, const char* name) noexcept -> bool {
    if (Context->CheckExpectedBytes(rva, bytes, static_cast<std::uint32_t>(size))) {
        return true;
    }
    if (IsTrackedInlineHook(rva, bytes, size)) {
        LogInfo("UnusableItems: %s at RVA 0x%llX is hooked by another plugin, calling through it.", name,
                static_cast<unsigned long long>(rva));
        return true;
    }
    LogError("UnusableItems: %s at RVA 0x%llX does not match D2RLoader 1.3.1.", name, static_cast<unsigned long long>(rva));
    return false;
}

auto ValidateRuleFunctions() noexcept -> bool {
    return CheckCallable(ReadUnitStatRva, ReadUnitStatThunk.data(), ReadUnitStatThunk.size(), "STATLIST_GetUnitStat thunk")
        && CheckCallable(GetDataContextRva, GetDataContextBody.data(), GetDataContextBody.size(), "UNITS_GetDataContext")
        && CheckCallable(GetDataTablesRva, GetDataTablesBody.data(), GetDataTablesBody.size(), "DATATBLS_GetTables")
        && CheckExact(StatRowCountWitnessRva, StatRowCountWitness.data(), StatRowCountWitness.size(),
                      "itemstatcost row count read");
}

auto ValidateDiagnosticsFunctions() noexcept -> bool {
    return CheckCallable(GetItemCodeRva, GetItemCodeEntry.data(), GetItemCodeEntry.size(), "ITEMS_GetItemCode")
        && CheckCallable(GetUnitIdRva, GetUnitIdBody.data(), GetUnitIdBody.size(), "UNITS_GetUnitId");
}

// Finds, for each D2RCore usability export, the game-symbol record it calls
// through, and keeps it when the record names the requirement check. The
// records are only read, never written.
void ResolveCoreRoutes() noexcept {
    const HMODULE module = GetModuleHandleW(CoreModuleName);
    if (module == nullptr) {
        LogWarn("UnusableItems: D2RCore.dll is not loaded; its usability checks could not be verified.");
        return;
    }
    const auto* image     = reinterpret_cast<const std::uint8_t*>(module);
    const auto  ntOffset  = static_cast<std::size_t>(ReadU32(image, 0x3C));
    const auto  imageSize = static_cast<std::uintptr_t>(ReadU32(image, ntOffset + 0x50));
    const auto  imageBase = reinterpret_cast<std::uintptr_t>(module);

    for (std::size_t index = 0; index < CoreRoutes.size(); ++index) {
        const auto& route   = CoreRoutes[index];
        const auto  address = reinterpret_cast<std::uintptr_t>(GetProcAddress(module, route.exportName));
        if (address < imageBase || address + route.loadOffset + 7 > imageBase + imageSize) {
            LogWarn("UnusableItems: D2RCore.dll has no export %s; the %s check was not verified.", route.exportName,
                    route.label);
            continue;
        }
        const auto* load = reinterpret_cast<const std::uint8_t*>(address + route.loadOffset);
        if (load[0] != 0x48 || load[1] != 0x8B || load[2] != 0x05) {
            LogWarn("UnusableItems: D2RCore.dll %s is not the 1.3.1 code; the %s check was not verified.",
                    route.exportName, route.label);
            continue;
        }
        std::int32_t displacement = 0;
        std::memcpy(&displacement, load + 3, sizeof(displacement));
        const auto record = address + route.loadOffset + 7 + static_cast<std::intptr_t>(displacement);
        if (record < imageBase || record + 16 > imageBase + imageSize
            || ReadU64(reinterpret_cast<const void*>(record), 8) != RequirementCheckRva) {
            LogWarn("UnusableItems: D2RCore.dll %s does not call the requirement check; the %s check ignores the rule.",
                    route.exportName, route.label);
            continue;
        }
        CoreRecords[index] = reinterpret_cast<const std::uint8_t*>(record);
    }
}

// yes: bound to the hooked check. pending: not bound yet. no: bound elsewhere.
auto CoreRouteState(std::size_t index) noexcept -> const char* {
    const auto* record = CoreRecords[index];
    if (record == nullptr) {
        return "unverified";
    }
    const auto pointer = ReadU64(record, 0);
    if (pointer == Base + RequirementCheckRva) {
        return "yes";
    }
    return pointer == 0 ? "pending" : "no";
}

// ---------------------------------------------------------------------------
//  The rule, on native units
// ---------------------------------------------------------------------------
struct NativeEnvironment {
    auto StatRowCount(void* item, std::uint64_t* rows) const noexcept -> bool {
        const std::uint8_t context = GetDataContext(item);
        if (context >= DataContextCount) {
            return false;
        }
        const std::uintptr_t tables = GetDataTables(context);
        if (tables == 0) {
            return false;
        }
        *rows = ReadU64(reinterpret_cast<const void*>(tables), StatRowCountOffset);
        return true;
    }
    auto ReadStat(void* item, std::uint32_t statId) const noexcept -> std::int32_t {
        return ReadUnitStat(item, statId, 0);
    }
};

void NoteRefusedItem(void* item, std::int32_t value) noexcept {
    const std::uint32_t id = GetUnitId(item, SourceFile, 0);
    for (auto& slot : LoggedItems) {
        std::uint32_t current = slot.load(std::memory_order_relaxed);
        if (current == id) {
            return;
        }
        if (current != NoUnit) {
            continue;
        }
        if (slot.compare_exchange_strong(current, id, std::memory_order_relaxed)) {
            char code[5]{};
            UnpackItemCode(GetItemCode(item), code);
            LogInfo("UnusableItems: item %s (unit id %u) carries stat %u = %d and is treated as unusable.", code, id,
                    Config.statId, value);
            return;
        }
        if (current == id) {
            return;
        }
    }
}

void NoteMissingRow(std::uint64_t rows) noexcept {
    MissingRowChecks.fetch_add(1, std::memory_order_relaxed);
    MissingRowCount.store(rows, std::memory_order_relaxed);
    if (!MissingRowLogged.exchange(true, std::memory_order_relaxed)) {
        LogWarn("UnusableItems: stat_id %u is not a row of the loaded itemstatcost.txt (%llu rows), so no item is "
                "affected. Add the row or change stat_id.", Config.statId, static_cast<unsigned long long>(rows));
    }
}

// ---------------------------------------------------------------------------
//  Hook
// ---------------------------------------------------------------------------
std::int32_t __fastcall HookRequirementCheck(void* item, void* unit, std::int32_t equipping, std::int32_t* strengthMet,
                                             std::int32_t* dexterityMet, std::int32_t* levelMet,
                                             std::uint32_t classOption) noexcept {
    const std::int32_t stock =
        OriginalRequirementCheck(item, unit, equipping, strengthMet, dexterityMet, levelMet, classOption);
    if (stock == 0 || item == nullptr || !RuleActive.load(std::memory_order_acquire)) {
        return stock;
    }
    Checks.fetch_add(1, std::memory_order_relaxed);

    NativeEnvironment env{};
    const auto evaluation = EvaluateItem(env, item, Config.statId);
    switch (evaluation.outcome) {
    case Outcome::Refused:
        Refused.fetch_add(1, std::memory_order_relaxed);
        if (DiagnosticsActive.load(std::memory_order_relaxed)) {
            NoteRefusedItem(item, evaluation.value);
        }
        return 0;
    case Outcome::MissingRow:
        NoteMissingRow(evaluation.rows);
        return stock;
    case Outcome::NoTable:
    case Outcome::Stock:
        break;
    }
    return stock;
}

// ---------------------------------------------------------------------------
//  Installation
// ---------------------------------------------------------------------------
void Install() noexcept {
    if (!ValidateRuleFunctions()) {
        SetInactive("the game image does not match D2RLoader 1.3.1");
        return;
    }
    ReadUnitStat   = At<ReadUnitStatFn>(ReadUnitStatRva);
    GetDataContext = At<GetDataContextFn>(GetDataContextRva);
    GetDataTables  = At<GetDataTablesFn>(GetDataTablesRva);

    if (Config.diagnostics) {
        if (ValidateDiagnosticsFunctions()) {
            GetItemCode = At<GetItemCodeFn>(GetItemCodeRva);
            GetUnitId   = At<GetUnitIdFn>(GetUnitIdRva);
            DiagnosticsActive.store(true, std::memory_order_relaxed);
        } else {
            LogWarn("UnusableItems: diagnostics are off; the rule itself is not affected.");
        }
    }

    if (!Context->CheckExpectedBytes(RequirementCheckRva, RequirementCheckEntry.data(),
                                     static_cast<std::uint32_t>(RequirementCheckEntry.size()))) {
        if (IsTrackedInlineHook(RequirementCheckRva, RequirementCheckEntry.data(), RequirementCheckEntry.size())) {
            LogError("UnusableItems: another plugin already hooks the requirement check at RVA 0x%llX.",
                     static_cast<unsigned long long>(RequirementCheckRva));
            SetInactive("another plugin already hooks the requirement check");
        } else {
            LogError("UnusableItems: the requirement check at RVA 0x%llX does not match D2RLoader 1.3.1.",
                     static_cast<unsigned long long>(RequirementCheckRva));
            SetInactive("the game image does not match D2RLoader 1.3.1");
        }
        return;
    }

    // The rule reads Config only after this store, and Config never changes
    // afterwards. The loader writes the call-through pointer straight into
    // OriginalRequirementCheck, so it is set before the first hooked call.
    RuleActive.store(true, std::memory_order_release);
    if (!Context->InstallInlineHook(D2RL::MakeInlineHook(
            RequirementCheckRva, RequirementCheckEntry.data(), static_cast<std::uint32_t>(RequirementCheckEntry.size()),
            reinterpret_cast<void*>(&HookRequirementCheck), reinterpret_cast<void**>(&OriginalRequirementCheck)))
        || OriginalRequirementCheck == nullptr) {
        RuleActive.store(false, std::memory_order_relaxed);
        LogError("UnusableItems: the loader refused the hook on the requirement check at RVA 0x%llX.",
                 static_cast<unsigned long long>(RequirementCheckRva));
        SetInactive("the requirement check could not be hooked");
        return;
    }
    HookInstalled = true;

    ResolveCoreRoutes();
}

// ---------------------------------------------------------------------------
//  Console
// ---------------------------------------------------------------------------
auto OnOff(bool value) noexcept -> const char* {
    return value ? "on" : "off";
}

auto StatusCommand(D2R::Game::Client*, const D2RL::ConsoleCommandContext* command, void*) noexcept -> D2RL::ConsoleCommandResult {
    if (command == nullptr || command->plugin == nullptr) {
        return D2RL::ConsoleCommandResult::Failed;
    }

    char line[512]{};
    std::snprintf(line, sizeof(line), "Unusable Items %s (%s): stat %u; rule %s; diagnostics %s.%s%s", PluginVersion,
                  ConfigSource, Config.statId, OnOff(HookInstalled), OnOff(DiagnosticsActive.load()),
                  InactiveReason[0] != '\0' ? " Inactive: " : "", InactiveReason);
    command->plugin->WriteConsoleMessage(line);

    if (HookInstalled) {
        std::snprintf(line, sizeof(line), "D2RCore checks that follow the rule: %s %s, %s %s, %s %s.",
                      CoreRoutes[0].label, CoreRouteState(0), CoreRoutes[1].label, CoreRouteState(1),
                      CoreRoutes[2].label, CoreRouteState(2));
        command->plugin->WriteConsoleMessage(line);
    }

    std::snprintf(line, sizeof(line), "Items that passed the stock requirements and were checked: %llu. Made unusable by the stat: %llu.",
                  static_cast<unsigned long long>(Checks.load(std::memory_order_relaxed)),
                  static_cast<unsigned long long>(Refused.load(std::memory_order_relaxed)));
    command->plugin->WriteConsoleMessage(line);

    if (MissingRowChecks.load(std::memory_order_relaxed) != 0) {
        std::snprintf(line, sizeof(line), "stat %u is not a row of the loaded itemstatcost.txt (%llu rows): no item is affected.",
                      Config.statId, static_cast<unsigned long long>(MissingRowCount.load(std::memory_order_relaxed)));
        command->plugin->WriteConsoleMessage(line);
    }
    return D2RL::ConsoleCommandResult::Handled;
}

void ResetState() noexcept {
    Config        = Settings{};
    HookInstalled = false;
    InactiveReason[0] = '\0';
    OriginalRequirementCheck = nullptr;
    RuleActive.store(false, std::memory_order_relaxed);
    DiagnosticsActive.store(false, std::memory_order_relaxed);
    Checks.store(0, std::memory_order_relaxed);
    Refused.store(0, std::memory_order_relaxed);
    MissingRowChecks.store(0, std::memory_order_relaxed);
    MissingRowCount.store(0, std::memory_order_relaxed);
    MissingRowLogged.store(false, std::memory_order_relaxed);
    for (auto& slot : LoggedItems) {
        slot.store(NoUnit, std::memory_order_relaxed);
    }
    CoreRecords.fill(nullptr);
}

}  // namespace
}  // namespace CelestialRayOne::UnusableItems

namespace {

constexpr D2RL::PluginInfo Info{
    .infoSize    = D2RL::PluginInfoSize,
    .abiVersion  = D2RL_PLUGIN_ABI_VERSION,
    .id          = "celestialrayone.unusable-items",
    .name        = "Unusable Items",
    .version     = CelestialRayOne::UnusableItems::PluginVersion,
    .author      = "CelestialRayOne",
    .description = "Items that carry a configured stat are treated as items whose requirements are not met.",
    .flags       = D2RL::PluginFlags::Shared | D2RL::PluginFlags::NativeHooks,
};

}  // namespace

D2RL_PLUGIN_EXPORT auto D2RLoaderGetPluginInfo() noexcept -> const D2RL::PluginInfo* {
    return &Info;
}

D2RL_PLUGIN_EXPORT auto D2RLoaderLoadPlugin(const D2RL::PluginContext* context) noexcept -> bool {
    using namespace CelestialRayOne::UnusableItems;

    if (!D2RL::HasContext(context) || context->exeBase == 0) {
        return false;
    }
    Context = context;
    Base    = context->exeBase;
    ResetState();

    if (!context->RegisterConsoleCommand("unusableitems", StatusCommand,
                                         "Show Unusable Items settings, state and counters.")) {
        LogWarn("UnusableItems: the unusableitems console command could not be registered.");
    }

    if (!LoadConfig()) {
        Config.enabled = false;
        SetInactive("the config was rejected, see the plugin log");
        return true;
    }
    if (!Config.enabled) {
        SetInactive("disabled by config");
        LogInfo("Unusable Items %s loaded disabled by config; the game runs stock.", PluginVersion);
        return true;
    }

    Install();

    LogInfo("Unusable Items %s by CelestialRayOne: stat %u, rule %s, diagnostics %s, config %s, build %s.",
            PluginVersion, Config.statId, OnOff(HookInstalled), OnOff(DiagnosticsActive.load()), ConfigSource,
            context->buildName != nullptr ? context->buildName : "unknown");
    if (HookInstalled) {
        LogInfo("UnusableItems: D2RCore checks that follow the rule: %s %s, %s %s, %s %s.", CoreRoutes[0].label,
                CoreRouteState(0), CoreRoutes[1].label, CoreRouteState(1), CoreRoutes[2].label, CoreRouteState(2));
    }
    return true;
}

D2RL_PLUGIN_EXPORT void D2RLoaderUnloadPlugin() noexcept {
    using namespace CelestialRayOne::UnusableItems;
    if (Context == nullptr) {
        return;
    }
    // D2RLoader removes the inline hook itself. Until it does, the hook answers stock.
    RuleActive.store(false, std::memory_order_relaxed);
    DiagnosticsActive.store(false, std::memory_order_relaxed);
}
