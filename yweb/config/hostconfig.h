#pragma once

#include "clustercfg.h"
#include "hostcrc.h"
#include "str_hash.h"

#include <library/cpp/yconf/conf.h>

#include <library/cpp/charset/ci_string.h>
#include <library/cpp/deprecated/fgood/fgood.h>
#include <util/generic/hash.h>
#include <util/generic/hash_set.h>
#include <util/generic/noncopyable.h>
#include <util/generic/ptr.h>
#include <util/generic/string.h>
#include <util/generic/vector.h>
#include <util/generic/list.h>
#include <util/generic/ylimits.h>
#include <util/memory/segmented_string_pool.h>
#include <util/network/init.h>
#include <util/string/printf.h>
#include <util/string/vector.h>
#include <util/system/filemap.h>
#include <util/system/progname.h>

#include <algorithm>

struct SplitCrc {
    ui64 minCrc;
    ui64 maxCrc;
    ui32 segmentId;
    const char *Host;
};

struct TGroupSeq
{
    ui16 Group;
    ui16 Seq;
};

struct TSegmentFriends {
    typedef TList<const char*> THosts;
    THashMap<TString, THosts> Friends;

    // for backward compat, when only one host is defined for each type
    const char* GetFriends(const char* name) const {
        const auto it = Friends.find(name);
        return it == Friends.end() ? nullptr : (it->second).back();
    }

    const THosts* GetFriendHosts(const char* name) const {
        const auto it = Friends.find(name);
        return it == Friends.end() ? nullptr : &it->second;
    }
};

class TWalSctGroup
{
public:
    std::vector<ui16> Walruses;
    std::vector<ui16> Scatters;
};

struct TClusterRange {
    ui16 Start;
    ui16 End;

    TClusterRange(ui16 start = 0, ui16 end = 0)
        : Start(start), End(end) {
    }
};

typedef TList<TClusterRange> TClusterRanges;

typedef THashMap<const char*, ClusterConfig> Comp2Config;
typedef THashMap< const char*, std::shared_ptr<SearchClusterConfig> > Comp2SearchConfig;
typedef TVector<const char*> TSearchZoneNames;
typedef TVector< std::shared_ptr<Comp2Config> > TSearchZoneConfigs;
typedef THashMap<const char*, const char*> TSpecHosts;

typedef TVector<TGroupSeq> TGroupSeqs;
typedef TVector<TWalSctGroup> TWalSctGroups;
typedef THashMap< const char*, TVector<ui16> > TSegs4Friends;

inline bool SplitCrcByMax(const struct SplitCrc& i, const struct SplitCrc& j) {
    return i.maxCrc <= j.maxCrc;
}

inline bool SplitCrcByMaxLess(const struct SplitCrc& i, const struct SplitCrc& j) {
    return i.maxCrc < j.maxCrc;
}

inline unsigned CheckHostCfg(const char *host, Comp2Config &TheConfigs) {
    if (TheConfigs.find(host) != TheConfigs.end())
        return 0;
    Comp2Config::const_iterator ConfIt = TheConfigs.find("_default");
    if (ConfIt == TheConfigs.end())
        return 1;
    TheConfigs[host] = ConfIt->second;
    return 0;
}

struct TName2Crc : public THashMap<TStringBuf, ui32, ci_hash, ci_equal_to> {
    bool FullDomain;
    TName2Crc() : FullDomain(false) {}
};

class TCrcOverride : public THashMap<const char*, TName2Crc, ci_hash, ci_equal_to> {
public:
    TCrcOverride();
    void AddName(const char *name, ui32 crc, const TConfigNameSet& dom2);
    bool Check(const TStringBuf &Name, size_t dom2off, ui32 &crc) const;
};

class THostConfig : public TYandexConfig {
public:

    class TMain : public TYandexConfig::Directives {
    public: TMain() {}
    };

    THostConfig ()
        : MySegNum(-1)
        , Dom2Proc(D2PS_LimitedSpread)
        , IsScattered(false)
        , ScatterGroups(0)
        , MaxGroup(0)
        , HashShift(0)
    {
        InitNetworkSubSystem(); // needed on windows for gethostname()
    }

