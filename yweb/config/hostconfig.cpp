#include <stdlib.h>

#include <algorithm>

#include <util/generic/hash_set.h>
#include <util/system/maxlen.h>
#include <util/string/util.h>
#include <library/cpp/string_utils/url/url.h>
#include <util/string/split.h>
#include <util/network/socket.h>
#include <util/string/cast.h>
#include "str_hash.h"
#include "hostconfig.h"
#include "environment.h"
#include "multilanguage_hosts.h"

using namespace std;

const ui64 LastUi32 = 0xffffffffULL;


static const char* DEFAULT_SEARCH_ZONE_NAME = "Rus0";
static const char* SEARCH_CL_CONF_NAME = "SearchClusterConfigs";
static const ui32 SEARCH_CL_CONF_LEN = strlen(SEARCH_CL_CONF_NAME);

//static const char* OLD_ROBOT_SEL_DOCS_QUOTA_CONF_NAME = "RobotSelectionQuota";
//static const ui32 OLD_ROBOT_SEL_DOCS_QUOTA_CONF_LEN = strlen(OLD_ROBOT_SEL_DOCS_QUOTA_CONF_NAME);

//static const char* ROBOT_SEL_DOCS_QUOTA_CONF_NAME = "RobotSelectionDocsQuota";
//static const ui32 ROBOT_SEL_DOCS_QUOTA_CONF_LEN = strlen(ROBOT_SEL_DOCS_QUOTA_CONF_NAME);

//static const char* META_ROBOT_SHARD_CONF_NAME = "MetaRobotShardConfig";
//static const ui32 META_ROBOT_SHARD_CONF_LEN = strlen(META_ROBOT_SHARD_CONF_NAME);

//static const char* SEARCH_SHARD_CONF_NAME = "SearchShardConfig";
//static const ui32 SEARCH_SHARD_CONF_LEN = strlen(SEARCH_SHARD_CONF_NAME);

const char *THostConfig::HostCfgDefName = "/Berkanavt/database/host.cfg";
const char *THostConfig::ClusterCfgDefSegName = nullptr;

#define OurSections \
    DefSection(SpecialHosts) \
    DefSection(ConfigVersion) \
    DefSection(HostTable) \
    DefSection(ClusterConfigs) \
    DefSection(ScatterTable)   \
    DefSection(SearchClusterConfigs) \
    DefSection(Dom2) \
    DefSection(CrcOverride) \
    DefSection(PrnSort) /*unused*/ \
    DefSection(SearchSelectionQuota) /* for compatibility with old host.cfg format */ \
    DefSection(SearchSelectionDocsQuota) \
    DefSection(SearchSelectionSizeQuota) \
    DefSection(RobotSelectionQuota) /* for compatibility with old host.cfg format */ \
    DefSection(RobotSelectionDocsQuota) /* for compatibility with old host.cfg format */ \
    DefSection(MetaRobotShardSizeQuota) \
    DefSection(MetaRobotShardConfig) /* for compatibility with old host.cfg format */ \
    DefSection(HashShift) \
    DefSection(SearchShardConfig) /* for compatibility with old host.cfg format */ \
    DefSection(FriendHosts) \


enum EHostcfgSectionType {
    NoSection = 0,
    #define DefSection(a) ST_##a,
    OurSections
};

inline const char *AppendUniq(segmented_string_pool &pool, THashSet<const char*> &uniq, const TStringBuf &what) {
    THashSet<const char*>::const_iterator i = uniq.find(what);
    if (i != uniq.end())
        return *i;
    const char *copy = pool.append(what.data(), what.size());
    uniq.insert(copy);
    return copy;
}

bool THostConfig::OnBeginSection(Section& sec)
{
    sec.Cookie = &Main;
    CurSectionId = NoSection;
    // for now, always look for prefix to simplify code
    #undef DefSection
    #define DefSection(name) if (!strnicmp(sec.Name, #name, sizeof(#name) - 1)) CurSectionId = ST_##name; else
    OurSections
    /*... else*/ if (*sec.Name) {
        fprintf(stderr, "hostconfig: illegal section name \"%s\"\n", sec.Name);
        return false;
    }
    if (CurSectionId == ST_ScatterTable)
        IsScattered = true;
    SectionAttrs::const_iterator a = sec.Attrs.find("type");
    CurSectionType = a == sec.Attrs.end()? nullptr : AppendUniq(Pool, PoolUniq, a->second);
    if (CurSectionId == ST_Dom2) {
        a = sec.Attrs.find("spread");
        if (a == sec.Attrs.end() || !stricmp(a->second, "Limited"))
            Dom2Proc = D2PS_LimitedSpread;
        else if (!stricmp(a->second, "Full"))
            Dom2Proc = D2PS_FullSpread;
        else ythrow yexception() << "Dom2 spread type " << a->second << " not supported";
    }
    return true;
}

bool THostConfig::OnEndSection(Section& sec)
{
    if (!stricmp(sec.Name, "HostTable"))
    {
        UnsortedSplits = Splits;
        sort(Splits.begin(), Splits.end(), SplitCrcByMaxLess);
        Friends.resize(Splits.size());
    }
    CurSectionId = NoSection;
    return true;
}

