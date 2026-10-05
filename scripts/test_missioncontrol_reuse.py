#!/usr/bin/env python3
"""Test reuse against an explicit, existing pinned upstream checkout.

Only compiles/runs project-owned host tests. No downloads or console operations.
"""
import argparse
import hashlib
from pathlib import Path
import shlex
import subprocess

PIN='71bbe2d4e97d618775b5a3e2f91bfeaf52d8b75e'
BUTTONS_SHA='e82ad438b0f4fb3c076a93ab2c944951e99fab225a678ae2c2998457df652dde'
ROOT=Path(__file__).resolve().parents[1]

def main():
    parser=argparse.ArgumentParser()
    parser.add_argument('--source',required=True,type=Path)
    parser.add_argument('--cxx',default='c++')
    args=parser.parse_args()
    source=args.source.resolve()
    if subprocess.check_output(['git','rev-parse','HEAD'],cwd=source,text=True).strip()!=PIN:
        raise RuntimeError('Explicit upstream checkout is not the reviewed pin')
    buttons=source/'mc_mitm/source/controllers/switch_buttons.hpp'
    if hashlib.sha256(buttons.read_bytes()).hexdigest()!=BUTTONS_SHA:
        raise RuntimeError('Upstream button implementation differs from reviewed public source')
    out=ROOT/'tools/amazon-remote-module/build/reuse-test';out.mkdir(parents=True,exist_ok=True)
    target=out/'test_missioncontrol_buttons'
    command=shlex.split(args.cxx)+['-std=c++20','-O1','-g','-Wall','-Wextra','-Werror',
        '-fsanitize=address,undefined','-fno-omit-frame-pointer',
        '-I'+str(ROOT/'host_tests/missioncontrol_shim'),'-I'+str(buttons.parent),
        str(ROOT/'host_tests/test_missioncontrol_buttons.cpp'),
        str(ROOT/'mc_mitm/source/amazon_remote/remote_decoder.cpp'),'-o',str(target)]
    subprocess.run(command,check=True);subprocess.run([str(target)],check=True)
    print('Verified unchanged upstream file SHA256='+BUTTONS_SHA+'; compile/execution passed; hardware untouched.')

if __name__=='__main__':main()
