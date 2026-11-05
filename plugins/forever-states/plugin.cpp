// ===========================================================================
// Forever States (celestialrayone.forever-states)
// ===========================================================================
//
// Server side. A state that a skill grants (skills.txt aurastate) lasts while
// the player has one of the stats its rule names, comes back after save and
// exit, and Consume is no longer ended by binding demons. When the player
// loses the stat (the item giving it is unequipped or swapped out), a state
// the plugin was keeping ends on the next frame through the game's own expiry.
//
// ---------------------------------------------------------------------------
// 1. How a skill state ends (D2R 3.3.93847, D2RLoader 1.3.1 process image)
// ---------------------------------------------------------------------------
//   States like Consume are made by 433D20, the generic state applicator (a
//   loader thunk into D2RCore!ApplyWideTimedStatEffect, args below). It stores
//   the end frame on the state's stat list as a float at +24h, sets timer flag
//   2 at +1Ch, and schedules unit event 12 for that frame (EVENT_SetEvent
//   48B720). When event 12 fires, 42EA80 runs the expiry pass, which tail-calls
//   j_ExpireWideUnitStats 2F82F0 with the game frame [game+170h]; it removes
//   every list that has flag 2 and an end frame at or before that frame. The
//   client's copy of a state is made with no timer, so only the server ends it.
//
// ---------------------------------------------------------------------------
// 2. Why binding demons ended Consume
// ---------------------------------------------------------------------------
//   Consume is srvdofunc 159 (521970). skills.txt periodic = 1, so the timer
//   437230 runs it again every perdelay frames with unit flag 20000000h at
//   +128h set. In that mode it reads stat 84 from the Consume list (the pettype
//   of the eaten pet, written at cast), asks 4FEB50 whether that pettype is
//   full, and if so unlinks (2F7920) and frees (2F4180) the list. Binding up to
//   the petmax that Consume itself raised filled the pettype, so Consume ended,
//   petmax dropped back and the pet list trimmed the extra demons. Bind Demon's
//   own srvdofunc 158 (520CF0) only works on the demon, never on the caster.
//
// ---------------------------------------------------------------------------
// 3. What the plugin does
// ---------------------------------------------------------------------------
//   Keep (every frame). A player's stat regen event (type 3) reschedules itself
//   every frame at 42E634 through EVENT_SetEvent. A relay on that call checks
//   each rule state on the player after the event is set:
//     - stat present: the end frame is kept at least HoldFrames ahead (2F7AB0),
//       and the state is marked as kept;
//     - stat gone on a kept state: the end frame becomes the current frame and
//       an event 12 is set for the next frame, so the game's own expiry pass
//       removes it (state bit, client update and remove callback included);
//     - a state that was never kept keeps its normal timer.
//   Consume. An inline hook on 521970 returns 1 in periodic mode while the
//   player has the rule's stat, which is the value that branch returns when
//   it removes nothing.
//   Save. The player save 52F090 writes the bound demon area last through
//   533AE0, once into a measuring buffer (call at 52F717) and once for real
//   (52F830). Relays on both calls append one "FRVS" block after whatever
//   533AE0 wrote, Summon Enhancements' blocks included. 52F090 takes the save
//   size from the cursor after those calls, so the block is counted.
//   Load. The player load 52E910 reads the bound demon area through 531510
//   (call at 52EFC8) and never reads its cursor again. A relay on that call
//   finds the block (scanning from the area's start, CRC-checked) and rebuilds
//   the kept states BEFORE 531510 runs, so Summon Enhancements brings the
//   demons back with Consume's doubled pet cap already there. Pet caps are
//   worked out again later (5005A0 works every pettype's petmax out and trims
//   the pets over it, run among others by the state remove callback 436240);
//   with Consume rebuilt first, that never finds the cap halved. Item stats do
//   not count at load, so the rule's stat is checked afterwards: the state is
//   kept on probation until the stat counts, or ends 15 s after the player is
//   in the world without it.
//   Every helper the plugin calls is proven through a game call that reaches
//   it, never by its entry bytes, so plugins hooking those entries (crossbow-
//   charges, damage-overflow-fix) do not switch this one off.
//   Save rule. A state is written when the plugin keeps it or the player has
//   the stat. If the game saves after a kept state is already gone, the copy
//   the keep check takes every HoldFrames is written instead.
//   Rebuild. Exactly what Consume's cast does: 433D20, then j_SetWideListStat
//   2F3030 for every saved stat, then 350B40 (the unit speed refresh Consume
//   calls), the last one once the player is in a room (34B440). A state the
//   game refuses at load is tried again at world entry (the relay on the
//   53BC60 call at 4856F1, the last call of 485350) and then every frame.
//
// ---------------------------------------------------------------------------
// 4. Layouts relied on (each read the same way by the game code named)
// ---------------------------------------------------------------------------
//   Unit: +0 type (0 player), +8 unit id, +128h extra flags (34FB00).
//   Game: +106h data context byte (521970), +170h current frame (42E615).
//   State stat list: +1Ch flags, +24h end frame (float, 2F7AB0), +28h skill,
//   +2Ch skill level (written by ApplyWideTimedStatEffect), +30h stat records,
//   +38h record count; a record is 16 bytes, key (stat << 32 | layer) then the
//   value (D2RCore!ReadWideListStat). Before saving, the first records are
//   read back through the game's own reader 2F5DF0 and must match.
//   skills.txt record: +A0h aurastate (521970).
//
// ===========================================================================

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <D2RLPlugin/api.h>

#include <Windows.h>

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <deque>
#include <string>
#include <string_view>
#include <vector>

namespace {

// PURE-BEGIN ------------------------------------------------------------------
// Config parsing and the save block. No game or Windows code, so the test
// harness compiles this part on its own.

struct Rule {
    std::int32_t skill = -1;
    std::int32_t stat  = -1;
};

struct Config {
    bool                     enabled = true;
    bool                     haveRules = false;
    std::vector<Rule>        rules;
    std::vector<std::string> problems;
};

constexpr std::int32_t MaxSkillId = 0xFFFF;
constexpr std::int32_t MaxStatId  = 0x7FFF;
constexpr std::size_t  MaxRules   = 256;

inline auto Trim(std::string_view s) noexcept -> std::string_view {
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t' || s.front() == '\r' || s.front() == '\n')) s.remove_prefix(1);
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r' || s.back() == '\n')) s.remove_suffix(1);
    return s;
}

inline auto StripComment(std::string_view line) noexcept -> std::string_view {
    const std::size_t hash = line.find('#');
    return hash == std::string_view::npos ? line : line.substr(0, hash);
}

inline auto BracketDepth(std::string_view text) noexcept -> int {
    int depth = 0;
    for (const char c : text) {
        if (c == '[') ++depth;
        if (c == ']') --depth;
    }
    return depth;
}

// value := integer | '[' [value (',' value)* [',']] ']'
struct ArrayNode {
    bool                   isArray = false;
    std::int64_t           value   = 0;
    std::vector<ArrayNode> items;
};

class ArrayParser {
public:
    explicit ArrayParser(std::string_view text) noexcept : text_(text) {}

    auto Parse(ArrayNode& out, std::string& error) -> bool {
        SkipSpaces();
        if (!Value(out, error, 0)) return false;
        SkipSpaces();
        if (pos_ != text_.size()) {
            error = "unexpected text after the closing ]";
            return false;
        }
        return true;
    }

private:
    void SkipSpaces() noexcept {
        while (pos_ < text_.size() && (text_[pos_] == ' ' || text_[pos_] == '\t' || text_[pos_] == '\r' || text_[pos_] == '\n')) {
            ++pos_;
        }
    }

    auto Value(ArrayNode& out, std::string& error, int depth) -> bool {
        if (depth > 4) {
            error = "arrays nested too deep";
            return false;
        }
        SkipSpaces();
        if (pos_ >= text_.size()) {
            error = "the array ends early";
            return false;
        }
        if (text_[pos_] == '[') {
            ++pos_;
            out.isArray = true;
            SkipSpaces();
            if (pos_ < text_.size() && text_[pos_] == ']') {
                ++pos_;
                return true;
            }
            for (;;) {
                ArrayNode item;
                if (!Value(item, error, depth + 1)) return false;
                out.items.push_back(std::move(item));
                SkipSpaces();
                if (pos_ >= text_.size()) {
                    error = "an array is missing its ]";
                    return false;
                }
                if (text_[pos_] == ',') {
                    ++pos_;
                    SkipSpaces();
                    if (pos_ < text_.size() && text_[pos_] == ']') {
                        ++pos_;
                        return true;
                    }
                    continue;
                }
                if (text_[pos_] == ']') {
                    ++pos_;
                    return true;
                }
                error = "expected , or ] in an array";
                return false;
            }
        }
        bool negative = false;
        if (text_[pos_] == '-' || text_[pos_] == '+') {
            negative = text_[pos_] == '-';
            ++pos_;
        }
        if (pos_ >= text_.size() || text_[pos_] < '0' || text_[pos_] > '9') {
            error = "expected a whole number";
            return false;
        }
        std::int64_t number = 0;
        while (pos_ < text_.size() && ((text_[pos_] >= '0' && text_[pos_] <= '9') || text_[pos_] == '_')) {
            if (text_[pos_] != '_') {
                number = number * 10 + (text_[pos_] - '0');
                if (number > 0x7FFFFFFF) {
                    error = "a number is too large";
                    return false;
                }
            }
            ++pos_;
        }
        out.value = negative ? -number : number;
        return true;
    }

