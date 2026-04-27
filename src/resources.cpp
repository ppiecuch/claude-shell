#include "util/incbin.h"

INCBIN(AppLogo, SOURCE_DIR "/logo.png");
INCBIN(KomsoftLogo, SOURCE_DIR "/komsoftlogo.png");
INCBIN(MenuIcon, SOURCE_DIR "/menu32.png");

// Frontend files served via HTTP
INCTXT(FrontendHtml, SOURCE_DIR "/frontend/index.html");
INCTXT(FrontendCss,  SOURCE_DIR "/frontend/style.css");
INCTXT(FrontendJs,   SOURCE_DIR "/frontend/app.js");
