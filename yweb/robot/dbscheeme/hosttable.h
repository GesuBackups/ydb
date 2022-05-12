#pragma once

#include "baserecords.h"
#include "mergecfg.h"

// util
#include <library/cpp/charset/ci_string.h>
#include <util/generic/hash.h>
#include <util/generic/hash_set.h>
#include <util/generic/yexception.h>
#include <util/digest/fnv.h>
#include <util/memory/pool.h>
#include <util/system/hi_lo.h>
#include <library/cpp/deprecated/autoarray/autoarray.h>

#include <library/cpp/microbdb/safeopen.h>
#include <kernel/hosts/owner/owner.h>

#include <algorithm>

template <class TInFile>
class THostTableImpl {
public:
    TVector<THostRec*> table;

    THostTableImpl()
        : table(1)
        , HostFile("host file", 0, -1)
    {}

    ~THostTableImpl() {
        Term();
    }

    void Init(const char *fname) {
        HostFile.Open(fname);
        ui32 maxhost = 0;
        const THostRec *host = HostFile.GotoLastPage();
        while (host) {
            maxhost = host->HostId;
            host = HostFile.Next();
        }
        table.reserve(maxhost + 1);
        host = HostFile.GotoPage(0);
        ui32 lastHostId = 0;

        while (host) {
            if (lastHostId >= host->HostId)
                ythrow yexception() << "bad host id order: " <<  lastHostId << " >= " <<  host->HostId;
            lastHostId = host->HostId;

            table.resize(lastHostId + 1);
            table[host->HostId] = (THostRec*) host;
            host = HostFile.Next();
        }

        if (HostFile.GetError())
            ythrow yexception() << "\"" <<  fname << "\" file error: " <<  HostFile.GetError();
    }

    ui32 GetTableSize() const {
        return (ui32)table.size();
    }

    void Flush(const char *fname) {
        TOutDatFile<THostRec> NewHostFile("flushhost", dbcfg::pg_host, dbcfg::fbufsize, 0);
        NewHostFile.Open(fname);
        for(ui32 id = 1; id < GetTableSize(); id++)
            if (table[id])
                NewHostFile.Push(table[id]);
        NewHostFile.Close();
    }

    void Term() {
        HostFile.Close();
    }

protected:
    TInFile HostFile;
};

typedef THostTableImpl<TInDatFile<THostRec> > THostTable;
typedef THostTableImpl<TMappedInDatFile<THostRec> > TMappedHostTable;

struct THostHashOp {
    size_t operator()(const THostRec* key) const {
        return Lo32(key->Crc);
    }
};

struct THostEqualOp {
    size_t operator()(const THostRec* a, const THostRec* b) const {
        return !stricmp(a->Name, b->Name);
    }
};

typedef THashSet<const THostRec*, THostHashOp, THostEqualOp> THostHash;

template <size_t Border = 50<<20>
class TTruncatedExpGrow: public TMemoryPool::IGrowPolicy {
public:
    size_t Next(size_t prev) const noexcept override {
        if (prev < Border)
            return TMemoryPool::TExpGrow::Instance()->Next(prev);
        else
            return TMemoryPool::TLinearGrow::Instance()->Next(prev);
    }

    static IGrowPolicy* Instance() {
        static TTruncatedExpGrow instance;
        return &instance;
    }
};

template <class TTable>
class THostHashTableImpl: public TTable {
private:
    static const size_t INITITAL_CHUNK_SIZE = 10 * sizeof(THostRec);
protected:
    THostHash HostHash;
    TMemoryPool Pool;
    ui32 CurHostId;
public:
    THostHashTableImpl()
        : HostHash()
        , Pool(INITITAL_CHUNK_SIZE, TTruncatedExpGrow<>::Instance())
        , CurHostId(1)
    {
    }

    ~THostHashTableImpl() {
        Term();
    }

    void Init(const char *fname) {
        TTable::Init(fname);
        init_hash();
        CurHostId = 1;
    }

    void Term() {
        {
            THostHash().swap(HostHash);
        }

        TTable::Term();
    }

    ui32 FindHostId()
    {
        ui32 size = this->GetTableSize();
        while ((CurHostId < size) && (table[CurHostId]))
            CurHostId++;
        return CurHostId;
    }

