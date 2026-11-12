// Optional task/mesh lowering to compute plus vertex pulling. MeshSpirv remains the native path.
using System;
using System.Collections.Generic;
using System.Linq;

static class MeshComputeSpirv
{
    // ABI 1: one storage buffer, 20 header words, task payload records, then expanded vertex records.
    // Header: draw command [0..3], total tasks [4], first task [5], batch size [6], task threads [7],
    // payload words [8], max primitives [9], indices/primitive [10], vertex stride [11], vertices [12], payloads [13].
    public const int Abi = 1;
    sealed class I
    {
        public int Op; public uint[] A;
        public I(int op, params uint[] a) { Op = op; A = a; }
    }
    sealed class Module
    {
        public List<I> Ins = new(), Types = new(), Annotations = new(), Body = new();
        public uint Bound = 1, Generator;
        readonly Dictionary<uint, uint> constants = new();
        public uint Id() => Bound++;
        public uint Type(int op, params uint[] a)
        {
            var existing = Ins.Concat(Types).FirstOrDefault(i => i.Op == op && i.A.Skip(1).SequenceEqual(a));
            if (existing != null) return existing.A[0];
            uint id = Id(); Types.Add(new I(op, new[] { id }.Concat(a).ToArray())); return id;
        }
        public uint C(uint v)
        {
            if (constants.TryGetValue(v, out uint id)) return id;
            uint t = Type(21, 32, 0); id = Id(); constants[v] = id;
            Types.Add(new I(43, t, id, v)); return id;
        }
        public uint Ptr(uint sc, uint type) => Type(32, sc, type);
        public uint Var(uint sc, uint type, int builtin = -1, int location = -1)
        {
            uint ptr = Ptr(sc, type), id = Id(); Types.Add(new I(59, ptr, id, sc));
            if (builtin >= 0) Annotations.Add(new I(71, id, 11, (uint)builtin));
            if (location >= 0) Annotations.Add(new I(71, id, 30, (uint)location));
            return id;
        }
        public uint Emit(int op, uint type, params uint[] a)
        {
            uint id = Id(); Body.Add(new I(op, new[] { type, id }.Concat(a).ToArray())); return id;
        }
        public void Do(int op, params uint[] a) => Body.Add(new I(op, a));
        public void Label(uint id) => Do(248, id);
        public void If(uint condition, Action action)
        {
            uint yes = Id(), end = Id(); Do(247, end, 0); Do(250, condition, yes, end);
            Label(yes); action(); Do(249, end); Label(end);
        }
        public void ReturnIf(uint condition)
        {
            uint yes = Id(), end = Id(); Do(247, end, 0); Do(250, condition, yes, end);
            Label(yes); Do(253); Label(end);
        }
        public byte[] Finish()
        {
            int firstType = Ins.FindIndex(i => i.Op >= 19 && i.Op <= 39 || i.Op == 43 || i.Op == 59);
            int firstFn = Ins.FindIndex(i => i.Op == 54);
            var result = new List<uint> { 0x07230203, 0x00010300, Generator, Bound, 0 };
            void Write(I i) { result.Add((uint)((i.A.Length + 1) << 16 | i.Op)); result.AddRange(i.A); }
            var variables = firstFn < 0 ? new List<I>() : Ins.Take(firstFn).Where(i => i.Op == 59).ToList();
            for (int k = 0; k <= Ins.Count; k++)
            {
                if (k == firstType) foreach (var i in Annotations) Write(i);
                if (k == firstFn || k == Ins.Count && firstFn < 0)
                {
                    foreach (var i in Types) Write(i);
                    foreach (var i in variables) Write(i);
                }
                if (k < Ins.Count && !variables.Contains(Ins[k])) Write(Ins[k]);
            }
            foreach (var i in Body) Write(i);
            var bytes = new byte[result.Count * 4]; Buffer.BlockCopy(result.ToArray(), 0, bytes, 0, bytes.Length); return bytes;
        }
        public static Module Read(byte[] bytes)
        {
            if (bytes.Length < 20 || bytes.Length % 4 != 0 || BitConverter.ToUInt32(bytes, 0) != 0x07230203)
                throw new Exception("mesh compute: invalid SPIR-V");
            uint[] w = new uint[bytes.Length / 4]; Buffer.BlockCopy(bytes, 0, w, 0, bytes.Length);
            var m = new Module { Bound = w[3], Generator = w[2] };
            for (int at = 5; at < w.Length;)
            {
                int n = (int)(w[at] >> 16);
                if (n < 1 || n > w.Length - at) throw new Exception("mesh compute: malformed instruction");
                m.Ins.Add(new I((int)(w[at] & 65535), w.Skip(at + 1).Take(n - 1).ToArray())); at += n;
            }
            return m;
        }
    }
    static uint[] Name(string name)
    {
        byte[] b = System.Text.Encoding.UTF8.GetBytes(name + "\0");
        Array.Resize(ref b, (b.Length + 3) & ~3); uint[] w = new uint[b.Length / 4];
        Buffer.BlockCopy(b, 0, w, 0, b.Length); return w;
    }
    static uint StorageBuffer(Module s, uint binding, uint u)
    {
        uint array = s.Type(29, u), block = s.Type(30, array);
        void Decorate(I annotation)
        {
            if (!s.Ins.Concat(s.Annotations).Any(i => i.Op == annotation.Op && i.A.SequenceEqual(annotation.A)))
                s.Annotations.Add(annotation);
        }
        Decorate(new I(71, array, 6, 4));
        Decorate(new I(71, block, 3)); // BufferBlock, SPIR-V 1.3 Uniform storage
        Decorate(new I(72, block, 0, 35, 0));
        uint buffer = s.Var(2, block);
        s.Annotations.Add(new I(71, buffer, 34, 0)); s.Annotations.Add(new I(71, buffer, 33, binding));
        return buffer;
    }
    public static int Stride(AmpereLowering.MeshInfo m) => 8 + 4 * (MeshSpirv.GenericLocations(m).DefaultIfEmpty(-1).Max() + 1);

