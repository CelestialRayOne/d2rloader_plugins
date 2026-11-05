#include "filter_program.h"

#include "json_value.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>

namespace lootfilter {

// ---------------------------------------------------------------------------
// Small model helpers
// ---------------------------------------------------------------------------

auto Text::Pick(std::string_view language) const noexcept -> const std::string& {
    static const std::string empty;
    if (variants.empty()) return empty;
    for (const auto& [code, text] : variants) {
        if (code == language) return text;
    }
    for (const auto& [code, text] : variants) {
        if (code.empty() || code == "enUS") return text;
    }
    return variants.front().second;
}

auto IntSpec::Matches(int value) const noexcept -> bool {
    switch (kind) {
    case Kind::Any: return true;
    case Kind::Values: return std::find(values.begin(), values.end(), value) != values.end();
    case Kind::Range:
        if (hasMin && value < min) return false;
        if (hasMax && value > max) return false;
        return true;
    }
    return true;
}

auto StatCheck::Passes(int actual) const noexcept -> bool {
    switch (op) {
    case StatOp::Equal:          return actual == value;
    case StatOp::NotEqual:       return actual != value;
    case StatOp::Greater:        return actual > value;
    case StatOp::GreaterOrEqual: return actual >= value;
    case StatOp::Less:           return actual < value;
    case StatOp::LessOrEqual:    return actual <= value;
    case StatOp::Between:        return actual >= value && actual <= value2;
    }
    return false;
}

void Actions::FillFrom(const Actions& other) {
    if (!hide) hide = other.hide;
    if (!hideIfOtherConditionsFail) hideIfOtherConditionsFail = other.hideIfOtherConditionsFail;
    if (!replaceName) replaceName = other.replaceName;
    if (!namePrefix) namePrefix = other.namePrefix;
    if (!nameSuffix) nameSuffix = other.nameSuffix;
    if (!chatMessage) chatMessage = other.chatMessage;
    if (!chatMessageWhenHidden) chatMessageWhenHidden = other.chatMessageWhenHidden;
    if (!sound) sound = other.sound;
    if (!labelBackground) labelBackground = other.labelBackground;
    if (!labelBorder) labelBorder = other.labelBorder;
    if (!notify) notify = other.notify;
    if (!soundVolume) soundVolume = other.soundVolume;
    if (!icon) icon = other.icon;
    if (!droplight) droplight = other.droplight;
    if (!show) show = other.show;
}

auto Actions::Empty() const noexcept -> bool {
    return !hide && !hideIfOtherConditionsFail && !replaceName && !namePrefix && !nameSuffix && !chatMessage
        && !chatMessageWhenHidden && !sound && !labelBackground && !labelBorder && !notify && !soundVolume && !icon && !droplight && !show;
}

auto FilterProgram::FindLevel(int id) const noexcept -> const Level* {
    for (const Level& level : levels) {
        if (level.id == id) return &level;
    }
    return nullptr;
}

auto FilterProgram::EffectiveLevel(int wanted) const noexcept -> int {
    if (levels.empty()) return 0;
    if (FindLevel(wanted) != nullptr) return wanted;
    if (FindLevel(defaultLevel) != nullptr) return defaultLevel;
    return levels.front().id;
}

auto FilterProgram::NextLevel(int current) const noexcept -> int {
    if (levels.empty()) return 0;
    for (std::size_t i = 0; i < levels.size(); ++i) {
        if (levels[i].id == current) return levels[(i + 1) % levels.size()].id;
    }
    return levels.front().id;
}

auto FilterProgram::EnabledRuleCount() const noexcept -> std::size_t {
    return static_cast<std::size_t>(std::count_if(rules.begin(), rules.end(), [](const Rule& r) { return r.enabled; }));
}

auto Problem::ToString() const -> std::string {
    std::string text = severity == Severity::Error ? "Error" : "Warning";
    if (!where.empty()) {
        text += ", ";
        text += where;
    }
    if (line != 0) {
        char buffer[32];
        std::snprintf(buffer, sizeof(buffer), " (line %u)", line);
        text += buffer;
    }
    text += ": ";
    text += message;
    return text;
}

auto IsPreIdentifyStat(int stat) noexcept -> bool {
    return stat == 14 || stat == 31 || stat == 70 || stat == 72 || stat == 73 || stat == 194;
}

auto PackItemCode(std::string_view code) noexcept -> std::uint32_t {
    std::uint32_t packed = 0;
    for (std::size_t i = 0; i < 4; ++i) {
        const auto c = static_cast<std::uint8_t>(i < code.size() ? code[i] : ' ');
        packed |= static_cast<std::uint32_t>(c) << (8 * i);
    }
    return packed;
}

auto UnpackItemCode(std::uint32_t code) -> std::string {
    std::string text;
    for (int i = 0; i < 4; ++i) {
        const char c = static_cast<char>((code >> (8 * i)) & 0xFF);
        if (c == ' ' || c == '\0') break;
        text.push_back(c);
    }
    return text;
}

auto QualityName(int quality) noexcept -> const char* {
    static constexpr std::array<const char*, 10> names{ "", "Inferior", "Normal", "Superior", "Magic", "Set", "Rare", "Unique", "Crafted", "Tempered" };
    return quality >= 0 && quality < static_cast<int>(names.size()) ? names[static_cast<std::size_t>(quality)] : "";
}

auto BaseTierName(int tier) noexcept -> const char* {
    switch (tier) {
    case 0: return "Normal";
    case 1: return "Exceptional";
    case 2: return "Elite";
    default: return "";
    }
}

// ---------------------------------------------------------------------------
// Compiler
// ---------------------------------------------------------------------------

namespace {

using json::Type;
using json::Value;

struct NamedNumber {
    const char* name;
    int         value;
};

constexpr std::array<NamedNumber, 9> QualityNames{ {
    { "inferior", 1 }, { "normal", 2 }, { "superior", 3 }, { "magic", 4 }, { "set", 5 },
    { "rare", 6 }, { "unique", 7 }, { "crafted", 8 }, { "tempered", 9 },
} };
constexpr std::array<NamedNumber, 3> TierNames{ { { "normal", 0 }, { "exceptional", 1 }, { "elite", 2 } } };
constexpr std::array<NamedNumber, 3> DifficultyNames{ { { "normal", 0 }, { "nightmare", 1 }, { "hell", 2 } } };

constexpr std::array<const char*, 13> Languages{
    "enUS", "deDE", "esES", "frFR", "itIT", "koKR", "plPL", "ruRU", "zhCN", "zhTW", "esMX", "jaJP", "ptBR",
};

constexpr std::array<const char*, 16> D2rlanColorTags{
    "white", "red", "green", "blue", "gold", "grey", "gray", "black", "tan", "orange", "yellow", "purple",
    "dark green", "turquoise", "pink", "lilac",
};

constexpr std::array<const char*, 25> ConditionKeys{
    "itemCodes", "excludeItemCodes", "itemTypes", "excludeItemTypes", "quality", "baseTier", "itemLevel",
    "sockets", "maxSockets", "isFullySocketed", "isEthereal", "isIdentified", "isRuneword", "uniqueOrSetIds",
    "excludeUniqueOrSetIds", "itemStats", "itemStatsAnyOf", "playerStats", "playerStatsAnyOf", "location",
    "difficulty", "areas", "excludeAreas", "activeInLevels", "formula",
};

constexpr std::array<const char*, 15> ActionKeys{
    "hide", "hideIfOtherConditionsFail", "replaceName", "namePrefix", "nameSuffix", "chatMessage",
    "chatMessageWhenHidden", "sound", "labelBackground", "labelBorder", "notify", "soundVolume", "icon", "droplight", "show",
};

// Where a text is used decides which {placeholders} it may hold.
enum class TextKind : std::uint8_t {
    Name,         // item names: the item's placeholders
    Chat,         // chat lines about an item: the item's placeholders and {link}
    LevelChange,  // the level change line: {level} and {filter}
    Plain,        // level names: only {string:KEY}
};

struct OldKey {
    const char* old;
    const char* hint;
};

// D2RLAN names, so a half-converted filter points at the right new name.
constexpr std::array<OldKey, 31> OldKeys{ {
    { "code", "use itemCodes" }, { "codes", "use itemCodes" }, { "index", "use uniqueOrSetIds" },
    { "rarity", "use baseTier" }, { "ilvl", "use itemLevel" }, { "maxsock", "use isFullySocketed or maxSockets" },
    { "ethereal", "use isEthereal" }, { "identified", "use isIdentified" }, { "runeword", "use isRuneword" },
    { "itype", "use itemTypes" }, { "stat", "use itemStats" }, { "pstat", "use playerStats" },
    { "area", "use areas (level ids)" }, { "filter_levels", "use activeInLevels" }, { "hide_mismatches", "use hideIfOtherConditionsFail" },
    { "name_override", "use replaceName" }, { "prefix", "use namePrefix" }, { "suffix", "use nameSuffix" },
    { "notify", "use chatMessage" }, { "print", "use chatMessageWhenHidden" }, { "audio", "use sound" },
    { "background", "use labelBackground" }, { "border", "use labelBorder" }, { "style", "use useStyle" },
    { "name_style", "name styles are not supported" }, { "background_style", "label effects are not supported" },
    { "prefix_desc", "tooltip text is not supported" }, { "suffix_desc", "tooltip text is not supported" },
    { "grail", "grail conditions are not supported" }, { "filterlevels", "use activeInLevels" },
    { "filter_lvels", "use activeInLevels" },
} };

auto IsOneOf(std::string_view key, const auto& list) -> bool {
    return std::any_of(list.begin(), list.end(), [&](const char* entry) { return key == entry; });
}

auto Lower(std::string_view text) -> std::string {
    std::string out(text);
    for (char& c : out) {
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    }
    return out;
}

class Compiler {
public:
    Compiler(FilterProgram& program, std::vector<Problem>& problems) : program_(program), problems_(problems) {}

