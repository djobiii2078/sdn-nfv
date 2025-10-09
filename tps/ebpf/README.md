# Introduction à eBPF

Dans ce TP, on va écrire un mini-programme eBPF pour bloquer les paquets arrivant sur des ports spécifiques.
L'objectif est d'avoir deux machines: un client et un serveur.  
Le client émet des paquets vers le serveur, et le serveur les bloquera en fonction des ports d'envoi.
Dans ce TP, on manipulera eBPF, Iperf3, Docker et Tcpdump.
Vous travaillerez en binôme pour le client. 

## Mise à jour de l'environement

Sur la machine serveur, lancer la commande ci-dessous: 

```
apt update && \
    apt install -y \
    linux-headers-$(uname -r) \
    build-essential \
    g++ \
    gcc \
    libelf-dev \
    libzstd-dev \
    make \
    autoconf \
    automake \
    autotools-dev \
    libtool \
    pkg-config \
    sudo \
    iproute2 \
    iputils-ping \
    iptables \
    curl \
    iperf3 \
    net-tools \
    tcpdump \
    git \
    python3 \
    python3-pip \
    nano \
    python3-scapy \
    tmux \
    wget \
    libbpfcc-dev \
    libbpf-dev \
    llvm \
    clang \
    gcc-multilib \
    build-essential \
    linux-tools-$(uname -r) \
    linux-tools-common \
    linux-tools-generic
```

La machine cliente n'a besoin que d'iperf3 et de tcpdump. 


## Connexion et filtrage de port 

Sur votre container "server", lancer un serveur iperf3 sur le port 8003 avec la commande `iperf3 -s -p 8003`. 
Sur votre container client, lancer un paquet vers l'adresse du serveur en précisant le port: `iperf3 -c 10.0.2.18 -p 8003 -t 120`. Si votre architecture est correcte, vous verrez les résultats de latence et de débit sur le client et le serveur. 

Maintenant, on va écrire un programme eBPF pour filtrer les paquets et refuser ceux qui arrivent sur le port 8003. 

Créer un fichier `ebpf_drop.c`: 

```
#include <linux/bpf.h>
#include <linux/if_ether.h>
#include <linux/ip.h>
#include <linux/tcp.h>
#include <linux/udp.h>
#include <linux/pkt_cls.h>    
#include <bpf/bpf_helpers.h>


#define DROP_PORT 8080

#ifndef IPPROTO_TCP
#define IPPROTO_TCP 6
#endif
#ifndef IPPROTO_UDP
#define IPPROTO_UDP 17
#endif

#ifndef bpf_htons
#define bpf_htons(x) __builtin_bswap16(x)
#endif
#ifndef bpf_ntohs
#define bpf_ntohs(x) __builtin_bswap16(x)
#endif
#ifndef bpf_htonl
#define bpf_htonl(x) __builtin_bswap32(x)
#endif
#ifndef bpf_ntohl
#define bpf_ntohl(x) __builtin_bswap32(x)
#endif

SEC("tc")
int tc_drop_port(struct __sk_buff *skb) {
    void *data     = (void *)(long)skb->data;
    void *data_end = (void *)(long)skb->data_end;

    // parse ethernet header
    struct ethhdr *eth = data;
    if ((void *)(eth + 1) > data_end)
        return XDP_PASS;

    // only IPv4
    if (bpf_ntohs(eth->h_proto) != ETH_P_IP)
        return XDP_PASS;

    // ip header
    struct iphdr *ip = data + sizeof(*eth);
    if ((void *)(ip + 1) > data_end)
        return XDP_PASS;

    // compute transport header offset
    __u32 ihl = ip->ihl * 4;
    void *trans = (void *)ip + ihl;
    if (trans > data_end)
        return XDP_PASS;

    __u16 dst_port = 0;
    __u8 proto = ip->protocol;

    if (proto == IPPROTO_TCP) {
        struct tcphdr *tcp = trans;
        if ((void *)(tcp + 1) > data_end)
            return XDP_PASS;
        dst_port = bpf_ntohs(tcp->dest);
    } else if (proto == IPPROTO_UDP) {
        struct udphdr *udp = trans;
        if ((void *)(udp + 1) > data_end)
            return XDP_PASS;
        dst_port = bpf_ntohs(udp->dest);
    } else {
        return XDP_PASS;
    }

     if (dst_port == DROP_PORT) {
        // drop the packet
        return XDP_DROP;
    }


    return XDP_PASS;
}

char _license[] SEC("license") = "GPL";
```

