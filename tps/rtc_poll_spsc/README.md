# TP SDN/NFV : run-to-completion, pipeline et communication entre threads

**Public :** M1/M2 systèmes, après gestion des cœurs et introduction aux chemins RX/TX.
**Durée :** deux séances de 3 h, plus un projet optionnel DPDK sur matériel.
**Pré-requis :** Linux, C++17, threads, sockets, files bornées, notions de latence p99.

## 1. La question expérimentale

À budget CPU donné, faut-il que chaque thread reçoive, traite et transmette son paquet, ou faut-il transmettre le paquet à un autre thread ? Comment cette décision évolue-t-elle lorsque les émetteurs, files RX, workers et récepteurs augmentent ?

RTC signifie ici **run-to-completion**. Ce modèle et le pipeline décrivent la répartition du travail. Le polling et l'attente décrivent la stratégie d'inactivité. Une architecture RTC peut utiliser le polling, tout comme une architecture pipeline. Les termes ne désignent donc pas trois alternatives exclusives.

Le TP fournit un forwarder UDP réel en espace utilisateur et trois modèles :

| Modèle | Travail sur les threads | Communication entre threads |
|---|---|---|
| `rtc` | P threads exécutent chacun réception, calcul et émission | Aucune file logicielle intermédiaire |
| `shared` | P threads RX et W workers calcul/TX | Une file bornée MPMC avec mutex |
| `spsc` | P threads RX et W workers calcul/TX | P × W rings SPSC, atomiques acquire/release |

**Important :** ces sockets passent par Linux. Le benchmark ne modifie pas le pilote et ne mesure pas directement un PMD DPDK. Il permet d'observer les coûts de handoff, de contention, de cache et de scheduling au-dessus du chemin réseau. La section DPDK décrit le transfert des mêmes expériences au niveau pilote. Une différence mesurée dans ce premier TP ne suffit pas à conclure sur une NIC à 100 Gb/s.

Le mode `poll` boucle sur des sockets non bloquantes. Le mode `wait` attend la disponibilité avec `poll(2)` pour RX, et attend 50 µs lorsqu'une file de workers est vide. Malgré son nom, **`poll(2)` est ici une attente d'événements**, différente de la boucle active. Cette variante est une référence pédagogique, pas un driver à interruptions ni une implémentation optimale de réveil des workers.

## 2. Topologie et variables indépendantes

Sur une même machine Linux :

1. S threads générateurs émettent vers P ports UDP du forwarder.
2. P threads reçoivent chacun sur leur propre port, 19000 + index.
3. En RTC, le thread RX calcule puis transmet directement.
4. En pipeline, RX dépose le paquet dans une file. Un worker calcule puis transmet.
5. T threads destination reçoivent chacun sur un port 20000 + index.

| Symbole | Signification | Paramètre |
|---|---|---|
| S | Émetteurs de trafic externes au forwarder | `send --threads` |
| P | Threads de réception dans le forwarder | `forward --rx` |
| W | Workers de calcul/TX, pipeline uniquement | `forward --workers` |
| T | Récepteurs de trafic après forwarding | `sink --threads` |
| B | Nombre maximal d'opérations par tour de boucle | `--batch` |
| C | Capacité d'une file logicielle | `--capacity` |
| λ | Débit total cible de tous les émetteurs | `send --rate` |

Les ports P sont des **files logicielles de réception distinctes**, pas des queues matérielles RX. Le choix de port utilise `flow % P`. Le choix du worker SPSC et du sink utilise respectivement `flow % W` et `flow % T`. Un flux conserve donc son affectation pendant un run. Les workers MPMC peuvent traiter successivement les paquets d'un même flux sur des threads différents et provoquer du réordonnancement. Le TP transmet des datagrammes indépendants, sans état TCP.

Chaque émetteur dispose de 64 flux synthétiques. Le nombre de flux est distinct du nombre de threads : augmenter S ne représente pas uniquement augmenter le nombre de connexions. Le mapping modulo est délibérément simple et peut créer des corrélations entre P, W et T. Examiner les compteurs par thread avant d'interpréter une différence de débit.