    ~THostConfig () override {
    }

    [[nodiscard]]
    virtual int Load(const char *filename, const TString& cluster = TString());
    virtual int LoadForHost(const char* filename, const char* hostName, const TString& cluster = TString());
    void LoadMem(const char *config);

    void TryLoad(const char* filename, const TString& cluster = TString()) {
        int ret;
        if ((ret = Load(filename, cluster)) != 0)
            ythrow yexception() << "\"" <<  filename << "\" config load error: " <<  ret;
    }

    void ReplaceSplits(TVector<struct SplitCrc> &NewSplits) {
        std::swap(Splits, NewSplits);
    }
    int CompareSegment(const SplitCrc &Segment, bool &Changed, TVector<const char*> &SplitSegments) const;
    ui32 GetSegmentIndex(TStringBuf SiteName) const;
    const char *GetSegmentName(TStringBuf SiteName) const;
    ui64 GetCrc(TStringBuf SiteName) const;
    unsigned GetNumSegments() const {
        return (unsigned)Splits.size();
    }
    const char *GetSegmentName(unsigned Num) const {
        return Num >= UnsortedSplits.size() ? nullptr : UnsortedSplits[Num].Host;
    }
    ui32 GetSegmentId(const char *segmentHostName) const;
    ui32 GetScatterId(const char *scatterHostName) const;

    const SplitCrc &GetSegmentDescr(const char *segName) const;
    bool IsScatterHost(const char *hostName) const;

    const TClusterRanges& GetFriendsHostsClusterRanges(const char *Host, const char *Type) const;

    const ClusterConfig *GetSegmentClusterConfig(unsigned Num) const;
    const ClusterConfig *GetSegmentClusterConfig(const char *SegmentName) const;

    SearchClusterConfig *GetScatterSearchClusterConfig(ui32 scatterId) const;
    SearchClusterConfig *GetSegmentSearchClusterConfig(const char *SegmentName) const;
    SearchClusterConfig *GetSearchClusterConfig() const;

    const ClusterConfig *GetDstScatterClusterConfig(const char* SegmentName) const;
    const TConfigNameSet& GetDom2() const {return Dom2;}
    const TCrcOverride& GetCrcOverride() const { return CrcOverride; }

    int Compare(const THostConfig *HC2) const;

    int GetMySegmentId() const { return MySegNum; }
    const char *GetMyHostName() const { return MyHostName.data(); }

    const char *GetSpecialHost(const char *role) const;

    size_t GetNumScatterHosts() const { /*|W|*/
        return ScatterHosts.size();
    }

    const char* GetScatterHost(unsigned num) const {
        return num >= ScatterHosts.size() ? nullptr : ScatterHosts[num];
    }

    size_t GetNumScatterGroups() const { /*G*/
        return ScatterGroups;
    }

    TSearchZoneNames GetSearchZoneNames() const {
        return SearchZoneNames;
    }

    ui32 GetSearchSelectionDocsQuota(const char *zoneName) const;
    ui64 GetSearchSelectionSizeQuota(const char *zoneName) const;

    size_t GetNumScatterPieces(const char* segmentName) const; /*i*/
    void GetNotSpamScatterPieces(const char* segmentName, TVector<ui32>& notSpamScatterPieces) const;
    void GetNotSpamScatterPiecesOnScatter(const char* scatterName, TVector<ui32>& notSpamScatterPieces) const;
    void GetSpamScatterPieces(const char* segmentName, TVector<ui32>& spamScatterPieces) const;
    void GetSpamScatterPiecesOnScatter(const char* scatterName, TVector<ui32>& spamScatterPieces) const;

