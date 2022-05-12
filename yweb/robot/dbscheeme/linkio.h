#pragma once

#include <util/generic/yexception.h>
#include <library/cpp/microbdb/safeopen.h>
#include "mergecfg.h"
#include "refrecords.h"
#include "linkcfg.h"

struct TLinkIntReaderState {
    template<typename TType>
    struct TFileState {
        typedef TFileState<TType> TSelf;
        i64 PageFirstOffset;
        i64 Offset;
        i64 Left;

        int Page;

    public:
        TFileState() :
            PageFirstOffset(), Offset(), Left(), Page() {
        }

        const TType* Advance(TInDatFile<TType>& file, bool check = false) {
            int page = file.GetPageNum();
            const TType* next = file.Next();

            if (next) {
                ++Offset;
                --Left;
                if(file.GetPageNum() != page) {
                    Page = file.GetPageNum();
                    PageFirstOffset = Offset;
                }
            }

            if (!next && Left && check)
                ythrow yexception () << file.GetName() << " finished unexpectedly left " << Left;

            return next;
        }

        const TType* Rewind(TInDatFile<TType>& file, const TSelf state) {
            if (state.Page == Page && state.Offset == Offset && file.Current()) {
                if (state.Left != Left)
                    ythrow yexception () << Left << " != " << state.Left;
                return file.Current();
            }

            const TType* rec = file.GotoPage(state.Page);

            i64 off = state.PageFirstOffset;
            for (; rec && off < state.Offset; ++off)
                rec = file.Next();

            if (off != state.Offset)
                ythrow yexception() << off << " != " << state.Offset;

            if (file.GetPageNum() != state.Page)
                ythrow yexception() << file.GetPageNum() << " != " << state.Page;

            *this = state;
            return rec;
        }
    };

    typedef TFileState<TLinkIntHostRec> THostState;
    typedef TFileState<TLinkIntDstFnvRec> TDstFnvState;
    typedef TFileState<TLinkIntSrcRec> TSrcState;
    THostState Host;
    TDstFnvState DstFnv;
    TSrcState Src;
};

class TLinkIntDstReader: private TLinkIntReaderState {
private:
    urlid_t Rec;

    TInDatFile<TLinkIntHostRec> HostFile;
    TInDatFile<TLinkIntDstFnvRec> DstFnvFile;

    const TLinkIntHostRec* HostRec;
    const TLinkIntDstFnvRec* DstFnvRec;

public:
    TLinkIntDstReader() :
        HostFile("inthost file", dbcfg::fbufsize, 0), DstFnvFile("intdstfnv file", dbcfg::fbufsize, 0)
    {
        Clear();
    }

    void Clear() {
        *(TLinkIntReaderState*) this = TLinkIntReaderState();
        HostRec = nullptr;
        DstFnvRec = nullptr;
    }

    void Open(const TString& hostFileName, const TString& dstfnvFileName) {
        HostFile.Open(hostFileName);
        DstFnvFile.Open(dstfnvFileName);
        Clear();
    }

    void OpenDefault() {
        Open(linkcfg::fname_inthost, linkcfg::fname_intdstfnv);
    }

    const urlid_t* Next() {
        if (DstFnv.Left == 0) {
            HostRec = Host.Advance(HostFile);

            if (HostRec == nullptr)
                return nullptr;

            DstFnv.Left = HostRec->DstOffset - DstFnv.Offset;
            Rec.HostId = HostRec->HostId;
        }
        DstFnvRec = DstFnv.Advance(DstFnvFile, true);
        Rec.UrlId = DstFnvRec->UrlIdTo;
        return &Rec;
    }

    const urlid_t* Current() {
        return HostRec ? &Rec : nullptr;
    }

    void Close() {
        HostFile.Close();
        DstFnvFile.Close();
    }
};


class TLinkIntReader: private TLinkIntReaderState {
private:
    TLinkIntRec Rec;

    TInDatFile<TLinkIntHostRec> HostFile;
    TInDatFile<TLinkIntDstFnvRec> DstFnvFile;
    TInDatFile<TLinkIntSrcRec> SrcFile;

    const TLinkIntHostRec* HostRec;
    const TLinkIntDstFnvRec* DstFnvRec;
    const TLinkIntSrcRec* SrcRec;

public:
    typedef TLinkIntRec TRec;

