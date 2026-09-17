#pragma once
#include "core.h"
namespace wi {
// Returns the immutable component directory selected for this process.
fs::path lyricProviderDirectory();
fs::path settingsRuntimeDirectory();
// Test-only command explicitly redirects bootstrap and known legacy locations.
void setInstallerTestHome(const fs::path& path);
// Extracts the embedded component bundle when needed. Diagnostic/test runs do
// not touch the user's installation record.
bool prepareEmbeddedComponents(bool diagnostic = false);
int storagePathTest(const fs::path& output);
int embeddedComponentsTest(const fs::path& output);
}

