#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <libusb.h>

#include "usb_mock_abi.hpp"

import std;

import viu.device.mock;
import viu.format;
import viu.transfer;
import viu.usb;
import viu.usb.descriptors;
import viu.usb.mock.abi;

namespace viu::test {

struct test_device_mock final {
    test_device_mock() = default;

    test_device_mock(const test_device_mock&) = delete;
    test_device_mock(test_device_mock&&) = delete;
    auto operator=(const test_device_mock&) -> test_device_mock& = delete;
    auto operator=(test_device_mock&&) -> test_device_mock& = delete;

    void on_transfer_request(viu_usb_mock_transfer_control_opaque xfer)
    {
        const auto ep = xfer.ep(&xfer) & 0x0f;

        if (xfer.is_in(&xfer)) {
            xfer.fill(&xfer, ep_data_[ep].data(), ep_data_[ep].size());
        } else if (xfer.is_out(&xfer)) {
            const auto size = xfer.size(&xfer);
            auto read_buffer = usb::transfer::buffer_type(size);
            xfer.read(&xfer, read_buffer.data(), 0);
            ep_data_[ep] = read_buffer;
        }

        xfer.complete(&xfer);
    }

    int on_control_setup(
        [[maybe_unused]] libusb_control_setup setup,
        [[maybe_unused]] std::uint8_t* data,
        [[maybe_unused]] std::size_t data_size,
        [[maybe_unused]] int result
    )
    {
        return LIBUSB_ERROR_NOT_SUPPORTED;
    }

    int on_set_configuration([[maybe_unused]] std::uint8_t index)
    {
        return LIBUSB_SUCCESS;
    }

    int on_set_interface(
        [[maybe_unused]] std::uint8_t interface,
        [[maybe_unused]] std::uint8_t alt_setting
    )
    {
        return LIBUSB_SUCCESS;
    }

private:
    std::array<std::vector<std::uint8_t>, 15> ep_data_{};
};

static_assert(!std::copyable<test_device_mock>);

REGISTER_USB_MOCK(test_device_mock_plugin, test_device_mock)

struct host final {
    host()
    {
        t_ = std::jthread{[this](const std::stop_token& stoken) {
            auto usb_dev = usb::device{0x0000, 0x0001};

            start_writes(usb_dev);

            auto completed = int{0};
            auto event_handler_thread = std::thread{[&usb_dev, &completed]() {
                while (completed == 0) {
                    auto result = usb_dev.handle_events(
                        std::chrono::milliseconds{100},
                        &completed
                    );

                    ASSERT_EQ(result, LIBUSB_SUCCESS);
                }
            }};

            wait_for_writes();

            while (!stoken.stop_requested()) {
                using namespace std::chrono_literals;
                std::this_thread::sleep_for(20ms);
                start_reads(usb_dev);
                wait_for_reads();
            }

            completed = 1;
            event_handler_thread.join();
        }};
    }

    ~host()
    {
        while (!t_.request_stop())
            ;
        t_.join();
    }

private:
    using xfer_ptr_t = usb::transfer::pointer;

    void submit_transfer(usb::device& usb_dev, const usb::transfer::info& xfer)
    {
        const auto ep_xfer_type = usb_dev.ep_transfer_type(xfer.ep_address);
        ASSERT_TRUE(ep_xfer_type);

        switch (*ep_xfer_type) {
            case LIBUSB_ENDPOINT_TRANSFER_TYPE_CONTROL:
                break;

            case LIBUSB_ENDPOINT_TRANSFER_TYPE_ISOCHRONOUS:
                usb_dev.submit_iso_transfer(xfer);
                break;

            case LIBUSB_ENDPOINT_TRANSFER_TYPE_BULK:
                usb_dev.submit_bulk_transfer(xfer);
                break;

            case LIBUSB_ENDPOINT_TRANSFER_TYPE_INTERRUPT:
                usb_dev.submit_interrupt_transfer(xfer);
                break;

            default:
                ASSERT_TRUE(false);
                break;
        }
    }

