#pragma once

#include <stdio.h>
#include <algorithm>
#include <util/system/defaults.h>
#include <util/generic/yexception.h>
#include <yweb/urlfilter/urlfilter.h>

#define HOP_MAX 0xFF
//max suitable hop for fast robot
#define FR_MAX_HOP 2

enum URL_CHECK_ERRORS {
    URL_CHECK_NOTABS = UrlFilterMax, // UrlFilterMax
    URL_CHECK_NOTHTTP,
    URL_CHECK_LONGHOST,
    URL_CHECK_MIRROR,
    URL_CHECK_DEEPDIR,
    URL_CHECK_DUPDIRS, // 21
    URL_CHECK_REGEXP,
    URL_CHECK_ROBOTS,
    URL_CHECK_OLDDELETED,
    URL_CHECK_PENALTY,
    URL_CHECK_POLICY,
    URL_CHECK_TOOOLD,
    URL_CHECK_GARBAGE,
    URL_CHECK_FOREIGN,
    URL_CHECK_MANUAL,
    URL_CHECK_CLEAN_PARAM, // 31
    URL_CHECK_BIGHOPS,
    URL_CHECK_POLICY_MIME_TYPE,
    URL_CHECK_EXT_REGEXP,
    URL_CHECK_HOPS,
    URL_CHECK_CRAWL,
    URL_CHECK_ALLOW,
    URL_CHECK_DNS_SPAM,
    URL_CHECK_FAST_FORCE_CRAWL,
    URL_CHECK_SELECT,
    URL_CHECK_SPAM,
    URL_CHECK_TOO_NEW,
    URL_CHECK_NODOC,
    URL_CHECK_NOLINKS,
    URL_CHECK_NOLINKS_PREREMOVING,
    URL_CHECK_SOFT_MIRRORS,
    URL_CHECK_WRONG_MULTI_LANG,
    URL_CHECK_BIGLEVEL,
    URL_CHECK_BAD_LANGREGION,
    URL_CHECK_MAX
};

// https://wiki.yandex-team.ru/robot/manual/hostscheme/
namespace HostStatus {
    enum HOST_FLAGS {
        // errors
        OK                  = 0,
        FINISHED            = 1,
        TERMINATED          = 2,
        HUGE_QUEUE          = 3,
        BUSY                = 4,
        CONNECT_LOST        = 5,
        DISALLOWED          = 6,
        SUBS_CONNECT_FAILED = 7,

        HUGE_ROBOTS         = 10,
        BAD_ROBOTS          = 11,
        DNS_ERROR           = 12,
        TEMP_DNS_ERROR      = 13,
        CONNECT_FAILED      = 14,
        RECLUSTER_FAKE      = 15,
        DNS_SPAM            = 16,
        SSL_CONNECTION_ERROR = 17,

        // spider specific internal host states
        UNINITIALIZED       = 20,
        CONNECT_LOST_INIT   = 21,
        BUSY_DONTRY         = 22,
        OK_INIT             = 23,

        HOST_FLAGS_MAX
    };

    enum HOST_BADNESS {
        UNKNOWN            = -1,
        GOOD               = 0,
        BAD                = 1,
        FROM_RECLUSTER     = 2,
        DISALLOWED_BY_FILTERS = 3
    };

    inline enum HOST_BADNESS HostBadness(enum HOST_FLAGS status) {
        switch (status) {
        case OK:
        case FINISHED:
        case HUGE_ROBOTS:
        case BAD_ROBOTS:
            return GOOD;
        case RECLUSTER_FAKE:
            return FROM_RECLUSTER;
        case DISALLOWED:
            return DISALLOWED_BY_FILTERS;
        case UNINITIALIZED:
        case HUGE_QUEUE:
            return UNKNOWN;
        default:
            return BAD;
        }
    }

