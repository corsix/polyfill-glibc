
enum keyword_t {
KW__INIT = 2,
#define KW_ENUM(_, k, ...) KW_##k##__, KW_##k = KW_##k##__ + ((sizeof(#k) + 7) >> 2),
KWS(KW_ENUM)
#undef KW_ENUM
KW__END
};

static void enter_keywords(sht_t* sht) {
  const char* init =
#define KWS_STR(l, s, ...) l #s
  KWS(KWS_STR)
#undef KWS_STR
  ;
  uint32_t n;
  for (; (n = (unsigned char)*init++); init += n) {
    (void)sht_intern_u(sht, init, n);
  }
}
