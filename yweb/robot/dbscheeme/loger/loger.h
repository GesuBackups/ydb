#pragma once

#include <yweb/robot/dbscheeme/spiderrecords.h>
#include <util/stream/zlib.h>
#include <util/stream/buffer.h>
#include <util/generic/vector.h>
#include <util/generic/yexception.h>

#include <contrib/libs/zlib/zlib.h>

#include <library/cpp/microbdb/microbdb.h>
#include <library/cpp/robots_txt/constants.h>
#include <library/cpp/robots_txt/robots_txt.h>
#include <yweb/robot/spiderlib/old/docquel/docquel.h>

static inline size_t GetDocLogelSize(TDocLogel const * const doc) {
    return sizeof(TDocRec) + doc->DataStream.TotalSize;
}

static inline
size_t CompressedBound(size_t origsize) {
    return (((ui64)origsize*1002+999)/1000 + 12 + 15) & ~15;
}

struct TLogRes {
    void *Ptr;
    size_t Size;
    TLogRes(void *ptr = nullptr, size_t size = 0)
        : Ptr(ptr)
        , Size(size)
    {}
};

template <typename writer>
class TLogger: public writer {
protected:
    using writer::Failed;

public:
    TLogger(void * = nullptr) {
        Buf = BufPtr = nullptr;
        FreeSize = BufSize = 0;
        LogVersion = 0;
    }

    inline int Error() const {
        return writer::Failed;
    }

    int Open(size_t minBufSize) {
        return Open(minBufSize, minBufSize);
    }

    int Open(size_t minBufSize, size_t maxBufSize) {
        BufPtr = Buf = (char*) malloc(minBufSize);
        if (!Buf)
            return Failed = MBDB_NO_MEMORY;
        memset(BufPtr, 0, minBufSize);
        FreeSize = BufSize = minBufSize;
        MinBufSize = minBufSize;
        MaxBufSize = maxBufSize;
        return 0;
    }

    bool IsOpen()
    {
        return Buf != NULL;
    }

    int Close() {
        int ret = Flush();
        free(Buf);
        BufPtr = Buf = nullptr;
        FreeSize = BufSize = 0;
        MinBufSize = MaxBufSize = 0;
        return ret;
    }

    template <typename TRecord>
    void LogRecord(const TRecord *Record) {
        size_t recsize = SizeOf(Record);
        TRecord *ptr = PutHeader<TRecord>(recsize);
        memcpy(ptr, Record, recsize);
    }

    template <typename TRecord>
    void LogRecord(const TRecord *Record, const typename TRecord::TExtInfo * extInfo) {
        size_t recsize = SizeOf(Record);
        ui32 extInfoSize = extInfo ? extInfo->ByteSize() : 0;
        size_t fullRecSize = recsize + extInfoSize + sizeof extInfoSize;
        char * ptr = reinterpret_cast<char *>(PutHeader<TRecord>(fullRecSize));
        memcpy(ptr, Record, recsize);
        ptr += recsize;
        memcpy(ptr, &extInfoSize, sizeof extInfoSize);
        ptr += sizeof extInfoSize;
        if (extInfoSize)
            extInfo->SerializeToArray(ptr,extInfoSize);
    }

