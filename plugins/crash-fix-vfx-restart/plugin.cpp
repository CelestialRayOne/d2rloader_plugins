// VFX Restart Fix
//
// See celestialrayone.vfx-restart-fix.toml for the write-up. In short:
//
//   Every HD visual effect (one VfxComponent instance, 224 bytes) plays a
//   PopcornFX particle effect and puts a "this particle effect died" callback
//   on it: 0xF6A030, added by both update functions (0xF6A870, 0xF6B190)
//   through the subscribe routine 0xA8CA30. Only that callback ever takes the
//   entry off again.
//
//   A restart (0xF6A150: rebuild flag +0x18 set, started flag +0x19 cleared;
//   its only caller is the animation state-machine event handler 0x7DA460)
//   makes the next update swap in a new particle effect from the factory
//   0xF6D640 and release the old one, without taking the callback off the old
//   one. The particle scene holds its own reference on every running effect
//   (0xA94160), so the old one keeps playing and keeps the callback. When it
//   dies (natural death 0xA90720 -> 0xA4CA00, or its particle file is unloaded:
//   0xA949A0 -> 0xA4C890 -> 0xA4B040), the stale callback runs against a visual
//   effect that has moved on:
//     - if that visual effect holds no particle effect any more, the callback
//       write-locks a list at address 0x20 and the game dies
//       (D2RLoader.exe+0xA4696A, called from +0xF6A04F);
//     - if it holds the new one, the callback takes its entry off the NEW
//       particle effect instead, so that one never reports its own death;
//     - if it was freed, the callback runs on freed memory.
//   The same orphan appears if a visual effect's started flag is cleared while
//   its particle effect still runs: Start (0xA4F0D0) refuses a running effect
//   and the update drops it.
//
// The fix: at the top of both update functions, when a visual effect that
// holds a particle effect is about to rebuild or (re)start, its callback is
// taken off that particle effect with the game's own unsubscribe (0xA8CB10).
// If the update then keeps that particle effect and starts it, it adds the
// callback back itself, as it always does after a successful start. The old
// particle effect still plays out exactly as before.
//
// Built against D2R 3.3 (module 140000000.D2RLoader.exe, D2RLoader 1.3.1) and
// PluginSDK 0.3.0. Every address below is pinned by a byte check before
// anything is hooked.

