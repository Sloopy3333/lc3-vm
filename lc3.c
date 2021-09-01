#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/time.h>
#include <unistd.h>
#include <sys/termios.h>
#include <signal.h>

/* memory array */
uint16_t memory[UINT16_MAX];

/*see https://justinmeiners.github.io/lc3-vm/supplies/lc3-isa.pdf
 for specifications */

/* registers */
enum
  {
    R_R0 = 0,
    R_R1,
    R_R2,
    R_R3,
    R_R4,
    R_R5,
    R_R6,
    R_R7,
    R_PC,                       /* program counter */
    R_COND,                     /* condition counter */
    R_COUNT
  };

/* Memory maped reg */
enum
  {
    MR_KBSR = 0xFE00, /* keyboard status */
    MR_KBDR = 0xFE02  /* keyboard data */
  };

/* registers array */
uint16_t reg[R_COUNT];

/* opcodes */
enum
  {
    OP_BR = 0, /* branch */
    OP_ADD,    /* add  */
    OP_LD,     /* load */
    OP_ST,     /* store */
    OP_JSR,    /* jump register */
    OP_AND,    /* bitwise and */
    OP_LDR,    /* load register */
    OP_STR,    /* store register */
    OP_RTI,    /* unused */
    OP_NOT,    /* bitwise not */
    OP_LDI,    /* load indirect */
    OP_STI,    /* store indirect */
    OP_JMP,    /* jump */
    OP_RES,    /* reserved (unused) */
    OP_LEA,    /* load effective address */
    OP_TRAP    /* execute trap */
  };

/* condition flag */
enum
  {
    FL_POS = 1 << 0, /* Positive */
    FL_ZRO = 1 << 1, /* Zero */
    FL_NEG = 1 << 2, /* Negitive */
  };

/* trap codes */
enum
  {
    TRAP_GETC = 0x20,  /* get character from keyboard, not echoed onto the terminal */
    TRAP_OUT = 0x21,   /* output a character */
    TRAP_PUTS = 0x22,  /* output a word string */
    TRAP_IN = 0x23,    /* get character from keyboard, echoed onto the terminal */
    TRAP_PUTSP = 0x24, /* output a byte string */
    TRAP_HALT = 0x25   /* halt the program */
  };

