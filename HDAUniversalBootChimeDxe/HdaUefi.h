// HDAUniversalBootChimeDxe
// Copyright © 2026 DNMTechLabs
// Contact: dnmtechlabs@gmail.com
// Boot chime DXE driver for Clover/OpenCore.

#ifndef HDA_UNIVERSAL_BOOT_CHIME_DXE_H
#define HDA_UNIVERSAL_BOOT_CHIME_DXE_H

#include <Uefi.h>
#include <PiDxe.h>
#include <Library/UefiLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/DebugLib.h>
#include <Library/BaseLib.h>
#include <Library/PrintLib.h>
#include <Library/UefiRuntimeServicesTableLib.h>
#include <Protocol/PciIo.h>
#include <Protocol/LoadedImage.h>
#include <Protocol/SimpleFileSystem.h>
#include <Protocol/SimpleTextInEx.h>
#include <IndustryStandard/Pci.h>
#include <Guid/EventGroup.h>

#define HDAU_CHIME_VERSION            L"1.0.87-final-audit"
#define HDAU_CHIME_VERSION_ASCII      "1.0.87-final-audit"
#define HDAU_CHIME_NAME               L"HDAUniversalBootChimeDxe"

#define HDA_PCI_CLASS_MULTIMEDIA      0x04
#define HDA_PCI_SUBCLASS_HD_AUDIO     0x03

#define HDA_REG_GCAP                  0x00
#define HDA_REG_VMIN                  0x02
#define HDA_REG_VMAJ                  0x03
#define HDA_REG_OUTPAY                0x04
#define HDA_REG_INPAY                 0x06
#define HDA_REG_GCTL                  0x08
#define HDA_REG_WAKEEN                0x0C
#define HDA_REG_STATESTS              0x0E
#define HDA_REG_GSTS                  0x10
#define HDA_REG_OUTSTRMPAY            0x18
#define HDA_REG_INSTRMPAY             0x1A
#define HDA_REG_INTCTL                0x20
#define HDA_REG_INTSTS                0x24
#define HDA_REG_WALCLK                0x30
#define HDA_REG_SSYNC                 0x38
#define HDA_REG_CORBLBASE             0x40
#define HDA_REG_CORBUBASE             0x44
#define HDA_REG_CORBWP                0x48
#define HDA_REG_CORBRP                0x4A
#define HDA_REG_CORBCTL               0x4C
#define HDA_REG_CORBSTS               0x4D
#define HDA_REG_CORBSIZE              0x4E
#define HDA_REG_RIRBLBASE             0x50
#define HDA_REG_RIRBUBASE             0x54
#define HDA_REG_RIRBWP                0x58
#define HDA_REG_RINTCNT               0x5A
#define HDA_REG_RIRBCTL               0x5C
#define HDA_REG_RIRBSTS               0x5D
#define HDA_REG_RIRBSIZE              0x5E
#define HDA_REG_ICOI                  0x60
#define HDA_REG_ICII                  0x64
#define HDA_REG_ICIS                  0x68
#define HDA_REG_DPLBASE               0x70
#define HDA_REG_DPUBASE               0x74
#define HDA_REG_SD_BASE               0x80
#define HDA_SD_SIZE                   0x20

#define HDA_GCAP_BSS_SHIFT            3
#define HDA_GCAP_BSS_MASK             0x1F
#define HDA_GCAP_64OK                 BIT0
#define HDA_DMA_ADDRESS_ALIGNMENT     128U
#define HDA_GCTL_CRST                 BIT0
#define HDA_ICIS_ICB                  BIT0
#define HDA_ICIS_IRV                  BIT1

