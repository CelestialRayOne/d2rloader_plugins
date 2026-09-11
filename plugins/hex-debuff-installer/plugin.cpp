// Hex Debuff Installer (auraeventfunc 36, WarApplyHexDebuff).
//
// See celestialrayone.hex-debuff-installer.toml for the full write-up. In short:
//
//   auraeventfunc 36 puts the skill's auratargetstate on the unit it hits. Only when
//   that state is 217 (hexpurgedebuff, hardcoded: `cmp dword [rsp+74h], 0D9h` at
//   51FFB6, `jne 520060` at 51FFC3) does it also register the skill's Param1 as an
//   event function on that unit for damagedbymissile, damagedinmelee and hextrigger.
//   The registrations carry no installer: sub_140438470 passes a literal 0 to the
//   single-event registrar sub_140438230 (`mov qword [rsp+48h], 0` at 438504), so
//   every node stores installer type 6, id -1.
//
//   auraeventfunc 33 (SkillActivateSubskill) tests aurastatcalc4 by whether the column
//   holds a formula (`cmp dword [rsi+94h], 0` at 42F3A9), not by its value. When it
//   does, the caster is looked up from the node's installer (sub_14048FE80). Type 6
//   finds nothing and the function returns at 42F3CE, before the chance roll and
//   before the cast. So Param1 = 33 on a hex can never cast anything.
//
//   This plugin makes two independent changes:
//
//   1. ANY TARGET STATE
//      NOPs the `jne` at 51FFC3, so Param1 is registered for every auratargetstate,
//      not only hexpurgedebuff.
//
//   2. RECORD INSTALLER
//      Gives the registrar the hex caster as the installer for the registrations
//      auraeventfunc 36 makes. The engine then stores the caster's unit type and id
//      on each node through its own code path.
//
// Built against D2R 3.3 (module 140000000.D2RLoader.exe,
// md5 baf085b077a4f9605bfddd977cfd9207) and PluginSDK v4.

#include <D2RLPlugin/api.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace {

// ---------------------------------------------------------------------------
// RVAs, all read out of the 3.3 image
// ---------------------------------------------------------------------------

// auraeventfunc 36, WarApplyHexDebuff. Table off_14238E5C0, slot 36.
constexpr std::uint64_t HexDebuffRva = 0x0051FDA0;

// Single-event registrar. Its tenth argument is the installer unit; with a unit it
// stores that unit's type and id on the node, with null it leaves type 6, id -1.
constexpr std::uint64_t RegisterEventRva = 0x00438230;

// Prologue of auraeventfunc 36. Sixteen bytes, no rip-relative operands, ends on an
// instruction boundary (the next instruction is `sub rsp, 0A0h` at 51FDB0).
//   48 89 5C 24 10 mov [rsp+10h], rbx / 55 push rbp / 56 push rsi / 57 push rdi
//   41 54 push r12 / 41 55 push r13 / 41 56 push r14 / 41 57 push r15
constexpr std::uint8_t ExpectedHexDebuffPrologue[] {
	0x48, 0x89, 0x5C, 0x24, 0x10, 0x55, 0x56, 0x57, 0x41, 0x54, 0x41, 0x55, 0x41, 0x56, 0x41, 0x57,
};

// Prologue of the registrar. Seventeen bytes, no rip-relative operands, ends on an
// instruction boundary (the next instruction is `sub rsp, 60h` at 438241).
//   48 89 6C 24 08 mov [rsp+8], rbp / 48 89 74 24 10 mov [rsp+10h], rsi
//   48 89 7C 24 18 mov [rsp+18h], rdi / 41 56 push r14
constexpr std::uint8_t ExpectedRegisterEventPrologue[] {
	0x48, 0x89, 0x6C, 0x24, 0x08, 0x48, 0x89, 0x74, 0x24, 0x10, 0x48, 0x89, 0x7C, 0x24, 0x18, 0x41, 0x56,
};