    public static byte[] Convert(byte[] source, AmpereLowering.MeshInfo mesh, uint binding)
    {
        if (mesh.Threads < 1 || mesh.Threads > 1024 || mesh.PayloadBytes % 4 != 0)
            throw new Exception("mesh compute: invalid workgroup/payload layout");
        if (mesh.Stage == 7 && (mesh.MaxVertices < 1 || mesh.MaxVertices > 256 || mesh.MaxPrimitives < 1 || mesh.MaxPrimitives > 256))
            throw new Exception("mesh compute: missing geometry limits");
        Module s = Module.Read(source);
        uint u = s.Type(21, 32, 0), b = s.Type(20), vec3 = s.Type(23, u, 3), v = s.Type(19), fn = s.Type(33, v);
        var shared = s.Ins.Where(i => i.Op == 59 && i.A[2] == 4).ToList();
        if (shared.Count != 1) throw new Exception("mesh compute: expected one stage-memory array");
        uint stageMemory = shared[0].A[1], sharedPtr = s.Ptr(4, u);
        uint buffer = StorageBuffer(s, binding, u), bufferPtr = s.Ptr(2, u);
        uint li = s.Var(1, u, 29), group = s.Var(1, vec3, 26);
        var entry = s.Ins.Single(i => i.Op == 15);
        if (entry.A[0] != 5) throw new Exception("mesh compute: expected lowered compute entry");
        uint original = entry.A[1], wrapper = s.Id();
        // Original workgroup/global IDs refer to the guest task or child mesh group, not the tiled host dispatch.
        var replacements = new List<(uint variable, uint builtin)>();
        foreach (var dec in s.Ins.Where(i => i.Op == 71 && i.A.Length == 3 && i.A[1] == 11 &&
                                            (i.A[2] == 24 || i.A[2] == 26 || i.A[2] == 28)).ToList())
        {
            var variable = s.Ins.Single(i => i.Op == 59 && i.A[1] == dec.A[0]);
            variable.A[0] = s.Ptr(6, vec3); variable.A[2] = 6;
            replacements.Add((dec.A[0], dec.A[2])); s.Ins.Remove(dec);
            // Access chains into the former input variable must use Private pointers too.
            var pointers = new HashSet<uint> { dec.A[0] };
            foreach (var i in s.Ins)
                if ((i.Op == 65 || i.Op == 66) && pointers.Contains(i.A[2]))
                {
                    var type = s.Ins.Concat(s.Types).Single(t => t.Op == 32 && t.A[0] == i.A[0]);
                    i.A[0] = s.Ptr(6, type.A[2]); pointers.Add(i.A[1]);
                }
        }
        // Keep the compute execution modes, but make the wrapper the only entry point.
        foreach (var i in s.Ins.Where(i => i.Op == 16 && i.A[0] == original)) i.A[0] = wrapper;
        entry.A[1] = wrapper;
        var oldInterface = entry.A.Skip(2 + Name("main").Length).Where(id => !replacements.Any(r => r.variable == id));
        entry.A = new uint[] { 5, wrapper }.Concat(Name("main")).Concat(oldInterface).Concat(new[] { li, group }).DistinctEntry().ToArray();
        uint outputBase = s.Var(4, u);
        s.Do(54, v, wrapper, 0, fn); s.Label(s.Id());
        uint Load(uint ptr) => s.Emit(61, u, ptr);
        uint Add(uint x, uint y) => s.Emit(128, u, x, y);
        uint Mul(uint x, uint y) => s.Emit(132, u, x, y);
        uint Read(uint index) => Load(s.Emit(65, bufferPtr, buffer, s.C(0), index));
        void Write(uint index, uint value) => s.Do(62, s.Emit(65, bufferPtr, buffer, s.C(0), index), value);
        uint Shared(uint index) => Load(s.Emit(65, sharedPtr, stageMemory, index));
        void PutShared(uint index, uint value) => s.Do(62, s.Emit(65, sharedPtr, stageMemory, index), value);
        uint Less(uint x, uint y) => s.Emit(176, b, x, y);
        uint Minimum(uint x, uint y) => s.Emit(169, u, Less(x, y), x, y);
        void Barrier() => s.Do(224, s.C(2), s.C(2), s.C(0x108));
        uint tid = Load(li), xyz = s.Emit(61, vec3, group), gx = s.Emit(81, u, xyz, 0);
        uint taskThreads = Read(s.C(7));
        uint parent = mesh.Stage == 7 && mesh.HasTask ? s.Emit(134, u, gx, taskThreads) : gx;
        uint child = mesh.Stage == 7 && mesh.HasTask ? s.Emit(137, u, gx, taskThreads) : gx;
        uint taskOffset = Add(Read(s.C(5)), parent);
        uint globalTask = Add(Read(s.C(14)), taskOffset);
        uint oversized = Less(Read(s.C(15)), Read(s.C(4)));
        s.If(oversized, () => s.If(Less(gx, s.C(1)), () => s.If(Less(tid, s.C(1)), () => Write(s.C(16), Read(s.C(4))))));
        s.ReturnIf(oversized);
        // OR of the out-of-range conditions; uniform for every invocation of this workgroup.
        uint inactive = s.Emit(166, b, s.Emit(168, b, Less(parent, Read(s.C(6)))), s.Emit(168, b, Less(taskOffset, Read(s.C(4)))));
        s.ReturnIf(inactive);
        uint payloadWords = Read(s.C(8));
        uint payloadAt = Add(Read(s.C(13)), Mul(parent, Add(payloadWords, s.C(1))));
        if (mesh.Stage == 7 && mesh.HasTask)
            s.ReturnIf(s.Emit(168, b, Less(child, Read(payloadAt))));
        uint guestGroup = mesh.Stage == 7 && mesh.HasTask ? child : globalTask;
        foreach (var r in replacements)
        {
            uint x = r.builtin == 24 ? (mesh.Stage == 7 && mesh.HasTask ? Read(payloadAt) : Read(s.C(4))) :
                     r.builtin == 28 ? Add(Mul(guestGroup, s.C((uint)mesh.Threads)), tid) : guestGroup;
            uint y = r.builtin == 24 ? s.C(1) : s.C(0);
            s.Do(62, r.variable, s.Emit(80, vec3, x, y, y));
        }
        s.If(Less(tid, s.C(1)), () => PutShared(s.C((uint)(mesh.Stage == 6 ? mesh.Pay + 4 : mesh.Map) / 4), s.C(0)));
        if (mesh.Stage == 7 && mesh.HasTask)
            for (int k = 0; k < mesh.PayloadBytes / 4; k += mesh.Threads)
            {
                uint index = Add(tid, s.C((uint)k));
                s.If(Less(index, s.C((uint)mesh.PayloadBytes / 4)), () =>
                    PutShared(Add(s.C((uint)mesh.Pay / 4), index), Read(Add(Add(payloadAt, s.C(1)), index))));
            }
        Barrier(); s.Emit(57, v, original); Barrier();
        if (mesh.Stage == 6)
        {
            s.If(Less(tid, s.C(1)), () => Write(payloadAt, Minimum(Shared(s.C((uint)(mesh.Pay + 4) / 4)), s.C((uint)mesh.Threads))));
            for (int k = 0; k < mesh.PayloadBytes / 4; k += mesh.Threads)
            {
                uint index = Add(tid, s.C((uint)k));
                s.If(Less(index, s.C((uint)mesh.PayloadBytes / 4)), () =>
                    Write(Add(Add(payloadAt, s.C(1)), index), Shared(Add(s.C((uint)mesh.Pay / 4), index))));
            }
        }
        else
        {
            uint count = Minimum(Shared(s.C((uint)mesh.Map / 4)), s.C((uint)mesh.MaxPrimitives));
            s.If(Less(tid, s.C(1)), () =>
            {
                uint counter = s.Emit(65, bufferPtr, buffer, s.C(0), s.C(0));
                uint first = s.Emit(234, u, counter, s.C(1), s.C(0), Mul(count, s.C((uint)mesh.IndicesPerPrimitive)));
                s.Do(62, outputBase, first);
            });
            Barrier(); uint firstVertex = Load(outputBase);
            for (int k = 0; k < mesh.MaxPrimitives; k += mesh.Threads)
            {
                uint primitive = Add(tid, s.C((uint)k));
                s.If(Less(primitive, count), () =>
                {
                    for (int j = 0; j < mesh.IndicesPerPrimitive; j++)
                    {
                        uint byteAt = Add(s.C((uint)mesh.Map + 4 + (uint)j), Mul(primitive, s.C((uint)mesh.IndicesPerPrimitive)));
                        uint packed = Shared(s.Emit(194, u, byteAt, s.C(2)));
                        uint shift = Mul(s.Emit(199, u, byteAt, s.C(3)), s.C(8));
                        uint index = Minimum(s.Emit(199, u, s.Emit(194, u, packed, shift), s.C(255)), s.C((uint)mesh.MaxVertices - 1));
                        uint vertex = Add(firstVertex, Add(Mul(primitive, s.C((uint)mesh.IndicesPerPrimitive)), s.C((uint)j)));
                        uint at = Add(Read(s.C(12)), Mul(vertex, s.C((uint)Stride(mesh))));
                        for (int word = 0; word < Stride(mesh); word++) Write(Add(at, s.C((uint)word)), s.C(word == 3 || word == 4 ? 0x3f800000u : 0));
                        uint lane = s.Emit(199, u, index, s.C(31)), warp = s.Emit(194, u, index, s.C(5));
                        uint sourceAt = Add(s.C((uint)mesh.Attr / 4), Add(Mul(warp, s.C((uint)mesh.Lines.Count * 32)), lane));
                        for (int line = 0; line < mesh.Lines.Count; line++)
                        {
                            int attribute = mesh.Lines[line];
                            int dest = attribute >= 0x70 && attribute < 0x80 ? (attribute - 0x70) / 4 :
                                       attribute == 0x6c ? 4 : attribute >= 0x80 ? 8 + (attribute - 0x80) / 4 : -1;
                            if (dest >= 0) Write(Add(at, s.C((uint)dest)), Shared(Add(sourceAt, s.C((uint)line * 32))));
                        }
                    }
                });
            }
        }
        s.Do(253); s.Do(56);
        return s.Finish();
    }

