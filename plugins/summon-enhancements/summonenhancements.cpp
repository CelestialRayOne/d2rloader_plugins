// ===========================================================================
// Summon Enhancements (celestialrayone.summon-enhancements)
// ===========================================================================
//
// Formerly Bind Demon Persistence and Auras. Server side.
//   - Every bound demon is saved and restored, for every class.
//   - The summons of the skills in summon_skills are saved and come back
//     when the character enters the world.
//   - Bound demons with Aura Enchanted roll their aura from a list.
//
// ---------------------------------------------------------------------------
// 1. How the game saves a bound demon (D2R 3.3.93847, D2RLoader image)
// ---------------------------------------------------------------------------
//   Player save 52F090 writes the demon block last, through 533AE0, and only
//   in a RotW game (Game+106h == 3). It runs twice per save: once into a 32 KB
//   measuring buffer, once into the real buffer.
//   533AE0 (game, player, cursor**, end) writes one block:
//     word 1, dword 666Ch with byte 2 = "has demon", then 5Ch header bytes and
//     the stat blob. The demon comes from the serializer 533CE0, which refuses
//     unless: the player is class 7, 4FF1A0 (game, owner, 21, 1) returns the
//     FIRST pet of pettype 21, that pet has state 207 (3351B0), the monster
//     has a monpet.txt row (D2RCore GetMonPetRecordForUnit), and the block
//     fits ("[PLAYERSAVEWrite] Save too large, Demon will be deleted.").
//     So one demon at most, and only whichever is first in the pet list.
//   4FF1A0 (game, owner, petType, includeFlagged): player data 34B240 +98h is
//     the pet container, its first qword the bucket array, 20h bytes per pet
//     type: +0 first node, +8 count, +0Ch max, +10h pool. Node: +4 unit guid,
//     +18h next. The unit comes from 48FE80 (game, 1, guid).
//
// ---------------------------------------------------------------------------
// 2. How the game restores it
// ---------------------------------------------------------------------------
//   Load 52E910 calls the block reader 531510 (game, player, cursor**, end,
//   version) and then never looks at the cursor again, so bytes after the
//   block are ignored. 531510 checks word 1 and dword 666Ch (byte 2 <= 1),
//   copies the header and blob and calls the restore 531790, which recreates
//   the monster and binds it again (51E0F0 -> 51EF90). The bind adds the pet
//   through 501550, which first sets the pettype cap with 501830 (game, owner,
//   type, petmax, enforce) and trims pets over the cap. petmax is worked out
//   while the save is still loading, before items and +skills count.
//   A block is 62h bytes plus the blob, whose size is the header's first word.
//
// ---------------------------------------------------------------------------
// 3. Every bound demon (save_every_demon)
// ---------------------------------------------------------------------------
//   Save: 533AE0 is called once per bound demon. During each call 4FF1A0
//   returns that demon instead of the first pet, so every native rule above
//   still decides whether the demon is written. A demon the game refuses is
//   rolled back and named in the log with the rule it failed. With no demon
//   written the native call runs once as before (an empty block).
//   Load: after the native block, every following block that holds a demon is
//   read by the same native reader. While a save loads, the Bind Demon cap
//   (pettype 21) is raised to fit the demons already restored plus the one
//   being added, so a cap worked out before items count cannot remove them.
//   Without the plugin the game reads only the first block and ignores the
//   rest, so the character still loads with its first demon.
//
// ---------------------------------------------------------------------------
// 4. Bound demons of any class (demon_all_classes)
// ---------------------------------------------------------------------------
//   533CE0 compares the class with 7 at 533D38 (cmp eax,7 / jne), and the
//   compiler reuses that 7 to build the 4FF1A0 arguments right after it
//   (lea r9d,[rax-6] / mov rdx,rdi / lea r8d,[rax+0Eh] = 1, player, 21).
//   Removing only the jne would hand other classes a wrong pettype, so the
//   20 bytes 533D38..533D4B become mov r9d,1 / mov rdx,rdi / mov r8d,21 /
//   nop5: the Warlock's own arguments for every class. 533D4C (mov rcx,r13)
//   and the call follow unchanged. No branch of 533CE0..5341BE lands inside
//   the range.
//   Restore: 531790 only rebuilds a demon when 33DD40 (player, Bind Demon, 1)
//   finds the skill; the id is the mov edx at 531808 (382 in the stock image,
//   692 after warlock-hardcoded-ids-fix.json), read live. 52E910 reads the
//   items before the demon block, but item stats only count later, so a
//   character that has Bind Demon only from items (a set bonus oskill) does
//   not have it yet. Its demon blocks are kept by the plugin instead of being
//   handed to 531510, and given to the same native reader when the character
//   enters the world: 485350 (client, player, game, room, x, y) places the
//   player in a room and ends with 53BC60 (game, player), which warps every
//   pet to the player through 500A40. After the native call the plugin reads
//   the kept blocks (the demon is created without a room, as at load) and
//   calls 53BC60 again to place it. A character that still has no Bind Demon
//   keeps the blocks for its next world entry in the same game.
//
// ---------------------------------------------------------------------------
// 5. Summons (save_summons, summon_skills)
// ---------------------------------------------------------------------------
//   Save: after the demon blocks, one block: "SUMN", format 1, a reserved
//   byte, a u16 count, then per summon the skills.txt id and the monstats row
//   (u16 each). A pet is matched to a listed skill by pettype (skill record
//   +112h); when several listed skills share the pettype, by summon monster
//   (+110h). pettype 21 (binddemon) is left to the demon blocks.
//   Load: the block is read after the demon blocks and kept until the
//   character enters the world (485350, after the native call), so skill
//   levels and petmax count items. Each summon is made again the way its
//   srvdofunc (skill record +4Eh, read by the do handler at 43AED3 to index
//   238EA00) makes it, at the player's feet: SummonPet 4350E0 with flag 1
//   and the player's path position (341A20 x, 341A30 y of unit+38h), the
//   arguments block laid out as srvdofunc 31 builds it at 55E6F5.
//     16  558000 Valkyrie: class and mode from 433800, SummonPet, 55FB50
//         with 0, the +194h calc as the pet level for 55EF80, 48B890,
//         48B720 at frame +20, state 93, 34EF50, 58D950.
//     31  55E440 skeletons: 55EDB0, 433800, SummonPet, 55FB50 with 0, 55A5D0
//         with element 0 (ESR's montype.txt element column is empty), 55EF80,
//         55EC60, 58D950.
//     56  55C080 golems: summon +110h, mode +113h, SummonPet, 55B040,
//         48FDE0 then 480AD0, 58D950.
//     119 575990 druid summons: 433800, SummonPet, 58D950, 55FB50 with the
//         +194h calc (at least 1), 55EF80.
//   Left out, because they belong to casting: the target corpse of 31 and of
//   56's corpse variant, the target check 432B70 of 119, and 34E790, which
//   sets the "srvdofunc ran" flag 40h of unit+124h. petmax is the +114h calc
//   at the current level; a summon is skipped when it works out below 1, and
//   the pet list trims to petmax as usual. The skill and its level come from
//   33DD40 and 33D1E0 (the restore's own calls at 531814 and 531A1B).
//
// ---------------------------------------------------------------------------
// 6. Where a bound demon's aura is rolled (aura list feature)
// ---------------------------------------------------------------------------
//   Monster mod 30 (Aura Enchanted) runs D2RCore!ApplyAuraEnchanted(unit, mod,
//   flag). The game's monster mod table slot 0x2395FE0 + 30*8 points at it
//   through a loader thunk, and D2RCore!ApplyBindDemonBoundStats also calls it
//   directly. A demon reaches it at three moments:
//     a. New bind: the bind conversion 51EF90 adds the mods from Bind Demon's
//        skills.txt calc2-calc5 through ApplyUMod 4995E0, called at 51F333.
//     b. End of every bind, new or restored: 51EF90 calls
//        D2RCore!ApplyBindDemonBoundStats at 51F3AE. With
//        aura_enchanted_selection "3.2" it rolls Aura Enchanted again for a
//        demon that has mod 30. With "3.1" it does not.
//     c. Restore: 531790 re-applies the saved mods through 49B3D0, called at
//        5319FF, before it runs the bind conversion. 49B3D0 is only called
//        when the saved rank flags are nonzero (5319E9), which is why a plain
//        monster comes back without its mods.
//   D2RCore's roll keys on the monster's name seed (38E370): slot =
//   (seed * 6AC690C5h + 29Ah) mod slots, level = monster level / divisor,
//   clamped 1..99, then set-skill 433910 (unit, skill, level, 1) and
//   select-skill 438A70 (unit, 0, skill, -1), the active right-hand aura.
//
// ---------------------------------------------------------------------------
// 7. What the aura list feature does
// ---------------------------------------------------------------------------
//   - A detour on D2RCore!ApplyAuraEnchanted. While the plugin has marked a
//     unit as the demon being bound or restored (thread local), a roll for
//     that unit picks from the TOML list instead. Every other unit goes to
//     the original routine. The detour copies the 19 byte prologue (8 pushes,
//     sub rsp,148h, no relative operands) into a trampoline.
//   - Three call relays mark the demon around the calls in 6a, 6b and 6c. The
//     relay for 6b also rolls once after the call when D2RCore did not (the
//     "3.1" setting), so every bound demon with mod 30 gets a list aura.
//   - One call relay on 51F2C6, where 51EF90 hands normal monsters and minions
//     a new random name seed through D2RCore!PrepareBindDemonRules. For a
//     restore (51EF90's newBind is 0) of a demon with mod 30 the relay passes
//     the demon's current seed, which 49B3D0 just restored from the save, so
//     a restored minion keeps its aura. The relay stub reads newBind from
//     51EF90's frame, [rsp+330h] at the call site, the byte 51F2CB tests.
//   - The pick: every entry whose min_level <= monster level, in list order;
//     index = (seed * 6AC690C5h + 29Ah) mod eligible count, D2RCore's hash.
//     The default list reproduces D2RCore's "3.2" pet column exactly.
//   - A skill id outside skills.txt is never set: it is checked against the
//     row count at DataTables+11B8h, the bound the skills-record lookup 97790
//     checks (its call to DataTables 300A90 is part of the witness).
//
// ---------------------------------------------------------------------------
// 8. How the features meet
// ---------------------------------------------------------------------------
//   The persistence hooks own five function entries (4FF1A0, 501830, 485350,
//   533AE0, 531510) and the 20 bytes at 533D38. The aura feature patches four
//   call sites inside 51EF90 and 531790 and the entry of
//   D2RCore!ApplyAuraEnchanted. No byte is shared. Every demon the plugin
//   restores, at load or on entering the world, goes through the native
//   restore 531790, so each one passes the aura relays like the first one.
//   Each feature installs on its own: one refusing never takes another down.
//   Unit facts the aura code relies on, read the same way by D2RCore: unit+0
//   type (1 = monster), +4 class id, +8 unit id, +10h MonsterData, +1BDh data
//   context; MonsterData+20h is the 9 byte monster mod list. Monster level is
//   stat 12 through 2F5020 (unit, stat, layer).
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

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

namespace {

// PURE-BEGIN ------------------------------------------------------------------
// Config parsing and the aura pick. No game or Windows code, so the test
// harness compiles this part on its own.

struct AuraEntry {
    std::int32_t skill    = -1;
    std::int32_t divisor  = 6;
    std::int32_t minLevel = 0;
};

struct Config {
    bool                      enabled{ true };
    bool                      saveEveryDemon{ true };
    bool                      demonAllClasses{ true };
    bool                      saveSummons{ true };
    bool                      auraList{ true };
    bool                      haveAuras{ false };
    bool                      haveSummonSkills{ false };
    std::vector<AuraEntry>    auras;
    std::vector<std::int32_t> summonSkills;
};

constexpr std::size_t  MaxAuraEntries  = 256;
constexpr std::size_t  MaxSummonSkills = 256;
constexpr std::int32_t MaxSkillId      = 0xFFFF;
constexpr std::int32_t MaxDivisor      = 9999;
constexpr std::int32_t MaxMinLevel     = 99999;


class Scanner {
public:
    explicit Scanner(std::string_view text) noexcept : text_(text) {}

    auto AtEnd() const noexcept -> bool { return pos_ >= text_.size(); }
    auto Peek() const noexcept -> char { return AtEnd() ? '\0' : text_[pos_]; }
    auto Line() const noexcept -> std::size_t { return line_; }

    void Advance() noexcept {
        if (AtEnd()) return;
        if (text_[pos_] == '\n') ++line_;
        ++pos_;
    }

    void SkipSpaces() noexcept {
        while (!AtEnd() && (Peek() == ' ' || Peek() == '\t' || Peek() == '\r')) Advance();
    }

    void SkipComment() noexcept {
        if (Peek() != '#') return;
        while (!AtEnd() && Peek() != '\n') Advance();
    }

    // Spaces, comments and line breaks.
    void SkipBlank() noexcept {
        for (;;) {
            SkipSpaces();
            SkipComment();
            if (Peek() == '\n') {
                Advance();
                continue;
            }
            return;
        }
    }

    auto Key() noexcept -> std::string_view {
        const std::size_t start = pos_;
        while (!AtEnd()) {
            const char c = Peek();
            const bool word = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-';
            if (!word) break;
            Advance();
        }
        return text_.substr(start, pos_ - start);
    }

    auto Integer(std::int64_t& out) noexcept -> bool {
        bool negative = false;
        if (Peek() == '+' || Peek() == '-') {
            negative = Peek() == '-';
            Advance();
        }
        std::int64_t value  = 0;
        std::size_t  digits = 0;
        while (!AtEnd()) {
            const char c = Peek();
            if (c == '_') {
                Advance();
                continue;
            }
            if (c < '0' || c > '9') break;
            value = value * 10 + (c - '0');
            if (value > 0x7FFFFFFFLL) return false;
            ++digits;
            Advance();
        }
        if (digits == 0) return false;
        out = negative ? -value : value;
        return true;
    }

    auto Boolean(bool& out) noexcept -> bool {
        const std::string_view word = Key();
        if (word == "true") {
            out = true;
            return true;
        }
        if (word == "false") {
            out = false;
            return true;
        }
        return false;
    }

