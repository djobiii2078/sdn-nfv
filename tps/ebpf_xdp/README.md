# TP : comprendre le verifier eBPF en réparant un filtre XDP

**Durée :** 3 h, extension 1 h. **Public :** M1/M2, après la séance sur les en-têtes réseau.
**Objectif :** rejeter les paquets IPv4 TCP/UDP à destination du port 8080, sans rejeter les paquets ordinaires vers 8081. Le port devient modifiable par map dans la solution.

Le TP sépare trois diagnostics : erreur de compilation C, rejet du bytecode par le verifier, et défaut de logique d'un programme accepté. Les diagnostics du kernel dépendent de sa version et des instructions produites par Clang : les messages ci-dessous sont des familles d'erreurs attendues, pas des transcriptions d'une exécution garantie.

## 1. Installation et environnement, 15 min

Utiliser une VM Linux récente, par exemple Ubuntu 24.04, avec accès root et fonctionnalités BPF/XDP activées. Un conteneur peut bloquer BPF ou les namespaces malgré un UID root. Les capacités et la politique de sécurité de l'hôte restent déterminantes.

```bash
sudo apt-get update
sudo apt-get install clang llvm gcc make linux-libc-dev bpftool iproute2 python3 curl
uname -r
clang --version
bpftool version
sudo bpftool feature probe kernel
```

Les sources fournissent un petit header de helpers BPF autonome, basé sur les identifiants UAPI Linux. Elles n'exigent pas de header `vmlinux.h` ni de génération de skeleton. On utilise des maps BTF (`.maps`) : les outils de chargement doivent supporter ce format. En production, préférer les headers libbpf et un loader entretenu.

En cas d'erreur `asm/types.h` introuvable, vérifier le chemin multiarch :

```bash
gcc -dumpmachine
make ARCH_INCLUDE=/usr/include/x86_64-linux-gnu
```

Adapter sur ARM. Compiler en `-O2 -g` : le verifier examine le bytecode généré, pas le texte C. Ne pas conclure à un problème du verifier si la compilation ou la création de map échoue d'abord.

## 2. Avant de modifier du code

### Le chemin de validation

1. Clang compile le programme C en bytecode BPF.
2. Le loader lit l'ELF, crée les maps et prépare le chargement.
3. Le kernel vérifie les instructions, les types de pointeurs, les bornes et les chemins d'exécution.
4. Un programme accepté peut être exécuté avec `BPF_PROG_TEST_RUN`, puis attaché à XDP.
5. Les tests de trafic déterminent s'il implémente la politique attendue.

Une preuve d'accès mémoire valide n'est pas une preuve de politique réseau correcte.

### Lire un log

Identifier l'instruction de load fautive, son registre pointeur, sa taille et son offset. Chercher le dernier contrôle de bornes qui devrait protéger cette instruction. Pour chaque branche, préciser les propriétés acquises.

| État ou famille de message | Interprétation |
|---|---|
| `pkt`, `pkt_end` | Pointeur vers les données ou leur fin |
| `invalid access to packet` | Pas de preuve suffisante pour la lecture demandée |
| `map_value_or_null` | Résultat de lookup dont la nullité n'a pas été exclue |
| `invalid mem access` | Type ou propriétés du pointeur insuffisants |
| `r=14`, par exemple | Étendue prouvée accessible, dans le contexte du log |

Les registres `R1`, `R2`, etc. ne correspondent pas durablement à des noms de variables C. Utiliser aussi le désassemblage et les lignes debug.

## 3. Compiler et recueillir les rejets, 15 min

```bash
make
make compile-bug        # échec volontaire de l'étape 00
llvm-objdump -S build/bugs/03_missing_udp_check.bpf.o
```

`make` compile les autres étapes, y compris les programmes que le verifier doit rejeter. Il ne les charge pas et ne prétend pas qu'ils sont corrects.

Vérifier que `/sys/fs/bpf` est un filesystem BPF. S'il n'est pas monté, dans votre VM de TP :

```bash
mount | rg '/sys/fs/bpf'   # ou grep si rg absent
sudo mount -t bpf bpf /sys/fs/bpf   # seulement si le filesystem BPF n'est pas déjà monté
```

Choisir un nouveau répertoire de pinning, sans objet préexistant. Exemple pour la première erreur verifier :

```bash
mkdir -p logs
sudo mkdir /sys/fs/bpf/tp-verifier-01
sudo bpftool -d prog load build/bugs/01_no_bounds.bpf.o \
  /sys/fs/bpf/tp-verifier-01/prog type xdp \
  >logs/01.out 2>logs/01.verifier.log
cat logs/01.verifier.log
```

