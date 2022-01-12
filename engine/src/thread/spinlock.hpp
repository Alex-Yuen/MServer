#pragma once

#include <atomic>

/**
 * https://www.cnblogs.com/coding-my-life/p/15779082.html
 * C++ std����ʱû��spinlock����Ҫ�Լ�ʵ��
 * pthread_spinlock_t���ܸ���һЩ��������ͨ��
 */

/// ��std::atomic_flagʵ�ֵ�spin lock
class SpinLock final
{
public:
    SpinLock()
    {
        _flag.clear();

        // C++20 This macro is no longer needed and deprecated,
        // since default constructor of std::atomic_flag initializes it to clear
        // state. _flag = ATOMIC_FLAG_INIT;
    }
    ~SpinLock()                 = default;
    SpinLock(const SpinLock &)  = delete;
    SpinLock(const SpinLock &&) = delete;

    void lock() noexcept
    {
        // https://en.cppreference.com/w/cpp/atomic/atomic_flag_test_and_set
        // Example A spinlock mutex can be implemented in userspace using an atomic_flag

        //
        while (_flag.test_and_set(std::memory_order_acquire))
            ;
    }

    bool try_lock() noexcept
    {
        return !_flag.test_and_set(std::memory_order_acquire);
    }

    void unlock() noexcept
    {
        _flag.clear(std::memory_order_release);
    }

private:
    std::atomic_flag _flag;
};
