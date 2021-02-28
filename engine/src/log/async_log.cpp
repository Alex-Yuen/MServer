#include <filesystem>

#include "async_log.hpp"
#include "../system/static_global.hpp"

////////////////////////////////////////////////////////////////////////////////
AsyncLog::Policy::Policy()
{
    _data  = 0;
    _data2 = 0;
    _file  = nullptr;
    _type  = PT_NONE;
}
AsyncLog::Policy::~Policy()
{
    close_stream();
}

void AsyncLog::Policy::close_stream()
{
    if (_file) ::fclose(_file);
}

void AsyncLog::Policy::trigger_daily_rollover(int64_t now)
{
    assert(PT_NONE != _type);
    // 还在同一天
    if (PT_DAILY != _type || now >= _data || now <= _data + 86400) return;

    close_stream();

    struct tm ntm;
    ::localtime_r(&_data, &ntm);

    // 修正文件名 runtime&DAILY% 转换为 runtime2021-02-28
    char date[64];
    int len = snprintf(date, sizeof(date), "%04d-%02d-%02d", ntm.tm_year,
                       ntm.tm_mon + 1, ntm.tm_mday);

    std::string path(_raw_path);
    path.replace(path.begin(), path.end(), date, len);

    ::localtime_r(&now, &ntm);
    ntm.tm_hour = 0;
    ntm.tm_min  = 0;
    ntm.tm_sec  = 0;
    _data       = std::mktime(&ntm);

    std::error_code e;
    bool ok = std::filesystem::exists(_path, e);
    if (!e)
    {
        ERROR_R("rename daily check file exist file error %s %s", _path.c_str(),
                e.message().c_str());
        return;
    }
    if (ok)
    {
        std::filesystem::rename(_path, path, e);
        if (!e)
        {
            ERROR_R("rename daily log file error %s %s", path.c_str(),
                    e.message().c_str());
        }
    }
}

void AsyncLog::Policy::trigger_size_rollover(int64_t size)
{
    assert(PT_NONE != _type);
    if (PT_SIZE != _type) return;

    _data2 += size;
    if (_data2 < _data) return;

    _data2 = 0;
    close_stream();

    std::string old_path;
    std::string new_path;

    // TODO C++20之后直接用std::string::format而不需要用c的snprintf
    char old_buff[1024];
    char new_buff[1024];

    std::error_code e;
    int32_t max_index = 0;
    for (int32_t i = max_index + 1; i < 1024; i++)
    {
        snprintf(old_buff, sizeof(old_buff), _raw_path.c_str(), i);

        old_path.assign(old_buff);
        bool ok = std::filesystem::exists(old_path, e);
        if (!e)
        {
            ERROR_R("rename size check file exist file error %s %s",
                    _path.c_str(), e.message().c_str());
            return;
        }

        if (!ok) break;

        max_index++;
    }

    for (int32_t i = max_index + 1; i > 1; i--)
    {
        snprintf(new_buff, sizeof(new_buff), _raw_path.c_str(), i);
        snprintf(old_buff, sizeof(old_buff), _raw_path.c_str(), i - 1);

        new_path.assign(new_buff);
        old_path.assign(old_buff);
        std::filesystem::rename(old_path, new_path, e);
        if (!e)
        {
            ERROR_R("rename size file error %s %s", new_path.c_str(),
                    e.message().c_str());
            return;
        }
    }

    // 把runtime命名为runtime.1
    bool ok = std::filesystem::exists(_path, e);
    if (!e)
    {
        ERROR_R("rename size check current file exist file error %s %s",
                _path.c_str(), e.message().c_str());
        return;
    }

    if (ok)
    {
        snprintf(new_buff, sizeof(new_buff), _raw_path.c_str(), 1);
        std::filesystem::rename(_path, new_path, e);
        if (!e)
        {
            ERROR_R("rename size current file error %s %s", _path.c_str(),
                    e.message().c_str());
            return;
        }
    }
}

