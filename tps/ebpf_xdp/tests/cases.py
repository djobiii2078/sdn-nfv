#!/usr/bin/env python3
"""Packet parsing unit tests or BPF_PROG_TEST_RUN, no raw network injection."""
import argparse,json,pathlib,struct,subprocess
ROOT=pathlib.Path(__file__).resolve().parents[1]
def frame(proto=17,dport=8080,ihl=5,vlan=0,frag=0,version=4,total=None,tcp_doff=5):
    eth=b'\x02\x00\x00\x00\x00\x02'+b'\x02\x00\x00\x00\x00\x01'
    tags=b''
    if vlan:
        eth+=struct.pack('!H',0x8100)
        for i in range(vlan):tags+=struct.pack('!HH',100,0x8100 if i<vlan-1 else 0x0800)
    else:eth+=struct.pack('!H',0x0800)
    # Zeroed options are deliberate: they reveal fixed-offset misparsing.
    options=b'\0'*max(0,(ihl-5)*4)
    payload=b'test'
    if proto==17:l4=struct.pack('!HHHH',53000,dport,8+len(payload),0)+payload
    elif proto==6:l4=struct.pack('!HHIIBBHHH',53000,dport,1,0,tcp_doff<<4,2,4096,0,0)+payload
    else:l4=b'\0'*8
    ip=struct.pack('!BBHHHBBH4s4s',(version<<4)|ihl,0,total if total is not None else 20+len(options)+len(l4),1,frag,64,proto,0,b'\x0a\0\0\x01',b'\x0a\0\0\x02')
    return eth+tags+ip+options+l4

def cases():
    v=[('udp_block',frame(),1),('udp_allow',frame(dport=8081),2),('tcp_block',frame(proto=6),1),('tcp_allow',frame(proto=6,dport=8081),2),('ipv4_options_allow',frame(ihl=6,dport=8081),2),('ipv4_options_block',frame(ihl=6),1),('vlan_block',frame(vlan=1),1),('qinq_allow',frame(vlan=2,dport=8081),2),('mf_fragment_allow_port',frame(frag=0x2000,dport=8081),1),('later_fragment',frame(frag=1),1),('bad_version',frame(version=6),1),('short_ihl',frame(ihl=4),1),('bad_total_small',frame(total=19),1),('bad_total_large',frame(total=2000),1),('tcp_doff_small',frame(proto=6,tcp_doff=4),1),('tcp_doff_large',frame(proto=6,tcp_doff=15),1),('icmp_pass',frame(proto=1),2),('empty',b'',1),('short_eth',b'\0'*13,1),('short_ip',frame()[:33],1),('short_udp',frame()[:38],1),('short_vlan',frame(vlan=1)[:16],1),('arp_pass',b'\0'*12+b'\x08\x06'+b'\0'*28,2),('ipv6_pass',b'\0'*12+b'\x86\xdd'+b'\0'*40,2),('three_tags_pass',frame(vlan=3),2)]
    bad=bytearray(frame());bad[38:40]=struct.pack('!H',7);v.append(('udp_bad_length',bytes(bad),1))
    # Ethernet padding must not let a transport header hide beyond IP total length.
    v.append(('transport_in_padding',frame(total=20),1))
    return v

def main():
    a=argparse.ArgumentParser();g=a.add_mutually_exclusive_group();g.add_argument('--native');g.add_argument('--bpf',help='Pinned XDP program, requires privileges');a.add_argument('--generate-only',action='store_true');a.add_argument('--skip-short',action='store_true',help='Skip frames shorter than Ethernet for BPF TEST_RUN');args=a.parse_args()
    folder=ROOT/'build/fixtures';folder.mkdir(parents=True,exist_ok=True);expected={};failed=0;executed=0;skipped=0
    for name,data,want in cases():
        f=folder/(name+'.bin');f.write_bytes(data);expected[name]=want
        if args.generate_only or not(args.native or args.bpf):continue
        if args.bpf and args.skip_short and len(data)<14:
            skipped+=1;print(f'SKIP {name}: less than 14 bytes');continue
        executed+=1
        if args.native:
            r=subprocess.run([str(pathlib.Path(args.native).resolve()),str(f)],text=True,capture_output=True,check=True);got=int(r.stdout)
        else:
            r=subprocess.run(['bpftool','-j','prog','run','pinned',args.bpf,'data_in',str(f),'repeat','1'],text=True,capture_output=True,check=True);got=json.loads(r.stdout)['retval']
        ok=got==want;failed+=not ok;print(f'{"PASS" if ok else "FAIL"} {name}: got {got}, expected {want}')
    (folder/'expected.json').write_text(json.dumps(expected,indent=2))
    if args.native or args.bpf:print(f'{executed-failed}/{executed} passed; {skipped} skipped');raise SystemExit(bool(failed))
if __name__=='__main__':main()
