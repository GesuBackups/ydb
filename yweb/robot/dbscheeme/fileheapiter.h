#pragma once

#include <library/cpp/microbdb/microbdb.h>
#include <library/cpp/microbdb/safeopen.h>

template<class TInputS, template<typename> class TComp>
class TFileHeapIter {
public:
    typedef typename TInputS::TRec TRec;
protected:
    TInputS* Files;
    THeapIter<TRec, TInputS, TComp<TRec> >  Heap;
    int FilesNum;

public:
    TFileHeapIter()
        : Files(0)
        , FilesNum(0) {}

    ~TFileHeapIter() {
        Term();
    }

    void Init(const char* fpaths, int fnum, const char* fnames = "heapfile_%d", int buffer = 1, int inpages = 1) {
        assert(FilesNum == 0 && Files == 0);
        FilesNum = fnum;

        Files = (TInputS*) malloc(FilesNum * sizeof(TInputS));
        int plen = strlen(fpaths);
        int nlen = strlen(fnames);
        char* fpath = (char*) alloca(plen + 200);
        char* fname = (char*) alloca(nlen + 200);
        for (int cl = 0; cl < FilesNum; cl++) {
            sprintf(fname, fnames, cl);
            new(&Files[cl]) TInputS(fname, buffer, inpages);
            sprintf(fpath, fpaths, cl);
            Files[cl].Open(fpath);
        }
        Heap.Init(Files, FilesNum);
    }

    void Term() {
        for (int cl = 0; cl < FilesNum; cl++) {
            Files[cl].Close();
            Files[cl].~TInputS();
        }
        if (Files)
            free(Files);
        Files = 0;
        FilesNum = 0;
        Heap.Term();
    }

    const TRec* Next() {
        return Heap.Next();
    }

    const TRec* Current() {
        return Heap.Current();
    }

    const TInputS* CurrentStream() {
        return Heap.CurrentIter();
    }
};