**Deux protocoles indispensables :**

- **Charge totale fixe :** augmenter S tout en conservant λ. On isole l'effet du parallélisme des émetteurs.
- **Charge par émetteur fixe :** fixer λ = S × λ₀. On mesure la capacité face à une charge croissante.

Faire le même raisonnement pour P, W et T. Un générateur ou un sink saturé ne prouve pas que le forwarder est saturé.

## 3. Installation et premier essai, 30 minutes

Aucun root n'est nécessaire. Les ports sont locaux et le programme ne change aucune configuration réseau.

```bash
make
python3 run.py --preset quick --seconds 2 --repeats 1 --rate 1000 --output results/verification
cat results/verification/results.csv
```

Les trois variantes doivent livrer des datagrammes. Vérifier `sender_errors`, `forward_errors`, `queue_drop`, `residual` et le nombre de paquets reçus. Un débit de 1000 pps suffit à tester le fonctionnement, pas à classer les modèles.

Pour les graphiques, dans votre environnement Python habituel :

```bash
python3 -m pip install matplotlib
python3 plot.py results/verification/results.csv
```

Le fichier `comparison.png` affiche les mesures réelles, moyenne et écart-type entre répétitions. Il n'invente aucune courbe théorique. Pour un preset long, produire les figures par sous-ensemble de configurations plutôt qu'un graphique illisible.

### Contrôler les dimensions sans modifier le script

```bash
# Trafic croissant à 10 000 pps par émetteur : répéter S=1,2,4.
python3 run.py --preset quick --senders 4 --sinks 2 --rate 40000 --seconds 10 --repeats 5 --output results/S4
# P fixe, workers croissants : répéter W=1,2,4.
python3 run.py --preset quick --rx 2 --workers 4 --senders 2 --sinks 2 --rate 50000 --work 1000 --seconds 10 --repeats 5 --output results/W4
```

Avec `--preset traffic`, une override de S/T peut produire des configurations répétées : utiliser `quick` pour des valeurs fixées. Dans RTC, W reste nul et le nombre de threads vaut P. `--batch` du binaire est contrôlé par le preset `handoff` dans le launcher.

### Lancement manuel

Ouvrir trois terminaux. Démarrer la destination puis le forwarder, puis les émetteurs immédiatement :

```bash
./netbench sink --threads 2 --seconds 14 --idle poll
./netbench forward --model spsc --rx 2 --workers 2 --sinks 2 --seconds 13 --capacity 1024 --batch 8 --work 100 --idle poll
./netbench send --threads 2 --rx 2 --seconds 10 --rate 20000 --bytes 256 --flows 64
```

Le programme travaille exclusivement sur 127.0.0.1. Les timestamps utilisent l'horloge monotone de la même machine. Ces timestamps ne permettraient pas de mesurer une latence unidirectionnelle sur deux hôtes non synchronisés.

## 4. Conditions de comparaison, 30 minutes

Relever et joindre au rapport :

```bash
uname -a
lscpu -e=CPU,CORE,SOCKET,NODE
lscpu
cat /proc/sys/net/core/rmem_max
cat /proc/sys/net/core/wmem_max
```

Privilégier un CPU logique par cœur physique au début. Réserver des cœurs séparés pour le générateur, le forwarder et les sinks. Répéter ensuite volontairement avec SMT ou co-localisation.

Le launcher accepte une liste de CPU, allouée sans chevauchement au forwarder, aux émetteurs, puis aux sinks :

```bash
# Exemple seulement : adapter les IDs aux CPU autorisés et à votre topologie.
python3 run.py --preset quick --cpus 0,1,2,3 --seconds 10 --repeats 5 --output results/pinned
```

Ne pas copier ces IDs sur une machine où ils ne sont pas autorisés. Prévoir autant de CPU que le maximum de threads d'une configuration. Sans `--cpus`, l'ordonnanceur place les threads : c'est utilisable pour comprendre le scheduling, mais la migration constitue une variable supplémentaire.