    // Skips the rest of a value: to the end of the line, or past the
    // matching bracket of an array or inline table.
    void SkipValue() noexcept {
        int  depth    = 0;
        bool inString = false;
        while (!AtEnd()) {
            const char c = Peek();
            if (inString) {
                if (c == '\\') {
                    Advance();
                } else if (c == '"') {
                    inString = false;
                }
                Advance();
                continue;
            }
            if (c == '"') {
                inString = true;
            } else if (c == '#') {
                SkipComment();
                continue;
            } else if (c == '[' || c == '{') {
                ++depth;
            } else if (c == ']' || c == '}') {
                --depth;
            } else if (c == '\n' && depth <= 0) {
                return;
            }
            Advance();
        }
    }

private:
    std::string_view text_;
    std::size_t      pos_  = 0;
    std::size_t      line_ = 1;
};

auto Fail(std::string& error, const Scanner& scan, const char* what) -> bool {
    error = "line " + std::to_string(scan.Line()) + ": " + what;
    return false;
}

auto ParseAuraEntry(Scanner& scan, AuraEntry& entry, std::string& error) -> bool {
    bool haveSkill = false, haveDivisor = false, haveMinLevel = false;
    scan.Advance();  // {
    for (;;) {
        scan.SkipBlank();
        if (scan.Peek() == '}') {
            scan.Advance();
            break;
        }
        const std::string_view key = scan.Key();
        if (key.empty()) return Fail(error, scan, "expected skill, divisor or min_level inside { }");
        scan.SkipSpaces();
        if (scan.Peek() != '=') return Fail(error, scan, "expected = after a key inside { }");
        scan.Advance();
        scan.SkipSpaces();
        std::int64_t value = 0;
        if (!scan.Integer(value)) return Fail(error, scan, "expected a whole number");
        if (key == "skill") {
            if (haveSkill) return Fail(error, scan, "skill is given twice in one entry");
            if (value < 1 || value > MaxSkillId) return Fail(error, scan, "skill must be from 1 to 65535");
            entry.skill = static_cast<std::int32_t>(value);
            haveSkill   = true;
        } else if (key == "divisor") {
            if (haveDivisor) return Fail(error, scan, "divisor is given twice in one entry");
            if (value < 1 || value > MaxDivisor) return Fail(error, scan, "divisor must be from 1 to 9999");
            entry.divisor = static_cast<std::int32_t>(value);
            haveDivisor   = true;
        } else if (key == "min_level") {
            if (haveMinLevel) return Fail(error, scan, "min_level is given twice in one entry");
            if (value < 0 || value > MaxMinLevel) return Fail(error, scan, "min_level must be from 0 to 99999");
            entry.minLevel = static_cast<std::int32_t>(value);
            haveMinLevel   = true;
        } else {
            return Fail(error, scan, "unknown key inside { } (use skill, divisor, min_level)");
        }
        scan.SkipBlank();
        if (scan.Peek() == ',') {
            scan.Advance();
            continue;
        }
        if (scan.Peek() != '}') return Fail(error, scan, "expected , or } inside { }");
    }
    if (!haveSkill) return Fail(error, scan, "an entry has no skill");
    return true;
}

auto ParseAuraList(Scanner& scan, std::vector<AuraEntry>& list, std::string& error) -> bool {
    if (scan.Peek() != '[') return Fail(error, scan, "auras must be a list: auras = [ ... ]");
    scan.Advance();
    list.clear();
    for (;;) {
        scan.SkipBlank();
        if (scan.Peek() == ']') {
            scan.Advance();
            return true;
        }
        if (scan.Peek() != '{') return Fail(error, scan, "each aura is written { skill = N, divisor = N }");
        AuraEntry entry{};
        if (!ParseAuraEntry(scan, entry, error)) return false;
        if (list.size() >= MaxAuraEntries) return Fail(error, scan, "more than 256 entries");
        list.push_back(entry);
        scan.SkipBlank();
        if (scan.Peek() == ',') {
            scan.Advance();
            continue;
        }
        if (scan.Peek() != ']') return Fail(error, scan, "expected , or ] after an entry");
    }
}

// summon_skills = [ id, id, ... ]. A repeated id is kept once.
auto ParseSkillList(Scanner& scan, std::vector<std::int32_t>& list, std::string& error) -> bool {
    if (scan.Peek() != '[') return Fail(error, scan, "summon_skills must be a list: summon_skills = [ ... ]");
    scan.Advance();
    list.clear();
    for (;;) {
        scan.SkipBlank();
        if (scan.Peek() == ']') {
            scan.Advance();
            return true;
        }
        std::int64_t value = 0;
        if (!scan.Integer(value)) return Fail(error, scan, "each summon_skills entry is a skills.txt *Id");
        if (value < 1 || value > MaxSkillId) return Fail(error, scan, "summon_skills ids must be from 1 to 65535");
        const auto id = static_cast<std::int32_t>(value);
        if (std::find(list.begin(), list.end(), id) == list.end()) {
            if (list.size() >= MaxSummonSkills) return Fail(error, scan, "summon_skills has more than 256 entries");
            list.push_back(id);
        }
        scan.SkipBlank();
        if (scan.Peek() == ',') {
            scan.Advance();
            continue;
        }
        if (scan.Peek() != ']') return Fail(error, scan, "expected , or ] in summon_skills");
    }
}

// After a list that could not be read: skips past the ] that closes it, so
// the keys after it are still read.
void SkipPastList(Scanner& scan) noexcept {
    bool inString = false;
    while (!scan.AtEnd()) {
        const char c = scan.Peek();
        if (inString) {
            if (c == '"') inString = false;
            scan.Advance();
            continue;
        }
        if (c == '"') {
            inString = true;
        } else if (c == '#') {
            scan.SkipComment();
            continue;
        } else if (c == ']') {
            scan.Advance();
            return;
        }
        scan.Advance();
    }
}

// Reads the switches, summon_skills and auras. A switch that is not true or
// false is reported and stays on. A list that cannot be read fills its own
// error and stays empty; the keys after it are still read.
void ParseConfig(std::string_view text, Config& out, std::vector<std::string>& warnings, std::string& auraError,
        std::string& summonError) {
    Scanner scan(text);
    for (;;) {
        scan.SkipBlank();
        if (scan.AtEnd()) return;
        if (scan.Peek() == '[') {
            scan.SkipValue();
            continue;
        }
        const std::size_t      line = scan.Line();
        const std::string_view key  = scan.Key();
        scan.SkipSpaces();
        if (key.empty() || scan.Peek() != '=') {
            warnings.push_back("line " + std::to_string(line) + ": expected key = value; the line was skipped");
            scan.SkipValue();
            if (!scan.AtEnd() && scan.Peek() != '\n') scan.Advance();
            continue;
        }
        scan.Advance();
        scan.SkipSpaces();
        bool* flag = key == "enabled"           ? &out.enabled
                   : key == "save_every_demon"  ? &out.saveEveryDemon
                   : key == "demon_all_classes" ? &out.demonAllClasses
                   : key == "save_summons"      ? &out.saveSummons
                   : key == "aura_list"         ? &out.auraList
                   : nullptr;
        if (flag != nullptr) {
            bool value = true;
            if (scan.Boolean(value)) {
                *flag = value;
            } else {
                *flag = true;
                warnings.push_back("line " + std::to_string(line) + ": " + std::string(key) + " must be true or false; using true");
            }
            scan.SkipValue();
            continue;
        }
        if (key == "auras") {
            if (ParseAuraList(scan, out.auras, auraError)) {
                out.haveAuras = true;
                scan.SkipValue();
            } else {
                out.auras.clear();
                out.haveAuras = false;
                SkipPastList(scan);
            }
            continue;
        }
        if (key == "summon_skills") {
            if (ParseSkillList(scan, out.summonSkills, summonError)) {
                out.haveSummonSkills = true;
                scan.SkipValue();
            } else {
                out.summonSkills.clear();
                out.haveSummonSkills = false;
                SkipPastList(scan);
            }
            continue;
        }
        scan.SkipValue();
    }
}


// The same hash D2RCore's Aura Enchanted roll uses on the name seed.
constexpr auto SeedHash(std::uint16_t seed) noexcept -> std::uint32_t {
    return static_cast<std::uint32_t>(seed) * 0x6AC690C5u + 0x29Au;
}

// Picks the entry for a demon. False when no entry qualifies.
auto PickEntry(const std::vector<AuraEntry>& list, std::uint16_t seed, std::int32_t monsterLevel, AuraEntry& out) noexcept -> bool {
    std::uint32_t eligible = 0;
    for (const auto& entry : list) {
        if (entry.minLevel <= monsterLevel) ++eligible;
    }
    if (eligible == 0) return false;
    std::uint32_t index = SeedHash(seed) % eligible;
    for (const auto& entry : list) {
        if (entry.minLevel > monsterLevel) continue;
        if (index == 0) {
            out = entry;
            return true;
        }
        --index;
    }
    return false;
}

// Level = monster level / divisor, kept between 1 and 99, as D2RCore does.
constexpr auto AuraLevel(std::int32_t monsterLevel, std::int32_t divisor) noexcept -> std::int32_t {
    const std::int32_t base  = monsterLevel < 1 ? 1 : monsterLevel;
    const std::int32_t level = base / (divisor < 1 ? 1 : divisor);
    return level < 1 ? 1 : (level > 99 ? 99 : level);
}


// The summon block the plugin writes after the bound demon blocks:
// "SUMN", format, a reserved byte, a u16 count, then per summon the
// skills.txt id and the monstats row (u16 each).
struct SavedSummon {
    std::uint16_t skill{};
    std::uint16_t monster{};
};

constexpr std::uint8_t   SummonMagic[4]{ 'S', 'U', 'M', 'N' };
constexpr std::uint8_t   SummonFormat     = 1;
constexpr std::ptrdiff_t SummonHeaderSize = 8;
constexpr std::ptrdiff_t SummonRecordSize = 4;
constexpr std::size_t    MaximumSummons   = 255;
constexpr std::ptrdiff_t DemonBlockFixed  = 0x62;  // word 1, dword 666Ch, 5Ch header
constexpr std::size_t    DemonBlobSizeAt  = 6;     // the header's first word

inline auto LoadU16(const std::uint8_t* p) noexcept -> std::uint16_t {
    std::uint16_t value = 0;
    std::memcpy(&value, p, sizeof(value));
    return value;
}

// How many of count summons fit in room bytes, header included.
constexpr auto SummonsThatFit(std::size_t count, std::ptrdiff_t room) noexcept -> std::size_t {
    if (room < SummonHeaderSize + SummonRecordSize) return 0;
    const auto space = static_cast<std::size_t>((room - SummonHeaderSize) / SummonRecordSize);
    const std::size_t wanted = count < MaximumSummons ? count : MaximumSummons;
    return wanted < space ? wanted : space;
}

// Writes the block for the first count summons to out and returns its size.
inline auto EncodeSummons(std::uint8_t* out, const std::vector<SavedSummon>& summons, std::size_t count) noexcept
        -> std::ptrdiff_t {
    if (count > summons.size()) count = summons.size();
    if (count > MaximumSummons) count = MaximumSummons;
    std::memcpy(out, SummonMagic, sizeof(SummonMagic));
    out[4] = SummonFormat;
    out[5] = 0;
    const auto stored = static_cast<std::uint16_t>(count);
    std::memcpy(out + 6, &stored, sizeof(stored));
    std::uint8_t* record = out + SummonHeaderSize;
    for (std::size_t i = 0; i < count; ++i) {
        std::memcpy(record, &summons[i].skill, sizeof(std::uint16_t));
        std::memcpy(record + 2, &summons[i].monster, sizeof(std::uint16_t));
        record += SummonRecordSize;
    }
    return SummonHeaderSize + SummonRecordSize * static_cast<std::ptrdiff_t>(count);
}

// The size of the summon block at p when a whole one lies before end, its
// summons added to out; 0 when there is none.
inline auto DecodeSummons(const std::uint8_t* p, const std::uint8_t* end, std::vector<SavedSummon>& out)
        -> std::ptrdiff_t {
    if (p == nullptr || end - p < SummonHeaderSize || std::memcmp(p, SummonMagic, sizeof(SummonMagic)) != 0
            || p[4] != SummonFormat) {
        return 0;
    }
    const std::uint16_t  count = LoadU16(p + 6);
    const std::ptrdiff_t size  = SummonHeaderSize + SummonRecordSize * static_cast<std::ptrdiff_t>(count);
    if (end - p < size) return 0;
    const std::uint8_t* record = p + SummonHeaderSize;
    for (std::uint16_t i = 0; i < count; ++i) {
        if (out.size() < MaximumSummons) out.push_back({ LoadU16(record), LoadU16(record + 2) });
        record += SummonRecordSize;
    }
    return size;
}

// Length of the bound demon block at p (62h bytes plus the blob), 0 when it
// does not fit before end.
inline auto DemonBlockLength(const std::uint8_t* p, const std::uint8_t* end) noexcept -> std::ptrdiff_t {
    if (p == nullptr || end - p < DemonBlockFixed) return 0;
    const std::ptrdiff_t length = DemonBlockFixed + static_cast<std::ptrdiff_t>(LoadU16(p + DemonBlobSizeAt));
    return end - p >= length ? length : 0;
}

// "493 x8, 504 x4" for the first count summons.
inline auto CountBySkill(const std::vector<SavedSummon>& summons, std::size_t count) -> std::string {
    std::vector<std::pair<std::uint16_t, std::size_t>> counts;
    for (std::size_t i = 0; i < count && i < summons.size(); ++i) {
        auto it = std::find_if(counts.begin(), counts.end(),
            [&](const std::pair<std::uint16_t, std::size_t>& entry) { return entry.first == summons[i].skill; });
        if (it == counts.end()) {
            counts.emplace_back(summons[i].skill, 1);
        } else {
            ++it->second;
        }
    }
    std::string text;
    for (const auto& entry : counts) {
        if (!text.empty()) text += ", ";
        text += std::to_string(entry.first) + " x" + std::to_string(entry.second);
    }
    return text;
}

// PURE-END --------------------------------------------------------------------

// ---------------------------------------------------------------------------
// Addresses (D2RLoader process image, D2R 3.3.93847)
// ---------------------------------------------------------------------------

constexpr std::uint64_t WriteDemonBlockRva = 0x533AE0;
constexpr std::uint64_t ReadDemonBlockRva  = 0x531510;
constexpr std::uint64_t FirstPetOfTypeRva  = 0x4FF1A0;
constexpr std::uint64_t SetPetTypeCapRva   = 0x501830;
constexpr std::uint64_t GetServerUnitRva   = 0x48FE80;
constexpr std::uint64_t GetPlayerDataRva   = 0x34B240;
constexpr std::uint64_t GetUnitGuidRva     = 0x34A330;
constexpr std::uint64_t GetClassIdRva      = 0x349860;
constexpr std::uint64_t CheckStateRva      = 0x3351B0;
constexpr std::uint64_t DataTablesRva      = 0x300A90;

constexpr std::uint64_t WriteBlockTypeCheckRva = 0x533B00;
constexpr std::uint64_t ReadBlockTypeCheckRva  = 0x531530;
constexpr std::uint64_t FirstPetOfTypeBodyRva  = 0x4FF1BA;
constexpr std::uint64_t SetPetTypeCapBodyRva   = 0x501842;

constexpr std::int32_t  BindPetType          = 21;    // pettype.txt binddemon, fixed by the native save
constexpr std::int32_t  BindDemonState       = 207;   // states.txt bind_demon, fixed by the native save
constexpr std::int32_t  WarlockClass         = 7;
constexpr std::uint32_t MonsterUnitType      = 1;
constexpr std::uint8_t  ClassicGame          = 1;
constexpr std::size_t   GameContextOffset    = 0x106;
constexpr std::size_t   PetContainerOffset   = 0x98;
constexpr std::size_t   PetTypeCountOffset   = 0x12E0;
constexpr std::size_t   BucketStride         = 0x20;
constexpr std::size_t   BucketCountOffset    = 0x08;
constexpr std::size_t   NodeGuidOffset       = 0x04;
constexpr std::size_t   NodeNextOffset       = 0x18;
constexpr std::uint32_t OldestModernVersion  = 0x66;  // 531510 uses its old layout up to this version
constexpr std::ptrdiff_t BlockHeaderSize     = 6;
constexpr std::size_t   MaximumDemons        = 64;
constexpr std::size_t   MaximumNodes         = 1024;

// ---------------------------------------------------------------------------
// Byte witnesses
// ---------------------------------------------------------------------------

// 533AE0: push rbp / push r14 / push r15 / lea rbp,[rsp-47h] / sub rsp,0E0h.
// The next instruction is the RIP-relative cookie load, so 18 bytes.
constexpr std::uint8_t WriteDemonBlockBytes[]{
    0x40,0x55,0x41,0x56,0x41,0x57,0x48,0x8D,0x6C,0x24,0xB9,0x48,0x81,0xEC,0xE0,0x00,0x00,0x00 };
// 531510: push rbp / push r12 / push r13 / lea rbp,[rsp-3Fh] / sub rsp,0F0h.
constexpr std::uint8_t ReadDemonBlockBytes[]{
    0x40,0x55,0x41,0x54,0x41,0x55,0x48,0x8D,0x6C,0x24,0xC1,0x48,0x81,0xEC,0xF0,0x00,0x00,0x00 };
// 4FF1A0: mov [rsp+8],rbx / mov [rsp+10h],rbp / mov [rsp+18h],rsi.
constexpr std::uint8_t FirstPetOfTypeBytes[]{
    0x48,0x89,0x5C,0x24,0x08,0x48,0x89,0x6C,0x24,0x10,0x48,0x89,0x74,0x24,0x18 };
// 501830: mov [rsp+8],rbx / mov [rsp+10h],rbp / push rsi / push rdi / push r14.
constexpr std::uint8_t SetPetTypeCapBytes[]{
    0x48,0x89,0x5C,0x24,0x08,0x48,0x89,0x6C,0x24,0x10,0x56,0x57,0x41,0x56 };
// 533B00 / 531530: cmp byte [rcx+106h],1, the classic-game check of both blocks.
constexpr std::uint8_t BlockTypeCheckBytes[]{ 0x80,0xB9,0x06,0x01,0x00,0x00,0x01 };
// 4FF1BA: mov r14,rcx / movsxd rdi,r8d / mov rcx,rdx / mov ebx,r9d
// (game, owner, petType, includeFlagged).
constexpr std::uint8_t FirstPetOfTypeBodyBytes[]{
    0x4C,0x8B,0xF1,0x49,0x63,0xF8,0x48,0x8B,0xCA,0x41,0x8B,0xD9 };
// 501842: mov rbp,rcx / mov ebx,r9d / mov rcx,rdx / mov edi,r8d
// (game, owner, petType, petmax).
constexpr std::uint8_t SetPetTypeCapBodyBytes[]{
    0x48,0x8B,0xE9,0x41,0x8B,0xD9,0x48,0x8B,0xCA,0x41,0x8B,0xF8 };

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

const std::array<Witness, 8> Witnesses{
    W(WriteDemonBlockRva, WriteDemonBlockBytes, "demon block writer"),
    W(ReadDemonBlockRva, ReadDemonBlockBytes, "demon block reader"),
    W(FirstPetOfTypeRva, FirstPetOfTypeBytes, "first pet of a pettype"),
    W(SetPetTypeCapRva, SetPetTypeCapBytes, "pettype cap"),
    W(WriteBlockTypeCheckRva, BlockTypeCheckBytes, "demon block writer game check"),
    W(ReadBlockTypeCheckRva, BlockTypeCheckBytes, "demon block reader game check"),
    W(FirstPetOfTypeBodyRva, FirstPetOfTypeBodyBytes, "first pet of a pettype arguments"),
    W(SetPetTypeCapBodyRva, SetPetTypeCapBodyBytes, "pettype cap arguments"),
};

// ---------------------------------------------------------------------------
// Natives
// ---------------------------------------------------------------------------

using WriteBlockFn    = std::uint32_t(__fastcall*)(void* game, void* player, std::uint8_t** cursor,
                            std::uint8_t* end);
using ReadBlockFn     = std::uint32_t(__fastcall*)(void* game, void* player, std::uint8_t** cursor,
                            std::uint8_t* end, std::uint32_t version);
using FirstPetFn      = void*(__fastcall*)(void* game, void* owner, std::int32_t petType,
                            std::int32_t includeFlagged);
using SetCapFn        = std::uint64_t(__fastcall*)(void* game, void* owner, std::int32_t petType,
                            std::int32_t petMax, std::uint8_t enforce);
using ServerUnitFn    = void*(__fastcall*)(void* game, std::uint32_t unitType, std::uint32_t guid);
using PlayerDataFn    = void*(__fastcall*)(void* unit);
using UnitGuidFn      = std::uint32_t(__fastcall*)(void* unit, const char* file, std::int32_t line);
using ClassIdFn       = std::int32_t(__fastcall*)(void* unit, const char* file, std::int32_t line);
using CheckStateFn    = std::uint64_t(__fastcall*)(void* unit, std::int32_t state);
using DataTablesFn    = std::uint8_t*(__fastcall*)(std::uint8_t context);

const D2RL::PluginContext* Context{};
std::uintptr_t             Base{};

WriteBlockFn OriginalWriteBlock{};
ReadBlockFn  OriginalReadBlock{};
FirstPetFn   OriginalFirstPet{};
SetCapFn     OriginalSetCap{};
ServerUnitFn ServerUnit{};
PlayerDataFn PlayerData{};
UnitGuidFn   UnitGuid{};
ClassIdFn    ClassId{};
CheckStateFn CheckState{};
DataTablesFn DataTables{};

constexpr char SourceName[] = "summon-enhancements";

template <typename T>
auto At(std::uint64_t rva) noexcept -> T {
    return reinterpret_cast<T>(Base + rva);
}

template <typename T>
auto Read(const void* base, std::size_t offset) noexcept -> T {
    T value{};
    std::memcpy(&value, static_cast<const std::uint8_t*>(base) + offset, sizeof(T));
    return value;
}

// ---------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------

enum class PluginState { NotLoaded, DisabledByConfig, BadConfig, UnsupportedBuild, InstallFailed, Running };

PluginState                SaveState{ PluginState::NotLoaded };
std::atomic<bool>          PersistenceActive{};
std::atomic<std::uint64_t> DemonsWritten{};
std::atomic<std::uint64_t> DemonsRefused{};
std::atomic<std::uint64_t> DemonsRestored{};
std::atomic<std::uint64_t> BlocksNotRestored{};
std::atomic<std::uint64_t> CapsRaised{};

// The demon 4FF1A0 must return while one block is written, and how deep the
// current thread is inside a demon block read. Saves and loads run on the
// server thread, so each thread keeps its own.
struct Redirection {
    void* owner{};
    void* demon{};
};
thread_local Redirection Redirect{};
thread_local int         RestoreDepth{};

std::atomic_flag SummaryLock = ATOMIC_FLAG_INIT;
std::string      LastSave;
std::string      LastLoad;

void SetSummary(std::string& target, const std::string& text, bool log) {
    bool changed = false;
    while (SummaryLock.test_and_set(std::memory_order_acquire)) {}
    if (target != text) {
        target = text;
        changed = true;
    }
    SummaryLock.clear(std::memory_order_release);
    if (changed && log) Context->LogInfo(text.c_str());
}

auto GetSummary(const std::string& source) -> std::string {
    while (SummaryLock.test_and_set(std::memory_order_acquire)) {}
    std::string copy = source;
    SummaryLock.clear(std::memory_order_release);
    return copy;
}

// Rows of skills.txt (DataTables), the bound the skills record lookup checks.
constexpr std::size_t SkillRowCountOffset = 0x11B8;

// ===========================================================================
// SUMMONS AND BOUND DEMONS OF ANY CLASS: addresses, witnesses, natives
// ===========================================================================

constexpr std::uint64_t EnterWorldRva    = 0x485350;  // (client, player, game, room, x, y)
constexpr std::uint64_t WarpPetsRva      = 0x53BC60;  // (game, player): every pet to the player
constexpr std::uint64_t ClassCheckRva    = 0x533D38;  // inside the demon serializer 533CE0
constexpr std::uint64_t BindSkillIdRva   = 0x531808;  // mov edx, <Bind Demon id> in the restore 531790
constexpr std::uint64_t BindSkillTailRva = 0x53180D;  // mov rcx,r12 / lea r15d,[rsi-1] / call 33DD40
constexpr std::uint64_t SrvDoReadRva     = 0x43AED3;  // the do handler reads srvdofunc

constexpr std::uint64_t HighestSkillRva  = 0x33DD40;  // (unit, skill, 1) -> skill
constexpr std::uint64_t SkillLevelRva    = 0x33D1E0;  // (unit, skill, 1, 0) -> level, loader thunk
constexpr std::uint64_t PathXRva         = 0x341A20;  // (path) -> x
constexpr std::uint64_t PathYRva         = 0x341A30;  // (path) -> y
constexpr std::uint64_t UnitRoomRva      = 0x34B440;  // (unit) -> active room
constexpr std::uint64_t SkillRecordRva   = 0x97790;   // (context, skill) -> skills record
constexpr std::uint64_t CalcRva          = 0x3B5160;  // (context, owner, calc, skill, level)
constexpr std::uint64_t SummonClassRva   = 0x433800;  // (owner, 0, skill, level, &mode, 0, 0) -> monstats row
constexpr std::uint64_t SummonPetRva     = 0x4350E0;  // (game, args) -> pet
constexpr std::uint64_t CastIdRva        = 0x34B6C0;  // (unit) -> cast id
constexpr std::uint64_t PlayerSlotRva    = 0x34B8B0;  // (unit) -> player slot
constexpr std::uint64_t AssignSlotRva    = 0x58D950;  // (game, unit, 0, slot)
constexpr std::uint64_t GolemSetupRva    = 0x55B040;  // (game, owner, pet, skill, level, cast id)
constexpr std::uint64_t PetLevelRva      = 0x55FB50;  // (game, owner, pet, value, level)
constexpr std::uint64_t SummonStatsRva   = 0x55EF80;  // (game, owner, pet, skill, level, cast id, level override)
constexpr std::uint64_t SkeletonSetupRva = 0x55A5D0;  // (game, owner, pet, element, skill, level)
constexpr std::uint64_t SkeletonPreRva   = 0x55EDB0;  // (game, owner, skill, level)
constexpr std::uint64_t SkeletonPostRva  = 0x55EC60;  // (owner, pet)
constexpr std::uint64_t ClientOfRva      = 0x48FDE0;  // (unit) -> client
constexpr std::uint64_t PetInfoRva       = 0x480AD0;  // (client, pet)
constexpr std::uint64_t AiModeRva        = 0x48B890;  // (game, pet, 2, 0)
constexpr std::uint64_t AiTimerRva       = 0x48B720;  // (game, pet, 2, frame, 0, 0, 0)
constexpr std::uint64_t SetStateRva      = 0x3354C0;  // (unit, state, 1)
constexpr std::uint64_t LinkOwnerRva     = 0x34EF50;  // (pet, owner)

// Skills record and table fields, each one witnessed below.
constexpr std::size_t   SkillSrvDoField      = 0x4E;   // srvdofunc, word
constexpr std::size_t   SkillSummonField     = 0x110;  // summon (monstats row), word
constexpr std::size_t   SkillPetTypeField    = 0x112;  // pettype, signed byte
constexpr std::size_t   SkillSummonModeField = 0x113;  // summode, byte
constexpr std::size_t   SkillPetMaxField     = 0x114;  // petmax calc
constexpr std::size_t   SkillCalcField       = 0x194;  // the calc srvdofunc 16 and 119 read
constexpr std::size_t   MonStatsCountOffset  = 0xF60;  // DataTables
constexpr std::size_t   GameFrameOffset      = 0x170;
constexpr std::size_t   UnitPathOffset       = 0x38;
constexpr std::int32_t  ValkyrieState        = 93;
constexpr std::uint32_t PlayerUnitType       = 0;
constexpr std::int32_t  SrvDoValkyrie        = 16;
constexpr std::int32_t  SrvDoSkeleton        = 31;
constexpr std::int32_t  SrvDoGolem           = 56;
constexpr std::int32_t  SrvDoDruid           = 119;
constexpr std::uint64_t MaximumPetTypes      = 256;
constexpr std::size_t   MaximumPending       = 16;

// 485350: push rbp/rbx/r12/r13/r15 / lea rbp,[rsp-27h] / sub rsp,0B0h. The
// next instruction is the RIP-relative cookie load, so 21 bytes.
constexpr std::uint8_t EnterWorldBytes[]{ 0x40, 0x55, 0x53, 0x41, 0x54, 0x41, 0x55, 0x41, 0x57, 0x48, 0x8D, 0x6C, 0x24, 0xD9, 0x48, 0x81, 0xEC, 0xB0, 0x00, 0x00, 0x00 };
// 53BC60: entry through the call to the pet count 4FF480.
constexpr std::uint8_t WarpPetsBytes[]{ 0x48, 0x89, 0x5C, 0x24, 0x20, 0x55, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B, 0xE9, 0x48, 0x8B, 0xDA, 0x48, 0x8B, 0xCA, 0xE8, 0x08, 0x38, 0xFC, 0xFF };
// 53180D: mov rcx,r12 / lea r15d,[rsi-1] / call 33DD40. The mov edx before
// it carries the Bind Demon id, which the repoint patch rewrites.
constexpr std::uint8_t BindSkillTailBytes[]{ 0x49, 0x8B, 0xCC, 0x44, 0x8D, 0x7E, 0xFF, 0xE8, 0x27, 0xC5, 0xE0, 0xFF };
constexpr std::uint8_t BindSkillOpcode[]{ 0xBA };
// 533D38: cmp eax,7 / jne 5341B4 / lea r9d,[rax-6] / mov rdx,rdi / lea r8d,[rax+0Eh].
constexpr std::uint8_t ClassCheckStock[]{ 0x83, 0xF8, 0x07, 0x0F, 0x85, 0x73, 0x04, 0x00, 0x00, 0x44, 0x8D, 0x48, 0xFA, 0x48, 0x8B, 0xD7, 0x44, 0x8D, 0x40, 0x0E };
// mov r9d,1 / mov rdx,rdi / mov r8d,21 / nop dword [rax+rax]: 4FF1A0 (game,
// player, 21, 1) for every class. 533D4C (mov rcx,r13) follows unchanged.
constexpr std::uint8_t ClassCheckPatched[]{ 0x41, 0xB9, 0x01, 0x00, 0x00, 0x00, 0x48, 0x89, 0xFA, 0x41, 0xB8, 0x15, 0x00, 0x00, 0x00, 0x0F, 0x1F, 0x44, 0x00, 0x00 };
static_assert(sizeof(ClassCheckStock) == 20 && sizeof(ClassCheckPatched) == 20);

// Field witnesses, in the functions whose steps the restore repeats.
constexpr std::uint8_t SrvDoReadBytes[]{ 0x0F, 0xBF, 0x4E, 0x4E };          // 43AED3 movsx ecx,word [rsi+4Eh]
constexpr std::uint8_t MonStatsCountBytes[]{ 0x48, 0x8B, 0xB8, 0x60, 0x0F, 0x00, 0x00 };   // 55C0E3 mov rdi,[rax+0F60h]
constexpr std::uint8_t PetTypeReadBytes[]{ 0x0F, 0xB6, 0xBB, 0x12, 0x01, 0x00, 0x00 };      // 55C236 movzx edi,byte [rbx+112h]
constexpr std::uint8_t SummonModeReadBytes[]{ 0x0F, 0xB6, 0x8B, 0x13, 0x01, 0x00, 0x00 };   // 55C281 movzx ecx,byte [rbx+113h]
constexpr std::uint8_t SummonReadBytes[]{ 0x0F, 0xB7, 0x83, 0x10, 0x01, 0x00, 0x00 };        // 55C2C9 movzx eax,word [rbx+110h]
constexpr std::uint8_t PetMaxReadBytes[]{ 0x44, 0x8B, 0x83, 0x14, 0x01, 0x00, 0x00 };        // 55C2F2 mov r8d,[rbx+114h]
constexpr std::uint8_t CalcReadBytes[]{ 0x44, 0x8B, 0x87, 0x94, 0x01, 0x00, 0x00 };            // 575B4F mov r8d,[rdi+194h]
constexpr std::uint8_t GameFrameBytes[]{ 0x44, 0x8B, 0x8E, 0x70, 0x01, 0x00, 0x00 };          // 5581E1 mov r9d,[rsi+170h]
constexpr std::uint8_t ValkyrieStateBytes[]{ 0xBA, 0x5D, 0x00, 0x00, 0x00 };  // 55820C mov edx,5Dh
// 55E6F5..55E73D: srvdofunc 31 fills the SummonPet arguments (flags 1 at
// +0, owner +8, monster +10h, AI 0 at +14h, mode +18h, x +1Ch, y +20h,
// pettype +24h, petmax +28h) and evaluates petmax.
constexpr std::uint8_t SummonArgsBytes[]{ 0x66, 0x89, 0x45, 0xFF, 0x8B, 0x45, 0xE3, 0x89, 0x45, 0x07, 0x8B, 0x45, 0xCB, 0x89, 0x45, 0x0B, 0x8B, 0x45, 0xCF, 0x89, 0x45, 0x0F, 0x8B, 0x45, 0xE7, 0x89, 0x45, 0x13, 0x48, 0xC7, 0x45, 0xEF, 0x01, 0x00, 0x00, 0x00, 0x66, 0x89, 0x75, 0x01, 0x89, 0x75, 0x1B, 0x4C, 0x89, 0x7D, 0xF7, 0x89, 0x75, 0x03, 0x45, 0x8B, 0x85, 0x14, 0x01, 0x00, 0x00, 0x89, 0x7C, 0x24, 0x20, 0xE8, 0x29, 0x6A, 0xE5, 0xFF, 0x48, 0x8D, 0x55, 0xEF, 0x89, 0x45, 0x17 };

const std::array<Witness, 4> CoreWitnesses{
    W(EnterWorldRva, EnterWorldBytes, "world entry"),
    W(WarpPetsRva, WarpPetsBytes, "pet warp"),
    W(BindSkillTailRva, BindSkillTailBytes, "restore, Bind Demon lookup"),
    W(BindSkillIdRva, BindSkillOpcode, "restore, Bind Demon id load"),
};

const std::array<Witness, 10> SummonWitnesses{
    W(SrvDoReadRva, SrvDoReadBytes, "do handler, srvdofunc field"),
    W(0x55C0E3, MonStatsCountBytes, "srvdofunc 56, monstats count"),
    W(0x55C236, PetTypeReadBytes, "srvdofunc 56, pettype field"),
    W(0x55C281, SummonModeReadBytes, "srvdofunc 56, summode field"),
    W(0x55C2C9, SummonReadBytes, "srvdofunc 56, summon field"),
    W(0x55C2F2, PetMaxReadBytes, "srvdofunc 56, petmax field"),
    W(0x575B4F, CalcReadBytes, "srvdofunc 119, calc field"),
    W(0x5581E1, GameFrameBytes, "srvdofunc 16, game frame"),
    W(0x55820C, ValkyrieStateBytes, "srvdofunc 16, Valkyrie state"),
    W(0x55E6F5, SummonArgsBytes, "srvdofunc 31, SummonPet arguments"),
};

struct CallWitness {
    std::uint64_t site;
    std::uint64_t target;
    const char*   name;
};

const std::array<CallWitness, 5> CoreCalls{ {
    { 0x55C394, UnitRoomRva, "srvdofunc 56, unit room" },
    { 0x531814, HighestSkillRva, "restore, Bind Demon lookup" },
    { 0x531A1B, SkillLevelRva, "restore, Bind Demon level" },
    { 0x53BCC0, PathYRva, "pet warp, player y" },
    { 0x53BCEE, PathXRva, "pet warp, player x" },
} };

const std::array<CallWitness, 30> SummonCalls{ {
    { 0x55C0BC, SkillRecordRva, "srvdofunc 56, skills record" },
    { 0x55C2FE, CalcRva, "srvdofunc 56, petmax" },
    { 0x55C30F, SummonPetRva, "srvdofunc 56, SummonPet" },
    { 0x55C323, CastIdRva, "srvdofunc 56, cast id" },
    { 0x55C343, GolemSetupRva, "srvdofunc 56, golem setup" },
    { 0x55C3B4, ClientOfRva, "srvdofunc 56, owner client" },
    { 0x55C3BF, PetInfoRva, "srvdofunc 56, pet info" },
    { 0x55C3C7, PlayerSlotRva, "srvdofunc 56, player slot" },
    { 0x55C3D8, AssignSlotRva, "srvdofunc 56, slot list" },
    { 0x5759F4, SummonClassRva, "srvdofunc 119, summon class" },
    { 0x575B25, SummonPetRva, "srvdofunc 119, SummonPet" },
    { 0x575B4A, AssignSlotRva, "srvdofunc 119, slot list" },
    { 0x575B68, CalcRva, "srvdofunc 119, calc" },
    { 0x575B89, PetLevelRva, "srvdofunc 119, pet level" },
    { 0x575BB0, SummonStatsRva, "srvdofunc 119, summon stats" },
    { 0x558070, SummonClassRva, "srvdofunc 16, summon class" },
    { 0x558162, SummonPetRva, "srvdofunc 16, SummonPet" },
    { 0x558184, PetLevelRva, "srvdofunc 16, pet level" },
    { 0x5581CA, SummonStatsRva, "srvdofunc 16, summon stats" },
    { 0x5581DC, AiModeRva, "srvdofunc 16, AI mode" },
    { 0x558207, AiTimerRva, "srvdofunc 16, AI timer" },
    { 0x558218, SetStateRva, "srvdofunc 16, Valkyrie state" },
    { 0x558223, LinkOwnerRva, "srvdofunc 16, owner link" },
    { 0x55E49C, SkeletonPreRva, "srvdofunc 31, skeleton pre-step" },
    { 0x55E4D9, SummonClassRva, "srvdofunc 31, summon class" },
    { 0x55E741, SummonPetRva, "srvdofunc 31, SummonPet" },
    { 0x55E762, PetLevelRva, "srvdofunc 31, pet level" },
    { 0x55E77D, SkeletonSetupRva, "srvdofunc 31, skeleton setup" },
    { 0x55E7A2, SummonStatsRva, "srvdofunc 31, summon stats" },
    { 0x55E7AD, SkeletonPostRva, "srvdofunc 31, skeleton post-step" },
} };

// SummonPet's argument block, as 4350E0 reads it.
struct SummonArgs {
    std::uint32_t flags;     // 1 = place at x, y
    std::uint32_t pad0;
    void*         owner;
    std::int16_t  monster;   // monstats row
    std::int16_t  pad1;
    std::int32_t  aiState;
    std::int32_t  mode;
    std::int32_t  x;
    std::int32_t  y;
    std::int32_t  petType;
    std::int32_t  petMax;
    std::int32_t  reserved;
};
static_assert(sizeof(SummonArgs) == 0x30 && offsetof(SummonArgs, owner) == 0x08 && offsetof(SummonArgs, monster) == 0x10
    && offsetof(SummonArgs, aiState) == 0x14 && offsetof(SummonArgs, mode) == 0x18 && offsetof(SummonArgs, x) == 0x1C
    && offsetof(SummonArgs, y) == 0x20 && offsetof(SummonArgs, petType) == 0x24 && offsetof(SummonArgs, petMax) == 0x28);

using HighestSkillFn  = void*(__fastcall*)(void* unit, std::int32_t skill, std::uint8_t includeItems);
using SkillLevelFn    = std::int32_t(__fastcall*)(void* unit, void* skill, std::int32_t mode, std::int32_t flag);
using PathCoordFn     = std::int32_t(__fastcall*)(void* path);
using WarpPetsFn      = void(__fastcall*)(void* game, void* player);
using UnitRoomFn      = void*(__fastcall*)(void* unit);
using EnterWorldFn    = void(__fastcall*)(void* client, void* player, void* game, void* room, std::int32_t x, std::int32_t y);
using SkillRecordFn   = const std::uint8_t*(__fastcall*)(std::uint8_t context, std::int32_t skill);
using CalcFn          = std::int32_t(__fastcall*)(std::uint8_t context, void* owner, std::uint32_t calc, std::int32_t skill,
                            std::int32_t level);
using SummonClassFn   = std::int32_t(__fastcall*)(void* owner, std::int32_t zero, std::int32_t skill, std::int32_t level,
                            std::int32_t* mode, void* unused1, void* unused2);
using SummonPetFn     = void*(__fastcall*)(void* game, SummonArgs* args);
using UnitIntFn       = std::int32_t(__fastcall*)(void* unit);
using AssignSlotFn    = void(__fastcall*)(void* game, void* unit, std::int32_t zero, std::int32_t slot);
using GolemSetupFn    = void(__fastcall*)(void* game, void* owner, void* pet, std::int32_t skill, std::int32_t level,
                            std::int32_t castId);
using PetLevelFn      = void(__fastcall*)(void* game, void* owner, void* pet, std::int32_t value, std::int32_t level);
using SummonStatsFn   = void(__fastcall*)(void* game, void* owner, void* pet, std::int32_t skill, std::int32_t level,
                            std::int32_t castId, std::int32_t levelOverride);
using SkeletonSetupFn = void(__fastcall*)(void* game, void* owner, void* pet, std::int32_t element, std::int32_t skill,
                            std::int32_t level);
using SkeletonPreFn   = void(__fastcall*)(void* game, void* owner, std::int32_t skill, std::int32_t level);
using SkeletonPostFn  = void(__fastcall*)(void* owner, void* pet);
using ClientOfFn      = void*(__fastcall*)(void* unit);
using PetInfoFn       = void(__fastcall*)(void* client, void* pet);
using AiModeFn        = void(__fastcall*)(void* game, void* pet, std::int32_t mode, std::int32_t flag);
using AiTimerFn       = void(__fastcall*)(void* game, void* pet, std::int32_t type, std::int32_t frame, std::int32_t a5,
                            std::int32_t a6, std::int32_t a7);
using SetStateFn      = void(__fastcall*)(void* unit, std::int32_t state, std::uint32_t on);
using LinkOwnerFn     = void(__fastcall*)(void* pet, void* owner);

HighestSkillFn  HighestSkill{};
SkillLevelFn    SkillLevel{};
PathCoordFn     PathX{};
PathCoordFn     PathY{};
WarpPetsFn      WarpPets{};
UnitRoomFn      UnitRoom{};
EnterWorldFn    OriginalEnterWorld{};
SkillRecordFn   SkillRecord{};
CalcFn          Calc{};
SummonClassFn   SummonClass{};
SummonPetFn     SummonPet{};
UnitIntFn       CastId{};
UnitIntFn       PlayerSlot{};
AssignSlotFn    AssignSlot{};
GolemSetupFn    GolemSetup{};
PetLevelFn      PetLevel{};
SummonStatsFn   SummonStats{};
SkeletonSetupFn SkeletonSetup{};
SkeletonPreFn   SkeletonPre{};
SkeletonPostFn  SkeletonPost{};
ClientOfFn      ClientOf{};
PetInfoFn       PetInfo{};
AiModeFn        AiMode{};
AiTimerFn       AiTimer{};
SetStateFn      SetState{};
LinkOwnerFn     LinkOwner{};

PluginState                DemonState{ PluginState::NotLoaded };   // save_every_demon
PluginState                ClassState{ PluginState::NotLoaded };   // demon_all_classes
PluginState                SummonState{ PluginState::NotLoaded };  // save_summons
std::atomic<bool>          MultiDemonActive{};
std::atomic<bool>          AllClassesActive{};
std::atomic<bool>          SummonsActive{};
bool                       ClassPatchApplied{};
std::atomic<bool>          ListReported{};
std::atomic<std::uint64_t> DemonsKept{};
std::atomic<std::uint64_t> DemonsRebuilt{};
std::atomic<std::uint64_t> SummonsWritten{};
std::atomic<std::uint64_t> SummonsLeftOut{};
std::atomic<std::uint64_t> SummonsBack{};
std::atomic<std::uint64_t> SummonsNotBack{};
std::string                LastEntry;

// What a load leaves for the character's next world entry: demon blocks of
// a character without Bind Demon yet, and the saved summons.
struct Pending {
    void*                                  game{};
    std::uint32_t                          player{};
    std::uint32_t                          version{};
    std::vector<std::vector<std::uint8_t>> demons;
    std::vector<SavedSummon>               summons;
};

SRWLOCK              PendingLock = SRWLOCK_INIT;
std::vector<Pending> PendingList;

// A load replaces what the same player left before, and anything left in
// another game.
void StorePending(Pending&& entry) {
    AcquireSRWLockExclusive(&PendingLock);
    PendingList.erase(std::remove_if(PendingList.begin(), PendingList.end(),
        [&](const Pending& old) { return old.game != entry.game || old.player == entry.player; }), PendingList.end());
    if (PendingList.size() >= MaximumPending) PendingList.erase(PendingList.begin());
    PendingList.push_back(std::move(entry));
    ReleaseSRWLockExclusive(&PendingLock);
}

// game null: the entry of that player in whichever game it was left.
auto TakePending(void* game, std::uint32_t player, Pending& out) -> bool {
    bool found = false;
    AcquireSRWLockExclusive(&PendingLock);
    for (auto it = PendingList.begin(); it != PendingList.end(); ++it) {
        if ((game == nullptr || it->game == game) && it->player == player) {
            out = std::move(*it);
            PendingList.erase(it);
            found = true;
            break;
        }
    }
    ReleaseSRWLockExclusive(&PendingLock);
    return found;
}

void ClearPending() {
    AcquireSRWLockExclusive(&PendingLock);
    PendingList.clear();
    ReleaseSRWLockExclusive(&PendingLock);
}

auto PendingCount() -> std::size_t {
    AcquireSRWLockShared(&PendingLock);
    const std::size_t count = PendingList.size();
    ReleaseSRWLockShared(&PendingLock);
    return count;
}

// ---------------------------------------------------------------------------
// Configuration (the TOML doubles as the plugin's documentation)
// ---------------------------------------------------------------------------

Config      Settings{};
std::string AuraListError;    // set when the auras list cannot be read
std::string SummonListError;  // set when the summon_skills list cannot be read

constexpr char DefaultConfigToml[] = R"toml(# Summon Enhancements (celestialrayone.summon-enhancements)
#
# Formerly "Bind Demon Persistence and Auras". Four features, each with its
# own switch below. Read when the game starts: restart the game after
# editing.
#
# ---------------------------------------------------------------------------
# 1. SAVE EVERY BOUND DEMON (save_every_demon)
# ---------------------------------------------------------------------------
# Every bound demon is saved with the character and comes back in the next
# game, not only the first one.
#
# The game's own save rules still decide whether a demon is saved:
#   - the character is a Warlock (feature 2 lifts this rule)
#   - the demon is a pet of pettype.txt row 21 (binddemon)
#   - the demon has states.txt row 207 (bind_demon)
#   - the monster has a row in monpet.txt
#   - the character save still has room for it
# Without the plugin the game only looks at the first binddemon pet and
# saves at most that one. The plugin goes through all of them.
#
# Coming back into a game:
#   Bind Demon's petmax is worked out while the character is still loading,
#   before items and +skills count, so it can come out lower than the number
#   of saved demons, and the game would remove the extra demons right away.
#   While the character loads, the plugin raises the cap just enough to fit
#   every saved demon. Once you are in game, your normal petmax applies the
#   next time you bind a demon.
#
# Save file:
#   Each demon is stored as its own copy of the game's normal demon block.
#   If the plugin is ever removed, the game reads the first block and
#   ignores the rest, so the character still loads with its first demon.
#
# Game limit, not changed by this plugin: the game only puts a saved demon's
# monster mods back when the monster was a champion, unique, superunique or
# minion. A plain monster comes back without its calc2-calc5 mods, Aura
# Enchanted included, so it has no aura after a relog.
#
# ---------------------------------------------------------------------------
# 2. BOUND DEMONS OF ANY CLASS (demon_all_classes)
# ---------------------------------------------------------------------------
# The game only saves a bound demon when the character is a Warlock. With
# this on, every class saves its bound demons by the same rules.
#
# Coming back into a game: the game only rebuilds a saved demon when the
# character has Bind Demon while the save loads, and item stats do not count
# yet at that point. A character that has Bind Demon only from items (the
# Arion's Augury set bonus, for example) would lose its demons there. For
# such a character the plugin keeps the demons and rebuilds them with the
# game's own code when the character enters the world, where items count.
# They appear at your side.
# If the character has no Bind Demon yet at that moment, the plugin tries
# again when D2RLoader reports you ready, and on every later world entry in
# that game (after changing act, for example). Leaving the game drops
# demons that never got rebuilt, as the game itself would at load.
#
# ---------------------------------------------------------------------------
# 3. SAVE SUMMONS (save_summons, summon_skills)
# ---------------------------------------------------------------------------
# The summons of the skills in summon_skills are saved with the character
# and come back when you enter the world.
#
# How a summon comes back:
#   - It is summoned again at your feet by the skill's own summon code, at
#     your current level in that skill, items and +skills included. It
#     starts with full life and has the stats the skill gives it now.
#   - Nothing is spent: no mana, no corpse, no cooldown.
#   - The skill's petmax applies. If petmax is now lower than the number of
#     saved summons, the game keeps as many as petmax allows.
#   - A summon whose skill the character does not have yet (an item skill
#     that does not count yet) is tried again the same way as a kept bound
#     demon, see feature 2. Leaving the game drops it.
#   - A summon is not brought back when the skill's petmax works out to 0
#     now.
#
# Which skills can be listed: skills.txt rows with a pettype and a summon
# monster, made by srvdofunc 16 (Valkyrie), 31 (Raise Skeleton, Raise
# Skeletal Mage), 56 (golems, Warlock demons, Raise Kornac, Skeleton King)
# or 119 (druid summons, Shadow Warrior, Shadow Master, Holy Defender).
# A summon of another srvdofunc is named in the log and not brought back.
# Bind Demon does not belong here: its demons use features 1 and 2.
#
# How a saved summon is matched to a listed skill: by the skill's pettype.
# When several listed skills share a pettype (the golems share one, the
# Warlock demons share another), by the skill's summon monster.
#
# Raise Skeleton and Raise Skeletal Mage come back as if raised from a
# corpse with no montype.txt element, which is every corpse in ESR today
# (the element column is empty).
#
# Save file: the summons are stored after the bound demon blocks, 4 bytes
# per summon. Without the plugin the game ignores them.
#
# ---------------------------------------------------------------------------
# 4. AURA LIST (aura_list, auras)
# ---------------------------------------------------------------------------
# Picks the aura of a bound demon from the auras list instead of
# D2RLoader's built-in Aura Enchanted lists.
#
# Which demons get an aura: only demons with the Aura Enchanted monster mod
# (monumod.txt row 30):
#   - a new bind whose Bind Demon skills.txt calc2-calc5 add mod 30;
#   - champions, uniques, superuniques and minions that already had Aura
#     Enchanted before they were bound.
# A demon is rolled when it is bound and again when it is restored from the
# save. It works with both values of d2rloader.toml
# d2rcore.game_rules.aura_enchanted_selection ("3.1" and "3.2").
#
# How the roll works:
#   - Every entry the demon qualifies for is equally likely. List a skill
#     twice to make it twice as likely.
#   - The pick comes from the demon's monster seed. A restored demon keeps the
#     seed it was saved with, so it comes back with the same aura.
#   - Level = monster level / divisor, kept between 1 and 99. The monster
#     level is the demon's level stat at the end of the bind.
#   - The aura becomes the demon's active aura, the same way Aura Enchanted
#     sets it.
#   - If no entry qualifies (empty list, or every min_level is above the
#     demon's level), the plugin gives no aura.
#
# Each entry:
#   skill      skills.txt *Id of the aura (required, must be an aura skill)
#   divisor    level = monster level / divisor (default 6)
#   min_level  the entry only counts for demons of at least this monster
#              level (default 0)
#
# The default list below is D2RLoader's own "3.2" Bind Demon list, so nothing
# changes until you edit it. The names are the vanilla skills.txt rows.
# Not affected: monsters that are not bound demons, and the herald extra aura.
#
# ---------------------------------------------------------------------------
# Console
# ---------------------------------------------------------------------------
#   summonsave    what the last save wrote, what the last load restored or
#                 kept, what came back on entering the world, and why a
#                 demon or a summon was left out
#   summonauras   the aura list, the counters and the last rolls
# The same lines go to this plugin's log, along with every aura roll.

# Set to false to turn the whole plugin off.
enabled = true

# Save and restore every bound demon (feature 1).
save_every_demon = true

# Bound demons of every class (feature 2).
demon_all_classes = true

# Save and restore the summons of the skills below (feature 3).
save_summons = true

# skills.txt *Id of every skill whose summons are saved. The names are
# ESR's skills.txt rows.
summon_skills = [
    440,  # Valkyrie
    493,  # Raise Skeleton
    504,  # Raise Skeletal Mage
    498,  # Clay Golem
    506,  # Blood Golem
    510,  # Iron Golem
    514,  # Fire Golem
    518,  # Summon Skeleton King
    552,  # Summon Holy Defender
    593,  # Raven
    594,  # Summon Spirit Wolf
    600,  # Summon Dire Wolf
    612,  # Spirit of Nature
    626,  # Summon Grizzly
    641,  # Shadow Warrior
    644,  # Shadow Master
    663,  # Summon Goatman
    675,  # Summon Tainted
    680,  # Summon Defiler
    688,  # Summon Abyss Knight
    385,  # Raise Kornac
]

# Pick bound demons' aura from the list below (feature 4). false leaves the
# aura to D2RLoader's aura_enchanted_selection.
aura_list = true

auras = [
    { skill = 113, divisor = 6 },                  # Concentration
    { skill = 115, divisor = 6 },                  # Vigor
    { skill = 103, divisor = 10 },                 # Thorns
    { skill = 365, divisor = 7 },                  # Holy Freeze (MonHolyFreeze)
    { skill = 122, divisor = 8 },                  # Fanaticism
    { skill = 122, divisor = 8 },                  # Fanaticism
    { skill = 115, divisor = 6, min_level = 20 },  # Vigor
]
)toml";

