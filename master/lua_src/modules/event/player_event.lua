-- 玩家事件总线

require "modules.event.event_header"

local Player_event = oo.singleton( nil,... )

function Player_event:__init()
    self.event = {}
end

function Player_event:register( event_id,handler )
    if not self.event[event_id] then self.event[event_id] = {} end

    -- 有可能是热更新时重新注册
    for _,hdl in pairs( self.event[event_id] ) do
        if hdl == handler then return end
    end

    table.insert( self.event[event_id],handler )
end

function Player_event:fire_event( event_id,... )
    if not self.event[event_id] then return end

    for _,hdl in pairs( self.event[event_id] ) do hdl( ... ) end
end

local event = Player_event()

return event