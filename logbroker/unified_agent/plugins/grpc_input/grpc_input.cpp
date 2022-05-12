#include <logbroker/unified_agent/common/common.h>
#include <logbroker/unified_agent/common/grpc/grpc_io.h>
#include <logbroker/unified_agent/common/tasks.h>
#include <logbroker/unified_agent/common/util/async_joiner.h>
#include <logbroker/unified_agent/common/util/clock.h>
#include <logbroker/unified_agent/common/util/logger.h>
#include <logbroker/unified_agent/interface/input_plugin.h>
#include <logbroker/unified_agent/plugins/grpc_input/proto/unified_agent.grpc.pb.h>
#include <logbroker/unified_agent/plugins/lib/grpc/grpc_utils.h>
#include <logbroker/unified_agent/plugins/lib/secrets.h>

#include <contrib/libs/grpc/include/grpcpp/resource_quota.h>
#include <contrib/libs/grpc/include/grpc++/grpc++.h>
#include <contrib/libs/grpc/include/grpc/impl/codegen/grpc_types.h>
#include <library/cpp/containers/absl_flat_hash/flat_hash_map.h>
#include <library/cpp/threading/future/future.h>

#include <util/generic/guid.h>
#include <util/generic/hash_set.h>
#include <util/generic/intrlist.h>
#include <util/generic/scope.h>
#include <util/generic/size_literals.h>
#include <util/system/env.h>
#include <util/system/sysstat.h>
#include <util/system/thread.h>
#include <util/string/join.h>
#include <util/string/printf.h>

#include <thread>

using namespace NThreading;
using namespace NMonitoring;

namespace NUnifiedAgent {
    namespace {
        struct TConfig: public TConfigNode {
            using TConfigNode::TConfigNode;

            CONFIG_FIELD(TString, Uri);

            CONFIG_FIELD(TFMaybe<TString>, DeprecatedSharedSecretKey,
                .Optional()
                .Key("shared_secret_key")
                .Configure([](auto& builder) {
                    if (builder.Node().Type() != YAML::NodeType::Scalar) {
                        builder.Disable();
                    }
                }));

            CONFIG_FIELD(TFMaybe<TSecretConfig>, SharedSecretKey,
                .Optional()
                .Configure([](auto& builder) {
                    if (builder.Node().Type() != YAML::NodeType::Map) {
                        builder.Disable();
                    }
                }));

            CONFIG_FIELD(size_t, GrpcMemoryQuota,
                .Optional(20_MB)
                .ParseAs<TDataSize>());

            CONFIG_FIELD(TFMaybe<size_t>, MaxReceiveMessageSize,
                .Optional()
                .ParseAs<TDataSize>());

            CONFIG_FIELD(ELogPriority, GrpcErrorLogPriority,
                .Optional(TLOG_ERR));

            CONFIG_FIELD(bool, SetReceiptTimestamp,
                .Optional(true)
                .Key("_set_receipt_timestamp"));
        };

        struct TCounters: public TDynamicCountersWrapper {
            explicit TCounters(TFlowPluginContext& pluginContext)
                : TDynamicCountersWrapper(pluginContext.Counters())
                , DataBatchSizeBytes(Unwrap()->GetHistogram("DataBatchSizeBytes", ExplicitHistogram({
                    10, 50, 100, 200, 500, 1000, 2000, 3000, 5000, 10000, 50000, 100000,
                    200000, 300000, 500000, 1000000, 10000000, 100000000, 1000000000})))
                , GrpcErrors(GetCounter("GrpcErrors", true))
            {
            }

            NMonitoring::THistogramPtr DataBatchSizeBytes;
            NMonitoring::TDeprecatedCounter& GrpcErrors;
        };

        class TSessionIdsPool {
        public:
            struct TSessionKey {
                TString Value;
            };

        public:
            explicit TSessionIdsPool(TScopeLogger& logger)
                : Logger(logger.Child("session_ids_pool"))
                , ReuseSessions_(GetEnv("UA_GRPC_INPUT_DISABLE_REUSE_SESSIONS").Empty())
                , Queue()
                , IndexByMetaKey()
                , Lock()
            {
            }

            inline bool ReuseSessions() const {
                return ReuseSessions_;
            }

            TFMaybe<TSessionKey> TryExtractReuseKey(THashMap<TString, TString>& sessionMeta) const {
                if (!ReuseSessions_) {
                    return Nothing();
                }
                const auto it = sessionMeta.find("_reusable");
                if (it == sessionMeta.end()) {
                    return Nothing();
                }
                const bool reusable = it->second == "true";
                sessionMeta.erase(it);
                if (!reusable) {
                    return Nothing();
                }
                return TSessionKey{DescribeHashMap(sessionMeta)};
            }

