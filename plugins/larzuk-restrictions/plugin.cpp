// =============================================================================
//  Larzuk Restrictions  -  celestialrayone.larzuk-restrictions
//
//  Larzuk refuses to socket items of the configured itemtypes.txt types. The
//  default list is "char", which refuses every charm. A refused item is
//  handled exactly like any other item Larzuk cannot socket: his socket panel
//  does not take it, the server refuses the request, and the quest reward
//  stays unused.
//
//  Built for D2R 3.3 under D2RLoader 1.3.1. Every address below was read out
//  of the dumped D2RLoader 1.3.1 image (md5 8af7a47ce9077de0378c69af2930854d)
//  and disassembled before being relied on. Byte witnesses guard every game
//  address, so a different build refuses cleanly and the game runs stock.
//
// -----------------------------------------------------------------------------
//  LARZUK'S CHECK
// -----------------------------------------------------------------------------
//  0x36B410, int32 (item), D2MOO ITEMS_IsSocketable. 1 means Larzuk may add
//  sockets to the item. It answers 0 for gold, no-sell items, quest items
//  other than "leg", broken items, items that hold something, items already
//  socketed, items whose ITEMS_GetMaxSockets (0x36EAD0) is 0 and items whose
//  item_numsockets (stat 194) is not 0.
//
//  Its only callers in the image:
//    0x4FCD23  D2GAME_NPC_HandleItemServiceTransaction 0x4FC230, service 2
//              (Larzuk). A 0 logs "Unable to upgrade the item" and returns
//              before ITEMS_AddSockets (call at 0x4FD574) and before
//              QUESTS_ConsumeLarzukSocketReward (0x548B60), so nothing is
//              spent.
//    0x1E389B  the client service panel predicate 0x1E37F0
//    0x1E598C  the client service panel slot toggle 0x1E58F0
//              Both call it only while the open NPC is class 511 (Larzuk).
//              A 0 makes the panel refuse the item with the stock refusal.
//  The fourth reference, 0x143C354C8, is the function's .pdata entry.
//
//  The hook takes the prologue up to the first branch:
//      36B410  40 57            push rdi
//      36B412  48 83 EC 20      sub  rsp, 20h
//      36B416  48 8B F9         mov  rdi, rcx       ; item
//      36B419  48 85 C9         test rcx, rcx
//      36B41C  75 13            jnz  36B431         ; not taken by the hook
//  No branch in the function (36B410..36B694) lands inside those 12 bytes and
//  none of them is RIP-relative, so they relocate as they are. The jnz reads
//  the flags of the relocated test, which the jump back does not change.
//
// -----------------------------------------------------------------------------
//  THE RULE
// -----------------------------------------------------------------------------
//  The stock check runs first, so everything it refuses stays refused. Only a
//  yes is overridden: if the item is of one of the configured types, the
//  answer becomes 0.
//
//  The type test is the game's ITEMS_CheckItemTypeId 0x373890 (item, type
//  row), which reads the compiled equivalence table for the item's type and
//  type2. A type therefore also matches every type whose Equiv1/Equiv2 chain
//  reaches it: "char" covers scha, mcha, lcha and, in ESR, qcha, ocha, xcha.
//
//  Codes are matched against itemtypes.txt on every call, in the item's own
//  data context (0x34A0E0, byte [unit+1BDh]) and that context's tables
//  (0x300A90): rows at +1348h, row count at +1350h, 0E8h bytes per row, the
//  code in the first four bytes. The itemtypes.txt loader 0x318690 proves it
//  when it hands every row's code to the code linker:
//      318E40  lea  rdi, [rsi+1348h]          ; rsi = tables
//      318EA8  mov  rbx, [rdi]                ; first row
//      318EAB  imul rdi, [rdi+8], 0E8h        ; row count * row size
//      318EC0  mov  edx, [rbx]                ; the row's code
//      318ECA  add  rbx, 0E8h
//      318ED6  mov  rbx, [rsi+1350h]          ; row count
//  and ITEMS_CheckItemTypeId bounds its type row by the same count:
//      3738E9  mov  rbx, [rax+1350h]
//
//  Console command "larzukrestrictions" shows the settings, state and counters.
//  Settings: d2rloader/config/celestialrayone.larzuk-restrictions.toml
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

