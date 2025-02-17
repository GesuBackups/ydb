#include "defrag_rewriter.h"
#include <ydb/core/blobstorage/vdisk/skeleton/blobstorage_takedbsnap.h>

namespace NKikimr {

    ////////////////////////////////////////////////////////////////////////////
    // TDefragRewriter
    ////////////////////////////////////////////////////////////////////////////
    class TDefragRewriter : public TActorBootstrapped<TDefragRewriter>
    {
        friend class TActorBootstrapped<TDefragRewriter>;

        std::shared_ptr<TDefragCtx> DCtx;
        const TVDiskID SelfVDiskId;
        const TActorId NotifyId;
        // we can rewrite data while we are holding snapshot for data being read
        std::optional<THullDsSnap> FullSnap;
        std::vector<TDefragRecord> Recs;
        size_t RecToReadIdx = 0;
        size_t RewrittenRecsCounter = 0;
        size_t RewrittenBytes = 0;

        void Bootstrap(const TActorContext &ctx) {
            SendNextRead(ctx);
            Become(&TThis::StateFunc);
        }

        void SendNextRead(const TActorContext &ctx) {
            if (RecToReadIdx < Recs.size()) {
                ctx.Send(DCtx->SkeletonId, new TEvTakeHullSnapshot(false));
            } else if (RewrittenRecsCounter == Recs.size()) {
                ctx.Send(NotifyId, new TEvDefragRewritten(RewrittenRecsCounter, RewrittenBytes));
                Die(ctx);
            }
        }

        void Handle(TEvTakeHullSnapshotResult::TPtr ev, const TActorContext& ctx) {
            FullSnap.emplace(std::move(ev->Get()->Snap));
            FullSnap->BlocksSnap.Destroy();
            FullSnap->BarriersSnap.Destroy();

            TLogoBlobsSnapshot::TForwardIterator iter(FullSnap->HullCtx, &FullSnap->LogoBlobsSnap);
            const TLogoBlobID id = Recs[RecToReadIdx].LogoBlobId;
            iter.Seek(id.FullID());
            if (iter.Valid() && iter.GetCurKey().LogoBlobID() == id.FullID()) {
                struct TCallback {
                    const ui32 PartId;
                    TDiskPart Location;

                    void operator ()(const TDiskPart& data, const NMatrix::TVectorType v) {
                        if (v.Get(PartId - 1)) {
                            Y_VERIFY(v == NMatrix::TVectorType::MakeOneHot(PartId - 1, v.GetSize()));
                            Location = data;
                        }
                    }

                    void operator ()(const TDiskBlob&) {}
                } callback{id.PartId(), {}};
                TRecordMergerCallback<TKeyLogoBlob, TMemRecLogoBlob, TCallback> merger(&callback, DCtx->VCtx->Top->GType);
                iter.PutToMerger(&merger);
                merger.Finish();

                const TDiskPart &p = callback.Location;
                auto msg = std::make_unique<NPDisk::TEvChunkRead>(DCtx->PDiskCtx->Dsk->Owner,
                    DCtx->PDiskCtx->Dsk->OwnerRound, p.ChunkIdx, p.Offset, p.Size, NPriRead::HullComp, nullptr);
                ctx.Send(DCtx->PDiskCtx->PDiskId, msg.release());
                DCtx->DefragMonGroup.DefragBytesRewritten() += p.Size;
                RewrittenBytes += p.Size;
            } else {
                ++RecToReadIdx;
                ++RewrittenRecsCounter;
                SendNextRead(ctx);
            }
        }

        void Handle(NPDisk::TEvChunkReadResult::TPtr ev, const TActorContext& ctx) {
            FullSnap.reset();

            // FIXME: handle read errors gracefully
            CHECK_PDISK_RESPONSE(DCtx->VCtx, ev, ctx);

            auto *msg = ev->Get();
            const TDefragRecord &rec = Recs[RecToReadIdx++];

            const auto &gtype = DCtx->VCtx->Top->GType;
            ui8 partId = rec.LogoBlobId.PartId();
            Y_VERIFY(partId);

            TString data = msg->Data.ToString();
            Y_VERIFY(data.size() == TDiskBlob::HeaderSize + gtype.PartSize(rec.LogoBlobId));
            const char *header = data.data();

            ui32 fullDataSize;
            memcpy(&fullDataSize, header, sizeof(fullDataSize));
            header += sizeof(fullDataSize);
            Y_VERIFY(fullDataSize == rec.LogoBlobId.BlobSize());

            Y_VERIFY(NMatrix::TVectorType::MakeOneHot(partId - 1, gtype.TotalPartCount()).Raw() == static_cast<ui8>(*header));

            TRope rope(data);
            rope.EraseFront(TDiskBlob::HeaderSize);

            auto writeEvent = std::make_unique<TEvBlobStorage::TEvVPut>(rec.LogoBlobId, std::move(rope),
                    SelfVDiskId, true, nullptr, TInstant::Max(), NKikimrBlobStorage::EPutHandleClass::AsyncBlob);
            writeEvent->RewriteBlob = true;
            Send(DCtx->SkeletonId, writeEvent.release());
        }

        void Handle(TEvBlobStorage::TEvVPutResult::TPtr& /*ev*/, const TActorContext& ctx) {
            // this message is received when huge blob is written by Skeleton
            // FIXME: Handle NotOK, in case of RACE just cancel the job

            ++RewrittenRecsCounter;
            SendNextRead(ctx);
        }

        void HandlePoison(TEvents::TEvPoisonPill::TPtr &ev, const TActorContext &ctx) {
            Y_UNUSED(ev);
            Die(ctx);
        }

        STRICT_STFUNC(StateFunc,
            HFunc(TEvents::TEvPoisonPill, HandlePoison)
            HFunc(TEvTakeHullSnapshotResult, Handle)
            HFunc(NPDisk::TEvChunkReadResult, Handle)
            HFunc(TEvBlobStorage::TEvVPutResult, Handle)
        )

        PDISK_TERMINATE_STATE_FUNC_DEF;

    public:
        static constexpr NKikimrServices::TActivity::EType ActorActivityType() {
            return NKikimrServices::TActivity::BS_DEFRAG_REWRITER;
        }

        TDefragRewriter(
                const std::shared_ptr<TDefragCtx> &dCtx,
                const TVDiskID &selfVDiskId,
                const TActorId &notifyId,
                std::vector<TDefragRecord> &&recs)
            : TActorBootstrapped<TDefragRewriter>()
            , DCtx(dCtx)
            , SelfVDiskId(selfVDiskId)
            , NotifyId(notifyId)
            , Recs(std::move(recs))
        {}
    };

    ////////////////////////////////////////////////////////////////////////////
    // VDISK DEFRAG REWRITER
    // Rewrites selected huge blobs to free up some Huge Heap chunks
    ////////////////////////////////////////////////////////////////////////////
    IActor* CreateDefragRewriter(
            const std::shared_ptr<TDefragCtx> &dCtx,
            const TVDiskID &selfVDiskId,
            const TActorId &notifyId,
            std::vector<TDefragRecord> &&recs) {
        return new TDefragRewriter(dCtx, selfVDiskId, notifyId, std::move(recs));
    }

} // NKikimr
