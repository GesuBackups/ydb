#include <stdio.h>
#include <stdlib.h>
#ifdef _win_
  #include <io.h> // isatty
#endif

#include <library/cpp/regex/glob/glob_compat.h>
#include <util/system/compat.h>
#include <library/cpp/deprecated/fgood/ffb.h>
#include <util/memory/segmented_string_pool.h>
#include <util/generic/yexception.h>
#include <util/generic/hash.h>
#include <util/folder/dirut.h>

#include "urlfilter_int.h"

using namespace std;

static int global_error = 0;

#define errmess(msg, msg_arg) \
    warnx("%s(%d): error: " msg, *fname, line_number, msg_arg), global_error = 1
    //throw yexception("%s(%d): error: " msg, *fname, line_number, msg_arg)

struct str_less {
    bool operator()(const char* x, const char* y) const {
        return strcmp(x, y) < 0;
    }
};

struct IFDescr {
    int Prio; // 0 = *.spm, 1 = *.flt
    const char *Name;
    IFDescr() {}
    IFDescr(const char *name) : Name(name) {
        int len = strlen(name);
        Prio = len < 4 || strcmp(name + len - 4, ".spm");
    }
    bool operator <(const IFDescr &w) const {
        return Prio < w.Prio || Prio == w.Prio && strcmp(Name, w.Name) < 0;
    }
};

struct Node {
    bool Enable;
    bool UrlsDir; // path ends up with port or slash -> affects children with urls

    int Lineno;
    IFDescr *FDescr; // also used as a flag

    ui32 NodePathLen;
    ui32 NodePath[256];

    Node() : Enable(false), UrlsDir(false), Lineno(0), FDescr(nullptr), NodePathLen(0) { *NodePath = 0; }

    bool ParseInput(char *url, bool disable, IFDescr *fd, int line_number, class Filter &UrlFilter);
    size_t StorageSize() const;

    bool IsSpm() const { return FDescr && FDescr->Prio == 0; }
    void Errmsg(const char *mes, const Node &node2, const char *name_offs) const;
    TString PrintNode(const char *name_offs) const;
};

inline bool Less4Node(const Node *a, const Node *b) {
    size_t path_max = std::min(a->NodePathLen, b->NodePathLen);
    for(size_t n = 0; n < path_max; n++)
        if (a->NodePath[n] < b->NodePath[n])
            return true;
        else if (a->NodePath[n] > b->NodePath[n])
            return false;
    if (a->NodePathLen < b->NodePathLen)
        return true;
    if (a->NodePathLen > b->NodePathLen)
        return false;

    if  (a->UrlsDir < b->UrlsDir)
        return true;
    if  (a->UrlsDir > b->UrlsDir)
        return false;

    // FDescr's are sorted, so just pointer comparison is sufficient
    if (a->FDescr < b->FDescr) return true;
    if (a->FDescr > b->FDescr) return false;
    return a->Lineno < b->Lineno;
}

TString Node::PrintNode(const char *name_offs) const {
    TString ret;
    for (ui32 n = 0; n < NodePathLen; n++) {
        const char *frag = name_offs + NodePath[n];
        if (n != 0 && *frag != ':' && *frag != '/')
            ret.prepend(TString::Join(frag, "."));
        else
            ret += frag;
    }
    return ret;
}

void Node::Errmsg(const char *mes, const Node &node2, const char *name_offs) const {
    global_error = 1;
    if (FDescr)
        fprintf(stderr, "%s(%d) [%s]: %s", FDescr->Name, Lineno, PrintNode(name_offs).data(), mes);
    else
        fprintf(stderr, "defaults: %s", mes);
    if (node2.FDescr)
        fprintf(stderr, " %s(%d) [%s]\n", node2.FDescr->Name, node2.Lineno, node2.PrintNode(name_offs).data());
    else
        fprintf(stderr, " defaults\n");
}

enum SubNodeType {
    SNT_NOTSUB = 0,
    SNT_SAME   = 1,
    SNT_CHILD  = 2,
};

