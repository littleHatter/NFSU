"""Run from any directory: python3 tests/build_and_run.py /path/to/game."""
import pathlib, subprocess, sys, tempfile
import unicorn
root=pathlib.Path(__file__).resolve().parents[1]
pkg=pathlib.Path(unicorn.__file__).resolve().parent
game=pathlib.Path(sys.argv[1]).resolve()
exe=pathlib.Path(tempfile.gettempdir())/'nfsuc_runtime_probe'
subprocess.run(['g++','-std=c++17','-O2','-DMGS_GLES','-Ithird_party/gles1','-I.','-I'+str(pkg/'include'),'tests/runtime_probe.cpp','loader.cpp','runtime_s3e.cpp','gles.cpp','hw_host.cpp','heap.cpp','cpu_unicorn.cpp','cpu_native.cpp','third_party/lzma/LzmaDec.c','-L'+str(pkg/'lib'),'-Wl,-rpath,'+str(pkg/'lib'),'-l:libunicorn.so.2','-ldl','-lz','-l:libEGL.so.1','-l:libGL.so.1','-o',str(exe)],cwd=root,check=True)
for base in ['4a000000','09510000']:
 subprocess.run([str(exe),str(game/'nfsuc.xe3u'),str(game),str(exe)+'_saves_'+base,base],cwd=root,check=True)
