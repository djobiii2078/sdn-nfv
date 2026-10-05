#!/usr/bin/env python3
import argparse,subprocess,sys
p=argparse.ArgumentParser();p.add_argument('map_pin');p.add_argument('port',type=int);a=p.parse_args()
if not 1<=a.port<=65535:p.error('Port must be 1..65535')
key=(0).to_bytes(4,sys.byteorder);value=a.port.to_bytes(4,sys.byteorder)
subprocess.run(['bpftool','map','update','pinned',a.map_pin,'key','hex',*[f'{b:02x}' for b in key],'value','hex',*[f'{b:02x}' for b in value]],check=True)
