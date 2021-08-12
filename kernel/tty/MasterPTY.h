/*
 * Copyright (c) 2021, Krisna Pranav
 *
 * SPDX-License-Identifier: BSD-2-Clause
*/

#pragma once

// includes
#include <base/Badge.h>
#include <kernel/devices/CharacterDevice.h>
#include <kernel/DoubleBuffer.h>

namespace Kernel {

class SlavePTY;

class MasterPTY final : public CharacterDevice {
public:
    [[nodiscard]] static RefPtr<MasterPTY> try_create(unsigned index);
    virtual ~MasterPTY() override;

    unsigned index() const { return m_index; }
    String pts_name() const;
    KResultOr<size_t> on_slave_write(const UserOrKernelBuffer&, size_t);
    bool can_write_from_slave() const;
    void notify_slave_closed(Badge<SlavePTY>);
    bool is_closed() const { return m_closed; }

    virtual String absolute_path(const FileDescription&) const override;

    virtual mode_t required_mode() const override { return 0640; }
    virtual String device_name() const override;

private:
    explicit MasterPTY(unsigned index, NonnullOwnPtr<DoubleBuffer> buffer);

    virtual KResultOr<size_t> read(FileDescription&, u64, UserOrKernelBuffer&, size_t) override;
    virtual KResultOr<size_t> write(FileDescription&, u64, const UserOrKernelBuffer&, size_t) override;
    virtual bool can_read(const FileDescription&, size_t) const override;
    virtual bool can_write(const FileDescription&, size_t) const override;
    virtual KResult close() override;
    virtual bool is_master_pty() const override { return true; }
    virtual KResult ioctl(FileDescription&, unsigned request, Userspace<void*> arg) override;
    virtual StringView class_name() const override { return "MasterPTY"; }

    RefPtr<SlavePTY> m_slave;
    unsigned m_index;
    bool m_closed { false };
    NonnullOwnPtr<DoubleBuffer> m_buffer;
    String m_pts_name;
};

}