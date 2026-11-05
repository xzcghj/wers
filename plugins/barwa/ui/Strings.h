#pragma once
// All text of Barwa in one file, so another language is a simple addition later.
namespace barwa::text {

inline constexpr const char* bands = "Liczba pasm";
inline constexpr const char* split1 = "Podział 1";
inline constexpr const char* split2 = "Podział 2";
inline constexpr const char* band = "Pasmo";
inline constexpr const char* character = "charakter";
inline constexpr const char* solo = "solo";
inline constexpr const char* drive = "Drive";
inline constexpr const char* mix = "Mix";
inline constexpr const char* mask = "Maska głosek";
inline constexpr const char* tone = "Ton";
inline constexpr const char* autoLevel = "Auto poziom";
inline constexpr const char* output = "Output";
inline constexpr const char* delta = "Delta";
inline constexpr const char* quality = "Jakość";
inline constexpr const char* bypass = "Bypass";

inline constexpr const char* bandsShort = "Pasma";
inline constexpr const char* whole = "Całość";
inline constexpr const char* wholeBand = "Całe pasmo";
inline constexpr const char* wholeBandText = "cały sygnał jednym charakterem";
inline constexpr const char* upTo = "do";
inline constexpr const char* from = "od";
inline constexpr const char* spectrumTitle = "Widmo";
inline constexpr const char* overload = "Przester na wyjściu";
inline constexpr const char* maskStripTitle = "Barwa na głoskach";
inline constexpr const char* maskStripText = "Ostatnie 6 s. Im wyżej, tym więcej barwy dostaje głos.";
inline constexpr const char* maskStripOff = "Maska wyłączona. Barwa trafia na cały dźwięk.";

inline constexpr const char* maskTip = "Chroni głoski syczące i oddechy. Przy 100% barwę dostają tylko samogłoski i trochę zwarte. Na bicie i basie ustaw 0%.";
inline constexpr const char* toneTip = "Barwa samej dodanej saturacji. W lewo ciemniej, w prawo jaśniej. Suchego dźwięku nie zmienia.";
inline constexpr const char* deltaTip = "Słuchasz samego tego, co wtyczka dodaje.";
inline constexpr const char* bypassTip = "Przepuszcza dźwięk bez zmian, z tym samym opóźnieniem.";
inline constexpr const char* autoLevelTip = "Trzyma głośność taką jak na wejściu, żeby porównywać brzmienie, a nie poziom. Nie rusza tego, co maska zostawia czyste.";
inline constexpr const char* qualityTip = "Więcej to czystsze wysokie tony i większe obciążenie procesora. Zmiana daje krótki trzask podczas odtwarzania.";
inline constexpr const char* bandsTip = "Na ile pasm dzielić dźwięk. Każde ma własny charakter i Drive.";
inline constexpr const char* soloTip = "Słuchasz tylko tego pasma.";
inline constexpr const char* maskStripTip = "Wysoko są samogłoski, nisko syczące i przerwy. Pokazuje, ile barwy trafia na głos w każdej chwili.";

inline constexpr const char* lamp = "Lampa";
inline constexpr const char* tape = "Taśma";
inline constexpr const char* transformer = "Transformator";
inline constexpr const char* transistor = "Tranzystor";

} // namespace barwa::text
