#pragma once

#include <cstddef>
#include <util/system/defaults.h>

//#define USE_LOG_FORMAT_FOR_PARSEDDOCS

namespace dbcfg {

    // directories
    const char* const fname_temp        = "temp/";
    const char* const fname_queuedir    = "queue/";
    const char* const fname_copyquedir  = "inqueue/";
    const char* const fname_sortquedir  = "sortqueue/";
    const char* const fname_logdir      = "curlogs/";
    const char* const fname_outlogdir   = "outlogs/";
    const char* const fname_fastlogdir  = "curlogs/fastlogs/";
    const char* const fname_fastlogdirc = "fastdata/curlogsc/";
    const char* const fname_fast2main   = "fast2main/";
    const char* const fname_2others     = "2others";
    const char* const fname_extracteddir = "extracted/";
    const char* const fname_torqueue    = "torqueue/"; // queue to resolve
    const char* const fname_rqueue      = "rqueue/"; // dns resolved queue
    const char* const fname_todqueue    = "todqueue/"; // queue to distribute among spiders
    const char* const fname_dqueue      = "dqueue/"; // distributed among spiders queue

    const char* const fname_clsdir = "walrus/%03d/";
    const char* const fname_docurl = "walrus/url.dat";
    const char* const fname_segplan= "walrus/segplan.dat";
    const char* const fname_hostids= "walrus/hostids.dat";
    const char* const fname_hostidsx= "walrus/hostidsx.dat";
    const char* const fname_clsattrsportionsdir = "walrus/%03d/attrsportions/";
    const char* const fname_stamptag = "walrus/stamp.TAG";
    const char* const fname_walherf = "walrus/indexherf";

    // config
    const char* const fname_filter      = "config/filter.so";
    const char* const fname_rusfilter   = "config/rus.filter.so";
    const char* const fname_mirrors     = "config/mirrors.res";
    const char* const fname_mirrors_hash = "config/mirrors.hash";
    const char* const fname_mirrors_trie = "config/mirrors.trie";
    const char* const fname_redirs_filter = "config/redirsfilter.rfl";
    const char* const fname_dns_filter = "config/dnsfilter.rfl";
    const char* const fname_hr_hash1    = "config/hr.hash1";
    const char* const fname_areas       = "config/areas.lst";
    const char* const fname_domain2g    = "config/g2ld.list";
    const char* const fname_domain2b    = "config/b2ld.list";
    const char* const fname_domain2r    = "config/r2ld.list";
    const char* const fname_domain2d    = "config/d2ld.list";
    const char* const fname_hostconfig  = "config/host.cfg";
    const char* const fname_uploadconfig = "config/upload.cfg";
    const char* const fname_fakeuploadconfig = "config/fakeupload.cfg";
    const char* const fname_logreaderconfig = "config/logreader.cfg";
    const char* const fname_storingconfig  = "config/storing.cfg";
    const char* const fname_robotzonesconfig = "config/robotzones.pb.txt";
    const char* const fname_uploadrulesconfig = "config/uploadrules.pb.txt";
    const char* const fname_rus1hosts = "config/rus1_hosts.txt";
    const char* const fname_enghosts = "config/eng_hosts.txt";
    const char* const fname_citind      = "config/citind.dat";
    const char* const fname_stonefilter = "config/sg_ign.rst";
    const char* const fname_uidprx      = "config/uidpr.ext.dat";
    const char* const fname_selflt      = "config/selectionflt.dat";
    const char* const fname_selectionrankboostlist = "config/selectionrankboostlist.dat";
    const char* const fname_htparser    = "config/htparser.ini";
    const char* const fname_recogndict  = "config/dict.dict";
    const char* const fname_regionaltowns  = "config/towns";
    const char* const fname_faketitlesexceptions = "urlrules/faketitlesexceptions.txt";
    const char* const fname_webmastersitemapslog = "config/wmc.log";
    const char* const fname_polyglottrie = "config/poly.trie";
    const char* const fname_bannedurls = "config/bannedurls.list";
    const char* const fname_ukrop_attrs = "config/ukrop_attrs.pb.txt";

    const char* const fname_newsearchzonerankruler = "newdata/searchzonerankruler%02d.txt";
    const char* const fname_fakenewsearchzonerankruler = "newdata/fakesearchzonerankruler%02d.txt";
    const char* const fname_dumpsearchzonerankruler = "dump/searchzonerankruler%02d.txt";
    const char* const fname_selrankfilterthreshold = "dump/selrankfilterthreshold.txt";
    const char* const fname_newsearchzoneuploadstat = "newdata/uploadstat_%s.txt";

    // fastbot config
    const char* const fname_fastsourcesconfig     = "config/sources-fast.cfg";
    const char* const fname_ultrasourcesconfig    = "config/sources-ultra.cfg";
    const char* const fname_blogconverterconfig   = "config/blogconverter.cfg";
    const char* const fname_monitoringfile        = "stat/urlstat.new.txt";
    const char* const fname_yandarchtag           = "yandindex/%03d-archivetag";
    const char* const fname_linkdump              = "temp_yandex/linkdump.pro";
    const char* const fname_yandexbaselinkdump    = "dump/linkdump.pro";


