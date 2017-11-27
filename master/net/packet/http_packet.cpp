#include <http_parser.h>

#include "../socket.h"
#include "http_packet.h"
#include "../../lua_cpplib/ltools.h"
#include "../../lua_cpplib/lstate.h"

// 开始解析报文，第一个回调的函数，在这里初始化数据
int32 on_message_begin( http_parser *parser )
{
    assert( "on_url no parser",parser && (parser->data) );

    class http_packet * http_packet = 
        static_cast<class http_packet *>(parser->data);
    http_packet->reset();

    return 0;
}

// 解析到url报文，可能只是一部分
int32 on_url( http_parser *parser, const char *at, size_t length )
{
    assert( "on_url no parser",parser && (parser->data) );

    class http_packet * http_packet = 
        static_cast<class http_packet *>(parser->data);
    http_packet->append_url( at,length );

    return 0;
}

int32 on_status( http_parser *parser, const char *at, size_t length )
{
    UNUSED( parser );
    UNUSED( at );
    UNUSED( length );
    // parser->status_code 本身有缓存，这里不再缓存
    return 0;
}

int32 on_header_field( http_parser *parser, const char *at, size_t length )
{
    assert( "on_header_field no parser",parser && (parser->data) );

    class http_packet * http_packet = 
        static_cast<class http_packet *>(parser->data);
    http_packet->append_cur_field( at,length );

    return 0;
}

int32 on_header_value( http_parser *parser, const char *at, size_t length )
{
    assert( "on_header_value no parser",parser && (parser->data) );

    class http_packet * http_packet = 
        static_cast<class http_packet *>(parser->data);
    http_packet->append_cur_value( at,length );

    return 0;
}

int32 on_headers_complete( http_parser *parser )
{
    assert( "on_header_value no parser",parser && (parser->data) );

    class http_packet * http_packet = 
        static_cast<class http_packet *>(parser->data);
    http_packet->on_headers_complete();

    return 0;
}

int32 on_body( http_parser *parser, const char *at, size_t length )
{
    assert( "on_body no parser",parser && (parser->data) );

    class http_packet * http_packet = 
        static_cast<class http_packet *>(parser->data);
    http_packet->append_body( at,length );

    return 0;
}

int32 on_message_complete( http_parser *parser )
{
    assert( "on_message_complete no parser",parser && (parser->data) );

    class http_packet * http_packet = 
        static_cast<class http_packet *>(parser->data);
    http_packet->on_message_complete();

    return 0;
}

/* http chunk应该用不到，暂不处理 */
static const struct http_parser_settings settings = 
{
    on_message_begin,
    on_url,
    on_status,
    on_header_field,
    on_header_value,
    on_headers_complete,
    on_body,
    on_message_complete,

    NULL,NULL
};

/* ====================== HTTP FUNCTION END ================================ */
http_packet::~http_packet()
{
    delete _parser;
    _parser = NULL;
}

http_packet::http_packet( class socket *sk ) : packet( sk )
{
    //HTTP_REQUEST, HTTP_RESPONSE, HTTP_BOTH
    _parser = new struct http_parser();
    http_parser_init( _parser,HTTP_BOTH );
    _parser->data = this;
}

int32 http_packet::pack()
{
    return 0;
}

int32 http_packet::unpack()
{
    class buffer &recv = _socket->recv_buffer();
    uint32 size = recv.data_size();
    if ( size == 0 ) return 0;

    int32 nparsed = 
        http_parser_execute( _parser,&settings,recv.data(),size );

    recv.clear(); // http_parser不需要旧缓冲区

    /* web_socket报文,暂时不用回调到上层
     * The user is expected to check if parser->upgrade has been set to 1 after 
     * http_parser_execute() returns. Non-HTTP data begins at the buffer 
     * supplied offset by the return value of http_parser_execute()
     */
    if ( _parser->upgrade )
    {
        return 0;
    }
    else if ( nparsed != (int32)size )  /* error */
    {
        int32 no = _parser->http_errno;
        ERROR( "http socket parse error(%d):%s",
            no,http_errno_name(static_cast<enum http_errno>(no)) );

        return -1;
    }

    return 0;
}

void http_packet::reset()
{
    _cur_field.clear();
    _cur_value.clear();

    _http_info._url.clear();
    _http_info._body.clear();
    _http_info._head_field.clear();
}

void http_packet::on_headers_complete()
{
    if ( _cur_field.empty() ) return;

    _http_info._head_field[_cur_field] = _cur_value;
}

void http_packet::on_message_complete()
{
    static lua_State *L = lstate::instance()->state();
    assert( "lua stack dirty",0 == lua_gettop(L) );

    lua_pushcfunction( L,traceback );
    lua_getglobal    ( L,"http_command_new" );
    lua_pushinteger  ( L,_socket->conn_id() );
    lua_pushstring   ( L,_http_info._url.c_str()  );
    lua_pushstring   ( L,_http_info._body.c_str() );

    if ( expect_false( LUA_OK != lua_pcall( L,3,0,1 ) ) )
    {
        ERROR( "http_command_new:%s",lua_tostring( L,-1 ) );

        lua_pop( L,2 ); /* remove traceback and error object */
        return;
    }
    lua_pop( L,1 ); /* remove traceback */
}

void http_packet::append_url( const char *at,size_t len )
{
    _http_info._url.append( at,len );
}

void http_packet::append_body( const char *at,size_t len )
{
    _http_info._body.append( at,len );
}

void http_packet::append_cur_field( const char *at,size_t len )
{
    /* 报文中的field和value是成对的，但是http-parser解析完一对字段后并没有回调任何函数
     * 如果检测到value不为空，则说明当前收到的是新字段
     */
    if ( !_cur_value.empty() )
    {
        _http_info._head_field[_cur_field] = _cur_value;

        _cur_field.clear();
        _cur_value.clear();
    }

    _cur_field.append( at,len );
}

void http_packet::append_cur_value( const char *at,size_t len )
{
    _cur_value.append( at,len );
}
