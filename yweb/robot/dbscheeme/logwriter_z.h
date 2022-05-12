#pragma once

#include <library/cpp/microbdb/compressed.h>
#include "logwriter.h"

typedef TTrivialLogWriterImpl<TCompressedOutputFileManip> TTrivialZippedLogWriter;
typedef TSimpleLogWriterImpl<TCompressedOutputFileManip> TZippedLogWriter;