    const THostRec *find_or_insert(const THostRec *host, int *isnew) {
        THostHash::insert_ctx ins;
        THostHash::iterator i = HostHash.find(host, ins);
        *isnew = i == HostHash.end();

        if (*isnew) {
            THostRec *host_copy = static_cast<THostRec*>(Pool.Allocate(host->SizeOf()));
            memcpy(host_copy, host, host->SizeOf());
            host_copy->HostId = FindHostId(); // this->GetTableSize(); //lastHostId;
            if (host_copy->HostId < this->GetTableSize())
                table[host_copy->HostId] = host_copy;
            else
                table.push_back(host_copy);
            i = HostHash.insert_direct(host_copy, ins);
        }

        return *i;
    }

    const THostRec *find_or_insert(const TStringBuf& netloc, int *isnew) {
        THostRec Host;
        memset(&Host, 0, sizeof(THostRec));
        make_dummy_host(Host, netloc);
        return find_or_insert(&Host, isnew);
    }

    bool find(const THostRec *host, ui32 *hostid) const {
        THostHash::const_iterator it = HostHash.find(host);
        bool found = it != HostHash.end();
        if (found)
            *hostid = (*it)->HostId;
        return found;
    }
    bool find(const TStringBuf& hostname, ui32 *hostid) const {
        THostRec Host;
        make_dummy_host(Host, hostname);
        return find(&Host, hostid);
    }

    using TTable::table;

protected:
    void make_dummy_host(THostRec &host, const TStringBuf& netloc) const {
        dbscheeme::host_t lnetloc;
        const size_t len = netloc.strcpy(host.Name, sizeof(dbscheeme::host_t));
        strcpy(lnetloc, host.Name);
        strlwr(lnetloc);
        host.Crc = FnvHash<ui64>(lnetloc, len);
    }

    void init_hash() {
        THostHash().swap(HostHash);

        for(ui32 i = 0; i < this->GetTableSize(); i++) {
            if (table[i]) {
                std::pair<THostHash::iterator, bool> rv = HostHash.insert(table[i]);
                if (Y_UNLIKELY(!rv.second))
                    ythrow yexception() << "host id's " <<  i << ", " << (*rv.first)->HostId << " duplicated";
            }
        }
    }
};

typedef THostHashTableImpl<THostTable> THostHashTable;
typedef THostHashTableImpl <TMappedHostTable> TMappedHostHashTable;


static const ui32 TOV_HOST = 1;
static const ui32 NTOV_HOST = 2;
static const ui32 GOOD_HOST = 4;

class TCitindTable {
public:
    TCitindTable() : Citinds(nullptr), Flags(nullptr) {
    }

    ~TCitindTable() {
        Term();
    }

    void Init(const char *fname, THostHashTable *table) {
        TInDatFile<TCitindRec> CitindFile("indcit file", dbcfg::fbufsize, 0);
        const TCitindRec *pCitind;
        ui32 hostid = 0;

        Term();
        CitindFile.Open(fname);
        size_t asize = sizeof(ui32)*(table->GetTableSize());
        if (!(Citinds = (ui32*) malloc(asize)))
            ythrow yexception() << "can't malloc " <<  asize << " bytes";
        memset(Citinds, 0, asize);
        asize = sizeof(ui8)*(table->GetTableSize());
        if (!(Flags = (ui8*) malloc(asize)))
            ythrow yexception() << "can't malloc " <<  asize << " bytes";
        memset(Flags, 0, asize);
        while((pCitind = CitindFile.Next()))
            if (table->find(pCitind->Name, &hostid))
            {
                Citinds[hostid] = pCitind->Citind;
                Flags[hostid] = pCitind->Flags;
            }
    }

    void Term() {
        free(Citinds);
        Citinds = nullptr;
        free(Flags);
        Flags = nullptr;
    }

    ui32 GetCitind(ui32 hostid) {
        return Citinds[hostid];
    }

    bool GoodHost(ui32 hostid)
    {
        ui8 flags = Flags[hostid];
        ui32 citind = Citinds[hostid];
        if (flags & GOOD_HOST)
            return true;
        if (flags & NTOV_HOST)
            return citind >= 15;
        if (flags & TOV_HOST)
            return citind >= 60;
        return false;
    }

protected:
    ui32   *Citinds;
    ui8    *Flags;
};

class TGoodHostTable
{
public:
    TCitindTable* Table;
    THashSet<ui32> GoodHosts;

public:
    TGoodHostTable(TCitindTable* table)
    {
        Table = table;
    }
    bool GoodHost(ui32 hostid)
    {
        return Table->GoodHost(hostid) || (GoodHosts.find(hostid) != GoodHosts.end());
    }
};