#define HDA_CORBCTL_CMEIE             BIT0
#define HDA_CORBCTL_DMA_ENABLE        BIT1
#define HDA_CORBCTL_RSVDP_MASK        0xFCU
#define HDA_RIRBCTL_RINTCTL           BIT0
#define HDA_RIRBCTL_DMA_ENABLE        BIT1
#define HDA_RIRBCTL_RIRBOIC           BIT2
#define HDA_RIRBCTL_RSVDP_MASK        0xF8U
#define HDA_CORBWP_RSVDP_MASK         0xFF00U
#define HDA_CORBRP_RSVDP_MASK         0x7F00U
#define HDA_CORBRP_RESET              BIT15
#define HDA_RIRBWP_RSVDP_MASK         0x7F00U
#define HDA_RIRBWP_RESET              BIT15
#define HDA_RINTCNT_RSVDP_MASK        0xFF00U
#define HDA_CORBSTS_CMEI              BIT0
#define HDA_RIRBSTS_RINTFL            BIT0
#define HDA_RIRBSTS_RIRBOIS           BIT2
#define HDA_RIRB_RESPONSE_CODEC_MASK  0x0FU
#define HDA_RIRB_RESPONSE_UNSOLICITED BIT4
#define HDA_RING_SIZE_CAP_2            BIT4
#define HDA_RING_SIZE_CAP_16           BIT5
#define HDA_RING_SIZE_CAP_256          BIT6
#define HDA_RING_SIZE_CAP_MASK         (BIT4 | BIT5 | BIT6)
#define HDA_RING_SIZE_SELECT_MASK      0x03U
#define HDA_RING_SIZE_SELECT_2         0x00U
#define HDA_RING_SIZE_SELECT_16        0x01U
#define HDA_RING_SIZE_SELECT_256       0x02U

#define HDA_SD_CTL                    0x00
#define HDA_SD_STS                    0x03
#define HDA_SD_LPIB                   0x04
#define HDA_SD_CBL                    0x08
#define HDA_SD_LVI                    0x0C
#define HDA_SD_FMT                    0x12
#define HDA_SD_BDLPL                  0x18
#define HDA_SD_BDLPU                  0x1C
#define HDA_SD_CTL_SRST               BIT0
#define HDA_SD_CTL_RUN                BIT1
#define HDA_SD_CTL_IOCE               BIT2
#define HDA_SD_CTL_FEIE               BIT3
#define HDA_SD_CTL_DEIE               BIT4
#define HDA_SD_CTL3_DIR               BIT3  /* SDnCTL bit 19; set for output on bidirectional descriptors */
#define HDA_SD_CTL_RSVDP_LOW_MASK     0xE0U
#define HDA_SD_CTL_INTERRUPT_MASK     0x1CU
#define HDA_SD_STS_ALL                0x1C
#define HDA_SD_STS_BCIS               BIT2
#define HDA_SD_STS_FIFOE              BIT3
#define HDA_SD_STS_DESE               BIT4
#define HDA_SD_STS_FIFORDY            BIT5
#define HDA_SD_STS_OWNERSHIP_MASK     (HDA_SD_STS_ALL | HDA_SD_STS_FIFORDY)
#define HDA_SD_LVI_RSVDP_MASK         0xFF00U
#define HDA_SD_LVI_VALUE_MASK         0x00FFU
#define HDA_SD_FMT_PRESERVE_MASK      (BIT15 | BIT7)
#define HDA_SD_BDLPL_RSVDZ_MASK       0x7FU

#define HDA_FMT_48K_16B_MONO          0x0010
#define HDA_FMT_48K_16B_STEREO        0x0011
#define HDA_FMT_44K_16B_MONO          0x4010
#define HDA_FMT_44K_16B_STEREO        0x4011
#define HDA_STREAM_TAG                1

#define HDA_PARAM_VENDOR_ID           0x00
#define HDA_PARAM_REV_ID              0x02
#define HDA_PARAM_NODE_COUNT          0x04
#define HDA_PARAM_FUNCTION_TYPE       0x05
#define HDA_PARAM_AUDIO_WIDGET_CAP    0x09
#define HDA_PARAM_PCM                 0x0A

#define HDA_PARAM_AFG_CAP              0x08
#define HDA_PARAM_SUPPORTED_PCM        HDA_PARAM_PCM
#define HDA_PARAM_SUPPORTED_STREAM     0x0B
#define HDA_PARAM_AMP_IN_CAP           0x0D
#define HDA_PARAM_POWER_STATES         0x0F
#define HDA_PARAM_PROCESSING_CAP       0x10
#define HDA_PARAM_GPIO_CAP             0x11
#define HDA_PARAM_VOL_KNB_CAP          0x13
#define HDA_PARAM_HDMI_LIPSYNC_CAP     0x14
#define HDA_PARAM_PIN_CAP             0x0C
#define HDA_PARAM_AMP_OUT_CAP         0x12
#define HDA_PARAM_CONNECTION_LIST_LEN 0x0E

