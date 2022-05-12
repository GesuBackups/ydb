#pragma once
// This #pragma once is not needed and is actually adverse, because it prohibits the validation below,
// but I'm sick of fighting with ARC-1133 check during svn-commits.


#ifndef CONFIG_NODE_INL_H_
#error "Direct inclusion of this file is not allowed, include config_node.h"
// For the sake of sane code completion.
#include "config_node.h"
#endif

#include <logbroker/unified_agent/common/common.h>
#include <logbroker/unified_agent/common/tmp.h>
#include <logbroker/unified_agent/common/util/variant.h>

#include <util/generic/hash_set.h>
#include <util/string/subst.h>

namespace NUnifiedAgent {
    namespace NPrivate {
        YAML::Node GetKeyNode(const YAML::Node& node, const TString& key);

        inline void CheckNotNull(const TString& key, const YAML::Node& node) {
            CHECK_NODE(node, !node.IsNull(),
                       Sprintf("field [%s] can't be null", key.c_str()));
        }

        inline void CheckMapNode(const TString& key, const YAML::Node& node) {
            CHECK_NODE(node, node.Type() == YAML::NodeType::Map,
                       Sprintf("field [%s] must be a map", key.c_str()));
        }

        inline void CheckSequenceNode(const TString& key, const YAML::Node& node) {
            CHECK_NODE(node, node.Type() == YAML::NodeType::Sequence,
                       Sprintf("field [%s] must be a sequence", key.c_str()));
        }

        inline void CheckScalarNode(const TString& key, const YAML::Node& node, const TString& typeName) {
            CHECK_NODE(node, node.IsScalar(),
                Sprintf("field [%s], expected type [%s]",
                    key.c_str(),
                    typeName.c_str()));
        }

        inline TString GetMapItemKey(const TString& key, const TString& item) {
            return Sprintf("%s[%s]", key.c_str(), item.c_str());
        }

        template <typename T>
        inline const TValueHandler<TParserFunc<T>> SimpleTypeParser(FormatTypeName<T>(), YAML::convert<T>::decode);

        template <typename T, typename=void>
        struct TTypeRewriteRule;

        template <typename T>
        struct TTypeRewriteRule<T, std::enable_if<std::is_constructible_v<T, TString>>> {
            using TTargetType = TString;
        };

        IValueHandler::IValueHandler(const TString& name)
            : Name_(name)
        {
        }

        const TString& IValueHandler::Name() const {
            return Name_;
        }

        template <typename TFunc>
        TValueHandler<TFunc>::TValueHandler(const TString& name, TFunc&& func)
            : IValueHandler(name)
            , Func_(std::move(func))
        {
        }

        template <typename TFunc>
        const TFunc& TValueHandler<TFunc>::Func() const {
            return Func_;
        }

        template <typename T>
        TValueHandler<TParserFunc<T>>* TFieldContext::FindParser() const {
            auto handlers = GetHandlers<TParserFunc<T>>();
            Y_VERIFY(handlers.size() <= 1);
            return handlers.empty() ? nullptr : handlers[0];
        }

        template <typename TParser, typename T>
        void TFieldContext::RegisterParser(const TParser& parser, const TString& name) {
            Y_VERIFY(FindParser<T>() == nullptr, "parser for [%s] already registered", TypeName<T>().c_str());
            RegisterHandler<TParserFunc<T>>(parser, name);
        }

        template <typename T>
        inline TVector<TValueHandler<TValidatorFunc<T>>*> TFieldContext::GetValidators() const {
            return GetHandlers<TValidatorFunc<T>>();
        }

        template <typename TValidator, typename T>
        inline void TFieldContext::RegisterValidator(const TValidator& validator) {
            RegisterHandler<TValidatorFunc<T>>(validator, "");
        }

        template<typename TFunc>
        void TFieldContext::RegisterHandler(TFunc&& func, const TString& name) {
            ValueHandlers.push_back(MakeIntrusive<TValueHandler<TFunc>>(name, std::move(func)));
        }

