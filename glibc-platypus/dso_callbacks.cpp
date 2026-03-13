#pragma once
#include <unordered_set>
#include <string>

extern const std::unordered_set<std::string> dso_callbacks = {
};

extern const std::unordered_set<std::string> struct_names = {
    "CLIENT",
    "SVCXPRT",
    "XDR",
    "_IO_FILE",
    "_IO_FILE_plus",
    "_IO_codecvt",
    "_IO_cookie_file",
    "_IO_cookie_io_functions_t",
    "_IO_jump_t",
    "_IO_marker",
    "_IO_strfile_",
    "_IO_wide_data",
    "_Unwind_Exception",
    "__gconv_info",
    "__gconv_loaded_object",
    "__gconv_step",
    "__netgrent",
    "__printf_buffer_as_file",
    "__printf_buffer_obstack",
    "__printf_buffer_to_file",
    "__pthread_cleanup_frame",
    "__wprintf_buffer_as_file",
    "__wprintf_buffer_to_file",
    "_pthread_cleanup_buffer",

    "accepted_reply",
    "glob_t",
    "aiocb",
    "argp",
    "argp_fmtstream",
    "argp_state",
    "fork_handler",
    "gconv_fcts",
    "libc_ifunc_impl",
    "loaded_domain",
    "msort_param",
    "nss_action",
    "nss_files_per_file_data",
    "nss_module",
    "obstack",
    "posix_spawn_args",
    "proglst_",
    "pthread",
    "rec_strm",
    "requestlist",
    "rmtcallargs",
    "rmtcallres",
    "rpc_msg",
    "sigaction",
    "sigevent",
    "sigvec",
    "svc_callout",
    "svc_req",
    "xdr_discrim",
};

extern const std::unordered_set<std::string> global_names = {
};