    // for yandex merge
    const char* const fname_clsdochitscount  = "walrus/%03d/dochitscount.dat";
    const char* const fname_clsdocrobotzones  = "walrus/%03d/docrobotzones.dat";
    const char* const fname_clsdocsearchzones  = "walrus/%03d/docsearchzones.dat";
    const char* const fname_clsarcheaders  = "walrus/%03d/archeaders.dat";
    const char* const fname_clsdocarchivesizes = "walrus/%03d/docarchivesizes.dat";
    const char* const fname_clssrfactors = "walrus/%03d/srfactors.protobin";
    const char* const fname_clsddocarchivedir = "yandex/oldbd%03ddir";
    const char* const fname_clsddocarchivearc = "yandex/oldbd%03darc";
    const char* const fname_clsddocarchivetdr = "yandex/oldbd%03dtdr";
    const char* const fname_clsddocarchivetag = "yandex/oldbd%03dtag";
    const char* const fname_attrportion = "walrus/%03d/attr.portion";
    const char* const fname_totaldeldoc = "walrus/deldoc.dat";
    const char* const fname_tree        = "walrus/tree.dat";
    const char* const fname_clsattrs    = "walrus/%03d/attrs.dat";
    const char* const fname_clsurl      = "walrus/%03d/url.dat";
    const char* const fname_clsfakeurl  = "walrus/%03d/fakeurl.dat";
    const char* const fname_newclsurl   = "walrus/%03d/url.new.dat";
    const char* const fname_clsdoc      = "walrus/%03d/portion.dat";
    const char* const fname_clsadddoc   = "walrus/%03d/adddoc.dat";
    const char* const fname_clsdeldoc   = "walrus/%03d/deldoc.dat";
    const char* const fname_clsspamdoc  = "walrus/%03d/spamdoc.dat";
    const char* const fname_clsdupdoc  = "walrus/%03d/dupdoc.dat";
    const char* const fname_clssln      = "walrus/%03d/indexsln";
    const char* const fname_clsprn      = "walrus/%03d/indexprn";
    const char* const fname_clserf2     = "walrus/%03d/indexerf2";
    const char* const fname_clsjudgementsdoc = "walrus/%03d/judgementsdoc.dat";
    const char* const fname_clstestuploaddoc = "walrus/%03d/testuploaddocs.dat";
    const char* const fname_clsexcludefromsearchdoc = "walrus/%03d/excludefromsearchdoc.dat";
    const char* const fname_aggrnumrefsfrommp = "walrus/%03d/aggr_num_refs_from_mp.dat";
    const char* const fname_sourcerankcluster = "walrus/%03d/sourcerank.dat";
    const char* const fname_clsweeklyshows = "walrus/%03d/weeklyshows.dat";
    const char* const fname_clsextdatarank = "walrus/%03d/extdatarank.dat";
    const char* const fname_clsmultilanguagedoc = "walrus/%03d/multilanguagedoc.dat";
    const char* const fname_clszoneindexprn  = "walrus/%03d/searchzone_%d_indexprn";
    const char* const fname_fakeurlswhitelist = "walrus/fakeurlswhitelist.txt";
    const char* const fname_fakeurlswhitelist_regex = "config/fakeurlswhitelist_regex.txt";
    const char* const fname_mobile_beauty_url = "walrus/%03d/mobile_beauty_url.txt";
    const char* const fname_mobile_factors = "walrus/%03d/mobile_factors.txt";
    const char* const fname_snippets_dat = "walrus/%03d/snippets.dat";

    const char* const fname_sourcerank = "dump/sourcerank.dat";
    const char* const fname_sourcerank_cluster = "dump/sourcerank_cluster%03d.dat";
    const char* const fname_uploadranks = "dump/uploadranks.dat";
    const char* const fname_newindexsizes  = "newdata/indexsizes.dat";
    const char* const fname_prevsearchzonehitscount  = "prevdata/searchzonehitscount.dat";
    const char* const fname_newsearchzonehitscount  = "newdata/searchzonehitscount.dat";
    const char* const fname_newcompressioncoeffs  = "newdata/compressioncoeffs.dat";
    const char* const fname_newslnspamurl  = "newdata/slnspamurl.dat";
    const char* const fname_uiddoc      = "prevdata/uiddoc.dat";
    const char* const fname_uiddocmain  = "prevdata/uiddocmain.dat";
    const char* const fname_uiddocfull  = "prevdata/uiddocfull.dat";
    const char* const fname_uiddocdup   = "prevdata/uiddocdup.dat";
    const char* const fname_uiddoc0     = "prevdata/uiddocpure.dat";
    const char* const fname_uiddoc0full = "work/uiddocpurefull.dat";
    const char* const fname_robotbaselastaccess = "work/robotbaselastaccess.bin";
    const char* const fname_uiddocr     = "prevdata/uiddocr.dat";
    const char* const fname_uiddocsl    = "prevdata/uiddocsl.dat";
    const char* const fname_uiddocredir = "prevdata/uiddocredir.dat";
    const char* const fname_engurls     = "prevdata/engurls.dat";
    const char* const fname_langs       = "prevdata/langs";
    const char* const fname_urlsup      = "prevdata/urlsup.dat";
    const char* const fname_docnames    = "prevdata/docnames.dat";
    const char* const fname_extredirs   = "prevdata/extredirs.dat";
    const char* const fname_redirstop   = "config/redirstops.txt";
    const char* const fname_linksbydst = "work/linksbydst.dat";
    const char* const fname_ukrop_url_updates = "work/url_updates_for_urkop.bin";
    const char* const fname_robotbaseindexsizessum = "dump/robotbase-indexsizes-sum.txt";

    const char* const fname_clspagerank = "pagerank/PRs.%03d";
    const char* const fname_clspagerankua = "pagerank/PRsua.%03d";
    const char* const fname_clsindexprefix = "yandex/oldbd%03d";
    const char* const fname_clsrefxmap = "google/yandex/ref%03dxmap";
    const char* const fname_gluedlinkshosts = "dump/gluedlinkshosts";
    const char* const fname_prevgluedlinkshosts = "prevdata/gluedlinkshosts";
    const char* const fname_newgluedlinkshosts = "newdata/gluedlinkshosts";
    const char* const fname_langhostlinks = "dump/langhostlinks.dat";
    const char* const fname_newlanghostlinks = "newdata/langhostlinks.dat";

    // for yandex incremental merge
    const char* const fname_prevarchplan = "prevdata/archplan.dat";
    const char* const fname_prevyandplan  = "prevdata/yandplan.dat";
    const char* const fname_robotdocids  = "prevdata/robotdocids.txt";
    const char* const fname_newarchplan     = "newdata/archplan.dat";
    const char* const fname_newyandplan     = "newdata/yandplan.dat";

    const char* const fname_newnotspamarchplan     = "newdata/notspamarchplan.dat";
    const char* const fname_newnotspamyandplan     = "newdata/notspamyandplan.dat";

    const char* const fname_clsprescatterurl   = "prescatter/%03d/url.dat";
    const char* const fname_clsprescattersln   = "prescatter/%03d/indexsln";
    const char* const fname_clsprescatterprn   = "prescatter/%03d/indexprn";
    const char* const fname_clsprescatterzoneprn   = "prescatter/%03d/searchzone_%d_indexprn";
    const char* const fname_clsrawscatterplan  = "prescatter/%03d/rawscatterplan.dat";
    const char* const fname_clsprescatterspamdoc = "prescatter/%03d/spamdoc.dat";
    const char* const fname_prescatterplan     = "prescatter/prescatterplan.dat";
    const char* const fname_clsuploadrulecandidateslist  = "prescatter/%03d/uploadrule_%d_candidateslist.dat";
    const char* const fname_uploadrulecandidateslist  = "prescatter/uploadrule_%d_candidateslist.dat";
    const char* const fname_uploadruledocslist  = "prescatter/uploadrule_%d_docslist.dat";
    const char* const fname_clssearchzoneuploaddocs  = "prescatter/%03d/searchzone_%d_uploaddocs.dat";
    const char* const fname_clssearchzoneuploaddocso = "prescatter/%03d/searchzone_%d_uploaddocso.dat";
    const char* const fname_fakeclssearchzoneuploaddocs  = "prescatter/%03d/searchzone_%d_fakeuploaddocs.dat";
    const char* const fname_searchzoneuploaddocs  = "prescatter/searchzone_%d_uploaddocs.dat";
    const char* const fname_searchzoneuploaddocso = "prescatter/searchzone_%d_uploaddocso.dat";
    const char* const fname_fakesearchzoneuploaddocs  = "prescatter/searchzone_%d_fakeuploaddocs.dat";
    const char* const fname_newuploadgroupstat = "newdata/uploadstat.group";
    const char* const fname_newuploadsegmentsstat = "newdata/uploadstat.segments";
    const char* const fname_newuploadzonesstat = "newdata/uploadstat.zones";
    const char* const fname_newuploadsegmentszonesstat = "newdata/uploadstat.segmentszones";

