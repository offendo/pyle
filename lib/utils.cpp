#include <algorithm>
#include <cctype>
#include <cstddef>
#include <lean/lean.h>
#include <string>
#include <utility>
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
 */
/* TODO I think this function isn't working quite right. Need to test and make
 * sure the header/body is being split off properly. */
std::pair<std::string, std::string>
parse_header_and_body(const std::string &s) {
  auto header_lines = std::vector<std::string>{};
  auto body_lines = std::vector<std::string>{};

  // First, check to see if there are any imports.
  // If not, the whole thing is the body so the header can be empty.
  size_t header_start = s.find("import");
  if (header_start == std::string::npos) {
    return std::make_pair("", s);
  }
  // start index of the last line
  size_t header_last_line = s.rfind("import");
  size_t header_end = s.find("\n", header_last_line);
  const std::string &header = s.substr(header_start, header_end - header_start);
  const std::string &body = s.substr(header_end, s.size() - header_end);
  return std::make_pair(header, body);
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
