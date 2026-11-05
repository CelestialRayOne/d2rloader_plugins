// skill_srcdam_calc_plugin.cpp
//
// Drives skills.txt "SrcDam" from a skills.txt formula column (calc1 .. calc10)
// instead of the static byte in the row.
//
// Port of the 2.4 "skills.txt srcdam -> prgcalc3" three-entry byte patch set to
// D2R 3.3.93847. The 2.4 version needed a hook plus a 140-byte code cave because
// it had to splice into the middle of the skill-start function; 3.3 exposes a
// clean server entry point and a proper formula-evaluator wrapper, so the whole
// thing is one inline hook on a function entry with no cave.
//
// Verified against the D2RLoader image (base 0x140000000):
//   0x0043ACB0  D2GAME_SKILLS_ServerDoHandler(game, unit, skillId, skillLevel,
//                                             consumeResources, itemCast, itemEffect)
//               Resolves the skills.txt row at entry, then dispatches srvdofunc.
//   0x00097790  DATATBLS_GetSkillsTxtRecordForContext(dataContext, skillId)
//               Record stride 0x2EC (748 bytes).
//   0x003B5160  SKILLS_EvaluateSkillFormula(dataContext, unit, calcHandle,
//                                           skillId, skillLevel)
//               Bounds-checks the handle against the expression pool itself and
//               returns 0 for a blank or out-of-range cell.
//   0x003BFFC1  cmovne ecx, eax inside the missile damage init (0x003BF9B0).
//               Vanilla discards the skills.txt srcdam whenever the missile's own
//               missiles.txt SrcDamage cell is blank (0xFF).
//
// Record offsets, taken from the skills.txt column table in the loader at
// 0x00302380 and cross-checked against the code that reads them:
//   SrcDam  byte  at 589   (read at 0x003BFEF8)
//   calc1 .. calc10 dwords at 400, 404, 408, 412, 416, 420, 424, 428, 432, 436
//
// The SrcDam byte lives in the shared compiled skills table, so a written value
// stays there until something writes it again. The row's static SrcDam is
// captured the first time the plugin touches it, put back whenever the formula
// evaluates to 0 or less, and put back on unload.

#include <D2RLPlugin/api.h>

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <unordered_map>