void ReadConfiguration() {
    if (!Context->EnsureConfig(DefaultConfigToml)) {
        Context->LogWarn("SummonEnhancements: the config file could not be created; using defaults.");
    }
    std::vector<char> buffer(16384, '\0');
    std::uint32_t     required = 0;
    bool read = Context->ReadConfig(buffer.data(), static_cast<std::uint32_t>(buffer.size()), &required);
    if (!read && required > buffer.size() && required <= (1u << 20)) {
        buffer.assign(static_cast<std::size_t>(required) + 1, '\0');
        read = Context->ReadConfig(buffer.data(), static_cast<std::uint32_t>(buffer.size()), nullptr);
    }
    if (!read) Context->LogWarn("SummonEnhancements: the config file could not be read; using defaults.");
    buffer.back() = '\0';
    const std::string_view text = read ? std::string_view(buffer.data()) : std::string_view(DefaultConfigToml);

    std::vector<std::string> warnings;
    ParseConfig(text, Settings, warnings, AuraListError, SummonListError);
    for (const auto& warning : warnings) D2RL::LogWarnF(Context, "SummonEnhancements: %s.", warning.c_str());
    if ((AuraListError.empty() && !Settings.haveAuras) || (SummonListError.empty() && !Settings.haveSummonSkills)) {
        Config                   defaults{};
        std::vector<std::string> ignoredWarnings;
        std::string              ignoredAuraError;
        std::string              ignoredSummonError;
        ParseConfig(DefaultConfigToml, defaults, ignoredWarnings, ignoredAuraError, ignoredSummonError);
        if (AuraListError.empty() && !Settings.haveAuras) {
            Settings.auras = defaults.auras;
            Context->LogWarn("SummonEnhancements: the TOML has no auras list; the default list is used.");
        }
        if (SummonListError.empty() && !Settings.haveSummonSkills) {
            Settings.summonSkills = defaults.summonSkills;
            Context->LogWarn("SummonEnhancements: the TOML has no summon_skills list; the default list is used.");
        }
    }
}


