cmd_/home/aniruddha_deb_2002/sig_mod/sig_mod.mod := printf '%s\n'   sig_mod.o | awk '!x[$$0]++ { print("/home/aniruddha_deb_2002/sig_mod/"$$0) }' > /home/aniruddha_deb_2002/sig_mod/sig_mod.mod
