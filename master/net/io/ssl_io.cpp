#include <openssl/ssl.h>
#include <openssl/err.h>

#include "ssl_io.h"
#include "ssl_mgr.h"

#define X_SSL(x) static_cast<SSL *>( x )
#define X_SSL_CTX(x) static_cast<SSL_CTX *>( x )

ssl_io::~ssl_io()
{
}

ssl_io::ssl_io( int32 ctx_idx,class buffer *recv,class buffer *send )
    : io( recv,send )
{
    _handshake = false;
    _ssl_ctx = NULL;
    _ctx_idx = ctx_idx;
}

/* 接收数据
 * * 返回: < 0 错误，0 成功，1 需要重读，2 需要重写
 */
int32 ssl_io::recv()
{
    assert( "io recv fd invalid",_fd > 0 );

    if ( !_handshake ) return do_handshake();

    if ( !_recv->reserved() ) return -1; /* no more memory */

    uint32 size = _recv->buff_size();
    int32 len = SSL_read( X_SSL( _ssl_ctx ),_recv->buff_pointer(),size );
    if ( expect_true(len > 0) )
    {
        _recv->increase( len );
        return 0;
    }

    int32 ecode = SSL_get_error( X_SSL( _ssl_ctx ),len );
    if ( SSL_ERROR_WANT_READ == ecode ) return 1;

    // 非主动断开，打印错误日志
    if ( SSL_ERROR_ZERO_RETURN != ecode )
    {
        ERROR( "ssl io recv:%s",ERR_error_string(ecode,NULL) );
    }

    return -1;
}

/* 发送数据
 * * 返回: < 0 错误，0 成功，1 需要重读，2 需要重写
 */
int32 ssl_io::send()
{
    assert( "io send fd invalid",_fd > 0 );

    if ( !_handshake ) return do_handshake();

    size_t bytes = _send->data_size();
    assert( "io send without data",bytes > 0 );
    int32 len = SSL_write( X_SSL( _ssl_ctx ),_send->data_pointer(),bytes );
    if ( expect_true(len > 0) )
    {
        _send->subtract( len );
        return ((size_t)len) == bytes ? 0 : 2;
    }

    int32 ecode = SSL_get_error( X_SSL( _ssl_ctx ),len );
    if ( SSL_ERROR_WANT_WRITE == ecode ) return 2;

    // 非主动断开，打印错误日志
    if ( SSL_ERROR_ZERO_RETURN != ecode )
    {
        ERROR( "ssl io send:%s",ERR_error_string(ecode,NULL) );
    }

    return -1;
}

/* 准备接受状态
 */
int32 ssl_io::init_accept( int32 fd )
{
    if ( init_ssl_ctx( fd ) < 0 ) return -1;

    _fd = fd;
    SSL_set_accept_state( X_SSL( _ssl_ctx ) );

    return do_handshake();
}

/* 准备连接状态
 */
int32 ssl_io::init_connect( int32 fd )
{
    if ( init_ssl_ctx( fd ) < 0 ) return -1;

    _fd = fd;
    SSL_set_connect_state( X_SSL( _ssl_ctx ) );

    return do_handshake();
}

int32 ssl_io::init_ssl_ctx( int32 fd )
{
    static class ssl_mgr *ctx_mgr = ssl_mgr::instance();

    void *base_ctx = ctx_mgr->get_ssl_ctx( _ctx_idx );
    if ( !base_ctx )
    {
        ERROR( "ssl io init ssl ctx no base ctx found" );
        return -1;
    }

    _ssl_ctx = SSL_new( X_SSL_CTX( base_ctx ) );
    if ( !_ssl_ctx )
    {
        ERROR( "ssl io init ssl SSL_new fail" );
        return -1;
    }

    if ( !SSL_set_fd( X_SSL( _ssl_ctx ),fd ) )
    {
        ERROR( "ssl io init ssl SSL_set_fd fail" );
        return -1;
    }

    return 0;
}

// 返回: < 0 错误，0 成功，1 需要重读，2 需要重写
int32 ssl_io::do_handshake()
{
    int32 ecode = SSL_do_handshake( X_SSL( _ssl_ctx ) );
    if ( 1 == ecode )
    {
        _handshake = true;
        // 可能上层在握手期间发送了一些数据，握手成功要检查一下
        return _send->data_size() > 0 ? 2 : 0;
    }

    /* Caveat: Any TLS/SSL I/O function can lead to either of 
     * SSL_ERROR_WANT_READ and SSL_ERROR_WANT_WRITE. In particular, SSL_read() 
     * or SSL_peek() may want to write data and SSL_write() may want to read 
     * data. This is mainly because TLS/SSL handshakes may occur at any time 
     * during the protocol (initiated by either the client or the server); 
     * SSL_read(), SSL_peek(), and SSL_write() will handle any pending 
     * handshakes.
     */
    ecode = SSL_get_error( X_SSL( _ssl_ctx ),ecode );
    if (  SSL_ERROR_WANT_READ == ecode ) return 1;
    if ( SSL_ERROR_WANT_WRITE == ecode ) return 2;

    // error
    ERROR( "ssl io do handshake "
        "error.system:%s,ssl:%s",strerror(errno),ERR_error_string(ecode,NULL) );

    return -1;
}