Compilons ce code avec `clang -O2 -g -target bpf -c ebpf_drop.c -o ebpf_drop.o` 

Attachons ce code pour filtrer les paquets entrant sur l'interface principale de votre serveur (ici je suppose `eth0`): 

```
sudo ip link set dev eth0 xdp obj ebpf_drop.o sec xdp
```
Corriger les erreurs du code qui sont signalés par le checkeur ebpf. 

Retester la connexion avec `iperf3` et commenter vos observations avec `tcpdump` sur les deux machines.

## Le rendre generique avec les Maps

Nous allons mettre à jour le code afin de ne plus dépendre d'une définition statique des ports à bloquer. 
Nous allons écrire du code en espace utilisateur qui remplira une map de ports à bloquer, et le programme ebpf se basera dessus pour bloquer un paquet. 

Rajoutons la définition du map ainsi: 

```
struct {
    __uint(type, BPF_MAP_TYPE_HASH);
    __uint(max_entries, 512);
    __type(key, __u16);
    __type(value, __u8);
} blocked_ports SEC(".maps");

```

et remplacer le code du drop de paquet avec ce code qui vérifier si le port de destination est dans le map

```
 __u8 *found = bpf_map_lookup_elem(&blocked_ports, &dst_port);
    if (found) {
        // drop packet
        return XDP_DROP;
    }
``` 

Place au code utilisateur (update_port.c) pour remplir et retirer un port du map: 

```
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <bpf/bpf.h>
#include <bpf/libbpf.h>

#define MAP_PATH "/sys/fs/bpf/xdp/globals/blocked_ports"

int main(int argc, char **argv) {
    if (argc < 3) {
        fprintf(stderr, "Usage: %s <add|del|list> <port>\n", argv[0]);
        return 1;
    }

    int map_fd = bpf_obj_get(MAP_PATH);
    if (map_fd < 0) {
        perror("bpf_obj_get");
        return 1;
    }

    if (strcmp(argv[1], "add") == 0) {
        __u16 key = atoi(argv[2]);
        __u8 value = 1;
        if (bpf_map_update_elem(map_fd, &key, &value, BPF_ANY) < 0) {
            perror("bpf_map_update_elem");
            return 1;
        }
        printf("Blocked port %u\n", key);
    } else if (strcmp(argv[1], "del") == 0) {
        __u16 key = atoi(argv[2]);
        if (bpf_map_delete_elem(map_fd, &key) < 0) {
            perror("bpf_map_delete_elem");
            return 1;
        }
        printf("Unblocked port %u\n", key);
    } else if (strcmp(argv[1], "list") == 0) {
        __u16 key, next_key;
        __u8 value;
        int res = bpf_map_get_next_key(map_fd, NULL, &key);
        while (res == 0) {
            if (bpf_map_lookup_elem(map_fd, &key, &value) == 0) {
                printf("Blocked: %u\n", key);
            }
            res = bpf_map_get_next_key(map_fd, &key, &next_key);
            key = next_key;
        }
    } else {
        fprintf(stderr, "Invalid command. Use add|del|list.\n");
        return 1;
    }

    close(map_fd);
    return 0;
}
```

Pour compiler ce programme, `gcc -o update_port update_port.c -lbpf`.

Réattacher le nouveau filtreur ebpf sur l'interface principale du serveur, puis jouer avec le programme `update_port`.

`./update_port add 8004`
`./update_port del 8004`
`./update_port list`

## A vous de jouer 

Modifier le programme utilisateur afin qu'il mette à jour le map en bouclant sur un fichier contenant tous les ports à bloquer. 
Supposons que, dans ce fichier, tous les ports soient listés sur une ligne après l'autre. 




