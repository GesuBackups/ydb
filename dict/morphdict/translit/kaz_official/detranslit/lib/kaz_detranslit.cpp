#include "kaz_detranslit.h"

#include <wctype.h>

#include <library/cpp/containers/comptrie/comptrie.h>
#include <library/cpp/unicode/normalization/normalization.h>

#include <util/charset/wide.h>
#include <util/generic/hash_set.h>
#include <util/generic/singleton.h>
#include <util/generic/string.h>
#include <util/generic/vector.h>
#include <util/string/vector.h>

namespace {
    extern "C" {
        extern const unsigned char KazDetranslitFixlist[];
        extern const ui32 KazDetranslitFixlistSize;
    }

    class TOfficialLikeKazDetransliter {
        enum class Sound {
            Vowel,
            Consonant,
            Unknown
        };

        struct TLetterMapVariants {
            TUtf16String Default;
            TUtf16String Special;

            enum class SpecialCondition {
                Nothing,
                AfterConsonant,
                AfterVowel
            };

            SpecialCondition Condition = SpecialCondition::Nothing;

            explicit TLetterMapVariants(const TUtf16String& def)
                : Default(def)
                , Special(Default)
            {
            }

            TLetterMapVariants(const TUtf16String& def, const TUtf16String& special, SpecialCondition condition)
                : Default(def)
                , Special(special)
                , Condition(condition)
            {
            }
        };

        const THashMap<TUtf16String, TLetterMapVariants> PairsMap{
            {u"ıý", TLetterMapVariants(u"ю", u"ию", TLetterMapVariants::SpecialCondition::AfterConsonant)},
            {u"ıa", TLetterMapVariants(u"я", u"ия", TLetterMapVariants::SpecialCondition::AfterConsonant)},
            {u"sh", TLetterMapVariants(u"ш")},
            {u"ch", TLetterMapVariants(u"ч")}};

        const THashMap<wchar16, TUtf16String> IrregularMap {
            {u'C', u"К"},
            {u'c', u"к"},
            {u'W', u"В"},
            {u'w', u"в"},
            {u'X', u"Кс"},
            {u'x', u"кс"}};

        const THashMap<wchar16, TLetterMapVariants> SingleMap{
            {u'I', TLetterMapVariants(u"И", u"Й", TLetterMapVariants::SpecialCondition::AfterVowel)},
            {u'ı', TLetterMapVariants(u"и", u"й", TLetterMapVariants::SpecialCondition::AfterVowel)},
            {u'Á', TLetterMapVariants(u"Ә")},
            {u'á', TLetterMapVariants(u"ә")},
            {u'Ó', TLetterMapVariants(u"Ө")},
            {u'ó', TLetterMapVariants(u"ө")},
            {u'Ú', TLetterMapVariants(u"Ү")},
            {u'ú', TLetterMapVariants(u"ү")},
            {u'Ń', TLetterMapVariants(u"Ң")},
            {u'ń', TLetterMapVariants(u"ң")},
            {u'Ǵ', TLetterMapVariants(u"Ғ")},
            {u'ǵ', TLetterMapVariants(u"ғ")},
            {u'Ý', TLetterMapVariants(u"У")},
            {u'ý', TLetterMapVariants(u"у")},
            {u'A', TLetterMapVariants(u"А")},
            {u'a', TLetterMapVariants(u"а")},
            {u'B', TLetterMapVariants(u"Б")},
            {u'b', TLetterMapVariants(u"б")},
            {u'S', TLetterMapVariants(u"С")},
            {u's', TLetterMapVariants(u"с")},
            {u'D', TLetterMapVariants(u"Д")},
            {u'd', TLetterMapVariants(u"д")},
            {u'E', TLetterMapVariants(u"Е")},
            {u'e', TLetterMapVariants(u"е")},
            {u'F', TLetterMapVariants(u"Ф")},
            {u'f', TLetterMapVariants(u"ф")},
            {u'G', TLetterMapVariants(u"Г")},
            {u'g', TLetterMapVariants(u"г")},
            {u'H', TLetterMapVariants(u"Х")},
            {u'h', TLetterMapVariants(u"х")},
            {u'İ', TLetterMapVariants(u"І")},
            {u'i', TLetterMapVariants(u"і")},
            {u'K', TLetterMapVariants(u"К")},
            {u'k', TLetterMapVariants(u"к")},
            {u'L', TLetterMapVariants(u"Л")},
            {u'l', TLetterMapVariants(u"л")},
            {u'M', TLetterMapVariants(u"М")},
            {u'm', TLetterMapVariants(u"м")},
            {u'N', TLetterMapVariants(u"Н")},
            {u'n', TLetterMapVariants(u"н")},
            {u'O', TLetterMapVariants(u"О")},
            {u'o', TLetterMapVariants(u"о")},
            {u'P', TLetterMapVariants(u"П")},
            {u'p', TLetterMapVariants(u"п")},
            {u'Q', TLetterMapVariants(u"Қ")},
            {u'q', TLetterMapVariants(u"қ")},
            {u'R', TLetterMapVariants(u"Р")},
            {u'r', TLetterMapVariants(u"р")},
            {u'T', TLetterMapVariants(u"Т")},
            {u't', TLetterMapVariants(u"т")},
            {u'U', TLetterMapVariants(u"Ұ")},
            {u'u', TLetterMapVariants(u"ұ")},
            {u'V', TLetterMapVariants(u"В")},
            {u'v', TLetterMapVariants(u"в")},
            {u'Y', TLetterMapVariants(u"Ы")},
            {u'y', TLetterMapVariants(u"ы")},
            {u'Z', TLetterMapVariants(u"З")},
            {u'z', TLetterMapVariants(u"з")},
            {u'J', TLetterMapVariants(u"Ж")},
            {u'j', TLetterMapVariants(u"ж")}};

