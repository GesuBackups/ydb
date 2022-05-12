#include "connection.h"
#include "config.h"
#include "server.h"
#include "dispatcher_impl.h"

#include <unistd.h>
#include <yt/yt/core/misc/enum.h>
#include <yt/yt/core/misc/fs.h>
#include <yt/yt/core/misc/proc.h>
#include <yt/yt/core/misc/string.h>

#include <yt/yt/core/net/socket.h>
#include <yt/yt/core/net/dialer.h>

#include <yt/yt/core/concurrency/thread_affinity.h>

#include <yt/yt/core/ytree/convert.h>
#include <yt/yt/core/ytree/fluent.h>

#include <yt/yt/core/profiling/timing.h>

#include <util/system/error.h>
#include <util/system/guard.h>

#ifdef __linux__
#include <netinet/tcp.h>
#endif

#include <cerrno>

namespace NYT::NBus {

using namespace NConcurrency;
using namespace NFS;
using namespace NNet;
using namespace NYTree;
using namespace NYson;
using namespace NYTAlloc;
using namespace NProfiling;

////////////////////////////////////////////////////////////////////////////////

static constexpr size_t ReadBufferSize = 16_KB;
static constexpr size_t MaxBatchReadSize = 64_KB;
static constexpr auto ReadTimeWarningThreshold = TDuration::MilliSeconds(100);

static constexpr size_t MaxFragmentsPerWrite = 256;
static constexpr size_t WriteBufferSize = 16_KB;
static constexpr size_t MaxBatchWriteSize = 64_KB;
static constexpr size_t MaxWriteCoalesceSize = 1_KB;
static constexpr auto WriteTimeWarningThreshold = TDuration::MilliSeconds(100);

static constexpr auto TcpStatisticsUpdatePeriod = TDuration::Seconds(1);

////////////////////////////////////////////////////////////////////////////////

struct TTcpConnectionReadBufferTag { };
struct TTcpConnectionWriteBufferTag { };

////////////////////////////////////////////////////////////////////////////////

bool TTcpConnection::TPacket::MarkEncoded()
{
    auto expected = EPacketState::Queued;
    return State.compare_exchange_strong(expected, EPacketState::Encoded);
}

void TTcpConnection::TPacket::OnCancel(const TError& /* error */)
{
    auto expected = EPacketState::Queued;
    if (!State.compare_exchange_strong(expected, EPacketState::Canceled)) {
        return;
    }

    Message.Reset();
    if (Connection) {
        Connection->UpdatePendingOut(-1, -PacketSize);
    }
}

void TTcpConnection::TPacket::EnableCancel(TTcpConnectionPtr connection)
{
    Connection = std::move(connection);
    if (!Promise.OnCanceled(BIND(&TPacket::OnCancel, MakeWeak(this)))) {
        OnCancel(TError());
    }
}

////////////////////////////////////////////////////////////////////////////////

TTcpConnection::TTcpConnection(
    TTcpBusConfigPtr config,
    EConnectionType connectionType,
    const TString& networkName,
    TConnectionId id,
    int socket,
    const TString& endpointDescription,
    const IAttributeDictionary& endpointAttributes,
    const TNetworkAddress& endpointNetworkAddress,
    const std::optional<TString>& endpointAddress,
    const std::optional<TString>& unixDomainSocketPath,
    IMessageHandlerPtr handler,
    IPollerPtr poller)
    : Config_(std::move(config))
    , ConnectionType_(connectionType)
    , Id_(id)
    , EndpointDescription_(endpointDescription)
    , EndpointAttributes_(endpointAttributes.Clone())
    , EndpointNetworkAddress_(endpointNetworkAddress)
    , EndpointAddress_(endpointAddress)
    , UnixDomainSocketPath_(unixDomainSocketPath)
    , Handler_(std::move(handler))
    , Poller_(std::move(poller))
    , Logger(BusLogger.WithTag("ConnectionId: %v, RemoteAddress: %v",
        Id_,
        EndpointDescription_))
    , LoggingTag_(Format("ConnectionId: %v", Id_))
    , NetworkName_(networkName)
    , GenerateChecksums_(Config_->GenerateChecksums)
    , Socket_(socket)
    , Decoder_(Logger, Config_->VerifyChecksums)
    , ReadStallTimeout_(NProfiling::DurationToCpuDuration(Config_->ReadStallTimeout))
    , Encoder_(Logger)
    , WriteStallTimeout_(NProfiling::DurationToCpuDuration(Config_->WriteStallTimeout))
{ }

TTcpConnection::~TTcpConnection()
{
    Close();
}

void TTcpConnection::Close()
{
    {
        auto guard = Guard(Lock_);

        if (Error_.Load().IsOK()) {
            Error_.Store(TError(NBus::EErrorCode::TransportError, "Bus terminated")
                << *EndpointAttributes_);
        }

        if (State_ == EState::Open) {
            UpdateConnectionCount(-1);
        }

        UpdateTcpStatistics(true);

        UnarmPoller();

        CloseSocket();

        State_ = EState::Closed;
        PendingControl_.store(static_cast<ui64>(EPollControl::Offline));
    }

    DiscardOutcomingMessages();
    DiscardUnackedMessages();

    while (!QueuedPackets_.empty()) {
        const auto& packet = QueuedPackets_.front();
        if (packet->Connection) {
            packet->OnCancel(TError());
        } else {
            UpdatePendingOut(-1, -packet->PacketSize);
        }
        QueuedPackets_.pop();
    }

    while (!EncodedPackets_.empty()) {
        const auto& packet = EncodedPackets_.front();
        UpdatePendingOut(-1, -packet->PacketSize);
        EncodedPackets_.pop();
    }

    EncodedFragments_.clear();
}

void TTcpConnection::Start()
{
    // Offline in PendingControl_ prevents retrying events until end of Open().
    YT_VERIFY(Any(static_cast<EPollControl>(PendingControl_.load()) & EPollControl::Offline));

    if (!Poller_->TryRegister(this)) {
        Abort(TError("Cannot register connection pollable"));
        return;
    }

    TTcpDispatcher::TImpl::Get()->RegisterConnection(this);
    InitBuffers();

    switch (ConnectionType_) {
        case EConnectionType::Client:
            YT_VERIFY(Socket_ == INVALID_SOCKET);
            State_ = EState::Resolving;
            ResolveAddress();
            break;

        case EConnectionType::Server: {
            auto guard = Guard(Lock_);
            YT_VERIFY(Socket_ != INVALID_SOCKET);
            State_ = EState::Opening;
            SetupNetwork(NetworkName_);
            Open();

            guard.Release();
            ReadyPromise_.TrySet();
            break;
        }

        default:
            YT_ABORT();
    }
}

void TTcpConnection::CheckLiveness()
{
    if (State_ != EState::Open) {
        return;
    }

    auto now = NProfiling::GetCpuInstant();

    if (LastIncompleteWriteTime_.load(std::memory_order_relaxed) < now - WriteStallTimeout_) {
        Counters_->StalledWrites.fetch_add(1, std::memory_order_relaxed);
        Terminate(TError(
            NBus::EErrorCode::TransportError,
            "Socket write stalled")
            << TErrorAttribute("timeout", Config_->WriteStallTimeout)
            << TErrorAttribute("pending_control", static_cast<EPollControl>(PendingControl_.load())));
        return;
    }

    if (LastIncompleteReadTime_.load(std::memory_order_relaxed) < now - ReadStallTimeout_) {
        Counters_->StalledReads.fetch_add(1, std::memory_order_relaxed);
        Terminate(TError(
            NBus::EErrorCode::TransportError,
            "Socket read stalled")
            << TErrorAttribute("timeout", Config_->ReadStallTimeout)
            << TErrorAttribute("pending_control", static_cast<EPollControl>(PendingControl_.load())));
        return;
    }
}

const TString& TTcpConnection::GetLoggingTag() const
{
    return LoggingTag_;
}

void TTcpConnection::UpdateConnectionCount(int delta)
{
    VERIFY_SPINLOCK_AFFINITY(Lock_);

    switch (ConnectionType_) {
        case EConnectionType::Client:
            Counters_->ClientConnections.fetch_add(delta, std::memory_order_relaxed);
            break;

        case EConnectionType::Server:
            Counters_->ServerConnections.fetch_add(delta, std::memory_order_relaxed);
            break;

        default:
            YT_ABORT();
    }
}

void TTcpConnection::UpdatePendingOut(int countDelta, i64 sizeDelta)
{
    Counters_->PendingOutPackets.fetch_add(countDelta, std::memory_order_relaxed);
    Counters_->PendingOutBytes.fetch_add(sizeDelta, std::memory_order_relaxed);
}

TConnectionId TTcpConnection::GetId() const
{
    return Id_;
}

void TTcpConnection::Open()
{
    State_ = EState::Open;

    YT_LOG_DEBUG("Connection established (LocalPort: %v)", GetSocketPort());

    if (LastIncompleteWriteTime_ != std::numeric_limits<NProfiling::TCpuInstant>::max()) {
        // Rewind stall detection if already armed by pending send
        LastIncompleteWriteTime_ = NProfiling::GetCpuInstant();
    }

    UpdateConnectionCount(+1);

    // Go online and start event processing.
    auto previousPendingControl = static_cast<EPollControl>(PendingControl_.fetch_and(~static_cast<ui64>(EPollControl::Offline)));

    ArmPoller();

    // Something might be pending already, for example Terminate.
    if (Any(previousPendingControl & ~EPollControl::Offline)) {
        YT_LOG_TRACE("Retrying event processing for Open (PendingControl: %v)", previousPendingControl);
        Poller_->Retry(this);
    }
}

void TTcpConnection::ResolveAddress()
{
    if (UnixDomainSocketPath_) {
        if (!IsLocalBusTransportEnabled()) {
            Abort(TError(NBus::EErrorCode::TransportError, "Local bus transport is not available"));
            return;
        }

        NetworkName_ = LocalNetworkName;
        // NB(gritukan): Unix domain socket path cannot be longer than 108 symbols, so let's try to shorten it.
        OnAddressResolved(
            TNetworkAddress::CreateUnixDomainSocketAddress(GetShortestPath(*UnixDomainSocketPath_)));
    } else {
        TStringBuf hostName;
        try {
            ParseServiceAddress(*EndpointAddress_, &hostName, &Port_);
        } catch (const std::exception& ex) {
            Abort(TError(ex).SetCode(NBus::EErrorCode::TransportError));
            return;
        }

        TAddressResolver::Get()->Resolve(TString(hostName)).Subscribe(
            BIND(&TTcpConnection::OnAddressResolveFinished, MakeStrong(this))
                .Via(Poller_->GetInvoker()));
    }
}

void TTcpConnection::OnAddressResolveFinished(const TErrorOr<TNetworkAddress>& result)
{
    if (!result.IsOK()) {
        Abort(result);
        return;
    }

    TNetworkAddress address(result.Value(), Port_);

    YT_LOG_DEBUG("Connection network address resolved (Address: %v)",
        address);

    OnAddressResolved(address);
}

void TTcpConnection::OnAddressResolved(
    const TNetworkAddress& address)
{
    State_ = EState::Opening;
    SetupNetwork(NetworkName_);
    ConnectSocket(address);
}

void TTcpConnection::SetupNetwork(const TString& networkName)
{
    YT_VERIFY(!Counters_);

    YT_LOG_DEBUG("Using %Qv network", networkName);

    Counters_ = TTcpDispatcher::TImpl::Get()->GetCounters(networkName);

    // Suppress checksum generation for local traffic.
    if (networkName == LocalNetworkName) {
        GenerateChecksums_ = false;
    }
}

void TTcpConnection::Abort(const TError& error)
{
    // Fast path.
    if (State_ == EState::Aborted || State_ == EState::Closed) {
        return;
    }

    // Construct a detailed error.
    YT_VERIFY(!error.IsOK());
    auto detailedError = error << *EndpointAttributes_;

    {
        auto guard = Guard(Lock_);

        if (State_ == EState::Aborted || State_ == EState::Closed) {
            return;
        }

        UnarmPoller();

        if (State_ == EState::Open) {
            UpdateConnectionCount(-1);
        }

        State_ = EState::Aborted;

        Error_.Store(detailedError);

        // Prevent starting new OnSocketRead/OnSocketWrite and Retry.
        // Already running will continue, Unregister will drain them.
        PendingControl_.fetch_or(static_cast<ui64>(EPollControl::Shutdown));
    }

    YT_LOG_DEBUG(detailedError, "Connection aborted");

    // OnShutdown() will be called after draining events from thread pools.
    Poller_->Unregister(this);

    ReadyPromise_.TrySet(detailedError);
}

void TTcpConnection::InitBuffers()
{
    ReadBuffer_ = TBlob(TTcpConnectionReadBufferTag(), ReadBufferSize, false);

    WriteBuffers_.push_back(std::make_unique<TBlob>(TTcpConnectionWriteBufferTag()));
    WriteBuffers_[0]->Reserve(WriteBufferSize);
}

int TTcpConnection::GetSocketPort()
{
    TNetworkAddress address;
    auto* sockAddr = address.GetSockAddr();
    socklen_t sockAddrLen = address.GetLength();
    int result = getsockname(Socket_, sockAddr, &sockAddrLen);
    if (result < 0) {
        return -1;
    }

    switch (sockAddr->sa_family) {
        case AF_INET:
            return ntohs(reinterpret_cast<sockaddr_in*>(sockAddr)->sin_port);

        case AF_INET6:
            return ntohs(reinterpret_cast<sockaddr_in6*>(sockAddr)->sin6_port);

        default:
            return -1;
    }
}

void TTcpConnection::ConnectSocket(const TNetworkAddress& address)
{
    auto dialer = CreateAsyncDialer(
        Config_,
        Poller_,
        Logger);
    DialerSession_ = dialer->CreateSession(
        address,
        BIND(&TTcpConnection::OnDialerFinished, MakeWeak(this)));
    DialerSession_->Dial();
}

void TTcpConnection::OnDialerFinished(const TErrorOr<SOCKET>& socketOrError)
{
    DialerSession_.Reset();

    if (!socketOrError.IsOK()) {
        Abort(TError(
            NBus::EErrorCode::TransportError,
            "Error connecting to %v",
            EndpointDescription_)
            << socketOrError);
        return;
    }

    {
        auto guard = Guard(Lock_);

        YT_VERIFY(State_ == EState::Opening);

        Socket_ = socketOrError.Value();

        auto tosLevel = TosLevel_.load();
        if (tosLevel != DefaultTosLevel) {
            InitSocketTosLevel(tosLevel);
        }

        Open();

        guard.Release();
        ReadyPromise_.TrySet();
    }
}

const TString& TTcpConnection::GetEndpointDescription() const
{
    return EndpointDescription_;
}

const IAttributeDictionary& TTcpConnection::GetEndpointAttributes() const
{
    return *EndpointAttributes_;
}

const TString& TTcpConnection::GetEndpointAddress() const
{
    if (EndpointAddress_) {
        return *EndpointAddress_;
    } else {
        static const TString EmptyAddress;
        return EmptyAddress;
    }
}

const TNetworkAddress& TTcpConnection::GetEndpointNetworkAddress() const
{
    return EndpointNetworkAddress_;
}

TTcpDispatcherStatistics TTcpConnection::GetStatistics() const
{
    return Counters_->ToStatistics();
}

TFuture<void> TTcpConnection::GetReadyFuture() const
{
    return ReadyPromise_.ToFuture();
}

TFuture<void> TTcpConnection::Send(TSharedRefArray message, const TSendOptions& options)
{
    TTcpDispatcher::TImpl::Get()->ValidateNetworkingNotDisabled(EMessageDirection::Outcoming);

    if (message.Size() > MaxMessagePartCount) {
        return MakeFuture<void>(TError(
            NRpc::EErrorCode::TransportError,
            "Message exceeds part count limit: %v > %v",
            message.Size(),
            MaxMessagePartCount));
    }

    for (size_t index = 0; index < message.Size(); ++index) {
        const auto& part = message[index];
        if (part.Size() > MaxMessagePartSize) {
            return MakeFuture<void>(TError(
                NRpc::EErrorCode::TransportError,
                "Message part %v exceeds size limit: %v > %v",
                index,
                part.Size(),
                MaxMessagePartSize));
        }
    }

    TQueuedMessage queuedMessage(std::move(message), options);

    auto pendingOutPayloadBytes = PendingOutPayloadBytes_.fetch_add(queuedMessage.PayloadSize);

    // Log first to avoid producing weird traces.
    YT_LOG_DEBUG("Outcoming message enqueued (PacketId: %v, PendingOutPayloadBytes: %v)",
        queuedMessage.PacketId,
        pendingOutPayloadBytes);

    if (LastIncompleteWriteTime_ == std::numeric_limits<NProfiling::TCpuInstant>::max()) {
        // Arm stall detection.
        LastIncompleteWriteTime_ = NProfiling::GetCpuInstant();
    }

    QueuedMessages_.Enqueue(queuedMessage);

    // Wake up the event processing if needed.
    {
        auto previousPendingControl = static_cast<EPollControl>(PendingControl_.fetch_or(static_cast<ui64>(EPollControl::Write)));
        if (None(previousPendingControl)) {
            YT_LOG_TRACE("Retrying event processing for Send");
            Poller_->Retry(this);
        }
    }

    // Double-check the state not to leave any dangling outcoming messages.
    if (State_.load() == EState::Closed) {
        DiscardOutcomingMessages();
    }

    return queuedMessage.Promise;
}

void TTcpConnection::SetTosLevel(TTosLevel tosLevel)
{
    if (TosLevel_.load() == tosLevel) {
        return;
    }

    {
        auto guard = Guard(Lock_);
        if (Socket_ != INVALID_SOCKET) {
            InitSocketTosLevel(tosLevel);
        }
    }

    TosLevel_.store(tosLevel);
}

void TTcpConnection::Terminate(const TError& error)
{
    // Construct a detailed error.
    YT_VERIFY(!error.IsOK());
    auto detailedError = error << *EndpointAttributes_;

    auto guard = Guard(Lock_);

    if (!Error_.Load().IsOK() ||
        State_ == EState::Aborted ||
        State_ == EState::Closed)
    {
        YT_LOG_DEBUG("Connection is already terminated, termination request ignored (State: %v, PendingControl: %v, PendingOutPayloadBytes: %v)",
            State_.load(),
            static_cast<EPollControl>(PendingControl_.load()),
            PendingOutPayloadBytes_.load());
        return;
    }

    YT_LOG_DEBUG("Sending termination request");

    // Save error for OnTerminate().
    Error_.Store(detailedError);

    // Arm calling OnTerminate() from OnEvent().
    auto previousPendingControl = static_cast<EPollControl>(PendingControl_.fetch_or(static_cast<ui64>(EPollControl::Terminate)));

    // To recover from bogus state always retry processing unless socket is offline
    if (None(previousPendingControl & EPollControl::Offline)) {
        YT_LOG_TRACE("Retrying event processing for Terminate (PendingControl: %v)", previousPendingControl);
        Poller_->Retry(this);
    }
}

void TTcpConnection::SubscribeTerminated(const TCallback<void(const TError&)>& callback)
{
    Terminated_.Subscribe(callback);
}

void TTcpConnection::UnsubscribeTerminated(const TCallback<void(const TError&)>& callback)
{
    Terminated_.Unsubscribe(callback);
}

void TTcpConnection::OnEvent(EPollControl control)
{
    EPollControl action;
    {
        auto rawPendingControl = PendingControl_.load(std::memory_order_acquire);
        while (true) {
            auto pendingControl = static_cast<EPollControl>(rawPendingControl);
            // New events could come while previous handler is still running.
            if (Any(pendingControl & (EPollControl::Running | EPollControl::Shutdown))) {
                if (!PendingControl_.compare_exchange_weak(rawPendingControl, static_cast<ui64>(pendingControl | control))) {
                    continue;
                }
                // CAS succeded, bail out.
                YT_LOG_TRACE("Event handler is already running (PendingControl: %v)",
                    pendingControl);
                return;
            }

            action = pendingControl | control;

            // Clear Read/Write before operation. Consequent event will raise it
            // back and retry handling. OnSocketRead() always consumes all backlog
            // or aborts connection if something went wrong, othwewise if somehting
            // left then handling should raise Read in PendingControl_ back.
            if (!PendingControl_.compare_exchange_weak(rawPendingControl, static_cast<ui64>(EPollControl::Running))) {
                continue;
            }

            // CAS succeded, bail out.
            break;
        }
    }

    // OnEvent should never be called for an offline socket.
    YT_VERIFY(None(action & EPollControl::Offline));

    if (Any(action & EPollControl::Terminate)) {
        OnTerminate();
        // Leave Running flag set in PendingControl_ to drain further events and
        // prevent Retry which could race with Unregister()/OnShutdown().
        return;
    }

    YT_LOG_TRACE("Event processing started");

    // NB: Try to read from the socket before writing into it to avoid
    // getting SIGPIPE when the other party closes the connection.
    if (Any(action & EPollControl::Read)) {
        OnSocketRead();
    }

    if (State_ == EState::Open) {
        ProcessQueuedMessages();
        OnSocketWrite();
    }

    YT_LOG_TRACE("Event processing finished (HasUnsentData: %v)",
        HasUnsentData());

    // Finaly, clear Running flag and recheck new pending events.
    //
    // Looping here around one pollable could cause starvation for others and
    // increase latency for events already picked by this thread. So, put it
    // away into retry queue without waking other threads. This or any other
    // thread will handle it on next iteration after handling picked events.
    //
    // Do not retry processing if socket is already started shutdown sequence.
    // Retry request could be picked by thread which already passed draining.
    {
        auto previousPendingControl = static_cast<EPollControl>(PendingControl_.fetch_and(~static_cast<ui64>(EPollControl::Running)));
        YT_ASSERT(Any(previousPendingControl & EPollControl::Running));
        if (Any(previousPendingControl & ~EPollControl::Running) && None(previousPendingControl & EPollControl::Shutdown)) {
            YT_LOG_TRACE("Retrying event processing for OnEvent (PendingControl: %v)", previousPendingControl);
            Poller_->Retry(this, false);
        }
    }
}

void TTcpConnection::OnShutdown()
{
    // Perform the initial cleanup (the final one will be in dtor).
    Close();

    auto error = Error_.Load();
    YT_LOG_DEBUG(error, "Connection terminated");

    Terminated_.Fire(error);
}

void TTcpConnection::OnSocketRead()
{
    YT_LOG_TRACE("Started serving read request");

    TTcpDispatcher::TImpl::Get()->ValidateNetworkingNotDisabled(EMessageDirection::Incoming);

    size_t bytesReadTotal = 0;
    while (true) {
        // Check if the decoder is expecting a chunk of large enough size.
        auto decoderChunk = Decoder_.GetFragment();
        size_t decoderChunkSize = decoderChunk.Size();

        if (decoderChunkSize >= ReadBufferSize) {
            // Read directly into the decoder buffer.
            size_t bytesToRead = std::min(decoderChunkSize, MaxBatchReadSize);
            YT_LOG_TRACE("Reading from socket into decoder (BytesToRead: %v)",bytesToRead);

            size_t bytesRead;
            if (!ReadSocket(decoderChunk.Begin(), bytesToRead, &bytesRead)) {
                break;
            }
            bytesReadTotal += bytesRead;

            if (!AdvanceDecoder(bytesRead)) {
                return;
            }
        } else {
            // Read a chunk into the read buffer.
            YT_LOG_TRACE("Reading from socket into buffer (BytesToRead: %v)", ReadBuffer_.Size());

            size_t bytesRead;
            if (!ReadSocket(ReadBuffer_.Begin(), ReadBuffer_.Size(), &bytesRead)) {
                break;
            }
            bytesReadTotal += bytesRead;

            // Feed the read buffer to the decoder.
            const char* recvBegin = ReadBuffer_.Begin();
            size_t recvRemaining = bytesRead;
            while (recvRemaining != 0) {
                decoderChunk = Decoder_.GetFragment();
                decoderChunkSize = decoderChunk.Size();
                size_t bytesToCopy = std::min(recvRemaining, decoderChunkSize);
                YT_LOG_TRACE("Feeding buffer into decoder (DecoderNeededBytes: %v, RemainingBufferBytes: %v, BytesToCopy: %v)",
                    decoderChunkSize,
                    recvRemaining,
                    bytesToCopy);
                std::copy(recvBegin, recvBegin + bytesToCopy, decoderChunk.Begin());
                if (!AdvanceDecoder(bytesToCopy)) {
                    return;
                }
                recvBegin += bytesToCopy;
                recvRemaining -= bytesToCopy;
            }
            YT_LOG_TRACE("Buffer exhausted");
        }
    }

    LastIncompleteReadTime_ = HasUnreadData()
        ? NProfiling::GetCpuInstant()
        : std::numeric_limits<NProfiling::TCpuInstant>::max();

    YT_LOG_TRACE("Finished serving read request (BytesReadTotal: %v)",
        bytesReadTotal);
}

bool TTcpConnection::HasUnreadData() const
{
    return Decoder_.IsInProgress();
}

bool TTcpConnection::ReadSocket(char* buffer, size_t size, size_t* bytesRead)
{
    NProfiling::TWallTimer timer;
    auto result = HandleEintr(recv, Socket_, buffer, size, 0);
    auto elapsed = timer.GetElapsedTime();
    if (elapsed > ReadTimeWarningThreshold) {
        YT_LOG_DEBUG("Socket read took too long (Elapsed: %v)",
            elapsed);
    }

    if (!CheckReadError(result)) {
        *bytesRead = 0;
        return false;
    }

    *bytesRead = static_cast<size_t>(result);
    Counters_->InBytes.fetch_add(result, std::memory_order_relaxed);

    YT_LOG_TRACE("Socket read (BytesRead: %v)", *bytesRead);

    if (Config_->EnableQuickAck) {
        if (!TrySetSocketEnableQuickAck(Socket_)) {
            YT_LOG_TRACE("Failed to set socket quick ack option");
        }
    }

    return true;
}

bool TTcpConnection::CheckReadError(ssize_t result)
{
    if (result == 0) {
        Abort(TError(NBus::EErrorCode::TransportError, "Socket was closed"));
        return false;
    }

    if (result < 0) {
        int error = LastSystemError();
        if (IsSocketError(error)) {
            Counters_->ReadErrors.fetch_add(1, std::memory_order_relaxed);
            Abort(TError(NBus::EErrorCode::TransportError, "Socket read error")
                << TError::FromSystem(error));
        }
        return false;
    }

    return true;
}

bool TTcpConnection::AdvanceDecoder(size_t size)
{
    if (!Decoder_.Advance(size)) {
        Counters_->DecoderErrors.fetch_add(1, std::memory_order_relaxed);
        Abort(TError(NBus::EErrorCode::TransportError, "Error decoding incoming packet"));
        return false;
    }

    if (Decoder_.IsFinished()) {
        bool result = OnPacketReceived();
        Decoder_.Restart();
        return result;
    }

    return true;
}

bool TTcpConnection::OnPacketReceived() noexcept
{
    Counters_->InPackets.fetch_add(1, std::memory_order_relaxed);
    switch (Decoder_.GetPacketType()) {
        case EPacketType::Ack:
            return OnAckPacketReceived();
        case EPacketType::Message:
            return OnMessagePacketReceived();
        default:
            YT_ABORT();
    }
}

bool TTcpConnection::OnAckPacketReceived()
{
    if (UnackedPackets_.empty()) {
        Abort(TError(NBus::EErrorCode::TransportError, "Unexpected ack received"));
        return false;
    }

    auto& unackedMessage = UnackedPackets_.front();

    if (Decoder_.GetPacketId() != unackedMessage->PacketId) {
        Abort(TError(
            NBus::EErrorCode::TransportError,
            "Ack for invalid packet ID received: expected %v, found %v",
            unackedMessage->PacketId,
            Decoder_.GetPacketId()));
        return false;
    }

    YT_LOG_DEBUG("Ack received (PacketId: %v)", Decoder_.GetPacketId());

    if (unackedMessage->Promise) {
        unackedMessage->Promise.TrySet(TError());
    }

    UnackedPackets_.pop();

    return true;
}

bool TTcpConnection::OnMessagePacketReceived()
{
    YT_LOG_DEBUG("Incoming message received (PacketId: %v, PacketSize: %v, PacketFlags: %v)",
        Decoder_.GetPacketId(),
        Decoder_.GetPacketSize(),
        Decoder_.GetPacketFlags());

    if (Any(Decoder_.GetPacketFlags() & EPacketFlags::RequestAcknowledgement)) {
        EnqueuePacket(EPacketType::Ack, EPacketFlags::None, 0, Decoder_.GetPacketId());
    }

    auto message = Decoder_.GrabMessage();
    Handler_->HandleMessage(std::move(message), this);

    return true;
}

TTcpConnection::TPacket* TTcpConnection::EnqueuePacket(
    EPacketType type,
    EPacketFlags flags,
    int checksummedPartCount,
    TPacketId packetId,
    TSharedRefArray message,
    size_t payloadSize)
{
    size_t packetSize = TPacketEncoder::GetPacketSize(type, message, payloadSize);
    auto& packet = QueuedPackets_.emplace(New<TPacket>(
        type,
        flags,
        checksummedPartCount,
        packetId,
        std::move(message),
        payloadSize,
        packetSize));
    UpdatePendingOut(+1, +packetSize);
    return packet.Get();
}

void TTcpConnection::OnSocketWrite()
{
    YT_LOG_TRACE("Started serving write request");

    size_t bytesWrittenTotal = 0;
    while (true) {
        if (!HasUnsentData()) {
            // Unarm stall detection at end of write
            LastIncompleteWriteTime_ = std::numeric_limits<NProfiling::TCpuInstant>::max();
            break;
        }

        if (!MaybeEncodeFragments()) {
            break;
        }

        size_t bytesWritten;
        bool success = WriteFragments(&bytesWritten);
        bytesWrittenTotal += bytesWritten;

        FlushWrittenFragments(bytesWritten);
        FlushWrittenPackets(bytesWritten);

        if (bytesWritten) {
            // Rearm stall detection after progress.
            LastIncompleteWriteTime_ = NProfiling::GetCpuInstant();
        }

        if (!success) {
            break;
        }
    }

    UpdateTcpStatistics(false);
    YT_LOG_TRACE("Finished serving write request (BytesWrittenTotal: %v)", bytesWrittenTotal);
}

bool TTcpConnection::HasUnsentData() const
{
    return !EncodedFragments_.empty() || !QueuedPackets_.empty() || !EncodedPackets_.empty();
}

bool TTcpConnection::WriteFragments(size_t* bytesWritten)
{
    YT_LOG_TRACE("Writing fragments (EncodedFragments: %v)", EncodedFragments_.size());

    auto fragmentIt = EncodedFragments_.begin();
    auto fragmentEnd = EncodedFragments_.end();

    SendVector_.clear();
    size_t bytesAvailable = MaxBatchWriteSize;

    while (fragmentIt != fragmentEnd &&
           SendVector_.size() < MaxFragmentsPerWrite &&
           bytesAvailable > 0)
    {
        const auto& fragment = *fragmentIt;
        size_t size = std::min(fragment.Size(), bytesAvailable);
        struct iovec item;
        item.iov_base = const_cast<char*>(fragment.Begin());
        item.iov_len = size;
        SendVector_.push_back(item);
        EncodedFragments_.move_forward(fragmentIt);
        bytesAvailable -= size;
    }

    NProfiling::TWallTimer timer;
    auto result = HandleEintr(::writev, Socket_, SendVector_.data(), SendVector_.size());
    auto elapsed = timer.GetElapsedTime();
    if (elapsed > WriteTimeWarningThreshold) {
        YT_LOG_DEBUG("Socket write took too long (Elapsed: %v)",
            elapsed);
    }

    *bytesWritten = result >= 0 ? static_cast<size_t>(result) : 0;
    bool isOK = CheckWriteError(result);
    if (isOK) {
        Counters_->OutBytes.fetch_add(*bytesWritten, std::memory_order_relaxed);
        YT_LOG_TRACE("Socket written (BytesWritten: %v)", *bytesWritten);
    }
    return isOK;
}

void TTcpConnection::FlushWrittenFragments(size_t bytesWritten)
{
    size_t bytesToFlush = bytesWritten;
    YT_LOG_TRACE("Flushing fragments (BytesWritten: %v)", bytesWritten);

    while (bytesToFlush != 0) {
        YT_ASSERT(!EncodedFragments_.empty());
        auto& fragment = EncodedFragments_.front();

        if (fragment.Size() > bytesToFlush) {
            size_t bytesRemaining = fragment.Size() - bytesToFlush;
            YT_LOG_TRACE("Partial write (Size: %v, RemainingSize: %v)",
                fragment.Size(),
                bytesRemaining);
            fragment = TRef(fragment.End() - bytesRemaining, bytesRemaining);
            break;
        }

        YT_LOG_TRACE("Full write (Size: %v)", fragment.Size());

        bytesToFlush -= fragment.Size();
        EncodedFragments_.pop();
    }
}

void TTcpConnection::FlushWrittenPackets(size_t bytesWritten)
{
    size_t bytesToFlush = bytesWritten;
    YT_LOG_TRACE("Flushing packets (BytesWritten: %v)", bytesWritten);

    while (bytesToFlush != 0) {
        YT_ASSERT(!EncodedPacketSizes_.empty());
        auto& packetSize = EncodedPacketSizes_.front();

        if (packetSize > bytesToFlush) {
            size_t bytesRemaining = packetSize - bytesToFlush;
            YT_LOG_TRACE("Partial write (Size: %v, RemainingSize: %v)",
                packetSize,
                bytesRemaining);
            packetSize = bytesRemaining;
            break;
        }

        YT_LOG_TRACE("Full write (Size: %v)", packetSize);

        bytesToFlush -= packetSize;
        OnPacketSent();
        EncodedPacketSizes_.pop();
    }
}

bool TTcpConnection::MaybeEncodeFragments()
{
    if (!EncodedFragments_.empty() || QueuedPackets_.empty()) {
        return true;
    }

    // Discard all buffers except for a single one.
    WriteBuffers_.resize(1);
    auto* buffer = WriteBuffers_.back().get();
    buffer->Clear();

    size_t encodedSize = 0;
    size_t coalescedSize = 0;

    auto flushCoalesced = [&] () {
        if (coalescedSize > 0) {
            EncodedFragments_.push(TRef(buffer->End() - coalescedSize, coalescedSize));
            coalescedSize = 0;
        }
    };

    auto coalesce = [&] (TRef fragment) {
        if (buffer->Size() + fragment.Size() > buffer->Capacity()) {
            // Make sure we never reallocate.
            flushCoalesced();
            WriteBuffers_.push_back(std::make_unique<TBlob>(TTcpConnectionWriteBufferTag()));
            buffer = WriteBuffers_.back().get();
            buffer->Reserve(std::max(WriteBufferSize, fragment.Size()));
        }
        buffer->Append(fragment);
        coalescedSize += fragment.Size();
    };

    while (EncodedFragments_.size() < MaxFragmentsPerWrite &&
           encodedSize <= MaxBatchWriteSize &&
           !QueuedPackets_.empty())
    {
        auto& queuedPacket = QueuedPackets_.front();
        YT_LOG_TRACE("Checking packet cancel state (PacketId: %v)", queuedPacket->PacketId);
        if (!queuedPacket->MarkEncoded()) {
            YT_LOG_TRACE("Packet was canceled (PacketId: %v)", queuedPacket->PacketId);
            QueuedPackets_.pop();
            continue;
        }

        // Move the packet from queued to encoded.
        if (Any(queuedPacket->Flags & EPacketFlags::RequestAcknowledgement)) {
            UnackedPackets_.push(queuedPacket);
        }
        EncodedPackets_.push(std::move(queuedPacket));
        QueuedPackets_.pop();

        const auto& packet = EncodedPackets_.back();

        // Encode the packet.
        YT_LOG_TRACE("Starting encoding packet (PacketId: %v)", packet->PacketId);

        bool encodeResult = Encoder_.Start(
            packet->Type,
            packet->Flags,
            GenerateChecksums_,
            packet->ChecksummedPartCount,
            packet->PacketId,
            packet->Message);
        if (!encodeResult) {
            Counters_->EncoderErrors.fetch_add(1, std::memory_order_relaxed);
            Abort(TError(NBus::EErrorCode::TransportError, "Error encoding outcoming packet"));
            return false;
        }

        do {
            auto fragment = Encoder_.GetFragment();
            if (!Encoder_.IsFragmentOwned() || fragment.Size() <= MaxWriteCoalesceSize) {
                coalesce(fragment);
            } else {
                flushCoalesced();
                EncodedFragments_.push(fragment);
            }
            YT_LOG_TRACE("Fragment encoded (Size: %v)", fragment.Size());
            Encoder_.NextFragment();
        } while (!Encoder_.IsFinished());

        EncodedPacketSizes_.push(packet->PacketSize);
        encodedSize += packet->PacketSize;

        YT_LOG_TRACE("Finished encoding packet (PacketId: %v)", packet->PacketId);
    }

    flushCoalesced();

    return true;
}

bool TTcpConnection::CheckWriteError(ssize_t result)
{
    if (result < 0) {
        int error = LastSystemError();
        if (IsSocketError(error)) {
            Counters_->WriteErrors.fetch_add(1, std::memory_order_relaxed);
            Abort(TError(NBus::EErrorCode::TransportError, "Socket write error")
                << TError::FromSystem(error));
        }
        return false;
    }

    return true;
}

void TTcpConnection::OnPacketSent()
{
    const auto& packet = EncodedPackets_.front();
    switch (packet->Type) {
        case EPacketType::Ack:
            OnAckPacketSent(*packet);
            break;

        case EPacketType::Message:
            OnMessagePacketSent(*packet);
            break;

        default:
            YT_ABORT();
    }


    UpdatePendingOut(-1, -packet->PacketSize);
    Counters_->OutPackets.fetch_add(1, std::memory_order_relaxed);

    EncodedPackets_.pop();
}

void  TTcpConnection::OnAckPacketSent(const TPacket& packet)
{
    YT_LOG_DEBUG("Ack sent (PacketId: %v)",
        packet.PacketId);
}

void TTcpConnection::OnMessagePacketSent(const TPacket& packet)
{
    YT_LOG_DEBUG("Outcoming message sent (PacketId: %v)",
        packet.PacketId);

    PendingOutPayloadBytes_.fetch_sub(packet.PayloadSize);

    // Arm read stall timeout for incoming ACK.
    if (Any(packet.Flags & EPacketFlags::RequestAcknowledgement) &&
        LastIncompleteReadTime_ == std::numeric_limits<NProfiling::TCpuInstant>::max())
    {
        LastIncompleteReadTime_ = NProfiling::GetCpuInstant();
    }
}

void TTcpConnection::OnTerminate()
{
    if (State_ == EState::Aborted || State_ == EState::Closed) {
        return;
    }

    YT_LOG_DEBUG("Termination request received");

    Abort(Error_.Load());
}

void TTcpConnection::ProcessQueuedMessages()
{
    auto messages = QueuedMessages_.DequeueAll();

    for (auto it = messages.rbegin(); it != messages.rend(); ++it) {
        auto& queuedMessage = *it;

        auto packetId = queuedMessage.PacketId;
        auto flags = queuedMessage.Options.TrackingLevel == EDeliveryTrackingLevel::Full
            ? EPacketFlags::RequestAcknowledgement
            : EPacketFlags::None;
        if (queuedMessage.Options.MemoryZone == EMemoryZone::Undumpable) {
            flags |= EPacketFlags::UseUndumpableMemoryZone;
        }

        auto* packet = EnqueuePacket(
            EPacketType::Message,
            flags,
            GenerateChecksums_ ? queuedMessage.Options.ChecksummedPartCount : 0,
            packetId,
            std::move(queuedMessage.Message),
            queuedMessage.PayloadSize);

        packet->Promise = queuedMessage.Promise;
        if (queuedMessage.Options.EnableSendCancelation) {
            packet->EnableCancel(MakeStrong(this));
        }

        YT_LOG_DEBUG("Outcoming message dequeued (PacketId: %v, PacketSize: %v, Flags: %v)",
            packetId,
            packet->PacketSize,
            flags);

        if (queuedMessage.Promise && !queuedMessage.Options.EnableSendCancelation && !Any(flags & EPacketFlags::RequestAcknowledgement)) {
            queuedMessage.Promise.TrySet();
        }
    }
}

void TTcpConnection::DiscardOutcomingMessages()
{
    auto error = Error_.Load();

    auto guard = Guard(QueuedMessagesDiscardLock_);
    auto queuedMessages = QueuedMessages_.DequeueAll();
    guard.Release();

    for (const auto& queuedMessage : queuedMessages) {
        YT_LOG_DEBUG("Outcoming message discarded (PacketId: %v)",
            queuedMessage.PacketId);
        if (queuedMessage.Promise) {
            queuedMessage.Promise.TrySet(error);
        }
    }
}

void TTcpConnection::DiscardUnackedMessages()
{
    auto error = Error_.Load();


    while (!UnackedPackets_.empty()) {
        auto& message = UnackedPackets_.front();
        if (message->Promise) {
            message->Promise.TrySet(error);
        }
        UnackedPackets_.pop();
    }
}

int TTcpConnection::GetSocketError() const
{
    return NNet::GetSocketError(Socket_);
}

bool TTcpConnection::IsSocketError(ssize_t result)
{
    return
        result != EWOULDBLOCK &&
        result != EAGAIN &&
        result != EINPROGRESS;
}

void TTcpConnection::CloseSocket()
{
    VERIFY_SPINLOCK_AFFINITY(Lock_);

    if (Socket_ != INVALID_SOCKET) {
        NNet::CloseSocket(Socket_);
        Socket_ = INVALID_SOCKET;
    }
}

void TTcpConnection::ArmPoller()
{
    VERIFY_SPINLOCK_AFFINITY(Lock_);
    YT_VERIFY(Socket_ != INVALID_SOCKET);

    Poller_->Arm(Socket_, this, EPollControl::Read | EPollControl::Write | EPollControl::EdgeTriggered);
}

void TTcpConnection::UnarmPoller()
{
    VERIFY_SPINLOCK_AFFINITY(Lock_);

    if (Socket_ != INVALID_SOCKET) {
        Poller_->Unarm(Socket_, this);
    }
}

void TTcpConnection::InitSocketTosLevel(TTosLevel tosLevel)
{
    if (TosLevel_ == BlackHoleTosLevel && tosLevel != BlackHoleTosLevel) {
        if (!TrySetSocketInputFilter(Socket_, false)) {
            YT_LOG_DEBUG("Failed to remove socket input filter");
        }
    }

    if (tosLevel == BlackHoleTosLevel) {
        if (TrySetSocketInputFilter(Socket_, true)) {
            YT_LOG_DEBUG("Socket TOS level set to BlackHole");
        } else {
            YT_LOG_DEBUG("Failed to set socket input filter");
        }
    } else if (TrySetSocketTosLevel(Socket_, tosLevel)) {
        YT_LOG_DEBUG("Socket TOS level set (TosLevel: %x)",
            tosLevel);
    } else {
        YT_LOG_DEBUG("Failed to set socket TOS level");
    }
}

void TTcpConnection::UpdateTcpStatistics(bool force)
{
    if (!force) {
        auto now = GetCpuInstant();
        if (CpuDurationToDuration(now - LastTcpStatisticsUpdate_) < TcpStatisticsUpdatePeriod) {
            return;
        }

        LastTcpStatisticsUpdate_ = now;
    }

    if (Socket_ == INVALID_SOCKET) {
        return;
    }

#ifdef _linux_
    tcp_info info;
    socklen_t len = sizeof(info);
    int ret = ::getsockopt(Socket_, IPPROTO_TCP, TCP_INFO, &info, &len);
    if (ret == 0) {
        // Handle counter overflow.
        if (info.tcpi_total_retrans < LastRetransmitCount_) {
            Counters_->Retransmits += info.tcpi_total_retrans + (Max<ui32>() - LastRetransmitCount_);
        } else {
            Counters_->Retransmits += info.tcpi_total_retrans - LastRetransmitCount_;
        }

        LastRetransmitCount_ = info.tcpi_total_retrans;
    }
#else
    Y_UNUSED(LastRetransmitCount_);
#endif
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NBus
