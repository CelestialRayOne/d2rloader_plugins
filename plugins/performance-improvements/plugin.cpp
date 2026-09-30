// ---------------------------------------------------------------------------
// Performance improvements  -  D2RLoader plugin (D2R 3.3, D2RLoader 1.3.1)
//
// Every missile-performance fix in one plugin. Each fix removes work the game
// did in a way that grew with the square of the number of missiles alive, and
// each leaves gameplay exactly as it was: the game gets the same results, it
// just stops redoing the same search thousands of times per frame. Each fix
// can be switched off on its own in the config file.
//
//   room index            Server and client room unit lists (every missile is
//                         linked into its room's list). A side index next to
//                         each list lets a unit leave its room without a walk
//                         over the whole list, and lets missile collision
//                         checks look at players and monsters only, for the
//                         collide types that can only ever hit those
//                         (missiles.txt CollideType 1, 2, 3, 5, 8).
//                         Hooks 0x38EC90, 0x38EFE0, 0x38F270, 0x324BC0.
//   room add check        Skips a debug-only duplicate check that walked a
//                         room's whole unit list every time a unit was added.
//                         Byte patch at 0x38ED8B (replaces the patch file
//                         skip-room-add-duplicate-check.json, and detects it).
//   translation queue     HD layer, PreprocessMessageQueue: drops superseded
//                         unit messages with hash lookups instead of comparing
//                         every message with every earlier one. Hook 0x775A70.
//   weak instances        HD layer, EntityContainerDefinition::
//                         RemoveWeakInstance: a destroyed missile visual leaves
//                         its type's instance list directly instead of a walk
//                         over every other visual of that type (the main cost
//                         of many missiles dying or colliding at once).
//                         Hooks 0x955E90, 0x94A7A0.
//   client unit loop      Client per-type unit loop: skips the per-unit
//                         re-lookup whose result the game only uses for
//                         monsters and objects. Hook 0x09F470.
//   entity pool           The HD entity registry's allocator: one fixed 70 MB
//                         TLSF pool that runs out around 16k missiles (the
//                         "Unable to allocate for entity pool" crash). Raised
//                         to entity_pool_mb, by the game's own setup code at
//                         0xD714E0 when it has not run yet, or as a second
//                         TLSF pool when it has.
//   unit entity lookup    HD layer: finds a removed unit's HD entity by
//                         scanning the key array instead of walking every
//                         entity with a lookup and a call each (the second
//                         cost of mass missile deaths). Hook 0x9725E0.
//
// Every hooked function is checked byte for byte against this build before
// anything is installed; a part whose code does not match, or whose hook site
// is already taken by another plugin, stays off and says so in the log, and
// the other parts carry on.
//
// Console
//     perf           state of every part and its counters
//     perf reset     zero the counters
//     perf verify    check the room index against the game's room lists
//     perf memory    memory held by this plugin, the game's RAM and commit,
//                    system headroom, and the game's video memory per GPU
//
// Runtime settings live in
// d2rloader/config/celestialrayone.performance-improvements.toml, created
// with defaults on first load.
//
// Replaces the separate plugins room-unit-index (formerly in this folder),
// translation-queue, client-unit-loop, weak-instances and unit-entity-lookup.
// The sampling profiler stays a plugin of its own.
// ---------------------------------------------------------------------------

#include <D2RLPlugin/api.h>

#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX // keep windows.h from defining min/max macros
#endif
#include <windows.h>
#if defined(_WIN32)
#include <dxgi1_4.h>
#endif

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <new>

namespace {

// ===========================================================================
// Shared
// ===========================================================================

namespace perf_common {

template <std::size_t Size>
constexpr auto ByteCount(const std::uint8_t (&)[Size]) noexcept -> std::uint32_t {
	return static_cast<std::uint32_t>(Size);
}

auto CheckSite(const D2RL::PluginContext* context,
               std::uint64_t              rva,
               const std::uint8_t*        bytes,
               std::uint32_t              size,
               const char*                what) noexcept -> bool {
	if (context->CheckExpectedBytes(rva, bytes, size)) {
		return true;
	}
	char message[256]{};
	std::snprintf(message, sizeof(message),
	              "%s at RVA 0x%08llX does not match the expected bytes - another plugin already patched it (an old separate plugin with the same fix still in the plugins folder?) or a different game build.",
	              what, static_cast<unsigned long long>(rva));
	context->LogError(message);
	return false;
}

} // namespace perf_common

// ===========================================================================
// Room index (server and client room unit lists)
// ===========================================================================

namespace room_index {


// --------------------------------------------------------------------------
// Build-specific constants
// --------------------------------------------------------------------------

constexpr std::uint64_t kAddUnitToRoomRva      = 0x0038EC90ULL;
constexpr std::uint64_t kRemoveUnitFromRoomRva = 0x0038EFE0ULL;
constexpr std::uint64_t kSortRoomUnitsRva      = 0x0038F270ULL;
constexpr std::uint64_t kFindUnitRva           = 0x00324BC0ULL;

constexpr std::uint64_t kUnitsGetRoomRva    = 0x0034B440ULL;
constexpr std::uint64_t kRoomIntersectsRva  = 0x003256E0ULL;
constexpr std::uint64_t kUnitMatchesMaskRva = 0x00325760ULL;
constexpr std::uint64_t kUnitsGetSizeRva    = 0x0034B580ULL;

// Witness-only sites: the plugin reads these fields directly, and these
// accessors prove the offsets are the ones this build uses.
constexpr std::uint64_t kRoomUnitListSlotRva = 0x002E8070ULL;
constexpr std::uint64_t kRoomFirstUnitRva    = 0x002EFD90ULL;
constexpr std::uint64_t kRoomNearListRva     = 0x002EFDE0ULL;
constexpr std::uint64_t kUnitNextInRoomRva   = 0x0034B4A0ULL;
constexpr std::uint64_t kPathGetXRva         = 0x00341A20ULL;
constexpr std::uint64_t kPathGetYRva         = 0x00341A30ULL;

constexpr std::uint64_t kServerRulePlayerSideRva = 0x00457C70ULL;
constexpr std::uint64_t kServerRuleMonstersRva   = 0x00457BE0ULL;
constexpr std::uint64_t kServerRuleBothRva       = 0x00457D10ULL;
constexpr std::uint64_t kClientRulePlayerSideRva = 0x001B0080ULL;
constexpr std::uint64_t kClientRuleMonstersRva   = 0x001B0020ULL;
constexpr std::uint64_t kClientRuleBothRva       = 0x001B00F0ULL;

// Hook prologues. Every witness ends on an instruction boundary and holds no
// relative operand.
constexpr std::uint8_t kAddUnitToRoomBytes[]{
	0x48, 0x89, 0x5C, 0x24, 0x18, 0x55, 0x56, 0x57, 0x48, 0x83, 0xEC, 0x50,
};
constexpr std::uint8_t kRemoveUnitFromRoomBytes[]{
	0x48, 0x89, 0x5C, 0x24, 0x10, 0x48, 0x89, 0x74, 0x24, 0x18, 0x57, 0x48, 0x83, 0xEC, 0x20,
};
constexpr std::uint8_t kSortRoomUnitsBytes[]{
	0x40, 0x55, 0x56, 0x41, 0x57, 0x48, 0x83, 0xEC, 0x20,
};
constexpr std::uint8_t kFindUnitBytes[]{
	0x48, 0x89, 0x5C, 0x24, 0x18, 0x88, 0x4C, 0x24, 0x08, 0x55, 0x56, 0x57,
	0x41, 0x54, 0x41, 0x55, 0x41, 0x56, 0x41, 0x57, 0x48, 0x83, 0xEC, 0x30,
};

// Called functions.
constexpr std::uint8_t kUnitsGetRoomBytes[]{
	0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B, 0xD9, 0x48, 0x85, 0xC9, 0x75, 0x13,
	0x88, 0x4C, 0x24, 0x30, 0x48, 0x8D, 0x4C, 0x24, 0x30, 0xE8, 0x54, 0xA7, 0xFF, 0xFF,
	0x84, 0xC0, 0x74, 0x01, 0xCC, 0x8B, 0x0B, 0x83, 0xE9, 0x02, 0x74, 0x25, 0x83, 0xE9,
	0x02, 0x74, 0x20, 0x83, 0xF9, 0x01, 0x74, 0x1B, 0x48, 0x8B, 0x4B, 0x38, 0x48, 0x85,
	0xC9, 0x74, 0x0A, 0x48, 0x83, 0xC4, 0x20, 0x5B, 0xE9, 0xAB, 0x67, 0xFF, 0xFF,
};
constexpr std::uint8_t kRoomIntersectsBytes[]{
	0x48, 0x89, 0x5C, 0x24, 0x10, 0x48, 0x89, 0x74, 0x24, 0x18, 0x57, 0x48, 0x83, 0xEC, 0x50,
};
constexpr std::uint8_t kUnitMatchesMaskBytes[]{
	0x48, 0x89, 0x5C, 0x24, 0x08, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x8B, 0x79, 0x0C, 0x8B,
	0xDA, 0xE8, 0x5C, 0x62, 0x02, 0x00,
};
constexpr std::uint8_t kUnitsGetSizeBytes[]{
	0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B, 0xD9, 0x48, 0x85, 0xC9, 0x75, 0x13,
	0x88, 0x4C, 0x24, 0x30, 0x48, 0x8D, 0x4C, 0x24, 0x30, 0xE8, 0x84, 0xAB, 0xFF, 0xFF,
};

// Layout witnesses.
constexpr std::uint8_t kRoomUnitListSlotBytes[]{ 0x48, 0x8D, 0x81, 0xA8, 0x00, 0x00, 0x00, 0xC3 };
constexpr std::uint8_t kRoomFirstUnitBytes[]{ 0x48, 0x8B, 0x81, 0xA8, 0x00, 0x00, 0x00, 0xC3 };
constexpr std::uint8_t kRoomNearListBytes[]{
	0x8B, 0x41, 0x40, 0x41, 0x89, 0x00, 0x48, 0x8B, 0x01, 0x48, 0x89, 0x02, 0xC3,
};
constexpr std::uint8_t kUnitNextInRoomBytes[]{
	0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B, 0xD9, 0x48, 0x85, 0xC9, 0x75, 0x20,
	0x88, 0x4C, 0x24, 0x30, 0x48, 0x8D, 0x4C, 0x24, 0x30, 0xE8, 0xB4, 0x9E, 0xFF, 0xFF,
	0x84, 0xC0, 0x74, 0x01, 0xCC, 0x48, 0x8B, 0x83, 0x60, 0x01, 0x00, 0x00, 0x48, 0x83,
	0xC4, 0x20, 0x5B, 0xC3, 0x48, 0x8B, 0x81, 0x60, 0x01, 0x00, 0x00, 0x48, 0x83, 0xC4,
	0x20, 0x5B, 0xC3,
};
constexpr std::uint8_t kPathGetXBytes[]{ 0x0F, 0xB7, 0x41, 0x02, 0xC3 };
constexpr std::uint8_t kPathGetYBytes[]{ 0x0F, 0xB7, 0x41, 0x06, 0xC3 };

// Collision rules. The server three share their first 36 bytes, the call
// displacement after that tells them apart.
constexpr std::uint8_t kServerRuleMonstersBytes[]{
	0x48, 0x89, 0x5C, 0x24, 0x08, 0x57, 0x48, 0x83, 0xEC, 0x30, 0x49, 0x8B, 0xD8, 0x48,
	0x8B, 0xFA, 0x4D, 0x85, 0xC0, 0x74, 0x07, 0x49, 0x83, 0x78, 0x18, 0x00, 0x75, 0x19,
	0x48, 0x8D, 0x4C, 0x24, 0x50, 0xC6, 0x44, 0x24, 0x50, 0x00, 0xE8, 0xA5, 0x9F, 0x00, 0x00,
};
constexpr std::uint8_t kServerRulePlayerSideBytes[]{
	0x48, 0x89, 0x5C, 0x24, 0x08, 0x57, 0x48, 0x83, 0xEC, 0x30, 0x49, 0x8B, 0xD8, 0x48,
	0x8B, 0xFA, 0x4D, 0x85, 0xC0, 0x74, 0x07, 0x49, 0x83, 0x78, 0x18, 0x00, 0x75, 0x19,
	0x48, 0x8D, 0x4C, 0x24, 0x50, 0xC6, 0x44, 0x24, 0x50, 0x00, 0xE8, 0x65, 0x9D, 0x00, 0x00,
};
constexpr std::uint8_t kServerRuleBothBytes[]{
	0x48, 0x89, 0x5C, 0x24, 0x08, 0x57, 0x48, 0x83, 0xEC, 0x30, 0x49, 0x8B, 0xD8, 0x48,
	0x8B, 0xFA, 0x4D, 0x85, 0xC0, 0x74, 0x07, 0x49, 0x83, 0x78, 0x18, 0x00, 0x75, 0x19,
	0x48, 0x8D, 0x4C, 0x24,
};
constexpr std::uint8_t kClientRuleMonstersBytes[]{
	0x48, 0x89, 0x5C, 0x24, 0x08, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B, 0xCA, 0x49,
	0x8B, 0xD8, 0x48, 0x8B, 0xFA, 0xE8, 0x98, 0xB9, 0x19, 0x00, 0x83, 0xF8, 0x01, 0x75, 0x27,
};
constexpr std::uint8_t kClientRulePlayerSideBytes[]{
	0x48, 0x89, 0x5C, 0x24, 0x08, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B, 0xCA, 0x49,
	0x8B, 0xF8, 0x48, 0x8B, 0xDA, 0xE8, 0x38, 0xB9, 0x19, 0x00, 0x83, 0xF8, 0x01, 0x75, 0x1A,
};
constexpr std::uint8_t kClientRuleBothBytes[]{
	0x48, 0x89, 0x5C, 0x24, 0x08, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B, 0xCA, 0x49,
	0x8B, 0xD8, 0x48, 0x8B, 0xFA, 0xE8, 0xC8, 0xB8, 0x19, 0x00, 0x83, 0xF8, 0x01, 0x77, 0x27,
};

// --------------------------------------------------------------------------
// Layout constants
// --------------------------------------------------------------------------

constexpr std::size_t kRoomNearListOffset  = 0x00;
constexpr std::size_t kRoomNearCountOffset = 0x40;
constexpr std::size_t kRoomUnitHeadOffset  = 0xA8;
constexpr std::size_t kUnitTypeOffset      = 0x00;
constexpr std::size_t kUnitPathOffset      = 0x38;
constexpr std::size_t kUnitNextOffset      = 0x160;
constexpr std::size_t kPathXOffset         = 0x02;
constexpr std::size_t kPathYOffset         = 0x06;

constexpr std::uint32_t kUnitTypeMonster = 1; // players are type 0

// The unit search always tests rooms against a square of this radius.
constexpr std::int32_t kSearchRoomRadius = 2;

// A real room list never gets anywhere near this long. Hitting it means the
// list is corrupt (a cycle), and the plugin switches itself off.
constexpr std::size_t kMaxRoomWalk = std::size_t{ 1 } << 21;

// Players and monsters one search may gather before it hands the call back
// to the engine untouched.
constexpr std::uint32_t kSnapshotCapacity = 512;

// --------------------------------------------------------------------------
// Game function types
// --------------------------------------------------------------------------

// The search passes its data context byte as the first rule argument.
using RuleFn               = std::uint32_t(__fastcall*)(std::uint64_t dataContext, void* unit, void* ruleContext) noexcept;
using AddUnitToRoomFn      = std::int64_t(__fastcall*)(void* unit, void* room, std::int32_t flag) noexcept;
using RemoveUnitFromRoomFn = void*(__fastcall*)(void* unit) noexcept;
using SortRoomUnitsFn      = void*(__fastcall*)(void* room) noexcept;
using FindUnitFn           = void*(__fastcall*)(std::uint8_t dataContext, void* room, std::int32_t x, std::int32_t y,
                                                RuleFn rule, void* ruleContext, std::int32_t size, std::uint32_t typeMask) noexcept;
using UnitsGetRoomFn       = void*(__fastcall*)(void* unit) noexcept;
using RoomIntersectsFn     = std::uint32_t(__fastcall*)(void* room, std::int32_t x, std::int32_t y, std::int32_t radius) noexcept;
using UnitMatchesMaskFn    = std::uint32_t(__fastcall*)(void* unit, std::uint32_t typeMask) noexcept;
using UnitsGetSizeFn       = std::int32_t(__fastcall*)(void* unit) noexcept;

// --------------------------------------------------------------------------
// Raw field access
// --------------------------------------------------------------------------

template <typename T>
auto ReadAt(const void* base, std::size_t offset) noexcept -> T {
	T value;
	std::memcpy(&value, static_cast<const std::uint8_t*>(base) + offset, sizeof(T));
	return value;
}

template <typename T>
void WriteAt(void* base, std::size_t offset, T value) noexcept {
	std::memcpy(static_cast<std::uint8_t*>(base) + offset, &value, sizeof(T));
}

auto RoomHead(void* room) noexcept -> void* { return ReadAt<void*>(room, kRoomUnitHeadOffset); }
void SetRoomHead(void* room, void* unit) noexcept { WriteAt<void*>(room, kRoomUnitHeadOffset, unit); }
auto NextInRoom(void* unit) noexcept -> void* { return ReadAt<void*>(unit, kUnitNextOffset); }
void SetNextInRoom(void* unit, void* next) noexcept { WriteAt<void*>(unit, kUnitNextOffset, next); }

auto IsPlayerOrMonster(void* unit) noexcept -> bool {
	return ReadAt<std::uint32_t>(unit, kUnitTypeOffset) <= kUnitTypeMonster;
}

// --------------------------------------------------------------------------
// Index data
// --------------------------------------------------------------------------

struct Node {
	void*         unit;
	void*         room;
	Node*         prev;     // shadow of the real list, every unit
	Node*         next;
	Node*         sidePrev; // players and monsters only
	Node*         sideNext;
	std::uint32_t stamp;
	bool          onSide;
};

struct RoomEntry {
	void*         room;
	Node*         head;
	Node*         sideHead;
	std::uint32_t count;
	std::uint32_t sideCount;
};

// Open addressing, linear probing, backward-shift deletion. Keys are never
// null.
template <typename Value>
class PointerMap {
public:
	auto Find(const void* key) const noexcept -> Value* {
		if (m_capacity == 0 || key == nullptr) {
			return nullptr;
		}
		const std::size_t mask = m_capacity - 1;
		for (std::size_t index = Hash(key) & mask;; index = (index + 1) & mask) {
			const Slot& slot = m_slots[index];
			if (slot.key == key) {
				return slot.value;
			}
			if (slot.key == nullptr) {
				return nullptr;
			}
		}
	}

	// The key must not be present.
	auto Insert(const void* key, Value* value) noexcept -> bool {
		if ((m_size + 1) * 2 > m_capacity && !Grow()) {
			return false;
		}
		Place(key, value);
		++m_size;
		return true;
	}

	void Erase(const void* key) noexcept {
		if (m_capacity == 0 || key == nullptr) {
			return;
		}
		const std::size_t mask = m_capacity - 1;
		std::size_t hole = Hash(key) & mask;
		for (;; hole = (hole + 1) & mask) {
			if (m_slots[hole].key == nullptr) {
				return;
			}
			if (m_slots[hole].key == key) {
				break;
			}
		}
		for (std::size_t probe = (hole + 1) & mask; m_slots[probe].key != nullptr; probe = (probe + 1) & mask) {
			const std::size_t home  = Hash(m_slots[probe].key) & mask;
			const bool        stays = hole <= probe ? (hole < home && home <= probe) : (hole < home || home <= probe);
			if (!stays) {
				m_slots[hole] = m_slots[probe];
				hole          = probe;
			}
		}
		m_slots[hole] = Slot{};
		--m_size;
	}

	template <typename Function>
	void ForEach(Function function) const noexcept {
		for (std::size_t index = 0; index < m_capacity; ++index) {
			if (m_slots[index].key != nullptr) {
				function(m_slots[index].value);
			}
		}
	}

	auto Size() const noexcept -> std::size_t { return m_size; }
	auto HeldBytes() const noexcept -> std::size_t { return m_capacity * sizeof(Slot); }

private:
	struct Slot {
		const void* key   = nullptr;
		Value*      value = nullptr;
	};

	static auto Hash(const void* key) noexcept -> std::size_t {
		std::uint64_t value = static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(key));
		value ^= value >> 33;
		value *= 0xFF51AFD7ED558CCDULL;
		value ^= value >> 33;
		value *= 0xC4CEB9FE1A85EC53ULL;
		value ^= value >> 33;
		return static_cast<std::size_t>(value);
	}

	void Place(const void* key, Value* value) noexcept {
		const std::size_t mask  = m_capacity - 1;
		std::size_t       index = Hash(key) & mask;
		while (m_slots[index].key != nullptr) {
			index = (index + 1) & mask;
		}
		m_slots[index] = Slot{ key, value };
	}