    // Entry operands can repeat a builtin already present in the lowered compute interface.
    static IEnumerable<uint> DistinctEntry(this IEnumerable<uint> words)
    {
        var all = words.ToArray(); int nameEnd = 2;
        do { uint word = all[nameEnd++]; if ((word & 0xff000000) == 0 || (word & 0xff0000) == 0 || (word & 0xff00) == 0 || (word & 0xff) == 0) break; } while (nameEnd < all.Length);
        return all.Take(nameEnd).Concat(all.Skip(nameEnd).Distinct());
    }

    public static byte[] Vertex(AmpereLowering.MeshInfo mesh, uint binding)
    {
        var s = new Module(); uint u = s.Type(21, 32, 0), si = s.Type(21, 32, 1), f = s.Type(22, 32);
        uint v = s.Type(19), fn = s.Type(33, v), v4 = s.Type(23, f, 4);
        uint buffer = StorageBuffer(s, binding, u), element = s.Ptr(2, u);
        uint index = s.Var(1, si, 42), position = s.Var(3, v4, 0), size = s.Var(3, f, 1);
        var outputs = MeshSpirv.GenericLocations(mesh).ToDictionary(g => g, g => s.Var(3, v4, -1, g));
        uint main = s.Id();
        s.Ins.Add(new I(17, 1)); s.Ins.Add(new I(14, 0, 1));
        s.Ins.Add(new I(15, new uint[] { 0, main }.Concat(Name("main")).Concat(new[] { index, position, size }).Concat(outputs.Values).ToArray()));
        // Give Finish a type insertion point before its generated declarations.
        s.Ins.Add(new I(19, v)); s.Types.RemoveAll(i => i.Op == 19 && i.A[0] == v);
        s.Do(54, v, main, 0, fn); s.Label(s.Id());
        uint Read(uint at) => s.Emit(61, u, s.Emit(65, element, buffer, s.C(0), at));
        uint vertex = s.Emit(124, u, s.Emit(61, si, index));
        uint at = s.Emit(128, u, Read(s.C(12)), s.Emit(132, u, vertex, s.C((uint)Stride(mesh))));
        uint FloatAt(uint offset) => s.Emit(124, f, Read(s.Emit(128, u, at, s.C(offset))));
        uint VectorAt(uint offset) => s.Emit(80, v4, FloatAt(offset), FloatAt(offset + 1), FloatAt(offset + 2), FloatAt(offset + 3));
        s.Do(62, position, VectorAt(0)); s.Do(62, size, FloatAt(4));
        foreach (var (g, variable) in outputs) s.Do(62, variable, VectorAt((uint)(8 + 4 * g)));
        s.Do(253); s.Do(56); return s.Finish();
    }
}