inline bool THostConfig::DoAddKeyValue(Section& sec, const char* key, const char* value)
{
    size_t secNameLen = strlen(sec.Name);
    const char *secName = AppendUniq(Pool, PoolUniq, sec.Name);
    key = AppendUniq(Pool, PoolUniq, key);
    value = Pool.append(value);

    switch ((EHostcfgSectionType)CurSectionId) {
    case ST_SpecialHosts:   return SetSpecialHost(key, value);
    case ST_ConfigVersion:  return SetVersionInfo(key, value);
    case ST_HostTable:      return AddSplit(key, value);
    case ST_ClusterConfigs: return SetConfig(key, value, Configs, "");
    case ST_ScatterTable:   return AddScatter(key, value);
    case ST_FriendHosts:    return AddFriends(CurSectionType, key, value);
    case ST_SearchSelectionQuota:
    case ST_SearchSelectionDocsQuota:     return SetSearchSelectionDocsQuota(key, value);
    case ST_SearchSelectionSizeQuota: return SetSearchSelectionSizeQuota(key, value);
    case ST_MetaRobotShardSizeQuota: return SetMetaRobotShardSizeQuota(key, value);
    case ST_MetaRobotShardConfig: return true; // for compatibility only!
    case ST_SearchShardConfig: return true; // for compatibility only!
    case ST_RobotSelectionQuota: return true; // for compatibility only!
    case ST_RobotSelectionDocsQuota: return true; // for compatibility only!
    case ST_HashShift: return SetHashShift(value);

    case ST_Dom2:
        if (!CrcOverride.empty()) ythrow yexception() << "CrcOverride must be defined after Dom2";
        Dom2.insert(key);
        return true;

    case ST_CrcOverride:
        CrcOverride.AddName(key, isdigit((ui8)*value)? FromString<ui32>(value) : GetSegmentDescr(value).maxCrc, Dom2);
        return true;

    case ST_SearchClusterConfigs:
        if (secNameLen != SEARCH_CL_CONF_LEN) {
            return SetSearchConfig(secName + SEARCH_CL_CONF_LEN, key, value);
        } else {
            return SetSearchConfig(CurSectionType? CurSectionType : DEFAULT_SEARCH_ZONE_NAME, key, value);
        }

    case ST_PrnSort: return false;
    case NoSection: break;
    }

    Y_FAIL("unknown section id: %d", int(CurSectionId));
    return false;
}

bool THostConfig::AddKeyValue(Section& sec, const char* key, const char* value) {
    if (Y_LIKELY(DoAddKeyValue(sec, key, value)))
        return true;
    warnx("Failed to parse %s at section %s, key %s, value %s", GetConfigPath(), sec.Name, key, value);
    return false;
}

const char *THostConfig::GetSegmentName(TStringBuf SiteName) const {
    return UnsortedSplits[GetSegmentIndex(SiteName)].Host;
}

ui32 THostConfig::GetSegmentIndex(TStringBuf SiteName) const {
    struct SplitCrc C;
    C.maxCrc = GetCrc(IsMultilanguageHost(SiteName) ? DecodeMultilanguageHost(SiteName.data()) : SiteName);
    TVector<struct SplitCrc>::const_iterator Found =
            upper_bound(Splits.begin(), Splits.end(), C, SplitCrcByMax);
    assert(Found != Splits.end());
    if (Found == Splits.end())
        abort();
    return Found->segmentId;
}

ui32 THostConfig::GetSegmentId(const char *segmentHostName) const {
    THashMap<const char*, ui32>::const_iterator i = SplitName2Id.find(segmentHostName);
    if (i != SplitName2Id.end())
        return i->second;
    return UnsortedSplits.size();
}

ui32 THostConfig::GetScatterId(const char *scatterHostName) const {
    for(ui32 i = 0; i < ScatterHosts.size(); i++)
        if (!stricmp(scatterHostName, ScatterHosts[i]))
            return i;
    return ScatterHosts.size();
}

const SplitCrc &THostConfig::GetSegmentDescr(const char *segName) const {
    ui32 id = GetSegmentId(segName);
    if (id >= UnsortedSplits.size())
        ythrow yexception() << "Segment '" << segName << "' is not (yet?) defined in config";
    return UnsortedSplits[id];
}

bool THostConfig::IsScatterHost(const char *hostName) const {
    return GetScatterId(hostName) < ScatterHosts.size();
}

ui32 THostConfig::CheckSearchHostConfigs(const char *host) {
    ui32 result = 0;

    for (TSearchZoneConfigs::iterator it = SearchZoneConfigs.begin(); it != SearchZoneConfigs.end(); ++it) {
        result += CheckHostCfg(host, **it);
    }

    return result;
}

int THostConfig::Check() {
    if (!Splits.size()) {
        fprintf(stderr, "hostconfig: no splits set\n");
        return 1;
    }
    if (Splits[0].minCrc != 0UL) {
        fprintf(stderr, "hostconfig: no host for crc in 0, %" PRIu64 "\n", Splits[0].minCrc);
        return 1;
    }
    if (VersionMajor != 2) {
        fprintf(stderr, "hostconfig: config major version %u not supported\n", VersionMajor);
        return 1;
    }

    unsigned CheckConfigs = CheckHostCfg(Splits[0].Host, Configs);
    unsigned SearchCheckConfigs = CheckSearchHostConfigs(Splits[0].Host);

    unsigned i = 1;
    for(; i < Splits.size(); i++) {
        if (Splits[i].minCrc != Splits[i-1].maxCrc + 1) {
            if (Splits[i].minCrc >  Splits[i-1].maxCrc + 1)
                fprintf(stderr, "hostconfig: no host for crc in %" PRIu64 ", %" PRIu64 "\n",
                    Splits[i-1].maxCrc + 1, Splits[i].minCrc);
            else
                fprintf(stderr, "hostconfig: crc intervals for %s (%" PRIu64 ", %" PRIu64 ") and %s (%" PRIu64 ", %" PRIu64 ") overlap\n",
                    (Splits[i-1].Host), Splits[i-1].minCrc, Splits[i-1].maxCrc,
                    (Splits[i].Host), Splits[i].minCrc, Splits[i].maxCrc);
            return 1;
        }
        CheckConfigs += CheckHostCfg(Splits[i].Host, Configs);
        SearchCheckConfigs += CheckSearchHostConfigs(Splits[i].Host);
    }

    if (i && Splits[i-1].maxCrc != LastUi32) {
        fprintf(stderr, "hostconfig: no host for crc in %" PRIu64 ", %" PRIu64 "\n", Splits[i-1].maxCrc, LastUi32);
        return 1;
    }
    if (CheckConfigs) {
        fprintf(stderr, "hostconfig: ClusterConfig not set for %" PRIu32 " hosts of total %" PRISZT "\n", CheckConfigs, Splits.size());
        return 1;
    }
    if (SearchCheckConfigs) {
        fprintf(stderr, "hostconfig: SearchClusterConfig not set for %" PRIu32 " hosts of total %" PRISZT "\n", SearchCheckConfigs, Splits.size());
        return 1;
    }

    if (IsScattered) {
        if (ScatterHosts.empty()) {
            fprintf(stderr, "hostconfig: ScatterTable is empty\n");
            return 1;
        }
        if (!ScatterGroups) {
            fprintf(stderr, "hostconfig: zero scatter groups\n");
            return 1;
        }
        if (Splits.size() != WalGroups.size())
        {
            fprintf(stderr, "hostconfig: group number must be assigned to each segment\n");
            return 1;
        }
        if (ScatterHosts.size() != SctGroups.size())
        {
            fprintf(stderr, "hostconfig: group number must be assigned to each scatter\n");
            return 1;
        }
        for (ui32 i2 = 0; i2 < ScatterGroups; i2++)
        {
            if (Seqs[i2] == 0) {
                fprintf(stderr, "hostconfig: group %i is not assigned to any segment\n", i2);
                return 1;
            }
            if (SctSeqs[i2] == 0) {
                fprintf(stderr, "hostconfig: group %i is not assigned to any scatter\n", i2);
                return 1;
            }
        }
        ui32 scatterCheckConfigs = 0;
        for (ui32 i3 = 0; i3 < ScatterHosts.size(); ++i3)
            scatterCheckConfigs += CheckSearchHostConfigs(ScatterHosts[i3]);
        if (scatterCheckConfigs) {
            fprintf(stderr, "hostconfig: SearchClusterConfig not set for %" PRIu32 " scatter hosts of total %" PRISZT "\n",
                scatterCheckConfigs, ScatterHosts.size());
            return 1;
        }
    }

    return 0;
}