	auto Grow() noexcept -> bool {
		const std::size_t capacity = m_capacity == 0 ? std::size_t{ 4096 } : m_capacity * 2;
		Slot* fresh = new (std::nothrow) Slot[capacity];
		if (fresh == nullptr) {
			return false;
		}
		Slot* const       old         = m_slots;
		const std::size_t oldCapacity = m_capacity;
		m_slots    = fresh;
		m_capacity = capacity;
		for (std::size_t index = 0; index < oldCapacity; ++index) {
			if (old[index].key != nullptr) {
				Place(old[index].key, old[index].value);
			}
		}
		delete[] old;
		return true;
	}

	Slot*       m_slots    = nullptr;
	std::size_t m_capacity = 0;
	std::size_t m_size     = 0;
};

class NodePool {
public:
	auto Alloc() noexcept -> Node* {
		if (m_free == nullptr && !Refill()) {
			return nullptr;
		}
		Node* node = m_free;
		m_free     = node->next;
		*node      = Node{};
		++m_live;
		return node;
	}

	void Free(Node* node) noexcept {
		*node      = Node{};
		node->next = m_free;
		m_free     = node;
		--m_live;
	}

	auto Live() const noexcept -> std::size_t { return m_live; }
	auto HeldBytes() const noexcept -> std::size_t { return m_chunkCount * sizeof(Chunk); }

private:
	struct Chunk {
		Chunk* next;
		Node   nodes[1024];
	};

	auto Refill() noexcept -> bool {
		Chunk* chunk = new (std::nothrow) Chunk;
		if (chunk == nullptr) {
			return false;
		}
		chunk->next = m_chunks;
		m_chunks    = chunk;
		++m_chunkCount;
		for (Node& node : chunk->nodes) {
			node      = Node{};
			node.next = m_free;
			m_free    = &node;
		}
		return true;
	}

	Chunk*      m_chunks     = nullptr;
	Node*       m_free       = nullptr;
	std::size_t m_live       = 0;
	std::size_t m_chunkCount = 0;
};

class PointerStack {
public:
	PointerStack() noexcept = default;
	PointerStack(const PointerStack&) = delete;
	auto operator=(const PointerStack&) -> PointerStack& = delete;
	~PointerStack() { delete[] m_items; }

	auto Push(void* value) noexcept -> bool {
		if (m_size == m_capacity) {
			const std::size_t capacity = m_capacity == 0 ? std::size_t{ 256 } : m_capacity * 2;
			void** fresh = new (std::nothrow) void*[capacity];
			if (fresh == nullptr) {
				return false;
			}
			if (m_size != 0) {
				std::memcpy(fresh, m_items, m_size * sizeof(void*));
			}
			delete[] m_items;
			m_items    = fresh;
			m_capacity = capacity;
		}
		m_items[m_size++] = value;
		return true;
	}

	void Clear() noexcept { m_size = 0; }
	auto Size() const noexcept -> std::size_t { return m_size; }
	auto HeldBytes() const noexcept -> std::size_t { return m_capacity * sizeof(void*); }
	auto At(std::size_t index) const noexcept -> void* { return m_items[index]; }

private:
	void**      m_items    = nullptr;
	std::size_t m_size     = 0;
	std::size_t m_capacity = 0;
};

struct Settings {
	bool enabled     = true;
	bool fastRemoval = true;
	bool searchIndex = true;
};

struct Stats {
	volatile LONG64 adds             = 0;
	volatile LONG64 removes          = 0;
	volatile LONG64 removesFast      = 0;
	volatile LONG64 removesAtHead    = 0;
	volatile LONG64 removesWalked    = 0;
	volatile LONG64 reorders         = 0;
	volatile LONG64 rebuilds         = 0;
	volatile LONG64 inconsistencies  = 0;
	volatile LONG64 searchesIndexed  = 0;
	volatile LONG64 searchesPassed   = 0;
	volatile LONG64 unitsNotVisited  = 0;
};

// --------------------------------------------------------------------------
// State
// --------------------------------------------------------------------------

const D2RL::PluginContext* g_context    = nullptr;
std::uintptr_t             g_moduleBase = 0;
Settings                   g_settings{};
Stats                      g_stats{};

AddUnitToRoomFn      g_originalAdd    = nullptr;
RemoveUnitFromRoomFn g_originalRemove = nullptr;
SortRoomUnitsFn      g_originalSort   = nullptr;
FindUnitFn           g_originalFind   = nullptr;

UnitsGetRoomFn    g_unitsGetRoom    = nullptr;
RoomIntersectsFn  g_roomIntersects  = nullptr;
UnitMatchesMaskFn g_unitMatchesMask = nullptr;
UnitsGetSizeFn    g_unitsGetSize    = nullptr;

std::uintptr_t g_playerMonsterRules[6]{};

SRWLOCK g_lock = SRWLOCK_INIT;

PointerMap<Node>      g_units;
PointerMap<RoomEntry> g_rooms;
NodePool              g_nodes;
PointerStack          g_scratch;
std::uint32_t         g_stamp = 0;

volatile LONG g_indexLive   = 0;
volatile LONG g_searchLive  = 0;
volatile LONG g_brokenState = 0; // 0 fine, 1 broken and not reported yet, 2 reported
const char*   g_brokenReason = "";

// --------------------------------------------------------------------------
// Small helpers
// --------------------------------------------------------------------------

void Bump(volatile LONG64& counter) noexcept { InterlockedIncrement64(&counter); }
void Add(volatile LONG64& counter, LONG64 amount) noexcept { InterlockedExchangeAdd64(&counter, amount); }
auto Read(const volatile LONG64& counter) noexcept -> long long { return static_cast<long long>(counter); }

auto IndexLive() noexcept -> bool { return g_indexLive != 0; }
auto SearchLive() noexcept -> bool { return g_searchLive != 0 && g_indexLive != 0; }

// Switches the whole plugin to pass-through. Called with the lock held; the
// message is written by ReportBroken once the lock is released.
void MarkBroken(const char* reason) noexcept {
	InterlockedExchange(&g_indexLive, 0);
	InterlockedExchange(&g_searchLive, 0);
	if (InterlockedCompareExchange(&g_brokenState, 1, 0) == 0) {
		g_brokenReason = reason;
	}
}

void ReportBroken() noexcept {
	if (g_brokenState != 1 || InterlockedCompareExchange(&g_brokenState, 2, 1) != 1 || g_context == nullptr) {
		return;
	}
	char message[256]{};
	std::snprintf(message, sizeof(message),
	              "Room unit index switched itself off: %s. The engine's own code runs from here on.",
	              g_brokenReason);
	g_context->LogError(message);
}

auto IsPlayerMonsterRule(RuleFn rule) noexcept -> bool {
	const std::uintptr_t address = reinterpret_cast<std::uintptr_t>(rule);
	for (const std::uintptr_t candidate : g_playerMonsterRules) {
		if (candidate != 0 && candidate == address) {
			return true;
		}
	}
	return false;
}

// The unit search's footprint test, reproduced case for case from 0x324BC0.
// selector = min(unit size, 3) + 3 * searcher size - 4.
auto CellMatches(std::int32_t selector, std::int32_t x, std::int32_t y, std::int32_t unitX, std::int32_t unitY) noexcept -> bool {
	const std::int32_t dx = x > unitX ? x - unitX : unitX - x;
	const std::int32_t dy = y > unitY ? y - unitY : unitY - y;
	switch (selector) {
		case 0:
			return x == unitX && y == unitY;
		case 1:
		case 3:
			return dx + dy <= 1;
		case 2:
		case 6:
			return dx <= 1 && dy <= 1;
		case 4:
			return dx + dy <= 2;
		case 5:
		case 7:
			return (dx <= 2 && dy <= 1) || (dy <= 2 && dx <= 1);
		case 8:
			return dx <= 2 && dy <= 2;
		default:
			return false;
	}
}

// --------------------------------------------------------------------------
// Index maintenance. Every function here runs with g_lock held exclusively.
// --------------------------------------------------------------------------

auto FindOrCreateRoom(void* room) noexcept -> RoomEntry* {
	RoomEntry* entry = g_rooms.Find(room);
	if (entry != nullptr) {
		return entry;
	}
	entry = new (std::nothrow) RoomEntry{};
	if (entry == nullptr) {
		return nullptr;
	}
	entry->room = room;
	if (!g_rooms.Insert(room, entry)) {
		delete entry;
		return nullptr;
	}
	return entry;
}

void EraseRoomIfEmpty(RoomEntry* entry) noexcept {
	if (entry != nullptr && entry->count == 0 && entry->head == nullptr) {
		g_rooms.Erase(entry->room);
		delete entry;
	}
}

auto NewNode(void* unit) noexcept -> Node* {
	Node* node = g_nodes.Alloc();
	if (node == nullptr) {
		MarkBroken("out of memory");
		return nullptr;
	}
	node->unit = unit;
	if (!g_units.Insert(unit, node)) {
		g_nodes.Free(node);
		MarkBroken("out of memory");
		return nullptr;
	}
	return node;
}

void ForgetNode(Node* node) noexcept {
	g_units.Erase(node->unit);
	g_nodes.Free(node);
}

void LinkAtHead(RoomEntry* entry, Node* node) noexcept {
	node->prev = nullptr;
	node->next = entry->head;
	if (entry->head != nullptr) {
		entry->head->prev = node;
	}
	entry->head = node;
	++entry->count;

	node->sidePrev = nullptr;
	node->sideNext = nullptr;
	if (node->onSide) {
		node->sideNext = entry->sideHead;
		if (entry->sideHead != nullptr) {
			entry->sideHead->sidePrev = node;
		}
		entry->sideHead = node;
		++entry->sideCount;
	}
}

void AppendAtTail(RoomEntry* entry, Node* node, Node*& tail, Node*& sideTail) noexcept {
	node->prev = tail;
	node->next = nullptr;
	if (tail != nullptr) {
		tail->next = node;
	} else {
		entry->head = node;
	}
	tail = node;
	++entry->count;

	node->sidePrev = nullptr;
	node->sideNext = nullptr;
	if (node->onSide) {
		node->sidePrev = sideTail;
		if (sideTail != nullptr) {
			sideTail->sideNext = node;
		} else {
			entry->sideHead = node;
		}
		sideTail = node;
		++entry->sideCount;
	}
}

void ClearLinks(Node* node) noexcept {
	node->prev     = nullptr;
	node->next     = nullptr;
	node->sidePrev = nullptr;
	node->sideNext = nullptr;
	node->room     = nullptr;
}

void UnlinkFromRoom(RoomEntry* entry, Node* node) noexcept {
	if (node->prev != nullptr) {
		node->prev->next = node->next;
	} else if (entry->head == node) {
		entry->head = node->next;
	}
	if (node->next != nullptr) {
		node->next->prev = node->prev;
	}
	if (entry->count != 0) {
		--entry->count;
	}

	if (node->onSide) {
		if (node->sidePrev != nullptr) {
			node->sidePrev->sideNext = node->sideNext;
		} else if (entry->sideHead == node) {
			entry->sideHead = node->sideNext;
		}
		if (node->sideNext != nullptr) {
			node->sideNext->sidePrev = node->sidePrev;
		}
		if (entry->sideCount != 0) {
			--entry->sideCount;
		}
	}
	ClearLinks(node);
}

void DetachNode(Node* node) noexcept {
	RoomEntry* entry = node->room != nullptr ? g_rooms.Find(node->room) : nullptr;
	if (entry != nullptr) {
		UnlinkFromRoom(entry, node);
	} else {
		ClearLinks(node);
	}
}

// Rebuilds one room's shadow and players/monsters lists from its real list.
void RebuildRoom(void* room) noexcept {
	Bump(g_stats.rebuilds);
	RoomEntry* entry = FindOrCreateRoom(room);
	if (entry == nullptr) {
		MarkBroken("out of memory");
		return;
	}

	g_scratch.Clear();
	std::size_t guard = 0;
	for (Node* node = entry->head; node != nullptr;) {
		Node* next = node->next;
		if (!g_scratch.Push(node)) {
			MarkBroken("out of memory");
			return;
		}
		ClearLinks(node);
		node = next;
		if (++guard > kMaxRoomWalk) {
			MarkBroken("an index list exceeded the walk limit");
			return;
		}
	}
	entry->head      = nullptr;
	entry->sideHead  = nullptr;
	entry->count     = 0;
	entry->sideCount = 0;

	Node* tail     = nullptr;
	Node* sideTail = nullptr;
	guard = 0;
	for (void* unit = RoomHead(room); unit != nullptr; unit = NextInRoom(unit)) {
		if (++guard > kMaxRoomWalk) {
			MarkBroken("a room unit list exceeded the walk limit (corrupt list)");
			return;
		}
		Node* node = g_units.Find(unit);
		if (node != nullptr && node->room == room) {
			MarkBroken("a unit is listed twice in one room unit list (corrupt list)");
			return;
		}
		if (node != nullptr && node->room != nullptr) {
			// The index had this unit in another room.
			Bump(g_stats.inconsistencies);
			RoomEntry* other = g_rooms.Find(node->room);
			DetachNode(node);
			EraseRoomIfEmpty(other);
		}
		if (node == nullptr) {
			node = NewNode(unit);
			if (node == nullptr) {
				return;
			}
		}
		node->room   = room;
		node->onSide = IsPlayerOrMonster(unit);
		AppendAtTail(entry, node, tail, sideTail);
	}

	// Units the index held for this room that are no longer in its list.
	for (std::size_t index = 0; index < g_scratch.Size(); ++index) {
		Node* node = static_cast<Node*>(g_scratch.At(index));
		if (node->room == nullptr) {
			ForgetNode(node);
		}
	}
	EraseRoomIfEmpty(entry);
}

// True when the shadow list holds exactly the real list, in order.
auto ShadowMatches(const RoomEntry* entry, void* room) noexcept -> bool {
	const Node*   node  = entry->head;
	void*         unit  = RoomHead(room);
	std::uint32_t count = 0;
	while (node != nullptr && unit != nullptr) {
		if (node->unit != unit || ++count > entry->count) {
			return false;
		}
		node = node->next;
		unit = NextInRoom(unit);
	}
	return node == nullptr && unit == nullptr && count == entry->count;
}

// True when the players/monsters list is the shadow list filtered by type.
auto SideMatches(const RoomEntry* entry) noexcept -> bool {
	const Node*   side  = entry->sideHead;
	std::uint32_t count = 0;
	for (const Node* node = entry->head; node != nullptr; node = node->next) {
		if (node->onSide != IsPlayerOrMonster(node->unit)) {
			return false;
		}
		if (!node->onSide) {
			continue;
		}
		if (side != node) {
			return false;
		}
		side = side->sideNext;
		++count;
	}
	return side == nullptr && count == entry->sideCount;
}

// After the engine reordered a room's list (the sort pass), re-threads the
// existing nodes in the new order. Fails without touching anything when the
// real list holds a unit the index does not have in this room.
auto ReThreadRoom(RoomEntry* entry, void* room) noexcept -> bool {
	const std::uint32_t stamp = ++g_stamp;
	std::uint32_t       count = 0;
	std::size_t         guard = 0;
	for (void* unit = RoomHead(room); unit != nullptr; unit = NextInRoom(unit)) {
		if (++guard > kMaxRoomWalk) {
			return false;
		}
		Node* node = g_units.Find(unit);
		if (node == nullptr || node->room != room || node->stamp == stamp) {
			return false;
		}
		node->stamp = stamp;
		++count;
	}
	if (count != entry->count) {
		return false;
	}

	entry->head      = nullptr;
	entry->sideHead  = nullptr;
	entry->count     = 0;
	entry->sideCount = 0;
	Node* tail     = nullptr;
	Node* sideTail = nullptr;
	for (void* unit = RoomHead(room); unit != nullptr; unit = NextInRoom(unit)) {
		AppendAtTail(entry, g_units.Find(unit), tail, sideTail);
	}
	return true;
}

// Runs after the engine linked `unit` at the head of `room`'s list.
void OnUnitAdded(void* unit, void* room) noexcept {
	Bump(g_stats.adds);
	RoomEntry* entry = FindOrCreateRoom(room);
	if (entry == nullptr) {
		MarkBroken("out of memory");
		return;
	}

	Node* node = g_units.Find(unit);
	if (node != nullptr) {
		// Added again without a removal the index saw.
		Bump(g_stats.inconsistencies);
		void* previousRoom = node->room;
		DetachNode(node);
		if (previousRoom != nullptr && previousRoom != room) {
			EraseRoomIfEmpty(g_rooms.Find(previousRoom));
		}
	} else {
		node = NewNode(unit);
		if (node == nullptr) {
			return;
		}
	}

	void* const expectedNext = entry->head != nullptr ? entry->head->unit : nullptr;
	node->room   = room;
	node->onSide = IsPlayerOrMonster(unit);
	LinkAtHead(entry, node);

	if (RoomHead(room) != unit || NextInRoom(unit) != expectedNext) {
		Bump(g_stats.inconsistencies);
		RebuildRoom(room);
	}
}

// --------------------------------------------------------------------------
// Hooks
// --------------------------------------------------------------------------

auto __fastcall HookAddUnitToRoom(void* unit, void* room, std::int32_t flag) noexcept -> std::int64_t {
	const std::int64_t result = g_originalAdd(unit, room, flag);
	if (IndexLive() && unit != nullptr && room != nullptr) {
		AcquireSRWLockExclusive(&g_lock);
		if (IndexLive()) {
			OnUnitAdded(unit, room);
		}
		ReleaseSRWLockExclusive(&g_lock);
		ReportBroken();
	}
	return result;
}

auto __fastcall HookRemoveUnitFromRoom(void* unit) noexcept -> void* {
	if (!IndexLive() || unit == nullptr) {
		return g_originalRemove(unit);
	}

	void* room         = nullptr;
	bool  rebuildAfter = false;

	AcquireSRWLockExclusive(&g_lock);
	if (IndexLive()) {
		Bump(g_stats.removes);
		room = g_unitsGetRoom(unit); // the room the engine is about to unlink from
		if (room != nullptr) {
			Node* node = g_units.Find(unit);
			if (node != nullptr && node->room == room) {
				if (node->prev == nullptr) {
					if (RoomHead(room) == unit) {
						Bump(g_stats.removesAtHead);
					} else {
						Bump(g_stats.inconsistencies);
						rebuildAfter = true;
					}
				} else if (g_settings.fastRemoval) {
					void* const previous = node->prev->unit;
					if (NextInRoom(previous) == unit) {
						// Move the unit to the front. The engine's walk then
						// stops on its first step and unlinks it from there.
						SetNextInRoom(previous, NextInRoom(unit));
						SetNextInRoom(unit, RoomHead(room));
						SetRoomHead(room, unit);
						Bump(g_stats.removesFast);
					} else {
						Bump(g_stats.inconsistencies);
						rebuildAfter = true;
					}
				} else {
					Bump(g_stats.removesWalked);
				}
			} else if (node != nullptr || RoomHead(room) != nullptr) {
				// Indexed in another room, or not indexed in a populated room.
				Bump(g_stats.inconsistencies);
				rebuildAfter = true;
			}
		}
	}
	ReleaseSRWLockExclusive(&g_lock);

	void* const result = g_originalRemove(unit);

	if (room != nullptr) {
		AcquireSRWLockExclusive(&g_lock);
		if (IndexLive()) {
			Node* node = g_units.Find(unit);
			if (node != nullptr) {
				void* const trackedRoom = node->room;
				RoomEntry*  tracked     = trackedRoom != nullptr ? g_rooms.Find(trackedRoom) : nullptr;
				DetachNode(node);
				ForgetNode(node);
				if (trackedRoom != room) {
					Bump(g_stats.inconsistencies);
					EraseRoomIfEmpty(tracked);
				}
			}
			if (RoomHead(room) == unit) {
				// The engine did not unlink it.
				Bump(g_stats.inconsistencies);
				rebuildAfter = true;
			}
			if (rebuildAfter) {
				RebuildRoom(room);
			} else {
				EraseRoomIfEmpty(g_rooms.Find(room));
			}
		}
		ReleaseSRWLockExclusive(&g_lock);
		ReportBroken();
	}
	return result;
}

auto __fastcall HookSortRoomUnits(void* room) noexcept -> void* {
	void* const result = g_originalSort(room);
	if (IndexLive() && room != nullptr) {
		AcquireSRWLockExclusive(&g_lock);
		if (IndexLive()) {
			RoomEntry* entry = g_rooms.Find(room);
			if (entry != nullptr) {
				if (!ShadowMatches(entry, room)) {
					if (ReThreadRoom(entry, room)) {
						Bump(g_stats.reorders);
					} else {
						Bump(g_stats.inconsistencies);
						RebuildRoom(room);
					}
				}
			} else if (RoomHead(room) != nullptr) {
				Bump(g_stats.inconsistencies);
				RebuildRoom(room);
			}
		}
		ReleaseSRWLockExclusive(&g_lock);
		ReportBroken();
	}
	return result;
}

auto __fastcall HookFindUnit(std::uint8_t  dataContext,
                             void*         room,
                             std::int32_t  x,
                             std::int32_t  y,
                             RuleFn        rule,
                             void*         ruleContext,
                             std::int32_t  size,
                             std::uint32_t typeMask) noexcept -> void* {
	if (!SearchLive() || room == nullptr || size < 1 || size > 3 || !IsPlayerMonsterRule(rule)) {
		Bump(g_stats.searchesPassed);
		return g_originalFind(dataContext, room, x, y, rule, ruleContext, size, typeMask);
	}

	void* const* const nearRooms = ReadAt<void* const*>(room, kRoomNearListOffset);
	const std::int32_t nearCount = ReadAt<std::int32_t>(room, kRoomNearCountOffset);
	if (nearCount <= 0) {
		Bump(g_stats.searchesIndexed);
		return nullptr;
	}

	// Gather the candidates first, in the engine's order: near rooms in list
	// order, units in list order. The tests below call engine code, which
	// runs without the lock.
	void*         snapshot[kSnapshotCapacity];
	std::uint32_t taken     = 0;
	bool          overflow  = false;
	void*         staleRoom = nullptr;
	LONG64        skipped   = 0;

	AcquireSRWLockShared(&g_lock);
	const bool live = SearchLive();
	if (live) {
		for (std::int32_t index = 0; index < nearCount && !overflow; ++index) {
			void* const nearRoom = nearRooms[index];
			if (g_roomIntersects(nearRoom, x, y, kSearchRoomRadius) == 0) {
				continue;
			}
			const RoomEntry* entry = g_rooms.Find(nearRoom);
			if (entry != nullptr) {
				// Units always enter a list at its head, so a unit linked by
				// code the index does not know about shows up here. That
				// search goes to the engine and the room gets rebuilt.
				void* const shadowHead = entry->head != nullptr ? entry->head->unit : nullptr;
				if (shadowHead != RoomHead(nearRoom)) {
					staleRoom = nearRoom;
					overflow  = true;
					break;
				}
				skipped += static_cast<LONG64>(entry->count - entry->sideCount);
				for (const Node* node = entry->sideHead; node != nullptr; node = node->sideNext) {
					if (taken == kSnapshotCapacity) {
						overflow = true;
						break;
					}
					snapshot[taken++] = node->unit;
				}
			} else {
				// No entry means no unit was added to this room since the
				// plugin loaded, so the list is normally empty. Walk it anyway.
				std::size_t guard = 0;
				for (void* unit = RoomHead(nearRoom); unit != nullptr; unit = NextInRoom(unit)) {
					if (++guard > kMaxRoomWalk || taken == kSnapshotCapacity) {
						overflow = true;
						break;
					}
					if (IsPlayerOrMonster(unit)) {
						snapshot[taken++] = unit;
					}
				}
			}
		}
	}
	ReleaseSRWLockShared(&g_lock);

	if (staleRoom != nullptr) {
		AcquireSRWLockExclusive(&g_lock);
		if (IndexLive()) {
			Bump(g_stats.inconsistencies);
			RebuildRoom(staleRoom);
		}
		ReleaseSRWLockExclusive(&g_lock);
		ReportBroken();
	}

	if (!live || overflow) {
		Bump(g_stats.searchesPassed);
		return g_originalFind(dataContext, room, x, y, rule, ruleContext, size, typeMask);
	}
	Bump(g_stats.searchesIndexed);
	Add(g_stats.unitsNotVisited, skipped);

	const std::int32_t sizeTerm = 3 * size - 4;
	for (std::uint32_t index = 0; index < taken; ++index) {
		void* const unit = snapshot[index];
		if (g_unitMatchesMask(unit, typeMask) == 0) {
			continue;
		}
		std::int32_t unitSize = g_unitsGetSize(unit);
		if (unitSize > 3) {
			unitSize = 3;
		}
		if (unitSize <= 0) {
			continue;
		}
		// Players and monsters keep a dynamic path: x/y are words at +2/+6.
		void* const        path  = ReadAt<void*>(unit, kUnitPathOffset);
		const std::int32_t unitX = path != nullptr ? static_cast<std::int32_t>(ReadAt<std::uint16_t>(path, kPathXOffset)) : 0;
		const std::int32_t unitY = path != nullptr ? static_cast<std::int32_t>(ReadAt<std::uint16_t>(path, kPathYOffset)) : 0;
		if (!CellMatches(unitSize + sizeTerm, x, y, unitX, unitY)) {
			continue;
		}
		if (rule(dataContext, unit, ruleContext) != 0) {
			return unit;
		}
	}
	return nullptr;
}

// --------------------------------------------------------------------------
// Verification (console)
// --------------------------------------------------------------------------

struct VerifyResult {
	std::uint32_t rooms    = 0;
	std::uint64_t units    = 0;
	std::uint32_t repaired = 0;
	bool          live     = false;
};

auto VerifyIndex() noexcept -> VerifyResult {
	VerifyResult result{};
	PointerStack rooms;

	AcquireSRWLockExclusive(&g_lock);
	result.live = IndexLive();
	if (result.live) {
		bool complete = true;
		g_rooms.ForEach([&](RoomEntry* entry) {
			if (entry->count != 0 && !rooms.Push(entry->room)) {
				complete = false;
			}
		});
		(void)complete;
		for (std::size_t index = 0; index < rooms.Size() && IndexLive(); ++index) {
			void* const room  = rooms.At(index);
			RoomEntry*  entry = g_rooms.Find(room);
			if (entry == nullptr) {
				continue;
			}
			++result.rooms;
			result.units += entry->count;
			if (!ShadowMatches(entry, room) || !SideMatches(entry)) {
				++result.repaired;
				Bump(g_stats.inconsistencies);
				RebuildRoom(room);
			}
		}
	}
	ReleaseSRWLockExclusive(&g_lock);
	ReportBroken();
	return result;
}

// ---- installation and console (room index) ----

using perf_common::ByteCount;
using perf_common::CheckSite;

auto CheckAllSites(const D2RL::PluginContext* context) noexcept -> bool {
	struct Site {
		std::uint64_t       rva;
		const std::uint8_t* bytes;
		std::uint32_t       size;
		const char*         what;
	};
	const Site sites[]{
		{ kAddUnitToRoomRva, kAddUnitToRoomBytes, ByteCount(kAddUnitToRoomBytes), "Room index: add unit to room" },
		{ kRemoveUnitFromRoomRva, kRemoveUnitFromRoomBytes, ByteCount(kRemoveUnitFromRoomBytes), "Room index: remove unit from room" },
		{ kSortRoomUnitsRva, kSortRoomUnitsBytes, ByteCount(kSortRoomUnitsBytes), "Room index: room unit sort pass" },
		{ kFindUnitRva, kFindUnitBytes, ByteCount(kFindUnitBytes), "Room index: unit search" },
		{ kUnitsGetRoomRva, kUnitsGetRoomBytes, ByteCount(kUnitsGetRoomBytes), "Room index: unit room getter" },
		{ kRoomIntersectsRva, kRoomIntersectsBytes, ByteCount(kRoomIntersectsBytes), "Room index: room search square test" },
		{ kUnitMatchesMaskRva, kUnitMatchesMaskBytes, ByteCount(kUnitMatchesMaskBytes), "Room index: unit type mask test" },
		{ kUnitsGetSizeRva, kUnitsGetSizeBytes, ByteCount(kUnitsGetSizeBytes), "Room index: unit size getter" },
		{ kRoomUnitListSlotRva, kRoomUnitListSlotBytes, ByteCount(kRoomUnitListSlotBytes), "Room index: room unit list slot" },
		{ kRoomFirstUnitRva, kRoomFirstUnitBytes, ByteCount(kRoomFirstUnitBytes), "Room index: room first unit" },
		{ kRoomNearListRva, kRoomNearListBytes, ByteCount(kRoomNearListBytes), "Room index: room near list" },
		{ kUnitNextInRoomRva, kUnitNextInRoomBytes, ByteCount(kUnitNextInRoomBytes), "Room index: next unit in room" },
		{ kPathGetXRva, kPathGetXBytes, ByteCount(kPathGetXBytes), "Room index: path X getter" },
		{ kPathGetYRva, kPathGetYBytes, ByteCount(kPathGetYBytes), "Room index: path Y getter" },
		{ kServerRulePlayerSideRva, kServerRulePlayerSideBytes, ByteCount(kServerRulePlayerSideBytes), "Room index: server collide rule 1" },
		{ kServerRuleMonstersRva, kServerRuleMonstersBytes, ByteCount(kServerRuleMonstersBytes), "Room index: server collide rule 2/5" },
		{ kServerRuleBothRva, kServerRuleBothBytes, ByteCount(kServerRuleBothBytes), "Room index: server collide rule 3/8" },
		{ kClientRulePlayerSideRva, kClientRulePlayerSideBytes, ByteCount(kClientRulePlayerSideBytes), "Room index: client collide rule 1" },
		{ kClientRuleMonstersRva, kClientRuleMonstersBytes, ByteCount(kClientRuleMonstersBytes), "Room index: client collide rule 2/5" },
		{ kClientRuleBothRva, kClientRuleBothBytes, ByteCount(kClientRuleBothBytes), "Room index: client collide rule 3/8" },
	};
	bool ok = true;
	for (const Site& site : sites) {
		ok = CheckSite(context, site.rva, site.bytes, site.size, site.what) && ok;
	}
	return ok;
}

auto Install(const D2RL::PluginContext* context, bool fastRemoval, bool searchIndex) noexcept -> bool {
	g_context              = context;
	g_settings.enabled     = true;
	g_settings.fastRemoval = fastRemoval;
	g_settings.searchIndex = searchIndex;

	g_moduleBase = reinterpret_cast<std::uintptr_t>(::GetModuleHandleW(nullptr));
	if (g_moduleBase == 0) {
		context->LogError("Room index: could not resolve the main module base.");
		return false;
	}
	if (!CheckAllSites(context)) {
		return false;
	}

	g_unitsGetRoom    = reinterpret_cast<UnitsGetRoomFn>(g_moduleBase + kUnitsGetRoomRva);
	g_roomIntersects  = reinterpret_cast<RoomIntersectsFn>(g_moduleBase + kRoomIntersectsRva);
	g_unitMatchesMask = reinterpret_cast<UnitMatchesMaskFn>(g_moduleBase + kUnitMatchesMaskRva);
	g_unitsGetSize    = reinterpret_cast<UnitsGetSizeFn>(g_moduleBase + kUnitsGetSizeRva);

	g_playerMonsterRules[0] = g_moduleBase + kServerRulePlayerSideRva;
	g_playerMonsterRules[1] = g_moduleBase + kServerRuleMonstersRva;
	g_playerMonsterRules[2] = g_moduleBase + kServerRuleBothRva;
	g_playerMonsterRules[3] = g_moduleBase + kClientRulePlayerSideRva;
	g_playerMonsterRules[4] = g_moduleBase + kClientRuleMonstersRva;
	g_playerMonsterRules[5] = g_moduleBase + kClientRuleBothRva;

	if (!context->InstallInlineHook(kAddUnitToRoomRva, kAddUnitToRoomBytes, ByteCount(kAddUnitToRoomBytes),
	                                &HookAddUnitToRoom, &g_originalAdd)) {
		context->LogError("Room index: failed to install the add-unit-to-room hook.");
		return false;
	}
	// From here on a failed hook only switches the index off, which makes
	// every installed hook pass straight through to the game.
	const bool listHooks =
		context->InstallInlineHook(kRemoveUnitFromRoomRva, kRemoveUnitFromRoomBytes, ByteCount(kRemoveUnitFromRoomBytes),
		                           &HookRemoveUnitFromRoom, &g_originalRemove)
		&& context->InstallInlineHook(kSortRoomUnitsRva, kSortRoomUnitsBytes, ByteCount(kSortRoomUnitsBytes),
		                              &HookSortRoomUnits, &g_originalSort);
	if (!listHooks) {
		context->LogError("Room index: failed to install the room list hooks; the game's own code stays in use.");
		return false;
	}
	InterlockedExchange(&g_indexLive, 1);
	if (searchIndex) {
		if (context->InstallInlineHook(kFindUnitRva, kFindUnitBytes, ByteCount(kFindUnitBytes), &HookFindUnit, &g_originalFind)) {
			InterlockedExchange(&g_searchLive, 1);
		} else {
			context->LogError("Room index: failed to install the unit search hook; fast removal still works.");
		}
	}
	return true;
}

struct MemoryHeldResult {
	std::uint64_t bytes;
	std::size_t   units;
};

auto MemoryHeld() noexcept -> MemoryHeldResult {
	AcquireSRWLockShared(&g_lock);
	const std::uint64_t bytes = g_nodes.HeldBytes() + g_units.HeldBytes() + g_rooms.HeldBytes()
	                          + g_rooms.Size() * sizeof(RoomEntry) + g_scratch.HeldBytes();
	const std::size_t units = g_units.Size();
	ReleaseSRWLockShared(&g_lock);
	return { bytes, units };
}

void Uninstall() noexcept {
	InterlockedExchange(&g_searchLive, 0);
	InterlockedExchange(&g_indexLive, 0);
}

void ResetCounters() noexcept {
	volatile LONG64* counters[]{
		&g_stats.adds, &g_stats.removes, &g_stats.removesFast, &g_stats.removesAtHead,
		&g_stats.removesWalked, &g_stats.reorders, &g_stats.rebuilds, &g_stats.inconsistencies,
		&g_stats.searchesIndexed, &g_stats.searchesPassed, &g_stats.unitsNotVisited,
	};
	for (volatile LONG64* counter : counters) {
		InterlockedExchange64(counter, 0);
	}
}

void Status(const D2RL::PluginContext* console) noexcept {
	std::size_t rooms = 0;
	std::size_t units = 0;
	AcquireSRWLockShared(&g_lock);
	rooms = g_rooms.Size();
	units = g_units.Size();
	ReleaseSRWLockShared(&g_lock);

	char line[256]{};
	std::snprintf(line, sizeof(line), "  index %s, unit search %s, fast removal %s; %zu rooms, %zu units indexed.",
	              IndexLive() ? "live" : "OFF", SearchLive() ? "live" : "off", g_settings.fastRemoval ? "on" : "off", rooms, units);
	console->WriteConsoleMessage(line);
	std::snprintf(line, sizeof(line), "  removes %lld (moved to front %lld, already first %lld, walked %lld), reorders %lld.",
	              Read(g_stats.removes), Read(g_stats.removesFast), Read(g_stats.removesAtHead), Read(g_stats.removesWalked),
	              Read(g_stats.reorders));
	console->WriteConsoleMessage(line);
	std::snprintf(line, sizeof(line), "  searches indexed %lld, passed to the game %lld, units not visited %lld; inconsistencies %lld%s.",
	              Read(g_stats.searchesIndexed), Read(g_stats.searchesPassed), Read(g_stats.unitsNotVisited),
	              Read(g_stats.inconsistencies), g_brokenState != 0 ? " (switched itself off, see the log)" : "");
	console->WriteConsoleMessage(line);
}

} // namespace room_index

