#include "pyle/utils.hpp"
#include <cctype>
#include <iostream>
#include <sstream>
#include <vector>

void trim_right(std::string &s) {
  s.erase(
    std::find_if(
      s.rbegin(),
      s.rend(),
      [](unsigned char ch) { return !std::isspace(ch); })
      .base(),
    s.end());
}
void trim_left(std::string &s) {
  s.erase(s.begin(), std::find_if(s.begin(), s.end(), [](unsigned char ch) {
            return !std::isspace(ch);
          }));
}
void trim(std::string &s) {
  trim_left(s);
  trim_right(s);
}

/**
 * Parses the given string into a header of import lines and the remaining body.
 *
 * We read through `s` line by line, collecting all consecutive lines
 * that begin with the keyword "import". Once a line does not start with
 * "import", the header stops and the rest of the string is split to the body at
 * that point.
 *
 * @param s The input string potentially containing import directives at the
 * top.
 * @return A std::pair where
 *         - first:  a string of concatenated import lines (no trailing
 * newline),
 *         - second: the remainder of the original string beginning at the first
 *                   non-import line.
 */
std::pair<std::string, std::string> parse_header_and_body(std::string s) {
  auto header = std::vector<std::string>{};
  auto body = std::vector<std::string>{};
  auto ss = std::stringstream{s};

  size_t split_index = 0;
  for (std::string line; std::getline(ss, line, '\n');) {
    if (line.find("import") == 0) {
      // add 1 for the \n
      split_index += line.length() + 1;
    } else {
      // end of imports - we can split and return now
      break;
    }
  }
  // Substract 1 because we don't want the trailing newline.
  std::string head = s.substr(0, split_index);
  std::string bod = s.substr(split_index, s.size());
  trim(head);
  trim(bod);
  return std::make_pair(head, bod);
}