            TString TryReuse(const TSessionKey& reuseKey) {
                with_lock(Lock) {
                    if (const auto it = IndexByMetaKey.find(reuseKey.Value); it != IndexByMetaKey.end()) {
                        auto& sessions = it->second;
                        auto result = sessions.back()->Id;
                        Remove(it, sessions.size() - 1);
                        YLOG_INFO(Sprintf("reused session id [%s], key [%s]",
                            result.c_str(), reuseKey.Value.c_str()));
                        return result;
                    }
                    return "";
                }
            }

            void Stash(const TString& sessionId, const TSessionKey& key) {
                auto* newSession = new TReusableSession();
                newSession->Key = key.Value;
                newSession->Id = sessionId;

                with_lock(Lock) {
                    Queue.PushFront(newSession);
                    auto& sessions = IndexByMetaKey[key.Value];
                    newSession->Index = sessions.size();
                    sessions.push_back(newSession);
                    if (Queue.Size() > MaxSize) {
                        auto* session = Queue.Back();
                        YLOG_INFO(Sprintf("evicted session id [%s], key [%s]",
                            session->Id.c_str(), session->Key.c_str()));
                        const auto it = IndexByMetaKey.find(session->Key);
                        Y_VERIFY(it != IndexByMetaKey.end());
                        Remove(it, session->Index);
                    }
                }
                YLOG_INFO(Sprintf("stashed session id [%s], key [%s]",
                    sessionId.c_str(), key.Value.c_str()));
            }

        private:
            struct TReusableSession: public TIntrusiveListItem<TReusableSession> {
                TString Key;
                TString Id;
                size_t Index;
            };

            using TIndexByMetaKey = absl::flat_hash_map<TString, TVector<TReusableSession*>>;

        private:
            void Remove(TIndexByMetaKey::iterator it, size_t index) {
                // Lock must be held

                auto& sessions = it->second;
                Y_VERIFY(!sessions.empty());
                auto* session = sessions[index];
                if (index != sessions.size() - 1) {
                    auto* s = sessions.back();
                    sessions[index] = s;
                    s->Index = index;
                }
                sessions.pop_back();
                if (sessions.empty()) {
                    IndexByMetaKey.erase(it);
                }
                Queue.Remove(session);
                delete session;
            }

        private:
            TScopeLogger Logger;
            const bool ReuseSessions_;
            TIntrusiveListWithAutoDelete<TReusableSession, TDelete> Queue;
            TIndexByMetaKey IndexByMetaKey;
            TAdaptiveLock Lock;

        private:
            static constexpr size_t MaxSize = 200;
        };

        class TPlugin: public TFlowPluginBase<IInputPlugin, TConfig, TCounters> {
            class TGrpcSession;
            class TAgentPusher;

        public:
            explicit TPlugin(TFlowPluginContext& ctx)
                : TFlowPluginBase(ctx)
                , Service()
                , Server(nullptr)
                , IOCompletionQueue(nullptr)
                , IOPoller(Nothing())
                , SessionLogLabel(0)
                , Stopping(false)
                , ActiveSessions()
                , SessionsJoiner()
                , SharedSecretKey()
                , SessionIdsPool(ctx.Logger())
                , Lock()
            {
                EnsureGrpcConfigured(Ctx().AgentContext());

                const auto& f = Ctx().SessionsFactory()->GetFlowControlConfig().Inflight().ActionField();
                CHECK_NODE(f.Node,
                           !f.Value.Defined() || *f.Value == EInflightLimitAction::Drop,
                           Sprintf("[%s] is not supported", ToString(*f.Value).c_str()));
                Ctx().SessionsFactory()->GetFlowControlSpec().Inflight.Action = EInflightLimitAction::Drop;
            }

            void Start() override {
                SharedSecretKey = Config().SharedSecretKey().Defined()
                                  ? LoadSecret(*Config().SharedSecretKey(), "shared secret key")
                                  : Config().DeprecatedSharedSecretKey().GetOrElse("");
                grpc::ServerBuilder builder;
                {
                    grpc::ResourceQuota quota("unified_agent_quota");
                    quota.Resize(Max(Config().GrpcMemoryQuota(), Config().MaxReceiveMessageSize().GetOrElse(0) * 2));
                    builder.SetResourceQuota(quota);
                }
                if (Config().MaxReceiveMessageSize().Defined()) {
                    builder.SetMaxReceiveMessageSize(static_cast<int>(*Config().MaxReceiveMessageSize()));
                }

                // do not drop idle clients, set infinity
                builder.AddChannelArgument(GRPC_ARG_HTTP2_MAX_PING_STRIKES, 0);
                builder.AddChannelArgument(GRPC_ARG_HTTP2_MAX_PINGS_WITHOUT_DATA, 0);
                builder.AddChannelArgument(GRPC_ARG_KEEPALIVE_PERMIT_WITHOUT_CALLS, 1);
                builder.AddChannelArgument(GRPC_ARG_HTTP2_MIN_SENT_PING_INTERVAL_WITHOUT_DATA_MS, 5000);
                builder.AddChannelArgument(GRPC_ARG_HTTP2_MIN_RECV_PING_INTERVAL_WITHOUT_DATA_MS, 5000);
                builder.AddChannelArgument(GRPC_ARG_KEEPALIVE_TIME_MS, 60000);
                builder.AddChannelArgument(GRPC_ARG_KEEPALIVE_TIMEOUT_MS, 5000);
                builder.AddChannelArgument(GRPC_ARG_TCP_READ_CHUNK_SIZE, 1024*1024);

                builder.AddListeningPort(Config().Uri(), grpc::InsecureServerCredentials());
                builder.RegisterService(&Service);
                IOCompletionQueue = builder.AddCompletionQueue();
                IOPoller.ConstructInPlace(*IOCompletionQueue);
                IOPoller->Start();
                Server = builder.BuildAndStart();
                Y_ENSURE(Server, Sprintf("failed to start grpc server on [%s]", Config().Uri().c_str()));
                {
                    static const auto udsPrefix = TString("unix://");
                    if (Config().Uri().StartsWith(udsPrefix)) {
                        const auto socketFilePath = Config().Uri().substr(udsPrefix.size());
                        Y_ENSURE(Chmod(socketFilePath.c_str(), MODE0777) == 0,
                                 Sprintf("chmod failed for [%s], error [%s]",
                                         socketFilePath.c_str(), LastSystemErrorText()));
                    }
                }
                TGrpcSession::StartNew(*this);
                YLOG_NOTICE(Sprintf("listening [%s]", Config().Uri().c_str()));
            }

