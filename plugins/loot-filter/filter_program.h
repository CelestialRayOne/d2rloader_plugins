#pragma once
//
// The compiled form of one filter file.
//
// CompileFilter turns the filter JSON into this structure and reports every
// problem with the rule number, the property and the line. A rule with an error
// is switched off; the rest of the filter still loads. Only an unreadable file
// (bad JSON, or rules that are not a list) stops the whole filter.
//
// Names that need the game's data (item type codes, stat names, sound names,
// sound files) are resolved afterwards by ResolveNames, on the game thread.
//
#include "formula.h"

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace lootfilter {

// Placeholders that survive into finished text and are filled in at the last moment.
inline constexpr char OriginalNameToken = '\x1F';  // the game's own item name, in names
inline constexpr char ItemLinkToken     = '\x1E';  // the clickable item link, in chat lines

inline constexpr int CurrentFormatVersion = 1;

enum LocationBit : std::uint8_t {
    LocationGround   = 1,
    LocationStored   = 2,
    LocationEquipped = 4,
    LocationVendor   = 8,
};

struct Text {
    std::vector<std::pair<std::string, std::string>> variants;  // language code, text ("" = all languages)

    [[nodiscard]] auto Empty() const noexcept -> bool { return variants.empty(); }
    // The text for this language, else English, else the first one written.
    [[nodiscard]] auto Pick(std::string_view language) const noexcept -> const std::string&;
};

// A number condition: any value, one of a list, or a min/max range.
struct IntSpec {
    enum class Kind : std::uint8_t { Any, Values, Range };
    Kind             kind{ Kind::Any };
    std::vector<int> values;
    bool             hasMin{};
    bool             hasMax{};
    int              min{};
    int              max{};

    [[nodiscard]] auto IsSet() const noexcept -> bool { return kind != Kind::Any; }
    [[nodiscard]] auto Matches(int value) const noexcept -> bool;
};

struct TypeRef {
    int         row{ -1 };  // itemtypes.txt row
    std::string code;       // itemtypes.txt Code, resolved to row by ResolveNames
};

enum class StatOp : std::uint8_t { Equal, NotEqual, Greater, GreaterOrEqual, Less, LessOrEqual, Between };

struct StatCheck {
    int         stat{ -1 };  // itemstatcost.txt row
    std::string name;        // itemstatcost.txt Stat, resolved to stat by ResolveNames
    int         param{};
    StatOp      op{ StatOp::GreaterOrEqual };
    int         value{};
    int         value2{};    // upper bound for Between

    [[nodiscard]] auto Passes(int actual) const noexcept -> bool;
};

struct SoundRef {
    enum class Kind : std::uint8_t { None, File, TableRow, TableName };
    Kind         kind{ Kind::None };
    std::string  file;  // .wav, .mp3 or .flac name inside the filter's sounds folder
    std::wstring path;  // full path, filled by ResolveNames
    int          row{ -1 };
    std::string  name;  // sounds.txt Sound, resolved to row by ResolveNames
};

struct Rgba {
    std::uint8_t r{};
    std::uint8_t g{};
    std::uint8_t b{};
    std::uint8_t a{ 255 };
};

// A minimap icon: a .sprite from the filter's icons folder, drawn while the
// item lies on the ground.
struct IconSpec {
    std::string name;                       // file name in the icons folder, without ".sprite"
    int         size{};                     // longer side, in minimap pixels; 0 = the file's own size
    Rgba        color{ 255, 255, 255, 255 };  // tint; alpha fades the icon
    std::string spriteName;                 // the name the game loads it by, set by ResolveNames
    int         width{};                    // the loaded sprite's size, set by ResolveNames
    int         height{};
};

// The droplight effect on an item while it lies on the ground.
struct DroplightSpec {
    std::optional<Rgba> color;  // the beam's color; none keeps the game's own
};

struct Border {
    Rgba color;
    int  width{ 1 };
};

struct Conditions {
    std::vector<std::uint32_t> itemCodes;  // packed; empty = every item
    std::vector<std::uint32_t> excludeItemCodes;
    std::vector<TypeRef>       itemTypes;
    std::vector<TypeRef>       excludeItemTypes;
    IntSpec                    quality;
    IntSpec                    baseTier;
    IntSpec                    itemLevel;
    IntSpec                    sockets;
    IntSpec                    maxSockets;
    IntSpec                    difficulty;
    std::optional<bool>        isFullySocketed;
    std::optional<bool>        isEthereal;
    std::optional<bool>        isIdentified;
    std::optional<bool>        isRuneword;
    std::vector<int>           uniqueOrSetIds;
    std::vector<int>           excludeUniqueOrSetIds;
    std::vector<StatCheck>     itemStats;
    std::vector<StatCheck>     itemStatsAnyOf;
    std::vector<StatCheck>     playerStats;
    std::vector<StatCheck>     playerStatsAnyOf;
    std::uint8_t               locations{ LocationGround };
    std::vector<int>           areas;
    std::vector<int>           excludeAreas;
    std::vector<int>           activeInLevels;  // empty = every level
    std::optional<Formula>     formula;         // the "formula" condition
};

struct Actions {
    std::optional<bool>   hide;
    std::optional<bool>   hideIfOtherConditionsFail;
    std::optional<Text>   replaceName;
    std::optional<Text>   namePrefix;
    std::optional<Text>   nameSuffix;
    std::optional<Text>   chatMessage;
    std::optional<Text>   chatMessageWhenHidden;
    std::optional<SoundRef> sound;
    std::optional<Rgba>   labelBackground;
    std::optional<Border> labelBorder;
    std::optional<bool>   notify;        // print the filter's drop line when the item drops
    std::optional<int>    soundVolume;   // 0-100, percent of full volume
    std::optional<IconSpec> icon;        // minimap icon
    std::optional<DroplightSpec> droplight;
    std::optional<bool>   show;          // later rules cannot hide the item

    // Fills every action this one leaves out from the other (a rule over its style).
    void FillFrom(const Actions& other);
    [[nodiscard]] auto Empty() const noexcept -> bool;
};

struct Rule {
    int         number{};   // 1-based position in the file
    std::uint32_t line{};
    bool        enabled{ true };
    bool        skipsUnidentified{};  // checks an item stat the client only gets once the item is identified
    std::string comment;
    Conditions  when;
    Actions     then;
};

struct Level {
    int  id{};
    Text name;
};

// Chat lines the filter itself prints. An empty text means the built-in one.
struct Notifications {
    Text dropFormat;         // for rules with "notify": true and no chatMessage
    Text levelChangeFormat;  // when the filter level changes in game
};

// Name colors by item quality: the color code character (the X of ÿcX), or
// '\0' to keep the game's color. Index = quality 1..9.
struct QualityColors {
    std::array<char, 10> byQuality{};

    [[nodiscard]] auto For(int quality) const noexcept -> char {
        return quality >= 1 && quality <= 9 ? byQuality[static_cast<std::size_t>(quality)] : '\0';
    }
    [[nodiscard]] auto Any() const noexcept -> bool {
        for (char c : byQuality) {
            if (c != '\0') return true;
        }
        return false;
    }
};

inline constexpr char DefaultDropFormat[]        = "{qualityColor}{name}";
inline constexpr char DefaultLevelChangeFormat[] = "\xC3\xBF" "c4Loot filter level: \xC3\xBF" "c0{level}";

struct FilterProgram {
    int                formatVersion{ CurrentFormatVersion };
    std::string        title;
    std::string        author;
    std::string        version;
    std::string        description;
    std::string        updateUrl;
    std::vector<Level> levels;
    int                defaultLevel{};
    bool               hideUnmatchedItems{};
    bool               chatNotifications{ true };  // false: items print no chat lines (notify, chatMessage, chatMessageWhenHidden)
    std::optional<int> soundVolume;   // 0-100, for rules without their own
    Notifications      notifications;
    QualityColors      qualityColors;
    std::vector<Rule>  rules;

    [[nodiscard]] auto HasLevels() const noexcept -> bool { return !levels.empty(); }
    [[nodiscard]] auto FindLevel(int id) const noexcept -> const Level*;
    // The level to use when the player picked `wanted`: wanted if it exists,
    // else the filter's default, else the first level. 0 when the filter has none.
    [[nodiscard]] auto EffectiveLevel(int wanted) const noexcept -> int;
    // The level after `current`, wrapping around.
    [[nodiscard]] auto NextLevel(int current) const noexcept -> int;
    [[nodiscard]] auto EnabledRuleCount() const noexcept -> std::size_t;
};

struct Problem {
    enum class Severity : std::uint8_t { Error, Warning };
    Severity      severity{ Severity::Error };
    std::string   where;    // "rule 12 (\"Socket Donut\"), quality" or "filterLevels"
    std::uint32_t line{};
    std::string   message;

    [[nodiscard]] auto ToString() const -> std::string;
};

// Returns false when the file cannot be used at all. Problems are appended either way.
auto CompileFilter(std::string_view text, FilterProgram& program, std::vector<Problem>& problems) -> bool;

// Looks up the names a filter uses in the game's data. Returning -1 / false means "not found".
class NameResolver {
public:
    virtual ~NameResolver() = default;
    virtual auto ItemTypeRow(std::string_view code) -> int = 0;
    virtual auto StatId(std::string_view name) -> int = 0;
    virtual auto SoundRow(std::string_view name) -> int = 0;
    virtual auto SoundFile(std::string_view fileName, std::wstring& fullPath) -> bool = 0;
    // Prepares the icon file for the game: sets spriteName, width and height.
    virtual auto IconFile(IconSpec& icon) -> bool = 0;
    // skills.txt row of a skill name ("skill" column).
    virtual auto SkillRow(std::string_view name) -> int = 0;
    // True when the stat's param holds a skill and its level (itemstatcost Encode 2 or 3).
    virtual auto StatPacksSkillLevel(int stat) -> bool = 0;
};

// Resolves names in place. A rule that refers to something missing is switched off,
// except a missing sound or icon, which only drops that sound or icon. Also marks
// the rules that skip unidentified items.
void ResolveNames(FilterProgram& program, NameResolver& resolver, std::vector<Problem>& problems);

// The item stats the client has before an item is identified (the item packet
// carries them for every item): gold, armorclass, quantity, durability,
// maxdurability and item_numsockets. A rule checking any other item stat skips
// unidentified items.
auto IsPreIdentifyStat(int stat) noexcept -> bool;

// Item codes are compared as the packed four bytes the game uses, padded with spaces.
auto PackItemCode(std::string_view code) noexcept -> std::uint32_t;
auto UnpackItemCode(std::uint32_t code) -> std::string;

// Display names for the numbers conditions use.
auto QualityName(int quality) noexcept -> const char*;
auto BaseTierName(int tier) noexcept -> const char*;

}  // namespace lootfilter
