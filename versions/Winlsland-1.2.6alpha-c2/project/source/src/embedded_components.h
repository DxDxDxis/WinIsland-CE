#pragma once
#include "core.h"
namespace wi {
// Returns the immutable component directory selected for this process.
fs::path lyricProviderDirectory();
// Extracts the embedded component bundle when needed. Diagnostic/test runs do
// not touch the user's installation record.
bool prepareEmbeddedComponents(bool diagnostic = false);
int embeddedComponentsTest(const fs::path& output);
}