    const char* const fname_arcdeltascatterplan  = "scatter/arcdeltascatterplan.dat";
    const char* const fname_yanddeltascatterplan  = "scatter/yanddeltascatterplan.dat";
    const char* const fname_yandfullscatterplan  = "scatter/yandfullscatterplan.dat";
    const char* const fname_fullscatterplan   = "scatter/fullscatterplan.dat";
    const char* const fname_notspamfullscatterplan = "scatter/notspamfullscatterplan.dat";
    const char* const fname_spamscatterplan   = "scatter/spamscatterplan.dat";

    const char* const fname_clsfromyandmap  = "walrus/%d/yandmap.dat";
    const char* const fname_clsdiversity  = "walrus/%03d/diversity.dat";
    const char* const fname_clsdiversity_ua  = "walrus/%03d/diversity_ua.dat";
    const char* const fname_clsdiversity_tr  = "walrus/%03d/diversity_tr.dat";
    const char* const fname_clsgeoshard  = "walrus/%03d/geoshard.dat";
    const char* const fname_clsgeoshardflt  = "walrus/%03d/geoshard_filtered.dat";
    const char* const fname_clsgeoshardtr  = "walrus/%03d/geoshardtr.dat";
    const char* const fname_clsdocscattershard = "walrus/%03d/doc_scatter_shard.dat";
    const char* const fname_preclsdocscattershard = "prescatter/%03d/doc_scatter_shard.dat";

    const char* const fname_clssearchurl = "search/%03d/url.dat";
    const char* const fname_clsarchmap   = "search/%03d/archmap.dat";
    const char* const fname_clsyandmap   = "search/%03d/yandmap.dat";
    const char* const fname_clsarchplan  = "search/%03d/archplan.dat";
    const char* const fname_clsyandplan  = "search/%03d/yandplan.dat";
    const char* const fname_clsindexarr  = "search/%03d/indexarr";
    const char* const fname_clsindexiarr = "search/%03d/indexiarr";
    const char* const fname_clsindexgrp  = "search/%03d/indexgrp";
    const char* const fname_clsindexprn  = "search/%03d/indexprn";


    // yandex merge database
    const char* const fname_yandhost     = "yanddata/host.dat";
    const char* const fname_yandchost    = "yanddata/chost.dat";
    const char* const fname_yandurl      = "yanddata/url.dat";
    const char* const fname_yandhandle   = "yanddata/handle.dat";
    const char* const fname_yandsigdoc   = "yanddata/sigdoc.dat";
    const char* const fname_yanddocgroup = "yanddata/docgroup.dat";
    const char* const fname_yanddocuid   = "yanddata/docuid.dat";
    const char* const fname_yandportion  = "yanddata/portion.dat";
    const char* const fname_yandhostlog  = "yanddata/hostlog.dat";
    const char* const fname_yandrobots   = "yanddata/robots.dat";
    const char* const fname_yandredirs   = "yanddata/redirs.dat";
    const char* const fname_yanduiddocmain  = "yanddata/uiddocmain.dat";
    const char* const fname_yanduiddocfull  = "yanddata/uiddocfull.dat";
    const char* const fname_yanduiddoc0  = "yanddata/uiddocpure.dat";
    const char* const fname_yanduiddocr  = "yanddata/uiddocr.dat";
    const char* const fname_yanduiddocsl = "yanddata/uiddocsl.dat";
    const char* const fname_yanduiddocredir = "yanddata/uiddocredir.dat";
    const char* const fname_yanduiddocdup = "yanddata/uiddocdup.dat";
    const char* const fname_yandengurls  = "yanddata/engurls.dat";
    const char* const fname_yandlangs    =  "yanddata/langs";
    const char* const fname_yanddocnames = "yanddata/docnames.dat";
    const char* const fname_yanddocgen   = "yanddata/docgen.dat";
    const char* const fname_yandhostinfo = "yanddata/hostinfo.dat";
    const char* const fname_yanduidimagessnip = "yanddata/uidimagessnip.dat";

    // previous database
    const char* const fname_host         = "prevdata/host.dat";
    const char* const fname_hosth        = "prevdata/hosth.dat";
    const char* const fname_chost        = "prevdata/chost.dat";
    const char* const fname_deletedhosts = "prevdata/deletedhost.dat";
    const char* const fname_url          = "prevdata/url.dat";
    const char* const fname_handle       = "prevdata/handle.dat";
    const char* const fname_sigdoc       = "prevdata/sigdoc.dat";
    const char* const fname_docgroup     = "prevdata/docgroup.dat";
    const char* const fname_docuid       = "prevdata/docuid.dat";
    const char* const fname_portion      = "prevdata/portion.dat";
    const char* const fname_prevhostlog  = "prevdata/hostlog.dat";
    const char* const fname_prevrobots   = "prevdata/robots.dat";
    const char* const fname_olddd        = "prevdata/dd";
    const char* const fname_redirs       = "prevdata/redirs.dat";
    const char* const fname_redirbonus   = "prevdata/redirbonus.dat";
    const char* const fname_groupurls    = "prevdata/groupurls.dat";
    const char* const fname_cleanparammainurls = "prevdata/cleanparammainurls.dat";
    const char* const fname_cleanparamtoreject = "prevdata/cleanparamtoreject.dat";
    const char* const fname_semidupurl   = "prevdata/semidupurl.dat";
    const char* const fname_queuedurls   = "prevdata/queuedurls.dat";
    const char* const fname_hostqueued   = "prevdata/hostqueued.dat";
    const char* const fname_isnewqueued  = "prevdata/isnewqueued.dat";
    const char* const fname_hostpolicy   = "prevdata/hostpolicy.dat";
    const char* const fname_hostvisited  = "prevdata/hostvisited.dat";
    const char* const fname_prevdeldoc   = "prevdata/deldocfast.dat";
    const char* const fname_paramcount = "prevdata/paramcount.dat";
    const char* const fname_urlpath = "prevdata/urlpath.dat";
    const char* const fname_cleanparam    = "prevdata/cleanparam.dat";
    const char* const fname_cleanparamflt    = "prevdata/cleanparamflt.dat";
    const char* const fname_queuednewurls = "prevdata/queuednewurls.dat";
    const char* const fname_orphanage    = "prevdata/orphanage.dat";
    const char* const fname_intsources   = "prevdata/intsources.dat";
    const char* const fname_linkexthost  = "prevdata/linkexthost.dat";
    const char* const fname_prev_mirrors = "prevdata/mirrors.res";
    const char* const fname_prev_mirrors_hash  = "prevdata/mirrors.hash";
    const char* const fname_uidrr        = "prevdata/uidrr.dat";
    const char* const fname_recalcday    = "prevdata/recalcday.txt";
    const char* const fname_ipstat       = "prevdata/ipstat.dat";
    const char* const fname_resettledst  = "prevdata/resettledst.dat";
    const char* const fname_docgen       = "prevdata/docgen.dat";
    const char* const fname_gen          = "prevdata/generation.txt";
    const char* const fname_csuids       = "prevdata/csuids.dat";
    const char* const fname_notfound     = "prevdata/notfoundrank.dat";
    const char* const fname_prevdoclogsourcecount = "prevdata/doclogsourcecount.dat";
    const char* const fname_prevhostpenaltydelta = "prevdata/hostpenaltydelta.dat";
    const char* const fname_prevuidimagessnip = "prevdata/uidimagessnip.dat";
    const char* const fname_delhistory   = "prevdata/delhistory.dat";
    const char* const fname_delhistoryx  = "prevdata/delhistoryx.dat";

