#!/usr/bin/env python3
"""Test our corrected observer and fixed-page guard with synthetic stubs only.

Never load or execute firmware. QueryMemory, native queues/locks and tail
destinations are fixtures. Passing is not a live startup or wake qualification.
"""
import argparse
from hashlib import sha256
import json
from pathlib import Path
import struct
import subprocess
import tempfile
from capstone import Cs, CS_ARCH_ARM64, CS_MODE_ARM
from unicorn import Uc, UC_ARCH_ARM64, UC_MODE_ARM, UC_HOOK_CODE
from unicorn import UC_HOOK_MEM_READ, UC_HOOK_MEM_WRITE
from unicorn import arm64_const as r
import test_deferred_bridge_arm as old
import test_deferred_endpoint_arm as endpoint
from test_deferred_arm import need

SOURCE=Path(__file__).resolve().parent
ORIGIN=old.ORIGIN
GUARD_ORIGIN=0x80000
X=old.X

def symbols(prefix,elf):
    return {parts[2]:int(parts[0],16) for line in subprocess.check_output(
        [prefix+'nm',str(elf)],text=True).splitlines() if len(parts:=line.split())==3}

def no_runtime(prefix,elf):
    need('There are no relocations' in subprocess.check_output([prefix+'readelf','-r',str(elf)],text=True),
         'Runtime relocation appeared')
    need(not subprocess.check_output([prefix+'nm','-u',str(elf)],text=True).strip(), 'Unresolved symbol')

def build(prefix,destination,bindings=old.BINDINGS,origin=ORIGIN):
    need(set(bindings)==set(old.BINDINGS),'Binding names changed')
    flags=['-std=c++20','-O2','-Wall','-Wextra','-Werror','-fno-exceptions','-fno-rtti',
        '-ffreestanding','-mno-outline-atomics','-march=armv8-a+crc+crypto','-mtune=cortex-a57',
        '-fpie','-fvisibility=hidden','-fno-unwind-tables','-fno-asynchronous-unwind-tables']
    subprocess.run([prefix+'g++',*flags,'-c',str(SOURCE/'deferred_runtime.cpp'),
                    '-o',str(destination/'runtime.o')],check=True)
    for name in ('deferred_runtime','deferred_bridge'):
        subprocess.run([prefix+'gcc','-c',str(SOURCE/(name+'.S')),
                        '-o',str(destination/(name+'.o'))],check=True)
    elf=destination/'image.elf'
    subprocess.run([prefix+'ld','--no-undefined','-T',str(SOURCE/'deferred_runtime.ld'),
        '--defsym=deferred_image_base='+hex(origin),
        *['--defsym='+name+'='+hex(value) for name,value in bindings.items()],
        str(destination/'runtime.o'),str(destination/'deferred_runtime.o'),
        str(destination/'deferred_bridge.o'),'-o',str(elf)],check=True)
    no_runtime(prefix,elf)
    blob=elf.read_bytes()
    need(blob[:7]==b'\x7fELF\x02\x01\x01' and struct.unpack_from('<H',blob,18)[0]==183,'Wrong ELF')
    phoff=struct.unpack_from('<Q',blob,32)[0];phsize,phnum=struct.unpack_from('<HH',blob,54)
    need(phsize==56 and phnum==3,'Unexpected load segments')
    layout=[]
    for i in range(3):
        kind,flags,offset,va,pa,size,memsize,align=struct.unpack_from('<IIQQQQQQ',blob,phoff+i*56)
        need(kind==1 and flags==(5,4,6)[i] and va==pa==origin+i*4096 and
             size==memsize and 0<size<=(4096,4096,0x1024)[i] and offset+size<=len(blob),
             'Segment contract changed')
        layout.append((va,flags,blob[offset:offset+size]))
    names=symbols(prefix,elf)
    validate(layout,names,origin)
    return blob,layout,names

