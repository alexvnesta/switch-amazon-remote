#!/usr/bin/env python3
"""Execute only our adapters and endpoint against synthetic native stubs.

No firmware, console, real mutex, driver, queue, or radio is executed. Invented
external addresses exercise page-relative binding and ASLR, not native layout.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import tempfile
from capstone import Cs, CS_ARCH_ARM64, CS_MODE_ARM
from unicorn import Uc, UC_ARCH_ARM64, UC_MODE_ARM, UC_HOOK_CODE
from unicorn import UC_HOOK_MEM_READ, UC_HOOK_MEM_WRITE
from unicorn import UC_PROT_READ, UC_PROT_WRITE, UC_PROT_EXEC
from unicorn import arm64_const as r
from test_deferred_arm import need, acyclic_bound

X=[getattr(r,'UC_ARM64_REG_X'+str(i)) for i in range(31)]
D=[getattr(r,'UC_ARM64_REG_D'+str(i)) for i in range(8,16)]
ORIGIN=0x2f4000
BINDINGS=dict(previous_custom_mux=0x11080,native_queue=0x20120,
    native_hid_body=0x11010,previous_metadata_relay=0x22010,queue_pop=0x22080,
    core_mailbox=0x1af3e8,native_lock=0x39010,relay_publish=0x22100)
STACK,DESC,INPUT,NODE,PAYLOAD,END=[0x40000000+i*0x1000 for i in range(6)]
SP=STACK+0xf00
NONCE=0x8877665544332211
PREFIX=bytes([0,8,0,0x34,0x12,0,1,16,0])

def build(prefix,destination,bindings=BINDINGS,origin=ORIGIN):
    source=Path(__file__).resolve().parent
    obj=destination/'image.o';bridge=destination/'bridge.o';elf=destination/'image.elf'
    subprocess.run([prefix+'g++','-std=c++20','-O2','-Wall','-Wextra','-Werror',
        '-fno-exceptions','-fno-rtti','-ffreestanding','-mno-outline-atomics',
        '-march=armv8-a+crc+crypto','-mtune=cortex-a57','-fpie','-fvisibility=hidden',
        '-fno-unwind-tables','-fno-asynchronous-unwind-tables','-c',str(source/'deferred_image.cpp'),
        '-o',str(obj)],check=True)
    subprocess.run([prefix+'gcc','-c',str(source/'deferred_bridge.S'),'-o',str(bridge)],check=True)
    need(set(bindings)==set(BINDINGS),'External binding names changed')
    subprocess.run([prefix+'ld','--no-undefined','-T',str(source/'deferred_image.ld'),
        '--defsym=deferred_image_base='+hex(origin),
        *['--defsym='+name+'='+hex(value) for name,value in bindings.items()],
        str(obj),str(bridge),'-o',str(elf)],check=True)
    need('There are no relocations' in subprocess.check_output([prefix+'readelf','-r',str(elf)],text=True),
         'Runtime relocation required')
    need(not subprocess.check_output([prefix+'nm','-u',str(elf)],text=True).strip(),'Unresolved dependency')
    blob=elf.read_bytes();need(blob[:7]==b'\x7fELF\x02\x01\x01','Wrong ELF')
    need(struct.unpack_from('<H',blob,18)[0]==183,'Wrong architecture')
    phoff=struct.unpack_from('<Q',blob,32)[0];phsize,phnum=struct.unpack_from('<HH',blob,54)
    need(phsize==56 and phnum==3,'Unexpected load segments');layout=[]
    for i in range(3):
        kind,flags,offset,va,pa,size,memsize,align=struct.unpack_from('<IIQQQQQQ',blob,phoff+i*phsize)
        need(kind==1 and va==origin+i*4096 and pa==va and flags==(5,4,6)[i] and
             size==memsize and 0<size<=4096 and offset+size<=len(blob),'Segment contract changed')
        layout.append((va,flags,blob[offset:offset+size]))
    need(layout[1][2]==struct.pack('<4I',0x31444341,1,64,1),'Wrong read-only format')
    need(layout[2][2]==struct.pack('<III',0,0x31504341,1)+bytes(24),'Wrong slot initialization')
    symbols={parts[2]:int(parts[0],16) for line in subprocess.check_output([prefix+'nm',str(elf)],text=True).splitlines()
             if len(parts:=line.split())==3}
    return blob,layout,symbols

def run(prefix,bindings=BINDINGS,*,build_fn=build,slot_offset=0x2000):
    with tempfile.TemporaryDirectory(prefix='deferred-bridge-arm-') as folder:
        blob,layout,symbols=build_fn(prefix,Path(folder),bindings)
    instructions={i.address:(i.mnemonic,i.op_str) for i in Cs(CS_ARCH_ARM64,CS_MODE_ARM).disasm(layout[0][2],ORIGIN)}
    need(len(instructions)*4==len(layout[0][2]),'Incomplete decoding')
    bounds={name:acyclic_bound(instructions,symbols[name]) for name in ('capture_envelope','read_record')}
    calls=0;bases=[];max_steps=0
    for base in (0x100000,0x123450000,0x7000100000):
        mu=Uc(UC_ARCH_ARM64,UC_MODE_ARM);mapped=set()
        def mapping(addr,permissions=UC_PROT_READ|UC_PROT_WRITE):
            page=addr&~4095
            if page not in mapped:mu.mem_map(page,4096,permissions);mapped.add(page)
        for va,flags,data in layout:
            extent=(len(data)+4095)&~4095
            for offset in range(0,extent,4096):mapping(base+va+offset)
            mu.mem_write(base+va,data)
            mu.mem_protect(base+va,extent,(UC_PROT_READ if flags&4 else 0)|(UC_PROT_WRITE if flags&2 else 0)|(UC_PROT_EXEC if flags&1 else 0))
        for name,offset in bindings.items():mapping(base+offset,UC_PROT_READ|UC_PROT_EXEC if name!='core_mailbox' else UC_PROT_READ|UC_PROT_WRITE)
        for addr in (STACK,DESC,INPUT,NODE,PAYLOAD):mapping(addr)
        mapping(END,UC_PROT_READ|UC_PROT_EXEC)
        slot=base+ORIGIN+slot_offset;mailbox=base+bindings['core_mailbox']
        reads=[];writes=[];observed=[];steps=0;queue_result=0;terminal=None
        stub_at={base+value:name for name,value in bindings.items() if name!='core_mailbox'}
        def guard_read(_mu,_access,address,size,_value,_user):
            need(any(lo<=address and address+size<=hi for lo,hi in reads),f'Out-of-contract read {address:x}+{size}')
        def guard_write(_mu,_access,address,size,_value,_user):
            need(any(lo<=address and address+size<=hi for lo,hi in writes),f'Out-of-contract write {address:x}+{size}')
        def instruction(_mu,pc,_size,_user):
            nonlocal terminal,steps
            if pc==END:terminal='return';mu.emu_stop();return
            if pc in stub_at:
                name=stub_at[pc];observed.append(name)
                if name=='native_queue':
                    need([mu.reg_read(X[i]) for i in (0,1,2,3)]==[SP-0x60,80,2,0x6d],'Queue ABI differs')
                    observed.append(bytes(mu.mem_read(SP-0x60,80)))
                    saved_lr=mu.reg_read(X[30])
                    for i in range(18):mu.reg_write(X[i],0xa000+i)
                    mu.reg_write(X[0],queue_result&0xffffffff);mu.reg_write(r.UC_ARM64_REG_PC,saved_lr);return
                if name=='native_lock':
                    need(mu.reg_read(X[0])==mailbox+0x30,'Wrong mutex argument')
                    saved_lr=mu.reg_read(X[30])
                    for i in range(18):mu.reg_write(X[i],0xb000+i)
                    mu.reg_write(r.UC_ARM64_REG_PC,saved_lr);return
                terminal=name;mu.emu_stop();return
            need(pc-base in instructions,'Executed something other than our source');steps+=1
        mu.hook_add(UC_HOOK_CODE,instruction);mu.hook_add(UC_HOOK_MEM_READ,guard_read);mu.hook_add(UC_HOOK_MEM_WRITE,guard_write)
        def call(name,args=(),registers=None):
            nonlocal calls,steps,terminal,max_steps
            calls+=1;steps=0;terminal=None;observed.clear()
            for i,reg in enumerate(X):mu.reg_write(reg,0x12340000+i)
            for i,reg in enumerate(D):mu.reg_write(reg,0x123456789abc0000+i)
            for i,value in enumerate(args):mu.reg_write(X[i],value)
            mu.reg_write(X[30],END);mu.reg_write(r.UC_ARM64_REG_SP,SP)
            for i,value in (registers or {}).items():mu.reg_write(X[i],value)
            mu.emu_start(base+symbols[name],END+4,count=200)
            need(terminal is not None,'Adapter did not terminate within 200 source instructions')
            max_steps=max(max_steps,steps)
            for i in (18,19,20,21,22,23,24,26,27,28):
                need(mu.reg_read(X[i])==(registers or {}).get(i,0x12340000+i),'Callee/platform register damaged')
            for i,reg in enumerate(D):need(mu.reg_read(reg)==0x123456789abc0000+i,'Callee SIMD half damaged')
            return mu.reg_read(X[0])
        def intact_frame():
            need(mu.reg_read(r.UC_ARM64_REG_SP)==SP and mu.reg_read(X[29])==0x1234001d and
                 mu.reg_read(X[30])==END,'Frame/LR not restored')
            need(mu.reg_read(X[25])==0x12340019,'Unexpected x25 change')
        def request(command=11,magic=0x31524341,nonce=NONCE,size=16,address=0,pointer=INPUT,descriptor=DESC):
            nonlocal reads,writes
            mu.mem_write(DESC,struct.pack('<QQ',pointer,size));mu.mem_write(INPUT,struct.pack('<IIQ',command,magic,nonce))
            reads=[];writes=[]
            if not address and descriptor:
                reads.append((DESC,DESC+16))
                if size==16 and pointer==INPUT:reads.append((INPUT,INPUT+16))
            selected=not address and descriptor and size==16 and pointer and command==11
            valid=selected and magic==0x31524341 and nonce
            if valid:
                reads.extend([(slot,slot+36),(SP-0x100,SP)]);writes.append((SP-0x100,SP))
            value=call('deferred_read_mux',(0xabcdef,address,descriptor));intact_frame()
            if valid:need(terminal=='return' and observed[0]=='native_queue' and value==(queue_result&0xffffffff),'Wrong queue outcome')
            elif selected:need(terminal=='return' and value==0xffffffff and not observed,'Invalid request accepted')
            else:
                need(terminal=='previous_custom_mux' and [mu.reg_read(X[i]) for i in range(3)]==[0xabcdef,address,descriptor],
                     'Unselected request did not tail-chain unchanged')
            return observed[1] if valid else None
        before=calls
        for size in (*range(0,257),0xffffffff,0xffffffffffffffff):
            if size!=16:request(size=size,pointer=0xdeadc000)
        for command in range(256):request(command=command)
        for bit in range(32):request(magic=0x31524341^(1<<bit))
        request(nonce=0);request(pointer=0);request(descriptor=0);request(address=1,descriptor=0xdeadc000)
        for state in (0,1,2):
            event=struct.pack('<4I',0x31504341,1,9,9)+PREFIX+bytes(7)
            mu.mem_write(slot,struct.pack('<I',state)+event)
            for result in (0,1,-1,0xf601):
                queue_result=result;reply=request()
                expected=struct.pack('<IIQ8I',0x314c4341,1,NONCE,0x31444341,1,64,state,1,0,0,0)
                need(reply==expected+(event if state==2 else bytes(32)),'Getter returned wrong record')
        reads=[];writes=[]
        need(call('deferred_legacy_refuse',(7,0,0xdeadc000))==0xffffffff and terminal=='return','Generic bridge not refused')
        intact_frame();reads=[];writes=[(SP-0x30,SP)]
        call('deferred_legacy_refuse',(7,123,0xdeadc000))
        need(terminal=='native_hid_body' and mu.reg_read(r.UC_ARM64_REG_SP)==SP-0x30 and
             mu.reg_read(X[29])==SP-0x20,'Wrong native HID prologue')
        need(bytes(mu.mem_read(SP-0x20,32))==struct.pack('<4Q',0x1234001d,END,0x12340014,0x12340013),'HID saved frame differs')
        def relay(kind=0x6d,size=80,magic=0x314c4341,version=1,nonce=NONCE):
            nonlocal reads,writes
            packet=struct.pack('<IIQ',magic,version,nonce)+bytes(range(64))
            mu.mem_write(NODE,bytes(16)+struct.pack('<Q',size));mu.mem_write(PAYLOAD,packet)
            mu.mem_write(mailbox,bytes([0xa5])*0x444);reads=[];writes=[]
            if kind==0x6d:
                reads.append((NODE+16,NODE+24))
                if size==80:reads.append((PAYLOAD,PAYLOAD+80))
            valid=kind==0x6d and size==80 and magic==0x314c4341 and version==1 and nonce
            if valid:writes=[(mailbox+0x38,mailbox+0x90),(mailbox+0x440,mailbox+0x444)]
            call('deferred_metadata_relay',registers={8:kind,25:NODE,28:PAYLOAD})
            need(mu.reg_read(r.UC_ARM64_REG_SP)==SP,'Relay changed stack')
            if valid:
                need(terminal=='relay_publish' and observed==['native_lock','relay_publish'],'Wrong metadata delivery path')
                expected=bytearray([0xa5]*0x444);struct.pack_into('<Q',expected,0x38,80)
                expected[0x40:0x90]=packet;struct.pack_into('<I',expected,0x440,0x6d)
                need(bytes(mu.mem_read(mailbox,0x444))==expected,'Mailbox copy exceeded fixed record')
            else:
                need(terminal==('queue_pop' if kind==0x6d else 'previous_metadata_relay'),'Wrong invalid/unselected path')
                need(mu.reg_read(X[25])==NODE and bytes(mu.mem_read(mailbox,0x444))==bytes([0xa5])*0x444,'Invalid event changed native mailbox')
        for kind in range(256):relay(kind=kind)
        for size in (*range(0,257),0xffffffffffffffff):relay(size=size)
        for bit in range(32):relay(magic=0x314c4341^(1<<bit));relay(version=1^(1<<bit))
        relay(nonce=0)
        bases.append(dict(base=hex(base),calls=calls-before))
    source=Path(__file__).resolve().parent
    return dict(schema=1,status='own_adapters_synthetic_tests_passed',calls=calls,bases=bases,external_stub_bindings=bindings,
        elf_sha256=hashlib.sha256(blob).hexdigest(),text_bytes=len(layout[0][2]),maximum_source_instructions=max_steps,
        capture_and_read_bounds=bounds,queue_result_forwarded=True,callee_register_and_stack_checks=True,
        guarded_memory=True,one_fixed_metadata_request=True,generic_legacy_commands_refused=True,
        nonselected_requests_and_events_tail_chain=True,firmware_executed=False,real_mutex_or_queue_tested=False,
        native_binding_verified=False,console_contact=False,deployed=False,physical_test_ready=False,
        source_hashes={p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in
            (Path(__file__),*(source/name for name in ('deferred_bridge.S','deferred_image.cpp','deferred_image.ld',
                'deferred_capture.hpp','deferred_endpoint.hpp','deferred_transport.hpp')))})

if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('--prefix',required=True)
    parser.add_argument('--output',type=Path,required=True);args=parser.parse_args()
    need(not args.output.exists(),'Refuse receipt overwrite');result=run(args.prefix)
    with args.output.open('x') as stream:json.dump(result,stream,indent=2);stream.write('\n')
    print(json.dumps(result,indent=2))