    inline const char *HOST_FLAGS2c_str(enum HOST_FLAGS status) {
        switch (status) {
            case OK:                    return "OK";
            case FINISHED:              return "FINISHED";
            case TERMINATED:            return "TERMINATED";
            case HUGE_QUEUE:            return "HUGE_QUEUE";
            case BUSY:                  return "BUSY";
            case CONNECT_LOST:          return "CONNECT_LOST";
            case DISALLOWED:            return "DISALLOWED";
            case SUBS_CONNECT_FAILED:   return "SUBS_CONNECT_FAILED";
            case HUGE_ROBOTS:           return "HUGE_ROBOTS";
            case BAD_ROBOTS:            return "BAD_ROBOTS";
            case DNS_ERROR:             return "DNS_ERROR";
            case SSL_CONNECTION_ERROR:  return "SSL_CONNECTION_ERROR";
            case TEMP_DNS_ERROR:        return "TEMP_DNS_ERROR";
            case CONNECT_FAILED:        return "CONNECT_FAILED";
            case RECLUSTER_FAKE:        return "RECLUSTER_FAKE";
            case DNS_SPAM:              return "DNS_SPAM";
            case UNINITIALIZED:         return "UNINITIALIZED";
            case CONNECT_LOST_INIT:     return "CONNECT_LOST_INIT";
            case BUSY_DONTRY:           return "BUSY_DONTRY";
            case OK_INIT:               return "OK_INIT";
            default:                    return "UNK";
        }
    }
};

namespace UrlStatus {
    const ui8 NEW       = 0;
    const ui8 DELETED   = 2;
    const ui8 INDEXED   = 3;
    //const ui8 DUPLICATE = 4;
    const ui8 EMPTY     = 7;
    const ui8 SEMIDUP   = 8;
    const ui8 FAKE      = 9;
    const ui8 MAXSTATUS = FAKE;

    inline const char *UrlStatus2c_str(ui8 const status) {
        switch(status) {
        case NEW:       return "NEW";
        case DELETED:   return "DELETED";
        case INDEXED:   return "INDEXED";
        case EMPTY:     return "EMPTY";
        case SEMIDUP:   return "SEMIDUP";
        case FAKE:      return "FAKE";
        default: return "__unknown_url_status__";
        }
    }
}

namespace UrlFlags {
    // TUrlRec
    const ui32 LOST      =  0x0001;
    const ui32 NEWDOC    =  0x0002;

    // TAddDocRec
    const ui32 NEWSIG    =  0x0004;  // TEMP
    const ui32 FRAMESET  =  0x0008;
    // TNewUrlLogel
    const ui32 PRIORITY[] = { 0x0010, 0x0100 };

    const ui32 SRCSCRIPT =  0x0020;  // TEMP
    const ui32 REFRESH   =  0x1000;  // TEMP
    const ui32 REDIR     =  0x2000;  // TEMP
    const ui32 SAMEHOST  =  0x4000;  // TEMP

    // TNewUrlLogel, TUrlLogel, TIndUrlLogel
    const ui32 NOFOLLOW  =  0x0040;  // TEMP
    const ui32 NOINDEX   =  0x00010000; // TEMP
    const ui32 NOARCHIVE =  0x01000000;

    // TUrlRec, TNewUrlLogel
    const ui32 SUSPECT   =  0x0080;

    // TNewUrlLogel
    // force one time crawling of url (used for delurl and recrawl urls from webmaster)
    const ui32 FORCECRAWL    = 0x0200;
    // force replacing document on url
    const ui32 FORCEREPLACE  = 0x0400;

    // queuer
    const ui32 QUEUED    = 0x8000;

    // linkdb
    const ui32 HAS_INT_LINKS    = 0x00100000;
    const ui32 HAS_EXT_LINKS    = 0x00200000;
    const ui32 HAS_HIGHWEIGHT   = 0x00400000;
    const ui32 IS_LINK_SRC      = 0x0800;

    // filter
    const ui32 FILTERED   = 0x00020000;
    const ui32 THINSITE   = 0x00040000;
    const ui32 CRAWLCHECK = 0x00080000;

