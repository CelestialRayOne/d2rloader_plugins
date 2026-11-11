// ---------------------------------------------------------------------------
// sequence-skill-stat
//
// Exposes one virtual stat id (configurable, no default) to .txt formulas.
// While the unit a formula is evaluated for is in a sequence (player mode 18,
// monster mode 14), the stat reads as the skills.txt id of the skill running
// that sequence. Otherwise it reads 0. Every other stat id is untouched.
//
// Target: the D2RLoader 1.3.1 image of D2R 3.3 (image base 0x140000000).
// Nothing is installed unless every byte this plugin depends on matches. On a
// build it does not recognise it installs nothing, logs which check failed
// and stays loaded so the console command can say why.
//
// ---------------------------------------------------------------------------
// Where the stat is answered, and why there
// ---------------------------------------------------------------------------
//
// .txt formulas are compiled to bytecode and run by the generic evaluator
// 0xA234B0. A function token calls through a nine-record callback table at
// 0x1D09C50, records of {callback VA : qword, arity : dword, pad : dword}.
// Record 5 (slot 0x1D09CA0) is stat(), callback 0x3B33F0, arity 2. The
// evaluator reads the slot on every call:
//
//   call [table + 16 * index] (bankByte, arg1 = stat id, arg2 = field, 0, context)
//
// and every table-wired evaluator call site passes this same table (census in
// the RuffnecKk research ledger, SKILLS_BBEEvaluatorCallbackTableWiringWitness).
// Apart from unwind data, that slot is the only reference to 0x3B33F0.
//
// playermode-exposing already owns an inline hook on the first 5 bytes of
// 0x3B33F0, and D2RLoader refuses a patch that overlaps a tracked one. So this
// plugin redirects the table slot instead: the slot points at the handler
// below, which answers its own id and hands everything else to the stock
// callback. The stock callback is wherever playermode-exposing's hook sits, so
// both plugins chain in either load order. The same slot-swap through
// PatchWriteU64 is what RuffnecKk's auto-pickup does to the server packet
// table at 0x1D2A790, in the same read-only data region.
//
// ---------------------------------------------------------------------------
// Why the used skill is the right answer
// ---------------------------------------------------------------------------
//
// The game picks a running sequence from the unit's used skill:
//
//   0x3CB890 (sequence resolver)  call 0x34BA40 UNITS_GetUsedSkill
//                                 call 0x33DBC0 seqnum from that skill
//   0x34C880 (mode start)         player mode 18 -> 0x34D900 SetSequence
//                                 monster mode 14 -> 0x34D900 SetSequence
//
// UNITS_GetUsedSkill is [[unit+0x100]+0x18] (0x34BA40 -> 0x33E090), and a
// skill's id is the signed word at the start of its skills.txt record
// (0x33E080: mov rax,[rcx] / movsx eax,word [rax]). So "in a sequence and the
// used skill is X" is exactly "running X's sequence".
//
// Procs do not move it. The central server do-handler 0x43ACB0, which every
// chance-to-cast goes through, never installs the skill it casts: its only
// UNITS_SetUsedSkill (0x34F430) call clears the slot on its abort path. No
// caller of UNITS_SetUsedSkill sits in the item-skill casters (0x589930,
// 0x5896E0, 0x589820), and none of the 172 srvdofunc slots at 0x238EA00 (the
// functions a proc runs, ItemEffect included) is one of its callers. The one
// event function that calls it, 0x584480, sets it on a monster it has just
// spawned, not on the attacker.
// ---------------------------------------------------------------------------

