// Copyright 2016 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include "common/archives.h"
#include "core/core.h"
#include "core/hle/ipc_helpers.h"
#include "core/hle/kernel/event.h"
#include "core/hle/lock.h"
#include "core/hle/service/nfc/nfc.h"
#include "core/hle/service/nfc/nfc_m.h"
#include "core/hle/service/nfc/nfc_u.h"

/*
    Convert to a 16-bit big-endian date to be stored on the NFC chip.
    The year is relative to 2000.
    Format: DDDDD-MMMM-YYYYYYY
*/
static u16_be pack_date(u8 month, u8 day, u16 year) {
    return ((day & 0x1F) << 11) | ((month & 0x0F) << 7) | ((year - 2000) & 0x7F);
}

static u8 unpack_date_day(u16_be date) {
    return (date & 0xF800) >> 11;
}

static u8 unpack_date_month(u16_be date) {
    return (date & 0x780) >> 7;
}

static u16 unpack_date_year(u16_be date) {
    return (date & 0x7F) + 2000;
}

/* If this bit is enabled, the "amiibo Settings" app has been used. */
static bool flag_settings_initted(u8 flags) {
    return flags & 0x10;
}

/* If this bit is enabled, AppData exists on the amiibo. */
static bool flag_appdata_initted(u8 flags) {
    return flags & 0x20;
}

SERVICE_CONSTRUCT_IMPL(Service::NFC::Module)
SERIALIZE_EXPORT_IMPL(Service::NFC::Module)

