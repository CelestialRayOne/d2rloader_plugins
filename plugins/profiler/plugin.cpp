// ---------------------------------------------------------------------------
// Sampling profiler  -  D2RLoader plugin (diagnostic, touches no game code)
//
// Shows where the game's time actually goes, with no external tool.
//
// While recording, a background thread pauses the game's main thread about
// once a millisecond for a few microseconds, reads where it is, walks its
// call stack, and lets it continue. It does the same for the busiest other
// threads (the renderer, when the renderer is what holds a frame back).
// When the recording ends it writes the report in one go to its own file,
// d2rloader/logs/celestialrayone.sampling-profiler.report.txt (replaced on
// every recording), and puts one line in this plugin's log saying so:
//   * CPU time per thread during the recording, with the thread names;
//   * per sampled thread: the functions found on the stack most often
//     ("inclusive", i.e. that function plus everything it called), the
//     innermost functions ("self", the code that was actually running), and
//     the most common complete call stacks.
// Addresses are printed as module+RVA. D2RLoader.exe RVAs are the same as
// in the IDB, so every line maps straight to a function there.
//
// Cost while recording: each sample holds a thread for a few tens of
// microseconds, a few percent of speed in total. Nothing is sampled when no
// recording is running, and no game code is patched or hooked.
//
// Console
//     profile              record for the default time (15 s), or show the
//                          progress when a recording is running
//     profile <seconds>    record for that many seconds (1 to 120)
//     profile stop         stop early and write the report
//     profile last         the headline of the last report
//
// The report used to go to the plugin log line by line. D2RLoader's logger
// takes about 13 ms per line, so a 10 s recording kept the log growing for
// another 40 s while its 3,000 lines trickled in. One file write instead
// makes the report complete a moment after the recording ends.
//
// Runtime settings live in
// d2rloader/config/celestialrayone.sampling-profiler.toml, created with
// defaults on first load.
// ---------------------------------------------------------------------------

#include <D2RLPlugin/api.h>

#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX // keep windows.h from defining min/max macros (MSVC C2589)
#endif
#include <windows.h>
#include <tlhelp32.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <new>
#include <string>
#include <unordered_map>
#include <vector>

namespace {

// --------------------------------------------------------------------------
// Settings
// --------------------------------------------------------------------------

struct Settings {
	int defaultSeconds   = 15;
	int sampleIntervalUs = 1000;
	int extraThreads     = 2;
	int reportFunctions  = 40;
	int reportStacks     = 12;
};

constexpr int kMaxStackDepth   = 64;  // frames walked per sample
constexpr int kStackKeyDepth   = 24;  // frames that identify a "call stack"
constexpr int kStackPrintDepth = 16;  // frames printed per call stack

// --------------------------------------------------------------------------
// Modules and their unwind tables
// --------------------------------------------------------------------------

struct ModuleInfo {
	std::uintptr_t          base      = 0;
	std::uintptr_t          end       = 0;
	const RUNTIME_FUNCTION* functions = nullptr;
	std::uint32_t           count     = 0;
	char                    name[64]{};
};

// Built by the sampler thread while no thread is paused; read-only while one
// is, so a lookup never takes a lock another thread could be holding.
std::vector<ModuleInfo> g_modules;

void LoadModules() {
	g_modules.clear();
	HANDLE snapshot = ::CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, ::GetCurrentProcessId());
	if (snapshot == INVALID_HANDLE_VALUE) {
		return;
	}
	MODULEENTRY32W entry{};
	entry.dwSize = sizeof(entry);
	for (BOOL more = ::Module32FirstW(snapshot, &entry); more; more = ::Module32NextW(snapshot, &entry)) {
		ModuleInfo module{};
		module.base = reinterpret_cast<std::uintptr_t>(entry.modBaseAddr);
		module.end  = module.base + entry.modBaseSize;
		::WideCharToMultiByte(CP_UTF8, 0, entry.szModule, -1, module.name, static_cast<int>(sizeof(module.name)) - 1, nullptr, nullptr);

		const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(module.base);
		if (dos->e_magic == IMAGE_DOS_SIGNATURE) {
			const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(module.base + static_cast<std::uint32_t>(dos->e_lfanew));
			if (nt->Signature == IMAGE_NT_SIGNATURE && nt->OptionalHeader.Magic == IMAGE_NT_OPTIONAL_HDR64_MAGIC
			    && nt->OptionalHeader.NumberOfRvaAndSizes > IMAGE_DIRECTORY_ENTRY_EXCEPTION) {
				const IMAGE_DATA_DIRECTORY& directory = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXCEPTION];
				if (directory.VirtualAddress != 0 && directory.Size >= sizeof(RUNTIME_FUNCTION)) {
					module.functions = reinterpret_cast<const RUNTIME_FUNCTION*>(module.base + directory.VirtualAddress);
					module.count     = directory.Size / sizeof(RUNTIME_FUNCTION);
				}
			}
		}
		g_modules.push_back(module);
	}
	::CloseHandle(snapshot);
	std::sort(g_modules.begin(), g_modules.end(),
	          [](const ModuleInfo& a, const ModuleInfo& b) { return a.base < b.base; });
}

