#include <stdio.h>
#include <assert.h>
#include <util/system/compat.h>
#include <util/system/maxlen.h>
#include <library/cpp/deprecated/fgood/ffb.h>
#include <util/string/type.h>
#include <library/cpp/string_utils/url/url.h>

#include <library/cpp/regex/pcre/regexp.h>

#include "findboard.h"

using namespace std;

Dict_4bit::Dict_4bit() {
   mydict.reserve(65536);
   memset(firstbyte, 0, sizeof(firstbyte));
   alloc_block(); // because zero offset is reserved ;)
}
inline void Dict_4bit::flag(ui16 &p, int t) { // t is either 1 or 2
   if ((p & 3) == 3) return;
   p = (p & ~3) | t;
}
char *Dict_4bit::alloc_block() {
   mydict.resize(mydict.size() + 32, 0);
   return &*mydict.end() - 32;
}
char *Dict_4bit::next_or_alloc1(char *ptr, ui32 off) {
   ui32 f = off[(ui16*)ptr];
   if (f)
      return ptr + (f << 5); // f contains a 16-bit offset
   ui32 ptr_off = ptr - &*mydict.begin();
   char *ptr2 = alloc_block();
   ptr = ptr_off + &*mydict.begin();
   if (ptr2 - ptr > (65535 << 5))
      ythrow yexception() << "Dict_4bit buffer overflow";
   off[(ui16*)ptr] = (ptr2 - ptr) >> 5;
   return ptr2;
}
char *Dict_4bit::next_or_alloc2(char *ptr, ui32 off) {
   ui32 f = off[(ui16*)ptr];
   if (f & ~3)
      return ptr + ((f & ~3) << 3); // f contains a 14-bit offset
   ui32 ptr_off = ptr - &*mydict.begin();
   char *ptr2 = alloc_block();
   ptr = ptr_off + &*mydict.begin();
   if (ptr2 - ptr > (16383 << 5))
      ythrow yexception() << "Dict_4bit buffer overflow";
   off[(ui16*)ptr] |= (ptr2 - ptr) >> 3;
   return ptr2;
}

int Dict_4bit::nextchar(const char *ptr, const char *s, int f1) {
   for(; *s; s++) {
      ui32 f = ((ui16*)ptr)[(ui8)*s >> 4];
      //printf("[2]: '%c' (0x%02x) -> 0x%04x (0x%04x)\n:", (ui8)*s, (ui8)*s, f, f1);
      if (!f) return f1; // f contains a 16-bit offset
      ptr += f << 5;
      f = ((ui16*)ptr)[(ui8)*s & 15];
      //printf("[3]: '%c' (0x%02x) -> 0x%04x (0x%04x)\n:", (ui8)*s, (ui8)*s, f, f1);
      if (f & 3) {
         switch(f & 3) {
         case 3: return 3;
         case 2: f1 |= 4; break;
         default: f1 |= 1;
         }
      }
      if (!(f & ~3)) return f1;
      ptr += ((f & ~3) << 3); // f contains a 14-bit offset
   }
   return f1;
}
void Dict_4bit::addword(const char *s, int t) {
   assert(t == 1 || t == 2 || t == 3);
   if (!*s) return;
   //printf("addword: %s -> %i\n", s, t);
   ui16 &f = firstbyte[(ui8)*s];
   if (!s[1]) {
      flag(f, t);
      return;
   }
   char *ptr;
   if (f & ~3) {
      ptr = &*mydict.begin() + ((f & ~3) << 3);
   } else {
      ptr = alloc_block();
      f |= (ptr - &*mydict.begin()) >> 3;
   }
   for(s++; *s; s++) {
      ptr = next_or_alloc1(ptr, (ui8)*s >> 4);
      if (!s[1]) {
         flag(((ui16*)ptr)[(ui8)*s & 15], t);
         break;
      }
      ptr = next_or_alloc2(ptr, (ui8)*s & 15);
   }
}
int Dict_4bit::find_any(const char *text) const {
   int wf = 0;
   for(const char *t = text; *t; t++) {
      int f = findword(t);
      //printf("%s -> %i\n", t, f);
      if (f == 3) return 3;
      wf |= f;
   }
   return wf;
}

