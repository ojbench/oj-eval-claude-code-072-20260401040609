#include <iostream>
#include <fstream>
#include <vector>
#include <cstring>
#include <cstdint>
#include <iomanip>

using namespace std;

class RISCVSimulator {
private:
    static const uint32_t MEMORY_SIZE = 0x600000; // 6MB memory
    uint8_t memory[MEMORY_SIZE];
    uint32_t registers[32];
    uint32_t pc;
    bool running;
    uint64_t instruction_count;

    // Sign extend functions
    int32_t signExtend(uint32_t value, int bits) {
        uint32_t mask = 1U << (bits - 1);
        return (value ^ mask) - mask;
    }

    // Extract instruction fields
    uint32_t getRd(uint32_t inst) { return (inst >> 7) & 0x1F; }
    uint32_t getRs1(uint32_t inst) { return (inst >> 15) & 0x1F; }
    uint32_t getRs2(uint32_t inst) { return (inst >> 20) & 0x1F; }
    uint32_t getOpcode(uint32_t inst) { return inst & 0x7F; }
    uint32_t getFunct3(uint32_t inst) { return (inst >> 12) & 0x7; }
    uint32_t getFunct7(uint32_t inst) { return (inst >> 25) & 0x7F; }

    // Immediate extraction
    int32_t getImmI(uint32_t inst) {
        return signExtend(inst >> 20, 12);
    }

    int32_t getImmS(uint32_t inst) {
        uint32_t imm = ((inst >> 7) & 0x1F) | ((inst >> 20) & 0xFE0);
        return signExtend(imm, 12);
    }

    int32_t getImmB(uint32_t inst) {
        uint32_t imm = ((inst >> 7) & 0x1E) | ((inst >> 20) & 0x7E0) |
                       ((inst << 4) & 0x800) | ((inst >> 19) & 0x1000);
        return signExtend(imm, 13);
    }

    int32_t getImmU(uint32_t inst) {
        return inst & 0xFFFFF000;
    }

    int32_t getImmJ(uint32_t inst) {
        uint32_t imm = (inst & 0xFF000) |           // imm[19:12]
                       ((inst >> 9) & 0x800) |       // imm[11]
                       ((inst >> 20) & 0x7FE) |      // imm[10:1]
                       ((inst >> 11) & 0x100000);    // imm[20]
        return signExtend(imm, 21);
    }

    // Memory access
    uint32_t readWord(uint32_t addr) {
        if (addr + 3 >= MEMORY_SIZE) return 0;
        return memory[addr] | (memory[addr+1] << 8) |
               (memory[addr+2] << 16) | (memory[addr+3] << 24);
    }

    uint16_t readHalf(uint32_t addr) {
        if (addr + 1 >= MEMORY_SIZE) return 0;
        return memory[addr] | (memory[addr+1] << 8);
    }

    uint8_t readByte(uint32_t addr) {
        if (addr >= MEMORY_SIZE) return 0;
        return memory[addr];
    }

    void writeWord(uint32_t addr, uint32_t value) {
        if (addr + 3 >= MEMORY_SIZE) return;
        memory[addr] = value & 0xFF;
        memory[addr+1] = (value >> 8) & 0xFF;
        memory[addr+2] = (value >> 16) & 0xFF;
        memory[addr+3] = (value >> 24) & 0xFF;
    }

    void writeHalf(uint32_t addr, uint16_t value) {
        if (addr + 1 >= MEMORY_SIZE) return;
        memory[addr] = value & 0xFF;
        memory[addr+1] = (value >> 8) & 0xFF;
    }

    void writeByte(uint32_t addr, uint8_t value) {
        if (addr >= MEMORY_SIZE) return;
        memory[addr] = value;
    }

    void handleSyscall() {
        uint32_t syscall_id = registers[17]; // a7 register

        switch(syscall_id) {
            case 64: // write syscall (fd, buffer, size)
                {
                    uint32_t fd = registers[10]; // a0
                    uint32_t buffer = registers[11]; // a1
                    uint32_t size = registers[12]; // a2
                    if (fd == 1) { // stdout
                        for (uint32_t i = 0; i < size && buffer + i < MEMORY_SIZE; i++) {
                            cout << (char)memory[buffer + i];
                        }
                    }
                }
                break;
            case 93: // exit syscall
                running = false;
                break;
            default:
                // Unknown syscall - just continue
                break;
        }
    }

