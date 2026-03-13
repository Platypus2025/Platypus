#pragma once
#include <unordered_set>
#include <string>

std::string libstring = "ssl";

extern const std::unordered_set<std::string> dso_callbacks = {
};

extern const std::unordered_set<std::string> struct_names = {
    "ASN1_ITEM_st",
    "bio_method_st",
    "cert_st",
    "crypto_thread_st",
    "hm_fragment_st",
    "hm_header_st",
    "ossl_ackm_st",
    "ossl_ackm_tx_pkt_st",
    "ossl_cc_method_st",
    "ossl_cc_newreno_st",
    "ossl_dispatch_st",
    "ossl_pqueue_st",
    "ossl_qrx_pkt_st",
    "ossl_qrx_st",
    "ossl_qtx_st",
    "ossl_quic_tx_packetiser_args_st",
    "ossl_quic_tx_packetiser_st",
    "ossl_record_layer_st",
    "ossl_record_method_st",
    "qctx_st",
    "quic_cfq_item_ex_st",
    "quic_cfq_st",
    "quic_channel_args_st",
    "quic_channel_st",
    "quic_demux_conn_st",
    "quic_demux_st",
    "quic_fifd_st",
    "quic_reactor_st",
    "quic_rstream_st",
    "quic_rxfc_st",
    "quic_stream_iter_st",
    "quic_stream_map_st",
    "quic_stream_st",
    "quic_thread_assist_st",
    "quic_tls_args_st",
    "quic_tls_st",
    "quic_tserver_args_st",
    "quic_tserver_st",
    "quic_txpim_pkt_st",
    "quic_txpim_st",
    "record_layer_st",
    "ssl_conf_ctx_st",
    "ssl_connection_st",
    "ssl_ctx_st",
    "ssl_method_st",
    "ssl_session_st",
    "ssl_st",
    "unreg_arg",
};

extern const std::unordered_set<std::string> global_names = {
        "l_ctx_st",
};