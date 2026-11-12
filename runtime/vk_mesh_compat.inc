// Additional mesh/task path. Included after the regular binding helpers; native draw functions remain unchanged.
enum { MC_INDIRECT_LIMIT = 4096, MC_HEADER_WORDS = 20, MC_TARGET_BYTES = 32 * 1024 * 1024 };
typedef struct {
    VkBuffer buffer;
    VmaAllocation allocation;
    uint32_t *mapped;
    uint64_t bytes, fence;
} MeshCompatBuffer;
static MeshCompatBuffer mc_buffer;
static struct {
    int active, indirect;
    uint32_t count, first, base, batch, capacity;
    VkMemSpan arguments;
} mc_draw;

static void mesh_compat_release(void *argument) {
    MeshCompatBuffer *b = argument;
    static volatile int32_t overflow_logs;
    if (b->mapped[16] && plat_inc32(&overflow_logs) <= 8)
        rt_log("vk compat: indirect mesh draw requested %u task groups; limit %u, entire draw omitted",
                              b->mapped[16], MC_INDIRECT_LIMIT);
    vk_diag_note("mesh-completed", b->fence, VK_NULL_HANDLE, (uintptr_t)b->buffer, b->bytes, b->mapped[4], b->mapped[5]);
    vk_diag_mesh_memory(-(int64_t)b->bytes);
    vmaDestroyBuffer(vk_ctx()->vma, b->buffer, b->allocation);
    free(b);
}

static int mesh_compat_buffer(VkExec *ex, uint64_t bytes) {
    if (mc_buffer.buffer && mc_buffer.fence == ex->fence && bytes <= mc_buffer.bytes) {
        ex->compat_mesh_buffer = (VkDescriptorBufferInfo){mc_buffer.buffer, 0, bytes};
        return 1;
    }
    MeshCompatBuffer *b = calloc(1, sizeof *b);
    if (!b) return 0;
    VkBufferCreateInfo bi = {.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, .size = bytes,
        .usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT |
                 VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT};
    VmaAllocationCreateInfo ai = {.usage = VMA_MEMORY_USAGE_AUTO_PREFER_HOST,
        .flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT,
        .requiredFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT};
    VmaAllocationInfo info;
    if (vmaCreateBuffer(vk_ctx()->vma, &bi, &ai, &b->buffer, &b->allocation, &info) != VK_SUCCESS) { free(b); return 0; }
    b->mapped = info.pMappedData, b->bytes = bytes, b->fence = ex->fence;
    b->mapped[16] = 0; // GPU writes overflow status; completion reads it only after this fence.
    mc_buffer = *b;
    vk_diag_mesh_memory((int64_t)bytes);
    vk_diag_note("mesh-allocated", ex->fence, ex->cb, (uintptr_t)b->buffer, bytes, 0, 0);
    vk_completion_add(ex->fence, mesh_compat_release, b);
    ex->compat_mesh_buffer = (VkDescriptorBufferInfo){b->buffer, 0, bytes};
    return 1;
}