    auto Compile(const Value& root) -> bool {
        if (!root.IsObject()) {
            Report(Problem::Severity::Error, "", root.line, "the filter must be one object { ... }");
            return false;
        }
        WarnDuplicates(root, "the filter");
        for (const json::Member& member : root.object) {
            const std::string& key = member.key;
            if (key == "formatVersion" || key == "title" || key == "author" || key == "version" || key == "description"
                || key == "updateUrl" || key == "filterLevels" || key == "defaultLevel" || key == "hideUnmatchedItems"
                || key == "styles" || key == "rules" || key == "notifications" || key == "soundVolume"
                || key == "qualityColors" || key == "chatNotifications") {
                continue;
            }
            WarnUnknownTopLevel(key, member.value.line);
        }

        if (const Value* v = root.Find("formatVersion")) {
            if (!v->IsInteger()) {
                Report(Problem::Severity::Error, "formatVersion", v->line, "expected a whole number");
            } else if (v->integer != CurrentFormatVersion) {
                Report(Problem::Severity::Warning, "formatVersion", v->line,
                    "this plugin reads format version 1; parts of a newer format may be ignored");
                program_.formatVersion = static_cast<int>(v->integer);
            }
        }
        ReadString(root, "title", program_.title);
        ReadString(root, "author", program_.author);
        ReadString(root, "version", program_.version);
        ReadString(root, "description", program_.description);
        ReadString(root, "updateUrl", program_.updateUrl);
        if (const Value* v = root.Find("hideUnmatchedItems")) {
            if (!v->IsBool()) Report(Problem::Severity::Error, "hideUnmatchedItems", v->line, "expected true or false");
            else program_.hideUnmatchedItems = v->boolean;
        }
        if (const Value* v = root.Find("chatNotifications")) {
            if (!v->IsBool()) Report(Problem::Severity::Error, "chatNotifications", v->line, "expected true or false");
            else program_.chatNotifications = v->boolean;
        }

        if (const Value* v = root.Find("soundVolume")) {
            if (!v->IsInteger() || v->integer < 0 || v->integer > 100) {
                Report(Problem::Severity::Error, "soundVolume", v->line, "expected a whole number from 0 to 100; ignored");
            } else {
                program_.soundVolume = static_cast<int>(v->integer);
            }
        }

        inRule_ = false;
        ReadLevels(root);
        ReadNotifications(root);
        ReadQualityColors(root);
        inRule_ = true;
        ReadStyles(root);

        const Value* rules = root.Find("rules");
        if (rules == nullptr) {
            Report(Problem::Severity::Warning, "rules", root.line, "the filter has no rules, so it changes nothing");
            return true;
        }
        if (!rules->IsArray()) {
            Report(Problem::Severity::Error, "rules", rules->line, "expected a list [ ... ] of rules");
            return false;
        }
        int number = 0;
        for (const Value& entry : rules->array) {
            ++number;
            ReadRule(entry, number);
        }
        return true;
    }

private:
    FilterProgram&        program_;
    std::vector<Problem>& problems_;
    std::vector<std::pair<std::string, Actions>> styles_;
    bool                  ruleFailed_{};
    bool                  inRule_{ true };  // false while reading filter-wide settings

    void Report(Problem::Severity severity, std::string where, std::uint32_t line, std::string message) {
        problems_.push_back(Problem{ severity, std::move(where), line, std::move(message) });
    }

    void Fail(const std::string& where, std::uint32_t line, std::string message) {
        Report(Problem::Severity::Error, where, line, std::move(message) + (inRule_ ? "; the rule is switched off" : "; ignored"));
        ruleFailed_ = true;
    }

