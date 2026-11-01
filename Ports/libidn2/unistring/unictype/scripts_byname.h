/* ANSI-C code produced by gperf version 3.1 */
/* Command-line: gperf -m 10 unictype/scripts_byname.gperf  */
/* Computed positions: -k'1,3,5,8' */

#if !((' ' == 32) && ('!' == 33) && ('"' == 34) && ('#' == 35) \
      && ('%' == 37) && ('&' == 38) && ('\'' == 39) && ('(' == 40) \
      && (')' == 41) && ('*' == 42) && ('+' == 43) && (',' == 44) \
      && ('-' == 45) && ('.' == 46) && ('/' == 47) && ('0' == 48) \
      && ('1' == 49) && ('2' == 50) && ('3' == 51) && ('4' == 52) \
      && ('5' == 53) && ('6' == 54) && ('7' == 55) && ('8' == 56) \
      && ('9' == 57) && (':' == 58) && (';' == 59) && ('<' == 60) \
      && ('=' == 61) && ('>' == 62) && ('?' == 63) && ('A' == 65) \
      && ('B' == 66) && ('C' == 67) && ('D' == 68) && ('E' == 69) \
      && ('F' == 70) && ('G' == 71) && ('H' == 72) && ('I' == 73) \
      && ('J' == 74) && ('K' == 75) && ('L' == 76) && ('M' == 77) \
      && ('N' == 78) && ('O' == 79) && ('P' == 80) && ('Q' == 81) \
      && ('R' == 82) && ('S' == 83) && ('T' == 84) && ('U' == 85) \
      && ('V' == 86) && ('W' == 87) && ('X' == 88) && ('Y' == 89) \
      && ('Z' == 90) && ('[' == 91) && ('\\' == 92) && (']' == 93) \
      && ('^' == 94) && ('_' == 95) && ('a' == 97) && ('b' == 98) \
      && ('c' == 99) && ('d' == 100) && ('e' == 101) && ('f' == 102) \
      && ('g' == 103) && ('h' == 104) && ('i' == 105) && ('j' == 106) \
      && ('k' == 107) && ('l' == 108) && ('m' == 109) && ('n' == 110) \
      && ('o' == 111) && ('p' == 112) && ('q' == 113) && ('r' == 114) \
      && ('s' == 115) && ('t' == 116) && ('u' == 117) && ('v' == 118) \
      && ('w' == 119) && ('x' == 120) && ('y' == 121) && ('z' == 122) \
      && ('{' == 123) && ('|' == 124) && ('}' == 125) && ('~' == 126))
/* The character set is not based on ISO-646.  */
#error "gperf generated tables don't work with this execution character set. Please report a bug to <bug-gperf@gnu.org>."
#endif

#line 20 "unictype/scripts_byname.gperf"
struct named_script { int name; unsigned int index; };

#define TOTAL_KEYWORDS 170
#define MIN_WORD_LENGTH 2
#define MAX_WORD_LENGTH 22
#define MIN_HASH_VALUE 6
#define MAX_HASH_VALUE 291
/* maximum key range = 286, duplicates = 0 */

#ifdef __GNUC__
__inline
#else
#ifdef __cplusplus
inline
#endif
#endif
static unsigned int
scripts_hash (register const char *str, register size_t len)
{
  static const unsigned short asso_values[] =
    {
      292, 292, 292, 292, 292, 292, 292, 292, 292, 292,
      292, 292, 292, 292, 292, 292, 292, 292, 292, 292,
      292, 292, 292, 292, 292, 292, 292, 292, 292, 292,
      292, 292, 292, 292, 292, 292, 292, 292, 292, 292,
      292, 292, 292, 292, 292, 292, 292, 292, 292, 292,
      292, 292, 292, 292, 292, 292, 292, 292, 292, 292,
      292, 292, 292, 292, 292,  58,   5,   8,  60,  54,
      292,  45,  91,  55,   5,  15,  55,  30,  73,  69,
       16, 292,  50,   7,   4,  15,  86,  86, 292,   4,
       10, 292, 292, 292, 292,  60, 292,   7,  30,  91,
       54,  14,  31,  38,  74,  10,  47,  20,  28,   6,
        4,  16, 102, 292,   9,  76,   4,  73,  15,  98,
      292, 105,   6, 292, 292, 292, 292, 292, 292, 292,
      292, 292, 292, 292, 292, 292, 292, 292, 292, 292,
      292, 292, 292, 292, 292, 292, 292, 292, 292, 292,
      292, 292, 292, 292, 292, 292, 292, 292, 292, 292,
      292, 292, 292, 292, 292, 292, 292, 292, 292, 292,
      292, 292, 292, 292, 292, 292, 292, 292, 292, 292,
      292, 292, 292, 292, 292, 292, 292, 292, 292, 292,
      292, 292, 292, 292, 292, 292, 292, 292, 292, 292,
      292, 292, 292, 292, 292, 292, 292, 292, 292, 292,
      292, 292, 292, 292, 292, 292, 292, 292, 292, 292,
      292, 292, 292, 292, 292, 292, 292, 292, 292, 292,
      292, 292, 292, 292, 292, 292, 292, 292, 292, 292,
      292, 292, 292, 292, 292, 292, 292, 292, 292, 292,
      292, 292, 292, 292, 292, 292
    };
  register unsigned int hval = len;

  switch (hval)
    {
      default:
        hval += asso_values[(unsigned char)str[7]];
      /*FALLTHROUGH*/
      case 7:
      case 6:
      case 5:
        hval += asso_values[(unsigned char)str[4]];
      /*FALLTHROUGH*/
      case 4:
      case 3:
        hval += asso_values[(unsigned char)str[2]];
      /*FALLTHROUGH*/
      case 2:
      case 1:
        hval += asso_values[(unsigned char)str[0]];
        break;
    }
  return hval;
}