            void Stop() override {
                Y_VERIFY(!Stopping.exchange(true));
                {
                    THashSet<TIntrusivePtr<TGrpcSession>> activeSessions;
                    with_lock(Lock) {
                        activeSessions = ActiveSessions;
                    }
                    for (auto& s: activeSessions) {
                        s->TryCancel();
                    }
                }
                if (Server) {
                    Server->Shutdown(gpr_timespec{30, 0, GPR_TIMESPAN});
                    SessionsJoiner.Join().Wait();
                }
                if (IOCompletionQueue) {
                    IOCompletionQueue->Shutdown();
                }
                if (IOPoller) {
                    IOPoller->Join();
                }
            }

        private:
            void RegisterSession(TGrpcSession* session) {
                with_lock(Lock) {
                    Y_VERIFY(ActiveSessions.insert(session).second);
                }
            }

            void UnregisterSession(TGrpcSession* session) {
                with_lock(Lock) {
                    Y_VERIFY(ActiveSessions.erase(session) == 1);
                }
            }

        private:
            TScopeLogger CreateSessionLogger() {
                return Logger.Child(ToString(SessionLogLabel.fetch_add(1)));
            }

        private:
            struct TReceivedRequest {
                THolder<NUnifiedAgentProto::Request> Request;
                TFMaybe<TInstant> Timestamp;
            };

            class TGrpcSessionHandler: public IInputSessionHandler {
            public:
                TGrpcSessionHandler(const TString& sessionId, IMessageConsumer& messageConsumer, TGrpcSession& session)
                    : IInputSessionHandler(sessionId, messageConsumer)
                    , Session(session)
                {
                }

            private:
                void Start(TSeqNo lastSeqNo) override {
                    Session.StartDataTransfer(SessionId(), lastSeqNo, Consumer());
                }

                void Ack(TSeqNo seqNo) override {
                    Session.SendAck(seqNo);
                }

            private:
                TGrpcSession& Session;
            };

            class TGrpcSession final: public TAtomicRefCount<TGrpcSession> {
            public:
                TGrpcSession(TPlugin& owner)
                    : SessionsJoinerToken(&owner.SessionsJoiner)
                    , AcceptTag(MakeIOCallback(this, &TGrpcSession::EndAccept))
                    , ReadTag(MakeIOCallback(this, &TGrpcSession::EndRead))
                    , WriteTag(MakeIOCallback(this, &TGrpcSession::EndWrite))
                    , FinishTag(MakeIOCallback(this, &TGrpcSession::EndFinish))
                    , DoneTag(MakeIOCallback(this, &TGrpcSession::OnDone))
                    , Owner(owner)
                    , ServerContext()
                    , Started(false)
                    , ReaderWriter(&ServerContext)
                    , IOJoiner()
                    , ActiveWriteToken()
                    , Pusher(nullptr)
                    , Request()
                    , InitResponse()
                    , AckResponse()
                    , LastSentAck(Nothing())
                    , Logger(Owner.CreateSessionLogger())
                    , WritePending(false)
                    , ReadPending(false)
                    , WriteError(false)
                    , FinishStarted(false)
                    , Done(false)
                    , Finished(false)
                    , Poisoned(false)
                    , Lock()
                {
                    if (Owner.SessionIdsPool.ReuseSessions()) {
                        ServerContext.AddInitialMetadata("ua-reuse-sessions", "true");
                    }
                    ServerContext.AddInitialMetadata("ua-max-receive-message-size",
                        ToString(Owner.Config().MaxReceiveMessageSize().GetOrElse(GRPC_DEFAULT_MAX_RECV_MESSAGE_LENGTH)));

                    Owner.RegisterSession(this);
                    YLOG_INFO("created");
                }

