/*************************************************************************************|
|   1. YOU ARE NOT ALLOWED TO SHARE/PUBLISH YOUR CODE (e.g., post on piazza or online)|
|   2. Fill mipssim.c                                                                 |
|   3. Do not use any other .c files neither alter mipssim.h or parser.h              |
|   4. Do not include any other library files
|
|      gcc -o mipssim mipssim.c memory_hierarchy.c -std=gnu99 -lm
|      ./mipssim 0 memfile-simple.txt regfile.txt
|*************************************************************************************/

#include "mipssim.h"

#define BREAK_POINT 200000 // exit after so many cycles -- useful for debugging

// Rest of instruction types
#define J_TYPE 2
#define I_TYPE 3
#define BRANCH_TYPE 4
#define MEM_TYPE 5

// Rest of OP codes
#define ADDU 33

// Global variables
char mem_init_path[1000];
char reg_init_path[1000];

uint32_t cache_size = 0;
struct architectural_state arch_state;

static inline uint8_t get_instruction_type(int opcode)
{
    switch (opcode) {
        /// opcodes are defined in mipssim.h

        case SPECIAL:
            return R_TYPE; // includes add, addu, slt
        case ADDI:
            return I_TYPE;
        case LW:
        case SW:
            return MEM_TYPE;
        case BEQ:
            return BRANCH_TYPE;
        case J:
            return J_TYPE;
        case EOP:
            return EOP_TYPE;
        default:
            assert(false);
    }
    assert(false);
}


void FSM()
{
    struct ctrl_signals *control = &arch_state.control;
    struct instr_meta *IR_meta = &arch_state.IR_meta;

    //reset control signals
    memset(control, 0, (sizeof(struct ctrl_signals)));

    int opcode = IR_meta->opcode;
    int state = arch_state.state;
    switch (state) {
        case INSTR_FETCH:
            control->MemRead = 1;
            control->ALUSrcA = 0;
            control->IorD = 0;
            control->IRWrite = 1;
            control->ALUSrcB = 1;
            control->ALUOp = 0;
            control->PCWrite = 1;
            control->PCSource = 0;
            state = DECODE;
            break;
        case DECODE:
            control->ALUSrcA = 0;
            control->ALUSrcB = 3;
            control->ALUOp = 0;
            if (IR_meta->type == R_TYPE) state = EXEC;
            else if (IR_meta->type == I_TYPE) state = I_TYPE_EXEC;
            else if (IR_meta->type == BRANCH_TYPE) state = BRANCH_COMPL;
            else if (IR_meta->type == MEM_TYPE) state = MEM_ADDR_COMP;
            else if (IR_meta->type == J_TYPE) state = JUMP_COMPL;
            else if (opcode == EOP) state = EXIT_STATE;
            else assert(false);
            break;
        case EXEC:
            control->ALUSrcA = 1;
            control->ALUSrcB = 0;
            control->ALUOp = 2;
            state = R_TYPE_COMPL;
            break;
        case R_TYPE_COMPL:
            control->RegDst = 1;
            control->RegWrite = 1;
            control->MemtoReg = 0;
            state = INSTR_FETCH;
            break;
        case I_TYPE_EXEC:
            control->ALUSrcA = 1;
            control->ALUSrcB = 2;
            control->ALUOp = 0;
            state = I_TYPE_COMPL;
            break;
        case I_TYPE_COMPL:
            control->RegDst = 0;
            control->MemtoReg = 0;
            control->RegWrite = 1;
            state = INSTR_FETCH;
            break;
        case BRANCH_COMPL:
            control->ALUSrcA = 1;
            control->ALUSrcB = 0;
            control->ALUOp = 1;
            control->PCWriteCond = 1;
            control->PCSource = 1;
            state = INSTR_FETCH;
            break;
        case JUMP_COMPL:
            control->PCWrite = 1;
            control->PCSource = 2;
            state = INSTR_FETCH;
            break;
        case MEM_ADDR_COMP:
            control->ALUSrcA = 1;
            control->ALUSrcB = 2;
            control->ALUOp = 0;
            switch (IR_meta->opcode) {
                case SW:
                    state = MEM_ACCESS_ST;
                    break;
                case LW:
                    state = MEM_ACCESS_LD;
                    break;
                default:
                    printf("Missed case in FSM -> MEM_ADDR_COMP for IR_meta->opcode: %d\n", IR_meta->opcode);
                    assert(false);
            }
            break;
        case MEM_ACCESS_ST:
            control->MemWrite = 1;
            control->IorD = 1;
            state = INSTR_FETCH;
            break;
        case MEM_ACCESS_LD:
            control->MemRead = 1;
            control->IorD = 1;
            state = WB_STEP;
            break;
        case WB_STEP:
            control->RegDst = 0;
            control->RegWrite = 1;
            control->MemtoReg = 1;
            state = INSTR_FETCH;
            break;
        default: assert(false);
    }
    arch_state.state = state;
}