    std::string_view text_;
    std::size_t      pos_ = 0;
};

// rules = [[skill, stat], ...], or the flat form [skill, stat, skill, stat].
inline auto ParseRules(std::string_view text, std::vector<Rule>& out, std::vector<std::string>& problems) -> bool {
    ArrayNode   root;
    std::string error;
    ArrayParser parser(text);
    if (!parser.Parse(root, error)) {
        problems.push_back("rules could not be read (" + error + ")");
        return false;
    }
    if (!root.isArray) {
        problems.push_back("rules must be an array");
        return false;
    }
    std::vector<std::pair<std::int64_t, std::int64_t>> pairs;
    bool nested = !root.items.empty();
    bool flat   = true;
    for (const ArrayNode& item : root.items) {
        if (item.isArray) {
            flat = false;
        } else {
            nested = false;
        }
    }
    if (root.items.empty()) {
        out.clear();
        return true;
    }
    if (nested) {
        for (const ArrayNode& item : root.items) {
            if (item.items.size() != 2 || item.items[0].isArray || item.items[1].isArray) {
                problems.push_back("every rule must be [skill id, stat id]");
                return false;
            }
            pairs.emplace_back(item.items[0].value, item.items[1].value);
        }
    } else if (flat && root.items.size() % 2 == 0) {
        for (std::size_t i = 0; i < root.items.size(); i += 2) pairs.emplace_back(root.items[i].value, root.items[i + 1].value);
    } else {
        problems.push_back("rules must be pairs: [[skill id, stat id], ...]");
        return false;
    }
    out.clear();
    for (const auto& pair : pairs) {
        if (out.size() >= MaxRules) {
            problems.push_back("more than 256 rules; the rest are ignored");
            break;
        }
        if (pair.first < 0 || pair.first > MaxSkillId) {
            problems.push_back("rule skill " + std::to_string(pair.first) + " is not a skills.txt id; rule ignored");
            continue;
        }
        if (pair.second < -1 || pair.second > MaxStatId) {
            problems.push_back("rule stat " + std::to_string(pair.second) + " for skill " + std::to_string(pair.first)
                + " is not an itemstatcost.txt id; rule ignored");
            continue;
        }
        out.push_back({ static_cast<std::int32_t>(pair.first), static_cast<std::int32_t>(pair.second) });
    }
    return true;
}

inline auto ParseConfig(std::string_view text) -> Config {
    Config      config;
    std::string section;
    bool        sawSection = false;
    std::size_t pos        = 0;
    while (pos < text.size()) {
        std::size_t eol = text.find('\n', pos);
        if (eol == std::string_view::npos) eol = text.size();
        std::string_view line = Trim(StripComment(text.substr(pos, eol - pos)));
        pos = eol + 1;
        if (line.empty()) continue;
        if (line.front() == '[') {
            if (line.size() > 1 && line[1] == '[') {
                section = "[[]]";
                continue;
            }
            const std::size_t close = line.find(']');
            section = std::string(Trim(line.substr(1, close == std::string_view::npos ? std::string_view::npos : close - 1)));
            if (section == "forever-states") sawSection = true;
            continue;
        }
        if (section != "forever-states") continue;
        const std::size_t equals = line.find('=');
        if (equals == std::string_view::npos) {
            config.problems.push_back("line \"" + std::string(line) + "\" is not key = value");
            continue;
        }
        const std::string_view key   = Trim(line.substr(0, equals));
        std::string            value = std::string(Trim(line.substr(equals + 1)));
        if (key == "enabled") {
            if (value == "true") {
                config.enabled = true;
            } else if (value == "false") {
                config.enabled = false;
            } else {
                config.problems.push_back("enabled must be true or false");
            }
        } else if (key == "rules") {
            while (BracketDepth(value) > 0 && pos < text.size()) {
                std::size_t next = text.find('\n', pos);
                if (next == std::string_view::npos) next = text.size();
                value += ' ';
                value += std::string(Trim(StripComment(text.substr(pos, next - pos))));
                pos = next + 1;
            }
            if (ParseRules(value, config.rules, config.problems)) config.haveRules = true;
        } else {
            config.problems.push_back("unknown key \"" + std::string(key) + "\" ignored");
        }
    }
    if (!sawSection) config.problems.push_back("no [forever-states] table");
    return config;
}

// One kept state as written to the save.
struct SavedStat {
    std::uint16_t stat  = 0;
    std::uint16_t layer = 0;
    std::int32_t  value = 0;
};

struct SavedState {
    std::uint16_t          state = 0;
    std::uint16_t          skill = 0;
    std::int32_t           level = 0;
    std::vector<SavedStat> stats;
};

// Block: "FRVS", format, reserved byte, u16 state count, u32 payload size,
// payload, CRC-32 of the payload. Per state: u16 state, u16 skill, i32 level,
// u16 stat count, u16 reserved, then per stat u16 stat, u16 layer, i32 value.
constexpr std::uint8_t BlockMagic[4]{ 'F', 'R', 'V', 'S' };
constexpr std::uint8_t BlockFormat      = 1;
constexpr std::size_t  BlockHeaderSize  = 12;
constexpr std::size_t  BlockTrailerSize = 4;
constexpr std::size_t  StateHeaderSize  = 12;
constexpr std::size_t  StatRecordSize   = 8;
constexpr std::size_t  MaxSavedStates   = 64;
constexpr std::size_t  MaxStatsPerState = 512;

inline void Store16(std::uint8_t* p, std::uint16_t v) noexcept { std::memcpy(p, &v, sizeof(v)); }
inline void Store32(std::uint8_t* p, std::uint32_t v) noexcept { std::memcpy(p, &v, sizeof(v)); }
inline auto Load16(const std::uint8_t* p) noexcept -> std::uint16_t {
    std::uint16_t v = 0;
    std::memcpy(&v, p, sizeof(v));
    return v;
}
inline auto Load32(const std::uint8_t* p) noexcept -> std::uint32_t {
    std::uint32_t v = 0;
    std::memcpy(&v, p, sizeof(v));
    return v;
}

inline auto Crc32(const std::uint8_t* p, std::size_t n) noexcept -> std::uint32_t {
    std::uint32_t crc = 0xFFFFFFFFu;
    for (std::size_t i = 0; i < n; ++i) {
        crc ^= p[i];
        for (int bit = 0; bit < 8; ++bit) crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
    }
    return ~crc;
}

inline auto StateBytes(const SavedState& state) noexcept -> std::size_t {
    return StateHeaderSize + StatRecordSize * state.stats.size();
}

// Writes as many whole states as fit in room bytes. Returns the block size,
// 0 when not even one state fits; written receives the number of states.
inline auto EncodeBlock(std::uint8_t* out, std::size_t room, const std::vector<SavedState>& states, std::size_t& written)
        -> std::size_t {
    written = 0;
    if (out == nullptr || room < BlockHeaderSize + BlockTrailerSize) return 0;
    std::size_t payload = 0;
    for (const SavedState& state : states) {
        if (written >= MaxSavedStates || state.stats.size() > MaxStatsPerState) break;
        const std::size_t bytes = StateBytes(state);
        if (BlockHeaderSize + payload + bytes + BlockTrailerSize > room) break;
        payload += bytes;
        ++written;
    }
    if (written == 0) return 0;
    std::uint8_t* p = out + BlockHeaderSize;
    for (std::size_t i = 0; i < written; ++i) {
        const SavedState& state = states[i];
        Store16(p, state.state);
        Store16(p + 2, state.skill);
        Store32(p + 4, static_cast<std::uint32_t>(state.level));
        Store16(p + 8, static_cast<std::uint16_t>(state.stats.size()));
        Store16(p + 10, 0);
        p += StateHeaderSize;
        for (const SavedStat& stat : state.stats) {
            Store16(p, stat.stat);
            Store16(p + 2, stat.layer);
            Store32(p + 4, static_cast<std::uint32_t>(stat.value));
            p += StatRecordSize;
        }
    }
    std::memcpy(out, BlockMagic, sizeof(BlockMagic));
    out[4] = BlockFormat;
    out[5] = 0;
    Store16(out + 6, static_cast<std::uint16_t>(written));
    Store32(out + 8, static_cast<std::uint32_t>(payload));
    Store32(out + BlockHeaderSize + payload, Crc32(out + BlockHeaderSize, payload));
    return BlockHeaderSize + payload + BlockTrailerSize;
}

// Decodes the block at p when a whole, valid one lies before end. Returns its
// size, 0 otherwise (out untouched).
inline auto DecodeBlockAt(const std::uint8_t* p, const std::uint8_t* end, std::vector<SavedState>& out) -> std::size_t {
    if (p == nullptr || end == nullptr || end < p) return 0;
    const auto available = static_cast<std::size_t>(end - p);
    if (available < BlockHeaderSize + BlockTrailerSize || std::memcmp(p, BlockMagic, sizeof(BlockMagic)) != 0
            || p[4] != BlockFormat || p[5] != 0) {
        return 0;
    }
    const std::size_t count   = Load16(p + 6);
    const std::size_t payload = Load32(p + 8);
    if (count == 0 || count > MaxSavedStates || payload > available - BlockHeaderSize - BlockTrailerSize) return 0;
    const std::uint8_t* body = p + BlockHeaderSize;
    if (Crc32(body, payload) != Load32(body + payload)) return 0;
    std::vector<SavedState> states;
    std::size_t             used = 0;
    for (std::size_t i = 0; i < count; ++i) {
        if (payload - used < StateHeaderSize) return 0;
        const std::uint8_t* s = body + used;
        SavedState state;
        state.state = Load16(s);
        state.skill = Load16(s + 2);
        state.level = static_cast<std::int32_t>(Load32(s + 4));
        const std::size_t stats = Load16(s + 8);
        if (stats > MaxStatsPerState || payload - used - StateHeaderSize < stats * StatRecordSize) return 0;
        const std::uint8_t* r = s + StateHeaderSize;
        for (std::size_t k = 0; k < stats; ++k) {
            state.stats.push_back({ Load16(r), Load16(r + 2), static_cast<std::int32_t>(Load32(r + 4)) });
            r += StatRecordSize;
        }
        used += StateHeaderSize + stats * StatRecordSize;
        states.push_back(std::move(state));
    }
    if (used != payload) return 0;
    out = std::move(states);
    return BlockHeaderSize + payload + BlockTrailerSize;
}

// The first valid block in [begin, end). at receives where it starts.
inline auto FindBlock(const std::uint8_t* begin, const std::uint8_t* end, std::vector<SavedState>& out,
        const std::uint8_t** at) -> std::size_t {
    if (begin == nullptr || end == nullptr || end <= begin) return 0;
    for (const std::uint8_t* p = begin; static_cast<std::size_t>(end - p) >= BlockHeaderSize + BlockTrailerSize; ++p) {
        if (p[0] != BlockMagic[0] || std::memcmp(p, BlockMagic, sizeof(BlockMagic)) != 0) continue;
        const std::size_t size = DecodeBlockAt(p, end, out);
        if (size != 0) {
            if (at != nullptr) *at = p;
            return size;
        }
    }
    return 0;
}

// PURE-END --------------------------------------------------------------------

// ---------------------------------------------------------------------------
// Addresses (D2RLoader 1.3.1 process image, D2R 3.3.93847)
// ---------------------------------------------------------------------------

constexpr std::uint64_t ConsumeRva        = 0x521970;  // srvdofunc 159 (game, unit, skill, level)
constexpr std::uint64_t SetEventRva       = 0x48B720;  // EVENT_SetEvent (game, unit, type, frame, id, param, arg7)
constexpr std::uint64_t WriteBlockRva     = 0x533AE0;  // (game, player, cursor**, end)
constexpr std::uint64_t ReadBlockRva      = 0x531510;  // (game, player, cursor**, end, version)
constexpr std::uint64_t WarpPetsRva       = 0x53BC60;  // (game, player)
constexpr std::uint64_t GetStatListRva    = 0x2F5940;  // (unit, state) -> list
constexpr std::uint64_t ReadListStatRva   = 0x2F5DF0;  // (context, list, stat, layer), loader thunk
constexpr std::uint64_t SetListStatRva    = 0x2F3030;  // (context, list, stat, value, layer), loader thunk
constexpr std::uint64_t SetExpireFrameRva = 0x2F7AB0;  // (list, frame): flag 2, float at +24h
constexpr std::uint64_t GetUnitStatRva    = 0x2F5020;  // (unit, stat, layer), loader thunk
constexpr std::uint64_t ApplyStateRva     = 0x433D20;  // (args) -> list, loader thunk
constexpr std::uint64_t UnitSpeedRva      = 0x350B40;  // (unit, file, line)
constexpr std::uint64_t StatesCountRva    = 0x2141A0;  // (context) -> states.txt rows
constexpr std::uint64_t SkillRecordRva    = 0x97790;   // (context, skill) -> skills.txt record
constexpr std::uint64_t DataTablesRva     = 0x300A90;  // (context) -> data tables
constexpr std::uint64_t UnitRoomRva       = 0x34B440;  // (unit) -> active room, null outside the world

constexpr std::uint32_t PlayerUnitType     = 0;
constexpr std::size_t   UnitGuidOffset     = 0x08;
constexpr std::size_t   UnitFlagExOffset   = 0x128;
constexpr std::uint32_t PeriodicRunFlag    = 0x20000000;
constexpr std::size_t   GameContextOffset  = 0x106;
constexpr std::size_t   GameFrameOffset    = 0x170;
constexpr std::size_t   ListFlagsOffset    = 0x1C;
constexpr std::uint32_t ListTimedFlag      = 0x2;
constexpr std::size_t   ListExpireOffset   = 0x24;
constexpr std::size_t   ListSkillOffset    = 0x28;
constexpr std::size_t   ListLevelOffset    = 0x2C;
constexpr std::size_t   ListRecordsOffset  = 0x30;
constexpr std::size_t   ListCountOffset    = 0x38;
constexpr std::size_t   RecordSize         = 0x10;
constexpr std::size_t   SkillAuraStateAt   = 0xA0;
constexpr std::size_t   SkillCountOffset   = 0x11B8;
constexpr std::int32_t  ExpiryEventType    = 12;
constexpr std::int32_t  HoldFrames         = 25;
constexpr std::int32_t  RestoreFrames      = 2 * HoldFrames;
constexpr std::int32_t  RestoreWaitFrames  = 15 * 25;  // a restore waits up to 15 s in the world for the stat
constexpr std::int32_t  LoadKeepFrames     = 90 * 25;  // first life of a state made at load, outlasts any loading screen
constexpr std::size_t   MaxHeld            = 256;
constexpr std::size_t   MaxPending         = 64;
constexpr std::size_t   ContextSlots       = 8;
constexpr std::int32_t  Unresolved         = -2;

// ---------------------------------------------------------------------------
// Byte witnesses (read from the live IDB)
// ---------------------------------------------------------------------------
// Every function the plugin calls is proven by a game call that reaches it,
// never by its own entry bytes: other plugins hook some of those entries
// (damage-overflow-fix rewrites the 2F3030 thunk, crossbow-charges hooks the
// player regen handler), and their hooks must not switch this plugin off.

// 521970: mov [rsp+8],rbx / mov [rsp+20h],r9d / mov [rsp+18h],r8d / push rbp,
// rsi, rdi, r12, r13, r14, r15 / sub rsp,0B0h. No relative operand.
constexpr std::uint8_t ConsumeEntry[]{
    0x48, 0x89, 0x5C, 0x24, 0x08, 0x44, 0x89, 0x4C, 0x24, 0x20, 0x44, 0x89, 0x44, 0x24, 0x18, 0x55, 0x56,
    0x57, 0x41, 0x54, 0x41, 0x55, 0x41, 0x56, 0x41, 0x57, 0x48, 0x81, 0xEC, 0xB0, 0x00, 0x00, 0x00,
};

// Consume's own calls (all inside 521970).
// 5219AD: movzx ecx,byte [rbx+106h] / mov edx,r12d / call 97790 (skill record).
constexpr std::uint8_t SkillRecordCall[]{ 0x0F, 0xB6, 0x8B, 0x06, 0x01, 0x00, 0x00, 0x41, 0x8B, 0xD4, 0xE8, 0xD4, 0x5D, 0xB7, 0xFF };
// 5219D7: movzx ecx,byte [rbx+106h] / call 2141A0 (states count).
constexpr std::uint8_t StatesCountCall[]{ 0x0F, 0xB6, 0x8B, 0x06, 0x01, 0x00, 0x00, 0xE8, 0xBD, 0x27, 0xCF, 0xFF };
// 521A64: movsx edx,word [rbp+0A0h] (aurastate) / mov rcx,rsi / call 2F5940.
constexpr std::uint8_t GetStatListCall[]{ 0x0F, 0xBF, 0x95, 0xA0, 0x00, 0x00, 0x00, 0x48, 0x8B, 0xCE, 0xE8, 0xCD, 0x3E, 0xDD, 0xFF };
// 521A7B: context / xor r9d,r9d / mov rdx,rax / lea r8d,[r9+54h] / call 2F5DF0.
constexpr std::uint8_t ReadListStatCall[]{
    0x0F, 0xB6, 0x8B, 0x06, 0x01, 0x00, 0x00, 0x45, 0x33, 0xC9, 0x48, 0x8B, 0xD0, 0x45, 0x8D, 0x41, 0x54,
    0xE8, 0x5F, 0x43, 0xDD, 0xFF,
};
// 521BAA: call 433D20 (the Consume cast's state creation).
constexpr std::uint8_t ApplyStateCall[]{ 0xE8, 0x71, 0x21, 0xF1, 0xFF };
// 521D57: call 2F3030 (the consumestat loop's stat setter).
constexpr std::uint8_t SetListStatCall[]{ 0xE8, 0xD4, 0x12, 0xDD, 0xFF };
// 521DA0: mov r8d,591h / lea rdx,[file] / mov rcx,rsi / call 350B40.
constexpr std::uint8_t UnitSpeedCall[]{
    0x41, 0xB8, 0x91, 0x05, 0x00, 0x00, 0x48, 0x8D, 0x15, 0x83, 0x9B, 0x81, 0x01, 0x48, 0x8B, 0xCE,
    0xE8, 0x8B, 0xED, 0xE2, 0xFF,
};
// 43AC59 (inside 43AC00): call 48B720 / mov edx,ebx / mov rcx,rsi / call 2F7AB0.
constexpr std::uint8_t EventAndEndFrameCalls[]{ 0xE8, 0xC2, 0x0A, 0x05, 0x00, 0x8B, 0xD3, 0x48, 0x8B, 0xCE, 0xE8, 0x48, 0xCE, 0xEB, 0xFF };
// 440AB8: xor r8d,r8d / lea edx,[rax+63h] / mov rcx,rbx / call 2F5020 (stat 100, layer 0).
constexpr std::uint8_t GetUnitStatCall[]{ 0x45, 0x33, 0xC0, 0x8D, 0x50, 0x63, 0x48, 0x8B, 0xCB, 0xE8, 0x5A, 0x45, 0xEB, 0xFF };
// 52EFFD (inside the player load 52E910): mov rcx,r14 / call 34B440.
constexpr std::uint8_t UnitRoomCall[]{ 0x49, 0x8B, 0xCE, 0xE8, 0x3B, 0xC4, 0xE1, 0xFF };
// 977A2 (inside the skill record lookup 97790): call 300A90.
constexpr std::uint8_t DataTablesCall[]{ 0xE8, 0xE9, 0x92, 0x26, 0x00 };

// The relayed call sites.
// 42E615: mov r9d,[rcx+170h] ... inc r9d ... mov r8d,3 / call 48B720.
constexpr std::uint8_t TickWindow[]{
    0x44, 0x8B, 0x89, 0x70, 0x01, 0x00, 0x00, 0x48, 0x8B, 0xDA, 0x89, 0x44, 0x24, 0x28, 0x41, 0xFF, 0xC1, 0x44,
    0x89, 0x44, 0x24, 0x20, 0x48, 0x8B, 0xE9, 0x41, 0xB8, 0x03, 0x00, 0x00, 0x00, 0xE8, 0xE7, 0xD0, 0x05, 0x00,
};
// 52F6FC: cmp byte [rsi+106h],3 / jne / lea r9,[rbp+8120h] / mov rdx,r15 /
// lea r8,[rsp+58h] / mov rcx,rsi / call 533AE0 (measuring pass).
constexpr std::uint8_t SaveMeasureWindow[]{
    0x80, 0xBE, 0x06, 0x01, 0x00, 0x00, 0x03, 0x75, 0x1F, 0x4C, 0x8D, 0x8D, 0x20, 0x81, 0x00, 0x00,
    0x49, 0x8B, 0xD7, 0x4C, 0x8D, 0x44, 0x24, 0x58, 0x48, 0x8B, 0xCE, 0xE8, 0xC4, 0x43, 0x00, 0x00,
};
// 52F819: cmp byte [rsi+106h],3 / jne / mov r9,rdi / lea r8,[rsp+48h] /
// mov rdx,r15 / mov rcx,rsi / call 533AE0 (real pass).
constexpr std::uint8_t SaveRealWindow[]{
    0x80, 0xBE, 0x06, 0x01, 0x00, 0x00, 0x03, 0x75, 0x17, 0x4C, 0x8B, 0xCF, 0x4C, 0x8D,
    0x44, 0x24, 0x48, 0x49, 0x8B, 0xD7, 0x48, 0x8B, 0xCE, 0xE8, 0xAB, 0x42, 0x00, 0x00,
};
// 52EFB6: mov r9,r12 / mov [rsp+20h],r13d / lea r8,[rbp-69h] / mov rdx,r14 /
// mov rcx,r15 / call 531510.
constexpr std::uint8_t LoadWindow[]{
    0x4D, 0x8B, 0xCC, 0x44, 0x89, 0x6C, 0x24, 0x20, 0x4C, 0x8D, 0x45, 0x97,
    0x49, 0x8B, 0xD6, 0x49, 0x8B, 0xCF, 0xE8, 0x43, 0x25, 0x00, 0x00,
};
// 4856EB: mov rdx,r12 / mov rcx,r15 / call 53BC60, the last call of 485350.
constexpr std::uint8_t RestoreWindow[]{ 0x49, 0x8B, 0xD4, 0x49, 0x8B, 0xCF, 0xE8, 0x6A, 0x65, 0x0B, 0x00 };

constexpr std::uint8_t CoreReadStub[]{ 0x48, 0x83, 0xEC, 0x28, 0xE8 };
constexpr std::uint8_t CoreReadStubTail[]{ 0x89, 0xC0, 0x48, 0x83, 0xC4, 0x28, 0xC3 };
constexpr std::size_t  CoreRecordSearchAt = 0x3D;
constexpr std::uint8_t CoreRecordSearch[]{
    0x48, 0xC1, 0xE3, 0x20, 0x89, 0xE9, 0x48, 0x09, 0xD9, 0x4C, 0x8B, 0x47, 0x38, 0x31, 0xF6, 0x4D, 0x85,
    0xC0, 0x74, 0x36, 0x4C, 0x8B, 0x4F, 0x30, 0x31, 0xD2, 0x4D, 0x89, 0xC3, 0x4D, 0x89, 0xC2, 0xEB, 0x09,
    0x90, 0x4D, 0x89, 0xD3, 0x4D, 0x85, 0xD2, 0x74, 0x21, 0x49, 0xD1, 0xEA, 0x49, 0x8D, 0x1C, 0x12, 0x48,
    0xC1, 0xE3, 0x04, 0x49, 0x39, 0x0C, 0x19, 0x73, 0xE7, 0x4C, 0x01, 0xD2, 0x48, 0xFF, 0xC2, 0x49, 0xF7,
    0xD2, 0x4D, 0x01, 0xDA, 0xEB, 0xD9, 0x31, 0xD2, 0x4C, 0x39, 0xC2, 0x74, 0x41, 0x4C, 0x8B, 0x47, 0x30,
    0x48, 0xC1, 0xE2, 0x04, 0x49, 0x39, 0x0C, 0x10, 0x75, 0x33, 0x49, 0x01, 0xD0, 0x41, 0x8B, 0x70, 0x08,
};

struct Witness {
    std::uint64_t       rva;
    const std::uint8_t* bytes;
    std::uint32_t       size;
    const char*         name;
};

template <std::size_t N>
constexpr auto W(std::uint64_t rva, const std::uint8_t (&bytes)[N], const char* name) noexcept -> Witness {
    return { rva, bytes, static_cast<std::uint32_t>(N), name };
}

// A call inside a witness window and the function it must reach.
struct CallCheck {
    std::uint64_t site;
    std::uint64_t target;
    const char*   name;
};

const std::array<Witness, 13> HelperWitnesses{
    W(0x5219AD, SkillRecordCall, "Consume, skills.txt record call"),
    W(0x5219D7, StatesCountCall, "Consume, states count call"),
    W(0x521A64, GetStatListCall, "Consume, state list lookup"),
    W(0x521A7B, ReadListStatCall, "Consume, list stat read"),
    W(0x521BAA, ApplyStateCall, "Consume, state creation"),
    W(0x521D57, SetListStatCall, "Consume, list stat write"),
    W(0x521DA0, UnitSpeedCall, "Consume, speed refresh"),
    W(0x43AC59, EventAndEndFrameCalls, "state timer helper 43AC00"),
    W(0x440AB8, GetUnitStatCall, "unit stat read at 440AC1"),
    W(0x52EFFD, UnitRoomCall, "player load, unit room call"),
    W(0x977A2, DataTablesCall, "skill record lookup, data tables call"),
    W(0x42E615, TickWindow, "player regen event reschedule"),
    W(0x4856EB, RestoreWindow, "world entry, pet warp call"),
};

const std::array<CallCheck, 14> HelperCalls{ {
    { 0x5219B7, SkillRecordRva, "skills.txt record" },
    { 0x5219DE, StatesCountRva, "states count" },
    { 0x521A6E, GetStatListRva, "state list lookup" },
    { 0x521A8C, ReadListStatRva, "list stat read" },
    { 0x521BAA, ApplyStateRva, "state creation" },
    { 0x521D57, SetListStatRva, "list stat write" },
    { 0x521DB0, UnitSpeedRva, "speed refresh" },
    { 0x43AC59, SetEventRva, "EVENT_SetEvent" },
    { 0x43AC63, SetExpireFrameRva, "state end frame setter" },
    { 0x440AC1, GetUnitStatRva, "unit stat read" },
    { 0x52F000, UnitRoomRva, "unit room" },
    { 0x977A2, DataTablesRva, "data tables" },
    { 0x42E634, SetEventRva, "EVENT_SetEvent (regen reschedule)" },
    { 0x4856F1, WarpPetsRva, "pet warp" },
} };

// ---------------------------------------------------------------------------
// Game functions
// ---------------------------------------------------------------------------

struct ApplyArgs {
    void*        source;          // +00
    void*        target;          // +08
    std::int32_t skill;           // +10
    std::int32_t level;           // +14
    std::int32_t duration;        // +18 frames
    std::int32_t statId;          // +1C one extra stat, -1 for none
    std::int32_t statValue;       // +20
    std::int32_t state;           // +24
    void*        removeCallback;  // +28 nullptr: the default
};
static_assert(sizeof(ApplyArgs) == 0x30, "433D20 takes a 30h-byte argument record");

using ConsumeFn        = std::uint64_t (*)(void* game, void* unit, std::uint32_t skill, std::uint32_t level);
using SetEventFn       = std::uint64_t (*)(void* game, void* unit, std::int32_t type, std::int32_t frame, std::int32_t customId,
                                    std::int32_t customParam, std::int32_t arg7);
using WriteBlockFn     = std::uint32_t (*)(void* game, void* player, std::uint8_t** cursor, std::uint8_t* end);
using ReadBlockFn      = std::uint32_t (*)(void* game, void* player, std::uint8_t** cursor, std::uint8_t* end, std::uint32_t version);
using WarpPetsFn       = std::uint64_t (*)(void* game, void* player);
using GetStatListFn    = void* (*)(void* unit, std::int32_t state);
using ReadListStatFn   = std::int32_t (*)(std::uint8_t context, void* list, std::int32_t stat, std::int32_t layer);
using SetListStatFn    = void (*)(std::uint8_t context, void* list, std::int32_t stat, std::int32_t value, std::uint16_t layer);
using SetExpireFrameFn = void (*)(void* list, std::int32_t frame);
using GetUnitStatFn    = std::int32_t (*)(void* unit, std::int32_t stat, std::int32_t layer);
using ApplyStateFn     = void* (*)(ApplyArgs* args);
using UnitSpeedFn      = void (*)(void* unit, const char* file, std::int32_t line);
using StatesCountFn    = std::int32_t (*)(std::uint8_t context);
using SkillRecordFn    = const std::uint8_t* (*)(std::uint8_t context, std::int32_t skill);
using DataTablesFn     = const std::uint8_t* (*)(std::uint8_t context);
using UnitRoomFn       = void* (*)(void* unit);

const D2RL::PluginContext* Context{};
std::uintptr_t             Base{};
constexpr const char*      SourceName = "forever-states";

ConsumeFn        OriginalConsume{};
SetEventFn       SetEvent{};
WriteBlockFn     WriteBlock{};
ReadBlockFn      ReadBlock{};
WarpPetsFn       WarpPets{};
GetStatListFn    GetStatList{};
ReadListStatFn   ReadListStat{};
SetListStatFn    SetListStat{};
SetExpireFrameFn SetExpireFrame{};
GetUnitStatFn    GetUnitStat{};
ApplyStateFn     ApplyState{};
UnitSpeedFn      UnitSpeed{};
StatesCountFn    StatesCount{};
SkillRecordFn    SkillRecord{};
DataTablesFn     DataTables{};
UnitRoomFn       UnitRoom{};

template <typename T>
auto Read(const void* base, std::size_t offset) noexcept -> T {
    T value{};
    std::memcpy(&value, static_cast<const std::uint8_t*>(base) + offset, sizeof(T));
    return value;
}

template <typename Fn>
auto At(std::uint64_t rva) noexcept -> Fn {
    return reinterpret_cast<Fn>(Base + static_cast<std::uintptr_t>(rva));
}

// ---------------------------------------------------------------------------
// Rules, kept states, pending restores
// ---------------------------------------------------------------------------

struct Target {
    std::int32_t                                        skill = -1;
    std::vector<std::int32_t>                           stats;
    std::array<std::atomic<std::int32_t>, ContextSlots> stateByContext{};
};

Config             Settings{};
std::deque<Target> Targets;    // built once at load, read-only afterwards
std::vector<Rule>  IdleRules;  // rules with no stat set yet

// A state the plugin keeps, with the last copy of its stats (taken every
// HoldFrames) in case the game saves after the state is already gone.
// probation: made at load, before items count. It is kept without the stat
// until the player has been in the world for RestoreWaitFrames; the first
// frame the stat counts ends the probation, and if it never does the state
// ends then. speedRefresh: the unit speed refresh Consume calls is run once
// the player is in a room (at load the unit is not placed yet).
struct Held {
    void*         game              = nullptr;
    std::uint32_t guid              = 0;
    std::int32_t  state             = -1;
    std::int32_t  snapshotFrame     = -1;
    bool          probation         = false;
    std::int32_t  probationDeadline = 0;  // game frame, 0 until the player is in a room
    bool          speedRefresh      = false;
    SavedState    snapshot;
};

struct HeldInfo {
    bool         held              = false;
    std::int32_t snapshotFrame     = -1;
    bool         probation         = false;
    std::int32_t probationDeadline = 0;
    bool         speedRefresh      = false;
};

struct Pending {
    void*                   game     = nullptr;
    std::uint32_t           guid     = 0;
    std::int32_t            deadline = 0;  // game frame the wait for the stat ends, 0 before the first try
    bool                    waiting  = false;
    std::vector<SavedState> states;
};

SRWLOCK                  TableLock = SRWLOCK_INIT;
std::vector<Held>        HeldTable;
std::vector<Pending>     PendingTable;
std::atomic<std::size_t> PendingCount{ 0 };

std::atomic<bool> HoldActive{ false };
std::atomic<bool> BindActive{ false };
std::atomic<bool> PersistActive{ false };

std::atomic<std::uint64_t> HoldsStarted{ 0 };
std::atomic<std::uint64_t> StatesReleased{ 0 };
std::atomic<std::uint64_t> PeriodicSkips{ 0 };
std::atomic<std::uint64_t> StatesSaved{ 0 };
std::atomic<std::uint64_t> SnapshotsSaved{ 0 };
std::atomic<std::uint64_t> StatesLoaded{ 0 };
std::atomic<std::uint64_t> StatesRestored{ 0 };
std::atomic<std::uint64_t> RestoresRefused{ 0 };

SRWLOCK     SummaryLock = SRWLOCK_INIT;
std::string LastSave    = "none yet";
std::string LastLoad    = "none yet";
std::string LastRestore = "none yet";
std::string HoldStatus  = "not installed";
std::string BindStatus  = "not installed";
std::string SaveStatus  = "not installed";

// log: write it to the plugin log when it differs from the previous summary.
void SetSummary(std::string& slot, const std::string& text, bool log) {
    AcquireSRWLockExclusive(&SummaryLock);
    const bool changed = slot != text;
    slot = text;
    ReleaseSRWLockExclusive(&SummaryLock);
    if (log && changed && Context != nullptr) Context->LogInfo(text.c_str());
}

auto GetSummary(const std::string& slot) -> std::string {
    AcquireSRWLockShared(&SummaryLock);
    std::string copy = slot;
    ReleaseSRWLockShared(&SummaryLock);
    return copy;
}

void BuildTargets() {
    Targets.clear();
    IdleRules.clear();
    for (const Rule& rule : Settings.rules) {
        if (rule.stat < 0) {
            IdleRules.push_back(rule);
            continue;
        }
        Target* found = nullptr;
        for (Target& target : Targets) {
            if (target.skill == rule.skill) found = &target;
        }
        if (found == nullptr) {
            Targets.emplace_back();
            found        = &Targets.back();
            found->skill = rule.skill;
            for (auto& slot : found->stateByContext) slot.store(Unresolved, std::memory_order_relaxed);
        }
        bool duplicate = false;
        for (const std::int32_t stat : found->stats) duplicate = duplicate || stat == rule.stat;
        if (!duplicate) found->stats.push_back(rule.stat);
    }
}

void ResetResolvedStates() noexcept {
    for (Target& target : Targets) {
        for (auto& slot : target.stateByContext) slot.store(Unresolved, std::memory_order_relaxed);
    }
}

// The skill's aurastate for this data context, -1 when the skill or its state
// is not in the tables. The skill id is bounded the way 97790 bounds it.
auto ResolveState(Target& target, std::uint8_t context) noexcept -> std::int32_t {
    std::atomic<std::int32_t>* slot = context < ContextSlots ? &target.stateByContext[context] : nullptr;
    if (slot != nullptr) {
        const std::int32_t known = slot->load(std::memory_order_relaxed);
        if (known != Unresolved) return known;
    }
    std::int32_t        state  = -1;
    const std::uint8_t* tables = DataTables(context);
    if (tables != nullptr && target.skill >= 0
            && static_cast<std::uint64_t>(target.skill) < Read<std::uint64_t>(tables, SkillCountOffset)) {
        const std::uint8_t* record = SkillRecord(context, target.skill);
        if (record != nullptr) {
            const auto aura = Read<std::int16_t>(record, SkillAuraStateAt);
            if (aura >= 0 && aura < StatesCount(context)) state = aura;
        }
    }
    if (slot != nullptr) slot->store(state, std::memory_order_relaxed);
    return state;
}

auto FindTarget(std::int32_t skill) noexcept -> Target* {
    for (Target& target : Targets) {
        if (target.skill == skill) return &target;
    }
    return nullptr;
}

auto HasAnyStat(void* unit, const Target& target) noexcept -> bool {
    for (const std::int32_t stat : target.stats) {
        if (GetUnitStat(unit, stat, 0) > 0) return true;
    }
    return false;
}

// The unit's list of this state when it came from this skill.
auto OwnList(void* unit, std::int32_t state, std::int32_t skill) noexcept -> void* {
    void* list = GetStatList(unit, state);
    if (list == nullptr || Read<std::int32_t>(list, ListSkillOffset) != skill) return nullptr;
    return list;
}

// Copies a state's stats straight from its list. The first records are read
// back through the game's own reader and must match, so a different list
// layout is refused instead of saved.
auto ReadState(std::uint8_t context, void* list, std::int32_t state, std::int32_t skill, SavedState& out) -> bool {
    const auto* records = Read<const std::uint8_t*>(list, ListRecordsOffset);
    const auto  count   = Read<std::uint64_t>(list, ListCountOffset);
    if ((records == nullptr && count != 0) || count > MaxStatsPerState || state < 0 || state > 0xFFFF || skill < 0
            || skill > 0xFFFF) {
        return false;
    }
    SavedState saved;
    saved.state = static_cast<std::uint16_t>(state);
    saved.skill = static_cast<std::uint16_t>(skill);
    saved.level = Read<std::int32_t>(list, ListLevelOffset);
    for (std::uint64_t i = 0; i < count; ++i) {
        const std::uint8_t* record = records + i * RecordSize;
        const auto          key    = Read<std::uint64_t>(record, 0);
        const auto          value  = Read<std::int32_t>(record, 8);
        const auto          stat   = static_cast<std::uint32_t>(key >> 32);
        const auto          layer  = static_cast<std::uint32_t>(key & 0xFFFFFFFFu);
        if (stat > static_cast<std::uint32_t>(MaxStatId) || layer > 0xFFFFu) return false;
        if (i < 3 && ReadListStat(context, list, static_cast<std::int32_t>(stat), static_cast<std::int32_t>(layer)) != value) {
            return false;
        }
        saved.stats.push_back({ static_cast<std::uint16_t>(stat), static_cast<std::uint16_t>(layer), value });
    }
    out = std::move(saved);
    return true;
}

auto FindHeldIndex(void* game, std::uint32_t guid, std::int32_t state) noexcept -> std::size_t {
    for (std::size_t i = 0; i < HeldTable.size(); ++i) {
        const Held& entry = HeldTable[i];
        if (entry.game == game && entry.guid == guid && entry.state == state) return i;
    }
    return HeldTable.size();
}

// -1 when not kept, otherwise the frame of the last snapshot (-1 is also "kept,
// no snapshot yet", so the caller asks IsHeld first).
auto IsHeld(void* game, std::uint32_t guid, std::int32_t state, std::int32_t* snapshotFrame = nullptr) noexcept -> bool {
    AcquireSRWLockShared(&TableLock);
    const std::size_t index = FindHeldIndex(game, guid, state);
    const bool        held  = index < HeldTable.size();
    if (held && snapshotFrame != nullptr) *snapshotFrame = HeldTable[index].snapshotFrame;
    ReleaseSRWLockShared(&TableLock);
    return held;
}

auto GetHeld(void* game, std::uint32_t guid, std::int32_t state) noexcept -> HeldInfo {
    HeldInfo info;
    AcquireSRWLockShared(&TableLock);
    const std::size_t index = FindHeldIndex(game, guid, state);
    if (index < HeldTable.size()) {
        const Held& entry      = HeldTable[index];
        info.held              = true;
        info.snapshotFrame     = entry.snapshotFrame;
        info.probation         = entry.probation;
        info.probationDeadline = entry.probationDeadline;
        info.speedRefresh      = entry.speedRefresh;
    }
    ReleaseSRWLockShared(&TableLock);
    return info;
}

// Sets the probation fields of a kept state; deadline < 0 leaves it alone.
void SetProbation(void* game, std::uint32_t guid, std::int32_t state, bool probation, std::int32_t deadline, bool speedRefresh) {
    AcquireSRWLockExclusive(&TableLock);
    const std::size_t index = FindHeldIndex(game, guid, state);
    if (index < HeldTable.size()) {
        HeldTable[index].probation    = probation;
        HeldTable[index].speedRefresh = speedRefresh;
        if (deadline >= 0) HeldTable[index].probationDeadline = deadline;
    }
    ReleaseSRWLockExclusive(&TableLock);
}

void Hold(void* game, std::uint32_t guid, std::int32_t state) {
    AcquireSRWLockExclusive(&TableLock);
    if (FindHeldIndex(game, guid, state) == HeldTable.size()) {
        for (std::size_t i = 0; i < HeldTable.size(); ++i) {
            if (HeldTable[i].guid == guid && HeldTable[i].state == state) {
                HeldTable.erase(HeldTable.begin() + static_cast<std::ptrdiff_t>(i));
                break;
            }
        }
        if (HeldTable.size() >= MaxHeld) HeldTable.erase(HeldTable.begin());
        Held entry;
        entry.game  = game;
        entry.guid  = guid;
        entry.state = state;
        HeldTable.push_back(std::move(entry));
    }
    ReleaseSRWLockExclusive(&TableLock);
}

void Unhold(void* game, std::uint32_t guid, std::int32_t state) {
    AcquireSRWLockExclusive(&TableLock);
    const std::size_t index = FindHeldIndex(game, guid, state);
    if (index < HeldTable.size()) HeldTable.erase(HeldTable.begin() + static_cast<std::ptrdiff_t>(index));
    ReleaseSRWLockExclusive(&TableLock);
}

void StoreSnapshot(void* game, std::uint32_t guid, std::int32_t state, std::int32_t frame, SavedState&& snapshot) {
    AcquireSRWLockExclusive(&TableLock);
    const std::size_t index = FindHeldIndex(game, guid, state);
    if (index < HeldTable.size()) {
        HeldTable[index].snapshotFrame = frame;
        HeldTable[index].snapshot      = std::move(snapshot);
    }
    ReleaseSRWLockExclusive(&TableLock);
}

auto CopySnapshot(void* game, std::uint32_t guid, std::int32_t state, SavedState& out) -> bool {
    AcquireSRWLockShared(&TableLock);
    const std::size_t index = FindHeldIndex(game, guid, state);
    const bool        found = index < HeldTable.size() && HeldTable[index].snapshotFrame >= 0;
    if (found) out = HeldTable[index].snapshot;
    ReleaseSRWLockShared(&TableLock);
    return found;
}

void ClearTables() {
    AcquireSRWLockExclusive(&TableLock);
    HeldTable.clear();
    PendingTable.clear();
    PendingCount.store(0);
    ReleaseSRWLockExclusive(&TableLock);
}

void StorePending(Pending&& pending) {
    AcquireSRWLockExclusive(&TableLock);
    for (std::size_t i = 0; i < PendingTable.size(); ++i) {
        if (PendingTable[i].guid == pending.guid) {
            PendingTable.erase(PendingTable.begin() + static_cast<std::ptrdiff_t>(i));
            break;
        }
    }
    if (PendingTable.size() >= MaxPending) PendingTable.erase(PendingTable.begin());
    PendingTable.push_back(std::move(pending));
    PendingCount.store(PendingTable.size());
    ReleaseSRWLockExclusive(&TableLock);
}

// The player's pending restore: same game first, then the same unit id alone.
auto TakePending(void* game, std::uint32_t guid, Pending& out) -> bool {
    AcquireSRWLockExclusive(&TableLock);
    std::size_t index = PendingTable.size();
    for (std::size_t i = 0; i < PendingTable.size(); ++i) {
        if (PendingTable[i].guid == guid && PendingTable[i].game == game) index = i;
    }
    if (index == PendingTable.size()) {
        for (std::size_t i = 0; i < PendingTable.size(); ++i) {
            if (PendingTable[i].guid == guid) index = i;
        }
    }
    const bool found = index < PendingTable.size();
    if (found) {
        out = std::move(PendingTable[index]);
        PendingTable.erase(PendingTable.begin() + static_cast<std::ptrdiff_t>(index));
        PendingCount.store(PendingTable.size());
    }
    ReleaseSRWLockExclusive(&TableLock);
    return found;
}

// ---------------------------------------------------------------------------
// Keep and release (every frame, per player)
// ---------------------------------------------------------------------------

void KeepAhead(void* list, std::int32_t frame) noexcept {
    if ((Read<std::uint32_t>(list, ListFlagsOffset) & ListTimedFlag) == 0) return;
    const float end = Read<float>(list, ListExpireOffset);
    if (end < static_cast<float>(frame + HoldFrames)) SetExpireFrame(list, frame + 2 * HoldFrames);
}

void EndNow(void* game, void* unit, void* list, std::int32_t frame) noexcept {
    SetExpireFrame(list, frame > 0 ? frame : 1);
    SetEvent(game, unit, ExpiryEventType, frame + 1, 0, 0, 0);
}

void TryRestore(void* game, void* player, const char* moment);

void Tick(void* game, void* unit) noexcept {
    if (!HoldActive.load(std::memory_order_relaxed) || game == nullptr || unit == nullptr
            || Read<std::uint32_t>(unit, 0) != PlayerUnitType) {
        return;
    }
    if (PendingCount.load(std::memory_order_relaxed) != 0 && PersistActive.load(std::memory_order_relaxed)) {
        TryRestore(game, unit, "in the world");
    }
    const std::uint8_t  context = Read<std::uint8_t>(game, GameContextOffset);
    const std::int32_t  frame   = Read<std::int32_t>(game, GameFrameOffset);
    const std::uint32_t guid    = Read<std::uint32_t>(unit, UnitGuidOffset);
    bool roomKnown = false;
    bool inRoom    = false;
    const auto placed = [&]() noexcept {
        if (!roomKnown) {
            inRoom    = UnitRoom(unit) != nullptr;
            roomKnown = true;
        }
        return inRoom;
    };
    for (Target& target : Targets) {
        const std::int32_t state = ResolveState(target, context);
        if (state < 0) continue;
        void*          list = OwnList(unit, state, target.skill);
        const HeldInfo held = GetHeld(game, guid, state);
        if (list == nullptr) {
            if (held.held) Unhold(game, guid, state);
            continue;
        }
        if (held.held && held.speedRefresh && placed()) {
            UnitSpeed(unit, SourceName, __LINE__);
            SetProbation(game, guid, state, held.probation, -1, false);
        }
        if (HasAnyStat(unit, target)) {
            if (!held.held) {
                Hold(game, guid, state);
                HoldsStarted.fetch_add(1, std::memory_order_relaxed);
                D2RL::LogInfoF(Context, "ForeverStates: player %u, state %d (skill %d) is now kept.", guid, state, target.skill);
            } else if (held.probation) {
                SetProbation(game, guid, state, false, 0, false);
                D2RL::LogInfoF(Context, "ForeverStates: player %u, the rule's stat counts now; state %d (skill %d) stays.", guid,
                    state, target.skill);
            }
            KeepAhead(list, frame);
            if (!held.held || held.snapshotFrame < 0 || frame - held.snapshotFrame >= HoldFrames || frame < held.snapshotFrame) {
                SavedState snapshot;
                if (ReadState(context, list, state, target.skill, snapshot)) {
                    StoreSnapshot(game, guid, state, frame, std::move(snapshot));
                }
            }
        } else if (held.held && held.probation) {
            KeepAhead(list, frame);
            if (!placed()) continue;
            if (held.probationDeadline == 0) {
                SetProbation(game, guid, state, true, frame + RestoreWaitFrames, false);
            } else if (frame >= held.probationDeadline) {
                Unhold(game, guid, state);
                EndNow(game, unit, list, frame);
                StatesReleased.fetch_add(1, std::memory_order_relaxed);
                SetSummary(LastRestore, "ForeverStates: player " + std::to_string(guid) + " had none of the rule's stats "
                    + std::to_string(RestoreWaitFrames / 25) + " s after entering the world (item not equipped); state "
                    + std::to_string(state) + " (skill " + std::to_string(target.skill) + ") ends now.", true);
            }
        } else if (held.held) {
            Unhold(game, guid, state);
            EndNow(game, unit, list, frame);
            StatesReleased.fetch_add(1, std::memory_order_relaxed);
            D2RL::LogInfoF(Context, "ForeverStates: player %u lost the stat, state %d (skill %d) ends now.", guid, state,
                target.skill);
        }
    }
}

// ---------------------------------------------------------------------------
// Save, load, restore
// ---------------------------------------------------------------------------

// The states to save: a kept state, or one whose stat the player has now.
// When the game saves after a kept state is already gone, the last copy the
// keep check took (at most HoldFrames old) is saved instead.
auto CollectStates(void* game, void* player, std::string& notes, std::size_t& fromSnapshot) -> std::vector<SavedState> {
    std::vector<SavedState> states;
    fromSnapshot                = 0;
    const std::uint8_t  context = Read<std::uint8_t>(game, GameContextOffset);
    const std::uint32_t guid    = Read<std::uint32_t>(player, UnitGuidOffset);
    for (Target& target : Targets) {
        const std::int32_t state = ResolveState(target, context);
        if (state < 0) continue;
        void*      list = OwnList(player, state, target.skill);
        const bool held = IsHeld(game, guid, state);
        if (list != nullptr) {
            if (!held && !HasAnyStat(player, target)) continue;
            SavedState saved;
            if (!ReadState(context, list, state, target.skill, saved)) {
                notes += " State " + std::to_string(state) + ": its stat records do not read back as expected, not saved.";
                continue;
            }
            states.push_back(std::move(saved));
        } else if (held) {
            SavedState saved;
            if (CopySnapshot(game, guid, state, saved)) {
                states.push_back(std::move(saved));
                ++fromSnapshot;
            }
        }
    }
    return states;
}

auto DescribeStates(const std::vector<SavedState>& states) -> std::string {
    std::string text;
    for (const SavedState& state : states) {
        if (!text.empty()) text += ", ";
        text += "state " + std::to_string(state.state) + " (skill " + std::to_string(state.skill) + " level "
            + std::to_string(state.level) + ", " + std::to_string(state.stats.size()) + " stats)";
    }
    return text;
}

enum class Outcome { Restored, Wait, Refused };

auto RestoreOne(void* game, void* player, std::uint8_t context, std::uint32_t guid, const SavedState& saved, std::string& note)
        -> Outcome {
    Target* target = FindTarget(saved.skill);
    if (target == nullptr) {
        note = "no rule names skill " + std::to_string(saved.skill) + " any more";
        return Outcome::Refused;
    }
    const std::int32_t state = ResolveState(*target, context);
    if (state != saved.state) {
        note = "skill " + std::to_string(saved.skill) + " no longer grants state " + std::to_string(saved.state);
        return Outcome::Refused;
    }
    if (GetStatList(player, state) != nullptr) {
        note = "the player already has state " + std::to_string(state);
        return Outcome::Refused;
    }
    if (!HasAnyStat(player, *target)) {
        note = "the player does not have the rule's stat yet";
        return Outcome::Wait;
    }
    ApplyArgs args{};
    args.source         = player;
    args.target         = player;
    args.skill          = saved.skill;
    args.level          = saved.level;
    args.duration       = RestoreFrames;
    args.statId         = -1;
    args.statValue      = 0;
    args.state          = state;
    args.removeCallback = nullptr;
    void* list = ApplyState(&args);
    if (list == nullptr) {
        note = "the game refused to create state " + std::to_string(state);
        return Outcome::Refused;
    }
    for (const SavedStat& stat : saved.stats) SetListStat(context, list, stat.stat, stat.value, stat.layer);
    UnitSpeed(player, SourceName, __LINE__);
    Hold(game, guid, state);
    SavedState copy = saved;
    StoreSnapshot(game, guid, state, Read<std::int32_t>(game, GameFrameOffset), std::move(copy));
    note = "state " + std::to_string(state) + " (skill " + std::to_string(saved.skill) + ") restored with "
        + std::to_string(saved.stats.size()) + " stats";
    return Outcome::Restored;
}

// At load, before the bound demon area is read: rebuilds the state so the
// stats it gives (a doubled pet cap, for Consume) are there when the demons
// come back and every time the game works the pet caps out afterwards. Item
// stats do not count yet, so the rule's stat is not checked here; the state
// is kept on probation and the per-frame check decides once the player is in
// the world.
auto RestoreAtLoad(void* game, void* player, std::uint8_t context, std::uint32_t guid, const SavedState& saved, std::string& note)
        -> Outcome {
    Target* target = FindTarget(saved.skill);
    if (target == nullptr) {
        note = "no rule names skill " + std::to_string(saved.skill) + " any more";
        return Outcome::Refused;
    }
    const std::int32_t state = ResolveState(*target, context);
    if (state != saved.state) {
        note = "skill " + std::to_string(saved.skill) + " no longer grants state " + std::to_string(saved.state);
        return Outcome::Refused;
    }
    if (GetStatList(player, state) != nullptr) {
        note = "the player already has state " + std::to_string(state);
        return Outcome::Refused;
    }
    ApplyArgs args{};
    args.source         = player;
    args.target         = player;
    args.skill          = saved.skill;
    args.level          = saved.level;
    args.duration       = LoadKeepFrames;
    args.statId         = -1;
    args.statValue      = 0;
    args.state          = state;
    args.removeCallback = nullptr;
    void* list = ApplyState(&args);
    if (list == nullptr) {
        note = "the game refused to create state " + std::to_string(state) + " at load";
        return Outcome::Wait;
    }
    for (const SavedStat& stat : saved.stats) SetListStat(context, list, stat.stat, stat.value, stat.layer);
    Hold(game, guid, state);
    SetProbation(game, guid, state, true, 0, true);
    SavedState copy = saved;
    StoreSnapshot(game, guid, state, Read<std::int32_t>(game, GameFrameOffset), std::move(copy));
    note = "state " + std::to_string(state) + " (skill " + std::to_string(saved.skill) + " level " + std::to_string(saved.level)
        + ", " + std::to_string(saved.stats.size()) + " stats) rebuilt before the bound demons";
    return Outcome::Restored;
}

// Restores what the player's save kept, once the player is in the world. A
// state whose stat does not count yet (items count a little after entering)
// is tried again every frame for up to RestoreWaitFrames.
void TryRestore(void* game, void* player, const char* moment) {
    if (UnitRoom(player) == nullptr) return;
    const std::uint32_t guid = Read<std::uint32_t>(player, UnitGuidOffset);
    Pending             pending{};
    if (!TakePending(game, guid, pending)) return;
    const std::uint8_t context = Read<std::uint8_t>(game, GameContextOffset);
    const std::int32_t frame   = Read<std::int32_t>(game, GameFrameOffset);
    if (pending.deadline == 0) pending.deadline = frame + RestoreWaitFrames;
    std::string             summary;
    std::vector<SavedState> waiting;
    std::string             waitNote;
    for (const SavedState& saved : pending.states) {
        std::string   note;
        const Outcome outcome = RestoreOne(game, player, context, guid, saved, note);
        if (outcome == Outcome::Restored) {
            StatesRestored.fetch_add(1, std::memory_order_relaxed);
            summary += " " + note + ".";
        } else if (outcome == Outcome::Refused) {
            RestoresRefused.fetch_add(1, std::memory_order_relaxed);
            summary += " State " + std::to_string(saved.state) + " not restored: " + note + ".";
        } else {
            waiting.push_back(saved);
            waitNote = note;
        }
    }
    const bool timedOut = !waiting.empty() && frame >= pending.deadline;
    if (timedOut) {
        RestoresRefused.fetch_add(waiting.size(), std::memory_order_relaxed);
        summary += " " + std::to_string(waiting.size()) + " state(s) dropped: the player still had none of the rule's stats "
            + std::to_string(RestoreWaitFrames / 25) + " s after entering (item not equipped).";
    } else if (!waiting.empty() && !pending.waiting) {
        summary += " " + std::to_string(waiting.size()) + " state(s) wait for the stat (" + waitNote + "), up to "
            + std::to_string(RestoreWaitFrames / 25) + " s.";
    }
    if (!summary.empty()) {
        SetSummary(LastRestore, std::string("ForeverStates: ") + moment + ", player " + std::to_string(guid) + ":" + summary, true);
    }
    if (!waiting.empty() && !timedOut) {
        pending.states  = std::move(waiting);
        pending.waiting = true;
        StorePending(std::move(pending));
    }
}

// ---------------------------------------------------------------------------
// Hook and relays
// ---------------------------------------------------------------------------

// 521970, srvdofunc 159. In periodic mode, a player with the rule's stat
// keeps the state: return what that branch returns when it removes nothing.
auto HookedConsume(void* game, void* unit, std::uint32_t skill, std::uint32_t level) -> std::uint64_t {
    if (BindActive.load(std::memory_order_relaxed) && game != nullptr && unit != nullptr
            && Read<std::uint32_t>(unit, 0) == PlayerUnitType
            && (Read<std::uint32_t>(unit, UnitFlagExOffset) & PeriodicRunFlag) != 0 && skill <= 0xFFFFu) {
        const Target* target = FindTarget(static_cast<std::int32_t>(skill));
        if (target != nullptr && HasAnyStat(unit, *target)) {
            PeriodicSkips.fetch_add(1, std::memory_order_relaxed);
            return 1;
        }
    }
    return OriginalConsume(game, unit, skill, level);
}

// 42E634: the player's regen event sets itself again for the next frame.
auto RelayTick(void* game, void* unit, std::int32_t type, std::int32_t frame, std::int32_t customId, std::int32_t customParam,
        std::int32_t arg7) -> std::uint64_t {
    const std::uint64_t result = SetEvent(game, unit, type, frame, customId, customParam, arg7);
    Tick(game, unit);
    return result;
}

// 52F717 and 52F830: the block goes after whatever 533AE0 wrote.
auto RelayWrite(void* game, void* player, std::uint8_t** cursor, std::uint8_t* end) -> std::uint32_t {
    const std::uint32_t status = WriteBlock(game, player, cursor, end);
    if (status != 0 || !PersistActive.load(std::memory_order_relaxed) || game == nullptr || player == nullptr
            || cursor == nullptr || *cursor == nullptr || end == nullptr || end < *cursor
            || Read<std::uint32_t>(player, 0) != PlayerUnitType) {
        return status;
    }
    const std::uint32_t           guid         = Read<std::uint32_t>(player, UnitGuidOffset);
    std::string                   notes;
    std::size_t                   fromSnapshot = 0;
    const std::vector<SavedState> states       = CollectStates(game, player, notes, fromSnapshot);
    if (states.empty()) {
        SetSummary(LastSave, "ForeverStates: save, player " + std::to_string(guid) + ", no kept state to write." + notes, true);
        return status;
    }
    std::size_t       written = 0;
    const std::size_t size    = EncodeBlock(*cursor, static_cast<std::size_t>(end - *cursor), states, written);
    if (size == 0) {
        SetSummary(LastSave, "ForeverStates: save, player " + std::to_string(guid) + ", no room left in the save for the kept states."
            + notes, true);
        return status;
    }
    *cursor += size;
    StatesSaved.fetch_add(written, std::memory_order_relaxed);
    SnapshotsSaved.fetch_add(fromSnapshot, std::memory_order_relaxed);
    std::vector<SavedState> kept(states.begin(), states.begin() + static_cast<std::ptrdiff_t>(written));
    std::string summary = "ForeverStates: save, player " + std::to_string(guid) + ", " + DescribeStates(kept) + ".";
    if (fromSnapshot != 0) summary += " " + std::to_string(fromSnapshot) + " of them from the last copy (the state was already gone).";
    if (written < states.size()) summary += " " + std::to_string(states.size() - written) + " state(s) did not fit.";
    SetSummary(LastSave, summary + notes, true);
    return status;
}

// 52EFC8: the kept states are rebuilt BEFORE 531510 reads the bound demon
// area, so Consume's doubled pet cap is there for the demons. The block sits
// after the demon blocks, so it is found by scanning from the area's start
// (its CRC keeps a demon block from being taken for it).
auto RelayRead(void* game, void* player, std::uint8_t** cursor, std::uint8_t* end, std::uint32_t version) -> std::uint32_t {
    if (!PersistActive.load(std::memory_order_relaxed) || game == nullptr || player == nullptr
            || Read<std::uint32_t>(player, 0) != PlayerUnitType || cursor == nullptr || *cursor == nullptr || end == nullptr
            || end <= *cursor) {
        return ReadBlock(game, player, cursor, end, version);
    }
    const std::uint32_t     guid    = Read<std::uint32_t>(player, UnitGuidOffset);
    const std::uint8_t      context = Read<std::uint8_t>(game, GameContextOffset);
    std::vector<SavedState> states;
    const std::uint8_t*     at   = nullptr;
    const std::size_t       size = FindBlock(*cursor, end, states, &at);
    std::string             summary;
    Pending                 later{};
    if (size == 0) {
        summary = " the save holds no kept state.";
    } else {
        StatesLoaded.fetch_add(states.size(), std::memory_order_relaxed);
        for (const SavedState& saved : states) {
            std::string   note;
            const Outcome outcome = RestoreAtLoad(game, player, context, guid, saved, note);
            if (outcome == Outcome::Restored) {
                StatesRestored.fetch_add(1, std::memory_order_relaxed);
                summary += " " + note + "; kept until the rule's stat counts (up to " + std::to_string(RestoreWaitFrames / 25)
                    + " s in the world).";
            } else if (outcome == Outcome::Refused) {
                RestoresRefused.fetch_add(1, std::memory_order_relaxed);
                summary += " State " + std::to_string(saved.state) + " not restored: " + note + ".";
            } else {
                later.states.push_back(saved);
                summary += " State " + std::to_string(saved.state) + ": " + note + "; tried again in the world.";
            }
        }
    }
    const std::uint32_t status = ReadBlock(game, player, cursor, end, version);
    if (size != 0 && status == 0 && cursor != nullptr && *cursor == at) *cursor += size;
    if (!later.states.empty()) {
        later.game = game;
        later.guid = guid;
        StorePending(std::move(later));
    }
    SetSummary(LastLoad, "ForeverStates: load, player " + std::to_string(guid) + ":" + summary, true);
    return status;
}

// 4856F1: the player is in the world.
auto RelayWarp(void* game, void* player) -> std::uint64_t {
    const std::uint64_t result = WarpPets(game, player);
    if (PersistActive.load(std::memory_order_relaxed) && PendingCount.load(std::memory_order_relaxed) != 0 && game != nullptr
            && player != nullptr && Read<std::uint32_t>(player, 0) == PlayerUnitType) {
        TryRestore(game, player, "entering the world");
    }
    return result;
}

// ---------------------------------------------------------------------------
// Install
// ---------------------------------------------------------------------------

struct CallSite {
    const char*                 name;
    std::uint64_t               callRva;
    std::uint64_t               targetRva;
    std::size_t                 stubOffset;
    void*                       relay;
    std::array<std::uint8_t, 5> stock{};
    std::array<std::uint8_t, 5> written{};
    bool                        applied{};
};

std::array<CallSite, 1> TickSites{ {
    { "player regen event reschedule 42E634", 0x42E634, SetEventRva, 0x00, reinterpret_cast<void*>(&RelayTick) },
} };
std::array<CallSite, 4> PersistSites{ {
    { "demon area writer, measuring pass 52F717", 0x52F717, WriteBlockRva, 0x40, reinterpret_cast<void*>(&RelayWrite) },
    { "demon area writer, real pass 52F830", 0x52F830, WriteBlockRva, 0x80, reinterpret_cast<void*>(&RelayWrite) },
    { "demon area reader 52EFC8", 0x52EFC8, ReadBlockRva, 0xC0, reinterpret_cast<void*>(&RelayRead) },
    { "pet warp at world entry 4856F1", 0x4856F1, WarpPetsRva, 0x100, reinterpret_cast<void*>(&RelayWarp) },
} };

const std::array<Witness, 3> PersistWitnesses{
    W(0x52F6FC, SaveMeasureWindow, "player save, measuring pass"),
    W(0x52F819, SaveRealWindow, "player save, real pass"),
    W(0x52EFB6, LoadWindow, "player load"),
};

constexpr std::size_t RelayPageBytes = 0x1000;
std::uint8_t*         RelayPage{};

auto CallTarget(std::uint64_t site) noexcept -> std::uint64_t {
    const auto* code = reinterpret_cast<const std::uint8_t*>(Base + static_cast<std::uintptr_t>(site));
    if (code[0] != 0xE8) return 0;
    const auto rel = static_cast<std::int32_t>(Read<std::uint32_t>(code, 1));
    return static_cast<std::uint64_t>(static_cast<std::int64_t>(site) + 5 + rel);
}

template <std::size_t N>
auto VerifyWitnesses(const std::array<Witness, N>& witnesses, const char* group) -> bool {
    bool ok = true;
    for (const Witness& witness : witnesses) {
        if (!Context->CheckExpectedBytes(witness.rva, witness.bytes, witness.size)) {
            D2RL::LogErrorF(Context, "ForeverStates (%s): the %s at %llX is not the D2RLoader 1.3.1 code (or another plugin "
                "patched it).", group, witness.name, static_cast<unsigned long long>(witness.rva));
            ok = false;
        }
    }
    return ok;
}

auto VerifyHelpers() -> bool {
    bool ok = VerifyWitnesses(HelperWitnesses, "helpers");
    for (const CallCheck& call : HelperCalls) {
        if (CallTarget(call.site) != call.target) {
            D2RL::LogErrorF(Context, "ForeverStates (helpers): the call at %llX does not reach the %s (%llX).",
                static_cast<unsigned long long>(call.site), call.name, static_cast<unsigned long long>(call.target));
            ok = false;
        }
    }
    if (!ok) return false;
    SetEvent       = At<SetEventFn>(SetEventRva);
    WriteBlock     = At<WriteBlockFn>(WriteBlockRva);
    ReadBlock      = At<ReadBlockFn>(ReadBlockRva);
    WarpPets       = At<WarpPetsFn>(WarpPetsRva);
    GetStatList    = At<GetStatListFn>(GetStatListRva);
    ReadListStat   = At<ReadListStatFn>(ReadListStatRva);
    SetListStat    = At<SetListStatFn>(SetListStatRva);
    SetExpireFrame = At<SetExpireFrameFn>(SetExpireFrameRva);
    GetUnitStat    = At<GetUnitStatFn>(GetUnitStatRva);
    ApplyState     = At<ApplyStateFn>(ApplyStateRva);
    UnitSpeed      = At<UnitSpeedFn>(UnitSpeedRva);
    StatesCount    = At<StatesCountFn>(StatesCountRva);
    SkillRecord    = At<SkillRecordFn>(SkillRecordRva);
    DataTables     = At<DataTablesFn>(DataTablesRva);
    UnitRoom       = At<UnitRoomFn>(UnitRoomRva);
    return true;
}

void WriteAbsoluteJump(std::uint8_t* at, const void* destination) noexcept {
    static constexpr std::uint8_t JumpIndirect[6]{ 0xFF, 0x25, 0x00, 0x00, 0x00, 0x00 };  // jmp [rip+0]
    std::memcpy(at, JumpIndirect, sizeof(JumpIndirect));
    const auto address = static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(destination));
    std::memcpy(at + sizeof(JumpIndirect), &address, sizeof(address));
}