void THostConfig::PrepareSearchConfig() {
    Y_ASSERT(SearchConfigs.empty());
    typedef THashSet<const char*> THosts;

    THosts hosts;
    for (TSearchZoneConfigs::const_iterator confIt = SearchZoneConfigs.begin(); confIt != SearchZoneConfigs.end(); ++confIt) {
        const Comp2Config& conf = **confIt;
        for (Comp2Config::const_iterator hostsIt = conf.begin(); hostsIt != conf.end(); ++hostsIt) {
            hosts.insert(hostsIt->first);
        }
    }

    size_t zonesNum = SearchZoneConfigs.size();
    for (THosts::iterator hostIt = hosts.begin(); hostIt != hosts.end(); ++hostIt) {
        TVector<SearchClusterConfig::TZoneConfig> configs;
        configs.reserve(zonesNum);

        for (size_t i = 0; i < SearchZoneConfigs.size(); ++i) {
            const Comp2Config& conf = *SearchZoneConfigs[i];

            Comp2Config::const_iterator confIt = conf.find(*hostIt);
            if (confIt == conf.end()) {
                confIt = conf.find("_default");
                if (confIt == conf.end())
                    ythrow yexception() << "unable to find search config for host " <<  *hostIt << ", zone " <<  SearchZoneNames[i];
            }

            configs.push_back(SearchClusterConfig::TZoneConfig(SearchZoneNames[i], confIt->second));
        }

        SearchConfigs[*hostIt] = std::make_shared<SearchClusterConfig>(configs);
    }

    SearchZoneConfigs.clear();
}

void THostConfig::GenerateDstScatterConfigs() {
    for (size_t i = 0; i < ScatterHosts.size(); ++i) {
        int walGroupSize = WalSctGroups[SctGroups[i].Group].Walruses.size();
        SearchClusterConfig* clcfg = GetSegmentSearchClusterConfig(ScatterHosts[i]);
        DstScatterConfigs[ScatterHosts[i]] = ClusterConfig(clcfg->Clusters() * walGroupSize);
    }
}

void THostConfig::LoadMem(const char *config) {
    if (int ret = LoadReal(nullptr, config, TString()))
        ythrow yexception() << "config load error: " <<  ret;
    return ;
}

int THostConfig::Load(const char *filename, const TString& cluster) {
    return LoadReal(filename, nullptr, cluster, nullptr);
}

int THostConfig::LoadForHost(const char* filename, const char* hostName, const TString& cluster) {
    return LoadReal(filename, nullptr, cluster, hostName);
}

int THostConfig::LoadReal(const char *filename, const char *config, const TString& cluster, const char* hostName) {
    MySegNum = -1;
    VersionMajor = 2, VersionMinor = 0; // no version block means version 2
    VersionCVSId = "";
    if (filename? !Parse(filename) : !ParseMemory(config))
        return 1;
    BuildGroups();
    int rez = Check();
    if (rez)
        return rez;
    SetMySegmentId(cluster, hostName);

    PrepareSearchConfig();

    if (IsScattered) {
        GenerateDstScatterConfigs();
    }

    MergeFriendsAndHosts();
    return 0;
}

void THostConfig::MergeFriendsAndHosts()
{
    for (auto& types: RevFriends) {
        THostsClusterRanges& hosts = types.second;

        for (size_t i = 0; i < GetNumSegments(); ++i) {
            const TString& segmentName = GetSegmentName(i);
            const ClusterConfig* clusterConfig = GetSegmentClusterConfig(segmentName.data());

            auto result = hosts.insert(THostsClusterRanges::value_type(segmentName, TClusterRanges() ));
            if (!result.second) {
                throw yexception() << segmentName << " has been defined already in FriendsHosts group type " << types.first;
            }
            {
                FriendGrp[types.first].push_back(segmentName.data());
                Segs4Friends[segmentName.data()].push_back(i);
                Friends[i].Friends[types.first].push_back(segmentName.data());
            }

            TClusterRanges& ranges = (result.first)->second;

            int clusterNum = clusterConfig->Clusters();

            const TSegmentFriends::THosts* friendHosts = GetFriends(segmentName.data()).GetFriendHosts(types.first.data());
            if (friendHosts == nullptr) {
                ranges.push_back(TClusterRange(0, clusterNum-1));
                continue;
            }

            TVector<char> cluster(++clusterNum);
            cluster[--clusterNum] = 1;

            for (const auto& host : *friendHosts) {
                const auto it = hosts.find(host);
                if (it == hosts.end()) {
                    throw yexception() << "Friend " << host << " for segment " << segmentName << " wasnt found";
                }
                const TClusterRanges& ranges2 = it->second;
                for (const auto& r : ranges2) {
                    for (size_t k = r.Start; k <= r.End; ++k) {
                        if (cluster[k]) {
                            throw yexception() << "group type " << types.first << ", segment " << segmentName << ", friend " << host <<  " has overlapping cluster";
                        }
                        cluster[k] = 1;
                    }
                }
            }

            for (size_t start = 0, end = 1; end < cluster.size(); ++end) {
                if (cluster[start] > cluster[end]) {
                    start = end;
                    continue;
                }
                if (cluster[start] < cluster[end]) {
                    ranges.push_back(TClusterRange(start, end-1));
                    start = end;
                    continue;
                }
            }
        }
    }
}

