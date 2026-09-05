#pragma once
//
// Shared helpers for ESR plugins. Empty for now: the SDK's PluginContext
// already provides logging, config, memory patches and inline hooks, so
// there is nothing to wrap yet. Put genuinely shared code here as it appears.
//
#include <D2RLPlugin/api.h>

namespace esr {

// Placeholder so the static library has a symbol and links cleanly.
auto SdkApiVersion() noexcept -> unsigned;

}  // namespace esr