// A page above the image, within rel32 reach of every relayed call.
auto AllocateRelayPage() noexcept -> std::uint8_t* {
    const auto* image     = reinterpret_cast<const std::uint8_t*>(Base);
    const auto  ntOffset  = static_cast<std::size_t>(Read<std::uint32_t>(image, 0x3C));
    const auto  imageSize = Read<std::uint32_t>(image, ntOffset + 0x50);
    SYSTEM_INFO systemInfo{};
    GetSystemInfo(&systemInfo);
    const std::uintptr_t granularity = systemInfo.dwAllocationGranularity != 0 ? systemInfo.dwAllocationGranularity : 0x10000;
    const auto alignUp = [granularity](std::uintptr_t value) noexcept { return (value + granularity - 1) & ~(granularity - 1); };
    const std::uintptr_t limit   = Base + 0x42E634 + 0x7FF00000ULL;  // 42E634 is the lowest relayed call
    std::uintptr_t       address = alignUp(Base + imageSize);
    while (address + RelayPageBytes < limit) {
        MEMORY_BASIC_INFORMATION info{};
        if (VirtualQuery(reinterpret_cast<LPCVOID>(address), &info, sizeof(info)) == 0) break;
        const auto regionEnd = reinterpret_cast<std::uintptr_t>(info.BaseAddress) + info.RegionSize;
        if (info.State == MEM_FREE && address + RelayPageBytes <= regionEnd) {
            if (auto* page = VirtualAlloc(reinterpret_cast<LPVOID>(address), RelayPageBytes, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE)) {
                return static_cast<std::uint8_t*>(page);
            }
        }
        const auto next = alignUp(regionEnd);
        if (next <= address) break;
        address = next;
    }
    return nullptr;
}