                ~TGrpcSession() {
                    YLOG_INFO("destroyed");
                }

                static void StartNew(TPlugin& owner) {
                    MakeIntrusive<TGrpcSession>(owner)->Start();
                }

                inline TScopeLogger& GetLogger() noexcept {
                    return Logger;
                }

                inline TPlugin& GetOwner() noexcept {
                    return Owner;
                }

                void StartDataTransfer(const TString& sessionId, TSeqNo lastSeqNo, IMessageConsumer& messageConsumer) {
                    Pusher->Start(messageConsumer, lastSeqNo);
                    with_lock(Lock) {
                        if (WriteError) {
                            YLOG_INFO("StartDataTransfer, write error, dropped");
                            return;
                        }
                        InitResponse.MutableInitialized()->SetSessionId(sessionId);
                        InitResponse.MutableInitialized()->SetLastSeqNo(lastSeqNo.Value);
                        if (!WritePending) {
                            BeginWrite();
                        }
                    }
                }

                void BeginWrite() {
                    //Lock must be held

                    Y_VERIFY(!WritePending && (InitResponse.HasInitialized() || AckResponse.HasAck()));
                    ActiveWriteToken = &IOJoiner;
                    WritePending = true;
                    auto& response = InitResponse.HasInitialized() ? InitResponse : AckResponse;
                    ReaderWriter.Write(response, WriteTag->Ref());
                    YLOG_DEBUG(Sprintf("Write started, %s",
                                       response.HasInitialized()
                                       ? "init"
                                       : Sprintf("ack [%lu]", response.GetAck().GetSeqNo()).c_str()));
                    response.Clear();
                }

                void SendError(grpc::StatusCode code, const grpc::string& message) {
                    with_lock(Lock) {
                        ScheduleFinish(grpc::Status(code, message));
                    }
                }

                void SendAck(TSeqNo seqNo) {
                    with_lock(Lock) {
                        YLOG_DEBUG(Sprintf("SendAck [%lu]", seqNo.Value));
                        if (WriteError) {
                            YLOG_INFO("SendAck, write error, dropped");
                            return;
                        }
                        Y_VERIFY(!LastSentAck.Defined() || seqNo.Value > LastSentAck->Value,
                                 "must increase, seqNo [%lu], last ack [%lu]",
                                 seqNo.Value, LastSentAck->Value);
                        LastSentAck = seqNo;
                        AckResponse.MutableAck()->SetSeqNo(seqNo.Value);
                        if (!WritePending) {
                            BeginWrite();
                        }
                    }
                }

                void TryCancel() {
                    with_lock(Lock) {
                        if (Started) {
                            ServerContext.TryCancel();
                        }
                    }
                }

                void Poison() {
                    with_lock(Lock) {
                        Poisoned = true;
                    }
                    SendError(grpc::CANCELLED, "poisoned");
                }

            private:
                void Start() {
                    ServerContext.AsyncNotifyWhenDone(DoneTag->Ref());
                    Owner.Service.RequestSession(&ServerContext,
                                                 &ReaderWriter,
                                                 Owner.IOCompletionQueue.get(),
                                                 Owner.IOCompletionQueue.get(),
                                                 AcceptTag->Ref());
                    YLOG_INFO("RequestSession started");
                }

                void EndAccept(EIOStatus status) {
                    LogEndIO(status, "EndAccept", true, false);
                    with_lock(Lock) {
                        if (!Owner.Stopping.load()) {
                            StartNew(Owner);
                        }
                        if (!ServerContext.c_call()) {
                            // Request dropped before it could start
                            Y_VERIFY(status == EIOStatus::Error);
                            // Drop extra reference by OnDoneTag
                            UnRef();
                            Owner.UnregisterSession(this);
                            YLOG_INFO("EndAccept drop");
                            return;
                        }
                        if (status == EIOStatus::Error) {
                            ServerContext.TryCancel();
                            Owner.UnregisterSession(this);
                            YLOG_INFO("EndAccept error");
                            return;
                        }
                        Pusher = MakeHolder<TAgentPusher>(*this);
                        BeginRead();
                        Started = true;
                    }
                }

                void BeginRead() {
                    //Lock must be held

                    ReadPending = true;
                    Request = MakeHolder<NUnifiedAgentProto::Request>();
                    ReaderWriter.Read(Request.Get(), ReadTag->Ref());
                    YLOG_DEBUG("Read started");
                }

                void EndRead(EIOStatus status) {
                    LogEndIO(status, "EndRead", true, true);
                    with_lock(Lock) {
                        ReadPending = false;
                        if (status == EIOStatus::Error) {
                            ScheduleFinish(grpc::Status());
                            UnregisterIfNeeded();
                            return;
                        }
                        if (FinishStarted) {
                            UnregisterIfNeeded();
                            YLOG_INFO("EndRead FinishStarted, message dropped");
                            return;
                        }
                        Y_VERIFY(Pusher);
                        Pusher->Enqueue({std::move(Request), TClock::Now()});
                        BeginRead();
                    }
                }

