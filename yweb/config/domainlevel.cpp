#include "domainlevel.h"

ui32 GetDomainLevel(const char* name) {
    if (IsMultilanguageHost(name))
        name = DecodeMultilanguageHost(name);
    const char* dot = strchr(name, '.');
    ui32 cnt = 1;
    while (dot != nullptr) {
        dot = strchr(dot+1, '.');
        ++cnt;
    }
    return cnt;
}