// ---------------------------------------------------------------------------
// Pet list
// ---------------------------------------------------------------------------

auto BindBucket(void* game, void* owner) -> const std::uint8_t* {
    if (game == nullptr || owner == nullptr) return nullptr;
    const std::uint8_t* tables = DataTables(Read<std::uint8_t>(game, GameContextOffset));
    if (tables == nullptr) return nullptr;
    if (static_cast<std::uint64_t>(BindPetType) >= Read<std::uint64_t>(tables, PetTypeCountOffset)) return nullptr;
    void* playerData = PlayerData(owner);
    if (playerData == nullptr) return nullptr;
    void* container = Read<void*>(playerData, PetContainerOffset);
    if (container == nullptr) return nullptr;
    auto* buckets = Read<const std::uint8_t*>(container, 0);
    if (buckets == nullptr) return nullptr;
    return buckets + BucketStride * static_cast<std::size_t>(BindPetType);
}

auto BindCount(void* game, void* owner) -> std::int32_t {
    const std::uint8_t* bucket = BindBucket(game, owner);
    return bucket == nullptr ? -1 : Read<std::int32_t>(bucket, BucketCountOffset);
}

void CollectDemons(void* game, void* owner, std::vector<void*>& demons) {
    const std::uint8_t* bucket = BindBucket(game, owner);
    if (bucket == nullptr) return;
    auto* node = Read<const std::uint8_t*>(bucket, 0);
    for (std::size_t visited = 0; node != nullptr && visited < MaximumNodes; ++visited) {
        void* demon = ServerUnit(game, MonsterUnitType, Read<std::uint32_t>(node, NodeGuidOffset));
        if (demon != nullptr && demons.size() < MaximumDemons
                && std::find(demons.begin(), demons.end(), demon) == demons.end()) {
            demons.push_back(demon);
        }
        node = Read<const std::uint8_t*>(node, NodeNextOffset);
    }
}

auto HoldsDemon(const std::uint8_t* block) noexcept -> bool {
    return block[0] == 0x01 && block[1] == 0x00 && block[2] == 0x6C && block[3] == 0x66 && block[4] == 0x01
        && block[5] == 0x00;
}

// ---------------------------------------------------------------------------
// Hooks
// ---------------------------------------------------------------------------

// 4FF1A0: while one demon block is written, the serializer's "first binddemon
// pet" is the demon being written.
void* __fastcall HookedFirstPet(void* game, void* owner, std::int32_t petType, std::int32_t includeFlagged) {
    if (Redirect.demon != nullptr && petType == BindPetType && includeFlagged == 1 && owner == Redirect.owner) {
        return Redirect.demon;
    }
    return OriginalFirstPet(game, owner, petType, includeFlagged);
}

// 501830: while a save loads, the Bind Demon cap fits the demons already
// restored plus the one being added.
std::uint64_t __fastcall HookedSetCap(void* game, void* owner, std::int32_t petType, std::int32_t petMax,
        std::uint8_t enforce) {
    if (RestoreDepth > 0 && petType == BindPetType && PersistenceActive.load(std::memory_order_relaxed)) {
        const std::int32_t count = BindCount(game, owner);
        if (count >= 0 && petMax < count + 1) {
            petMax = count + 1;
            CapsRaised.fetch_add(1, std::memory_order_relaxed);
        }
    }
    return OriginalSetCap(game, owner, petType, petMax, enforce);
}

// ---------------------------------------------------------------------------
// Bound demons of any class
// ---------------------------------------------------------------------------

// The Bind Demon id the restore 531790 looks up, read live so the repoint
// patch is followed. 0 when the instruction is not the expected mov edx.
auto LiveBindDemonId() noexcept -> std::int32_t {
    const auto* code = reinterpret_cast<const std::uint8_t*>(Base + BindSkillIdRva);
    if (code[0] != BindSkillOpcode[0]) return 0;
    return Read<std::int32_t>(code, 1);
}

// The same check 531790 makes before it rebuilds a demon.
auto HasBindDemon(void* player) noexcept -> bool {
    const std::int32_t id = LiveBindDemonId();
    return id > 0 && HighestSkill(player, id, 1) != nullptr;
}

// One demon block: kept for the world entry when the character has no Bind
// Demon yet, else read by the native reader.
auto ReadDemonBlock(void* game, void* player, std::uint8_t** cursor, std::uint8_t* end, std::uint32_t version, bool keep,
        Pending& pending, std::size_t& blocks) -> std::uint32_t {
    const bool holds = end - *cursor >= BlockHeaderSize && HoldsDemon(*cursor);
    if (holds) ++blocks;
    if (holds && keep && pending.demons.size() < MaximumDemons) {
        const std::ptrdiff_t length = DemonBlockLength(*cursor, end);
        if (length > 0) {
            pending.demons.emplace_back(*cursor, *cursor + length);
            *cursor += length;
            DemonsKept.fetch_add(1, std::memory_order_relaxed);
            return 0;
        }
    }
    return OriginalReadBlock(game, player, cursor, end, version);
}

// ---------------------------------------------------------------------------
// Summons
// ---------------------------------------------------------------------------

