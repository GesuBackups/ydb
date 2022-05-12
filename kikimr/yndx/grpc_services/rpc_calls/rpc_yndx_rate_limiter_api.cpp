#include "service_yndx_rate_limiter.h"
#include <ydb/core/grpc_services/rpc_scheme_base.h>
#include <ydb/core/grpc_services/rpc_common.h>
#include <ydb/core/base/quoter.h>
#include <ydb/core/kesus/tablet/events.h>

#include <kikimr/yndx/api/protos/ydb_yndx_rate_limiter.pb.h>

namespace NKikimr::NGRpcService {

using namespace NActors;
using namespace Ydb;
using namespace NKesus;

using TEvCreateYndxRateLimiterResource =
    TGrpcRequestOperationCall<Ydb::Yndx::RateLimiter::CreateResourceRequest,
        Ydb::Yndx::RateLimiter::CreateResourceResponse>;
using TEvAlterYndxRateLimiterResource =
    TGrpcRequestOperationCall<Ydb::Yndx::RateLimiter::AlterResourceRequest,
        Ydb::Yndx::RateLimiter::AlterResourceResponse>;
using TEvDropYndxRateLimiterResource =
    TGrpcRequestOperationCall<Ydb::Yndx::RateLimiter::DropResourceRequest,
        Ydb::Yndx::RateLimiter::DropResourceResponse>;
using TEvListYndxRateLimiterResources =
    TGrpcRequestOperationCall<Ydb::Yndx::RateLimiter::ListResourcesRequest,
        Ydb::Yndx::RateLimiter::ListResourcesResponse>;
using TEvDescribeYndxRateLimiterResource =
    TGrpcRequestOperationCall<Ydb::Yndx::RateLimiter::DescribeResourceRequest,
        Ydb::Yndx::RateLimiter::DescribeResourceResponse>;
using TEvAcquireYndxRateLimiterResource =
    TGrpcRequestOperationCall<Ydb::Yndx::RateLimiter::AcquireResourceRequest,
        Ydb::Yndx::RateLimiter::AcquireResourceResponse>;

namespace {

template <typename TDerived, typename TRequest>
class TRateLimiterRequest : public TRpcOperationRequestActor<TDerived, TRequest> {
public:
    using TBase = TRpcOperationRequestActor<TDerived, TRequest>;
    using TBase::TBase;

    bool ValidateResource(const Ydb::Yndx::RateLimiter::Resource& resource, Ydb::StatusIds::StatusCode& status, NYql::TIssues& issues) {
        if (!ValidateResourcePath(resource.resource_path(), status, issues)) {
            return false;
        }

        if (resource.type_case() == Ydb::Yndx::RateLimiter::Resource::TYPE_NOT_SET) {
            status = StatusIds::BAD_REQUEST;
            issues.AddIssue("No resource properties.");
            return false;
        }

        return true;
    }

    bool ValidateResourcePath(const TString& path, Ydb::StatusIds::StatusCode& status, NYql::TIssues& issues) {
        if (path != CanonizeQuoterResourcePath(path)) {
            status = StatusIds::BAD_REQUEST;
            issues.AddIssue("Bad resource path.");
            return false;
        }
        return true;
    }
};

template <class TEvRequest>
class TRateLimiterControlRequest : public TRateLimiterRequest<TRateLimiterControlRequest<TEvRequest>, TEvRequest> {
public:
    using TBase = TRateLimiterRequest<TRateLimiterControlRequest<TEvRequest>, TEvRequest>;
    using TBase::TBase;

    void Bootstrap(const TActorContext& ctx) {
        TBase::Bootstrap(ctx);

        this->Become(&TRateLimiterControlRequest::StateFunc);

        Ydb::StatusIds::StatusCode status = Ydb::StatusIds::STATUS_CODE_UNSPECIFIED;
        NYql::TIssues issues;
        if (!ValidateRequest(status, issues)) {
            this->Reply(status, issues, TActivationContext::ActorContextFor(this->SelfId()));
            return;
        }

        ResolveCoordinationPath();
    }

protected:
    STFUNC(StateFunc) {
        switch (ev->GetTypeRewrite()) {
            hFunc(TEvTxProxySchemeCache::TEvNavigateKeySetResult, Handle);
            hFunc(TEvTabletPipe::TEvClientConnected, Handle);
            hFunc(TEvTabletPipe::TEvClientDestroyed, Handle);
        default:
            return TBase::StateFuncBase(ev, ctx);
        }
    }