auto FindModule(std::uintptr_t address) noexcept -> const ModuleInfo* {
	std::size_t low  = 0;
	std::size_t high = g_modules.size();
	while (low < high) {
		const std::size_t middle = (low + high) / 2;
		if (g_modules[middle].base <= address) {
			low = middle + 1;
		} else {
			high = middle;
		}
	}
	if (low == 0) {
		return nullptr;
	}
	const ModuleInfo& module = g_modules[low - 1];
	return address < module.end ? &module : nullptr;
}

auto FindFunction(const ModuleInfo& module, std::uint32_t rva) noexcept -> const RUNTIME_FUNCTION* {
	std::uint32_t low  = 0;
	std::uint32_t high = module.count;
	while (low < high) {
		const std::uint32_t middle = (low + high) / 2;
		if (module.functions[middle].BeginAddress <= rva) {
			low = middle + 1;
		} else {
			high = middle;
		}
	}
	if (low == 0) {
		return nullptr;
	}
	const RUNTIME_FUNCTION* function = &module.functions[low - 1];
	return rva < function->EndAddress ? function : nullptr;
}

// Start of the function that holds `address`, or the address itself for
// code without unwind data.
auto FunctionStart(std::uintptr_t address) noexcept -> std::uintptr_t {
	const ModuleInfo* module = FindModule(address);
	if (module == nullptr || module->functions == nullptr) {
		return address;
	}
	const RUNTIME_FUNCTION* function = FindFunction(*module, static_cast<std::uint32_t>(address - module->base));
	return function != nullptr ? module->base + function->BeginAddress : address;
}

void FormatAddress(std::uintptr_t address, char* out, std::size_t size) {
	const ModuleInfo* module = FindModule(address);
	if (module != nullptr) {
		std::snprintf(out, size, "%s+0x%llX", module->name, static_cast<unsigned long long>(address - module->base));
	} else {
		std::snprintf(out, size, "0x%llX", static_cast<unsigned long long>(address));
	}
}

// --------------------------------------------------------------------------
// Stack capture
// --------------------------------------------------------------------------

struct RawStack {
	int            depth = 0;
	std::uintptr_t pcs[kMaxStackDepth]{};
};

// Walks a paused thread's stack with the modules' own unwind tables. Plain
// data only, so the MSVC build can guard it with __try.
void WalkStack(CONTEXT* context, RawStack* out) noexcept {
	out->depth = 0;
	for (int frame = 0; frame < kMaxStackDepth; ++frame) {
		const std::uintptr_t pc = static_cast<std::uintptr_t>(context->Rip);
		if (pc == 0) {
			break;
		}
		out->pcs[out->depth++] = pc;

		const ModuleInfo* module = FindModule(pc);
		if (module == nullptr) {
			break; // generated code without unwind data: the walk ends here
		}
		const DWORD64 previousSp = context->Rsp;
		const RUNTIME_FUNCTION* function = module->functions != nullptr
			? FindFunction(*module, static_cast<std::uint32_t>(pc - module->base))
			: nullptr;
		if (function == nullptr) {
			// Leaf code: the return address is on top of the stack.
			context->Rip = *reinterpret_cast<const DWORD64*>(context->Rsp);
			context->Rsp += 8;
		} else {
			void*   handlerData = nullptr;
			DWORD64 frameBase   = 0;
			::RtlVirtualUnwind(UNW_FLAG_NHANDLER, module->base, pc, const_cast<PRUNTIME_FUNCTION>(function),
			                   context, &handlerData, &frameBase, nullptr);
		}
		if (context->Rsp <= previousSp) {
			break; // no progress: stop rather than loop
		}
	}
}

#if defined(_MSC_VER)
void WalkStackGuarded(CONTEXT* context, RawStack* out) noexcept {
	__try {
		WalkStack(context, out);
	} __except (EXCEPTION_EXECUTE_HANDLER) {
		// A frame the unwinder could not read: keep what was collected.
	}
}
#else
void WalkStackGuarded(CONTEXT* context, RawStack* out) noexcept {
	WalkStack(context, out);
}
#endif

// Pauses the thread, copies its registers, walks its stack, resumes it.
// Nothing between the pause and the resume allocates or takes a lock.
auto CaptureThread(HANDLE thread, RawStack* out) noexcept -> bool {
	if (::SuspendThread(thread) == static_cast<DWORD>(-1)) {
		return false;
	}
	alignas(16) CONTEXT context{};
	context.ContextFlags = CONTEXT_CONTROL | CONTEXT_INTEGER;
	const bool captured = ::GetThreadContext(thread, &context) != FALSE;
	if (captured) {
		WalkStackGuarded(&context, out);
	}
	::ResumeThread(thread);
	return captured && out->depth > 0;
}

// --------------------------------------------------------------------------
// Aggregation
// --------------------------------------------------------------------------

struct StackRecord {
	std::uint64_t  count = 0;
	int            depth = 0;
	std::uintptr_t functions[kStackKeyDepth]{};
};

struct ThreadProfile {
	DWORD                                            threadId = 0;
	std::string                                      name;
	std::uint64_t                                    samples  = 0;
	std::uint64_t                                    failures = 0;
	std::unordered_map<std::uintptr_t, std::uint64_t> self;
	std::unordered_map<std::uintptr_t, std::uint64_t> inclusive;
	std::unordered_map<std::uint64_t, StackRecord>    stacks;
};

