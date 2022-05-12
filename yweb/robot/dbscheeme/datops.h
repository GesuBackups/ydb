#pragma once
#include <util/generic/ptr.h>
#include <util/generic/string.h>
#include <library/cpp/microbdb/safeopen.h>
#include "baserecords.h"
#include "mergerecords.h"
#include "fastbotrecords.h"
#include "mergecfg.h"

//
//Размеры буферов для известных структур
//

template<typename TRecord>
struct TBufSizes {};

template<> struct TBufSizes<TUrlRec> {
    static size_t const pagesize  = dbcfg::pg_url;
    static size_t const buffersize = dbcfg::fbufsize;
    static size_t const sorter = dbcfg::large_sorter_size;
    static size_t const idx_page = dbcfg::pg_url_idx;
    static size_t const idx_buffer = dbcfg::fbufsize;
};

template<> struct TBufSizes<TUrlRecNoProto> {
    static size_t const pagesize  = dbcfg::pg_url;
    static size_t const buffersize = dbcfg::fbufsize;
    static size_t const sorter = dbcfg::large_sorter_size;
    static size_t const idx_page = dbcfg::pg_url_idx;
    static size_t const idx_buffer = dbcfg::fbufsize;
};

template<> struct TBufSizes<THostRec> {
    static size_t const pagesize  = dbcfg::pg_host;
    static size_t const buffersize = dbcfg::fbufsize;
    static size_t const sorter = dbcfg::large_sorter_size;
    static size_t const idx_page = dbcfg::pg_host_idx;
    static size_t const idx_buffer = dbcfg::fbufsize;
};

template<> struct TBufSizes<TIndUrlCrawlData> {
    static size_t const pagesize  = dbcfg::pg_doclogel;
    static size_t const buffersize = dbcfg::lportion_size;
    static size_t const sorter = dbcfg::large_sorter_size;
    static size_t const idx_page = dbcfg::pg_url_idx;
    static size_t const idx_buffer = dbcfg::fbufsize;
};

template<> struct TBufSizes<TIndUrlCrawlDataTransit> {
    static size_t const pagesize  = dbcfg::pg_doclogel;
    static size_t const buffersize = dbcfg::lportion_size;
    static size_t const sorter = dbcfg::large_sorter_size;
    static size_t const idx_page = dbcfg::pg_url_idx;
    static size_t const idx_buffer = dbcfg::fbufsize;
};

template<> struct TBufSizes<TFastDocDate> {
    static size_t const pagesize  = dbcfg::pg_url;
    static size_t const buffersize = dbcfg::fbufsize;
    static size_t const sorter = dbcfg::large_sorter_size;
    static size_t const idx_page = dbcfg::pg_url_idx;
    static size_t const idx_buffer = dbcfg::fbufsize;
};

//
//
//

template<> struct TBufSizes<TDocNumRec> {
    static size_t const pagesize  = dbcfg::pg_url;
    static size_t const buffersize = dbcfg::fbufsize;
    static size_t const sorter = dbcfg::large_sorter_size;
    static size_t const idx_page = dbcfg::pg_url_idx;
    static size_t const idx_buffer = dbcfg::fbufsize;
};

//
//
//

template<typename TRecord>
struct TInDatCreator {
    typedef TRecord TRecordType;
    typedef TBufSizes<TRecordType> TBufSizesType;
    typedef TInDatFile<TRecordType> TDatFileType;
    typedef TAutoPtr<TDatFileType> TRetType;

    static TRetType Create(TString const& fname, TString const& label) {
        TRetType ret(new TDatFileType(label.data(), TBufSizesType::buffersize, 0));
        ret->Open(fname.data());
        return ret;
    }
};

template<typename TRecord, typename TKey>
struct TDirectInDatCreator {
    typedef TKey TKeyType;
    typedef TRecord TRecordType;
    typedef TBufSizes<TRecordType> TBufSizesType;
    typedef TDirectInDatFile<TRecordType, TKeyType> TDatFileType;
    typedef TAutoPtr<TDatFileType> TRetType;

    static TRetType Create(TString const& fname) {
        TRetType ret(new TDatFileType());
        ret->Open(fname.data(), TBufSizesType::buffersize, TBufSizesType::idx_buffer, 0);
        return ret;
    }
};

template<typename TRecord>
struct TOutDatCreator {
    typedef TRecord TRecordType;
    typedef TBufSizes<TRecordType> TBufSizesType;
    typedef TOutDatFile<TRecordType> TDatFileType;
    typedef TAutoPtr<TDatFileType> TRetType;
    static TRetType Create(TString const& fname, TString const& label) {
        TRetType ret(new TDatFileType(label.data(), TBufSizesType::pagesize, TBufSizesType::buffersize, 0));
        ret->Open(fname.data());
        return ret;
    }
};

template<typename TRecord, typename TKey>
struct TDirectOutDatCreator {
    typedef TKey TKeyType;
    typedef TRecord TRecordType;
    typedef TBufSizes<TRecordType> TBufSizesType;
    typedef TOutDirectFile<TRecordType, TKeyType> TDatFileType;
    typedef TAutoPtr<TDatFileType> TRetType;
    static TRetType Create(TString const& fname, TString const& label) {
        TRetType ret(new TDatFileType(label.data(), TBufSizesType::pagesize, TBufSizesType::buffersize, TBufSizesType::idx_page, TBufSizesType::idx_buffer, 0));
        ret->Open(fname.data());
        return ret;
    }
};
