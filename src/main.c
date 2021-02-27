#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include <sys/types.h>

// unix
#include <unistd.h>

uint16_t memory[UINT16_MAX];

// registers
enum {
  R_R0 = 0,
  R_R1,
  R_R2,
  R_R3,
  R_R4,
  R_R5,
  R_R6,
  R_R7,
  R_PC,
  R_COND,
  R_COUNT
};

// opcodes
enum {
  OP_BR = 0, // branch
  OP_ADD,    // add
  OP_LD,     // load
  OP_ST,     // store
  OP_JSR,    // jump register
  OP_AND,    // bitwise and
  OP_LDR,    // load register
  OP_STR,    // store register
  OP_RTI,    // unused
  OP_NOT,    // bitwise not
  OP_LDI,    // load indirect
  OP_STI,    // store indirect
  OP_JMP,    // jump
  OP_RES,    // reserved (unused)
  OP_LEA,    // load effective address
  OP_TRAP    // execute trap
};

// conditional flags
enum {
  FL_POS = 1 << 0, // P 0x0000000000000001
  FL_ZRO = 1 << 1, // Z 0x0000000000000010
  FL_NEG = 1 << 2  // N 0x0000000000000100
};

uint16_t reg[R_COUNT];

uint16_t sign_extend(uint16_t x, int bit_count) {
  if ((x >> (bit_count - 1)) & 1) {
    x |= 0xFFFF << bit_count;
  }
  return x;
}

void update_flags(uint16_t r) {
  if (reg[r] == 0) {
    reg[R_COND] = FL_ZRO;
  } else if (reg[r] >> 15) {
    reg[R_COND] = FL_NEG;
  } else {
    reg[R_COND] = FL_POS;
  }
}

uint16_t mem_read(uint16_t address) { return 0; }

void mem_write(uint16_t address, uint16_t value) {
}

int read_image(const char *image_path) { return 1; }

