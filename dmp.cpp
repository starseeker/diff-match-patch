/*
 * Diff Match and Patch
 * Copyright 2018 The diff-match-patch Authors.
 * https://github.com/google/diff-match-patch
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <algorithm>
#include <codecvt>
#include <cwctype>
#include <limits>
#include <locale>
#include <regex>
#include <sstream>
#include <stack>
#include <tuple>
#include <time.h>
#include "./dmp.h"

static inline std::string ToUTF8(const std::wstring& str)
{
  std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>> converter;
  return converter.to_bytes(str);
}
static inline std::wstring ToUTF16(const std::string& str)
{
#ifdef WIN32
  std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>> converter;
#else  // g++ 5.4
  std::wstring_convert<std::codecvt_utf8<wchar_t>> converter;
#endif
  return converter.from_bytes(str);
}

std::wstring toPercentEncoding(const std::wstring& input, const std::string exclude="")
{
  if (input.empty())
    return input;
  std::string inputUTF8 = ToUTF8(input);
  std::stringstream outputUTF8;
  for (unsigned int i = 0; i < inputUTF8.length(); ++i)
  {
    unsigned char c = inputUTF8[i];
    if (((c >= 0x61 && c <= 0x7A)    // ALPHA
         || (c >= 0x41 && c <= 0x5A) // ALPHA
         || (c >= 0x30 && c <= 0x39) // DIGIT
         || c == 0x2D                // -
         || c == 0x2E                // .
         || c == 0x5F                // _
         || c == 0x7E                // ~
         || exclude.find(c) != std::string::npos))
      outputUTF8 << c;
    else
    {
      outputUTF8 << "%" << std::hex << std::uppercase << ((c & 0xf0) >> 4) << (c & 0xf);
      outputUTF8 << std::nouppercase;
    }
  }
  return ToUTF16(outputUTF8.str());
}

std::wstring fromPercentEncoding(const std::wstring &input)
{
  if (input.empty())
    return input;
  std::string inputUTF8 = ToUTF8(input);
  std::stringstream outputUTF8;
  for (auto charIterator = inputUTF8.begin(); charIterator < inputUTF8.end(); charIterator++)
  {
    if (*charIterator != '%')
      outputUTF8 << *charIterator;
    else
    {
      charIterator++;
      outputUTF8 << (char)std::stoi(std::string(charIterator, charIterator + 2), nullptr, 16);
      charIterator++;
    }
  }
  return ToUTF16(outputUTF8.str());
}

//////////////////////////
//
// Diff Class
//
//////////////////////////


/**
 * Constructor.  Initializes the diff with the provided values.
 * @param operation One of Diff::Operation::Insert, Diff::Operation::Delete or Diff::Operation::Equal
 * @param text The text being applied
 */
Diff::Diff(Operation _operation, const std::wstring &_text) :
  operation(_operation), text(_text) {
  // Construct a diff with the specified operation and text.
}

Diff::Diff() {
}


std::wstring Diff::strOperation(Operation op) {
  switch (op) {
    case Diff::Operation::Insert:
      return L"Insert";
    case Diff::Operation::Delete:
      return L"Delete";
    case Diff::Operation::Equal:
      return L"Equal";
  }
  throw "Invalid operation.";
}

/**
 * Display a human-readable version of this Diff.
 * @return text version
 */
std::wstring Diff::toString() const {
  std::wstring prettyText = text;
  // Replace linebreaks with Pilcrow signs.
  std::replace(prettyText.begin(), prettyText.end(), L'\n', L'\u00b6');
  // prettyText.replace('\n', L'\u00b6');
  return std::wstring(L"Diff(") + strOperation(operation) + std::wstring(L",\"")
      + prettyText + std::wstring(L"\")");
}

/**
 * Is this Diff equivalent to another Diff?
 * @param d Another Diff to compare against
 * @return true or false
 */
bool Diff::operator==(const Diff &d) const {
  return (d.operation == this->operation) && (d.text == this->text);
}

bool Diff::operator!=(const Diff &d) const {
  return !(operator == (d));
}


/////////////////////////////////////////////
//
// Patch Class
//
/////////////////////////////////////////////


/**
 * Constructor.  Initializes with an empty list of diffs.
 */
Patch::Patch() :
  start1(0), start2(0),
  length1(0), length2(0) {
}

bool Patch::empty() const {
  if (start1 == 0 && start2 == 0 && length1 == 0 && length2 == 0
      && diffs.size() == 0) {
    return true;
  }
  return false;
}


/**
 * Emulate GNU diff's format.
 * Header: @@ -382,8 +481,9 @@
 * Indices are printed as 1-based, not 0-based.
 * @return The GNU diff string
 */
std::wstring Patch::toString() const {
  std::wstring coords1, coords2;
  if (length1 == 0) {
    coords1 = std::to_wstring(start1) + std::wstring(L",0");
  } else if (length1 == 1) {
    coords1 = std::to_wstring(start1 + 1);
  } else {
    coords1 = std::to_wstring(start1 + 1) + std::wstring(L",")
        + std::to_wstring(length1);
  }
  if (length2 == 0) {
    coords2 = std::to_wstring(start2) + std::wstring(L",0");
  } else if (length2 == 1) {
    coords2 = std::to_wstring(start2 + 1);
  } else {
    coords2 = std::to_wstring(start2 + 1) + std::wstring(L",")
        + std::to_wstring(length2);
  }
  std::wstring text;
  text = std::wstring(L"@@ -") + coords1 + std::wstring(L" +") + coords2
      + std::wstring(L" @@\n");
  // Escape the body of the patch with %xx notation.
  for (const Diff& aDiff : diffs) {
    switch (aDiff.operation) {
      case Diff::Operation::Insert:
        text += std::wstring(L"+");
        break;
      case Diff::Operation::Delete:
        text += std::wstring(L"-");
        break;
      case Diff::Operation::Equal:
        text += std::wstring(L" ");
        break;
    }
    text += toPercentEncoding(aDiff.text, " !~*'();/?:@&=+$,#") + std::wstring(L"\n");
  }

  return text;
}


/////////////////////////////////////////////
//
// diff_match_patch Class
//
/////////////////////////////////////////////

diff_match_patch::diff_match_patch() :
  Diff_Timeout(1.0f),
  Diff_EditCost(4),
  Match_Threshold(0.5f),
  Match_Distance(1000),
  Patch_DeleteThreshold(0.5f),
  Patch_Margin(4),
  Match_MaxBits(32) {
}


std::deque<Diff> diff_match_patch::diff_main(const std::wstring &text1,
                                        const std::wstring &text2) {
  return diff_main(text1, text2, true);
}

std::deque<Diff> diff_match_patch::diff_main(const std::wstring &text1,
    const std::wstring &text2, bool checklines) {
  // Set a deadline by which time the diff must be complete.
  clock_t deadline;
  if (Diff_Timeout <= 0) {
    deadline = std::numeric_limits<clock_t>::max();
  } else {
    deadline = clock() + (clock_t)(Diff_Timeout * CLOCKS_PER_SEC);
  }
  return diff_main(text1, text2, checklines, deadline);
}

std::deque<Diff> diff_match_patch::diff_main(const std::wstring &text1,
    const std::wstring &text2, bool checklines, clock_t deadline) {
  // Check for equality (speedup).
  std::deque<Diff> diffs;
  if (text1 == text2) {
    if (!text1.empty()) {
      diffs.push_back(Diff(Diff::Operation::Equal, text1));
    }
    return diffs;
  }

  // Trim off common prefix (speedup).
  int commonlength = diff_commonPrefix(text1, text2);
  const std::wstring &commonprefix = text1.substr(0, commonlength);
  std::wstring textChopped1 = text1.substr(commonlength);
  std::wstring textChopped2 = text2.substr(commonlength);

  // Trim off common suffix (speedup).
  commonlength = diff_commonSuffix(textChopped1, textChopped2);
  const std::wstring &commonsuffix = textChopped1.substr(textChopped1.length() - commonlength);
  textChopped1 = textChopped1.substr(0, textChopped1.length() - commonlength);
  textChopped2 = textChopped2.substr(0, textChopped2.length() - commonlength);

  // Compute the diff on the middle block.
  diffs = diff_compute(textChopped1, textChopped2, checklines, deadline);

  // Restore the prefix and suffix.
  if (!commonprefix.empty()) {
    diffs.push_front(Diff(Diff::Operation::Equal, commonprefix));
  }
  if (!commonsuffix.empty()) {
    diffs.push_back(Diff(Diff::Operation::Equal, commonsuffix));
  }

  diff_cleanupMerge(diffs);

  return diffs;
}