auto BuildRelayPage() noexcept -> bool {
    RelayPage = AllocateRelayPage();
    if (RelayPage == nullptr) return false;
    std::memset(RelayPage, 0xCC, RelayPageBytes);
    for (const CallSite& site : TickSites) WriteAbsoluteJump(RelayPage + site.stubOffset, site.relay);
    for (const CallSite& site : PersistSites) WriteAbsoluteJump(RelayPage + site.stubOffset, site.relay);
    DWORD previous = 0;
    if (!VirtualProtect(RelayPage, RelayPageBytes, PAGE_EXECUTE_READ, &previous)) return false;
    FlushInstructionCache(GetCurrentProcess(), RelayPage, RelayPageBytes);
    return true;
}

template <std::size_t N>
void RestoreSites(std::array<CallSite, N>& sites) noexcept {
    for (CallSite& site : sites) {
        if (site.applied && Context->PatchBytes(site.callRva, site.written.data(), 5, site.stock.data(), 5)) site.applied = false;
    }
}

template <std::size_t N>
auto PatchSites(std::array<CallSite, N>& sites, const char* group) -> bool {
    for (CallSite& site : sites) {
        std::memcpy(site.stock.data(), reinterpret_cast<const void*>(Base + static_cast<std::uintptr_t>(site.callRva)), 5);
        if (CallTarget(site.callRva) != site.targetRva) {
            D2RL::LogErrorF(Context, "ForeverStates (%s): the %s does not call the expected function.", group, site.name);
            RestoreSites(sites);
            return false;
        }
        const auto from = Base + static_cast<std::uintptr_t>(site.callRva) + 5;
        const auto to   = reinterpret_cast<std::uintptr_t>(RelayPage) + site.stubOffset;
        const auto rel  = static_cast<std::int64_t>(to) - static_cast<std::int64_t>(from);
        if (rel < INT32_MIN || rel > INT32_MAX) {
            D2RL::LogErrorF(Context, "ForeverStates (%s): the relay page is out of reach of the %s.", group, site.name);
            RestoreSites(sites);
            return false;
        }
        site.written[0]  = 0xE8;
        const auto rel32 = static_cast<std::int32_t>(rel);
        std::memcpy(site.written.data() + 1, &rel32, sizeof(rel32));
        if (!Context->PatchBytes(site.callRva, site.stock.data(), 5, site.written.data(), 5)) {
            D2RL::LogErrorF(Context, "ForeverStates (%s): D2RLoader refused the patch on the %s (another plugin may own it).",
                group, site.name);
            RestoreSites(sites);
            return false;
        }
        site.applied = true;
        if (std::memcmp(reinterpret_cast<const void*>(Base + static_cast<std::uintptr_t>(site.callRva)), site.written.data(), 5) != 0) {
            D2RL::LogErrorF(Context, "ForeverStates (%s): the %s does not hold the written bytes.", group, site.name);
            RestoreSites(sites);
            return false;
        }
    }
    return true;
}

