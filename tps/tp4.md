# Monitoring et loadbalanceur avec eBPF/XDP

Dans ce TP, nous allons construire un mécanisme de monitoring avec eBPF/XDP et implémenter un système de load balancer HTTP.

## Monitoring / Firewall 

L'objectif est de capturer les paquets arrivant sur l'interface principale Ethernet de votre machine et d'afficher ses informations (numéro de séquence, protocole et taille de la payload).

1. Pour cela écrivez un programme eBPF qui affiche ces informations dans le fichier debug du noyau `/sys/kernel/debug/tracing/trace` grâce au helper `bpf_printk`.

2. Modifier le précédent programme afin de remonter ces informations vers un programme s'exécutant en espace utilisateur qui affichera les informations sous forme de tableau avec des timestamps pour chaque ligne.
Pour cela, vous avez plusieurs choix, itérer sur des maps, exploiter les perf_events ou les ring buffer.

3. Modifier votre programme afin de rejetter les paquets arrivant sur le port 3003 et ajouter une colonne "ACCEPT" pour dire si le paquet correspondant a été accepté ou rejeté. (Les paquets arrivant sur le port 3003 doivent affichés "REJET"). 

## Load Balancer HTTP

Modifier votre précédent afin de rediriger les paquets arrivant sur le port 3005 vers deux machines différentes en round robin. 
Concrètement, à l'initialisation votre programme devra charger les adresses mac des deux serveurs à travers un `map`, et rediriger les paquets sur chaque machine à raison d'un paquet par machine. 

Utilisez `iperf` et `tcpdump` pour valider vos programmes. 