    // recluster
    const ui32 WAS_SEMIDUP = 0x00800000;

    // masks
    const ui32 LINKREDIR = REDIR + REFRESH;
    const ui32 LINKFLAGS = LINKREDIR + SRCSCRIPT + SAMEHOST;
    const ui32 TEMP = NEWSIG + LINKFLAGS + NOINDEX + NOFOLLOW + THINSITE + CRAWLCHECK;
    const ui32 NONINHERIT = FRAMESET + HAS_INT_LINKS + HAS_EXT_LINKS + HAS_HIGHWEIGHT;
    const ui32 TEMPQ = TEMP + PRIORITY[0] + PRIORITY[1];

    // flags to be cleaned in db & queue (use temporary for old flags removal)
    //const ui32 TOCLEAN = FORCEREPLACE + 0x0800;
    const ui32 TOCLEAN = FORCEREPLACE + FORCECRAWL;

    // mask and shift for extraction source id from flags field (fast robot)
    const ui32 SRCID_MASK    = 0xFF000000;
    const ui32 SRCID_SHIFT   = 24;

    // priority constants
    const ui32 PRIORITY_BITS = sizeof(PRIORITY) / sizeof(ui32);
    const ui32 MAX_PRIORITY = (1 << PRIORITY_BITS) - 1;

    //
    // special SID values
    //

    //invalid value
    const ui32 SRCID_NULL    = 0;
    //most weak value, for filtered urls
    const ui32 SRCID_NEUTRAL = 255;

    //
    // Source Id and Hops processing
    //

    inline ui32 GetSrcIdFromFlags(const ui32 flags) {
        return (flags & SRCID_MASK) >> SRCID_SHIFT;
    }

    inline void PutSrcIdToFlags(ui32 &flags, const ui32 srcId) {
        flags = (flags & ~SRCID_MASK) | (srcId << SRCID_SHIFT);
    }

    inline ui32 GetPriorityMask() {
        ui32 r = 0;
        for (ui32 i = 0; i < PRIORITY_BITS; ++i)
            r |= PRIORITY[i];
        return r;
    }
    inline ui32 GetPriority(ui32 flags) {
        ui32 r = 0;
        for (ui32 i = 0; i < PRIORITY_BITS; ++i)
            if (flags & PRIORITY[i])
                r |= (1 << i);
        return r;
    }
    inline void SetPriority(ui32 &flags, ui32 value) {
        if ((i32)value < 0)
            value = 0;
        if (value > MAX_PRIORITY)
            value = MAX_PRIORITY;
        for (ui32 i = 0; i < PRIORITY_BITS; ++i)
            if (value & (1 << i))
                flags |= PRIORITY[i];
            else
                flags &= ~PRIORITY[i];
    }
    inline void SetMaxPriority(ui32 &flags) {
        for (ui32 i = 0; i < PRIORITY_BITS; ++i)
            flags |= PRIORITY[i];
    }
    inline void ClearPriority(ui32 &flags) {
        for (ui32 i = 0; i < PRIORITY_BITS; ++i)
            flags &= ~PRIORITY[i];
    }
    inline void ClearTempQ(ui32 &flags) {
        flags &= ~TEMPQ;
    }

    inline void IncPriority(ui32 &flags) {
        SetPriority(flags, GetPriority(flags) + 1);
    }
    inline void DecPriority(ui32 &flags) {
        SetPriority(flags, GetPriority(flags) - 1);
    }
    inline void CopyPriorityIfGreater(ui32 &flagsA, ui32 flagsB) {
        ui32 p = GetPriority(flagsB);
        if (GetPriority(flagsA) < p)
            SetPriority(flagsA, p);
    }

    //
    template<typename TUrlType>
    inline bool CrawlCheck(TUrlType const& url) {
        return (url.Status == UrlStatus::NEW) || ((url.Flags & UrlFlags::CRAWLCHECK) == 0);
    }
    template<typename TUrlType>
    inline bool TooOldFP(TUrlType const& url, time_t const startTime) {
        return (url.Hops == 0) &&
            ((url.Status == UrlStatus::NEW) || (url.LastAccess < startTime - 7 * 24 * 60 * 60));
    }