void compile_or_die(TRegExSubst &re, TString &str) {
   if (!!str) {
      re.Compile(str.data(), REG_ICASE | REG_EXTENDED);
   }
}

inline bool empty_or_comment_line(const char *c) {
   for(; *c; c++) {
      if (*c == '#') return 1;
      if ((ui8)*c > ' ') return 0;
   }
   return 1;
}

FindBoard::FindBoard(const char *fname) {
   regpos_called = regneg_called = 0;
   regpos = new TRegExSubst; regneg = new TRegExSubst;
   if (fname)
      LoadRules(fname);
}
FindBoard::~FindBoard() {
   delete regpos; delete regneg;
   regpos = regneg = nullptr;
}

bool istrue_ex(const char *val) {
   if (!val)
      return false;
   while (isspace((ui8)*val))
      val++;
   if (!*val)
      return false;
   if (*val == '+' || *val == '-' || isdigit((ui8)*val))
      return atoi(val) != 0;
   return IsTrue(val);
}

void FindBoard::LoadRules(const char *regex_fname) {
   TFILEPtr f(regex_fname, "r");
   int nl = 0;
   char buf[16384];

   have_regpos = false, have_regneg = false;
   normalize_url = false, lowercase_quoted = false;
   TString str_regpos, str_regneg;
   nl = 0;
   while(fgets(buf, 16384, f)) {
      if (empty_or_comment_line(buf)) continue;
      chomp(buf);
      strlwr(buf); // case insensitive
      char *eq_sign = strchr(buf + 1, '=');
      switch (*buf) {
      case '+': words.addword(buf + 1, 1); break;
      case '=': words.addword(buf + 1, 2); break;
      case '!': words.addword(buf + 1, 3); break;
      case '~': if (!!str_regpos) str_regpos += '|'; str_regpos += buf + 1; have_regpos = true; break;
      case '^': if (!!str_regneg) str_regneg += '|'; str_regneg += buf + 1; have_regneg = true; break;
      case '$':
         if (!eq_sign) ythrow yexception() << "" <<  regex_fname << " line " <<  nl + 1 << ": '=' not found for parameter (" <<  buf << ")";
         *eq_sign++ = 0;
         if (char *sp = strchr(buf + 1, ' '))
            *sp = 0;
         if (!strcmp(buf + 1, "normalize_url"))
            normalize_url = istrue_ex(eq_sign);
         else if (!strcmp(buf + 1, "lowercase_quoted"))
            lowercase_quoted = istrue_ex(eq_sign);
         else
            ythrow yexception() << "" <<  regex_fname << " line " <<  nl + 1 << ": unknown parameter '" <<  buf << "'";
         break;
      default: ythrow yexception() << "" <<  regex_fname << " line " <<  nl + 1 << ": unknown rule type '" <<  *buf << "'";
      }
      nl++;
   }

   compile_or_die(*regpos, str_regpos);
   compile_or_die(*regneg, str_regneg);
}

bool FindBoard::Reject(const char *full_url) const {
   char url[FULLURL_MAX + 4];
   if (normalize_url) {
      NormalizeUrlName(url, full_url, FULLURL_MAX + 3);
   } else {
      strlcpy(url, full_url, FULLURL_MAX + 1);
      strlwr(url); // case insensitive
      if (lowercase_quoted) { // {%41-%5a => %61-%7a}
         for (char *c = url; *c; c++)
            if (*c == '%' &&
               (c[1] == '4' && c[2] != '0' && isxdigit((ui8)c[2]) ||
                c[1] == '5' && c[2] <= 'a' && isxdigit((ui8)c[2]))
            )
               c[1] += 2;
      }
   }
   int wf = words.find_any(url);
   //printf("%s: %i\n", url, wf); fflush(stdout);
   if (wf == 4 && have_regpos) {
      regpos_called++;
      regmatch_t pmatch[NMATCHES];
      if (!regpos->Exec(url, pmatch, 0))
         wf = 5;
      //printf(" -> %i\n", wf); fflush(stdout);
   }
   if (wf == 5 && have_regneg) {
      regneg_called++;
      regmatch_t pmatch[NMATCHES];
      if (!regneg->Exec(url, pmatch, 0))
         wf = 3;
      //printf(" -> %i\n", wf); fflush(stdout);
   }
   return (wf & ~4) == 1;
}
