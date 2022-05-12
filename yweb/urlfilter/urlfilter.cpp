#include <util/system/compat.h>
#include <util/system/maxlen.h>
#include <library/cpp/uri/http_url.h>
#include <library/cpp/string_utils/url/url.h>

#include "urlfilter.h"
#include "urlfilter_int.h"

TUrlFilter::TUrlFilter() {
}

TUrlFilter::~TUrlFilter() {
}

TUrlFilter GlobalFlt1, GlobalFlt2;
TUrlFilter *GlobalFlt = &GlobalFlt1;

static ui32 DefFilter = 0;

TUrlFilterData::TUrlFilterData()
    : FilterData(nullptr)
    , NodeNum(0)
    , FirstChar(nullptr)
    , BadExtensions(nullptr)
    , RootNode(&DefFilter)
{}

// unfortunately this implementation is 5% faster than one with FilterReject and FindChild being class members
template <int FirstCharOnly> static const ui32 *FindChild(const ui32 *Node, const char *key, const char *firstchar, const ui32 *Filter);
static int FilterReject(char *Host, char *Path, const ui32 *root, const char *firstchar, const ui32 *Filter);

int FilterReject(const char *URL)
{
    return GlobalFlt->Reject(URL);
}

int TUrlFilter::Reject(const char *URL) const {
    THttpURL Rel;
    int ret = Rel.Parse(URL);

    if (ret)
        return ret;
    if (Rel.GetScheme() != THttpURL::SchemeHTTP)
        return UrlFilterBadScheme;
    if (Rel.IsNull(THttpURL::FlagHost) || !strchr(Rel.Get(THttpURL::FieldHost), '.'))
        return UrlFilterBadFormat;
    if (Rel.IsNull(THttpURL::FlagPath) || Rel.Get(THttpURL::FieldPath)[0] != '/')
        return UrlFilterBadPath;
    if (strlen(Rel.Get(THttpURL::FieldPath)) >= URL_MAX || strlen(Rel.Get(THttpURL::FieldHost)) >= HOST_MAX)
        return UrlFilterTooLong;
    TString netLoc = Rel.PrintS(THttpURL::FlagHostPort); // todo: rewrite FilterReject() to avoid print/scan port
    return Reject2(netLoc.data(), Rel.Get(THttpURL::FieldPath));
}

int FilterReject2(const char *host, const char *path)
{
    return GlobalFlt->Reject2(host, path);
}

int TUrlFilter::Reject2(const char *host, const char *path) const {
    // reject https hosts as http
    if (!strncmp(host, "https://", 8))
        host += 8;

    char Host[HOST_MAX], Path[URL_MAX];
    if (path) {
        NormalizeUrlName(Path, path, URL_MAX);
        if (!IsGoodExtension(Path))
            return UrlFilterSuffix;
    }
    NormalizeHostName(Host, host, HOST_MAX);
    return FilterReject(Host, path? Path : nullptr, Obj().RootNode, Obj().FirstChar, Obj().FilterData);
}

int FilterRejectHost(const char *host)
{
    return GlobalFlt->RejectHost(host);
}

//*********************************************************************
// static int isGoodExtension(const char* path)
//*********************************************************************
int isGoodExtension(const char* path)
{
    return GlobalFlt->IsGoodExtension(path);
}

bool TUrlFilter::IsGoodExtension(const char* path) const {
    if (!Obj().BadExtensions)
        return 1;
    if (!path || !*path)
        return 0;
    char *buf = (char*)alloca(strlen(path)+2);
    strcpy(buf, path);
    char *ptr = strchr(buf, '?');
    if (ptr)
        *ptr = 0;
    ptr = strrchr(buf, '/');
    char *dot = strrchr(ptr ? ptr : buf, '.');
    if (!dot || !dot[1])
        return 1;
//    strlwr(dot);
    strcat(dot, ".");
    return strstr(Obj().BadExtensions, dot) == nullptr;
}

#define NODE2STR(p) (&firstchar[*(p)])
#define FINDC0(node,path) FindChild<0>(node, path, firstchar, Filter)
//*********************************************************************
// int FilterFind(const char *Host, const char *Path)
//*********************************************************************
static int FilterReject(char *Host, char *Path, const ui32 *root, const char *firstchar, const ui32 *Filter)
{
    char *colon, *domain, *query, *path, *slash;
    bool domenable = false;
    const ui32 *NextNode, *Node;
    Node = root;

    // Scan domain
    colon = strchr(Host, ':');
    if (colon)
        *colon = 0;
    for(domain = nullptr; domain != Host; Node = NextNode) {
        domenable = domenable || (*Node & (SAME_MASK | NEXT_MASK));
        if (domain)
            *(domain-1) = 0;
        domain = strrchr(Host, '.');
        domain = domain ? domain+1 : Host;
        if (
            !(*Node&NUM_MASK) ||
            !((NextNode = FINDC0(Node, domain)) || (NextNode = FINDC0(Node, "?")))
        )
            return *Node&SAME_MASK ? 0
                : (domenable ? (int)UrlFilterDomain : (int)UrlFilterExtDomain);
    }

    if (!(*Node&NUM_MASK))
        return *Node&NEXT_MASK ? 0
            : (*Node&SAME_MASK ? (int)UrlFilterHost :(int)UrlFilterDomain);

    // Scan Port
    if (colon)
        *colon = ':';
    if (!(NextNode = FINDC0(Node, colon ? colon : ":80")) && !(NextNode = FINDC0(Node, ":?")))
        return *Node&NEXT_MASK ? 0
            : FindChild<1>(Node, ":", firstchar, Filter) ? (int)UrlFilterPort : (int)UrlFilterHost;

    Node = NextNode;
    if (!Path)
        return !(*Node&NUM_MASK) && (*Node&NEXT_MASK) ? 0
            : !(*Node&NUM_MASK) && !(*Node&NEXT_MASK) ? (int)UrlFilterPort
            : (int)UrlFilterUrl;

    // Scan Path
    query = (char*)strchr(Path, '?');
    if (query)
        *query = 0;
    for(path = (char*)Path; path; Node = NextNode, path = slash) {
        slash = strchr(path+1, '/');
        if (slash)
            *slash = 0;
        if (
            !(*Node&NUM_MASK) ||
            !((NextNode = FINDC0(Node, path)) || (NextNode = FINDC0(Node, "/?")))
        )
            return *Node&NEXT_MASK ? 0 : (int)UrlFilterUrl;
        if (slash)
            *slash = '/';
    }
    return *Node&SAME_MASK ? 0 : (int)UrlFilterUrl;
}

