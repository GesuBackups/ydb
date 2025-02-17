#include "pq_schema_actor.h"

#include <ydb/library/persqueue/obfuscate/obfuscate.h>
#include <ydb/library/persqueue/topic_parser/topic_parser.h>

#include <ydb/public/lib/jwt/jwt.h>

#include <ydb/library/yql/public/decimal/yql_decimal.h>

#include <util/string/vector.h>

namespace NKikimr::NGRpcProxy::V1 {

    constexpr TStringBuf GRPCS_ENDPOINT_PREFIX = "grpcs://";
    constexpr i64 DEFAULT_MAX_DATABASE_MESSAGEGROUP_SEQNO_RETENTION_PERIOD_MS =
        TDuration::Days(16).MilliSeconds();
    constexpr ui64 DEFAULT_PARTITION_SPEED = 1_MB;
    constexpr i32 MAX_READ_RULES_COUNT = 3000;
    constexpr i32 MAX_SUPPORTED_CODECS_COUNT = 100;

    TClientServiceTypes GetSupportedClientServiceTypes(const TActorContext& ctx) {
        TClientServiceTypes serviceTypes;
        const auto& pqConfig = AppData(ctx)->PQConfig;
        ui32 count = pqConfig.GetDefaultClientServiceType().GetMaxReadRulesCountPerTopic();
        if (count == 0) count = Max<ui32>();
        TString name = pqConfig.GetDefaultClientServiceType().GetName();
        serviceTypes.insert({name, {name, count}});
        for (const auto& serviceType : pqConfig.GetClientServiceType()) {
            ui32 count = serviceType.GetMaxReadRulesCountPerTopic();
            if (count == 0) count = Max<ui32>();
            TString name = serviceType.GetName();
            serviceTypes.insert({name, {name, count}});
        }
        return serviceTypes;
    }

    TString ReadRuleServiceTypeMigration(NKikimrPQ::TPQTabletConfig *config, const TActorContext& ctx) {
        auto rrServiceTypes = config->MutableReadRuleServiceTypes();
        if (config->ReadRuleServiceTypesSize() > config->ReadRulesSize()) {
            rrServiceTypes->Clear();
        }
        if (config->ReadRuleServiceTypesSize() < config->ReadRulesSize()) {
            rrServiceTypes->Reserve(config->ReadRulesSize());
            const auto& pqConfig = AppData(ctx)->PQConfig;
            if (pqConfig.GetDisallowDefaultClientServiceType()) {
                return "service type must be set for all read rules";
            }
            for (ui64 i = rrServiceTypes->size(); i < config->ReadRulesSize(); ++i) {
                *rrServiceTypes->Add() = pqConfig.GetDefaultClientServiceType().GetName();
            }
        }
        return "";
    }

