#!/usr/bin/env python3
"""Decode exact address/key-free guard evidence; never infer a completed bond."""
import argparse
import json
from pathlib import Path
import struct
from read_driver_state_snapshot import checksum
SIZE=400
UNKNOWN=0xffffffff
STAGES=('not_reached','binary_identity','process_service_open','process_query','sd_filesystem',
        'startup_read','startup_invalid','saved_context_read','status_read','connected_ready',
        'user_client_open','user_connection_getter','connection_list_bounds',
        'connection_list_invalid','target_not_visible','saved_context_binding','passed')


def decode(data):
    if len(data)!=SIZE:raise ValueError('Wrong exact guard record size')
    magic,version,call,stage,result,flags,firmware,aruid=struct.unpack_from('<8I',data)
    begin,end,checked=struct.unpack_from('<3Q',data,32)
    pm_init,pm_query,status_rc,user_open,user_getter,count,matches,invalid,handle,query_rc=struct.unpack_from('<10I',data,56)
    hash_value,reserved=struct.unpack_from('<2I',data,392)
    if magic!=0x41524744 or version not in (1,2) or reserved or hash_value!=checksum(data[:392]):raise ValueError('Invalid guard header/checksum')
    if not 1<=call<=2 or not 1<=stage<=16 or flags&~127 or not begin or not begin<=checked<=end:raise ValueError('Invalid guard fields/timing')
    if (aruid not in (0,1) if flags&32 else aruid!=UNKNOWN):raise ValueError('Invalid observed ARUID-zero flag')
    if (result!=0 if stage==16 else result==0):raise ValueError('Inconsistent guard outcome')
    rc=lambda n:None if n==UNKNOWN else f'0x{n:08x}'
    stage_name=STAGES[stage]
    if version==2 and stage in (10,11):stage_name=('system_client_open','system_connection_getter')[stage-10]
    prefix='user' if version==1 else 'system'
    out=dict(format_valid=True,format_version=version,connection_view=prefix,guard_call=call,stage=stage_name,result=rc(result),flags=flags,
             firmware_components=[firmware>>16,(firmware>>8)&255,firmware&255],
             begin_boot_ms=begin,end_boot_ms=end,check_boot_ms=checked,
             process_results=dict(open=rc(pm_init),query=rc(pm_query)),
             status_read_result=rc(status_rc),
             caller_aruid_zero=bool(aruid) if flags&32 else None,
             native_request_or_bond_completion_proven=False)
    out[prefix+'_client_open_result']=rc(user_open)
    out[prefix+'_connection_getter_result']=rc(user_getter)
    if flags&4:
        start=data[96:136];sm,sv,ss,sr,si,sn,sres,sh=struct.unpack('<4I2Q2I',start)
        out['startup']=dict(instance=si,stage=ss,result=rc(sr),boot_ms=sn,
            valid=sm==0x41525331 and sv==1 and 1<=ss<=14 and si>0 and sn>0 and sres==0 and sh==checksum(start[:36]))
    if flags&16:
        raw=data[136:224];v=struct.unpack('<4I4Q6I2Q',raw)
        valid=v[0]==0x41524d31 and v[1]==3 and v[2]<=5 and v[4]>0 and v[5]>0 and v[8]<=1 and v[10]<=1 and v[12]<=2 and v[11]==checksum(raw[:60]+raw[64:])
        out['status']=dict(instance=v[4],state=v[2],result=rc(v[3]),boot_ms=v[5],
            wire_valid=valid,fresh_at_check=valid and v[5]<=checked and checked-v[5]<=2000,
            scan_admission_flag=v[8],remote_buttons=v[9],output_attached=v[10],cleanup_result=rc(v[13]),
            acknowledged_buttons=v[14],successful_writes=v[15])
    if flags&8:
        raw=data[224:392];cm,cv,ci,expected,before,after,res0=struct.unpack_from('<2IQ4I',raw)
        sb,se=struct.unpack_from('<2Q',raw,40)
        uopen=struct.unpack_from('<I',raw,56)[0];uq=struct.unpack_from('<5I',raw,60)
        bopen=struct.unpack_from('<I',raw,96)[0];bq=struct.unpack_from('<5I',raw,100)
        ch,res=struct.unpack_from('<2I',raw,160)
        snap=raw[32:160];snap_magic,snap_version=struct.unpack_from('<2I',snap)
        snap_res0,snap_hash,snap_res=struct.unpack_from('<3I',snap,116)
        valid=cm==0x41524331 and cv==1 and ci>0 and expected not in (0,UNKNOWN) and not res0 and not res and ch==checksum(raw[:160]) and snap_magic==0x41525033 and snap_version==3 and not snap_res0 and not snap_res and snap_hash==checksum(snap[:120])
        out['context']=dict(instance=ci,expected_handle=expected,before_binding=rc(before),after_binding=rc(after),
            wire_valid=valid,begin_boot_ms=sb,end_boot_ms=se,age_at_check_ms=checked-se if checked>=se else None,
            user_open=rc(uopen),system_open=rc(bopen),
            user_connection=dict(result=rc(uq[0]),count=uq[1],matches=uq[2],invalid=uq[3],handle=uq[4]),
            system_connection=dict(result=rc(bq[0]),count=bq[1],matches=bq[2],invalid=bq[3],handle=bq[4]))
    if flags&64:
        out['current_'+prefix+'_connection']=dict(count=count,matches=None if matches==UNKNOWN else matches,
            invalid=None if invalid==UNKNOWN else invalid,handle=handle,reducer_result=rc(query_rc))
    return out