    ui32 GetRawScatterPlanClusterShift(ui32 segmentId, ui32 scatterHostIdIndexInGroup) const;
    const char *GetScatterDst(const char *segmentHostName, ui32 segmentSctCluster, /*out*/ ui32 &dstSctCluster) const;
    void GetScatterIds(ui32 segmentSrc, TVector<ui32> &scattersDst) const;
    void GetSegmentIds(ui32 scatterDst, TVector<ui32> &segmentsSrc) const;
    void GetGroupScatterIds(ui32 group, TVector<ui32> &scattersDst) const;
    void GetGroupSegmentIds(ui32 group, TVector<ui32> &segmentsSrc) const;
    bool GetIsScattered() const {return IsScattered;}
    ui32 GetMyScatterCount();
    ui32 GetMyGroup(const ui32 scatterId) const;

    const TSegmentFriends& GetFriends(const char* segmentName) const;
    void GetFriendSegs(const char *host, TVector<ui32> &segments) const;
    const TVector<const char*> &GetFullList(const char *type) const; // currently for 'friends'

    ui32 GetSeq(ui32 segmentId)
    {
        return WalGroups[segmentId].Seq;
    }
    ui32 GetGroupBySeg(ui32 segmentId) const
    {
        return WalGroups[segmentId].Group;
    }
    ui32 GetGroupByScatter(ui32 scatterId)
    {
        return SctGroups[scatterId].Group;
    }
    bool IsDom2(const char* host)
    {
        return Dom2.find(host) != Dom2.end();
    }
    ui32 GetGroupByUrl(TStringBuf url) const {
        return GetGroupBySeg(GetSegmentIndex(url));
    }

    ui64 GetMetaRobotShardSizeQuota(const char* segmentName) const;

public:
    TMain Main;

protected:
    void SetMySegmentId(const TString& cluster, const char* hostName);
    [[nodiscard]]
    int LoadReal(const char *filename, const char *config, const TString& cluster, const char* hostName = nullptr);
    bool AddKeyValue(Section& sec, const char* key, const char* value) override;
    bool OnBeginSection(Section& sec) override;
    bool OnEndSection(Section& sec) override;
    TVector<struct SplitCrc> Splits;
    TVector<struct SplitCrc> UnsortedSplits;
    THashMap<const char*, ui32> SplitName2Id;

private:
    bool DoAddKeyValue(Section& sec, const char* key, const char* value);
    bool AddSplit(const char* key, const char* value);
    bool AddScatter(const char* key, const char* value);
    bool AddFriends(const char *type, const char* key, const char* value);
    bool SetConfig(const char* key, const char* value, Comp2Config &TheConfigs, const char *errpref);
    bool SetSearchConfig(const char* zoneName, const char* key, const char* value);
    bool SetSpecialHost(const char* key, const char* value);
    bool SetVersionInfo(const char* key, const char* value);
    bool SetScatterInfo(const char* key, const char* value);
    bool SetSearchSelectionDocsQuota(const char* key, const char* value);
    bool SetSearchSelectionSizeQuota(const char* key, const char* value);
    bool SetMetaRobotShardSizeQuota(const char* key, const char* value);
    bool SetHashShift(const char* value);
    ui32 CheckSearchHostConfigs(const char *host);
    int Check();
    void PrepareSearchConfig();
    void GenerateDstScatterConfigs();
    void MergeFriendsAndHosts();
    void BuildGroups();

private:
    typedef THashMap<TString, TClusterRanges> THostsClusterRanges;
    typedef THashMap<TString, THostsClusterRanges> TFriendsHosts;

    Comp2Config Configs;
    Comp2SearchConfig SearchConfigs;
    Comp2Config DstScatterConfigs;
    TGroupSeqs WalGroups, SctGroups;
    TWalSctGroups WalSctGroups;
    THashMap<ui16, ui16> Seqs, SctSeqs;
    TVector<TSegmentFriends> Friends;
    THashMap<TString, TVector<const char*> > FriendGrp;
    TFriendsHosts RevFriends;
    TSegs4Friends Segs4Friends;

    TSearchZoneNames SearchZoneNames;

    //just for parsing
    int CurSectionId; // section name -> enum
    const char *CurSectionType; // section's "type" attribute
    TSearchZoneConfigs SearchZoneConfigs;