struct script_stringpool_t
  {
    char script_stringpool_str6[sizeof("Yi")];
    char script_stringpool_str12[sizeof("Toto")];
    char script_stringpool_str15[sizeof("Thai")];
    char script_stringpool_str19[sizeof("Cham")];
    char script_stringpool_str21[sizeof("Thaana")];
    char script_stringpool_str22[sizeof("Bamum")];
    char script_stringpool_str23[sizeof("Bengali")];
    char script_stringpool_str24[sizeof("Brahmi")];
    char script_stringpool_str25[sizeof("Sinhala")];
    char script_stringpool_str27[sizeof("Chakma")];
    char script_stringpool_str28[sizeof("Sharada")];
    char script_stringpool_str29[sizeof("Syriac")];
    char script_stringpool_str30[sizeof("Carian")];
    char script_stringpool_str31[sizeof("Shavian")];
    char script_stringpool_str32[sizeof("Tai_Tham")];
    char script_stringpool_str33[sizeof("Kannada")];
    char script_stringpool_str34[sizeof("Batak")];
    char script_stringpool_str35[sizeof("Khmer")];
    char script_stringpool_str36[sizeof("Common")];
    char script_stringpool_str38[sizeof("Samaritan")];
    char script_stringpool_str39[sizeof("Takri")];
    char script_stringpool_str40[sizeof("Cuneiform")];
    char script_stringpool_str41[sizeof("Miao")];
    char script_stringpool_str43[sizeof("Tamil")];
    char script_stringpool_str44[sizeof("Kirat_Rai")];
    char script_stringpool_str45[sizeof("Tibetan")];
    char script_stringpool_str46[sizeof("Javanese")];
    char script_stringpool_str47[sizeof("Braille")];
    char script_stringpool_str48[sizeof("Mandaic")];
    char script_stringpool_str49[sizeof("Mro")];
    char script_stringpool_str50[sizeof("Myanmar")];
    char script_stringpool_str51[sizeof("Chorasmian")];
    char script_stringpool_str52[sizeof("Kharoshthi")];
    char script_stringpool_str54[sizeof("Katakana")];
    char script_stringpool_str56[sizeof("Phoenician")];
    char script_stringpool_str57[sizeof("Khojki")];
    char script_stringpool_str58[sizeof("Khitan_Small_Script")];
    char script_stringpool_str59[sizeof("Balinese")];
    char script_stringpool_str60[sizeof("Cherokee")];
    char script_stringpool_str61[sizeof("Bhaiksuki")];
    char script_stringpool_str62[sizeof("Sogdian")];
    char script_stringpool_str63[sizeof("Grantha")];
    char script_stringpool_str64[sizeof("Tagbanwa")];
    char script_stringpool_str65[sizeof("Gothic")];
    char script_stringpool_str66[sizeof("Mongolian")];
    char script_stringpool_str67[sizeof("Zanabazar_Square")];
    char script_stringpool_str68[sizeof("Latin")];
    char script_stringpool_str69[sizeof("Buginese")];
    char script_stringpool_str70[sizeof("Yezidi")];
    char script_stringpool_str71[sizeof("Mende_Kikakui")];
    char script_stringpool_str72[sizeof("Multani")];
    char script_stringpool_str73[sizeof("Todhri")];
    char script_stringpool_str74[sizeof("Lao")];
    char script_stringpool_str75[sizeof("Tai_Le")];
    char script_stringpool_str76[sizeof("Telugu")];
    char script_stringpool_str77[sizeof("Tagalog")];
    char script_stringpool_str78[sizeof("Ahom")];
    char script_stringpool_str79[sizeof("Linear_B")];
    char script_stringpool_str80[sizeof("Armenian")];
    char script_stringpool_str81[sizeof("Arabic")];
    char script_stringpool_str82[sizeof("Glagolitic")];
    char script_stringpool_str83[sizeof("Avestan")];
    char script_stringpool_str84[sizeof("Greek")];
    char script_stringpool_str85[sizeof("Gurung_Khema")];
    char script_stringpool_str86[sizeof("Osmanya")];
    char script_stringpool_str87[sizeof("Tangut")];
    char script_stringpool_str88[sizeof("Modi")];
    char script_stringpool_str89[sizeof("Canadian_Aboriginal")];
    char script_stringpool_str90[sizeof("Tangsa")];
    char script_stringpool_str91[sizeof("Oriya")];
    char script_stringpool_str92[sizeof("Nko")];
    char script_stringpool_str93[sizeof("Tirhuta")];
    char script_stringpool_str94[sizeof("Sora_Sompeng")];
    char script_stringpool_str95[sizeof("Osage")];
    char script_stringpool_str96[sizeof("Devanagari")];
    char script_stringpool_str97[sizeof("Adlam")];
    char script_stringpool_str98[sizeof("Han")];
    char script_stringpool_str99[sizeof("Vai")];
    char script_stringpool_str100[sizeof("Meetei_Mayek")];
    char script_stringpool_str101[sizeof("Saurashtra")];
    char script_stringpool_str102[sizeof("Psalter_Pahlavi")];
    char script_stringpool_str103[sizeof("Sundanese")];
    char script_stringpool_str104[sizeof("Bassa_Vah")];
    char script_stringpool_str105[sizeof("Kaithi")];
    char script_stringpool_str106[sizeof("Hanunoo")];
    char script_stringpool_str107[sizeof("Rejang")];
    char script_stringpool_str108[sizeof("Hatran")];
    char script_stringpool_str109[sizeof("Anatolian_Hieroglyphs")];
    char script_stringpool_str110[sizeof("Dogra")];
    char script_stringpool_str111[sizeof("Georgian")];
    char script_stringpool_str112[sizeof("Tai_Viet")];
    char script_stringpool_str113[sizeof("Caucasian_Albanian")];
    char script_stringpool_str114[sizeof("Phags_Pa")];
    char script_stringpool_str116[sizeof("Sunuwar")];
    char script_stringpool_str117[sizeof("Kawi")];
    char script_stringpool_str118[sizeof("Warang_Citi")];
    char script_stringpool_str119[sizeof("Gujarati")];
    char script_stringpool_str120[sizeof("Marchen")];
    char script_stringpool_str121[sizeof("Tifinagh")];
    char script_stringpool_str122[sizeof("Lydian")];
    char script_stringpool_str123[sizeof("Nabataean")];
    char script_stringpool_str124[sizeof("Syloti_Nagri")];
    char script_stringpool_str125[sizeof("Soyombo")];
    char script_stringpool_str126[sizeof("Coptic")];
    char script_stringpool_str127[sizeof("Cypriot")];
    char script_stringpool_str128[sizeof("Vithkuqi")];
    char script_stringpool_str129[sizeof("Gunjala_Gondi")];
    char script_stringpool_str131[sizeof("Ugaritic")];
    char script_stringpool_str132[sizeof("Linear_A")];
    char script_stringpool_str133[sizeof("Makasar")];
    char script_stringpool_str135[sizeof("Lisu")];
    char script_stringpool_str136[sizeof("Nandinagari")];
    char script_stringpool_str137[sizeof("Bopomofo")];
    char script_stringpool_str138[sizeof("Buhid")];
    char script_stringpool_str139[sizeof("Limbu")];
    char script_stringpool_str140[sizeof("Ol_Onal")];
    char script_stringpool_str141[sizeof("Hebrew")];
    char script_stringpool_str142[sizeof("Siddham")];
    char script_stringpool_str143[sizeof("Tulu_Tigalari")];
    char script_stringpool_str144[sizeof("Cyrillic")];
    char script_stringpool_str145[sizeof("Gurmukhi")];
    char script_stringpool_str146[sizeof("SignWriting")];
    char script_stringpool_str148[sizeof("Cypro_Minoan")];
    char script_stringpool_str149[sizeof("Manichaean")];
    char script_stringpool_str150[sizeof("Runic")];
    char script_stringpool_str151[sizeof("Old_South_Arabian")];
    char script_stringpool_str152[sizeof("Deseret")];
    char script_stringpool_str153[sizeof("Hiragana")];
    char script_stringpool_str154[sizeof("Ogham")];
    char script_stringpool_str155[sizeof("Old_Permic")];
    char script_stringpool_str156[sizeof("Meroitic_Cursive")];
    char script_stringpool_str157[sizeof("Old_Turkic")];
    char script_stringpool_str158[sizeof("Khudawadi")];
    char script_stringpool_str159[sizeof("Lycian")];
    char script_stringpool_str160[sizeof("Meroitic_Hieroglyphs")];
    char script_stringpool_str161[sizeof("Inherited")];
    char script_stringpool_str162[sizeof("Palmyrene")];
    char script_stringpool_str163[sizeof("Nyiakeng_Puachue_Hmong")];
    char script_stringpool_str164[sizeof("Garay")];
    char script_stringpool_str165[sizeof("Inscriptional_Pahlavi")];
    char script_stringpool_str166[sizeof("Inscriptional_Parthian")];
    char script_stringpool_str167[sizeof("Elbasan")];
    char script_stringpool_str168[sizeof("Pau_Cin_Hau")];
    char script_stringpool_str169[sizeof("Mahajani")];
    char script_stringpool_str170[sizeof("Wancho")];
    char script_stringpool_str173[sizeof("Elymaic")];
    char script_stringpool_str174[sizeof("Hangul")];
    char script_stringpool_str175[sizeof("Newa")];
    char script_stringpool_str179[sizeof("Malayalam")];
    char script_stringpool_str180[sizeof("Medefaidrin")];
    char script_stringpool_str182[sizeof("Dives_Akuru")];
    char script_stringpool_str187[sizeof("Egyptian_Hieroglyphs")];
    char script_stringpool_str188[sizeof("Masaram_Gondi")];
    char script_stringpool_str190[sizeof("Duployan")];
    char script_stringpool_str191[sizeof("Hanifi_Rohingya")];
    char script_stringpool_str195[sizeof("Old_Sogdian")];
    char script_stringpool_str206[sizeof("Nag_Mundari")];
    char script_stringpool_str210[sizeof("Imperial_Aramaic")];
    char script_stringpool_str212[sizeof("Kayah_Li")];
    char script_stringpool_str216[sizeof("Old_Italic")];
    char script_stringpool_str217[sizeof("Old_North_Arabian")];
    char script_stringpool_str221[sizeof("Ol_Chiki")];
    char script_stringpool_str222[sizeof("Old_Uyghur")];
    char script_stringpool_str226[sizeof("Old_Persian")];
    char script_stringpool_str227[sizeof("Nushu")];
    char script_stringpool_str237[sizeof("Lepcha")];
    char script_stringpool_str243[sizeof("Ethiopic")];
    char script_stringpool_str246[sizeof("New_Tai_Lue")];
    char script_stringpool_str265[sizeof("Old_Hungarian")];
    char script_stringpool_str291[sizeof("Pahawh_Hmong")];
  };