void THostConfig::BuildGroups()
{
    ScatterGroups = MaxGroup + 1;
    WalSctGroups.resize(ScatterGroups);
    for (ui32 gr = 0; gr <= MaxGroup; gr++)
    {
        WalSctGroups[gr].Walruses.resize(Seqs[gr]);
        WalSctGroups[gr].Scatters.resize(SctSeqs[gr]);
    }
    ui32 seg = 0;
    for (TGroupSeqs::iterator it = WalGroups.begin(); it != WalGroups.end(); it++, seg++)
    {
        WalSctGroups[it->Group].Walruses[it->Seq] = seg;
    }
    seg = 0;
    for (TGroupSeqs::iterator it = SctGroups.begin(); it != SctGroups.end(); it++, seg++)
    {
        WalSctGroups[it->Group].Scatters[it->Seq] = seg;
    }
}

bool THostConfig::SetSpecialHost(const char* key, const char* value) {
    if (SpecHosts.find(key) != SpecHosts.end()) {
        fprintf(stderr, "hostconfig: SpecialHost (%s) is already set\n", key);
        return false;
    }
    SpecHosts[key] = value;
    return true;
}

bool THostConfig::SetVersionInfo(const char* key, const char* value) {
    if (!stricmp(key, "Version")) {
        char *e, *e1;
        VersionMajor = strtoul(value, &e, 10);
        e1 = e + 1;
        if (*e)
            VersionMinor = strtoul(e + 1, &e1, 10);
        if (e == value || *e != '.' && *e != ',' || e1 == e + 1) {
            fprintf(stderr, "hostconfig: Version string is expected in %s format\n", key);
            return false;
        }
    }
    if (!stricmp(key, "CVS"))
        VersionCVSId = value;
    return true;
}

bool THostConfig::SetHashShift(const char* value) {
    try {
        HashShift = FromString<ui32>(value) % 32;
    } catch (yexception e) {
        return false;
    }

    return true;
}

bool THostConfig::AddScatter(const char* key, const char* value)
{
    ScatterHosts.push_back(key);
    if (!value || (*value == 0)) {
        fprintf(stderr, "hostconfig: bad group number(%s) for key(%s)\n", value, key);
        return false;
    }
    TGroupSeq gs;
    gs.Group = strtoull(value, nullptr, 0);
    gs.Seq = SctSeqs[gs.Group]++;
    SctGroups.push_back(gs);
    if (gs.Group > MaxGroup)
        MaxGroup = gs.Group;
    return true;
}

bool THostConfig::AddSplit(const char* key, const char* value)
{
    struct SplitCrc NewSplit;
    NewSplit.Host = key;
    NewSplit.minCrc = strtoull(value, nullptr, 0);
    const char *ptr = strchr(value, ',');
    if (ptr == nullptr) {
        fprintf(stderr, "hostconfig: bad value(%s) for key(%s)\n", value, key);
        return false;
    }

    NewSplit.maxCrc = strtoull(ptr + 1, nullptr, 0);

    if (NewSplit.minCrc > NewSplit.maxCrc) {
        fprintf(stderr, "hostconfig: bad interval %" PRIu64 ", %" PRIu64 "\n", NewSplit.minCrc, NewSplit.maxCrc);
        return false;
    }

    ptr = strchr(ptr + 1, ',');
    if (ptr == nullptr) {
        fprintf(stderr, "hostconfig: bad group number(%s) for key(%s)\n", value, key);
        return false;
    }
    TGroupSeq gs;
    gs.Group = strtoull(ptr + 1, (char**)&ptr, 0);
    gs.Seq = Seqs[gs.Group]++;
    WalGroups.push_back(gs);
    if (gs.Group > MaxGroup)
        MaxGroup = gs.Group;
    NewSplit.segmentId = Splits.size();
    Splits.push_back(NewSplit);
    SplitName2Id[NewSplit.Host] = NewSplit.segmentId;
    return true;
}

bool THostConfig::AddFriends(const char *type, const char* key, const char* value) {
    if (!type) {
        fprintf(stderr, "section FriendHosts needs 'type' attribute\n");
        return false;
    }
    if (strcmp(type, "zm") && (strlen(type) < 4) || type[strcspn(type, "0123456789")]) {
        throw yexception() << "FriendHosts type name must be at least 4 chars long " << type;
    }
    TVector<const char *> *all = &FriendGrp[type];

    TVector<TStringBuf> res;
    StringSplitter(value).SplitBySet(" ,").SkipEmpty().Collect(&res);

    const auto segmentIt = SplitName2Id.find(key);
    if (segmentIt != SplitName2Id.end()) {
        // reverse writing of friends.
        // segmented_host : friend_host [cluster]
        auto result1 = RevFriends.insert(TFriendsHosts::value_type(type, THostsClusterRanges() ));
        THostsClusterRanges& hostsRanges = (result1.first)->second;

        ui16 segmentId = segmentIt->second;
        auto hostsRangesIt = hostsRanges.end();
        bool maybeRange = false;

        for (size_t n = 0; n < res.size(); n++) {
            int clusterStart = 0;
            int clusterEnd = 0;
            if (maybeRange) {
                if (sscanf(ToString(res[n]).data(), "%d..%d", &clusterStart, &clusterEnd) == 2) {
                    (hostsRangesIt->second).push_back(TClusterRange(clusterStart, clusterEnd));
                    maybeRange = false;
                    continue;
                }
            }
            maybeRange = true;
            auto result2 = hostsRanges.insert(THostsClusterRanges::value_type(ToString(res[n]), TClusterRanges() ));
            hostsRangesIt = result2.first;

            const TString &host = hostsRangesIt->first;
            if (!result2.second) {
                throw yexception() << "FriendHosts host " << host << " has been already defined";
            }

            all->push_back(host.data());
            Segs4Friends[host.data()].push_back(segmentId);
            Friends[segmentId].Friends[type].push_back(host.data());
        }
        return true;
    }

    all->push_back(key);
    TVector<ui16> &mySegments = Segs4Friends[key];
    for (size_t n = 0; n < res.size(); n++) {
        THashMap<const char*, ui32>::const_iterator i = SplitName2Id.find(res[n]);
        if (i == SplitName2Id.end()) {
            fprintf(stderr, "for %s, expected segment name, got '%.*s'\n", key, (int)res[n].size(), res[n].data());
            return false;
        }
        mySegments.push_back(i->second);
        if (Friends[i->second].GetFriends(type)) {
            fprintf(stderr, "trying to set %s as type %s friend for %.*s, which is already set to %s\n", key, type, (int)res[n].size(), res[n].data(), Friends[i->second].GetFriends(type));
            return false;
        }
        Friends[i->second].Friends[type].push_back(key);
    }
    return true;
}