def validate(layout,names,origin):
    text,ro,rw=[row[2] for row in layout]
    need(text[:16]==struct.pack('<4I',0x14000002,0x1000,0xd65f03c0,0xd5032c9f),'Legacy prefix/D2 marker changed')
    need(struct.unpack_from('<13I',ro)==(0x30444f4d,0x40,0x2024,0x2024,0,0,0x1000,0,0,0,0,0,0),
         'MOD0 storage bounds differ')
    need(ro[0x34:0x40]==bytes(12),'Header alignment differs')
    need([struct.unpack_from('<QQ',ro,0x40+i*16) for i in range(6)]==
         [(4,0x10a0),(5,0x10c8),(6,0x10b0),(10,1),(11,24),(0,0)],'Dynamic table changed')
    need(ro[0xa0:0xb0]==struct.pack('<4I',1,1,0,0) and ro[0xb0:0xc9]==bytes(25),
         'Empty hash/symbol/string metadata changed')
    fmt=names['endpoint_format']-origin-0x1000
    need(ro[fmt:fmt+16]==struct.pack('<4I',0x31444341,1,64,1),'Endpoint format differs')
    need(rw==bytes(4096)+struct.pack('<III',0,0x31504341,1)+bytes(24),'Runtime page or slot initializer changed')
    need(names['deferred_runtime_object']==origin+0x2000 and
         names['deferred_runtime_slot']==origin+0x3000 and
         names['deferred_runtime_data_end']==origin+0x3024,'Storage overlaps or moved')

def build_guard(prefix,destination,bindings,addon_symbols,origin=GUARD_ORIGIN,capacity=0x400):
    elf=destination/'main-guard.elf';obj=destination/'main-guard.o'
    subprocess.run([prefix+'gcc','-c',str(SOURCE/'deferred_main_guard.S'),'-o',str(obj)],check=True)
    values={name:bindings[name] for name in ('previous_custom_mux','previous_metadata_relay','native_hid_body')}
    values.update({name:addon_symbols[name] for name in ('deferred_read_mux','deferred_metadata_relay','capture_envelope')})
    values.update(deferred_image_base=ORIGIN,deferred_main_guard_base=origin,deferred_main_guard_capacity=capacity)
    subprocess.run([prefix+'ld','--no-undefined','-T',str(SOURCE/'deferred_main_guard.ld'),
        *['--defsym='+name+'='+hex(value) for name,value in values.items()],str(obj),'-o',str(elf)],check=True)
    no_runtime(prefix,elf)
    raw=destination/'main-guard.bin'
    subprocess.run([prefix+'objcopy','-O','binary','-j','.text',str(elf),str(raw)],check=True)
    code=raw.read_bytes();need(0<len(code)<=capacity,'Guard bounds changed')
    return code,symbols(prefix,elf),sha256(elf.read_bytes()).hexdigest()

