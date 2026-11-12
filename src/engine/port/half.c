/* The half-float conversions clang calls for __fp16, mapped to libgcc's. */
float __gnu_h2f_ieee(unsigned short h);
unsigned short __gnu_f2h_ieee(float f);

float __aeabi_h2f(unsigned short h) { return __gnu_h2f_ieee(h); }
unsigned short __aeabi_f2h(float f) { return __gnu_f2h_ieee(f); }