// The hexpurgedebuff gate: cmp dword [rsp+74h], 0D9h / mov ebp, 1 / jne 520060.
// Checked in full before anything is written, so the NOP can only ever land on this
// exact sequence. `mov ebp, 1` is kept: it is the function's return value.
constexpr std::uint64_t HexGateRva    = 0x0051FFB6;
constexpr std::uint64_t HexGateJneRva = 0x0051FFC3;

constexpr std::uint8_t ExpectedHexGate[] {
	0x81, 0x7C, 0x24, 0x74, 0xD9, 0x00, 0x00, 0x00,  // cmp dword [rsp+74h], 0D9h
	0xBD, 0x01, 0x00, 0x00, 0x00,                    // mov ebp, 1
	0x0F, 0x85, 0x97, 0x00, 0x00, 0x00,              // jne 520060
};
constexpr std::uint8_t ExpectedHexGateJne[] { 0x0F, 0x85, 0x97, 0x00, 0x00, 0x00 };

// Group argument auraeventfunc 36 passes to the registrar (`mov [rsp+28h], ebp` with
// ebp = 1 at 520053). The skill id it passes is its own sixth argument
// (`mov r14d, [rsp+108h]` at 51FE21, then `mov r8d, r14d` at 520038).
constexpr std::int32_t HexInstallGroup = 1;

// ---------------------------------------------------------------------------
// Game function types
// ---------------------------------------------------------------------------

// The nine arguments the event dispatcher (sub_1405881E0) passes to every
// auraeventfunc: game, event id, the unit that owns the event node (for a hex, the
// hex caster), the other unit, damage, skill id, skill level, node param and a
// pointer to the node's installer type/id pair.
using HexDebuffFn = std::int64_t(__fastcall*)(void*        game,
                                               std::uint32_t eventId,
                                               void*        unit,
                                               void*        other,
                                               void*        damage,
                                               std::int32_t skillId,
                                               std::int32_t skillLevel,
                                               std::int32_t param,
                                               void*        installerRef) noexcept;

// game, the unit that receives the event, event id, skill id, skill level, node
// param, auraeventfunc index, group, state, installer unit.
using RegisterEventFn = std::int64_t(__fastcall*)(void*         game,
                                                   void*         unit,
                                                   std::int32_t  eventId,
                                                   std::int32_t  skillId,
                                                   std::int32_t  skillLevel,
                                                   std::int32_t  param,
                                                   std::uint32_t eventFunc,
                                                   std::int32_t  group,
                                                   std::int32_t  state,
                                                   void*         installer) noexcept;

HexDebuffFn     OriginalHexDebuff     = nullptr;
RegisterEventFn OriginalRegisterEvent = nullptr;

// The hex caster and hex skill while auraeventfunc 36 runs on this thread.
struct ActiveHex {
	void*        caster  = nullptr;
	std::int32_t skillId = -1;
};

thread_local ActiveHex CurrentHex {};

// ---------------------------------------------------------------------------
// Settings
// ---------------------------------------------------------------------------

struct Settings {
	bool anyTargetState  = true;
	bool recordInstaller = true;
};

constexpr auto ByteSize(std::size_t size) noexcept -> std::uint32_t {
	return static_cast<std::uint32_t>(size);
}

template <std::size_t Size>
constexpr auto ByteCount(const std::uint8_t (&)[Size]) noexcept -> std::uint32_t {
	return ByteSize(Size);
}

constexpr auto IsBlank(char c) noexcept -> bool {
	return c == ' ' || c == '\t' || c == '\r';
}