void instruction_fetch()
{
    if (arch_state.control.MemRead) {
        int address = arch_state.curr_pipe_regs.pc;
        arch_state.next_pipe_regs.IR = memory_read(address);
        arch_state.next_pipe_regs.MDR = memory_read(address);
    }
}

void decode_and_read_RF()
{
    int read_register_1 = arch_state.IR_meta.reg_21_25;
    int read_register_2 = arch_state.IR_meta.reg_16_20;
    check_is_valid_reg_id(read_register_1);
    check_is_valid_reg_id(read_register_2);
    arch_state.next_pipe_regs.A = arch_state.registers[read_register_1];
    arch_state.next_pipe_regs.B = arch_state.registers[read_register_2];
}

void execute()
{
    struct ctrl_signals *control = &arch_state.control;
    struct instr_meta *IR_meta = &arch_state.IR_meta;
    struct pipe_regs *curr_pipe_regs = &arch_state.curr_pipe_regs;
    struct pipe_regs *next_pipe_regs = &arch_state.next_pipe_regs;

    int alu_opA = control->ALUSrcA == 1 ? curr_pipe_regs->A : curr_pipe_regs->pc;
    int alu_opB = 0;
    int immediate = IR_meta->immediate; // already sign-extended
    int shifted_immediate = (immediate) << 2;
    int shifted_j_offset = IR_meta->jmp_offset << 2;

    switch (control->ALUSrcB) {
        case 0:
            alu_opB = curr_pipe_regs->B;
            break;
        case 1:
            alu_opB = WORD_SIZE;
            break;
        case 2:
            // For Memory Reference or ADDI?
            alu_opB = immediate;
            break;
        case 3:
            alu_opB = shifted_immediate;
            break;
        default:
            assert(false);
    }


    switch (control->ALUOp) {
        case 0: // ALU performs an add
            next_pipe_regs->ALUOut = alu_opA + alu_opB;
            break;
        case 1: // ALU performs sub
            next_pipe_regs->ALUOut = alu_opA - alu_opB;
            break;
        case 2: // R_Type, ALU action determined by function field
            if ((IR_meta->function == ADD) || (IR_meta->function == ADDU)) // ADDU same functionality bc no exceptions
                next_pipe_regs->ALUOut = alu_opA + alu_opB;
            else if (IR_meta->function == SLT) {
                if (alu_opA < alu_opB)
                    next_pipe_regs->ALUOut = 1;
                else
                    next_pipe_regs->ALUOut = 0;
            }
            else {
                printf("Missed a case in execute() -> ALUOp switch -> for IR_meta->function:%d\n", IR_meta->function);
                assert(false);
            }
            break;
        default:
            assert(false);
    }

    // PC calculation
    switch (control->PCSource) {
        case 0:
            next_pipe_regs->pc = next_pipe_regs->ALUOut;
            break;
        case 1: // beq
            if (!next_pipe_regs->ALUOut) { // i.e. A - B == 0; A == B
                if (control->PCWriteCond) {
                    next_pipe_regs->pc = curr_pipe_regs->ALUOut;
                    control->PCWrite = 1;
                }
            }
            break;
        case 2: // Jump
            if (control->PCWrite)
                next_pipe_regs->pc = (curr_pipe_regs->pc & 0xf000000) | shifted_j_offset; // NOLINT(hicpp-signed-bitwise)
            break;
        default:
            printf("Missed case in execute() for control->PCSource: %d\n", control->PCSource);
            assert(false);
    }
}