    TLinkIntReader() :
        HostFile("inthost file", dbcfg::fbufsize, 0), DstFnvFile("intdstfnv file", dbcfg::fbufsize,
                0), SrcFile("intsrc file", dbcfg::fbufsize, 0) {
        Clear();
    }

    TLinkIntReaderState GetState() const {
        return (TLinkIntReaderState) *this;
    }

    const TLinkIntRec* LoadState(TLinkIntReaderState state) {
        HostRec = Host.Rewind(HostFile, state.Host);
        DstFnvRec = DstFnv.Rewind(DstFnvFile, state.DstFnv);
        SrcRec = Src.Rewind(SrcFile, state.Src);
        *(TLinkIntReaderState*) this = state;
        if(HostRec) {
            Rec.HostId = HostRec->HostId;
            Rec.UrlIdTo = DstFnvRec->UrlIdTo;
            Rec.Fnv = DstFnvRec->Fnv;
            Rec.UrlIdFrom = SrcRec->UrlIdFrom;
            Rec.DateDiscovered = SrcRec->DateDiscovered;
            Rec.Flags = SrcRec->Flags;
        }
        return Current();
    }

    void Clear() {
        *(TLinkIntReaderState*) this = TLinkIntReaderState();
        HostRec = nullptr;
        DstFnvRec = nullptr;
        SrcRec = nullptr;
    }

    void Open(const TString& hostFileName, const TString& dstfnvFileName, const TString& srcFileName) {
        HostFile.Open(hostFileName);
        DstFnvFile.Open(dstfnvFileName);
        SrcFile.Open(srcFileName);
        Clear();
    }

    void OpenDefault() {
        Open(linkcfg::fname_inthost, linkcfg::fname_intdstfnv, linkcfg::fname_intsrc);
    }

    const TLinkIntRec* Next() {
        if (DstFnv.Left == 0 && Src.Left == 0) {
            HostRec = Host.Advance(HostFile);

            if (HostRec == nullptr)
                return nullptr;

            DstFnv.Left = HostRec->DstOffset - DstFnv.Offset;
            Rec.HostId = HostRec->HostId;
        }

        if (Src.Left == 0) {
            DstFnvRec = DstFnv.Advance(DstFnvFile, true);

            Src.Left = DstFnvRec->SrcOffset - Src.Offset;
            Rec.UrlIdTo = DstFnvRec->UrlIdTo;
            Rec.Fnv = DstFnvRec->Fnv;
        }

        SrcRec = Src.Advance(SrcFile, true);
        Rec.UrlIdFrom = SrcRec->UrlIdFrom;
        Rec.DateDiscovered = SrcRec->DateDiscovered;
        Rec.Flags = SrcRec->Flags;

        return &Rec;
    }

    const TLinkIntRec* Current() {
        return HostRec ? &Rec : nullptr;
    }

    void Close() {
        HostFile.Close();
        DstFnvFile.Close();
        SrcFile.Close();
    }
};

class TLinkIntWriter {
private:
    TOutDatFile<TLinkIntHostRec> HostFile;
    TOutDatFile<TLinkIntDstFnvRec, TSnappyCompression> DstFnvFile;
    TOutDatFile<TLinkIntSrcRec, TSnappyCompression> SrcFile;

    ui64 DstFnvOffset;
    ui64 SrcOffset;

    TLinkIntRec lastRec;
    bool hostChanged;
    bool dstChanged;
    bool fnvChanged;

    TLinkIntRec Rec;
protected:
public:

    TLinkIntWriter()
        : HostFile("inthost file", linkcfg::pg_large, dbcfg::fbufsize, 0)
        , DstFnvFile("intdstfnv file", linkcfg::pg_large, dbcfg::fbufsize, 0)
        , SrcFile("intsrc file", linkcfg::pg_large, dbcfg::fbufsize, 0)
    {
        Clear();
    }

    void Clear() {
        DstFnvOffset = 0;
        SrcOffset = 0;
    }

    void Open(const char* hostFileName, const char* dstfnvFileName, const char* srcFileName) {
        HostFile.Open(hostFileName);
        DstFnvFile.Open(dstfnvFileName);
        SrcFile.Open(srcFileName);
        Clear();
    }