    void executeInstruction(uint32_t inst) {
        uint32_t opcode = getOpcode(inst);
        uint32_t rd = getRd(inst);
        uint32_t rs1 = getRs1(inst);
        uint32_t rs2 = getRs2(inst);
        uint32_t funct3 = getFunct3(inst);
        uint32_t funct7 = getFunct7(inst);

        int32_t imm_i = getImmI(inst);
        int32_t imm_s = getImmS(inst);
        int32_t imm_b = getImmB(inst);
        int32_t imm_u = getImmU(inst);
        int32_t imm_j = getImmJ(inst);

        uint32_t nextPc = pc + 4;

        switch(opcode) {
            case 0x37: // LUI
                registers[rd] = imm_u;
                break;
            case 0x17: // AUIPC
                registers[rd] = pc + imm_u;
                break;
            case 0x6F: // JAL
                registers[rd] = pc + 4;
                nextPc = pc + imm_j;
                break;
            case 0x67: // JALR
                registers[rd] = pc + 4;
                nextPc = (registers[rs1] + imm_i) & ~1;
                break;
            case 0x63: // Branch
                switch(funct3) {
                    case 0x0: // BEQ
                        if (registers[rs1] == registers[rs2])
                            nextPc = pc + imm_b;
                        break;
                    case 0x1: // BNE
                        if (registers[rs1] != registers[rs2])
                            nextPc = pc + imm_b;
                        break;
                    case 0x4: // BLT
                        if ((int32_t)registers[rs1] < (int32_t)registers[rs2])
                            nextPc = pc + imm_b;
                        break;
                    case 0x5: // BGE
                        if ((int32_t)registers[rs1] >= (int32_t)registers[rs2])
                            nextPc = pc + imm_b;
                        break;
                    case 0x6: // BLTU
                        if (registers[rs1] < registers[rs2])
                            nextPc = pc + imm_b;
                        break;
                    case 0x7: // BGEU
                        if (registers[rs1] >= registers[rs2])
                            nextPc = pc + imm_b;
                        break;
                }
                break;
            case 0x03: // Load
                {
                    uint32_t addr = registers[rs1] + imm_i;
                    switch(funct3) {
                        case 0x0: // LB
                            registers[rd] = signExtend(readByte(addr), 8);
                            break;
                        case 0x1: // LH
                            registers[rd] = signExtend(readHalf(addr), 16);
                            break;
                        case 0x2: // LW
                            registers[rd] = readWord(addr);
                            break;
                        case 0x4: // LBU
                            registers[rd] = readByte(addr);
                            break;
                        case 0x5: // LHU
                            registers[rd] = readHalf(addr);
                            break;
                    }
                }
                break;
            case 0x23: // Store
                {
                    uint32_t addr = registers[rs1] + imm_s;
                    switch(funct3) {
                        case 0x0: // SB
                            writeByte(addr, registers[rs2]);
                            break;
                        case 0x1: // SH
                            writeHalf(addr, registers[rs2]);
                            break;
                        case 0x2: // SW
                            writeWord(addr, registers[rs2]);
                            break;
                    }
                }
                break;
            case 0x13: // I-type ALU
                switch(funct3) {
                    case 0x0: // ADDI
                        registers[rd] = registers[rs1] + imm_i;
                        break;
                    case 0x2: // SLTI
                        registers[rd] = ((int32_t)registers[rs1] < imm_i) ? 1 : 0;
                        break;
                    case 0x3: // SLTIU
                        registers[rd] = (registers[rs1] < (uint32_t)imm_i) ? 1 : 0;
                        break;
                    case 0x4: // XORI
                        registers[rd] = registers[rs1] ^ imm_i;
                        break;
                    case 0x6: // ORI
                        registers[rd] = registers[rs1] | imm_i;
                        break;
                    case 0x7: // ANDI
                        registers[rd] = registers[rs1] & imm_i;
                        break;
                    case 0x1: // SLLI
                        registers[rd] = registers[rs1] << (imm_i & 0x1F);
                        break;
                    case 0x5: // SRLI/SRAI
                        if (funct7 == 0x00)
                            registers[rd] = registers[rs1] >> (imm_i & 0x1F);
                        else
                            registers[rd] = (int32_t)registers[rs1] >> (imm_i & 0x1F);
                        break;
                }
                break;
            case 0x33: // R-type ALU
                switch(funct3) {
                    case 0x0: // ADD/SUB/MUL
                        if (funct7 == 0x00)
                            registers[rd] = registers[rs1] + registers[rs2];
                        else if (funct7 == 0x20)
                            registers[rd] = registers[rs1] - registers[rs2];
                        else if (funct7 == 0x01) // MUL
                            registers[rd] = (int32_t)registers[rs1] * (int32_t)registers[rs2];
                        break;
                    case 0x1: // SLL/MULH
                        if (funct7 == 0x00)
                            registers[rd] = registers[rs1] << (registers[rs2] & 0x1F);
                        else if (funct7 == 0x01) { // MULH
                            int64_t result = (int64_t)(int32_t)registers[rs1] * (int64_t)(int32_t)registers[rs2];
                            registers[rd] = result >> 32;
                        }
                        break;
                    case 0x2: // SLT/MULHSU
                        if (funct7 == 0x00)
                            registers[rd] = ((int32_t)registers[rs1] < (int32_t)registers[rs2]) ? 1 : 0;
                        else if (funct7 == 0x01) { // MULHSU
                            int64_t result = (int64_t)(int32_t)registers[rs1] * (uint64_t)registers[rs2];
                            registers[rd] = result >> 32;
                        }
                        break;
                    case 0x3: // SLTU/MULHU
                        if (funct7 == 0x00)
                            registers[rd] = (registers[rs1] < registers[rs2]) ? 1 : 0;
                        else if (funct7 == 0x01) { // MULHU
                            uint64_t result = (uint64_t)registers[rs1] * (uint64_t)registers[rs2];
                            registers[rd] = result >> 32;
                        }
                        break;
                    case 0x4: // XOR/DIV
                        if (funct7 == 0x00)
                            registers[rd] = registers[rs1] ^ registers[rs2];
                        else if (funct7 == 0x01) { // DIV
                            if (registers[rs2] != 0)
                                registers[rd] = (int32_t)registers[rs1] / (int32_t)registers[rs2];
                            else
                                registers[rd] = -1;
                        }
                        break;
                    case 0x5: // SRL/SRA/DIVU
                        if (funct7 == 0x00)
                            registers[rd] = registers[rs1] >> (registers[rs2] & 0x1F);
                        else if (funct7 == 0x20)
                            registers[rd] = (int32_t)registers[rs1] >> (registers[rs2] & 0x1F);
                        else if (funct7 == 0x01) { // DIVU
                            if (registers[rs2] != 0)
                                registers[rd] = registers[rs1] / registers[rs2];
                            else
                                registers[rd] = 0xFFFFFFFF;
                        }
                        break;
                    case 0x6: // OR/REM
                        if (funct7 == 0x00)
                            registers[rd] = registers[rs1] | registers[rs2];
                        else if (funct7 == 0x01) { // REM
                            if (registers[rs2] != 0)
                                registers[rd] = (int32_t)registers[rs1] % (int32_t)registers[rs2];
                            else
                                registers[rd] = registers[rs1];
                        }
                        break;
                    case 0x7: // AND/REMU
                        if (funct7 == 0x00)
                            registers[rd] = registers[rs1] & registers[rs2];
                        else if (funct7 == 0x01) { // REMU
                            if (registers[rs2] != 0)
                                registers[rd] = registers[rs1] % registers[rs2];
                            else
                                registers[rd] = registers[rs1];
                        }
                        break;
                }
                break;
            case 0x0F: // FENCE - NOP for now
                break;
            case 0x73: // System calls
                if (inst == 0x00000073) { // ECALL
                    handleSyscall();
                } else if (inst == 0x00100073) { // EBREAK
                    running = false;
                }
                break;
        }

        registers[0] = 0; // x0 is always 0
        pc = nextPc;
        instruction_count++;
    }

public:
    RISCVSimulator() : pc(0), running(true), instruction_count(0) {
        memset(memory, 0, sizeof(memory));
        memset(registers, 0, sizeof(registers));
        registers[2] = 0x80000000; // sp (stack pointer) - typical RISC-V stack location
    }

    void loadProgram(const vector<uint8_t>& program) {
        size_t size = min(program.size(), (size_t)MEMORY_SIZE);
        memcpy(memory, program.data(), size);
    }

    void run(uint64_t max_instructions = 100000000) {
        while(running && pc < MEMORY_SIZE - 3 && instruction_count < max_instructions) {
            uint32_t inst = readWord(pc);
            if (inst == 0) {
                break;
            }
            executeInstruction(inst);
        }
    }

    uint32_t getRegister(int reg) {
        if (reg >= 0 && reg < 32)
            return registers[reg];
        return 0;
    }
};

int main() {
    ios_base::sync_with_stdio(false);
    cin.tie(NULL);

    vector<uint8_t> program;

    // Read binary data from stdin
    char byte;
    while(cin.get(byte)) {
        program.push_back((uint8_t)byte);
    }

    RISCVSimulator sim;
    sim.loadProgram(program);
    sim.run();

    // Output the result in register a0 as a signed integer
    cout << (int32_t)sim.getRegister(10) << endl;

    return 0;
}