static const struct script_stringpool_t script_stringpool_contents =
  {
    "Yi",
    "Toto",
    "Thai",
    "Cham",
    "Thaana",
    "Bamum",
    "Bengali",
    "Brahmi",
    "Sinhala",
    "Chakma",
    "Sharada",
    "Syriac",
    "Carian",
    "Shavian",
    "Tai_Tham",
    "Kannada",
    "Batak",
    "Khmer",
    "Common",
    "Samaritan",
    "Takri",
    "Cuneiform",
    "Miao",
    "Tamil",
    "Kirat_Rai",
    "Tibetan",
    "Javanese",
    "Braille",
    "Mandaic",
    "Mro",
    "Myanmar",
    "Chorasmian",
    "Kharoshthi",
    "Katakana",
    "Phoenician",
    "Khojki",
    "Khitan_Small_Script",
    "Balinese",
    "Cherokee",
    "Bhaiksuki",
    "Sogdian",
    "Grantha",
    "Tagbanwa",
    "Gothic",
    "Mongolian",
    "Zanabazar_Square",
    "Latin",
    "Buginese",
    "Yezidi",
    "Mende_Kikakui",
    "Multani",
    "Todhri",
    "Lao",
    "Tai_Le",
    "Telugu",
    "Tagalog",
    "Ahom",
    "Linear_B",
    "Armenian",
    "Arabic",
    "Glagolitic",
    "Avestan",
    "Greek",
    "Gurung_Khema",
    "Osmanya",
    "Tangut",
    "Modi",
    "Canadian_Aboriginal",
    "Tangsa",
    "Oriya",
    "Nko",
    "Tirhuta",
    "Sora_Sompeng",
    "Osage",
    "Devanagari",
    "Adlam",
    "Han",
    "Vai",
    "Meetei_Mayek",
    "Saurashtra",
    "Psalter_Pahlavi",
    "Sundanese",
    "Bassa_Vah",
    "Kaithi",
    "Hanunoo",
    "Rejang",
    "Hatran",
    "Anatolian_Hieroglyphs",
    "Dogra",
    "Georgian",
    "Tai_Viet",
    "Caucasian_Albanian",
    "Phags_Pa",
    "Sunuwar",
    "Kawi",
    "Warang_Citi",
    "Gujarati",
    "Marchen",
    "Tifinagh",
    "Lydian",
    "Nabataean",
    "Syloti_Nagri",
    "Soyombo",
    "Coptic",
    "Cypriot",
    "Vithkuqi",
    "Gunjala_Gondi",
    "Ugaritic",
    "Linear_A",
    "Makasar",
    "Lisu",
    "Nandinagari",
    "Bopomofo",
    "Buhid",
    "Limbu",
    "Ol_Onal",
    "Hebrew",
    "Siddham",
    "Tulu_Tigalari",
    "Cyrillic",
    "Gurmukhi",
    "SignWriting",
    "Cypro_Minoan",
    "Manichaean",
    "Runic",
    "Old_South_Arabian",
    "Deseret",
    "Hiragana",
    "Ogham",
    "Old_Permic",
    "Meroitic_Cursive",
    "Old_Turkic",
    "Khudawadi",
    "Lycian",
    "Meroitic_Hieroglyphs",
    "Inherited",
    "Palmyrene",
    "Nyiakeng_Puachue_Hmong",
    "Garay",
    "Inscriptional_Pahlavi",
    "Inscriptional_Parthian",
    "Elbasan",
    "Pau_Cin_Hau",
    "Mahajani",
    "Wancho",
    "Elymaic",
    "Hangul",
    "Newa",
    "Malayalam",
    "Medefaidrin",
    "Dives_Akuru",
    "Egyptian_Hieroglyphs",
    "Masaram_Gondi",
    "Duployan",
    "Hanifi_Rohingya",
    "Old_Sogdian",
    "Nag_Mundari",
    "Imperial_Aramaic",
    "Kayah_Li",
    "Old_Italic",
    "Old_North_Arabian",
    "Ol_Chiki",
    "Old_Uyghur",
    "Old_Persian",
    "Nushu",
    "Lepcha",
    "Ethiopic",
    "New_Tai_Lue",
    "Old_Hungarian",
    "Pahawh_Hmong"
  };
