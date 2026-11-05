// Uber Affixes
//
// Sets the affixes (monumod.txt unique monster mods) the six uber bosses get
// when they spawn, replacing the fixed list the game hardcodes for each one.
// See celestialrayone.uber-affixes.toml for the behaviour write-up.
//
// Read out of the live D2RLoader 1.3.1 image (D2R 3.3.93847, base
// 0x140000000) in IDA before being relied on:
//
//   0x494A60 (spawn, monster), the special-monster setup. Its only code
//       reference is the call at 0x4943F2 in the monster spawn 0x493CC0,
//       which ignores the return value, so it runs once for every monster
//       that spawns. spawn+0x00 is the game (mov rdi,[rcx] at 0x494A78) and
//       spawn+0x18 the monstats.txt row (mov ebp,[rcx+18h] at 0x494A72). It
//       switches on the row's base monster, and for the six uber rows it does
//       nothing but add affixes through AssignUMod with unique = 1:
//         704 Uber Mephisto  22, 30, 17, 8, 6
//         705 Uber Diablo    22, 8, 6
//         706 Uber Izual     6, 18
//         707 Lilith         23, 6, 29
//         708 Uber Duriel    6, 18
//         709 Uber Baal      22, 18, 8, 6
//
//   0x4995E0 AssignUMod (game, monster, mod, unique). With unique set it first
//       runs MarkUnique, then writes the mod into the first free byte of the
//       monster's 9-byte affix list and runs the mod's init callback from the
//       table at 0x2395FE0 as (monster, mod, unique). It has no bound check on
//       the mod id, which is why the TOML only accepts 1 to 42.
//
//   0x49EED0 MarkUnique (game, monster). Counts the monster in its level's
//       unique counter (only when it is not unique yet and not alignment 2),
//       then sets rank flag 8 (unique).
//
//   Row 22 (questcomplete) has a mode-change callback, 0x4A1480 (slot 1 of
//       its row in the affix table at 0x2396210), and that routine is what
//       calls the uber Tristram death routine 0x5A8450 for 704, 705 and 709.
//
// What the plugin does: 0x494A60's entry is hooked. For a row listed in the
// TOML (only 704-709 are accepted), it calls MarkUnique once, then AssignUMod
// with unique = 1 for each listed mod in order, and returns without running
// the native routine. Every other monster runs the native routine untouched.
// The prologue, the 77 bytes after it, and the entries of AssignUMod and
// MarkUnique are compared byte for byte first; on any mismatch nothing is
// installed and the plugin log says why.

#include <D2RLPlugin/api.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

