#include "clipboard.h"

// Linux clipboard support is not implemented yet; no-op.
void copy_to_clipboard(const std::string& text) { (void)text; }
