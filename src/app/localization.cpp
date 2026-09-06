#include "app/localization.h"

namespace stage_manager::app {
namespace {

std::wstring_view chinese_text(UiText text) noexcept
{
    switch (text) {
    case UiText::CustomSettingTitle: return L"\u81ea\u5b9a\u4e49\u53c2\u6570";
    case UiText::SettingsTitle: return L"\u7a97\u53e3\u7ba1\u7406\u5668\u8bbe\u7f6e";
    case UiText::Ok: return L"\u786e\u5b9a";
    case UiText::Cancel: return L"\u53d6\u6d88";
    case UiText::CurrentValue: return L"\u5f53\u524d\u503c\uff1a";
    case UiText::PreviewHint: return L"\u63d0\u793a\uff1a\u7eff\u8272\u533a\u57df\u8868\u793a\u672a\u6fc0\u6d3b\u7a97\u53e3\u9700\u8981\u4fdd\u7559\u7684\u53ef\u70b9\u51fb\u8fb9\u7f18\u3002\u4fee\u6539\u6570\u503c\u65f6\u9884\u89c8\u4f1a\u540c\u6b65\u53d8\u5316\u3002";
    case UiText::RestoreDefaults: return L"\u6062\u590d\u9ed8\u8ba4";
    case UiText::SaveAndApply: return L"\u4fdd\u5b58\u5e76\u5e94\u7528";
    case UiText::UnitPrefix: return L"\u5355\u4f4d\uff1a";
    case UiText::InputRange: return L"    \u53ef\u8f93\u5165\u8303\u56f4\uff1a";
    case UiText::ChooseCandidate: return L"    \u8bf7\u4ece\u5019\u9009\u503c\u4e2d\u9009\u62e9";
    case UiText::CurrentMonitor: return L"\r\n\u5f53\u524d\u663e\u793a\u5668\uff1a";
    case UiText::Pixels: return L" \u50cf\u7d20";
    case UiText::InvalidParameterMessage: return L"\u8bf7\u9009\u62e9\u9884\u8bbe\u503c\uff0c\u6216\u8f93\u5165\u5141\u8bb8\u8303\u56f4\u5185\u7684\u6574\u6570\u3002";
    case UiText::InvalidParameterTitle: return L"\u53c2\u6570\u65e0\u6548";
    case UiText::ActiveWindow: return L"\u5f53\u524d\u6d3b\u52a8\u7a97\u53e3";
    case UiText::PreviewCaption: return L"\u5de6\u534a\u6807\u9898\u680f\u5b8c\u6574\u9732\u51fa\uff1b\u4fa7\u6761\u5e2e\u52a9\u8fa8\u8ba4";
    case UiText::EnterIntegerPrefix: return L"\uff1a\u8bf7\u8f93\u5165 ";
    case UiText::EnterIntegerSuffix: return L" \u4e4b\u95f4\u7684\u6574\u6570";
    case UiText::InvalidValueMessage: return L"\u8bf7\u8f93\u5165\u63d0\u793a\u8303\u56f4\u5185\u7684\u6574\u6570\u3002";
    case UiText::InvalidValueTitle: return L"\u6570\u503c\u65e0\u6548";
    case UiText::CurrentCustomValue: return L"\u5f53\u524d\u81ea\u5b9a\u4e49\u503c\uff1a";
    case UiText::Custom: return L"\u81ea\u5b9a\u4e49\u2026";
    case UiText::PauseManagement: return L"\u6682\u505c\u7ba1\u7406";
    case UiText::EnableManagement: return L"\u542f\u7528\u7ba1\u7406";
    case UiText::OpenVisualSettings: return L"\u6253\u5f00\u53ef\u89c6\u5316\u8bbe\u7f6e\u2026";
    case UiText::QuickSettings: return L"\u5feb\u901f\u53c2\u6570\u8bbe\u7f6e";
    case UiText::Exit: return L"\u9000\u51fa";
    case UiText::UnsatisfiableTitle: return L"\u5f53\u524d\u7ea6\u675f\u4e0b\u6ca1\u6709\u53ef\u884c\u5e03\u5c40";
    case UiText::UnsatisfiableMessage: return L"\u5df2\u68c0\u67e5\u5f53\u524d\u5019\u9009\u4f4d\u7f6e\uff0c\u4f46\u6ca1\u6709\u65b9\u6848\u80fd\u540c\u65f6\u6ee1\u8db3\u53ef\u8fa8\u8bc6\u8fb9\u7f18\u548c\u5b89\u5168\u7ea6\u675f\u3002\u672a\u6539\u53d8\u7a97\u53e3\u5c42\u7ea7\uff0c\u5c06\u5728\u7a97\u53e3\u72b6\u6001\u53d8\u5316\u540e\u91cd\u8bd5\u3002";
    case UiText::SearchLimitTitle: return L"\u7a97\u53e3\u5e03\u5c40\u8ba1\u7b97\u672a\u5b8c\u6210";
    case UiText::SearchLimitMessage: return L"\u7a97\u53e3\u8f83\u591a\u6216\u91cd\u53e0\u8f83\u590d\u6742\uff0c\u6c42\u89e3\u5728\u8fbe\u5230\u65f6\u95f4\u6216\u72b6\u6001\u4e0a\u9650\u524d\u672a\u5b8c\u6210\u3002\u8fd9\u4e0d\u8868\u793a\u5c4f\u5e55\u7a7a\u95f4\u4e0d\u8db3\uff1b\u5c06\u5728\u7a97\u53e3\u72b6\u6001\u53d8\u5316\u540e\u91cd\u8bd5\u3002";
    case UiText::GeometryTooComplexTitle: return L"\u7a97\u53e3\u906e\u6321\u5173\u7cfb\u8fc7\u4e8e\u590d\u6742";
    case UiText::GeometryTooComplexMessage: return L"\u53ef\u89c1\u533a\u57df\u7684\u51e0\u4f55\u5206\u5272\u8d85\u8fc7\u4e86\u5b89\u5168\u4e0a\u9650\u3002\u672c\u8f6e\u672a\u5957\u7528\u672a\u7ecf\u9a8c\u8bc1\u7684\u5e03\u5c40\uff0c\u5c06\u5728\u7a97\u53e3\u72b6\u6001\u53d8\u5316\u540e\u91cd\u8bd5\u3002";
    case UiText::InvalidLayoutInputTitle: return L"\u7a97\u53e3\u72b6\u6001\u6682\u65f6\u65e0\u6cd5\u7528\u4e8e\u5e03\u5c40";
    case UiText::InvalidLayoutInputMessage: return L"\u672c\u8f6e\u7684\u7a97\u53e3\u51e0\u4f55\u6216\u6c42\u89e3\u7ea6\u675f\u65e0\u6548\u3002\u7a0b\u5e8f\u672a\u79fb\u52a8\u7a97\u53e3\uff0c\u5c06\u5728\u83b7\u53d6\u5230\u65b0\u72b6\u6001\u540e\u91cd\u8bd5\u3002";
    case UiText::LayoutFailureTitle: return L"\u7a97\u53e3\u5e03\u5c40\u6682\u65f6\u5931\u8d25";
    case UiText::LayoutFailureMessage: return L"\u672c\u8f6e\u6ca1\u6709\u4ea7\u751f\u53ef\u5b89\u5168\u5957\u7528\u7684\u5e03\u5c40\u3002\u672a\u6539\u53d8\u7a97\u53e3\u5c42\u7ea7\uff0c\u5c06\u5728\u7a97\u53e3\u72b6\u6001\u53d8\u5316\u540e\u91cd\u8bd5\u3002";
    case UiText::TooltipRunning: return L"Windows \u7a97\u53e3\u7ba1\u7406\u5668\uff08\u8fd0\u884c\u4e2d\uff09";
    case UiText::TooltipPaused: return L"Windows \u7a97\u53e3\u7ba1\u7406\u5668\uff08\u5df2\u6682\u505c\uff09";
    case UiText::TooltipUnsatisfiable: return L"Windows \u7a97\u53e3\u7ba1\u7406\u5668\uff08\u65e0\u53ef\u7528\u5e03\u5c40\uff09";
    case UiText::TooltipApiError: return L"Windows \u7a97\u53e3\u7ba1\u7406\u5668\uff08API \u9519\u8bef\uff09";
    case UiText::TooltipRebuilding: return L"Windows \u7a97\u53e3\u7ba1\u7406\u5668\uff08\u6b63\u5728\u91cd\u5efa\uff09";
    }
    return L"";
}

std::wstring_view english_text(UiText text) noexcept
{
    switch (text) {
    case UiText::CustomSettingTitle: return L"Custom setting";
    case UiText::SettingsTitle: return L"Window Manager Settings";
    case UiText::Ok: return L"OK";
    case UiText::Cancel: return L"Cancel";
    case UiText::CurrentValue: return L"Current value:";
    case UiText::PreviewHint: return L"Tip: Green areas are the clickable edges retained for inactive windows. The preview updates as values change.";
    case UiText::RestoreDefaults: return L"Restore defaults";
    case UiText::SaveAndApply: return L"Save and apply";
    case UiText::UnitPrefix: return L"Unit: ";
    case UiText::InputRange: return L"    Valid range: ";
    case UiText::ChooseCandidate: return L"    Choose one of the available values";
    case UiText::CurrentMonitor: return L"\r\nCurrent monitor: ";
    case UiText::Pixels: return L" pixels";
    case UiText::InvalidParameterMessage: return L"Select a preset or enter an integer within the valid range.";
    case UiText::InvalidParameterTitle: return L"Invalid setting";
    case UiText::ActiveWindow: return L"Active window";
    case UiText::PreviewCaption: return L"Full-height left half of title bar; side strips aid recognition";
    case UiText::EnterIntegerPrefix: return L": enter an integer from ";
    case UiText::EnterIntegerSuffix: return L".";
    case UiText::InvalidValueMessage: return L"Enter an integer within the displayed range.";
    case UiText::InvalidValueTitle: return L"Invalid value";
    case UiText::CurrentCustomValue: return L"Current custom value: ";
    case UiText::Custom: return L"Custom...";
    case UiText::PauseManagement: return L"Pause management";
    case UiText::EnableManagement: return L"Enable management";
    case UiText::OpenVisualSettings: return L"Open visual settings...";
    case UiText::QuickSettings: return L"Quick settings";
    case UiText::Exit: return L"Exit";
    case UiText::UnsatisfiableTitle: return L"No feasible layout under the current constraints";
    case UiText::UnsatisfiableMessage: return L"The available candidates were checked, but none satisfied both the recognizable-edge and safety constraints. Window Z-order was not changed; the app will retry after window state changes.";
    case UiText::SearchLimitTitle: return L"Window layout calculation did not finish";
    case UiText::SearchLimitMessage: return L"Many or heavily overlapping windows caused the search to reach its time or state limit. This does not mean screen space is insufficient; the app will retry after window state changes.";
    case UiText::GeometryTooComplexTitle: return L"Window occlusion geometry is too complex";
    case UiText::GeometryTooComplexMessage: return L"The visible-region subdivision exceeded its safety limit. No unverified layout was applied; the app will retry after window state changes.";
    case UiText::InvalidLayoutInputTitle: return L"Window state cannot currently be used for layout";
    case UiText::InvalidLayoutInputMessage: return L"Window geometry or solver constraints were invalid for this batch. No windows were moved; the app will retry after obtaining fresh state.";
    case UiText::LayoutFailureTitle: return L"Window layout temporarily failed";
    case UiText::LayoutFailureMessage: return L"This batch produced no layout that could be applied safely. Window Z-order was not changed; the app will retry after window state changes.";
    case UiText::TooltipRunning: return L"Windows Stage Manager (running)";
    case UiText::TooltipPaused: return L"Windows Stage Manager (paused)";
    case UiText::TooltipUnsatisfiable: return L"Windows Stage Manager (no layout available)";
    case UiText::TooltipApiError: return L"Windows Stage Manager (API error)";
    case UiText::TooltipRebuilding: return L"Windows Stage Manager (rebuilding)";
    }
    return L"";
}

std::wstring numeric_prefix(std::uint32_t value, bool include_numeric_prefix)
{
    return include_numeric_prefix ? std::to_wstring(value) + L" - " : L"";
}

} // namespace

std::wstring_view ui_text(UiLanguage language, UiText text) noexcept
{
    return language == UiLanguage::English ? english_text(text) : chinese_text(text);
}

std::wstring_view setting_field_title(SettingField field, UiLanguage language) noexcept
{
    if (language == UiLanguage::English) {
        switch (field) {
        case SettingField::DryRun: return L"Run mode";
        case SettingField::PlaceActivatedWindow: return L"Place newly activated windows";
        case SettingField::ActivationHorizontalAlignment: return L"Activated-window horizontal position";
        case SettingField::ActivationVerticalAlignment: return L"Activated-window vertical position";
        case SettingField::AffordancePreset: return L"Recognizability preset";
        case SettingField::TopMinimumLengthDip: return L"Top minimum length";
        case SettingField::TopMaximumLengthDip: return L"Top maximum length";
        case SettingField::TopDepthDip: return L"Minimum exposed height";
        case SettingField::TopLengthPercent: return L"Top dynamic percentage";
        case SettingField::LeftMinimumLengthDip: return L"Left minimum length";
        case SettingField::LeftMaximumLengthDip: return L"Left maximum length";
        case SettingField::LeftDepthDip: return L"Left depth";
        case SettingField::LeftLengthPercent: return L"Left dynamic percentage";
        case SettingField::RightMinimumLengthDip: return L"Right minimum length";
        case SettingField::RightMaximumLengthDip: return L"Right maximum length";
        case SettingField::RightDepthDip: return L"Right depth";
        case SettingField::RightLengthPercent: return L"Right dynamic percentage";
        case SettingField::BottomMinimumLengthDip: return L"Bottom minimum length";
        case SettingField::BottomMaximumLengthDip: return L"Bottom maximum length";
        case SettingField::BottomDepthDip: return L"Bottom depth";
        case SettingField::BottomLengthPercent: return L"Bottom dynamic percentage";
        case SettingField::MinimumOnscreenWidthDip: return L"Minimum on-screen width";
        case SettingField::MinimumOnscreenHeightDip: return L"Minimum on-screen height";
        case SettingField::EventCoalesceWindowMs: return L"Event coalescing window";
        case SettingField::ReconcileIntervalMs: return L"State refresh interval";
        case SettingField::MaximumMovesPerBatch: return L"Maximum moves per batch";
        case SettingField::MaximumSolverStates: return L"Maximum solver states";
        case SettingField::MaximumSolveTimeMs: return L"Maximum solve time";
        case SettingField::MaximumManagedWindows: return L"Maximum managed windows";
        case SettingField::MaximumConsecutiveFailures: return L"Consecutive failure threshold";
        case SettingField::UiLanguage: return L"Interface language";
        case SettingField::Count: return L"Unknown setting";
        }
    }
    switch (field) {
    case SettingField::DryRun: return L"\u8fd0\u884c\u6a21\u5f0f";
    case SettingField::PlaceActivatedWindow: return L"\u81ea\u52a8\u653e\u7f6e\u65b0\u6fc0\u6d3b\u7a97\u53e3";
    case SettingField::ActivationHorizontalAlignment: return L"\u65b0\u6fc0\u6d3b\u7a97\u53e3\u6c34\u5e73\u4f4d\u7f6e";
    case SettingField::ActivationVerticalAlignment: return L"\u65b0\u6fc0\u6d3b\u7a97\u53e3\u5782\u76f4\u4f4d\u7f6e";
    case SettingField::AffordancePreset: return L"\u53ef\u8fa8\u8bc6\u5ea6\u9884\u8bbe";
    case SettingField::TopMinimumLengthDip: return L"\u9876\u90e8\u6700\u5c0f\u957f\u5ea6";
    case SettingField::TopMaximumLengthDip: return L"\u9876\u90e8\u6700\u5927\u957f\u5ea6";
    case SettingField::TopDepthDip: return L"\u9876\u90e8\u6700\u5c0f\u9732\u51fa\u9ad8\u5ea6";
    case SettingField::TopLengthPercent: return L"\u9876\u90e8\u52a8\u6001\u6bd4\u4f8b";
    case SettingField::LeftMinimumLengthDip: return L"\u5de6\u4fa7\u6700\u5c0f\u957f\u5ea6";
    case SettingField::LeftMaximumLengthDip: return L"\u5de6\u4fa7\u6700\u5927\u957f\u5ea6";
    case SettingField::LeftDepthDip: return L"\u5de6\u4fa7\u6df1\u5ea6";
    case SettingField::LeftLengthPercent: return L"\u5de6\u4fa7\u52a8\u6001\u6bd4\u4f8b";
    case SettingField::RightMinimumLengthDip: return L"\u53f3\u4fa7\u6700\u5c0f\u957f\u5ea6";
    case SettingField::RightMaximumLengthDip: return L"\u53f3\u4fa7\u6700\u5927\u957f\u5ea6";
    case SettingField::RightDepthDip: return L"\u53f3\u4fa7\u6df1\u5ea6";
    case SettingField::RightLengthPercent: return L"\u53f3\u4fa7\u52a8\u6001\u6bd4\u4f8b";
    case SettingField::BottomMinimumLengthDip: return L"\u5e95\u90e8\u6700\u5c0f\u957f\u5ea6";
    case SettingField::BottomMaximumLengthDip: return L"\u5e95\u90e8\u6700\u5927\u957f\u5ea6";
    case SettingField::BottomDepthDip: return L"\u5e95\u90e8\u6df1\u5ea6";
    case SettingField::BottomLengthPercent: return L"\u5e95\u90e8\u52a8\u6001\u6bd4\u4f8b";
    case SettingField::MinimumOnscreenWidthDip: return L"\u6700\u5c0f\u5c4f\u4e0a\u5bbd\u5ea6";
    case SettingField::MinimumOnscreenHeightDip: return L"\u6700\u5c0f\u5c4f\u4e0a\u9ad8\u5ea6";
    case SettingField::EventCoalesceWindowMs: return L"\u4e8b\u4ef6\u5408\u5e76\u7a97\u53e3";
    case SettingField::ReconcileIntervalMs: return L"\u72b6\u6001\u5237\u65b0\u95f4\u9694";
    case SettingField::MaximumMovesPerBatch: return L"\u6bcf\u6279\u6700\u5927\u79fb\u52a8\u6570";
    case SettingField::MaximumSolverStates: return L"\u6700\u5927\u6c42\u89e3\u72b6\u6001\u6570";
    case SettingField::MaximumSolveTimeMs: return L"\u6700\u5927\u6c42\u89e3\u65f6\u95f4";
    case SettingField::MaximumManagedWindows: return L"\u6700\u5927\u7ba1\u7406\u7a97\u53e3\u6570";
    case SettingField::MaximumConsecutiveFailures: return L"\u8fde\u7eed\u5931\u8d25\u9608\u503c";
    case SettingField::UiLanguage: return L"\u754c\u9762\u8bed\u8a00";
    case SettingField::Count: return L"\u672a\u77e5\u8bbe\u7f6e";
    }
    return L"";
}

std::wstring setting_choice_text(SettingField field,
                                 std::uint32_t value,
                                 UiLanguage language,
                                 bool include_numeric_prefix)
{
    auto prefix = numeric_prefix(value, include_numeric_prefix);
    const bool english = language == UiLanguage::English;
    switch (field) {
    case SettingField::DryRun:
        return prefix + (value == 0 ? (english ? L"Apply window moves" : L"\u5e94\u7528\u7a97\u53e3\u8c03\u6574")
                                    : (english ? L"Preview only (DryRun)" : L"\u4ec5\u9884\u89c8\uff08DryRun\uff09"));
    case SettingField::PlaceActivatedWindow:
        return prefix + (value == 0 ? (english ? L"Off" : L"\u5173\u95ed")
                                    : (english ? L"On" : L"\u5f00\u542f"));
    case SettingField::UiLanguage:
        return prefix + (value == 0 ? L"\u7b80\u4f53\u4e2d\u6587" : L"English");
    case SettingField::ActivationHorizontalAlignment:
        switch (static_cast<ActivationHorizontalAlignment>(value)) {
        case ActivationHorizontalAlignment::Left: return prefix + (english ? L"Left" : L"\u9760\u5de6");
        case ActivationHorizontalAlignment::Center: return prefix + (english ? L"Center" : L"\u6c34\u5e73\u5c45\u4e2d");
        case ActivationHorizontalAlignment::Right: return prefix + (english ? L"Right" : L"\u9760\u53f3");
        }
        break;
    case SettingField::ActivationVerticalAlignment:
        switch (static_cast<ActivationVerticalAlignment>(value)) {
        case ActivationVerticalAlignment::Top: return prefix + (english ? L"Top" : L"\u9760\u4e0a");
        case ActivationVerticalAlignment::Center: return prefix + (english ? L"Center" : L"\u5782\u76f4\u5c45\u4e2d");
        case ActivationVerticalAlignment::Bottom: return prefix + (english ? L"Bottom" : L"\u9760\u4e0b");
        }
        break;
    case SettingField::AffordancePreset:
        switch (static_cast<AffordancePreset>(value)) {
        case AffordancePreset::Compact: return prefix + (english ? L"Compact" : L"\u7d27\u51d1");
        case AffordancePreset::Balanced: return prefix + (english ? L"Balanced" : L"\u5e73\u8861");
        case AffordancePreset::Prominent: return prefix + (english ? L"Prominent" : L"\u9192\u76ee");
        case AffordancePreset::Custom: return prefix + (english ? L"Custom" : L"\u81ea\u5b9a\u4e49");
        }
        break;
    default: break;
    }

    auto result = std::to_wstring(value);
    switch (field) {
    case SettingField::TopMinimumLengthDip:
    case SettingField::TopMaximumLengthDip:
    case SettingField::TopDepthDip:
    case SettingField::LeftMinimumLengthDip:
    case SettingField::LeftMaximumLengthDip:
    case SettingField::LeftDepthDip:
    case SettingField::RightMinimumLengthDip:
    case SettingField::RightMaximumLengthDip:
    case SettingField::RightDepthDip:
    case SettingField::BottomMinimumLengthDip:
    case SettingField::BottomMaximumLengthDip:
    case SettingField::BottomDepthDip:
    case SettingField::MinimumOnscreenWidthDip:
    case SettingField::MinimumOnscreenHeightDip: return result + L" DIP";
    case SettingField::TopLengthPercent:
    case SettingField::LeftLengthPercent:
    case SettingField::RightLengthPercent:
    case SettingField::BottomLengthPercent: return result + L"%";
    case SettingField::EventCoalesceWindowMs:
    case SettingField::ReconcileIntervalMs:
    case SettingField::MaximumSolveTimeMs: return result + L" ms";
    case SettingField::MaximumMovesPerBatch: return result + (english ? L" moves" : L" \u6b21\u79fb\u52a8");
    case SettingField::MaximumSolverStates: return result + (english ? L" states" : L" \u4e2a\u72b6\u6001");
    case SettingField::MaximumManagedWindows: return result + (english ? L" windows" : L" \u4e2a\u7a97\u53e3");
    case SettingField::MaximumConsecutiveFailures: return result + (english ? L" failures" : L" \u6b21\u5931\u8d25");
    default: return result;
    }
}

} // namespace stage_manager::app
