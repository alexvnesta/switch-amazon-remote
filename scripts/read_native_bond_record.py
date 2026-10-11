#!/usr/bin/env python3
"""Decode local address-free trial metadata, never bond keys or device events."""
import argparse
import json
from pathlib import Path
import struct
from read_driver_state_snapshot import checksum
SIZE=72
UNKNOWN=0xffffffff
def decode(data):
    if len(data)!=SIZE:raise ValueError('Wrong exact record size')
    magic,version,state,opened,instance,begin,end,handle,guard,getter,enabled,result,issued,hash_value,reserved=struct.unpack('<4I3Q8I',data)
    if (magic,version)!=(0x41524e42,1) or reserved or hash_value!=checksum(data[:64]):raise ValueError('Invalid header/checksum')
    if not 1<=state<=4 or enabled>1 or issued>1 or not begin or end<begin or end-begin>3000:raise ValueError('Invalid fields/timing')
    if bool(issued)!=(state in (3,4)):raise ValueError('Inconsistent request state')
    if issued and (not instance or not handle or opened or guard or getter or enabled!=1):raise ValueError('Issued without prerequisites')
    if (state==3 and result) or (state==4 and result in (0,UNKNOWN)) or (state in (1,2) and result!=UNKNOWN):raise ValueError('Inconsistent result')
    return dict(format_valid=True,state={1:'refused',2:'intent_saved_request_not_observed',3:'request_accepted_outcome_unknown',4:'request_failed'}[state],
        instance=instance,handle=handle,begin_boot_ms=begin,end_boot_ms=end,
        results=dict(open=f'0x{opened:08x}',guard=f'0x{guard:08x}',getter=f'0x{getter:08x}',request=f'0x{result:08x}'),
        driver_enabled=bool(enabled),request_issued=bool(issued),bond_completion_proven=False,
        encryption_or_persistence_proven=False,
        interpretation='Normal targeted native request only. Acceptance is not completion; durable intent without a final result leaves the request boundary uncertain. Never infer a safe retry or a saved bond from this record.')
def self_test():
    data=bytearray(struct.pack('<4I3Q8I',0x41524e42,1,3,0,42,100,101,4,0,0,1,0,1,0,0));struct.pack_into('<I',data,64,checksum(data[:64]))
    assert decode(data)['state']=='request_accepted_outcome_unknown'
    for i in range(SIZE):
        bad=bytearray(data);bad[i]^=1
        try:decode(bad)
        except ValueError:pass
        else:raise AssertionError('Corruption admitted')
    for offset,fmt,value in ((8,'<I',5),(16,'<Q',0),(24,'<Q',0),(32,'<Q',99),(32,'<Q',3101),(40,'<I',0),(44,'<I',1),(48,'<I',1),(52,'<I',0),(56,'<I',1),(60,'<I',0)):
        bad=bytearray(data);struct.pack_into(fmt,bad,offset,value);struct.pack_into('<I',bad,64,checksum(bad[:64]))
        try:decode(bad)
        except ValueError:pass
        else:raise AssertionError('Malformed checksum-valid issued record admitted')
    print('PASS native metadata decoder: exact72 bytes, every-byte corruption, malformed checksum-valid issued prerequisites/results/clocks; no completion/persistence inference')
def main():
    p=argparse.ArgumentParser();p.add_argument('snapshot',nargs='?',type=Path);p.add_argument('--self-test',action='store_true');a=p.parse_args()
    if a.self_test:self_test()
    elif a.snapshot:
        with a.snapshot.open('rb') as f:data=f.read(SIZE+1)
        print(json.dumps(decode(data),indent=2))
    else:p.error('Specify a local snapshot or --self-test')
if __name__=='__main__':main()
