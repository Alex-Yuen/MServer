#include <lflatbuffers.hpp>

#include "lstream_socket.h"

#include "ltools.h"
#include "../net/packet.h"
#include "../ev/ev_def.h"

lstream_socket::lstream_socket( lua_State *L )
    : lsocket(L)
{

}

lstream_socket::~lstream_socket()
{
}

/* check if a message packet complete
 * all packet should start with a packet length(uint16)
 */
int32 lstream_socket::is_message_complete()
{
    uint32 sz = _recv.data_size();
    if ( sz < sizeof(packet_length) ) return 0;
    return sz >= *(reinterpret_cast<packet_length *>(_recv.data())) ? 1 : 0;
}

const class lsocket *lstream_socket::accept_new( int32 fd )
{
    class lstream_socket *_s = new class lstream_socket( L );

    _s->socket::set<lsocket,&lsocket::message_cb>( _s );
    _s->socket::start( fd,EV_READ );  /* 这里会设置fd */

    return static_cast<class lsocket *>( _s );
}

/* get next server message */
int lstream_socket::srv_next()
{
    uint32 sz = _recv.data_size();
    if ( sz < sizeof(struct s2s_header) ) return 0;

    struct s2s_header *header =
        reinterpret_cast<struct s2s_header *>( _recv.data() );

    if ( sz < header->_length ) return 0;

    lua_pushinteger( L,header->_cmd );
    return 1;
}

/* get next client message */
int lstream_socket::clt_next()
{
    uint32 sz = _recv.data_size();
    if ( sz < sizeof(struct c2s_header) ) return 0;

    struct c2s_header *header =
        reinterpret_cast<struct c2s_header *>( _recv.data() );

    if ( sz < header->_length ) return 0;

    lua_pushinteger( L,header->_cmd );
    return 1;
}

/* ssc_flatbuffers_send( lfb,srv_msg,clt_msg,schema,object,tbl ) */
int lstream_socket::ssc_flatbuffers_send()
{
    class lflatbuffers** lfb =
        (class lflatbuffers**)luaL_checkudata( L, 1, "lua_flatbuffers" );
    if ( lfb == NULL || *lfb == NULL )
    {
        return luaL_error( L, "argument #1 expect lua_flatbuffers" );
    }

    int32 srv_msg = luaL_checkinteger( L,2 );
    int32 clt_msg = luaL_checkinteger( L,3 );

    const char *schema = luaL_checkstring( L,4 );
    const char *object = luaL_checkstring( L,5 );

    if ( !lua_istable( L,6 ) )
    {
        return luaL_error( L,
            "argument #6 expect table,got %s",lua_typename( L,lua_type(L,6) ) );
    }

    if ( (*lfb)->encode( L,schema,object,6 ) < 0 )
    {
        return luaL_error( L,(*lfb)->last_error() );
    }

    size_t sz = 0;
    const char *buffer = (*lfb)->get_buffer( sz );
    if ( sz > USHRT_MAX )
    {
        return luaL_error( L,"buffer size over USHRT_MAX" );
    }

    if ( !_send.reserved( sz + sizeof(struct s2c_header) + sizeof(struct s2s_header) ) )
    {
        return luaL_error( L,"out of socket buffer" );
    }

    struct s2c_header s2ch;
    s2ch._length = static_cast<packet_length>(
        sz + sizeof(struct s2c_header) - sizeof(packet_length) );
    s2ch._cmd    = static_cast<uint16>  ( clt_msg );

    struct s2s_header s2sh;
    s2sh._length = static_cast<packet_length>( sz +
        sizeof(struct s2c_header) + sizeof(struct s2s_header) - sizeof(packet_length) );
    s2sh._cmd    = static_cast<uint16>  ( srv_msg );

    _send.__append( &s2sh,sizeof(struct s2s_header) );
    _send.__append( &s2ch,sizeof(struct s2c_header) );
    _send.__append( buffer,sz );

    pending_send();
    return 0;
}