#define script_stringpool ((const char *) &script_stringpool_contents)

static const struct named_script script_names[] =
  {
    {-1}, {-1}, {-1}, {-1}, {-1}, {-1},
#line 67 "unictype/scripts_byname.gperf"
    {(int)(size_t)&((struct script_stringpool_t *)0)->script_stringpool_str6, 36},
    {-1}, {-1}, {-1}, {-1}, {-1},
#line 190 "unictype/scripts_byname.gperf"
    {(int)(size_t)&((struct script_stringpool_t *)0)->script_stringpool_str12, 159},
    {-1}, {-1},
#line 50 "unictype/scripts_byname.gperf"
    {(int)(size_t)&((struct script_stringpool_t *)0)->script_stringpool_str15, 19},
    {-1}, {-1}, {-1},
#line 107 "unictype/scripts_byname.gperf"
    {(int)(size_t)&((struct script_stringpool_t *)0)->script_stringpool_str19, 76},
    {-1},
#line 39 "unictype/scripts_byname.gperf"
    {(int)(size_t)&((struct script_stringpool_t *)0)->script_stringpool_str21, 8},
#line 114 "unictype/scripts_byname.gperf"
    {(int)(size_t)&((struct script_stringpool_t *)0)->script_stringpool_str22, 83},
#line 41 "unictype/scripts_byname.gperf"
    {(int)(size_t)&((struct script_stringpool_t *)0)->script_stringpool_str23, 10},
#line 124 "unictype/scripts_byname.gperf"
    {(int)(size_t)&((struct script_stringpool_t *)0)->script_stringpool_str24, 93},
#line 49 "unictype/scripts_byname.gperf"
    {(int)(size_t)&((struct script_stringpool_t *)0)->script_stringpool_str25, 18},
    {-1},
#line 126 "unictype/scripts_byname.gperf"
    {(int)(size_t)&((struct script_stringpool_t *)0)->script_stringpool_str27, 95},
#line 130 "unictype/scripts_byname.gperf"
    {(int)(size_t)&((struct script_stringpool_t *)0)->script_stringpool_str28, 99},
#line 38 "unictype/scripts_byname.gperf"
    {(int)(size_t)&((struct script_stringpool_t *)0)->script_stringpool_str29, 7},
#line 105 "unictype/scripts_byname.gperf"
    {(int)(size_t)&((struct script_stringpool_t *)0)->script_stringpool_str30, 74},
#line 80 "unictype/scripts_byname.gperf"
    {(int)(size_t)&((struct script_stringpool_t *)0)->script_stringpool_str31, 49},
#line 108 "unictype/scripts_byname.gperf"
    {(int)(size_t)&((struct script_stringpool_t *)0)->script_stringpool_str32, 77},
#line 47 "unictype/scripts_byname.gperf"
    {(int)(size_t)&((struct script_stringpool_t *)0)->script_stringpool_str33, 16},
#line 123 "unictype/scripts_byname.gperf"
    {(int)(size_t)&((struct script_stringpool_t *)0)->script_stringpool_str34, 92},
#line 61 "unictype/scripts_byname.gperf"
    {(int)(size_t)&((struct script_stringpool_t *)0)->script_stringpool_str35, 30},
#line 31 "unictype/scripts_byname.gperf"
    {(int)(size_t)&((struct script_stringpool_t *)0)->script_stringpool_str36, 0},
    {-1},
#line 112 "unictype/scripts_byname.gperf"
    {(int)(size_t)&((struct script_stringpool_t *)0)->script_stringpool_str38, 81},
#line 132 "unictype/scripts_byname.gperf"
    {(int)(size_t)&((struct script_stringpool_t *)0)->script_stringpool_str39, 101},
#line 93 "unictype/scripts_byname.gperf"
    {(int)(size_t)&((struct script_stringpool_t *)0)->script_stringpool_str40, 62},
#line 129 "unictype/scripts_byname.gperf"
    {(int)(size_t)&((struct script_stringpool_t *)0)->script_stringpool_str41, 98},
    {-1},
#line 45 "unictype/scripts_byname.gperf"
    {(int)(size_t)&((struct script_stringpool_t *)0)->script_stringpool_str43, 14},
#line 196 "unictype/scripts_byname.gperf"
    {(int)(size_t)&((struct script_stringpool_t *)0)->script_stringpool_str44, 165},
#line 52 "unictype/scripts_byname.gperf"
    {(int)(size_t)&((struct script_stringpool_t *)0)->script_stringpool_str45, 21},
#line 115 "unictype/scripts_byname.gperf"
    {(int)(size_t)&((struct script_stringpool_t *)0)->script_stringpool_str46, 84},
#line 83 "unictype/scripts_byname.gperf"
    {(int)(size_t)&((struct script_stringpool_t *)0)->script_stringpool_str47, 52},
#line 125 "unictype/scripts_byname.gperf"
    {(int)(size_t)&((struct script_stringpool_t *)0)->script_stringpool_str48, 94},
#line 145 "unictype/scripts_byname.gperf"
    {(int)(size_t)&((struct script_stringpool_t *)0)->script_stringpool_str49, 114},
#line 53 "unictype/scripts_byname.gperf"
    {(int)(size_t)&((struct script_stringpool_t *)0)->script_stringpool_str50, 22},
#line 183 "unictype/scripts_byname.gperf"
    {(int)(size_t)&((struct script_stringpool_t *)0)->script_stringpool_str51, 152},
#line 91 "unictype/scripts_byname.gperf"
    {(int)(size_t)&((struct script_stringpool_t *)0)->script_stringpool_str52, 60},
    {-1},
#line 64 "unictype/scripts_byname.gperf"
    {(int)(size_t)&((struct script_stringpool_t *)0)->script_stringpool_str54, 33},
    {-1},
#line 94 "unictype/scripts_byname.gperf"
    {(int)(size_t)&((struct script_stringpool_t *)0)->script_stringpool_str56, 63},
#line 139 "unictype/scripts_byname.gperf"
    {(int)(size_t)&((struct script_stringpool_t *)0)->script_stringpool_str57, 108},
#line 185 "unictype/scripts_byname.gperf"
    {(int)(size_t)&((struct script_stringpool_t *)0)->script_stringpool_str58, 154},
#line 92 "unictype/scripts_byname.gperf"
    {(int)(size_t)&((struct script_stringpool_t *)0)->script_stringpool_str59, 61},
#line 57 "unictype/scripts_byname.gperf"
    {(int)(size_t)&((struct script_stringpool_t *)0)->script_stringpool_str60, 26},
#line 163 "unictype/scripts_byname.gperf"
    {(int)(size_t)&((struct script_stringpool_t *)0)->script_stringpool_str61, 132},
#line 177 "unictype/scripts_byname.gperf"
    {(int)(size_t)&((struct script_stringpool_t *)0)->script_stringpool_str62, 146},
#line 137 "unictype/scripts_byname.gperf"
    {(int)(size_t)&((struct script_stringpool_t *)0)->script_stringpool_str63, 106},
#line 75 "unictype/scripts_byname.gperf"
    {(int)(size_t)&((struct script_stringpool_t *)0)->script_stringpool_str64, 44},
#line 69 "unictype/scripts_byname.gperf"
    {(int)(size_t)&((struct script_stringpool_t *)0)->script_stringpool_str65, 38},
#line 62 "unictype/scripts_byname.gperf"
    {(int)(size_t)&((struct script_stringpool_t *)0)->script_stringpool_str66, 31},
#line 171 "unictype/scripts_byname.gperf"
    {(int)(size_t)&((struct script_stringpool_t *)0)->script_stringpool_str67, 140},
#line 32 "unictype/scripts_byname.gperf"
    {(int)(size_t)&((struct script_stringpool_t *)0)->script_stringpool_str68, 1},
#line 84 "unictype/scripts_byname.gperf"
    {(int)(size_t)&((struct script_stringpool_t *)0)->script_stringpool_str69, 53},
#line 186 "unictype/scripts_byname.gperf"
    {(int)(size_t)&((struct script_stringpool_t *)0)->script_stringpool_str70, 155},
#line 143 "unictype/scripts_byname.gperf"
    {(int)(size_t)&((struct script_stringpool_t *)0)->script_stringpool_str71, 112},
#line 159 "unictype/scripts_byname.gperf"
    {(int)(size_t)&((struct script_stringpool_t *)0)->script_stringpool_str72, 128},
#line 199 "unictype/scripts_byname.gperf"
    {(int)(size_t)&((struct script_stringpool_t *)0)->script_stringpool_str73, 168},
#line 51 "unictype/scripts_byname.gperf"
    {(int)(size_t)&((struct script_stringpool_t *)0)->script_stringpool_str74, 20},
#line 77 "unictype/scripts_byname.gperf"
    {(int)(size_t)&((struct script_stringpool_t *)0)->script_stringpool_str75, 46},
#line 46 "unictype/scripts_byname.gperf"
    {(int)(size_t)&((struct script_stringpool_t *)0)->script_stringpool_str76, 15},
#line 72 "unictype/scripts_byname.gperf"
    {(int)(size_t)&((struct script_stringpool_t *)0)->script_stringpool_str77, 41},
#line 156 "unictype/scripts_byname.gperf"
    {(int)(size_t)&((struct script_stringpool_t *)0)->script_stringpool_str78, 125},
#line 78 "unictype/scripts_byname.gperf"
    {(int)(size_t)&((struct script_stringpool_t *)0)->script_stringpool_str79, 47},
#line 35 "unictype/scripts_byname.gperf"
    {(int)(size_t)&((struct script_stringpool_t *)0)->script_stringpool_str80, 4},
#line 37 "unictype/scripts_byname.gperf"
    {(int)(size_t)&((struct script_stringpool_t *)0)->script_stringpool_str81, 6},
#line 87 "unictype/scripts_byname.gperf"
    {(int)(size_t)&((struct script_stringpool_t *)0)->script_stringpool_str82, 56},
#line 110 "unictype/scripts_byname.gperf"
    {(int)(size_t)&((struct script_stringpool_t *)0)->script_stringpool_str83, 79},
#line 33 "unictype/scripts_byname.gperf"
    {(int)(size_t)&((struct script_stringpool_t *)0)->script_stringpool_str84, 2},
#line 195 "unictype/scripts_byname.gperf"
    {(int)(size_t)&((struct script_stringpool_t *)0)->script_stringpool_str85, 164},
#line 81 "unictype/scripts_byname.gperf"
    {(int)(size_t)&((struct script_stringpool_t *)0)->script_stringpool_str86, 50},
#line 167 "unictype/scripts_byname.gperf"
    {(int)(size_t)&((struct script_stringpool_t *)0)->script_stringpool_str87, 136},
#line 144 "unictype/scripts_byname.gperf"
    {(int)(size_t)&((struct script_stringpool_t *)0)->script_stringpool_str88, 113},
#line 58 "unictype/scripts_byname.gperf"
    {(int)(size_t)&((struct script_stringpool_t *)0)->script_stringpool_str89, 27},
#line 189 "unictype/scripts_byname.gperf"
    {(int)(size_t)&((struct script_stringpool_t *)0)->script_stringpool_str90, 158},
#line 44 "unictype/scripts_byname.gperf"
    {(int)(size_t)&((struct script_stringpool_t *)0)->script_stringpool_str91, 13},
#line 96 "unictype/scripts_byname.gperf"
    {(int)(size_t)&((struct script_stringpool_t *)0)->script_stringpool_str92, 65},
#line 154 "unictype/scripts_byname.gperf"
    {(int)(size_t)&((struct script_stringpool_t *)0)->script_stringpool_str93, 123},
#line 131 "unictype/scripts_byname.gperf"
    {(int)(size_t)&((struct script_stringpool_t *)0)->script_stringpool_str94, 100},
#line 166 "unictype/scripts_byname.gperf"
    {(int)(size_t)&((struct script_stringpool_t *)0)->script_stringpool_str95, 135},
#line 40 "unictype/scripts_byname.gperf"
    {(int)(size_t)&((struct script_stringpool_t *)0)->script_stringpool_str96, 9},
#line 162 "unictype/scripts_byname.gperf"
    {(int)(size_t)&((struct script_stringpool_t *)0)->script_stringpool_str97, 131},
#line 66 "unictype/scripts_byname.gperf"
    {(int)(size_t)&((struct script_stringpool_t *)0)->script_stringpool_str98, 35},
#line 100 "unictype/scripts_byname.gperf"
    {(int)(size_t)&((struct script_stringpool_t *)0)->script_stringpool_str99, 69},
#line 116 "unictype/scripts_byname.gperf"
    {(int)(size_t)&((struct script_stringpool_t *)0)->script_stringpool_str100, 85},
#line 101 "unictype/scripts_byname.gperf"
    {(int)(size_t)&((struct script_stringpool_t *)0)->script_stringpool_str101, 70},
#line 151 "unictype/scripts_byname.gperf"
    {(int)(size_t)&((struct script_stringpool_t *)0)->script_stringpool_str102, 120},
#line 97 "unictype/scripts_byname.gperf"
    {(int)(size_t)&((struct script_stringpool_t *)0)->script_stringpool_str103, 66},
#line 134 "unictype/scripts_byname.gperf"
    {(int)(size_t)&((struct script_stringpool_t *)0)->script_stringpool_str104, 103},
#line 122 "unictype/scripts_byname.gperf"
    {(int)(size_t)&((struct script_stringpool_t *)0)->script_stringpool_str105, 91},
#line 73 "unictype/scripts_byname.gperf"
    {(int)(size_t)&((struct script_stringpool_t *)0)->script_stringpool_str106, 42},
#line 103 "unictype/scripts_byname.gperf"
    {(int)(size_t)&((struct script_stringpool_t *)0)->script_stringpool_str107, 72},
#line 158 "unictype/scripts_byname.gperf"
    {(int)(size_t)&((struct script_stringpool_t *)0)->script_stringpool_str108, 127},
#line 157 "unictype/scripts_byname.gperf"
    {(int)(size_t)&((struct script_stringpool_t *)0)->script_stringpool_str109, 126},
#line 172 "unictype/scripts_byname.gperf"
    {(int)(size_t)&((struct script_stringpool_t *)0)->script_stringpool_str110, 141},
#line 54 "unictype/scripts_byname.gperf"
    {(int)(size_t)&((struct script_stringpool_t *)0)->script_stringpool_str111, 23},
#line 109 "unictype/scripts_byname.gperf"
    {(int)(size_t)&((struct script_stringpool_t *)0)->script_stringpool_str112, 78},
#line 133 "unictype/scripts_byname.gperf"
    {(int)(size_t)&((struct script_stringpool_t *)0)->script_stringpool_str113, 102},
#line 95 "unictype/scripts_byname.gperf"
    {(int)(size_t)&((struct script_stringpool_t *)0)->script_stringpool_str114, 64},
    {-1},
#line 198 "unictype/scripts_byname.gperf"
    {(int)(size_t)&((struct script_stringpool_t *)0)->script_stringpool_str116, 167},
#line 192 "unictype/scripts_byname.gperf"
    {(int)(size_t)&((struct script_stringpool_t *)0)->script_stringpool_str117, 161},
#line 155 "unictype/scripts_byname.gperf"
    {(int)(size_t)&((struct script_stringpool_t *)0)->script_stringpool_str118, 124},
#line 43 "unictype/scripts_byname.gperf"
    {(int)(size_t)&((struct script_stringpool_t *)0)->script_stringpool_str119, 12},
#line 164 "unictype/scripts_byname.gperf"
    {(int)(size_t)&((struct script_stringpool_t *)0)->script_stringpool_str120, 133},
#line 88 "unictype/scripts_byname.gperf"
    {(int)(size_t)&((struct script_stringpool_t *)0)->script_stringpool_str121, 57},
#line 106 "unictype/scripts_byname.gperf"
    {(int)(size_t)&((struct script_stringpool_t *)0)->script_stringpool_str122, 75},
#line 147 "unictype/scripts_byname.gperf"
    {(int)(size_t)&((struct script_stringpool_t *)0)->script_stringpool_str123, 116},
#line 89 "unictype/scripts_byname.gperf"
    {(int)(size_t)&((struct script_stringpool_t *)0)->script_stringpool_str124, 58},
#line 170 "unictype/scripts_byname.gperf"
    {(int)(size_t)&((struct script_stringpool_t *)0)->script_stringpool_str125, 139},
#line 85 "unictype/scripts_byname.gperf"
    {(int)(size_t)&((struct script_stringpool_t *)0)->script_stringpool_str126, 54},
#line 82 "unictype/scripts_byname.gperf"
    {(int)(size_t)&((struct script_stringpool_t *)0)->script_stringpool_str127, 51},
#line 191 "unictype/scripts_byname.gperf"
    {(int)(size_t)&((struct script_stringpool_t *)0)->script_stringpool_str128, 160},
#line 173 "unictype/scripts_byname.gperf"
    {(int)(size_t)&((struct script_stringpool_t *)0)->script_stringpool_str129, 142},
    {-1},
#line 79 "unictype/scripts_byname.gperf"
    {(int)(size_t)&((struct script_stringpool_t *)0)->script_stringpool_str131, 48},
#line 140 "unictype/scripts_byname.gperf"
    {(int)(size_t)&((struct script_stringpool_t *)0)->script_stringpool_str132, 109},
#line 174 "unictype/scripts_byname.gperf"
    {(int)(size_t)&((struct script_stringpool_t *)0)->script_stringpool_str133, 143},
    {-1},
#line 113 "unictype/scripts_byname.gperf"
    {(int)(size_t)&((struct script_stringpool_t *)0)->script_stringpool_str135, 82},
#line 180 "unictype/scripts_byname.gperf"
    {(int)(size_t)&((struct script_stringpool_t *)0)->script_stringpool_str136, 149},
#line 65 "unictype/scripts_byname.gperf"
    {(int)(size_t)&((struct script_stringpool_t *)0)->script_stringpool_str137, 34},
#line 74 "unictype/scripts_byname.gperf"
    {(int)(size_t)&((struct script_stringpool_t *)0)->script_stringpool_str138, 43},
#line 76 "unictype/scripts_byname.gperf"
    {(int)(size_t)&((struct script_stringpool_t *)0)->script_stringpool_str139, 45},
#line 197 "unictype/scripts_byname.gperf"
    {(int)(size_t)&((struct script_stringpool_t *)0)->script_stringpool_str140, 166},
#line 36 "unictype/scripts_byname.gperf"
    {(int)(size_t)&((struct script_stringpool_t *)0)->script_stringpool_str141, 5},
#line 152 "unictype/scripts_byname.gperf"
    {(int)(size_t)&((struct script_stringpool_t *)0)->script_stringpool_str142, 121},
#line 200 "unictype/scripts_byname.gperf"
    {(int)(size_t)&((struct script_stringpool_t *)0)->script_stringpool_str143, 169},
#line 34 "unictype/scripts_byname.gperf"
    {(int)(size_t)&((struct script_stringpool_t *)0)->script_stringpool_str144, 3},
#line 42 "unictype/scripts_byname.gperf"
    {(int)(size_t)&((struct script_stringpool_t *)0)->script_stringpool_str145, 11},
#line 161 "unictype/scripts_byname.gperf"
    {(int)(size_t)&((struct script_stringpool_t *)0)->script_stringpool_str146, 130},
    {-1},
#line 187 "unictype/scripts_byname.gperf"
    {(int)(size_t)&((struct script_stringpool_t *)0)->script_stringpool_str148, 156},
#line 142 "unictype/scripts_byname.gperf"
    {(int)(size_t)&((struct script_stringpool_t *)0)->script_stringpool_str149, 111},
#line 60 "unictype/scripts_byname.gperf"
    {(int)(size_t)&((struct script_stringpool_t *)0)->script_stringpool_str150, 29},
#line 118 "unictype/scripts_byname.gperf"
    {(int)(size_t)&((struct script_stringpool_t *)0)->script_stringpool_str151, 87},
#line 70 "unictype/scripts_byname.gperf"
    {(int)(size_t)&((struct script_stringpool_t *)0)->script_stringpool_str152, 39},
#line 63 "unictype/scripts_byname.gperf"
    {(int)(size_t)&((struct script_stringpool_t *)0)->script_stringpool_str153, 32},
#line 59 "unictype/scripts_byname.gperf"
    {(int)(size_t)&((struct script_stringpool_t *)0)->script_stringpool_str154, 28},
#line 150 "unictype/scripts_byname.gperf"
    {(int)(size_t)&((struct script_stringpool_t *)0)->script_stringpool_str155, 119},
#line 127 "unictype/scripts_byname.gperf"
    {(int)(size_t)&((struct script_stringpool_t *)0)->script_stringpool_str156, 96},
#line 121 "unictype/scripts_byname.gperf"
    {(int)(size_t)&((struct script_stringpool_t *)0)->script_stringpool_str157, 90},
#line 153 "unictype/scripts_byname.gperf"
    {(int)(size_t)&((struct script_stringpool_t *)0)->script_stringpool_str158, 122},
#line 104 "unictype/scripts_byname.gperf"
    {(int)(size_t)&((struct script_stringpool_t *)0)->script_stringpool_str159, 73},
#line 128 "unictype/scripts_byname.gperf"
    {(int)(size_t)&((struct script_stringpool_t *)0)->script_stringpool_str160, 97},
#line 71 "unictype/scripts_byname.gperf"
    {(int)(size_t)&((struct script_stringpool_t *)0)->script_stringpool_str161, 40},
#line 148 "unictype/scripts_byname.gperf"
    {(int)(size_t)&((struct script_stringpool_t *)0)->script_stringpool_str162, 117},
#line 181 "unictype/scripts_byname.gperf"
    {(int)(size_t)&((struct script_stringpool_t *)0)->script_stringpool_str163, 150},
#line 194 "unictype/scripts_byname.gperf"
    {(int)(size_t)&((struct script_stringpool_t *)0)->script_stringpool_str164, 163},
#line 120 "unictype/scripts_byname.gperf"
    {(int)(size_t)&((struct script_stringpool_t *)0)->script_stringpool_str165, 89},
#line 119 "unictype/scripts_byname.gperf"
    {(int)(size_t)&((struct script_stringpool_t *)0)->script_stringpool_str166, 88},
#line 136 "unictype/scripts_byname.gperf"
    {(int)(size_t)&((struct script_stringpool_t *)0)->script_stringpool_str167, 105},
#line 149 "unictype/scripts_byname.gperf"
    {(int)(size_t)&((struct script_stringpool_t *)0)->script_stringpool_str168, 118},
#line 141 "unictype/scripts_byname.gperf"
    {(int)(size_t)&((struct script_stringpool_t *)0)->script_stringpool_str169, 110},
#line 182 "unictype/scripts_byname.gperf"
    {(int)(size_t)&((struct script_stringpool_t *)0)->script_stringpool_str170, 151},
    {-1}, {-1},
#line 179 "unictype/scripts_byname.gperf"
    {(int)(size_t)&((struct script_stringpool_t *)0)->script_stringpool_str173, 148},
#line 55 "unictype/scripts_byname.gperf"
    {(int)(size_t)&((struct script_stringpool_t *)0)->script_stringpool_str174, 24},
#line 165 "unictype/scripts_byname.gperf"
    {(int)(size_t)&((struct script_stringpool_t *)0)->script_stringpool_str175, 134},
    {-1}, {-1}, {-1},
#line 48 "unictype/scripts_byname.gperf"
    {(int)(size_t)&((struct script_stringpool_t *)0)->script_stringpool_str179, 17},
#line 175 "unictype/scripts_byname.gperf"
    {(int)(size_t)&((struct script_stringpool_t *)0)->script_stringpool_str180, 144},
    {-1},
#line 184 "unictype/scripts_byname.gperf"
    {(int)(size_t)&((struct script_stringpool_t *)0)->script_stringpool_str182, 153},
    {-1}, {-1}, {-1}, {-1},
#line 111 "unictype/scripts_byname.gperf"
    {(int)(size_t)&((struct script_stringpool_t *)0)->script_stringpool_str187, 80},
#line 168 "unictype/scripts_byname.gperf"
    {(int)(size_t)&((struct script_stringpool_t *)0)->script_stringpool_str188, 137},
    {-1},
#line 135 "unictype/scripts_byname.gperf"
    {(int)(size_t)&((struct script_stringpool_t *)0)->script_stringpool_str190, 104},
#line 176 "unictype/scripts_byname.gperf"
    {(int)(size_t)&((struct script_stringpool_t *)0)->script_stringpool_str191, 145},
    {-1}, {-1}, {-1},
#line 178 "unictype/scripts_byname.gperf"
    {(int)(size_t)&((struct script_stringpool_t *)0)->script_stringpool_str195, 147},
    {-1}, {-1}, {-1}, {-1}, {-1}, {-1}, {-1}, {-1}, {-1},
    {-1},
#line 193 "unictype/scripts_byname.gperf"
    {(int)(size_t)&((struct script_stringpool_t *)0)->script_stringpool_str206, 162},
    {-1}, {-1}, {-1},
#line 117 "unictype/scripts_byname.gperf"
    {(int)(size_t)&((struct script_stringpool_t *)0)->script_stringpool_str210, 86},
    {-1},
#line 102 "unictype/scripts_byname.gperf"
    {(int)(size_t)&((struct script_stringpool_t *)0)->script_stringpool_str212, 71},
    {-1}, {-1}, {-1},
#line 68 "unictype/scripts_byname.gperf"
    {(int)(size_t)&((struct script_stringpool_t *)0)->script_stringpool_str216, 37},
#line 146 "unictype/scripts_byname.gperf"
    {(int)(size_t)&((struct script_stringpool_t *)0)->script_stringpool_str217, 115},
    {-1}, {-1}, {-1},
#line 99 "unictype/scripts_byname.gperf"
    {(int)(size_t)&((struct script_stringpool_t *)0)->script_stringpool_str221, 68},
#line 188 "unictype/scripts_byname.gperf"
    {(int)(size_t)&((struct script_stringpool_t *)0)->script_stringpool_str222, 157},
    {-1}, {-1}, {-1},
#line 90 "unictype/scripts_byname.gperf"
    {(int)(size_t)&((struct script_stringpool_t *)0)->script_stringpool_str226, 59},
#line 169 "unictype/scripts_byname.gperf"
    {(int)(size_t)&((struct script_stringpool_t *)0)->script_stringpool_str227, 138},
    {-1}, {-1}, {-1}, {-1}, {-1}, {-1}, {-1}, {-1}, {-1},
#line 98 "unictype/scripts_byname.gperf"
    {(int)(size_t)&((struct script_stringpool_t *)0)->script_stringpool_str237, 67},
    {-1}, {-1}, {-1}, {-1}, {-1},
#line 56 "unictype/scripts_byname.gperf"
    {(int)(size_t)&((struct script_stringpool_t *)0)->script_stringpool_str243, 25},
    {-1}, {-1},
#line 86 "unictype/scripts_byname.gperf"
    {(int)(size_t)&((struct script_stringpool_t *)0)->script_stringpool_str246, 55},
    {-1}, {-1}, {-1}, {-1}, {-1}, {-1}, {-1}, {-1}, {-1},
    {-1}, {-1}, {-1}, {-1}, {-1}, {-1}, {-1}, {-1}, {-1},
#line 160 "unictype/scripts_byname.gperf"
    {(int)(size_t)&((struct script_stringpool_t *)0)->script_stringpool_str265, 129},
    {-1}, {-1}, {-1}, {-1}, {-1}, {-1}, {-1}, {-1}, {-1},
    {-1}, {-1}, {-1}, {-1}, {-1}, {-1}, {-1}, {-1}, {-1},
    {-1}, {-1}, {-1}, {-1}, {-1}, {-1}, {-1},
#line 138 "unictype/scripts_byname.gperf"
    {(int)(size_t)&((struct script_stringpool_t *)0)->script_stringpool_str291, 107}
  };

static const struct named_script *
uc_script_lookup (register const char *str, register size_t len)
{
  if (len <= MAX_WORD_LENGTH && len >= MIN_WORD_LENGTH)
    {
      register unsigned int key = scripts_hash (str, len);

      if (key <= MAX_HASH_VALUE)
        {
          register int o = script_names[key].name;
          if (o >= 0)
            {
              register const char *s = o + script_stringpool;

              if (*str == *s && !strcmp (str + 1, s + 1))
                return &script_names[key];
            }
        }
    }
  return 0;
}