def self_test():
    raw=bytearray(SIZE)
    struct.pack_into('<8I3Q10I',raw,0,0x41524744,1,1,14,0xfffffffe,32|64,0x160500,0,100,110,105,
                     0,0,0,0,0,0,0,0,0,0)
    struct.pack_into('<I',raw,392,checksum(raw[:392]))
    d=decode(raw);assert d['stage']=='target_not_visible' and d['caller_aruid_zero'] is False and d['current_user_connection']['count']==0
    assert d['format_version']==1 and d['connection_view']=='user' and 'current_system_connection' not in d
    for i in range(SIZE):
        bad=bytearray(raw);bad[i]^=1
        try:decode(bad)
        except ValueError:pass
        else:raise AssertionError('Corrupt diagnostic admitted')
    for offset,fmt,value in ((8,'<I',0),(8,'<I',3),(12,'<I',17),(16,'<I',0),(20,'<I',128),(28,'<I',2),(40,'<Q',99)):
        bad=bytearray(raw);struct.pack_into(fmt,bad,offset,value);struct.pack_into('<I',bad,392,checksum(bad[:392]))
        try:decode(bad)
        except ValueError:pass
        else:raise AssertionError('Malformed diagnostic admitted')
    new=bytearray(raw);struct.pack_into('<I',new,4,2);struct.pack_into('<I',new,392,checksum(new[:392]))
    d=decode(new);assert d['format_version']==2 and d['connection_view']=='system'
    assert d['current_system_connection']['count']==0 and 'current_user_connection' not in d
    assert d['system_client_open_result']=='0x00000000' and 'user_client_open_result' not in d
    for i in range(SIZE):
        bad=bytearray(new);bad[i]^=1
        try:decode(bad)
        except ValueError:pass
        else:raise AssertionError('Corrupt version2 diagnostic admitted')
    for stage,name in ((10,'system_client_open'),(11,'system_connection_getter')):
        frame=bytearray(new);struct.pack_into('<I',frame,12,stage);struct.pack_into('<I',frame,392,checksum(frame[:392]))
        assert decode(frame)['stage']==name
    bad=bytearray(new);struct.pack_into('<I',bad,4,3);struct.pack_into('<I',bad,392,checksum(bad[:392]))
    try:decode(bad)
    except ValueError:pass
    else:raise AssertionError('Unknown version admitted')
    print('PASS guard decoder: exact400 bytes, every-byte corruption, malformed signed metadata, observed-vs-unobserved fields; no bonding inference')


if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('snapshot',nargs='?',type=Path);p.add_argument('--self-test',action='store_true');a=p.parse_args()
    if a.self_test:self_test()
    elif a.snapshot:
        with a.snapshot.open('rb') as f:raw=f.read(SIZE+1)
        print(json.dumps(decode(raw),indent=2))
    else:p.error('Specify snapshot or --self-test')