#include <D2RLPlugin/api.h>

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace {

// ---------------------------------------------------------------------------
// Native contract
// ---------------------------------------------------------------------------

constexpr std::uint64_t BbeCallbackTableRva = 0x01D09C50ULL;
constexpr std::size_t   BbeRecordCount      = 9;
constexpr std::size_t   BbeRecordSize       = 16;
constexpr std::size_t   StatRecordIndex     = 5;
constexpr std::uint64_t StatSlotRva         = BbeCallbackTableRva + StatRecordIndex * BbeRecordSize;
constexpr std::uint64_t StatCallbackRva     = 0x003B33F0ULL;

struct BbeRecordContract {
	std::uint64_t callbackRva;
	std::uint32_t arity;
};

// The whole table as the stock image holds it. Checked byte for byte (with the
// callback addresses relocated to the running base) before the slot is touched.
constexpr BbeRecordContract BbeRecords[BbeRecordCount] {
	{ 0x3B3200ULL, 2U },
	{ 0x3B3210ULL, 2U },
	{ 0x3B3220ULL, 2U },
	{ 0x3B32F0ULL, 2U },
	{ 0x3B33A0ULL, 2U },
	{ 0x3B33F0ULL, 2U }, // stat()
	{ 0x3B3500ULL, 3U },
	{ 0x3B3590ULL, 2U },
	{ 0x3B3640ULL, 0xFFFFFFFFU },
};

static_assert(StatSlotRva == 0x01D09CA0ULL, "The stat() slot moved.");
static_assert(BbeRecords[StatRecordIndex].callbackRva == StatCallbackRva, "Record 5 must be the stat() callback.");

// Layout read by the handler. Each one is proven by a witness below.
constexpr std::size_t   ContextUnitOffset        = 0x10;  // stat() reads its unit here (0x3B3434)
constexpr std::size_t   UnitTypeOffset           = 0x00;  // UNITS_GetUnitType 0x34B9D0
constexpr std::size_t   UnitModeOffset           = 0x0C;  // UNITS_GetUnitMode 0x34AB60
constexpr std::size_t   UnitSkillListOffset      = 0x100; // UNITS_GetUsedSkill 0x34BA40
constexpr std::size_t   SkillListUsedSkillOffset = 0x18;  // 0x33E090
constexpr std::size_t   SkillRecordOffset        = 0x00;  // 0x33E080
constexpr std::size_t   SkillRecordIdOffset      = 0x00;  // 0x33E080, signed word

constexpr std::uint32_t UnitTypePlayer      = 0;
constexpr std::uint32_t UnitTypeMonster     = 1;
constexpr std::uint32_t PlayerSequenceMode  = 18; // 0x34CBEB
constexpr std::uint32_t MonsterSequenceMode = 14; // 0x34CB39

// ---------------------------------------------------------------------------
// Byte witnesses, all read from the 1.3.1 image. Only checked, never written.
// ---------------------------------------------------------------------------

// stat() callback from +5, so the 5 bytes playermode-exposing displaces are
// left out. Proves the context is the 5th argument (push rdi / sub rsp,20h /
// mov rsi,[rsp+50h]) and the unit sits at context+0x10 (mov rsi,[rsi+10h]).
constexpr std::uint8_t W_StatCallbackTail[] {
	0x48, 0x89, 0x74, 0x24, 0x18, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B, 0x74, 0x24, 0x50, 0x41,
	0x8B, 0xD8, 0x8B, 0xFA, 0x48, 0x85, 0xF6, 0x75, 0x26, 0x48, 0x8D, 0x4C, 0x24, 0x50, 0x40, 0x88,
	0x74, 0x24, 0x50, 0xE8, 0xF3, 0x0E, 0x00, 0x00, 0x84, 0xC0, 0x74, 0x01, 0xCC, 0x33, 0xC0, 0x48,
	0x8B, 0x5C, 0x24, 0x38, 0x48, 0x8B, 0x74, 0x24, 0x40, 0x48, 0x83, 0xC4, 0x20, 0x5F, 0xC3, 0x48,
	0x8B, 0x76, 0x10, 0x48, 0x85, 0xF6, 0x74, 0xE5,
};

// UNITS_GetUsedSkill: mov rcx,[rbx+100h] / jmp 0x33E090.
constexpr std::uint8_t W_GetUsedSkill[] {
	0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B, 0xD9, 0x48, 0x85, 0xC9, 0x75, 0x13, 0x88, 0x4C,
	0x24, 0x30, 0x48, 0x8D, 0x4C, 0x24, 0x30, 0xE8, 0x04, 0xA6, 0xFF, 0xFF, 0x84, 0xC0, 0x74, 0x01,
	0xCC, 0x48, 0x8B, 0x8B, 0x00, 0x01, 0x00, 0x00, 0x48, 0x83, 0xC4, 0x20, 0x5B, 0xE9, 0x1E, 0x26,
	0xFF, 0xFF,
};

// 0x33E080 skill id = movsx word [[skill]], then 0x33E090 used skill = [list+18h].
constexpr std::uint8_t W_SkillIdAndUsedSlot[] {
	0x48, 0x85, 0xC9, 0x75, 0x03, 0x33, 0xC0, 0xC3, 0x48, 0x8B, 0x01, 0x0F, 0xBF, 0x00, 0xC3, 0xCC,
	0x48, 0x85, 0xC9, 0x75, 0x03, 0x33, 0xC0, 0xC3, 0x48, 0x8B, 0x41, 0x18, 0xC3,
};

// UNITS_GetUnitType: mov eax,[rcx].
constexpr std::uint8_t W_GetUnitType[] {
	0x48, 0x83, 0xEC, 0x28, 0x48, 0x85, 0xC9, 0x75, 0x1D, 0x88, 0x4C, 0x24, 0x30, 0x48, 0x8D, 0x4C,
	0x24, 0x30, 0xE8, 0x39, 0x9E, 0xFF, 0xFF, 0x84, 0xC0, 0x74, 0x01, 0xCC, 0xB8, 0x06, 0x00, 0x00,
	0x00, 0x48, 0x83, 0xC4, 0x28, 0xC3, 0x8B, 0x01, 0x48, 0x83, 0xC4, 0x28, 0xC3,
};

// UNITS_GetUnitMode: mov eax,[rcx+0Ch].
constexpr std::uint8_t W_GetUnitMode[] {
	0x48, 0x83, 0xEC, 0x28, 0x48, 0x85, 0xC9, 0x75, 0x1A, 0x88, 0x4C, 0x24, 0x30, 0x48, 0x8D, 0x4C,
	0x24, 0x30, 0xE8, 0xF9, 0xA3, 0xFF, 0xFF, 0x84, 0xC0, 0x74, 0x01, 0xCC, 0x33, 0xC0, 0x48, 0x83,
	0xC4, 0x28, 0xC3, 0x8B, 0x41, 0x0C, 0x48, 0x83, 0xC4, 0x28, 0xC3,
};

// Sequence resolver head: call 0x34BA40 (used skill), then call 0x33DBC0
// (seqnum of that skill). The sequence is chosen from the used skill.
constexpr std::uint8_t W_SeqResolverHead[] {
	0x40, 0x55, 0x56, 0x48, 0x83, 0xEC, 0x38, 0x48, 0x8B, 0xF1, 0xE8, 0xA1, 0x01, 0xF8, 0xFF, 0x48,
	0x85, 0xC0, 0x0F, 0x84, 0x57, 0x01, 0x00, 0x00, 0x48, 0x8B, 0xD0, 0x48, 0x8B, 0xCE, 0xE8, 0x0D,
	0x23, 0xF7, 0xFF,
};

// Mode start, monster branch: cmp mode,0Eh / jne / call 0x34D900 SetSequence.
constexpr std::uint8_t W_MonsterSequenceBranch[] {
	0x83, 0x7D, 0xE4, 0x0E, 0x48, 0x8B, 0xCF, 0x75, 0x1B, 0xE8, 0xB9, 0x0D, 0x00, 0x00,
};

// Mode start, player branch: cmp mode,12h / jne / call 0x34D900 SetSequence.
constexpr std::uint8_t W_PlayerSequenceBranch[] {
	0x83, 0x7D, 0xE4, 0x12, 0x48, 0x8B, 0xCF, 0x75, 0x18, 0xE8, 0x07, 0x0D, 0x00, 0x00,
};

struct Witness {
	std::uint64_t       rva;
	const std::uint8_t* bytes;
	std::uint32_t       size;
	const char*         what;
};

template <std::size_t Size>
constexpr auto MakeWitness(std::uint64_t rva, const std::uint8_t (&bytes)[Size], const char* what) noexcept -> Witness {
	return { rva, bytes, static_cast<std::uint32_t>(Size), what };
}

constexpr Witness Witnesses[] {
	MakeWitness(0x3B33F5ULL, W_StatCallbackTail, "stat() callback body (context argument, unit at context+0x10)"),
	MakeWitness(0x34BA40ULL, W_GetUsedSkill, "UNITS_GetUsedSkill (skill list at unit+0x100)"),
	MakeWitness(0x33E080ULL, W_SkillIdAndUsedSlot, "skill id getter and used-skill slot (list+0x18)"),
	MakeWitness(0x34B9D0ULL, W_GetUnitType, "UNITS_GetUnitType (unit+0x00)"),
	MakeWitness(0x34AB60ULL, W_GetUnitMode, "UNITS_GetUnitMode (unit+0x0C)"),
	MakeWitness(0x3CB890ULL, W_SeqResolverHead, "sequence resolver (sequence picked from the used skill)"),
	MakeWitness(0x34CB39ULL, W_MonsterSequenceBranch, "mode start, monster sequence mode 14"),
	MakeWitness(0x34CBEBULL, W_PlayerSequenceBranch, "mode start, player sequence mode 18"),
};

static_assert(sizeof(W_StatCallbackTail) == 72, "stat() tail witness length changed.");
static_assert(sizeof(W_GetUsedSkill) == 50, "GetUsedSkill witness length changed.");
static_assert(sizeof(W_SkillIdAndUsedSlot) == 29, "Skill id witness length changed.");
static_assert(sizeof(W_GetUnitType) == 45, "GetUnitType witness length changed.");
static_assert(sizeof(W_GetUnitMode) == 43, "GetUnitMode witness length changed.");
static_assert(sizeof(W_SeqResolverHead) == 35, "Sequence resolver witness length changed.");
static_assert(W_StatCallbackTail[64] == 0x8B && W_StatCallbackTail[65] == 0x76 && W_StatCallbackTail[66] == ContextUnitOffset,
	"The stat() witness must contain mov rsi,[rsi+10h].");
static_assert(W_GetUsedSkill[36] == 0x00 && W_GetUsedSkill[37] == 0x01, "The used-skill witness must contain [rbx+100h].");
static_assert(W_SkillIdAndUsedSlot[27] == SkillListUsedSkillOffset, "The used-skill slot must be list+0x18.");
static_assert(W_GetUnitMode[37] == UnitModeOffset, "The mode witness must contain [rcx+0Ch].");
static_assert(W_MonsterSequenceBranch[3] == MonsterSequenceMode, "Monster sequence mode must be 14.");
static_assert(W_PlayerSequenceBranch[3] == PlayerSequenceMode, "Player sequence mode must be 18.");

// --- core begin (no SDK types below this line until "core end") -------------

// ---------------------------------------------------------------------------
// Configuration text scanner
// ---------------------------------------------------------------------------
//
// The loader hands the plugin the raw text of its own TOML file; there is no
// parser in the SDK. This understands exactly what this file needs: [section]
// headers, `key = value`, `#` comments (whole line or trailing), LF and CRLF.
// Anything else is ignored.

struct Slice {
	const char* begin;
	const char* end;
};

constexpr auto IsBlank(char value) noexcept -> bool {
	return value == ' ' || value == '\t' || value == '\r';
}

constexpr auto Trim(Slice slice) noexcept -> Slice {
	while (slice.begin < slice.end && IsBlank(*slice.begin)) {
		++slice.begin;
	}
	while (slice.end > slice.begin && IsBlank(slice.end[-1])) {
		--slice.end;
	}
	return slice;
}

auto SliceEquals(Slice slice, const char* literal) noexcept -> bool {
	const char* text   = literal;
	const char* cursor = slice.begin;
	while (cursor < slice.end && *text != '\0') {
		if (*cursor != *text) {
			return false;
		}
		++cursor;
		++text;
	}
	return cursor == slice.end && *text == '\0';
}

// Finds the raw right-hand side of section.key. Last assignment wins.
auto FindConfigValue(const char* toml, const char* section, const char* key, Slice& value) noexcept -> bool {
	if (toml == nullptr) {
		return false;
	}

	std::array<char, 64> currentSection {};
	bool                 found = false;

	for (const char* line = toml; *line != '\0';) {
		const char* lineEnd = line;
		while (*lineEnd != '\0' && *lineEnd != '\n') {
			++lineEnd;
		}

		const Slice trimmed = Trim({ line, lineEnd });

		if (trimmed.begin < trimmed.end && *trimmed.begin != '#') {
			if (*trimmed.begin == '[') {
				const char* close = trimmed.begin;
				while (close < trimmed.end && *close != ']') {
					++close;
				}

				const Slice name = Trim({ trimmed.begin + 1, close });
				std::size_t used = 0;
				for (const char* cursor = name.begin; cursor < name.end && used + 1 < currentSection.size(); ++cursor) {
					currentSection[used++] = *cursor;
				}
				currentSection[used] = '\0';
			} else {
				const char* equals = trimmed.begin;
				while (equals < trimmed.end && *equals != '=') {
					++equals;
				}

				if (equals < trimmed.end) {
					const Slice name = Trim({ trimmed.begin, equals });
					Slice       raw  = Trim({ equals + 1, trimmed.end });

					for (const char* cursor = raw.begin; cursor < raw.end; ++cursor) {
						if (*cursor == '#') {
							raw.end = cursor;
							break;
						}
					}
					raw = Trim(raw);

					if (std::strcmp(currentSection.data(), section) == 0 && SliceEquals(name, key)) {
						value = raw;
						found = true;
					}
				}
			}
		}

		line = (*lineEnd == '\0') ? lineEnd : lineEnd + 1;
	}

	return found;
}

// Both readers leave the caller's value untouched unless the key was present
// AND parsed cleanly.
auto ReadConfigBool(const char* toml, const char* section, const char* key, bool& value) noexcept -> bool {
	Slice raw {};
	if (!FindConfigValue(toml, section, key, raw)) {
		return false;
	}

	if (SliceEquals(raw, "true")) {
		value = true;
		return true;
	}
	if (SliceEquals(raw, "false")) {
		value = false;
		return true;
	}
	return false;
}

auto ReadConfigInt(const char* toml, const char* section, const char* key, std::int32_t& value) noexcept -> bool {
	Slice raw {};
	if (!FindConfigValue(toml, section, key, raw)) {
		return false;
	}

	const char* cursor   = raw.begin;
	bool        negative = false;
	if (cursor < raw.end && (*cursor == '+' || *cursor == '-')) {
		negative = (*cursor == '-');
		++cursor;
	}

	if (cursor >= raw.end) {
		return false;
	}

	std::int64_t parsed = 0;
	for (; cursor < raw.end; ++cursor) {
		if (*cursor == '_') { // TOML digit separator
			continue;
		}
		if (*cursor < '0' || *cursor > '9') {
			return false;
		}
		parsed = parsed * 10 + (*cursor - '0');
		if (parsed > 0x7FFFFFFFLL) {
			return false;
		}
	}

	value = static_cast<std::int32_t>(negative ? -parsed : parsed);
	return true;
}

constexpr std::int32_t StatIdNotSet  = -1;
constexpr std::int32_t MinimumStatId = 0;
constexpr std::int32_t MaximumStatId = 32767;

// ---------------------------------------------------------------------------
// The answer
// ---------------------------------------------------------------------------

template <typename Value>
auto Load(const std::uint8_t* address) noexcept -> Value {
	Value value;
	std::memcpy(&value, address, sizeof(value));
	return value;
}

// The skills.txt id of the skill running the unit's sequence, or 0 when the
// unit is not in one. unitType is reported back for the console counters.
auto ReadSequenceSkill(const std::uint8_t* unit, std::uint32_t& unitType) noexcept -> std::int32_t {
	unitType = Load<std::uint32_t>(unit + UnitTypeOffset);

	std::uint32_t sequenceMode = 0;
	if (unitType == UnitTypePlayer) {
		sequenceMode = PlayerSequenceMode;
	} else if (unitType == UnitTypeMonster) {
		sequenceMode = MonsterSequenceMode;
	} else {
		return 0;
	}

	if (Load<std::uint32_t>(unit + UnitModeOffset) != sequenceMode) {
		return 0;
	}

	const auto* skillList = Load<const std::uint8_t*>(unit + UnitSkillListOffset);
	if (skillList == nullptr) {
		return 0;
	}

	const auto* usedSkill = Load<const std::uint8_t*>(skillList + SkillListUsedSkillOffset);
	if (usedSkill == nullptr) {
		return 0;
	}

	const auto* record = Load<const std::uint8_t*>(usedSkill + SkillRecordOffset);
	if (record == nullptr) {
		return 0;
	}

	return static_cast<std::int32_t>(Load<std::int16_t>(record + SkillRecordIdOffset));
}

// Opaque parameters are carried at full width so the pass-through hands the
// stock callback exactly the registers the evaluator set up.
using StatCallbackFn = std::int64_t(__fastcall*)(
	std::uint64_t bankByte,
	std::int32_t  statId,
	std::int32_t  fieldCode,
	std::uint64_t reserved,
	std::uint64_t context) noexcept;

// Written once before the slot is redirected, read by every call afterwards.
std::atomic<StatCallbackFn> StockStatCallback { nullptr };
std::atomic<std::int32_t>   ActiveStatId { StatIdNotSet };

// Console counters. Touched only when a formula reads this plugin's id.
std::atomic<std::uint64_t> ReadsServed { 0 };
std::atomic<std::uint64_t> SequenceReads { 0 };
std::atomic<std::int32_t>  LastSequenceSkill { 0 };
std::atomic<std::uint32_t> LastSequenceUnitType { 0 };

auto __fastcall HookStatCallback(
	std::uint64_t bankByte,
	std::int32_t  statId,
	std::int32_t  fieldCode,
	std::uint64_t reserved,
	std::uint64_t context) noexcept -> std::int64_t {

	if (statId == ActiveStatId.load(std::memory_order_relaxed) && context != 0) {
		const auto* unit = Load<const std::uint8_t*>(reinterpret_cast<const std::uint8_t*>(context) + ContextUnitOffset);
		if (unit != nullptr) {
			std::uint32_t      unitType = 0;
			const std::int32_t skillId  = ReadSequenceSkill(unit, unitType);

			ReadsServed.fetch_add(1, std::memory_order_relaxed);
			if (skillId != 0) {
				SequenceReads.fetch_add(1, std::memory_order_relaxed);
				LastSequenceSkill.store(skillId, std::memory_order_relaxed);
				LastSequenceUnitType.store(unitType, std::memory_order_relaxed);
			}
			return skillId;
		}
	}

	// Everything else, including a null context or a context with no unit,
	// goes down the stock path untouched. The stock callback has its own
	// diagnostics for those two cases and swallowing them here would hide a
	// real bug.
	const StatCallbackFn stock = StockStatCallback.load(std::memory_order_acquire);
	return stock != nullptr ? stock(bankByte, statId, fieldCode, reserved, context) : 0;
}

// The callback table exactly as the stock image holds it at the given base.
auto BuildStockTable(std::uintptr_t base) noexcept -> std::array<std::uint8_t, BbeRecordCount * BbeRecordSize> {
	std::array<std::uint8_t, BbeRecordCount * BbeRecordSize> table {};
	for (std::size_t index = 0; index < BbeRecordCount; ++index) {
		const std::uint64_t callback = static_cast<std::uint64_t>(base) + BbeRecords[index].callbackRva;
		const std::uint32_t arity    = BbeRecords[index].arity;
		std::memcpy(table.data() + index * BbeRecordSize, &callback, sizeof(callback));
		std::memcpy(table.data() + index * BbeRecordSize + 8, &arity, sizeof(arity));
	}
	return table;
}

// --- core end ---------------------------------------------------------------

struct SequenceSkillConfig {
	bool         enabled { true };
	std::int32_t statId { StatIdNotSet };
};

SequenceSkillConfig        Config {};
bool                       ConfigFileWasRead = false;
const D2RL::PluginContext* Context           = nullptr;
std::uintptr_t             Base              = 0;
const char*                FailedWitness     = nullptr;

// Kept identical to the shipped celestialrayone.sequence-skill-stat.toml.
// This is what EnsureConfig writes when the file does not exist yet.
constexpr const char* DefaultConfigToml =
R"toml(# Sequence Skill Stat - which skill is running the current sequence, for softcode
#
# Adds one virtual stat that any .txt formula can read with stat(). While the
# unit the formula is evaluated for is in a sequence, the stat reads as the
# skills.txt id of the skill running that sequence. Otherwise it reads 0.
#
# "In a sequence" means player mode 18 or monster mode 14: Whirlwind, Charge,
# Mirrored Blades and every other skill that puts the unit into a sequence.
# The skill reported is the one the game itself uses to pick the sequence's
# animation, so two skills that share a seqnum are still told apart.
# Monsters count too (hirelings, summons, bound demons), through mode 14.
# Every other kind of unit always reads 0.
#
# Setup:
#   1. Add a row to itemstatcost.txt whose Stat name is the name your formulas
#      will use, for example sequenceskill. The row only gives formulas a name
#      for the id: this plugin answers every formula read of it and never
#      stores a value on any unit, so the row needs nothing else.
#   2. Put that row's id in stat_id below, then restart the game.
#
# Examples (151 = Whirlwind and 107 = Charge in vanilla skills.txt):
#   (stat('sequenceskill'.accr)==151)?50:0
#       50 while whirlwinding, 0 otherwise
#   (stat('sequenceskill'.accr)==151)?50:((stat('sequenceskill'.accr)==107)?30:0)
#       50 while whirlwinding, 30 while charging, 0 otherwise
#   (stat('sequenceskill'.accr)>0)?1:0
#       1 during any sequence
#
# Notes:
#   - .accr, .base and .mod all return the same value.
#   - The value is worked out at the moment a formula reads it, so a formula
#     sees it only when that formula is evaluated.
#   - Runs alongside playermode-exposing: each plugin answers only its own id.
#
# This file lives at <scope>\d2rloader\config\celestialrayone.sequence-skill-stat.toml
# and is created with these defaults on first run. Delete it to get them back.
# It is read once at plugin load, so restart the game after editing.
#
# Type `sequence-skill-stat` in the console to see whether the stat is live,
# how often formulas read it and what it last answered during a sequence.


[sequence-skill-stat]

# Master switch.
#   true  - the stat answers as described above.
#   false - nothing is installed. The console command still reports.
# Default: true
enabled = true

# The itemstatcost.txt row id the stat answers on.
#
# -1 means not set, and nothing is installed until you set it. There is no
# guessed default on purpose: if this pointed at a row that carries a real
# stat, every formula read of that stat would get this plugin's answer
# instead, and the plugin has no way to notice.
#
# Must be between 0 and 32767, or -1.
# Default: -1
stat_id = -1
)toml";

