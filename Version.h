#pragma once

#define WEBS_VERSION_MAJOR 2
#define WEBS_VERSION_MINOR 0
#define WEBS_VERSION_PATCH 2
#define WEBS_VERSION_BUILD 1

#define WEBS_STRINGIFY2(x) #x
#define WEBS_STRINGIFY(x) WEBS_STRINGIFY2(x)

#define WEBS_VERSION_STRING \
    WEBS_STRINGIFY(WEBS_VERSION_MAJOR) "." \
    WEBS_STRINGIFY(WEBS_VERSION_MINOR) "." \
    WEBS_STRINGIFY(WEBS_VERSION_PATCH)

#define WEBS_VERSION_RC \
    WEBS_VERSION_MAJOR,WEBS_VERSION_MINOR,WEBS_VERSION_PATCH,WEBS_VERSION_BUILD

#ifdef __cplusplus
namespace WebS {
    constexpr int VersionMajor = WEBS_VERSION_MAJOR;
    constexpr int VersionMinor = WEBS_VERSION_MINOR;
    constexpr int VersionPatch = WEBS_VERSION_PATCH;
    constexpr const char* Version = WEBS_VERSION_STRING;
}
#endif
