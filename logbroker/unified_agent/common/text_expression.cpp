#include "text_expression.h"

#include <logbroker/unified_agent/common/common.h>

#include <util/folder/path.h>
#include <util/generic/variant.h>
#include <util/system/env.h>
#include <util/system/hostname.h>
#include <util/stream/file.h>
#include <util/string/printf.h>

namespace NUnifiedAgent {
    namespace {
        struct TWithDefaultValue {
            TString DefaultValue;
        };

        struct TLiteral {
            TString Value;
        };

        struct TPropertyMacro: public TWithDefaultValue {
            size_t PropertyIndex;
        };

        struct TFuncMacro: public TWithDefaultValue {
            using TSignature = TFMaybe<TString>(const TString& name);

            size_t PropertyIndex;
            TSignature* TryResolve;
        };

        using TFormatItem = std::variant<
            TLiteral,
            TPropertyMacro,
            TFuncMacro>;

        class TFormatItemVisitor {
        public:
            TFormatItemVisitor(const ITextExpression::TPropertyValues& propertyValues, TStringOutput& output)
                : PropertyValues(propertyValues)
                , Output(output)
            {
            }

            void operator()(const TLiteral& item) const {
                Output << item.Value;
            }

            void operator()(const TPropertyMacro& item) const {
                const auto& v = PropertyValues[item.PropertyIndex];
                if (holds_alternative<std::monostate>(v)) {
                    Output << item.DefaultValue;
                } else if (holds_alternative<TStringBuf>(v)) {
                    Output << get<TStringBuf>(v);
                } else {
                    Output << get<TString>(v);
                }
            }

            void operator()(const TFuncMacro& item) const {
                const auto& v = PropertyValues[item.PropertyIndex];
                if (holds_alternative<std::monostate>(v)) {
                    Output << item.DefaultValue;
                } else {
                    const auto& s = holds_alternative<TString>(v)
                        ? get<TString>(v)
                        : TString(get<TStringBuf>(v));
                    Output << item.TryResolve(s).GetOrElse(item.DefaultValue);
                }
            }

        private:
            const ITextExpression::TPropertyValues& PropertyValues;
            TStringOutput& Output;
        };

        class TExpressionParser {
        public:
            explicit TExpressionParser(const TString& pattern, const TTextExpressionOptions& options)
                : Options(options)
                , FormatItems()
                , Properties()
                , State(Literal)
                , Begin(pattern.cbegin())
                , End(pattern.cend())
                , Current(Begin)
                , LiteralStart(Begin)
                , NameStart(nullptr)
                , NameFinish(nullptr)
                , DefaultValueStart(nullptr)
                , DefaultValueFinish(nullptr)
                , FormatStart(nullptr)
                , FormatFinish(nullptr)
            {
            }

            void Parse() {
                for (; Current != End; ++Current) {
                    switch (State) {
                        case Literal: {
                            if (OpenMacroToken()) {
                                FinishLiteral();
                                NameStart = Current + 1;
                                State = ItemName;
                            }
                            break;
                        }
                        case ItemName: {
                            CheckNoOpenMacroToken();
                            if (*Current == '|') {
                                NameFinish = Current;
                                DefaultValueStart = Current + 1;
                                State = ItemDefaultValue;
                            } else if (*Current == ':') {
                                NameFinish = Current;
                                FormatStart = Current + 1;
                                State = ItemFormat;
                            } else if (CloseMacroToken()) {
                                NameFinish = Current;
                                FinishMacro();
                            }
                            break;
                        }
                        case ItemFormat: {
                            CheckNoOpenMacroToken();
                            if (*Current == '|') {
                                FormatFinish = Current;
                                DefaultValueStart = Current + 1;
                                State = ItemDefaultValue;
                            } else if (CloseMacroToken()) {
                                FormatFinish = Current;
                                FinishMacro();
                            }
                            break;
                        }
                        case ItemDefaultValue: {
                            CheckNoOpenMacroToken();
                            if (CloseMacroToken()) {
                                DefaultValueFinish = Current;
                                FinishMacro();
                            }
                            break;
                        }
                    }
                }
                if (State != Literal) {
                    throw TInvalidFormatException()
                        << Sprintf("position %lu, missing closing '}'", Current == Begin ? 0 : Current - Begin - 1);
                } else {
                    FinishLiteral();
                }
            }

