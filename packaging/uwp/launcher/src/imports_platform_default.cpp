// Platforms without their own import providers add none. The UWP build links
// imports_platform_uwp.cpp instead.
#include "imports.h"

namespace nl {
void add_platform_providers(std::vector<ImportProvider> &) {}
}  // namespace nl
