// Copyright 2016 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <atomic>
#include <memory>
#include <boost/serialization/binary_object.hpp>
#include "common/common_types.h"
#include "core/hle/service/service.h"

namespace Core {
class System;
}

namespace Kernel {
class Event;
} // namespace Kernel

namespace Service::NFC {

/*
  All error codes use ErrorSummary::InvalidState and
  ErrorLevel::Status except one note below
*/
namespace ErrCodes {
enum {
    CommandInvalidForState = 512,
    AppDataUninitialized = 544,
    AmiiboNotSetup = 552,
    AppIdMismatch = 568,
    DataCorruption0 = 524, // ErrorSummary::NotSupported
    DataCorruption1 = 536
};
} // namespace ErrCodes

#pragma pack(push, 1)

/*
  An encrypted amiibo. Functionality before decryption should only
  include recognizing the character.

  TODO(FearlessTobi): Add more members to this struct
*/
struct AmiiboDataEnc {
    std::array<u8, 7> uuid;
    INSERT_PADDING_BYTES(0x4D);
    u16_le char_id;
    u8 char_variant;
    u8 figure_type;
    u16_be model_number;
    u8 series;
    INSERT_PADDING_BYTES(0x1C1);
};
static_assert(sizeof(AmiiboDataEnc) == 0x21C,
              "AmiiboDataEnc is an invalid size");

/*
  A decrypted amiibo. Functionality includes reading/writing nickname,
  author's Mii, and game data.
*/
struct AmiiboDataDec {
    /*00  2B*/ std::array<u8, 0x2B> dummy1;
    /*2B  01*/ u8 pagex4_byte3;
    /*2C  01*/ u8 flags;
    /*2D  01*/ u8 country;
    /*2E  02*/ u16_be crc_mismatch_count;
    /*30  02*/ u16_be setup_date;
    /*32  02*/ u16_be last_write_date;
    /*34  04*/ u32_be crc32;
    /*38  14*/ std::array<u16_be, 10> nickname;
    /*4C  60*/ std::array<u8, 0x60> mii;
    /*AC  08*/ u64_be title_id;
    /*B4  02*/ u16_be write_count;
    /*B6  04*/ u32_be app_id;
    /*BA  02*/ u16_be unknown1;
    /*BC  20*/ std::array<u8, 0x20> hmac_sha256;
    /*DC  D8*/ std::array<u8, 0xD8> app_data;
    /*1B4 20*/ std::array<u8, 0x20> dummy2;
    /*1D4 07*/ std::array<u8, 7> uuid;
    /*1DB 01*/ u8 unknown2;
    /*1DC 02*/ u16_le char_id;
    /*1DE 01*/ u8 char_variant;
    /*1DF 01*/ u8 figure_type;
    /*1E0 02*/ u16_be model_number;
    /*1E2 01*/ u8 series;
    /*1E3 01*/ u8 unknown3; // always 02?
    /*1E4 38*/ std::array<u8, 0x38> dummy3;
};
static_assert(sizeof(AmiiboDataDec) == 0x21C,
              "AmiiboDataDec is an invalid size");

struct AmiiboSettings {
    std::array<u8, 0x60> mii;
    std::array<u16, 11> nickname;
    u8 flags;
    u8 country;
    u16 setup_year;
    u8 setup_month;
    u8 setup_day;
    std::array<u8, 0x2C> dummy;
};
static_assert(sizeof(AmiiboSettings) == 0xA8,
              "AmiiboSettings is an invalid size");

struct AmiiboWriteRequest {
    u8 uuid[0x07];
    u16 unknown1;
    u8 uuid_length;
    u8 unknown2[0x15];
};
static_assert(sizeof(AmiiboWriteRequest) == 0x1F,
              "AmiiboWriteRequest is an invalid size");

#pragma pack(pop)

enum class TagState : u8 {
    NotInitialized = 0,
    NotScanning = 1,
    Scanning = 2,
    TagInRange = 3,
    TagOutOfRange = 4,
    TagDataLoaded = 5,
    Unknown6 = 6,
};

enum class CommunicationStatus : u8 {
    NotInitialized = 0,
    AttemptInitialize = 1,
    Initialized = 2,
};

class Module final {
public:
    explicit Module(Core::System& system);
    ~Module();

