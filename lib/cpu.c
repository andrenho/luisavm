#include "luisavm.h"

#include <assert.h>
#include <stdlib.h>
#include <syslog.h>

typedef struct LVM_CPU {
    LVM_Computer* computer;
    uint32_t reg[16];
} LVM_CPU;

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmissing-declarations"

// {{{ CONSTRUCTOR/DESTRUCTOR

LVM_CPU* 
lvm_createcpu(LVM_Computer* comp)
{
    LVM_CPU* cpu = calloc(1, sizeof(LVM_CPU));
    cpu->computer = comp;
    syslog(LOG_DEBUG, "New CPU created.");
    return cpu;
}


void
lvm_destroycpu(LVM_CPU* cpu)
{
    free(cpu);
    syslog(LOG_DEBUG, "CPU destroyed.");
}

// }}}

// {{{ STEP / RESET

void 
lvm_cpureset(LVM_CPU* cpu)
{
    for(int i=0; i<16; ++i) {
        cpu->reg[i] = 0;
    }
}

//
// shortcuts
//

#define GET(pos) (lvm_get(cpu->computer, pos))
#define GET16(pos) (lvm_get16(cpu->computer, pos))
#define GET32(pos) (lvm_get32(cpu->computer, pos))
#define rPC (cpu->reg[PC])

//
// parameters
//

typedef struct Parameter {
    enum { NONE, REG, INDREG, V8, V16, V32, INDV32 } type;
    uint32_t value;
} Parameter;

inline static void
_twin_registers_rr(Parameter pars[2], uint8_t pos)
{
    pars[0] = (Parameter) { REG, (pos >> 4) };
    pars[1] = (Parameter) { REG, (pos & 0xF) };
}

inline static void
_twin_registers_ri(Parameter pars[2], uint8_t pos)
{
    pars[0] = (Parameter) { REG, (pos >> 4) };
    pars[1] = (Parameter) { INDREG, (pos & 0xF) };
}

inline static void
_twin_registers_ir(Parameter pars[2], uint8_t pos)
{
    pars[0] = (Parameter) { INDREG, (pos >> 4) };
    pars[1] = (Parameter) { REG, (pos & 0xF) };
}

inline static void
_twin_registers_ii(Parameter pars[2], uint8_t pos)
{
    pars[0] = (Parameter) { INDREG, (pos >> 4) };
    pars[1] = (Parameter) { INDREG, (pos & 0xF) };
}

inline static Parameter _reg(LVM_CPU* cpu, uint32_t offset) { return (Parameter) { REG, GET(rPC+offset) }; }
inline static Parameter _indreg(LVM_CPU* cpu, uint32_t offset) { return (Parameter) { INDREG, GET(rPC+offset) }; }
inline static Parameter _v8(LVM_CPU* cpu, uint32_t offset) { return (Parameter) { V8, GET(rPC+offset) }; }
inline static Parameter _v16(LVM_CPU* cpu, uint32_t offset) { return (Parameter) { V16, GET16(rPC+offset) }; }
inline static Parameter _v32(LVM_CPU* cpu, uint32_t offset) { return (Parameter) { V32, GET32(rPC+offset) }; }
inline static Parameter _indv32(LVM_CPU* cpu, uint32_t offset) { return (Parameter) { INDV32, GET32(rPC+offset) }; }


//
// operation constructors
//

inline static uint32_t
_take(LVM_CPU* cpu, Parameter par)
{
    switch(par.type) {
        case REG: return cpu->reg[par.value];
        case V8: case V16: case V32: return par.value;
        case INDV32: return lvm_get32(cpu->computer, par.value);
        case INDREG: return lvm_get32(cpu->computer, cpu->reg[par.value]);
        case NONE: abort();
    }
    abort();
}
#define TAKE(par) (_take(cpu, par))


