require "global.global"
require "global.oo"
require "global.table"
require "global.string"

Main = {}       -- store dynamic runtime info to global
Main.command,Main.srvname,Main.srvindex,Main.srvid = ...

Main.wait = 
{
    gateway = 1,
}

local Unique_id = require "global.unique_id"

unique_id = Unique_id()
Main.session = unique_id:srv_session(
    Main.srvname,tonumber(Main.srvindex),tonumber(Main.srvid) )

setting     = require "world/setting"
network_mgr = require "network/network_mgr"
command_mgr = require "command/command_mgr"

local Srv_conn    = require "network/srv_conn"

function Main.sig_handler( signum )
    if g_store_mongo then g_store_mongo:stop() end
    if   g_store_sql then   g_store_sql:stop() end
    if     g_log_mgr then     g_log_mgr:stop() end

    ev:exit()
end

-- 检查需要等待的服务器是否初始化完成
function Main.one_wait_finish( name,val )
    if not Main.wait[name] then return end

    Main.wait[name] = Main.wait[name] - val
    if Main.wait[name] <= 0 then Main.wait[name] = nil end

    if table.empty( Main.wait ) then Main.final_init() end
end

function Main.init()
    Main.starttime = ev:time()

    command_mgr:init_command()
    local fs = command_mgr:load_schema()
    PLOG( "world load flatbuffers schema:%d",fs )

    if not network_mgr:srv_listen( setting.sip,setting.sport ) then
        ELOG( "world server listen fail,exit" )
        os.exit( 1 )
    end

    network_mgr:connect_srv( setting.servers )
end

function Main.final_init()
    Main.ok = true
    PLOG( "world server start OK" )
end

local function main()
    ev:set_signal_ref( Main.sig_handler )
    ev:signal( 2  )
    ev:signal( 15 )

    Main.init()

    ev:backend()
end

xpcall( main, __G__TRACKBACK__ )
