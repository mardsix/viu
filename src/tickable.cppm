export module viu.tickable;

import std;

import viu.boost;

namespace viu {

export struct tickable {
    [[nodiscard]] virtual auto interval() const
        -> std::chrono::milliseconds = 0;
    virtual void tick() = 0;
};

export class tick_service final {
public:
    tick_service() { start(); }
    ~tick_service() { stop(); }

    tick_service(const tick_service&) = delete;
    tick_service(tick_service&&) = delete;
    auto operator=(const tick_service&) -> tick_service& = delete;
    auto operator=(tick_service&&) -> tick_service& = delete;

    void add(const std::shared_ptr<viu::tickable>& obj)
    {
        std::scoped_lock lock(mutex_);
        tickables_.push_back(obj);
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
                for (auto& tickable_obj : tickables_) {
                    if (tickable_obj == nullptr) {
                        continue;
                    }

                    const auto interval = tickable_obj->interval();
                    const auto now = std::chrono::steady_clock::now();
                    const auto last_tick = last_tick_time_[tickable_obj.get()];
                    const auto elapsed =
                        std::chrono::duration_cast<std::chrono::milliseconds>(
                            now - last_tick
                        );

                    if (elapsed >= interval) {
                        tickable_obj->tick();
                        last_tick_time_[tickable_obj.get()] = now;
                    }
                }
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }

    std::vector<std::shared_ptr<viu::tickable>> tickables_;
    std::map<viu::tickable*, std::chrono::steady_clock::time_point>
        last_tick_time_;
    std::mutex mutex_;
    std::jthread worker_;
};

static_assert(!std::copyable<tick_service>);

} // namespace viu
