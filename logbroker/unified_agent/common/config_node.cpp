#include "config_node.h"

#include <logbroker/unified_agent/common/common.h>

#include <util/generic/vector.h>

#include <cctype>

namespace NUnifiedAgent {
    namespace NPrivate {
        YAML::Node GetKeyNode(const YAML::Node& node, const TString& key) {
            for (const auto& p: node) {
                TString k;
                CHECK_NODE(p.first, YAML::convert<TString>::decode(p.first, k), "map key must be a string");
                if (k == key) {
                    return p.first;
                }
            }
            return YAML::Node(YAML::NodeType::Undefined);
        }

        TString ParseName(const TString& key, const YAML::Node& node, const YAML::Node& containerNode) {
            CheckScalarNode(key, node, FormatTypeName<TString>());
            TString result = node.as<TString>();
            TNonEmpty{}(key, result, node.IsDefined() ? node : containerNode);
            return result;
        }

        void CheckNoExpr(const TString& key, const YAML::Node& node) {
            CHECK_NODE(node,
                node.Tag() != "!expr",
                Sprintf("field [%s] doesn't support !expr", key.c_str()));
        }

        bool IsMapWithExprValues(const YAML::Node& node) {
            if (!node.IsMap()) {
                return false;
            }

            for (const auto& p: node) {
                if (p.second.Tag() == "!expr") {
                    return true;
                }
            }
            return false;
        }

        TIntrusivePtr<ITextExpression> ParseExpression(const TString& key,
            const YAML::Node& node,
            const TTextExpressionOptions& options)
        {
            try {
                return ParseTextExpression(node.template as<TString>(), options);
            } catch (TInvalidFormatException& e) {
                throw TConfigurationException(node,
                    Sprintf("field [%s], !expr has invalid format - %s", key.c_str(), e.what()));
            }
        }
    }

    TConfigNode::TConfigNode(const YAML::Node& node)
        : Node(node ? node : YAML::Node(YAML::NodeType::Null))
        , Fields_()
    {
        CHECK_NODE(Node, Node.IsMap() || Node.IsNull(), "expected map or null node");
    }

    TString TConfigNode::PrintFieldNames() const {
        return JoinSeq(", ", Fields_, [](const auto& f) {
            return Sprintf("[%s]", f.Key.c_str());
        });
    }

    void TConfigNode::CheckAfterParse(bool isOneOf, bool required, bool inplace) const {
        if (!inplace && Node.size() > 0) {
            auto it = Node.begin();
            throw TConfigurationException(it->first,
                Sprintf("unrecognized field [%s]", it->first.Scalar().c_str()));
        }

        if (isOneOf) {
            bool assignedFieldFound = false;
            for (const auto& f: Fields_) {
                if (!f.Assigned) {
                    continue;
                }

                if (!assignedFieldFound) {
                    assignedFieldFound = true;
                    continue;
                }

                throw TConfigurationException(f.KeyNode,
                    Sprintf("only one of %s can be specified", PrintFieldNames().c_str()));
            }

            CHECK_NODE(Node, !required || assignedFieldFound,
                       Sprintf("one of %s must be specified", PrintFieldNames().c_str()));
        }
    }

    bool TConfigNode::HasAssignedFields() const {
        return FindIf(Fields_, [](const auto& f){ return f.Assigned; }) != Fields_.end();
    }

    thread_local TVector<const TConfigNode::IParamsScope*> TConfigNode::Params_;

    bool TDataSize::operator()(const YAML::Node& n, size_t& result) const {
        return TryParseDataSize(TStringBuf{n.Scalar()}, result);
    }
}
