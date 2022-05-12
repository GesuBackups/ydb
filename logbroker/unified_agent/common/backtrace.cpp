#include "backtrace.h"

#include <llvm/DebugInfo/Symbolize/Symbolize.h>
#include <llvm/DebugInfo/Symbolize/DIPrinter.h>
#include <llvm/Support/raw_ostream.h>

#include <util/generic/hash.h>
#include <util/system/execpath.h>

#include <dlfcn.h>
#include <link.h>

namespace NUnifiedAgent {
    namespace {
        class TLLVMBacktraceFormatter: public IBacktraceFormatter {
        public:
            explicit TLLVMBacktraceFormatter(IOutputStream& output)
                : BinaryPath(GetPersistentExecPath())
                , Stream(output)
                , Dlls()
                , Symbolyzer(llvm::symbolize::LLVMSymbolizer::Options())
                , Printer(Stream, true, true, false)
            {
                dl_iterate_phdr(DlIterCallback, &Dlls);
            }

            void Format(void* frame) override {
                Printer << GetSymbolAt(frame);
            }

        private:
            static int DlIterCallback(struct dl_phdr_info *info, size_t size, void *data) {
                Y_UNUSED(size);
                Y_UNUSED(data);
                if (*info->dlpi_name) {
                    TDllInfo dllInfo{info->dlpi_name, (ui64)info->dlpi_addr};
                    ((THashMap<TString, TDllInfo>*)data)->emplace(dllInfo.Path, dllInfo);
                }

                return 0;
            }

        private:
            struct TDllInfo {
                TString Path;
                ui64 BaseAddress;
            };

            struct TModuleInfo {
                TString Path;
                ui64 Offset;
            };

            class TRawOStreamProxy: public llvm::raw_ostream {
            public:
                TRawOStreamProxy(IOutputStream& out)
                    : llvm::raw_ostream(true) // unbuffered
                    , Slave_(out)
                {
                }
                void write_impl(const char* ptr, size_t size) override {
                    Slave_.Write(ptr, size);
                }
                uint64_t current_pos() const override {
                    return 0;
                }
                size_t preferred_buffer_size() const override {
                    return 0;
                }
            private:
                IOutputStream& Slave_;
            };

        private:
            const llvm::DILineInfo& GetSymbolAt(void* frame) {
                if (const auto it = SymbolsCache.find(frame)) {
                    return it->second;
                }

                ui64 address = (ui64)frame - 1; // last byte of the call instruction
                const auto moduleInfo = GetModuleInfo(address);

                llvm::symbolize::SectionedAddress secAddr;
                secAddr.Address = address - moduleInfo.Offset;
                auto resOrErr = Symbolyzer.symbolizeCode(moduleInfo.Path, secAddr);
                if (!resOrErr) {
                    throw yexception() << "can't symbolize symbol at " << frame;
                }
                auto value = resOrErr.get();
                if (value.FileName == "<invalid>" && moduleInfo.Offset > 0) {
                    value.FileName = moduleInfo.Path;
                }

                return SymbolsCache.insert({frame, std::move(value)}).first->second;
            }

            TModuleInfo GetModuleInfo(ui64 address) {
                TModuleInfo result{BinaryPath, 0};

                Dl_info dlInfo;
                memset(&dlInfo, 0, sizeof(dlInfo));
                auto ret = dladdr((void*)address, &dlInfo);
                if (ret) {
                    const auto path = dlInfo.dli_fname;
                    const auto it = Dlls.find(path);
                    if (it != Dlls.end()) {
                        result.Path = path;
                        result.Offset = it->second.BaseAddress;
                    }
                }
                return result;
            }

        public:
            TString BinaryPath;
            TRawOStreamProxy Stream;
            THashMap<TString, TDllInfo> Dlls;
            THashMap<void*, llvm::DILineInfo> SymbolsCache;
            llvm::symbolize::LLVMSymbolizer Symbolyzer;
            llvm::symbolize::DIPrinter Printer;
        };
    }

    THolder<IBacktraceFormatter> MakeBacktraceFormatter(IOutputStream& output) {
        return MakeHolder<TLLVMBacktraceFormatter>(output);
    }
}
