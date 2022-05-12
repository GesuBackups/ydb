#pragma once

#include <ysite/directtext/sentences_lens/sentenceslensproc.h>
#include <kernel/indexann/writer/writer.h>

#include <kernel/indexann/protos/data.pb.h>
#include <kernel/indexann/protos/portion.pb.h>
#include <kernel/indexann_data/data.h>

#include <util/generic/ptr.h>
#include <util/folder/path.h>


class TIndexAnnPortionKu {
private:
    THolder<NIndexAnn::TMemoryWriter> AnnWriter;
    TSentenceLensProcessor SentenceLensProcessor;

public:
    class TInvalidDataException : public yexception {};

public:
    TIndexAnnPortionKu(const TFsPath& dataPath = TFsPath());

    const NIndexAnn::TPortionPB* Ku(const NIndexAnn::IFeaturesProfile& profile, const NIndexAnn::TIndexAnnSiteData& anns, ui32 docId = 0) const;
};