        template<typename TFunc>
        TVector<TValueHandler<TFunc>*> TFieldContext::GetHandlers(size_t expectedCount) const {
            TVector<TValueHandler<TFunc>*> result;
            result.reserve(expectedCount);
            for (const auto& h: ValueHandlers) {
                auto* handler = dynamic_cast<TValueHandler<TFunc>*>(h.Get());
                if (handler != nullptr) {
                    result.push_back(handler);
                }
            }
            return result;
        }

        struct TValueContext {
            const TString& Key;
            const YAML::Node& Node;
            bool Required{true};
            bool Inplace{false};
        };

        template <typename T>
        struct TTypeMarker {
        };

        template <typename TVisitor>
        auto MakeUniqueVisitor(const TVisitor& visitor) {
            return [v = std::make_shared<THashSet<std::type_index>>(), &visitor]<typename T>(TTypeMarker<T> m) {
                if (v->insert(std::type_index(typeid(T))).second) {
                    visitor(m);
                }
            };
        }

        template <typename T, typename>
        struct TValueParser {
        private:
            using TValue = T;

        public:
            static constexpr auto IsComposite = false;

        public:
            static T Parse(const TValueContext& valueCtx, const TFieldContext& fieldCtx) {
                CheckNoExpr(valueCtx.Key, valueCtx.Node);

                CheckNotNull(valueCtx.Key, valueCtx.Node);

                const auto* parser = fieldCtx.template FindParser<T>();
                if (parser == nullptr) {
                    parser = &SimpleTypeParser<T>;
                }

                CheckScalarNode(valueCtx.Key, valueCtx.Node, parser->Name());

                T result;
                CHECK_NODE(valueCtx.Node, parser->Func()(valueCtx.Node, result),
                    Sprintf("field [%s], can't parse [%s] as [%s]",
                        valueCtx.Key.c_str(),
                        valueCtx.Node.Scalar().c_str(),
                        parser->Name().c_str()));

                for (auto* v: fieldCtx.template GetValidators<T>()) {
                    v->Func()(valueCtx.Key, result, valueCtx.Node.IsDefined() ? valueCtx.Node : fieldCtx.ContainerNode);
                }

                return result;
            }

            template <typename TVisitor>
            static inline void VisitTypes(const TVisitor& visitor) {
                visitor(TTypeMarker<TValue>{});
            }
        };

        template <>
        struct TValueParser<YAML::Node> {
        private:
            using TValue = YAML::Node;

        public:
            static constexpr auto IsComposite = false;

        public:
            static YAML::Node Parse(const TValueContext& valueCtx, const TFieldContext&) {
                return valueCtx.Node;
            }

            template <typename TVisitor>
            static inline void VisitTypes(const TVisitor& visitor) {
                visitor(TTypeMarker<TValue>{});
            }
        };

        template <typename T>
        struct TValueParser<TFMaybe<T>> {
        private:
            using TNestedParser = TValueParser<T>;

            using TValue = TFMaybe<T>;

        public:
            static constexpr auto IsComposite = TNestedParser::IsComposite;

        public:
            static inline TValue Parse(const TValueContext& valueCtx, const TFieldContext& fieldCtx) {
                if (valueCtx.Node.IsNull()) {
                    return Nothing();
                }

                return TNestedParser::Parse(valueCtx, fieldCtx);
            }

            template <typename TVisitor>
            static inline void VisitTypes(const TVisitor& visitor) {
                TNestedParser::VisitTypes(visitor);
            }
        };

        template <typename... T>
        struct TValueParser<std::variant<T...>> {
        private:
            using TValue = std::variant<T...>;

            using TNestedResult = std::variant<TValue, TConfigurationException>;

            static_assert(sizeof...(T) > 1);

        public:
            static constexpr auto IsComposite = true;

