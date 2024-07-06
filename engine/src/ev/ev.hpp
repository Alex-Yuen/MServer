#pragma once

#include <atomic>
#include <condition_variable>

#include "ev_watcher.hpp"
#include "thread/spin_lock.hpp"
#include "global/global.hpp"

/**
 * @brief 主循环事件定义，这些事件可能会同时触发，按位定义
 */
enum
{
    EV_NONE    = 0x000, /// 0  无任何事件
    EV_READ    = 0x001, /// 1  socket读(接收)
    EV_WRITE   = 0x002, /// 2  socket写(发送)
    EV_ACCEPT  = 0x004, /// 4  监听到有新连接
    EV_CONNECT = 0x008, /// 8  连接成功或者失败
    EV_CLOSE   = 0x010, /// 16 socket关闭
    EV_FLUSH   = 0x020, /// 32 发送数据，随后关闭
    EV_TIMER   = 0x080, /// 128 定时器超时
    EV_ERROR   = 0x100  /// 256 出错
};

class EVIO;
class EVTimer;
class EVWatcher;
class EVBackend;

using HeapNode = EVTimer *;

extern const char *__BACKEND__;

// event loop
class EV
{
public:
    EV();
    virtual ~EV();

    int32_t loop();
    int32_t quit();

    /**
     * @brief 启动一个io监听
     * @param id 唯一的id
     * @param fd 通过socket函数创建的文件描述符
     * @param event 监听事件，EV_READ|EV_WRITE
     * @return io对象
     */
    EVIO *io_start(int32_t id, int32_t fd, int32_t event);
    /**
     * @brief 通知io线程停止一个io监听
     * @param id 唯一的id
     * @param flush 是否发送完数据再关闭
     * @return
     */
    int32_t io_stop(int32_t id, bool flush);
    /**
     * @brief 删除一个io监听器
     * @param id 唯一的id
     * @return 
    */
    int32_t io_delete(int32_t id);
    /**
     * @brief 获取任意io对象，只能主线程调用
     * @param fd
     * @return
     */
    EVIO *get_io(int32_t fd)
    {
        auto found = _io_mgr.find(fd);
        return found == _io_mgr.end() ? nullptr : &(found->second);
    }

    /// @brief  启动定时器
    /// @param id 定时器唯一id
    /// @param after N毫秒秒后第一次执行
    /// @param repeat 重复执行间隔，毫秒数
    /// @param policy 定时器重新规则时的策略
    /// @return 成功返回》=1,失败返回值<0
    int32_t timer_start(int32_t id, int64_t after, int64_t repeat,
                        int32_t policy);
    /// @brief 停止定时器并从管理器中删除
    /// @param id 定时器唯一id
    /// @return 成功返回0
    int32_t timer_stop(int32_t id);
    /// 停止定时器，但不从管理器删除
    int32_t timer_stop(EVTimer *w);

    /// @brief  启动utc定时器
    /// @param id 定时器唯一id
    /// @param after N秒后第一次执行
    /// @param repeat 重复执行间隔，秒数
    /// @param policy 定时器重新规则时的策略
    /// @return 成功返回》=1,失败返回值<0
    int32_t periodic_start(int32_t id, int64_t after, int64_t repeat,
                           int32_t policy);
    /// @brief 停止utc定时器并从管理器删除
    /// @param id 定时器唯一id
    /// @return 成功返回0
    int32_t periodic_stop(int32_t id);
    /// 停止utc定时器，但不从管理器删除
    int32_t periodic_stop(EVTimer *w);

    /**
     * 获取自进程启动以来的毫秒数
     */
    static int64_t steady_clock();
    /**
     * 获取UTC时间，精度毫秒
     */
    static int64_t system_clock();

    /// 获取当前的帧时间，精度为毫秒
    inline int64_t ms_now()
    {
        return _steady_clock;
    }
    /// 获取当前的帧时间，精度为秒
    inline int64_t now()
    {
        return _system_now;
    }
    /// 定时器回调函数
    virtual void timer_callback(int32_t id, int32_t revents)
    {
    }
    /**
     * @brief 标记io变化，稍后异步处理
     * @param fd
     */
    void io_change(EVIO *w);

    /**
     * @brief 主线程把一个io操作事件发送给backend线程并马上唤醒它来执行
     * @param fd
     * @param events
     */
    void io_fast_event(EVIO *w, int32_t events);

