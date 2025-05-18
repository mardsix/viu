module viu.usbip.socket;

import viu.assert;

using viu::usbip::socket;

socket::socket()
{
    boost::asio::local::connect_pair(client_socket(), host_socket());
}

socket::~socket() { close(); }

auto socket::fd() -> int { return host_socket().native_handle(); }

void socket::read(boost::asio::streambuf& read_buffer, const std::size_t length)
{
    const auto bytes_received = boost::asio::read(
        client_socket(),
        read_buffer.prepare(length),
        boost::asio::transfer_exactly(length)
    );
    read_buffer.commit(length);

    viu::_assert(bytes_received == length);
}

void socket::write(
    boost::asio::streambuf& write_buffer,
    const std::size_t length
)
{
    const auto bytes_sent = boost::asio::write(
        client_socket(),
        write_buffer,
        boost::asio::transfer_exactly(length)
    );

    viu::_assert(bytes_sent == length);
}

// TODO: Fix close data race with socket ops
void socket::close() { shutdown(); }

void socket::shutdown()
{
    for (auto& s : sockets_) {
        if (!s.is_open()) {
            continue;
        }

        boost::system::error_code ec;
        s.shutdown(boost::asio::ip::tcp::socket::shutdown_both, ec);
        viu::_assert(ec == boost::system::errc::success);
        s.close(ec);
        viu::_assert(ec == boost::system::errc::success);
    }
}