            TVector<TFormatItem> TakeFormatItems() {
                return std::move(FormatItems);
            }

            TVector<ITextExpression::TProperty> TakeProperties() {
                return std::move(Properties);
            }

        private:
            enum EState {
                Literal,
                ItemName,
                ItemFormat,
                ItemDefaultValue
            };

        private:
            TFormatItem CreateFormatItem(TString name) {
                if (name.StartsWith("$")) {
                    if (name == "$host_name") {
                        CheckNoDefaultValue("function", "$host_name");
                        return TLiteral{GetHostName(EHostNameStyle::Long)};
                    }
                    if (name == "$short_host_name") {
                        CheckNoDefaultValue("function", "$short_host_name");
                        return TLiteral{GetHostName(EHostNameStyle::Short)};
                    }
                    if (name == "$fqdn_host_name") {
                        CheckNoDefaultValue("function", "$fqdn_host_name");
                        return TLiteral{FQDNHostName()};
                    }
                    if (name == "$config_dir") {
                        CheckNoDefaultValue("function", "$config_dir");
                        return TLiteral{TFsPath(Options.ConfigFilePath).Parent().RealPath()};
                    }
                    TFormatItem result;
                    if (TryParseFuncCall("env", name, TryResolveEnv, result)) {
                        return result;
                    }
                    if (TryParseFuncCall("file", name, TryResolveFile, result)) {
                        return result;
                    }
                    throw TInvalidFormatException()
                        << Sprintf("position %lu, unknown function [%s]",
                                   NameStart - Begin, name.c_str());
                } else if (name.StartsWith("\\$")) {
                    name = name.substr(1);
                }

                const auto* propertyMeta = GetPropertyMeta(name);
                if (propertyMeta != nullptr && !propertyMeta->HasDefaultValue) {
                    CheckNoDefaultValue("property", name.c_str());
                }
                TString format;
                {
                    const bool hasFormat = (propertyMeta != nullptr) && propertyMeta->HasFormat;
                    if (FormatStart) {
                        Y_VERIFY(FormatFinish);
                        if (!hasFormat) {
                            throw TInvalidFormatException()
                                << Sprintf("position %lu, can't apply format spec to [%s] property",
                                           FormatStart - Begin, name.c_str());
                        }
                        format = GetToken(FormatStart, FormatFinish);
                        FormatStart = nullptr;
                        FormatFinish = nullptr;
                    } else if (hasFormat) {
                        throw TInvalidFormatException()
                            << Sprintf("position %lu, expected format spec for [%s] property",
                                NameStart - Begin,
                                name.c_str());
                    }
                }
                Properties.push_back({name, propertyMeta, format});
                return TPropertyMacro{{PickUpDefaultValue()}, Properties.size() - 1};
            }

            const ITextExpression::TPropertyMeta* GetPropertyMeta(const TString& name) {
                if (Options.Properties == nullptr) {
                    return nullptr;
                }
                const auto& properties = *Options.Properties;
                const auto it = FindIf(properties, [&](const auto* f) {
                    return f->Name == name;
                });
                return it == end(properties) ? nullptr : *it;
            }

            void FinishMacro() {
                Y_VERIFY(NameStart && NameFinish);
                if (NameFinish == NameStart) {
                    throw TInvalidFormatException()
                        << Sprintf("position %lu, empty item name", NameStart - Begin);
                }
                FormatItems.push_back(CreateFormatItem(GetToken(NameStart, NameFinish)));
                NameStart = nullptr;
                NameFinish = nullptr;
                State = Literal;
                LiteralStart = Current + 1;
            }

            void FinishLiteral() {
                Y_VERIFY(LiteralStart);
                if (LiteralStart != Current) {
                    FormatItems.push_back(TLiteral{GetToken(LiteralStart, Current)});
                }
                LiteralStart = nullptr;
            }

            bool Escaped(const char* c) const {
                return c != Begin && *(c - 1) == '\\';
            }

            bool OpenMacroToken() const {
                return *Current == '{' && !Escaped(Current);
            }

            bool CloseMacroToken() const {
                return *Current == '}' && !Escaped(Current);
            }

