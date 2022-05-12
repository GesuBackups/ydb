#pragma once

#include <logbroker/unified_agent/common/config_helpers.h>
#include <logbroker/unified_agent/common/text_expression.h>
#include <logbroker/unified_agent/common/util/f_maybe.h>

#include <util/datetime/base.h>
#include <util/folder/path.h>
#include <util/generic/hash.h>
#include <util/generic/hash_set.h>
#include <util/generic/vector.h>
#include <util/system/type_name.h>
#include <util/string/builder.h>
#include <util/string/printf.h>

namespace NUnifiedAgent {
    namespace NPrivate {
        template <typename T>
        struct TFieldTraits;

        template <typename T, typename=void>
        struct TValueParser;

        template <typename T, typename TOp>
        class TNumericLimitConstraint {
        public:
            TNumericLimitConstraint(T limit, const TString& limitDesc, const char* opSymbol);

            void operator()(const TString& key, const T& value, const YAML::Node& node) const;

        private:
            T Limit;
            TString LimitDesc;
            const char* OpSymbol;
        };

        template <int Sign>
        class TNumericSignConstraint {
        public:
            template <typename T>
            void operator()(const TString& key, const T& value, const YAML::Node& node) const;
        };

        template <typename T>
        using TParserFunc = std::function<bool(const YAML::Node& n, T& result)>;

        template <typename T>
        using TValidatorFunc = std::function<void(const TString& key, const T& value, const YAML::Node& node)>;

        class IValueHandler: public TSimpleRefCount<IValueHandler> {
        public:
            virtual ~IValueHandler() = default;

            inline explicit IValueHandler(const TString& name);

            inline const TString& Name() const;

        private:
            TString Name_;
        };

        template <typename TFunc>
        class TValueHandler: public IValueHandler {
        public:
            TValueHandler(const TString& name, TFunc&& func);

            inline const TFunc& Func() const;

        private:
            TFunc Func_;
        };

        struct TFieldContext {
            bool ScalarAsSequence{false};
            YAML::Node ContainerNode{};
            TVector<TIntrusivePtr<IValueHandler>> ValueHandlers{};

        public:
            template <typename T>
            TValueHandler<TParserFunc<T>>* FindParser() const;

            template <typename TParser, typename T>
            void RegisterParser(const TParser& parser, const TString& name);

            template <typename T>
            inline TVector<TValueHandler<TValidatorFunc<T>>*> GetValidators() const;

            template <typename TValidator, typename T>
            inline void RegisterValidator(const TValidator& validator);

        private:
            template<typename TFunc>
            void RegisterHandler(TFunc&& func, const TString& name);

            template<typename TFunc>
            TVector<TValueHandler<TFunc>*> GetHandlers(size_t expectedCount = 1) const;
        };

        inline void CheckScalarNode(const TString& key, const YAML::Node& node, const TString& typeName);

        TString ParseName(const TString& key, const YAML::Node& node, const YAML::Node& containerNode);

        void CheckNoExpr(const TString& key, const YAML::Node& node);

        bool IsMapWithExprValues(const YAML::Node& node);

        TIntrusivePtr<ITextExpression> ParseExpression(const TString& key,
            const YAML::Node& node,
            const TTextExpressionOptions& options);
    };

#define GEN_LIMIT_CONSTRAINT(name, op, symbol)                                       \
    template <typename T>                                                            \
    class name: public NPrivate::TNumericLimitConstraint<T, op<T>> {                 \
    public:                                                                          \
        name(T limit, const TString& limitDesc)                                      \
            : NPrivate::TNumericLimitConstraint<T, op<T>>(limit, limitDesc, symbol)  \
        {                                                                            \
        }                                                                            \
    }

    GEN_LIMIT_CONSTRAINT(TGt, std::greater, ">");
    GEN_LIMIT_CONSTRAINT(TGe, std::greater_equal, ">=");
    GEN_LIMIT_CONSTRAINT(TLt, std::less, "<");
    GEN_LIMIT_CONSTRAINT(TLe, std::less_equal, "<=");

#undef GEN_LIMIT_CONSTRAINT

