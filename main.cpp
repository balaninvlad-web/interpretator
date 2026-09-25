#include <cstdint>
#include <vector>
#include <unordered_map>
#include <iostream>
#include <cstddef>    
#include <string>      

using word = uint32_t;
using sword = int32_t;

enum class ExceptionCause 
{
    kNone,
    kSystemCall,
    kMisalignedAccess,
    kIllegalInstruction,
    kOutOfBounds
};

enum class Opcode 
{
    kBeq, kSyscall, kLd, kSt, kMovz, 
    kAddi, kCls, kStp, kJ, kSsat, 
    kSelc, kAdd, kMovn, kLi, kSbit, kUnknown
};

struct Memory;

struct Instruction 
{
    Opcode op = Opcode::kUnknown;
    uint8_t rd = 0, rs = 0, rt = 0;
    uint8_t rs1 = 0, rs2 = 0, rt1 = 0, rt2 = 0, base = 0;
    sword imm = 0;
    uint32_t imm5 = 0;
    uint32_t instr_index = 0;
};

struct CpuState
{
    word regs[32] = {};
    word pc = 0;
    Memory* mem = nullptr;
    ExceptionCause cause = ExceptionCause::kNone;
    word exception_value = 0;
    bool halted = false;
    bool exception = false;
    word getReg (uint8_t index) const { return regs[index];}
    void setReg (uint8_t index, word value) { regs[index] = value;}
};

struct Memory
{
    std::vector<uint8_t> data;

    explicit Memory (size_t size) : data (size, 0) {}

    size_t size() const { return data.size(); }

    word read32 (word addr, CpuState* cpu) const //склеиваем так потому что хитровытраханный литлендиан
    {
        if (addr + 4 > data.size()) 
        {
            cpu->exception = true;
            cpu->cause = ExceptionCause::kOutOfBounds;
            cpu->exception_value = addr;
            return 0;
        }
        word b0 = data[addr];
        word b1 = data[addr + 1];
        word b2 = data[addr + 2];
        word b3 = data[addr + 3];
        return b0 | (b1 << 8) | (b2 << 16) | (b3 << 24);
    }
    void write32 (word addr, word value, CpuState* cpu) 
    {
        if (addr + 4 > data.size()) {
            cpu->exception = true;
            cpu->cause = ExceptionCause::kOutOfBounds;
            cpu->exception_value = addr;
            return;
        }
        data[addr]     = value & 0xFF;
        data[addr + 1] = (value >> 8) & 0xFF;
        data[addr + 2] = (value >> 16) & 0xFF;
        data[addr + 3] = (value >> 24) & 0xFF;
    }
};

sword sign_extend(word value, int bits) 
{
    word mask = 1u << (bits - 1);
    return static_cast<sword>((value ^ mask) - mask);
}

bool isAligned(word addr) { return (addr & 0x3) == 0; }

Instruction decode(word encoding);
void execute(CpuState* cpu, const Instruction& instr);
void SigException(CpuState* cpu, word num);
void handle_syscall(CpuState* cpu);
void handle_exception(CpuState* cpu);
void run(CpuState* cpu);