    // Merges SIDs and Hops for urls
    struct TSHMerger {
    public:
        enum EHopsDeadness {
            // Hops greater than FR_MAX_HOP
            EHD_MAX_HOP_GR = 0,
            // Hops greater or equal FR_MAX_HOP
            EHD_MAX_HOP_GE = 1,
            __EHD_MAX__ = 1
        };

        enum EHopsQuality {
            // forbid merge if Hops greater than FR_MAX_HOP
            HQ_MAX_HOP_GR = EHD_MAX_HOP_GR,
            // forbid merge if Hops greater of equal FR_MAX_HOP
            HQ_MAX_HOP_GE = EHD_MAX_HOP_GE,
            // merging always
            HQ_ALWAYS = __EHD_MAX__ + 1,
        };

        /// \return true: hops2 not dead
        template<typename THopsType1, typename THopsType2>
        static inline bool MergeUrlsBySrcIdHops(ui32 &flags1, THopsType1 &hops1,
                                                ui32 const flags2, THopsType2 const hops2,
                                                EHopsQuality const hq) {
            bool const hopsNotDead = !IsDeadHops(hops2, static_cast<EHopsDeadness>(hq));

            if ((hq == HQ_ALWAYS) || hopsNotDead) {
                if (MergeSrcIdBySrcId(flags1, flags2))
                    MergeHops(hops1, hops2);
            }

            return hopsNotDead;
        }

        /// \return true: when hops not dead
        template<typename TUrlType1, typename TUrlType2>
        static inline bool MergeUrlsBySrcIdHops(TUrlType1 &u1, const TUrlType2 &u2,
                                                EHopsQuality const hq) {
            return MergeUrlsBySrcIdHops(u1.Flags, u1.Hops, u2.Flags, u2.Hops, hq);
        }

        template <typename TUrlType>
        static inline void DisableSidHops(TUrlType &url, ui32 const disableFlags = 0) {
            PutSrcIdToFlags(url.Flags, SRCID_NEUTRAL);
            url.Flags &= ~disableFlags;
            url.Hops = HOP_MAX;
        }

        template<typename THopsType>
        static inline bool IsDeadHops(THopsType const hops, EHopsDeadness const hd) {
            switch(hd) {
            case EHD_MAX_HOP_GR: return (hops >  FR_MAX_HOP);
            case EHD_MAX_HOP_GE: return (hops >= FR_MAX_HOP);
            default: return false;
            };
        }

        static inline bool IsDeadSID(ui32 const sid) {
            return (sid == SRCID_NULL) || (sid == SRCID_NEUTRAL);
        }

    private:
        /// \return true: Hops can be moved from url to url.
        static inline bool MergeSrcIdBySrcId(ui32 &flags1, const ui32 flags2) {
            ui32 srcId1 = GetSrcIdFromFlags(flags1);
            ui32 srcId2 = GetSrcIdFromFlags(flags2);

            if (srcId2 < srcId1)
                PutSrcIdToFlags(flags1, srcId2);

            return srcId2 <= srcId1;
        }

        template<typename THopsType1, typename THopsType2>
        static inline void MergeHops(THopsType1 &hops1, const THopsType2 hops2) {
            hops1 = std::min(hops1, static_cast<THopsType1>(hops2));
        }
    };

    //
    // Flags debug
    //

    class Print {
    private:
        char Buffer[30*30];
        ui32 Flags;
        size_t Terminate;
    public:
        Print(ui32 const flags, size_t const terminate = 0)
            : Flags(flags)
            , Terminate(terminate) {}