    class Interface : public ServiceFramework<Interface> {
    public:
        Interface(std::shared_ptr<Module> nfc, const char* name, u32 max_session);
        ~Interface();

        std::shared_ptr<Module> GetModule() const;

        bool LoadAmiibo(const std::string& fullpath);

        void RemoveAmiibo();

    protected:
        /**
         * NFC::Initialize service function
         *  Inputs:
         *      0 : Header code [0x00010040]
         *      1 : (u8) unknown parameter. Can be either value 0x1 or 0x2
         *  Outputs:
         *      1 : Result of function, 0 on success, otherwise error code
         */
        void Initialize(Kernel::HLERequestContext& ctx);

        /**
         * NFC::Shutdown service function
         *  Inputs:
         *      0 : Header code [0x00020040]
         *      1 : (u8) unknown parameter
         *  Outputs:
         *      1 : Result of function, 0 on success, otherwise error code
         */
        void Shutdown(Kernel::HLERequestContext& ctx);

        /**
         * NFC::StartCommunication service function
         *  Inputs:
         *      0 : Header code [0x00030000]
         *  Outputs:
         *      1 : Result of function, 0 on success, otherwise error code
         */
        void StartCommunication(Kernel::HLERequestContext& ctx);

        /**
         * NFC::StopCommunication service function
         *  Inputs:
         *      0 : Header code [0x00040000]
         *  Outputs:
         *      1 : Result of function, 0 on success, otherwise error code
         */
        void StopCommunication(Kernel::HLERequestContext& ctx);

        /**
         * NFC::StartTagScanning service function
         *  Inputs:
         *      0 : Header code [0x00050040]
         *      1 : (u16) unknown. This is normally 0x0
         *  Outputs:
         *      1 : Result of function, 0 on success, otherwise error code
         */
        void StartTagScanning(Kernel::HLERequestContext& ctx);

        /**
         * NFC::StopTagScanning service function
         *  Inputs:
         *      0 : Header code [0x00060000]
         *  Outputs:
         *      1 : Result of function, 0 on success, otherwise error code
         */
        void StopTagScanning(Kernel::HLERequestContext& ctx);

        /**
         * NFC::LoadAmiiboData service function
         *  Inputs:
         *      0 : Header code [0x00070000]
         *  Outputs:
         *      1 : Result of function, 0 on success, otherwise error code
         */
        void LoadAmiiboData(Kernel::HLERequestContext& ctx);

        /**
         * NFC::ResetTagScanState service function
         *  Inputs:
         *      0 : Header code [0x00080000]
         *  Outputs:
         *      1 : Result of function, 0 on success, otherwise error code
         */
        void ResetTagScanState(Kernel::HLERequestContext& ctx);

        /**
         * NFC::UpdateStoredAmiiboData service function
         *  Inputs:
         *      0 : Header code [0x00090002]
         *  Outputs:
         *      1 : Result of function, 0 on success, otherwise error code
         */
        void UpdateStoredAmiiboData(Kernel::HLERequestContext& ctx);

        /**
         * NFC::GetTagInRangeEvent service function
         *  Inputs:
         *      0 : Header code [0x000B0000]
         *  Outputs:
         *      1 : Result of function, 0 on success, otherwise error code
         *      2 : Copy handle descriptor
         *      3 : Event Handle
         */
        void GetTagInRangeEvent(Kernel::HLERequestContext& ctx);

        /**
         * NFC::GetTagOutOfRangeEvent service function
         *  Inputs:
         *      0 : Header code [0x000C0000]
         *  Outputs:
         *      1 : Result of function, 0 on success, otherwise error code
         *      2 : Copy handle descriptor
         *      3 : Event Handle
         */
        void GetTagOutOfRangeEvent(Kernel::HLERequestContext& ctx);

        /**
         * NFC::GetTagState service function
         *  Inputs:
         *      0 : Header code [0x000D0000]
         *  Outputs:
         *      1 : Result of function, 0 on success, otherwise error code
         *      2 : (u8) Tag state
         */
        void GetTagState(Kernel::HLERequestContext& ctx);

        /**
         * NFC::CommunicationGetStatus service function
         *  Inputs:
         *      0 : Header code [0x000F0000]
         *  Outputs:
         *      1 : Result of function, 0 on success, otherwise error code
         *      2 : (u8) Communication state
         */
        void CommunicationGetStatus(Kernel::HLERequestContext& ctx);

