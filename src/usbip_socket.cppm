export module viu.usbip.socket;

import std;

import viu.boost;

namespace viu::usbip {

export struct socket final {
    socket();
    ~socket();

    socket(const socket&) = delete;
    socket(socket&&) = delete;
    auto operator=(const socket&) -> socket& = delete;
    auto operator=(socket&&) -> socket& = delete;

    auto fd() -> int;

    void read(boost::asio::streambuf& read_buffer, std::size_t length);
    void write(boost::asio::streambuf& write_buffer, std::size_t length);
    void close();

private:
    boost::asio::io_context io_context_;

    using socket_type = boost::asio::local::stream_protocol::socket;
    std::array<socket_type, 2> sockets_{
        socket_type{io_context_},
        socket_type{io_context_}
    };

    auto host_socket() -> socket_type& { return sockets_.at(1); }
    auto client_socket() -> socket_type& { return sockets_.at(0); }
    void shutdown();
};

static_assert(!std::copyable<socket>);

} // namespace viu::usbip