SubNodeType IsSubnode(const Node *node, ui32 path_depth, const Node *SubNode) {
    assert(path_depth <= node->NodePathLen);
    if (SubNode->NodePathLen < path_depth)
        return SNT_NOTSUB;
    for(ui32 n = 0; n < path_depth; n++)
        if (node->NodePath[n] != SubNode->NodePath[n])
            return SNT_NOTSUB;
    if (SubNode->NodePathLen == path_depth)
        return SNT_SAME;
    return SNT_CHILD;
}

bool IsEqual(const Node *node1, const Node *node2) {
    if (node1->NodePathLen != node2->NodePathLen)
        return false;
    return !memcmp(node1->NodePath, node2->NodePath, node1->NodePathLen * sizeof(node1->NodePath[0]));
}

class Filter {
    Node Root;
    TVector<Node*> Nodes;
    Node **CurNode, **EndNode; // iterators
    typedef THashMap<const char*, int> TDict;
    TDict Dict;
    TVector<const char *> DictNames;
    TVector<ui32> Tree;
    TVector<char> Names;
    TString Extensions;

    segmented_string_pool NamesPool;
    segmented_pool<char> NodesPool;
    int ignoreConflicts;

public:
    Filter(int conflicts, bool enableRoot = false)
        : NodesPool(1024 * 1024), ignoreConflicts(conflicts), SlashId(AddName("/"))
    {
        Root.Enable = enableRoot;
    }

    const ui32 SlashId;
    ui32 AddName(const char *name) {
        THashMap<const char*, int>::iterator i = Dict.find(name);
        if (i != Dict.end())
            return i->second;
        ui32 new_id = (ui32)Dict.size();
        Dict.insert(TDict::value_type(NamesPool.append(name), new_id));
        return new_id;
    }

    void AddNode(Node &tmp_node) {
        Node *node = (Node*)NodesPool.append((char*)&tmp_node, tmp_node.StorageSize());
        Nodes.push_back(node);
    }

    void SortNames() {
        TVector<const char *> names;
        names.reserve(Dict.size());
        // first we remap as temp. bitmap, to ensure no unused entries
        TVector<ui32> remap(Dict.size(), false);
        for (TVector<Node*>::iterator i = Nodes.begin(); i != Nodes.end(); ++i)
            for (ui32 *j = (*i)->NodePath, *e = j + (*i)->NodePathLen; j != e; j++)
                remap[*j] = true;
        for (THashMap<const char*, int>::iterator i = Dict.begin(); i != Dict.end(); ++i)
            if (remap[i->second])
                names.push_back(i->first);
        sort(names.begin(), names.end(), str_less());
        int offset = 0;
        for (TVector<const char *>::iterator name = names.begin(); name != names.end(); ++name) {
            THashMap<const char*, int>::iterator i = Dict.find(*name);
            remap[i->second] = offset;//name - names.begin();
            i->second = offset;
            offset += strlen(*name) + 1;
        }
        // 2nd pass is faster than realloc (or m.b. I could just save DictNames to file?)
        TVector<char>(offset).swap(Names);
        offset = 0;
        for (TVector<const char *>::iterator name = names.begin(); name != names.end(); ++name) {
            strcpy(&Names[offset], *name);
            offset += strlen(*name) + 1;
        }
        // fix path ids
        for (TVector<Node*>::iterator i = Nodes.begin(); i != Nodes.end(); ++i)
            for (ui32 *j = (*i)->NodePath, *e = j + (*i)->NodePathLen; j != e; j++)
                *j = remap[*j];
        DictNames.swap(names);
    }

    void SortNodes() {
        sort(Nodes.begin(), Nodes.end(), Less4Node);
    }

    void SetExtensions(TString ext) {
        Extensions = ext;
    }

    void ResolveConflicts() {
        // Finds duplicate and conflicting nodes
        if (Nodes.empty())
            return;
        Node **cur = &*Nodes.begin(), **end = cur + Nodes.size(), **dst = cur + 1;
        Node *prev_node = *cur++;
        for (; cur != end; cur++) {
            Node *node = *cur;
            if (prev_node->UrlsDir != node->UrlsDir || !IsEqual(prev_node, node)) {
                prev_node = node;
                *dst++ = node;
                continue;
            }
            if (!prev_node->IsSpm()) {
                if (prev_node->Enable != node->Enable) {
                    if (!(ignoreConflicts & FTE_Conflict))
                        node->Errmsg("conflicts with", *prev_node, Names.begin());
                } else {
                    if (!(ignoreConflicts & FTE_Duplicate))
                        node->Errmsg("duplicates", *prev_node, Names.begin());
                }
            }
        }
        Nodes.resize(dst - &*Nodes.begin());
    }