std::deque<Diff> diff_match_patch::diff_compute(std::wstring text1, std::wstring text2,
    bool checklines, clock_t deadline) {
  std::deque<Diff> diffs;

  if (text1.empty()) {
    // Just add some text (speedup).
    diffs.push_back(Diff(Diff::Operation::Insert, text2));
    return diffs;
  }

  if (text2.empty()) {
    // Just delete some text (speedup).
    diffs.push_back(Diff(Diff::Operation::Delete, text1));
    return diffs;
  }

  {
    const std::wstring longtext = text1.length() > text2.length() ? text1 : text2;
    const std::wstring shorttext = text1.length() > text2.length() ? text2 : text1;
    const int i = longtext.find(shorttext);
    if (i != -1) {
      // Shorter text is inside the longer text (speedup).
      const Diff::Operation op = (text1.length() > text2.length()) ? Diff::Operation::Delete : Diff::Operation::Insert;
      diffs.push_back(Diff(op, longtext.substr(0, i)));
      diffs.push_back(Diff(Diff::Operation::Equal, shorttext));
      diffs.push_back(Diff(op, longtext.substr(i + shorttext.length())));
      return diffs;
    }

    if (shorttext.length() == 1) {
      // Single character string.
      // After the previous speedup, the character can't be an equality.
      diffs.push_back(Diff(Diff::Operation::Delete, text1));
      diffs.push_back(Diff(Diff::Operation::Insert, text2));
      return diffs;
    }
    // Garbage collect longtext and shorttext by scoping out.
  }

  // Check to see if the problem can be split in two.
  const std::deque<std::wstring> hm = diff_halfMatch(text1, text2);
  if (hm.size() > 0) {
    // A half-match was found, sort out the return data.
    const std::wstring text1_a = hm[0];
    const std::wstring text1_b = hm[1];
    const std::wstring text2_a = hm[2];
    const std::wstring text2_b = hm[3];
    const std::wstring mid_common = hm[4];
    // Send both pairs off for separate processing.
    const std::deque<Diff> diffs_a = diff_main(text1_a, text2_a,
                                          checklines, deadline);
    const std::deque<Diff> diffs_b = diff_main(text1_b, text2_b,
                                          checklines, deadline);
    // Merge the results.
    diffs = diffs_a;
    diffs.push_back(Diff(Diff::Operation::Equal, mid_common));
    diffs.insert(diffs.end(), diffs_b.begin(), diffs_b.end());
    return diffs;
  }

  // Perform a real diff.
  if (checklines && text1.length() > 100 && text2.length() > 100) {
    return diff_lineMode(text1, text2, deadline);
  }

  return diff_bisect(text1, text2, deadline);
}


std::deque<Diff> diff_match_patch::diff_lineMode(std::wstring text1, std::wstring text2,
    clock_t deadline) {
  // Scan the text on a line-by-line basis first.
  const auto b = diff_linesToChars(text1, text2);
  text1 = std::get<0>(b);
  text2 = std::get<1>(b);
  std::deque<std::wstring> linearray = std::get<2>(b);

  std::deque<Diff> diffs = diff_main(text1, text2, false, deadline);

  // Convert the diff back to original text.
  diff_charsToLines(diffs, linearray);
  // Eliminate freak matches (e.g. blank lines)
  diff_cleanupSemantic(diffs);

  // Rediff any replacement blocks, this time character-by-character.
  // Add a dummy entry at the end.
  diffs.push_back(Diff(Diff::Operation::Equal, L""));
  int count_delete = 0;
  int count_insert = 0;
  std::wstring text_delete = L"";
  std::wstring text_insert = L"";

  auto thisDiff = diffs.begin();
  // Diff *thisDiff = pointer.hasNext() ? &pointer.next() : NULL;
  while (thisDiff != diffs.end()) {
    switch (thisDiff->operation) {
      case Diff::Operation::Insert:
        count_insert++;
        text_insert += thisDiff->text;
        break;
      case Diff::Operation::Delete:
        count_delete++;
        text_delete += thisDiff->text;
        break;
      case Diff::Operation::Equal:
        // Upon reaching an equality, check for prior redundancies.
        if (count_delete >= 1 && count_insert >= 1) {
          // Delete the offending records and add the merged ones.
          auto deleteBegin = std::prev(thisDiff, count_delete + count_insert);
          int thisDiffIndex = deleteBegin - diffs.begin();
          diffs.erase(deleteBegin, thisDiff);
          // pointer.previous();
          // for (int j = 0; j < count_delete + count_insert; j++) {
            // pointer.previous();
            // pointer.remove();
          // }
          auto newDiffs = diff_main(text_delete, text_insert, false, deadline);
          diffs.insert(diffs.begin() + thisDiffIndex, newDiffs.begin(), newDiffs.end());
          thisDiff = diffs.begin() + thisDiffIndex + newDiffs.size();
          // for (Diff newDiff : diff_main(text_delete, text_insert, false, deadline))
            // pointer.insert(newDiff);
        }
        count_insert = 0;
        count_delete = 0;
        text_delete = L"";
        text_insert = L"";
        break;
    }
    thisDiff++;
  }
  diffs.pop_back();  // Remove the dummy entry at the end.

  return diffs;
}


std::deque<Diff> diff_match_patch::diff_bisect(const std::wstring &text1,
    const std::wstring &text2, clock_t deadline) {
  // Cache the text lengths to prevent multiple calls.
  const int text1_length = text1.length();
  const int text2_length = text2.length();
  const int max_d = (text1_length + text2_length + 1) / 2;
  const int v_offset = max_d;
  const int v_length = 2 * max_d;
  std::vector<int> v1(v_length);
  std::vector<int> v2(v_length);
  for (int x = 0; x < v_length; x++) {
    v1[x] = -1;
    v2[x] = -1;
  }
  v1[v_offset + 1] = 0;
  v2[v_offset + 1] = 0;
  const int delta = text1_length - text2_length;
  // If the total number of characters is odd, then the front path will
  // collide with the reverse path.
  const bool front = (delta % 2 != 0);
  // Offsets for start and end of k loop.
  // Prevents mapping of space beyond the grid.
  int k1start = 0;
  int k1end = 0;
  int k2start = 0;
  int k2end = 0;
  for (int d = 0; d < max_d; d++) {
    // Bail out if deadline is reached.
    if (clock() > deadline) {
      break;
    }

    // Walk the front path one step.
    for (int k1 = -d + k1start; k1 <= d - k1end; k1 += 2) {
      const int k1_offset = v_offset + k1;
      int x1;
      if (k1 == -d || (k1 != d && v1[k1_offset - 1] < v1[k1_offset + 1])) {
        x1 = v1[k1_offset + 1];
      } else {
        x1 = v1[k1_offset - 1] + 1;
      }
      int y1 = x1 - k1;
      while (x1 < text1_length && y1 < text2_length
          && text1[x1] == text2[y1]) {
        x1++;
        y1++;
      }
      v1[k1_offset] = x1;
      if (x1 > text1_length) {
        // Ran off the right of the graph.
        k1end += 2;
      } else if (y1 > text2_length) {
        // Ran off the bottom of the graph.
        k1start += 2;
      } else if (front) {
        int k2_offset = v_offset + delta - k1;
        if (k2_offset >= 0 && k2_offset < v_length && v2[k2_offset] != -1) {
          // Mirror x2 onto top-left coordinate system.
          int x2 = text1_length - v2[k2_offset];
          if (x1 >= x2) {
            // Overlap detected.
            return diff_bisectSplit(text1, text2, x1, y1, deadline);
          }
        }
      }
    }

    // Walk the reverse path one step.
    for (int k2 = -d + k2start; k2 <= d - k2end; k2 += 2) {
      const int k2_offset = v_offset + k2;
      int x2;
      if (k2 == -d || (k2 != d && v2[k2_offset - 1] < v2[k2_offset + 1])) {
        x2 = v2[k2_offset + 1];
      } else {
        x2 = v2[k2_offset - 1] + 1;
      }
      int y2 = x2 - k2;
      while (x2 < text1_length && y2 < text2_length
          && text1[text1_length - x2 - 1] == text2[text2_length - y2 - 1]) {
        x2++;
        y2++;
      }
      v2[k2_offset] = x2;
      if (x2 > text1_length) {
        // Ran off the left of the graph.
        k2end += 2;
      } else if (y2 > text2_length) {
        // Ran off the top of the graph.
        k2start += 2;
      } else if (!front) {
        int k1_offset = v_offset + delta - k2;
        if (k1_offset >= 0 && k1_offset < v_length && v1[k1_offset] != -1) {
          int x1 = v1[k1_offset];
          int y1 = v_offset + x1 - k1_offset;
          // Mirror x2 onto top-left coordinate system.
          x2 = text1_length - x2;
          if (x1 >= x2) {
            // Overlap detected.
            return diff_bisectSplit(text1, text2, x1, y1, deadline);
          }
        }
      }
    }
  }
  // Diff took too long and hit the deadline or
  // number of diffs equals number of characters, no commonality at all.
  std::deque<Diff> diffs;
  diffs.push_back(Diff(Diff::Operation::Delete, text1));
  diffs.push_back(Diff(Diff::Operation::Insert, text2));
  return diffs;
}

std::deque<Diff> diff_match_patch::diff_bisectSplit(const std::wstring &text1,
    const std::wstring &text2, int x, int y, clock_t deadline) {
  std::wstring text1a = text1.substr(0, x);
  std::wstring text2a = text2.substr(0, y);
  std::wstring text1b = text1.substr(x);
  std::wstring text2b = text2.substr(y);

  // Compute both diffs serially.
  std::deque<Diff> diffs = diff_main(text1a, text2a, false, deadline);
  std::deque<Diff> diffsb = diff_main(text1b, text2b, false, deadline);
  diffs.insert(diffs.end(), diffsb.begin(), diffsb.end());

  return diffs;
}

std::tuple<std::wstring, std::wstring, std::deque<std::wstring>> diff_match_patch::diff_linesToChars(const std::wstring &text1,
                                                    const std::wstring &text2) {
  std::deque<std::wstring> lineArray;
  std::map<std::wstring, int> lineHash;
  // e.g. linearray[4] == "Hello\n"
  // e.g. linehash.get("Hello\n") == 4

  // "\x00" is a valid character, but various debuggers don't like it.
  // So we'll insert a junk entry to avoid generating a null character.
  lineArray.push_back(L"");

  const std::wstring chars1 = diff_linesToCharsMunge(text1, lineArray, lineHash);
  const std::wstring chars2 = diff_linesToCharsMunge(text2, lineArray, lineHash);

  return std::make_tuple(chars1, chars2, lineArray);
}