def guard_tests(prefix,bindings=old.BINDINGS,guard_origin=GUARD_ORIGIN,capacity=0x400):
    with tempfile.TemporaryDirectory(prefix='deferred-runtime-guard-') as folder:
        path=Path(folder);elf,layout,addon=build(prefix,path,bindings)
        code,names,guard_sha=build_guard(prefix,path,bindings,addon,guard_origin,capacity)
    instructions={i.address:(i.mnemonic,i.op_str) for i in Cs(CS_ARCH_ARM64,CS_MODE_ARM).disasm(code,guard_origin)}
    need(len(instructions)*4==len(code),'Guard decode incomplete')
    need([op for mn,op in instructions.values() if mn=='svc']==['#6'],'Unexpected syscall')
    calls=0;max_steps=0;cases=0
    for base in (0x100000,0x123450000,0x7000100000):
        for route,target,fallback in (
            ('deferred_main_read_guard',addon['deferred_read_mux'],bindings['previous_custom_mux']),
            ('deferred_main_relay_guard',addon['deferred_metadata_relay'],bindings['previous_metadata_relay']),
            ('deferred_main_capture_guard',addon['capture_envelope'],None)):
            fixtures=[dict(label='matched')]
            fixtures += [dict(label='permission-'+str(p),perm=p) for p in range(16) if p!=5]
            fixtures += [dict(label='syscall-error',error=0x1234),dict(label='shifted-base',start=ORIGIN+0x9000),
                         dict(label='zero-size',size=0),dict(label='larger-region',size=8192),
                         dict(label='wrong-base',start=ORIGIN-4096)]
            fixtures += [dict(label='prefix-bit-'+str(bit),first=0x14000002^(1<<bit)) for bit in range(32)]
            fixtures += [dict(label='mod0-offset-bit-'+str(bit),second=0x1000^(1<<bit)) for bit in range(32)]
            fixtures += [dict(label='entry-return-bit-'+str(bit),third=0xd65f03c0^(1<<bit)) for bit in range(32)]
            fixtures += [dict(label='abi-marker-bit-'+str(bit),fourth=0xd5032c9f^(1<<bit)) for bit in range(32)]
            fixtures += [dict(label='old-inert-L2-addon',fourth=0xd503201f)]
            for fixture in fixtures:
                cases+=1;mu=Uc(UC_ARCH_ARM64,UC_MODE_ARM);mapped=set()
                def mapping(address):
                    page=address&~4095
                    if page not in mapped:mu.mem_map(page,4096);mapped.add(page)
                for addr in (base+guard_origin,old.STACK,old.END,base+target,old.DESC,old.INPUT,old.PAYLOAD):mapping(addr)
                if fallback is not None:mapping(base+fallback)
                mu.mem_write(base+guard_origin,code)
                # No addon is mapped for invalid permissions/errors: any read would fail.
                may_read=not fixture.get('error',0) and fixture.get('perm',5)==5 and \
                    fixture.get('start',ORIGIN)==ORIGIN and fixture.get('size',4096)==4096
                if may_read:
                    mapping(base+ORIGIN)
                    mu.mem_write(base+ORIGIN,struct.pack('<4I',fixture.get('first',0x14000002),fixture.get('second',0x1000),
                        fixture.get('third',0xd65f03c0),fixture.get('fourth',0xd5032c9f)))
                steps=0;queries=0;terminal=None;header_reads=0
                def memory_read(_mu,_access,address,size,_value,_user):
                    nonlocal header_reads
                    if base+ORIGIN<=address<base+ORIGIN+16:
                        need(may_read and address+size<=base+ORIGIN+16,'Read addon before valid query')
                        header_reads+=1
                    else:
                        ranges=[(old.SP-0xc0,old.SP)]
                        if route=='deferred_main_read_guard':ranges.extend([(old.DESC,old.DESC+16),(old.INPUT,old.INPUT+4)])
                        if route=='deferred_main_capture_guard':ranges.append((old.PAYLOAD,old.PAYLOAD+2))
                        need(any(lo<=address and address+size<=hi for lo,hi in ranges),'Guard read outside declared inputs/stack/header')
                def memory_write(_mu,_access,address,size,_value,_user):
                    need(old.SP-0xc0<=address and address+size<=old.SP,'Guard wrote outside stack')
                def hook(_mu,pc,_size,_user):
                    nonlocal steps,queries,terminal
                    if pc in (old.END,base+target) or (fallback is not None and pc==base+fallback):
                        terminal=pc;mu.emu_stop();return
                    need(pc-base in instructions,'Executed outside own-source guard');steps+=1
                    if instructions[pc-base][0]=='svc':
                        queries+=1
                        need(mu.reg_read(X[0])==old.SP-0x30 and mu.reg_read(X[2])==base+ORIGIN,
                             'Query address/output depends on caller')
                        info=struct.pack('<QQ6I',base+fixture.get('start',ORIGIN),fixture.get('size',4096),
                                         3,0,fixture.get('perm',5),0,0,0)
                        mu.mem_write(old.SP-0x30,info)
                        for i in (*range(8),16,17):mu.reg_write(X[i],0xbad000+i)
                        mu.reg_write(X[0],fixture.get('error',0));mu.reg_write(X[1],0x1234)
                        mu.reg_write(r.UC_ARM64_REG_PC,pc+4)
                mu.hook_add(UC_HOOK_CODE,hook);mu.hook_add(UC_HOOK_MEM_READ,memory_read);mu.hook_add(UC_HOOK_MEM_WRITE,memory_write)
                for i in range(31):mu.reg_write(X[i],0x12340000+i)
                for i,reg in enumerate(old.D):mu.reg_write(reg,0x123456789abc0000+i)
                mu.reg_write(X[30],old.END);mu.reg_write(r.UC_ARM64_REG_SP,old.SP)
                expected={i:0x12340000+i for i in range(30)}
                if route=='deferred_main_read_guard':
                    expected.update({1:0,2:old.DESC});mu.mem_write(old.DESC,struct.pack('<QQ',old.INPUT,16))
                    mu.mem_write(old.INPUT,struct.pack('<I',11))
                elif route=='deferred_main_relay_guard':expected[8]=0x6d
                else:expected[0]=old.PAYLOAD;mu.mem_write(old.PAYLOAD,struct.pack('<H',0xfd53))
                for i,value in expected.items():mu.reg_write(X[i],value)
                mu.emu_start(base+names[route],old.END+4,count=100)
                success=fixture['label']=='matched'
                need(terminal==(base+target if success else old.END if fallback is None else base+fallback),
                     'Invalid layout accepted or matched layout refused')
                need(queries==1 and steps<100,'Query retried or guard failed to terminate')
                need(mu.reg_read(r.UC_ARM64_REG_SP)==old.SP and mu.reg_read(X[30])==old.END,'Frame/LR damaged')
                for i in (*range(16),*range(18,30)):
                    need(mu.reg_read(X[i])==expected[i],'Argument/platform/callee register damaged')
                for i,reg in enumerate(old.D):need(mu.reg_read(reg)==0x123456789abc0000+i,'Callee SIMD damaged')
                need(bool(header_reads)==may_read,'Header-read gate differs')
                calls+=1;max_steps=max(max_steps,steps)
    return dict(status='own_source_fixed_page_guard_synthetic_checks_passed',cases=cases,calls=calls,
        guard_bytes=len(code),guard_elf_sha256=guard_sha,addon_elf_sha256=sha256(elf).hexdigest(),
        maximum_source_instructions=max_steps,single_fixed_QueryMemory_only=True,
        invalid_mapping_rejected_before_header_read=True,argument_and_callee_registers_preserved=True,
        native_firmware_executed=False,kernel_syscall_executed=False,device_contact=False,deployed=False)

