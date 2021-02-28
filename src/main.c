#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <fcntl.h>
#include <unistd.h>

#include <sys/mman.h>
#include <sys/termios.h>
#include <sys/time.h>
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

// traps
enum {
  // get character from keyboard, not echoed onto the terminal
  TRAP_GETC = 0x20,
  // output a character
  TRAP_OUT = 0x21,
  // output a word string
  TRAP_PUTS = 0x22,
  // get character from keyboard, echoed onto the terminal
  TRAP_IN = 0x23,
  // output a byte string
  TRAP_PUTSP = 0x24,
  // halt to program
  TRAP_HALT = 0x25
};

// memory mapped registers
enum {
  // keyboard status
  MR_KBSR = 0xFE00,
  // keyboard data
  MR_KBDR = 0xFE02
};

uint16_t reg[R_COUNT];

struct termios original_tio;

void disable_input_buffering() {
  tcgetattr(STDIN_FILENO, &original_tio);
  struct termios new_tio = original_tio;
  new_tio.c_lflag &= ~ICANON & ~ECHO;
  tcsetattr(STDIN_FILENO, TCSANOW, &new_tio);
}

void restore_input_buffering() {
  tcsetattr(STDIN_FILENO, TCSANOW, &original_tio);
}

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

uint16_t check_key() {
  fd_set readfds;
  FD_ZERO(&readfds);
  FD_SET(STDIN_FILENO, &readfds);

  struct timeval timeout;
  timeout.tv_sec = 0;
  timeout.tv_usec = 0;
  return select(1, &readfds, NULL, NULL, &timeout) != 0;
}

uint16_t mem_read(uint16_t address) {
  if (address == MR_KBSR) {
    if (check_key()) {
      memory[MR_KBSR] = 1 << 15;
      memory[MR_KBDR] = getchar();
    } else {
      memory[MR_KBSR] = 0;
    }
  }
  return memory[address];
}

void mem_write(uint16_t address, uint16_t value) { memory[address] = value; }

uint16_t swap16(uint16_t x) { return (x << 8) | (x >> 8); }

void read_image_file(FILE *file) {
  // where in memory to place the image
  uint16_t origin;
  fread(&origin, sizeof(origin), 1, file);
  origin = swap16(origin);

  // we know the maximum file size so
  // we only need one fread
  uint16_t max_read = UINT16_MAX - origin;
  uint16_t *p = memory + origin;
  size_t read = fread(p, sizeof(uint16_t), max_read, file);

  while (read-- > 0) {
    *p = swap16(*p);
    ++p;
  }
}

int read_image(const char *image_path) {
  FILE *file = fopen(image_path, "rb");
  if (!file) {
    return 0;
  }
  read_image_file(file);
  fclose(file);
  return 1;
}

int handle_trap(uint16_t instr) {
  switch (instr & 0xFF) {
  case TRAP_GETC: {
    reg[R_R0] = (uint16_t)getchar();
    break;
  }
  case TRAP_OUT: {
    putc((char)reg[R_R0], stdout);
    fflush(stdout);
    break;
  }
  case TRAP_PUTS: {
    uint16_t *c = memory + reg[R_R0];
    while (*c) {
      putc((char)*c, stdout);
      ++c;
    }
    fflush(stdout);
    break;
  }
  case TRAP_IN: {
    printf("> ");
    char ch = getchar();
    putc(ch, stdout);
    reg[R_R0] = (uint16_t)ch;
    break;
  }
  case TRAP_PUTSP: {
    uint16_t *c = memory + reg[R_R0];
    while (*c) {
      char ch_lo = (*c) & 0xFF;
      char ch_hi = (*c) >> 8;
      putc(ch_lo, stdout);
      if (ch_hi) {
        putc(ch_hi, stdout);
      }
      ++c;
    }
    fflush(stdout);
    break;
  }
  case TRAP_HALT: {
    puts("HALT\n");
    fflush(stdout);
    return 0;
  }
  }
  return 1;
}

void handle_interrupt(int signal) {
  restore_input_buffering();
  printf("\n");
  exit(-2);
}