    TString AddReadRuleToConfig(
        NKikimrPQ::TPQTabletConfig* config,
        const Ydb::PersQueue::V1::TopicSettings::ReadRule& rr,
        const TClientServiceTypes& supportedClientServiceTypes,
        const TActorContext& ctx
    ) {
        auto consumerName = NPersQueue::ConvertNewConsumerName(rr.consumer_name(), ctx);
        if(consumerName.find("/") != TString::npos || consumerName.find("|") != TString::npos) {
            return TStringBuilder() << "consumer '" << rr.consumer_name() << "' has illegal symbols";
        }
        {
            TString migrationError = ReadRuleServiceTypeMigration(config, ctx);
            if (migrationError) {
                return migrationError;
            }
        }

        config->AddReadRules(consumerName);

        if (rr.starting_message_timestamp_ms() < 0) {
            return TStringBuilder() << "starting_message_timestamp_ms in read_rule can't be negative, provided " << rr.starting_message_timestamp_ms();
        }
        config->AddReadFromTimestampsMs(rr.starting_message_timestamp_ms());

        if (!Ydb::PersQueue::V1::TopicSettings::Format_IsValid((int)rr.supported_format()) || rr.supported_format() == 0) {
            return TStringBuilder() << "Unknown format version with value " << (int)rr.supported_format()  << " for " << rr.consumer_name();
        }
        config->AddConsumerFormatVersions(rr.supported_format() - 1);

        if (rr.version() < 0) {
            return TStringBuilder() << "version in read_rule can't be negative, provided " << rr.version();
        }
        config->AddReadRuleVersions(rr.version());
        auto ct = config->AddConsumerCodecs();
        if (rr.supported_codecs().size() > MAX_SUPPORTED_CODECS_COUNT) {
            return TStringBuilder() << "supported_codecs count cannot be more than "
                                    << MAX_SUPPORTED_CODECS_COUNT << ", provided " << rr.supported_codecs().size();
        }
        for (const auto& codec : rr.supported_codecs()) {
            if (!Ydb::PersQueue::V1::Codec_IsValid(codec) || codec == 0)
                return TStringBuilder() << "Unknown codec with value " << codec  << " for " << rr.consumer_name();
            ct->AddIds(codec - 1);
            ct->AddCodecs(to_lower(Ydb::PersQueue::V1::Codec_Name((Ydb::PersQueue::V1::Codec)codec)).substr(6));
        }

        if (rr.important())
            config->MutablePartitionConfig()->AddImportantClientId(consumerName);

        if (!rr.service_type().empty()) {
            if (!supportedClientServiceTypes.contains(rr.service_type())) {
                return TStringBuilder() << "Unknown read rule service type '" << rr.service_type()
                                        << "' for consumer '" << rr.consumer_name() << "'";
            }
            config->AddReadRuleServiceTypes(rr.service_type());
        } else {
            const auto& pqConfig = AppData(ctx)->PQConfig;
            if (pqConfig.GetDisallowDefaultClientServiceType()) {
                return TStringBuilder() << "service type cannot be empty for consumer '" << rr.consumer_name() << "'";
            }
            const auto& defaultCientServiceType = pqConfig.GetDefaultClientServiceType().GetName();
            config->AddReadRuleServiceTypes(defaultCientServiceType);
        }
        return "";
    }

    TString RemoveReadRuleFromConfig(
        NKikimrPQ::TPQTabletConfig* config,
        const NKikimrPQ::TPQTabletConfig& originalConfig,
        const TString& consumerName,
        const TActorContext& ctx
    ) {
        THashSet<TString> rulesToRemove;
        rulesToRemove.insert(consumerName);

        config->ClearReadRuleVersions();
        config->ClearReadRules();
        config->ClearReadFromTimestampsMs();
        config->ClearConsumerFormatVersions();
        config->ClearConsumerCodecs();
        config->MutablePartitionConfig()->ClearImportantClientId();
        config->ClearReadRuleServiceTypes();

        for (const auto& importantConsumer : originalConfig.GetPartitionConfig().GetImportantClientId()) {
            if (rulesToRemove.find(importantConsumer) == rulesToRemove.end()) {
                config->MutablePartitionConfig()->AddImportantClientId(importantConsumer);
            }
        }

        const auto& pqConfig = AppData(ctx)->PQConfig;
        for (size_t i = 0; i < originalConfig.ReadRulesSize(); i++) {
            if (auto it = rulesToRemove.find(originalConfig.GetReadRules(i)); it != rulesToRemove.end()) {
                rulesToRemove.erase(it);
                continue;
            }

            config->AddReadRuleVersions(originalConfig.GetReadRuleVersions(i));
            config->AddReadRules(originalConfig.GetReadRules(i));
            config->AddReadFromTimestampsMs(originalConfig.GetReadFromTimestampsMs(i));
            config->AddConsumerFormatVersions(originalConfig.GetConsumerFormatVersions(i));
            auto ct = config->AddConsumerCodecs();
            for (size_t j = 0; j < originalConfig.GetConsumerCodecs(i).CodecsSize(); j++) {
                ct->AddCodecs(originalConfig.GetConsumerCodecs(i).GetCodecs(j));
                ct->AddIds(originalConfig.GetConsumerCodecs(i).GetIds(j));
            }
            if (i < originalConfig.ReadRuleServiceTypesSize()) {
                config->AddReadRuleServiceTypes(originalConfig.GetReadRuleServiceTypes(i));
            } else {
                if (pqConfig.GetDisallowDefaultClientServiceType()) {
                    return TStringBuilder() << "service type cannot be empty for consumer '"
                        << originalConfig.GetReadRules(i) << "'";
                }
                config->AddReadRuleServiceTypes(pqConfig.GetDefaultClientServiceType().GetName());
            }
        }

        if (rulesToRemove.size() > 0) {
            return TStringBuilder() << "Rule for consumer " << *rulesToRemove.begin() << " doesn't exist";
        }

        return "";
    }

