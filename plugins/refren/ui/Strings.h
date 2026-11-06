#pragma once
// All text of Refren in one file, so another language is a simple addition later.
namespace refren::text {

inline constexpr const char* amount = "Ile";
inline constexpr const char* width = "Szerokość";
inline constexpr const char* time = "Czas";
inline constexpr const char* tilt = "Ton";
inline constexpr const char* grain = "Ziarno";
inline constexpr const char* grainChar = "Charakter ziarna";
inline constexpr const char* motion = "Rodzaj ruchu";
inline constexpr const char* motionRate = "Tempo ruchu";
inline constexpr const char* motionDepth = "Ruch";
inline constexpr const char* echoMix = "Echo";
inline constexpr const char* echoTime = "Czas echa";
inline constexpr const char* echoFeedback = "Sprzężenie echa";
inline constexpr const char* echoPing = "Ping-pong";
inline constexpr const char* echoTone = "Góra echa";
inline constexpr const char* verbMix = "Pogłos";
inline constexpr const char* verbSize = "Rozmiar";
inline constexpr const char* verbDecay = "Czas pogłosu";
inline constexpr const char* verbTone = "Góra pogłosu";
inline constexpr const char* verbPre = "Predelay";
inline constexpr const char* verbShimmer = "Shimmer";
inline constexpr const char* duck = "Chowanie";
inline constexpr const char* sendDry = "Wysyłka suchego";
inline constexpr const char* consonants = "Spółgłoski w środku";
inline constexpr const char* dry = "Suchy";
inline constexpr const char* trim = "Wyrównanie";
inline constexpr const char* output = "Output";
inline constexpr const char* delta = "Delta";
inline constexpr const char* bypass = "Bypass";

inline constexpr const char* layer = "Warstwa";
inline constexpr const char* layerOn = "włączona";
inline constexpr const char* layerSource = "Źródło";
inline constexpr const char* layerVoices = "Głosy";
inline constexpr const char* layerSemis = "Interwał";
inline constexpr const char* layerFormant = "Barwa głosu";
inline constexpr const char* layerLevel = "Poziom";
inline constexpr const char* layerWidth = "Szerokość warstwy";
inline constexpr const char* layerTime = "Czas warstwy";
inline constexpr const char* layerDetune = "Rozstrojenie";
inline constexpr const char* layerLow = "Filtr górny";
inline constexpr const char* layerHigh = "Filtr dolny";
inline constexpr const char* layerNote = "Nuta";

inline constexpr const char* switchOn = "wł";
inline constexpr const char* switchOff = "wył";

inline constexpr const char* lamp = "Lampa";
inline constexpr const char* tape = "Taśma";
inline constexpr const char* transformer = "Transformator";
inline constexpr const char* transistor = "Tranzystor";

inline constexpr const char* motionNone = "Brak";
inline constexpr const char* motionAutopan = "Autopan";
inline constexpr const char* motionTremolo = "Tremolo";
inline constexpr const char* motionFlanger = "Flanger";

inline constexpr const char* echoSixteenth = "1/16";
inline constexpr const char* echoEighth = "1/8";
inline constexpr const char* echoDottedEighth = "1/8 z kropką";
inline constexpr const char* echoQuarter = "1/4";
inline constexpr const char* echoDottedQuarter = "1/4 z kropką";
inline constexpr const char* echoHalf = "1/2";
inline constexpr const char* echoSlap = "Slap 80 ms";

inline constexpr const char* sourceVoice = "Głos";
inline constexpr const char* sourceWhisper = "Szept";
inline constexpr const char* sourceSub = "Sub";
inline constexpr const char* sourceRobot = "Robot";

inline constexpr const char* favorites = "Ulubione";
inline constexpr const char* mine = "Moje";
inline constexpr const char* stage = "Scena";
inline constexpr const char* left = "L";
inline constexpr const char* right = "P";
inline constexpr const char* randomise = "Losuj";
inline constexpr const char* details = "Szczegóły";
inline constexpr const char* consonantsShort = "Spółgłoski";
inline constexpr const char* grainCharShort = "Charakter";
inline constexpr const char* layerWidthShort = "Szerokość";
inline constexpr const char* layerTimeShort = "Czas";

inline constexpr const char* emptyFavorites = "Tu trafiają presety z gwiazdką. Kliknij gwiazdkę na karcie, żeby dodać pierwszy.";
inline constexpr const char* emptyMine = "Tu trafiają twoje presety. Ustaw brzmienie i kliknij Zapisz na górze.";

inline constexpr const char* amountTip = "Ile dodanego dźwięku słychać. Przy zerze zostaje sam głos.";
inline constexpr const char* deltaTip = "Słychać tylko to, co dodaje wtyczka.";
inline constexpr const char* bypassTip = "Omija wtyczkę. Opóźnienie zostaje takie samo.";
inline constexpr const char* randomiseTip = "Rusza szerokość, czas, rozstrojenie, ton, pogłos i echo obecnego brzmienia. Interwały i barwy zostają.";
inline constexpr const char* detailsTip = "Pokazuje wszystkie warstwy i efekty zamiast presetów.";
inline constexpr const char* widthTip = "Mnoży szerokość wszystkich warstw.";
inline constexpr const char* timeTip = "Mnoży rozsunięcie głosów w czasie.";
inline constexpr const char* tiltTip = "Przechyla barwę warstw w stronę dołu albo góry.";
inline constexpr const char* grainTip = "Nasycenie warstw. Głośność zostaje ta sama.";
inline constexpr const char* motionTip = "Głębokość ruchu wybranego w szczegółach.";
inline constexpr const char* verbTip = "Ile pogłosu dochodzi do warstw.";
inline constexpr const char* echoTip = "Ile echa dochodzi do warstw. Czas idzie za tempem projektu.";
inline constexpr const char* consonantsTip = "Chowa warstwy na syczących i spółgłoskach, żeby słowa zostały czytelne.";
inline constexpr const char* outputTip = "Głośność na wyjściu wtyczki.";

} // namespace refren::text