//*********************************************************************
// static ui32 *FindChild(ui32 *Node, const char *key) (Microsoft bsearch)
//*********************************************************************
#define PTR2NODE(ptr) (Filter+Node[(*Node&NUM_MASK)+1+(ptr-&Node[1])])

template <int prefix>
static const ui32 *FindChild(const ui32 *Node, const char *key, const char *firstchar, const ui32 *Filter)
{
    const ui32 *lo, *hi, *mid;
    int num, half, result;

    num = *Node&NUM_MASK;
    lo = &Node[1];
    hi = lo + (num - 1);

    while (lo <= hi)
        if ((half = num / 2))
        {
            mid = lo + (num & 1 ? half : (half - 1));
            result = prefix ? ((unsigned char)key[0] - (unsigned char)NODE2STR(mid)[0]) : strcmp(key, NODE2STR(mid));
            if (!result)
                return PTR2NODE(mid);
            else if (result < 0)
            {
                hi = mid - 1;
                num = num & 1 ? half : half-1;
            }
            else
            {
                lo = mid + 1;
                num = half;
            }
        }
        else if (num)
            return strcmp(key, NODE2STR(lo)) ? nullptr : PTR2NODE(lo);
        else
            break;

    return(nullptr);
}

//*********************************************************************
// int FilterLoad(const char *fname)
//*********************************************************************
void TUrlFilterData::Init(bool isMap, const char *msgName) {
    ui32 *data = (ui32*)Start;
    bool newVer = data[4] == 0 && data[5] == NEXT_MASK &&
                  data[6] == SAME_MASK && data[7] == (SAME_MASK | NEXT_MASK);
    if (!newVer && isMap)
        throw yexception() << "\"" << msgName << "\": old-format filter-so files can't be mapped";

    NodeNum = Hdr().NodeNum;
    FirstChar = (char*)&data[NodeNum];
    BadExtensions = FirstChar + Hdr().BadExtensionsOffset;
    RootNode = &data[Hdr().RootPos];
    FilterData = data;

    if (strlen(BadExtensions) < 2)
        BadExtensions = nullptr; // optimize

    if (!newVer) {
        data[0] = 0;
        data[1] = NEXT_MASK;
        data[2] = SAME_MASK;
        data[3] = SAME_MASK | NEXT_MASK;
    }
}

void TUrlFilterData::Swap(TUrlFilterData &with) {
    TDataFile<TFilterSoHdr>::swap(with);
    std::swap(FilterData, with.FilterData);
    std::swap(NodeNum, with.NodeNum);
    std::swap(FirstChar, with.FirstChar);
    std::swap(BadExtensions, with.BadExtensions);
    std::swap(RootNode, with.RootNode);
}

void TUrlFilter::Load(const char *fname, EDataLoadMode loadMode) {
    Obj().LoadFilter(fname, loadMode);
}

void TUrlFilter::Precharge() const {
    Obj().Precharge();
}

int FilterLoad(const char *fname)
{
    // try .. catch?
    GlobalFlt->Load(fname, DLM_READ /*DLM_DEFAULT*/);
    return 0;
}

void FilterSwap() {
    if (GlobalFlt == &GlobalFlt1)
        GlobalFlt = &GlobalFlt2;
    else
        GlobalFlt = &GlobalFlt1;
}

void TUrlFilter::PrintNode(const ui32 *Node, int ident, bool friendly, const TString &me) const
{
    ui32 subnodes = *Node&NUM_MASK;
    ui32 flags = (*Node&NEXT_MASK ? 1 : 0) + (*Node&SAME_MASK ? 2 : 0);
    printf("\t%u\t%u\n", flags, subnodes);
    ident += 4;
    if (!subnodes)
        return;
    const ui32 *child = &Node[1];
    const char *firstchar = Obj().FirstChar;
    const ui32 *Filter = Obj().FilterData;
    for(ui32 i = 0; i < subnodes; i++, child++) {
        const char *frag = NODE2STR(child);
        TString child_name;
        if (friendly) {
            child_name = me + (me.size() && *frag != ':' && *frag != '/'? "." : "") + frag;
            printf("%s", child_name.data());
        } else
            printf("%*s%s", ident, "", frag);
        PrintNode(PTR2NODE(child), ident, friendly, child_name);
    }
}

void TUrlFilter::PrintRootNode(bool friendly) const {
    printf("BadExtensions: %s\n", Obj().BadExtensions);
    printf(".");
    PrintNode(Obj().RootNode, 0, friendly, TString());
}