namespace Service::NFC {

template <class Archive>
void Module::serialize(Archive& ar, const unsigned int) {
    ar& tag_in_range_event;
    ar& tag_out_of_range_event;
    ar& nfc_tag_state;
    ar& nfc_status;
    ar& amiibo_data;
    ar& amiibo_decrypted;
    ar& amiibo_in_range;
}
SERIALIZE_IMPL(Module)

#pragma pack(push, 1)

/*
    When id_offset_size is <= 10, it's treated as the length of a UUID in the
    id array.
    When id_offset_size is above 10, it's treated as the position of a 7-byte
    UUID as an offset of the uuid array.
*/
struct TagInfo {
    u16_le id_offset_size;
    u8 unk1;
    u8 unk2;
    std::array<u8, 0x28> id;
};
static_assert(sizeof(TagInfo) == 0x2C,
              "TagInfo is an invalid size");

struct AmiiboConfig {
    u16_le last_write_year;
    u8 last_write_month;
    u8 last_write_day;
    u16_le write_count;
    u16_le char_id;
    u8 char_variant;
    u8 series;
    u16_be model_number;
    u8 figure_type;
    u8 pagex4_byte3;
    u16_le appdata_size;
    INSERT_PADDING_BYTES(0x30);
};
static_assert(sizeof(AmiiboConfig) == 0x40,
              "AmiiboConfig is an invalid size");

struct IdentificationBlockReply {
    u16_le char_id;
    u8 char_variant;
    u8 series;
    u16_le model_number;
    u8 figure_type;
    INSERT_PADDING_BYTES(0x2F);
};
static_assert(sizeof(IdentificationBlockReply) == 0x36,
              "IdentificationBlockReply is an invalid size");

#pragma pack(pop)

void Module::Interface::Initialize(Kernel::HLERequestContext& ctx) {
    IPC::RequestParser rp(ctx, 0x01, 1, 0);
    u8 param = rp.Pop<u8>();

    IPC::RequestBuilder rb = rp.MakeBuilder(1, 0);
    if (nfc->nfc_tag_state != TagState::NotInitialized) {
        LOG_ERROR(Service_NFC, "Invalid TagState {}", static_cast<int>(nfc->nfc_tag_state));
        rb.Push(ResultCode(ErrCodes::CommandInvalidForState, ErrorModule::NFC,
                           ErrorSummary::InvalidState, ErrorLevel::Status));
        return;
    }

    nfc->nfc_tag_state = TagState::NotScanning;

    rb.Push(RESULT_SUCCESS);
    LOG_WARNING(Service_NFC, "(STUBBED) called, param={}", param);
}

void Module::Interface::Shutdown(Kernel::HLERequestContext& ctx) {
    IPC::RequestParser rp(ctx, 0x02, 1, 0);
    u8 param = rp.Pop<u8>();

    nfc->nfc_tag_state = TagState::NotInitialized;

    IPC::RequestBuilder rb = rp.MakeBuilder(1, 0);
    rb.Push(RESULT_SUCCESS);
    LOG_WARNING(Service_NFC, "(STUBBED) called, param={}", param);
}

void Module::Interface::StartCommunication(Kernel::HLERequestContext& ctx) {
    IPC::RequestParser rp(ctx, 0x03, 0, 0);

    IPC::RequestBuilder rb = rp.MakeBuilder(1, 0);
    rb.Push(RESULT_SUCCESS);
    LOG_WARNING(Service_NFC, "(STUBBED) called");
}

void Module::Interface::StopCommunication(Kernel::HLERequestContext& ctx) {
    IPC::RequestParser rp(ctx, 0x04, 0, 0);

    IPC::RequestBuilder rb = rp.MakeBuilder(1, 0);
    rb.Push(RESULT_SUCCESS);
    LOG_WARNING(Service_NFC, "(STUBBED) called");
}

void Module::Interface::StartTagScanning(Kernel::HLERequestContext& ctx) {
    IPC::RequestParser rp(ctx, 0x05, 1, 0); // 0x00050040
    u16 in_val = rp.Pop<u16>();

    IPC::RequestBuilder rb = rp.MakeBuilder(1, 0);
    if (nfc->nfc_tag_state != TagState::NotScanning &&
        nfc->nfc_tag_state != TagState::TagOutOfRange) {
        LOG_ERROR(Service_NFC, "Invalid TagState {}", static_cast<int>(nfc->nfc_tag_state));
        rb.Push(ResultCode(ErrCodes::CommandInvalidForState, ErrorModule::NFC,
                           ErrorSummary::InvalidState, ErrorLevel::Status));
        return;
    }

    nfc->nfc_tag_state = TagState::Scanning;
    nfc->SyncTagState();

    rb.Push(RESULT_SUCCESS);
    LOG_WARNING(Service_NFC, "(STUBBED) called, in_val={:04x}", in_val);
}

void Module::Interface::GetTagInfo(Kernel::HLERequestContext& ctx) {
    IPC::RequestParser rp(ctx, 0x11, 0, 0);

    if (nfc->nfc_tag_state != TagState::TagInRange &&
        nfc->nfc_tag_state != TagState::TagDataLoaded && nfc->nfc_tag_state != TagState::Unknown6) {
        IPC::RequestBuilder rb = rp.MakeBuilder(1, 0);

        rb.Push(ResultCode(ErrCodes::CommandInvalidForState, ErrorModule::NFC,
                           ErrorSummary::InvalidState, ErrorLevel::Status));
        LOG_ERROR(Service_NFC, "Invalid TagState {}", static_cast<int>(nfc->nfc_tag_state));
        return;
    }

    TagInfo tag_info{};
    if (!nfc->amiibo_decrypted) {
        auto amiibo = (AmiiboDataEnc*)nfc->amiibo_data;

        std::memcpy(tag_info.id.data(), amiibo->uuid.data(), amiibo->uuid.size());
        tag_info.id_offset_size = (u16_le)amiibo->uuid.size();
    } else {
        auto amiibo = (AmiiboDataDec*)nfc->amiibo_data;

        std::memcpy(tag_info.id.data(), amiibo->uuid.data(), amiibo->uuid.size());
        tag_info.id_offset_size = (u16_le)amiibo->uuid.size();
    }
    tag_info.unk1 = 0x0;
    tag_info.unk2 = 0x2;

    IPC::RequestBuilder rb = rp.MakeBuilder(12, 0);
    rb.Push(RESULT_SUCCESS);
    rb.PushRaw<TagInfo>(tag_info);
    LOG_DEBUG(Service_NFC, "called");
}

void Module::Interface::GetAmiiboSettings(Kernel::HLERequestContext& ctx) {
    IPC::RequestParser rp(ctx, 0x17, 0, 0);
    AmiiboSettings amiibo_settings{};
    auto amiibo = (AmiiboDataDec*)nfc->amiibo_data;

    if (!nfc->amiibo_decrypted) {
        LOG_ERROR(Service_NFC, "Tried to access with encrypted amiibo.");
        return;
    } else if (!flag_settings_initted(amiibo->flags)) {
        /*
           Settings have not yet been initialized. Pass a zeroed struct.
           It's unclear whether or not RESULT_SUCCESS is sent here.
        */
        std::memset(&amiibo_settings, 0, sizeof(amiibo_settings));
        IPC::RequestBuilder rb = rp.MakeBuilder(43, 0);

        rb.Push(ResultCode(ErrCodes::AmiiboNotSetup, ErrorModule::NFC, ErrorSummary::InvalidState,
                           ErrorLevel::Status));
        rb.PushRaw<AmiiboSettings>(amiibo_settings);
        LOG_WARNING(Service_NFC, "Failed because amiibo is not setup.");
        return;
    }

    std::memcpy(amiibo_settings.mii.data(), amiibo->mii.data(), sizeof(AmiiboSettings::mii));
    std::memcpy(amiibo_settings.nickname.data(), amiibo->nickname.data(), sizeof(AmiiboSettings::nickname));

    /* Apparently only the least significant 4 bits get read in here. */
    amiibo_settings.flags = amiibo->flags & 0xF;
    amiibo_settings.country = amiibo->country;

    /* Getting full setup date from the packed u16 actually stored on the amiibo */
    amiibo_settings.setup_day = unpack_date_day(amiibo->setup_date);
    amiibo_settings.setup_month = unpack_date_month(amiibo->setup_date);
    amiibo_settings.setup_year = unpack_date_year(amiibo->setup_date);

    /* What is this? */
    std::memset(amiibo_settings.dummy.data(), 0, amiibo_settings.dummy.size());

    IPC::RequestBuilder rb = rp.MakeBuilder(43, 0);
    rb.Push(RESULT_SUCCESS);
    rb.PushRaw<AmiiboSettings>(amiibo_settings);
    LOG_DEBUG(Service_NFC, "called");
}

/*
    TODO: "Once all the checks pass, the state field checked by the
    reading/writing commands is set to 1."
*/
void Module::Interface::OpenAppData(Kernel::HLERequestContext& ctx) {
    IPC::RequestParser rp(ctx, 0x13, 1, 0);

    if (!nfc->amiibo_decrypted) {
        LOG_ERROR(Service_NFC, "Tried to access with encrypted amiibo.");
        return;
    } else {
        u32 app_id = rp.Pop<u32>();
        IPC::RequestBuilder rb = rp.MakeBuilder(1, 0);
        auto amiibo = reinterpret_cast<AmiiboDataDec*>(nfc->amiibo_data);

        if (app_id != amiibo->app_id)
            rb.Push(ResultCode(ErrCodes::AppIdMismatch, ErrorModule::NFC,
                               ErrorSummary::InvalidState, ErrorLevel::Status));
        else if (!flag_appdata_initted(amiibo->flags))
            rb.Push(ResultCode(ErrCodes::AppDataUninitialized, ErrorModule::NFC,
                               ErrorSummary::InvalidState, ErrorLevel::Status));
        else
            rb.Push(RESULT_SUCCESS);

        LOG_INFO(Service_NFC, "called");
    }
}

void Module::Interface::InitializeWriteAppData(Kernel::HLERequestContext& ctx) {
    IPC::RequestParser rp(ctx, 0x14, 18, 2);

    if (!nfc->amiibo_decrypted) {
        LOG_ERROR(Service_NFC, "Tried to access with encrypted amiibo.");
        return;
    } else {
        u32 app_id = rp.Pop<u32>();
        u32 size = rp.Pop<u32>();
        std::vector<u8> buffer = rp.PopStaticBuffer();
        auto amiibo = reinterpret_cast<AmiiboDataDec*>(nfc->amiibo_data);

        if (size != sizeof(AmiiboDataDec::app_data))
            LOG_WARNING(Service_NFC, "AppData is of unusual length ({} instead of {}).", size,
                        sizeof(AmiiboDataDec::app_data));
        if (size != buffer.size())
            LOG_WARNING(Service_NFC,
                        "Reported AppData size does not match buffer size ({} versus {}).", size,
                        buffer.size());
        else {
            std::memcpy(amiibo->app_data.data(), buffer.data(), size);
            amiibo->app_id = app_id;
        }

        IPC::RequestBuilder rb = rp.MakeBuilder(1, 0);
        rb.Push(RESULT_SUCCESS);
    }

    LOG_DEBUG(Service_NFC, "called");
}

void Module::Interface::ReadAppData(Kernel::HLERequestContext& ctx) {
    IPC::RequestParser rp(ctx, 0x15, 0, 0);

    if (!nfc->amiibo_decrypted) {
        LOG_ERROR(Service_NFC, "Tried to access with encrypted amiibo.");
        return;
    } else if (nfc->nfc_tag_state == TagState::NotInitialized) {
        IPC::RequestBuilder rb = rp.MakeBuilder(1, 0);
        rb.Push(ResultCode(ErrCodes::CommandInvalidForState, ErrorModule::NFC,
                           ErrorSummary::InvalidState, ErrorLevel::Status));
    } else {
        IPC::RequestBuilder rb = rp.MakeBuilder(1, 2);
        auto amiibo = reinterpret_cast<AmiiboDataDec*>(nfc->amiibo_data);
        std::vector<u8> buffer(sizeof(amiibo->app_data));

        std::memcpy(buffer.data(), amiibo->app_data.data(), buffer.size());
        nfc->nfc_tag_state = TagState::TagDataLoaded; // Is this correct?

        rb.Push(RESULT_SUCCESS);
        rb.PushStaticBuffer(buffer, 0);
    }

    LOG_INFO(Service_NFC, "called");
}

void Module::Interface::WriteAppData(Kernel::HLERequestContext& ctx) {
    IPC::RequestParser rp(ctx, 0x16, 12, 2);

    if (!nfc->amiibo_decrypted) {
        LOG_ERROR(Service_NFC, "Tried to access with encrypted amiibo.");
        return;
    } else if (nfc->nfc_tag_state == TagState::NotInitialized) {
        IPC::RequestBuilder rb = rp.MakeBuilder(1, 0);
        rb.Push(ResultCode(ErrCodes::CommandInvalidForState, ErrorModule::NFC,
                           ErrorSummary::InvalidState, ErrorLevel::Status));
    } else {
        /* Some other code for getting the UID is done here, but not emulated yet. */
        u32 size = rp.Pop<u32>();
        std::vector<u8> dummy = rp.PopStaticBuffer();
        std::vector<u8> buffer = rp.PopStaticBuffer();
        auto amiibo = reinterpret_cast<AmiiboDataDec*>(nfc->amiibo_data);

        if (size != amiibo->app_data.size()) {
            LOG_WARNING(Service_NFC, "AppData is of unusual size ({} instead of {}).", size,
                        amiibo->app_data.size());
            if (size > amiibo->app_data.size())
                size = (u32)amiibo->app_data.size();
        }
        if (size != buffer.size())
            LOG_ERROR(Service_NFC, "AppData size does not match buffer size ({} versus {}).", size,
                      buffer.size());
        else
            std::memcpy(amiibo->app_data.data(), buffer.data(), size);

        nfc->nfc_tag_state = TagState::TagDataLoaded; // Is this correct?

        IPC::RequestBuilder rb = rp.MakeBuilder(1, 0);
        rb.Push(RESULT_SUCCESS);
    }

    LOG_INFO(Service_NFC, "called");
}

void Module::Interface::GetAmiiboConfig(Kernel::HLERequestContext& ctx) {
    IPC::RequestParser rp(ctx, 0x18, 0, 0);

    AmiiboConfig amiibo_config{};
    if (!nfc->amiibo_decrypted) {
        auto amiibo = reinterpret_cast<AmiiboDataEnc*>(nfc->amiibo_data);

        /* Dummy data */
        amiibo_config.last_write_year = 2014;
        amiibo_config.last_write_month = 11;
        amiibo_config.last_write_day = 21;
        amiibo_config.write_count = 1;

        amiibo_config.char_id = amiibo->char_id;
        amiibo_config.char_variant = amiibo->char_variant;
        amiibo_config.series = amiibo->series;
        amiibo_config.model_number = amiibo->model_number;
        amiibo_config.figure_type = amiibo->figure_type;
        amiibo_config.pagex4_byte3 = 0x0;
        amiibo_config.appdata_size = 0;
    } else {
        auto amiibo = reinterpret_cast<AmiiboDataDec*>(nfc->amiibo_data);

        amiibo_config.last_write_year = unpack_date_year(amiibo->last_write_date);
        amiibo_config.last_write_month = unpack_date_month(amiibo->last_write_date);
        amiibo_config.last_write_day = unpack_date_day(amiibo->last_write_date);
        amiibo_config.write_count = amiibo->write_count;
        amiibo_config.char_id = amiibo->char_id;
        amiibo_config.char_variant = amiibo->char_variant;
        amiibo_config.series = amiibo->series;
        amiibo_config.model_number = amiibo->model_number;
        amiibo_config.figure_type = amiibo->figure_type;
        amiibo_config.pagex4_byte3 = 0x0;
        amiibo_config.appdata_size = sizeof(amiibo->app_data);
    }

    IPC::RequestBuilder rb = rp.MakeBuilder(17, 0);
    rb.Push(RESULT_SUCCESS);
    rb.PushRaw<AmiiboConfig>(amiibo_config);
    LOG_DEBUG(Service_NFC, "called");
}

void Module::Interface::StopTagScanning(Kernel::HLERequestContext& ctx) {
    IPC::RequestParser rp(ctx, 0x06, 0, 0);

    IPC::RequestBuilder rb = rp.MakeBuilder(1, 0);
    if (nfc->nfc_tag_state == TagState::NotInitialized ||
        nfc->nfc_tag_state == TagState::NotScanning) {
        LOG_ERROR(Service_NFC, "Invalid TagState {}", static_cast<int>(nfc->nfc_tag_state));
        rb.Push(ResultCode(ErrCodes::CommandInvalidForState, ErrorModule::NFC,
                           ErrorSummary::InvalidState, ErrorLevel::Status));
        return;
    }

    nfc->nfc_tag_state = TagState::NotScanning;

    rb.Push(RESULT_SUCCESS);
    LOG_DEBUG(Service_NFC, "called");
}

void Module::Interface::LoadAmiiboData(Kernel::HLERequestContext& ctx) {
    IPC::RequestParser rp(ctx, 0x07, 0, 0);

    // TODO(FearlessTobi): Add state checking when this function gets properly implemented
    // The SHA256 check should be done here, and DataCorruption0/1 should be sent if it fails

    nfc->nfc_tag_state = TagState::TagDataLoaded;

    IPC::RequestBuilder rb = rp.MakeBuilder(1, 0);
    rb.Push(RESULT_SUCCESS);
    LOG_WARNING(Service_NFC, "(STUBBED) called");
}

void Module::Interface::ResetTagScanState(Kernel::HLERequestContext& ctx) {
    IPC::RequestParser rp(ctx, 0x08, 0, 0);

    IPC::RequestBuilder rb = rp.MakeBuilder(1, 0);
    if (nfc->nfc_tag_state != TagState::TagDataLoaded && nfc->nfc_tag_state != TagState::Unknown6) {
        LOG_ERROR(Service_NFC, "Invalid TagState {}", static_cast<int>(nfc->nfc_tag_state));
        rb.Push(ResultCode(ErrCodes::CommandInvalidForState, ErrorModule::NFC,
                           ErrorSummary::InvalidState, ErrorLevel::Status));
        return;
    }

    nfc->nfc_tag_state = TagState::TagInRange;
    nfc->SyncTagState();

    rb.Push(RESULT_SUCCESS);
    LOG_DEBUG(Service_NFC, "called");
}

void Module::Interface::UpdateStoredAmiiboData(Kernel::HLERequestContext& ctx) {
    IPC::RequestParser rp(ctx, 0x09, 0, 0);
    IPC::RequestBuilder rb = rp.MakeBuilder(1, 0);

    if (nfc->nfc_tag_state != TagState::TagDataLoaded)
        LOG_ERROR(Service_NFC, "NFC tag state was {} instead of {} on write request.",
                  nfc->nfc_tag_state, TagState::TagDataLoaded);
    else if (!nfc->amiibo_decrypted)
        LOG_ERROR(Service_NFC, "Tried to access with encrypted amiibo.");
    else if (nfc->amiibo_filename.empty())
        LOG_ERROR(Service_NFC, "Tried to use UpdateStoredAmiiboData on a nonexistant file.");
    else {
        std::lock_guard lock(HLE::g_hle_lock);
        FileUtil::IOFile amiibo_file(nfc->amiibo_filename, "wb");
        auto amiibo = reinterpret_cast<AmiiboDataDec*>(nfc->amiibo_data);

        /* Update metadata */
        amiibo->last_write_date = pack_date(11, 21, 2014); // TODO: Get the actual date.
        amiibo->write_count++;

        if (!amiibo_file.IsOpen())
            LOG_ERROR(Service_NFC, "Could not open amiibo file \"{}\"", nfc->amiibo_filename);
        else if (!amiibo_file.WriteBytes(amiibo, sizeof(AmiiboDataDec)))
            LOG_ERROR(Service_NFC, "Could not write to amiibo file \"{}\"", nfc->amiibo_filename);
        amiibo_file.Close();
        RemoveAmiibo();

        rb.Push(RESULT_SUCCESS);
        LOG_INFO(Service_NFC, "called");
        return;
    }
    rb.Push(ResultCode(ErrCodes::CommandInvalidForState, ErrorModule::NFC,
                       ErrorSummary::InvalidState, ErrorLevel::Status));
}

void Module::Interface::GetTagInRangeEvent(Kernel::HLERequestContext& ctx) {
    IPC::RequestParser rp(ctx, 0x0B, 0, 0);

    if (nfc->nfc_tag_state != TagState::NotScanning) {
        LOG_ERROR(Service_NFC, "Invalid TagState {}", static_cast<int>(nfc->nfc_tag_state));
        IPC::RequestBuilder rb = rp.MakeBuilder(1, 0);
        rb.Push(ResultCode(ErrCodes::CommandInvalidForState, ErrorModule::NFC,
                           ErrorSummary::InvalidState, ErrorLevel::Status));
        return;
    }

    IPC::RequestBuilder rb = rp.MakeBuilder(1, 2);
    rb.Push(RESULT_SUCCESS);
    rb.PushCopyObjects(nfc->tag_in_range_event);
    LOG_DEBUG(Service_NFC, "called");
}

void Module::Interface::GetTagOutOfRangeEvent(Kernel::HLERequestContext& ctx) {
    IPC::RequestParser rp(ctx, 0x0C, 0, 0);

    if (nfc->nfc_tag_state != TagState::NotScanning) {
        LOG_ERROR(Service_NFC, "Invalid TagState {}", static_cast<int>(nfc->nfc_tag_state));
        IPC::RequestBuilder rb = rp.MakeBuilder(1, 0);
        rb.Push(ResultCode(ErrCodes::CommandInvalidForState, ErrorModule::NFC,
                           ErrorSummary::InvalidState, ErrorLevel::Status));
        return;
    }

    IPC::RequestBuilder rb = rp.MakeBuilder(1, 2);
    rb.Push(RESULT_SUCCESS);
    rb.PushCopyObjects(nfc->tag_out_of_range_event);
    LOG_DEBUG(Service_NFC, "called");
}

void Module::Interface::GetTagState(Kernel::HLERequestContext& ctx) {
    IPC::RequestParser rp(ctx, 0x0D, 0, 0);

    IPC::RequestBuilder rb = rp.MakeBuilder(2, 0);
    rb.Push(RESULT_SUCCESS);
    rb.PushEnum(nfc->nfc_tag_state);
    LOG_DEBUG(Service_NFC, "called");
}

void Module::Interface::CommunicationGetStatus(Kernel::HLERequestContext& ctx) {
    IPC::RequestParser rp(ctx, 0x0F, 0, 0);

    IPC::RequestBuilder rb = rp.MakeBuilder(2, 0);
    rb.Push(RESULT_SUCCESS);
    rb.PushEnum(nfc->nfc_status);
    LOG_DEBUG(Service_NFC, "(STUBBED) called");
}

void Module::Interface::Unknown0x1A(Kernel::HLERequestContext& ctx) {
    IPC::RequestParser rp(ctx, 0x1A, 0, 0);

    IPC::RequestBuilder rb = rp.MakeBuilder(1, 0);
    if (nfc->nfc_tag_state != TagState::TagInRange) {
        LOG_ERROR(Service_NFC, "Invalid TagState {}", static_cast<int>(nfc->nfc_tag_state));
        rb.Push(ResultCode(ErrCodes::CommandInvalidForState, ErrorModule::NFC,
                           ErrorSummary::InvalidState, ErrorLevel::Status));
        return;
    }

    nfc->nfc_tag_state = TagState::Unknown6;

    rb.Push(RESULT_SUCCESS);
    LOG_DEBUG(Service_NFC, "called");
}

void Module::Interface::GetIdentificationBlock(Kernel::HLERequestContext& ctx) {
    IPC::RequestParser rp(ctx, 0x1B, 0, 0);

    if (nfc->nfc_tag_state != TagState::TagDataLoaded && nfc->nfc_tag_state != TagState::Unknown6) {
        LOG_ERROR(Service_NFC, "Invalid TagState {}", static_cast<int>(nfc->nfc_tag_state));
        IPC::RequestBuilder rb = rp.MakeBuilder(1, 0);
        rb.Push(ResultCode(ErrCodes::CommandInvalidForState, ErrorModule::NFC,
                           ErrorSummary::InvalidState, ErrorLevel::Status));
        return;
    }

    IdentificationBlockReply reply{};
    if (!nfc->amiibo_decrypted) {
        auto amiibo = reinterpret_cast<AmiiboDataEnc*>(nfc->amiibo_data);

        reply.char_id = amiibo->char_id;
        reply.char_variant = amiibo->char_variant;
        reply.series = amiibo->series;
        reply.model_number = amiibo->model_number;
        reply.figure_type = amiibo->figure_type;
    } else {
        auto amiibo = reinterpret_cast<AmiiboDataDec*>(nfc->amiibo_data);

        reply.char_id = amiibo->char_id;
        reply.char_variant = amiibo->char_variant;
        reply.series = amiibo->series;
        reply.model_number = amiibo->model_number;
        reply.figure_type = amiibo->figure_type;
    }

    IPC::RequestBuilder rb = rp.MakeBuilder(0x1F, 0);
    rb.Push(RESULT_SUCCESS);
    rb.PushRaw<IdentificationBlockReply>(reply);
    LOG_DEBUG(Service_NFC, "called");
}

std::shared_ptr<Module> Module::Interface::GetModule() const {
    return nfc;
}

bool Module::Interface::LoadAmiibo(const std::string& fullpath) {
    std::lock_guard lock(HLE::g_hle_lock);
    AmiiboDataDec amiibo_data;
    FileUtil::IOFile amiibo_file(fullpath, "rb");
    bool success = false;

    if (!amiibo_file.IsOpen())
        LOG_ERROR(Service_NFC, "Could not open amiibo file \"{}\"", fullpath);
    else if (!amiibo_file.ReadBytes(&amiibo_data, sizeof(amiibo_data)))
        LOG_ERROR(Service_NFC, "Could not read amiibo data from file \"{}\"", fullpath);
    else {
        /*
           TODO: This is a naive check that should tell us if an amiibo dump is
           encrypted or not, but may give a rare false positive.
        */
        nfc->amiibo_decrypted = amiibo_data.unknown3 == 0x02;
        nfc->amiibo_filename = fullpath;
        nfc->amiibo_in_range = true;
        std::memcpy(nfc->amiibo_data, &amiibo_data, sizeof(amiibo_data));
        nfc->SyncTagState();

        LOG_INFO(Service_NFC, "Loaded {} amiibo from {}.",
                 nfc->amiibo_decrypted ? "a decrypted" : "an encrypted", fullpath);

        success = true;
    }
    amiibo_file.Close();

    return success;
}

void Module::Interface::RemoveAmiibo() {
    std::lock_guard lock(HLE::g_hle_lock);
    nfc->amiibo_in_range = false;
    nfc->SyncTagState();
}

void Module::SyncTagState() {
    if (amiibo_in_range &&
        (nfc_tag_state == TagState::TagOutOfRange || nfc_tag_state == TagState::Scanning)) {
        // TODO (wwylele): Should TagOutOfRange->TagInRange transition only happen on the same tag
        // detected on Scanning->TagInRange?
        nfc_tag_state = TagState::TagInRange;
        tag_in_range_event->Signal();
    } else if (!amiibo_in_range &&
               (nfc_tag_state == TagState::TagInRange || nfc_tag_state == TagState::TagDataLoaded ||
                nfc_tag_state == TagState::Unknown6)) {
        // TODO (wwylele): If a tag is removed during TagDataLoaded/Unknown6, should this event
        // signals early?
        nfc_tag_state = TagState::TagOutOfRange;
        tag_out_of_range_event->Signal();
    }
}

Module::Interface::Interface(std::shared_ptr<Module> nfc, const char* name, u32 max_session)
    : ServiceFramework(name, max_session), nfc(std::move(nfc)) {}

Module::Interface::~Interface() = default;

Module::Module(Core::System& system) {
    tag_in_range_event =
        system.Kernel().CreateEvent(Kernel::ResetType::OneShot, "NFC::tag_in_range_event");
    tag_out_of_range_event =
        system.Kernel().CreateEvent(Kernel::ResetType::OneShot, "NFC::tag_out_range_event");
}

Module::~Module() = default;

void InstallInterfaces(Core::System& system) {
    auto& service_manager = system.ServiceManager();
    auto nfc = std::make_shared<Module>(system);
    std::make_shared<NFC_M>(nfc)->InstallAsService(service_manager);
    std::make_shared<NFC_U>(nfc)->InstallAsService(service_manager);
}

} // namespace Service::NFC
