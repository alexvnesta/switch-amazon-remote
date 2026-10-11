#!/usr/bin/env python3
"""Compile/test only our deferred slot at invented addresses, never firmware.

Proves bounded control flow for this generated object and exercises guarded
reads, publication and busy/refused cases. Does not bind or patch a driver.
Requires an AArch64 compiler prefix, Capstone and Unicorn.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import struct
import subprocess
import tempfile
from capstone import Cs, CS_ARCH_ARM64, CS_MODE_ARM
from unicorn import Uc, UC_ARCH_ARM64, UC_MODE_ARM, UC_HOOK_CODE
from unicorn import UC_HOOK_MEM_READ, UC_HOOK_MEM_WRITE, UC_PROT_READ, UC_PROT_EXEC
from unicorn import arm64_const as reg

CODE, SLOT, STACK, PAYLOAD, OUTPUT, END = 0x100000,0x200000,0x300000,0x400000,0x500000,0x600000
SP=STACK+0xf00
X=[getattr(reg,'UC_ARM64_REG_X'+str(i)) for i in range(31)]
D=[getattr(reg,'UC_ARM64_REG_D'+str(i)) for i in range(8,16)]

def need(ok,message):
    if not ok:raise AssertionError(message)

def acyclic_bound(instructions,start):
    """All conditional edges, not just observed cases. No calls/indirect jumps."""
    visiting=set();done={}
    def visit(pc):
        need(pc in instructions,'Control escaped compiled source')
        need(pc not in visiting,'Retry or other control-flow cycle')
        if pc in done:return done[pc]
        visiting.add(pc);mn,op=instructions[pc]
        need(mn not in ('bl','blr','br','svc','hvc','smc'),'External operation in deferred capture')
        if mn=='ret':steps=1
        elif mn=='b' or mn.startswith('b.') or mn in ('cbz','cbnz','tbz','tbnz'):
            match=re.search(r'#(0x[0-9a-f]+|[0-9]+)$',op);need(match,'Unknown branch syntax')
            target=int(match[1],0)
            steps=1+max(visit(target),visit(pc+4) if mn!='b' else 0)
        else:steps=1+visit(pc+4)
        visiting.remove(pc);done[pc]=steps;return steps
    return visit(start)

def run(prefix):
    root=Path(__file__).resolve().parents[2]
    source=root/'host_tests/deferred_capture_arm.cpp'
    header=Path(__file__).with_name('deferred_capture.hpp')
    with tempfile.TemporaryDirectory(prefix='deferred-slot-arm-') as folder:
        path=Path(folder);obj=path/'slot.o';elf=path/'slot.elf'
        subprocess.run([prefix+'g++','-std=c++20','-O2','-Wall','-Wextra','-Werror',
            '-fno-exceptions','-fno-rtti','-ffreestanding','-mno-outline-atomics',
            '-march=armv8-a+crc+crypto','-mtune=cortex-a57','-fno-pic','-fno-pie',
            '-c',str(source),'-o',str(obj)],check=True)
        need(not subprocess.check_output([prefix+'nm','-u',str(obj)],text=True).strip(),
             'Undefined helper: queue/lock/atomic or other dependency')
        subprocess.run([prefix+'ld','-Ttext='+hex(CODE),'-Tdata='+hex(SLOT),
            '-e','capture',str(obj),'-o',str(elf)],check=True)
        data={}
        for section in ('text','data'):
            file=path/(section+'.bin')
            subprocess.run([prefix+'objcopy','-O','binary','-j','.'+section,str(elf),str(file)],check=True)
            data[section]=file.read_bytes()
        symbols={parts[2]:int(parts[0],16) for line in subprocess.check_output(
            [prefix+'nm',str(elf)],text=True).splitlines() if len(parts:=line.split())==3}
    need(symbols['offline_slot']==SLOT and len(data['data'])==36,'Different owned slot layout')
    code=data['text'];cs=Cs(CS_ARCH_ARM64,CS_MODE_ARM)
    instructions={x.address:(x.mnemonic,x.op_str) for x in cs.disasm(code,CODE)}
    need(len(instructions)*4==len(code),'Incomplete code decoding')
    bounds={name:acyclic_bound(instructions,symbols[name]) for name in ('capture','read_first')}
    need(sum(mn=='ldaxr' for mn,_ in instructions.values())==1 and
         sum(mn=='stxr' for mn,_ in instructions.values())==1 and
         sum(mn=='stlr' for mn,_ in instructions.values())==2,'Different publication/abandoned-claim implementation')
    # The guard itself must reject cycles and unreviewed external calls.
    for bad in ({0:('b','#0')},{0:('bl','#4'),4:('ret','')}):
        try:acyclic_bound(bad,0)
        except AssertionError:pass
        else:raise AssertionError('Invalid CFG accepted')
    mu=Uc(UC_ARCH_ARM64,UC_MODE_ARM)
    for address in (CODE,SLOT,STACK,PAYLOAD,OUTPUT,END):mu.mem_map(address,0x1000)
    mu.mem_write(CODE,code);mu.mem_protect(CODE,0x1000,UC_PROT_READ|UC_PROT_EXEC)
    allowed_reads=[];allowed_writes=[];writes=[];steps=0;maximum=0;calls=0;fail_exclusive=False
    def read_hook(_mu,_access,address,size,_value,_user):
        need(any(lo<=address and address+size<=hi for lo,hi in allowed_reads),'Out-of-contract read')
    def write_hook(_mu,_access,address,size,value,_user):
        need(any(lo<=address and address+size<=hi for lo,hi in allowed_writes),'Out-of-contract write')
        writes.append((address,size,value))
    def instruction(_mu,pc,_size,_user):
        nonlocal steps
        if pc==END:mu.emu_stop();return
        need(pc in instructions,'Unexpected code execution');steps+=1
        mn,op=instructions[pc]
        if fail_exclusive and mn=='stxr':
            # Synthetic contention/spurious failure, no store and no retry.
            register=int(op.split(',')[0][1:]);mu.reg_write(X[register],1)
            mu.reg_write(reg.UC_ARM64_REG_PC,pc+4)
    mu.hook_add(UC_HOOK_CODE,instruction)
    mu.hook_add(UC_HOOK_MEM_READ,read_hook);mu.hook_add(UC_HOOK_MEM_WRITE,write_hook)
    def invoke(name,args):
        nonlocal steps,maximum,calls
        writes.clear();steps=0
        for i in range(31):mu.reg_write(X[i],0xabc00000+i)
        for i,r in enumerate(D):mu.reg_write(r,0xdef00000+i)
        for i,value in enumerate(args):mu.reg_write(X[i],value)
        mu.reg_write(X[30],END);mu.reg_write(reg.UC_ARM64_REG_SP,SP)
        mu.emu_start(symbols[name],END+4,count=200)
        need(mu.reg_read(reg.UC_ARM64_REG_PC)==END and steps<=bounds[name],'Did not return within CFG bound')
        need(mu.reg_read(reg.UC_ARM64_REG_SP)==SP,'Stack not restored')
        need(all(mu.reg_read(X[i])==0xabc00000+i for i in range(19,30)),'Saved register changed')
        need(all(mu.reg_read(r)==0xdef00000+i for i,r in enumerate(D)),'Saved SIMD half changed')
        maximum=max(maximum,steps);calls+=1;return mu.reg_read(X[0])
    prefix_bytes=bytes([0,8,0,0x34,0x12,0,1,16,0])
    def capture(length,status=0,pointer_override=None):
        nonlocal allowed_reads,allowed_writes
        copied=1 if status and 1<=length<=252 else 9 if not status and (length==9 or 11<=length<=252) else 0
        readable=bytes([status]) if 1<=length<=252 and copied!=9 else prefix_bytes if copied==9 else b''
        pointer=PAYLOAD+0x1000-len(readable)
        if readable:mu.mem_write(pointer,readable)
        if pointer_override is not None:pointer=pointer_override;copied=0;readable=b''
        mu.mem_write(SLOT,data['data'])
        allowed_reads=[(SLOT,SLOT+4),(SP-0x100,SP),(pointer,pointer+len(readable))]
        allowed_writes=[(SLOT,SLOT+36),(SP-0x100,SP)]
        need(invoke('capture',[0xfd53,pointer,length])==(2 if copied else 3),'Wrong capture result')
        expected=struct.pack('<4I',0x31504341,1,length,copied)+readable[:copied]+bytes(12-copied)+bytes(4)
        expected_slot=struct.pack('<I',2)+expected if copied else data['data']
        need(bytes(mu.mem_read(SLOT,36))==expected_slot,'Wrong published/abandoned slot')
        state_writes=[value for address,size,value in writes if address==SLOT and size==4]
        need(state_writes==[1,2 if copied else 0],'Wrong claim/publication ordering')
    for length in range(65536):capture(length)
    for status in range(1,256):capture(252,status)
    capture(9,pointer_override=0)
    # A successful status-only callback must not hide the subsequent full reply.
    capture(1)
    pointer=PAYLOAD+0x1000-9;mu.mem_write(pointer,prefix_bytes)
    allowed_reads=[(SLOT,SLOT+4),(SP-0x100,SP),(pointer,pointer+9)]
    allowed_writes=[(SLOT,SLOT+36),(SP-0x100,SP)]
    need(invoke('capture',[0xfd53,pointer,9])==2,'Status-only observation occupied slot')
    # A structurally invalid flag also releases only an unpublished claim.
    mu.mem_write(SLOT,data['data']);bad=bytearray(prefix_bytes);bad[6]=2;mu.mem_write(pointer,bytes(bad))
    need(invoke('capture',[0xfd53,pointer,9])==3,'Invalid flag admitted')
    need(bytes(mu.mem_read(SLOT,36))==data['data'],'Invalid flag changed published storage')
    # Busy and ready slots never dereference a new borrowed payload.
    for state in (1,2):
        mu.mem_write(SLOT,struct.pack('<I',state)+bytes([0xa5])*32)
        allowed_reads=[(SLOT,SLOT+4)];allowed_writes=[]
        need(invoke('capture',[0xfd53,1,9])==1 and not writes,'Occupied capture did not refuse')
    mu.mem_write(SLOT,data['data']);fail_exclusive=True
    allowed_reads=[(SLOT,SLOT+4)];allowed_writes=[]
    need(invoke('capture',[0xfd53,1,9])==1 and not writes,'Failed exclusive store retried or copied')
    fail_exclusive=False
    for opcode in (0,1,0x1001,0xfc27,0xfd57,0xffff):
        allowed_reads=[];allowed_writes=[]
        need(invoke('capture',[opcode,1,9])==0 and not writes,'Unselected callback touched memory')
    for state in (0,1,2):
        mu.mem_write(SLOT,struct.pack('<I',state)+bytes([0xa5])*32);mu.mem_write(OUTPUT,bytes([0x5a])*32)
        allowed_reads=[(SLOT,SLOT+(36 if state==2 else 4))]
        allowed_writes=[(OUTPUT,OUTPUT+32)] if state==2 else []
        need(invoke('read_first',[OUTPUT])==state,'Wrong read state')
        need(bytes(mu.mem_read(OUTPUT,32))==bytes([0xa5 if state==2 else 0x5a])*32,'Read changed wrong bytes')
    allowed_reads=[];allowed_writes=[]
    need(invoke('read_first',[0])==99,'Null output handling')
    return dict(schema=1,status='deferred_owned_slot_synthetic_checks_passed_not_integrated',
        calls=calls,all_u16_lengths=True,controller_error_status_cases=255,
        source_sha256=hashlib.sha256(header.read_bytes()).hexdigest(),
        text_sha256=hashlib.sha256(code).hexdigest(),text_bytes=len(code),owned_storage_bytes=36,
        cfg_instruction_bounds=bounds,maximum_observed_instructions=maximum,
        cfg_cycles_or_external_calls=False,undefined_helpers=[],
        busy_and_ready_refusal=True,synthetic_failed_exclusive_store_no_retry=True,
        status_only_then_complete=True,invalid_flag_releases_unpublished_claim=True,
        firmware_loaded=False,device_access=False,native_storage_bound=False,
        native_retrieval_implemented=False,deployment=False,
        limits=['Only our source and synthetic memory/CPU were tested.',
                'No hardware latency or complete C++ memory-model proof.',
                'Only the first complete prefix or explicit error is retained; no later conflict or loss count.',
                'Native allocation/initialization, callback envelope, read-only retrieval and rollback remain unimplemented.'])

if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('--prefix',required=True);parser.add_argument('--output',type=Path)
    args=parser.parse_args();result=run(args.prefix);rendered=json.dumps(result,indent=2)+'\n'
    if args.output:
        with args.output.open('x') as stream:stream.write(rendered)
    print(rendered,end='')
