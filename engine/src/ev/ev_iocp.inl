const char *__BACKEND__ = "IOCP";

#include <thread>

/// backend using iocp implement
class FinalBackend final : public EVBackend
{
public:
    FinalBackend();
    ~FinalBackend();

    bool stop();
    bool start(class EV *ev);
    void wake();
    void backend();
    void modify(int32_t fd, EVIO *w);

private:
    /**
     * @brief iocp���߳�������
    */
    void iocp_routine();

private:
    HANDLE _h_iocp; // global iocp handle
    std::vector<std::thread> _threads;
};

 FinalBackend::FinalBackend()
{
    _h_iocp = 0;
}

FinalBackend::~FinalBackend()
{
}

bool FinalBackend::stop()
{
    if (!_h_iocp) return true;

    // ֪ͨ����iocp���߳��˳�
    for (size_t i = 0; i < _threads.size();i ++)
    {
        if (0 ==PostQueuedCompletionStatus(_h_iocp, 0, 0, nullptr))
        {
            ELOG("stop iocp thread fail: " FMT64u, GetLastError());
        }
    }

    // �ȴ����߳��˳�
    for (auto &thd : _threads)
    {
        thd.join();
    }

    // �ر�iocp���
    CloseHandle(_h_iocp);

    return true;
}

bool FinalBackend::start(class EV *ev)
{
    /**
     * ��Ϸ������ioѹ�����󣬲���Ҫʹ��̫����߳�
     * 
     * https://stackoverflow.com/questions/38133870/how-the-parameter-numberofconcurrentthreads-is-used-in-createiocompletionport
     * https://msdn.microsoft.com/en-us/library/windows/desktop/aa365198.aspx
     * 
     * @param thread_num
     * If this parameter is zero, the system allows as many concurrently running threads as there are processors in the system
     * �������ָ����������Ҫ����ʱ��iocp������߳����������Ǵ������߳���
     * ���������ֵΪ1�����洴����2���̵߳���GetQueuedCompletionStatus��Ҳֻ��
     * ����1�̹߳���
     */

    /*
    SYSTEM_INFO sys_info;
    GetSystemInfo(&sys_info);
    const DWORD thread_num = sys_info.dwNumberOfProcessors * 2;
    */
    const DWORD thread_num = 2;
    _h_iocp = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, thread_num);
    if (!_h_iocp)
    {
        ELOG("iocp start fail: " FMT64u, GetLastError());
        return false;
    }

    for (DWORD i = 0; i < thread_num; ++i)
    {
        _threads.emplace_back(&FinalBackend::iocp_routine, this);
    }

    return true;
}

void FinalBackend::iocp_routine()
{
    const DWORD ms = 2000; // ms = INFINITE;

    while (true)
    {
        OVERLAPPED *overlapped = nullptr;
        ULONG_PTR key          = 0;
        DWORD bytes            = 0;
        int ok = GetQueuedCompletionStatus(_h_iocp, &bytes, &key, &overlapped, ms);

        /**
         * https://docs.microsoft.com/en-us/windows/win32/api/ioapiset/nf-ioapiset-getqueuedcompletionstatus
         * 
         * 1. If a call to GetQueuedCompletionStatus fails because the completion port handle associated with it is closed while the call is outstanding, the function returns FALSE, *lpOverlapped will be NULL, and GetLastError will return ERROR_ABANDONED_WAIT_0
         */

        // 1. _h_iocp�ر�
        // 2. ��д�¼�
        // ��ӡ��Ƴ�д�¼�
        // socket�رջ��߳���

        // �ر�ʱ��stop������PostQueuedCompletionStatus��keyֵ0
        // ����_h_iocpǿ�ƹرգ�If a call to GetQueuedCompletionStatus fails because the completion port handle associated with it is closed while the call is outstanding, the function returns FALSE, *lpOverlapped will be NULL, and GetLastError will return ERROR_ABANDONED_WAIT_0
        if (0 == key || !overlapped) break;


    }
}

void FinalBackend::wake()
{
}
void FinalBackend::backend()
{
}

void FinalBackend::modify(int32_t fd, EVIO *w)
{
}