// ===========================================================================
// Room add check (byte patch)
//
// UNITROOM_AddUnitToRoomEx walks the room's whole unit list before linking a
// unit, only to feed a debug assert when the unit is already there. The walk
// changes nothing in game. A short jump from the loop setup (0x38ED8B)
// straight to the insertion (0x38EDC9) skips it; nothing after the loop reads
// the registers it used. If the patch file skip-room-add-duplicate-check.json
// already applied the same bytes, they are left as they are.
// ===========================================================================

namespace room_add_check {

constexpr std::uint64_t kRva = 0x0038ED8BULL;
constexpr std::uint8_t  kOriginal[]{ 0x48, 0x8B, 0xCE, 0xE8, 0xFD, 0x0F, 0xF6, 0xFF };
constexpr std::uint8_t  kPatched[]{ 0xEB, 0x3C, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90 };

bool g_byPatchFile = false;

auto Install(const D2RL::PluginContext* context) noexcept -> bool {
	// Read the 8 bytes directly and compare them with both known states. The
	// loader logs every failed CheckExpectedBytes as an error, and probing one
	// state after the other would always log one false alarm.
	const auto base = reinterpret_cast<std::uintptr_t>(::GetModuleHandleW(nullptr));
	if (base == 0) {
		context->LogError("Room add check: could not resolve the main module base.");
		return false;
	}
	const auto* site = reinterpret_cast<const std::uint8_t*>(base + kRva);
	if (std::memcmp(site, kPatched, sizeof(kPatched)) == 0) {
		g_byPatchFile = true;
		context->LogInfo("Room add check: already applied by the patch file skip-room-add-duplicate-check.json; that file can be deleted.");
		return true;
	}
	if (std::memcmp(site, kOriginal, sizeof(kOriginal)) != 0) {
		context->LogError("Room add check: unexpected bytes at RVA 0x0038ED8B - wrong game build, or another patch got there first.");
		return false;
	}
	if (context->PatchBytes(kRva, kOriginal, perf_common::ByteCount(kOriginal), kPatched, perf_common::ByteCount(kPatched))) {
		return true;
	}
	context->LogError("Room add check: the patch could not be written.");
	return false;
}

void Status(const D2RL::PluginContext* console) noexcept {
	console->WriteConsoleMessage(g_byPatchFile ? "  applied by the old patch file (delete skip-room-add-duplicate-check.json, this plugin does it now)."
	                                           : "  applied by this plugin.");
}

} // namespace room_add_check

// ===========================================================================
// Translation queue (HD layer, PreprocessMessageQueue)
// ===========================================================================