void InstallHold(bool helpersOk) {
    if (!helpersOk) {
        HoldStatus = "off: the game code is not the expected build (see the errors above)";
        return;
    }
    if (RelayPage == nullptr) {
        HoldStatus = "off: no relay page could be allocated";
        Context->LogError("ForeverStates: no relay page could be allocated near the game image.");
        return;
    }
    if (!PatchSites(TickSites, "keep")) {
        HoldStatus = "off: the regen event call could not be relayed";
        return;
    }
    HoldActive.store(true);
    HoldStatus = "on";
}

void InstallBind() {
    if (!Context->CheckExpectedBytes(ConsumeRva, ConsumeEntry, sizeof(ConsumeEntry))) {
        BindStatus = "off: Consume's server function is not the D2RLoader 1.3.1 code (or another plugin hooked it)";
        Context->LogError("ForeverStates (Consume): srvdofunc 159 at 521970 is not the D2RLoader 1.3.1 code (or another plugin "
                          "hooked it).");
        return;
    }
    void* original = nullptr;
    if (!Context->InstallInlineHook(ConsumeRva, ConsumeEntry, static_cast<std::uint32_t>(sizeof(ConsumeEntry)),
            reinterpret_cast<void*>(&HookedConsume), &original) || original == nullptr) {
        BindStatus = "off: D2RLoader refused the hook on 521970 (another plugin may own it)";
        Context->LogError("ForeverStates (Consume): D2RLoader refused the hook on srvdofunc 159 (another plugin may own it).");
        return;
    }
    OriginalConsume = reinterpret_cast<ConsumeFn>(original);
    BindActive.store(true);
    BindStatus = "on";
}

