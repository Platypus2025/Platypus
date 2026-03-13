#pragma once
#include <unordered_set>
#include <string>

static const std::unordered_set<std::string> dso_callbacks = {
};

std::unordered_set<std::string> struct_names = {
    "__rl_keyseq_context",
    "__rl_readstr_context",
    "__rl_search_context",
    "_keymap_entry",
    "readline_state",
    "sigaction",
};

extern const std::unordered_set<std::string> global_names = {
        "rl_linefunc",
};