// Finds `key = value` at the start of a line. The config is a single table, so
// section headers and comments are simply skipped rather than tracked.
auto FindConfigValue(const char* text, const char* key, const char*& value, std::size_t& length) noexcept -> bool {
	if (text == nullptr || key == nullptr) {
		return false;
	}

	const std::size_t keyLength = std::strlen(key);

	for (const char* line = text; *line != '\0';) {
		const char* cursor = line;
		while (IsBlank(*cursor)) {
			++cursor;
		}

		if (std::strncmp(cursor, key, keyLength) == 0) {
			const char* after = cursor + keyLength;
			while (IsBlank(*after)) {
				++after;
			}

			if (*after == '=') {
				++after;
				while (IsBlank(*after)) {
					++after;
				}

				const char* end = after;
				while (*end != '\0' && *end != '\n' && *end != '#') {
					++end;
				}
				while (end > after && IsBlank(end[-1])) {
					--end;
				}

				value  = after;
				length = static_cast<std::size_t>(end - after);
				return true;
			}
		}

		while (*line != '\0' && *line != '\n') {
			++line;
		}
		if (*line == '\n') {
			++line;
		}
	}

	return false;
}

auto ReadBool(const char* text, const char* key, bool fallback) noexcept -> bool {
	const char* value  = nullptr;
	std::size_t length = 0;
	if (!FindConfigValue(text, key, value, length)) {
		return fallback;
	}

	if (length == 4 && std::strncmp(value, "true", 4) == 0) {
		return true;
	}
	if (length == 5 && std::strncmp(value, "false", 5) == 0) {
		return false;
	}
	return fallback;
}

auto LoadSettings(const D2RL::PluginContext* context) noexcept -> Settings {
	Settings settings {};

	std::array<char, 16'384> buffer {};
	if (!context->ReadConfig(buffer.data(), ByteSize(buffer.size()))) {
		context->LogWarn("Could not read celestialrayone.hex-debuff-installer.toml; using defaults.");
		return settings;
	}
	buffer.back() = '\0';

	settings.anyTargetState  = ReadBool(buffer.data(), "any_target_state", settings.anyTargetState);
	settings.recordInstaller = ReadBool(buffer.data(), "record_installer", settings.recordInstaller);
	return settings;
}

// ---------------------------------------------------------------------------
// The hooks
// ---------------------------------------------------------------------------

auto __fastcall HookHexDebuff(void*         game,
                              std::uint32_t eventId,
                              void*         unit,
                              void*         other,
                              void*         damage,
                              std::int32_t  skillId,
                              std::int32_t  skillLevel,
                              std::int32_t  param,
                              void*         installerRef) noexcept -> std::int64_t {
	const HexDebuffFn original = OriginalHexDebuff;
	if (original == nullptr) {
		return 0;
	}

	// Saved and restored rather than cleared, so a nested call can never leave the
	// outer one without its caster.
	const ActiveHex previous = CurrentHex;
	CurrentHex               = ActiveHex { .caster = unit, .skillId = skillId };

	const std::int64_t result = original(game, eventId, unit, other, damage, skillId, skillLevel, param, installerRef);

	CurrentHex = previous;
	return result;
}

auto __fastcall HookRegisterEvent(void*         game,
                                  void*         unit,
                                  std::int32_t  eventId,
                                  std::int32_t  skillId,
                                  std::int32_t  skillLevel,
                                  std::int32_t  param,
                                  std::uint32_t eventFunc,
                                  std::int32_t  group,
                                  std::int32_t  state,
                                  void*         installer) noexcept -> std::int64_t {
	const RegisterEventFn original = OriginalRegisterEvent;
	if (original == nullptr) {
		return 0;
	}

	// Only the registrations auraeventfunc 36 itself makes: no installer, group 1 and
	// the hex skill's own id. Any other registration, including one made by something
	// nested inside auraeventfunc 36 for a different skill, passes through untouched.
	const ActiveHex hex = CurrentHex;
	if (hex.caster != nullptr && installer == nullptr && group == HexInstallGroup && skillId == hex.skillId) {
		installer = hex.caster;
	}

	return original(game, unit, eventId, skillId, skillLevel, param, eventFunc, group, state, installer);
}