        public:
            static TValue Parse(const TValueContext& valueCtx, const TFieldContext& fieldCtx) {
                std::array<TNestedResult, sizeof...(T)> results{TryParseNested<T>(valueCtx, fieldCtx)...};
                size_t exceptionsCount = 0;
                for (const auto& v: results) {
                    if (holds_alternative<TConfigurationException>(v)) {
                        ++exceptionsCount;
                    }
                }
                CHECK_NODE(valueCtx.Node, exceptionsCount >= sizeof...(T) - 1,
                    Sprintf("field [%s] ambiguous variant type", valueCtx.Key.c_str()));
                CHECK_NODE(valueCtx.Node, exceptionsCount < sizeof...(T),
                    Sprintf("field [%s] variant error %s",
                        valueCtx.Key.c_str(),
                        JoinSeq(", ", results, [](const TNestedResult& r) {
                            return Sprintf("[%s]", get<TConfigurationException>(r).what());
                        }).c_str()));

                for (auto& v: results) {
                    if (holds_alternative<TValue>(v)) {
                        return get<TValue>(std::move(v));
                    }
                }

                Y_FAIL("unreachable");
            }

            template <typename TVisitor>
            static inline void VisitTypes(const TVisitor& visitor) {
                (TValueParser<T>::VisitTypes(visitor), ...);
            }

        private:
            template <typename TNested>
            static TNestedResult TryParseNested(const TValueContext& valueCtx, const TFieldContext& fieldCtx) {
                try {
                    return TValueParser<TNested>::Parse(valueCtx, fieldCtx);
                } catch (const TConfigurationException& e) {
                    return e;
                }
            }
        };

        template <typename T>
        struct TValueParser<TNameValue<T>> {
        private:
            using TNestedParser = TValueParser<T>;

            using TValue = TNameValue<T>;

        public:
            static constexpr auto IsComposite = false;

        public:
            static TValue Parse(const TValueContext& valueCtx, const TFieldContext& fieldCtx) {
                CheckNotNull(valueCtx.Key, valueCtx.Node);

                CheckMapNode(valueCtx.Key, valueCtx.Node);

                CHECK_NODE(valueCtx.Node,
                    valueCtx.Node.size() == 1,
                    Sprintf("field [%s] must have single child", valueCtx.Key.c_str()));

                auto it = valueCtx.Node.begin();
                return TNameValue<T> {
                    ParseName(Sprintf("%s/name", valueCtx.Key.c_str()), it->first, fieldCtx.ContainerNode),
                    TNestedParser::Parse(TValueContext {
                        Sprintf("%s/value", valueCtx.Key.c_str()),
                        it->second
                    }, fieldCtx)
                };
            }

            template <typename TVisitor>
            static inline void VisitTypes(const TVisitor& visitor) {
                TNestedParser::VisitTypes(visitor);
            }
        };

        template <typename T>
        struct TValueParser<THolder<T>> {
        private:
            using TNestedParser = TValueParser<T>;

            using TValue = THolder<T>;

        public:
            static constexpr auto IsComposite = false;

        public:
            static TValue Parse(const TValueContext& valueCtx, const TFieldContext& fieldCtx) {
                static_assert(std::is_base_of_v<TConfigNode, T>);

                return MakeHolder<T>(TNestedParser::Parse(valueCtx, fieldCtx));
            }

            template <typename TVisitor>
            static inline void VisitTypes(const TVisitor& visitor) {
                TNestedParser::VisitTypes(visitor);
            }
        };

        template <typename T>
        struct TValueParser<T, std::void_t<typename TTypeRewriteRule<T>::TTargetType>> {
        private:
            using TRewriteRule = TTypeRewriteRule<T>;

            using TTargetType = typename TRewriteRule::TTargetType;

            using TNestedParser = TValueParser<TTargetType>;

            using TValue = T;

            static_assert(std::is_constructible_v<T, TTargetType>);

        public:
            static constexpr auto IsComposite = TNestedParser::IsComposite;

        public:
            static inline TValue Parse(const TValueContext& valueCtx, const TFieldContext& fieldCtx) {
                return TNestedParser::Parse(valueCtx, fieldCtx);
            }

            template <typename TVisitor>
            static inline void VisitTypes(const TVisitor& visitor) {
                TNestedParser::VisitTypes(visitor);
            }
        };