bool AsyncLog::Policy::init_size_policy(const std::string &path)
{
    /// 按文件大小切分 runtime%SIZE1024%表示文件大小为1024
    size_t pos = path.find("%SIZE");
    if (pos == std::string::npos) return false;

    size_t spos = path.find("%", pos + 5);
    if (spos == std::string::npos) return false;

    // 把runtime%SIZE1024%改成runtime用于写入日志
    _path.assign(path);
    _path.replace(pos, spos - pos, "");

    // 把runtime%SIZE1024%改成runtime.%d，用于稍后格式化成runtime.1
    _raw_path.assign(path);
    _path.replace(pos, spos - pos, ".%d");

    _type = PT_SIZE;
    _data = std::stoll(path.substr(pos + 5, spos)); // 日志文件的大小上限

    std::error_code e;
    bool ok = std::filesystem::exists(_path, e);
    if (!e)
    {
        ERROR_R("init size policy check file exist file error %s %s",
                _path.c_str(), e.message().c_str());
        return true;
    }
    if (ok)
    {
        _data2 = std::filesystem::file_size(_path, e);
        if (!e)
        {
            ERROR_R("init size policy file size exist file error %s %s",
                    _path.c_str(), e.message().c_str());
            return true;
        }
    }

    return true;
}

bool AsyncLog::Policy::init_daily_policy(const std::string &path)
{
    /// 按天切分
    size_t pos = path.find("%DAILY%");
    if (pos == std::string::npos) return false;

    // 获取当前文件的最后修改时间
    _raw_path.assign(path);

    _path.assign(path);
    _path.replace(_path.begin(), _path.end(), "%DAILY%", "");

    _data = -86400;
    _type = PT_DAILY;

    std::error_code e;
    bool ok = std::filesystem::exists(_path, e);
    if (!e)
    {
        ERROR_R("rename daily initfile exist file error %s %s", _path.c_str(),
                e.message().c_str());
        return true; // 即使获取不了上次文件的时间，也按天切分文件
    }
    if (ok)
    {
        auto ftime = std::filesystem::last_write_time(_path);
        _data      = decltype(ftime)::clock::to_time_t(ftime);
    }
    return true;
}

void AsyncLog::Policy::init_policy(const std::string &path)
{
    if (init_daily_policy(path)) return;
    if (init_size_policy(path)) return;

    /// 不需要切分，传的即文件名
    _type = PT_NORMAL;
    _path.assign(path);
}

FILE *AsyncLog::Policy::open_stream()
{
    assert(PT_NONE != _type);

    if (!_file)
    {
        _file = ::fopen(_path.c_str(), "ab+");
        if (!_file)
        {
            ERROR_R("can't open log file(%s):%s\n", _path.c_str(),
                    strerror(errno));
            return nullptr;
        }
    }

    return _file;
}
////////////////////////////////////////////////////////////////////////////////

size_t AsyncLog::busy_job(size_t *finished, size_t *unfinished)
{
    lock();
    size_t unfinished_sz = 0;
    for (auto iter = _device.begin(); iter != _device.end(); iter++)
    {
        unfinished_sz += iter->second._buff.size();
    }

    if (is_busy()) unfinished_sz += 1;
    unlock();

    if (finished) *finished = 0;
    if (unfinished) *unfinished = unfinished_sz;

    return unfinished_sz;
}

void AsyncLog::append(const char *path, LogType type, int64_t time,
                      const char *ctx, size_t len)
{
    assert(path);
    thread_local std::string str_path;
    str_path.assign(path);

    /* 时间必须取主循环的帧，不能取即时的时间戳 */
    lock();
    Device &device = _device[str_path];
    if (Policy::PT_NONE == device._policy.get_type())
    {
        device._policy.init_policy(str_path);
    }
    Buffer *buff = device_reserve(device, time, type);

    size_t cpy_len = std::min(sizeof(buff->_buff), len);
    memcpy(buff->_buff, ctx, cpy_len);
    buff->_used += cpy_len;

    // 如果一个缓冲区放不下，后面接多个缓冲区，时间戳为0
    if (EXPECT_FALSE(cpy_len < len))
    {
        size_t cur_len = cpy_len;
        do
        {
            buff = device_reserve(device, 0, type);

            cpy_len = std::min(sizeof(buff->_buff), len - cur_len);
            memcpy(buff->_buff, ctx + cur_len, cpy_len);

            cur_len += cpy_len;
            buff->_used += cpy_len;
        } while (cur_len < len);
    }

    unlock();
}

