#pragma once

#include <library/cpp/yconf/conf.h>

#include <library/cpp/deprecated/fgood/fgood.h>
#include <util/generic/set.h>
#include <util/generic/yexception.h>

class TLogreaderConfig : public TYandexConfig {
public:
    class TMain : public TYandexConfig::Directives {
         public: TMain() {}
    };

private:
    TSet<int> AllowedSources;
    TMain Main;

protected:
    bool AddKeyValue(Section& sec, const char* key, const char* value) override;
    bool OnBeginSection(Section& sec) override;
    bool OnEndSection(Section& sec) override;

public:
    TLogreaderConfig() {
    }

    void Load(const char* filename);

    bool IsAllowedSource(int source) const;
};
