#pragma once

#include <library/cpp/microbdb/microbdb.h>
#include "baserecords.h"
#include "spiderrecords.h"
#include <library/cpp/robots_txt/constants.h>

template <class T>
inline size_t MinFirstBytesToDetermineSize() {
    return sizeof(T);
}

template <>
inline size_t MinFirstBytesToDetermineSize<TRobotsRec>() {
    return sizeof(TRobotsRec) - robots_max;
}

template <>
inline size_t MinFirstBytesToDetermineSize<TRobotsLogel>() {
    return sizeof(TRobotsLogel) - robots_max;
}

class TSizeOf {
public:
    template <class T>
    static int Action(const T *t, const char *, void *) {
        return ::SizeOf(t);
    }
};

class TMinFirstBytesToDetermineSize {
public:
    template <class T>
    static int Action(const T*, const char *, void *ret) {
        *((size_t *)ret) = MinFirstBytesToDetermineSize<T>();
        return 0;
    }
};

// Generalized log reader implementation
template <class file_manip = TInputFileManip>
class TGLogReaderImpl  {
public:
    TGLogReaderImpl()
    : Failed(1) {
    }

    ~TGLogReaderImpl() {
        Term();
    }

    int Error() {
        return Failed;
    }

    int Init(size_t bufsize) {
        if (!Failed)
            return Failed = 1;

        Buf = (ui8*)malloc(bufsize);
        if (!Buf) {
            Failed = 1;
            return MBDB_NO_MEMORY;
        }

        BufSize = bufsize;
        Cur = Data = Buf;
        return Failed = 0;
    }

    int Term() {
        FileManip.Close();
        if (Buf) {
            free(Buf);
            Cur = Data = Buf = nullptr;
        }
        Failed = 1;
        return 0;
    }

    int OpenFile(const char *fname) {
        if (Failed)
            return Failed;

        int ret = FileManip.Open(fname);
        return Failed = (ret != 0);
    }

    int SetFile(const TFile& file) {
        if (Failed)
            return Failed;

        int ret = FileManip.Init(file);
        return Failed = (ret != 0);
    }

    template<class RecordType>
    const RecordType *Next() {
        size_t data_size = 0;
        if (Failed)
            ythrow yexception() << "TGLogReader: inconsistent state";

        data_size = ReadData(sizeof(TLogHeader));
        if (data_size == 0)
            return NULL;
        else if (data_size < sizeof(TLogHeader)) {
            Failed = 1;
            ythrow yexception() << "TGLogReader: corrupted file";
        }

        TLogHeader *lh = (TLogHeader *)Cur;
        if (lh->Signature != TLogHeader::RecordSig) {
            Failed = 1;
            ythrow yexception() << "TGLogReader: bad log header; signature was 0x" <<  lh->Signature;
        }

        if (lh->Type != RecordType::RecordSig) {
            Failed = 1;
            ythrow yexception() << "TGLogReader: bad record header; signature was 0x" <<  lh->Type;
        }

        Cur += sizeof(TLogHeader);

        if (ReadData(MinFirstBytesToDetermineSize<RecordType>()) == 0) {
            Failed = 1;
            ythrow yexception() << "TGLogReader: corrupted file";
        }

        RecordType *rec = (RecordType *)Cur;
        size_t rec_size = rec->SizeOf();
        Y_ASSERT(rec_size <= BufSize);
        rec_size = DatCeil(rec_size);

        if (ReadData(rec_size) < rec_size) {
            Failed = 1;
            ythrow yexception() << "TGLogReader: corrupted file";
        }

        rec = (RecordType *)Cur; // renew rec, since Cur could be changed
        Cur += rec_size;
        return rec;
    }

    template<class TDispatcher>
    const void *Next(ui32 &rec_sig, size_t *out_rec_size = nullptr) {
        size_t data_size = 0;
        if (Failed)
            ythrow yexception() << "TGLogReader: inconsistent state";

        data_size = ReadData(sizeof(TLogHeader));
        if (data_size == 0)
            return nullptr;
        else if (data_size < sizeof(TLogHeader)) {
            Failed = 1;
            ythrow yexception() << "TGLogReader: corrupted file";
        }

        TLogHeader *lh = (TLogHeader *)Cur;
        if (lh->Signature != TLogHeader::RecordSig) {
            Failed = 1;
            off_t pos = FileManip.GetPosition() - (Data - Cur);
            ythrow yexception() << "TGLogReader: bad log header; signature was " <<  lh->Signature << "; position: " << pos;
        }

        rec_sig = lh->Type;
        Cur += sizeof(TLogHeader);

        size_t minsize = 0;
        if (TDispatcher::template DispatchRecord<TMinFirstBytesToDetermineSize>(rec_sig, nullptr, &minsize)) {
            Failed = 1;
            ythrow yexception() << "TGlogReader: unknown record: " << rec_sig;
        }
        if (ReadData(minsize) == 0) {
            Failed = 1;
            ythrow yexception() << "TGLogReader: corrupted file";
        }

        size_t rec_size = TDispatcher::template DispatchRecord<TSizeOf>(rec_sig, Cur, nullptr);
        Y_ASSERT(rec_size <= BufSize);
        rec_size = DatCeil(rec_size);

        if (ReadData(rec_size) < rec_size) {
            Failed = 1;
            ythrow yexception() << "TGLogReader: corrupted file";
        }

        void *rec = Cur;
        Cur += rec_size;
        if (out_rec_size)
            *out_rec_size = rec_size;
        return rec;
    }

protected:
    /* The function fills the buffer by reading at least n bytes at current file
     * position. Data is placed into continious fragment of memory.
     * The function returns number of bytes available in the buffer. */
    size_t ReadData(size_t n) {
        Y_ASSERT(n);
        size_t data_size = Data - Cur;

        if (data_size >= n) {
            return data_size; // Ok
        }

        if ((size_t)(Buf + BufSize - Cur) < n) {
            // requested data will not fit in the rest of the buffer
            if (BufSize < n) {
                Failed = 1;
                ythrow yexception() << "TGLogReader: requested to read more data than fits into the buffer (n=" <<  n << ")";
            }

            memmove(Buf, Cur, data_size);
            Cur = Buf;
            Data = Buf + data_size;
        }

        size_t to_read = Buf + BufSize - Data;
        int res = 0;
        while (to_read) {
            res = FileManip.Read(Data, to_read);
            if (res == -1) {
                Failed = 1;
                ythrow yexception() << "TGLogReader: error reading file (errno=" <<  errno << ")";
            }
            Data += res;
            data_size += res;
            to_read -= res;

            if (res == 0 || data_size >= n) { // eof or we've read enough data
                return data_size;
            }
        }

        Failed = 1;
        ythrow yexception() << "TGLogReader: invalid logic";
    }

    int Failed;
    file_manip FileManip;
    size_t BufSize;
    ui8 *Buf, *Cur, *Data;
};

typedef TGLogReaderImpl<TInputFileManip> TGLogReader;