        template <typename TItem, template <typename... T> class TContainer>
        struct TSequenceContainer {
        private:
            using TNestedParser = TValueParser<TItem>;

            using TValue = TContainer<TItem>;

        public:
            static constexpr auto IsComposite = true;

        public:
            static TValue Parse(const TValueContext& valueCtx, const TFieldContext& fieldCtx) {
                CheckNotNull(valueCtx.Key, valueCtx.Node);

                CheckNoExpr(valueCtx.Key, valueCtx.Node);

                TContainer<TItem> result;

                if (fieldCtx.ScalarAsSequence) {
                    if (valueCtx.Node.Type() == YAML::NodeType::Scalar) {
                        result.insert(result.end(), TNestedParser::Parse({valueCtx.Key, valueCtx.Node}, fieldCtx));
                        return result;
                    }
                    CHECK_NODE(valueCtx.Node, valueCtx.Node.Type() == YAML::NodeType::Sequence,
                               Sprintf("field [%s] must be a scalar or sequence", valueCtx.Key.c_str()));
                } else {
                    CheckSequenceNode(valueCtx.Key, valueCtx.Node);
                }

                const auto size = valueCtx.Node.size();
                result.reserve(size);
                for (size_t i = 0; i < size; ++i) {
                    result.insert(result.end(),
                        TNestedParser::Parse({Sprintf("%s[%lu]", valueCtx.Key.c_str(), i), valueCtx.Node[i]},
                            fieldCtx));
                }
                return result;
            }

            template <typename TVisitor>
            static inline void VisitTypes(const TVisitor& visitor) {
                TNestedParser::VisitTypes(visitor);
            }
        };

        template <typename T>
        struct TValueParser<TVector<T>>: public TSequenceContainer<T, TVector> {
        };

        template <typename T>
        struct TValueParser<THashSet<T>>: public TSequenceContainer<T, THashSet> {
        };

        template <typename TItem>
        struct TValueParser<THashMap<TString, TItem>> {
        private:
            using TValue = THashMap<TString, TItem>;

            using TNestedParser = TValueParser<TItem>;

        public:
            static constexpr auto IsComposite = true;

        public:
            static TValue Parse(const TValueContext& valueCtx, const TFieldContext& fieldCtx) {
                CheckNotNull(valueCtx.Key, valueCtx.Node);

                CheckMapNode(valueCtx.Key, valueCtx.Node);

                CheckNoExpr(valueCtx.Key, valueCtx.Node);

                THashMap<TString, TItem> result;
                for (const auto& p: valueCtx.Node) {
                    const auto itemName = ParseName(valueCtx.Key, p.first, fieldCtx.ContainerNode);
                    Y_VERIFY(result.emplace(itemName,
                        TNestedParser::Parse({GetMapItemKey(valueCtx.Key, itemName), p.second}, fieldCtx)).second);
                }
                return result;
            }

            template <typename TVisitor>
            static inline void VisitTypes(const TVisitor& visitor) {
                TNestedParser::VisitTypes(visitor);
            }
        };

        template <typename T>
        struct TValueParser<T, std::enable_if_t<std::is_base_of_v<TConfigNode, T>>> {
        private:
            using TValue = T;

        public:
            static constexpr auto IsComposite = false;

        public:
            static TValue Parse(const TValueContext& valueCtx, const TFieldContext& fieldCtx) {
                if (valueCtx.Node) {
                    CheckNotNull(valueCtx.Key, valueCtx.Node);

                    CheckMapNode(valueCtx.Key, valueCtx.Node);

                    return CreateInstance(valueCtx);
                }

                try {
                    return CreateInstance({
                        valueCtx.Key,
                        YAML::Node(YAML::NodeType::Map),
                        valueCtx.Required,
                        valueCtx.Inplace
                    });
                } catch (const TConfigurationException&) {
                    throw TConfigurationException(fieldCtx.ContainerNode,
                        Sprintf("field [%s] not set", valueCtx.Key.c_str()));
                }
            }

