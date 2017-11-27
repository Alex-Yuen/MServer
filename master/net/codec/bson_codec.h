#ifndef __BSON_CODEC_H__
#define __BSON_CODEC_H__

#include "codec.h"

struct x_bson_t;
class bson_codec : public codec
{
public:
    bson_codec();
    ~bson_codec();

    void finalize();

    /* 解码数据包
     * return: <0 error,otherwise the number of parameter push to stack
     */
    int32 decode(
         lua_State *L,const char *buffer,int32 len,const cmd_cfg_t *cfg );
    /* 编码数据包
     * return: <0 error,otherwise the length of buffer
     */
    int32 encode(
        lua_State *L,int32 index,const char **buffer,const cmd_cfg_t *cfg );
private:
    struct x_bson_t *_bson_doc;
};

#endif /* __BSON_CODEC_H__ */
