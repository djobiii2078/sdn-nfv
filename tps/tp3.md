# Evaluation des unikernels

Les unikernels sont des systèmes d'exploitation allegés et spécialisé pour une application. 
Compiler un unikernel de zéro n'est pas une tâche évidente donc il est important de se familiariser avec des unikernels existants et explorer leur structure.
Dans ce TP, nous allons compiler l'unikernel mini-os et le lancer avec l'hyperviseur Xen.
Puis, il vous incombera la tâche de compiler et exécuter les unikernels que vous avez listé dans vos recherches. 

## Test de MiniOS

L'architecture de MiniOS est décrite ici [https://wiki.xenproject.org/wiki/Mini-OS-DevNotes](https://wiki.xenproject.org/wiki/Mini-OS-DevNotes).
En fonction des périphériques associé à MiniOS, son comportement peut varier, mais l'OS boucle sur le système de stockage et écrivant des données aléatoires, afficher les paquets réseau entrant, et maintenir une horloge.
Pour obtenir une image de MiniOS, suivez les étapes ci-dessous:

- Obtenir le code source de MiniOS: `git clone https://github.com/xen-project/mini-os.git`
- Télechargez LwIP 1.3.2 (une implémentation légère du protocole TCP/IP) : `wget https://download.savannah.nongnu.org/releases/lwip/older_versions/lwip-1.3.2.zip`
- Décompressez l'archive LwIP: `unzip lwip-1.3.2.zip`
- Compilez le noyau de MiniOS: `make LWIPDIR=$(pwd)/lwip-1.3.2`
- Le noyau compilé aura le nom **mini-os.gz**

Afin de lancer MiniOS, nous devons installer l'hyperviseur Xen. Vous pouvez reprendre les instructions de l'année dernière [https://github.com/djobiii2078/cloud_course_resources/tree/main/TP/TP1](https://github.com/djobiii2078/cloud_course_resources/tree/main/TP/TP1).
Une fois Xen installé, créer le fichier de configuration suivant `minios.cfg`:

```
kernel = "mini-os.gz"
# 32 MB de mémoire
memory = 32
name = "Mini-OS"
on_crash = 'destroy'
```

Lancer l'unikernel MiniOS avec la commande `xl create -c minios.cfg`. 

- Rajouter un disque et une interface réseau virtuelle (avec un pont réseau) à MiniOS
- Relancer MiniOS et examinez son comportement.

## Avec vos unikernels

Recuperez les manuels d'installation de 3 de vos unikernels (compatible avec Xen), compiler, et lancer avec Xen.
Je vous conseille *ClickOS, MirageOS, et HalVM*

## Temps de démarrage

Réalisez une évaluation où vous démarrez vos unikernels (miniOS inclus) 10 fois et comparez les résultats obtenus avec le 
temps de démarrage de votre serveur. Idéalement, tracer une courbe où on verra la moyenne, l'écart-type et la médiane pour chaque unikernel.