    void OpenDefault() {
        Open(linkcfg::fname_newinthost, linkcfg::fname_newintdstfnv, linkcfg::fname_newintsrc);
    }

    void Push(const TLinkIntRec* pRec) {
        if (SrcOffset > 0) {

            if (pRec) {
                hostChanged = pRec->HostId != lastRec.HostId;
                dstChanged = pRec->UrlIdTo != lastRec.UrlIdTo;
                fnvChanged = pRec->Fnv != lastRec.Fnv;
            } else {
                hostChanged = true;
                dstChanged = true;
                fnvChanged = true;
            }

            // this is a new fnv or a new destination
            if (fnvChanged || dstChanged || hostChanged) {
                TLinkIntDstFnvRec* dstfnvRec = DstFnvFile.Reserve(sizeof(TLinkIntDstFnvRec));
                dstfnvRec->UrlIdTo = lastRec.UrlIdTo;
                dstfnvRec->Fnv = lastRec.Fnv;
                dstfnvRec->SrcOffset = SrcOffset;
                DstFnvOffset++;
            }

            // this is a new host
            if (hostChanged) {
                TLinkIntHostRec* hostRec = HostFile.Reserve(sizeof(TLinkIntHostRec));
                hostRec->HostId = lastRec.HostId;
                hostRec->DstOffset = DstFnvOffset;
            }
        }

        if (pRec) {
            // store src
            TLinkIntSrcRec* srcRec = SrcFile.Reserve(sizeof(TLinkIntSrcRec));
            srcRec->UrlIdFrom = pRec->UrlIdFrom;
            srcRec->DateDiscovered = pRec->DateDiscovered;
            srcRec->Flags = pRec->Flags;
            SrcOffset++;
            lastRec = *pRec;
        }
    }

    void Close() {
        Push(nullptr);
        HostFile.Close();
        DstFnvFile.Close();
        SrcFile.Close();
    }
};

class TLinkExtReader {
    TInDatFile<TLinkExtSrcRec> SrcFile;
    TInDatFile<TLinkExtDstRec> DstFile;

    const TLinkExtSrcRec* SrcRec;
    const TLinkExtDstRec* DstRec;

    size_t DstOffset;

    size_t DstLeft;

    TLinkExtRec Rec;
protected:
public:
    typedef TLinkExtRec TRec;

    TLinkExtReader() :
        SrcFile("extsrc file", dbcfg::fbufsize, 0), DstFile("extdst file", dbcfg::fbufsize, 0) {
        Clear();
    }

    void Clear() {
        SrcRec = nullptr;
        DstRec = nullptr;
        DstOffset = 0;
        DstLeft = 0;
    }

    void Open(const char* srcFileName, const char* dstFileName) {
        SrcFile.Open(srcFileName);
        DstFile.Open(dstFileName);
        Clear();
    }

    void OpenDefault() {
        Open(linkcfg::fname_extsrc, linkcfg::fname_extdst);
    }

    const TLinkExtRec* Next() {
        if (DstLeft == 0) {
            SrcRec = SrcFile.Next();
            if (SrcRec == nullptr)
                return nullptr;
            DstLeft = SrcRec->DstOffset - DstOffset;
            Rec.HostIdFrom = SrcRec->HostIdFrom;
            Rec.UrlIdFrom = SrcRec->UrlIdFrom;
        }
        DstRec = DstFile.Next();
        DstLeft--;
        DstOffset++;
        if (DstRec == nullptr)
            ythrow yexception () << "Dst file finished unexpectedly";
        Rec.HostIdTo = DstRec->HostIdTo;
        Rec.UrlIdTo = DstRec->UrlIdTo;
        Rec.Fnv = DstRec->Fnv;
        Rec.DateDiscovered = DstRec->DateDiscovered;
        Rec.DateSynced = DstRec->DateSynced;
        Rec.Flags = DstRec->Flags;
        Rec.SeoFlags = DstRec->SeoFlags;
        return &Rec;
    }

    void Close() {
        SrcFile.Close();
        DstFile.Close();
    }
};

class TLinkExtDstWriter {
private:
    TOutDatFile<TLinkExtDstRec, TSnappyCompression> DstFile;
protected:
    TLinkExtRec lastRec;
public:

    TLinkExtDstWriter()
        : DstFile("intsrc file", linkcfg::pg_large, dbcfg::fbufsize, 0)
    {
    }

