#include <chrono>
#include <utility>
#include <iostream>

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <lean/lean.h>
#include <sstream>
#include <vector>

void trim_right(std::string& s) {
  s.erase(
    std::find_if(
      s.rbegin(),
      s.rend(),
      [](unsigned char ch) { return !std::isspace(ch); })
      .base(),
    s.end());
}
void trim_left(std::string& s) {
  s.erase(s.begin(), std::find_if(s.begin(), s.end(), [](unsigned char ch) {
            return !std::isspace(ch);
          }));
}
void trim(std::string& s) {
  trim_left(s);
  trim_right(s);
}

/**
 * Parses the given string into a header of import lines and the remaining body.
 */
/* TODO I think this function isn't working quite right. Need to test and make
 * sure the header/body is being split off properly. */
std::pair<std::string, std::string>
parse_header_and_body(const std::string &s) {
  auto header_lines = std::vector<std::string>{};
  auto body_lines = std::vector<std::string>{};
  auto ss = std::stringstream{s};

  size_t header_length = 0;
  for (std::string line; std::getline(ss, line, '\n');) {
    if (line.find("import") == 0) {
      // add 1 for the \n
      header_length += line.length() + 1;
    } else {
      // end of imports - we can split and return now
      break;
    }
  }
  // Substract 1 because we don't want the trailing newline.
  header_length--;
  std::string head = s.substr(0, header_length);
  std::string bod = s.substr(header_length, s.size() - header_length);
  trim(head);
  trim(bod);
  return std::make_pair(head, bod);
}

lean_obj_res lean_mk_array_of_strings(const std::vector<std::string> &vec) {
  lean_obj_res arr = lean_mk_empty_array_with_capacity(lean_box(vec.size()));
  for (auto &s : vec) {
    lean_obj_res str_i = lean_mk_string(s.c_str());
    lean_array_push(arr, str_i);
  }
  return arr;
}

void cleanup_lean_object(void *ptr) {
  lean_object *obj = static_cast<lean_object *>(ptr);
  if (obj)
    lean_dec(obj);
}