enum class InstallState : std::uint8_t {
	NotAttempted,
	Installed,
	DisabledByConfig,
	NotConfigured,
	UnsupportedBuild,
	SlotTakenByOther,
	InstallFailed,
};

std::atomic<InstallState> State { InstallState::NotAttempted };

void LoadConfiguration() noexcept {
	if (!Context->EnsureConfig(DefaultConfigToml)) {
		Context->LogWarn(
			"Could not create or open celestialrayone.sequence-skill-stat.toml. "
			"Running with defaults: enabled, stat id not set.");
		return;
	}

	std::array<char, 8192> buffer {};
	std::uint32_t          requiredSize = 0;

	if (!Context->ReadConfig(buffer.data(), static_cast<std::uint32_t>(buffer.size() - 1), &requiredSize)) {
		Context->LogWarn(
			"Could not read celestialrayone.sequence-skill-stat.toml. "
			"Running with defaults: enabled, stat id not set.");
		return;
	}

	if (requiredSize >= buffer.size()) {
		D2RL::LogWarnF(
			Context,
			"celestialrayone.sequence-skill-stat.toml is %u bytes, larger than the %zu byte read buffer. "
			"Running with defaults: enabled, stat id not set.",
			requiredSize,
			buffer.size());
		return;
	}

	buffer[buffer.size() - 1] = '\0';
	ConfigFileWasRead         = true;

	(void)ReadConfigBool(buffer.data(), "sequence-skill-stat", "enabled", Config.enabled);

	std::int32_t requestedStatId = Config.statId;
	if (ReadConfigInt(buffer.data(), "sequence-skill-stat", "stat_id", requestedStatId)) {
		if (requestedStatId == StatIdNotSet
			|| (requestedStatId >= MinimumStatId && requestedStatId <= MaximumStatId)) {
			Config.statId = requestedStatId;
		} else {
			D2RL::LogWarnF(
				Context,
				"stat_id %d is outside the allowed range %d..%d. Treating it as not set.",
				requestedStatId,
				MinimumStatId,
				MaximumStatId);
			Config.statId = StatIdNotSet;
		}
	}

	D2RL::LogInfoF(
		Context,
		"config: enabled=%s, stat_id=%d.",
		Config.enabled ? "true" : "false",
		Config.statId);
}