inline static void
_apply(LVM_CPU* cpu, Parameter par, uint32_t value, uint32_t sz)
{
    lvm_cpusetflag(cpu, Z, value == 0);
    lvm_cpusetflag(cpu, S, (value >> 31) & 1);
    lvm_cpusetflag(cpu, V, false);
    lvm_cpusetflag(cpu, Y, false);
    lvm_cpusetflag(cpu, GT, false);
    lvm_cpusetflag(cpu, LT, false);

    switch(par.type) {
        case REG:
            assert(par.value < 16);
            cpu->reg[par.value] = value;
            break;
        case INDREG:
            switch(sz) {
                case  8: lvm_set(cpu->computer, cpu->reg[par.value], (uint8_t)value); return;
                case 16: lvm_set16(cpu->computer, cpu->reg[par.value], (uint16_t)value); return;
                case 32: lvm_set32(cpu->computer, cpu->reg[par.value], value); return;
                default: abort();
            }
        case INDV32:
            switch(sz) {
                case  8: lvm_set(cpu->computer, par.value, (uint8_t)value); return;
                case 16: lvm_set16(cpu->computer, par.value, (uint16_t)value); return;
                case 32: lvm_set32(cpu->computer, par.value, value); return;
                default: abort();
            }
        case V8: case V16: case V32: case NONE: default:
            abort();
    }
}
#define APPLY(par, value) (_apply(cpu, par, value, 0))
#define APPLY_SZ(par, value, sz) (_apply(cpu, par, value, sz))


inline static void
_advance_pc(LVM_CPU* cpu, Parameter pars[2])
{
    if(((pars[0].type == REG) || (pars[0].type == INDREG)) && ((pars[1].type == REG) || (pars[1].type == INDREG))) {
        cpu->reg[PC] += 2;
    } else {
        for(int i=0; i<2; ++i) {
            switch(pars[i].type) {
                case REG: case INDREG: case V8:
                    cpu->reg[PC] += 1;
                    break;
                case V16:
                    cpu->reg[PC] += 2;
                    break;
                case V32: case INDV32:
                    cpu->reg[PC] += 4;
                    break;
                case NONE:
                    break;
                default:
                    abort();
            }
        }
    }
}
#define ADVANCE_PC(pars) (_advance_pc(cpu, pars))


// 
// instruction parser
//

typedef enum Instruction { 
    MOV, MOVB, MOVW, MOVD, SWAP,
    OR, XOR, AND, SHL, SHR, NOT,
    ADD, SUB, CMP, MUL, IDIV, MOD, INC, DEC,
    BZ, BEQ, BNZ, BNEG, BPOS, BGT, BGTE, BLT, BLTE, BV, BNV,
    JMP, JSR, RET, IRET,
    PUSHB, PUSHW, PUSHD, PUSH_A, POPB, POPW, POPD, POP_A, POPX,
    NOP, HALT, DBG,
    INVALID 
} Instruction;

#define PACK_NONE(pos, inst) \
        case pos: pars[0] = (Parameter){ NONE, 0 }; pars[1] = (Parameter){ NONE, 0 }; return inst;

#define PACK_REG(pos, inst) \
        case pos: pars[0] = _reg(cpu, rPC+1); pars[1] = (Parameter){ NONE, 0 }; return inst;

#define PACK_RR(pos, inst) \
        case pos: _twin_registers_rr(pars, GET(rPC+1)); return inst;

#define PACK_2(pos, inst) \
        case pos+0: _twin_registers_rr(pars, GET(rPC+1));                            return inst; \
        case pos+1: pars[0] = _reg(cpu, rPC+1); pars[1] = _v8(cpu, rPC+2);           return inst; \

#define PACK_4(pos, inst) \
        case pos+0: _twin_registers_rr(pars, GET(rPC+1));                            return inst; \
        case pos+1: pars[0] = _reg(cpu, rPC+1); pars[1] = _v8(cpu, rPC+2);           return inst; \
        case pos+2: pars[0] = _reg(cpu, rPC+1); pars[1] = _v16(cpu, rPC+2);          return inst; \
        case pos+3: pars[0] = _reg(cpu, rPC+1); pars[1] = _v32(cpu, rPC+2);          return inst;