#define HDA_WIDGET_TYPE_AUDIO_OUT     0x0
#define HDA_WIDGET_TYPE_AUDIO_IN      0x1
#define HDA_WIDGET_TYPE_AUDIO_MIXER   0x2
#define HDA_WIDGET_TYPE_AUDIO_SELECTOR 0x3
#define HDA_WIDGET_TYPE_PIN_COMPLEX   0x4
#define HDA_WIDGET_TYPE_POWER         0x5
#define HDA_WIDGET_TYPE_VOLUME_KNOB   0x6
#define HDA_WIDGET_TYPE_BEEP          0x7
#define HDA_WIDGET_TYPE_VENDOR        0xF

/* Audio Widget Capabilities bits used to avoid unsupported amplifier verbs. */
#define HDA_AWCAP_STEREO               BIT0
#define HDA_AWCAP_IN_AMP_PRESENT       BIT1
#define HDA_AWCAP_OUT_AMP_PRESENT      BIT2
#define HDA_AWCAP_AMP_OVERRIDE         BIT3
#define HDA_AWCAP_FORMAT_OVERRIDE      BIT4
#define HDA_AWCAP_CONNECTION_LIST      BIT8
#define HDA_AWCAP_DIGITAL              BIT9
#define HDA_AWCAP_POWER_CONTROL        BIT10

#define HDA_PIN_CAP_IMPEDANCE_SENSE   BIT0
#define HDA_PIN_CAP_TRIGGER_REQUIRED  BIT1
#define HDA_PIN_CAP_PRESENCE_DETECT   BIT2
#define HDA_PIN_CAP_HEADPHONE         BIT3
#define HDA_PIN_CAP_OUTPUT            BIT4
#define HDA_PIN_CAP_HDMI              BIT7
#define HDA_PIN_CAP_EAPD              BIT16
#define HDA_PIN_CAP_DISPLAYPORT       BIT24
#define HDA_PIN_CTL_OUT_ENABLE        BIT6
#define HDA_PIN_CTL_HEADPHONE_ENABLE  BIT7

#define HDA_DEFAULT_DEVICE_LINE_OUT   0x0
#define HDA_DEFAULT_DEVICE_SPEAKER    0x1
#define HDA_DEFAULT_DEVICE_HP_OUT     0x2
#define HDA_DEFAULT_DEVICE_SPDIF_OUT  0x4
#define HDA_DEFAULT_DEVICE_DIGITAL_OUT 0x5
#define HDA_DEFAULT_DEVICE_OTHER      0xF

#define HDA_DEFAULT_CONN_UNKNOWN      0x0
#define HDA_DEFAULT_CONN_1_8_JACK     0x1
#define HDA_DEFAULT_CONN_OPTICAL      0x5
#define HDA_DEFAULT_CONN_DIGITAL      0x6
#define HDA_DEFAULT_CONN_ATAPI        0x3

#define HDA_VERB_GET_PARAMETER        0xF00
#define HDA_VERB_GET_CONNECTION_ENTRY 0xF02
#define HDA_VERB_GET_PIN_SENSE        0xF09
#define HDA_VERB_GET_PROCESSING_STATE 0xF03
#define HDA_VERB_GET_DIGI_CONVERT_1   0xF0D
#define HDA_VERB_GET_HDMI_DIP_SIZE    0xF2E
#define HDA_VERB_GET_HDMI_ELD_DATA    0xF2F
#define HDA_VERB_GET_HDMI_DIP_INDEX   0xF30
#define HDA_VERB_GET_HDMI_DIP_DATA    0xF31
#define HDA_VERB_GET_HDMI_DIP_XMIT    0xF32
#define HDA_VERB_GET_HDMI_CHAN_SLOT   0xF34
#define HDA_VERB_GET_CONFIG_DEFAULT   0xF1C
#define HDA_VERB_SET_CONFIG_DEFAULT_0 0x71C
#define HDA_VERB_SET_CONFIG_DEFAULT_1 0x71D
#define HDA_VERB_SET_CONFIG_DEFAULT_2 0x71E
#define HDA_VERB_SET_CONFIG_DEFAULT_3 0x71F

