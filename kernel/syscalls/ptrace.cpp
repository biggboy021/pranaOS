/*
 * Copyright (c) 2021, Krisna Pranav
 *
 * SPDX-License-Identifier: BSD-2-Clause
*/

// includes
#include <base/ScopeGuard.h>
#include <kernel/memory/MemoryManager.h>
#include <kernel/memory/PrivateInodeVMObject.h>
#include <kernel/memory/ProcessPagingScope.h>
#include <kernel/memory/Region.h>
#include <kernel/memory/SharedInodeVMObject.h>
#include <kernel/Process.h>
#include <kernel/ThreadTracer.h>

namespace Kernel {

static KResultOr<u32> handle_ptrace(const Kernel::Syscall::SC_ptrace_params& params, Process& caller)
{
    ScopedSpinLock scheduler_lock(g_scheduler_lock);
    if (params.request == PT_TRACE_ME) {
        if (Process::current()->tracer())
            return EBUSY;

        caller.set_wait_for_tracer_at_next_execve(true);
        return KSuccess;
    }

    if (params.tid == caller.pid().value())
        return EINVAL;

    auto peer = Thread::from_tid(params.tid);
    if (!peer)
        return ESRCH;

    MutexLocker ptrace_locker(peer->process().ptrace_lock());

    if ((peer->process().uid() != caller.euid())
        || (peer->process().uid() != peer->process().euid())) 
        return EACCES;

    if (!peer->process().is_dumpable())
        return EACCES;

    auto& peer_process = peer->process();
    if (params.request == PT_ATTACH) {
        if (peer_process.tracer()) {
            return EBUSY;
        }
        auto result = peer_process.start_tracing_from(caller.pid());
        if (result.is_error())
            return result.error();
        ScopedSpinLock lock(peer->get_lock());
        if (peer->state() != Thread::State::Stopped) {
            peer->send_signal(SIGSTOP, &caller);
        }
        return KSuccess;
    }

    auto* tracer = peer_process.tracer();

    if (!tracer)
        return EPERM;

    if (tracer->tracer_pid() != caller.pid())
        return EBUSY;

    if (peer->state() == Thread::State::Running)
        return EBUSY;

    scheduler_lock.unlock();

    switch (params.request) {
    case PT_CONTINUE:
        peer->send_signal(SIGCONT, &caller);
        break;

    case PT_DETACH:
        peer_process.stop_tracing();
        peer->send_signal(SIGCONT, &caller);
        break;

    case PT_SYSCALL:
        tracer->set_trace_syscalls(true);
        peer->send_signal(SIGCONT, &caller);
        break;

    case PT_GETREGS: {
        if (!tracer->has_regs())
            return EINVAL;
        auto* regs = reinterpret_cast<PtraceRegisters*>(params.addr);
        if (!copy_to_user(regs, &tracer->regs()))
            return EFAULT;
        break;
    }

    case PT_SETREGS: {
        if (!tracer->has_regs())
            return EINVAL;

        PtraceRegisters regs {};
        if (!copy_from_user(&regs, (const PtraceRegisters*)params.addr))
            return EFAULT;

        auto& peer_saved_registers = peer->get_register_dump_from_stack();

        if ((peer_saved_registers.cs & 0x03) != 3)
            return EFAULT;

        tracer->set_regs(regs);
        copy_ptrace_registers_into_kernel_registers(peer_saved_registers, regs);
        break;
    }

    case PT_PEEK: {
        Kernel::Syscall::SC_ptrace_peek_params peek_params {};
        if (!copy_from_user(&peek_params, reinterpret_cast<Kernel::Syscall::SC_ptrace_peek_params*>(params.addr)))
            return EFAULT;
        if (!Memory::is_user_address(VirtualAddress { peek_params.address }))
            return EFAULT;
        auto result = peer->process().peek_user_data(Userspace<const u32*> { (FlatPtr)peek_params.address });
        if (result.is_error())
            return result.error();
        if (!copy_to_user(peek_params.out_data, &result.value()))
            return EFAULT;
        break;
    }

    case PT_POKE:
        if (!Memory::is_user_address(VirtualAddress { params.addr }))
            return EFAULT;
        return peer->process().poke_user_data(Userspace<u32*> { (FlatPtr)params.addr }, params.data);

    case PT_PEEKDEBUG: {
        Kernel::Syscall::SC_ptrace_peek_params peek_params {};
        if (!copy_from_user(&peek_params, reinterpret_cast<Kernel::Syscall::SC_ptrace_peek_params*>(params.addr)))
            return EFAULT;
        auto result = peer->peek_debug_register(reinterpret_cast<uintptr_t>(peek_params.address));
        if (result.is_error())
            return result.error();
        if (!copy_to_user(peek_params.out_data, &result.value()))
            return EFAULT;
        break;
    }
    case PT_POKEDEBUG:
        return peer->poke_debug_register(reinterpret_cast<uintptr_t>(params.addr), params.data);
    default:
        return EINVAL;
    }

    return KSuccess;
}

KResultOr<FlatPtr> Process::sys$ptrace(Userspace<const Syscall::SC_ptrace_params*> user_params)
{
    VERIFY_PROCESS_BIG_LOCK_ACQUIRED(this)
    REQUIRE_PROMISE(ptrace);
    Syscall::SC_ptrace_params params {};
    if (!copy_from_user(&params, user_params))
        return EFAULT;
    auto result = handle_ptrace(params, *this);
    return result.is_error() ? result.error().error() : result.value();
}

/**
 * "Does this process have a thread that is currently being traced by the provided process?"
 */
bool Process::has_tracee_thread(ProcessID tracer_pid)
{
    if (auto tracer = this->tracer())
        return tracer->tracer_pid() == tracer_pid;
    return false;
}

KResultOr<u32> Process::peek_user_data(Userspace<const u32*> address)
{
    uint32_t result;

    ProcessPagingScope scope(*this);
    if (!copy_from_user(&result, address)) {
        dbgln("Invalid address for peek_user_data: {}", address.ptr());
        return EFAULT;
    }

    return result;
}

KResult Process::poke_user_data(Userspace<u32*> address, u32 data)
{
    Memory::VirtualRange range = { VirtualAddress(address), sizeof(u32) };
    auto* region = address_space().find_region_containing(range);
    if (!region)
        return EFAULT;
    ProcessPagingScope scope(*this);
    if (region->is_shared()) {

        VERIFY(region->vmobject().is_shared_inode());
        auto vmobject = Memory::PrivateInodeVMObject::try_create_with_inode(static_cast<Memory::SharedInodeVMObject&>(region->vmobject()).inode());
        if (!vmobject)
            return ENOMEM;
        region->set_vmobject(vmobject.release_nonnull());
        region->set_shared(false);
    }
    const bool was_writable = region->is_writable();
    if (!was_writable) {
        region->set_writable(true);
        region->remap();
    }
    ScopeGuard rollback([&]() {
        if (!was_writable) {
            region->set_writable(false);
            region->remap();
        }
    });

    if (!copy_to_user(address, &data)) {
        dbgln("poke_user_data: Bad address {:p}", address.ptr());
        return EFAULT;
    }

    return KSuccess;
}

KResultOr<u32> Thread::peek_debug_register(u32 register_index)
{
    u32 data;
    switch (register_index) {
    case 0:
        data = m_debug_register_state.dr0;
        break;
    case 1:
        data = m_debug_register_state.dr1;
        break;
    case 2:
        data = m_debug_register_state.dr2;
        break;
    case 3:
        data = m_debug_register_state.dr3;
        break;
    case 6:
        data = m_debug_register_state.dr6;
        break;
    case 7:
        data = m_debug_register_state.dr7;
        break;
    default:
        return EINVAL;
    }
    return data;
}

KResult Thread::poke_debug_register(u32 register_index, u32 data)
{
    switch (register_index) {
    case 0:
        m_debug_register_state.dr0 = data;
        break;
    case 1:
        m_debug_register_state.dr1 = data;
        break;
    case 2:
        m_debug_register_state.dr2 = data;
        break;
    case 3:
        m_debug_register_state.dr3 = data;
        break;
    case 7:
        m_debug_register_state.dr7 = data;
        break;
    default:
        return EINVAL;
    }
    return KSuccess;
}

}