    const TString& GetCoordinationNodePath() const {
        return this->GetProtoRequest()->coordination_node_path();
    }

    void ResolveCoordinationPath() {
        TVector<TString> path = NKikimr::SplitPath(GetCoordinationNodePath());
        if (path.empty()) {
            this->Reply(StatusIds::BAD_REQUEST, "Empty path.", NKikimrIssues::TIssuesIds::GENERIC_RESOLVE_ERROR, TActivationContext::ActorContextFor(this->SelfId()));
            return;
        }

        auto req = MakeHolder<NSchemeCache::TSchemeCacheNavigate>();
        req->ResultSet.emplace_back();
        req->ResultSet.back().Path.swap(path);
        req->ResultSet.back().Operation = NSchemeCache::TSchemeCacheNavigate::OpPath;
        this->Send(MakeSchemeCacheID(), new TEvTxProxySchemeCache::TEvNavigateKeySet(req), 0, 0);
    }

    void Handle(TEvTxProxySchemeCache::TEvNavigateKeySetResult::TPtr& ev) {
        THolder<NSchemeCache::TSchemeCacheNavigate> navigate = std::move(ev->Get()->Request);
        if (navigate->ResultSet.size() != 1 || navigate->ErrorCount > 0) {
            this->Reply(StatusIds::INTERNAL_ERROR, TActivationContext::ActorContextFor(this->SelfId()));
            return;
        }

        const auto& entry = navigate->ResultSet.front();
        if (entry.Status != NSchemeCache::TSchemeCacheNavigate::EStatus::Ok) {
            this->Reply(StatusIds::SCHEME_ERROR, TActivationContext::ActorContextFor(this->SelfId()));
            return;
        }

        if (entry.Kind != NSchemeCache::TSchemeCacheNavigate::KindKesus) {
            this->Reply(StatusIds::BAD_REQUEST, "Path is not a coordination node path.", NKikimrIssues::TIssuesIds::GENERIC_RESOLVE_ERROR, TActivationContext::ActorContextFor(this->SelfId()));
            return;
        }

        if (!entry.KesusInfo) {
            this->Reply(StatusIds::INTERNAL_ERROR, "Internal error: no coordination node info found.", NKikimrIssues::TIssuesIds::GENERIC_RESOLVE_ERROR, TActivationContext::ActorContextFor(this->SelfId()));
            return;
        }

        KesusTabletId = entry.KesusInfo->Description.GetKesusTabletId();

        if (!KesusTabletId) {
            this->Reply(StatusIds::INTERNAL_ERROR, "Internal error: no coordination node id found.", NKikimrIssues::TIssuesIds::GENERIC_RESOLVE_ERROR, TActivationContext::ActorContextFor(this->SelfId()));
            return;
        }

        CreatePipe();

        SendRequest();
    }

    NTabletPipe::TClientConfig GetPipeConfig() {
        NTabletPipe::TClientConfig cfg;
        cfg.RetryPolicy = {
            .RetryLimitCount = 3u
        };
        return cfg;
    }

    void CreatePipe() {
        KesusPipeClient = this->Register(NTabletPipe::CreateClient(this->SelfId(), KesusTabletId, GetPipeConfig()));
    }

    void Handle(TEvTabletPipe::TEvClientConnected::TPtr& ev) {
        if (ev->Get()->Status != NKikimrProto::OK) {
            this->Reply(StatusIds::UNAVAILABLE, "Failed to connect to coordination node.", NKikimrIssues::TIssuesIds::SHARD_NOT_AVAILABLE, TActivationContext::ActorContextFor(this->SelfId()));
        }
    }

    void Handle(TEvTabletPipe::TEvClientDestroyed::TPtr&) {
        this->Reply(StatusIds::UNAVAILABLE, "Connection to coordination node was lost.", NKikimrIssues::TIssuesIds::SHARD_NOT_AVAILABLE, TActivationContext::ActorContextFor(this->SelfId()));
    }

    void ReplyFromKesusError(const NKikimrKesus::TKesusError& err) {
        this->Reply(err.GetStatus(), err.GetIssues(), TActivationContext::ActorContextFor(this->SelfId()));
    }

    virtual bool ValidateRequest(Ydb::StatusIds::StatusCode& status, NYql::TIssues& issues) = 0;

    virtual void SendRequest() = 0;

