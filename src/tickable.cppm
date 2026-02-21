export module viu.tickable;

import std;

import viu.boost;
import viu.usb.mock.abi;

namespace viu {

export class tick_service final {
public:
    tick_service() { start(); }
    ~tick_service() { stop(); }

    tick_service(const tick_service&) = delete;
    tick_service(tick_service&&) = delete;
    auto operator=(const tick_service&) -> tick_service& = delete;
    auto operator=(tick_service&&) -> tick_service& = delete;

    void add(viu_usb_mock_opaque* opaque)
    {
        std::scoped_lock lock(mutex_);
        opaque_tickables_.push_back(opaque);
    }

    void start()
    {
        if (worker_.joinable()) {
            return;
        }

        worker_ = std::jthread([this](const std::stop_token& st) -> void {
            run(st);
        });
    }

    void stop()
    {
        if (worker_.joinable()) {
            worker_.request_stop();
            worker_.join();
        }
    }

private:
    void run(const std::stop_token& st)
    {
        while (!st.stop_requested()) {
            {
                std::scoped_lock lock(mutex_);

                for (auto& opaque_obj : opaque_tickables_) {
                    if (opaque_obj == nullptr ||
                        opaque_obj->tick_interval == nullptr ||
                        opaque_obj->tick == nullptr) {
                        continue;
                    }

                    const auto interval = std::chrono::milliseconds{
                        opaque_obj->tick_interval(opaque_obj->ctx)
                    };
                    if (interval.count() == 0) {
                        continue;
                    }

                    const auto now = std::chrono::steady_clock::now();
                    const auto last_tick = opaque_last_tick_time_[opaque_obj];
                    const auto elapsed =
                        std::chrono::duration_cast<std::chrono::milliseconds>(
                            now - last_tick
                        );

                    if (elapsed >= interval) {
                        opaque_obj->tick(opaque_obj->ctx);
                        opaque_last_tick_time_[opaque_obj] = now;
                    }
                }
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }

    std::vector<viu_usb_mock_opaque*> opaque_tickables_;
    std::map<viu_usb_mock_opaque*, std::chrono::steady_clock::time_point>
        opaque_last_tick_time_;

    std::mutex mutex_;
    std::jthread worker_;
};

static_assert(!std::copyable<tick_service>);

} // namespace viu
