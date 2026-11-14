/* Game-module side (compiled by the builder together with the generated blocks):
 * forwards the bus calls of the blocks to the host program. */
#include "phmod.h"
#ifndef PH_MACHINE
#define PH_MACHINE "hikaru"
#endif
static const PhHost *H;
u8 bus_r8(Cpu *c, u32 a) { return H->bus_r8(c, a); }
u16 bus_r16(Cpu *c, u32 a) { return H->bus_r16(c, a); }
u32 bus_r32(Cpu *c, u32 a) { return H->bus_r32(c, a); }
void bus_w8(Cpu *c, u32 a, u8 v) { H->bus_w8(c, a, v); }
void bus_w16(Cpu *c, u32 a, u16 v) { H->bus_w16(c, a, v); }
void bus_w32(Cpu *c, u32 a, u32 v) { H->bus_w32(c, a, v); }
void sh4_illegal(Cpu *c, u32 pc, u16 op) { H->sh4_illegal(c, pc, op); }
void sh4_on_sr_change(Cpu *c) { H->sh4_on_sr_change(c); }
void sh4_pref(Cpu *c, u32 a) { H->sh4_pref(c, a); }
PH_EXPORT const PhModule *ph_module_init(const PhHost *h) {
    static PhModule m;
    H = h; m.abi = PH_MOD_ABI; m.cpu_size = sizeof(Cpu); m.machine = PH_MACHINE; m.table = blk_table; m.count = blk_count;
    return &m;
}
