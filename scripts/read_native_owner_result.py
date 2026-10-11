#!/usr/bin/env python3
"""Decode address/key-free owner result; acceptance is never bond proof."""
import argparse
import json
from pathlib import Path
import struct
from read_driver_state_snapshot import checksum
from read_native_bond_record import decode as trial_decode
SIZE=256
STAGES=('unknown','local_state','saved_metadata','live_user_binding','live_system_getter','list_bounds','list_invalid','target_binding','passed')
def decode(data):
    if len(data)!=SIZE:raise ValueError('Wrong exact owner result size')
    magic,version,sequence=struct.unpack_from('<IIQ',data)
    count,hash_value=struct.unpack_from('<II',data,248)
    if magic!=0x41524f52 or version!=1 or not sequence or count>2 or hash_value!=checksum(data[:252]):raise ValueError('Invalid owner header/checksum')
    trial=trial_decode(data[16:88]);checks=[]
    for index in range(count):
        raw=data[88+80*index:168+80*index]
        m,v,call,stage,result,reserved,instance,begin,end,user,system,n,matches,invalid,handle,h,pad=struct.unpack('<6I3Q8I',raw)
        if m!=0x41524f43 or v!=1 or reserved or pad or call!=index+1 or not 1<=stage<=8 or not begin or end<begin or end-begin>3000 or h!=checksum(raw[:72]):raise ValueError('Invalid owner check')
        if (result!=0 if stage==8 else result==0):raise ValueError('Inconsistent owner guard result')
        checks.append(dict(call=call,stage=STAGES[stage],result=f'0x{result:08x}',instance=instance,begin_boot_ms=begin,end_boot_ms=end,
            user_result=None if user==0xffffffff else f'0x{user:08x}',system_result=None if system==0xffffffff else f'0x{system:08x}',
            count=None if n==0xffffffff else n,matches=None if matches==0xffffffff else matches,invalid=None if invalid==0xffffffff else invalid,handle=handle))
    if trial['request_issued'] and (count!=2 or any(c['stage']!='passed' for c in checks)):raise ValueError('Request without two passing owner guards')
    return dict(format_valid=True,sequence=sequence,trial=trial,checks=checks,connection_client_mode='owner_borrowed_clients',
        bond_completion_proven=False,encryption_or_persistence_proven=False)
def self_test():
    raw=bytearray(SIZE);struct.pack_into('<IIQ',raw,0,0x41524f52,1,6)
    struct.pack_into('<4I3Q8I',raw,16,0x41524e42,1,3,0,42,100,102,4,0,0,1,0,1,0,0)
    struct.pack_into('<I',raw,80,checksum(raw[16:80]))
    for i in range(2):
        start=88+80*i
        struct.pack_into('<6I3Q8I',raw,start,0x41524f43,1,i+1,8,0,0,42,100+i,101+i,0,0,1,1,0,4,0,0)
        struct.pack_into('<I',raw,start+72,checksum(raw[start:start+72]))
    struct.pack_into('<I',raw,248,2);struct.pack_into('<I',raw,252,checksum(raw[:252]))
    assert decode(raw)['trial']['request_issued']
    for i in range(SIZE):
        bad=bytearray(raw);bad[i]^=1
        try:decode(bad)
        except ValueError:pass
        else:raise AssertionError('Corruption admitted')
    for offset,value in ((4,2),(248,3),(100,9),(104,1),(88+8,2),(88+72,0)):
        bad=bytearray(raw);struct.pack_into('<I',bad,offset,value)
        if 88<=offset<160:struct.pack_into('<I',bad,160,checksum(bad[88:160]))
        struct.pack_into('<I',bad,252,checksum(bad[:252]))
        try:decode(bad)
        except ValueError:pass
        else:raise AssertionError('Malformed owner result admitted')
    print('PASS owner decoder: exact256 bytes, every-byte corruption and malformed guards; no request/completion conflation')
if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('snapshot',nargs='?',type=Path);p.add_argument('--self-test',action='store_true');a=p.parse_args()
    if a.self_test:self_test()
    elif a.snapshot:print(json.dumps(decode(a.snapshot.read_bytes()),indent=2))
    else:p.error('Specify snapshot or --self-test')