                void EndWrite(EIOStatus status) {
                    LogEndIO(status, "EndWrite", false, true);
                    with_lock(Lock) {
                        Y_VERIFY(WritePending);
                        auto activeWriteToken = std::move(ActiveWriteToken);
                        WritePending = false;
                        if (status == EIOStatus::Error) {
                            WriteError = true;
                            ScheduleFinish(grpc::Status(grpc::UNAVAILABLE, "write failed"));
                            return;
                        }
                        if (InitResponse.HasInitialized() || AckResponse.HasAck()) {
                            BeginWrite();
                        }
                    }
                }

                void EndFinish(EIOStatus status) {
                    LogEndIO(status, "EndFinish", false, false);
                    with_lock(Lock) {
                        Finished = true;
                        UnregisterIfNeeded();
                    }
                }

                void OnDone(EIOStatus status) {
                    LogEndIO(status, "OnDone", false, false);
                    with_lock(Lock) {
                        if (Pusher && ServerContext.IsCancelled() && !Owner.Stopping.load() && !Poisoned) {
                            Pusher->OnClientCancelled();
                        }
                        Done = true;
                        ScheduleFinish(grpc::Status(grpc::CANCELLED, ""));
                        UnregisterIfNeeded();
                    }
                }

                void LogEndIO(EIOStatus status, const char* method, bool errorsExpected, bool debug) {
                    const auto isError = status == EIOStatus::Error && !errorsExpected && !Owner.Stopping.load();
                    YLOG(isError
                         ? Owner.Config().GrpcErrorLogPriority()
                         : (debug ? TLOG_DEBUG : TLOG_INFO),
                         Sprintf("%s, status [%s]", method, ToString(status).c_str()),
                         Logger);
                    if (isError) {
                        ++Owner.Counters()->GrpcErrors;
                    }
                }

                void UnregisterIfNeeded() {
                    //Lock must be held

                    if (!ReadPending && (Done && Finished)) {
                        Owner.UnregisterSession(this);
                    }
                }

                void ScheduleFinish(grpc::Status&& status) {
                    //Lock must be held

                    if (FinishStarted) {
                        return;
                    }
                    Y_VERIFY(Pusher);
                    YLOG(status.ok() || Poisoned ? TLOG_INFO : TLOG_ERR,
                         Sprintf("ScheduleFinish, code [%s], message [%s]",
                                 ToString(status.error_code()).c_str(), status.error_message().c_str()),
                         Logger);
                    FinishStarted = true;
                    Pusher->Stop(!status.ok() || Owner.Stopping.load())
                        .Apply([this_ = this, status = std::move(status)](const auto& f) {
                            Y_VERIFY(f.HasValue());

                            //Дожидаемся только write-ов - не понятно, как заставить read
                            //вернуться, если клиент не шлет данных. После TryCancel Finish не сработает.
                            //В комментариях к Finish-у сказано "Should not be used concurrently with other operations",
                            //но в grpc_streaming.h чуваки не ждут read-ов, и видимо это как-то работает.
                            return this_->IOJoiner.Join().Subscribe(
                                [this_ = TIntrusivePtr(this_), status = std::move(status)](const auto& f) {
                                    Y_VERIFY(f.HasValue());
                                    this_->ReaderWriter.Finish(status, this_->FinishTag->Ref());
                                    YLOG(ELogPriority::TLOG_INFO, "Finish started", this_->Logger);
                                });
                        });
                }

            private:
                TAsyncJoinerToken SessionsJoinerToken;
                THolder<IIOCallback> AcceptTag;
                THolder<IIOCallback> ReadTag;
                THolder<IIOCallback> WriteTag;
                THolder<IIOCallback> FinishTag;
                THolder<IIOCallback> DoneTag;
                TPlugin& Owner;
                grpc::ServerContext ServerContext;
                bool Started;
                grpc::ServerAsyncReaderWriter<NUnifiedAgentProto::Response, NUnifiedAgentProto::Request> ReaderWriter;
                TAsyncJoiner IOJoiner;
                TAsyncJoinerToken ActiveWriteToken;
                THolder<TAgentPusher> Pusher;
                THolder<NUnifiedAgentProto::Request> Request;
                NUnifiedAgentProto::Response InitResponse;
                NUnifiedAgentProto::Response AckResponse;
                TFMaybe<TSeqNo> LastSentAck;
                TScopeLogger Logger;
                bool WritePending;
                bool ReadPending;
                bool WriteError;
                bool FinishStarted;
                bool Done;
                bool Finished;
                bool Poisoned;
                TAdaptiveLock Lock;
            };

