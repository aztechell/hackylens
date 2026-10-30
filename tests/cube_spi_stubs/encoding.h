#ifndef SPI_TEST_ENCODING_H
#define SPI_TEST_ENCODING_H
#define MSTATUS_MIE 8UL
extern unsigned long spi_test_mstatus;
extern unsigned long spi_test_mhartid;
static inline unsigned long spi_test_clear(unsigned long mask)
{
    unsigned long saved = spi_test_mstatus;
    spi_test_mstatus &= ~mask;
    return saved;
}
static inline unsigned long spi_test_set(unsigned long mask)
{
    unsigned long saved = spi_test_mstatus;
    spi_test_mstatus |= mask;
    return saved;
}
#define clear_csr(reg, mask) spi_test_clear(mask)
#define set_csr(reg, mask) spi_test_set(mask)
#define read_csr(reg) spi_test_##reg
#endif