    auto generate_data(std::size_t size)
    {
        auto rnd_device = std::random_device{};
        std::mt19937 mersenne_twister{rnd_device()};

        auto distribution = std::uniform_int_distribution<int>{
            std::numeric_limits<usb::transfer::buffer_type::value_type>::min(),
            std::numeric_limits<usb::transfer::buffer_type::value_type>::max()
        };
        auto generator = [&]() { return distribution(mersenne_twister); };

        auto vec = usb::transfer::buffer_type(size);
        std::generate(vec.begin(), vec.end(), generator);

        return vec;
    }

    void start_writes(usb::device& usb_dev)
    {
        write_count_ = 3;
        for (auto i = std::uint8_t{1}; i <= 3; ++i) {
            auto data_buffer = generate_data(data_size_);

            ep_data_[i - 1] = data_buffer;

            const auto cb = [=, this](const xfer_ptr_t transfer) {
                EXPECT_TRUE(transfer != nullptr);
                EXPECT_TRUE(transfer->buffer != nullptr);

                const auto xfer_data = viu::format::unsafe::vectorize(
                    transfer->buffer,
                    usb::transfer::actual_length(transfer)
                );

                EXPECT_EQ(xfer_data, ep_data_[i - 1]);

                write_count_--;
            };

            const auto xfer = usb::transfer::info{
                .ep_address = i,
                .buffer = data_buffer,
                .callback = cb
            };

            submit_transfer(usb_dev, xfer);
        }
    }

    void wait_for_writes()
    {
        while (write_count_ != 0)
            ;
    }

    void start_reads(usb::device& usb_dev)
    {
        read_count_ = 3;
        for (auto i = 1; i <= 3; ++i) {
            auto data_buffer = usb::transfer::buffer_type(data_size_);

            const auto cb = [=, this](const xfer_ptr_t transfer) {
                EXPECT_TRUE(transfer != nullptr);
                EXPECT_TRUE(transfer->buffer != nullptr);

                const auto xfer_data = viu::format::unsafe::vectorize(
                    transfer->buffer,
                    usb::transfer::actual_length(transfer)
                );

                EXPECT_EQ(xfer_data, ep_data_[i - 1]);

                read_count_--;
            };

            const auto xfer = usb::transfer::info{
                .ep_address = static_cast<std::uint8_t>(i | std::uint8_t{0x80}),
                .buffer = data_buffer,
                .callback = cb
            };

            submit_transfer(usb_dev, xfer);
        }
    }

    void wait_for_reads()
    {
        while (read_count_ != 0)
            ;
    }

    std::jthread device_thread_{};
    std::jthread t_{};
    std::atomic_uint32_t write_count_{};
    std::atomic_uint32_t read_count_{};
    std::array<usb::transfer::buffer_type, 15> ep_data_{};
    static constexpr std::uint32_t data_size_{1024};
};

struct device final {
    device()
    {
        t_ = std::jthread{[this](const std::stop_token& stoken) {
            load_descriptor_tree();

            [[maybe_unused]] auto mock_device = viu::device::mock{
                descriptor_tree_,
                test_device_mock_plugin_create()
            };

            while (!stoken.stop_requested()) {
                using namespace std::chrono_literals;
                std::this_thread::sleep_for(20ms);
            }
        }};
    }

    ~device()
    {
        while (!t_.request_stop())
            ;
        t_.join();
    }

private:
    void load_descriptor_tree()
    {
        descriptor_tree_.load("test_device_config.json");
    }

    usb::descriptor::tree descriptor_tree_{};
    std::jthread t_{};
};

class usb_mock_test : public testing::Test {};

TEST_F(usb_mock_test, 3s_transfer)
{
    using namespace std::chrono_literals;

    auto virtual_device = device{};
    std::this_thread::sleep_for(500ms);
    auto test_host = host{};
    std::this_thread::sleep_for(3s);
}

} // namespace viu::test