    // new database
    const char* const fname_newhost      = "newdata/host.dat";
    const char* const fname_newhosth     = "newdata/hosth.dat";
    const char* const fname_newchost     = "newdata/chost.dat";
    const char* const fname_newdeletedhosts = "newdata/deletedhost.dat";
    const char* const fname_newurl       = "newdata/url.dat";
    const char* const fname_newsigdoc    = "newdata/sigdoc.dat";
    const char* const fname_newdocgroup  = "newdata/docgroup.dat";
    const char* const fname_newdocuid    = "newdata/docuid.dat";
    const char* const fname_newportion   = "newdata/portion.dat";
    const char* const fname_newhostlog   = "newdata/hostlog.dat";
    const char* const fname_newrobots    = "newdata/robots.dat";
    const char* const fname_newhostinfo  = "newdata/hostinfo.dat";
    const char* const fname_newdd        = "newdata/dd";
    const char* const fname_newredirs    = "newdata/redirs.dat";
    const char* const fname_newredirbonus= "newdata/redirbonus.dat";
    const char* const fname_newgroupurls = "newdata/groupurls.dat";
    const char* const fname_newcleanparammainurls = "newdata/cleanparammainurls.dat";
    const char* const fname_newcleanparamtoreject = "newdata/cleanparamtoreject.dat";
    const char* const fname_newqueuedurls= "newdata/queuedurls.dat";
    const char* const fname_newhostqueued= "newdata/hostqueued.dat";
    const char* const fname_newisnewqueued= "newdata/isnewqueued.dat";
    const char* const fname_newhostpolicy= "newdata/hostpolicy.dat";
    const char* const fname_newhostvisited="newdata/hostvisited.dat";
    const char* const fname_newdeldoc    = "newdata/deldocfast.dat";
    const char* const fname_newextsrc    = "newdata/extsrc.dat";
    const char* const fname_newextdst    = "newdata/extdst.dat";
    const char* const fname_newlinkexthost = "newdata/linkexthost.dat";
    const char* const fname_newlinkexturl  = "newdata/linkexturl.dat";
    const char* const fname_newweight      = "newdata/weight.dat";
    const char* const fname_newparamcount = "newdata/paramcount.dat";
    const char* const fname_newurlpath    = "newdata/urlpath.dat";
    const char* const fname_newcleanparam    = "newdata/cleanparam.dat";
    const char* const fname_newparamstatsrfl    = "work/paramstats.rfl";
    const char* const fname_newcleanparamflt    = "newdata/cleanparamflt.dat";
    const char* const fname_newdomainswww      = "newdata/wwwdomains.dat";
    const char* const fname_newqueuednewurls = "newdata/queuednewurls.dat";
    const char* const fname_newuiddoc      = "newdata/uiddoc.dat";
    const char* const fname_newuiddocmain  = "newdata/uiddocmain.dat";
    const char* const fname_newuiddocfull  = "newdata/uiddocfull.dat";
    const char* const fname_newuiddocdup  = "newdata/uiddocdup.dat";
    const char* const fname_neworphanage = "newdata/orphanage.dat";
    const char* const fname_newintsources   = "newdata/intsources.dat";
    const char* const fname_newuidrr        = "newdata/uidrr.dat";
    const char* const fname_newrecalcday = "newdata/recalcday.txt";
    const char* const fname_newexcludedfromsearchhostlist = "newdata/excludedfromsearchhostlist.dat";
    const char* const fname_newipstat       = "newdata/ipstat.dat";
    const char* const fname_newmainmirrors = "newdata/mainmirrors.txt";
    const char* const fname_newnotmainmirrors = "newdata/notmainmirrors.dat";
    const char* const fname_newtruemirrors = "newdata/truemirrors.txt";
    const char* const fname_newresettledst  = "newdata/resettledst.dat";
    const char* const fname_newhandle       = "newdata/handle.dat";
    const char* const fname_newdocgen       = "newdata/docgen.dat";
    const char* const fname_newgen          = "newdata/generation.txt";
    const char* const fname_newhostsigdoc   = "newdata/hostsigdoc.dat";
    const char* const fname_newnotfound  = "newdata/notfoundrank.dat";
    const char* const fname_newdoclogsourcecount = "newdata/doclogsourcecount.dat";
    const char* const fname_newhostpenaltydelta = "newdata/hostpenaltydelta.dat";
    const char* const fname_newuidimagessnip = "newdata/uidimagessnip.dat";
    const char* const fname_newdelhistory   = "newdata/delhistory.dat";
    const char* const fname_newdelhistoryx  = "newdata/delhistoryx.dat";
    const char* const fname_newmimestat  = "newdata/mimestat.dat";