    void WarnDuplicates(const Value& object, const std::string& where) {
        for (std::size_t i = 0; i < object.object.size(); ++i) {
            for (std::size_t j = i + 1; j < object.object.size(); ++j) {
                if (object.object[i].key == object.object[j].key) {
                    Report(Problem::Severity::Warning, where, object.object[j].value.line,
                        "\"" + object.object[j].key + "\" is written twice; the last one is used");
                }
            }
        }
    }

    void WarnUnknownTopLevel(const std::string& key, std::uint32_t line) {
        static constexpr std::array<OldKey, 8> oldTop{ {
            { "reload", "not needed: the filter reloads by itself when the file changes" },
            { "language", "not needed: texts follow the game's language" },
            { "filter_titles", "use filterLevels" }, { "filter_level", "use defaultLevel" },
            { "audioPlayback", "sounds are switched on and off in the launcher" },
            { "audioVoice", "text to speech is not supported" }, { "debug", "use the lootfilter console command" },
            { "allowOverrides", "overrides are not supported" },
        } };
        for (const OldKey& old : oldTop) {
            if (key == old.old) {
                Report(Problem::Severity::Warning, key, line, std::string("D2RLAN setting ignored, ") + old.hint);
                return;
            }
        }
        if (key == "hide_mismatches") {
            Report(Problem::Severity::Warning, key, line, "D2RLAN setting ignored, use hideUnmatchedItems");
            return;
        }
        Report(Problem::Severity::Warning, key, line, "unknown setting, ignored");
    }

    void ReadString(const Value& root, const char* key, std::string& out) {
        if (const Value* v = root.Find(key)) {
            if (!v->IsString()) Report(Problem::Severity::Error, key, v->line, "expected a text");
            else out = v->string;
        }
    }

    // --- texts -------------------------------------------------------------

    void CheckTextContent(const std::string& text, const std::string& where, std::uint32_t line, TextKind kind) {
        const bool itemText = kind == TextKind::Name || kind == TextKind::Chat;
        std::size_t pos = 0;
        while ((pos = text.find('{', pos)) != std::string::npos) {
            const std::size_t end = text.find('}', pos + 1);
            if (end == std::string::npos) break;
            const std::string inner = text.substr(pos + 1, end - pos - 1);
            pos = end + 1;
            if (inner.rfind("string:", 0) == 0) {
                if (inner.size() == 7) {
                    Report(Problem::Severity::Warning, where, line, "{string:} needs a string key, like {string:r01}; it is shown as written");
                }
                continue;
            }
            if (kind == TextKind::LevelChange && (inner == "level" || inner == "filter")) continue;
            const bool itemPlaceholder = inner == "name" || inner == "code" || inner == "quality" || inner == "baseTier"
                || inner == "itemLevel" || inner == "sockets" || inner == "maxSockets" || inner == "ethereal"
                || inner == "uniqueOrSetId" || inner == "qualityColor" || inner == "link" || inner.rfind("stat:", 0) == 0
                || inner.rfind("playerStat:", 0) == 0;
            if (itemPlaceholder && !itemText) {
                Report(Problem::Severity::Warning, where, line, "{" + inner + "} only works in item names and chat lines; it is shown as written");
                continue;
            }
            if (inner == "link") {
                if (kind != TextKind::Chat) Report(Problem::Severity::Warning, where, line, "{link} only works in chat messages");
                continue;
            }
            if (inner.rfind("stat:", 0) == 0 || inner.rfind("playerStat:", 0) == 0) {
                const std::string args = inner.substr(inner.find(':') + 1);
                bool ok = !args.empty();
                int commas = 0;
                for (char c : args) {
                    if (c == ',') ++commas;
                    else if (c < '0' || c > '9') ok = false;
                }
                if (!ok || commas > 1 || args.front() == ',' || args.back() == ',') {
                    Report(Problem::Severity::Warning, where, line,
                        "{" + inner + "} must be {stat:ID} or {stat:ID,PARAM} with numbers; it is shown as written");
                }
                continue;
            }
            if (itemPlaceholder) continue;
            if (IsOneOf(Lower(inner), D2rlanColorTags)) {
                Report(Problem::Severity::Warning, where, line,
                    "{" + inner + "} is a D2RLAN color tag; use a ÿc color code instead. It is shown as written");
                continue;
            }
            Report(Problem::Severity::Warning, where, line, "unknown placeholder {" + inner + "}, shown as written");
        }
        // ÿc must be followed by one of 0-9 : ; < = > ? @ A-V.
        const std::string marker = "\xC3\xBF" "c";
        pos = 0;
        while ((pos = text.find(marker, pos)) != std::string::npos) {
            const std::size_t codeAt = pos + marker.size();
            if (codeAt >= text.size() || text[codeAt] < '0' || text[codeAt] > 'V') {
                Report(Problem::Severity::Warning, where, line, "ÿc is not followed by a color code (0-9 : ; < = > ? @ A-V)");
            }
            pos = codeAt;
        }
    }

    auto ReadText(const Value& value, const std::string& where, Text& out, TextKind kind) -> bool {
        out.variants.clear();
        if (value.IsString()) {
            out.variants.emplace_back("", value.string);
            CheckTextContent(value.string, where, value.line, kind);
            return true;
        }
        if (value.IsObject()) {
            for (const json::Member& member : value.object) {
                if (!member.value.IsString()) {
                    Fail(where + "." + member.key, member.value.line, "expected a text");
                    return false;
                }
                if (!IsOneOf(member.key, Languages)) {
                    Report(Problem::Severity::Warning, where + "." + member.key, member.value.line,
                        "unknown language code; the game uses enUS, deDE, esES, frFR, itIT, koKR, plPL, ruRU, zhCN, zhTW, esMX, jaJP, ptBR");
                }
                out.variants.emplace_back(member.key, member.value.string);
                CheckTextContent(member.value.string, where + "." + member.key, member.value.line, kind);
            }
            if (out.variants.empty()) {
                Fail(where, value.line, "the text has no language entries");
                return false;
            }
            return true;
        }
        Fail(where, value.line, "expected a text, or an object of texts by language");
        return false;
    }

    // --- numbers -----------------------------------------------------------

    template <std::size_t N>
    auto NamedValue(const Value& value, const std::array<NamedNumber, N>* names, int minimum, int maximum, int& out,
                    const std::string& where) -> bool {
        if (value.IsInteger()) {
            if (value.integer < minimum || value.integer > maximum) {
                char message[96];
                std::snprintf(message, sizeof(message), "expected a number from %d to %d", minimum, maximum);
                Fail(where, value.line, message);
                return false;
            }
            out = static_cast<int>(value.integer);
            return true;
        }
        if (value.IsString() && names != nullptr) {
            const std::string lowered = Lower(value.string);
            for (const NamedNumber& entry : *names) {
                if (lowered == entry.name) {
                    out = entry.value;
                    return true;
                }
            }
            std::string message = "unknown name \"" + value.string + "\"; expected";
            for (std::size_t i = 0; i < names->size(); ++i) {
                message += i == 0 ? " " : ", ";
                message += (*names)[i].name;
            }
            Fail(where, value.line, message);
            return false;
        }
        Fail(where, value.line, names != nullptr ? "expected a number or a name" : "expected a whole number");
        return false;
    }

