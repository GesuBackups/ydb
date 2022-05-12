#pragma once

#include <ctype.h>
#include <vector>
#include <util/system/defaults.h>

struct Dict_4bit {
   ui16 firstbyte[256]; // may be ui32?
   std::vector<char> mydict;

   Dict_4bit();
   // returns: 0 = not found, (1 = found1 | 4 = found2), 3 = found3
   int findword(const char *s) const {
      ui32 f = firstbyte[(ui8)*s]; // we don't need to check that *s != 0
      int f1 = f & 3;
      f1 = f1 == 2? 4 : f1;
      if (!(f & ~3)) return f1;
      if (f1 == 3) return 3;
      return nextchar(&mydict[0] + ((f & ~3) << 3), s + 1, f1); // f contains a 14-bit offset
   }
   void addword(const char *s, int t);
   int find_any(const char *text) const;

 protected:
   static int nextchar(const char *ptr, const char *s, int f1);
   static void flag(ui16 &p, int t);
   char *alloc_block();
   char *next_or_alloc1(char *ptr, ui32 off);
   char *next_or_alloc2(char *ptr, ui32 off);
   static inline bool token_char(char c) {
      return isalnum((ui8)c) || c == '_' || c == '-';
   }
};

class TRegExSubst;

// This class should be used to access yweb/filter/findboard.re.1 file
struct FindBoard {
   void LoadRules(const char *fname);
   bool Reject(const char *full_url) const;
   FindBoard(const char *fname = nullptr);
   ~FindBoard();

   mutable size_t regpos_called, regneg_called;
 protected:
   TRegExSubst *regpos, *regneg;
   Dict_4bit words;
   bool have_regpos, have_regneg;
   bool normalize_url, lowercase_quoted;
};