struct ListedSkill {
    std::int32_t skill{};
    std::int32_t petType{};
    std::int32_t summon{};
};

auto IsListed(std::int32_t skill) -> bool {
    return std::find(Settings.summonSkills.begin(), Settings.summonSkills.end(), skill) != Settings.summonSkills.end();
}

// The listed skills that can be saved: skills.txt rows with a pettype other
// than binddemon. The others are named in the log once.
void ResolveListedSkills(std::uint8_t context, std::vector<ListedSkill>& out) {
    out.clear();
    const std::uint8_t* tables = DataTables(context);
    if (tables == nullptr) return;
    const auto  skills   = Read<std::uint64_t>(tables, SkillRowCountOffset);
    const auto  petTypes = Read<std::uint64_t>(tables, PetTypeCountOffset);
    std::string unusable;
    for (const std::int32_t skill : Settings.summonSkills) {
        const std::uint8_t* record  = static_cast<std::uint64_t>(skill) < skills ? SkillRecord(context, skill) : nullptr;
        const std::int32_t  petType = record != nullptr ? static_cast<std::int8_t>(record[SkillPetTypeField]) : -1;
        const char*         why     = record == nullptr ? "not a skills.txt row"
                                    : petType < 0 || static_cast<std::uint64_t>(petType) >= petTypes ? "no pettype"
                                    : petType == BindPetType ? "binddemon pettype, saved as a bound demon"
                                    : nullptr;
        if (why != nullptr) {
            unusable += " " + std::to_string(skill) + " (" + why + ")";
            continue;
        }
        out.push_back({ skill, petType, Read<std::int16_t>(record, SkillSummonField) });
    }
    if (!unusable.empty() && !ListReported.exchange(true)) {
        D2RL::LogWarnF(Context, "SummonEnhancements: summon_skills entries that are never saved:%s.", unusable.c_str());
    }
}

// Every pet of the owner that belongs to a listed skill.
void CollectSummons(void* game, void* owner, std::vector<SavedSummon>& out, std::size_t& unmatched) {
    const std::uint8_t context = Read<std::uint8_t>(game, GameContextOffset);
    std::vector<ListedSkill> listed;
    ResolveListedSkills(context, listed);
    if (listed.empty()) return;
    const std::uint8_t* tables = DataTables(context);
    void* playerData = tables != nullptr ? PlayerData(owner) : nullptr;
    void* container  = playerData != nullptr ? Read<void*>(playerData, PetContainerOffset) : nullptr;
    const auto* buckets = container != nullptr ? Read<const std::uint8_t*>(container, 0) : nullptr;
    if (buckets == nullptr) return;
    const std::uint64_t types = std::min(Read<std::uint64_t>(tables, PetTypeCountOffset), MaximumPetTypes);
    std::vector<const ListedSkill*> candidates;
    for (std::uint64_t type = 0; type < types; ++type) {
        candidates.clear();
        for (const auto& entry : listed) {
            if (static_cast<std::uint64_t>(entry.petType) == type) candidates.push_back(&entry);
        }
        if (candidates.empty()) continue;
        const auto* node = Read<const std::uint8_t*>(buckets + BucketStride * static_cast<std::size_t>(type), 0);
        for (std::size_t visited = 0; node != nullptr && visited < MaximumNodes; ++visited) {
            void* pet = ServerUnit(game, MonsterUnitType, Read<std::uint32_t>(node, NodeGuidOffset));
            node = Read<const std::uint8_t*>(node, NodeNextOffset);
            if (pet == nullptr || out.size() >= MaximumSummons) continue;
            const std::int32_t monster = ClassId(pet, SourceName, __LINE__);
            const ListedSkill* match   = candidates.size() == 1 ? candidates.front() : nullptr;
            for (std::size_t i = 0; match == nullptr && i < candidates.size(); ++i) {
                if (candidates[i]->summon == monster) match = candidates[i];
            }
            if (match == nullptr || monster < 0 || monster > 0xFFFF) {
                ++unmatched;
                continue;
            }
            out.push_back({ static_cast<std::uint16_t>(match->skill), static_cast<std::uint16_t>(monster) });
        }
    }
}

// The summon block after the demon blocks, as many summons as fit.
void WriteSummons(void* game, void* player, std::uint8_t** cursor, std::uint8_t* end, std::string& summary) {
    std::vector<SavedSummon> summons;
    std::size_t              unmatched = 0;
    CollectSummons(game, player, summons, unmatched);
    const std::string note = unmatched == 0 ? std::string()
        : " " + std::to_string(unmatched) + " summon(s) matched no listed skill (listed skills share the pettype and "
          "none of them summons that monster).";
    if (summons.empty()) {
        summary += " No listed summon." + note;
        return;
    }
    const std::size_t fit = SummonsThatFit(summons.size(), end - *cursor);
    if (fit == 0) {
        SummonsLeftOut.fetch_add(summons.size(), std::memory_order_relaxed);
        summary += " " + std::to_string(summons.size()) + " summon(s) left out: the save is full." + note;
        return;
    }
    *cursor += EncodeSummons(*cursor, summons, fit);
    SummonsWritten.fetch_add(fit, std::memory_order_relaxed);
    summary += " " + std::to_string(fit) + " summon(s) written (skill x count: " + CountBySkill(summons, fit) + ").";
    if (fit < summons.size()) {
        SummonsLeftOut.fetch_add(summons.size() - fit, std::memory_order_relaxed);
        summary += " " + std::to_string(summons.size() - fit) + " left out: the save is full.";
    }
    summary += note;
}

// Back: made again. Wait: the character has no level in the skill yet,
// which items can still bring; tried again later in the same game. Fail:
// cannot come back, dropped with the reason.
enum class Outcome { Back, Wait, Fail };

// Makes one saved summon again the way its srvdofunc makes it, at the
// player's feet.
auto RestoreSummon(void* game, void* owner, const SavedSummon& saved, const char*& why) -> Outcome {
    const std::uint8_t  context = Read<std::uint8_t>(game, GameContextOffset);
    const std::uint8_t* tables  = DataTables(context);
    const std::int32_t  skill   = saved.skill;
    if (tables == nullptr || static_cast<std::uint64_t>(skill) >= Read<std::uint64_t>(tables, SkillRowCountOffset)) {
        why = "not a skills.txt row";
        return Outcome::Fail;
    }
    if (!IsListed(skill)) {
        why = "no longer in summon_skills";
        return Outcome::Fail;
    }
    const std::uint8_t* record = SkillRecord(context, skill);
    if (record == nullptr) {
        why = "no skills record";
        return Outcome::Fail;
    }
    void* owned = HighestSkill(owner, skill, 1);
    if (owned == nullptr) {
        why = "the character does not have the skill";
        return Outcome::Wait;
    }
    const std::int32_t level = SkillLevel(owner, owned, 1, 0);
    if (level < 1) {
        why = "the skill is at level 0";
        return Outcome::Wait;
    }
    const std::int32_t petType = static_cast<std::int8_t>(record[SkillPetTypeField]);
    if (petType < 0 || static_cast<std::uint64_t>(petType) >= Read<std::uint64_t>(tables, PetTypeCountOffset)) {
        why = "the skill has no pettype";
        return Outcome::Fail;
    }
    const std::int32_t petMax = Calc(context, owner, Read<std::uint32_t>(record, SkillPetMaxField), skill, level);
    if (petMax < 1) {
        why = "petmax works out below 1";
        return Outcome::Fail;
    }
    void* path = Read<void*>(owner, UnitPathOffset);
    if (path == nullptr) {
        why = "the character has no position";
        return Outcome::Fail;
    }

    SummonArgs args{};
    args.flags   = 1;
    args.owner   = owner;
    args.x       = PathX(path);
    args.y       = PathY(path);
    args.petType = petType;
    args.petMax  = petMax;

    const std::int32_t srvdo = Read<std::int16_t>(record, SkillSrvDoField);
    if (srvdo == SrvDoGolem) {
        const std::int32_t monster = Read<std::int16_t>(record, SkillSummonField);
        if (monster < 0 || static_cast<std::uint64_t>(monster) >= Read<std::uint64_t>(tables, MonStatsCountOffset)) {
            why = "the skill has no summon monster";
            return Outcome::Fail;
        }
        const std::uint8_t mode = record[SkillSummonModeField];
        args.monster = static_cast<std::int16_t>(monster);
        args.mode    = mode < 16 ? mode : 1;
        void* pet = SummonPet(game, &args);
        if (pet == nullptr) {
            why = "the game found no room for it";
            return Outcome::Fail;
        }
        GolemSetup(game, owner, pet, skill, level, CastId(owner));
        if (void* client = ClientOf(owner)) PetInfo(client, pet);
        AssignSlot(game, pet, 0, PlayerSlot(owner));
        return Outcome::Back;
    }
    if (srvdo != SrvDoValkyrie && srvdo != SrvDoSkeleton && srvdo != SrvDoDruid) {
        why = "its srvdofunc is not 16, 31, 56 or 119";
        return Outcome::Fail;
    }
    if (srvdo == SrvDoSkeleton) SkeletonPre(game, owner, skill, level);
    std::int32_t       mode    = 0;
    const std::int32_t monster = SummonClass(owner, 0, skill, level, &mode, nullptr, nullptr);
    if (monster < 0 || monster > 0x7FFF) {
        why = "the skill names no summon monster";
        return Outcome::Fail;
    }
    args.monster = static_cast<std::int16_t>(monster);
    args.mode    = mode;
    void* pet = SummonPet(game, &args);
    if (pet == nullptr) {
        why = "the game found no room for it";
        return Outcome::Fail;
    }
    if (srvdo == SrvDoDruid) {
        AssignSlot(game, pet, 0, PlayerSlot(owner));
        const std::int32_t value = Calc(context, owner, Read<std::uint32_t>(record, SkillCalcField), skill, level);
        PetLevel(game, owner, pet, value < 1 ? 1 : value, level);
        SummonStats(game, owner, pet, skill, level, CastId(owner), 0);
        return Outcome::Back;
    }
    PetLevel(game, owner, pet, 0, level);
    if (srvdo == SrvDoValkyrie) {
        const std::int32_t petLevel = Calc(context, owner, Read<std::uint32_t>(record, SkillCalcField), skill, level);
        SummonStats(game, owner, pet, skill, level, CastId(owner), petLevel);
        AiMode(game, pet, 2, 0);
        AiTimer(game, pet, 2, Read<std::int32_t>(game, GameFrameOffset) + 20, 0, 0, 0);
        SetState(pet, ValkyrieState, 1);
        LinkOwner(pet, owner);
        AssignSlot(game, pet, 0, PlayerSlot(owner));
        return Outcome::Back;
    }
    SkeletonSetup(game, owner, pet, 0, skill, level);
    SummonStats(game, owner, pet, skill, level, CastId(owner), 0);
    SkeletonPost(owner, pet);
    AssignSlot(game, pet, 0, PlayerSlot(owner));
    return Outcome::Back;
}

void RestoreSummons(void* game, void* player, const Pending& pending, std::string& summary, Pending& later) {
    if (pending.summons.empty()) return;
    if (!SummonsActive.load(std::memory_order_relaxed)) {
        summary += " " + std::to_string(pending.summons.size()) + " saved summon(s) not brought back: save_summons is off.";
        return;
    }
    std::size_t back = 0;
    std::vector<std::pair<std::string, std::size_t>> failures;
    for (const SavedSummon& saved : pending.summons) {
        const char*   why     = "";
        const Outcome outcome = RestoreSummon(game, player, saved, why);
        if (outcome == Outcome::Back) {
            ++back;
            continue;
        }
        if (outcome == Outcome::Wait) later.summons.push_back(saved);
        const std::string key = "skill " + std::to_string(saved.skill) + ": " + why;
        auto it = std::find_if(failures.begin(), failures.end(),
            [&](const std::pair<std::string, std::size_t>& failure) { return failure.first == key; });
        if (it == failures.end()) {
            failures.emplace_back(key, 1);
        } else {
            ++it->second;
        }
    }
    const std::size_t waiting = later.summons.size();
    SummonsBack.fetch_add(back, std::memory_order_relaxed);
    SummonsNotBack.fetch_add(pending.summons.size() - back - waiting, std::memory_order_relaxed);
    summary += " " + std::to_string(back) + " of " + std::to_string(pending.summons.size()) + " summon(s) back.";
    for (const auto& failure : failures) {
        summary += " Not back, " + failure.first + " (x" + std::to_string(failure.second) + ")"
            + (failure.first.find("does not have") != std::string::npos || failure.first.find("level 0") != std::string::npos
                ? ", tried again later in this game." : ".");
    }
}

// The demon blocks kept at load, read by the native reader now that the
// character's items count, then placed by the game's own pet warp.
void RebuildDemons(void* game, void* player, Pending& pending, std::string& summary, Pending& later) {
    if (pending.demons.empty()) return;
    const std::size_t kept = pending.demons.size();
    if (!HasBindDemon(player)) {
        summary += " " + std::to_string(kept) + " kept bound demon(s) still wait: the character has no Bind Demon yet.";
        later.demons = std::move(pending.demons);
        return;
    }
    ++RestoreDepth;
    const std::int32_t before     = BindCount(game, player);
    std::size_t        unreadable = 0;
    for (auto& block : pending.demons) {
        std::uint8_t* cursor = block.data();
        if (OriginalReadBlock(game, player, &cursor, block.data() + block.size(), pending.version) != 0) ++unreadable;
    }
    --RestoreDepth;
    const std::int32_t after   = BindCount(game, player);
    const std::int32_t rebuilt = before >= 0 && after >= before ? after - before : 0;
    DemonsRebuilt.fetch_add(static_cast<std::uint64_t>(rebuilt), std::memory_order_relaxed);
    if (rebuilt > 0) WarpPets(game, player);
    summary += " " + std::to_string(rebuilt) + " of " + std::to_string(kept) + " kept bound demon(s) rebuilt.";
    if (unreadable > 0) summary += " " + std::to_string(unreadable) + " block(s) could not be read.";
    if (static_cast<std::size_t>(rebuilt) + unreadable < kept) {
        summary += " The rest were refused by the game: the monster, the bind or its saved stats.";
    }
}

// ---------------------------------------------------------------------------
// Save, load and world entry hooks
// ---------------------------------------------------------------------------

// Every bound demon, written through the native writer one at a time.
auto WriteEveryDemon(void* game, void* player, std::uint8_t** cursor, std::uint8_t* end, std::string& summary)
        -> std::uint32_t {
    std::vector<void*> demons;
    CollectDemons(game, player, demons);
    if (demons.empty()) {
        summary += " no bound demon on the character.";
        return OriginalWriteBlock(game, player, cursor, end);
    }
    std::uint8_t* const begin   = *cursor;
    std::size_t         written = 0;
    std::string         refused;
    for (void* demon : demons) {
        std::uint8_t* const start = *cursor;
        Redirect = { player, demon };
        const std::uint32_t status = OriginalWriteBlock(game, player, cursor, end);
        Redirect = {};
        if (status != 0) {
            *cursor = start;
            break;
        }
        if (*cursor - start >= BlockHeaderSize && HoldsDemon(start)) {
            ++written;
            continue;
        }
        *cursor = start;
        DemonsRefused.fetch_add(1, std::memory_order_relaxed);
        const bool classAllowed = ClassPatchApplied || ClassId(player, SourceName, __LINE__) == WarlockClass;
        const bool bound        = CheckState(demon, BindDemonState) != 0;
        char line[256];
        std::snprintf(line, sizeof(line), " Monster %d (unit %u) left out: class %s, bind_demon state %s%s.",
            ClassId(demon, SourceName, __LINE__), UnitGuid(demon, SourceName, __LINE__),
            classAllowed ? "allowed" : "not a Warlock", bound ? "yes" : "no",
            classAllowed && bound ? ", so it has no monpet.txt row or the save is full" : "");
        refused += line;
    }
    if (written == 0) {
        *cursor = begin;
        summary += " " + std::to_string(demons.size()) + " bound demon(s), none written." + refused;
        return OriginalWriteBlock(game, player, cursor, end);
    }
    DemonsWritten.fetch_add(written, std::memory_order_relaxed);
    summary += " " + std::to_string(demons.size()) + " bound demon(s), " + std::to_string(written) + " written." + refused;
    return 0;
}

// 533AE0: the bound demon blocks, then the summon block.
std::uint32_t __fastcall HookedWriteBlock(void* game, void* player, std::uint8_t** cursor, std::uint8_t* end) {
    if (!PersistenceActive.load(std::memory_order_relaxed) || game == nullptr || player == nullptr || cursor == nullptr
            || *cursor == nullptr || Read<std::uint8_t>(game, GameContextOffset) == ClassicGame) {
        return OriginalWriteBlock(game, player, cursor, end);
    }
    std::string   summary = "SummonEnhancements: save,";
    std::uint32_t status  = 0;
    if (MultiDemonActive.load(std::memory_order_relaxed)) {
        status = WriteEveryDemon(game, player, cursor, end, summary);
    } else {
        std::uint8_t* const start = *cursor;
        status = OriginalWriteBlock(game, player, cursor, end);
        summary += status == 0 && *cursor - start >= BlockHeaderSize && HoldsDemon(start)
            ? " the game wrote its one bound demon." : " the game wrote no bound demon.";
    }
    if (status == 0 && SummonsActive.load(std::memory_order_relaxed)) WriteSummons(game, player, cursor, end, summary);
    SetSummary(LastSave, summary, true);
    return status;
}

