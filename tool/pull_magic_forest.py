"""Copy only this artifact's files from the Aurea Android sandbox."""
import subprocess, sys, json
from pathlib import Path
root=Path(__file__).resolve().parents[1]/'output/floresta-magica'
adb=r'C:\Users\SnyX\AppData\Local\Android\sdk\platform-tools\adb.exe'
base=[adb,'-s','emulator-5556','exec-out','run-as','com.aurea.aurea','cat']
names=sys.argv[1:] or ['status.json']+[f'frame-{f:03}.png' for f in [0,45,89,90,135,179,180,225,269,270,315,359,360,405,449]]
for name in names:
    assert '/' not in name and '\\' not in name and '..' not in name
    with (root/name).open('wb') as f:
        subprocess.run(base+['app_flutter/forest-render/'+name],stdout=f,check=True)
print(json.dumps([{'name':n,'bytes':(root/n).stat().st_size} for n in names]))