#define HDA_VERB_GET_CONNECTION_SELECT 0xF01
#define HDA_VERB_GET_POWER_STATE      0xF05
#define HDA_VERB_GET_PIN_WIDGET_CONTROL 0xF07
#define HDA_VERB_GET_UNSOLICITED_RESPONSE 0xF08
#define HDA_VERB_GET_EAPD_BTL         0xF0C
#define HDA_VERB_GET_GPIO_DATA        0xF15
#define HDA_VERB_GET_GPIO_ENABLE_MASK 0xF16
#define HDA_VERB_GET_GPIO_DIRECTION   0xF17
#define HDA_VERB_GET_GPIO_WAKE_ENABLE_MASK 0xF18
#define HDA_VERB_GET_BEEP_GENERATION  0xF0A
#define HDA_VERB_GET_VOLUME_KNOB_CONTROL 0xF0F
#define HDA_VERB_GET_SUBSYSTEM_ID     0xF20
#define HDA_VERB_GET_CONVERTER_FORMAT 0xA
#define HDA_VERB_GET_CONVERTER_STREAM_CHANNEL 0xF06
#define HDA_VERB_SET_CONNECTION_SELECT 0x701
#define HDA_VERB_SET_PROCESSING_STATE  0x703
#define HDA_VERB_SET_POWER_STATE      0x705
#define HDA_VERB_SET_CONVERTER_STREAM_CHANNEL 0x706
#define HDA_VERB_SET_PIN_WIDGET_CONTROL 0x707
#define HDA_VERB_SET_UNSOLICITED_RESPONSE 0x708
#define HDA_VERB_SET_PIN_SENSE          0x709
#define HDA_VERB_SET_EAPD_BTL         0x70C
#define HDA_VERB_SET_DIGI_CONVERT_1    0x70D
#define HDA_VERB_SET_HDMI_DIP_INDEX    0x730
#define HDA_VERB_SET_HDMI_DIP_DATA     0x731
#define HDA_VERB_SET_HDMI_DIP_XMIT     0x732
#define HDA_VERB_SET_HDMI_CHAN_SLOT    0x734
#define HDA_VERB_SET_GPIO_DATA        0x715
#define HDA_VERB_SET_GPIO_ENABLE_MASK 0x716
#define HDA_VERB_SET_GPIO_DIRECTION   0x717
#define HDA_VERB_SET_GPIO_WAKE_ENABLE_MASK 0x718
#define HDA_VERB_SET_BEEP_GENERATION  0x70A
#define HDA_VERB_SET_VOLUME_KNOB_CONTROL 0x70F
#define HDA_VERB_SET_SUBSYSTEM_ID_0   0x720
#define HDA_VERB_SET_SUBSYSTEM_ID_1   0x721
#define HDA_VERB_SET_SUBSYSTEM_ID_2   0x722
#define HDA_VERB_SET_SUBSYSTEM_ID_3   0x723
#define HDA_VERB_SET_COEF_INDEX       0x5
#define HDA_VERB_GET_PROC_COEF        0xC
#define HDA_VERB_GET_COEF_INDEX       0xD
#define HDA_VERB_SET_PROC_COEF        0x4
#define HDA_VERB_SET_CONVERTER_FORMAT 0x2
#define HDA_VERB_SET_AMP_GAIN_MUTE    0x3
#define HDA_VERB_GET_AMP_GAIN_MUTE    0xB

#define HDA_VERB12(_Verb, _Payload)   (((_Verb) << 8) | ((_Payload) & 0xFF))
#define HDA_VERB4(_Verb, _Payload)    (((_Verb) << 16) | ((_Payload) & 0xFFFF))