    template <std::size_t N = 1>
    auto ReadIntSpec(const Value& value, const std::string& where, IntSpec& out, int minimum, int maximum,
                     const std::array<NamedNumber, N>* names = nullptr) -> bool {
        out = IntSpec{};
        int number = 0;
        if (value.IsInteger() || value.IsString()) {
            if (!NamedValue(value, names, minimum, maximum, number, where)) return false;
            out.kind = IntSpec::Kind::Values;
            out.values.push_back(number);
            return true;
        }
        if (value.IsArray()) {
            if (value.array.empty()) {
                Fail(where, value.line, "the list is empty");
                return false;
            }
            out.kind = IntSpec::Kind::Values;
            for (const Value& element : value.array) {
                if (!NamedValue(element, names, minimum, maximum, number, where)) return false;
                out.values.push_back(number);
            }
            return true;
        }
        if (value.IsObject()) {
            out.kind = IntSpec::Kind::Range;
            for (const json::Member& member : value.object) {
                if (member.key == "min") {
                    if (!NamedValue(member.value, names, minimum, maximum, out.min, where + ".min")) return false;
                    out.hasMin = true;
                } else if (member.key == "max") {
                    if (!NamedValue(member.value, names, minimum, maximum, out.max, where + ".max")) return false;
                    out.hasMax = true;
                } else {
                    Report(Problem::Severity::Warning, where + "." + member.key, member.value.line, "unknown entry; use min and max");
                }
            }
            if (!out.hasMin && !out.hasMax) {
                Fail(where, value.line, "a range needs min, max or both");
                return false;
            }
            if (out.hasMin && out.hasMax && out.min > out.max) {
                Fail(where, value.line, "min is larger than max");
                return false;
            }
            return true;
        }
        Fail(where, value.line, names != nullptr ? "expected a number, a name, a list or { \"min\", \"max\" }"
                                                 : "expected a number, a list or { \"min\", \"max\" }");
        return false;
    }

    auto ReadIntList(const Value& value, const std::string& where, std::vector<int>& out, int minimum, int maximum) -> bool {
        out.clear();
        const auto take = [&](const Value& element) -> bool {
            if (!element.IsInteger() || element.integer < minimum || element.integer > maximum) {
                char message[96];
                std::snprintf(message, sizeof(message), "expected whole numbers from %d to %d", minimum, maximum);
                Fail(where, element.line, message);
                return false;
            }
            out.push_back(static_cast<int>(element.integer));
            return true;
        };
        if (value.IsInteger()) return take(value);
        if (!value.IsArray()) {
            Fail(where, value.line, "expected a list of numbers");
            return false;
        }
        for (const Value& element : value.array) {
            if (!take(element)) return false;
        }
        return true;
    }

    auto ReadBool(const Value& value, const std::string& where, std::optional<bool>& out) -> bool {
        if (!value.IsBool()) {
            Fail(where, value.line, "expected true or false");
            return false;
        }
        out = value.boolean;
        return true;
    }

    // --- item codes, types, stats ------------------------------------------

    auto ReadCodes(const Value& value, const std::string& where, std::vector<std::uint32_t>& out) -> bool {
        out.clear();
        const auto take = [&](const Value& element) -> bool {
            if (!element.IsString() || element.string.empty() || element.string.size() > 4
                || element.string.find(' ') != std::string::npos) {
                Fail(where, element.line, "item codes are texts of 1 to 4 characters, like \"r01\"");
                return false;
            }
            out.push_back(PackItemCode(element.string));
            return true;
        };
        if (value.IsString()) return take(value);
        if (!value.IsArray() || value.array.empty()) {
            Fail(where, value.line, "expected a list of item codes");
            return false;
        }
        for (const Value& element : value.array) {
            if (!take(element)) return false;
        }
        return true;
    }

    auto ReadTypes(const Value& value, const std::string& where, std::vector<TypeRef>& out) -> bool {
        out.clear();
        const auto take = [&](const Value& element) -> bool {
            TypeRef ref;
            if (element.IsInteger() && element.integer >= 0 && element.integer < 65536) {
                ref.row = static_cast<int>(element.integer);
            } else if (element.IsString() && !element.string.empty() && element.string.size() <= 4) {
                ref.code = element.string;
            } else {
                Fail(where, element.line, "item types are itemtypes.txt codes like \"ring\", or row numbers");
                return false;
            }
            out.push_back(std::move(ref));
            return true;
        };
        if (value.IsString() || value.IsInteger()) return take(value);
        if (!value.IsArray() || value.array.empty()) {
            Fail(where, value.line, "expected a list of item types");
            return false;
        }
        for (const Value& element : value.array) {
            if (!take(element)) return false;
        }
        return true;
    }

    auto ReadFormula(const Value& value, const std::string& where, std::optional<Formula>& out) -> bool {
        out.reset();
        if (!value.IsString() || value.string.empty()) {
            Fail(where, value.line, "expected a formula text, e.g. \"(stat('item_fastercastrate')>=10)\"");
            return false;
        }
        Formula formula;
        std::string error;
        std::uint32_t column = 0;
        if (!CompileFormula(value.string, formula, error, column)) {
            Fail(where, value.line, "at character " + std::to_string(column) + ": " + error);
            return false;
        }
        out = std::move(formula);
        return true;
    }

