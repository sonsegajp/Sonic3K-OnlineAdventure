"""Regenerate the selected native hooks from the owner's ROM, never edit emitted C.
Requires the pinned v0.5.2 source and its GenesisRecomp compiler.
"""
from pathlib import Path
import argparse,json,re,shutil,subprocess,zlib
p=argparse.ArgumentParser();p.add_argument('--sonic-root',type=Path,required=True);p.add_argument('--recompiler',type=Path,required=True);p.add_argument('--rom',type=Path,required=True);p.add_argument('--build',type=Path,default=Path('build'));a=p.parse_args()
root=Path(__file__).resolve().parents[1];spec=json.loads((root/'config/guest-hooks.json').read_text());out=a.build.resolve()/'generated';inputs=a.build.resolve()/'guest-input';stage=a.build.resolve()/'guest-stage';inputs.mkdir(parents=True,exist_ok=True);stage.mkdir(parents=True,exist_ok=True);out.mkdir(parents=True,exist_ok=True)
rom=a.rom.read_bytes()
if len(rom)!=0x400000 or zlib.crc32(rom)!=0x63522553:raise SystemExit('The combined World ROM must be 4 MiB with CRC32 63522553.')
source=a.sonic_root.resolve()/'game/sonic3k'
for file in source.iterdir():
 if file.suffix in ('.toml','.csv','.txt'):shutil.copy2(file,inputs/file.name)
text=(inputs/'game.toml').read_text();m=re.search(r'(?ms)(^\[functions\].*?\bextra\s*=\s*\[)(.*?)(\])',text)
if not m:raise SystemExit('Missing [functions].extra in pinned configuration')
text=text[:m.start(2)]+m[2]+'\n'+','.join(hex(x) for x in spec['continuations'])+',\n'+text[m.end(2):]
existing=set(int(m[1],0) for m in re.finditer(r'(?m)^addr\s*=\s*(0x[0-9a-fA-F]+|\d+)',text))
for pc in spec['instruction_sites']:
 if pc not in existing:text+='\n[[widescreen_site]]\naddr = '+hex(pc)+'\nkind = "game_hook"\n'
(inputs/'game.toml').write_text(text)
# ROM stays at the owner's chosen path; it is not copied into the source package.
subprocess.run([str(a.recompiler.resolve()),str(a.rom.resolve()),'--game','game.toml','--output-dir',str(stage)],cwd=inputs,check=True)
functions={}
for file in stage.glob('sonic3k_part*.c'):
 content=file.read_text()
 for match in re.finditer(r'(?ms)^void func_([0-9A-F]+)\(void\) \{.*?^\}',content):
  pc=int(match[1],16)
  if pc in spec['entries']:functions[pc]=match[0]
missing=set(spec['entries'])-functions.keys()
if missing:raise SystemExit('Missing native hook entries: '+str(sorted(missing)))
text='#include "stock_bindings.h"\n#include "genesis_runtime.h"\n\n'+'\n\n'.join(functions[x] for x in spec['entries'])+'\n'
(out/'guest_hooks.c').write_text(text)
# Pointer table is data-only and contains no cartridge implementation.
shutil.copy2(root/'config/guest_table.c',out/'guest_table.c')
print('Prepared',len(functions),'native hooks from the supplied ROM')
