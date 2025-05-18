module;

module viu.client;

import std;

import viu.boost;
import viu.error;
import viu.cli;
import viu.daemon;

using boost::asio::local::stream_protocol;

namespace viu {

auto client::send_command(int argc, const char* argv[]) -> int
{
    try {
        boost::asio::io_context io;
        stream_protocol::socket socket(io);
        auto path = viu::daemon::service::socket_path();
        socket.connect(stream_protocol::endpoint(path));

        auto payload = cli::serialize_argv(argc, argv);
        std::uint32_t size = payload.size();

        boost::asio::write(socket, boost::asio::buffer(&size, sizeof(size)));
        boost::asio::write(socket, boost::asio::buffer(payload));

        std::uint32_t msg_size = 0;
        boost::asio::read(
            socket,
            boost::asio::buffer(&msg_size, sizeof(msg_size))
        );

        std::vector<char> buffer(msg_size);
        boost::asio::read(
            socket,
            boost::asio::buffer(buffer.data(), buffer.size())
        );

        std::string data(buffer.begin(), buffer.end());
        viu::response resp = viu::response::deserialize(data);

        std::println("Response:\n{}", resp.message());
    } catch (const std::exception& e) {
        std::println("Daemon command failed: {}", e.what());
        return 1;
    }
    return 0;
}

auto client::run(int argc, const char* argv[]) -> int
{
    return send_command(argc, argv);
}

} // namespace viu