Un retour non nul est attendu pour cette version. `-d` demande des diagnostics détaillés, y compris le log verifier sur échec. Le loader peut s'arrêter avant cette étape : `Operation not permitted`, un type de map non disponible ou une erreur ELF doivent être distingués d'un accès paquet rejeté. Pour les étapes avec map, utiliser un nouveau dossier et ajouter `pinmaps /sys/fs/bpf/NOUVEAU_DOSSIER/maps` après avoir créé `maps`.

## 4. Progression guidée, 90 min

Les fichiers sont des points de départ indépendants. Les étapes 01 à 03 cumulent les contrôles précédents ; 04 à 07 isolent une nouvelle famille de défauts. Pour chaque étape, travailler sur une copie afin de conserver la version fautive.

### Étape 00 : le programme n'atteint pas le verifier

**Fichier :** `bugs/00_compile.bpf.c`.

```c
return XDP_PAS;
```

**À faire :** corriger l'identifiant, compiler, puis charger. Quelle différence entre cette erreur et un log kernel ?

**Correction :** `XDP_PASS`. Ce programme accepte tout : compilation et chargement réussis ne satisfont pas encore l'objectif de filtrage.

### Étape 01 : lire Ethernet sans preuve de bornes

**Fichier :** `bugs/01_no_bounds.bpf.c`.

```c
struct ethhdr *eth = data;
if (ntoh16(eth->h_proto) == ETH_P_IP) return XDP_DROP;
```

**Prévision :** rejet d'accès au paquet. Le verifier ne suppose pas qu'une trame Ethernet complète existe.

**Indice :** la lecture d'EtherType touche deux octets à l'offset 12. Prouver que 14 octets sont accessibles avant la lecture.

**À faire :** ajouter `data_end` puis le contrôle. Écrire ce que le verifier sait sur la branche qui poursuit.

### Étape 02 : le contrôle Ethernet ne protège pas IPv4

**Fichier :** `bugs/02_only_ethernet.bpf.c`.

```c
if ((void *)(eth + 1) > end) return XDP_PASS;
struct iphdr *ip = (void *)(eth + 1);
return ip->protocol == 17 ? XDP_DROP : XDP_PASS;
```

**Prévision :** rejet lors de la lecture de `ip->protocol`.

**À faire :** ajouter le contrôle de l'en-tête IPv4 minimum avant de lire `version`, `ihl` ou `protocol`. Que représente `ip + 1` en C ? Pourquoi vérifier `ip` seul n'est-il pas suffisant ?

### Étape 03 : offset variable et accès UDP

**Fichier :** `bugs/03_missing_udp_check.bpf.c`.

```c
__u32 ihl = (__u32)ip->ihl * 4;
if ((void *)ip + ihl > end) return XDP_PASS;
struct udphdr *udp = (void *)ip + ihl;
return ntoh16(udp->dest) == 8080 ? XDP_DROP : XDP_PASS;
```

**Prévision :** rejet. Le contrôle protège l'en-tête IP, pas les octets après cet en-tête.

**À faire :** prouver l'existence de l'en-tête UDP avant sa lecture. Pourquoi `ip->ihl` doit-il être au moins 5 ? Quelle est sa borne supérieure de représentation ?

**Attention :** après ajout de la borne UDP, le programme peut être accepté alors qu'il ne valide pas `tot_len`, les fragments ou le champ UDP Length. L'acceptation n'achève pas le TP.

### Étape 04 : un résultat de map peut être nul

**Fichier :** `bugs/04_nullable_map.bpf.c`.

```c
__u32 *value = lookup(&config, &key);
__u16 port = *value;
```

**Prévision :** rejet de déréférencement du résultat nullable de lookup.

**À faire :** tester le pointeur. Définir le comportement pour une valeur nulle et une configuration absente/non valide. Le type ARRAY et une clé fixe ne remplacent pas la preuve attendue sur le retour du helper.

**Contrat du TP :** valeur 0 ou hors de 1..65535 signifie port par défaut 8080. Une valeur valide change le port bloqué.

### Étape 05 : accepté, mais mauvais ordre des octets

**Fichier :** `bugs/05_endian.bpf.c`. Le défaut se trouve dans `include/parser.h` sous `LAB_RAW_PORT`.

```c
return dest == port ? XDP_DROP : XDP_PASS;
```

**Prévision :** accepté si les autres conditions de plateforme sont réunies. Sur little-endian, le filtre ne bloque pas le bon port.

**À faire :** comparer cette variante avec la solution sur UDP et TCP, ports 8080 et 8081. Corriger la comparaison. Calculer les deux octets réseau de 8080 : `1f 90`. Quel entier le CPU little-endian obtient-il sans conversion ?

**Nuance :** sur big-endian cette erreur ne se manifeste pas de la même manière. Toujours annoncer l'architecture du test.