#define PACK_MOV(pos, inst, sz) \
        case pos+0: _twin_registers_ri(pars, GET(rPC+1));                            return inst; \
        case pos+1: pars[0] = _reg(cpu, rPC+1); pars[1] = _indv32(cpu, rPC+2);       return inst; \
        case pos+2: _twin_registers_ir(pars, GET(rPC+1));                            return inst; \
        case pos+3: pars[0] = _indreg(cpu, rPC+1); pars[1] = _v ## sz(cpu, rPC+2);   return inst; \
        case pos+4: _twin_registers_ii(pars, GET(rPC+1));                            return inst; \
        case pos+5: pars[0] = _indreg(cpu, rPC+1); pars[1] = _indv32(cpu, rPC+2);    return inst; \
        case pos+6: pars[0] = _indv32(cpu, rPC+1); pars[1] = _reg(cpu, rPC+5);       return inst; \
        case pos+7: pars[0] = _indv32(cpu, rPC+1); pars[1] = _v ## sz(cpu, rPC+5);   return inst; \
        case pos+8: pars[0] = _indv32(cpu, rPC+1); pars[1] = _indreg(cpu, rPC+5);    return inst; \
        case pos+9: pars[0] = _indv32(cpu, rPC+1); pars[1] = _indv32(cpu, rPC+5);    return inst;

#define PACK_BRANCH(pos, inst) \
        case pos+0: pars[0] = _reg(cpu, rPC+1); pars[1] = (Parameter){ NONE, 0 }; return inst; \
        case pos+1: pars[0] = _v32(cpu, rPC+1); pars[1] = (Parameter){ NONE, 0 }; return inst;

#define PACK_PUSH(pos, inst, sz) \
        case pos+0: pars[0] = _reg(cpu, rPC+1); pars[1] = (Parameter){ NONE, 0 }; return inst; \
        case pos+1: pars[0] = _v ## sz(cpu, rPC+1); pars[1] = (Parameter){ NONE, 0 }; return inst;

#define PACK_POPX(pos, inst) \
        case pos+0: pars[0] = _reg(cpu, rPC+1); pars[1] = (Parameter){ NONE, 0 }; return inst; \
        case pos+1: pars[0] = _v8(cpu, rPC+1); pars[1] = (Parameter){ NONE, 0 }; return inst; \
        case pos+2: pars[0] = _v16(cpu, rPC+1); pars[1] = (Parameter){ NONE, 0 }; return inst;

inline static Instruction
_parse_opcode(LVM_CPU* cpu, Parameter pars[2])
{
    uint8_t opcode = GET(rPC);
    switch(opcode) {

        PACK_4(0x01, MOV)
        PACK_MOV(0x05, MOVB, 8)
        PACK_MOV(0x0F, MOVW, 16)
        PACK_MOV(0x19, MOVD, 32)

        PACK_RR(0x23, SWAP)

        PACK_4(0x24, OR)
        PACK_4(0x28, XOR)
        PACK_4(0x2C, AND)

        PACK_2(0x30, SHL)
        PACK_2(0x32, SHR)

        PACK_REG(0x34, NOT)

        PACK_4(0x35, ADD)
        PACK_4(0x39, SUB)
        PACK_4(0x3D, CMP)
        PACK_4(0x42, MUL)
        PACK_4(0x46, IDIV)
        PACK_4(0x4A, MOD)

        PACK_REG(0x4E, INC)
        PACK_REG(0x4F, DEC)

        PACK_BRANCH(0x50, BZ)
        PACK_BRANCH(0x52, BEQ)
        PACK_BRANCH(0x54, BNZ)
        PACK_BRANCH(0x56, BNEG)
        PACK_BRANCH(0x58, BPOS)
        PACK_BRANCH(0x5A, BGT)
        PACK_BRANCH(0x5C, BGTE)
        PACK_BRANCH(0x5E, BLT)
        PACK_BRANCH(0x60, BLTE)
        PACK_BRANCH(0x62, BV)
        PACK_BRANCH(0x64, BNV)

        PACK_BRANCH(0x66, JMP)
        PACK_BRANCH(0x68, JSR)

        PACK_NONE(0x6A, RET)
        PACK_NONE(0x6B, IRET)

        PACK_PUSH(0x6C, PUSHB, 8)
        PACK_PUSH(0x6E, PUSHW, 16)
        PACK_PUSH(0x70, PUSHD, 32)
        PACK_NONE(0x72, PUSH_A)
        PACK_REG(0x73, POPB)
        PACK_REG(0x74, POPW)
        PACK_REG(0x75, POPD)
        PACK_NONE(0x76, PUSH_A)
        PACK_POPX(0x77, POPX)

        PACK_NONE(0x7A, NOP)
        PACK_NONE(0x7B, HALT)
        PACK_NONE(0x7C, DBG)

        default:
            return INVALID;
    }
}