auto CheckTable(const std::array<std::uint8_t, BbeRecordCount * BbeRecordSize>& table) noexcept -> InstallState {
	if (Context->CheckExpectedBytes(BbeCallbackTableRva, table.data(), static_cast<std::uint32_t>(table.size()))) {
		return InstallState::Installed;
	}

	// Tell a foreign redirect of the stat() slot apart from a different build:
	// check everything except the 8-byte stat() callback pointer.
	const std::uint64_t slotOffset = StatRecordIndex * BbeRecordSize;
	const bool before = Context->CheckExpectedBytes(BbeCallbackTableRva, table.data(), static_cast<std::uint32_t>(slotOffset));
	const bool after  = Context->CheckExpectedBytes(
		BbeCallbackTableRva + slotOffset + 8,
		table.data() + slotOffset + 8,
		static_cast<std::uint32_t>(table.size() - slotOffset - 8));
	return before && after ? InstallState::SlotTakenByOther : InstallState::UnsupportedBuild;
}

void Install() noexcept {
	if (!Config.enabled) {
		State.store(InstallState::DisabledByConfig, std::memory_order_release);
		Context->LogInfo("Sequence skill stat not installed: turned off in the config file.");
		return;
	}

	if (Config.statId == StatIdNotSet) {
		State.store(InstallState::NotConfigured, std::memory_order_release);
		Context->LogWarn(
			"Sequence skill stat not installed: stat_id is not set. Add a row to itemstatcost.txt, put its id "
			"in celestialrayone.sequence-skill-stat.toml and restart the game.");
		return;
	}

	for (const Witness& witness : Witnesses) {
		if (!Context->CheckExpectedBytes(witness.rva, witness.bytes, witness.size)) {
			FailedWitness = witness.what;
			State.store(InstallState::UnsupportedBuild, std::memory_order_release);
			D2RL::LogErrorF(
				Context,
				"Sequence skill stat NOT installed: RVA %08llX (%s) does not match the 1.3.1 image. Nothing was patched.",
				static_cast<unsigned long long>(witness.rva),
				witness.what);
			return;
		}
	}

	const auto         table = BuildStockTable(Base);
	const InstallState check = CheckTable(table);
	if (check != InstallState::Installed) {
		State.store(check, std::memory_order_release);
		if (check == InstallState::SlotTakenByOther) {
			Context->LogError(
				"Sequence skill stat NOT installed: the formula stat() slot at RVA 01D09CA0 already points somewhere "
				"else, so another plugin redirected it first. Nothing was patched.");
		} else {
			FailedWitness = "formula callback table";
			Context->LogError(
				"Sequence skill stat NOT installed: the formula callback table at RVA 01D09C50 does not match the "
				"1.3.1 image. Nothing was patched.");
		}
		return;
	}

	const std::uint64_t stock       = static_cast<std::uint64_t>(Base) + StatCallbackRva;
	const std::uint64_t replacement = static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(&HookStatCallback));

	// Publish the pass-through and the id before the slot can reach the handler.
	StockStatCallback.store(reinterpret_cast<StatCallbackFn>(static_cast<std::uintptr_t>(stock)), std::memory_order_release);
	ActiveStatId.store(Config.statId, std::memory_order_release);

	if (!Context->PatchWriteU64(StatSlotRva, &stock, static_cast<std::uint32_t>(sizeof(stock)), replacement)) {
		State.store(InstallState::InstallFailed, std::memory_order_release);
		Context->LogError("Sequence skill stat NOT installed: the loader refused to write the stat() slot at RVA 01D09CA0.");
		return;
	}

	if (!Context->CheckExpectedBytes(StatSlotRva, &replacement, static_cast<std::uint32_t>(sizeof(replacement)))) {
		State.store(InstallState::InstallFailed, std::memory_order_release);
		Context->LogError("Sequence skill stat NOT installed: the stat() slot did not read back as written.");
		return;
	}

	State.store(InstallState::Installed, std::memory_order_release);
	D2RL::LogInfoF(Context, "Sequence skill stat installed: stat() slot redirected, answering on stat id %d.", Config.statId);
}