#define HDA_AMP_INDEX_MAX             0x0F
#define HDA_PCM_RATE_44K               BIT5
#define HDA_PCM_RATE_48K               BIT6
#define HDA_PCM_BITS_16                0x00020000U
#define HDA_STREAM_FORMAT_PCM          BIT0
#define HDA_DIGI_ENABLE                 BIT0
#define HDA_PIN_SENSE_ELD_VALID         BIT30
#define HDA_PIN_SENSE_PRESENCE          BIT31
#define HDA_HDMI_DIP_XMIT_BEST_EFFORT   0xC0U

#define HDAU_MAX_CODECS               15
#define HDAU_MAX_WIDGETS              (HDAU_MAX_NID + 1U)
#define HDAU_MAX_PATH_DEPTH           (HDAU_MAX_NID + 1U)
#define HDAU_MAX_CONNECTIONS          HDAU_MAX_WIDGETS
#define HDAU_SAMPLE_RATE              48000
#define HDAU_BOOT_CHIME_VOLUME_PERCENT 30U
#define HDAU_BOOT_CHIME_GAIN_Q8       ((HDAU_BOOT_CHIME_VOLUME_PERCENT * 256U + 50U) / 100U) /* nearest Q8 policy value; PCM scaling below remains exact 30% */
#define HDAU_PCM_TRANSPORT_GAIN_Q8     HDAU_BOOT_CHIME_GAIN_Q8 /* PCM/DMA can never exceed the global 30% ceiling */
#define HDAU_FORCE_CODEC_NONE         0xFFU
#define HDAU_FORCE_NID_NONE           0x100U
#define HDAU_MAX_CONFIG_FILE_BYTES    65536
#define HDAU_MAX_LOG_FILE_BYTES       (4U * 1024U * 1024U)
#define HDAU_MAX_D_POLL_MS            30000
#define HDAU_MAX_INPUT_EVENTS_PER_POLL 64U
#define HDAU_MAX_F6_POLL_MS           HDAU_MAX_D_POLL_MS /* legacy config alias */
#define HDAU_MAX_STALL_MS             10000
#define HDAU_MAX_NID                  0x7FU
#define HDAU_ALCID_PROFILE_PAIR_COUNT 12U
#define HDAU_MAX_ROUTE_CANDIDATES      24U
#define HDAU_MAX_SNAPSHOT_NODES        64U
#define HDAU_MAX_COEF_SNAPSHOTS        64U
#define HDAU_MAX_COEF_INDEX_SNAPSHOTS  64U
#define HDAU_BDL_ENTRY_COUNT            2U
#define HDAU_EAPD_STABILIZE_US         85000U
#define HDAU_CORB_RIRB_TIMEOUT_US      30000U
#define HDAU_CORB_RIRB_POLL_US         10U
#define HDAU_MAX_VOLUME_Q8            HDAU_BOOT_CHIME_GAIN_Q8
#define HDAU_MAX_AUDIO_BUFFER_BYTES   (16U * 1024U * 1024U)
#define HDAU_DEFAULT_DEBUG_STALL_MS   0
#define HDAU_DEFAULT_D_POLL_MS        0
#define HDAU_DEFAULT_F6_POLL_MS       HDAU_DEFAULT_D_POLL_MS /* legacy config alias */
#define HDAU_DUMP_KEY_LOWER           L'd'
#define HDAU_DUMP_KEY_UPPER           L'D'
#define HDAU_ASYNC_PLAYBACK_TICK_US   20000
#define HDAU_RESET_ASSERT_TIMEOUT_US  10000
#define HDAU_RESET_RELEASE_TIMEOUT_US 30000
#define HDAU_STREAM_RESET_TIMEOUT_US  10000
#define HDAU_STREAM_STOP_TIMEOUT_US   5000
#define HDAU_IMMEDIATE_BUSY_TIMEOUT_US 10000
#define HDAU_IMMEDIATE_RESPONSE_TIMEOUT_US 20000
#define HDAU_IMMEDIATE_POLL_US       10
#define HDAU_CODEC_WAKE_TIMEOUT_US    10000
#define HDAU_WIDGET_POWER_TIMEOUT_US  HDAU_CODEC_WAKE_TIMEOUT_US
#define HDAU_CODEC_POST_RESET_MIN_US  521
#define HDAU_POLL_GRANULARITY_US      100
#define HDAU_CHANNELS                 2
#define HDAU_BYTES_PER_SAMPLE         2
#define HDAU_BYTES_PER_SECOND         ((UINT64)HDAU_SAMPLE_RATE * (UINT64)HDAU_CHANNELS * (UINT64)HDAU_BYTES_PER_SAMPLE)
#define HDAU_TRAILING_SILENCE_MS      3000
#define HDAU_FASTBOOT_SILENCE_MS      15000  /* async mode safety tail; keeps picker fast without BDL wrap */
#define HDAU_TRAILING_SILENCE_BYTES   ((UINTN)((HDAU_BYTES_PER_SECOND * (UINT64)HDAU_TRAILING_SILENCE_MS) / 1000ULL))
#define HDAU_FASTBOOT_SILENCE_BYTES   ((UINTN)((HDAU_BYTES_PER_SECOND * (UINT64)HDAU_FASTBOOT_SILENCE_MS) / 1000ULL))
#define HDAU_POST_CHIME_DRAIN_MS      3000
#define HDAU_WAIT_CHIME_DRAIN_MS      700
#define HDAU_ASYNC_HARD_TIMEOUT_MS    22000
#define HDAU_ASYNC_NO_PROGRESS_TIMEOUT_MS 6000
#define HDAU_POST_CHIME_DRAIN_BYTES   ((UINT32)((HDAU_BYTES_PER_SECOND * (UINT64)HDAU_POST_CHIME_DRAIN_MS) / 1000ULL))
#define HDAU_WAIT_CHIME_DRAIN_BYTES   ((UINT32)((HDAU_BYTES_PER_SECOND * (UINT64)HDAU_WAIT_CHIME_DRAIN_MS) / 1000ULL))