Instruction decode(word encoding)
{
    Instruction instr;
    word opcode6 = (encoding >> 26) & 0x3F;
    word opcode5 = encoding & 0x3F;

    switch (opcode6) //opcode
    {
        case 0b111111: //BEQ
            instr.op = Opcode::kBeq;
            instr.rs = (encoding >> 21) & 0x1F;
            instr.rt = (encoding >> 16) & 0x1F;
            instr.imm = encoding & 0xFFFF;
            break;
        case 0b000000: //syscall, movz, cls, Selc, add, movn
            switch (opcode5)
            {
                case 0b011101:
                    instr.op =  Opcode::kSyscall;
                    break;
                case 0b011100:
                    instr.op =  Opcode::kMovz;
                    instr.rs =  (encoding >> 21) & 0x1F;
                    instr.rt =  (encoding >> 16) & 0x1F;
                    instr.rd =  (encoding >> 11) & 0x1F;
                    break;
                case 0b110011:
                    instr.op =  Opcode::kCls;
                    instr.rd =  (encoding >> 21) & 0x1F;
                    instr.rs =  (encoding >> 16) & 0x1F;
                    break;
                case 0b010000:
                    instr.op =  Opcode::kSelc;
                    instr.rd =  (encoding >> 21) & 0x1F;
                    instr.rs1 = (encoding >> 16) & 0x1F;
                    instr.rs2 = (encoding >> 11) & 0x1F;
                    break;
                case 0b110010:
                    instr.op =  Opcode::kAdd;
                    instr.rs =  (encoding >> 21) & 0x1F;
                    instr.rt =  (encoding >> 16) & 0x1F;
                    instr.rd =  (encoding >> 11) & 0x1F;
                    break;
                case 0b000101:
                    instr.op =  Opcode::kMovn;
                    instr.rs =  (encoding >> 21) & 0x1F;
                    instr.rt =  (encoding >> 16) & 0x1F;
                    instr.rd =  (encoding >> 11) & 0x1F;
                    break;
                default:
                    instr.op = Opcode::kUnknown;
                    break;
            }
            break;
        case 0b010011: //Ld
            instr.op = Opcode::kLd;
            instr.base = (encoding >> 21) & 0x1F;
            instr.rt = (encoding >> 16) & 0x1F;
            instr.imm = encoding & 0x3FFF;
            break;
        case 0b011110: //St
            instr.op = Opcode::kSt;
            instr.base = (encoding >> 21) & 0x1F;
            instr.rt = (encoding >> 16) & 0x1F;
            instr.imm = encoding & 0x3FFF;
            break;
        case 0b111001: //addi
            instr.op = Opcode::kAddi;
            instr.rs = (encoding >> 21) & 0x1F;
            instr.rt = (encoding >> 16) & 0x1F;
            instr.imm = encoding & 0xFFFF;
            break;
        case 0b001110: // stp
            instr.op = Opcode::kStp;
            instr.base = (encoding >> 21) & 0x1F;
            instr.rt1 = (encoding >> 16) & 0x1F;
            instr.rt2 = (encoding >> 11) & 0x1F;
            instr.imm = encoding & 0x7FF; //offset
            break; 
        case 0b101111: //J
            instr.op = Opcode::kJ;
            instr.instr_index = encoding & 0x3FFFFFF;
            break;
        case 0b111100: //Ssat
            instr.op = Opcode::kSsat;
            instr.rd = (encoding >> 21) & 0x1F;
            instr.rs = (encoding >> 16) & 0x1F;
            instr.imm5 = (encoding >> 11) & 0x1F;
            break;
        case 0b010001: //Li
            instr.op = Opcode::kLi;
            instr.rt = (encoding >> 16) & 0x1F;
            instr.imm = encoding & 0xFFFF;
            break;
        case 0b010010: //Sbit
            instr.op = Opcode::kSbit;
            instr.rd = (encoding >> 21) & 0x1F;
            instr.rs = (encoding >> 16) & 0x1F;
            instr.imm5 = (encoding >> 11) & 0x1F;
            break;
        default:
            instr.op = Opcode::kUnknown;
            break;
    }
    return instr;
}

