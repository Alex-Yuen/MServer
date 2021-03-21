#include <csignal>
#include "thread.hpp"
#include "../system/static_global.hpp"

std::atomic<uint32_t> Thread::_sig_mask(0);
std::atomic<int32_t> Thread::_id_seed(0);

Thread::Thread(const char *name)
{
    snprintf(_name, sizeof(_name), "%s", name);
    _id     = ++_id_seed;
    _status = S_NONE;

    _ev      = 0;
    _main_ev = 0;

    set_wait_busy(true); // 默认关服时都是需要待所有任务处理才能结束
}

Thread::~Thread()
{
    assert(!active());
    assert(!_thread.joinable());
}

void Thread::signal_block()
{
    /* 信号阻塞
     * 多线程中需要处理三种信号：
     * 1)pthread_kill向指定线程发送的信号，只有指定的线程收到并处理
     * 2)SIGSEGV之类由本线程异常的信号，只有本线程能收到。但通常是终止整个进程。
     * 3)SIGINT等通过外部传入的信号(如kill指令)，查找一个不阻塞该信号的线程。如果有多个，
     *   则选择第一个。
     * 因此，对于1，当前框架并未使用。对于2，默认abort并coredump。对于3，我们希望主线程
     * 收到而不希望子线程收到，故在这里要block掉。
     */

    /* TODO C++标准在csignal中提供了部分信号处理的接口，但没提供屏蔽信号的
     * linux下，使用pthread的接口应该是兼容的，但无法保证所有std::thread的实现都使用
     * pthread。因此不屏蔽子线程信号了，直接存起来，由主线程定时处理。但是这有一个问题就是
     * 假如主线程阻塞很久，没法唤醒。幸好当前的游戏框架会定时循环，对信号处理的实时性不高，
     * 因此不需要处理这个问题
     */

    /*
    sigset_t mask;
    sigemptyset(&mask);
    sigaddset(&mask, SIGINT);
    sigaddset(&mask, SIGTERM);

    pthread_sigmask(SIG_BLOCK, &mask, NULL);*/
}

void Thread::sig_handler(int32_t signum)
{
    // 把有线程收到的信号存这里，由主线程定时处理
    _sig_mask |= (1 << signum);
}

void Thread::signal(int32_t sig, int32_t action)
{
    /* check /usr/include/bits/signum.h for more */
    if (0 == action)
    {
        ::signal(sig, SIG_DFL);
    }
    else if (1 == action)
    {
        ::signal(sig, SIG_IGN);
    }
    else
    {
        ::signal(sig, sig_handler);
    }
}

/* 开始线程 */
bool Thread::start(int32_t us)
{
    // 只能在主线程调用，threadmgr没加锁
    assert(std::this_thread::get_id() != _thread.get_id());

    mark(S_RUN);
    _thread = std::thread(&Thread::spawn, this, us);

    StaticGlobal::thread_mgr()->push(this);

    return true;
}

/* 终止线程 */
void Thread::stop()
{
    // 只能在主线程调用，thread_mgr没加锁
    assert(std::this_thread::get_id() != _thread.get_id());
    if (!active())
    {
        ERROR("thread::stop:thread not running");
        return;
    }

    unmark(S_RUN);
    wakeup(S_RUN);
    _thread.join();

    StaticGlobal::thread_mgr()->pop(_id);
}

/* 线程入口函数 */
void Thread::spawn(int32_t us)
{
    signal_block(); /* 子线程不处理外部信号 */

    if (!initialize()) /* 初始化 */
    {
        ERROR("%s thread initialize fail", _name);
        return;
    }

    std::chrono::microseconds timeout(us);
    {
        // unique_lock在构建时会加锁，然后由wait_for解锁并进入等待
        // 当条件满足时，wait_for返回并加锁，最后unique_lock析构时解锁
        std::unique_lock<std::mutex> ul(_mutex);
        while (_status & S_RUN)
        {
            int32_t ev = _ev.exchange(0);
            if (!ev)
            {
                // 这里可能会出现spurious wakeup(例如收到一个信号)，但不需要额外处理
                // 目前所有的子线程唤醒多次都没有问题，以后有需求再改
                _cv.wait_for(ul, timeout);
            }

            ul.unlock();
            mark(S_BUSY);
            this->routine(ev);
            unmark(S_BUSY);
            ul.lock();
        }
    }

    if (!uninitialize()) /* 清理 */
    {
        ERROR("%s thread uninitialize fail", _name);
        return;
    }
}
