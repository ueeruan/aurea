import pathlib, subprocess, json
import argparse
parser=argparse.ArgumentParser(description='Generate original CFR/VFR + AAC playback fixtures; development only.')
parser.add_argument('--ffmpeg', required=True)
parser.add_argument('--output', required=True)
args=parser.parse_args()
root=pathlib.Path(args.output);root.mkdir(parents=True,exist_ok=True)
ff=args.ffmpeg
cases=[('h264-720p30',1280,720,30,'libx264'),('h264-1080p30',1920,1080,30,'libx264'),('h264-1080p60',1920,1080,60,'libx264'),('hevc-1080p30',1920,1080,30,'libx265'),('h264-1080p-vfr',1920,1080,60,'libx264')]
for name,w,h,fps,codec in cases:
 out=root/(name+'.mp4')
 cmd=[ff,'-y','-hide_banner','-f','lavfi','-i',f'testsrc2=size={w}x{h}:rate={fps}:duration=20','-f','lavfi','-i','sine=frequency=440:sample_rate=48000:duration=20','-c:v',codec,'-preset','ultrafast','-crf','26','-threads','3','-pix_fmt','yuv420p','-g',str(fps*2),'-bf','2','-c:a','aac','-ac','2','-b:a','160k','-movflags','+faststart']
 if codec=='libx265':cmd+=['-x265-params','pools=3:frame-threads=2','-tag:v','hvc1']
 if 'vfr' in name:cmd+=['-vf',"select='if(lt(t,6),not(mod(n,2)),1)'",'-fps_mode','vfr']
 with (root/(name+'-encode.log')).open('w') as log:subprocess.run(cmd+[str(out)],stdout=log,stderr=log,check=True)
 print(name, out.stat().st_size,flush=True)
(root/'cases.json').write_text(json.dumps(cases))
