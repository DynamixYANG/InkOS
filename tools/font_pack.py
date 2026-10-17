"""Immutable raw font archive, shared by both OTA applications. Pixels are unchanged."""
import struct
MAGIC=b'INKFNT1\0'
SIZES=(14,18,24,28)
RECORD=struct.Struct('<IbbBB')
HEADER_SIZE=64
SPLIT=0x400000

def pack_fonts(fonts):
    codes=[g[0] for g in fonts[0][2]]
    assert codes==sorted(set(codes)) and max(codes)<65536
    assert tuple(f[0] for f in fonts)==SIZES
    records,pixels=bytearray(),bytearray()
    for size,bits,glyphs in fonts:
        assert [g[0] for g in glyphs]==codes
        for cp,offset,x,y,w,h in glyphs:
            raw=bits[offset:offset+(w*h+7)//8]
            assert len(raw)==(w*h+7)//8 and len(raw)<=256
            records.extend(RECORD.pack(len(pixels),x,y,w,h))
            pixels.extend(raw)
    code_offset=HEADER_SIZE
    index_offset=(code_offset+len(codes)*2+15)//16*16
    data_offset=(index_offset+len(records)+15)//16*16
    total=data_offset+len(pixels)
    assert total<=SPLIT*2
    result=bytearray(total)
    struct.pack_into('<8s7I',result,0,MAGIC,1,total,len(codes),len(fonts),code_offset,index_offset,data_offset)
    struct.pack_into('<%dH'%len(codes),result,code_offset,*codes)
    result[index_offset:index_offset+len(records)]=records
    result[data_offset:]=pixels
    return bytes(result),{'glyphs_per_size':len(codes),'sizes':list(SIZES),'asset_bytes':total,'record_bytes':RECORD.size,'internal_bytes':min(total,SPLIT),'external_bytes':max(0,total-SPLIT)}

def compress_archive(raw):
    import zlib,hashlib
    block=262144
    blocks=[]
    for offset in range(0,len(raw),block):
        compressor=zlib.compressobj(9,zlib.DEFLATED,-15)
        value=compressor.compress(raw[offset:offset+block])+compressor.flush()
        assert zlib.decompress(value,-15)==raw[offset:offset+block]
        blocks.append(value)
    cursor=64+4*(len(blocks)+1)
    positions=[cursor]
    for b in blocks:cursor+=len(b);positions.append(cursor)
    result=struct.pack('<8s6I32s',b'INKFZ1\0\0',1,cursor,len(raw),block,len(blocks),64,hashlib.sha256(raw).digest())
    result+=struct.pack('<%dI'%len(positions),*positions)+b''.join(blocks)
    assert len(result)==cursor and cursor<=SPLIT
    return result

def write_archive(fonts,root):
    import hashlib,json,zlib
    raw,stats=pack_fonts(fonts)
    encoded=compress_archive(raw)
    output=root/'out/assets';output.mkdir(parents=True,exist_ok=True)
    (output/'fonts-raw.bin').write_bytes(raw)
    (output/'fonts.bin').write_bytes(encoded)
    (output/'fonts-internal.bin').write_bytes(encoded)
    (output/'fonts-external.bin').write_bytes(b'')
    digest=hashlib.sha256(encoded).hexdigest()
    stats.update(raw_bytes=len(raw),raw_sha256=hashlib.sha256(raw).hexdigest(),asset_bytes=len(encoded),sha256=digest,internal_bytes=len(encoded),external_bytes=0,codec='raw-deflate-262144-byte-blocks',zlib_version=zlib.ZLIB_VERSION,zlib_runtime=zlib.ZLIB_RUNTIME_VERSION,all_blocks_lossless_verified=True)
    (output/'manifest.json').write_text(json.dumps(stats,indent=2)+'\n')
    header='#pragma once\n// Generated immutable font archive identity. OFL-1.1 glyphs.\nnamespace ink {\nconstexpr char FONT_SHA256[]="'+digest+'";\nconstexpr unsigned FONT_ARCHIVE_BYTES='+str(len(encoded))+';\nconstexpr unsigned FONT_RAW_BYTES='+str(len(raw))+';\n}\n'
    (root/'firmware/InkOS/src/CjkAssetsData.h').write_text(header,encoding='utf-8')
    return stats