namespace translation_queue {


// --------------------------------------------------------------------------
// Build-specific constants
// --------------------------------------------------------------------------

constexpr std::uint64_t kPreprocessRva = 0x00775A70ULL;
constexpr std::uint64_t kLoopRva       = 0x00775AFCULL;
constexpr std::uint64_t kJumpTableRva  = 0x00775D64ULL;
constexpr std::uint64_t kSortRva       = 0x006D6070ULL;

// Prologue: whole instructions, no relative operand.
constexpr std::uint8_t kPreprocessBytes[]{
	0x48, 0x89, 0x5C, 0x24, 0x20, 0x55, 0x56, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x4C, 0x89,
	0x74, 0x24, 0x48, 0x4C, 0x8B, 0xF1,
};

// The engine's dedup loop and its sort call, 0x775AFC..0x775CF6.
constexpr std::uint8_t kLoopBytes[]{
	0x4D, 0x8B, 0x16, 0x49, 0x8B, 0x56, 0x08, 0x48, 0x8B, 0xC2, 0x48, 0xC1, 0xE0, 0x07,
	0x49, 0x03, 0xC2, 0x4C, 0x3B, 0xD0, 0x0F, 0x84, 0xC7, 0x01, 0x00, 0x00, 0x49, 0x8B,
	0xC2, 0x4C, 0x8D, 0x05, 0xE0, 0xA4, 0x88, 0xFF, 0x41, 0x8B, 0x0A, 0x83, 0xF9, 0x02,
	0x75, 0x2C, 0x49, 0x3B, 0xC2, 0x0F, 0x84, 0x8B, 0x01, 0x00, 0x00, 0x83, 0x38, 0x02,
	0x75, 0x10, 0x49, 0x8B, 0x4A, 0x08, 0x48, 0x39, 0x48, 0x08, 0x75, 0x06, 0xC7, 0x00,
	0x12, 0x00, 0x00, 0x00, 0x48, 0x83, 0xE8, 0x80, 0x49, 0x3B, 0xC2, 0x75, 0xE2, 0xE9,
	0x68, 0x01, 0x00, 0x00, 0x83, 0xF9, 0x04, 0x75, 0x2C, 0x49, 0x3B, 0xC2, 0x0F, 0x84,
	0x5A, 0x01, 0x00, 0x00, 0x83, 0x38, 0x04, 0x75, 0x10, 0x49, 0x8B, 0x4A, 0x08, 0x48,
	0x39, 0x48, 0x08, 0x75, 0x06, 0xC7, 0x00, 0x12, 0x00, 0x00, 0x00, 0x48, 0x83, 0xE8,
	0x80, 0x49, 0x3B, 0xC2, 0x75, 0xE2, 0xE9, 0x37, 0x01, 0x00, 0x00, 0x83, 0xF9, 0x07,
	0x75, 0x33, 0x49, 0x3B, 0xC2, 0x0F, 0x84, 0x29, 0x01, 0x00, 0x00, 0x83, 0x38, 0x06,
	0x75, 0x17, 0x49, 0x8B, 0x4A, 0x08, 0x48, 0x39, 0x48, 0x08, 0x75, 0x0D, 0xC7, 0x00,
	0x12, 0x00, 0x00, 0x00, 0x41, 0xC7, 0x02, 0x12, 0x00, 0x00, 0x00, 0x48, 0x83, 0xE8,
	0x80, 0x49, 0x3B, 0xC2, 0x75, 0xDB, 0xE9, 0xFF, 0x00, 0x00, 0x00, 0x83, 0xF9, 0x10,
	0x75, 0x31, 0x49, 0x3B, 0xC2, 0x0F, 0x84, 0xF1, 0x00, 0x00, 0x00, 0x0F, 0x1F, 0x44,
	0x00, 0x00, 0x83, 0x38, 0x10, 0x75, 0x10, 0x49, 0x8B, 0x4A, 0x18, 0x48, 0x39, 0x48,
	0x18, 0x75, 0x06, 0xC7, 0x00, 0x12, 0x00, 0x00, 0x00, 0x48, 0x83, 0xE8, 0x80, 0x49,
	0x3B, 0xC2, 0x75, 0xE2, 0xE9, 0xC9, 0x00, 0x00, 0x00, 0x83, 0xF9, 0x01, 0x75, 0x7A,
	0x49, 0x3B, 0xC2, 0x0F, 0x84, 0xBB, 0x00, 0x00, 0x00, 0x48, 0x63, 0x08, 0x83, 0xF9,
	0x11, 0x77, 0x5E, 0x41, 0x8B, 0x94, 0x88, 0x64, 0x5D, 0x77, 0x00, 0x49, 0x03, 0xD0,
	0xFF, 0xE2, 0x49, 0x8B, 0x4A, 0x08, 0x48, 0x39, 0x48, 0x08, 0x75, 0x47, 0xC7, 0x00,
	0x12, 0x00, 0x00, 0x00, 0x41, 0xC7, 0x02, 0x12, 0x00, 0x00, 0x00, 0xEB, 0x38, 0x49,
	0x8B, 0x4A, 0x08, 0x48, 0x39, 0x48, 0x08, 0x74, 0x28, 0x49, 0x8B, 0x4A, 0x10, 0x48,
	0x39, 0x48, 0x20, 0xEB, 0x1C, 0x49, 0x8B, 0x4A, 0x08, 0x48, 0x39, 0x48, 0x08, 0x74,
	0x14, 0x49, 0x8B, 0x4A, 0x10, 0x48, 0x39, 0x48, 0x18, 0xEB, 0x08, 0x49, 0x8B, 0x4A,
	0x08, 0x48, 0x39, 0x48, 0x08, 0x75, 0x06, 0xC7, 0x00, 0x12, 0x00, 0x00, 0x00, 0x48,
	0x83, 0xE8, 0x80, 0x49, 0x3B, 0xC2, 0x75, 0x91, 0xEB, 0x4A, 0x83, 0xF9, 0x0B, 0x75,
	0x45, 0x49, 0x3B, 0xC2, 0x74, 0x40, 0x0F, 0x1F, 0x40, 0x00, 0x8B, 0x08, 0x83, 0xF9,
	0x0A, 0x74, 0x16, 0x83, 0xF9, 0x0C, 0x75, 0x27, 0x41, 0x8B, 0x4A, 0x20, 0x39, 0x48,
	0x20, 0x75, 0x1E, 0xC7, 0x00, 0x12, 0x00, 0x00, 0x00, 0xEB, 0x16, 0x41, 0x8B, 0x4A,
	0x20, 0x39, 0x48, 0x20, 0x75, 0x0D, 0xC7, 0x00, 0x12, 0x00, 0x00, 0x00, 0x41, 0xC7,
	0x02, 0x12, 0x00, 0x00, 0x00, 0x48, 0x83, 0xE8, 0x80, 0x49, 0x3B, 0xC2, 0x75, 0xC4,
	0x49, 0x8B, 0x56, 0x08, 0x49, 0x83, 0xEA, 0x80, 0x49, 0x8B, 0x06, 0x48, 0x8B, 0xCA,
	0x48, 0xC1, 0xE1, 0x07, 0x48, 0x03, 0xC8, 0x4C, 0x3B, 0xD1, 0x0F, 0x85, 0x46, 0xFE,
	0xFF, 0xFF, 0x4C, 0x8B, 0xD0, 0x48, 0xC1, 0xE2, 0x07, 0x45, 0x33, 0xC9, 0x4C, 0x8B,
	0xC2, 0x49, 0x8B, 0xCA, 0x49, 0xC1, 0xF8, 0x07, 0x49, 0x03, 0xD2, 0xE8, 0x7A, 0x03,
	0xF6, 0xFF,
};

// Jump table the loop uses for a type 1 message (earlier type 0..17).
constexpr std::uint8_t kJumpTableBytes[]{
	0x16, 0x5C, 0x77, 0x00, 0x57, 0x5C, 0x77, 0x00, 0x57, 0x5C, 0x77, 0x00, 0x57, 0x5C,
	0x77, 0x00, 0x57, 0x5C, 0x77, 0x00, 0x57, 0x5C, 0x77, 0x00, 0x57, 0x5C, 0x77, 0x00,
	0x57, 0x5C, 0x77, 0x00, 0x43, 0x5C, 0x77, 0x00, 0x2F, 0x5C, 0x77, 0x00, 0x67, 0x5C,
	0x77, 0x00, 0x67, 0x5C, 0x77, 0x00, 0x67, 0x5C, 0x77, 0x00, 0x57, 0x5C, 0x77, 0x00,
	0x57, 0x5C, 0x77, 0x00, 0x57, 0x5C, 0x77, 0x00, 0x67, 0x5C, 0x77, 0x00, 0x57, 0x5C,
	0x77, 0x00,
};

constexpr std::uint8_t kSortBytes[]{
	0x48, 0x8B, 0xC4, 0x4C, 0x89, 0x40, 0x18, 0x48, 0x89, 0x50, 0x10, 0x55, 0x53, 0x56,
	0x57, 0x41, 0x55, 0x41, 0x56, 0x41, 0x57,
};

// --------------------------------------------------------------------------
// Message layout (UnitTranslationMsg, 128 bytes)
// --------------------------------------------------------------------------

constexpr std::size_t   kMessageSize   = 128;
constexpr std::size_t   kTypeOffset    = 0x00; // int32
constexpr std::size_t   kKeyOffset     = 0x08; // u64, the unit/entity the message is about
constexpr std::size_t   kKey10Offset   = 0x10; // u64
constexpr std::size_t   kKey18Offset   = 0x18; // u64
constexpr std::size_t   kKey20Offset   = 0x20; // u64 (type 9) / u32 (types 10, 11, 12)
constexpr std::int32_t  kDropped       = 18;

// Queue = blz::vector<UnitTranslationMsg>: data pointer at +0, count at +8.
constexpr std::size_t kQueueDataOffset  = 0x00;
constexpr std::size_t kQueueCountOffset = 0x08;

using PreprocessFn = void(__fastcall*)(void* queue) noexcept;
using SortFn       = void(__fastcall*)(void* first, void* last, std::int64_t count, std::uint64_t predicate) noexcept;

template <typename T>
auto ReadAt(const std::uint8_t* base, std::size_t offset) noexcept -> T {
	T value;
	std::memcpy(&value, base + offset, sizeof(T));
	return value;
}

auto TypeOf(const std::uint8_t* message) noexcept -> std::int32_t { return ReadAt<std::int32_t>(message, kTypeOffset); }
void Drop(std::uint8_t* message) noexcept { std::memcpy(message + kTypeOffset, &kDropped, sizeof(kDropped)); }

// --------------------------------------------------------------------------
// Indexed dedup
// --------------------------------------------------------------------------

// Lists of earlier live messages, by what a later message looks them up by.
enum Kind : std::uint32_t {
	kAnyByKey = 1, // types 0-9, 13, 14, 15, 17 by +0x08 (looked up by type 1)
	kT2ByKey,      // type 2 by +0x08
	kT4ByKey,      // type 4 by +0x08
	kT6ByKey,      // type 6 by +0x08 (looked up by type 7)
	kT16By18,      // type 16 by +0x18
	kT8By18,       // type 8 by +0x18 (looked up by type 1 with its +0x10)
	kT9By20,       // type 9 by +0x20 u64 (looked up by type 1 with its +0x10)
	kT10By20,      // type 10 by +0x20 u32 (looked up by type 11)
	kT12By20,      // type 12 by +0x20 u32 (looked up by type 11)
};

struct Membership {
	std::uint32_t kind;
	std::uint64_t key;
};

// Which lists a live message of this type belongs to (at most two).
auto Memberships(const std::uint8_t* message, std::int32_t type, Membership out[2]) noexcept -> int {
	const std::uint64_t key = ReadAt<std::uint64_t>(message, kKeyOffset);
	switch (type) {
		case 0: case 1: case 3: case 5: case 7: case 13: case 14: case 15: case 17:
			out[0] = { kAnyByKey, key };
			return 1;
		case 2:
			out[0] = { kAnyByKey, key };
			out[1] = { kT2ByKey, key };
			return 2;
		case 4:
			out[0] = { kAnyByKey, key };
			out[1] = { kT4ByKey, key };
			return 2;
		case 6:
			out[0] = { kAnyByKey, key };
			out[1] = { kT6ByKey, key };
			return 2;
		case 8:
			out[0] = { kAnyByKey, key };
			out[1] = { kT8By18, ReadAt<std::uint64_t>(message, kKey18Offset) };
			return 2;
		case 9:
			out[0] = { kAnyByKey, key };
			out[1] = { kT9By20, ReadAt<std::uint64_t>(message, kKey20Offset) };
			return 2;
		case 10:
			out[0] = { kT10By20, ReadAt<std::uint32_t>(message, kKey20Offset) };
			return 1;
		case 12:
			out[0] = { kT12By20, ReadAt<std::uint32_t>(message, kKey20Offset) };
			return 1;
		case 16:
			out[0] = { kT16By18, ReadAt<std::uint64_t>(message, kKey18Offset) };
			return 1;
		default:
			return 0;
	}
}

// Scratch held by every thread that ran the dedup, and the most messages
// one of them has room for (reported by `perf memory`).
volatile LONG64 g_scratchBytes    = 0;
volatile LONG64 g_scratchMessages = 0;

class Scratch {
public:
	~Scratch() {
		delete[] m_originalType;
		delete[] m_next;
		delete[] m_table;
	}

	auto Prepare(std::size_t count) noexcept -> bool {
		if (count > m_capacity) {
			std::size_t capacity = m_capacity == 0 ? std::size_t{ 1024 } : m_capacity;
			while (capacity < count) {
				capacity *= 2;
			}
			auto* types = new (std::nothrow) std::int32_t[capacity];
			auto* next  = new (std::nothrow) std::int32_t[capacity * 2];
			auto* table = new (std::nothrow) Slot[capacity * 4];
			if (types == nullptr || next == nullptr || table == nullptr) {
				delete[] types;
				delete[] next;
				delete[] table;
				return false;
			}
			delete[] m_originalType;
			delete[] m_next;
			delete[] m_table;
			const LONG64 oldBytes = static_cast<LONG64>(m_capacity * (sizeof(std::int32_t) * 3 + sizeof(Slot) * 4));
			const LONG64 newBytes = static_cast<LONG64>(capacity * (sizeof(std::int32_t) * 3 + sizeof(Slot) * 4));
			InterlockedExchangeAdd64(&g_scratchBytes, newBytes - oldBytes);
			LONG64 most = g_scratchMessages;
			while (static_cast<LONG64>(capacity) > most
			       && InterlockedCompareExchange64(&g_scratchMessages, static_cast<LONG64>(capacity), most) != most) {
				most = g_scratchMessages;
			}
			m_originalType = types;
			m_next         = next;
			m_table        = table;
			m_capacity     = capacity;
			m_tableMask    = capacity * 4 - 1;
			m_generation   = 0;
			for (std::size_t index = 0; index <= m_tableMask; ++index) {
				m_table[index] = Slot{};
			}
		}
		if (++m_generation == 0) { // wrapped: clear the table once
			for (std::size_t index = 0; index <= m_tableMask; ++index) {
				m_table[index] = Slot{};
			}
			m_generation = 1;
		}
		return true;
	}

	// Head of the list for (kind, key); created empty when asked to.
	auto Head(std::uint32_t kind, std::uint64_t key, bool create) noexcept -> std::int32_t* {
		std::uint64_t hash = key ^ (static_cast<std::uint64_t>(kind) * 0x9E3779B97F4A7C15ULL);
		hash ^= hash >> 33;
		hash *= 0xFF51AFD7ED558CCDULL;
		hash ^= hash >> 33;
		for (std::size_t index = static_cast<std::size_t>(hash) & m_tableMask;; index = (index + 1) & m_tableMask) {
			Slot& slot = m_table[index];
			if (slot.generation != m_generation) {
				if (!create) {
					return nullptr;
				}
				slot = Slot{ key, kind, m_generation, -1 };
				return &slot.head;
			}
			if (slot.kind == kind && slot.key == key) {
				return &slot.head;
			}
		}
	}

	std::int32_t* m_originalType = nullptr;
	std::int32_t* m_next         = nullptr; // two list links per message

private:
	struct Slot {
		std::uint64_t key        = 0;
		std::uint32_t kind       = 0;
		std::uint32_t generation = 0;
		std::int32_t  head       = -1;
	};

	Slot*         m_table      = nullptr;
	std::size_t   m_capacity   = 0;
	std::size_t   m_tableMask  = 0;
	std::uint32_t m_generation = 0;
};

thread_local Scratch t_scratch;

// Drops every live message on the list (kind, key) and empties the list.
// Returns how many were dropped; droppedType0 reports whether one of them
// was originally a type 0.
auto DropList(std::uint8_t* begin, std::uint32_t kind, std::uint64_t key, bool* droppedType0) noexcept -> std::uint32_t {
	Scratch&      scratch = t_scratch;
	std::int32_t* head    = scratch.Head(kind, key, false);
	if (head == nullptr) {
		return 0;
	}
	std::uint32_t dropped = 0;
	for (std::int32_t node = *head; node >= 0; node = scratch.m_next[node]) {
		const std::int32_t index    = node >> 1;
		std::uint8_t*      message  = begin + static_cast<std::size_t>(index) * kMessageSize;
		const std::int32_t original = scratch.m_originalType[index];
		if (TypeOf(message) != original) {
			continue; // already dropped through another list
		}
		Drop(message);
		++dropped;
		if (droppedType0 != nullptr && original == 0) {
			*droppedType0 = true;
		}
	}
	*head = -1;
	return dropped;
}

// Same result as the engine's loop at 0x775AFC, message for message.
// Returns false (having changed nothing) when scratch memory is unavailable.
auto DedupQueue(std::uint8_t* begin, std::size_t count) noexcept -> bool {
	if (count == 0) {
		return true;
	}
	if (count > 0x3FFFFFFF || !t_scratch.Prepare(count)) {
		return false;
	}
	Scratch& scratch = t_scratch;

	for (std::size_t current = 0; current < count; ++current) {
		std::uint8_t* const message = begin + current * kMessageSize;
		const std::int32_t  type    = TypeOf(message);
		scratch.m_originalType[current] = type;

		switch (type) {
			case 2:
				DropList(begin, kT2ByKey, ReadAt<std::uint64_t>(message, kKeyOffset), nullptr);
				break;
			case 4:
				DropList(begin, kT4ByKey, ReadAt<std::uint64_t>(message, kKeyOffset), nullptr);
				break;
			case 7:
				if (DropList(begin, kT6ByKey, ReadAt<std::uint64_t>(message, kKeyOffset), nullptr) != 0) {
					Drop(message);
				}
				break;
			case 16:
				DropList(begin, kT16By18, ReadAt<std::uint64_t>(message, kKey18Offset), nullptr);
				break;
			case 1: {
				bool pairedWithCreate = false;
				DropList(begin, kAnyByKey, ReadAt<std::uint64_t>(message, kKeyOffset), &pairedWithCreate);
				const std::uint64_t key10 = ReadAt<std::uint64_t>(message, kKey10Offset);
				DropList(begin, kT8By18, key10, nullptr);
				DropList(begin, kT9By20, key10, nullptr);
				if (pairedWithCreate) {
					Drop(message);
				}
				break;
			}
			case 11: {
				const std::uint32_t id = ReadAt<std::uint32_t>(message, kKey20Offset);
				if (DropList(begin, kT10By20, id, nullptr) != 0) {
					Drop(message);
				}
				DropList(begin, kT12By20, id, nullptr);
				break;
			}
			default:
				break;
		}

		// Still live: later messages may drop it.
		if (TypeOf(message) == type) {
			Membership memberships[2];
			const int  memberCount = Memberships(message, type, memberships);
			for (int slot = 0; slot < memberCount; ++slot) {
				std::int32_t* head = scratch.Head(memberships[slot].kind, memberships[slot].key, true);
				const std::int32_t node = static_cast<std::int32_t>(current * 2 + static_cast<std::size_t>(slot));
				scratch.m_next[node] = *head;
				*head = node;
			}
		}
	}
	return true;
}

// --------------------------------------------------------------------------
// Plugin state
// --------------------------------------------------------------------------

const D2RL::PluginContext* g_context        = nullptr;

std::uintptr_t             g_moduleBase     = 0;
PreprocessFn               g_originalPreprocess = nullptr;
SortFn                     g_sort           = nullptr;

volatile LONG64 g_calls     = 0;
volatile LONG64 g_messages  = 0;
volatile LONG64 g_dropped   = 0;
volatile LONG64 g_largest   = 0;
volatile LONG64 g_fallbacks = 0;

void __fastcall HookPreprocess(void* queue) noexcept {
	std::uint8_t* const begin = *reinterpret_cast<std::uint8_t**>(static_cast<std::uint8_t*>(queue) + kQueueDataOffset);
	const std::size_t   count = *reinterpret_cast<std::size_t*>(static_cast<std::uint8_t*>(queue) + kQueueCountOffset);

	LONG64 droppedBefore = 0;
	for (std::size_t index = 0; index < count; ++index) {
		droppedBefore += TypeOf(begin + index * kMessageSize) == kDropped ? 1 : 0;
	}

	if (!DedupQueue(begin, count)) {
		InterlockedIncrement64(&g_fallbacks);
		g_originalPreprocess(queue);
		return;
	}

	LONG64 droppedAfter = 0;
	for (std::size_t index = 0; index < count; ++index) {
		droppedAfter += TypeOf(begin + index * kMessageSize) == kDropped ? 1 : 0;
	}
	InterlockedIncrement64(&g_calls);
	InterlockedExchangeAdd64(&g_messages, static_cast<LONG64>(count));
	InterlockedExchangeAdd64(&g_dropped, droppedAfter - droppedBefore);
	LONG64 largest = g_largest;
	while (static_cast<LONG64>(count) > largest
	       && InterlockedCompareExchange64(&g_largest, static_cast<LONG64>(count), largest) != largest) {
		largest = g_largest;
	}

	// The engine sorts the queue afterwards; same call, same arguments.
	g_sort(begin, begin + count * kMessageSize, static_cast<std::int64_t>(count), 0);
}

// ---- installation and console (translation queue) ----

using perf_common::ByteCount;
using perf_common::CheckSite;

auto Install(const D2RL::PluginContext* context) noexcept -> bool {
	g_context    = context;
	g_moduleBase = reinterpret_cast<std::uintptr_t>(::GetModuleHandleW(nullptr));
	if (g_moduleBase == 0) {
		context->LogError("Translation queue: could not resolve the main module base.");
		return false;
	}
	bool ok = CheckSite(context, kPreprocessRva, kPreprocessBytes, ByteCount(kPreprocessBytes), "Translation queue: PreprocessMessageQueue prologue");
	ok = CheckSite(context, kLoopRva, kLoopBytes, ByteCount(kLoopBytes), "Translation queue: PreprocessMessageQueue loop") && ok;
	ok = CheckSite(context, kJumpTableRva, kJumpTableBytes, ByteCount(kJumpTableBytes), "Translation queue: jump table") && ok;
	ok = CheckSite(context, kSortRva, kSortBytes, ByteCount(kSortBytes), "Translation queue: unit message sort") && ok;
	if (!ok) {
		return false;
	}
	g_sort = reinterpret_cast<SortFn>(g_moduleBase + kSortRva);
	if (!context->InstallInlineHook(kPreprocessRva, kPreprocessBytes, ByteCount(kPreprocessBytes), &HookPreprocess,
	                                &g_originalPreprocess)) {
		context->LogError("Translation queue: failed to install the PreprocessMessageQueue hook.");
		return false;
	}
	return true;
}

void ResetCounters() noexcept {
	InterlockedExchange64(&g_calls, 0);
	InterlockedExchange64(&g_messages, 0);
	InterlockedExchange64(&g_dropped, 0);
	InterlockedExchange64(&g_largest, 0);
	InterlockedExchange64(&g_fallbacks, 0);
}

void Status(const D2RL::PluginContext* console) noexcept {
	const long long calls = static_cast<long long>(g_calls);
	char line[256]{};
	std::snprintf(line, sizeof(line), "  %lld calls, %lld messages (%.1f per call, largest %lld), %lld dropped, %lld game fallbacks.",
	              calls, static_cast<long long>(g_messages),
	              calls != 0 ? static_cast<double>(g_messages) / static_cast<double>(calls) : 0.0,
	              static_cast<long long>(g_largest), static_cast<long long>(g_dropped), static_cast<long long>(g_fallbacks));
	console->WriteConsoleMessage(line);
}

} // namespace translation_queue

