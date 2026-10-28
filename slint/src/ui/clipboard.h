// platform-neutral clipboard helper; the per-platform implementation writes the
// given text to the system clipboard (used to copy the simulation log selection)
#pragma once

#include <string>

void copy_to_clipboard(const std::string& text);