    // logreader files
    const char* const fname_parseddocs   = "work/parseddocs.log";
    const char* const fname_ranges       = "work/ranges.log";
    const char* const fname_sitemaps     = "work/sitemaps.log";
    const char* const fname_lparseddocs  = "work/lparseddocs.dat";
    const char* const fname_hostlog      = "work/hostlog.dat";
    const char* const fname_robots       = "work/robots.dat";
    const char* const fname_actualrobots = "work/arobots.dat";
    const char* const fname_indcrc       = "work/indcrc.dat";
    const char* const fname_addurl       = "work/addurl.dat";
    const char* const fname_srcaddurl    = "work/srcaddurl.dat";
    const char* const fname_updurl       = "work/updurl.dat";
    const char* const fname_loghost      = "work/loghost.dat";
    const char* const fname_loghosthash  = "work/loghost.hash";
    const char* const fname_loghosthashdirect = "work/loghost.hash.direct";
    const char* const fname_logextredirhost = "work/redirexthost.dat";
    const char* const fname_logextredirurl  = "work/redirexturl.dat";
    const char* const fname_logextredirlink = "work/redirextlink.dat";
    const char* const fname_logdocgroup  = "work/logdocgroup.dat";
    const char* const fname_delurl       = "work/deluid.dat";
    const char* const fname_logweights   = "work/logweight.dat";
    const char* const fname_updsitemapurl= "work/updsitemapurl.dat";
    const char* const fname_prevuiddoc= "work/prevuiddoc.dat";
    const char* const fname_uidmap = "work/uidmap";
    const char* const fname_uidmaphosts = "work/uidmaphosts";
    const char* const fname_kiwidocdata = "work/kiwidocdata.bin";
    const char* const fname_blogpostdata = "work/blogpostdata.bin";
    const char* const fname_urlsfromkiwi = "work/urlsfromkiwi.txt";

    // previous database
    const char* const fname_hostfast     = "fastprevdata/host.dat";
    const char* const fname_urlfast      = "fastprevdata/url.dat";

    // fast robot files
    const char* const fname_urlcrawldatatrans = "work/urlcrawldatatrans.dat";
    const char* const fname_urlcrawldata      = "prevdata/urlcrawldata.dat";
    const char* const fname_newurlcrawldata   = "newdata/urlcrawldata.dat";
    const char* const fname_yandurlcrawldata  = "yanddata/urlcrawldata.dat";
    const char* const fname_extracted_mask    = "%s.extracted.%" PRISZT ".txt";
    const char* const fname_archdaterdate     = "work/archdaterdate.dat";

    // merge files
    // chkurl
    const char* const fname_workgroupurls = "work/groupurls.dat";
    const char* const fname_addhost     = "work/addhost.dat";
    const char* const fname_chkurl      = "work/chkurl.dat";
    const char* const fname_addmirrdoc  = "archlogs/addmirrdoc.%u";
    const char* const fname_foreignurl  = "work/foreignurl.dat";
    const char* const fname_dd          = "work/dd";
    const char* const fname_links       = "work/links.dat";
    const char* const fname_linksdelay  = "prevdata/linksdelay.dat";
    const char* const fname_newlinksdelay = "newdata/linksdelay.dat";
    const char* const fname_ukropdelay  = "prevdata/ukropdelay.dat";
    const char* const fname_newukropdelay  = "newdata/ukropdelay.dat";
    const char* const fname_urlfromsitemap = "work/urlfromsitemap.dat";
    const char* const fname_urlfromsitemapinfo = "work/urlfromsitemapinfo.dat";
    const char* const fname_exturlfromsitemap = "work/exturlfromsitemap.dat";
    const char* const fname_exturlfromsitemapinfo = "work/exturlfromsitemapinfo.dat";

    // ddmerge
    const char* const fname_prevddd         = "work/prevdd";
    const char* const fname_newddd          = "work/newdd";
    const char* const fname_mordasmainurls  = "prevdata/mordasmainurls.dat";
    const char* const fname_newmordasmainurls  = "newdata/mordasmainurls.dat";

    // updhost
    const char* const fname_updhost         = "work/updhost.dat";

    // movesemidups
    const char* const fname_semidups        = "work/semidups.dat";

    // adddoc
    const char* const fname_indurl          = "work/indurl.dat";
    const char* const fname_addhandle       = "work/addhandle.dat";
    const char* const fname_adddocgroup     = "work/adddocgroup.dat";
    const char* const fname_updsigdoc       = "work/updsigdoc.dat";
    const char* const fname_docnum          = "work/docnum.dat";
    const char* const fname_semidupdoc      = "work/semidupdoc.dat";
    const char* const fname_bannedsemidups  = "work/bannedsemidups.dat";

    // rss
    const char* const fname_clsrsslinks     = "work/rsslinks.%03d.dat";
    const char* const fname_clsrsslinksdate = "work/rsslinksdate.%03d.dat";

    // updurl
    const char* const fname_allowedurl      = "work/allowedurl.dat";
    const char* const fname_filtercache     = "work/filtercache.dat";
    const char* const fname_filtercachepart = "work/filtercache.part.%zu.dat";
    const char* const fname_filtercachethreadsnumber = "work/filtercache.threadsnumber.txt";
    const char* const fname_filteredurl     = "work/filteredurls.dat";
    const char* const fname_refdocuid       = "work/refdocuid.dat";
    const char* const fname_addportion      = "work/addportion.dat";
    const char* const fname_queue           = "queue/fullqueue.dat";
    const char* const fname_pqueue          = "queue/fullpqueue.dat";
    const char* const fname_queues          = "queue/%d.queue.dat";
    const char* const fname_pqueues         = "queue/%d.pqueue.dat";
    const char* const fname_lastqueueid     = "queue/lastqueueid.txt";
    const char* const fname_lastqueuetime   = "queue/lastqueuetime.txt";
    const char* const fname_hostplan        = "work/hostplan.dat";
    const char* const fname_anthost         = "work/anthost.dat";
    const char* const fname_updurl_start_time = "work/updurl_start_time.bin";
    const char* const fname_foreign_trigrams = "config/foreign_trigrams";
    const char* const fname_ngrams_clusters = "config/ngrams_clusters.txt";
    const char* const fname_ngrams_model    = "config/ngrams_model.info";
    const char* const fname_hostfactors     = "config/hostfactors.dat"; //TODO find why here
    const char* const fname_tophosturls     = "work/tophosturls.dat";
    const char* const fname_indexherf       = "config/indexherf";
    const char* const fname_urlsscdataconfig= "config/urlsscdata.dat";
    const char* const fname_preremoving     = "newdata/preremoving.dat";
    const char* const fname_structureddata  = "config/structureddata.dat";
    const char* const fname_foreignhosts    = "work/foreignhosts.dat";
    const char* const fname_uidsource       = "work/uidsource.dat";

    const char* const fname_prequeue_common         = "work/prequeue_common.dat";

    // fakeprewalrus files
    const char* const fname_clsmovedmirrordocsinfo = "work/movedmirrordocsinfo.%03d.dat";
    const char* const fname_clsmovedmirrordocstag = "work/movedmirrordocs.%03d.tag";
    const char* const fname_clsmovedmirrordocslog = "dump/movedmirrordocs.log";

    // Getting CrawlRank from incoming logels, using spider config:
    // Location of the spider cfg file
    const char* const fname_spidersourceconfig = "config/squota.spider";
    // Source name (main production cluster to get crawlranks from)
    const char* const spidersourceforcrawlrank = "orange";