static int mesh_compat_dispatch(VkExec *ex, const GfxShader *cs, int stage, uint32_t groups) {
    const VkPipeInfo *info = NULL;
    VkPipeline pipeline = vk_pipeline_compute(cs, &info);
    if (!pipeline || !info || info->n_bindings > MAX_BINDINGS) return 0;
    // The compute pipeline declares compute visibility, but its resources still come from the original task/mesh banks.
    VkPipeBinding bindings[MAX_BINDINGS];
    memcpy(bindings, info->bindings, info->n_bindings * sizeof *bindings);
    for (uint32_t i = 0; i < info->n_bindings; i++) bindings[i].stage = (uint8_t)stage;
    VkPipeInfo local = *info;
    local.bindings = bindings;
    bind_pipeline(ex, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
    twin_dispatch = 1;
    bind_resources(ex, &local);
    twin_dispatch = 0;
    vk_diag_note("mesh-dispatch", ex->fence, ex->cb, vk_diag_shader(cs), groups, stage, 0);
    VkPassMark *mark = dispatch_mark(ex, cs, groups, 1, 1);
    vkCmdDispatch(ex->cb, groups, 1, 1);
    vk_pass_mark_end(ex->cb, mark);
    ex->recorded++;
    return 1;
}

static int mesh_compat_generate(VkExec *ex, const GfxShader *mesh, const GfxShader *task,
                                const GfxShader **vertex, int *primitive) {
    const GfxMeshCompute *m = gfx_shader_mesh_compute(mesh), *t = task ? gfx_shader_mesh_compute(task) : NULL;
    if (!mc_draw.active || !m || (task && !t) || (m->task != (task != NULL)) || !m->maxp ||
        m->maxp > 256 || m->ipp < 1 || m->ipp > 3 || m->stride < 8 || m->stride > 136 ||
        !m->threads || m->threads > 1024 || m->payload > 65536 ||
        (t && (!t->threads || t->threads > 1024 || t->payload != m->payload))) {
        warn_once(W_MESH, "mesh compute compatibility metadata is missing, inconsistent or outside its limits");
        return 0;
    }
    uint32_t children = t ? t->threads : 1;
    uint64_t per_task = (uint64_t)children * m->maxp * m->ipp * m->stride * 4 + (m->payload + 1ull) * 4;
    uint64_t budget = vk_ctx()->props.limits.maxStorageBufferRange;
    if (budget > MC_TARGET_BYTES) budget = MC_TARGET_BYTES;
    uint32_t capacity = budget > 128 && per_task ? (uint32_t)((budget - 128) / per_task) : 0;
    static int larger = -1;
    if (larger < 0) larger = !strcmp(cfg_value("NX2NAVI_COMPAT_MESH_BATCH"), "auto");
    if (!larger && capacity > 8) capacity = 8;
    uint32_t groups_limit = vk_ctx()->props.limits.maxComputeWorkGroupCount[0];
    if (capacity > groups_limit / children) capacity = groups_limit / children;
    if (!capacity) return warn_once(W_MESH, "one compatibility task group exceeds the geometry buffer budget"), 0;
    mc_draw.capacity = capacity;
    uint32_t remaining = mc_draw.count - mc_draw.base;
    uint32_t batch = remaining < capacity ? remaining : capacity;
    mc_draw.batch = batch;
    uint32_t payload_at = MC_HEADER_WORDS;
    uint32_t vertices_at = (payload_at + capacity * (m->payload + 1) + 3) & ~3u;
    uint64_t bytes = ((uint64_t)vertices_at + (uint64_t)capacity * children * m->maxp * m->ipp * m->stride) * 4;
    if (!mesh_compat_buffer(ex, bytes)) return warn_once(W_MESH, "compatibility geometry buffer allocation failed"), 0;
    vk_diag_note("mesh-batch", ex->fence, ex->cb, ex->prog ? ex->prog->serial : 0, mc_draw.base, batch, capacity);
    vk_contact_flush(ex);
    vk_exec_barrier(ex); // previous draws have finished reading this buffer before its next batch rewrites it
    uint32_t header[16] = {0, 1, 0, 0, mc_draw.count, mc_draw.base, batch, children, m->payload,
        m->maxp, m->ipp, m->stride, vertices_at, payload_at, mc_draw.first,
        mc_draw.indirect ? MC_INDIRECT_LIMIT : mc_draw.count};
    vkCmdUpdateBuffer(ex->cb, mc_buffer.buffer, 0, sizeof header, header);
    vk_diag_note("mesh-fill", ex->fence, ex->cb, (uintptr_t)mc_buffer.buffer, 4ull * payload_at,
                 4ull * capacity * (m->payload + 1), mc_buffer.bytes);
    vkCmdFillBuffer(ex->cb, mc_buffer.buffer, 4ull * payload_at, 4ull * capacity * (m->payload + 1), 0);
    // The barrier below skips redundant barriers using recorded. Count these transfer writes or it can mistake
    // the earlier barrier for a current one, leaving stale counts and payloads visible to the compute shaders.
    ex->recorded += 2;
    if (mc_draw.indirect) {
        // Keep the actual count and first group on the GPU; no readback or CPU wait.
        VkBufferCopy copies[2] = {{mc_draw.arguments.offset, 16, 4}, {mc_draw.arguments.offset + 4, 56, 4}};
        vkCmdCopyBuffer(ex->cb, mc_draw.arguments.buffer, mc_buffer.buffer, 2, copies);
        ex->recorded++;
        vk_diag_note("mesh-arguments", ex->fence, ex->cb, (uintptr_t)mc_draw.arguments.buffer,
                     mc_draw.arguments.offset, (uintptr_t)mc_buffer.buffer, 0);
    }
    vk_exec_barrier(ex);
    if (t) {
        if (!mesh_compat_dispatch(ex, t->compute, GFX_STAGE_TS, batch)) return 0;
        vk_exec_barrier(ex); // task payloads are visible to mesh compute
    }
    if (!mesh_compat_dispatch(ex, m->compute, GFX_STAGE_MS, batch * children)) return 0;
    vk_exec_barrier(ex); // generated vertices and draw count are visible to vertex/indirect reads
    B.res_valid = 0;
    *vertex = m->vertex;
    *primitive = m->ipp == 1 ? NVN_DRAW_PRIMITIVE_POINTS : m->ipp == 2 ? NVN_DRAW_PRIMITIVE_LINES : NVN_DRAW_PRIMITIVE_TRIANGLES;
    return 1;
}