    TSpecHosts SpecHosts;
    int MySegNum;
    TString MyHostName;
    TString MySegmentName;
    ui32 VersionMajor, VersionMinor;
    TString VersionCVSId;
    TConfigNameSet Dom2;
    EDom2ProcStyle Dom2Proc;
    TCrcOverride CrcOverride;

    bool IsScattered;
    TVector<const char*> ScatterHosts;
    size_t ScatterGroups;
    TMap<const char* /*ZoneName*/, ui32> SearchSelectionDocsQuota;
    TMap<const char* /*ZoneName*/, ui64> SearchSelectionSizeQuota;
    TMap<const char* /* segment host name */, ui64> MetaRobotShardSizeQuota;
    ui16 MaxGroup;
    ui32 HashShift;

protected:
    segmented_string_pool Pool;
    THashSet<const char*> PoolUniq;

public:
    static const char *HostCfgDefName;
    static const char *ClusterCfgDefSegName; // NULL
};

class TDefHostConfig : public THostConfig
{
public:
    int Result;

public:
    TDefHostConfig(const char* name = nullptr)
    {
        Result = Load(name ? name : HostCfgDefName);
    }
};

inline void LoadOwners(const char* owners, TMappedArray<ui32>& howners, ui32 maxhost, ui32* pmaxowner = nullptr)
{
    howners.Create(maxhost + 1);
    memset(&howners[0], 0, (maxhost + 1) * sizeof(ui32));
    TFILEPtr fow(owners, "r");
    char str[HOST_MAX + 100];
    ui32 maxOwner = 0;
    while (fow.fgets(str, sizeof(str))) {
        ui32 host, owner;
        if (sscanf(str, "%i %i", &host, &owner) >= 2) {
            if (host <= maxhost)
                howners[host] = owner;
            if (owner > maxOwner)
                maxOwner = owner;
        }
    }
    fclose(fow);
    if (pmaxowner)
        *pmaxowner = maxOwner;
}

inline ui32 LoadHosts(const char* whp, TMappedArray<ui32>& hosts, ui32& mdocs) {
    mdocs = 0;
    char name[PATH_MAX];
    sprintf(name, "%s.%03i", whp, 0);
    TFILEPtr w(name, "r");
    ui32 maxdoc = ui32(w.length() / sizeof(ui32));
    if (maxdoc > mdocs)
        mdocs = maxdoc;
    hosts.Create(mdocs);
    ui32 maxhost = 0;
    ui32 h;
    ui32 doc = 0;
    while (w.fget(h)) {
        hosts[doc] = h;
        if ((h != ~(ui32)0) && (h > maxhost))
            maxhost = h;
        doc++;
    }
    fclose(w);
    return maxhost;
}

inline ui32 LoadHosts(THostConfig& hconfig, const char* whp, TMappedArray<ui32>& hosts, ui32& mdocs)
{
    mdocs = 0;
    int mySeg = hconfig.GetMySegmentId();
    const ClusterConfig* cconfig = hconfig.GetSegmentClusterConfig(mySeg);
    ui32 clCount = cconfig->Clusters();
    TVector<TFILEPtr> whps(clCount);
    for (ui32 c = 0; c < clCount; c++) {
        TString name = Sprintf("%s.%03i", whp, c);
        Cerr << GetProgramName() << ": opening file with hosts: " << name.Quote() << Endl;
        whps[c].open(name.data(), "r");
        ui32 maxdoc = ui32(whps[c].length() / sizeof(ui32));
        ui32 maxdocs;
        cconfig->Cluster2Handle(maxdocs, c, maxdoc);
        if (maxdocs > mdocs)
            mdocs = maxdocs;
    }
    hosts.Create(mdocs);
    ui32 maxhost = 0;
    for (ui32 c = 0; c < clCount; c++) {
        ui32 h;
        ui32 doc = 0;
        while (whps[c].fget(h)) {
            ui32 sdoc;
            cconfig->Cluster2Handle(sdoc, c, doc);
            hosts[sdoc] = h;
            if ((h != ~(ui32)0) && (h > maxhost))
                maxhost = h;
            doc++;
        }
        fclose(whps[c]);
    }
    return maxhost;
}
