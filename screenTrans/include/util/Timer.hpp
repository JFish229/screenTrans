#pragma once
#include <chrono>
namespace util {
    class Timer {
    public:
        using Clock = std::chrono::steady_clock;
        using TimePoint = Clock::time_point;
        using Duration = std::chrono::duration<double>;   // 单位为秒的浮点时长

        /// 构造后立即开始计时
        Timer() { start(); }

        /// 从一个已有时间点开始计时（用于测量“某时刻到现在”）
        explicit Timer(TimePoint tp) : start_(tp), running_(true) {}

        /// 重新开始（清零并计时）
        void start() {
            start_ = Clock::now();
            elapsed_ = Duration::zero();
            running_ = true;
        }

        /// 暂停，累计已过时间
        void stop() {
            if (!running_) return;
            elapsed_ += Clock::now() - start_;
            running_ = false;
        }

        /// 从暂停处继续
        void resume() {
            if (running_) return;
            start_ = Clock::now();
            running_ = true;
        }

        /// 清零并停止
        void reset() {
            elapsed_ = Duration::zero();
            running_ = false;
        }

        /// 清零并立即重新开始（等价于 start()）
        void restart() { start(); }

        bool isRunning() const { return running_; }

        /// 已过时间（duration<double>，秒）
        Duration elapsed() const {
            if (!running_) return elapsed_;
            return elapsed_ + (Clock::now() - start_);
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
        TimePoint start_{};
        Duration  elapsed_{ Duration::zero() };
        bool      running_{ false };
    };
}