// 531510: the native block, every following block that holds a demon, then
// the summon block. The demons of a character without Bind Demon and the
// summons wait for the character to enter the world.
std::uint32_t __fastcall HookedReadBlock(void* game, void* player, std::uint8_t** cursor, std::uint8_t* end,
        std::uint32_t version) {
    if (!PersistenceActive.load(std::memory_order_relaxed) || game == nullptr || player == nullptr || cursor == nullptr
            || *cursor == nullptr) {
        return OriginalReadBlock(game, player, cursor, end, version);
    }
    const bool modern = version > OldestModernVersion;
    const bool keep   = modern && AllClassesActive.load(std::memory_order_relaxed) && !HasBindDemon(player);
    Pending pending{};
    pending.game    = game;
    pending.player  = UnitGuid(player, SourceName, __LINE__);
    pending.version = version;

    ++RestoreDepth;
    const std::int32_t  before = BindCount(game, player);
    std::size_t         blocks = 0;
    std::string         problems;
    const std::uint32_t status = ReadDemonBlock(game, player, cursor, end, version, keep, pending, blocks);
    if (status == 0 && modern && MultiDemonActive.load(std::memory_order_relaxed)) {
        while (*cursor != nullptr && end - *cursor >= BlockHeaderSize && HoldsDemon(*cursor)) {
            std::uint8_t* const start = *cursor;
            const std::uint32_t extra = ReadDemonBlock(game, player, cursor, end, version, keep, pending, blocks);
            if (extra != 0 || *cursor <= start) {
                BlocksNotRestored.fetch_add(1, std::memory_order_relaxed);
                problems += " Block " + std::to_string(blocks) + " could not be read (status " + std::to_string(extra)
                    + "); the blocks after it were skipped.";
                break;
            }
        }
    }
    bool summonBlock = false;
    if (status == 0 && modern && *cursor != nullptr && SummonsActive.load(std::memory_order_relaxed)) {
        // Demon blocks left unread (save_every_demon off) are stepped over.
        std::uint8_t* scan = *cursor;
        while (scan != nullptr && end - scan >= BlockHeaderSize && HoldsDemon(scan)) {
            const std::ptrdiff_t length = DemonBlockLength(scan, end);
            scan = length > 0 ? scan + length : nullptr;
        }
        const std::ptrdiff_t size = scan != nullptr ? DecodeSummons(scan, end, pending.summons) : 0;
        if (size > 0) {
            *cursor     = scan + size;
            summonBlock = true;
        }
    }
    --RestoreDepth;

    const std::int32_t after    = BindCount(game, player);
    const std::int32_t restored = before >= 0 && after >= before ? after - before : 0;
    DemonsRestored.fetch_add(static_cast<std::uint64_t>(restored), std::memory_order_relaxed);
    const std::size_t kept = pending.demons.size();
    std::string summary = "SummonEnhancements: load,";
    if (blocks > 0) {
        summary += " " + std::to_string(blocks) + " saved demon(s), " + std::to_string(restored) + " restored";
        if (kept > 0) summary += ", " + std::to_string(kept) + " kept for entering the world (no Bind Demon on the character yet)";
        summary += ".";
        const std::size_t accounted = static_cast<std::size_t>(restored) + kept;
        if (accounted < blocks) {
            summary += " " + std::to_string(blocks - accounted)
                + " demon(s) were not recreated: the game refused the monster, the bind or its saved stats.";
        }
    } else {
        summary += " no saved demon.";
    }
    summary += problems;
    if (summonBlock) {
        summary += " " + std::to_string(pending.summons.size()) + " saved summon(s) wait for entering the world (skill x count: "
            + CountBySkill(pending.summons, pending.summons.size()) + ").";
    }
    if (!pending.demons.empty() || !pending.summons.empty()) StorePending(std::move(pending));
    if (blocks > 0 || summonBlock) SetSummary(LastLoad, summary, true);
    return status;
}

// The kept demons and the saved summons of a player in the world. What still
// has to wait is kept for the next try in the same game.
void BringBack(void* game, void* player, Pending& pending, const char* moment) {
    Pending later{};
    later.game    = pending.game;
    later.player  = pending.player;
    later.version = pending.version;
    std::string summary = std::string("SummonEnhancements: ") + moment + ",";
    RebuildDemons(game, player, pending, summary, later);
    RestoreSummons(game, player, pending, summary, later);
    if (!later.demons.empty() || !later.summons.empty()) StorePending(std::move(later));
    SetSummary(LastEntry, summary, true);
}

// D2RLoader's "local player ready", on the game thread: a second try for
// whatever still waits, once the client has finished entering.
void __cdecl RetryOnGameThread(const D2RL::PluginContext*, void* userData) noexcept {
    if (!PersistenceActive.load(std::memory_order_relaxed)) return;
    const auto guid = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(userData));
    Pending pending{};
    if (!TakePending(nullptr, guid, pending)) return;
    void* player = ServerUnit(pending.game, PlayerUnitType, guid);
    if (player == nullptr || UnitRoom(player) == nullptr) {
        StorePending(std::move(pending));
        return;
    }
    BringBack(pending.game, player, pending, "player ready");
}

// 485350: after the native call has placed the player in the world, the
// kept demons and the saved summons of that player come back.
void __fastcall HookedEnterWorld(void* client, void* player, void* game, void* room, std::int32_t x, std::int32_t y) {
    OriginalEnterWorld(client, player, game, room, x, y);
    if (!PersistenceActive.load(std::memory_order_relaxed) || game == nullptr || player == nullptr
            || Read<std::uint32_t>(player, 0) != PlayerUnitType) {
        return;
    }
    Pending pending{};
    if (!TakePending(game, UnitGuid(player, SourceName, __LINE__), pending)) return;
    BringBack(game, player, pending, "entering the world");
}

// ===========================================================================
// AURA LIST
// ===========================================================================

// ---------------------------------------------------------------------------
// Aura byte witnesses (D2RLoader 1.3.1 process image, D2RCore 1.3.1-beta)
// ---------------------------------------------------------------------------

// 51EF90 at 51F29B: non-elite test, new name seed from the unit RNG, call
// D2RCore!PrepareBindDemonRules (thunk 3E2B47E) at 51F2C6, then
// cmp byte ptr [rsp+330h],0 (newBind) at 51F2CB.
constexpr std::uint64_t PrepareWindowRva = 0x51F29B;
constexpr std::uint8_t  PrepareWindow[]{
    0x48, 0x8B, 0xCB, 0xE8, 0x3D, 0xAF, 0xE2, 0xFF, 0x8B, 0x08, 0x4C, 0x69,
    0xC1, 0xC5, 0x90, 0xC6, 0x6A, 0x8B, 0x48, 0x04, 0x4C, 0x03, 0xC1, 0x49,
    0x8B, 0xC8, 0x44, 0x89, 0x00, 0x48, 0xC1, 0xE9, 0x20, 0x41, 0x0F, 0xB7,
    0xD0, 0x89, 0x48, 0x04, 0x48, 0x8B, 0xCB, 0xE8, 0xB3, 0xC1, 0x90, 0x03,
    0x80, 0xBC, 0x24, 0x30, 0x03, 0x00, 0x00, 0x00,
};
// The calc2-calc5 loop: evaluate, range check 1..45, HasUMod, then ApplyUMod
// 4995E0 (game, demon, mod, 1) at 51F333.
constexpr std::uint64_t ApplyUModWindowRva = 0x51F2F0;
constexpr std::uint8_t  ApplyUModWindow[]{
    0x45, 0x8B, 0x06, 0x45, 0x8B, 0xCC, 0x0F, 0xB6, 0x8E, 0x06, 0x01, 0x00,
    0x00, 0x49, 0x8B, 0xD7, 0x44, 0x89, 0x6C, 0x24, 0x20, 0xE8, 0x56, 0x5E,
    0xE9, 0xFF, 0x8B, 0xF8, 0x8D, 0x48, 0xFF, 0x83, 0xF9, 0x2C, 0x77, 0x31,
    0x40, 0x0F, 0xB6, 0xD7, 0x48, 0x8B, 0xCB, 0xE8, 0x40, 0xB4, 0xF7, 0xFF,
    0x84, 0xC0, 0x75, 0x14, 0x41, 0xB9, 0x01, 0x00, 0x00, 0x00, 0x44, 0x8B,
    0xC7, 0x48, 0x8B, 0xD3, 0x48, 0x8B, 0xCE, 0xE8, 0xA8, 0xA2, 0xF7, 0xFF,
    0x48, 0xFF, 0xC5, 0x49, 0x83, 0xC6, 0x04, 0x48, 0x83, 0xFD, 0x04, 0x7E,
    0xAB,
};
// Summon setup 55EF80, then D2RCore!ApplyBindDemonBoundStats (owner, demon)
// through thunk 3E2B484 at 51F3AE.
constexpr std::uint64_t BoundStatsWindowRva = 0x51F37F;
constexpr std::uint8_t  BoundStatsWindow[]{
    0x8B, 0x84, 0x24, 0x28, 0x03, 0x00, 0x00, 0x45, 0x8B, 0xCC, 0xC7, 0x44,
    0x24, 0x30, 0x00, 0x00, 0x00, 0x00, 0x4C, 0x8B, 0xC3, 0x89, 0x44, 0x24,
    0x28, 0x49, 0x8B, 0xD7, 0x48, 0x8B, 0xCE, 0x44, 0x89, 0x6C, 0x24, 0x20,
    0xE8, 0xD8, 0xFB, 0x03, 0x00, 0x48, 0x8B, 0xD3, 0x49, 0x8B, 0xCF, 0xE8,
    0xD1, 0xC0, 0x90, 0x03,
};
// Restore 531790: saved flags nonzero -> 49B3D0 (demon, seed, flags,
// superunique, saved mods) at 5319FF.
constexpr std::uint64_t RestoreModsWindowRva = 0x5319E0;
constexpr std::uint8_t  RestoreModsWindow[]{
    0x44, 0x0F, 0xB7, 0x47, 0x08, 0x66, 0x45, 0x85, 0xC0, 0x74, 0x19, 0x0F,
    0xB7, 0x57, 0x06, 0x48, 0x8D, 0x47, 0x50, 0x45, 0x0F, 0xB7, 0xCF, 0x48,
    0x89, 0x44, 0x24, 0x20, 0x48, 0x8B, 0xCE, 0xE8, 0xCC, 0x99, 0xF6, 0xFF,
};

constexpr std::uint64_t GetNameSeedRva = 0x38E370;  // (unit) -> word
constexpr std::uint8_t  GetNameSeedEntry[]{
    0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B, 0xD9, 0x48, 0x85, 0xC9,
    0x74, 0x0A, 0xE8, 0x4D, 0xD6, 0xFB, 0xFF, 0x83, 0xF8, 0x01, 0x74, 0x19,
};
constexpr std::uint64_t GetStatRva = 0x2F5020;  // (unit, stat, layer), loader thunk
constexpr std::uint8_t  GetStatEntry[]{
    0xFF, 0x25, 0xF2, 0x51, 0xB3, 0x03, 0x90, 0x90, 0x90, 0x90,
};
constexpr std::uint64_t SetSkillRva = 0x433910;  // (unit, skill, level, mode)
constexpr std::uint8_t  SetSkillEntry[]{
    0x48, 0x89, 0x5C, 0x24, 0x18, 0x55, 0x56, 0x41, 0x54, 0x41, 0x56, 0x41,
    0x57, 0x48, 0x83, 0xEC, 0x50, 0x45, 0x8B, 0xE1, 0x4C, 0x63, 0xF2, 0x45,
    0x8B, 0xF8, 0x48, 0x8B, 0xE9,
};
constexpr std::uint64_t SelectSkillRva = 0x438A70;  // (unit, left, skill, owner)
constexpr std::uint8_t  SelectSkillEntry[]{
    0x41, 0x83, 0xF8, 0x05, 0x0F, 0x84, 0x28, 0x02, 0x00, 0x00, 0x44, 0x89,
    0x4C, 0x24, 0x20, 0x55, 0x41, 0x56, 0x41, 0x57, 0x48, 0x83, 0xEC, 0x40,
};
// Skills record lookup: calls DataTables 300A90 and bounds the id with
// cmp rbx,[rax+11B8h].
constexpr std::uint64_t SkillsRecordRva  = 0x97790;
constexpr std::size_t   SkillCountOffset = 0x11B8;
constexpr std::uint8_t  SkillsRecordEntry[]{
    0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x74, 0x24, 0x20, 0x57, 0x48,
    0x83, 0xEC, 0x30, 0x48, 0x63, 0xF2, 0xE8, 0xE9, 0x92, 0x26, 0x00, 0x48,
    0x8B, 0xF8, 0x48, 0x8B, 0xDE, 0x85, 0xF6, 0x78, 0x09, 0x48, 0x3B, 0x98,
    0xB8, 0x11, 0x00, 0x00, 0x72, 0x18,
};

// D2RCore!ApplyAuraEnchanted: prologue, argument saves and the unit type
// test. Checked in memory, not through the loader.
constexpr std::uint8_t CoreAuraEntry[]{
    0x55, 0x41, 0x57, 0x41, 0x56, 0x41, 0x55, 0x41, 0x54, 0x56, 0x57, 0x53,
    0x48, 0x81, 0xEC, 0x48, 0x01, 0x00, 0x00, 0x48, 0x8D, 0xAC, 0x24, 0x80,
    0x00, 0x00, 0x00, 0x48, 0xC7, 0x85, 0xC0, 0x00, 0x00, 0x00, 0xFE, 0xFF,
    0xFF, 0xFF, 0x48, 0x85, 0xC9, 0x0F, 0x84, 0x5F, 0x01, 0x00, 0x00, 0x89,
    0x95, 0x9C, 0x00, 0x00, 0x00, 0x44, 0x89, 0x85, 0xBC, 0x00, 0x00, 0x00,
    0x48, 0x89, 0x8D, 0xB0, 0x00, 0x00, 0x00, 0x83, 0x39, 0x01, 0x0F, 0x85,
    0x76, 0x04, 0x00, 0x00,
};
constexpr std::size_t CoreAuraStolen = 19;  // 8 pushes + sub rsp,148h

// Relay stub prefix that hands newBind to the seed relay:
// movzx r8d, byte ptr [rsp+338h].
constexpr std::uint8_t NewBindLoad[]{
    0x44, 0x0F, 0xB6, 0x84, 0x24, 0x38, 0x03, 0x00, 0x00,
};

constexpr std::size_t  UnitClassOffset   = 0x04;
constexpr std::size_t  UnitIdOffset      = 0x08;
constexpr std::size_t  MonsterDataOffset = 0x10;
constexpr std::size_t  DataContextOffset = 0x1BD;
constexpr std::size_t  ModListOffset     = 0x20;
constexpr std::size_t  ModListLength     = 9;
constexpr std::uint8_t AuraEnchantedMod  = 30;
constexpr std::uint32_t LevelStat        = 12;

static_assert(sizeof(PrepareWindow) == 0x38 && PrepareWindow[0x2B] == 0xE8 && PrepareWindow[0x30] == 0x80);
static_assert(ApplyUModWindow[0x43] == 0xE8 && BoundStatsWindow[0x2F] == 0xE8 && RestoreModsWindow[0x1F] == 0xE8);
static_assert(SkillsRecordEntry[0x12] == 0xE8 && SkillsRecordEntry[0x24] == 0xB8 && SkillsRecordEntry[0x25] == 0x11);
static_assert(CoreAuraEntry[12] == 0x48 && CoreAuraEntry[13] == 0x81 && CoreAuraEntry[14] == 0xEC);
static_assert(sizeof(NewBindLoad) == 9);

const std::array<Witness, 9> AuraWitnesses{
    W(PrepareWindowRva, PrepareWindow, "bind conversion, PrepareBindDemonRules call"),
    W(ApplyUModWindowRva, ApplyUModWindow, "bind conversion, calc2-calc5 loop"),
    W(BoundStatsWindowRva, BoundStatsWindow, "bind conversion, ApplyBindDemonBoundStats call"),
    W(RestoreModsWindowRva, RestoreModsWindow, "restore, saved mods call"),
    W(GetNameSeedRva, GetNameSeedEntry, "name seed"),
    W(GetStatRva, GetStatEntry, "stat reader"),
    W(SetSkillRva, SetSkillEntry, "set skill"),
    W(SelectSkillRva, SelectSkillEntry, "select skill"),
    W(SkillsRecordRva, SkillsRecordEntry, "skills record lookup"),
};

// ---------------------------------------------------------------------------
// Aura natives and state
// ---------------------------------------------------------------------------

using PrepareFn     = void(__fastcall*)(void* demon, std::uint32_t seed);
using ApplyUModFn   = std::uint64_t(__fastcall*)(void* game, void* unit, std::uint32_t mod, std::uint32_t flag);
using BoundStatsFn  = void(__fastcall*)(void* owner, void* demon);
using RestoreModsFn = void*(__fastcall*)(void* unit, std::uint32_t seed, std::uint32_t flags, std::uint32_t superUnique,
                          const std::uint8_t* mods);
using AuraFn        = void(__fastcall*)(void* unit, std::uint32_t mod, std::uint32_t flag);
using GetNameSeedFn = std::uint16_t(__fastcall*)(void* unit);
using GetStatFn     = std::int32_t(__fastcall*)(void* unit, std::uint32_t stat, std::uint32_t layer);
using SetSkillFn    = void(__fastcall*)(void* unit, std::int32_t skill, std::int32_t level, std::int32_t mode);
using SelectSkillFn = void(__fastcall*)(void* unit, std::int32_t left, std::int32_t skill, std::int32_t owner);

PrepareFn     OriginalPrepare{};
ApplyUModFn   OriginalApplyUMod{};
BoundStatsFn  OriginalBoundStats{};
RestoreModsFn OriginalRestoreMods{};
AuraFn        OriginalAura{};
GetNameSeedFn GetNameSeed{};
GetStatFn     GetStat{};
SetSkillFn    SetSkill{};
SelectSkillFn SelectSkill{};

PluginState       AuraState{ PluginState::NotLoaded };
std::atomic<bool> AuraActive{};

// The demon being bound or restored on this thread.
struct BindScope {
    void*       demon{};
    const char* moment{};
    bool        rolled{};
};
thread_local BindScope Scope{};

std::atomic<std::uint64_t> RollsApplied{};
std::atomic<std::uint64_t> RollsEmpty{};
std::atomic<std::uint64_t> RollsBadSkill{};
std::atomic<std::uint64_t> SeedsKept{};
std::atomic<std::uint64_t> NativeRollsReplaced{};

struct RollRecord {
    std::uint32_t unitId{};
    std::uint32_t classId{};
    std::int32_t  monsterLevel{};
    std::uint16_t seed{};
    std::int32_t  skill{ -1 };
    std::int32_t  level{};
    const char*   moment{ "" };
    const char*   result{ "" };
};
constexpr std::size_t               HistorySize = 8;
std::array<RollRecord, HistorySize> History{};
std::size_t                         HistoryNext{};
std::size_t                         HistoryCount{};
SRWLOCK                             HistoryLock = SRWLOCK_INIT;

void Remember(const RollRecord& record) noexcept {
    AcquireSRWLockExclusive(&HistoryLock);
    History[HistoryNext] = record;
    HistoryNext          = (HistoryNext + 1) % HistorySize;
    if (HistoryCount < HistorySize) ++HistoryCount;
    ReleaseSRWLockExclusive(&HistoryLock);
}

// MonsterData of a monster unit, null for anything else.
auto MonsterData(const void* unit) noexcept -> const std::uint8_t* {
    if (unit == nullptr || Read<std::uint32_t>(unit, 0) != MonsterUnitType) return nullptr;
    return Read<const std::uint8_t*>(unit, MonsterDataOffset);
}

auto HasAuraEnchanted(const void* unit) noexcept -> bool {
    const std::uint8_t* data = MonsterData(unit);
    if (data == nullptr) return false;
    for (std::size_t i = 0; i < ModListLength; ++i) {
        if (data[ModListOffset + i] == AuraEnchantedMod) return true;
    }
    return false;
}

auto SkillExists(const void* unit, std::int32_t skill) noexcept -> bool {
    if (skill < 0) return false;
    const std::uint8_t* tables = DataTables(Read<std::uint8_t>(unit, DataContextOffset));
    if (tables == nullptr) return false;
    return static_cast<std::uint64_t>(skill) < Read<std::uint64_t>(tables, SkillCountOffset);
}

