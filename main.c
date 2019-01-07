#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>

typedef unsigned int u32;
typedef unsigned char u8;

//data
typedef struct
{
    u32 size;
    u8* data;
} data_t;

//crc hash function
u32 crc32(data_t data)
{
    //initialize crc table
    static bool init = false;
    static u32  crc_table[256];
    if(!init)
    {
        u32 c;
        for(u32 i = 0; i < 256; i++)
        {
            c = i;
            for(u32 j = 0; j < 8; j++)
            {
                c = ((c & 1) ? (0xEDB88320 ^ (c >> 1)) : (c >> 1));
            }
            crc_table[i] = c;
        }
        
        init = true;
    }
    
    //feed crc alorithm
    u32 crc = 0xFFFFFFFF;
    for(u32 i = 0; i < data.size; i++)
    {
        crc = (crc >> 8) ^ crc_table[(crc ^ data.data[i]) & 0xFF];
    }
    return (crc ^ 0xFFFFFFFF);
}

//decode patch block
u32 decode(u8* patch, u32* patch_pos)
{
    u32 dt  = 0;
    u32 sh  = 0;
    u8 next = 0;
    while(true)
    {
        next = patch[(*patch_pos)++];
        dt  += (next ^ 0x80) << sh;
        if(next & 0x80) { return dt; }
        sh  += 7;
    }
}

//decode patch special block
u32 decode_special(u8* patch, u32* patch_pos)
{
    u32 enc = decode(patch, patch_pos);
    u32 ret = enc >> 1;
    if(enc & 1) { ret = -ret; }
    return ret;
}

//apply bps patch
void apply_bps(data_t rom, data_t patch, u8* output_file)
{
    u32 patch_pos = 0;
    
#define RB()        patch.data[patch_pos++]
#define RW(pos)    (patch.data[pos + 0] << 0  | \
                    patch.data[pos + 1] << 8  | \
                    patch.data[pos + 2] << 16 | \
                    patch.data[pos + 3] << 24)
    
    //check bps header
    if(RB() != 0x42 || RB() != 0x50 || RB() != 0x53 || RB() != 0x31)
    {
        printf("apply_bps() error: not a BPS patch\n");
        return;
    }
    
    //check patch rom size
    if(decode(patch.data, &patch_pos) != rom.size)
    {
        printf("apply_bps() error: wrong input file [decoding]\n");
        return;
    }
    
    //check patch rom crc
    if(crc32(rom) != RW(patch.size - 12))
    {
        printf("appy_bps() error: wrong input file [crc]\n");
        return;
    }
    
    //create output rom buffer
    data_t patched_rom =
    {
        .size = decode(patch.data, &patch_pos),
    }; patched_rom.data = malloc(patched_rom.size);

    u32 patched_rom_pos = 0;
    
    u32 meta_len = decode(patch.data, &patch_pos);
    patch_pos   += meta_len;
    
    u32 in_read_pos  = 0;
    u32 out_read_pos = 0;
    

    enum BPS_INS { SRC_READ, TGT_READ, SRC_COPY, TGT_COPY };
    
    //patch rom
    while(patch_pos < patch.size - 12)
    {
        u32 ins    = decode(patch.data, &patch_pos);
        u32 len    = (ins >> 2) + 1;
        u32 action = (ins & 3);
        
        switch(action)
        {
            case SRC_READ:
            {
                for(u32 i = 0; i < len; i++)
                {
                    patched_rom.data[patched_rom_pos] = rom.data[patched_rom_pos];
                    patched_rom_pos++;
                }
                break;
            }
                
            case TGT_READ:
            {
                for(u32 i = 0; i < len; i++)
                {
                    patched_rom.data[patched_rom_pos++] = RB();
                }
                break;
            }
                
            case SRC_COPY:
            {
                in_read_pos += decode_special(patch.data, &patch_pos);
                for(u32 i = 0; i < len; i++)
                {
                    patched_rom.data[patched_rom_pos++] = rom.data[in_read_pos++];
                }
                break;
            }
                
            case TGT_COPY:
            {
                out_read_pos += decode_special(patch.data, &patch_pos);
                for(u32 i = 0; i < len; i++)
                {
                    patched_rom.data[patched_rom_pos++] = patched_rom.data[out_read_pos++];
                }
                break;
            }
        }
    }
    
    //write patched rom
    FILE* patched_rom_file = fopen((char*)output_file, "wb");
    if(patched_rom_file == NULL)
    {
        printf("apply_patch() error: cannot open output file: %s\n", (char*)output_file);
        goto CLEAN;
    }
    fwrite(patched_rom.data, sizeof(u8), patched_rom.size, patched_rom_file);
    fclose(patched_rom_file);
    
CLEAN:
    free(patched_rom.data);
}


//file size
u32 fsize(FILE* in)
{
    fseek(in, 0, SEEK_END);
    u32 s = ftell(in);
    fseek(in, 0, SEEK_SET);
    return s;
}

//main
int main(int argc, char* argv[])
{
    //argument check
    if(argc != 4)
    {
        printf("usage bps [rom] [patch] [output]\n");
        return 0;
    }
    
    //open rom file
    FILE* rom   = fopen(argv[1], "rb");
    if(rom == NULL)
    {
        printf("main() error: cannot open rom file: %s\n", argv[1]);
        return 1;
    }
    //open patch file
    FILE* patch = fopen(argv[2], "rb");
    if(patch == NULL)
    {
        printf("main() error: cannot open patch file: %s\n", argv[1]);
        fclose(rom);
        return 2;
    }
    
    data_t rom_data =
    {
        .size = fsize(rom),
    }; rom_data.data = malloc(rom_data.size);
    
    data_t patch_data =
    {
        .size = fsize(patch),
    }; patch_data.data = malloc(patch_data.size);
    //read rom and patch
    //u32 rom_size = fsize(rom),        patch_size = fsize(patch);
    //u8* rom_data = malloc(rom_size), *patch_data = malloc(patch_size);
    
    fread(rom_data.data,   sizeof(u8), rom_data.size,   rom);
    fread(patch_data.data, sizeof(u8), patch_data.size, patch);
    
    //apply bps patch
    apply_bps(rom_data, patch_data, (u8*)argv[3]);
    
    //cleanup
    fclose(rom);
    fclose(patch);
    free(rom_data.data);
    free(patch_data.data);
    
    return 0;
}

