#define SOL_H_IMPL
#include "sol.h"

int main() {

    u8 *buf = malloc(10000);

    u16 *buf16 = (u16*)(buf);
    for(u16 i = 0; i < 50; ++i)
        buf16[i] = i;


    float mat[4][4];
    for(u32 i = 0; i < 4; ++i)
        for(u32 j = 0; j < 4; ++j)
            mat[i][j] = (float)(i * j);

    float *buf_mat = (float*)(buf + 128);
    for(u32 i = 0; i < 32; ++i)
        memcpy(buf_mat + i * 16, mat, 4 * 16);

    float *buf_vec3 = (float*)(buf + 2176);
    float vec[3];
    for(u32 i = 0; i < 3; ++i)
        vec[i] = i + 1;

    for(u32 i = 0; i < 32; ++i)
        memcpy(buf_vec3 + i * 3, vec, 4 * 3);
    
    file_write_bytes("buf.bin", buf, 10000);

    u64 size;
    u8 *file = file_read_bytes("buf.bin", &size);

    assert(memcmp(buf16,    file +   0,  100) == 0);
    assert(memcmp(buf_mat,  file + 128, 2076) == 0);
    assert(memcmp(buf_vec3, file + 2176, 384) == 0);

    printf("%f", *(float*)(file + 2176 + 0));
    printf("%f", *(float*)(file + 2176 + 4));
    printf("%f", *(float*)(file + 2176 + 8));

    return 0;
}
