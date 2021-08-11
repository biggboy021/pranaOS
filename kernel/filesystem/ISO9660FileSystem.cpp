/*
 * Copyright (c) 2021, Krisna Pranav
 *
 * SPDX-License-Identifier: BSD-2-Clause
*/

// includes
#include "ISO9660FileSystem.h"
#include "kernel/FileSystem/BlockBasedFileSystem.h"
#include <base/CharacterTypes.h>
#include <base/Endian.h>
#include <base/HashFunctions.h>
#include <base/NonnullRefPtr.h>
#include <base/OwnPtr.h>
#include <base/RefPtr.h>
#include <base/StringHash.h>
#include <base/StringView.h>
#include <kernel/Debug.h>
#include <kernel/Forward.h>
#include <kernel/KBuffer.h>
#include <kernel/KResult.h>
#include <kernel/UnixTypes.h>
#include <kernel/UserOrKernelBuffer.h>

namespace Kernel {

constexpr u32 first_data_area_block = 16;
constexpr u32 logical_sector_size = 2048;
constexpr u32 max_cached_directory_entries = 128;

struct DirectoryState {
    RefPtr<ISO9660FS::DirectoryEntry> entry;
    u32 offset { 0 };
};

class ISO9660DirectoryIterator {
public:
    ISO9660DirectoryIterator(ISO9660FS& fs, ISO::DirectoryRecordHeader const& header)
        : m_fs(fs)
        , m_current_header(&header)
    {
        (void)read_directory_contents();
        get_header();
    }

    ISO::DirectoryRecordHeader const* operator*() { return m_current_header; }

    KResultOr<bool> next()
    {
        if (done())
            return false;
        dbgln_if(ISO9660_VERY_DEBUG, "next(): Called");

        if (has_flag(m_current_header->file_flags, ISO::FileFlags::Directory)) {
            dbgln_if(ISO9660_VERY_DEBUG, "next(): Recursing");
            {
                bool result = m_directory_stack.try_append(move(m_current_directory));
                if (!result) {
                    return ENOMEM;
                }
            }

            dbgln_if(ISO9660_VERY_DEBUG, "next(): Pushed into directory stack");

            {
                auto result = read_directory_contents();
                if (result.is_error()) {
                    return result;
                }
            }

            dbgln_if(ISO9660_VERY_DEBUG, "next(): Read directory contents");

            m_current_directory.offset = 0;
            get_header();
            if (m_current_header->length == 0) {

                if (!go_up())
                    return false;
            } else {

                return true;
            }
        }

        return skip();
    }

    bool skip()
    {
        VERIFY(m_current_directory.entry);

        if (m_current_directory.offset >= m_current_directory.entry->length) {
            dbgln_if(ISO9660_VERY_DEBUG, "skip(): Was at last item already");
            return false;
        }

        m_current_directory.offset += m_current_header->length;
        get_header();
        if (m_current_header->length == 0) {

            u32 remaining_bytes = m_current_directory.entry->length - m_current_directory.offset;
            if (remaining_bytes > m_fs.logical_block_size()) {
                m_current_directory.offset += remaining_bytes % m_fs.logical_block_size();
                get_header();

                dbgln_if(ISO9660_VERY_DEBUG, "skip(): Snapped to next logical block (succeeded)");
                return true;
            }

            dbgln_if(ISO9660_VERY_DEBUG, "skip(): Was at the last logical block, at padding now (offset {}, data length {})", m_current_directory.entry->length, m_current_directory.offset);
            return false;
        }

        dbgln_if(ISO9660_VERY_DEBUG, "skip(): Skipped to next item");
        return true;
    }

    bool go_up()
    {
        if (m_directory_stack.is_empty()) {
            dbgln_if(ISO9660_VERY_DEBUG, "go_up(): Empty directory stack");
            return false;
        }

        m_current_directory = m_directory_stack.take_last();
        get_header();

        dbgln_if(ISO9660_VERY_DEBUG, "go_up(): Went up a directory");
        return true;
    }

    bool done() const
    {
        VERIFY(m_current_directory.entry);
        auto result = m_directory_stack.is_empty() && m_current_directory.offset >= m_current_directory.entry->length;
        dbgln_if(ISO9660_VERY_DEBUG, "done(): {}", result);
        return result;
    }

private:
    KResult read_directory_contents()
    {
        auto result = m_fs.directory_entry_for_record({}, m_current_header);
        if (result.is_error()) {
            return result.error();
        }

        m_current_directory.entry = result.value();
        return KSuccess;
    }

