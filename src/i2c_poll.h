/* i2c_poll.h - the LPC4350's two I2C ports without interrupts, for the smoke
 * images. Include after lpc4350.h.
 *
 * As the stock application sets them up: bus 0 on the dedicated I2C0 pads
 * (SFSI2C0 = 0x808), bus 1 on P2_3 (SDA) and P2_4 (SCL), function 1, 0xC1;
 * 400 kHz. It writes to address 0x2A on bus 0 (the footswitch LEDs) and 0x40
 * on bus 1, and listens as a slave on both: its own address register is
 * loaded with 0x80 on bus 0 and 0x54 on bus 1. Whether its addresses are
 * 7-bit or already shifted has not been settled; i2c_probe() finds out. */
#define I2C_BASE(bus)   ((bus) ? 0x400E0000u : 0x400A1000u)
#define I2C_CONSET(b)   REG32(I2C_BASE(b) + 0x00)
#define I2C_STAT(b)     REG32(I2C_BASE(b) + 0x04)
#define I2C_DAT(b)      REG32(I2C_BASE(b) + 0x08)
#define I2C_ADR0(b)     REG32(I2C_BASE(b) + 0x0C)
#define I2C_SCLH(b)     REG32(I2C_BASE(b) + 0x10)
#define I2C_SCLL(b)     REG32(I2C_BASE(b) + 0x14)
#define I2C_CONCLR(b)   REG32(I2C_BASE(b) + 0x18)
#define I2C_AA   0x04u
#define I2C_SI   0x08u
#define I2C_STO  0x10u
#define I2C_STA  0x20u
#define I2C_EN   0x40u

/* own: the value for the address register (0 = do not answer as a slave). */
static void i2c_init(unsigned bus, unsigned own) {
    if (bus) {
        SCU_SFS(2, 3) = 0xC1;
        SCU_SFS(2, 4) = 0xC1;
        REG32(CGU_BASE + 0x084) = (1u << 24) | (1u << 11);      /* BASE_APB3_CLK: IRC */
        REG32(CCU1_BASE + 0x108) |= 1u;                         /* CLK_APB3_I2C1      */
    } else {
        REG32(SCU_BASE + 0xC84) = 0x808;
        REG32(CGU_BASE + 0x080) = (1u << 24) | (1u << 11);      /* BASE_APB1_CLK: IRC */
        REG32(CCU1_BASE + 0x210) |= 1u;                         /* CLK_APB1_I2C0      */
    }
    I2C_CONCLR(bus) = 0x6C;
    I2C_SCLH(bus) = 15;                                         /* 12 MHz / 30 = 400 kHz */
    I2C_SCLL(bus) = 15;
    I2C_ADR0(bus) = own;
    I2C_CONSET(bus) = I2C_EN | (own ? I2C_AA : 0);
}

static int i2c_wait(unsigned bus) {
    for (unsigned n = 0; n < 200000; ++n)
        if (I2C_CONSET(bus) & I2C_SI) return (int)I2C_STAT(bus);
    return -1;
}

/* Write n bytes to a 7-bit address. Returns the number of bytes acknowledged,
 * -1 if the address was not, -2 if the bus did not respond. */
static int i2c_write(unsigned bus, unsigned addr7, const uint8_t *p, unsigned n) {
    int st, done = -1;
    I2C_CONSET(bus) = I2C_STA;
    st = i2c_wait(bus);
    if (st != 0x08 && st != 0x10) { I2C_CONCLR(bus) = I2C_STA | I2C_SI; return -2; }
    I2C_DAT(bus) = addr7 << 1;
    I2C_CONCLR(bus) = I2C_STA | I2C_SI;
    st = i2c_wait(bus);
    if (st == 0x18) {
        for (done = 0; done < (int)n; ++done) {
            I2C_DAT(bus) = p[done];
            I2C_CONCLR(bus) = I2C_SI;
            if (i2c_wait(bus) != 0x28) break;
        }
    } else if (st < 0)
        done = -2;
    I2C_CONSET(bus) = I2C_STO;
    I2C_CONCLR(bus) = I2C_SI;
    for (unsigned k = 0; k < 20000 && (I2C_CONSET(bus) & I2C_STO); ++k) { }
    return done;
}

/* Does anything acknowledge this 7-bit address? */
static int i2c_probe(unsigned bus, unsigned addr7) {
    return i2c_write(bus, addr7, 0, 0) == 0;
}

/* Slave side, to be called often: bytes written to us go to buf (up to max).
 * Returns the length of a message when its stop arrives, else 0. */
static unsigned i2c_listen(unsigned bus, uint8_t *buf, unsigned max, unsigned *fill) {
    if (!(I2C_CONSET(bus) & I2C_SI)) return 0;
    unsigned st = I2C_STAT(bus), len = 0;
    switch (st) {
    case 0x60: case 0x68: case 0x70: case 0x78: *fill = 0; break;           /* addressed for writing */
    case 0x80: case 0x90: if (*fill < max) buf[*fill] = (uint8_t)I2C_DAT(bus); ++*fill; break;
    case 0xA0: len = *fill; *fill = 0; break;                                /* stop */
    case 0xA8: case 0xB0: case 0xB8: I2C_DAT(bus) = 0xFF; break;             /* asked to send: nothing to say */
    default: break;
    }
    I2C_CONSET(bus) = I2C_AA;
    I2C_CONCLR(bus) = I2C_SI;
    return len;
}