namespace {

// ---------------------------------------------------------------------------
// Native addresses and layout
// ---------------------------------------------------------------------------

constexpr std::uint64_t ServerDoHandlerRva      = 0x0043ACB0;
constexpr std::uint64_t GetSkillsTxtRecordRva   = 0x00097790;
constexpr std::uint64_t EvaluateSkillFormulaRva = 0x003B5160;
constexpr std::uint64_t MissileSrcDamGateRva    = 0x003BFFC1;

constexpr std::uint8_t ExpectedServerDoHandler[] {
	0x48, 0x89, 0x5C, 0x24, 0x10, 0x44, 0x89, 0x4C,
	0x24, 0x20, 0x44, 0x89, 0x44, 0x24, 0x18, 0x55,
};

constexpr std::uint8_t ExpectedGetSkillsTxtRecord[] {
	0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x74,
	0x24, 0x20, 0x57, 0x48, 0x83, 0xEC, 0x30,
};

constexpr std::uint8_t ExpectedEvaluateSkillFormula[] {
	0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x6C,
	0x24, 0x10, 0x48, 0x89, 0x74, 0x24, 0x20, 0x57,
};

// cmovne ecx, eax  ->  mov ecx, eax ; nop
constexpr std::uint8_t ExpectedMissileSrcDamGate[] { 0x0F, 0x45, 0xC8 };
constexpr std::uint8_t UngatedMissileSrcDam[]      { 0x8B, 0xC8, 0x90 };

constexpr std::size_t GameDataContextOffset = 0x106;
constexpr std::size_t SrcDamOffset          = 589;
constexpr std::size_t Calc1Offset           = 400;
constexpr std::size_t CalcStride            = 4;

constexpr int CalcColumnMin   = 1;
constexpr int CalcColumnMax   = 10;
constexpr int CalcColumnDefault = 10;
constexpr int SrcDamMax       = 255;

using ServerDoHandlerFn = std::int32_t(__fastcall*)(
	void*         game,
	void*         unit,
	std::int32_t  skillId,
	std::uint32_t skillLevel,
	std::int32_t  consumeResources,
	std::int32_t  itemCast,
	std::int32_t  itemEffect) noexcept;

using GetSkillsTxtRecordFn = void*(__fastcall*)(std::uint8_t dataContext, std::int32_t skillId) noexcept;

using EvaluateSkillFormulaFn = std::int32_t(__fastcall*)(
	std::uint8_t  dataContext,
	void*         unit,
	std::uint32_t calcHandle,
	std::int32_t  skillId,
	std::int32_t  skillLevel) noexcept;

// ---------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------

struct PluginConfig {
	bool enabled        = true;
	int  calcColumn     = CalcColumnDefault;
	bool ungateMissiles = true;
	bool logApplied     = false;
};

PluginConfig g_config {};

const D2RL::PluginContext* g_context              = nullptr;
ServerDoHandlerFn          g_originalServerDo     = nullptr;
GetSkillsTxtRecordFn       g_getSkillsTxtRecord   = nullptr;
EvaluateSkillFormulaFn     g_evaluateSkillFormula = nullptr;

std::size_t   g_calcOffset    = Calc1Offset + (CalcColumnDefault - 1) * CalcStride;
bool          g_hookInstalled = false;
bool          g_missileUngated = false;

// Static SrcDam of every row this plugin has touched, keyed by the address of
// the byte in the compiled table. Captured before the first write.
std::unordered_map<std::uintptr_t, std::uint8_t> g_staticSrcDam;

// Last applied write, so the console command can prove the path is live.
std::uint64_t g_appliedCount  = 0;
std::int32_t  g_lastSkillId   = -1;
std::int32_t  g_lastSkillLvl  = -1;
std::int32_t  g_lastValue     = -1;
bool          g_lastWasStatic = false;

constexpr auto ByteSize(std::size_t size) noexcept -> std::uint32_t {
	return static_cast<std::uint32_t>(size);
}

template <std::size_t Size>
constexpr auto ByteCount(const std::uint8_t (&)[Size]) noexcept -> std::uint32_t {
	return ByteSize(Size);
}

// ---------------------------------------------------------------------------
// Minimal TOML reader (flat key = value inside one table)
// ---------------------------------------------------------------------------

struct TextSpan {
	const char* data = nullptr;
	std::size_t size = 0;
};

constexpr auto IsSpace(char c) noexcept -> bool {
	return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

constexpr auto ToLower(char c) noexcept -> char {
	return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
}

auto Trim(TextSpan span) noexcept -> TextSpan {
	while (span.size > 0 && IsSpace(span.data[0])) {
		++span.data;
		--span.size;
	}
	while (span.size > 0 && IsSpace(span.data[span.size - 1])) {
		--span.size;
	}
	return span;
}

auto EqualsIgnoreCase(TextSpan span, const char* literal) noexcept -> bool {
	std::size_t index = 0;
	for (; index < span.size && literal[index] != '\0'; ++index) {
		if (ToLower(span.data[index]) != ToLower(literal[index])) {
			return false;
		}
	}
	return index == span.size && literal[index] == '\0';
}

auto FindTomlValue(const char* toml, const char* table, const char* key, TextSpan* value) noexcept -> bool {
	if (toml == nullptr || key == nullptr || value == nullptr) {
		return false;
	}

	bool        inTable = (table == nullptr);
	const char* cursor  = toml;

	while (*cursor != '\0') {
		const char* lineEnd = cursor;
		while (*lineEnd != '\0' && *lineEnd != '\n') {
			++lineEnd;
		}

		TextSpan line { cursor, static_cast<std::size_t>(lineEnd - cursor) };
		for (std::size_t index = 0; index < line.size; ++index) {
			if (line.data[index] == '#') {
				line.size = index;
				break;
			}
		}
		line = Trim(line);

		if (line.size > 0) {
			if (line.data[0] == '[') {
				TextSpan name { line.data + 1, line.size >= 2 ? line.size - 2 : 0 };
				inTable = (table == nullptr) || EqualsIgnoreCase(Trim(name), table);
			} else if (inTable) {
				for (std::size_t index = 0; index < line.size; ++index) {
					if (line.data[index] != '=') {
						continue;
					}

					const TextSpan name = Trim(TextSpan { line.data, index });
					TextSpan       raw  = Trim(TextSpan { line.data + index + 1, line.size - index - 1 });
					if (EqualsIgnoreCase(name, key)) {
						if (raw.size >= 2 && (raw.data[0] == '"' || raw.data[0] == '\'') && raw.data[raw.size - 1] == raw.data[0]) {
							raw.data += 1;
							raw.size -= 2;
						}
						*value = raw;
						return true;
					}
					break;
				}
			}
		}

		cursor = (*lineEnd == '\0') ? lineEnd : lineEnd + 1;
	}

	return false;
}

auto ParseBool(TextSpan span, bool fallback) noexcept -> bool {
	if (EqualsIgnoreCase(span, "true") || EqualsIgnoreCase(span, "1") || EqualsIgnoreCase(span, "yes") || EqualsIgnoreCase(span, "on")) {
		return true;
	}
	if (EqualsIgnoreCase(span, "false") || EqualsIgnoreCase(span, "0") || EqualsIgnoreCase(span, "no") || EqualsIgnoreCase(span, "off")) {
		return false;
	}
	return fallback;
}

auto ParseInt(TextSpan span, int fallback) noexcept -> int {
	if (span.size == 0) {
		return fallback;
	}

	std::size_t index    = 0;
	bool        negative = false;
	if (span.data[0] == '+' || span.data[0] == '-') {
		negative = span.data[0] == '-';
		index    = 1;
	}
	if (index >= span.size) {
		return fallback;
	}

	long long accumulator = 0;
	for (; index < span.size; ++index) {
		const char digit = span.data[index];
		if (digit < '0' || digit > '9') {
			return fallback;
		}
		accumulator = accumulator * 10 + (digit - '0');
		if (accumulator > 1'000'000) {
			return fallback;
		}
	}

	return static_cast<int>(negative ? -accumulator : accumulator);
}

// ---------------------------------------------------------------------------
// Config
// ---------------------------------------------------------------------------

constexpr const char* ConfigTable = "srcdam";

constexpr const char* DefaultConfigToml = R"TOML(# Skill SrcDam from Calc
#
# Makes skills.txt "SrcDam" come from one of the skills.txt formula columns
# instead of the fixed number in the SrcDam cell.
#
# HOW TO USE IT
#   Put a formula in the chosen calc column of any skills.txt row. Every time
#   that skill starts, the formula is evaluated in the caster's context and the
#   result is used as that skill's SrcDam (percent of weapon damage, 128 = 100%).
#   Leave the column blank and nothing changes: the row keeps whatever static
#   number is in its SrcDam cell. A formula that evaluates to 0 or less is also
#   ignored, so you can gate it on a condition and fall back to the static value.
#
#   The result is clamped to 255, because SrcDam is a single byte in the game's
#   compiled skills table. 255 is about 199% weapon damage.
#
# WHICH COLUMN TO PICK
#   calc1 through calc10 all exist and all accept a formula.
#   calc1 to calc6 are also read by hardcoded skill functions on some skills
#   (Static Field's server function reads calc1 and calc2, for example), so a
#   skill that uses them for its own behaviour will fight over the cell.
#   calc7 to calc10 are not read by anything hardcoded, so use one of those
#   unless you have a reason not to.

[srcdam]

# Master switch. false leaves the game completely untouched.
enabled = true

# Which skills.txt column holds the SrcDam formula. 1 to 10 = calc1 to calc10.
calc_column = 10

# Missiles only read skills.txt SrcDam when the missile's own missiles.txt
# "SrcDamage" cell is filled in. A blank cell there means "throw the skill's
# SrcDam away", which silently kills the formula on most missile skills.
# true removes that condition so the skill's SrcDam always reaches the missile.
#
# This does not touch the other branch, where missiles.txt SrcDamage is itself
# the damage source (missiles with no Skill and no MissileSkill flag). Those
# behave exactly as before either way.
ungate_missiles = true

# Writes one log line per skill start that actually changed a SrcDam byte.
# Useful once, noisy forever. Leave it false for normal play.
log_applied = false
)TOML";

void LoadConfig(const D2RL::PluginContext* context) noexcept {
	char          buffer[8192] {};
	std::uint32_t requiredSize = 0;
	if (!context->ReadConfig(buffer, ByteSize(sizeof(buffer)), &requiredSize)) {
		context->LogWarn("could not read the config file, using built-in defaults.");
		return;
	}

	TextSpan value {};
	if (FindTomlValue(buffer, ConfigTable, "enabled", &value)) {
		g_config.enabled = ParseBool(value, g_config.enabled);
	}
	if (FindTomlValue(buffer, ConfigTable, "calc_column", &value)) {
		g_config.calcColumn = ParseInt(value, g_config.calcColumn);
	}
	if (FindTomlValue(buffer, ConfigTable, "ungate_missiles", &value)) {
		g_config.ungateMissiles = ParseBool(value, g_config.ungateMissiles);
	}
	if (FindTomlValue(buffer, ConfigTable, "log_applied", &value)) {
		g_config.logApplied = ParseBool(value, g_config.logApplied);
	}

	if (g_config.calcColumn < CalcColumnMin || g_config.calcColumn > CalcColumnMax) {
		char message[160] {};
		std::snprintf(message,
		              sizeof(message),
		              "calc_column = %d is out of range, falling back to calc%d.",
		              g_config.calcColumn,
		              CalcColumnDefault);
		context->LogWarn(message);
		g_config.calcColumn = CalcColumnDefault;
	}

	g_calcOffset = Calc1Offset + static_cast<std::size_t>(g_config.calcColumn - 1) * CalcStride;
}

// ---------------------------------------------------------------------------
// The actual work
// ---------------------------------------------------------------------------

void ApplyCalcToSrcDam(void* game, void* unit, std::int32_t skillId, std::uint32_t skillLevel) noexcept {
	if (game == nullptr || unit == nullptr) {
		return;
	}
	if (g_getSkillsTxtRecord == nullptr || g_evaluateSkillFormula == nullptr) {
		return;
	}

	const std::uint8_t dataContext = *(static_cast<const std::uint8_t*>(game) + GameDataContextOffset);

	auto* record = static_cast<std::uint8_t*>(g_getSkillsTxtRecord(dataContext, skillId));
	if (record == nullptr) {
		return;
	}

	// A blank formula cell reads back as 0 or as -1 depending on the column, and
	// the evaluator rejects anything past the end of the expression pool anyway.
	std::uint32_t calcHandle = 0;
	std::memcpy(&calcHandle, record + g_calcOffset, sizeof(calcHandle));
	if (calcHandle == 0U || calcHandle == 0xFFFFFFFFU) {
		return;
	}

	std::uint8_t* const srcDam = record + SrcDamOffset;

	// First touch of this row: remember its static SrcDam before anything is
	// written, so a later 0-or-less result can put it back.
	std::uint8_t staticSrcDam = *srcDam;
	try {
		staticSrcDam = g_staticSrcDam.try_emplace(reinterpret_cast<std::uintptr_t>(srcDam), *srcDam).first->second;
	} catch (...) {
		// Out of memory: the row could not be restored later, so leave it alone.
		return;
	}

	const std::int32_t value = g_evaluateSkillFormula(dataContext, unit, calcHandle, skillId, static_cast<std::int32_t>(skillLevel));

	const bool         useStatic = value <= 0;
	const std::uint8_t newValue  = useStatic ? staticSrcDam : static_cast<std::uint8_t>(value > SrcDamMax ? SrcDamMax : value);
	const bool         changed   = *srcDam != newValue;

	*srcDam = newValue;

	++g_appliedCount;
	g_lastSkillId   = skillId;
	g_lastSkillLvl  = static_cast<std::int32_t>(skillLevel);
	g_lastValue     = newValue;
	g_lastWasStatic = useStatic;

	if (changed && g_config.logApplied && g_context != nullptr) {
		char message[160] {};
		std::snprintf(message,
		              sizeof(message),
		              useStatic ? "skill %d level %d: calc%d <= 0 -> static SrcDam %d"
		                        : "skill %d level %d: calc%d -> SrcDam %d",
		              skillId,
		              g_lastSkillLvl,
		              g_config.calcColumn,
		              g_lastValue);
		g_context->LogInfo(message);
	}
}

void RestoreStaticSrcDam() noexcept {
	for (const auto& [address, staticValue] : g_staticSrcDam) {
		*reinterpret_cast<std::uint8_t*>(address) = staticValue;
	}
	g_staticSrcDam.clear();
}

auto __fastcall HookServerDoHandler(
	void*         game,
	void*         unit,
	std::int32_t  skillId,
	std::uint32_t skillLevel,
	std::int32_t  consumeResources,
	std::int32_t  itemCast,
	std::int32_t  itemEffect) noexcept -> std::int32_t {
	// The byte has to be in place before the original dispatches srvdofunc,
	// because that is what creates the missile that reads it.
	ApplyCalcToSrcDam(game, unit, skillId, skillLevel);

	const ServerDoHandlerFn original = g_originalServerDo;
	return original != nullptr ? original(game, unit, skillId, skillLevel, consumeResources, itemCast, itemEffect) : 0;
}

// ---------------------------------------------------------------------------
// Console command
// ---------------------------------------------------------------------------

auto SrcDamCalcCommand(D2R::Game::Client* client, const D2RL::ConsoleCommandContext* command, void* userData) noexcept -> D2RL::ConsoleCommandResult {
	(void)client;
	(void)userData;

	if (command == nullptr || command->plugin == nullptr) {
		return D2RL::ConsoleCommandResult::Failed;
	}

	char message[224] {};

	if (!g_config.enabled) {
		command->plugin->WriteConsoleMessage("skill srcdam from calc: disabled in config.");
		return D2RL::ConsoleCommandResult::Handled;
	}

	std::snprintf(message,
	              sizeof(message),
	              "source column: skills.txt calc%d (record offset %zu), hook %s, missile ungate %s",
	              g_config.calcColumn,
	              g_calcOffset,
	              g_hookInstalled ? "installed" : "NOT installed",
	              g_missileUngated ? "applied" : "not applied");
	command->plugin->WriteConsoleMessage(message);

	if (g_appliedCount == 0) {
		command->plugin->WriteConsoleMessage("no skill has written a SrcDam value yet.");
	} else {
		std::snprintf(message,
		              sizeof(message),
		              "%llu writes so far, last: skill %d level %d -> SrcDam %d%s",
		              static_cast<unsigned long long>(g_appliedCount),
		              g_lastSkillId,
		              g_lastSkillLvl,
		              g_lastValue,
		              g_lastWasStatic ? " (static, formula <= 0)" : "");
		command->plugin->WriteConsoleMessage(message);
	}

	return D2RL::ConsoleCommandResult::Handled;
}

// ---------------------------------------------------------------------------
// Install
// ---------------------------------------------------------------------------

auto VerifyNativeEntries(const D2RL::PluginContext* context) noexcept -> bool {
	if (!context->CheckExpectedBytes(ServerDoHandlerRva, ExpectedServerDoHandler, ByteCount(ExpectedServerDoHandler))) {
		context->LogError("skill-start handler entry bytes did not match, refusing to hook.");
		return false;
	}
	if (!context->CheckExpectedBytes(GetSkillsTxtRecordRva, ExpectedGetSkillsTxtRecord, ByteCount(ExpectedGetSkillsTxtRecord))) {
		context->LogError("skills.txt record getter entry bytes did not match, refusing to hook.");
		return false;
	}
	if (!context->CheckExpectedBytes(EvaluateSkillFormulaRva, ExpectedEvaluateSkillFormula, ByteCount(ExpectedEvaluateSkillFormula))) {
		context->LogError("skill formula evaluator entry bytes did not match, refusing to hook.");
		return false;
	}
	return true;
}

void ApplyMissileUngate(const D2RL::PluginContext* context) noexcept {
	if (!g_config.ungateMissiles) {
		return;
	}

	if (!context->CheckExpectedBytes(MissileSrcDamGateRva, ExpectedMissileSrcDamGate, ByteCount(ExpectedMissileSrcDamGate))) {
		context->LogWarn("missile SrcDam gate bytes did not match, leaving the missile path alone.");
		return;
	}

	if (!context->PatchBytes(MissileSrcDamGateRva,
	                         ExpectedMissileSrcDamGate,
	                         ByteCount(ExpectedMissileSrcDamGate),
	                         UngatedMissileSrcDam,
	                         ByteCount(UngatedMissileSrcDam))) {
		context->LogWarn("could not write the missile SrcDam ungate patch.");
		return;
	}

	g_missileUngated = true;
}

constexpr D2RL::PluginInfo SkillSrcDamCalcInfo {
	.infoSize    = D2RL::PluginInfoSize,
	.abiVersion  = D2RL_PLUGIN_ABI_VERSION,
	.id          = "celestialrayone.skill-srcdam-calc",
	.name        = "Skill SrcDam from Calc",
	.version     = "1.0.1",
	.author      = "CelestialRayOne",
	.description = "Drives skills.txt SrcDam from a skills.txt calc1-calc10 formula evaluated in the caster's context.",
	.flags       = D2RL::PluginFlags::Server | D2RL::PluginFlags::NativeHooks,
};

}

