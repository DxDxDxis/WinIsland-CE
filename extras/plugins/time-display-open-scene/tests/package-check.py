import zipfile,hashlib,json,struct,pathlib
root=pathlib.Path.cwd()
with zipfile.ZipFile(root/'time-display.wimod') as z:
    assert sorted(z.namelist())==['LICENSE','README.md','bin/time-display.dll','mod.json']
    assert z.testzip() is None
    m=json.loads(z.read('mod.json'))
    assert m['version']=='1.1.0' and m['id']=='time-display'
    data=z.read(m['entry'])
    assert data==(root/'package/bin/time-display.dll').read_bytes()
    pe=struct.unpack_from('<I',data,60)[0]
    assert data[pe:pe+4]==b'PE\0\0'
    assert struct.unpack_from('<H',data,pe+4)[0]==0x8664
    assert struct.unpack_from('<H',data,pe+22)[0]&0x2000
    assert z.read('README.md')==(root/'README.md').read_bytes()
    print('PASS ZIP, manifest 1.1.0, x64 DLL, packaged DLL and README match')
for file in ['src/time-display.cpp','sdk/mod_api.h','sdk/scene_api.h','package/bin/time-display.dll','time-display.wimod']:
    print(hashlib.sha256((root/file).read_bytes()).hexdigest().upper(), file)