        const char* operator~() {
            size_t filled = 0;
            memset(&Buffer[0], 0, sizeof(Buffer));
            if (Flags & LOST) filled += sprintf(&Buffer[0], "LOST|");
            if (Flags & NEWDOC) filled += sprintf(Buffer+filled, "NEWDOC|");
            if (Flags & NEWSIG) filled += sprintf(Buffer+filled, "NEWSIG|");
            if (Flags & FRAMESET) filled += sprintf(Buffer+filled, "FRAMESET|");
            if (Flags & FORCECRAWL) filled += sprintf(Buffer+filled, "FORCECRAWL|");
            if (Flags & SRCSCRIPT) filled += sprintf(Buffer+filled, "SRCSCRIPT|");
            if (Flags & REFRESH) filled += sprintf(Buffer+filled, "REFRESH|");
            if (Flags & REDIR) filled += sprintf(Buffer+filled, "REDIR|");
            if (Flags & SAMEHOST) filled += sprintf(Buffer+filled, "SAMEHOST|");
            if (Flags & NOFOLLOW) filled += sprintf(Buffer+filled, "NOFOLLOW|");
            if (Flags & NOINDEX) filled += sprintf(Buffer+filled, "NOINDEX|");
            if (Flags & SUSPECT) filled += sprintf(Buffer+filled, "SUSPECT|");
            if (Flags & QUEUED) filled += sprintf(Buffer+filled, "QUEUED|");
            if (Flags & HAS_INT_LINKS) filled += sprintf(Buffer+filled, "HAS_INT_LINKS|");
            if (Flags & HAS_EXT_LINKS) filled += sprintf(Buffer+filled, "HAS_EXT_LINKS|");
            if (Flags & IS_LINK_SRC) filled += sprintf(Buffer+filled, "IS_LINK_SRC|");
            if (Flags & HAS_HIGHWEIGHT) filled += sprintf(Buffer+filled, "HAS_HIGHWEIGHT|");
            if (Flags & FILTERED) filled += sprintf(Buffer+filled, "FILTERED|");
            if (Flags & THINSITE) filled += sprintf(Buffer+filled, "THINSITE|");
            if (Flags & CRAWLCHECK) filled += sprintf(Buffer+filled, "CRAWLCHECK|");
            if (Flags & WAS_SEMIDUP) filled += sprintf(Buffer+filled, "WAS_SEMIDUP|");
            if ((ui32)UrlFlags::GetPriority(Flags) != 0)
                filled += sprintf(Buffer+filled, "PRIO_%u", (ui32)UrlFlags::GetPriority(Flags));
            if (Terminate) filled += sprintf(Buffer+filled, "\n");

            return &Buffer[0];
        }

        const char* Sid() {
            memset(&Buffer[0], 0, sizeof(Buffer));
            sprintf(&Buffer[0], "SID=%u", (ui32)GetSrcIdFromFlags(Flags));
            return &Buffer[0];
        }
    };

    inline void CheckSourceFlags(const char *funcName, const ui32 flags, const char* extraMsg, bool errorOrException) {
        ui32 sid = GetSrcIdFromFlags(flags);
        if (TSHMerger::IsDeadSID(sid)) {
            if (errorOrException)
                fprintf(stderr, "%s: invalid SID: %" PRIu32 "; %s\n", funcName, sid, extraMsg ? extraMsg : "");
            else
                ythrow yexception() <<  funcName << ": invalid SID: " <<  sid << "; " <<  (extraMsg ? extraMsg : "");
        }
    }

    template <typename THopsType>
    inline void CheckHops(const char *funcName, THopsType const hops, const char* extraMsg, bool const errorOrException) {
        if (TSHMerger::IsDeadHops(hops, TSHMerger::EHD_MAX_HOP_GR)) {
            if (errorOrException)
                fprintf(stderr, "%s: invalid Hops: %" PRIu32 "; %s\n", funcName, static_cast<ui32>(hops), extraMsg ? extraMsg : "");
            else
                ythrow yexception() <<  funcName << ": invalid Hops: " <<  static_cast<ui32>(hops) << "; " <<  (extraMsg ? extraMsg : "");
        }
    }
}