uint16_t swap16(uint16_t x)
{
    return (x << 8) | (x >> 8);
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

void mem_write(uint16_t address, uint16_t val) {
  memory[address] = val;
}

uint16_t mem_read(uint16_t addr){
    if(addr == MR_KBSR){
        if(check_key()){
            memory[MR_KBSR] = 1 << 15;
            memory[MR_KBDR] = getchar();
        }else{
            memory[MR_KBSR] = 0;
        }
    }
    uint16_t val = memory[addr];
    return val;
}

/* function that converts any val of bits to a 16 bit val */
uint16_t sign_extend(uint16_t val, int bit_count){
  /* check if val is negitive if negitive take 2 complement */
  if((val >> (bit_count-1)) & 1){
    val |= (0xffff << bit_count);
  }
  return val;
}

void update_flag(uint16_t r){
  if(reg[r] == 0){
    reg[R_COND] = FL_ZRO;
  }
  else if(reg[r] >> 15 == 1){
    reg[R_COND] = FL_NEG;
  }
  else{
    reg[R_COND] = FL_POS;
  }
}

/* read file from the disc */
int load_image_from_file(const char* file){
    FILE* image= fopen(file,"rb");
    if(!image){
        return 0;
    }
    uint16_t origin;
    fread(&origin,sizeof(origin),1,image);
    origin = swap16(origin);
    uint16_t max_read = UINT16_MAX - origin;
    uint16_t* i = memory + origin;
    size_t read = fread(i,sizeof(uint16_t),max_read,image);
    // swap
    while(read-- > 0){
        *i = swap16(*i);
        ++i;
    }
    return 1;
}

void op_add(uint16_t instr){
    // rister to store results
    uint16_t r0 = (instr >> 9) & 0x7;
    //r1 register
    uint16_t r1 = (instr >> 6) & 0x7;
    //imm flag
    uint16_t imm_flag = (instr >> 5) & 0x1;
    if(imm_flag){
        uint16_t imm = instr  & 0x1f;
        reg[r0] = reg[r1] + sign_extend(imm,5);
    }else{
        uint16_t sr2 =  instr & 0x7;
        reg[r0] = reg[r1] + reg[sr2];
    }
    update_flag(r0);
}

void op_and(uint16_t instr){
    uint16_t r0 = (instr >> 9) & 0x7;
    uint16_t r1 = (instr >> 6) & 0x7;
    uint16_t imm_flag = (instr >> 5) & 0x1;
    if (imm_flag){
        uint16_t imm = instr & 0x1f;
        reg[r0] = reg[r1] & sign_extend(imm,5);
    }else{
        reg[r0] = reg[r1] & reg[(instr & 0x7)];
    }
    update_flag(r0);
}

void op_br(uint16_t ins){
    uint16_t cond_flag = (ins >> 9) & 0x7;
    uint16_t pc_offset = sign_extend((ins & 0x1ff),9);
    if(cond_flag & reg[R_COND]){
        reg[R_PC] += pc_offset;
    }
}

void op_jmp(uint16_t ins){
    uint16_t r =  (ins >> 6) & 0x7;
    reg[R_PC] = reg[r];
}

void op_jsr(uint16_t ins){
    reg[R_R7] = reg[R_PC];
    uint16_t flag = (ins >> 11) & 1;
    if(flag){
        reg[R_PC] += sign_extend((ins & 0x7ff),11);
    }else{
        reg[R_PC] = (ins >> 6) & 0x7;
    }
}

void op_ld(uint16_t ins){
    uint16_t r0 = (ins >> 9) & 0x7;
    uint16_t address = sign_extend((ins & 0x1ff),9) + reg[R_PC];
    reg[r0] = mem_read(address);
    update_flag(r0);
}

void op_ldi(uint16_t ins){
    uint16_t r0 = (ins >> 9) & 0x7;
    uint16_t address = sign_extend((ins & 0x1ff),9) + reg[R_PC];
    reg[r0] = mem_read(mem_read(address));
    update_flag(r0);
}

void op_ldr(uint16_t ins){
    uint16_t r0 = (ins >> 9) & 0x7;
    uint16_t address = reg[(ins>>6) & 0x7] + sign_extend(ins & 0x3f,6);
    reg[r0] = mem_read(address);
    update_flag(r0);
}

void op_lea(uint16_t ins){
    uint16_t r0 = (ins >> 9) & 0x7;
    uint16_t address = sign_extend(ins & 0x1ff,9) + reg[R_PC];
    reg[r0] = address;
    update_flag(r0);
}

void op_not(uint16_t ins){
    uint16_t r0 = (ins >> 9) & 0x7;
    uint16_t r1 = (ins >> 6) & 0x7;
    reg[r0] = ~reg[r1];
    update_flag(r0);
}

void op_st(uint16_t ins){
    uint16_t sr = (ins >> 9) & 0x7;
    uint16_t address = sign_extend(ins & 0x1ff, 9) + reg[R_PC];
    uint16_t val = reg[sr];
    mem_write(address,val);
}

void op_sti(uint16_t ins){
    uint16_t sr = (ins >> 9) & 0x7;
    uint16_t address = sign_extend(ins & 0x1ff, 9) + reg[R_PC];
    uint16_t val = reg[sr];
    mem_write(mem_read(address),val);
}

void op_str(uint16_t ins){
    uint16_t sr = (ins >> 9) & 0x7;
    uint16_t br = (ins >> 6) & 0x7;
    uint16_t address = reg[br] + sign_extend(ins & 0x3f,6);
    mem_write(address, reg[sr]);
}

void trap_puts(){
    uint16_t* c = memory + reg[R_R0];
    while(*c){
        putc((char)*c,stdout);
        ++c;
    }
    fflush(stdout);
}

void trap_getc(){
    uint16_t c = (uint16_t)getchar();
    reg[R_R0] = c;
}

void trap_out(){
    putc((char)reg[R_R0],stdout);
    fflush(stdout);
}

void trap_putsp(){
    // 2 chars, in big endian c1 0:8,c2 9:15
    uint16_t* c = memory + reg[R_R0];
    while(*c){
        // c1
        putc((char)((*c)&0xff),stdout);
        // c2
        char c2 = (*c) >> 8;
        if(c2){
            putc(c2,stdout);
        }
        ++c;
    }
    fflush(stdout);
}

/* Input Buffering */
struct termios original_tio;

void disable_input_buffering()
{
    tcgetattr(STDIN_FILENO, &original_tio);
    struct termios new_tio = original_tio;
    new_tio.c_lflag &= ~ICANON & ~ECHO;
    tcsetattr(STDIN_FILENO, TCSANOW, &new_tio);
}

void restore_input_buffering()
{
    tcsetattr(STDIN_FILENO, TCSANOW, &original_tio);
}

void handle_interrupt(int signal)
{
    restore_input_buffering();
    printf("\n");
    exit(-2);
}

void setup(){
    signal(SIGINT, handle_interrupt);
    disable_input_buffering();
}

int main(int argc, const char* argv[]){

    if(argc != 2){
        printf("Usage: ./lc3 <program>");
        exit(2);
    }
    if(!load_image_from_file(argv[1])){
        printf("Unable to load program from file %s",argv[1]);
        exit(2);
    }
    else{
      printf("Loaded program from file %s",argv[1]);
    }

  setup();
  /* start PC at 0x3000 */
  enum {PC_Start = 0x3000};
  reg[R_PC] = PC_Start;

  int running = 1;
  while(running){
    uint16_t instr = mem_read(reg[R_PC]++);
    uint16_t op = instr >> 12;
        switch (op)
        {
        case OP_ADD:
            op_add(instr);
            break;
        case OP_AND:
            op_and(instr);
            break;
        case OP_BR:
            op_br(instr);
            break;
        case OP_JMP:
            op_jmp(instr);
            break;
        case OP_JSR:
            op_jsr(instr);
            break;
        case OP_LD:
            op_ld(instr);
            break;
        case OP_LDI:
            op_ldi(instr);
            break;
        case OP_LDR:
            op_ldr(instr);
            break;
        case OP_LEA:
            op_lea(instr);
            break;
        case OP_NOT:
            op_not(instr);
            break;
        case OP_ST:
            op_st(instr);
            break;
        case OP_STI:
            op_sti(instr);
            break;
        case OP_STR:
            op_str(instr);
            break;
        case OP_TRAP:
            switch(instr & 0xff){
                case TRAP_PUTS:
                    trap_puts();
                    break;
                case TRAP_GETC:
                    trap_getc();
                    break;
                case TRAP_OUT:
                    trap_out();
                    break;
                case TRAP_PUTSP:
                    trap_putsp();
                    break;
                case TRAP_HALT:
                    puts("HALTING");
                    fflush(stdout);
                    running = 0;
                    break;
            }
            break;
        case OP_RES:
        case OP_RTI:
        default:
            abort();
            break;
        }
  }
  restore_input_buffering();
  return 0;
}
