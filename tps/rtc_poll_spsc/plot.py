#!/usr/bin/env python3
"""Plots measured values only; matplotlib required."""
import csv, pathlib, sys, collections, statistics
import matplotlib.pyplot as plt
path=pathlib.Path(sys.argv[1] if len(sys.argv)>1 else 'results/results.csv')
rows=list(csv.DictReader(path.open()))
metrics=[('delivery_pps','Débit livré (paquets/s)'),('p99_us','Latence p99 échantillonnée (µs)'),('loss_pct','Pertes (%)'),('cpu_s_per_million','CPU secondes / million livré')]
fig,axs=plt.subplots(2,2,figsize=(15,10),layout='constrained')
for ax,(metric,title) in zip(axs.flat,metrics):
 groups=collections.defaultdict(list)
 for r in rows:
  # A plot is a complete configuration comparison, not a collapsed scaling curve.
  key=(r['model'],r['rx'],r['workers'],r['senders'],r['sinks'],r['batch'],r['idle'],r['rate'])
  val=float(r[metric])
  if val>=0:groups[key].append(val)
 keys=sorted(groups);xs=range(len(keys));ys=[statistics.mean(groups[k]) for k in keys];err=[statistics.stdev(groups[k]) if len(groups[k])>1 else 0 for k in keys]
 ax.errorbar(xs,ys,yerr=err,fmt='o',capsize=3);ax.set_title(title);ax.set_xticks(list(xs));ax.set_xticklabels([f'{k[0]} P{k[1]} W{k[2]}\nS{k[3]} T{k[4]} B{k[5]} {k[6]}\n{k[7]} pps' for k in keys],rotation=90,fontsize=7);ax.grid(alpha=.25)
fig.savefig(path.parent/'comparison.png',dpi=160)
print(path.parent/'comparison.png')
