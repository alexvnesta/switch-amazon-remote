#!/usr/bin/env python3
"""Test our source-owned endpoint image, never execute or patch firmware.

No native addresses, console operations, NSO packaging or loader claims. The
three synthetic RX/R/RW segments are loaded at several invented page bases.
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
from unicorn import UC_HOOK_MEM_READ, UC_HOOK_MEM_WRITE, UC_PROT_READ, UC_PROT_WRITE, UC_PROT_EXEC
from unicorn import arm64_const as reg
from test_deferred_arm import acyclic_bound, need

STACK, ENVELOPE, PAYLOAD, OUTPUT, END = 0x20000000,0x20010000,0x20020000,0x20030000,0x20040000
SP=STACK+0xf00
X=[getattr(reg,'UC_ARM64_REG_X'+str(i)) for i in range(31)]
D=[getattr(reg,'UC_ARM64_REG_D'+str(i)) for i in range(8,16)]
MAGIC=0x31444341
EVENT_MAGIC=0x31504341
PREFIX=bytes([0,8,0,0x34,0x12,0,1,16,0])

def segments(blob):
    need(blob[:7]==b'\x7fELF\x02\x01\x01','Expected little-endian ELF64')
    need(struct.unpack_from('<H',blob,18)[0]==183,'Expected AArch64')
    phoff=struct.unpack_from('<Q',blob,32)[0]
    phsize,phnum=struct.unpack_from('<HH',blob,54)
    need(phsize==56 and phnum==3,'Expected three owned load segments')
    result=[]
    for i in range(phnum):
        kind,flags,offset,va,pa,size,memsize,align=struct.unpack_from('<IIQQQQQQ',blob,phoff+i*phsize)
        need(kind==1 and va==i*0x1000 and pa==va and size==memsize and 0<size<=0x1000,
             'Different segment layout')
        need(flags==(5,4,6)[i] and offset+size<=len(blob),'Segment permissions or bounds')
        result.append((va,flags,blob[offset:offset+size]))
    need(len(result[1][2])==16 and len(result[2][2])==36,'Unexpected ro/owned data sizes')
    return result

def run(prefix,*,build_fn=None,slot_offset=0x2000):
    root=Path(__file__).resolve().parents[2]
    source=root/'host_tests/deferred_endpoint_arm.cpp'
    linker=root/'host_tests/deferred_endpoint_arm.ld'
    with tempfile.TemporaryDirectory(prefix='deferred-endpoint-arm-') as folder:
        path=Path(folder);obj=path/'endpoint.o';elf=path/'endpoint.elf'
        if build_fn is not None:
            blob,layout,symbols=build_fn(prefix,path)
        else:
            subprocess.run([prefix+'g++','-std=c++20','-O2','-Wall','-Wextra','-Werror',
            '-fno-exceptions','-fno-rtti','-ffreestanding','-mno-outline-atomics',
            '-march=armv8-a+crc+crypto','-mtune=cortex-a57','-fpie','-fvisibility=hidden',
            '-fno-unwind-tables','-fno-asynchronous-unwind-tables',
                '-c',str(source),'-o',str(obj)],check=True)
            need(not subprocess.check_output([prefix+'nm','-u',str(obj)],text=True).strip(),
                 'Undefined helper or native dependency')
            subprocess.run([prefix+'ld','--no-undefined','-T',str(linker),str(obj),'-o',str(elf)],check=True)
            need('There are no relocations' in subprocess.check_output([prefix+'readelf','-r',str(elf)],text=True),
                 'Runtime relocation required')
            blob=elf.read_bytes();layout=segments(blob)
            symbols={parts[2]:int(parts[0],16) for line in subprocess.check_output(
                [prefix+'nm',str(elf)],text=True).splitlines() if len(parts:=line.split())==3}
    if build_fn is None:need(layout[1][2]==struct.pack('<IIII',MAGIC,1,64,1),'Wrong format metadata')
    initializer=layout[2][2][slot_offset-layout[2][0]:slot_offset-layout[2][0]+36]
    need(initializer==struct.pack('<III',0,EVENT_MAGIC,1)+bytes(24),'Unexpected slot initializer')
    instructions={x.address:(x.mnemonic,x.op_str) for x in Cs(CS_ARCH_ARM64,CS_MODE_ARM).disasm(layout[0][2],0)}
    need(len(instructions)*4==len(layout[0][2]),'Incomplete source code decoding')
    bounds={name:acyclic_bound(instructions,symbols[name]) for name in ('capture_envelope','read_record')}
    need(sum(mn=='ldaxr' for mn,_ in instructions.values())==1 and
         sum(mn=='stxr' for mn,_ in instructions.values())==1 and
         sum(mn=='stlr' for mn,_ in instructions.values())==2,'Atomic publication contract changed')
    calls=0;maximum=0;base_results=[]
    for base in (0x100000,0x123450000,0x7000100000):
        mu=Uc(UC_ARCH_ARM64,UC_MODE_ARM);slot=base+slot_offset
        for va,flags,data in layout:
            extent=(len(data)+4095)&~4095
            mu.mem_map(base+va,extent);mu.mem_write(base+va,data)
            permissions=(UC_PROT_READ if flags&4 else 0)|(UC_PROT_WRITE if flags&2 else 0)|(UC_PROT_EXEC if flags&1 else 0)
            mu.mem_protect(base+va,extent,permissions)
        for addr in (STACK,ENVELOPE,PAYLOAD,OUTPUT,END):mu.mem_map(addr,0x1000)
        allowed_reads=[];allowed_writes=[];steps=0;force_failure=False;writes=[]
        def access_read(_mu,_access,address,size,_value,_user):
            need(any(lo<=address and address+size<=hi for lo,hi in allowed_reads),'Read outside declared envelope/prefix/slot/stack')
        def access_write(_mu,_access,address,size,value,_user):
            need(any(lo<=address and address+size<=hi for lo,hi in allowed_writes),'Write outside owned slot/output/stack')
            writes.append((address,size,value))
        def instruction(_mu,pc,_size,_user):
            nonlocal steps
            if pc==END:mu.emu_stop();return
            need(pc-base in instructions,'Unexpected code outside our endpoint');steps+=1
            mn,op=instructions[pc-base]
            if force_failure and mn=='stxr':
                mu.reg_write(X[int(op.split(',')[0][1:])],1)
                mu.reg_write(reg.UC_ARM64_REG_PC,pc+4)
        mu.hook_add(UC_HOOK_CODE,instruction)
        mu.hook_add(UC_HOOK_MEM_READ,access_read);mu.hook_add(UC_HOOK_MEM_WRITE,access_write)
        def call(name,arg0,arg1=0):
            nonlocal calls,steps,maximum
            steps=0;writes.clear()
            for i,r in enumerate(X):mu.reg_write(r,0x12340000+i)
            for i,r in enumerate(D):mu.reg_write(r,0x123456789abc0000+i)
            mu.reg_write(X[0],arg0);mu.reg_write(X[1],arg1);mu.reg_write(X[30],END)
            mu.reg_write(reg.UC_ARM64_REG_SP,SP)
            mu.emu_start(base+symbols[name],END+4,count=200)
            need(mu.reg_read(reg.UC_ARM64_REG_PC)==END,'Did not return')
            need(mu.reg_read(reg.UC_ARM64_REG_SP)==SP and mu.reg_read(X[30])==END,'Stack/LR damaged')
            for i in range(18,30):need(mu.reg_read(X[i])==0x12340000+i,'Platform/callee register damaged')
            for i,r in enumerate(D):need(mu.reg_read(r)==0x123456789abc0000+i,'Callee SIMD half damaged')
            need(steps<=bounds[name],'Static control-flow bound exceeded')
            maximum=max(maximum,steps);calls+=1
            return mu.reg_read(X[0])
        def capture(opcode=0xfd53,length=9,payload=PREFIX,pointer=PAYLOAD,envelope=ENVELOPE,reset=True,forced=False):
            nonlocal allowed_reads,allowed_writes,force_failure
            if reset:mu.mem_write(slot,initializer)
            before=bytes(mu.mem_read(slot,36));force_failure=forced
            mu.mem_write(ENVELOPE,struct.pack('<HHIQ',opcode,length,0xffffffff,pointer))
            mu.mem_write(PAYLOAD,bytes(payload))
            allowed_reads=[(SP-64,SP)]
            allowed_writes=[(SP-64,SP)]
            if envelope:
                allowed_reads.append((ENVELOPE,ENVELOPE+2))
                if opcode==0xfd53:
                    allowed_reads.extend([(ENVELOPE,ENVELOPE+4),(ENVELOPE+8,ENVELOPE+16),(slot,slot+4)])
                    allowed_writes.append((slot,slot+36))
                    if not forced and before[:4]==bytes(4) and pointer==PAYLOAD and 1<=length<=252:
                        size=1 if payload[0] or length<9 or length==10 else 9
                        allowed_reads.append((PAYLOAD,PAYLOAD+size))
            result=call('capture_envelope',envelope)
            force_failure=False
            after=bytes(mu.mem_read(slot,36))
            if result in (0,1,3):need(after==before,'Rejected/unselected/busy capture changed immutable bytes')
            else:need(result==2 and after[:4]==struct.pack('<I',2),'Unexpected publication state')
            return result
        def read(state=None,size=64,pointer=OUTPUT):
            nonlocal allowed_reads,allowed_writes
            if state is not None:mu.mem_write(slot,struct.pack('<I',state)) # fixture only, not a production API
            stored_state=struct.unpack('<I',mu.mem_read(slot,4))[0]
            before=bytes(mu.mem_read(slot,36));mu.mem_write(OUTPUT,b'\xa5'*64)
            valid=size==64 and pointer==OUTPUT
            allowed_reads=[(SP-64,SP)]+([(slot,slot+(36 if stored_state==2 else 4))] if valid else [])
            allowed_writes=[(SP-64,SP)]+([(OUTPUT,OUTPUT+64)] if valid else [])
            result=call('read_record',pointer,size)
            need(bool(result)==valid and bytes(mu.mem_read(slot,36))==before,'Getter result or slot mutation')
            output=bytes(mu.mem_read(OUTPUT,64))
            if not valid:need(output==b'\xa5'*64,'Failed getter partially wrote output');return output
            need(output[:32]==struct.pack('<8I',MAGIC,1,64,stored_state,1,0,0,0),'Noncanonical record header')
            need(output[32:]==(before[4:] if stored_state==2 else bytes(32)),'Stale or incorrect event bytes')
            return output
        before_calls=calls
        lengths=range(65536) if base==0x100000 else (0,1,8,9,10,11,252,253,65535)
        for length in lengths:
            expected=2 if 9<=length<=252 and length!=10 else 3
            need(capture(length=length)==expected,'Length acceptance changed')
        opcodes=range(65536) if base==0x100000 else (0,0xfd52,0xfd54,65535)
        for opcode in opcodes:
            if opcode!=0xfd53:need(capture(opcode=opcode,pointer=0xdeadc000)==0,'Unselected opcode read or captured')
        for status in range(1,256):
            need(capture(length=1,payload=bytes([status]))==2,'One-byte explicit error not published')
            record=read();need(record[48]==status and struct.unpack_from('<I',record,44)[0]==1,'Error readout differs')
        need(capture(envelope=0)==3,'Null envelope accepted')
        need(capture(pointer=0)==3,'Null prefix accepted')
        need(capture(length=1)==3 and capture(reset=False)==2,'Acknowledgment hid complete reply')
        snapshot=read()
        need(capture(pointer=0xdeadc000,reset=False)==1 and read()==snapshot,'First record overwritten')
        bad=bytearray(PREFIX);bad[6]=2
        need(capture(payload=bad)==3 and capture(reset=False)==2,'Invalid prefix retained claim')
        need(capture(forced=True)==1,'Forced weak exclusive-store failure was retried')
        read(state=0);read(state=1)
        need(capture(pointer=0xdeadc000,reset=False)==1,'Busy slot read payload')
        for size in (*range(0,130),0xffffffff,0xffffffffffffffff):read(size=size)
        read(pointer=0)
        for offset in (1,2,3):read(pointer=OUTPUT+offset)
        need(capture()==2,'Fresh synthetic capture failed')
        record=read()
        need(record[32:]==struct.pack('<IIII',EVENT_MAGIC,1,9,9)+PREFIX+bytes(7),'Complete prefix mismatch')
        base_results.append(dict(synthetic_base=hex(base),calls=calls-before_calls))
    return dict(schema=1,status='source_owned_endpoint_synthetic_checks_passed',
        elf_sha256=hashlib.sha256(blob).hexdigest(),text_sha256=hashlib.sha256(layout[0][2]).hexdigest(),
        text_bytes=len(layout[0][2]),owned_initialized_data_bytes=36,record_bytes=64,
        segments=['RX code','R format metadata','RW initialized owned slot'],
        instruction_bounds=bounds,maximum_observed_instructions=maximum,calls=calls,bases=base_results,
        all_u16_lengths=True,all_unselected_u16_opcodes=True,all_nonzero_error_statuses=True,
        failed_claim_no_retry=True,invalid_sizes_or_alignment_no_output_write=True,
        no_runtime_relocations_helpers_constructors_or_native_dependencies=build_fn is None,
        source_owned_adapters_not_exercised_here=build_fn is not None,
        no_stack_or_callee_register_damage=True,guarded_reads_writes=True,
        actual_native_callback_binding=False,native_getter_ipc_binding=False,
        actual_module_loader_startup_compatibility_proven=False,nsos_or_patch_generated=False,
        firmware_executed=False,console_contact=False,physical_test_ready=False,
        source_hashes={str(p.relative_to(root)):hashlib.sha256(p.read_bytes()).hexdigest() for p in
            (source,linker,Path(__file__),Path(__file__).with_name('deferred_endpoint.hpp'),
             Path(__file__).with_name('deferred_capture.hpp'),
             Path(__file__).with_name('deferred_image.cpp'))})

if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('--prefix',required=True)
    parser.add_argument('--output',type=Path,required=True);args=parser.parse_args()
    need(not args.output.exists(),'Refuse receipt overwrite')
    result=run(args.prefix)
    with args.output.open('x') as stream:json.dump(result,stream,indent=2);stream.write('\n')
    print(json.dumps(result,indent=2))