#include <D2RLPlugin/api.h>

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace {

// ---------------------------------------------------------------------------
// Addresses, all read out of the 3.3 image
// ---------------------------------------------------------------------------

// The visual effect's "particle effect died" callback. Its first act is to
// write-lock the callback list of the particle effect it currently holds
// (lock at effect+0x20, through 0xA46950). With no particle effect held that
// is address 0x20: the crash.
constexpr std::uint64_t DeathCallbackRva = 0x00F6A030;
constexpr std::uint8_t ExpectedDeathCallback[] {
	0x48, 0x89, 0x5C, 0x24, 0x10,  // mov [rsp+10h], rbx
	0x57,                          // push rdi
	0x48, 0x83, 0xEC, 0x20,        // sub rsp, 20h
	0x48, 0x8B, 0x79, 0x10,        // mov rdi, [rcx+10h]     the particle effect it holds
	0x48, 0x8B, 0xD9,              // mov rbx, rcx
	0x48, 0x8D, 0x4C, 0x24, 0x30,  // lea rcx, [rsp+30h]
	0x48, 0x8D, 0x57, 0x20,        // lea rdx, [rdi+20h]     the list's lock
	0xE8, 0x01, 0xC9, 0xAD, 0xFF,  // call 0xA46950          write-lock
};

// The game's unsubscribe for these callback lists: write-locks the list (lock
// at list+0x10), removes the matching {object, function} entry and bumps the
// list's version at list+0x14.
constexpr std::uint64_t UnsubscribeRva = 0x00A8CB10;
constexpr std::uint8_t ExpectedUnsubscribe[] {
	0x48, 0x89, 0x5C, 0x24, 0x10,  // mov [rsp+10h], rbx
	0x57,                          // push rdi
	0x48, 0x83, 0xEC, 0x20,        // sub rsp, 20h
	0x48, 0x8B, 0xFA,              // mov rdi, rdx           the entry to remove
	0x48, 0x8B, 0xD9,              // mov rbx, rcx           the list
	0x48, 0x8D, 0x51, 0x10,        // lea rdx, [rcx+10h]     the list's lock
	0x48, 0x8D, 0x4C, 0x24, 0x30,  // lea rcx, [rsp+30h]
	0xE8, 0x22, 0x9E, 0xFB, 0xFF,  // call 0xA46950          write-lock
};

// The restart. Only witnessed: it proves what +0x18 and +0x19 mean, and that it
// never touches the callback list.
constexpr std::uint64_t RestartRva = 0x00F6A150;
constexpr std::uint8_t ExpectedRestart[] {
	0x48, 0x83, 0x79, 0x10, 0x00,        // cmp qword [rcx+10h], 0
	0x74, 0x0A,                          // je ret
	0x66, 0xC7, 0x41, 0x18, 0x01, 0x00,  // mov word [rcx+18h], 1   rebuild = 1, started = 0
	0xC6, 0x41, 0x1A, 0x00,              // mov byte [rcx+1Ah], 0
	0xC3,                                // ret
};

// Update, static visual effects: void(vfx). Hooked at its entry.
constexpr std::uint64_t UpdateRva = 0x00F6A870;
constexpr std::uint8_t ExpectedUpdatePrologue[] {
	0x40, 0x55,                                      // push rbp
	0x53,                                            // push rbx
	0x41, 0x54,                                      // push r12
	0x41, 0x57,                                      // push r15
	0x48, 0x8D, 0xAC, 0x24, 0x78, 0xFF, 0xFF, 0xFF,  // lea rbp, [rsp-88h]
};
// Its rebuild block: tests and clears +0x18, then asks the factory for a new
// particle effect. The old one is swapped out and released right after.
constexpr std::uint64_t UpdateRebuildRva = 0x00F6A89F;
constexpr std::uint8_t ExpectedUpdateRebuild[] {
	0x44, 0x38, 0x61, 0x18,                    // cmp [rcx+18h], r12b    rebuild flag
	0x74, 0x72,                                // je  0xF6A917
	0x44, 0x88, 0x61, 0x18,                    // mov [rcx+18h], r12b
	0x48, 0x8D, 0x55, 0x80,                    // lea rdx, [rbp-80h]
	0x48, 0x8B, 0x05, 0xCC, 0x57, 0x89, 0x01,  // mov rax, [0x2800080]   VFX system
	0x48, 0x8B, 0x49, 0x08,                    // mov rcx, [rcx+8]       definition
	0x4C, 0x8B, 0x40, 0x08,                    // mov r8, [rax+8]
	0xE8, 0x7F, 0x2D, 0x00, 0x00,              // call 0xF6D640          new particle effect
};
// Its start block: with a particle effect held and +0x19 clear, it starts it.
constexpr std::uint64_t UpdateStartRva = 0x00F6A917;
constexpr std::uint8_t ExpectedUpdateStart[] {
	0x48, 0x8B, 0x4B, 0x10,              // mov rcx, [rbx+10h]     particle effect
	0x48, 0x85, 0xC9,                    // test rcx, rcx
	0x0F, 0x84, 0xF5, 0x07, 0x00, 0x00,  // je  0xF6B119
	0x44, 0x38, 0x63, 0x19,              // cmp [rbx+19h], r12b    started flag
	0x0F, 0x85, 0xDB, 0x07, 0x00, 0x00,  // jne 0xF6B109
};
// Its subscribe, after a successful start: {visual effect, 0xF6A030} on the
// particle effect's list at +0x10.
constexpr std::uint64_t UpdateSubscribeRva = 0x00F6B0A0;
constexpr std::uint8_t ExpectedUpdateSubscribe[] {
	0x48, 0x8B, 0x4B, 0x10,                    // mov rcx, [rbx+10h]
	0x48, 0x8D, 0x05, 0x85, 0xEF, 0xFF, 0xFF,  // lea rax, [0xF6A030]
	0x48, 0x83, 0xC1, 0x10,                    // add rcx, 10h           the callback list
	0x48, 0x89, 0x45, 0xD8,                    // mov [rbp-28h], rax
	0x48, 0x8D, 0x55, 0xD0,                    // lea rdx, [rbp-30h]
	0x48, 0x89, 0x5D, 0xD0,                    // mov [rbp-30h], rbx     {vfx, callback}
	0xE8, 0x70, 0x19, 0xB2, 0xFF,              // call 0xA8CA30          subscribe
};

// Update, moving visual effects: void(vfx, transform, seconds in xmm2).
constexpr std::uint64_t UpdateMovingRva = 0x00F6B190;
constexpr std::uint8_t ExpectedUpdateMovingPrologue[] {
	0x40, 0x55,                                      // push rbp
	0x53,                                            // push rbx
	0x56,                                            // push rsi
	0x41, 0x55,                                      // push r13
	0x48, 0x8D, 0xAC, 0x24, 0xF8, 0xFE, 0xFF, 0xFF,  // lea rbp, [rsp-108h]
};
constexpr std::uint64_t UpdateMovingRebuildRva = 0x00F6B1BE;
constexpr std::uint8_t ExpectedUpdateMovingRebuild[] {
	0x80, 0x79, 0x18, 0x00,                    // cmp byte [rcx+18h], 0  rebuild flag
	0x0F, 0x28, 0xFA,                          // movaps xmm7, xmm2      seconds
	0x4C, 0x8B, 0xEA,                          // mov r13, rdx           transform
	0x48, 0x8B, 0xD9,                          // mov rbx, rcx
	0xBE, 0xFF, 0xFF, 0xFF, 0xFF,              // mov esi, -1
	0x74, 0x74,                                // je  0xF6B246
	0xC6, 0x41, 0x18, 0x00,                    // mov byte [rcx+18h], 0
	0x48, 0x8D, 0x55, 0x80,                    // lea rdx, [rbp-80h]
	0x48, 0x8B, 0x05, 0x9F, 0x4E, 0x89, 0x01,  // mov rax, [0x2800080]
	0x48, 0x8B, 0x49, 0x08,                    // mov rcx, [rcx+8]
	0x4C, 0x8B, 0x40, 0x08,                    // mov r8, [rax+8]
	0xE8, 0x52, 0x24, 0x00, 0x00,              // call 0xF6D640
};
constexpr std::uint64_t UpdateMovingStartRva = 0x00F6B42D;
constexpr std::uint8_t ExpectedUpdateMovingStart[] {
	0x80, 0x7B, 0x19, 0x00,              // cmp byte [rbx+19h], 0  started flag
	0x0F, 0x85, 0xC2, 0x07, 0x00, 0x00,  // jne 0xF6BBF9
	0x41, 0x8B, 0x46, 0x08,              // mov eax, [r14+8]
	0x48, 0x8D, 0x55, 0x30,              // lea rdx, [rbp+30h]
	0x0F, 0x10, 0x07,                    // movups xmm0, [rdi]
	0xC6, 0x43, 0x19, 0x01,              // mov byte [rbx+19h], 1
};
constexpr std::uint64_t UpdateMovingSubscribeRva = 0x00F6BBB4;
constexpr std::uint8_t ExpectedUpdateMovingSubscribe[] {
	0x48, 0x8B, 0x4B, 0x10,                    // mov rcx, [rbx+10h]
	0x48, 0x8D, 0x05, 0x71, 0xE4, 0xFF, 0xFF,  // lea rax, [0xF6A030]
	0x48, 0x83, 0xC1, 0x10,                    // add rcx, 10h
	0x48, 0x89, 0x45, 0xE8,                    // mov [rbp-18h], rax
	0x48, 0x8D, 0x55, 0xE0,                    // lea rdx, [rbp-20h]
	0x48, 0x89, 0x5D, 0xE0,                    // mov [rbp-20h], rbx
	0xE8, 0x5C, 0x0E, 0xB2, 0xFF,              // call 0xA8CA30
};

// PopcornFX Start reads the particle file's name for its "No compiled
// connection-map in effect" message: effect+0x40 is the effect asset,
// asset+0x48 its file, file+0x10 the path string. The text is at string+0x20,
// or behind a pointer stored there when the length field at string+0x10 is
// negative. The log line reads it the same way.
constexpr std::uint64_t StartAssetLoadRva = 0x00A4F499;
constexpr std::uint8_t ExpectedStartAssetLoad[] {
	0x48, 0x8B, 0x47, 0x40,  // mov rax, [rdi+40h]    rdi = the particle effect
};
constexpr std::uint64_t StartNameReadRva = 0x00A4F4FF;
constexpr std::uint8_t ExpectedStartNameRead[] {
	0x48, 0x8B, 0x40, 0x48,        // mov rax, [rax+48h]
	0x48, 0x8B, 0x48, 0x10,        // mov rcx, [rax+10h]
	0x48, 0x85, 0xC9,              // test rcx, rcx
	0x74, 0x0D,                    // je
	0x83, 0x79, 0x10, 0x00,        // cmp dword [rcx+10h], 0
	0x4C, 0x8D, 0x71, 0x20,        // lea r14, [rcx+20h]
	0x7D, 0x03,                    // jge
	0x4D, 0x8B, 0x36,              // mov r14, [r14]
};

// Visual effect instance.
constexpr std::size_t VfxEffectOffset  = 0x10;  // the particle effect it plays (counted reference)
constexpr std::size_t VfxRebuildOffset = 0x18;  // nonzero: the next update swaps in a new particle effect
constexpr std::size_t VfxStartedOffset = 0x19;  // zero: the next update starts the particle effect it holds

// Particle effect instance (184 bytes, built by 0xA4C680).
constexpr std::size_t EffectCallbackListOffset = 0x10;  // {entries, count, lock, version}
constexpr std::size_t ListVersionOffset        = 0x14;  // bumped by every removal
constexpr std::size_t EffectAssetOffset        = 0x40;
constexpr std::size_t AssetFileOffset          = 0x48;
constexpr std::size_t FilePathOffset           = 0x10;
constexpr std::size_t PathLengthOffset         = 0x10;
constexpr std::size_t PathTextOffset           = 0x20;

// ---------------------------------------------------------------------------
// Game function types
// ---------------------------------------------------------------------------

struct CallbackEntry {
	void* object;
	void* function;
};
static_assert(sizeof(CallbackEntry) == 16);

using UpdateFn       = void(__fastcall*)(void* vfx) noexcept;
using UpdateMovingFn = void(__fastcall*)(void* vfx, const void* transform, float seconds) noexcept;
using UnsubscribeFn  = void*(__fastcall*)(void* list, const CallbackEntry* entry) noexcept;

const D2RL::PluginContext* Context              = nullptr;
std::uintptr_t             ExeBase              = 0;
UpdateFn                   OriginalUpdate       = nullptr;
UpdateMovingFn             OriginalUpdateMoving = nullptr;
UnsubscribeFn              Unsubscribe          = nullptr;
void*                      DeathCallback        = nullptr;
bool                       LogEffectFiles       = true;

std::atomic<std::uint64_t> CancelledOnRebuild { 0 };
std::atomic<std::uint64_t> CancelledOnStart { 0 };

// Particle files seen, for the log and the console command.
constexpr std::size_t MaxFiles      = 64;
constexpr std::size_t MaxNameLength = 192;

struct SeenFile {
	std::uint32_t hash;
	std::uint32_t count;
	char          name[MaxNameLength];
};

std::atomic_flag                 FilesLock {};
std::array<SeenFile, MaxFiles>   Files {};
std::size_t                      FileCount   = 0;
std::uint64_t                    FilesMissed = 0;

class SpinGuard {
public:
	explicit SpinGuard(std::atomic_flag& flag) noexcept : flag_(flag) {
		while (flag_.test_and_set(std::memory_order_acquire)) {
		}
	}
	~SpinGuard() { flag_.clear(std::memory_order_release); }
	SpinGuard(const SpinGuard&)            = delete;
	SpinGuard& operator=(const SpinGuard&) = delete;

private:
	std::atomic_flag& flag_;
};

// ---------------------------------------------------------------------------
// Settings
// ---------------------------------------------------------------------------

struct Settings {
	bool enabled        = true;
	bool logEffectFiles = true;
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
// comments are simply skipped.
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

// Written by EnsureConfig when the file does not exist yet. It is the plugin's
// documentation as shipped.
constexpr char DefaultConfigToml[] = R"toml(# celestialrayone.vfx-restart-fix
#
# VFX Restart Fix
#
# Built for D2RLoader 1.3.1 (Diablo II: Resurrected 3.3). Read when the plugin
# loads; restart the game after editing.
#
# What goes wrong without it
#   Every HD visual effect on a unit (state and aura overlays, skill, missile
#   and death visuals and so on) plays a particle effect and asks that particle
#   effect to call it back when it dies.
#   When the game restarts a visual effect (an animation state-machine event
#   can do that), it starts a fresh particle effect and lets the old one play
#   out on its own, but it never cancels the old one's callback. The old
#   particle effect keeps running with a callback to a visual effect that has
#   moved on.
#   When that old particle effect finally dies, for example when its particle
#   file is unloaded, the callback reaches a visual effect that may no longer
#   hold any particle effect, or that no longer exists, and the game crashes:
#   access violation writing address 0x20 at D2RLoader.exe+0xA4696A, called
#   from D2RLoader.exe+0xF6A04F. It needs an unlucky order of events, so it
#   hits some players once in a while, often around a death, when many
#   effects end at once.
#   When the visual effect still holds its new particle effect instead, the
#   stale callback cancels the NEW particle effect's callback by mistake, so
#   that one never reports its own death.
#
# What the plugin does
#   Right before the game replaces or restarts the particle effect of a visual
#   effect, it cancels that visual effect's callback on the particle effect it
#   is about to let go, with the game's own cancel routine. The old particle
#   effect still plays out exactly as before. Nothing else changes.
#
# Console
#   vfxrestart   how many stale callbacks were cancelled this session, and on
#                which particle files.

# Turn the fix on or off. Off leaves the game exactly as stock D2RLoader runs it.
enabled = true

# Write each particle file to the plugin log the first time one of its stale
# callbacks is cancelled in a session.
log_effect_files = true
)toml";

auto LoadSettings(const D2RL::PluginContext* context) noexcept -> Settings {
	Settings settings {};
	if (!context->EnsureConfig(DefaultConfigToml)) {
		context->LogWarn("Could not create celestialrayone.vfx-restart-fix.toml; using defaults.");
		return settings;
	}
	std::array<char, 8'192> buffer {};
	if (!context->ReadConfig(buffer.data(), ByteSize(buffer.size()))) {
		context->LogWarn("Could not read celestialrayone.vfx-restart-fix.toml; using defaults.");
		return settings;
	}
	buffer.back()           = '\0';
	settings.enabled        = ReadBool(buffer.data(), "enabled", settings.enabled);
	settings.logEffectFiles = ReadBool(buffer.data(), "log_effect_files", settings.logEffectFiles);
	return settings;
}

// ---------------------------------------------------------------------------
// Particle file names, for the log only
// ---------------------------------------------------------------------------

// Plain function with no destructible locals, so it can use structured
// exception handling: a name that cannot be read only costs the log line.
auto CopyEffectFileName(const std::uint8_t* effect, char* out, std::size_t outSize) noexcept -> bool {
	if (effect == nullptr || out == nullptr || outSize == 0) {
		return false;
	}
	out[0] = '\0';
	__try {
		const auto* asset = *reinterpret_cast<const std::uint8_t* const*>(effect + EffectAssetOffset);
		if (asset == nullptr) {
			return false;
		}
		const auto* file = *reinterpret_cast<const std::uint8_t* const*>(asset + AssetFileOffset);
		if (file == nullptr) {
			return false;
		}
		const auto* path = *reinterpret_cast<const std::uint8_t* const*>(file + FilePathOffset);
		if (path == nullptr) {
			return false;
		}
		const char* text = reinterpret_cast<const char*>(path + PathTextOffset);
		if (*reinterpret_cast<const std::int32_t*>(path + PathLengthOffset) < 0) {
			text = *reinterpret_cast<const char* const*>(path + PathTextOffset);
		}
		if (text == nullptr) {
			return false;
		}
		std::size_t length = 0;
		while (length + 1 < outSize && text[length] != '\0') {
			out[length] = text[length];
			++length;
		}
		out[length] = '\0';
		return length != 0;
	} __except (1) {
		out[0] = '\0';
		return false;
	}
}

auto Fnv1a(const char* text) noexcept -> std::uint32_t {
	std::uint32_t hash = 2166136261u;
	for (; *text != '\0'; ++text) {
		hash = (hash ^ static_cast<std::uint8_t>(*text)) * 16777619u;
	}
	return hash;
}

void RecordEffectFile(const std::uint8_t* effect) noexcept {
	char name[MaxNameLength] {};
	if (!CopyEffectFileName(effect, name, sizeof(name))) {
		std::snprintf(name, sizeof(name), "(particle file name not readable)");
	}
	const std::uint32_t hash  = Fnv1a(name);
	bool                first = false;
	{
		const SpinGuard guard(FilesLock);
		std::size_t     index = 0;
		while (index < FileCount && Files[index].hash != hash) {
			++index;
		}
		if (index < FileCount) {
			++Files[index].count;
		} else if (FileCount < MaxFiles) {
			SeenFile& seen = Files[FileCount++];
			seen.hash      = hash;
			seen.count     = 1;
			std::memcpy(seen.name, name, sizeof(seen.name));
			first = true;
		} else {
			++FilesMissed;
		}
	}
	if (first && Context != nullptr) {
		char message[MaxNameLength + 128] {};
		std::snprintf(message, sizeof(message),
		              "Cancelled a stale death callback on %s (first one for this particle file this session).", name);
		Context->LogInfo(message);
	}
}

// ---------------------------------------------------------------------------
// Hooks
// ---------------------------------------------------------------------------

// Runs at the top of both updates, before they look at the rebuild and started
// flags themselves.
void CancelStaleDeathCallback(void* vfx) noexcept {
	if (vfx == nullptr || Unsubscribe == nullptr) {
		return;
	}
	auto* const self       = static_cast<std::uint8_t*>(vfx);
	const bool  rebuilding = self[VfxRebuildOffset] != 0;
	const bool  starting   = self[VfxStartedOffset] == 0;
	if (!rebuilding && !starting) {
		return;  // every frame of a running visual effect ends here
	}
	auto* const effect = *reinterpret_cast<std::uint8_t* const*>(self + VfxEffectOffset);
	if (effect == nullptr) {
		return;  // nothing held, nothing to let go of
	}
	auto* const list    = effect + EffectCallbackListOffset;
	const auto* version = reinterpret_cast<const volatile std::uint32_t*>(list + ListVersionOffset);
	const std::uint32_t before = *version;

	const CallbackEntry entry { vfx, DeathCallback };
	Unsubscribe(list, &entry);

	if (*version == before) {
		return;  // no callback was registered on it: the usual case
	}
	if (rebuilding) {
		CancelledOnRebuild.fetch_add(1, std::memory_order_relaxed);
	} else {
		CancelledOnStart.fetch_add(1, std::memory_order_relaxed);
	}
	if (LogEffectFiles) {
		RecordEffectFile(effect);
	}
}

void __fastcall HookUpdate(void* vfx) noexcept {
	CancelStaleDeathCallback(vfx);
	if (const UpdateFn original = OriginalUpdate; original != nullptr) {
		original(vfx);
	}
}

void __fastcall HookUpdateMoving(void* vfx, const void* transform, float seconds) noexcept {
	CancelStaleDeathCallback(vfx);
	if (const UpdateMovingFn original = OriginalUpdateMoving; original != nullptr) {
		original(vfx, transform, seconds);
	}
}

// ---------------------------------------------------------------------------
// Console
// ---------------------------------------------------------------------------

auto __cdecl VfxRestartCommand(D2R::Game::Client*, const D2RL::ConsoleCommandContext* command, void*) noexcept
        -> D2RL::ConsoleCommandResult {
	const std::uint64_t onRebuild = CancelledOnRebuild.load();
	const std::uint64_t onStart   = CancelledOnStart.load();

	std::array<SeenFile, MaxFiles> files {};
	std::size_t                    fileCount = 0;
	std::uint64_t                  missed    = 0;
	{
		const SpinGuard guard(FilesLock);
		fileCount = FileCount;
		missed    = FilesMissed;
		for (std::size_t index = 0; index < fileCount; ++index) {
			files[index] = Files[index];
		}
	}

	char line[MaxNameLength + 96] {};
	std::snprintf(line, sizeof(line),
	              "VFX Restart Fix: %llu stale death callback(s) cancelled this session (%llu on rebuild, %llu on restart).",
	              static_cast<unsigned long long>(onRebuild + onStart),
	              static_cast<unsigned long long>(onRebuild),
	              static_cast<unsigned long long>(onStart));
	command->plugin->WriteConsoleMessage(line);
	if (!LogEffectFiles) {
		command->plugin->WriteConsoleMessage("  Particle files are not tracked (log_effect_files = false).");
		return D2RL::ConsoleCommandResult::Handled;
	}
	for (std::size_t index = 0; index < fileCount; ++index) {
		std::snprintf(line, sizeof(line), "  %s: %u", files[index].name, files[index].count);
		command->plugin->WriteConsoleMessage(line);
	}
	if (missed != 0) {
		std::snprintf(line, sizeof(line), "  %llu more on files past the first %u.",
		              static_cast<unsigned long long>(missed), static_cast<unsigned>(MaxFiles));
		command->plugin->WriteConsoleMessage(line);
	}
	return D2RL::ConsoleCommandResult::Handled;
}

// ---------------------------------------------------------------------------
// Install
// ---------------------------------------------------------------------------

struct Witness {
	std::uint64_t       rva;
	const std::uint8_t* bytes;
	std::uint32_t       size;
	const char*         what;
};

auto CheckWitnesses(const D2RL::PluginContext* context) noexcept -> bool {
	const Witness witnesses[] {
		{ DeathCallbackRva, ExpectedDeathCallback, ByteCount(ExpectedDeathCallback),
		  "0xF6A030 is not the visual effect's death callback" },
		{ UnsubscribeRva, ExpectedUnsubscribe, ByteCount(ExpectedUnsubscribe),
		  "0xA8CB10 is not the callback list unsubscribe" },
		{ RestartRva, ExpectedRestart, ByteCount(ExpectedRestart),
		  "0xF6A150 is not the visual effect restart" },
		{ UpdateRva, ExpectedUpdatePrologue, ByteCount(ExpectedUpdatePrologue),
		  "0xF6A870 is not the static visual effect update" },
		{ UpdateRebuildRva, ExpectedUpdateRebuild, ByteCount(ExpectedUpdateRebuild),
		  "0xF6A89F is not the static update's rebuild block" },
		{ UpdateStartRva, ExpectedUpdateStart, ByteCount(ExpectedUpdateStart),
		  "0xF6A917 is not the static update's start block" },
		{ UpdateSubscribeRva, ExpectedUpdateSubscribe, ByteCount(ExpectedUpdateSubscribe),
		  "0xF6B0A0 does not subscribe 0xF6A030 on the particle effect's list" },
		{ UpdateMovingRva, ExpectedUpdateMovingPrologue, ByteCount(ExpectedUpdateMovingPrologue),
		  "0xF6B190 is not the moving visual effect update" },
		{ UpdateMovingRebuildRva, ExpectedUpdateMovingRebuild, ByteCount(ExpectedUpdateMovingRebuild),
		  "0xF6B1BE is not the moving update's rebuild block" },
		{ UpdateMovingStartRva, ExpectedUpdateMovingStart, ByteCount(ExpectedUpdateMovingStart),
		  "0xF6B42D is not the moving update's start block" },
		{ UpdateMovingSubscribeRva, ExpectedUpdateMovingSubscribe, ByteCount(ExpectedUpdateMovingSubscribe),
		  "0xF6BBB4 does not subscribe 0xF6A030 on the particle effect's list" },
		{ StartAssetLoadRva, ExpectedStartAssetLoad, ByteCount(ExpectedStartAssetLoad),
		  "0xA4F499 does not load the effect asset from +0x40" },
		{ StartNameReadRva, ExpectedStartNameRead, ByteCount(ExpectedStartNameRead),
		  "0xA4F4FF does not read the particle file name" },
	};
	for (const Witness& witness : witnesses) {
		if (!context->CheckExpectedBytes(witness.rva, witness.bytes, witness.size)) {
			char message[200] {};
			std::snprintf(message, sizeof(message), "%s; wrong game build. Nothing was hooked.", witness.what);
			context->LogError(message);
			return false;
		}
	}
	return true;
}

constexpr D2RL::PluginInfo VfxRestartInfo {
	.infoSize    = D2RL::PluginInfoSize,
	.abiVersion  = D2RL_PLUGIN_ABI_VERSION,
	.id          = "celestialrayone.vfx-restart-fix",
	.name        = "VFX Restart Fix",
	.version     = "1.0.0",
	.author      = "CelestialRayOne",
	.description = "When the game restarts or replaces a visual effect's particle effect, cancels the death callback "
	               "on the one it lets go, so it cannot crash the game (D2RLoader.exe+0xA4696A) when it dies later.",
	.flags       = D2RL::PluginFlags::Client | D2RL::PluginFlags::NativeHooks,
};

}  // namespace