def fastpath_tests(prefix,bindings=old.BINDINGS,guard_origin=GUARD_ORIGIN,capacity=0x400):
    with tempfile.TemporaryDirectory(prefix='deferred-guard-fast-') as folder:
        path=Path(folder);_,_,addon=build(prefix,path,bindings)
        code,names,_=build_guard(prefix,path,bindings,addon,guard_origin,capacity)
    instructions={i.address:(i.mnemonic,i.op_str) for i in Cs(CS_ARCH_ARM64,CS_MODE_ARM).disasm(code,guard_origin)}
    cases=[]
    for size in (*range(257),0xffffffffffffffff):
        if size!=16:cases.append(('deferred_main_read_guard',{1:0,2:old.DESC},size,11,bindings['previous_custom_mux']))
    for command in range(256):
        if command!=11:cases.append(('deferred_main_read_guard',{1:0,2:old.DESC},16,command,bindings['previous_custom_mux']))
    cases.extend([('deferred_main_read_guard',{1:1,2:0xdeadc000},16,11,bindings['previous_custom_mux']),
                  ('deferred_main_read_guard',{1:0,2:0},16,11,bindings['previous_custom_mux']),
                  ('deferred_main_read_guard',{1:0,2:old.DESC},16,None,bindings['previous_custom_mux'])])
    for kind in range(256):
        if kind!=0x6d:cases.append(('deferred_main_relay_guard',{8:kind},0,0,bindings['previous_metadata_relay']))
    for opcode in (0,1,0x1001,0xfc27,0xfd57,0xffff):
        cases.append(('deferred_main_capture_guard',{0:old.PAYLOAD},0,opcode,None))
    cases.extend([('deferred_main_capture_guard',{0:0},0,0,None),
                  ('deferred_main_legacy_refuse',{1:0},0,0,None),
                  ('deferred_main_legacy_refuse',{1:123},0,0,bindings['native_hid_body'])])
    calls=0
    for base in (0x100000,0x123450000,0x7000100000):
        mu=Uc(UC_ARCH_ARM64,UC_MODE_ARM);mapped=set()
        for address in (base+guard_origin,old.STACK,old.END,old.DESC,old.INPUT,old.PAYLOAD,
                        *(base+bindings[name] for name in ('previous_custom_mux','previous_metadata_relay','native_hid_body'))):
            page=address&~4095
            if page not in mapped:mu.mem_map(page,4096);mapped.add(page)
        mu.mem_write(base+guard_origin,code);reads=[];writes=[];terminal=None
        targets={old.END,*(base+bindings[name] for name in ('previous_custom_mux','previous_metadata_relay','native_hid_body'))}
        def read_hook(_mu,_access,address,size,_value,_user):
            need(any(lo<=address and address+size<=hi for lo,hi in reads),'Fast path touched undeclared memory')
        def write_hook(_mu,_access,address,size,_value,_user):
            need(any(lo<=address and address+size<=hi for lo,hi in writes),'Fast path wrote undeclared memory')
        def hook(_mu,pc,_size,_user):
            nonlocal terminal
            if pc in targets:terminal=pc;mu.emu_stop();return
            need(pc-base in instructions and instructions[pc-base][0]!='svc','Normal path entered query or non-source code')
        mu.hook_add(UC_HOOK_CODE,hook);mu.hook_add(UC_HOOK_MEM_READ,read_hook);mu.hook_add(UC_HOOK_MEM_WRITE,write_hook)
        for route,override,size,command,target in cases:
            calls+=1;terminal=None;reads=[];writes=[]
            for i in range(31):mu.reg_write(X[i],override.get(i,0x12340000+i))
            mu.reg_write(X[30],old.END);mu.reg_write(r.UC_ARM64_REG_SP,old.SP)
            if route=='deferred_main_read_guard' and not override[1] and override[2]:
                reads=[(old.DESC,old.DESC+16)]
                mu.mem_write(old.DESC,struct.pack('<QQ',old.INPUT if command is not None else 0,size))
                if size==16 and command is not None:
                    reads.append((old.INPUT,old.INPUT+4));mu.mem_write(old.INPUT,struct.pack('<I',command))
            if route=='deferred_main_capture_guard' and override[0]:
                reads=[(old.PAYLOAD,old.PAYLOAD+2)];mu.mem_write(old.PAYLOAD,struct.pack('<H',command))
            normal_hid=route=='deferred_main_legacy_refuse' and override[1]
            if normal_hid:writes=[(old.SP-0x30,old.SP)]
            mu.emu_start(base+names[route],old.END+4,count=40)
            need(terminal==(old.END if target is None else base+target),'Wrong fast tail route')
            need(mu.reg_read(r.UC_ARM64_REG_SP)==old.SP-(0x30 if normal_hid else 0),'Fast path frame changed')
            for i in (*range(18,29),30):
                need(mu.reg_read(X[i])==(old.END if i==30 else override.get(i,0x12340000+i)), 'Fast path saved/platform register damaged')
            if normal_hid:
                need(mu.reg_read(X[29])==old.SP-0x20 and bytes(mu.mem_read(old.SP-0x20,32))==
                     struct.pack('<4Q',0x1234001d,old.END,0x12340014,0x12340013),'Native HID frame differs')
            elif route=='deferred_main_legacy_refuse':need(mu.reg_read(X[0])==0xffffffff,'Generic command not refused')
    return dict(calls=calls,ordinary_routes_perform_no_query=True,generic_commands_refused=True,
                native_HID_frame_preserved=True,firmware_executed=False)