Le mode polling nécessite assez de CPU disponibles. Ne pas interpréter l'oversubscription comme un coût intrinsèque du pipeline. Les scripts n'imposent pas de politique énergétique, d'isolation CPU ou de configuration NUMA. Les enregistrer et les garder constantes. Si `perf` est accessible, observer aussi cycles, instructions, cache-misses et context-switches sur le processus forwarder.

### Budget CPU égal

| Budget du forwarder | RTC | Pipeline |
|---|---|---|
| 2 threads | P = 2 | P = 1, W = 1 |
| 4 threads | P = 4 | P = 1, W = 3 ou P = 2, W = 2 |
| 8 threads | P = 8 | P = 2, W = 6 ou P = 4, W = 4 |

Ne pas présenter RTC P=4 versus pipeline P=4,W=4 comme une comparaison à budget égal. Ajouter des threads RX modifie aussi le nombre de ports d'entrée. Compléter avec une comparaison P fixe où seul W varie.

## 5. Séance 1 : montée en charge et montée en parallélisme

### Expérience A : les sources et destinations, 60 minutes

À λ total constant, balayer S et T dans {1, 2, 4}, puis les modèles :

```bash
python3 run.py --preset traffic --rate 20000 --seconds 10 --repeats 5 --output results/traffic_20k
```

Ce preset balaie aussi P=1,2,4 et W=P pour les pipelines. C'est une exploration, pas une comparaison à budget CPU égal. Commencer avec deux répétitions et cinq secondes pour choisir les configurations utiles. Sur une machine modeste, réduire les listes dans `run.py`.

**Questions :**

1. Le débit émis réalisé reste-t-il proche du débit demandé lorsque S augmente ?
2. Ajouter des sinks améliore-t-il le débit alors que le forwarder ne change pas ?
3. Les pertes sont-elles accompagnées d'une file logicielle pleine ou d'une autre saturation ?
4. Le trafic se répartit-il équitablement entre RX et workers ?

Faire ensuite λ = S × 10000 pps en lançant chaque S séparément avec `--senders S --rate LAMBDA` dans le launcher ou les commandes manuelles. Tracer les deux protocoles sur deux figures distinctes. Les presets utilisent un débit total fixe par exécution.

### Expérience B : RTC versus pipeline, 60 minutes

```bash
python3 run.py --preset budget --rate 50000 --seconds 10 --repeats 5 --output results/budget_50k
```

Les budgets 8 threads demandent une machine adaptée. Retirer ce budget sur les petits postes. Fixer S et T suffisamment hauts pour que les extrémités ne limitent pas les résultats, et garder ces valeurs identiques entre modèles.

Tester `--work 0`, `--work 100` et `--work 1000` dans le launcher. Le calcul dépend du payload et son résultat est consommé pour empêcher sa suppression par le compilateur. Il représente un coût synthétique, pas un firewall complet. Les trois modèles font le même calcul et la même allocation d'un buffer par datagramme reçu.

**Hypothèses à vérifier, sans résultat présupposé :** RTC peut éviter le handoff et conserver la localité. Un pipeline peut rendre service si les étapes ont des coûts déséquilibrés. Une file partagée peut concentrer la contention. Des SPSC peuvent réduire cette contention, mais ajoutent des rings, des scans et des transferts de cache.

Pour chaque modèle, augmenter λ jusqu'à ce que les pertes dépassent un seuil pédagogique choisi, par exemple 0,1 %, ou que p99 dépasse une cible choisie avant mesure. Rapporter cette capacité comme « débit soutenable sous ces critères », et pas seulement le débit maximal obtenu.

## 6. Séance 2 : files, batching et attente

### Expérience C : communication entre threads, 45 minutes

Pour un pipeline P=2,W=2, comparer shared et SPSC. Le code utilise :

- un mutex protégeant une FIFO dans le cas shared ;
- un ring pour chaque paire RX/worker dans le cas SPSC ;
- `release` pour publier un pointeur après initialisation ;
- `acquire` pour lire le pointeur et les données publiées ;
- une politique de drop si la file cible est pleine, sans attente du producteur.

