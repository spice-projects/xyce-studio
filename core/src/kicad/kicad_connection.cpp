#include <cstdlib>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>

#include <nng/nng.h>
#include <nng/protocol/reqrep0/req.h>
#include <spdlog/spdlog.h>

#include "common/commands/base_commands.pb.h"
#include "core/util.h"
#include "kicad_connection.h"

namespace
{
    // client name reported to the KiCad API server for debug logging
    constexpr const char* CLIENT_NAME = "Xyce Simulator Plugin";

    // send and receive timeout for a single API request in milliseconds
    constexpr int REQUEST_TIMEOUT_MS = 30000;
} // namespace

KiCadConnection::KiCadConnection(std::string socket_url, std::string token, std::string client_name) :
    m_token(std::move(token)), m_client_name(std::move(client_name)) {
    // open a REQ socket for the request/reply exchange
    int rv = nng_req0_open(&m_socket);
    if (rv != 0)
        throw std::runtime_error(std::string("failed to open KiCad API socket: ") + nng_strerror(rv));
    // mark the socket as open
    m_open = true;
    // bound the time spent waiting for the server to respond
    nng_socket_set_ms(m_socket, NNG_OPT_SENDTIMEO, REQUEST_TIMEOUT_MS);
    nng_socket_set_ms(m_socket, NNG_OPT_RECVTIMEO, REQUEST_TIMEOUT_MS);
    // connect to the KiCad API server
    rv = nng_dial(m_socket, socket_url.c_str(), nullptr, 0);
    if (rv != 0) {
        // log the failure with the nng error description
        spdlog::error("Failed to dial KiCad API socket {}: {}", socket_url, nng_strerror(rv));
        // close the socket before throwing
        nng_close(m_socket);
        m_open = false;
        // exit with a descriptive error
        throw std::runtime_error(std::string("failed to dial KiCad API socket: ") + nng_strerror(rv));
    }
}

KiCadConnection::~KiCadConnection() {
    // close the socket if it is still open
    if (m_open)
        nng_close(m_socket);
}

KiCadConnection::KiCadConnection(KiCadConnection&& other) noexcept :
    m_socket(other.m_socket), m_open(other.m_open), m_token(std::move(other.m_token)), m_client_name(std::move(other.m_client_name)) {
    // reset the source object so it no longer owns the socket
    other.m_socket = NNG_SOCKET_INITIALIZER;
    other.m_open = false;
}

KiCadConnection& KiCadConnection::operator=(KiCadConnection&& other) noexcept {
    // guard against self assignment
    if (this == &other)
        return *this;
    // close the current socket before taking ownership
    if (m_open)
        nng_close(m_socket);
    // transfer ownership from the source object
    m_socket = other.m_socket;
    m_open = other.m_open;
    m_token = std::move(other.m_token);
    m_client_name = std::move(other.m_client_name);
    // reset the source object
    other.m_socket = NNG_SOCKET_INITIALIZER;
    other.m_open = false;
    // exit
    return *this;
}

bool KiCadConnection::is_kicad_mode() {
    // both environment variables must be present and non-empty for KiCad plugin mode
    const auto socket = get_environment_variable("KICAD_API_SOCKET");
    const auto token = get_environment_variable("KICAD_API_TOKEN");
    // exit
    return socket.has_value() && !socket->empty() && token.has_value() && !token->empty();
}

std::unique_ptr<KiCadConnection> KiCadConnection::from_environment() {
    // no connection when running in standalone mode
    if (!is_kicad_mode())
        return nullptr;
    // read the socket URL and token from the environment
    const auto socket = get_environment_variable("KICAD_API_SOCKET");
    const auto token = get_environment_variable("KICAD_API_TOKEN");
    // log information
    spdlog::info("Running in KiCad plugin mode, KICAD_API_SOCKET={}, KICAD_API_TOKEN={}", socket.value_or(""), token.value_or(""));
    // create the connection
    return std::make_unique<KiCadConnection>(socket.value_or(""), token.value_or(""), CLIENT_NAME);
}

std::string KiCadConnection::get_kicad_binary_path(const std::string& binary_name) {
    // build the request command
    kiapi::common::commands::GetKiCadBinaryPath command;
    command.set_binary_name(binary_name);
    // exchange the command and unpack the path response
    kiapi::common::commands::PathResponse path_response;
    exchange(command, &path_response);
    // exit
    return path_response.path();
}

std::string KiCadConnection::get_version() {
    // build the request command
    kiapi::common::commands::GetVersion command;
    // exchange the command and unpack the version response
    kiapi::common::commands::GetVersionResponse version_response;
    exchange(command, &version_response);
    // reference the reported version
    const auto& version = version_response.version();
    // prefer the full identifier string when present
    if (!version.full_version().empty())
        return version.full_version();
    // fall back to a composed major.minor.patch string
    return std::to_string(version.major()) + "." + std::to_string(version.minor()) + "." + std::to_string(version.patch());
}

void KiCadConnection::ping() {
    // build the request command
    kiapi::common::commands::Ping command;
    // exchange the command without expecting a payload
    exchange(command, nullptr);
}

void KiCadConnection::exchange(const google::protobuf::Message& command, google::protobuf::Message* response) {
    // build the request envelope
    kiapi::common::ApiRequest request;
    auto* header = request.mutable_header();
    // identify the client and carry the connection token
    header->set_kicad_token(m_token);
    header->set_client_name(m_client_name);
    // pack the command into the Any payload
    request.mutable_message()->PackFrom(command);
    // serialize the request envelope
    std::string payload;
    if (!request.SerializeToString(&payload))
        throw std::runtime_error("failed to serialize KiCad API request");
    // send the serialized request
    int rv = nng_send(m_socket, payload.data(), payload.size(), 0);
    if (rv != 0)
        throw std::runtime_error(std::string("failed to send KiCad API request: ") + nng_strerror(rv));
    // receive the response envelope
    void* data = nullptr;
    size_t size = 0;
    rv = nng_recv(m_socket, &data, &size, NNG_FLAG_ALLOC);
    if (rv != 0)
        throw std::runtime_error(std::string("failed to receive KiCad API response: ") + nng_strerror(rv));
    // free the received buffer when leaving this scope
    struct BufferGuard
    {
        void* data;
        size_t size;

        ~BufferGuard() { nng_free(data, size); }
    } buffer_guard{data, size};
    // parse the response envelope
    kiapi::common::ApiResponse api_response;
    if (!api_response.ParseFromArray(data, static_cast<int>(size)))
        throw std::runtime_error("failed to parse KiCad API response");
    // adopt the server token for subsequent requests
    if (!api_response.header().kicad_token().empty())
        m_token = api_response.header().kicad_token();
    // check the response status
    if (api_response.status().status() != kiapi::common::AS_OK) {
        // build an error message from the reported status
        std::string error = "KiCad API request failed";
        if (!api_response.status().error_message().empty())
            error += ": " + api_response.status().error_message();
        // throw with the composed message
        throw std::runtime_error(error);
    }
    // unpack the response payload when a destination was provided
    if (response != nullptr && !api_response.message().UnpackTo(response))
        throw std::runtime_error("KiCad API response payload did not match the expected type");
}
