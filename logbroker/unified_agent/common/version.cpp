#include "version.h"

#include <library/cpp/build_info/build_info.h>
#include <library/cpp/resource/resource.h>
#include <library/cpp/svnversion/svnversion.h>

#include <util/folder/path.h>
#include <util/generic/hash.h>
#include <util/generic/singleton.h>
#include <util/stream/file.h>
#include <util/string/builder.h>
#include <util/string/escape.h>
#include <util/string/split.h>
#include <util/string/strip.h>

#ifdef _linux_
#include <sys/utsname.h>
#endif

namespace NUnifiedAgent {
    namespace {
#ifdef _linux_
        THashMap<TString, TString> ParseKvFile(const TString& filePath) {
            THashMap<TString, TString> result;
            if (!TFsPath(filePath).Exists()) {
                return result;
            }
            const auto fileContent = TFileInput(filePath).ReadAll();
            for (const auto& line: StringSplitter(fileContent).Split('\n')) {
                TString key;
                TString value;
                if (StringSplitter(line.Token()).Split('=').TryCollectInto(&key, &value)) {
                    result[key] = value;
                }
            }
            return result;
        }

        TString Unquote(const TString& s) {
            return s.StartsWith('"') && s.EndsWith('"') ? UnescapeC(s.substr(1, s.size() - 2)) : s;
        }
#endif

        TString GetUnifiedAgentVersion() {
            return Strip(NResource::Find("/unified_agent_version"));
        }

        TString BuildUserAgentDescription() {
            TStringBuilder builder;
            builder << "Unified Agent/" << GetUnifiedAgentVersion();

            builder << " (";
            builder << "build/" << GetProgramRevision();

#ifdef _linux_
            {
                //https://www.freedesktop.org/software/systemd/man/os-release.html
                const auto osRelease = ParseKvFile("/etc/os-release");
                builder << "; " << Unquote(osRelease.Value("NAME", "Linux"));
                if (auto it = osRelease.find("VERSION"); it != osRelease.end()) {
                    builder << "/" << Unquote(it->second);
                }
            }
            {
                struct utsname name;
                if (uname(&name) == 0) {
                    builder << "; " << name.sysname << " " << name.machine << "/" << name.release << "," << name.version;
                }
            }
#elif defined(_win_)
            // It is not an easy task to detect exact windows version, see e.g.
            // https://stackoverflow.com/questions/29944745/get-osversion-in-windows-using-c

            builder << "; Windows";
#elif defined(_darwin_)
            builder << "; MacOS";
#endif
            builder << ")";

            return builder;
        }

        struct TProgramDescription {
            TProgramDescription()
                : UserAgent("Unified Agent/" + GetUnifiedAgentVersion())
                , UserAgentDescription(BuildUserAgentDescription())
            {
            }

            TString UserAgent;
            TString UserAgentDescription;
        };
    }

    int GetProgramRevision() {
        return GetArcadiaLastChangeNum();
    }

    const TString& GetProgramUserAgentDescription() {
        return Singleton<TProgramDescription>()->UserAgentDescription;
    }

    const TString& GetProgramUserAgent() {
        return Singleton<TProgramDescription>()->UserAgent;
    }

    TString GetProgramVersionString() {
        TStringBuilder result;
        result << "Unified Agent version: " << GetUnifiedAgentVersion() << '/' << GetArcadiaLastChangeNum() << "\n\n";
        result << GetProgramSvnVersion();
        result << '\n';
        result << GetBuildInfo();
        if (const auto sandboxTaskId = GetSandboxTaskId(); sandboxTaskId != TString("0")) {
            result << "\nSandbox task id: ";
            result << sandboxTaskId;
        }
        return result;
    }

    TString GetCredits() {
        return NResource::Find("/unified_agent_credits");
    }

    TString GetLicense() {
        return NResource::Find("/unified_agent_license");
    }
}