// ===========================================================================
// Weak instances (HD layer, EntityContainerDefinition::RemoveWeakInstance)
// ===========================================================================

namespace weak_instances {


// --------------------------------------------------------------------------
// Build-specific constants
// --------------------------------------------------------------------------

constexpr std::uint64_t kRemoveWeakInstanceRva = 0x00955E90ULL;
constexpr std::uint64_t kRemoveLoopRva         = 0x00955F24ULL;
constexpr std::uint64_t kAddInstanceRva        = 0x0094A7A0ULL;

// Prologues: whole instructions, no relative operand.
constexpr std::uint8_t kRemoveWeakInstanceBytes[]{
	0x40, 0x53, 0x56, 0x57, 0x41, 0x54, 0x48, 0x83, 0xEC, 0x38, 0x48, 0x89, 0x6C, 0x24,
	0x68, 0x48, 0x8B, 0xE9, 0x4C, 0x89, 0x74, 0x24, 0x28, 0x4C, 0x8B, 0xF2,
};
constexpr std::uint8_t kAddInstanceBytes[]{
	0x4C, 0x8B, 0xDC, 0x53, 0x57, 0x48, 0x81, 0xEC, 0xD8, 0x0A, 0x00, 0x00,
};

// The engine's removal loop, 0x955F24..0x956078.
constexpr std::uint8_t kRemoveLoopBytes[]{
	0x48, 0x8B, 0xBD, 0x30, 0x01, 0x00, 0x00, 0x48, 0x8B, 0x85, 0x38, 0x01, 0x00, 0x00,
	0x48, 0xC1, 0xE0, 0x04, 0x48, 0x03, 0xC7, 0x48, 0x3B, 0xF8, 0x0F, 0x84, 0x3B, 0x01,
	0x00, 0x00, 0x4C, 0x89, 0x6C, 0x24, 0x30, 0x45, 0x33, 0xED, 0x66, 0x0F, 0x1F, 0x44,
	0x00, 0x00, 0x48, 0x8B, 0x5F, 0x08, 0x48, 0x85, 0xDB, 0x74, 0x31, 0x0F, 0x1F, 0x80,
	0x00, 0x00, 0x00, 0x00, 0x8B, 0x43, 0x08, 0x85, 0xC0, 0x74, 0x20, 0x8D, 0x48, 0x01,
	0xF0, 0x0F, 0xB1, 0x4B, 0x08, 0x75, 0xEF, 0x48, 0x8B, 0x07, 0x48, 0x85, 0xC0, 0x74,
	0x11, 0x49, 0x3B, 0xC6, 0x74, 0x0C, 0x48, 0x83, 0xC7, 0x10, 0xE9, 0xA6, 0x00, 0x00,
	0x00, 0x49, 0x8B, 0xDD, 0x48, 0x8B, 0xC7, 0x48, 0x2B, 0x85, 0x30, 0x01, 0x00, 0x00,
	0x48, 0xC1, 0xF8, 0x04, 0x48, 0x3B, 0x85, 0x38, 0x01, 0x00, 0x00, 0x72, 0x14, 0x48,
	0x8D, 0x4C, 0x24, 0x60, 0x44, 0x88, 0x6C, 0x24, 0x60, 0xE8, 0x50, 0xF1, 0xFE, 0xFF,
	0x84, 0xC0, 0x74, 0x01, 0xCC, 0x48, 0x8B, 0x8D, 0x38, 0x01, 0x00, 0x00, 0x48, 0x8B,
	0xB5, 0x30, 0x01, 0x00, 0x00, 0x48, 0xC1, 0xE1, 0x04, 0x48, 0x83, 0xC6, 0xF0, 0x48,
	0x03, 0xF1, 0x48, 0x3B, 0xFE, 0x74, 0x30, 0x48, 0x8B, 0x56, 0x08, 0x48, 0x8B, 0x06,
	0x4C, 0x89, 0x6E, 0x08, 0x48, 0x8B, 0x4F, 0x08, 0x48, 0x89, 0x07, 0x48, 0x89, 0x57,
	0x08, 0x48, 0x85, 0xC9, 0x74, 0x15, 0xB8, 0xFF, 0xFF, 0xFF, 0xFF, 0xF0, 0x0F, 0xC1,
	0x41, 0x0C, 0x83, 0xF8, 0x01, 0x75, 0x06, 0x48, 0x8B, 0x01, 0xFF, 0x50, 0x18, 0x48,
	0xFF, 0x8D, 0x38, 0x01, 0x00, 0x00, 0x48, 0x8B, 0x4E, 0x08, 0x48, 0x85, 0xC9, 0x74,
	0x15, 0xB8, 0xFF, 0xFF, 0xFF, 0xFF, 0xF0, 0x0F, 0xC1, 0x41, 0x0C, 0x83, 0xF8, 0x01,
	0x75, 0x06, 0x48, 0x8B, 0x01, 0xFF, 0x50, 0x18, 0x48, 0x85, 0xDB, 0x74, 0x30, 0xB8,
	0xFF, 0xFF, 0xFF, 0xFF, 0xF0, 0x0F, 0xC1, 0x43, 0x08, 0x83, 0xF8, 0x01, 0x75, 0x21,
	0x48, 0x8B, 0x03, 0x48, 0x8B, 0xCB, 0xFF, 0x50, 0x10, 0xB8, 0xFF, 0xFF, 0xFF, 0xFF,
	0xF0, 0x0F, 0xC1, 0x43, 0x0C, 0x83, 0xF8, 0x01, 0x75, 0x09, 0x48, 0x8B, 0x03, 0x48,
	0x8B, 0xCB, 0xFF, 0x50, 0x18, 0x48, 0x8B, 0x85, 0x38, 0x01, 0x00, 0x00, 0x48, 0xC1,
	0xE0, 0x04, 0x48, 0x03, 0x85, 0x30, 0x01, 0x00, 0x00, 0x48, 0x3B, 0xF8, 0x0F, 0x85,
	0xD8, 0xFE, 0xFF, 0xFF,
};

// --------------------------------------------------------------------------
// Layout
// --------------------------------------------------------------------------

constexpr std::size_t kListDataOffset   = 0x130;
constexpr std::size_t kListCountOffset  = 0x138;
constexpr std::size_t kStrongOffset     = 0x08;
constexpr std::size_t kWeakOffset       = 0x0C;
constexpr std::size_t kDeleteThisOffset = 0x18;

struct WeakEntry {
	void*         object;
	std::uint8_t* control;
};
static_assert(sizeof(WeakEntry) == 16, "list entries are 16 bytes");

using RemoveWeakInstanceFn = void(__fastcall*)(void* definition, void* instance) noexcept;
using AddInstanceFn        = std::int64_t(__fastcall*)(void* definition, void* sharedInstance, void* argument3,
                                                       void* argument4, std::int32_t argument5) noexcept;
using DeleteThisFn         = void(__fastcall*)(void* control) noexcept;

template <typename T>
auto ReadAt(const void* base, std::size_t offset) noexcept -> T {
	T value;
	std::memcpy(&value, static_cast<const std::uint8_t*>(base) + offset, sizeof(T));
	return value;
}

template <typename T>
void WriteAt(void* base, std::size_t offset, T value) noexcept {
	std::memcpy(static_cast<std::uint8_t*>(base) + offset, &value, sizeof(T));
}

// Weak release exactly as the engine does it: lock xadd -1 on the weak count,
// and delete the control block through its vtable when that was the last.
void ReleaseWeak(std::uint8_t* control) noexcept {
	if (control == nullptr) {
		return;
	}
	if (InterlockedExchangeAdd(reinterpret_cast<volatile LONG*>(control + kWeakOffset), -1) == 1) {
		const std::uintptr_t vtable = ReadAt<std::uintptr_t>(control, 0);
		const auto deleteThis = reinterpret_cast<DeleteThisFn>(ReadAt<std::uintptr_t>(reinterpret_cast<void*>(vtable), kDeleteThisOffset));
		deleteThis(control);
	}
}

auto StrongCount(const std::uint8_t* control) noexcept -> LONG {
	return InterlockedCompareExchange(reinterpret_cast<volatile LONG*>(const_cast<std::uint8_t*>(control) + kStrongOffset), 0, 0);
}

// --------------------------------------------------------------------------
// Instance -> list position
// --------------------------------------------------------------------------

// Open addressing, linear probing, backward-shift deletion.
class PositionMap {
public:
	auto Find(const void* key) const noexcept -> const std::size_t* {
		if (m_capacity == 0 || key == nullptr) {
			return nullptr;
		}
		const std::size_t mask = m_capacity - 1;
		for (std::size_t index = Hash(key) & mask;; index = (index + 1) & mask) {
			if (m_slots[index].key == key) {
				return &m_slots[index].value;
			}
			if (m_slots[index].key == nullptr) {
				return nullptr;
			}
		}
	}

	auto Assign(const void* key, std::size_t value) noexcept -> bool {
		if (key == nullptr) {
			return true;
		}
		if ((m_size + 1) * 2 > m_capacity && !Grow()) {
			return false;
		}
		const std::size_t mask = m_capacity - 1;
		for (std::size_t index = Hash(key) & mask;; index = (index + 1) & mask) {
			if (m_slots[index].key == key) {
				m_slots[index].value = value;
				return true;
			}
			if (m_slots[index].key == nullptr) {
				m_slots[index] = Slot{ key, value };
				++m_size;
				return true;
			}
		}
	}

	void Erase(const void* key) noexcept {
		if (m_capacity == 0 || key == nullptr) {
			return;
		}
		const std::size_t mask = m_capacity - 1;
		std::size_t hole = Hash(key) & mask;
		for (;; hole = (hole + 1) & mask) {
			if (m_slots[hole].key == nullptr) {
				return;
			}
			if (m_slots[hole].key == key) {
				break;
			}
		}
		for (std::size_t probe = (hole + 1) & mask; m_slots[probe].key != nullptr; probe = (probe + 1) & mask) {
			const std::size_t home  = Hash(m_slots[probe].key) & mask;
			const bool        stays = hole <= probe ? (hole < home && home <= probe) : (hole < home || home <= probe);
			if (!stays) {
				m_slots[hole] = m_slots[probe];
				hole          = probe;
			}
		}
		m_slots[hole] = Slot{};
		--m_size;
	}

	auto Size() const noexcept -> std::size_t { return m_size; }
	auto HeldBytes() const noexcept -> std::size_t { return m_capacity * sizeof(Slot); }

private:
	struct Slot {
		const void* key   = nullptr;
		std::size_t value = 0;
	};

	static auto Hash(const void* key) noexcept -> std::size_t {
		std::uint64_t value = static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(key));
		value ^= value >> 33;
		value *= 0xFF51AFD7ED558CCDULL;
		value ^= value >> 33;
		return static_cast<std::size_t>(value);
	}

	auto Grow() noexcept -> bool {
		const std::size_t capacity = m_capacity == 0 ? std::size_t{ 4096 } : m_capacity * 2;
		Slot* fresh = new (std::nothrow) Slot[capacity];
		if (fresh == nullptr) {
			return false;
		}
		Slot* const       old         = m_slots;
		const std::size_t oldCapacity = m_capacity;
		m_slots    = fresh;
		m_capacity = capacity;
		m_size     = 0;
		for (std::size_t index = 0; index < oldCapacity; ++index) {
			if (old[index].key != nullptr) {
				Assign(old[index].key, old[index].value);
			}
		}
		delete[] old;
		return true;
	}

	Slot*       m_slots    = nullptr;
	std::size_t m_capacity = 0;
	std::size_t m_size     = 0;
};

// --------------------------------------------------------------------------
// State
// --------------------------------------------------------------------------

const D2RL::PluginContext* g_context = nullptr;


RemoveWeakInstanceFn g_originalRemove = nullptr;
AddInstanceFn        g_originalAdd    = nullptr;

SRWLOCK       g_lock = SRWLOCK_INIT;
PositionMap   g_positions;
volatile LONG g_live = 0;
thread_local int t_engineDepth = 0; // > 0 while the engine's own removal runs

volatile LONG64 g_direct    = 0;
volatile LONG64 g_fallbacks = 0;
volatile LONG64 g_largest   = 0;

void NoteListSize(std::size_t count) noexcept {
	LONG64 largest = g_largest;
	while (static_cast<LONG64>(count) > largest
	       && InterlockedCompareExchange64(&g_largest, static_cast<LONG64>(count), largest) != largest) {
		largest = g_largest;
	}
}

// Records the position of every entry of one list. Lock held by the caller.
void IndexList(std::uint8_t* definition, std::size_t from) noexcept {
	WeakEntry* const  data  = ReadAt<WeakEntry*>(definition, kListDataOffset);
	const std::size_t count = ReadAt<std::size_t>(definition, kListCountOffset);
	for (std::size_t index = from; index < count; ++index) {
		if (!g_positions.Assign(data[index].object, index)) {
			InterlockedExchange(&g_live, 0); // out of memory: the engine's code from here on
			return;
		}
	}
}

// The engine's removal of the entry at `position` when it is the only
// expired entry: no lock was taken on it, the last entry moves into its slot,
// the count drops by one, and the two weak releases run in the engine's order.
auto TryDirectRemove(std::uint8_t* definition, void* instance) noexcept -> bool {
	WeakEntry* const  data  = ReadAt<WeakEntry*>(definition, kListDataOffset);
	const std::size_t count = ReadAt<std::size_t>(definition, kListCountOffset);

	AcquireSRWLockExclusive(&g_lock);
	const std::size_t* known = g_positions.Find(instance);
	if (known == nullptr || *known >= count || data[*known].object != instance || data[*known].control == nullptr
	    || StrongCount(data[*known].control) != 0) {
		ReleaseSRWLockExclusive(&g_lock);
		return false;
	}
	const std::size_t position  = *known;
	const std::size_t lastIndex = count - 1;
	g_positions.Erase(instance);
	if (position != lastIndex && !g_positions.Assign(data[lastIndex].object, position)) {
		InterlockedExchange(&g_live, 0);
	}
	ReleaseSRWLockExclusive(&g_lock);

	WeakEntry* const entry = data + position;
	WeakEntry* const last  = data + lastIndex;
	if (entry != last) {
		std::uint8_t* const movedControl   = last->control;
		void* const         movedObject    = last->object;
		last->control                      = nullptr;
		std::uint8_t* const removedControl = entry->control;
		entry->object                      = movedObject;
		entry->control                     = movedControl;
		ReleaseWeak(removedControl);
	}
	WriteAt<std::size_t>(definition, kListCountOffset, ReadAt<std::size_t>(definition, kListCountOffset) - 1);
	ReleaseWeak(last->control);
	return true;
}

void __fastcall HookRemoveWeakInstance(void* definition, void* instance) noexcept {
	if (g_live != 0 && t_engineDepth == 0 && definition != nullptr && instance != nullptr) {
		NoteListSize(ReadAt<std::size_t>(definition, kListCountOffset));
		if (TryDirectRemove(static_cast<std::uint8_t*>(definition), instance)) {
			InterlockedIncrement64(&g_direct);
			return;
		}
	}

	++t_engineDepth;
	g_originalRemove(definition, instance);
	--t_engineDepth;

	if (g_live != 0 && t_engineDepth == 0 && definition != nullptr) {
		InterlockedIncrement64(&g_fallbacks);
		AcquireSRWLockExclusive(&g_lock);
		g_positions.Erase(instance);
		IndexList(static_cast<std::uint8_t*>(definition), 0);
		ReleaseSRWLockExclusive(&g_lock);
	}
}

auto __fastcall HookAddInstance(void* definition, void* sharedInstance, void* argument3, void* argument4,
                                std::int32_t argument5) noexcept -> std::int64_t {
	const std::size_t before = definition != nullptr ? ReadAt<std::size_t>(definition, kListCountOffset) : 0;
	const std::int64_t result = g_originalAdd(definition, sharedInstance, argument3, argument4, argument5);
	if (g_live != 0 && definition != nullptr) {
		AcquireSRWLockExclusive(&g_lock);
		IndexList(static_cast<std::uint8_t*>(definition), before);
		ReleaseSRWLockExclusive(&g_lock);
	}
	return result;
}

// ---- installation and console (weak instances) ----

using perf_common::ByteCount;
using perf_common::CheckSite;

auto Install(const D2RL::PluginContext* context) noexcept -> bool {
	g_context = context;
	bool ok = CheckSite(context, kRemoveWeakInstanceRva, kRemoveWeakInstanceBytes, ByteCount(kRemoveWeakInstanceBytes),
	                    "Weak instances: RemoveWeakInstance prologue");
	ok = CheckSite(context, kRemoveLoopRva, kRemoveLoopBytes, ByteCount(kRemoveLoopBytes), "Weak instances: RemoveWeakInstance loop") && ok;
	ok = CheckSite(context, kAddInstanceRva, kAddInstanceBytes, ByteCount(kAddInstanceBytes), "Weak instances: instance registration") && ok;
	if (!ok) {
		return false;
	}
	// Registration first, so every instance created from now on has a known
	// position before the removal hook can be asked about it.
	if (!context->InstallInlineHook(kAddInstanceRva, kAddInstanceBytes, ByteCount(kAddInstanceBytes), &HookAddInstance,
	                                &g_originalAdd)) {
		context->LogError("Weak instances: failed to install the instance registration hook.");
		return false;
	}
	InterlockedExchange(&g_live, 1);
	if (!context->InstallInlineHook(kRemoveWeakInstanceRva, kRemoveWeakInstanceBytes, ByteCount(kRemoveWeakInstanceBytes),
	                                &HookRemoveWeakInstance, &g_originalRemove)) {
		// The registration hook stays; on its own it only records positions,
		// which changes nothing.
		context->LogError("Weak instances: failed to install the RemoveWeakInstance hook; the game's own removal stays in use.");
		return false;
	}
	return true;
}

auto MemoryHeld(std::size_t* tracked) noexcept -> std::uint64_t {
	AcquireSRWLockExclusive(&g_lock);
	const std::uint64_t bytes = g_positions.HeldBytes();
	*tracked = g_positions.Size();
	ReleaseSRWLockExclusive(&g_lock);
	return bytes;
}

void ResetCounters() noexcept {
	InterlockedExchange64(&g_direct, 0);
	InterlockedExchange64(&g_fallbacks, 0);
	InterlockedExchange64(&g_largest, 0);
}

void Status(const D2RL::PluginContext* console) noexcept {
	std::size_t known = 0;
	AcquireSRWLockExclusive(&g_lock);
	known = g_positions.Size();
	ReleaseSRWLockExclusive(&g_lock);
	char line[256]{};
	std::snprintf(line, sizeof(line), "  %lld removals done directly, %lld by the game's own code, largest list %lld, %zu visuals tracked.",
	              static_cast<long long>(g_direct), static_cast<long long>(g_fallbacks), static_cast<long long>(g_largest), known);
	console->WriteConsoleMessage(line);
}

} // namespace weak_instances

// ===========================================================================
// Client unit loop (client per-type unit loop)
// ===========================================================================

