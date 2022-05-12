#include "logreader_config.h"

bool TLogreaderConfig::AddKeyValue(Section& sec, const char* key, const char* value) {
    if (strcmp(sec.Name, "AllowedSources") == 0) {
        if (strcmp(key, "Allow") == 0) {
            int source = FromString<int>(value);
            AllowedSources.insert(source);
        } else {
            return false;
        }
    } else {
        ythrow yexception() << "Unknown section " << sec.Name;
    }

    return true;
}

bool TLogreaderConfig::OnBeginSection(Section& sec) {
    sec.Cookie = &Main;
    return true;
}

bool TLogreaderConfig::OnEndSection(Section&) {
    return true;
}

void TLogreaderConfig::Load(const char* filename) {
    if (!Parse(filename)) {
        throw yexception() << "Can't parse logreader config from " << filename;
    }
}

bool TLogreaderConfig::IsAllowedSource(int source) const {
    return AllowedSources.find(source) != AllowedSources.end();
}
