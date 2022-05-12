#include <logbroker/unified_agent/client/cpp/yd_logger/yd_backend.h>

#include <library/cpp/getopt/opt.h>
#include <library/cpp/logger/global/global.h>

class TYdOptions {
public:
    TString Uri;
    TString LoggerName;

    TYdOptions(int argc, const char* argv[]) {
        NLastGetopt::TOpts opts;
        TString logPriorityStr;

        opts
            .AddLongOption("uri")
            .DefaultValue("")
            .StoreResult(&Uri);
        opts    
            .AddLongOption("logger_name")
            .DefaultValue("")
            .StoreResult(&LoggerName);

        opts.AddHelpOption();
        opts.AddVersionOption();
        NLastGetopt::TOptsParseResult res(&opts, argc, argv);
    }
};

int main(int argc, const char* argv[]) {
    TLog emptyLog;
    {
        TYdOptions options(argc, argv);
        auto clientParameters = NUnifiedAgent::TClientParameters(options.Uri).SetLog(emptyLog);
        auto logBackend = NUnifiedAgent::MakeYdLogBackend(options.LoggerName, clientParameters);
        DoInitGlobalLog(std::move(logBackend));
    }

    TString line;
    while (true) {
        Cin.ReadLine(line);
        if (line.Empty()) {
            break;
        }
        INFO_LOG << line << Endl;
    }

    return 0;
}
