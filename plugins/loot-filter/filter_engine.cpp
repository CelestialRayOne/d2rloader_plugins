#include "filter_engine.h"

#include <algorithm>
#include <cstdlib>

namespace lootfilter {

namespace {

auto LocationBitOf(Location location) noexcept -> std::uint8_t {
    switch (location) {
    case Location::Ground:   return LocationGround;
    case Location::Stored:   return LocationStored;
    case Location::Equipped: return LocationEquipped;
    case Location::Vendor:   return LocationVendor;
    case Location::Other:    return 0;
    }
    return 0;
}

auto CodeMatches(const Conditions& when, std::uint32_t code) -> bool {
    if (!when.itemCodes.empty()
        && std::find(when.itemCodes.begin(), when.itemCodes.end(), code) == when.itemCodes.end()) {
        return false;
    }
    return std::find(when.excludeItemCodes.begin(), when.excludeItemCodes.end(), code) == when.excludeItemCodes.end();
}

auto StatsPass(const std::vector<StatCheck>& checks, bool anyOf, const auto& read) -> bool {
    if (checks.empty()) return true;
    for (const StatCheck& check : checks) {
        const bool passed = check.Passes(read(check.stat, check.param));
        if (anyOf && passed) return true;
        if (!anyOf && !passed) return false;
    }
    return !anyOf;
}

// A formula's stat calls, answered from the item and the player.
class RuleValues final : public FormulaValues {
public:
    RuleValues(const ItemSubject& item, const PlayerSubject* player) : item_(item), player_(player) {}

