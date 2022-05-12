#pragma once

#include <util/system/defaults.h>

namespace linkcfg {
    const char* const fname_preparatuiddocr = "/Berkanavt/pagerank/preparat/uiddocr.dat";
    const char* const fname_ownerspr        = "/Berkanavt/pagerank/preparat/ownerspr.dmp";

    // prev tables
    const char* const fname_inthost         = "prevdata/inthost.dat";
    const char* const fname_intdstfnv       = "prevdata/intdstfnv.dat";
    const char* const fname_intsrc          = "prevdata/intsrc.dat";
    const char* const fname_extsrc          = "prevdata/extsrc.dat";
    const char* const fname_extdst          = "prevdata/extdst.dat";

    const char* const fname_incdst          = "prevdata/incdst.dat";
    const char* const fname_incfnv          = "prevdata/incfnv.dat";
    const char* const fname_incsrc          = "prevdata/incsrc.dat";

    const char* const fname_text            = "prevdata/linktext.dat";
    const char* const fname_yandtext        = "yanddata/linktext.dat";
    const char* const fname_exturl          = "prevdata/linkexturl.dat";

    const char* const fname_linkexthost     = "prevdata/linkexthost.dat";

    const char* const fname_weight          = "prevdata/weight.dat";

    // new tables
    const char* const fname_newinthost      = "newdata/inthost.dat";
    const char* const fname_newintdstfnv    = "newdata/intdstfnv.dat";
    const char* const fname_newintsrc       = "newdata/intsrc.dat";
    const char* const fname_newextsrc       = "newdata/extsrc.dat";
    const char* const fname_newextdst       = "newdata/extdst.dat";

    const char* const fname_newincdst       = "newdata/incdst.dat";
    const char* const fname_newincfnv       = "newdata/incfnv.dat";
    const char* const fname_newincsrc       = "newdata/incsrc.dat";

    const char* const fname_newtext         = "newdata/linktext.dat";
    const char* const fname_newexturl       = "newdata/linkexturl.dat";

    const char* const fname_newlinkexthost  = "newdata/linkexthost.dat";

    // temp mirror internal links
    const char* const fname_mirrorinthost   = "linkwork/inthost.mirror.dat";
    const char* const fname_mirrorintdstfnv = "linkwork/intdstfnv.mirror.dat";
    const char* const fname_mirrorintsrc    = "linkwork/intsrc.mirror.dat";
    const char* const fname_aaa="aaa";

    // temp files
    const char* const fname_intdeltamask    = "linkwork/linkint%.3i.dat";
    const char* const fname_updlinkint      = "linkwork/updlinkint.dat";

    const char* const fname_extdeltamask    = "linkwork/linkext%.3i.dat";
    const char* const fname_updlinkext      = "linkwork/updlinkext.dat";

    const char* const fname_yabarlinkext_temp    = "mrdata/outgoing/yabarlinkext.dat";
    const char* const fname_watchloglinkext_temp = "mrdata/outgoing/watchloglinkext.dat";
    const char* const fname_yabarlinkext    = "config/yabarlinkext.dat";
    const char* const fname_watchloglinkext = "config/watchloglinkext.dat";

    const char* const fname_uidaction       = "linkwork/uidaction.dat";
    const char* const fname_docmainuid      = "linkwork/docmainuid.dat";

    const char* const fname_updexturl       = "linkwork/updexturl.dat";
    const char* const fname_exturlrefcnt    = "linkwork/exturlrefcnt.dat";
    const char* const fname_exturldeltamask = "linkwork/linkexturl%.3i.dat";
    const char* const fname_inturldeltamask = "linkwork/linkurl%.3i.dat";

    const char* const fname_inclinkf        = "linkwork/inclinkf.dat";
    const char* const fname_inclink         = "linkwork/inclink.dat";
    const char* const fname_syncforeign     = "linkwork/syncforeign.dat";
    const char* const fname_foreignurlmask  = "linkwork/%s.foreignurl.dat.gz";
    const char* const fname_foreignlinkmask = "linkwork/%s.foreignlink.dat.gz";

    const char* const fname_textrefcnt      = "linkwork/textrefcnt.dat";
    const char* const fname_exttextrefcnt   = "linkwork/exttextrefcnt.dat";
    const char* const fname_textdeltamask   = "linkwork/linktext%.3i.dat";

    const char* const fname_linkhostmask    = "linkwork/linkhost%.3i.dat";
    const char* const fname_linkexthostmask = "linkwork/linkexthost%.3i.dat";

    const char* const fname_tempextsrc      = "linkwork/extsrc.dat";
    const char* const fname_tempextdst      = "linkwork/extdst.dat";

    const char* const fname_temphops        = "linkwork/hops.dat";

    const char* const fname_yandinthost     = "yanddata/inthost.dat";
    const char* const fname_yandintdstfnv   = "yanddata/intdstfnv.dat";
    const char* const fname_yandintsrc      = "yanddata/intsrc.dat";

    const char* const fname_yandextsrc      = "yanddata/extsrc.dat";
    const char* const fname_yandextdst      = "yanddata/extdst.dat";
    const char* const fname_yandexturl      = "yanddata/linkexturl.dat";
    const char* const fname_yandlinkexthost = "yanddata/linkexthost.dat";

    const char* const fdir_prdump           = "dump/pr/";

    // page sizes
    const size_t pg_link     = 16u<<10;
    const size_t pg_large    = 1u<<20;

    // time to keep orphan links
    const time_t orphan_life = 60*60*24*30;
    // time to keep external redirects
    const time_t extredir_days_life = 90;
}