            template <typename TVisitor>
            static inline void VisitTypes(const TVisitor& visitor) {
                visitor(TTypeMarker<TValue>{});
            }

        private:
            static T CreateInstance(const TValueContext& valueCtx) {
                auto result = T(valueCtx.Node);

                result.template DoCheckAfterParse<T>(valueCtx.Required, valueCtx.Inplace);

                return result;
            }
        };

        template <typename T>
        struct TFieldTraits {
            using TValueType = T;
            using TContentType = T;
            static constexpr auto IsNullable = false;
            static constexpr auto IsExpr = false;
        };

        template <typename T>
        struct TFieldTraits<TFMaybe<T>> {
            using TValueType = TFMaybe<T>;
            using TContentType = T;
            static constexpr auto IsNullable = true;
            static constexpr auto IsExpr = false;
        };

        template <typename T>
        struct TFieldTraits<THolder<T>> {
            using TValueType = THolder<T>;
            using TContentType = T;
            static constexpr auto IsNullable = true;
            static constexpr auto IsExpr = false;
        };

        template <typename T, typename TScope>
        struct TFieldTraits<TTypedExpr<T, TScope>> {
        private:
            using TNestedTraits = TFieldTraits<T>;

        public:
            using TValueType = T;
            using TExprScope = TScope;
            using TContentType = typename TNestedTraits::TContentType;
            static constexpr auto IsNullable = TNestedTraits::IsNullable;
            static constexpr auto IsExpr = true;
        };

        template <typename T>
        inline void RunValidator(const TValidatorFunc<typename TFieldTraits<T>::TContentType>& validator,
            const TString& key,
            const T& value,
            const YAML::Node& node)
        {
            if constexpr (TFieldTraits<T>::IsNullable) {
                if (value) {
                    validator(key, *value, node);
                }
            } else {
                validator(key, value, node);
            }
        }

        template <typename T, typename TOp>
        TNumericLimitConstraint<T, TOp>::TNumericLimitConstraint(T limit,
            const TString& limitDesc,
            const char* opSymbol)
            : Limit(limit)
            , LimitDesc(limitDesc)
            , OpSymbol(opSymbol)
        {
        }

        template <typename T, typename TOp>
        void TNumericLimitConstraint<T, TOp>::operator()(const TString& key, const T& value,
            const YAML::Node& node) const
        {
            CHECK_NODE(node, TOp{}(value, this->Limit),
                Sprintf("[%s] doesn't satisfy the constraint \"[%s] %s [%s] == [%s]\"",
                    ToString(value).c_str(),
                    key.c_str(),
                    OpSymbol,
                    this->LimitDesc.c_str(),
                    ToString(this->Limit).c_str()));
        }

        template <int Sign>
        template <typename T>
        void TNumericSignConstraint<Sign>::operator()(const TString& key, const T& value,
            const YAML::Node& node) const
        {
            const auto sign = value > 0 ? 1 : -1;
            CHECK_NODE(node, sign == Sign,
                Sprintf("[%s] doesn't satisfy the constraint \"[%s] is %s\"",
                    ToString(value).c_str(),
                    key.c_str(),
                    Sign < 0 ? "negative" : "positive"));
        }
    }

    template <typename T, bool IsInOneOf>
    TConfigFieldBuilder<T, IsInOneOf>::TConfigFieldBuilder(TConfigNode& container,
        TString&& containerTypeName,
        TStringBuf fieldName)
        : Container_(container)
        , ContainerTypeName_(std::move(containerTypeName))
        , DefaultValue_(Nothing())
        , Key_(CamelCaseToSnakeCase(fieldName))
        , Validator_()
        , Disabled_(false)
        , Params_()
        , FieldContext_()
        , Inplace_(false)
    {
        const auto& n = FieldContext_.ContainerNode = Container_.Node;
        Y_VERIFY(n && (n.IsMap() || n.IsNull()));
    }