    using TPositive = NPrivate::TNumericSignConstraint<1>;
    using TNegative = NPrivate::TNumericSignConstraint<-1>;

    class TNonEmpty {
    public:
        template <typename T>
        void operator()(const TString& key, const T& value, const YAML::Node& node) const;
    };

    template <typename TFieldType>
    struct TConfigField;

    struct TConfigFieldBase {
        inline TConfigFieldBase(const TString& key, const YAML::Node& node, const YAML::Node& keyNode, bool assigned);

        TString Key;
        YAML::Node Node;
        YAML::Node KeyNode;
        bool Assigned;
    };

    template <typename TFieldType>
    struct TConfigField: public TConfigFieldBase {
        inline TConfigField(const TString& key,
            const YAML::Node& node,
            const YAML::Node& keyNode,
            bool assigned,
            TFieldType&& value);

        TFieldType Value;
    };

    struct TGlobalConfigNodeParams {
        TFsPath ConfigFilePath;
    };

    class TConfigNode: public TMoveOnly {
    public:
        struct IParamsScope: public TSimpleRefCount<IParamsScope> {
            virtual ~IParamsScope() = default;
        };

    public:
        explicit TConfigNode(const YAML::Node& node = YAML::Node(YAML::NodeType::Null));

        template <typename T>
        static T Parse(const YAML::Node& node);

        template <typename TParams>
        static inline TIntrusivePtr<IParamsScope> SetParams(TParams&& params);

        static inline void SwitchToRunning() {
            Y_VERIFY(Stage_ == EStage::Configuring);
            Stage_ = EStage::Running;
        }

        inline const YAML::Node& GetNode() const noexcept {
            return Node;
        }

    private:
        template <typename TParams>
        struct TParamsScope: public IParamsScope {
            TParams Params;

            TParamsScope(TParams&& params);

            ~TParamsScope();
        };

        enum class EStage {
            Configuring,
            Running
        };

    protected:
        template <typename TParams>
        static const TParams& GetParams();

    private:
        template <typename TThis>
        inline void DoCheckAfterParse(bool required, bool inplace);

        template <typename T, bool IsInOneOf>
        friend class TConfigFieldBuilder;

        template <typename T, typename>
        friend struct NPrivate::TValueParser;

        void CheckAfterParse(bool isOneOf, bool required, bool inplace) const;

        TString PrintFieldNames() const;

        bool HasAssignedFields() const;

    private:
        YAML::Node Node;
        TVector<TConfigFieldBase> Fields_;
        static thread_local TVector<const IParamsScope*> Params_;
        static inline EStage Stage_{EStage::Configuring};
    };

    struct TOneOf: public TConfigNode {
        using TConfigNode::TConfigNode;
    };

    template <typename T, bool IsInOneOf>
    class TConfigFieldBuilder {
    private:
        using TFieldTraits = NPrivate::TFieldTraits<T>;

        using TValueType = typename TFieldTraits::TValueType;

        using TContentType = typename TFieldTraits::TContentType;

        using TValueParser = typename NPrivate::TValueParser<TValueType>;

        static bool constexpr IsOneOf = std::is_base_of_v<TOneOf, TContentType>;

        static bool constexpr OptionalEnabled = !std::is_base_of_v<TConfigNode, TValueType> ||
                                                std::is_base_of_v<TOneOf, TValueType>;

        static_assert(!IsInOneOf || TFieldTraits::IsNullable,
                      "TOneOf can contain only nullable fields");

    public:
        TConfigFieldBuilder(TConfigNode& container, TString&& containerTypeName, TStringBuf fieldName);

        template <typename TDummy = void,
                  typename = std::enable_if_t<!IsInOneOf && OptionalEnabled, TDummy>>
        inline TConfigFieldBuilder& Optional(TValueType&& defaultValue = TValueType());

