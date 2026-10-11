#!/usr/bin/env python3
"""Decode a local metadata-only driver-state record; never accesses the console."""
import argparse
import json
from pathlib import Path
import struct

SIZE=64
NOT_ATTEMPTED=0xffffffff
INVALID=0xfffffffe

def checksum(data):
    v=2166136261
    for b in data:v=((v^b)*16777619)&0xffffffff
    return v

def decode(data):
    if len(data)!=SIZE:raise ValueError('Wrong exact record size')
    magic,version,begin,end,opened,query,validation,enabled,firmware,command,r0,r1,hash_value,tail=struct.unpack('<IIQQ10I',data)
    if (magic,version,firmware,command)!=(0x41524453,1,0x160500,100) or r0 or r1 or tail or hash_value!=checksum(data[:56]):
        raise ValueError('Invalid header/checksum/reserved fields')
    if not begin or end<begin or end-begin>2000:raise ValueError('Invalid clock bounds')
    eligible=opened==query==validation==0
    if opened:
        if query!=NOT_ATTEMPTED or validation!=NOT_ATTEMPTED or enabled:raise ValueError('Open failure contains evidence')
    elif query:
        if validation!=NOT_ATTEMPTED or enabled:raise ValueError('Query failure contains evidence')
    elif validation:
        if validation!=INVALID or enabled:raise ValueError('Invalid getter contains evidence')
    elif enabled>1:raise ValueError('Invalid boolean response')
    return dict(format_valid=True,collection_begin_boot_ms=begin,collection_end_boot_ms=end,
        results=dict(open=f'0x{opened:08x}',getter=f'0x{query:08x}',validation=f'0x{validation:08x}'),
        eligible_metadata=eligible,bluetooth_enabled=bool(enabled) if eligible else None,
        selected_backend_proven=False,bond_completion_proven=False,persistence_proven=False,
        interpretation='True can exclude the default false-returning backend only if the live firmware matches the reviewed stored image. False or error is inconclusive about backend selection. No BLE security or bond claim.')

def self_test():
    data=bytearray(struct.pack('<IIQQ10I',0x41524453,1,100,101,0,0,0,1,0x160500,100,0,0,0,0))
    struct.pack_into('<I',data,56,checksum(data[:56]))
    assert decode(data)['bluetooth_enabled'] is True
    for i in range(SIZE):
        bad=bytearray(data);bad[i]^=1
        try:decode(bad)
        except ValueError:pass
        else:raise AssertionError('Corruption accepted')
    for offset,fmt,value in ((0,'<I',0),(8,'<Q',0),(16,'<Q',99),(16,'<Q',2101),(36,'<I',2),(40,'<I',0),(44,'<I',10),(48,'<I',1),(60,'<I',1),(24,'<I',1),(28,'<I',1),(32,'<I',INVALID)):
        bad=bytearray(data);struct.pack_into(fmt,bad,offset,value);struct.pack_into('<I',bad,56,checksum(bad[:56]))
        try:decode(bad)
        except ValueError:pass
        else:raise AssertionError('Malformed checksum-valid record accepted')
    for length in (0,63,65):
        try:decode(bytes(length))
        except ValueError:pass
        else:raise AssertionError('Wrong size accepted')
    for opened,query,validation in ((44,NOT_ATTEMPTED,NOT_ATTEMPTED),(0,55,NOT_ATTEMPTED),(0,0,INVALID)):
        bad=bytearray(data);struct.pack_into('<4I',bad,24,opened,query,validation,0);struct.pack_into('<I',bad,56,checksum(bad[:56]))
        assert decode(bad)['bluetooth_enabled'] is None
    bad=bytearray(data);struct.pack_into('<I',bad,36,0);struct.pack_into('<I',bad,56,checksum(bad[:56]))
    assert decode(bad)['bluetooth_enabled'] is False
    print('PASS driver decoder: exact64 bytes, every-byte corruption, malformed checksum-valid fields, clocks and errors; no bond inference')

def main():
    p=argparse.ArgumentParser();p.add_argument('snapshot',nargs='?',type=Path);p.add_argument('--self-test',action='store_true');a=p.parse_args()
    if a.self_test:self_test()
    elif a.snapshot:
        with a.snapshot.open('rb') as f:data=f.read(SIZE+1)
        print(json.dumps(decode(data),indent=2))
    else:p.error('Specify a local snapshot or --self-test')

if __name__=='__main__':main()