    auto ReadStatChecks(const Value& value, const std::string& where, std::vector<StatCheck>& out) -> bool {
        out.clear();
        const auto take = [&](const Value& element, const std::string& at) -> bool {
            if (!element.IsObject()) {
                Fail(at, element.line, "expected { \"stat\": ..., \"op\": \">=\", \"value\": ... }");
                return false;
            }
            StatCheck check;
            const Value* stat = element.Find("stat");
            if (stat == nullptr) {
                Fail(at, element.line, "\"stat\" is missing");
                return false;
            }
            if (stat->IsInteger() && stat->integer >= 0 && stat->integer < 65536) {
                check.stat = static_cast<int>(stat->integer);
            } else if (stat->IsString() && !stat->string.empty()) {
                check.name = stat->string;
            } else {
                Fail(at + ".stat", stat->line, "expected an itemstatcost.txt stat name or id");
                return false;
            }
            if (const Value* param = element.Find("param")) {
                if (!param->IsInteger() || param->integer < 0 || param->integer > 65535) {
                    Fail(at + ".param", param->line, "expected a whole number from 0 to 65535");
                    return false;
                }
                check.param = static_cast<int>(param->integer);
            }
            const Value* op = element.Find("op");
            const std::string opText = op != nullptr && op->IsString() ? op->string : std::string(op == nullptr ? ">=" : "");
            if (opText == "==") check.op = StatOp::Equal;
            else if (opText == "!=") check.op = StatOp::NotEqual;
            else if (opText == ">") check.op = StatOp::Greater;
            else if (opText == ">=") check.op = StatOp::GreaterOrEqual;
            else if (opText == "<") check.op = StatOp::Less;
            else if (opText == "<=") check.op = StatOp::LessOrEqual;
            else if (opText == "between") check.op = StatOp::Between;
            else {
                Fail(at + ".op", op != nullptr ? op->line : element.line, "op must be ==, !=, >, >=, <, <= or between");
                return false;
            }
            const Value* number = element.Find("value");
            if (number == nullptr) {
                Fail(at, element.line, "\"value\" is missing");
                return false;
            }
            if (check.op == StatOp::Between) {
                if (!number->IsArray() || number->array.size() != 2 || !number->array[0].IsInteger() || !number->array[1].IsInteger()) {
                    Fail(at + ".value", number->line, "between needs \"value\": [low, high]");
                    return false;
                }
                check.value  = static_cast<int>(number->array[0].integer);
                check.value2 = static_cast<int>(number->array[1].integer);
                if (check.value > check.value2) {
                    Fail(at + ".value", number->line, "low is larger than high");
                    return false;
                }
            } else {
                if (!number->IsInteger()) {
                    Fail(at + ".value", number->line, "expected a whole number");
                    return false;
                }
                check.value = static_cast<int>(number->integer);
            }
            for (const json::Member& member : element.object) {
                if (member.key != "stat" && member.key != "param" && member.key != "op" && member.key != "value") {
                    Report(Problem::Severity::Warning, at + "." + member.key, member.value.line, "unknown entry, ignored");
                }
            }
            out.push_back(std::move(check));
            return true;
        };
        if (value.IsObject()) return take(value, where);
        if (!value.IsArray() || value.array.empty()) {
            Fail(where, value.line, "expected a list of stat checks");
            return false;
        }
        for (std::size_t i = 0; i < value.array.size(); ++i) {
            if (!take(value.array[i], where + "[" + std::to_string(i + 1) + "]")) return false;
        }
        return true;
    }

    auto ReadLocations(const Value& value, const std::string& where, std::uint8_t& out) -> bool {
        out = 0;
        const auto take = [&](const Value& element) -> bool {
            if (element.IsString()) {
                if (element.string == "onGround") { out |= LocationGround; return true; }
                if (element.string == "stored") { out |= LocationStored; return true; }
                if (element.string == "equipped") { out |= LocationEquipped; return true; }
                if (element.string == "atVendor") { out |= LocationVendor; return true; }
            }
            Fail(where, element.line, "locations are onGround, stored, equipped and atVendor");
            return false;
        };
        if (value.IsString()) return take(value);
        if (!value.IsArray() || value.array.empty()) {
            Fail(where, value.line, "expected a list of locations");
            return false;
        }
        for (const Value& element : value.array) {
            if (!take(element)) return false;
        }
        return true;
    }

    // --- actions -----------------------------------------------------------

    auto ReadRgba(const Value& value, const std::string& where, Rgba& out) -> bool {
        if (!value.IsArray() || (value.array.size() != 3 && value.array.size() != 4)) {
            Fail(where, value.line, "expected [red, green, blue] or [red, green, blue, alpha], each 0 to 255");
            return false;
        }
        std::array<int, 4> channels{ 0, 0, 0, 242 };
        for (std::size_t i = 0; i < value.array.size(); ++i) {
            const Value& channel = value.array[i];
            if (!channel.IsInteger() || channel.integer < 0 || channel.integer > 255) {
                Fail(where, channel.line, "each color channel is a whole number from 0 to 255");
                return false;
            }
            channels[i] = static_cast<int>(channel.integer);
        }
        out = Rgba{ static_cast<std::uint8_t>(channels[0]), static_cast<std::uint8_t>(channels[1]),
                    static_cast<std::uint8_t>(channels[2]), static_cast<std::uint8_t>(channels[3]) };
        return true;
    }

    auto ReadSound(const Value& value, const std::string& where, SoundRef& out) -> bool {
        out = SoundRef{};
        if (value.IsInteger()) {
            if (value.integer < 0 || value.integer > 65535) {
                Fail(where, value.line, "a sounds.txt row is a number from 0 to 65535");
                return false;
            }
            out.kind = SoundRef::Kind::TableRow;
            out.row  = static_cast<int>(value.integer);
            return true;
        }
        if (!value.IsString() || value.string.empty()) {
            Fail(where, value.line, "expected a sounds.txt name or row, or a sound file name");
            return false;
        }
        const std::string lowered = Lower(value.string);
        const auto endsWith = [&](const char* suffix) {
            const std::size_t length = std::strlen(suffix);
            return lowered.size() > length && lowered.compare(lowered.size() - length, length, suffix) == 0;
        };
        if (endsWith(".ogg")) {
            Fail(where, value.line, "Ogg files are not supported; use .wav, .mp3 or .flac");
            return false;
        }
        if (endsWith(".wav") || endsWith(".mp3") || endsWith(".flac")) {
            if (value.string.find("..") != std::string::npos || value.string.find(':') != std::string::npos
                || value.string.front() == '/' || value.string.front() == '\\') {
                Fail(where, value.line, "a sound file is a name inside the filter's sounds folder, not a path");
                return false;
            }
            out.kind = SoundRef::Kind::File;
            out.file = value.string;
            return true;
        }
        out.kind = SoundRef::Kind::TableName;
        out.name = value.string;
        return true;
    }