bool THostConfig::SetConfig(const char* key, const char* value, Comp2Config &TheConfigs, const char *errpref) {
    unsigned i = 0;
    if (key == nullptr) {
        fprintf(stderr, "hostconfig: cannot allocate copy of key (%s)\n", key);
        return false;
    }

    if (key[0] != '_' || strcmp(key, "_default")) {
        if (!IsScattered) {
            for(; i < Splits.size(); i++)
                if (Splits[i].Host == key)
                    break;

            if (i == Splits.size()) {
                fprintf(stderr, "hostconfig: cannot assign %sClusterConfig for nonexisting host (%s)\n", errpref, key);
                return false;
            }
        } else {
            for (; i < ScatterHosts.size(); i++)
                if (ScatterHosts[i] == key)
                    break;
            if (i == ScatterHosts.size()) {
                fprintf(stderr, "hostconfig: cannot assign %sClusterConfig for nonexisting scatter host (%s)\n", errpref, key);
                return false;
            }
        }
    }

    Comp2Config::iterator ConfIt = TheConfigs.find(key);
    if (ConfIt != TheConfigs.end()) {
        fprintf(stderr, "hostconfig: %sClusterConfig for host (%s) is already set\n", errpref, key);
        return false;
    }

    int cl = atoi(value);
    if (!cl && !IsScattered) {
        fprintf(stderr, "hostconfig: error parsing %sClusterConfig line (%s) (host=%s)\n", errpref, value, key);
        return false;
    }

    TheConfigs[key] = ClusterConfig(cl);
    return true;
}

template<class V, class T>
V *nstl_find(V *first, V *last, const T& value) {
    std::equal_to<V> eq; // typeof(*first)
    for (; first != last; first++)
        if (eq(*first, value)) break;
    return first;
}

bool THostConfig::SetSearchConfig(const char* zoneName, const char* key, const char* value) {
    size_t zone_id;

    TSearchZoneNames::iterator it = nstl_find(SearchZoneNames.begin(), SearchZoneNames.end(), zoneName);
    if (it != SearchZoneNames.end()) {
        zone_id = std::distance(SearchZoneNames.begin(), it);
    } else {
        SearchZoneNames.push_back(zoneName);
        SearchZoneConfigs.push_back(std::make_shared<Comp2Config>());
        zone_id = SearchZoneNames.size() - 1;
    }

    return SetConfig(key, value, *SearchZoneConfigs[zone_id], zoneName);
}

bool THostConfig::SetScatterInfo(const char* key, const char* value) {
    if (!stricmp(key, "groups"))
        ScatterGroups = FromString<size_t>(value);
    else
        return false;
    return true;
}

ui32 THostConfig::GetSearchSelectionDocsQuota(const char *zoneName) const {
    TMap<const char*, ui32>::const_iterator it = SearchSelectionDocsQuota.find(zoneName);
    if (it != SearchSelectionDocsQuota.end())
        return it->second;
    else
        return std::numeric_limits<ui32>::max();
}

ui64 THostConfig::GetSearchSelectionSizeQuota(const char *zoneName) const {
    TMap<const char*, ui64>::const_iterator it = SearchSelectionSizeQuota.find(zoneName);
    if (it != SearchSelectionSizeQuota.end())
        return it->second;
    else
        return std::numeric_limits<ui64>::max();
}

bool THostConfig::SetSearchSelectionDocsQuota(const char * key, const char* value) {
    SearchSelectionDocsQuota[key] = FromString<ui32>(value);
    return true;
}

bool THostConfig::SetSearchSelectionSizeQuota(const char * key, const char* value) {
    SearchSelectionSizeQuota[key] = FromString<ui64>(value);
    return true;
}

bool THostConfig::SetMetaRobotShardSizeQuota(const char* key, const char* value) {
    MetaRobotShardSizeQuota[key] = FromString<ui64>(value);
    return true;
}

int THostConfig::Compare(const THostConfig *HC2) const {
    TVector<const char*> Dummy;
    unsigned i = 0;
    bool Changed;
    unsigned SumChanged = 0;
    for(; i < Splits.size(); i++) {
        if (HC2->CompareSegment(Splits[i], Changed, Dummy)) {
            fprintf(stderr, "hostconfig: unable to split %s\n", Splits[i].Host);
            return 1;
        }
        if (!Changed)
            continue;
        SumChanged += Dummy.size();
    }
    return 0;
}

int THostConfig::CompareSegment(const SplitCrc &Segment, bool &Changed, TVector<const char*> &SplitSegments) const {
    SplitSegments.clear();
    Changed = false;
    SplitCrc C;
    C.maxCrc = Segment.minCrc;
    TVector<struct SplitCrc>::const_iterator Found =  upper_bound(Splits.begin(), Splits.end(), C, SplitCrcByMax);
    if (Found == Splits.end())
        return 1;
    if (!strcmp(Found->Host, Segment.Host) && Found->minCrc == Segment.minCrc && Found->maxCrc == Segment.maxCrc)
        return 0;
    for(; Found != Splits.end(); Found++) {
        if (Found->minCrc > Segment.maxCrc)
            break;
        SplitSegments.push_back(Found->Host);
    }
    Changed = true;
    return 0;
}

const TClusterRanges& THostConfig::GetFriendsHostsClusterRanges(const char *Host, const char *Type) const {
    const auto itt = RevFriends.find(Type);
    if (itt == RevFriends.end()) {
        throw yexception() << "<FriendsHosts> group type " << Type << "wasnt found";
    }
    const auto ith = (itt->second).find(Host);
    if (ith == (itt->second).end()) {
        throw yexception() << "<FriendsHosts> host " << Host << " for group type " << Type << "wasnt found";
    }
    return ith->second;
}

