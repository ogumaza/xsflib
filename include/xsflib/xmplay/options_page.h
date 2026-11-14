// SPDX-License-Identifier: MIT

// The options page of the XMPlay input plugin (xsflib/xmplay/input.h). The page is dialog 1000 in
// cmake/templates/xmplay_options.rc. It sets the length and fade of files without a length tag, endless playback and
// rendering ahead. XMPlay stores the settings among the rest of its settings (XMPFUNC_REGISTRY).

#pragma once

#include <windows.h>
#include <xmpin.h>

#include <algorithm>
#include <mutex>
#include <string>

#include "xsflib/player/playback.h"

namespace xsflib::xmplay
{
namespace detail
{

// The options page's controls, numbered as in cmake/templates/xmplay_options.rc. XMPlay's options pages have their
// Apply button at 1000.
constexpr int kLengthEdit = 10;
constexpr int kFadeEdit = 11;
constexpr int kEndlessBox = 12;
constexpr int kRenderAheadBox = 13;
constexpr int kApplyButton = 1000;

// The ranges of the default length and fade, in seconds.
constexpr int kMinLength = 1;
constexpr int kMaxLength = 24 * 3600;
constexpr int kMaxFade = 3600;

// A whole number of seconds from an options page's edit, clamped to [low, high], or `old` when the edit holds none.
inline int SecondsFromEdit(UINT value, BOOL valid, int low, int high, int old)
{
    if (!valid)
    {
        return old;
    }

    return std::max(low, static_cast<int>(std::min<UINT>(value, static_cast<UINT>(high))));
}

} // namespace detail

// The options page of the plugin for `Traits`, and the settings that it sets. XMPlay shows the page in its options
// window, on its main thread. The plugin reads the settings on any thread.
template <typename Traits>
class OptionsPage
{
public:
    // The settings on the page.
    struct Settings
    {
        int default_length_s =
            static_cast<int>(player::PlaybackOptions<typename Traits::Player>::kDefaultLengthMs / 1000);
        int default_fade_s = static_cast<int>(player::PlaybackOptions<typename Traits::Player>::kDefaultFadeMs / 1000);
        bool endless = false;
        bool render_ahead = true;
    };

    // Reads the settings from XMPlay's settings through `registry`. A setting that XMPlay doesn't hold keeps its
    // default. Without `registry`, every setting keeps its default, and the page doesn't store the settings.
    static void Load(XMPFUNC_REGISTRY* registry)
    {
        registry_ = registry;

        Settings settings;
        if (registry_ != nullptr)
        {
            const std::string section = Section();
            int value = 0;
            if (registry_->GetInt(section.c_str(), kLengthKey, &value))
            {
                settings.default_length_s = std::clamp(value, detail::kMinLength, detail::kMaxLength);
            }

            if (registry_->GetInt(section.c_str(), kFadeKey, &value))
            {
                settings.default_fade_s = std::clamp(value, 0, detail::kMaxFade);
            }

            if (registry_->GetInt(section.c_str(), kEndlessKey, &value))
            {
                settings.endless = value != 0;
            }

            if (registry_->GetInt(section.c_str(), kRenderAheadKey, &value))
            {
                settings.render_ahead = value != 0;
            }
        }

        std::lock_guard lock(mutex_);
        settings_ = settings;
    }

    static Settings Current()
    {
        std::lock_guard lock(mutex_);
        return settings_;
    }

    // The page's handler, XMPIN's Options.
    static INT_PTR CALLBACK Handle(HWND page, UINT message, WPARAM wparam, LPARAM /*lparam*/)
    {
        try
        {
            if (message == WM_INITDIALOG)
            {
                Show(page, Current());
                return TRUE;
            }

            if (message != WM_COMMAND)
            {
                return FALSE;
            }

            const int control = LOWORD(wparam);
            const int notification = HIWORD(wparam);
            if (control == detail::kApplyButton && notification == BN_CLICKED)
            {
                Apply(page);
                return TRUE;
            }

            const bool edited =
                (control == detail::kLengthEdit || control == detail::kFadeEdit) && notification == EN_CHANGE;
            const bool clicked =
                (control == detail::kEndlessBox || control == detail::kRenderAheadBox) && notification == BN_CLICKED;
            if (edited || clicked)
            {
                EnableWindow(GetDlgItem(page, detail::kApplyButton), TRUE);
                return TRUE;
            }
        }
        catch (...)
        {
        }

        return FALSE;
    }

private:
    // The settings' keys in XMPlay's settings, in the section that Section() names.
    static constexpr const char* kLengthKey = "Default length";
    static constexpr const char* kFadeKey = "Default fade";
    static constexpr const char* kEndlessKey = "Endless";
    static constexpr const char* kRenderAheadKey = "Render ahead";

    // The section of XMPlay's settings that holds the plugin's.
    static std::string Section()
    {
        return std::string(Traits::kName) + " plugin";
    }

    static void Save(const Settings& settings)
    {
        if (registry_ == nullptr)
        {
            return;
        }

        const std::string section = Section();
        const int endless = settings.endless ? 1 : 0;
        const int render_ahead = settings.render_ahead ? 1 : 0;
        registry_->SetInt(section.c_str(), kLengthKey, &settings.default_length_s);
        registry_->SetInt(section.c_str(), kFadeKey, &settings.default_fade_s);
        registry_->SetInt(section.c_str(), kEndlessKey, &endless);
        registry_->SetInt(section.c_str(), kRenderAheadKey, &render_ahead);
    }

    // Shows `settings` on the page. Nothing is left to apply.
    static void Show(HWND page, const Settings& settings)
    {
        SetDlgItemInt(page, detail::kLengthEdit, static_cast<UINT>(settings.default_length_s), FALSE);
        SetDlgItemInt(page, detail::kFadeEdit, static_cast<UINT>(settings.default_fade_s), FALSE);
        CheckDlgButton(page, detail::kEndlessBox, settings.endless ? BST_CHECKED : BST_UNCHECKED);
        CheckDlgButton(page, detail::kRenderAheadBox, settings.render_ahead ? BST_CHECKED : BST_UNCHECKED);

        // Setting the edits' text enabled the Apply button.
        EnableWindow(GetDlgItem(page, detail::kApplyButton), FALSE);
    }

    // Takes the settings from the page, stores them in XMPlay's settings, and shows them as they apply. The next file
    // that XMPlay opens plays with them.
    static void Apply(HWND page)
    {
        Settings settings = Current();
        BOOL valid = FALSE;
        UINT value = GetDlgItemInt(page, detail::kLengthEdit, &valid, FALSE);
        settings.default_length_s =
            detail::SecondsFromEdit(value, valid, detail::kMinLength, detail::kMaxLength, settings.default_length_s);
        value = GetDlgItemInt(page, detail::kFadeEdit, &valid, FALSE);
        settings.default_fade_s = detail::SecondsFromEdit(value, valid, 0, detail::kMaxFade, settings.default_fade_s);
        settings.endless = IsDlgButtonChecked(page, detail::kEndlessBox) == BST_CHECKED;
        settings.render_ahead = IsDlgButtonChecked(page, detail::kRenderAheadBox) == BST_CHECKED;

        {
            std::lock_guard lock(mutex_);
            settings_ = settings;
        }

        Save(settings);
        Show(page, settings);
    }

    inline static XMPFUNC_REGISTRY* registry_ = nullptr; // without it, the settings aren't stored

    inline static std::mutex mutex_;
    inline static Settings settings_; // guarded by mutex_
};

} // namespace xsflib::xmplay
