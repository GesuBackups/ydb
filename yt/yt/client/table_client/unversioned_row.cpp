#include "unversioned_row.h"

#include "composite_compare.h"
#include "helpers.h"
#include "serialize.h"
#include "unversioned_value.h"
#include "validate_logical_type.h"

#include <yt/yt/client/table_client/name_table.h>
#include <yt/yt/client/table_client/row_buffer.h>
#include <yt/yt/client/table_client/schema.h>

#include <yt/yt/library/decimal/decimal.h>

#include <yt/yt/core/misc/hash.h>
#include <yt/yt/core/misc/string.h>
#include <yt/yt/core/misc/range.h>
#include <yt/yt/core/misc/format.h>

#include <yt/yt/core/yson/consumer.h>

#include <yt/yt/core/ytree/helpers.h>
#include <yt/yt/core/ytree/node.h>
#include <yt/yt/core/ytree/convert.h>

#include <library/cpp/yt/farmhash/farm_hash.h>

#include <library/cpp/yt/coding/varint.h>

#include <util/generic/ymath.h>

#include <util/charset/utf8.h>
#include <util/stream/str.h>

#include <cmath>

namespace NYT::NTableClient {

using namespace NChunkClient;
using namespace NYTree;
using namespace NYson;

////////////////////////////////////////////////////////////////////////////////

const TString SerializedNullRow("");
struct TOwningRowTag { };

////////////////////////////////////////////////////////////////////////////////

size_t GetByteSize(const TUnversionedValue& value)
{
    int result = MaxVarUint32Size * 2; // id and type

    switch (value.Type) {
        case EValueType::Null:
        case EValueType::Min:
        case EValueType::Max:
        case EValueType::TheBottom:
            return result;

        case EValueType::Int64:
        case EValueType::Uint64:
            result += MaxVarInt64Size;
            return result;

        case EValueType::Double:
            result += sizeof(double);
            return result;


        case EValueType::Boolean:
            result += 1;
            return result;

        case EValueType::String:
        case EValueType::Any:
        case EValueType::Composite:
            result += MaxVarUint32Size + value.Length;
            return result;
    }

    return result;
}

size_t WriteRowValue(char* output, const TUnversionedValue& value)
{
    char* current = output;

    current += WriteVarUint32(current, value.Id);
    current += WriteVarUint32(current, static_cast<ui16>(value.Type));

    switch (value.Type) {
        case EValueType::Null:
        case EValueType::Min:
        case EValueType::Max:
        case EValueType::TheBottom:
            break;

        case EValueType::Int64:
            current += WriteVarInt64(current, value.Data.Int64);
            break;

        case EValueType::Uint64:
            current += WriteVarUint64(current, value.Data.Uint64);
            break;

        case EValueType::Double:
            ::memcpy(current, &value.Data.Double, sizeof (double));
            current += sizeof (double);
            break;

        case EValueType::Boolean:
            *current++ = value.Data.Boolean ? '\x01' : '\x00';
            break;

        case EValueType::String:
        case EValueType::Any:
        case EValueType::Composite:
            current += WriteVarUint32(current, value.Length);
            ::memcpy(current, value.Data.String, value.Length);
            current += value.Length;
            break;

        default:
            YT_ABORT();
    }

    return current - output;
}

size_t ReadRowValue(const char* input, TUnversionedValue* value)
{
    const char* current = input;

    ui32 id;
    current += ReadVarUint32(current, &id);

    ui32 typeValue;
    current += ReadVarUint32(current, &typeValue);
    auto type = static_cast<EValueType>(typeValue);

    switch (type) {
        case EValueType::Null:
        case EValueType::Min:
        case EValueType::Max:
        case EValueType::TheBottom:
            *value = MakeUnversionedSentinelValue(type, id);
            break;

        case EValueType::Int64: {
            i64 data;
            current += ReadVarInt64(current, &data);
            *value = MakeUnversionedInt64Value(data, id);
            break;
        }

        case EValueType::Uint64: {
            ui64 data;
            current += ReadVarUint64(current, &data);
            *value = MakeUnversionedUint64Value(data, id);
            break;
        }

        case EValueType::Double: {
            double data;
            ::memcpy(&data, current, sizeof (double));
            current += sizeof (double);
            *value = MakeUnversionedDoubleValue(data, id);
            break;
        }

        case EValueType::Boolean: {
            bool data = (*current) == 1;
            current += 1;
            *value = MakeUnversionedBooleanValue(data, id);
            break;
        }

        case EValueType::Any:
        case EValueType::Composite:
        case EValueType::String: {
            ui32 length;
            current += ReadVarUint32(current, &length);
            TStringBuf data(current, current + length);
            current += length;
            *value = MakeUnversionedStringLikeValue(type, data, id);
            break;
        }

        default:
            ThrowUnexpectedValueType(type);
    }

    return current - input;
}

void Save(TStreamSaveContext& context, const TUnversionedValue& value)
{
    auto* output = context.GetOutput();
    if (IsStringLikeType(value.Type)) {
        output->Write(&value, sizeof (ui16) + sizeof (ui16) + sizeof (ui32)); // Id, Type, Length
        if (value.Length != 0) {
            output->Write(value.Data.String, value.Length);
        }
    } else {
        output->Write(&value, sizeof (TUnversionedValue));
    }
}

void Load(TStreamLoadContext& context, TUnversionedValue& value, TChunkedMemoryPool* pool)
{
    auto* input = context.GetInput();
    const size_t fixedSize = sizeof (ui16) + sizeof (ui16) + sizeof (ui32); // Id, Type, Length
    YT_VERIFY(input->Load(&value, fixedSize) == fixedSize);
    if (IsStringLikeType(value.Type)) {
        if (value.Length != 0) {
            value.Data.String = pool->AllocateUnaligned(value.Length);
            YT_VERIFY(input->Load(const_cast<char*>(value.Data.String), value.Length) == value.Length);
        } else {
            value.Data.String = nullptr;
        }
    } else {
        YT_VERIFY(input->Load(&value.Data, sizeof (value.Data)) == sizeof (value.Data));
    }
}

size_t GetYsonSize(const TUnversionedValue& value)
{
    switch (value.Type) {
        case EValueType::Any:
        case EValueType::Composite:
            return value.Length;

        case EValueType::Null:
            // Marker type.
            return 1;

        case EValueType::Int64:
        case EValueType::Uint64:
            // Type marker + size;
            return 1 + MaxVarInt64Size;

        case EValueType::Double:
            // Type marker + sizeof double.
            return 1 + 8;

        case EValueType::String:
            // Type marker + length + string bytes.
            return 1 + MaxVarInt32Size + value.Length;

        case EValueType::Boolean:
            // Type marker + value.
            return 1 + 1;

        default:
            YT_ABORT();
    }
}

size_t WriteYson(char* buffer, const TUnversionedValue& unversionedValue)
{
    // TODO(psushin): get rid of output stream.
    TMemoryOutput output(buffer, GetYsonSize(unversionedValue));
    TYsonWriter writer(&output, EYsonFormat::Binary);
    switch (unversionedValue.Type) {
        case EValueType::Int64:
            writer.OnInt64Scalar(unversionedValue.Data.Int64);
            break;
        case EValueType::Uint64:
            writer.OnUint64Scalar(unversionedValue.Data.Uint64);
            break;

        case EValueType::Double:
            writer.OnDoubleScalar(unversionedValue.Data.Double);
            break;

        case EValueType::String:
            writer.OnStringScalar(unversionedValue.AsStringBuf());
            break;

        case EValueType::Boolean:
            writer.OnBooleanScalar(unversionedValue.Data.Boolean);
            break;

        case EValueType::Null:
            writer.OnEntity();
            break;

        default:
            YT_ABORT();
    }
    return output.Buf() - buffer;
}

namespace {

[[noreturn]] void ThrowIncomparableTypes(const TUnversionedValue& lhs, const TUnversionedValue& rhs)
{
    THROW_ERROR_EXCEPTION(
        EErrorCode::IncomparableType,
        "Cannot compare values of types %Qlv and %Qlv; only scalar types are allowed for key columns",
        lhs.Type,
        rhs.Type)
        << TErrorAttribute("lhs_value", lhs)
        << TErrorAttribute("rhs_value", rhs);
}

} // namespace

Y_FORCE_INLINE bool IsSentinel(EValueType valueType)
{
    return valueType == EValueType::Min || valueType == EValueType::Max;
}

int CompareRowValues(const TUnversionedValue& lhs, const TUnversionedValue& rhs)
{
    // TODO(babenko): check flags; forbid comparing hunks and aggregates.

    if (lhs.Type == EValueType::Any || rhs.Type == EValueType::Any) {
        if (!IsSentinel(lhs.Type) && !IsSentinel(rhs.Type)) {
            // Never compare composite values with non-sentinels.
            ThrowIncomparableTypes(lhs, rhs);
        }
    }

    if (lhs.Type == EValueType::Composite || rhs.Type == EValueType::Composite) {
        if (lhs.Type != rhs.Type) {
            if (!IsSentinel(lhs.Type) && lhs.Type != EValueType::Null &&
                !IsSentinel(rhs.Type) && rhs.Type != EValueType::Null)
            {
                ThrowIncomparableTypes(lhs, rhs);
            }
            return static_cast<int>(lhs.Type) - static_cast<int>(rhs.Type);
        }
        auto lhsData = lhs.AsStringBuf();
        auto rhsData = rhs.AsStringBuf();
        try {
            return CompareCompositeValues(lhsData, rhsData);
        } catch (const std::exception& ex) {
            THROW_ERROR_EXCEPTION("Cannot compare complex values")
                << TErrorAttribute("lhs_value", lhs)
                << TErrorAttribute("rhs_value", rhs)
                << ex;
        }
    }

    if (Y_UNLIKELY(lhs.Type != rhs.Type)) {
        return static_cast<int>(lhs.Type) - static_cast<int>(rhs.Type);
    }

    switch (lhs.Type) {
        case EValueType::Int64: {
            auto lhsValue = lhs.Data.Int64;
            auto rhsValue = rhs.Data.Int64;
            if (lhsValue < rhsValue) {
                return -1;
            } else if (lhsValue > rhsValue) {
                return +1;
            } else {
                return 0;
            }
        }

        case EValueType::Uint64: {
            auto lhsValue = lhs.Data.Uint64;
            auto rhsValue = rhs.Data.Uint64;
            if (lhsValue < rhsValue) {
                return -1;
            } else if (lhsValue > rhsValue) {
                return +1;
            } else {
                return 0;
            }
        }

        case EValueType::Double: {
            double lhsValue = lhs.Data.Double;
            double rhsValue = rhs.Data.Double;
            if (lhsValue < rhsValue) {
                return -1;
            } else if (lhsValue > rhsValue) {
                return +1;
            } else if (std::isnan(lhsValue)) {
                if (std::isnan(rhsValue)) {
                    return 0;
                } else {
                    return 1;
                }
            } else if (std::isnan(rhsValue)) {
                return -1;
            } else {
                return 0;
            }
        }

        case EValueType::Boolean: {
            bool lhsValue = lhs.Data.Boolean;
            bool rhsValue = rhs.Data.Boolean;
            if (lhsValue < rhsValue) {
                return -1;
            } else if (lhsValue > rhsValue) {
                return +1;
            } else {
                return 0;
            }
        }

        case EValueType::String: {
            size_t lhsLength = lhs.Length;
            size_t rhsLength = rhs.Length;
            size_t minLength = std::min(lhsLength, rhsLength);
            int result = ::memcmp(lhs.Data.String, rhs.Data.String, minLength);
            if (result == 0) {
                if (lhsLength < rhsLength) {
                    return -1;
                } else if (lhsLength > rhsLength) {
                    return +1;
                } else {
                    return 0;
                }
            } else {
                return result;
            }
        }

        // NB: All sentinel types are equal.
        case EValueType::Null:
        case EValueType::Min:
        case EValueType::Max:
            return 0;

        default:
            YT_ABORT();
    }
}

bool operator == (const TUnversionedValue& lhs, const TUnversionedValue& rhs)
{
    return CompareRowValues(lhs, rhs) == 0;
}

bool operator != (const TUnversionedValue& lhs, const TUnversionedValue& rhs)
{
    return CompareRowValues(lhs, rhs) != 0;
}

bool operator <= (const TUnversionedValue& lhs, const TUnversionedValue& rhs)
{
    return CompareRowValues(lhs, rhs) <= 0;
}

bool operator < (const TUnversionedValue& lhs, const TUnversionedValue& rhs)
{
    return CompareRowValues(lhs, rhs) < 0;
}

bool operator >= (const TUnversionedValue& lhs, const TUnversionedValue& rhs)
{
    return CompareRowValues(lhs, rhs) >= 0;
}

bool operator > (const TUnversionedValue& lhs, const TUnversionedValue& rhs)
{
    return CompareRowValues(lhs, rhs) > 0;
}

bool AreRowValuesIdentical(const TUnversionedValue& lhs, const TUnversionedValue& rhs)
{
    if (lhs.Flags != rhs.Flags) {
        return false;
    }

    if (lhs.Type != rhs.Type) {
        return false;
    }

    // Handle "any" and "composite" values specially since these are not comparable.
    if (lhs.Type == EValueType::Any || lhs.Type == EValueType::Composite) {
        return
            lhs.Length == rhs.Length &&
            ::memcmp(lhs.Data.String, rhs.Data.String, lhs.Length) == 0;
    }

    return lhs == rhs;
}

bool AreRowValuesIdentical(const TVersionedValue& lhs, const TVersionedValue& rhs)
{
    if (lhs.Timestamp != rhs.Timestamp) {
        return false;
    }

    return AreRowValuesIdentical(
        static_cast<const TUnversionedValue&>(lhs),
        static_cast<const TUnversionedValue&>(rhs));
}

////////////////////////////////////////////////////////////////////////////////

int CompareRows(
    const TUnversionedValue* lhsBegin,
    const TUnversionedValue* lhsEnd,
    const TUnversionedValue* rhsBegin,
    const TUnversionedValue* rhsEnd)
{
    auto* lhsCurrent = lhsBegin;
    auto* rhsCurrent = rhsBegin;
    while (lhsCurrent != lhsEnd && rhsCurrent != rhsEnd) {
        int result = CompareRowValues(*lhsCurrent++, *rhsCurrent++);
        if (result != 0) {
            return result;
        }
    }
    return static_cast<int>(lhsEnd - lhsBegin) - static_cast<int>(rhsEnd - rhsBegin);
}

int CompareRows(TUnversionedRow lhs, TUnversionedRow rhs, ui32 prefixLength)
{
    if (!lhs && !rhs) {
        return 0;
    }

    if (lhs && !rhs) {
        return +1;
    }

    if (!lhs && rhs) {
        return -1;
    }

    return CompareRows(
        lhs.Begin(),
        lhs.Begin() + std::min(lhs.GetCount(), prefixLength),
        rhs.Begin(),
        rhs.Begin() + std::min(rhs.GetCount(), prefixLength));
}

bool operator == (TUnversionedRow lhs, TUnversionedRow rhs)
{
    return CompareRows(lhs, rhs) == 0;
}

bool operator != (TUnversionedRow lhs, TUnversionedRow rhs)
{
    return CompareRows(lhs, rhs) != 0;
}

bool operator <= (TUnversionedRow lhs, TUnversionedRow rhs)
{
    return CompareRows(lhs, rhs) <= 0;
}

bool operator < (TUnversionedRow lhs, TUnversionedRow rhs)
{
    return CompareRows(lhs, rhs) < 0;
}

bool operator >= (TUnversionedRow lhs, TUnversionedRow rhs)
{
    return CompareRows(lhs, rhs) >= 0;
}

bool operator > (TUnversionedRow lhs, TUnversionedRow rhs)
{
    return CompareRows(lhs, rhs) > 0;
}

bool AreRowsIdentical(TUnversionedRow lhs, TUnversionedRow rhs)
{
    if (!lhs && !rhs) {
        return true;
    }

    if (!lhs || !rhs) {
        return false;
    }

    if (lhs.GetCount() != rhs.GetCount()) {
        return false;
    }

    for (int index = 0; index < static_cast<int>(lhs.GetCount()); ++index) {
        if (!AreRowValuesIdentical(lhs[index], rhs[index])) {
            return false;
        }
    }
    return true;
}

////////////////////////////////////////////////////////////////////////////////

bool operator == (TUnversionedRow lhs, const TUnversionedOwningRow& rhs)
{
    return CompareRows(lhs, rhs) == 0;
}

bool operator != (TUnversionedRow lhs, const TUnversionedOwningRow& rhs)
{
    return CompareRows(lhs, rhs) != 0;
}

bool operator <= (TUnversionedRow lhs, const TUnversionedOwningRow& rhs)
{
    return CompareRows(lhs, rhs) <= 0;
}

bool operator < (TUnversionedRow lhs, const TUnversionedOwningRow& rhs)
{
    return CompareRows(lhs, rhs) < 0;
}

bool operator >= (TUnversionedRow lhs, const TUnversionedOwningRow& rhs)
{
    return CompareRows(lhs, rhs) >= 0;
}

bool operator > (TUnversionedRow lhs, const TUnversionedOwningRow& rhs)
{
    return CompareRows(lhs, rhs) > 0;
}

////////////////////////////////////////////////////////////////////////////////

ui64 GetHash(TUnversionedRow row, ui32 keyColumnCount)
{
    auto partCount = std::min(row.GetCount(), keyColumnCount);
    const auto* begin = row.Begin();
    return GetHash(begin, begin + partCount);
}

TFingerprint GetFarmFingerprint(TUnversionedRow row, ui32 keyColumnCount)
{
    auto partCount = std::min(row.GetCount(), keyColumnCount);
    const auto* begin = row.Begin();
    return GetFarmFingerprint(begin, begin + partCount);
}

size_t GetUnversionedRowByteSize(ui32 valueCount)
{
    return sizeof(TUnversionedRowHeader) + sizeof(TUnversionedValue) * valueCount;
}

size_t GetDataWeight(TUnversionedRow row)
{
    if (!row) {
        return 0;
    }

    return 1 + std::accumulate(
        row.Begin(),
        row.End(),
        0ULL,
        [] (size_t x, const TUnversionedValue& value) {
            return GetDataWeight(value) + x;
        });
}

////////////////////////////////////////////////////////////////////////////////

TMutableUnversionedRow TMutableUnversionedRow::Allocate(TChunkedMemoryPool* pool, ui32 valueCount)
{
    size_t byteSize = GetUnversionedRowByteSize(valueCount);
    return Create(pool->AllocateAligned(byteSize), valueCount);
}

TMutableUnversionedRow TMutableUnversionedRow::Create(void* buffer, ui32 valueCount)
{
    auto* header = reinterpret_cast<TUnversionedRowHeader*>(buffer);
    header->Count = valueCount;
    header->Capacity = valueCount;
    return TMutableUnversionedRow(header);
}

////////////////////////////////////////////////////////////////////////////////

namespace {

class TYsonAnyValidator
    : public IYsonConsumer
{
public:
    void OnStringScalar(TStringBuf /*value*/) override
    { }

    void OnInt64Scalar(i64 /*value*/) override
    { }

    void OnUint64Scalar(ui64 /*value*/) override
    { }

    void OnDoubleScalar(double /*value*/) override
    { }

    void OnBooleanScalar(bool /*value*/) override
    { }

    void OnEntity() override
    { }

    void OnBeginList() override
    {
        ++Depth_;
    }

    void OnListItem() override
    { }

    void OnEndList() override
    {
        --Depth_;
    }

    void OnBeginMap() override
    {
        ++Depth_;
    }

    void OnKeyedItem(TStringBuf /*key*/) override
    { }

    void OnEndMap() override
    {
        --Depth_;
    }

    void OnBeginAttributes() override
    {
        if (Depth_ == 0) {
            THROW_ERROR_EXCEPTION("Table values cannot have top-level attributes");
        }
    }

    void OnEndAttributes() override
    { }

    void OnRaw(TStringBuf /*yson*/, EYsonType /*type*/) override
    { }

private:
    int Depth_ = 0;
};

void ValidateAnyValue(TStringBuf yson)
{
    TYsonAnyValidator validator;
    ParseYsonStringBuffer(yson, EYsonType::Node, &validator);
}

void ValidateDynamicValue(const TUnversionedValue& value, bool isKey)
{
    switch (value.Type) {
        case EValueType::String:
            if (value.Length > MaxStringValueLength) {
                THROW_ERROR_EXCEPTION("Value of type %Qlv is too long for dynamic data: length %v, limit %v",
                    value.Type,
                    value.Length,
                    MaxStringValueLength);
            }
            break;

        case EValueType::Any:
            if (value.Length > MaxAnyValueLength) {
                THROW_ERROR_EXCEPTION("Value of type %Qlv is too long for dynamic data: length %v, limit %v",
                    value.Type,
                    value.Length,
                    MaxAnyValueLength);
            }
            ValidateAnyValue(value.AsStringBuf());
            break;

        case EValueType::Double:
            if (isKey && std::isnan(value.Data.Double)) {
                THROW_ERROR_EXCEPTION("Key of type \"double\" cannot be NaN");
            }
            break;

        default:
            break;
    }
}

void ValidateClientRow(
    TUnversionedRow row,
    const TTableSchema& schema,
    const TNameTableToSchemaIdMapping& idMapping,
    const TNameTablePtr& nameTable,
    bool isKey,
    std::optional<int> tabletIndexColumnId = std::nullopt)
{
    if (!row) {
        THROW_ERROR_EXCEPTION("Unexpected empty row");
    }

    ValidateRowValueCount(row.GetCount());
    ValidateKeyColumnCount(schema.GetKeyColumnCount());

    bool keyColumnSeen[MaxKeyColumnCount]{};
    bool haveDataColumns = false;

    for (const auto& value : row) {
        int mappedId = ApplyIdMapping(value, &idMapping);
        if (mappedId < 0 || mappedId >= std::ssize(schema.Columns())) {
            int size = nameTable->GetSize();
            if (value.Id < 0 || value.Id >= size) {
                THROW_ERROR_EXCEPTION("Expected value id in range [0:%v] but got %v",
                    size - 1,
                    value.Id);
            }

            THROW_ERROR_EXCEPTION("Unexpected column %Qv", nameTable->GetName(value.Id));
        }

        const auto& column = schema.Columns()[mappedId];
        ValidateValueType(value, schema, mappedId, /*typeAnyAcceptsAllValues*/false);

        if (Any(value.Flags & EValueFlags::Aggregate) && !column.Aggregate()) {
            THROW_ERROR_EXCEPTION(
                "\"aggregate\" flag is set for value in column %Qv which is not aggregating",
                column.Name());
        }

        if (mappedId < schema.GetKeyColumnCount()) {
            if (keyColumnSeen[mappedId]) {
                THROW_ERROR_EXCEPTION("Duplicate key column %Qv",
                    column.Name());
            }
            keyColumnSeen[mappedId] = true;
            ValidateKeyValue(value);
        } else if (isKey) {
            THROW_ERROR_EXCEPTION("Non-key column %Qv in a key",
                column.Name());
        } else {
            haveDataColumns = true;
            ValidateDataValue(value);
        }
    }

    if (!isKey && !haveDataColumns) {
        THROW_ERROR_EXCEPTION("At least one non-key column must be specified");
    }

    if (tabletIndexColumnId) {
        YT_VERIFY(std::ssize(idMapping) > *tabletIndexColumnId);
        auto mappedId = idMapping[*tabletIndexColumnId];
        YT_VERIFY(mappedId >= 0);
        keyColumnSeen[mappedId] = true;
    }

    for (int index = 0; index < schema.GetKeyColumnCount(); ++index) {
        if (!keyColumnSeen[index] && !schema.Columns()[index].Expression()) {
            THROW_ERROR_EXCEPTION("Missing key column %Qv",
                schema.Columns()[index].Name());
        }
    }

    auto dataWeight = GetDataWeight(row);
    if (dataWeight >= MaxClientVersionedRowDataWeight) {
        THROW_ERROR_EXCEPTION("Row is too large: data weight %v, limit %v",
            dataWeight,
            MaxClientVersionedRowDataWeight);
    }
}

} // namespace

////////////////////////////////////////////////////////////////////////////////

TString SerializeToString(TUnversionedRow row)
{
    return row
        ? SerializeToString(row.Begin(), row.End())
        : SerializedNullRow;
}

TString SerializeToString(const TUnversionedValue* begin, const TUnversionedValue* end)
{
    int size = 2 * MaxVarUint32Size; // header size
    for (auto* it = begin; it != end; ++it) {
        size += GetByteSize(*it);
    }

    TString buffer;
    buffer.resize(size);

    char* current = const_cast<char*>(buffer.data());
    current += WriteVarUint32(current, 0); // format version
    current += WriteVarUint32(current, static_cast<ui32>(std::distance(begin, end)));

    for (auto* it = begin; it != end; ++it) {
        current += WriteRowValue(current, *it);
    }

    buffer.resize(current - buffer.data());

    return buffer;
}

TUnversionedOwningRow DeserializeFromString(const TString& data, std::optional<int> nullPaddingWidth = std::nullopt)
{
    if (data == SerializedNullRow) {
        return TUnversionedOwningRow();
    }

    const char* current = data.data();

    ui32 version;
    current += ReadVarUint32(current, &version);
    YT_VERIFY(version == 0);

    ui32 valueCount;
    current += ReadVarUint32(current, &valueCount);

    // TODO(max42): YT-14049.
    int nullCount = nullPaddingWidth ? std::max<int>(0, *nullPaddingWidth - static_cast<int>(valueCount)) : 0;

    size_t fixedSize = GetUnversionedRowByteSize(valueCount + nullCount);
    auto rowData = TSharedMutableRef::Allocate<TOwningRowTag>(fixedSize, false);
    auto* header = reinterpret_cast<TUnversionedRowHeader*>(rowData.Begin());

    header->Count = static_cast<i32>(valueCount + nullCount);
    header->Capacity = static_cast<i32>(valueCount + nullCount);

    auto* values = reinterpret_cast<TUnversionedValue*>(header + 1);
    for (int index = 0; index < static_cast<int>(valueCount); ++index) {
        auto* value = values + index;
        current += ReadRowValue(current, value);
    }
    for (int index = valueCount; index < static_cast<int>(valueCount + nullCount); ++index) {
        values[index] = MakeUnversionedNullValue(index);
    }

    return TUnversionedOwningRow(std::move(rowData), data);
}

TUnversionedRow DeserializeFromString(const TString& data, const TRowBufferPtr& rowBuffer)
{
    if (data == SerializedNullRow) {
        return TUnversionedRow();
    }

    const char* current = data.data();

    ui32 version;
    current += ReadVarUint32(current, &version);
    YT_VERIFY(version == 0);

    ui32 valueCount;
    current += ReadVarUint32(current, &valueCount);

    auto row = rowBuffer->AllocateUnversioned(valueCount);

    auto* values = row.begin();
    for (int index = 0; index < static_cast<int>(valueCount); ++index) {
        auto* value = values + index;
        current += ReadRowValue(current, value);
        rowBuffer->CaptureValue(value);
    }

    return row;
}

////////////////////////////////////////////////////////////////////////////////

void TUnversionedRow::Save(TSaveContext& context) const
{
    NYT::Save(context, SerializeToString(*this));
}

void TUnversionedRow::Load(TLoadContext& context)
{
    *this = DeserializeFromString(NYT::Load<TString>(context), context.GetRowBuffer());
}

void ValidateValueType(
    const TUnversionedValue& value,
    const TTableSchema& schema,
    int schemaId,
    bool typeAnyAcceptsAllValues,
    bool ignoreRequired)
{
    ValidateValueType(value, schema.Columns()[schemaId], typeAnyAcceptsAllValues, ignoreRequired);
}

[[noreturn]] static void ThrowInvalidColumnType(EValueType expected, EValueType actual)
{
    THROW_ERROR_EXCEPTION(
        EErrorCode::SchemaViolation,
        "Invalid type, expected type %Qlv but got %Qlv",
        expected,
        actual);
}

static inline void ValidateColumnType(EValueType expected, const TUnversionedValue& value)
{
    if (value.Type != expected) {
        ThrowInvalidColumnType(expected, value.Type);
    }
}

template <ESimpleLogicalValueType logicalType>
Y_FORCE_INLINE auto GetValue(const TUnversionedValue& value)
{
    constexpr auto physicalType = GetPhysicalType(logicalType);
    ValidateColumnType(physicalType, value);
    if constexpr (physicalType == EValueType::Int64) {
        return value.Data.Int64;
    } else if constexpr (physicalType == EValueType::Uint64) {
        return value.Data.Uint64;
    } else if constexpr (physicalType == EValueType::Double) {
        return value.Data.Double;
    } else if constexpr (physicalType == EValueType::Boolean) {
        return value.Data.Boolean;
    } else {
        static_assert(physicalType == EValueType::String || physicalType == EValueType::Any);
        return value.AsStringBuf();
    }
}

static const TLogicalTypePtr& UnwrapTaggedAndOptional(const TLogicalTypePtr& type)
{
    const TLogicalTypePtr* current = &type;
    while ((*current)->GetMetatype() == ELogicalMetatype::Tagged) {
        current = &(*current)->UncheckedAsTaggedTypeRef().GetElement();
    }

    if ((*current)->GetMetatype() != ELogicalMetatype::Optional) {
        return *current;
    }

    const auto& optionalType = (*current)->UncheckedAsOptionalTypeRef();
    if (optionalType.IsElementNullable()) {
        return *current;
    }

    current = &optionalType.GetElement();

    while ((*current)->GetMetatype() == ELogicalMetatype::Tagged) {
        current = &(*current)->UncheckedAsTaggedTypeRef().GetElement();
    }

    return *current;
}

void ValidateValueType(
    const TUnversionedValue& value,
    const TColumnSchema& columnSchema,
    bool typeAnyAcceptsAllValues,
    bool ignoreRequired)
{
    if (value.Type == EValueType::Null) {
        if (columnSchema.Required()) {
            if (ignoreRequired) {
                return;
            }

            THROW_ERROR_EXCEPTION(
                EErrorCode::SchemaViolation,
                "Required column %Qv cannot have %Qlv value",
                columnSchema.Name(),
                value.Type);
        } else {
            return;
        }
    }

    try {
        auto v1Type = columnSchema.CastToV1Type();
        switch (v1Type) {
            case ESimpleLogicalValueType::Null:
            case ESimpleLogicalValueType::Void:
                // this case should be handled before
                ValidateColumnType(EValueType::Null, value);
                return;
            case ESimpleLogicalValueType::Any:
                if (columnSchema.IsOfV1Type()) {
                    if (!typeAnyAcceptsAllValues) {
                        ValidateColumnType(EValueType::Any, value);
                    }
                } else {
                    ValidateColumnType(EValueType::Composite, value);
                    ValidateComplexLogicalType(value.AsStringBuf(), columnSchema.LogicalType());
                }
                return;
            case ESimpleLogicalValueType::String:
                if (columnSchema.IsOfV1Type()) {
                    ValidateSimpleLogicalType<ESimpleLogicalValueType::String>(GetValue<ESimpleLogicalValueType::String>(value));
                } else {
                    ValidateColumnType(EValueType::String, value);
                    auto type = UnwrapTaggedAndOptional(columnSchema.LogicalType());
                    YT_VERIFY(type->GetMetatype() == ELogicalMetatype::Decimal);
                    NDecimal::TDecimal::ValidateBinaryValue(
                        value.AsStringBuf(),
                        type->UncheckedAsDecimalTypeRef().GetPrecision(),
                        type->UncheckedAsDecimalTypeRef().GetScale());
                }
                return;
#define CASE(x) \
        case x: \
            ValidateSimpleLogicalType<x>(GetValue<x>(value)); \
            return;

            CASE(ESimpleLogicalValueType::Int64)
            CASE(ESimpleLogicalValueType::Uint64)
            CASE(ESimpleLogicalValueType::Double)
            CASE(ESimpleLogicalValueType::Boolean)

            CASE(ESimpleLogicalValueType::Float)

            CASE(ESimpleLogicalValueType::Int8)
            CASE(ESimpleLogicalValueType::Int16)
            CASE(ESimpleLogicalValueType::Int32)

            CASE(ESimpleLogicalValueType::Uint8)
            CASE(ESimpleLogicalValueType::Uint16)
            CASE(ESimpleLogicalValueType::Uint32)

            CASE(ESimpleLogicalValueType::Utf8)
            CASE(ESimpleLogicalValueType::Date)
            CASE(ESimpleLogicalValueType::Datetime)
            CASE(ESimpleLogicalValueType::Timestamp)
            CASE(ESimpleLogicalValueType::Interval)
            CASE(ESimpleLogicalValueType::Json)
            CASE(ESimpleLogicalValueType::Uuid)
#undef CASE
        }
        YT_ABORT();
    } catch (const std::exception& ex) {
        THROW_ERROR_EXCEPTION(EErrorCode::SchemaViolation,
            "Error validating column %Qv",
            columnSchema.Name())
            << ex;
    }
}

void ValidateStaticValue(const TUnversionedValue& value)
{
    ValidateDataValueType(value.Type);
    if (IsStringLikeType(value.Type)) {
        if (value.Length > MaxRowWeightLimit) {
            THROW_ERROR_EXCEPTION("Value of type %Qlv is too long for static data: length %v, limit %v",
                value.Type,
                value.Length,
                MaxRowWeightLimit);
        }
    }
}

void ValidateDataValue(const TUnversionedValue& value)
{
    ValidateDataValueType(value.Type);
    ValidateDynamicValue(value, /*isKey*/ false);
}

void ValidateKeyValue(const TUnversionedValue& value)
{
    ValidateKeyValueType(value.Type);
    ValidateDynamicValue(value, /*isKey*/ true);
}

void ValidateRowValueCount(int count)
{
    if (count < 0) {
        THROW_ERROR_EXCEPTION("Negative number of values in row");
    }
    if (count > MaxValuesPerRow) {
        THROW_ERROR_EXCEPTION("Too many values in row: actual %v, limit %v",
            count,
            MaxValuesPerRow);
    }
}

void ValidateKeyColumnCount(int count)
{
    if (count < 0) {
        THROW_ERROR_EXCEPTION("Negative number of key columns");
    }
    if (count > MaxKeyColumnCount) {
        THROW_ERROR_EXCEPTION("Too many columns in key: actual %v, limit %v",
            count,
            MaxKeyColumnCount);
    }
}

void ValidateRowCount(int count)
{
    if (count < 0) {
        THROW_ERROR_EXCEPTION("Negative number of rows in rowset");
    }
    if (count > MaxRowsPerRowset) {
        THROW_ERROR_EXCEPTION("Too many rows in rowset: actual %v, limit %v",
            count,
            MaxRowsPerRowset);
    }
}

void ValidateClientDataRow(
    TUnversionedRow row,
    const TTableSchema& schema,
    const TNameTableToSchemaIdMapping& idMapping,
    const TNameTablePtr& nameTable,
    std::optional<int> tabletIndexColumnId)
{
    ValidateClientRow(row, schema, idMapping, nameTable, false, tabletIndexColumnId);
}

void ValidateDuplicateAndRequiredValueColumns(
    TUnversionedRow row,
    const TTableSchema& schema,
    const TNameTableToSchemaIdMapping& idMapping,
    std::vector<bool>* columnPresenceBuffer)
{
    auto& columnSeen = *columnPresenceBuffer;
    YT_VERIFY(std::ssize(columnSeen) >= schema.GetColumnCount());
    std::fill(columnSeen.begin(), columnSeen.end(), 0);

    for (const auto& value : row) {
        int mappedId = ApplyIdMapping(value, &idMapping);
        if (mappedId < 0) {
            continue;
        }
        const auto& column = schema.Columns()[mappedId];

        if (columnSeen[mappedId]) {
            THROW_ERROR_EXCEPTION("Duplicate column %Qv",
                column.Name());
        }
        columnSeen[mappedId] = true;
    }

    for (int index = schema.GetKeyColumnCount(); index < schema.GetColumnCount(); ++index) {
        if (!columnSeen[index] && schema.Columns()[index].Required()) {
            THROW_ERROR_EXCEPTION("Missing required column %Qv",
                schema.Columns()[index].Name());
        }
    }
}

void ValidateClientKey(TLegacyKey key)
{
    for (const auto& value : key) {
        ValidateKeyValue(value);
    }
}

void ValidateClientKey(
    TLegacyKey key,
    const TTableSchema& schema,
    const TNameTableToSchemaIdMapping& idMapping,
    const TNameTablePtr& nameTable)
{
    ValidateClientRow(key, schema, idMapping, nameTable, true);
}

void ValidateReadTimestamp(TTimestamp timestamp)
{
    if (timestamp != SyncLastCommittedTimestamp &&
        timestamp != AsyncLastCommittedTimestamp &&
        (timestamp < MinTimestamp || timestamp > MaxTimestamp))
    {
        THROW_ERROR_EXCEPTION("Invalid read timestamp %llx", timestamp);
    }
}

void ValidateGetInSyncReplicasTimestamp(TTimestamp timestamp)
{
    if (timestamp != SyncLastCommittedTimestamp &&
       (timestamp < MinTimestamp || timestamp > MaxTimestamp))
    {
        THROW_ERROR_EXCEPTION("Invalid GetInSyncReplicas timestamp %llx", timestamp);
    }
}

void ValidateWriteTimestamp(TTimestamp timestamp)
{
    if (timestamp < MinTimestamp || timestamp > MaxTimestamp) {
        THROW_ERROR_EXCEPTION("Invalid write timestamp %llx", timestamp);
    }
}

int ApplyIdMapping(
    const TUnversionedValue& value,
    const TNameTableToSchemaIdMapping* idMapping)
{
    auto valueId = value.Id;
    if (idMapping) {
        const auto& idMapping_ = *idMapping;
        if (valueId >= idMapping_.size()) {
            THROW_ERROR_EXCEPTION("Invalid column id during remapping: expected in range [0, %v), got %v",
                idMapping_.size(),
                valueId);
        }
        return idMapping_[valueId];
    } else {
        return valueId;
    }
}

////////////////////////////////////////////////////////////////////////////////

TLegacyOwningKey GetKeySuccessorImpl(TLegacyKey key, ui32 prefixLength, EValueType sentinelType)
{
    auto length = std::min(prefixLength, key.GetCount());
    TUnversionedOwningRowBuilder builder(length + 1);
    for (int index = 0; index < static_cast<int>(length); ++index) {
        builder.AddValue(key[index]);
    }
    builder.AddValue(MakeUnversionedSentinelValue(sentinelType));
    return builder.FinishRow();
}

TLegacyKey GetKeySuccessorImpl(TLegacyKey key, ui32 prefixLength, EValueType sentinelType, const TRowBufferPtr& rowBuffer)
{
    auto length = std::min(prefixLength, key.GetCount());
    auto result = rowBuffer->AllocateUnversioned(length + 1);
    for (int index = 0; index < static_cast<int>(length); ++index) {
        result[index] = rowBuffer->CaptureValue(key[index]);
    }
    result[length] = MakeUnversionedSentinelValue(sentinelType);
    return result;
}

TLegacyOwningKey GetKeySuccessor(TLegacyKey key)
{
    return GetKeySuccessorImpl(
        key,
        key.GetCount(),
        EValueType::Min);
}

TLegacyKey GetKeySuccessor(TLegacyKey key, const TRowBufferPtr& rowBuffer)
{
    return GetKeySuccessorImpl(
        key,
        key.GetCount(),
        EValueType::Min,
        rowBuffer);
}

TLegacyOwningKey GetKeyPrefixSuccessor(TLegacyKey key, ui32 prefixLength)
{
    return GetKeySuccessorImpl(
        key,
        prefixLength,
        EValueType::Max);
}

TLegacyKey GetKeyPrefixSuccessor(TLegacyKey key, ui32 prefixLength, const TRowBufferPtr& rowBuffer)
{
    return GetKeySuccessorImpl(
        key,
        prefixLength,
        EValueType::Max,
        rowBuffer);
}

TLegacyOwningKey GetKeyPrefix(TLegacyKey key, ui32 prefixLength)
{
    return TLegacyOwningKey(
        key.Begin(),
        key.Begin() + std::min(key.GetCount(), prefixLength));
}

TLegacyKey GetKeyPrefix(TLegacyKey key, ui32 prefixLength, const TRowBufferPtr& rowBuffer)
{
    return rowBuffer->CaptureRow(MakeRange(
        key.Begin(),
        std::min(key.GetCount(), prefixLength)));
}

TLegacyKey GetStrictKey(TLegacyKey key, ui32 keyColumnCount, const TRowBufferPtr& rowBuffer, EValueType sentinelType)
{
    if (key.GetCount() > keyColumnCount) {
        return GetKeyPrefix(key, keyColumnCount, rowBuffer);
    } else {
        return WidenKey(key, keyColumnCount, rowBuffer, sentinelType);
    }
}

TLegacyKey GetStrictKeySuccessor(TLegacyKey key, ui32 keyColumnCount, const TRowBufferPtr& rowBuffer, EValueType sentinelType)
{
    if (key.GetCount() >= keyColumnCount) {
        return GetKeyPrefixSuccessor(key, keyColumnCount, rowBuffer);
    } else {
        return WidenKeySuccessor(key, keyColumnCount, rowBuffer, sentinelType);
    }
}

////////////////////////////////////////////////////////////////////////////////

static TLegacyOwningKey MakeSentinelKey(EValueType type)
{
    TUnversionedOwningRowBuilder builder;
    builder.AddValue(MakeUnversionedSentinelValue(type));
    return builder.FinishRow();
}

static const TLegacyOwningKey CachedMinKey = MakeSentinelKey(EValueType::Min);
static const TLegacyOwningKey CachedMaxKey = MakeSentinelKey(EValueType::Max);

const TLegacyOwningKey MinKey()
{
    return CachedMinKey;
}

const TLegacyOwningKey MaxKey()
{
    return CachedMaxKey;
}

static TLegacyOwningKey MakeEmptyKey()
{
    TUnversionedOwningRowBuilder builder;
    return builder.FinishRow();
}

static const TLegacyOwningKey CachedEmptyKey = MakeEmptyKey();

const TLegacyOwningKey EmptyKey()
{
    return CachedEmptyKey;
}

const TLegacyOwningKey& ChooseMinKey(const TLegacyOwningKey& a, const TLegacyOwningKey& b)
{
    int result = CompareRows(a, b);
    return result <= 0 ? a : b;
}

const TLegacyOwningKey& ChooseMaxKey(const TLegacyOwningKey& a, const TLegacyOwningKey& b)
{
    int result = CompareRows(a, b);
    return result >= 0 ? a : b;
}

void ToProto(TProtoStringType* protoRow, TUnversionedRow row)
{
    *protoRow = SerializeToString(row);
}

void ToProto(TProtoStringType* protoRow, const TUnversionedOwningRow& row)
{
    ToProto(protoRow, row.Get());
}

void ToProto(TProtoStringType* protoRow, const TUnversionedValue* begin, const TUnversionedValue* end)
{
    *protoRow = SerializeToString(begin, end);
}

void FromProto(TUnversionedOwningRow* row, const TProtoStringType& protoRow, std::optional<int> nullPaddingWidth)
{
    *row = DeserializeFromString(protoRow, nullPaddingWidth);
}

void FromProto(TUnversionedRow* row, const TProtoStringType& protoRow, const TRowBufferPtr& rowBuffer)
{
    if (protoRow == SerializedNullRow) {
        *row = TUnversionedRow();
        return;
    }

    const char* current = protoRow.data();

    ui32 version;
    current += ReadVarUint32(current, &version);
    YT_VERIFY(version == 0);

    ui32 valueCount;
    current += ReadVarUint32(current, &valueCount);

    auto mutableRow = rowBuffer->AllocateUnversioned(valueCount);
    *row = mutableRow;

    auto* values = mutableRow.Begin();
    for (auto* value = values; value < values + valueCount; ++value) {
        current += ReadRowValue(current, value);
        rowBuffer->CaptureValue(value);
    }
}

void ToBytes(TString* bytes, const TUnversionedOwningRow& row)
{
    *bytes = SerializeToString(row);
}

void FromBytes(TUnversionedOwningRow* row, TStringBuf bytes)
{
    *row = DeserializeFromString(TString(bytes));
}

void PrintTo(const TUnversionedOwningRow& key, ::std::ostream* os)
{
    *os << KeyToYson(key);
}

void PrintTo(const TUnversionedRow& value, ::std::ostream* os)
{
    *os << ToString(value);
}

TLegacyOwningKey RowToKey(
    const TTableSchema& schema,
    TUnversionedRow row)
{
    TUnversionedOwningRowBuilder builder;
    for (int index = 0; index < schema.GetKeyColumnCount(); ++index) {
        builder.AddValue(row[index]);
    }
    return builder.FinishRow();
}

namespace {

template <class TRow>
std::pair<TSharedRange<TUnversionedRow>, i64> CaptureRowsImpl(
    TRange<TRow> rows,
    TRefCountedTypeCookie tagCookie)
{
    size_t bufferSize = 0;
    bufferSize += sizeof (TUnversionedRow) * rows.Size();
    for (auto row : rows) {
        bufferSize += GetUnversionedRowByteSize(row.GetCount());
        for (const auto& value : row) {
            if (IsStringLikeType(value.Type)) {
                bufferSize += value.Length;
            }
        }
    }
    auto buffer = TSharedMutableRef::Allocate(bufferSize, false, tagCookie);

    char* alignedPtr = buffer.Begin();
    auto allocateAligned = [&] (size_t size) {
        auto* result = alignedPtr;
        alignedPtr += size;
        return result;
    };

    char* unalignedPtr = buffer.End();
    auto allocateUnaligned = [&] (size_t size) {
        unalignedPtr -= size;
        return unalignedPtr;
    };

    auto* capturedRows = reinterpret_cast<TUnversionedRow*>(allocateAligned(sizeof (TUnversionedRow) * rows.Size()));
    for (size_t index = 0; index < rows.Size(); ++index) {
        auto row = rows[index];
        int valueCount = row.GetCount();
        auto* capturedHeader = reinterpret_cast<TUnversionedRowHeader*>(allocateAligned(GetUnversionedRowByteSize(valueCount)));
        capturedHeader->Capacity = valueCount;
        capturedHeader->Count = valueCount;
        auto capturedRow = TMutableUnversionedRow(capturedHeader);
        capturedRows[index] = capturedRow;
        ::memcpy(capturedRow.Begin(), row.Begin(), sizeof (TUnversionedValue) * row.GetCount());
        for (auto& capturedValue : capturedRow) {
            if (IsStringLikeType(capturedValue.Type)) {
                auto* capturedString = allocateUnaligned(capturedValue.Length);
                ::memcpy(capturedString, capturedValue.Data.String, capturedValue.Length);
                capturedValue.Data.String = capturedString;
            }
        }
    }

    YT_VERIFY(alignedPtr == unalignedPtr);

    return {
        MakeSharedRange(
            MakeRange(capturedRows, rows.Size()),
            std::move(buffer.ReleaseHolder())),
        bufferSize
    };
}

} // namespace

std::pair<TSharedRange<TUnversionedRow>, i64> CaptureRows(
    TRange<TUnversionedRow> rows,
    TRefCountedTypeCookie tagCookie)
{
    return CaptureRowsImpl(rows, tagCookie);
}

std::pair<TSharedRange<TUnversionedRow>, i64> CaptureRows(
    TRange<TUnversionedOwningRow> rows,
    TRefCountedTypeCookie tagCookie)
{
    return CaptureRowsImpl(rows, tagCookie);
}

void Serialize(const TUnversionedValue& value, IYsonConsumer* consumer, bool anyAsRaw)
{
    auto type = value.Type;
    switch (type) {
        case EValueType::Int64:
            consumer->OnInt64Scalar(value.Data.Int64);
            break;

        case EValueType::Uint64:
            consumer->OnUint64Scalar(value.Data.Uint64);
            break;

        case EValueType::Double:
            consumer->OnDoubleScalar(value.Data.Double);
            break;

        case EValueType::Boolean:
            consumer->OnBooleanScalar(value.Data.Boolean);
            break;

        case EValueType::String:
            consumer->OnStringScalar(value.AsStringBuf());
            break;

        case EValueType::Any:
            if (anyAsRaw) {
                consumer->OnRaw(value.AsStringBuf(), EYsonType::Node);
            } else {
                ParseYsonStringBuffer(value.AsStringBuf(), EYsonType::Node, consumer);
            }
            break;

        case EValueType::Null:
            consumer->OnEntity();
            break;

        case EValueType::Composite:
            consumer->OnBeginAttributes();
            consumer->OnKeyedItem("type");
            consumer->OnStringScalar(FormatEnum(type));
            consumer->OnEndAttributes();
            if (anyAsRaw) {
                consumer->OnRaw(value.AsStringBuf(), EYsonType::Node);
            } else {
                ParseYsonStringBuffer(value.AsStringBuf(), EYsonType::Node, consumer);
            }
            break;

        default:
            consumer->OnBeginAttributes();
            consumer->OnKeyedItem("type");
            consumer->OnStringScalar(FormatEnum(type));
            consumer->OnEndAttributes();
            consumer->OnEntity();
            break;
    }
}

void Serialize(TLegacyKey key, IYsonConsumer* consumer)
{
    consumer->OnBeginList();
    for (const auto& value : key) {
        consumer->OnListItem();
        Serialize(value, consumer);
    }
    consumer->OnEndList();
}

void Serialize(const TLegacyOwningKey& key, IYsonConsumer* consumer)
{
    return Serialize(key.Get(), consumer);
}

void Deserialize(TLegacyOwningKey& key, INodePtr node)
{
    if (node->GetType() != ENodeType::List) {
        THROW_ERROR_EXCEPTION("Key cannot be parsed from %Qlv",
            node->GetType());
    }

    TUnversionedOwningRowBuilder builder;
    int id = 0;
    for (const auto& item : node->AsList()->GetChildren()) {
        try {
            switch (item->GetType()) {
                case ENodeType::Int64:
                    builder.AddValue(MakeUnversionedInt64Value(item->GetValue<i64>(), id));
                    break;

                case ENodeType::Uint64:
                    builder.AddValue(MakeUnversionedUint64Value(item->GetValue<ui64>(), id));
                    break;

                case ENodeType::Double:
                    builder.AddValue(MakeUnversionedDoubleValue(item->GetValue<double>(), id));
                    break;

                case ENodeType::Boolean:
                    builder.AddValue(MakeUnversionedBooleanValue(item->GetValue<bool>(), id));
                    break;

                case ENodeType::String:
                    builder.AddValue(MakeUnversionedStringValue(item->GetValue<TString>(), id));
                    break;

                case ENodeType::Entity: {
                    auto valueType = item->Attributes().Get<EValueType>("type", EValueType::Null);
                    if (valueType != EValueType::Null && !IsSentinelType(valueType)) {
                        THROW_ERROR_EXCEPTION("Entities can only represent %Qlv and sentinel values but "
                            "not values of type %Qlv",
                            EValueType::Null,
                            valueType);
                    }
                    builder.AddValue(MakeUnversionedSentinelValue(valueType, id));
                    break;
                }

                default:
                    THROW_ERROR_EXCEPTION("Key cannot contain %Qlv values",
                        item->GetType());
            }
        } catch (const std::exception& ex) {
            THROW_ERROR_EXCEPTION("Error deserializing key component #%v", id)
                << ex;
        }
        ++id;
    }
    key = builder.FinishRow();
}

void Deserialize(TLegacyOwningKey& key, TYsonPullParserCursor* cursor)
{
    // TODO(levysotsky): Speed up?
    Deserialize(key, ExtractTo<INodePtr>(cursor));
}

void TUnversionedOwningRow::Save(TStreamSaveContext& context) const
{
    NYT::Save(context, SerializeToString(Get()));
}

void TUnversionedOwningRow::Load(TStreamLoadContext& context)
{
    *this = DeserializeFromString(NYT::Load<TString>(context));
}

////////////////////////////////////////////////////////////////////////////////

TUnversionedRowBuilder::TUnversionedRowBuilder(int initialValueCapacity /*= 16*/)
{
    RowData_.resize(GetUnversionedRowByteSize(initialValueCapacity));
    Reset();
    GetHeader()->Capacity = initialValueCapacity;
}

int TUnversionedRowBuilder::AddValue(const TUnversionedValue& value)
{
    auto* header = GetHeader();
    if (header->Count == header->Capacity) {
        auto valueCapacity = 2 * std::max(1U, header->Capacity);
        RowData_.resize(GetUnversionedRowByteSize(valueCapacity));
        header = GetHeader();
        header->Capacity = valueCapacity;
    }

    *GetValue(header->Count) = value;
    return header->Count++;
}

TMutableUnversionedRow TUnversionedRowBuilder::GetRow()
{
    return TMutableUnversionedRow(GetHeader());
}

void TUnversionedRowBuilder::Reset()
{
    auto* header = GetHeader();
    header->Count = 0;
}

TUnversionedRowHeader* TUnversionedRowBuilder::GetHeader()
{
    return reinterpret_cast<TUnversionedRowHeader*>(RowData_.data());
}

TUnversionedValue* TUnversionedRowBuilder::GetValue(ui32 index)
{
    return reinterpret_cast<TUnversionedValue*>(GetHeader() + 1) + index;
}

////////////////////////////////////////////////////////////////////////////////

TUnversionedOwningRowBuilder::TUnversionedOwningRowBuilder(int initialValueCapacity /*= 16*/)
    : InitialValueCapacity_(initialValueCapacity)
    , RowData_(TOwningRowTag())
{
    Reset();
}

int TUnversionedOwningRowBuilder::AddValue(const TUnversionedValue& value)
{
    auto* header = GetHeader();
    if (header->Count == header->Capacity) {
        auto valueCapacity = 2 * std::max(1U, header->Capacity);
        RowData_.Resize(GetUnversionedRowByteSize(valueCapacity));
        header = GetHeader();
        header->Capacity = valueCapacity;
    }

    auto* newValue = GetValue(header->Count);
    *newValue = value;

    if (IsStringLikeType(value.Type)) {
        const char* oldStringDataPtr = StringData_.data();
        auto oldStringDataLength = StringData_.length();
        StringData_.append(value.Data.String, value.Data.String + value.Length);
        const char* newStringDataPtr = StringData_.data();
        newValue->Data.String = newStringDataPtr + oldStringDataLength;
        if (newStringDataPtr != oldStringDataPtr) {
            for (int index = 0; index < static_cast<int>(header->Count); ++index) {
                auto* existingValue = GetValue(index);
                if (IsStringLikeType(existingValue->Type)) {
                    existingValue->Data.String = newStringDataPtr + (existingValue->Data.String - oldStringDataPtr);
                }
            }
        }
    }

    return header->Count++;
}

TUnversionedValue* TUnversionedOwningRowBuilder::BeginValues()
{
    return reinterpret_cast<TUnversionedValue*>(GetHeader() + 1);
}

TUnversionedValue* TUnversionedOwningRowBuilder::EndValues()
{
    return BeginValues() + GetHeader()->Count;
}

TUnversionedOwningRow TUnversionedOwningRowBuilder::FinishRow()
{
    auto row = TUnversionedOwningRow(
        TSharedMutableRef::FromBlob(std::move(RowData_)),
        std::move(StringData_));
    Reset();
    return row;
}

void TUnversionedOwningRowBuilder::Reset()
{
    RowData_.Resize(GetUnversionedRowByteSize(InitialValueCapacity_));

    auto* header = GetHeader();
    header->Count = 0;
    header->Capacity = InitialValueCapacity_;
}

TUnversionedRowHeader* TUnversionedOwningRowBuilder::GetHeader()
{
    return reinterpret_cast<TUnversionedRowHeader*>(RowData_.Begin());
}

TUnversionedValue* TUnversionedOwningRowBuilder::GetValue(ui32 index)
{
    return reinterpret_cast<TUnversionedValue*>(GetHeader() + 1) + index;
}

////////////////////////////////////////////////////////////////////////////////

void TUnversionedOwningRow::Init(const TUnversionedValue* begin, const TUnversionedValue* end)
{
    int count = std::distance(begin, end);

    size_t fixedSize = GetUnversionedRowByteSize(count);
    RowData_ = TSharedMutableRef::Allocate<TOwningRowTag>(fixedSize, false);
    auto* header = GetHeader();

    header->Count = count;
    header->Capacity = count;
    ::memcpy(header + 1, begin, reinterpret_cast<const char*>(end) - reinterpret_cast<const char*>(begin));

    size_t variableSize = 0;
    for (auto it = begin; it != end; ++it) {
        const auto& otherValue = *it;
        if (IsStringLikeType(otherValue.Type)) {
            variableSize += otherValue.Length;
        }
    }

    if (variableSize > 0) {
        StringData_.resize(variableSize);
        char* current = const_cast<char*>(StringData_.data());

        for (int index = 0; index < count; ++index) {
            const auto& otherValue = begin[index];
            auto& value = reinterpret_cast<TUnversionedValue*>(header + 1)[index];
            if (IsStringLikeType(otherValue.Type)) {
                ::memcpy(current, otherValue.Data.String, otherValue.Length);
                value.Data.String = current;
                current += otherValue.Length;
            }
        }
    }
}

////////////////////////////////////////////////////////////////////////////////

TLegacyOwningKey WidenKey(const TLegacyOwningKey& key, ui32 keyColumnCount, EValueType sentinelType)
{
    return WidenKeyPrefix(key, key.GetCount(), keyColumnCount, sentinelType);
}

TLegacyKey WidenKey(const TLegacyKey& key, ui32 keyColumnCount, const TRowBufferPtr& rowBuffer, EValueType sentinelType)
{
    return WidenKeyPrefix(key, key.GetCount(), keyColumnCount, rowBuffer, sentinelType);
}

TLegacyOwningKey WidenKeySuccessor(const TLegacyOwningKey& key, ui32 keyColumnCount, EValueType sentinelType)
{
    YT_VERIFY(static_cast<int>(keyColumnCount) >= key.GetCount());

    TUnversionedOwningRowBuilder builder;
    for (int index = 0; index < key.GetCount(); ++index) {
        builder.AddValue(key[index]);
    }

    for (ui32 index = key.GetCount(); index < keyColumnCount; ++index) {
        builder.AddValue(MakeUnversionedSentinelValue(sentinelType));
    }

    builder.AddValue(MakeUnversionedSentinelValue(EValueType::Max));

    return builder.FinishRow();
}

TLegacyKey WidenKeySuccessor(const TLegacyKey& key, ui32 keyColumnCount, const TRowBufferPtr& rowBuffer, EValueType sentinelType)
{
    YT_VERIFY(keyColumnCount >= key.GetCount());

    auto wideKey = rowBuffer->AllocateUnversioned(keyColumnCount + 1);

    for (ui32 index = 0; index < key.GetCount(); ++index) {
        wideKey[index] = rowBuffer->CaptureValue(key[index]);
    }

    for (ui32 index = key.GetCount(); index < keyColumnCount; ++index) {
        wideKey[index] = MakeUnversionedSentinelValue(sentinelType);
    }

    wideKey[keyColumnCount] = MakeUnversionedSentinelValue(EValueType::Max);

    return wideKey;
}

TLegacyOwningKey WidenKeyPrefix(const TLegacyOwningKey& key, ui32 prefixLength, ui32 keyColumnCount, EValueType sentinelType)
{
    YT_VERIFY(static_cast<int>(prefixLength) <= key.GetCount() && prefixLength <= keyColumnCount);

    if (key.GetCount() == static_cast<int>(prefixLength) && prefixLength == keyColumnCount) {
        return key;
    }

    TUnversionedOwningRowBuilder builder;
    for (ui32 index = 0; index < prefixLength; ++index) {
        builder.AddValue(key[index]);
    }

    for (ui32 index = prefixLength; index < keyColumnCount; ++index) {
        builder.AddValue(MakeUnversionedSentinelValue(sentinelType));
    }

    return builder.FinishRow();
}

TLegacyKey WidenKeyPrefix(TLegacyKey key, ui32 prefixLength, ui32 keyColumnCount, const TRowBufferPtr& rowBuffer, EValueType sentinelType)
{
    YT_VERIFY(prefixLength <= key.GetCount() && prefixLength <= keyColumnCount);

    if (key.GetCount() == prefixLength && prefixLength == keyColumnCount) {
        return rowBuffer->CaptureRow(key);
    }

    auto wideKey = rowBuffer->AllocateUnversioned(keyColumnCount);

    for (ui32 index = 0; index < prefixLength; ++index) {
        wideKey[index] = rowBuffer->CaptureValue(key[index]);
    }

    for (ui32 index = prefixLength; index < keyColumnCount; ++index) {
        wideKey[index] = MakeUnversionedSentinelValue(sentinelType);
    }

    return wideKey;
}

////////////////////////////////////////////////////////////////////////////////

TSharedRange<TRowRange> MakeSingletonRowRange(TLegacyKey lowerBound, TLegacyKey upperBound)
{
    auto rowBuffer = New<TRowBuffer>();
    TCompactVector<TRowRange, 1> ranges(1, TRowRange(
        rowBuffer->CaptureRow(lowerBound),
        rowBuffer->CaptureRow(upperBound)));
    return MakeSharedRange(
        std::move(ranges),
        std::move(rowBuffer));
}

////////////////////////////////////////////////////////////////////////////////

TRange<TUnversionedValue> ToKeyRef(TUnversionedRow row)
{
    return MakeRange(row.Begin(), row.End());
}

TRange<TUnversionedValue> ToKeyRef(TUnversionedRow row, int prefix)
{
    YT_VERIFY(prefix <= static_cast<int>(row.GetCount()));
    return MakeRange(row.Begin(), prefix);
}

////////////////////////////////////////////////////////////////////////////////

void FormatValue(TStringBuilderBase* builder, TUnversionedRow row, TStringBuf format)
{
    if (row) {
        builder->AppendChar('[');
        JoinToString(
            builder,
            row.Begin(),
            row.End(),
            [&] (TStringBuilderBase* builder, const TUnversionedValue& value) {
                FormatValue(builder, value, format);
            });
        builder->AppendChar(']');
    } else {
        builder->AppendString("<null>");
    }
}

void FormatValue(TStringBuilderBase* builder, TMutableUnversionedRow row, TStringBuf format)
{
    FormatValue(builder, TUnversionedRow(row), format);
}

void FormatValue(TStringBuilderBase* builder, const TUnversionedOwningRow& row, TStringBuf format)
{
    FormatValue(builder, TUnversionedRow(row), format);
}

TString ToString(TUnversionedRow row, bool valuesOnly)
{
    return ToStringViaBuilder(row, valuesOnly ? "k" : "");
}

TString ToString(TMutableUnversionedRow row, bool valuesOnly)
{
    return ToString(TUnversionedRow(row), valuesOnly);
}

TString ToString(const TUnversionedOwningRow& row, bool valuesOnly)
{
    return ToString(row.Get(), valuesOnly);
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NTableClient