namespace client_unit_loop {


constexpr std::uint64_t kLoopRva         = 0x0009F470ULL;
constexpr std::uint64_t kUnitIdRva       = 0x0034A330ULL;
constexpr std::uint64_t kUnitTypeRva     = 0x0034B9D0ULL;
constexpr std::uint64_t kUpdateUnitRva   = 0x000FA930ULL;
constexpr std::uint64_t kFindUnitRva     = 0x0009F270ULL;
constexpr std::uint64_t kPostMonsterRva  = 0x001031D0ULL;
constexpr std::uint64_t kPostObjectRva   = 0x001CB170ULL;
constexpr std::uint64_t kTableBaseRva    = 0x02A25110ULL;
constexpr std::uint64_t kSourceFileRva   = 0x01CBD400ULL;
constexpr std::int32_t  kSourceLine      = 0x527;

constexpr std::size_t   kBucketCount     = 128;
constexpr std::size_t   kTableStride     = 0x400; // 128 bucket heads per unit type
constexpr std::size_t   kNextInBucket    = 0x158;
constexpr std::uint32_t kUnitTypeMonster = 1;
constexpr std::uint32_t kUnitTypeObject  = 2;

// Prologue: four register saves, no relative operand.
constexpr std::uint8_t kLoopPrologue[]{
	0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x6C, 0x24, 0x10, 0x48, 0x89, 0x74, 0x24,
	0x18, 0x48, 0x89, 0x7C, 0x24, 0x20,
};

// The whole loop, 0x9F470..0x9F536.
constexpr std::uint8_t kLoopBody[]{
	0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x6C, 0x24, 0x10, 0x48, 0x89, 0x74, 0x24,
	0x18, 0x48, 0x89, 0x7C, 0x24, 0x20, 0x41, 0x54, 0x41, 0x56, 0x41, 0x57, 0x48, 0x83,
	0xEC, 0x20, 0x4C, 0x8B, 0xF9, 0x4C, 0x8D, 0x25, 0x78, 0x5C, 0x98, 0x02, 0x45, 0x33,
	0xF6, 0x0F, 0x1F, 0x44, 0x00, 0x00, 0x4B, 0x8B, 0x34, 0xF7, 0x48, 0x85, 0xF6, 0x74,
	0x7D, 0x0F, 0x1F, 0x80, 0x00, 0x00, 0x00, 0x00, 0x48, 0x8B, 0xAE, 0x58, 0x01, 0x00,
	0x00, 0x48, 0x8D, 0x15, 0x42, 0xDF, 0xC1, 0x01, 0x41, 0xB8, 0x27, 0x05, 0x00, 0x00,
	0x48, 0x8B, 0xCE, 0xE8, 0x64, 0xAE, 0x2A, 0x00, 0x48, 0x8B, 0xCE, 0x8B, 0xF8, 0xE8,
	0xFA, 0xC4, 0x2A, 0x00, 0x48, 0x8B, 0xCE, 0x48, 0x63, 0xD8, 0xE8, 0x4F, 0xB4, 0x05,
	0x00, 0x48, 0x8B, 0xCB, 0x8B, 0xD7, 0x48, 0xC1, 0xE1, 0x0A, 0x83, 0xE2, 0x7F, 0x49,
	0x03, 0xCC, 0x44, 0x8B, 0xCB, 0x44, 0x8B, 0xC7, 0xE8, 0x75, 0xFD, 0xFF, 0xFF, 0x48,
	0x85, 0xC0, 0x74, 0x1E, 0x8B, 0x08, 0x83, 0xF9, 0x01, 0x75, 0x0A, 0x48, 0x8B, 0xC8,
	0xE8, 0xC1, 0x3C, 0x06, 0x00, 0xEB, 0x0D, 0x83, 0xF9, 0x02, 0x75, 0x08, 0x48, 0x8B,
	0xC8, 0xE8, 0x52, 0xBC, 0x12, 0x00, 0x48, 0x8B, 0xF5, 0x48, 0x85, 0xED, 0x75, 0x8A,
	0x49, 0xFF, 0xC6, 0x49, 0x81, 0xFE, 0x80, 0x00, 0x00, 0x00, 0x0F, 0x8C, 0x6A, 0xFF,
	0xFF, 0xFF,
};

using UnitIdFn     = std::uint32_t(__fastcall*)(void* unit, const char* file, std::int32_t line) noexcept;
using UnitTypeFn   = std::int32_t(__fastcall*)(void* unit) noexcept;
using UpdateUnitFn = void(__fastcall*)(void* unit) noexcept;
using FindUnitFn   = void*(__fastcall*)(void* buckets, std::uint32_t bucket, std::uint32_t id, std::uint32_t type) noexcept;
using PostUnitFn   = void(__fastcall*)(void* unit) noexcept;
using LoopFn       = void(__fastcall*)(void* table) noexcept;

const D2RL::PluginContext* g_context = nullptr;

LoopFn                     g_originalLoop = nullptr;

UnitIdFn       g_unitId      = nullptr;
UnitTypeFn     g_unitType    = nullptr;
UpdateUnitFn   g_updateUnit  = nullptr;
FindUnitFn     g_findUnit    = nullptr;
PostUnitFn     g_postMonster = nullptr;
PostUnitFn     g_postObject  = nullptr;
std::uint8_t*  g_tableBase   = nullptr;
const char*    g_sourceFile  = nullptr;

volatile LONG64 g_lookups = 0;
volatile LONG64 g_skipped = 0;

// Same loop as 0x9F470: bucket by bucket, next pointer read before the
// update, id and type read before the update, lookup after it.
void __fastcall HookLoop(void* table) noexcept {
	void** const buckets = static_cast<void**>(table);
	LONG64       lookups = 0;
	LONG64       skipped = 0;
	for (std::size_t bucket = 0; bucket < kBucketCount; ++bucket) {
		void* unit = buckets[bucket];
		while (unit != nullptr) {
			void* next;
			std::memcpy(&next, static_cast<std::uint8_t*>(unit) + kNextInBucket, sizeof(next));
			const std::uint32_t id   = g_unitId(unit, g_sourceFile, kSourceLine);
			const std::int64_t  type = g_unitType(unit);
			g_updateUnit(unit);
			// The engine only uses the lookup's result for monsters and objects.
			if (type == kUnitTypeMonster || type == kUnitTypeObject) {
				++lookups;
				void* const found = g_findUnit(g_tableBase + static_cast<std::size_t>(type) * kTableStride,
				                               id & 0x7F, id, static_cast<std::uint32_t>(type));
				if (found != nullptr) {
					std::uint32_t foundType;
					std::memcpy(&foundType, found, sizeof(foundType));
					if (foundType == kUnitTypeMonster) {
						g_postMonster(found);
					} else if (foundType == kUnitTypeObject) {
						g_postObject(found);
					}
				}
			} else {
				++skipped;
			}
			unit = next;
		}
	}
	InterlockedExchangeAdd64(&g_lookups, lookups);
	InterlockedExchangeAdd64(&g_skipped, skipped);
}

// ---- installation and console (client unit loop) ----

using perf_common::ByteCount;

auto Install(const D2RL::PluginContext* context) noexcept -> bool {
	g_context = context;
	if (!context->CheckExpectedBytes(kLoopRva, kLoopBody, ByteCount(kLoopBody))) {
		context->LogError("Client unit loop: code at RVA 0x0009F470 does not match the expected bytes - most likely the old d2rl-celestialrayone-client-unit-loop.dll is still in the plugins folder (delete it), or a different game build.");
		return false;
	}
	const auto base = reinterpret_cast<std::uintptr_t>(::GetModuleHandleW(nullptr));
	if (base == 0) {
		context->LogError("Client unit loop: could not resolve the main module base.");
		return false;
	}
	g_unitId      = reinterpret_cast<UnitIdFn>(base + kUnitIdRva);
	g_unitType    = reinterpret_cast<UnitTypeFn>(base + kUnitTypeRva);
	g_updateUnit  = reinterpret_cast<UpdateUnitFn>(base + kUpdateUnitRva);
	g_findUnit    = reinterpret_cast<FindUnitFn>(base + kFindUnitRva);
	g_postMonster = reinterpret_cast<PostUnitFn>(base + kPostMonsterRva);
	g_postObject  = reinterpret_cast<PostUnitFn>(base + kPostObjectRva);
	g_tableBase   = reinterpret_cast<std::uint8_t*>(base + kTableBaseRva);
	g_sourceFile  = reinterpret_cast<const char*>(base + kSourceFileRva);
	if (!context->InstallInlineHook(kLoopRva, kLoopPrologue, ByteCount(kLoopPrologue), &HookLoop, &g_originalLoop)) {
		context->LogError("Client unit loop: failed to install the hook.");
		return false;
	}
	return true;
}

void ResetCounters() noexcept {
	InterlockedExchange64(&g_lookups, 0);
	InterlockedExchange64(&g_skipped, 0);
}

void Status(const D2RL::PluginContext* console) noexcept {
	char line[256]{};
	std::snprintf(line, sizeof(line), "  %lld lookups done (monsters, objects), %lld skipped (result unused).",
	              static_cast<long long>(g_lookups), static_cast<long long>(g_skipped));
	console->WriteConsoleMessage(line);
}

} // namespace client_unit_loop

// ===========================================================================
// Unit entity lookup (HD layer, entity destroy by unit key)
// ===========================================================================

namespace unit_entity_lookup {


constexpr std::uint64_t kDestroyByKeyRva = 0x009725E0ULL;
constexpr std::uint64_t kStorageRva      = 0x006E4A50ULL; // registry.assure<unit key>()
constexpr std::uint64_t kSlotOfRva       = 0x001D5840ULL; // sparse-set slot of an entity

// Prologue: whole instructions, no relative operand.
constexpr std::uint8_t kPrologue[]{
	0x48, 0x89, 0x5C, 0x24, 0x10, 0x55, 0x56, 0x57, 0x41, 0x54, 0x41, 0x56, 0x48, 0x83,
	0xEC, 0x40,
};

// The whole function, 0x9725E0..0x972850.
constexpr std::uint8_t kBody[]{
	0x48, 0x89, 0x5C, 0x24, 0x10, 0x55, 0x56, 0x57, 0x41, 0x54, 0x41, 0x56, 0x48, 0x83,
	0xEC, 0x40, 0x4C, 0x8B, 0xE2, 0x4C, 0x8B, 0xF1, 0xBE, 0xFF, 0xFF, 0x0F, 0x00, 0xE8,
	0x50, 0x24, 0xD7, 0xFF, 0x48, 0x8B, 0xD8, 0x48, 0x85, 0xC0, 0x74, 0x23, 0x48, 0x8B,
	0x48, 0x28, 0x48, 0x83, 0xC0, 0x20, 0x48, 0x89, 0x44, 0x24, 0x20, 0x48, 0x2B, 0x08,
	0x48, 0x8D, 0x44, 0x24, 0x20, 0x48, 0xC1, 0xF9, 0x02, 0x48, 0x89, 0x4C, 0x24, 0x28,
	0x48, 0x8B, 0xCB, 0xEB, 0x0F, 0x0F, 0x57, 0xC0, 0x48, 0x8D, 0x44, 0x24, 0x30, 0x0F,
	0x11, 0x44, 0x24, 0x30, 0x33, 0xC9, 0x0F, 0x10, 0x00, 0x0F, 0x11, 0x44, 0x24, 0x30,
	0x48, 0x85, 0xC9, 0x74, 0x14, 0x48, 0x8D, 0x41, 0x20, 0x48, 0xC7, 0x44, 0x24, 0x28,
	0x00, 0x00, 0x00, 0x00, 0x48, 0x89, 0x44, 0x24, 0x20, 0xEB, 0x08, 0x0F, 0x57, 0xC0,
	0x0F, 0x11, 0x44, 0x24, 0x20, 0x48, 0x8B, 0x7C, 0x24, 0x38, 0x48, 0x8D, 0x44, 0x24,
	0x20, 0x0F, 0x10, 0x00, 0x4C, 0x89, 0x7C, 0x24, 0x70, 0x66, 0x0F, 0x73, 0xD8, 0x08,
	0x66, 0x48, 0x0F, 0x7E, 0xC5, 0x48, 0x3B, 0xEF, 0x0F, 0x84, 0x97, 0x00, 0x00, 0x00,
	0x4C, 0x8B, 0x7C, 0x24, 0x30, 0x0F, 0x1F, 0x00, 0x49, 0x8B, 0x07, 0x8B, 0x74, 0xB8,
	0xFC, 0x48, 0x85, 0xC9, 0x74, 0x41, 0x4C, 0x8B, 0x41, 0x08, 0x8B, 0xD6, 0x48, 0x8B,
	0x41, 0x10, 0x44, 0x8B, 0xCE, 0x48, 0xC1, 0xEA, 0x08, 0x49, 0x2B, 0xC0, 0x81, 0xE2,
	0xFF, 0x0F, 0x00, 0x00, 0x48, 0xC1, 0xF8, 0x03, 0x48, 0x3B, 0xD0, 0x73, 0x1E, 0x49,
	0x8B, 0x0C, 0xD0, 0x48, 0x85, 0xC9, 0x74, 0x15, 0x41, 0x0F, 0xB6, 0xC1, 0x8B, 0x0C,
	0x81, 0x81, 0xE1, 0xFF, 0xFF, 0x0F, 0x00, 0x81, 0xF9, 0xFF, 0xFF, 0x0F, 0x00, 0x75,
	0x1A, 0x48, 0x8D, 0x8C, 0x24, 0x80, 0x00, 0x00, 0x00, 0xC6, 0x84, 0x24, 0x80, 0x00,
	0x00, 0x00, 0x00, 0xE8, 0xFE, 0xEC, 0xFF, 0xFF, 0x84, 0xC0, 0x74, 0x01, 0xCC, 0x8B,
	0xD6, 0x48, 0x8B, 0xCB, 0xE8, 0x3F, 0x31, 0x86, 0xFF, 0x48, 0x8B, 0x4B, 0x38, 0x4C,
	0x39, 0x24, 0xC1, 0x74, 0x14, 0x48, 0xFF, 0xCF, 0x48, 0x8B, 0xCB, 0x48, 0x3B, 0xEF,
	0x0F, 0x85, 0x76, 0xFF, 0xFF, 0xFF, 0xE9, 0x23, 0x01, 0x00, 0x00, 0x8B, 0xEE, 0x81,
	0xE5, 0xFF, 0xFF, 0x0F, 0x00, 0x81, 0xFD, 0xFF, 0xFF, 0x0F, 0x00, 0x0F, 0x84, 0x0F,
	0x01, 0x00, 0x00, 0x49, 0x8B, 0x56, 0x30, 0x49, 0x8B, 0x46, 0x38, 0x48, 0x2B, 0xC2,
	0x8B, 0xCE, 0x48, 0xC1, 0xF8, 0x02, 0x81, 0xE1, 0xFF, 0xFF, 0x0F, 0x00, 0x44, 0x8B,
	0xFE, 0x48, 0x3B, 0xC8, 0x73, 0x05, 0x39, 0x34, 0x8A, 0x74, 0x1A, 0x48, 0x8D, 0x8C,
	0x24, 0x80, 0x00, 0x00, 0x00, 0xC6, 0x84, 0x24, 0x80, 0x00, 0x00, 0x00, 0x00, 0xE8,
	0xE4, 0x14, 0xD1, 0xFF, 0x84, 0xC0, 0x74, 0x01, 0xCC, 0x49, 0x8B, 0x4E, 0x08, 0x48,
	0xB8, 0x67, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x49, 0x2B, 0x0E, 0x48, 0xF7,
	0xE9, 0x89, 0xB4, 0x24, 0x80, 0x00, 0x00, 0x00, 0x48, 0x8B, 0xDA, 0x48, 0xC1, 0xFB,
	0x04, 0x48, 0x8B, 0xC3, 0x48, 0xC1, 0xE8, 0x3F, 0x48, 0x03, 0xD8, 0x0F, 0x84, 0x84,
	0x00, 0x00, 0x00, 0x48, 0x8D, 0x3C, 0x9B, 0x48, 0xC1, 0xE7, 0x03, 0x0F, 0x1F, 0x44,
	0x00, 0x00, 0x4D, 0x8B, 0x16, 0x49, 0x8B, 0x44, 0x3A, 0xF8, 0x48, 0x85, 0xC0, 0x74,
	0x60, 0x48, 0x8B, 0x50, 0x08, 0x49, 0x8B, 0xCF, 0x48, 0x8B, 0x40, 0x10, 0x48, 0x2B,
	0xC2, 0x48, 0xC1, 0xE9, 0x08, 0x48, 0xC1, 0xF8, 0x03, 0x81, 0xE1, 0xFF, 0x0F, 0x00,
	0x00, 0x48, 0x3B, 0xC8, 0x73, 0x3F, 0x48, 0x8B, 0x14, 0xCA, 0x48, 0x85, 0xD2, 0x74,
	0x36, 0x41, 0x0F, 0xB6, 0xC7, 0x8B, 0x14, 0x82, 0x81, 0xE2, 0xFF, 0xFF, 0x0F, 0x00,
	0x81, 0xFA, 0xFF, 0xFF, 0x0F, 0x00, 0x74, 0x21, 0x49, 0x8B, 0x44, 0x3A, 0xF0, 0x49,
	0x8D, 0x4A, 0xD8, 0x4C, 0x8D, 0x8C, 0x24, 0x84, 0x00, 0x00, 0x00, 0x49, 0x8B, 0xD6,
	0x4C, 0x8D, 0x84, 0x24, 0x80, 0x00, 0x00, 0x00, 0x48, 0x03, 0xCF, 0xFF, 0x10, 0x48,
	0x83, 0xEF, 0x28, 0x48, 0x83, 0xEB, 0x01, 0x75, 0x89, 0x49, 0x8B, 0x46, 0x30, 0x81,
	0xE6, 0x00, 0x00, 0xF0, 0xFF, 0x81, 0xC6, 0x00, 0x00, 0x10, 0x00, 0x41, 0x0B, 0x76,
	0x60, 0x89, 0x34, 0xA8, 0x41, 0x89, 0x6E, 0x60, 0x4C, 0x8B, 0x7C, 0x24, 0x70, 0x48,
	0x8B, 0x5C, 0x24, 0x78, 0x48, 0x83, 0xC4, 0x40,
};

// Layout
constexpr std::size_t   kSparseBegin   = 0x08;
constexpr std::size_t   kSparseEnd     = 0x10;
constexpr std::size_t   kPackedBegin   = 0x20;
constexpr std::size_t   kPackedEnd     = 0x28;
constexpr std::size_t   kKeysBegin     = 0x38;
constexpr std::size_t   kStoragesBegin = 0x00;
constexpr std::size_t   kStoragesEnd   = 0x08;
constexpr std::size_t   kEntryStride   = 40;
constexpr std::size_t   kEntryCallback = 0x18;
constexpr std::size_t   kEntryStorage  = 0x20;
constexpr std::size_t   kEntities      = 0x30;
constexpr std::size_t   kFreeHead      = 0x60;
constexpr std::uint32_t kIndexMask     = 0xFFFFF;
constexpr std::uint32_t kVersionMask   = 0xFFF00000;
constexpr std::uint32_t kVersionStep   = 0x100000;

using DestroyByKeyFn = void(__fastcall*)(void* registry, std::uint64_t key) noexcept;
using StorageFn      = std::uint8_t*(__fastcall*)(void* registry) noexcept;
using SlotOfFn       = std::uint64_t(__fastcall*)(void* storage, std::uint32_t entity) noexcept;
// A storage remove callback takes the entities as a range [first, last)
// (0x1406BCBF0 -> 0x140743F20: destroy listeners, then swap-and-pop).
using RemoveFn       = void(__fastcall*)(void* entry, void* registry, std::uint32_t* first, std::uint32_t* last) noexcept;

template <typename T>
auto ReadAt(const void* base, std::size_t offset) noexcept -> T {
	T value;
	std::memcpy(&value, static_cast<const std::uint8_t*>(base) + offset, sizeof(T));
	return value;
}

template <typename T>
void WriteAt(void* base, std::size_t offset, T value) noexcept {
	std::memcpy(static_cast<std::uint8_t*>(base) + offset, &value, sizeof(T));
}

const D2RL::PluginContext* g_context  = nullptr;

DestroyByKeyFn             g_original = nullptr;
StorageFn                  g_storage  = nullptr;
SlotOfFn                   g_slotOf   = nullptr;

volatile LONG64 g_direct    = 0;
volatile LONG64 g_notFound  = 0;
volatile LONG64 g_fallbacks = 0;

// Does this storage hold the entity? The engine's inline sparse-page test.
auto StorageHolds(const std::uint8_t* storage, std::uint32_t entity) noexcept -> bool {
	const auto* pagesBegin = ReadAt<const std::uint64_t*>(storage, kSparseBegin);
	const auto* pagesEnd   = ReadAt<const std::uint64_t*>(storage, kSparseEnd);
	const std::uint64_t page = (static_cast<std::uint64_t>(entity) >> 8) & 0xFFF;
	if (page >= static_cast<std::uint64_t>(pagesEnd - pagesBegin)) {
		return false;
	}
	const auto* slots = reinterpret_cast<const std::uint32_t*>(pagesBegin[page]);
	if (slots == nullptr) {
		return false;
	}
	return (slots[entity & 0xFF] & kIndexMask) != kIndexMask;
}

// 0x97271F..0x972841: destroy the entity the search found.
void DestroyEntity(std::uint8_t* registry, std::uint32_t entity) noexcept {
	const std::uint32_t index = entity & kIndexMask;
	if (index == kIndexMask) {
		return;
	}
	// The engine stores the entity at [rsp+80h] and passes [rsp+84h] as the
	// end of the range: a one-entity range [first, first + 1).
	std::uint32_t range[1]{ entity };
	const std::int64_t span = ReadAt<std::int64_t>(registry, kStoragesEnd) - ReadAt<std::int64_t>(registry, kStoragesBegin);
	for (std::int64_t remaining = span / static_cast<std::int64_t>(kEntryStride), offset = remaining * static_cast<std::int64_t>(kEntryStride);
	     remaining != 0; --remaining, offset -= static_cast<std::int64_t>(kEntryStride)) {
		std::uint8_t* const entries = ReadAt<std::uint8_t*>(registry, kStoragesBegin);
		std::uint8_t* const entry   = entries + offset - static_cast<std::int64_t>(kEntryStride);
		const auto*         storage = ReadAt<const std::uint8_t*>(entry, kEntryStorage);
		if (storage == nullptr || !StorageHolds(storage, entity)) {
			continue;
		}
		const auto* table  = ReadAt<const std::uintptr_t*>(entry, kEntryCallback);
		const auto  remove = reinterpret_cast<RemoveFn>(table[0]);
		remove(entry, registry, range, range + 1);
	}
	auto* const entities = ReadAt<std::uint32_t*>(registry, kEntities);
	entities[index] = ((entity & kVersionMask) + kVersionStep) | ReadAt<std::uint32_t>(registry, kFreeHead);
	WriteAt<std::uint32_t>(registry, kFreeHead, index);
}

void __fastcall HookDestroyByKey(void* registry, std::uint64_t key) noexcept {
	std::uint8_t* const storage = g_storage(registry); // same call as the engine, first thing
	if (storage == nullptr) {
		g_original(registry, key);
		return;
	}
	const auto*       packed = ReadAt<const std::uint32_t*>(storage, kPackedBegin);
	const std::size_t count  = static_cast<std::size_t>(ReadAt<const std::uint32_t*>(storage, kPackedEnd) - packed);
	const auto*       keys   = ReadAt<const std::uint64_t*>(storage, kKeysBegin);

	std::size_t position = count;
	while (position != 0 && keys[position - 1] != key) {
		--position;
	}
	if (position == 0) {
		InterlockedIncrement64(&g_notFound); // the engine's walk finds nothing either
		return;
	}
	const std::uint32_t entity = packed[position - 1];
	if (g_slotOf(storage, entity) != position - 1) {
		InterlockedIncrement64(&g_fallbacks);
		g_original(registry, key);
		return;
	}
	InterlockedIncrement64(&g_direct);
	DestroyEntity(static_cast<std::uint8_t*>(registry), entity);
}

// ---- installation and console (unit entity lookup) ----

using perf_common::ByteCount;

auto Install(const D2RL::PluginContext* context) noexcept -> bool {
	g_context = context;
	if (!context->CheckExpectedBytes(kDestroyByKeyRva, kBody, ByteCount(kBody))) {
		context->LogError("Unit entity lookup: code at RVA 0x009725E0 does not match the expected bytes - most likely the old d2rl-celestialrayone-unit-entity-lookup.dll is still in the plugins folder (delete it), or a different game build.");
		return false;
	}
	const auto base = reinterpret_cast<std::uintptr_t>(::GetModuleHandleW(nullptr));
	if (base == 0) {
		context->LogError("Unit entity lookup: could not resolve the main module base.");
		return false;
	}
	g_storage = reinterpret_cast<StorageFn>(base + kStorageRva);
	g_slotOf  = reinterpret_cast<SlotOfFn>(base + kSlotOfRva);
	if (!context->InstallInlineHook(kDestroyByKeyRva, kPrologue, ByteCount(kPrologue), &HookDestroyByKey, &g_original)) {
		context->LogError("Unit entity lookup: failed to install the hook.");
		return false;
	}
	return true;
}

void ResetCounters() noexcept {
	InterlockedExchange64(&g_direct, 0);
	InterlockedExchange64(&g_notFound, 0);
	InterlockedExchange64(&g_fallbacks, 0);
}

void Status(const D2RL::PluginContext* console) noexcept {
	char line[256]{};
	std::snprintf(line, sizeof(line), "  %lld found directly, %lld not present, %lld by the game's own code.",
	              static_cast<long long>(g_direct), static_cast<long long>(g_notFound), static_cast<long long>(g_fallbacks));
	console->WriteConsoleMessage(line);
}

} // namespace unit_entity_lookup