    const char* const fname_ukroppoints     = "ukroppoints.txt";
    const char* const fname_ukropstreamconfig     = "streamconfig.pb.txt";

    // mirrors
    const char* const fname_localmirrors = "work/localmirrors.txt";
    const char* const fname_localmirrors_full = "work/localmirrorsfull.txt";

    // deldoc
    const char* const fname_deldoc          = "work/deldoc.dat";
    const char* const fname_delreason       = "work/delreason.dat";
    const char* const fname_semidupswomain  = "work/semidupswomain.dat";
    const char* const fname_urlschangesstat = "work/urlschangesstat.txt";

    // reclusterurl
    const char* const fname_priorityboost   = "work/priorityboost.dat";
    const char* const fname_hitrobotdata    = "mrdata/hitrobotdata.txt";

    // clusterhandle
    const char* const fname_freehandle      = "work/freehandle.dat";

    // hoststat
    const char* const fname_stathost        = "work/stathost.dat";
    const char* const fname_fhost           = "work/fasthost.dat";

    // hubs
    const char* const fname_hubs            = "work/hubs.dat";
    const char* const fname_dump_hubs       = "dump/hubs.dat";
    const char* const fname_indhubsstat     = "work/hubsstatind.dat";
    const char* const fname_hubsstat        = "prevdata/hubsstat.dat";
    const char* const fname_newhubsstat     = "newdata/hubsstat.dat";
    const char* const fname_spyloghubs      = "config/spyloghubs.dat";

    // trans_close
    const char* const fname_redirurls       = "prevdata/redirurls.dat";
    const char* const fname_transredirs     = "work/transredirs.dat";
    const char* const fname_prev_whp        = "prevdata/whp";
    const char* const fname_prev_fdocmap    = "prevdata/fdocmap";
    const char* const fname_prev_fakedocmap = "prevdata/fakedocmap";
    const char* const fname_new_whp         = "newdata/whp";
    const char* const fname_new_fdocmap     = "newdata/fdocmap";
    const char* const fname_new_fakedocmap  = "newdata/fakedocmap";

    // temp dd file
    const char* const fname_tmpdd       = "temp/dd";
    const char* const fname_tmpind      = "temp/ind";

    // garbage rules
    const char* const fname_pars        = "prevdata/pars";
    const char* const fname_vpars       = "prevdata/vpars";
    const char* const fname_gpars       = "config/vpars";
    const char* const fname_nouid       = "temp/no";
    const char* const fname_ouid        = "temp/ouid";
    const char* const fname_tmpurl      = "temp/tmpurl";

    const char* const fname_fmirr       = "work/fmirr.%s.log";
    const char* const fname_inmirrdir   = "mirrorsin/";
    const char* const fname_inmirr      = "mirrorsin/fmirr.%s.log";
    const char* const fname_outmirrprefix = "mirrorsout/%s.tmirr.log";
    const char* const fname_tmirr       = "work/%s.tmirr.dat";
    const char* const fname_mirrlog     = "temp/mirrlogs/mirr.%i";
    const char* const fname_localmirrurl = "work/localmirrurl.log";

    // queuer
    const char* const fname_dirdict     = "config/dirdict.dat";
    const char* const fname_hostdirplan = "work/hostdirplan.dat";
    const char* const fname_qbhosts     = "config/qb_hosts.txt";
    const char* const fname_parsw       = "config/parsw";
    const char* const fname_casehosts   = "config/casehosts.txt";

    // popularity calculator
    const char* const fname_fltic_over0    = "config/fltic_over0.txt";
    const char* const fname_2ld_list       = "config/2ld.list";
    const char* const fname_tmprevertlinks = "temp/revertlinks.dat";
    const char* const fdir_portions        = "portions/";

    // robots.txt deny for Yandex
    const char* const fname_robotsdenyresult = "work/robotsdeny";
    const char* const fname_robotsdenyrobotsresult = "work/robotstxt.dat";

    // seo link commerciality calculator
    const char* const fname_seodir          = "config/seolnk/";

    // semidups
    const char* const fname_semidupidx          = "newindex/%03d-index";
    const char* const fname_semiduparc          = "newindex/%03d-archive";
    const char* const fname_prevdocsig          = "prevdata/docsig.dat";
    const char* const fname_newdocsig           = "newdata/docsig.dat";
    const char* const fname_cldocsig            = "work/docsig.%03d.dat";
    const char* const fname_docsigchk           = "work/docsigchk.dat";
    const char* const fname_words               = "config/words";
    const char* const fname_adddoc              = "work/adddoc.%03d.dat";
    const char* const fname_semidupsfilter      = "config/semidups.rfl";
    const char* const fname_filteredsemidups    = "work/filteredsemidups.dat";
    const char* const fname_semidupuid          = "work/semidupuid.dat";
    const char* const fname_misignature         = "work/midocsig.dat";
    const char* const fname_docidchanges        = "work/docidchanges.dat";

    // SimHash
    const char* const fname_dump_simhash              = "dump/docsimhash.kiwi.dat";
    //const char* const fname_prevdocsimhash            = "prevdata/docsimhash.dat";
    //const char* const fname_workdocsimhash            = "work/docsimhash.dat";
    //const char* const fname_newdocsimhash             = "newdata/docsimhash.dat";
    const char* const fname_prevsemidupsigs           = "prevdata/semidupsigs.dat";
    const char* const fname_newsemidupsigs            = "newdata/semidupsigs.dat";
    const char* const fname_previndex_archive         = "previndex/%03d-archive";
    const char* const fname_workindex                 = "workindex/%03d-archive";
    const char* const fname_prevkiwidocsimhash        = "prevdata/docsimhash.kiwi.dat";
    const char* const fname_workkiwidocsimhash        = "work/docsimhash.kiwi.dat";
    const char* const fname_newkiwidocsimhash         = "newdata/docsimhash.kiwi.dat";

    // Semidups wo main
    const char* const fname_addsdwmdocgroup           = "work/addsdwmdocgroup.dat";
    const char* const fname_addsdwmsigdoc             = "work/addsdwmsigdoc.dat";
    const char* const fname_semidupswomain_nosdwmfix  = "work/semidupswomain.nosdwmfix.dat";
    const char* const fname_newsigdoc_nosdwmfix       = "work/sigdoc.nosdwmfix.dat";
    const char* const fname_newdocgroup_nosdwmfix     = "work/docgroup.nosdwmfix.dat";
    const char* const fname_delreason_nosdwmfix       = "work/delreason.nosdwmfix.dat";
    const char* const fname_deldoc_nosdwmfix          = "work/deldoc.nosdwmfix.dat";
    const char* const fname_fixedsemidupswomain       = "work/fixedsemidupswomain.dat";

