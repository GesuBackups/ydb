#pragma once

#include <fcntl.h>
#include <util/system/maxlen.h>
#include <util/system/fs.h>
#include <library/cpp/microbdb/microbdb.h>

#include <ctime>

const unsigned  DefLogFileSize = 400u << 20;

// ************************ Writer *****************************************
template <class file_manip = TOutputFileManip>
class TTrivialLogWriterImpl  {
public:
    TTrivialLogWriterImpl() : Failed(1) { }

    int Error() {
        return Failed;
    }

    int Write(char* Buf, int blen) {
        if (Failed)
            return Failed;
        if (FileManip.Write(Buf, blen) < 0)
            Failed = errno;
        return Failed;
    }

    int open(const char* fname) {
        if (!Failed)
            return Failed = 1;
        return Failed = FileManip.Open(fname, WrOnly | CreateAlways | ARW);
    }

    int close() {
        int ret = FileManip.Close();
        Failed = 1;
        return ret;
    }

protected:
    file_manip FileManip;
    int Failed;
};

typedef TTrivialLogWriterImpl<TOutputFileManip> TTrivialLogWriter;

template <class file_manip = TOutputFileManip>
class TSimpleLogWriterImpl  {
public:
    TSimpleLogWriterImpl() : Failed(1) { }

    int Error() {
        return Failed;
    }

    int Write(char *Buf, int blen) {
        if (Failed)
            return Failed;
        if (CurFileSize > MaxFileSize && (Failed = SwapFiles()) != 0)
            return Failed;
        if (FileManip.Write(Buf, blen) < 0)
            return Failed = errno;
        CurFileSize += blen;
        return Failed;
    }

    int SwapFiles() {
        if (Failed)
            return Failed;
        time_t newstamp = FileStamp + 1;
        char fname[FILENAME_MAX];
        sprintf(fname, ActiveTempl, newstamp);
        CurFileSize = 0;
        int ret = FileManip.Rotate(fname);
        if (ret)
            return ret;
        FileStamp = newstamp;
        return Failed;
    }

    int Rotate() {
        return SwapFiles();
    }

    int Init(const char *Templ, size_t maxsize = DefLogFileSize) {
        if (!Failed)
            return Failed = 1;

        if (maxsize < (1u<<20) || maxsize > (1u<<30))
            return EINVAL;
        MaxFileSize = maxsize;
        strcpy(ActiveTempl, Templ);
        FileStamp = time(nullptr);
        int ret = OpenFile(FileStamp);
        return Failed = (ret != 0);
    }

    int OpenFile(time_t stamp) {
        char fname[FILENAME_MAX];
        sprintf(fname, ActiveTempl, stamp);
        CurFileSize = 0;
        return FileManip.Open(fname, CreateNew | ARW);
    }

    int DeleteCurFile()
    {
        if (FileManip.IsOpen())
            return EBADF; //not implemented if WriterOpened

        char fname[FILENAME_MAX];
        sprintf(fname, ActiveTempl, FileStamp);
        return NFs::Remove(fname) ? 0 : -1;
    }

    int Term() {
        int ret = 0;
        FileManip.Close();
        Failed = 1;
        return ret;
    }

    char ActiveTempl[PATH_MAX + 1];
    time_t FileStamp;
    int Failed;
    size_t MaxFileSize, CurFileSize;

    file_manip FileManip;
};

typedef TSimpleLogWriterImpl<TOutputFileManip> TSimpleLogWriter;