        const THashSet<wchar16> LatinCharsSet = {u'A', u'Á', u'B', u'C', u'D', u'E', u'F', u'G', u'Ǵ', u'H', u'İ', u'I', u'J', u'K', u'L', u'M', u'N', u'Ń', u'O', u'Ó', u'P', u'Q', u'R', u'S', u'T', u'U', u'Ú', u'V', u'Y', u'Ý', u'Z', u'-'};
        const THashSet<wchar16> LatinVowelCharsSet = {u'А', u'Ә', u'Ə', u'Е', u'И', u'О', u'Ө', u'Ɵ', u'Ұ', u'Ү', u'У', u'Ы', u'І', u'Э'};

        const TCompactTrie<wchar16, TUtf16String> FixlistTrie;

        TUtf16String ConvertWord(const TUtf16String& oldValue, const TUtf16String& newValue) {
            if (oldValue.empty()) return oldValue;

            TUtf16String result = newValue;
            TUtf16String tmp = oldValue;
            tmp.to_upper();
            if (oldValue == tmp) {
                result.to_upper();
            } else {
                if (LatinCharsSet.count(oldValue[0])) {
                    result.to_upper(0, 1);
                } else {
                    result.to_lower();
                }
            }

            return result;
        }

    public:
        TOfficialLikeKazDetransliter()
            : FixlistTrie(reinterpret_cast<const char*>(KazDetranslitFixlist), KazDetranslitFixlistSize)
        {}

        TUtf16String ConvertLatinToCyrillic(const TUtf16String& s, bool convertAllChars) {
            TWtringBuf text(s);
            TUtf16String normalized = Normalize<NUnicode::NFC>(text);
            // NFC normalization doesn't compose О́
            auto it = normalized.find(u"О́");
            if (it != TUtf16String::npos) {
                normalized.replace(it, it + 2, u"Ó");
            }
            normalized += u".";

            TVector<TUtf16String> parts(normalized.size());
            TUtf16String latinWord;

            for (size_t i = 0; i < normalized.size(); ++i) {
                if (!LatinCharsSet.count(ToUpper(normalized[i]))) {
                    if (!latinWord.empty()) {
                        latinWord.to_lower();
                        size_t j = i - latinWord.size();
                        if (latinWord.size() >= 2) {
                            TUtf16String specialWordDetranslit;
                            size_t specialWordLength;
                            if (FixlistTrie.FindLongestPrefix(latinWord, &specialWordLength, &specialWordDetranslit)) {
                                if (specialWordLength > 3 || specialWordLength == latinWord.size()) {
                                    parts[j] = ConvertWord(latinWord.substr(0, specialWordLength), specialWordDetranslit);
                                    j += specialWordLength;
                                }
                            }
                        }
                        size_t lastStartIndex = j;
                        Sound prevSound = Sound::Unknown;
                        for (; j < i; ++j) {
                            if (j > lastStartIndex && !parts[j - 1].empty()) {
                                prevSound = LatinVowelCharsSet.count(ToUpper(parts[j - 1].back())) ? Sound::Vowel : Sound::Consonant;
                            }
                            if (j + 1 < latinWord.size()) {
                                TUtf16String key = latinWord.substr(j, 2);
                                auto it = PairsMap.find(key);
                                if (it != PairsMap.end()) {
                                    if (it->second.Condition == TLetterMapVariants::SpecialCondition::AfterConsonant && prevSound == Sound::Consonant) {
                                        parts[j] = ConvertWord(key, it->second.Special);
                                    } else {
                                        parts[j] = ConvertWord(key, it->second.Default);
                                    }
                                    j += 1;
                                    continue;
                                }
                            }

                            auto it = SingleMap.find(normalized[j]);
                            if (it != SingleMap.end()) {
                                if (it->second.Condition == TLetterMapVariants::SpecialCondition::AfterVowel && prevSound == Sound::Vowel) {
                                    parts[j] = it->second.Special;
                                } else {
                                    parts[j] = it->second.Default;
                                }
                            } else {
                                if (IsAlpha(normalized[j]) && !convertAllChars) {
                                    return u"";
                                }
                                auto it = IrregularMap.find(normalized[j]);
                                if (it != IrregularMap.end()) {
                                    parts[j] += it->second;
                                } else {
                                    parts[j] += normalized[j];
                                }
                            }
                        }
                        latinWord.clear();
                    }
                    if (IsAlpha(normalized[i]) && !convertAllChars) {
                        return u"";
                    }
                    auto it = IrregularMap.find(normalized[i]);
                    if (it != IrregularMap.end()) {
                        parts[i] += it->second;
                    } else {
                        parts[i] += normalized[i];
                    }
                    continue;
                }
                latinWord += normalized[i];
            }

            return JoinStrings(parts.begin(), parts.end() - 1, u"");
        }
    };

    const auto OfficialLikeKazDetransliter = Singleton<TOfficialLikeKazDetransliter>();
}

TUtf16String KazLatinToCyrillicOfficialLike(const TUtf16String& s, bool convertAllChars) {
    return OfficialLikeKazDetransliter->ConvertLatinToCyrillic(s, convertAllChars);
}
