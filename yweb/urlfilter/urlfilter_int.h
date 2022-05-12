#pragma once
#include <util/system/defaults.h>
#include <util/generic/string.h>
#include <library/cpp/deprecated/datafile/datafile.h>

// stuct TreeNode {
//   int       SuffixSame:1;
//   int       SuffixNext:1;
//   int       NumChilds:32-2=28;
//   char     *Entry[NumChilds];
//   TreeNode *ChildNode[NumChilds];
// };

#define EN_URLS     1   // "NEXT"
#define EN_SUBDOMS  2   // "SAME"

static const ui32 NUM_MASK = (1 << 28) - 1;
static const ui32 NEXT_MASK = EN_URLS    << 28;
static const ui32 SAME_MASK = EN_SUBDOMS << 28;

/** definitions for filter builder */
enum EFilterTreeErrors { // for ignoreConflicts
    FTE_CoverAllow = 1,
    FTE_CoverDisallow = 2,
    FTE_Duplicate = 4,
    FTE_Conflict = 8,
    FTE_All = 15,
};

/// Build filter from null-terminated in_file_list
int MakeFilter(const char *outfile, TVector<char> *outmem, int verbose, int ignoreConflicts, bool enableRoot, const char *badextfile, char *in_file_list[]);

/// Use predefined 'glob' of *.flt *.spm in directory curdir as input file set, then call the first MakeFilter
//  badextfile may also be set to NULL for defaults
int MakeFilter(const char *curdir, int verbose, int ignoreConflicts, bool enableRoot, const char *outfile, TVector<char> *outmem, const char *badextfile);


/** Filter loader stuff */
struct TFilterSoHdr {
    ui32 Size;
    ui32 NodeNum;
    ui32 RootPos;
    ui32 BadExtensionsOffset;

    size_t FileSize() const {
        return Size;
    }
};

class TUrlFilterData : public TDataFile<TFilterSoHdr> {
public:
    const ui32 *FilterData;
    ui32 NodeNum;
    const char *FirstChar;
    const char *BadExtensions;
    const ui32 *RootNode;

    void Init(bool isMap, const char *msgName); // urlfilter.cpp
    void Destroy();

public:
    TUrlFilterData();

    void LoadFilter(const char *fname, EDataLoadMode loadMode) {
        Load(fname, loadMode);
        Init((loadMode & DLM_LD_TYPE_MASK) != DLM_READ, fname);
    }

    void BuildFromDir(const char *dirname, bool enableRoot) {
        Destroy();
        MakeFilter(dirname, false, FTE_All, enableRoot, nullptr, &MemData, nullptr); // urlfilter_builder.cpp
        Start = MemData.begin();
        Length = MemData.size();
        Init(true, dirname);
    }

    void Swap(TUrlFilterData &with);
};