void Uninstall() noexcept {
	if (Context == nullptr || State.load(std::memory_order_acquire) != InstallState::Installed) {
		return;
	}

	const std::uint64_t stock = static_cast<std::uint64_t>(Base) + StatCallbackRva;
	const std::uint64_t ours  = static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(&HookStatCallback));

	// Only put the stock pointer back if the slot is still ours. If someone
	// changed it after us, their chain is theirs to undo.
	if (!Context->CheckExpectedBytes(StatSlotRva, &ours, static_cast<std::uint32_t>(sizeof(ours)))) {
		Context->LogWarn("The stat() slot was changed by someone else after this plugin; left as it is.");
		return;
	}

	if (Context->PatchWriteU64(StatSlotRva, &ours, static_cast<std::uint32_t>(sizeof(ours)), stock)) {
		State.store(InstallState::NotAttempted, std::memory_order_release);
	} else {
		Context->LogError("Could not restore the stat() slot at RVA 01D09CA0.");
	}
}

auto UnitTypeName(std::uint32_t unitType) noexcept -> const char* {
	return unitType == UnitTypePlayer ? "player" : (unitType == UnitTypeMonster ? "monster" : "other unit");
}

auto SequenceSkillStatCommand(
	D2R::Game::Client*                 client,
	const D2RL::ConsoleCommandContext* command,
	void*                              userData) noexcept -> D2RL::ConsoleCommandResult {
	(void)client;
	(void)userData;

	if (command == nullptr || command->plugin == nullptr) {
		return D2RL::ConsoleCommandResult::Failed;
	}

	const D2RL::PluginContext* context = command->plugin;

	char header[176] {};
	if (Config.statId == StatIdNotSet) {
		std::snprintf(
			header,
			sizeof(header),
			"sequence-skill-stat: config %s, stat id not set.",
			ConfigFileWasRead ? "loaded" : "NOT READ (defaults in use)");
	} else {
		std::snprintf(
			header,
			sizeof(header),
			"sequence-skill-stat: config %s, stat id %d.",
			ConfigFileWasRead ? "loaded" : "NOT READ (defaults in use)",
			Config.statId);
	}
	context->WriteConsoleMessage(header);

	char message[256] {};
	switch (State.load(std::memory_order_acquire)) {
	case InstallState::Installed: {
		const auto reads    = ReadsServed.load(std::memory_order_relaxed);
		const auto sequence = SequenceReads.load(std::memory_order_relaxed);
		if (sequence == 0) {
			std::snprintf(
				message,
				sizeof(message),
				"active: formulas read the stat %llu times, none of them during a sequence yet.",
				static_cast<unsigned long long>(reads));
		} else {
			std::snprintf(
				message,
				sizeof(message),
				"active: formulas read the stat %llu times, %llu of them during a sequence. Last sequence answer: skill %d on a %s.",
				static_cast<unsigned long long>(reads),
				static_cast<unsigned long long>(sequence),
				LastSequenceSkill.load(std::memory_order_relaxed),
				UnitTypeName(LastSequenceUnitType.load(std::memory_order_relaxed)));
		}
		context->WriteConsoleMessage(message);
		break;
	}

	case InstallState::DisabledByConfig:
		context->WriteConsoleWarning("NOT ACTIVE: the config file turns it off. The game build is fine.");
		break;

	case InstallState::NotConfigured:
		context->WriteConsoleWarning(
			"NOT ACTIVE: stat_id is not set. Add an itemstatcost.txt row, put its id in the config file and restart.");
		break;

	case InstallState::UnsupportedBuild:
		std::snprintf(
			message,
			sizeof(message),
			"NOT ACTIVE: this game build is not recognised (%s does not match). Nothing was patched.",
			FailedWitness != nullptr ? FailedWitness : "a checked range");
		context->WriteConsoleError(message);
		break;

	case InstallState::SlotTakenByOther:
		context->WriteConsoleError(
			"NOT ACTIVE: another plugin already redirected the formula stat() slot. Nothing was patched.");
		break;

	case InstallState::InstallFailed:
		context->WriteConsoleError("NOT ACTIVE: the bytes matched but the stat() slot could not be written. See the plugin log.");
		break;

	case InstallState::NotAttempted:
	default:
		context->WriteConsoleError("NOT ACTIVE: installation was never attempted.");
		break;
	}

	return D2RL::ConsoleCommandResult::Handled;
}