### Étape 06 : accepté, mais mauvaise position de l'en-tête transport

**Fichier :** `bugs/06_fixed_ihl.bpf.c`. Le défaut est sous `LAB_FIXED_IHL`.

```c
__u32 transport_offset = 20;
```

**Prévision :** accepté, car les lectures sont bornées. Avec options IPv4, le programme prend des octets d'options pour l'en-tête TCP/UDP.

**À faire :** tester des paquets `IHL=6`. Vérifier notamment qu'un paquet légitime vers 8081 reste accepté. Dans la fixture fournie, la mauvaise lecture de UDP Length peut entraîner un drop collatéral : le programme bloque alors bien 8080 mais bloque aussi 8081. Réparer l'offset avec `ihl * 4`.

### Étape 07, bonus : le helper invalide la preuve précédente

**Fichier :** `bugs/07_stale_pointer.bpf.c`.

```c
if ((void *)(eth + 1) > end) return XDP_PASS;
if (adjust_head(ctx, 0)) return XDP_PASS;
return ntoh16(eth->h_proto) == ETH_P_IP ? XDP_DROP : XDP_PASS;
```

**Prévision :** rejet lié à un pointeur ou une preuve d'accès devenue invalide. Même avec le déplacement nul de cet exercice, le contrat de ce helper oblige à renouveler les pointeurs et vérifications.

**À faire :** après le helper réussi, relire `ctx->data` et `ctx->data_end`, reconstruire `eth`, et refaire le contrôle. Ne pas conclure que tous les helpers invalident tous les pointeurs : raisonner à partir du contrat du helper concerné.

## 5. Tests de paquets sans interface, 20 min

### Niveau A : tests du parseur natif

```bash
make check
python3 tests/cases.py --native build/native-endian
python3 tests/cases.py --native build/native-ihl
```

Le premier doit réussir les 27 cas. Les variantes fautives doivent produire des différences sur les cas pertinents. Sur little-endian, `native-endian` laisse passer des ports qui devraient être bloqués. `native-ihl` rejette notamment le paquet avec options vers le port autorisé.

Ces tests compilent exactement le parseur C partagé, **pas un programme eBPF** : ils ne font pas intervenir le verifier. Les fixtures sont synthétiques, sans checksums IP/TCP valides, car le parseur n'en vérifie pas les checksums. Elles servent à tester les décisions et les bornes, pas à prouver leur acceptation par la pile IP.

### Niveau B : exécution du programme XDP chargé

```bash
sudo mkdir /sys/fs/bpf/tp-verifier-final
sudo mkdir /sys/fs/bpf/tp-verifier-final/maps
sudo bpftool -d prog load build/solution/filter.bpf.o \
  /sys/fs/bpf/tp-verifier-final/prog type xdp \
  pinmaps /sys/fs/bpf/tp-verifier-final/maps
sudo python3 tests/cases.py --bpf /sys/fs/bpf/tp-verifier-final/prog --skip-short
```

`bpftool prog run` utilise `BPF_PROG_TEST_RUN` : il exécute un programme chargé sur le buffer fourni, sans injecter la trame sur le réseau. Dans ce TP, `XDP_DROP=1`, `XDP_PASS=2`.

`--skip-short` exclut explicitement les deux fixtures plus courtes que 14 octets de TEST_RUN, tout en les conservant dans les tests natifs. Certains kernels refusent un buffer vide ou trop court dans TEST_RUN avant d'exécuter le programme. Dans ce cas le script s'arrête avec l'erreur système. Consigner ce cas comme **test non exécutable via cette API**, pas comme échec logique du parseur ni comme drop produit par XDP. Garder les tests natifs pour les buffers très courts et sélectionner les fixtures acceptées par l'API pour la validation BPF.

Si une instruction BPF est rejetée dans la solution sur votre plateforme, recueillir le log complet et l'environnement : la validation native ne remplace pas le chargement kernel. Ne pas masquer ce problème en retirant les cas de parsing concernés.

## 6. Attacher le filtre et vérifier le trafic, 20 min

Les scripts créent deux namespaces et un lien veth dédiés. Ils refusent de réutiliser des noms existants. L'attachement se fait sur **l'entrée du namespace destination**, pas sur l'interface physique du poste.

```bash
sudo bash scripts/lab.sh up
sudo ip netns exec tp-vf-src ping -c 2 10.23.0.2
```

Dans un premier terminal :

```bash
sudo ip netns exec tp-vf-dst python3 scripts/traffic.py server
```

Dans un deuxième terminal, baseline :

```bash
sudo ip netns exec tp-vf-src python3 scripts/traffic.py client
```

Les deux ports doivent répondre. Si la baseline échoue, résoudre le réseau/service avant d'accuser XDP.

