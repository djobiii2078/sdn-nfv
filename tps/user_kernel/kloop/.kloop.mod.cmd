savedcmd_/home/bmvondod/kloop/kloop.mod := printf '%s\n'   kloop.o | awk '!x[$$0]++ { print("/home/bmvondod/kloop/"$$0) }' > /home/bmvondod/kloop/kloop.mod