std::wstring diff_match_patch::diff_linesToCharsMunge(const std::wstring &text,
                                                 std::deque<std::wstring> &lineArray,
                                                 std::map<std::wstring, int> &lineHash) {
  int lineStart = 0;
  int lineEnd = -1;
  std::wstring line;
  std::wstring chars;
  // Walk the text, pulling out a substring for each line.
  // text.split('\n') would would temporarily double our memory footprint.
  // Modifying text would create many large strings to garbage collect.
  while (lineEnd < (int)text.length() - 1) {
    lineEnd = text.find('\n', lineStart);
    if (lineEnd == -1) {
      lineEnd = text.length() - 1;
    }
    line = text.substr(lineStart, lineEnd + 1 - lineStart);
    lineStart = lineEnd + 1;

    if (lineHash.find(line) != lineHash.end()) {
      chars += wchar_t(static_cast<unsigned short>(lineHash[line]));
    } else {
      lineArray.push_back(line);
      lineHash.emplace(line, lineArray.size() - 1);
      chars += wchar_t(static_cast<unsigned short>(lineArray.size() - 1));
    }
  }
  return chars;
}



void diff_match_patch::diff_charsToLines(std::deque<Diff> &diffs,
                                         const std::deque<std::wstring> &lineArray) {
  for (Diff &diff : diffs)
  {
    std::wstring text;
    for (size_t y = 0; y < diff.text.length(); y++)
    {
      text += lineArray[static_cast<unsigned short>(diff.text[y])];
    }
    diff.text = text;
  }
}


int diff_match_patch::diff_commonPrefix(const std::wstring &text1,
                                        const std::wstring &text2) {
  // Performance analysis: http://neil.fraser.name/news/2007/10/09/
  const int n = std::min(text1.length(), text2.length());
  for (int i = 0; i < n; i++) {
    if (text1[i] != text2[i]) {
      return i;
    }
  }
  return n;
}


int diff_match_patch::diff_commonSuffix(const std::wstring &text1,
                                        const std::wstring &text2) {
  // Performance analysis: http://neil.fraser.name/news/2007/10/09/
  const int text1_length = text1.length();
  const int text2_length = text2.length();
  const int n = std::min(text1_length, text2_length);
  for (int i = 1; i <= n; i++) {
    if (text1[text1_length - i] != text2[text2_length - i]) {
      return i - 1;
    }
  }
  return n;
}

int diff_match_patch::diff_commonOverlap(const std::wstring &text1,
                                         const std::wstring &text2) {
  // Cache the text lengths to prevent multiple calls.
  const int text1_length = text1.length();
  const int text2_length = text2.length();
  // Eliminate the null case.
  if (text1_length == 0 || text2_length == 0) {
    return 0;
  }
  // Truncate the longer string.
  std::wstring text1_trunc = text1;
  std::wstring text2_trunc = text2;
  if (text1_length > text2_length) {
    text1_trunc = text1.substr(text1_length - text2_length);
  } else if (text1_length < text2_length) {
    text2_trunc = text2.substr(0, text1_length);
  }
  const int text_length = std::min(text1_length, text2_length);
  // Quick check for the worst case.
  if (text1_trunc == text2_trunc) {
    return text_length;
  }

  // Start by looking for a single character match
  // and increase length until no match is found.
  // Performance analysis: http://neil.fraser.name/news/2010/11/04/
  int best = 0;
  int length = 1;
  while (true) {
    std::wstring pattern = text1_trunc.substr(text1_trunc.length() - length);
    int found = text2_trunc.find(pattern);
    if (found == -1) {
      return best;
    }
    length += found;
    if (found == 0 || text1_trunc.substr(text1_trunc.length() - length) == text2_trunc.substr(0, length)) {
      best = length;
      length++;
    }
  }
}

std::deque<std::wstring> diff_match_patch::diff_halfMatch(const std::wstring &text1,
                                             const std::wstring &text2) {
  if (Diff_Timeout <= 0) {
    // Don't risk returning a non-optimal diff if we have unlimited time.
    return std::deque<std::wstring>();
  }
  const std::wstring longtext = text1.length() > text2.length() ? text1 : text2;
  const std::wstring shorttext = text1.length() > text2.length() ? text2 : text1;
  if (longtext.length() < 4 || shorttext.length() * 2 < longtext.length()) {
    return std::deque<std::wstring>();  // Pointless.
  }

  // First check if the second quarter is the seed for a half-match.
  const std::deque<std::wstring> hm1 = diff_halfMatchI(longtext, shorttext,
      (longtext.length() + 3) / 4);
  // Check again based on the third quarter.
  const std::deque<std::wstring> hm2 = diff_halfMatchI(longtext, shorttext,
      (longtext.length() + 1) / 2);
  std::deque<std::wstring> hm;
  if (hm1.empty() && hm2.empty()) {
    return std::deque<std::wstring>();
  } else if (hm2.empty()) {
    hm = hm1;
  } else if (hm1.empty()) {
    hm = hm2;
  } else {
    // Both matched.  Select the longest.
    hm = hm1[4].length() > hm2[4].length() ? hm1 : hm2;
  }

  // A half-match was found, sort out the return data.
  if (text1.length() > text2.length()) {
    return hm;
  } else {
    std::deque<std::wstring> listRet{hm[2], hm[3], hm[0], hm[1], hm[4]};
    // listRet << hm[2] << hm[3] << hm[0] << hm[1] << hm[4];
    return listRet;
  }
}


std::deque<std::wstring> diff_match_patch::diff_halfMatchI(const std::wstring &longtext,
                                              const std::wstring &shorttext,
                                              int i) {
  // Start with a 1/4 length substring at position i as a seed.
  const std::wstring seed = longtext.substr(i, longtext.length() / 4);
  int j = -1;
  std::wstring best_common;
  std::wstring best_longtext_a, best_longtext_b;
  std::wstring best_shorttext_a, best_shorttext_b;
  while ((j = shorttext.find(seed, j + 1)) != -1) {
    const int prefixLength = diff_commonPrefix(longtext.substr(i),
        shorttext.substr(j));
    const int suffixLength = diff_commonSuffix(longtext.substr(0, i),
        shorttext.substr(0, j));
    if (best_common.length() < suffixLength + prefixLength) {
      best_common = shorttext.substr(j - suffixLength, suffixLength)
          + shorttext.substr(j, prefixLength);
      best_longtext_a = longtext.substr(0, i - suffixLength);
      best_longtext_b = longtext.substr(i + prefixLength);
      best_shorttext_a = shorttext.substr(0, j - suffixLength);
      best_shorttext_b = shorttext.substr(j + prefixLength);
    }
  }
  if (best_common.length() * 2 >= longtext.length()) {
    std::deque<std::wstring> listRet{best_longtext_a, best_longtext_b, best_shorttext_a, best_shorttext_b, best_common};
    return listRet;
  } else {
    return std::deque<std::wstring>();
  }
}