    //SuperDups
    const char* const fname_superdupshistory    = "prevdata/superdupshistory.dat";
    const char* const fname_newsuperdupshistory = "newdata/superdupshistory.dat";
    const char* const fname_superdupsgroups     = "work/superdupsgroups.dat";

    //fast link index
    const char* const fname_clsfasthostlinksrc = "walrus/%03d/hostlinksrc.dat";
    const char* const fname_clspreparat     = "walrus/%03d/%s.dat";
    const char* const fname_clshostcr       = "walrus/%03d/doccrawlranks.dat";
    const char* const fname_clslinkdcr      = "walrus/%03d/linkdcr.dat";
    const char* const fname_clslinkdpr      = "walrus/%03d/linkdpr.dat";
    const char* const fname_clsindexpr      = "walrus/%03d/indexpr";
    const char* const fname_clsxranks       = "walrus/%03d/doc_xranks.dat";
    const char* const fname_walhost         = "walrus/host.dat";
    const char* const fname_fhr_table       = "temp_yandex/fhr";
    const char* const fname_yanduiddoc      = "yanddata/uiddoc.dat";
    const char* const fname_yandexarch_tag  = "yandex/oldbd%03dtag";
    const char* const fname_fasthr          = "temp_yandex/HR_FAST.gz";
    const char* const fname_fasthostrank    = "temp_yandex/hostrank.dat";

    //preparat
    const char* const fname_srcnames = "preparat/rsrcnames.%s.dat";
    const char* const fname_remoteglfnv = "preparat/rglfnv.%s.dat";
    const char* const fname_localglfnv = "preparat/glfnvu.%s.dat";
    const char* const fname_remotetexts = "preparat/texts.%s";
    const char* const fname_localtexts = "preparat/rtexts.%d";
    const char* const fname_fdocmap = "walrus/fdocmap";
    const char* const fname_docruid = "pagerank/work/docruid.dat";
    const char* const fname_clskiwipreparat = "walrus/%03d/kiwi_preparat.dat";

    // sitemaps
    const char* const fname_prevsitemapstatnew   = "prevdata/sitemapstatnew.dat";
    const char* const fname_prevsitemapurls   = "prevdata/sitemapurls.dat";
    const char* const fname_prevsitemaperrs   = "prevdata/sitemaperrs.dat";
    const char* const fname_prevsitemapexthost = "prevdata/sitemapexthost.dat";
    const char* const fname_prevsitemapindextositemap = "prevdata/sitemapindextositemap.dat";
    const char* const fname_prevsitemapredirs = "prevdata/sitemapredirs.dat";
    const char* const fname_webmastersitemaps = "work/webmastersitemaps.dat";
    const char* const fname_sitemaplinks      = "work/sitemaplinks.dat";
    const char* const fname_sitemapstat       = "work/sitemapstat.dat";
    const char* const fname_sitemaperrs       = "work/sitemaperrs.dat";
    const char* const fname_sitemapurls       = "work/sitemapurls.dat";
    const char* const fname_removedfromsitemapurls = "work/removedfromsitemapurls.dat";
    const char* const fname_sitemapindextositemap = "work/sitemapindextositemap.dat";
    const char* const fname_sitemapextqueue   = "work/sextqueue.dat";
    const char* const fname_newsitemapstatnew    = "newdata/sitemapstatnew.dat";
    const char* const fname_newsitemapurls    = "newdata/sitemapurls.dat";
    const char* const fname_newsitemaperrs    = "newdata/sitemaperrs.dat";
    const char* const fname_newsitemapexthost = "newdata/sitemapexthost.dat";
    const char* const fname_newsitemapindextositemap = "newdata/sitemapindextositemap.dat";
    const char* const fname_newsitemapredirs = "newdata/sitemapredirs.dat";
    const char* const fname_newsitemapsource = "newdata/sitemapsource.dat";
    const char* const fname_newallowedsitemaps  = "newdata/allowedsitemaps.dat";
    const char* const fname_newwebmasterallowedsitemaps = "newdata/webmaster_allowedsitemaps.dat";
    const char* const fname_localsitemaporange = "work/sitemaporange.txt";

    //crawl policies
    const char* const fname_newregexcrawl   = "config/newregexcrawl.txt";
    const char* const fname_oldregexcrawl   = "config/oldregexcrawl.txt";
    const char* const fname_yellowpagesstoringregexp = "walrus/yellowpagesstoringregexp.txt";

    const char* const fname_policycfg          = "config/policy.cfg";
    const char* const fname_localpolicyquotas  = "mrdata/localpolicyzonequotas.txt";
    const char* const fname_policyqueuemask    = "work/policyqueue.%s.dat";

    const char* const fname_fullqueuesize      = "work/fullqueuesize.txt";
    const char* const fname_fullpqueuesize     = "work/fullpqueuesize.txt";

    //crawl stat files
    const char* const fname_queuenum        = "prevdata/queuenum.dat";
    const char* const fname_newqueuenum     = "newdata/queuenum.dat";
    const char* const fname_hostqstat       = "prevdata/hostqstat.dat";
    const char* const fname_newhostqstat    = "newdata/hostqstat.dat";
    const char* const fname_policystat      = "prevdata/policystat.dat";
    const char* const fname_newpolicystat   = "newdata/policystat.dat";
    const char* const fname_queueparts      = "prevdata/queueparts.dat";
    const char* const fname_newqueueparts   = "newdata/queueparts.dat";
    const char* const fname_crawltime       = "queue/crawltime.txt";
    const char* const fname_newlinksstat    = "work/newlinksstat.txt";
    const char* const fname_effstat         = "work/effstat.txt";
    const char* const fname_quotastat       = "work/quotastat.txt";
    const char* const fname_quotaused       = "work/quotaused.txt";
    const char* const fname_complainhosts   = "config/complain.txt";
    const char* const fname_complainage     = "work/complainage.txt";
    const char* const fname_complainqueue   = "work/complainqueue.txt";
    const char* const fname_complainqueueph = "work/complainqueueph.txt";
    const char* const fname_dom2q           = "work/dom2queue.txt";
    const char* const fname_dom2qph         = "work/dom2queueph.txt";
    const char* const fname_realfakestat    = "newdata/realfakestat.txt";
    const char* const fname_real_realfakestat    = "newdata/real_realfakestat.txt";
    const char* const fname_storingpolicystat     = "dump/storingpolicystat.txt";
    const char* const fname_coverage_enq_uid      = "work/coverage_enq_uid.dat";
    const char* const fname_doclangstat        = "work/doclangstat.txt";
    const char* const fname_enq_queue       = "work/queue_enq.txt";
    const char* const fname_ipexcess        = "dump/ipexcess.txt";
    const char* const fname_ipnumstat       = "dump/ipnumstat.txt";
    const char* const fname_ippartstat      = "dump/ippartstat.txt";
    const char* const fname_hoststatusstat  = "work/hoststatusstat.txt";
    const char* const fname_iphoststatusalert = "work/iphoststatusalert.txt";
    const char* const fname_allqueuednum    = "work/allqueuednum.txt";
    const char* const fname_semiduplangstat = "work/semiduplangstat.txt";
    const char* const fname_doclogelsourcecounts = "work/doclogelsourcecounts.txt";