class CHostDomain
{
public:
    THostRec* m_host;
    TStringBuf m_domain;
    bool m_good;

public:
    void SetHost(THostRec* host, const TOwnerCanonizer& oc)
    {
        m_good = false;
        m_host = host;
        m_domain = oc.GetHostOwner(host->Name);
        if (strchr(host->Name, ':')) { // bug compatibility mode!
            static HashSet Empty;
            m_domain = GetHostOwner(Empty, host->Name);
        }
    }
    template<class Table>
    void SetHost(THostRec* host, const TOwnerCanonizer& oc, Table* tab)
    {
        m_host = host;
        if ((m_good = tab && tab->GoodHost(host->HostId)))
            m_domain = host->Name;
        else
            m_domain = oc.GetHostOwner(host->Name);
    }
    bool operator<(const CHostDomain& hd) const
    {
        if (m_good != hd.m_good)
            return m_good;
        int res = m_domain.compare(hd.m_domain);
        if (res)
            return res < 0;
        return m_host->AntiqUrls < hd.m_host->AntiqUrls;
    }
};

class CHostDomainO
{
public:
    THostRec* m_host;
    TStringBuf m_domain;

public:
    void SetHost(THostRec* host, const TOwnerCanonizer& oc)
    {
        m_host = host;
        m_domain = GetOwnerPart(oc.GetHostOwner(host->Name));
        if (strchr(host->Name, ':')) { // bug compatibility mode!
            static HashSet Empty;
            m_domain = GetOwnerPart(GetHostOwner(Empty, host->Name));
        }
    }
    bool operator<(const CHostDomainO& hd) const
    {
        // can't use TCiString::compare for now because it uses CodePage
        int res = strnicmp(m_domain.data(), hd.m_domain.data(), std::min(m_domain.size(), hd.m_domain.size()));
        if (res)
            return res < 0;
        return m_domain.size() < hd.m_domain.size();
    }
    bool operator==(const CHostDomainO& hd) const
    {
        return (m_domain.size() == hd.m_domain.size()) && (strnicmp(m_domain.data(), hd.m_domain.data(), m_domain.size()) == 0);
    }
};


class THostDomainTable : public THostHashTable
{
public:
    THostRec** domTable;
    THostRec*** domHostTable;
    autoarray<TStringBuf> domains;
    ui32 domSize;
    ui32 domHostSize;
    TOwnerCanonizer oc;

public:
    THostDomainTable()
        : domTable(nullptr)
        , domHostTable(nullptr)
    {
    }
    ~THostDomainTable()
    {
        free(domTable);
        free(domHostTable);
    }
    void LoadDom2(const char* fname)
    {
        oc.LoadDom2(fname);
    }
    TStringBuf GetDomain(const TStringBuf &host)
    {
        return oc.GetHostOwner(host);
    }

    template<class Table>
    void CreateDomTableTempl(Table* ctab = NULL)
    {
        CHostDomain* hd;
        size_t tsize = sizeof(CHostDomain) * GetTableSize();
        if (!(hd = (CHostDomain*) malloc(tsize)))
            ythrow yexception() << "can't malloc " <<  tsize << " bytes";
        domSize = 0;
        for (ui32 i = 0; i < GetTableSize(); i++)
        {
            if (table[i])
            {
                hd[domSize++].SetHost(table[i], oc, ctab);
            }
        }
        MBDB_SORT_FUN(hd, hd + domSize);
        tsize = sizeof(THostRec*) * domSize;
        if (!(domTable = (THostRec**) malloc(tsize)))
            ythrow yexception() << "can't malloc " <<  tsize << " bytes";
        tsize = sizeof(THostRec**) * (domSize + 1);
        if (!(domHostTable = (THostRec***) malloc(tsize)))
            ythrow yexception() << "can't malloc " <<  tsize << " bytes";
        domains.resize(domSize);
        THostRec*** dh = domHostTable;
        TStringBuf *dom = domains.begin();
        for (ui32 i = 0; i < domSize; )
        {
            TStringBuf domain = hd[i].m_domain;
            *dh = domTable + i;
            dh++;
            *dom = hd[i].m_good ? TStringBuf() : domain;
            dom++;
            ui32 ii = i;
            for (; (i < domSize) && domain == hd[i].m_domain && (!hd[i].m_good || (ii == i)); i++)
            {
                domTable[i] = hd[i].m_host;
            }
        }
        *dh = domTable + domSize;
        domHostSize = ui32(dh - domHostTable);
        free(hd);
    }