        template <typename TDummy = void,
                  typename = std::enable_if_t<!IsInOneOf && OptionalEnabled, TDummy>>
        inline TConfigFieldBuilder& Optional(const TValueType& defaultValue);

        template <typename ...TConstraint>
        TConfigFieldBuilder& Validate(const TConstraint&... constraint);

        template <typename ...TConstraint,
                  typename TDummy = void,
                  typename = std::enable_if_t<TValueParser::IsComposite, TDummy>>
        TConfigFieldBuilder& ValidateScalar(const TConstraint&... constraint);

        inline TConfigFieldBuilder& Key(const TString& key);

        template <
            typename TDummy = void,
            typename = std::enable_if_t<!IsInOneOf && (std::is_base_of_v<TConfigNode, TContentType>), TDummy>>
        inline TConfigFieldBuilder& Inplace();

        inline TConfigFieldBuilder& ScalarAsSequence();

        template <typename TDummy = void,
            typename = std::enable_if_t<std::is_default_constructible_v<T>, TDummy>>
        inline TConfigFieldBuilder& Disable();

        template <class TConfigureFunc>
        inline TConfigFieldBuilder& Configure(TConfigureFunc&& configure);

        template <typename TParams>
        inline TConfigFieldBuilder& SetParams(TParams&& params);

        template <typename TParser>
        inline TConfigFieldBuilder& ParseAs(TParser&& parser = TParser(), const TString& name = "");

        inline const TConfigNode& Container() const;

        inline YAML::Node Node() const;

        TConfigField<T> Build();

    private:
        T ParseValue(const TString& key, const YAML::Node& node);

        TConfigField<T> CreateField();

        template <typename TValidator>
        void RegisterScalarValidator(const TValidator& validator);

    private:
        TConfigNode& Container_;
        const TString ContainerTypeName_;
        TFMaybe<TValueType> DefaultValue_;
        TString Key_;
        NPrivate::TValidatorFunc<TContentType> Validator_;
        bool Disabled_;
        TVector<TIntrusivePtr<TConfigNode::IParamsScope>> Params_;
        NPrivate::TFieldContext FieldContext_;
        bool Inplace_;
    };

    template <typename T, typename TContainer>
    inline auto MakeConfigFieldBuilder(TContainer& container, TStringBuf fieldName);

    template <typename T>
    struct TNameValue {
        TString Name;
        T Value;
    };

    struct TDataSize {
        static inline TString ParserName = "data_size";

        bool operator()(const YAML::Node& n, size_t& result) const;
    };

    struct TNumericEnum {
        static inline TString ParserName = "numeric_enum";

        template <typename TEnum>
        bool operator()(const YAML::Node& n, TEnum& result) const {
            std::underlying_type_t<TEnum> val;
            if (!TryFromString(TStringBuf{n.Scalar()}, val)) {
                return false;
            }
            result = static_cast<TEnum>(val);
            return true;
        }
    };
}

#define SINGLE_ARG(...) typename ::NUnifiedAgent::TArgumentType<void(__VA_ARGS__)>::TType

#define CONFIG_FIELD(type, name, ...)                                                                           \
public:                                                                                                         \
    inline const type& name() const noexcept { return name##_.Value; }                                          \
    inline type& name() noexcept { return name##_.Value; }                                                      \
    inline const ::NUnifiedAgent::TConfigField<type>& name##Field() const noexcept {                            \
        return name##_;                                                                                         \
    }                                                                                                           \
    inline ::NUnifiedAgent::TConfigField<type>& name##Field() noexcept {                                        \
        return name##_;                                                                                         \
    }                                                                                                           \
private:                                                                                                        \
    using name##Type = type;  /* without this using msvc refuses to compile config_node ut  */                  \
    ::NUnifiedAgent::TConfigField<type> name##_{                                                                \
        ::NUnifiedAgent::MakeConfigFieldBuilder<name##Type>(*this, Y_CAT(#name, sv))__VA_ARGS__ .Build()        \
    }


#define CONFIG_NODE_INL_H_
#include "config_node-inl.h"
#undef CONFIG_NODE_INL_H_
