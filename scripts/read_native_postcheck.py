#!/usr/bin/env python3
"""Decode fixed address/key-free getter evidence, never bond proof."""
import argparse
import json
from pathlib import Path
import struct
from read_driver_state_snapshot import checksum

SIZE=184
UNKNOWN=0xffffffff
INVALID=0xfffffffe
GUARDS=('passed','preflight_refused','owner_refused','intent_refused','stopped_session_refused','changed_after_getters')

def decode(data):
    if len(data)!=SIZE:raise ValueError('Wrong exact postcheck size')
    magic,version,instance,sequence,request_end,owner_hash,intent_hash,guard,reserved=struct.unpack_from('<II3Q4I',data)
    stored,tail=struct.unpack_from('<II',data,176)
    if magic!=0x41525034 or version!=1 or reserved or tail or guard>=len(GUARDS) or stored!=checksum(data[:176]):
        raise ValueError('Invalid postcheck header/checksum')
    raw=data[48:176]
    m,v,begin,end=struct.unpack_from('<IIQQ',raw)
    user_open=struct.unpack_from('<I',raw,24)[0];system_open=struct.unpack_from('<I',raw,64)[0]
    sys_open,flag_result,flag,r0,h,r1=struct.unpack_from('<6I',raw,104)
    if m!=0x41525033 or v!=3 or r0 or r1 or h!=checksum(raw[:120]) or flag>1 or (flag_result and flag):
        raise ValueError('Invalid inner context')
    def query(offset,connected=False):
        result,count,matches,invalid=struct.unpack_from('<4I',raw,offset)
        handle=struct.unpack_from('<I',raw,offset+16)[0] if connected else 0
        valid=invalid in (0,1) and count<=255
        if result==0:
            valid &= not invalid and count<=(15 if connected else 10) and matches<=min(1,count)
            if connected:valid &= (0<handle<UNKNOWN if matches else not handle)
        elif result==INVALID:
            valid &= invalid==1 and matches<=(1 if connected else 10) and (not connected or not handle)
        else:valid &= not count and not matches and not invalid and not handle
        if not valid:raise ValueError('Invalid bounded getter metadata')
        return dict(result=f'0x{result:08x}',count=count,matches=matches,invalid=bool(invalid),target_handle=handle if connected else None)
    uc,sc,up,sp=query(28,True),query(68,True),query(48),query(88)
    for opened,connection,paired in ((user_open,uc,up),(system_open,sc,sp)):
        if opened and (connection['result']!='0xffffffff' or paired['result']!='0xffffffff'):raise ValueError('Evidence after client-open failure')
        if not opened and (connection['result']=='0xffffffff' or paired['result']=='0xffffffff'):raise ValueError('Missing getter after client-open success')
    if (sys_open and (flag_result!=UNKNOWN or flag)) or (not sys_open and flag_result==UNKNOWN):raise ValueError('Invalid flag getter lifecycle')
    timing=bool(instance and sequence and request_end and begin>=request_end and end>=begin and end-begin<=2000)
    eligible=guard==0 and timing
    def paired(opened,q):
        return bool(q['matches']) if eligible and not opened and q['result']=='0x00000000' and not q['invalid'] and q['count']<10 else None
    return dict(format_valid=True,instance=instance,sequence=sequence,request_end_boot_ms=request_end,
        owner_checksum=owner_hash,intent_checksum=intent_hash,guard=GUARDS[guard],begin_boot_ms=begin,end_boot_ms=end,
        timing_after_request_valid=timing,eligible_correlated_metadata=eligible,
        user_open=f'0x{user_open:08x}',system_open=f'0x{system_open:08x}',native_flag_open=f'0x{sys_open:08x}',
        user_connections=uc,system_connections=sc,user_paired=up,system_paired=sp,
        native_flag_result=f'0x{flag_result:08x}',native_flag_started=bool(flag) if not sys_open and not flag_result else None,
        experimental_scoped_user_target_listed=paired(user_open,up),experimental_scoped_system_target_listed=paired(system_open,sp),
        bond_completion_proven=False,encryption_or_persistence_proven=False,
        interpretation='Foreground sequential getters, correlated to accepted request only after external owner/intent verification. AD-derived scope is experimental. Empty scoped lists do not disprove bonding; a match does not prove encryption, stored bond or reconnect persistence.')

def self_test():
    raw=bytearray(SIZE);struct.pack_into('<II3Q4I',raw,0,0x41525034,1,42,4,902,1,2,0,0)
    struct.pack_into('<IIQQ',raw,48,0x41525033,3,1000,1001)
    for offset in (28,68):struct.pack_into('<5I',raw,48+offset,0,1,1,0,4)
    def seal(r):
        struct.pack_into('<I',r,168,checksum(r[48:168]));struct.pack_into('<I',r,176,checksum(r[:176]));return r
    seal(raw);assert decode(raw)['experimental_scoped_system_target_listed'] is False
    for i in range(SIZE):
        bad=bytearray(raw);bad[i]^=1
        try:decode(bad)
        except ValueError:pass
        else:raise AssertionError('Corruption admitted')
    for length in (0,183,185):
        try:decode(bytes(length))
        except ValueError:pass
        else:raise AssertionError('Wrong size admitted')
    for offset,value in ((4,2),(44,1),(180,1),(40,6),(48+76,2),(48+84,0),(48+72,16),(48+116,1)):
        bad=bytearray(raw);struct.pack_into('<I',bad,offset,value);seal(bad)
        try:decode(bad)
        except ValueError:pass
        else:raise AssertionError('Resealed malformed metadata admitted')
    for offset,fmt,value in ((40,'<I',5),(56,'<Q',901),(64,'<Q',3001)):
        bad=bytearray(raw);struct.pack_into(fmt,bad,offset,value);seal(bad)
        assert not decode(bad)['eligible_correlated_metadata']
    bad=bytearray(raw);struct.pack_into('<6I',bad,112,0x615,UNKNOWN,0,0,0,0);struct.pack_into('<4I',bad,136,UNKNOWN,0,0,0);seal(bad)
    assert decode(bad)['experimental_scoped_system_target_listed'] is None
    print('PASS postcheck decoder: exact184, every-byte corruption, resealed bounds, API-open failure, clock/guard refusal; no bond inference')

if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('snapshot',nargs='?',type=Path);p.add_argument('--self-test',action='store_true');a=p.parse_args()
    if a.self_test:self_test()
    elif a.snapshot:
        with a.snapshot.open('rb') as f:raw=f.read(SIZE+1)
        print(json.dumps(decode(raw),indent=2))
    else:p.error('Specify snapshot or --self-test')
