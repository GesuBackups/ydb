#include "schemaful_writer.h"
#include "config.h"

#include <yt/yt/core/concurrency/async_stream.h>

namespace NYT::NFormats {

using namespace NConcurrency;
using namespace NYson;
using namespace NTableClient;

////////////////////////////////////////////////////////////////////////////////

TSchemafulWriter::TSchemafulWriter(
    NConcurrency::IAsyncOutputStreamPtr stream,
    NTableClient::TTableSchemaPtr schema,
    const std::function<std::unique_ptr<IFlushableYsonConsumer>(IZeroCopyOutput*)>& consumerBuilder)
    : Stream_(std::move(stream))
    , Schema_(std::move(schema))
    , Consumer_(consumerBuilder(&Buffer_))
{ }

TFuture<void> TSchemafulWriter::Close()
{
    return Result_;
}

bool TSchemafulWriter::Write(TRange<TUnversionedRow> rows)
{
    Buffer_.Clear();

    int columnCount = Schema_->GetColumnCount();
    for (auto row : rows) {
        if (!row) {
            Consumer_->OnEntity();
            continue;
        }

        YT_ASSERT(static_cast<int>(row.GetCount()) >= columnCount);
        Consumer_->OnBeginMap();
        for (int index = 0; index < columnCount; ++index) {
            const auto& value = row[index];

            const auto& column = Schema_->Columns()[index];
            Consumer_->OnKeyedItem(column.Name());

            switch (value.Type) {
                case EValueType::Int64:
                    Consumer_->OnInt64Scalar(value.Data.Int64);
                    break;
                case EValueType::Uint64:
                    Consumer_->OnUint64Scalar(value.Data.Uint64);
                    break;
                case EValueType::Double:
                    Consumer_->OnDoubleScalar(value.Data.Double);
                    break;
                case EValueType::Boolean:
                    Consumer_->OnBooleanScalar(value.Data.Boolean);
                    break;
                case EValueType::String:
                    Consumer_->OnStringScalar(value.AsStringBuf());
                    break;
                case EValueType::Null:
                    Consumer_->OnEntity();
                    break;
                case EValueType::Any:
                    Consumer_->OnRaw(value.AsStringBuf(), EYsonType::Node);
                    break;

                case EValueType::Composite:

                case EValueType::Min:
                case EValueType::Max:
                case EValueType::TheBottom:
                    ThrowUnexpectedValueType(value.Type);
            }
        }
        Consumer_->OnEndMap();
    }

    Consumer_->Flush();
    auto buffer = Buffer_.Flush();
    Result_ = Stream_->Write(buffer);
    return Result_.IsSet() && Result_.Get().IsOK();
}

TFuture<void> TSchemafulWriter::GetReadyEvent()
{
    return Result_;
}

////////////////////////////////////////////////////////////////////////////////


} // namespace NYT::NFormats
