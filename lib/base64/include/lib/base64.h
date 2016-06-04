#ifndef BASE64_H
#define BASE64_H

#define BASE64_ENCODED_SIZE(n) (ROUNDUP(4*((n)/3)+1, 4)+1)

int
b64_ntop(unsigned char const *src, size_t srclength, char *target, size_t targsize);

int
b64_pton(
  char const *src,
  unsigned char *target,
  size_t targsize
  );

#endif // BASE64_H