    void get_header()
    {
        VERIFY(m_current_directory.entry);
        if (!m_current_directory.entry->blocks)
            return;

        m_current_header = reinterpret_cast<ISO::DirectoryRecordHeader const*>(m_current_directory.entry->blocks->data() + m_current_directory.offset);
    }

    ISO9660FS& m_fs;

    DirectoryState m_current_directory;
    ISO::DirectoryRecordHeader const* m_current_header { nullptr };

    Vector<DirectoryState> m_directory_stack;
};

KResultOr<NonnullRefPtr<ISO9660FS>> ISO9660FS::try_create(FileDescription& description)
{
    auto result = adopt_ref_if_nonnull(new (nothrow) ISO9660FS(description));
    if (!result) {
        return ENOMEM;
    }
    return result.release_nonnull();
}

ISO9660FS::ISO9660FS(FileDescription& description)
    : BlockBasedFileSystem(description)
{
    set_block_size(logical_sector_size);
    m_logical_block_size = logical_sector_size;
}

ISO9660FS::~ISO9660FS()
{
}

bool ISO9660FS::initialize()
{
    if (!BlockBasedFileSystem::initialize())
        return false;

    if (parse_volume_set().is_error())
        return false;
    if (create_root_inode().is_error())
        return false;
    return true;
}

Inode& ISO9660FS::root_inode()
{
    VERIFY(!m_root_inode.is_null());
    return *m_root_inode;
}

unsigned ISO9660FS::total_block_count() const
{
    return LittleEndian { m_primary_volume->volume_space_size.little };
}

unsigned ISO9660FS::total_inode_count() const
{
    if (!m_cached_inode_count) {
        auto result = calculate_inode_count();
        if (result.is_error()) {

            return 0;
        }
    }

    return m_cached_inode_count;
}

u8 ISO9660FS::internal_file_type_to_directory_entry_type(const DirectoryEntryView& entry) const
{
    if (has_flag(static_cast<ISO::FileFlags>(entry.file_type), ISO::FileFlags::Directory)) {
        return DT_DIR;
    }

    return DT_REG;
}

KResult ISO9660FS::parse_volume_set()
{
    VERIFY(!m_primary_volume);

    auto block = KBuffer::try_create_with_size(m_logical_block_size, Memory::Region::Access::Read | Memory::Region::Access::Write, "ISO9660FS: Temporary volume descriptor storage");
    if (!block) {
        return ENOMEM;
    }

    auto block_buffer = UserOrKernelBuffer::for_kernel_buffer(block->data());

    auto current_block_index = first_data_area_block;
    while (true) {
        bool result = raw_read(BlockIndex { current_block_index }, block_buffer);
        if (!result) {
            dbgln_if(ISO9660_DEBUG, "Failed to read volume descriptor from ISO file");
            return EIO;
        }

        auto header = reinterpret_cast<ISO::VolumeDescriptorHeader const*>(block->data());
        if (StringView { header->identifier, 5 } != "CD001") {
            dbgln_if(ISO9660_DEBUG, "Header magic at volume descriptor {} is not valid", current_block_index - first_data_area_block);
            return EIO;
        }

        switch (header->type) {
        case ISO::VolumeDescriptorType::PrimaryVolumeDescriptor: {
            auto primary_volume = reinterpret_cast<ISO::PrimaryVolumeDescriptor const*>(header);
            m_primary_volume = adopt_own_if_nonnull(new ISO::PrimaryVolumeDescriptor(*primary_volume));
            break;
        }
        case ISO::VolumeDescriptorType::BootRecord:
        case ISO::VolumeDescriptorType::SupplementaryOrEnhancedVolumeDescriptor:
        case ISO::VolumeDescriptorType::VolumePartitionDescriptor: {
            break;
        }
        case ISO::VolumeDescriptorType::VolumeDescriptorSetTerminator: {
            goto all_headers_read;
        }
        default:
            dbgln_if(ISO9660_DEBUG, "Unexpected volume descriptor type {} in volume set", static_cast<u8>(header->type));
            return EIO;
        }

        current_block_index++;
    }

all_headers_read:
    if (!m_primary_volume) {
        dbgln_if(ISO9660_DEBUG, "Could not find primary volume");
        return EIO;
    }

    m_logical_block_size = LittleEndian { m_primary_volume->logical_block_size.little };
    return KSuccess;
}

KResult ISO9660FS::create_root_inode()
{
    if (!m_primary_volume) {
        dbgln_if(ISO9660_DEBUG, "Primary volume doesn't exist, can't create root inode");
        return EIO;
    }

    auto maybe_inode = ISO9660Inode::try_create_from_directory_record(*this, m_primary_volume->root_directory_record_header, {});
    if (maybe_inode.is_error()) {
        return ENOMEM;
    }

    m_root_inode = maybe_inode.release_value();
    return KSuccess;
}

KResult ISO9660FS::calculate_inode_count() const
{
    if (!m_primary_volume) {
        dbgln_if(ISO9660_DEBUG, "Primary volume doesn't exist, can't calculate inode count");
        return EIO;
    }

    size_t inode_count = 1;

    auto traversal_result = visit_directory_record(m_primary_volume->root_directory_record_header, [&](ISO::DirectoryRecordHeader const* header) {
        if (header == nullptr) {
            return RecursionDecision::Continue;
        }

        inode_count += 1;

        if (has_flag(header->file_flags, ISO::FileFlags::Directory)) {
            if (header->file_identifier_length == 1) {
                auto file_identifier = reinterpret_cast<u8 const*>(header + 1);
                if (file_identifier[0] == '\0' || file_identifier[0] == '\1') {
                    return RecursionDecision::Continue;
                }
            }

            return RecursionDecision::Recurse;
        }

        return RecursionDecision::Continue;
    });

    if (traversal_result.is_error()) {
        dbgln_if(ISO9660_DEBUG, "Failed to traverse for caching inode count!");
        return traversal_result;
    }

    m_cached_inode_count = inode_count;
    return KSuccess;
}

KResult ISO9660FS::visit_directory_record(ISO::DirectoryRecordHeader const& record, Function<KResultOr<RecursionDecision>(ISO::DirectoryRecordHeader const*)> const& visitor) const
{
    if (!has_flag(record.file_flags, ISO::FileFlags::Directory)) {
        return KSuccess;
    }

    ISO9660DirectoryIterator iterator { const_cast<ISO9660FS&>(*this), record };

    while (!iterator.done()) {
        auto maybe_result = visitor(*iterator);
        if (maybe_result.is_error()) {
            return maybe_result.error();
        }

        switch (maybe_result.value()) {
        case RecursionDecision::Recurse: {
            auto maybe_has_moved = iterator.next();
            if (maybe_has_moved.is_error()) {
                return maybe_has_moved.error();
            }

            if (!maybe_has_moved.value()) {

                return KSuccess;
            }

            continue;
        }
        case RecursionDecision::Continue: {
            while (!iterator.done()) {
                if (iterator.skip())
                    break;
                if (!iterator.go_up())
                    return KSuccess;
            }

            continue;
        }
        case RecursionDecision::Break:
            return KSuccess;
        }
    }

    return KSuccess;
}

KResultOr<NonnullRefPtr<ISO9660FS::DirectoryEntry>> ISO9660FS::directory_entry_for_record(Badge<ISO9660DirectoryIterator>, ISO::DirectoryRecordHeader const* record)
{
    u32 extent_location = LittleEndian { record->extent_location.little };
    u32 data_length = LittleEndian { record->data_length.little };

    auto key = calculate_directory_entry_cache_key(*record);
    auto it = m_directory_entry_cache.find(key);
    if (it != m_directory_entry_cache.end()) {
        dbgln_if(ISO9660_DEBUG, "Cache hit for dirent @ {}", extent_location);
        return it->value;
    }
    dbgln_if(ISO9660_DEBUG, "Cache miss for dirent @ {} :^(", extent_location);

    if (m_directory_entry_cache.size() == max_cached_directory_entries) {

        m_directory_entry_cache.remove(m_directory_entry_cache.begin());
    }

    if (!(data_length % logical_block_size() == 0)) {
        dbgln_if(ISO9660_DEBUG, "Found a directory with non-logical block size aligned data length!");
        return EIO;
    }

    auto blocks = KBuffer::try_create_with_size(data_length, Memory::Region::Access::Read | Memory::Region::Access::Write, "ISO9660FS: Directory traversal buffer");
    if (!blocks) {
        return ENOMEM;
    }

    auto blocks_buffer = UserOrKernelBuffer::for_kernel_buffer(blocks->data());
    auto did_read = raw_read_blocks(BlockBasedFileSystem::BlockIndex { extent_location }, data_length / logical_block_size(), blocks_buffer);
    if (!did_read) {
        return EIO;
    }

    auto maybe_entry = DirectoryEntry::try_create(extent_location, data_length, move(blocks));
    if (maybe_entry.is_error()) {
        return maybe_entry.error();
    }
    m_directory_entry_cache.set(key, maybe_entry.value());

    dbgln_if(ISO9660_DEBUG, "Cached dirent @ {}", extent_location);
    return maybe_entry.release_value();
}

u32 ISO9660FS::calculate_directory_entry_cache_key(ISO::DirectoryRecordHeader const& record)
{
    return LittleEndian { record.extent_location.little };
}

KResultOr<size_t> ISO9660Inode::read_bytes(off_t offset, size_t size, UserOrKernelBuffer& buffer, FileDescription*) const
{
    MutexLocker inode_locker(m_inode_lock);

    auto& file_system = const_cast<ISO9660FS&>(static_cast<ISO9660FS const&>(fs()));
    u32 data_length = LittleEndian { m_record.data_length.little };
    u32 extent_location = LittleEndian { m_record.extent_location.little };

    if (static_cast<u64>(offset) >= data_length)
        return 0;

    auto block = KBuffer::try_create_with_size(file_system.m_logical_block_size);
    if (!block) {
        return ENOMEM;
    }
    auto block_buffer = UserOrKernelBuffer::for_kernel_buffer(block->data());

    size_t total_bytes = min(size, data_length - offset);
    size_t nread = 0;
    size_t blocks_already_read = offset / file_system.m_logical_block_size;
    size_t initial_offset = offset % file_system.m_logical_block_size;

    auto current_block_index = BlockBasedFileSystem::BlockIndex { extent_location + blocks_already_read };
    while (nread != total_bytes) {
        size_t bytes_to_read = min(total_bytes - nread, file_system.logical_block_size() - initial_offset);
        auto buffer_offset = buffer.offset(nread);
        dbgln_if(ISO9660_VERY_DEBUG, "ISO9660Inode::read_bytes: Reading {} bytes into buffer offset {}/{}, logical block index: {}", bytes_to_read, nread, total_bytes, current_block_index.value());

        if (bool result = file_system.raw_read(current_block_index, block_buffer); !result) {
            return EIO;
        }

        bool result = buffer_offset.write(block->data() + initial_offset, bytes_to_read);
        if (!result) {
            return EFAULT;
        }

        nread += bytes_to_read;
        initial_offset = 0;
        current_block_index = BlockBasedFileSystem::BlockIndex { current_block_index.value() + 1 };
    }

    return nread;
}

InodeMetadata ISO9660Inode::metadata() const
{
    return m_metadata;
}

KResult ISO9660Inode::traverse_as_directory(Function<bool(FileSystem::DirectoryEntryView const&)> visitor) const
{
    auto& file_system = static_cast<ISO9660FS const&>(fs());
    Array<u8, max_file_identifier_length> file_identifier_buffer;

    auto traversal_result = file_system.visit_directory_record(m_record, [&](ISO::DirectoryRecordHeader const* record) {
        StringView filename = get_normalized_filename(*record, file_identifier_buffer);
        dbgln_if(ISO9660_VERY_DEBUG, "traverse_as_directory(): Found {}", filename);

        InodeIdentifier id { fsid(), get_inode_index(*record, filename) };
        auto entry = FileSystem::DirectoryEntryView(filename, id, static_cast<u8>(record->file_flags));

        if (!visitor(entry)) {
            return RecursionDecision::Break;
        }

        return RecursionDecision::Continue;
    });

    if (traversal_result.is_error())
        return traversal_result;

    return KSuccess;
}

RefPtr<Inode> ISO9660Inode::lookup(StringView name)
{
    auto& file_system = static_cast<ISO9660FS const&>(fs());
    RefPtr<Inode> inode;
    Array<u8, max_file_identifier_length> file_identifier_buffer;

    auto traversal_result = file_system.visit_directory_record(m_record, [&](ISO::DirectoryRecordHeader const* record) {
        StringView filename = get_normalized_filename(*record, file_identifier_buffer);

        if (filename == name) {
            auto maybe_inode = ISO9660Inode::try_create_from_directory_record(const_cast<ISO9660FS&>(file_system), *record, filename);
            if (maybe_inode.is_error()) {

                dbgln("Could not allocate inode for lookup!");
            } else {
                inode = maybe_inode.release_value();
            }
            return RecursionDecision::Break;
        }

        return RecursionDecision::Continue;
    });

    if (traversal_result.is_error()) {
        return {};
    }

    return inode;
}

void ISO9660Inode::flush_metadata()
{
}

KResultOr<size_t> ISO9660Inode::write_bytes(off_t, size_t, const UserOrKernelBuffer&, FileDescription*)
{
    return EROFS;
}

KResultOr<NonnullRefPtr<Inode>> ISO9660Inode::create_child(StringView, mode_t, dev_t, uid_t, gid_t)
{
    return EROFS;
}

KResult ISO9660Inode::add_child(Inode&, const StringView&, mode_t)
{
    return EROFS;
}

KResult ISO9660Inode::remove_child(const StringView&)
{
    return EROFS;
}

KResult ISO9660Inode::chmod(mode_t)
{
    return EROFS;
}

KResult ISO9660Inode::chown(uid_t, gid_t)
{
    return EROFS;
}

KResult ISO9660Inode::truncate(u64)
{
    return EROFS;
}

KResult ISO9660Inode::set_atime(time_t)
{
    return EROFS;
}

KResult ISO9660Inode::set_ctime(time_t)
{
    return EROFS;
}

KResult ISO9660Inode::set_mtime(time_t)
{
    return EROFS;
}

void ISO9660Inode::one_ref_left()
{
}

ISO9660Inode::ISO9660Inode(ISO9660FS& fs, ISO::DirectoryRecordHeader const& record, StringView const& name)
    : Inode(fs, get_inode_index(record, name))
    , m_record(record)
{
    dbgln_if(ISO9660_VERY_DEBUG, "Creating inode #{}", index());
    create_metadata();
}

ISO9660Inode::~ISO9660Inode()
{
}

KResultOr<NonnullRefPtr<ISO9660Inode>> ISO9660Inode::try_create_from_directory_record(ISO9660FS& fs, ISO::DirectoryRecordHeader const& record, StringView const& name)
{
    auto result = adopt_ref_if_nonnull(new (nothrow) ISO9660Inode(fs, record, name));
    if (!result) {
        return ENOMEM;
    }
    return result.release_nonnull();
}

void ISO9660Inode::create_metadata()
{
    u32 data_length = LittleEndian { m_record.data_length.little };
    bool is_directory = has_flag(m_record.file_flags, ISO::FileFlags::Directory);
    time_t recorded_at = parse_numerical_date_time(m_record.recording_date_and_time);

    m_metadata = {
        .inode = identifier(),
        .size = data_length,
        .mode = static_cast<mode_t>((is_directory ? S_IFDIR : S_IFREG) | (is_directory ? 0555 : 0444)),
        .uid = 0,
        .gid = 0,
        .link_count = 1,
        .atime = recorded_at,
        .ctime = recorded_at,
        .mtime = recorded_at,
        .dtime = 0,
        .block_count = 0,
        .block_size = 0,
        .major_device = 0,
        .minor_device = 0,
    };
}

time_t ISO9660Inode::parse_numerical_date_time(ISO::NumericalDateAndTime const& date)
{
    i32 year_offset = date.years_since_1900 - 70;

    return (year_offset * 60 * 60 * 24 * 30 * 12)
        + (date.month * 60 * 60 * 24 * 30)
        + (date.day * 60 * 60 * 24)
        + (date.hour * 60 * 60)
        + (date.minute * 60)
        + date.second;
}

StringView ISO9660Inode::get_normalized_filename(ISO::DirectoryRecordHeader const& record, Bytes buffer)
{
    auto file_identifier = reinterpret_cast<u8 const*>(&record + 1);
    auto filename = StringView { file_identifier, record.file_identifier_length };

    if (filename.length() == 1) {
        if (filename[0] == '\0') {
            filename = "."sv;
        }

        if (filename[0] == '\1') {
            filename = ".."sv;
        }
    }

    if (!has_flag(record.file_flags, ISO::FileFlags::Directory)) {
        
        Optional<size_t> semicolon = filename.find(';');
        if (semicolon.has_value()) {
            filename = filename.substring_view(0, semicolon.value());
        }

        if (filename[filename.length() - 1] == '.') {
            filename = filename.substring_view(0, filename.length() - 1);
        }
    }

    if (filename.length() > buffer.size()) {

        filename = filename.substring_view(0, buffer.size());
    }

    for (size_t i = 0; i < filename.length(); i++) {
        buffer[i] = to_ascii_lowercase(filename[i]);
    }

    return { buffer.data(), filename.length() };
}

InodeIndex ISO9660Inode::get_inode_index(ISO::DirectoryRecordHeader const& record, StringView const& name)
{
    if (name.is_null()) {
        // NOTE: This is the index of the root inode.
        return 1;
    }

    return { pair_int_hash(LittleEndian { record.extent_location.little }, string_hash(name.characters_without_null_termination(), name.length())) };
}

}