// Numeric reference and contrast tests for perceptual theme generation.
namespace perceptual_color_checks {
inline float linear(uint8_t c) noexcept {
    const float s=c/255.f;
    return s<=.04045f ? s/12.92f : std::pow((s+.055f)/1.055f,2.4f);
}
inline float luminance(etch::Color c) noexcept { return .2126f*linear(c.r)+.7152f*linear(c.g)+.0722f*linear(c.b); }
inline float contrast(etch::Color a, etch::Color b) noexcept {
    const float x=luminance(a)+.05f,y=luminance(b)+.05f;
    return x>y ? x/y : y/x;
}
inline bool verify() noexcept {
    using namespace etch;
    const auto red=Color::oklch(.62795536f,.25768331f,29.233885f);
    bool ok=red.r==255 && red.g<=1 && red.b<=1;
    const auto middle=Color::lerp_oklab(Color::black(),Color::white(),.5f);
    ok &= middle.r>=98 && middle.r<=100 && middle.r==middle.g && middle.g==middle.b;
    ok &= Color::oklch(NAN,0,0).a==0 && Color::oklch(.5f,INFINITY,0).a==0;
    for(unsigned r=0;r<256;r+=51) for(unsigned g=0;g<256;g+=51) for(unsigned b=0;b<256;b+=51) {
        const Color seed=Color::rgba8(uint8_t(r),uint8_t(g),uint8_t(b));
        // Conversion through Oklab and back must retain 8-bit sRGB endpoints.
        const Color copy=Color::lerp_oklab(seed,seed,.5f);
        ok &= copy.pack_rgba8()==seed.pack_rgba8();
        for(bool dark : {false,true}) {
            const Theme th=Theme::from_seed(seed,dark,2);
            ok &= contrast(th.text_primary,th.bg_canvas)>=4.5f && contrast(th.on_primary,th.accent_primary)>=4.5f;
            const EtchTheme ct=etch_theme_from_seed(etch_rgba8(seed.r,seed.g,seed.b,seed.a),dark,2);
            ok &= ct.accent.r==th.accent.r && ct.accent.g==th.accent.g && ct.accent.b==th.accent.b;
        }
    }
    ok &= SpringSpec::bouncy().damping_ratio<1 && SpringSpec::gentle().response>SpringSpec::snappy().response;
    if(!ok) std::fprintf(stderr,"perceptual color regression failed\n");
    return ok;
}
}
