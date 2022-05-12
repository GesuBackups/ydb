#pragma once

#include <util/system/defaults.h>

template<class T, ui32 offset, ui32 bits>
struct TFlagBits {
    const static size_t Offs = offset;
    const static size_t Bits = bits;
    inline static void Pack(T& flags, ui32 value) {
        T mask = (T(1) << bits) - 1;
        flags &= ~(mask << offset);
        flags |= (value << offset);
    }
    inline static ui32 Unpack(T flags) {
        T mask = (T(1) << bits) - 1;
        return (flags >> offset) & mask;
    }
};

// ext link flags
// Compactly stored link seo factors and markup.
// Used to interpret bits of TLinkExtRec::Flags as values.
//
// Description         | Bits
// -----------------------------
// Link segment        |  0-2
// Online seo markup   |  3-14
// NoFollow            |  15
// Offtop data         |  16-31
//

// Offset, #bits
typedef TFlagBits<ui32,  0, 3> TLinkFlagSegment;
typedef TFlagBits<ui32,  3, 2> TLinkFlagSeoMark;
typedef TFlagBits<ui32,  5, 2> TLinkFlagSeoDoc;
typedef TFlagBits<ui32,  7, 4> TLinkFlagSeoPrice;
typedef TFlagBits<ui32, 11, 1> TLinkFlagSeoSubgr;
typedef TFlagBits<ui32, 12, 1> TLinkFlagDocFoldingLevel;
typedef TFlagBits<ui32, 13, 2> TLinkFlagSeoTrash;
typedef TFlagBits<ui32, 15, 1> TLinkFlagNoFollow;
// NB: 15-th bit of 'Flags' field in TLinkExtRec and TLinkIntRec is preserved for internal usage.

typedef TFlagBits<ui32, 16, 16> TLinkFlagOfftop;

const ui32 OFFTOP_WORD_COUNT = 8;

inline ui16 GetWordOfftopBits(ui16 offtopInfo, ui32 word) {
    if (word >= OFFTOP_WORD_COUNT)
        return 0;
    return ui16((offtopInfo >> (word * 2)) & 3);
}

inline void SetWordOfftopBits(ui16& offtopInfo, ui32 word, ui16 bits) {
    if (word >= OFFTOP_WORD_COUNT)
        return;
    offtopInfo |= (bits & 3) << (word * 2);
}

// Compactly stored text seo factors.
// Used mostly in io records and hash values and
// also to interpret bits of TLinkExtRec::SeoFlags as values.
//
// Achtung! There must be enough bits to represent all values!
// Check it when you add new values to enums!
//
// Description   | Bits  | Values
// ------------------------------
// Normalization | 0-2   | 0-6
// Sale category | 3-5   | 0-3
// Theme id      | 6-12  | 0-26
// free          | 13    |
// Theme status  | 14    | 0-1
// free          | 15-24 |
// Fake link     | 25    | 0-1
// free          | 26-27 |
// link target   | 28-29 | 0-3
// Is site wide  | 30    | 0-1
// Walrus redir  | 31    | 1

typedef TFlagBits<ui32,  0, 3> TLinkTFlagTextNorm;
typedef TFlagBits<ui32,  3, 3> TLinkTFlagTextSale;
typedef TFlagBits<ui32,  6, 7> TLinkTFlagTextThemeId;
typedef TFlagBits<ui32, 14, 1> TLinkTFlagTextThemeSt;
typedef TFlagBits<ui32, 25, 1> TLinkTFlagIsFakeLink;
typedef TFlagBits<ui32, 28, 2> TLinkTFlagLinkTargetAttr;
typedef TFlagBits<ui32, 30, 1> TLinkTFlagIsSiteWideLink;
typedef TFlagBits<ui32, 31, 1> TLinkTFlagExtRedirect;

enum ELinkTarget {
    LT_NoneOrSelf,
    LT_Other,
    LT_TopOrParent,
    LT_Blank,
};