void diff_match_patch::diff_cleanupSemantic(std::deque<Diff> &diffs) {
  if (diffs.empty()) {
    return;
  }
  bool changes = false;
  std::stack<Diff> equalities;  // Stack of equalities.
  std::wstring lastequality;  // Always equal to equalities.lastElement().text
  // Number of characters that changed prior to the equality.
  int length_insertions1 = 0;
  int length_deletions1 = 0;
  // Number of characters that changed after the equality.
  int length_insertions2 = 0;
  int length_deletions2 = 0;
  auto thisDiff = diffs.begin();
  while (thisDiff != diffs.end()) {
    if (thisDiff->operation == Diff::Operation::Equal) {
      // Equality found.
      equalities.push(*thisDiff);
      length_insertions1 = length_insertions2;
      length_deletions1 = length_deletions2;
      length_insertions2 = 0;
      length_deletions2 = 0;
      lastequality = thisDiff->text;
    } else {
      // An insertion or deletion.
      if (thisDiff->operation == Diff::Operation::Insert) {
        length_insertions2 += thisDiff->text.length();
      } else {
        length_deletions2 += thisDiff->text.length();
      }
      // Eliminate an equality that is smaller or equal to the edits on both
      // sides of it.
      if (!lastequality.empty()
          && (lastequality.length()
              <= std::max(length_insertions1, length_deletions1))
          && (lastequality.length()
              <= std::max(length_insertions2, length_deletions2))) {
        // printf("Splitting: '%s'\n", qPrintable(lastequality));
        // Walk back to offending equality.
        while (*thisDiff != equalities.top()) {
          thisDiff--;
        }

        // Replace equality with a delete.
        *thisDiff = Diff(Diff::Operation::Delete, lastequality);
        // Insert a corresponding an insert.
        {
          int thisDiffIndex = thisDiff - diffs.begin();
          diffs.insert(thisDiff + 1, Diff(Diff::Operation::Insert, lastequality));
          thisDiff = diffs.begin() + thisDiffIndex;
        }

        equalities.pop();  // Throw away the equality we just deleted.
        if (!equalities.empty()) {
          // Throw away the previous equality (it needs to be reevaluated).
          equalities.pop();
        }
        if (equalities.empty()) {
          // There are no previous equalities, walk back to the start.
          while (thisDiff != diffs.begin()) {
            thisDiff--;
          }
        } else {
          // There is a safe equality we can fall back to.
          while (*thisDiff != equalities.top())
            thisDiff--;
        }

        length_insertions1 = 0;  // Reset the counters.
        length_deletions1 = 0;
        length_insertions2 = 0;
        length_deletions2 = 0;
        lastequality = std::wstring();
        changes = true;

        continue;
      }
    }
    thisDiff++;
  }

  // Normalize the diff.
  if (changes) {
    diff_cleanupMerge(diffs);
  }
  diff_cleanupSemanticLossless(diffs);

  // Find any overlaps between deletions and insertions.
  // e.g: <del>abcxxx</del><ins>xxxdef</ins>
  //   -> <del>abc</del>xxx<ins>def</ins>
  // e.g: <del>xxxabc</del><ins>defxxx</ins>
  //   -> <ins>def</ins>xxx<del>abc</del>
  // Only extract an overlap if it is as big as the edit ahead or behind it.
  if (diffs.size() > 1)
    thisDiff = diffs.begin() + 1;
  else
    thisDiff = diffs.end();
  while (thisDiff != diffs.end()) {
    auto prevDiff = thisDiff - 1;
    if (prevDiff->operation == Diff::Operation::Delete &&
        thisDiff->operation == Diff::Operation::Insert) {
      std::wstring deletion = prevDiff->text;
      std::wstring insertion = thisDiff->text;
      int overlap_length1 = diff_commonOverlap(deletion, insertion);
      int overlap_length2 = diff_commonOverlap(insertion, deletion);
      if (overlap_length1 >= overlap_length2) {
        if (overlap_length1 >= deletion.length() / 2.0 ||
            overlap_length1 >= insertion.length() / 2.0) {
          // Overlap found.  Insert an equality and trim the surrounding edits.
          int prevDiffIndex = prevDiff - diffs.begin();
          diffs.insert(thisDiff, Diff(Diff::Operation::Equal, insertion.substr(0, overlap_length1)));
          prevDiff = diffs.begin() + prevDiffIndex;
          thisDiff = prevDiff + 2;
          prevDiff->text =
              deletion.substr(0, deletion.length() - overlap_length1);
          thisDiff->text = insertion.substr(overlap_length1);
          // pointer.insert inserts the element before the cursor, so there is
          // no need to step past the new element.
        }
      } else {
        if (overlap_length2 >= deletion.length() / 2.0 ||
            overlap_length2 >= insertion.length() / 2.0) {
          // Reverse overlap found.
          // Insert an equality and swap and trim the surrounding edits.
          int prevDiffIndex = prevDiff - diffs.begin();
          diffs.insert(thisDiff, Diff(Diff::Operation::Equal, deletion.substr(0, overlap_length2)));
          prevDiff = diffs.begin() + prevDiffIndex;
          thisDiff = prevDiff + 2;
          prevDiff->operation = Diff::Operation::Insert;
          prevDiff->text =
              insertion.substr(0, insertion.length() - overlap_length2);
          thisDiff->operation = Diff::Operation::Delete;
          thisDiff->text = deletion.substr(overlap_length2);
          // pointer.insert inserts the element before the cursor, so there is
          // no need to step past the new element.
        }
      }
    }
    thisDiff++;
  }
}


void diff_match_patch::diff_cleanupSemanticLossless(std::deque<Diff> &diffs) {
  if (diffs.size() < 3)
    return;
  std::wstring equality1, edit, equality2;
  std::wstring commonString;
  int commonOffset;
  int score, bestScore;
  std::wstring bestEquality1, bestEdit, bestEquality2;
  // Create a new iterator at the start.
  auto thisDiff = diffs.begin() + 1;

  // Intentionally ignore the first and last element (don't need checking).
  while (thisDiff + 1 != diffs.end()) {
    auto prevDiff = thisDiff - 1;
    auto nextDiff = thisDiff + 1;
    if (prevDiff->operation == Diff::Operation::Equal &&
      nextDiff->operation == Diff::Operation::Equal) {
        // This is a single edit surrounded by equalities.
        equality1 = prevDiff->text;
        edit = thisDiff->text;
        equality2 = nextDiff->text;

        // First, shift the edit as far left as possible.
        commonOffset = diff_commonSuffix(equality1, edit);
        if (commonOffset != 0) {
          commonString = edit.substr(edit.length() - commonOffset);
          equality1 = equality1.substr(0, equality1.length() - commonOffset);
          edit = commonString + edit.substr(0, edit.length() - commonOffset);
          equality2 = commonString + equality2;
        }

        // Second, step character by character right, looking for the best fit.
        bestEquality1 = equality1;
        bestEdit = edit;
        bestEquality2 = equality2;
        bestScore = diff_cleanupSemanticScore(equality1, edit)
            + diff_cleanupSemanticScore(edit, equality2);
        while (!edit.empty() && !equality2.empty()
            && edit[0] == equality2[0]) {
          equality1 += edit[0];
          edit = edit.substr(1) + equality2[0];
          equality2 = equality2.substr(1);
          score = diff_cleanupSemanticScore(equality1, edit)
              + diff_cleanupSemanticScore(edit, equality2);
          // The >= encourages trailing rather than leading whitespace on edits.
          if (score >= bestScore) {
            bestScore = score;
            bestEquality1 = equality1;
            bestEdit = edit;
            bestEquality2 = equality2;
          }
        }

        if (prevDiff->text != bestEquality1) {
          // We have an improvement, save it back to the diff.
          if (!bestEquality1.empty()) {
            prevDiff->text = bestEquality1;
          } else {
            int prevDiffIndex = prevDiff - diffs.begin();
            diffs.erase(prevDiff);
            thisDiff = diffs.begin() + prevDiffIndex;
            nextDiff = thisDiff + 1;
          }
          thisDiff->text = bestEdit;
          if (!bestEquality2.empty()) {
            nextDiff->text = bestEquality2;
          } else {
            int prevDiffIndex = prevDiff - diffs.begin();
            diffs.erase(nextDiff);
            thisDiff = diffs.begin() + prevDiffIndex;
          }
        }
    }
    thisDiff++;
  }
}


int diff_match_patch::diff_cleanupSemanticScore(const std::wstring &one,
                                                const std::wstring &two) {
  if (one.empty() || two.empty()) {
    // Edges are the best.
    return 6;
  }

  // Each port of this function behaves slightly differently due to
  // subtle differences in each language's definition of things like
  // 'whitespace'.  Since this function's purpose is largely cosmetic,
  // the choice has been made to use each language's native features
  // rather than force total conformity.
  wchar_t char1 = one[one.length() - 1];
  wchar_t char2 = two[0];
  bool nonAlphaNumeric1 = !std::iswalnum(char1);
  bool nonAlphaNumeric2 = !std::iswalnum(char2);
  bool whitespace1 = nonAlphaNumeric1 && std::iswspace(char1);
  bool whitespace2 = nonAlphaNumeric2 && std::iswspace(char2);
  bool lineBreak1 = whitespace1 && std::iswcntrl(char1);
  bool lineBreak2 = whitespace2 && std::iswcntrl(char2);
  bool blankLine1 = lineBreak1 && std::regex_search(one, BLANKLINEEND);
  bool blankLine2 = lineBreak2 && std::regex_search(two, BLANKLINESTART);

  if (blankLine1 || blankLine2) {
    // Five points for blank lines.
    return 5;
  } else if (lineBreak1 || lineBreak2) {
    // Four points for line breaks.
    return 4;
  } else if (nonAlphaNumeric1 && !whitespace1 && whitespace2) {
    // Three points for end of sentences.
    return 3;
  } else if (whitespace1 || whitespace2) {
    // Two points for whitespace.
    return 2;
  } else if (nonAlphaNumeric1 || nonAlphaNumeric2) {
    // One point for non-alphanumeric.
    return 1;
  }
  return 0;
}


// Define some regex patterns for matching boundaries.
std::wregex diff_match_patch::BLANKLINEEND(L"\\n\\r?\\n$");
std::wregex diff_match_patch::BLANKLINESTART(L"^\\r?\\n\\r?\\n");