            class TAgentPusher: private ITask {
            public:
                TAgentPusher(TGrpcSession& session)
                    : Session(session)
                    , SubmitTask(TaskExecutor().RegisterSingle("GrpcInput/Push", ETaskPriority::Urgent, *this))
                    , Logger(Session.GetLogger().Child("pusher"))
                    , SessionId(Nothing())
                    , MessageConsumer(nullptr)
                    , LastSeqNo(Nothing())
                    , Queue()
                    , TaskData()
                    , ParseReceiptTimestampFromMeta(!GetEnv("UA_GRPC_INPUT_PARSE_RECEIPT_TIMESTAMP_FROM_META").empty())
                    , SessionReuseKey(Nothing())
                    , ClientCancelled(false)
                    , SessionIdsPool(Session.GetOwner().SessionIdsPool)
                    , Lock()
                {
                    YLOG_INFO("started");
                }

                ~TAgentPusher() {
                    if (ClientCancelled && SessionId.Defined() && SessionReuseKey.Defined()) {
                        SessionIdsPool.Stash(*SessionId, *SessionReuseKey);
                    }
                }

                void Start(IMessageConsumer& messageConsumer, TSeqNo lastSeqNo) {
                    with_lock(Lock) {
                        MessageConsumer = &messageConsumer;
                        LastSeqNo = lastSeqNo;
                    }
                }

                void OnClientCancelled() {
                    with_lock(Lock) {
                        ClientCancelled = true;
                    }
                }

                TFuture<void> Stop(bool force) {
                    if (force) {
                        with_lock(Lock) {
                            Queue.Clear();
                        }
                    }
                    return SubmitTask->Unregister()
                        .Apply([this, force](const auto& f) {
                            YLOG_INFO("task unregistered");
                            Y_VERIFY(f.HasValue());
                            Y_VERIFY(SubmitTask != nullptr);
                            SubmitTask = nullptr;
                            if (!SessionId.Defined()) {
                                return MakeFuture();
                            }
                            YLOG_INFO("StopSession started");
                            return GetSessionsFactory().StopSession(*SessionId, force)
                                .Apply([this](const auto& s) {
                                    Y_VERIFY(s.HasValue());
                                    YLOG_INFO("StopSession completed");
                                });
                            })
                        .Apply([this](const auto& f) mutable {
                            Y_VERIFY(f.HasValue());
                            YLOG_INFO("stopped");
                        });
                }

                void Enqueue(TReceivedRequest&& request) {
                    bool pulse;
                    with_lock(Lock) {
                        pulse = Queue.Empty();
                        Queue.Enqueue(std::move(request));
                    }
                    if (pulse) {
                        SubmitTask->Pulse();
                    }
                }

            private:
                TTaskExecutor& TaskExecutor() {
                    return Session.GetOwner().Ctx().TaskExecutor();
                }

                ISessionsFactory& GetSessionsFactory() {
                    return *Session.GetOwner().Ctx().SessionsFactory();
                }

                bool Run() override {
                    if (!TaskData.HasNextMessage() && !TaskData.HasNextRequest()) {
                        with_lock(Lock) {
                            TaskData.Requests = Queue.Dequeue();
                            TaskData.MessageConsumer = MessageConsumer;
                        }
                        if (TaskData.Requests == nullptr) {
                            return false;
                        }
                        YLOG_DEBUG(Sprintf("task, queue submit snapshot [%lu]", TaskData.Requests->size()));
                        TaskData.NextMessageIndex = 0;
                        TaskData.NextRequestIndex = 0;
                    }

                    while (true) {
                        try {
                            switch (TaskData.State) {
                                case EState::Initialize:
                                    StartSession();
                                    TaskData.State = EState::PrepareBatch;
                                    break;
                                case EState::PrepareBatch:
                                    if (!TaskData.HasNextRequest()) {
                                        return true;
                                    }
                                    PrepareBatch();
                                    TaskData.State = EState::SubmitBatch;
                                    break;
                                case EState::SubmitBatch:
                                    if (!TaskData.HasNextMessage()) {
                                        TaskData.State = EState::PrepareBatch;
                                        break;
                                    }
                                    SubmitBatch();
                                    return true;
                                case EState::Error: {
                                    return false;
                                }
                            }
                        } catch (const TProtocolViolationException& e) {
                            Session.SendError(e.Code, e.Message);
                            TaskData.State = EState::Error;
                        } catch (const TPoisonedException&) {
                            Session.Poison();
                            TaskData.State = EState::Error;
                        }
                    }
                }

