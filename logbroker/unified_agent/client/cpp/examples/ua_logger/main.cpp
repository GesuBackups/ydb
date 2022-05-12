#include <logbroker/unified_agent/client/cpp/logger/backend.h>

#include <library/cpp/getopt/opt.h>
#include <library/cpp/logger/global/global.h>

class TOptions {
public:
    TString Uri;

    TOptions(int argc, const char* argv[]) {
        NLastGetopt::TOpts opts;
        TString logPriorityStr;

        opts
            .AddLongOption("uri")
            .RequiredArgument()
            .Required()
            .StoreResult(&Uri);

        opts.AddHelpOption();
        opts.AddVersionOption();
        NLastGetopt::TOptsParseResult res(&opts, argc, argv);
    }
};

int main(int argc, const char* argv[]) {
    TLog emptyLog;
    {
        TOptions options(argc, argv);
        auto clientParameters = NUnifiedAgent::TClientParameters(options.Uri).SetLog(emptyLog);
        auto logBackend = NUnifiedAgent::MakeLogBackend(clientParameters);
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
