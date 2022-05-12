#include "ephemeral_node_factory.h"
#include "ephemeral_attribute_owner.h"
#include "node_detail.h"
#include "ypath_client.h"
#include "ypath_detail.h"

#include <yt/yt/core/misc/hash.h>
#include <yt/yt/core/misc/singleton.h>

#include <yt/yt/core/yson/async_consumer.h>

#include <algorithm>

namespace NYT::NYTree {

using namespace NRpc;
using namespace NYson;

////////////////////////////////////////////////////////////////////////////////

class TEphemeralNodeBase
    : public TNodeBase
    , public TSupportsAttributes
    , public TEphemeralAttributeOwner
{
public:
    explicit TEphemeralNodeBase(bool shouldHideAttributes)
        : ShouldHideAttributes_(shouldHideAttributes)
    { }

    std::unique_ptr<ITransactionalNodeFactory> CreateFactory() const override
    {
        return CreateEphemeralNodeFactory(ShouldHideAttributes_);
    }

    ICompositeNodePtr GetParent() const override
    {
        return Parent_.Lock();
    }

    void SetParent(const ICompositeNodePtr& parent) override
    {
        YT_ASSERT(!parent || Parent_.IsExpired());
        Parent_ = parent;
    }

    bool ShouldHideAttributes() override
    {
        return ShouldHideAttributes_;
    }

    void DoWriteAttributesFragment(
        IAsyncYsonConsumer* consumer,
        const std::optional<std::vector<TString>>& attributeKeys,
        bool stable) override
    {
        if (!HasAttributes()) {
            return;
        }

        const auto& attributes = Attributes();

        auto pairs = attributes.ListPairs();
        if (stable) {
            std::sort(pairs.begin(), pairs.end(), [] (const auto& lhs, const auto& rhs) {
                return lhs.first < rhs.first;
            });
        }

        THashSet<TString> matchingKeys;
        if (attributeKeys) {
            matchingKeys = THashSet<TString>(attributeKeys->begin(), attributeKeys->end());
        }

        for (const auto& [key, value] : pairs) {
            if (!attributeKeys || matchingKeys.find(key) != matchingKeys.end()) {
                consumer->OnKeyedItem(key);
                consumer->OnRaw(value);
            }
        }
    }

protected:
    // TSupportsAttributes members
    IAttributeDictionary* GetCustomAttributes() override
    {
        return MutableAttributes();
    }

private:
    TWeakPtr<ICompositeNode> Parent_;
    bool ShouldHideAttributes_;
};

////////////////////////////////////////////////////////////////////////////////

template <class TValue, class IBase>
class TScalarNode
    : public TEphemeralNodeBase
    , public virtual IBase
{
public:
    TScalarNode(bool shouldHideAttributes)
        : TEphemeralNodeBase(shouldHideAttributes)
        , Value_()
    { }

    typename NMpl::TCallTraits<TValue>::TType GetValue() const override
    {
        return Value_;
    }

    void SetValue(typename NMpl::TCallTraits<TValue>::TType value) override
    {
        Value_ = value;
    }

private:
    TValue Value_;
};

////////////////////////////////////////////////////////////////////////////////

#define DECLARE_SCALAR_TYPE(name, type) \
    class T ## name ## Node \
        : public TScalarNode<type, I ## name ## Node> \
    { \
        YTREE_NODE_TYPE_OVERRIDES(name) \
        \
        public: \
            T ## name ## Node (bool shouldHideAttributes) \
                : TScalarNode<type, I ## name ## Node> (shouldHideAttributes) \
            { } \
    };

DECLARE_SCALAR_TYPE(String, TString)
DECLARE_SCALAR_TYPE(Int64, i64)
DECLARE_SCALAR_TYPE(Uint64, ui64)
DECLARE_SCALAR_TYPE(Double, double)
DECLARE_SCALAR_TYPE(Boolean, bool)

#undef DECLARE_SCALAR_TYPE

////////////////////////////////////////////////////////////////////////////////

template <class IBase>
class TCompositeNodeBase
    : public TEphemeralNodeBase
    , public virtual IBase
{
public:
    TCompositeNodeBase(bool shouldHideAttributes)
        : TEphemeralNodeBase(shouldHideAttributes)
    { }

    TIntrusivePtr<ICompositeNode> AsComposite() override
    {
        return this;
    }

    TIntrusivePtr<const ICompositeNode> AsComposite() const override
    {
        return this;
    }
};

////////////////////////////////////////////////////////////////////////////////

class TMapNode
    : public TCompositeNodeBase<IMapNode>
    , public TMapNodeMixin
{
    YTREE_NODE_TYPE_OVERRIDES(Map)

public:
    explicit TMapNode(bool shouldHideAttributes)
        : TCompositeNodeBase<IMapNode>(shouldHideAttributes)
    { }

    void Clear() override
    {
        for (const auto& [key, child] : KeyToChild_) {
            child->SetParent(nullptr);
        }
        KeyToChild_.clear();
        ChildToKey_.clear();
    }

    int GetChildCount() const override
    {
        return KeyToChild_.ysize();
    }

    std::vector< std::pair<TString, INodePtr> > GetChildren() const override
    {
        return std::vector< std::pair<TString, INodePtr> >(KeyToChild_.begin(), KeyToChild_.end());
    }

    std::vector<TString> GetKeys() const override
    {
        std::vector<TString> result;
        result.reserve(KeyToChild_.size());
        for (const auto& [key, child] : KeyToChild_) {
            result.push_back(key);
        }
        return result;
    }

    INodePtr FindChild(const TString& key) const override
    {
        auto it = KeyToChild_.find(key);
        return it == KeyToChild_.end() ? nullptr : it->second;
    }

    bool AddChild(const TString& key, const INodePtr& child) override
    {
        YT_ASSERT(child);
        ValidateYTreeKey(key);

        if (KeyToChild_.emplace(key, child).second) {
            YT_VERIFY(ChildToKey_.emplace(child, key).second);
            child->SetParent(this);
            return true;
        } else {
            return false;
        }
    }

    bool RemoveChild(const TString& key) override
    {
        auto it = KeyToChild_.find(TString(key));
        if (it == KeyToChild_.end())
            return false;

        auto child = it->second;
        child->SetParent(nullptr);
        KeyToChild_.erase(it);
        YT_VERIFY(ChildToKey_.erase(child) == 1);

        return true;
    }

    void RemoveChild(const INodePtr& child) override
    {
        YT_ASSERT(child);

        child->SetParent(nullptr);

        auto it = ChildToKey_.find(child);
        YT_ASSERT(it != ChildToKey_.end());

        // NB: don't use const auto& here, it becomes invalid!
        auto key = it->second;
        ChildToKey_.erase(it);
        YT_VERIFY(KeyToChild_.erase(key) == 1);
    }

    void ReplaceChild(const INodePtr& oldChild, const INodePtr& newChild) override
    {
        YT_ASSERT(oldChild);
        YT_ASSERT(newChild);

        if (oldChild == newChild)
            return;

        auto it = ChildToKey_.find(oldChild);
        YT_ASSERT(it != ChildToKey_.end());

        // NB: don't use const auto& here, it becomes invalid!
        auto key = it->second;

        oldChild->SetParent(nullptr);
        ChildToKey_.erase(it);

        KeyToChild_[key] = newChild;
        newChild->SetParent(this);
        YT_VERIFY(ChildToKey_.emplace(newChild, key).second);
    }

    std::optional<TString> FindChildKey(const IConstNodePtr& child) override
    {
        YT_ASSERT(child);

        auto it = ChildToKey_.find(const_cast<INode*>(child.Get()));
        return it == ChildToKey_.end() ? std::nullopt : std::make_optional(it->second);
    }

private:
    THashMap<TString, INodePtr> KeyToChild_;
    THashMap<INodePtr, TString> ChildToKey_;

    bool DoInvoke(const IServiceContextPtr& context) override
    {
        DISPATCH_YPATH_SERVICE_METHOD(List);
        return TEphemeralNodeBase::DoInvoke(context);
    }

    IYPathService::TResolveResult ResolveRecursive(
        const TYPath& path,
        const IServiceContextPtr& context) override
    {
        return TMapNodeMixin::ResolveRecursive(path, context);
    }
};

////////////////////////////////////////////////////////////////////////////////

class TListNode
    : public TCompositeNodeBase<IListNode>
    , public TListNodeMixin
{
    YTREE_NODE_TYPE_OVERRIDES(List)

public:
    explicit TListNode(bool shouldHideAttributes)
        : TCompositeNodeBase<IListNode>(shouldHideAttributes)
    { }

    void Clear() override
    {
        for (const auto& node : IndexToChild_) {
            node->SetParent(nullptr);
        }
        IndexToChild_.clear();
        ChildToIndex_.clear();
    }

    int GetChildCount() const override
    {
        return IndexToChild_.size();
    }

    std::vector<INodePtr> GetChildren() const override
    {
        return IndexToChild_;
    }

    INodePtr FindChild(int index) const override
    {
        return index >= 0 && index < std::ssize(IndexToChild_) ? IndexToChild_[index] : nullptr;
    }

    void AddChild(const INodePtr& child, int beforeIndex = -1) override
    {
        YT_ASSERT(child);

        if (beforeIndex < 0) {
            YT_VERIFY(ChildToIndex_.emplace(child, static_cast<int>(IndexToChild_.size())).second);
            IndexToChild_.push_back(child);
        } else {
            YT_VERIFY(beforeIndex <= std::ssize(IndexToChild_));
            for (auto it = IndexToChild_.begin() + beforeIndex; it != IndexToChild_.end(); ++it) {
                ++ChildToIndex_[*it];
            }

            YT_VERIFY(ChildToIndex_.emplace(child, beforeIndex).second);
            IndexToChild_.insert(IndexToChild_.begin() + beforeIndex, child);
        }
        child->SetParent(this);
    }

    bool RemoveChild(int index) override
    {
        if (index < 0 || index >= std::ssize(IndexToChild_))
            return false;

        auto child = IndexToChild_[index];

        for (auto it = IndexToChild_.begin() + index + 1; it != IndexToChild_.end(); ++it) {
            --ChildToIndex_[*it];
        }
        IndexToChild_.erase(IndexToChild_.begin() + index);

        YT_VERIFY(ChildToIndex_.erase(child) == 1);
        child->SetParent(nullptr);

        return true;
    }

    void ReplaceChild(const INodePtr& oldChild, const INodePtr& newChild) override
    {
        YT_ASSERT(oldChild);
        YT_ASSERT(newChild);

        if (oldChild == newChild)
            return;

        auto it = ChildToIndex_.find(oldChild);
        YT_ASSERT(it != ChildToIndex_.end());

        int index = it->second;

        oldChild->SetParent(nullptr);

        IndexToChild_[index] = newChild;
        ChildToIndex_.erase(it);
        YT_VERIFY(ChildToIndex_.emplace(newChild, index).second);
        newChild->SetParent(this);
    }

    void RemoveChild(const INodePtr& child) override
    {
        YT_ASSERT(child);

        int index = GetChildIndexOrThrow(child);
        YT_VERIFY(RemoveChild(index));
    }

    std::optional<int> FindChildIndex(const IConstNodePtr& child) override
    {
        YT_ASSERT(child);

        auto it = ChildToIndex_.find(const_cast<INode*>(child.Get()));
        return it == ChildToIndex_.end() ? std::nullopt : std::make_optional(it->second);
    }

private:
    std::vector<INodePtr> IndexToChild_;
    THashMap<INodePtr, int> ChildToIndex_;

    TResolveResult ResolveRecursive(
        const TYPath& path,
        const IServiceContextPtr& context) override
    {
        return TListNodeMixin::ResolveRecursive(path, context);
    }
};

////////////////////////////////////////////////////////////////////////////////

class TEntityNode
    : public TEphemeralNodeBase
    , public virtual IEntityNode
{
    YTREE_NODE_TYPE_OVERRIDES(Entity)

public:
    explicit TEntityNode(bool shouldHideAttributes)
        : TEphemeralNodeBase(shouldHideAttributes)
    { }
};

////////////////////////////////////////////////////////////////////////////////

class TEphemeralNodeFactory
    : public TTransactionalNodeFactoryBase
{
public:
    explicit TEphemeralNodeFactory(bool shouldHideAttributes)
        : ShouldHideAttributes_(shouldHideAttributes)
    { }

    virtual ~TEphemeralNodeFactory() override
    {
        RollbackIfNeeded();
    }

    IStringNodePtr CreateString() override
    {
        return New<TStringNode>(ShouldHideAttributes_);
    }

    IInt64NodePtr CreateInt64() override
    {
        return New<TInt64Node>(ShouldHideAttributes_);
    }

    IUint64NodePtr CreateUint64() override
    {
        return New<TUint64Node>(ShouldHideAttributes_);
    }

    IDoubleNodePtr CreateDouble() override
    {
        return New<TDoubleNode>(ShouldHideAttributes_);
    }

    IBooleanNodePtr CreateBoolean() override
    {
        return New<TBooleanNode>(ShouldHideAttributes_);
    }

    IMapNodePtr CreateMap() override
    {
        return New<TMapNode>(ShouldHideAttributes_);
    }

    IListNodePtr CreateList() override
    {
        return New<TListNode>(ShouldHideAttributes_);
    }

    IEntityNodePtr CreateEntity() override
    {
        return New<TEntityNode>(ShouldHideAttributes_);
    }

private:
    const bool ShouldHideAttributes_;
};

std::unique_ptr<ITransactionalNodeFactory> CreateEphemeralNodeFactory(bool shouldHideAttributes)
{
    return std::unique_ptr<ITransactionalNodeFactory>(new TEphemeralNodeFactory(shouldHideAttributes));
}

INodeFactory* GetEphemeralNodeFactory(bool shouldHideAttributes)
{
    static auto hidingFactory = CreateEphemeralNodeFactory(true);
    static auto nonhidingFactory = CreateEphemeralNodeFactory(false);
    return shouldHideAttributes ? hidingFactory.get() : nonhidingFactory.get();
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NYTree