size_t AsyncLog::write_buffer(FILE *stream, const char *prefix,
                              const Buffer *buffer, bool beg, bool end)
{
    size_t bytes = 0;
    if (buffer->_time)
    {
        if (!beg)
        {
            bytes++;
            fputc('\n', stream);
        }
        bytes += write_prefix(stream, prefix, buffer->_time);
    }

    bytes += fwrite(buffer->_buff, 1, buffer->_used, stream);

    if (end)
    {
        bytes++;
        fputc('\n', stream);
    }

    return bytes;
}

void AsyncLog::write_device(Policy *policy, const BufferList &buffers)
{
    size_t size = buffers.size();
    for (size_t i = 0; i < size; i++)
    {
        size_t bytes         = 0;
        bool beg             = 0 == i;
        bool end             = i == size - 1;
        const Buffer *buffer = buffers[i];
        FILE *stream         = policy->open_stream();
        policy->trigger_daily_rollover(buffer->_time);
        switch (buffer->_type)
        {
        case LT_FILE:
        {
            bytes = write_buffer(stream, "", buffer, beg, end);
            break;
        }
        case LT_LPRINTF:
        {
            bytes = write_buffer(stream, "LP", buffer, beg, end);
            if (!is_deamon()) write_buffer(stdout, "LP", buffer, beg, end);
            break;
        }
        case LT_LERROR:
        {
            bytes = write_buffer(stream, "LE", buffer, beg, end);
            if (!is_deamon()) write_buffer(stderr, "LE", buffer, beg, end);
            break;
        }
        case LT_CPRINTF:
        {
            bytes = write_buffer(stream, "CP", buffer, beg, end);
            if (!is_deamon()) write_buffer(stdout, "CP", buffer, beg, end);
            break;
        }
        case LT_CERROR:
        {
            bytes = write_buffer(stream, "CE", buffer, beg, end);
            if (!is_deamon()) write_buffer(stderr, "CE", buffer, beg, end);
            break;
        }
        default: assert(false); break;
        }

        policy->trigger_size_rollover(bytes);
    }
}

// 线程主循环
void AsyncLog::routine(int32_t ev)
{
    UNUSED(ev);

    // https://en.cppreference.com/w/cpp/container/unordered_map/erase
    // https://stackoverflow.com/questions/38468844/erasing-elements-from-unordered-map-in-a-loop
    // The order of the elements that are not erased is preserved. (This makes
    // it possible to erase individual elements while iterating through the
    // container.)
    // C++14以后，允许在循环中删除
    static_assert(__cplusplus > 201402L);

    auto now = StaticGlobal::ev()->now();

    lock();
    while (true)
    {
        Policy *policy = nullptr;

        // 这里有点问题，如果日志量很大，可能会饿死其他文件。导致某些文件一下没写入
        for (auto iter = _device.begin(); iter != _device.end(); iter++)
        {
            auto &device = iter->second;
            policy       = &device._policy;
            if (!device._buff.empty())
            {
                _writing_buffers.assign(device._buff.begin(), device._buff.end());

                device._time = now;
                device._buff.clear();
                break;
            }

            int64_t sec = now - device._time;
            if (sec > 5 * 60)
            {
                if (Policy::PT_DAILY == policy->get_type())
                {
                    policy->close_stream();
                }
                else
                {
                    iter = _device.erase(iter);
                }
            }
            else if (sec > 10)
            {
                // 当没有日志写入时，10秒检测一次日期切换
                policy->trigger_daily_rollover(now);
            }
        }

        if (!policy) break;

        unlock();
        write_device(policy, _writing_buffers);
        lock();

        // 回收缓冲区
        for (auto buffer : _writing_buffers) _buffer_pool.destroy(buffer);
        _writing_buffers.clear();
    }
    unlock();
}
