
PHP_ARG_ENABLE(spech, whether to enable spech support,
[  --enable-spech           Enable spech extension])

if test "$PHP_SPECH" != "no"; then
  PHP_NEW_EXTENSION(spech, spech_udp.c, $ext_shared)
fi
