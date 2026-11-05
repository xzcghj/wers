#pragma once
// The text of what the plugins of the series share, in one file.
namespace wers::ui::text {

inline constexpr const char* presetChoose = "Wybierz preset";
inline constexpr const char* presetName = "Nazwa presetu";
inline constexpr const char* presetSave = "Zapisz";
inline constexpr const char* presetDelete = "Usuń preset";
inline constexpr const char* presetPreviousTip = "Poprzedni preset";
inline constexpr const char* presetNextTip = "Następny preset";
inline constexpr const char* presetListTip = "Lista presetów";
inline constexpr const char* presetSaveTip = "Zapisuje obecne ustawienia jako nowy preset.";
inline constexpr const char* presetSlotTip = "Dwa zestawy ustawień do porównania. Oba zapisują się w projekcie.";
inline constexpr const char* presetCopyTip = "Kopiuje obecne ustawienia do drugiego zestawu.";

// These show where the name of the preset usually is, so they stay short.
inline constexpr const char* presetNeedsName = "Podaj nazwę presetu.";
inline constexpr const char* presetIsFactory = "To nazwa presetu fabrycznego.";
inline constexpr const char* presetNoFolder = "Nie mogę utworzyć folderu.";
inline constexpr const char* presetNoWrite = "Nie mogę zapisać presetu.";
inline constexpr const char* presetNoRead = "Nie mogę wczytać presetu.";
inline constexpr const char* presetNoDelete = "Nie mogę usunąć presetu.";

} // namespace wers::ui::text