void diff_match_patch::diff_cleanupEfficiency(std::deque<Diff> &diffs) {
  if (diffs.empty()) {
    return;
  }
  bool changes = false;
  std::stack<Diff> equalities;  // Stack of equalities.
  std::wstring lastequality;  // Always equal to equalities.lastElement().text
  // Is there an insertion operation before the last equality.
  bool pre_ins = false;
  // Is there a deletion operation before the last equality.
  bool pre_del = false;
  // Is there an insertion operation after the last equality.
  bool post_ins = false;
  // Is there a deletion operation after the last equality.
  bool post_del = false;

  auto thisDiff = diffs.begin();
  Diff safeDiff = *thisDiff;

  while (thisDiff != diffs.end()) {
    if (thisDiff->operation == Diff::Operation::Equal) {
      // Equality found.
      if (thisDiff->text.length() < Diff_EditCost && (post_ins || post_del)) {
        // Candidate found.
        equalities.push(*thisDiff);
        pre_ins = post_ins;
        pre_del = post_del;
        lastequality = thisDiff->text;
      } else {
        // Not a candidate, and can never become one.
        std::stack<Diff>().swap(equalities);
        lastequality = std::wstring();
        safeDiff = *thisDiff;
      }
      post_ins = post_del = false;
    } else {
      // An insertion or deletion.
      if (thisDiff->operation == Diff::Operation::Delete) {
        post_del = true;
      } else {
        post_ins = true;
      }
      /*
      * Five types to be split:
      * <ins>A</ins><del>B</del>XY<ins>C</ins><del>D</del>
      * <ins>A</ins>X<ins>C</ins><del>D</del>
      * <ins>A</ins><del>B</del>X<ins>C</ins>
      * <ins>A</del>X<ins>C</ins><del>D</del>
      * <ins>A</ins><del>B</del>X<del>C</del>
      */
      if (!lastequality.empty()
          && ((pre_ins && pre_del && post_ins && post_del)
          || ((lastequality.length() < Diff_EditCost / 2)
          && ((pre_ins ? 1 : 0) + (pre_del ? 1 : 0)
          + (post_ins ? 1 : 0) + (post_del ? 1 : 0)) == 3))) {
        // printf("Splitting: '%s'\n", qPrintable(lastequality));
        // Walk back to offending equality.
        while (*thisDiff != equalities.top())
          thisDiff--;

        // Replace equality with a delete.
        *thisDiff = Diff(Diff::Operation::Delete, lastequality);
        thisDiff++;
        // Insert a corresponding an insert.
        {
          int thisDiffIndex = thisDiff - diffs.begin();
          diffs.insert(thisDiff, Diff(Diff::Operation::Insert, lastequality));
          thisDiff = diffs.begin() + thisDiffIndex - 1;
        }

        equalities.pop();  // Throw away the equality we just deleted.
        lastequality = std::wstring();
        if (pre_ins && pre_del) {
          // No changes made which could affect previous entry, keep going.
          post_ins = post_del = true;
          std::stack<Diff>().swap(equalities);
          safeDiff = *thisDiff;
        } else {
          if (!equalities.empty()) {
            // Throw away the previous equality (it needs to be reevaluated).
            equalities.pop();
          }
          if (equalities.empty()) {
            // There are no previous questionable equalities,
            // walk back to the last known safe diff.
            while (*thisDiff != safeDiff)
              thisDiff--;
          } else {
            // There is an equality we can fall back to.
            while (*thisDiff != equalities.top())
              thisDiff--;
          }
          post_ins = post_del = false;
        }

        changes = true;
        continue;
      }
    }
    thisDiff++;
  }

  if (changes) {
    diff_cleanupMerge(diffs);
  }
}


void diff_match_patch::diff_cleanupMerge(std::deque<Diff> &diffs) {
  diffs.push_back(Diff(Diff::Operation::Equal, L""));  // Add a dummy entry at the end.
  int count_delete = 0;
  int count_insert = 0;
  std::wstring text_delete = L"";
  std::wstring text_insert = L"";
  auto thisDiff = diffs.begin();
  auto prevEqual = diffs.end();
  int commonlength;
  while (thisDiff != diffs.end()) {
    switch (thisDiff->operation) {
      case Diff::Operation::Insert:
        count_insert++;
        text_insert += thisDiff->text;
        prevEqual = diffs.end();
        break;
      case Diff::Operation::Delete:
        count_delete++;
        text_delete += thisDiff->text;
        prevEqual = diffs.end();
        break;
      case Diff::Operation::Equal:
        if (count_delete + count_insert > 1) {
          bool both_types = count_delete != 0 && count_insert != 0;
          // Delete the offending records.
          {
            auto deleteBegin = std::prev(thisDiff, count_delete + count_insert);
            int thisDiffIndex = deleteBegin - diffs.begin();
            diffs.erase(deleteBegin, thisDiff);
            thisDiff = diffs.begin() + thisDiffIndex;
          }
          if (both_types) {
            // Factor out any common prefixies.
            commonlength = diff_commonPrefix(text_insert, text_delete);
            if (commonlength != 0) {
              if (thisDiff != diffs.begin()) {
                thisDiff--;
                if (thisDiff->operation != Diff::Operation::Equal) {
                  throw "Previous diff should have been an equality.";
                }
                thisDiff->text += text_insert.substr(0, commonlength);
                thisDiff++;
              } else {
                diffs.insert(thisDiff, Diff(Diff::Operation::Equal, text_insert.substr(0, commonlength)));
                thisDiff = diffs.begin() + 1;
              }
              text_insert = text_insert.substr(commonlength);
              text_delete = text_delete.substr(commonlength);
            }
            // Factor out any common suffixies.
            commonlength = diff_commonSuffix(text_insert, text_delete);
            if (commonlength != 0) {
              thisDiff->text = text_insert.substr(text_insert.length() - commonlength) + thisDiff->text;
              text_insert = text_insert.substr(0, text_insert.length() - commonlength);
              text_delete = text_delete.substr(0, text_delete.length() - commonlength);
            }
          }
          // Insert the merged records.
          if (!text_delete.empty()) {
            int thisDiffIndex = thisDiff - diffs.begin();
            diffs.insert(thisDiff, Diff(Diff::Operation::Delete, text_delete));
            thisDiff = diffs.begin() + thisDiffIndex + 1;
          }
          if (!text_insert.empty()) {
            int thisDiffIndex = thisDiff - diffs.begin();
            diffs.insert(thisDiff, Diff(Diff::Operation::Insert, text_insert));
            thisDiff = diffs.begin() + thisDiffIndex + 1;
          }
        } else if (prevEqual != diffs.end()) {
          // Merge this equality with the previous one.
          prevEqual->text += thisDiff->text;
          int thisDiffIndex = thisDiff - diffs.begin();
          diffs.erase(thisDiff);
          thisDiff = diffs.begin() + thisDiffIndex - 1;
        }
        count_insert = 0;
        count_delete = 0;
        text_delete = L"";
        text_insert = L"";
        prevEqual = thisDiff;
        break;
      }
      thisDiff++;
  }
  if (diffs.back().text.empty()) {
    diffs.pop_back();  // Remove the dummy entry at the end.
  }

  /*
  * Second pass: look for single edits surrounded on both sides by equalities
  * which can be shifted sideways to eliminate an equality.
  * e.g: A<ins>BA</ins>C -> <ins>AB</ins>AC
  */
  bool changes = false;
  if (diffs.size() >= 3)
  {
    // Create a new iterator at the start.
    // (As opposed to walking the current one back.)
    thisDiff = diffs.begin() + 1;

    // Intentionally ignore the first and last element (don't need checking).
    while (thisDiff != diffs.end() && thisDiff + 1 != diffs.end())
    {
      auto prevDiff = thisDiff - 1;
      auto nextDiff = thisDiff + 1;
      if (prevDiff->operation == Diff::Operation::Equal &&
          nextDiff->operation == Diff::Operation::Equal)
      {
        // This is a single edit surrounded by equalities.
        if (thisDiff->text.length() > prevDiff->text.length() &&
          thisDiff->text.compare(thisDiff->text.length() - prevDiff->text.length(), prevDiff->text.length(), prevDiff->text) == 0)
        {
          // Shift the edit over the previous equality.
          thisDiff->text = prevDiff->text + thisDiff->text.substr(0, thisDiff->text.length() - prevDiff->text.length());
          nextDiff->text = prevDiff->text + nextDiff->text;
          int thisDiffIndex = thisDiff - diffs.begin();
          diffs.erase(prevDiff);
          thisDiff = diffs.begin() + thisDiffIndex;
          changes = true;
        }
        else if (thisDiff->text.compare(0, nextDiff->text.length(), nextDiff->text) == 0)
        {
          // Shift the edit over the next equality.
          prevDiff->text += nextDiff->text;
          thisDiff->text = thisDiff->text.substr(nextDiff->text.length()) + nextDiff->text;
          int thisDiffIndex = thisDiff - diffs.begin();
          diffs.erase(nextDiff);
          thisDiff = diffs.begin() + thisDiffIndex;
          changes = true;
        }
      }
      if (thisDiff != diffs.end())
        thisDiff++;
    }
  }
  // If shifts were made, the diff needs reordering and another shift sweep.
  if (changes) {
    diff_cleanupMerge(diffs);
  }
}


int diff_match_patch::diff_xIndex(const std::deque<Diff> &diffs, int loc) {
  int chars1 = 0;
  int chars2 = 0;
  int last_chars1 = 0;
  int last_chars2 = 0;
  Diff lastDiff;
  for (const Diff& aDiff : diffs) {
    if (aDiff.operation != Diff::Operation::Insert) {
      // Equality or deletion.
      chars1 += aDiff.text.length();
    }
    if (aDiff.operation != Diff::Operation::Delete) {
      // Equality or insertion.
      chars2 += aDiff.text.length();
    }
    if (chars1 > loc) {
      // Overshot the location.
      lastDiff = aDiff;
      break;
    }
    last_chars1 = chars1;
    last_chars2 = chars2;
  }
  if (lastDiff.operation == Diff::Operation::Delete) {
    // The location was deleted.
    return last_chars2;
  }
  // Add the remaining character length.
  return last_chars2 + (loc - last_chars1);
}


std::wstring diff_match_patch::diff_prettyHtml(const std::deque<Diff> &diffs) {
  std::wstring html;
  std::wstring text;
  for (const Diff& aDiff : diffs) {
    text = aDiff.text;
    text = std::regex_replace(text, std::wregex(L"&"), L"&amp;");
    text = std::regex_replace(text, std::wregex(L"<"), L"&lt;");
    text = std::regex_replace(text, std::wregex(L">"), L"&gt;");
    text = std::regex_replace(text, std::wregex(L"\\n"), L"&para;<br>");
    switch (aDiff.operation) {
      case Diff::Operation::Insert:
        html += std::wstring(L"<ins style=\"background:#e6ffe6;\">") + text
            + std::wstring(L"</ins>");
        break;
      case Diff::Operation::Delete:
        html += std::wstring(L"<del style=\"background:#ffe6e6;\">") + text
            + std::wstring(L"</del>");
        break;
      case Diff::Operation::Equal:
        html += std::wstring(L"<span>") + text + std::wstring(L"</span>");
        break;
    }
  }
  return html;
}