D2RL_PLUGIN_EXPORT auto D2RLoaderGetPluginInfo() noexcept -> const D2RL::PluginInfo* {
	return &SkillSrcDamCalcInfo;
}

D2RL_PLUGIN_EXPORT auto D2RLoaderLoadPlugin(const D2RL::PluginContext* context) noexcept -> bool {
	if (context == nullptr) {
		return false;
	}

	g_context = context;

	if (!context->EnsureConfig(DefaultConfigToml)) {
		context->LogWarn("could not create the config file, using built-in defaults.");
	}
	LoadConfig(context);

	char message[192] {};

	if (!g_config.enabled) {
		context->LogInfo("disabled in config, nothing was hooked or patched.");
		return context->RegisterConsoleCommand("srcdam-calc", SrcDamCalcCommand, "Show skill SrcDam from calc status.");
	}

	if (!VerifyNativeEntries(context)) {
		return false;
	}

	g_getSkillsTxtRecord   = reinterpret_cast<GetSkillsTxtRecordFn>(context->exeBase + GetSkillsTxtRecordRva);
	g_evaluateSkillFormula = reinterpret_cast<EvaluateSkillFormulaFn>(context->exeBase + EvaluateSkillFormulaRva);

	if (!context->InstallInlineHook(ServerDoHandlerRva,
	                                ExpectedServerDoHandler,
	                                ByteCount(ExpectedServerDoHandler),
	                                HookServerDoHandler,
	                                &g_originalServerDo)) {
		context->LogError("could not install the skill-start hook.");
		return false;
	}
	g_hookInstalled = true;

	ApplyMissileUngate(context);

	std::snprintf(message,
	              sizeof(message),
	              "SrcDam now comes from skills.txt calc%d; missile ungate %s.",
	              g_config.calcColumn,
	              g_missileUngated ? "applied" : "off");
	context->LogInfo(message);

	return context->RegisterConsoleCommand("srcdam-calc", SrcDamCalcCommand, "Show skill SrcDam from calc status.");
}

D2RL_PLUGIN_EXPORT void D2RLoaderUnloadPlugin() noexcept {
	RestoreStaticSrcDam();
	g_context              = nullptr;
	g_originalServerDo     = nullptr;
	g_getSkillsTxtRecord   = nullptr;
	g_evaluateSkillFormula = nullptr;
}
