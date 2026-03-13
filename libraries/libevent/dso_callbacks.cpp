#pragma once
#include <unordered_set>
#include <string>

std::string libstring = "even";

extern const std::unordered_set<std::string> dso_callbacks = {
};

extern const std::unordered_set<std::string> struct_names = {
    "bufferevent",
    "bufferevent_filtered",
    "bufferevent_ops",
    "bufferevent_private",
    "bufferevent_rate_limit_group",
    "evbuffer",
    "evbuffer_cb_entry",
    "evbuffer_chain",
    "evbuffer_file_segment",
    "evconnlistener",
    "evconnlistener_event",
    "evdns_base",
    "evdns_getaddrinfo_request",
    "evdns_request",
    "evdns_server_port",
    "event",
    "event_base",
    "event_callback",
    "evhttp",
    "evhttp_cb",
    "evhttp_connection",
    "evhttp_request",
    "evrpc",
    "evrpc_base",
    "evrpc_hook",
    "evrpc_hook_ctx",
    "evrpc_pool",
    "evrpc_req_generic",
    "evrpc_request_wrapper",
    "evthread_condition_callbacks",
    "evthread_lock_callbacks",
    "request",
    "sigaction",
};

extern const std::unordered_set<std::string> global_names = {
        "evthread_id_fn_",
        "fatal_fn",
        "log_fn",
        "mm_malloc_fn_",
        "mm_realloc_fn_",
        "mm_free_fn_",
        "evdns_getaddrinfo_impl",
        "evdns_getaddrinfo_cancel_impl",
        "evdns_log_fn",
};