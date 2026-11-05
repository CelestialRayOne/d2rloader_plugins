#pragma once
//
// Runs a compiled filter against one item.
//
// The engine knows nothing about the game: it asks an ItemSubject and a
// PlayerSubject for facts, so the plugin can feed it live units and the tests
// can feed it plain values. Rules run top to bottom, every matching rule
// applies, name edits stack, and the first matching hide ends the evaluation,
// as in D2RLAN.
//
#include "filter_program.h"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace lootfilter {

enum class Location : std::uint8_t { Ground, Stored, Equipped, Vendor, Other };

class ItemSubject {
public:
    virtual ~ItemSubject() = default;
    [[nodiscard]] virtual auto Code() const -> std::uint32_t = 0;
    [[nodiscard]] virtual auto Quality() const -> int = 0;
    [[nodiscard]] virtual auto BaseTier() const -> int = 0;          // 0 normal, 1 exceptional, 2 elite, -1 none
    [[nodiscard]] virtual auto ItemLevel() const -> int = 0;
    [[nodiscard]] virtual auto Sockets() const -> int = 0;
    [[nodiscard]] virtual auto MaxSockets() const -> int = 0;
    [[nodiscard]] virtual auto Ethereal() const -> bool = 0;
    [[nodiscard]] virtual auto Identified() const -> bool = 0;
    [[nodiscard]] virtual auto Runeword() const -> bool = 0;
    [[nodiscard]] virtual auto UniqueOrSetId() const -> int = 0;
    [[nodiscard]] virtual auto Where() const -> Location = 0;
    [[nodiscard]] virtual auto Area() const -> int = 0;              // levels.txt id, -1 unknown
    [[nodiscard]] virtual auto IsType(int itemTypeRow) const -> bool = 0;
    // Stats read as the number the item tooltip prints (stat_values.h).
    [[nodiscard]] virtual auto Stat(int stat, int param) const -> int = 0;
    // Every param of one stat: the highest and the total value, 0 when it has none.
    [[nodiscard]] virtual auto StatMax(int stat) const -> int = 0;
    [[nodiscard]] virtual auto StatSum(int stat) const -> std::int64_t = 0;
    // For a stat whose param holds a skill and its level: the highest value of
    // the entries for `skill`, at any level.
    [[nodiscard]] virtual auto StatForSkill(int stat, int skill) const -> int = 0;
    [[nodiscard]] virtual auto IsGold() const -> bool = 0;
    [[nodiscard]] virtual auto DisplayName() const -> std::string = 0;  // plain name, for chat lines
    // The color code character the item's name shows in (the X of ÿcX), after
    // the filter's qualityColors. '\0' when unknown.
    [[nodiscard]] virtual auto NameColor() const -> char = 0;
};

// The game's own texts, for {string:KEY}.
class StringSource {
public:
    virtual ~StringSource() = default;
    // The text of a game string key in the game's language. False when there is none.
    virtual auto GameString(std::string_view key, std::string& text) const -> bool = 0;
};

class PlayerSubject {
public:
    virtual ~PlayerSubject() = default;
    [[nodiscard]] virtual auto Stat(int stat, int param) const -> int = 0;  // as the tooltip prints it
    [[nodiscard]] virtual auto StatForSkill(int stat, int skill) const -> int = 0;
    [[nodiscard]] virtual auto Difficulty() const -> int = 0;        // 0-2, -1 unknown
};

struct EvalSettings {
    int                 level{};
    std::string_view    language{ "enUS" };
    const StringSource* strings{};  // null: {string:KEY} shows the key
};

struct SoundPlay {
    const SoundRef* sound{};
    int             volume{ 100 };  // 0-100, the rule's soundVolume, else the filter's, else 100
};

struct Outcome {
    bool                        hidden{};
    bool                        nameChanged{};
    std::string                 nameTemplate;   // OriginalNameToken marks where the game's name goes
    std::optional<Rgba>         background;
    std::optional<Border>       border;
    std::vector<std::string>    chatMessages;   // ItemLinkToken marks the item link
    std::vector<SoundPlay>      sounds;
    const IconSpec*             icon{};         // minimap icon; null when none or when hidden
    const DroplightSpec*        droplight{};    // null when none or when hidden
    std::vector<int>            matchedRules;   // rule numbers, in order
    int                         hiddenBy{};     // rule number that hid the item, -1 for hideUnmatchedItems, 0 when shown
    int                         shownBy{};      // rule number whose "show" kept later rules from hiding it, 0 for none
    std::vector<int>            skippedUnidentified;  // rules skipped because the item is unidentified
};

void Evaluate(const FilterProgram& program, const ItemSubject& item, const PlayerSubject* player,
              const EvalSettings& settings, Outcome& outcome);

// Puts the game's name where the template asks for it.
auto ApplyNameTemplate(std::string_view nameTemplate, std::string_view originalName) -> std::string;

// Fills the {...} placeholders. In names, {name} becomes OriginalNameToken; in
// chat lines it becomes the item's name, and {link} becomes ItemLinkToken.
auto ExpandText(const std::string& text, const ItemSubject& item, const PlayerSubject* player, bool forName,
                const StringSource* strings) -> std::string;

// Fills the placeholders of texts that are not about an item: {string:KEY},
// and {level} and {filter} for the level change line.
auto ExpandPlainText(const std::string& text, const StringSource* strings, std::string_view level = {},
                     std::string_view filter = {}) -> std::string;

}  // namespace lootfilter
