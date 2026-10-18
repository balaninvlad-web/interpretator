#include <cstdint>
#include <vector>
#include <unordered_map>
#include <iostream>
#include <cstddef>    
#include <string>
#include <cstring>
#include <fstream>
#include <iomanip>

using word = uint32_t;

enum class ExceptionCause 
{
    kNone,
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

const char* opcode_name(Opcode op) 
{
    switch (op) 
    {
        case Opcode::kBeq:      return "BEQ";
        case Opcode::kSyscall:  return "SYSCALL";
        case Opcode::kLd:       return "LD";
        case Opcode::kSt:       return "ST";
        case Opcode::kMovz:     return "MOVZ";
        case Opcode::kAddi:     return "ADDI";
        case Opcode::kCls:      return "CLS";
        case Opcode::kStp:      return "STP";
        case Opcode::kJ:        return "J";
        case Opcode::kSsat:     return "SSAT";
        case Opcode::kSelc:     return "SELC";
        case Opcode::kAdd:      return "ADD";
        case Opcode::kMovn:     return "MOVN";
        case Opcode::kLi:       return "LI";
        case Opcode::kSbit:     return "SBIT";
        default:                return "UNKNOWN";
    }
}

struct Memory;

struct Instruction 
{
    Opcode op = Opcode::kUnknown;
    uint8_t rd = 0, rs = 0, rt = 0;
    uint8_t rs1 = 0, rs2 = 0, rt1 = 0, rt2 = 0, base = 0;
    int32_t imm = 0;
    uint32_t imm5 = 0;
    uint32_t instr_index = 0;
};

struct BasicBlock
    {
        std::vector<Instruction> instrs;
    };


struct CpuState
{
    word regs[32] = {};
    word pc = 0;
    Memory* mem = nullptr;
    bool syscall_pending = false;
    word syscall_num = 0;
    ExceptionCause cause = ExceptionCause::kNone;
    word exception_value = 0;
    bool halted = false;
    bool exception = false;
    word getReg (uint8_t index) const { return regs[index];}
    void setReg (uint8_t index, word value) { regs[index] = value;}
    void dump (bool full = false) const;
    std::unordered_map<word, BasicBlock> bb_cache;
};

struct Memory
{
    std::vector<uint8_t> data;

    explicit Memory (size_t size) : data (size, 0) {}

    size_t size() const { return data.size(); }

    bool read32 (word addr, word& out) const //склеиваем так потому что хитровытраханный литлендиан
    {
        if (data.size() < 4 || addr > data.size() - 4) return false;
        std::memcpy(&out, &data[addr], 4);
        return true;
    }
    bool write32 (word addr, word value) 
    {
        if (data.size() < 4 || addr > data.size() - 4) return false; 
        std::memcpy(&data[addr], &value, 4);
        return true;
    }