    template <typename T, bool IsInOneOf>
    template <typename, typename>
    auto TConfigFieldBuilder<T, IsInOneOf>::Optional(TValueType&& defaultValue) -> TConfigFieldBuilder& {
        DefaultValue_ = std::move(defaultValue);
        return *this;
    }

    template <typename T, bool IsInOneOf>
    template <typename, typename>
    auto TConfigFieldBuilder<T, IsInOneOf>::Optional(const TValueType& defaultValue) -> TConfigFieldBuilder& {
        DefaultValue_ = defaultValue;
        return *this;
    }

    template <typename T, bool IsInOneOf>
    template <typename... TConstraint>
    auto TConfigFieldBuilder<T, IsInOneOf>::Validate(const TConstraint&... constraint) -> TConfigFieldBuilder& {
        Validator_ = [v = std::move(Validator_), constraint...](const TString& key,
            const TContentType& value,
            const YAML::Node& node)
        {
            if (v != nullptr) {
                v(key, value, node);
            }
            (constraint(key, value, node), ...);
        };
        return *this;
    }

    template <typename T, bool IsInOneOf>
    template <typename... TConstraint, typename, typename>
    auto TConfigFieldBuilder<T, IsInOneOf>::ValidateScalar(const TConstraint&... constraint) -> TConfigFieldBuilder& {
        (RegisterScalarValidator<TConstraint>(constraint), ...);

        return *this;
    }

    template <typename T, bool IsInOneOf>
    template <typename TValidator>
    void TConfigFieldBuilder<T, IsInOneOf>::RegisterScalarValidator(const TValidator& validator) {
        TValueParser::VisitTypes(NUnifiedAgent::NPrivate::MakeUniqueVisitor(
            [&]<typename U>(const NPrivate::TTypeMarker<U>&) {
                FieldContext_.RegisterValidator<TValidator, U>(validator);
            }));
    }

    template <typename T, bool IsInOneOf>
    auto TConfigFieldBuilder<T, IsInOneOf>::Key(const TString& key) -> TConfigFieldBuilder& {
        Key_ = key;
        return *this;
    }

    template <typename T, bool IsInOneOf>
    template <typename, typename>
    auto TConfigFieldBuilder<T, IsInOneOf>::Inplace() -> TConfigFieldBuilder& {
        Inplace_ = true;
        return *this;
    }

    template <typename T, bool IsInOneOf>
    auto TConfigFieldBuilder<T, IsInOneOf>::ScalarAsSequence() -> TConfigFieldBuilder& {
        FieldContext_.ScalarAsSequence = true;
        return *this;
    }

    template <typename T, bool IsInOneOf>
    template <typename, typename>
    auto TConfigFieldBuilder<T, IsInOneOf>::Disable() -> TConfigFieldBuilder& {
        Disabled_ = true;
        return *this;
    }

    template <typename T, bool IsInOneOf>
    template <typename TParams>
    auto TConfigFieldBuilder<T, IsInOneOf>::SetParams(TParams&& params) -> TConfigFieldBuilder& {
        Params_.push_back(TConfigNode::SetParams(std::move(params)));
        return *this;
    }

    template <typename T, bool IsInOneOf>
    template <typename TConfigureFunc>
    auto TConfigFieldBuilder<T, IsInOneOf>::Configure(TConfigureFunc&& configure) -> TConfigFieldBuilder& {
        configure(*this);
        return *this;
    }

    template <typename T, bool IsInOneOf>
    template <typename TParser>
    auto TConfigFieldBuilder<T, IsInOneOf>::ParseAs(TParser&& parser, const TString& name) -> TConfigFieldBuilder& {
        TString defaultParserName;
        if constexpr (IS_VALID(TParser)(_0.ParserName)) {
            defaultParserName = parser.ParserName;
        }

        TValueParser::VisitTypes(NUnifiedAgent::NPrivate::MakeUniqueVisitor(
            [&]<typename U>(const NPrivate::TTypeMarker<U>&) {
                FieldContext_.RegisterParser<TParser, U>(parser, name.Empty() ? defaultParserName : name);
            }));

        return *this;
    }

