#pragma once

#include <util/folder/path.h>

#include <variant>

namespace NUnifiedAgent {
    class TInvalidFormatException : public yexception {
    };

    class ITextExpression: public TAtomicRefCount<ITextExpression> {
    public:
        struct TPropertyMeta {
            TString Name;
            bool HasFormat;
            bool HasDefaultValue;
        };

        struct TProperty {
            TString Name;
            const TPropertyMeta* Meta;
            TString Format;
        };

        using TPropertyValue = std::variant<std::monostate, TStringBuf, TString>;

        using TPropertyValues = TVector<TPropertyValue>;

        using TPropertyMetaCollection = TVector<const TPropertyMeta*>;

        class TScopeBase {
        protected:
            using TProperty = TProperty;

            using TPropertyValue = TPropertyValue;

            using TPropertyValues = TPropertyValues;

            using TPropertyMetaCollection = TPropertyMetaCollection;

        protected:
            inline explicit TScopeBase(TPropertyValues& buffer)
                : Buffer_(buffer)
            {
            }

        public:
            inline TPropertyValues& Buffer() const {
                return Buffer_;
            }

        protected:
            TPropertyValues& Buffer_;
        };

    public:
        explicit ITextExpression(TVector<TProperty>&& properties);

        virtual ~ITextExpression() = default;

        inline const TVector<TProperty>& Properties() const {
            return Properties_;
        }

        virtual TString Eval(const TPropertyValues& propertyValues) const = 0;

        template <typename TScope,
            typename = std::enable_if_t<std::is_base_of_v<TScopeBase, TScope>>>
        inline TString Eval(const TScope& scope) const {
            return Eval(scope.Buffer(), [&](const TProperty& p) { return scope.GetValue(p); });
        }

        template <typename TGetter>
        inline TString Eval(TPropertyValues& buffer, const TGetter& getter) const {
            const auto count = Properties().size();
            buffer.resize(count);
            for (size_t i = 0; i < count; ++i) {
                buffer[i] = getter(Properties()[i]);
            }
            return Eval(buffer);
        }

    private:
        const TVector<TProperty> Properties_;
    };

    struct TTextExpressionOptions {
        TFsPath ConfigFilePath;
        const ITextExpression::TPropertyMetaCollection* Properties{nullptr};
    };

    TIntrusivePtr<ITextExpression> ParseTextExpression(const TString& pattern, const TTextExpressionOptions& options);

    template <typename T, typename TExprScope>
    class TTypedExpr {
    public:
        using TEvalFunc = std::function<T(TExprScope&)>;

    public:
        inline explicit TTypedExpr(TEvalFunc&& eval)
            : EvalFunc(std::move(eval))
        {
        }

        inline T Eval(TExprScope& scope) const {
            return EvalFunc(scope);
        }

    private:
        TEvalFunc EvalFunc;
    };
}