    bool CheckReadRulesConfig(const NKikimrPQ::TPQTabletConfig& config, const TClientServiceTypes& supportedClientServiceTypes,
                                TString& error) {

        if (config.GetReadRules().size() > MAX_READ_RULES_COUNT) {
            error = TStringBuilder() << "read rules count cannot be more than "
                                     << MAX_SUPPORTED_CODECS_COUNT << ", provided " << config.GetReadRules().size();
            return false;
        }

        THashSet<TString> readRuleConsumers;
        for (auto consumerName : config.GetReadRules()) {
            if (readRuleConsumers.find(consumerName) != readRuleConsumers.end()) {
                error = TStringBuilder() << "Duplicate consumer name " << consumerName;
                return true;
            }
            readRuleConsumers.insert(consumerName);
        }

        for (const auto& t : supportedClientServiceTypes) {

            auto type = t.first;
            auto count = std::count_if(config.GetReadRuleServiceTypes().begin(), config.GetReadRuleServiceTypes().end(),
                        [type](const TString& cType){
                            return type == cType;
                        });
            auto limit = t.second.MaxCount;
            if (count > limit) {
                error = TStringBuilder() << "Count of consumers with service type '" << type << "' is limited for " << limit << " for stream\n";
                return false;
            }
        }

        return false;
    }

    NYql::TIssue FillIssue(const TString &errorReason, const Ydb::PersQueue::ErrorCode::ErrorCode errorCode) {
        NYql::TIssue res(NYql::TPosition(), errorReason);
        res.SetCode(errorCode, NYql::ESeverity::TSeverityIds_ESeverityId_S_ERROR);

        return res;
    }