    template <typename T, bool IsInOneOf>
    const TConfigNode& TConfigFieldBuilder<T, IsInOneOf>::Container() const {
        return Container_;
    }

    template <typename T, bool IsInOneOf>
    YAML::Node TConfigFieldBuilder<T, IsInOneOf>::Node() const {
        return Container_.Node[Key_];
    }

    template <typename T, bool IsInOneOf>
    auto TConfigFieldBuilder<T, IsInOneOf>::ParseValue(const TString& key, const YAML::Node& node) -> T {
        if (node.Tag() == "!expr" || NPrivate::IsMapWithExprValues(node)) {
            if constexpr (TFieldTraits::IsExpr) {
                CHECK_NODE(node, node.IsScalar() || node.IsMap(),
                    Sprintf("field [%s], !expr must be a string", key.c_str()));
                if (node.IsMap()) {
                    NPrivate::CheckNoExpr(key, node);
                }

                const auto options = TTextExpressionOptions {
                    TConfigNode::GetParams<TGlobalConfigNodeParams>().ConfigFilePath,
                    TFieldTraits::TExprScope::PropertyMetaCollection()
                };
                TVector<TIntrusivePtr<ITextExpression>> expressions;
                if (node.IsMap()) {
                    for (const auto& p: node) {
                        if (p.second.Tag() != "!expr") {
                            continue;
                        }
                        const auto itemName = NPrivate::ParseName(key, p.first, FieldContext_.ContainerNode);
                        const auto itemKey = NPrivate::GetMapItemKey(key, itemName);
                        expressions.push_back(NPrivate::ParseExpression(itemKey, p.second, options));
                    }
                } else {
                    expressions.push_back(NPrivate::ParseExpression(key, node, options));
                }

                return T([expressions = std::move(expressions),
                    key,
                    n = node.IsDefined() ? node : Container_.Node,
                    validator = Validator_,
                    fieldCtx = FieldContext_](auto& scope)
                {
                    auto nodeToParse = CloneWithMarks(n);
                    if (nodeToParse.IsMap()) {
                        size_t expressionIndex = 0;
                        for (const auto& p: nodeToParse) {
                            if (p.second.Tag() != "!expr") {
                                continue;
                            }
                            auto mutableNode = p.second;
                            mutableNode = expressions[expressionIndex++]->Eval(scope).c_str();
                            mutableNode.SetTag("");
                        }
                    } else {
                        nodeToParse = expressions[0]->Eval(scope).c_str();
                        nodeToParse.SetTag("");
                    }

                    auto result = TValueParser::Parse({key, nodeToParse, true, false}, fieldCtx);

                    if (validator != nullptr) {
                        NPrivate::RunValidator(validator, key, result, nodeToParse);
                    }

                    return result;
                });
            }
        }

        auto result = [&]() -> TValueType {
            if (!node) {
                if constexpr (IsInOneOf) {
                    return Nothing();
                }

                if constexpr (OptionalEnabled) {
                    if (DefaultValue_.Defined()) {
                        return std::move(*DefaultValue_);
                    }

                    throw TConfigurationException(Container_.Node,
                        Sprintf("field [%s] not set", key.c_str()));
                }
            }

            if constexpr (IsInOneOf) {
                NPrivate::CheckNotNull(key, node);
            }

            auto result = TValueParser::Parse({key, node, !DefaultValue_.Defined(), Inplace_}, FieldContext_);

            if (Validator_ != nullptr) {
                auto n = node.IsDefined() ? node : Container_.Node;
                NPrivate::RunValidator(Validator_, key, result, n);
            }

            if constexpr (IsOneOf && TFieldTraits::IsNullable) {
                if (!result.Defined() || !result->HasAssignedFields()) {
                    return Nothing();
                }
            }

            if constexpr (IsOneOf && std::is_same_v<TContentType, TValueType>) {
                if (!result.HasAssignedFields() && DefaultValue_.Defined()) {
                    return std::move(*DefaultValue_);
                }
            }

            return result;
        }();

        if constexpr (TFieldTraits::IsExpr) {
            return T([v = result](auto&) { return v; });
        } else {
            return result;
        }
    }

