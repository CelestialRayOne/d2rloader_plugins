#pragma once
//
// Shared helpers for the plugins in this repository. Empty for now: the SDK's PluginContext
// already provides logging, config, memory patches and inline hooks, so
// there is nothing to wrap yet. Put genuinely shared code here as it appears.
//
#include <D2RLPlugin/api.h>

namespace common {

// Placeholder so the static library has a symbol and links cleanly.
auto SdkApiVersion() noexcept -> unsigned;

}  // namespace common
