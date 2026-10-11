#!/usr/bin/env python3
"""Replay only our assembly, with synthetic addresses and fake queue/lock calls.

No Nintendo binary, patch generation, networking or controller commands. A pass
qualifies these local ABI/bounds contracts, NOT the native integration or device.
Requires Unicorn and an AArch64 bare-metal compiler prefix supplied explicitly.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import tempfile

from unicorn import Uc, UC_ARCH_ARM64, UC_MODE_ARM, UC_HOOK_CODE
from unicorn import UC_HOOK_MEM_READ, UC_HOOK_MEM_WRITE, UC_PROT_READ, UC_PROT_EXEC
from unicorn import arm64_const as reg

X = [getattr(reg, "UC_ARM64_REG_X" + str(i)) for i in range(31)]
D = [getattr(reg, "UC_ARM64_REG_D" + str(i)) for i in range(8, 16)]
CODE, STUBS, STACK, INPUT, PAYLOAD, MAILBOX = (
    0x100000, 0x180000, 0x200000, 0x300000, 0x400000, 0x500000)
SP = STACK + 0xf00
BINDINGS = dict(native_hid_body=STUBS, native_queue=STUBS+4,
                native_lock=STUBS+8, previous_relay_continue=STUBS+12,
                queue_pop=STUBS+16, relay_publish=STUBS+20,
                core_mailbox=MAILBOX)
END = STUBS+24
MAGIC = 0x31504341


def need(condition, message):
    if not condition:
        raise AssertionError(message)


def run(prefix):
    source = Path(__file__).with_name("observer.S")
    with tempfile.TemporaryDirectory(prefix="passive-observer-arm-") as directory:
        output = Path(directory)
        subprocess.run([prefix+"gcc", "-c", str(source), "-o", str(output/"observer.o")], check=True)
        subprocess.run([prefix+"ld", "-Ttext="+hex(CODE), "-e", "capability_callback",
                        *["--defsym="+key+"="+hex(value) for key, value in BINDINGS.items()],
                        str(output/"observer.o"), "-o", str(output/"observer.elf")], check=True)
        subprocess.run([prefix+"objcopy", "-O", "binary", "-j", ".text",
                        str(output/"observer.elf"), str(output/"observer.bin")], check=True)
        code = (output/"observer.bin").read_bytes()
        symbols = {parts[2]: int(parts[0], 16) for line in subprocess.check_output(
            [prefix+"nm", str(output/"observer.elf")], text=True).splitlines()
                   if len(parts := line.split()) == 3}
    need(0 < len(code) <= 0x320, "Unexpected assembly size")
    mu = Uc(UC_ARCH_ARM64, UC_MODE_ARM)
    for address in (CODE, STUBS, STACK, INPUT, PAYLOAD, MAILBOX):
        mu.mem_map(address, 0x1000)
    mu.mem_write(CODE, code)
    for address in (CODE, STUBS):
        mu.mem_protect(address, 0x1000, UC_PROT_READ | UC_PROT_EXEC)
    reads, writes, queue_events, locks = [], [], [], []
    allowed_reads, allowed_writes = [], []
    mode, instructions, maximum, calls, queue_result = "", 0, 0, 0, 0

    def memory_read(_mu, _access, address, size, _value, _user):
        need(any(lo <= address and address+size <= hi for lo, hi in allowed_reads),
             f"Out-of-contract read {address:x}+{size} in {mode}")
        reads.append((address, size))

    def memory_write(_mu, _access, address, size, _value, _user):
        need(any(lo <= address and address+size <= hi for lo, hi in allowed_writes),
             f"Out-of-contract write {address:x}+{size} in {mode}")
        writes.append((address, size))

    def clobber_volatile():
        for index in range(19):
            mu.reg_write(X[index], 0xcafe0000+index)
        mu.reg_write(reg.UC_ARM64_REG_NZCV, 0xa0000000)

    def instruction(_mu, pc, _size, _user):
        nonlocal instructions
        if pc == END:
            mu.emu_stop()
            return
        if CODE <= pc < CODE+len(code):
            instructions += 1
            return
        need(mu.reg_read(reg.UC_ARM64_REG_SP) % 16 == 0, "Misaligned external-call stack")
        if pc == BINDINGS["native_queue"]:
            need(mode == "callback", "Unexpected queue call")
            pointer = mu.reg_read(X[0])
            need(pointer == SP-0x30, "Queue payload is not owned stack storage")
            need([mu.reg_read(X[i]) for i in (1, 2, 3)] == [32, 2, 0x6c], "Wrong queue contract")
            queue_events.append(bytes(mu.mem_read(pointer, 32)))
            continuation = mu.reg_read(X[30])
            clobber_volatile()
            mu.reg_write(X[0], queue_result & 0xffffffff)
            mu.reg_write(reg.UC_ARM64_REG_PC, continuation)
            return
        if pc == BINDINGS["native_lock"]:
            need(mode == "relay" and mu.reg_read(X[0]) == MAILBOX+0x30, "Wrong mailbox lock")
            locks.append(pc)
            continuation = mu.reg_read(X[30])
            clobber_volatile()
            mu.reg_write(reg.UC_ARM64_REG_PC, continuation)
            return
        need(pc in (BINDINGS["native_hid_body"], BINDINGS["previous_relay_continue"],
                    BINDINGS["queue_pop"], BINDINGS["relay_publish"]), "Unbound external transfer")
        mu.emu_stop()  # Native continuations deliberately are NOT executed.

    mu.hook_add(UC_HOOK_CODE, instruction)
    mu.hook_add(UC_HOOK_MEM_READ, memory_read)
    mu.hook_add(UC_HOOK_MEM_WRITE, memory_write)

    def invoke(name, args, extra_regs=()):
        nonlocal instructions, maximum, calls
        reads.clear(); writes.clear(); queue_events.clear(); locks.clear()
        for index in range(31):
            mu.reg_write(X[index], 0xabba0000+index)
        for index, register in enumerate(D):
            mu.reg_write(register, 0x12340000+index)
        for index, value in enumerate(args):
            mu.reg_write(X[index], value)
        for index, value in extra_regs:
            mu.reg_write(X[index], value)
        mu.reg_write(X[30], END)
        mu.reg_write(reg.UC_ARM64_REG_SP, SP)
        mu.mem_write(SP-0x40, b"\xa5"*0x60)
        instructions = 0
        mu.emu_start(symbols[name], END+4, count=200)
        maximum = max(maximum, instructions)
        calls += 1
        need(instructions < 200, "Instruction bound reached")
        need(all(mu.reg_read(register) == 0x12340000+index for index, register in enumerate(D)),
             "Callee-saved SIMD low half changed")
        return mu.reg_read(reg.UC_ARM64_REG_PC)

    def ordinary_return(pc):
        need(pc == END and mu.reg_read(reg.UC_ARM64_REG_SP) == SP, "Stack/return mismatch")
        need(all(mu.reg_read(X[i]) == 0xabba0000+i for i in range(19, 30)), "Callee-saved register changed")

    # Every u16 length, with the readable payload ending at a guard page. The
    # declared response may be extended; only 1 or 9 bytes may actually be read.
    mode = "callback"
    prefix_bytes = bytes([0, 8, 0, 0x34, 0x12, 0, 1, 16, 0])
    for length in range(65536):
        for status in ((0, 17) if length <= 253 else (0,)):
            copied = 0
            readable = b""
            if 1 <= length <= 252:
                copied = 1 if status else (9 if length == 9 or length >= 11 else 0)
                readable = bytes([status]) if status or not copied else prefix_bytes
            pointer = PAYLOAD+0x1000-len(readable)
            if readable:
                mu.mem_write(pointer, readable)
            envelope = INPUT+0x1000-16
            mu.mem_write(envelope, struct.pack("<HHIQ", 0xfd53, length, 0, pointer))
            allowed_reads = [(envelope, envelope+16), (pointer, pointer+len(readable)), (SP-0x30, SP)]
            allowed_writes = [(SP-0x30, SP)]
            queue_result = -1 if length % 2 else 0
            pc = invoke("capability_callback", [envelope])
            ordinary_return(pc)
            expected = struct.pack("<4I", MAGIC, 1, length, copied)
            expected += (readable[:copied] + bytes(12-copied)) + bytes(4)
            need(queue_events == [expected], "Queued metadata differs")
    # Wrong opcode must not read the rest of the envelope or any payload.
    for opcode in (0, 1, 0x1001, 0xfc27, 0xfd57, 0xffff):
        envelope = INPUT+0xffe
        mu.mem_write(envelope, struct.pack("<H", opcode))
        allowed_reads = [(envelope, envelope+2)]
        allowed_writes = []
        ordinary_return(invoke("capability_callback", [envelope]))
        need(not queue_events and not writes, "Unselected callback produced effects")
    allowed_reads = allowed_writes = []
    ordinary_return(invoke("capability_callback", [0]))
    need(not queue_events and not reads, "Null callback read memory")
    envelope = INPUT+0xff0
    mu.mem_write(envelope, struct.pack("<HHIQ", 0xfd53, 9, 0, 0))
    allowed_reads = [(envelope, envelope+16), (SP-0x30, SP)]
    allowed_writes = [(SP-0x30, SP)]
    ordinary_return(invoke("capability_callback", [envelope]))
    need(queue_events == [struct.pack("<4I", MAGIC, 1, 9, 0)+bytes(16)], "Null payload handling")

    # Refusal does not touch a caller-supplied generic payload. Normal HID
    # fallback is checked at the native-body boundary, never run as firmware.
    mode = "legacy"
    allowed_reads = allowed_writes = []
    ordinary_return(invoke("legacy_refuse", [0, 0, 0xdeadbeef]))
    need(mu.reg_read(X[0]) == 0xffffffff and not reads and not writes, "Refusal contract differs")
    for address in (1, 0x010203040506, 0xffffffffffff):
        allowed_writes = [(SP-0x30, SP)]
        pc = invoke("legacy_refuse", [0, address, 0xdeadbeef])
        need(pc == BINDINGS["native_hid_body"] and mu.reg_read(reg.UC_ARM64_REG_SP) == SP-0x30,
             "Normal HID entry contract differs")
        need(mu.reg_read(X[29]) == SP-0x20, "Wrong native frame pointer")
        need(bytes(mu.mem_read(SP-0x20, 32)) == struct.pack("<4Q", 0xabba001d, END, 0xabba0014, 0xabba0013),
             "Wrong native saved frame")

    mode = "relay"
    for event_type in range(256):
        if event_type == 0x6c:
            continue
        allowed_reads = allowed_writes = []
        pc = invoke("metadata_relay", [], [(8, event_type)])
        need(pc == BINDINGS["previous_relay_continue"] and not writes and not locks,
             "Unrelated event did not continue unchanged")
        expected_flags = 0x60000000 if event_type == 0x6b else (0x20000000 if event_type > 0x6b else 0x80000000)
        need(mu.reg_read(reg.UC_ARM64_REG_NZCV) == expected_flags, "Displaced comparison flags differ")
    good_event = struct.pack("<4I", MAGIC, 1, 9, 9)+prefix_bytes+bytes(7)
    for length, event in [(32, good_event), (0, b""), (31, b""), (33, b""),
                          (32, bytes(4)), (32, struct.pack("<2I", MAGIC, 2))]:
        node = INPUT+0x100
        pointer = PAYLOAD+0x1000-len(event)
        mu.mem_write(node+16, struct.pack("<Q", length))
        if event:
            mu.mem_write(pointer, event)
        mu.mem_write(MAILBOX, b"\xa5"*0x500)
        allowed_reads = [(node+16, node+24), (pointer, pointer+len(event))]
        allowed_writes = [(MAILBOX+0x38, MAILBOX+0x60), (MAILBOX+0x440, MAILBOX+0x444)]
        pc = invoke("metadata_relay", [], [(8, 0x6c), (25, node), (28, pointer)])
        if event == good_event:
            need(pc == BINDINGS["relay_publish"] and len(locks) == 1, "Valid relay not published")
            need(bytes(mu.mem_read(MAILBOX+0x38, 40)) == struct.pack("<Q", 32)+event,
                 "Mailbox payload/length mismatch")
            need(bytes(mu.mem_read(MAILBOX+0x440, 4)) == struct.pack("<I", 0x6c), "Mailbox type mismatch")
        else:
            need(pc == BINDINGS["queue_pop"] and not locks and not writes, "Malformed relay touched mailbox")
        need(mu.reg_read(reg.UC_ARM64_REG_SP) == SP, "Relay changed caller stack")
        for index in range(19, 30):
            if index in (25, 28):
                continue
            need(mu.reg_read(X[index]) == 0xabba0000+index, "Relay changed unrelated saved register")
    return dict(status="synthetic_compiled_arm_contracts_passed_not_deployable", calls=calls,
                maximum_own_instructions=maximum, text_bytes=len(code),
                source_sha256=hashlib.sha256(source.read_bytes()).hexdigest(),
                synthetic_text_sha256=hashlib.sha256(code).hexdigest(),
                all_u16_lengths=True, guarded_payload_reads=True,
                queue_success_and_failure_stubbed=True, volatile_registers_clobbered_by_stubs=True,
                native_queue_or_lock_executed=False, native_firmware_loaded=False,
                device_access=False, deployment=False,
                remaining="Native synchronization, startup delivery and companion integration are NOT qualified by this test.")


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--prefix", required=True)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    result = run(args.prefix)
    report = json.dumps(result, indent=2)+"\n"
    if args.output:
        with args.output.open("x") as stream:
            stream.write(report)
    print(report, end="")
