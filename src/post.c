#include "post.h"
#include "lpc4350.h"

void post_add(post_log *L, const char *name, post_result r, uint32_t detail) {
    if (L->n < POST_MAX) { L->e[L->n].name = name; L->e[L->n].result = r;
                           L->e[L->n].detail = detail; L->n++; }
}

/* Decode PLL1_CTRL to the M4 core clock in kHz. Assumes 12 MHz crystal. */
post_result post_clock(post_log *L) {
    uint32_t ctrl = CGU_PLL1_CTRL;
    uint32_t stat = CGU_PLL1_STAT;
    uint32_t msel = ((ctrl >> 16) & 0xFF) + 1;      /* M divider */
    uint32_t nsel = ((ctrl >> 12) & 0x3) + 1;       /* N divider */
    uint32_t psel = ((ctrl >> 8)  & 0x3);           /* P (post)  */
    uint32_t direct = (ctrl >> 7) & 1;
    uint32_t fin = 12000;                            /* kHz, crystal */
    uint32_t fout = direct ? (fin * msel / nsel)
                           : (fin * msel / nsel / (1u << (psel+1)));
    post_result r = (stat & 1) ? POST_PASS : POST_FAIL;   /* PLL locked? */
    post_add(L, "CGU PLL1 lock+freq(kHz)", r, fout);
    return r;
}

post_result post_chipid(post_log *L) {
    uint32_t id = REG32(CREG_BASE + 0x200);         /* CREG CHIPID */
    post_result r = (id == 0x4906002B || id == 0x7906002B) ? POST_PASS : POST_FAIL;
    post_add(L, "CREG CHIPID", r, id);
    return r;
}

/* Walking-bit + address-uniqueness march over external SDRAM. */
post_result post_sdram(post_log *L) {
    volatile uint32_t *m = (volatile uint32_t *)SDRAM_BASE;
    const uint32_t words = 0x100000;                /* test first 4 MiB */
    uint32_t bad = 0;
    for (uint32_t i = 0; i < words; ++i) m[i] = i * 2654435761u;   /* Knuth hash */
    for (uint32_t i = 0; i < words; ++i) if (m[i] != i * 2654435761u) { bad = i; break; }
    /* walking ones at base */
    for (int b = 0; b < 32; ++b) { m[0] = 1u << b; if (m[0] != (1u << b)) { bad = 0xF0000000u | b; break; } }
    post_result r = (bad == 0) ? POST_PASS : POST_FAIL;
    post_add(L, "SDRAM march (4MiB)", r, bad);
    return r;
}
