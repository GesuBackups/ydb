#ifndef CODEGEN_MODULE_INL_H_
#error "Direct inclusion of this file is not allowed, include module.h"
// For the sake of sane code completion.
#include "module.h"
#endif
#undef CODEGEN_MODULE_INL_H_

#include <yt/yt/core/actions/bind.h>

namespace NYT::NCodegen {

////////////////////////////////////////////////////////////////////////////////

template <class TSignature>
TCGFunction<TSignature> TCGModule::GetCompiledFunction(const TString& name)
{
    auto type = TTypeBuilder<TSignature>::Get(GetContext());
    YT_VERIFY(type == GetModule()->getFunction(name.c_str())->getFunctionType());
    return TCGFunction<TSignature>(GetFunctionAddress(name), MakeStrong(this));
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NCodegen