    void PassAway() override {
        if (KesusPipeClient) {
            NTabletPipe::CloseClient(this->SelfId(), KesusPipeClient);
            KesusPipeClient = {};
        }
        TBase::PassAway();
    }

protected:
    ui64 KesusTabletId = 0;
    TActorId KesusPipeClient;
};

static void CopyProps(const Ydb::Yndx::RateLimiter::Resource& src, NKikimrKesus::TStreamingQuoterResource& dst) {
    dst.SetResourcePath(src.resource_path());
    const auto& srcProps = src.hierarchical_drr();
    auto& props = *dst.MutableHierarhicalDRRResourceConfig();
    props.SetMaxUnitsPerSecond(srcProps.max_units_per_second());
    props.SetMaxBurstSizeCoefficient(srcProps.max_burst_size_coefficient());
    props.SetPrefetchCoefficient(srcProps.prefetch_coefficient());
    props.SetPrefetchWatermark(srcProps.prefetch_watermark());
    if (src.has_accounting_config()) {
        const auto& srcAcc = src.accounting_config();
        auto& acc = *dst.MutableAccountingConfig();
        acc.SetEnabled(srcAcc.enabled());
        acc.SetReportPeriodMs(srcAcc.report_period_ms());
        acc.SetAccountPeriodMs(srcAcc.account_period_ms());
        acc.SetCollectPeriodSec(srcAcc.collect_period_sec());
        acc.SetProvisionedUnitsPerSecond(srcAcc.provisioned_units_per_second());
        acc.SetProvisionedCoefficient(srcAcc.provisioned_coefficient());
        acc.SetOvershootCoefficient(srcAcc.overshoot_coefficient());
        auto copyMetric = [] (const Ydb::Yndx::RateLimiter::AccountingConfig::Metric& srcMetric, NKikimrKesus::TAccountingConfig::TMetric& metric) {
            metric.SetEnabled(srcMetric.enabled());
            metric.SetBillingPeriodSec(srcMetric.billing_period_sec());
            metric.SetVersion(srcMetric.version());
            metric.SetSchema(srcMetric.schema());
            metric.SetCloudId(srcMetric.cloud_id());
            metric.SetFolderId(srcMetric.folder_id());
            metric.SetResourceId(srcMetric.resource_id());
            metric.SetSourceId(srcMetric.source_id());
        };
        if (srcAcc.has_provisioned()) {
            copyMetric(srcAcc.provisioned(), *acc.MutableProvisioned());
        }
        if (srcAcc.has_on_demand()) {
            copyMetric(srcAcc.on_demand(), *acc.MutableOnDemand());
        }
        if (srcAcc.has_overshoot()) {
            copyMetric(srcAcc.overshoot(), *acc.MutableOvershoot());
        }
    }
}

static void CopyProps(const NKikimrKesus::TStreamingQuoterResource& src, Ydb::Yndx::RateLimiter::Resource& dst) {
    dst.set_resource_path(src.GetResourcePath());
    const auto& srcProps = src.GetHierarhicalDRRResourceConfig();
    auto& props = *dst.mutable_hierarchical_drr();
    props.set_max_units_per_second(srcProps.GetMaxUnitsPerSecond());
    props.set_max_burst_size_coefficient(srcProps.GetMaxBurstSizeCoefficient());
    props.set_prefetch_coefficient(srcProps.GetPrefetchCoefficient());
    props.set_prefetch_watermark(srcProps.GetPrefetchWatermark());
    if (src.HasAccountingConfig()) {
        const auto& srcAcc = src.GetAccountingConfig();
        auto& acc = *dst.mutable_accounting_config();
        acc.set_enabled(srcAcc.GetEnabled());
        acc.set_report_period_ms(srcAcc.GetReportPeriodMs());
        acc.set_account_period_ms(srcAcc.GetAccountPeriodMs());
        acc.set_collect_period_sec(srcAcc.GetCollectPeriodSec());
        acc.set_provisioned_units_per_second(srcAcc.GetProvisionedUnitsPerSecond());
        acc.set_provisioned_coefficient(srcAcc.GetProvisionedCoefficient());
        acc.set_overshoot_coefficient(srcAcc.GetOvershootCoefficient());
        auto copyMetric = [] (const NKikimrKesus::TAccountingConfig::TMetric& srcMetric, Ydb::Yndx::RateLimiter::AccountingConfig::Metric& metric) {
            metric.set_enabled(srcMetric.GetEnabled());
            metric.set_billing_period_sec(srcMetric.GetBillingPeriodSec());
            metric.set_version(srcMetric.GetVersion());
            metric.set_schema(srcMetric.GetSchema());
            metric.set_cloud_id(srcMetric.GetCloudId());
            metric.set_folder_id(srcMetric.GetFolderId());
            metric.set_resource_id(srcMetric.GetResourceId());
            metric.set_source_id(srcMetric.GetSourceId());
        };
        if (srcAcc.HasProvisioned()) {
            copyMetric(srcAcc.GetProvisioned(), *acc.mutable_provisioned());
        }
        if (srcAcc.HasOnDemand()) {
            copyMetric(srcAcc.GetOnDemand(), *acc.mutable_on_demand());
        }
        if (srcAcc.HasOvershoot()) {
            copyMetric(srcAcc.GetOvershoot(), *acc.mutable_overshoot());
        }
    }
}

class TCreateRateLimiterResourceRPC : public TRateLimiterControlRequest<TEvCreateYndxRateLimiterResource> {
public:
    using TBase = TRateLimiterControlRequest<TEvCreateYndxRateLimiterResource>;
    using TBase::TBase;
    using TBase::Handle;