void Aggregate(ThreadProfile& profile, const RawStack& raw) {
	++profile.samples;

	std::uintptr_t functions[kMaxStackDepth];
	for (int index = 0; index < raw.depth; ++index) {
		// Frames above the innermost one hold return addresses; step back one
		// byte so a call that ends a function is charged to its caller.
		const std::uintptr_t pc = index == 0 ? raw.pcs[index] : raw.pcs[index] - 1;
		functions[index] = FunctionStart(pc);
	}

	++profile.self[functions[0]];
	for (int index = 0; index < raw.depth; ++index) {
		bool seen = false;
		for (int earlier = 0; earlier < index && !seen; ++earlier) {
			seen = functions[earlier] == functions[index];
		}
		if (!seen) {
			++profile.inclusive[functions[index]];
		}
	}

	const int     keyDepth = raw.depth < kStackKeyDepth ? raw.depth : kStackKeyDepth;
	std::uint64_t hash     = 0xCBF29CE484222325ULL;
	for (int index = 0; index < keyDepth; ++index) {
		hash ^= static_cast<std::uint64_t>(functions[index]);
		hash *= 0x100000001B3ULL;
	}
	StackRecord& record = profile.stacks[hash];
	if (record.count == 0) {
		record.depth = keyDepth;
		std::memcpy(record.functions, functions, sizeof(std::uintptr_t) * static_cast<std::size_t>(keyDepth));
	}
	++record.count;
}

// --------------------------------------------------------------------------
// Threads
// --------------------------------------------------------------------------

struct ThreadInfo {
	DWORD         threadId    = 0;
	HANDLE        handle      = nullptr;
	std::string   name;
	ULONG64       cpuStart    = 0; // CpuTime units
	ULONG64       cpuMark     = 0; // at the last busy-thread check
	ULONG64       cpuEnd      = 0;
	bool          present     = true;
};

using GetThreadDescriptionFn = HRESULT(WINAPI*)(HANDLE, PWSTR*);
using NtSetTimerResolutionFn = LONG(NTAPI*)(ULONG, BOOLEAN, PULONG);

auto ThreadName(HANDLE thread) -> std::string {
	static const auto getDescription = reinterpret_cast<GetThreadDescriptionFn>(
		::GetProcAddress(::GetModuleHandleW(L"kernel32.dll"), "GetThreadDescription"));
	if (getDescription == nullptr) {
		return {};
	}
	PWSTR wide = nullptr;
	if (FAILED(getDescription(thread, &wide)) || wide == nullptr) {
		return {};
	}
	char narrow[128]{};
	::WideCharToMultiByte(CP_UTF8, 0, wide, -1, narrow, static_cast<int>(sizeof(narrow)) - 1, nullptr, nullptr);
	::LocalFree(wide);
	return narrow;
}

// CPU time the thread has used so far (user + kernel), in 100 ns units.
auto CpuTime(HANDLE thread) -> ULONG64 {
	FILETIME created{};
	FILETIME exited{};
	FILETIME kernel{};
	FILETIME user{};
	if (!::GetThreadTimes(thread, &created, &exited, &kernel, &user)) {
		return 0;
	}
	const ULONG64 kernelTime = (static_cast<ULONG64>(kernel.dwHighDateTime) << 32) | kernel.dwLowDateTime;
	const ULONG64 userTime   = (static_cast<ULONG64>(user.dwHighDateTime) << 32) | user.dwLowDateTime;
	return kernelTime + userTime;
}

// Adds threads that appeared since the last call and marks the rest.
void RefreshThreads(std::vector<ThreadInfo>& threads, DWORD samplerId) {
	for (ThreadInfo& info : threads) {
		info.present = false;
	}
	HANDLE snapshot = ::CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
	if (snapshot == INVALID_HANDLE_VALUE) {
		return;
	}
	const DWORD   processId = ::GetCurrentProcessId();
	THREADENTRY32 entry{};
	entry.dwSize = sizeof(entry);
	for (BOOL more = ::Thread32First(snapshot, &entry); more; more = ::Thread32Next(snapshot, &entry)) {
		if (entry.th32OwnerProcessID != processId || entry.th32ThreadID == samplerId) {
			continue;
		}
		auto existing = std::find_if(threads.begin(), threads.end(),
		                             [&](const ThreadInfo& info) { return info.threadId == entry.th32ThreadID; });
		if (existing != threads.end()) {
			existing->present = true;
			continue;
		}
		HANDLE handle = ::OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION,
		                             FALSE, entry.th32ThreadID);
		if (handle == nullptr) {
			continue;
		}
		ThreadInfo info{};
		info.threadId    = entry.th32ThreadID;
		info.handle      = handle;
		info.name        = ThreadName(handle);
		info.cpuStart = CpuTime(handle);
		info.cpuMark  = info.cpuStart;
		info.cpuEnd   = info.cpuStart;
		threads.push_back(std::move(info));
	}
	::CloseHandle(snapshot);
}

// --------------------------------------------------------------------------
// Recording state
// --------------------------------------------------------------------------