    template <typename T, bool IsInOneOf>
    auto TConfigFieldBuilder<T, IsInOneOf>::Build() -> TConfigField<T> {
        auto result = CreateField();
        Container_.Fields_.push_back(result);
        return result;
    }

    template <typename T, bool IsInOneOf>
    auto TConfigFieldBuilder<T, IsInOneOf>::CreateField() -> TConfigField<T> {
        if constexpr (std::is_default_constructible_v<T>) {
            if (Disabled_) {
                return TConfigField<T>("", YAML::Node(), YAML::Node(), false, T{});
            }
        }

        if (Inplace_) {
            return TConfigField<T>("", Container_.Node, YAML::Node(), false,
                ParseValue("", Container_.Node));
        }

        const auto key = Key_;
        const auto keyNode = NPrivate::GetKeyNode(Container_.Node, key);
        const auto node = Node();
        Container_.Node.remove(key);
        return TConfigField<T>(key,
            node.IsDefined() ? node : Container_.Node,
            keyNode,
            node.IsDefined(),
            ParseValue(key, node));
    }

    template <typename T, typename TContainer>
    auto MakeConfigFieldBuilder(TContainer& container, TStringBuf fieldName) {
        return TConfigFieldBuilder<T, std::is_base_of_v<TOneOf, TContainer>>(container,
            TypeName<TContainer>(),
            fieldName);
    }

    template <typename TParams>
    TConfigNode::TParamsScope<TParams>::TParamsScope(TParams&& params)
        : Params(std::move(params))
    {
        Params_.push_back(this);
    }

    template <typename TParams>
    TConfigNode::TParamsScope<TParams>::~TParamsScope() {
        Y_VERIFY(!Params_.empty());
        Y_VERIFY(Params_.back() == this);
        Params_.pop_back();
    }

    template <typename T>
    T TConfigNode::Parse(const YAML::Node& node) {
        CHECK_NODE(node, Stage_ == EStage::Configuring,
            Sprintf("trying to parse [%s] at the wrong stage [%d]",
                TypeName<T>().c_str(), static_cast<int>(Stage_)));

        T result(node);
        result.template DoCheckAfterParse<T>(true, false);
        return result;
    }

    template <typename TParams>
    auto TConfigNode::SetParams(TParams&& params) -> TIntrusivePtr<IParamsScope> {
        return MakeIntrusive<TParamsScope<TParams>>(std::move(params));
    }

    template <typename TParams>
    const TParams& TConfigNode::GetParams() {
        const auto& params = Params_;
        for (size_t i = params.size(); i >= 1; --i) {
            const auto* p = dynamic_cast<const TParamsScope<TParams>*>(params[i - 1]);
            if (p != nullptr) {
                return p->Params;
            }
        }
        static TParams defaultParams{};
        return defaultParams;
    }

    template <typename TThis>
    void TConfigNode::DoCheckAfterParse(bool required, bool inplace) {
        CheckAfterParse(std::is_base_of_v<TOneOf, TThis>, required, inplace);
    }

    template <typename T>
    void TNonEmpty::operator()(const TString& key, const T& value, const YAML::Node& node) const {
        CHECK_NODE(node, !value.empty(), Sprintf("field [%s] can't be empty", key.c_str()));
    }

    TConfigFieldBase::TConfigFieldBase(const TString& key, const YAML::Node& node,
                                       const YAML::Node& keyNode, bool assigned)
        : Key(key)
        , Node(node)
        , KeyNode(keyNode)
        , Assigned(assigned)
    {
    }

    template <typename TFieldType>
    TConfigField<TFieldType>::TConfigField(const TString& key,
        const YAML::Node& node,
        const YAML::Node& keyNode,
        bool assigned,
        TFieldType&& value)
        : TConfigFieldBase(key, node, keyNode, assigned)
        , Value(std::move(value))
    {
    }
}