    STFUNC(StateFunc) {
        switch (ev->GetTypeRewrite()) {
            hFunc(TEvKesus::TEvAddQuoterResourceResult, Handle);
        default:
            return TBase::StateFunc(ev, ctx);
        }
    }

    bool ValidateRequest(Ydb::StatusIds::StatusCode& status, NYql::TIssues& issues) override {
        return ValidateResource(GetProtoRequest()->resource(), status, issues);
    }

    void SendRequest() override {
        Become(&TCreateRateLimiterResourceRPC::StateFunc);

        THolder<TEvKesus::TEvAddQuoterResource> req = MakeHolder<TEvKesus::TEvAddQuoterResource>();
        CopyProps(GetProtoRequest()->resource(), *req->Record.MutableResource());
        NTabletPipe::SendData(SelfId(), KesusPipeClient, req.Release(), 0);
    }

    void Handle(TEvKesus::TEvAddQuoterResourceResult::TPtr& ev) {
        ReplyFromKesusError(ev->Get()->Record.GetError());
    }
};

class TAlterRateLimiterResourceRPC : public TRateLimiterControlRequest<TEvAlterYndxRateLimiterResource> {
public:
    using TBase = TRateLimiterControlRequest<TEvAlterYndxRateLimiterResource>;
    using TBase::TBase;
    using TBase::Handle;

    STFUNC(StateFunc) {
        switch (ev->GetTypeRewrite()) {
            hFunc(TEvKesus::TEvUpdateQuoterResourceResult, Handle);
        default:
            return TBase::StateFunc(ev, ctx);
        }
    }

    bool ValidateRequest(Ydb::StatusIds::StatusCode& status, NYql::TIssues& issues) override {
        return ValidateResource(GetProtoRequest()->resource(), status, issues);
    }

    void SendRequest() override {
        Become(&TAlterRateLimiterResourceRPC::StateFunc);

        THolder<TEvKesus::TEvUpdateQuoterResource> req = MakeHolder<TEvKesus::TEvUpdateQuoterResource>();
        CopyProps(GetProtoRequest()->resource(), *req->Record.MutableResource());
        NTabletPipe::SendData(SelfId(), KesusPipeClient, req.Release(), 0);
    }

    void Handle(TEvKesus::TEvUpdateQuoterResourceResult::TPtr& ev) {
        ReplyFromKesusError(ev->Get()->Record.GetError());
    }
};

class TDropRateLimiterResourceRPC : public TRateLimiterControlRequest<TEvDropYndxRateLimiterResource> {
public:
    using TBase = TRateLimiterControlRequest<TEvDropYndxRateLimiterResource>;
    using TBase::TBase;
    using TBase::Handle;

    STFUNC(StateFunc) {
        switch (ev->GetTypeRewrite()) {
            hFunc(TEvKesus::TEvDeleteQuoterResourceResult, Handle);
        default:
            return TBase::StateFunc(ev, ctx);
        }
    }

    bool ValidateRequest(Ydb::StatusIds::StatusCode& status, NYql::TIssues& issues) override {
        return ValidateResourcePath(GetProtoRequest()->resource_path(), status, issues);
    }

    void SendRequest() override {
        Become(&TDropRateLimiterResourceRPC::StateFunc);

        THolder<TEvKesus::TEvDeleteQuoterResource> req = MakeHolder<TEvKesus::TEvDeleteQuoterResource>();
        req->Record.SetResourcePath(GetProtoRequest()->resource_path());
        NTabletPipe::SendData(SelfId(), KesusPipeClient, req.Release(), 0);
    }

