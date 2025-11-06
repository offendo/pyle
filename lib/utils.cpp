#include "pyle/utils.hpp"
#include <vector>
#include <sstream>

/**
 * Parses the given string into a header of import lines and the remaining body.
 *
 * We read through `s` line by line, collecting all consecutive lines
 * that begin with the keyword "import". Once a line does not start with "import",
 * the header stops and the rest of the string is split to the body at that point.
 *
 * @param s The input string potentially containing import directives at the top.
 * @return A std::pair where
 *         - first:  a string of concatenated import lines (no trailing newline),
 *         - second: the remainder of the original string beginning at the first
 *                   non-import line.
 */
std::pair<std::string, std::string> parse_header_and_body(std::string s)
  {
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
  std::string head = s.substr(0, split_index - 1);
  std::string bod = s.substr(split_index, s.size());
  return std::make_pair(head, bod);
}