// D2RCore keeps a list's stats the way ReadState reads them.
auto VerifyCoreLayout() noexcept -> bool {
    const HMODULE core = GetModuleHandleW(L"D2RCore.dll");
    if (core == nullptr) return false;
    const auto* stub = reinterpret_cast<const std::uint8_t*>(reinterpret_cast<void*>(GetProcAddress(core, "ReadWideListStat")));
    if (stub == nullptr) return false;
    MEMORY_BASIC_INFORMATION info{};
    const auto readable = [&info](const std::uint8_t* at, std::size_t size) noexcept {
        if (VirtualQuery(at, &info, sizeof(info)) != sizeof(info) || info.State != MEM_COMMIT) return false;
        return reinterpret_cast<std::uintptr_t>(at) + size <= reinterpret_cast<std::uintptr_t>(info.BaseAddress) + info.RegionSize;
    };
    if (!readable(stub, 16) || std::memcmp(stub, CoreReadStub, sizeof(CoreReadStub)) != 0
            || std::memcmp(stub + 9, CoreReadStubTail, sizeof(CoreReadStubTail)) != 0) {
        return false;
    }
    const auto          rel  = static_cast<std::int32_t>(Read<std::uint32_t>(stub, 5));
    const std::uint8_t* impl = stub + 9 + rel;
    return readable(impl + CoreRecordSearchAt, sizeof(CoreRecordSearch))
        && std::memcmp(impl + CoreRecordSearchAt, CoreRecordSearch, sizeof(CoreRecordSearch)) == 0;
}