std::wstring diff_match_patch::diff_text1(const std::deque<Diff> &diffs) {
  std::wstring text;
  for (const Diff& aDiff : diffs) {
    if (aDiff.operation != Diff::Operation::Insert) {
      text += aDiff.text;
    }
  }
  return text;
}


std::wstring diff_match_patch::diff_text2(const std::deque<Diff> &diffs) {
  std::wstring text;
  for (const Diff& aDiff : diffs) {
    if (aDiff.operation != Diff::Operation::Delete) {
      text += aDiff.text;
    }
  }
  return text;
}


int diff_match_patch::diff_levenshtein(const std::deque<Diff> &diffs) {
  int levenshtein = 0;
  int insertions = 0;
  int deletions = 0;
  for (const Diff& aDiff : diffs) {
    switch (aDiff.operation) {
      case Diff::Operation::Insert:
        insertions += aDiff.text.length();
        break;
      case Diff::Operation::Delete:
        deletions += aDiff.text.length();
        break;
      case Diff::Operation::Equal:
        // A deletion and an insertion is one substitution.
        levenshtein += std::max(insertions, deletions);
        insertions = 0;
        deletions = 0;
        break;
    }
  }
  levenshtein += std::max(insertions, deletions);
  return levenshtein;
}


std::wstring diff_match_patch::diff_toDelta(const std::deque<Diff> &diffs) {
  std::wstring text;
  for (const Diff& aDiff : diffs) {
    switch (aDiff.operation) {
      case Diff::Operation::Insert: {
        std::wstring encoded = std::wstring(toPercentEncoding(aDiff.text, " !~*'();/?:@&=+$,#"));
        text += std::wstring(L"+") + encoded + std::wstring(L"\t");
        break;
      }
      case Diff::Operation::Delete:
        text += std::wstring(L"-") + std::to_wstring(aDiff.text.length())
            + std::wstring(L"\t");
        break;
      case Diff::Operation::Equal:
        text += std::wstring(L"=") + std::to_wstring(aDiff.text.length())
            + std::wstring(L"\t");
        break;
    }
  }
  if (!text.empty()) {
    // Strip off trailing tab character.
    text = text.substr(0, text.length() - 1);
  }
  return text;
}


std::deque<Diff> diff_match_patch::diff_fromDelta(const std::wstring &text1,
                                             const std::wstring &delta) {
  std::deque<Diff> diffs;
  int pointer = 0;  // Cursor in text1
  std::deque<std::wstring> tokens;
  {
    std::wstring line;
    std::wstringstream ss(delta);
    while (std::getline(ss, line, L'\t'))
      if (!line.empty())
        tokens.push_back(line);
  }
  for (std::wstring& token : tokens) {
    if (token.empty()) {
      // Blank tokens are ok (from a trailing \t).
      continue;
    }
    // Each token begins with a one character parameter which specifies the
    // operation of this token (delete, insert, equality).
    std::wstring param = token.substr(1);
    switch (token[0]) {
      case L'+':
        param = fromPercentEncoding(param);
        diffs.push_back(Diff(Diff::Operation::Insert, param));
        break;
      case L'-':
        // Fall through.
      case L'=': {
        int n;
        n = std::stoi(param);
        if (n < 0) {
          throw static_cast<std::wstringstream &>(std::wstringstream(L"Negative number in diff_fromDelta: ").flush() << param).str();
        }
        std::wstring text;
        text = text1.substr(pointer, n);
        pointer += n;
        if (token[0] == wchar_t('=')) {
          diffs.push_back(Diff(Diff::Operation::Equal, text));
        } else {
          diffs.push_back(Diff(Diff::Operation::Delete, text));
        }
        break;
      }
      default:
        throw static_cast<std::wstringstream &>(std::wstringstream(L"Invalid diff operation in diff_fromDelta: ").flush() << token[0]).str();
    }
  }
  if (pointer != text1.length()) {
    throw static_cast<std::wstringstream &>(std::wstringstream(L"Delta length (").flush() << pointer << L") smaller than source text length (" << text1.length() << L")").str();
  }
  return diffs;
}


  //  MATCH FUNCTIONS


int diff_match_patch::match_main(const std::wstring &text, const std::wstring &pattern,
                                 int loc)
{
  loc = std::max((size_t)0, std::min((size_t)loc, text.length()));
  if (text == pattern) {
    // Shortcut (potentially not guaranteed by the algorithm)
    return 0;
  } else if (text.empty()) {
    // Nothing to match.
    return -1;
  } else if (loc + pattern.length() <= text.length()
      && text.substr(loc, pattern.length()) == pattern) {
    // Perfect match at the perfect spot!  (Includes case of null pattern)
    return loc;
  } else {
    // Do a fuzzy compare.
    return match_bitap(text, pattern, loc);
  }
}


int diff_match_patch::match_bitap(const std::wstring &text, const std::wstring &pattern,
                                  int loc) {
  if (!(Match_MaxBits == 0 || pattern.length() <= Match_MaxBits)) {
    throw "Pattern too long for this application.";
  }

  // Initialise the alphabet.
  std::map<wchar_t, int> s = match_alphabet(pattern);

  // Highest score beyond which we give up.
  double score_threshold = Match_Threshold;
  // Is there a nearby exact match? (speedup)
  int best_loc = text.find(pattern, loc);
  if (best_loc != -1) {
    score_threshold = std::min(match_bitapScore(0, best_loc, loc, pattern),
        score_threshold);
    // What about in the other direction? (speedup)
    best_loc = text.rfind(pattern, loc + pattern.length());
    if (best_loc != -1) {
      score_threshold = std::min(match_bitapScore(0, best_loc, loc, pattern),
          score_threshold);
    }
  }

  // Initialise the bit arrays.
  int matchmask = 1 << (pattern.length() - 1);
  best_loc = -1;

  int bin_min, bin_mid;
  int bin_max = pattern.length() + text.length();
  int *rd;
  int *last_rd = NULL;
  for (int d = 0; d < pattern.length(); d++) {
    // Scan for the best match; each iteration allows for one more error.
    // Run a binary search to determine how far from 'loc' we can stray at
    // this error level.
    bin_min = 0;
    bin_mid = bin_max;
    while (bin_min < bin_mid) {
      if (match_bitapScore(d, loc + bin_mid, loc, pattern)
          <= score_threshold) {
        bin_min = bin_mid;
      } else {
        bin_max = bin_mid;
      }
      bin_mid = (bin_max - bin_min) / 2 + bin_min;
    }
    // Use the result from this iteration as the maximum for the next.
    bin_max = bin_mid;
    int start = std::max(1, loc - bin_mid + 1);
    int finish = std::min(size_t(loc + bin_mid), text.length()) + pattern.length();

    rd = new int[finish + 2];
    rd[finish + 1] = (1 << d) - 1;
    for (int j = finish; j >= start; j--) {
      int charMatch = 0;
      if (text.length() > j - 1) {
        if (s.find(text[j - 1]) != s.end())
          charMatch = s[text[j - 1]];
      }
      if (d == 0) {
        // First pass: exact match.
        rd[j] = ((rd[j + 1] << 1) | 1) & charMatch;
      } else {
        // Subsequent passes: fuzzy match.
        rd[j] = ((rd[j + 1] << 1) | 1) & charMatch
            | (((last_rd[j + 1] | last_rd[j]) << 1) | 1)
            | last_rd[j + 1];
      }
      if ((rd[j] & matchmask) != 0) {
        double score = match_bitapScore(d, j - 1, loc, pattern);
        // This match will almost certainly be better than any existing
        // match.  But check anyway.
        if (score <= score_threshold) {
          // Told you so.
          score_threshold = score;
          best_loc = j - 1;
          if (best_loc > loc) {
            // When passing loc, don't exceed our current distance from loc.
            start = std::max(1, 2 * loc - best_loc);
          } else {
            // Already passed loc, downhill from here on in.
            break;
          }
        }
      }
    }
    if (match_bitapScore(d + 1, loc, loc, pattern) > score_threshold) {
      // No hope for a (better) match at greater error levels.
      break;
    }
    delete [] last_rd;
    last_rd = rd;
  }
  delete [] last_rd;
  delete [] rd;
  return best_loc;
}


double diff_match_patch::match_bitapScore(int e, int x, int loc,
                                          const std::wstring &pattern) {
  const float accuracy = static_cast<float> (e) / pattern.length();
  const int proximity = std::abs(loc - x);
  if (Match_Distance == 0) {
    // Dodge divide by zero error.
    return proximity == 0 ? accuracy : 1.0;
  }
  return accuracy + (proximity / static_cast<float> (Match_Distance));
}


std::map<wchar_t, int> diff_match_patch::match_alphabet(const std::wstring &pattern) {
  std::map<wchar_t, int> s;
  int i;
  for (i = 0; i < pattern.length(); i++) {
    wchar_t c = pattern[i];
    s.emplace(c, 0);
  }
  for (i = 0; i < pattern.length(); i++) {
    wchar_t c = pattern[i];
    s[c] |= (1 << (pattern.length() - i - 1));
  }
  return s;
}


//  PATCH FUNCTIONS