    Ydb::StatusIds::StatusCode FillProposeRequestImpl(const TString& name, const Ydb::PersQueue::V1::TopicSettings& settings,
                                                      NKikimrSchemeOp::TModifyScheme& modifyScheme, const TActorContext& ctx, bool alter, TString& error)
    {
        modifyScheme.SetOperationType(alter ? NKikimrSchemeOp::EOperationType::ESchemeOpAlterPersQueueGroup : NKikimrSchemeOp::EOperationType::ESchemeOpCreatePersQueueGroup);

        auto pqDescr = alter ? modifyScheme.MutableAlterPersQueueGroup() : modifyScheme.MutableCreatePersQueueGroup();
        pqDescr->SetName(name);
        if (settings.partitions_count() <= 0) {
            error = TStringBuilder() << "Partitions count must be positive, provided " << settings.partitions_count();
            return Ydb::StatusIds::BAD_REQUEST;
        }

        pqDescr->SetTotalGroupCount(settings.partitions_count());

        auto config = pqDescr->MutablePQTabletConfig();

        config->SetRequireAuthWrite(true);
        config->SetRequireAuthRead(true);
        if (!alter)
            pqDescr->SetPartitionPerTablet(2);

        for (auto& pair : settings.attributes()) {
            if (pair.first == "_partitions_per_tablet") {
                try {
                    if (!alter)
                        pqDescr->SetPartitionPerTablet(FromString<ui32>(pair.second));
                    if (pqDescr->GetPartitionPerTablet() > 20) {
                        error = TStringBuilder() << "Attribute partitions_per_tablet is " << pair.second << ", which is greater than 20";
                        return Ydb::StatusIds::BAD_REQUEST;
                    }
                } catch(...) {
                    error = TStringBuilder() << "Attribute partitions_per_tablet is " << pair.second << ", which is not ui32";
                    return Ydb::StatusIds::BAD_REQUEST;
                }
            } else if (pair.first == "_allow_unauthenticated_read") {
                try {
                    config->SetRequireAuthRead(!FromString<bool>(pair.second));
                } catch(...) {
                    error = TStringBuilder() << "Attribute allow_unauthenticated_read is " << pair.second << ", which is not bool";
                    return Ydb::StatusIds::BAD_REQUEST;
                }
            } else if (pair.first == "_allow_unauthenticated_write") {
                try {
                    config->SetRequireAuthWrite(!FromString<bool>(pair.second));
                } catch(...) {
                    error = TStringBuilder() << "Attribute allow_unauthenticated_write is " << pair.second << ", which is not bool";
                    return Ydb::StatusIds::BAD_REQUEST;
                }
            } else if (pair.first == "_abc_slug") {
                config->SetAbcSlug(pair.second);
            } else if (pair.first == "_abc_id") {
                try {
                    config->SetAbcId(!FromString<ui32>(pair.second));
                } catch(...) {
                    error = TStringBuilder() << "Attribute abc_id is " << pair.second << ", which is not integer";
                    return Ydb::StatusIds::BAD_REQUEST;
                }
            } else {
                error = TStringBuilder() << "Attribute " << pair.first << " is not supported";
                return Ydb::StatusIds::BAD_REQUEST;
            }
        }
        bool local = !settings.client_write_disabled();
        config->SetLocalDC(local);
        config->SetDC(NPersQueue::GetDC(name));
        config->SetTopicName(name);
        config->SetTopicPath(NKikimr::JoinPath({modifyScheme.GetWorkingDir(), name}));
        config->SetProducer(NPersQueue::GetProducer(name));
        config->SetTopic(LegacySubstr(NPersQueue::GetRealTopic(name), config->GetProducer().size() + 2));
        config->SetIdent(config->GetProducer());
        auto partConfig = config->MutablePartitionConfig();

        const auto& channelProfiles = AppData(ctx)->PQConfig.GetChannelProfiles();
        if (channelProfiles.size() > 2) {
            partConfig->SetNumChannels(channelProfiles.size() - 2); // channels 0,1 are reserved in tablet
            partConfig->MutableExplicitChannelProfiles()->CopyFrom(channelProfiles);
        }
        if (settings.max_partition_storage_size() < 0) {
            error = TStringBuilder() << "Max_partiton_strorage_size must can't be negative, provided " << settings.max_partition_storage_size();
            return Ydb::StatusIds::BAD_REQUEST;
        }
        partConfig->SetMaxSizeInPartition(settings.max_partition_storage_size() ? settings.max_partition_storage_size() : Max<i64>());
        partConfig->SetMaxCountInPartition(Max<i32>());

        if (settings.retention_period_ms() <= 0) {
            error = TStringBuilder() << "retention_period_ms must be positive, provided " << settings.retention_period_ms();
            return Ydb::StatusIds::BAD_REQUEST;
        }
        partConfig->SetLifetimeSeconds(settings.retention_period_ms() > 999 ? settings.retention_period_ms() / 1000 : 1);

        if (settings.message_group_seqno_retention_period_ms() > 0 && settings.message_group_seqno_retention_period_ms() < settings.retention_period_ms()) {
            error = TStringBuilder() << "message_group_seqno_retention_period_ms (provided " << settings.message_group_seqno_retention_period_ms() << ") must be more then retention_period_ms (provided " << settings.retention_period_ms() << ")";
            return Ydb::StatusIds::BAD_REQUEST;
        }
        if (settings.message_group_seqno_retention_period_ms() >
            DEFAULT_MAX_DATABASE_MESSAGEGROUP_SEQNO_RETENTION_PERIOD_MS) {
            error = TStringBuilder() <<
                "message_group_seqno_retention_period_ms (provided " <<
                settings.message_group_seqno_retention_period_ms() <<
                ") must be less then default limit for database " <<
                DEFAULT_MAX_DATABASE_MESSAGEGROUP_SEQNO_RETENTION_PERIOD_MS;
            return Ydb::StatusIds::BAD_REQUEST;
        }
        if (settings.message_group_seqno_retention_period_ms() < 0) {
            error = TStringBuilder() << "message_group_seqno_retention_period_ms can't be negative, provided " << settings.message_group_seqno_retention_period_ms();
            return Ydb::StatusIds::BAD_REQUEST;
        }
        if (settings.message_group_seqno_retention_period_ms() > 0) {
            partConfig->SetSourceIdLifetimeSeconds(settings.message_group_seqno_retention_period_ms() > 999 ? settings.message_group_seqno_retention_period_ms() / 1000 :1);
        } else {
            // default value
            partConfig->SetSourceIdLifetimeSeconds(NKikimrPQ::TPartitionConfig().GetSourceIdLifetimeSeconds());
        }

        if (settings.max_partition_message_groups_seqno_stored() < 0) {
            error = TStringBuilder() << "max_partition_message_groups_seqno_stored can't be negative, provided " << settings.max_partition_message_groups_seqno_stored();
            return Ydb::StatusIds::BAD_REQUEST;
        }
        if (settings.max_partition_message_groups_seqno_stored() > 0) {
            partConfig->SetSourceIdMaxCounts(settings.max_partition_message_groups_seqno_stored());
        } else {
            // default value
            partConfig->SetSourceIdMaxCounts(NKikimrPQ::TPartitionConfig().GetSourceIdMaxCounts());
        }

        if (local) {
            auto partSpeed = settings.max_partition_write_speed();
            if (partSpeed < 0) {
                error = TStringBuilder() << "max_partition_write_speed can't be negative, provided " << partSpeed;
                return Ydb::StatusIds::BAD_REQUEST;
            } else if (partSpeed == 0) {
                partSpeed = DEFAULT_PARTITION_SPEED;
            }
            partConfig->SetWriteSpeedInBytesPerSecond(partSpeed);

            const auto& burstSpeed = settings.max_partition_write_burst();
            if (burstSpeed < 0) {
                error = TStringBuilder() << "max_partition_write_burst can't be negative, provided " << burstSpeed;
                return Ydb::StatusIds::BAD_REQUEST;
            } else if (burstSpeed == 0) {
                partConfig->SetBurstSize(partSpeed);
            } else {
                partConfig->SetBurstSize(burstSpeed);
            }
        }

        if (!Ydb::PersQueue::V1::TopicSettings::Format_IsValid((int)settings.supported_format()) || settings.supported_format() == 0) {
            error = TStringBuilder() << "Unknown format version with value " << (int)settings.supported_format();
            return Ydb::StatusIds::BAD_REQUEST;
        }
        config->SetFormatVersion(settings.supported_format() - 1);

        auto ct = config->MutableCodecs();
        if (settings.supported_codecs().size() > MAX_SUPPORTED_CODECS_COUNT) {
            error = TStringBuilder() << "supported_codecs count cannot be more than "
                                     << MAX_SUPPORTED_CODECS_COUNT << ", provided " << settings.supported_codecs().size();
            return Ydb::StatusIds::BAD_REQUEST;

        }
        for(const auto& codec : settings.supported_codecs()) {
            if (!Ydb::PersQueue::V1::Codec_IsValid(codec) || codec == 0) {
                error = TStringBuilder() << "Unknown codec with value " << codec;
                return Ydb::StatusIds::BAD_REQUEST;
            }
            ct->AddIds(codec - 1);
            ct->AddCodecs(LegacySubstr(to_lower(Ydb::PersQueue::V1::Codec_Name((Ydb::PersQueue::V1::Codec)codec)), 6));
        }

        //TODO: check all values with defaults

        if (settings.read_rules().size() > MAX_READ_RULES_COUNT) {
            error = TStringBuilder() << "read rules count cannot be more than "
                                     << MAX_SUPPORTED_CODECS_COUNT << ", provided " << settings.read_rules().size();
            return Ydb::StatusIds::BAD_REQUEST;
        }

        {
            error = ReadRuleServiceTypeMigration(config, ctx);
            if (error) {
                return Ydb::StatusIds::INTERNAL_ERROR;
            }
        }
        const auto& supportedClientServiceTypes = GetSupportedClientServiceTypes(ctx);
        for (const auto& rr : settings.read_rules()) {
            error = AddReadRuleToConfig(config, rr, supportedClientServiceTypes, ctx);
            if (!error.Empty()) {
                return Ydb::StatusIds::BAD_REQUEST;
            }
        }
        if (settings.has_remote_mirror_rule()) {
            auto mirrorFrom = partConfig->MutableMirrorFrom();
            {
                TString endpoint = settings.remote_mirror_rule().endpoint();
                if (endpoint.StartsWith(GRPCS_ENDPOINT_PREFIX)) {
                    mirrorFrom->SetUseSecureConnection(true);
                    endpoint = TString(endpoint.begin() + GRPCS_ENDPOINT_PREFIX.size(), endpoint.end());
                }
                auto parts = SplitString(endpoint, ":");
                if (parts.size() != 2) {
                    error = TStringBuilder() << "endpoint in remote mirror rule must be in format [grpcs://]server:port, but got '"
                                             << settings.remote_mirror_rule().endpoint() << "'";
                    return Ydb::StatusIds::BAD_REQUEST;
                }
                ui16 port;
                if (!TryFromString(parts[1], port)) {
                    error = TStringBuilder() << "cannot parse port from endpoint ('" << settings.remote_mirror_rule().endpoint() << "') for remote mirror rule";
                    return Ydb::StatusIds::BAD_REQUEST;
                }
                mirrorFrom->SetEndpoint(parts[0]);
                mirrorFrom->SetEndpointPort(port);
            }
            mirrorFrom->SetTopic(settings.remote_mirror_rule().topic_path());
            mirrorFrom->SetConsumer(settings.remote_mirror_rule().consumer_name());
            if (settings.remote_mirror_rule().starting_message_timestamp_ms() < 0) {
                error = TStringBuilder() << "starting_message_timestamp_ms in remote_mirror_rule can't be negative, provided "
                                         << settings.remote_mirror_rule().starting_message_timestamp_ms();
                return Ydb::StatusIds::BAD_REQUEST;
            }
            mirrorFrom->SetReadFromTimestampsMs(settings.remote_mirror_rule().starting_message_timestamp_ms());
            if (!settings.remote_mirror_rule().has_credentials()) {
                error = "credentials for remote mirror rule must be set";
                return Ydb::StatusIds::BAD_REQUEST;
            }
            const auto& credentials = settings.remote_mirror_rule().credentials();
            switch (credentials.credentials_case()) {
                case Ydb::PersQueue::V1::Credentials::kOauthToken: {
                    mirrorFrom->MutableCredentials()->SetOauthToken(credentials.oauth_token());
                    break;
                }
                case Ydb::PersQueue::V1::Credentials::kJwtParams: {
                    try {
                        auto res = NYdb::ParseJwtParams(credentials.jwt_params());
                        NYdb::MakeSignedJwt(res);
                    } catch (...) {
                        error = TStringBuilder() << "incorrect jwt params in remote mirror rule: " << CurrentExceptionMessage();
                        return Ydb::StatusIds::BAD_REQUEST;
                    }
                    mirrorFrom->MutableCredentials()->SetJwtParams(credentials.jwt_params());
                    break;
                }
                case Ydb::PersQueue::V1::Credentials::kIam: {
                    try {
                        auto res = NYdb::ParseJwtParams(credentials.iam().service_account_key());
                        NYdb::MakeSignedJwt(res);
                    } catch (...) {
                        error = TStringBuilder() << "incorrect service account key for iam in remote mirror rule: " << CurrentExceptionMessage();
                        return Ydb::StatusIds::BAD_REQUEST;
                    }
                    if (credentials.iam().endpoint().empty()) {
                        error = "iam endpoint must be set in remote mirror rule";
                        return Ydb::StatusIds::BAD_REQUEST;
                    }
                    mirrorFrom->MutableCredentials()->MutableIam()->SetEndpoint(credentials.iam().endpoint());
                    mirrorFrom->MutableCredentials()->MutableIam()->SetServiceAccountKey(credentials.iam().service_account_key());
                    break;
                }
                case Ydb::PersQueue::V1::Credentials::CREDENTIALS_NOT_SET: {
                    error = "one of the credential fields must be filled for remote mirror rule";
                    return Ydb::StatusIds::BAD_REQUEST;
                }
                default: {
                    error = TStringBuilder() << "unsupported credentials type " << ::NPersQueue::ObfuscateString(ToString(credentials));
                    return Ydb::StatusIds::BAD_REQUEST;
                }
            }
            if (settings.remote_mirror_rule().database()) {
                mirrorFrom->SetDatabase(settings.remote_mirror_rule().database());
            }
        }

        CheckReadRulesConfig(*config, supportedClientServiceTypes, error);
        return error.empty() ? Ydb::StatusIds::SUCCESS : Ydb::StatusIds::BAD_REQUEST;
    }

}