const ClusterConfig* THostConfig::GetSegmentClusterConfig(unsigned Num) const {
    const char *hn = GetSegmentName(Num);
    if (hn == nullptr)
        return nullptr;
    return GetSegmentClusterConfig(hn);
}

const ClusterConfig* THostConfig::GetSegmentClusterConfig(const char *SegmentName) const {
    Comp2Config::const_iterator confIt = Configs.find(SegmentName);
    if (confIt == Configs.end())
        // that should not happen
        return nullptr;
    return &confIt->second;
}

SearchClusterConfig* THostConfig::GetScatterSearchClusterConfig(ui32 scatterId) const {
    const char *hn = GetScatterHost(scatterId);
    if (hn == nullptr)
        return nullptr;
    return GetSegmentSearchClusterConfig(hn);
}

SearchClusterConfig* THostConfig::GetSegmentSearchClusterConfig(const char *SegmentName) const {
    Comp2SearchConfig::const_iterator confIt = SearchConfigs.find(SegmentName);
    if (confIt == SearchConfigs.end()) {
        if (!strcmp(SegmentName, "_default"))
            return nullptr;
        // that should not happen in better world
        return GetSegmentSearchClusterConfig("_default");
    }
    return confIt->second.get();
}

SearchClusterConfig* THostConfig::GetSearchClusterConfig() const {
    return GetSegmentSearchClusterConfig("_default");
}

const ClusterConfig* THostConfig::GetDstScatterClusterConfig(const char* SegmentName) const {
    Comp2Config::const_iterator confIt = DstScatterConfigs.find(SegmentName);
    assert(confIt != DstScatterConfigs.end());
    return &confIt->second;
}

size_t THostConfig::GetNumScatterPieces(const char* segmentName) const {
    ui32 seg_id = GetSegmentId(segmentName);
    if (seg_id >= GetNumSegments()) {
        ui32 sct_id = GetScatterId(segmentName);
        if (sct_id >= GetNumScatterHosts())
            return 0;
        SearchClusterConfig* clcfg = GetSegmentSearchClusterConfig(segmentName);
        Y_ASSERT(clcfg);
        return WalSctGroups[SctGroups[sct_id].Group].Walruses.size() * clcfg->Clusters();
    }
    size_t pieces = 0;
    TVector<ui32> sct_ids;
    GetScatterIds(seg_id, sct_ids);
    for (ui32 sct_idx = 0; sct_idx < sct_ids.size(); ++sct_idx) {
        SearchClusterConfig* clcfg = GetSegmentSearchClusterConfig(ScatterHosts[sct_ids[sct_idx]]);
        if (!clcfg)
            ythrow yexception() << "No search cluster config for scatter host " <<  ScatterHosts[sct_ids[sct_idx]];
        pieces += clcfg->Clusters();
    }
    return pieces;
}

void THostConfig::GetNotSpamScatterPieces(const char* segmentName, TVector<ui32>& notSpamScatterPieces) const {
    notSpamScatterPieces.resize(0);
    ui32 seg_id = GetSegmentId(segmentName);
    if (seg_id >= GetNumSegments())
        return;
    size_t pieces = 0;
    TVector<ui32> sct_ids;
    GetScatterIds(seg_id, sct_ids);
    for (ui32 sct_idx = 0; sct_idx < sct_ids.size(); ++sct_idx) {
        SearchClusterConfig* clcfg = GetSegmentSearchClusterConfig(ScatterHosts[sct_ids[sct_idx]]);
        if (!clcfg)
            ythrow yexception() << "No search cluster config for scatter host " <<  ScatterHosts[sct_ids[sct_idx]];

        for (int uploadCluster = 0; uploadCluster < clcfg->UploadClusters(); ++uploadCluster) {
            notSpamScatterPieces.push_back(uploadCluster + pieces);
        }
        pieces += clcfg->Clusters();
    }

}

void THostConfig::GetNotSpamScatterPiecesOnScatter(const char* scatterName, TVector<ui32>& notSpamScatterPieces) const {
    notSpamScatterPieces.resize(0);
    ui32 sct_id = GetScatterId(scatterName);
    if (sct_id >= GetNumScatterHosts())
        return;

    SearchClusterConfig* clcfg = GetSegmentSearchClusterConfig(scatterName);
    if (!clcfg)
        ythrow yexception() << "No search cluster config for scatter host " <<  scatterName;

//    ui32 walGroupSize = Splits.size() / ScatterGroups;
    ui32 walGroupSize = WalSctGroups[SctGroups[sct_id].Group].Walruses.size();
    ui32 pieces = 0;
    for (ui32 walrusIndex = 0; walrusIndex < walGroupSize; ++walrusIndex) {
        for (int uploadCluster = 0; uploadCluster < clcfg->UploadClusters(); ++uploadCluster) {
            notSpamScatterPieces.push_back(uploadCluster + pieces);
        }
        pieces += clcfg->Clusters();
    }
}


void THostConfig::GetSpamScatterPieces(const char* segmentName, TVector<ui32>& spamScatterPieces) const {
    spamScatterPieces.resize(0);
    ui32 seg_id = GetSegmentId(segmentName);
    if (seg_id >= GetNumSegments())
        return;
    size_t pieces = 0;
    TVector<ui32> sct_ids;
    GetScatterIds(seg_id, sct_ids);
    for (ui32 sct_idx = 0; sct_idx < sct_ids.size(); ++sct_idx) {
        SearchClusterConfig* clcfg = GetSegmentSearchClusterConfig(ScatterHosts[sct_ids[sct_idx]]);
        if (!clcfg)
            ythrow yexception() << "No search cluster config for scatter host " <<  ScatterHosts[sct_ids[sct_idx]];

        for (int cluster = clcfg->UploadClusters(); cluster < clcfg->Clusters(); ++cluster) {
            spamScatterPieces.push_back(cluster + pieces);
        }
        pieces += clcfg->Clusters();
    }

}