#pragma pack(1)
typedef struct {
  UINT64 Address;
  UINT32 Length;
  UINT32 Ioc;
} HDA_BDL_ENTRY;
#pragma pack()

typedef struct {
  BOOLEAN Debug;
  BOOLEAN FileLog;
  BOOLEAN ScreenLog;
  BOOLEAN NvramStatus;
  BOOLEAN ProbeOnly;
  BOOLEAN NoPlayback;
  BOOLEAN DumpCodec;
  BOOLEAN PreferIntel;
  BOOLEAN AppleALCPolicy;
  BOOLEAN PreferKnownPins;
  BOOLEAN AllowDigitalPins;
  BOOLEAN SingleShot;
  BOOLEAN CodecDumpOnF6;
  BOOLEAN CodecDumpAlways;
  BOOLEAN CodecDumpSkipsPlayback;
  BOOLEAN BootScreenF6;
  BOOLEAN FastBoot;
  BOOLEAN WaitForChime;
  BOOLEAN ReadConfig;
  BOOLEAN ReadDebugConfig;
  BOOLEAN LowLatency;
  BOOLEAN QuickPath;
  BOOLEAN LegacyRealtekFanout;
  BOOLEAN MobileRealtekFanout;
  BOOLEAN BroadRealtekFanout;
  BOOLEAN RealtekGpioAmp;
  BOOLEAN RealtekLineOutPinctl;
  BOOLEAN RealtekMultiDacStream;
  BOOLEAN RealtekAppleAlcCombos;
  BOOLEAN RealtekCodecWarmReset;
  BOOLEAN RealtekLegacyPinDrive;
  BOOLEAN FullPathMap;
  BOOLEAN RouteFallback;
  BOOLEAN PinSensePolicy;
  BOOLEAN TransactionalRestore;
  BOOLEAN CorbRirbFallback;
  BOOLEAN PcmFormatFallback;
  BOOLEAN DigitalPipeline;
  BOOLEAN DigitalInfoFrame;
  BOOLEAN ExactProfileInit;
  UINTN   F6PollMs;
  UINTN   StopAfterStage;
  UINTN   StallMs;
  UINT8   ForceCodec;
  UINT16  ForcePin;
  UINT16  ForceDac;
  UINTN   VolumeQ8;
} HDAU_DEBUG_CONFIG;

