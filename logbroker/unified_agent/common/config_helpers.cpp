#include "config_helpers.h"

#include <contrib/libs/yaml-cpp/src/nodebuilder.h>

#include <util/generic/size_literals.h>
#include <util/generic/strbuf.h>
#include <util/generic/vector.h>
#include <util/string/ascii.h>

namespace {
    static const auto ZombieNode_ = YAML::Node()["nonexistent"];

    const TString DefaultSourceFile = "yaml-cpp";

    class TNodeVisitor {
    public:
        explicit TNodeVisitor(YAML::EventHandler& eventHandler)
            : Handler(eventHandler)
        {
        }

        void Visit(const YAML::Node node) const {
            Handler.OnDocumentStart(node.Mark());
            DoVisit(node);
            Handler.OnDocumentEnd();
        }

    private:
        void DoVisit(const YAML::Node node) const {
            switch (node.Type()) {
                case YAML::NodeType::Undefined:
                    break;
                case YAML::NodeType::Null:
                    Handler.OnNull(node.Mark(), YAML::NullAnchor);
                    break;
                case YAML::NodeType::Scalar:
                    Handler.OnScalar(node.Mark(), node.Tag(), YAML::NullAnchor, node.Scalar());
                    break;
                case YAML::NodeType::Sequence:
                    Handler.OnSequenceStart(node.Mark(), node.Tag(), YAML::NullAnchor, node.Style());
                    for (auto& n: node) {
                        DoVisit(n);
                    }
                    Handler.OnSequenceEnd();
                    break;
                case YAML::NodeType::Map:
                    Handler.OnMapStart(node.Mark(), node.Tag(), YAML::NullAnchor, node.Style());
                    for (auto& p: node) {
                        DoVisit(p.first);
                        DoVisit(p.second);
                    }
                    Handler.OnMapEnd();
                    break;
            }
        }

    private:
        YAML::EventHandler& Handler;
    };
}

namespace NUnifiedAgent {
    YAML::Node StubNode(const YAML::Node& parent) {
        Y_VERIFY(parent);
        YAML::NodeBuilder builder;
        builder.OnDocumentStart(YAML::Mark());
        builder.OnMapStart(parent.Mark(), "", 0, YAML::EmitterStyle::Default);
        builder.OnMapEnd();
        builder.OnDocumentEnd();
        return builder.Root();
    }

    YAML::Node Cut(const YAML::Node& arg, const TString& key) {
        if (!arg || !arg.IsMap()) {
            return ZombieNode_;
        }
        auto result = arg[key];
        if (result) {
            auto argCopy = arg;
            Y_VERIFY(argCopy.remove(key));
        }
        return result;
    }

    static TSourceFileResolver Resolver = [](const YAML::Node&) {
        return GetDefaultSourceFile();
    };

    const TString& GetDefaultSourceFile() {
        return DefaultSourceFile;
    }

    void SetSourceFileResolver(TSourceFileResolver&& resolver) {
        Resolver = std::move(resolver);
    }

    TConfigurationException::TConfigurationException(const YAML::Node& n, const TString& message)
        : std::runtime_error(FormatErrorWithConfig(n, message))
    {
    }

    TString FormatErrorWithConfig(const YAML::Node& n, const TString& message) {
        const auto m = n.Mark();
        return Sprintf("%s%s: %s",
            Resolver(n).c_str(),
            m.is_null() ? "" : Sprintf(", line %d, column %d", m.line + 1, m.column + 1).c_str(),
            message.c_str());
    }

    TString DescribeLocation(const YAML::Node& n) {
        const auto m = n.Mark();
        return m.is_null()
            ? ""
            : Sprintf(" at (%s, line %d, column %d)", Resolver(n).c_str(), m.line + 1, m.column + 1);
    }

    YAML::Node CloneWithMarks(YAML::Node n) {
        YAML::NodeBuilder builder;
        TNodeVisitor visitor(builder);
        visitor.Visit(n);
        return builder.Root();
    }
}
