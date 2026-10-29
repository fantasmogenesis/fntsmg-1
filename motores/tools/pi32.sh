#!/bin/sh
# Dentro del contenedor (build.sh pi32): compila cada motor con el toolchain del FM-1 y reporta el tamaño por sección.
set -e
CC=/opt/jieli/pi32v2/bin/clang OD=/opt/jieli/pi32v2/bin/objdump
printf '%-10s %8s %8s %8s %8s\n' objeto .text .rodata .data .bss
for f in fg_tables fg_kick fg_snare fg_metal fg_perc fg_synth fg_mix fg_seq fg_vis; do
    $CC -target pi32v2 -Os -Wall -Isrc -c src/$f.c -o /tmp/$f.o
    t=0 r=0 d=0 b=0
    $OD -h /tmp/$f.o > /tmp/$f.h
    while read -r i name size rest; do
        case "$name" in
            .text*) t=$((t + 0x$size)) ;; .rodata*) r=$((r + 0x$size)) ;; .data*) d=$((d + 0x$size)) ;; .bss*) b=$((b + 0x$size)) ;;
        esac
    done < /tmp/$f.h
    printf '%-10s %8d %8d %8d %8d\n' $f $t $r $d $b
done
# que no se cuele nada de afuera: lo único permitido es memset/memcpy, que clang mete solo (bucles de fg_zero,
# copias de structs) y que el SDK del AC79 ya trae
U=$(for f in /tmp/fg_*.o; do $OD -t $f | grep UND | tr -s ' ' | cut -d' ' -f4-; done | sort -u | grep -v -e '^fg_' -e '^FG_' -e '^memset$' -e '^memcpy$' || true)
[ -n "$U" ] && { echo "externos que no deberían estar: $U"; exit 1; }
echo "pi32: compila"