    void Open(const char* dstFileName) {
        DstFile.Open(dstFileName);
    }

    void OpenDefault() {
        Open(linkcfg::fname_newextdst);
    }

    void Push(const TLinkExtRec* pRec) {
        Y_ASSERT(pRec);
        TLinkExtDstRec* dstRec = DstFile.Reserve(sizeof(TLinkExtDstRec));
        dstRec->HostIdTo = pRec->HostIdTo;
        dstRec->UrlIdTo = pRec->UrlIdTo;
        dstRec->Fnv = pRec->Fnv;
        dstRec->DateDiscovered = pRec->DateDiscovered;
        dstRec->DateSynced = pRec->DateSynced;
        dstRec->Flags = pRec->Flags;
        dstRec->SeoFlags = pRec->SeoFlags;
        lastRec = *pRec;
    }

    void Close() {
        DstFile.Close();
    }
};

class TLinkExtWriter: protected TLinkExtDstWriter {
private:
    typedef TLinkExtDstWriter TSuper;

    TOutDatFile<TLinkExtSrcRec, TSnappyCompression> SrcFile;
    ui64 DstOffset;
protected:
public:

    TLinkExtWriter()
        : SrcFile("inthost file", linkcfg::pg_large, dbcfg::fbufsize, 0)
    {
        Clear();
    }

    void Clear() {
        DstOffset = 0;
    }

    void Open(const char* srcFileName, const char* dstFileName) {
        SrcFile.Open(srcFileName);
        TSuper::Open(dstFileName);
        Clear();
    }

    void OpenDefault() {
        Open(linkcfg::fname_newextsrc, linkcfg::fname_newextdst);
    }

    void Push(const TLinkExtRec* pRec) {
        if (DstOffset > 0) {
            // this is a new source
            if (!pRec || pRec->HostIdFrom != lastRec.HostIdFrom || pRec->UrlIdFrom
                    != lastRec.UrlIdFrom) {
                TLinkExtSrcRec* srcRec = SrcFile.Reserve(sizeof(TLinkExtSrcRec));
                srcRec->HostIdFrom = lastRec.HostIdFrom;
                srcRec->UrlIdFrom = lastRec.UrlIdFrom;
                srcRec->DstOffset = DstOffset;
            }
        }

        if (pRec) {
            // store src
            TSuper::Push(pRec);
            DstOffset++;
        }
    }

    void Close() {
        Push(nullptr);
        SrcFile.Close();
        TSuper::Close();
    }
};

class TLinkIncWriter {
private:
    TOutDatFile<TLinkIncDstRec> DstFile;
    TOutDatFile<TLinkIncFnvRec> FnvFile;
    TOutDatFile<TLinkIncSrcRec, TSnappyCompression> SrcFile;

    ui32 FnvOffset;
    ui64 SrcOffset;

    TLinkIncRec lastRec;
    bool dstChanged;
    bool fnvChanged;

    TLinkIncRec Rec;
protected:
public:

    TLinkIncWriter()
        : DstFile("dst file", linkcfg::pg_large, dbcfg::fbufsize, 0)
        , FnvFile("fnv file", linkcfg::pg_large, dbcfg::fbufsize, 0)
        , SrcFile("src file", linkcfg::pg_large, dbcfg::fbufsize, 0)
    {
        Clear();
    }

    void Clear() {
        FnvOffset = 0;
        SrcOffset = 0;
    }

    void Open(const char* dstFileName, const char* fnvFileName, const char* srcFileName) {
        DstFile.Open(dstFileName);
        FnvFile.Open(fnvFileName);
        SrcFile.Open(srcFileName);
        Clear();
    }

    void OpenDefault() {
        Open(linkcfg::fname_newincdst, linkcfg::fname_newincfnv, linkcfg::fname_newincsrc);
    }