void execude (CpuState* cpu, const Instruction& instr)
{
    word old_pc = cpu->pc;
    bool pc_updated = false;

    switch (instr.op)
    {
        case Opcode::kBeq:
        {
            sword offset = sign_extend(instr.imm, 16);
            sword target = offset << 2;
            if (cpu->getReg(instr.rs) == cpu->getReg(instr.rt))
                cpu->pc = cpu->pc + target;
            else
                cpu->pc += 4;

            pc_updated = true;
            break;
        }
        case Opcode::kSyscall:
        {
            SigException(cpu, cpu->getReg(8)); //x8 - num of syscall
            break;
        }
        case Opcode::kLd:
        {
            word addr = cpu->getReg(instr.base) + sign_extend(instr.imm, 14);
            if (!isAligned(addr))
            {
                cpu->exception = true;
                cpu->cause = ExceptionCause::kMisalignedAccess;
                cpu->exception_value = addr;
                pc_updated = true; 
                break;
            }
            word value = cpu->mem->read32(addr, cpu);
            if (cpu->exception) 
            {
                pc_updated = true;
                break;
            }
            cpu->setReg(instr.rt, value);
            break;
        }
        case Opcode::kSt:
        {
            word addr = cpu->getReg(instr.base) + sign_extend(instr.imm, 14);
            if (!isAligned(addr))
            {
                cpu->exception = true;
                cpu->cause = ExceptionCause::kMisalignedAccess;
                cpu->exception_value = addr;
                break;
            }
            cpu->mem->write32(addr, cpu->getReg(instr.rt), cpu);
            break;
        }
        case Opcode::kMovz:
        {
            if (cpu->getReg(instr.rt) == 0)
                cpu->setReg(instr.rd, cpu->getReg(instr.rs));
            break;
        }
        case Opcode::kAddi:
        {
            cpu->setReg(instr.rt, (cpu->getReg(instr.rs) + sign_extend(instr.imm, 16)));
            break;
        }
        case Opcode::kCls:
        {
            word value = cpu->getReg(instr.rs);
            int count = 0; 
            if (value & 0x80000000)
                while (count < 32 && (value & (1U << (31 - count)))) ++count;
            else    
                count = 0;
            cpu->setReg(instr.rd, count);
            break;
        }
        case Opcode::kStp:
        {
            word addr = cpu->getReg(instr.base) + sign_extend(instr.imm, 11);
            if (!isAligned(addr))
            {
                cpu->exception = true;
                cpu->cause = ExceptionCause::kMisalignedAccess;
                cpu->exception_value = addr;
                break;
            }
            cpu->mem->write32(addr, cpu->getReg(instr.rt1), cpu);
            if (cpu->exception) { pc_updated = true; break; }
            cpu->mem->write32(addr + 4, cpu->getReg(instr.rt2), cpu);
            if (cpu->exception) { pc_updated = true; break; }
            break;
        }
        case Opcode::kJ:
        {
            cpu->pc = (cpu->pc & 0xF0000000) | (instr.instr_index << 2);
            pc_updated = true;
            break;
        }
        case Opcode::kSsat:
        {
            uint32_t n = instr.imm5;
            sword value = static_cast<sword>(cpu->getReg(instr.rs));
            if (n == 0 || n >= 32)
            {
                cpu->setReg(instr.rd, value);
                break;
            }
            sword max_val = (1 << (n - 1)) - 1;
            sword min_val = -(1 << (n - 1));
            if (value > max_val) value = max_val;
            if (value < min_val) value = min_val;
            cpu->setReg(instr.rd, static_cast<word>(value));
            break;
        }
        case Opcode::kSelc:
        {
            sword a = static_cast<sword>(cpu->getReg(instr.rs1));
            sword b = static_cast<sword>(cpu->getReg(instr.rs2));
            cpu->setReg(instr.rd, (a > b) ? a : b);
            break;
        }
        case Opcode::kAdd:
        {
            cpu->setReg(instr.rd, static_cast<sword>((cpu->getReg(instr.rs)) + (cpu->getReg(instr.rt))));
            break;
        }
        case Opcode::kMovn:
        {
            if ((cpu->getReg(instr.rt)) != 0) 
                cpu->setReg(instr.rd, cpu->getReg(instr.rs));
            break;
        }
        case Opcode::kLi:
        {
            sword imm = sign_extend(instr.imm, 16);
            cpu->setReg(instr.rt, static_cast<word>(imm));
            break;
        }
        case Opcode::kSbit:
        {
            cpu->setReg(instr.rd, 1u << instr.imm5);
            break;
        }
        default:
            cpu->exception = true;
            cpu->cause = ExceptionCause::kIllegalInstruction;
            cpu->exception_value = static_cast<word> (instr.op); 
            break;
    }
    if (!pc_updated) cpu->pc = old_pc + 4;
}