        /**
         * NFC::GetTagInfo service function
         *  Inputs:
         *      0 : Header code [0x00110000]
         *  Outputs:
         *      1 : Result of function, 0 on success, otherwise error code
         *   2-12 : 0x2C-byte struct
         */
        void GetTagInfo(Kernel::HLERequestContext& ctx);

        /**
         * NFC::OpenAppData service function
         *  Inputs:
         *      0 : Header code [0x00130040]
         *      1 : (u32) App ID
         *  Outputs:
         *      1 : Result of function, 0 on success, otherwise error code
         */
        void OpenAppData(Kernel::HLERequestContext& ctx);

        /**
         * NFC::InitializeWriteAppData service function
         *  Inputs:
         *      0 : Header code [0x00140384]
         *      1 : (u32) App ID
         *      2 : Size
         *   3-14 : 0x30-byte zeroed-out struct
         *     15 : 0x20, PID translate-header for kernel
         *     16 : PID written by kernel
         *     17 : (Size << 14) | 2
         *     18 : Pointer to input buffer
         *  Outputs:
         *      1 : Result of function, 0 on success, otherwise error code
         */
        void InitializeWriteAppData(Kernel::HLERequestContext& ctx);

        /**
         * NFC::ReadAppData service function
         *  Inputs:
         *      0 : Header code [0x00150040]
         *      1 : Size (unused? Hard-coded to be 0xD8)
         *  Outputs:
         *      1 : Result of function, 0 on success, otherwise error code
         */
        void ReadAppData(Kernel::HLERequestContext& ctx);

        /**
         * NFC::WriteAppData service function
         *  Inputs:
         *      0 : Header code [0x00160242]
         *      1 : Size
         *    2-9 : AmiiboWriteRequest struct (see above)
         *     10 : (Size << 14) | 2
         *     11 : Pointer to input appdata buffer
         *  Outputs:
         *      1 : Result of function, 0 on success, otherwise error code
         */
        void WriteAppData(Kernel::HLERequestContext& ctx);

        /**
         * NFC::GetAmiiboSettings service function
         *  Inputs:
         *      0 : Header code [0x00170000]
         *  Outputs:
         *      1 : Result of function, 0 on success, otherwise error code
         *   2-43 : AmiiboSettings struct (see above)
         */
        void GetAmiiboSettings(Kernel::HLERequestContext& ctx);

        /**
         * NFC::GetAmiiboConfig service function
         *  Inputs:
         *      0 : Header code [0x00180000]
         *  Outputs:
         *      1 : Result of function, 0 on success, otherwise error code
         *   2-17 : 0x40-byte config struct
         */
        void GetAmiiboConfig(Kernel::HLERequestContext& ctx);

        /**
         * NFC::Unknown0x1A service function
         *  Inputs:
         *      0 : Header code [0x001A0000]
         *  Outputs:
         *      1 : Result of function, 0 on success, otherwise error code
         */
        void Unknown0x1A(Kernel::HLERequestContext& ctx);

        /**
         * NFC::GetIdentificationBlock service function
         *  Inputs:
         *      0 : Header code [0x001B0000]
         *  Outputs:
         *      1 : Result of function, 0 on success, otherwise error code
         *   2-31 : 0x36-byte struct
         */
        void GetIdentificationBlock(Kernel::HLERequestContext& ctx);

    protected:
        std::shared_ptr<Module> nfc;
    };

private:
    // Sync nfc_tag_state with amiibo_in_range and signal events on state change.
    void SyncTagState();

    std::shared_ptr<Kernel::Event> tag_in_range_event;
    std::shared_ptr<Kernel::Event> tag_out_of_range_event;
    TagState nfc_tag_state = TagState::NotInitialized;
    CommunicationStatus nfc_status = CommunicationStatus::Initialized;

    u8 amiibo_data[sizeof(AmiiboDataDec)];
    bool amiibo_decrypted = false;
    std::string amiibo_filename = "";
    bool amiibo_in_range = false;

    template <class Archive>
    void serialize(Archive& ar, const unsigned int);
    friend class boost::serialization::access;
};

void InstallInterfaces(Core::System& system);

} // namespace Service::NFC

SERVICE_CONSTRUCT(Service::NFC::Module)
BOOST_CLASS_EXPORT_KEY(Service::NFC::Module)
