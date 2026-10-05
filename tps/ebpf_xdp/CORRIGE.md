# Corrigé enseignant

Distribuer ce fichier après les essais. Les fichiers bugs sont volontairement indépendants afin de garder le phénomène isolé.

## 00 : compilation

Remplacer `XDP_PAS` par `XDP_PASS`. Le verifier n'avait pas encore été invoqué.

## 01 : Ethernet

```c
void *end = (void *)(long)ctx->data_end;
struct ethhdr *eth = data;
if ((void *)(eth + 1) > end) return XDP_PASS;
```

La lecture doit apparaître seulement après cette branche. La politique PASS sur troncature de cette réparation minimale diffère du DROP choisi dans le filtre final.

## 02 : IPv4

```c
struct iphdr *ip = (void *)(eth + 1);
if ((void *)(ip + 1) > end) return XDP_PASS;
```

Le contrôle ne valide que les 20 premiers octets de l'en-tête. Les options exigent une seconde vérification dépendant de IHL.

## 03 : UDP

```c
struct udphdr *udp = (void *)ip + ihl;
if ((void *)(udp + 1) > end) return XDP_PASS;
```

Cette correction répond au rejet mémoire, mais ne traite pas les incohérences de tot_len, les fragments ou les tags VLAN. La solution complète est dans `include/parser.h`.

## 04 : map

```c
__u16 port = 8080;
if (value && *value > 0 && *value <= 65535)
    port = (__u16)*value;
```

La branche non nulle transforme la connaissance du verifier sur le pointeur de map. Une valeur initiale 0 est une configuration par défaut, pas une clé absente.

## 05 : endianness

Retirer `#define LAB_RAW_PORT` du fichier buggy ou réparer la branche correspondante de parser.h :

```c
return ntoh16(dest) == port ? XDP_DROP : XDP_PASS;
```

Équivalent : convertir le port vers l'ordre réseau avant comparaison. Sur little-endian, 8080 (`0x1f90`) lu sans conversion donne `0x901f`, soit 36895. La mémoire est sûre mais l'expression ne représente pas la politique attendue.

## 06 : IHL

Retirer `#define LAB_FIXED_IHL`, ou remplacer `transport_offset = 20` par `transport_offset = ihl`, après validation IHL et bornes. Le cas `ipv4_options_allow` révèle un faux positif. Tester seulement le port bloqué aurait masqué ce défaut.

## 07 : invalidation des pointeurs

Après `adjust_head` réussi :

```c
data = (void *)(long)ctx->data;
end = (void *)(long)ctx->data_end;
eth = data;
if ((void *)(eth + 1) > end) return XDP_PASS;
```

Reconstruire pointeurs et preuves avant toute lecture. Le helper a un contrat d'invalidation même si son argument nul ne change pas la géométrie du paquet ici. L'exercice isole cette règle ; il n'ajoute pas de comportement utile au filtre final.

## Tests et questions supplémentaires

1. Ajouter des compteurs par CPU pour PASS/DROP avec clés constantes. Éviter bpf_printk pour chaque paquet en mesure de performance.
2. Produire une version fautive avec une clé partiellement initialisée passée à un helper. Inspecter le bytecode : le compilateur peut éliminer ou transformer du C à comportement indéfini, donc la preuve d'un test verifier doit porter sur l'objet généré.
3. Une boucle bornée n'est pas nécessairement interdite sur les kernels récents. Ne pas enseigner « eBPF interdit toutes les boucles ». Étudier complexité et exploration des chemins séparément.
4. Une relecture d'un pointeur depuis un champ non validé ne restaure pas automatiquement un type sûr.
5. Un firewall général doit traiter le périmètre IPv6 et les encapsulations que cette solution laisse passer.