const D2RL::PluginContext* g_context = nullptr;
Settings                   g_settings{};

volatile LONG g_recording   = 0; // 1 while the sampler thread runs
volatile LONG g_stopRequest = 0;
HANDLE        g_sampler     = nullptr;
DWORD         g_mainThread  = 0; // thread that asked for the recording
int           g_seconds     = 0;
LONG64        g_startQpc    = 0;
volatile LONG g_mainSamples = 0;

SRWLOCK                  g_summaryLock = SRWLOCK_INIT;
std::vector<std::string> g_summary; // headline of the last report, for the console

constexpr const wchar_t* kReportFileName = L"celestialrayone.sampling-profiler.report.txt";

// The report being built. Sampler thread only; written out in one go when the
// recording ends.
std::string g_report;

void Log(const char* line) {
	g_report.append(line);
	g_report.append("\r\n");
}

// Full path of the report file: the folder of this plugin's log plus
// kReportFileName. False when D2RLoader gave no log path.
auto ReportPath(wchar_t* out, std::size_t capacity) noexcept -> bool {
	if (g_context == nullptr || g_context->pluginLogPath == nullptr) {
		return false;
	}
	const wchar_t* logPath = g_context->pluginLogPath;
	std::size_t    folder  = 0; // length up to and including the last separator
	for (std::size_t index = 0; logPath[index] != L'\0'; ++index) {
		if (logPath[index] == L'\\' || logPath[index] == L'/') {
			folder = index + 1;
		}
	}
	const std::size_t nameLength = std::wcslen(kReportFileName);
	if (folder + nameLength + 1 > capacity) {
		return false;
	}
	std::wmemcpy(out, logPath, folder);
	std::wmemcpy(out + folder, kReportFileName, nameLength + 1);
	return true;
}