int main(int argc, char *argv[]) {
  if (argc < 2) {
    printf("lc3 [image] ...\n");
    exit(2);
  }

  for (int i = 1; i < argc; ++i) {
    if (!read_image(argv[i])) {
      printf("failed to load image: %s\n", argv[i]);
      exit(1);
    }
  }

  signal(SIGINT, handle_interrupt);
  disable_input_buffering();

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
      uint16_t pc_offset = sign_extend(instr & 0x1FF, 9);
      uint16_t cond_flag = (instr >> 9) & 0x7;
      if (cond_flag & reg[R_COND]) {
        reg[R_PC] += pc_offset;
      }
      break;
    }
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
      break;
    }
    case OP_LD: {
      // pc offset (PCoffset9)
      uint16_t pc_offset = sign_extend(instr & 0x1FF, 9);
      // destination register (DR)
      uint16_t r0 = (instr >> 9) & 0x7;
      reg[r0] = mem_read(reg[R_PC] + pc_offset);
      update_flags(r0);
      break;
    }
    case OP_ST: {
      // pc offset (PCoffset9)
      uint16_t pc_offset = sign_extend(instr & 0x1FF, 9);
      // source register (SR)
      uint16_t r1 = (instr >> 9) & 0x7;
      mem_write(reg[R_PC] + pc_offset, reg[r1]);
      break;
    }
    case OP_JSR: {
      reg[R_R7] = reg[R_PC];
      // pc offset mode flag
      uint16_t offset_flag = (instr >> 11) & 0x1;
      if (offset_flag) {
        // pc offset (PCoffset11)
        uint16_t pc_offset = sign_extend(instr & 0x7FF, 11);
        reg[R_PC] += pc_offset;
      } else {
        // base register (BaseR)
        uint16_t r_base = (instr >> 6) & 0x7;
        reg[R_PC] = reg[r_base];
      }
      break;
    }
    case OP_AND: {
      uint16_t r0 = (instr >> 9) & 0x7;
      uint16_t r1 = (instr >> 6) & 0x7;
      uint16_t imm_flag = (instr >> 5) & 0x1;
      if (imm_flag) {
        uint16_t imm5 = sign_extend(instr & 0x1F, 5);
        reg[r0] = reg[r1] & imm5;
      } else {
        uint16_t r2 = instr & 0x7;
        reg[r0] = reg[r1] & reg[r2];
      }
      update_flags(r0);
      break;
    }
    case OP_LDR: {
      // destination register (DR)
      uint16_t r0 = (instr >> 9) & 0x7;
      // base register (BaseR)
      uint16_t r_base = (instr >> 6) & 0x7;
      // memory offset (offset6)
      uint16_t offset = sign_extend(instr & 0x3F, 6);
      reg[r0] = mem_read(reg[r_base] + offset);
      update_flags(r0);
      break;
    }
    case OP_STR: {
      // source register (SR)
      uint16_t r1 = (instr >> 9) & 0x7;
      // base register (BaseR)
      uint16_t r_base = (instr >> 6) & 0x7;
      // memory offset (offset6)
      uint16_t offset = sign_extend(instr & 0x3F, 6);
      mem_write(reg[r_base] + offset, reg[r1]);
      break;
    }
    case OP_NOT: {
      // destination register (DR)
      uint16_t r0 = (instr >> 9) & 0x7;
      // source register (SR)
      uint16_t r1 = (instr >> 6) & 0x7;
      reg[r0] = ~reg[r1];
      update_flags(r0);
      break;
    }
    case OP_LDI: {
      // destination register (DR)
      uint16_t r0 = (instr >> 9) & 0x7;
      // PC offset (PCOffset9)
      uint16_t pc_offset = sign_extend(instr & 0x1FF, 9);
      // add pc_offset to the current PC, look at that memory
      // location to get the final address
      reg[r0] = mem_read(mem_read(reg[R_PC] + pc_offset));
      update_flags(r0);
      break;
    }
    case OP_STI: {
      // source register (SR)
      uint16_t r1 = (instr >> 9) & 0x7;
      // pc offset (PCoffset9)
      uint16_t pc_offset = sign_extend(instr & 0x1FF, 9);
      mem_write(mem_read(reg[R_PC] + pc_offset), reg[r1]);
      break;
    }
    case OP_JMP: {
      // base register (BaseR)
      uint16_t r_base = (instr >> 6) & 0x7;
      reg[R_PC] = reg[r_base];
      break;
    }
    case OP_LEA: {
      // destination register (DR)
      uint16_t r0 = (instr >> 9) & 0x7;
      // PC offset (PCOffset9)
      uint16_t pc_offset = sign_extend(instr & 0x1FF, 9);
      reg[r0] = reg[R_PC] + pc_offset;
      update_flags(r0);
      break;
    }
    case OP_TRAP: {
      running = handle_trap(instr);
      break;
    }
    case OP_RTI:
    case OP_RES:
    default:
      abort();
      break;
    }
  }

  restore_input_buffering();
  return 0;
}
