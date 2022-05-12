#include "indexannportion.h"

#include <yweb/robot/userdata/indexann/lib/serializer.h>
#include <yweb/robot/userdata/indexann/lib/filter.h>

#include <util/stream/buffer.h>


TIndexAnnPortionKu::TIndexAnnPortionKu(const TFsPath& dataPath)
{
    NIndexAnn::TWriterConfig cfg(dataPath);
    cfg.InvIndexMem = 50 * 1024 * 1024;
    cfg.InvIndexDocCount = 1;
    AnnWriter.Reset(new NIndexAnn::TMemoryWriter(cfg, IYndexStorage::FINAL_FORMAT));
    AnnWriter->AddDirectTextCallback(&SentenceLensProcessor);
}

const NIndexAnn::TPortionPB* TIndexAnnPortionKu::Ku(const NIndexAnn::IFeaturesProfile& profile, const NIndexAnn::TIndexAnnSiteData& anns, ui32 docId) const {
    NIndexAnn::TIndexAnnSiteData filtered;
    if (!NIndexAnn::FilterEssentialFeatures(profile, anns, filtered)) {
        return nullptr;
    }

    TVector<const NIndexAnn::TAnnotationRec*> filteredAnns;
    FilterIndexAnnRecords(filtered, profile, filteredAnns);

    AnnWriter->StartPortion();

    AnnWriter->StartDoc(docId);
    NIndexAnn::TBinaryStreamsData streamsData;
    bool hasData = false;
    for (const auto& ann : filteredAnns) {
        if (!ann->HasText()) {
            ythrow TInvalidDataException();
        }
        streamsData.Clear();
        for (const auto& d : ann->GetData()) {
            NIndexAnn::WriteAnnData(d, streamsData);
        }

        NIndexAnn::TSentenceParams sentParams;
        if (ann->HasTextLanguage()) {
            sentParams.TextLanguage = static_cast<ELanguage>(ann->GetTextLanguage());
        }

        if (streamsData && AnnWriter->StartSentence(ann->GetText(), sentParams)) {
            size_t offset = 0;
            for (size_t i = 0; i < streamsData.Regions.size(); ++i) {
                size_t curSize = streamsData.Sizes[i];
                AnnWriter->AddData(
                    streamsData.Regions[i],
                    streamsData.Streams[i],
                    TArrayRef<const char>(streamsData.Data.Data() + offset, curSize)
                );
                offset += curSize;
            }
            hasData = true;
        }
    }

    AnnWriter->FinishDoc();

    if (!hasData)
        return nullptr;

    auto& res = AnnWriter->DonePortion();
    {
        TStringStream out;
        SentenceLensProcessor.WriteWithZeroSentence(&out);
        res.SetSentenceLens(out.Str());
    }

    return &res;
}

