#pragma once

#include <memory>
#include <optional>
#include <string>

#include <nng/nng.h>

#include "common/envelope.pb.h"

// client for the KiCad API server over an NNG REQ/REP connection
class KiCadConnection
{
public:
    KiCadConnection(std::string socket_url, std::string token, std::string client_name);

    ~KiCadConnection();

    KiCadConnection(const KiCadConnection&) = delete;

    KiCadConnection& operator=(const KiCadConnection&) = delete;

    KiCadConnection(KiCadConnection&& other) noexcept;

    KiCadConnection& operator=(KiCadConnection&& other) noexcept;

    // true when the KiCad plugin environment variables are set
    [[nodiscard]] static bool is_kicad_mode();

    // connect using the KICAD_API_SOCKET and KICAD_API_TOKEN environment variables
    [[nodiscard]] static std::unique_ptr<KiCadConnection> from_environment();

    // resolve the full path to a KiCad binary, such as kicad-cli
    [[nodiscard]] std::string get_kicad_binary_path(const std::string& binary_name);

    // query the running KiCad version
    [[nodiscard]] std::string get_version();

    // verify the connection to the KiCad API server
    void ping();

private:
    // send a command and unpack the response payload into response
    void exchange(const google::protobuf::Message& command, google::protobuf::Message* response);

    nng_socket m_socket;
    bool m_open = false;
    std::string m_token;
    std::string m_client_name;
};
