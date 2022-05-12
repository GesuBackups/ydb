#include "schema.h"

#include "column_sort_schema.h"
#include "comparator.h"
#include "logical_type.h"
#include "unversioned_row.h"

#include <yt/yt/client/tablet_client/public.h>

#include <yt/yt/core/yson/public.h>
#include <yt/yt/core/yson/pull_parser_deserialize.h>

#include <yt/yt/core/ytree/serialize.h>
#include <yt/yt/core/ytree/convert.h>
#include <yt/yt/core/ytree/fluent.h>
#include <yt/yt/core/ytree/yson_struct.h>

#include <yt/yt_proto/yt/client/table_chunk_format/proto/chunk_meta.pb.h>
#include <yt/yt_proto/yt/client/table_chunk_format/proto/wire_protocol.pb.h>

namespace NYT::NTableClient {

using namespace NYTree;
using namespace NYson;
using namespace NChunkClient;
using namespace NTabletClient;

using NYT::ToProto;
using NYT::FromProto;

/////////////////////////////////////////////////////////////////////////////

namespace {

int GetLockPriority(ELockType lockType)
{
    switch (lockType) {
        case ELockType::None:
            return 0;
        case ELockType::SharedWeak:
            return 1;
        case ELockType::SharedStrong:
            return 2;
        case ELockType::Exclusive:
            return 3;
        default:
            YT_ABORT();
    }
}

} // namespace

////////////////////////////////////////////////////////////////////////////////

ELockType GetStrongestLock(ELockType lhs, ELockType rhs)
{
    return GetLockPriority(lhs) > GetLockPriority(rhs) ? lhs : rhs;
}

////////////////////////////////////////////////////////////////////////////////

TLockMask MaxMask(TLockMask lhs, TLockMask rhs)
{
    int size = std::max<int>(lhs.GetSize(), rhs.GetSize());
    for (int index = 0; index < size; ++index) {
        lhs.Set(index, GetStrongestLock(lhs.Get(index), rhs.Get(index)));
    }

    return lhs;
}

////////////////////////////////////////////////////////////////////////////////

TColumnSchema::TColumnSchema()
    : TColumnSchema(
        TString(),
        NullLogicalType(),
        std::nullopt)
{ }

TColumnSchema::TColumnSchema(
    TString name,
    EValueType type,
    std::optional<ESortOrder> sortOrder)
    : TColumnSchema(
        std::move(name),
        MakeLogicalType(GetLogicalType(type), /*required*/ false),
        sortOrder)
{ }

TColumnSchema::TColumnSchema(
    TString name,
    ESimpleLogicalValueType type,
    std::optional<ESortOrder> sortOrder)
    : TColumnSchema(
        std::move(name),
        MakeLogicalType(type, /*required*/ false),
        sortOrder)
{ }

TColumnSchema::TColumnSchema(
    TString name,
    TLogicalTypePtr type,
    std::optional<ESortOrder> sortOrder)
    : Name_(std::move(name))
    , SortOrder_(sortOrder)
{
    SetLogicalType(std::move(type));
}

TColumnSchema& TColumnSchema::SetName(TString value)
{
    Name_ = std::move(value);
    return *this;
}

TColumnSchema& TColumnSchema::SetSortOrder(std::optional<ESortOrder> value)
{
    SortOrder_ = value;
    return *this;
}

TColumnSchema& TColumnSchema::SetLock(std::optional<TString> value)
{
    Lock_ = std::move(value);
    return *this;
}

TColumnSchema& TColumnSchema::SetGroup(std::optional<TString> value)
{
    Group_ = std::move(value);
    return *this;
}

TColumnSchema& TColumnSchema::SetExpression(std::optional<TString> value)
{
    Expression_ = std::move(value);
    return *this;
}

TColumnSchema& TColumnSchema::SetAggregate(std::optional<TString> value)
{
    Aggregate_ = std::move(value);
    return *this;
}

TColumnSchema& TColumnSchema::SetRequired(bool value)
{
    Required_ = value;
    return *this;
}

TColumnSchema& TColumnSchema::SetMaxInlineHunkSize(std::optional<i64> value)
{
    MaxInlineHunkSize_ = value;
    return *this;
}

TColumnSchema& TColumnSchema::SetLogicalType(TLogicalTypePtr type)
{
    LogicalType_ = std::move(type);
    IsOfV1Type_ = IsV1Type(LogicalType_);
    std::tie(V1Type_, Required_) = NTableClient::CastToV1Type(LogicalType_);
    return *this;
}

EValueType TColumnSchema::GetWireType() const
{
    return NTableClient::GetWireType(LogicalType_);
}

i64 TColumnSchema::GetMemoryUsage() const
{
    return
        sizeof(TColumnSchema) +
        Name_.size() +
        (LogicalType_ ? LogicalType_->GetMemoryUsage() : 0) +
        (Lock_ ? Lock_->size() : 0) +
        (Expression_ ? Expression_->size() : 0) +
        (Aggregate_ ? Aggregate_->size() : 0) +
        (Group_ ? Group_->size() : 0);
}

bool TColumnSchema::IsOfV1Type() const
{
    return IsOfV1Type_;
}

bool TColumnSchema::IsOfV1Type(ESimpleLogicalValueType type) const
{
    return IsOfV1Type_ && V1Type_ == type;
}

ESimpleLogicalValueType TColumnSchema::CastToV1Type() const
{
    return V1Type_;
}

////////////////////////////////////////////////////////////////////////////////

static void RunColumnSchemaPostprocessor(
    TColumnSchema& schema,
    std::optional<ESimpleLogicalValueType> logicalTypeV1,
    std::optional<bool> requiredV1,
    const TLogicalTypePtr& logicalTypeV3)
{
    // Name
    if (schema.Name().empty()) {
        THROW_ERROR_EXCEPTION("Column name cannot be empty");
    }

    try {
        int setTypeVersion = 0;
        if (logicalTypeV3) {
            schema.SetLogicalType(logicalTypeV3);
            setTypeVersion = 3;
        }

        if (logicalTypeV1) {
            if (setTypeVersion == 0) {
                schema.SetLogicalType(MakeLogicalType(*logicalTypeV1, requiredV1.value_or(false)));
                setTypeVersion = 1;
            } else {
                if (*logicalTypeV1 != schema.CastToV1Type()) {
                    THROW_ERROR_EXCEPTION(
                        "\"type_v%v\" does not match \"type\"; \"type_v%v\": %Qv \"type\": %Qlv expected \"type\": %Qlv",
                        setTypeVersion,
                        setTypeVersion,
                        *schema.LogicalType(),
                        *logicalTypeV1,
                        schema.CastToV1Type());
                }
            }
        }

        if (requiredV1 && setTypeVersion > 1 && *requiredV1 != schema.Required()) {
            THROW_ERROR_EXCEPTION(
                "\"type_v%v\" does not match \"required\"; \"type_v%v\": %Qv \"required\": %Qlv",
                setTypeVersion,
                setTypeVersion,
                *schema.LogicalType(),
                *requiredV1);
        }

        if (setTypeVersion == 0) {
            THROW_ERROR_EXCEPTION("Column type is not specified");
        }

        if (*DetagLogicalType(schema.LogicalType()) == *SimpleLogicalType(ESimpleLogicalValueType::Any)) {
            THROW_ERROR_EXCEPTION("Column of type %Qlv cannot be \"required\"",
                ESimpleLogicalValueType::Any);
        }

        // Lock
        if (schema.Lock() && schema.Lock()->empty()) {
            THROW_ERROR_EXCEPTION("Lock name cannot be empty");
        }

        // Group
        if (schema.Group() && schema.Group()->empty()) {
            THROW_ERROR_EXCEPTION("Group name cannot be empty");
        }
    } catch (const std::exception& ex) {
        THROW_ERROR_EXCEPTION("Error validating column %Qv in table schema",
            schema.Name())
            << ex;
    }
}

struct TSerializableColumnSchema
    : public TYsonStructLite
    , private TColumnSchema
{
    REGISTER_YSON_STRUCT_LITE(TSerializableColumnSchema);

    static void Register(TRegistrar registrar)
    {
        registrar.BaseClassParameter("name", &TSerializableColumnSchema::Name_)
            .NonEmpty();
        registrar.Parameter("type", &TThis::LogicalTypeV1_)
            .Default(std::nullopt);
        registrar.Parameter("required", &TThis::RequiredV1_)
            .Default(std::nullopt);
        registrar.Parameter("type_v3", &TSerializableColumnSchema::LogicalTypeV3_)
            .Default();
        registrar.BaseClassParameter("lock", &TSerializableColumnSchema::Lock_)
            .Default();
        registrar.BaseClassParameter("expression", &TSerializableColumnSchema::Expression_)
            .Default();
        registrar.BaseClassParameter("aggregate", &TSerializableColumnSchema::Aggregate_)
            .Default();
        registrar.BaseClassParameter("sort_order", &TSerializableColumnSchema::SortOrder_)
            .Default();
        registrar.BaseClassParameter("group", &TSerializableColumnSchema::Group_)
            .Default();
        registrar.BaseClassParameter("max_inline_hunk_size", &TSerializableColumnSchema::MaxInlineHunkSize_)
            .Default();

        registrar.Postprocessor([] (TSerializableColumnSchema* schema) {
            RunColumnSchemaPostprocessor(
                *schema,
                schema->LogicalTypeV1_,
                schema->RequiredV1_,
                schema->LogicalTypeV3_ ? schema->LogicalTypeV3_->LogicalType : nullptr);
        });
    }

public:
    void SetColumnSchema(const TColumnSchema& columnSchema)
    {
        static_cast<TColumnSchema&>(*this) = columnSchema;
        LogicalTypeV1_ = columnSchema.CastToV1Type();
        RequiredV1_ = columnSchema.Required();
        LogicalTypeV3_ = TTypeV3LogicalTypeWrapper{columnSchema.LogicalType()};
    }

    const TColumnSchema& GetColumnSchema() const
    {
        return *this;
    }

private:
    std::optional<ESimpleLogicalValueType> LogicalTypeV1_;
    std::optional<bool> RequiredV1_;

    std::optional<TTypeV3LogicalTypeWrapper> LogicalTypeV3_;
};

void FormatValue(TStringBuilderBase* builder, const TColumnSchema& schema, TStringBuf /*spec*/)
{
    builder->AppendChar('{');

    builder->AppendFormat("name=%Qv", schema.Name());

    if (const auto& logicalType = schema.LogicalType()) {
        builder->AppendFormat("; type=%v", *logicalType);
    }

    if (const auto& sortOrder = schema.SortOrder()) {
        builder->AppendFormat("; sort_order=%v", *sortOrder);
    }

    if (const auto& lock = schema.Lock()) {
        builder->AppendFormat("; lock=%v", *lock);
    }

    if (const auto& expression = schema.Expression()) {
        builder->AppendFormat("; expression=%Qv", *expression);
    }

    if (const auto& aggregate = schema.Aggregate()) {
        builder->AppendFormat("; aggregate=%v", *aggregate);
    }

    if (const auto& group = schema.Group()) {
        builder->AppendFormat("; group=%v", *group);
    }

    builder->AppendFormat("; physical_type=%v", CamelCaseToUnderscoreCase(ToString(schema.CastToV1Type())));

    builder->AppendFormat("; required=%v", schema.Required());

    if (auto maxInlineHunkSize = schema.MaxInlineHunkSize()) {
        builder->AppendFormat("; max_inline_hunk_size=%v", *maxInlineHunkSize);
    }

    builder->AppendChar('}');
}

void Serialize(const TColumnSchema& schema, IYsonConsumer* consumer)
{
    TSerializableColumnSchema wrapper = TSerializableColumnSchema::Create();
    wrapper.SetColumnSchema(schema);
    Serialize(static_cast<const TYsonStructLite&>(wrapper), consumer);
}

void Deserialize(TColumnSchema& schema, INodePtr node)
{
    TSerializableColumnSchema wrapper = TSerializableColumnSchema::Create();
    Deserialize(static_cast<TYsonStructLite&>(wrapper), node);
    schema = wrapper.GetColumnSchema();
}

void Deserialize(TColumnSchema& schema, NYson::TYsonPullParserCursor* cursor)
{
    std::optional<ESimpleLogicalValueType> logicalTypeV1;
    std::optional<bool> requiredV1;
    TLogicalTypePtr logicalTypeV3;

    cursor->ParseMap([&] (NYson::TYsonPullParserCursor* cursor) {
        EnsureYsonToken("column schema attribute key", *cursor, EYsonItemType::StringValue);
        auto key = (*cursor)->UncheckedAsString();
        if (key == TStringBuf("name")) {
            cursor->Next();
            schema.SetName(ExtractTo<TString>(cursor));
        } else if (key == TStringBuf("required")) {
            cursor->Next();
            requiredV1 = ExtractTo<bool>(cursor);
        } else if (key == TStringBuf("type")) {
            cursor->Next();
            logicalTypeV1 = ExtractTo<ESimpleLogicalValueType>(cursor);
        } else if (key == TStringBuf("type_v3")) {
            cursor->Next();
            DeserializeV3(logicalTypeV3, cursor);
        } else if (key == TStringBuf("lock")) {
            cursor->Next();
            schema.SetLock(ExtractTo<std::optional<TString>>(cursor));
        } else if (key == TStringBuf("expression")) {
            cursor->Next();
            schema.SetExpression(ExtractTo<std::optional<TString>>(cursor));
        } else if (key == TStringBuf("aggregate")) {
            cursor->Next();
            schema.SetAggregate(ExtractTo<std::optional<TString>>(cursor));
        } else if (key == TStringBuf("sort_order")) {
            cursor->Next();
            schema.SetSortOrder(ExtractTo<std::optional<ESortOrder>>(cursor));
        } else if (key == TStringBuf("group")) {
            cursor->Next();
            schema.SetGroup(ExtractTo<std::optional<TString>>(cursor));
        } else if (key == TStringBuf("max_inline_hunk_size")) {
            cursor->Next();
            schema.SetMaxInlineHunkSize(ExtractTo<std::optional<i64>>(cursor));
        } else {
            cursor->Next();
            cursor->SkipComplexValue();
        }
    });

    RunColumnSchemaPostprocessor(schema, logicalTypeV1, requiredV1, logicalTypeV3);
}

void ToProto(NProto::TColumnSchema* protoSchema, const TColumnSchema& schema)
{
    protoSchema->set_name(schema.Name());
    protoSchema->set_type(static_cast<int>(GetPhysicalType(schema.CastToV1Type())));

    if (schema.IsOfV1Type()) {
        protoSchema->set_simple_logical_type(static_cast<int>(schema.CastToV1Type()));
    } else {
        protoSchema->clear_simple_logical_type();
    }
    if (schema.Required()) {
        protoSchema->set_required(true);
    } else {
        protoSchema->clear_required();
    }
    ToProto(protoSchema->mutable_logical_type(), schema.LogicalType());
    if (schema.Lock()) {
        protoSchema->set_lock(*schema.Lock());
    } else {
        protoSchema->clear_lock();
    }
    if (schema.Expression()) {
        protoSchema->set_expression(*schema.Expression());
    } else {
        protoSchema->clear_expression();
    }
    if (schema.Aggregate()) {
        protoSchema->set_aggregate(*schema.Aggregate());
    } else {
        protoSchema->clear_aggregate();
    }
    if (schema.SortOrder()) {
        protoSchema->set_sort_order(static_cast<int>(*schema.SortOrder()));
    } else {
        protoSchema->clear_sort_order();
    }
    if (schema.Group()) {
        protoSchema->set_group(*schema.Group());
    } else {
        protoSchema->clear_group();
    }
    if (schema.MaxInlineHunkSize()) {
        protoSchema->set_max_inline_hunk_size(*schema.MaxInlineHunkSize());
    } else {
        protoSchema->clear_max_inline_hunk_size();
    }
}

void FromProto(TColumnSchema* schema, const NProto::TColumnSchema& protoSchema)
{
    schema->SetName(protoSchema.name());

    if (protoSchema.has_logical_type()) {
        TLogicalTypePtr logicalType;
        FromProto(&logicalType, protoSchema.logical_type());
        schema->SetLogicalType(logicalType);
    } else if (protoSchema.has_simple_logical_type()) {
        schema->SetLogicalType(
            MakeLogicalType(
                CheckedEnumCast<ESimpleLogicalValueType>(protoSchema.simple_logical_type()),
                protoSchema.required()));
    } else {
        auto physicalType = CheckedEnumCast<EValueType>(protoSchema.type());
        schema->SetLogicalType(MakeLogicalType(GetLogicalType(physicalType), protoSchema.required()));
    }

    schema->SetLock(protoSchema.has_lock() ? std::make_optional(protoSchema.lock()) : std::nullopt);
    schema->SetExpression(protoSchema.has_expression() ? std::make_optional(protoSchema.expression()) : std::nullopt);
    schema->SetAggregate(protoSchema.has_aggregate() ? std::make_optional(protoSchema.aggregate()) : std::nullopt);
    schema->SetSortOrder(protoSchema.has_sort_order() ? std::make_optional(CheckedEnumCast<ESortOrder>(protoSchema.sort_order())) : std::nullopt);
    schema->SetGroup(protoSchema.has_group() ? std::make_optional(protoSchema.group()) : std::nullopt);
    schema->SetMaxInlineHunkSize(protoSchema.has_max_inline_hunk_size() ? std::make_optional(protoSchema.max_inline_hunk_size()) : std::nullopt);
}

void PrintTo(const TColumnSchema& columnSchema, std::ostream* os)
{
    *os << Format("%v", columnSchema);
}

////////////////////////////////////////////////////////////////////////////////

TTableSchema::TTableSchema(
    std::vector<TColumnSchema> columns,
    bool strict,
    bool uniqueKeys,
    ETableSchemaModification schemaModification)
    : Columns_(std::move(columns))
    , Strict_(strict)
    , UniqueKeys_(uniqueKeys)
    , SchemaModification_(schemaModification)
{
    for (int index = 0; index < static_cast<int>(Columns_.size()); ++index) {
        const auto& column = Columns_[index];
        if (column.SortOrder()) {
            ++KeyColumnCount_;
        }
        if (column.Expression()) {
            HasComputedColumns_ = true;
        }
        if (column.Aggregate()) {
            HasAggregateColumns_ = true;
        }
        if (column.MaxInlineHunkSize()) {
            HunkColumnsIds_.push_back(index);
        }
    }
}

const TColumnSchema* TTableSchema::FindColumn(TStringBuf name) const
{
    for (auto& column : Columns_) {
        if (column.Name() == name) {
            return &column;
        }
    }
    return nullptr;
}

const TColumnSchema& TTableSchema::GetColumn(TStringBuf name) const
{
    auto* column = FindColumn(name);
    YT_VERIFY(column);
    return *column;
}

const TColumnSchema& TTableSchema::GetColumnOrThrow(TStringBuf name) const
{
    auto* column = FindColumn(name);
    if (!column) {
        THROW_ERROR_EXCEPTION("Missing schema column %Qv", name);
    }
    return *column;
}

int TTableSchema::GetColumnIndex(const TColumnSchema& column) const
{
    return &column - Columns().data();
}

int TTableSchema::GetColumnIndex(TStringBuf name) const
{
    return GetColumnIndex(GetColumn(name));
}

int TTableSchema::GetColumnIndexOrThrow(TStringBuf name) const
{
    return GetColumnIndex(GetColumnOrThrow(name));
}

TTableSchemaPtr TTableSchema::Filter(const TColumnFilter& columnFilter, bool discardSortOrder) const
{
    int newKeyColumnCount = 0;
    std::vector<TColumnSchema> columns;

    if (columnFilter.IsUniversal()) {
        if (!discardSortOrder) {
            return New<TTableSchema>(*this);
        } else {
            columns = Columns_;
            for (auto& column : columns) {
                column.SetSortOrder(std::nullopt);
            }
        }
    } else {
        bool inKeyColumns = !discardSortOrder;
        for (int id : columnFilter.GetIndexes()) {
            if (id < 0 || id >= std::ssize(Columns_)) {
                THROW_ERROR_EXCEPTION("Invalid column during schema filtering: expected in range [0, %v), got %v",
                    Columns_.size(),
                    id);
            }

            if (id != std::ssize(columns) || !Columns_[id].SortOrder()) {
                inKeyColumns = false;
            }

            columns.push_back(Columns_[id]);

            if (!inKeyColumns) {
                columns.back().SetSortOrder(std::nullopt);
            }

            if (columns.back().SortOrder()) {
                ++newKeyColumnCount;
            }
        }
    }

    return New<TTableSchema>(
        std::move(columns),
        Strict_,
        UniqueKeys_ && (newKeyColumnCount == GetKeyColumnCount()));
}

TTableSchemaPtr TTableSchema::Filter(const THashSet<TString>& columns, bool discardSortOrder) const
{
    TColumnFilter::TIndexes indexes;
    for (const auto& column : Columns()) {
        if (columns.find(column.Name()) != columns.end()) {
            indexes.push_back(GetColumnIndex(column));
        }
    }
    return Filter(TColumnFilter(std::move(indexes)), discardSortOrder);
}

TTableSchemaPtr TTableSchema::Filter(const std::optional<std::vector<TString>>& columns, bool discardSortOrder) const
{
    if (!columns) {
        return Filter(TColumnFilter(), discardSortOrder);
    }

    return Filter(THashSet<TString>(columns->begin(), columns->end()), discardSortOrder);
}

bool TTableSchema::HasComputedColumns() const
{
    return HasComputedColumns_;
}

bool TTableSchema::HasAggregateColumns() const
{
    return HasAggregateColumns_;
}

bool TTableSchema::HasHunkColumns() const
{
    return !HunkColumnsIds_.empty();
}

bool TTableSchema::HasTimestampColumn() const
{
    return FindColumn(TimestampColumnName);
}

bool TTableSchema::IsSorted() const
{
    return KeyColumnCount_ > 0;
}

bool TTableSchema::IsUniqueKeys() const
{
    return UniqueKeys_;
}

TKeyColumns TTableSchema::GetKeyColumns() const
{
    TKeyColumns keyColumns;
    for (const auto& column : Columns()) {
        if (column.SortOrder()) {
            keyColumns.push_back(column.Name());
        }
    }
    return keyColumns;
}

int TTableSchema::GetColumnCount() const
{
    return static_cast<int>(Columns_.size());
}

std::vector<TString> TTableSchema::GetColumnNames() const
{
    std::vector<TString> result;
    result.reserve(Columns_.size());
    for (const auto& column : Columns_) {
        result.push_back(column.Name());
    }
    return result;
}

const THunkColumnIds& TTableSchema::GetHunkColumnIds() const
{
    return HunkColumnsIds_;
}

int TTableSchema::GetKeyColumnCount() const
{
    return KeyColumnCount_;
}

int TTableSchema::GetValueColumnCount() const
{
    return GetColumnCount() - GetKeyColumnCount();
}

TSortColumns TTableSchema::GetSortColumns() const
{
    TSortColumns sortColumns;
    sortColumns.reserve(GetKeyColumnCount());

    for (const auto& column : Columns()) {
        if (column.SortOrder()) {
            sortColumns.push_back(TColumnSortSchema{
                .Name = column.Name(),
                .SortOrder = *column.SortOrder()
            });
        }
    }

    return sortColumns;
}

TTableSchemaPtr TTableSchema::SetKeyColumnCount(int keyColumnCount) const
{
    auto schema = *this;

    for (int columnIndex = 0; columnIndex < schema.GetColumnCount(); ++columnIndex) {
        auto& column = schema.Columns_[columnIndex];
        if (columnIndex < keyColumnCount) {
            column.SetSortOrder(ESortOrder::Ascending);
        } else {
            column.SetSortOrder(std::nullopt);
        }
    }

    schema.KeyColumnCount_ = keyColumnCount;

    return New<TTableSchema>(std::move(schema));
}

TTableSchemaPtr TTableSchema::SetUniqueKeys(bool uniqueKeys) const
{
    auto schema = *this;
    schema.UniqueKeys_ = uniqueKeys;
    return New<TTableSchema>(std::move(schema));
}

TTableSchemaPtr TTableSchema::SetSchemaModification(ETableSchemaModification schemaModification) const
{
    auto schema = *this;
    schema.SchemaModification_ = schemaModification;
    return New<TTableSchema>(std::move(schema));
}

bool TTableSchema::HasNontrivialSchemaModification() const
{
    return GetSchemaModification() != ETableSchemaModification::None;
}

TTableSchemaPtr TTableSchema::FromKeyColumns(const TKeyColumns& keyColumns)
{
    TTableSchema schema;
    for (const auto& columnName : keyColumns) {
        schema.Columns_.push_back(
            TColumnSchema(columnName, ESimpleLogicalValueType::Any)
                .SetSortOrder(ESortOrder::Ascending));
    }
    schema.KeyColumnCount_ = keyColumns.size();
    ValidateTableSchema(schema);
    return New<TTableSchema>(std::move(schema));
}

TTableSchemaPtr TTableSchema::FromSortColumns(const TSortColumns& sortColumns)
{
    TTableSchema schema;
    for (const auto& sortColumn : sortColumns) {
        schema.Columns_.push_back(
            TColumnSchema(sortColumn.Name, ESimpleLogicalValueType::Any)
                .SetSortOrder(sortColumn.SortOrder));
    }
    schema.KeyColumnCount_ = sortColumns.size();
    ValidateTableSchema(schema);
    return New<TTableSchema>(std::move(schema));
}

TTableSchemaPtr TTableSchema::ToQuery() const
{
    if (IsSorted()) {
        return New<TTableSchema>(*this);
    } else {
        std::vector<TColumnSchema> columns {
            TColumnSchema(TabletIndexColumnName, ESimpleLogicalValueType::Int64)
                .SetSortOrder(ESortOrder::Ascending),
            TColumnSchema(RowIndexColumnName, ESimpleLogicalValueType::Int64)
                .SetSortOrder(ESortOrder::Ascending)
        };
        columns.insert(columns.end(), Columns_.begin(), Columns_.end());
        return New<TTableSchema>(std::move(columns));
    }
}

TTableSchemaPtr TTableSchema::ToWrite() const
{
    std::vector<TColumnSchema> columns;
    if (IsSorted()) {
        for (const auto& column : Columns_) {
            if (!column.Expression()) {
                columns.push_back(column);
            }
        }
    } else {
        columns.push_back(TColumnSchema(TabletIndexColumnName, ESimpleLogicalValueType::Int64)
            .SetSortOrder(ESortOrder::Ascending));
        for (const auto& column : Columns_) {
            if (column.Name() != TimestampColumnName && column.Name() != CumulativeDataWeightColumnName) {
                columns.push_back(column);
            }
        }
    }
    return New<TTableSchema>(std::move(columns), Strict_, UniqueKeys_);
}

TTableSchemaPtr TTableSchema::WithTabletIndex() const
{
    if (IsSorted()) {
        return New<TTableSchema>(*this);
    } else {
        auto columns = Columns_;
        // XXX: Is it ok? $tablet_index is usually a key column.
        columns.push_back(TColumnSchema(TabletIndexColumnName, ESimpleLogicalValueType::Int64));
        return New<TTableSchema>(std::move(columns), Strict_, UniqueKeys_);
    }
}

TTableSchemaPtr TTableSchema::ToVersionedWrite() const
{
    if (IsSorted()) {
        return New<TTableSchema>(*this);
    } else {
        auto columns = Columns_;
        columns.insert(columns.begin(), TColumnSchema(TabletIndexColumnName, ESimpleLogicalValueType::Int64)
            .SetSortOrder(ESortOrder::Ascending));
        return New<TTableSchema>(std::move(columns), Strict_, UniqueKeys_);
    }
}

TTableSchemaPtr TTableSchema::ToLookup() const
{
    std::vector<TColumnSchema> columns;
    for (const auto& column : Columns_) {
        if (column.SortOrder() && !column.Expression()) {
            columns.push_back(column);
        }
    }
    return New<TTableSchema>(std::move(columns), Strict_, UniqueKeys_);
}

TTableSchemaPtr TTableSchema::ToDelete() const
{
    return ToLookup();
}

TTableSchemaPtr TTableSchema::ToKeys() const
{
    std::vector<TColumnSchema> columns(Columns_.begin(), Columns_.begin() + KeyColumnCount_);
    return New<TTableSchema>(std::move(columns), Strict_, UniqueKeys_);
}

TTableSchemaPtr TTableSchema::ToValues() const
{
    std::vector<TColumnSchema> columns(Columns_.begin() + KeyColumnCount_, Columns_.end());
    return New<TTableSchema>(std::move(columns), Strict_, false);
}

TTableSchemaPtr TTableSchema::ToUniqueKeys() const
{
    return New<TTableSchema>(Columns_, Strict_, /*uniqueKeys*/ true);
}

TTableSchemaPtr TTableSchema::ToStrippedColumnAttributes() const
{
    std::vector<TColumnSchema> strippedColumns;
    for (auto& column : Columns_) {
        strippedColumns.emplace_back(column.Name(), column.LogicalType());
    }
    return New<TTableSchema>(strippedColumns, Strict_, /*uniqueKeys*/ false);
}

TTableSchemaPtr TTableSchema::ToSortedStrippedColumnAttributes() const
{
    std::vector<TColumnSchema> strippedColumns;
    for (auto& column : Columns_) {
        strippedColumns.emplace_back(column.Name(), column.LogicalType(), column.SortOrder());
    }
    return New<TTableSchema>(strippedColumns, Strict_, UniqueKeys_);
}

TTableSchemaPtr TTableSchema::ToCanonical() const
{
    auto columns = Columns();
    std::sort(
        columns.begin() + KeyColumnCount_,
        columns.end(),
        [] (const TColumnSchema& lhs, const TColumnSchema& rhs) {
            return lhs.Name() < rhs.Name();
        });
    return New<TTableSchema>(columns, Strict_, UniqueKeys_);
}

TTableSchemaPtr TTableSchema::ToSorted(const TKeyColumns& keyColumns) const
{
    TSortColumns sortColumns;
    sortColumns.reserve(keyColumns.size());
    for (const auto& keyColumn : keyColumns) {
        sortColumns.push_back(TColumnSortSchema{
            .Name = keyColumn,
            .SortOrder = ESortOrder::Ascending
        });
    }

    return TTableSchema::ToSorted(sortColumns);
}

TTableSchemaPtr TTableSchema::ToSorted(const TSortColumns& sortColumns) const
{
    int oldKeyColumnCount = 0;
    auto columns = Columns();
    for (int index = 0; index < std::ssize(sortColumns); ++index) {
        auto it = std::find_if(
            columns.begin() + index,
            columns.end(),
            [&] (const TColumnSchema& column) {
                return column.Name() == sortColumns[index].Name;
            });

        if (it == columns.end()) {
            if (Strict_) {
                THROW_ERROR_EXCEPTION(
                    EErrorCode::IncompatibleKeyColumns,
                    "Column %Qv is not found in strict schema", sortColumns[index].Name)
                    << TErrorAttribute("schema", *this)
                    << TErrorAttribute("sort_columns", sortColumns);
            } else {
                columns.push_back(TColumnSchema(sortColumns[index].Name, EValueType::Any));
                it = columns.end();
                --it;
            }
        }

        if (it->SortOrder()) {
            ++oldKeyColumnCount;
        }

        std::swap(columns[index], *it);
        columns[index].SetSortOrder(sortColumns[index].SortOrder);
    }

    auto uniqueKeys = UniqueKeys_ && oldKeyColumnCount == GetKeyColumnCount();

    for (auto it = columns.begin() + sortColumns.size(); it != columns.end(); ++it) {
        it->SetSortOrder(std::nullopt);
    }

    return New<TTableSchema>(columns, Strict_, uniqueKeys, GetSchemaModification());
}

TTableSchemaPtr TTableSchema::ToReplicationLog() const
{
    std::vector<TColumnSchema> columns;
    columns.push_back(TColumnSchema(TimestampColumnName, ESimpleLogicalValueType::Uint64));
    if (IsSorted()) {
        columns.push_back(TColumnSchema(TReplicationLogTable::ChangeTypeColumnName, ESimpleLogicalValueType::Int64));
        for (const auto& column : Columns_) {
            if (column.SortOrder()) {
                columns.push_back(
                    TColumnSchema(
                        TReplicationLogTable::KeyColumnNamePrefix + column.Name(),
                        column.LogicalType()));
            } else {
                columns.push_back(
                    TColumnSchema(
                        TReplicationLogTable::ValueColumnNamePrefix + column.Name(),
                        MakeOptionalIfNot(column.LogicalType())));
                columns.push_back(
                    TColumnSchema(TReplicationLogTable::FlagsColumnNamePrefix + column.Name(), ESimpleLogicalValueType::Uint64));
            }
        }
    } else {
        for (const auto& column : Columns_) {
            columns.push_back(
                TColumnSchema(
                    TReplicationLogTable::ValueColumnNamePrefix + column.Name(),
                    MakeOptionalIfNot(column.LogicalType())));
        }
        columns.push_back(TColumnSchema(TReplicationLogTable::ValueColumnNamePrefix + TabletIndexColumnName, ESimpleLogicalValueType::Int64));
    }
    return New<TTableSchema>(std::move(columns), /* strict */ true, /* uniqueKeys */ false);
}

TTableSchemaPtr TTableSchema::ToUnversionedUpdate(bool sorted) const
{
    YT_VERIFY(IsSorted());

    std::vector<TColumnSchema> columns;
    columns.reserve(GetKeyColumnCount() + 1 + GetValueColumnCount() * 2);

    // Keys.
    for (int columnIndex = 0; columnIndex < GetKeyColumnCount(); ++columnIndex) {
        auto column = Columns_[columnIndex];
        if (!sorted) {
            column.SetSortOrder(std::nullopt);
        }
        columns.push_back(column);
    }

    // Modification type.
    columns.emplace_back(
        TUnversionedUpdateSchema::ChangeTypeColumnName,
        MakeLogicalType(ESimpleLogicalValueType::Uint64, /*required*/ true));

    // Values.
    for (int columnIndex = GetKeyColumnCount(); columnIndex < GetColumnCount(); ++columnIndex) {
        const auto& column = Columns_[columnIndex];
        YT_VERIFY(!column.SortOrder());
        columns.emplace_back(
            TUnversionedUpdateSchema::ValueColumnNamePrefix + column.Name(),
            MakeOptionalIfNot(column.LogicalType()));
        columns.emplace_back(
            TUnversionedUpdateSchema::FlagsColumnNamePrefix + column.Name(),
            MakeLogicalType(ESimpleLogicalValueType::Uint64, /*required*/ false));
    }

    return New<TTableSchema>(std::move(columns), /*strict*/ true, /*uniqueKeys*/ sorted);
}

TTableSchemaPtr TTableSchema::ToModifiedSchema(ETableSchemaModification schemaModification) const
{
    if (HasNontrivialSchemaModification()) {
        THROW_ERROR_EXCEPTION("Cannot apply schema modification because schema is already modified")
            << TErrorAttribute("existing_modification", GetSchemaModification())
            << TErrorAttribute("requested_modification", schemaModification);
    }
    YT_VERIFY(GetSchemaModification() == ETableSchemaModification::None);

    switch (schemaModification) {
        case ETableSchemaModification::None:
            return New<TTableSchema>(*this);

        case ETableSchemaModification::UnversionedUpdate:
            return ToUnversionedUpdate(/*sorted*/ true)
                ->SetSchemaModification(schemaModification);

        case ETableSchemaModification::UnversionedUpdateUnsorted:
            return ToUnversionedUpdate(/*sorted*/ false)
                ->SetSchemaModification(schemaModification);

        default:
            YT_ABORT();
    }
}

TComparator TTableSchema::ToComparator() const
{
    std::vector<ESortOrder> sortOrders(KeyColumnCount_);
    for (int index = 0; index < KeyColumnCount_; ++index) {
        YT_VERIFY(Columns_[index].SortOrder());
        sortOrders[index] = *Columns_[index].SortOrder();
    }
    return TComparator(std::move(sortOrders));
}

void TTableSchema::Save(TStreamSaveContext& context) const
{
    using NYT::Save;
    Save(context, ToProto<NTableClient::NProto::TTableSchemaExt>(*this));
}

void TTableSchema::Load(TStreamLoadContext& context)
{
    using NYT::Load;
    auto protoSchema = NYT::Load<NTableClient::NProto::TTableSchemaExt>(context);
    *this = FromProto<TTableSchema>(protoSchema);
}

i64 TTableSchema::GetMemoryUsage() const
{
    i64 usage = sizeof(TTableSchema);
    for (const auto& column : Columns_) {
        usage += column.GetMemoryUsage();
    }
    return usage;
}

TKeyColumnTypes TTableSchema::GetKeyColumnTypes() const
{
    TKeyColumnTypes result(KeyColumnCount_);
    for (int index = 0; index < KeyColumnCount_; ++index) {
        result[index] = Columns_[index].GetWireType();
    }
    return result;
}

////////////////////////////////////////////////////////////////////////////////

void FormatValue(TStringBuilderBase* builder, const TTableSchema& schema, TStringBuf /*spec*/)
{
    builder->AppendFormat("<strict=%v;unique_keys=%v", schema.GetStrict(), schema.GetUniqueKeys());
    if (schema.HasNontrivialSchemaModification()) {
        builder->AppendFormat(";schema_modification=%v", schema.GetSchemaModification());
    }
    builder->AppendChar('>');
    builder->AppendChar('[');
    bool first = true;
    for (const auto& column : schema.Columns()) {
        if (!first) {
            builder->AppendString(TStringBuf("; "));
        }
        builder->AppendFormat("%v", column);
        first = false;
    }
    builder->AppendChar(']');
}

TString ToString(const TTableSchema& schema)
{
    return ToStringViaBuilder(schema);
}

void FormatValue(TStringBuilderBase* builder, const TTableSchemaPtr& schema, TStringBuf spec)
{
    if (schema) {
        FormatValue(builder, *schema, spec);
    } else {
        builder->AppendString(TStringBuf("<null>"));
    }
}

TString ToString(const TTableSchemaPtr& schema)
{
    return ToStringViaBuilder(schema);
}

TString SerializeToWireProto(const TTableSchemaPtr& schema)
{
    NTableClient::NProto::TTableSchemaExt protoSchema;
    ToProto(&protoSchema, schema);
    return protoSchema.SerializeAsString();
}

void DeserializeFromWireProto(TTableSchemaPtr* schema, const TString& serializedProto)
{
    NTableClient::NProto::TTableSchemaExt protoSchema;
    if (!protoSchema.ParseFromString(serializedProto)) {
        THROW_ERROR_EXCEPTION("Failed to deserialize table schema from wire proto");
    }
    FromProto(schema, protoSchema);
}

void Serialize(const TTableSchema& schema, IYsonConsumer* consumer)
{
    BuildYsonFluently(consumer)
        .BeginAttributes()
            .Item("strict").Value(schema.GetStrict())
            .Item("unique_keys").Value(schema.GetUniqueKeys())
            .DoIf(schema.HasNontrivialSchemaModification(), [&] (TFluentMap fluent) {
                fluent.Item("schema_modification").Value(schema.GetSchemaModification());
            })
        .EndAttributes()
        .Value(schema.Columns());
}

void Deserialize(TTableSchema& schema, INodePtr node)
{
    schema = TTableSchema(
        ConvertTo<std::vector<TColumnSchema>>(node),
        node->Attributes().Get<bool>("strict", true),
        node->Attributes().Get<bool>("unique_keys", false),
        node->Attributes().Get<ETableSchemaModification>(
            "schema_modification",
            ETableSchemaModification::None));
}

void Deserialize(TTableSchema& schema, NYson::TYsonPullParserCursor* cursor)
{
    bool strict = true;
    bool uniqueKeys = false;
    ETableSchemaModification modification = ETableSchemaModification::None;
    std::vector<TColumnSchema> columns;

    if ((*cursor)->GetType() == EYsonItemType::BeginAttributes) {
        cursor->ParseAttributes([&] (TYsonPullParserCursor* cursor) {
            EnsureYsonToken(TStringBuf("table schema attribute key"), *cursor, EYsonItemType::StringValue);
            auto key = (*cursor)->UncheckedAsString();
            if (key == TStringBuf("strict")) {
                cursor->Next();
                strict = ExtractTo<bool>(cursor);
            } else if (key == TStringBuf("unique_keys")) {
                cursor->Next();
                uniqueKeys = ExtractTo<bool>(cursor);
            } else if (key == TStringBuf("schema_modification")) {
                cursor->Next();
                modification = ExtractTo<ETableSchemaModification>(cursor);
            } else {
                cursor->Next();
                cursor->SkipComplexValue();
            }
        });
    }
    EnsureYsonToken(TStringBuf("table schema"), *cursor, EYsonItemType::BeginList);
    columns = ExtractTo<std::vector<TColumnSchema>>(cursor);
    schema = TTableSchema(std::move(columns), strict, uniqueKeys, modification);
}

void Serialize(const TTableSchemaPtr& schema, IYsonConsumer* consumer)
{
    Serialize(*schema, consumer);
}


void Deserialize(TTableSchemaPtr& schema, INodePtr node)
{
    TTableSchema actualSchema;
    Deserialize(actualSchema, node);
    schema = New<TTableSchema>(std::move(actualSchema));
}

void Deserialize(TTableSchemaPtr& schema, NYson::TYsonPullParserCursor* cursor)
{
    TTableSchema actualSchema;
    Deserialize(actualSchema, cursor);
    schema = New<TTableSchema>(std::move(actualSchema));
}

void ToProto(NProto::TTableSchemaExt* protoSchema, const TTableSchema& schema)
{
    ToProto(protoSchema->mutable_columns(), schema.Columns());
    protoSchema->set_strict(schema.GetStrict());
    protoSchema->set_unique_keys(schema.GetUniqueKeys());
    protoSchema->set_schema_modification(static_cast<int>(schema.GetSchemaModification()));
}

void FromProto(TTableSchema* schema, const NProto::TTableSchemaExt& protoSchema)
{
    *schema = TTableSchema(
        FromProto<std::vector<TColumnSchema>>(protoSchema.columns()),
        protoSchema.strict(),
        protoSchema.unique_keys(),
        CheckedEnumCast<ETableSchemaModification>(protoSchema.schema_modification()));
}

void FromProto(
    TTableSchema* schema,
    const NProto::TTableSchemaExt& protoSchema,
    const NProto::TKeyColumnsExt& protoKeyColumns)
{
    auto columns = FromProto<std::vector<TColumnSchema>>(protoSchema.columns());
    for (int columnIndex = 0; columnIndex < protoKeyColumns.names_size(); ++columnIndex) {
        auto& columnSchema = columns[columnIndex];
        YT_VERIFY(columnSchema.Name() == protoKeyColumns.names(columnIndex));
        // TODO(gritukan): YT-14155
        if (!columnSchema.SortOrder()) {
            columnSchema.SetSortOrder(ESortOrder::Ascending);
        }
    }
    for (int columnIndex = protoKeyColumns.names_size(); columnIndex < std::ssize(columns); ++columnIndex) {
        auto& columnSchema = columns[columnIndex];
        YT_VERIFY(!columnSchema.SortOrder());
    }
    *schema = TTableSchema(
        std::move(columns),
        protoSchema.strict(),
        protoSchema.unique_keys());
}

void ToProto(NProto::TTableSchemaExt* protoSchema, const TTableSchemaPtr& schema)
{
    ToProto(protoSchema, *schema);
}

void FromProto(TTableSchemaPtr* schema, const NProto::TTableSchemaExt& protoSchema)
{
    *schema = New<TTableSchema>();
    FromProto(schema->Get(), protoSchema);
}

void FromProto(
    TTableSchemaPtr* schema,
    const NProto::TTableSchemaExt& protoSchema,
    const NProto::TKeyColumnsExt& keyColumnsExt)
{
    *schema = New<TTableSchema>();
    FromProto(schema->Get(), protoSchema, keyColumnsExt);
}

void PrintTo(const TTableSchema& tableSchema, std::ostream* os)
{
    *os << Format("%v", tableSchema);
}

////////////////////////////////////////////////////////////////////////////////

bool operator==(const TColumnSchema& lhs, const TColumnSchema& rhs)
{
    return
        lhs.Name() == rhs.Name() &&
        *lhs.LogicalType() == *rhs.LogicalType() &&
        lhs.Required() == rhs.Required() &&
        lhs.SortOrder() == rhs.SortOrder() &&
        lhs.Lock() == rhs.Lock() &&
        lhs.Expression() == rhs.Expression() &&
        lhs.Aggregate() == rhs.Aggregate() &&
        lhs.Group() == rhs.Group() &&
        lhs.MaxInlineHunkSize() == rhs.MaxInlineHunkSize();
}

bool operator!=(const TColumnSchema& lhs, const TColumnSchema& rhs)
{
    return !(lhs == rhs);
}

bool operator==(const TTableSchema& lhs, const TTableSchema& rhs)
{
    return
        lhs.Columns() == rhs.Columns() &&
        lhs.GetStrict() == rhs.GetStrict() &&
        lhs.GetUniqueKeys() == rhs.GetUniqueKeys() &&
        lhs.GetSchemaModification() == rhs.GetSchemaModification();
}

// Compat code for https://st.yandex-team.ru/YT-10668 workaround.
bool IsEqualIgnoringRequiredness(const TTableSchema& lhs, const TTableSchema& rhs)
{
    auto dropRequiredness = [] (const TTableSchema& schema) {
        std::vector<TColumnSchema> resultColumns;
        for (auto column : schema.Columns()) {
            if (column.LogicalType()->GetMetatype() == ELogicalMetatype::Optional) {
                column.SetLogicalType(column.LogicalType()->AsOptionalTypeRef().GetElement());
            }
            resultColumns.emplace_back(column);
        }
        return TTableSchema(resultColumns, schema.GetStrict(), schema.GetUniqueKeys());
    };
    return dropRequiredness(lhs) == dropRequiredness(rhs);
}

bool operator!=(const TTableSchema& lhs, const TTableSchema& rhs)
{
    return !(lhs == rhs);
}

////////////////////////////////////////////////////////////////////////////////

void ValidateKeyColumns(const TKeyColumns& keyColumns)
{
    ValidateKeyColumnCount(keyColumns.size());

    THashSet<TString> names;
    for (const auto& name : keyColumns) {
        if (!names.insert(name).second) {
            THROW_ERROR_EXCEPTION("Duplicate key column name %Qv",
                name);
        }
    }
}

////////////////////////////////////////////////////////////////////////////////

void ValidateSystemColumnSchema(
    const TColumnSchema& columnSchema,
    bool isTableSorted,
    bool allowUnversionedUpdateColumns)
{
    static const auto allowedSortedTablesSystemColumns = THashMap<TString, ESimpleLogicalValueType>{
    };

    static const auto allowedOrderedTablesSystemColumns = THashMap<TString, ESimpleLogicalValueType>{
        {TimestampColumnName, ESimpleLogicalValueType::Uint64},
        {CumulativeDataWeightColumnName, ESimpleLogicalValueType::Int64}
    };

    auto validateType = [&] (ESimpleLogicalValueType expected) {
        if (!columnSchema.IsOfV1Type(expected)) {
            THROW_ERROR_EXCEPTION("Invalid type of column %Qv: expected %Qlv, got %Qlv",
                columnSchema.Name(),
                expected,
                *columnSchema.LogicalType());
        }
    };

    const auto& name = columnSchema.Name();

    const auto& allowedSystemColumns = isTableSorted
        ? allowedSortedTablesSystemColumns
        : allowedOrderedTablesSystemColumns;
    auto it = allowedSystemColumns.find(name);

    // Ordinary system column.
    if (it != allowedSystemColumns.end()) {
        validateType(it->second);
        return;
    }

    if (allowUnversionedUpdateColumns) {
        // Unversioned update schema system column.
        if (name == TUnversionedUpdateSchema::ChangeTypeColumnName) {
            validateType(ESimpleLogicalValueType::Uint64);
            return;
        } else if (name.StartsWith(TUnversionedUpdateSchema::FlagsColumnNamePrefix)) {
            validateType(ESimpleLogicalValueType::Uint64);
            return;
        } else if (name.StartsWith(TUnversionedUpdateSchema::ValueColumnNamePrefix)) {
            // Value can have any type.
            return;
        }
    }

    // Unexpected system column.
    THROW_ERROR_EXCEPTION("System column name %Qv is not allowed here",
        name);
}

void ValidateColumnSchema(
    const TColumnSchema& columnSchema,
    bool isTableSorted,
    bool isTableDynamic,
    bool allowUnversionedUpdateColumns)
{
    static const auto allowedAggregates = THashSet<TString>{
        "sum",
        "min",
        "max",
        "first",
        "xdelta"
    };

    const auto& name = columnSchema.Name();
    if (name.empty()) {
        THROW_ERROR_EXCEPTION("Column name cannot be empty");
    }

    try {
        if (name.StartsWith(SystemColumnNamePrefix)) {
            ValidateSystemColumnSchema(columnSchema, isTableSorted, allowUnversionedUpdateColumns);
        }

        if (name.size() > MaxColumnNameLength) {
            THROW_ERROR_EXCEPTION("Column name is longer than maximum allowed: %v > %v",
                columnSchema.Name().size(),
                MaxColumnNameLength);
        }

        {
            TComplexTypeFieldDescriptor descriptor(name, columnSchema.LogicalType());
            ValidateLogicalType(descriptor, MaxSchemaDepth);
        }

        if (!IsComparable(columnSchema.LogicalType()) &&
            columnSchema.SortOrder() &&
            !columnSchema.IsOfV1Type(ESimpleLogicalValueType::Any))
        {
            THROW_ERROR_EXCEPTION("Key column cannot be of %Qv type",
                *columnSchema.LogicalType());
        }

        if (*DetagLogicalType(columnSchema.LogicalType()) == *SimpleLogicalType(ESimpleLogicalValueType::Any)) {
            THROW_ERROR_EXCEPTION("Column of type %Qlv cannot be required",
                ESimpleLogicalValueType::Any);
        }

        if (columnSchema.Lock()) {
            if (columnSchema.Lock()->empty()) {
                THROW_ERROR_EXCEPTION("Column lock name cannot be empty");
            }
            if (columnSchema.Lock()->size() > MaxColumnLockLength) {
                THROW_ERROR_EXCEPTION("Column lock name is longer than maximum allowed: %v > %v",
                    columnSchema.Lock()->size(),
                    MaxColumnLockLength);
            }
            if (columnSchema.SortOrder()) {
                THROW_ERROR_EXCEPTION("Column lock cannot be set on a key column");
            }
        }

        if (columnSchema.Group()) {
            if (columnSchema.Group()->empty()) {
                THROW_ERROR_EXCEPTION("Column group should either be unset or be non-empty");
            }
            if (columnSchema.Group()->size() > MaxColumnGroupLength) {
                THROW_ERROR_EXCEPTION("Column group name is longer than maximum allowed: %v > %v",
                    columnSchema.Group()->size(),
                    MaxColumnGroupLength);
            }
        }

        ValidateSchemaValueType(columnSchema.GetWireType());

        if (columnSchema.Expression() && !columnSchema.SortOrder() && isTableDynamic) {
            THROW_ERROR_EXCEPTION("Non-key column cannot be computed");
        }

        if (columnSchema.Aggregate() && columnSchema.SortOrder()) {
            THROW_ERROR_EXCEPTION("Key column cannot be aggregated");
        }

        if (columnSchema.Aggregate() && allowedAggregates.find(*columnSchema.Aggregate()) == allowedAggregates.end()) {
            THROW_ERROR_EXCEPTION("Invalid aggregate function %Qv",
                *columnSchema.Aggregate());
        }

        if (columnSchema.Expression() && columnSchema.Required()) {
            THROW_ERROR_EXCEPTION("Computed column cannot be required");
        }

        if (columnSchema.MaxInlineHunkSize()) {
            if (columnSchema.MaxInlineHunkSize() <= 0) {
                THROW_ERROR_EXCEPTION("Max inline hunk size must be positive");
            }
            if (!IsStringLikeType(columnSchema.GetWireType())) {
                THROW_ERROR_EXCEPTION("Max inline hunk size can only be set for string-like columns, not %Qlv",
                    columnSchema.GetWireType());
            }
            if (columnSchema.SortOrder()) {
                THROW_ERROR_EXCEPTION("Max inline hunk size cannot be set for key column");
            }
            if (columnSchema.Aggregate()) {
                THROW_ERROR_EXCEPTION("Max inline hunk size cannot be set for aggregate column");
            }
        }
    } catch (const std::exception& ex) {
        THROW_ERROR_EXCEPTION("Error validating schema of a column %Qv",
            name)
            << ex;
    }
}

void ValidateDynamicTableConstraints(const TTableSchema& schema)
{
    if (!schema.GetStrict()) {
        THROW_ERROR_EXCEPTION("\"strict\" cannot be \"false\" for a dynamic table");
    }

    if (schema.IsSorted() && !schema.GetUniqueKeys()) {
        THROW_ERROR_EXCEPTION("\"unique_keys\" cannot be \"false\" for a sorted dynamic table");
    }

    if (schema.GetKeyColumnCount() == std::ssize(schema.Columns())) {
       THROW_ERROR_EXCEPTION("There must be at least one non-key column");
    }

    if (schema.GetKeyColumnCount() > MaxKeyColumnCountInDynamicTable) {
        THROW_ERROR_EXCEPTION("Too many key columns: limit %v, actual: %v",
            MaxKeyColumnCountInDynamicTable,
            schema.GetKeyColumnCount());
    }

    for (const auto& column : schema.Columns()) {
        try {
            if (column.SortOrder() && (
                    column.GetWireType() == EValueType::Any ||
                    column.GetWireType() == EValueType::Composite ||
                    !column.IsOfV1Type()))
            {
                THROW_ERROR_EXCEPTION("Dynamic table cannot have key column of type %Qv",
                    *column.LogicalType());
            }
        } catch (const std::exception& ex) {
            THROW_ERROR_EXCEPTION("Error validating column %Qv in dynamic table schema",
                column.Name())
                << ex;
        }
    }
}

//! Validates that there are no duplicates among the column names.
void ValidateColumnUniqueness(const TTableSchema& schema)
{
    THashSet<TString> columnNames;
    for (const auto& column : schema.Columns()) {
        if (!columnNames.insert(column.Name()).second) {
            THROW_ERROR_EXCEPTION("Duplicate column name %Qv in table schema",
                column.Name());
        }
    }
}

//! Validates that number of locks doesn't exceed #MaxColumnLockCount.
void ValidateLocks(const TTableSchema& schema)
{
    THashSet<TString> lockNames;
    YT_VERIFY(lockNames.insert(PrimaryLockName).second);
    for (const auto& column : schema.Columns()) {
        if (column.Lock()) {
            lockNames.insert(*column.Lock());
        }
    }

    if (lockNames.size() > MaxColumnLockCount) {
        THROW_ERROR_EXCEPTION("Too many column locks in table schema: actual %v, limit %v",
            lockNames.size(),
            MaxColumnLockCount);
    }
}

//! Validates that key columns form a prefix of a table schema.
void ValidateKeyColumnsFormPrefix(const TTableSchema& schema)
{
    for (int index = 0; index < schema.GetKeyColumnCount(); ++index) {
        if (!schema.Columns()[index].SortOrder()) {
            THROW_ERROR_EXCEPTION("Key columns must form a prefix of schema");
        }
    }
    // The fact that first GetKeyColumnCount() columns have SortOrder automatically
    // implies that the rest of columns don't have SortOrder, so we don't need to check it.
}

//! Validates |$timestamp| column, if any.
/*!
 *  Validate that:
 *  - |$timestamp| column cannot be a part of key.
 *  - |$timestamp| column can only be present in ordered tables.
 *  - |$timestamp| column has type |uint64|.
 */
void ValidateTimestampColumn(const TTableSchema& schema)
{
    auto* column = schema.FindColumn(TimestampColumnName);
    if (!column) {
        return;
    }

    if (column->SortOrder()) {
        THROW_ERROR_EXCEPTION("Column %Qv cannot be a part of key",
            TimestampColumnName);
    }

    if (!column->IsOfV1Type(ESimpleLogicalValueType::Uint64)) {
        THROW_ERROR_EXCEPTION("Column %Qv must have %Qlv type",
            TimestampColumnName,
            EValueType::Uint64);
    }

    if (schema.IsSorted()) {
        THROW_ERROR_EXCEPTION("Column %Qv cannot appear in a sorted table",
            TimestampColumnName);
    }
}

//! Validates |$cumulative_data_weight| column, if any.
/*!
 *  Validate that:
 *  - |$cumulative_data_weight| column cannot be a part of key.
 *  - |$cumulative_data_weight| column can only be present in ordered tables.
 *  - |$cumulative_data_weight| column has type |int64|.
 */
void ValidateCumulativeDataWeightColumn(const TTableSchema& schema)
{
    auto* column = schema.FindColumn(CumulativeDataWeightColumnName);
    if (!column) {
        return;
    }

    if (column->SortOrder()) {
        THROW_ERROR_EXCEPTION(
            "Column %Qv cannot be a part of key",
            CumulativeDataWeightColumnName);
    }

    if (!column->IsOfV1Type(ESimpleLogicalValueType::Int64)) {
        THROW_ERROR_EXCEPTION(
            "Column %Qv must have %Qlv type",
            CumulativeDataWeightColumnName,
            EValueType::Int64);
    }

    if (schema.IsSorted()) {
        THROW_ERROR_EXCEPTION(
            "Column %Qv cannot appear in a sorted table",
            CumulativeDataWeightColumnName);
    }
}

// Validate schema attributes.
void ValidateSchemaAttributes(const TTableSchema& schema)
{
    if (schema.GetUniqueKeys() && schema.GetKeyColumnCount() == 0) {
        THROW_ERROR_EXCEPTION("\"unique_keys\" can only be true if key columns are present");
    }
}

void ValidateTableSchema(const TTableSchema& schema, bool isTableDynamic, bool allowUnversionedUpdateColumns)
{
    int totalTypeComplexity = 0;
    for (const auto& column : schema.Columns()) {
        ValidateColumnSchema(
            column,
            schema.IsSorted(),
            isTableDynamic,
            allowUnversionedUpdateColumns);
        totalTypeComplexity += column.LogicalType()->GetTypeComplexity();
    }
    if (totalTypeComplexity >= MaxSchemaTotalTypeComplexity) {
        THROW_ERROR_EXCEPTION("Table schema is too complex, reduce number of columns or simplify their types");
    }
    ValidateColumnUniqueness(schema);
    ValidateLocks(schema);
    ValidateKeyColumnsFormPrefix(schema);
    ValidateTimestampColumn(schema);
    ValidateCumulativeDataWeightColumn(schema);
    ValidateSchemaAttributes(schema);
    if (isTableDynamic) {
        ValidateDynamicTableConstraints(schema);
    }
}

void ValidateNoDescendingSortOrder(const TTableSchema& schema)
{
    for (const auto& column : schema.Columns()) {
        if (column.SortOrder() == ESortOrder::Descending) {
            THROW_ERROR_EXCEPTION(
                NTableClient::EErrorCode::InvalidSchemaValue,
                "Descending sort order is not available in this context yet")
                << TErrorAttribute("column_name", column.Name());
        }
    }
}

////////////////////////////////////////////////////////////////////////////////

THashMap<TString, int> GetLocksMapping(
    const NTableClient::TTableSchema& schema,
    bool fullAtomicity,
    std::vector<int>* columnIndexToLockIndex,
    std::vector<TString>* lockIndexToName)
{
    if (columnIndexToLockIndex) {
        // Assign dummy lock indexes to key components.
        columnIndexToLockIndex->assign(schema.Columns().size(), -1);
    }

    if (lockIndexToName) {
        lockIndexToName->push_back(PrimaryLockName);
    }

    THashMap<TString, int> groupToIndex;
    if (fullAtomicity) {
        // Assign lock indexes to data components.
        for (int index = schema.GetKeyColumnCount(); index < std::ssize(schema.Columns()); ++index) {
            const auto& columnSchema = schema.Columns()[index];
            int lockIndex = PrimaryLockIndex;

            if (columnSchema.Lock()) {
                auto emplaced = groupToIndex.emplace(*columnSchema.Lock(), groupToIndex.size() + 1);
                if (emplaced.second && lockIndexToName) {
                    lockIndexToName->push_back(*columnSchema.Lock());
                }
                lockIndex = emplaced.first->second;
            }

            if (columnIndexToLockIndex) {
                (*columnIndexToLockIndex)[index] = lockIndex;
            }
        }
    } else if (columnIndexToLockIndex) {
        // No locking supported for non-atomic tablets, however we still need the primary
        // lock descriptor to maintain last commit timestamps.
        for (int index = schema.GetKeyColumnCount(); index < std::ssize(schema.Columns()); ++index) {
            (*columnIndexToLockIndex)[index] = PrimaryLockIndex;
        }
    }
    return groupToIndex;
}

TLockMask GetLockMask(
    const NTableClient::TTableSchema& schema,
    bool fullAtomicity,
    const std::vector<TString>& locks,
    ELockType lockType)
{
    auto groupToIndex = GetLocksMapping(schema, fullAtomicity);

    TLockMask lockMask;
    for (const auto& lock : locks) {
        auto it = groupToIndex.find(lock);
        if (it != groupToIndex.end()) {
            lockMask.Set(it->second, lockType);
        } else {
            THROW_ERROR_EXCEPTION("Lock group %Qv not found in schema", lock);
        }
    }
    return lockMask;
}

////////////////////////////////////////////////////////////////////////////////

namespace NProto {

using NYT::ToProto;
using NYT::FromProto;

void ToProto(TKeyColumnsExt* protoKeyColumns, const TKeyColumns& keyColumns)
{
    ToProto(protoKeyColumns->mutable_names(), keyColumns);
}

void FromProto(TKeyColumns* keyColumns, const TKeyColumnsExt& protoKeyColumns)
{
    *keyColumns = FromProto<TKeyColumns>(protoKeyColumns.names());
}

void ToProto(TColumnFilter* protoColumnFilter, const NTableClient::TColumnFilter& columnFilter)
{
    if (!columnFilter.IsUniversal()) {
        for (auto index : columnFilter.GetIndexes()) {
            protoColumnFilter->add_indexes(index);
        }
    }
}

void FromProto(NTableClient::TColumnFilter* columnFilter, const TColumnFilter& protoColumnFilter)
{
    *columnFilter = protoColumnFilter.indexes().empty()
        ? NTableClient::TColumnFilter()
        : NTableClient::TColumnFilter(FromProto<NTableClient::TColumnFilter::TIndexes>(protoColumnFilter.indexes()));
}

} // namespace NProto

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NTableClient

size_t THash<NYT::NTableClient::TColumnSchema>::operator()(const NYT::NTableClient::TColumnSchema& columnSchema) const
{
    return MultiHash(
        columnSchema.Name(),
        *columnSchema.LogicalType(),
        columnSchema.SortOrder(),
        columnSchema.Lock(),
        columnSchema.Expression(),
        columnSchema.Aggregate(),
        columnSchema.Group(),
        columnSchema.MaxInlineHunkSize());
}

size_t THash<NYT::NTableClient::TTableSchema>::operator()(const NYT::NTableClient::TTableSchema& tableSchema) const
{
    size_t result = CombineHashes(THash<bool>()(tableSchema.GetUniqueKeys()), THash<bool>()(tableSchema.GetStrict()));
    if (tableSchema.HasNontrivialSchemaModification()) {
        result = CombineHashes(
            result,
            THash<NYT::NTableClient::ETableSchemaModification>()(tableSchema.GetSchemaModification()));
    }
    for (const auto& columnSchema : tableSchema.Columns()) {
        result = CombineHashes(result, THash<NYT::NTableClient::TColumnSchema>()(columnSchema));
    }
    return result;
}