    void Push(const TLinkIncRec* pRec) {
        if (SrcOffset > 0) {

            if (pRec) {
                dstChanged = pRec->HostIdTo != lastRec.HostIdTo || pRec->UrlIdTo != lastRec.UrlIdTo;
                fnvChanged = pRec->Fnv != lastRec.Fnv;
            } else {
                dstChanged = true;
                fnvChanged = true;
            }

            if (fnvChanged || dstChanged) {
                TLinkIncFnvRec* fnvRec = FnvFile.Reserve(sizeof(TLinkIncFnvRec));
                fnvRec->Fnv = lastRec.Fnv;
                fnvRec->SrcOffset = SrcOffset;
                FnvOffset++;
            }

            // this is a new dst
            if (dstChanged) {
                TLinkIncDstRec* dstRec = DstFile.Reserve(sizeof(TLinkIncDstRec));
                dstRec->HostIdTo = lastRec.HostIdTo;
                dstRec->UrlIdTo = lastRec.UrlIdTo;
                dstRec->FnvOffset = FnvOffset;
            }
        }

        if (pRec) {
            // store src
            TLinkIncSrcRec* srcRec = SrcFile.Reserve(sizeof(TLinkIncSrcRec));
            srcRec->SrcSeg = (ui16) pRec->SrcSeg;
            srcRec->HostIdFrom = pRec->HostIdFrom;
            srcRec->UrlIdFrom = pRec->UrlIdFrom;
            srcRec->DateDiscovered = pRec->DateDiscovered;
            srcRec->DateSynced = pRec->DateSynced;
            srcRec->Flags = (ui16) pRec->Flags;
            SrcOffset++;
            lastRec = *pRec;
        }
    }

    void Close() {
        Push(nullptr);
        DstFile.Close();
        FnvFile.Close();
        SrcFile.Close();
    }
};


class TLinkIncReader {
private:
    TInDatFile<TLinkIncDstRec> DstFile;
    TInDatFile<TLinkIncFnvRec> FnvFile;
    TInDatFile<TLinkIncSrcRec> SrcFile;

    const TLinkIncDstRec* DstRec;
    const TLinkIncFnvRec* FnvRec;
    const TLinkIncSrcRec* SrcRec;

    size_t FnvOffset;
    size_t SrcOffset;

    size_t FnvLeft;
    size_t   SrcLeft;

    TLinkIncRec Rec;
protected:
public:

    TLinkIncReader() :
        DstFile("dst file", dbcfg::fbufsize, 0), FnvFile("fnv file", dbcfg::fbufsize, 0), SrcFile(
                "src file", dbcfg::fbufsize, 0) {
        Clear();
    }

    void Clear() {
        DstRec = nullptr;
        FnvRec = nullptr;
        SrcRec = nullptr;
        FnvOffset = 0;
        SrcOffset = 0;
        FnvLeft = 0;
        SrcLeft = 0;
    }

    void Open(const char* dstFileName, const char* fnvFileName, const char* srcFileName) {
        DstFile.Open(dstFileName);
        FnvFile.Open(fnvFileName);
        SrcFile.Open(srcFileName);
        Clear();
    }

    void OpenDefault() {
        Open(linkcfg::fname_incdst, linkcfg::fname_incfnv, linkcfg::fname_incsrc);
    }

    const TLinkIncRec* Next() {
        if (FnvLeft == 0 && SrcLeft == 0) {
            DstRec = DstFile.Next();
            if (DstRec == nullptr)
                return nullptr;
            FnvLeft = DstRec->FnvOffset - FnvOffset;
            Rec.HostIdTo = DstRec->HostIdTo;
            Rec.UrlIdTo = DstRec->UrlIdTo;
        }
        if (SrcLeft == 0) {
            FnvRec = FnvFile.Next();
            FnvLeft--;
            FnvOffset++;
            if (FnvRec == nullptr)
                ythrow yexception () << "Fnv file finished unexpectedly";
            SrcLeft = FnvRec->SrcOffset - SrcOffset;
            Rec.Fnv = FnvRec->Fnv;
        }
        SrcRec = SrcFile.Next();
        SrcLeft--;
        SrcOffset++;
        if (SrcRec == nullptr)
            ythrow yexception () << "Src file finished unexpectedly";
        Rec.SrcSeg = SrcRec->SrcSeg;
        Rec.HostIdFrom = SrcRec->HostIdFrom;
        Rec.UrlIdFrom = SrcRec->UrlIdFrom;
        Rec.DateDiscovered = SrcRec->DateDiscovered;
        Rec.DateSynced = SrcRec->DateSynced;
        Rec.Flags = SrcRec->Flags;
        return &Rec;
    }

    void Close() {
        DstFile.Close();
        FnvFile.Close();
        SrcFile.Close();
    }
};