    int MakeNode(const Node *parent, size_t path_depth) {
        int flags = -1;
        TVector<ui32> childnames;
        TVector<ui32> childoff;

        const Node *const loop_node = *CurNode;
        const Node *parent4dom = parent, *parent4url = parent;

        SubNodeType subnode_type;
        while(CurNode != EndNode && (subnode_type = IsSubnode(loop_node, path_depth, *CurNode))) {
            if (subnode_type == SNT_SAME) {
                // This node

                const Node *node = *CurNode;

                if (node->UrlsDir) parent = parent4dom;
                else parent4dom = node;
                parent4url = node;
                if (!parent->Enable && !node->Enable || parent->IsSpm()) {
                    if (!parent->IsSpm() && !(ignoreConflicts & FTE_CoverDisallow))
                        node->Errmsg("covered by", *parent, Names.begin());
                    CurNode++;
                    continue;
                } else if (parent->Enable && node->Enable) {
                    if (!(ignoreConflicts & FTE_CoverAllow))
                        node->Errmsg("covered by", *parent, Names.begin());

                    CurNode++;
                    continue;
                }


                if (node->Enable && node->UrlsDir)
                    flags = EN_URLS;
                else if (node->Enable)
                    flags = EN_URLS + EN_SUBDOMS;
                else if (!node->Enable && node->UrlsDir)
                    flags = EN_SUBDOMS;
                else // !node->Enable
                    flags = 0;

                CurNode++;
            } else if (subnode_type == SNT_CHILD) {
                // Child node
                ui32 child_path_id = (*CurNode)->NodePath[path_depth];
                const char *name = &Names[child_path_id];
                bool is_url = *name == '/' || *name == ':';
                int ret = MakeNode(is_url? parent4url : parent4dom, path_depth + 1);
                if (ret >= 0) {
                    childnames.push_back(child_path_id);
                    childoff.push_back(ret);
                }
            } else {
                assert(false);
            }
        }
        if (childnames.empty()) {
            if (flags == -1)
                return -1;
            else
                return flags + 4;
        }
        if (flags == -1)
            flags = parent->Enable? EN_URLS + EN_SUBDOMS : 0;
        int ret = (int)Tree.size();
        Tree.push_back(childnames.size() + (flags << 28));
        Tree.insert(Tree.end(), childnames.begin(), childnames.end());
        Tree.insert(Tree.end(), childoff.begin(), childoff.end());
        return ret;
    }

    void Make() {
        SortNames();
        SortNodes();
        ResolveConflicts();
        Tree.clear();
        Tree.resize(8, 0);
        CurNode = &*Nodes.begin(), EndNode = CurNode + Nodes.size();
        int ret = MakeNode(&Root, 0);
        Tree[0] = sizeof(ui32) * Tree.size() + Names.size() + Extensions.size() + 1;
        Tree[1] = Tree.size();
        Tree[2] = ret;
        Tree[3] = Names.size();
        Tree[4 + 0] = 0;
        Tree[4 + 1] = NEXT_MASK;
        Tree[4 + 2] = SAME_MASK;
        Tree[4 + 3] = SAME_MASK|NEXT_MASK;
    }

    void Flush(FILE *file) {
        fwrite(&Tree[0], sizeof(Tree[0]), Tree.size(), file);
        fwrite(&Names[0], 1, Names.size(), file);
        fwrite(Extensions.data(), 1, Extensions.size(), file);
        putc(0, file);
    }

    void Flush(TVector<char> &memData) {
        const size_t len = sizeof(Tree[0]) * Tree.size() + Names.size() + Extensions.size() + 1;
        TVector<char>(len).swap(memData);
        char *pos = memData.data();
        memcpy(pos, &Tree[0], sizeof(Tree[0]) * Tree.size());
        pos += sizeof(Tree[0]) * Tree.size();
        memcpy(pos, &Names[0], Names.size());
        pos += Names.size();
        memcpy(pos, Extensions.data(), Extensions.size());
        pos += Extensions.size();
        *pos = 0;
    }
};

