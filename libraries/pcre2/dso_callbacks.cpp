#pragma once
#include <unordered_set>
#include <string>

static const std::unordered_set<std::string> dso_callbacks = {
};

std::unordered_set<std::string> struct_names = {
    "compile_block_8",
    "pcre2_memctl",
    "pcre2_real_code_8",
    "pcre2_real_compile_context_8",
    "pcre2_real_convert_context_8",
    "pcre2_real_general_context_8",
    "pcre2_real_jit_stack_8",
    "pcre2_real_match_context_8",
    "pcre2_real_match_data_8",
};

extern const std::unordered_set<std::string> global_names = {
};