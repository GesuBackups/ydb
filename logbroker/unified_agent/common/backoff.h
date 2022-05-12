#pragma once

#include <util/datetime/base.h>

namespace NUnifiedAgent {
    class IBackoff {
    public:
        virtual ~IBackoff() = default;

        virtual TDuration Next() = 0;

        virtual void Reset() = 0;
    };

    class TExpBackoff: public IBackoff {
    public:
        TExpBackoff(TDuration min, TDuration max);

        TDuration Next() override;

        void Reset() override;

    private:
        const TDuration Min_;
        const TDuration Max_;
        TDuration Current_;
    };
}