/* sc_flatbuffers_send( lfb,clt_msg,schema,object,tbl ) */
int lstream_socket::sc_flatbuffers_send()
{
    class lflatbuffers** lfb =
        (class lflatbuffers**)luaL_checkudata( L, 1, "lua_flatbuffers" );
    if ( lfb == NULL || *lfb == NULL )
    {
        return luaL_error( L, "argument #1 expect lua_flatbuffers" );
    }

    int32 clt_msg = luaL_checkinteger( L,2 );

    const char *schema = luaL_checkstring( L,3 );
    const char *object = luaL_checkstring( L,4 );

    if ( !lua_istable( L,5 ) )
    {
        return luaL_error( L,
            "argument #5 expect table,got %s",lua_typename( L,lua_type(L,5) ) );
    }

    if ( (*lfb)->encode( L,schema,object,6 ) < 0 )
    {
        return luaL_error( L,(*lfb)->last_error() );
    }

    size_t sz = 0;
    const char *buffer = (*lfb)->get_buffer( sz );
    if ( sz > USHRT_MAX )
    {
        return luaL_error( L,"buffer size over USHRT_MAX" );
    }

    if ( !_send.reserved( sz + sizeof(struct s2c_header) + sizeof(struct s2s_header) ) )
    {
        return luaL_error( L,"out of socket buffer" );
    }

    struct s2c_header s2ch;
    s2ch._length = static_cast<packet_length>(
        sz + sizeof(struct s2c_header) - sizeof(packet_length) );
    s2ch._cmd    = static_cast<uint16>  ( clt_msg );

    _send.__append( &s2ch,sizeof(struct s2c_header) );
    _send.__append( buffer,sz );

    pending_send();
    return 0;
}

 /* server to server:ss_flatbuffers_send( lfb,msg,schema,object,tbl ) */
int lstream_socket::ss_flatbuffers_send()
{
    class lflatbuffers** lfb =
        (class lflatbuffers**)luaL_checkudata( L, 1, "lua_flatbuffers" );
    if ( lfb == NULL || *lfb == NULL )
    {
        return luaL_error( L, "argument #1 expect lua_flatbuffers" );
    }

    int32 srv_msg = luaL_checkinteger( L,2 );

    const char *schema = luaL_checkstring( L,3 );
    const char *object = luaL_checkstring( L,4 );

    if ( !lua_istable( L,5 ) )
    {
        return luaL_error( L,
            "argument #5 expect table,got %s",lua_typename( L,lua_type(L,5) ) );
    }

    if ( (*lfb)->encode( L,schema,object,5 ) < 0 )
    {
        return luaL_error( L,(*lfb)->last_error() );
    }

    size_t sz = 0;
    const char *buffer = (*lfb)->get_buffer( sz );
    if ( sz > USHRT_MAX )
    {
        return luaL_error( L,"buffer size over USHRT_MAX" );
    }

    if ( !_send.reserved( sz + sizeof(struct s2s_header) ) )
    {
        return luaL_error( L,"out of socket buffer" );
    }

    struct s2s_header s2sh;
    s2sh._length = static_cast<packet_length>(
        sz + sizeof(struct s2s_header) - sizeof(packet_length) );
    s2sh._cmd    = static_cast<uint16>  ( srv_msg );

    _send.__append( &s2sh,sizeof(struct s2s_header) );
    _send.__append( buffer,sz );

    pending_send();
    return 0;
}

/* decode server to server message:ss_flatbuffers_decode( lfb,srv_msg,schema,object ) */
int lstream_socket::ss_flatbuffers_decode()
{
    class lflatbuffers** lfb =
        (class lflatbuffers**)luaL_checkudata( L, 1, "lua_flatbuffers" );
    if ( lfb == NULL || *lfb == NULL )
    {
        return luaL_error( L, "argument #1 expect lua_flatbuffers" );
    }

    int32 srv_msg = luaL_checkinteger( L,2 );

    const char *schema = luaL_checkstring( L,3 );
    const char *object = luaL_checkstring( L,4 );

    uint32 sz = _recv.data_size();
    if ( sz < sizeof(s2s_header) )
    {
        return luaL_error( L, "incomplete message header" );
    }

    struct s2s_header *ph = reinterpret_cast<struct s2s_header *>(_recv.data());

    /* 验证包长度，_length并不包含本身 */
    size_t len = ph->_length + sizeof( packet_length );
    if ( sz < len )
    {
        return luaL_error( L, "incomplete message header" );
    }

    /* 协议号是否匹配 */
    if ( srv_msg != ph->_cmd )
    {
        return luaL_error( L,
            "message valid fail,expect %d,got %d",srv_msg,ph->_cmd );
    }

    /* 删除buffer,避免luaL_error longjump影响 */
    _recv.subtract( len );
    const char *buffer = _recv.data() + sizeof( struct s2s_header );

    if ( (*lfb)->decode( L,schema,object,buffer,len ) < 0 )
    {
        return luaL_error( L,(*lfb)->last_error() );
    }

    return 1;
}
