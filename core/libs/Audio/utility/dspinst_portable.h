/* Portable (host) implementations of the Cortex-M4 DSP intrinsics in dspinst.h.
 * Semantics follow the ARMv7E-M instruction definitions:
 *   ssat      signed saturation to N bits
 *   smulw[bt] (a * b16) >> 16, 48-bit intermediate, low 32 bits kept
 *   smmul[r]  high 32 bits of 64-bit product, optional rounding
 *   pkh[bt]   halfword packing
 *   q*16      saturating halfword arithmetic, sh*16 halving arithmetic
 *   smla*     multiply-accumulate variants (wrap-around on 32-bit sums)
 */
#ifndef dspinst_portable_h_
#define dspinst_portable_h_

#include <stdint.h>

static inline int32_t dsp_ssat(int64_t v, int bits)
{
    const int64_t max = ((int64_t)1 << (bits - 1)) - 1;
    const int64_t min = -((int64_t)1 << (bits - 1));
    return (int32_t)(v > max ? max : (v < min ? min : v));
}
static inline int16_t dsp_lo16(uint32_t v) { return (int16_t)(v & 0xFFFF); }
static inline int16_t dsp_hi16(uint32_t v) { return (int16_t)(v >> 16); }
static inline uint32_t dsp_pack(int32_t hi, int32_t lo)
{
    return ((uint32_t)hi << 16) | ((uint32_t)lo & 0xFFFF);
}