// this doesn't work ->
void ReCompile() {

#define MARK      "-_!~*()'" // . excluded
#define RESERVED  ";:@&=+$," // / excluded
#define ALPHA     "a-z"
#define NUM       "0-9"
#define ALPHANUM  NUM ALPHA
#define HEX       NUM "a-f"
#define URIC      MARK RESERVED ALPHANUM
#define UCHAR     "([" URIC "]|(%[" HEX "][" HEX "]))"
#define DOM       "(([" ALPHANUM "]+(-+[" ALPHANUM "]+)*)|[?])"
#define DIR       "([?]|([.]*" UCHAR "([.]|" UCHAR ")*))" // either \? or UCHAR|\. not ..

#if 0
    TRegExSubst ReMask, ReHost, RePath, ReDirs;
    ReMask.Compile("^([^:/]+)(?:(:)(\\?|\\d+)?)?(/.*)?$");
    ReHost.Compile("^(" DOM "[.])*[" ALPHA "]+$");
    RePath.Compile("^(/" DIR ")*/?$");
    ReDirs.Compile("/[^/]*");
#endif
}

// regexp replacement
bool is_valid_path(const char *path) {
    static str_spn uri_chars(URIC, true);
    int dots = 0, chars = 1, qmarks = 0;
    for (const char *c = path; ; c++) {
        if (*c == '/' || !*c) {
            if (qmarks > 1 || (!qmarks ^ !!chars))
                return false;
            if (!*c || !c[1])
                return true;
            dots = 0, chars = 0, qmarks = 0;
        } else if (*c == '?')
            qmarks++;
        else if (*c == '.')
            dots++;
        else if (uri_chars.chars_table[(ui8)*c])
            chars++;
        else if (*c == '%' && isxdigit((ui8)c[1]) && isxdigit((ui8)c[2])) {
            chars++;
            c += 2;
        } else
            return false;
    }
    return true;
}

// regexp replacement
bool is_valid_host(const char *host) {
    static str_spn host_chars(ALPHANUM ".?-", true);
    static str_spn host_alpha(ALPHA, true);
    if (*host_chars.cbrk(host))
        return false;
    const char *rdot = strrchr(host, '.');
    if (!rdot) return true;
    //if (*host_alpha.cbrk(rdot + 1))
    //    return false;
    if (strchr(host, '-'))
        if (*host == '-' || strstr(host, ".-") || strstr(host, "-."))
            return false;
    return true;
}

static int bad_host = 0, bad_port = 0, bad_path = 0;

bool Node::ParseInput(char *url, bool disable, IFDescr *fd, int line_number, Filter &UrlFilter) {
    Enable = !disable;
    Lineno = line_number;
    FDescr = fd;

    if (strlen(url) >= sizeof(NodePath) / sizeof(*NodePath))
        return false;

    char *path = strchr(url, '/');
    if (path) *path = 0;
    char *colon = strchr(url, ':');
    if (colon) *colon = 0;

    //regmatch_t pmatch[NMATCHES];
    if (/*!ReHost.exec(host, pmatch, 0)*/ !is_valid_host(url)) {
//                errmess("bad host: \"%s\"", host);
        bad_host++;
        return false;
    }
    ui32 *path_ptr = NodePath;

    // put host name sections in reverse order
    char *hostfrg[32];
    int nhf = sf('.', hostfrg, url);
    for (--nhf; nhf >= 0; nhf--)
        *path_ptr++ = UrlFilter.AddName(hostfrg[nhf]);

    UrlsDir = false;

    // put port number, if necessary
    if (colon) {
        *colon = ':';
        if (!strcmp(colon, ":?") && colon[1] && colon[1] != '?') {
            char *end;
            int port = strtol(colon + 1, &end, 10);
            if (!port || *end) {
                //errmess("bad host: \"%s\"", url);
                bad_port++;
                return false;
            }
        }
        if (colon[1])
            *path_ptr++ = UrlFilter.AddName(colon);
    } else {
        if (path)
            *path_ptr++ = UrlFilter.AddName(colon = (char*)":80");
    }
    if (colon)
        UrlsDir = true;

    // put directories
    if (path) {
        *path = '/';
        //regmatch_t pmatch[NMATCHES];
        if (/*!RePath.exec(path, pmatch, 0)*/!is_valid_path(path)) {
            //errmess("bad path: \"%s\"", path);
            bad_path++;
            return false;
        }
        for (char *curdir = path; curdir; ) {
            char *nextdir = strchr(curdir + 1, '/');
            if (nextdir) *nextdir = 0;
            *path_ptr++ = UrlFilter.AddName(curdir);
            if (nextdir) *nextdir = '/';
            curdir = nextdir;
        }
        if (path_ptr[-1] == UrlFilter.SlashId)
            path_ptr--;
        else
            UrlsDir = false;
    }
    NodePathLen = path_ptr - NodePath;
    return true;
}