// ===========================================================================
// Entity pool (HD entity registry allocator)
//
// Every component storage of D2R's HD entity registry (one set of components
// per missile visual, among everything else) comes from one TLSF heap with a
// fixed 70 MB pool, created at startup by 0xD714E0 ("Entity System Pool").
// Around 16k missiles the pool is full: the allocator returns null, the game
// asserts "Unable to allocate for entity pool" (entt_config.h:31) and then
// writes through the null pointer and crashes.
//
// This part gives that heap more room, the game's own way:
//   - when the plugin loads before the pool exists, the four copies of the
//     pool size in 0xD714E0 are raised and the game creates the bigger pool
//     itself;
//   - when the pool already exists, a second pool is added to the same TLSF
//     heap with the exact steps tlsf_create_with_pool (0x1215A30) uses for its
//     first pool: first block marked free, the game's block_insert
//     (0x1215650), end sentinel. It is taken from the same parent allocator,
//     under the allocator's own lock, and registered with the game's memory
//     tracker under the same name. The allocator's allocate, free, resize and
//     size calls (0x82DAD0, 0x82DC60, 0x82DBF0, 0x82DCB0) never look at which
//     pool a block is in.
// ===========================================================================

namespace entity_pool {

constexpr std::uint64_t kInitRva          = 0x00D714E0ULL; // creates the pool
constexpr std::uint64_t kAccessorRva      = 0x00913F60ULL; // returns the allocator object (lea at +0x27)
constexpr std::uint64_t kAddPoolRva       = 0x01215BA4ULL; // inline add-pool inside tlsf_create_with_pool
constexpr std::uint64_t kAllocatorRva     = 0x02677438ULL; // the allocator object
constexpr std::uint64_t kBlockInsertRva   = 0x01215650ULL; // TLSF block_insert(control, block)
constexpr std::uint64_t kRegisterRva      = 0x0120D280ULL; // tracker: register(memory, size, name, allocator name)
constexpr std::uint64_t kPoolNameRva      = 0x01DF0BA8ULL; // "Entity System Pool"
constexpr std::uint64_t kAllocatorNameRva = 0x01D6E078ULL; // "TlsfAlloctor"
constexpr std::uint32_t kGameBytes        = 0x04600000U;   // 70 MB
constexpr std::uint32_t kMaxBytes         = 0x7F800000U;   // the stored size is a sign-extended imm32

// 0xD714E0..0xD7156F: allocator setup, allocation, TLSF creation, registration.
constexpr std::uint8_t kInitBytes[]{
	0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x74, 0x24, 0x10, 0x48, 0x89, 0x7C, 0x24,
	0x18, 0x4C, 0x89, 0x74, 0x24, 0x20, 0x55, 0x48, 0x8B, 0xEC, 0x48, 0x81, 0xEC, 0x80,
	0x00, 0x00, 0x00, 0x48, 0x8B, 0x05, 0xC2, 0x9D, 0xC5, 0x01, 0x48, 0x33, 0xC4, 0x48,
	0x89, 0x45, 0xF0, 0xE8, 0x4E, 0x2A, 0xBA, 0xFF, 0x48, 0x8B, 0xD8, 0xE8, 0xA6, 0x4E,
	0x49, 0x00, 0x48, 0x89, 0x43, 0x08, 0xBA, 0x00, 0x00, 0x60, 0x04, 0x41, 0xB8, 0x10,
	0x00, 0x00, 0x00, 0x48, 0x8B, 0x08, 0x4C, 0x8B, 0x49, 0x08, 0x48, 0x8B, 0xC8, 0x41,
	0xFF, 0xD1, 0xBA, 0x00, 0x00, 0x60, 0x04, 0x48, 0x89, 0x43, 0x10, 0x48, 0x8B, 0xC8,
	0x48, 0xC7, 0x43, 0x18, 0x00, 0x00, 0x60, 0x04, 0xE8, 0xE1, 0x44, 0x4A, 0x00, 0x48,
	0x8B, 0x4B, 0x10, 0x4C, 0x8D, 0x0D, 0x1E, 0xCB, 0xFF, 0x00, 0x4C, 0x8D, 0x05, 0x47,
	0xF6, 0x07, 0x01, 0x48, 0x89, 0x43, 0x20, 0xBA, 0x00, 0x00, 0x60, 0x04, 0xE8, 0x11,
	0xBD, 0x49, 0x00,
};
// 0x913F60..0x913F93: the accessor's fast path, whose lea yields 0x2677438.
constexpr std::uint8_t kAccessorBytes[]{
	0x48, 0x83, 0xEC, 0x28, 0x65, 0x48, 0x8B, 0x04, 0x25, 0x58, 0x00, 0x00, 0x00, 0x8B,
	0x0D, 0x55, 0x05, 0xCA, 0x02, 0xBA, 0x6C, 0x12, 0x00, 0x00, 0x48, 0x8B, 0x0C, 0xC8,
	0x8B, 0x04, 0x0A, 0x39, 0x05, 0x73, 0xFE, 0xB9, 0x02, 0x7F, 0x0C, 0x48, 0x8D, 0x05,
	0xAA, 0x34, 0xD6, 0x01, 0x48, 0x83, 0xC4, 0x28, 0xC3,
};
// 0x1215BA4..0x1215BE5: size check, first block free, block_insert(control
// in rcx, block in rdx), end sentinel.
constexpr std::uint8_t kAddPoolBytes[]{
	0x48, 0x8D, 0x41, 0xE8, 0x48, 0xBA, 0xE8, 0xFF, 0xFF, 0xFF, 0x03, 0x00, 0x00, 0x00,
	0x48, 0x3B, 0xC2, 0x77, 0x41, 0x48, 0x83, 0xE1, 0xFD, 0x48, 0x8D, 0x5E, 0xF8, 0x48,
	0x83, 0xC9, 0x01, 0x48, 0x8B, 0xD3, 0x48, 0x89, 0x0E, 0x48, 0x8B, 0xCF, 0xE8, 0x7F,
	0xFA, 0xFF, 0xFF, 0x48, 0x8B, 0x06, 0x48, 0x83, 0xE0, 0xFC, 0x48, 0x89, 0x1C, 0x30,
	0x48, 0xC7, 0x44, 0x30, 0x08, 0x02, 0x00, 0x00, 0x00,
};

struct SizeSite {
	std::uint64_t rva;
	std::uint8_t  bytes[8];
	std::uint32_t size;
};
// The four copies of the pool size, allocation first so a failed patch can
// never leave the heap told it owns more than was allocated.
constexpr SizeSite kSizeSites[4]{
	{ 0x00D7151EULL, { 0xBA, 0x00, 0x00, 0x60, 0x04 }, 5 },                   // mov edx, size (allocation)
	{ 0x00D71536ULL, { 0xBA, 0x00, 0x00, 0x60, 0x04 }, 5 },                   // mov edx, size (tlsf_create_with_pool)
	{ 0x00D71542ULL, { 0x48, 0xC7, 0x43, 0x18, 0x00, 0x00, 0x60, 0x04 }, 8 }, // mov qword [rbx+18h], size
	{ 0x00D71565ULL, { 0xBA, 0x00, 0x00, 0x60, 0x04 }, 5 },                   // mov edx, size (tracker)
};

// Allocator object: +0x08 parent allocator, +0x10 memory, +0x18 size,
// +0x20 TLSF heap, +0x28 SRW lock (0xD714E0 writes the first four,
// 0x82DAD0 takes the lock).
constexpr std::size_t   kParent       = 0x08;
constexpr std::size_t   kMemory       = 0x10;
constexpr std::size_t   kSize         = 0x18;
constexpr std::size_t   kHeap         = 0x20;
constexpr std::size_t   kLock         = 0x28;
constexpr std::uint64_t kControlBytes = 0x1B90; // TLSF control block; the first pool follows it

using BlockInsertFn    = void(__fastcall*)(void* control, void* block) noexcept;
using RegisterFn       = void(__fastcall*)(void* memory, std::uint64_t size, const char* name, const char* allocatorName) noexcept;
using ParentAllocateFn = void*(__fastcall*)(void* allocator, std::uint64_t size, std::uint64_t alignment) noexcept;

template <typename T>
auto ReadAt(const void* base, std::size_t offset) noexcept -> T {
	T value;
	std::memcpy(&value, static_cast<const std::uint8_t*>(base) + offset, sizeof(T));
	return value;
}

template <typename T>
void WriteAt(void* base, std::size_t offset, T value) noexcept {
	std::memcpy(static_cast<std::uint8_t*>(base) + offset, &value, sizeof(T));
}

enum class Mode {
	None,
	Raised, // the game created the pool at the raised size
	Added,  // a second pool was added to the existing heap
};

Mode          g_mode        = Mode::None;
std::uint8_t* g_allocator   = nullptr;
std::uint8_t* g_addedPool   = nullptr;
std::uint64_t g_addedBytes  = 0;
BlockInsertFn g_blockInsert = nullptr;
RegisterFn    g_register    = nullptr;

// tlsf_add_pool, the same steps as the inline one at 0x1215BA4.
auto AddPool(void* control, std::uint8_t* memory, std::uint64_t bytes) noexcept -> std::uint64_t {
	const std::uint64_t poolBytes = (bytes - 16) & ~std::uint64_t{ 7 };
	if (bytes < 64 || poolBytes - 0x18 > 0x3FFFFFFE8ULL) {
		return 0;
	}
	std::uint8_t* const block = memory - 8;                   // its prev_phys field is never touched
	const std::uint64_t sizeField = (poolBytes & ~std::uint64_t{ 2 }) | 1; // free, previous block in use
	WriteAt<std::uint64_t>(memory, 0, sizeField);
	g_blockInsert(control, block);
	const std::uint64_t size = sizeField & ~std::uint64_t{ 3 };
	WriteAt<void*>(memory + size, 0, block);                  // sentinel: previous physical block
	WriteAt<std::uint64_t>(memory + size, 8, 2);              // sentinel: size 0, previous block free
	return size;
}

auto PatchSizes(const D2RL::PluginContext* context, std::uint32_t bytes) noexcept -> bool {
	std::uint8_t patched[4][8]{};
	int applied = 0;
	for (int index = 0; index < 4; ++index) {
		const SizeSite& site = kSizeSites[index];
		std::memcpy(patched[index], site.bytes, site.size);
		std::memcpy(patched[index] + site.size - 4, &bytes, 4);
		if (!context->PatchBytes(site.rva, site.bytes, site.size, patched[index], site.size)) {
			break;
		}
		++applied;
	}
	if (applied == 4) {
		return true;
	}
	// Put back what was changed, last first, so the sizes stay consistent.
	while (applied > 0) {
		--applied;
		const SizeSite& site = kSizeSites[applied];
		if (!context->PatchBytes(site.rva, patched[applied], site.size, site.bytes, site.size)) {
			context->LogError("Entity pool: could not restore a pool size site after a failed patch; restart the game.");
		}
	}
	return false;
}

auto Install(const D2RL::PluginContext* context, std::uint32_t megabytes) noexcept -> bool {
	const auto base = reinterpret_cast<std::uintptr_t>(::GetModuleHandleW(nullptr));
	if (base == 0) {
		context->LogError("Entity pool: could not resolve the main module base.");
		return false;
	}
	bool ok = perf_common::CheckSite(context, kInitRva, kInitBytes, perf_common::ByteCount(kInitBytes), "Entity pool: pool setup");
	ok = perf_common::CheckSite(context, kAccessorRva, kAccessorBytes, perf_common::ByteCount(kAccessorBytes), "Entity pool: allocator accessor") && ok;
	ok = perf_common::CheckSite(context, kAddPoolRva, kAddPoolBytes, perf_common::ByteCount(kAddPoolBytes), "Entity pool: TLSF add-pool") && ok;
	if (!ok) {
		return false;
	}
	const std::uint64_t wanted = static_cast<std::uint64_t>(megabytes) * 1024 * 1024;
	if (wanted <= kGameBytes || wanted > kMaxBytes) {
		context->LogError("Entity pool: entity_pool_mb must be above 70 and at most 2040.");
		return false;
	}
	g_allocator   = reinterpret_cast<std::uint8_t*>(base + kAllocatorRva);
	if (g_blockInsert == nullptr) {
		g_blockInsert = reinterpret_cast<BlockInsertFn>(base + kBlockInsertRva);
	}
	if (g_register == nullptr) {
		g_register = reinterpret_cast<RegisterFn>(base + kRegisterRva);
	}

	if (ReadAt<void*>(g_allocator, kHeap) == nullptr) {
		// Not created yet: the game will create it at the raised size.
		if (!PatchSizes(context, static_cast<std::uint32_t>(wanted))) {
			context->LogError("Entity pool: the pool size could not be raised.");
			return false;
		}
		g_mode = Mode::Raised;
		return true;
	}

	// Already created: add a second pool to the same heap.
	const std::uint64_t current = ReadAt<std::uint64_t>(g_allocator, kSize);
	if (wanted <= current) {
		g_mode = Mode::Raised; // already at least that big
		return true;
	}
	const std::uint64_t extra = (wanted - current) & ~std::uint64_t{ 15 };
	auto* const lock = reinterpret_cast<PSRWLOCK>(g_allocator + kLock);
	AcquireSRWLockExclusive(lock);
	void* const parent = ReadAt<void*>(g_allocator, kParent);
	const auto parentAllocate = reinterpret_cast<ParentAllocateFn>(ReadAt<std::uintptr_t*>(parent, 0)[1]);
	auto* const memory = static_cast<std::uint8_t*>(parentAllocate(parent, extra, 16));
	std::uint64_t added = 0;
	if (memory != nullptr) {
		added = AddPool(ReadAt<void*>(g_allocator, kHeap), memory, extra);
	}
	ReleaseSRWLockExclusive(lock);
	if (added == 0) {
		context->LogError("Entity pool: could not add memory to the entity heap.");
		return false;
	}
	g_register(memory, extra, reinterpret_cast<const char*>(base + kPoolNameRva), reinterpret_cast<const char*>(base + kAllocatorNameRva));
	g_addedPool  = memory;
	g_addedBytes = extra;
	g_mode       = Mode::Added;
	return true;
}

struct Usage {
	std::uint64_t total;
	std::uint64_t used;
	std::uint64_t free;
	std::uint64_t largestFree;
};

// Walks one pool's blocks: size field bit 0 = free, a zero size ends the pool.
void WalkPool(const std::uint8_t* pool, const std::uint8_t* end, Usage* usage) noexcept {
	const std::uint8_t* block = pool - 8;
	while (block + 16 <= end) {
		const std::uint64_t field = ReadAt<std::uint64_t>(block, 8);
		const std::uint64_t size  = field & ~std::uint64_t{ 3 };
		if (size == 0) {
			break;
		}
		if ((field & 1) != 0) {
			usage->free += size;
			usage->largestFree = size > usage->largestFree ? size : usage->largestFree;
		} else {
			usage->used += size;
		}
		block += 8 + size;
	}
}

auto Measure() noexcept -> Usage {
	Usage usage{};
	if (g_allocator == nullptr || ReadAt<void*>(g_allocator, kHeap) == nullptr) {
		return usage;
	}
	auto* const lock = reinterpret_cast<PSRWLOCK>(g_allocator + kLock);
	AcquireSRWLockExclusive(lock);
	const auto*         memory = ReadAt<const std::uint8_t*>(g_allocator, kMemory);
	const std::uint64_t size   = ReadAt<std::uint64_t>(g_allocator, kSize);
	usage.total = size + g_addedBytes;
	WalkPool(memory + kControlBytes, memory + size, &usage);
	if (g_addedPool != nullptr) {
		WalkPool(g_addedPool, g_addedPool + g_addedBytes, &usage);
	}
	ReleaseSRWLockExclusive(lock);
	return usage;
}

void Status(const D2RL::PluginContext* console) noexcept {
	const Usage usage = Measure();
	char line[256]{};
	std::snprintf(line, sizeof(line), "  %.0f MB (%s), %.1f MB in use, %.1f MB free, largest free block %.1f MB.",
	              static_cast<double>(usage.total) / (1024.0 * 1024.0),
	              g_mode == Mode::Added ? "the game's 70 MB plus a pool added by this plugin" : "created at this size by the game",
	              static_cast<double>(usage.used) / (1024.0 * 1024.0), static_cast<double>(usage.free) / (1024.0 * 1024.0),
	              static_cast<double>(usage.largestFree) / (1024.0 * 1024.0));
	console->WriteConsoleMessage(line);
}

} // namespace entity_pool

// ===========================================================================
// Plugin: config, console, loading
// ===========================================================================

enum class PartState {
	Disabled, // switched off in the config file
	Live,
	Failed,   // could not install; the log says why, the game's own code runs
};

struct PartsConfig {
	bool enabled            = true;
	bool roomFastRemoval    = true;
	bool roomUnitSearch     = true;
	bool skipRoomAddCheck   = true;
	bool translationQueue   = true;
	bool weakInstances      = true;
	bool clientUnitLoop     = true;
	bool unitEntityLookup   = true;
	std::uint32_t entityPoolMb = 1024; // 0 or 70 and below: leave the game's 70 MB
};

PartsConfig g_parts{};
PartState   g_roomIndexState        = PartState::Disabled;
PartState   g_roomAddCheckState     = PartState::Disabled;
PartState   g_translationQueueState = PartState::Disabled;
PartState   g_weakInstancesState    = PartState::Disabled;
PartState   g_clientUnitLoopState   = PartState::Disabled;
PartState   g_unitEntityLookupState = PartState::Disabled;
PartState   g_entityPoolState       = PartState::Disabled;