def run(prefix,bindings=old.BINDINGS,guard_origin=GUARD_ORIGIN,capacity=0x400):
    adapter=old.run(prefix,bindings,build_fn=build,slot_offset=0x3000)
    guard=guard_tests(prefix,bindings,guard_origin,capacity)
    need(adapter['elf_sha256']==guard['addon_elf_sha256'],'Tests exercised different addon bytes')
    def normalized_build(prefix,path):
        blob,layout,names=build(prefix,path,bindings)
        return blob,[(va-ORIGIN,flags,data) for va,flags,data in layout],{name:va-ORIGIN for name,va in names.items()}
    capture=endpoint.run(prefix,build_fn=normalized_build,slot_offset=0x3000)
    need(capture['elf_sha256']==adapter['elf_sha256'],'Capture tests exercised different bytes')
    fast=fastpath_tests(prefix,bindings,guard_origin,capacity)
    return dict(schema=1,status='corrected_observer_source_tests_passed_not_deployment_qualified',
        adapters=adapter,guard=guard,capture=capture,fast_paths=fast,runtime_object_page_bytes=4096,slot_offset=0x3000,
        addon_allocation_bytes=0x4000,source_hashes={p.name:sha256(p.read_bytes()).hexdigest() for p in
        (Path(__file__),*(SOURCE/name for name in ('deferred_runtime.S','deferred_runtime.ld','deferred_runtime.cpp',
        'deferred_main_guard.S','deferred_main_guard.ld','deferred_bridge.S','deferred_endpoint.hpp','deferred_capture.hpp')))},
        native_binding_verified=False,full_native_startup_proven=False,physical_test_ready=False,
        deployed=False,deep_sleep_wake_proven=False)

if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--prefix',required=True);parser.add_argument('--output',required=True,type=Path)
    args=parser.parse_args();need(not args.output.exists(),'Refuse existing receipt')
    result=run(args.prefix)
    with args.output.open('x') as handle:json.dump(result,handle,indent=2);handle.write('\n')
    print(json.dumps(result,indent=2))
