-- mongodb_sync.lua
-- 2018-02-28
--xzc

-- 用coroutine来封装一套接近同步操作的数据库接口

-- 为了能判断coroutine是否出错，又要能够返回可变参数，这里
-- 要wrap一层，把返回值变成...参数
local function after_coroutine_resume( co,ok,args,... )
    if ok then return args,... end

    __G__TRACKBACK( args,co )
    return false
end

local function after_coroutine_start( co,ok,args,... )
    if ok then return ok,args,... end

    __G__TRACKBACK( args,co )
    return false
end

local MongodbSync = oo.class( ... )

function MongodbSync:__init( mongodb,co )
    self.co = co
    self.mongodb = mongodb
    self.callback = function( ecode,res )
        return after_coroutine_resume( co,coroutine.resume( co,ecode,res ) )
    end
end

function MongodbSync:start( ... )
    return after_coroutine_start( self.co,coroutine.resume( self.co,... ) )
end

-- 是否有效
function MongodbSync:valid()
    return "dead" ~= coroutine.status( self.co )
end

-- 这些数据库操作接口同mongodb.lua中的一样

function MongodbSync:count( collection,query,opts )
    self.mongodb:count( collection,query,opts,self.callback )

    return coroutine.yield()
end

function MongodbSync:find( collection,query,opts )
    self.mongodb:find( collection,query,opts,self.callback )

    return coroutine.yield()
end

return MongodbSync
