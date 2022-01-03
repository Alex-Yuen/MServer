#pragma once

/**
 * @brief ����ִ��io�����ĺ�̨��
 */
class EVBackend
{
public:
    /// �������io�¼�
    class ModifyEvent final
    {
    public:
        ModifyEvent(int32_t fd, int32_t old_ev, int32_t new_ev)
        {
            _fd = fd;
            _old_ev = old_ev;
            _new_ev = new_ev;
        }
    public:
        int32_t _fd;
        int32_t _old_ev;
        int32_t _new_ev;
    };

public:
    EVBackend()
    {
        _done = false;
        _ev   = nullptr;
    }
    virtual ~EVBackend(){};

    /// �����ӽ���
    virtual void wake() = 0;

    /// ����backend�߳�
    virtual bool start(class EV *ev)
    {
        _ev = ev;
        return true;
    }

    /// ��ֹbackend�߳�
    virtual bool stop()
    {
        _done = true;

        wake();
        return true;
    }

    /// �޸�io�¼�(����ɾ��)
    virtual void modify(int32_t fd, int32_t old_ev, int32_t new_ev) = 0;

protected:
    bool _done;    /// �Ƿ���ֹ����
    class EV *_ev; /// ��ѭ��
};