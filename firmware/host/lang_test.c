/* cada texto de la tabla, traducido por fg_tr (entero, por tramos y por pedazos), en los tres idiomas */
#include <stdio.h>
#include <string.h>
#include "fg_lang.h"
int main(void)
{
    static const char *T[] = {"BOMBO", "RANURA 3", "SINTE A · PATRÓN 12", "\1CAJA", "CAJA · \1CAJA", "3 PALMAS", "1 DE 4", "SIERRA·CUAD",
        "MANTÉN = GUARDAR ENCIMA", "1/16 TRESILLO", "−2 OCT 127", "DESTINO 2", "SAMPLE · BOMBOS", "NO CUPO EN 8 KB", "LOCK · 3 7", "ALGO QUE NO ESTÁ"};
    for (int l = 0; l < 3; l++) { fg_lang = (uint8_t)l; for (unsigned i = 0; i < sizeof T / sizeof *T; i++) { char o[44]; fg_tr(T[i], o, sizeof o); printf("%s%s", i ? " | " : "", o); } printf("\n"); }
    /* que ninguna traducción pase de 43 bytes (el búfer de la pantalla) */
    FILE *f = fopen("../../motores/tools/lang.tsv", "r"); char ln[512]; int bad = 0;
    while (fgets(ln, sizeof ln, f)) { if (ln[0] == '#') continue; char *a = strtok(ln, "\t\n"), *b = strtok(0, "\t\n"), *c = strtok(0, "\t\n");
        for (int l = 1; l < 3; l++) { fg_lang = (uint8_t)l; char o[64]; fg_tr(a, o, sizeof o); const char *want = l == 1 ? b : c; if (strcmp(o, want)) { printf("MAL «%s» -> «%s» (quería «%s»)\n", a, o, want); bad++; } if (strlen(want) > 43) { printf("LARGO %s\n", want); bad++; } } }
    printf(bad ? "FALLAS %d\n" : "toda la tabla bien\n", bad);
    return bad;
}