    auto ReadActions(const Value& object, const std::string& where, Actions& out) -> bool {
        for (const json::Member& member : object.object) {
            const std::string at = where + ", " + member.key;
            const Value& value = member.value;
            const std::string& key = member.key;
            if (key == "hide") {
                if (!ReadBool(value, at, out.hide)) return false;
            } else if (key == "hideIfOtherConditionsFail") {
                if (!ReadBool(value, at, out.hideIfOtherConditionsFail)) return false;
            } else if (key == "show") {
                if (!ReadBool(value, at, out.show)) return false;
            } else if (key == "replaceName" || key == "namePrefix" || key == "nameSuffix" || key == "chatMessage"
                       || key == "chatMessageWhenHidden") {
                Text text;
                const bool chat = key == "chatMessage" || key == "chatMessageWhenHidden";
                if (!ReadText(value, at, text, chat ? TextKind::Chat : TextKind::Name)) return false;
                if (key == "replaceName") out.replaceName = std::move(text);
                else if (key == "namePrefix") out.namePrefix = std::move(text);
                else if (key == "nameSuffix") out.nameSuffix = std::move(text);
                else if (key == "chatMessage") out.chatMessage = std::move(text);
                else out.chatMessageWhenHidden = std::move(text);
            } else if (key == "sound") {
                SoundRef sound;
                if (!ReadSound(value, at, sound)) return false;
                out.sound = std::move(sound);
            } else if (key == "labelBackground") {
                Rgba color;
                if (!ReadRgba(value, at, color)) return false;
                out.labelBackground = color;
            } else if (key == "labelBorder") {
                if (!value.IsObject()) {
                    Fail(at, value.line, "expected { \"color\": [r, g, b, a], \"width\": 1 }");
                    return false;
                }
                Border border;
                const Value* color = value.Find("color");
                if (color == nullptr) {
                    Fail(at, value.line, "\"color\" is missing");
                    return false;
                }
                if (!ReadRgba(*color, at + ".color", border.color)) return false;
                if (color->IsArray() && color->array.size() == 3) border.color.a = 255;
                if (const Value* width = value.Find("width")) {
                    if (!width->IsInteger() || width->integer < 1 || width->integer > 8) {
                        Fail(at + ".width", width->line, "expected a whole number from 1 to 8");
                        return false;
                    }
                    border.width = static_cast<int>(width->integer);
                }
                for (const json::Member& part : value.object) {
                    if (part.key != "color" && part.key != "width") {
                        Report(Problem::Severity::Warning, at + "." + part.key, part.value.line, "unknown entry, ignored");
                    }
                }
                out.labelBorder = border;
            } else if (key == "notify") {
                if (!ReadBool(value, at, out.notify)) return false;
            } else if (key == "soundVolume") {
                if (!value.IsInteger() || value.integer < 0 || value.integer > 100) {
                    Fail(at, value.line, "expected a whole number from 0 to 100");
                    return false;
                }
                out.soundVolume = static_cast<int>(value.integer);
            } else if (key == "icon") {
                IconSpec icon;
                if (!ReadIcon(value, at, icon)) return false;
                out.icon = std::move(icon);
            } else if (key == "droplight") {
                if (!value.IsObject()) {
                    Fail(at, value.line, "expected { \"color\": [r, g, b] } or {}");
                    return false;
                }
                DroplightSpec droplight;
                for (const json::Member& member : value.object) {
                    if (member.key == "color") {
                        Rgba color;
                        if (!ReadRgba(member.value, at + ".color", color)) return false;
                        color.a = 255;
                        droplight.color = color;
                    } else {
                        Report(Problem::Severity::Warning, at + "." + member.key, member.value.line, "unknown droplight entry, ignored; use color");
                    }
                }
                out.droplight = droplight;
            }
        }
        return true;
    }

    // { "iconName": "rune", "iconSize": 16, "color": [r, g, b] or [r, g, b, a] }
    auto ReadIcon(const Value& value, const std::string& where, IconSpec& out) -> bool {
        out = IconSpec{};
        if (!value.IsObject()) {
            Fail(where, value.line, "expected { \"iconName\": \"...\", \"iconSize\": 16 }");
            return false;
        }
        WarnDuplicates(value, where);
        for (const json::Member& member : value.object) {
            const std::string at = where + "." + member.key;
            const Value& field = member.value;
            if (member.key == "iconName") {
                std::string name = field.IsString() ? field.string : std::string();
                if (name.size() > 7 && Lower(name.substr(name.size() - 7)) == ".sprite") name.resize(name.size() - 7);
                bool ok = !name.empty() && name.size() <= 64 && name.front() != '.';
                for (char c : name) {
                    const auto u = static_cast<unsigned char>(c);
                    if (u < 0x20 || u > 0x7E || std::strchr("/\\:*?\"<>|", c) != nullptr) ok = false;
                }
                if (!ok) {
                    Fail(at, field.line, "expected the name of a .sprite file in the icons folder");
                    return false;
                }
                out.name = std::move(name);
            } else if (member.key == "iconSize") {
                if (!field.IsInteger() || field.integer < 4 || field.integer > 256) {
                    Fail(at, field.line, "expected a whole number of pixels from 4 to 256");
                    return false;
                }
                out.size = static_cast<int>(field.integer);
            } else if (member.key == "color") {
                Rgba color;
                if (!ReadRgba(field, at, color)) return false;
                if (field.array.size() == 3) color.a = 255;
                out.color = color;
            } else {
                Report(Problem::Severity::Warning, at, field.line, "unknown icon entry, ignored; use iconName, iconSize and color");
            }
        }
        if (out.name.empty()) {
            Fail(where, value.line, "an icon needs an iconName");
            return false;
        }
        return true;
    }

    // --- levels and styles -------------------------------------------------

    void ReadLevels(const Value& root) {
        const Value* levels = root.Find("filterLevels");
        if (levels == nullptr) {
            if (const Value* v = root.Find("defaultLevel")) {
                Report(Problem::Severity::Warning, "defaultLevel", v->line, "ignored: the filter has no filterLevels");
            }
            return;
        }
        if (!levels->IsArray()) {
            Report(Problem::Severity::Error, "filterLevels", levels->line, "expected a list of { \"id\": 1, \"name\": \"...\" }");
            return;
        }
        int index = 0;
        for (const Value& entry : levels->array) {
            ++index;
            const std::string where = "filterLevels[" + std::to_string(index) + "]";
            if (!entry.IsObject()) {
                Report(Problem::Severity::Error, where, entry.line, "expected { \"id\": 1, \"name\": \"...\" }");
                continue;
            }
            const Value* id = entry.Find("id");
            const Value* name = entry.Find("name");
            if (id == nullptr || !id->IsInteger() || id->integer < 0 || id->integer > 999) {
                Report(Problem::Severity::Error, where, entry.line, "\"id\" must be a whole number from 0 to 999");
                continue;
            }
            Level level;
            level.id = static_cast<int>(id->integer);
            if (program_.FindLevel(level.id) != nullptr) {
                Report(Problem::Severity::Error, where, entry.line, "this id is already used by another level");
                continue;
            }
            if (name == nullptr) {
                level.name.variants.emplace_back("", "Level " + std::to_string(level.id));
            } else {
                ruleFailed_ = false;
                if (!ReadText(*name, where + ".name", level.name, TextKind::Plain)) {
                    level.name.variants.clear();
                    level.name.variants.emplace_back("", "Level " + std::to_string(level.id));
                }
            }
            program_.levels.push_back(std::move(level));
        }
        if (program_.levels.empty()) {
            Report(Problem::Severity::Warning, "filterLevels", levels->line, "no usable levels; the filter acts as one level");
            return;
        }
        program_.defaultLevel = program_.levels.front().id;
        if (const Value* v = root.Find("defaultLevel")) {
            if (!v->IsInteger() || program_.FindLevel(static_cast<int>(v->integer)) == nullptr) {
                Report(Problem::Severity::Error, "defaultLevel", v->line, "must be the id of one of the filterLevels");
            } else {
                program_.defaultLevel = static_cast<int>(v->integer);
            }
        }
    }