constexpr D2RL::PluginInfo SequenceSkillStatInfo {
	.infoSize    = D2RL::PluginInfoSize,
	.abiVersion  = D2RL_PLUGIN_ABI_VERSION,
	.id          = "celestialrayone.sequence-skill-stat",
	.name        = "Sequence Skill Stat",
	.version     = "0.1.0",
	.author      = "CelestialRayOne",
	.description = "Exposes the skill running a unit's sequence to .txt formulas as a configurable virtual stat id.",
	// Shared, not Client or Server: .txt formulas are evaluated on both sides
	// and the two must agree on what the virtual stat returns. NativeHooks
	// because the game calls straight into this module through the slot.
	.flags       = D2RL::PluginFlags::Shared | D2RL::PluginFlags::NativeHooks,
};

static_assert(D2RL::HasValidPluginRole(SequenceSkillStatInfo.flags), "Exactly one execution role must be set.");
static_assert(D2RL::HasOnlyKnownPluginFlags(SequenceSkillStatInfo.flags), "Unknown plugin flag set.");

} // namespace

D2RL_PLUGIN_EXPORT auto D2RLoaderGetPluginInfo() noexcept -> const D2RL::PluginInfo* {
	return &SequenceSkillStatInfo;
}

D2RL_PLUGIN_EXPORT auto D2RLoaderLoadPlugin(const D2RL::PluginContext* context) noexcept -> bool {
	if (context == nullptr) {
		return false;
	}

	Context = context;
	Base    = context->exeBase;

	// Registered before anything is installed on purpose. Returning false from
	// this function unloads the DLL and takes the console command with it,
	// which would leave a user with no in-game signal at all. This plugin
	// would rather stay loaded and be able to say that it did nothing.
	if (!context->RegisterConsoleCommand(
			"sequence-skill-stat",
			SequenceSkillStatCommand,
			"Report whether the sequence skill stat is active and what it last answered.")) {
		context->LogWarn("The sequence-skill-stat console command was not registered.");
	}

	if (const char* build = D2RL::GetBuildVersion(context); build != nullptr) {
		D2RL::LogInfoF(context, "sequence-skill-stat loading against build %s.", build);
	}

	LoadConfiguration();
	Install();

	if (State.load(std::memory_order_acquire) != InstallState::Installed) {
		context->LogWarn("sequence-skill-stat loaded with the virtual stat NOT active.");
		return true;
	}

	context->LogInfo("sequence-skill-stat loaded.");
	return true;
}

D2RL_PLUGIN_EXPORT void D2RLoaderUnloadPlugin() noexcept {
	Uninstall();
}
