#pragma once

#include <util/system/yassert.h>

#include <exception>

namespace NYT {
namespace NDetail {

template <typename T>
void FinishOrDie(T* pThis, const char* className) noexcept
{
    auto fail = [&] (const char* what) {
        Y_FAIL(
            "\n\n"
            "Destructor of %s caught exception during Finish: %s.\n"
            "Some data is probably has not been written.\n"
            "In order to handle such exceptions consider explicitly call Finish() method.\n",
            className,
            what);
    };

    try {
        pThis->Finish();
    } catch (const std::exception& ex) {
        if (!std::uncaught_exception()) {
            fail(ex.what());
        }
    } catch (...) {
        if (!std::uncaught_exception()) {
            fail("<unkwnown exception>");
        }
    }
}

} // namespace NDetail
} // namespace NYT
