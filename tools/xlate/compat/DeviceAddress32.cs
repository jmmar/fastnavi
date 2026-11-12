// Optional paired-word address generation. The native 64-bit address generator remains the default.
using static Spv.Specification;
using SpvInstruction = Spv.Generator.Instruction;

namespace Ryujinx.Graphics.Shader.CodeGen.Spirv
{
    static class DeviceAddress32
    {
        // Same table and twin-write contract as Instructions.GetDeviceAddressPointers, with modulo-2^64 addition
        // carried explicitly between two uint words. Physical pointers are formed by OpBitcast from uvec2.
        public static SpvInstruction GetPointers(CodeGenContext c, int binding, SpvInstruction low,
                                                SpvInstruction high, out SpvInstruction twinPointer)
        {
            SpvInstruction u32 = c.TypeU32(), boolean = c.TypeBool();
            SpvInstruction pair = c.TypeVector(u32, 2);
            SpvInstruction pointer = c.TypePointer(StorageClass.PhysicalStorageBuffer, u32);
            SpvInstruction element = c.TypePointer(StorageClass.Uniform, u32);
            SpvInstruction zero = c.Constant(c.TypeS32(), 0);
            SpvInstruction U(uint n) => c.Constant(u32, n);
            SpvInstruction Word(SpvInstruction n) => c.Load(u32, c.AccessChain(element, c.StorageBuffers[binding], zero, n));
            (SpvInstruction, SpvInstruction) Add(SpvInstruction al, SpvInstruction ah, SpvInstruction bl, SpvInstruction bh)
            {
                SpvInstruction lo = c.IAdd(u32, al, bl);
                SpvInstruction carry = c.Select(u32, c.ULessThan(boolean, lo, al), U(1), U(0));
                return (lo, c.IAdd(u32, c.IAdd(u32, ah, bh), carry));
            }
            SpvInstruction ChoosePointer(SpvInstruction condition, (SpvInstruction lo, SpvInstruction hi) yes,
                                        (SpvInstruction lo, SpvInstruction hi) no)
            {
                // Select the words independently: a scalar condition on a vector needs newer SPIR-V semantics.
                SpvInstruction lo = c.Select(u32, condition, yes.lo, no.lo);
                SpvInstruction hi = c.Select(u32, condition, yes.hi, no.hi);
                return c.Bitcast(pointer, c.CompositeConstruct(pair, lo, hi));
            }

            SpvInstruction inRange = c.ULessThan(boolean, high, U(256));
            SpvInstruction l1 = c.BitwiseOr(u32, c.ShiftLeftLogical(u32, high, U(8)), c.ShiftRightLogical(u32, low, U(24)));
            l1 = c.Select(u32, inRange, l1, U(0));
            SpvInstruction block = Word(c.IAdd(u32, l1, U(16)));
            SpvInstruction page = c.BitwiseAnd(u32, c.ShiftRightLogical(u32, low, U(12)), U(0xfff));
            SpvInstruction record = Word(c.IAdd(u32, c.IAdd(u32, c.IMul(u32, block, U(4096)), U(65552)), page));
            SpvInstruction mapped = c.INotEqual(boolean, record, U(0));
            SpvInstruction at = c.IAdd(u32, Word(U(2)), c.IMul(u32, record, U(4)));
            SpvInstruction dl = Word(at), dh = Word(c.IAdd(u32, at, U(1)));
            SpvInstruction tl = Word(c.IAdd(u32, at, U(2))), th = Word(c.IAdd(u32, at, U(3)));
            SpvInstruction aligned = c.BitwiseAnd(u32, low, U(0xfffffffc));
            SpvInstruction offset = c.BitwiseAnd(u32, low, U(0xffc));
            var device = Add(aligned, high, dl, dh);
            var dummy = Add(Word(U(0)), Word(U(1)), offset, U(0));
            var twin = Add(aligned, high, tl, th);
            var scratch = Add(Word(U(3)), Word(U(4)), offset, U(0));
            SpvInstruction sameTwin = c.LogicalAnd(boolean, c.IEqual(boolean, tl, dl), c.IEqual(boolean, th, dh));
            SpvInstruction hasTwin = c.LogicalAnd(boolean, mapped, c.LogicalNot(boolean, sameTwin));
            twinPointer = ChoosePointer(hasTwin, twin, scratch);
            return ChoosePointer(mapped, device, dummy);
        }
    }
}