                void StartSession() {
                    Y_VERIFY(!SessionId.Defined());

                    auto request = TaskData.ExtractRequest();
                    if (request.Request->request_case() != NUnifiedAgentProto::Request::kInitialize) {
                        throw TProtocolViolationException(grpc::INVALID_ARGUMENT, "expected kInitialize");
                    }

                    const auto& requestInitialize = request.Request->GetInitialize();
                    if (const auto& sharedSecretKey = Session.GetOwner().SharedSecretKey;
                        !sharedSecretKey.Empty() && requestInitialize.GetSharedSecretKey() != sharedSecretKey)
                    {
                        throw TProtocolViolationException(grpc::PERMISSION_DENIED, "invalid shared_secret_key");
                    }

                    THashMap<TString, TString> meta;
                    for (auto& p: requestInitialize.GetMeta()) {
                        meta[p.GetName()] = p.GetValue();
                    }
                    SessionReuseKey = SessionIdsPool.TryExtractReuseKey(meta);

                    auto sessionId = requestInitialize.GetSessionId();
                    bool ignoreRateLimit = false;
                    if (sessionId.Empty() && SessionReuseKey.Defined()) {
                        sessionId = SessionIdsPool.TryReuse(*SessionReuseKey);
                        ignoreRateLimit = !sessionId.Empty();
                    }
                    if (sessionId.Empty()) {
                        sessionId = CreateGuidAsString();
                    }
                    YLOG_INFO(Sprintf("init, sessionId [%s]", sessionId.c_str()));

                    const auto startSessionResult = GetSessionsFactory().StartSession(
                        sessionId,
                        MakeIntrusiveConst<TSessionMetaDictionary>(std::move(meta)),
                        [sessionId, this](auto& messageConsumer) {
                            return MakeHolder<TGrpcSessionHandler>(sessionId, messageConsumer, Session);
                        },
                        TSessionOptions(ESessionFaultTolerance::ResendOnFailure, nullptr, ignoreRateLimit));
                    switch (startSessionResult.Status) {
                        case TStartSessionResult::Ok: {
                            break;
                        }
                        case TStartSessionResult::AlreadyStarted: {
                            throw TProtocolViolationException(grpc::ALREADY_EXISTS,
                                Sprintf("session [%s] already started", sessionId.c_str()));
                        }
                        case TStartSessionResult::Throttled: {
                            throw TProtocolViolationException(grpc::RESOURCE_EXHAUSTED,
                                "new sessions per second limit reached");
                        }
                        case TStartSessionResult::Error: {
                            throw TProtocolViolationException(grpc::FAILED_PRECONDITION,
                                *startSessionResult.ErrorMessage);
                        }
                        default: {
                            Y_FAIL("unexpected status [%d]", static_cast<int>(startSessionResult.Status));
                        }
                    }

                    SessionId = sessionId;
                    YLOG_INFO("agent session started");
                }

                void PrepareBatch() {
                    auto request = TaskData.ExtractRequest();
                    if (request.Request->request_case() != NUnifiedAgentProto::Request::kDataBatch) {
                        throw TProtocolViolationException(grpc::INVALID_ARGUMENT, "expected kDataBatch");
                    }

                    if (TaskData.MessageConsumer == nullptr) {
                        throw TProtocolViolationException(grpc::FAILED_PRECONDITION, "session is not started yet");
                    }
                    Y_VERIFY(LastSeqNo.Defined());

                    auto& batch = *request.Request->MutableDataBatch();
                    if (batch.GetSeqNo().empty() ||
                        batch.TimestampSize() != batch.SeqNoSize() ||
                        batch.PayloadSize() != batch.SeqNoSize())
                    {
                        throw TProtocolViolationException(grpc::INVALID_ARGUMENT,
                            Sprintf("invalid batch, seq_no [%lu], timestamp [%lu], payload [%lu]",
                                    batch.SeqNoSize(),
                                    batch.TimestampSize(),
                                    batch.PayloadSize()));
                    }

                    Y_VERIFY(TaskData.Batch.empty());
                    TaskData.Batch.resize(batch.SeqNoSize());
                    size_t batchSize = 0;
                    for (size_t i = 0; i < batch.SeqNoSize(); ++i) {
                        if (SessionReuseKey.Defined() &&
                            batch.GetSeqNo(i) == std::numeric_limits<::google::protobuf::uint64>::max())
                        {
                            YLOG_INFO("poison pill received");
                            throw TPoisonedException();
                        }
                        const auto seqNo = TSeqNo{batch.GetSeqNo(i)};
                        if (seqNo.Value <= LastSeqNo->Value) {
                            throw TProtocolViolationException(grpc::FAILED_PRECONDITION,
                                Sprintf("seqNo [%lu] is not greater than last seqNo [%lu]",
                                        seqNo.Value, LastSeqNo->Value));
                        }
                        LastSeqNo->Value = seqNo.Value;
                        auto message =  MakeIntrusive<TMessage>(
                            std::move(*batch.MutablePayload(i)),
                            TInstant::MicroSeconds(batch.GetTimestamp(i)));
                        batchSize += message->Payload.size();
                        if (Session.GetOwner().Config().SetReceiptTimestamp()) {
                            message->ReceiptTimestamp = request.Timestamp;
                        }
                        TaskData.Batch[i].SeqNo = seqNo;
                        TaskData.Batch[i].Message = std::move(message);
                    }
                    Session.GetOwner().Counters()->DataBatchSizeBytes->Collect(batchSize);

                    for (const auto& m: batch.GetMeta()) {
                        if (m.SkipStartSize() != m.SkipLengthSize()) {
                            throw TProtocolViolationException(grpc::INVALID_ARGUMENT,
                                Sprintf("invalid meta, skip start size [%lu], "
                                        "skip length size [%lu]",
                                        m.SkipStartSize(), m.SkipLengthSize()));
                        }
                        size_t messageIndex = 0;
                        size_t skipIndex = 0;
                        for (size_t i = 0; i < m.ValueSize(); ++i) {
                            if (skipIndex < m.SkipStartSize() && messageIndex == m.GetSkipStart(skipIndex)) {
                                messageIndex += m.GetSkipLength(skipIndex);
                                ++skipIndex;
                            }
                            if (messageIndex >= TaskData.Batch.size()) {
                                throw TProtocolViolationException(grpc::INVALID_ARGUMENT,
                                    Sprintf("invalid meta, message index [%lu], "
                                            "messages count [%lu]",
                                            messageIndex, TaskData.Batch.size()));
                            }
                            TaskData.Batch[messageIndex].Message->Meta[m.GetKey()] = m.GetValue(i);
                            ++messageIndex;
                        }
                    }

                    if (ParseReceiptTimestampFromMeta) {
                        for (const auto& item: TaskData.Batch) {
                            auto& m = *item.Message;
                            if (const auto* value = m.Meta.Find(TMessage::ReceiptTimestampMetaKey); value) {
                                m.ReceiptTimestamp = TInstant::MicroSeconds(FromString<ui64>(*value));
                                m.Meta.Erase(TMessage::ReceiptTimestampMetaKey);
                            }
                        }
                    }
                }

