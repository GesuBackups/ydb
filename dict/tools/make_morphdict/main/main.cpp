#include <kernel/lemmer/new_engine/build_lib/common.h>

#include <library/cpp/getopt/last_getopt.h>
#include <library/cpp/getopt/modchooser.h>
#include <library/cpp/langs/langs.h>
#include <kernel/lemmer/alpha/abc.h>

#include <util/generic/string.h>
#include <util/stream/input.h>
#include <util/stream/output.h>
#include <util/stream/file.h>

using namespace NLastGetopt;
using NDictBuild::TDictBuildParams;

void AddLanguageOpt(TOpts& opts, TDictBuildParams& params, bool isOptional) {
    auto& opt = opts.AddLongOption("language", "language (for diamask")
                    .DefaultValue("unk")
                    .StoreMappedResultT<TString, ELanguage>(&params.Language, [](const TString& lang) { return LanguageByName(lang); });
    if (isOptional) {
        opt.Optional();
    } else {
        opt.Required();
    }
}

void AddSchemeOpt(TOpts& opts, TDictBuildParams& params) {
    opts.AddLongOption("string", "Treat grammems as an ordered list of strings")
        .NoArgument()
        .StoreResult(&params.SchemesAsList, true);
    opts.AddLongOption("lcs", "Determine the stem as the longest common substring")
        .NoArgument()
        .StoreResult(&params.SchemesLcs, true);
}

void AddDictBuildOpt(TOpts& opts, TDictBuildParams& params) {
    opts.AddLongOption("fio-mode", "enables fio specific processing")
        .NoArgument()
        .StoreResult(&params.FioMode, true);
    opts.AddLongOption("bastard-freq", "last non rare rule id")
        .DefaultValue("400")
        .Required()
        .StoreResult(&params.BastardFreq);

    opts.AddCharOption('d', "Save diamask")
        .StoreResult(&params.UseDiaMask, true)
        .NoArgument();
    opts.AddCharOption('f', "Save freqs")
        .StoreResult(&params.UseFreqs, true)
        .NoArgument();
    opts.AddCharOption('g', "Detach grammar")
        .StoreResult(&params.DetachGrammar, true)
        .NoArgument();
    opts.AddLongOption("no-generation", "Disable form generation")
        .StoreResult(&params.UseGeneration, false)
        .NoArgument();
    opts.AddLongOption("fingerprint", "Save fingerprint")
        .RequiredArgument("<fingerprint>")
        .StoreResult(&params.Fingerprint);
    opts.AddLongOption("make-proto-dict", "Make protoDict")
        .StoreResult(&params.MakeProtoDict, true)
        .NoArgument();
}

int ModeNormalize(int argc, const char** argv) {
    TOpts opts = TOpts::Default();
    opts.SetFreeArgsNum(0);
    opts.AddHelpOption('h');
    TDictBuildParams params;
    AddLanguageOpt(opts, params, false);
    TOptsParseResult res(&opts, argc, argv);
    NDictBuild::Normalize(Cin, Cout, params);
    return 0;
}

int ModeMakeSchemes(int argc, const char** argv) {
    TOpts opts = TOpts::Default();
    opts.SetFreeArgsNum(0);
    opts.AddHelpOption('h');
    TString schemesFileName;
    opts.AddLongOption("schemes-file").Required().StoreResult(&schemesFileName);
    TString grammarFileName;
    opts.AddLongOption("grammar-file").Required().StoreResult(&grammarFileName);

    TDictBuildParams params;
    AddSchemeOpt(opts, params);

    TOptsParseResult res(&opts, argc, argv);
    TFileOutput schemesFile(schemesFileName);
    TFileOutput grammarFile(grammarFileName);
    NDictBuild::MakeSchemes(
        Cin,
        schemesFile,
        grammarFile,
        params);
    return 0;
}

int ModeMakeDict(int argc, const char** argv) {
    TOpts opts = TOpts::Default();
    opts.SetFreeArgsNum(0);
    opts.AddHelpOption('h');
    TString schemesFileName;
    opts.AddLongOption("schemes-file")
        .Required()
        .StoreResult(&schemesFileName);
    TString grammarFileName;
    opts.AddLongOption("grammar-file")
        .Required()
        .StoreResult(&grammarFileName);

    TDictBuildParams params;
    AddLanguageOpt(opts, params, true);
    AddDictBuildOpt(opts, params);

    TOptsParseResult res(&opts, argc, argv);

    TFileInput schemesFile(schemesFileName);
    TFileInput grammarFile(grammarFileName);
    NDictBuild::MakeDict(
        schemesFile,
        grammarFile,
        Cout,
        params);
    return 0;
}

int ModeMakeAll(int argc, const char** argv) {
    TOpts opts = TOpts::Default();
    opts.SetFreeArgsNum(0);
    opts.AddHelpOption('h');

    TDictBuildParams params;
    AddLanguageOpt(opts, params, false);
    AddSchemeOpt(opts, params);
    AddDictBuildOpt(opts, params);
    opts.AddLongOption("verbose", "be verbose")
        .NoArgument()
        .StoreResult(&params.Verbose, true);

    TString inputFileName;
    opts.AddLongOption("input")
        .DefaultValue("-")
        .RequiredArgument("FILE")
        .Help("input file")
        .StoreResult(&inputFileName);

    TOptsParseResult res(&opts, argc, argv);
    if (inputFileName == "-") {
        NDictBuild::MakeAll(Cin, Cout, params);
    } else {
        TFileInput fi(inputFileName);
        NDictBuild::MakeAll(fi, Cout, params);
    }

    return 0;
}

int main(int argc, const char** argv) {
    TModChooser modChooser;
    modChooser.AddMode(
        "normalize",
        ModeNormalize,
        "Normalizes standard input for given language");
    modChooser.AddMode(
        "make-schemes",
        ModeMakeSchemes,
        "Make schemes and grammar files");
    modChooser.AddMode(
        "make-dict",
        ModeMakeDict,
        "Main part: produce single binary file from schemes and grammar");
    modChooser.AddMode(
        "make-all",
        ModeMakeAll,
        "Do everything at once: lister -> dict.bin");
    modChooser.Run(argc, argv);
    return 0;
}