    /**
     * 获取backend线程待处理的事件
     */
    std::vector<EVIO *> &get_fast_event()
    {
        return _io_fevents;
    }
    /// 其他线程发送io事件给主线程处理(此函数需要外部加锁)
    void io_receive_event(EVIO *w, int32_t revents);

    /// 获取加锁对象
    std::mutex &lock()
    {
        // 加锁应该使用std::lock_guard而不是手动加锁_mutex.lock();
        // 尽量减少忘记释放锁的概率
        // 而且手动调lock()在vs下总是会有个C26110警告
        return _mutex;
    }

    /// 获取快速锁
    SpinLock &fast_lock()
    {
        return _spin_lock;
    }

    /**
     * @brief 唤醒主线程
     * @param job 如果为true，则加锁并设置_has_job标记
    */
    void wake(bool job)
    {
        if (job)
        {
            std::lock_guard<std::mutex> guard(_mutex);
            _has_job = true;
        }
        _cv.notify_one();
    }

    /// 设置是否有任务需要处理
    void set_job(bool job)
    {
        _has_job = job;
    }

protected:
    virtual void running() = 0;

    void io_reify();
    void time_update();
    /**
     * 设置watcher的回调事件
     */
    void feed_event(EVWatcher *w, int32_t revents);
    void invoke_pending();
    void clear_pending(EVWatcher *w);
    /**
     * 清除等待backend线程处理的事件
     * @param w io监听器
     */
    void clear_io_fast_event(EVIO *w);
    /**
     * 清除该监听器收到的io事件
     * @param w io监听器
     */
    void clear_io_receive_event(EVIO *w);
    void io_receive_event_reify();
    void timers_reify();
    void periodic_reify();
    void down_heap(HeapNode *heap, int32_t N, int32_t k);
    void up_heap(HeapNode *heap, int32_t k);
    void adjust_heap(HeapNode *heap, int32_t N, int32_t k);
    void reheap(HeapNode *heap, int32_t N);

protected:
    volatile bool _done; /// 主循环是否已结束

    /**
     * @brief 是否有任务需要处理
     * std::atomic_flag guaranteed to be lock-free而std::atomic<bool>不一定
     * 但atomic_flag需要C++20才有test函数，这就离谱
     * std::atomic<bool>在x86下是lock-free，将就着用吧，过几年再改用flag
     */
    std::atomic<bool> _has_job;

    /// 收到的io待处理事件
    std::vector<EVIO *> _io_revents;
    /// 触发了事件，等待处理的watcher
    std::vector<EVWatcher *> _pendings;

    /// 在主线程设置，待backend线程处理的事件
    std::vector<EVIO *> _io_fevents;

    /**
     * @brief 已经改变，等待设置到内核的io watcher
    */
    std::vector<EVIO *> _io_changes;
    /**
     * @brief _io_changes中用于删除的索引
    */
    int32_t _io_delete_index;

    std::unordered_map<int32_t, EVIO> _io_mgr; /// 管理所有io对象

    /**
     * 当前拥有的timer数量
     * 额外使用一个计数器管理timer，可以不用对_timers进行pop之类的操作
     */
    int32_t _timer_cnt;
    std::vector<EVTimer *> _timers; /// 按二叉树排列的定时器
    std::unordered_map<int32_t, EVTimer> _timer_mgr;

    int32_t _periodic_cnt;
    std::vector<EVTimer *> _periodics; /// 按二叉树排列的utc定时器
    std::unordered_map<int32_t, EVTimer> _periodic_mgr;

    EVBackend *_backend;          ///< io后台
    int64_t _busy_time;           ///< 上一次执行消耗的时间，毫秒

    int64_t _steady_clock;              ///< 起服到现在的毫秒
    int64_t _system_clock; // UTC时间戳（单位：毫秒）
    std::atomic<int64_t> _system_now; ///< UTC时间戳(CLOCK_REALTIME,秒)
    int64_t _last_system_clock_update;       ///< 上一次更新UTC的MONOTONIC时间
    /**
     * UTC时间与MONOTONIC时间的差值，用于通过mn_now直接计算出rt_now而
     * 不需要通过clock_gettime来得到rt_now，以提高效率
     */
    int64_t _clock_diff;

    int64_t _next_backend_time; // 下次执行backend的时间

    /// 主线程的wait condition_variable
    std::condition_variable _cv;

    /// 主线程锁
    std::mutex _mutex;

    /// 用于数据交换的spin lock
    SpinLock _spin_lock;
};