    // viewers
    const char* const fname_recrawlstaturls   = "config/recrawlstaturls.txt";
    const char* const fname_newqueuedat       = "newdata/queuedat.dat";
    const char* const fname_newqueuedatext    = "newdata/queuedatext.dat";
    const char* const fname_srdeldocs         = "work/srdeldocs.dat";
    const char* const fname_srstats           = "work/srstats.dat";
    const char* const fname_srdelhistory      = "prevdata/srdelhistory.dat";
    const char* const fname_newsrdelhistory   = "newdata/srdelhistory.dat";
    const char* const fname_uploadhistory     = "dump/uploadhistory.dat";
    const char* const fname_uploadsummarystat = "dump/uploadsummarystat.txt";
    const char* const fname_statrus0          = "newdata/statrus0.dat";
    const char* const fname_statrus1          = "newdata/statrus1.dat";

    //spider limits
    const char* const fname_limits          = "config/limits.txt";
    const char* const fname_distr           = "config/distr.conf";
    const char* const fname_iplimits        = "config/iplimits.dat";
    const char* const fname_lastipnewurls   = "config/lastnewurlsip.dat";
    const char* const fname_lastdocip       = "config/lastdocip.dat";
    const char* const fname_yaiplimits      = "dump/%s.iplimits.dat";
    const char* const fname_yaipnewurls     = "dump/%s.newurlsip.dat";
    const char* const fname_yadocip         = "dump/%s.docip.dat";
    const char* const fname_yaipstat        = "dump/%s.ipstat.dat";
    const char* const fname_docip           = "work/docip.dat";
    const char* const fname_newurlsip       = "work/newurlsip.dat";
    const char* const fname_hostautoquotas  = "work/hostautoquotas.dat";

    //mapreduce data
    const char* const fname_freqstatus          = "mrdata/freq_status.txt";
    const char* const fname_fromyandexstatus    = "mrdata/from_yandex_status.txt";
    const char* const fname_fromgoogle          = "mrdata/from_google.txt";
    const char* const fname_fromgoogle1m        = "mrdata/from_google_1m.txt";
    const char* const fname_urlsscdata          = "mrdata/urls.txt";
    const char* const fname_ownersscdata        = "mrdata/owners.txt";
    const char* const fname_urlsscdataweekly    = "mrdata/urls_week.txt";
    const char* const fname_ownersscdataweekly  = "mrdata/owners_week.txt";
    const char* const fname_urlsscdata3m        = "mrdata/urls_month.txt";
    const char* const fname_ownersscdata3m      = "mrdata/owners_month.txt";
    const char* const fname_stonefeatures       = "mrdata/sf_for_rr.bin";

    const char* const fname_scdataalltxt        = "mrdata/scdataall.txt";
    const char* const fname_ukropextdataranktxt  = "mrdata/ukropextdatarank.txt";

    const char* const fname_urlsscdatadat_temp  = "mrdata/outgoing/urlsscdata.dat";
    const char* const fname_ukropextdatadat_temp  = "mrdata/outgoing/ukropextdata.dat";
    const char* const fname_ownersscdatadat_temp = "mrdata/outgoing/ownersscdata.dat";

    const char* const fname_urlsscdatadat       = "config/urlsscdata.dat";
    const char* const fname_ukropextdatadat     = "config/ukropextdata.dat";
    const char* const fname_ownersscdatadat     = "config/ownersscdata.dat";

    // reclusterization

    const char* const fname_deletedurlstosave      = "work/deleted_urls_to_save.dat";
    const char* const fname_linksfromdeleteddocs   = "work/links_from_deleted_docs.dat";
    const char* const fname_linktextssfromdeleteddocs   = "work/linktexts_from_deleted_docs.dat";

    // crawl constants
    const ui32 zeroiplimit                  = 160000;
    const ui32 miniplimit                   = 50; // Min ip limit at all
    const ui32 startiplimit                 = 1000; // Ip limit for segments without docs on this IP (to get start with)
    const ui32 up_bound                     = 2000000;

    // some filter const
    const ui32 max_domain_level             = 6; //filter domains with level higher than this

    // constants
    const i32 paramstats_max_param_age = (60*60*24)*30; // in seconds
    const ui8 doclogsource_max_history = 30;

    // db page size
    const size_t pg_host       = 16u<<10;
    const size_t pg_host_idx   = 16u<<10;
    const size_t pg_url        = 64u<<10;
    const size_t pg_url_idx    = 32u<<10;
    const size_t pg_handle     = 16u<<10;
    const size_t pg_sigdoc     = 16u<<10;
    const size_t pg_docuid     = 32u<<10;
    const size_t pg_docuid_idx = 16u<<10;
    const size_t pg_docgroup   = 16u<<10;
    const size_t pg_robots     = 512u<<10;
    const size_t pg_hostlog    = 16u<<10;
    const size_t pg_hops       = 16u<<10;
    const size_t pg_weight     = 16u<<10;
    const size_t pg_book       = 64u<<10;

    // merge page size
    const size_t pg_queue      = 16u<<10;
    const size_t pg_adddoc     = 16u<<10;
    const size_t pg_deldoc     = 16u<<10;
    const size_t pg_docgrp     = 16u<<10;
    const size_t pg_indurl     = 16u<<10;
    const size_t pg_updurl     = 16u<<10;
    const size_t pg_addurl     = 64u<<10;
    const size_t pg_addportion = 16u<<10;
    const size_t pg_doclogel   = 4u<<20;
    const size_t pg_map        = 16u<<10;
    const size_t pg_crawlrank  = 4u<<10;

    const size_t fbufsize      = 1u<<20;
    const size_t largebufsize  = 64u<<20;
    const size_t handle_multiple = 10000;
    const size_t lportion_size = 32u<<20;
    const size_t lportion_readbuf = 1<<20;

    // sorer size
    static const size_t large_sorter_size = 800u<<20;       // 838 860 800
    static const size_t sorter_size       = 400u<<20;       // 419 430 400
    static const size_t small_sorter_size = 100u<<20;       // 104 857 600
    static const size_t very_small_sorter_size = 25u<<20;   // 26 214 400
    static const size_t ultra_small_sorter_size = 4u<<20;   // 4 194 304

    const ui32 rank_ruler_base_step = 10000;

    // selection rank data
    const char* const fname_sr_data = "sr.data.proto";

#ifdef TEST_MERGE
    const char * const fname_robotrankfactors = "work/rrfactors.txt";
#endif
};
