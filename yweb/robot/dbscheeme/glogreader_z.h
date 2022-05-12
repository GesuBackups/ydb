#pragma once

#include <library/cpp/microbdb/compressed.h>
#include "glogreader.h"

typedef TGLogReaderImpl<TCompressedInputFileManip> TGZippedLogReader;
