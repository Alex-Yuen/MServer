-- http_performance.lua
-- 2016-04-19
-- xzc

local network_mgr = network_mgr

local IP = "0.0.0.0"
local PORT = 8887

local url_tbl =
{
    'GET / HTTP/1.1\r\n',
    'Host: www.baidu.com\r\n',
    'Connection: keep-alive\r\n',
    'Accept: text/html,application/xhtml+xml,application/xml;q=0.9,*/*;q=0.8\r\n',
    'Upgrade-Insecure-Requests: 1\r\n',
    --'User-Agent: Mozilla/4.0 (compatible; MSIE 6.0; Windows NT 5.1)'
    --'User-Agent: Mozilla/5.0 (Windows NT 6.1; WOW64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/49.0.2623.112 Safari/537.36\r\n',
    --'Accept-Encoding: gzip, deflate, sdch\r\n',
    'Accept-Language: zh-CN,zh;q=0.8\r\n\r\n',
}

local url_page = table.concat( url_tbl )


function http_accept_new( conn_id )
    network_mgr:set_conn_io( conn_id,network_mgr.IOT_NONE )
    network_mgr:set_conn_codec( conn_id,network_mgr.CDC_NONE )
    network_mgr:set_conn_packet( conn_id,network_mgr.PKT_HTTP )

    print( "http_accept_new",conn_id )
end

function http_connect_new( conn_id )
    network_mgr:set_conn_io( conn_id,network_mgr.IOT_NONE )
    network_mgr:set_conn_codec( conn_id,network_mgr.CDC_NONE )
    network_mgr:set_conn_packet( conn_id,network_mgr.PKT_HTTP )

    print( "http_connect_new",conn_id )

    network_mgr:send_raw_packet( conn_id,url_page )
end

function http_connect_del( conn_id )
    print( "http_connect_del",conn_id )
end

-- http回调
function http_command_new( conn_id,url,body )
    print( "http_command_new",conn_id,url,body )
end

local http_listen = network_mgr:listen( IP,PORT,network_mgr.CNT_HTTP )
PLOG( "http listen at %s:%d",IP,PORT )

-- 阻塞获取，考虑到服务器起服后连后台域名解析并不常用，不提供多线程
local url = "www.baidu.com"
local ip1,ip2 = util.gethostbyname( url )

--[[
1.部分网站是需要User-Agent才会返回数据的，不同的User-Agent导致返回不同的数据
2.有时候收到301、302返回的，需要从头部取出Location进行跳转。如www.bing.com是不存在的，
  应该是cn.bing.com才有返回
3.很多网站收到的是二进制压缩包，chrome会进行解压的，但这里不行。这时get_body通常都拿不
  到数据，但底层是有收到数据的，头部有Content-Encoding: gzip、
  Transfer-Encoding: chunked字段，部分网站通过设置Accept-Encoding可解决。
  cn.bing.com、www.163.com、www.oschina.net都是返回二进制，但www.baidu.com不压缩
]]
-- connect是不支持域名解析的，只能写ip
local url_conn_id = network_mgr:connect( ip1,80,network_mgr.CNT_HTTP )
print( "connnect to http ",url,ip1,url_conn_id )
