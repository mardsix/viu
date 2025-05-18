export module viu.device.basic;

import std;

import viu.boost;
import viu.transfer;
import viu.usb.descriptors;
import viu.vhci;

namespace viu::device {

export class basic {
public:
    basic();
    virtual ~basic();

protected:
    struct transfer_data {
        std::vector<std::uint8_t> buffer{};
        std::size_t iso_descriptor_size{};
        std::int32_t error_count;
    };

    void queue_reply_to_host(
        const usbip::command& cmd,
        const void* data,
        std::size_t size,
        std::int32_t status = 0,
        std::size_t iso_descriptor_size = 0,
        std::int32_t error_count = 0
    );

    void queue_data_for_host(const usb::transfer::pointer& transfer);
    void attach(std::uint32_t speed, std::uint8_t device_id);

private:
    virtual void execute_in_control_command(
        [[maybe_unused]] const usbip::command& cmd
    ) {};

    virtual void execute_out_control_command(
        [[maybe_unused]] const usbip::command& cmd
    ) {};

    virtual void send_data_to_device(
        [[maybe_unused]] const usbip::command& cmd
    ) {};

    virtual void read_data_from_device(
        [[maybe_unused]] const usbip::command& cmd
    ) {};

    void command_produce_thread();
    void reply_consume_thread();
    void transfer_thread(std::uint32_t ep);
    void command_execution_thread();
    auto read_command() -> usbip::command;
    void execute_command();
    void execute_control_command(const usbip::command& cmd);
    void execute_ep_command(const usbip::command& cmd);
    void unlink_command(const usbip::command& cmd);
    void send_data_to_host(std::uint32_t ep);

    std::vector<std::jthread> threads_{};

    using command_queue_type = boost::sync_queue<usbip::command>;
    std::array<command_queue_type, usb::endpoint::max_count_in> in_commands_{};

    using transfer_queue_type = boost::sync_queue<transfer_data>;
    std::array<transfer_queue_type, usb::endpoint::max_count_in> in_data_{};

    boost::sync_queue<usbip::command> commands_queue_{};
    boost::sync_queue<usbip::command> replies_queue_{};

    std::mutex unlinked_set_mutex_{};
    std::set<std::uint32_t> unlinked_seqnums_{};

    vhci::driver vhci_driver_{};
};

} // namespace viu::device
