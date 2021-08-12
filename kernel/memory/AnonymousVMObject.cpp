/*
 * Copyright (c) 2021, Krisna Pranav
 *
 * SPDX-License-Identifier: BSD-2-Clause
*/

// includes
#include <kernel/arch/x86/SmapDisabler.h>
#include <kernel/Debug.h>
#include <kernel/memory/AnonymousVMObject.h>
#include <kernel/memory/MemoryManager.h>
#include <kernel/memory/PhysicalPage.h>
#include <kernel/Process.h>

namespace Kernel::Memory {

RefPtr<VMObject> AnonymousVMObject::try_clone()
{
    ScopedSpinLock lock(m_lock);

    if (is_purgeable() && is_volatile()) {

        auto clone = try_create_purgeable_with_size(size(), AllocationStrategy::None);
        if (!clone)
            return {};
        clone->m_volatile = true;
        return clone;
    }

    size_t new_cow_pages_needed = page_count();

    dbgln_if(COMMIT_DEBUG, "Cloning {:p}, need {} committed cow pages", this, new_cow_pages_needed);

    auto committed_pages = MM.commit_user_physical_pages(new_cow_pages_needed);
    if (!committed_pages.has_value())
        return {};

    auto new_shared_committed_cow_pages = try_create<SharedCommittedCowPages>(committed_pages.release_value());

    if (!new_shared_committed_cow_pages)
        return {};

    auto clone = adopt_ref_if_nonnull(new (nothrow) AnonymousVMObject(*this, *new_shared_committed_cow_pages));
    if (!clone)
        return {};

    m_shared_committed_cow_pages = move(new_shared_committed_cow_pages);

    ensure_or_reset_cow_map();

    if (m_unused_committed_pages.has_value() && !m_unused_committed_pages->is_empty()) {

        for (size_t i = 0; i < page_count(); i++) {
            auto& page = clone->m_physical_pages[i];
            if (page && page->is_lazy_committed_page()) {
                page = MM.shared_zero_page();
            }
        }
    }

    return clone;
}

RefPtr<AnonymousVMObject> AnonymousVMObject::try_create_with_size(size_t size, AllocationStrategy strategy)
{
    Optional<CommittedPhysicalPageSet> committed_pages;
    if (strategy == AllocationStrategy::Reserve || strategy == AllocationStrategy::AllocateNow) {
        committed_pages = MM.commit_user_physical_pages(ceil_div(size, static_cast<size_t>(PAGE_SIZE)));
        if (!committed_pages.has_value())
            return {};
    }
    return adopt_ref_if_nonnull(new (nothrow) AnonymousVMObject(size, strategy, move(committed_pages)));
}

RefPtr<AnonymousVMObject> AnonymousVMObject::try_create_physically_contiguous_with_size(size_t size)
{
    auto contiguous_physical_pages = MM.allocate_contiguous_supervisor_physical_pages(size);
    if (contiguous_physical_pages.is_empty())
        return {};
    return adopt_ref_if_nonnull(new (nothrow) AnonymousVMObject(contiguous_physical_pages.span()));
}

RefPtr<AnonymousVMObject> AnonymousVMObject::try_create_purgeable_with_size(size_t size, AllocationStrategy strategy)
{
    Optional<CommittedPhysicalPageSet> committed_pages;
    if (strategy == AllocationStrategy::Reserve || strategy == AllocationStrategy::AllocateNow) {
        committed_pages = MM.commit_user_physical_pages(ceil_div(size, static_cast<size_t>(PAGE_SIZE)));
        if (!committed_pages.has_value())
            return {};
    }
    auto vmobject = adopt_ref_if_nonnull(new (nothrow) AnonymousVMObject(size, strategy, move(committed_pages)));
    if (!vmobject)
        return {};
    vmobject->m_purgeable = true;
    return vmobject;
}

RefPtr<AnonymousVMObject> AnonymousVMObject::try_create_with_physical_pages(Span<NonnullRefPtr<PhysicalPage>> physical_pages)
{
    return adopt_ref_if_nonnull(new (nothrow) AnonymousVMObject(physical_pages));
}

RefPtr<AnonymousVMObject> AnonymousVMObject::try_create_for_physical_range(PhysicalAddress paddr, size_t size)
{
    if (paddr.offset(size) < paddr) {
        dbgln("Shenanigans! try_create_for_physical_range({}, {}) would wrap around", paddr, size);
        return nullptr;
    }
    return adopt_ref_if_nonnull(new (nothrow) AnonymousVMObject(paddr, size));
}

AnonymousVMObject::AnonymousVMObject(size_t size, AllocationStrategy strategy, Optional<CommittedPhysicalPageSet> committed_pages)
    : VMObject(size)
    , m_unused_committed_pages(move(committed_pages))
{
    if (strategy == AllocationStrategy::AllocateNow) {

        for (size_t i = 0; i < page_count(); ++i)
            physical_pages()[i] = m_unused_committed_pages->take_one();
    } else {
        auto& initial_page = (strategy == AllocationStrategy::Reserve) ? MM.lazy_committed_page() : MM.shared_zero_page();
        for (size_t i = 0; i < page_count(); ++i)
            physical_pages()[i] = initial_page;
    }
}

AnonymousVMObject::AnonymousVMObject(PhysicalAddress paddr, size_t size)
    : VMObject(size)
{
    VERIFY(paddr.page_base() == paddr);
    for (size_t i = 0; i < page_count(); ++i)
        physical_pages()[i] = PhysicalPage::create(paddr.offset(i * PAGE_SIZE), MayReturnToFreeList::No);
}

AnonymousVMObject::AnonymousVMObject(Span<NonnullRefPtr<PhysicalPage>> physical_pages)
    : VMObject(physical_pages.size() * PAGE_SIZE)
{
    for (size_t i = 0; i < physical_pages.size(); ++i) {
        m_physical_pages[i] = physical_pages[i];
    }
}

AnonymousVMObject::AnonymousVMObject(AnonymousVMObject const& other, NonnullRefPtr<SharedCommittedCowPages> shared_committed_cow_pages)
    : VMObject(other)
    , m_shared_committed_cow_pages(move(shared_committed_cow_pages))
    , m_purgeable(other.m_purgeable)
{
    ensure_cow_map();
}

AnonymousVMObject::~AnonymousVMObject()
{
}

size_t AnonymousVMObject::purge()
{
    ScopedSpinLock lock(m_lock);

    if (!is_purgeable() || !is_volatile())
        return 0;

    size_t total_pages_purged = 0;

    for (auto& page : m_physical_pages) {
        VERIFY(page);
        if (page->is_shared_zero_page())
            continue;
        page = MM.shared_zero_page();
        ++total_pages_purged;
    }

    m_was_purged = true;

    for_each_region([](Region& region) {
        region.remap();
    });

    return total_pages_purged;
}

KResult AnonymousVMObject::set_volatile(bool is_volatile, bool& was_purged)
{
    VERIFY(is_purgeable());

    ScopedSpinLock locker(m_lock);

    was_purged = m_was_purged;
    if (m_volatile == is_volatile)
        return KSuccess;

    if (is_volatile) {

        for (auto& page : m_physical_pages) {
            if (page && page->is_lazy_committed_page())
                page = MM.shared_zero_page();
        }

        m_unused_committed_pages = {};
        m_shared_committed_cow_pages = nullptr;

        if (!m_cow_map.is_null())
            m_cow_map = {};

        m_volatile = true;
        m_was_purged = false;

        for_each_region([&](auto& region) { region.remap(); });
        return KSuccess;
    }

    size_t committed_pages_needed = 0;
    for (auto& page : m_physical_pages) {
        VERIFY(page);
        if (page->is_shared_zero_page())
            ++committed_pages_needed;
    }

    if (!committed_pages_needed) {
        m_volatile = false;
        return KSuccess;
    }

    m_unused_committed_pages = MM.commit_user_physical_pages(committed_pages_needed);
    if (!m_unused_committed_pages.has_value())
        return ENOMEM;

    for (auto& page : m_physical_pages) {
        if (page->is_shared_zero_page())
            page = MM.lazy_committed_page();
    }

    m_volatile = false;
    m_was_purged = false;
    for_each_region([&](auto& region) { region.remap(); });
    return KSuccess;
}

NonnullRefPtr<PhysicalPage> AnonymousVMObject::allocate_committed_page(Badge<Region>)
{
    return m_unused_committed_pages->take_one();
}

Bitmap& AnonymousVMObject::ensure_cow_map()
{
    if (m_cow_map.is_null())
        m_cow_map = Bitmap { page_count(), true };
    return m_cow_map;
}

void AnonymousVMObject::ensure_or_reset_cow_map()
{
    if (m_cow_map.is_null())
        ensure_cow_map();
    else
        m_cow_map.fill(true);
}

bool AnonymousVMObject::should_cow(size_t page_index, bool is_shared) const
{
    auto& page = physical_pages()[page_index];
    if (page && (page->is_shared_zero_page() || page->is_lazy_committed_page()))
        return true;
    if (is_shared)
        return false;
    return !m_cow_map.is_null() && m_cow_map.get(page_index);
}

void AnonymousVMObject::set_should_cow(size_t page_index, bool cow)
{
    ensure_cow_map().set(page_index, cow);
}

size_t AnonymousVMObject::cow_pages() const
{
    if (m_cow_map.is_null())
        return 0;
    return m_cow_map.count_slow(true);
}

PageFaultResponse AnonymousVMObject::handle_cow_fault(size_t page_index, VirtualAddress vaddr)
{
    VERIFY_INTERRUPTS_DISABLED();
    ScopedSpinLock lock(m_lock);

    if (is_volatile()) {

        dbgln("COW fault in volatile region, will crash.");
        return PageFaultResponse::ShouldCrash;
    }

    auto& page_slot = physical_pages()[page_index];


    if (m_shared_committed_cow_pages && m_shared_committed_cow_pages->is_empty())
        m_shared_committed_cow_pages = nullptr;

    if (page_slot->ref_count() == 1) {
        dbgln_if(PAGE_FAULT_DEBUG, "    >> It's a COW page but nobody is sharing it anymore. Remap r/w");
        set_should_cow(page_index, false);

        if (m_shared_committed_cow_pages) {
            m_shared_committed_cow_pages->uncommit_one();
            if (m_shared_committed_cow_pages->is_empty())
                m_shared_committed_cow_pages = nullptr;
        }
        return PageFaultResponse::Continue;
    }

    RefPtr<PhysicalPage> page;
    if (m_shared_committed_cow_pages) {
        dbgln_if(PAGE_FAULT_DEBUG, "    >> It's a committed COW page and it's time to COW!");
        page = m_shared_committed_cow_pages->take_one();
    } else {
        dbgln_if(PAGE_FAULT_DEBUG, "    >> It's a COW page and it's time to COW!");
        page = MM.allocate_user_physical_page(MemoryManager::ShouldZeroFill::No);
        if (page.is_null()) {
            dmesgln("MM: handle_cow_fault was unable to allocate a physical page");
            return PageFaultResponse::OutOfMemory;
        }
    }

    u8* dest_ptr = MM.quickmap_page(*page);
    dbgln_if(PAGE_FAULT_DEBUG, "      >> COW {} <- {}", page->paddr(), page_slot->paddr());
    {
        SmapDisabler disabler;
        void* fault_at;
        if (!safe_memcpy(dest_ptr, vaddr.as_ptr(), PAGE_SIZE, fault_at)) {
            if ((u8*)fault_at >= dest_ptr && (u8*)fault_at <= dest_ptr + PAGE_SIZE)
                dbgln("      >> COW: error copying page {}/{} to {}/{}: failed to write to page at {}",
                    page_slot->paddr(), vaddr, page->paddr(), VirtualAddress(dest_ptr), VirtualAddress(fault_at));
            else if ((u8*)fault_at >= vaddr.as_ptr() && (u8*)fault_at <= vaddr.as_ptr() + PAGE_SIZE)
                dbgln("      >> COW: error copying page {}/{} to {}/{}: failed to read from page at {}",
                    page_slot->paddr(), vaddr, page->paddr(), VirtualAddress(dest_ptr), VirtualAddress(fault_at));
            else
                VERIFY_NOT_REACHED();
        }
    }
    page_slot = move(page);
    MM.unquickmap_page();
    set_should_cow(page_index, false);
    return PageFaultResponse::Continue;
}

AnonymousVMObject::SharedCommittedCowPages::SharedCommittedCowPages(CommittedPhysicalPageSet&& committed_pages)
    : m_committed_pages(move(committed_pages))
{
}

AnonymousVMObject::SharedCommittedCowPages::~SharedCommittedCowPages()
{
}

NonnullRefPtr<PhysicalPage> AnonymousVMObject::SharedCommittedCowPages::take_one()
{
    ScopedSpinLock locker(m_lock);
    return m_committed_pages.take_one();
}

void AnonymousVMObject::SharedCommittedCowPages::uncommit_one()
{
    ScopedSpinLock locker(m_lock);
    m_committed_pages.uncommit_one();
}

}