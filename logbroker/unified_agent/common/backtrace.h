#pragma once

#include <util/generic/ptr.h>

namespace NUnifiedAgent {
    class IBacktraceFormatter {
    public:
        virtual ~IBacktraceFormatter() = default;

        virtual void Format(void* frame) = 0;
    };

    THolder<IBacktraceFormatter> MakeBacktraceFormatter(IOutputStream& output);
}