D2RL_PLUGIN_EXPORT auto D2RLoaderGetPluginInfo() noexcept -> const D2RL::PluginInfo* {
	return &VfxRestartInfo;
}

D2RL_PLUGIN_EXPORT auto D2RLoaderLoadPlugin(const D2RL::PluginContext* context) noexcept -> bool {
	if (context == nullptr) {
		return false;
	}
	const Settings settings = LoadSettings(context);
	if (!settings.enabled) {
		context->LogInfo("enabled is false; nothing hooked.");
		return true;
	}
	ExeBase = context->exeBase;
	if (ExeBase == 0) {
		context->LogError("exeBase is zero; cannot resolve game addresses.");
		return false;
	}
	if (!CheckWitnesses(context)) {
		return false;
	}
	Context        = context;
	LogEffectFiles = settings.logEffectFiles;
	Unsubscribe    = reinterpret_cast<UnsubscribeFn>(ExeBase + UnsubscribeRva);
	DeathCallback  = reinterpret_cast<void*>(ExeBase + DeathCallbackRva);

	if (!context->InstallInlineHook(UpdateRva,
	                                ExpectedUpdatePrologue,
	                                ByteCount(ExpectedUpdatePrologue),
	                                HookUpdate,
	                                &OriginalUpdate)) {
		context->LogError("Inline hook at 0xF6A870 (static visual effect update) failed. Nothing was hooked.");
		return false;
	}
	if (!context->InstallInlineHook(UpdateMovingRva,
	                                ExpectedUpdateMovingPrologue,
	                                ByteCount(ExpectedUpdateMovingPrologue),
	                                HookUpdateMoving,
	                                &OriginalUpdateMoving)) {
		context->LogError("Inline hook at 0xF6B190 (moving visual effect update) failed; only static visual effects "
		                  "are covered this session.");
	}

	if (!context->RegisterConsoleCommand("vfxrestart", &VfxRestartCommand,
	                                     "Shows how many stale visual effect death callbacks were cancelled, and on "
	                                     "which particle files.")) {
		context->LogWarn("The vfxrestart console command was refused.");
	}
	context->LogInfo("Watching visual effect restarts; the death callback on a particle effect that is let go is "
	                 "cancelled first.");
	return true;
}

D2RL_PLUGIN_EXPORT void D2RLoaderUnloadPlugin() noexcept {
	Context              = nullptr;
	OriginalUpdate       = nullptr;
	OriginalUpdateMoving = nullptr;
	Unsubscribe          = nullptr;
	DeathCallback        = nullptr;
}