//
// operator execution
//

void 
lvm_cpustep(LVM_CPU* cpu)
{
    Parameter pars[2];
    switch(_parse_opcode(cpu, pars)) {
        case MOV:  APPLY(pars[0], TAKE(pars[1])); break;
        case MOVB: APPLY_SZ(pars[0], (uint8_t)TAKE(pars[1]), 8); break;
        case MOVW: APPLY_SZ(pars[0], (uint16_t)TAKE(pars[1]), 16); break;
        case MOVD: APPLY_SZ(pars[0], TAKE(pars[1]), 32); break;
        case SWAP: {
                uint32_t tmp = TAKE(pars[1]);
                APPLY(pars[1], TAKE(pars[0]));
                APPLY(pars[0], tmp);
            }
            break;
        case OR:   APPLY(pars[0], TAKE(pars[0]) | TAKE(pars[1])); break;
        case XOR:  APPLY(pars[0], TAKE(pars[0]) ^ TAKE(pars[1])); break;
        case AND:  APPLY(pars[0], TAKE(pars[0]) & TAKE(pars[1])); break;
        case SHL:  APPLY(pars[0], TAKE(pars[0]) << TAKE(pars[1])); break;
        case SHR:  APPLY(pars[0], TAKE(pars[0]) >> TAKE(pars[1])); break;
        case NOT:  APPLY(pars[0], !TAKE(pars[0])); break;
        case ADD: {
                bool y = ((uint64_t)TAKE(pars[0]) + (uint64_t)TAKE(pars[1])) > 0xFFFFFFFF;
                APPLY(pars[0], TAKE(pars[0]) + TAKE(pars[1]) + lvm_cpuflag(cpu, Y)); 
                lvm_cpusetflag(cpu, Y, y);
            }
            break;
        case INVALID:
        default:
            syslog(LOG_ERR, "Invalid opcode 0x%02X", GET(cpu->reg[PC]));
            abort();
    }
    ADVANCE_PC(pars);
}

// }}}

#pragma GCC diagnostic pop

// {{{ REGS & FLAGS

uint32_t 
lvm_cpuregister(LVM_CPU* cpu, LVM_CPURegister r)
{
    if(r > 15) {
        syslog(LOG_ERR, "Invalid register %u, exiting...", r);
        abort();
    }
    return cpu->reg[r];
}


void
lvm_cpusetregister(LVM_CPU* cpu, LVM_CPURegister r, uint32_t data)
{
    if(r > 15) {
        syslog(LOG_ERR, "Invalid register %u, exiting...", r);
        abort();
    }
    cpu->reg[r] = data;
}


bool 
lvm_cpuflag(LVM_CPU* cpu, LVM_CPUFlag f)
{
    return (bool)((lvm_cpuregister(cpu, FL) >> (int)f) & 1);
}


void 
lvm_cpusetflag(LVM_CPU* cpu, LVM_CPUFlag f, bool value)
{
    int64_t new_value = lvm_cpuregister(cpu, FL);
    new_value ^= (-value ^ new_value) & (1 << (int)f);
    lvm_cpusetregister(cpu, FL, (uint32_t)new_value);
}

// }}}