void THostConfig::GetSpamScatterPiecesOnScatter(const char* scatterName, TVector<ui32>& spamScatterPieces) const {
    spamScatterPieces.resize(0);
    ui32 sct_id = GetScatterId(scatterName);
    if (sct_id >= GetNumScatterHosts())
        return;

    SearchClusterConfig* clcfg = GetSegmentSearchClusterConfig(scatterName);
    if (!clcfg)
        ythrow yexception() << "No search cluster config for scatter host " <<  scatterName;

//    ui32 walGroupSize = Splits.size() / ScatterGroups;
    ui32 walGroupSize = WalSctGroups[SctGroups[sct_id].Group].Walruses.size();
    ui32 pieces = 0;
    for (ui32 walrusIndex = 0; walrusIndex < walGroupSize; ++walrusIndex) {
        for (int notUploadCluster = clcfg->UploadClusters(); notUploadCluster < clcfg->Clusters(); ++notUploadCluster) {
            spamScatterPieces.push_back(notUploadCluster + pieces);
        }
        pieces += clcfg->Clusters();
    }
}

ui32 THostConfig::GetRawScatterPlanClusterShift(ui32 segmentId, ui32 scatterHostIdIndexInGroup) const {
    ui32 clusterShift = 0;

    const std::vector<ui16>& scatterIds = WalSctGroups[WalGroups[segmentId].Group].Scatters;

    Y_VERIFY(scatterHostIdIndexInGroup < scatterIds.size(), "Scatter index %u exceed group size %zu", scatterHostIdIndexInGroup, scatterIds.size());

    for (ui32 scatterIndex = 0; scatterIndex < scatterHostIdIndexInGroup; ++scatterIndex) {
        SearchClusterConfig* clcfg = GetSegmentSearchClusterConfig(ScatterHosts[scatterIds[scatterIndex]]);
        if (!clcfg)
            ythrow yexception() << "No search cluster config for scatter host " <<  ScatterHosts[scatterIds[scatterIndex]];

        clusterShift += clcfg->Clusters();
    }

    return clusterShift;
}

const char* THostConfig::GetScatterDst(const char *segmentHostName, ui32 segmentSctCluster, /*out*/ ui32 &dstSctCluster) const {

    ui32 segmentId = GetSegmentId(segmentHostName);
    ui32 pieces = GetNumScatterPieces(segmentHostName);
    if (segmentId >= GetNumSegments() || segmentSctCluster >= pieces) {
        dstSctCluster = Max<ui32>();
        return nullptr;
    }

    TVector<ui32> scatterGroupHostIds;
    GetScatterIds(segmentId, scatterGroupHostIds);

//    ui32 walGroupSize = Splits.size() / ScatterGroups;
    ui32 wal_seq = WalGroups[segmentId].Seq;
    TVector<ui32> scatterIndexToClusterShift(scatterGroupHostIds.size());
    for (ui32 scatterIndex = 0; scatterIndex < scatterGroupHostIds.size(); ++scatterIndex) {
        scatterIndexToClusterShift[scatterIndex] = GetRawScatterPlanClusterShift(segmentId, scatterIndex);
    }

    ui32 scatterIndex = 0;
    for (; scatterIndex < scatterGroupHostIds.size() - 1; ++scatterIndex) {

        ui32 scatterHostBeginCluster = scatterIndexToClusterShift[scatterIndex];
        ui32 scatterHostEndCluster = scatterIndexToClusterShift[scatterIndex + 1] - 1;

        if (segmentSctCluster >= scatterHostBeginCluster && segmentSctCluster <= scatterHostEndCluster) {
            const char* scatterHostName = GetScatterHost(scatterGroupHostIds[scatterIndex]);
            dstSctCluster = segmentSctCluster - scatterHostBeginCluster  + GetSegmentSearchClusterConfig(scatterHostName)->Clusters() * wal_seq;
            return scatterHostName;
        }

    }

    const char* scatterHostName = GetScatterHost(scatterGroupHostIds[scatterIndex]);
    dstSctCluster = segmentSctCluster - scatterIndexToClusterShift[scatterIndex] + GetSegmentSearchClusterConfig(scatterHostName)->Clusters() * wal_seq;
    return scatterHostName;

}

void THostConfig::GetScatterIds(ui32 segmentSrc, TVector<ui32> &scattersDst) const {
    scattersDst.clear();
    if (segmentSrc >= Splits.size())
        return;
    const std::vector<ui16>& scatters = WalSctGroups[WalGroups[segmentSrc].Group].Scatters;
    for (std::vector<ui16>::const_iterator it = scatters.begin(); it != scatters.end(); it++)
    {
        scattersDst.push_back(*it);
    }
}

ui32 THostConfig::GetMyScatterCount()
{
    return WalSctGroups[WalGroups[MySegNum].Group].Scatters.size();
}

ui32 THostConfig::GetMyGroup(const ui32 scatterId) const {
    return SctGroups[scatterId].Group;
}

void THostConfig::GetGroupScatterIds(ui32 group, TVector<ui32> &scattersDst) const {
    scattersDst.clear();
    if (group >= ScatterGroups)
        return;
    const std::vector<ui16>& scatters = WalSctGroups[group].Scatters;
    for (std::vector<ui16>::const_iterator it = scatters.begin(); it != scatters.end(); it++)
    {
        scattersDst.push_back(*it);
    }
}

void THostConfig::GetSegmentIds(ui32 scatterDst, TVector<ui32> &segmentsSrc) const {
    segmentsSrc.clear();
    if (scatterDst >= ScatterHosts.size())
        return;
    const std::vector<ui16>& walruses = WalSctGroups[SctGroups[scatterDst].Group].Walruses;
    for (std::vector<ui16>::const_iterator it = walruses.begin(); it != walruses.end(); it++)
    {
        segmentsSrc.push_back(*it);
    }
}

void THostConfig::GetGroupSegmentIds(ui32 sct_group_id, TVector<ui32> &SegmentsSrc) const {
    SegmentsSrc.clear();
    if (sct_group_id >= ScatterGroups)
        return;
    const std::vector<ui16>& walruses = WalSctGroups[sct_group_id].Walruses;
    for (std::vector<ui16>::const_iterator it = walruses.begin(); it != walruses.end(); it++)
    {
        SegmentsSrc.push_back(*it);
    }
}

const char *THostConfig::GetSpecialHost(const char *role) const {
    TSpecHosts::const_iterator i = SpecHosts.find(role);
    if (i == SpecHosts.end())
        return nullptr;
    return i->second;
}

const TSegmentFriends& THostConfig::GetFriends(const char* segmentName) const {
    ui32 seg_id = GetSegmentId(segmentName);
    if (seg_id >= GetNumSegments())
        throw yexception() << "GetFriends: " << segmentName << " is not a segment";
    return Friends[seg_id];
}

