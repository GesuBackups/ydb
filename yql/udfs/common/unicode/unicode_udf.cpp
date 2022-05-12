#include <ydb/library/yql/public/udf/udf_allocator.h>
#include <ydb/library/yql/public/udf/udf_helpers.h>
#include <ydb/library/yql/udfs/common/unicode_base/lib/unicode_base_udf.h>

#include <kernel/lemmer/core/language.h>
#include <library/cpp/unicode/folding/fold.h>

using namespace NYql;
using namespace NUdf;
using namespace NLemmer;

namespace {
    const TLanguage* GetLanguage(const TUnboxedValuePod& value) {
        if (!value) {
            return GetLanguageById(LANG_RUS);
        } else {
            const TString name(value.AsStringRef());
            const TLanguage* language = GetLanguageByName(name.c_str());
            if (!language) {
                ythrow yexception() << "Unsupported language: " << name;
            }
            return language;
        }
    }

    inline bool GetFlag(const TUnboxedValuePod& value, bool defaultValue) {
        return value.GetOrDefault<bool>(defaultValue);
    }

    extern const char language[] = "Language";
    extern const char doLowerCase[] = "DoLowerCase";
    extern const char doRenyxa[] = "DoRenyxa";
    extern const char doSimpleCyr[] = "DoSimpleCyr";
    extern const char fillOffset[] = "FillOffset";

    using TLanguage = TNamedArg<char*, language>;
    using TDoLowerCase = TNamedArg<bool, doLowerCase>;
    using TDoRenyxa = TNamedArg<bool, doRenyxa>;
    using TDoSimpleCyr = TNamedArg<bool, doSimpleCyr>;
    using TFillOffset = TNamedArg<bool, fillOffset>;

    SIMPLE_UDF_OPTIONS(TFold, TUtf8(TAutoMap<TUtf8>, TLanguage, TDoLowerCase, TDoRenyxa, TDoSimpleCyr, TFillOffset),
                       builder.OptionalArgs(5)) {
        const auto& inputRef = args[0].AsStringRef();
        const TUtf16String& input = UTF8ToWide(inputRef.Data(), inputRef.Size());
        ui8 language = GetLanguage(args[1])->Id;
        NUF::TNormalizer n(language);
        n.SetInput(input);
        n.SetDoLowerCase(GetFlag(args[2], true));
        n.SetDoRenyxa(GetFlag(args[3], true));
        n.SetDoSimpleCyr(GetFlag(args[4], true));
        n.SetFillOffsets(GetFlag(args[5], true));
        n.DoNormalize();
        const TString& output = WideToUTF8(n.GetOutput());
        return valueBuilder->NewString(output);
    }

    SIMPLE_UDF_OPTIONS(TTranslit, TUtf8(TAutoMap<TUtf8>, TOptional<char*>),
                       builder.OptionalArgs(1)) {
        const TStringRef input = args[0].AsStringRef();
        TStringBuilder result;
        TAutoPtr<TUntransliter> transliter = GetLanguage(args[1])->GetTransliter();
        Y_ENSURE(transliter, "No transliter");

        TUtf32String utf32Input = UTF8ToUTF32<false>(input);
        for (size_t start = 0; start < utf32Input.size(); ) {
            size_t curr = start;
            while (curr < utf32Input.size() && IsAlpha(utf32Input[curr])) {
                curr++;
            }

            if (curr > start) {
                auto chunk = UTF32ToWide(utf32Input.data() + start, curr - start);
                transliter->Init(chunk);
                const auto& wp = transliter->GetNextAnswer();
                if (!wp.Empty()) {
                    result << WideToUTF8(wp.GetWord());
                } else {
                    result << WideToUTF8(chunk);
                }
                start = curr;
            }

            while (curr < utf32Input.size() && !IsAlpha(utf32Input[curr])) {
                curr++;
            }

            if (curr > start) {
                result << WideToUTF8(UTF32ToWide(utf32Input.data() + start, curr - start));
                start = curr;
            }
        }

        return valueBuilder->NewString(result);
    }

    SIMPLE_MODULE(TUnicodeModule,
                  EXPORTED_UNICODE_BASE_UDF,
                  TFold,
                  TTranslit)
}

REGISTER_MODULES(TUnicodeModule)
