#include "original_alert_win32.hpp"

#include <cstdint>
#include <type_traits>

namespace simtower {

static_assert(std::is_same_v<UINT, std::uint32_t>);
static_assert(kOriginalAlertIconExclamation == MB_ICONEXCLAMATION);
static_assert(kOriginalAlertSystemModal == MB_SYSTEMMODAL);
static_assert(kOriginalAlertIconMask == MB_ICONMASK);
static_assert(kOriginalAlertButtonsOk == MB_OK);
static_assert(kOriginalAlertButtonsOkCancel == MB_OKCANCEL);
static_assert(kOriginalAlertButtonsAbortRetryIgnore == MB_ABORTRETRYIGNORE);
static_assert(kOriginalAlertButtonsYesNoCancel == MB_YESNOCANCEL);
static_assert(kOriginalAlertButtonsYesNo == MB_YESNO);
static_assert(kOriginalAlertButtonsRetryCancel == MB_RETRYCANCEL);
static_assert(kOriginalMessageBoxTypeMask == MB_TYPEMASK);
static_assert(kOriginalAlertResultOk == IDOK);
static_assert(kOriginalAlertResultCancel == IDCANCEL);
static_assert(kOriginalAlertResultAbort == IDABORT);
static_assert(kOriginalAlertResultRetry == IDRETRY);
static_assert(kOriginalAlertResultIgnore == IDIGNORE);
static_assert(kOriginalAlertResultYes == IDYES);
static_assert(kOriginalAlertResultNo == IDNO);

int show_original_alert(
    HWND owner,
    const OriginalResources& resources,
    int resource_id,
    const std::array<std::string_view, 4>& substitutions) {
  // Native owner/resource boundary for the shared 11e0:0c9e wrapper and its
  // 1208:0133/017a/0274/0369 resource/format/result helpers. The ALRT
  // bytes are immutable spans in the self-contained resource pack, so the
  // obsolete Win16 lock-count cleanup at 1208:0cb5 is unnecessary here.
  const auto presentation =
      prepare_original_alert(resources, resource_id, substitutions);
  const auto request = build_original_message_box(presentation);
  return original_alert_result(request.button_mode,
                               show_original_message_box(owner, request));
}

int show_original_message_box(HWND owner,
                              const OriginalMessageBoxRequest& request) {
  if (request.beep_first) MessageBeep(static_cast<UINT>(-1));
  return MessageBoxA(owner, request.text.c_str(), request.caption.c_str(),
                     request.style);
}

}  // namespace simtower