    void Handle(TEvKesus::TEvDeleteQuoterResourceResult::TPtr& ev) {
        ReplyFromKesusError(ev->Get()->Record.GetError());
    }
};

class TListRateLimiterResourcesRPC : public TRateLimiterControlRequest<TEvListYndxRateLimiterResources> {
public:
    using TBase = TRateLimiterControlRequest<TEvListYndxRateLimiterResources>;
    using TBase::TBase;
    using TBase::Handle;

    STFUNC(StateFunc) {
        switch (ev->GetTypeRewrite()) {
            hFunc(TEvKesus::TEvDescribeQuoterResourcesResult, Handle);
        default:
            return TBase::StateFunc(ev, ctx);
        }
    }

    bool ValidateRequest(Ydb::StatusIds::StatusCode& status, NYql::TIssues& issues) override {
        if (const TString& path = GetProtoRequest()->resource_path()) {
            return ValidateResourcePath(path, status, issues);
        }
        return true;
    }

    void SendRequest() override {
        Become(&TListRateLimiterResourcesRPC::StateFunc);

        THolder<TEvKesus::TEvDescribeQuoterResources> req = MakeHolder<TEvKesus::TEvDescribeQuoterResources>();
        if (const TString& path = GetProtoRequest()->resource_path()) {
            req->Record.AddResourcePaths(path);
        }
        req->Record.SetRecursive(GetProtoRequest()->recursive());
        NTabletPipe::SendData(SelfId(), KesusPipeClient, req.Release(), 0);
    }

    void Handle(TEvKesus::TEvDescribeQuoterResourcesResult::TPtr& ev) {
        const NKikimrKesus::TKesusError& kesusError = ev->Get()->Record.GetError();
        if (kesusError.GetStatus() == Ydb::StatusIds::SUCCESS) {
            Ydb::Yndx::RateLimiter::ListResourcesResult result;
            for (const auto& resource : ev->Get()->Record.GetResources()) {
                result.add_resource_paths(resource.GetResourcePath());
            }
            Request_->SendResult(result, Ydb::StatusIds::SUCCESS);
            PassAway();
        } else {
            ReplyFromKesusError(kesusError);
        }
    }
};

class TDescribeRateLimiterResourceRPC : public TRateLimiterControlRequest<TEvDescribeYndxRateLimiterResource> {
public:
    using TBase = TRateLimiterControlRequest<TEvDescribeYndxRateLimiterResource>;
    using TBase::TBase;
    using TBase::Handle;

    STFUNC(StateFunc) {
        switch (ev->GetTypeRewrite()) {
            hFunc(TEvKesus::TEvDescribeQuoterResourcesResult, Handle);
        default:
            return TBase::StateFunc(ev, ctx);
        }
    }

    bool ValidateRequest(Ydb::StatusIds::StatusCode& status, NYql::TIssues& issues) override {
        return ValidateResourcePath(GetProtoRequest()->resource_path(), status, issues);
    }

    void SendRequest() override {
        Become(&TDescribeRateLimiterResourceRPC::StateFunc);

        THolder<TEvKesus::TEvDescribeQuoterResources> req = MakeHolder<TEvKesus::TEvDescribeQuoterResources>();
        req->Record.AddResourcePaths(GetProtoRequest()->resource_path());
        NTabletPipe::SendData(SelfId(), KesusPipeClient, req.Release(), 0);
    }

    void Handle(TEvKesus::TEvDescribeQuoterResourcesResult::TPtr& ev) {
        const NKikimrKesus::TKesusError& kesusError = ev->Get()->Record.GetError();
        if (kesusError.GetStatus() == Ydb::StatusIds::SUCCESS) {
            Ydb::Yndx::RateLimiter::DescribeResourceResult result;
            if (ev->Get()->Record.ResourcesSize() == 0) {
                this->Reply(StatusIds::INTERNAL_ERROR, "No resource properties found.", NKikimrIssues::TIssuesIds::DEFAULT_ERROR, TActivationContext::ActorContextFor(this->SelfId()));
                return;
            }
            CopyProps(ev->Get()->Record.GetResources(0), *result.mutable_resource());
            Request_->SendResult(result, Ydb::StatusIds::SUCCESS);
            PassAway();
        } else {
            ReplyFromKesusError(kesusError);
        }
    }
};

class TAcquireRateLimiterResourceRPC : public TRateLimiterRequest<TAcquireRateLimiterResourceRPC, TEvAcquireYndxRateLimiterResource> {
public:
    using TBase = TRateLimiterRequest<TAcquireRateLimiterResourceRPC, TEvAcquireYndxRateLimiterResource>;
    using TBase::TBase;