typedef struct {
  UINTN  Segment;
  UINTN  Bus;
  UINTN  Device;
  UINTN  Function;
  UINT16 VendorId;
  UINT16 DeviceId;
  UINT8  ClassCode[3];
} HDAU_PCI_INFO;


typedef struct {
  UINT8   Afg;
  UINT8   Dac;
  UINT8   Pin;
  UINT8   Path[HDAU_MAX_PATH_DEPTH];
  UINTN   PathLen;
  INTN    Score;
  BOOLEAN AppleAlcExact;
  BOOLEAN Digital;
  BOOLEAN Hdmi;
  BOOLEAN DisplayPort;
} HDAU_ROUTE_CANDIDATE;

#define HDAU_SNAP_POWER          BIT0
#define HDAU_SNAP_CONN_SELECT    BIT1
#define HDAU_SNAP_PIN_CTL        BIT2
#define HDAU_SNAP_EAPD           BIT3
#define HDAU_SNAP_CONVERTER_FMT  BIT4
#define HDAU_SNAP_STREAM_CHAN    BIT5
#define HDAU_SNAP_PROCESSING     BIT6
#define HDAU_SNAP_OUT_AMP_L      BIT7
#define HDAU_SNAP_OUT_AMP_R      BIT8
#define HDAU_SNAP_IN_AMP_L       BIT9
#define HDAU_SNAP_IN_AMP_R       BIT10
#define HDAU_SNAP_DIGITAL_CTL    BIT11
#define HDAU_SNAP_HDMI_SLOT0     BIT12
#define HDAU_SNAP_HDMI_SLOT1     BIT13
#define HDAU_SNAP_HDMI_DIP       BIT14
#define HDAU_SNAP_CONFIG_DEFAULT  BIT15
#define HDAU_SNAP_UNSOLICITED     BIT16
#define HDAU_SNAP_GPIO_WAKE       BIT17
#define HDAU_SNAP_BEEP            BIT18
#define HDAU_SNAP_VOLUME_KNOB     BIT19
#define HDAU_SNAP_SUBSYSTEM_ID    BIT20
#define HDAU_SNAP_GPIO_DATA       BIT21
#define HDAU_SNAP_GPIO_ENABLE     BIT22
#define HDAU_SNAP_GPIO_DIRECTION  BIT23

typedef struct {
  UINT8  Nid;
  UINT8  InputIndex;
  UINT32 Flags;
  UINT32 AwCaps;
  UINT32 PowerState;
  UINT32 ConnectionSelect;
  UINT32 PinCtl;
  UINT32 ConfigDefault;
  UINT32 Eapd;
  UINT32 ConverterFormat;
  UINT32 StreamChannel;
  UINT32 ProcessingState;
  UINT32 OutAmpLeft;
  UINT32 OutAmpRight;
  UINT32 InAmpLeft;
  UINT32 InAmpRight;
  UINT32 DigitalCtl;
  UINT32 HdmiSlot0;
  UINT32 HdmiSlot1;
  UINT32 HdmiDipIndex;
  UINT32 HdmiDipXmit;
  UINT32 Unsolicited;
  UINT32 GpioWake;
  UINT32 BeepControl;
  UINT32 VolumeKnob;
  UINT32 SubsystemId;
  UINT32 GpioData;
  UINT32 GpioEnable;
  UINT32 GpioDirection;
  UINT8  HdmiDipData[14];
  UINT8  HdmiDipDataCount;
} HDAU_WIDGET_SNAPSHOT;

typedef struct {
  UINT8 Nid;
  UINT8 PowerState;
} HDAU_POWER_SNAPSHOT;

typedef struct {
  UINT8  Nid;
  UINT16 Index;
  UINT16 Value;
} HDAU_COEF_SNAPSHOT;

typedef struct {
  UINT8  Nid;
  UINT16 OriginalIndex;
} HDAU_COEF_INDEX_SNAPSHOT;