    void ReadNotifications(const Value& root) {
        const Value* notifications = root.Find("notifications");
        if (notifications == nullptr) return;
        if (!notifications->IsObject()) {
            Report(Problem::Severity::Error, "notifications", notifications->line,
                "expected { \"dropFormat\": \"...\", \"levelChangeFormat\": \"...\" }; ignored");
            return;
        }
        WarnDuplicates(*notifications, "notifications");
        for (const json::Member& member : notifications->object) {
            const std::string where = "notifications." + member.key;
            if (member.key == "dropFormat") {
                Text text;
                if (ReadText(member.value, where, text, TextKind::Chat)) program_.notifications.dropFormat = std::move(text);
            } else if (member.key == "levelChangeFormat") {
                Text text;
                if (ReadText(member.value, where, text, TextKind::LevelChange)) program_.notifications.levelChangeFormat = std::move(text);
            } else {
                Report(Problem::Severity::Warning, where, member.value.line, "unknown entry, ignored; use dropFormat and levelChangeFormat");
            }
        }
    }

    // "ÿcX" (or "\u00ffcX") -> X
    static auto ColorCodeOf(const Value& value, char& code) -> bool {
        static const std::string marker = "\xC3\xBF" "c";
        if (!value.IsString() || value.string.size() != marker.size() + 1 || value.string.compare(0, marker.size(), marker) != 0) return false;
        code = value.string.back();
        return code >= '0' && code <= 'V';
    }

    void ReadQualityColors(const Value& root) {
        const Value* colors = root.Find("qualityColors");
        if (colors == nullptr) return;
        if (!colors->IsObject()) {
            Report(Problem::Severity::Error, "qualityColors", colors->line, "expected { \"magic\": \"ÿc2\", ... }; ignored");
            return;
        }
        WarnDuplicates(*colors, "qualityColors");
        for (const json::Member& member : colors->object) {
            const std::string where = "qualityColors." + member.key;
            int quality = 0;
            const std::string lowered = Lower(member.key);
            for (const NamedNumber& entry : QualityNames) {
                if (lowered == entry.name) quality = entry.value;
            }
            if (quality == 0) {
                Report(Problem::Severity::Error, where, member.value.line,
                    "unknown quality; use inferior, normal, superior, magic, set, rare, unique, crafted or tempered");
                continue;
            }
            char code = '\0';
            if (!ColorCodeOf(member.value, code)) {
                Report(Problem::Severity::Error, where, member.value.line, "expected one color code, like \"ÿc2\"; ignored");
                continue;
            }
            program_.qualityColors.byQuality[static_cast<std::size_t>(quality)] = code;
        }
    }

    void ReadStyles(const Value& root) {
        const Value* styles = root.Find("styles");
        if (styles == nullptr) return;
        if (!styles->IsObject()) {
            Report(Problem::Severity::Error, "styles", styles->line, "expected { \"StyleName\": { actions }, ... }");
            return;
        }
        for (const json::Member& member : styles->object) {
            const std::string where = "style \"" + member.key + "\"";
            if (!member.value.IsObject()) {
                Report(Problem::Severity::Error, where, member.value.line, "expected an object of actions");
                continue;
            }
            for (const json::Member& entry : member.value.object) {
                if (!IsOneOf(entry.key, ActionKeys)) {
                    Report(Problem::Severity::Warning, where + ", " + entry.key, entry.value.line,
                        IsOneOf(entry.key, ConditionKeys) ? "conditions do not belong in a style, ignored" : "unknown action, ignored");
                }
            }
            Actions actions;
            ruleFailed_ = false;
            if (!ReadActions(member.value, where, actions) || ruleFailed_) continue;
            styles_.emplace_back(member.key, std::move(actions));
        }
    }

    // --- rules -------------------------------------------------------------

