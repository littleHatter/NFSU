"""Build the diagnostic desktop executable on Linux with g++ and Unicorn 2.1.4."""
import pathlib,subprocess,sys
import unicorn
root=pathlib.Path(__file__).resolve().parent;package=pathlib.Path(unicorn.__file__).resolve().parent
if not sys.platform.startswith('linux'):raise SystemExit('This build helper is currently tested only on Linux.')
source=['main_probe.cpp','loader.cpp','heap.cpp','runtime_s3e.cpp','cpu_unicorn.cpp','cpu_native.cpp','third_party/lzma/LzmaDec.c']
cmd=['g++','-std=c++17','-O2','-I'+str(package/'include')]+source+['-L'+str(package/'lib'),'-Wl,-rpath,'+str(package/'lib'),'-l:libunicorn.so.2','-ldl','-o','nfsuc_probe']
subprocess.run(cmd,cwd=root,check=True)
print('Built',root/'nfsuc_probe')
