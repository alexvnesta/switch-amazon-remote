"""Decode a bounded S1 report and optional full-readback marker. No device I/O."""
import argparse
import json
from pathlib import Path
import struct

SIZE=20080
def decode(blob,marker=None):
    if len(blob)!=SIZE:raise ValueError('Wrong S1 report size')
    words=struct.unpack_from('<12I',blob)
    magic,version,size,verdict,pc,ic,bc,pd,idrop,bd,flags,checksum=words
    if (magic,version,size,flags)!=(0x41525331,1,SIZE,7) or not 1<=verdict<=4 or pc>32 or ic>192 or bc>192:
        raise ValueError('Wrong S1 schema or bounds')
    h=2166136261
    for i,b in enumerate(blob):h=((h^(0 if 44<=i<48 else b))*16777619)&0xffffffff
    if h!=checksum:raise ValueError('S1 checksum mismatch')
    begin,deadline,sleep,resume,finished=struct.unpack_from('<5Q',blob,48)
    if not begin or deadline!=begin+600000 or finished<begin:raise ValueError('Invalid window')
    if verdict==1 and not (begin<=sleep<=resume<=finished-10000):raise ValueError('Invalid acknowledged cycle')
    intent_drops=struct.unpack_from('<3I',blob,88);ble_drops=struct.unpack_from('<3I',blob,100)
    if (sum(intent_drops)&0xffffffff)!=idrop or (sum(ble_drops)&0xffffffff)!=bd:raise ValueError('Drop counters differ')
    sections={};offset=112
    for name,count,capacity,kind in [('power',pc,32,None),('intents',ic,192,4),('ble',bc,192,3)]:
        rows=[]
        for index in range(capacity):
            raw=blob[offset+48*index:offset+48*(index+1)]
            if index>=count:
                if any(raw):raise ValueError('Nonzero unused record')
                continue
            time,program,sequence,event_kind,selector,result,record_flags,reported,metadata=struct.unpack('<QQ6I8s',raw)
            if not time or time<begin or time>finished:raise ValueError('Record outside observation interval')
            if kind and event_kind!=kind:raise ValueError('Wrong record kind')
            phase=record_flags>>24
            if name=='power':
                if program or event_kind not in (1,2) or reported:raise ValueError('Invalid power record')
                row=dict(boot_ms=time,sequence=sequence,kind=event_kind,state=selector,result=result,flags=record_flags,
                         previous_state=int.from_bytes(metadata[:4],'little') if event_kind==1 else None)
            elif name=='intents':
                if (program not in (0x0100000000000013,0x010000000000002a) or phase>2 or result!=0xffffffff or
                    not (47<=selector<=49 or 55<=selector<=61)):raise ValueError('Invalid intent metadata')
                if any(metadata[4:]):raise ValueError('Unexpected retained intent bytes')
                row=dict(boot_ms=time,sequence=sequence,phase=phase,program=f'{program:016x}',command=selector,
                         reported_size=reported,size_or_null_flags=record_flags&0xffffff,
                         metadata=list(metadata[:4]),forward_result='unobserved')
            else:
                copied=record_flags&0xffffff
                if program or selector not in (6,7) or phase>2 or copied>(7 if selector==6 else 8) or any(metadata[copied:]):
                    raise ValueError('Invalid BLE prefix metadata')
                row=dict(boot_ms=time,sequence=sequence,phase=phase,event=selector,reported_size=reported,
                         copied_size=copied,prefix=metadata[:copied].hex(),ownership='unproven')
            rows.append(row)
        sections[name]=rows;offset+=48*capacity
    verified=False
    if marker is not None:
        if len(marker)!=16 or struct.unpack('<4I',marker)!=(0x4152534f,1,SIZE,checksum):raise ValueError('Readback marker mismatch')
        verified=True
    return dict(schema='sleep-resume-s1',verdict=['pending','PSC_sleep_resume_observed','expired','power_error','shutdown'][verdict],
                begin_ms=begin,deadline_ms=deadline,sleep_ms=sleep,resume_ms=resume,finished_ms=finished,
                full_report_readback_marker_matched=verified,phase_names=['pre_sleep','sleep_transition','post_resume'],
                drops=dict(power=pd,intent_by_phase=intent_drops,ble_by_phase=ble_drops),records=sections,
                limits=['PSC state is not measured hardware power state','Callbacks do not prove filter ownership or radio retention',
                        'Late producers/counters may be outside the final snapshot','No remote Home advertisement payload or wake interrupt captured'])
def read_bounded(path,size):
    with path.open('rb') as f:blob=f.read(size+1)
    if len(blob)!=size:raise ValueError('Wrong bounded file size: '+str(path))
    return blob
if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('report',type=Path);p.add_argument('--marker',type=Path)
    a=p.parse_args();print(json.dumps(decode(read_bounded(a.report,SIZE),read_bounded(a.marker,16) if a.marker else None),indent=2))