namespace CelestialRayOne::LarzukRestrictions {
namespace {

constexpr char PluginVersion[] = "1.0.0";

// ---------------------------------------------------------------------------
//  Default configuration. EnsureConfig writes this text on first load, and it
//  is the plugin's documentation as shipped.
// ---------------------------------------------------------------------------
constexpr const char DefaultConfigToml[] = R"TOML(# celestialrayone.larzuk-restrictions
#
# Larzuk Restrictions
#   Larzuk refuses to socket items of the item types listed below. The default
#   list refuses every charm: Small, Large and Grand Charms and every charm
#   type built on them (in ESR the Square, Odd and Extra charms too).
#
#   A refused item is treated like any other item Larzuk cannot socket:
#     - his socket panel does not take it (the usual refusal);
#     - the server refuses the request as well, should one reach it;
#     - his socket reward is not used up, so it can still go to an item he
#       accepts.
#   Items of every other type are socketed exactly as before.
#
# Changes are read when the plugin loads. Restart the game after editing.
# This is a Shared plugin: TCP/IP clients need it too.
# Built for D2RLoader 1.3.1 (Diablo II: Resurrected 3.3). On any other build
# the byte checks fail and the plugin loads without changing the game.

# Master switch. false loads the plugin without touching the game.
enabled = true

# -----------------------------------------------------------------------------
# refused_item_types
#
# Codes from the Code column of itemtypes.txt, written exactly as there (1 to
# 4 characters), for example ["char"] or ["scha", "lcha"]. An item is refused
# when its type or type2 is one of these codes, or reaches one of them
# through the Equiv1/Equiv2 columns. So "char" covers every charm type, while
# "scha" alone would refuse Small Charms only.
#
# A code that is not in your itemtypes.txt refuses nothing and is reported
# once in the plugin log. An empty list [] refuses nothing.
refused_item_types = ["char"]

# -----------------------------------------------------------------------------
# diagnostics
#
# Writes one line to the plugin log for each of the first 64 different items
# (by item code) that Larzuk refuses because of this plugin. Keep false for
# normal play. The console command "larzukrestrictions" always shows the
# settings and the counters.
diagnostics = false
)TOML";

// LOGIC-BEGIN
// ---------------------------------------------------------------------------
//  Settings
// ---------------------------------------------------------------------------
constexpr std::size_t   MaximumTypeCodes    = 32;
constexpr std::size_t   ItemTypeRowSize     = 0xE8;  // itemtypes.txt row in the compiled table, see 318EAB
constexpr std::uint64_t MaximumItemTypeRows = 4096;  // sanity bound on the row count read from the tables

struct TypeCode {
    char         text[5]{};
    std::uint8_t length{0};
};

struct Settings {
    bool                                   enabled{true};
    std::array<TypeCode, MaximumTypeCodes> codes{};
    std::size_t                            codeCount{0};
    bool                                   diagnostics{false};
};

// ---------------------------------------------------------------------------
//  Configuration parser. Accepts the subset of TOML the default file uses:
//  key = true|false and key = ["code", ...]. Anything it does not understand
//  is an error, never a silent default.
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

// ["code", "code", ...]. The list may span lines and hold comments. A code is
// 1 to 4 visible characters. Repeated codes are kept once.
inline auto ParseCodeList(ConfigCursor& cursor, Settings& parsed, std::string& error) noexcept -> bool {
    const auto line = cursor.line;
    if (CursorPeek(cursor) != '[') {
        return FormatError(error, "line %zu: refused_item_types must be a list such as [\"char\"]", line);
    }
    CursorAdvance(cursor);
    parsed.codeCount = 0;

    for (;;) {
        SkipBlank(cursor, true);
        if (CursorAtEnd(cursor)) {
            return FormatError(error, "line %zu: refused_item_types is missing its closing ]", line);
        }
        if (CursorPeek(cursor) == ']') {
            CursorAdvance(cursor);
            return true;
        }
        if (CursorPeek(cursor) != '"') {
            return FormatError(error, "line %zu: expected a quoted item type code in refused_item_types", cursor.line);
        }
        CursorAdvance(cursor);

        TypeCode code{};
        for (;;) {
            if (CursorAtEnd(cursor) || CursorPeek(cursor) == '\n') {
                return FormatError(error, "line %zu: an item type code is missing its closing quote", cursor.line);
            }
            const char current = CursorPeek(cursor);
            if (current == '"') {
                CursorAdvance(cursor);
                break;
            }
            if (current < 0x21 || current > 0x7E || current == '\\') {
                return FormatError(error, "line %zu: item type codes are 1 to 4 visible characters without spaces",
                                   cursor.line);
            }
            if (code.length == 4) {
                return FormatError(error, "line %zu: item type codes are at most 4 characters long", cursor.line);
            }
            code.text[code.length++] = current;
            CursorAdvance(cursor);
        }
        if (code.length == 0) {
            return FormatError(error, "line %zu: an item type code is empty", cursor.line);
        }

        bool repeated = false;
        for (std::size_t index = 0; index < parsed.codeCount; ++index) {
            if (std::strcmp(parsed.codes[index].text, code.text) == 0) {
                repeated = true;
                break;
            }
        }
        if (!repeated) {
            if (parsed.codeCount == MaximumTypeCodes) {
                return FormatError(error, "line %zu: refused_item_types holds at most %zu codes", cursor.line,
                                   MaximumTypeCodes);
            }
            parsed.codes[parsed.codeCount++] = code;
        }

        SkipBlank(cursor, true);
        if (CursorPeek(cursor) == ',') {
            CursorAdvance(cursor);
            continue;
        }
        if (CursorPeek(cursor) == ']') {
            CursorAdvance(cursor);
            return true;
        }
        return FormatError(error, "line %zu: expected , or ] in refused_item_types", cursor.line);
    }
}

// Parses the whole file into result. On failure result is untouched and error
// names the line. Unknown keys are reported in unknown and otherwise ignored.
inline auto ParseConfig(std::string_view text, Settings& result, std::string& error, std::string& unknown) noexcept -> bool {
    Settings parsed{};
    ConfigCursor cursor{text};
    bool seenEnabled = false;
    bool seenTypes = false;
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
        } else if (key == "refused_item_types") {
            if (duplicate(seenTypes)) {
                return FormatError(error, "line %zu: refused_item_types appears more than once", cursor.line);
            }
            if (!ParseCodeList(cursor, parsed, error)) {
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
    Stock,    // no configured type matches: the stock answer stands
    Refused,  // a configured type matches: the answer becomes 0
    NoTable,  // no itemtypes table for the item's data context: stock answer
};

struct Evaluation {
    Outcome     outcome{Outcome::Stock};
    std::size_t code{0};  // index into Settings::codes when Refused
};

// A row's code is four bytes; codes shorter than four are padded with spaces
// (or NULs, accepted as well).
inline auto RowMatches(const std::uint8_t* row, const TypeCode& code) noexcept -> bool {
    for (std::size_t index = 0; index < 4; ++index) {
        const char have = static_cast<char>(row[index]);
        if (index < code.length) {
            if (have != code.text[index]) {
                return false;
            }
        } else if (have != ' ' && have != '\0') {
            return false;
        }
    }
    return true;
}

inline auto FindTypeRow(const std::uint8_t* rows, std::uint64_t count, const TypeCode& code) noexcept -> std::int32_t {
    for (std::uint64_t row = 0; row < count; ++row) {
        if (RowMatches(rows + row * ItemTypeRowSize, code)) {
            return static_cast<std::int32_t>(row);
        }
    }
    return -1;
}

template <typename Env>
inline auto EvaluateItem(Env& env, void* item, const Settings& settings) noexcept -> Evaluation {
    Evaluation result{};
    const std::uint8_t* rows    = nullptr;
    std::uint64_t       count   = 0;
    std::uint8_t        context = 0;
    if (!env.ItemTypeRows(item, &rows, &count, &context)) {
        result.outcome = Outcome::NoTable;
        return result;
    }
    for (std::size_t index = 0; index < settings.codeCount; ++index) {
        const std::int32_t row = FindTypeRow(rows, count, settings.codes[index]);
        if (row < 0) {
            env.UnknownCode(index, context);
            continue;
        }
        if (env.IsOfType(item, row)) {
            result.outcome = Outcome::Refused;
            result.code    = index;
            return result;
        }
    }
    return result;
}

// D2 packs a base item code into four bytes, space padded: "cm1" -> 'c','m','1',' '.
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
constexpr std::uint8_t DataContextCount     = 4;       // 0x300A90 asserts from 4 up
constexpr std::size_t  ItemTypeRowsOffset   = 0x1348;  // qword: first itemtypes row, see 318E40
constexpr std::size_t  ItemTypeCountOffset  = 0x1350;  // qword: itemtypes row count, see 318ED6 / 3738E9

constexpr std::uint64_t LarzukCheckRva          = 0x36B410;  // Larzuk's socket eligibility check, hooked
constexpr std::uint64_t CheckItemTypeRva        = 0x373890;  // ITEMS_CheckItemTypeId (item, type row)
constexpr std::uint64_t CheckItemTypeWindowRva  = 0x3738DC;  // inside 0x373890: tables lookup and row bound
constexpr std::uint64_t GetDataContextRva       = 0x34A0E0;  // UNITS_GetDataContext (unit) -> byte [unit+1BDh]
constexpr std::uint64_t GetDataTablesRva        = 0x300A90;  // DATATBLS_GetTables (context)
constexpr std::uint64_t ItemTypesLoaderRva      = 0x318E40;  // inside the itemtypes.txt loader 0x318690
constexpr std::uint64_t GetItemCodeRva          = 0x36EF50;  // ITEMS_GetItemCode (item), diagnostics only

// The hook site: the prologue up to the first branch (0x36B41C jnz).
constexpr auto LarzukCheckEntry = std::to_array<std::uint8_t>({
    0x40, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B, 0xF9, 0x48, 0x85, 0xC9,
});
constexpr auto CheckItemTypeEntry = std::to_array<std::uint8_t>({
    0x48, 0x89, 0x5C, 0x24, 0x10, 0x48, 0x89, 0x6C, 0x24, 0x18, 0x48, 0x89,
    0x74, 0x24, 0x20, 0x57, 0x41, 0x56, 0x41, 0x57, 0x48, 0x83, 0xEC, 0x20,
});
// 3738DC..3739C4: call 34A0E0 / call 300A90 / mov rbx, [rax+1350h] / bound the
// type row, then the type and type2 lookups in the equivalence table.
constexpr auto CheckItemTypeWindow = std::to_array<std::uint8_t>({
    0xE8, 0xFF, 0x67, 0xFD, 0xFF, 0x0F, 0xB6, 0xC8, 0xE8, 0xA7, 0xD1, 0xF8,
    0xFF, 0x48, 0x8B, 0x98, 0x50, 0x13, 0x00, 0x00, 0x48, 0x63, 0xC3, 0x48,
    0x3B, 0xC3, 0x75, 0x04, 0x85, 0xDB, 0x79, 0x14, 0x48, 0x8D, 0x4C, 0x24,
    0x40, 0xC6, 0x44, 0x24, 0x40, 0x00, 0xE8, 0x55, 0x30, 0xD1, 0xFF, 0x84,
    0xC0, 0x74, 0x01, 0xCC, 0x3B, 0xF3, 0x7D, 0x41, 0x41, 0xB8, 0x75, 0x12,
    0x00, 0x00, 0x48, 0x8D, 0x15, 0x6F, 0x0A, 0x99, 0x01, 0x48, 0x8B, 0xCF,
    0xE8, 0x37, 0x5F, 0xFD, 0xFF, 0x48, 0x8B, 0xCF, 0x8B, 0xD8, 0xE8, 0xAD,
    0x67, 0xFD, 0xFF, 0x8B, 0xD3, 0x0F, 0xB6, 0xC8, 0xE8, 0xD3, 0x07, 0xFA,
    0xFF, 0x48, 0x85, 0xC0, 0x75, 0x2E, 0x48, 0x8D, 0x4C, 0x24, 0x40, 0x88,
    0x44, 0x24, 0x40, 0xE8, 0xF0, 0x52, 0xFF, 0xFF, 0x84, 0xC0, 0x74, 0x01,
    0xCC, 0x33, 0xC0, 0x48, 0x8B, 0x5C, 0x24, 0x48, 0x48, 0x8B, 0x6C, 0x24,
    0x50, 0x48, 0x8B, 0x74, 0x24, 0x58, 0x48, 0x83, 0xC4, 0x20, 0x41, 0x5F,
    0x41, 0x5E, 0x5F, 0xC3, 0x0F, 0xBF, 0x98, 0x2E, 0x01, 0x00, 0x00, 0x83,
    0xFB, 0xFF, 0x74, 0xD9, 0x48, 0x8B, 0xCF, 0xE8, 0x5C, 0x67, 0xFD, 0xFF,
    0x0F, 0xB6, 0xC8, 0xE8, 0x04, 0xD1, 0xF8, 0xFF, 0x48, 0x8B, 0xE8, 0x0F,
    0xAF, 0x98, 0x60, 0x13, 0x00, 0x00, 0x48, 0x8D, 0x88, 0x68, 0x13, 0x00,
    0x00, 0x48, 0x63, 0xD3, 0xE8, 0x7B, 0x38, 0xFF, 0xFF, 0x48, 0x8B, 0xD6,
    0x48, 0x8B, 0xCE, 0x48, 0xC1, 0xE9, 0x05, 0x83, 0xE2, 0x1F, 0x48, 0x8D,
    0x34, 0x8D, 0x00, 0x00, 0x00, 0x00, 0x8B, 0x04, 0x06, 0x48, 0x8D, 0x0D,
    0x0C, 0x5D, 0xA2, 0x01,
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
// 318E40..318EDD: lea rdi, [rsi+1348h] ... the loop that hands each row's
// first dword to the code linker (row size 0E8h) ... mov rbx, [rsi+1350h].
constexpr auto ItemTypesLoaderWindow = std::to_array<std::uint8_t>({
    0x48, 0x8D, 0xBE, 0x48, 0x13, 0x00, 0x00, 0x48, 0x8D, 0x44, 0x24, 0x48,
    0x48, 0x3B, 0xF8, 0x74, 0x2B, 0x4C, 0x39, 0x7F, 0x10, 0x7C, 0x18, 0x48,
    0x8B, 0x1F, 0xE8, 0x61, 0xD5, 0xEE, 0x00, 0x48, 0x8B, 0xD3, 0x48, 0x8B,
    0xC8, 0x4C, 0x8B, 0x00, 0x41, 0xFF, 0x50, 0x20, 0x4C, 0x89, 0x3F, 0x48,
    0x8D, 0x54, 0x24, 0x48, 0x48, 0x8B, 0xCF, 0xE8, 0x44, 0x7F, 0x00, 0x00,
    0x4C, 0x85, 0x6C, 0x24, 0x58, 0x75, 0x19, 0xE8, 0x38, 0xD5, 0xEE, 0x00,
    0x48, 0x8B, 0x54, 0x24, 0x48, 0x48, 0x8B, 0xC8, 0x4C, 0x8B, 0x00, 0x41,
    0xFF, 0x50, 0x20, 0x4C, 0x89, 0x7C, 0x24, 0x48, 0x49, 0x8B, 0x0E, 0xE8,
    0x7C, 0x76, 0x6F, 0x00, 0x84, 0xC0, 0x74, 0x2E, 0x48, 0x8B, 0x1F, 0x48,
    0x69, 0x7F, 0x08, 0xE8, 0x00, 0x00, 0x00, 0x4D, 0x8B, 0x36, 0x48, 0x03,
    0xFB, 0x48, 0x3B, 0xDF, 0x74, 0x18, 0x66, 0x90, 0x8B, 0x13, 0x49, 0x8B,
    0xCE, 0xE8, 0x16, 0x8E, 0x6F, 0x00, 0x48, 0x81, 0xC3, 0xE8, 0x00, 0x00,
    0x00, 0x48, 0x3B, 0xDF, 0x75, 0xEA, 0x48, 0x8B, 0x9E, 0x50, 0x13, 0x00,
    0x00,
});
constexpr auto GetItemCodeEntry = std::to_array<std::uint8_t>({
    0x48, 0x89, 0x5C, 0x24, 0x10, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B,
    0xF9, 0x48, 0x85, 0xC9, 0x75, 0x13, 0x88, 0x4C, 0x24, 0x30, 0x48, 0x8D,
    0x4C, 0x24, 0x30, 0xE8, 0x80, 0x83, 0xFF, 0xFF,
});

// ---------------------------------------------------------------------------
//  Native signatures
// ---------------------------------------------------------------------------
using LarzukCheckFn    = std::int32_t(__fastcall*)(void* item) noexcept;
using CheckItemTypeFn  = std::int32_t(__fastcall*)(void* item, std::int32_t typeRow) noexcept;
using GetDataContextFn = std::uint8_t(__fastcall*)(void* unit) noexcept;
using GetDataTablesFn  = std::uintptr_t(__fastcall*)(std::uint8_t context) noexcept;
using GetItemCodeFn    = std::uint32_t(__fastcall*)(void* item) noexcept;

// ---------------------------------------------------------------------------
//  State
// ---------------------------------------------------------------------------
const D2RL::PluginContext* Context{};
std::uintptr_t             Base{};
Settings                   Config{};
bool                       HookInstalled{false};
char                       ConfigSource[48]{"built-in defaults"};
char                       InactiveReason[160]{};

LarzukCheckFn    OriginalLarzukCheck{};
CheckItemTypeFn  CheckItemType{};
GetDataContextFn GetDataContext{};
GetDataTablesFn  GetDataTables{};
GetItemCodeFn    GetItemCode{};

std::atomic<bool> RuleActive{false};
std::atomic<bool> DiagnosticsActive{false};

std::atomic<std::uint64_t> Checks{};
std::atomic<std::uint64_t> Refused{};
std::atomic<std::uint64_t> NoTableChecks{};

std::array<std::atomic<bool>, MaximumTypeCodes> UnknownCodeLogged{};
std::array<std::atomic<bool>, MaximumTypeCodes> UnknownCodeSeen{};

constexpr std::size_t DiagnosticsSlots = 64;
std::array<std::atomic<std::uint32_t>, DiagnosticsSlots> LoggedItemCodes{};

template <typename T>
auto At(std::uint64_t rva) noexcept -> T {
    return reinterpret_cast<T>(Base + static_cast<std::uintptr_t>(rva));
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

// "char", "scha" as one quoted, comma separated string, for the log and console.
void DescribeCodes(char* output, std::size_t size) noexcept {
    output[0] = '\0';
    std::size_t used = 0;
    for (std::size_t index = 0; index < Config.codeCount && used + 1 < size; ++index) {
        const int written = std::snprintf(output + used, size - used, "%s\"%s\"", index == 0 ? "" : ", ",
                                          Config.codes[index].text);
        if (written < 0) {
            break;
        }
        used += static_cast<std::size_t>(written);
    }
    if (Config.codeCount == 0) {
        std::snprintf(output, size, "%s", "none");
    }
}

// ---------------------------------------------------------------------------
//  Configuration
// ---------------------------------------------------------------------------
auto LoadConfig() noexcept -> bool {
    Config = Settings{};
    std::string error;
    std::string unknown;
    if (!ParseConfig(DefaultConfigToml, Config, error, unknown)) {
        LogError("LarzukRestrictions: the built-in default configuration does not parse (%s).", error.c_str());
        return false;
    }
    std::snprintf(ConfigSource, sizeof(ConfigSource), "%s", "built-in defaults");

    if (!Context->EnsureConfig(DefaultConfigToml)) {
        LogWarn("LarzukRestrictions: could not create the default config, using built-in defaults.");
        return true;
    }

    std::string buffer(8192, '\0');
    std::uint32_t required = 0;
    if (!Context->ReadConfig(buffer.data(), static_cast<std::uint32_t>(buffer.size()), &required)) {
        if (required <= buffer.size()) {
            LogWarn("LarzukRestrictions: could not read the config, using built-in defaults.");
            return true;
        }
        buffer.assign(required, '\0');
        if (!Context->ReadConfig(buffer.data(), static_cast<std::uint32_t>(buffer.size()), &required)) {
            LogWarn("LarzukRestrictions: could not read the config, using built-in defaults.");
            return true;
        }
    }

    Settings parsed{};
    error.clear();
    unknown.clear();
    if (!ParseConfig(std::string_view(buffer.c_str()), parsed, error, unknown)) {
        LogError("LarzukRestrictions: config rejected (%s). Nothing is changed so a setting you meant is not "
                 "silently ignored; fix the file and restart.", error.c_str());
        return false;
    }
    if (!unknown.empty()) {
        LogWarn("LarzukRestrictions: ignoring unknown config setting(s): %s.", unknown.c_str());
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
    LogError("LarzukRestrictions: %s at RVA 0x%llX does not match D2RLoader 1.3.1.", name,
             static_cast<unsigned long long>(rva));
    return false;
}

// A function the plugin only calls may already carry another plugin's inline
// hook at its entry. The loader tracks those, and calling through one is fine.
auto CheckCallable(std::uint64_t rva, const std::uint8_t* bytes, std::size_t size, const char* name) noexcept -> bool {
    if (Context->CheckExpectedBytes(rva, bytes, static_cast<std::uint32_t>(size))) {
        return true;
    }
    if (IsTrackedInlineHook(rva, bytes, size)) {
        LogInfo("LarzukRestrictions: %s at RVA 0x%llX is hooked by another plugin, calling through it.", name,
                static_cast<unsigned long long>(rva));
        return true;
    }
    LogError("LarzukRestrictions: %s at RVA 0x%llX does not match D2RLoader 1.3.1.", name,
             static_cast<unsigned long long>(rva));
    return false;
}

auto ValidateRuleFunctions() noexcept -> bool {
    return CheckCallable(CheckItemTypeRva, CheckItemTypeEntry.data(), CheckItemTypeEntry.size(), "ITEMS_CheckItemTypeId")
        && CheckExact(CheckItemTypeWindowRva, CheckItemTypeWindow.data(), CheckItemTypeWindow.size(),
                      "the itemtypes lookup in ITEMS_CheckItemTypeId")
        && CheckCallable(GetDataContextRva, GetDataContextBody.data(), GetDataContextBody.size(), "UNITS_GetDataContext")
        && CheckCallable(GetDataTablesRva, GetDataTablesBody.data(), GetDataTablesBody.size(), "DATATBLS_GetTables")
        && CheckExact(ItemTypesLoaderRva, ItemTypesLoaderWindow.data(), ItemTypesLoaderWindow.size(),
                      "the itemtypes.txt row layout in its loader");
}

// ---------------------------------------------------------------------------
//  The rule, on native units
// ---------------------------------------------------------------------------
void NoteUnknownCode(std::size_t index, std::uint8_t context) noexcept {
    if (index >= MaximumTypeCodes) {
        return;
    }
    UnknownCodeSeen[index].store(true, std::memory_order_relaxed);
    if (!UnknownCodeLogged[index].exchange(true, std::memory_order_relaxed)) {
        LogWarn("LarzukRestrictions: item type code \"%s\" is not in itemtypes.txt (data context %u), so it refuses "
                "nothing. Fix refused_item_types.", Config.codes[index].text, static_cast<unsigned>(context));
    }
}

struct NativeEnvironment {
    auto ItemTypeRows(void* item, const std::uint8_t** rows, std::uint64_t* count, std::uint8_t* context) const noexcept -> bool {
        const std::uint8_t dataContext = GetDataContext(item);
        if (dataContext >= DataContextCount) {
            return false;
        }
        const std::uintptr_t tables = GetDataTables(dataContext);
        if (tables == 0) {
            return false;
        }
        const auto* first = reinterpret_cast<const std::uint8_t*>(
            static_cast<std::uintptr_t>(ReadU64(reinterpret_cast<const void*>(tables), ItemTypeRowsOffset)));
        const std::uint64_t rowCount = ReadU64(reinterpret_cast<const void*>(tables), ItemTypeCountOffset);
        if (first == nullptr || rowCount == 0 || rowCount > MaximumItemTypeRows) {
            return false;
        }
        *rows    = first;
        *count   = rowCount;
        *context = dataContext;
        return true;
    }
    auto IsOfType(void* item, std::int32_t row) const noexcept -> bool {
        return CheckItemType(item, row) != 0;
    }
    void UnknownCode(std::size_t index, std::uint8_t context) const noexcept {
        NoteUnknownCode(index, context);
    }
};

void NoteRefusedItem(void* item, std::size_t codeIndex) noexcept {
    const std::uint32_t code = GetItemCode(item);
    if (code == 0) {
        return;
    }
    for (auto& slot : LoggedItemCodes) {
        std::uint32_t current = slot.load(std::memory_order_relaxed);
        if (current == code) {
            return;
        }
        if (current != 0) {
            continue;
        }
        if (slot.compare_exchange_strong(current, code, std::memory_order_relaxed)) {
            char itemCode[5]{};
            UnpackItemCode(code, itemCode);
            LogInfo("LarzukRestrictions: Larzuk refuses item %s, of item type \"%s\".", itemCode,
                    Config.codes[codeIndex].text);
            return;
        }
        if (current == code) {
            return;
        }
    }
}

// ---------------------------------------------------------------------------
//  Hook
// ---------------------------------------------------------------------------
std::int32_t __fastcall HookLarzukCheck(void* item) noexcept {
    const std::int32_t stock = OriginalLarzukCheck(item);
    if (stock == 0 || item == nullptr || !RuleActive.load(std::memory_order_acquire)) {
        return stock;
    }
    Checks.fetch_add(1, std::memory_order_relaxed);

    NativeEnvironment env{};
    const auto evaluation = EvaluateItem(env, item, Config);
    switch (evaluation.outcome) {
    case Outcome::Refused:
        Refused.fetch_add(1, std::memory_order_relaxed);
        if (DiagnosticsActive.load(std::memory_order_relaxed)) {
            NoteRefusedItem(item, evaluation.code);
        }
        return 0;
    case Outcome::NoTable:
        NoTableChecks.fetch_add(1, std::memory_order_relaxed);
        break;
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
    CheckItemType  = At<CheckItemTypeFn>(CheckItemTypeRva);
    GetDataContext = At<GetDataContextFn>(GetDataContextRva);
    GetDataTables  = At<GetDataTablesFn>(GetDataTablesRva);

    if (Config.diagnostics) {
        if (CheckCallable(GetItemCodeRva, GetItemCodeEntry.data(), GetItemCodeEntry.size(), "ITEMS_GetItemCode")) {
            GetItemCode = At<GetItemCodeFn>(GetItemCodeRva);
            DiagnosticsActive.store(true, std::memory_order_relaxed);
        } else {
            LogWarn("LarzukRestrictions: diagnostics are off; the rule itself is not affected.");
        }
    }

    if (!Context->CheckExpectedBytes(LarzukCheckRva, LarzukCheckEntry.data(),
                                     static_cast<std::uint32_t>(LarzukCheckEntry.size()))) {
        if (IsTrackedInlineHook(LarzukCheckRva, LarzukCheckEntry.data(), LarzukCheckEntry.size())) {
            LogError("LarzukRestrictions: another plugin already hooks Larzuk's check at RVA 0x%llX.",
                     static_cast<unsigned long long>(LarzukCheckRva));
            SetInactive("another plugin already hooks Larzuk's check");
        } else {
            LogError("LarzukRestrictions: Larzuk's check at RVA 0x%llX does not match D2RLoader 1.3.1.",
                     static_cast<unsigned long long>(LarzukCheckRva));
            SetInactive("the game image does not match D2RLoader 1.3.1");
        }
        return;
    }

    // The rule reads Config only after this store, and Config never changes
    // afterwards. The loader writes the call-through pointer straight into
    // OriginalLarzukCheck, so it is set before the first hooked call.
    RuleActive.store(true, std::memory_order_release);
    if (!Context->InstallInlineHook(D2RL::MakeInlineHook(
            LarzukCheckRva, LarzukCheckEntry.data(), static_cast<std::uint32_t>(LarzukCheckEntry.size()),
            reinterpret_cast<void*>(&HookLarzukCheck), reinterpret_cast<void**>(&OriginalLarzukCheck)))
        || OriginalLarzukCheck == nullptr) {
        RuleActive.store(false, std::memory_order_relaxed);
        LogError("LarzukRestrictions: the loader refused the hook on Larzuk's check at RVA 0x%llX.",
                 static_cast<unsigned long long>(LarzukCheckRva));
        SetInactive("Larzuk's check could not be hooked");
        return;
    }
    HookInstalled = true;
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

    char codes[256]{};
    DescribeCodes(codes, sizeof(codes));

    char line[640]{};
    std::snprintf(line, sizeof(line), "Larzuk Restrictions %s (%s): refused item types %s; rule %s; diagnostics %s.%s%s",
                  PluginVersion, ConfigSource, codes, OnOff(HookInstalled), OnOff(DiagnosticsActive.load()),
                  InactiveReason[0] != '\0' ? " Inactive: " : "", InactiveReason);
    command->plugin->WriteConsoleMessage(line);

    std::snprintf(line, sizeof(line), "Items Larzuk would accept that were checked: %llu. Refused by item type: %llu.",
                  static_cast<unsigned long long>(Checks.load(std::memory_order_relaxed)),
                  static_cast<unsigned long long>(Refused.load(std::memory_order_relaxed)));
    command->plugin->WriteConsoleMessage(line);

    if (NoTableChecks.load(std::memory_order_relaxed) != 0) {
        std::snprintf(line, sizeof(line), "Checks with no itemtypes table for the item's data context (left stock): %llu.",
                      static_cast<unsigned long long>(NoTableChecks.load(std::memory_order_relaxed)));
        command->plugin->WriteConsoleMessage(line);
    }

    for (std::size_t index = 0; index < Config.codeCount; ++index) {
        if (UnknownCodeSeen[index].load(std::memory_order_relaxed)) {
            std::snprintf(line, sizeof(line), "Item type code \"%s\" is not in itemtypes.txt and refuses nothing.",
                          Config.codes[index].text);
            command->plugin->WriteConsoleMessage(line);
        }
    }
    return D2RL::ConsoleCommandResult::Handled;
}

void ResetState() noexcept {
    Config            = Settings{};
    HookInstalled     = false;
    InactiveReason[0] = '\0';
    OriginalLarzukCheck = nullptr;
    RuleActive.store(false, std::memory_order_relaxed);
    DiagnosticsActive.store(false, std::memory_order_relaxed);
    Checks.store(0, std::memory_order_relaxed);
    Refused.store(0, std::memory_order_relaxed);
    NoTableChecks.store(0, std::memory_order_relaxed);
    for (auto& flag : UnknownCodeLogged) {
        flag.store(false, std::memory_order_relaxed);
    }
    for (auto& flag : UnknownCodeSeen) {
        flag.store(false, std::memory_order_relaxed);
    }
    for (auto& slot : LoggedItemCodes) {
        slot.store(0, std::memory_order_relaxed);
    }
}

}  // namespace
}  // namespace CelestialRayOne::LarzukRestrictions

namespace {

constexpr D2RL::PluginInfo Info{
    .infoSize    = D2RL::PluginInfoSize,
    .abiVersion  = D2RL_PLUGIN_ABI_VERSION,
    .id          = "celestialrayone.larzuk-restrictions",
    .name        = "Larzuk Restrictions",
    .version     = CelestialRayOne::LarzukRestrictions::PluginVersion,
    .author      = "CelestialRayOne",
    .description = "Larzuk refuses to socket items of the configured item types (charms by default).",
    .flags       = D2RL::PluginFlags::Shared | D2RL::PluginFlags::NativeHooks,
};

}  // namespace

D2RL_PLUGIN_EXPORT auto D2RLoaderGetPluginInfo() noexcept -> const D2RL::PluginInfo* {
    return &Info;
}

D2RL_PLUGIN_EXPORT auto D2RLoaderLoadPlugin(const D2RL::PluginContext* context) noexcept -> bool {
    using namespace CelestialRayOne::LarzukRestrictions;

    if (!D2RL::HasContext(context) || context->exeBase == 0) {
        return false;
    }
    Context = context;
    Base    = context->exeBase;
    ResetState();

    if (!context->RegisterConsoleCommand("larzukrestrictions", StatusCommand,
                                         "Show Larzuk Restrictions settings, state and counters.")) {
        LogWarn("LarzukRestrictions: the larzukrestrictions console command could not be registered.");
    }

    if (!LoadConfig()) {
        Config.enabled = false;
        SetInactive("the config was rejected, see the plugin log");
        return true;
    }
    if (!Config.enabled) {
        SetInactive("disabled by config");
        LogInfo("Larzuk Restrictions %s loaded disabled by config; the game runs stock.", PluginVersion);
        return true;
    }
    if (Config.codeCount == 0) {
        SetInactive("refused_item_types is empty");
        LogInfo("Larzuk Restrictions %s loaded with an empty refused_item_types; the game runs stock.", PluginVersion);
        return true;
    }

    Install();

    char codes[256]{};
    DescribeCodes(codes, sizeof(codes));
    LogInfo("Larzuk Restrictions %s by CelestialRayOne: refused item types %s, rule %s, diagnostics %s, config %s, build %s.",
            PluginVersion, codes, OnOff(HookInstalled), OnOff(DiagnosticsActive.load()), ConfigSource,
            context->buildName != nullptr ? context->buildName : "unknown");
    return true;
}

D2RL_PLUGIN_EXPORT void D2RLoaderUnloadPlugin() noexcept {
    using namespace CelestialRayOne::LarzukRestrictions;
    if (Context == nullptr) {
        return;
    }
    // D2RLoader removes the inline hook itself. Until it does, the hook answers stock.
    RuleActive.store(false, std::memory_order_relaxed);
    DiagnosticsActive.store(false, std::memory_order_relaxed);
}