auto WriteReportFile(const wchar_t* path) noexcept -> bool {
	HANDLE file = ::CreateFileW(path, GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
	if (file == INVALID_HANDLE_VALUE) {
		return false;
	}
	bool        ok     = true;
	std::size_t offset = 0;
	while (ok && offset < g_report.size()) {
		const std::size_t left    = g_report.size() - offset;
		const DWORD       chunk   = static_cast<DWORD>(left < 0x10000000u ? left : 0x10000000u);
		DWORD             written = 0;
		ok = ::WriteFile(file, g_report.data() + offset, chunk, &written, nullptr) != FALSE && written == chunk;
		offset += written;
	}
	::CloseHandle(file);
	return ok;
}

// Hands the finished report over: one file write, plus one line in the plugin
// log pointing at it. If the file cannot be written, the report goes to the
// plugin log line by line instead (slow, but nothing is lost).
void PublishReport() {
	if (g_context == nullptr) {
		return;
	}
	wchar_t path[1024]{};
	if (ReportPath(path, sizeof(path) / sizeof(path[0])) && WriteReportFile(path)) {
		char narrow[1200]{};
		::WideCharToMultiByte(CP_UTF8, 0, path, -1, narrow, static_cast<int>(sizeof(narrow)) - 1, nullptr, nullptr);
		std::string line = "profile: report written to ";
		line += narrow;
		g_context->LogInfo(line.c_str());
		return;
	}
	g_context->LogWarn("profile: could not write the report file, writing the report to this log instead.");
	std::size_t start = 0;
	while (start < g_report.size()) {
		std::size_t end = g_report.find("\r\n", start);
		if (end == std::string::npos) {
			end = g_report.size();
		}
		const std::string line = g_report.substr(start, end - start);
		g_context->LogInfo(line.c_str());
		start = end + 2;
	}
}

auto Percent(std::uint64_t part, std::uint64_t whole) -> double {
	return whole == 0 ? 0.0 : 100.0 * static_cast<double>(part) / static_cast<double>(whole);
}

void WriteTop(const char* title, const std::unordered_map<std::uintptr_t, std::uint64_t>& counts,
              std::uint64_t total, int limit) {
	std::vector<std::pair<std::uintptr_t, std::uint64_t>> sorted(counts.begin(), counts.end());
	std::sort(sorted.begin(), sorted.end(), [](const auto& a, const auto& b) { return a.second > b.second; });

	char line[256];
	std::snprintf(line, sizeof(line), "  %s", title);
	Log(line);
	for (int index = 0; index < limit && index < static_cast<int>(sorted.size()); ++index) {
		char address[96];
		FormatAddress(sorted[static_cast<std::size_t>(index)].first, address, sizeof(address));
		std::snprintf(line, sizeof(line), "    %6.2f%%  %s", Percent(sorted[static_cast<std::size_t>(index)].second, total), address);
		Log(line);
	}
}

void WriteProfile(const ThreadProfile& profile, const Settings& settings) {
	char line[512];
	std::snprintf(line, sizeof(line), "--- thread %lu \"%s\": %llu samples (%llu missed)",
	              static_cast<unsigned long>(profile.threadId), profile.name.c_str(),
	              static_cast<unsigned long long>(profile.samples), static_cast<unsigned long long>(profile.failures));
	Log(line);
	if (profile.samples == 0) {
		return;
	}
	WriteTop("inclusive (share of samples with this function anywhere on the stack):",
	         profile.inclusive, profile.samples, settings.reportFunctions);
	WriteTop("self (share of samples with this function innermost):",
	         profile.self, profile.samples, settings.reportFunctions);

	std::vector<const StackRecord*> stacks;
	stacks.reserve(profile.stacks.size());
	for (const auto& entry : profile.stacks) {
		stacks.push_back(&entry.second);
	}
	std::sort(stacks.begin(), stacks.end(), [](const StackRecord* a, const StackRecord* b) { return a->count > b->count; });

	Log("  call stacks (innermost first):");
	for (int index = 0; index < settings.reportStacks && index < static_cast<int>(stacks.size()); ++index) {
		const StackRecord* stack = stacks[static_cast<std::size_t>(index)];
		std::snprintf(line, sizeof(line), "    %6.2f%%", Percent(stack->count, profile.samples));
		Log(line);
		const int printed = stack->depth < kStackPrintDepth ? stack->depth : kStackPrintDepth;
		for (int frame = 0; frame < printed; ++frame) {
			char address[96];
			FormatAddress(stack->functions[frame], address, sizeof(address));
			std::snprintf(line, sizeof(line), "             %s", address);
			Log(line);
		}
	}
}

// --------------------------------------------------------------------------
// Sampler thread
// --------------------------------------------------------------------------

DWORD WINAPI SamplerMain(void*) {
	const Settings settings = g_settings;
	const DWORD    samplerId = ::GetCurrentThreadId();

	const auto setResolution = reinterpret_cast<NtSetTimerResolutionFn>(
		::GetProcAddress(::GetModuleHandleW(L"ntdll.dll"), "NtSetTimerResolution"));
	ULONG currentResolution = 0;
	if (setResolution != nullptr) {
		setResolution(5000, TRUE, &currentResolution); // 0.5 ms timer ticks while recording
	}

	LARGE_INTEGER frequency{};
	LARGE_INTEGER start{};
	::QueryPerformanceFrequency(&frequency);
	::QueryPerformanceCounter(&start);
	const LONG64 deadline = start.QuadPart + frequency.QuadPart * g_seconds;

	std::vector<ThreadInfo> threads;
	std::unordered_map<DWORD, ThreadProfile> profiles;
	std::vector<DWORD> busy;
	bool outOfMemory = false;

	try {
		LoadModules();
		RefreshThreads(threads, samplerId);

		// D2R names its game thread "Main-Thread". Prefer it; the thread that
		// ran the console command is the fallback.
		for (const ThreadInfo& info : threads) {
			if (info.name == "Main-Thread") {
				g_mainThread = info.threadId;
				break;
			}
		}

		const LONG64 intervalTicks = (std::max)(LONG64{ 1 }, frequency.QuadPart * settings.sampleIntervalUs / 1000000);
		LONG64 nextSample   = start.QuadPart;
		LONG64 nextBusyScan = start.QuadPart + frequency.QuadPart / 4;
		LONG64 nextRefresh  = start.QuadPart + frequency.QuadPart * 2;

		ThreadInfo* mainInfo = nullptr;
		for (ThreadInfo& info : threads) {
			if (info.threadId == g_mainThread) {
				mainInfo = &info;
			}
		}

		while (g_stopRequest == 0) {
			LARGE_INTEGER now{};
			::QueryPerformanceCounter(&now);
			if (now.QuadPart >= deadline) {
				break;
			}
			if (now.QuadPart < nextSample) {
				::Sleep(1);
				continue;
			}
			nextSample = (std::max)(nextSample + intervalTicks, now.QuadPart);

			// Main thread first, then the busiest others.
			std::vector<DWORD> targets;
			targets.push_back(g_mainThread);
			targets.insert(targets.end(), busy.begin(), busy.end());
			for (const DWORD threadId : targets) {
				ThreadInfo* info = nullptr;
				for (ThreadInfo& candidate : threads) {
					if (candidate.threadId == threadId) {
						info = &candidate;
						break;
					}
				}
				if (info == nullptr) {
					continue;
				}
				ThreadProfile& profile = profiles[threadId];
				profile.threadId = threadId;
				if (profile.name.empty()) {
					profile.name = info->name;
				}
				RawStack raw;
				if (CaptureThread(info->handle, &raw)) {
					Aggregate(profile, raw);
					if (threadId == g_mainThread) {
						InterlockedIncrement(&g_mainSamples);
					}
				} else {
					++profile.failures;
				}
			}

			if (now.QuadPart >= nextBusyScan) {
				nextBusyScan = now.QuadPart + frequency.QuadPart / 4;
				std::vector<std::pair<ULONG64, DWORD>> deltas;
				for (ThreadInfo& info : threads) {
					const ULONG64 used = CpuTime(info.handle);
					if (info.threadId != g_mainThread && used > info.cpuMark) {
						deltas.emplace_back(used - info.cpuMark, info.threadId);
					}
					info.cpuMark = used;
				}
				std::sort(deltas.begin(), deltas.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
				busy.clear();
				for (int index = 0; index < settings.extraThreads && index < static_cast<int>(deltas.size()); ++index) {
					busy.push_back(deltas[static_cast<std::size_t>(index)].second);
				}
			}
			if (now.QuadPart >= nextRefresh) {
				nextRefresh = now.QuadPart + frequency.QuadPart * 2;
				RefreshThreads(threads, samplerId);
				mainInfo = nullptr;
				for (ThreadInfo& info : threads) {
					if (info.threadId == g_mainThread) {
						mainInfo = &info;
					}
				}
			}
		}
		(void)mainInfo;
	} catch (const std::bad_alloc&) {
		outOfMemory = true;
	}

	LARGE_INTEGER end{};
	::QueryPerformanceCounter(&end);
	if (setResolution != nullptr) {
		setResolution(5000, FALSE, &currentResolution);
	}

	const double seconds = static_cast<double>(end.QuadPart - start.QuadPart) / static_cast<double>(frequency.QuadPart);

	try {
		for (ThreadInfo& info : threads) {
			info.cpuEnd = CpuTime(info.handle);
		}

		g_report.clear();
		g_report.reserve(256 * 1024);

		char line[512];
		Log("===================== sampling profile =====================");
		std::snprintf(line, sizeof(line), "recorded %.1f s of %d s requested, target interval %d us, main thread %lu%s",
		              seconds, g_seconds, settings.sampleIntervalUs, static_cast<unsigned long>(g_mainThread),
		              outOfMemory ? " (stopped early: out of memory)" : "");
		Log(line);

		// CPU per thread, busiest first.
		std::vector<const ThreadInfo*> sorted;
		for (const ThreadInfo& info : threads) {
			sorted.push_back(&info);
		}
		std::sort(sorted.begin(), sorted.end(), [](const ThreadInfo* a, const ThreadInfo* b) {
			return (a->cpuEnd - a->cpuStart) > (b->cpuEnd - b->cpuStart);
		});
		Log("threads by CPU time during the recording (100% = one full core):");
		for (const ThreadInfo* info : sorted) {
			const double used = seconds > 0.0
				? 100.0 * static_cast<double>(info->cpuEnd - info->cpuStart) / (seconds * 1.0e7)
				: 0.0;
			if (used < 1.0 && info->threadId != g_mainThread) {
				continue;
			}
			const auto profile = profiles.find(info->threadId);
			std::snprintf(line, sizeof(line), "  %6.1f%%  thread %lu \"%s\"%s%s",
			              used, static_cast<unsigned long>(info->threadId), info->name.c_str(),
			              info->threadId == g_mainThread ? "  <- main" : "",
			              profile != profiles.end() && profile->second.samples != 0 ? "  (sampled)" : "");
			Log(line);
		}

		{
			AcquireSRWLockExclusive(&g_summaryLock);
			g_summary.clear();
			std::snprintf(line, sizeof(line), "profile: %.1f s recorded, report in d2rloader/logs/celestialrayone.sampling-profiler.report.txt", seconds);
			g_summary.emplace_back(line);
			const auto mainProfile = profiles.find(g_mainThread);
			if (mainProfile != profiles.end() && mainProfile->second.samples != 0) {
				std::vector<std::pair<std::uintptr_t, std::uint64_t>> self(mainProfile->second.self.begin(),
				                                                            mainProfile->second.self.end());
				std::sort(self.begin(), self.end(), [](const auto& a, const auto& b) { return a.second > b.second; });
				for (std::size_t index = 0; index < self.size() && index < 5; ++index) {
					char address[96];
					FormatAddress(self[index].first, address, sizeof(address));
					std::snprintf(line, sizeof(line), "profile: main thread self %5.1f%%  %s",
					              Percent(self[index].second, mainProfile->second.samples), address);
					g_summary.emplace_back(line);
				}
			}
			ReleaseSRWLockExclusive(&g_summaryLock);
		}

		const auto mainProfile = profiles.find(g_mainThread);
		if (mainProfile != profiles.end()) {
			WriteProfile(mainProfile->second, settings);
		}
		std::vector<const ThreadProfile*> others;
		for (const auto& entry : profiles) {
			if (entry.first != g_mainThread && entry.second.samples != 0) {
				others.push_back(&entry.second);
			}
		}
		std::sort(others.begin(), others.end(), [](const ThreadProfile* a, const ThreadProfile* b) { return a->samples > b->samples; });
		for (const ThreadProfile* profile : others) {
			WriteProfile(*profile, settings);
		}

		Log("modules:");
		for (const ModuleInfo& module : g_modules) {
			std::snprintf(line, sizeof(line), "  %s  0x%llX..0x%llX", module.name,
			              static_cast<unsigned long long>(module.base), static_cast<unsigned long long>(module.end));
			Log(line);
		}
		Log("=================== end of sampling profile ===================");
		PublishReport();
	} catch (const std::bad_alloc&) {
		if (g_context != nullptr) {
			g_context->LogError("profile: out of memory while writing the report.");
		}
	}
	std::string().swap(g_report);

	for (ThreadInfo& info : threads) {
		::CloseHandle(info.handle);
	}
	InterlockedExchange(&g_recording, 0);
	return 0;
}

auto StartRecording(int seconds, DWORD requestingThread) noexcept -> bool {
	if (InterlockedCompareExchange(&g_recording, 1, 0) != 0) {
		return false;
	}
	if (g_sampler != nullptr) {
		::WaitForSingleObject(g_sampler, INFINITE);
		::CloseHandle(g_sampler);
		g_sampler = nullptr;
	}
	g_seconds    = seconds;
	g_mainThread = requestingThread;
	InterlockedExchange(&g_stopRequest, 0);
	InterlockedExchange(&g_mainSamples, 0);
	LARGE_INTEGER now{};
	::QueryPerformanceCounter(&now);
	g_startQpc = now.QuadPart;

	g_sampler = ::CreateThread(nullptr, 0, SamplerMain, nullptr, 0, nullptr);
	if (g_sampler == nullptr) {
		InterlockedExchange(&g_recording, 0);
		return false;
	}
	::SetThreadPriority(g_sampler, THREAD_PRIORITY_TIME_CRITICAL);
	return true;
}

// --------------------------------------------------------------------------
// Config file
// --------------------------------------------------------------------------

constexpr const char* kDefaultConfigToml =
	"# Sampling profiler\n"
	"#\n"
	"# Shows where the game's time goes, without any external tool. While a\n"
	"# recording runs, the game's main thread is paused about once a\n"
	"# millisecond for a few microseconds so its call stack can be read; the\n"
	"# busiest other threads get the same treatment. When the recording ends the\n"
	"# report is written in one go to\n"
	"# d2rloader/logs/celestialrayone.sampling-profiler.report.txt (replaced on\n"
	"# every recording); the plugin log gets one line pointing at it.\n"
	"# Nothing in the game is patched or hooked, and nothing runs while no\n"
	"# recording is in progress.\n"
	"#\n"
	"# Console:\n"
	"#   profile              record for default_seconds, or show the progress\n"
	"#                        of a recording that is running\n"
	"#   profile <seconds>    record for that many seconds (1 to 120)\n"
	"#   profile stop         stop early and write the report\n"
	"#   profile last         the top of the last report\n"
	"#\n"
	"# Type `profile` in the console, close it, reproduce the lag, and send the\n"
	"# report file once the time is up. The game's own \"Main-Thread\" is the main\n"
	"# thread sampled; the thread that ran the command stands in if it has no\n"
	"# such name.\n"
	"\n"
	"[sampling-profiler]\n"
	"\n"
	"# Seconds recorded by `profile` when no number is given.\n"
	"default_seconds = 15\n"
	"\n"
	"# Target time between samples, in microseconds. 1000 is about a thousand\n"
	"# samples a second per sampled thread.\n"
	"sample_interval_us = 1000\n"
	"\n"
	"# Besides the main thread, also sample this many of the busiest other\n"
	"# threads (chosen again four times a second). 0 samples the main thread\n"
	"# only.\n"
	"extra_threads = 2\n"
	"\n"
	"# Report sizes: functions listed per table, and call stacks per thread.\n"
	"report_functions = 40\n"
	"report_stacks = 12\n";

auto FindValue(const char* text, const char* key) noexcept -> const char* {
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
				return after + 1;
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

auto ReadInt(const char* text, const char* key, int fallback, int low, int high) noexcept -> int {
	const char* value = FindValue(text, key);
	if (value == nullptr) {
		return fallback;
	}
	while (*value == ' ' || *value == '\t') {
		++value;
	}
	bool digits = false;
	long parsed = 0;
	while (*value >= '0' && *value <= '9' && parsed < 100000000L) {
		parsed = parsed * 10 + (*value - '0');
		digits = true;
		++value;
	}
	if (!digits || parsed < low || parsed > high) {
		return fallback;
	}
	return static_cast<int>(parsed);
}

void LoadSettings(const D2RL::PluginContext* context) noexcept {
	if (!context->EnsureConfig(kDefaultConfigToml)) {
		context->LogWarn("Could not create the config file, using built in defaults.");
		return;
	}
	char          toml[4096]{};
	std::uint32_t required = 0;
	if (!context->ReadConfig(toml, static_cast<std::uint32_t>(sizeof(toml)), &required)) {
		context->LogWarn("Could not read the config file, using built in defaults.");
		return;
	}
	toml[sizeof(toml) - 1] = '\0';
	g_settings.defaultSeconds   = ReadInt(toml, "default_seconds", g_settings.defaultSeconds, 1, 120);
	g_settings.sampleIntervalUs = ReadInt(toml, "sample_interval_us", g_settings.sampleIntervalUs, 200, 100000);
	g_settings.extraThreads     = ReadInt(toml, "extra_threads", g_settings.extraThreads, 0, 8);
	g_settings.reportFunctions  = ReadInt(toml, "report_functions", g_settings.reportFunctions, 5, 200);
	g_settings.reportStacks     = ReadInt(toml, "report_stacks", g_settings.reportStacks, 0, 100);
}

// --------------------------------------------------------------------------
// Console
// --------------------------------------------------------------------------

auto __cdecl ProfileCommand(D2R::Game::Client*                 client,
                            const D2RL::ConsoleCommandContext* command,
                            void*                              userData) noexcept -> D2RL::ConsoleCommandResult {
	(void)client;
	(void)userData;
	if (command == nullptr || command->plugin == nullptr) {
		return D2RL::ConsoleCommandResult::Failed;
	}

	// First word of the arguments.
	char        argument[32]{};
	std::size_t length = 0;
	if (command->args != nullptr) {
		const char* cursor = command->args;
		const char* end    = command->args + command->argsLength;
		while (cursor < end && (*cursor == ' ' || *cursor == '\t')) {
			++cursor;
		}
		while (cursor < end && *cursor != ' ' && *cursor != '\t' && *cursor != '\0' && length + 1 < sizeof(argument)) {
			argument[length++] = *cursor++;
		}
	}

	char message[256]{};

	if (std::strcmp(argument, "stop") == 0) {
		if (g_recording == 0) {
			command->plugin->WriteConsoleMessage("profile: nothing is recording.");
		} else {
			InterlockedExchange(&g_stopRequest, 1);
			command->plugin->WriteConsoleMessage("profile: stopping; the report goes to d2rloader/logs/celestialrayone.sampling-profiler.report.txt.");
		}
		return D2RL::ConsoleCommandResult::Handled;
	}

	if (std::strcmp(argument, "last") == 0) {
		AcquireSRWLockShared(&g_summaryLock);
		if (g_summary.empty()) {
			command->plugin->WriteConsoleMessage("profile: no report yet.");
		}
		for (const std::string& line : g_summary) {
			command->plugin->WriteConsoleMessage(line.c_str());
		}
		ReleaseSRWLockShared(&g_summaryLock);
		return D2RL::ConsoleCommandResult::Handled;
	}

	if (g_recording != 0) {
		LARGE_INTEGER now{};
		LARGE_INTEGER frequency{};
		::QueryPerformanceCounter(&now);
		::QueryPerformanceFrequency(&frequency);
		std::snprintf(message, sizeof(message), "profile: recording, %.1f s of %d s, %ld main-thread samples.",
		              static_cast<double>(now.QuadPart - g_startQpc) / static_cast<double>(frequency.QuadPart),
		              g_seconds, static_cast<long>(g_mainSamples));
		command->plugin->WriteConsoleMessage(message);
		return D2RL::ConsoleCommandResult::Handled;
	}

	int seconds = g_settings.defaultSeconds;
	if (length != 0) {
		seconds = 0;
		for (std::size_t index = 0; index < length; ++index) {
			if (argument[index] < '0' || argument[index] > '9' || seconds > 120) {
				command->plugin->WriteConsoleMessage("profile: usage is `profile`, `profile <seconds>`, `profile stop` or `profile last`.");
				return D2RL::ConsoleCommandResult::InvalidArguments;
			}
			seconds = seconds * 10 + (argument[index] - '0');
		}
		if (seconds < 1 || seconds > 120) {
			command->plugin->WriteConsoleMessage("profile: pick between 1 and 120 seconds.");
			return D2RL::ConsoleCommandResult::InvalidArguments;
		}
	}

	if (!StartRecording(seconds, ::GetCurrentThreadId())) {
		command->plugin->WriteConsoleMessage("profile: could not start the recording.");
		return D2RL::ConsoleCommandResult::Failed;
	}
	std::snprintf(message, sizeof(message),
	              "profile: recording %d s. Close the console and reproduce the lag; the report goes to d2rloader/logs/celestialrayone.sampling-profiler.report.txt.",
	              seconds);
	command->plugin->WriteConsoleMessage(message);
	return D2RL::ConsoleCommandResult::Handled;
}

// --------------------------------------------------------------------------
// Plugin plumbing
// --------------------------------------------------------------------------

constexpr D2RL::PluginInfo kPluginInfo{
	.infoSize    = D2RL::PluginInfoSize,
	.abiVersion  = D2RL_PLUGIN_ABI_VERSION,
	.id          = "celestialrayone.sampling-profiler",
	.name        = "Sampling Profiler",
	.version     = "1.1.0",
	.author      = "CelestialRayOne",
	.description = "Console-driven sampling profiler: records where the game's threads spend their time and writes the report to its own file in the logs folder.",
	.flags       = D2RL::PluginFlags::Client,
};

} // namespace

D2RL_PLUGIN_EXPORT auto D2RLoaderGetPluginInfo() noexcept -> const D2RL::PluginInfo* {
	return &kPluginInfo;
}

D2RL_PLUGIN_EXPORT auto D2RLoaderLoadPlugin(const D2RL::PluginContext* context) noexcept -> bool {
	if (context == nullptr) {
		return false;
	}
	g_context = context;
	LoadSettings(context);
	if (!context->RegisterConsoleCommand("profile", ProfileCommand,
	                                     "Sampling profiler. `profile [seconds]` records, `profile stop` ends early, `profile last` shows the last result.")) {
		context->LogError("Could not register the `profile` console command.");
		return false;
	}
	context->LogInfo("Sampling profiler loaded. Type `profile` in the console to record.");
	return true;
}

D2RL_PLUGIN_EXPORT void D2RLoaderUnloadPlugin() noexcept {
	if (g_sampler != nullptr) {
		InterlockedExchange(&g_stopRequest, 1);
		::WaitForSingleObject(g_sampler, 10000);
		::CloseHandle(g_sampler);
		g_sampler = nullptr;
	}
}