int main(int argc, char *argv[]) {
  if (argc < 2) {
    printf("lc3 [image] ...\n");
    exit(2);
  }

  for (int i = 1; i < argc; ++i) {
    if (!read_image(argv[i])) {
      printf("failed to load image: %s\n", argv[i]);
    }
  }

  enum { PC_START = 0x3000 };
  reg[R_PC] = PC_START;

  int running = 1;
  while (running) {
    // read next instruction and increment PC
    uint16_t instr = mem_read(reg[R_PC]++);
    // get the opcode (leftmost 4 bits)
    uint16_t op = instr >> 12;
    switch (op) {
    case OP_BR: {
      uint16_t p_flag = (instr >> 10) & 0x1;
      uint16_t z_flag = (instr >> 11) & 0x1;
      uint16_t n_flag = (instr >> 12) & 0x1;

      if ((p_flag && reg[R_COND] == FL_NEG) ||
          (z_flag && reg[R_COND] == FL_ZRO) ||
          (n_flag && reg[R_COND] == FL_POS)) {
        uint16_t pc_offset = sign_extend(instr & 0x1FF, 9);
        reg[R_PC] += pc_offset;
      }
    } break;
    case OP_ADD: {
      // destination register (DR)
      uint16_t r0 = (instr >> 9) & 0x7;
      // first operand (SR1)
      uint16_t r1 = (instr >> 6) & 0x7;
      // immediate mode flag
      uint16_t imm_flag = (instr >> 5) & 0x1;

      if (imm_flag) {
        uint16_t imm5 = sign_extend(instr & 0x1F, 5);
        reg[r0] = reg[r1] + imm5;
      } else {
        // second operand (SR2)
        uint16_t r2 = instr & 0x7;
        reg[r0] = reg[r1] + reg[r2];
      }
      update_flags(r0);
    } break;
    case OP_LD: {
      // pc offset (PCoffset9)
      uint16_t pc_offset = sign_extend(instr & 0x1FF, 9);
      // destination register (DR)
      uint16_t r0 = (instr >> 9) & 0x7;
      reg[r0] = mem_read(reg[R_PC] + pc_offset);
      update_flags(r0);
    } break;
    case OP_ST: {
      // pc offset (PCoffset9)
      uint16_t pc_offset = sign_extend(instr & 0x1FF, 9);
      // source register (SR)
      uint16_t r1 = (instr >> 9) & 0x7;
      mem_write(reg[R_PC] + pc_offset, reg[r1]);
    } break;
    case OP_JSR: {
      reg[R_R7] = reg[R_PC];
      // pc offset mode flag
      uint16_t offset_flag = (instr >> 11) & 0x1;
      if (offset_flag) {
        // pc offset (PCoffset11)
        uint16_t pc_offset = instr & 0x3FF;
        reg[R_PC] += pc_offset;
      } else {
        // base register (BaseR)
        uint16_t r_base = (instr >> 6) & 0x7;
        reg[R_PC] = reg[r_base];
      }
    } break;
    case OP_AND: {
      uint16_t r0 = (instr >> 9) & 0x7;
      uint16_t r1 = (instr >> 6) & 0x7;
      uint16_t imm_flag = (instr >> 5) & 0x1;
      if (imm_flag) {
        uint16_t imm5 = sign_extend(instr & 0x1F, 5);
        reg[r0] = reg[r1] + imm5;
      } else {
        uint16_t r2 = instr & 0x7;
        reg[r0] = reg[r1] & reg[r2];
      }
      update_flags(r0);
    } break;
    case OP_LDR: {
      // destination register (DR)
      uint16_t r0 = (instr >> 9) & 0x7;
      // base register (BaseR)
      uint16_t r_base = (instr >> 6) & 0x7;
      // memory offset (offset6)
      uint16_t offset = sign_extend(instr & 0x3F, 6);
      reg[r0] = mem_read(reg[r_base] + offset);
      update_flags(r0);
    } break;
    case OP_STR: {
      // source register (SR)
      uint16_t r1 = (instr >> 9) & 0x7;
      // base register (BaseR)
      uint16_t r_base = (instr >> 6) & 0x7;
      // memory offset (offset6)
      uint16_t offset = sign_extend(instr & 0x3F, 6);
      mem_write(reg[r_base] + offset, reg[r1]);
    } break;
    case OP_NOT: {
      // destination register (DR)
      uint16_t r0 = (instr >> 9) & 0x7;
      // source register (SR)
      uint16_t r1 = (instr >> 6) & 0x7;
      reg[r0] = !reg[r1];
      update_flags(r0);
    } break;
    case OP_LDI: {
      // destination register (DR)
      uint16_t r0 = (instr >> 9) & 0x7;
      // PC offset (PCOffset9)
      uint16_t pc_offset = sign_extend(instr & 0x1FF, 9);
      // add pc_offset to the current PC, look at that memory
      // location to get the final address
      reg[r0] = mem_read(mem_read(reg[R_PC] + pc_offset));
      update_flags(r0);
    } break;
    case OP_STI: {
      // pc offset (PCoffset9)
      uint16_t pc_offset = sign_extend(instr & 0x1FF, 9);
      // source register (SR)
      uint16_t r1 = (instr >> 9) & 0x7;
      mem_write(mem_read(reg[R_PC] + pc_offset), reg[r1]);
    } break;
    case OP_JMP: {
      // base register (BaseR)
      uint16_t r = (instr >> 6) & 0x2;
      reg[R_PC] = r;
    } break;
    case OP_LEA: {
      // destination register (DR)
      uint16_t r0 = (instr >> 9) & 0x7;
      // PC offset (PCOffset9)
      uint16_t pc_offset = sign_extend(instr & 0x1FF, 9);
      reg[r0] = reg[R_PC] + pc_offset;
      update_flags(r0);
    } break;
    case OP_TRAP:
      break;
    case OP_RTI:
    case OP_RES:
    default:
      break;
    }
  }

  return 0;
}
