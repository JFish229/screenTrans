#pragma once
#include <chrono>

namespace util {
    class Timer {
    public:
        using Clock = std::chrono::steady_clock;
        using TimePoint = Clock::time_point;
        using Duration = std::chrono::duration<double>;   // 单位为秒的浮点时长

        /// 构造后立即开始计时
        Timer() : m_start(Clock::now()) {}

        /// 清零并重新开始
        void restart() { m_start = Clock::now(); }

        /// 已过时间（duration<double>，秒）
        Duration elapsed() const {
            return Clock::now() - m_start;
        }

        /// 以任意 chrono 单位返回，例如 elapsedAs<std::chrono::milliseconds>()
        template <typename Unit = std::chrono::seconds>
        double elapsedAs() const {
            return std::chrono::duration<double, typename Unit::period>(elapsed()).count();
        }

        double seconds()      const { return elapsed().count(); }
        double milliseconds() const { return elapsedAs<std::chrono::milliseconds>(); }
        double microseconds() const { return elapsedAs<std::chrono::microseconds>(); }
        double nanoseconds()  const { return elapsedAs<std::chrono::nanoseconds>(); }

    private:
        TimePoint m_start;
    };
}