void memory_access() {
    ///@students: appropriate calls to functions defined in memory_hierarchy.c must be added
    int address = arch_state.curr_pipe_regs.ALUOut; // TODO: may be curr_pipe_reg instead

    if ((arch_state.control.MemWrite) && (arch_state.control.IorD)) { // store word
        // TODO: may be next_pipe_reg instead
        int write_data = arch_state.next_pipe_regs.B;

        // Memory[ALUOut] = B
        printf("\tMemory[%d] = %d\n", address, write_data);
        memory_write(address, write_data);

    }

    if (arch_state.control.IorD) { // load word
        if (arch_state.control.MemRead) {
            // MDR = Memory[ALUOut]
            arch_state.next_pipe_regs.MDR = memory_read(address);
        }
    }
}

void write_back()
{
    if (arch_state.control.RegWrite) {
        // load
        if ((arch_state.control.MemtoReg) && (!arch_state.control.RegDst)) {
            int write_reg_id = arch_state.IR_meta.reg_16_20;
            int write_data = arch_state.next_pipe_regs.MDR;
            check_is_valid_reg_id(write_reg_id);

            // Reg[IR[20-16]] = MDR
            arch_state.registers[write_reg_id] = write_data;
            printf("\tReg $%u = %d \n", write_reg_id, write_data);
        } else if ((!arch_state.control.MemtoReg) && (!arch_state.control.RegDst)) { // addi
            int write_reg_id = arch_state.IR_meta.reg_16_20;
            int write_data = arch_state.curr_pipe_regs.ALUOut;
            check_is_valid_reg_id(write_reg_id);

            // Reg[IR[20-16]] = immediate
            arch_state.registers[write_reg_id] = write_data;
            printf("\tReg $%u = %d \n", write_reg_id, write_data);
        } else { // r-type
            int write_reg_id = arch_state.IR_meta.reg_11_15;
            check_is_valid_reg_id(write_reg_id);
            int write_data = arch_state.curr_pipe_regs.ALUOut;
            if (write_reg_id > 0) { // that's for R-instructions
                arch_state.registers[write_reg_id] = write_data;
                printf("\tReg $%u = %d \n", write_reg_id, write_data);
            } else printf("Attempting to write reg_0. That is likely a mistake \n");
        }
    }
}


void set_up_IR_meta(int IR, struct instr_meta *IR_meta)
{
    IR_meta->opcode = get_piece_of_a_word(IR, OPCODE_OFFSET, OPCODE_SIZE);
    IR_meta->immediate = get_sign_extended_imm_id(IR, IMMEDIATE_OFFSET);
    IR_meta->function = get_piece_of_a_word(IR, 0, 6);
    IR_meta->jmp_offset = get_piece_of_a_word(IR, 0, 26);
    IR_meta->reg_11_15 = (uint8_t) get_piece_of_a_word(IR, 11, REGISTER_ID_SIZE);
    IR_meta->reg_16_20 = (uint8_t) get_piece_of_a_word(IR, 16, REGISTER_ID_SIZE);
    IR_meta->reg_21_25 = (uint8_t) get_piece_of_a_word(IR, 21, REGISTER_ID_SIZE);
    IR_meta->type = get_instruction_type(IR_meta->opcode);

    switch (IR_meta->opcode) {
        // debug switch, no logic implemented here.
        case SPECIAL:
            if (IR_meta->function == ADD)
                printf("Executing ADD(op:%d), $%u = $%u + $%u (function: %u)\n",
                       IR_meta->opcode,  IR_meta->reg_11_15, IR_meta->reg_21_25,  IR_meta->reg_16_20, IR_meta->function);
            else if (IR_meta->function == ADDU)
                printf("Executing ADDU(op:%d), $%u = $%u + $%u (function: %u)\n",
                       IR_meta->opcode,  IR_meta->reg_11_15, IR_meta->reg_21_25,  IR_meta->reg_16_20, IR_meta->function);
            else if (IR_meta->function == SLT)
                printf("Executing SLT(op:%d), $%u = $%u < $%u (function: %u) (1 if true, else 0)\n",
                       IR_meta->opcode,  IR_meta->reg_11_15, IR_meta->reg_21_25,  IR_meta->reg_16_20, IR_meta->function);
            else assert(false);
            break;
        case ADDI:
            printf("Executing ADDI(op:%d), $%u = $%u + %u\n",
                   IR_meta->opcode,  IR_meta->reg_16_20, IR_meta->reg_21_25,  IR_meta->immediate);
            break;
        case LW:
            printf("Executing LW(op:%d), $%u = MEM[$%u + %u]\n",
                   IR_meta->opcode,  IR_meta->reg_16_20, IR_meta->reg_21_25,  IR_meta->immediate);
            break;
        case SW:
            printf("Executing SW(op:%d), MEM[$%u + %u] = $%u\n",
                   IR_meta->opcode,  IR_meta->reg_21_25,  IR_meta->immediate, IR_meta->reg_16_20);
            break;
        case BEQ:
            printf("Executing BEQ(op:%d):\n"
                   "\tif (%u == %u)\n"
                   "\t\tPC = OLD_PC + %d\n"
                   "\telse\n"
                   "\t\tPC = OLD_PC + 1\n",
                   IR_meta->opcode,  arch_state.registers[IR_meta->reg_21_25],  arch_state.registers[IR_meta->reg_16_20], (IR_meta->immediate<<2)/4); // NOLINT(hicpp-signed-bitwise)
            break;
        case J:
            printf("Executing J(op:%d) with immediate %u\n",
                   IR_meta->opcode, IR_meta->immediate);
            break;
        case EOP:
            printf("Executing EOP(%d) \n", IR_meta->opcode);
            break;
        default: assert(false);
    }
}

