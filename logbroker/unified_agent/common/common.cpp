#include "common.h"

#include <logbroker/unified_agent/common/util/clock.h>
#include <ydb/library/yql/utils/backtrace/backtrace.h>

#include <util/folder/dirut.h>
#include <util/generic/size_literals.h>
#include <util/string/ascii.h>
#include <util/string/builder.h>
#include <util/string/printf.h>
#include <util/system/env.h>
#include <util/system/hostname.h>
#include <util/system/shellcommand.h>

extern char** environ;

namespace NUnifiedAgent {
    namespace {
        struct TDataUnit {
            TStringBuf Suffix;
            ui64 Factor;
        };

        TVector<TDataUnit> Units {
            {"kb"sv, 1_KB},
            {"mb"sv, 1_MB},
            {"gb"sv, 1_GB},
            {"tb"sv, 1_TB}
        };
    }

    TString ExpandHomePrefix(const TString& s) {
        static const auto homePrefix = TString("~/");
        return s.StartsWith(homePrefix)
            ? GetHomeDir() + "/" + s.substr(homePrefix.Size())
            : s;
    }

    TString CamelCaseToSnakeCase(const TStringBuf& str) {
        TStringBuilder builder;
        bool first = true;
        for (char c : str) {
            if (std::isupper(c) && std::isalpha(c)) {
                if (!first) {
                    builder << '_';
                }
                c = std::tolower(c);
            }
            builder << c;
            first = false;
        }
        return builder;
    }

    TString GetHostName(EHostNameStyle style) {
        auto result = GetEnv("UA_HOST_NAME");
        if (result.empty()) {
            result = HostName();
        }
        if (style == EHostNameStyle::Short) {
            const auto dotPos = result.find('.');
            result = result.substr(0, dotPos);
        }
        return result;
    }

    TString CaptureBacktrace() {
        TString result;
        {
            TStringOutput output(result);
            FormatBackTrace(&output);
        }
        return result;
    }

    THashMap<TString, TString> MergeCurrentEnv(THashMap<TString, TString>&& env) {
        for (const auto* const* s = environ; *s; ++s) {
            TStringBuf name, value;
            TStringBuf e(*s);
            e.Split('=', name, value);
            if (const auto it = env.find(name); it == env.end()) {
                env.insert({TString(name), TString(value)});
            }
        }

        return std::move(env);
    }

    bool TryParseDataSize(TStringBuf s, size_t& result) {
        for (const auto& u: Units) {
            if (AsciiHasSuffixIgnoreCase(s, u.Suffix) && TryFromString(s.Data(), s.Size() - u.Suffix.Size(), result)) {
                result *= u.Factor;
                return true;
            }
        }

        return TryFromString(s, result);
    }

    void ReportListenedPorts(TScopeLogger& logger) {
#ifdef _win_
        const auto* listListenedPortsCmd = "netstat -a -b";
#else
        const auto* listListenedPortsCmd = "ss -lpn";
#endif
        TShellCommand cmd(listListenedPortsCmd);
        TString listenedPorts;
        try {
            cmd.Run().Wait();
            Y_ENSURE(cmd.GetStatus() == TShellCommand::ECommandStatus::SHELL_FINISHED,
                "unexpected exit status");
            listenedPorts = '\n' + cmd.GetOutput();
        } catch (...) {
            listenedPorts = Sprintf("can't list listened ports, error [%s], "
                                    "exit code [%d], stdout [%s], stderr [%s]",
                ShortExceptionMessage().c_str(),
                *cmd.GetExitCode(),
                cmd.GetOutput().c_str(),
                cmd.GetError().c_str());
        }

        YLOG(TLOG_NOTICE,
            Sprintf("currently listened ports (%s): %s",
                listListenedPortsCmd,
                listenedPorts.c_str()),
            logger);
    }

    TString ShortExceptionMessage() {
        const auto exceptionPtr = std::current_exception();
        if (!exceptionPtr) {
            return "(NO EXCEPTION)";
        }
        try {
            std::rethrow_exception(exceptionPtr);
        } catch (const yexception& e) {
            return FormatExc(e);
        } catch (const std::exception& e) {
            return FormatExc(e);
        } catch (...) {
            return "unknown error";
        }
    }
}
