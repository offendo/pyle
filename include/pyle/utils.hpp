#include <lean/lean.h>
#include <vector>

void trim_right(std::string &s);
void trim_left(std::string &s);
void trim(std::string &s);
std::pair<std::string, std::string> parse_header_and_body(const std::string &s);
lean_obj_res lean_mk_array_of_strings(const std::vector<std::string> &vec);
void cleanup_lean_object(void *ptr);