Observer `handoff_p99_us`, mesuré entre réception par RX et début de traitement worker. Il inclut enqueue et scheduling, pas seulement le temps dans le ring.

**Capacité totale égale :** shared C_total=4096, SPSC C_par_ring=floor(4096/(P×W)). Sinon, le pipeline SPSC gagnerait aussi en capacité totale de buffering. Le launcher applique cette règle. Même capacité totale ne signifie pas même comportement : un ring SPSC peut être plein alors qu'un autre est vide.

Changer le budget total avec `--queue-slots 256`, `--queue-slots 1024` et `--queue-slots 4096`. Observer pertes et latence. `queue_peak` est le maximum de l'occupation observée d'une file, pas la somme des queues ni une moyenne temporelle. Étendre le code pour échantillonner occupation moyenne et distribution.

### Expérience D : batching, 45 minutes

```bash
python3 run.py --preset handoff --rate 50000 --seconds 10 --repeats 5 --output results/batching
```

B = 1, 8, 32. **Dans le starter, B limite les opérations d'un tour, mais chaque datagramme utilise encore recv/sendto et chaque handoff une opération de queue.** Le preset ne démontre donc pas le gain d'amortissement de `recvmmsg`, `sendmmsg` ou `rte_ring_*_burst`.

Travail demandé :

1. Implémenter `recvmmsg` non bloquant et `sendmmsg` pour le forwarding.
2. Traiter correctement les transmissions partielles et garder les buffers non transmis sous votre responsabilité.
3. Implémenter un enqueue/dequeue par lot pour shared, avec un verrou par lot.
4. Comparer sans attendre de remplir tout le lot, puis avec un timeout de batching borné.

Ainsi, on sépare la taille maximale d'un burst d'une attente artificielle de remplissage. Les grands lots peuvent réduire un coût par paquet tout en augmentant l'attente à faible charge.

### Expérience E : polling versus attente, 45 minutes

```bash
python3 run.py --preset idle --rate 1000 --seconds 10 --repeats 5 --output results/idle_low
python3 run.py --preset idle --rate 50000 --seconds 10 --repeats 5 --output results/idle_high
```

Comparer latence et CPU à faible charge, puis à forte charge. Le polling peut consommer beaucoup de CPU même sans saturation utile. Une boucle d'attente réduit l'activité mais peut introduire un délai de réveil. Ajouter comme extension un mécanisme de notification condition variable ou eventfd au lieu du sleep fixe du worker.

### Expérience F : bruit et localité, 45 minutes

Garder une configuration et comparer : cœurs physiques distincts, deux siblings SMT, et déplacement du worker sur un autre NUMA node. Sur un petit poste, commencer par co-localiser un émetteur et RX. Consigner les migrations et les autres charges de la machine.

Le starter alloue les buffers dans le thread RX et les transfère par pointeur. Il ne force pas la politique mémoire NUMA. Déplacer seulement un worker ne garantit pas que tous les buffers ont le placement voulu. Relever l'affinité et le placement mémoire, puis préciser la portée de votre conclusion.

## 7. Mesures, calculs et interprétation

Le launcher produit `results.csv` et un `raw.json` par configuration/répétition.

