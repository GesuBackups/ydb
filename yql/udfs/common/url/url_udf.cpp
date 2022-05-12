#include <ydb/library/yql/public/udf/udf_helpers.h>
#include <ydb/library/yql/udfs/common/url_base/lib/url_base_udf.h>

#include <kernel/hosts/owner/owner.h>
#include <kernel/urlnorm/normalize.h>
#include <yweb/robot/dbscheeme/dbtypes.h>
#include <library/cpp/robots_txt/robots_txt.h>

namespace {
    using namespace NYql;
    using namespace NUdf;

    SIMPLE_UDF(TNormalizeWithDefaultHttpScheme, TOptional<char*>(TOptional<char*>)) {
        EMPTY_RESULT_ON_EMPTY_ARG(0);
        TString normUrl;
        if (NUrlNorm::NormalizeUrl(args[0].AsStringRef(), normUrl)) {
            return valueBuilder->NewString(normUrl);
        }
        return args[0];
    }

    SIMPLE_UDF(TGetOwner, char*(TAutoMap<char*>)) {
        return valueBuilder->NewString(
                TOwnerCanonizer::WithTrueOwners().GetUrlOwner(args[0].AsStringRef()));
    }

    SIMPLE_UDF(TIsAllowedByRobotsTxt, bool(TAutoMap<char*>, TOptional<char*>, ui32)) {
        Y_UNUSED(valueBuilder);
        if (!args[1]) {
            return TUnboxedValuePod(true);
        }

        const auto& robotsRef = args[1].AsStringRef();
        if (!robotsRef.Size()) {
            return TUnboxedValuePod(true);
        }
        const auto botId = args[2].Get<ui32>();
        if (botId >= robotstxtcfg::max_botid) {
            return TUnboxedValuePod(false);
        }
        TRobotsTxt robotsTxt({botId});
        const auto multirobots = reinterpret_cast<const host_multirobots_t*>(robotsRef.Data());
        const char* robotsPacked;
        if (multirobots->GetRobots(robotsPacked)) {
            robotsTxt.LoadPacked(robotsPacked);
        } else {
            return TUnboxedValuePod(true);
        }

        const std::string_view urlPath(GetPathAndQuery(args[0].AsStringRef()));
        const bool allowed = !robotsTxt.IsDisallow(botId, urlPath.data(), true);
        if (robotstxtcfg::IsExplicitDisallow(botId)) {
            return TUnboxedValuePod(allowed || !robotsTxt.IsDisallow(botId, urlPath.data(), false));
        } else {
            return TUnboxedValuePod(allowed);
        }
    }

    SIMPLE_MODULE(TUrlModule,
                  EXPORTED_URL_BASE_UDF,
                  TNormalizeWithDefaultHttpScheme,
                  TGetOwner,
                  TIsAllowedByRobotsTxt)
}

REGISTER_MODULES(TUrlModule)