constexpr const char* kPartsConfigToml =
	"# Performance improvements\n"
	"#\n"
	"# Every missile-performance fix in one plugin. Each fix removes work the\n"
	"# game did in a way that grew with the square of the number of missiles\n"
	"# alive. None of them changes gameplay: the game gets the same results, it\n"
	"# just stops repeating the same search thousands of times per frame.\n"
	"# Each fix can be switched off on its own below.\n"
	"#\n"
	"# Console:\n"
	"#   perf           state of every fix and its counters\n"
	"#   perf reset     zero the counters\n"
	"#   perf verify    check the room index against the game's room lists\n"
	"#   perf memory    memory held by this plugin, the game's RAM and commit,\n"
	"#                  system headroom, and the game's video memory per GPU.\n"
	"#                  Read it before spawning missiles, while N are alive\n"
	"#                  and after they are gone: (during - before) / N is the\n"
	"#                  cost of one missile.\n"
	"#\n"
	"# This plugin replaces the separate plugins room-unit-index,\n"
	"# translation-queue, client-unit-loop, weak-instances and\n"
	"# unit-entity-lookup, and the patch file skip-room-add-duplicate-check.json.\n"
	"\n"
	"[performance-improvements]\n"
	"\n"
	"# Master switch. false installs nothing.\n"
	"enabled = true\n"
	"\n"
	"# Room lists, server and client. Every missile sits in its room's unit\n"
	"# list; a side index next to each list makes two things cheap:\n"
	"#   room_fast_removal  a unit (a dying missile) leaves its room without a\n"
	"#                      walk over the room's whole list;\n"
	"#   room_unit_search   missile collision checks look at players and\n"
	"#                      monsters only, for missiles.txt CollideType 1, 2, 3,\n"
	"#                      5 and 8, the types that can only ever hit those.\n"
	"room_fast_removal = true\n"
	"room_unit_search = true\n"
	"\n"
	"# Skip the debug-only duplicate check that walked a room's whole unit list\n"
	"# every time a unit (every new missile) was added to it. If the old patch\n"
	"# file skip-room-add-duplicate-check.json is still installed, this notices\n"
	"# and leaves its patch alone; the file can be deleted.\n"
	"skip_room_add_check = true\n"
	"\n"
	"# HD layer: drop superseded unit messages (create, move, remove) with hash\n"
	"# lookups instead of comparing every message with every earlier one.\n"
	"translation_queue = true\n"
	"\n"
	"# HD layer: a destroyed missile visual leaves its type's instance list\n"
	"# directly instead of a walk over every other visual of that type. The main\n"
	"# cost of many missiles colliding or expiring at the same time.\n"
	"weak_instances = true\n"
	"\n"
	"# Client: skip the per-unit re-lookup whose result the game only uses for\n"
	"# monsters and objects (it was thrown away for every missile).\n"
	"client_unit_loop = true\n"
	"\n"
	"# HD entity pool, in MB. Every HD entity component (every missile visual\n"
	"# among them) comes from one fixed pool the game makes 70 MB big. Around\n"
	"# 16k missiles it is full and the game crashes (\"Unable to allocate for\n"
	"# entity pool\"). This makes it bigger. Memory is only used as the game\n"
	"# fills it. 70 or less (or 0) leaves the game's 70 MB; at most 2040.\n"
	"entity_pool_mb = 1024\n"
	"\n"
	"# HD layer: find a removed unit's HD entity by scanning the key array\n"
	"# instead of walking every entity with a lookup and a call each. The\n"
	"# second cost of mass missile deaths.\n"
	"unit_entity_lookup = true\n";

auto FindConfigValue(const char* text, const char* key) noexcept -> const char* {
	const std::size_t keyLength = std::strlen(key);
	for (const char* line = text; *line != '\0';) {
		const char* cursor = line;
		while (*cursor == ' ' || *cursor == '\t') {
			++cursor;
		}
		if (*cursor != '#' && std::strncmp(cursor, key, keyLength) == 0) {
			const char* after = cursor + keyLength;
			while (*after == ' ' || *after == '\t') {
				++after;
			}
			if (*after == '=') {
				++after;
				while (*after == ' ' || *after == '\t') {
					++after;
				}
				return after;
			}
		}
		while (*line != '\0' && *line != '\n') {
			++line;
		}
		if (*line == '\n') {
			++line;
		}
	}
	return nullptr;
}

auto ReadConfigBool(const char* text, const char* key, bool fallback) noexcept -> bool {
	const char* value = FindConfigValue(text, key);
	if (value == nullptr) {
		return fallback;
	}
	if (value[0] == 't' || value[0] == 'T' || value[0] == '1') {
		return true;
	}
	if (value[0] == 'f' || value[0] == 'F' || value[0] == '0') {
		return false;
	}
	return fallback;
}

auto ReadConfigUnsigned(const char* text, const char* key, std::uint32_t fallback) noexcept -> std::uint32_t {
	const char* value = FindConfigValue(text, key);
	if (value == nullptr || *value < '0' || *value > '9') {
		return fallback;
	}
	std::uint64_t parsed = 0;
	while (*value >= '0' && *value <= '9' && parsed < 100000) {
		parsed = parsed * 10 + static_cast<std::uint64_t>(*value - '0');
		++value;
	}
	return parsed > 100000 ? fallback : static_cast<std::uint32_t>(parsed);
}

void LoadPartsConfig(const D2RL::PluginContext* context) noexcept {
	if (!context->EnsureConfig(kPartsConfigToml)) {
		context->LogWarn("Could not create the config file, using built in defaults.");
		return;
	}
	char          toml[8192]{};
	std::uint32_t required = 0;
	if (!context->ReadConfig(toml, static_cast<std::uint32_t>(sizeof(toml)), &required)) {
		context->LogWarn("Could not read the config file, using built in defaults.");
		return;
	}
	toml[sizeof(toml) - 1] = '\0';
	g_parts.enabled          = ReadConfigBool(toml, "enabled", g_parts.enabled);
	g_parts.roomFastRemoval  = ReadConfigBool(toml, "room_fast_removal", g_parts.roomFastRemoval);
	g_parts.roomUnitSearch   = ReadConfigBool(toml, "room_unit_search", g_parts.roomUnitSearch);
	g_parts.skipRoomAddCheck = ReadConfigBool(toml, "skip_room_add_check", g_parts.skipRoomAddCheck);
	g_parts.translationQueue = ReadConfigBool(toml, "translation_queue", g_parts.translationQueue);
	g_parts.weakInstances    = ReadConfigBool(toml, "weak_instances", g_parts.weakInstances);
	g_parts.clientUnitLoop   = ReadConfigBool(toml, "client_unit_loop", g_parts.clientUnitLoop);
	g_parts.unitEntityLookup = ReadConfigBool(toml, "unit_entity_lookup", g_parts.unitEntityLookup);
	g_parts.entityPoolMb     = ReadConfigUnsigned(toml, "entity_pool_mb", g_parts.entityPoolMb);
}

auto StateText(PartState state) noexcept -> const char* {
	switch (state) {
		case PartState::Live:
			return "live";
		case PartState::Failed:
			return "FAILED (see the plugin log)";
		default:
			return "off (config)";
	}
}

void StatusHeader(const D2RL::PluginContext* console, const char* name, PartState state) noexcept {
	char line[160]{};
	std::snprintf(line, sizeof(line), "%s: %s", name, StateText(state));
	console->WriteConsoleMessage(line);
}

auto ArgumentIs(const D2RL::ConsoleCommandContext* command, const char* word) noexcept -> bool {
	if (command->args == nullptr || command->argsLength == 0) {
		return false;
	}
	const char* cursor = command->args;
	const char* end    = command->args + command->argsLength;
	while (cursor < end && (*cursor == ' ' || *cursor == '\t')) {
		++cursor;
	}
	const std::size_t length = std::strlen(word);
	if (static_cast<std::size_t>(end - cursor) < length || std::strncmp(cursor, word, length) != 0) {
		return false;
	}
	cursor += length;
	return cursor == end || *cursor == ' ' || *cursor == '\t' || *cursor == '\0';
}

auto Megabytes(std::uint64_t bytes) noexcept -> double { return static_cast<double>(bytes) / (1024.0 * 1024.0); }
auto Gigabytes(std::uint64_t bytes) noexcept -> double { return static_cast<double>(bytes) / (1024.0 * 1024.0 * 1024.0); }

#if defined(_WIN32)
// PROCESS_MEMORY_COUNTERS_EX, read through kernel32's K32GetProcessMemoryInfo.
struct ProcessMemoryCounters {
	DWORD  cb;
	DWORD  pageFaultCount;
	SIZE_T peakWorkingSetSize;
	SIZE_T workingSetSize;
	SIZE_T quotaPeakPagedPoolUsage;
	SIZE_T quotaPagedPoolUsage;
	SIZE_T quotaPeakNonPagedPoolUsage;
	SIZE_T quotaNonPagedPoolUsage;
	SIZE_T pagefileUsage;
	SIZE_T peakPagefileUsage;
	SIZE_T privateUsage;
};

void ProcessAndSystemReport(const D2RL::PluginContext* console) noexcept {
	char line[256]{};
	using GetProcessMemoryInfoFn = BOOL(WINAPI*)(HANDLE, void*, DWORD);
	const auto getInfo = reinterpret_cast<GetProcessMemoryInfoFn>(
		::GetProcAddress(::GetModuleHandleW(L"kernel32.dll"), "K32GetProcessMemoryInfo"));
	ProcessMemoryCounters counters{};
	counters.cb = sizeof(counters);
	if (getInfo != nullptr && getInfo(::GetCurrentProcess(), &counters, sizeof(counters))) {
		std::snprintf(line, sizeof(line), "  game process: %.2f GB committed (peak %.2f GB), %.2f GB of it in RAM right now.",
		              Gigabytes(counters.privateUsage), Gigabytes(counters.peakPagefileUsage), Gigabytes(counters.workingSetSize));
		console->WriteConsoleMessage(line);
	}
	MEMORYSTATUSEX status{};
	status.dwLength = sizeof(status);
	if (::GlobalMemoryStatusEx(&status)) {
		std::snprintf(line, sizeof(line), "  system: RAM %lu%% in use (%.1f of %.1f GB), commit %.1f of %.1f GB used.",
		              static_cast<unsigned long>(status.dwMemoryLoad),
		              Gigabytes(status.ullTotalPhys - status.ullAvailPhys), Gigabytes(status.ullTotalPhys),
		              Gigabytes(status.ullTotalPageFile - status.ullAvailPageFile), Gigabytes(status.ullTotalPageFile));
		console->WriteConsoleMessage(line);
	}
}

// Video memory the game itself uses on each graphics card (DXGI reports the
// calling process only): dedicated (on the card) and shared (system RAM).
void GpuReport(const D2RL::PluginContext* console) noexcept {
	const HMODULE dxgi = ::GetModuleHandleW(L"dxgi.dll");
	if (dxgi == nullptr) {
		console->WriteConsoleMessage("  GPU: dxgi.dll is not loaded, no video memory figures.");
		return;
	}
	using CreateFactoryFn = HRESULT(WINAPI*)(REFIID, void**);
	const auto createFactory = reinterpret_cast<CreateFactoryFn>(::GetProcAddress(dxgi, "CreateDXGIFactory1"));
	static const GUID kFactory1{ 0x770aae78, 0xf26f, 0x4dba, { 0xa8, 0x29, 0x25, 0x3c, 0x83, 0xd1, 0xb3, 0x87 } };
	static const GUID kAdapter3{ 0x645967a4, 0x1392, 0x4310, { 0xa7, 0x98, 0x80, 0x53, 0xce, 0x3e, 0x93, 0xfd } };
	IDXGIFactory1* factory = nullptr;
	if (createFactory == nullptr || FAILED(createFactory(kFactory1, reinterpret_cast<void**>(&factory))) || factory == nullptr) {
		console->WriteConsoleMessage("  GPU: could not open DXGI, no video memory figures.");
		return;
	}
	for (UINT index = 0;; ++index) {
		IDXGIAdapter1* adapter = nullptr;
		if (factory->EnumAdapters1(index, &adapter) == DXGI_ERROR_NOT_FOUND || adapter == nullptr) {
			break;
		}
		DXGI_ADAPTER_DESC1 description{};
		if (SUCCEEDED(adapter->GetDesc1(&description)) && (description.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) == 0) {
			IDXGIAdapter3* adapter3 = nullptr;
			if (SUCCEEDED(adapter->QueryInterface(kAdapter3, reinterpret_cast<void**>(&adapter3))) && adapter3 != nullptr) {
				DXGI_QUERY_VIDEO_MEMORY_INFO local{};
				DXGI_QUERY_VIDEO_MEMORY_INFO shared{};
				adapter3->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &local);
				adapter3->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_NON_LOCAL, &shared);
				char name[128]{};
				::WideCharToMultiByte(CP_UTF8, 0, description.Description, -1, name, static_cast<int>(sizeof(name)) - 1, nullptr, nullptr);
				char line[320]{};
				std::snprintf(line, sizeof(line), "  GPU %s: the game uses %.2f GB of a %.2f GB dedicated budget, plus %.2f GB of shared system memory.",
				              name, Gigabytes(local.CurrentUsage), Gigabytes(local.Budget), Gigabytes(shared.CurrentUsage));
				console->WriteConsoleMessage(line);
				adapter3->Release();
			}
		}
		adapter->Release();
	}
	factory->Release();
}
#endif

void MemoryReport(const D2RL::PluginContext* console) noexcept {
	const room_index::MemoryHeldResult room = room_index::MemoryHeld();
	std::size_t visuals = 0;
	const std::uint64_t weak  = weak_instances::MemoryHeld(&visuals);
	const std::uint64_t queue = static_cast<std::uint64_t>(translation_queue::g_scratchBytes);
	char line[320]{};
	std::snprintf(line, sizeof(line), "perf memory: the plugin holds %.1f MB in total.", Megabytes(room.bytes + weak + queue));
	console->WriteConsoleMessage(line);
	std::snprintf(line, sizeof(line), "  room index %.1f MB (%zu units indexed now), translation queue %.1f MB (room for %lld messages), weak instances %.1f MB (%zu visuals now).",
	              Megabytes(room.bytes), room.units, Megabytes(queue), static_cast<long long>(translation_queue::g_scratchMessages),
	              Megabytes(weak), visuals);
	console->WriteConsoleMessage(line);
	if (g_entityPoolState == PartState::Live) {
		console->WriteConsoleMessage("  HD entity pool:");
		entity_pool::Status(console);
	}
#if defined(_WIN32)
	ProcessAndSystemReport(console);
	GpuReport(console);
#endif
}

auto __cdecl PerfCommand(D2R::Game::Client*                 client,
                         const D2RL::ConsoleCommandContext* command,
                         void*                              userData) noexcept -> D2RL::ConsoleCommandResult {
	(void)client;
	(void)userData;
	if (command == nullptr || command->plugin == nullptr) {
		return D2RL::ConsoleCommandResult::Failed;
	}
	const D2RL::PluginContext* console = command->plugin;

	if (ArgumentIs(command, "reset")) {
		room_index::ResetCounters();
		translation_queue::ResetCounters();
		weak_instances::ResetCounters();
		client_unit_loop::ResetCounters();
		unit_entity_lookup::ResetCounters();
		console->WriteConsoleMessage("perf: counters reset.");
		return D2RL::ConsoleCommandResult::Handled;
	}

	if (ArgumentIs(command, "memory")) {
		MemoryReport(console);
		return D2RL::ConsoleCommandResult::Handled;
	}

	if (ArgumentIs(command, "verify")) {
		if (g_roomIndexState != PartState::Live) {
			console->WriteConsoleMessage("perf verify: the room index is not running, nothing to verify.");
			return D2RL::ConsoleCommandResult::Handled;
		}
		const room_index::VerifyResult result = room_index::VerifyIndex();
		char line[200]{};
		std::snprintf(line, sizeof(line), "perf verify: %u rooms, %llu units checked, %u rooms differed and were rebuilt.",
		              result.rooms, static_cast<unsigned long long>(result.units), result.repaired);
		console->WriteConsoleMessage(line);
		return D2RL::ConsoleCommandResult::Handled;
	}

	if (!g_parts.enabled) {
		console->WriteConsoleMessage("perf: the plugin is switched off in its config file (enabled = false).");
		return D2RL::ConsoleCommandResult::Handled;
	}
	StatusHeader(console, "entity pool", g_entityPoolState);
	if (g_entityPoolState == PartState::Live) {
		entity_pool::Status(console);
	}
	StatusHeader(console, "room index", g_roomIndexState);
	if (g_roomIndexState == PartState::Live) {
		room_index::Status(console);
	}
	StatusHeader(console, "room add check", g_roomAddCheckState);
	if (g_roomAddCheckState == PartState::Live) {
		room_add_check::Status(console);
	}
	StatusHeader(console, "translation queue", g_translationQueueState);
	if (g_translationQueueState == PartState::Live) {
		translation_queue::Status(console);
	}
	StatusHeader(console, "weak instances", g_weakInstancesState);
	if (g_weakInstancesState == PartState::Live) {
		weak_instances::Status(console);
	}
	StatusHeader(console, "client unit loop", g_clientUnitLoopState);
	if (g_clientUnitLoopState == PartState::Live) {
		client_unit_loop::Status(console);
	}
	StatusHeader(console, "unit entity lookup", g_unitEntityLookupState);
	if (g_unitEntityLookupState == PartState::Live) {
		unit_entity_lookup::Status(console);
	}
	return D2RL::ConsoleCommandResult::Handled;
}

constexpr D2RL::PluginInfo kPluginInfo{
	.infoSize    = D2RL::PluginInfoSize,
	.abiVersion  = D2RL_PLUGIN_ABI_VERSION,
	.id          = "celestialrayone.performance-improvements",
	.name        = "Performance Improvements",
	.version     = "2.2.0",
	.author      = "CelestialRayOne",
	.description = "All missile-performance fixes in one plugin: entity pool, room index, room add check, translation queue, weak instances, client unit loop, unit entity lookup.",
	.flags       = D2RL::PluginFlags::Shared | D2RL::PluginFlags::NativeHooks,
};

auto Run(bool wanted, bool (*install)(const D2RL::PluginContext*), const D2RL::PluginContext* context) noexcept -> PartState {
	if (!wanted) {
		return PartState::Disabled;
	}
	return install(context) ? PartState::Live : PartState::Failed;
}

auto InstallEntityPool(const D2RL::PluginContext* context) noexcept -> bool {
	return entity_pool::Install(context, g_parts.entityPoolMb);
}

auto InstallRoomIndex(const D2RL::PluginContext* context) noexcept -> bool {
	return room_index::Install(context, g_parts.roomFastRemoval, g_parts.roomUnitSearch);
}

} // namespace

D2RL_PLUGIN_EXPORT auto D2RLoaderGetPluginInfo() noexcept -> const D2RL::PluginInfo* {
	return &kPluginInfo;
}

D2RL_PLUGIN_EXPORT auto D2RLoaderLoadPlugin(const D2RL::PluginContext* context) noexcept -> bool {
	if (context == nullptr) {
		return false;
	}
	LoadPartsConfig(context);
	if (!context->RegisterConsoleCommand("perf", PerfCommand,
	                                     "Performance improvements: state and counters. 'perf reset' zeroes them, 'perf verify' checks the room index, 'perf memory' shows memory use.")) {
		context->LogWarn("Console command 'perf' was not registered.");
	}
	if (!g_parts.enabled) {
		context->LogInfo("Performance improvements: disabled in config, nothing installed.");
		return true;
	}

	// Each part installs on its own; one that fails stays off and the rest
	// carry on. A part never returns with a half-installed state that changes
	// behaviour: at worst an installed hook passes straight through.
	g_entityPoolState       = Run(g_parts.entityPoolMb > 70, InstallEntityPool, context);
	g_roomAddCheckState     = Run(g_parts.skipRoomAddCheck, room_add_check::Install, context);
	g_roomIndexState        = Run(g_parts.roomFastRemoval || g_parts.roomUnitSearch, InstallRoomIndex, context);
	g_translationQueueState = Run(g_parts.translationQueue, translation_queue::Install, context);
	g_weakInstancesState    = Run(g_parts.weakInstances, weak_instances::Install, context);
	g_clientUnitLoopState   = Run(g_parts.clientUnitLoop, client_unit_loop::Install, context);
	g_unitEntityLookupState = Run(g_parts.unitEntityLookup, unit_entity_lookup::Install, context);

	const PartState states[]{ g_entityPoolState, g_roomIndexState, g_roomAddCheckState, g_translationQueueState,
	                          g_weakInstancesState, g_clientUnitLoopState, g_unitEntityLookupState };
	int live   = 0;
	int failed = 0;
	for (const PartState state : states) {
		live += state == PartState::Live ? 1 : 0;
		failed += state == PartState::Failed ? 1 : 0;
	}
	char summary[256]{};
	std::snprintf(summary, sizeof(summary),
	              "Performance improvements loaded: %d of 7 parts live, %d failed%s. Type 'perf' in the console for details.",
	              live, failed, failed != 0 ? " (see the errors above)" : "");
	if (failed != 0) {
		context->LogWarn(summary);
	} else {
		context->LogInfo(summary);
	}
	return true;
}

D2RL_PLUGIN_EXPORT void D2RLoaderUnloadPlugin() noexcept {
	room_index::Uninstall();
}
