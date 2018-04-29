-- log_mgr.lua
-- 2018-04-24
-- xzc

-- 日志管理

local Log = require "Log"
require "modules.log.log_header"

local Log_mgr = oo.singleton( nil,... )

-- 初始化
function Log_mgr:__init()
    -- 启动文件日志线程
    local fl_logger = Log()
    fl_logger:start( 3,0 ); -- 3秒写入一次

    self.fl_logger = fl_logger
end

-- 初始化db日志
function Log_mgr:db_logger_init()
    local callback = function()
        self:on_db_logger_init()
    end

    self.db_logger = g_mysql_mgr:new()
    self.db_logger:start( g_setting.mysql_ip,g_setting.mysql_port,
    g_setting.mysql_user,g_setting.mysql_pwd,g_setting.mysql_db,callback )
end

-- db日志初始化完成
function Log_mgr:on_db_logger_init()
    g_app:one_initialized( "db_logger",1 )
end

-- 关闭文件日志线程及数据库日志线程
function Log_mgr:close()
    self.fl_logger:stop()
    -- db则由g_mysql_mgr去管理
end

-- 记录登录、退出日志
local login_out_stmt = "INSERT INTO `login_logout` (pid,op_type,op_time) values (%d,%d,%d)"
function Log_mgr:login_or_logout( pid,op_type )
    self.db_logger:insert( string.format(login_out_stmt,pid,op_type,ev:time()) )
end

local log_mgr = Log_mgr()
return log_mgr