    TLogRes LogRobotsLogelOld(const TRobotsLogelOld* doc) {

        size_t namelen = strlen(doc->Packed.Data) + 1;
        size_t onePackedRobotsLength = doc->Packed.Size;

        // 2 ui32 numbers
        // first = num of encoded bot data's (in this case it equal to 1)
        // second = bot identifier
        // note, that length of packed robots record for bot stored in begin packed robots
        size_t recordLength = offsetof(TRobotsLogel, Packed.Data) + namelen + 2 * sizeof(ui32) + onePackedRobotsLength;

        TRobotsLogel *ptr = PutHeader<TRobotsLogel>(recordLength);
        ptr->LastAccess = doc->LastAccess;
        ptr->Packed.Size = 2 * sizeof(ui32) + onePackedRobotsLength;

        char* packedDataPtr = ptr->Packed.Data;

        // copy host name
        memcpy(packedDataPtr, doc->Packed.Data, namelen);
        packedDataPtr += namelen;

        // save num of bots, for which store packed data
        *(ui32*)packedDataPtr = 1; // as store data only for one bot
        packedDataPtr += sizeof(ui32);
        // save bot identifier
        *(ui32*)packedDataPtr = robotstxtcfg::id_defbot;
        packedDataPtr += sizeof(ui32);
        // save packed data
        if (onePackedRobotsLength != 0)
            memcpy(packedDataPtr, doc->Packed.Data + namelen, doc->Packed.Size);

        return TLogRes(ptr, recordLength);

    }


    TLogRes LogRobotsTxt(const char *name, const time_t lastAccess, TRobotsTxt* robots) {
        const char *packed = nullptr;
        size_t len = robots ? robots->GetPacked(packed) : 0;
        size_t namelen = strlen(name) + 1;
        if (len + namelen > robots_max) {
            robots->DoAllowAll();
            len = robots->GetPacked(packed);
        }
        // len include length of encoded robots data for list of bots (in new format)
        size_t recordLength = offsetof(TRobotsLogel, Packed.Data) + len + namelen;
        TRobotsLogel *ptr = PutHeader<TRobotsLogel>(recordLength);
        ptr->LastAccess = lastAccess;
        ptr->Packed.Size = len;
        memcpy(ptr->Packed.Data, name, namelen);
        if (len)
            memcpy(ptr->Packed.Data + namelen, packed, len);
        return TLogRes(ptr, recordLength);
    }

    void LogDocGroup(ui32 doc, ui32 group) {
        TDocGroupRec *ptr = PutHeader<TDocGroupRec>();
        ptr->DocId = doc;
        ptr->GroupId = group;
    }

    void CompressData(packed_stream_t& stream, TDocQuel *Doc, size_t origsize) {
        size_t corigsize = CompressedBound(origsize);

        stream_elem_t *origelem = (stream_elem_t *)(stream.Data() + stream.TotalSize);
        struct z_stream_s zlibData;
        int level = Z_DEFAULT_COMPRESSION;
        memset(&zlibData, 0, sizeof(z_stream_s));
        zlibData.next_out = (Bytef*)(origelem->data);
        zlibData.avail_out = corigsize;
        if (Doc->compression_method == HTTP_COMPRESSION_GZIP ||
            Doc->compression_method == HTTP_COMPRESSION_DEFLATE)
            level = Z_NO_COMPRESSION;
        int Zlib_res = deflateInit(&zlibData, level);
        if (Zlib_res  != Z_OK)
            ythrow yexception() << "zlib: " <<  Zlib_res << " - " <<  zlibData.msg;

        for(TMemoStream *Chunk = &Doc->Stream; Chunk ; Chunk = Chunk->Next) {
            if (!Chunk->Size)
                continue;
            zlibData.next_in = (Bytef*)Chunk->Data();
            zlibData.avail_in = Chunk->Size;
            Zlib_res = deflate(&zlibData, Z_NO_FLUSH);
            if (Zlib_res  != Z_OK)
                ythrow yexception() << "zlib: " <<  Zlib_res << " - " <<  zlibData.msg;
        }

        Zlib_res = deflate(&zlibData, Z_FINISH);
        if (Zlib_res != Z_STREAM_END)
            ythrow yexception() << "zlib: " <<  Zlib_res << " - " <<  zlibData.msg;
        assert(zlibData.total_out <= corigsize);
        origelem->type = SET_ORIG_DOC;
        origelem->size = zlibData.total_out;
        stream.TotalSize += offsetof(stream_elem_t, data) + DatCeil(zlibData.total_out);

        Zlib_res = deflateEnd(&zlibData);
        if (Zlib_res  != Z_OK)
            ythrow yexception() << "zlib: " <<  Zlib_res << " - " <<  zlibData.msg;
    }

