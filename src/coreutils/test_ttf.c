#include "stb_math.h"
#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"
#include "ubuntu_font.h"

int main() {
    stbtt_fontinfo font;
    stbtt_InitFont(&font, usr_share_fonts_truetype_ubuntu_Ubuntu_R_ttf, 0);
    return 0;
}