    void load_binary (const std::string& path, word base, CpuState* cpu)
    {
        std::ifstream file(path, std::ios::binary);
        if (!file)
        {
            std::cerr << "Cannot open " << path << "\n";
            cpu->halted = true;
            return;
        } 
        
        file.seekg(0, std::ios::end);
        size_t size = file.tellg();
        file.seekg(0);

        if (base + size > data.size())
        {
            std::cerr << "Binary too large for memory\n";
            cpu->halted = true;
            return;
        }

        file.read(reinterpret_cast<char*>(data.data() + base), size);
    }
};

// sext<15, 32>(m);

template<int FromBits, int ToBits>
constexpr int32_t setx(uint32_t value)
{
    static_assert(FromBits > 0,         "FromBits must be > 0");
    static_assert(FromBits < ToBits,    "FromBits must be < ToBits");
    static_assert(ToBits <= 32,         "ToBits must be <= 32");

    constexpr uint32_t from_mask = (1u << FromBits) - 1u;
    value &= from_mask;
    constexpr uint32_t sign_bit = 1u << (FromBits - 1);
    if (value & sign_bit)
        return static_cast<int32_t>(value - (1u << FromBits));
    return static_cast<int32_t>(value);
}

bool isAligned(word addr) { return (addr & 0x3) == 0; }

Instruction decode(word encoding);
void execute(CpuState* cpu, const Instruction& instr);
void handle_syscall(CpuState* cpu);
void handle_exception(CpuState* cpu);
void run(CpuState* cpu);
BasicBlock& build_bb (CpuState* cpu, word entry_pc);

void CpuState::dump(bool full) const
{
    std::cout << "==========<(CPU dump)>===========\n";
    std::cout << "PC = 0x" << std::hex << std::setw(8) << std::setfill('0')
              << pc << std::dec << "  (" << pc << ")\n";

    std::cout << "halted = "    << (halted    ? "true" : "false") << "\n";
    std::cout << "exception = " << (exception ? "true" : "false") << "\n";

    if (exception || cause != ExceptionCause::kNone) 
    {
        std::cout << "cause = " << static_cast<int>(cause)
                  << "  value = 0x" << std::hex << exception_value
                  << std::dec << "\n";
    }

    std::cout << "\nRegisters:\n";
    for (int row = 0; row < 4; ++row) 
    {
        for (int col = 0; col < 8; ++col) 
        {
            int i = row * 8 + col;
            std::cout << "r" << std::setw(2) << std::setfill(' ') << i << " = "
                      << std::hex << std::setw(8) << std::setfill('0')
                      << regs[i] << std::dec << "  ";
        }
        std::cout << "\n";
    }

    if (full && mem) 
    {
        std::cout << "\nMemory around PC (0x"
                  << std::hex << pc << "):\n" << std::dec;

        word start = (pc >= 16) ? pc - 16 : 0;
        for (word addr = start; addr < start + 64; addr += 4) 
        {
            if (addr + 4 > mem->size()) break;

            // Читаем байты напрямую — не трогаем флаги CPU.
            word val = static_cast<word>(mem->data[addr])
                     | (static_cast<word>(mem->data[addr + 1]) << 8)
                     | (static_cast<word>(mem->data[addr + 2]) << 16)
                     | (static_cast<word>(mem->data[addr + 3]) << 24);

            std::cout << "  [0x" << std::hex << std::setw(8) << std::setfill('0')
                      << addr << "] = 0x" << std::setw(8) << val
                      << std::dec << "\n";
        }
    }

    if (full && !bb_cache.empty())
    {
        std::cout << "\nBB cache(size" << bb_cache.size() <<" blocks):\n";
        
        for (const auto& [addr, bb] : bb_cache)
        {
            std::cout << " [0x" 
                      << std::hex <<std::setw(8) << std::setfill('0') 
                      << addr << "]" << std::dec 
                      << "  " << bb.instrs.size() << "instr(s)\n";
            
            for (size_t i = 0; i < bb.instrs.size(); ++i)
            {
                const auto& ins = bb.instrs[i];
                word insn_pc = addr + static_cast<word>(i * 4); 

                std::cout << "      0x" << std::hex << std::setw(8)
                          << std::setfill('0') << insn_pc
                          << std::setfill(' ') << std::dec << "  "
                          << std::setw(8) << std::left << opcode_name(ins.op)
                          << std::right;

            if (ins.rd || ins.rs || ins.rt) {
                    std::cout << " rd=" << (int)ins.rd
                              << " rs=" << (int)ins.rs
                              << " rt=" << (int)ins.rt;
                }
                if (ins.imm) {
                    std::cout << " imm=" << ins.imm;
                }
                if (ins.imm5) {
                    std::cout << " imm5=" << ins.imm5;
                }
                if (ins.base) {
                    std::cout << " base=" << (int)ins.base;
                }
                if (ins.rt1 || ins.rt2) {
                    std::cout << " rt1=" << (int)ins.rt1
                              << " rt2=" << (int)ins.rt2;
                }
                if (ins.rs1 || ins.rs2) {
                    std::cout << " rs1=" << (int)ins.rs1
                              << " rs2=" << (int)ins.rs2;
                }
                if (ins.instr_index) {
                    std::cout << " idx=" << ins.instr_index;
                }
                std::cout << "\n";
            }
        }

    }

    std::cout << "==============================\n";
}

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

void execute (CpuState* cpu, const Instruction& instr)
{
    word old_pc = cpu->pc;
    bool pc_updated = false;

    switch (instr.op)
    {
        case Opcode::kBeq:
        {
            int32_t offset = setx<16, 32>(instr.imm);
            int32_t target = offset * 4;
            if (cpu->getReg(instr.rs) == cpu->getReg(instr.rt))
                cpu->pc = cpu->pc + target;
            else
                cpu->pc += 4;

            pc_updated = true;
            break;
        }
        case Opcode::kSyscall:
        {
            cpu->syscall_pending = true;
            cpu->syscall_num = cpu->getReg(8); //x8 - num of syscall
            break;
        }
        case Opcode::kLd:
        {
            word addr = cpu->getReg(instr.base) + setx<14, 32>(instr.imm);
            if (!isAligned(addr))
            {
                cpu->exception = true;
                cpu->cause = ExceptionCause::kMisalignedAccess;
                cpu->exception_value = addr;
                pc_updated = true; 
                break;
            }
            word value;
            if (!cpu->mem->read32(addr, value)) 
            {
                cpu->exception = true;
                cpu->cause = ExceptionCause::kOutOfBounds;
                cpu->exception_value = addr;
                pc_updated = true;
                break;
            }
            cpu->setReg(instr.rt, value);
            break;
        }
        case Opcode::kSt:
        {
            word addr = cpu->getReg(instr.base) + setx<14, 32>(instr.imm);
            if (!isAligned(addr))
            {
                cpu->exception = true;
                cpu->cause = ExceptionCause::kMisalignedAccess;
                cpu->exception_value = addr;
                pc_updated = true;
                break;
            }
            if(!cpu->mem->write32(addr, cpu->getReg(instr.rt)))
            {
                cpu->exception = true;
                cpu->cause = ExceptionCause::kOutOfBounds;
                cpu->exception_value = addr;
                pc_updated = true;
                break;
            }
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
            cpu->setReg(instr.rt, (cpu->getReg(instr.rs) + setx<16, 32>(instr.imm)));
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
            word addr = cpu->getReg(instr.base) + setx<11, 32>(instr.imm);
            if (!isAligned(addr))
            {
                cpu->exception = true;
                cpu->cause = ExceptionCause::kMisalignedAccess;
                cpu->exception_value = addr;
                pc_updated = true;
                break;
            }
            if (!cpu->mem->write32(addr, cpu->getReg(instr.rt1))) 
            {
                cpu->exception = true;
                cpu->cause = ExceptionCause::kOutOfBounds;
                cpu->exception_value = addr;
                pc_updated = true;
                break;
            }
            if (!cpu->mem->write32(addr + 4, cpu->getReg(instr.rt2)))
            {
                cpu->exception = true;
                cpu->cause = ExceptionCause::kOutOfBounds;
                cpu->exception_value = addr;
                pc_updated = true;
                break;
            }
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
            int32_t value = static_cast<int32_t>(cpu->getReg(instr.rs));
            uint32_t n = instr.imm5;
            if (n == 0 || n >= 32)
            {
                cpu->setReg(instr.rd, value);
                break;
            }
            int32_t max_val = (1 << (n - 1)) - 1;
            int32_t min_val = -(1 << (n - 1));
            if (value > max_val) value = max_val;
            if (value < min_val) value = min_val;
            cpu->setReg(instr.rd, static_cast<word>(value));
            break;
        }
        case Opcode::kSelc:
        {
            int32_t a = static_cast<int32_t>(cpu->getReg(instr.rs1));
            int32_t b = static_cast<int32_t>(cpu->getReg(instr.rs2));
            cpu->setReg(instr.rd, (a > b) ? a : b);
            break;
        }
        case Opcode::kAdd:
        {
            cpu->setReg(instr.rd, static_cast<word>((cpu->getReg(instr.rs)) + (cpu->getReg(instr.rt))));
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
            int32_t imm = setx<16, 32>(instr.imm);
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

void handle_syscall(CpuState* cpu)
{
    word num = cpu->syscall_num;

    switch (num)
    {
        case 1:
        {
            std::cout << static_cast<int32_t>(cpu->getReg(0));
            break;
        }
        case 64: 
        {
            word fd = cpu->getReg(0);
            word buf = cpu->getReg(1);
            word count = cpu->getReg(2);

            if (buf > cpu->mem->size() || count > cpu->mem->size() - buf)
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

BasicBlock& build_bb (CpuState* cpu, word entry_pc)
{
    BasicBlock bb;
    word pc = entry_pc;
    while (true)
    {
        word encoding;
        if (!cpu->mem->read32(pc, encoding)) break;

        Instruction instr = decode (encoding);
        bb.instrs.push_back(instr);
        pc += 4;


        if (instr.op == Opcode::kBeq ||
            instr.op == Opcode::kJ   ||
            instr.op == Opcode::kSyscall) break;

        if (bb.instrs.size() > 256) break;
    }
    auto [it, _] = cpu->bb_cache.emplace(entry_pc, std::move(bb));
    return it->second;
}

void run (CpuState* cpu)
{
    while (!cpu->halted)
    {
        BasicBlock* bb_ptr = nullptr;
        auto it = cpu->bb_cache.find(cpu->pc);
        if (it != cpu->bb_cache.end())
        {
            bb_ptr = &it->second;
        } else {
            bb_ptr = &build_bb(cpu, cpu->pc);
            if (cpu->exception) { handle_exception(cpu); continue;}
        }

        for (const Instruction& instr : bb_ptr->instrs) 
        {
            execute(cpu, instr);
            if (cpu->exception || cpu->halted) break;
        }

        if (cpu->exception) handle_exception (cpu);

        if (cpu->syscall_pending)
        {
            handle_syscall(cpu);
            cpu->syscall_pending = false;
        }
        //cpu->dump();
    }   
}

#ifndef UNIT_TESTS
int main(int argc, char** argv)
{
    Memory mem(1 << 20);           
    CpuState cpu;
    cpu.mem = &mem;
    cpu.pc = 0;

    const char* path = (argc > 1) ? argv[1] : "output.bin";
    mem.load_binary(path, 0, &cpu);
    
    if (cpu.halted) return 1;

    run(&cpu);

    //cpu.dump(true);

    return 0;
}
#endif