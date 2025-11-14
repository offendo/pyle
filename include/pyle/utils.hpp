#include <lean/lean.h>
#include <string>
#include <utility>
#include <vector>

std::pair<std::string, std::string> parse_header_and_body(std::string s);
void trim_right(std::string &s);
void trim_left(std::string &s);
lean_obj_res lean_mk_array_of_strings(const std::vector<std::string> &vec);
