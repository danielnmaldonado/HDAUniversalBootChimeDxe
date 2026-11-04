// HDAUniversalBootChimeDxe
// Copyright © 2026 DNMTechLabs
// Contact: dnmtechlabs@gmail.com
// Boot chime DXE driver for Clover/OpenCore.

#include "HdaUefi.h"
#include "BootChimePcm.h"
#include "BootChimePcm44.h"

VOID *
memcpy(
  OUT VOID        *Destination,
  IN  CONST VOID  *Source,
  IN  UINTN       Length
  );

/*
 * XCODE5/Clang may lower large structure copies introduced by optimization to
 * the C runtime symbol memcpy(). DXE modules do not link libc, so provide the
 * exact compiler helper locally and route it through the EDK2 BaseMemoryLib.
 * This does not alter HDA, routing, DMA, codec or volume behavior.
 */
VOID *
memcpy(
  OUT VOID        *Destination,
  IN  CONST VOID  *Source,
  IN  UINTN       Length
  )
{
  return CopyMem(Destination, Source, Length);
}

STATIC EFI_GUID gHdaUChimeStatusGuid = { 0x48444155, 0x4348, 0x494d, { 0x45, 0x44, 0x58, 0x45, 0x44, 0x42, 0x47, 0x31 } };
STATIC EFI_GUID gAppleBootVariableGuid = { 0x7C436110, 0xAB2A, 0x4BBB, { 0xA8, 0x80, 0xFE, 0x41, 0x99, 0x5C, 0x9F, 0x82 } };
STATIC EFI_GUID gHdaUChimeLoadedGuid = { 0x48444155, 0x4c4f, 0x4144, { 0x45, 0x44, 0x58, 0x45, 0x4d, 0x41, 0x4c, 0x44 } };
STATIC UINTN    mHdaLoadedMarker = 0x48444155;
STATIC EFI_HANDLE mHdaLoadedProtocolHandle = NULL;


typedef struct {
  EFI_PCI_IO_PROTOCOL *PciIo;
  VOID *CorbHost;
  VOID *RirbHost;
  VOID *CorbMapping;
  VOID *RirbMapping;
  UINTN CorbPages;
  UINTN RirbPages;
  BOOLEAN CorbAllocated;
  BOOLEAN RirbAllocated;
  BOOLEAN CorbMapped;
  BOOLEAN RirbMapped;
  BOOLEAN RegistersTouched;
  BOOLEAN DmaStopUnconfirmed;
  BOOLEAN DmaContainment;
  BOOLEAN BusMasterEnabledByFallback;
  BOOLEAN Supports64BitDma;
  UINT8 OriginalCorbCtl;
  UINT8 OriginalRirbCtl;
  UINT8 OriginalCorbSize;
  UINT8 OriginalRirbSize;
  UINT16 OriginalCorbWp;
  UINT16 OriginalCorbRp;
  UINT16 OriginalRirbWp;
  UINT16 OriginalRintCnt;
  UINT32 OriginalCorbLbase;
  UINT32 OriginalCorbUbase;
  UINT32 OriginalRirbLbase;
  UINT32 OriginalRirbUbase;
} HDAU_CORB_RIRB_STATE;

STATIC HDAU_CORB_RIRB_STATE mHdaRetainedCorbRirbState;
STATIC BOOLEAN mHdaRetainedCorbRirbValid = FALSE;

STATIC EFI_FILE_HANDLE mHdaLogFile = NULL;
STATIC UINTN mHdaLogLineCount = 0;
STATIC HDAU_DEBUG_CONFIG mHdaCfg = {
  FALSE,                      /* Debug: release-stable default */
  FALSE,                      /* FileLog: off by default to avoid slow EFI writes */
  FALSE,                      /* ScreenLog */
  FALSE,                      /* NvramStatus: off by default to avoid slow NVRAM writes */
  FALSE,                      /* ProbeOnly */
  FALSE,                      /* NoPlayback */
  FALSE,                      /* DumpCodec */
  TRUE,                       /* PreferIntel */
  TRUE,                       /* AppleALCPolicy */
  FALSE,                      /* PreferKnownPins: optional legacy ranking; runtime defaults drive selection */
  FALSE,                      /* AllowDigitalPins */
  TRUE,                       /* SingleShot */
  TRUE,                       /* CodecDumpOnD (legacy aliases: CodecDumpOnF6/F6CodecDump) */
  FALSE,                      /* CodecDumpAlways */
  TRUE,                       /* CodecDumpSkipsPlayback */
  TRUE,                       /* BootScreenD (legacy alias: BootScreenF6): D works on Clover/OpenCore boot screen */
  FALSE,                      /* FastBoot: off by default in countdown-safe mode */
  TRUE,                       /* WaitForChime: block bootloader picker until chime tail is safely played */
  TRUE,                       /* ReadConfig: one release config path only; still user-editable */
  TRUE,                       /* ReadDebugConfig: debug folder is authoritative for diagnostics/logs */
  TRUE,                       /* LowLatency: avoid cold HDA reset when firmware already left codec awake */
  FALSE,                      /* QuickPath: optional policy hint path; full runtime graph scan is default */
  FALSE,                      /* LegacyRealtekFanout: compatibility override; disabled by default */
  FALSE,                      /* MobileRealtekFanout: compatibility override; disabled by default */
  FALSE,                      /* BroadRealtekFanout: unsafe broad sweep disabled by default */
  FALSE,                      /* RealtekGpioAmp: requires an exact profile-backed sequence; disabled by default */
  TRUE,                       /* RealtekLineOutPinctl: use line-out pinctl for line/speaker outputs, HP bit only for headphone */
  FALSE,                      /* RealtekMultiDacStream: do not arm unrelated converters by default */
  FALSE,                      /* RealtekAppleAlcCombos: legacy heuristic; exact alcid runtime paths remain active */
  TRUE,                       /* RealtekCodecWarmReset: wake Realtek AFG/widgets again before verb/profile init */
  FALSE,                      /* RealtekLegacyPinDrive: explicit compatibility override only */
  TRUE,                       /* FullPathMap: prefer complete AppleALC node chains when generated */
  TRUE,                       /* RouteFallback: retry validated candidates only after real programming/capability failure */
  TRUE,                       /* PinSensePolicy: presence-aware ranking without excluding internal speakers */
  TRUE,                       /* TransactionalRestore: snapshot and restore selected codec state */
  TRUE,                       /* CorbRirbFallback: use temporary rings only when Immediate Command fails and rings are idle */
  TRUE,                       /* PcmFormatFallback: 48 kHz stereo -> 44.1 kHz stereo -> mono */
  TRUE,                       /* DigitalPipeline: separate SPDIF/HDMI programming when digital pins are explicitly allowed */
  FALSE,                      /* DigitalInfoFrame: conservative default; channel mapping only unless explicitly enabled */
  TRUE,                       /* ExactProfileInit: apply only exact generated codec/layout/revision/subsystem sequences */
  HDAU_DEFAULT_D_POLL_MS,     /* DPollMs (legacy alias: F6PollMs): 0 = no pre-menu blocking poll */
  0,                          /* StopAfterStage */
  HDAU_DEFAULT_DEBUG_STALL_MS, /* StallMs */
  HDAU_FORCE_CODEC_NONE,      /* ForceCodec */
  HDAU_FORCE_NID_NONE,        /* ForcePin */
  HDAU_FORCE_NID_NONE,        /* ForceDac */
  HDAU_BOOT_CHIME_GAIN_Q8     /* VolumeQ8 */
};
STATIC UINTN mHdaLastStage = 0;
STATIC BOOLEAN mHdaCodecDumpRequested = FALSE;
STATIC BOOLEAN mHdaCodecDumpByF6 = FALSE;
STATIC EFI_HANDLE mHdaImageHandle = NULL;
STATIC VOID *mHdaF6NotifyRegistration = NULL;
STATIC VOID *mHdaDNotifyRegistrationUpper = NULL;
STATIC EFI_SIMPLE_TEXT_INPUT_EX_PROTOCOL *mHdaF6TextInEx = NULL;
STATIC BOOLEAN mHdaBootArgsCodecDumpRequested = FALSE;
STATIC BOOLEAN mHdaF6DumpInProgress = FALSE;
STATIC BOOLEAN mHdaF6DumpDone = FALSE;
STATIC BOOLEAN mHdaDumpDeferredPending = FALSE;
STATIC BOOLEAN mHdaControllerOperationInProgress = FALSE;
/* R20: callbacks at TPL_CALLBACK may preempt the DXE entry path running at
   TPL_APPLICATION. Keep asynchronous workers away from entry-owned globals
   (especially the log handle) until entry teardown is complete. */
STATIC BOOLEAN mHdaEntryWorkInProgress = FALSE;
/* UEFI permits key notifications at any TPL <= TPL_CALLBACK. Track nesting,
   not just a boolean active flag: an inner notification must not clear the
   exclusion barrier while an outer key-notify stack frame is still active. */
STATIC UINTN mHdaKeyNotifyDepth = 0;
STATIC HDAU_CONTEXT *mHdaAsyncContext = NULL;
STATIC HDAU_CONTEXT mHdaRetainedContext;
STATIC BOOLEAN mHdaRetainedContextValid = FALSE;
STATIC EFI_EVENT mHdaAsyncEvent = NULL;
STATIC EFI_EVENT mHdaExitBootServicesEvent = NULL;
STATIC EFI_EVENT mHdaDumpDeferredEvent = NULL;
STATIC BOOLEAN mHdaBootScreenNotifyQuarantined = FALSE;
/* A successful RegisterKeyNotify() must return a unique NotifyHandle. If a
   broken firmware reports success with a NULL handle, the callback may have
   been installed but cannot be unregistered safely. Keep that unknown
   registration permanently side-effect-free for the remainder of this boot. */
STATIC BOOLEAN mHdaBootScreenUnknownRegistration = FALSE;
/* Prevent callback/timer side effects while key-notify ownership is being
   mutated. Firmware may invoke the callback re-entrantly from registration,
   and a lower-TPL unregister implementation may likewise be preempted before
   the ownership transition is complete. */
STATIC BOOLEAN mHdaBootScreenNotifyMutationInProgress = FALSE;
STATIC BOOLEAN mHdaExitBootServicesInProgress = FALSE;
STATIC UINTN mHdaAsyncElapsedUsec = 0;
STATIC UINTN mHdaAsyncLastProgressUsec = 0;
STATIC UINT32 mHdaAsyncLastLpib = 0;
STATIC BOOLEAN mHdaAsyncHaveLpibBaseline = FALSE;
STATIC BOOLEAN mHdaAsyncMoved = FALSE;
STATIC BOOLEAN mHdaAsyncFault = FALSE;
STATIC BOOLEAN mHdaCorbRirbFallbackDisabled = FALSE;
/* A confirmed persistent gain readback mismatch makes the candidate unsafe.
   Fanout helpers are void, so surface their failure to the owning route. */
STATIC BOOLEAN mHdaVolumeReadbackMismatch = FALSE;
STATIC UINT16 mHdaActiveControllerVendorId = 0;
STATIC UINT16 mHdaActiveControllerDeviceId = 0;
STATIC BOOLEAN mHdaBootArgsAlcIdValid = FALSE;
STATIC UINTN mHdaBootArgsAlcId = 0;
STATIC CONST CHAR8 *mHdaBootArgsAlcIdSource = "none";
#define HDAU_MAX_ALCID_FANOUT_PATHS HDAU_ALCID_PROFILE_PAIR_COUNT
STATIC UINT8 mHdaAlcIdFanoutPins[HDAU_MAX_ALCID_FANOUT_PATHS];
STATIC UINT8 mHdaAlcIdFanoutDacs[HDAU_MAX_ALCID_FANOUT_PATHS];
STATIC UINTN mHdaAlcIdFanoutCount = 0;
#define HDAU_SELECTOR_CLAIM_NONE 0x100U
STATIC UINT16 mHdaFanoutSelectorNext[HDAU_MAX_WIDGETS];
STATIC BOOLEAN mHdaFanoutSelectorClaimsValid = FALSE;
STATIC UINT8 mHdaActiveStreamTag = HDA_STREAM_TAG;
STATIC UINT16 mHdaActiveStreamFormat = HDA_FMT_48K_16B_STEREO;

STATIC EFI_STATUS HdaDumpAllControllersDynamic(BOOLEAN ByF6);
STATIC VOID HdaServiceDeferredCodecDumpIfSafe(VOID);
STATIC BOOLEAN HdaIsCodecDumpKey(IN CONST EFI_INPUT_KEY *Key);
STATIC VOID HdaUninstallBootScreenDNotify(BOOLEAN CloseDeferredEvent);
STATIC EFI_STATUS HdaCleanupAsyncPlayback(BOOLEAN ExitBootServices);
STATIC EFI_STATUS HdaInstallExitBootServicesCleanup(VOID);
STATIC EFI_STATUS HdaEnsureAppendLog(BOOLEAN *OpenedLog, BOOLEAN *SavedFileLog);
STATIC VOID HdaCloseAppendLog(BOOLEAN OpenedLog, BOOLEAN SavedFileLog);
STATIC EFI_STATUS HdaStopOutputStream(HDAU_CONTEXT *Context);
STATIC EFI_STATUS HdaRestorePciAttributes(HDAU_CONTEXT *Context);
STATIC EFI_STATUS HdaGetPciAttributesExact(EFI_PCI_IO_PROTOCOL *PciIo, UINT64 *Attributes);
STATIC EFI_STATUS HdaVerifyPciAttributeBits(EFI_PCI_IO_PROTOCOL *PciIo, UINT64 ExpectedAttributes, UINT64 Mask);
STATIC EFI_STATUS HdaApplyPciAttributeBits(EFI_PCI_IO_PROTOCOL *PciIo, UINT64 DesiredAttributes, UINT64 Mask);
STATIC EFI_STATUS HdaRequireNoRunningStreamsBeforeBusMasterEnable(EFI_PCI_IO_PROTOCOL *PciIo);
STATIC EFI_STATUS HdaSnapshotCodecState(HDAU_CONTEXT *Context);
STATIC EFI_STATUS HdaRestoreCodecState(HDAU_CONTEXT *Context, CONST CHAR8 *Reason);
STATIC EFI_STATUS HdaRestoreCodecStateRetry(HDAU_CONTEXT *Context, CONST CHAR8 *Reason);
STATIC EFI_STATUS HdaRetainContextForCleanup(HDAU_CONTEXT *Context, CONST CHAR8 *Reason);
STATIC BOOLEAN HdaPathInputAmpsProgrammable(EFI_PCI_IO_PROTOCOL *PciIo, UINT8 Codec, UINT8 Afg, CONST UINT8 *Path, UINTN PathLen);
STATIC BOOLEAN HdaFreePlaybackBuffers(HDAU_CONTEXT *Context);
STATIC EFI_STATUS HdaFinalizePlaybackContext(HDAU_CONTEXT *Context, CONST CHAR8 *Reason, EFI_STATUS ResultStatus, EFI_STATUS RestoreStatus);
STATIC EFI_STATUS HdaCleanupRetainedCorbRirb(BOOLEAN ExitBootServices);
STATIC VOID HdaDischargeRetainedCorbRirbBusMasterRestore(EFI_PCI_IO_PROTOCOL *PciIo, UINT64 DesiredAttributes, UINT64 ManagedMask);
STATIC CONST CHAR8 *HdaRealtekFanoutReason(VOID);
STATIC VOID HdaLog(CONST CHAR8 *Format, ...);
STATIC EFI_STATUS HdaCloseFileHandleTerminal(EFI_FILE_HANDLE *Handle);
STATIC EFI_STATUS HdaCloseEventTerminal(EFI_EVENT *Event);
STATIC EFI_STATUS HdaCloseTrackedLogFile(BOOLEAN FlushFirst);
STATIC VOID HdaStall(UINTN Microseconds);

#define HDAU_DEFERRED_DUMP_RETRY_100NS 10000ULL /* 1 ms */

STATIC EFI_STATUS HdaArmDeferredCodecDumpTimer(UINT64 TriggerTime100ns) {
  EFI_STATUS Status;

  if (mHdaDumpDeferredEvent == NULL || gBS == NULL) return EFI_NOT_READY;
  Status = gBS->SetTimer(mHdaDumpDeferredEvent, TimerRelative, TriggerTime100ns);
  if (Status != EFI_SUCCESS && !EFI_ERROR(Status)) Status = EFI_DEVICE_ERROR;
  return Status;
}

STATIC CHAR8 HdaAsciiLower(CHAR8 Ch) {
  if (Ch >= 'A' && Ch <= 'Z') return (CHAR8)(Ch - 'A' + 'a');
  return Ch;
}
STATIC EFI_STATUS
EFIAPI
HdaF6KeyNotify(IN EFI_KEY_DATA *KeyData) {
  EFI_STATUS Status;

  if (KeyData == NULL) return EFI_INVALID_PARAMETER;

  /* Protect the entire notification lifetime, not only SetTimer(). UEFI
     permits this callback at any TPL <= TPL_CALLBACK, and firmware is not
     required to make separate key notifications non-nested. A depth counter
     keeps the exclusion barrier asserted until the outermost callback has
     unwound, so an earlier timer can never enter the heavy worker while any
     key-notify stack frame is still active. */
  if (mHdaKeyNotifyDepth == MAX_UINTN) return EFI_OUT_OF_RESOURCES;
  mHdaKeyNotifyDepth++;
  Status = EFI_SUCCESS;

  if (mHdaBootScreenNotifyQuarantined ||
      mHdaBootScreenUnknownRegistration ||
      mHdaBootScreenNotifyMutationInProgress ||
      mHdaExitBootServicesInProgress) {
    Status = EFI_NOT_READY;
    goto Done;
  }
  if (!mHdaCfg.CodecDumpOnF6 || !mHdaCfg.BootScreenF6) goto Done;
  if (!HdaIsCodecDumpKey(&KeyData->Key)) goto Done;
  if (mHdaF6DumpInProgress || mHdaF6DumpDone || mHdaDumpDeferredPending) goto Done;
  if (mHdaDumpDeferredEvent == NULL || gBS == NULL) {
    Status = EFI_NOT_READY;
    goto Done;
  }

  /* Key notifications may run at any TPL <= TPL_CALLBACK. Never perform
     filesystem, PCI reset, allocation, or full codec walking inline. While
     the DXE entry path is still active, leave the request pending: that path
     already services it synchronously at a controller-safe boundary. This
     prevents a TPL_CALLBACK timer from preempting entry-owned log/global
     state. Once entry is finished, arm the one-shot timer normally. */
  mHdaDumpDeferredPending = TRUE;
  if (mHdaEntryWorkInProgress) goto Done;

  Status = HdaArmDeferredCodecDumpTimer(0);
  if (Status != EFI_SUCCESS) {
    mHdaDumpDeferredPending = FALSE;
  }

Done:
  mHdaKeyNotifyDepth--;
  return Status;
}


STATIC BOOLEAN HdaAsciiEqualsInsensitive(CONST CHAR8 *A, CONST CHAR8 *B) {
  if (A == NULL || B == NULL) return FALSE;
  while (*A != '\0' && *B != '\0') {
    if (HdaAsciiLower(*A) != HdaAsciiLower(*B)) return FALSE;
    A++;
    B++;
  }
  return (*A == '\0' && *B == '\0');
}

STATIC CHAR8 *HdaSkipSpaces(CHAR8 *Text) {
  while (*Text == ' ' || *Text == '\t') Text++;
  return Text;
}

STATIC VOID HdaTrimRight(CHAR8 *Text) {
  UINTN Len;
  if (Text == NULL) return;
  Len = AsciiStrLen(Text);
  while (Len > 0) {
    CHAR8 Ch = Text[Len - 1];
    if (Ch != ' ' && Ch != '\t') break;
    Text[Len - 1] = '\0';
    Len--;
  }
}

STATIC BOOLEAN HdaParseNumberStrict(CONST CHAR8 *Text, UINTN *ValueOut) {
  UINTN Value;
  UINTN Base;
  BOOLEAN AnyDigit;

  if (Text == NULL || ValueOut == NULL) return FALSE;
  while (*Text == ' ' || *Text == '\t') Text++;
  Value = 0;
  Base = 10;
  AnyDigit = FALSE;
  if (Text[0] == '0' && (Text[1] == 'x' || Text[1] == 'X')) {
    Base = 16;
    Text += 2;
  }

  while (*Text != '\0') {
    CHAR8 Ch = *Text;
    UINTN Digit;

    if (Ch >= '0' && Ch <= '9') Digit = (UINTN)(Ch - '0');
    else if (Ch >= 'a' && Ch <= 'f') Digit = (UINTN)(Ch - 'a' + 10);
    else if (Ch >= 'A' && Ch <= 'F') Digit = (UINTN)(Ch - 'A' + 10);
    else break;
    if (Digit >= Base) return FALSE;
    if (Value > ((MAX_UINTN - Digit) / Base)) return FALSE;
    Value = (Value * Base) + Digit;
    AnyDigit = TRUE;
    Text++;
  }

  while (*Text == ' ' || *Text == '\t') Text++;
  if (!AnyDigit || *Text != '\0') return FALSE;
  *ValueOut = Value;
  return TRUE;
}

STATIC BOOLEAN HdaParseBoolStrict(CONST CHAR8 *Text, BOOLEAN *ValueOut) {
  UINTN Value;

  if (Text == NULL || ValueOut == NULL) return FALSE;
  if (HdaAsciiEqualsInsensitive(Text, "true") ||
      HdaAsciiEqualsInsensitive(Text, "yes") ||
      HdaAsciiEqualsInsensitive(Text, "on")) {
    *ValueOut = TRUE;
    return TRUE;
  }
  if (HdaAsciiEqualsInsensitive(Text, "false") ||
      HdaAsciiEqualsInsensitive(Text, "no") ||
      HdaAsciiEqualsInsensitive(Text, "off")) {
    *ValueOut = FALSE;
    return TRUE;
  }
  if (!HdaParseNumberStrict(Text, &Value)) return FALSE;
  *ValueOut = (Value != 0);
  return TRUE;
}

STATIC VOID HdaApplyConfigBool(CONST CHAR8 *Text, BOOLEAN *Field) {
  BOOLEAN Parsed;

  if (Field != NULL && HdaParseBoolStrict(Text, &Parsed)) {
    *Field = Parsed;
  }
}

STATIC VOID HdaAsciiToUnicode(CONST CHAR8 *Ascii, CHAR16 *Unicode, UINTN UnicodeChars) {
  UINTN Index;
  if (UnicodeChars == 0) return;
  for (Index = 0; Index + 1 < UnicodeChars && Ascii != NULL && Ascii[Index] != '\0'; Index++) {
    Unicode[Index] = (CHAR16)(UINT8)Ascii[Index];
  }
  Unicode[Index] = L'\0';
}

STATIC VOID HdaSetNvramAscii(CONST CHAR16 *Name, CONST CHAR8 *Value) {
  CHAR16 Wide[192];
  UINTN Size;
  UINT32 Attr;

  if (!mHdaCfg.NvramStatus || Name == NULL || Value == NULL || gRT == NULL) return;

  HdaAsciiToUnicode(Value, Wide, sizeof(Wide) / sizeof(Wide[0]));
  Size = (StrLen(Wide) + 1) * sizeof(CHAR16);
  Attr = EFI_VARIABLE_BOOTSERVICE_ACCESS | EFI_VARIABLE_RUNTIME_ACCESS | EFI_VARIABLE_NON_VOLATILE;
  (VOID)gRT->SetVariable((CHAR16 *)Name, &gHdaUChimeStatusGuid, Attr, Size, Wide);
}



STATIC EFI_STATUS HdaCloseFileHandleTerminal(EFI_FILE_HANDLE *Handle) {
  EFI_FILE_HANDLE LocalHandle;
  EFI_STATUS Status;

  if (Handle == NULL) return EFI_INVALID_PARAMETER;
  LocalHandle = *Handle;
  if (LocalHandle == NULL) return EFI_SUCCESS;

  /*
   * UEFI File.Close() is terminal: "in all cases the handle is closed".
   * Clear caller ownership after the call even if non-conforming firmware
   * returns an undocumented status, because retrying the handle can become a
   * double-close/use-after-free.
   */
  Status = LocalHandle->Close(LocalHandle);
  *Handle = NULL;
  if (Status != EFI_SUCCESS && !EFI_ERROR(Status)) Status = EFI_DEVICE_ERROR;
  return Status;
}

STATIC EFI_STATUS HdaCloseEventTerminal(EFI_EVENT *Event) {
  EFI_EVENT LocalEvent;
  EFI_STATUS Status;

  if (Event == NULL) return EFI_INVALID_PARAMETER;
  LocalEvent = *Event;
  if (LocalEvent == NULL) return EFI_SUCCESS;
  if (gBS == NULL) return EFI_NOT_READY;

  /*
   * CloseEvent() removes the caller reference and invalidates the event.
   * The specification defines only EFI_SUCCESS. Never retry the same handle
   * after the call, even if broken firmware reports an undocumented status.
   */
  Status = gBS->CloseEvent(LocalEvent);
  *Event = NULL;
  if (Status != EFI_SUCCESS && !EFI_ERROR(Status)) Status = EFI_DEVICE_ERROR;
  return Status;
}

STATIC EFI_STATUS HdaCloseTrackedLogFile(BOOLEAN FlushFirst) {
  EFI_FILE_HANDLE File;
  EFI_STATUS Status;
  EFI_STATUS FirstStatus;

  File = mHdaLogFile;
  if (File == NULL) {
    mHdaLogLineCount = 0;
    return EFI_SUCCESS;
  }

  FirstStatus = EFI_SUCCESS;
  if (FlushFirst) {
    Status = File->Flush(File);
    if (Status != EFI_SUCCESS) {
      if (!EFI_ERROR(Status)) Status = EFI_DEVICE_ERROR;
      FirstStatus = Status;
    }
  }

  Status = File->Close(File);
  mHdaLogFile = NULL;
  mHdaLogLineCount = 0;
  if (Status != EFI_SUCCESS) {
    if (!EFI_ERROR(Status)) Status = EFI_DEVICE_ERROR;
    if (FirstStatus == EFI_SUCCESS) FirstStatus = Status;
  }
  return FirstStatus;
}

STATIC BOOLEAN HdaAcquireBootGuard(VOID) {
  EFI_STATUS Status;
  VOID      *Existing;
  UINT8      Value;
  UINTN      Size;
  UINT32     Attr;
  EFI_HANDLE ProtocolHandle;
  BOOLEAN    GuardArmed;

  if (!mHdaCfg.SingleShot || gBS == NULL) return TRUE;

  GuardArmed = FALSE;

  /* Volatile boot-services variable: catches duplicate driver loads while
     disappearing automatically on reboot. */
  if (gRT != NULL) {
    Value = 0;
    Size = sizeof(Value);
    Attr = 0;
    Status = gRT->GetVariable(L"HDAUChimeBootGuard", &gHdaUChimeStatusGuid, &Attr, &Size, &Value);
    if (Status == EFI_SUCCESS && Size == sizeof(Value) && Value == 1) {
      HdaSetNvramAscii(L"HDAUChimeLastStatus", "SkippedDuplicateLoadVolatileGuard");
      HdaLog("[HDAU] single-shot guard: duplicate volatile boot guard, skipping playback");
      return FALSE;
    }

    Value = 1;
    Attr = EFI_VARIABLE_BOOTSERVICE_ACCESS;
    Status = gRT->SetVariable(L"HDAUChimeBootGuard", &gHdaUChimeStatusGuid, Attr, sizeof(Value), &Value);
    if (Status != EFI_SUCCESS) {
      if (!EFI_ERROR(Status)) Status = EFI_DEVICE_ERROR;
      HdaLog("[HDAU] single-shot volatile boot guard set failed: %r; using protocol guard", Status);
    } else {
      GuardArmed = TRUE;
      HdaLog("[HDAU] single-shot volatile boot guard armed for this boot");
    }
  }

  Existing = NULL;
  Status = gBS->LocateProtocol(&gHdaUChimeLoadedGuid, NULL, &Existing);
  if (Status == EFI_SUCCESS && Existing != NULL) {
    HdaSetNvramAscii(L"HDAUChimeLastStatus", "SkippedDuplicateLoadProtocol");
    HdaLog("[HDAU] single-shot guard: duplicate loaded-driver protocol, skipping playback");
    return FALSE;
  }

  /*
   * InstallProtocolInterface() takes an IN OUT EFI_HANDLE. Keep the firmware
   * output local until exact success. A value written together with a failure
   * is not consumed or uninstalled by this driver because no ownership
   * transfer is established by the failure contract.
   */
  ProtocolHandle = NULL;
  Status = gBS->InstallProtocolInterface(
                  &ProtocolHandle,
                  &gHdaUChimeLoadedGuid,
                  EFI_NATIVE_INTERFACE,
                  &mHdaLoadedMarker
                  );
  if (Status != EFI_SUCCESS) {
    ProtocolHandle = NULL;
    mHdaLoadedProtocolHandle = NULL;
    if (!EFI_ERROR(Status)) Status = EFI_DEVICE_ERROR;
    HdaLog("[HDAU] single-shot protocol guard install failed: %r", Status);
  } else if (ProtocolHandle == NULL) {
    mHdaLoadedProtocolHandle = NULL;
    HdaLog("[HDAU] single-shot protocol guard install violated UEFI output contract");
  } else {
    mHdaLoadedProtocolHandle = ProtocolHandle;
    GuardArmed = TRUE;
    HdaLog("[HDAU] single-shot protocol guard armed for this boot");
  }

  /*
   * SingleShot is a concurrency safety boundary, not a best-effort hint. If
   * neither independent per-boot guard could be established, proceeding would
   * allow another image instance to program the same HDA controller and DMA
   * state concurrently. Fail closed instead of weakening the configured guard.
   */
  if (!GuardArmed) {
    HdaSetNvramAscii(L"HDAUChimeLastStatus", "SingleShotGuardUnavailable");
    HdaLog("[HDAU] single-shot guard unavailable: refusing unguarded playback");
    return FALSE;
  }

  return TRUE;
}

STATIC VOID HdaLog(CONST CHAR8 *Format, ...) {
  EFI_STATUS WriteStatus;
  VA_LIST Marker;
  CHAR8 Buffer[512];
  CHAR8 Line[560];
  UINTN ExpectedSize;
  UINTN Size;

  if (Format == NULL) return;
  if (!mHdaCfg.Debug && !mHdaCfg.FileLog && !mHdaCfg.ScreenLog) return;

  VA_START(Marker, Format);
  AsciiVSPrint(Buffer, sizeof(Buffer), Format, Marker);
  VA_END(Marker);

  AsciiSPrint(Line, sizeof(Line), "%a\r\n", Buffer);

  if (mHdaCfg.FileLog && mHdaLogFile != NULL) {
    ExpectedSize = AsciiStrLen(Line);
    Size = ExpectedSize;
    WriteStatus = mHdaLogFile->Write(mHdaLogFile, &Size, Line);
    if (WriteStatus != EFI_SUCCESS || Size != ExpectedSize) {
      /* Stop hammering a failed filesystem handle. Screen/DEBUG sinks remain
         available, and the normal finish path will not reuse the closed file. */
      (VOID)HdaCloseTrackedLogFile(FALSE);
      mHdaCfg.FileLog = FALSE;
    } else {
      mHdaLogLineCount++;
      /* Avoid one filesystem flush per line. Full flushes happen at completion
         boundaries and every 32 lines to limit loss after firmware failure. */
      if ((mHdaLogLineCount & 31U) == 0U) {
        WriteStatus = mHdaLogFile->Flush(mHdaLogFile);
        if (WriteStatus != EFI_SUCCESS) {
          (VOID)HdaCloseTrackedLogFile(FALSE);
          mHdaCfg.FileLog = FALSE;
        }
      }
    }
  }

  if (mHdaCfg.ScreenLog) {
    Print(L"%a\r\n", Buffer);
  }

  if (mHdaCfg.Debug) {
    DEBUG((DEBUG_INFO, "%a\r\n", Buffer));
  }
}



STATIC BOOLEAN HdaTraceStage(UINTN Stage, CONST CHAR8 *Name) {
  CHAR8 Status[128];

  mHdaLastStage = Stage;
  AsciiSPrint(Status, sizeof(Status), "%02u:%a", (UINT32)Stage, Name == NULL ? "stage" : Name);
  HdaSetNvramAscii(L"HDAUChimeLastStage", Status);
  HdaLog("[HDAU] Stage %02u: %a", (UINT32)Stage, Name == NULL ? "stage" : Name);

  if (mHdaCfg.StopAfterStage != 0 && Stage >= mHdaCfg.StopAfterStage) {
    HdaSetNvramAscii(L"HDAUChimeLastStatus", "StoppedByConfig");
    HdaLog("[HDAU] StopAfterStage=%u reached", (UINT32)mHdaCfg.StopAfterStage);
    if (mHdaCfg.StallMs != 0) HdaStall(mHdaCfg.StallMs * 1000);
    return TRUE;
  }

  if (mHdaCfg.StallMs != 0) HdaStall(mHdaCfg.StallMs * 1000);
  return FALSE;
}

STATIC VOID HdaSetLastStatus(CONST CHAR8 *Status) {
  HdaSetNvramAscii(L"HDAUChimeLastStatus", Status == NULL ? "Unknown" : Status);
  HdaLog("[HDAU] Status: %a", Status == NULL ? "Unknown" : Status);
}

STATIC VOID HdaApplyConfigKey(CHAR8 *Key, CHAR8 *Value) {
  UINTN Number;

  if (Key == NULL || Value == NULL) return;
  HdaTrimRight(Key);
  HdaTrimRight(Value);
  Value = HdaSkipSpaces(Value);

  if (HdaAsciiEqualsInsensitive(Key, "Debug")) HdaApplyConfigBool(Value, &mHdaCfg.Debug);
  else if (HdaAsciiEqualsInsensitive(Key, "FileLog")) HdaApplyConfigBool(Value, &mHdaCfg.FileLog);
  else if (HdaAsciiEqualsInsensitive(Key, "ScreenLog")) HdaApplyConfigBool(Value, &mHdaCfg.ScreenLog);
  else if (HdaAsciiEqualsInsensitive(Key, "NvramStatus")) HdaApplyConfigBool(Value, &mHdaCfg.NvramStatus);
  else if (HdaAsciiEqualsInsensitive(Key, "ProbeOnly")) HdaApplyConfigBool(Value, &mHdaCfg.ProbeOnly);
  else if (HdaAsciiEqualsInsensitive(Key, "NoPlayback")) HdaApplyConfigBool(Value, &mHdaCfg.NoPlayback);
  else if (HdaAsciiEqualsInsensitive(Key, "DumpCodec") ||
           HdaAsciiEqualsInsensitive(Key, "CodecDump") ||
           HdaAsciiEqualsInsensitive(Key, "CodecDumpFile") ||
           HdaAsciiEqualsInsensitive(Key, "DumpCodecFile") ||
           HdaAsciiEqualsInsensitive(Key, "ForceCodecDump") ||
           HdaAsciiEqualsInsensitive(Key, "OneShotCodecDump")) {
    BOOLEAN Enabled;
    /* Aliases are one logical switch. A later debug/release override with =0
       must be able to disable a value enabled by an earlier config file. */
    if (!HdaParseBoolStrict(Value, &Enabled)) return;
    mHdaCfg.DumpCodec = Enabled;
    mHdaCfg.CodecDumpAlways = Enabled;
  }
  else if (HdaAsciiEqualsInsensitive(Key, "PreferIntel")) HdaApplyConfigBool(Value, &mHdaCfg.PreferIntel);
  else if (HdaAsciiEqualsInsensitive(Key, "AppleALCPolicy")) HdaApplyConfigBool(Value, &mHdaCfg.AppleALCPolicy);
  else if (HdaAsciiEqualsInsensitive(Key, "PreferKnownPins")) HdaApplyConfigBool(Value, &mHdaCfg.PreferKnownPins);
  else if (HdaAsciiEqualsInsensitive(Key, "AllowDigitalPins")) HdaApplyConfigBool(Value, &mHdaCfg.AllowDigitalPins);
  else if (HdaAsciiEqualsInsensitive(Key, "SingleShot")) HdaApplyConfigBool(Value, &mHdaCfg.SingleShot);
  else if (HdaAsciiEqualsInsensitive(Key, "CodecDumpOnD")) HdaApplyConfigBool(Value, &mHdaCfg.CodecDumpOnF6);
  else if (HdaAsciiEqualsInsensitive(Key, "DCodecDump")) HdaApplyConfigBool(Value, &mHdaCfg.CodecDumpOnF6);
  else if (HdaAsciiEqualsInsensitive(Key, "CodecDumpOnF6")) HdaApplyConfigBool(Value, &mHdaCfg.CodecDumpOnF6); /* legacy alias */
  else if (HdaAsciiEqualsInsensitive(Key, "F6CodecDump")) HdaApplyConfigBool(Value, &mHdaCfg.CodecDumpOnF6); /* legacy alias */
  else if (HdaAsciiEqualsInsensitive(Key, "CodecDumpAlways")) HdaApplyConfigBool(Value, &mHdaCfg.CodecDumpAlways);
  else if (HdaAsciiEqualsInsensitive(Key, "CodecDumpSkipsPlayback")) HdaApplyConfigBool(Value, &mHdaCfg.CodecDumpSkipsPlayback);
  else if (HdaAsciiEqualsInsensitive(Key, "BootScreenD")) HdaApplyConfigBool(Value, &mHdaCfg.BootScreenF6);
  else if (HdaAsciiEqualsInsensitive(Key, "BootScreenF6")) HdaApplyConfigBool(Value, &mHdaCfg.BootScreenF6); /* legacy alias */
  else if (HdaAsciiEqualsInsensitive(Key, "FastBoot")) HdaApplyConfigBool(Value, &mHdaCfg.FastBoot);
  else if (HdaAsciiEqualsInsensitive(Key, "AsyncPlayback")) HdaApplyConfigBool(Value, &mHdaCfg.FastBoot);
  else if (HdaAsciiEqualsInsensitive(Key, "WaitForChime")) HdaApplyConfigBool(Value, &mHdaCfg.WaitForChime);
  else if (HdaAsciiEqualsInsensitive(Key, "BlockUntilDone")) HdaApplyConfigBool(Value, &mHdaCfg.WaitForChime);
  else if (HdaAsciiEqualsInsensitive(Key, "CountdownSafe")) HdaApplyConfigBool(Value, &mHdaCfg.WaitForChime);
  else if (HdaAsciiEqualsInsensitive(Key, "ReadConfig")) HdaApplyConfigBool(Value, &mHdaCfg.ReadConfig);
  else if (HdaAsciiEqualsInsensitive(Key, "ReadDebugConfig")) HdaApplyConfigBool(Value, &mHdaCfg.ReadDebugConfig);
  else if (HdaAsciiEqualsInsensitive(Key, "LowLatency")) HdaApplyConfigBool(Value, &mHdaCfg.LowLatency);
  else if (HdaAsciiEqualsInsensitive(Key, "QuickReset")) HdaApplyConfigBool(Value, &mHdaCfg.LowLatency);
  else if (HdaAsciiEqualsInsensitive(Key, "QuickPath")) HdaApplyConfigBool(Value, &mHdaCfg.QuickPath);
  else if (HdaAsciiEqualsInsensitive(Key, "FastPath")) HdaApplyConfigBool(Value, &mHdaCfg.QuickPath);
  else if (HdaAsciiEqualsInsensitive(Key, "LegacyRealtekFanout")) HdaApplyConfigBool(Value, &mHdaCfg.LegacyRealtekFanout);
  else if (HdaAsciiEqualsInsensitive(Key, "RealtekFanout")) HdaApplyConfigBool(Value, &mHdaCfg.LegacyRealtekFanout);
  else if (HdaAsciiEqualsInsensitive(Key, "AnalogFanout")) HdaApplyConfigBool(Value, &mHdaCfg.LegacyRealtekFanout);
  else if (HdaAsciiEqualsInsensitive(Key, "MobileRealtekFanout")) HdaApplyConfigBool(Value, &mHdaCfg.MobileRealtekFanout);
  else if (HdaAsciiEqualsInsensitive(Key, "AioRealtekFanout")) HdaApplyConfigBool(Value, &mHdaCfg.MobileRealtekFanout);
  else if (HdaAsciiEqualsInsensitive(Key, "AllInOneRealtekFanout")) HdaApplyConfigBool(Value, &mHdaCfg.MobileRealtekFanout);
  else if (HdaAsciiEqualsInsensitive(Key, "RealtekMobileFanout")) HdaApplyConfigBool(Value, &mHdaCfg.MobileRealtekFanout);
  else if (HdaAsciiEqualsInsensitive(Key, "BroadRealtekFanout")) HdaApplyConfigBool(Value, &mHdaCfg.BroadRealtekFanout);
  else if (HdaAsciiEqualsInsensitive(Key, "LegacyRealtekBroadSweep")) HdaApplyConfigBool(Value, &mHdaCfg.BroadRealtekFanout);
  else if (HdaAsciiEqualsInsensitive(Key, "RealtekBroadSweep")) HdaApplyConfigBool(Value, &mHdaCfg.BroadRealtekFanout);
  else if (HdaAsciiEqualsInsensitive(Key, "RealtekAllAnalogPins")) HdaApplyConfigBool(Value, &mHdaCfg.BroadRealtekFanout);
  else if (HdaAsciiEqualsInsensitive(Key, "RealtekGpioAmp")) HdaApplyConfigBool(Value, &mHdaCfg.RealtekGpioAmp);
  else if (HdaAsciiEqualsInsensitive(Key, "RealtekAmpGpio")) HdaApplyConfigBool(Value, &mHdaCfg.RealtekGpioAmp);
  else if (HdaAsciiEqualsInsensitive(Key, "RealtekLineOutPinctl")) HdaApplyConfigBool(Value, &mHdaCfg.RealtekLineOutPinctl);
  else if (HdaAsciiEqualsInsensitive(Key, "RealtekLineOutPinCtl")) HdaApplyConfigBool(Value, &mHdaCfg.RealtekLineOutPinctl);
  else if (HdaAsciiEqualsInsensitive(Key, "RealtekMultiDacStream")) HdaApplyConfigBool(Value, &mHdaCfg.RealtekMultiDacStream);
  else if (HdaAsciiEqualsInsensitive(Key, "RealtekAllDacStream")) HdaApplyConfigBool(Value, &mHdaCfg.RealtekMultiDacStream);
  else if (HdaAsciiEqualsInsensitive(Key, "RealtekAppleAlcCombos")) HdaApplyConfigBool(Value, &mHdaCfg.RealtekAppleAlcCombos);
  else if (HdaAsciiEqualsInsensitive(Key, "AppleALCRealtekCombos")) HdaApplyConfigBool(Value, &mHdaCfg.RealtekAppleAlcCombos);
  else if (HdaAsciiEqualsInsensitive(Key, "RealtekLayoutCombos")) HdaApplyConfigBool(Value, &mHdaCfg.RealtekAppleAlcCombos);
  else if (HdaAsciiEqualsInsensitive(Key, "RealtekCodecWarmReset")) HdaApplyConfigBool(Value, &mHdaCfg.RealtekCodecWarmReset);
  else if (HdaAsciiEqualsInsensitive(Key, "RealtekWarmReset")) HdaApplyConfigBool(Value, &mHdaCfg.RealtekCodecWarmReset);
  else if (HdaAsciiEqualsInsensitive(Key, "RealtekLegacyPinDrive")) HdaApplyConfigBool(Value, &mHdaCfg.RealtekLegacyPinDrive);
  else if (HdaAsciiEqualsInsensitive(Key, "RealtekH81PinDrive")) HdaApplyConfigBool(Value, &mHdaCfg.RealtekLegacyPinDrive);
  else if (HdaAsciiEqualsInsensitive(Key, "RealtekHpDriveOnLineOut")) HdaApplyConfigBool(Value, &mHdaCfg.RealtekLegacyPinDrive);
  else if (HdaAsciiEqualsInsensitive(Key, "FullPathMap")) HdaApplyConfigBool(Value, &mHdaCfg.FullPathMap);
  else if (HdaAsciiEqualsInsensitive(Key, "RouteFallback")) HdaApplyConfigBool(Value, &mHdaCfg.RouteFallback);
  else if (HdaAsciiEqualsInsensitive(Key, "PinSensePolicy")) HdaApplyConfigBool(Value, &mHdaCfg.PinSensePolicy);
  else if (HdaAsciiEqualsInsensitive(Key, "TransactionalRestore")) HdaApplyConfigBool(Value, &mHdaCfg.TransactionalRestore);
  else if (HdaAsciiEqualsInsensitive(Key, "CorbRirbFallback")) HdaApplyConfigBool(Value, &mHdaCfg.CorbRirbFallback);
  else if (HdaAsciiEqualsInsensitive(Key, "PcmFormatFallback")) HdaApplyConfigBool(Value, &mHdaCfg.PcmFormatFallback);
  else if (HdaAsciiEqualsInsensitive(Key, "DigitalPipeline")) HdaApplyConfigBool(Value, &mHdaCfg.DigitalPipeline);
  else if (HdaAsciiEqualsInsensitive(Key, "DigitalInfoFrame")) HdaApplyConfigBool(Value, &mHdaCfg.DigitalInfoFrame);
  else if (HdaAsciiEqualsInsensitive(Key, "ExactProfileInit")) HdaApplyConfigBool(Value, &mHdaCfg.ExactProfileInit);
  else if (HdaAsciiEqualsInsensitive(Key, "DPollMs") || HdaAsciiEqualsInsensitive(Key, "DKeyPollMs") || HdaAsciiEqualsInsensitive(Key, "F6PollMs")) {
    if (!HdaParseNumberStrict(Value, &Number)) return;
    if (Number > HDAU_MAX_D_POLL_MS) Number = HDAU_MAX_D_POLL_MS;
    mHdaCfg.F6PollMs = Number;
  }
  else if (HdaAsciiEqualsInsensitive(Key, "StopAfterStage")) {
    if (!HdaParseNumberStrict(Value, &Number)) return;
    mHdaCfg.StopAfterStage = Number;
  }
  else if (HdaAsciiEqualsInsensitive(Key, "StallMs")) {
    if (!HdaParseNumberStrict(Value, &Number)) return;
    if (Number > HDAU_MAX_STALL_MS) Number = HDAU_MAX_STALL_MS;
    mHdaCfg.StallMs = Number;
  }
  else if (HdaAsciiEqualsInsensitive(Key, "ForceCodec")) {
    if (!HdaParseNumberStrict(Value, &Number)) return;
    mHdaCfg.ForceCodec = (Number == HDAU_FORCE_CODEC_NONE || Number >= HDAU_MAX_CODECS) ? HDAU_FORCE_CODEC_NONE : (UINT8)Number;
  }
  else if (HdaAsciiEqualsInsensitive(Key, "ForcePin")) {
    if (!HdaParseNumberStrict(Value, &Number)) return;
    mHdaCfg.ForcePin = (Number == HDAU_FORCE_NID_NONE || Number > HDAU_MAX_NID) ? HDAU_FORCE_NID_NONE : (UINT16)Number;
  }
  else if (HdaAsciiEqualsInsensitive(Key, "ForceDac")) {
    if (!HdaParseNumberStrict(Value, &Number)) return;
    mHdaCfg.ForceDac = (Number == HDAU_FORCE_NID_NONE || Number > HDAU_MAX_NID) ? HDAU_FORCE_NID_NONE : (UINT16)Number;
  }
  else if (HdaAsciiEqualsInsensitive(Key, "Volume")) {
    /* Keep the legacy key compatible, but never allow an INI to move the
       global boot-chime level away from the fixed 30% policy. */
    if (!HdaParseNumberStrict(Value, &Number)) return;
    (VOID)Number;
    mHdaCfg.VolumeQ8 = HDAU_BOOT_CHIME_GAIN_Q8;
  }
}

STATIC VOID HdaParseConfigBuffer(CHAR8 *Buffer) {
  CHAR8 *Line;
  CHAR8 *Ptr;
  CHAR8 *Eq;
  BOOLEAN Done;

  if (Buffer == NULL) return;

  Line = Buffer;
  Done = FALSE;
  for (Ptr = Buffer; !Done; Ptr++) {
    if (*Ptr == '\0') Done = TRUE;
    if (*Ptr == '\r' || *Ptr == '\n' || *Ptr == '\0') {
      CHAR8 Saved = *Ptr;
      *Ptr = '\0';
      Line = HdaSkipSpaces(Line);
      if (*Line != '\0' && *Line != '#' && *Line != ';') {
        Eq = Line;
        while (*Eq != '\0' && *Eq != '=') Eq++;
        if (*Eq == '=') {
          *Eq = '\0';
          HdaApplyConfigKey(Line, Eq + 1);
        }
      }
      if (Saved == '\0') Done = TRUE;
      Line = Ptr + 1;
    }
  }
}

STATIC EFI_STATUS HdaReadConfigFromPath(EFI_FILE_HANDLE Root, CONST CHAR16 *Path) {
  EFI_STATUS Status;
  EFI_STATUS CloseStatus;
  EFI_FILE_HANDLE File;
  UINT64 FileSize;
  UINTN ReadSize;
  CHAR8 *Buffer;

  if (Root == NULL || Path == NULL) return EFI_INVALID_PARAMETER;

  File = NULL;
  Status = Root->Open(Root, &File, (CHAR16 *)Path, EFI_FILE_MODE_READ, 0);
  if (Status != EFI_SUCCESS) {
    /* NewHandle is consumed only on exact success. */
    File = NULL;
    return EFI_ERROR(Status) ? Status : EFI_DEVICE_ERROR;
  }
  if (File == NULL) return EFI_DEVICE_ERROR;

  FileSize = 0;
  Status = File->SetPosition(File, 0xFFFFFFFFFFFFFFFFULL);
  if (Status == EFI_SUCCESS) Status = File->GetPosition(File, &FileSize);
  if (Status != EFI_SUCCESS || FileSize == 0 || FileSize > HDAU_MAX_CONFIG_FILE_BYTES) {
    CloseStatus = HdaCloseFileHandleTerminal(&File);
    if (Status != EFI_SUCCESS) return EFI_ERROR(Status) ? Status : EFI_DEVICE_ERROR;
    if (CloseStatus != EFI_SUCCESS) return CloseStatus;
    return EFI_BAD_BUFFER_SIZE;
  }

  Status = File->SetPosition(File, 0);
  if (Status != EFI_SUCCESS) {
    CloseStatus = HdaCloseFileHandleTerminal(&File);
    if (Status != EFI_SUCCESS) return EFI_ERROR(Status) ? Status : EFI_DEVICE_ERROR;
    return CloseStatus;
  }

  Buffer = AllocateZeroPool((UINTN)FileSize + 1U);
  if (Buffer == NULL) {
    (VOID)HdaCloseFileHandleTerminal(&File);
    return EFI_OUT_OF_RESOURCES;
  }

  ReadSize = (UINTN)FileSize;
  Status = File->Read(File, &ReadSize, Buffer);
  CloseStatus = HdaCloseFileHandleTerminal(&File);
  if (Status != EFI_SUCCESS) {
    Status = EFI_ERROR(Status) ? Status : EFI_DEVICE_ERROR;
  } else if (ReadSize != (UINTN)FileSize) {
    Status = EFI_DEVICE_ERROR;
  } else if (CloseStatus != EFI_SUCCESS) {
    Status = CloseStatus;
  }

  if (Status == EFI_SUCCESS) {
    Buffer[ReadSize] = '\0';
    HdaParseConfigBuffer(Buffer);
  }

  FreePool(Buffer);
  return Status;
}

STATIC EFI_STATUS HdaOpenOrCreateDir(EFI_FILE_HANDLE Parent, CONST CHAR16 *Name, EFI_FILE_HANDLE *Dir) {
  EFI_STATUS Status;

  if (Parent == NULL || Name == NULL || Dir == NULL) return EFI_INVALID_PARAMETER;
  *Dir = NULL;

  /* UEFI File.Open(): Attributes is only valid when EFI_FILE_MODE_CREATE is
     present. Existing-directory lookup therefore must pass zero attributes. */
  Status = Parent->Open(
                     Parent,
                     Dir,
                     (CHAR16 *)Name,
                     EFI_FILE_MODE_READ | EFI_FILE_MODE_WRITE,
                     0
                     );
  if (Status == EFI_SUCCESS) return (*Dir != NULL) ? EFI_SUCCESS : EFI_DEVICE_ERROR;

  /* Failed Open() does not establish caller ownership of NewHandle. */
  *Dir = NULL;
  if (Status != EFI_NOT_FOUND) return EFI_ERROR(Status) ? Status : EFI_DEVICE_ERROR;

  Status = Parent->Open(
                     Parent,
                     Dir,
                     (CHAR16 *)Name,
                     EFI_FILE_MODE_READ | EFI_FILE_MODE_WRITE | EFI_FILE_MODE_CREATE,
                     EFI_FILE_DIRECTORY
                     );
  if (Status != EFI_SUCCESS) {
    *Dir = NULL;
    return EFI_ERROR(Status) ? Status : EFI_DEVICE_ERROR;
  }
  return (*Dir != NULL) ? EFI_SUCCESS : EFI_DEVICE_ERROR;
}

STATIC EFI_STATUS HdaOpenDebugLog(EFI_FILE_HANDLE Root, BOOLEAN Append) {
  EFI_STATUS Status;
  EFI_STATUS CleanupStatus;
  EFI_STATUS FirstCleanupError;
  EFI_FILE_HANDLE EfiDir;
  EFI_FILE_HANDLE HdaDir;
  EFI_FILE_HANDLE LogsDir;
  EFI_FILE_HANDLE OldFile;
  UINT64 LogSize;

  if (Root == NULL) return EFI_INVALID_PARAMETER;
  if (mHdaLogFile != NULL) return EFI_ALREADY_STARTED;

  EfiDir = NULL;
  HdaDir = NULL;
  LogsDir = NULL;
  OldFile = NULL;
  LogSize = 0;
  FirstCleanupError = EFI_SUCCESS;

  Status = HdaOpenOrCreateDir(Root, L"EFI", &EfiDir);
  if (Status != EFI_SUCCESS) goto Exit;
  Status = HdaOpenOrCreateDir(EfiDir, L"HDAUniversalBootChimeDxe", &HdaDir);
  if (Status != EFI_SUCCESS) goto Exit;
  Status = HdaOpenOrCreateDir(HdaDir, L"Logs", &LogsDir);
  if (Status != EFI_SUCCESS) goto Exit;

  if (!Append) {
    Status = LogsDir->Open(
                        LogsDir,
                        &OldFile,
                        L"HDAUChime.log",
                        EFI_FILE_MODE_READ | EFI_FILE_MODE_WRITE,
                        0
                        );
    if (Status == EFI_SUCCESS) {
      if (OldFile == NULL) {
        Status = EFI_DEVICE_ERROR;
        goto Exit;
      }
      /* Delete() closes OldFile in every outcome. */
      Status = OldFile->Delete(OldFile);
      OldFile = NULL;
      if (Status != EFI_SUCCESS) {
        if (!EFI_ERROR(Status)) Status = EFI_DEVICE_ERROR;
        goto Exit;
      }
    } else if (Status == EFI_NOT_FOUND) {
      OldFile = NULL;
      Status = EFI_SUCCESS;
    } else {
      /* Failed Open(): do not inspect/close its OUT NewHandle. */
      OldFile = NULL;
      if (!EFI_ERROR(Status)) Status = EFI_DEVICE_ERROR;
      goto Exit;
    }
  }

OpenLog:
  mHdaLogFile = NULL;
  Status = LogsDir->Open(
                     LogsDir,
                     &mHdaLogFile,
                     L"HDAUChime.log",
                     EFI_FILE_MODE_READ | EFI_FILE_MODE_WRITE | EFI_FILE_MODE_CREATE,
                     0
                     );
  if (Status != EFI_SUCCESS) {
    /* Failed Open() never transfers NewHandle ownership. */
    mHdaLogFile = NULL;
    if (!EFI_ERROR(Status)) Status = EFI_DEVICE_ERROR;
    goto Exit;
  }
  if (mHdaLogFile == NULL) {
    Status = EFI_DEVICE_ERROR;
    goto Exit;
  }

  if (Append) {
    Status = mHdaLogFile->SetPosition(mHdaLogFile, 0xFFFFFFFFFFFFFFFFULL);
    if (Status == EFI_SUCCESS) Status = mHdaLogFile->GetPosition(mHdaLogFile, &LogSize);
    if (Status != EFI_SUCCESS) {
      if (!EFI_ERROR(Status)) Status = EFI_DEVICE_ERROR;
      (VOID)HdaCloseTrackedLogFile(FALSE);
      goto Exit;
    }

    if (LogSize >= HDAU_MAX_LOG_FILE_BYTES) {
      /* Delete() is terminal for the file handle. */
      Status = mHdaLogFile->Delete(mHdaLogFile);
      mHdaLogFile = NULL;
      mHdaLogLineCount = 0;
      if (Status != EFI_SUCCESS) {
        if (!EFI_ERROR(Status)) Status = EFI_DEVICE_ERROR;
        goto Exit;
      }
      Append = FALSE;
      goto OpenLog;
    }
  }

  mHdaLogLineCount = 0;
  Status = EFI_SUCCESS;

Exit:
  CleanupStatus = HdaCloseFileHandleTerminal(&LogsDir);
  if (CleanupStatus != EFI_SUCCESS && FirstCleanupError == EFI_SUCCESS) FirstCleanupError = CleanupStatus;
  CleanupStatus = HdaCloseFileHandleTerminal(&HdaDir);
  if (CleanupStatus != EFI_SUCCESS && FirstCleanupError == EFI_SUCCESS) FirstCleanupError = CleanupStatus;
  CleanupStatus = HdaCloseFileHandleTerminal(&EfiDir);
  if (CleanupStatus != EFI_SUCCESS && FirstCleanupError == EFI_SUCCESS) FirstCleanupError = CleanupStatus;

  if (Status == EFI_SUCCESS && FirstCleanupError != EFI_SUCCESS) Status = FirstCleanupError;
  if (Status != EFI_SUCCESS) {
    if (mHdaLogFile != NULL) (VOID)HdaCloseTrackedLogFile(FALSE);
    mHdaCfg.FileLog = FALSE;
  }
  return Status;
}



STATIC BOOLEAN HdaAsciiStartsWithInsensitive(CONST CHAR8 *Text, CONST CHAR8 *Prefix) {
  if (Text == NULL || Prefix == NULL) return FALSE;
  while (*Prefix != '\0') {
    if (*Text == '\0') return FALSE;
    if (HdaAsciiLower(*Text) != HdaAsciiLower(*Prefix)) return FALSE;
    Text++;
    Prefix++;
  }
  return TRUE;
}

STATIC BOOLEAN HdaBootArgTokenBoundary(CHAR8 Ch) {
  return (Ch == '\0' || Ch == ' ' || Ch == '\t' || Ch == '\r' || Ch == '\n');
}

STATIC BOOLEAN HdaFindAlcIdInAscii(CONST CHAR8 *Text, UINTN *AlcIdOut) {
  CONST CHAR8 *Ptr;

  if (Text == NULL || AlcIdOut == NULL) return FALSE;

  for (Ptr = Text; *Ptr != '\0'; Ptr++) {
    if ((Ptr == Text || HdaBootArgTokenBoundary(*(Ptr - 1))) &&
        HdaAsciiStartsWithInsensitive(Ptr, "alcid=")) {
      CONST CHAR8 *Cursor;
      UINTN Base;
      UINTN Value;
      UINTN DigitCount;

      Cursor = Ptr + 6;
      Base = 10;
      Value = 0;
      DigitCount = 0;
      if (Cursor[0] == '0' && (Cursor[1] == 'x' || Cursor[1] == 'X')) {
        Base = 16;
        Cursor += 2;
      }

      while (*Cursor != '\0') {
        UINTN Digit;
        CHAR8 Ch;

        Ch = *Cursor;
        if (Ch >= '0' && Ch <= '9') Digit = (UINTN)(Ch - '0');
        else if (Base == 16 && Ch >= 'a' && Ch <= 'f') Digit = (UINTN)(Ch - 'a' + 10);
        else if (Base == 16 && Ch >= 'A' && Ch <= 'F') Digit = (UINTN)(Ch - 'A' + 10);
        else break;

        if (Digit >= Base || Value > (((UINTN)MAX_UINT32 - Digit) / Base)) {
          DigitCount = 0;
          break;
        }
        Value = (Value * Base) + Digit;
        DigitCount++;
        Cursor++;
      }

      if (DigitCount != 0 && HdaBootArgTokenBoundary(*Cursor)) {
        *AlcIdOut = Value; /* AppleALC contains valid layout-id 0 profiles. */
        return TRUE;
      }
    }
  }
  return FALSE;
}



STATIC BOOLEAN HdaBootArgIsEnabledToken(CONST CHAR8 *Ptr, CONST CHAR8 *Token) {
  UINTN Len;

  if (Ptr == NULL || Token == NULL) return FALSE;
  if (!HdaAsciiStartsWithInsensitive(Ptr, Token)) return FALSE;

  Len = AsciiStrLen(Token);
  if (HdaBootArgTokenBoundary(Ptr[Len])) return TRUE;
  if (Ptr[Len] == '=') {
    CONST CHAR8 *Value = Ptr + Len + 1;
    if (Value[0] == '1' && HdaBootArgTokenBoundary(Value[1])) return TRUE;
    if ((HdaAsciiStartsWithInsensitive(Value, "yes") && HdaBootArgTokenBoundary(Value[3])) ||
        (HdaAsciiStartsWithInsensitive(Value, "true") && HdaBootArgTokenBoundary(Value[4])) ||
        (HdaAsciiStartsWithInsensitive(Value, "on") && HdaBootArgTokenBoundary(Value[2]))) {
      return TRUE;
    }
  }

  return FALSE;
}

STATIC BOOLEAN HdaFindCodecDumpRequestInAscii(CONST CHAR8 *Text) {
  CONST CHAR8 *Ptr;

  if (Text == NULL) return FALSE;

  for (Ptr = Text; *Ptr != '\0'; Ptr++) {
    if (!(Ptr == Text || HdaBootArgTokenBoundary(*(Ptr - 1)))) continue;

    if (HdaBootArgIsEnabledToken(Ptr, "hdauchimedump") ||
        HdaBootArgIsEnabledToken(Ptr, "hdaucodecdump") ||
        HdaBootArgIsEnabledToken(Ptr, "hdaudump") ||
        HdaBootArgIsEnabledToken(Ptr, "codecdump") ||
        HdaBootArgIsEnabledToken(Ptr, "dumpcodec") ||
        HdaBootArgIsEnabledToken(Ptr, "-hdauchimedump") ||
        HdaBootArgIsEnabledToken(Ptr, "-hdaucodecdump") ||
        HdaBootArgIsEnabledToken(Ptr, "-hdaudump")) {
      return TRUE;
    }
  }

  return FALSE;
}

STATIC CHAR8 *HdaConvertUtf16BootArgsInPlace(UINT8 *Buffer, UINTN Size) {
  UINTN CharCount;
  UINTN Index;

  if (Buffer == NULL || Size < sizeof(CHAR16)) return NULL;
  CharCount = Size / sizeof(CHAR16);

  /* Destination byte Index is always below the next unread UTF-16 source
     offset, so this forward conversion is safe in the original allocation. */
  for (Index = 0; Index < CharCount; Index++) {
    UINTN Offset = Index * sizeof(CHAR16);
    CHAR16 Ch = (CHAR16)((UINT16)Buffer[Offset] | ((UINT16)Buffer[Offset + 1U] << 8));
    if (Ch == L'\0') break;
    Buffer[Index] = (Ch <= 0x7FU) ? (UINT8)Ch : (UINT8)'?';
  }
  Buffer[(Index < CharCount) ? Index : CharCount] = 0;
  return (CHAR8 *)Buffer;
}

STATIC VOID HdaReadAlcIdFromBootArgs(VOID) {
  EFI_STATUS Status;
  UINT32 Attr;
  UINTN Size;
  UINTN Capacity;
  UINT8 *Buffer;
  CHAR8 *Utf16Ascii;
  UINTN AlcId;
  BOOLEAN AlcIdFoundAscii;
  BOOLEAN DumpFoundAscii;

  mHdaBootArgsAlcIdValid = FALSE;
  mHdaBootArgsAlcId = 0;
  mHdaBootArgsAlcIdSource = "none";
  mHdaBootArgsCodecDumpRequested = FALSE;

  if (gRT == NULL) {
    HdaLog("[HDAU] alcid: runtime services unavailable; using automatic codec profile");
    return;
  }

  Attr = 0;
  Size = 0;
  Status = gRT->GetVariable(L"boot-args", &gAppleBootVariableGuid, &Attr, &Size, NULL);
  if (Status != EFI_BUFFER_TOO_SMALL || Size == 0 || Size > 4096) {
    HdaLog("[HDAU] alcid: boot-args unavailable status=%r; using automatic codec profile", Status);
    return;
  }

  Capacity = Size;
  Buffer = AllocateZeroPool(Capacity + sizeof(CHAR16));
  if (Buffer == NULL) {
    HdaLog("[HDAU] alcid: boot-args allocation failed; using automatic codec profile");
    return;
  }

  Attr = 0;
  Size = Capacity;
  Status = gRT->GetVariable(L"boot-args", &gAppleBootVariableGuid, &Attr, &Size, Buffer);
  if (Status != EFI_SUCCESS) {
    /* boot-args is parsed as untrusted text.  Do not scan a buffer returned
       with an EFI warning because its completeness is not guaranteed. */
    HdaLog("[HDAU] alcid: boot-args read was not a clean success status=%r; using automatic codec profile", Status);
    FreePool(Buffer);
    return;
  }
  if (Size > Capacity) {
    /* A successful GetVariable() must not publish more bytes than the input
       capacity. Treat a broken firmware implementation as untrusted instead
       of scanning beyond the allocation. */
    HdaLog("[HDAU] alcid: boot-args returned oversized success size=%u capacity=%u; ignoring",
           (UINT32)Size, (UINT32)Capacity);
    FreePool(Buffer);
    return;
  }

  AlcId = 0;
  AlcIdFoundAscii = HdaFindAlcIdInAscii((CONST CHAR8 *)Buffer, &AlcId);
  DumpFoundAscii = HdaFindCodecDumpRequestInAscii((CONST CHAR8 *)Buffer);

  if (AlcIdFoundAscii) {
    mHdaBootArgsAlcIdValid = TRUE;
    mHdaBootArgsAlcId = AlcId;
    mHdaBootArgsAlcIdSource = "boot-args-ascii";
  }
  if (DumpFoundAscii) {
    mHdaBootArgsCodecDumpRequested = TRUE;
    HdaLog("[HDAU] codec dump: requested by boot-args-ascii");
  }

  /* Reuse the same allocation for the UTF-16 interpretation instead of
     allocating four temporary ASCII copies for the two token searches. */
  Utf16Ascii = HdaConvertUtf16BootArgsInPlace(Buffer, Size);
  if (!mHdaBootArgsAlcIdValid && Utf16Ascii != NULL) {
    AlcId = 0;
    if (HdaFindAlcIdInAscii(Utf16Ascii, &AlcId)) {
      mHdaBootArgsAlcIdValid = TRUE;
      mHdaBootArgsAlcId = AlcId;
      mHdaBootArgsAlcIdSource = "boot-args-utf16";
    }
  }
  if (!mHdaBootArgsCodecDumpRequested && Utf16Ascii != NULL &&
      HdaFindCodecDumpRequestInAscii(Utf16Ascii)) {
    mHdaBootArgsCodecDumpRequested = TRUE;
    HdaLog("[HDAU] codec dump: requested by boot-args-utf16");
  }

  if (mHdaBootArgsAlcIdValid) {
    HdaLog("[HDAU] alcid: found alcid=%u source=%a", (UINT32)mHdaBootArgsAlcId, mHdaBootArgsAlcIdSource);
  } else {
    HdaLog("[HDAU] alcid: no alcid token in boot-args; using automatic path selection");
  }

  FreePool(Buffer);
}


STATIC BOOLEAN
HdaIsCodecDumpKey(IN CONST EFI_INPUT_KEY *Key) {
  if (Key == NULL) return FALSE;
  return (Key->UnicodeChar == HDAU_DUMP_KEY_LOWER || Key->UnicodeChar == HDAU_DUMP_KEY_UPPER);
}

STATIC VOID EFIAPI HdaDeferredCodecDumpNotify(IN EFI_EVENT Event, IN VOID *Context) {
  EFI_STATUS CleanupStatus;
  EFI_STATUS DumpStatus;
  BOOLEAN OpenedLog;
  BOOLEAN SavedFileLog;

  (VOID)Context;

  /* Quarantine is a terminal side-effect barrier, including for a request
     that may have become pending re-entrantly before a broken registration
     call returned its unusable handle. Check it before the callback-depth
     rearm path so no timer or synchronous service point can resurrect work. */
  if (mHdaBootScreenNotifyQuarantined ||
      mHdaBootScreenUnknownRegistration ||
      mHdaExitBootServicesInProgress) {
    mHdaDumpDeferredPending = FALSE;
    return;
  }

  /* Registration/unregistration are temporary ownership transitions, not
     terminal states. If a pending timer becomes dispatchable while either
     operation is still on the stack, defer it until ownership is known. */
  if (mHdaBootScreenNotifyMutationInProgress) {
    EFI_STATUS RearmStatus = HdaArmDeferredCodecDumpTimer(HDAU_DEFERRED_DUMP_RETRY_100NS);
    if (RearmStatus != EFI_SUCCESS) mHdaDumpDeferredPending = FALSE;
    return;
  }

  /* If a firmware implementation dispatched this TPL_CALLBACK timer before
     the lower-TPL key notification returned, do not enter the heavy worker
     re-entrantly. Re-arm with a bounded one-millisecond delay; if the callback
     is still active then, the same gate repeats until it has unwound. */
  if (mHdaKeyNotifyDepth != 0) {
    EFI_STATUS RearmStatus = HdaArmDeferredCodecDumpTimer(HDAU_DEFERRED_DUMP_RETRY_100NS);
    if (RearmStatus != EFI_SUCCESS) mHdaDumpDeferredPending = FALSE;
    return;
  }

  if (!mHdaDumpDeferredPending || mHdaF6DumpInProgress || mHdaF6DumpDone) return;

  /* The D-key event executes at TPL_CALLBACK, which UEFI permits for file/disk
     I/O. A pending notification must never reset or enumerate HDA hardware
     while HdaTryController still owns a stack-local playback/verb context that
     HdaCleanupAsyncPlayback cannot see. Keep the request pending and service
     it immediately after the critical section. */
  if (mHdaControllerOperationInProgress) return;

  mHdaDumpDeferredPending = FALSE;
  mHdaF6DumpInProgress = TRUE;
  OpenedLog = FALSE;
  SavedFileLog = mHdaCfg.FileLog;
  (VOID)HdaEnsureAppendLog(&OpenedLog, &SavedFileLog);

  CleanupStatus = HdaCleanupAsyncPlayback(FALSE);
  if (EFI_ERROR(CleanupStatus)) {
    /* Never reset/walk the controller while an output stream may still own
       DMA buffers. Keep the request retryable and let ExitBootServices make
       the final stop attempt. */
    HdaSetLastStatus("CodecDumpBlockedByPendingCleanup");
    HdaLog("[HDAU] D codec dump deferred: playback cleanup is not complete: %r", CleanupStatus);
    mHdaF6DumpInProgress = FALSE;
    HdaCloseAppendLog(OpenedLog, SavedFileLog);
    return;
  }

  mHdaCodecDumpRequested = TRUE;
  mHdaCodecDumpByF6 = TRUE;
  HdaSetLastStatus("CodecDumpDBootScreen");
  DumpStatus = HdaDumpAllControllersDynamic(TRUE);
  if (EFI_ERROR(DumpStatus)) {
    /* Do not consume the one-shot interactive request on a failed dump.
       Keep the key notifications installed so the user can retry after a
       transient filesystem/PCI/controller failure in the same boot. */
    HdaSetLastStatus("CodecDumpDeferredFailed");
    HdaLog("[HDAU] D codec dump failed and remains retryable: %r", DumpStatus);
    mHdaF6DumpInProgress = FALSE;
    HdaCloseAppendLog(OpenedLog, SavedFileLog);
    return;
  }
  mHdaF6DumpDone = TRUE;
  mHdaF6DumpInProgress = FALSE;
  HdaUninstallBootScreenDNotify(FALSE);

  /* UEFI explicitly permits CloseEvent() from the corresponding notify
     function. Once the one-shot dump succeeded and key registrations are
     gone, no owner remains for this timer event. Close it now instead of
     carrying a dead Boot Services allocation until ExitBootServices. */
  if (mHdaDumpDeferredEvent == Event && gBS != NULL) {
    CleanupStatus = HdaCloseEventTerminal(&mHdaDumpDeferredEvent);
    if (CleanupStatus != EFI_SUCCESS) {
      HdaLog("[HDAU] D deferred event terminal close returned unexpected status: %r", CleanupStatus);
    }
  }
  HdaCloseAppendLog(OpenedLog, SavedFileLog);
}

STATIC VOID HdaServiceDeferredCodecDumpIfSafe(VOID) {
  if (!mHdaDumpDeferredPending || mHdaControllerOperationInProgress ||
      mHdaF6DumpInProgress || mHdaF6DumpDone) {
    return;
  }

  /* A one-shot timer notification that fires while the controller critical
     section is active leaves the request pending. Invoke the same worker at
     the caller's normal TPL once the owner is gone; pending/done gates make
     any later timer callback a no-op. */
  HdaDeferredCodecDumpNotify(mHdaDumpDeferredEvent, NULL);
}

STATIC VOID HdaUninstallBootScreenDNotify(BOOLEAN CloseDeferredEvent) {
  EFI_STATUS Status;

  /* Treat unregistration as one ownership transaction. A callback that
     preempts an implementation of UnregisterKeyNotify() must observe the
     mutation barrier before it can publish/rearm deferred work. */
  mHdaBootScreenNotifyMutationInProgress = TRUE;

  if (!mHdaBootScreenNotifyQuarantined &&
      mHdaF6TextInEx != NULL &&
      mHdaF6TextInEx->UnregisterKeyNotify != NULL) {
    if (mHdaF6NotifyRegistration != NULL) {
      Status = mHdaF6TextInEx->UnregisterKeyNotify(mHdaF6TextInEx, mHdaF6NotifyRegistration);
      if (Status == EFI_SUCCESS || Status == EFI_INVALID_PARAMETER) {
        mHdaF6NotifyRegistration = NULL;
      } else {
        if (!EFI_ERROR(Status)) Status = EFI_DEVICE_ERROR;
        /*
         * The specification defines only SUCCESS/INVALID_PARAMETER here.
         * An undocumented result has ambiguous removal state; never retry the
         * token. Quarantine the callback so a still-installed registration is
         * side-effect-free.
         */
        mHdaBootScreenNotifyQuarantined = TRUE;
        mHdaF6NotifyRegistration = NULL;
        HdaLog("[HDAU] D notify lower-case unregister returned undocumented status; callback quarantined: %r", Status);
      }
    }

    if (mHdaDNotifyRegistrationUpper != NULL && !mHdaBootScreenNotifyQuarantined) {
      Status = mHdaF6TextInEx->UnregisterKeyNotify(mHdaF6TextInEx, mHdaDNotifyRegistrationUpper);
      if (Status == EFI_SUCCESS || Status == EFI_INVALID_PARAMETER) {
        mHdaDNotifyRegistrationUpper = NULL;
      } else {
        if (!EFI_ERROR(Status)) Status = EFI_DEVICE_ERROR;
        mHdaBootScreenNotifyQuarantined = TRUE;
        mHdaDNotifyRegistrationUpper = NULL;
        HdaLog("[HDAU] D notify upper-case unregister returned undocumented status; callback quarantined: %r", Status);
      }
    }
  }

  if (mHdaBootScreenNotifyQuarantined) {
    /* Tokens are intentionally forgotten: retry would be unsafe. */
    mHdaF6NotifyRegistration = NULL;
    mHdaDNotifyRegistrationUpper = NULL;
  }

  if (mHdaF6NotifyRegistration == NULL && mHdaDNotifyRegistrationUpper == NULL) {
    mHdaF6TextInEx = NULL;
  }

  if (CloseDeferredEvent && mHdaDumpDeferredEvent != NULL && gBS != NULL) {
    Status = HdaCloseEventTerminal(&mHdaDumpDeferredEvent);
    if (Status != EFI_SUCCESS) {
      HdaLog("[HDAU] D deferred event close returned undocumented status after terminal close: %r", Status);
    }
  }

  /* Publish the completed teardown only after token removal and optional
     event close are both terminal. */
  mHdaBootScreenNotifyMutationInProgress = FALSE;
}

STATIC VOID HdaInstallBootScreenDNotify(VOID) {
  EFI_STATUS Status;
  EFI_EVENT DeferredEvent;
  VOID *Registration;
  EFI_KEY_DATA KeyData;
  CONST CHAR8 *InputSource;

  if (!mHdaCfg.CodecDumpOnF6 || !mHdaCfg.BootScreenF6 || gBS == NULL) return;
  if (mHdaBootScreenNotifyQuarantined ||
      mHdaBootScreenUnknownRegistration ||
      mHdaBootScreenNotifyMutationInProgress) return;
  if (mHdaF6NotifyRegistration != NULL || mHdaDNotifyRegistrationUpper != NULL) return;

  if (mHdaDumpDeferredEvent == NULL) {
    DeferredEvent = NULL;
    Status = gBS->CreateEvent(
                    EVT_TIMER | EVT_NOTIFY_SIGNAL,
                    TPL_CALLBACK,
                    HdaDeferredCodecDumpNotify,
                    NULL,
                    &DeferredEvent
                    );
    if (Status != EFI_SUCCESS) {
      /*
       * UEFI explicitly defines Event as undefined on CreateEvent() failure.
       * Never close, compare, publish, or otherwise consume that OUT value.
       */
      DeferredEvent = NULL;
      if (!EFI_ERROR(Status)) Status = EFI_DEVICE_ERROR;
      HdaLog("[HDAU] D deferred dump event unavailable: %r", Status);
      return;
    }
    if (DeferredEvent == NULL) {
      HdaLog("[HDAU] D deferred dump event violated UEFI success contract");
      return;
    }
    mHdaDumpDeferredEvent = DeferredEvent;
  }

  mHdaF6TextInEx = NULL;
  InputSource = "none";
  Status = EFI_NOT_FOUND;
  if (gST != NULL && gST->ConsoleInHandle != NULL) {
    Status = gBS->HandleProtocol(
                    gST->ConsoleInHandle,
                    &gEfiSimpleTextInputExProtocolGuid,
                    (VOID **)&mHdaF6TextInEx
                    );
    if (Status == EFI_SUCCESS && mHdaF6TextInEx != NULL) {
      InputSource = "console-in-handle";
    } else {
      mHdaF6TextInEx = NULL;
    }
  }
  if (Status != EFI_SUCCESS || mHdaF6TextInEx == NULL) {
    mHdaF6TextInEx = NULL;
    Status = gBS->LocateProtocol(
                    &gEfiSimpleTextInputExProtocolGuid,
                    NULL,
                    (VOID **)&mHdaF6TextInEx
                    );
    if (Status == EFI_SUCCESS && mHdaF6TextInEx != NULL) {
      InputSource = "locate-protocol-fallback";
    } else {
      mHdaF6TextInEx = NULL;
    }
  }
  if (Status != EFI_SUCCESS ||
      mHdaF6TextInEx == NULL ||
      mHdaF6TextInEx->RegisterKeyNotify == NULL) {
    if (Status == EFI_SUCCESS) Status = EFI_UNSUPPORTED;
    else if (!EFI_ERROR(Status)) Status = EFI_DEVICE_ERROR;
    HdaLog("[HDAU] D boot-screen notify unavailable: %r", Status);
    HdaUninstallBootScreenDNotify(TRUE);
    return;
  }

  ZeroMem(&KeyData, sizeof(KeyData));
  KeyData.Key.UnicodeChar = HDAU_DUMP_KEY_LOWER;
  Registration = NULL;
  mHdaBootScreenNotifyMutationInProgress = TRUE;
  Status = mHdaF6TextInEx->RegisterKeyNotify(
                              mHdaF6TextInEx,
                              &KeyData,
                              HdaF6KeyNotify,
                              &Registration
                              );
  if (Status != EFI_SUCCESS) {
    /* UEFI 2.11 documents EFI_OUT_OF_RESOURCES as the only failure result for
       RegisterKeyNotify(). Any other status leaves callback ownership outside
       the protocol contract: the OUT handle is not usable, yet a broken
       implementation could already have inserted the callback. Quarantine
       that ambiguous state exactly like SUCCESS-with-NULL. */
    Registration = NULL;
    if (Status != EFI_OUT_OF_RESOURCES) {
      if (!EFI_ERROR(Status)) Status = EFI_DEVICE_ERROR;
      mHdaBootScreenUnknownRegistration = TRUE;
      mHdaDumpDeferredPending = FALSE;
      HdaLog("[HDAU] D lower-case registration returned undocumented status; unknown callback contained: %r", Status);
      HdaUninstallBootScreenDNotify(TRUE);
      return;
    }
    HdaLog("[HDAU] D boot-screen notify lower-case register failed: %r", Status);
  } else if (Registration == NULL) {
    /* UEFI requires a unique NotifyHandle on EFI_SUCCESS. The firmware may
       nevertheless have installed the callback, but without a token there is
       no safe unregister operation. Gate every future callback before closing
       the timer event, then permanently disable re-registration this boot. */
    mHdaBootScreenUnknownRegistration = TRUE;
    mHdaDumpDeferredPending = FALSE;
    HdaLog("[HDAU] D lower-case registration violated UEFI success contract; unknown callback contained");
    HdaUninstallBootScreenDNotify(TRUE);
    return;
  } else {
    mHdaF6NotifyRegistration = Registration;
  }

  /* Keep one mutation transaction across both key registrations. This avoids
     exposing a half-installed lower/upper key pair between firmware calls. */
  ZeroMem(&KeyData, sizeof(KeyData));
  KeyData.Key.UnicodeChar = HDAU_DUMP_KEY_UPPER;
  Registration = NULL;
  Status = mHdaF6TextInEx->RegisterKeyNotify(
                              mHdaF6TextInEx,
                              &KeyData,
                              HdaF6KeyNotify,
                              &Registration
                              );
  if (Status != EFI_SUCCESS) {
    Registration = NULL;
    if (Status != EFI_OUT_OF_RESOURCES) {
      if (!EFI_ERROR(Status)) Status = EFI_DEVICE_ERROR;
      mHdaBootScreenUnknownRegistration = TRUE;
      mHdaDumpDeferredPending = FALSE;
      HdaLog("[HDAU] D upper-case registration returned undocumented status; unknown callback contained: %r", Status);
      HdaUninstallBootScreenDNotify(TRUE);
      return;
    }
    HdaLog("[HDAU] D boot-screen notify upper-case register failed: %r", Status);
  } else if (Registration == NULL) {
    /* Publish the containment gate before removing the known lower-case
       registration. This closes the preemption window in which an untracked
       upper-case callback could otherwise arm an event being torn down. */
    mHdaBootScreenUnknownRegistration = TRUE;
    mHdaDumpDeferredPending = FALSE;
    HdaLog("[HDAU] D upper-case registration violated UEFI success contract; unknown callback contained");
    HdaUninstallBootScreenDNotify(TRUE);
    return;
  } else {
    mHdaDNotifyRegistrationUpper = Registration;
  }

  if (mHdaF6NotifyRegistration == NULL && mHdaDNotifyRegistrationUpper == NULL) {
    HdaUninstallBootScreenDNotify(TRUE);
    return;
  }

  /* Lower/upper registration ownership is now fully classified and any known
     tokens have been published. Only now may callbacks have side effects. */
  mHdaBootScreenNotifyMutationInProgress = FALSE;

  HdaLog("[HDAU] D boot-screen notify registered lower=%u upper=%u deferred=1 input=%a",
         mHdaF6NotifyRegistration != NULL,
         mHdaDNotifyRegistrationUpper != NULL,
         InputSource);
}



STATIC EFI_STATUS HdaCleanupAsyncPlayback(BOOLEAN ExitBootServices) {
  HDAU_CONTEXT *Context;
  BOOLEAN SavedDebug;
  BOOLEAN SavedFileLog;
  BOOLEAN SavedScreenLog;
  BOOLEAN SavedCorbRirbFallback;
  BOOLEAN HeapAllocated;
  BOOLEAN BuffersReleased;
  EFI_STATUS StopStatus;
  EFI_STATUS RestoreStatus;
  EFI_STATUS EventStatus;

  Context = mHdaAsyncContext;
  SavedDebug = mHdaCfg.Debug;
  SavedFileLog = mHdaCfg.FileLog;
  SavedScreenLog = mHdaCfg.ScreenLog;
  SavedCorbRirbFallback = mHdaCfg.CorbRirbFallback;
  StopStatus = EFI_SUCCESS;
  RestoreStatus = EFI_SUCCESS;
  EventStatus = EFI_SUCCESS;

  if (mHdaAsyncEvent != NULL && !ExitBootServices) {
    if (gBS == NULL) return EFI_NOT_READY;
    (VOID)gBS->SetTimer(mHdaAsyncEvent, TimerCancel, 0);
    EventStatus = HdaCloseEventTerminal(&mHdaAsyncEvent);
  } else if (ExitBootServices) {
    /*
     * Boot handoff callbacks must not perturb the memory map.
     * Firmware will reclaim event structures; forget local Boot Services
     * event ownership without calling CloseEvent().
     */
    mHdaAsyncEvent = NULL;
  }

  if (Context != NULL) {
    HeapAllocated = Context->HeapAllocated;
    if (ExitBootServices) {
      mHdaCfg.Debug = FALSE;
      mHdaCfg.FileLog = FALSE;
      mHdaCfg.ScreenLog = FALSE;
      mHdaCfg.CorbRirbFallback = FALSE;
    }

    if (Context->StreamOffset != 0) {
      StopStatus = HdaStopOutputStream(Context);
    }
    if (!EFI_ERROR(StopStatus)) {
      if (ExitBootServices) {
        /*
         * UEFI 2.11 requires EXIT_BOOT_SERVICES handlers to minimize external
         * services. Once DMA is proven stopped (or bus mastering is contained),
         * codec replay and PCI attribute rollback are not required for memory
         * safety; the operating system owns controller initialization next.
         * Drop the pre-boot rollback obligation instead of invoking additional
         * protocol methods during the terminal handoff.
         */
        Context->HaveOriginalPciAttributes = FALSE;
        RestoreStatus = EFI_SUCCESS;
      } else if (Context->PciDmaContainment) {
        RestoreStatus = HdaRestorePciAttributes(Context);
      } else {
        EFI_STATUS CodecRestoreStatus;
        EFI_STATUS PciRestoreStatus;

        CodecRestoreStatus = HdaRestoreCodecStateRetry(Context, "async-cleanup");
        PciRestoreStatus = EFI_SUCCESS;
        if (!EFI_ERROR(CodecRestoreStatus)) {
          PciRestoreStatus = HdaRestorePciAttributes(Context);
        }
        RestoreStatus = EFI_ERROR(CodecRestoreStatus) ? CodecRestoreStatus : PciRestoreStatus;
      }
    }

    if (ExitBootServices) {
      mHdaCfg.Debug = SavedDebug;
      mHdaCfg.FileLog = SavedFileLog;
      mHdaCfg.ScreenLog = SavedScreenLog;
      mHdaCfg.CorbRirbFallback = SavedCorbRirbFallback;
    }

    if (EFI_ERROR(StopStatus)) {
      mHdaAsyncContext = ExitBootServices ? NULL : Context;
      if (!ExitBootServices) {
        HdaLog("[HDAU] retained DMA context cleanup still pending: %r", StopStatus);
      }
      return StopStatus;
    }

    if (!ExitBootServices) {
      /*
       * CloseEvent() is terminal. Even an undocumented firmware status does
       * not authorize reusing/reclosing the event. The callback can no longer
       * be invoked through that event, so its context may be released.
       */
      BuffersReleased = HdaFreePlaybackBuffers(Context);
      if (EFI_ERROR(RestoreStatus) || !BuffersReleased) {
        if (Context->PostPlaybackCleanupStatus == EFI_SUCCESS) {
          Context->PostPlaybackCleanupStatus = EFI_ERROR(RestoreStatus)
                                                 ? RestoreStatus
                                                 : EFI_DEVICE_ERROR;
        }
        mHdaAsyncContext = Context;
        HdaLog("[HDAU] cleanup context retained for resource retry: restore=%r buffersReleased=%u event=%r cleanup=%r",
               RestoreStatus, BuffersReleased ? 1 : 0, EventStatus, Context->PostPlaybackCleanupStatus);
        if (EFI_ERROR(RestoreStatus)) return RestoreStatus;
        return EFI_DEVICE_ERROR;
      }

      mHdaAsyncContext = NULL;
      if (HeapAllocated) {
        FreePool(Context);
      } else if (Context == &mHdaRetainedContext) {
        ZeroMem(&mHdaRetainedContext, sizeof(mHdaRetainedContext));
        mHdaRetainedContextValid = FALSE;
      }
    } else {
      mHdaAsyncContext = NULL;
    }
  } else {
    mHdaAsyncContext = NULL;
  }

  mHdaAsyncElapsedUsec = 0;
  mHdaAsyncLastProgressUsec = 0;
  mHdaAsyncLastLpib = 0;
  mHdaAsyncHaveLpibBaseline = FALSE;
  mHdaAsyncMoved = FALSE;
  mHdaAsyncFault = FALSE;

  if (EFI_ERROR(RestoreStatus)) return RestoreStatus;
  if (EFI_ERROR(StopStatus)) return StopStatus;
  if (EFI_ERROR(EventStatus)) return EventStatus;
  if (StopStatus != EFI_SUCCESS) return StopStatus;
  return EventStatus;
}

STATIC VOID EFIAPI HdaExitBootServicesCleanup(IN EFI_EVENT Event, IN VOID *Context) {
  (VOID)Event;
  (VOID)Context;

  /*
   * This callback is registered in EFI_EVENT_GROUP_BEFORE_EXIT_BOOT_SERVICES.
   * UEFI 2.11 defines that group as the last opportunity to use firmware
   * interfaces in the boot environment. Keep the work synchronous and bounded:
   * stop/contain DMA only, with no delayed timer dependency and no allocation,
   * filesystem, console, NVRAM, event-registration or ownership-release work.
   * HdaStall() switches to a bounded CpuPause loop while this flag is set.
   */
  mHdaExitBootServicesInProgress = TRUE;
  mHdaDumpDeferredPending = FALSE;
  mHdaBootScreenNotifyQuarantined = TRUE;

  /*
   * Disable every diagnostic/fallback path before either hardware cleanup.
   * This is intentionally not restored: after the first ExitBootServices()
   * attempt the loader must treat Boot Services as partially shut down and
   * must not resume ordinary driver activity.
   */
  mHdaCfg.Debug = FALSE;
  mHdaCfg.FileLog = FALSE;
  mHdaCfg.ScreenLog = FALSE;
  mHdaCfg.NvramStatus = FALSE;
  mHdaCfg.CorbRirbFallback = FALSE;

  /*
   * Do not CloseEvent(), UnregisterKeyNotify(), Unmap(), FreeBuffer(),
   * FreePages() or FreePool() in this callback. Firmware/OS ownership takes
   * over Boot Services memory after a successful handoff, while a failed
   * handoff is a partial-shutdown state where only the loader should proceed
   * toward GetMemoryMap()/ExitBootServices().
   */
  mHdaF6NotifyRegistration = NULL;
  mHdaDNotifyRegistrationUpper = NULL;
  mHdaF6TextInEx = NULL;
  mHdaDumpDeferredEvent = NULL;
  mHdaExitBootServicesEvent = NULL;

  (VOID)HdaCleanupAsyncPlayback(TRUE);
  (VOID)HdaCleanupRetainedCorbRirb(TRUE);
}


STATIC EFI_STATUS HdaInstallExitBootServicesCleanup(VOID) {
  EFI_STATUS Status;
  EFI_EVENT CleanupEvent;

  if (mHdaExitBootServicesEvent != NULL) return EFI_SUCCESS;
  if (gBS == NULL) return EFI_NOT_READY;

  CleanupEvent = NULL;
  Status = gBS->CreateEventEx(
                  EVT_NOTIFY_SIGNAL,
                  TPL_CALLBACK,
                  HdaExitBootServicesCleanup,
                  NULL,
                  &gEfiEventBeforeExitBootServicesGuid,
                  &CleanupEvent
                  );
  if (Status != EFI_SUCCESS) {
    /* Event is explicitly undefined on CreateEvent() failure. */
    CleanupEvent = NULL;
    if (!EFI_ERROR(Status)) Status = EFI_DEVICE_ERROR;
    HdaLog("[HDAU] BeforeExitBootServices cleanup event unavailable: %r", Status);
    return Status;
  }
  if (CleanupEvent == NULL) {
    HdaLog("[HDAU] BeforeExitBootServices cleanup event violated UEFI success contract");
    return EFI_DEVICE_ERROR;
  }

  mHdaExitBootServicesEvent = CleanupEvent;
  HdaLog("[HDAU] BeforeExitBootServices cleanup event registered");
  return EFI_SUCCESS;
}

STATIC BOOLEAN HdaPollForF6(UINTN PollMs) {
  EFI_STATUS Status;
  EFI_SIMPLE_TEXT_INPUT_EX_PROTOCOL *TextInEx;
  EFI_KEY_DATA KeyData;
  EFI_INPUT_KEY Key;
  UINTN Elapsed;
  UINTN StepMs;
  UINTN DelayMs;
  UINTN KeyReads;

  if (!mHdaCfg.CodecDumpOnF6) return FALSE;

  TextInEx = NULL;
  Status = EFI_NOT_FOUND;
  if (gST != NULL && gST->ConsoleInHandle != NULL) {
    Status = gBS->HandleProtocol(gST->ConsoleInHandle,
                                 &gEfiSimpleTextInputExProtocolGuid,
                                 (VOID **)&TextInEx);
    if (Status != EFI_SUCCESS) TextInEx = NULL;
  }
  if (TextInEx == NULL) {
    Status = gBS->LocateProtocol(&gEfiSimpleTextInputExProtocolGuid, NULL, (VOID **)&TextInEx);
    if (Status != EFI_SUCCESS) TextInEx = NULL;
  }

  StepMs = 25;
  if (PollMs == 0) PollMs = 1;

  Elapsed = 0;
  for (;;) {
    if (TextInEx != NULL && TextInEx->ReadKeyStrokeEx != NULL) {
      ZeroMem(&KeyData, sizeof(KeyData));
      for (KeyReads = 0; KeyReads < HDAU_MAX_INPUT_EVENTS_PER_POLL; KeyReads++) {
        Status = TextInEx->ReadKeyStrokeEx(TextInEx, &KeyData);
        if (Status != EFI_SUCCESS) break;
        if (HdaIsCodecDumpKey(&KeyData.Key)) {
          return TRUE;
        }
        ZeroMem(&KeyData, sizeof(KeyData));
      }
      if (KeyReads == HDAU_MAX_INPUT_EVENTS_PER_POLL && mHdaCfg.Debug) {
        HdaLog("[HDAU] D-key extended input drain budget reached; yielding to timeout loop");
      }
    }

    if (gST != NULL && gST->ConIn != NULL) {
      ZeroMem(&Key, sizeof(Key));
      for (KeyReads = 0; KeyReads < HDAU_MAX_INPUT_EVENTS_PER_POLL; KeyReads++) {
        Status = gST->ConIn->ReadKeyStroke(gST->ConIn, &Key);
        if (Status != EFI_SUCCESS) break;
        if (HdaIsCodecDumpKey(&Key)) {
          return TRUE;
        }
        ZeroMem(&Key, sizeof(Key));
      }
      if (KeyReads == HDAU_MAX_INPUT_EVENTS_PER_POLL && mHdaCfg.Debug) {
        HdaLog("[HDAU] D-key console input drain budget reached; yielding to timeout loop");
      }
    }

    if (Elapsed >= PollMs) break;
    DelayMs = PollMs - Elapsed;
    if (DelayMs > StepMs) DelayMs = StepMs;
    HdaStall(DelayMs * 1000);
    Elapsed += DelayMs;
  }

  return FALSE;
}

STATIC VOID HdaDebugInit(EFI_HANDLE ImageHandle) {
  EFI_STATUS Status;
  EFI_LOADED_IMAGE_PROTOCOL *LoadedImage;
  EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *Fs;
  EFI_FILE_HANDLE Root;

  mHdaImageHandle = ImageHandle;
  LoadedImage = NULL;
  Fs = NULL;
  Root = NULL;

  Status = gBS->HandleProtocol(ImageHandle, &gEfiLoadedImageProtocolGuid, (VOID **)&LoadedImage);
  if (Status != EFI_SUCCESS || LoadedImage == NULL) return;

  Status = gBS->HandleProtocol(LoadedImage->DeviceHandle, &gEfiSimpleFileSystemProtocolGuid, (VOID **)&Fs);
  if (Status != EFI_SUCCESS || Fs == NULL) return;

  Status = Fs->OpenVolume(Fs, &Root);
  if (Status != EFI_SUCCESS) {
    /* Root is an OUT value and is consumed only after exact success. */
    Root = NULL;
    return;
  }
  if (Root == NULL) return;

  if (mHdaCfg.ReadConfig) {
    (VOID)HdaReadConfigFromPath(Root, L"\\EFI\\HDAUniversalBootChimeDxe\\HDAUChimeRelease.ini");
    if (mHdaCfg.ReadDebugConfig) {
      (VOID)HdaReadConfigFromPath(Root, L"\\EFI\\HDAUniversalBootChimeDxe\\HDAUChimeDebug.ini");
    }
  }

  mHdaCfg.VolumeQ8 = HDAU_BOOT_CHIME_GAIN_Q8;

  if (mHdaCfg.FileLog) {
    (VOID)HdaOpenDebugLog(Root, FALSE);
  }

  HdaReadAlcIdFromBootArgs();

  mHdaCodecDumpRequested = mHdaCfg.CodecDumpAlways;
  if (mHdaCfg.DumpCodec) mHdaCodecDumpRequested = TRUE;
  if (mHdaBootArgsCodecDumpRequested) {
    mHdaCodecDumpRequested = TRUE;
    mHdaCfg.CodecDumpSkipsPlayback = TRUE;
  }
  if (mHdaCfg.WaitForChime) mHdaCfg.FastBoot = FALSE;

  mHdaCodecDumpByF6 = FALSE;
  if (mHdaCfg.F6PollMs != 0 && HdaPollForF6(mHdaCfg.F6PollMs)) {
    mHdaCodecDumpRequested = TRUE;
    mHdaCodecDumpByF6 = TRUE;
  }
  (VOID)HdaCloseFileHandleTerminal(&Root);

  HdaLog("===== HDAUniversalBootChimeDxe " HDAU_CHIME_VERSION_ASCII " =====");
  HdaLog("[HDAU] Debug=%u FileLog=%u ScreenLog=%u NvramStatus=%u ProbeOnly=%u NoPlayback=%u DumpCodec=%u PreferIntel=%u AppleALCPolicy=%u PreferKnownPins=%u AllowDigitalPins=%u SingleShot=%u CodecDumpOnD=%u CodecDumpAlways=%u CodecDumpRequested=%u CodecDumpByD=%u CodecDumpSkipsPlayback=%u BootScreenD=%u FastBoot=%u WaitForChime=%u ReadConfig=%u ReadDebugConfig=%u LowLatency=%u QuickPath=%u LegacyRealtekFanout=%u MobileRealtekFanout=%u BroadRealtekFanout=%u RealtekGpioAmp=%u RealtekLineOutPinctl=%u RealtekMultiDacStream=%u RealtekAppleAlcCombos=%u RealtekCodecWarmReset=%u RealtekLegacyPinDrive=%u FullPathMap=%u RouteFallback=%u PinSensePolicy=%u TransactionalRestore=%u CorbRirbFallback=%u PcmFormatFallback=%u DigitalPipeline=%u DigitalInfoFrame=%u ExactProfileInit=%u AlcIdValid=%u AlcId=%u AlcIdSource=%a DPollMs=%u StopAfterStage=%u StallMs=%u ForceCodec=0x%x ForcePin=0x%x ForceDac=0x%x VolumeQ8=%u",
         mHdaCfg.Debug, mHdaCfg.FileLog, mHdaCfg.ScreenLog, mHdaCfg.NvramStatus,
         mHdaCfg.ProbeOnly, mHdaCfg.NoPlayback, mHdaCfg.DumpCodec, mHdaCfg.PreferIntel,
         mHdaCfg.AppleALCPolicy, mHdaCfg.PreferKnownPins, mHdaCfg.AllowDigitalPins, mHdaCfg.SingleShot,
         mHdaCfg.CodecDumpOnF6, mHdaCfg.CodecDumpAlways, mHdaCodecDumpRequested, mHdaCodecDumpByF6,
         mHdaCfg.CodecDumpSkipsPlayback, mHdaCfg.BootScreenF6, mHdaCfg.FastBoot, mHdaCfg.WaitForChime,
         mHdaCfg.ReadConfig, mHdaCfg.ReadDebugConfig, mHdaCfg.LowLatency, mHdaCfg.QuickPath,
         mHdaCfg.LegacyRealtekFanout, mHdaCfg.MobileRealtekFanout, mHdaCfg.BroadRealtekFanout,
         mHdaCfg.RealtekGpioAmp, mHdaCfg.RealtekLineOutPinctl, mHdaCfg.RealtekMultiDacStream,
         mHdaCfg.RealtekAppleAlcCombos, mHdaCfg.RealtekCodecWarmReset, mHdaCfg.RealtekLegacyPinDrive,
         mHdaCfg.FullPathMap, mHdaCfg.RouteFallback, mHdaCfg.PinSensePolicy,
         mHdaCfg.TransactionalRestore, mHdaCfg.CorbRirbFallback, mHdaCfg.PcmFormatFallback,
         mHdaCfg.DigitalPipeline, mHdaCfg.DigitalInfoFrame, mHdaCfg.ExactProfileInit,
         mHdaBootArgsAlcIdValid, (UINT32)mHdaBootArgsAlcId, mHdaBootArgsAlcIdSource,
         (UINT32)mHdaCfg.F6PollMs, (UINT32)mHdaCfg.StopAfterStage, (UINT32)mHdaCfg.StallMs,
         mHdaCfg.ForceCodec, mHdaCfg.ForcePin, mHdaCfg.ForceDac, (UINT32)mHdaCfg.VolumeQ8);
}

STATIC VOID HdaDebugFinish(VOID) {
  if (mHdaLogFile != NULL) {
    HdaLog("[HDAU] Debug finish at stage %u", (UINT32)mHdaLastStage);
    (VOID)HdaCloseTrackedLogFile(TRUE);
  }
}


STATIC BOOLEAN HdaReadPciInfo(EFI_PCI_IO_PROTOCOL *PciIo, HDAU_PCI_INFO *Info) {
  EFI_STATUS Status;
  PCI_TYPE00 Pci;

  if (PciIo == NULL || Info == NULL) return FALSE;
  ZeroMem(Info, sizeof(*Info));
  ZeroMem(&Pci, sizeof(Pci));

  Status = PciIo->Pci.Read(PciIo, EfiPciIoWidthUint32, 0, sizeof(Pci) / sizeof(UINT32), &Pci);
  if (Status != EFI_SUCCESS) return FALSE;

  Info->VendorId = Pci.Hdr.VendorId;
  Info->DeviceId = Pci.Hdr.DeviceId;
  Info->ClassCode[0] = Pci.Hdr.ClassCode[0];
  Info->ClassCode[1] = Pci.Hdr.ClassCode[1];
  Info->ClassCode[2] = Pci.Hdr.ClassCode[2];

  Status = PciIo->GetLocation(PciIo, &Info->Segment, &Info->Bus, &Info->Device, &Info->Function);
  return (Status == EFI_SUCCESS);
}

STATIC EFI_STATUS HdaNormalizeExactSuccess(EFI_STATUS Status) {
  if (Status == EFI_SUCCESS) return EFI_SUCCESS;
  return EFI_ERROR(Status) ? Status : EFI_DEVICE_ERROR;
}

STATIC EFI_STATUS HdaPciFlushExact(EFI_PCI_IO_PROTOCOL *PciIo) {
  if (PciIo == NULL || PciIo->Flush == NULL) return EFI_INVALID_PARAMETER;
  return HdaNormalizeExactSuccess(PciIo->Flush(PciIo));
}

STATIC EFI_STATUS HdaMemRead8(EFI_PCI_IO_PROTOCOL *PciIo, UINTN Offset, UINT8 *Value) {
  EFI_STATUS Status;
  if (PciIo == NULL || Value == NULL) return EFI_INVALID_PARAMETER;
  Status = PciIo->Mem.Read(PciIo, EfiPciIoWidthUint8, 0, Offset, 1, Value);
  return HdaNormalizeExactSuccess(Status);
}

STATIC EFI_STATUS HdaMemRead16(EFI_PCI_IO_PROTOCOL *PciIo, UINTN Offset, UINT16 *Value) {
  EFI_STATUS Status;
  if (PciIo == NULL || Value == NULL) return EFI_INVALID_PARAMETER;
  Status = PciIo->Mem.Read(PciIo, EfiPciIoWidthUint16, 0, Offset, 1, Value);
  return HdaNormalizeExactSuccess(Status);
}

STATIC EFI_STATUS HdaRequireNoRunningStreamsBeforeBusMasterEnable(
  EFI_PCI_IO_PROTOCOL *PciIo
) {
  EFI_STATUS Status;
  UINT16 Gcap;
  UINT8 Iss;
  UINT8 Oss;
  UINT8 Bss;
  UINT8 TotalStreams;
  UINT8 StreamIndex;
  UINT8 Ctl;
  UINT16 StreamOffset;

  if (PciIo == NULL) return EFI_INVALID_PARAMETER;

  Gcap = 0;
  Status = HdaMemRead16(PciIo, HDA_REG_GCAP, &Gcap);
  if (EFI_ERROR(Status)) return Status;

  Iss = (UINT8)((Gcap >> 8) & 0x0FU);
  Oss = (UINT8)((Gcap >> 12) & 0x0FU);
  Bss = (UINT8)((Gcap >> HDA_GCAP_BSS_SHIFT) & HDA_GCAP_BSS_MASK);
  TotalStreams = (UINT8)(Iss + Oss + Bss);

  for (StreamIndex = 0; StreamIndex < TotalStreams; StreamIndex++) {
    StreamOffset = (UINT16)(HDA_REG_SD_BASE + ((UINT16)StreamIndex * HDA_SD_SIZE));
    Ctl = 0;
    Status = HdaMemRead8(PciIo, StreamOffset + HDA_SD_CTL, &Ctl);
    if (EFI_ERROR(Status)) return Status;
    if ((Ctl & HDA_SD_CTL_RUN) != 0) {
      HdaLog("[HDAU] BUS_MASTER enable refused: foreign/running HDA stream descriptor at 0x%x", StreamOffset);
      return EFI_ACCESS_DENIED;
    }
  }

  return EFI_SUCCESS;
}

STATIC EFI_STATUS HdaMemRead32(EFI_PCI_IO_PROTOCOL *PciIo, UINTN Offset, UINT32 *Value) {
  EFI_STATUS Status;
  if (PciIo == NULL || Value == NULL) return EFI_INVALID_PARAMETER;
  Status = PciIo->Mem.Read(PciIo, EfiPciIoWidthUint32, 0, Offset, 1, Value);
  return HdaNormalizeExactSuccess(Status);
}

STATIC EFI_STATUS HdaMemWrite8(EFI_PCI_IO_PROTOCOL *PciIo, UINTN Offset, UINT8 Value) {
  EFI_STATUS Status;
  if (PciIo == NULL) return EFI_INVALID_PARAMETER;
  Status = PciIo->Mem.Write(PciIo, EfiPciIoWidthUint8, 0, Offset, 1, &Value);
  return HdaNormalizeExactSuccess(Status);
}

STATIC EFI_STATUS HdaMemWrite16(EFI_PCI_IO_PROTOCOL *PciIo, UINTN Offset, UINT16 Value) {
  EFI_STATUS Status;
  if (PciIo == NULL) return EFI_INVALID_PARAMETER;
  Status = PciIo->Mem.Write(PciIo, EfiPciIoWidthUint16, 0, Offset, 1, &Value);
  return HdaNormalizeExactSuccess(Status);
}

STATIC EFI_STATUS HdaMemWrite32(EFI_PCI_IO_PROTOCOL *PciIo, UINTN Offset, UINT32 Value) {
  EFI_STATUS Status;
  if (PciIo == NULL) return EFI_INVALID_PARAMETER;
  Status = PciIo->Mem.Write(PciIo, EfiPciIoWidthUint32, 0, Offset, 1, &Value);
  return HdaNormalizeExactSuccess(Status);
}

STATIC EFI_STATUS HdaRequireControllerResetQuiescent(EFI_PCI_IO_PROTOCOL *PciIo) {
  EFI_STATUS Status;
  UINT8 CorbCtl;
  UINT8 RirbCtl;
  UINT16 Gcap;
  UINT8 Iss;
  UINT8 Oss;
  UINT8 Bss;
  UINT8 TotalStreams;
  UINT8 StreamIndex;
  UINT8 Ctl;
  UINT16 StreamOffset;

  if (PciIo == NULL) return EFI_INVALID_PARAMETER;

  CorbCtl = 0;
  RirbCtl = 0;
  Status = HdaMemRead8(PciIo, HDA_REG_CORBCTL, &CorbCtl);
  if (EFI_ERROR(Status)) return Status;
  Status = HdaMemRead8(PciIo, HDA_REG_RIRBCTL, &RirbCtl);
  if (EFI_ERROR(Status)) return Status;
  if ((CorbCtl & HDA_CORBCTL_DMA_ENABLE) != 0 ||
      (RirbCtl & HDA_RIRBCTL_DMA_ENABLE) != 0) {
    HdaLog("[HDAU] controller reset refused: CORB/RIRB DMA is still running");
    return EFI_ACCESS_DENIED;
  }

  Gcap = 0;
  Status = HdaMemRead16(PciIo, HDA_REG_GCAP, &Gcap);
  if (EFI_ERROR(Status)) return Status;
  Iss = (UINT8)((Gcap >> 8) & 0x0FU);
  Oss = (UINT8)((Gcap >> 12) & 0x0FU);
  Bss = (UINT8)((Gcap >> HDA_GCAP_BSS_SHIFT) & HDA_GCAP_BSS_MASK);
  TotalStreams = (UINT8)(Iss + Oss + Bss);

  for (StreamIndex = 0; StreamIndex < TotalStreams; StreamIndex++) {
    StreamOffset = (UINT16)(HDA_REG_SD_BASE + ((UINT16)StreamIndex * HDA_SD_SIZE));
    Ctl = 0;
    Status = HdaMemRead8(PciIo, StreamOffset + HDA_SD_CTL, &Ctl);
    if (EFI_ERROR(Status)) return Status;
    if ((Ctl & HDA_SD_CTL_RUN) != 0) {
      HdaLog("[HDAU] controller reset refused: HDA stream descriptor still running at 0x%x", StreamOffset);
      return EFI_ACCESS_DENIED;
    }
  }

  return EFI_SUCCESS;
}

STATIC VOID HdaStall(UINTN Microseconds) {
  UINTN Index;
  UINTN PauseCount;

  if (Microseconds == 0) return;

  if (mHdaExitBootServicesInProgress || gBS == NULL) {
    /*
     * EXIT_BOOT_SERVICES handlers should minimize external services. Use a
     * bounded local pause between MMIO polls instead of gBS->Stall(). This is
     * a scheduling hint, not a calibrated delay; all surrounding loops remain
     * bounded by their iteration caps.
     */
    PauseCount = Microseconds;
    if (PauseCount > 1024U) PauseCount = 1024U;
    for (Index = 0; Index < PauseCount; Index++) CpuPause();
    return;
  }

  gBS->Stall(Microseconds);
}

STATIC EFI_STATUS HdaControllerReset(EFI_PCI_IO_PROTOCOL *PciIo) {
  EFI_STATUS Status;
  UINT32 Gctl;
  UINTN Index;

  if (PciIo == NULL) return EFI_INVALID_PARAMETER;
  Status = HdaMemRead32(PciIo, HDA_REG_GCTL, &Gctl);
  if (EFI_ERROR(Status)) return Status;

  /* Intel HDA requires CORB/RIRB DMA and every stream RUN bit to be read
     back clear before software asserts CRST.  Refuse to destroy active
     firmware/third-party DMA state instead of forcing a non-clean restart.
     If CRST is already 0, the controller is already in reset and the other
     MMIO registers are not required to be readable. */
  if ((Gctl & HDA_GCTL_CRST) != 0) {
    Status = HdaRequireControllerResetQuiescent(PciIo);
    if (EFI_ERROR(Status)) return Status;
  }

  Gctl &= ~(UINT32)HDA_GCTL_CRST;
  Status = HdaMemWrite32(PciIo, HDA_REG_GCTL, Gctl);
  if (EFI_ERROR(Status)) return Status;
  HdaStall(1000);

  for (Index = 0; Index < (HDAU_RESET_ASSERT_TIMEOUT_US / HDAU_POLL_GRANULARITY_US); Index++) {
    Status = HdaMemRead32(PciIo, HDA_REG_GCTL, &Gctl);
    if (EFI_ERROR(Status)) return Status;
    if ((Gctl & HDA_GCTL_CRST) == 0) break;
    HdaStall(HDAU_POLL_GRANULARITY_US);
  }
  if ((Gctl & HDA_GCTL_CRST) != 0) return EFI_TIMEOUT;

  Gctl |= HDA_GCTL_CRST;
  Status = HdaMemWrite32(PciIo, HDA_REG_GCTL, Gctl);
  if (EFI_ERROR(Status)) return Status;
  HdaStall(1000);

  for (Index = 0; Index < (HDAU_RESET_RELEASE_TIMEOUT_US / HDAU_POLL_GRANULARITY_US); Index++) {
    Status = HdaMemRead32(PciIo, HDA_REG_GCTL, &Gctl);
    if (EFI_ERROR(Status)) return Status;
    if ((Gctl & HDA_GCTL_CRST) != 0) {
      /* Codec discovery is not valid immediately when CRST becomes readable
         as 1. Intel HDA 1.0a section 4.3 requires at least 521 us from this
         observation before codecs are assumed to have registered. */
      HdaStall(HDAU_CODEC_POST_RESET_MIN_US);
      return EFI_SUCCESS;
    }
    HdaStall(HDAU_POLL_GRANULARITY_US);
  }

  return EFI_TIMEOUT;
}



STATIC EFI_STATUS HdaControllerEnsureReady(EFI_PCI_IO_PROTOCOL *PciIo) {
  EFI_STATUS Status;
  UINT32 Gctl;
  UINT16 StateSts;
  UINTN Index;

  if (!mHdaCfg.LowLatency) return HdaControllerReset(PciIo);

  Gctl = 0;
  StateSts = 0;
  Status = HdaMemRead32(PciIo, HDA_REG_GCTL, &Gctl);
  if (EFI_ERROR(Status)) return Status;

  if ((Gctl & HDA_GCTL_CRST) != 0) {
    /* Even when firmware left the controller running, this driver has only
       just observed CRST=1.  Intel HDA requires the same 521-us discovery
       window before software assumes STATESTS contains every codec request. */
    HdaStall(HDAU_CODEC_POST_RESET_MIN_US);
    Status = HdaMemRead16(PciIo, HDA_REG_STATESTS, &StateSts);
    if (EFI_ERROR(Status)) return Status;
    if (StateSts != 0) return EFI_SUCCESS;
  }

  if ((Gctl & HDA_GCTL_CRST) == 0) {
    Gctl |= HDA_GCTL_CRST;
    Status = HdaMemWrite32(PciIo, HDA_REG_GCTL, Gctl);
    if (!EFI_ERROR(Status)) {
      for (Index = 0; Index < (HDAU_RESET_RELEASE_TIMEOUT_US / HDAU_POLL_GRANULARITY_US); Index++) {
        Status = HdaMemRead32(PciIo, HDA_REG_GCTL, &Gctl);
        if (EFI_ERROR(Status)) return Status;
        if ((Gctl & HDA_GCTL_CRST) != 0) break;
        HdaStall(HDAU_POLL_GRANULARITY_US);
      }
      if ((Gctl & HDA_GCTL_CRST) != 0) {
        /* Intel HDA requires at least 521 us after observing CRST=1 before
           codec discovery or commands. The low-latency wake path must obey
           the same post-reset settle interval as a full controller reset. */
        HdaStall(HDAU_CODEC_POST_RESET_MIN_US);
        Status = HdaMemRead16(PciIo, HDA_REG_STATESTS, &StateSts);
        if (EFI_ERROR(Status)) return Status;
        if (StateSts != 0) return EFI_SUCCESS;
      }
    }
  }
  return HdaControllerReset(PciIo);
}

STATIC EFI_STATUS HdaAbortImmediateCommand(EFI_PCI_IO_PROTOCOL *PciIo) {
  EFI_STATUS Status;
  UINT8 CorbCtl;
  UINT8 RirbCtl;
  UINT16 Icis;
  UINTN Index;

  if (PciIo == NULL) return EFI_INVALID_PARAMETER;

  /* Immediate-command ICB must not be forced clear while CORB/RIRB command
     transport is enabled.  This driver never overlaps the mechanisms, so an
     enabled ring here is foreign/ambiguous ownership and must be left alone. */
  CorbCtl = 0;
  RirbCtl = 0;
  Status = HdaMemRead8(PciIo, HDA_REG_CORBCTL, &CorbCtl);
  if (EFI_ERROR(Status)) return Status;
  Status = HdaMemRead8(PciIo, HDA_REG_RIRBCTL, &RirbCtl);
  if (EFI_ERROR(Status)) return Status;
  if ((CorbCtl & HDA_CORBCTL_DMA_ENABLE) != 0 ||
      (RirbCtl & HDA_RIRBCTL_DMA_ENABLE) != 0) {
    return EFI_ACCESS_DENIED;
  }

  /* This helper is used only after this driver has asserted ICB for its own
     command. Intel HDA specifies clearing ICB after a response timeout, then
     polling until the controller reports it clear before the transport is
     reused. */
  Status = HdaMemWrite16(PciIo, HDA_REG_ICIS, 0);
  if (EFI_ERROR(Status)) return Status;

  for (Index = 0; Index < (HDAU_IMMEDIATE_BUSY_TIMEOUT_US / HDAU_IMMEDIATE_POLL_US); Index++) {
    Icis = 0;
    Status = HdaMemRead16(PciIo, HDA_REG_ICIS, &Icis);
    if (EFI_ERROR(Status)) return Status;
    if ((Icis & HDA_ICIS_ICB) == 0) {
      /* A response racing the timeout may have raised IRV while the abort
         completed. Before the 16-bit ICIS write that acknowledges IRV,
         revalidate ring ownership: that write also presents ICB=0, which
         Intel HDA forbids while CORB is active. */
      if ((Icis & HDA_ICIS_IRV) != 0) {
        CorbCtl = 0;
        RirbCtl = 0;
        Status = HdaMemRead8(PciIo, HDA_REG_CORBCTL, &CorbCtl);
        if (EFI_ERROR(Status)) return Status;
        Status = HdaMemRead8(PciIo, HDA_REG_RIRBCTL, &RirbCtl);
        if (EFI_ERROR(Status)) return Status;
        if ((CorbCtl & HDA_CORBCTL_DMA_ENABLE) != 0 ||
            (RirbCtl & HDA_RIRBCTL_DMA_ENABLE) != 0) {
          return EFI_ACCESS_DENIED;
        }
        Status = HdaMemWrite16(PciIo, HDA_REG_ICIS, HDA_ICIS_IRV);
        if (EFI_ERROR(Status)) return Status;
      }
      return EFI_SUCCESS;
    }
    HdaStall(HDAU_IMMEDIATE_POLL_US);
  }

  return EFI_TIMEOUT;
}

STATIC EFI_STATUS HdaAcknowledgeImmediateResult(EFI_PCI_IO_PROTOCOL *PciIo) {
  EFI_STATUS Status;
  UINT8 CorbCtl;
  UINT8 RirbCtl;

  if (PciIo == NULL) return EFI_INVALID_PARAMETER;

  /* A 16-bit ICIS write that acknowledges IRV also presents ICB=0. Intel HDA
     forbids writing ICB=0 while CORB is active, so revalidate both DMA engines
     immediately before acknowledging a result owned by this driver. */
  CorbCtl = 0;
  RirbCtl = 0;
  Status = HdaMemRead8(PciIo, HDA_REG_CORBCTL, &CorbCtl);
  if (EFI_ERROR(Status)) return Status;
  Status = HdaMemRead8(PciIo, HDA_REG_RIRBCTL, &RirbCtl);
  if (EFI_ERROR(Status)) return Status;
  if ((CorbCtl & HDA_CORBCTL_DMA_ENABLE) != 0 ||
      (RirbCtl & HDA_RIRBCTL_DMA_ENABLE) != 0) {
    return EFI_ACCESS_DENIED;
  }

  return HdaMemWrite16(PciIo, HDA_REG_ICIS, HDA_ICIS_IRV);
}

STATIC EFI_STATUS HdaImmediateCommandRaw(
  EFI_PCI_IO_PROTOCOL *PciIo,
  UINT8 Codec,
  UINT8 Nid,
  UINT32 VerbPayload,
  UINT32 *Response
) {
  EFI_STATUS Status;
  EFI_STATUS AbortStatus;
  EFI_STATUS ResponseStatus;
  EFI_STATUS AckStatus;
  UINT8 CorbCtl;
  UINT8 RirbCtl;
  UINT8 CorbSts;
  UINT16 Icis;
  UINT32 Command;
  UINTN Index;

  if (PciIo == NULL || Codec >= HDAU_MAX_CODECS || Nid > HDAU_MAX_NID) return EFI_INVALID_PARAMETER;

  /* Intel HDA forbids simultaneous use of Immediate Command and CORB/RIRB.
     Do not inject PIO verbs into command transport owned by firmware or
     another pre-boot agent. */
  CorbCtl = 0;
  RirbCtl = 0;
  Status = HdaMemRead8(PciIo, HDA_REG_CORBCTL, &CorbCtl);
  if (EFI_ERROR(Status)) return Status;
  Status = HdaMemRead8(PciIo, HDA_REG_RIRBCTL, &RirbCtl);
  if (EFI_ERROR(Status)) return Status;
  if ((CorbCtl & HDA_CORBCTL_DMA_ENABLE) != 0 ||
      (RirbCtl & HDA_RIRBCTL_DMA_ENABLE) != 0) {
    return EFI_ACCESS_DENIED;
  }

  /* CMEI denotes a severe CORB memory-path error for which Intel HDA requires
     controller reset before operation continues. Do not hide or clear a
     pre-existing controller fault by issuing an unrelated PIO command. */
  CorbSts = 0;
  Status = HdaMemRead8(PciIo, HDA_REG_CORBSTS, &CorbSts);
  if (EFI_ERROR(Status)) return Status;
  if ((CorbSts & HDA_CORBSTS_CMEI) != 0) return EFI_DEVICE_ERROR;

  /* Synchronous calls made by this driver never leave an owned Immediate
     Command outstanding. Therefore an entry-time ICB or IRV is foreign or
     otherwise ambiguous state. In particular, clearing a pre-existing IRV
     would discard another agent's unread response. Fail closed instead. */
  Icis = 0;
  Status = HdaMemRead16(PciIo, HDA_REG_ICIS, &Icis);
  if (EFI_ERROR(Status)) return Status;
  if ((Icis & (HDA_ICIS_ICB | HDA_ICIS_IRV)) != 0) return EFI_ACCESS_DENIED;

  /* The reads above give firmware callbacks time to change command transport
     ownership. Revalidate the rings and ICIS at the last safe point before
     writing ICOI so a stale observation cannot knowingly overwrite a foreign
     Immediate Command or overlap CORB/RIRB. */
  CorbCtl = 0;
  RirbCtl = 0;
  Status = HdaMemRead8(PciIo, HDA_REG_CORBCTL, &CorbCtl);
  if (EFI_ERROR(Status)) return Status;
  Status = HdaMemRead8(PciIo, HDA_REG_RIRBCTL, &RirbCtl);
  if (EFI_ERROR(Status)) return Status;
  if ((CorbCtl & HDA_CORBCTL_DMA_ENABLE) != 0 ||
      (RirbCtl & HDA_RIRBCTL_DMA_ENABLE) != 0) {
    return EFI_ACCESS_DENIED;
  }
  Icis = 0;
  Status = HdaMemRead16(PciIo, HDA_REG_ICIS, &Icis);
  if (EFI_ERROR(Status)) return Status;
  if ((Icis & (HDA_ICIS_ICB | HDA_ICIS_IRV)) != 0) return EFI_ACCESS_DENIED;

  /* Intel HDA PIO order: ICOI/ICW first, then assert ICB. IRV is already
     proven clear above; there is no stale response owned by this driver to
     acknowledge before launch. */
  Command = ((UINT32)Codec << 28) | ((UINT32)Nid << 20) | (VerbPayload & 0xFFFFF);
  Status = HdaMemWrite32(PciIo, HDA_REG_ICOI, Command);
  if (EFI_ERROR(Status)) return Status;
  Status = HdaMemWrite16(PciIo, HDA_REG_ICIS, HDA_ICIS_ICB);
  if (EFI_ERROR(Status)) return Status;

  for (Index = 0; Index < (HDAU_IMMEDIATE_RESPONSE_TIMEOUT_US / HDAU_IMMEDIATE_POLL_US); Index++) {
    Status = HdaMemRead16(PciIo, HDA_REG_ICIS, &Icis);
    if (EFI_ERROR(Status)) {
      /* ICB belongs to this driver after the successful launch above.  Do not
         abandon that ownership merely because a later status read failed. */
      AbortStatus = HdaAbortImmediateCommand(PciIo);
      return EFI_ERROR(AbortStatus) ? AbortStatus : Status;
    }
    if ((Icis & HDA_ICIS_ICB) == 0) {
      if ((Icis & HDA_ICIS_IRV) == 0) return EFI_NOT_READY;

      ResponseStatus = EFI_SUCCESS;
      if (Response != NULL) {
        ResponseStatus = HdaMemRead32(PciIo, HDA_REG_ICII, Response);
      }

      /* IRV belongs to this completed command even when the response read
         fails. Attempt to retire it so a recoverable MMIO read error does not
         poison the next command; transport ownership is revalidated first. */
      AckStatus = HdaAcknowledgeImmediateResult(PciIo);
      if (EFI_ERROR(AckStatus)) return AckStatus;
      return ResponseStatus;
    }
    HdaStall(HDAU_IMMEDIATE_POLL_US);
  }

  Status = HdaAbortImmediateCommand(PciIo);
  if (EFI_ERROR(Status)) return Status;
  return EFI_TIMEOUT;
}

STATIC BOOLEAN HdaCorbRirbStatePending(CONST HDAU_CORB_RIRB_STATE *State) {
  if (State == NULL) return FALSE;
  return (State->RegistersTouched ||
          State->CorbMapped || State->RirbMapped ||
          State->CorbAllocated || State->RirbAllocated ||
          State->BusMasterEnabledByFallback);
}

STATIC VOID HdaDischargeRetainedCorbRirbBusMasterRestore(
  EFI_PCI_IO_PROTOCOL *PciIo,
  UINT64 DesiredAttributes,
  UINT64 ManagedMask
) {
  if (!mHdaRetainedCorbRirbValid ||
      PciIo == NULL ||
      mHdaRetainedCorbRirbState.PciIo != PciIo ||
      !mHdaRetainedCorbRirbState.BusMasterEnabledByFallback) {
    return;
  }

  /* A fallback-owned BUS_MASTER transition remains an ownership obligation
     until a verified restore has actually put that bit back to zero. The
     caller reaches this helper only after the PCI attribute apply path has
     verified the requested managed bits. Do not infer discharge from a stale
     snapshot or from a restore that did not explicitly own BUS_MASTER. */
  if ((ManagedMask & EFI_PCI_IO_ATTRIBUTE_BUS_MASTER) == 0 ||
      (DesiredAttributes & EFI_PCI_IO_ATTRIBUTE_BUS_MASTER) != 0) {
    return;
  }

  mHdaRetainedCorbRirbState.BusMasterEnabledByFallback = FALSE;
  HdaLog("[HDAU] retained CORB/RIRB fallback BUS_MASTER ownership discharged after verified external restore");

  if (!HdaCorbRirbStatePending(&mHdaRetainedCorbRirbState)) {
    ZeroMem(&mHdaRetainedCorbRirbState, sizeof(mHdaRetainedCorbRirbState));
    mHdaRetainedCorbRirbValid = FALSE;
    HdaLog("[HDAU] retained CORB/RIRB state cleared after external PCI restore");
  }
}

STATIC EFI_STATUS HdaRequireCorbRirbSnapshotUnchanged(
  EFI_PCI_IO_PROTOCOL *PciIo,
  CONST HDAU_CORB_RIRB_STATE *State
) {
  EFI_STATUS Status;
  UINT8 CorbCtl;
  UINT8 RirbCtl;
  UINT8 CorbSize;
  UINT8 RirbSize;
  UINT8 CorbSts;
  UINT8 RirbSts;
  UINT16 CorbWp;
  UINT16 CorbRp;
  UINT16 RirbWp;
  UINT16 RintCnt;
  UINT16 Icis;
  UINT32 CorbLbase;
  UINT32 CorbUbase;
  UINT32 RirbLbase;
  UINT32 RirbUbase;

  if (PciIo == NULL || State == NULL) return EFI_INVALID_PARAMETER;

  /* Allocation/mapping can invoke firmware and leave a sizable ownership
     window after the initial snapshot. Before the first CORB/RIRB register
     write, prove that no other pre-boot agent changed any state we intend to
     restore later. A mismatch is foreign/ambiguous ownership, not permission
     to overwrite the newer configuration with our stale snapshot. */
  Status = HdaMemRead8(PciIo, HDA_REG_CORBCTL, &CorbCtl);
  if (EFI_ERROR(Status)) return Status;
  Status = HdaMemRead8(PciIo, HDA_REG_RIRBCTL, &RirbCtl);
  if (EFI_ERROR(Status)) return Status;
  Status = HdaMemRead8(PciIo, HDA_REG_CORBSIZE, &CorbSize);
  if (EFI_ERROR(Status)) return Status;
  Status = HdaMemRead8(PciIo, HDA_REG_RIRBSIZE, &RirbSize);
  if (EFI_ERROR(Status)) return Status;
  Status = HdaMemRead16(PciIo, HDA_REG_CORBWP, &CorbWp);
  if (EFI_ERROR(Status)) return Status;
  Status = HdaMemRead16(PciIo, HDA_REG_CORBRP, &CorbRp);
  if (EFI_ERROR(Status)) return Status;
  Status = HdaMemRead16(PciIo, HDA_REG_RIRBWP, &RirbWp);
  if (EFI_ERROR(Status)) return Status;
  Status = HdaMemRead16(PciIo, HDA_REG_RINTCNT, &RintCnt);
  if (EFI_ERROR(Status)) return Status;
  Status = HdaMemRead32(PciIo, HDA_REG_CORBLBASE, &CorbLbase);
  if (EFI_ERROR(Status)) return Status;
  Status = HdaMemRead32(PciIo, HDA_REG_CORBUBASE, &CorbUbase);
  if (EFI_ERROR(Status)) return Status;
  Status = HdaMemRead32(PciIo, HDA_REG_RIRBLBASE, &RirbLbase);
  if (EFI_ERROR(Status)) return Status;
  Status = HdaMemRead32(PciIo, HDA_REG_RIRBUBASE, &RirbUbase);
  if (EFI_ERROR(Status)) return Status;

  if (CorbCtl != State->OriginalCorbCtl ||
      RirbCtl != State->OriginalRirbCtl ||
      CorbSize != State->OriginalCorbSize ||
      RirbSize != State->OriginalRirbSize ||
      CorbWp != State->OriginalCorbWp ||
      CorbRp != State->OriginalCorbRp ||
      RirbWp != State->OriginalRirbWp ||
      RintCnt != State->OriginalRintCnt ||
      CorbLbase != State->OriginalCorbLbase ||
      CorbUbase != State->OriginalCorbUbase ||
      RirbLbase != State->OriginalRirbLbase ||
      RirbUbase != State->OriginalRirbUbase) {
    return EFI_ACCESS_DENIED;
  }

  /* Status is deliberately not part of the restorable snapshot because it is
     RW1C. A newly pending status therefore means ownership changed or the
     controller faulted while we were preparing; preserve it for its owner. */
  CorbSts = 0;
  RirbSts = 0;
  Status = HdaMemRead8(PciIo, HDA_REG_CORBSTS, &CorbSts);
  if (EFI_ERROR(Status)) return Status;
  Status = HdaMemRead8(PciIo, HDA_REG_RIRBSTS, &RirbSts);
  if (EFI_ERROR(Status)) return Status;
  if ((CorbSts & HDA_CORBSTS_CMEI) != 0 ||
      (RirbSts & (HDA_RIRBSTS_RINTFL | HDA_RIRBSTS_RIRBOIS)) != 0) {
    return EFI_ACCESS_DENIED;
  }

  /* Immediate Command and CORB/RIRB are mutually exclusive. Check this in the
     same last-pre-mutation ownership gate so an allocation callback cannot
     introduce a pending PIO transaction that we then disrupt. */
  Icis = 0;
  Status = HdaMemRead16(PciIo, HDA_REG_ICIS, &Icis);
  if (EFI_ERROR(Status)) return Status;
  if ((Icis & (HDA_ICIS_ICB | HDA_ICIS_IRV)) != 0) return EFI_ACCESS_DENIED;

  return EFI_SUCCESS;
}

STATIC EFI_STATUS HdaVerifyTemporaryCorbRirbProgramming(
  EFI_PCI_IO_PROTOCOL *PciIo,
  CONST HDAU_CORB_RIRB_STATE *State,
  EFI_PHYSICAL_ADDRESS CorbDevice,
  EFI_PHYSICAL_ADDRESS RirbDevice,
  UINT8 SizeSelect
) {
  EFI_STATUS Status;
  UINT8 CorbCtl;
  UINT8 RirbCtl;
  UINT8 CorbSize;
  UINT8 RirbSize;
  UINT16 CorbWp;
  UINT16 CorbRp;
  UINT16 RirbWp;
  UINT16 RintCnt;
  UINT32 CorbLbase;
  UINT32 CorbUbase;
  UINT32 RirbLbase;
  UINT32 RirbUbase;

  if (PciIo == NULL || State == NULL) return EFI_INVALID_PARAMETER;

  CorbCtl = 0;
  RirbCtl = 0;
  CorbSize = 0;
  RirbSize = 0;
  CorbWp = 0;
  CorbRp = 0;
  RirbWp = 0;
  RintCnt = 0;
  CorbLbase = 0;
  CorbUbase = 0;
  RirbLbase = 0;
  RirbUbase = 0;

  Status = HdaMemRead8(PciIo, HDA_REG_CORBCTL, &CorbCtl);
  if (EFI_ERROR(Status)) return Status;
  Status = HdaMemRead8(PciIo, HDA_REG_RIRBCTL, &RirbCtl);
  if (EFI_ERROR(Status)) return Status;
  Status = HdaMemRead8(PciIo, HDA_REG_CORBSIZE, &CorbSize);
  if (EFI_ERROR(Status)) return Status;
  Status = HdaMemRead8(PciIo, HDA_REG_RIRBSIZE, &RirbSize);
  if (EFI_ERROR(Status)) return Status;
  Status = HdaMemRead16(PciIo, HDA_REG_CORBWP, &CorbWp);
  if (EFI_ERROR(Status)) return Status;
  Status = HdaMemRead16(PciIo, HDA_REG_CORBRP, &CorbRp);
  if (EFI_ERROR(Status)) return Status;
  Status = HdaMemRead16(PciIo, HDA_REG_RIRBWP, &RirbWp);
  if (EFI_ERROR(Status)) return Status;
  Status = HdaMemRead16(PciIo, HDA_REG_RINTCNT, &RintCnt);
  if (EFI_ERROR(Status)) return Status;
  Status = HdaMemRead32(PciIo, HDA_REG_CORBLBASE, &CorbLbase);
  if (EFI_ERROR(Status)) return Status;
  Status = HdaMemRead32(PciIo, HDA_REG_CORBUBASE, &CorbUbase);
  if (EFI_ERROR(Status)) return Status;
  Status = HdaMemRead32(PciIo, HDA_REG_RIRBLBASE, &RirbLbase);
  if (EFI_ERROR(Status)) return Status;
  Status = HdaMemRead32(PciIo, HDA_REG_RIRBUBASE, &RirbUbase);
  if (EFI_ERROR(Status)) return Status;

  if (CorbCtl != (UINT8)(State->OriginalCorbCtl & HDA_CORBCTL_RSVDP_MASK) ||
      RirbCtl != (UINT8)(State->OriginalRirbCtl & HDA_RIRBCTL_RSVDP_MASK) ||
      CorbSize != (UINT8)((State->OriginalCorbSize & (UINT8)~HDA_RING_SIZE_SELECT_MASK) | SizeSelect) ||
      RirbSize != (UINT8)((State->OriginalRirbSize & (UINT8)~HDA_RING_SIZE_SELECT_MASK) | SizeSelect) ||
      CorbWp != (UINT16)(State->OriginalCorbWp & HDA_CORBWP_RSVDP_MASK) ||
      CorbRp != (UINT16)(State->OriginalCorbRp & HDA_CORBRP_RSVDP_MASK) ||
      RirbWp != (UINT16)(State->OriginalRirbWp & HDA_RIRBWP_RSVDP_MASK) ||
      RintCnt != (UINT16)((State->OriginalRintCnt & HDA_RINTCNT_RSVDP_MASK) | 1U) ||
      CorbLbase != (UINT32)(CorbDevice & 0xFFFFFFFFU) ||
      CorbUbase != (State->Supports64BitDma
                     ? (UINT32)(CorbDevice >> 32)
                     : State->OriginalCorbUbase) ||
      RirbLbase != (UINT32)(RirbDevice & 0xFFFFFFFFU) ||
      RirbUbase != (State->Supports64BitDma
                     ? (UINT32)(RirbDevice >> 32)
                     : State->OriginalRirbUbase)) {
    HdaLog("[HDAU] CORB/RIRB programming read-back mismatch; refusing DMA enable");
    return EFI_DEVICE_ERROR;
  }

  return EFI_SUCCESS;
}

STATIC EFI_STATUS HdaVerifyCorbRirbDmaEnabled(
  EFI_PCI_IO_PROTOCOL *PciIo,
  CONST HDAU_CORB_RIRB_STATE *State
) {
  EFI_STATUS Status;
  UINT8 CorbCtl;
  UINT8 RirbCtl;
  UINT8 ExpectedCorbCtl;
  UINT8 ExpectedRirbCtl;

  if (PciIo == NULL || State == NULL) return EFI_INVALID_PARAMETER;

  CorbCtl = 0;
  RirbCtl = 0;
  Status = HdaMemRead8(PciIo, HDA_REG_CORBCTL, &CorbCtl);
  if (EFI_ERROR(Status)) return Status;
  Status = HdaMemRead8(PciIo, HDA_REG_RIRBCTL, &RirbCtl);
  if (EFI_ERROR(Status)) return Status;

  ExpectedCorbCtl = (UINT8)((State->OriginalCorbCtl & HDA_CORBCTL_RSVDP_MASK) | HDA_CORBCTL_DMA_ENABLE);
  ExpectedRirbCtl = (UINT8)((State->OriginalRirbCtl & HDA_RIRBCTL_RSVDP_MASK) | HDA_RIRBCTL_DMA_ENABLE);
  if (CorbCtl != ExpectedCorbCtl || RirbCtl != ExpectedRirbCtl) {
    HdaLog("[HDAU] CORB/RIRB DMA enable read-back mismatch: corb=0x%x rirb=0x%x", CorbCtl, RirbCtl);
    return EFI_DEVICE_ERROR;
  }

  return EFI_SUCCESS;
}

STATIC EFI_STATUS HdaResetCorbReadPointer(EFI_PCI_IO_PROTOCOL *PciIo) {
  EFI_STATUS Status;
  UINT16 CorbRp;
  UINTN Index;

  if (PciIo == NULL) return EFI_INVALID_PARAMETER;

  Status = HdaMemRead16(PciIo, HDA_REG_CORBRP, &CorbRp);
  if (EFI_ERROR(Status)) return Status;
  Status = HdaMemWrite16(
             PciIo, HDA_REG_CORBRP,
             (UINT16)((CorbRp & HDA_CORBRP_RSVDP_MASK) | HDA_CORBRP_RESET)
             );
  if (EFI_ERROR(Status)) return Status;
  for (Index = 0; Index < (HDAU_CORB_RIRB_TIMEOUT_US / HDAU_CORB_RIRB_POLL_US); Index++) {
    Status = HdaMemRead16(PciIo, HDA_REG_CORBRP, &CorbRp);
    if (EFI_ERROR(Status)) return Status;
    if ((CorbRp & HDA_CORBRP_RESET) != 0) break;
    HdaStall(HDAU_CORB_RIRB_POLL_US);
  }
  if (Index >= (HDAU_CORB_RIRB_TIMEOUT_US / HDAU_CORB_RIRB_POLL_US)) return EFI_TIMEOUT;

  Status = HdaMemWrite16(
             PciIo, HDA_REG_CORBRP,
             (UINT16)(CorbRp & HDA_CORBRP_RSVDP_MASK)
             );
  if (EFI_ERROR(Status)) return Status;
  for (Index = 0; Index < (HDAU_CORB_RIRB_TIMEOUT_US / HDAU_CORB_RIRB_POLL_US); Index++) {
    Status = HdaMemRead16(PciIo, HDA_REG_CORBRP, &CorbRp);
    if (EFI_ERROR(Status)) return Status;
    if ((CorbRp & HDA_CORBRP_RESET) == 0) return EFI_SUCCESS;
    HdaStall(HDAU_CORB_RIRB_POLL_US);
  }
  return EFI_TIMEOUT;
}

STATIC EFI_STATUS HdaResetRirbWritePointer(EFI_PCI_IO_PROTOCOL *PciIo) {
  EFI_STATUS Status;
  UINT16 RirbWp;

  if (PciIo == NULL) return EFI_INVALID_PARAMETER;

  /* RIRBWP[14:8] is RsvdP and must survive the write-one reset request.
     Bit 15 is write-only/always reads zero, so no acknowledgement poll is
     defined by Intel HDA for this pointer reset. */
  RirbWp = 0;
  Status = HdaMemRead16(PciIo, HDA_REG_RIRBWP, &RirbWp);
  if (EFI_ERROR(Status)) return Status;
  return HdaMemWrite16(
           PciIo, HDA_REG_RIRBWP,
           (UINT16)((RirbWp & HDA_RIRBWP_RSVDP_MASK) | HDA_RIRBWP_RESET)
           );
}

STATIC EFI_STATUS HdaCleanupCorbRirbState(
  HDAU_CORB_RIRB_STATE *State,
  BOOLEAN ExitBootServices
) {
  EFI_STATUS CleanupStatus;
  EFI_STATUS FirstCleanupError;
  UINT8 CorbCtl;
  UINT8 RirbCtl;
  BOOLEAN RingsStopped;

  if (State == NULL || State->PciIo == NULL) return EFI_INVALID_PARAMETER;

  FirstCleanupError = EFI_SUCCESS;
  CorbCtl = 0;
  RirbCtl = 0;

  if (State->RegistersTouched) {
    RingsStopped = TRUE;

    CleanupStatus = HdaMemWrite8(
                      State->PciIo, HDA_REG_CORBCTL,
                      (UINT8)(State->OriginalCorbCtl & HDA_CORBCTL_RSVDP_MASK)
                      );
    if (EFI_ERROR(CleanupStatus)) {
      RingsStopped = FALSE;
      if (!EFI_ERROR(FirstCleanupError)) FirstCleanupError = CleanupStatus;
    }
    CleanupStatus = HdaMemWrite8(
                      State->PciIo, HDA_REG_RIRBCTL,
                      (UINT8)(State->OriginalRirbCtl & HDA_RIRBCTL_RSVDP_MASK)
                      );
    if (EFI_ERROR(CleanupStatus)) {
      RingsStopped = FALSE;
      if (!EFI_ERROR(FirstCleanupError)) FirstCleanupError = CleanupStatus;
    }
    CleanupStatus = HdaPciFlushExact(State->PciIo);
    if (EFI_ERROR(CleanupStatus)) {
      RingsStopped = FALSE;
      if (!EFI_ERROR(FirstCleanupError)) FirstCleanupError = CleanupStatus;
    }

    CleanupStatus = HdaMemRead8(State->PciIo, HDA_REG_CORBCTL, &CorbCtl);
    if (EFI_ERROR(CleanupStatus)) {
      RingsStopped = FALSE;
      if (!EFI_ERROR(FirstCleanupError)) FirstCleanupError = CleanupStatus;
    }
    CleanupStatus = HdaMemRead8(State->PciIo, HDA_REG_RIRBCTL, &RirbCtl);
    if (EFI_ERROR(CleanupStatus)) {
      RingsStopped = FALSE;
      if (!EFI_ERROR(FirstCleanupError)) FirstCleanupError = CleanupStatus;
    }
    if (RingsStopped &&
        (((CorbCtl & HDA_CORBCTL_DMA_ENABLE) != 0) ||
         ((RirbCtl & HDA_RIRBCTL_DMA_ENABLE) != 0))) {
      RingsStopped = FALSE;
      if (!EFI_ERROR(FirstCleanupError)) FirstCleanupError = EFI_DEVICE_ERROR;
    }

    if (RingsStopped && ExitBootServices) {
      /*
       * The only terminal invariant required here is that both ring DMA-enable
       * bits are observed clear. Restoring base/size/pointer/status registers
       * would add a long chain of external PCI I/O operations during
       * EXIT_BOOT_SERVICES without improving DMA safety. The OS will program
       * fresh CORB/RIRB state when its HDA driver takes ownership.
       */
      State->DmaStopUnconfirmed = FALSE;
      State->RegistersTouched = FALSE;
    } else if (RingsStopped) {
      EFI_STATUS RestoreError;

      State->DmaStopUnconfirmed = FALSE;
      RestoreError = EFI_SUCCESS;
      CleanupStatus = HdaMemWrite32(State->PciIo, HDA_REG_CORBLBASE, State->OriginalCorbLbase);
      if (EFI_ERROR(CleanupStatus) && !EFI_ERROR(RestoreError)) RestoreError = CleanupStatus;
      if (State->Supports64BitDma) {
        CleanupStatus = HdaMemWrite32(State->PciIo, HDA_REG_CORBUBASE, State->OriginalCorbUbase);
        if (EFI_ERROR(CleanupStatus) && !EFI_ERROR(RestoreError)) RestoreError = CleanupStatus;
      }
      CleanupStatus = HdaMemWrite32(State->PciIo, HDA_REG_RIRBLBASE, State->OriginalRirbLbase);
      if (EFI_ERROR(CleanupStatus) && !EFI_ERROR(RestoreError)) RestoreError = CleanupStatus;
      if (State->Supports64BitDma) {
        CleanupStatus = HdaMemWrite32(State->PciIo, HDA_REG_RIRBUBASE, State->OriginalRirbUbase);
        if (EFI_ERROR(CleanupStatus) && !EFI_ERROR(RestoreError)) RestoreError = CleanupStatus;
      }
      CleanupStatus = HdaMemWrite8(State->PciIo, HDA_REG_CORBSIZE, State->OriginalCorbSize);
      if (EFI_ERROR(CleanupStatus) && !EFI_ERROR(RestoreError)) RestoreError = CleanupStatus;
      CleanupStatus = HdaMemWrite8(State->PciIo, HDA_REG_RIRBSIZE, State->OriginalRirbSize);
      if (EFI_ERROR(CleanupStatus) && !EFI_ERROR(RestoreError)) RestoreError = CleanupStatus;
      CleanupStatus = HdaResetCorbReadPointer(State->PciIo);
      if (EFI_ERROR(CleanupStatus) && !EFI_ERROR(RestoreError)) RestoreError = CleanupStatus;
      CleanupStatus = HdaMemWrite16(State->PciIo, HDA_REG_CORBWP, State->OriginalCorbWp);
      if (EFI_ERROR(CleanupStatus) && !EFI_ERROR(RestoreError)) RestoreError = CleanupStatus;
      CleanupStatus = HdaResetRirbWritePointer(State->PciIo);
      if (EFI_ERROR(CleanupStatus) && !EFI_ERROR(RestoreError)) RestoreError = CleanupStatus;
      CleanupStatus = HdaMemWrite16(State->PciIo, HDA_REG_RINTCNT, State->OriginalRintCnt);
      if (EFI_ERROR(CleanupStatus) && !EFI_ERROR(RestoreError)) RestoreError = CleanupStatus;
      CleanupStatus = HdaMemWrite8(State->PciIo, HDA_REG_CORBSTS, HDA_CORBSTS_CMEI);
      if (EFI_ERROR(CleanupStatus) && !EFI_ERROR(RestoreError)) RestoreError = CleanupStatus;
      CleanupStatus = HdaMemWrite8(State->PciIo, HDA_REG_RIRBSTS, HDA_RIRBSTS_RINTFL | HDA_RIRBSTS_RIRBOIS);
      if (EFI_ERROR(CleanupStatus) && !EFI_ERROR(RestoreError)) RestoreError = CleanupStatus;
      CleanupStatus = HdaMemWrite8(
                        State->PciIo, HDA_REG_CORBCTL,
                        ExitBootServices
                          ? (UINT8)(State->OriginalCorbCtl & (UINT8)~HDA_CORBCTL_DMA_ENABLE)
                          : State->OriginalCorbCtl
                        );
      if (EFI_ERROR(CleanupStatus) && !EFI_ERROR(RestoreError)) RestoreError = CleanupStatus;
      CleanupStatus = HdaMemWrite8(
                        State->PciIo, HDA_REG_RIRBCTL,
                        ExitBootServices
                          ? (UINT8)(State->OriginalRirbCtl & (UINT8)~HDA_RIRBCTL_DMA_ENABLE)
                          : State->OriginalRirbCtl
                        );
      if (EFI_ERROR(CleanupStatus) && !EFI_ERROR(RestoreError)) RestoreError = CleanupStatus;
      CleanupStatus = HdaPciFlushExact(State->PciIo);
      if (EFI_ERROR(CleanupStatus) && !EFI_ERROR(RestoreError)) RestoreError = CleanupStatus;

      /* The temporary rings were acquired only when both original DMA-enable
         bits were clear. Re-read both controls after the restore sequence
         before releasing mappings/pages; a broken controller or firmware
         that reports successful writes without applying them must never let
         DMA outlive the buffers backing it. */
      if (!EFI_ERROR(RestoreError)) {
        CleanupStatus = HdaMemRead8(State->PciIo, HDA_REG_CORBCTL, &CorbCtl);
        if (EFI_ERROR(CleanupStatus)) RestoreError = CleanupStatus;
      }
      if (!EFI_ERROR(RestoreError)) {
        CleanupStatus = HdaMemRead8(State->PciIo, HDA_REG_RIRBCTL, &RirbCtl);
        if (EFI_ERROR(CleanupStatus)) RestoreError = CleanupStatus;
      }
      if (!EFI_ERROR(RestoreError) &&
          (((CorbCtl & HDA_CORBCTL_DMA_ENABLE) != 0) ||
           ((RirbCtl & HDA_RIRBCTL_DMA_ENABLE) != 0))) {
        HdaLog("[HDAU] CORB/RIRB restore verification found DMA still enabled; resources retained");
        RestoreError = EFI_DEVICE_ERROR;
      }

      if (EFI_ERROR(RestoreError)) {
        if (!EFI_ERROR(FirstCleanupError)) FirstCleanupError = RestoreError;
      } else {
        State->RegistersTouched = FALSE;
        if (State->DmaContainment) {
          /* The outer controller context owns restoration of the original PCI
             attributes after the rings have been proven stopped. */
          State->DmaContainment = FALSE;
        }
      }
    } else {
      State->DmaStopUnconfirmed = TRUE;
      CleanupStatus = State->PciIo->Attributes(
                                       State->PciIo,
                                       EfiPciIoAttributeOperationDisable,
                                       EFI_PCI_IO_ATTRIBUTE_BUS_MASTER,
                                       NULL
                                       );
      if (CleanupStatus == EFI_SUCCESS) {
        CleanupStatus = HdaVerifyPciAttributeBits(
                          State->PciIo,
                          0,
                          EFI_PCI_IO_ATTRIBUTE_BUS_MASTER
                          );
      } else if (!EFI_ERROR(CleanupStatus)) {
        CleanupStatus = EFI_DEVICE_ERROR;
      }
      if (EFI_ERROR(CleanupStatus)) {
        if (!EFI_ERROR(FirstCleanupError)) FirstCleanupError = CleanupStatus;
        HdaLog("[HDAU] CORB/RIRB DMA stop could not be confirmed and PCI bus-master containment failed: %r", CleanupStatus);
      } else {
        State->DmaContainment = TRUE;
        State->BusMasterEnabledByFallback = FALSE;
        HdaLog("[HDAU] CORB/RIRB DMA stop could not be confirmed; PCI bus mastering disabled and verified, ring ownership retained");
      }
    }
  }

  /* If this fallback was the code that enabled PCI bus mastering, restore that
     exact bit after ring DMA has been proven stopped. This is intentionally
     independent of mapping/buffer release so a failed PCI attribute write can
     remain retryable without keeping dead DMA buffers alive. */
  if (!State->RegistersTouched && State->BusMasterEnabledByFallback) {
    CleanupStatus = State->PciIo->Attributes(
                                     State->PciIo,
                                     EfiPciIoAttributeOperationDisable,
                                     EFI_PCI_IO_ATTRIBUTE_BUS_MASTER,
                                     NULL
                                     );
    if (CleanupStatus == EFI_SUCCESS) {
      CleanupStatus = HdaVerifyPciAttributeBits(
                        State->PciIo,
                        0,
                        EFI_PCI_IO_ATTRIBUTE_BUS_MASTER
                        );
    } else if (!EFI_ERROR(CleanupStatus)) {
      CleanupStatus = EFI_DEVICE_ERROR;
    }
    if (EFI_ERROR(CleanupStatus)) {
      if (!EFI_ERROR(FirstCleanupError)) FirstCleanupError = CleanupStatus;
      HdaLog("[HDAU] CORB/RIRB fallback bus-master restore failed: %r", CleanupStatus);
    } else {
      State->BusMasterEnabledByFallback = FALSE;
    }
  }

  /* ExitBootServices cleanup deliberately avoids Unmap/FreeBuffer because
     those operations can perturb the memory map being committed. The retained
     ownership record remains valid until firmware reclaims boot-service data. */
  if (!ExitBootServices && !State->RegistersTouched) {
    if (State->RirbMapped) {
      CleanupStatus = State->PciIo->Unmap(State->PciIo, State->RirbMapping);
      /* PCI I/O Unmap releases mapping resources even on EFI_DEVICE_ERROR. */
      State->RirbMapped = FALSE;
      State->RirbMapping = NULL;
      if (CleanupStatus != EFI_SUCCESS) {
        if (!EFI_ERROR(CleanupStatus)) CleanupStatus = EFI_DEVICE_ERROR;
        if (!EFI_ERROR(FirstCleanupError)) FirstCleanupError = CleanupStatus;
      }
    }
    if (State->CorbMapped) {
      CleanupStatus = State->PciIo->Unmap(State->PciIo, State->CorbMapping);
      State->CorbMapped = FALSE;
      State->CorbMapping = NULL;
      if (CleanupStatus != EFI_SUCCESS) {
        if (!EFI_ERROR(CleanupStatus)) CleanupStatus = EFI_DEVICE_ERROR;
        if (!EFI_ERROR(FirstCleanupError)) FirstCleanupError = CleanupStatus;
      }
    }
    if (State->RirbAllocated && !State->RirbMapped) {
      CleanupStatus = State->PciIo->FreeBuffer(State->PciIo, State->RirbPages, State->RirbHost);
      if (CleanupStatus == EFI_SUCCESS || CleanupStatus == EFI_INVALID_PARAMETER) {
        State->RirbAllocated = FALSE;
        State->RirbHost = NULL;
        State->RirbPages = 0;
        if (CleanupStatus == EFI_INVALID_PARAMETER && !EFI_ERROR(FirstCleanupError)) {
          FirstCleanupError = CleanupStatus;
        }
      } else {
        if (!EFI_ERROR(CleanupStatus)) CleanupStatus = EFI_DEVICE_ERROR;
        if (!EFI_ERROR(FirstCleanupError)) FirstCleanupError = CleanupStatus;
      }
    }
    if (State->CorbAllocated && !State->CorbMapped) {
      CleanupStatus = State->PciIo->FreeBuffer(State->PciIo, State->CorbPages, State->CorbHost);
      if (CleanupStatus == EFI_SUCCESS || CleanupStatus == EFI_INVALID_PARAMETER) {
        State->CorbAllocated = FALSE;
        State->CorbHost = NULL;
        State->CorbPages = 0;
        if (CleanupStatus == EFI_INVALID_PARAMETER && !EFI_ERROR(FirstCleanupError)) {
          FirstCleanupError = CleanupStatus;
        }
      } else {
        if (!EFI_ERROR(CleanupStatus)) CleanupStatus = EFI_DEVICE_ERROR;
        if (!EFI_ERROR(FirstCleanupError)) FirstCleanupError = CleanupStatus;
      }
    }
  }

  return FirstCleanupError;
}

STATIC EFI_STATUS HdaCleanupRetainedCorbRirb(BOOLEAN ExitBootServices) {
  EFI_STATUS Status;

  if (!mHdaRetainedCorbRirbValid) return EFI_SUCCESS;

  Status = HdaCleanupCorbRirbState(&mHdaRetainedCorbRirbState, ExitBootServices);
  if (!ExitBootServices && !HdaCorbRirbStatePending(&mHdaRetainedCorbRirbState)) {
    ZeroMem(&mHdaRetainedCorbRirbState, sizeof(mHdaRetainedCorbRirbState));
    mHdaRetainedCorbRirbValid = FALSE;
    HdaLog("[HDAU] retained CORB/RIRB resources released after cleanup retry");
  }
  return Status;
}

STATIC EFI_STATUS HdaCorbRirbCommand(
  EFI_PCI_IO_PROTOCOL *PciIo,
  UINT8 Codec,
  UINT8 Nid,
  UINT32 VerbPayload,
  UINT32 *Response
) {
  EFI_STATUS Status;
  EFI_STATUS CleanupStatus;
  EFI_STATUS ResetStatus;
  EFI_STATUS CorbStatusRead;
  EFI_STATUS RirbStatusRead;
  HDAU_CORB_RIRB_STATE RingState;
  VOID *CorbHost;
  VOID *RirbHost;
  VOID *CorbMapping;
  VOID *RirbMapping;
  EFI_PHYSICAL_ADDRESS CorbDevice;
  EFI_PHYSICAL_ADDRESS RirbDevice;
  UINTN CorbBytes;
  UINTN RirbBytes;
  UINTN Entries;
  UINT8 SizeSelect;
  UINT32 *Corb;
  UINT64 *Rirb;
  UINT32 Command;
  UINT16 Gcap;
  UINT16 Icis;
  UINT16 RirbWp;
  UINT16 LastRirbWp;
  UINT64 PciAttributes;
  UINT8 CorbSts;
  UINT8 RirbSts;
  BOOLEAN CorbMemoryError;
  UINTN Index;

  if (PciIo == NULL || Codec >= HDAU_MAX_CODECS || Nid > HDAU_MAX_NID) return EFI_INVALID_PARAMETER;
  if (mHdaCorbRirbFallbackDisabled || mHdaRetainedCorbRirbValid) return EFI_ACCESS_DENIED;

  CorbMemoryError = FALSE;

  /* CORB/RIRB and Immediate Command are mutually exclusive in Intel HDA.
     A busy ICB or pending IRV observed before this fallback is not ours, so
     never start DMA command transport on top of foreign/ambiguous state. */
  Icis = 0;
  Status = HdaMemRead16(PciIo, HDA_REG_ICIS, &Icis);
  if (EFI_ERROR(Status)) return Status;
  if ((Icis & (HDA_ICIS_ICB | HDA_ICIS_IRV)) != 0) return EFI_ACCESS_DENIED;

  ZeroMem(&RingState, sizeof(RingState));
  RingState.PciIo = PciIo;
  CorbHost = NULL;
  RirbHost = NULL;
  CorbMapping = NULL;
  RirbMapping = NULL;
  CorbDevice = 0;
  RirbDevice = 0;

  if (EFI_ERROR(HdaMemRead8(PciIo, HDA_REG_CORBCTL, &RingState.OriginalCorbCtl)) ||
      EFI_ERROR(HdaMemRead8(PciIo, HDA_REG_RIRBCTL, &RingState.OriginalRirbCtl))) {
    return EFI_DEVICE_ERROR;
  }
  /* Never seize rings owned by firmware or another pre-boot driver. */
  if ((RingState.OriginalCorbCtl & HDA_CORBCTL_DMA_ENABLE) != 0 ||
      (RingState.OriginalRirbCtl & HDA_RIRBCTL_DMA_ENABLE) != 0) {
    return EFI_ACCESS_DENIED;
  }

  /* Status bits are RW1C. Never erase a pre-existing CORB/RIRB condition that
     may belong to firmware or another pre-boot driver. CMEI is additionally a
     severe memory-path error for which Intel HDA requires CRST before further
     controller operation. Acquire temporary rings only from a clean state. */
  CorbSts = 0;
  RirbSts = 0;
  Status = HdaMemRead8(PciIo, HDA_REG_CORBSTS, &CorbSts);
  if (EFI_ERROR(Status)) return Status;
  Status = HdaMemRead8(PciIo, HDA_REG_RIRBSTS, &RirbSts);
  if (EFI_ERROR(Status)) return Status;
  if ((CorbSts & HDA_CORBSTS_CMEI) != 0 ||
      (RirbSts & (HDA_RIRBSTS_RINTFL | HDA_RIRBSTS_RIRBOIS)) != 0) {
    return EFI_ACCESS_DENIED;
  }

  if (EFI_ERROR(HdaMemRead8(PciIo, HDA_REG_CORBSIZE, &RingState.OriginalCorbSize)) ||
      EFI_ERROR(HdaMemRead8(PciIo, HDA_REG_RIRBSIZE, &RingState.OriginalRirbSize)) ||
      EFI_ERROR(HdaMemRead16(PciIo, HDA_REG_CORBWP, &RingState.OriginalCorbWp)) ||
      EFI_ERROR(HdaMemRead16(PciIo, HDA_REG_CORBRP, &RingState.OriginalCorbRp)) ||
      EFI_ERROR(HdaMemRead16(PciIo, HDA_REG_RIRBWP, &RingState.OriginalRirbWp)) ||
      EFI_ERROR(HdaMemRead16(PciIo, HDA_REG_RINTCNT, &RingState.OriginalRintCnt)) ||
      EFI_ERROR(HdaMemRead32(PciIo, HDA_REG_CORBLBASE, &RingState.OriginalCorbLbase)) ||
      EFI_ERROR(HdaMemRead32(PciIo, HDA_REG_CORBUBASE, &RingState.OriginalCorbUbase)) ||
      EFI_ERROR(HdaMemRead32(PciIo, HDA_REG_RIRBLBASE, &RingState.OriginalRirbLbase)) ||
      EFI_ERROR(HdaMemRead32(PciIo, HDA_REG_RIRBUBASE, &RingState.OriginalRirbUbase))) {
    return EFI_DEVICE_ERROR;
  }

  /* CORBRP and RIRBWP only expose reset controls for software writes; their
     live indices cannot be reconstructed after a temporary ring reset. Use
     this fallback only when the disabled rings are already at reset position. */
  if ((RingState.OriginalCorbWp & 0x00FFU) != 0 ||
      (RingState.OriginalCorbRp & HDA_CORBRP_RESET) != 0 ||
      (RingState.OriginalCorbRp & 0x00FFU) != 0 ||
      (RingState.OriginalRirbWp & 0x00FFU) != 0) {
    return EFI_ACCESS_DENIED;
  }

  /* Do not turn on PCI bus mastering until the HDA ring engines have been
     confirmed disabled and at reset position. Otherwise a controller whose
     BME was intentionally off could resume stale DMA as soon as the PCI bit
     is enabled. Playback normally arrives with BME already on; diagnostics
     can reach this fallback with it off, so the fallback owns that temporary
     transition and restores it during cleanup. */
  PciAttributes = 0;
  Status = HdaGetPciAttributesExact(PciIo, &PciAttributes);
  if (EFI_ERROR(Status)) return Status;
  if ((PciAttributes & EFI_PCI_IO_ATTRIBUTE_BUS_MASTER) == 0) {
    Status = HdaRequireNoRunningStreamsBeforeBusMasterEnable(PciIo);
    if (EFI_ERROR(Status)) return Status;
    Status = PciIo->Attributes(
                     PciIo,
                     EfiPciIoAttributeOperationEnable,
                     EFI_PCI_IO_ATTRIBUTE_BUS_MASTER,
                     NULL
                     );
    if (Status != EFI_SUCCESS) {
      /* The pre-call snapshot says BME was clear. Even though Enable() failed,
         do not assume broken firmware made no partial hardware change. Publish
         a local cleanup obligation and let the common cleanup path disable +
         read back BUS_MASTER before returning. */
      Status = EFI_ERROR(Status) ? Status : EFI_DEVICE_ERROR;
      RingState.BusMasterEnabledByFallback = TRUE;
      goto Done;
    }
    RingState.BusMasterEnabledByFallback = TRUE;
    Status = HdaVerifyPciAttributeBits(
               PciIo,
               EFI_PCI_IO_ATTRIBUTE_BUS_MASTER,
               EFI_PCI_IO_ATTRIBUTE_BUS_MASTER
               );
    if (EFI_ERROR(Status)) goto Done;
  }

  /* Use the largest ring supported by both engines. AllocateBuffer is page
     granular for every legal ring size here, so selecting 256 entries does
     not consume additional pages but materially increases overrun margin. */
  if (((RingState.OriginalCorbSize & HDA_RING_SIZE_CAP_MASK) & HDA_RING_SIZE_CAP_256) != 0 &&
      ((RingState.OriginalRirbSize & HDA_RING_SIZE_CAP_MASK) & HDA_RING_SIZE_CAP_256) != 0) {
    Entries = 256;
    SizeSelect = HDA_RING_SIZE_SELECT_256;
  } else if (((RingState.OriginalCorbSize & HDA_RING_SIZE_CAP_MASK) & HDA_RING_SIZE_CAP_16) != 0 &&
             ((RingState.OriginalRirbSize & HDA_RING_SIZE_CAP_MASK) & HDA_RING_SIZE_CAP_16) != 0) {
    Entries = 16;
    SizeSelect = HDA_RING_SIZE_SELECT_16;
  } else if (((RingState.OriginalCorbSize & HDA_RING_SIZE_CAP_MASK) & HDA_RING_SIZE_CAP_2) != 0 &&
             ((RingState.OriginalRirbSize & HDA_RING_SIZE_CAP_MASK) & HDA_RING_SIZE_CAP_2) != 0) {
    Entries = 2;
    SizeSelect = HDA_RING_SIZE_SELECT_2;
  } else {
    /* BUS_MASTER may already have been enabled temporarily by this fallback.
       Route every post-BME failure through the common cleanup path so the
       driver never leaves a PCI attribute transition behind just because the
       controller exposes no CORB/RIRB size that this transaction can share. */
    Status = EFI_UNSUPPORTED;
    goto Done;
  }

  CorbBytes = Entries * sizeof(UINT32);
  RirbBytes = Entries * sizeof(UINT64);
  RingState.CorbPages = EFI_SIZE_TO_PAGES(CorbBytes);
  RingState.RirbPages = EFI_SIZE_TO_PAGES(RirbBytes);

  Status = PciIo->AllocateBuffer(PciIo, AllocateAnyPages, EfiBootServicesData,
                                 RingState.CorbPages, &CorbHost, 0);
  if (Status != EFI_SUCCESS) {
    Status = HdaNormalizeExactSuccess(Status);
    goto Done;
  }
  RingState.CorbHost = CorbHost;
  RingState.CorbAllocated = TRUE;
  if (RingState.CorbHost == NULL) {
    HdaLog("[HDAU] CORB AllocateBuffer returned EFI_SUCCESS with a NULL host address");
    Status = EFI_DEVICE_ERROR;
    goto Done;
  }

  Status = PciIo->AllocateBuffer(PciIo, AllocateAnyPages, EfiBootServicesData,
                                 RingState.RirbPages, &RirbHost, 0);
  if (Status != EFI_SUCCESS) {
    Status = HdaNormalizeExactSuccess(Status);
    goto Done;
  }
  RingState.RirbHost = RirbHost;
  RingState.RirbAllocated = TRUE;
  if (RingState.RirbHost == NULL) {
    HdaLog("[HDAU] RIRB AllocateBuffer returned EFI_SUCCESS with a NULL host address");
    Status = EFI_DEVICE_ERROR;
    goto Done;
  }

  ZeroMem(RingState.CorbHost, EFI_PAGES_TO_SIZE(RingState.CorbPages));
  ZeroMem(RingState.RirbHost, EFI_PAGES_TO_SIZE(RingState.RirbPages));
  Status = PciIo->Map(PciIo, EfiPciIoOperationBusMasterCommonBuffer,
                      RingState.CorbHost, &CorbBytes, &CorbDevice, &CorbMapping);
  if (Status != EFI_SUCCESS) {
    Status = HdaNormalizeExactSuccess(Status);
    goto Done;
  }
  RingState.CorbMapping = CorbMapping;
  RingState.CorbMapped = TRUE;

  Status = PciIo->Map(PciIo, EfiPciIoOperationBusMasterCommonBuffer,
                      RingState.RirbHost, &RirbBytes, &RirbDevice, &RirbMapping);
  if (Status != EFI_SUCCESS) {
    Status = HdaNormalizeExactSuccess(Status);
    goto Done;
  }
  RingState.RirbMapping = RirbMapping;
  RingState.RirbMapped = TRUE;
  if (CorbBytes < Entries * sizeof(UINT32) || RirbBytes < Entries * sizeof(UINT64)) {
    Status = EFI_BAD_BUFFER_SIZE;
    goto Done;
  }
  if ((CorbBytes != 0 && (UINT64)CorbDevice > (MAX_UINT64 - (UINT64)(CorbBytes - 1U))) ||
      (RirbBytes != 0 && (UINT64)RirbDevice > (MAX_UINT64 - (UINT64)(RirbBytes - 1U)))) {
    HdaLog("[HDAU] CORB/RIRB DMA range wraps physical address space: corb=0x%lx/%u rirb=0x%lx/%u",
           (UINT64)CorbDevice, (UINT32)CorbBytes, (UINT64)RirbDevice, (UINT32)RirbBytes);
    Status = EFI_DEVICE_ERROR;
    goto Done;
  }
  if (((UINT64)CorbDevice & (HDA_DMA_ADDRESS_ALIGNMENT - 1U)) != 0 ||
      ((UINT64)RirbDevice & (HDA_DMA_ADDRESS_ALIGNMENT - 1U)) != 0) {
    HdaLog("[HDAU] CORB/RIRB mapped device address is not %u-byte aligned: corb=0x%lx rirb=0x%lx",
           HDA_DMA_ADDRESS_ALIGNMENT, (UINT64)CorbDevice, (UINT64)RirbDevice);
    Status = EFI_UNSUPPORTED;
    goto Done;
  }
  Status = HdaMemRead16(PciIo, HDA_REG_GCAP, &Gcap);
  if (EFI_ERROR(Status)) goto Done;
  RingState.Supports64BitDma = ((Gcap & HDA_GCAP_64OK) != 0);
  if (!RingState.Supports64BitDma &&
      ((CorbBytes != 0 && ((UINT64)(CorbBytes - 1U) > 0xFFFFFFFFULL ||
                           (UINT64)CorbDevice > (0xFFFFFFFFULL - (UINT64)(CorbBytes - 1U)))) ||
       (RirbBytes != 0 && ((UINT64)(RirbBytes - 1U) > 0xFFFFFFFFULL ||
                           (UINT64)RirbDevice > (0xFFFFFFFFULL - (UINT64)(RirbBytes - 1U)))))) {
    HdaLog("[HDAU] controller lacks 64-bit DMA but CORB/RIRB range exceeds 4 GiB: corb=0x%lx/%u rirb=0x%lx/%u",
           (UINT64)CorbDevice, (UINT32)CorbBytes, (UINT64)RirbDevice, (UINT32)RirbBytes);
    Status = EFI_UNSUPPORTED;
    goto Done;
  }

  /* Revalidate the complete disabled-ring snapshot after every allocation,
     mapping and capability read, but before the first register mutation. If
     another pre-boot agent changed the rings in that window, leave its newer
     state untouched and release only our temporary resources. */
  Status = HdaRequireCorbRirbSnapshotUnchanged(PciIo, &RingState);
  if (EFI_ERROR(Status)) goto Done;

  Corb = (UINT32 *)RingState.CorbHost;
  Rirb = (UINT64 *)RingState.RirbHost;
  Command = ((UINT32)Codec << 28) | ((UINT32)Nid << 20) | (VerbPayload & 0xFFFFF);
  Corb[1] = Command;

  RingState.RegistersTouched = TRUE;
  Status = HdaMemWrite8(
             PciIo, HDA_REG_CORBCTL,
             (UINT8)(RingState.OriginalCorbCtl & HDA_CORBCTL_RSVDP_MASK)
             );
  if (EFI_ERROR(Status)) goto Done;
  Status = HdaMemWrite8(
             PciIo, HDA_REG_RIRBCTL,
             (UINT8)(RingState.OriginalRirbCtl & HDA_RIRBCTL_RSVDP_MASK)
             );
  if (EFI_ERROR(Status)) goto Done;
  Status = HdaMemWrite8(
             PciIo, HDA_REG_CORBSIZE,
             (UINT8)((RingState.OriginalCorbSize & (UINT8)~HDA_RING_SIZE_SELECT_MASK) | SizeSelect)
             );
  if (EFI_ERROR(Status)) goto Done;
  Status = HdaMemWrite8(
             PciIo, HDA_REG_RIRBSIZE,
             (UINT8)((RingState.OriginalRirbSize & (UINT8)~HDA_RING_SIZE_SELECT_MASK) | SizeSelect)
             );
  if (EFI_ERROR(Status)) goto Done;
  Status = HdaMemWrite32(PciIo, HDA_REG_CORBLBASE, (UINT32)(CorbDevice & 0xFFFFFFFFU));
  if (EFI_ERROR(Status)) goto Done;
  if (RingState.Supports64BitDma) {
    Status = HdaMemWrite32(PciIo, HDA_REG_CORBUBASE, (UINT32)(CorbDevice >> 32));
    if (EFI_ERROR(Status)) goto Done;
  }
  Status = HdaMemWrite32(PciIo, HDA_REG_RIRBLBASE, (UINT32)(RirbDevice & 0xFFFFFFFFU));
  if (EFI_ERROR(Status)) goto Done;
  if (RingState.Supports64BitDma) {
    Status = HdaMemWrite32(PciIo, HDA_REG_RIRBUBASE, (UINT32)(RirbDevice >> 32));
    if (EFI_ERROR(Status)) goto Done;
  }
  Status = HdaResetCorbReadPointer(PciIo);
  if (EFI_ERROR(Status)) goto Done;
  Status = HdaMemWrite16(
             PciIo, HDA_REG_CORBWP,
             (UINT16)(RingState.OriginalCorbWp & HDA_CORBWP_RSVDP_MASK)
             );
  if (EFI_ERROR(Status)) goto Done;
  Status = HdaResetRirbWritePointer(PciIo);
  if (EFI_ERROR(Status)) goto Done;
  Status = HdaMemWrite16(
             PciIo, HDA_REG_RINTCNT,
             (UINT16)((RingState.OriginalRintCnt & HDA_RINTCNT_RSVDP_MASK) | 1U)
             );
  if (EFI_ERROR(Status)) goto Done;

  /* A successful PCI I/O write does not prove that a defective controller
     retained the requested DMA base/size/pointer state. Verify every critical
     ring parameter before DMA can reference the temporary buffers. */
  Status = HdaVerifyTemporaryCorbRirbProgramming(
             PciIo, &RingState, CorbDevice, RirbDevice, SizeSelect
             );
  if (EFI_ERROR(Status)) goto Done;

  /* Stopped-ring programming can still take long enough for a foreign
     Immediate Command to appear after the snapshot gate. Revalidate ICIS at
     the last safe point before either ring DMA engine is enabled. */
  Icis = 0;
  Status = HdaMemRead16(PciIo, HDA_REG_ICIS, &Icis);
  if (EFI_ERROR(Status)) goto Done;
  if ((Icis & (HDA_ICIS_ICB | HDA_ICIS_IRV)) != 0) {
    Status = EFI_ACCESS_DENIED;
    goto Done;
  }

  Status = HdaMemWrite8(
             PciIo, HDA_REG_RIRBCTL,
             (UINT8)((RingState.OriginalRirbCtl & HDA_RIRBCTL_RSVDP_MASK) | HDA_RIRBCTL_DMA_ENABLE)
             );
  if (EFI_ERROR(Status)) goto Done;
  Status = HdaMemWrite8(
             PciIo, HDA_REG_CORBCTL,
             (UINT8)((RingState.OriginalCorbCtl & HDA_CORBCTL_RSVDP_MASK) | HDA_CORBCTL_DMA_ENABLE)
             );
  if (EFI_ERROR(Status)) goto Done;
  Status = HdaPciFlushExact(PciIo);
  if (EFI_ERROR(Status)) goto Done;
  Status = HdaVerifyCorbRirbDmaEnabled(PciIo, &RingState);
  if (EFI_ERROR(Status)) goto Done;
  Status = HdaMemWrite16(
             PciIo, HDA_REG_CORBWP,
             (UINT16)((RingState.OriginalCorbWp & HDA_CORBWP_RSVDP_MASK) | 1U)
             );
  if (EFI_ERROR(Status)) goto Done;

  Status = EFI_TIMEOUT;
  LastRirbWp = 0;
  for (Index = 0; Index < (HDAU_CORB_RIRB_TIMEOUT_US / HDAU_CORB_RIRB_POLL_US); Index++) {
    CorbSts = 0;
    Status = HdaMemRead8(PciIo, HDA_REG_CORBSTS, &CorbSts);
    if (EFI_ERROR(Status)) break;
    if ((CorbSts & HDA_CORBSTS_CMEI) != 0) {
      /* Intel classifies CMEI as a severe memory-path failure: command data is
         invalid and controller state is not trustworthy until CRST. */
      CorbMemoryError = TRUE;
      Status = EFI_DEVICE_ERROR;
      break;
    }

    Status = HdaMemRead8(PciIo, HDA_REG_RIRBSTS, &RirbSts);
    if (EFI_ERROR(Status)) break;
    if ((RirbSts & HDA_RIRBSTS_RIRBOIS) != 0) {
      /* Once RIRBOIS is raised one or more responses were discarded. The
         command/response association is no longer trustworthy. Cleanup owns
         the W1C acknowledgement after both DMA engines are stopped. */
      Status = EFI_DEVICE_ERROR;
      break;
    }
    if (EFI_ERROR(HdaMemRead16(PciIo, HDA_REG_RIRBWP, &RirbWp))) {
      Status = EFI_DEVICE_ERROR;
      break;
    }
    RirbWp &= 0x00FFU;
    if (RirbWp >= Entries) {
      Status = EFI_DEVICE_ERROR;
      break;
    }
    while (LastRirbWp != RirbWp) {
      UINT64 Entry;
      UINT32 Extended;

      LastRirbWp = (UINT16)((LastRirbWp + 1U) % Entries);
      Entry = Rirb[LastRirbWp];
      Extended = (UINT32)(Entry >> 32);

      /* RESP_EX bit 4 marks unsolicited responses. They may legitimately
         arrive while GCTL.UNSOL is enabled and must never satisfy this verb. */
      if ((Extended & HDA_RIRB_RESPONSE_UNSOLICITED) != 0) continue;
      if ((Extended & HDA_RIRB_RESPONSE_CODEC_MASK) != Codec) continue;

      if (Response != NULL) *Response = (UINT32)Entry;
      Status = EFI_SUCCESS;
      goto Done;
    }
    HdaStall(HDAU_CORB_RIRB_POLL_US);
  }

Done:
  /* Close the race between the last poll/response and DMA shutdown. Late CMEI
     or RIRB overrun must never be silently erased by cleanup and reported as a
     successful verb. RINTFL itself is expected for our temporary RIRB and is
     acknowledged only after both DMA engines are stopped. */
  if (RingState.RegistersTouched) {
    CorbSts = 0;
    CorbStatusRead = HdaMemRead8(PciIo, HDA_REG_CORBSTS, &CorbSts);
    if (EFI_ERROR(CorbStatusRead)) {
      if (!EFI_ERROR(Status)) Status = CorbStatusRead;
    } else if ((CorbSts & HDA_CORBSTS_CMEI) != 0) {
      CorbMemoryError = TRUE;
      Status = EFI_DEVICE_ERROR;
    }

    RirbSts = 0;
    RirbStatusRead = HdaMemRead8(PciIo, HDA_REG_RIRBSTS, &RirbSts);
    if (EFI_ERROR(RirbStatusRead)) {
      if (!EFI_ERROR(Status)) Status = RirbStatusRead;
    } else if ((RirbSts & HDA_RIRBSTS_RIRBOIS) != 0) {
      Status = EFI_DEVICE_ERROR;
    }
  }

  CleanupStatus = HdaCleanupCorbRirbState(&RingState, FALSE);
  if (EFI_ERROR(CleanupStatus) || HdaCorbRirbStatePending(&RingState)) {
    mHdaCorbRirbFallbackDisabled = TRUE;
    if (HdaCorbRirbStatePending(&RingState)) {
      if (!mHdaRetainedCorbRirbValid) {
        CopyMem(&mHdaRetainedCorbRirbState, &RingState, sizeof(RingState));
        mHdaRetainedCorbRirbValid = TRUE;
        HdaLog("[HDAU] CORB/RIRB ownership retained for cleanup retry");
      } else if (!EFI_ERROR(CleanupStatus)) {
        CleanupStatus = EFI_ALREADY_STARTED;
      }
    }
    if (EFI_ERROR(CleanupStatus)) {
      HdaLog("[HDAU] CORB/RIRB cleanup failed: %r; fallback disabled for this boot", CleanupStatus);
      /* Cleanup/ownership failure is more severe than the verb error that
         led to this fallback.  Never hide a retained DMA/mapping state behind
         an earlier timeout or transport error. */
      Status = CleanupStatus;
    }
  }

  if (CorbMemoryError &&
      !EFI_ERROR(CleanupStatus) &&
      !HdaCorbRirbStatePending(&RingState)) {
    /* Clearing CMEI is only acknowledgement; Intel requires a controller
       reset before operation continues. Cleanup first guarantees temporary
       DMA ownership is discharged before CRST is attempted. */
    ResetStatus = HdaControllerReset(PciIo);
    if (EFI_ERROR(ResetStatus)) {
      mHdaCorbRirbFallbackDisabled = TRUE;
      HdaLog("[HDAU] CORB memory error detected and required controller reset failed: %r; fallback disabled", ResetStatus);
      return ResetStatus;
    }
    HdaLog("[HDAU] CORB memory error detected; temporary rings cleaned and controller reset completed");
    return EFI_DEVICE_ERROR;
  }
  return Status;
}

STATIC EFI_STATUS HdaImmediateCommand(
  EFI_PCI_IO_PROTOCOL *PciIo,
  UINT8 Codec,
  UINT8 Nid,
  UINT32 VerbPayload,
  UINT32 *Response
) {
  EFI_STATUS Status;
  EFI_STATUS FallbackStatus;

  Status = HdaImmediateCommandRaw(PciIo, Codec, Nid, VerbPayload, Response);
  if (Status == EFI_SUCCESS) return EFI_SUCCESS;
  if (!EFI_ERROR(Status)) {
    /* EFI_WARN_* is not proof that the verb completed. Do not retry an
       ambiguous command via CORB/RIRB: SET verbs can change auto-incrementing
       hardware state, and replay could mutate it twice. */
    HdaLog("[HDAU] Immediate Command warning is not success: codec=0x%x nid=0x%x payload=0x%x status=%r",
           Codec, Nid, VerbPayload, Status);
    if (Response != NULL) *Response = 0;
    return EFI_DEVICE_ERROR;
  }
  if (!mHdaCfg.CorbRirbFallback) {
    if (Response != NULL) *Response = 0;
    return Status;
  }

  FallbackStatus = HdaCorbRirbCommand(PciIo, Codec, Nid, VerbPayload, Response);
  if (FallbackStatus == EFI_SUCCESS) {
    if (mHdaCfg.Debug) {
      HdaLog("[HDAU] Immediate Command fallback succeeded through temporary CORB/RIRB: codec=0x%x nid=0x%x payload=0x%x original=%r",
             Codec, Nid, VerbPayload, Status);
    }
    return EFI_SUCCESS;
  }
  if (!EFI_ERROR(FallbackStatus)) {
    HdaLog("[HDAU] CORB/RIRB warning is not success: codec=0x%x nid=0x%x payload=0x%x status=%r",
           Codec, Nid, VerbPayload, FallbackStatus);
    FallbackStatus = EFI_DEVICE_ERROR;
  }
  if (Response != NULL) *Response = 0;
  if (mHdaCfg.Debug) {
    HdaLog("[HDAU] CORB/RIRB fallback unavailable: codec=0x%x nid=0x%x payload=0x%x immediate=%r fallback=%r",
           Codec, Nid, VerbPayload, Status, FallbackStatus);
  }
  if (mHdaCorbRirbFallbackDisabled) return FallbackStatus;
  return Status;
}


STATIC EFI_STATUS HdaGetParamChecked(
  EFI_PCI_IO_PROTOCOL *PciIo,
  UINT8 Codec,
  UINT8 Nid,
  UINT8 Param,
  UINT32 *Response
) {
  EFI_STATUS Status;

  if (PciIo == NULL || Response == NULL) return EFI_INVALID_PARAMETER;
  *Response = 0;
  Status = HdaImmediateCommand(PciIo, Codec, Nid,
                               HDA_VERB12(HDA_VERB_GET_PARAMETER, Param), Response);
  return Status;
}

STATIC UINT32 HdaGetParam(EFI_PCI_IO_PROTOCOL *PciIo, UINT8 Codec, UINT8 Nid, UINT8 Param) {
  UINT32 Response = 0;
  (VOID)HdaGetParamChecked(PciIo, Codec, Nid, Param, &Response);
  return Response;
}

STATIC EFI_STATUS HdaSetVerb(EFI_PCI_IO_PROTOCOL *PciIo, UINT8 Codec, UINT8 Nid, UINT32 VerbPayload) {
  EFI_STATUS Status;

  Status = HdaImmediateCommand(PciIo, Codec, Nid, VerbPayload, NULL);
  if (EFI_ERROR(Status) && mHdaCfg.Debug) {
    HdaLog("[HDAU] verb failed: codec=0x%x nid=0x%x payload=0x%x status=%r",
           Codec, Nid, VerbPayload, Status);
  }
  return Status;
}

STATIC UINT8 HdaWidgetType(UINT32 AwCaps) {
  return (UINT8)((AwCaps >> 20) & 0x0F);
}


STATIC UINT8 HdaAmpUnityGainFromCaps(UINT32 AmpCaps) {
  UINT8 NumSteps;
  UINT8 Offset;

  /* HDA AmpCaps bits 14:8 advertise NumSteps and bits 6:0 advertise the
     zero-dB offset.  Apple-like initialization starts from unity gain rather
     than driving every codec to its maximum positive gain. */
  NumSteps = (UINT8)((AmpCaps >> 8) & 0x7F);
  Offset = (UINT8)(AmpCaps & 0x7F);
  return (Offset <= NumSteps) ? Offset : NumSteps;
}

STATIC UINT16 HdaAmpOutUnmuteUnityFromCaps(UINT32 AmpCaps) {
  return (UINT16)(0xB000 | HdaAmpUnityGainFromCaps(AmpCaps));
}

STATIC UINT16 HdaAmpInUnmuteUnityFromCaps(UINT32 AmpCaps, UINTN Index) {
  /*
     SET_AMP_GAIN_MUTE payload bits are:
       0x8000 = output amp, 0x4000 = input amp, 0x2000/0x1000 = L/R.
     The previous 0x3000 payload only selected L/R and missed the input-amp
     selector bit.  On mixer paths such as ALC887 0x14 -> 0x0C -> 0x02, that
     can leave the mixer input muted even when the route, EAPD, pinctl and DMA
     all look correct.  Use 0x7000 (input + L/R) and preserve the connection
     index in bits 11:8 for every codec/layout, not just Realtek/H81.
  */
  return (UINT16)(0x7000 | (((UINT16)Index & 0x0F) << 8) | HdaAmpUnityGainFromCaps(AmpCaps));
}

#define HDAU_CPF_REALTEK             BIT0
#define HDAU_CPF_CONEXANT            BIT1
#define HDAU_CPF_IDT                 BIT2
#define HDAU_CPF_CIRRUS              BIT3
#define HDAU_CPF_VIA                 BIT4
#define HDAU_CPF_ANALOG_DEVICES      BIT5
#define HDAU_CPF_CREATIVE            BIT6
#define HDAU_CPF_HDMI_DP             BIT7
#define HDAU_CPF_SKIP_BY_DEFAULT     BIT8

typedef struct {
  CONST CHAR8 *Name;
  UINT32      Flags;
} HDAU_CODEC_POLICY;

/* Vendor-family classification only. Exact codec/layout routes and ConfigData
   come from the generated AppleALC tables and are validated against the live
   widget graph. No board, mobile/desktop, or fixed-NID ranking is encoded here. */
STATIC CONST HDAU_CODEC_POLICY mHdaGenericRealtekPolicy = {
  "Realtek runtime family", HDAU_CPF_REALTEK
};
STATIC CONST HDAU_CODEC_POLICY mHdaGenericConexantPolicy = {
  "Conexant runtime family", HDAU_CPF_CONEXANT
};
STATIC CONST HDAU_CODEC_POLICY mHdaGenericIdtPolicy = {
  "IDT/Sigmatel runtime family", HDAU_CPF_IDT
};
STATIC CONST HDAU_CODEC_POLICY mHdaGenericCirrusPolicy = {
  "Cirrus Logic runtime family", HDAU_CPF_CIRRUS
};
STATIC CONST HDAU_CODEC_POLICY mHdaGenericViaPolicy = {
  "VIA runtime family", HDAU_CPF_VIA
};
STATIC CONST HDAU_CODEC_POLICY mHdaGenericAnalogDevicesPolicy = {
  "Analog Devices runtime family", HDAU_CPF_ANALOG_DEVICES
};
STATIC CONST HDAU_CODEC_POLICY mHdaGenericCreativePolicy = {
  "Creative/EMU runtime family", HDAU_CPF_CREATIVE
};
STATIC CONST HDAU_CODEC_POLICY mHdaGenericHdmiPolicy = {
  "HDMI/DisplayPort digital codec", HDAU_CPF_HDMI_DP | HDAU_CPF_SKIP_BY_DEFAULT
};

STATIC CONST HDAU_CODEC_POLICY *mHdaActivePolicy = NULL;
STATIC UINT32 mHdaActiveCodecId = 0;

typedef struct {
  UINT32      CodecId;
  UINTN       LayoutId;
  CONST CHAR8 *Name;
  UINT8       Pins[HDAU_ALCID_PROFILE_PAIR_COUNT];
  UINT8       Dacs[HDAU_ALCID_PROFILE_PAIR_COUNT];
} HDAU_ALCID_PROFILE;

typedef struct {
  UINT32      CodecId;
  UINTN       LayoutId;
  CONST CHAR8 *Name;
  UINT32      VerbOffset;
  UINT16      VerbCount;
} HDAU_ALCID_CONFIG_PROFILE;

typedef struct {
  UINT32      CodecId;
  UINTN       LayoutId;
  CONST CHAR8 *Name;
  UINT32      NodeOffset;
  UINT16      NodeCount;
  UINT8       Pin;
  UINT8       Dac;
} HDAU_ALCID_ROUTE_PROFILE;

typedef struct {
  UINT32 CodecId;
  UINTN  LayoutId;
  UINT32 RevisionId;
  UINT32 RevisionMask;
  UINT32 SubsystemId;
  UINT32 SubsystemMask;
  UINT32 VerbOffset;
  UINT16 VerbCount;
} HDAU_ALCID_EXACT_PROFILE;

#include "HdaAppleAlcLayouts.h"

STATIC CONST HDAU_ALCID_PROFILE *mHdaActiveAlcIdProfile = NULL;


STATIC CONST HDAU_ALCID_PROFILE *HdaFindAlcIdProfile(UINT32 CodecId) {
  UINTN Index;

  if (!mHdaBootArgsAlcIdValid) return NULL;

  for (Index = 0; Index < ARRAY_SIZE(mHdaAlcIdProfiles); Index++) {
    if (mHdaAlcIdProfiles[Index].CodecId == CodecId &&
        mHdaAlcIdProfiles[Index].LayoutId == mHdaBootArgsAlcId) {
      return &mHdaAlcIdProfiles[Index];
    }
  }

  return NULL;
}

STATIC CONST HDAU_ALCID_PROFILE *HdaResolveAlcIdProfile(UINT32 CodecId) {
  if (!mHdaBootArgsAlcIdValid) return NULL;
  return HdaFindAlcIdProfile(CodecId);
}

STATIC CONST HDAU_ALCID_CONFIG_PROFILE *HdaFindAlcIdConfigProfile(CONST HDAU_ALCID_PROFILE *Profile) {
  UINTN Index;

  if (Profile == NULL) return NULL;

  for (Index = 0; Index < ARRAY_SIZE(mHdaAlcConfigProfiles); Index++) {
    if (mHdaAlcConfigProfiles[Index].CodecId == Profile->CodecId &&
        mHdaAlcConfigProfiles[Index].LayoutId == Profile->LayoutId) {
      return &mHdaAlcConfigProfiles[Index];
    }
  }

  return NULL;
}

STATIC BOOLEAN HdaConfigVerbIsRollbackSafe(UINT32 VerbPayload) {
  UINT32 Verb12;
  UINT32 Verb4;

  VerbPayload &= 0xFFFFFU;
  Verb12 = (VerbPayload >> 8) & 0xFFFU;
  Verb4 = (VerbPayload >> 16) & 0x0FU;

  /* Four-bit GET verbs used by ConfigData are read-only. */
  if (Verb4 == HDA_VERB_GET_CONVERTER_FORMAT ||
      Verb4 == HDA_VERB_GET_AMP_GAIN_MUTE ||
      Verb4 == HDA_VERB_GET_PROC_COEF ||
      Verb4 == HDA_VERB_GET_COEF_INDEX) {
    return TRUE;
  }

  /* Fxx verbs are the standard 12-bit GET namespace. Unknown vendor SETs
     are deliberately not inferred from their payload shape. */
  if (Verb12 >= 0xF00U) return TRUE;

  if (Verb4 == HDA_VERB_SET_CONVERTER_FORMAT ||
      Verb4 == HDA_VERB_SET_AMP_GAIN_MUTE ||
      Verb4 == HDA_VERB_SET_COEF_INDEX ||
      Verb4 == HDA_VERB_SET_PROC_COEF) {
    return TRUE;
  }

  switch (Verb12) {
    case HDA_VERB_SET_CONNECTION_SELECT:
    case HDA_VERB_SET_PROCESSING_STATE:
    case HDA_VERB_SET_POWER_STATE:
    case HDA_VERB_SET_CONVERTER_STREAM_CHANNEL:
    case HDA_VERB_SET_PIN_WIDGET_CONTROL:
    case HDA_VERB_SET_UNSOLICITED_RESPONSE:
    case HDA_VERB_SET_EAPD_BTL:
    case HDA_VERB_SET_DIGI_CONVERT_1:
    case HDA_VERB_SET_GPIO_DATA:
    case HDA_VERB_SET_GPIO_ENABLE_MASK:
    case HDA_VERB_SET_GPIO_DIRECTION:
    case HDA_VERB_SET_GPIO_WAKE_ENABLE_MASK:
    case HDA_VERB_SET_BEEP_GENERATION:
    case HDA_VERB_SET_VOLUME_KNOB_CONTROL:
    case HDA_VERB_SET_CONFIG_DEFAULT_0:
    case HDA_VERB_SET_CONFIG_DEFAULT_1:
    case HDA_VERB_SET_CONFIG_DEFAULT_2:
    case HDA_VERB_SET_CONFIG_DEFAULT_3:
    case HDA_VERB_SET_SUBSYSTEM_ID_0:
    case HDA_VERB_SET_SUBSYSTEM_ID_1:
    case HDA_VERB_SET_SUBSYSTEM_ID_2:
    case HDA_VERB_SET_SUBSYSTEM_ID_3:
    case HDA_VERB_SET_HDMI_DIP_INDEX:
    case HDA_VERB_SET_HDMI_DIP_DATA:
    case HDA_VERB_SET_HDMI_DIP_XMIT:
    case HDA_VERB_SET_HDMI_CHAN_SLOT:
      return TRUE;
    default:
      break;
  }
  return FALSE;
}

STATIC BOOLEAN HdaConfigSpanIsRollbackSafe(UINTN Offset, UINTN Count) {
  UINTN End;
  UINTN Index;

  if (Offset >= ARRAY_SIZE(mHdaAlcConfigVerbs)) return FALSE;
  End = Offset + Count;
  if (End < Offset || End > ARRAY_SIZE(mHdaAlcConfigVerbs)) return FALSE;
  for (Index = Offset; Index < End; Index++) {
    UINT32 Encoded = mHdaAlcConfigVerbs[Index];
    if (Encoded == 0) continue;
    if ((Encoded & 0x08000000U) != 0) return FALSE;
    if (!HdaConfigVerbIsRollbackSafe(Encoded & 0xFFFFFU)) return FALSE;
  }
  return TRUE;
}

STATIC EFI_STATUS HdaApplyAlcIdPrebootVerbs(
  HDAU_CONTEXT *Context,
  CONST CHAR8 *Reason
) {
  CONST HDAU_ALCID_CONFIG_PROFILE *ConfigProfile;
  UINTN Index;
  UINTN Applied;
  UINTN End;

  if (Context == NULL || Context->PciIo == NULL || mHdaActiveAlcIdProfile == NULL) return EFI_SUCCESS;
  ConfigProfile = HdaFindAlcIdConfigProfile(mHdaActiveAlcIdProfile);
  if (ConfigProfile == NULL) return EFI_SUCCESS;

  if (Context->SkipAlcIdConfigData ||
      !HdaConfigSpanIsRollbackSafe(ConfigProfile->VerbOffset, ConfigProfile->VerbCount)) {
    HdaLog("[HDAU] AppleALC ConfigData skipped: codec=0x%08x alcid=%u profile=%a reason=non-transactional-private-verb",
           mHdaActiveAlcIdProfile->CodecId, (UINT32)mHdaActiveAlcIdProfile->LayoutId,
           ConfigProfile->Name);
    return EFI_SUCCESS;
  }

  End = (UINTN)ConfigProfile->VerbOffset + (UINTN)ConfigProfile->VerbCount;
  Applied = 0;
  for (Index = ConfigProfile->VerbOffset; Index < End; Index++) {
    UINT32 Encoded = mHdaAlcConfigVerbs[Index];
    UINT8 Nid;
    UINT32 VerbPayload;
    EFI_STATUS Status;

    if (Encoded == 0) continue;
    Nid = (UINT8)((Encoded >> 20) & 0xFFU);
    VerbPayload = Encoded & 0xFFFFFU;
    Status = HdaSetVerb(Context->PciIo, Context->Codec, Nid, VerbPayload);
    if (EFI_ERROR(Status)) {
      HdaLog("[HDAU] AppleALC ConfigData write failed: nid=0x%x payload=0x%x profile=%a index=%u status=%r reason=%a",
             Nid, VerbPayload, ConfigProfile->Name, (UINT32)Index, Status,
             (Reason != NULL) ? Reason : "unspecified");
      return Status;
    }
    Applied++;
  }

  if (Applied > 0) {
    HdaLog("[HDAU] AppleALC ConfigData: codec=0x%08x alcid=%u profile=%a applied=%u/%u reason=%a",
           mHdaActiveAlcIdProfile->CodecId, (UINT32)mHdaActiveAlcIdProfile->LayoutId, ConfigProfile->Name,
           (UINT32)Applied, (UINT32)ConfigProfile->VerbCount,
           (Reason != NULL) ? Reason : "unspecified");
  }
  return EFI_SUCCESS;
}

STATIC EFI_STATUS HdaApplyExactProfileVerbs(HDAU_CONTEXT *Context, CONST CHAR8 *Reason) {
  UINTN Index;

  if (Context == NULL || Context->PciIo == NULL || !mHdaCfg.ExactProfileInit ||
      !mHdaBootArgsAlcIdValid || Context->SkipExactProfileData) return EFI_SUCCESS;
  if (!Context->SubsystemIdValid && Context->Afg != 0) {
    EFI_STATUS IdentityStatus;

    Context->SubsystemId = 0;
    IdentityStatus = HdaImmediateCommand(Context->PciIo, Context->Codec, Context->Afg,
                                         HDA_VERB12(HDA_VERB_GET_SUBSYSTEM_ID, 0), &Context->SubsystemId);
    Context->SubsystemIdValid = !EFI_ERROR(IdentityStatus);
  }

  for (Index = 0; Index < ARRAY_SIZE(mHdaAlcExactProfiles); Index++) {
    CONST HDAU_ALCID_EXACT_PROFILE *Exact = &mHdaAlcExactProfiles[Index];
    UINTN VerbIndex;
    UINTN End;
    UINTN Applied;

    if (Exact->CodecId == 0 || Exact->VerbCount == 0) continue;
    if (Exact->CodecId != Context->CodecId || Exact->LayoutId != mHdaBootArgsAlcId) continue;
    if (Exact->RevisionMask != 0 &&
        (!Context->RevisionIdValid ||
         (Context->RevisionId & Exact->RevisionMask) != (Exact->RevisionId & Exact->RevisionMask))) continue;
    if (Exact->SubsystemMask != 0 &&
        (!Context->SubsystemIdValid ||
         (Context->SubsystemId & Exact->SubsystemMask) != (Exact->SubsystemId & Exact->SubsystemMask))) continue;
    End = (UINTN)Exact->VerbOffset + (UINTN)Exact->VerbCount;
    if (End < Exact->VerbOffset || End > ARRAY_SIZE(mHdaAlcConfigVerbs)) return EFI_COMPROMISED_DATA;
    if (!HdaConfigSpanIsRollbackSafe(Exact->VerbOffset, Exact->VerbCount)) {
      HdaLog("[HDAU] exact ConfigData skipped because it contains a private mutation without deterministic rollback: codec=0x%08x alcid=%u verbs=%u",
             Exact->CodecId, (UINT32)Exact->LayoutId, (UINT32)Exact->VerbCount);
      return EFI_SUCCESS;
    }

    Applied = 0;
    for (VerbIndex = Exact->VerbOffset; VerbIndex < End; VerbIndex++) {
      UINT32 Encoded = mHdaAlcConfigVerbs[VerbIndex];
      UINT8 Nid;
      UINT32 Payload;
      EFI_STATUS Status;
      if (Encoded == 0) continue;
      Nid = (UINT8)((Encoded >> 20) & 0xFFU);
      Payload = Encoded & 0xFFFFFU;
      Status = HdaSetVerb(Context->PciIo, Context->Codec, Nid, Payload);
      if (EFI_ERROR(Status)) return Status;
      Applied++;
    }
    HdaLog("[HDAU] exact profile applied: codec=0x%08x revision=0x%08x subsystem=0x%08x alcid=%u verbs=%u reason=%a",
           Context->CodecId, Context->RevisionId, Context->SubsystemId,
           (UINT32)mHdaBootArgsAlcId, (UINT32)Applied,
           Reason == NULL ? "exact-profile" : Reason);
    return EFI_SUCCESS;
  }
  return EFI_SUCCESS;
}

STATIC CONST HDAU_CODEC_POLICY *HdaFindCodecPolicy(UINT32 CodecId) {
  UINT16 Vendor;

  if (!mHdaCfg.AppleALCPolicy) return NULL;
  Vendor = (UINT16)(CodecId >> 16);

  if (Vendor == 0x10EC) return &mHdaGenericRealtekPolicy;
  if (Vendor == 0x14F1) return &mHdaGenericConexantPolicy;
  if (Vendor == 0x111D || Vendor == 0x8384) return &mHdaGenericIdtPolicy;
  if (Vendor == 0x1013) return &mHdaGenericCirrusPolicy;
  if (Vendor == 0x1106) return &mHdaGenericViaPolicy;
  if (Vendor == 0x11D4) return &mHdaGenericAnalogDevicesPolicy;
  if (Vendor == 0x1102) return &mHdaGenericCreativePolicy;
  if (Vendor == 0x8086 || Vendor == 0x1002 || Vendor == 0x10DE) return &mHdaGenericHdmiPolicy;

  return NULL;
}

STATIC UINT8 HdaDefaultDevice(UINT32 ConfigDefault) {
  return (UINT8)((ConfigDefault >> 20) & 0x0F);
}

STATIC UINT8 HdaDefaultPortConnectivity(UINT32 ConfigDefault) {
  return (UINT8)((ConfigDefault >> 30) & 0x03);
}

STATIC UINT8 HdaDefaultAssociation(UINT32 ConfigDefault) {
  return (UINT8)((ConfigDefault >> 4) & 0x0F);
}

STATIC UINT8 HdaDefaultSequence(UINT32 ConfigDefault) {
  return (UINT8)(ConfigDefault & 0x0F);
}

STATIC UINT8 HdaDefaultConnectionType(UINT32 ConfigDefault) {
  return (UINT8)((ConfigDefault >> 16) & 0x0F);
}

STATIC UINT8 HdaDefaultLocation(UINT32 ConfigDefault) {
  return (UINT8)((ConfigDefault >> 24) & 0x3F);
}

STATIC UINT8 HdaDefaultColor(UINT32 ConfigDefault) {
  return (UINT8)((ConfigDefault >> 12) & 0x0F);
}

STATIC BOOLEAN HdaPinDefaultIsDigital(UINT32 ConfigDefault) {
  UINT8 Device = HdaDefaultDevice(ConfigDefault);
  UINT8 ConnectionType = HdaDefaultConnectionType(ConfigDefault);

  if (Device == HDA_DEFAULT_DEVICE_SPDIF_OUT || Device == HDA_DEFAULT_DEVICE_DIGITAL_OUT) return TRUE;
  if (ConnectionType == HDA_DEFAULT_CONN_OPTICAL || ConnectionType == HDA_DEFAULT_CONN_DIGITAL) return TRUE;
  return FALSE;
}

STATIC BOOLEAN HdaPinDefaultLooksConnected(UINT32 ConfigDefault) {
  UINT8 PortConnectivity = HdaDefaultPortConnectivity(ConfigDefault);

  /* PortConnectivity == 1 is the specification-defined no-physical-connection
     state. Association values 1..15 are valid groups, so association 0xF must
     not be treated as a generic disable marker merely because it appears in
     the common 0x411111F0 firmware placeholder. */
  if (ConfigDefault == 0 || ConfigDefault == 0xFFFFFFFF) return FALSE;
  if (PortConnectivity == 1) return FALSE;
  return TRUE;
}

STATIC BOOLEAN HdaGetSubnodes(EFI_PCI_IO_PROTOCOL *PciIo, UINT8 Codec, UINT8 Nid, UINT8 *Start, UINT8 *Count) {
  UINT32 Response;
  UINTN First;
  UINTN Number;

  if (PciIo == NULL || Start == NULL || Count == NULL) return FALSE;
  *Start = 0;
  *Count = 0;
  Response = HdaGetParam(PciIo, Codec, Nid, HDA_PARAM_NODE_COUNT);
  if (Response == 0 || Response == MAX_UINT32) return FALSE;

  First = (Response >> 16) & 0xFFU;
  Number = Response & 0xFFU;
  if (First == 0 || Number == 0 || First > HDAU_MAX_NID ||
      Number > ((UINTN)HDAU_MAX_NID - First + 1U)) {
    if (mHdaCfg.Debug) {
      HdaLog("[HDAU] invalid subnode range: nid=0x%x response=0x%x start=%u count=%u",
             Nid, Response, (UINT32)First, (UINT32)Number);
    }
    return FALSE;
  }

  *Start = (UINT8)First;
  *Count = (UINT8)Number;
  return TRUE;
}

STATIC UINTN HdaGetConnections(EFI_PCI_IO_PROTOCOL *PciIo, UINT8 Codec, UINT8 Nid, UINT8 *Conn, UINTN ConnMax) {
  UINT32 LenParam;
  UINT8 Length;
  BOOLEAN LongForm;
  BOOLEAN HavePrevious;
  BOOLEAN PreviousWasRange;
  UINT16 Previous;
  UINTN Index;
  UINTN Out;

  if (PciIo == NULL || Conn == NULL || ConnMax == 0) return 0;
  LenParam = HdaGetParam(PciIo, Codec, Nid, HDA_PARAM_CONNECTION_LIST_LEN);
  Length = (UINT8)(LenParam & 0x7F);
  LongForm = ((LenParam & BIT7) != 0);
  if (Length == 0) return 0;

  Index = 0;
  Out = 0;
  HavePrevious = FALSE;
  PreviousWasRange = FALSE;
  Previous = 0;
  while (Index < Length) {
    UINT32 Response = 0;
    UINTN Packed;
    UINTN PerResponse = LongForm ? 2U : 4U;

    if (EFI_ERROR(HdaImmediateCommand(PciIo, Codec, Nid,
                                      HDA_VERB12(HDA_VERB_GET_CONNECTION_ENTRY, (UINT8)Index),
                                      &Response))) {
      if (mHdaCfg.Debug) HdaLog("[HDAU] connection list read failed: nid=0x%x entry=%u", Nid, (UINT32)Index);
      return 0;
    }

    for (Packed = 0; Packed < PerResponse && Index < Length; Packed++, Index++) {
      UINT16 Raw;
      UINT16 Node;
      BOOLEAN IsRange;

      if (LongForm) {
        Raw = (UINT16)((Response >> (Packed * 16U)) & 0xFFFFU);
        IsRange = ((Raw & 0x8000U) != 0);
        Node = (UINT16)(Raw & 0x7FFFU);
      } else {
        Raw = (UINT16)((Response >> (Packed * 8U)) & 0xFFU);
        IsRange = ((Raw & 0x80U) != 0);
        Node = (UINT16)(Raw & 0x7FU);
      }

      if (Node > HDAU_MAX_NID) {
        if (mHdaCfg.Debug) HdaLog("[HDAU] invalid connection node: nid=0x%x node=0x%x", Nid, Node);
        return 0;
      }

      if (IsRange) {
        UINT16 Expanded;
        UINTN Required;
        if (!HavePrevious || PreviousWasRange || Node <= Previous) {
          if (mHdaCfg.Debug) HdaLog("[HDAU] malformed connection range: nid=0x%x previous=0x%x end=0x%x", Nid, Previous, Node);
          return 0;
        }
        Required = (UINTN)(Node - Previous);
        if (Required > (ConnMax - Out)) {
          if (mHdaCfg.Debug) HdaLog("[HDAU] connection list expansion overflow: nid=0x%x need=%u available=%u", Nid, (UINT32)Required, (UINT32)(ConnMax - Out));
          return 0;
        }
        for (Expanded = (UINT16)(Previous + 1U); Expanded <= Node; Expanded++) {
          Conn[Out++] = (UINT8)Expanded;
        }
      } else {
        if (Out >= ConnMax) {
          if (mHdaCfg.Debug) HdaLog("[HDAU] connection list capacity exceeded: nid=0x%x capacity=%u", Nid, (UINT32)ConnMax);
          return 0;
        }
        Conn[Out++] = (UINT8)Node;
      }
      Previous = Node;
      HavePrevious = TRUE;
      PreviousWasRange = IsRange;
    }
  }

  if (Index != Length) return 0;
  return Out;
}


STATIC VOID HdaDumpWrite(EFI_FILE_HANDLE File, CONST CHAR8 *Format, ...) {
  VA_LIST Marker;
  CHAR8 Buffer[1024];

  (VOID)File; /* The dump shares the single robust HdaLog sink. */
  if (Format == NULL) return;
  VA_START(Marker, Format);
  AsciiVSPrint(Buffer, sizeof(Buffer), Format, Marker);
  VA_END(Marker);
  HdaLog("[HDAUDUMP] %a", Buffer);
}


STATIC CONST CHAR8 *HdaDumpWidgetTypeName(UINT8 Type) {
  switch (Type) {
    case HDA_WIDGET_TYPE_AUDIO_OUT: return "Audio Output";
    case HDA_WIDGET_TYPE_AUDIO_IN: return "Audio Input";
    case HDA_WIDGET_TYPE_AUDIO_MIXER: return "Audio Mixer";
    case HDA_WIDGET_TYPE_AUDIO_SELECTOR: return "Audio Selector";
    case HDA_WIDGET_TYPE_PIN_COMPLEX: return "Pin Complex";
    case HDA_WIDGET_TYPE_POWER: return "Power Widget";
    case HDA_WIDGET_TYPE_VOLUME_KNOB: return "Volume Knob";
    case HDA_WIDGET_TYPE_BEEP: return "Beep Generator";
    case HDA_WIDGET_TYPE_VENDOR: return "Vendor Defined";
    default: return "Unknown Widget";
  }
}

STATIC CONST CHAR8 *HdaDumpDefaultDeviceName(UINT8 Device) {
  switch (Device) {
    case 0x0: return "Line Out";
    case 0x1: return "Speaker";
    case 0x2: return "Headphone Out";
    case 0x3: return "CD";
    case 0x4: return "SPDIF Out";
    case 0x5: return "Digital Out";
    case 0x6: return "Modem Line Side";
    case 0x7: return "Modem Handset Side";
    case 0x8: return "Line In";
    case 0x9: return "Aux";
    case 0xA: return "Mic In";
    case 0xB: return "Telephony";
    case 0xC: return "SPDIF In";
    case 0xD: return "Digital In";
    case 0xF: return "Other";
    default: return "Reserved";
  }
}

STATIC CONST CHAR8 *HdaDumpConnTypeName(UINT8 Type) {
  switch (Type) {
    case 0x0: return "Unknown";
    case 0x1: return "1/8 Jack";
    case 0x2: return "1/4 Jack";
    case 0x3: return "ATAPI Internal";
    case 0x4: return "RCA";
    case 0x5: return "Optical";
    case 0x6: return "Digital";
    case 0x7: return "Analog";
    case 0x8: return "DIN";
    case 0x9: return "XLR";
    case 0xA: return "RJ11";
    case 0xB: return "Combination";
    case 0xF: return "Other";
    default: return "Reserved";
  }
}

STATIC CONST CHAR8 *HdaDumpPortConnName(UINT8 Port) {
  switch (Port) {
    case 0: return "Jack";
    case 1: return "No physical connection";
    case 2: return "Fixed";
    case 3: return "Both";
    default: return "Unknown";
  }
}

STATIC EFI_STATUS HdaOpenLoadedImageRoot(EFI_FILE_HANDLE *Root) {
  EFI_STATUS Status;
  EFI_LOADED_IMAGE_PROTOCOL *LoadedImage;
  EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *Fs;

  if (Root == NULL || mHdaImageHandle == NULL) return EFI_INVALID_PARAMETER;
  *Root = NULL;

  LoadedImage = NULL;
  Fs = NULL;
  Status = gBS->HandleProtocol(mHdaImageHandle, &gEfiLoadedImageProtocolGuid, (VOID **)&LoadedImage);
  if (Status != EFI_SUCCESS || LoadedImage == NULL) {
    return (Status == EFI_SUCCESS) ? EFI_DEVICE_ERROR : (EFI_ERROR(Status) ? Status : EFI_DEVICE_ERROR);
  }

  Status = gBS->HandleProtocol(LoadedImage->DeviceHandle, &gEfiSimpleFileSystemProtocolGuid, (VOID **)&Fs);
  if (Status != EFI_SUCCESS || Fs == NULL) {
    return (Status == EFI_SUCCESS) ? EFI_DEVICE_ERROR : (EFI_ERROR(Status) ? Status : EFI_DEVICE_ERROR);
  }

  Status = Fs->OpenVolume(Fs, Root);
  if (Status != EFI_SUCCESS) {
    *Root = NULL;
    return EFI_ERROR(Status) ? Status : EFI_DEVICE_ERROR;
  }
  if (*Root == NULL) return EFI_DEVICE_ERROR;
  return EFI_SUCCESS;
}




STATIC VOID HdaDumpConnectionList(EFI_FILE_HANDLE File, EFI_PCI_IO_PROTOCOL *PciIo, UINT8 Codec, UINT8 Nid) {
  UINT8 Conn[HDAU_MAX_CONNECTIONS];
  UINTN Count;
  UINTN Index;
  CHAR8 Line[512];
  UINTN Off;

  Count = HdaGetConnections(PciIo, Codec, Nid, Conn, HDAU_MAX_CONNECTIONS);
  if (Count == 0) return;

  Off = AsciiSPrint(Line, sizeof(Line), "  Connection: %u", (UINT32)Count);
  for (Index = 0; Index < Count; Index++) {
    /* Keep each fragment below the 512-byte HdaLog limit, including its
       "[HDAUDUMP] " prefix. Never silently omit the remaining NIDs. */
    if (Off + 6U >= sizeof(Line) - 16U) {
      HdaDumpWrite(File, "%a", Line);
      Off = AsciiSPrint(Line, sizeof(Line), "  Connection (continued):");
    }
    Off += AsciiSPrint(Line + Off, sizeof(Line) - Off, " 0x%02x", Conn[Index]);
  }
  HdaDumpWrite(File, "%a", Line);
}


STATIC EFI_STATUS HdaEnsureAppendLog(BOOLEAN *OpenedLog, BOOLEAN *SavedFileLog) {
  EFI_STATUS Status;
  EFI_STATUS CloseStatus;
  EFI_FILE_HANDLE Root;

  if (OpenedLog == NULL || SavedFileLog == NULL) return EFI_INVALID_PARAMETER;
  *OpenedLog = FALSE;
  *SavedFileLog = mHdaCfg.FileLog;
  if (mHdaLogFile != NULL) return EFI_SUCCESS;

  Root = NULL;
  Status = HdaOpenLoadedImageRoot(&Root);
  if (Status != EFI_SUCCESS) return Status;
  if (Root == NULL) return EFI_NOT_FOUND;

  Status = HdaOpenDebugLog(Root, TRUE);
  CloseStatus = HdaCloseFileHandleTerminal(&Root);
  if (Status != EFI_SUCCESS) return Status;
  if (CloseStatus != EFI_SUCCESS) {
    /* The append log was opened, but its root close failed. The caller will
       not own the log on error, so close it before returning. */
    (VOID)HdaCloseTrackedLogFile(FALSE);
    return CloseStatus;
  }

  mHdaCfg.FileLog = TRUE;
  *OpenedLog = TRUE;
  return EFI_SUCCESS;
}

STATIC VOID HdaCloseAppendLog(BOOLEAN OpenedLog, BOOLEAN SavedFileLog) {
  if (OpenedLog && mHdaLogFile != NULL) {
    (VOID)HdaCloseTrackedLogFile(TRUE);
  }
  if (OpenedLog) mHdaCfg.FileLog = SavedFileLog;
}

STATIC VOID HdaDumpNodeFull(EFI_FILE_HANDLE File, EFI_PCI_IO_PROTOCOL *PciIo, UINT8 Codec, UINT8 Nid) {
  UINT32 Wcaps;
  UINT32 Pcm;
  UINT32 Stream;
  UINT32 PinCap;
  UINT32 InAmp;
  UINT32 OutAmp;
  UINT32 ConnLen;
  UINT32 PowerStates;
  UINT32 ProcCap;
  UINT32 VolKnob;
  UINT32 Config;
  UINT32 PinCtl;
  UINT32 PinSense;
  UINT32 Eapd;
  UINT32 ConnSel;
  UINT32 Power;
  UINT32 ConvFmt;
  UINT32 StreamChan;
  UINT8 Type;
  UINT8 Device;
  UINT8 Port;
  UINT8 Assoc;
  UINT8 Seq;
  UINT8 ConnType;
  UINT8 Loc;
  UINT8 Color;

  Wcaps = HdaGetParam(PciIo, Codec, Nid, HDA_PARAM_AUDIO_WIDGET_CAP);
  Type = HdaWidgetType(Wcaps);
  Pcm = HdaGetParam(PciIo, Codec, Nid, HDA_PARAM_SUPPORTED_PCM);
  Stream = HdaGetParam(PciIo, Codec, Nid, HDA_PARAM_SUPPORTED_STREAM);
  PinCap = HdaGetParam(PciIo, Codec, Nid, HDA_PARAM_PIN_CAP);
  InAmp = HdaGetParam(PciIo, Codec, Nid, HDA_PARAM_AMP_IN_CAP);
  OutAmp = HdaGetParam(PciIo, Codec, Nid, HDA_PARAM_AMP_OUT_CAP);
  ConnLen = HdaGetParam(PciIo, Codec, Nid, HDA_PARAM_CONNECTION_LIST_LEN);
  PowerStates = HdaGetParam(PciIo, Codec, Nid, HDA_PARAM_POWER_STATES);
  ProcCap = HdaGetParam(PciIo, Codec, Nid, HDA_PARAM_PROCESSING_CAP);
  VolKnob = HdaGetParam(PciIo, Codec, Nid, HDA_PARAM_VOL_KNB_CAP);
  Config = 0;
  PinCtl = 0;
  PinSense = 0;
  Eapd = 0;
  ConnSel = 0;
  Power = 0;
  ConvFmt = 0;
  StreamChan = 0;

  HdaDumpWrite(File, "Node 0x%02x [%a] wcaps 0x%08x", Nid, HdaDumpWidgetTypeName(Type), Wcaps);
  HdaDumpWrite(File, "  Params: pcm=0x%08x stream=0x%08x inAmp=0x%08x outAmp=0x%08x connLen=0x%08x power=0x%08x proc=0x%08x volKnob=0x%08x",
               Pcm, Stream, InAmp, OutAmp, ConnLen, PowerStates, ProcCap, VolKnob);

  (VOID)HdaImmediateCommand(PciIo, Codec, Nid, HDA_VERB12(HDA_VERB_GET_POWER_STATE, 0), &Power);
  HdaDumpWrite(File, "  Power: setting=0x%08x", Power);

  if (Type == HDA_WIDGET_TYPE_AUDIO_OUT || Type == HDA_WIDGET_TYPE_AUDIO_IN) {
    (VOID)HdaImmediateCommand(PciIo, Codec, Nid, HDA_VERB4(HDA_VERB_GET_CONVERTER_FORMAT, 0), &ConvFmt);
    (VOID)HdaImmediateCommand(PciIo, Codec, Nid, HDA_VERB12(HDA_VERB_GET_CONVERTER_STREAM_CHANNEL, 0), &StreamChan);
    HdaDumpWrite(File, "  Converter: stream/channel=0x%08x format=0x%08x", StreamChan, ConvFmt);
  }

  if (ConnLen != 0) {
    (VOID)HdaImmediateCommand(PciIo, Codec, Nid, HDA_VERB12(HDA_VERB_GET_CONNECTION_SELECT, 0), &ConnSel);
    HdaDumpWrite(File, "  Connection Select: 0x%08x", ConnSel);
    HdaDumpConnectionList(File, PciIo, Codec, Nid);
  }

  if (Type == HDA_WIDGET_TYPE_PIN_COMPLEX) {
    (VOID)HdaImmediateCommand(PciIo, Codec, Nid, HDA_VERB12(HDA_VERB_GET_CONFIG_DEFAULT, 0), &Config);
    (VOID)HdaImmediateCommand(PciIo, Codec, Nid, HDA_VERB12(HDA_VERB_GET_PIN_WIDGET_CONTROL, 0), &PinCtl);
    (VOID)HdaImmediateCommand(PciIo, Codec, Nid, HDA_VERB12(HDA_VERB_GET_PIN_SENSE, 0), &PinSense);
    (VOID)HdaImmediateCommand(PciIo, Codec, Nid, HDA_VERB12(HDA_VERB_GET_EAPD_BTL, 0), &Eapd);
    Device = HdaDefaultDevice(Config);
    Port = HdaDefaultPortConnectivity(Config);
    Assoc = HdaDefaultAssociation(Config);
    Seq = HdaDefaultSequence(Config);
    ConnType = HdaDefaultConnectionType(Config);
    Loc = HdaDefaultLocation(Config);
    Color = HdaDefaultColor(Config);
    HdaDumpWrite(File, "  Pincap: 0x%08x", PinCap);
    HdaDumpWrite(File, "  Pin Default: 0x%08x [%a, %a, port=%u/%a, assoc=0x%x, seq=0x%x, loc=0x%x, color=0x%x]",
                 Config, HdaDumpDefaultDeviceName(Device), HdaDumpConnTypeName(ConnType), Port, HdaDumpPortConnName(Port), Assoc, Seq, Loc, Color);
    HdaDumpWrite(File, "  Pin-ctls: 0x%08x", PinCtl);
    HdaDumpWrite(File, "  Pin Sense: 0x%08x", PinSense);
    HdaDumpWrite(File, "  EAPD/BTL: 0x%08x", Eapd);
  }
}

STATIC EFI_STATUS HdaDumpCodecFull(EFI_FILE_HANDLE File, EFI_PCI_IO_PROTOCOL *PciIo, HDAU_PCI_INFO *Info, UINT8 Codec, UINT16 StateSts) {
  EFI_STATUS Status;
  UINT32 VendorId;
  UINT32 RevisionId;
  UINT8 FgStart;
  UINT8 FgCount;
  UINT8 WStart;
  UINT8 WCount;
  UINTN FgIndex;
  UINTN WIndex;

  if (PciIo == NULL || Info == NULL) return EFI_INVALID_PARAMETER;

  VendorId = 0;
  RevisionId = 0;
  Status = HdaImmediateCommand(PciIo, Codec, 0,
                               HDA_VERB12(HDA_VERB_GET_PARAMETER, HDA_PARAM_VENDOR_ID), &VendorId);
  if (EFI_ERROR(Status)) {
    HdaDumpWrite(File, "Codec %u vendor-id read failed: %r", (UINT32)Codec, Status);
    return Status;
  }
  Status = HdaImmediateCommand(PciIo, Codec, 0,
                               HDA_VERB12(HDA_VERB_GET_PARAMETER, HDA_PARAM_REV_ID), &RevisionId);
  if (EFI_ERROR(Status)) {
    HdaDumpWrite(File, "Codec %u revision-id read failed: %r", (UINT32)Codec, Status);
    return Status;
  }

  HdaDumpWrite(File, "");
  HdaDumpWrite(File, "Codec: 0x%08x", VendorId);
  HdaDumpWrite(File, "Address: %u", (UINT32)Codec);
  HdaDumpWrite(File, "Controller: seg=%u bus=%02x dev=%02x func=%02x pci=%04x:%04x class=%02x%02x%02x STATESTS=0x%04x",
               (UINT32)Info->Segment, (UINT32)Info->Bus, (UINT32)Info->Device, (UINT32)Info->Function,
               Info->VendorId, Info->DeviceId, Info->ClassCode[2], Info->ClassCode[1], Info->ClassCode[0], StateSts);
  HdaDumpWrite(File, "Vendor Id: 0x%08x", VendorId);
  HdaDumpWrite(File, "Revision Id: 0x%08x", RevisionId);

  if (!HdaGetSubnodes(PciIo, Codec, 0, &FgStart, &FgCount)) {
    HdaDumpWrite(File, "No root subnodes found");
    return EFI_NOT_FOUND;
  }

  HdaDumpWrite(File, "Root Node: start=0x%02x count=%u", FgStart, (UINT32)FgCount);
  for (FgIndex = 0; FgIndex < FgCount; FgIndex++) {
    UINT8 Fg = (UINT8)(FgStart + FgIndex);
    UINT32 FunctionType = HdaGetParam(PciIo, Codec, Fg, HDA_PARAM_FUNCTION_TYPE);
    UINT8 FunctionGroupType = (UINT8)(FunctionType & 0xFFU);
    UINT32 AfgCaps = HdaGetParam(PciIo, Codec, Fg, HDA_PARAM_AFG_CAP);
    UINT32 SubsystemId = 0;
    HdaDumpWrite(File, "");
    HdaDumpWrite(File, "Function Group 0x%02x: function=0x%08x afgCaps=0x%08x", Fg, FunctionType, AfgCaps);

    /* F20h is specification-defined for Audio (01h) and Modem (02h)
       Function Groups, not for the codec root or vendor-defined groups. */
    if (FunctionGroupType == 0x01U || FunctionGroupType == 0x02U) {
      Status = HdaImmediateCommand(PciIo, Codec, Fg,
                                   HDA_VERB12(HDA_VERB_GET_SUBSYSTEM_ID, 0), &SubsystemId);
      if (EFI_ERROR(Status)) {
        HdaDumpWrite(File, "  Subsystem Id: unavailable (%r)", Status);
      } else {
        HdaDumpWrite(File, "  Subsystem Id: 0x%08x", SubsystemId);
      }
    } else {
      HdaDumpWrite(File, "  Subsystem Id: not defined for function-group type 0x%02x", FunctionGroupType);
    }

    if (!HdaGetSubnodes(PciIo, Codec, Fg, &WStart, &WCount)) {
      HdaDumpWrite(File, "  No widget subnodes");
      continue;
    }

    HdaDumpWrite(File, "  Widget nodes: start=0x%02x count=%u", WStart, (UINT32)WCount);
    for (WIndex = 0; WIndex < WCount && WIndex < HDAU_MAX_WIDGETS; WIndex++) {
      HdaDumpNodeFull(File, PciIo, Codec, (UINT8)(WStart + WIndex));
    }
  }
  return EFI_SUCCESS;
}

STATIC EFI_STATUS HdaDumpControllerFull(EFI_FILE_HANDLE File, EFI_PCI_IO_PROTOCOL *PciIo, HDAU_PCI_INFO *Info) {
  EFI_STATUS Status;
  EFI_STATUS RestoreStatus;
  UINT64 OriginalAttributes;
  UINT64 RestoreAttributes;
  UINT64 RestoreMask;
  BOOLEAN HaveOriginalAttributes;
  UINT16 StateSts;
  UINT8 Codec;
  UINT16 Gcap;
  UINT8 Vmin;
  UINT8 Vmaj;
  UINT16 OutPay;
  UINT16 InPay;

  if (PciIo == NULL || Info == NULL) return EFI_INVALID_PARAMETER;

  HdaDumpWrite(File, "");
  HdaDumpWrite(File, "================================================================");
  HdaDumpWrite(File, "HDA Controller seg=%u bus=%02x dev=%02x func=%02x pci=%04x:%04x class=%02x%02x%02x",
               (UINT32)Info->Segment, (UINT32)Info->Bus, (UINT32)Info->Device, (UINT32)Info->Function,
               Info->VendorId, Info->DeviceId, Info->ClassCode[2], Info->ClassCode[1], Info->ClassCode[0]);

  OriginalAttributes = 0;
  HaveOriginalAttributes = FALSE;
  Status = HdaGetPciAttributesExact(PciIo, &OriginalAttributes);
  if (EFI_ERROR(Status)) {
    /* Diagnostics must be transactional too. Never enable MMIO when the
       original PCI attribute state is unknown because there would be no
       deterministic rollback path after the dump. */
    HdaDumpWrite(File, "Controller attributes snapshot failed: %r", Status);
    return Status;
  }
  HaveOriginalAttributes = TRUE;

  /* The diagnostic owner itself requires MMIO only. If Immediate Command
     later falls back to CORB/RIRB, that fallback verifies idle rings before
     transactionally enabling/restoring BUS_MASTER on its own. */
  Status = PciIo->Attributes(PciIo, EfiPciIoAttributeOperationEnable,
                             EFI_PCI_IO_ATTRIBUTE_MEMORY, NULL);
  if (Status != EFI_SUCCESS) {
    if (!EFI_ERROR(Status)) Status = EFI_DEVICE_ERROR;
    HdaDumpWrite(File, "Controller attributes enable failed: %r", Status);
    goto Exit;
  }
  Status = HdaVerifyPciAttributeBits(
             PciIo,
             OriginalAttributes | EFI_PCI_IO_ATTRIBUTE_MEMORY,
             EFI_PCI_IO_ATTRIBUTE_MEMORY
             );
  if (EFI_ERROR(Status)) {
    HdaDumpWrite(File, "Controller memory attribute enable read-back failed: %r", Status);
    goto Exit;
  }

  Status = HdaControllerEnsureReady(PciIo);
  if (EFI_ERROR(Status)) {
    HdaDumpWrite(File, "Controller ready check failed: %r", Status);
    goto Exit;
  }

  Gcap = 0; Vmin = 0; Vmaj = 0; OutPay = 0; InPay = 0; StateSts = 0;
  Status = HdaMemRead16(PciIo, HDA_REG_GCAP, &Gcap);
  if (EFI_ERROR(Status)) {
    HdaDumpWrite(File, "GCAP read failed: %r", Status);
    goto Exit;
  }
  Status = HdaMemRead8(PciIo, HDA_REG_VMIN, &Vmin);
  if (EFI_ERROR(Status)) {
    HdaDumpWrite(File, "VMIN read failed: %r", Status);
    goto Exit;
  }
  Status = HdaMemRead8(PciIo, HDA_REG_VMAJ, &Vmaj);
  if (EFI_ERROR(Status)) {
    HdaDumpWrite(File, "VMAJ read failed: %r", Status);
    goto Exit;
  }
  Status = HdaMemRead16(PciIo, HDA_REG_OUTPAY, &OutPay);
  if (EFI_ERROR(Status)) {
    HdaDumpWrite(File, "OUTPAY read failed: %r", Status);
    goto Exit;
  }
  Status = HdaMemRead16(PciIo, HDA_REG_INPAY, &InPay);
  if (EFI_ERROR(Status)) {
    HdaDumpWrite(File, "INPAY read failed: %r", Status);
    goto Exit;
  }
  Status = HdaMemRead16(PciIo, HDA_REG_STATESTS, &StateSts);
  if (EFI_ERROR(Status)) {
    HdaDumpWrite(File, "STATESTS read failed: %r", Status);
    goto Exit;
  }
  HdaDumpWrite(File, "GCAP=0x%04x version=%u.%u OUTPAY=0x%04x INPAY=0x%04x STATESTS=0x%04x", Gcap, Vmaj, Vmin, OutPay, InPay, StateSts);

  if (StateSts == 0) {
    HdaDumpWrite(File, "No codecs reported by STATESTS");
    Status = EFI_NOT_FOUND;
    goto Exit;
  }

  for (Codec = 0; Codec < HDAU_MAX_CODECS; Codec++) {
    if ((StateSts & (1U << Codec)) == 0) continue;
    Status = HdaDumpCodecFull(File, PciIo, Info, Codec, StateSts);
    if (EFI_ERROR(Status)) {
      HdaDumpWrite(File, "Codec %u dump failed: %r", (UINT32)Codec, Status);
      goto Exit;
    }
    if (mHdaRetainedCorbRirbValid && mHdaRetainedCorbRirbState.PciIo == PciIo) {
      HdaDumpWrite(File, "Codec enumeration stopped: CORB/RIRB cleanup ownership remains pending");
      Status = EFI_ABORTED;
      goto Exit;
    }
  }
  Status = EFI_SUCCESS;

Exit:
  if (HaveOriginalAttributes) {
    RestoreAttributes = OriginalAttributes;
    if (mHdaRetainedCorbRirbValid &&
        mHdaRetainedCorbRirbState.PciIo == PciIo &&
        (mHdaRetainedCorbRirbState.DmaContainment || mHdaRetainedCorbRirbState.DmaStopUnconfirmed)) {
      /* A failed ring stop has already established (or still requires) DMA
         containment.  Restoring the original BUS_MASTER bit here would
         re-arm DMA against buffers whose ownership is deliberately retained. */
      RestoreAttributes &= ~((UINT64)EFI_PCI_IO_ATTRIBUTE_BUS_MASTER);
      HdaDumpWrite(File, "PCI bus-master restore suppressed while CORB/RIRB DMA ownership is retained");
    }
    RestoreMask = ((OriginalAttributes & EFI_PCI_IO_ATTRIBUTE_MEMORY) == 0)
                    ? EFI_PCI_IO_ATTRIBUTE_MEMORY
                    : 0;
    if (mHdaRetainedCorbRirbValid &&
        mHdaRetainedCorbRirbState.PciIo == PciIo &&
        (mHdaRetainedCorbRirbState.DmaContainment ||
         mHdaRetainedCorbRirbState.DmaStopUnconfirmed ||
         mHdaRetainedCorbRirbState.BusMasterEnabledByFallback)) {
      RestoreMask |= EFI_PCI_IO_ATTRIBUTE_BUS_MASTER;
    }
    RestoreStatus = HdaApplyPciAttributeBits(
                      PciIo,
                      RestoreAttributes,
                      RestoreMask
                      );
    if (EFI_ERROR(RestoreStatus)) {
      HdaDumpWrite(File, "Controller managed attributes restore failed: %r", RestoreStatus);
      /* Rollback failure is the critical result for a diagnostic operation.
         Surface it even if enumeration also returned EFI_NOT_FOUND or another
         non-success status, so callers never classify a mutated controller as
         a successfully completed dump. */
      Status = RestoreStatus;
    }
    if (!EFI_ERROR(RestoreStatus) && mHdaRetainedCorbRirbValid &&
               mHdaRetainedCorbRirbState.PciIo == PciIo &&
               (RestoreAttributes & EFI_PCI_IO_ATTRIBUTE_BUS_MASTER) == 0 &&
               (mHdaRetainedCorbRirbState.DmaContainment || mHdaRetainedCorbRirbState.DmaStopUnconfirmed)) {
      mHdaRetainedCorbRirbState.DmaContainment = TRUE;
    }
    if (!EFI_ERROR(RestoreStatus)) {
      HdaDischargeRetainedCorbRirbBusMasterRestore(PciIo, RestoreAttributes, RestoreMask);
    }
  }
  return Status;
}

STATIC BOOLEAN HdaPathSearch(
  EFI_PCI_IO_PROTOCOL *PciIo,
  UINT8 Codec,
  UINT8 StartPin,
  UINT8 TargetDac,
  UINT8 *Path,
  UINTN *PathLen
) {
  UINT8 Queue[HDAU_MAX_PATH_DEPTH];
  UINT8 Parent[HDAU_MAX_PATH_DEPTH];
  UINT8 Reverse[HDAU_MAX_PATH_DEPTH];
  BOOLEAN Visited[HDAU_MAX_PATH_DEPTH];
  BOOLEAN ParentValid[HDAU_MAX_PATH_DEPTH];
  UINTN Head;
  UINTN Tail;
  UINTN ReverseLen;
  UINT8 Current;
  UINTN Index;

  if (PciIo == NULL || Path == NULL || PathLen == NULL ||
      StartPin == 0 || TargetDac == 0 ||
      StartPin > HDAU_MAX_NID || TargetDac > HDAU_MAX_NID) {
    return FALSE;
  }

  *PathLen = 0;
  ZeroMem(Path, sizeof(UINT8) * HDAU_MAX_PATH_DEPTH);
  ZeroMem(Queue, sizeof(Queue));
  ZeroMem(Visited, sizeof(Visited));
  ZeroMem(ParentValid, sizeof(ParentValid));

  Head = 0;
  Tail = 0;
  Queue[Tail++] = StartPin;
  Visited[StartPin] = TRUE;

  /* Breadth-first traversal removes the former arbitrary eight-node route
     limit while keeping memory bounded by the supported direct 7-bit HDA NID
     domain. It also returns the shortest live route from the selected pin to DAC. */
  while (Head < Tail) {
    UINT8 Conn[HDAU_MAX_CONNECTIONS];
    UINTN Count;
    UINT32 Caps;
    UINT8 Type;

    Current = Queue[Head++];
    if (Current == TargetDac) break;

    Caps = HdaGetParam(PciIo, Codec, Current, HDA_PARAM_AUDIO_WIDGET_CAP);
    Type = HdaWidgetType(Caps);
    if (Current != StartPin && Type == HDA_WIDGET_TYPE_PIN_COMPLEX) continue;
    if (Type != HDA_WIDGET_TYPE_PIN_COMPLEX &&
        Type != HDA_WIDGET_TYPE_AUDIO_MIXER &&
        Type != HDA_WIDGET_TYPE_AUDIO_SELECTOR) {
      continue;
    }

    Count = HdaGetConnections(PciIo, Codec, Current, Conn, ARRAY_SIZE(Conn));
    for (Index = 0; Index < Count; Index++) {
      UINT8 Next = Conn[Index];
      if (Next == 0 || Visited[Next]) continue;
      Visited[Next] = TRUE;
      Parent[Next] = Current;
      ParentValid[Next] = TRUE;
      if (Tail >= ARRAY_SIZE(Queue)) return FALSE;
      Queue[Tail++] = Next;
    }
  }

  if (!Visited[TargetDac]) return FALSE;

  ReverseLen = 0;
  Current = TargetDac;
  for (;;) {
    if (ReverseLen >= ARRAY_SIZE(Reverse)) return FALSE;
    Reverse[ReverseLen++] = Current;
    if (Current == StartPin) break;
    if (!ParentValid[Current]) return FALSE;
    Current = Parent[Current];
  }

  for (Index = 0; Index < ReverseLen; Index++) {
    Path[Index] = Reverse[ReverseLen - Index - 1U];
  }
  *PathLen = ReverseLen;
  return TRUE;
}

STATIC BOOLEAN HdaReadPinSense(
  EFI_PCI_IO_PROTOCOL *PciIo,
  UINT8 Codec,
  UINT8 Nid,
  UINT32 PinCap,
  UINT32 *SenseOut
) {
  EFI_STATUS Status;
  UINT32 Sense;

  if (PciIo == NULL || SenseOut == NULL) return FALSE;
  *SenseOut = 0;
  if ((PinCap & HDA_PIN_CAP_PRESENCE_DETECT) == 0) return FALSE;
  if ((PinCap & HDA_PIN_CAP_TRIGGER_REQUIRED) != 0) {
    Status = HdaSetVerb(PciIo, Codec, Nid, HDA_VERB12(HDA_VERB_SET_PIN_SENSE, 0));
    if (EFI_ERROR(Status)) return FALSE;
    HdaStall(100);
  }
  Sense = 0;
  Status = HdaImmediateCommand(PciIo, Codec, Nid,
                               HDA_VERB12(HDA_VERB_GET_PIN_SENSE, 0), &Sense);
  if (EFI_ERROR(Status)) return FALSE;
  *SenseOut = Sense;
  return TRUE;
}

STATIC INTN HdaPinPriority(EFI_PCI_IO_PROTOCOL *PciIo, UINT8 Codec, UINT8 Nid) {
  EFI_STATUS Status;
  UINT32 PinCap = HdaGetParam(PciIo, Codec, Nid, HDA_PARAM_PIN_CAP);
  UINT32 Config = 0;
  UINT8 Device;
  UINT8 PortConnectivity;
  UINT8 Association;
  UINT8 Sequence;
  UINT8 ConnectionType;
  UINT8 Location;
  UINT8 GrossLocation;
  UINT8 Color;
  INTN Score = 0;
  BOOLEAN ForcedPin = (mHdaCfg.ForcePin != HDAU_FORCE_NID_NONE && Nid == (UINT8)mHdaCfg.ForcePin);
  BOOLEAN DigitalPin;
  UINT32 PinSense;
  BOOLEAN HavePinSense;
  BOOLEAN Present;
  BOOLEAN EldValid;
  CONST CHAR8 *PolicyName = (mHdaActivePolicy == NULL) ? "generic" : mHdaActivePolicy->Name;

  if ((PinCap & HDA_PIN_CAP_OUTPUT) == 0) {
    if (mHdaCfg.Debug) HdaLog("[HDAU] pin 0x%x skip: no output capability pincap=0x%x", Nid, PinCap);
    return -1;
  }

  Status = HdaImmediateCommand(PciIo, Codec, Nid,
                               HDA_VERB12(HDA_VERB_GET_CONFIG_DEFAULT, 0), &Config);
  if (EFI_ERROR(Status)) {
    if (mHdaCfg.Debug) {
      HdaLog("[HDAU] pin 0x%x skip: ConfigDefault read failed status=%r", Nid, Status);
    }
    return -1;
  }
  Device = HdaDefaultDevice(Config);
  PortConnectivity = HdaDefaultPortConnectivity(Config);
  Association = HdaDefaultAssociation(Config);
  Sequence = HdaDefaultSequence(Config);
  ConnectionType = HdaDefaultConnectionType(Config);
  Location = HdaDefaultLocation(Config);
  GrossLocation = (UINT8)((Location >> 4) & 0x03U);
  Color = HdaDefaultColor(Config);
  DigitalPin = HdaPinDefaultIsDigital(Config) ||
               ((PinCap & (HDA_PIN_CAP_HDMI | HDA_PIN_CAP_DISPLAYPORT)) != 0);
  PinSense = 0;
  HavePinSense = FALSE;
  Present = FALSE;
  EldValid = FALSE;
  if (mHdaCfg.PinSensePolicy) {
    HavePinSense = HdaReadPinSense(PciIo, Codec, Nid, PinCap, &PinSense);
    Present = HavePinSense && ((PinSense & HDA_PIN_SENSE_PRESENCE) != 0);
    EldValid = HavePinSense && ((PinSense & HDA_PIN_SENSE_ELD_VALID) != 0);
  }

  if (!ForcedPin && !HdaPinDefaultLooksConnected(Config)) {
    if (mHdaCfg.Debug) {
      HdaLog("[HDAU] pin 0x%x skip: disabled/no-connect config=0x%x port=%u assoc=0x%x seq=0x%x device=0x%x conn=0x%x",
             Nid, Config, PortConnectivity, Association, Sequence, Device, ConnectionType);
    }
    return -1;
  }

  if (!ForcedPin && DigitalPin && !mHdaCfg.AllowDigitalPins) {
    if (mHdaCfg.Debug) {
      HdaLog("[HDAU] pin 0x%x skip: digital output disabled for analog boot chime config=0x%x device=0x%x conn=0x%x policy=%a",
             Nid, Config, Device, ConnectionType, PolicyName);
    }
    return -1;
  }

  if (!ForcedPin && DigitalPin && mHdaCfg.DigitalPipeline &&
      ((PinCap & (HDA_PIN_CAP_HDMI | HDA_PIN_CAP_DISPLAYPORT)) != 0) &&
      HavePinSense && (!Present || !EldValid)) {
    if (mHdaCfg.Debug) {
      HdaLog("[HDAU] pin 0x%x skip: HDMI/DP sink is not ready sense=0x%x present=%u eld=%u",
             Nid, PinSense, Present ? 1 : 0, EldValid ? 1 : 0);
    }
    return -1;
  }

  if (ForcedPin && !HdaPinDefaultLooksConnected(Config) && mHdaCfg.Debug) {
    HdaLog("[HDAU] pin 0x%x forced despite disabled/no-connect config=0x%x port=%u assoc=0x%x seq=0x%x",
           Nid, Config, PortConnectivity, Association, Sequence);
  }

  if (Device == HDA_DEFAULT_DEVICE_SPEAKER) {
    Score = 420;
  } else if (Device == HDA_DEFAULT_DEVICE_HP_OUT) {
    Score = 340;
  } else if (Device == HDA_DEFAULT_DEVICE_LINE_OUT) {
    Score = 300;
  } else if ((PinCap & HDA_PIN_CAP_HEADPHONE) != 0) {
    Score = 190;
  } else {
    Score = 120;
  }

  if (DigitalPin) {
    Score -= 220;
  }

  /* HDA Default Configuration gross location: 0=external, 1=internal.
     Prefer an internal fixed speaker for a boot chime without excluding
     external endpoints that are positively detected. */
  if (GrossLocation == 1U) {
    Score += (Device == HDA_DEFAULT_DEVICE_SPEAKER) ? 60 : 10;
  } else if (GrossLocation == 0U && Present) {
    Score += 20;
  }

  if (HavePinSense) {
    if (Present) {
      if (Device == HDA_DEFAULT_DEVICE_HP_OUT) Score += 260;
      else if (DigitalPin) Score += 220;
      else Score += 120;
    } else if (DigitalPin) {
      /* HDMI/DP readiness is handled above.  Keep an unconfirmed generic
         digital endpoint late without changing analog route ordering. */
      Score -= 220;
    } else if (mHdaCfg.Debug) {
      /* Analog presence detection is not reliable during preboot on every
         codec/controller.  Absence is diagnostic only: v1.0.31-compatible
         speaker/headphone/line-out ranking must remain intact. */
      HdaLog("[HDAU] pin 0x%x analog sense absent; preserving default-config score", Nid);
    }
  }

  if (mHdaActivePolicy != NULL &&
      (mHdaActivePolicy->Flags & HDAU_CPF_SKIP_BY_DEFAULT) != 0 &&
      !ForcedPin) {
    Score -= 600;
  }

  if (ConnectionType == HDA_DEFAULT_CONN_1_8_JACK) Score += 40;
  if (ConnectionType == HDA_DEFAULT_CONN_UNKNOWN) Score += 8;

  if (Association != 0x0F) {
    Score += (INTN)((0x0F - Association) * 4);
  }
  Score += (INTN)(0x0F - Sequence);

  if (ForcedPin) {
    Score += 1000;
  }

  if (mHdaCfg.Debug) {
    HdaLog("[HDAU] pin 0x%x candidate score=%d config=0x%x port=%u assoc=0x%x seq=0x%x device=0x%x conn=0x%x loc=0x%x color=0x%x pincap=0x%x policy=%a",
           Nid, (INT32)Score, Config, PortConnectivity, Association, Sequence,
           Device, ConnectionType, Location, Color, PinCap, PolicyName);
    if (HavePinSense) {
      HdaLog("[HDAU] pin 0x%x sense=0x%x present=%u eld=%u", Nid, PinSense,
             Present ? 1 : 0, EldValid ? 1 : 0);
    }
  }

  return Score;
}


STATIC BOOLEAN HdaIsOutputPin(EFI_PCI_IO_PROTOCOL *PciIo, UINT8 Codec, UINT8 Nid);
STATIC BOOLEAN HdaIsAudioOutputDac(EFI_PCI_IO_PROTOCOL *PciIo, UINT8 Codec, UINT8 Afg, UINT8 Nid);
STATIC BOOLEAN HdaPathContainsPinIntermediate(EFI_PCI_IO_PROTOCOL *PciIo, UINT8 Codec, CONST UINT8 *Path, UINTN PathLen);
STATIC UINTN HdaCollectAudioOutputDacs(EFI_PCI_IO_PROTOCOL *PciIo, UINT8 Codec, UINT8 Afg, UINT8 *Dacs, UINTN DacCapacity);
STATIC UINTN HdaBuildAlcIdDacOrder(CONST HDAU_ALCID_PROFILE *Profile, UINTN PairIndex, CONST UINT8 *RuntimeDacs, UINTN RuntimeDacCount, UINT8 *Order, UINTN OrderCapacity);


STATIC BOOLEAN HdaClassifySelectedRoute(HDAU_CONTEXT *Context) {
  UINT32 Config;
  UINT32 PinCap;
  EFI_STATUS Status;
  BOOLEAN ForcedPin;

  if (Context == NULL || Context->PciIo == NULL || Context->Pin == 0) return FALSE;
  Config = 0;
  Status = HdaGetParamChecked(Context->PciIo, Context->Codec, Context->Pin,
                              HDA_PARAM_PIN_CAP, &PinCap);
  if (EFI_ERROR(Status)) {
    Context->HdmiRoute = FALSE;
    Context->DisplayPortRoute = FALSE;
    Context->DigitalRoute = FALSE;
    return FALSE;
  }
  if (EFI_ERROR(HdaImmediateCommand(Context->PciIo, Context->Codec, Context->Pin,
                                    HDA_VERB12(HDA_VERB_GET_CONFIG_DEFAULT, 0), &Config))) {
    Context->HdmiRoute = FALSE;
    Context->DisplayPortRoute = FALSE;
    Context->DigitalRoute = FALSE;
    return FALSE;
  }
  Context->HdmiRoute = ((PinCap & HDA_PIN_CAP_HDMI) != 0);
  Context->DisplayPortRoute = ((PinCap & HDA_PIN_CAP_DISPLAYPORT) != 0);
  Context->DigitalRoute = Context->HdmiRoute || Context->DisplayPortRoute ||
                          HdaPinDefaultIsDigital(Config);
  ForcedPin = (mHdaCfg.ForcePin != HDAU_FORCE_NID_NONE &&
               Context->Pin == (UINT8)mHdaCfg.ForcePin);
  if (Context->DigitalRoute && !mHdaCfg.AllowDigitalPins && !ForcedPin) {
    if (mHdaCfg.Debug) {
      HdaLog("[HDAU] selected route rejected: pin=0x%x is digital by live pin capabilities/config and AllowDigitalPins=0",
             Context->Pin);
    }
    return FALSE;
  }
  return TRUE;
}

STATIC BOOLEAN HdaValidateFullRoute(
  HDAU_CONTEXT *Context,
  UINT8 Afg,
  CONST UINT8 *Route,
  UINTN RouteLen
) {
  UINTN Index;

  if (Context == NULL || Context->PciIo == NULL || Route == NULL ||
      RouteLen < 2 || RouteLen > HDAU_MAX_PATH_DEPTH) return FALSE;
  if (!HdaIsOutputPin(Context->PciIo, Context->Codec, Route[0])) return FALSE;
  if (!HdaIsAudioOutputDac(Context->PciIo, Context->Codec, Afg, Route[RouteLen - 1U])) return FALSE;
  if (HdaPathContainsPinIntermediate(Context->PciIo, Context->Codec, Route, RouteLen)) return FALSE;

  for (Index = 0; Index + 1U < RouteLen; Index++) {
    UINT8 Connections[HDAU_MAX_CONNECTIONS];
    UINTN Count;
    UINTN ConnIndex;
    BOOLEAN Found;

    Count = HdaGetConnections(Context->PciIo, Context->Codec, Route[Index],
                              Connections, ARRAY_SIZE(Connections));
    Found = FALSE;
    for (ConnIndex = 0; ConnIndex < Count; ConnIndex++) {
      if (Connections[ConnIndex] == Route[Index + 1U]) {
        Found = TRUE;
        break;
      }
    }
    if (!Found) return FALSE;
  }
  return HdaPathInputAmpsProgrammable(Context->PciIo, Context->Codec, Afg, Route, RouteLen);
}

STATIC BOOLEAN HdaTryFullAlcIdRoute(
  HDAU_CONTEXT *Context,
  UINT32 CodecId,
  UINT8 Afg,
  CONST HDAU_ALCID_PROFILE *Profile
) {
  UINTN Index;
  BOOLEAN HaveBest;
  INTN BestScore;
  CONST HDAU_ALCID_ROUTE_PROFILE *BestProfile;
  CONST UINT8 *BestRoute;

  if (!mHdaCfg.FullPathMap || Context == NULL || Profile == NULL) return FALSE;
  HaveBest = FALSE;
  BestScore = 0;
  BestProfile = NULL;
  BestRoute = NULL;

  for (Index = 0; Index < ARRAY_SIZE(mHdaAlcRouteProfiles); Index++) {
    CONST HDAU_ALCID_ROUTE_PROFILE *RouteProfile = &mHdaAlcRouteProfiles[Index];
    UINTN End;
    CONST UINT8 *Route;
    UINT32 Config;
    UINT32 PinCap;
    BOOLEAN Digital;
    INTN Score;

    if (RouteProfile->CodecId == 0 || RouteProfile->NodeCount < 2) continue;
    if (RouteProfile->CodecId != CodecId || RouteProfile->LayoutId != Profile->LayoutId) continue;
    End = (UINTN)RouteProfile->NodeOffset + (UINTN)RouteProfile->NodeCount;
    if (End < RouteProfile->NodeOffset || End > ARRAY_SIZE(mHdaAlcRouteNodes) ||
        RouteProfile->NodeCount > HDAU_MAX_PATH_DEPTH) continue;
    Route = &mHdaAlcRouteNodes[RouteProfile->NodeOffset];
    if (Route[0] != RouteProfile->Pin || Route[RouteProfile->NodeCount - 1U] != RouteProfile->Dac) continue;
    if (mHdaCfg.ForcePin != HDAU_FORCE_NID_NONE && Route[0] != (UINT8)mHdaCfg.ForcePin) continue;
    if (mHdaCfg.ForceDac != HDAU_FORCE_NID_NONE &&
        Route[RouteProfile->NodeCount - 1U] != (UINT8)mHdaCfg.ForceDac) continue;

    Config = 0;
    PinCap = HdaGetParam(Context->PciIo, Context->Codec, Route[0], HDA_PARAM_PIN_CAP);
    if (EFI_ERROR(HdaImmediateCommand(Context->PciIo, Context->Codec, Route[0],
                                      HDA_VERB12(HDA_VERB_GET_CONFIG_DEFAULT, 0), &Config))) {
      continue;
    }
    Digital = HdaPinDefaultIsDigital(Config) ||
              ((PinCap & (HDA_PIN_CAP_HDMI | HDA_PIN_CAP_DISPLAYPORT)) != 0);
    if (Digital && !mHdaCfg.AllowDigitalPins) continue;
    if (!HdaValidateFullRoute(Context, Afg, Route, RouteProfile->NodeCount)) continue;

    Score = HdaPinPriority(Context->PciIo, Context->Codec, Route[0]);
    if (Score < 0) continue;
    Score -= (INTN)RouteProfile->NodeCount;
    if (!HaveBest || Score > BestScore) {
      HaveBest = TRUE;
      BestScore = Score;
      BestProfile = RouteProfile;
      BestRoute = Route;
    }
  }

  if (!HaveBest || BestProfile == NULL || BestRoute == NULL) return FALSE;
  Context->Afg = Afg;
  Context->Pin = BestRoute[0];
  Context->Dac = BestRoute[BestProfile->NodeCount - 1U];
  Context->PathLen = BestProfile->NodeCount;
  ZeroMem(Context->Path, sizeof(Context->Path));
  CopyMem(Context->Path, BestRoute, Context->PathLen * sizeof(Context->Path[0]));
  if (!HdaClassifySelectedRoute(Context)) return FALSE;
  mHdaActiveAlcIdProfile = Profile;
  HdaLog("[HDAU] exact AppleALC full PathMap selected: codec=0x%08x alcid=%u profile=%a afg=0x%x pin=0x%x dac=0x%x nodes=%u score=%d",
         CodecId, (UINT32)Profile->LayoutId, BestProfile->Name, Afg,
         Context->Pin, Context->Dac, (UINT32)Context->PathLen, (INT32)BestScore);
  return TRUE;
}

STATIC BOOLEAN HdaIsAudioOutputDac(EFI_PCI_IO_PROTOCOL *PciIo, UINT8 Codec, UINT8 Afg, UINT8 Nid);

STATIC BOOLEAN HdaTryAlcIdProfilePath(
  HDAU_CONTEXT *Context,
  UINT32 CodecId,
  UINT8 Afg,
  CONST HDAU_ALCID_PROFILE *Profile
) {
  EFI_PCI_IO_PROTOCOL *PciIo;
  UINT8 Codec;
  UINT8 RuntimeDacs[HDAU_MAX_WIDGETS];
  UINTN RuntimeDacCount;
  UINTN Pass;

  if (Context == NULL || Profile == NULL || Afg == 0) return FALSE;

  PciIo = Context->PciIo;
  Codec = Context->Codec;
  /* Route discovery is read-only. ConfigData is applied only after the
     transactional snapshot inside HdaUnmutePath(). */
  if (HdaTryFullAlcIdRoute(Context, CodecId, Afg, Profile)) return TRUE;
  ZeroMem(RuntimeDacs, sizeof(RuntimeDacs));
  RuntimeDacCount = HdaCollectAudioOutputDacs(PciIo, Codec, Afg, RuntimeDacs, ARRAY_SIZE(RuntimeDacs));
  if (RuntimeDacCount == 0) {
    HdaLog("[HDAU] alcid profile found but codec has no runtime Audio Output DACs: codec=0x%08x alcid=%u profile=%a",
           CodecId, (UINT32)Profile->LayoutId, Profile->Name);
    return FALSE;
  }

  /*
     The AppleALC table is now used as a layout hint, not as blind verbs.
     Each AppleALC layout contributes its preferred pin order and DAC hints,
     but this DXE validates everything against the live codec graph at boot:
       - pin must be a real output-capable pin on this codec;
       - analog-only unless AllowDigitalPins is explicitly enabled;
       - connected firmware defaults are tried first;
       - DAC must be a real Audio Output widget on this codec;
       - route must be discoverable through the codec connection list.
     This keeps all AppleALC layouts dynamic without hardcoding H81/Realtek
     behaviour and without programming input/SPDIF/vendor paths as PCM output.
  */
  for (Pass = 0; Pass < 2; Pass++) {
    BOOLEAN AllowNoConnect = (Pass != 0);
    UINTN PairIndex;

    for (PairIndex = 0; PairIndex < HDAU_ALCID_PROFILE_PAIR_COUNT; PairIndex++) {
      UINT8 Pin = Profile->Pins[PairIndex];
      UINT32 PinCap;
      UINT32 Config;
      UINT8 OrderedDacs[HDAU_MAX_WIDGETS];
      UINTN OrderedCount;
      UINTN DacIndex;

      if (Pin == 0) continue;
      if (mHdaCfg.ForcePin != HDAU_FORCE_NID_NONE &&
          Pin != (UINT8)mHdaCfg.ForcePin) continue;

      PinCap = HdaGetParam(PciIo, Codec, Pin, HDA_PARAM_PIN_CAP);
      if (!HdaIsOutputPin(PciIo, Codec, Pin)) {
        if (mHdaCfg.Debug) {
          HdaLog("[HDAU] alcid runtime skip: alcid=%u profile=%a pin=0x%x no-output pincap=0x%x",
                 (UINT32)Profile->LayoutId, Profile->Name, Pin, PinCap);
        }
        continue;
      }

      Config = 0;
      if (EFI_ERROR(HdaImmediateCommand(PciIo, Codec, Pin,
                                        HDA_VERB12(HDA_VERB_GET_CONFIG_DEFAULT, 0), &Config))) {
        if (mHdaCfg.Debug) {
          HdaLog("[HDAU] alcid runtime skip: alcid=%u profile=%a pin=0x%x ConfigDefault read failed",
                 (UINT32)Profile->LayoutId, Profile->Name, Pin);
        }
        continue;
      }
      if (!AllowNoConnect && !HdaPinDefaultLooksConnected(Config)) {
        if (mHdaCfg.Debug) {
          HdaLog("[HDAU] alcid runtime defer: alcid=%u profile=%a pin=0x%x default disabled/no-connect config=0x%x",
                 (UINT32)Profile->LayoutId, Profile->Name, Pin, Config);
        }
        continue;
      }
      if (HdaPinDefaultIsDigital(Config) && !mHdaCfg.AllowDigitalPins) {
        if (mHdaCfg.Debug) {
          HdaLog("[HDAU] alcid runtime skip: alcid=%u profile=%a pin=0x%x digital config=0x%x",
                 (UINT32)Profile->LayoutId, Profile->Name, Pin, Config);
        }
        continue;
      }

      ZeroMem(OrderedDacs, sizeof(OrderedDacs));
      OrderedCount = HdaBuildAlcIdDacOrder(Profile, PairIndex, RuntimeDacs, RuntimeDacCount, OrderedDacs, ARRAY_SIZE(OrderedDacs));

      for (DacIndex = 0; DacIndex < OrderedCount; DacIndex++) {
        UINT8 Dac = OrderedDacs[DacIndex];
        UINT8 Path[HDAU_MAX_PATH_DEPTH];
        UINTN PathLen;
        UINT32 DacCaps;

        if (Dac == 0) continue;
        if (!HdaIsAudioOutputDac(PciIo, Codec, Afg, Dac)) {
          if (mHdaCfg.Debug) {
            DacCaps = HdaGetParam(PciIo, Codec, Dac, HDA_PARAM_AUDIO_WIDGET_CAP);
            HdaLog("[HDAU] alcid runtime skip: alcid=%u profile=%a dac=0x%x is not Audio Output wcaps=0x%x",
                   (UINT32)Profile->LayoutId, Profile->Name, Dac, DacCaps);
          }
          continue;
        }

        PathLen = 0;
        if (HdaPathSearch(PciIo, Codec, Pin, Dac, Path, &PathLen) &&
            HdaPathInputAmpsProgrammable(PciIo, Codec, Afg, Path, PathLen)) {
          if (HdaPathContainsPinIntermediate(PciIo, Codec, Path, PathLen)) {
            if (mHdaCfg.Debug) {
              HdaLog("[HDAU] alcid runtime skip: alcid=%u profile=%a pin=0x%x dac=0x%x has pin-intermediate pathLen=%u",
                     (UINT32)Profile->LayoutId, Profile->Name, Pin, Dac, (UINT32)PathLen);
            }
            continue;
          }

          Context->Afg = Afg;
          Context->Pin = Pin;
          Context->Dac = Dac;
          Context->PathLen = PathLen;
          ZeroMem(Context->Path, sizeof(Context->Path));
          CopyMem(Context->Path, Path, PathLen * sizeof(Path[0]));
          if (!HdaClassifySelectedRoute(Context)) continue;
          mHdaActiveAlcIdProfile = Profile;
          HdaLog("[HDAU] alcid runtime path selected: codec=0x%08x alcid=%u source=%a profile=%a afg=0x%x pin=0x%x dac=0x%x pathLen=%u pass=%a pair=%u hintDac=0x%x",
                 CodecId, (UINT32)Profile->LayoutId,
                 mHdaBootArgsAlcIdSource, Profile->Name, Afg, Pin, Dac, (UINT32)PathLen,
                 AllowNoConnect ? "allow-no-connect" : "connected-only", (UINT32)PairIndex, (UINT32)Profile->Dacs[PairIndex]);
          return TRUE;
        }

        if (mHdaCfg.Debug) {
          HdaLog("[HDAU] alcid runtime no-route: alcid=%u profile=%a pin=0x%x dac=0x%x config=0x%x",
                 (UINT32)Profile->LayoutId, Profile->Name, Pin, Dac, Config);
        }
      }
    }
  }

  HdaLog("[HDAU] alcid profile found but no usable runtime analog PCM route: codec=0x%08x alcid=%u profile=%a",
         CodecId, (UINT32)Profile->LayoutId, Profile->Name);
  if (mHdaActiveAlcIdProfile == Profile) {
    mHdaActiveAlcIdProfile = NULL;
  }
  return FALSE;
}

STATIC BOOLEAN HdaTryQuickPath(HDAU_CONTEXT *Context) {
  EFI_PCI_IO_PROTOCOL *PciIo;
  UINT8 Codec;
  UINT32 CodecId;
  UINT8 FgStart;
  UINT8 FgCount;
  UINTN FgIndex;
  CONST HDAU_CODEC_POLICY *Policy;
  CONST HDAU_ALCID_PROFILE *LayoutProfile;
  EFI_STATUS Status;

  if (!mHdaCfg.QuickPath || Context == NULL || Context->PciIo == NULL) return FALSE;

  PciIo = Context->PciIo;
  Codec = Context->Codec;
  CodecId = mHdaActiveCodecId;

  if (CodecId == 0 || CodecId == MAX_UINT32) {
    Status = HdaImmediateCommand(PciIo, Codec, 0,
                                 HDA_VERB12(HDA_VERB_GET_PARAMETER, HDA_PARAM_VENDOR_ID),
                                 &CodecId);
    if (EFI_ERROR(Status) || CodecId == 0 || CodecId == MAX_UINT32) return FALSE;
  }
  mHdaActiveCodecId = CodecId;
  Policy = HdaFindCodecPolicy(CodecId);
  mHdaActivePolicy = Policy;
  mHdaActiveAlcIdProfile = NULL;
  LayoutProfile = HdaResolveAlcIdProfile(CodecId);

  if (mHdaBootArgsAlcIdValid && LayoutProfile == NULL) {
    HdaLog("[HDAU] alcid: alcid=%u source=%a has no exact pre-boot layout profile for codec=0x%08x; falling back to automatic path selection",
           (UINT32)mHdaBootArgsAlcId, mHdaBootArgsAlcIdSource, CodecId);
  }

  if (!HdaGetSubnodes(PciIo, Codec, 0, &FgStart, &FgCount)) return FALSE;
  for (FgIndex = 0; FgIndex < FgCount; FgIndex++) {
    UINT8 Afg = (UINT8)(FgStart + FgIndex);
    UINT32 FunctionType = HdaGetParam(PciIo, Codec, Afg, HDA_PARAM_FUNCTION_TYPE) & 0xFF;
    UINT8 WStart;
    UINT8 WCount;
    UINTN WIndex;
    UINT8 PinCandidates[HDAU_MAX_WIDGETS];
    UINT8 DacCandidates[HDAU_MAX_WIDGETS];
    UINTN PinCount;
    UINTN DacCount;
    UINTN PinIndex;
    UINTN DacIndex;

    if (FunctionType != 0x01) continue;
    if (LayoutProfile != NULL && HdaTryAlcIdProfilePath(Context, CodecId, Afg, LayoutProfile)) {
      return TRUE;
    }

    WStart = 0;
    WCount = 0;
    PinCount = 0;
    DacCount = 0;
    ZeroMem(PinCandidates, sizeof(PinCandidates));
    ZeroMem(DacCandidates, sizeof(DacCandidates));

    if (mHdaCfg.ForcePin != HDAU_FORCE_NID_NONE) {
      PinCandidates[PinCount++] = (UINT8)mHdaCfg.ForcePin;
    } else {
      /* QuickPath remains hardware-dynamic: exact AppleALC layout paths were
         tried first, and this fallback enumerates the live AFG widget graph. */
      if (!HdaGetSubnodes(PciIo, Codec, Afg, &WStart, &WCount)) continue;
      for (WIndex = 0; WIndex < WCount && PinCount < ARRAY_SIZE(PinCandidates); WIndex++) {
        UINT8 Nid = (UINT8)(WStart + WIndex);
        UINT32 Caps = HdaGetParam(PciIo, Codec, Nid, HDA_PARAM_AUDIO_WIDGET_CAP);
        UINT32 PinCap;
        UINT32 Config;

        if (HdaWidgetType(Caps) != HDA_WIDGET_TYPE_PIN_COMPLEX) continue;
        PinCap = HdaGetParam(PciIo, Codec, Nid, HDA_PARAM_PIN_CAP);
        if ((PinCap & HDA_PIN_CAP_OUTPUT) == 0) continue;
        Config = 0;
        if (EFI_ERROR(HdaImmediateCommand(PciIo, Codec, Nid,
                                          HDA_VERB12(HDA_VERB_GET_CONFIG_DEFAULT, 0), &Config))) {
          continue;
        }
        if (!HdaPinDefaultLooksConnected(Config)) continue;
        if (HdaPinDefaultIsDigital(Config) && !mHdaCfg.AllowDigitalPins) continue;
        PinCandidates[PinCount++] = Nid;
      }
      if (PinCount == 0) continue;
    }

    if (mHdaCfg.ForceDac != HDAU_FORCE_NID_NONE) {
      DacCandidates[DacCount++] = (UINT8)mHdaCfg.ForceDac;
    } else {
      DacCount = HdaCollectAudioOutputDacs(PciIo, Codec, Afg, DacCandidates, ARRAY_SIZE(DacCandidates));
      if (DacCount == 0) continue;
    }

    for (PinIndex = 0; PinIndex < PinCount; PinIndex++) {
      UINT8 Pin = PinCandidates[PinIndex];
      UINT32 Config;
      BOOLEAN ForcedPin;

      if (Pin == 0) continue;
      ForcedPin = (mHdaCfg.ForcePin != HDAU_FORCE_NID_NONE && Pin == (UINT8)mHdaCfg.ForcePin);
      if (!HdaIsOutputPin(PciIo, Codec, Pin)) continue;

      Config = 0;
      if (!ForcedPin) {
        Status = HdaImmediateCommand(PciIo, Codec, Pin,
                                     HDA_VERB12(HDA_VERB_GET_CONFIG_DEFAULT, 0), &Config);
        if (EFI_ERROR(Status)) continue;
        if (!HdaPinDefaultLooksConnected(Config)) continue;
        if (HdaPinDefaultIsDigital(Config) && !mHdaCfg.AllowDigitalPins) continue;
      }

      for (DacIndex = 0; DacIndex < DacCount; DacIndex++) {
        UINT8 Dac = DacCandidates[DacIndex];
        UINT8 Path[HDAU_MAX_PATH_DEPTH];
        UINTN PathLen;

        if (Dac == 0 || !HdaIsAudioOutputDac(PciIo, Codec, Afg, Dac)) continue;
        PathLen = 0;
        if (HdaPathSearch(PciIo, Codec, Pin, Dac, Path, &PathLen) &&
            HdaPathInputAmpsProgrammable(PciIo, Codec, Afg, Path, PathLen)) {
          Context->Afg = Afg;
          Context->Pin = Pin;
          Context->Dac = Dac;
          Context->PathLen = PathLen;
          ZeroMem(Context->Path, sizeof(Context->Path));
          CopyMem(Context->Path, Path, PathLen * sizeof(Path[0]));
          if (!HdaClassifySelectedRoute(Context)) continue;
          HdaLog("[HDAU] quick path selected: codec=0x%08x afg=0x%x dac=0x%x pin=0x%x pathLen=%u policy=%a",
                 CodecId, Afg, Dac, Pin, (UINT32)PathLen,
                 Policy == NULL ? "generic-hda" : Policy->Name);
          return TRUE;
        }
      }
    }
  }

  return FALSE;
}


STATIC BOOLEAN HdaFindPlayablePath(HDAU_CONTEXT *Context) {
  EFI_PCI_IO_PROTOCOL *PciIo;
  UINT8 Codec;
  UINT8 FgStart;
  UINT8 FgCount;
  UINTN FgIndex;
  INTN BestScore;
  BOOLEAN HaveBest;
  BOOLEAN FoundAfg;
  UINT8 BestAfg;
  UINT8 BestPin;
  UINT8 BestDac;
  UINT8 BestPath[HDAU_MAX_PATH_DEPTH];
  UINTN BestPathLen;
  UINTN TotalDacCount;
  UINTN TotalPinCount;
  UINT32 CodecId;
  CONST HDAU_ALCID_PROFILE *LayoutProfile;
  EFI_STATUS Status;

  if (Context == NULL || Context->PciIo == NULL) return FALSE;
  PciIo = Context->PciIo;
  Codec = Context->Codec;
  BestScore = 0;
  HaveBest = FALSE;
  FoundAfg = FALSE;
  BestAfg = 0;
  BestPin = 0;
  BestDac = 0;
  BestPathLen = 0;
  TotalDacCount = 0;
  TotalPinCount = 0;
  ZeroMem(BestPath, sizeof(BestPath));

  CodecId = mHdaActiveCodecId;
  if (CodecId == 0 || CodecId == MAX_UINT32) {
    Status = HdaImmediateCommand(PciIo, Codec, 0,
                                 HDA_VERB12(HDA_VERB_GET_PARAMETER, HDA_PARAM_VENDOR_ID),
                                 &CodecId);
    if (EFI_ERROR(Status) || CodecId == 0 || CodecId == MAX_UINT32) return FALSE;
  }
  mHdaActiveCodecId = CodecId;
  mHdaActivePolicy = HdaFindCodecPolicy(CodecId);
  mHdaActiveAlcIdProfile = NULL;
  if (mHdaActivePolicy != NULL) {
    HdaLog("[HDAU] runtime codec family: codec=0x%08x name=%a flags=0x%x", CodecId, mHdaActivePolicy->Name, mHdaActivePolicy->Flags);
  } else {
    HdaLog("[HDAU] runtime codec family: codec=0x%08x name=generic-hda flags=0x0", CodecId);
  }
  if (mHdaBootArgsAlcIdValid) {
    HdaLog("[HDAU] AppleALC layout hint: alcid=%u source=%a", (UINT32)mHdaBootArgsAlcId, mHdaBootArgsAlcIdSource);
  }

  if (!HdaGetSubnodes(PciIo, Codec, 0, &FgStart, &FgCount)) return FALSE;
  LayoutProfile = HdaResolveAlcIdProfile(CodecId);

  for (FgIndex = 0; FgIndex < FgCount; FgIndex++) {
    UINT8 Afg = (UINT8)(FgStart + FgIndex);
    UINT32 FunctionType = HdaGetParam(PciIo, Codec, Afg, HDA_PARAM_FUNCTION_TYPE) & 0xFF;
    UINT8 WStart;
    UINT8 WCount;
    UINT8 Dacs[HDAU_MAX_WIDGETS];
    UINT8 Pins[HDAU_MAX_WIDGETS];
    INTN PinScores[HDAU_MAX_WIDGETS];
    UINTN DacCount;
    UINTN PinCount;
    UINTN WIndex;
    UINTN PinIndex;
    UINTN DacIndex;

    if (FunctionType != 0x01) continue;
    FoundAfg = TRUE;
    HdaLog("[HDAU] codec %u AFG NID=0x%x", Codec, Afg);

    if (LayoutProfile != NULL && HdaTryAlcIdProfilePath(Context, CodecId, Afg, LayoutProfile)) {
      return TRUE;
    }
    if (!HdaGetSubnodes(PciIo, Codec, Afg, &WStart, &WCount)) continue;

    DacCount = 0;
    PinCount = 0;
    ZeroMem(Dacs, sizeof(Dacs));
    ZeroMem(Pins, sizeof(Pins));
    ZeroMem(PinScores, sizeof(PinScores));

    for (WIndex = 0; WIndex < WCount; WIndex++) {
      UINT8 Nid = (UINT8)(WStart + WIndex);
      UINT32 Caps = HdaGetParam(PciIo, Codec, Nid, HDA_PARAM_AUDIO_WIDGET_CAP);
      UINT8 Type = HdaWidgetType(Caps);

      if (Type == HDA_WIDGET_TYPE_AUDIO_OUT && DacCount < ARRAY_SIZE(Dacs)) {
        if (!HdaIsAudioOutputDac(PciIo, Codec, Afg, Nid)) continue;
        if (mHdaCfg.ForceDac != HDAU_FORCE_NID_NONE && Nid != (UINT8)mHdaCfg.ForceDac) continue;
        Dacs[DacCount++] = Nid;
      } else if (Type == HDA_WIDGET_TYPE_PIN_COMPLEX && PinCount < ARRAY_SIZE(Pins)) {
        INTN CachedPinScore;
        if (mHdaCfg.ForcePin != HDAU_FORCE_NID_NONE && Nid != (UINT8)mHdaCfg.ForcePin) continue;
        CachedPinScore = HdaPinPriority(PciIo, Codec, Nid);
        if (CachedPinScore >= 0) {
          Pins[PinCount] = Nid;
          PinScores[PinCount] = CachedPinScore;
          PinCount++;
        }
      }
    }

    TotalDacCount += DacCount;
    TotalPinCount += PinCount;
    for (PinIndex = 0; PinIndex < PinCount; PinIndex++) {
      INTN PinScore = PinScores[PinIndex];
      for (DacIndex = 0; DacIndex < DacCount; DacIndex++) {
        UINT8 Path[HDAU_MAX_PATH_DEPTH];
        UINTN PathLen = 0;
        if (HdaPathSearch(PciIo, Codec, Pins[PinIndex], Dacs[DacIndex], Path, &PathLen) &&
            HdaPathInputAmpsProgrammable(PciIo, Codec, Afg, Path, PathLen)) {
          INTN Score = PinScore - (INTN)PathLen;
          if (!HaveBest || Score > BestScore) {
            HaveBest = TRUE;
            BestScore = Score;
            BestAfg = Afg;
            BestPin = Pins[PinIndex];
            BestDac = Dacs[DacIndex];
            BestPathLen = PathLen;
            ZeroMem(BestPath, sizeof(BestPath));
            CopyMem(BestPath, Path, PathLen * sizeof(Path[0]));
          }
        }
      }
    }
  }

  if (!FoundAfg) {
    HdaLog("[HDAU] no audio function group found for codec %u", Codec);
    return FALSE;
  }
  HdaLog("[HDAU] path candidates: dacs=%u usablePins=%u haveBest=%u bestScore=%d",
         (UINT32)TotalDacCount, (UINT32)TotalPinCount, HaveBest, (INT32)BestScore);
  if (!HaveBest || BestPathLen == 0) return FALSE;

  HdaLog("[HDAU] selected path: codec=%u afg=0x%x dac=0x%x pin=0x%x pathLen=%u score=%d",
         Codec, BestAfg, BestDac, BestPin, (UINT32)BestPathLen, (INT32)BestScore);
  Context->Afg = BestAfg;
  Context->Pin = BestPin;
  Context->Dac = BestDac;
  Context->PathLen = BestPathLen;
  ZeroMem(Context->Path, sizeof(Context->Path));
  CopyMem(Context->Path, BestPath, BestPathLen * sizeof(BestPath[0]));
  return HdaClassifySelectedRoute(Context);
}


STATIC BOOLEAN HdaRouteCandidateEquals(
  CONST HDAU_ROUTE_CANDIDATE *Candidate,
  UINT8 Afg,
  UINT8 Pin,
  UINT8 Dac,
  CONST UINT8 *Path,
  UINTN PathLen
) {
  if (Candidate == NULL || Path == NULL) return FALSE;
  if (Candidate->Afg != Afg || Candidate->Pin != Pin || Candidate->Dac != Dac ||
      Candidate->PathLen != PathLen) return FALSE;
  return CompareMem(Candidate->Path, Path, PathLen * sizeof(Path[0])) == 0;
}

STATIC VOID HdaAddRouteCandidate(
  HDAU_CONTEXT *Context,
  UINT8 Afg,
  UINT8 Pin,
  UINT8 Dac,
  CONST UINT8 *Path,
  UINTN PathLen,
  INTN Score,
  BOOLEAN AppleAlcExact
) {
  UINTN Index;
  HDAU_ROUTE_CANDIDATE *Candidate;
  BOOLEAN PrimaryRoute;
  BOOLEAN Hdmi;
  BOOLEAN DisplayPort;
  BOOLEAN Digital;
  EFI_STATUS Status;

  if (Context == NULL || Path == NULL || PathLen < 2 || PathLen > HDAU_MAX_PATH_DEPTH) return;
  for (Index = 0; Index < Context->RouteCandidateCount; Index++) {
    if (HdaRouteCandidateEquals(&Context->RouteCandidates[Index], Afg, Pin, Dac, Path, PathLen)) return;
  }
  if (Context->RouteCandidateCount >= ARRAY_SIZE(Context->RouteCandidates)) return;

  PrimaryRoute = (Afg == Context->Afg && Pin == Context->Pin && Dac == Context->Dac &&
                  PathLen == Context->PathLen &&
                  CompareMem(Path, Context->Path, PathLen * sizeof(Path[0])) == 0);
  if (PrimaryRoute) {
    Hdmi = Context->HdmiRoute;
    DisplayPort = Context->DisplayPortRoute;
    Digital = Context->DigitalRoute;
  } else {
    UINT32 Config = 0;
    UINT32 PinCap = 0;
    Status = HdaGetParamChecked(Context->PciIo, Context->Codec, Pin,
                                HDA_PARAM_PIN_CAP, &PinCap);
    if (EFI_ERROR(Status)) return;
    Status = HdaImmediateCommand(Context->PciIo, Context->Codec, Pin,
                                 HDA_VERB12(HDA_VERB_GET_CONFIG_DEFAULT, 0), &Config);
    if (EFI_ERROR(Status)) {
      return;
    }
    Hdmi = ((PinCap & HDA_PIN_CAP_HDMI) != 0);
    DisplayPort = ((PinCap & HDA_PIN_CAP_DISPLAYPORT) != 0);
    Digital = Hdmi || DisplayPort || HdaPinDefaultIsDigital(Config);
  }

  Candidate = &Context->RouteCandidates[Context->RouteCandidateCount++];
  ZeroMem(Candidate, sizeof(*Candidate));
  Candidate->Afg = Afg;
  Candidate->Pin = Pin;
  Candidate->Dac = Dac;
  Candidate->PathLen = PathLen;
  Candidate->Score = Score;
  Candidate->AppleAlcExact = AppleAlcExact;
  Candidate->Hdmi = Hdmi;
  Candidate->DisplayPort = DisplayPort;
  Candidate->Digital = Digital;
  CopyMem(Candidate->Path, Path, PathLen * sizeof(Path[0]));
}

STATIC VOID HdaSortRouteCandidates(HDAU_CONTEXT *Context) {
  UINTN Index;

  if (Context == NULL || Context->RouteCandidateCount < 3) return;
  /* Candidate zero is the already-selected, user-validated primary route.
     Sort only fallbacks so an expansion cannot change the desktop baseline. */
  for (Index = 2; Index < Context->RouteCandidateCount; Index++) {
    HDAU_ROUTE_CANDIDATE Key = Context->RouteCandidates[Index];
    UINTN Insert = Index;
    while (Insert > 1 && Context->RouteCandidates[Insert - 1U].Score < Key.Score) {
      Context->RouteCandidates[Insert] = Context->RouteCandidates[Insert - 1U];
      Insert--;
    }
    Context->RouteCandidates[Insert] = Key;
  }
}

STATIC VOID HdaCollectRouteCandidates(HDAU_CONTEXT *Context) {
  UINT8 WStart;
  UINT8 WCount;
  UINT8 Dacs[HDAU_MAX_WIDGETS];
  UINTN DacCount;
  UINTN WIndex;
  UINTN DacIndex;

  if (Context == NULL || Context->PciIo == NULL || Context->PathLen < 2) return;
  Context->RouteCandidateCount = 0;
  Context->RouteCandidateIndex = 0;
  ZeroMem(Context->RouteCandidates, sizeof(Context->RouteCandidates));
  HdaAddRouteCandidate(Context, Context->Afg, Context->Pin, Context->Dac,
                       Context->Path, Context->PathLen, 0x7FFFFFFF,
                       mHdaActiveAlcIdProfile != NULL);
  if (!mHdaCfg.RouteFallback) return;
  if (!HdaGetSubnodes(Context->PciIo, Context->Codec, Context->Afg, &WStart, &WCount)) return;
  ZeroMem(Dacs, sizeof(Dacs));
  DacCount = HdaCollectAudioOutputDacs(Context->PciIo, Context->Codec, Context->Afg,
                                       Dacs, ARRAY_SIZE(Dacs));

  for (WIndex = 0; WIndex < WCount && Context->RouteCandidateCount < ARRAY_SIZE(Context->RouteCandidates); WIndex++) {
    UINT8 Pin = (UINT8)(WStart + WIndex);
    INTN PinScore;

    if (mHdaCfg.ForcePin != HDAU_FORCE_NID_NONE &&
        Pin != (UINT8)mHdaCfg.ForcePin) continue;
    if (HdaWidgetType(HdaGetParam(Context->PciIo, Context->Codec, Pin,
                                  HDA_PARAM_AUDIO_WIDGET_CAP)) != HDA_WIDGET_TYPE_PIN_COMPLEX) continue;
    PinScore = HdaPinPriority(Context->PciIo, Context->Codec, Pin);
    if (PinScore < 0) continue;
    for (DacIndex = 0; DacIndex < DacCount &&
                       Context->RouteCandidateCount < ARRAY_SIZE(Context->RouteCandidates); DacIndex++) {
      UINT8 Path[HDAU_MAX_PATH_DEPTH];
      UINTN PathLen = 0;
      UINT8 Dac = Dacs[DacIndex];
      if (!HdaPathSearch(Context->PciIo, Context->Codec, Pin, Dac, Path, &PathLen)) continue;
      if (HdaPathContainsPinIntermediate(Context->PciIo, Context->Codec, Path, PathLen)) continue;
      if (!HdaPathInputAmpsProgrammable(Context->PciIo, Context->Codec, Context->Afg, Path, PathLen)) continue;
      HdaAddRouteCandidate(Context, Context->Afg, Pin, Dac, Path, PathLen,
                           PinScore - (INTN)PathLen, FALSE);
    }
  }
  HdaSortRouteCandidates(Context);
  HdaLog("[HDAU] validated route candidates=%u primaryPin=0x%x primaryDac=0x%x",
         (UINT32)Context->RouteCandidateCount, Context->Pin, Context->Dac);
}

STATIC BOOLEAN HdaLoadRouteCandidate(HDAU_CONTEXT *Context, UINTN Index) {
  CONST HDAU_ROUTE_CANDIDATE *Candidate;

  if (Context == NULL || Index >= Context->RouteCandidateCount) return FALSE;
  Candidate = &Context->RouteCandidates[Index];
  Context->Afg = Candidate->Afg;
  Context->Pin = Candidate->Pin;
  Context->Dac = Candidate->Dac;
  Context->PathLen = Candidate->PathLen;
  Context->DigitalRoute = Candidate->Digital;
  Context->HdmiRoute = Candidate->Hdmi;
  Context->DisplayPortRoute = Candidate->DisplayPort;
  ZeroMem(Context->Path, sizeof(Context->Path));
  CopyMem(Context->Path, Candidate->Path, Candidate->PathLen * sizeof(Candidate->Path[0]));
  Context->RouteCandidateIndex = Index;
  if (Index > 0 && !Candidate->AppleAlcExact) mHdaActiveAlcIdProfile = NULL;
  Context->HardwareVolumeApplied = FALSE;
  Context->HardwareVolumeInputAmp = FALSE;
  Context->HardwareVolumeNid = 0;
  Context->HardwareVolumeInputIndex = 0;
  HdaLog("[HDAU] route candidate %u/%u loaded: pin=0x%x dac=0x%x pathLen=%u digital=%u hdmi=%u dp=%u score=%d exact=%u",
         (UINT32)(Index + 1U), (UINT32)Context->RouteCandidateCount,
         Context->Pin, Context->Dac, (UINT32)Context->PathLen,
         Context->DigitalRoute ? 1 : 0, Context->HdmiRoute ? 1 : 0,
         Context->DisplayPortRoute ? 1 : 0,
         (INT32)Candidate->Score, Candidate->AppleAlcExact ? 1 : 0);
  return TRUE;
}


STATIC EFI_STATUS HdaGetEffectiveAmpCapsChecked(
  EFI_PCI_IO_PROTOCOL *PciIo,
  UINT8 Codec,
  UINT8 Afg,
  UINT8 Nid,
  UINT32 AwCaps,
  UINT8 Param,
  UINT32 *AmpCaps
) {
  UINT8 ParamNid;

  if (PciIo == NULL || AmpCaps == NULL || Nid == 0) return EFI_INVALID_PARAMETER;
  ParamNid = ((AwCaps & HDA_AWCAP_AMP_OVERRIDE) != 0 || Afg == 0 || Nid == Afg) ? Nid : Afg;
  return HdaGetParamChecked(PciIo, Codec, ParamNid, Param, AmpCaps);
}

STATIC BOOLEAN HdaPathInputAmpsProgrammable(
  EFI_PCI_IO_PROTOCOL *PciIo,
  UINT8 Codec,
  UINT8 Afg,
  CONST UINT8 *Path,
  UINTN PathLen
) {
  UINTN Index;
  EFI_STATUS Status;

  (VOID)Afg;
  if (PciIo == NULL || Path == NULL || PathLen == 0) return FALSE;
  for (Index = 0; Index + 1U < PathLen; Index++) {
    UINT8 Current = Path[Index];
    UINT8 Next = Path[Index + 1U];
    UINT8 Connections[HDAU_MAX_CONNECTIONS];
    UINTN Count;
    UINTN ConnIndex;
    UINT32 AwCaps;
    BOOLEAN Found;

    Count = HdaGetConnections(PciIo, Codec, Current, Connections, ARRAY_SIZE(Connections));
    if (Count == 0) return FALSE;
    Found = FALSE;
    for (ConnIndex = 0; ConnIndex < Count; ConnIndex++) {
      if (Connections[ConnIndex] != Next) continue;
      Found = TRUE;
      Status = HdaGetParamChecked(PciIo, Codec, Current,
                                  HDA_PARAM_AUDIO_WIDGET_CAP, &AwCaps);
      if (EFI_ERROR(Status)) return FALSE;
      if ((AwCaps & HDA_AWCAP_IN_AMP_PRESENT) != 0 && ConnIndex > HDA_AMP_INDEX_MAX) {
        if (mHdaCfg.Debug) {
          HdaLog("[HDAU] route candidate rejected: nid=0x%x input-index=%u cannot be encoded by SET_AMP_GAIN_MUTE",
                 Current, (UINT32)ConnIndex);
        }
        return FALSE;
      }
      break;
    }
    if (!Found) return FALSE;
  }
  return TRUE;
}

STATIC EFI_STATUS HdaProgramSelectedInputAmp(
  EFI_PCI_IO_PROTOCOL *PciIo,
  UINT8 Codec,
  UINT8 Afg,
  UINT8 Nid,
  UINTN ConnIndex,
  CONST CHAR8 *Reason
) {
  UINT32 AwCaps;
  UINT32 InCaps;
  UINT16 Payload;
  EFI_STATUS Status;

  if (PciIo == NULL || Nid == 0) return EFI_INVALID_PARAMETER;
  Status = HdaGetParamChecked(PciIo, Codec, Nid, HDA_PARAM_AUDIO_WIDGET_CAP, &AwCaps);
  if (EFI_ERROR(Status)) return Status;
  if ((AwCaps & HDA_AWCAP_IN_AMP_PRESENT) == 0) return EFI_SUCCESS;
  if (ConnIndex > HDA_AMP_INDEX_MAX) {
    HdaLog("[HDAU] input amp route rejected: nid=0x%x index=%u exceeds HDA amp index field reason=%a",
           Nid, (UINT32)ConnIndex, Reason == NULL ? "route" : Reason);
    return EFI_UNSUPPORTED;
  }
  Status = HdaGetEffectiveAmpCapsChecked(PciIo, Codec, Afg, Nid, AwCaps,
                                         HDA_PARAM_AMP_IN_CAP, &InCaps);
  if (EFI_ERROR(Status)) return Status;
  Payload = HdaAmpInUnmuteUnityFromCaps(InCaps, ConnIndex);
  return HdaSetVerb(PciIo, Codec, Nid, HDA_VERB4(HDA_VERB_SET_AMP_GAIN_MUTE, Payload));
}

STATIC EFI_STATUS HdaProgramConnectionPath(
  EFI_PCI_IO_PROTOCOL *PciIo,
  UINT8 Codec,
  UINT8 Afg,
  CONST UINT8 *Path,
  UINTN PathLen,
  CONST CHAR8 *Reason
) {
  UINTN Index;

  if (PciIo == NULL || Path == NULL || PathLen == 0) return EFI_INVALID_PARAMETER;

  for (Index = 0; Index + 1 < PathLen; Index++) {
    UINT8 Current = Path[Index];
    UINT8 Next = Path[Index + 1];
    UINT8 Conn[HDAU_MAX_CONNECTIONS];
    UINTN Count;
    UINTN ConnIndex;
    UINT32 AwCaps;
    UINT8 Type;
    EFI_STATUS Status;
    BOOLEAN Found;

    Count = HdaGetConnections(PciIo, Codec, Current, Conn, HDAU_MAX_CONNECTIONS);
    Found = FALSE;
    for (ConnIndex = 0; ConnIndex < Count; ConnIndex++) {
      if (Conn[ConnIndex] != Next) continue;
      Found = TRUE;
      Status = HdaGetParamChecked(PciIo, Codec, Current, HDA_PARAM_AUDIO_WIDGET_CAP, &AwCaps);
      if (EFI_ERROR(Status)) return Status;
      Type = HdaWidgetType(AwCaps);

      if (mHdaCfg.Debug) {
        HdaLog("[HDAU] route: nid=0x%x index=%u -> nid=0x%x widgetType=0x%x reason=%a",
               Current, (UINT32)ConnIndex, Next, Type, Reason == NULL ? "path" : Reason);
      }

      /* Mixers combine enabled inputs; they do not select one connection.
         Select only on selector/pin widgets, then unmute the indexed mixer or
         selector input amp when the widget advertises one. */
      if (Count > 1 &&
          (Type == HDA_WIDGET_TYPE_AUDIO_SELECTOR || Type == HDA_WIDGET_TYPE_PIN_COMPLEX)) {
        Status = HdaSetVerb(PciIo, Codec, Current,
                            HDA_VERB12(HDA_VERB_SET_CONNECTION_SELECT, (UINT8)ConnIndex));
        if (EFI_ERROR(Status)) return Status;
      }
      Status = HdaProgramSelectedInputAmp(PciIo, Codec, Afg, Current, ConnIndex, Reason);
      if (EFI_ERROR(Status)) return Status;
      break;
    }
    if (!Found) {
      HdaLog("[HDAU] route failed: nid=0x%x has no live connection to nid=0x%x reason=%a",
             Current, Next, Reason == NULL ? "path" : Reason);
      return EFI_NOT_FOUND;
    }
  }
  return EFI_SUCCESS;
}

STATIC EFI_STATUS HdaSelectConnectionForPath(HDAU_CONTEXT *Context) {
  if (Context == NULL || Context->PciIo == NULL) return EFI_INVALID_PARAMETER;
  return HdaProgramConnectionPath(Context->PciIo, Context->Codec, Context->Afg,
                                  Context->Path, Context->PathLen, "selected-path");
}

STATIC EFI_STATUS HdaProgramAmpSafeUnity(
  EFI_PCI_IO_PROTOCOL *PciIo,
  UINT8 Codec,
  UINT8 Afg,
  UINT8 Nid,
  CONST CHAR8 *Reason
) {
  UINT32 AwCaps;
  UINT32 InCaps;
  UINT32 OutCaps;
  UINT16 OutPayload;
  UINT16 InPayload;
  UINT8 Connections[HDAU_MAX_CONNECTIONS];
  UINTN ConnectionCount;
  EFI_STATUS Status;

  if (PciIo == NULL || Nid == 0) return EFI_INVALID_PARAMETER;
  Status = HdaGetParamChecked(PciIo, Codec, Nid, HDA_PARAM_AUDIO_WIDGET_CAP, &AwCaps);
  if (EFI_ERROR(Status)) return Status;
  InCaps = 0;
  OutCaps = 0;
  OutPayload = 0;
  InPayload = 0;

  if ((AwCaps & HDA_AWCAP_OUT_AMP_PRESENT) != 0) {
    Status = HdaGetEffectiveAmpCapsChecked(PciIo, Codec, Afg, Nid, AwCaps,
                                           HDA_PARAM_AMP_OUT_CAP, &OutCaps);
    if (EFI_ERROR(Status)) return Status;
    OutPayload = HdaAmpOutUnmuteUnityFromCaps(OutCaps);
    Status = HdaSetVerb(PciIo, Codec, Nid, HDA_VERB4(HDA_VERB_SET_AMP_GAIN_MUTE, OutPayload));
    if (EFI_ERROR(Status)) return Status;
  }

  /* A multi-input mixer/selector must be unmuted at its chosen connection
     index by HdaProgramConnectionPath. Touch index zero here only when the
     widget has exactly one input, avoiding changes to unrelated mixer inputs. */
  if ((AwCaps & HDA_AWCAP_IN_AMP_PRESENT) != 0) {
    ConnectionCount = HdaGetConnections(PciIo, Codec, Nid, Connections, ARRAY_SIZE(Connections));
    if (ConnectionCount == 1) {
      Status = HdaGetEffectiveAmpCapsChecked(PciIo, Codec, Afg, Nid, AwCaps,
                                             HDA_PARAM_AMP_IN_CAP, &InCaps);
      if (EFI_ERROR(Status)) return Status;
      InPayload = HdaAmpInUnmuteUnityFromCaps(InCaps, 0);
      Status = HdaSetVerb(PciIo, Codec, Nid, HDA_VERB4(HDA_VERB_SET_AMP_GAIN_MUTE, InPayload));
      if (EFI_ERROR(Status)) return Status;
    }
  }

  if (mHdaCfg.Debug) {
    HdaLog("[HDAU] amp: nid=0x%x wcaps=0x%x outPresent=%u outCaps=0x%x outGain=0x%x inPresent=%u inCaps=0x%x inGain=0x%x reason=%a",
           Nid, AwCaps, (AwCaps & HDA_AWCAP_OUT_AMP_PRESENT) != 0,
           OutCaps, (UINT32)(OutPayload & 0x7F),
           (AwCaps & HDA_AWCAP_IN_AMP_PRESENT) != 0,
           InCaps, (UINT32)(InPayload & 0x7F), Reason == NULL ? "path" : Reason);
  }
  return EFI_SUCCESS;
}



/* The PCM transport retains its independent hard 30% attenuation.  When
   the codec exposes enough negative amplifier range, request another 30%
   amplitude at exactly one programmable amp per analog route.  HDA amp
   controls are quantized in dB, not percentages; never approximate the
   requested level with a higher-than-30% gain.  This cannot calibrate a
   physical speaker or an external amplifier. */
STATIC CONST UINT16 mHdaPerceptualAttenuationQuarterDb[101] = {
  65535, 160, 136, 122, 112, 104, 98, 92, 88, 84, 80, 77, 74, 71, 68, 66,
  64, 62, 60, 58, 56, 54, 53, 51, 50, 48, 47, 45, 44, 43, 42, 41,
  40, 39, 37, 36, 35, 35, 34, 33, 32, 31, 30, 29, 29, 28, 27, 26,
  26, 25, 24, 23, 23, 22, 21, 21, 20, 20, 19, 18, 18, 17, 17, 16,
  16, 15, 14, 14, 13, 13, 12, 12, 11, 11, 10, 10, 10, 9, 9, 8,
  8, 7, 7, 6, 6, 6, 5, 5, 4, 4, 4, 3, 3, 3, 2, 2,
  1, 1, 1, 0, 0
};

STATIC BOOLEAN HdaAmpVolumeGainFromCaps(UINT32 AmpCaps, UINTN *GainOut, BOOLEAN *MuteOut) {
  UINTN UnityGain;
  UINTN RatioPercent;
  UINTN StepQuarterDb;
  UINTN AttenuationQuarterDb;
  UINTN StepsDown;

  if (GainOut == NULL || MuteOut == NULL) return FALSE;
  UnityGain = HdaAmpUnityGainFromCaps(AmpCaps);
  if (mHdaCfg.VolumeQ8 == 0) {
    /* A gain index of zero is not guaranteed to be silent.  Use the hardware
       amp only when it advertises mute; otherwise let the caller fall back to
       a zeroed digital stream so Volume=0 is deterministic on every codec. */
    if ((AmpCaps & BIT31) == 0) return FALSE;
    *GainOut = 0;
    *MuteOut = TRUE;
    return TRUE;
  }
  if (mHdaCfg.VolumeQ8 > HDAU_PCM_TRANSPORT_GAIN_Q8 || UnityGain == 0) return FALSE;

  /* The second 30% applies to the codec amplifier independently of the 30%
     PCM scaling.  The table expresses 20*log10(100/30) in quarter-dB: 42.
     Round attenuation UP to whole hardware steps so it never exceeds 30%. */
  RatioPercent = HDAU_BOOT_CHIME_VOLUME_PERCENT;
  AttenuationQuarterDb = mHdaPerceptualAttenuationQuarterDb[RatioPercent];
  StepQuarterDb = ((AmpCaps >> 16) & 0x7FU) + 1U;
  StepsDown = (AttenuationQuarterDb + StepQuarterDb - 1U) / StepQuarterDb;
  /* A coarse amp step can round a non-zero requested attenuation to zero.
     Force one step in that case so a requested reduction is never silently
     converted back to unity gain. */
  if (AttenuationQuarterDb > 0 && StepsDown == 0) StepsDown = 1;
  /* A widget that cannot reach -10.5 dB must not claim to have a 30%
     hardware limit.  Look for a different amp on the same live route. */
  if (StepsDown > UnityGain) return FALSE;

  *GainOut = UnityGain - StepsDown;
  *MuteOut = FALSE;
  return TRUE;
}

STATIC EFI_STATUS HdaVerifyAmpGain(
  EFI_PCI_IO_PROTOCOL *PciIo,
  UINT8 Codec,
  BOOLEAN InputAmp,
  UINT8 Nid,
  UINT8 InputIndex,
  UINT8 Expected
) {
  UINT32 Left;
  UINT32 Right;
  UINT16 LeftPayload;
  UINT16 RightPayload;
  EFI_STATUS LeftStatus;
  EFI_STATUS RightStatus;

  if (PciIo == NULL) return EFI_INVALID_PARAMETER;
  if (InputAmp) {
    LeftPayload = (UINT16)(0x2000U | (InputIndex & 0x0FU));
    RightPayload = (UINT16)(InputIndex & 0x0FU);
  } else {
    LeftPayload = 0xA000U;
    RightPayload = 0x8000U;
  }
  Left = 0;
  Right = 0;
  LeftStatus = HdaImmediateCommand(PciIo, Codec, Nid,
                                   HDA_VERB4(HDA_VERB_GET_AMP_GAIN_MUTE, LeftPayload), &Left);
  /* Always attempt both channels. A transport error on one side must not
     hide a successful, contradictory gain read on the other side. */
  RightStatus = HdaImmediateCommand(PciIo, Codec, Nid,
                                    HDA_VERB4(HDA_VERB_GET_AMP_GAIN_MUTE, RightPayload), &Right);
  if ((LeftStatus == EFI_SUCCESS && (UINT8)Left != Expected) ||
      (RightStatus == EFI_SUCCESS && (UINT8)Right != Expected)) {
    HdaLog("[HDAU] volume readback mismatch: nid=0x%x amp=%a index=%u expected=0x%x leftStatus=%r left=0x%x rightStatus=%r right=0x%x",
           Nid, InputAmp ? "input" : "output", InputAmp ? InputIndex : 0,
           Expected, LeftStatus, (UINT8)Left, RightStatus, (UINT8)Right);
    /* Any confirmed channel mismatch is evidence that the limit is unsafe,
       even when the opposite channel cannot be read. */
    return EFI_COMPROMISED_DATA;
  }
  if (LeftStatus != EFI_SUCCESS) return LeftStatus;
  if (RightStatus != EFI_SUCCESS) return RightStatus;
  return EFI_SUCCESS;
}

STATIC EFI_STATUS HdaProgramStoredHardwareVolume(HDAU_CONTEXT *Context, CONST CHAR8 *Reason) {
  UINT32 AwCaps;
  UINT32 AmpCaps;
  UINTN Gain;
  BOOLEAN Mute;
  BOOLEAN FirstReadbackContradicted;
  UINT16 Payload;
  EFI_STATUS Status;

  if (Context == NULL || Context->PciIo == NULL || !Context->HardwareVolumeApplied) {
    return EFI_INVALID_PARAMETER;
  }

  Status = HdaGetParamChecked(Context->PciIo, Context->Codec, Context->HardwareVolumeNid,
                              HDA_PARAM_AUDIO_WIDGET_CAP, &AwCaps);
  if (EFI_ERROR(Status)) return Status;
  if (Context->HardwareVolumeInputAmp) {
    if ((AwCaps & HDA_AWCAP_IN_AMP_PRESENT) == 0 ||
        Context->HardwareVolumeInputIndex > HDA_AMP_INDEX_MAX) return EFI_UNSUPPORTED;
    Status = HdaGetEffectiveAmpCapsChecked(Context->PciIo, Context->Codec, Context->Afg,
                                           Context->HardwareVolumeNid, AwCaps,
                                           HDA_PARAM_AMP_IN_CAP, &AmpCaps);
    if (EFI_ERROR(Status)) return Status;
    if (!HdaAmpVolumeGainFromCaps(AmpCaps, &Gain, &Mute)) return EFI_UNSUPPORTED;
    Payload = (UINT16)(0x7000U |
                       (((UINT16)Context->HardwareVolumeInputIndex & 0x0FU) << 8) |
                       (Mute ? 0x0080U : 0U) | ((UINT16)Gain & 0x007FU));
  } else {
    if ((AwCaps & HDA_AWCAP_OUT_AMP_PRESENT) == 0) return EFI_UNSUPPORTED;
    Status = HdaGetEffectiveAmpCapsChecked(Context->PciIo, Context->Codec, Context->Afg,
                                           Context->HardwareVolumeNid, AwCaps,
                                           HDA_PARAM_AMP_OUT_CAP, &AmpCaps);
    if (EFI_ERROR(Status)) return Status;
    if (!HdaAmpVolumeGainFromCaps(AmpCaps, &Gain, &Mute)) return EFI_UNSUPPORTED;
    Payload = (UINT16)(0xB000U | (Mute ? 0x0080U : 0U) | ((UINT16)Gain & 0x007FU));
  }

  HdaLog("[HDAU] volume: pcmTargetPercent=%u codecTargetPercent=%u requestedQ8=%u transportQ8=%u nid=0x%x amp=%a index=%u gain=0x%x mute=%u reason=%a",
         (UINT32)HDAU_BOOT_CHIME_VOLUME_PERCENT, (UINT32)HDAU_BOOT_CHIME_VOLUME_PERCENT,
         (UINT32)mHdaCfg.VolumeQ8, (UINT32)HDAU_PCM_TRANSPORT_GAIN_Q8,
         Context->HardwareVolumeNid, Context->HardwareVolumeInputAmp ? "input" : "output",
         Context->HardwareVolumeInputAmp ? Context->HardwareVolumeInputIndex : 0U,
         (UINT32)Gain, Mute, Reason == NULL ? "volume" : Reason);
  {
    EFI_STATUS VerifyStatus;
    UINT8 Expected = (UINT8)((Mute ? 0x80U : 0U) | (Gain & 0x7FU));

    Status = HdaSetVerb(Context->PciIo, Context->Codec, Context->HardwareVolumeNid,
                        HDA_VERB4(HDA_VERB_SET_AMP_GAIN_MUTE, Payload));
    if (EFI_ERROR(Status)) return Status;

    VerifyStatus = HdaVerifyAmpGain(Context->PciIo, Context->Codec,
                                    Context->HardwareVolumeInputAmp, Context->HardwareVolumeNid,
                                    Context->HardwareVolumeInputIndex, Expected);
    if (VerifyStatus == EFI_SUCCESS) return EFI_SUCCESS;
    FirstReadbackContradicted = (VerifyStatus == EFI_COMPROMISED_DATA);

    /* Some codecs have unreliable preboot GET_AMP_GAIN_MUTE. Retry the SET,
       but distinguish a read transport error from two successful reads that
       explicitly contradict the requested safe attenuation. */
    HdaLog("[HDAU] volume readback advisory: nid=0x%x status=%r; retrying SET once",
           Context->HardwareVolumeNid, VerifyStatus);
    Status = HdaSetVerb(Context->PciIo, Context->Codec, Context->HardwareVolumeNid,
                        HDA_VERB4(HDA_VERB_SET_AMP_GAIN_MUTE, Payload));
    if (EFI_ERROR(Status)) return Status;
    VerifyStatus = HdaVerifyAmpGain(Context->PciIo, Context->Codec,
                                    Context->HardwareVolumeInputAmp, Context->HardwareVolumeNid,
                                    Context->HardwareVolumeInputIndex, Expected);
    if (VerifyStatus == EFI_COMPROMISED_DATA ||
        (FirstReadbackContradicted && VerifyStatus != EFI_SUCCESS)) {
      HdaLog("[HDAU] volume safety: unresolved gain mismatch at nid=0x%x; rejecting route",
             Context->HardwareVolumeNid);
      return EFI_COMPROMISED_DATA;
    }
    if (VerifyStatus != EFI_SUCCESS) {
      /* No trustworthy readback is available; preserve the legacy best-effort
         SET compatibility, but never label the hardware attenuation verified. */
      HdaLog("[HDAU] volume readback unavailable: nid=0x%x status=%r; successful SET unverified",
             Context->HardwareVolumeNid, VerifyStatus);
    }
    return EFI_SUCCESS;
  }
}

STATIC EFI_STATUS HdaTryHardwareOutputVolume(HDAU_CONTEXT *Context, UINT8 Nid, CONST CHAR8 *Reason) {
  UINT32 AwCaps;
  UINT32 AmpCaps;
  UINTN Gain;
  BOOLEAN Mute;
  EFI_STATUS Status;

  if (Context == NULL || Context->PciIo == NULL || Nid == 0) return EFI_INVALID_PARAMETER;
  Status = HdaGetParamChecked(Context->PciIo, Context->Codec, Nid,
                              HDA_PARAM_AUDIO_WIDGET_CAP, &AwCaps);
  if (EFI_ERROR(Status)) return Status;
  if ((AwCaps & HDA_AWCAP_OUT_AMP_PRESENT) == 0) return EFI_UNSUPPORTED;
  Status = HdaGetEffectiveAmpCapsChecked(Context->PciIo, Context->Codec, Context->Afg, Nid,
                                         AwCaps, HDA_PARAM_AMP_OUT_CAP, &AmpCaps);
  if (EFI_ERROR(Status)) return Status;
  if (!HdaAmpVolumeGainFromCaps(AmpCaps, &Gain, &Mute)) return EFI_UNSUPPORTED;

  Context->HardwareVolumeApplied = TRUE;
  Context->HardwareVolumeInputAmp = FALSE;
  Context->HardwareVolumeNid = Nid;
  Context->HardwareVolumeInputIndex = 0;
  return HdaProgramStoredHardwareVolume(Context, Reason);
}

STATIC EFI_STATUS HdaTryHardwareInputVolume(
  HDAU_CONTEXT *Context,
  UINT8 Nid,
  UINT8 NextNid,
  CONST CHAR8 *Reason
) {
  UINT32 AwCaps;
  UINT32 AmpCaps;
  UINT8 Connections[HDAU_MAX_CONNECTIONS];
  UINTN Count;
  UINTN Index;
  UINTN Gain;
  BOOLEAN Mute;
  EFI_STATUS Status;

  if (Context == NULL || Context->PciIo == NULL || Nid == 0 || NextNid == 0) return EFI_INVALID_PARAMETER;
  Status = HdaGetParamChecked(Context->PciIo, Context->Codec, Nid,
                              HDA_PARAM_AUDIO_WIDGET_CAP, &AwCaps);
  if (EFI_ERROR(Status)) return Status;
  if ((AwCaps & HDA_AWCAP_IN_AMP_PRESENT) == 0) return EFI_UNSUPPORTED;
  Status = HdaGetEffectiveAmpCapsChecked(Context->PciIo, Context->Codec, Context->Afg, Nid,
                                         AwCaps, HDA_PARAM_AMP_IN_CAP, &AmpCaps);
  if (EFI_ERROR(Status)) return Status;
  if (!HdaAmpVolumeGainFromCaps(AmpCaps, &Gain, &Mute)) return EFI_UNSUPPORTED;

  Count = HdaGetConnections(Context->PciIo, Context->Codec, Nid,
                            Connections, ARRAY_SIZE(Connections));
  for (Index = 0; Index < Count; Index++) {
    if (Connections[Index] != NextNid) continue;
    if (Index > HDA_AMP_INDEX_MAX) return EFI_UNSUPPORTED;
    Context->HardwareVolumeApplied = TRUE;
    Context->HardwareVolumeInputAmp = TRUE;
    Context->HardwareVolumeNid = Nid;
    Context->HardwareVolumeInputIndex = (UINT8)Index;
    return HdaProgramStoredHardwareVolume(Context, Reason);
  }
  return EFI_UNSUPPORTED;
}

STATIC EFI_STATUS HdaProgramPlaybackVolume(HDAU_CONTEXT *Context, BOOLEAN Reapply, CONST CHAR8 *Reason) {
  EFI_STATUS Status;
  UINTN Index;

  if (Context == NULL || Context->PciIo == NULL) return EFI_INVALID_PARAMETER;
  if (Reapply) return HdaProgramStoredHardwareVolume(Context, Reason);

  Context->HardwareVolumeApplied = FALSE;
  Context->HardwareVolumeInputAmp = FALSE;
  Context->HardwareVolumeNid = 0;
  Context->HardwareVolumeInputIndex = 0;

  /* Equality is intentional: 30% PCM and 30% hardware attenuation are
     independent ceilings.  No longer bypass the hardware control at 30%. */
  if (mHdaCfg.VolumeQ8 > HDAU_PCM_TRANSPORT_GAIN_Q8) return EFI_UNSUPPORTED;

  Status = HdaTryHardwareOutputVolume(Context, Context->Dac, Reason);
  if (!EFI_ERROR(Status)) return EFI_SUCCESS;
  if (Status != EFI_UNSUPPORTED) return Status;

  for (Index = Context->PathLen; Index > 0; Index--) {
    UINT8 Nid = Context->Path[Index - 1U];
    if (Nid == Context->Dac) continue;
    Status = HdaTryHardwareOutputVolume(Context, Nid, Reason);
    if (!EFI_ERROR(Status)) return EFI_SUCCESS;
    if (Status != EFI_UNSUPPORTED) return Status;
  }

  if (Context->PathLen > 1) {
    for (Index = Context->PathLen - 1U; Index > 0; Index--) {
      UINT8 Nid = Context->Path[Index - 1U];
      UINT8 NextNid = Context->Path[Index];
      Status = HdaTryHardwareInputVolume(Context, Nid, NextNid, Reason);
      if (!EFI_ERROR(Status)) return EFI_SUCCESS;
      if (Status != EFI_UNSUPPORTED) return Status;
    }
  }

  return EFI_UNSUPPORTED;
}


/* Additional layout/fanout routes do not own the selected HDAU_CONTEXT volume
   state.  Apply the same hardware limit directly, without overwriting the
   selected route's saved amplifier.  All probing uses the live codec caps. */
STATIC EFI_STATUS HdaTryHardwareVolumeOnRouteNode(
  EFI_PCI_IO_PROTOCOL *PciIo,
  UINT8 Codec,
  UINT8 Afg,
  UINT8 Nid,
  BOOLEAN InputAmp,
  UINTN InputIndex,
  CONST CHAR8 *Reason
) {
  UINT32 AwCaps;
  UINT32 AmpCaps;
  UINTN Gain;
  BOOLEAN Mute;
  UINT16 Payload;
  UINT8 Expected;
  EFI_STATUS Status;
  EFI_STATUS VerifyStatus;
  BOOLEAN FirstReadbackContradicted;

  if (PciIo == NULL || Nid == 0) return EFI_INVALID_PARAMETER;
  if (InputAmp && InputIndex > HDA_AMP_INDEX_MAX) return EFI_UNSUPPORTED;
  Status = HdaGetParamChecked(PciIo, Codec, Nid, HDA_PARAM_AUDIO_WIDGET_CAP, &AwCaps);
  if (EFI_ERROR(Status)) return Status;
  if ((AwCaps & (InputAmp ? HDA_AWCAP_IN_AMP_PRESENT : HDA_AWCAP_OUT_AMP_PRESENT)) == 0) {
    return EFI_UNSUPPORTED;
  }
  Status = HdaGetEffectiveAmpCapsChecked(PciIo, Codec, Afg, Nid, AwCaps,
                                         InputAmp ? HDA_PARAM_AMP_IN_CAP : HDA_PARAM_AMP_OUT_CAP,
                                         &AmpCaps);
  if (EFI_ERROR(Status)) return Status;
  if (!HdaAmpVolumeGainFromCaps(AmpCaps, &Gain, &Mute)) return EFI_UNSUPPORTED;

  Payload = InputAmp
              ? (UINT16)(0x7000U | (((UINT16)InputIndex & 0x0FU) << 8) |
                         (Mute ? 0x0080U : 0U) | ((UINT16)Gain & 0x007FU))
              : (UINT16)(0xB000U | (Mute ? 0x0080U : 0U) | ((UINT16)Gain & 0x007FU));
  Expected = (UINT8)((Mute ? 0x80U : 0U) | (Gain & 0x7FU));
  Status = HdaSetVerb(PciIo, Codec, Nid, HDA_VERB4(HDA_VERB_SET_AMP_GAIN_MUTE, Payload));
  if (EFI_ERROR(Status)) return Status;
  HdaLog("[HDAU] fanout hardware 30%%: nid=0x%x amp=%a index=%u gain=0x%x reason=%a",
         Nid, InputAmp ? "input" : "output", InputAmp ? (UINT32)InputIndex : 0U,
         (UINT32)Gain, Reason == NULL ? "fanout" : Reason);
  VerifyStatus = HdaVerifyAmpGain(PciIo, Codec, InputAmp, Nid, (UINT8)InputIndex, Expected);
  if (VerifyStatus == EFI_SUCCESS) return EFI_SUCCESS;
  FirstReadbackContradicted = (VerifyStatus == EFI_COMPROMISED_DATA);
  /* Match selected-path behavior: transient/unsupported preboot readback is
     advisory after a successful SET.  Retry the SAME attenuated value once. */
  HdaLog("[HDAU] fanout volume readback advisory: nid=0x%x status=%r; retrying SET",
         Nid, VerifyStatus);
  Status = HdaSetVerb(PciIo, Codec, Nid, HDA_VERB4(HDA_VERB_SET_AMP_GAIN_MUTE, Payload));
  if (EFI_ERROR(Status)) return Status;
  VerifyStatus = HdaVerifyAmpGain(PciIo, Codec, InputAmp, Nid, (UINT8)InputIndex, Expected);
  if (VerifyStatus == EFI_COMPROMISED_DATA ||
      (FirstReadbackContradicted && VerifyStatus != EFI_SUCCESS)) {
    /* Extra analog paths may be skipped by their void fanout caller.  Propagate
       the confirmed mismatch to the selected route so playback is aborted
       and the transactional codec snapshot can be restored. */
    mHdaVolumeReadbackMismatch = TRUE;
    HdaLog("[HDAU] fanout volume safety: unresolved gain mismatch at nid=0x%x; rejecting playback",
           Nid);
    return EFI_COMPROMISED_DATA;
  }
  if (VerifyStatus != EFI_SUCCESS) {
    HdaLog("[HDAU] fanout volume readback unavailable: nid=0x%x status=%r; successful SET unverified",
           Nid, VerifyStatus);
  }
  return EFI_SUCCESS;
}

/* Use one suitable amplifier per fanout route; selecting the same shared amp
   on multiple routes rewrites its absolute gain, rather than attenuating it
   repeatedly.  If a codec has no amp with enough negative range, the 30%
   digital PCM ceiling still applies, but acoustic gain is not calibrated. */
STATIC EFI_STATUS HdaLimitFanoutHardwareVolume(
  EFI_PCI_IO_PROTOCOL *PciIo,
  UINT8 Codec,
  UINT8 Afg,
  UINT8 Dac,
  CONST UINT8 *Path,
  UINTN PathLen,
  CONST CHAR8 *Reason
) {
  EFI_STATUS Status;
  UINTN Index;

  if (PciIo == NULL || Dac == 0 || Path == NULL || PathLen == 0) return EFI_INVALID_PARAMETER;
  Status = HdaTryHardwareVolumeOnRouteNode(PciIo, Codec, Afg, Dac, FALSE, 0, Reason);
  if (Status != EFI_UNSUPPORTED) return Status;

  for (Index = PathLen; Index > 0; Index--) {
    UINT8 Nid = Path[Index - 1U];
    if (Nid == Dac) continue;
    Status = HdaTryHardwareVolumeOnRouteNode(PciIo, Codec, Afg, Nid, FALSE, 0, Reason);
    if (Status != EFI_UNSUPPORTED) return Status;
  }
  if (PathLen > 1) {
    for (Index = PathLen - 1U; Index > 0; Index--) {
      UINT8 Nid = Path[Index - 1U];
      UINT8 NextNid = Path[Index];
      UINT8 Connections[HDAU_MAX_CONNECTIONS];
      UINTN Count;
      UINTN ConnIndex;

      Count = HdaGetConnections(PciIo, Codec, Nid, Connections, ARRAY_SIZE(Connections));
      for (ConnIndex = 0; ConnIndex < Count; ConnIndex++) {
        if (Connections[ConnIndex] != NextNid) continue;
        if (ConnIndex > HDA_AMP_INDEX_MAX) return EFI_UNSUPPORTED;
        Status = HdaTryHardwareVolumeOnRouteNode(PciIo, Codec, Afg, Nid,
                                                  TRUE, ConnIndex, Reason);
        if (Status != EFI_UNSUPPORTED) return Status;
        break;
      }
    }
  }
  return EFI_UNSUPPORTED;
}

STATIC BOOLEAN HdaU8ArrayContains(CONST UINT8 *Array, UINTN Count, UINT8 Value) {
  UINTN Index;
  if (Array == NULL || Value == 0) return FALSE;
  for (Index = 0; Index < Count; Index++) {
    if (Array[Index] == Value) return TRUE;
  }
  return FALSE;
}

STATIC BOOLEAN HdaIsOutputPin(EFI_PCI_IO_PROTOCOL *PciIo, UINT8 Codec, UINT8 Nid) {
  UINT32 AwCaps;
  UINT32 PinCaps;

  if (PciIo == NULL || Nid == 0) return FALSE;
  AwCaps = HdaGetParam(PciIo, Codec, Nid, HDA_PARAM_AUDIO_WIDGET_CAP);
  if (HdaWidgetType(AwCaps) != HDA_WIDGET_TYPE_PIN_COMPLEX) return FALSE;
  PinCaps = HdaGetParam(PciIo, Codec, Nid, HDA_PARAM_PIN_CAP);
  return ((PinCaps & HDA_PIN_CAP_OUTPUT) != 0);
}

STATIC EFI_STATUS HdaSetPowerD0AndWait(EFI_PCI_IO_PROTOCOL *PciIo, UINT8 Codec, UINT8 Nid) {
  EFI_STATUS Status;
  UINT32 PowerState;
  UINTN Elapsed;

  if (PciIo == NULL || Nid == 0) return EFI_INVALID_PARAMETER;
  Status = HdaSetVerb(PciIo, Codec, Nid, HDA_VERB12(HDA_VERB_SET_POWER_STATE, 0x00));
  if (EFI_ERROR(Status)) return Status;

  for (Elapsed = 0; Elapsed < HDAU_WIDGET_POWER_TIMEOUT_US; Elapsed += HDAU_POLL_GRANULARITY_US) {
    Status = HdaImmediateCommand(PciIo, Codec, Nid, HDA_VERB12(HDA_VERB_GET_POWER_STATE, 0), &PowerState);
    if (EFI_ERROR(Status)) return Status;
    if (((PowerState >> 4) & 0x0F) == 0) return EFI_SUCCESS;
    HdaStall(HDAU_POLL_GRANULARITY_US);
  }
  HdaLog("[HDAU] node power transition timeout: nid=0x%x", Nid);
  return EFI_TIMEOUT;
}

STATIC EFI_STATUS HdaSetWidgetPowerD0(EFI_PCI_IO_PROTOCOL *PciIo, UINT8 Codec, UINT8 Nid) {
  UINT32 AwCaps;
  EFI_STATUS Status;

  if (PciIo == NULL || Nid == 0) return EFI_INVALID_PARAMETER;
  Status = HdaGetParamChecked(PciIo, Codec, Nid, HDA_PARAM_AUDIO_WIDGET_CAP, &AwCaps);
  if (EFI_ERROR(Status)) return Status;
  if ((AwCaps & HDA_AWCAP_POWER_CONTROL) == 0) return EFI_SUCCESS;
  return HdaSetPowerD0AndWait(PciIo, Codec, Nid);
}

STATIC BOOLEAN HdaFindConnectionIndexForNext(
  EFI_PCI_IO_PROTOCOL *PciIo,
  UINT8 Codec,
  UINT8 Nid,
  UINT8 NextNid,
  UINT8 *IndexOut
) {
  UINT8 Connections[HDAU_MAX_CONNECTIONS];
  UINTN Count;
  UINTN Index;

  if (PciIo == NULL || IndexOut == NULL || NextNid == 0) return FALSE;
  Count = HdaGetConnections(PciIo, Codec, Nid, Connections, ARRAY_SIZE(Connections));
  for (Index = 0; Index < Count && Index <= HDA_AMP_INDEX_MAX; Index++) {
    if (Connections[Index] == NextNid) {
      *IndexOut = (UINT8)Index;
      return TRUE;
    }
  }
  return FALSE;
}

STATIC EFI_STATUS HdaSnapshotOneNode(HDAU_CONTEXT *Context, UINT8 Nid, UINT8 NextNid) {
  HDAU_WIDGET_SNAPSHOT *Snap;
  EFI_STATUS Status;
  UINT32 Value;
  UINT8 Type;
  UINT8 InputIndex;
  UINTN Existing;
  UINT32 AwCaps;
  UINT32 PinCaps;

  if (Context == NULL || Context->PciIo == NULL || Nid == 0) return EFI_INVALID_PARAMETER;
  for (Existing = 0; Existing < Context->SnapshotNodeCount; Existing++) {
    if (Context->SnapshotNodes[Existing].Nid == Nid) return EFI_SUCCESS;
  }
  if (Context->SnapshotNodeCount >= ARRAY_SIZE(Context->SnapshotNodes)) return EFI_OUT_OF_RESOURCES;

  Status = HdaGetParamChecked(Context->PciIo, Context->Codec, Nid,
                              HDA_PARAM_AUDIO_WIDGET_CAP, &AwCaps);
  if (EFI_ERROR(Status)) return Status;

  Snap = &Context->SnapshotNodes[Context->SnapshotNodeCount];
  ZeroMem(Snap, sizeof(*Snap));
  Snap->Nid = Nid;
  Snap->AwCaps = AwCaps;
  Type = HdaWidgetType(Snap->AwCaps);

  Value = 0;
  if ((Snap->AwCaps & HDA_AWCAP_POWER_CONTROL) != 0 &&
      !EFI_ERROR(HdaImmediateCommand(Context->PciIo, Context->Codec, Nid,
                                     HDA_VERB12(HDA_VERB_GET_POWER_STATE, 0), &Value))) {
    Snap->PowerState = Value;
    Snap->Flags |= HDAU_SNAP_POWER;
  }

  if ((Snap->AwCaps & HDA_AWCAP_CONNECTION_LIST) != 0 &&
      !EFI_ERROR(HdaImmediateCommand(Context->PciIo, Context->Codec, Nid,
                                     HDA_VERB12(HDA_VERB_GET_CONNECTION_SELECT, 0), &Value))) {
    Snap->ConnectionSelect = Value;
    Snap->Flags |= HDAU_SNAP_CONN_SELECT;
  }

  if (Type == HDA_WIDGET_TYPE_PIN_COMPLEX) {
    Status = HdaGetParamChecked(Context->PciIo, Context->Codec, Nid,
                                HDA_PARAM_PIN_CAP, &PinCaps);
    if (EFI_ERROR(Status)) return Status;
    if (!EFI_ERROR(HdaImmediateCommand(Context->PciIo, Context->Codec, Nid,
                                       HDA_VERB12(HDA_VERB_GET_CONFIG_DEFAULT, 0), &Value))) {
      Snap->ConfigDefault = Value;
      Snap->Flags |= HDAU_SNAP_CONFIG_DEFAULT;
    }
    if (!EFI_ERROR(HdaImmediateCommand(Context->PciIo, Context->Codec, Nid,
                                       HDA_VERB12(HDA_VERB_GET_PIN_WIDGET_CONTROL, 0), &Value))) {
      Snap->PinCtl = Value;
      Snap->Flags |= HDAU_SNAP_PIN_CTL;
    }
    if ((PinCaps & HDA_PIN_CAP_EAPD) != 0 &&
        !EFI_ERROR(HdaImmediateCommand(Context->PciIo, Context->Codec, Nid,
                                       HDA_VERB12(HDA_VERB_GET_EAPD_BTL, 0), &Value))) {
      Snap->Eapd = Value;
      Snap->Flags |= HDAU_SNAP_EAPD;
    }
    if ((Context->HdmiRoute || Context->DisplayPortRoute) && Nid == Context->Pin) {
      if (!EFI_ERROR(HdaImmediateCommand(Context->PciIo, Context->Codec, Nid,
                                         HDA_VERB12(HDA_VERB_GET_HDMI_CHAN_SLOT, 0), &Value))) {
        Snap->HdmiSlot0 = Value;
        Snap->Flags |= HDAU_SNAP_HDMI_SLOT0;
      }
      if (!EFI_ERROR(HdaImmediateCommand(Context->PciIo, Context->Codec, Nid,
                                         HDA_VERB12(HDA_VERB_GET_HDMI_CHAN_SLOT, 1), &Value))) {
        Snap->HdmiSlot1 = Value;
        Snap->Flags |= HDAU_SNAP_HDMI_SLOT1;
      }
      if (mHdaCfg.DigitalInfoFrame && Context->HdmiRoute) {
        UINT32 DipSize = 0;
        UINT32 DipIndex = 0;
        UINT32 DipXmit = 0;
        UINTN DipCount;
        UINTN DipByte;
        BOOLEAN DipComplete = TRUE;

        if (EFI_ERROR(HdaImmediateCommand(Context->PciIo, Context->Codec, Nid,
                                          HDA_VERB12(HDA_VERB_GET_HDMI_DIP_SIZE, 0), &DipSize)) ||
            EFI_ERROR(HdaImmediateCommand(Context->PciIo, Context->Codec, Nid,
                                          HDA_VERB12(HDA_VERB_GET_HDMI_DIP_INDEX, 0), &DipIndex)) ||
            EFI_ERROR(HdaImmediateCommand(Context->PciIo, Context->Codec, Nid,
                                          HDA_VERB12(HDA_VERB_GET_HDMI_DIP_XMIT, 0), &DipXmit))) {
          DipComplete = FALSE;
        }
        DipCount = (UINTN)(DipSize & 0xFFU);
        if (DipCount > ARRAY_SIZE(Snap->HdmiDipData)) DipCount = ARRAY_SIZE(Snap->HdmiDipData);
        if (DipComplete && DipCount > 0) {
          if (EFI_ERROR(HdaSetVerb(Context->PciIo, Context->Codec, Nid,
                                   HDA_VERB12(HDA_VERB_SET_HDMI_DIP_INDEX, 0)))) {
            DipComplete = FALSE;
          }
          for (DipByte = 0; DipComplete && DipByte < DipCount; DipByte++) {
            if (EFI_ERROR(HdaImmediateCommand(Context->PciIo, Context->Codec, Nid,
                                              HDA_VERB12(HDA_VERB_GET_HDMI_DIP_DATA, 0), &Value))) {
              DipComplete = FALSE;
            } else {
              Snap->HdmiDipData[DipByte] = (UINT8)Value;
            }
          }
          if (DipComplete) {
            Snap->HdmiDipDataCount = (UINT8)DipCount;
            Snap->HdmiDipIndex = DipIndex;
            Snap->HdmiDipXmit = DipXmit;
            Snap->Flags |= HDAU_SNAP_HDMI_DIP;
          }
          Status = HdaSetVerb(Context->PciIo, Context->Codec, Nid,
                              HDA_VERB12(HDA_VERB_SET_HDMI_DIP_INDEX, (UINT8)DipIndex));
          if (EFI_ERROR(Status)) return Status;
        }
        if (!DipComplete || DipCount == 0) return EFI_UNSUPPORTED;
      }
    }
  }

  if (Type == HDA_WIDGET_TYPE_AUDIO_OUT || Type == HDA_WIDGET_TYPE_AUDIO_IN) {
    if (!EFI_ERROR(HdaImmediateCommand(Context->PciIo, Context->Codec, Nid,
                                       HDA_VERB4(HDA_VERB_GET_CONVERTER_FORMAT, 0), &Value))) {
      Snap->ConverterFormat = Value;
      Snap->Flags |= HDAU_SNAP_CONVERTER_FMT;
    }
    if (!EFI_ERROR(HdaImmediateCommand(Context->PciIo, Context->Codec, Nid,
                                       HDA_VERB12(HDA_VERB_GET_CONVERTER_STREAM_CHANNEL, 0), &Value))) {
      Snap->StreamChannel = Value;
      Snap->Flags |= HDAU_SNAP_STREAM_CHAN;
    }
    if ((Snap->AwCaps & HDA_AWCAP_DIGITAL) != 0 &&
        !EFI_ERROR(HdaImmediateCommand(Context->PciIo, Context->Codec, Nid,
                                       HDA_VERB12(HDA_VERB_GET_DIGI_CONVERT_1, 0), &Value))) {
      Snap->DigitalCtl = Value;
      Snap->Flags |= HDAU_SNAP_DIGITAL_CTL;
    }
  }

  if (HdaGetParam(Context->PciIo, Context->Codec, Nid, HDA_PARAM_PROCESSING_CAP) != 0 &&
      !EFI_ERROR(HdaImmediateCommand(Context->PciIo, Context->Codec, Nid,
                                     HDA_VERB12(HDA_VERB_GET_PROCESSING_STATE, 0), &Value))) {
    Snap->ProcessingState = Value;
    Snap->Flags |= HDAU_SNAP_PROCESSING;
  }

  if ((Snap->AwCaps & HDA_AWCAP_OUT_AMP_PRESENT) != 0) {
    if (!EFI_ERROR(HdaImmediateCommand(Context->PciIo, Context->Codec, Nid,
                                       HDA_VERB4(HDA_VERB_GET_AMP_GAIN_MUTE, 0xA000), &Value))) {
      Snap->OutAmpLeft = Value;
      Snap->Flags |= HDAU_SNAP_OUT_AMP_L;
    }
    if (!EFI_ERROR(HdaImmediateCommand(Context->PciIo, Context->Codec, Nid,
                                       HDA_VERB4(HDA_VERB_GET_AMP_GAIN_MUTE, 0x8000), &Value))) {
      Snap->OutAmpRight = Value;
      Snap->Flags |= HDAU_SNAP_OUT_AMP_R;
    }
  }

  InputIndex = 0;
  if ((Snap->AwCaps & HDA_AWCAP_IN_AMP_PRESENT) != 0) {
    BOOLEAN HaveInputIndex;
    HaveInputIndex = HdaFindConnectionIndexForNext(Context->PciIo, Context->Codec, Nid, NextNid, &InputIndex);
    if (!HaveInputIndex && NextNid == 0) {
      UINT8 Connections[HDAU_MAX_CONNECTIONS];
      if (HdaGetConnections(Context->PciIo, Context->Codec, Nid, Connections, ARRAY_SIZE(Connections)) == 1) {
        InputIndex = 0;
        HaveInputIndex = TRUE;
      }
    }
    if (NextNid != 0 && !HaveInputIndex) return EFI_NOT_READY;
    if (HaveInputIndex) {
      Snap->InputIndex = InputIndex;
    if (!EFI_ERROR(HdaImmediateCommand(Context->PciIo, Context->Codec, Nid,
                                       HDA_VERB4(HDA_VERB_GET_AMP_GAIN_MUTE,
                                                 (UINT16)(0x2000U | InputIndex)), &Value))) {
      Snap->InAmpLeft = Value;
      Snap->Flags |= HDAU_SNAP_IN_AMP_L;
    }
    if (!EFI_ERROR(HdaImmediateCommand(Context->PciIo, Context->Codec, Nid,
                                       HDA_VERB4(HDA_VERB_GET_AMP_GAIN_MUTE,
                                                 (UINT16)(InputIndex & 0x0FU)), &Value))) {
      Snap->InAmpRight = Value;
      Snap->Flags |= HDAU_SNAP_IN_AMP_R;
    }
    }
  }
  Context->SnapshotNodeCount++;
  return EFI_SUCCESS;
}

STATIC HDAU_WIDGET_SNAPSHOT *HdaFindNodeSnapshot(HDAU_CONTEXT *Context, UINT8 Nid) {
  UINTN Index;
  if (Context == NULL || Nid == 0) return NULL;
  for (Index = 0; Index < Context->SnapshotNodeCount; Index++) {
    if (Context->SnapshotNodes[Index].Nid == Nid) return &Context->SnapshotNodes[Index];
  }
  return NULL;
}

STATIC EFI_STATUS HdaCaptureSnapshotValue(
  HDAU_CONTEXT *Context,
  HDAU_WIDGET_SNAPSHOT *Snap,
  UINT32 Flag,
  UINT32 VerbPayload,
  UINT32 *Destination
) {
  EFI_STATUS Status;
  UINT32 Value;

  if (Context == NULL || Snap == NULL || Destination == NULL) return EFI_INVALID_PARAMETER;
  if ((Snap->Flags & Flag) != 0) return EFI_SUCCESS;
  Value = 0;
  Status = HdaImmediateCommand(Context->PciIo, Context->Codec, Snap->Nid, VerbPayload, &Value);
  if (EFI_ERROR(Status)) return Status;
  *Destination = Value;
  Snap->Flags |= Flag;
  return EFI_SUCCESS;
}

STATIC EFI_STATUS HdaSnapshotPathNodes(
  HDAU_CONTEXT *Context,
  CONST UINT8 *Path,
  UINTN PathLen,
  UINT8 Pin,
  UINT8 Dac
) {
  UINTN Index;

  if (Context == NULL || Path == NULL || PathLen == 0) return EFI_INVALID_PARAMETER;
  if (!mHdaCfg.TransactionalRestore) return EFI_SUCCESS;

  for (Index = 0; Index < PathLen; Index++) {
    UINT8 Next = (Index + 1U < PathLen) ? Path[Index + 1U] : 0;
    HDAU_WIDGET_SNAPSHOT *Snap;
    UINT8 Connections[HDAU_MAX_CONNECTIONS];
    UINTN ConnectionCount;
    UINT8 Type;
    EFI_STATUS Status;

    Status = HdaSnapshotOneNode(Context, Path[Index], Next);
    if (EFI_ERROR(Status)) return Status;
    Snap = HdaFindNodeSnapshot(Context, Path[Index]);
    if (Snap == NULL) return EFI_COMPROMISED_DATA;
    Type = HdaWidgetType(Snap->AwCaps);

    if ((Snap->AwCaps & HDA_AWCAP_POWER_CONTROL) != 0 &&
        (Snap->Flags & HDAU_SNAP_POWER) == 0) return EFI_NOT_READY;
    if ((Snap->AwCaps & HDA_AWCAP_OUT_AMP_PRESENT) != 0 &&
        (Snap->Flags & (HDAU_SNAP_OUT_AMP_L | HDAU_SNAP_OUT_AMP_R)) !=
        (HDAU_SNAP_OUT_AMP_L | HDAU_SNAP_OUT_AMP_R)) return EFI_NOT_READY;

    ConnectionCount = HdaGetConnections(Context->PciIo, Context->Codec, Path[Index],
                                        Connections, ARRAY_SIZE(Connections));
    if (Index + 1U < PathLen) {
      UINT8 InputIndex = 0;
      if (ConnectionCount > 1U &&
          (Type == HDA_WIDGET_TYPE_AUDIO_SELECTOR || Type == HDA_WIDGET_TYPE_PIN_COMPLEX) &&
          (Snap->Flags & HDAU_SNAP_CONN_SELECT) == 0) return EFI_NOT_READY;
      if ((Snap->AwCaps & HDA_AWCAP_IN_AMP_PRESENT) != 0 &&
          HdaFindConnectionIndexForNext(Context->PciIo, Context->Codec, Path[Index],
                                        Path[Index + 1U], &InputIndex)) {
        if ((Snap->Flags & (HDAU_SNAP_IN_AMP_L | HDAU_SNAP_IN_AMP_R)) !=
            (HDAU_SNAP_IN_AMP_L | HDAU_SNAP_IN_AMP_R) || Snap->InputIndex != InputIndex) {
          return EFI_NOT_READY;
        }
      }
    } else if ((Snap->AwCaps & HDA_AWCAP_IN_AMP_PRESENT) != 0 && ConnectionCount == 1U) {
      if ((Snap->Flags & (HDAU_SNAP_IN_AMP_L | HDAU_SNAP_IN_AMP_R)) !=
          (HDAU_SNAP_IN_AMP_L | HDAU_SNAP_IN_AMP_R) || Snap->InputIndex != 0) {
        return EFI_NOT_READY;
      }
    }

    if (Path[Index] == Pin) {
      UINT32 PinCap;
      Status = HdaGetParamChecked(Context->PciIo, Context->Codec, Pin,
                                  HDA_PARAM_PIN_CAP, &PinCap);
      if (EFI_ERROR(Status)) return Status;
      if ((Snap->Flags & HDAU_SNAP_PIN_CTL) == 0) return EFI_NOT_READY;
      if ((PinCap & HDA_PIN_CAP_EAPD) != 0 && (Snap->Flags & HDAU_SNAP_EAPD) == 0) {
        return EFI_NOT_READY;
      }
    }
    if (Path[Index] == Dac) {
      if ((Snap->Flags & (HDAU_SNAP_CONVERTER_FMT | HDAU_SNAP_STREAM_CHAN)) !=
          (HDAU_SNAP_CONVERTER_FMT | HDAU_SNAP_STREAM_CHAN)) return EFI_NOT_READY;
    }
  }
  return EFI_SUCCESS;
}

STATIC EFI_STATUS HdaSnapshotWarmResetPower(HDAU_CONTEXT *Context) {
  UINT8 WStart;
  UINT8 WCount;
  UINTN Index;

  if (Context == NULL || Context->PciIo == NULL) return EFI_INVALID_PARAMETER;
  Context->WarmPowerSnapshotCount = 0;
  ZeroMem(Context->WarmPowerSnapshots, sizeof(Context->WarmPowerSnapshots));
  if (!mHdaCfg.TransactionalRestore || !mHdaCfg.RealtekCodecWarmReset ||
      mHdaActivePolicy == NULL || (mHdaActivePolicy->Flags & HDAU_CPF_REALTEK) == 0) {
    return EFI_SUCCESS;
  }
  if (!HdaGetSubnodes(Context->PciIo, Context->Codec, Context->Afg, &WStart, &WCount)) {
    return EFI_NOT_FOUND;
  }

  for (Index = 0; Index < WCount; Index++) {
    UINT8 Nid = (UINT8)(WStart + Index);
    UINT32 Caps;
    UINT8 Type;
    UINT32 Value;
    EFI_STATUS Status;

    Status = HdaGetParamChecked(Context->PciIo, Context->Codec, Nid,
                                HDA_PARAM_AUDIO_WIDGET_CAP, &Caps);
    if (EFI_ERROR(Status)) return Status;
    Type = HdaWidgetType(Caps);
    if (Type != HDA_WIDGET_TYPE_AUDIO_OUT && Type != HDA_WIDGET_TYPE_AUDIO_MIXER &&
        Type != HDA_WIDGET_TYPE_AUDIO_SELECTOR && Type != HDA_WIDGET_TYPE_PIN_COMPLEX) continue;
    if ((Caps & HDA_AWCAP_POWER_CONTROL) == 0) continue;
    if (Context->WarmPowerSnapshotCount >= ARRAY_SIZE(Context->WarmPowerSnapshots)) return EFI_OUT_OF_RESOURCES;
    Value = 0;
    Status = HdaImmediateCommand(Context->PciIo, Context->Codec, Nid,
                                 HDA_VERB12(HDA_VERB_GET_POWER_STATE, 0), &Value);
    if (EFI_ERROR(Status)) return Status;
    Context->WarmPowerSnapshots[Context->WarmPowerSnapshotCount].Nid = Nid;
    Context->WarmPowerSnapshots[Context->WarmPowerSnapshotCount].PowerState = (UINT8)(Value & 0x0FU);
    Context->WarmPowerSnapshotCount++;
  }
  return EFI_SUCCESS;
}

STATIC EFI_STATUS HdaCaptureProfileVerbState(
  HDAU_CONTEXT *Context,
  UINT8 Nid,
  UINT32 VerbPayload
) {
  HDAU_WIDGET_SNAPSHOT *Snap;
  UINT32 Verb12;
  UINT32 Verb4;
  UINT16 AmpPayload;
  EFI_STATUS Status;

  if (Context == NULL || Nid == 0) return EFI_INVALID_PARAMETER;
  Status = HdaSnapshotOneNode(Context, Nid, 0);
  if (EFI_ERROR(Status)) return Status;
  Snap = HdaFindNodeSnapshot(Context, Nid);
  if (Snap == NULL) return EFI_COMPROMISED_DATA;

  VerbPayload &= 0xFFFFFU;
  Verb12 = (VerbPayload >> 8) & 0xFFFU;
  Verb4 = (VerbPayload >> 16) & 0x0FU;

  if (Verb4 == HDA_VERB_GET_CONVERTER_FORMAT || Verb4 == HDA_VERB_GET_AMP_GAIN_MUTE ||
      Verb4 == HDA_VERB_GET_PROC_COEF || Verb4 == HDA_VERB_GET_COEF_INDEX || Verb12 >= 0xF00U) {
    return EFI_SUCCESS;
  }
  if (Verb4 == HDA_VERB_SET_COEF_INDEX || Verb4 == HDA_VERB_SET_PROC_COEF) return EFI_SUCCESS;

  if (Verb4 == HDA_VERB_SET_CONVERTER_FORMAT) {
    return HdaCaptureSnapshotValue(Context, Snap, HDAU_SNAP_CONVERTER_FMT,
                                   HDA_VERB4(HDA_VERB_GET_CONVERTER_FORMAT, 0), &Snap->ConverterFormat);
  }
  if (Verb4 == HDA_VERB_SET_AMP_GAIN_MUTE) {
    AmpPayload = (UINT16)(VerbPayload & 0xFFFFU);
    if ((AmpPayload & 0xC000U) == 0) return EFI_UNSUPPORTED;
    if ((AmpPayload & 0x8000U) != 0) {
      if ((AmpPayload & 0x2000U) != 0) {
        Status = HdaCaptureSnapshotValue(Context, Snap, HDAU_SNAP_OUT_AMP_L,
                                         HDA_VERB4(HDA_VERB_GET_AMP_GAIN_MUTE, 0xA000U), &Snap->OutAmpLeft);
        if (EFI_ERROR(Status)) return Status;
      }
      if ((AmpPayload & 0x1000U) != 0) {
        Status = HdaCaptureSnapshotValue(Context, Snap, HDAU_SNAP_OUT_AMP_R,
                                         HDA_VERB4(HDA_VERB_GET_AMP_GAIN_MUTE, 0x8000U), &Snap->OutAmpRight);
        if (EFI_ERROR(Status)) return Status;
      }
    }
    if ((AmpPayload & 0x4000U) != 0) {
      UINT8 InputIndex = (UINT8)((AmpPayload >> 8) & 0x0FU);
      if ((Snap->Flags & (HDAU_SNAP_IN_AMP_L | HDAU_SNAP_IN_AMP_R)) != 0 &&
          Snap->InputIndex != InputIndex) return EFI_UNSUPPORTED;
      Snap->InputIndex = InputIndex;
      if ((AmpPayload & 0x2000U) != 0) {
        Status = HdaCaptureSnapshotValue(Context, Snap, HDAU_SNAP_IN_AMP_L,
                                         HDA_VERB4(HDA_VERB_GET_AMP_GAIN_MUTE,
                                                   (UINT16)(0x2000U | InputIndex)), &Snap->InAmpLeft);
        if (EFI_ERROR(Status)) return Status;
      }
      if ((AmpPayload & 0x1000U) != 0) {
        Status = HdaCaptureSnapshotValue(Context, Snap, HDAU_SNAP_IN_AMP_R,
                                         HDA_VERB4(HDA_VERB_GET_AMP_GAIN_MUTE,
                                                   (UINT16)(InputIndex & 0x0FU)), &Snap->InAmpRight);
        if (EFI_ERROR(Status)) return Status;
      }
    }
    return EFI_SUCCESS;
  }

  switch (Verb12) {
    case HDA_VERB_SET_CONNECTION_SELECT:
      return HdaCaptureSnapshotValue(Context, Snap, HDAU_SNAP_CONN_SELECT,
                                     HDA_VERB12(HDA_VERB_GET_CONNECTION_SELECT, 0), &Snap->ConnectionSelect);
    case HDA_VERB_SET_PROCESSING_STATE:
      return HdaCaptureSnapshotValue(Context, Snap, HDAU_SNAP_PROCESSING,
                                     HDA_VERB12(HDA_VERB_GET_PROCESSING_STATE, 0), &Snap->ProcessingState);
    case HDA_VERB_SET_POWER_STATE:
      return HdaCaptureSnapshotValue(Context, Snap, HDAU_SNAP_POWER,
                                     HDA_VERB12(HDA_VERB_GET_POWER_STATE, 0), &Snap->PowerState);
    case HDA_VERB_SET_CONVERTER_STREAM_CHANNEL:
      return HdaCaptureSnapshotValue(Context, Snap, HDAU_SNAP_STREAM_CHAN,
                                     HDA_VERB12(HDA_VERB_GET_CONVERTER_STREAM_CHANNEL, 0), &Snap->StreamChannel);
    case HDA_VERB_SET_PIN_WIDGET_CONTROL:
      return HdaCaptureSnapshotValue(Context, Snap, HDAU_SNAP_PIN_CTL,
                                     HDA_VERB12(HDA_VERB_GET_PIN_WIDGET_CONTROL, 0), &Snap->PinCtl);
    case HDA_VERB_SET_UNSOLICITED_RESPONSE:
      return HdaCaptureSnapshotValue(Context, Snap, HDAU_SNAP_UNSOLICITED,
                                     HDA_VERB12(HDA_VERB_GET_UNSOLICITED_RESPONSE, 0), &Snap->Unsolicited);
    case HDA_VERB_SET_EAPD_BTL:
      return HdaCaptureSnapshotValue(Context, Snap, HDAU_SNAP_EAPD,
                                     HDA_VERB12(HDA_VERB_GET_EAPD_BTL, 0), &Snap->Eapd);
    case HDA_VERB_SET_DIGI_CONVERT_1:
      return HdaCaptureSnapshotValue(Context, Snap, HDAU_SNAP_DIGITAL_CTL,
                                     HDA_VERB12(HDA_VERB_GET_DIGI_CONVERT_1, 0), &Snap->DigitalCtl);
    case HDA_VERB_SET_CONFIG_DEFAULT_0:
    case HDA_VERB_SET_CONFIG_DEFAULT_1:
    case HDA_VERB_SET_CONFIG_DEFAULT_2:
    case HDA_VERB_SET_CONFIG_DEFAULT_3:
      return HdaCaptureSnapshotValue(Context, Snap, HDAU_SNAP_CONFIG_DEFAULT,
                                     HDA_VERB12(HDA_VERB_GET_CONFIG_DEFAULT, 0), &Snap->ConfigDefault);
    case HDA_VERB_SET_GPIO_DATA:
      return HdaCaptureSnapshotValue(Context, Snap, HDAU_SNAP_GPIO_DATA,
                                     HDA_VERB12(HDA_VERB_GET_GPIO_DATA, 0), &Snap->GpioData);
    case HDA_VERB_SET_GPIO_ENABLE_MASK:
      return HdaCaptureSnapshotValue(Context, Snap, HDAU_SNAP_GPIO_ENABLE,
                                     HDA_VERB12(HDA_VERB_GET_GPIO_ENABLE_MASK, 0), &Snap->GpioEnable);
    case HDA_VERB_SET_GPIO_DIRECTION:
      return HdaCaptureSnapshotValue(Context, Snap, HDAU_SNAP_GPIO_DIRECTION,
                                     HDA_VERB12(HDA_VERB_GET_GPIO_DIRECTION, 0), &Snap->GpioDirection);
    case HDA_VERB_SET_GPIO_WAKE_ENABLE_MASK:
      return HdaCaptureSnapshotValue(Context, Snap, HDAU_SNAP_GPIO_WAKE,
                                     HDA_VERB12(HDA_VERB_GET_GPIO_WAKE_ENABLE_MASK, 0), &Snap->GpioWake);
    case HDA_VERB_SET_BEEP_GENERATION:
      return HdaCaptureSnapshotValue(Context, Snap, HDAU_SNAP_BEEP,
                                     HDA_VERB12(HDA_VERB_GET_BEEP_GENERATION, 0), &Snap->BeepControl);
    case HDA_VERB_SET_VOLUME_KNOB_CONTROL:
      return HdaCaptureSnapshotValue(Context, Snap, HDAU_SNAP_VOLUME_KNOB,
                                     HDA_VERB12(HDA_VERB_GET_VOLUME_KNOB_CONTROL, 0), &Snap->VolumeKnob);
    case HDA_VERB_SET_SUBSYSTEM_ID_0:
    case HDA_VERB_SET_SUBSYSTEM_ID_1:
    case HDA_VERB_SET_SUBSYSTEM_ID_2:
    case HDA_VERB_SET_SUBSYSTEM_ID_3:
      return HdaCaptureSnapshotValue(Context, Snap, HDAU_SNAP_SUBSYSTEM_ID,
                                     HDA_VERB12(HDA_VERB_GET_SUBSYSTEM_ID, 0), &Snap->SubsystemId);
    case HDA_VERB_SET_HDMI_DIP_INDEX:
    case HDA_VERB_SET_HDMI_DIP_DATA:
    case HDA_VERB_SET_HDMI_DIP_XMIT:
    case HDA_VERB_SET_HDMI_CHAN_SLOT:
      /* Current generated ConfigData does not contain these SETs. If a future
         generator adds one, require dedicated packet/slot snapshot support
         rather than pretending the mutation is transactional. */
      return EFI_UNSUPPORTED;
    default:
      break;
  }
  return EFI_UNSUPPORTED;
}

STATIC HDAU_COEF_INDEX_SNAPSHOT *HdaGetCoefIndexSnapshot(HDAU_CONTEXT *Context, UINT8 Nid) {
  UINTN Index;
  UINT32 Value;
  EFI_STATUS Status;
  HDAU_COEF_INDEX_SNAPSHOT *Snap;

  if (Context == NULL || Nid == 0) return NULL;
  for (Index = 0; Index < Context->CoefIndexSnapshotCount; Index++) {
    if (Context->CoefIndexSnapshots[Index].Nid == Nid) return &Context->CoefIndexSnapshots[Index];
  }
  if (Context->CoefIndexSnapshotCount >= ARRAY_SIZE(Context->CoefIndexSnapshots)) return NULL;
  Value = 0;
  Status = HdaImmediateCommand(Context->PciIo, Context->Codec, Nid,
                               HDA_VERB4(HDA_VERB_GET_COEF_INDEX, 0), &Value);
  if (EFI_ERROR(Status)) return NULL;
  Snap = &Context->CoefIndexSnapshots[Context->CoefIndexSnapshotCount++];
  Snap->Nid = Nid;
  Snap->OriginalIndex = (UINT16)(Value & 0xFFFFU);
  return Snap;
}

STATIC EFI_STATUS HdaSnapshotCoefficientValue(HDAU_CONTEXT *Context, UINT8 Nid, UINT16 CoefIndex) {
  UINTN Index;
  UINT32 Value;
  EFI_STATUS Status;

  for (Index = 0; Index < Context->CoefSnapshotCount; Index++) {
    if (Context->CoefSnapshots[Index].Nid == Nid && Context->CoefSnapshots[Index].Index == CoefIndex) {
      return EFI_SUCCESS;
    }
  }
  if (Context->CoefSnapshotCount >= ARRAY_SIZE(Context->CoefSnapshots)) return EFI_OUT_OF_RESOURCES;
  Status = HdaSetVerb(Context->PciIo, Context->Codec, Nid,
                      HDA_VERB4(HDA_VERB_SET_COEF_INDEX, CoefIndex));
  if (EFI_ERROR(Status)) return Status;
  Value = 0;
  Status = HdaImmediateCommand(Context->PciIo, Context->Codec, Nid,
                               HDA_VERB4(HDA_VERB_GET_PROC_COEF, 0), &Value);
  if (EFI_ERROR(Status)) return Status;
  Context->CoefSnapshots[Context->CoefSnapshotCount].Nid = Nid;
  Context->CoefSnapshots[Context->CoefSnapshotCount].Index = CoefIndex;
  Context->CoefSnapshots[Context->CoefSnapshotCount].Value = (UINT16)(Value & 0xFFFFU);
  Context->CoefSnapshotCount++;
  return EFI_SUCCESS;
}

STATIC EFI_STATUS HdaSnapshotCoefficientSpan(HDAU_CONTEXT *Context, UINTN Offset, UINTN Count) {
  BOOLEAN HaveCurrent[HDAU_MAX_WIDGETS];
  BOOLEAN TouchedIndex[HDAU_MAX_WIDGETS];
  UINT16 CurrentIndex[HDAU_MAX_WIDGETS];
  UINTN End;
  UINTN Index;
  UINTN StartCoefCount;
  UINTN StartIndexCount;
  EFI_STATUS CaptureError;
  EFI_STATUS RestoreError;

  if (Context == NULL) return EFI_INVALID_PARAMETER;
  End = Offset + Count;
  if (Offset >= ARRAY_SIZE(mHdaAlcConfigVerbs) || End < Offset || End > ARRAY_SIZE(mHdaAlcConfigVerbs)) {
    return EFI_COMPROMISED_DATA;
  }
  StartCoefCount = Context->CoefSnapshotCount;
  StartIndexCount = Context->CoefIndexSnapshotCount;
  ZeroMem(HaveCurrent, sizeof(HaveCurrent));
  ZeroMem(TouchedIndex, sizeof(TouchedIndex));
  ZeroMem(CurrentIndex, sizeof(CurrentIndex));
  CaptureError = EFI_SUCCESS;

  for (Index = Offset; Index < End; Index++) {
    UINT32 Encoded = mHdaAlcConfigVerbs[Index];
    UINT8 Nid;
    UINT32 VerbPayload;
    UINT32 Verb4;
    HDAU_COEF_INDEX_SNAPSHOT *IndexSnap;

    if (Encoded == 0) continue;
    Nid = (UINT8)((Encoded >> 20) & 0xFFU);
    VerbPayload = Encoded & 0xFFFFFU;
    Verb4 = (VerbPayload >> 16) & 0x0FU;
    if (Verb4 != HDA_VERB_SET_COEF_INDEX && Verb4 != HDA_VERB_SET_PROC_COEF) continue;
    if (Nid > HDAU_MAX_NID) {
      CaptureError = EFI_UNSUPPORTED;
      break;
    }

    IndexSnap = HdaGetCoefIndexSnapshot(Context, Nid);
    if (IndexSnap == NULL) {
      CaptureError = EFI_UNSUPPORTED;
      break;
    }
    TouchedIndex[Nid] = TRUE;
    if (Verb4 == HDA_VERB_SET_COEF_INDEX) {
      CurrentIndex[Nid] = (UINT16)(VerbPayload & 0xFFFFU);
      HaveCurrent[Nid] = TRUE;
      continue;
    }
    if (!HaveCurrent[Nid]) {
      CurrentIndex[Nid] = IndexSnap->OriginalIndex;
      HaveCurrent[Nid] = TRUE;
    }
    CaptureError = HdaSnapshotCoefficientValue(Context, Nid, CurrentIndex[Nid]);
    if (EFI_ERROR(CaptureError)) break;
    CurrentIndex[Nid] = (UINT16)(CurrentIndex[Nid] + 1U);
  }

  RestoreError = EFI_SUCCESS;
  for (Index = 0; Index < Context->CoefIndexSnapshotCount; Index++) {
    HDAU_COEF_INDEX_SNAPSHOT *IndexSnap = &Context->CoefIndexSnapshots[Index];
    EFI_STATUS Status;
    if (!TouchedIndex[IndexSnap->Nid]) continue;
    Status = HdaSetVerb(Context->PciIo, Context->Codec,
                        IndexSnap->Nid,
                        HDA_VERB4(HDA_VERB_SET_COEF_INDEX, IndexSnap->OriginalIndex));
    if (EFI_ERROR(Status) && !EFI_ERROR(RestoreError)) RestoreError = Status;
  }
  if (EFI_ERROR(RestoreError)) return RestoreError;
  if (EFI_ERROR(CaptureError)) {
    Context->CoefSnapshotCount = StartCoefCount;
    Context->CoefIndexSnapshotCount = StartIndexCount;
    return EFI_UNSUPPORTED;
  }
  return EFI_SUCCESS;
}

STATIC EFI_STATUS HdaSnapshotConfigProfileState(HDAU_CONTEXT *Context) {
  CONST HDAU_ALCID_CONFIG_PROFILE *Profile;
  UINTN End;
  UINTN Index;
  EFI_STATUS Status;

  if (Context == NULL || mHdaActiveAlcIdProfile == NULL) return EFI_SUCCESS;
  Profile = HdaFindAlcIdConfigProfile(mHdaActiveAlcIdProfile);
  if (Profile == NULL) return EFI_SUCCESS;
  if (!HdaConfigSpanIsRollbackSafe(Profile->VerbOffset, Profile->VerbCount)) {
    Context->SkipAlcIdConfigData = TRUE;
    return EFI_SUCCESS;
  }
  End = (UINTN)Profile->VerbOffset + (UINTN)Profile->VerbCount;
  if (End < Profile->VerbOffset || End > ARRAY_SIZE(mHdaAlcConfigVerbs)) return EFI_COMPROMISED_DATA;

  for (Index = Profile->VerbOffset; Index < End; Index++) {
    UINT32 Encoded = mHdaAlcConfigVerbs[Index];
    if (Encoded == 0) continue;
    Status = HdaCaptureProfileVerbState(Context, (UINT8)((Encoded >> 20) & 0xFFU), Encoded & 0xFFFFFU);
    if (EFI_ERROR(Status)) {
      Context->SkipAlcIdConfigData = TRUE;
      HdaLog("[HDAU] ConfigData snapshot unavailable; profile will be skipped transactionally: profile=%a index=%u status=%r",
             Profile->Name, (UINT32)Index, Status);
      return EFI_SUCCESS;
    }
  }

  Status = HdaSnapshotCoefficientSpan(Context, Profile->VerbOffset, Profile->VerbCount);
  if (Status == EFI_UNSUPPORTED) {
    Context->SkipAlcIdConfigData = TRUE;
    HdaLog("[HDAU] ConfigData coefficient snapshot unavailable; profile will be skipped transactionally: %a", Profile->Name);
    return EFI_SUCCESS;
  }
  return Status;
}

STATIC EFI_STATUS HdaSnapshotCodecState(HDAU_CONTEXT *Context) {
  UINTN Index;
  UINT32 Value;

  if (Context == NULL || Context->PciIo == NULL || Context->PathLen < 2) return EFI_INVALID_PARAMETER;
  Context->SnapshotNodeCount = 0;
  Context->CoefSnapshotCount = 0;
  Context->CoefIndexSnapshotCount = 0;
  Context->WarmPowerSnapshotCount = 0;
  Context->SnapshotValid = FALSE;
  Context->SnapshotRestored = FALSE;
  Context->SkipAlcIdConfigData = FALSE;
  Context->SkipExactProfileData = FALSE;
  Context->HaveAfgPowerSnapshot = FALSE;
  Context->HaveGpioSnapshot = FALSE;
  Context->HaveGpioWakeSnapshot = FALSE;
  ZeroMem(Context->SnapshotNodes, sizeof(Context->SnapshotNodes));
  ZeroMem(Context->CoefSnapshots, sizeof(Context->CoefSnapshots));
  ZeroMem(Context->CoefIndexSnapshots, sizeof(Context->CoefIndexSnapshots));
  ZeroMem(Context->WarmPowerSnapshots, sizeof(Context->WarmPowerSnapshots));
  if (!mHdaCfg.TransactionalRestore) return EFI_SUCCESS;

  Value = 0;
  if (!EFI_ERROR(HdaImmediateCommand(Context->PciIo, Context->Codec, Context->Afg,
                                     HDA_VERB12(HDA_VERB_GET_POWER_STATE, 0), &Value))) {
    Context->OriginalAfgPowerState = Value;
    Context->HaveAfgPowerSnapshot = TRUE;
  }
  if (!Context->HaveAfgPowerSnapshot) return EFI_NOT_READY;

  if (HdaGetParam(Context->PciIo, Context->Codec, Context->Afg, HDA_PARAM_GPIO_CAP) != 0) {
    if (!EFI_ERROR(HdaImmediateCommand(Context->PciIo, Context->Codec, Context->Afg,
                                       HDA_VERB12(HDA_VERB_GET_GPIO_DATA, 0), &Value))) {
      Context->OriginalGpioData = Value;
      if (!EFI_ERROR(HdaImmediateCommand(Context->PciIo, Context->Codec, Context->Afg,
                                         HDA_VERB12(HDA_VERB_GET_GPIO_ENABLE_MASK, 0), &Value))) {
        Context->OriginalGpioEnable = Value;
        if (!EFI_ERROR(HdaImmediateCommand(Context->PciIo, Context->Codec, Context->Afg,
                                           HDA_VERB12(HDA_VERB_GET_GPIO_DIRECTION, 0), &Value))) {
          Context->OriginalGpioDirection = Value;
          Context->HaveGpioSnapshot = TRUE;
        }
      }
    }
  }

  {
    EFI_STATUS Status = HdaSnapshotPathNodes(Context, Context->Path, Context->PathLen,
                                             Context->Pin, Context->Dac);
    if (EFI_ERROR(Status)) return Status;
    Status = HdaSnapshotWarmResetPower(Context);
    if (EFI_ERROR(Status)) return Status;
  }

  /* AppleALC ConfigData and exact profiles may touch pins or widgets outside
     the selected route. Snapshot every unique target before any profile verb
     is applied so candidate retries and post-playback cleanup remain
     transactional for all standard HDA state that can be read back. */
  if (mHdaActiveAlcIdProfile != NULL) {
    CONST HDAU_ALCID_CONFIG_PROFILE *ConfigProfile =
      HdaFindAlcIdConfigProfile(mHdaActiveAlcIdProfile);
    if (ConfigProfile != NULL) {
      UINTN End = (UINTN)ConfigProfile->VerbOffset + (UINTN)ConfigProfile->VerbCount;
      if (End < ConfigProfile->VerbOffset || End > ARRAY_SIZE(mHdaAlcConfigVerbs)) {
        return EFI_COMPROMISED_DATA;
      }
      for (Index = ConfigProfile->VerbOffset; Index < End; Index++) {
        UINT8 Nid = (UINT8)((mHdaAlcConfigVerbs[Index] >> 20) & 0xFFU);
        EFI_STATUS Status;
        if (Nid == 0) continue;
        Status = HdaSnapshotOneNode(Context, Nid, 0);
        if (EFI_ERROR(Status)) return Status;
      }
    }
  }

  {
    EFI_STATUS Status = HdaSnapshotConfigProfileState(Context);
    if (EFI_ERROR(Status)) return Status;
  }

  if (mHdaCfg.ExactProfileInit && mHdaBootArgsAlcIdValid) {
    for (Index = 0; Index < ARRAY_SIZE(mHdaAlcExactProfiles); Index++) {
      CONST HDAU_ALCID_EXACT_PROFILE *Exact = &mHdaAlcExactProfiles[Index];
      UINTN VerbIndex;
      UINTN End;
      if (Exact->CodecId == 0 || Exact->VerbCount == 0) continue;
      if (Exact->CodecId != Context->CodecId || Exact->LayoutId != mHdaBootArgsAlcId) continue;
      if (Exact->RevisionMask != 0 &&
          (!Context->RevisionIdValid ||
           (Context->RevisionId & Exact->RevisionMask) !=
           (Exact->RevisionId & Exact->RevisionMask))) continue;
      if (Exact->SubsystemMask != 0 &&
          (!Context->SubsystemIdValid ||
           (Context->SubsystemId & Exact->SubsystemMask) !=
           (Exact->SubsystemId & Exact->SubsystemMask))) continue;
      End = (UINTN)Exact->VerbOffset + (UINTN)Exact->VerbCount;
      if (End < Exact->VerbOffset || End > ARRAY_SIZE(mHdaAlcConfigVerbs)) {
        return EFI_COMPROMISED_DATA;
      }
      if (!HdaConfigSpanIsRollbackSafe(Exact->VerbOffset, Exact->VerbCount)) {
        Context->SkipExactProfileData = TRUE;
        HdaLog("[HDAU] exact profile snapshot skipped: private mutation has no deterministic rollback");
        break;
      }
      for (VerbIndex = Exact->VerbOffset; VerbIndex < End; VerbIndex++) {
        UINT32 Encoded = mHdaAlcConfigVerbs[VerbIndex];
        UINT8 Nid;
        EFI_STATUS Status;
        if (Encoded == 0) continue;
        Nid = (UINT8)((Encoded >> 20) & 0xFFU);
        if (Nid == 0) continue;
        Status = HdaCaptureProfileVerbState(Context, Nid, Encoded & 0xFFFFFU);
        if (EFI_ERROR(Status)) {
          Context->SkipExactProfileData = TRUE;
          HdaLog("[HDAU] exact profile snapshot unavailable; exact sequence will be skipped: index=%u status=%r",
                 (UINT32)VerbIndex, Status);
          break;
        }
      }
      if (!Context->SkipExactProfileData) {
        EFI_STATUS Status = HdaSnapshotCoefficientSpan(Context, Exact->VerbOffset, Exact->VerbCount);
        if (Status == EFI_UNSUPPORTED) {
          Context->SkipExactProfileData = TRUE;
          HdaLog("[HDAU] exact profile coefficient snapshot unavailable; exact sequence will be skipped");
        } else if (EFI_ERROR(Status)) {
          return Status;
        }
      }
      break;
    }
  }
  Context->SnapshotValid = (Context->SnapshotNodeCount > 0 || Context->CoefSnapshotCount > 0 ||
                            Context->CoefIndexSnapshotCount > 0 || Context->WarmPowerSnapshotCount > 0);
  HdaLog("[HDAU] codec snapshot: nodes=%u coefficients=%u coefIndices=%u warmPower=%u skipConfig=%u skipExact=%u routeCandidate=%u",
         (UINT32)Context->SnapshotNodeCount, (UINT32)Context->CoefSnapshotCount,
         (UINT32)Context->CoefIndexSnapshotCount, (UINT32)Context->WarmPowerSnapshotCount,
         Context->SkipAlcIdConfigData ? 1 : 0, Context->SkipExactProfileData ? 1 : 0,
         (UINT32)Context->RouteCandidateIndex);
  return EFI_SUCCESS;
}

STATIC EFI_STATUS HdaRestoreCodecState(HDAU_CONTEXT *Context, CONST CHAR8 *Reason) {
  EFI_STATUS FirstError;
  UINTN Index;

  if (Context == NULL || Context->PciIo == NULL) return EFI_INVALID_PARAMETER;
  if (!mHdaCfg.TransactionalRestore || !Context->SnapshotValid || Context->SnapshotRestored) return EFI_SUCCESS;
  if (Context->PciDmaContainment) return EFI_ACCESS_DENIED;
  FirstError = EFI_SUCCESS;

  /* Processing Coefficient SET auto-increments the coefficient index. Restore
     each saved value by selecting its exact index, then restore the original
     per-node index after all values are back in place. */
  for (Index = Context->CoefSnapshotCount; Index > 0; Index--) {
    HDAU_COEF_SNAPSHOT *Coef = &Context->CoefSnapshots[Index - 1U];
    EFI_STATUS Status;
    Status = HdaSetWidgetPowerD0(Context->PciIo, Context->Codec, Coef->Nid);
    if (EFI_ERROR(Status) && !EFI_ERROR(FirstError)) FirstError = Status;
    Status = HdaSetVerb(Context->PciIo, Context->Codec, Coef->Nid,
                        HDA_VERB4(HDA_VERB_SET_COEF_INDEX, Coef->Index));
    if (EFI_ERROR(Status) && !EFI_ERROR(FirstError)) FirstError = Status;
    Status = HdaSetVerb(Context->PciIo, Context->Codec, Coef->Nid,
                        HDA_VERB4(HDA_VERB_SET_PROC_COEF, Coef->Value));
    if (EFI_ERROR(Status) && !EFI_ERROR(FirstError)) FirstError = Status;
  }
  for (Index = Context->CoefIndexSnapshotCount; Index > 0; Index--) {
    HDAU_COEF_INDEX_SNAPSHOT *CoefIndex = &Context->CoefIndexSnapshots[Index - 1U];
    EFI_STATUS Status = HdaSetVerb(Context->PciIo, Context->Codec, CoefIndex->Nid,
                                   HDA_VERB4(HDA_VERB_SET_COEF_INDEX, CoefIndex->OriginalIndex));
    if (EFI_ERROR(Status) && !EFI_ERROR(FirstError)) FirstError = Status;
  }

  for (Index = Context->SnapshotNodeCount; Index > 0; Index--) {
    HDAU_WIDGET_SNAPSHOT *Snap = &Context->SnapshotNodes[Index - 1U];
    EFI_STATUS Status;
    UINT8 OriginalPower = (UINT8)(Snap->PowerState & 0x0FU);

    if ((Snap->Flags & HDAU_SNAP_POWER) != 0 && OriginalPower != 0) {
      Status = HdaSetWidgetPowerD0(Context->PciIo, Context->Codec, Snap->Nid);
      if (EFI_ERROR(Status) && !EFI_ERROR(FirstError)) FirstError = Status;
    }
    if ((Snap->Flags & HDAU_SNAP_CONN_SELECT) != 0) {
      Status = HdaSetVerb(Context->PciIo, Context->Codec, Snap->Nid,
                          HDA_VERB12(HDA_VERB_SET_CONNECTION_SELECT, (UINT8)Snap->ConnectionSelect));
      if (EFI_ERROR(Status) && !EFI_ERROR(FirstError)) FirstError = Status;
    }
    if ((Snap->Flags & HDAU_SNAP_PROCESSING) != 0) {
      Status = HdaSetVerb(Context->PciIo, Context->Codec, Snap->Nid,
                          HDA_VERB12(HDA_VERB_SET_PROCESSING_STATE, (UINT8)Snap->ProcessingState));
      if (EFI_ERROR(Status) && !EFI_ERROR(FirstError)) FirstError = Status;
    }
    if ((Snap->Flags & HDAU_SNAP_CONFIG_DEFAULT) != 0) {
      Status = HdaSetVerb(Context->PciIo, Context->Codec, Snap->Nid,
                          HDA_VERB12(HDA_VERB_SET_CONFIG_DEFAULT_0,
                                     (UINT8)(Snap->ConfigDefault & 0xFFU)));
      if (EFI_ERROR(Status) && !EFI_ERROR(FirstError)) FirstError = Status;
      Status = HdaSetVerb(Context->PciIo, Context->Codec, Snap->Nid,
                          HDA_VERB12(HDA_VERB_SET_CONFIG_DEFAULT_1,
                                     (UINT8)((Snap->ConfigDefault >> 8) & 0xFFU)));
      if (EFI_ERROR(Status) && !EFI_ERROR(FirstError)) FirstError = Status;
      Status = HdaSetVerb(Context->PciIo, Context->Codec, Snap->Nid,
                          HDA_VERB12(HDA_VERB_SET_CONFIG_DEFAULT_2,
                                     (UINT8)((Snap->ConfigDefault >> 16) & 0xFFU)));
      if (EFI_ERROR(Status) && !EFI_ERROR(FirstError)) FirstError = Status;
      Status = HdaSetVerb(Context->PciIo, Context->Codec, Snap->Nid,
                          HDA_VERB12(HDA_VERB_SET_CONFIG_DEFAULT_3,
                                     (UINT8)((Snap->ConfigDefault >> 24) & 0xFFU)));
      if (EFI_ERROR(Status) && !EFI_ERROR(FirstError)) FirstError = Status;
    }
    if ((Snap->Flags & HDAU_SNAP_PIN_CTL) != 0) {
      Status = HdaSetVerb(Context->PciIo, Context->Codec, Snap->Nid,
                          HDA_VERB12(HDA_VERB_SET_PIN_WIDGET_CONTROL, (UINT8)Snap->PinCtl));
      if (EFI_ERROR(Status) && !EFI_ERROR(FirstError)) FirstError = Status;
    }
    if ((Snap->Flags & HDAU_SNAP_EAPD) != 0) {
      Status = HdaSetVerb(Context->PciIo, Context->Codec, Snap->Nid,
                          HDA_VERB12(HDA_VERB_SET_EAPD_BTL, (UINT8)Snap->Eapd));
      if (EFI_ERROR(Status) && !EFI_ERROR(FirstError)) FirstError = Status;
    }
    if ((Snap->Flags & HDAU_SNAP_HDMI_SLOT0) != 0) {
      Status = HdaSetVerb(Context->PciIo, Context->Codec, Snap->Nid,
                          HDA_VERB12(HDA_VERB_SET_HDMI_CHAN_SLOT, (UINT8)Snap->HdmiSlot0));
      if (EFI_ERROR(Status) && !EFI_ERROR(FirstError)) FirstError = Status;
    }
    if ((Snap->Flags & HDAU_SNAP_HDMI_SLOT1) != 0) {
      Status = HdaSetVerb(Context->PciIo, Context->Codec, Snap->Nid,
                          HDA_VERB12(HDA_VERB_SET_HDMI_CHAN_SLOT, (UINT8)Snap->HdmiSlot1));
      if (EFI_ERROR(Status) && !EFI_ERROR(FirstError)) FirstError = Status;
    }
    if ((Snap->Flags & HDAU_SNAP_HDMI_DIP) != 0) {
      UINTN DipByte;
      Status = HdaSetVerb(Context->PciIo, Context->Codec, Snap->Nid,
                          HDA_VERB12(HDA_VERB_SET_HDMI_DIP_XMIT, 0));
      if (EFI_ERROR(Status) && !EFI_ERROR(FirstError)) FirstError = Status;
      Status = HdaSetVerb(Context->PciIo, Context->Codec, Snap->Nid,
                          HDA_VERB12(HDA_VERB_SET_HDMI_DIP_INDEX, 0));
      if (EFI_ERROR(Status) && !EFI_ERROR(FirstError)) FirstError = Status;
      for (DipByte = 0; DipByte < Snap->HdmiDipDataCount; DipByte++) {
        Status = HdaSetVerb(Context->PciIo, Context->Codec, Snap->Nid,
                            HDA_VERB12(HDA_VERB_SET_HDMI_DIP_DATA, Snap->HdmiDipData[DipByte]));
        if (EFI_ERROR(Status) && !EFI_ERROR(FirstError)) FirstError = Status;
      }
      Status = HdaSetVerb(Context->PciIo, Context->Codec, Snap->Nid,
                          HDA_VERB12(HDA_VERB_SET_HDMI_DIP_INDEX, (UINT8)Snap->HdmiDipIndex));
      if (EFI_ERROR(Status) && !EFI_ERROR(FirstError)) FirstError = Status;
      Status = HdaSetVerb(Context->PciIo, Context->Codec, Snap->Nid,
                          HDA_VERB12(HDA_VERB_SET_HDMI_DIP_XMIT, (UINT8)Snap->HdmiDipXmit));
      if (EFI_ERROR(Status) && !EFI_ERROR(FirstError)) FirstError = Status;
    }
    if ((Snap->Flags & HDAU_SNAP_DIGITAL_CTL) != 0) {
      Status = HdaSetVerb(Context->PciIo, Context->Codec, Snap->Nid,
                          HDA_VERB12(HDA_VERB_SET_DIGI_CONVERT_1, (UINT8)Snap->DigitalCtl));
      if (EFI_ERROR(Status) && !EFI_ERROR(FirstError)) FirstError = Status;
    }
    if ((Snap->Flags & HDAU_SNAP_CONVERTER_FMT) != 0) {
      Status = HdaSetVerb(Context->PciIo, Context->Codec, Snap->Nid,
                          HDA_VERB4(HDA_VERB_SET_CONVERTER_FORMAT, (UINT16)Snap->ConverterFormat));
      if (EFI_ERROR(Status) && !EFI_ERROR(FirstError)) FirstError = Status;
    }
    if ((Snap->Flags & HDAU_SNAP_STREAM_CHAN) != 0) {
      Status = HdaSetVerb(Context->PciIo, Context->Codec, Snap->Nid,
                          HDA_VERB12(HDA_VERB_SET_CONVERTER_STREAM_CHANNEL, (UINT8)Snap->StreamChannel));
      if (EFI_ERROR(Status) && !EFI_ERROR(FirstError)) FirstError = Status;
    }
    if ((Snap->Flags & HDAU_SNAP_OUT_AMP_L) != 0) {
      Status = HdaSetVerb(Context->PciIo, Context->Codec, Snap->Nid,
                          HDA_VERB4(HDA_VERB_SET_AMP_GAIN_MUTE,
                                    (UINT16)(0xA000U | ((UINT8)Snap->OutAmpLeft))));
      if (EFI_ERROR(Status) && !EFI_ERROR(FirstError)) FirstError = Status;
    }
    if ((Snap->Flags & HDAU_SNAP_OUT_AMP_R) != 0) {
      Status = HdaSetVerb(Context->PciIo, Context->Codec, Snap->Nid,
                          HDA_VERB4(HDA_VERB_SET_AMP_GAIN_MUTE,
                                    (UINT16)(0x9000U | ((UINT8)Snap->OutAmpRight))));
      if (EFI_ERROR(Status) && !EFI_ERROR(FirstError)) FirstError = Status;
    }
    if ((Snap->Flags & HDAU_SNAP_IN_AMP_L) != 0) {
      Status = HdaSetVerb(Context->PciIo, Context->Codec, Snap->Nid,
                          HDA_VERB4(HDA_VERB_SET_AMP_GAIN_MUTE,
                                    (UINT16)(0x6000U | (((UINT16)Snap->InputIndex & 0x0FU) << 8) |
                                             ((UINT8)Snap->InAmpLeft))));
      if (EFI_ERROR(Status) && !EFI_ERROR(FirstError)) FirstError = Status;
    }
    if ((Snap->Flags & HDAU_SNAP_IN_AMP_R) != 0) {
      Status = HdaSetVerb(Context->PciIo, Context->Codec, Snap->Nid,
                          HDA_VERB4(HDA_VERB_SET_AMP_GAIN_MUTE,
                                    (UINT16)(0x5000U | (((UINT16)Snap->InputIndex & 0x0FU) << 8) |
                                             ((UINT8)Snap->InAmpRight))));
      if (EFI_ERROR(Status) && !EFI_ERROR(FirstError)) FirstError = Status;
    }
    if ((Snap->Flags & HDAU_SNAP_UNSOLICITED) != 0) {
      Status = HdaSetVerb(Context->PciIo, Context->Codec, Snap->Nid,
                          HDA_VERB12(HDA_VERB_SET_UNSOLICITED_RESPONSE, (UINT8)Snap->Unsolicited));
      if (EFI_ERROR(Status) && !EFI_ERROR(FirstError)) FirstError = Status;
    }
    if ((Snap->Flags & HDAU_SNAP_GPIO_DATA) != 0) {
      Status = HdaSetVerb(Context->PciIo, Context->Codec, Snap->Nid,
                          HDA_VERB12(HDA_VERB_SET_GPIO_DATA, (UINT8)Snap->GpioData));
      if (EFI_ERROR(Status) && !EFI_ERROR(FirstError)) FirstError = Status;
    }
    if ((Snap->Flags & HDAU_SNAP_GPIO_DIRECTION) != 0) {
      Status = HdaSetVerb(Context->PciIo, Context->Codec, Snap->Nid,
                          HDA_VERB12(HDA_VERB_SET_GPIO_DIRECTION, (UINT8)Snap->GpioDirection));
      if (EFI_ERROR(Status) && !EFI_ERROR(FirstError)) FirstError = Status;
    }
    if ((Snap->Flags & HDAU_SNAP_GPIO_ENABLE) != 0) {
      Status = HdaSetVerb(Context->PciIo, Context->Codec, Snap->Nid,
                          HDA_VERB12(HDA_VERB_SET_GPIO_ENABLE_MASK, (UINT8)Snap->GpioEnable));
      if (EFI_ERROR(Status) && !EFI_ERROR(FirstError)) FirstError = Status;
    }
    if ((Snap->Flags & HDAU_SNAP_GPIO_WAKE) != 0) {
      Status = HdaSetVerb(Context->PciIo, Context->Codec, Snap->Nid,
                          HDA_VERB12(HDA_VERB_SET_GPIO_WAKE_ENABLE_MASK, (UINT8)Snap->GpioWake));
      if (EFI_ERROR(Status) && !EFI_ERROR(FirstError)) FirstError = Status;
    }
    if ((Snap->Flags & HDAU_SNAP_BEEP) != 0) {
      Status = HdaSetVerb(Context->PciIo, Context->Codec, Snap->Nid,
                          HDA_VERB12(HDA_VERB_SET_BEEP_GENERATION, (UINT8)Snap->BeepControl));
      if (EFI_ERROR(Status) && !EFI_ERROR(FirstError)) FirstError = Status;
    }
    if ((Snap->Flags & HDAU_SNAP_VOLUME_KNOB) != 0) {
      Status = HdaSetVerb(Context->PciIo, Context->Codec, Snap->Nid,
                          HDA_VERB12(HDA_VERB_SET_VOLUME_KNOB_CONTROL, (UINT8)Snap->VolumeKnob));
      if (EFI_ERROR(Status) && !EFI_ERROR(FirstError)) FirstError = Status;
    }
    if ((Snap->Flags & HDAU_SNAP_SUBSYSTEM_ID) != 0) {
      Status = HdaSetVerb(Context->PciIo, Context->Codec, Snap->Nid,
                          HDA_VERB12(HDA_VERB_SET_SUBSYSTEM_ID_0, (UINT8)(Snap->SubsystemId & 0xFFU)));
      if (EFI_ERROR(Status) && !EFI_ERROR(FirstError)) FirstError = Status;
      Status = HdaSetVerb(Context->PciIo, Context->Codec, Snap->Nid,
                          HDA_VERB12(HDA_VERB_SET_SUBSYSTEM_ID_1, (UINT8)((Snap->SubsystemId >> 8) & 0xFFU)));
      if (EFI_ERROR(Status) && !EFI_ERROR(FirstError)) FirstError = Status;
      Status = HdaSetVerb(Context->PciIo, Context->Codec, Snap->Nid,
                          HDA_VERB12(HDA_VERB_SET_SUBSYSTEM_ID_2, (UINT8)((Snap->SubsystemId >> 16) & 0xFFU)));
      if (EFI_ERROR(Status) && !EFI_ERROR(FirstError)) FirstError = Status;
      Status = HdaSetVerb(Context->PciIo, Context->Codec, Snap->Nid,
                          HDA_VERB12(HDA_VERB_SET_SUBSYSTEM_ID_3, (UINT8)((Snap->SubsystemId >> 24) & 0xFFU)));
      if (EFI_ERROR(Status) && !EFI_ERROR(FirstError)) FirstError = Status;
    }
    if ((Snap->Flags & HDAU_SNAP_POWER) != 0 && OriginalPower != 0) {
      Status = HdaSetVerb(Context->PciIo, Context->Codec, Snap->Nid,
                          HDA_VERB12(HDA_VERB_SET_POWER_STATE, OriginalPower));
      if (EFI_ERROR(Status) && !EFI_ERROR(FirstError)) FirstError = Status;
    }
  }

  for (Index = Context->WarmPowerSnapshotCount; Index > 0; Index--) {
    HDAU_POWER_SNAPSHOT *Power = &Context->WarmPowerSnapshots[Index - 1U];
    EFI_STATUS Status = HdaSetVerb(Context->PciIo, Context->Codec, Power->Nid,
                                   HDA_VERB12(HDA_VERB_SET_POWER_STATE, Power->PowerState));
    if (EFI_ERROR(Status) && !EFI_ERROR(FirstError)) FirstError = Status;
  }

  if (Context->HaveGpioSnapshot) {
    EFI_STATUS Status;
    Status = HdaSetVerb(Context->PciIo, Context->Codec, Context->Afg,
                        HDA_VERB12(HDA_VERB_SET_GPIO_DATA, (UINT8)Context->OriginalGpioData));
    if (EFI_ERROR(Status) && !EFI_ERROR(FirstError)) FirstError = Status;
    Status = HdaSetVerb(Context->PciIo, Context->Codec, Context->Afg,
                        HDA_VERB12(HDA_VERB_SET_GPIO_DIRECTION, (UINT8)Context->OriginalGpioDirection));
    if (EFI_ERROR(Status) && !EFI_ERROR(FirstError)) FirstError = Status;
    Status = HdaSetVerb(Context->PciIo, Context->Codec, Context->Afg,
                        HDA_VERB12(HDA_VERB_SET_GPIO_ENABLE_MASK, (UINT8)Context->OriginalGpioEnable));
    if (EFI_ERROR(Status) && !EFI_ERROR(FirstError)) FirstError = Status;
  }
  if (Context->HaveAfgPowerSnapshot) {
    EFI_STATUS Status;
    UINT8 OriginalPower = (UINT8)(Context->OriginalAfgPowerState & 0x0FU);
    Status = HdaSetVerb(Context->PciIo, Context->Codec, Context->Afg,
                        HDA_VERB12(HDA_VERB_SET_POWER_STATE, OriginalPower));
    if (EFI_ERROR(Status) && !EFI_ERROR(FirstError)) FirstError = Status;
  }
  /* Keep the snapshot eligible for a later cleanup retry when any write
     failed. Mark it restored only after the whole transaction succeeded. */
  Context->SnapshotRestored = !EFI_ERROR(FirstError);
  HdaLog("[HDAU] codec restore: nodes=%u coefficients=%u warmPower=%u afgPower=%u reason=%a status=%r",
         (UINT32)Context->SnapshotNodeCount, (UINT32)Context->CoefSnapshotCount,
         (UINT32)Context->WarmPowerSnapshotCount, Context->HaveAfgPowerSnapshot ? 1 : 0,
         Reason == NULL ? "unspecified" : Reason, FirstError);
  return FirstError;
}

STATIC EFI_STATUS HdaRestoreCodecStateRetry(HDAU_CONTEXT *Context, CONST CHAR8 *Reason) {
  EFI_STATUS Status;
  EFI_STATUS RetryStatus;

  Status = HdaRestoreCodecState(Context, Reason);
  if (!EFI_ERROR(Status) || Context == NULL || Context->PciDmaContainment ||
      !Context->SnapshotValid || Context->SnapshotRestored) {
    return Status;
  }

  /* A codec may reject one write while a widget is still completing a power
     transition. Retry the complete idempotent restore transaction once before
     allowing a temporary candidate or playback failure to leave residual
     state for the next boot stage. */
  HdaStall(1000);
  RetryStatus = HdaRestoreCodecState(Context, "restore-retry");
  HdaLog("[HDAU] codec restore retry: reason=%a first=%r retry=%r",
         Reason == NULL ? "unspecified" : Reason, Status, RetryStatus);
  return EFI_ERROR(RetryStatus) ? RetryStatus : EFI_SUCCESS;
}

STATIC BOOLEAN HdaIsAudioOutputDac(EFI_PCI_IO_PROTOCOL *PciIo, UINT8 Codec, UINT8 Afg, UINT8 Nid) {
  UINT32 AwCaps;
  UINT32 PcmCaps;
  UINT32 StreamCaps;
  UINT8 ParamNid;
  BOOLEAN HasRate;
  EFI_STATUS Status;

  if (PciIo == NULL || Afg == 0 || Nid == 0) return FALSE;
  Status = HdaGetParamChecked(PciIo, Codec, Nid, HDA_PARAM_AUDIO_WIDGET_CAP, &AwCaps);
  if (EFI_ERROR(Status)) return FALSE;
  if (HdaWidgetType(AwCaps) != HDA_WIDGET_TYPE_AUDIO_OUT) return FALSE;

  ParamNid = ((AwCaps & HDA_AWCAP_FORMAT_OVERRIDE) != 0) ? Nid : Afg;
  Status = HdaGetParamChecked(PciIo, Codec, ParamNid, HDA_PARAM_SUPPORTED_PCM, &PcmCaps);
  if (EFI_ERROR(Status)) return FALSE;
  Status = HdaGetParamChecked(PciIo, Codec, ParamNid, HDA_PARAM_SUPPORTED_STREAM, &StreamCaps);
  if (EFI_ERROR(Status)) return FALSE;
  HasRate = ((PcmCaps & HDA_PCM_RATE_48K) != 0) ||
            (mHdaCfg.PcmFormatFallback && ((PcmCaps & HDA_PCM_RATE_44K) != 0));
  if (!HasRate || (PcmCaps & HDA_PCM_BITS_16) == 0 ||
      (StreamCaps & HDA_STREAM_FORMAT_PCM) == 0) {
    if (mHdaCfg.Debug) {
      HdaLog("[HDAU] converter skip: dac=0x%x lacks supported PCM rate/16-bit wcaps=0x%x pcm=0x%x stream=0x%x sourceNid=0x%x",
             Nid, AwCaps, PcmCaps, StreamCaps, ParamNid);
    }
    return FALSE;
  }
  return TRUE;
}

STATIC EFI_STATUS HdaSelectPcmFormat(HDAU_CONTEXT *Context) {
  UINT32 AwCaps;
  UINT32 PcmCaps;
  UINT32 StreamCaps;
  UINT8 ParamNid;
  BOOLEAN Stereo;
  BOOLEAN Rate48;
  BOOLEAN Rate44;
  EFI_STATUS Status;

  if (Context == NULL || Context->PciIo == NULL || Context->Afg == 0 || Context->Dac == 0) {
    return EFI_INVALID_PARAMETER;
  }
  Status = HdaGetParamChecked(Context->PciIo, Context->Codec, Context->Dac,
                              HDA_PARAM_AUDIO_WIDGET_CAP, &AwCaps);
  if (EFI_ERROR(Status)) return Status;
  ParamNid = ((AwCaps & HDA_AWCAP_FORMAT_OVERRIDE) != 0) ? Context->Dac : Context->Afg;
  Status = HdaGetParamChecked(Context->PciIo, Context->Codec, ParamNid,
                              HDA_PARAM_SUPPORTED_PCM, &PcmCaps);
  if (EFI_ERROR(Status)) return Status;
  Status = HdaGetParamChecked(Context->PciIo, Context->Codec, ParamNid,
                              HDA_PARAM_SUPPORTED_STREAM, &StreamCaps);
  if (EFI_ERROR(Status)) return Status;
  if ((PcmCaps & HDA_PCM_BITS_16) == 0 || (StreamCaps & HDA_STREAM_FORMAT_PCM) == 0) {
    return EFI_UNSUPPORTED;
  }
  Stereo = ((AwCaps & HDA_AWCAP_STEREO) != 0);
  Rate48 = ((PcmCaps & HDA_PCM_RATE_48K) != 0);
  Rate44 = ((PcmCaps & HDA_PCM_RATE_44K) != 0);

  Context->Channels = Stereo ? 2 : 1;
  if (Rate48) {
    Context->SampleRate = 48000;
    Context->StreamFormat = Stereo ? HDA_FMT_48K_16B_STEREO : HDA_FMT_48K_16B_MONO;
    Context->PcmData = mBootChimePcm;
    Context->PcmFrames = HDAU_CHIME_PCM_FRAMES;
  } else if (mHdaCfg.PcmFormatFallback && Rate44) {
    Context->SampleRate = 44100;
    Context->StreamFormat = Stereo ? HDA_FMT_44K_16B_STEREO : HDA_FMT_44K_16B_MONO;
    Context->PcmData = mBootChimePcm44;
    Context->PcmFrames = HDAU_CHIME_PCM44_FRAMES;
  } else {
    return EFI_UNSUPPORTED;
  }
  if (Context->Channels == 0 || Context->PcmFrames > (MAX_UINTN / Context->Channels)) {
    return EFI_BAD_BUFFER_SIZE;
  }
  Context->PcmSamples = Context->PcmFrames * Context->Channels;
  if (Context->PcmSamples > (MAX_UINTN / sizeof(INT16))) return EFI_BAD_BUFFER_SIZE;
  Context->PcmBytes = Context->PcmSamples * sizeof(INT16);
  Context->BytesPerSecond = (UINT64)Context->SampleRate * Context->Channels * sizeof(INT16);
  mHdaActiveStreamFormat = Context->StreamFormat;
  HdaLog("[HDAU] PCM format selected: dac=0x%x rate=%u channels=%u fmt=0x%x frames=%u bytes=%u fallback=%u",
         Context->Dac, Context->SampleRate, Context->Channels, Context->StreamFormat,
         (UINT32)Context->PcmFrames, (UINT32)Context->PcmBytes,
         Context->SampleRate == 44100 || Context->Channels == 1 ? 1 : 0);
  return EFI_SUCCESS;
}

STATIC UINTN HdaCollectAudioOutputDacs(
  EFI_PCI_IO_PROTOCOL *PciIo,
  UINT8 Codec,
  UINT8 Afg,
  UINT8 *Dacs,
  UINTN DacCapacity
) {
  UINT8 WStart;
  UINT8 WCount;
  UINTN WIndex;
  UINTN DacCount;

  if (PciIo == NULL || Dacs == NULL || DacCapacity == 0 || Afg == 0) return 0;
  if (!HdaGetSubnodes(PciIo, Codec, Afg, &WStart, &WCount)) return 0;

  DacCount = 0;
  for (WIndex = 0; WIndex < WCount && DacCount < DacCapacity; WIndex++) {
    UINT8 Nid = (UINT8)(WStart + WIndex);
    if (!HdaIsAudioOutputDac(PciIo, Codec, Afg, Nid)) continue;
    if (mHdaCfg.ForceDac != HDAU_FORCE_NID_NONE && Nid != (UINT8)mHdaCfg.ForceDac) continue;
    if (!HdaU8ArrayContains(Dacs, DacCount, Nid)) {
      Dacs[DacCount++] = Nid;
    }
  }

  return DacCount;
}

STATIC UINTN HdaBuildAlcIdDacOrder(
  CONST HDAU_ALCID_PROFILE *Profile,
  UINTN PairIndex,
  CONST UINT8 *RuntimeDacs,
  UINTN RuntimeDacCount,
  UINT8 *Order,
  UINTN OrderCapacity
) {
  UINTN OutCount;
  UINTN Index;
  UINT8 HintDac;

  if (Order == NULL || OrderCapacity == 0) return 0;
  OutCount = 0;
  HintDac = 0;

  if (Profile != NULL && PairIndex < HDAU_ALCID_PROFILE_PAIR_COUNT) {
    HintDac = Profile->Dacs[PairIndex];
  }

  if (HintDac != 0 && RuntimeDacs != NULL && HdaU8ArrayContains(RuntimeDacs, RuntimeDacCount, HintDac)) {
    Order[OutCount++] = HintDac;
  }

  for (Index = 0; RuntimeDacs != NULL && Index < RuntimeDacCount && OutCount < OrderCapacity; Index++) {
    UINT8 Dac = RuntimeDacs[Index];
    if (Dac == 0 || HdaU8ArrayContains(Order, OutCount, Dac)) continue;
    Order[OutCount++] = Dac;
  }

  return OutCount;
}

STATIC BOOLEAN HdaLegacyRealtekFanoutAllowed(VOID) {
  if (!mHdaCfg.LegacyRealtekFanout && !mHdaCfg.MobileRealtekFanout && !mHdaCfg.BroadRealtekFanout) return FALSE;
  if (mHdaActiveAlcIdProfile != NULL) return FALSE;
  if (mHdaCfg.ForcePin != HDAU_FORCE_NID_NONE) return FALSE;
  if (mHdaActivePolicy == NULL || (mHdaActivePolicy->Flags & HDAU_CPF_REALTEK) == 0) return FALSE;
  return TRUE;
}

STATIC CONST CHAR8 *HdaRealtekFanoutReason(VOID) {
  if (mHdaCfg.BroadRealtekFanout) return "broad-realtek-fanout";
  if (mHdaCfg.MobileRealtekFanout) return "mobile-realtek-fanout";
  return "legacy-realtek-fanout";
}

STATIC BOOLEAN HdaRealtekLegacyPinDriveAllowed(VOID) {
  if (!mHdaCfg.RealtekLegacyPinDrive) return FALSE;
  if (mHdaActivePolicy == NULL || (mHdaActivePolicy->Flags & HDAU_CPF_REALTEK) == 0) return FALSE;
  return TRUE;
}




STATIC UINT8 HdaComputeOutputPinCtl(
  UINT8 Pin,
  UINT32 PinCap,
  UINT32 Config
) {
  UINT8 Device;
  UINT8 PinCtl;

  Device = HdaDefaultDevice(Config);
  PinCtl = HDA_PIN_CTL_OUT_ENABLE;

  if (!mHdaCfg.RealtekLineOutPinctl) {
    if ((PinCap & HDA_PIN_CAP_HEADPHONE) != 0) {
      PinCtl |= HDA_PIN_CTL_HEADPHONE_ENABLE;
    }
    return PinCtl;
  }

  if (Device == HDA_DEFAULT_DEVICE_HP_OUT ||
      (Device != HDA_DEFAULT_DEVICE_LINE_OUT &&
       Device != HDA_DEFAULT_DEVICE_SPEAKER &&
       (PinCap & HDA_PIN_CAP_HEADPHONE) != 0)) {
    PinCtl |= HDA_PIN_CTL_HEADPHONE_ENABLE;
  }

  if ((PinCtl & HDA_PIN_CTL_HEADPHONE_ENABLE) == 0 &&
      (PinCap & HDA_PIN_CAP_HEADPHONE) != 0 &&
      HdaRealtekLegacyPinDriveAllowed()) {
    PinCtl |= HDA_PIN_CTL_HEADPHONE_ENABLE;
    if (mHdaCfg.Debug) {
      HdaLog("[HDAU] realtek legacy pin-drive: controller=%04x:%04x pin=0x%x device=0x%x pinctl=0x%x",
             mHdaActiveControllerVendorId, mHdaActiveControllerDeviceId, Pin, Device, PinCtl);
    }
  }

  return PinCtl;
}

STATIC VOID HdaLogSelectedPathState(HDAU_CONTEXT *Context, CONST CHAR8 *Reason) {
  UINT32 Value;
  UINTN Index;

  if (Context == NULL || Context->PciIo == NULL || !mHdaCfg.Debug) return;

  Value = 0;
  (VOID)HdaImmediateCommand(Context->PciIo, Context->Codec, Context->Pin,
                            HDA_VERB12(HDA_VERB_GET_PIN_WIDGET_CONTROL, 0), &Value);
  HdaLog("[HDAU] state: pin=0x%x pinctl=0x%x reason=%a",
         Context->Pin, Value & 0xFF, Reason == NULL ? "state" : Reason);

  Value = 0;
  (VOID)HdaImmediateCommand(Context->PciIo, Context->Codec, Context->Pin,
                            HDA_VERB12(HDA_VERB_GET_EAPD_BTL, 0), &Value);
  HdaLog("[HDAU] state: pin=0x%x eapd=0x%x reason=%a",
         Context->Pin, Value & 0xFF, Reason == NULL ? "state" : Reason);

  for (Index = 0; Index + 1 < Context->PathLen; Index++) {
    UINT8 Nid = Context->Path[Index];
    UINT32 Caps = HdaGetParam(Context->PciIo, Context->Codec, Nid, HDA_PARAM_AUDIO_WIDGET_CAP);
    UINT8 Type = HdaWidgetType(Caps);

    if (Type == HDA_WIDGET_TYPE_AUDIO_MIXER ||
        Type == HDA_WIDGET_TYPE_AUDIO_SELECTOR ||
        Type == HDA_WIDGET_TYPE_PIN_COMPLEX) {
      Value = 0;
      (VOID)HdaImmediateCommand(Context->PciIo, Context->Codec, Nid,
                                HDA_VERB12(HDA_VERB_GET_CONNECTION_SELECT, 0), &Value);
      HdaLog("[HDAU] state: nid=0x%x conn-select=0x%x next=0x%x reason=%a",
             Nid, Value & 0xFF, Context->Path[Index + 1], Reason == NULL ? "state" : Reason);
    }
  }

  Value = 0;
  (VOID)HdaImmediateCommand(Context->PciIo, Context->Codec, Context->Dac,
                            HDA_VERB4(HDA_VERB_GET_CONVERTER_FORMAT, 0), &Value);
  HdaLog("[HDAU] state: dac=0x%x fmt=0x%x reason=%a",
         Context->Dac, Value & 0xFFFF, Reason == NULL ? "state" : Reason);

  Value = 0;
  (VOID)HdaImmediateCommand(Context->PciIo, Context->Codec, Context->Dac,
                            HDA_VERB12(HDA_VERB_GET_CONVERTER_STREAM_CHANNEL, 0), &Value);
  HdaLog("[HDAU] state: dac=0x%x stream-channel=0x%x reason=%a",
         Context->Dac, Value & 0xFF, Reason == NULL ? "state" : Reason);
}

STATIC EFI_STATUS HdaProgramOutputPin(
  EFI_PCI_IO_PROTOCOL *PciIo,
  UINT8 Codec,
  UINT8 Pin,
  CONST CHAR8 *Reason
) {
  UINT32 PinCap;
  UINT32 Config;
  UINT32 OriginalEapd;
  UINT32 VerifyEapd;
  UINT8 DesiredEapd;
  UINT8 PinCtl;
  EFI_STATUS Status;
  EFI_STATUS VerifyStatus;
  BOOLEAN HaveOriginalEapd;
  BOOLEAN EapdVerified;

  if (PciIo == NULL || Pin == 0) return EFI_INVALID_PARAMETER;
  if (!HdaIsOutputPin(PciIo, Codec, Pin)) return EFI_UNSUPPORTED;

  Status = HdaSetWidgetPowerD0(PciIo, Codec, Pin);
  if (EFI_ERROR(Status)) return Status;
  Status = HdaGetParamChecked(PciIo, Codec, Pin, HDA_PARAM_PIN_CAP, &PinCap);
  if (EFI_ERROR(Status)) return Status;
  Config = 0;
  Status = HdaImmediateCommand(PciIo, Codec, Pin,
                               HDA_VERB12(HDA_VERB_GET_CONFIG_DEFAULT, 0), &Config);
  if (EFI_ERROR(Status)) return Status;
  PinCtl = HdaComputeOutputPinCtl(Pin, PinCap, Config);

  HdaLog("[HDAU] pin: nid=0x%x pincap=0x%x config=0x%x pinctl=0x%x reason=%a",
         Pin, PinCap, Config, PinCtl, Reason == NULL ? "output" : Reason);
  Status = HdaSetVerb(PciIo, Codec, Pin,
                      HDA_VERB12(HDA_VERB_SET_PIN_WIDGET_CONTROL, PinCtl));
  if (EFI_ERROR(Status)) return Status;

  if ((PinCap & HDA_PIN_CAP_EAPD) != 0) {
    OriginalEapd = 0;
    VerifyEapd = 0;
    DesiredEapd = 0x02U;
    HaveOriginalEapd = FALSE;

    VerifyStatus = HdaImmediateCommand(PciIo, Codec, Pin,
                                       HDA_VERB12(HDA_VERB_GET_EAPD_BTL, 0),
                                       &OriginalEapd);
    if (!EFI_ERROR(VerifyStatus)) {
      HaveOriginalEapd = TRUE;
      DesiredEapd = (UINT8)(OriginalEapd | 0x02U);
    } else {
      HdaLog("[HDAU] eapd read unavailable: nid=0x%x status=%r; using direct value 0x02 reason=%a",
             Pin, VerifyStatus, Reason == NULL ? "output" : Reason);
    }

    Status = HdaSetVerb(PciIo, Codec, Pin,
                        HDA_VERB12(HDA_VERB_SET_EAPD_BTL, DesiredEapd));
    if (EFI_ERROR(Status) && DesiredEapd != 0x02U) {
      HdaLog("[HDAU] eapd merged write failed: nid=0x%x value=0x%x status=%r; retrying 0x02",
             Pin, DesiredEapd, Status);
      Status = HdaSetVerb(PciIo, Codec, Pin,
                          HDA_VERB12(HDA_VERB_SET_EAPD_BTL, 0x02U));
      DesiredEapd = 0x02U;
    }
    if (EFI_ERROR(Status)) return Status;

    VerifyStatus = HdaImmediateCommand(PciIo, Codec, Pin,
                                       HDA_VERB12(HDA_VERB_GET_EAPD_BTL, 0),
                                       &VerifyEapd);
    if (!EFI_ERROR(VerifyStatus) && (VerifyEapd & 0x02U) != 0) {
      EapdVerified = TRUE;
    } else {
      HdaLog("[HDAU] eapd readback advisory: nid=0x%x desired=0x%x verify=0x%x status=%r; retrying direct 0x02",
             Pin, DesiredEapd, VerifyEapd, VerifyStatus);
      Status = HdaSetVerb(PciIo, Codec, Pin,
                          HDA_VERB12(HDA_VERB_SET_EAPD_BTL, 0x02U));
      if (EFI_ERROR(Status)) return Status;
      VerifyEapd = 0;
      VerifyStatus = HdaImmediateCommand(PciIo, Codec, Pin,
                                         HDA_VERB12(HDA_VERB_GET_EAPD_BTL, 0),
                                         &VerifyEapd);
      EapdVerified = !EFI_ERROR(VerifyStatus) && ((VerifyEapd & 0x02U) != 0);
      if (!EapdVerified) {
        /* Some otherwise functional codecs do not expose dependable EAPD
           readback in preboot.  A successful SET remains authoritative, as
           in the v1.0.31 desktop-compatible path. */
        HdaLog("[HDAU] eapd readback still unavailable/mismatched: nid=0x%x verify=0x%x status=%r; accepting successful write",
               Pin, VerifyEapd, VerifyStatus);
      }
    }

    HdaLog("[HDAU] eapd: nid=0x%x original=%a0x%x value=0x%x verified=%u reason=%a",
           Pin, HaveOriginalEapd ? "" : "unknown/", OriginalEapd,
           EapdVerified ? VerifyEapd : DesiredEapd, EapdVerified ? 1 : 0,
           Reason == NULL ? "output" : Reason);
    if (!HaveOriginalEapd || (OriginalEapd & 0x02U) == 0) {
      HdaStall(HDAU_EAPD_STABILIZE_US);
    }
  }
  return EFI_SUCCESS;
}


STATIC EFI_STATUS HdaProgramOutputDacStream(
  EFI_PCI_IO_PROTOCOL *PciIo,
  UINT8 Codec,
  UINT8 Afg,
  UINT8 Dac,
  CONST CHAR8 *Reason
) {
  EFI_STATUS Status;
  if (PciIo == NULL || Dac == 0) return EFI_INVALID_PARAMETER;
  if (!HdaIsAudioOutputDac(PciIo, Codec, Afg, Dac)) {
    if (mHdaCfg.Debug) {
      UINT32 Caps = HdaGetParam(PciIo, Codec, Dac, HDA_PARAM_AUDIO_WIDGET_CAP);
      HdaLog("[HDAU] converter skip: dac=0x%x is not Audio Output wcaps=0x%x reason=%a",
             Dac, Caps, Reason == NULL ? "output" : Reason);
    }
    return EFI_UNSUPPORTED;
  }
  Status = HdaSetWidgetPowerD0(PciIo, Codec, Dac);
  if (EFI_ERROR(Status)) return Status;
  Status = HdaProgramAmpSafeUnity(PciIo, Codec, Afg, Dac, Reason);
  if (EFI_ERROR(Status)) return Status;
  Status = HdaSetVerb(PciIo, Codec, Dac, HDA_VERB4(HDA_VERB_SET_CONVERTER_FORMAT, mHdaActiveStreamFormat));
  if (EFI_ERROR(Status)) return Status;
  Status = HdaSetVerb(PciIo, Codec, Dac, HDA_VERB12(HDA_VERB_SET_CONVERTER_STREAM_CHANNEL, (mHdaActiveStreamTag << 4)));
  if (EFI_ERROR(Status)) return Status;
  if (mHdaCfg.Debug) {
    HdaLog("[HDAU] converter: dac=0x%x fmt=0x%x streamTag=%u channel=0 reason=%a",
           Dac, mHdaActiveStreamFormat, mHdaActiveStreamTag, Reason == NULL ? "output" : Reason);
  }
  return EFI_SUCCESS;
}


STATIC EFI_STATUS HdaProgramAnalogOutputPath(
  EFI_PCI_IO_PROTOCOL *PciIo,
  UINT8 Codec,
  UINT8 Afg,
  UINT8 Pin,
  UINT8 Dac,
  CONST UINT8 *Path,
  UINTN PathLen,
  CONST CHAR8 *Reason
) {
  UINTN Index;
  EFI_STATUS Status;
  UINT32 PinCap;
  UINT32 Config;
  BOOLEAN DigitalPin;

  if (PciIo == NULL || Path == NULL || PathLen == 0 || Pin == 0 || Dac == 0) return EFI_INVALID_PARAMETER;

  PinCap = 0;
  Config = 0;
  Status = HdaGetParamChecked(PciIo, Codec, Pin, HDA_PARAM_PIN_CAP, &PinCap);
  if (EFI_ERROR(Status)) return Status;
  Status = HdaImmediateCommand(PciIo, Codec, Pin,
                               HDA_VERB12(HDA_VERB_GET_CONFIG_DEFAULT, 0), &Config);
  if (EFI_ERROR(Status)) return Status;
  DigitalPin = HdaPinDefaultIsDigital(Config) ||
               ((PinCap & (HDA_PIN_CAP_HDMI | HDA_PIN_CAP_DISPLAYPORT)) != 0);
  if (DigitalPin && !mHdaCfg.AllowDigitalPins) return EFI_UNSUPPORTED;

  for (Index = 0; Index < PathLen; Index++) {
    UINT8 Nid = Path[Index];
    Status = HdaSetWidgetPowerD0(PciIo, Codec, Nid);
    if (Status != EFI_SUCCESS) goto FanoutMutationFailure;
    Status = HdaProgramAmpSafeUnity(PciIo, Codec, Afg, Nid, Reason);
    if (Status != EFI_SUCCESS) goto FanoutMutationFailure;
  }

  Status = HdaProgramConnectionPath(PciIo, Codec, Afg, Path, PathLen, Reason);
  if (Status != EFI_SUCCESS) goto FanoutMutationFailure;
  Status = HdaProgramOutputPin(PciIo, Codec, Pin, Reason);
  if (Status != EFI_SUCCESS) goto FanoutMutationFailure;
  Status = HdaProgramOutputDacStream(PciIo, Codec, Afg, Dac, Reason);
  if (Status != EFI_SUCCESS) goto FanoutMutationFailure;
  Status = HdaLimitFanoutHardwareVolume(PciIo, Codec, Afg, Dac, Path, PathLen, Reason);
  if (Status == EFI_UNSUPPORTED) {
    HdaLog("[HDAU] fanout volume: codec cannot provide 30%% hardware attenuation; PCM remains 30%% pin=0x%x dac=0x%x",
           Pin, Dac);
    return EFI_SUCCESS;
  }
  if (Status != EFI_SUCCESS) goto FanoutMutationFailure;
  return EFI_SUCCESS;

FanoutMutationFailure:
  /* The fanout pin, mixer or DAC may already have been mutated, even if
     this attempt is discarded by its void caller. Never start DMA while a
     partially programmed extra analog output could bypass its gain limit.
     The caller aborts playback and restores the transactional snapshot. */
  mHdaVolumeReadbackMismatch = TRUE;
  HdaLog("[HDAU] fanout safety: partial analog route pin=0x%x dac=0x%x status=%r; blocking playback",
         Pin, Dac, Status);
  return HdaNormalizeExactSuccess(Status);
}







STATIC VOID HdaRealtekGpioAmpInit(HDAU_CONTEXT *Context) {
  if (Context == NULL || !mHdaCfg.RealtekGpioAmp) return;
  if (mHdaActivePolicy == NULL || (mHdaActivePolicy->Flags & HDAU_CPF_REALTEK) == 0) return;

  /* No generic GPIO polarity sweep is safe across Realtek codecs or boards.
     Exact GPIO verbs must come from a verified codec/layout profile. The
     current generated profile table has no such sequence, so preserve state. */
  HdaLog("[HDAU] realtek gpio amp: skipped; no exact AppleALC/AppleHDA profile-backed GPIO sequence");
}
















STATIC BOOLEAN HdaPathContainsPinIntermediate(
  EFI_PCI_IO_PROTOCOL *PciIo,
  UINT8 Codec,
  CONST UINT8 *Path,
  UINTN PathLen
) {
  UINTN Index;

  if (PciIo == NULL || Path == NULL || PathLen < 3) return FALSE;

  for (Index = 1; Index + 1 < PathLen; Index++) {
    UINT32 Caps;
    EFI_STATUS Status;

    Status = HdaGetParamChecked(PciIo, Codec, Path[Index], HDA_PARAM_AUDIO_WIDGET_CAP, &Caps);
    if (EFI_ERROR(Status)) return TRUE;
    if (HdaWidgetType(Caps) == HDA_WIDGET_TYPE_PIN_COMPLEX) {
      return TRUE;
    }
  }
  return FALSE;
}

STATIC VOID HdaResetFanoutSelectorClaims(
  EFI_PCI_IO_PROTOCOL *PciIo,
  UINT8 Codec,
  CONST UINT8 *PrimaryPath,
  UINTN PrimaryPathLen
) {
  UINTN Index;

  for (Index = 0; Index < ARRAY_SIZE(mHdaFanoutSelectorNext); Index++) {
    mHdaFanoutSelectorNext[Index] = HDAU_SELECTOR_CLAIM_NONE;
  }
  mHdaFanoutSelectorClaimsValid = FALSE;
  if (PciIo == NULL || PrimaryPath == NULL || PrimaryPathLen < 2) return;

  for (Index = 0; Index + 1 < PrimaryPathLen; Index++) {
    UINT8 Nid = PrimaryPath[Index];
    UINT8 Type;
    UINT32 Caps;
    EFI_STATUS Status;
    if (Nid > HDAU_MAX_NID || PrimaryPath[Index + 1] > HDAU_MAX_NID) return;
    Status = HdaGetParamChecked(PciIo, Codec, Nid, HDA_PARAM_AUDIO_WIDGET_CAP, &Caps);
    if (EFI_ERROR(Status)) {
      mHdaFanoutSelectorClaimsValid = FALSE;
      return;
    }
    Type = HdaWidgetType(Caps);
    if (Type == HDA_WIDGET_TYPE_AUDIO_SELECTOR || Type == HDA_WIDGET_TYPE_PIN_COMPLEX) {
      mHdaFanoutSelectorNext[Nid] = PrimaryPath[Index + 1];
    }
  }
  mHdaFanoutSelectorClaimsValid = TRUE;
}

STATIC BOOLEAN HdaPathsSelectorCompatible(
  EFI_PCI_IO_PROTOCOL *PciIo,
  UINT8 Codec,
  CONST UINT8 *PrimaryPath,
  UINTN PrimaryPathLen,
  CONST UINT8 *CandidatePath,
  UINTN CandidatePathLen
) {
  UINTN CandidateIndex;

  if (PciIo == NULL || PrimaryPath == NULL || CandidatePath == NULL ||
      PrimaryPathLen < 2 || CandidatePathLen < 2) return FALSE;
  if (!mHdaFanoutSelectorClaimsValid) {
    HdaResetFanoutSelectorClaims(PciIo, Codec, PrimaryPath, PrimaryPathLen);
  }
  if (!mHdaFanoutSelectorClaimsValid) return FALSE;

  for (CandidateIndex = 0; CandidateIndex + 1 < CandidatePathLen; CandidateIndex++) {
    UINT8 Nid = CandidatePath[CandidateIndex];
    UINT8 Type;
    UINT16 ClaimedNext;
    UINT32 Caps;
    EFI_STATUS Status;

    if (Nid > HDAU_MAX_NID || CandidatePath[CandidateIndex + 1] > HDAU_MAX_NID) return FALSE;
    Status = HdaGetParamChecked(PciIo, Codec, Nid, HDA_PARAM_AUDIO_WIDGET_CAP, &Caps);
    if (EFI_ERROR(Status)) return FALSE;
    Type = HdaWidgetType(Caps);
    if (Type != HDA_WIDGET_TYPE_AUDIO_SELECTOR && Type != HDA_WIDGET_TYPE_PIN_COMPLEX) continue;
    ClaimedNext = mHdaFanoutSelectorNext[Nid];
    if (ClaimedNext != HDAU_SELECTOR_CLAIM_NONE &&
        ClaimedNext != CandidatePath[CandidateIndex + 1]) {
      if (mHdaCfg.Debug) {
        HdaLog("[HDAU] fanout rejected: shared selector nid=0x%x claimed-next=0x%x candidate-next=0x%x",
               Nid, ClaimedNext, CandidatePath[CandidateIndex + 1]);
      }
      return FALSE;
    }
  }
  return TRUE;
}

STATIC VOID HdaCommitFanoutSelectorClaims(
  EFI_PCI_IO_PROTOCOL *PciIo,
  UINT8 Codec,
  CONST UINT8 *Path,
  UINTN PathLen
) {
  UINTN Index;

  if (!mHdaFanoutSelectorClaimsValid || PciIo == NULL || Path == NULL || PathLen < 2) return;
  for (Index = 0; Index + 1 < PathLen; Index++) {
    UINT8 Nid = Path[Index];
    UINT8 Type;
    UINT32 Caps;
    EFI_STATUS Status;
    if (Nid > HDAU_MAX_NID || Path[Index + 1] > HDAU_MAX_NID) {
      mHdaFanoutSelectorClaimsValid = FALSE;
      return;
    }
    Status = HdaGetParamChecked(PciIo, Codec, Nid, HDA_PARAM_AUDIO_WIDGET_CAP, &Caps);
    if (EFI_ERROR(Status)) {
      mHdaFanoutSelectorClaimsValid = FALSE;
      return;
    }
    Type = HdaWidgetType(Caps);
    if (Type == HDA_WIDGET_TYPE_AUDIO_SELECTOR || Type == HDA_WIDGET_TYPE_PIN_COMPLEX) {
      mHdaFanoutSelectorNext[Nid] = Path[Index + 1];
    }
  }
}

STATIC UINTN HdaRealtekBuildDacPreferenceOrder(
  UINT8 Pin,
  CONST UINT8 *AvailableDacs,
  UINTN AvailableDacCount,
  UINT8 *Order,
  UINTN OrderCapacity
) {
  UINTN Index;
  UINTN OutCount;

  (VOID)Pin;
  if (AvailableDacs == NULL || Order == NULL || OrderCapacity == 0) return 0;
  OutCount = 0;
  for (Index = 0; Index < AvailableDacCount && OutCount < OrderCapacity; Index++) {
    UINTN Existing;
    UINT8 Dac = AvailableDacs[Index];
    BOOLEAN Duplicate = FALSE;
    if (Dac == 0) continue;
    for (Existing = 0; Existing < OutCount; Existing++) {
      if (Order[Existing] == Dac) {
        Duplicate = TRUE;
        break;
      }
    }
    if (!Duplicate) Order[OutCount++] = Dac;
  }
  return OutCount;
}


STATIC BOOLEAN HdaRealtekProgramPinDacProfile(
  HDAU_CONTEXT *Context,
  UINT8 Pin,
  UINT8 Dac,
  CONST CHAR8 *Reason
) {
  EFI_PCI_IO_PROTOCOL *PciIo;
  UINT8 Codec;
  UINT8 Afg;
  UINT8 Path[HDAU_MAX_PATH_DEPTH];
  UINTN PathLen;
  UINT32 Config;

  if (Context == NULL || Context->PciIo == NULL || Pin == 0 || Dac == 0) return FALSE;
  PciIo = Context->PciIo;
  Codec = Context->Codec;
  Afg = Context->Afg;
  if (!HdaIsAudioOutputDac(PciIo, Codec, Afg, Dac)) {
    if (mHdaCfg.Debug) {
      UINT32 DacCaps = HdaGetParam(PciIo, Codec, Dac, HDA_PARAM_AUDIO_WIDGET_CAP);
      HdaLog("[HDAU] realtek profile skip: pin=0x%x dac=0x%x target is not Audio Output wcaps=0x%x reason=%a",
             Pin, Dac, DacCaps, Reason == NULL ? "profile" : Reason);
    }
    return FALSE;
  }

  if (!HdaIsOutputPin(PciIo, Codec, Pin)) return FALSE;

  Config = 0;
  if (EFI_ERROR(HdaImmediateCommand(PciIo, Codec, Pin,
                                    HDA_VERB12(HDA_VERB_GET_CONFIG_DEFAULT, 0), &Config))) {
    return FALSE;
  }
  if (HdaPinDefaultIsDigital(Config) && !mHdaCfg.AllowDigitalPins) return FALSE;

  PathLen = 0;
  if (!HdaPathSearch(PciIo, Codec, Pin, Dac, Path, &PathLen) ||
      !HdaPathInputAmpsProgrammable(PciIo, Codec, Afg, Path, PathLen)) return FALSE;
  if (HdaPathContainsPinIntermediate(PciIo, Codec, Path, PathLen)) {
    if (mHdaCfg.Debug) {
      HdaLog("[HDAU] realtek profile skip: pin=0x%x dac=0x%x has pin-intermediate pathLen=%u reason=%a",
             Pin, Dac, (UINT32)PathLen, Reason == NULL ? "profile" : Reason);
    }
    return FALSE;
  }
  if (!HdaPathsSelectorCompatible(PciIo, Codec, Context->Path, Context->PathLen, Path, PathLen)) {
    return FALSE;
  }

  HdaLog("[HDAU] realtek profile: pin=0x%x dac=0x%x pathLen=%u config=0x%x reason=%a",
         Pin, Dac, (UINT32)PathLen, Config, Reason == NULL ? "profile" : Reason);
  if (mHdaCfg.TransactionalRestore &&
      EFI_ERROR(HdaSnapshotPathNodes(Context, Path, PathLen, Pin, Dac))) {
    HdaLog("[HDAU] realtek profile skipped because its complete rollback state could not be captured: pin=0x%x dac=0x%x", Pin, Dac);
    return FALSE;
  }
  if (EFI_ERROR(HdaProgramAnalogOutputPath(PciIo, Codec, Afg, Pin, Dac, Path, PathLen, Reason))) {
    return FALSE;
  }
  HdaCommitFanoutSelectorClaims(PciIo, Codec, Path, PathLen);
  return TRUE;
}

STATIC VOID HdaRealtekCodecWarmReset(HDAU_CONTEXT *Context) {
  UINT8 WStart;
  UINT8 WCount;
  UINTN WIndex;

  if (Context == NULL || !mHdaCfg.RealtekCodecWarmReset) return;
  if (mHdaActivePolicy == NULL || (mHdaActivePolicy->Flags & HDAU_CPF_REALTEK) == 0) return;
  if (Context->PciIo == NULL || Context->Afg == 0) return;

  HdaLog("[HDAU] realtek warm init: afg=0x%x codec=0x%x", Context->Afg, Context->Codec);
  HdaSetVerb(Context->PciIo, Context->Codec, Context->Afg, HDA_VERB12(HDA_VERB_SET_POWER_STATE, 0x00));

  if (!HdaGetSubnodes(Context->PciIo, Context->Codec, Context->Afg, &WStart, &WCount)) return;
  for (WIndex = 0; WIndex < WCount; WIndex++) {
    UINT8 Nid = (UINT8)(WStart + WIndex);
    UINT32 Caps = HdaGetParam(Context->PciIo, Context->Codec, Nid, HDA_PARAM_AUDIO_WIDGET_CAP);
    UINT8 Type = HdaWidgetType(Caps);
    if (Type == HDA_WIDGET_TYPE_AUDIO_OUT ||
        Type == HDA_WIDGET_TYPE_AUDIO_MIXER ||
        Type == HDA_WIDGET_TYPE_AUDIO_SELECTOR ||
        Type == HDA_WIDGET_TYPE_PIN_COMPLEX) {
      (VOID)HdaSetWidgetPowerD0(Context->PciIo, Context->Codec, Nid);
    }
  }
}

STATIC VOID HdaAlcIdLayoutSafeFanout(HDAU_CONTEXT *Context) {
  EFI_PCI_IO_PROTOCOL *PciIo;
  UINT8 Codec;
  CONST HDAU_ALCID_PROFILE *Profile;
  UINT8 RuntimeDacs[HDAU_MAX_WIDGETS];
  UINTN RuntimeDacCount;
  UINTN PairIndex;
  UINTN EnabledCount;

  if (Context == NULL || Context->PciIo == NULL || Context->Afg == 0) return;
  Profile = mHdaActiveAlcIdProfile;
  if (Profile == NULL) return;
  if (mHdaCfg.ForcePin != HDAU_FORCE_NID_NONE) return;

  PciIo = Context->PciIo;
  Codec = Context->Codec;
  ZeroMem(RuntimeDacs, sizeof(RuntimeDacs));
  RuntimeDacCount = HdaCollectAudioOutputDacs(PciIo, Codec, Context->Afg, RuntimeDacs, ARRAY_SIZE(RuntimeDacs));
  if (RuntimeDacCount == 0) return;

  EnabledCount = 0;
  mHdaAlcIdFanoutCount = 0;
  ZeroMem(mHdaAlcIdFanoutPins, sizeof(mHdaAlcIdFanoutPins));
  ZeroMem(mHdaAlcIdFanoutDacs, sizeof(mHdaAlcIdFanoutDacs));

  /*
     Explicit alcid means the user selected an AppleALC layout, but in DXE we
     cannot know which physical jack is actually connected to speakers before
     macOS/AppleHDA is alive.  The previous builds obeyed alcid but only armed
     the first verified path.  On legacy desktop codecs this can make DMA move
     perfectly while the audible jack is another valid output in the same
     AppleALC layout.  Keep the selected path authoritative, but also arm the
     other analog output pins from the same layout after validating every node
     against the live codec graph.  This is dynamic for all AppleALC layouts:
     no H81-only, no ALC887-only and no fixed alcid=5 special case.
  */
  for (PairIndex = 0; PairIndex < HDAU_ALCID_PROFILE_PAIR_COUNT; PairIndex++) {
    UINT8 Pin = Profile->Pins[PairIndex];
    UINT32 PinCap;
    UINT32 Config;
    UINT8 OrderedDacs[HDAU_MAX_WIDGETS];
    UINTN OrderedCount;
    UINTN DacIndex;

    if (Pin == 0 || Pin == Context->Pin) continue;

    PinCap = HdaGetParam(PciIo, Codec, Pin, HDA_PARAM_PIN_CAP);
    if (!HdaIsOutputPin(PciIo, Codec, Pin)) {
      if (mHdaCfg.Debug) {
        HdaLog("[HDAU] alcid fanout skip: alcid=%u profile=%a pin=0x%x not-output-pin pincap=0x%x",
               (UINT32)Profile->LayoutId, Profile->Name, Pin, PinCap);
      }
      continue;
    }

    Config = 0;
    if (EFI_ERROR(HdaImmediateCommand(PciIo, Codec, Pin,
                                      HDA_VERB12(HDA_VERB_GET_CONFIG_DEFAULT, 0), &Config))) {
      if (mHdaCfg.Debug) {
        HdaLog("[HDAU] alcid fanout skip: alcid=%u profile=%a pin=0x%x ConfigDefault read failed",
               (UINT32)Profile->LayoutId, Profile->Name, Pin);
      }
      continue;
    }
    if (!HdaPinDefaultLooksConnected(Config)) {
      if (mHdaCfg.Debug) {
        HdaLog("[HDAU] alcid fanout skip: alcid=%u profile=%a pin=0x%x disabled/no-connect config=0x%x",
               (UINT32)Profile->LayoutId, Profile->Name, Pin, Config);
      }
      continue;
    }
    if (HdaPinDefaultIsDigital(Config) && !mHdaCfg.AllowDigitalPins) {
      if (mHdaCfg.Debug) {
        HdaLog("[HDAU] alcid fanout skip: alcid=%u profile=%a pin=0x%x digital config=0x%x",
               (UINT32)Profile->LayoutId, Profile->Name, Pin, Config);
      }
      continue;
    }

    ZeroMem(OrderedDacs, sizeof(OrderedDacs));
    OrderedCount = HdaBuildAlcIdDacOrder(Profile, PairIndex, RuntimeDacs, RuntimeDacCount, OrderedDacs, ARRAY_SIZE(OrderedDacs));
    if (Context->Dac != 0 && !HdaU8ArrayContains(OrderedDacs, OrderedCount, Context->Dac) && OrderedCount < ARRAY_SIZE(OrderedDacs)) {
      OrderedDacs[OrderedCount++] = Context->Dac;
    }

    for (DacIndex = 0; DacIndex < OrderedCount; DacIndex++) {
      UINT8 Dac = OrderedDacs[DacIndex];
      UINT8 Path[HDAU_MAX_PATH_DEPTH];
      UINTN PathLen;

      if (Dac == 0 || !HdaIsAudioOutputDac(PciIo, Codec, Context->Afg, Dac)) continue;
      PathLen = 0;
      if (!HdaPathSearch(PciIo, Codec, Pin, Dac, Path, &PathLen) ||
          !HdaPathInputAmpsProgrammable(PciIo, Codec, Context->Afg, Path, PathLen)) continue;
      if (HdaPathContainsPinIntermediate(PciIo, Codec, Path, PathLen)) {
        if (mHdaCfg.Debug) {
          HdaLog("[HDAU] alcid fanout skip: alcid=%u profile=%a pin=0x%x dac=0x%x pin-intermediate pathLen=%u",
                 (UINT32)Profile->LayoutId, Profile->Name, Pin, Dac, (UINT32)PathLen);
        }
        continue;
      }
      if (!HdaPathsSelectorCompatible(PciIo, Codec, Context->Path, Context->PathLen, Path, PathLen)) {
        continue;
      }

      HdaLog("[HDAU] alcid fanout: codec=0x%08x alcid=%u profile=%a pin=0x%x dac=0x%x pathLen=%u config=0x%x",
             mHdaActiveCodecId, (UINT32)Profile->LayoutId, Profile->Name, Pin, Dac, (UINT32)PathLen, Config);
      if (mHdaCfg.TransactionalRestore &&
          EFI_ERROR(HdaSnapshotPathNodes(Context, Path, PathLen, Pin, Dac))) {
        HdaLog("[HDAU] alcid fanout skipped because rollback snapshot is incomplete: pin=0x%x dac=0x%x", Pin, Dac);
        continue;
      }
      if (EFI_ERROR(HdaProgramAnalogOutputPath(PciIo, Codec, Context->Afg, Pin, Dac, Path, PathLen, "alcid-layout-fanout"))) {
        HdaLog("[HDAU] alcid fanout programming failed: pin=0x%x dac=0x%x", Pin, Dac);
        continue;
      }
      HdaCommitFanoutSelectorClaims(PciIo, Codec, Path, PathLen);
      if (mHdaAlcIdFanoutCount < HDAU_MAX_ALCID_FANOUT_PATHS) {
        mHdaAlcIdFanoutPins[mHdaAlcIdFanoutCount] = Pin;
        mHdaAlcIdFanoutDacs[mHdaAlcIdFanoutCount] = Dac;
        mHdaAlcIdFanoutCount++;
      }
      EnabledCount++;
      break;
    }
  }

  if (mHdaCfg.Debug || EnabledCount > 0) {
    HdaLog("[HDAU] alcid fanout done: codec=0x%08x alcid=%u profile=%a extraPins=%u selectedPin=0x%x selectedDac=0x%x",
           mHdaActiveCodecId, (UINT32)Profile->LayoutId, Profile->Name, (UINT32)EnabledCount, Context->Pin, Context->Dac);
  }
}

STATIC VOID HdaRealtekAppleAlcComboInit(HDAU_CONTEXT *Context) {
  EFI_PCI_IO_PROTOCOL *PciIo;
  UINT8 Codec;
  UINT8 WStart;
  UINT8 WCount;
  UINT8 Dacs[HDAU_MAX_WIDGETS];
  UINTN DacCount;
  UINTN WIndex;
  UINTN EnabledCount;
  CONST CHAR8 *Reason;

  if (Context == NULL || !mHdaCfg.RealtekAppleAlcCombos) return;
  if (mHdaActivePolicy == NULL || (mHdaActivePolicy->Flags & HDAU_CPF_REALTEK) == 0) return;
  if ((mHdaActivePolicy->Flags & HDAU_CPF_SKIP_BY_DEFAULT) != 0) return;
  if (mHdaCfg.ForcePin != HDAU_FORCE_NID_NONE) return;

  PciIo = Context->PciIo;
  Codec = Context->Codec;
  if (PciIo == NULL || Context->Afg == 0) return;
  if (!HdaGetSubnodes(PciIo, Codec, Context->Afg, &WStart, &WCount)) return;

  if (mHdaActiveAlcIdProfile != NULL) {
    CONST HDAU_ALCID_PROFILE *Profile = mHdaActiveAlcIdProfile;
    if (mHdaCfg.Debug) {
      HdaLog("[HDAU] alcid layout active: codec=0x%08x alcid=%u profile=%a selectedPin=0x%x selectedDac=0x%x; runtime layout fanout remains authoritative",
             mHdaActiveCodecId, (UINT32)Profile->LayoutId, Profile->Name, Context->Pin, Context->Dac);
    }
    return;
  }

  ZeroMem(Dacs, sizeof(Dacs));
  DacCount = HdaCollectAudioOutputDacs(PciIo, Codec, Context->Afg, Dacs, ARRAY_SIZE(Dacs));
  if (DacCount == 0) return;

  EnabledCount = 0;
  Reason = "runtime-realtek-combo";
  for (WIndex = 0; WIndex < WCount; WIndex++) {
    UINT8 Pin = (UINT8)(WStart + WIndex);
    UINT32 Caps = HdaGetParam(PciIo, Codec, Pin, HDA_PARAM_AUDIO_WIDGET_CAP);
    UINT32 PinCap;
    UINT32 Config;
    UINT8 OrderedDacs[HDAU_MAX_WIDGETS];
    UINTN OrderedCount;
    UINTN DacIndex;

    if (HdaWidgetType(Caps) != HDA_WIDGET_TYPE_PIN_COMPLEX || Pin == Context->Pin) continue;
    PinCap = HdaGetParam(PciIo, Codec, Pin, HDA_PARAM_PIN_CAP);
    if ((PinCap & HDA_PIN_CAP_OUTPUT) == 0) continue;
    Config = 0;
    if (EFI_ERROR(HdaImmediateCommand(PciIo, Codec, Pin,
                                      HDA_VERB12(HDA_VERB_GET_CONFIG_DEFAULT, 0), &Config))) {
      if (mHdaCfg.Debug) {
        HdaLog("[HDAU] runtime Realtek combo skip: pin=0x%x ConfigDefault read failed", Pin);
      }
      continue;
    }
    if (!HdaPinDefaultLooksConnected(Config)) continue;
    if (HdaPinDefaultIsDigital(Config) && !mHdaCfg.AllowDigitalPins) continue;

    ZeroMem(OrderedDacs, sizeof(OrderedDacs));
    OrderedCount = HdaRealtekBuildDacPreferenceOrder(Pin, Dacs, DacCount, OrderedDacs, ARRAY_SIZE(OrderedDacs));
    for (DacIndex = 0; DacIndex < OrderedCount; DacIndex++) {
      if (HdaRealtekProgramPinDacProfile(Context, Pin, OrderedDacs[DacIndex], Reason)) {
        EnabledCount++;
        break;
      }
    }
  }

  HdaLog("[HDAU] runtime Realtek combos: enabled=%u codec=0x%x selectedPin=0x%x selectedDac=0x%x",
         (UINT32)EnabledCount, (UINT32)mHdaActiveCodecId, Context->Pin, Context->Dac);
}

STATIC VOID HdaEnableLegacyRealtekFanout(HDAU_CONTEXT *Context) {
  EFI_PCI_IO_PROTOCOL *PciIo;
  UINT8 Codec;
  UINT8 WStart;
  UINT8 WCount;
  UINTN WIndex;
  UINT8 Pins[HDAU_MAX_WIDGETS];
  UINT8 Dacs[HDAU_MAX_WIDGETS];
  UINTN PinCount;
  UINTN DacCount;
  UINTN PinIndex;
  UINTN DacIndex;
  UINTN EnabledCount;
  CONST CHAR8 *FanoutReason;
  BOOLEAN BroadSweep;

  if (Context == NULL || !HdaLegacyRealtekFanoutAllowed()) return;

  PciIo = Context->PciIo;
  Codec = Context->Codec;
  if (PciIo == NULL || Context->Afg == 0) return;
  FanoutReason = HdaRealtekFanoutReason();
  BroadSweep = mHdaCfg.BroadRealtekFanout;

  ZeroMem(Pins, sizeof(Pins));
  ZeroMem(Dacs, sizeof(Dacs));
  PinCount = 0;
  DacCount = 0;
  EnabledCount = 0;

  if (!HdaGetSubnodes(PciIo, Codec, Context->Afg, &WStart, &WCount)) return;

  for (WIndex = 0; WIndex < WCount; WIndex++) {
    UINT8 Nid = (UINT8)(WStart + WIndex);
    UINT32 Caps = HdaGetParam(PciIo, Codec, Nid, HDA_PARAM_AUDIO_WIDGET_CAP);
    UINT8 Type = HdaWidgetType(Caps);

    if (Type == HDA_WIDGET_TYPE_AUDIO_OUT && DacCount < HDAU_MAX_WIDGETS) {
      if (!HdaIsAudioOutputDac(PciIo, Codec, Context->Afg, Nid)) continue;
      if (mHdaCfg.ForceDac != HDAU_FORCE_NID_NONE && Nid != (UINT8)mHdaCfg.ForceDac) continue;
      if (!HdaU8ArrayContains(Dacs, DacCount, Nid)) {
        Dacs[DacCount++] = Nid;
      }
    } else if (Type == HDA_WIDGET_TYPE_PIN_COMPLEX && PinCount < HDAU_MAX_WIDGETS) {
      UINT32 PinCap;
      UINT32 Config;

      if (Nid == Context->Pin || HdaU8ArrayContains(Pins, PinCount, Nid)) continue;
      PinCap = HdaGetParam(PciIo, Codec, Nid, HDA_PARAM_PIN_CAP);
      if ((PinCap & HDA_PIN_CAP_OUTPUT) == 0) continue;
      Config = 0;
      if (EFI_ERROR(HdaImmediateCommand(PciIo, Codec, Nid,
                                        HDA_VERB12(HDA_VERB_GET_CONFIG_DEFAULT, 0), &Config))) {
        continue;
      }
      if (HdaPinDefaultIsDigital(Config) && !mHdaCfg.AllowDigitalPins) continue;
      if (!BroadSweep && !HdaPinDefaultLooksConnected(Config)) continue;
      Pins[PinCount++] = Nid;
    }
  }

  for (PinIndex = 0; PinIndex < PinCount; PinIndex++) {
    UINT8 Pin = Pins[PinIndex];
    UINT32 PinCap;
    UINT32 Config;

    if (Pin == 0 || Pin == Context->Pin) continue;
    PinCap = HdaGetParam(PciIo, Codec, Pin, HDA_PARAM_PIN_CAP);
    if ((PinCap & HDA_PIN_CAP_OUTPUT) == 0) continue;
    Config = 0;
    if (EFI_ERROR(HdaImmediateCommand(PciIo, Codec, Pin,
                                      HDA_VERB12(HDA_VERB_GET_CONFIG_DEFAULT, 0), &Config))) {
      continue;
    }
    if (HdaPinDefaultIsDigital(Config) && !mHdaCfg.AllowDigitalPins) continue;
    if (!HdaPinDefaultLooksConnected(Config)) {
      if (!BroadSweep) continue;
      if (mHdaCfg.Debug) {
        HdaLog("[HDAU] realtek broad sweep: trying disabled/no-connect analog pin=0x%x config=0x%x", Pin, Config);
      }
    }

    {
      UINT8 OrderedDacs[HDAU_MAX_WIDGETS];
      UINTN OrderedCount;
      ZeroMem(OrderedDacs, sizeof(OrderedDacs));
      OrderedCount = HdaRealtekBuildDacPreferenceOrder(Pin, Dacs, DacCount, OrderedDacs, ARRAY_SIZE(OrderedDacs));

      for (DacIndex = 0; DacIndex < OrderedCount; DacIndex++) {
        UINT8 Dac = OrderedDacs[DacIndex];
        UINT8 Path[HDAU_MAX_PATH_DEPTH];
        UINTN PathLen;

        if (Dac == 0) continue;
        PathLen = 0;
        if (HdaPathSearch(PciIo, Codec, Pin, Dac, Path, &PathLen) &&
            HdaPathInputAmpsProgrammable(PciIo, Codec, Context->Afg, Path, PathLen)) {
          if (HdaPathContainsPinIntermediate(PciIo, Codec, Path, PathLen)) {
            if (mHdaCfg.Debug) {
              HdaLog("[HDAU] realtek fanout skip: mode=%a pin=0x%x dac=0x%x pin-intermediate pathLen=%u",
                     FanoutReason, Pin, Dac, (UINT32)PathLen);
            }
            continue;
          }
          if (!HdaPathsSelectorCompatible(PciIo, Codec, Context->Path, Context->PathLen, Path, PathLen)) {
            continue;
          }
          HdaLog("[HDAU] realtek fanout: mode=%a pin=0x%x dac=0x%x pathLen=%u config=0x%x",
                 FanoutReason, Pin, Dac, (UINT32)PathLen, Config);
          if (mHdaCfg.TransactionalRestore &&
              EFI_ERROR(HdaSnapshotPathNodes(Context, Path, PathLen, Pin, Dac))) {
            HdaLog("[HDAU] realtek fanout skipped because rollback snapshot is incomplete: pin=0x%x dac=0x%x", Pin, Dac);
            continue;
          }
          if (!EFI_ERROR(HdaProgramAnalogOutputPath(PciIo, Codec, Context->Afg, Pin, Dac, Path, PathLen, FanoutReason))) {
            HdaCommitFanoutSelectorClaims(PciIo, Codec, Path, PathLen);
            EnabledCount++;
            break;
          }
          HdaLog("[HDAU] realtek fanout programming failed: pin=0x%x dac=0x%x mode=%a", Pin, Dac, FanoutReason);
        }
      }
    }
  }

  if (EnabledCount > 0) {
    HdaLog("[HDAU] realtek fanout enabled: mode=%a extraPins=%u selectedPin=0x%x selectedDac=0x%x",
           FanoutReason, (UINT32)EnabledCount, Context->Pin, Context->Dac);
  } else if (mHdaCfg.Debug) {
    HdaLog("[HDAU] realtek fanout: mode=%a no extra analog pins enabled", FanoutReason);
  }
}

STATIC VOID HdaLogAlcIdFanoutState(HDAU_CONTEXT *Context, CONST CHAR8 *Reason) {
  UINTN Index;

  if (Context == NULL || Context->PciIo == NULL) return;
  for (Index = 0; Index < mHdaAlcIdFanoutCount; Index++) {
    UINT8 Pin = mHdaAlcIdFanoutPins[Index];
    UINT8 Dac = mHdaAlcIdFanoutDacs[Index];
    UINT32 PinCtl = 0;
    UINT32 Eapd = 0;
    UINT32 DacFmt = 0;
    UINT32 DacStream = 0;

    if (Pin == 0 || Dac == 0) continue;
    (VOID)HdaImmediateCommand(Context->PciIo, Context->Codec, Pin, HDA_VERB12(HDA_VERB_GET_PIN_WIDGET_CONTROL, 0), &PinCtl);
    (VOID)HdaImmediateCommand(Context->PciIo, Context->Codec, Pin, HDA_VERB12(HDA_VERB_GET_EAPD_BTL, 0), &Eapd);
    (VOID)HdaImmediateCommand(Context->PciIo, Context->Codec, Dac, HDA_VERB4(HDA_VERB_GET_CONVERTER_FORMAT, 0), &DacFmt);
    (VOID)HdaImmediateCommand(Context->PciIo, Context->Codec, Dac, HDA_VERB12(HDA_VERB_GET_CONVERTER_STREAM_CHANNEL, 0), &DacStream);
    HdaLog("[HDAU] state: fanout pin=0x%x pinctl=0x%x eapd=0x%x dac=0x%x fmt=0x%x stream-channel=0x%x reason=%a",
           Pin, PinCtl, Eapd, Dac, DacFmt, DacStream, (Reason != NULL) ? Reason : "unspecified");
  }
}

STATIC EFI_STATUS HdaProgramHdmiInfoFrame(HDAU_CONTEXT *Context) {
  STATIC CONST UINT8 StereoAudioInfoFrame[14] = {
    0x84, 0x01, 0x0A, 0x70, 0x01, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
  };
  UINT32 DipSize;
  UINT32 Verify;
  UINTN Index;
  EFI_STATUS Status;

  if (Context == NULL || !mHdaCfg.DigitalInfoFrame) return EFI_SUCCESS;
  /* HDMI and DisplayPort use different packet layouts. Do not emit an HDMI
     Audio InfoFrame on a DisplayPort route; channel mapping and ELD remain
     active, while DP packet programming stays conservative. */
  if (Context->DisplayPortRoute) return EFI_UNSUPPORTED;
  if (!Context->HdmiRoute) return EFI_SUCCESS;
  DipSize = 0;
  Status = HdaImmediateCommand(Context->PciIo, Context->Codec, Context->Pin,
                               HDA_VERB12(HDA_VERB_GET_HDMI_DIP_SIZE, 0), &DipSize);
  if (EFI_ERROR(Status)) return Status;
  if ((DipSize & 0xFFU) < ARRAY_SIZE(StereoAudioInfoFrame)) return EFI_UNSUPPORTED;
  Status = HdaSetVerb(Context->PciIo, Context->Codec, Context->Pin,
                      HDA_VERB12(HDA_VERB_SET_HDMI_DIP_XMIT, 0));
  if (EFI_ERROR(Status)) return Status;
  Status = HdaSetVerb(Context->PciIo, Context->Codec, Context->Pin,
                      HDA_VERB12(HDA_VERB_SET_HDMI_DIP_INDEX, 0));
  if (EFI_ERROR(Status)) return Status;
  for (Index = 0; Index < ARRAY_SIZE(StereoAudioInfoFrame); Index++) {
    Status = HdaSetVerb(Context->PciIo, Context->Codec, Context->Pin,
                        HDA_VERB12(HDA_VERB_SET_HDMI_DIP_DATA, StereoAudioInfoFrame[Index]));
    if (EFI_ERROR(Status)) return Status;
  }
  Status = HdaSetVerb(Context->PciIo, Context->Codec, Context->Pin,
                      HDA_VERB12(HDA_VERB_SET_HDMI_DIP_XMIT, HDA_HDMI_DIP_XMIT_BEST_EFFORT));
  if (EFI_ERROR(Status)) return Status;
  Verify = 0;
  Status = HdaImmediateCommand(Context->PciIo, Context->Codec, Context->Pin,
                               HDA_VERB12(HDA_VERB_GET_HDMI_DIP_XMIT, 0), &Verify);
  if (EFI_ERROR(Status)) return Status;
  return (((UINT8)Verify & 0xC0U) == HDA_HDMI_DIP_XMIT_BEST_EFFORT) ? EFI_SUCCESS : EFI_DEVICE_ERROR;
}

STATIC EFI_STATUS HdaProgramDigitalOutputPath(HDAU_CONTEXT *Context, CONST CHAR8 *Reason) {
  EFI_STATUS Status;
  UINTN Index;
  UINT32 PinCap;
  UINT32 Sense;
  UINT32 DigitalCtl;
  UINT32 Verify;

  if (Context == NULL || Context->PciIo == NULL || !Context->DigitalRoute ||
      !mHdaCfg.AllowDigitalPins || !mHdaCfg.DigitalPipeline) return EFI_UNSUPPORTED;
  Status = HdaSetPowerD0AndWait(Context->PciIo, Context->Codec, Context->Afg);
  if (EFI_ERROR(Status)) return Status;
  for (Index = 0; Index < Context->PathLen; Index++) {
    Status = HdaSetWidgetPowerD0(Context->PciIo, Context->Codec, Context->Path[Index]);
    if (EFI_ERROR(Status)) return Status;
  }
  Status = HdaGetParamChecked(Context->PciIo, Context->Codec, Context->Pin,
                              HDA_PARAM_PIN_CAP, &PinCap);
  if (EFI_ERROR(Status)) return Status;
  if ((Context->HdmiRoute || Context->DisplayPortRoute) &&
      (PinCap & HDA_PIN_CAP_PRESENCE_DETECT) != 0) {
    Sense = 0;
    if (!HdaReadPinSense(Context->PciIo, Context->Codec, Context->Pin, PinCap, &Sense) ||
        (Sense & HDA_PIN_SENSE_PRESENCE) == 0 || (Sense & HDA_PIN_SENSE_ELD_VALID) == 0) {
      return EFI_NOT_READY;
    }
  }
  Status = HdaProgramConnectionPath(Context->PciIo, Context->Codec, Context->Afg,
                                    Context->Path, Context->PathLen, Reason);
  if (EFI_ERROR(Status)) return Status;
  Status = HdaSetVerb(Context->PciIo, Context->Codec, Context->Pin,
                      HDA_VERB12(HDA_VERB_SET_PIN_WIDGET_CONTROL, HDA_PIN_CTL_OUT_ENABLE));
  if (EFI_ERROR(Status)) return Status;
  Status = HdaProgramOutputDacStream(Context->PciIo, Context->Codec, Context->Afg,
                                     Context->Dac, Reason);
  if (EFI_ERROR(Status)) return Status;

  DigitalCtl = 0;
  Status = HdaImmediateCommand(Context->PciIo, Context->Codec, Context->Dac,
                               HDA_VERB12(HDA_VERB_GET_DIGI_CONVERT_1, 0), &DigitalCtl);
  if (EFI_ERROR(Status)) return Status;
  Status = HdaSetVerb(Context->PciIo, Context->Codec, Context->Dac,
                      HDA_VERB12(HDA_VERB_SET_DIGI_CONVERT_1, (UINT8)(DigitalCtl | HDA_DIGI_ENABLE)));
  if (EFI_ERROR(Status)) return Status;
  Verify = 0;
  Status = HdaImmediateCommand(Context->PciIo, Context->Codec, Context->Dac,
                               HDA_VERB12(HDA_VERB_GET_DIGI_CONVERT_1, 0), &Verify);
  if (EFI_ERROR(Status) || (Verify & HDA_DIGI_ENABLE) == 0) return EFI_DEVICE_ERROR;

  if (Context->HdmiRoute || Context->DisplayPortRoute) {
    Status = HdaSetVerb(Context->PciIo, Context->Codec, Context->Pin,
                        HDA_VERB12(HDA_VERB_SET_HDMI_CHAN_SLOT, 0x00));
    if (EFI_ERROR(Status)) return Status;
    if (Context->Channels > 1) {
      Status = HdaSetVerb(Context->PciIo, Context->Codec, Context->Pin,
                          HDA_VERB12(HDA_VERB_SET_HDMI_CHAN_SLOT, 0x11));
      if (EFI_ERROR(Status)) return Status;
    }
    Status = HdaProgramHdmiInfoFrame(Context);
    if (EFI_ERROR(Status) && Status != EFI_UNSUPPORTED) return Status;
  }
  Context->HardwareVolumeApplied = FALSE;
  HdaLog("[HDAU] digital pipeline: pin=0x%x dac=0x%x hdmi=%u dp=%u fmt=0x%x channels=%u reason=%a",
         Context->Pin, Context->Dac, Context->HdmiRoute ? 1 : 0,
         Context->DisplayPortRoute ? 1 : 0,
         Context->StreamFormat, Context->Channels, Reason == NULL ? "digital" : Reason);
  return EFI_SUCCESS;
}

STATIC EFI_STATUS HdaPostStartCodecReapply(HDAU_CONTEXT *Context) {
  EFI_STATUS Status;
  if (Context == NULL || Context->PciIo == NULL) return EFI_INVALID_PARAMETER;

  if (Context->DigitalRoute) {
    return HdaProgramDigitalOutputPath(Context, "post-stream-start-digital");
  }

  HdaLog("[HDAU] post-start codec reapply: exact alcid verbs, validated routes, fanout and converters");
  Status = HdaApplyAlcIdPrebootVerbs(Context, "post-stream-start");
  if (EFI_ERROR(Status)) return Status;
  Status = HdaApplyExactProfileVerbs(Context, "post-stream-start-exact");
  if (EFI_ERROR(Status)) return Status;
  Status = HdaSelectConnectionForPath(Context);
  if (EFI_ERROR(Status)) return Status;
  Status = HdaProgramOutputPin(Context->PciIo, Context->Codec, Context->Pin, "post-stream-start-selected");
  if (EFI_ERROR(Status)) return Status;
  HdaResetFanoutSelectorClaims(Context->PciIo, Context->Codec, Context->Path, Context->PathLen);
  mHdaVolumeReadbackMismatch = FALSE;
  HdaAlcIdLayoutSafeFanout(Context);
  if (mHdaVolumeReadbackMismatch) {
    HdaLog("[HDAU] post-start volume safety: fanout gain mismatch; stopping playback");
    return EFI_COMPROMISED_DATA;
  }
  Status = HdaSelectConnectionForPath(Context);
  if (EFI_ERROR(Status)) return Status;
  Status = HdaProgramOutputPin(Context->PciIo, Context->Codec, Context->Pin, "post-stream-start-selected-final");
  if (EFI_ERROR(Status)) return Status;
  Status = HdaProgramOutputDacStream(Context->PciIo, Context->Codec, Context->Afg, Context->Dac, "post-stream-start-selected");
  if (EFI_ERROR(Status)) return Status;
  if (Context->HardwareVolumeApplied) {
    Status = HdaProgramPlaybackVolume(Context, TRUE, "post-stream-start-volume");
    if (EFI_ERROR(Status)) return Status;
  }
  HdaLogSelectedPathState(Context, "post-stream-start");
  HdaLogAlcIdFanoutState(Context, "post-stream-start");
  return EFI_SUCCESS;
}


STATIC EFI_STATUS HdaUnmutePath(HDAU_CONTEXT *Context) {
  UINTN Index;
  EFI_STATUS Status;

  if (Context == NULL || Context->PciIo == NULL) return EFI_INVALID_PARAMETER;
  mHdaVolumeReadbackMismatch = FALSE;
  if (Context->DigitalRoute) {
    return HdaProgramDigitalOutputPath(Context, "selected-digital-path");
  }

  HdaRealtekCodecWarmReset(Context);
  Status = HdaSetPowerD0AndWait(Context->PciIo, Context->Codec, Context->Afg);
  if (EFI_ERROR(Status)) return Status;
  Status = HdaApplyAlcIdPrebootVerbs(Context, "post-warm-pre-route");
  if (EFI_ERROR(Status)) return Status;
  Status = HdaApplyExactProfileVerbs(Context, "post-warm-exact-profile");
  if (EFI_ERROR(Status)) return Status;

  for (Index = 0; Index < Context->PathLen; Index++) {
    Status = HdaSetWidgetPowerD0(Context->PciIo, Context->Codec, Context->Path[Index]);
    if (EFI_ERROR(Status)) return Status;
  }

  Status = HdaSelectConnectionForPath(Context);
  if (EFI_ERROR(Status)) return Status;
  Status = HdaProgramOutputPin(Context->PciIo, Context->Codec, Context->Pin, "selected-path");
  if (EFI_ERROR(Status)) return Status;

  /* Keep every path amp muted/unity change after EAPD has stabilized so the
     first audible sample is not lost while an external amplifier powers up. */
  for (Index = 0; Index < Context->PathLen; Index++) {
    Status = HdaProgramAmpSafeUnity(Context->PciIo, Context->Codec, Context->Afg,
                                    Context->Path[Index], "selected-path");
    if (EFI_ERROR(Status)) return Status;
  }

  HdaResetFanoutSelectorClaims(Context->PciIo, Context->Codec, Context->Path, Context->PathLen);
  HdaRealtekGpioAmpInit(Context);
  HdaRealtekAppleAlcComboInit(Context);
  HdaAlcIdLayoutSafeFanout(Context);
  HdaEnableLegacyRealtekFanout(Context);
  if (mHdaVolumeReadbackMismatch) {
    HdaLog("[HDAU] volume safety: fanout gain mismatch; rejecting playback before DMA");
    return EFI_COMPROMISED_DATA;
  }
  Status = HdaSelectConnectionForPath(Context);
  if (EFI_ERROR(Status)) return Status;
  Status = HdaProgramOutputPin(Context->PciIo, Context->Codec, Context->Pin, "selected-path-final");
  if (EFI_ERROR(Status)) return Status;
  Status = HdaProgramOutputDacStream(Context->PciIo, Context->Codec, Context->Afg, Context->Dac, "selected-path");
  if (EFI_ERROR(Status)) return Status;
  HdaRealtekGpioAmpInit(Context);
  Status = HdaProgramPlaybackVolume(Context, FALSE, "selected-path-volume");
  if (Status == EFI_UNSUPPORTED) {
    Context->HardwareVolumeApplied = FALSE;
    HdaLog("[HDAU] volume: no single usable HDA amp; using software fallback Q8=%u",
           (UINT32)mHdaCfg.VolumeQ8);
  } else if (EFI_ERROR(Status)) {
    return Status;
  }
  HdaLogSelectedPathState(Context, "post-program");
  HdaLogAlcIdFanoutState(Context, "post-program");

  if (mHdaActiveAlcIdProfile != NULL) HdaStall(50000);
  return EFI_SUCCESS;
}


STATIC EFI_STATUS HdaCopyBootChime(HDAU_CONTEXT *Context, INT16 *Buffer, UINTN GainQ8) {
  UINTN Frame;

  if (Context == NULL || Buffer == NULL || Context->PcmData == NULL ||
      Context->PcmFrames == 0 || (Context->Channels != 1 && Context->Channels != 2)) {
    return EFI_INVALID_PARAMETER;
  }
  /* The final PCM amplitude is fixed regardless of caller or fallback state. */
  (VOID)GainQ8;
  for (Frame = 0; Frame < Context->PcmFrames; Frame++) {
    INT32 Left = Context->PcmData[Frame * 2U];
    INT32 Right = Context->PcmData[Frame * 2U + 1U];
    if (Context->Channels == 2) {
      INT32 OutLeft = (Left * (INT32)HDAU_BOOT_CHIME_VOLUME_PERCENT) / 100;
      INT32 OutRight = (Right * (INT32)HDAU_BOOT_CHIME_VOLUME_PERCENT) / 100;
      if (OutLeft > 32767) OutLeft = 32767;
      if (OutLeft < -32768) OutLeft = -32768;
      if (OutRight > 32767) OutRight = 32767;
      if (OutRight < -32768) OutRight = -32768;
      Buffer[Frame * 2U] = (INT16)OutLeft;
      Buffer[Frame * 2U + 1U] = (INT16)OutRight;
    } else {
      INT32 Mono = ((Left + Right) / 2) * (INT32)HDAU_BOOT_CHIME_VOLUME_PERCENT / 100;
      if (Mono > 32767) Mono = 32767;
      if (Mono < -32768) Mono = -32768;
      Buffer[Frame] = (INT16)Mono;
    }
  }
  return EFI_SUCCESS;
}

STATIC EFI_STATUS HdaAllocatePlaybackBuffers(HDAU_CONTEXT *Context) {
  EFI_STATUS Status;
  EFI_PHYSICAL_ADDRESS MaxAddress;
  EFI_PHYSICAL_ADDRESS AudioDeviceAddress;
  EFI_PHYSICAL_ADDRESS BdlDeviceAddress;
  VOID *AudioMapping;
  VOID *BdlMapping;
  UINTN SilenceBytes;
  UINTN RequiredAudioBufferBytes;
  UINTN MapAudioBytes;
  UINTN MapBdlBytes;
  UINTN BufferPages;
  UINTN BdlPages;
  UINTN FirstBdlBytes;
  UINTN SecondBdlBytes;
  HDA_BDL_ENTRY *Bdl;

  if (Context == NULL || Context->PciIo == NULL || gBS == NULL ||
      Context->PcmData == NULL || Context->PcmBytes == 0 || Context->BytesPerSecond == 0) {
    return EFI_INVALID_PARAMETER;
  }

  SilenceBytes = (UINTN)((Context->BytesPerSecond *
                          (mHdaCfg.FastBoot ? HDAU_FASTBOOT_SILENCE_MS : HDAU_TRAILING_SILENCE_MS)) / 1000ULL);
  if (SilenceBytes > (MAX_UINTN - Context->PcmBytes)) {
    HdaLog("[HDAU] DMA buffer size overflow: chimeBytes=%u silenceBytes=%u", (UINT32)Context->PcmBytes, (UINT32)SilenceBytes);
    return EFI_BAD_BUFFER_SIZE;
  }

  RequiredAudioBufferBytes = Context->PcmBytes + SilenceBytes;
  if (RequiredAudioBufferBytes == 0 || RequiredAudioBufferBytes > HDAU_MAX_AUDIO_BUFFER_BYTES || RequiredAudioBufferBytes > (UINTN)MAX_UINT32) {
    HdaLog("[HDAU] DMA buffer size rejected: bytes=%u", (UINT32)RequiredAudioBufferBytes);
    return EFI_BAD_BUFFER_SIZE;
  }

  BufferPages = EFI_SIZE_TO_PAGES(RequiredAudioBufferBytes);
  if (BufferPages == 0) return EFI_BAD_BUFFER_SIZE;

  if (RequiredAudioBufferBytes <= HDA_DMA_ADDRESS_ALIGNMENT ||
      (RequiredAudioBufferBytes & 1U) != 0) {
    HdaLog("[HDAU] DMA buffer cannot form two word-aligned BDL entries: bytes=%u",
           (UINT32)RequiredAudioBufferBytes);
    return EFI_BAD_BUFFER_SIZE;
  }

  BdlPages = EFI_SIZE_TO_PAGES(HDAU_BDL_ENTRY_COUNT * sizeof(HDA_BDL_ENTRY));
  AudioDeviceAddress = 0;
  BdlDeviceAddress = 0;
  AudioMapping = NULL;
  BdlMapping = NULL;
  Context->AudioBufferBytes = RequiredAudioBufferBytes;
  Context->BdlBytes = HDAU_BDL_ENTRY_COUNT * sizeof(HDA_BDL_ENTRY);
  Context->AudioMapping = NULL;
  Context->BdlMapping = NULL;
  Context->AudioMapped = FALSE;
  Context->BdlMapped = FALSE;
  Context->AudioDeviceAddress = 0;
  Context->BdlDeviceAddress = 0;
  Context->AudioBuffer = NULL;
  Context->Bdl = NULL;
  Context->AudioBufferPhys = 0;
  Context->BdlPhys = 0;
  Context->BufferPages = 0;
  Context->BdlPages = 0;

  MaxAddress = 0xFFFFFFFFULL;
  Status = gBS->AllocatePages(AllocateMaxAddress, EfiBootServicesData, BufferPages, &MaxAddress);
  if (Status != EFI_SUCCESS) return HdaNormalizeExactSuccess(Status);
  Context->AudioBufferPhys = MaxAddress;
  Context->AudioBuffer = (VOID *)(UINTN)MaxAddress;
  Context->BufferPages = BufferPages;
  if (Context->AudioBuffer == NULL) {
    HdaLog("[HDAU] DMA audio allocation returned physical address zero; rejecting null host pointer");
    HdaFreePlaybackBuffers(Context);
    return EFI_DEVICE_ERROR;
  }
  ZeroMem(Context->AudioBuffer, EFI_PAGES_TO_SIZE(BufferPages));
  Status = HdaCopyBootChime(Context, (INT16 *)Context->AudioBuffer,
                            HDAU_BOOT_CHIME_GAIN_Q8);
  if (EFI_ERROR(Status)) {
    HdaFreePlaybackBuffers(Context);
    return Status;
  }

  MapAudioBytes = RequiredAudioBufferBytes;
  /*
   * PCM is fully prepared before Map() and the HDA bus master only reads it.
   * UEFI therefore requires BusMasterRead here. CommonBuffer would require
   * memory allocated by EFI_PCI_IO_PROTOCOL.AllocateBuffer().
   */
  Status = Context->PciIo->Map(
    Context->PciIo,
    EfiPciIoOperationBusMasterRead,
    Context->AudioBuffer,
    &MapAudioBytes,
    &AudioDeviceAddress,
    &AudioMapping
  );
  if (Status != EFI_SUCCESS) {
    Status = HdaNormalizeExactSuccess(Status);
    HdaLog("[HDAU] DMA map audio buffer failed: %r", Status);
    HdaFreePlaybackBuffers(Context);
    return Status;
  }
  /* Publish DMA ownership only after Map() succeeds.  This prevents a failed
     firmware call from injecting an unowned Mapping token into cleanup. */
  Context->AudioDeviceAddress = AudioDeviceAddress;
  Context->AudioMapping = AudioMapping;
  Context->AudioMapped = TRUE;
  if (MapAudioBytes < RequiredAudioBufferBytes) {
    HdaLog("[HDAU] DMA map audio buffer truncated: mapped=%u required=%u",
           (UINT32)MapAudioBytes, (UINT32)RequiredAudioBufferBytes);
    HdaFreePlaybackBuffers(Context);
    return EFI_DEVICE_ERROR;
  }
  if (RequiredAudioBufferBytes != 0 &&
      Context->AudioDeviceAddress > (MAX_UINT64 - (UINT64)(RequiredAudioBufferBytes - 1U))) {
    HdaLog("[HDAU] DMA audio device range wraps physical address space: base=0x%lx bytes=%u",
           (UINT64)Context->AudioDeviceAddress, (UINT32)RequiredAudioBufferBytes);
    HdaFreePlaybackBuffers(Context);
    return EFI_DEVICE_ERROR;
  }
  Context->AudioBufferBytes = RequiredAudioBufferBytes;
  if ((Context->AudioDeviceAddress & (HDA_DMA_ADDRESS_ALIGNMENT - 1U)) != 0) {
    HdaLog("[HDAU] DMA audio device address is not %u-byte aligned: 0x%lx",
           HDA_DMA_ADDRESS_ALIGNMENT, (UINT64)Context->AudioDeviceAddress);
    HdaFreePlaybackBuffers(Context);
    return EFI_UNSUPPORTED;
  }

  MaxAddress = 0xFFFFFFFFULL;
  Status = gBS->AllocatePages(AllocateMaxAddress, EfiBootServicesData, BdlPages, &MaxAddress);
  if (Status != EFI_SUCCESS) {
    Status = HdaNormalizeExactSuccess(Status);
    HdaFreePlaybackBuffers(Context);
    return Status;
  }

  Context->BdlPhys = MaxAddress;
  Context->Bdl = (VOID *)(UINTN)MaxAddress;
  Context->BdlPages = BdlPages;
  if (Context->Bdl == NULL) {
    HdaLog("[HDAU] DMA BDL allocation returned physical address zero; rejecting null host pointer");
    HdaFreePlaybackBuffers(Context);
    return EFI_DEVICE_ERROR;
  }
  ZeroMem(Context->Bdl, EFI_PAGES_TO_SIZE(BdlPages));
  Bdl = (HDA_BDL_ENTRY *)Context->Bdl;

  /* Intel HDA requires LVI >= 1, i.e. at least two valid BDLEs. Keep the
     second buffer start 128-byte aligned while covering the exact CBL. */
  FirstBdlBytes = (Context->AudioBufferBytes / 2U) & ~(UINTN)(HDA_DMA_ADDRESS_ALIGNMENT - 1U);
  if (FirstBdlBytes < HDA_DMA_ADDRESS_ALIGNMENT) FirstBdlBytes = HDA_DMA_ADDRESS_ALIGNMENT;
  if (FirstBdlBytes >= Context->AudioBufferBytes) {
    HdaFreePlaybackBuffers(Context);
    return EFI_BAD_BUFFER_SIZE;
  }
  SecondBdlBytes = Context->AudioBufferBytes - FirstBdlBytes;
  if ((FirstBdlBytes & 1U) != 0 || (SecondBdlBytes & 1U) != 0 ||
      FirstBdlBytes > (UINTN)MAX_UINT32 || SecondBdlBytes > (UINTN)MAX_UINT32) {
    HdaFreePlaybackBuffers(Context);
    return EFI_BAD_BUFFER_SIZE;
  }
  Bdl[0].Address = Context->AudioDeviceAddress;
  Bdl[0].Length = (UINT32)FirstBdlBytes;
  Bdl[0].Ioc = 0;
  Bdl[1].Address = Context->AudioDeviceAddress + FirstBdlBytes;
  Bdl[1].Length = (UINT32)SecondBdlBytes;
  Bdl[1].Ioc = 1;

  MapBdlBytes = HDAU_BDL_ENTRY_COUNT * sizeof(HDA_BDL_ENTRY);
  /* The BDL is likewise immutable after Map() and read by the controller. */
  Status = Context->PciIo->Map(
    Context->PciIo,
    EfiPciIoOperationBusMasterRead,
    Context->Bdl,
    &MapBdlBytes,
    &BdlDeviceAddress,
    &BdlMapping
  );
  if (Status != EFI_SUCCESS) {
    Status = HdaNormalizeExactSuccess(Status);
    HdaLog("[HDAU] DMA map BDL failed: %r", Status);
    HdaFreePlaybackBuffers(Context);
    return Status;
  }
  Context->BdlDeviceAddress = BdlDeviceAddress;
  Context->BdlMapping = BdlMapping;
  Context->BdlMapped = TRUE;
  if (MapBdlBytes < HDAU_BDL_ENTRY_COUNT * sizeof(HDA_BDL_ENTRY)) {
    HdaLog("[HDAU] DMA map BDL truncated: mapped=%u required=%u",
           (UINT32)MapBdlBytes, (UINT32)(HDAU_BDL_ENTRY_COUNT * sizeof(HDA_BDL_ENTRY)));
    HdaFreePlaybackBuffers(Context);
    return EFI_DEVICE_ERROR;
  }
  Context->BdlBytes = HDAU_BDL_ENTRY_COUNT * sizeof(HDA_BDL_ENTRY);
  if (Context->BdlBytes != 0 &&
      Context->BdlDeviceAddress > (MAX_UINT64 - (UINT64)(Context->BdlBytes - 1U))) {
    HdaLog("[HDAU] DMA BDL device range wraps physical address space: base=0x%lx bytes=%u",
           (UINT64)Context->BdlDeviceAddress, (UINT32)Context->BdlBytes);
    HdaFreePlaybackBuffers(Context);
    return EFI_DEVICE_ERROR;
  }
  if ((Context->BdlDeviceAddress & (HDA_DMA_ADDRESS_ALIGNMENT - 1U)) != 0) {
    HdaLog("[HDAU] DMA BDL device address is not %u-byte aligned: 0x%lx",
           HDA_DMA_ADDRESS_ALIGNMENT, (UINT64)Context->BdlDeviceAddress);
    HdaFreePlaybackBuffers(Context);
    return EFI_UNSUPPORTED;
  }

  Status = HdaPciFlushExact(Context->PciIo);
  if (EFI_ERROR(Status)) {
    HdaLog("[HDAU] DMA buffer flush failed: %r", Status);
    HdaFreePlaybackBuffers(Context);
    return Status;
  }
  HdaLog("[HDAU] DMA buffers: audio host=0x%lx dev=0x%lx bytes=%u bdl host=0x%lx dev=0x%lx bytes=%u chimeBytes=%u silenceBytes=%u",
         (UINT64)Context->AudioBufferPhys, (UINT64)Context->AudioDeviceAddress, (UINT32)Context->AudioBufferBytes,
         (UINT64)Context->BdlPhys, (UINT64)Context->BdlDeviceAddress, (UINT32)Context->BdlBytes,
         (UINT32)Context->PcmBytes, (UINT32)SilenceBytes);

  return EFI_SUCCESS;
}

STATIC BOOLEAN HdaFreePlaybackBuffers(HDAU_CONTEXT *Context) {
  EFI_STATUS Status;
  BOOLEAN BdlReleased;
  BOOLEAN AudioReleased;

  if (Context == NULL) return TRUE;
  BdlReleased = TRUE;
  AudioReleased = TRUE;

  if (Context->BdlMapped) {
    if (Context->PciIo == NULL) {
      BdlReleased = FALSE;
    } else {
      /* Mapping is an opaque value and may legitimately be NULL for a direct
         mapping. UEFI still requires every successful Map() to be completed
         by Unmap(), so ownership is tracked separately from the token value. */
      Status = Context->PciIo->Unmap(Context->PciIo, Context->BdlMapping);
      Context->BdlMapped = FALSE;
      Context->BdlMapping = NULL;
      if (Status != EFI_SUCCESS) {
        if (!EFI_ERROR(Status)) Status = EFI_DEVICE_ERROR;
        HdaLog("[HDAU] DMA BDL unmap completed with error status: %r", Status);
      }
    }
  }

  if (Context->AudioMapped) {
    if (Context->PciIo == NULL) {
      AudioReleased = FALSE;
    } else {
      Status = Context->PciIo->Unmap(Context->PciIo, Context->AudioMapping);
      Context->AudioMapped = FALSE;
      Context->AudioMapping = NULL;
      if (Status != EFI_SUCCESS) {
        if (!EFI_ERROR(Status)) Status = EFI_DEVICE_ERROR;
        HdaLog("[HDAU] DMA audio unmap completed with error status: %r", Status);
      }
    }
  }

  if (Context->BdlPages != 0 && gBS == NULL) BdlReleased = FALSE;
  if (Context->BufferPages != 0 && gBS == NULL) AudioReleased = FALSE;

  if (BdlReleased && gBS != NULL && Context->BdlPages != 0) {
    Status = gBS->FreePages(Context->BdlPhys, Context->BdlPages);
    if (Status == EFI_SUCCESS || Status == EFI_NOT_FOUND) {
      /* NOT_FOUND means these pages are not an AllocatePages ownership. */
      if (Status == EFI_NOT_FOUND) {
        HdaLog("[HDAU] DMA BDL pages already absent from AllocatePages ownership");
      }
      Context->Bdl = NULL;
      Context->BdlPhys = 0;
      Context->BdlDeviceAddress = 0;
      Context->BdlBytes = 0;
      Context->BdlPages = 0;
    } else {
      if (!EFI_ERROR(Status)) Status = EFI_DEVICE_ERROR;
      BdlReleased = FALSE;
      HdaLog("[HDAU] DMA BDL page release failed; ownership retained: %r", Status);
    }
  } else if (BdlReleased) {
    Context->Bdl = NULL;
    Context->BdlPhys = 0;
    Context->BdlDeviceAddress = 0;
    Context->BdlBytes = 0;
  }

  if (AudioReleased && gBS != NULL && Context->BufferPages != 0) {
    Status = gBS->FreePages(Context->AudioBufferPhys, Context->BufferPages);
    if (Status == EFI_SUCCESS || Status == EFI_NOT_FOUND) {
      if (Status == EFI_NOT_FOUND) {
        HdaLog("[HDAU] DMA audio pages already absent from AllocatePages ownership");
      }
      Context->AudioBuffer = NULL;
      Context->AudioBufferPhys = 0;
      Context->AudioDeviceAddress = 0;
      Context->AudioBufferBytes = 0;
      Context->BufferPages = 0;
    } else {
      if (!EFI_ERROR(Status)) Status = EFI_DEVICE_ERROR;
      AudioReleased = FALSE;
      HdaLog("[HDAU] DMA audio page release failed; ownership retained: %r", Status);
    }
  } else if (AudioReleased) {
    Context->AudioBuffer = NULL;
    Context->AudioBufferPhys = 0;
    Context->AudioDeviceAddress = 0;
    Context->AudioBufferBytes = 0;
  }

  return (BdlReleased && AudioReleased &&
          !Context->BdlMapped && !Context->AudioMapped &&
          Context->BdlPages == 0 && Context->BufferPages == 0);
}

STATIC EFI_STATUS HdaFinalizePlaybackContext(
  HDAU_CONTEXT *Context,
  CONST CHAR8 *Reason,
  EFI_STATUS ResultStatus,
  EFI_STATUS RestoreStatus
) {
  EFI_STATUS RetainStatus;
  BOOLEAN BuffersReleased;

  if (Context == NULL) return EFI_INVALID_PARAMETER;
  BuffersReleased = HdaFreePlaybackBuffers(Context);
  if (!EFI_ERROR(RestoreStatus) && BuffersReleased) return ResultStatus;

  if (Context->PostPlaybackCleanupStatus == EFI_SUCCESS) {
    Context->PostPlaybackCleanupStatus = EFI_ERROR(RestoreStatus)
                                           ? RestoreStatus
                                           : EFI_DEVICE_ERROR;
  }
  RetainStatus = HdaRetainContextForCleanup(Context, Reason);
  HdaLog("[HDAU] final cleanup pending: reason=%a result=%r restore=%r buffersReleased=%u retain=%r cleanup=%r",
         Reason == NULL ? "unspecified" : Reason, ResultStatus, RestoreStatus,
         BuffersReleased ? 1 : 0, RetainStatus, Context->PostPlaybackCleanupStatus);
  if (EFI_ERROR(RetainStatus)) return RetainStatus;
  if (EFI_ERROR(ResultStatus)) return ResultStatus;
  /* Playback may already have completed. Keep the real cleanup EFI_STATUS in
     the context for diagnostics, but return playback success so controller
     selection cannot replay the chime solely because rollback is pending. */
  return EFI_SUCCESS;
}

STATIC EFI_STATUS HdaCaptureIdleStreamState(HDAU_CONTEXT *Context, UINT16 Sd) {
  EFI_STATUS Status;
  UINT8 Ctl0;
  UINT8 Ctl1;
  UINT8 Ctl2;
  UINT8 Sts;
  UINT32 Cbl;
  UINT16 Lvi;
  UINT16 Fmt;
  UINT32 Bdlpl;
  UINT32 Bdlpu;

  if (Context == NULL || Context->PciIo == NULL) return EFI_INVALID_PARAMETER;

  Ctl0 = 0;
  Ctl1 = 0;
  Ctl2 = 0;
  Sts = 0;
  Cbl = 0;
  Lvi = 0;
  Fmt = 0;
  Bdlpl = 0;
  Bdlpu = 0;
  Status = HdaMemRead8(Context->PciIo, Sd + HDA_SD_CTL, &Ctl0);
  if (EFI_ERROR(Status)) return Status;
  Status = HdaMemRead8(Context->PciIo, Sd + HDA_SD_CTL + 1, &Ctl1);
  if (EFI_ERROR(Status)) return Status;
  Status = HdaMemRead8(Context->PciIo, Sd + HDA_SD_CTL + 2, &Ctl2);
  if (EFI_ERROR(Status)) return Status;
  Status = HdaMemRead8(Context->PciIo, Sd + HDA_SD_STS, &Sts);
  if (EFI_ERROR(Status)) return Status;
  if ((Ctl0 & (HDA_SD_CTL_RUN | HDA_SD_CTL_SRST)) != 0 ||
      (Sts & HDA_SD_STS_OWNERSHIP_MASK) != 0) {
    return EFI_ACCESS_DENIED;
  }
  Status = HdaMemRead32(Context->PciIo, Sd + HDA_SD_CBL, &Cbl);
  if (EFI_ERROR(Status)) return Status;
  Status = HdaMemRead16(Context->PciIo, Sd + HDA_SD_LVI, &Lvi);
  if (EFI_ERROR(Status)) return Status;
  Status = HdaMemRead16(Context->PciIo, Sd + HDA_SD_FMT, &Fmt);
  if (EFI_ERROR(Status)) return Status;
  Status = HdaMemRead32(Context->PciIo, Sd + HDA_SD_BDLPL, &Bdlpl);
  if (EFI_ERROR(Status)) return Status;
  Status = HdaMemRead32(Context->PciIo, Sd + HDA_SD_BDLPU, &Bdlpu);
  if (EFI_ERROR(Status)) return Status;

  Context->OriginalStreamCtl0 = Ctl0;
  Context->OriginalStreamCtl1 = Ctl1;
  Context->OriginalStreamCtl2 = Ctl2;
  Context->OriginalStreamCbl = Cbl;
  Context->OriginalStreamLvi = Lvi;
  Context->OriginalStreamFmt = Fmt;
  Context->OriginalStreamBdlpl = Bdlpl;
  Context->OriginalStreamBdlpu = Bdlpu;
  Context->StreamStateValid = TRUE;
  return EFI_SUCCESS;
}

STATIC EFI_STATUS HdaRequireIdleStreamSnapshotUnchanged(HDAU_CONTEXT *Context, UINT16 Sd) {
  EFI_STATUS Status;
  UINT8 Ctl0;
  UINT8 Ctl1;
  UINT8 Ctl2;
  UINT8 Sts;
  UINT32 Cbl;
  UINT16 Lvi;
  UINT16 Fmt;
  UINT32 Bdlpl;
  UINT32 Bdlpu;

  if (Context == NULL || Context->PciIo == NULL || !Context->StreamStateValid) {
    return EFI_INVALID_PARAMETER;
  }

  Status = HdaMemRead8(Context->PciIo, Sd + HDA_SD_CTL, &Ctl0);
  if (EFI_ERROR(Status)) return Status;
  Status = HdaMemRead8(Context->PciIo, Sd + HDA_SD_CTL + 1, &Ctl1);
  if (EFI_ERROR(Status)) return Status;
  Status = HdaMemRead8(Context->PciIo, Sd + HDA_SD_CTL + 2, &Ctl2);
  if (EFI_ERROR(Status)) return Status;
  Status = HdaMemRead8(Context->PciIo, Sd + HDA_SD_STS, &Sts);
  if (EFI_ERROR(Status)) return Status;
  Status = HdaMemRead32(Context->PciIo, Sd + HDA_SD_CBL, &Cbl);
  if (EFI_ERROR(Status)) return Status;
  Status = HdaMemRead16(Context->PciIo, Sd + HDA_SD_LVI, &Lvi);
  if (EFI_ERROR(Status)) return Status;
  Status = HdaMemRead16(Context->PciIo, Sd + HDA_SD_FMT, &Fmt);
  if (EFI_ERROR(Status)) return Status;
  Status = HdaMemRead32(Context->PciIo, Sd + HDA_SD_BDLPL, &Bdlpl);
  if (EFI_ERROR(Status)) return Status;
  Status = HdaMemRead32(Context->PciIo, Sd + HDA_SD_BDLPU, &Bdlpu);
  if (EFI_ERROR(Status)) return Status;

  if ((Ctl0 & (HDA_SD_CTL_RUN | HDA_SD_CTL_SRST)) != 0 ||
      (Sts & HDA_SD_STS_OWNERSHIP_MASK) != 0 ||
      Ctl0 != Context->OriginalStreamCtl0 ||
      Ctl1 != Context->OriginalStreamCtl1 ||
      Ctl2 != Context->OriginalStreamCtl2 ||
      Cbl != Context->OriginalStreamCbl ||
      Lvi != Context->OriginalStreamLvi ||
      Fmt != Context->OriginalStreamFmt ||
      Bdlpl != Context->OriginalStreamBdlpl ||
      Bdlpu != Context->OriginalStreamBdlpu) {
    return EFI_ACCESS_DENIED;
  }
  return EFI_SUCCESS;
}

STATIC EFI_STATUS HdaRequireStreamTagAvailable(
  EFI_PCI_IO_PROTOCOL *PciIo,
  UINT16 SelectedSd,
  UINT8 StreamTag
) {
  EFI_STATUS Status;
  UINT16 Gcap;
  UINT8 Iss;
  UINT8 Oss;
  UINT8 Bss;
  UINT8 TotalStreams;
  UINT8 StreamIndex;
  UINT8 CtlBefore;
  UINT8 CtlAfter;
  UINT8 Ctl2;
  UINT8 Ctl2After;
  UINT8 Tag;

  if (PciIo == NULL || StreamTag == 0 || StreamTag > 15) return EFI_INVALID_PARAMETER;
  Status = HdaMemRead16(PciIo, HDA_REG_GCAP, &Gcap);
  if (EFI_ERROR(Status)) return Status;
  Iss = (UINT8)((Gcap >> 8) & 0x0F);
  Oss = (UINT8)((Gcap >> 12) & 0x0F);
  Bss = (UINT8)((Gcap >> HDA_GCAP_BSS_SHIFT) & HDA_GCAP_BSS_MASK);
  TotalStreams = (UINT8)(Iss + Oss + Bss);

  for (StreamIndex = 0; StreamIndex < TotalStreams; StreamIndex++) {
    UINT16 Candidate = (UINT16)(HDA_REG_SD_BASE + ((UINT16)StreamIndex * HDA_SD_SIZE));
    if (Candidate == SelectedSd) continue;
    Status = HdaMemRead8(PciIo, Candidate + HDA_SD_CTL, &CtlBefore);
    if (EFI_ERROR(Status)) return Status;
    if ((CtlBefore & HDA_SD_CTL_SRST) != 0) return EFI_ACCESS_DENIED;
    Status = HdaMemRead8(PciIo, Candidate + HDA_SD_CTL + 2, &Ctl2);
    if (EFI_ERROR(Status)) return Status;
    Status = HdaMemRead8(PciIo, Candidate + HDA_SD_CTL, &CtlAfter);
    if (EFI_ERROR(Status)) return Status;
    if ((CtlAfter & HDA_SD_CTL_SRST) != 0 || CtlAfter != CtlBefore) return EFI_ACCESS_DENIED;
    Status = HdaMemRead8(PciIo, Candidate + HDA_SD_CTL + 2, &Ctl2After);
    if (EFI_ERROR(Status)) return Status;
    if (Ctl2After != Ctl2) return EFI_ACCESS_DENIED;
    Tag = (UINT8)((Ctl2 >> 4) & 0x0F);
    if (Tag == StreamTag) return EFI_ACCESS_DENIED;
  }
  return EFI_SUCCESS;
}

STATIC EFI_STATUS HdaResetStreamDescriptor(EFI_PCI_IO_PROTOCOL *PciIo, UINT16 Sd) {
  EFI_STATUS Status;
  UINT8 Ctl;
  UINTN Index;

  if (PciIo == NULL) return EFI_INVALID_PARAMETER;
  Ctl = 0;
  Status = HdaMemRead8(PciIo, Sd + HDA_SD_CTL, &Ctl);
  if (EFI_ERROR(Status)) return Status;
  if ((Ctl & HDA_SD_CTL_RUN) != 0) return EFI_ACCESS_DENIED;

  Ctl |= HDA_SD_CTL_SRST;
  Status = HdaMemWrite8(PciIo, Sd + HDA_SD_CTL, Ctl);
  if (EFI_ERROR(Status)) return Status;
  for (Index = 0; Index < (HDAU_STREAM_RESET_TIMEOUT_US / HDAU_POLL_GRANULARITY_US); Index++) {
    Status = HdaMemRead8(PciIo, Sd + HDA_SD_CTL, &Ctl);
    if (EFI_ERROR(Status)) return Status;
    if ((Ctl & HDA_SD_CTL_SRST) != 0) break;
    HdaStall(HDAU_POLL_GRANULARITY_US);
  }
  if ((Ctl & HDA_SD_CTL_SRST) == 0) return EFI_TIMEOUT;

  Ctl &= (UINT8)~HDA_SD_CTL_SRST;
  Status = HdaMemWrite8(PciIo, Sd + HDA_SD_CTL, Ctl);
  if (EFI_ERROR(Status)) return Status;
  for (Index = 0; Index < (HDAU_STREAM_RESET_TIMEOUT_US / HDAU_POLL_GRANULARITY_US); Index++) {
    Status = HdaMemRead8(PciIo, Sd + HDA_SD_CTL, &Ctl);
    if (EFI_ERROR(Status)) return Status;
    if ((Ctl & HDA_SD_CTL_SRST) == 0) return EFI_SUCCESS;
    HdaStall(HDAU_POLL_GRANULARITY_US);
  }
  return EFI_TIMEOUT;
}

STATIC EFI_STATUS HdaWriteStreamLviPreserve(EFI_PCI_IO_PROTOCOL *PciIo, UINT16 Sd, UINT16 DesiredLvi) {
  EFI_STATUS Status;
  UINT16 CurrentLvi;

  if (PciIo == NULL) return EFI_INVALID_PARAMETER;
  CurrentLvi = 0;
  Status = HdaMemRead16(PciIo, Sd + HDA_SD_LVI, &CurrentLvi);
  if (EFI_ERROR(Status)) return Status;
  return HdaMemWrite16(
           PciIo,
           Sd + HDA_SD_LVI,
           (UINT16)((CurrentLvi & HDA_SD_LVI_RSVDP_MASK) | (DesiredLvi & HDA_SD_LVI_VALUE_MASK))
           );
}

STATIC EFI_STATUS HdaWriteStreamFormatPreserve(EFI_PCI_IO_PROTOCOL *PciIo, UINT16 Sd, UINT16 DesiredFmt) {
  EFI_STATUS Status;
  UINT16 CurrentFmt;

  if (PciIo == NULL) return EFI_INVALID_PARAMETER;
  CurrentFmt = 0;
  Status = HdaMemRead16(PciIo, Sd + HDA_SD_FMT, &CurrentFmt);
  if (EFI_ERROR(Status)) return Status;
  return HdaMemWrite16(
           PciIo,
           Sd + HDA_SD_FMT,
           (UINT16)((CurrentFmt & HDA_SD_FMT_PRESERVE_MASK) |
                    (DesiredFmt & (UINT16)~HDA_SD_FMT_PRESERVE_MASK))
           );
}

STATIC EFI_STATUS HdaVerifyPreparedOutputStream(HDAU_CONTEXT *Context, BOOLEAN UsingBidirectional) {
  EFI_STATUS Status;
  UINT8 Ctl0;
  UINT8 Ctl2;
  UINT8 Sts;
  UINT32 Cbl;
  UINT16 Lvi;
  UINT16 Fmt;
  UINT32 Bdlpl;
  UINT32 Bdlpu;
  UINT16 Sd;
  UINT8 ExpectedCtl2;

  if (Context == NULL || Context->PciIo == NULL || Context->StreamOffset == 0) {
    return EFI_INVALID_PARAMETER;
  }
  Sd = Context->StreamOffset;
  Status = HdaMemRead8(Context->PciIo, Sd + HDA_SD_CTL, &Ctl0);
  if (EFI_ERROR(Status)) return Status;
  Status = HdaMemRead8(Context->PciIo, Sd + HDA_SD_CTL + 2, &Ctl2);
  if (EFI_ERROR(Status)) return Status;
  Status = HdaMemRead8(Context->PciIo, Sd + HDA_SD_STS, &Sts);
  if (EFI_ERROR(Status)) return Status;
  Status = HdaMemRead32(Context->PciIo, Sd + HDA_SD_CBL, &Cbl);
  if (EFI_ERROR(Status)) return Status;
  Status = HdaMemRead16(Context->PciIo, Sd + HDA_SD_LVI, &Lvi);
  if (EFI_ERROR(Status)) return Status;
  Status = HdaMemRead16(Context->PciIo, Sd + HDA_SD_FMT, &Fmt);
  if (EFI_ERROR(Status)) return Status;
  Status = HdaMemRead32(Context->PciIo, Sd + HDA_SD_BDLPL, &Bdlpl);
  if (EFI_ERROR(Status)) return Status;
  Status = HdaMemRead32(Context->PciIo, Sd + HDA_SD_BDLPU, &Bdlpu);
  if (EFI_ERROR(Status)) return Status;

  ExpectedCtl2 = (UINT8)(mHdaActiveStreamTag << 4);
  if (UsingBidirectional) ExpectedCtl2 |= HDA_SD_CTL3_DIR;
  if ((Ctl0 & (HDA_SD_CTL_RUN | HDA_SD_CTL_SRST)) != 0 ||
      (Sts & HDA_SD_STS_OWNERSHIP_MASK) != 0 ||
      (Ctl2 & (UINT8)0xF8U) != ExpectedCtl2 ||
      Cbl != (UINT32)Context->AudioBufferBytes ||
      (Lvi & HDA_SD_LVI_VALUE_MASK) != (UINT16)(HDAU_BDL_ENTRY_COUNT - 1U) ||
      (Fmt & (UINT16)~HDA_SD_FMT_PRESERVE_MASK) !=
        (Context->StreamFormat & (UINT16)~HDA_SD_FMT_PRESERVE_MASK) ||
      (Bdlpl & (UINT32)~HDA_SD_BDLPL_RSVDZ_MASK) !=
        ((UINT32)Context->BdlDeviceAddress & (UINT32)~HDA_SD_BDLPL_RSVDZ_MASK) ||
      (Bdlpl & HDA_SD_BDLPL_RSVDZ_MASK) != 0 ||
      Bdlpu != (Context->Controller64BitDma
                  ? (UINT32)(Context->BdlDeviceAddress >> 32)
                  : Context->OriginalStreamBdlpu)) {
    return EFI_DEVICE_ERROR;
  }
  return EFI_SUCCESS;
}

STATIC EFI_STATUS HdaVerifyRestoredStreamDescriptorState(HDAU_CONTEXT *Context) {
  EFI_STATUS Status;
  UINT16 Sd;
  UINT8 Ctl0;
  UINT8 Ctl2;
  UINT8 Sts;
  UINT32 Cbl;
  UINT16 Lvi;
  UINT16 Fmt;
  UINT32 Bdlpl;
  UINT32 Bdlpu;

  if (Context == NULL || Context->PciIo == NULL || Context->StreamOffset == 0 ||
      !Context->StreamStateValid) {
    return EFI_INVALID_PARAMETER;
  }
  Sd = Context->StreamOffset;
  Ctl0 = 0;
  Ctl2 = 0;
  Sts = 0;
  Cbl = 0;
  Lvi = 0;
  Fmt = 0;
  Bdlpl = 0;
  Bdlpu = 0;

  Status = HdaMemRead8(Context->PciIo, Sd + HDA_SD_CTL, &Ctl0);
  if (EFI_ERROR(Status)) return Status;
  Status = HdaMemRead8(Context->PciIo, Sd + HDA_SD_CTL + 2, &Ctl2);
  if (EFI_ERROR(Status)) return Status;
  Status = HdaMemRead8(Context->PciIo, Sd + HDA_SD_STS, &Sts);
  if (EFI_ERROR(Status)) return Status;
  Status = HdaMemRead32(Context->PciIo, Sd + HDA_SD_CBL, &Cbl);
  if (EFI_ERROR(Status)) return Status;
  Status = HdaMemRead16(Context->PciIo, Sd + HDA_SD_LVI, &Lvi);
  if (EFI_ERROR(Status)) return Status;
  Status = HdaMemRead16(Context->PciIo, Sd + HDA_SD_FMT, &Fmt);
  if (EFI_ERROR(Status)) return Status;
  Status = HdaMemRead32(Context->PciIo, Sd + HDA_SD_BDLPL, &Bdlpl);
  if (EFI_ERROR(Status)) return Status;
  Status = HdaMemRead32(Context->PciIo, Sd + HDA_SD_BDLPU, &Bdlpu);
  if (EFI_ERROR(Status)) return Status;

  /* RsvdP fields are intentionally compared only through the writable
     functional masks: Intel requires a read-modify-write of the *current*
     reserved value, not replay of a pre-reset reserved snapshot. */
  if ((Ctl0 & (HDA_SD_CTL_RUN | HDA_SD_CTL_SRST | HDA_SD_CTL_INTERRUPT_MASK)) !=
        (Context->OriginalStreamCtl0 & HDA_SD_CTL_INTERRUPT_MASK) ||
      (Sts & HDA_SD_STS_OWNERSHIP_MASK) != 0 ||
      Ctl2 != Context->OriginalStreamCtl2 ||
      Cbl != Context->OriginalStreamCbl ||
      (Lvi & HDA_SD_LVI_VALUE_MASK) !=
        (Context->OriginalStreamLvi & HDA_SD_LVI_VALUE_MASK) ||
      (Fmt & (UINT16)~HDA_SD_FMT_PRESERVE_MASK) !=
        (Context->OriginalStreamFmt & (UINT16)~HDA_SD_FMT_PRESERVE_MASK) ||
      (Bdlpl & (UINT32)~HDA_SD_BDLPL_RSVDZ_MASK) !=
        (Context->OriginalStreamBdlpl & (UINT32)~HDA_SD_BDLPL_RSVDZ_MASK) ||
      (Bdlpl & HDA_SD_BDLPL_RSVDZ_MASK) != 0 ||
      Bdlpu != Context->OriginalStreamBdlpu) {
    return EFI_DEVICE_ERROR;
  }
  return EFI_SUCCESS;
}

STATIC EFI_STATUS HdaRestoreStreamDescriptorState(HDAU_CONTEXT *Context) {
  EFI_STATUS Status;
  UINT16 Sd;
  UINT8 Ctl0;

  if (Context == NULL || Context->PciIo == NULL || Context->StreamOffset == 0 ||
      !Context->StreamStateValid) {
    return EFI_INVALID_PARAMETER;
  }
  Sd = Context->StreamOffset;

  /* CBL may only be reprogrammed after a stream reset once RUN has ever been
     asserted. Reset first, then restore DIR/stream-tag state before any other
     descriptor register as required for bidirectional engines. */
  Status = HdaResetStreamDescriptor(Context->PciIo, Sd);
  if (EFI_ERROR(Status)) return Status;
  Status = HdaMemWrite8(Context->PciIo, Sd + HDA_SD_CTL + 2, Context->OriginalStreamCtl2);
  if (EFI_ERROR(Status)) return Status;
  Status = HdaMemWrite32(Context->PciIo, Sd + HDA_SD_CBL, Context->OriginalStreamCbl);
  if (EFI_ERROR(Status)) return Status;
  Status = HdaWriteStreamLviPreserve(Context->PciIo, Sd, Context->OriginalStreamLvi);
  if (EFI_ERROR(Status)) return Status;
  Status = HdaWriteStreamFormatPreserve(Context->PciIo, Sd, Context->OriginalStreamFmt);
  if (EFI_ERROR(Status)) return Status;
  Status = HdaMemWrite32(
             Context->PciIo,
             Sd + HDA_SD_BDLPL,
             Context->OriginalStreamBdlpl & (UINT32)~HDA_SD_BDLPL_RSVDZ_MASK
             );
  if (EFI_ERROR(Status)) return Status;
  if (Context->Controller64BitDma) {
    Status = HdaMemWrite32(Context->PciIo, Sd + HDA_SD_BDLPU, Context->OriginalStreamBdlpu);
    if (EFI_ERROR(Status)) return Status;
  }

  Ctl0 = 0;
  Status = HdaMemRead8(Context->PciIo, Sd + HDA_SD_CTL, &Ctl0);
  if (EFI_ERROR(Status)) return Status;
  Ctl0 = (UINT8)((Ctl0 & HDA_SD_CTL_RSVDP_LOW_MASK) |
                 (Context->OriginalStreamCtl0 & HDA_SD_CTL_INTERRUPT_MASK));
  Status = HdaMemWrite8(Context->PciIo, Sd + HDA_SD_CTL, Ctl0);
  if (EFI_ERROR(Status)) return Status;
  Status = HdaPciFlushExact(Context->PciIo);
  if (EFI_ERROR(Status)) return Status;
  Status = HdaVerifyRestoredStreamDescriptorState(Context);
  if (EFI_ERROR(Status)) return Status;

  Context->StreamStateValid = FALSE;
  return EFI_SUCCESS;
}

STATIC EFI_STATUS HdaPrepareOutputStream(HDAU_CONTEXT *Context) {
  EFI_STATUS Status;
  EFI_PCI_IO_PROTOCOL *PciIo;
  UINT16 Gcap;
  UINT8 Iss;
  UINT8 Oss;
  UINT8 OutputIndex;
  UINT8 BidirectionalIndex;
  UINT8 Bss;
  UINT8 TotalStreams;
  UINT8 StreamIndex;
  UINT8 Tag;
  UINT16 Sd;
  UINT8 Ctl;
  UINT8 Ctl2;
  UINT8 Ctl2After;
  UINT8 CandidateCtlAfter;
  UINT8 CandidateSts;
  BOOLEAN UsedTags[16];
  BOOLEAN FoundFree;
  BOOLEAN UsingBidirectional;

  if (Context == NULL || Context->PciIo == NULL) return EFI_INVALID_PARAMETER;
  Context->StreamOffset = 0;
  Context->StreamStateValid = FALSE;
  Context->Controller64BitDma = FALSE;
  PciIo = Context->PciIo;
  Status = HdaMemRead16(PciIo, HDA_REG_GCAP, &Gcap);
  if (EFI_ERROR(Status)) return Status;
  Context->Controller64BitDma = ((Gcap & HDA_GCAP_64OK) != 0);
  Iss = (UINT8)((Gcap >> 8) & 0x0F);
  Oss = (UINT8)((Gcap >> 12) & 0x0F);
  Bss = (UINT8)((Gcap >> HDA_GCAP_BSS_SHIFT) & HDA_GCAP_BSS_MASK);
  if (Oss == 0 && Bss == 0) return EFI_NOT_FOUND;
  if (!Context->Controller64BitDma &&
      ((Context->AudioBufferBytes != 0 &&
        ((UINT64)(Context->AudioBufferBytes - 1U) > 0xFFFFFFFFULL ||
         Context->AudioDeviceAddress > (0xFFFFFFFFULL - (UINT64)(Context->AudioBufferBytes - 1U)))) ||
       (Context->BdlBytes != 0 &&
        ((UINT64)(Context->BdlBytes - 1U) > 0xFFFFFFFFULL ||
         Context->BdlDeviceAddress > (0xFFFFFFFFULL - (UINT64)(Context->BdlBytes - 1U)))))) {
    HdaLog("[HDAU] controller lacks 64-bit DMA but mapped range exceeds 4 GiB: audio=0x%lx/%u bdl=0x%lx/%u",
           (UINT64)Context->AudioDeviceAddress, (UINT32)Context->AudioBufferBytes,
           (UINT64)Context->BdlDeviceAddress, (UINT32)Context->BdlBytes);
    return EFI_UNSUPPORTED;
  }

  /* Treat every non-zero stream tag as reserved, including descriptors that
     are currently stopped. Firmware can resume an idle descriptor while our
     stream is active; reusing its tag would then create a live link collision. */
  ZeroMem(UsedTags, sizeof(UsedTags));
  TotalStreams = (UINT8)(Iss + Oss + Bss);
  for (StreamIndex = 0; StreamIndex < TotalStreams; StreamIndex++) {
    UINT16 Candidate = (UINT16)(HDA_REG_SD_BASE + (StreamIndex * HDA_SD_SIZE));
    Status = HdaMemRead8(PciIo, Candidate + HDA_SD_CTL, &Ctl);
    if (EFI_ERROR(Status)) return Status;
    if ((Ctl & HDA_SD_CTL_SRST) != 0) return EFI_ACCESS_DENIED;
    Status = HdaMemRead8(PciIo, Candidate + HDA_SD_CTL + 2, &Ctl2);
    if (EFI_ERROR(Status)) return Status;
    Status = HdaMemRead8(PciIo, Candidate + HDA_SD_CTL, &CandidateCtlAfter);
    if (EFI_ERROR(Status)) return Status;
    if ((CandidateCtlAfter & HDA_SD_CTL_SRST) != 0 || CandidateCtlAfter != Ctl) {
      return EFI_ACCESS_DENIED;
    }
    Status = HdaMemRead8(PciIo, Candidate + HDA_SD_CTL + 2, &Ctl2After);
    if (EFI_ERROR(Status)) return Status;
    if (Ctl2After != Ctl2) return EFI_ACCESS_DENIED;
    Tag = (UINT8)((Ctl2 >> 4) & 0x0F);
    if (Tag != 0) UsedTags[Tag] = TRUE;
  }
  mHdaActiveStreamTag = 0;
  for (Tag = 1; Tag < 16; Tag++) {
    if (!UsedTags[Tag]) {
      mHdaActiveStreamTag = Tag;
      break;
    }
  }
  if (mHdaActiveStreamTag == 0) return EFI_NOT_READY;

  /* RUN=0 alone is not sufficient ownership: an idle descriptor can still be
     in reset or carry pending status belonging to firmware. Skip ambiguous
     descriptors instead of clearing their state as a side effect of playback. */
  FoundFree = FALSE;
  UsingBidirectional = FALSE;
  BidirectionalIndex = 0;
  Sd = 0;
  for (OutputIndex = 0; OutputIndex < Oss; OutputIndex++) {
    UINT16 Candidate = (UINT16)(HDA_REG_SD_BASE + ((Iss + OutputIndex) * HDA_SD_SIZE));
    Status = HdaMemRead8(PciIo, Candidate + HDA_SD_CTL, &Ctl);
    if (EFI_ERROR(Status)) return Status;
    if ((Ctl & (HDA_SD_CTL_RUN | HDA_SD_CTL_SRST)) != 0) continue;
    Status = HdaMemRead8(PciIo, Candidate + HDA_SD_STS, &CandidateSts);
    if (EFI_ERROR(Status)) return Status;
    if ((CandidateSts & HDA_SD_STS_OWNERSHIP_MASK) != 0) continue;
    Sd = Candidate;
    FoundFree = TRUE;
    break;
  }

  /* A controller may expose only bidirectional stream descriptors, or all
     dedicated output descriptors may already be in use. Intel HDA defines
     DIR=1 as output; it must be programmed immediately after stream reset and
     before any other stream-descriptor register is programmed. */
  if (!FoundFree) {
    for (BidirectionalIndex = 0; BidirectionalIndex < Bss; BidirectionalIndex++) {
      UINT16 Candidate = (UINT16)(HDA_REG_SD_BASE + ((Iss + Oss + BidirectionalIndex) * HDA_SD_SIZE));
      Status = HdaMemRead8(PciIo, Candidate + HDA_SD_CTL, &Ctl);
      if (EFI_ERROR(Status)) return Status;
      if ((Ctl & (HDA_SD_CTL_RUN | HDA_SD_CTL_SRST)) != 0) continue;
      Status = HdaMemRead8(PciIo, Candidate + HDA_SD_STS, &CandidateSts);
      if (EFI_ERROR(Status)) return Status;
      if ((CandidateSts & HDA_SD_STS_OWNERSHIP_MASK) != 0) continue;
      Sd = Candidate;
      FoundFree = TRUE;
      UsingBidirectional = TRUE;
      break;
    }
  }
  if (!FoundFree) return EFI_NOT_READY;

  HdaLog("[HDAU] stream: GCAP=0x%x ISS=%u OSS=%u BSS=%u descriptor=%a index=%u streamTag=%u using SD=0x%x",
         Gcap, Iss, Oss, Bss, UsingBidirectional ? "bidirectional-output" : "dedicated-output",
         UsingBidirectional ? BidirectionalIndex : OutputIndex, mHdaActiveStreamTag, Sd);

  /* Borrow an idle descriptor transactionally. Capture its configuration and
     prove it did not change across the snapshot before performing the first
     mutation. This prevents a firmware callback from starting/reconfiguring a
     stream between discovery and our stream reset. */
  Status = HdaCaptureIdleStreamState(Context, Sd);
  if (EFI_ERROR(Status)) return Status;
  Status = HdaRequireIdleStreamSnapshotUnchanged(Context, Sd);
  if (EFI_ERROR(Status)) {
    Context->StreamStateValid = FALSE;
    return Status;
  }
  Status = HdaRequireStreamTagAvailable(PciIo, Sd, mHdaActiveStreamTag);
  if (EFI_ERROR(Status)) {
    Context->StreamStateValid = FALSE;
    return Status;
  }

  /* Publish ownership at the first operation that can mutate the descriptor.
     From this point every failure path can restore or safely reset the stream. */
  Context->StreamOffset = Sd;
  Context->StreamBidirectional = UsingBidirectional;
  Status = HdaResetStreamDescriptor(PciIo, Sd);
  if (EFI_ERROR(Status)) return Status;

  Status = HdaMemRead8(PciIo, Sd + HDA_SD_CTL + 2, &Ctl2);
  if (EFI_ERROR(Status)) return Status;
  Ctl2 &= 0x07U; /* TP/STRIPE only; stream number and DIR are set explicitly. */
  if (UsingBidirectional) Ctl2 |= HDA_SD_CTL3_DIR;
  Ctl2 |= (UINT8)(mHdaActiveStreamTag << 4);
  Status = HdaMemWrite8(PciIo, Sd + HDA_SD_CTL + 2, Ctl2);
  if (EFI_ERROR(Status)) return Status;

  Status = HdaMemWrite32(PciIo, Sd + HDA_SD_CBL, (UINT32)Context->AudioBufferBytes);
  if (EFI_ERROR(Status)) return Status;
  Status = HdaWriteStreamLviPreserve(PciIo, Sd, (UINT16)(HDAU_BDL_ENTRY_COUNT - 1U));
  if (EFI_ERROR(Status)) return Status;
  Status = HdaWriteStreamFormatPreserve(PciIo, Sd, Context->StreamFormat);
  if (EFI_ERROR(Status)) return Status;
  Status = HdaMemWrite32(
             PciIo,
             Sd + HDA_SD_BDLPL,
             ((UINT32)Context->BdlDeviceAddress & (UINT32)~HDA_SD_BDLPL_RSVDZ_MASK)
             );
  if (EFI_ERROR(Status)) return Status;
  if (Context->Controller64BitDma) {
    Status = HdaMemWrite32(PciIo, Sd + HDA_SD_BDLPU, (UINT32)(Context->BdlDeviceAddress >> 32));
    if (EFI_ERROR(Status)) return Status;
  }
  Status = HdaPciFlushExact(PciIo);
  if (EFI_ERROR(Status)) return Status;

  /* Do not arm DMA if a controller ignored or corrupted any critical stream
     programming write. This turns a dangerous stale-BDL condition into a
     contained setup failure before RUN can be asserted. */
  Status = HdaVerifyPreparedOutputStream(Context, UsingBidirectional);
  if (EFI_ERROR(Status)) return Status;

  HdaLog("[HDAU] stream prepared: CBL=%u LVI=%u FMT=0x%x BDL=0x%lx",
         (UINT32)Context->AudioBufferBytes, (UINT32)(HDAU_BDL_ENTRY_COUNT - 1U), Context->StreamFormat,
         (UINT64)Context->BdlDeviceAddress);
  return EFI_SUCCESS;
}


STATIC BOOLEAN HdaMonitorPlaybackProgress(HDAU_CONTEXT *Context, UINTN DurationUsec) {
  UINTN  Elapsed;
  UINTN  Step;
  UINTN  Tick;
  UINT32 Lpib;
  UINT32 LastLpib;
  UINT8  Sts;
  UINT8  Ctl;
  BOOLEAN HaveLpibBaseline;
  BOOLEAN Moved;
  BOOLEAN EndReached;
  UINT32 ChimeEndBytes;
  UINT32 StopBytes;
  UINT32 EndGuardBytes;

  if (Context == NULL || Context->PciIo == NULL || Context->StreamOffset == 0 ||
      Context->AudioBufferBytes == 0 || Context->AudioBufferBytes > (UINTN)MAX_UINT32) {
    return FALSE;
  }

  Elapsed = 0;
  Step = 5000;
  Tick = 0;
  LastLpib = 0;
  HaveLpibBaseline = FALSE;
  Moved = FALSE;
  EndReached = FALSE;
  ChimeEndBytes = (UINT32)Context->PcmBytes;
  EndGuardBytes = 0; /* Fix31: no early-stop guard; wait for full post-chime drain. */
  StopBytes = ChimeEndBytes + (UINT32)((Context->BytesPerSecond *
                    (mHdaCfg.FastBoot ? HDAU_POST_CHIME_DRAIN_MS : HDAU_WAIT_CHIME_DRAIN_MS)) / 1000ULL);
  if (Context->AudioBufferBytes > EndGuardBytes && StopBytes > (UINT32)(Context->AudioBufferBytes - EndGuardBytes)) {
    StopBytes = (UINT32)(Context->AudioBufferBytes - EndGuardBytes);
  }
  if (StopBytes < ChimeEndBytes) StopBytes = ChimeEndBytes;

  while (Elapsed < DurationUsec) {
    HdaStall(Step);
    Elapsed += Step;
    Tick++;

    if (EFI_ERROR(HdaMemRead32(Context->PciIo, Context->StreamOffset + HDA_SD_LPIB, &Lpib)) ||
        EFI_ERROR(HdaMemRead8(Context->PciIo, Context->StreamOffset + HDA_SD_STS, &Sts)) ||
        EFI_ERROR(HdaMemRead8(Context->PciIo, Context->StreamOffset + HDA_SD_CTL, &Ctl))) {
      HdaLog("[HDAU] playback progress: MMIO read failed at elapsed=%u ms", (UINT32)(Elapsed / 1000));
      break;
    }

    if (Lpib > (UINT32)Context->AudioBufferBytes) {
      HdaLog("[HDAU] playback stream fault: LPIB outside CBL, elapsed=%u ms lpib=%u cbl=%u",
             (UINT32)(Elapsed / 1000), Lpib, (UINT32)Context->AudioBufferBytes);
      Moved = FALSE;
      break;
    }

    /* The first valid sample establishes the descriptor baseline only. A
       stale pre-start LPIB must not be accepted as proof of DMA movement or
       as successful completion. */
    if (!HaveLpibBaseline) {
      LastLpib = Lpib;
      HaveLpibBaseline = TRUE;
    } else if (Lpib != LastLpib) {
      Moved = TRUE;
      LastLpib = Lpib;
    }

    if (Moved && Lpib >= StopBytes) {
      EndReached = TRUE;
    }

    if ((Tick % 50) == 0 || EndReached || (Sts & HDA_SD_STS_BCIS) != 0 || Elapsed >= DurationUsec) {
      HdaLog("[HDAU] playback progress: elapsed=%u ms lpib=%u sts=0x%x ctl=0x%x moved=%u end=%u",
             (UINT32)(Elapsed / 1000), Lpib, Sts, Ctl, Moved ? 1 : 0, EndReached ? 1 : 0);
    }

    if ((Sts & (HDA_SD_STS_FIFOE | HDA_SD_STS_DESE)) != 0 ||
        (((Ctl & HDA_SD_CTL_RUN) == 0) && !EndReached)) {
      HdaLog("[HDAU] playback stream fault: elapsed=%u ms lpib=%u sts=0x%x ctl=0x%x",
             (UINT32)(Elapsed / 1000), Lpib, Sts, Ctl);
      Moved = FALSE;
      break;
    }

    if (EndReached) {
      HdaLog("[HDAU] playback stop: reached safe post-chime drain before BDL wrap, lpib=%u chimeBytes=%u stopBytes=%u cbl=%u",
             Lpib, ChimeEndBytes, StopBytes, (UINT32)Context->AudioBufferBytes);
      break;
    }

  }

  if (Moved && !EndReached) {
    HdaLog("[HDAU] playback incomplete: DMA moved but safe stop point was not reached before timeout");
  }
  return (Moved && EndReached);
}

STATIC EFI_STATUS HdaStartOutputStream(HDAU_CONTEXT *Context) {
  EFI_STATUS Status;
  UINT8 Ctl;
  UINT8 Ctl2;
  UINT8 Sts;

  if (Context == NULL || Context->PciIo == NULL || Context->StreamOffset == 0) return EFI_INVALID_PARAMETER;
  Status = HdaRequireStreamTagAvailable(Context->PciIo, Context->StreamOffset, mHdaActiveStreamTag);
  if (EFI_ERROR(Status)) return Status;
  /* Codec programming and PCI-attribute transitions occur after descriptor
     preparation and may invoke firmware. Revalidate the complete DMA
     descriptor immediately before RUN so a late mutation of CBL/BDL/FMT/tag
     cannot launch DMA against state this driver no longer owns. */
  Status = HdaVerifyPreparedOutputStream(Context, Context->StreamBidirectional);
  if (EFI_ERROR(Status)) return Status;
  Status = HdaMemRead8(Context->PciIo, Context->StreamOffset + HDA_SD_CTL + 2, &Ctl2);
  if (EFI_ERROR(Status)) return Status;
  if (((Ctl2 >> 4) & 0x0F) != mHdaActiveStreamTag) return EFI_ACCESS_DENIED;
  Status = HdaMemRead8(Context->PciIo, Context->StreamOffset + HDA_SD_STS, &Sts);
  if (EFI_ERROR(Status)) return Status;
  if ((Sts & (HDA_SD_STS_FIFOE | HDA_SD_STS_DESE)) != 0) return EFI_DEVICE_ERROR;
  Status = HdaMemRead8(Context->PciIo, Context->StreamOffset + HDA_SD_CTL, &Ctl);
  if (EFI_ERROR(Status)) return Status;
  if ((Ctl & HDA_SD_CTL_SRST) != 0) return EFI_NOT_READY;
  Ctl |= HDA_SD_CTL_RUN;
  Status = HdaMemWrite8(Context->PciIo, Context->StreamOffset + HDA_SD_CTL, Ctl);
  if (EFI_ERROR(Status)) return Status;
  Status = HdaPciFlushExact(Context->PciIo);
  if (EFI_ERROR(Status)) return Status;
  Status = HdaMemRead8(Context->PciIo, Context->StreamOffset + HDA_SD_CTL, &Ctl);
  if (EFI_ERROR(Status)) return Status;
  return ((Ctl & HDA_SD_CTL_RUN) != 0) ? EFI_SUCCESS : EFI_DEVICE_ERROR;
}


STATIC EFI_STATUS HdaGetPciAttributesExact(EFI_PCI_IO_PROTOCOL *PciIo, UINT64 *Attributes) {
  EFI_STATUS Status;

  if (PciIo == NULL || Attributes == NULL) return EFI_INVALID_PARAMETER;
  *Attributes = 0;
  Status = PciIo->Attributes(
                    PciIo,
                    EfiPciIoAttributeOperationGet,
                    0,
                    Attributes
                    );
  if (Status == EFI_SUCCESS) return EFI_SUCCESS;
  return EFI_ERROR(Status) ? Status : EFI_DEVICE_ERROR;
}

STATIC EFI_STATUS HdaVerifyPciAttributeBits(
  EFI_PCI_IO_PROTOCOL *PciIo,
  UINT64 ExpectedAttributes,
  UINT64 Mask
) {
  EFI_STATUS Status;
  UINT64 CurrentAttributes;

  if (PciIo == NULL) return EFI_INVALID_PARAMETER;
  CurrentAttributes = 0;
  Status = HdaGetPciAttributesExact(PciIo, &CurrentAttributes);
  if (EFI_ERROR(Status)) return Status;
  if ((CurrentAttributes & Mask) != (ExpectedAttributes & Mask)) return EFI_DEVICE_ERROR;
  return EFI_SUCCESS;
}

STATIC EFI_STATUS HdaApplyPciAttributeBits(
  EFI_PCI_IO_PROTOCOL *PciIo,
  UINT64 DesiredAttributes,
  UINT64 Mask
) {
  EFI_STATUS Status;
  UINT64 CurrentAttributes;
  UINT64 DisableMask;
  UINT64 EnableMask;

  if (PciIo == NULL) return EFI_INVALID_PARAMETER;
  if (Mask == 0) return EFI_SUCCESS;

  /* UEFI PCI I/O Attributes(Set) replaces the controller's entire current
     attribute mask.  This driver only owns the bits it changed, so rollback
     must use Disable/Enable deltas and preserve unrelated attributes that may
     have changed since the snapshot.  Disable first for the fail-safe case
     where BUS_MASTER is one of the managed bits. */
  CurrentAttributes = 0;
  Status = HdaGetPciAttributesExact(PciIo, &CurrentAttributes);
  if (EFI_ERROR(Status)) return Status;

  DisableMask = (CurrentAttributes & Mask) & ~(DesiredAttributes & Mask);
  if (DisableMask != 0) {
    Status = PciIo->Attributes(
                      PciIo,
                      EfiPciIoAttributeOperationDisable,
                      DisableMask,
                      NULL
                      );
    if (Status != EFI_SUCCESS) return EFI_ERROR(Status) ? Status : EFI_DEVICE_ERROR;
  }

  CurrentAttributes = 0;
  Status = HdaGetPciAttributesExact(PciIo, &CurrentAttributes);
  if (EFI_ERROR(Status)) return Status;
  EnableMask = (DesiredAttributes & Mask) & ~(CurrentAttributes & Mask);
  if (EnableMask != 0) {
    Status = PciIo->Attributes(
                      PciIo,
                      EfiPciIoAttributeOperationEnable,
                      EnableMask,
                      NULL
                      );
    if (Status != EFI_SUCCESS) return EFI_ERROR(Status) ? Status : EFI_DEVICE_ERROR;
  }

  return HdaVerifyPciAttributeBits(PciIo, DesiredAttributes, Mask);
}

STATIC EFI_STATUS HdaRestorePciAttributes(HDAU_CONTEXT *Context) {
  EFI_STATUS Status;
  UINT64 DesiredAttributes;
  UINT64 ManagedMask;

  if (Context == NULL || Context->PciIo == NULL) return EFI_INVALID_PARAMETER;
  if (!Context->HaveOriginalPciAttributes) return EFI_SUCCESS;

  /* The normal playback path only enables MEMORY/BUS_MASTER. Restore only
     bits that were clear in the snapshot and therefore could actually have
     been changed by this driver. This avoids reverting a later change made by
     another firmware agent to a bit that was already enabled when we arrived. */
  ManagedMask = (EFI_PCI_IO_ATTRIBUTE_MEMORY | EFI_PCI_IO_ATTRIBUTE_BUS_MASTER) &
                ~Context->OriginalPciAttributes;
  DesiredAttributes = Context->OriginalPciAttributes;
  if (Context->PciDmaContainment) {
    /* Containment deliberately owns BUS_MASTER even when it was originally
       enabled. Keep it off until the operating system reinitializes HDA, while
       still rolling back any MEMORY bit that this driver itself enabled. */
    ManagedMask |= EFI_PCI_IO_ATTRIBUTE_BUS_MASTER;
    DesiredAttributes &= ~((UINT64)EFI_PCI_IO_ATTRIBUTE_BUS_MASTER);
  }

  Status = HdaApplyPciAttributeBits(Context->PciIo, DesiredAttributes, ManagedMask);
  if (EFI_ERROR(Status)) {
    HdaLog("[HDAU] PCI managed-attribute restore failed: %r", Status);
    return Status;
  }
  HdaDischargeRetainedCorbRirbBusMasterRestore(
    Context->PciIo,
    DesiredAttributes,
    ManagedMask
    );
  Context->HaveOriginalPciAttributes = FALSE;
  return EFI_SUCCESS;
}


STATIC EFI_STATUS HdaRetainContextForCleanup(HDAU_CONTEXT *Context, CONST CHAR8 *Reason) {
  if (Context == NULL || Context->PciIo == NULL) return EFI_INVALID_PARAMETER;
  if (mHdaRetainedContextValid && Context != &mHdaRetainedContext) return EFI_ALREADY_STARTED;
  if (mHdaAsyncContext != NULL && mHdaAsyncContext != Context &&
      mHdaAsyncContext != &mHdaRetainedContext) {
    return EFI_ALREADY_STARTED;
  }
  if (Context != &mHdaRetainedContext) {
    CopyMem(&mHdaRetainedContext, Context, sizeof(mHdaRetainedContext));
  }
  mHdaRetainedContext.HeapAllocated = FALSE;
  mHdaRetainedContextValid = TRUE;
  mHdaAsyncContext = &mHdaRetainedContext;
  HdaLog("[HDAU] cleanup context retained reason=%a stream=0x%x",
         Reason == NULL ? "unspecified" : Reason, mHdaRetainedContext.StreamOffset);
  return EFI_SUCCESS;
}

STATIC EFI_STATUS HdaStopOutputStream(HDAU_CONTEXT *Context) {
  EFI_STATUS Status;
  EFI_STATUS ResetStatus;
  EFI_PCI_IO_PROTOCOL *PciIo;
  UINT16 Sd;
  UINT8 Ctl;
  UINTN Index;
  BOOLEAN Stopped;

  if (Context == NULL || Context->PciIo == NULL || Context->StreamOffset == 0) return EFI_INVALID_PARAMETER;
  PciIo = Context->PciIo;
  Sd = Context->StreamOffset;
  Ctl = 0;
  Stopped = FALSE;

  Status = HdaMemRead8(PciIo, Sd + HDA_SD_CTL, &Ctl);
  if (!EFI_ERROR(Status)) {
    Ctl &= (UINT8)~HDA_SD_CTL_RUN;
    Status = HdaMemWrite8(PciIo, Sd + HDA_SD_CTL, Ctl);
  }
  if (!EFI_ERROR(Status)) Status = HdaPciFlushExact(PciIo);

  if (!EFI_ERROR(Status)) {
    for (Index = 0; Index < (HDAU_STREAM_STOP_TIMEOUT_US / HDAU_POLL_GRANULARITY_US); Index++) {
      Status = HdaMemRead8(PciIo, Sd + HDA_SD_CTL, &Ctl);
      if (EFI_ERROR(Status)) break;
      if ((Ctl & HDA_SD_CTL_RUN) == 0) {
        Stopped = TRUE;
        break;
      }
      HdaStall(HDAU_POLL_GRANULARITY_US);
    }
    if (!Stopped && !EFI_ERROR(Status)) Status = EFI_TIMEOUT;
  }

  if (!Stopped) {
    EFI_STATUS ContainStatus;

    HdaLog("[HDAU] stream stop could not be confirmed: %r; attempting controller reset", Status);
    ResetStatus = HdaControllerReset(PciIo);
    if (EFI_ERROR(ResetStatus)) {
      HdaLog("[HDAU] controller reset after stream-stop failure also failed: %r", ResetStatus);

      /* DMA containment owns exactly one PCI attribute: BUS_MASTER.  Disable
         only that bit so unrelated attributes remain untouched. */
      ContainStatus = PciIo->Attributes(
                               PciIo,
                               EfiPciIoAttributeOperationDisable,
                               EFI_PCI_IO_ATTRIBUTE_BUS_MASTER,
                               NULL
                               );
      if (ContainStatus != EFI_SUCCESS) {
        if (!EFI_ERROR(ContainStatus)) ContainStatus = EFI_DEVICE_ERROR;
        HdaLog("[HDAU] PCI bus-master containment also failed: %r", ContainStatus);
        /* Containment failure is the safety-critical result. Returning the
           earlier stream-stop error here would hide the fact that DMA could
           not be fenced after both stop and controller reset failed. */
        return ContainStatus;
      }
      ContainStatus = HdaVerifyPciAttributeBits(
                        PciIo,
                        0,
                        EFI_PCI_IO_ATTRIBUTE_BUS_MASTER
                        );
      if (EFI_ERROR(ContainStatus)) {
        HdaLog("[HDAU] PCI bus-master containment read-back failed: %r", ContainStatus);
        return ContainStatus;
      }

      Context->PciDmaContainment = TRUE;
      Context->StreamOffset = 0;
      Context->StreamStateValid = FALSE;
      Context->StreamBidirectional = FALSE;
      HdaLog("[HDAU] DMA contained by disabling PCI bus mastering after stop/reset failure");
      return EFI_SUCCESS;
    }
    Context->StreamOffset = 0;
    Context->StreamStateValid = FALSE;
    Context->StreamBidirectional = FALSE;
    return EFI_SUCCESS;
  }

  if (mHdaExitBootServicesInProgress) {
    /* RUN has been read back clear. Descriptor register scrubbing is not
       required to prevent DMA and would add external PCI I/O calls inside an
       EXIT_BOOT_SERVICES notification. The OS will reinitialize the stream. */
    Context->StreamOffset = 0;
    Context->StreamStateValid = FALSE;
    Context->StreamBidirectional = FALSE;
    return EFI_SUCCESS;
  }

  /* CBL cannot legally be rewritten merely because RUN became zero after it
     has run; Intel HDA requires another stream reset first. Restore the idle
     descriptor transactionally rather than zeroing firmware's prior state. */
  Status = HdaRestoreStreamDescriptorState(Context);
  if (EFI_ERROR(Status)) {
    /* RUN was already read back clear, so the descriptor no longer fetches
       the BDL and its DMA buffers can be released safely. Do not escalate a
       local restore failure into CRST: Intel HDA global reset destroys nearly
       all controller state and can disrupt firmware-owned streams/transport.
       Preserve the concrete degradation status instead so callers do not
       replay the chime on another codec/controller merely because
       post-playback state restore was degraded. */
    HdaLog("[HDAU] stream descriptor restore/read-back failed after RUN stopped: %r; global controller reset intentionally avoided", Status);
    if (Context->PostPlaybackCleanupStatus == EFI_SUCCESS) {
      Context->PostPlaybackCleanupStatus = Status;
    }
    Context->StreamOffset = 0;
    Context->StreamStateValid = FALSE;
    Context->StreamBidirectional = FALSE;
    return EFI_SUCCESS;
  }
  Context->StreamOffset = 0;
  Context->StreamStateValid = FALSE;
  Context->StreamBidirectional = FALSE;
  return EFI_SUCCESS;
}

STATIC EFI_STATUS HdaReleasePreparedStreamAndBuffers(HDAU_CONTEXT *Context, EFI_STATUS FailureStatus) {
  EFI_STATUS StopStatus;
  EFI_STATUS RestoreStatus;

  if (Context == NULL) return EFI_INVALID_PARAMETER;
  if (Context->StreamOffset != 0) {
    StopStatus = HdaStopOutputStream(Context);
    if (EFI_ERROR(StopStatus)) {
      EFI_STATUS RetainStatus = HdaRetainContextForCleanup(Context, "prepared-stream-cleanup");
      HdaLog("[HDAU] prepared stream cleanup failed; DMA buffers retained: %r retain=%r", StopStatus, RetainStatus);
      return EFI_ERROR(RetainStatus) ? RetainStatus : StopStatus;
    }
  }
  RestoreStatus = Context->PciDmaContainment
                    ? EFI_SUCCESS
                    : HdaRestoreCodecStateRetry(Context, "prepared-stream-release");
  return HdaFinalizePlaybackContext(Context, "prepared-stream-release",
                                    FailureStatus, RestoreStatus);
}


STATIC VOID EFIAPI HdaAsyncPlaybackTick(IN EFI_EVENT Event, IN VOID *EventContext) {
  HDAU_CONTEXT *Context;
  UINT32 Lpib;
  UINT8 Sts;
  UINT8 Ctl;
  UINT32 ChimeEndBytes;
  UINT32 StopBytes;
  UINT32 EndGuardBytes;
  UINTN DurationUsec;
  BOOLEAN Done;
  BOOLEAN ReachedStop;
  BOOLEAN NoProgressTimeout;
  BOOLEAN HardTimeout;
  EFI_STATUS StopStatus;
  EFI_STATUS RestoreStatus;
  EFI_STATUS EventCloseStatus;
  BOOLEAN BuffersReleased;
  BOOLEAN OpenedLog;
  BOOLEAN SavedFileLog;

  Context = (HDAU_CONTEXT *)EventContext;
  if (Context == NULL || Context != mHdaAsyncContext) return;

  /* The periodic FastBoot event runs at TPL_CALLBACK. Do not let it preempt
     the still-active DXE entry path, which may be writing/flushing/closing the
     same global diagnostic sink. The periodic timer naturally retries after
     entry releases this gate. Playback timeout accounting intentionally starts
     only when the worker is allowed to run. */
  if (mHdaEntryWorkInProgress) return;

  /* A prior cleanup attempt may have stopped the stream but retained this
     context because codec/DMA resource cleanup could not be completed. The
     timer event itself is terminally closed and never retried. Never touch
     stream registers again in that state; retry only owned resource teardown. */
  if (Context->StreamOffset == 0) {
    EFI_STATUS CleanupStatus = HdaCleanupAsyncPlayback(FALSE);
    if (EFI_ERROR(CleanupStatus)) {
      HdaLog("[HDAU] async cleanup retry remains pending: %r", CleanupStatus);
    }
    return;
  }

  mHdaAsyncElapsedUsec += HDAU_ASYNC_PLAYBACK_TICK_US;
  Lpib = 0;
  Sts = 0;
  Ctl = 0;
  if (EFI_ERROR(HdaMemRead32(Context->PciIo, Context->StreamOffset + HDA_SD_LPIB, &Lpib)) ||
      EFI_ERROR(HdaMemRead8(Context->PciIo, Context->StreamOffset + HDA_SD_STS, &Sts)) ||
      EFI_ERROR(HdaMemRead8(Context->PciIo, Context->StreamOffset + HDA_SD_CTL, &Ctl))) {
    mHdaAsyncFault = TRUE;
  }
  if ((Sts & (HDA_SD_STS_FIFOE | HDA_SD_STS_DESE)) != 0 ||
      ((Ctl & HDA_SD_CTL_RUN) == 0)) {
    mHdaAsyncFault = TRUE;
  }

  if (!mHdaAsyncFault) {
    if (Context->AudioBufferBytes == 0 || Context->AudioBufferBytes > (UINTN)MAX_UINT32 ||
        Lpib > (UINT32)Context->AudioBufferBytes) {
      HdaLog("[HDAU] async playback stream fault: LPIB outside CBL, elapsed=%u ms lpib=%u cbl=%u",
             (UINT32)(mHdaAsyncElapsedUsec / 1000), Lpib, (UINT32)Context->AudioBufferBytes);
      mHdaAsyncFault = TRUE;
    } else if (!mHdaAsyncHaveLpibBaseline) {
      /* Establish a trustworthy baseline before counting progress. */
      mHdaAsyncLastLpib = Lpib;
      mHdaAsyncHaveLpibBaseline = TRUE;
    } else if (Lpib != mHdaAsyncLastLpib) {
      mHdaAsyncMoved = TRUE;
      mHdaAsyncLastLpib = Lpib;
      mHdaAsyncLastProgressUsec = mHdaAsyncElapsedUsec;
    }
  }

  ChimeEndBytes = (UINT32)Context->PcmBytes;
  EndGuardBytes = 0; /* Fix31: no early-stop guard; wait for full post-chime drain. */
  DurationUsec = (UINTN)HDAU_ASYNC_HARD_TIMEOUT_MS * 1000;

  ReachedStop = FALSE;
  NoProgressTimeout = FALSE;
  HardTimeout = FALSE;
  StopBytes = ChimeEndBytes + (UINT32)((Context->BytesPerSecond *
                    (mHdaCfg.FastBoot ? HDAU_POST_CHIME_DRAIN_MS : HDAU_WAIT_CHIME_DRAIN_MS)) / 1000ULL);
  if (Context->AudioBufferBytes > EndGuardBytes && StopBytes > (UINT32)(Context->AudioBufferBytes - EndGuardBytes)) {
    StopBytes = (UINT32)(Context->AudioBufferBytes - EndGuardBytes);
  }
  if (StopBytes < ChimeEndBytes) StopBytes = ChimeEndBytes;

  /*
    Fix28: never stop async playback based only on the expected PCM wall-clock
    duration. Some firmwares start the stream/timer with skew, and that can cut
    the last part of the chime even while the picker is already visible. The
    normal stop condition is now LPIB reaching chime end + the full drain.
    There is no early guard here; that avoids cutting the very last transient
    of the original BootChime. The shorter watchdog detects a DMA position
    that never starts or stops advancing; the longer wall-clock path remains
    the final leak-prevention timeout.
  */
  if (mHdaAsyncMoved && Lpib >= StopBytes) ReachedStop = TRUE;
  if ((mHdaAsyncElapsedUsec - mHdaAsyncLastProgressUsec) >=
      ((UINTN)HDAU_ASYNC_NO_PROGRESS_TIMEOUT_MS * 1000)) {
    NoProgressTimeout = TRUE;
  }
  if (mHdaAsyncElapsedUsec >= DurationUsec) HardTimeout = TRUE;
  Done = (mHdaAsyncFault || ReachedStop || NoProgressTimeout || HardTimeout);

  if (!Done) return;

  OpenedLog = FALSE;
  SavedFileLog = mHdaCfg.FileLog;
  if (mHdaCfg.FileLog) {
    (VOID)HdaEnsureAppendLog(&OpenedLog, &SavedFileLog);
  }
  StopStatus = HdaStopOutputStream(Context);
  RestoreStatus = EFI_SUCCESS;
  if (!EFI_ERROR(StopStatus)) {
    if (Context->PciDmaContainment) {
      RestoreStatus = HdaRestorePciAttributes(Context);
    } else {
      RestoreStatus = HdaRestoreCodecStateRetry(Context, "async-playback-complete");
      if (!EFI_ERROR(RestoreStatus)) RestoreStatus = HdaRestorePciAttributes(Context);
    }
  }
  if (Context->PciDmaContainment) {
    mHdaAsyncFault = TRUE;
    HdaSetLastStatus("PlaybackFastBootDmaContained");
  } else if (EFI_ERROR(StopStatus)) {
    mHdaAsyncFault = TRUE;
    HdaSetLastStatus("PlaybackFastBootStopFailed");
  } else if (EFI_ERROR(RestoreStatus)) {
    if (Context->PostPlaybackCleanupStatus == EFI_SUCCESS) {
      Context->PostPlaybackCleanupStatus = RestoreStatus;
    }
    HdaSetLastStatus("PlaybackFastBootPciRestoreFailed");
  } else if (EFI_ERROR(Context->PostPlaybackCleanupStatus)) {
    HdaLog("[HDAU] FastBoot playback completed with cleanup degradation: %r",
           Context->PostPlaybackCleanupStatus);
    HdaSetLastStatus("PlaybackDoneFastBootCleanupDegraded");
  } else if (mHdaAsyncFault) {
    HdaSetLastStatus("PlaybackFastBootStreamFault");
  } else if (ReachedStop) {
    HdaSetLastStatus("PlaybackDoneFastBoot");
  } else if (NoProgressTimeout) {
    HdaSetLastStatus("PlaybackFastBootNoDmaProgress");
  } else {
    HdaSetLastStatus("PlaybackFastBootIncompleteTimeout");
  }
  EventCloseStatus = EFI_SUCCESS;
  if (Event != NULL && gBS != NULL) {
    EFI_EVENT EventToClose;

    (VOID)gBS->SetTimer(Event, TimerCancel, 0);
    EventToClose = Event;
    EventCloseStatus = HdaCloseEventTerminal(&EventToClose);
    mHdaAsyncEvent = NULL;
    if (EventCloseStatus != EFI_SUCCESS) {
      HdaLog("[HDAU] async event terminal close returned undocumented status: %r", EventCloseStatus);
    }
  } else {
    mHdaAsyncEvent = NULL;
  }
  mHdaAsyncHaveLpibBaseline = FALSE;
  if (!EFI_ERROR(StopStatus)) {
    BuffersReleased = HdaFreePlaybackBuffers(Context);
    if (BuffersReleased && !EFI_ERROR(RestoreStatus)) {
      FreePool(Context);
      mHdaAsyncContext = NULL;
      mHdaAsyncElapsedUsec = 0;
      mHdaAsyncLastProgressUsec = 0;
      mHdaAsyncLastLpib = 0;
      mHdaAsyncHaveLpibBaseline = FALSE;
      mHdaAsyncMoved = FALSE;
      mHdaAsyncFault = FALSE;
    } else {
      mHdaAsyncContext = Context;
      if (!BuffersReleased) {
        HdaSetLastStatus("PlaybackFastBootCleanupPending");
      }
      HdaLog("[HDAU] async context retained for resource retry: restore=%r buffersReleased=%u event=%r",
             RestoreStatus, BuffersReleased ? 1 : 0, EventCloseStatus);
    }
  } else {
    /* Preserve ownership through the terminal handoff while DMA stop is unconfirmed. */
    mHdaAsyncContext = Context;
    HdaLog("[HDAU] async buffers retained; DMA stop was not confirmed and cleanup remains pending: %r", StopStatus);
  }
  HdaCloseAppendLog(OpenedLog, SavedFileLog);
}

STATIC EFI_STATUS HdaStartFastBootPlayback(HDAU_CONTEXT *Context) {
  EFI_STATUS Status;
  EFI_EVENT AsyncEvent;
  HDAU_CONTEXT *Async;

  if (!mHdaCfg.FastBoot) return EFI_UNSUPPORTED;
  if (Context == NULL || gBS == NULL) return EFI_INVALID_PARAMETER;
  if (mHdaAsyncContext != NULL) return EFI_ALREADY_STARTED;

  /* FastBoot transfers DMA ownership to an asynchronous context. Do not make
     that transfer unless a terminal boot-handoff cleanup hook is known
     to exist; otherwise a bootloader handoff can strand a running stream. */
  Status = HdaInstallExitBootServicesCleanup();
  if (EFI_ERROR(Status)) {
    HdaLog("[HDAU] FastBoot disabled for this playback because boot-handoff cleanup is unavailable: %r", Status);
    return Status;
  }

  Async = AllocateCopyPool(sizeof(HDAU_CONTEXT), Context);
  if (Async == NULL) return EFI_OUT_OF_RESOURCES;
  Async->HeapAllocated = TRUE;

  mHdaAsyncElapsedUsec = 0;
  mHdaAsyncLastProgressUsec = 0;
  mHdaAsyncLastLpib = 0;
  mHdaAsyncHaveLpibBaseline = FALSE;
  mHdaAsyncMoved = FALSE;
  mHdaAsyncFault = FALSE;
  AsyncEvent = NULL;
  Status = gBS->CreateEvent(EVT_TIMER | EVT_NOTIFY_SIGNAL, TPL_CALLBACK, HdaAsyncPlaybackTick, Async, &AsyncEvent);
  if (Status != EFI_SUCCESS || AsyncEvent == NULL) {
    if (Status == EFI_SUCCESS || !EFI_ERROR(Status)) Status = EFI_DEVICE_ERROR;
    FreePool(Async);
    return Status;
  }
  mHdaAsyncEvent = AsyncEvent;
  mHdaAsyncContext = Async;
  Status = gBS->SetTimer(mHdaAsyncEvent, TimerPeriodic, HDAU_ASYNC_PLAYBACK_TICK_US * 10ULL);
  if (Status != EFI_SUCCESS) {
    EFI_STATUS CleanupStatus;

    if (!EFI_ERROR(Status)) Status = EFI_DEVICE_ERROR;
    HdaLog("[HDAU] FastBoot timer arm failed; tearing down transferred playback ownership: %r", Status);
    CleanupStatus = HdaCleanupAsyncPlayback(FALSE);
    if (EFI_ERROR(CleanupStatus)) {
      HdaLog("[HDAU] FastBoot timer failure cleanup remains pending: %r", CleanupStatus);
      return CleanupStatus;
    }

    /* Cleanup released the copied context and its DMA buffers. The caller's
       stack copy now contains stale buffer addresses and must not fall back to
       blocking playback or probe another codec/controller. */
    return EFI_ABORTED;
  }

  HdaSetLastStatus("PlaybackStartedFastBoot");
  return EFI_SUCCESS;
}

STATIC EFI_STATUS HdaPlayChime(HDAU_CONTEXT *Context) {
  EFI_STATUS Status;
  EFI_STATUS StopStatus;
  EFI_STATUS RestoreStatus;
  BOOLEAN Completed;
  BOOLEAN BuffersSafeToFree;
  BOOLEAN PathProgrammed;
  UINTN CandidateIndex;

  if (Context == NULL || Context->PciIo == NULL) return EFI_INVALID_PARAMETER;
  BuffersSafeToFree = TRUE;
  RestoreStatus = EFI_SUCCESS;
  if (HdaTraceStage(5, "find playable path")) return EFI_ABORTED;
  if (!HdaTryQuickPath(Context) && !HdaFindPlayablePath(Context)) {
    HdaSetLastStatus("NoPlayablePath");
    return EFI_NOT_FOUND;
  }

  if (mHdaCfg.ProbeOnly) {
    HdaSetLastStatus("ProbeOnlyOK");
    return EFI_SUCCESS;
  }
  if (mHdaCfg.NoPlayback) {
    HdaSetLastStatus("NoPlaybackOK");
    return EFI_SUCCESS;
  }

  HdaCollectRouteCandidates(Context);
  PathProgrammed = FALSE;
  Status = EFI_NOT_FOUND;
  for (CandidateIndex = 0; CandidateIndex < Context->RouteCandidateCount; CandidateIndex++) {
    if (!HdaLoadRouteCandidate(Context, CandidateIndex)) break;
    Context->SubsystemId = 0;
    Context->SubsystemIdValid = FALSE;
    Status = HdaImmediateCommand(Context->PciIo, Context->Codec, Context->Afg,
                                 HDA_VERB12(HDA_VERB_GET_SUBSYSTEM_ID, 0), &Context->SubsystemId);
    if (!EFI_ERROR(Status)) Context->SubsystemIdValid = TRUE;
    Status = HdaSelectPcmFormat(Context);
    if (EFI_ERROR(Status)) {
      HdaLog("[HDAU] route candidate rejected by PCM negotiation: candidate=%u status=%r",
             (UINT32)CandidateIndex, Status);
      continue;
    }
    Status = HdaSnapshotCodecState(Context);
    if (EFI_ERROR(Status)) {
      HdaLog("[HDAU] route candidate snapshot failed: candidate=%u status=%r",
             (UINT32)CandidateIndex, Status);
      if (Context->SnapshotNodeCount > 0) {
        Context->SnapshotValid = TRUE;
        RestoreStatus = HdaRestoreCodecStateRetry(Context, "partial-snapshot-failure");
        if (EFI_ERROR(RestoreStatus)) {
          HdaSetLastStatus("CodecStateRestoreFailed");
          return HdaFinalizePlaybackContext(Context, "partial-snapshot-failure",
                                            RestoreStatus, RestoreStatus);
        }
      }
      continue;
    }

    if (HdaTraceStage(6, "program path verbs")) {
      RestoreStatus = HdaRestoreCodecStateRetry(Context, "trace-abort-before-stream");
      return HdaFinalizePlaybackContext(Context, "trace-abort-before-stream",
                                        EFI_ABORTED, RestoreStatus);
    }
    Status = HdaUnmutePath(Context);
    if (!EFI_ERROR(Status)) {
      PathProgrammed = TRUE;
      break;
    }
    HdaLog("[HDAU] route candidate programming failed: candidate=%u pin=0x%x dac=0x%x status=%r",
           (UINT32)CandidateIndex, Context->Pin, Context->Dac, Status);
    RestoreStatus = HdaRestoreCodecStateRetry(Context, "route-candidate-retry");
    if (EFI_ERROR(RestoreStatus)) {
      HdaSetLastStatus("CodecStateRestoreFailed");
      return HdaFinalizePlaybackContext(Context, "route-candidate-restore",
                                        RestoreStatus, RestoreStatus);
    }
  }
  if (!PathProgrammed) {
    HdaSetLastStatus("CodecPathProgrammingFailed");
    return Status;
  }

  if (HdaTraceStage(7, "allocate playback buffers")) {
    RestoreStatus = HdaRestoreCodecStateRetry(Context, "trace-abort-buffer-allocation");
    return HdaFinalizePlaybackContext(Context, "trace-abort-buffer-allocation",
                                      EFI_ABORTED, RestoreStatus);
  }
  Status = HdaAllocatePlaybackBuffers(Context);
  if (EFI_ERROR(Status)) {
    HdaLog("[HDAU] allocate playback buffers failed: %r", Status);
    HdaSetLastStatus("BufferAllocFailed");
    RestoreStatus = HdaRestoreCodecStateRetry(Context, "buffer-allocation-failed");
    return HdaFinalizePlaybackContext(Context, "buffer-allocation-failed",
                                      Status, RestoreStatus);
  }

  if (HdaTraceStage(8, "prepare output stream")) {
    return HdaReleasePreparedStreamAndBuffers(Context, EFI_ABORTED);
  }
  Status = HdaPrepareOutputStream(Context);
  if (EFI_ERROR(Status)) {
    EFI_STATUS CleanupStatus;
    HdaLog("[HDAU] prepare output stream failed: %r", Status);
    HdaSetLastStatus("StreamPrepareFailed");
    CleanupStatus = HdaReleasePreparedStreamAndBuffers(Context, Status);
    if (EFI_ERROR(CleanupStatus) && CleanupStatus != Status) return CleanupStatus;
    return Status;
  }

  if (HdaTraceStage(9, "start playback")) {
    return HdaReleasePreparedStreamAndBuffers(Context, EFI_ABORTED);
  }
  Status = HdaSetVerb(Context->PciIo, Context->Codec, Context->Dac,
                      HDA_VERB4(HDA_VERB_SET_CONVERTER_FORMAT, Context->StreamFormat));
  if (EFI_ERROR(Status)) {
    HdaSetLastStatus("ConverterFormatFailed");
    return HdaReleasePreparedStreamAndBuffers(Context, Status);
  }
  Status = HdaSetVerb(Context->PciIo, Context->Codec, Context->Dac,
                      HDA_VERB12(HDA_VERB_SET_CONVERTER_STREAM_CHANNEL, (mHdaActiveStreamTag << 4)));
  if (EFI_ERROR(Status)) {
    HdaSetLastStatus("ConverterStreamTagFailed");
    return HdaReleasePreparedStreamAndBuffers(Context, Status);
  }
  Status = HdaPciFlushExact(Context->PciIo);
  if (EFI_ERROR(Status)) {
    HdaSetLastStatus("ConverterFlushFailed");
    return HdaReleasePreparedStreamAndBuffers(Context, Status);
  }

  /* BUS_MASTER is needed only when the stream is about to run. Keep it off
     throughout discovery, codec programming, buffer mapping and descriptor
     preparation when firmware supplied it clear. This prevents an unrelated
     residual HDA DMA engine from being re-enabled merely by entering the
     driver. Exact read-back is required before RUN is asserted. */
  {
    UINT64 CurrentAttributes;

    CurrentAttributes = 0;
    Status = HdaGetPciAttributesExact(Context->PciIo, &CurrentAttributes);
    if (EFI_ERROR(Status)) {
      HdaSetLastStatus("PciBusMasterSnapshotFailed");
      return HdaReleasePreparedStreamAndBuffers(Context, Status);
    }
    if ((CurrentAttributes & EFI_PCI_IO_ATTRIBUTE_BUS_MASTER) == 0) {
      Status = HdaRequireNoRunningStreamsBeforeBusMasterEnable(Context->PciIo);
      if (EFI_ERROR(Status)) {
        HdaSetLastStatus("PciBusMasterForeignDmaActive");
        return HdaReleasePreparedStreamAndBuffers(Context, Status);
      }
      Status = Context->PciIo->Attributes(
                                  Context->PciIo,
                                  EfiPciIoAttributeOperationEnable,
                                  EFI_PCI_IO_ATTRIBUTE_BUS_MASTER,
                                  NULL
                                  );
      if (Status != EFI_SUCCESS) {
        Status = EFI_ERROR(Status) ? Status : EFI_DEVICE_ERROR;
        HdaSetLastStatus("PciBusMasterEnableFailed");
        return HdaReleasePreparedStreamAndBuffers(Context, Status);
      }
      Status = HdaVerifyPciAttributeBits(
                 Context->PciIo,
                 EFI_PCI_IO_ATTRIBUTE_BUS_MASTER,
                 EFI_PCI_IO_ATTRIBUTE_BUS_MASTER
                 );
      if (EFI_ERROR(Status)) {
        HdaSetLastStatus("PciBusMasterVerifyFailed");
        return HdaReleasePreparedStreamAndBuffers(Context, Status);
      }
    }
  }

  Status = HdaStartOutputStream(Context);
  if (EFI_ERROR(Status)) {
    HdaLog("[HDAU] start output stream failed: %r", Status);
    HdaSetLastStatus("StreamStartFailed");
    return HdaReleasePreparedStreamAndBuffers(Context, Status);
  }
  Status = HdaPostStartCodecReapply(Context);
  if (EFI_ERROR(Status)) {
    HdaLog("[HDAU] post-start codec path reapply failed: %r", Status);
    HdaSetLastStatus("PostStartCodecProgrammingFailed");
    StopStatus = HdaStopOutputStream(Context);
    if (!EFI_ERROR(StopStatus)) {
      RestoreStatus = Context->PciDmaContainment
                        ? EFI_SUCCESS
                        : HdaRestoreCodecStateRetry(Context, "post-start-programming-failed");
    } else {
      EFI_STATUS RetainStatus = HdaRetainContextForCleanup(Context, "post-start-codec-failure");
      HdaLog("[HDAU] playback buffers retained because stream stop after codec programming failure was not confirmed: %r retain=%r", StopStatus, RetainStatus);
      if (EFI_ERROR(RetainStatus)) return RetainStatus;
    }
    if (EFI_ERROR(StopStatus)) return StopStatus;
    return HdaFinalizePlaybackContext(Context, "post-start-programming-failed",
                                      Status, RestoreStatus);
  }

  if (mHdaCfg.FastBoot) {
    Status = HdaStartFastBootPlayback(Context);
    if (!EFI_ERROR(Status)) return EFI_SUCCESS;
    if (Status == EFI_ABORTED ||
        (mHdaAsyncContext != NULL && mHdaAsyncContext->PciIo == Context->PciIo)) {
      HdaLog("[HDAU] FastBoot setup failed after ownership transfer; blocking fallback suppressed: %r", Status);
      return Status;
    }
    HdaLog("[HDAU] FastBoot playback timer unavailable, falling back to blocking playback: %r", Status);
  }

  Completed = HdaMonitorPlaybackProgress(
                Context,
                (UINTN)(((UINT64)Context->PcmFrames * 1000000ULL) / Context->SampleRate) +
                ((UINTN)HDAU_WAIT_CHIME_DRAIN_MS + 250U) * 1000U);
  StopStatus = HdaStopOutputStream(Context);
  RestoreStatus = EFI_SUCCESS;
  if (!EFI_ERROR(StopStatus) && !Context->PciDmaContainment) {
    RestoreStatus = HdaRestoreCodecStateRetry(Context, "blocking-playback-complete");
  }
  if (Context->PciDmaContainment) {
    HdaSetLastStatus("PlaybackDmaContained");
    Status = EFI_DEVICE_ERROR;
    BuffersSafeToFree = TRUE;
  } else if (EFI_ERROR(StopStatus)) {
    EFI_STATUS RetainStatus;
    HdaSetLastStatus("PlaybackStreamStopFailed");
    Status = StopStatus;
    BuffersSafeToFree = FALSE;
    RetainStatus = HdaRetainContextForCleanup(Context, "blocking-playback-stop");
    if (EFI_ERROR(RetainStatus)) Status = RetainStatus;
  } else if (Completed) {
    if (EFI_ERROR(Context->PostPlaybackCleanupStatus)) {
      HdaLog("[HDAU] playback completed with stream descriptor cleanup degradation: %r", Context->PostPlaybackCleanupStatus);
      HdaSetLastStatus("PlaybackDoneStreamRestoreDegraded");
      Status = EFI_SUCCESS;
    } else if (EFI_ERROR(RestoreStatus)) {
      HdaLog("[HDAU] playback completed but codec state restore reported: %r", RestoreStatus);
      Context->PostPlaybackCleanupStatus = RestoreStatus;
      HdaSetLastStatus("PlaybackDoneRestoreDegraded");
      Status = EFI_SUCCESS;
    } else {
      HdaSetLastStatus("PlaybackDoneDmaMoved");
      Status = EFI_SUCCESS;
    }
  } else {
    HdaSetLastStatus("PlaybackIncomplete");
    Status = EFI_DEVICE_ERROR;
  }

  if (BuffersSafeToFree) {
    EFI_STATUS FinalizeStatus;
    FinalizeStatus = HdaFinalizePlaybackContext(Context, "blocking-playback-complete",
                                                Status, RestoreStatus);
    if (EFI_ERROR(RestoreStatus) && Context->PostPlaybackCleanupStatus == EFI_SUCCESS) {
      Context->PostPlaybackCleanupStatus = RestoreStatus;
    }
    if (mHdaAsyncContext != NULL && mHdaAsyncContext->PciIo == Context->PciIo) {
      HdaSetLastStatus("PlaybackCleanupPending");
    }
    Status = FinalizeStatus;
  } else {
    HdaLog("[HDAU] playback buffers intentionally retained because DMA stop was not confirmed");
  }
  return Status;
}

STATIC BOOLEAN PciIsHdaController(EFI_PCI_IO_PROTOCOL *PciIo, HDAU_PCI_INFO *Info OPTIONAL) {
  HDAU_PCI_INFO LocalInfo;
  HDAU_PCI_INFO *UseInfo;

  UseInfo = (Info == NULL) ? &LocalInfo : Info;
  if (!HdaReadPciInfo(PciIo, UseInfo)) return FALSE;

  return (UseInfo->ClassCode[2] == HDA_PCI_CLASS_MULTIMEDIA &&
          UseInfo->ClassCode[1] == HDA_PCI_SUBCLASS_HD_AUDIO);
}


STATIC EFI_STATUS HdaDumpAllControllers(EFI_HANDLE *Handles, UINTN HandleCount) {
  EFI_STATUS Status;
  EFI_FILE_HANDLE Root;
  EFI_FILE_HANDLE DumpFile;
  UINTN Index;
  UINTN Dumped;
  EFI_STATUS FirstControllerError;
  EFI_TIME Now;
  BOOLEAN HaveTime;
  BOOLEAN OpenedLog;
  BOOLEAN SavedFileLog;
  BOOLEAN LogWriteFailed;

  if (Handles == NULL && HandleCount != 0) return EFI_INVALID_PARAMETER;
  Root = NULL;
  DumpFile = mHdaLogFile;
  Dumped = 0;
  FirstControllerError = EFI_SUCCESS;
  HaveTime = FALSE;
  OpenedLog = FALSE;
  SavedFileLog = mHdaCfg.FileLog;
  ZeroMem(&Now, sizeof(Now));

  if (DumpFile == NULL) {
    Status = HdaOpenLoadedImageRoot(&Root);
    if (Status == EFI_SUCCESS && Root != NULL) {
      Status = HdaOpenDebugLog(Root, TRUE);
      (VOID)HdaCloseFileHandleTerminal(&Root);
      if (Status == EFI_SUCCESS) {
        DumpFile = mHdaLogFile;
        OpenedLog = TRUE;
        mHdaCfg.FileLog = TRUE;
      }
    }
  }
  if (DumpFile == NULL) return EFI_DEVICE_ERROR;

  HdaDumpWrite(DumpFile, "");
  HdaDumpWrite(DumpFile, "HDAUniversalBootChimeDxe codec dump");
  HdaDumpWrite(DumpFile, "Copyright © 2026 DNMTechLabs <dnmtechlabs@gmail.com>");
  HdaDumpWrite(DumpFile, "Version: %a", HDAU_CHIME_VERSION_ASCII);
  HdaDumpWrite(DumpFile, "Trigger: %a", mHdaCodecDumpByF6 ? "D" : "config");
  HdaDumpWrite(DumpFile, "Output: EFI/HDAUniversalBootChimeDxe/Logs/HDAUChime.log (single consolidated log)");
  HdaDumpWrite(DumpFile, "Format: Linux/AppleALC-like UEFI HDA codec dump");
  if (gRT != NULL) HaveTime = (gRT->GetTime(&Now, NULL) == EFI_SUCCESS);
  if (HaveTime) {
    HdaDumpWrite(DumpFile, "UEFI Time: %04u-%02u-%02u %02u:%02u:%02u",
                 Now.Year, Now.Month, Now.Day, Now.Hour, Now.Minute, Now.Second);
  }

  for (Index = 0; Index < HandleCount; Index++) {
    EFI_PCI_IO_PROTOCOL *PciIo = NULL;
    HDAU_PCI_INFO Info;

    Status = gBS->HandleProtocol(Handles[Index], &gEfiPciIoProtocolGuid, (VOID **)&PciIo);
    if (Status != EFI_SUCCESS || PciIo == NULL) continue;
    if (!PciIsHdaController(PciIo, &Info)) continue;

    Status = HdaDumpControllerFull(DumpFile, PciIo, &Info);
    if (Status == EFI_SUCCESS || Status == EFI_NOT_FOUND) {
      Dumped++;
    } else {
      Status = HdaNormalizeExactSuccess(Status);
      if (FirstControllerError == EFI_SUCCESS) FirstControllerError = Status;
    }

    /* A retained CORB/RIRB state is global hardware ownership.  Do not walk
       another controller while the previous diagnostic still owns mappings,
       buffers or ring registers awaiting terminal cleanup. */
    if (mHdaRetainedCorbRirbValid) {
      if (FirstControllerError == EFI_SUCCESS) FirstControllerError = EFI_ABORTED;
      HdaDumpWrite(DumpFile, "Codec dump stopped: CORB/RIRB cleanup ownership remains pending");
      break;
    }
  }

  HdaDumpWrite(DumpFile, "");
  HdaDumpWrite(DumpFile, "Dumped controllers: %u", (UINT32)Dumped);
  LogWriteFailed = (mHdaLogFile == NULL);
  if (!LogWriteFailed) {
    Status = mHdaLogFile->Flush(mHdaLogFile);
    if (Status != EFI_SUCCESS) {
      LogWriteFailed = TRUE;
      (VOID)HdaCloseTrackedLogFile(FALSE);
      mHdaCfg.FileLog = FALSE;
    }
  }
  if (!LogWriteFailed) {
    HdaSetNvramAscii(L"HDAUChimeCodecDumpPath", "EFI/HDAUniversalBootChimeDxe/Logs/HDAUChime.log");
    if (FirstControllerError != EFI_SUCCESS) {
      HdaSetLastStatus("CodecDumpControllerError");
      HdaLog("[HDAU] codec dump: controller failure propagated after %u completed controller(s): %r",
             (UINT32)Dumped, FirstControllerError);
    } else {
      HdaSetLastStatus(Dumped != 0 ? "CodecDumpDone" : "CodecDumpNoController");
      HdaLog("[HDAU] codec dump: done controllers=%u mode=single-log", (UINT32)Dumped);
    }
  } else {
    HdaSetLastStatus("CodecDumpWriteFailed");
  }

  if (OpenedLog) {
    if (mHdaLogFile != NULL) {
      (VOID)HdaCloseTrackedLogFile(TRUE);
    }
    mHdaLogLineCount = 0;
    mHdaCfg.FileLog = SavedFileLog;
  }
  if (LogWriteFailed) return EFI_DEVICE_ERROR;
  if (FirstControllerError != EFI_SUCCESS) return FirstControllerError;
  return (Dumped != 0) ? EFI_SUCCESS : EFI_NOT_FOUND;
}


STATIC EFI_STATUS HdaDumpAllControllersDynamic(BOOLEAN ByF6) {
  EFI_STATUS Status;
  EFI_HANDLE *Handles;
  UINTN HandleCount;

  Handles = NULL;
  HandleCount = 0;
  Status = gBS->LocateHandleBuffer(ByProtocol, &gEfiPciIoProtocolGuid, NULL, &HandleCount, &Handles);
  if (Status != EFI_SUCCESS) {
    /* Pool ownership is transferred only by a clean success. Never inspect or
       free ambiguous outputs published alongside a warning/error status. */
    Handles = NULL;
    HandleCount = 0;
    HdaSetLastStatus("CodecDumpNoPciIoHandles");
    return EFI_ERROR(Status) ? Status : EFI_DEVICE_ERROR;
  }
  if (Handles == NULL || HandleCount == 0) {
    if (Handles != NULL) FreePool(Handles);
    HdaSetLastStatus("CodecDumpNoPciIoHandles");
    return EFI_NOT_FOUND;
  }

  mHdaCodecDumpByF6 = ByF6;
  Status = HdaDumpAllControllers(Handles, HandleCount);
  if (Handles != NULL) FreePool(Handles);
  return Status;
}

STATIC VOID HdaDumpCodecBasic(EFI_PCI_IO_PROTOCOL *PciIo, UINT8 Codec) {
  UINT8 FgStart;
  UINT8 FgCount;
  UINT8 WStart;
  UINT8 WCount;
  UINTN FgIndex;
  UINTN WIndex;

  if (!mHdaCfg.DumpCodec) return;

  if (!HdaGetSubnodes(PciIo, Codec, 0, &FgStart, &FgCount)) {
    HdaLog("[HDAU] codec %u dump: no root subnodes", Codec);
    return;
  }

  HdaLog("[HDAU] codec %u root nodes: start=0x%x count=%u", Codec, FgStart, (UINT32)FgCount);
  for (FgIndex = 0; FgIndex < FgCount; FgIndex++) {
    UINT8 Fg = (UINT8)(FgStart + FgIndex);
    UINT32 FunctionType = HdaGetParam(PciIo, Codec, Fg, HDA_PARAM_FUNCTION_TYPE);
    HdaLog("[HDAU] codec %u fg=0x%x function=0x%x", Codec, Fg, FunctionType);
    if ((FunctionType & 0xFF) != 0x01) continue;
    if (!HdaGetSubnodes(PciIo, Codec, Fg, &WStart, &WCount)) continue;
    HdaLog("[HDAU] codec %u widgets: start=0x%x count=%u", Codec, WStart, (UINT32)WCount);
    for (WIndex = 0; WIndex < WCount && WIndex < HDAU_MAX_WIDGETS; WIndex++) {
      UINT8 Nid = (UINT8)(WStart + WIndex);
      UINT32 Caps = HdaGetParam(PciIo, Codec, Nid, HDA_PARAM_AUDIO_WIDGET_CAP);
      UINT8 Type = HdaWidgetType(Caps);
      if (Type == HDA_WIDGET_TYPE_AUDIO_OUT || Type == HDA_WIDGET_TYPE_PIN_COMPLEX || Type == HDA_WIDGET_TYPE_AUDIO_MIXER || Type == HDA_WIDGET_TYPE_AUDIO_SELECTOR) {
        UINT32 PinCap = 0;
        UINT32 Config = 0;
        if (Type == HDA_WIDGET_TYPE_PIN_COMPLEX) {
          PinCap = HdaGetParam(PciIo, Codec, Nid, HDA_PARAM_PIN_CAP);
          (VOID)HdaImmediateCommand(PciIo, Codec, Nid, HDA_VERB12(HDA_VERB_GET_CONFIG_DEFAULT, 0), &Config);
        }
        HdaLog("[HDAU] widget nid=0x%x type=0x%x caps=0x%x pincap=0x%x config=0x%x", Nid, Type, Caps, PinCap, Config);
      }
    }
  }
}

STATIC EFI_STATUS HdaTryController(EFI_PCI_IO_PROTOCOL *PciIo) {
  EFI_STATUS Status;
  EFI_STATUS Result;
  EFI_STATUS RestoreStatus;
  UINT16 StateSts;
  UINT8 Codec;
  UINT64 OriginalAttributes;
  HDAU_PCI_INFO Info;
  HDAU_CONTEXT ControllerContext;

  ZeroMem(&Info, sizeof(Info));
  ZeroMem(&ControllerContext, sizeof(ControllerContext));
  ControllerContext.PciIo = PciIo;
  OriginalAttributes = 0;

  if (!HdaReadPciInfo(PciIo, &Info)) {
    HdaLog("[HDAU] controller PCI configuration read failed");
    HdaSetLastStatus("PciConfigReadFailed");
    return EFI_DEVICE_ERROR;
  }
  mHdaActiveControllerVendorId = Info.VendorId;
  mHdaActiveControllerDeviceId = Info.DeviceId;
  HdaLog("[HDAU] trying HDA controller seg=%u bus=%02x dev=%02x func=%02x vendor=%04x device=%04x class=%02x%02x%02x",
         (UINT32)Info.Segment, (UINT32)Info.Bus, (UINT32)Info.Device, (UINT32)Info.Function,
         Info.VendorId, Info.DeviceId, Info.ClassCode[2], Info.ClassCode[1], Info.ClassCode[0]);

  if (HdaTraceStage(2, "enable pci attributes")) return EFI_ABORTED;
  Status = HdaGetPciAttributesExact(PciIo, &OriginalAttributes);
  if (EFI_ERROR(Status)) {
    HdaLog("[HDAU] PCI attributes snapshot failed: %r", Status);
    HdaSetLastStatus("PciAttributesSnapshotFailed");
    return Status;
  }
  ControllerContext.OriginalPciAttributes = OriginalAttributes;
  ControllerContext.HaveOriginalPciAttributes = TRUE;

  /* Enable only MMIO decode while the controller is inspected and codec/path
     state is prepared. If BUS_MASTER was originally clear, turning it on here
     could resume stale HDA DMA before this driver has validated/owned the
     stream engines. HdaPlayChime enables BUS_MASTER transactionally only at
     the final pre-RUN boundary. CORB/RIRB fallback owns its own temporary BME. */
  Status = PciIo->Attributes(
                    PciIo,
                    EfiPciIoAttributeOperationEnable,
                    EFI_PCI_IO_ATTRIBUTE_MEMORY,
                    NULL
                    );
  if (Status != EFI_SUCCESS) {
    if (!EFI_ERROR(Status)) Status = EFI_DEVICE_ERROR;
    HdaLog("[HDAU] PCI memory attribute enable failed: %r", Status);
    HdaSetLastStatus("PciAttributesFailed");
    Result = Status;
    goto Exit;
  }
  Status = HdaVerifyPciAttributeBits(
             PciIo,
             OriginalAttributes | EFI_PCI_IO_ATTRIBUTE_MEMORY,
             EFI_PCI_IO_ATTRIBUTE_MEMORY
             );
  if (EFI_ERROR(Status)) {
    HdaLog("[HDAU] PCI memory attribute enable read-back mismatch: %r", Status);
    HdaSetLastStatus("PciAttributesVerifyFailed");
    Result = Status;
    goto Exit;
  }

  if (HdaTraceStage(3, "prepare hda controller")) {
    Result = EFI_ABORTED;
    goto Exit;
  }
  Status = HdaControllerEnsureReady(PciIo);
  if (EFI_ERROR(Status)) {
    HdaLog("[HDAU] controller prepare failed: %r", Status);
    HdaSetLastStatus("ControllerPrepareFailed");
    Result = Status;
    goto Exit;
  }

  if (HdaTraceStage(4, "detect codecs")) {
    Result = EFI_ABORTED;
    goto Exit;
  }
  Status = HdaMemRead16(PciIo, HDA_REG_STATESTS, &StateSts);
  if (EFI_ERROR(Status)) {
    HdaSetLastStatus("CodecStateReadFailed");
    Result = Status;
    goto Exit;
  }
  if (StateSts == 0) {
    HdaStall(HDAU_CODEC_WAKE_TIMEOUT_US);
    Status = HdaMemRead16(PciIo, HDA_REG_STATESTS, &StateSts);
    if (EFI_ERROR(Status)) {
      HdaSetLastStatus("CodecStateReadFailed");
      Result = Status;
      goto Exit;
    }
  }
  HdaLog("[HDAU] STATESTS=0x%04x", StateSts);
  if (StateSts == 0) {
    HdaSetLastStatus("NoCodecStateSts");
    Result = EFI_NOT_FOUND;
    goto Exit;
  }

  for (Codec = 0; Codec < HDAU_MAX_CODECS; Codec++) {
    HDAU_CONTEXT Context;
    UINT32 VendorId = 0;
    UINT32 RevisionId = 0;
    BOOLEAN RevisionIdValid = FALSE;

    if (mHdaCfg.ForceCodec != HDAU_FORCE_CODEC_NONE && Codec != mHdaCfg.ForceCodec) continue;
    if ((StateSts & (1U << Codec)) == 0) {
      if (mHdaCfg.ForceCodec == Codec) HdaLog("[HDAU] forced codec %u is not present in STATESTS", Codec);
      continue;
    }

    Status = HdaImmediateCommand(PciIo, Codec, 0,
                                 HDA_VERB12(HDA_VERB_GET_PARAMETER, HDA_PARAM_VENDOR_ID),
                                 &VendorId);
    if (EFI_ERROR(Status) || VendorId == 0 || VendorId == MAX_UINT32) {
      HdaLog("[HDAU] codec %u vendor-id query failed or returned an invalid response: status=%r value=0x%08x",
             Codec, Status, VendorId);
      continue;
    }
    Status = HdaImmediateCommand(PciIo, Codec, 0,
                                 HDA_VERB12(HDA_VERB_GET_PARAMETER, HDA_PARAM_REV_ID),
                                 &RevisionId);
    if (EFI_ERROR(Status)) {
      HdaLog("[HDAU] codec %u revision-id query failed: %r; continuing with vendor-id only", Codec, Status);
      RevisionId = 0;
    } else {
      RevisionIdValid = TRUE;
    }
    HdaLog("[HDAU] codec %u vendor=0x%08x revision=0x%08x", Codec, VendorId, RevisionId);
    if (mHdaCfg.Debug || mHdaCfg.DumpCodec || mHdaCfg.ProbeOnly) {
      HdaDumpCodecBasic(PciIo, Codec);
    }

    ZeroMem(mHdaAlcIdFanoutPins, sizeof(mHdaAlcIdFanoutPins));
    ZeroMem(mHdaAlcIdFanoutDacs, sizeof(mHdaAlcIdFanoutDacs));
    mHdaAlcIdFanoutCount = 0;
    mHdaActiveStreamTag = HDA_STREAM_TAG;
    mHdaActiveStreamFormat = HDA_FMT_48K_16B_STEREO;
    mHdaActivePolicy = NULL;
    mHdaActiveCodecId = VendorId;
    mHdaActiveAlcIdProfile = NULL;
    ZeroMem(&Context, sizeof(Context));
    Context.PciIo = PciIo;
    Context.Codec = Codec;
    Context.CodecId = VendorId;
    Context.RevisionId = RevisionId;
    Context.RevisionIdValid = RevisionIdValid;
    Context.OriginalPciAttributes = OriginalAttributes;
    Context.HaveOriginalPciAttributes = TRUE;

    Status = HdaPlayChime(&Context);
    if (!EFI_ERROR(Status)) {
      HdaLog("[HDAU] success codec=%u dac=0x%x pin=0x%x status=%r cleanup=%r",
             Codec, Context.Dac, Context.Pin, Status, Context.PostPlaybackCleanupStatus);
      HdaSetLastStatus(EFI_ERROR(Context.PostPlaybackCleanupStatus)
                         ? "SuccessWithCleanupDegradation"
                         : "Success");
      if (mHdaAsyncContext != NULL && mHdaAsyncContext->PciIo == PciIo) {
        /* The copied asynchronous context owns restoration after DMA stops.
           Preserve cleanup degradation diagnostics while still treating it
           as successful playback, so higher layers never replay the chime. */
        return Status;
      }
      ControllerContext.PciDmaContainment = Context.PciDmaContainment;
      Result = Status;
      goto Exit;
    }

    HdaLog("[HDAU] codec %u failed: %r", Codec, Status);
    if (mHdaAsyncContext != NULL && mHdaAsyncContext->PciIo == PciIo) {
      HdaLog("[HDAU] controller probing stopped because a retained DMA context owns this controller");
      return Status;
    }
    if (Context.PciDmaContainment) {
      ControllerContext.PciDmaContainment = TRUE;
      HdaLog("[HDAU] controller probing stopped after DMA containment; no further codec may safely reuse this controller in this boot");
      Result = Status;
      goto Exit;
    }
    if (Status == EFI_ABORTED) {
      Result = Status;
      goto Exit;
    }
  }

  HdaSetLastStatus("NoWorkingCodec");
  Result = EFI_NOT_FOUND;

Exit:
  if (mHdaRetainedCorbRirbValid && mHdaRetainedCorbRirbState.PciIo == PciIo) {
    EFI_STATUS RingCleanupStatus;

    RingCleanupStatus = HdaCleanupRetainedCorbRirb(FALSE);
    if (EFI_ERROR(RingCleanupStatus)) {
      HdaLog("[HDAU] retained CORB/RIRB cleanup retry failed before PCI restore: %r", RingCleanupStatus);
      if (EFI_ERROR(Result)) Result = RingCleanupStatus;
    }
    if (mHdaRetainedCorbRirbValid &&
        mHdaRetainedCorbRirbState.PciIo == PciIo &&
        (mHdaRetainedCorbRirbState.DmaContainment || mHdaRetainedCorbRirbState.DmaStopUnconfirmed)) {
      ControllerContext.PciDmaContainment = TRUE;
      HdaLog("[HDAU] PCI attribute restore suppressed while CORB/RIRB DMA stop remains unconfirmed or contained");
    }
  }
  RestoreStatus = HdaRestorePciAttributes(&ControllerContext);
  if (EFI_ERROR(RestoreStatus)) {
    HdaSetLastStatus("PciAttributesRestoreFailed");
    /* Preserve a successful playback result to avoid trying another controller
       and playing the chime twice solely because state restoration failed. */
    if (EFI_ERROR(Result)) Result = RestoreStatus;
  }
  return Result;
}

EFI_STATUS
EFIAPI
HdaUniversalBootChimeDxeEntry(
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
) {
  EFI_STATUS Status;
  EFI_HANDLE *Handles = NULL;
  UINTN HandleCount = 0;
  UINTN Index;
  UINTN Pass;
  UINTN PassCount;
  EFI_STATUS LastStatus = EFI_NOT_FOUND;

  (VOID)SystemTable;
  HdaDebugInit(ImageHandle);

  if (HdaTraceStage(1, "driver entry")) {
    HdaDebugFinish();
    return EFI_SUCCESS;
  }

  Status = gBS->LocateHandleBuffer(ByProtocol, &gEfiPciIoProtocolGuid, NULL, &HandleCount, &Handles);
  if (Status != EFI_SUCCESS) {
    /* LocateHandleBuffer() transfers pool ownership only on clean success. */
    Handles = NULL;
    HandleCount = 0;
    if (!EFI_ERROR(Status)) Status = EFI_DEVICE_ERROR;
    HdaLog("[HDAU] LocateHandleBuffer PCI failed: %r", Status);
    HdaSetLastStatus("NoPciIoHandles");
    HdaDebugFinish();
    return EFI_SUCCESS;
  }
  if (HandleCount == 0 || Handles == NULL) {
    HdaLog("[HDAU] LocateHandleBuffer PCI returned no handles: count=%u", (UINT32)HandleCount);
    HdaSetLastStatus("NoPciIoHandles");
    if (Handles != NULL) FreePool(Handles);
    HdaDebugFinish();
    return EFI_SUCCESS;
  }

  if (!HdaAcquireBootGuard()) {
    if (Handles != NULL) FreePool(Handles);
    HdaDebugFinish();
    return EFI_SUCCESS;
  }

  HdaLog("[HDAU] PCI handles=%u", (UINT32)HandleCount);

  /* Every path that can touch HDA DMA or temporary CORB/RIRB state must have
     a terminal cleanup opportunity before ownership is acquired.  FastBoot
     already enforced this at transfer time; enforce the same invariant for
     blocking playback and diagnostic verb traffic, whose exceptional cleanup
     paths can also retain hardware resources. */
  Status = HdaInstallExitBootServicesCleanup();
  if (EFI_ERROR(Status)) {
    HdaLog("[HDAU] playback/diagnostics skipped because boot-handoff cleanup is unavailable: %r", Status);
    HdaSetLastStatus("ExitBootServicesCleanupUnavailable");
    if (Handles != NULL) FreePool(Handles);
    HdaDebugFinish();
    return EFI_SUCCESS;
  }

  if (mHdaCodecDumpRequested) {
    (VOID)HdaDumpAllControllers(Handles, HandleCount);
    if (mHdaRetainedCorbRirbValid) {
      HdaLog("[HDAU] playback suppressed because codec dump retained CORB/RIRB cleanup ownership");
      HdaSetLastStatus("CodecDumpCleanupOwnershipRetained");
      if (Handles != NULL) FreePool(Handles);
      HdaDebugFinish();
      return EFI_SUCCESS;
    }
    if (mHdaCfg.CodecDumpSkipsPlayback) {
      HdaLog("[HDAU] codec dump requested; skipping playback by config");
      if (Handles != NULL) FreePool(Handles);
      HdaDebugFinish();
      return EFI_SUCCESS;
    }
  }

  /* Install the interactive dump key only after any boot/config-requested
     synchronous dump has completed, so diagnostics cannot recursively walk
     the controller from the input callback path. From this point through
     HdaDebugFinish(), asynchronous TPL_CALLBACK workers must not preempt
     entry-owned global/log state. */
  mHdaEntryWorkInProgress = TRUE;
  HdaInstallBootScreenDNotify();

  PassCount = mHdaCfg.PreferIntel ? 2 : 1;

  for (Pass = 0; Pass < PassCount; Pass++) {
    for (Index = 0; Index < HandleCount; Index++) {
      EFI_PCI_IO_PROTOCOL *PciIo = NULL;
      HDAU_PCI_INFO Info;

      Status = gBS->HandleProtocol(Handles[Index], &gEfiPciIoProtocolGuid, (VOID **)&PciIo);
      if (Status != EFI_SUCCESS || PciIo == NULL) continue;
      if (!PciIsHdaController(PciIo, &Info)) continue;

      if (mHdaCfg.PreferIntel) {
        if (Pass == 0 && Info.VendorId != 0x8086) continue;
        if (Pass == 1 && Info.VendorId == 0x8086) continue;
      }

      HdaLog("[HDAU] HDA candidate pass=%u seg=%u bus=%02x dev=%02x func=%02x vendor=%04x device=%04x",
             (UINT32)Pass, (UINT32)Info.Segment, (UINT32)Info.Bus, (UINT32)Info.Device, (UINT32)Info.Function,
             Info.VendorId, Info.DeviceId);

      mHdaControllerOperationInProgress = TRUE;
      LastStatus = HdaTryController(PciIo);
      mHdaControllerOperationInProgress = FALSE;

      /* If D was pressed while the synchronous controller path owned local
         hardware state, perform the deferred dump now, never re-entrantly. */
      HdaServiceDeferredCodecDumpIfSafe();
      if (mHdaF6DumpDone && mHdaCfg.CodecDumpSkipsPlayback) {
        LastStatus = EFI_ABORTED;
      }

      /* A failed stream/ring cleanup is global hardware ownership, not merely
         a failure of this candidate.  Never continue to a second controller
         while the first controller still has DMA buffers, mappings, ring
         state, or a retry context retained for terminal cleanup.  Continuing
         here could compound recovery state and leave two controllers mutated
         at once.  A successful FastBoot transfer also owns mHdaAsyncContext,
         but LastStatus remains success and follows the normal success exit. */
      if (mHdaAsyncContext != NULL || mHdaRetainedCorbRirbValid) {
        HdaLog("[HDAU] controller iteration stopped because cleanup ownership is retained: async=%u corbRirb=%u",
               mHdaAsyncContext != NULL ? 1 : 0, mHdaRetainedCorbRirbValid ? 1 : 0);
        if (EFI_ERROR(LastStatus)) LastStatus = EFI_ABORTED;
        break;
      }
      if (!EFI_ERROR(LastStatus) || LastStatus == EFI_ABORTED) break;
    }
    if (!EFI_ERROR(LastStatus) || LastStatus == EFI_ABORTED) break;
  }

  if (EFI_ERROR(LastStatus) && LastStatus != EFI_ABORTED) {
    HdaLog("[HDAU] no controller produced playback/probe success: %r", LastStatus);
  }

  if (Handles != NULL) FreePool(Handles);
  HdaDebugFinish();
  mHdaEntryWorkInProgress = FALSE;

  /* A D request received while entry was active was deliberately not armed
     asynchronously. If no controller-safe synchronous service point consumed
     it, hand it to the timer only after the entry log/file state is closed. */
  if (mHdaDumpDeferredPending && !mHdaF6DumpInProgress && !mHdaF6DumpDone) {
    Status = HdaArmDeferredCodecDumpTimer(0);
    if (Status != EFI_SUCCESS) mHdaDumpDeferredPending = FALSE;
  }
  return EFI_SUCCESS;
}