// The roll that replaces Aura Enchanted for a bound demon.
void RollFromList(void* unit, const char* moment) noexcept {
    if (MonsterData(unit) == nullptr) return;
    RollRecord record{};
    record.unitId       = Read<std::uint32_t>(unit, UnitIdOffset);
    record.classId      = Read<std::uint32_t>(unit, UnitClassOffset);
    record.seed         = GetNameSeed(unit);
    record.monsterLevel = GetStat(unit, LevelStat, 0);
    record.moment       = moment;

    AuraEntry entry{};
    if (!PickEntry(Settings.auras, record.seed, record.monsterLevel, entry)) {
        record.result = "no entry qualifies, no aura";
        RollsEmpty.fetch_add(1, std::memory_order_relaxed);
        Remember(record);
        D2RL::LogInfoF(Context, "SummonEnhancements (auras): %s, demon %u (monstats row %u, level %d): no list entry qualifies, no aura.",
            moment, record.unitId, record.classId, record.monsterLevel);
        return;
    }
    record.skill = entry.skill;
    record.level = AuraLevel(record.monsterLevel, entry.divisor);
    if (!SkillExists(unit, entry.skill)) {
        record.result = "skill id is not in skills.txt";
        RollsBadSkill.fetch_add(1, std::memory_order_relaxed);
        Remember(record);
        D2RL::LogErrorF(Context, "SummonEnhancements (auras): %s, demon %u: skill %d is not a skills.txt row; no aura was set. Fix the TOML.",
            moment, record.unitId, entry.skill);
        return;
    }
    SetSkill(unit, entry.skill, record.level, 1);
    SelectSkill(unit, 0, entry.skill, -1);
    record.result = "set";
    RollsApplied.fetch_add(1, std::memory_order_relaxed);
    Remember(record);
    D2RL::LogInfoF(Context, "SummonEnhancements (auras): %s, demon %u (monstats row %u, level %d, seed %u): skill %d at level %d.", moment,
        record.unitId, record.classId, record.monsterLevel, static_cast<unsigned>(record.seed), entry.skill, record.level);
}

// ---------------------------------------------------------------------------
// Aura hooks
// ---------------------------------------------------------------------------

// D2RCore!ApplyAuraEnchanted: the demon marked on this thread rolls from the
// list, every other unit runs the original.
void __fastcall HookedApplyAuraEnchanted(void* unit, std::uint32_t mod, std::uint32_t flag) noexcept {
    if (unit != nullptr && unit == Scope.demon && AuraActive.load(std::memory_order_relaxed)) {
        if (flag != 0) {
            RollFromList(unit, Scope.moment);
            NativeRollsReplaced.fetch_add(1, std::memory_order_relaxed);
        }
        Scope.rolled = true;
        return;
    }
    OriginalAura(unit, mod, flag);
}

// 51F2C6: a restored demon with mod 30 keeps the seed it was saved with.
void __fastcall RelayPrepare(void* demon, std::uint32_t seed, std::uint32_t newBind) noexcept {
    if (AuraActive.load(std::memory_order_relaxed) && (newBind & 0xFF) == 0 && HasAuraEnchanted(demon)) {
        seed = GetNameSeed(demon);
        SeedsKept.fetch_add(1, std::memory_order_relaxed);
    }
    OriginalPrepare(demon, seed);
}

// 51F333: the calc2-calc5 mods of a new bind.
auto __fastcall RelayApplyUMod(void* game, void* unit, std::uint32_t mod, std::uint32_t flag) noexcept -> std::uint64_t {
    const BindScope saved = Scope;
    Scope = BindScope{ unit, "new bind, calc2-calc5 mods", false };
    const std::uint64_t result = OriginalApplyUMod(game, unit, mod, flag);
    Scope = saved;
    return result;
}

// 51F3AE: the end of every bind. With "3.1" D2RCore does not roll here, so
// the plugin does, which gives every bound demon with mod 30 a list aura.
void __fastcall RelayBoundStats(void* owner, void* demon) noexcept {
    const BindScope saved = Scope;
    Scope = BindScope{ demon, "end of bind", false };
    OriginalBoundStats(owner, demon);
    const bool rolled = Scope.rolled;
    Scope = saved;
    if (!rolled && AuraActive.load(std::memory_order_relaxed) && HasAuraEnchanted(demon)) {
        RollFromList(demon, "end of bind");
    }
}

// 5319FF: the saved mods of a restored demon.
auto __fastcall RelayRestoreMods(void* unit, std::uint32_t seed, std::uint32_t flags, std::uint32_t superUnique,
    const std::uint8_t* mods) noexcept -> void* {
    const BindScope saved = Scope;
    Scope = BindScope{ unit, "restore from the save", false };
    void* result = OriginalRestoreMods(unit, seed, flags, superUnique, mods);
    Scope = saved;
    return result;
}

// ---------------------------------------------------------------------------
// Aura install
// ---------------------------------------------------------------------------

struct CallSite {
    const char*                 name;
    std::uint64_t               windowRva;
    const std::uint8_t*         window;
    std::uint64_t               callRva;
    std::uint64_t               targetRva;
    std::size_t                 stubOffset;
    bool                        loadsNewBind;
    void*                       relay;
    std::array<std::uint8_t, 5> stock{};
    std::array<std::uint8_t, 5> written{};
    bool                        applied{};
};

std::array<CallSite, 4> Sites{ {
    { "PrepareBindDemonRules call 51F2C6", PrepareWindowRva, PrepareWindow, 0x51F2C6, 0x3E2B47E, 0x00, true,
      reinterpret_cast<void*>(&RelayPrepare) },
    { "ApplyUMod call 51F333", ApplyUModWindowRva, ApplyUModWindow, 0x51F333, 0x4995E0, 0x40, false,
      reinterpret_cast<void*>(&RelayApplyUMod) },
    { "ApplyBindDemonBoundStats call 51F3AE", BoundStatsWindowRva, BoundStatsWindow, 0x51F3AE, 0x3E2B484,
      0x80, false, reinterpret_cast<void*>(&RelayBoundStats) },
    { "restore mods call 5319FF", RestoreModsWindowRva, RestoreModsWindow, 0x5319FF, 0x49B3D0, 0xC0,
      false, reinterpret_cast<void*>(&RelayRestoreMods) },
} };

constexpr std::size_t RelayPageBytes = 0x1000;
std::uint8_t*         RelayPage{};

struct CoreDetour {
    std::uint8_t*                target{};
    std::uint8_t*                trampoline{};
    std::array<std::uint8_t, 32> saved{};
    std::size_t                  length{};
    bool                         active{};
};
CoreDetour AuraDetour{};

void WriteAbsoluteJump(std::uint8_t* at, const void* destination) noexcept {
    static constexpr std::uint8_t JumpIndirect[6]{ 0xFF, 0x25, 0x00, 0x00, 0x00, 0x00 };  // jmp [rip+0]
    std::memcpy(at, JumpIndirect, sizeof(JumpIndirect));
    const auto address = static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(destination));
    std::memcpy(at + sizeof(JumpIndirect), &address, sizeof(address));
}

