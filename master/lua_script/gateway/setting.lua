-- gateway 样本
-- 任何配置都要写到配置样本，并上传版本管理(svn、git...)
-- 各个服的配置从样本修改而来，但不要上传。避免配置被覆盖

return
{
    sip   = "127.0.0.1", -- s2s监听ip
    sport = 40025,       -- s2s监听端口
}
