#!/usr/bin/env python3
"""Independent runs; launcher never changes host/network configuration."""
import argparse, csv, json, subprocess, time, pathlib, random
ROOT=pathlib.Path(__file__).resolve().parent

def cmd(role, **kw):
    return [str(ROOT/'netbench'),role]+[x for k,v in kw.items() for x in ('--'+k.replace('_','-'),str(v))]

def run_case(c, seconds, directory, cpus=None):
    procs=[]
    try:
        cpu_kw={}
        if cpus:
            n=c['rx'] if c['model']=='rtc' else c['rx']+c['workers']
            if len(cpus)<n+c['senders']+c['sinks']: raise ValueError('Not enough CPUs for disjoint pinning')
            cpu_kw={'forward':','.join(map(str,cpus[:n])), 'send':','.join(map(str,cpus[n:n+c['senders']])), 'sink':','.join(map(str,cpus[n+c['senders']:n+c['senders']+c['sinks']]))}
        common={'idle':c['idle'],'sample':c['sample']}
        def launch(role,**kw):
            if role in cpu_kw: kw['cpus']=cpu_kw[role]
            p=subprocess.Popen(cmd(role,**kw),stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True)
            procs.append(p);return p
        # Sink and forwarder include idle lead-in and tail time. Normalize delivery by sender duration.
        sink=launch('sink',threads=c['sinks'],seconds=seconds+2,**common)
        forward=launch('forward',seconds=seconds+1.5,rx=c['rx'],workers=c['workers'],sinks=c['sinks'],model=c['model'],capacity=c['capacity'],batch=c['batch'],work=c['work'],**common)
        time.sleep(.25)
        for p in (sink,forward):
            if p.poll() is not None: raise RuntimeError(p.communicate()[1])
        sender=launch('send',threads=c['senders'],rx=c['rx'],rate=c['rate'],seconds=seconds,bytes=c['bytes'],flows=64)
        results={}
        for role,p in [('send',sender),('forward',forward),('sink',sink)]:
            out,err=p.communicate(timeout=seconds+8)
            if p.returncode: raise RuntimeError(f'{role}: {err}')
            results[role]=json.loads(out)
        sent=results['send']['packets'];received=results['sink']['packets'];f=results['forward'];r=results['sink'];tx=results['send']
        row=dict(c, sent=sent,received=received,achieved_send_pps=tx['pps'],delivery_pps=received/tx['seconds'],loss_pct=100*(1-received/sent) if sent else 100,sender_errors=tx['errors'],forward_errors=f['errors'],queue_drop=f['queue_drop'],residual=f['residual'],queue_peak=f['queue_peak'],p50_us=r['p50_us'],p99_us=r['p99_us'],latency_samples=r['latency_samples'],handoff_p99_us=f['p99_us'],forward_cpu_s=f['cpu_seconds'],forward_wall_s=f['seconds'],cpu_s_per_million=f['cpu_seconds']*1e6/received if received else -1)
        directory.mkdir(parents=True,exist_ok=True)
        (directory/'raw.json').write_text(json.dumps({'config':c,'results':results},indent=2))
        return row
    finally:
        for p in procs:
            if p.poll() is None: p.kill();p.communicate()

def main():
    ap=argparse.ArgumentParser();ap.add_argument('--preset',choices=['quick','traffic','budget','handoff','idle'],default='quick');ap.add_argument('--seconds',type=float,default=5);ap.add_argument('--repeats',type=int,default=3);ap.add_argument('--rate',type=int,default=20000);ap.add_argument('--cpus',help='Disjoint physical CPU IDs, comma separated');ap.add_argument('--work',type=int,default=100);ap.add_argument('--bytes',type=int,default=256);ap.add_argument('--sample',type=int,default=64);ap.add_argument('--queue-slots',type=int,default=4096);ap.add_argument('--senders',type=int);ap.add_argument('--sinks',type=int);ap.add_argument('--rx',type=int);ap.add_argument('--workers',type=int);ap.add_argument('--output',default='results');ap.add_argument('--seed',type=int,default=42);a=ap.parse_args()
    cpus=list(map(int,a.cpus.split(','))) if a.cpus else None
    if a.seconds<=0 or a.repeats<1 or a.rate<1: ap.error('Positive seconds, repeats and rate required')
    cases=[]
    for model in ['rtc','shared','spsc']:
        pairs=[(1,1)] if a.preset in ['quick','idle'] else [(1,1),(2,2),(4,4)]
        if a.preset=='budget': pairs=[(2,0),(4,0),(8,0)] if model=='rtc' else [(1,1),(1,3),(2,2),(2,6),(4,4)]
        for p,w in pairs:
            traffic=[(s,t) for s in [1,2,4] for t in [1,2,4]] if a.preset=='traffic' else [(1,1)]
            for s,t in traffic:
                for batch in ([1,8,32] if a.preset=='handoff' else [1]):
                    for idle in (['poll','wait'] if a.preset=='idle' else ['poll']):
                        # 4096 slots shared across the entire queue matrix, not per ring.
                        rp=a.rx if a.rx else p; rw=a.workers if a.workers else w
                        cap=a.queue_slots//(rp*rw) if model=='spsc' else a.queue_slots
                        if cap<1: raise ValueError('Queue budget smaller than number of SPSC rings')
                        cases.append(dict(model=model,senders=a.senders if a.senders else s,sinks=a.sinks if a.sinks else t,rx=rp,workers=rw if model!='rtc' else 0,capacity=cap,batch=batch,idle=idle,rate=a.rate,bytes=a.bytes,work=a.work,sample=a.sample))
    tasks=[dict(c,repeat=r) for r in range(a.repeats) for c in cases];random.Random(a.seed).shuffle(tasks)
    dest=pathlib.Path(a.output);dest.mkdir(parents=True,exist_ok=True)
    with (dest/'results.csv').open('w',newline='') as f:
        writer=None
        for i,c in enumerate(tasks):
            print(f'{i+1}/{len(tasks)} {c}',flush=True)
            row=run_case(c,a.seconds,dest/f'run-{i:04d}',cpus)
            if writer is None:writer=csv.DictWriter(f,fieldnames=list(row));writer.writeheader()
            writer.writerow(row);f.flush()
    print(dest/'results.csv')
if __name__=='__main__':main()
