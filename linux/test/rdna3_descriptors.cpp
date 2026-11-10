// CPU regression: descriptor replacement must preserve gfx11's twin and raw alias views.
#include "nrvk.hpp"
#include <cassert>
#include <map>
#include <set>

static std::map<uint32_t, VkBuffer> writes;
static std::set<uint32_t> copies;
extern "C" {
VKAPI_ATTR VkResult VKAPI_CALL vkCreateDescriptorPool(VkDevice, const VkDescriptorPoolCreateInfo*,
    const VkAllocationCallbacks*, VkDescriptorPool* p) { *p = (VkDescriptorPool)uintptr_t(99); return VK_SUCCESS; }
VKAPI_ATTR VkResult VKAPI_CALL vkAllocateDescriptorSets(VkDevice, const VkDescriptorSetAllocateInfo*, VkDescriptorSet* s) {
    *s = (VkDescriptorSet)uintptr_t(100); return VK_SUCCESS;
}
VKAPI_ATTR void VKAPI_CALL vkUpdateDescriptorSets(VkDevice, uint32_t nw, const VkWriteDescriptorSet* w,
    uint32_t nc, const VkCopyDescriptorSet* c) {
    for (uint32_t i = 0; i < nc; ++i) copies.insert(c[i].dstBinding);
    for (uint32_t i = 0; i < nw; ++i) writes[w[i].dstBinding] = w[i].pBufferInfo->buffer;
}
}
int main() {
    auto buf = [](uintptr_t v) { return (VkBuffer)v; };
    std::map<VkBuffer, VkBuffer> twins{{buf(1),buf(11)}, {buf(2),buf(12)}, {buf(3),buf(13)}};
    nrvk::Kernel k;
    k.pool = (VkDescriptorPool)uintptr_t(1);
    k.buffers = {buf(1),buf(1),buf(2)};
    k.buffer_bindings = 3; k.twin_map = &twins; k.twin_bindings = {0,2}; k.alias_bindings = {16,18};
    k.binding_ids = {0,1,2,3,16,18};
    k.pool_sizes = {{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,5}, {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,1}};
    std::vector<VkDescriptorPool> retired;
    assert(k.retarget_buffer(buf(1),buf(3),retired));
    assert(writes == (std::map<uint32_t,VkBuffer>{{0,buf(13)},{1,buf(3)},{16,buf(3)}}));
    assert(copies == (std::set<uint32_t>{0,1,2,3,16,18}));
    assert(retired.size() == 1);
    writes.clear(); copies.clear();
    assert(k.retarget_buffer(buf(3),buf(1),retired));
    assert(writes.at(0) == buf(11) && writes.at(1) == buf(1) && writes.at(16) == buf(1));
    assert(!k.retarget_buffer(buf(7),buf(8),retired));
    writes.clear();
    assert(k.retarget_buffer(buf(2),buf(4),retired)); // dedicated raw buffer without a twin
    assert(writes.at(2) == buf(4) && writes.at(18) == buf(4));
    puts("RDNA3 descriptor retarget: PASS (twins, raw views, aliases, repeated replacement)");
}