static inline int32_t signed_saturate_rshift(int32_t val, int bits, int rshift)
{
    return dsp_ssat((int64_t)(val >> rshift), bits);
}
static inline int16_t saturate16(int32_t val)
{
    return (int16_t)dsp_ssat(val, 16);
}
static inline int32_t signed_multiply_32x16b(int32_t a, uint32_t b)
{
    return (int32_t)(((int64_t)a * dsp_lo16(b)) >> 16);
}
static inline int32_t signed_multiply_32x16t(int32_t a, uint32_t b)
{
    return (int32_t)(((int64_t)a * dsp_hi16(b)) >> 16);
}
static inline int32_t multiply_32x32_rshift32(int32_t a, int32_t b)
{
    return (int32_t)(((int64_t)a * b) >> 32);
}
static inline int32_t multiply_32x32_rshift32_rounded(int32_t a, int32_t b)
{
    return (int32_t)((((int64_t)a * b) + 0x80000000LL) >> 32);
}
static inline int32_t multiply_accumulate_32x32_rshift32_rounded(int32_t sum, int32_t a, int32_t b)
{
    return (int32_t)(((((int64_t)sum) << 32) + ((int64_t)a * b) + 0x80000000LL) >> 32);
}
static inline int32_t multiply_subtract_32x32_rshift32_rounded(int32_t sum, int32_t a, int32_t b)
{
    return (int32_t)(((((int64_t)sum) << 32) - ((int64_t)a * b) + 0x80000000LL) >> 32);
}
static inline uint32_t pack_16t_16t(int32_t a, int32_t b)
{
    return ((uint32_t)a & 0xFFFF0000u) | ((uint32_t)(b >> 16) & 0xFFFF);
}
static inline uint32_t pack_16t_16b(int32_t a, int32_t b)
{
    return ((uint32_t)a & 0xFFFF0000u) | ((uint32_t)b & 0xFFFF);
}
static inline uint32_t pack_16b_16b(int32_t a, int32_t b)
{
    return ((uint32_t)a << 16) | ((uint32_t)b & 0xFFFF);
}
static inline uint32_t pack_16x16(int32_t a, int32_t b)
{
    return ((uint32_t)a << 16) | ((uint32_t)b & 0xFFFF);
}
static inline uint32_t signed_add_16_and_16(uint32_t a, uint32_t b)
{
    return dsp_pack(dsp_ssat((int32_t)dsp_hi16(a) + dsp_hi16(b), 16),
                    dsp_ssat((int32_t)dsp_lo16(a) + dsp_lo16(b), 16));
}
static inline int32_t signed_subtract_16_and_16(int32_t a, int32_t b)
{
    return (int32_t)dsp_pack(dsp_ssat((int32_t)dsp_hi16(a) - dsp_hi16(b), 16),
                             dsp_ssat((int32_t)dsp_lo16(a) - dsp_lo16(b), 16));
}
static inline int32_t signed_halving_add_16_and_16(int32_t a, int32_t b)
{
    return (int32_t)dsp_pack(((int32_t)dsp_hi16(a) + dsp_hi16(b)) >> 1,
                             ((int32_t)dsp_lo16(a) + dsp_lo16(b)) >> 1);
}
static inline int32_t signed_halving_subtract_16_and_16(int32_t a, int32_t b)
{
    return (int32_t)dsp_pack(((int32_t)dsp_hi16(a) - dsp_hi16(b)) >> 1,
                             ((int32_t)dsp_lo16(a) - dsp_lo16(b)) >> 1);
}
static inline int32_t signed_multiply_accumulate_32x16b(int32_t sum, int32_t a, uint32_t b)
{
    return (int32_t)((uint32_t)sum + (uint32_t)(((int64_t)a * dsp_lo16(b)) >> 16));
}
static inline int32_t signed_multiply_accumulate_32x16t(int32_t sum, int32_t a, uint32_t b)
{
    return (int32_t)((uint32_t)sum + (uint32_t)(((int64_t)a * dsp_hi16(b)) >> 16));
}
static inline uint32_t logical_and(uint32_t a, uint32_t b)
{
    return a & b;
}
static inline int32_t multiply_16tx16t_add_16bx16b(uint32_t a, uint32_t b)
{
    return (int32_t)((uint32_t)((int32_t)dsp_hi16(a) * dsp_hi16(b)) +
                     (uint32_t)((int32_t)dsp_lo16(a) * dsp_lo16(b)));
}
static inline int32_t multiply_16tx16b_add_16bx16t(uint32_t a, uint32_t b)
{
    return (int32_t)((uint32_t)((int32_t)dsp_hi16(a) * dsp_lo16(b)) +
                     (uint32_t)((int32_t)dsp_lo16(a) * dsp_hi16(b)));
}
static inline int64_t multiply_accumulate_16tx16t_add_16bx16b(int64_t sum, uint32_t a, uint32_t b)
{
    return sum + (int64_t)((int32_t)dsp_hi16(a) * dsp_hi16(b)) +
           (int64_t)((int32_t)dsp_lo16(a) * dsp_lo16(b));
}
static inline int64_t multiply_accumulate_16tx16b_add_16bx16t(int64_t sum, uint32_t a, uint32_t b)
{
    return sum + (int64_t)((int32_t)dsp_hi16(a) * dsp_lo16(b)) +
           (int64_t)((int32_t)dsp_lo16(a) * dsp_hi16(b));
}
static inline int32_t multiply_16bx16b(uint32_t a, uint32_t b)
{
    return (int32_t)dsp_lo16(a) * dsp_lo16(b);
}
static inline int32_t multiply_16bx16t(uint32_t a, uint32_t b)
{
    return (int32_t)dsp_lo16(a) * dsp_hi16(b);
}
static inline int32_t multiply_16tx16b(uint32_t a, uint32_t b)
{
    return (int32_t)dsp_hi16(a) * dsp_lo16(b);
}
static inline int32_t multiply_16tx16t(uint32_t a, uint32_t b)
{
    return (int32_t)dsp_hi16(a) * dsp_hi16(b);
}
static inline int32_t substract_32_saturate(uint32_t a, uint32_t b)
{
    return dsp_ssat((int64_t)(int32_t)a - (int64_t)(int32_t)b, 32);
}
static inline uint32_t get_q_psr(void) { return 0; }
static inline void clr_q_psr(void) {}
static inline int32_t multiply_32x32_rshift(int32_t x, int32_t y, int z)
{
    return (int32_t)(((int64_t)x * y) >> (31 - z));
}

#endif /* dspinst_portable_h_ */