typedef struct {
  EFI_PCI_IO_PROTOCOL *PciIo;
  UINT8               Codec;
  UINT8               Afg;
  UINT8               Dac;
  UINT8               Pin;
  UINT8               Path[HDAU_MAX_PATH_DEPTH];
  UINTN               PathLen;
  HDAU_ROUTE_CANDIDATE RouteCandidates[HDAU_MAX_ROUTE_CANDIDATES];
  UINTN               RouteCandidateCount;
  UINTN               RouteCandidateIndex;
  BOOLEAN             DigitalRoute;
  BOOLEAN             HdmiRoute;
  BOOLEAN             DisplayPortRoute;
  UINT16              StreamFormat;
  UINT32              SampleRate;
  UINT8               Channels;
  CONST INT16         *PcmData;
  UINTN               PcmFrames;
  UINTN               PcmSamples;
  UINTN               PcmBytes;
  UINT64              BytesPerSecond;
  UINT32              CodecId;
  UINT32              RevisionId;
  UINT32              SubsystemId;
  BOOLEAN             RevisionIdValid;
  BOOLEAN             SubsystemIdValid;
  HDAU_WIDGET_SNAPSHOT SnapshotNodes[HDAU_MAX_SNAPSHOT_NODES];
  UINTN               SnapshotNodeCount;
  HDAU_COEF_SNAPSHOT  CoefSnapshots[HDAU_MAX_COEF_SNAPSHOTS];
  UINTN               CoefSnapshotCount;
  HDAU_COEF_INDEX_SNAPSHOT CoefIndexSnapshots[HDAU_MAX_COEF_INDEX_SNAPSHOTS];
  UINTN               CoefIndexSnapshotCount;
  HDAU_POWER_SNAPSHOT WarmPowerSnapshots[HDAU_MAX_WIDGETS];
  UINTN               WarmPowerSnapshotCount;
  BOOLEAN             SnapshotValid;
  BOOLEAN             SnapshotRestored;
  BOOLEAN             SkipAlcIdConfigData;
  BOOLEAN             SkipExactProfileData;
  BOOLEAN             HaveAfgPowerSnapshot;
  UINT32              OriginalAfgPowerState;
  BOOLEAN             HaveGpioSnapshot;
  UINT32              OriginalGpioData;
  UINT32              OriginalGpioEnable;
  UINT32              OriginalGpioDirection;
  BOOLEAN             HaveGpioWakeSnapshot;
  UINT32              OriginalGpioWake;
  UINT16              StreamOffset;
  BOOLEAN             StreamStateValid;
  BOOLEAN             StreamBidirectional;
  EFI_STATUS          PostPlaybackCleanupStatus;
  BOOLEAN             Controller64BitDma;
  UINT8               OriginalStreamCtl0;
  UINT8               OriginalStreamCtl1;
  UINT8               OriginalStreamCtl2;
  UINT32              OriginalStreamCbl;
  UINT16              OriginalStreamLvi;
  UINT16              OriginalStreamFmt;
  UINT32              OriginalStreamBdlpl;
  UINT32              OriginalStreamBdlpu;
  UINTN               BufferPages;
  UINTN               BdlPages;
  VOID                *AudioBuffer;
  EFI_PHYSICAL_ADDRESS AudioBufferPhys;
  EFI_PHYSICAL_ADDRESS AudioDeviceAddress;
  VOID                *AudioMapping;
  BOOLEAN             AudioMapped;
  UINTN               AudioBufferBytes;
  VOID                *Bdl;
  EFI_PHYSICAL_ADDRESS BdlPhys;
  EFI_PHYSICAL_ADDRESS BdlDeviceAddress;
  VOID                *BdlMapping;
  BOOLEAN             BdlMapped;
  UINTN               BdlBytes;
  UINT64              OriginalPciAttributes;
  BOOLEAN             HaveOriginalPciAttributes;
  BOOLEAN             PciDmaContainment;
  BOOLEAN             HardwareVolumeApplied;
  BOOLEAN             HardwareVolumeInputAmp;
  UINT8               HardwareVolumeNid;
  UINT8               HardwareVolumeInputIndex;
  BOOLEAN             HeapAllocated;
} HDAU_CONTEXT;

#endif
