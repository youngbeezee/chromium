/*
 * (C) 1999 Lars Knoll (knoll@kde.org)
 * Copyright (C) 2004, 2005, 2006, 2007, 2008, 2010 Apple Inc. All rights
 * reserved.
 * Copyright (C) 2007-2009 Torch Mobile, Inc.
 * Copyright (C) 2011 Google Inc. All rights reserved.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public License
 * along with this library; see the file COPYING.LIB.  If not, write to
 * the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#include "platform/text/TextBreakIterator.h"

#include "platform/text/Character.h"
#include "platform/wtf/ASCIICType.h"
#include "platform/wtf/StdLibExtras.h"
#include "platform/wtf/text/CharacterNames.h"

#include <unicode/uchar.h>
#include <unicode/uvernum.h>

namespace blink {

unsigned NumGraphemeClusters(const String& string) {
  unsigned string_length = string.length();

  if (!string_length)
    return 0;

  // The only Latin-1 Extended Grapheme Cluster is CR LF
  if (string.Is8Bit() && !string.Contains('\r'))
    return string_length;

  NonSharedCharacterBreakIterator it(string);
  if (!it)
    return string_length;

  unsigned num = 0;
  while (it.Next() != kTextBreakDone)
    ++num;
  return num;
}

static inline bool IsBreakableSpace(UChar ch) {
  switch (ch) {
    case ' ':
    case '\n':
    case '\t':
      return true;
    default:
      return false;
  }
}

static const UChar kAsciiLineBreakTableFirstChar = '!';
static const UChar kAsciiLineBreakTableLastChar = 127;

// Pack 8 bits into one byte
#define B(a, b, c, d, e, f, g, h)                                         \
  ((a) | ((b) << 1) | ((c) << 2) | ((d) << 3) | ((e) << 4) | ((f) << 5) | \
   ((g) << 6) | ((h) << 7))

// Line breaking table row for each digit (0-9)
#define DI \
  { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 }

// Line breaking table row for ascii letters (a-z A-Z)
#define AL \
  { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 }

#define F 0xFF

// Line breaking table for printable ASCII characters. Line breaking
// opportunities in this table are as below:
// - before opening punctuations such as '(', '<', '[', '{' after certain
//   characters (compatible with Firefox 3.6);
// - after '-' and '?' (backward-compatible, and compatible with Internet
//   Explorer).
// Please refer to <https://bugs.webkit.org/show_bug.cgi?id=37698> for line
// breaking matrixes of different browsers and the ICU standard.
// clang-format off
static const unsigned char kAsciiLineBreakTable[][(kAsciiLineBreakTableLastChar - kAsciiLineBreakTableFirstChar) / 8 + 1] = {
    //  !  "  #  $  %  &  '  (     )  *  +  ,  -  .  /  0  1-8   9  :  ;  <  =  >  ?  @     A-X      Y  Z  [  \  ]  ^  _  `     a-x      y  z  {  |  }  ~  DEL
    { B(0, 0, 0, 0, 0, 0, 0, 1), B(0, 0, 0, 0, 0, 0, 0, 0), 0, B(0, 0, 0, 1, 0, 0, 0, 0), 0, 0, 0, B(0, 0, 1, 0, 0, 0, 0, 0), 0, 0, 0, B(0, 0, 1, 0, 0, 0, 0, 0) }, // !
    { B(0, 0, 0, 0, 0, 0, 0, 1), B(0, 0, 0, 0, 0, 0, 0, 0), 0, B(0, 0, 0, 1, 0, 0, 0, 0), 0, 0, 0, B(0, 0, 1, 0, 0, 0, 0, 0), 0, 0, 0, B(0, 0, 1, 0, 0, 0, 0, 0) }, // "
    { B(0, 0, 0, 0, 0, 0, 0, 1), B(0, 0, 0, 0, 0, 0, 0, 0), 0, B(0, 0, 0, 1, 0, 0, 0, 0), 0, 0, 0, B(0, 0, 1, 0, 0, 0, 0, 0), 0, 0, 0, B(0, 0, 1, 0, 0, 0, 0, 0) }, // #
    { B(0, 0, 0, 0, 0, 0, 0, 0), B(0, 0, 0, 0, 0, 0, 0, 0), 0, B(0, 0, 0, 0, 0, 0, 0, 0), 0, 0, 0, B(0, 0, 0, 0, 0, 0, 0, 0), 0, 0, 0, B(0, 0, 0, 0, 0, 0, 0, 0) }, // $
    { B(0, 0, 0, 0, 0, 0, 0, 1), B(0, 0, 0, 0, 0, 0, 0, 0), 0, B(0, 0, 0, 1, 0, 0, 0, 0), 0, 0, 0, B(0, 0, 1, 0, 0, 0, 0, 0), 0, 0, 0, B(0, 0, 1, 0, 0, 0, 0, 0) }, // %
    { B(0, 0, 0, 0, 0, 0, 0, 1), B(0, 0, 0, 0, 0, 0, 0, 0), 0, B(0, 0, 0, 1, 0, 0, 0, 0), 0, 0, 0, B(0, 0, 1, 0, 0, 0, 0, 0), 0, 0, 0, B(0, 0, 1, 0, 0, 0, 0, 0) }, // &
    { B(0, 0, 0, 0, 0, 0, 0, 0), B(0, 0, 0, 0, 0, 0, 0, 0), 0, B(0, 0, 0, 0, 0, 0, 0, 0), 0, 0, 0, B(0, 0, 0, 0, 0, 0, 0, 0), 0, 0, 0, B(0, 0, 0, 0, 0, 0, 0, 0) }, // '
    { B(0, 0, 0, 0, 0, 0, 0, 0), B(0, 0, 0, 0, 0, 0, 0, 0), 0, B(0, 0, 0, 0, 0, 0, 0, 0), 0, 0, 0, B(0, 0, 0, 0, 0, 0, 0, 0), 0, 0, 0, B(0, 0, 0, 0, 0, 0, 0, 0) }, // (
    { B(0, 0, 0, 0, 0, 0, 0, 1), B(0, 0, 0, 0, 0, 0, 0, 0), 0, B(0, 0, 0, 1, 0, 0, 0, 0), 0, 0, 0, B(0, 0, 1, 0, 0, 0, 0, 0), 0, 0, 0, B(0, 0, 1, 0, 0, 0, 0, 0) }, // )
    { B(0, 0, 0, 0, 0, 0, 0, 1), B(0, 0, 0, 0, 0, 0, 0, 0), 0, B(0, 0, 0, 1, 0, 0, 0, 0), 0, 0, 0, B(0, 0, 1, 0, 0, 0, 0, 0), 0, 0, 0, B(0, 0, 1, 0, 0, 0, 0, 0) }, // *
    { B(0, 0, 0, 0, 0, 0, 0, 1), B(0, 0, 0, 0, 0, 0, 0, 0), 0, B(0, 0, 0, 1, 0, 0, 0, 0), 0, 0, 0, B(0, 0, 1, 0, 0, 0, 0, 0), 0, 0, 0, B(0, 0, 1, 0, 0, 0, 0, 0) }, // +
    { B(0, 0, 0, 0, 0, 0, 0, 1), B(0, 0, 0, 0, 0, 0, 0, 0), 0, B(0, 0, 0, 1, 0, 0, 0, 0), 0, 0, 0, B(0, 0, 1, 0, 0, 0, 0, 0), 0, 0, 0, B(0, 0, 1, 0, 0, 0, 0, 0) }, // ,
    { B(0, 1, 1, 0, 1, 1, 1, 1), B(0, 1, 1, 0, 1, 0, 0, 0), 0, B(0, 0, 0, 1, 1, 1, 0, 1), F, F, F, B(1, 1, 1, 1, 0, 1, 1, 1), F, F, F, B(1, 1, 1, 1, 0, 1, 1, 1) }, // - Note: breaking before '0'-'9' is handled hard-coded in shouldBreakAfter().
    { B(0, 0, 0, 0, 0, 0, 0, 1), B(0, 0, 0, 0, 0, 0, 0, 0), 0, B(0, 0, 0, 1, 0, 0, 0, 0), 0, 0, 0, B(0, 0, 1, 0, 0, 0, 0, 0), 0, 0, 0, B(0, 0, 1, 0, 0, 0, 0, 0) }, // .
    { B(0, 0, 0, 0, 0, 0, 0, 0), B(0, 0, 0, 0, 0, 0, 0, 0), 0, B(0, 0, 0, 0, 0, 0, 0, 0), 0, 0, 0, B(0, 0, 0, 0, 0, 0, 0, 0), 0, 0, 0, B(0, 0, 0, 0, 0, 0, 0, 0) }, // /
    DI,  DI,  DI,  DI,  DI,  DI,  DI,  DI,  DI,  DI, // 0-9
    { B(0, 0, 0, 0, 0, 0, 0, 1), B(0, 0, 0, 0, 0, 0, 0, 0), 0, B(0, 0, 0, 1, 0, 0, 0, 0), 0, 0, 0, B(0, 0, 1, 0, 0, 0, 0, 0), 0, 0, 0, B(0, 0, 1, 0, 0, 0, 0, 0) }, // :
    { B(0, 0, 0, 0, 0, 0, 0, 1), B(0, 0, 0, 0, 0, 0, 0, 0), 0, B(0, 0, 0, 1, 0, 0, 0, 0), 0, 0, 0, B(0, 0, 1, 0, 0, 0, 0, 0), 0, 0, 0, B(0, 0, 1, 0, 0, 0, 0, 0) }, // ;
    { B(0, 0, 0, 0, 0, 0, 0, 0), B(0, 0, 0, 0, 0, 0, 0, 0), 0, B(0, 0, 0, 0, 0, 0, 0, 0), 0, 0, 0, B(0, 0, 0, 0, 0, 0, 0, 0), 0, 0, 0, B(0, 0, 0, 0, 0, 0, 0, 0) }, // <
    { B(0, 0, 0, 0, 0, 0, 0, 1), B(0, 0, 0, 0, 0, 0, 0, 0), 0, B(0, 0, 0, 1, 0, 0, 0, 0), 0, 0, 0, B(0, 0, 1, 0, 0, 0, 0, 0), 0, 0, 0, B(0, 0, 1, 0, 0, 0, 0, 0) }, // =
    { B(0, 0, 0, 0, 0, 0, 0, 1), B(0, 0, 0, 0, 0, 0, 0, 0), 0, B(0, 0, 0, 1, 0, 0, 0, 0), 0, 0, 0, B(0, 0, 1, 0, 0, 0, 0, 0), 0, 0, 0, B(0, 0, 1, 0, 0, 0, 0, 0) }, // >
    { B(0, 0, 1, 1, 1, 1, 0, 1), B(0, 1, 1, 0, 1, 0, 0, 1), F, B(1, 0, 0, 1, 1, 1, 0, 1), F, F, F, B(1, 1, 1, 1, 0, 1, 1, 1), F, F, F, B(1, 1, 1, 1, 0, 1, 1, 0) }, // ?
    { B(0, 0, 0, 0, 0, 0, 0, 0), B(0, 0, 0, 0, 0, 0, 0, 0), 0, B(0, 0, 0, 0, 0, 0, 0, 0), 0, 0, 0, B(0, 0, 0, 0, 0, 0, 0, 0), 0, 0, 0, B(0, 0, 0, 0, 0, 0, 0, 0) }, // @
    AL,  AL,  AL,  AL,  AL,  AL,  AL,  AL,  AL,  AL,  AL,  AL,  AL,  AL,  AL,  AL,  AL,  AL,  AL,  AL,  AL,  AL,  AL,  AL,  AL,  AL, // A-Z
    { B(0, 0, 0, 0, 0, 0, 0, 0), B(0, 0, 0, 0, 0, 0, 0, 0), 0, B(0, 0, 0, 0, 0, 0, 0, 0), 0, 0, 0, B(0, 0, 0, 0, 0, 0, 0, 0), 0, 0, 0, B(0, 0, 0, 0, 0, 0, 0, 0) }, // [
    { B(0, 0, 0, 0, 0, 0, 0, 1), B(0, 0, 0, 0, 0, 0, 0, 0), 0, B(0, 0, 0, 1, 0, 0, 0, 0), 0, 0, 0, B(0, 0, 1, 0, 0, 0, 0, 0), 0, 0, 0, B(0, 0, 1, 0, 0, 0, 0, 0) }, // '\'
    { B(0, 0, 0, 0, 0, 0, 0, 1), B(0, 0, 0, 0, 0, 0, 0, 0), 0, B(0, 0, 0, 1, 0, 0, 0, 0), 0, 0, 0, B(0, 0, 1, 0, 0, 0, 0, 0), 0, 0, 0, B(0, 0, 1, 0, 0, 0, 0, 0) }, // ]
    { B(0, 0, 0, 0, 0, 0, 0, 0), B(0, 0, 0, 0, 0, 0, 0, 0), 0, B(0, 0, 0, 0, 0, 0, 0, 0), 0, 0, 0, B(0, 0, 0, 0, 0, 0, 0, 0), 0, 0, 0, B(0, 0, 0, 0, 0, 0, 0, 0) }, // ^
    { B(0, 0, 0, 0, 0, 0, 0, 0), B(0, 0, 0, 0, 0, 0, 0, 0), 0, B(0, 0, 0, 0, 0, 0, 0, 0), 0, 0, 0, B(0, 0, 0, 0, 0, 0, 0, 0), 0, 0, 0, B(0, 0, 0, 0, 0, 0, 0, 0) }, // _
    { B(0, 0, 0, 0, 0, 0, 0, 0), B(0, 0, 0, 0, 0, 0, 0, 0), 0, B(0, 0, 0, 0, 0, 0, 0, 0), 0, 0, 0, B(0, 0, 0, 0, 0, 0, 0, 0), 0, 0, 0, B(0, 0, 0, 0, 0, 0, 0, 0) }, // `
    AL,  AL,  AL,  AL,  AL,  AL,  AL,  AL,  AL,  AL,  AL,  AL,  AL,  AL,  AL,  AL,  AL,  AL,  AL,  AL,  AL,  AL,  AL,  AL,  AL,  AL, // a-z
    { B(0, 0, 0, 0, 0, 0, 0, 0), B(0, 0, 0, 0, 0, 0, 0, 0), 0, B(0, 0, 0, 0, 0, 0, 0, 0), 0, 0, 0, B(0, 0, 0, 0, 0, 0, 0, 0), 0, 0, 0, B(0, 0, 0, 0, 0, 0, 0, 0) }, // {
    { B(0, 0, 0, 0, 0, 0, 0, 1), B(0, 0, 0, 0, 0, 0, 0, 0), 0, B(0, 0, 0, 1, 0, 0, 0, 0), 0, 0, 0, B(0, 0, 1, 0, 0, 0, 0, 0), 0, 0, 0, B(0, 0, 1, 0, 0, 0, 0, 0) }, // |
    { B(0, 0, 0, 0, 0, 0, 0, 1), B(0, 0, 0, 0, 0, 0, 0, 0), 0, B(0, 0, 0, 1, 0, 0, 0, 0), 0, 0, 0, B(0, 0, 1, 0, 0, 0, 0, 0), 0, 0, 0, B(0, 0, 1, 0, 0, 0, 0, 0) }, // }
    { B(0, 0, 0, 0, 0, 0, 0, 1), B(0, 0, 0, 0, 0, 0, 0, 0), 0, B(0, 0, 0, 1, 0, 0, 0, 0), 0, 0, 0, B(0, 0, 1, 0, 0, 0, 0, 0), 0, 0, 0, B(0, 0, 1, 0, 0, 0, 0, 0) }, // ~
    { B(0, 0, 0, 0, 0, 0, 0, 0), B(0, 0, 0, 0, 0, 0, 0, 0), 0, B(0, 0, 0, 0, 0, 0, 0, 0), 0, 0, 0, B(0, 0, 0, 0, 0, 0, 0, 0), 0, 0, 0, B(0, 0, 0, 0, 0, 0, 0, 0) }, // DEL
};
// clang-format on

#if U_ICU_VERSION_MAJOR_NUM >= 58
#define BA_LB_COUNT (U_LB_COUNT - 3)
#else
#define BA_LB_COUNT U_LB_COUNT
#endif
// Line breaking table for CSS word-break: break-all. This table differs from
// asciiLineBreakTable in:
// - Indices are Line Breaking Classes defined in UAX#14 Unicode Line Breaking
//   Algorithm: http://unicode.org/reports/tr14/#DescriptionOfProperties
// - 1 indicates additional break opportunities. 0 indicates to fallback to
//   normal line break, not "prohibit break."
// clang-format off
static const unsigned char kBreakAllLineBreakClassTable[][BA_LB_COUNT / 8 + 1] = {
    // XX AI AL B2 BA BB BK CB    CL CM CR EX GL HY ID IN    IS LF NS NU OP PO PR QU    SA SG SP SY ZW NL WJ H2    H3 JL JT JV CP CJ HL RI
    { B(0, 0, 0, 0, 0, 0, 0, 0), B(0, 0, 0, 0, 0, 0, 0, 0), B(0, 0, 0, 0, 0, 0, 0, 0), B(0, 0, 0, 0, 0, 0, 0, 0), B(0, 0, 0, 0, 0, 0, 0, 0) }, // XX
    { B(0, 1, 1, 0, 1, 0, 0, 0), B(0, 0, 0, 0, 0, 1, 0, 0), B(0, 0, 0, 1, 1, 0, 1, 0), B(1, 0, 0, 0, 0, 0, 0, 0), B(0, 0, 0, 0, 0, 0, 1, 0) }, // AI
    { B(0, 1, 1, 0, 1, 0, 0, 0), B(0, 0, 0, 0, 0, 1, 0, 0), B(0, 0, 0, 1, 1, 0, 1, 0), B(1, 0, 0, 0, 0, 0, 0, 0), B(0, 0, 0, 0, 0, 0, 1, 0) }, // AL
    { B(0, 0, 0, 0, 0, 0, 0, 0), B(0, 0, 0, 0, 0, 0, 0, 0), B(0, 0, 0, 0, 0, 0, 0, 0), B(0, 0, 0, 0, 0, 0, 0, 0), B(0, 0, 0, 0, 0, 0, 0, 0) }, // B2
    { B(0, 1, 1, 0, 1, 0, 0, 0), B(0, 0, 0, 0, 0, 1, 0, 0), B(0, 0, 0, 1, 1, 0, 1, 0), B(1, 0, 0, 0, 0, 0, 0, 0), B(0, 0, 0, 0, 0, 0, 1, 0) }, // BA
    { B(0, 0, 0, 0, 0, 0, 0, 0), B(0, 0, 0, 0, 0, 0, 0, 0), B(0, 0, 0, 0, 0, 0, 0, 0), B(0, 0, 0, 0, 0, 0, 0, 0), B(0, 0, 0, 0, 0, 0, 0, 0) }, // BB
    { B(0, 0, 0, 0, 0, 0, 0, 0), B(0, 0, 0, 0, 0, 0, 0, 0), B(0, 0, 0, 0, 0, 0, 0, 0), B(0, 0, 0, 0, 0, 0, 0, 0), B(0, 0, 0, 0, 0, 0, 0, 0) }, // BK
    { B(0, 0, 0, 0, 0, 0, 0, 0), B(0, 0, 0, 0, 0, 0, 0, 0), B(0, 0, 0, 0, 0, 0, 0, 0), B(0, 0, 0, 0, 0, 0, 0, 0), B(0, 0, 0, 0, 0, 0, 0, 0) }, // CB
    { B(0, 1, 1, 0, 1, 0, 0, 0), B(0, 0, 0, 0, 0, 1, 0, 0), B(0, 0, 0, 1, 0, 0, 1, 0), B(0, 0, 0, 0, 0, 0, 0, 0), B(0, 0, 0, 0, 0, 0, 1, 0) }, // CL
    { B(0, 0, 0, 0, 0, 0, 0, 0), B(0, 0, 0, 0, 0, 0, 0, 0), B(0, 0, 0, 0, 0, 0, 0, 0), B(0, 0, 0, 0, 0, 0, 0, 0), B(0, 0, 0, 0, 0, 0, 0, 0) }, // CM
    { B(0, 0, 0, 0, 0, 0, 0, 0), B(0, 0, 0, 0, 0, 0, 0, 0), B(0, 0, 0, 0, 0, 0, 0, 0), B(0, 0, 0, 0, 0, 0, 0, 0), B(0, 0, 0, 0, 0, 0, 0, 0) }, // CR
    { B(0, 1, 1, 0, 1, 0, 0, 0), B(0, 0, 0, 0, 0, 1, 0, 0), B(0, 0, 0, 1, 0, 1, 1, 0), B(0, 0, 0, 0, 0, 0, 0, 0), B(0, 0, 0, 0, 0, 0, 1, 0) }, // EX
    { B(0, 0, 0, 0, 0, 0, 0, 0), B(0, 0, 0, 0, 0, 0, 0, 0), B(0, 0, 0, 0, 0, 0, 0, 0), B(0, 0, 0, 0, 0, 0, 0, 0), B(0, 0, 0, 0, 0, 0, 0, 0) }, // GL
    { B(0, 0, 0, 0, 0, 0, 0, 0), B(0, 0, 0, 0, 0, 0, 0, 0), B(0, 0, 0, 1, 0, 0, 0, 0), B(0, 0, 0, 0, 0, 0, 0, 0), B(0, 0, 0, 0, 0, 0, 0, 0) }, // HY
    { B(0, 0, 0, 0, 0, 0, 0, 0), B(0, 0, 0, 0, 0, 0, 0, 0), B(0, 0, 0, 0, 0, 0, 0, 0), B(0, 0, 0, 0, 0, 0, 0, 0), B(0, 0, 0, 0, 0, 0, 0, 0) }, // ID
    { B(0, 0, 0, 0, 0, 0, 0, 0), B(0, 0, 0, 0, 0, 0, 0, 0), B(0, 0, 0, 0, 0, 0, 0, 0), B(0, 0, 0, 0, 0, 0, 0, 0), B(0, 0, 0, 0, 0, 0, 0, 0) }, // IN
    { B(0, 1, 1, 0, 1, 0, 0, 0), B(0, 0, 0, 0, 0, 1, 0, 0), B(0, 0, 0, 1, 0, 0, 0, 0), B(0, 0, 0, 0, 0, 0, 0, 0), B(0, 0, 0, 0, 0, 0, 1, 0) }, // IS
    { B(0, 0, 0, 0, 0, 0, 0, 0), B(0, 0, 0, 0, 0, 0, 0, 0), B(0, 0, 0, 0, 0, 0, 0, 0), B(0, 0, 0, 0, 0, 0, 0, 0), B(0, 0, 0, 0, 0, 0, 0, 0) }, // LF
    { B(0, 0, 0, 0, 0, 0, 0, 0), B(0, 0, 0, 0, 0, 0, 0, 0), B(0, 0, 0, 0, 0, 0, 0, 0), B(0, 0, 0, 0, 0, 0, 0, 0), B(0, 0, 0, 0, 0, 0, 0, 0) }, // NS
    { B(0, 1, 1, 0, 1, 0, 0, 0), B(0, 0, 0, 0, 0, 1, 0, 0), B(0, 0, 0, 1, 1, 0, 1, 0), B(1, 0, 0, 0, 0, 0, 0, 0), B(0, 0, 0, 0, 0, 0, 1, 0) }, // NU
    { B(0, 0, 0, 0, 0, 0, 0, 0), B(0, 0, 0, 0, 0, 0, 0, 0), B(0, 0, 0, 0, 0, 0, 0, 0), B(0, 0, 0, 0, 0, 0, 0, 0), B(0, 0, 0, 0, 0, 0, 0, 0) }, // OP
    { B(0, 1, 1, 0, 1, 0, 0, 0), B(0, 0, 0, 0, 0, 1, 0, 0), B(0, 0, 0, 1, 0, 1, 1, 0), B(0, 0, 0, 0, 0, 0, 0, 0), B(0, 0, 0, 0, 0, 0, 1, 0) }, // PO
    { B(0, 0, 0, 0, 0, 0, 0, 0), B(0, 0, 0, 0, 0, 0, 0, 0), B(0, 0, 0, 0, 0, 1, 0, 0), B(0, 0, 0, 0, 0, 0, 0, 0), B(0, 0, 0, 0, 0, 0, 0, 0) }, // PR
    { B(0, 0, 0, 0, 0, 0, 0, 0), B(0, 0, 0, 0, 0, 0, 0, 0), B(0, 0, 0, 0, 0, 0, 0, 0), B(0, 0, 0, 0, 0, 0, 0, 0), B(0, 0, 0, 0, 0, 0, 0, 0) }, // QU
    { B(0, 1, 1, 0, 1, 0, 0, 0), B(0, 0, 0, 0, 0, 1, 0, 0), B(0, 0, 0, 1, 1, 0, 1, 0), B(1, 0, 0, 0, 0, 0, 0, 0), B(0, 0, 0, 0, 0, 0, 1, 0) }, // SA
    { B(0, 0, 0, 0, 0, 0, 0, 0), B(0, 0, 0, 0, 0, 0, 0, 0), B(0, 0, 0, 0, 0, 0, 0, 0), B(0, 0, 0, 0, 0, 0, 0, 0), B(0, 0, 0, 0, 0, 0, 0, 0) }, // SG
    { B(0, 0, 0, 0, 0, 0, 0, 0), B(0, 0, 0, 0, 0, 0, 0, 0), B(0, 0, 0, 0, 0, 0, 0, 0), B(0, 0, 0, 0, 0, 0, 0, 0), B(0, 0, 0, 0, 0, 0, 0, 0) }, // SP
    { B(0, 1, 1, 0, 1, 0, 0, 0), B(0, 0, 0, 0, 0, 1, 0, 0), B(0, 0, 0, 1, 1, 0, 1, 0), B(1, 0, 0, 0, 0, 0, 0, 0), B(0, 0, 0, 0, 0, 0, 1, 0) }, // SY
    { B(0, 0, 0, 0, 0, 0, 0, 0), B(0, 0, 0, 0, 0, 0, 0, 0), B(0, 0, 0, 0, 0, 0, 0, 0), B(0, 0, 0, 0, 0, 0, 0, 0), B(0, 0, 0, 0, 0, 0, 0, 0) }, // ZW
    { B(0, 0, 0, 0, 0, 0, 0, 0), B(0, 0, 0, 0, 0, 0, 0, 0), B(0, 0, 0, 0, 0, 0, 0, 0), B(0, 0, 0, 0, 0, 0, 0, 0), B(0, 0, 0, 0, 0, 0, 0, 0) }, // NL
    { B(0, 0, 0, 0, 0, 0, 0, 0), B(0, 0, 0, 0, 0, 0, 0, 0), B(0, 0, 0, 0, 0, 0, 0, 0), B(0, 0, 0, 0, 0, 0, 0, 0), B(0, 0, 0, 0, 0, 0, 0, 0) }, // WJ
    { B(0, 0, 0, 0, 0, 0, 0, 0), B(0, 0, 0, 0, 0, 0, 0, 0), B(0, 0, 0, 0, 0, 0, 0, 0), B(0, 0, 0, 0, 0, 0, 0, 0), B(0, 0, 0, 0, 0, 0, 0, 0) }, // H2
    { B(0, 0, 0, 0, 0, 0, 0, 0), B(0, 0, 0, 0, 0, 0, 0, 0), B(0, 0, 0, 0, 0, 0, 0, 0), B(0, 0, 0, 0, 0, 0, 0, 0), B(0, 0, 0, 0, 0, 0, 0, 0) }, // H3
    { B(0, 0, 0, 0, 0, 0, 0, 0), B(0, 0, 0, 0, 0, 0, 0, 0), B(0, 0, 0, 0, 0, 0, 0, 0), B(0, 0, 0, 0, 0, 0, 0, 0), B(0, 0, 0, 0, 0, 0, 0, 0) }, // JL
    { B(0, 0, 0, 0, 0, 0, 0, 0), B(0, 0, 0, 0, 0, 0, 0, 0), B(0, 0, 0, 0, 0, 0, 0, 0), B(0, 0, 0, 0, 0, 0, 0, 0), B(0, 0, 0, 0, 0, 0, 0, 0) }, // JT
    { B(0, 0, 0, 0, 0, 0, 0, 0), B(0, 0, 0, 0, 0, 0, 0, 0), B(0, 0, 0, 0, 0, 0, 0, 0), B(0, 0, 0, 0, 0, 0, 0, 0), B(0, 0, 0, 0, 0, 0, 0, 0) }, // JV
    { B(0, 1, 1, 0, 1, 0, 0, 0), B(0, 0, 0, 0, 0, 1, 0, 0), B(0, 0, 0, 1, 0, 0, 1, 0), B(1, 0, 0, 0, 0, 0, 0, 0), B(0, 0, 0, 0, 0, 0, 1, 0) }, // CP
    { B(0, 0, 0, 0, 0, 0, 0, 0), B(0, 0, 0, 0, 0, 0, 0, 0), B(0, 0, 0, 0, 0, 0, 0, 0), B(0, 0, 0, 0, 0, 0, 0, 0), B(0, 0, 0, 0, 0, 0, 0, 0) }, // CJ
    { B(0, 1, 1, 0, 1, 0, 0, 0), B(0, 0, 0, 0, 0, 1, 0, 0), B(0, 0, 0, 1, 1, 0, 1, 0), B(1, 0, 0, 0, 0, 0, 0, 0), B(0, 0, 0, 0, 0, 0, 1, 0) }, // HL
    { B(0, 0, 0, 0, 0, 0, 0, 0), B(0, 0, 0, 0, 0, 0, 0, 0), B(0, 0, 0, 0, 0, 0, 0, 0), B(0, 0, 0, 0, 0, 0, 0, 0), B(0, 0, 0, 0, 0, 0, 0, 0) }, // RI
};
// clang-format on

#undef B
#undef F
#undef DI
#undef AL

static_assert(WTF_ARRAY_LENGTH(kAsciiLineBreakTable) ==
                  kAsciiLineBreakTableLastChar - kAsciiLineBreakTableFirstChar +
                      1,
              "asciiLineBreakTable should be consistent");
static_assert(WTF_ARRAY_LENGTH(kBreakAllLineBreakClassTable) == BA_LB_COUNT,
              "breakAllLineBreakClassTable should be consistent");

static inline bool ShouldBreakAfter(UChar last_ch, UChar ch, UChar next_ch) {
  // Don't allow line breaking between '-' and a digit if the '-' may mean a
  // minus sign in the context, while allow breaking in 'ABCD-1234' and
  // '1234-5678' which may be in long URLs.
  if (ch == '-' && IsASCIIDigit(next_ch))
    return IsASCIIAlphanumeric(last_ch);

  // If both ch and nextCh are ASCII characters, use a lookup table for enhanced
  // speed and for compatibility with other browsers (see comments for
  // asciiLineBreakTable for details).
  if (ch >= kAsciiLineBreakTableFirstChar &&
      ch <= kAsciiLineBreakTableLastChar &&
      next_ch >= kAsciiLineBreakTableFirstChar &&
      next_ch <= kAsciiLineBreakTableLastChar) {
    const unsigned char* table_row =
        kAsciiLineBreakTable[ch - kAsciiLineBreakTableFirstChar];
    int next_ch_index = next_ch - kAsciiLineBreakTableFirstChar;
    return table_row[next_ch_index / 8] & (1 << (next_ch_index % 8));
  }
  // Otherwise defer to the Unicode algorithm by returning false.
  return false;
}

static inline ULineBreak LineBreakPropertyValue(UChar last_ch, UChar ch) {
  if (ch == '+')  // IE tailors '+' to AL-like class when break-all is enabled.
    return U_LB_ALPHABETIC;
  UChar32 ch32 = U16_IS_LEAD(last_ch) && U16_IS_TRAIL(ch)
                     ? U16_GET_SUPPLEMENTARY(last_ch, ch)
                     : ch;
  return static_cast<ULineBreak>(u_getIntPropertyValue(ch32, UCHAR_LINE_BREAK));
}

static inline bool ShouldBreakAfterBreakAll(ULineBreak last_line_break,
                                            ULineBreak line_break) {
  if (line_break >= 0 && line_break < BA_LB_COUNT && last_line_break >= 0 &&
      last_line_break < BA_LB_COUNT) {
    const unsigned char* table_row =
        kBreakAllLineBreakClassTable[last_line_break];
    return table_row[line_break / 8] & (1 << (line_break % 8));
  }
  return false;
}

inline bool NeedsLineBreakIterator(UChar ch) {
  return ch > kAsciiLineBreakTableLastChar && ch != kNoBreakSpaceCharacter;
}

template <typename CharacterType, LineBreakType lineBreakType>
static inline int NextBreakablePosition(
    LazyLineBreakIterator& lazy_break_iterator,
    const CharacterType* str,
    unsigned length,
    int pos) {
  int len = static_cast<int>(length);
  int next_break = -1;

  UChar last_last_ch =
      pos > 1 ? str[pos - 2] : lazy_break_iterator.SecondToLastCharacter();
  UChar last_ch = pos > 0 ? str[pos - 1] : lazy_break_iterator.LastCharacter();
  ULineBreak last_line_break;
  if (lineBreakType == LineBreakType::kBreakAll)
    last_line_break = LineBreakPropertyValue(last_last_ch, last_ch);
  unsigned prior_context_length = lazy_break_iterator.PriorContextLength();
  for (int i = pos; i < len; i++) {
    CharacterType ch = str[i];

    if (IsBreakableSpace(ch) || ShouldBreakAfter(last_last_ch, last_ch, ch))
      return i;

    if (lineBreakType == LineBreakType::kBreakAll && !U16_IS_LEAD(ch)) {
      ULineBreak line_break = LineBreakPropertyValue(last_ch, ch);
      if (ShouldBreakAfterBreakAll(last_line_break, line_break))
        return i > pos && U16_IS_TRAIL(ch) ? i - 1 : i;
      if (line_break != U_LB_COMBINING_MARK)
        last_line_break = line_break;
    }

    if (NeedsLineBreakIterator(ch) || NeedsLineBreakIterator(last_ch)) {
      if (next_break < i) {
        // Don't break if positioned at start of primary context and there is no
        // prior context.
        if (i || prior_context_length) {
          TextBreakIterator* break_iterator =
              lazy_break_iterator.Get(prior_context_length);
          if (break_iterator) {
            next_break =
                break_iterator->following(i - 1 + prior_context_length);
            if (next_break >= 0) {
              next_break -= prior_context_length;
            }
          }
        }
      }
      if (i == next_break && !IsBreakableSpace(last_ch))
        return i;
    }

    last_last_ch = last_ch;
    last_ch = ch;
  }

  return len;
}

static inline bool ShouldKeepAfter(UChar last_ch, UChar ch, UChar next_ch) {
  UChar pre_ch = U_MASK(u_charType(ch)) & U_GC_M_MASK ? last_ch : ch;
  return U_MASK(u_charType(pre_ch)) & (U_GC_L_MASK | U_GC_N_MASK) &&
         !WTF::Unicode::HasLineBreakingPropertyComplexContext(pre_ch) &&
         U_MASK(u_charType(next_ch)) & (U_GC_L_MASK | U_GC_N_MASK) &&
         !WTF::Unicode::HasLineBreakingPropertyComplexContext(next_ch);
}

static inline int NextBreakablePositionKeepAllInternal(
    LazyLineBreakIterator& lazy_break_iterator,
    const UChar* str,
    unsigned length,
    int pos) {
  int len = static_cast<int>(length);
  int next_break = -1;

  UChar last_last_ch =
      pos > 1 ? str[pos - 2]
              : static_cast<UChar>(lazy_break_iterator.SecondToLastCharacter());
  UChar last_ch = pos > 0
                      ? str[pos - 1]
                      : static_cast<UChar>(lazy_break_iterator.LastCharacter());
  unsigned prior_context_length = lazy_break_iterator.PriorContextLength();
  for (int i = pos; i < len; i++) {
    UChar ch = str[i];

    if (IsBreakableSpace(ch) || ShouldBreakAfter(last_last_ch, last_ch, ch))
      return i;

    if (!ShouldKeepAfter(last_last_ch, last_ch, ch) &&
        (NeedsLineBreakIterator(ch) || NeedsLineBreakIterator(last_ch))) {
      if (next_break < i) {
        // Don't break if positioned at start of primary context and there is no
        // prior context.
        if (i || prior_context_length) {
          TextBreakIterator* break_iterator =
              lazy_break_iterator.Get(prior_context_length);
          if (break_iterator) {
            next_break =
                break_iterator->following(i - 1 + prior_context_length);
            if (next_break >= 0) {
              next_break -= prior_context_length;
            }
          }
        }
      }
      if (i == next_break && !IsBreakableSpace(last_ch))
        return i;
    }

    last_last_ch = last_ch;
    last_ch = ch;
  }

  return len;
}

template <LineBreakType lineBreakType>
static inline int NextBreakablePosition(
    LazyLineBreakIterator& lazy_break_iterator,
    const String& string,
    int pos) {
  if (string.Is8Bit())
    return NextBreakablePosition<LChar, lineBreakType>(
        lazy_break_iterator, string.Characters8(), string.length(), pos);
  return NextBreakablePosition<UChar, lineBreakType>(
      lazy_break_iterator, string.Characters16(), string.length(), pos);
}

int LazyLineBreakIterator::NextBreakablePositionIgnoringNBSP(int pos) {
  return NextBreakablePosition<LineBreakType::kNormal>(*this, string_, pos);
}

int LazyLineBreakIterator::NextBreakablePositionBreakAll(int pos) {
  return NextBreakablePosition<LineBreakType::kBreakAll>(*this, string_, pos);
}

int LazyLineBreakIterator::NextBreakablePositionKeepAll(int pos) {
  if (string_.Is8Bit())
    return NextBreakablePosition<LChar, LineBreakType::kNormal>(
        *this, string_.Characters8(), string_.length(), pos);
  return NextBreakablePositionKeepAllInternal(*this, string_.Characters16(),
                                              string_.length(), pos);
}

}  // namespace blink