    void CompressBuffer(TBuffer& input, TBuffer& compressed) {
        TBufferOutput output(compressed);
        TZLibCompress compressor(&output);
        compressor.Write(input.data(), input.Size());
    }

    void LogImage(TDocQuel *Doc, const void *Image, size_t strsize) {
        size_t urllen = Doc->base.size() + 1;
        size_t reclen = sizeof(TDocLogel) +
            packed_stream_t::CalcTotalSize(2, DatCeil(urllen) + DatCeil(strsize));

        TDocLogel *ptr = PutHeader<TDocLogel>(reclen);
        copy_header<TIndUrlHeader>(ptr, Doc);
        ptr->Size = strsize;
        ptr->DataStream.BeforePushing();
        ptr->DataStream.Push(SET_URL_NAME, Doc->base.c_str(), urllen);
        ptr->DataStream.Push(SET_ORIG_DOC, Image, strsize);
    }

    void LogAnyRec(ui32 recsig, const void *rec, size_t recsize) {
        void *ptr = PutHeader(recsig, recsize);
        memcpy(ptr, rec, recsize);
    }

    void LogDocLogelOld(const TDocLogelOld* doc) {
        size_t reclen = doc->SizeOf() + sizeof(TShingle);
        TDocLogel* ptr = PutHeader<TDocLogel>(reclen);
        memcpy(ptr, doc, sizeof(TIndUrlHeader));
        static const TVector<ui32> EMPTY_SHINGLE(SHINGLE_SIZE, 0);
        ptr->Shingle = EMPTY_SHINGLE;
        memcpy(&ptr->DataStream, &doc->DataStream, doc->DataStream.SizeOf());
    }

    template<class Record>
    TLogRes LogUpdUrl(TDocQuel *Doc) {
        size_t len = offsetof(Record, Name) + Doc->base.size() + 1;
        Record *ptr = PutHeader<Record>(len);
        ptr->LastAccess = Doc->LastAccess;
        ptr->Size = Doc->Size;
        ptr->Flags = Doc->Flags;
        ptr->HttpCode = Doc->HttpCode;
        ptr->HttpModTime = Doc->HttpModTime;
        ptr->MimeType = Doc->MimeType;
        ptr->Encoding = Doc->Encoding;
        ptr->Language = Doc->Language;
        strcpy(ptr->Name, Doc->base);
        return TLogRes(ptr, len);
    }

    template<class Record>
    TLogRes LogIndUrl(TDocQuel *Doc) {
        size_t urllen = Doc->base.size() + 1;
        size_t baselen = sizeof(Record);
        size_t origsize = Doc->Stream.SizeOf();
        size_t corigsize = CompressedBound(origsize);
        size_t langsize = Doc->NLanguages > 1 ? Doc->NLanguages * sizeof(TLangDistrib) : 0;
        size_t streamsize = packed_stream_t::CalcTotalSize(
                                (2) + (langsize ? 1 : 0),
                                DatCeil(urllen) + DatCeil(corigsize) + DatCeil(0) + DatCeil(langsize));
        size_t reclen = baselen + streamsize;

        Record *ptr = PutHeader<Record>(reclen);
        copy_header<TIndUrlHeader>(ptr, Doc);
        ptr->Size = origsize;
        ptr->DataStream.BeforePushing();
        ptr->DataStream.Push(SET_URL_NAME, Doc->base, urllen);
        if (langsize)
            ptr->DataStream.Push(SET_LANGUAGES, Doc->LangDistrib, langsize);
        if (Doc->HttpCode == HTTP_PARTIAL_CONTENT) {
            i32 range[2] = { Doc->RangeLow, Doc->RangeHigh };
            ptr->DataStream.Push(SET_RANGE_INFO, range, 2*sizeof(i32));
        }
        CompressData(ptr->DataStream, Doc, origsize);
        size_t reallen = DatCeil(baselen + ptr->DataStream.TotalSize);
        Shrink(reclen - reallen);
        return TLogRes(ptr, reallen);
    }

