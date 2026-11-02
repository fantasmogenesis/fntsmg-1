#!/bin/sh
# Las 16 ondas de TABLA (fg_cand.c) como tabla fija para la flash del aparato: motores/src/fg_cand_tab.c.
# En la Mac y en la web se calculan al arrancar; aquí se calculan igual y se escriben. Correr si cambian las ondas.
set -e
cd "$(dirname "$0")/.."
M=../motores/src
T=$(mktemp -d)
cat > "$T/dump.c" <<'C'
#include <stdio.h>
#include <stdint.h>
const int16_t *fg_cand_wt(void);
int main(void)
{
    const int16_t *w = fg_cand_wt();
    printf("/* Generado por firmware/tools/gen_cand_tab.sh: las ondas de TABLA (fg_cand.c). No editar a mano. */\n#include <stdint.h>\n");
    printf("const int16_t FGC_WT[16][257] = {\n");
    for (int k = 0; k < 16; k++) {
        printf("    {");
        for (int j = 0; j < 257; j++) printf("%d%s", w[k * 257 + j], j < 256 ? "," : "");
        printf("}%s\n", k < 15 ? "," : "");
    }
    printf("};\n");
    return 0;
}
C
cc -O1 -std=gnu99 -I"$M" -o "$T/dump" "$T/dump.c" "$M/fg_cand.c" "$M/fg_tables.c"
"$T/dump" > "$M/fg_cand_tab.c"
rm -rf "$T"
echo "-> $M/fg_cand_tab.c"