    void ReadRule(const Value& entry, int number) {
        Rule rule;
        rule.number = number;
        rule.line   = entry.line;
        std::string where = "rule " + std::to_string(number);
        if (!entry.IsObject()) {
            Report(Problem::Severity::Error, where, entry.line, "expected an object { ... }; the rule is skipped");
            return;
        }
        if (const Value* comment = entry.Find("comment")) {
            if (comment->IsString()) {
                rule.comment = comment->string;
                where += " (\"" + (comment->string.size() > 40 ? comment->string.substr(0, 40) + "..." : comment->string) + "\")";
            }
        }
        ruleFailed_ = false;
        WarnDuplicates(entry, where);
        Conditions& when = rule.when;
        for (const json::Member& member : entry.object) {
            const std::string& key = member.key;
            const Value& value = member.value;
            const std::string at = where + ", " + key;
            if (key == "comment" || key == "useStyle" || IsOneOf(key, ActionKeys)) continue;
            if (key == "itemCodes") ReadCodes(value, at, when.itemCodes);
            else if (key == "excludeItemCodes") ReadCodes(value, at, when.excludeItemCodes);
            else if (key == "itemTypes") ReadTypes(value, at, when.itemTypes);
            else if (key == "excludeItemTypes") ReadTypes(value, at, when.excludeItemTypes);
            else if (key == "quality") ReadIntSpec(value, at, when.quality, 1, 9, &QualityNames);
            else if (key == "baseTier") ReadIntSpec(value, at, when.baseTier, 0, 2, &TierNames);
            else if (key == "itemLevel") ReadIntSpec(value, at, when.itemLevel, 0, 999);
            else if (key == "sockets") ReadIntSpec(value, at, when.sockets, 0, 99);
            else if (key == "maxSockets") ReadIntSpec(value, at, when.maxSockets, 0, 99);
            else if (key == "difficulty") ReadIntSpec(value, at, when.difficulty, 0, 2, &DifficultyNames);
            else if (key == "isFullySocketed") ReadBool(value, at, when.isFullySocketed);
            else if (key == "isEthereal") ReadBool(value, at, when.isEthereal);
            else if (key == "isIdentified") ReadBool(value, at, when.isIdentified);
            else if (key == "isRuneword") ReadBool(value, at, when.isRuneword);
            else if (key == "uniqueOrSetIds") ReadIntList(value, at, when.uniqueOrSetIds, 0, 65535);
            else if (key == "excludeUniqueOrSetIds") ReadIntList(value, at, when.excludeUniqueOrSetIds, 0, 65535);
            else if (key == "itemStats") ReadStatChecks(value, at, when.itemStats);
            else if (key == "itemStatsAnyOf") ReadStatChecks(value, at, when.itemStatsAnyOf);
            else if (key == "playerStats") ReadStatChecks(value, at, when.playerStats);
            else if (key == "playerStatsAnyOf") ReadStatChecks(value, at, when.playerStatsAnyOf);
            else if (key == "formula") ReadFormula(value, at, when.formula);
            else if (key == "location") ReadLocations(value, at, when.locations);
            else if (key == "areas") ReadIntList(value, at, when.areas, 0, 65535);
            else if (key == "excludeAreas") ReadIntList(value, at, when.excludeAreas, 0, 65535);
            else if (key == "activeInLevels") {
                if (ReadIntList(value, at, when.activeInLevels, 0, 999)) {
                    for (int id : when.activeInLevels) {
                        if (program_.FindLevel(id) == nullptr) {
                            Fail(at, value.line, "level " + std::to_string(id) + " is not one of the filterLevels");
                            break;
                        }
                    }
                }
            } else {
                bool hinted = false;
                for (const OldKey& old : OldKeys) {
                    if (key == old.old) {
                        Report(Problem::Severity::Warning, at, value.line, std::string("D2RLAN name ignored, ") + old.hint);
                        hinted = true;
                        break;
                    }
                }
                if (!hinted) Report(Problem::Severity::Warning, at, value.line, "unknown property, ignored");
            }
        }
        ReadActions(entry, where, rule.then);
        if (const Value* style = entry.Find("useStyle")) {
            if (!style->IsString()) {
                Fail(where + ", useStyle", style->line, "expected the name of a style");
            } else {
                const auto found = std::find_if(styles_.begin(), styles_.end(),
                    [&](const auto& pair) { return pair.first == style->string; });
                if (found == styles_.end()) Fail(where + ", useStyle", style->line, "there is no style \"" + style->string + "\"");
                else rule.then.FillFrom(found->second);
            }
        }
        if (rule.then.show.value_or(false) && rule.then.hide.value_or(false)) {
            Fail(where + ", show", entry.line, "a rule cannot both show and hide an item");
        }
        if (rule.then.hideIfOtherConditionsFail.value_or(false) && rule.when.itemCodes.empty()) {
            Report(Problem::Severity::Warning, where + ", hideIfOtherConditionsFail", entry.line,
                "without itemCodes this hides every ground item that fails the rule");
        }
        rule.enabled = !ruleFailed_;
        program_.rules.push_back(std::move(rule));
    }
};

}  // namespace

auto CompileFilter(std::string_view text, FilterProgram& program, std::vector<Problem>& problems) -> bool {
    program = FilterProgram{};
    json::Value root;
    json::ParseError error;
    if (!json::Parse(text, root, error)) {
        problems.push_back(Problem{ Problem::Severity::Error, "", error.line,
            "the file is not valid JSON: " + error.message + " (column " + std::to_string(error.column) + ")" });
        return false;
    }
    Compiler compiler(program, problems);
    return compiler.Compile(root);
}

void ResolveNames(FilterProgram& program, NameResolver& resolver, std::vector<Problem>& problems) {
    for (Rule& rule : program.rules) {
        if (!rule.enabled) continue;
        const std::string where = "rule " + std::to_string(rule.number);
        const auto fail = [&](const std::string& message) {
            problems.push_back(Problem{ Problem::Severity::Error, where, rule.line, message + "; the rule is switched off" });
            rule.enabled = false;
        };
        for (auto* list : { &rule.when.itemTypes, &rule.when.excludeItemTypes }) {
            for (TypeRef& ref : *list) {
                if (ref.row >= 0) continue;
                ref.row = resolver.ItemTypeRow(ref.code);
                if (ref.row < 0) fail("item type \"" + ref.code + "\" is not in itemtypes.txt");
            }
        }
        for (auto* list : { &rule.when.itemStats, &rule.when.itemStatsAnyOf, &rule.when.playerStats, &rule.when.playerStatsAnyOf }) {
            for (StatCheck& check : *list) {
                if (check.stat >= 0) continue;
                check.stat = resolver.StatId(check.name);
                if (check.stat < 0) fail("stat \"" + check.name + "\" is not in itemstatcost.txt");
            }
        }
        if (rule.when.formula) {
            for (FormulaStat& call : rule.when.formula->stats) {
                if (call.stat < 0) {
                    call.stat = resolver.StatId(call.statName);
                    if (call.stat < 0) {
                        fail("formula: stat \"" + call.statName + "\" is not in itemstatcost.txt");
                        continue;
                    }
                }
                if (!call.skillName.empty()) {
                    const int skill = resolver.SkillRow(call.skillName);
                    if (skill < 0) {
                        fail("formula: skill \"" + call.skillName + "\" is not in skills.txt");
                        continue;
                    }
                    call.param = skill;
                    if (resolver.StatPacksSkillLevel(call.stat)) call.mode = FormulaStat::Mode::SkillAnyLevel;
                }
            }
        }
        if (rule.enabled) {
            bool readsMods = false;
            for (const auto* list : { &rule.when.itemStats, &rule.when.itemStatsAnyOf }) {
                for (const StatCheck& check : *list) readsMods = readsMods || !IsPreIdentifyStat(check.stat);
            }
            if (rule.when.formula) {
                for (const FormulaStat& call : rule.when.formula->stats) {
                    readsMods = readsMods || (call.source == FormulaStat::Source::Item && !IsPreIdentifyStat(call.stat));
                }
            }
            rule.skipsUnidentified = readsMods;
        }
        if (rule.then.sound) {
            SoundRef& sound = *rule.then.sound;
            if (sound.kind == SoundRef::Kind::TableName) {
                sound.row = resolver.SoundRow(sound.name);
                if (sound.row < 0) {
                    problems.push_back(Problem{ Problem::Severity::Warning, where, rule.line,
                        "sound \"" + sound.name + "\" is not in sounds.txt; the rule plays no sound" });
                    rule.then.sound.reset();
                } else {
                    sound.kind = SoundRef::Kind::TableRow;
                }
            } else if (sound.kind == SoundRef::Kind::File) {
                if (!resolver.SoundFile(sound.file, sound.path)) {
                    problems.push_back(Problem{ Problem::Severity::Warning, where, rule.line,
                        "sound file \"" + sound.file + "\" is not in the sounds folder; the rule plays no sound" });
                    rule.then.sound.reset();
                }
            }
        }
        if (rule.then.icon && !resolver.IconFile(*rule.then.icon)) {
            problems.push_back(Problem{ Problem::Severity::Warning, where, rule.line,
                "icon \"" + rule.then.icon->name + "\" could not be prepared from the icons folder (see the plugin log); the rule shows no icon" });
            rule.then.icon.reset();
        }
    }
}

}  // namespace lootfilter