    struct TElement {
    public:
        TElement() : Type(SET_MAX), Size(0), Data(nullptr) {}
        TElement(stream_elem_type_t const type, ui32 const size, void const * const data)
            : Type(type), Size(size), Data(data)
        {}
        stream_elem_type_t Type;
        ui32 Size;
        void const *Data;
    };
    typedef TVector<TElement> TStreamElements;

    template<class Record>
    TLogRes LogIndUrlEx(TDocQuel* const Doc, TStreamElements const& elements) {
        size_t elemssize = 0;
        for(typename TStreamElements::const_iterator it = elements.begin(); it < elements.end(); ++it) {
            elemssize += DatCeil(it->Size);
        }

        size_t urllen = Doc->base.size() + 1;
        size_t baselen = sizeof(Record);
        size_t origsize = Doc->Stream.SizeOf();
        size_t corigsize = CompressedBound(origsize);
        size_t langsize = Doc->NLanguages > 1 ? Doc->NLanguages * sizeof(TLangDistrib) : 0;
        size_t streamsize = packed_stream_t::CalcTotalSize((2) + (langsize ? 1 : 0) + elements.size(),
                                                           DatCeil(urllen) + DatCeil(corigsize) + DatCeil(0) + DatCeil(langsize) + elemssize);
        size_t reclen = baselen + streamsize;

        Record *ptr = PutHeader<Record>(reclen);
        copy_header<TIndUrlHeader>(ptr, Doc);
        ptr->Size = origsize;
        ptr->DataStream.BeforePushing();
        ptr->DataStream.Push(SET_URL_NAME, Doc->base, urllen);
        if (langsize)
            ptr->DataStream.Push(SET_LANGUAGES, Doc->LangDistrib, langsize);
        if (Doc->HttpCode == HTTP_PARTIAL_CONTENT) {
            i32 range[2] = { Doc->RangeLow, Doc->RangeHigh };
            ptr->DataStream.Push(SET_RANGE_INFO, range, 2*sizeof(i32));
        }
        CompressData(ptr->DataStream, Doc, origsize);

        // Extra elements
        for (typename TStreamElements::const_iterator it = elements.begin(); it < elements.end(); ++it) {
            Y_VERIFY(it->Data, "data can't be NULL");
            ptr->DataStream.Push(it->Type, it->Data, it->Size);
        }

        size_t reallen = DatCeil(baselen + ptr->DataStream.TotalSize);
        Shrink(reclen - reallen);
        return TLogRes(ptr, reallen);
    }

    TDocRec *LogDocRec(const TDocLogel *Doc, const urlid_t *uid) {
        size_t reclen = GetDocLogelSize(Doc);
        TDocRec *docRec = PutHeader<TDocRec>(reclen);
        SetUid(docRec, uid);
        copy_header<TIndUrlHeader>(docRec, Doc);
        docRec->DataStream.TotalSize = Doc->DataStream.TotalSize;
        memcpy(docRec->DataStream.Data(), Doc->DataStream.Data(), Doc->DataStream.TotalSize);
        return docRec;
    }

    TSitemapRec *LogSitemapRec(const TSitemapLogel *Doc, const urlid_t *uid) {
        size_t reclen = sizeof(TSitemapRec) + Doc->DataStream.TotalSize;
        TSitemapRec *docRec = PutHeader<TSitemapRec>(reclen);
        SetUid(docRec, uid);
        copy_header<TIndUrlHeader>(docRec, Doc);
        docRec->DataStream.TotalSize = Doc->DataStream.TotalSize;
        memcpy(docRec->DataStream.Data(), Doc->DataStream.Data(), Doc->DataStream.TotalSize);
        return docRec;
    }

