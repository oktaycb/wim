#include "version.h"

#include <string>

#ifndef WIM_VERSION
#define WIM_VERSION "0.0.0"
#endif

namespace wim {

const char* version() { return WIM_VERSION; }

const char* versionString()
{
    static const std::string s = std::string("wim ") + WIM_VERSION;
    return s.c_str();
}

} // namespace wim