void InstallPersist(bool helpersOk) {
    if (!HoldActive.load()) {
        SaveStatus = "off: it needs the keep feature, which is off";
        return;
    }
    if (!helpersOk || !VerifyWitnesses(PersistWitnesses, "save")) {
        SaveStatus = "off: the save code is not the expected build (see the errors above)";
        return;
    }
    if (!VerifyCoreLayout()) {
        SaveStatus = "off: D2RCore keeps stat lists differently than D2RLoader 1.3.1";
        Context->LogError("ForeverStates (save): D2RCore!ReadWideListStat is not the D2RLoader 1.3.1 code; kept states are not saved.");
        return;
    }
    if (!PatchSites(PersistSites, "save")) {
        SaveStatus = "off: a save, load or world entry call could not be relayed";
        return;
    }
    PersistActive.store(true);
    SaveStatus = "on";
}

// ---------------------------------------------------------------------------
// Config
// ---------------------------------------------------------------------------

constexpr const char* DefaultConfigToml = R"TOML(# ===========================================================================
# Forever States (celestialrayone.forever-states)
# ===========================================================================
# Makes a state that a skill grants last while the player has a stat.
#
# Each rule is [skill id, stat id]:
#   skill id  The skills.txt row (*Id) whose aurastate should last.
#   stat id   The itemstatcost.txt row (*ID). While the player has this stat
#             above 0, from any source (items, skills, states):
#               - the skill's state does not run out,
#               - it comes back after save and exit,
#               - for Consume, binding demons no longer ends it.
#
# When the player loses the stat (the item is unequipped or swapped out),
# a state the plugin was keeping ends on the next frame. A state cast while
# the player never had the stat keeps its normal timer.
#
# Several rules may name the same skill: any of their stats keeps it.
# A stat id of -1 means "not set yet" and the rule does nothing.
#
# Death is untouched: the game's own death cleanup still applies.
# Console command: foreverstates (rules, kept states, last save and load).
# ===========================================================================

