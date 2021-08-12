/*
 * Copyright (c) 2021, Krisna Pranav
 *
 * SPDX-License-Identifier: BSD-2-Clause
*/

#pragma once

// includes
#include <base/OwnPtr.h>
#include <kernel/bus/pci/Access.h>
#include <kernel/bus/pci/Device.h>
#include <kernel/IO.h>
#include <kernel/net/NetworkAdapter.h>
#include <kernel/Random.h>

namespace Kernel {

class NE2000NetworkAdapter final : public NetworkAdapter
    , public PCI::Device {
public:
    static RefPtr<NE2000NetworkAdapter> try_to_initialize(PCI::Address);

    virtual ~NE2000NetworkAdapter() override;

    virtual void send_raw(ReadonlyBytes) override;
    virtual bool link_up() override
    {
        return true;
    }
    virtual i32 link_speed()
    {
        return 10;
    }
    virtual bool link_full_duplex() { return true; }

    virtual StringView purpose() const override { return class_name(); }

private:
    NE2000NetworkAdapter(PCI::Address, u8 irq);
    virtual bool handle_irq(const RegisterState&) override;
    virtual StringView class_name() const override { return "NE2000NetworkAdapter"sv; }

    int ram_test();
    void reset();

    void rdma_read(size_t address, Bytes payload);
    void rdma_write(size_t address, ReadonlyBytes payload);

    void receive();

    void out8(u16 address, u8 data);
    void out16(u16 address, u16 data);
    u8 in8(u16 address);
    u16 in16(u16 address);

    IOAddress m_io_base;
    int m_ring_read_ptr;
    u8 m_interrupt_line { 0 };

    MACAddress m_mac_address;
    EntropySource m_entropy_source;

    WaitQueue m_wait_queue;
};
}