            void CheckNoOpenMacroToken() const {
                if (OpenMacroToken()) {
                    throw TInvalidFormatException()
                        << Sprintf("position %lu, nested '{' is not allowed", Current - Begin);
                }
            }

            TString GetToken(const char* begin, const char* end) {
                TString result;
                result.reserve(end - begin);
                for (const char* c = begin; c != end; ++c) {
                    const bool skip = *c == '\\' && c != end - 1 && (c[1] == '{' || c[1] == '}');
                    if (!skip) {
                        result.append(*c);
                    }
                }
                return result;
            }

            TString PickUpDefaultValue() {
                if (!DefaultValueStart) {
                    return "";
                }
                Y_VERIFY(DefaultValueFinish);
                const auto result = GetToken(DefaultValueStart, DefaultValueFinish);
                DefaultValueStart = nullptr;
                DefaultValueFinish = nullptr;
                return result;
            }

            bool TryParseFuncCall(const char* funcName, const TString& macroName,
                                  TFuncMacro::TSignature* tryResolve, TFormatItem& result)
            {
                const auto prefix = TString("$") + funcName + "(";
                const auto suffix = TString(")");
                if (!macroName.StartsWith(prefix) || !macroName.EndsWith(suffix)) {
                    return false;
                }
                const auto argument = macroName.substr(prefix.size(), macroName.size() - prefix.size() - suffix.size());
                if (argument.Size() > 2 &&
                    ((argument.StartsWith('\'') && argument.EndsWith('\'')) ||
                     (argument.StartsWith('\"') && argument.EndsWith('\"'))))
                {
                    const auto name = argument.substr(1, argument.size() - 2);
                    result = TLiteral{tryResolve(name).GetOrElse(PickUpDefaultValue())};
                } else {
                    Properties.push_back({argument, nullptr, ""});
                    result = TFuncMacro{{PickUpDefaultValue()}, Properties.size() - 1, tryResolve};
                }
                return true;
            }

            static TFMaybe<TString> TryResolveEnv(const TString& name) {
                auto result = GetEnv(name);
                return result.Empty() ? Nothing() : MakeFMaybe(std::move(result));
            }

            static TFMaybe<TString> TryResolveFile(const TString& name) {
                return TFsPath(name).Exists() ? MakeFMaybe(TFileInput(name).ReadAll()) : Nothing();
            }

            void CheckNoDefaultValue(const char* object, const char* name) {
                if (DefaultValueStart) {
                    throw TInvalidFormatException()
                        << Sprintf("position %lu, %s %s is always defined, "
                                   "default makes no sense for it",
                                   DefaultValueStart - Begin,
                                   name,
                                   object);
                }
            }

        private:
            const TTextExpressionOptions& Options;
            TVector<TFormatItem> FormatItems;
            TVector<ITextExpression::TProperty> Properties;
            EState State;
            const char* Begin;
            const char* End;
            const char* Current;
            const char* LiteralStart;
            const char* NameStart;
            const char* NameFinish;
            const char* DefaultValueStart;
            const char* DefaultValueFinish;
            const char* FormatStart;
            const char* FormatFinish;
        };

        class TTextExpression: public ITextExpression {
        public:
            TTextExpression(TVector<TProperty>&& properties, TVector<TFormatItem>&& formatItems)
                : ITextExpression(std::move(properties))
                , FormatItems(std::move(formatItems))
            {
            }

            TString Eval(const TPropertyValues& propertyValues) const override {
                Y_VERIFY(propertyValues.size() == Properties().size(),
                    "[%lu] == [%lu]",
                    propertyValues.size(), Properties().size());
                TString result;
                {
                    TStringOutput output(result);
                    TFormatItemVisitor visitor(propertyValues, output);
                    for (const auto& item: FormatItems) {
                        std::visit(visitor, item);
                    }
                }
                return result;
            }

        private:
            const TVector<TFormatItem> FormatItems;
        };
    }

    ITextExpression::ITextExpression(TVector<TProperty>&& properties)
        : Properties_(std::move(properties))
    {
    }

    TIntrusivePtr<ITextExpression> ParseTextExpression(const TString& pattern, const TTextExpressionOptions& options) {
        TExpressionParser parser(pattern, options);
        parser.Parse();
        return MakeIntrusive<TTextExpression>(parser.TakeProperties(), parser.TakeFormatItems());
    }
}