void assign_pipeline_registers_for_the_next_cycle()
{
    struct ctrl_signals *control = &arch_state.control;
    struct instr_meta *IR_meta = &arch_state.IR_meta;
    struct pipe_regs *curr_pipe_regs = &arch_state.curr_pipe_regs;
    struct pipe_regs *next_pipe_regs = &arch_state.next_pipe_regs;

    if (control->IRWrite) {
        curr_pipe_regs->IR = next_pipe_regs->IR;
        printf("PC %d: ", curr_pipe_regs->pc / 4);
        set_up_IR_meta(curr_pipe_regs->IR, IR_meta);
    } else if (control->PCWriteCond) { // for branching
        if (control->PCWrite) {
        curr_pipe_regs->pc = next_pipe_regs->pc;
        control->PCWriteCond = 0;
        control->PCWrite = 0;
        }
    }
    curr_pipe_regs->ALUOut = next_pipe_regs->ALUOut;
    curr_pipe_regs->A = next_pipe_regs->A;
    curr_pipe_regs->B = next_pipe_regs->B;
    curr_pipe_regs->MDR = next_pipe_regs->MDR;

    if (control->PCWrite) {
        check_address_is_word_aligned(next_pipe_regs->pc);
        curr_pipe_regs->pc = next_pipe_regs->pc;
    }
}


int main(int argc, const char* argv[])
{
    /*--------------------------------------
    /------- Global Variable Init ----------
    /--------------------------------------*/
    parse_arguments(argc, argv);
    arch_state_init(&arch_state);
    ///@students WARNING: Do NOT change/move/remove main's code above this point!
    while (true) {

        ///@students: Fill/modify the function bodies of the 7 functions below,
        /// Do NOT modify the main() itself, you only need to
        /// write code inside the definitions of the functions called below.

        FSM();

        instruction_fetch();

        decode_and_read_RF();

        execute();

        memory_access();

        write_back();

        assign_pipeline_registers_for_the_next_cycle();


       ///@students WARNING: Do NOT change/move/remove code below this point!
        marking_after_clock_cycle();
        //printf("Clock cycle finished: %llu\n", arch_state.clock_cycle);

        arch_state.clock_cycle++;
        // Check exit statements
        if (arch_state.state == EXIT_STATE) { // I.E. EOP instruction!
            printf("Exiting because the exit state was reached \n");
            break;
        }
        if (arch_state.clock_cycle == BREAK_POINT) {
            printf("Exiting because the break point (%u) was reached \n", BREAK_POINT);
            break;
        }
    }
    marking_at_the_end();
}
