#pragma once

#include <logbroker/unified_agent/common/datetime.h>

#include <contrib/libs/yaml-cpp/include/yaml-cpp/yaml.h>

#include <util/datetime/base.h>
#include <util/string/builder.h>
#include <util/string/cast.h>
#include <util/string/printf.h>

namespace NUnifiedAgent {
    template <typename T>
    bool TryParseScalar(TStringBuf s, T& result) {
        if constexpr (std::is_same_v<T, TInstant>) {
            return ::NUnifiedAgent::TryParseDateTime(s, result);
        }
        return TryFromString(s, result);
    }
}

namespace YAML {
    template <typename T>
    struct convert {
        static bool decode(const Node& n, T& r) {
            return n.IsScalar() && NUnifiedAgent::TryParseScalar(n.Scalar(), r);
        }
    };

    template <>
    struct convert<TString> {
        static Node encode(const TString& rhs) {
            return Node(std::string(rhs));
        }

        static bool decode(const Node& n, TString& target) {
            std::string s;
            if (!convert<std::string>::decode(n, s))
                return false;
            target = TString(s);
            return true;
        }
    };
}

// If an expression is false, throw an exception that points to the location of the given
// node as the source of the error.
#define CHECK_NODE(node, expr, ...) do if (!(expr)) \
    throw TConfigurationException(node, (TStringBuilder() << __VA_ARGS__)); while (0)

namespace NUnifiedAgent {
    YAML::Node StubNode(const YAML::Node& parent);

    template <typename...K>
    YAML::Node RequiredPath(const YAML::Node& arg, K... path) {
        YAML::Node result = arg;
        ApplyToMany([&](auto&& k) {
            Y_ENSURE(result.Type() == YAML::NodeType::Map || result.Type() == YAML::NodeType::Sequence,
                     Sprintf("can't use key [%s], invalid parent node type [%d]",
                             ToString(k).c_str(), static_cast<int>(result.Type())));
            result.reset(result[k]);
            Y_ENSURE(result, Sprintf("key [%s] not found", ToString(k).c_str()));
        }, path...);
        return result;
    }

    YAML::Node Cut(const YAML::Node& arg, const TString& key);

    namespace NPrivate {
        template <typename TPreorderAction>
        void DoTraverse(const YAML::Node& node, TPreorderAction& preorderAction) {
            if (!preorderAction(node)) {
                return;
            }
            if (node.Type() != YAML::NodeType::Sequence && node.Type() != YAML::NodeType::Map) {
                return;
            }
            for (const auto& n: node) {
                if (n.first) {
                    DoTraverse(n.first, preorderAction);
                }
                if (n.second) {
                    DoTraverse(n.second, preorderAction);
                }
                if (n) {
                    DoTraverse(n, preorderAction);
                }
            }
        };
    }

    template <typename TPreorderAction>
    void Traverse(const YAML::Node& node, TPreorderAction&& preorderAction) {
        NPrivate::DoTraverse(node, preorderAction);
    }

    using TSourceFileResolver = std::function<TString(const YAML::Node& n)>;

    const TString& GetDefaultSourceFile();

    void SetSourceFileResolver(TSourceFileResolver&& resolver);

    class TConfigurationException: public std::runtime_error {
    public:
        TConfigurationException(const YAML::Node& n, const TString& message);
    };

    TString FormatErrorWithConfig(const YAML::Node& n, const TString& message);

    TString DescribeLocation(const YAML::Node& n);

    YAML::Node CloneWithMarks(YAML::Node n);
}