[forever-states]
enabled = true

rules = [
    [691, -1],  # Consume. Replace -1 with the stat id the book grants.
]
)TOML";

void ReadConfiguration() {
    if (!Context->EnsureConfig(DefaultConfigToml)) {
        Context->LogWarn("ForeverStates: the config file could not be created; using defaults.");
    }
    std::vector<char> buffer(16384, '\0');
    std::uint32_t     required = 0;
    bool read = Context->ReadConfig(buffer.data(), static_cast<std::uint32_t>(buffer.size()), &required);
    if (!read && required > buffer.size() && required <= (1u << 20)) {
        buffer.assign(static_cast<std::size_t>(required) + 1, '\0');
        read = Context->ReadConfig(buffer.data(), static_cast<std::uint32_t>(buffer.size()), nullptr);
    }
    if (!read) Context->LogWarn("ForeverStates: the config file could not be read; using defaults.");
    buffer.back() = '\0';
    const std::string_view text = read ? std::string_view(buffer.data()) : std::string_view(DefaultConfigToml);
    Settings = ParseConfig(text);
    for (const std::string& problem : Settings.problems) D2RL::LogWarnF(Context, "ForeverStates: %s.", problem.c_str());
    if (!Settings.haveRules) Context->LogWarn("ForeverStates: the TOML has no readable rules list; nothing is kept.");
    BuildTargets();
    for (const Rule& rule : IdleRules) {
        D2RL::LogWarnF(Context, "ForeverStates: the rule for skill %d has no stat set (-1); it does nothing.", rule.skill);
    }
    for (const Target& target : Targets) {
        std::string stats;
        for (const std::int32_t stat : target.stats) stats += (stats.empty() ? "" : ", ") + std::to_string(stat);
        D2RL::LogInfoF(Context, "ForeverStates: skill %d is kept while the player has stat %s.", target.skill, stats.c_str());
    }
}

// ---------------------------------------------------------------------------
// Lifecycle and console
// ---------------------------------------------------------------------------

void __cdecl OnGameLeft(const D2RL::PluginContext*, const D2RL::Lifecycle::GameplayEvent*, void*) noexcept {
    ClearTables();
}

void __cdecl OnTablesLoaded(const D2RL::PluginContext*, const D2RL::Lifecycle::DataTablesLoadedEvent*, void*) noexcept {
    ResetResolvedStates();
}

void RegisterListeners() {
    const D2RL::LifecycleService* lifecycle = nullptr;
    if (Context->QueryService(&lifecycle) != D2RL::ServiceQueryResult::Success
            || !D2RL::HasLifecycleServiceField(lifecycle, D2RL::LifecycleServiceRequiredSize)) {
        Context->LogWarn("ForeverStates: the lifecycle service is unavailable; kept-state records are dropped at the next load.");
        return;
    }
    D2RL::Lifecycle::GameplayEventListener left{};
    left.structSize = D2RL::Lifecycle::GameplayEventListenerSize;
    left.kind       = D2RL::Lifecycle::GameplayEventKind::GameLeft;
    left.callback   = &OnGameLeft;
    D2RL::Lifecycle::ListenerHandle handle = D2RL::Lifecycle::InvalidHandle;
    if (lifecycle->registerGameplayEventListener(Context, &left, &handle) != D2RL::Lifecycle::Result::Success) {
        Context->LogWarn("ForeverStates: leaving a game could not be watched; kept-state records are dropped at the next load.");
    }
    D2RL::Lifecycle::DataTablesLoadedListener tables{};
    tables.structSize = D2RL::Lifecycle::DataTablesLoadedListenerSize;
    tables.callback   = &OnTablesLoaded;
    D2RL::Lifecycle::ListenerHandle tablesHandle = D2RL::Lifecycle::InvalidHandle;
    if (lifecycle->registerDataTablesLoadedListener(Context, &tables, &tablesHandle) != D2RL::Lifecycle::Result::Success) {
        Context->LogWarn("ForeverStates: table reloads could not be watched; skill states are looked up once.");
    }
}

auto StatusCommand(D2R::Game::Client*, const D2RL::ConsoleCommandContext* command, void*) noexcept -> D2RL::ConsoleCommandResult {
    const auto say = [command](const std::string& text) { command->plugin->WriteConsoleMessage(text.c_str()); };
    say("Forever States 1.2.0");
    say("  keep: " + HoldStatus);
    say("  Consume periodic check: " + BindStatus);
    say("  save and restore: " + SaveStatus);
    for (Target& target : Targets) {
        std::string stats;
        for (const std::int32_t stat : target.stats) stats += (stats.empty() ? "" : ", ") + std::to_string(stat);
        std::string states;
        for (std::size_t c = 0; c < ContextSlots; ++c) {
            const std::int32_t state = target.stateByContext[c].load(std::memory_order_relaxed);
            if (state != Unresolved) states += (states.empty() ? "" : ", ") + std::to_string(state);
        }
        say("  rule: skill " + std::to_string(target.skill) + ", state " + (states.empty() ? std::string("(looked up in game)") : states)
            + ", stat " + stats);
    }
    for (const Rule& rule : IdleRules) say("  rule: skill " + std::to_string(rule.skill) + " has no stat set, does nothing");
    AcquireSRWLockShared(&TableLock);
    std::vector<std::string> heldLines;
    for (const Held& entry : HeldTable) {
        heldLines.push_back("    player " + std::to_string(entry.guid) + ", state " + std::to_string(entry.state)
            + (entry.snapshotFrame >= 0 ? ", copy taken at frame " + std::to_string(entry.snapshotFrame) : ", no copy yet"));
    }
    const std::size_t pending = PendingTable.size();
    ReleaseSRWLockShared(&TableLock);
    say("  kept now: " + std::to_string(heldLines.size()) + ", waiting to be restored: " + std::to_string(pending));
    for (const std::string& line : heldLines) say(line);
    say("  counters: kept " + std::to_string(HoldsStarted.load()) + ", ended on stat loss " + std::to_string(StatesReleased.load())
        + ", Consume checks skipped " + std::to_string(PeriodicSkips.load()) + ", saved " + std::to_string(StatesSaved.load())
        + " (from the last copy " + std::to_string(SnapshotsSaved.load()) + "), loaded " + std::to_string(StatesLoaded.load())
        + ", restored " + std::to_string(StatesRestored.load()) + ", not restored " + std::to_string(RestoresRefused.load()));
    say("  last save: " + GetSummary(LastSave));
    say("  last load: " + GetSummary(LastLoad));
    say("  last restore: " + GetSummary(LastRestore));
    return D2RL::ConsoleCommandResult::Handled;
}

constexpr D2RL::PluginInfo PluginInfoData{
    .infoSize    = D2RL::PluginInfoSize,
    .abiVersion  = D2RL_PLUGIN_ABI_VERSION,
    .id          = "celestialrayone.forever-states",
    .name        = "Forever States",
    .version     = "1.2.0",
    .author      = "CelestialRayOne",
    .description = "A skill's state lasts while the player has a configured stat, survives save and exit, and Consume is not "
                   "ended by binding demons.",
    .flags       = D2RL::PluginFlags::Server | D2RL::PluginFlags::NativeHooks,
    .reserved    = {},
};

}  // namespace

D2RL_PLUGIN_EXPORT auto D2RLoaderGetPluginInfo() noexcept -> const D2RL::PluginInfo* {
    return &PluginInfoData;
}

D2RL_PLUGIN_EXPORT auto D2RLoaderLoadPlugin(const D2RL::PluginContext* context) noexcept -> bool {
    Context = context;
    if (Context == nullptr || Context->exeBase == 0) return false;
    Base = Context->exeBase;

    if (!Context->RegisterConsoleCommand("foreverstates", &StatusCommand,
            "Show Forever States' rules, the states it keeps and the last save, load and restore.")) {
        Context->LogWarn("ForeverStates: the foreverstates console command could not be registered.");
    }
    ReadConfiguration();
    if (!Settings.enabled) {
        HoldStatus = BindStatus = SaveStatus = "off: enabled = false";
        Context->LogInfo("ForeverStates: disabled by config.");
        return true;
    }
    if (Targets.empty()) {
        HoldStatus = BindStatus = SaveStatus = "off: no rule has a stat set";
        Context->LogWarn("ForeverStates: no rule has a stat set; nothing to do.");
        return true;
    }
    RegisterListeners();
    const bool helpersOk = VerifyHelpers();
    if (helpersOk && !BuildRelayPage()) RelayPage = nullptr;
    // Each feature installs on its own, so one refusing never takes another down.
    InstallHold(helpersOk);
    InstallBind();
    InstallPersist(helpersOk);
    D2RL::LogInfoF(Context, "ForeverStates 1.2.0: keep %s; Consume periodic check %s; save and restore %s.", HoldStatus.c_str(),
        BindStatus.c_str(), SaveStatus.c_str());
    return true;
}

D2RL_PLUGIN_EXPORT void D2RLoaderUnloadPlugin() noexcept {
    HoldActive.store(false);
    BindActive.store(false);
    PersistActive.store(false);
    if (Context != nullptr) {
        RestoreSites(PersistSites);
        RestoreSites(TickSites);
    }
    ClearTables();
}
