savedcmd_kphd.mod := printf '%s\n'   kphd.o | awk '!x[$$0]++ { print("./"$$0) }' > kphd.mod