| Mesure | Définition et portée |
|---|---|
| `achieved_send_pps` | Datagrams acceptés par sendto / durée des émetteurs |
| `delivery_pps` | Datagrams livrés au sink / durée des émetteurs, tail inclus |
| `loss_pct` | 100 × (acceptés à l'émission − reçus au sink) / acceptés à l'émission |
| `p50_us`, `p99_us` | Latence sender avant sendto jusqu'au recv du sink, échantillonnée |
| `handoff_p99_us` | RX userspace jusqu'au worker, pipelines uniquement |
| `queue_drop` | Rejets explicites faute de capacité dans les files du pipeline |
| `residual` | Buffers encore présents dans les files après arrêt des workers |
| `sender_errors`, `forward_errors` | Échecs d'appels système, distincts des drops de queue |
| `forward_cpu_s` | Temps CPU user + system attribué au processus forwarder |
| `cpu_s_per_million` | Temps CPU forwarder / paquets livrés × 10⁶ |
| `per_thread_packets` dans raw.json | Volumes par thread, RX puis workers pour pipeline |

L'utilisation CPU moyenne équivalente en cœurs vaut CPU_seconds / wall_seconds. Elle peut dépasser 1 pour un programme multithread. Les durées forwarder/sink incluent leur attente avant et après la génération. Le coût CPU livré du launcher inclut donc cet overhead : garder les durées identiques, utiliser des runs longs et séparer ensuite une fenêtre de mesure active pour une étude fine.

`loss_pct` inclut les pertes dans Linux, les drops de queue, les erreurs au forwarding et les pertes avant le sink. Le compteur seul ne localise pas la perte. Recueillir en complément les compteurs UDP (`nstat` ou `/proc/net/snmp`), les erreurs NIC en matériel et les drops par étage. Le sender utilise une boucle ouverte qui ne dépend pas des réponses. S'il manque son calendrier de plus de 10 ms, il recale l'émission : le débit réalisé doit donc figurer dans le rapport. La mesure de latence démarre à l'émission effective, pas à l'instant prévu.

Les latences sont échantillonnées par un hash de séquence et flux, environ un paquet sur 64. Chaque thread conserve au plus un million d'échantillons. Un percentile sans échantillon vaut -1. Pour p99, viser au moins 10 000 échantillons livrés, annoncer la taille de l'échantillon et augmenter la durée ou choisir `--sample 1` dans le launcher pour les expériences lentes. Les latences décrivent **uniquement les paquets livrés**. Une variante qui perd les paquets lents peut avoir un p99 artificiellement favorable. Vérifier les pertes en même temps.

Le workload utilise de vrais datagrammes avec un en-tête expérimental en ordre d'octets natif, uniquement entre processus locaux issus du même binaire. `--bytes` désigne la taille du payload UDP, pas la taille Ethernet sur le fil. Le starter ne fournit pas de contrôle d'intégrité/duplication ou de séquence par flux au sink : ajouter ces validations pour étudier le réordonnancement. Ne pas utiliser ces résultats comme débit Ethernet physique.

### Figures demandées

1. Débit livré en fonction de S, une courbe par modèle, T/P et budget CPU clairement indiqués.
2. Débit livré en fonction de T, S et P constants.
3. Débit soutenable en fonction du budget CPU forwarder, en comparant les configurations de budget égal.
4. p99 et pertes en fonction de λ, avec débit offert réalisé.
5. Coût CPU par million livré en fonction de λ, polling et attente.
6. Handoff p99 et drops selon capacité/batching, shared et SPSC.

Au moins cinq répétitions indépendantes pour les conclusions. Faire une exécution d'échauffement non conservée pour chaque configuration, puis répéter. Le launcher randomise l'ordre pour éviter une progression systématique avec la chauffe de la machine ; il ne fournit pas de phase warmup interne. Ne pas inventer les valeurs d'une courbe attendue.

## 8. Extension DPDK : même question au niveau du driver

**Matériel :** DUT avec NIC compatible, plusieurs queues RX/TX et assez de cœurs, générateur et récepteur distincts du DUT. Un générateur à deux ports peut jouer les deux extrémités. Une VM avec virtio peut servir à la fonctionnalité mais limite les conclusions sur matériel physique. Réserver une interface distincte pour administrer le DUT.

Utiliser une version DPDK déterminée et conserver son numéro, le PMD, la NIC, le firmware et les réglages. Partir de `l3fwd` pour comprendre RTC, puis construire le pipeline avec la ring library et la même fonction de forwarding. Un simple drop, un forwarder L2 et un routeur L3 n'effectuent pas le même travail.

### Architecture et règles de propriété

**RTC :** un lcore possède sa queue RX, récupère les mbufs, applique la fonction et transmet sur sa queue TX.

**Pipeline :** P lcores possèdent leurs queues RX. Ils déposent les **pointeurs de mbuf** dans des rings logiciels. W workers appliquent la même fonction puis transmettent. Chaque worker dispose de queues TX non partagées, sauf si le PMD garantit explicitement le mode de partage utilisé.

```c
/* RTC : même lcore RX / traitement / TX */
nb = rte_eth_rx_burst(in_port, rx_queue, mbufs, B);
process_same_function(mbufs, nb);
sent = rte_eth_tx_burst(out_port, tx_queue, mbufs, nb);
/* libérer ou conserver pour retry borné les mbufs [sent, nb) */

/* Pipeline : lcore RX */
nb = rte_eth_rx_burst(in_port, rx_queue, mbufs, B);
accepted = rte_ring_enqueue_burst(ring, (void **)mbufs, nb, NULL);
/* libérer les mbufs [accepted, nb), compter les drops */

/* Pipeline : worker */
nb = rte_ring_dequeue_burst(ring, (void **)mbufs, B, NULL);
process_same_function(mbufs, nb);
sent = rte_eth_tx_burst(out_port, tx_queue, mbufs, nb);
/* gérer les transmissions partielles comme en RTC */
```

Le choix MPMC permet plusieurs producteurs et consommateurs. Un ring configuré SP/SC impose un producteur et un consommateur : pour P×W, créer un ring par paire et borner le scan des rings. Ne jamais donner un ring SP/SC à plusieurs producteurs. Après enqueue réussi, le producteur ne modifie plus le mbuf. Après TX accepté, le driver récupère la propriété du buffer. Ces règles conditionnent la correction.

### Matrice matérielle

- S et T = 1,2,4 threads générateur/récepteur, en vérifiant qu'ils ne saturent pas.
- Budget DUT = 2,4,8 cœurs physiques selon disponibilité.
- Trafic = 64,256,1500 octets **trame Ethernet**, préciser si FCS inclus par l'outil.
- Mélange de flux = un flux, 64 flux, puis plusieurs milliers, RSS constant.
- Burst = 1,8,32,64, sans attente de remplissage dans la mesure de base.
- Charge = 10 %,50 %,90 % de capacité, puis rampe jusqu'au seuil de perte.
- Mempools locaux à la NIC, puis placement NUMA distant volontaire.

Un seul flux RSS peut arriver sur une seule queue malgré plusieurs lcores. Vérifier `rte_eth_stats`, xstats par queue, `rx_nombuf`, compteurs rings et transmissions partielles. Une queue RX doit appartenir à un seul lcore dans cette architecture. Le nombre de queues dépend de la NIC et du PMD ; ne pas annoncer automatiquement 8 queues disponibles.

Pour un link 10 Gb/s et des trames Ethernet 64 octets FCS inclus, la capacité théorique est environ 14,88 Mpps avec préambule/SFD et IFG. Ce calcul ne constitue pas un résultat mesuré. La latence physique nécessite des timestamps correctement synchronisés ou une méthode RTT explicitée. L'eBPF attaché à la pile Linux ne voit pas automatiquement les paquets qui passent exclusivement dans un PMD.

### Extension Linux/NAPI

NAPI combine habituellement notification initiale puis polling avec budget. Comparer IRQ affinity, budget, coalescing NIC et busy polling exige une expérience distincte. Le mode `wait` de ce starter ne simule pas exactement NAPI. Conserver la sémantique de forwarding et les réglages d'offload constants.

## 9. Question finale


Pour un edge gateway à faible trafic, un load balancer à petits paquets et un VNF à traitement coûteux, quelle architecture choisir, à quel budget CPU, et quelle télémétrie surveiller pour changer cette décision ? Une recommandation différente pour chaque cas est recevable si elle s'appuie sur vos mesures.

## 10. Sources primaires

- DPDK, modèles RTC/pipeline, PMD et ownership : https://static.dpdk.org/doc/guides/prog_guide/ethdev/ethdev.html
- DPDK, rings SP/SC et MP/MC : https://doc.dpdk.org/guides/prog_guide/ring_lib.html
- Linux, NAPI : https://docs.kernel.org/networking/napi.html
- Linux man-pages, sockets et attente : https://man7.org/linux/man-pages/man7/socket.7.html
- Linux man-pages, réception par lots : https://man7.org/linux/man-pages/man2/recvmmsg.2.html