namespace CelestialRayOne::UberAffixes {
namespace {

constexpr char PluginVersion[] = "1.0.0";

// ---------------------------------------------------------------------------
// Native sites
// ---------------------------------------------------------------------------

// 0x494A60: mov [rsp+18h],rbx / mov [rsp+20h],rbp / push rsi / push rdi /
// push r15 / sub rsp,30h. Eighteen bytes, no relative operands.
constexpr std::uint64_t SpecialSetupRva = 0x494A60;
constexpr std::uint8_t  SpecialSetupPrologue[]{
    0x48, 0x89, 0x5C, 0x24, 0x18, 0x48, 0x89, 0x6C, 0x24, 0x20, 0x56, 0x57, 0x41, 0x57, 0x48, 0x83,
    0xEC, 0x30,
};

// 0x494A72..0x494ABE: the spawn record reads, the monstats.txt lookup of the
// row and of its base monster. Pins the two spawn record offsets used below.
constexpr std::uint64_t SpecialSetupBodyRva = 0x494A72;
constexpr std::uint8_t  SpecialSetupBody[]{
    0x8B, 0x69, 0x18,                          // mov ebp, [rcx+18h]   monstats.txt row
    0x48, 0x8B, 0xDA,                          // mov rbx, rdx         monster
    0x48, 0x8B, 0x39,                          // mov rdi, [rcx]       game
    0x48, 0x8B, 0xF1, 0x4C, 0x8B, 0x79, 0x08, 0x8B, 0xD5, 0x0F, 0xB6, 0x8F, 0x06, 0x01, 0x00, 0x00,
    0xE8, 0x50, 0x2C, 0xC0, 0xFF, 0x48, 0x85, 0xC0, 0x75, 0x15, 0x48, 0x8D, 0x4C, 0x24, 0x50, 0x88,
    0x44, 0x24, 0x50, 0xE8, 0x6D, 0x20, 0xC0, 0xFF, 0x84, 0xC0, 0x74, 0x07, 0xCC, 0xEB, 0x04, 0x0F,
    0xBF, 0x68, 0x32, 0x48, 0x8B, 0x06, 0x8B, 0xD5, 0x0F, 0xB6, 0x88, 0x06, 0x01, 0x00, 0x00, 0xE8,
    0xB1, 0x09, 0xC7, 0xFF,
};

constexpr std::uint64_t AssignUModRva = 0x4995E0;
constexpr std::uint8_t  AssignUModEntry[]{
    0x48, 0x89, 0x5C, 0x24, 0x18, 0x55, 0x56, 0x41, 0x56, 0x48, 0x83, 0xEC, 0x20, 0x49, 0x63, 0xE8,
    0x45, 0x8B, 0xF1, 0x48, 0x8B, 0xDA, 0x48, 0x8B, 0xF1, 0x48, 0x85, 0xD2, 0x74, 0x0D, 0x48, 0x8B,
    0xCA,
};

constexpr std::uint64_t MarkUniqueRva = 0x49EED0;
constexpr std::uint8_t  MarkUniqueEntry[]{
    0x48, 0x89, 0x5C, 0x24, 0x08, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B, 0xDA, 0x48, 0x8B, 0xF9,
    0x48, 0x85, 0xD2, 0x74, 0x0D, 0x48, 0x8B, 0xCA,
};

constexpr std::size_t SpawnGameOffset = 0x00;
constexpr std::size_t SpawnRowOffset  = 0x18;

static_assert(SpecialSetupRva + sizeof(SpecialSetupPrologue) == SpecialSetupBodyRva,
    "The body witness must start right after the hooked prologue.");
static_assert(sizeof(SpecialSetupBody) == 77, "The body witness ends at 0x494ABF.");
static_assert(SpecialSetupBody[0] == 0x8B && SpecialSetupBody[1] == 0x69 && SpecialSetupBody[2] == SpawnRowOffset,
    "mov ebp,[rcx+18h] reads the monstats.txt row.");
static_assert(SpecialSetupBody[6] == 0x48 && SpecialSetupBody[7] == 0x8B && SpecialSetupBody[8] == 0x39
    && SpawnGameOffset == 0, "mov rdi,[rcx] reads the game.");

using SpecialSetupFn = std::uint64_t(__fastcall*)(void* spawn, void* monster) noexcept;
using AssignUModFn   = std::uint64_t(__fastcall*)(void* game, void* monster, std::int32_t mod, std::uint32_t unique) noexcept;
using MarkUniqueFn   = std::uint64_t(__fastcall*)(void* game, void* monster) noexcept;

// ---------------------------------------------------------------------------
// Rows and ids
// ---------------------------------------------------------------------------

constexpr std::int32_t FirstUberRow = 704;
constexpr std::int32_t LastUberRow  = 709;
constexpr std::size_t  UberCount    = static_cast<std::size_t>(LastUberRow - FirstUberRow + 1);

constexpr std::array<const char*, UberCount> UberNames{
    "Uber Mephisto", "Uber Diablo", "Uber Izual", "Lilith", "Uber Duriel", "Uber Baal",
};

constexpr std::int32_t MinimumAffixId = 1;
constexpr std::int32_t MaximumAffixId = 42;
constexpr std::size_t  MaximumAffixes = 9;     // the monster's affix list holds 9

// monumod.txt "uniquemod" column, rows 0-42, for the log and the console.
constexpr std::array<const char*, MaximumAffixId + 1> AffixCodes{
    "none", "rndname", "hpmultiply", "light", "leveladd", "strong", "fast", "curse", "resist", "fire",
    "poisondead", "durieldead", "bloodraven", "rage", "spcdamage", "partydead", "champion", "lightning", "cold",
    "hireable", "scarab", "killself", "questcomplete", "poisonhit", "thief", "manahit", "teleport", "spectralhit",
    "stoneskin", "multishot", "aura", "goboom", "firespike_explode", "suicideminion_explode", "ai_after_death",
    "shatter_on_death", "ghostly", "fanatic", "possessed", "berserk", "worms_on_death", "always_run_ai",
    "lightningdeath",
};

// ---------------------------------------------------------------------------
// Config
// ---------------------------------------------------------------------------

constexpr char MainTable[] = "uber_affixes";
constexpr char BossTable[] = "uber_affixes.boss";
constexpr std::size_t InitialConfigBytes = 16'384;
constexpr std::size_t MaximumConfigBytes = 1'048'576;

constexpr char DefaultConfigToml[] = R"toml(# Uber Affixes
#
# Sets the affixes (monumod.txt unique monster mods) of the six uber bosses.
# The game hardcodes a fixed list for each of them by monstats.txt row and
# adds it every time the boss spawns. A boss listed below gets exactly the
# list written here instead. A boss not listed keeps the game's own list, and
# no other monster is touched.
#
# Only these six monstats.txt rows (*hcIdx) can be listed, because they are
# the ones the game gives nothing but affixes:
#   704 Uber Mephisto     game's list: 22, 30, 17, 8, 6
#   705 Uber Diablo       game's list: 22, 8, 6
#   706 Uber Izual        game's list: 6, 18
#   707 Lilith            game's list: 23, 6, 29
#   708 Uber Duriel       game's list: 6, 18
#   709 Uber Baal         game's list: 22, 18, 8, 6
#
# monumod.txt ids (the "id" column) useful here:
#   5 Extra Strong        17 Lightning Enchanted    27 Spectral Hit
#   6 Extra Fast          18 Cold Enchanted         28 Stone Skin
#   7 Cursed              22 questcomplete          29 Multiple Shots
#   8 Magic Resistant     23 poisonhit              30 Aura Enchanted
#   9 Fire Enchanted      25 Mana Burn
#                         26 Teleportation
#
# Rules
#   - 22 (questcomplete) is not an affix and shows no name. On Uber Mephisto,
#     Uber Diablo and Uber Baal it is what runs the uber Tristram death
#     handling when they die (the Standard of Heroes drop). Keep it on those
#     three.
#   - Uber Mephisto's Conviction comes from Aura Enchanted (30): the game
#     forces Conviction level 20 on row 704 whenever it has that affix.
#   - Every boss listed here is still made a unique (unique rank, counted as
#     a unique in its level), even with an empty list, exactly like the game
#     does today.
#   - What each affix does is decided by the game and by the monster-affixes
#     plugin, not here. This file only decides who gets which.
#   - At most 9 affixes per boss, ids 1 to 42. An id listed twice counts once.
#   - Any mistake (a row other than 704-709, a row listed twice, a bad id)
#     stops the plugin from installing, and the plugin log says why. The
#     game then keeps all six of its own lists.
#
# Server side only. Single player hosts its own server, so it applies there.
# Takes effect for bosses that spawn after the game starts.
#
# Console command: uberaffixes (status, the lists, how often each was applied)

[uber_affixes]

# Master switch. false installs nothing: every uber keeps the game's list.
enabled = true

# Uber Mephisto: keeps his Conviction aura.
[[uber_affixes.boss]]
monster = 704
affixes = [22, 30]

# Uber Diablo: Spectral Hit.
[[uber_affixes.boss]]
monster = 705
affixes = [22, 27]

# Uber Izual: Cold Enchanted.
[[uber_affixes.boss]]
monster = 706
affixes = [18]

# Lilith: no affixes.
[[uber_affixes.boss]]
monster = 707
affixes = []

# Uber Duriel: Stone Skin.
[[uber_affixes.boss]]
monster = 708
affixes = [28]

# Uber Baal: Magic Resistant and Mana Burn.
[[uber_affixes.boss]]
monster = 709
affixes = [22, 8, 25]
)toml";

struct Boss {
    bool                                     listed = false;
    std::array<std::uint8_t, MaximumAffixes> affixes{};
    std::size_t                              count = 0;
};

struct BossDraft {
    int                       line = 0;
    bool                      monsterSet = false;
    std::int32_t              monster = -1;
    bool                      affixesSet = false;
    std::vector<std::int32_t> affixes;
    bool                      broken = false;
};

enum class InstallState : std::uint8_t {
    NotAttempted, Armed, DisabledByConfig, ConfigError, UnsupportedBuild, InstallFailed,
};

const D2RL::PluginContext* Context = nullptr;
std::uintptr_t             Base    = 0;

bool Enabled           = true;
bool ConfigFileWasRead = false;
int  ConfigErrors      = 0;

std::array<Boss, UberCount>                       Bosses{};
std::array<std::atomic<std::uint64_t>, UberCount> Applied{};
std::atomic<InstallState>                         State{ InstallState::NotAttempted };
std::atomic<bool>                                 Armed{ false };

SpecialSetupFn OriginalSpecialSetup = nullptr;
AssignUModFn   AssignUMod           = nullptr;
MarkUniqueFn   MarkUnique           = nullptr;

// ---------------------------------------------------------------------------
// Small helpers
// ---------------------------------------------------------------------------

template <typename Function>
auto At(std::uint64_t rva) noexcept -> Function {
    return reinterpret_cast<Function>(Base + rva);
}

template <typename T>
auto ReadAt(const void* base, std::size_t offset) noexcept -> T {
    T value{};
    std::memcpy(&value, static_cast<const std::uint8_t*>(base) + offset, sizeof(T));
    return value;
}

auto AffixText(std::int32_t id) noexcept -> const char* {
    return id >= 0 && id <= MaximumAffixId ? AffixCodes[static_cast<std::size_t>(id)] : "?";
}

auto ListText(const Boss& boss) -> std::string {
    if (boss.count == 0) return "no affixes";
    std::string out;
    for (std::size_t index = 0; index < boss.count; ++index) {
        if (index != 0) out += ", ";
        char item[48]{};
        std::snprintf(item, sizeof(item), "%u %s", static_cast<unsigned>(boss.affixes[index]),
            AffixText(boss.affixes[index]));
        out += item;
    }
    return out;
}

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

// Cuts a # comment, ignoring # inside "basic" and 'literal' strings.
auto StripComment(std::string_view line) noexcept -> std::string_view {
    bool inBasic = false;
    bool inLiteral = false;
    bool escaped = false;
    for (std::size_t index = 0; index < line.size(); ++index) {
        const char character = line[index];
        if (inBasic) {
            if (escaped) escaped = false;
            else if (character == '\\') escaped = true;
            else if (character == '"') inBasic = false;
            continue;
        }
        if (inLiteral) {
            if (character == '\'') inLiteral = false;
            continue;
        }
        if (character == '"') inBasic = true;
        else if (character == '\'') inLiteral = true;
        else if (character == '#') return line.substr(0, index);
    }
    return line;
}

auto WithoutSpaces(std::string_view text) -> std::string {
    std::string out;
    for (const char character : text) {
        if (character != ' ' && character != '\t') out.push_back(character);
    }
    return out;
}

void ConfigError(int line, const char* format, ...) noexcept {
    char message[512]{};
    va_list args;
    va_start(args, format);
    std::vsnprintf(message, sizeof(message), format, args);
    va_end(args);
    ++ConfigErrors;
    D2RL::LogErrorF(Context, "UberAffixes: config line %d: %s", line, message);
}

void ConfigWarning(int line, const char* format, ...) noexcept {
    char message[512]{};
    va_list args;
    va_start(args, format);
    std::vsnprintf(message, sizeof(message), format, args);
    va_end(args);
    D2RL::LogWarnF(Context, "UberAffixes: config line %d: %s", line, message);
}

// Decimal, optional leading '+', '_' separators allowed as in TOML.
auto ParseWhole(std::string_view text, std::int32_t limit, std::int32_t& out) noexcept -> bool {
    std::size_t index = 0;
    if (!text.empty() && text[0] == '+') index = 1;
    std::int32_t value = 0;
    bool anyDigit = false;
    for (; index < text.size(); ++index) {
        const char character = text[index];
        if (character == '_') continue;
        if (character < '0' || character > '9') return false;
        anyDigit = true;
        value = value * 10 + (character - '0');
        if (value > limit) return false;
    }
    if (!anyDigit) return false;
    out = value;
    return true;
}

// The text between the brackets, lines already joined with commas.
void ApplyAffixList(std::string_view text, BossDraft& boss, int line) {
    std::size_t cursor = 0;
    while (cursor <= text.size()) {
        const std::size_t comma = text.find(',', cursor);
        const std::size_t end = comma == std::string_view::npos ? text.size() : comma;
        const std::string_view element = Trim(text.substr(cursor, end - cursor));
        if (!element.empty()) {
            std::int32_t affix = 0;
            if (!ParseWhole(element, 0x7FFF, affix) || affix < MinimumAffixId || affix > MaximumAffixId) {
                const std::string shown(element.substr(0, 24));
                ConfigError(line, "affixes entry '%s' is not a monumod.txt id from %d to %d.", shown.c_str(),
                    MinimumAffixId, MaximumAffixId);
                boss.broken = true;
            } else if (std::find(boss.affixes.begin(), boss.affixes.end(), affix) != boss.affixes.end()) {
                ConfigWarning(line, "affix %d (%s) is listed twice for the same boss; it counts once.", affix,
                    AffixText(affix));
            } else {
                boss.affixes.push_back(affix);
            }
        }
        if (comma == std::string_view::npos) break;
        cursor = comma + 1;
    }
}

enum class Table : std::uint8_t { Root, Main, Boss, Other };

void ParseConfig(std::string_view text, std::vector<BossDraft>& bosses) {
    Table       table = Table::Root;
    bool        capturing = false;
    int         captureLine = 0;
    std::string arrayText;
    int         lineNumber = 0;

    std::size_t cursor = 0;
    while (cursor < text.size()) {
        std::size_t end = text.find('\n', cursor);
        if (end == std::string_view::npos) end = text.size();
        ++lineNumber;
        const std::string_view line = Trim(StripComment(text.substr(cursor, end - cursor)));
        cursor = end + 1;

        if (capturing) {
            if (!line.empty() && line.front() == '[') {
                ConfigError(captureLine, "affixes is missing its closing ']'.");
                bosses.back().broken = true;
                capturing = false;
            } else {
                const std::size_t close = line.find(']');
                arrayText.append(line.substr(0, close));
                arrayText.push_back(',');
                if (close == std::string_view::npos) continue;
                capturing = false;
                if (!Trim(line.substr(close + 1)).empty()) {
                    ConfigError(lineNumber, "unexpected text after the closing ']' of affixes.");
                    bosses.back().broken = true;
                }
                ApplyAffixList(arrayText, bosses.back(), captureLine);
                continue;
            }
        }

        if (line.empty()) continue;

        if (line.front() == '[') {
            const bool arrayTable = line.size() >= 2 && line[1] == '[';
            const std::size_t close = line.find(arrayTable ? "]]" : "]");
            if (close == std::string_view::npos) {
                ConfigError(lineNumber, "table header without its closing bracket.");
                table = Table::Other;
                continue;
            }
            const std::size_t open = arrayTable ? 2 : 1;
            const std::string name = WithoutSpaces(line.substr(open, close - open));
            if (arrayTable && name == BossTable) {
                BossDraft draft{};
                draft.line = lineNumber;
                bosses.push_back(std::move(draft));
                table = Table::Boss;
            } else if (!arrayTable && name == MainTable) {
                table = Table::Main;
            } else if (name == BossTable || name == MainTable) {
                ConfigError(lineNumber, "%s needs %s brackets.", name.c_str(),
                    arrayTable ? "single [ ]" : "double [[ ]]");
                table = Table::Other;
            } else {
                ConfigWarning(lineNumber, "unknown table [%s] ignored.", name.c_str());
                table = Table::Other;
            }
            continue;
        }

        const std::size_t equals = line.find('=');
        if (equals == std::string_view::npos) {
            ConfigError(lineNumber, "expected key = value.");
            if (table == Table::Boss) bosses.back().broken = true;
            continue;
        }
        const std::string_view key = Trim(line.substr(0, equals));
        const std::string_view value = Trim(line.substr(equals + 1));
        const std::string keyText(key);

        if (table == Table::Main) {
            if (key == "enabled") {
                if (value == "true") Enabled = true;
                else if (value == "false") Enabled = false;
                else ConfigError(lineNumber, "enabled must be true or false.");
            } else {
                ConfigWarning(lineNumber, "unknown key '%s' ignored.", keyText.c_str());
            }
            continue;
        }
        if (table != Table::Boss) {
            ConfigWarning(lineNumber, "key '%s' outside [%s] and [[%s]] ignored.", keyText.c_str(), MainTable,
                BossTable);
            continue;
        }

        BossDraft& boss = bosses.back();
        if (key == "monster") {
            if (boss.monsterSet) {
                ConfigError(lineNumber, "monster is given twice in the same block.");
                boss.broken = true;
                continue;
            }
            boss.monsterSet = true;
            std::int32_t row = 0;
            if (!ParseWhole(value, 0x7FFF, row)) {
                ConfigError(lineNumber, "monster must be a monstats.txt *hcIdx number.");
                boss.broken = true;
            } else {
                boss.monster = row;
            }
        } else if (key == "affixes") {
            if (boss.affixesSet) {
                ConfigError(lineNumber, "affixes is given twice in the same block.");
                boss.broken = true;
                continue;
            }
            boss.affixesSet = true;
            if (value.empty() || value.front() != '[') {
                ConfigError(lineNumber, "affixes must be a list in square brackets, for example [22, 30] or [].");
                boss.broken = true;
                continue;
            }
            const std::size_t close = value.find(']');
            if (close == std::string_view::npos) {
                arrayText.assign(value.substr(1));
                arrayText.push_back(',');
                capturing = true;
                captureLine = lineNumber;
                continue;
            }
            if (!Trim(value.substr(close + 1)).empty()) {
                ConfigError(lineNumber, "unexpected text after the closing ']' of affixes.");
                boss.broken = true;
            }
            ApplyAffixList(value.substr(1, close - 1), boss, lineNumber);
        } else {
            ConfigWarning(lineNumber, "unknown key '%s' ignored.", keyText.c_str());
        }
    }

    if (capturing) {
        ConfigError(captureLine, "affixes is missing its closing ']'.");
        bosses.back().broken = true;
    }
}

auto ReadConfigText(std::string& out) noexcept -> bool {
    try {
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
    } catch (...) {
        return false;
    }
}

// False when anything is wrong: nothing is installed then, so a typo can never
// hand a boss the wrong list.
auto ReadConfiguration() noexcept -> bool {
    try {
        std::string text;
        if (!Context->EnsureConfig(DefaultConfigToml)) {
            Context->LogWarn("UberAffixes: the config file could not be created; using the embedded defaults.");
            text = DefaultConfigToml;
        } else if (!ReadConfigText(text)) {
            Context->LogWarn("UberAffixes: the config file could not be read; using the embedded defaults.");
            text = DefaultConfigToml;
        } else {
            ConfigFileWasRead = true;
        }

        ConfigErrors = 0;
        std::vector<BossDraft> drafts;
        ParseConfig(text, drafts);

        std::array<int, UberCount> seenAt{};
        for (BossDraft& draft : drafts) {
            if (!draft.monsterSet) {
                ConfigError(draft.line, "this block has no monster.");
                continue;
            }
            if (!draft.affixesSet) {
                ConfigError(draft.line, "this block has no affixes list (write affixes = [] for none).");
                continue;
            }
            if (draft.broken) continue;
            if (draft.monster < FirstUberRow || draft.monster > LastUberRow) {
                ConfigError(draft.line, "monster %d is not one of the six uber rows (704 to 709). Only those get a "
                    "list from the game that holds nothing but affixes.", draft.monster);
                continue;
            }
            const std::size_t index = static_cast<std::size_t>(draft.monster - FirstUberRow);
            if (seenAt[index] != 0) {
                ConfigError(draft.line, "monster %d (%s) already has a block at line %d.", draft.monster,
                    UberNames[index], seenAt[index]);
                continue;
            }
            seenAt[index] = draft.line;
            if (draft.affixes.size() > MaximumAffixes) {
                ConfigError(draft.line, "monster %d (%s) has %zu affixes; a monster holds at most %zu.",
                    draft.monster, UberNames[index], draft.affixes.size(), MaximumAffixes);
            }
        }

        if (ConfigErrors > 0) {
            D2RL::LogErrorF(Context, "UberAffixes: NOT installed, %d config error%s (see above). Every uber keeps "
                "the game's own affixes.", ConfigErrors, ConfigErrors == 1 ? "" : "s");
            return false;
        }

        Bosses = {};
        for (const BossDraft& draft : drafts) {
            Boss& boss = Bosses[static_cast<std::size_t>(draft.monster - FirstUberRow)];
            boss.listed = true;
            boss.count = draft.affixes.size();
            for (std::size_t index = 0; index < boss.count; ++index) {
                boss.affixes[index] = static_cast<std::uint8_t>(draft.affixes[index]);
            }
        }
        return true;
    } catch (...) {
        Context->LogError("UberAffixes: NOT installed, out of memory while reading the config.");
        return false;
    }
}

// ---------------------------------------------------------------------------
// The hook
// ---------------------------------------------------------------------------

std::uint64_t __fastcall HookSpecialSetup(void* spawn, void* monster) noexcept {
    if (spawn != nullptr && monster != nullptr && Armed.load(std::memory_order_acquire)) {
        const std::int32_t row = ReadAt<std::int32_t>(spawn, SpawnRowOffset);
        if (row >= FirstUberRow && row <= LastUberRow) {
            const std::size_t index = static_cast<std::size_t>(row - FirstUberRow);
            const Boss& boss = Bosses[index];
            void* const game = ReadAt<void*>(spawn, SpawnGameOffset);
            if (boss.listed && game != nullptr) {
                // The native routine makes every uber a unique through its first
                // AssignUMod call; an empty list must not lose that.
                MarkUnique(game, monster);
                for (std::size_t slot = 0; slot < boss.count; ++slot) {
                    AssignUMod(game, monster, boss.affixes[slot], 1);
                }
                Applied[index].fetch_add(1, std::memory_order_relaxed);
                return 0;  // the only caller ignores the result
            }
        }
    }
    return OriginalSpecialSetup(spawn, monster);
}

// ---------------------------------------------------------------------------
// Install
// ---------------------------------------------------------------------------

auto Verify(std::uint64_t rva, const std::uint8_t* expected, std::size_t size, const char* label) noexcept -> bool {
    if (Context->CheckExpectedBytes(rva, expected, static_cast<std::uint32_t>(size))) return true;
    D2RL::LogErrorF(Context, "UberAffixes: NOT installed. %s at 0x%llX does not match the verified D2R image, or "
        "another plugin already owns it. Every uber keeps the game's own affixes.", label,
        static_cast<unsigned long long>(rva));
    return false;
}

void LogLists() noexcept {
    try {
        for (std::size_t index = 0; index < UberCount; ++index) {
            const Boss& boss = Bosses[index];
            const int row = FirstUberRow + static_cast<int>(index);
            if (boss.listed) {
                D2RL::LogInfoF(Context, "UberAffixes:   %d %s: %s.", row, UberNames[index], ListText(boss).c_str());
            } else {
                D2RL::LogInfoF(Context, "UberAffixes:   %d %s: not listed, keeps the game's own affixes.", row,
                    UberNames[index]);
            }
        }
    } catch (...) {
    }
}

void Install(bool configUsable) noexcept {
    if (!configUsable) {
        State.store(InstallState::ConfigError, std::memory_order_release);
        return;
    }
    if (!Enabled) {
        State.store(InstallState::DisabledByConfig, std::memory_order_release);
        Context->LogInfo("UberAffixes: disabled in the config file. Every uber keeps the game's own affixes.");
        return;
    }
    if (!Verify(SpecialSetupBodyRva, SpecialSetupBody, sizeof(SpecialSetupBody), "The special monster setup")
            || !Verify(AssignUModRva, AssignUModEntry, sizeof(AssignUModEntry), "AssignUMod")
            || !Verify(MarkUniqueRva, MarkUniqueEntry, sizeof(MarkUniqueEntry), "MarkUnique")) {
        State.store(InstallState::UnsupportedBuild, std::memory_order_release);
        return;
    }

    AssignUMod = At<AssignUModFn>(AssignUModRva);
    MarkUnique = At<MarkUniqueFn>(MarkUniqueRva);

    if (!Context->InstallInlineHook(SpecialSetupRva, SpecialSetupPrologue,
            static_cast<std::uint32_t>(sizeof(SpecialSetupPrologue)), &HookSpecialSetup, &OriginalSpecialSetup)
            || OriginalSpecialSetup == nullptr) {
        State.store(InstallState::InstallFailed, std::memory_order_release);
        Context->LogError("UberAffixes: NOT installed. The inline hook at 0x494A60 (special monster setup) was "
            "refused. Every uber keeps the game's own affixes.");
        return;
    }

    Armed.store(true, std::memory_order_release);
    State.store(InstallState::Armed, std::memory_order_release);
    D2RL::LogInfoF(Context, "UberAffixes %s: active (config %s).", PluginVersion,
        ConfigFileWasRead ? "loaded" : "NOT READ, embedded defaults");
    LogLists();
}

// ---------------------------------------------------------------------------
// Console command
// ---------------------------------------------------------------------------

auto StateText(InstallState state) noexcept -> const char* {
    switch (state) {
    case InstallState::Armed:            return "active";
    case InstallState::DisabledByConfig: return "off in the config file (game's own affixes)";
    case InstallState::ConfigError:      return "NOT ACTIVE, config errors, see the log (game's own affixes)";
    case InstallState::UnsupportedBuild: return "NOT ACTIVE, unrecognised game build, see the log";
    case InstallState::InstallFailed:    return "NOT ACTIVE, hook refused, see the log";
    case InstallState::NotAttempted:
    default:                             return "NOT ACTIVE, never attempted";
    }
}

auto __cdecl StatusCommand(D2R::Game::Client*, const D2RL::ConsoleCommandContext* command, void*) noexcept
        -> D2RL::ConsoleCommandResult {
    if (command == nullptr || command->plugin == nullptr) return D2RL::ConsoleCommandResult::Failed;
    const D2RL::PluginContext* plugin = command->plugin;

    char line[384]{};
    std::snprintf(line, sizeof(line), "Uber Affixes %s: %s | config %s", PluginVersion,
        StateText(State.load(std::memory_order_acquire)), ConfigFileWasRead ? "loaded" : "NOT READ (embedded defaults)");
    plugin->WriteConsoleMessage(line);

    try {
        for (std::size_t index = 0; index < UberCount; ++index) {
            const Boss& boss = Bosses[index];
            const int row = FirstUberRow + static_cast<int>(index);
            if (boss.listed) {
                std::snprintf(line, sizeof(line), "%d %s: %s | applied %llu", row, UberNames[index],
                    ListText(boss).c_str(),
                    static_cast<unsigned long long>(Applied[index].load(std::memory_order_relaxed)));
            } else {
                std::snprintf(line, sizeof(line), "%d %s: not listed, game's own affixes", row, UberNames[index]);
            }
            plugin->WriteConsoleMessage(line);
        }
    } catch (...) {
        return D2RL::ConsoleCommandResult::Failed;
    }
    return D2RL::ConsoleCommandResult::Handled;
}

constexpr D2RL::PluginInfo Info{
    .infoSize    = D2RL::PluginInfoSize,
    .abiVersion  = D2RL_PLUGIN_ABI_VERSION,
    .id          = "celestialrayone.uber-affixes",
    .name        = "Uber Affixes",
    .version     = PluginVersion,
    .author      = "CelestialRayOne",
    .description = "Sets the affixes the six uber bosses spawn with, replacing the lists the game hardcodes.",
    // Server: monster spawns are decided by the server.
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

    // Registered first, and the load always succeeds: returning false would
    // unload the DLL and take the command with it.
    if (!Context->RegisterConsoleCommand("uberaffixes", &StatusCommand,
            "Reports Uber Affixes status and the affixes each uber boss gets.")) {
        Context->LogWarn("UberAffixes: the status console command was refused.");
    }

    const bool configUsable = ReadConfiguration();
    Install(configUsable);
    return true;
}

D2RL_PLUGIN_EXPORT void D2RLoaderUnloadPlugin() noexcept {
    // The loader owns the hook; from here on it hands every call to the game.
    Armed.store(false, std::memory_order_release);
}

}  // namespace CelestialRayOne::UberAffixes