    void Bootstrap(const TActorContext& ctx) {
        TBase::Bootstrap(ctx);

        Become(&TAcquireRateLimiterResourceRPC::StateFunc);

        Ydb::StatusIds::StatusCode status = Ydb::StatusIds::STATUS_CODE_UNSPECIFIED;
        NYql::TIssues issues;
        if (!ValidateRequest(status, issues)) {
            Reply(status, issues, TActivationContext::AsActorContext());
            return;
        }

        SendRequest();
    }

    STFUNC(StateFunc) {
        switch (ev->GetTypeRewrite()) {
            hFunc(TEvQuota::TEvClearance, Handle);
        default:
            return TBase::StateFuncBase(ev, ctx);
        }
    }

    bool ValidateRequest(Ydb::StatusIds::StatusCode& status, NYql::TIssues& issues) {
        if (!ValidateResourcePath(GetProtoRequest()->resource_path(), status, issues)) {
            return false;
        }

        if (GetProtoRequest()->units_case() == Ydb::Yndx::RateLimiter::AcquireResourceRequest::UnitsCase::UNITS_NOT_SET) {
            return false;
        }

        return true;
    }

    void SendRequest() {
        Become(&TAcquireRateLimiterResourceRPC::StateFunc);

        if (GetProtoRequest()->units_case() == Ydb::Yndx::RateLimiter::AcquireResourceRequest::UnitsCase::kRequired) {
            SendLeaf(
                TEvQuota::TResourceLeaf(GetProtoRequest()->coordination_node_path(),
                                        GetProtoRequest()->resource_path(),
                                        GetProtoRequest()->required()));
            return;
        }

        SendLeaf(
            TEvQuota::TResourceLeaf(GetProtoRequest()->coordination_node_path(),
                                    GetProtoRequest()->resource_path(),
                                    GetProtoRequest()->used(),
                                    true));
    }

    void SendLeaf(const TEvQuota::TResourceLeaf& leaf) {
        Send(MakeQuoterServiceID(),
            new TEvQuota::TEvRequest(TEvQuota::EResourceOperator::And, { leaf }, GetOperationTimeout()), 0, 0);
    }

    void Handle(TEvQuota::TEvClearance::TPtr& ev) {
        switch (ev->Get()->Result) {
            case TEvQuota::TEvClearance::EResult::Success:
                Reply(StatusIds::SUCCESS, TActivationContext::AsActorContext());
            break;
            case TEvQuota::TEvClearance::EResult::UnknownResource:
                Reply(StatusIds::BAD_REQUEST, TActivationContext::AsActorContext());
            break;
            case TEvQuota::TEvClearance::EResult::Deadline:
                Reply(StatusIds::TIMEOUT, TActivationContext::AsActorContext());
            break;
            default:
                Reply(StatusIds::INTERNAL_ERROR, TActivationContext::AsActorContext());
        }
    }
};

} // namespace

void DoCreateYndxRateLimiterResource(std::unique_ptr<IRequestOpCtx> p, const IFacilityProvider&) {
    TActivationContext::AsActorContext().Register(new TCreateRateLimiterResourceRPC(p.release()));
}

void DoAlterYndxRateLimiterResource(std::unique_ptr<IRequestOpCtx> p, const IFacilityProvider&) {
    TActivationContext::AsActorContext().Register(new TAlterRateLimiterResourceRPC(p.release()));
}

void DoDropYndxRateLimiterResource(std::unique_ptr<IRequestOpCtx> p, const IFacilityProvider&) {
    TActivationContext::AsActorContext().Register(new TDropRateLimiterResourceRPC(p.release()));
}

void DoListYndxRateLimiterResources(std::unique_ptr<IRequestOpCtx> p, const IFacilityProvider&) {
    TActivationContext::AsActorContext().Register(new TListRateLimiterResourcesRPC(p.release()));
}

void DoDescribeYndxRateLimiterResource(std::unique_ptr<IRequestOpCtx> p, const IFacilityProvider&) {
    TActivationContext::AsActorContext().Register(new TDescribeRateLimiterResourceRPC(p.release()));
}

void DoAcquireYndxRateLimiterResource(std::unique_ptr<IRequestOpCtx> p, const IFacilityProvider&) {
    TActivationContext::AsActorContext().Register(new TAcquireRateLimiterResourceRPC(p.release()));
}

} // namespace NKikimr::NGRpcService
