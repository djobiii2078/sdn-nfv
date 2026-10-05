#!/usr/bin/env python3
import argparse,selectors,socket
p=argparse.ArgumentParser();p.add_argument('mode',choices=['server','client']);a=p.parse_args()
if a.mode=='server':
    sel=selectors.DefaultSelector()
    for port in (8080,8081):
        s=socket.socket(socket.AF_INET,socket.SOCK_DGRAM);s.bind(('10.23.0.2',port));sel.register(s,selectors.EVENT_READ)
    print('UDP echo on 8080 and 8081',flush=True)
    while True:
        for key,_ in sel.select():
            data,addr=key.fileobj.recvfrom(2048);key.fileobj.sendto(data,addr)
else:
    for port in (8080,8081):
        with socket.socket(socket.AF_INET,socket.SOCK_DGRAM) as s:
            s.settimeout(.5);s.sendto(b'verifier-lab',('10.23.0.2',port))
            try:
                data,_=s.recvfrom(2048);print(port,'DELIVERED',data)
            except socket.timeout:print(port,'TIMEOUT (requires baseline to attribute to XDP)')