auto InstallHooks(const D2RL::PluginContext* context) noexcept -> bool {
	// Registrar first: on its own it does nothing, because the caster is only ever
	// set by the auraeventfunc 36 hook.
	if (!context->InstallInlineHook(RegisterEventRva,
	                                ExpectedRegisterEventPrologue,
	                                ByteCount(ExpectedRegisterEventPrologue),
	                                HookRegisterEvent,
	                                &OriginalRegisterEvent)) {
		context->LogError("Inline hook at 0x438230 (event registrar) failed; wrong game build, "
		                  "or the prologue does not match.");
		return false;
	}

	if (!context->InstallInlineHook(HexDebuffRva,
	                                ExpectedHexDebuffPrologue,
	                                ByteCount(ExpectedHexDebuffPrologue),
	                                HookHexDebuff,
	                                &OriginalHexDebuff)) {
		context->LogError("Inline hook at 0x51FDA0 (auraeventfunc 36) failed; wrong game build, "
		                  "or the prologue does not match.");
		return false;
	}

	context->LogInfo("auraeventfunc 36 hooked; hex event nodes now record the hex caster as installer.");
	return true;
}

auto ApplyAnyTargetState(const D2RL::PluginContext* context) noexcept -> bool {
	if (!context->CheckExpectedBytes(HexGateRva, ExpectedHexGate, ByteCount(ExpectedHexGate))) {
		context->LogError("The site at 0x51FFB6 is not the hexpurgedebuff gate; wrong game build, or "
		                  "something else already owns it. Leaving the gate in place.");
		return false;
	}

	if (!context->PatchNop(HexGateJneRva, ExpectedHexGateJne, ByteCount(ExpectedHexGateJne), ByteCount(ExpectedHexGateJne))) {
		context->LogError("Failed to NOP the hexpurgedebuff gate at 0x51FFC3.");
		return false;
	}

	context->LogInfo("hexpurgedebuff gate removed; auraeventfunc 36 registers Param1 for every auratargetstate.");
	return true;
}

constexpr D2RL::PluginInfo HexDebuffInstallerInfo {
	.infoSize    = D2RL::PluginInfoSize,
	.apiVersion  = D2RL_PLUGIN_API_VERSION,
	.id          = "celestialrayone.hex-debuff-installer",
	.name        = "Hex Debuff Installer",
	.version     = "1.1.0",
	.author      = "CelestialRayOne",
	.description = "auraeventfunc 36 registers Param1 on the hexed unit for every auratargetstate, "
	               "and those event nodes record the hex caster as installer, so auraeventfunc 33 "
	               "can cast a real skill from any hex.",
	.flags       = D2RL::PluginFlags::Server | D2RL::PluginFlags::NativeHooks,
};

}  // namespace

D2RL_PLUGIN_EXPORT auto D2RLoaderGetPluginInfo() noexcept -> const D2RL::PluginInfo* {
	return &HexDebuffInstallerInfo;
}

D2RL_PLUGIN_EXPORT auto D2RLoaderLoadPlugin(const D2RL::PluginContext* context) noexcept -> bool {
	if (context == nullptr) {
		return false;
	}

	const Settings settings = LoadSettings(context);

	if (!settings.anyTargetState && !settings.recordInstaller) {
		context->LogInfo("any_target_state and record_installer are both false; nothing to do.");
		return true;
	}

	bool gate = true;
	if (settings.anyTargetState) {
		gate = ApplyAnyTargetState(context);
	} else {
		context->LogInfo("any_target_state is false; the hexpurgedebuff gate is left in place.");
	}

	bool installer = true;
	if (settings.recordInstaller) {
		installer = InstallHooks(context);
	} else {
		context->LogInfo("record_installer is false; auraeventfunc 36 and the event registrar are left unhooked.");
	}

	// The two changes are independent, so only a total failure unloads the plugin.
	if (!gate && !installer) {
		context->LogError("Neither change could be applied; unloading.");
		return false;
	}

	return true;
}