auto InstallCoreDetour(CoreDetour& detour, std::uint8_t* target, std::size_t stolen, const void* hook) noexcept -> bool {
    if (stolen < 14 || stolen > detour.saved.size()) return false;
    auto* trampoline = static_cast<std::uint8_t*>(VirtualAlloc(nullptr, 64, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
    if (trampoline == nullptr) return false;
    std::memset(trampoline, 0xCC, 64);
    std::memcpy(trampoline, target, stolen);
    WriteAbsoluteJump(trampoline + stolen, target + stolen);
    DWORD previous = 0;
    if (!VirtualProtect(trampoline, 64, PAGE_EXECUTE_READ, &previous)) {
        VirtualFree(trampoline, 0, MEM_RELEASE);
        return false;
    }
    FlushInstructionCache(GetCurrentProcess(), trampoline, 64);

    std::array<std::uint8_t, 32> patch{};
    patch.fill(0xCC);
    WriteAbsoluteJump(patch.data(), hook);
    if (!VirtualProtect(target, stolen, PAGE_EXECUTE_READWRITE, &previous)) {
        VirtualFree(trampoline, 0, MEM_RELEASE);
        return false;
    }
    std::memcpy(detour.saved.data(), target, stolen);
    std::memcpy(target, patch.data(), stolen);
    DWORD ignored = 0;
    VirtualProtect(target, stolen, previous, &ignored);
    FlushInstructionCache(GetCurrentProcess(), target, stolen);
    if (std::memcmp(target, patch.data(), stolen) != 0) return false;

    detour.target     = target;
    detour.trampoline = trampoline;
    detour.length     = stolen;
    detour.active     = true;
    return true;
}

void RemoveCoreDetour(CoreDetour& detour) noexcept {
    if (!detour.active) return;
    DWORD previous = 0;
    if (VirtualProtect(detour.target, detour.length, PAGE_EXECUTE_READWRITE, &previous)) {
        std::memcpy(detour.target, detour.saved.data(), detour.length);
        DWORD ignored = 0;
        VirtualProtect(detour.target, detour.length, previous, &ignored);
        FlushInstructionCache(GetCurrentProcess(), detour.target, detour.length);
        detour.active = false;
    }
}

// A page above the image within rel32 reach of every call site.
auto AllocateRelayPage() noexcept -> std::uint8_t* {
    const auto* image     = reinterpret_cast<const std::uint8_t*>(Base);
    const auto  ntOffset  = static_cast<std::size_t>(Read<std::uint32_t>(image, 0x3C));
    const auto  imageSize = Read<std::uint32_t>(image, ntOffset + 0x50);
    SYSTEM_INFO systemInfo{};
    GetSystemInfo(&systemInfo);
    const std::uintptr_t granularity = systemInfo.dwAllocationGranularity != 0 ? systemInfo.dwAllocationGranularity : 0x10000;
    const auto alignUp = [granularity](std::uintptr_t value) noexcept { return (value + granularity - 1) & ~(granularity - 1); };

    std::uint64_t lowestSite = Sites[0].callRva;
    for (const auto& site : Sites) lowestSite = site.callRva < lowestSite ? site.callRva : lowestSite;
    const std::uintptr_t limit   = Base + static_cast<std::uintptr_t>(lowestSite) + 0x7FF00000ULL;
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
    for (const auto& site : Sites) {
        std::uint8_t* stub = RelayPage + site.stubOffset;
        if (site.loadsNewBind) {
            std::memcpy(stub, NewBindLoad, sizeof(NewBindLoad));
            stub += sizeof(NewBindLoad);
        }
        WriteAbsoluteJump(stub, site.relay);
    }
    DWORD previous = 0;
    if (!VirtualProtect(RelayPage, RelayPageBytes, PAGE_EXECUTE_READ, &previous)) return false;
    FlushInstructionCache(GetCurrentProcess(), RelayPage, RelayPageBytes);
    return true;
}

void RestoreCallSites() noexcept {
    for (auto& site : Sites) {
        if (site.applied && Context->PatchBytes(site.callRva, site.written.data(), 5, site.stock.data(), 5)) site.applied = false;
    }
}

auto PatchCallSites() noexcept -> bool {
    for (auto& site : Sites) {
        const auto from = Base + static_cast<std::uintptr_t>(site.callRva) + 5;
        const auto to   = reinterpret_cast<std::uintptr_t>(RelayPage) + site.stubOffset;
        const auto rel  = static_cast<std::int64_t>(to) - static_cast<std::int64_t>(from);
        if (rel < INT32_MIN || rel > INT32_MAX) {
            D2RL::LogErrorF(Context, "SummonEnhancements (auras): the relay page is out of reach of the %s.", site.name);
            RestoreCallSites();
            return false;
        }
        site.written[0]  = 0xE8;
        const auto rel32 = static_cast<std::int32_t>(rel);
        std::memcpy(site.written.data() + 1, &rel32, sizeof(rel32));
        if (!Context->PatchBytes(site.callRva, site.stock.data(), 5, site.written.data(), 5)) {
            D2RL::LogErrorF(Context, "SummonEnhancements (auras): D2RLoader refused the patch on the %s (another plugin may own it).", site.name);
            RestoreCallSites();
            return false;
        }
        site.applied = true;
        if (std::memcmp(reinterpret_cast<const void*>(Base + static_cast<std::uintptr_t>(site.callRva)), site.written.data(), 5) != 0) {
            D2RL::LogErrorF(Context, "SummonEnhancements (auras): the %s does not hold the written bytes.", site.name);
            RestoreCallSites();
            return false;
        }
    }
    return true;
}

auto VerifyAura() noexcept -> bool {
    bool ok = true;
    for (const Witness& witness : AuraWitnesses) {
        if (!Context->CheckExpectedBytes(witness.rva, witness.bytes, witness.size)) {
            D2RL::LogErrorF(Context, "SummonEnhancements (auras): %s at RVA 0x%llX does not match.", witness.name,
                static_cast<unsigned long long>(witness.rva));
            ok = false;
        }
    }
    for (auto& site : Sites) {
        const auto offset = static_cast<std::size_t>(site.callRva - site.windowRva);
        std::memcpy(site.stock.data(), site.window + offset, 5);
        std::int32_t rel32 = 0;
        std::memcpy(&rel32, site.stock.data() + 1, sizeof(rel32));
        if (site.stock[0] != 0xE8 || site.callRva + 5 + static_cast<std::uint64_t>(static_cast<std::int64_t>(rel32)) != site.targetRva) {
            D2RL::LogErrorF(Context, "SummonEnhancements (auras): the %s does not call the expected target.", site.name);
            ok = false;
        }
    }
    return ok;
}

auto LocateCoreAura() noexcept -> std::uint8_t* {
    const HMODULE core = GetModuleHandleW(L"D2RCore.dll");
    if (core == nullptr) {
        Context->LogError("SummonEnhancements (auras): D2RCore.dll is not loaded.");
        return nullptr;
    }
    auto* entry = reinterpret_cast<std::uint8_t*>(reinterpret_cast<void*>(GetProcAddress(core, "ApplyAuraEnchanted")));
    if (entry == nullptr) {
        Context->LogError("SummonEnhancements (auras): D2RCore does not export ApplyAuraEnchanted.");
        return nullptr;
    }
    MEMORY_BASIC_INFORMATION info{};
    if (VirtualQuery(entry, &info, sizeof(info)) != sizeof(info) || info.State != MEM_COMMIT
        || reinterpret_cast<std::uintptr_t>(entry) + sizeof(CoreAuraEntry)
               > reinterpret_cast<std::uintptr_t>(info.BaseAddress) + info.RegionSize
        || std::memcmp(entry, CoreAuraEntry, sizeof(CoreAuraEntry)) != 0) {
        Context->LogError("SummonEnhancements (auras): D2RCore!ApplyAuraEnchanted is not the D2RLoader 1.3.1 code (or another plugin hooked it).");
        return nullptr;
    }
    return entry;
}

void InstallAuraList() noexcept {
    if (!Settings.auraList) {
        AuraState = PluginState::DisabledByConfig;
        Context->LogInfo("SummonEnhancements (auras): aura_list = false; D2RLoader picks bound demon auras.");
        return;
    }
    if (!AuraListError.empty()) {
        AuraState = PluginState::BadConfig;
        D2RL::LogErrorF(Context, "SummonEnhancements (auras): the auras list could not be read (%s); the aura list is off.",
            AuraListError.c_str());
        return;
    }
    std::uint8_t* coreAura = LocateCoreAura();
    if (coreAura == nullptr || !VerifyAura()) {
        AuraState = PluginState::UnsupportedBuild;
        Context->LogError("SummonEnhancements (auras): this D2R or D2RCore build does not match; the aura list is off.");
        return;
    }
    OriginalPrepare     = At<PrepareFn>(Sites[0].targetRva);
    OriginalApplyUMod   = At<ApplyUModFn>(Sites[1].targetRva);
    OriginalBoundStats  = At<BoundStatsFn>(Sites[2].targetRva);
    OriginalRestoreMods = At<RestoreModsFn>(Sites[3].targetRva);
    GetNameSeed         = At<GetNameSeedFn>(GetNameSeedRva);
    GetStat             = At<GetStatFn>(GetStatRva);
    SetSkill            = At<SetSkillFn>(SetSkillRva);
    SelectSkill         = At<SelectSkillFn>(SelectSkillRva);
    DataTables          = At<DataTablesFn>(DataTablesRva);
    if (!BuildRelayPage()) {
        AuraState = PluginState::InstallFailed;
        Context->LogError("SummonEnhancements (auras): no executable page within reach of the game image; the aura list is off.");
        return;
    }
    if (!InstallCoreDetour(AuraDetour, coreAura, CoreAuraStolen, reinterpret_cast<const void*>(&HookedApplyAuraEnchanted))) {
        AuraState = PluginState::InstallFailed;
        Context->LogError("SummonEnhancements (auras): D2RCore!ApplyAuraEnchanted could not be hooked; the aura list is off.");
        return;
    }
    OriginalAura = reinterpret_cast<AuraFn>(reinterpret_cast<void*>(AuraDetour.trampoline));
    AuraActive.store(true);
    if (!PatchCallSites()) {
        AuraActive.store(false);
        RemoveCoreDetour(AuraDetour);
        AuraState = PluginState::InstallFailed;
        return;
    }
    AuraState = PluginState::Running;
    D2RL::LogInfoF(Context, "SummonEnhancements (auras): active with %zu list entries (D2RCore detour + 4 call relays).", Settings.auras.size());
}

// ---------------------------------------------------------------------------
// Install
// ---------------------------------------------------------------------------

auto MakeCall(std::uint64_t site, std::uint64_t target) noexcept -> std::array<std::uint8_t, 5> {
    std::array<std::uint8_t, 5> bytes{ 0xE8, 0, 0, 0, 0 };
    const auto rel = static_cast<std::int32_t>(static_cast<std::int64_t>(target) - static_cast<std::int64_t>(site + 5));
    std::memcpy(bytes.data() + 1, &rel, sizeof(rel));
    return bytes;
}

template <std::size_t N>
auto VerifyWitnesses(const std::array<Witness, N>& witnesses, const char* group) -> bool {
    bool ok = true;
    for (const Witness& witness : witnesses) {
        if (!Context->CheckExpectedBytes(witness.rva, witness.bytes, witness.size)) {
            D2RL::LogErrorF(Context, "SummonEnhancements: %s: %s at RVA 0x%llX does not match.", group, witness.name,
                static_cast<unsigned long long>(witness.rva));
            ok = false;
        }
    }
    return ok;
}

template <std::size_t N>
auto VerifyCalls(const std::array<CallWitness, N>& calls, const char* group) -> bool {
    bool ok = true;
    for (const CallWitness& call : calls) {
        const auto bytes = MakeCall(call.site, call.target);
        if (!Context->CheckExpectedBytes(call.site, bytes.data(), static_cast<std::uint32_t>(bytes.size()))) {
            D2RL::LogErrorF(Context, "SummonEnhancements: %s: the call at RVA 0x%llX (%s) does not reach RVA 0x%llX.", group,
                static_cast<unsigned long long>(call.site), call.name, static_cast<unsigned long long>(call.target));
            ok = false;
        }
    }
    return ok;
}

auto VerifyAll() -> bool {
    bool ok = VerifyWitnesses(Witnesses, "save and load");
    ok = VerifyWitnesses(CoreWitnesses, "save and load") && ok;
    ok = VerifyCalls(CoreCalls, "save and load") && ok;
    return ok;
}

void BindNatives() {
    ServerUnit   = At<ServerUnitFn>(GetServerUnitRva);
    PlayerData   = At<PlayerDataFn>(GetPlayerDataRva);
    UnitGuid     = At<UnitGuidFn>(GetUnitGuidRva);
    ClassId      = At<ClassIdFn>(GetClassIdRva);
    CheckState   = At<CheckStateFn>(CheckStateRva);
    DataTables   = At<DataTablesFn>(DataTablesRva);
    HighestSkill = At<HighestSkillFn>(HighestSkillRva);
    SkillLevel   = At<SkillLevelFn>(SkillLevelRva);
    PathX        = At<PathCoordFn>(PathXRva);
    PathY        = At<PathCoordFn>(PathYRva);
    WarpPets     = At<WarpPetsFn>(WarpPetsRva);
    UnitRoom     = At<UnitRoomFn>(UnitRoomRva);
}

void BindSummonNatives() {
    SkillRecord   = At<SkillRecordFn>(SkillRecordRva);
    Calc          = At<CalcFn>(CalcRva);
    SummonClass   = At<SummonClassFn>(SummonClassRva);
    SummonPet     = At<SummonPetFn>(SummonPetRva);
    CastId        = At<UnitIntFn>(CastIdRva);
    PlayerSlot    = At<UnitIntFn>(PlayerSlotRva);
    AssignSlot    = At<AssignSlotFn>(AssignSlotRva);
    GolemSetup    = At<GolemSetupFn>(GolemSetupRva);
    PetLevel      = At<PetLevelFn>(PetLevelRva);
    SummonStats   = At<SummonStatsFn>(SummonStatsRva);
    SkeletonSetup = At<SkeletonSetupFn>(SkeletonSetupRva);
    SkeletonPre   = At<SkeletonPreFn>(SkeletonPreRva);
    SkeletonPost  = At<SkeletonPostFn>(SkeletonPostRva);
    ClientOf      = At<ClientOfFn>(ClientOfRva);
    PetInfo       = At<PetInfoFn>(PetInfoRva);
    AiMode        = At<AiModeFn>(AiModeRva);
    AiTimer       = At<AiTimerFn>(AiTimerRva);
    SetState      = At<SetStateFn>(SetStateRva);
    LinkOwner     = At<LinkOwnerFn>(LinkOwnerRva);
}

auto Hook(std::uint64_t rva, const std::uint8_t* bytes, std::uint32_t size, void* hook, void** original,
        const char* name) -> bool {
    if (Context->InstallInlineHook(rva, bytes, size, hook, original)) return true;
    D2RL::LogErrorF(Context, "SummonEnhancements: the %s at RVA 0x%llX could not be hooked.", name,
        static_cast<unsigned long long>(rva));
    return false;
}

auto StateName(PluginState state) noexcept -> const char* {
    switch (state) {
    case PluginState::NotLoaded:        return "not loaded";
    case PluginState::DisabledByConfig: return "off in the TOML";
    case PluginState::UnsupportedBuild: return "unsupported build, not installed";
    case PluginState::InstallFailed:    return "install failed, see the log";
    case PluginState::BadConfig:        return "TOML error, see the log";
    case PluginState::Running:          return "active";
    }
    return "?";
}

void SetFeatureStates(PluginState state) {
    if (Settings.saveEveryDemon) DemonState = state;
    if (Settings.demonAllClasses) ClassState = state;
    if (Settings.saveSummons) SummonState = state;
}

// 533D38: the Warlock check of the demon save, replaced with the Warlock's
// own arguments for every class. Verified after writing, put back otherwise.
void InstallAllClasses() {
    if (!Context->PatchBytes(ClassCheckRva, ClassCheckStock, sizeof(ClassCheckStock), ClassCheckPatched,
            sizeof(ClassCheckPatched))) {
        ClassState = PluginState::UnsupportedBuild;
        Context->LogError("SummonEnhancements: the Warlock check of the bound demon save does not match (or another plugin "
                          "owns it); only Warlocks save bound demons.");
        return;
    }
    if (std::memcmp(reinterpret_cast<const void*>(Base + ClassCheckRva), ClassCheckPatched, sizeof(ClassCheckPatched)) != 0) {
        const bool restored = Context->PatchBytes(ClassCheckRva, ClassCheckPatched, sizeof(ClassCheckPatched), ClassCheckStock,
            sizeof(ClassCheckStock));
        ClassState = PluginState::InstallFailed;
        Context->LogError(restored
            ? "SummonEnhancements: the bound demon save does not hold the written bytes; the Warlock check was put back."
            : "SummonEnhancements: the bound demon save does not hold the written bytes, and the Warlock check could not be put back.");
        return;
    }
    ClassPatchApplied = true;
    AllClassesActive.store(true, std::memory_order_relaxed);
    ClassState = PluginState::Running;
}

void InstallSummons() {
    if (!SummonListError.empty()) {
        SummonState = PluginState::BadConfig;
        D2RL::LogErrorF(Context, "SummonEnhancements: summon_skills could not be read (%s); summons are not saved.",
            SummonListError.c_str());
        return;
    }
    bool ok = VerifyWitnesses(SummonWitnesses, "summons");
    ok = VerifyCalls(SummonCalls, "summons") && ok;
    if (!ok) {
        SummonState = PluginState::UnsupportedBuild;
        Context->LogError("SummonEnhancements: the summon code of this D2R build does not match; summons are not saved.");
        return;
    }
    BindSummonNatives();
    SummonsActive.store(true, std::memory_order_relaxed);
    SummonState = PluginState::Running;
}

void InstallPersistence() {
    if (!Settings.saveEveryDemon) DemonState = PluginState::DisabledByConfig;
    if (!Settings.demonAllClasses) ClassState = PluginState::DisabledByConfig;
    if (!Settings.saveSummons) SummonState = PluginState::DisabledByConfig;
    if (!Settings.saveEveryDemon && !Settings.demonAllClasses && !Settings.saveSummons) {
        SaveState = PluginState::DisabledByConfig;
        Context->LogInfo("SummonEnhancements: save_every_demon, demon_all_classes and save_summons are all false.");
        return;
    }
    if (!VerifyAll()) {
        SaveState = PluginState::UnsupportedBuild;
        SetFeatureStates(PluginState::UnsupportedBuild);
        Context->LogError("SummonEnhancements: this D2R build does not match; nothing was installed for saving.");
        return;
    }
    BindNatives();

    // The helper hooks do nothing on their own, so they go first. The reader
    // goes last: blocks written without it would still load their first demon.
    const bool ok =
        Hook(FirstPetOfTypeRva, FirstPetOfTypeBytes, sizeof(FirstPetOfTypeBytes),
            reinterpret_cast<void*>(&HookedFirstPet), reinterpret_cast<void**>(&OriginalFirstPet), "first pet of a pettype")
        && Hook(SetPetTypeCapRva, SetPetTypeCapBytes, sizeof(SetPetTypeCapBytes),
            reinterpret_cast<void*>(&HookedSetCap), reinterpret_cast<void**>(&OriginalSetCap), "pettype cap")
        && Hook(EnterWorldRva, EnterWorldBytes, sizeof(EnterWorldBytes),
            reinterpret_cast<void*>(&HookedEnterWorld), reinterpret_cast<void**>(&OriginalEnterWorld), "world entry")
        && Hook(WriteDemonBlockRva, WriteDemonBlockBytes, sizeof(WriteDemonBlockBytes),
            reinterpret_cast<void*>(&HookedWriteBlock), reinterpret_cast<void**>(&OriginalWriteBlock), "demon block writer")
        && Hook(ReadDemonBlockRva, ReadDemonBlockBytes, sizeof(ReadDemonBlockBytes),
            reinterpret_cast<void*>(&HookedReadBlock), reinterpret_cast<void**>(&OriginalReadBlock), "demon block reader");
    if (!ok) {
        SaveState = PluginState::InstallFailed;
        SetFeatureStates(PluginState::InstallFailed);
        return;
    }
    if (Settings.saveEveryDemon) {
        MultiDemonActive.store(true, std::memory_order_relaxed);
        DemonState = PluginState::Running;
    }
    if (Settings.demonAllClasses) InstallAllClasses();
    if (Settings.saveSummons) InstallSummons();
    SaveState = PluginState::Running;
    PersistenceActive.store(true, std::memory_order_relaxed);
    D2RL::LogInfoF(Context, "SummonEnhancements: active. Every bound demon: %s. Every class: %s. Summons: %s (%zu listed skills).",
        StateName(DemonState), StateName(ClassState), StateName(SummonState), Settings.summonSkills.size());
}

void __cdecl OnGameLeft(const D2RL::PluginContext*, const D2RL::Lifecycle::GameplayEvent*, void*) noexcept {
    ClearPending();
}

const D2RL::ThreadService* Threads{};

void __cdecl OnPlayerReady(const D2RL::PluginContext* context, const D2RL::Lifecycle::GameplayEvent* event, void*) noexcept {
    if (Threads == nullptr || event == nullptr || PendingCount() == 0) return;
    const D2RL::Threads::Result queued = Threads->runOnGameThread(context, &RetryOnGameThread,
        reinterpret_cast<void*>(static_cast<std::uintptr_t>(event->playerId)));
    if (queued != D2RL::Threads::Result::Success) {
        D2RL::LogWarnF(Context, "SummonEnhancements: the player-ready retry could not be queued (%u).",
            static_cast<unsigned>(queued));
    }
}

void RegisterGameplayListeners() {
    const D2RL::LifecycleService* lifecycle = nullptr;
    if (Context->QueryService(&lifecycle) != D2RL::ServiceQueryResult::Success
            || !D2RL::HasLifecycleServiceField(lifecycle, D2RL::LifecycleServiceRequiredSize)) {
        Context->LogWarn("SummonEnhancements: the lifecycle service is unavailable; kept demons and summons of a game are "
                         "dropped at the next load instead of when leaving it.");
        return;
    }
    D2RL::Lifecycle::GameplayEventListener listener{};
    listener.structSize = D2RL::Lifecycle::GameplayEventListenerSize;
    listener.kind       = D2RL::Lifecycle::GameplayEventKind::GameLeft;
    listener.callback   = &OnGameLeft;
    D2RL::Lifecycle::ListenerHandle handle = D2RL::Lifecycle::InvalidHandle;
    if (lifecycle->registerGameplayEventListener(Context, &listener, &handle) != D2RL::Lifecycle::Result::Success) {
        Context->LogWarn("SummonEnhancements: leaving a game could not be watched; kept demons and summons of a game are "
                         "dropped at the next load instead.");
    }
    const D2RL::ThreadService* threads = nullptr;
    if (Context->QueryService(&threads) == D2RL::ServiceQueryResult::Success
            && D2RL::HasThreadServiceField(threads, D2RL::ThreadServiceRequiredSize)) {
        Threads = threads;
        D2RL::Lifecycle::GameplayEventListener ready{};
        ready.structSize = D2RL::Lifecycle::GameplayEventListenerSize;
        ready.kind       = D2RL::Lifecycle::GameplayEventKind::LocalPlayerReady;
        ready.callback   = &OnPlayerReady;
        D2RL::Lifecycle::ListenerHandle readyHandle = D2RL::Lifecycle::InvalidHandle;
        if (lifecycle->registerGameplayEventListener(Context, &ready, &readyHandle) == D2RL::Lifecycle::Result::Success) return;
    }
    Context->LogWarn("SummonEnhancements: the player-ready retry is unavailable; what waits is tried again only on the "
                     "next world entry.");
}

auto __cdecl StatusCommand(D2R::Game::Client*, const D2RL::ConsoleCommandContext* command, void*) noexcept
        -> D2RL::ConsoleCommandResult {
    char line[512];
    std::snprintf(line, sizeof(line), "Summon Enhancements saving: %s.", StateName(SaveState));
    command->plugin->WriteConsoleMessage(line);
    std::snprintf(line, sizeof(line), "  every bound demon: %s. every class: %s. summons: %s, %zu listed skills.",
        StateName(DemonState), StateName(ClassState), StateName(SummonState), Settings.summonSkills.size());
    command->plugin->WriteConsoleMessage(line);
    std::snprintf(line, sizeof(line), "  demons written %llu, left out %llu, restored %llu, kept for entering the world %llu, "
        "rebuilt there %llu, unreadable blocks %llu, load caps raised %llu.",
        static_cast<unsigned long long>(DemonsWritten.load()), static_cast<unsigned long long>(DemonsRefused.load()),
        static_cast<unsigned long long>(DemonsRestored.load()), static_cast<unsigned long long>(DemonsKept.load()),
        static_cast<unsigned long long>(DemonsRebuilt.load()), static_cast<unsigned long long>(BlocksNotRestored.load()),
        static_cast<unsigned long long>(CapsRaised.load()));
    command->plugin->WriteConsoleMessage(line);
    std::snprintf(line, sizeof(line), "  summons written %llu, left out %llu, back %llu, not back %llu. Characters waiting "
        "to enter the world: %zu.",
        static_cast<unsigned long long>(SummonsWritten.load()), static_cast<unsigned long long>(SummonsLeftOut.load()),
        static_cast<unsigned long long>(SummonsBack.load()), static_cast<unsigned long long>(SummonsNotBack.load()),
        PendingCount());
    command->plugin->WriteConsoleMessage(line);
    const std::string save  = GetSummary(LastSave);
    const std::string load  = GetSummary(LastLoad);
    const std::string entry = GetSummary(LastEntry);
    command->plugin->WriteConsoleMessage(save.empty() ? "  no save yet." : save.c_str());
    command->plugin->WriteConsoleMessage(load.empty() ? "  no load with a saved demon or summon yet." : load.c_str());
    command->plugin->WriteConsoleMessage(entry.empty() ? "  nothing brought back on entering the world yet." : entry.c_str());
    return D2RL::ConsoleCommandResult::Handled;
}

auto __cdecl AuraStatusCommand(D2R::Game::Client*, const D2RL::ConsoleCommandContext* command, void*) noexcept
        -> D2RL::ConsoleCommandResult {
    char line[512];
    std::snprintf(line, sizeof(line), "Summon Enhancements aura list: %s%s%s.", StateName(AuraState),
        AuraListError.empty() ? "" : " | ", AuraListError.c_str());
    command->plugin->WriteConsoleMessage(line);
    std::string list = "  list:";
    for (const auto& entry : Settings.auras) {
        char item[64];
        if (entry.minLevel > 0) {
            std::snprintf(item, sizeof(item), " %d/%d (level %d+)", entry.skill, entry.divisor, entry.minLevel);
        } else {
            std::snprintf(item, sizeof(item), " %d/%d", entry.skill, entry.divisor);
        }
        list += item;
    }
    if (Settings.auras.empty()) list += " empty, bound demons get no aura";
    command->plugin->WriteConsoleMessage(list.c_str());
    std::snprintf(line, sizeof(line),
        "  rolls set %llu, no entry %llu, bad skill id %llu, native rolls replaced %llu, restore seeds kept %llu.",
        static_cast<unsigned long long>(RollsApplied.load()), static_cast<unsigned long long>(RollsEmpty.load()),
        static_cast<unsigned long long>(RollsBadSkill.load()), static_cast<unsigned long long>(NativeRollsReplaced.load()),
        static_cast<unsigned long long>(SeedsKept.load()));
    command->plugin->WriteConsoleMessage(line);
    AcquireSRWLockShared(&HistoryLock);
    const std::size_t                         count = HistoryCount;
    const std::size_t                         next  = HistoryNext;
    const std::array<RollRecord, HistorySize> copy  = History;
    ReleaseSRWLockShared(&HistoryLock);
    if (count == 0) command->plugin->WriteConsoleMessage("  no roll yet.");
    for (std::size_t i = 0; i < count; ++i) {
        const RollRecord& r = copy[(next + HistorySize - count + i) % HistorySize];
        std::snprintf(line, sizeof(line), "  %s: demon %u (monstats %u, level %d, seed %u) -> skill %d level %d, %s.",
            r.moment, r.unitId, r.classId, r.monsterLevel, static_cast<unsigned>(r.seed), r.skill, r.level, r.result);
        command->plugin->WriteConsoleMessage(line);
    }
    return D2RL::ConsoleCommandResult::Handled;
}

constexpr D2RL::PluginInfo PluginInfoData{
    .infoSize    = D2RL::PluginInfoSize,
    .abiVersion  = D2RL_PLUGIN_ABI_VERSION,
    .id          = "celestialrayone.summon-enhancements",
    .name        = "Summon Enhancements",
    .version     = "2.0.0",
    .author      = "CelestialRayOne",
    .description = "Bound demons of every class and the summons of listed skills are saved and come back; bound demons roll "
                   "their aura from a list.",
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

    if (!Context->RegisterConsoleCommand("summonsave", &StatusCommand,
            "Show what Summon Enhancements saved, restored and brought back on entering the world.")) {
        Context->LogWarn("SummonEnhancements: the summonsave console command could not be registered.");
    }
    if (!Context->RegisterConsoleCommand("summonauras", &AuraStatusCommand,
            "Show the bound demon aura list, its counters and the last rolls.")) {
        Context->LogWarn("SummonEnhancements: the summonauras console command could not be registered.");
    }

    ReadConfiguration();
    if (!Settings.enabled) {
        SaveState   = PluginState::DisabledByConfig;
        DemonState  = PluginState::DisabledByConfig;
        ClassState  = PluginState::DisabledByConfig;
        SummonState = PluginState::DisabledByConfig;
        AuraState   = PluginState::DisabledByConfig;
        Context->LogInfo("SummonEnhancements: disabled by config.");
        return true;
    }
    RegisterGameplayListeners();
    // Each feature installs on its own, so one refusing never takes another down.
    InstallPersistence();
    InstallAuraList();
    return true;
}

D2RL_PLUGIN_EXPORT void D2RLoaderUnloadPlugin() noexcept {
    PersistenceActive.store(false);
    MultiDemonActive.store(false);
    AllClassesActive.store(false);
    SummonsActive.store(false);
    AuraActive.store(false);
    RemoveCoreDetour(AuraDetour);
    if (ClassPatchApplied && Context != nullptr) {
        if (Context->PatchBytes(ClassCheckRva, ClassCheckPatched, sizeof(ClassCheckPatched), ClassCheckStock,
                sizeof(ClassCheckStock))) {
            ClassPatchApplied = false;
        }
    }
    ClearPending();
}