    void CreateDomTable(TCitindTable* ctab = nullptr)
    {
        CreateDomTableTempl(ctab);
    }

    void CreateOwnerTable(ui32*& hdt)
    {
        CHostDomainO* hd;
        size_t tsize = sizeof(CHostDomainO) * GetTableSize();
        if (!(hd = (CHostDomainO*) malloc(tsize)))
            ythrow yexception() << "can't malloc " <<  tsize << " bytes";
        domSize = 0;
        for (ui32 i = 0; i < GetTableSize(); i++)
        {
            if (table[i])
            {
                hd[domSize++].SetHost(table[i], oc);
            }
        }
        MBDB_SORT_FUN(hd, hd + domSize);
        hdt = new ui32[GetTableSize()];
        memset(hdt, 0, GetTableSize() * sizeof(ui32));
        ui32 owner = 1;
        for (ui32 i = 0; i < domSize; )
        {
            ui32 ii = i;
            for (; (i < domSize) && (hd[i] == hd[ii]); i++)
            {
                hdt[hd[i].m_host->HostId] = owner;
                //printf("%i: %s %u: %.*s <- %i\n", i, hd[i].m_host->Name, hd[i].m_host->HostId, (ui32)+hd[i].m_domain, ~hd[i].m_domain, owner);
            }
            owner++;
        }
    }

    using THostHashTable::Init;

};


class TReverseHostTable {
public:
    TReverseHostTable() : mDirect2Reverse(nullptr), TableSize(0) {}

    ~TReverseHostTable() {
        Term();
    }

    void Term() {
        free(mDirect2Reverse);
        mDirect2Reverse = nullptr;
        TableSize = 0;
    }

    ui32 operator[](ui32 id) {
        if (id >= TableSize || !mDirect2Reverse[id])
            ythrow yexception() << "strange HostId:" <<  id;
        return mDirect2Reverse[id];
    }

    struct ReverseHostOrdering {
        ReverseHostOrdering(THostTable *table) : mTable(table) {}

        static void reverse(char *rev, const char *name) {
            const char *eptr = strrchr(name, ':'), *bptr = name;

            if (!eptr)
                eptr = strchr(name, 0);
            if (eptr > name && *(eptr-1) == '.')
                --eptr;
            size_t len = eptr - bptr;
            rev[len] = 0;

            while(1) {
                const char *dptr = strchr(bptr, '.');
                if (dptr && dptr < eptr) {
                    len -= dptr - bptr;
                    memcpy(rev + len, bptr, dptr - bptr);
                    bptr = dptr + 1;
                    rev[--len] = '.';
                } else {
                    memcpy(rev, bptr, eptr - bptr);
                    break;
                }
            }
            strlwr(rev);
        }

        bool operator()(ui32 id1, ui32 id2) const {
            dbscheeme::host_t h1, h2;
            reverse(h1, mTable->table[id1]->Name);
            reverse(h2, mTable->table[id2]->Name);
            return strcmp(h1, h2) < 0;
        }

        THostTable *mTable;
    };

    void Init(THostTable *table) {
        TableSize = table->GetTableSize();
        ui32 MaxRId = 0, Last = 1, *Reverse2Direct;
        mDirect2Reverse = (ui32*) malloc(sizeof(ui32) * TableSize);
        Reverse2Direct = (ui32*) malloc(sizeof(ui32) * TableSize);
        if (!mDirect2Reverse || !Reverse2Direct)
            ythrow yexception() << "Can't allocate " <<  sizeof(ui32) * TableSize << " bytes";
        for(ui32 i = 0; i < TableSize; i++)
            if (table->table[i])
                Reverse2Direct[MaxRId++] = table->table[i]->HostId;
        ReverseHostOrdering Order(table);
        MBDB_SORT_FUN(Reverse2Direct, Reverse2Direct + MaxRId, Order);
        memset(mDirect2Reverse, 0, sizeof(ui32) * TableSize);
        for(ui32 i = 0; i < MaxRId; i++) {
            if (i && Order(Reverse2Direct[i-1], Reverse2Direct[i]))
                ++Last;
            mDirect2Reverse[Reverse2Direct[i]] = Last;
        }
        free(Reverse2Direct);
    }

protected:
    ui32 *mDirect2Reverse, TableSize;
};