// Flags for robotrank dumping
namespace ExperimentInfo{
    const ui32 Print        = 0x10000000;
    const ui32 Relevant     = 0x20000000;
    const ui32 PrintStatus  = 0x40000000;

    const ui32 RelevanceMask= 0x000000FF;

    const ui32 Default  = Print | PrintStatus;
    const ui32 PrintRelevant = Print | Relevant | PrintStatus;
}

enum ENewUrlSourceId { //warning, don't add newurlsourceid with index 0
    NU_ADDURL        = 1,
    NU_DNSLOG        = 2,
    NU_CRAWLLIST     = 3,
    NU_TWITTER       = 4,
    NU_SITEMAP       = 5,
    NU_THINSITE      = 6,
    NU_YABAR         = 7,
    NU_MULTILANG     = 8,
    NU_MAINMIRR      = 9,
    NU_MOVEMIRR      = 10,
    NU_SIMILAR       = 11,
    NU_SURFCNONUS    = 12,
    NU_CLICKRU       = 13,
    NU_BIGMIR        = 14,
    NU_FAKEPREWALRUS = 15,
    NU_WATCHLOG      = 16,
    NU_FACEBOOK      = 17,
    NU_SHARE         = 18,
};

inline const char *ENewUrlSourceId2c_str(enum ENewUrlSourceId sourceId) {
    switch (sourceId) {
        case NU_ADDURL:         return "AddUrl";
        case NU_DNSLOG:         return "DNS_log";
        case NU_CRAWLLIST:      return "Crawllist";
        case NU_TWITTER:        return "Twitter";
        case NU_SITEMAP:        return "Sitemap";
        case NU_THINSITE:       return "ThinSite";
        case NU_YABAR:          return "YaBar";
        case NU_MULTILANG:      return "MultiLanguage";
        case NU_MAINMIRR:       return "Main_mirror";
        case NU_MOVEMIRR:       return "Move_mirror";
        case NU_SIMILAR:        return "Similar_group_logs";
        case NU_SURFCNONUS:     return "Surf_canyon_nonUS_logs";
        case NU_CLICKRU:        return "Click.ru_logs";
        case NU_BIGMIR:         return "Bigmir_logs";
        case NU_FAKEPREWALRUS:  return "Fakeprewalrus_extracturls";
        case NU_WATCHLOG:       return "Watch_log";
        case NU_FACEBOOK:       return "Facebook";
        case NU_SHARE:          return "Share";
        default:                return "Unk";
    }
}

inline const char *UrlCheckErrorToString(int code) {
    switch (code) {
        case UrlFilterEmpty:
            return "UrlFilterEmpty";
        case UrlFilterOpaque:
            return "UrlFilterOpaque";
        case UrlFilterBadFormat:
            return "UrlFilterBadFormat";
        case UrlFilterBadPath:
            return "UrlFilterBadPath";
        case UrlFilterTooLong:
            return "UrlFilterTooLong";
        case UrlFilterBadPort:
            return "UrlFilterBadPort";
        case UrlFilterBadAuth:
            return "UrlFilterBadAuth";
        case UrlFilterBadScheme:
            return "UrlFilterBadScheme";
        case UrlFilterSuffix:
            return "UrlFilterSuffix";
        case UrlFilterDomain:
            return "UrlFilterDomain";
        case UrlFilterExtDomain:
            return "UrlFilterExtDomain";
        case UrlFilterPort:
            return "UrlFilterPort";
        case URL_CHECK_NOTABS:
            return "URL_CHECK_NOTABS";
        case URL_CHECK_LONGHOST:
            return "URL_CHECK_LONGHOST";
        case URL_CHECK_GARBAGE:
            return "URL_CHECK_GARBAGE";
        case URL_CHECK_FOREIGN:
            return "URL_CHECK_FOREIGN";
        default:
             return nullptr;
    }
    return nullptr;
}