    template<class Record>
    TLogRes LogRedir(TDocQuel *Doc) {
        size_t urllen = Doc->base.size(), loclen = Doc->location.size();
        size_t reclen = offsetof(Record, Names) + urllen + loclen + 2;
        Record *ptr = PutHeader<Record>(reclen);
        ptr->LastAccess = Doc->LastAccess;
        ptr->Size = Doc->Size;
        ptr->Flags = Doc->Flags;
        ptr->HttpCode = Doc->HttpCode;
        ptr->Hops = Doc->Hops;
        strcpy(ptr->Names, Doc->base.c_str());
        strcpy(ptr->Names + urllen + 1, Doc->location.c_str());
        return TLogRes(ptr, reclen);
    }

    int Flush() {
        size_t len = BufPtr - Buf;
        if (!len)
            return 0;
        int ret = writer::Write(Buf, len);
        if (!ret) {
            BufPtr = Buf;
            FreeSize = BufSize;
        }
        return ret;
    }

    void RestoreBufferIfRequired() {
        if (BufSize > MinBufSize) {
            // restore buffer
            int ret = Flush();
            if (ret)
                ythrow yexception() << "loger: cannot flush: " << ret;
            // reallocate
            free(Buf);
            BufPtr = Buf = (char*) malloc(MinBufSize);
            if (!Buf)
                ythrow yexception() << "loger: not enough memory";
            memset(BufPtr, 0, MinBufSize);
            FreeSize = BufSize = MinBufSize;
        }
    }

    int RotateLogIfRequired(unsigned long logVersion) {
        if (logVersion > this->LogVersion) {
            int ret = 0;
            this->LogVersion = logVersion;
            if ((ret = Flush()))
                return ret;
            return writer::Rotate();
        }
        return 0;
    }

    void RollbackLastLogel(size_t logelSize) {
        size_t recSize = sizeof(TLogHeader) + logelSize;
        size_t realSize = DatCeil(recSize);
        Shrink(realSize);
    }

protected:
    template <typename T>
    T *PutHeader(size_t size = sizeof(T)) {
        TLogHeader *ptr = (TLogHeader *)Reserve(sizeof(TLogHeader) + size);
        ptr->Signature = TLogHeader::RecordSig;
        ptr->Type = T::RecordSig;
        return (T*)(ptr + 1);
    }

    void *PutHeader(ui32 recsig, size_t size) {
        TLogHeader *ptr = (TLogHeader *)Reserve(sizeof(TLogHeader) + size);
        ptr->Signature = TLogHeader::RecordSig;
        ptr->Type = recsig;
        return (void*)(ptr + 1);
    }

    void *Reserve(size_t size) {
        size_t realSize = DatCeil(size);
        assert(realSize <= MaxBufSize);
        if (realSize > FreeSize) {
            int ret = Flush();
            if (ret)
                ythrow yexception() << "loger: cannot flush: " << ret;
            if (realSize > FreeSize) {
                if (realSize > MaxBufSize)
                    ythrow yexception() << "loger: can't allocate " <<  size
                        << " bytes; FreeSize=" << FreeSize << "; BufSize=" << BufSize;
                // reallocate
                free(Buf);
                BufPtr = Buf = (char*) malloc(MaxBufSize);
                if (!Buf)
                    ythrow yexception() << "loger: not enough memory for reserve";
                memset(BufPtr, 0, MaxBufSize);
                FreeSize = BufSize = MaxBufSize;
            }
        }
        void *ptr = BufPtr;
        DatSet(ptr, size);
        BufPtr += realSize;
        FreeSize -= realSize;
        return ptr;
    }

    void Shrink(size_t size) {
        BufPtr -= size;
        FreeSize += size;
    }

    char *Buf, *BufPtr;
    size_t FreeSize, BufSize;
    size_t MinBufSize, MaxBufSize;
    unsigned long LogVersion;
};