Attacher le programme déjà chargé en mode générique :

```bash
sudo ip netns exec tp-vf-dst ip link set dev tp-vf-b xdpgeneric \
  pinned /sys/fs/bpf/tp-verifier-final/prog
sudo ip netns exec tp-vf-src python3 scripts/traffic.py client
```

Attendu : timeout sur 8080, réponse sur 8081. Un timeout seul ne prouve pas un drop XDP : il doit être comparé à la baseline et aux décisions de TEST_RUN. Un drop XDP peut également être invisible dans une capture réalisée plus loin dans la pile.

Changer le port sans recompiler :

```bash
sudo python3 scripts/set_port.py /sys/fs/bpf/tp-verifier-final/maps/config 8081
sudo ip netns exec tp-vf-src python3 scripts/traffic.py client
```

Attendu : réponse sur 8080, timeout sur 8081. **Les fixtures du script de tests supposent 8080**, donc rétablir la map avant de les rejouer :

```bash
sudo python3 scripts/set_port.py /sys/fs/bpf/tp-verifier-final/maps/config 8080
```

### TCP, extension

Dans un autre terminal, lancer un serveur TCP :

```bash
sudo ip netns exec tp-vf-dst python3 -m http.server 8080 --bind 10.23.0.2
```

Depuis la source :

```bash
sudo ip netns exec tp-vf-src curl --noproxy '*' --connect-timeout 1 http://10.23.0.2:8080/
```

Vérifier d'abord sans filtre, puis avec filtre. UDP et TCP ont chacun leur espace de ports : le serveur UDP 8080 ne bloque pas le bind du serveur TCP 8080.

## 7. Politique et limites à expliquer

La solution est un **filtre pédagogique ciblé**, pas un firewall complet :

- Ethernet II, jusqu'à deux tags VLAN, IPv4, TCP et UDP.
- Options IPv4 et TCP : longueurs variables validées.
- Port destination bloqué, indépendamment du port source.
- ARP, IPv6, autres protocoles IP et encapsulations plus profondes passent.
- Tous les fragments IPv4 TCP/UDP sont rejetés, même vers 8081 : c'est une politique conservatrice explicite, avec drops collatéraux.
- Les paquets reconnus mais tronqués/malformés sont rejetés.
- Le parseur exige ici UDP Length égal à la longueur du payload IPv4. Ce choix restrictif fait partie du contrat des tests.
- Pas de validation des checksums, pas de réassemblage, pas de suivi de connexion, pas de parsing d'extensions IPv6, pas de support de buffers XDP multi-fragments.

**Discussion :** autoriser simplement les fragments non initiaux ne permet pas de garantir une politique par port. Bloquer tous les fragments TCP/UDP couvre ce cas au prix de trafic légitime. Pour une politique générale, choisir un point où le réassemblage existe, ou ajouter un mécanisme avec état dont les limites sont documentées.

`data_end` représente les octets accessibles au programme. `tot_len`, UDP Length et TCP Data Offset expriment les limites logiques des protocoles. Un accès dans du padding Ethernet peut être mémoire-sûr tout en étant incorrect du point de vue IP.

## 8. Nettoyage

Détacher, arrêter les serveurs avec Ctrl-C, puis supprimer les namespaces créés par le TP :

```bash
sudo ip netns exec tp-vf-dst ip link set dev tp-vf-b xdpgeneric off
sudo bash scripts/lab.sh down
```

Le script refuse de supprimer un namespace où des processus sont encore présents. Supprimer ensuite uniquement les pins du TP :

```bash
sudo rm /sys/fs/bpf/tp-verifier-final/prog
sudo rm /sys/fs/bpf/tp-verifier-final/maps/config
sudo rmdir /sys/fs/bpf/tp-verifier-final/maps
sudo rmdir /sys/fs/bpf/tp-verifier-final
```

Pour les étapes rejetées, le dossier peut être vide ou contenir une map créée avant le rejet : inspecter puis supprimer seulement les objets du TP. Ne pas vider `/sys/fs/bpf` globalement. Ne pas faire `make clean` avant `lab.sh down`, car le marqueur d'appartenance des namespaces se trouve dans `build`.

## 9. Question finale


 « Le verifier accepte mon programme, donc mon firewall est correct. » Donner deux contre-exemples tirés du TP et les tests nécessaires pour les révéler.

## 10. Sources et validation du paquet

- Kernel Linux, verifier : https://docs.kernel.org/bpf/verifier.html
- Kernel Linux, cycle de chargement libbpf : https://docs.kernel.org/bpf/libbpf/libbpf_overview.html
- iproute2, options XDP : https://man7.org/linux/man-pages/man8/ip-link.8.html
- Documentation locale : `bpftool prog help`, `bpftool map help`, `ip link help`.