    [[nodiscard]] auto Value(const FormulaStat& call) const -> std::int64_t override {
        if (call.stat < 0) return 0;
        const bool anyLevel = call.mode == FormulaStat::Mode::SkillAnyLevel;
        if (call.source == FormulaStat::Source::Player) {
            if (player_ == nullptr) return 0;
            return anyLevel ? player_->StatForSkill(call.stat, call.param) : player_->Stat(call.stat, call.param);
        }
        switch (call.mode) {
        case FormulaStat::Mode::Exact:         return item_.Stat(call.stat, call.param);
        case FormulaStat::Mode::Max:           return item_.StatMax(call.stat);
        case FormulaStat::Mode::Sum:           return item_.StatSum(call.stat);
        case FormulaStat::Mode::SkillAnyLevel: return item_.StatForSkill(call.stat, call.param);
        }
        return 0;
    }

private:
    const ItemSubject&   item_;
    const PlayerSubject* player_;
};

// Every condition except the item code and the location.
auto OtherConditionsPass(const Conditions& when, const ItemSubject& item, const PlayerSubject* player) -> bool {
    if (when.quality.IsSet() && !when.quality.Matches(item.Quality())) return false;
    if (when.baseTier.IsSet() && !when.baseTier.Matches(item.BaseTier())) return false;
    if (when.itemLevel.IsSet() && !when.itemLevel.Matches(item.ItemLevel())) return false;
    if (when.sockets.IsSet() && !when.sockets.Matches(item.Sockets())) return false;
    if (when.maxSockets.IsSet() && !when.maxSockets.Matches(item.MaxSockets())) return false;
    if (when.isFullySocketed && (item.Sockets() >= item.MaxSockets()) != *when.isFullySocketed) return false;
    if (when.isEthereal && item.Ethereal() != *when.isEthereal) return false;
    if (when.isIdentified && item.Identified() != *when.isIdentified) return false;
    if (when.isRuneword && item.Runeword() != *when.isRuneword) return false;
    if (!when.uniqueOrSetIds.empty() || !when.excludeUniqueOrSetIds.empty()) {
        const int id = item.UniqueOrSetId();
        if (!when.uniqueOrSetIds.empty()
            && std::find(when.uniqueOrSetIds.begin(), when.uniqueOrSetIds.end(), id) == when.uniqueOrSetIds.end()) {
            return false;
        }
        if (std::find(when.excludeUniqueOrSetIds.begin(), when.excludeUniqueOrSetIds.end(), id) != when.excludeUniqueOrSetIds.end()) {
            return false;
        }
    }
    if (!when.itemTypes.empty()
        && std::none_of(when.itemTypes.begin(), when.itemTypes.end(), [&](const TypeRef& t) { return item.IsType(t.row); })) {
        return false;
    }
    if (std::any_of(when.excludeItemTypes.begin(), when.excludeItemTypes.end(), [&](const TypeRef& t) { return item.IsType(t.row); })) {
        return false;
    }
    if (!when.areas.empty() || !when.excludeAreas.empty()) {
        const int area = item.Area();
        if (!when.areas.empty() && std::find(when.areas.begin(), when.areas.end(), area) == when.areas.end()) return false;
        if (std::find(when.excludeAreas.begin(), when.excludeAreas.end(), area) != when.excludeAreas.end()) return false;
    }
    const auto itemStat = [&](int stat, int param) { return item.Stat(stat, param); };
    if (!StatsPass(when.itemStats, false, itemStat)) return false;
    if (!StatsPass(when.itemStatsAnyOf, true, itemStat)) return false;
    const bool needsPlayer = !when.playerStats.empty() || !when.playerStatsAnyOf.empty() || when.difficulty.IsSet();
    if (needsPlayer) {
        if (player == nullptr) return false;
        const auto playerStat = [&](int stat, int param) { return player->Stat(stat, param); };
        if (!StatsPass(when.playerStats, false, playerStat)) return false;
        if (!StatsPass(when.playerStatsAnyOf, true, playerStat)) return false;
        if (when.difficulty.IsSet() && !when.difficulty.Matches(player->Difficulty())) return false;
    }
    if (when.formula) {
        if (when.formula->readsPlayer && player == nullptr) return false;
        const RuleValues values(item, player);
        if (EvaluateFormula(*when.formula, values) == 0) return false;
    }
    return true;
}

auto ParseNumbers(std::string_view args, int& first, int& second) -> bool {
    first  = 0;
    second = 0;
    const std::size_t comma = args.find(',');
    const std::string_view a = args.substr(0, comma);
    const std::string_view b = comma == std::string_view::npos ? std::string_view{} : args.substr(comma + 1);
    if (a.empty() || a.size() > 6) return false;
    for (char c : a) {
        if (c < '0' || c > '9') return false;
        first = first * 10 + (c - '0');
    }
    if (comma != std::string_view::npos) {
        if (b.empty() || b.size() > 6) return false;
        for (char c : b) {
            if (c < '0' || c > '9') return false;
            second = second * 10 + (c - '0');
        }
    }
    return true;
}

// {string:KEY}: the game's text for KEY, or KEY itself when the game has none.
void AppendGameString(std::string& out, std::string_view key, const StringSource* strings) {
    std::string text;
    if (strings != nullptr && strings->GameString(key, text)) out += text;
    else out.append(key);
}

}  // namespace

auto ExpandPlainText(const std::string& text, const StringSource* strings, std::string_view level, std::string_view filter) -> std::string {
    std::string out;
    out.reserve(text.size() + 16);
    std::size_t pos = 0;
    while (pos < text.size()) {
        const std::size_t open = text.find('{', pos);
        if (open == std::string::npos) {
            out.append(text, pos, std::string::npos);
            break;
        }
        const std::size_t close = text.find('}', open + 1);
        if (close == std::string::npos) {
            out.append(text, pos, std::string::npos);
            break;
        }
        out.append(text, pos, open - pos);
        const std::string_view inner(text.data() + open + 1, close - open - 1);
        pos = close + 1;
        if (inner.rfind("string:", 0) == 0 && inner.size() > 7) AppendGameString(out, inner.substr(7), strings);
        else if (inner == "level") out.append(level);
        else if (inner == "filter") out.append(filter);
        else out.append(text, open, close - open + 1);
    }
    return out;
}

auto ExpandText(const std::string& text, const ItemSubject& item, const PlayerSubject* player, bool forName,
                const StringSource* strings) -> std::string {
    std::string out;
    out.reserve(text.size() + 16);
    std::size_t pos = 0;
    while (pos < text.size()) {
        const std::size_t open = text.find('{', pos);
        if (open == std::string::npos) {
            out.append(text, pos, std::string::npos);
            break;
        }
        const std::size_t close = text.find('}', open + 1);
        if (close == std::string::npos) {
            out.append(text, pos, std::string::npos);
            break;
        }
        out.append(text, pos, open - pos);
        const std::string_view inner(text.data() + open + 1, close - open - 1);
        pos = close + 1;
        if (inner == "name") {
            if (item.IsGold()) {
                out += std::to_string(item.Stat(14, 0));
                if (!forName) out += "G";
            } else if (forName) {
                out.push_back(OriginalNameToken);
            } else {
                out += item.DisplayName();
            }
        } else if (inner == "link") {
            if (forName) out += "{link}";
            else out.push_back(ItemLinkToken);
        } else if (inner == "code") {
            out += UnpackItemCode(item.Code());
        } else if (inner == "quality") {
            out += QualityName(item.Quality());
        } else if (inner == "baseTier") {
            out += BaseTierName(item.BaseTier());
        } else if (inner == "itemLevel") {
            out += std::to_string(item.ItemLevel());
        } else if (inner == "sockets") {
            out += std::to_string(item.Sockets());
        } else if (inner == "maxSockets") {
            out += std::to_string(item.MaxSockets());
        } else if (inner == "ethereal") {
            out += item.Ethereal() ? "true" : "false";
        } else if (inner == "uniqueOrSetId") {
            out += std::to_string(item.UniqueOrSetId());
        } else if (inner == "qualityColor") {
            const char color = item.NameColor();
            if (color != '\0') {
                out += "\xC3\xBF" "c";
                out.push_back(color);
            }
        } else if (inner.rfind("string:", 0) == 0 && inner.size() > 7) {
            AppendGameString(out, inner.substr(7), strings);
        } else if (inner.rfind("stat:", 0) == 0 || inner.rfind("playerStat:", 0) == 0) {
            const bool forPlayer = inner.front() == 'p';
            int stat = 0;
            int param = 0;
            if (ParseNumbers(inner.substr(inner.find(':') + 1), stat, param)) {
                if (forPlayer) out += player != nullptr ? std::to_string(player->Stat(stat, param)) : std::string("0");
                else out += std::to_string(item.Stat(stat, param));
            } else {
                out.append(text, open, close - open + 1);
            }
        } else {
            out.append(text, open, close - open + 1);
        }
    }
    return out;
}

auto ApplyNameTemplate(std::string_view nameTemplate, std::string_view originalName) -> std::string {
    std::string out;
    out.reserve(nameTemplate.size() + originalName.size());
    for (char c : nameTemplate) {
        if (c == OriginalNameToken) out.append(originalName);
        else out.push_back(c);
    }
    return out;
}

void Evaluate(const FilterProgram& program, const ItemSubject& item, const PlayerSubject* player,
              const EvalSettings& settings, Outcome& outcome) {
    outcome = Outcome{};
    outcome.nameTemplate.assign(1, OriginalNameToken);
    const Location location = item.Where();
    // Like D2RLAN: an unidentified item at a vendor is left alone, so a filter
    // cannot tell what an unidentified vendor or gamble item really is.
    if (location == Location::Vendor && !item.Identified()) return;

    const std::uint8_t locationBit = LocationBitOf(location);
    const bool onGround = location == Location::Ground;
    const std::uint32_t code = item.Code();
    bool anyMatch = false;
    bool shown = false;  // a matched "show" rule: no later rule hides the item
    const bool identified = item.Identified();
    std::string name = outcome.nameTemplate;

    for (const Rule& rule : program.rules) {
        if (!rule.enabled) continue;
        if (!rule.when.activeInLevels.empty()
            && std::find(rule.when.activeInLevels.begin(), rule.when.activeInLevels.end(), settings.level)
                   == rule.when.activeInLevels.end()) {
            continue;
        }
        if (!CodeMatches(rule.when, code)) continue;
        const bool locationOk = (rule.when.locations & locationBit) != 0;
        if (!locationOk) continue;
        // The client has no modifiers for an unidentified item, so a rule that
        // checks them skips it entirely: no actions, no hide.
        if (rule.skipsUnidentified && !identified) {
            outcome.skippedUnidentified.push_back(rule.number);
            continue;
        }
        if (!OtherConditionsPass(rule.when, item, player)) {
            if (onGround && !shown && rule.then.hideIfOtherConditionsFail.value_or(false)) {
                outcome.hidden = true;
                outcome.hiddenBy = rule.number;
                break;
            }
            continue;
        }
        anyMatch = true;
        outcome.matchedRules.push_back(rule.number);
        const Actions& then = rule.then;
        if (then.show.value_or(false) && !shown) {
            shown = true;
            outcome.shownBy = rule.number;
        }
        if (program.chatNotifications && then.chatMessage) {
            outcome.chatMessages.push_back(ExpandText(then.chatMessage->Pick(settings.language), item, player, false, settings.strings));
        } else if (program.chatNotifications && then.notify.value_or(false)) {
            const Text& format = program.notifications.dropFormat;
            outcome.chatMessages.push_back(ExpandText(format.Empty() ? std::string(DefaultDropFormat) : format.Pick(settings.language),
                                                      item, player, false, settings.strings));
        }
        if (then.sound) outcome.sounds.push_back(SoundPlay{ &*then.sound, then.soundVolume.value_or(program.soundVolume.value_or(100)) });
        if (onGround && !shown && then.hide.value_or(false)) {
            if (program.chatNotifications && then.chatMessageWhenHidden) {
                outcome.chatMessages.push_back(
                    ExpandText(then.chatMessageWhenHidden->Pick(settings.language), item, player, false, settings.strings));
            }
            outcome.hidden = true;
            outcome.hiddenBy = rule.number;
            break;
        }
        if (then.labelBackground) outcome.background = *then.labelBackground;
        if (then.icon && onGround) outcome.icon = &*then.icon;
        if (then.droplight && onGround) outcome.droplight = &*then.droplight;
        if (then.labelBorder) outcome.border = *then.labelBorder;
        if (then.replaceName) name = ExpandText(then.replaceName->Pick(settings.language), item, player, true, settings.strings);
        if (then.namePrefix) name = ExpandText(then.namePrefix->Pick(settings.language), item, player, true, settings.strings) + name;
        if (then.nameSuffix) name += ExpandText(then.nameSuffix->Pick(settings.language), item, player, true, settings.strings);
    }

    if (!outcome.hidden && !anyMatch && onGround && program.hideUnmatchedItems) {
        outcome.hidden = true;
        outcome.hiddenBy = -1;
    }
    if (outcome.hidden) {
        outcome.icon = nullptr;
        outcome.droplight = nullptr;
    }
    outcome.nameChanged = name.size() != 1 || name[0] != OriginalNameToken;
    outcome.nameTemplate = std::move(name);
}

}  // namespace lootfilter