void diff_match_patch::patch_addContext(Patch &patch, const std::wstring &text) {
  if (text.empty()) {
    return;
  }
  std::wstring pattern = text.substr(patch.start2, patch.length1);
  int padding = 0;

  // Look for the first and last matches of pattern in text.  If two different
  // matches are found, increase the pattern length.
  while (text.find(pattern) != text.rfind(pattern)
      && pattern.length() < Match_MaxBits - Patch_Margin - Patch_Margin) {
    padding += Patch_Margin;
    pattern = text.substr(std::max(0, patch.start2 - padding),
        std::min(text.length(), (size_t)patch.start2 + patch.length1 + padding)
        - std::max(0, patch.start2 - padding));
  }
  // Add one chunk for good luck.
  padding += Patch_Margin;

  // Add the prefix.
  std::wstring prefix = text.substr(std::max(0, patch.start2 - padding),
      patch.start2 - std::max(0, patch.start2 - padding));
  if (!prefix.empty()) {
    patch.diffs.push_front(Diff(Diff::Operation::Equal, prefix));
  }
  // Add the suffix.
  std::wstring suffix = text.substr(patch.start2 + patch.length1,
      std::min(text.length(), (size_t)patch.start2 + patch.length1 + padding)
      - (patch.start2 + patch.length1));
  if (!suffix.empty()) {
    patch.diffs.push_back(Diff(Diff::Operation::Equal, suffix));
  }

  // Roll back the start points.
  patch.start1 -= prefix.length();
  patch.start2 -= prefix.length();
  // Extend the lengths.
  patch.length1 += prefix.length() + suffix.length();
  patch.length2 += prefix.length() + suffix.length();
}


std::deque<Patch> diff_match_patch::patch_make(const std::wstring &text1,
                                          const std::wstring &text2)
{
  // No diffs provided, compute our own.
  std::deque<Diff> diffs = diff_main(text1, text2, true);
  if (diffs.size() > 2) {
    diff_cleanupSemantic(diffs);
    diff_cleanupEfficiency(diffs);
  }

  return patch_make(text1, diffs);
}


std::deque<Patch> diff_match_patch::patch_make(const std::deque<Diff> &diffs) {
  // No origin string provided, compute our own.
  const std::wstring text1 = diff_text1(diffs);
  return patch_make(text1, diffs);
}


std::deque<Patch> diff_match_patch::patch_make(const std::wstring &text1,
                                          const std::wstring &text2,
                                          const std::deque<Diff> &diffs) {
  // text2 is entirely unused.
  (void)text2;
  return patch_make(text1, diffs);
}


std::deque<Patch> diff_match_patch::patch_make(const std::wstring &text1,
                                          const std::deque<Diff> &diffs)
{
  std::deque<Patch> patches;
  if (diffs.empty()) {
    return patches;  // Get rid of the null case.
  }
  Patch patch;
  int char_count1 = 0;  // Number of characters into the text1 string.
  int char_count2 = 0;  // Number of characters into the text2 string.
  // Start with text1 (prepatch_text) and apply the diffs until we arrive at
  // text2 (postpatch_text).  We recreate the patches one by one to determine
  // context info.
  std::wstring prepatch_text = text1;
  std::wstring postpatch_text = text1;
  for (const Diff& aDiff : diffs) {
    if (patch.diffs.empty() && aDiff.operation != Diff::Operation::Equal) {
      // A new patch starts here.
      patch.start1 = char_count1;
      patch.start2 = char_count2;
    }

    switch (aDiff.operation) {
      case Diff::Operation::Insert:
        patch.diffs.push_back(aDiff);
        patch.length2 += aDiff.text.length();
        postpatch_text = postpatch_text.substr(0, char_count2)
            + aDiff.text + postpatch_text.substr(char_count2);
        break;
      case Diff::Operation::Delete:
        patch.length1 += aDiff.text.length();
        patch.diffs.push_back(aDiff);
        postpatch_text = postpatch_text.substr(0, char_count2) + postpatch_text.substr(char_count2 + aDiff.text.length());
        break;
      case Diff::Operation::Equal:
        if (aDiff.text.length() <= 2 * Patch_Margin
            && !patch.diffs.empty() && !(aDiff == diffs.back())) {
          // Small equality inside a patch.
          patch.diffs.push_back(aDiff);
          patch.length1 += aDiff.text.length();
          patch.length2 += aDiff.text.length();
        }

        if (aDiff.text.length() >= 2 * Patch_Margin) {
          // Time for a new patch.
          if (!patch.diffs.empty()) {
            patch_addContext(patch, prepatch_text);
            patches.push_back(patch);
            patch = Patch();
            // Unlike Unidiff, our patch lists have a rolling context.
            // http://code.google.com/p/google-diff-match-patch/wiki/Unidiff
            // Update prepatch text & pos to reflect the application of the
            // just completed patch.
            prepatch_text = postpatch_text;
            char_count1 = char_count2;
          }
        }
        break;
    }

    // Update the current character count.
    if (aDiff.operation != Diff::Operation::Insert) {
      char_count1 += aDiff.text.length();
    }
    if (aDiff.operation != Diff::Operation::Delete) {
      char_count2 += aDiff.text.length();
    }
  }
  // Pick up the leftover patch if not empty.
  if (!patch.diffs.empty()) {
    patch_addContext(patch, prepatch_text);
    patches.push_back(patch);
  }

  return patches;
}


std::deque<Patch> diff_match_patch::patch_deepCopy(std::deque<Patch> &patches) {
  std::deque<Patch> patchesCopy;
  for (const Patch& aPatch : patches) {
    Patch patchCopy = Patch();
    for (const Diff& aDiff : aPatch.diffs) {
      Diff diffCopy = Diff(aDiff.operation, aDiff.text);
      patchCopy.diffs.push_back(diffCopy);
    }
    patchCopy.start1 = aPatch.start1;
    patchCopy.start2 = aPatch.start2;
    patchCopy.length1 = aPatch.length1;
    patchCopy.length2 = aPatch.length2;
    patchesCopy.push_back(patchCopy);
  }
  return patchesCopy;
}


std::pair<std::wstring, std::deque<bool>> diff_match_patch::patch_apply(
    std::deque<Patch> &patches, const std::wstring &sourceText) {
  std::wstring text = sourceText;  // Copy to preserve original.
  if (patches.empty()) {
    return std::make_pair(text, std::deque<bool>(0));
  }

  // Deep copy the patches so that no changes are made to originals.
  std::deque<Patch> patchesCopy = patch_deepCopy(patches);

  std::wstring nullPadding = patch_addPadding(patchesCopy);
  text = nullPadding + text + nullPadding;
  patch_splitMax(patchesCopy);

  int x = 0;
  // delta keeps track of the offset between the expected and actual location
  // of the previous patch.  If there are patches expected at positions 10 and
  // 20, but the first patch was found at 12, delta is 2 and the second patch
  // has an effective expected position of 22.
  int delta = 0;
  std::deque<bool> results(patchesCopy.size());
  for (Patch& aPatch : patchesCopy) {
    int expected_loc = aPatch.start2 + delta;
    std::wstring text1 = diff_text1(aPatch.diffs);
    int start_loc;
    int end_loc = -1;
    if (text1.length() > Match_MaxBits) {
      // patch_splitMax will only provide an oversized pattern in the case of
      // a monster delete.
      start_loc = match_main(text, text1.substr(0, Match_MaxBits), expected_loc);
      if (start_loc != -1) {
        end_loc = match_main(text, text1.substr(text1.length() - Match_MaxBits),
            expected_loc + text1.length() - Match_MaxBits);
        if (end_loc == -1 || start_loc >= end_loc) {
          // Can't find valid trailing context.  Drop this patch.
          start_loc = -1;
        }
      }
    } else {
      start_loc = match_main(text, text1, expected_loc);
    }
    if (start_loc == -1) {
      // No match found.  :(
      results[x] = false;
      // Subtract the delta for this failed patch from subsequent patches.
      delta -= aPatch.length2 - aPatch.length1;
    } else {
      // Found a match.  :)
      results[x] = true;
      delta = start_loc - expected_loc;
      std::wstring text2;
      if (end_loc == -1) {
        text2 = text.substr(start_loc, text1.length());
      } else {
        text2 = text.substr(start_loc, end_loc + Match_MaxBits - start_loc);
      }
      if (text1 == text2) {
        // Perfect match, just shove the replacement text in.
        text = text.substr(0, start_loc) + diff_text2(aPatch.diffs)
            + text.substr(start_loc + text1.length());
      } else {
        // Imperfect match.  Run a diff to get a framework of equivalent
        // indices.
        std::deque<Diff> diffs = diff_main(text1, text2, false);
        if (text1.length() > Match_MaxBits
            && diff_levenshtein(diffs) / static_cast<float> (text1.length())
            > Patch_DeleteThreshold) {
          // The end points match, but the content is unacceptably bad.
          results[x] = false;
        } else {
          diff_cleanupSemanticLossless(diffs);
          int index1 = 0;
          for (const Diff& aDiff : aPatch.diffs) {
            if (aDiff.operation != Diff::Operation::Equal) {
              int index2 = diff_xIndex(diffs, index1);
              if (aDiff.operation == Diff::Operation::Insert) {
                // Insertion
                text = text.substr(0, start_loc + index2) + aDiff.text
                    + text.substr(start_loc + index2);
              } else if (aDiff.operation == Diff::Operation::Delete) {
                // Deletion
                text = text.substr(0, start_loc + index2)
                    + text.substr(start_loc + diff_xIndex(diffs, index1 + aDiff.text.length()));
              }
            }
            if (aDiff.operation != Diff::Operation::Delete) {
              index1 += aDiff.text.length();
            }
          }
        }
      }
    }
    x++;
  }
  // Strip the padding off.
  text = text.substr(nullPadding.length(), text.length() - 2 * nullPadding.length());
  return std::make_pair(text, results);
}