void SigException(CpuState* cpu, word num) 
{
    cpu->exception = true;
    cpu->cause = ExceptionCause::kSystemCall;
    cpu->exception_value = num;
}

void handle_syscall(CpuState* cpu)
{
    word num = cpu->getReg(8);

    switch (num)
    {
        case 64: 
        {
            word fd = cpu->getReg(0);
            word buf = cpu->getReg(1);
            word count = cpu->getReg(2);

            if (buf + count > cpu->mem->size())
            {
                std::cerr << "[syscall write] buffer out of bounds\n";
                cpu->setReg(0, static_cast<word>(-1));
                    return;
            }
            std::ostream& out = (fd == 2) ? std::cerr : std::cout;
                for (word i = 0; i < count; ++i)
                    out << static_cast<char>(cpu->mem->data[buf + i]);

            cpu->setReg(0, count);   // возвращаемое значение = сколько байт вывели
            break;
        }
        case 93:
        {
            cpu->halted = true;
            break;
        }
        default:
        {
            std::cerr << "[syscall] unknown number: " << num << "\n";
            cpu->halted = true;
            break;
        }
    }
}

void handle_exception(CpuState* cpu) 
{
    switch (cpu->cause) 
    {
        case ExceptionCause::kSystemCall:
            handle_syscall(cpu);
            break;
        case ExceptionCause::kMisalignedAccess:
        case ExceptionCause::kOutOfBounds:
        case ExceptionCause::kIllegalInstruction:
            std::cerr << "Exception: cause=" 
                      << static_cast<int>(cpu->cause)
                      << " value=0x" << std::hex << cpu->exception_value << "\n";
            cpu->halted = true;
            break;
        case ExceptionCause::kNone:
            break;
    }

    cpu->exception = false;
    cpu->cause = ExceptionCause::kNone;
}

void run (CpuState* cpu)
{
    while (!cpu->halted)
    {
        word encoding = cpu->mem->read32(cpu->pc, cpu);
        if (cpu->exception) 
        {
            handle_exception(cpu);
            continue;
        }
        Instruction instr = decode (encoding);
        execude (cpu, instr);
        if (cpu->exception) 
        {
            handle_exception (cpu);
        }
    }   
}

int main() 
{
    Memory mem(1 << 20);           // 1 МБ
    CpuState cpu;
    cpu.mem = &mem;
    cpu.pc = 0;

    // Программа:
    //   LI  X1, #0x200   ; buf
    //   LI  X0, #1       ; fd = stdout
    //   LI  X2, #6       ; count = 6
    //   LI  X8, #64      ; syscall write
    //   SYSCALL
    //   LI  X8, #93      ; syscall exit
    //   SYSCALL
    // Данные по адресу 0x200: "Hello\n"

    word prog[] = 
    {
        0x44010200,   // LI X1, #0x200
        0x44000001,   // LI X0, #1
        0x44020006,   // LI X2, #6
        0x44080040,   // LI X8, #64
        0x0000001D,   // SYSCALL
        0x4408005D,   // LI X8, #93
        0x0000001D,   // SYSCALL
    };

    for (size_t i = 0; i < sizeof(prog) / sizeof(prog[0]); ++i)
        mem.write32(static_cast<word>(i * 4), prog[i], &cpu);

    const char* msg = "Hello\n";
    for (size_t i = 0; i < 6; ++i)
        mem.data[0x200 + i] = static_cast<uint8_t>(msg[i]);

    run(&cpu);
    return 0;
}