size_t Node::StorageSize() const {
    size_t s_size = (const char*)(NodePath + NodePathLen) - (const char*)this;
#ifdef _must_align8_
    s_size = (s_size + 7) & (size_t)~7;
#else
    s_size = (s_size + 3) & (size_t)~3;
#endif
    return s_size;
}

int MakeFilter(const char *outfile, TVector<char> *outmem, int verbose, int ignoreConflicts, bool enableRoot, const char *badextfile, char *in_file_list[]) {
    if (verbose == 1 && !isatty(fileno(stderr)))
        verbose = 0;

    Filter UrlFilter(ignoreConflicts, enableRoot);
    char line[8192];
    Node tmp_node;

    ReCompile();

    TVector<IFDescr> files;
    for (char **fname = in_file_list; *fname; fname++)
        files.push_back(*fname);
    sort(files.begin(), files.end());

    for (TVector<IFDescr>::iterator fn = files.begin(); fn != files.end(); ++fn) {
        TFILEPtr f(fn->Name, "r");
        int line_number = 0;
        bad_host = 0, bad_port = 0, bad_path = 0;
        while(fgets(line, sizeof(line), f)) {
            line_number++;
            strlwr(line);

            char *arg[32];
            int nf = sf(' ', arg, line);
            if (!nf || arg[0][0] == ';')
                continue;
            bool disable = nf > 1 && arg[1][0] != ';';

            if (!tmp_node.ParseInput(arg[0], disable, &*fn, line_number, UrlFilter))
                continue;

            UrlFilter.AddNode(tmp_node);
        }
        if (verbose && (bad_host || bad_port || bad_path))
            warnx("%s: bad_host = %i, bad_port = %i, bad_path = %i", fn->Name, bad_host, bad_port, bad_path);
    }

    if (verbose) warnx("Reading extensions");
    TString extensions;
    TFILEPtr f(badextfile, "r");
    while (fgets(line, sizeof(line), f)) {
        chomp(line);
        strlwr(line);
        for (char *c = line; *c; c++)
            if (!isalnum((ui8)*c))
                throw yexception() << "extensions.bad: error: bad extension: " << line;
        extensions += '.';
        extensions += line;
    }
    extensions += '.';
    UrlFilter.SetExtensions(extensions);
    f.close();

    if (verbose) warnx("Make");
    UrlFilter.Make();

    if (outfile) {
        if (verbose) warnx("Flush to %s", outfile);
        UrlFilter.Flush(TFILEPtr(outfile, "wb"));
    }
    if (outmem)
        UrlFilter.Flush(*outmem);

    return global_error;
}

int MakeFilter(const char *curdir, int verbose, int ignoreConflicts, bool enableRoot, const char *outfile, TVector<char> *outmem, const char *badextfile) {
    TString cd(curdir);
    SlashFolderLocal(cd);
    TString mask_flt = TString::Join(cd, "*.flt");
    TString mask_spm = TString::Join(cd, "*.spm");
    TString def_ext = TString::Join(cd, "extension.bad");

    glob_t inp;
    glob(mask_flt.data(), 0, nullptr, &inp);
    glob(mask_spm.data(), GLOB_APPEND, nullptr, &inp);
    if (!inp.gl_pathc)
        throw yexception() << "MakeFilter: No input files found (" << mask_flt.data() << " " << mask_spm.data() << ")";

    badextfile = badextfile? badextfile : def_ext.data();
    int rv = MakeFilter(outfile, outmem, verbose, ignoreConflicts, enableRoot, badextfile, inp.gl_pathv);

    globfree(&inp);
    return rv;
}