std::wstring diff_match_patch::patch_addPadding(std::deque<Patch> &patches) {
  short paddingLength = Patch_Margin;
  std::wstring nullPadding = L"";
  for (short x = 1; x <= paddingLength; x++) {
    nullPadding += wchar_t((unsigned short)x);
  }

  // Bump all the patches forward.
  for (Patch& aPatch : patches)
  {
    aPatch.start1 += paddingLength;
    aPatch.start2 += paddingLength;
  }

  // Add some padding on start of first diff.
  Patch &firstPatch = patches.front();
  std::deque<Diff> &firstPatchDiffs = firstPatch.diffs;
  if (firstPatchDiffs.empty() || firstPatchDiffs.front().operation != Diff::Operation::Equal) {
    // Add nullPadding equality.
    firstPatchDiffs.push_front(Diff(Diff::Operation::Equal, nullPadding));
    firstPatch.start1 -= paddingLength;  // Should be 0.
    firstPatch.start2 -= paddingLength;  // Should be 0.
    firstPatch.length1 += paddingLength;
    firstPatch.length2 += paddingLength;
  } else if (paddingLength > firstPatchDiffs.front().text.length()) {
    // Grow first equality.
    Diff &firstDiff = firstPatchDiffs.front();
    int extraLength = paddingLength - firstDiff.text.length();
    firstDiff.text = nullPadding.substr(firstDiff.text.length(), paddingLength - firstDiff.text.length()) + firstDiff.text;
    firstPatch.start1 -= extraLength;
    firstPatch.start2 -= extraLength;
    firstPatch.length1 += extraLength;
    firstPatch.length2 += extraLength;
  }

  // Add some padding on end of last diff.
  Patch &lastPatch = patches.front();
  std::deque<Diff> &lastPatchDiffs = lastPatch.diffs;
  if (lastPatchDiffs.empty() || lastPatchDiffs.back().operation != Diff::Operation::Equal) {
    // Add nullPadding equality.
    lastPatchDiffs.push_back(Diff(Diff::Operation::Equal, nullPadding));
    lastPatch.length1 += paddingLength;
    lastPatch.length2 += paddingLength;
  } else if (paddingLength > lastPatchDiffs.back().text.length()) {
    // Grow last equality.
    Diff &lastDiff = lastPatchDiffs.back();
    int extraLength = paddingLength - lastDiff.text.length();
    lastDiff.text += nullPadding.substr(0, extraLength);
    lastPatch.length1 += extraLength;
    lastPatch.length2 += extraLength;
  }

  return nullPadding;
}


void diff_match_patch::patch_splitMax(std::deque<Patch> &patches) {
  short patch_size = Match_MaxBits;
  std::wstring precontext, postcontext;
  Patch patch;
  int start1, start2;
  bool empty;
  Diff::Operation diff_type;
  std::wstring diff_text;
  auto pointer = patches.begin();

  while (pointer != patches.end() && !pointer->empty()) {
    Patch bigpatch = *pointer;
    if (bigpatch.length1 <= patch_size) {
      pointer++;
      continue;
    }
    // Remove the big old patch.
    {
      int pointerIndex = pointer - patches.begin();
      patches.erase(pointer);
      pointer = patches.begin() + pointerIndex;
    }
    start1 = bigpatch.start1;
    start2 = bigpatch.start2;
    precontext = L"";
    while (!bigpatch.diffs.empty()) {
      // Create one of several smaller patches.
      patch = Patch();
      empty = true;
      patch.start1 = start1 - precontext.length();
      patch.start2 = start2 - precontext.length();
      if (!precontext.empty()) {
        patch.length1 = patch.length2 = precontext.length();
        patch.diffs.push_back(Diff(Diff::Operation::Equal, precontext));
      }
      while (!bigpatch.diffs.empty()
          && patch.length1 < patch_size - Patch_Margin) {
        diff_type = bigpatch.diffs.front().operation;
        diff_text = bigpatch.diffs.front().text;
        if (diff_type == Diff::Operation::Insert) {
          // Insertions are harmless.
          patch.length2 += diff_text.length();
          start2 += diff_text.length();
          patch.diffs.push_back(bigpatch.diffs.front());
          bigpatch.diffs.pop_front();
          empty = false;
        } else if (diff_type == Diff::Operation::Delete && patch.diffs.size() == 1
            && patch.diffs.front().operation == Diff::Operation::Equal
            && diff_text.length() > 2 * patch_size) {
          // This is a large deletion.  Let it pass in one chunk.
          patch.length1 += diff_text.length();
          start1 += diff_text.length();
          empty = false;
          patch.diffs.push_back(Diff(diff_type, diff_text));
          bigpatch.diffs.pop_front();
        } else {
          // Deletion or equality.  Only take as much as we can stomach.
          diff_text = diff_text.substr(0, std::min(diff_text.length(), (size_t)patch_size - patch.length1 - Patch_Margin));
          patch.length1 += diff_text.length();
          start1 += diff_text.length();
          if (diff_type == Diff::Operation::Equal) {
            patch.length2 += diff_text.length();
            start2 += diff_text.length();
          } else {
            empty = false;
          }
          patch.diffs.push_back(Diff(diff_type, diff_text));
          if (diff_text == bigpatch.diffs.front().text) {
            bigpatch.diffs.pop_front();
          } else {
            bigpatch.diffs.front().text = bigpatch.diffs.front().text.substr(diff_text.length());
          }
        }
      }
      // Compute the head context for the next patch.
      precontext = diff_text2(patch.diffs);
      precontext = precontext.substr(std::min(precontext.length() - Patch_Margin, precontext.length()));
      // Append the end context for this patch.
      if (diff_text1(bigpatch.diffs).length() > Patch_Margin) {
        postcontext = diff_text1(bigpatch.diffs).substr(0, Patch_Margin);
      } else {
        postcontext = diff_text1(bigpatch.diffs);
      }
      if (!postcontext.empty()) {
        patch.length1 += postcontext.length();
        patch.length2 += postcontext.length();
        if (!patch.diffs.empty()
            && patch.diffs.back().operation == Diff::Operation::Equal) {
          patch.diffs.back().text += postcontext;
        } else {
          patch.diffs.push_back(Diff(Diff::Operation::Equal, postcontext));
        }
      }
      if (!empty) {
        int pointerIndex = pointer - patches.begin();
        patches.insert(pointer, patch);
        pointer = patches.begin() + pointerIndex + 1;
      }
    }
  }
}


std::wstring diff_match_patch::patch_toText(const std::deque<Patch> &patches) {
  std::wstring text;
  for (const Patch& aPatch : patches) {
    text.append(aPatch.toString());
  }
  return text;
}


std::deque<Patch> diff_match_patch::patch_fromText(const std::wstring &textline) {
  std::deque<Patch> patches;
  if (textline.empty()) {
    return patches;
  }
  std::deque<std::wstring> text;
  {
    std::wstring line;
    std::wstringstream ss(textline);
    while (std::getline(ss, line, L'\n'))
      if (!line.empty())
        text.push_back(line);
  }
  Patch patch;
  std::wregex patchHeader(L"^@@ -(\\d+),?(\\d*) \\+(\\d+),?(\\d*) @@$");
  wchar_t sign;
  std::wstring line;
  while (!text.empty()) {
    std::wsmatch captures;
    if (!std::regex_match(text.front(), captures, patchHeader)) {
      throw static_cast<std::wstringstream &>(std::wstringstream(L"Invalid patch string: ").flush() << text.front()).str();
    }

    patch = Patch();
    patch.start1 = std::stoi(captures[1]);
    if (captures[2].str().empty()) {
      patch.start1--;
      patch.length1 = 1;
    } else if (captures[2].str() == L"0") {
      patch.length1 = 0;
    } else {
      patch.start1--;
      patch.length1 = std::stoi(captures[2]);
    }

    patch.start2 =  std::stoi(captures[3]);
    if (captures[4].str().empty()) {
      patch.start2--;
      patch.length2 = 1;
    } else if (captures[4].str() == L"0") {
      patch.length2 = 0;
    } else {
      patch.start2--;
      patch.length2 =  std::stoi(captures[4]);
    }
    text.pop_front();

    while (!text.empty()) {
      if (text.front().empty()) {
        text.pop_front();
        continue;
      }
      sign = text.front()[0];
      line = text.front().substr(1);
      line = std::regex_replace(line, std::wregex(L"\\+"), L"%2B");  // decode would change all "+" to " "
      line = fromPercentEncoding(line);
      if (sign == L'-') {
        // Deletion.
        patch.diffs.push_back(Diff(Diff::Operation::Delete, line));
      } else if (sign == L'+') {
        // Insertion.
        patch.diffs.push_back(Diff(Diff::Operation::Insert, line));
      } else if (sign == L' ') {
        // Minor equality.
        patch.diffs.push_back(Diff(Diff::Operation::Equal, line));
      } else if (sign == L'@') {
        // Start of next patch.
        break;
      } else {
        // WTF?
        throw static_cast<std::wstringstream &>(std::wstringstream().flush() << L"Invalid patch mode '" << sign << L"' in: " << line).str();
        return std::deque<Patch>();
      }
      text.pop_front();
    }

    patches.push_back(patch);

  }
  return patches;
}