void THostConfig::GetFriendSegs(const char *host, TVector<ui32> &segments) const {
    segments.clear();
    TSegs4Friends::const_iterator i = Segs4Friends.find(host);
    if (i != Segs4Friends.end())
        segments.insert(segments.end(), i->second.begin(), i->second.end());
}

const TVector<const char*> &THostConfig::GetFullList(const char *type) const {
    THashMap<TString, TVector<const char*> >::const_iterator it = FriendGrp.find(type);
    if (it == FriendGrp.end())
        throw yexception() << "GetFullList: no list for " << type;
    return it->second;
}

ui64 THostConfig::GetMetaRobotShardSizeQuota(const char* segmentName) const {

    TMap<const char* , ui64> :: const_iterator metaRobotShardSizeQuotaIterator = MetaRobotShardSizeQuota.find(segmentName);

    if (metaRobotShardSizeQuotaIterator == MetaRobotShardSizeQuota.end())
        metaRobotShardSizeQuotaIterator = MetaRobotShardSizeQuota.find("_default");

    if (metaRobotShardSizeQuotaIterator != MetaRobotShardSizeQuota.end())
        return metaRobotShardSizeQuotaIterator->second;
    else
        ythrow yexception() << "unable to find meta-robot shard size quota for host " << segmentName;
}

void THostConfig::SetMySegmentId(const TString& cluster, const char* hostName) {
    if (hostName && hostName[0]) {
        MyHostName = hostName;
    } else {
        char host[100];
        if (gethostname(host, sizeof(host)))
            host[0] = 0;
        MyHostName = host;
    }
    const char *ptr = strchr(MyHostName.data(), '.');
    if (ptr)
        MyHostName = MyHostName.substr(0, ptr - MyHostName.data());
    MySegmentName = MyHostName;
    if (!!cluster) {
        MySegmentName += "+";
        MySegmentName += cluster;
    }
    MySegNum = GetSegmentId(MySegmentName.data());
    if ((unsigned)MySegNum >= GetNumSegments())
        MySegNum = -1;
}

ui64 HostCrc4Config(TStringBuf Name, const char* config)
{
    static TDefHostConfig hconfig(config ? config : THostConfig::HostCfgDefName);
    if (hconfig.Result != 0)
        ythrow yexception() << "Can't load config " <<  (config ? config : THostConfig::HostCfgDefName);
    return hconfig.GetCrc(Name);
}

struct TIterTolower {
    const ui8 *Ptr;
    TIterTolower(const char *ptr) : Ptr((ui8*)ptr) {}
    ui8 operator *() const {
        return (ui8)tolower(*Ptr);
    }
    TIterTolower operator++(int) {
        return TIterTolower((const char*)Ptr++);
    }
    bool operator !=(const TIterTolower &with) const {
        return Ptr != with.Ptr;
    }
};

TCrcOverride::TCrcOverride() {
}

void TCrcOverride::AddName(const char *name, ui32 crc, const TConfigNameSet& dom2) {
    const char *d1 = strrchr(name, '.');
    const char *base = d1? (const char*)memrchr(name, '.', d1 - name) : nullptr;
    if (base && dom2.contains(base + 1))
        base = (const char*)memrchr(name, '.', base - name);
    const char *sub = base? (const char*)memrchr(name, '.', base - name) : nullptr;
    //if (!base)
    //    ythrow yexception() << "Sorry, L2 and Dom2 subdomains are not yet supported in CrcOverride (at " << name << ")";
    base = base? base + 1 : name;
    if (sub)
        ythrow yexception() << "Sorry, deep subdomains are not yet supported in CrcOverride (at " << name << ")";
    sub = sub? sub + 1 : name;

    TName2Crc& nc = (*this)[base];
    TStringBuf subBuf(sub, base == name? base : base - 1);
    //warnx("base = '%s', subBuf = '%.*s'", base, (int)+subBuf, ~subBuf);
    nc[subBuf] = crc;
    if (!subBuf)
        nc.FullDomain = true;

    //TStringBuf t(sub, base == name? base : base - 1);
    //printf("[%s][%.*s] <- %u\n", base, (int)+t, ~t, crc);
}

inline bool TCrcOverride::Check(const TStringBuf &Name, size_t dom2off, ui32 &crc) const {
    const_iterator it;
    if ((it = find(Name.Tail(dom2off))) == end())
        return false;

    TStringBuf dom(Name, 0, dom2off? dom2off - 1 : 0);
    dom2off = dom.rfind('.');
    dom2off = dom2off != TStringBuf::npos? dom2off + 1 : 0;
    TStringBuf segm = dom.Tail(dom2off);
    TName2Crc::const_iterator nit = it->second.find(segm);
    if (nit != it->second.end()) {
        crc = nit->second;
        return true;
    }
    if (it->second.FullDomain && (nit = it->second.find(TStringBuf(""))) != it->second.end()) {
        crc = nit->second;
        return true;
    }
    return false;
}

ui64 THostConfig::GetCrc(TStringBuf Name) const {
    Name = GetOnlyHost(Name);

    TStringBuf dom(Name, 0, Name.rfind('.'));
    size_t dom2off = dom.rfind('.');
    dom2off = dom2off != TStringBuf::npos? dom2off + 1 : 0;

    TStringBuf segm = dom.SubStr(dom2off);
    ui32 crc = FnvHash<ui32>(TIterTolower(segm.begin()), TIterTolower(segm.end()));

    if (dom2off && Dom2.contains(Name.SubStr(dom2off))) {
        dom.Trunc(dom2off - 1);
        dom2off = dom.rfind('.');
        dom2off = dom2off != TStringBuf::npos? dom2off + 1 : 0;
        segm = dom.SubStr(dom2off);
        if (Dom2Proc == D2PS_FullSpread)
            crc ^= FnvHash<ui32>(TIterTolower(segm.begin()), TIterTolower(segm.end()));
        else
            crc ^= 0x1FFFFFFF & FnvHash<ui32>(TIterTolower(segm.begin()), TIterTolower(segm.end()));
    }

    if (HashShift) {
        crc = (crc >> HashShift) | (crc << (8 * sizeof(crc) - HashShift));
    }

    CrcOverride.Check(Name, dom2off, crc);

    return crc;
}