                void SubmitBatch() {
                    const auto batchSize = Min(TaskData.Batch.size() - TaskData.NextMessageIndex, MaxBatchSize);
                    Y_VERIFY(batchSize > 0);
                    const auto batch = TArrayRef(TaskData.Batch.data() + TaskData.NextMessageIndex, batchSize);
                    Y_VERIFY(TaskData.MessageConsumer->Submit(batch).HasValue(), "suspend/resume is not supported");
                    YLOG_DEBUG(Sprintf("batch [%lu-%lu] submitted",
                                       batch.begin()->SeqNo.Value, batch.rbegin()->SeqNo.Value));
                    TaskData.NextMessageIndex += batchSize;
                    if (TaskData.NextMessageIndex == TaskData.Batch.size()) {
                        TaskData.Batch.clear();
                        TaskData.NextMessageIndex = 0;
                    }
                }

            private:
                static constexpr size_t MaxBatchSize = 1000;

                class TProtocolViolationException: public yexception {
                public:
                    TProtocolViolationException(grpc::StatusCode code, const grpc::string& message)
                        : Code(code)
                        , Message(message)
                    {
                    }

                    grpc::StatusCode Code;
                    grpc::string Message;
                };

                class TPoisonedException: public yexception {
                };

                enum class EState {
                    Initialize,
                    PrepareBatch,
                    SubmitBatch,
                    Error
                };

                struct TTaskData {
                    TVector<TReceivedRequest>* Requests{nullptr};
                    IMessageConsumer* MessageConsumer{nullptr};
                    size_t NextRequestIndex{0};
                    size_t NextMessageIndex{0};
                    EState State{EState::Initialize};
                    TVector<TSeqItem> Batch{Reserve(MaxBatchSize)};

                    inline bool HasNextMessage() const noexcept {
                        return NextMessageIndex < Batch.size();
                    }

                    inline bool HasNextRequest() const noexcept {
                        return Requests != nullptr && NextRequestIndex < Requests->size();
                    }

                    inline TReceivedRequest ExtractRequest() {
                        Y_VERIFY(NextRequestIndex < Requests->size());
                        return std::move((*Requests)[NextRequestIndex++]);
                    }
                };

            private:
                TGrpcSession& Session;
                TIntrusivePtr<TTaskHandle> SubmitTask;
                TScopeLogger Logger;
                TFMaybe<TString> SessionId;
                IMessageConsumer* MessageConsumer;
                TFMaybe<TSeqNo> LastSeqNo;
                TSwapQueue<TReceivedRequest> Queue;
                TTaskData TaskData;
                const bool ParseReceiptTimestampFromMeta;
                TFMaybe<TSessionIdsPool::TSessionKey> SessionReuseKey;
                bool ClientCancelled;
                TSessionIdsPool& SessionIdsPool;
                TAdaptiveLock Lock;
            };

        private:
            NUnifiedAgentProto::UnifiedAgentService::AsyncService Service;
            std::unique_ptr<grpc::Server> Server;
            std::unique_ptr<grpc::ServerCompletionQueue> IOCompletionQueue;
            TFMaybe<TGrpcCompletionQueuePoller> IOPoller;
            std::atomic<size_t> SessionLogLabel;
            std::atomic<bool> Stopping;
            THashSet<TIntrusivePtr<TGrpcSession>> ActiveSessions;
            TAsyncJoiner SessionsJoiner;
            TString SharedSecretKey;
            TSessionIdsPool SessionIdsPool;
            TAdaptiveLock Lock;
        };
    }

    REGISTER_PLUGIN(TPlugin, "grpc")
}
