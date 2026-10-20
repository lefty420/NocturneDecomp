// Non-UWP builds have no platform importer; the launcher falls back to the folder service.
#include "importer.h"

namespace nl {
std::unique_ptr<ImportService> make_platform_import_service(const std::string &) { return nullptr; }
}  // namespace nl
